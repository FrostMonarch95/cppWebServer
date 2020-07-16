#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <bits/stdc++.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <signal.h>
#include "lst_timer.h"

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "ssl.h"

//muduo 库
#include "muduo/net/TcpServer.h"

#include "muduo/base/AsyncLogging.h"
#include "muduo/base/Logging.h"
#include "muduo/base/Thread.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"

#include <functional>
#include <utility>

#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

#define TIMESLOT 5
#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

static int pipefd[2];   //我们使用这个管道来定时调用alarm函数.
static sort_timer_lst timer_lst;    //使用了基于升序链表的定时器.
static std::unordered_set<int> ms;   //我们需要用这个来存储accept的sslfd

//extern int addfd( int epollfd, int fd, bool one_shot );
//extern int removefd( int epollfd, int fd );
extern int setnonblocking(int);
void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}
int kRollSize = 500*1000*1000;  //回滚的大小.
std::unique_ptr<muduo::AsyncLogging> g_asyncLog;

void asyncOutput(const char* msg, int len)
{
  g_asyncLog->append(msg, len);
}
void setLogging(const char* argv0)
{
  muduo::Logger::setOutput(asyncOutput);
  char name[256];
  strncpy(name, argv0, 256);
  g_asyncLog.reset(new muduo::AsyncLogging(::basename(name), kRollSize));
  g_asyncLog->start();
}

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}
void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

void addsig( int sig )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}
void cb_func( http_conn* user_data )  //超时我们需要在这里关掉链接.
{
    user_data->close_conn();
    printf( "user time out close fd \n" );
}
void timer_handler()
{
    timer_lst.tick();
    alarm( TIMESLOT );
}
int main( int argc, char* argv[] )
{
    ssl_init(); //to be finished
    setLogging(argv[0]);
    LOG_INFO<<"server start pid "<<getpid()<<'\n';
    printf("server start \n");
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    addsig( SIGPIPE, SIG_IGN );

    threadpool< http_conn >* pool = NULL;
    try
    {
        pool = new threadpool< http_conn >;
    }
    catch( ... )
    {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ];
    assert( users );
    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    int sslfd=socket(PF_INET, SOCK_STREAM, 0 ); //we need ssl fd to support ssl reading

    assert( listenfd >= 0 );
    assert(sslfd>=0);

    struct linger tmp = { 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );//当调用close的时候 必须等待我方发送好fin或者等待0秒.

    int keepalive = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive , sizeof(keepalive ));
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    ///////////////SSL port binding
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port+1 ); //SSL port equals to http port + 1
    ret=bind(sslfd,( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );
    ///////////////

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );
    ret = listen( sslfd, 5 );
    assert( ret >= 0 );


    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    
    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );    //创建双向管道.
    assert( ret != -1 );
    setnonblocking( pipefd[1] );
    addfd( epollfd, pipefd[0],false ,true);
    addfd( epollfd, listenfd, false ,true);
    addfd(epollfd,sslfd,false,true);

    addsig( SIGALRM );
    addsig( SIGTERM );
    bool timeout = false;
    bool stop_server = false;
    alarm( TIMESLOT );

    http_conn::m_epollfd = epollfd;

    while( !stop_server )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if(sockfd == sslfd){    //sslfd receive message
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( sockfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                assert(!ms.count(connfd));  
                ms.insert(connfd);
                printf("epoll fd is %d,connfd is %d\n",epollfd,connfd);
                ssl_prepare_to_write(epollfd,connfd);   //... to be finished

            }else if( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                
                printf("accepting new fd %d\n",connfd);
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD )
                {
                    show_error( connfd, "Internal server busy" );
                    continue;
                }
                
                users[connfd].init( connfd, client_address );

                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer( timer );
            }
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                if(ms.count(sockfd)){
                    printf("error epoll\n");
                    ms.erase(sockfd);
                    ssl_close_client(sockfd); //... to be finished
                }else users[sockfd].close_conn();
            }else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 )
                {
                    // handle the error
                    printf("signal error\n");
                    continue;
                }
                else if( ret == 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGALRM:
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else if( events[i].events & EPOLLIN )
            {
                if(ms.count(sockfd))continue;   //sslfd we just dont care those input

                util_timer* timer = users[sockfd].timer;
                if( users[sockfd].read() )
                {
                     if( timer )
                    {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;
                        printf( "adjust timer once\n" );
                        timer_lst.adjust_timer( timer );
                    }
                    pool->append( users + sockfd );
                }
                else
                {
                    if( timer ) timer_lst.del_timer( timer );
                   
                    users[sockfd].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT )
            {
                if(ms.count(sockfd)){
                    ssl_write(sockfd);  //to be finished we need to write message to the client for ssl
                    ssl_close_client(sockfd);  //
                    ms.erase(sockfd);   
                }
                else if( !users[sockfd].write() )   //common http 
                {
                    users[sockfd].close_conn();
                }
            }
            else{
                printf("uncatch io operation \n");

            }
        }
        if( timeout )
        {
            timer_handler();
            timeout = false;
        }
    }
    ssl_out();  //to be finished
    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    close( pipefd[1] );
    close( pipefd[0] );
    return 0;
}
