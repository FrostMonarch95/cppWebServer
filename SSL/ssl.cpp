#include "ssl.h"
int create_socket(int port)
{
    int s;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
	perror("Unable to create socket");
	exit(EXIT_FAILURE);
    }

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
	perror("Unable to bind");
	exit(EXIT_FAILURE);
    }

    if (listen(s, 1) < 0) {
	perror("Unable to listen");
	exit(EXIT_FAILURE);
    }

    return s;
}

void init_openssl()
{ 
    SSL_load_error_strings();	
    OpenSSL_add_ssl_algorithms();

}

void cleanup_openssl()
{
    EVP_cleanup();
}

SSL_CTX *create_context()
{
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_server_method();

    ctx = SSL_CTX_new(method);
    if (!ctx) {
	perror("Unable to create SSL context");
	ERR_print_errors_fp(stderr);
	exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_context(SSL_CTX *ctx)
{
    SSL_CTX_set_ecdh_auto(ctx, 1);

    /* Set the key and cert */
    if (SSL_CTX_use_certificate_file(ctx, "/usr/local/ssl/certs/cert.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
	    exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "/usr/local/ssl/certs/key.pem", SSL_FILETYPE_PEM) <= 0 ) {
        ERR_print_errors_fp(stderr);
	    exit(EXIT_FAILURE);
    }
}
static SSL_CTX *ctx;
static int only_epoll_fd=-1;    //just one epoll is using os hm... bad example
static std::map<int,SSL *> mm;
void ssl_init(){
    init_openssl();
    ctx = create_context();
    configure_context(ctx);
    printf("ssl init complete\n");
}
//we are going to ssl handshake and register write event to epoll
std::string getOpenSSLError()
{
    BIO *bio = BIO_new(BIO_s_mem());
    ERR_print_errors(bio);
    char *buf;
    size_t len = BIO_get_mem_data(bio, &buf);
    std::string ret(buf,len);
    BIO_free(bio);
    return ret;
}
void ssl_prepare_to_write(int epoll_fd,int client){

    only_epoll_fd=epoll_fd;
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt( client, SOL_SOCKET, SO_ERROR, &error, &len );
    int reuse = 1;
    setsockopt( client, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ));
    addfd( epoll_fd, client, true,false );

    SSL *ssl;
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, client);
    int ssl_ret;
    while((ssl_ret=SSL_accept(ssl))<=0){
        
        int ssl_conn_err = SSL_get_error(ssl, ssl_ret);
        printf("ssl accept error %d\n",ssl_ret);
        if(ssl_conn_err == SSL_ERROR_WANT_READ)printf("accept want read error\n");
    }
    modfd(epoll_fd,client,EPOLLOUT);
    
    mm[client]=ssl;
    printf("ssl register ssl %d complete\n",ssl);
}
void ssl_close_client(int client){
    if(!mm.count(client))return;
    auto ssl=mm[client];
    SSL_shutdown(ssl);
    SSL_free(ssl);
    printf("shutdown close ssl %d\n",ssl);
    mm.erase(client);
    removefd( only_epoll_fd, client );
}
void ssl_write(int client){
    if(!mm.count(client))return;
    auto ssl=mm[client];
    const char reply[] = "ssl test\n";
    printf("ssl send\n");
    SSL_write(ssl, reply, strlen(reply));
}
void ssl_out(){
    SSL_CTX_free(ctx);
    cleanup_openssl();
}
/*
int main(int argc, char **argv)
{
    int sock;
    SSL_CTX *ctx;

    init_openssl();
    ctx = create_context();

    configure_context(ctx);

    sock = create_socket(4433);

    while(1) {
        struct sockaddr_in addr;
        uint len = sizeof(addr);
        SSL *ssl;
        const char reply[] = "test\n";

        int client = accept(sock, (struct sockaddr*)&addr, &len);
        if (client < 0) {
            perror("Unable to accept");
            exit(EXIT_FAILURE);
        }

        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client);

        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        }
        else {
            SSL_write(ssl, reply, strlen(reply));
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);
    }

    close(sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();
}
*/