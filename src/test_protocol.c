
#include <stdio.h>
#include <string.h>

#include "network.h"

int main(void)
{
    WSADATA wsa;
    SOCKET listener = INVALID_SOCKET;
    SOCKET client = INVALID_SOCKET;
    SOCKET server = INVALID_SOCKET;
    struct sockaddr_in address;
    int address_length = sizeof(address);
    int result = 1;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener == INVALID_SOCKET) {
        printf("Listener socket failed\n");
        goto cleanup;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(listener, (struct sockaddr *)&address,
             sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        goto cleanup;
    }

    if (listen(listener, 1) == SOCKET_ERROR) {
        printf("Listen failed\n");
        goto cleanup;
    }

    if (getsockname(listener, (struct sockaddr *)&address,
                    &address_length) == SOCKET_ERROR) {
        printf("Getsockname failed\n");
        goto cleanup;
    }

    client = socket(AF_INET, SOCK_STREAM, 0);

    if (client == INVALID_SOCKET) {
        printf("Client socket failed\n");
        goto cleanup;
    }

    if (connect(client, (struct sockaddr *)&address,
                sizeof(address)) == SOCKET_ERROR) {
        printf("Connect failed\n");
        goto cleanup;
    }

    server = accept(listener, NULL, NULL);

    if (server == INVALID_SOCKET) {
        printf("Accept failed\n");
        goto cleanup;
    }

    
Message sent = {0};
Message sent2 = {0};
Message received = {0};
Message received2 = {0};
Message sent3 = {0};
Message received3 = {0};

strcpy(sent.type, MSG_MOVE);
sent.response = RESP_NONE;
sent.mode = MODE_PLAYER;
sent.receiver = 7;
strcpy(sent.content, "3");

strcpy(sent2.type, MSG_CHAT);
sent2.response = RESP_NONE;
sent2.mode = MODE_PLAYER;
sent2.receiver = 7;
strcpy(sent2.content, "Hola|desde|Awale");

strcpy(sent3.type, MSG_CHAT);
sent3.response = RESP_NONE;
sent3.mode = MODE_PLAYER;
sent3.receiver = 7;
strcpy(sent3.content, "Hola|que|tal");

if (send_message(client, &sent) != 0) {
    printf("First send failed\n");
    goto cleanup;
}

if (send_message(client, &sent2) != 0) {
    printf("Second send failed\n");
    goto cleanup;
}

if (send_message(client, &sent3) != 0) {
    printf("Third send failed\n");
    goto cleanup;
}

if (receive_message(server, &received) != 0) {
    printf("First receive failed\n");
    goto cleanup;
}

if (receive_message(server, &received2) != 0) {
    printf("Second receive failed\n");
    goto cleanup;
}

if (receive_message(server, &received3) != 0) {
    printf("Third receive failed\n");
    goto cleanup;
}

if (strcmp(sent.type, received.type) != 0 ||
    sent.response != received.response ||
    sent.mode != received.mode ||
    sent.receiver != received.receiver ||
    strcmp(sent.content, received.content) != 0) {
    printf("First message mismatch\n");
    goto cleanup;
}

if (strcmp(sent2.type, received2.type) != 0 ||
    sent2.response != received2.response ||
    sent2.mode != received2.mode ||
    sent2.receiver != received2.receiver ||
    strcmp(sent2.content, received2.content) != 0) {
    printf("Second message mismatch\n");
    goto cleanup;
}

if (strcmp(sent3.type, received3.type) != 0 ||
    sent3.response != received3.response ||
    sent3.mode != received3.mode ||
    sent3.receiver != received3.receiver ||
    strcmp(sent3.content, received3.content) != 0) {
    printf("Third message mismatch\n");
    goto cleanup;
}

printf("Third message received correctly!\n");
printf("Content: %s\n", received3.content);

printf("Both messages received correctly!\n");

printf("Message 1: %s | %c | %c | %d | %s\n",
       received.type, received.response, received.mode,
       received.receiver, received.content);

printf("Message 2: %s | %c | %c | %d | %s\n",
       received2.type, received2.response, received2.mode,
       received2.receiver, received2.content);



closesocket(client);
client = INVALID_SOCKET;

if (receive_message(server, &received3) != -1) {
    printf("Disconnect test failed\n");
    goto cleanup;
}

printf("Disconnect detected correctly!\n");
result = 0;


cleanup:
    if (server != INVALID_SOCKET)
        closesocket(server);

    if (client != INVALID_SOCKET)
        closesocket(client);

    if (listener != INVALID_SOCKET)
        closesocket(listener);

    WSACleanup();

    return result;
}
