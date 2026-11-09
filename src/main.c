
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "game.h"

int main(void)
{
    WSADATA wsa;
    SOCKET client_socket;
    struct sockaddr_in server_addr;
    int game_id;
    int side;
    int menu_choice;
    int login_status;
    int logged_in = 0;
    char opponent[MAX_NAME + 1];

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    client_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (client_socket == INVALID_SOCKET)
    {
        printf("Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(client_socket, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == SOCKET_ERROR)
    {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    printf("Connected to server.\n");

    // Start menu and login
    while (!logged_in)
    {
        menu_choice = initial_menu(client_socket);

        if (menu_choice == 0)
        {
            break;
        }

        if (menu_choice == -1)
        {
            printf("Connection lost. Exiting.\n");
            break;
        }

        if (menu_choice == 2)
        {
            login_status = log_in(client_socket);

            if (login_status == 0)
            {
                logged_in = 1;
                printf("\nLogin completed. Welcome to the lobby!\n");
            }
            else if (login_status == -1)
            {
                printf("\nReturning to main menu...\n");
            }
            else if (login_status == -2)
            {
                printf("\nConnection lost. Exiting.\n");
                break;
            }
        }
    }

    // Lobby and games
    if (logged_in)
    {
        while (1)
        {
            int lobby_status = lobby_menu(client_socket, &game_id, &side,
                                          opponent, sizeof(opponent));

            if (lobby_status != 2)
            {
                break;
            }

            if (play_game(client_socket, game_id, side, opponent) == -1)
            {
                break;
            }
        }
    }

    closesocket(client_socket);
    WSACleanup();

    return 0;
}