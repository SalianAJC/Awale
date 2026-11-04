
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "network.h"

//Sending  full message
static int send_all(SOCKET socket, const char *buffer, int length)
{
    int sent = 0;

    while (sent < length) {
        int n = send(socket, buffer + sent, length - sent, 0);

        if (n <= 0) {
            return -1;
        }

        sent += n;
    }

    return 0;
}

int send_message(SOCKET socket, const Message *message) //Action of sending a message
{//Takes a socket and a message
    char buffer[MAX_LINE + 1]; //Max mesage length
    int length;//Length of the message

    if (message == NULL) { //If it's null return nothing
        return -1;
    }

    size_t type_len = strlen(message->type);
    size_t content_len = strlen(message->content);

    if (type_len < 1 || type_len > MAX_TYPE) return -1;
    if (content_len > MAX_CONTENT) return -1;

    //Check f there's invalid characters in the message type or content
    if (strchr(message->type, MSG_END) != NULL ||
        strchr(message->type, MSG_SEP) != NULL ||
        strchr(message->content, MSG_END) != NULL)
    {
        return -1;
    }

    //Format the message and store it in a buffer which will be called length

    length = snprintf(buffer,
                      sizeof(buffer),
                      "%s%c%c%c%c%c%d%c%s%c",
                      message->type,
                      MSG_SEP,
                      message->response,
                      MSG_SEP,
                      message->mode,
                      MSG_SEP,
                      message->receiver,
                      MSG_SEP,
                      message->content,
                      MSG_END);

    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        return -1;
    }

    return send_all(socket, buffer, length); //Send the message making sure it sends all of it
}

int receive_message(SOCKET socket, Message *message) { //Recieving a message
    if (message == NULL) return -1; //If there's nothing to receive return -1

    char buffer[MAX_LINE + 1];//Buffer to store the message, +1 for '\0'
    int length = 0;//For now the length is 0

    while (length < MAX_LINE) {//While length is smaller than the max line length
        char c; //Character to store the message

        int result = recv(socket, &c, 1, 0);//Recieve the message from the socket, store it in c and take it one byte at a time

        if (result <= 0) return -1; //if the result is smaller than or equal to 0 return -1

        if (c == MSG_END) break; //If the character is the end of the message break

        buffer[length++] = c;//buffer in length ++ position is equal to c
    }

    if (length == MAX_LINE) return -1; //If we reach the max message size we return -1

    buffer[length] = '\0'; //Null terminate the buffer

    char *fields[5]; //Pointer array to store our message
    fields[0] = buffer;//First field is the buffer

    int field_count = 1;//Current field is 1

    for (int i = 0; i < length; i++) {//For every character in the buffer
        if (buffer[i] == MSG_SEP && field_count < 5) { //If the character is a separator and we've got less than 5 fields
            buffer[i] = '\0'; //Replace the separator with a kill character
            fields[field_count++] = &buffer[i + 1];//Increase the field and set the next field to the next character in the buffer
        }
    }

    if (field_count != 5) return -1; //If we don't have 5 fields return -1

    size_t type_len = strlen(fields[0]); //Length of the message type
    size_t content_len = strlen(fields[4]); //Length of the content

    if (type_len < 1 || type_len > MAX_TYPE) return -1; //If the type length is less than 1 or greater than the max type length return -1
    if (strlen(fields[1]) != 1) return -1;//if the length of the response isn't 1 kill
    if (strlen(fields[2]) != 1) return -1;//if the length of the mode isn't 1 kill
    if (content_len > MAX_CONTENT) return -1; //If we exceed the max content length return -1

    char *end; //Pointer to the end of the string
    errno = 0;
    long receiver = strtol(fields[3], &end, 10); //Convert the receiver field to a long integer

    if (errno == ERANGE) return -1;
    if (fields[3][0] == '\0' || *end != '\0') return -1; //If the receiver field is empty or if there are any non-numeric characters return -1
    if (receiver < INT_MIN || receiver > INT_MAX) return -1;

    if (fields[1][0] != RESP_SUCCESS && //If the response field isn't success, failure or none return -1
        fields[1][0] != RESP_FAILURE &&
        fields[1][0] != RESP_NONE) {
        return -1;
    }

    if (fields[2][0] != MODE_OBSERVER && //If the mode isn't observer, player or none return -1
        fields[2][0] != MODE_PLAYER &&
        fields[2][0] != MODE_NONE) {
        return -1;
    }

    memcpy(message->type, fields[0], type_len + 1); //Copy the message type to the message struct, account for the null terminator
    message->response = fields[1][0]; //Set the response field in the message struct
    message->mode = fields[2][0]; //Set the mode in the message struct
    message->receiver = (int)receiver; //Set the receiver in its correct place in the struct
    memcpy(message->content, fields[4], content_len + 1); //Copy the content to the message struct, account for the null terminator

    return 0;
}
