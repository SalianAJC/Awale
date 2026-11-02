
#include <stdio.h>
#include <winsock2.h>
#include <string.h>

int main(void)
{
    WSADATA wsa;
    SOCKET server_socket;
    SOCKET client_socket;
    struct sockaddr_in server_addr;
    int addr_len = sizeof(server_addr);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket == INVALID_SOCKET) {
        printf("Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5050);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, 5) == SOCKET_ERROR) {
        printf("Listen failed.\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Server listening on port 5050.\n");

    client_socket = accept(server_socket, (struct sockaddr *)&server_addr,
                           &addr_len);

    if (client_socket == INVALID_SOCKET) {
        printf("Accept failed.\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Client connected!\n");

    const char *message = "CONNECTED\n";
    send(client_socket, message, (int)strlen(message), 0);

    closesocket(client_socket);
    closesocket(server_socket);
    WSACleanup();

    return 0;
}
