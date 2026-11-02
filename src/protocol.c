
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <stdint.h>
#include "protocol.h"

#define MAX_PAYLOAD 212

static int send_all(SOCKET socket, const char *data, int length) { // Send all bytes of a message
    int sent = 0;

    while (sent < length) {
        int result = send(socket, data + sent, length - sent, 0); //While it's not done, send the rest of the message

        if (result <= 0) return -1; //If the send fails, return -1

        sent += result;
    }

    return 0;
}

static int recv_all(SOCKET socket, char *data, int length) { // Receive all bytes of a message
    int received = 0; //Counter for the number of bytes received

    while (received < length) { //While we haven't received the entire message, keep receiving
        int result = recv(socket, data + received, //recieve from socket, store in data
                          length - received, 0);

        if (result <= 0) return -1; //In failure return -1

        received += result;
    }

    return 0;
}

int send_message(SOCKET socket, const Message *message) { // Send a message over the socket
    if (message == NULL) return -1; // If the message is null return -1

    size_t type_len = strlen(message->type); //Get the byte length of the message type
    size_t content_len = strlen(message->content); //Get the byte length of the message content

    if (type_len < 1 || type_len > 2) return -1; //If the type is less than 1 or greater than 2 push error
    if (content_len > MAX_MESSAGE) return -1; //If the content is too long push error

    char payload[MAX_PAYLOAD]; // Create a payload buffer to hold the serialized message
    int pos = 0; //Start position counter

    payload[pos++] = (char)type_len; // Store the length of the type in the first byte of the payload

    memcpy(payload + pos, message->type, type_len); //
    pos += (int)type_len;

    uint16_t net_content_len = htons((uint16_t)content_len);
    memcpy(payload + pos, &net_content_len, 2);
    pos += 2;

    memcpy(payload + pos, message->content, content_len);
    pos += (int)content_len;

    uint32_t net_receiver = htonl((uint32_t)message->receiver);
    memcpy(payload + pos, &net_receiver, 4);
    pos += 4;

    payload[pos++] = message->response;
    payload[pos++] = message->mode;

    uint32_t net_length = htonl((uint32_t)pos);

    if (send_all(socket, (char *)&net_length, 4) != 0)
        return -1;

    if (send_all(socket, payload, pos) != 0)
        return -1;

    return 0;
}

int receive_message(SOCKET socket, Message *message) {
    if (message == NULL) return -1;

    uint32_t net_length;

    if (recv_all(socket, (char *)&net_length, 4) != 0)
        return -1;

    uint32_t length = ntohl(net_length);

    if (length < 9 || length > MAX_PAYLOAD)
        return -1;

    char payload[MAX_PAYLOAD];

    if (recv_all(socket, payload, (int)length) != 0)
        return -1;

    int pos = 0;

    uint8_t type_len = (uint8_t)payload[pos++];

    if (type_len < 1 || type_len > 2)
        return -1;

    if (pos + type_len + 2 > length)
        return -1;

    memcpy(message->type, payload + pos, type_len);
    message->type[type_len] = '\0';
    pos += type_len;

    uint16_t net_content_len;
    memcpy(&net_content_len, payload + pos, 2);
    pos += 2;

    uint16_t content_len = ntohs(net_content_len);

    if (content_len > MAX_MESSAGE)
        return -1;

    if ((uint32_t)pos + content_len + 6 != length)
        return -1;

    memcpy(message->content, payload + pos, content_len);
    message->content[content_len] = '\0';
    pos += content_len;

    uint32_t net_receiver;
    memcpy(&net_receiver, payload + pos, 4);
    message->receiver = (int32_t)ntohl(net_receiver);
    pos += 4;

    message->response = payload[pos++];
    message->mode = payload[pos++];

    return 0;
}
