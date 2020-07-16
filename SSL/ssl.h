#ifndef __MYSSL__
#define __MYSSL__

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/epoll.h>
#include <bits/stdc++.h>
#include "http_conn.h"
void ssl_init();
void ssl_prepare_to_write(int ,int);
void ssl_close_client(int );
void ssl_write(int);
void ssl_out();

#endif