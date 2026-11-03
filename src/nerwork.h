//Sending and receiving messages over a socket. Declarations


#ifndef NETWORK_H
#define NETWORK_H

#include "protocol.h"

#ifdef _WIN32 //Windows

#include <winsock2.h>

#else //Linux

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

typedef int SOCKET; //Windows calls a socket SOCKET, Linux uses an int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)

#endif

int send_message(SOCKET socket, const Message *message);
int receive_message(SOCKET socket, Message *message);

#endif