
#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <string.h>
#include <stdlib.h>

#include "network.h"

DWORD WINAPI client_thread(LPVOID arg) //This function will be the one that will be executed by the thread for each client connection. It will receive messages from the client and print them to the console.
{
    SOCKET client_socket = *(SOCKET *)arg; //Creating a socket for client connection
    free(arg); //Freeing the memory allocated for the socket

    Message message; //Creating a new message struct

    while (1) { //Eternal loop to keep our ears perched
        int result = receive_message(client_socket, &message);//Result being the message recieved from the client socket, pre stored in a message struct

        if (result != 0) { //If it's not 0it means we have a dictonection and thus the operation has failed
            printf("Client disconnected or communication failed.\n");
            break;
        }

        printf("Message received: %s | %c | %c | %d | %s\n", //Print our message
               message.type,
               message.response,
               message.mode,
               message.receiver,
               message.content);

        message.response = RESP_SUCCESS;
        if (send_message(client_socket, &message) != 0) {
            printf("Failed to send response.\n");
            break;
        }
    }

    closesocket(client_socket);//We close the socket

    return 0;//We return 0 to indicate that the thread has finished executing
}

int main(void) //Main
{
    WSADATA wsa; //A struct that contains information about the Windows Sockets implementation
    SOCKET server_socket; //Our server socket
    SOCKET client_socket; //Our client socket
    struct sockaddr_in server_addr; //Server adress
    struct sockaddr_in client_addr; //Client adress
    int addr_len = sizeof(client_addr); //Size of the client adress

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {//If the WSAStartup function fails, it returns a non-zero value. In that case, we print an error message and return 1 to indicate failure.
        printf("Winsock initialization failed.\n");
        return 1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);//Create the server socket using the socket function. Address, type and protocol.

    if (server_socket == INVALID_SOCKET) { //If it's invalid
        printf("Socket creation failed.\n");//We die
        WSACleanup();//We clean up the Winsock library
        return 1; //And show failure
    }

    memset(&server_addr, 0, sizeof(server_addr)); //Use server address to 0 to avoid garbage values
    server_addr.sin_family = AF_INET; //Set the address family to AF_INET for IPv4
    server_addr.sin_port = htons(SERVER_PORT); //Set the port number to SERVER_PORT, converting it to network byte order using htons
    server_addr.sin_addr.s_addr = INADDR_ANY; //Set the IP address to INADDR_ANY, which allows the server to accept connections on any available network interface

    if (bind(server_socket, (struct sockaddr *)&server_addr, // we asociate the server socket with the adress and given port. If this fails we die.
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, MAX_CLIENTS) == SOCKET_ERROR) { //We listen with the server socket and the max clients. If this fails we die.
        printf("Listen failed.\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Server listening on port %d.\n", SERVER_PORT); //Announce we're open to recieve connections

    while (1) { //Eternal loop to keep our ears perched
        addr_len = sizeof(client_addr);//Reset the address length for each new connection

        client_socket = accept(server_socket, //Accept clients with socke, client and adress
                               (struct sockaddr *)&client_addr,
                               &addr_len);

        if (client_socket == INVALID_SOCKET) { //If the socket is invalid we print error and keep going
            printf("Accept failed: %d\n", WSAGetLastError());
            continue;
        }

        printf("Client connected!\n"); //If not, it's succesful and we print it

        SOCKET *client_arg = malloc(sizeof(SOCKET)); //Allocate memory for the client socket argument to be passed to the thread

        if (client_arg == NULL) {//If the client's null we fail memory allocation
            printf("Memory allocation failed.\n");
            closesocket(client_socket);
            continue;
        }

        *client_arg = client_socket; //client_arg points to the client socket

        HANDLE thread = CreateThread( //We create a new thread to handle the client connection
            NULL,
            0,
            client_thread,
            client_arg,
            0,
            NULL
        );

        if (thread == NULL) { //If the thread is null we failed and print, we continue as not to break the server for other clients
            printf("Thread creation failed.\n");
            free(client_arg);
            closesocket(client_socket);
            continue;
        }

        CloseHandle(thread);
    }

    closesocket(server_socket);
    WSACleanup();

    return 0;
}
