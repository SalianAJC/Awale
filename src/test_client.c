
#include <stdio.h>
#include <winsock2.h>
#include <string.h>

#include "network.h"

int main(void)
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;
    Message message = {0};
    char name[32];

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    printf("Client name: ");
    scanf("%31s", name);

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connected!\n");

    strcpy(message.type, MSG_CHAT);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    strcpy(message.content, name);

    if (send_message(sock, &message) != 0) {
        printf("Send failed.\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Message sent. Press ENTER to disconnect.\n");
    getchar();
    getchar();

    closesocket(sock);
    WSACleanup();

    return 0;
}
