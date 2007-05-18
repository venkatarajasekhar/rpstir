#ifndef __SOCKET_STUFF_H
#define __SOCKET_STUFF_H

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>

#define LOCAL 0
#define TCP 1
#define UDP 2

struct write_port {
  int out_desc;
  struct sockaddr_in server_addr;
  struct hostent *host;
  int to_length;
  int protocol;
};

int tcpsocket(struct write_port *, int);
int udpsocket(struct write_port *, int);
int outputMsg(struct write_port *, char *, unsigned int);


#endif
