//Message send and recieve functions

//Sending  full message
#include <stdio.h>
#include <string.h>
#include "network.h"

static int send_all(SOCKET socket, const char *buffer, int length)
{
    int sent = 0;

    while (sent < length) {
        int n = send(socket, buffer + sent, length - sent, 0);

        if (n == SOCKET_ERROR || n == 0) {
            return 0;
        }

        sent += n;
    }

    return 1;
}


int send_message(SOCKET socket, const Message *message) //Action of sending a message
{//Takes a socket and a message
    char buffer[MAX_LINE]; //Max mesage length
    int length;//Length of the message

    if (message == NULL) { //If it's null return nothing
        return 0;
    }


    //Check f there's invalid characters in the message type or content
    if (strchr(message->type, '\n') != NULL || //Ceck if there's a line skip on the type
        strchr(message->type, '|') != NULL || //Check if there's a separator on the type
        strchr(message->content, '\n') != NULL) //Check if there'sa line skip on the content
        { 
        return 0;
    }

    //Format the message and store it in a buffer which will be called length

    length = snprintf(buffer, //Final location
                    sizeof(buffer), //Size of the buffer
                      "%s|%c|%c|%d|%s\n",//Format
                      message->type,//Message type
                      message->response,//Wether there's a response or not
                      message->mode, //Wether it's a player or observer
                      message->receiver, //The player id of the receiver
                      message->content); //The message itself

    if (length < 0 || (size_t)length >= sizeof(buffer)) { //If the length is negative or greater than the buffer size return nothing
        return 0;
    }

    return send_all(socket, buffer, length); //Send the message making sure it sends all of it
}



int receive_message(SOCKET socket, Message *message)
{
    char buffer[MAX_LINE];
    int received = 0;

    if (message == NULL) {
        return -1;
    }

    // Receive one byte at a time until newline
    while (received < MAX_LINE - 1) {
        char c;
        int n = recv(socket, &c, 1, 0);

        if (n <= 0) {
            return -1;
        }

        if (c == '\n') {
            break;
        }

        buffer[received++] = c;
    }

    buffer[received] = '\0';

    // Parse the received message
    if (sscanf(buffer, "%[^|]|%c|%c|%d|%[^\n]",
               message->type,
               &message->response,
               &message->mode,
               &message->receiver,
               message->content) != 5) {
        return -1;
    }

    return 0;
}


