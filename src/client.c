
#include <stdio.h>
#include <winsock2.h>
#include <string.h>

#include "network.h"


int register_acc(SOCKET client_socket){
    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;

    printf("Enter username: ");

    if (fgets(username, sizeof(username), stdin) == NULL) {
        printf("Error reading username.\n");
        return -1;
    }

    size_t length = strlen(username);

    if (length > 0 && username[length - 1] == '\n') {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin)) {
        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF) {
        }

        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0') {
        printf("Username cannot be empty.\n");
        return -1;
    }

    strcpy(message.type, MSG_REGISTER);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(client_socket, &message) != 0) {
        printf("Failed to send registration request.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0) {
        printf("Failed to receive registration response.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_REGISTER) != 0) {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response == RESP_SUCCESS) {
        printf("Registration successful!\n");
        return 0;
    } else {
        printf("Registration failed: %s\n", response.content);
        return -1;
    }
}


int log_in(SOCKET client_socket){
    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;
    Message onl_list;

    printf("Enter username: ");

    if (fgets(username, sizeof(username), stdin) == NULL) {
        printf("Error reading username.\n");
        return -1;
    }

    size_t length = strlen(username);

    if (length > 0 && username[length - 1] == '\n') {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin)) {
        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF) {
        }

        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0') {
        printf("Username cannot be empty.\n");
        return -1;
    }

    strcpy(message.type, MSG_LOGIN);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(client_socket, &message) != 0) {
        printf("Failed to send login request.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0) {
        printf("Failed to receive login response.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_LOGIN) != 0) {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response != RESP_SUCCESS) {
        printf("Login failed: %s\n", response.content);
        return -1;
    }

    printf("Login successful!\n");
    printf("Welcome, %s!\n", username);

    if (receive_message(client_socket, &onl_list) != 0) {
        printf("Failed to receive online players list.\n");
        return -1;
    }

    if (strcmp(onl_list.type, MSG_PLAYERS_ONLINE) == 0) {
        printf("\n===== ONLINE PLAYERS =====\n");
        printf("%s\n", onl_list.content);
    } else {
        printf("Unexpected online players response.\n");
    }

    return 0;
}


int lobby_menu(SOCKET client_socket){

    char choice[16];
    Message message = {0};
    Message response;

    while (1) {
        printf("\n===== AWALE LOBBY =====\n");
        printf("1. Show online players\n");
        printf("2. Quit\n");
        printf("Choose an option: ");

        if (fgets(choice, sizeof(choice), stdin) == NULL) {
            return 0;
        }

        if (strcmp(choice, "1\n") == 0) {

            strcpy(message.type, MSG_LIST_ONLINE);
            message.response = RESP_NONE;
            message.mode = MODE_NONE;
            message.receiver = 0;
            message.content[0] = '\0';

            if (send_message(client_socket, &message) != 0) {
                printf("Failed to request online players.\n");
                return 0;
            }

            if (receive_message(client_socket, &response) != 0) {
                printf("Failed to receive online players.\n");
                return 0;
            }

            if (strcmp(response.type, MSG_LIST_ONLINE) == 0 &&
                response.response == RESP_SUCCESS) {

                printf("\n===== ONLINE PLAYERS =====\n");
                printf("%s\n", response.content);

            } else {
                printf("Failed to retrieve online players.\n");
            }

        } else if (strcmp(choice, "2\n") == 0) {

            return 0;

        } else {
            printf("Invalid option. Please try again.\n");
        }
    }
}


int initial_menu(SOCKET client_socket) {

    char choice[16];

    while (1){
        printf("Welcome to the game!\n");
        printf("Sam And Omar present:\n");
        printf("\n===== AWALE =====\n");

        printf("1. Register\n");
        printf("2. Login\n");
        printf("3. Exit\n");
        printf("Choose an option: ");

        if (fgets(choice, sizeof(choice), stdin) == NULL) {
            printf("Error reading input.\n");
            return 0;
        }

        // Process the user's choice
        if (strcmp(choice, "1\n") == 0) {
            // Handle registration
            if (register_acc(client_socket) == 0)
            {
                printf("\nReturning to main menu...\n");
            }
            continue;

        } else if (strcmp(choice, "2\n") == 0) {
            return 2;

        } else if (strcmp(choice, "3\n") == 0) {
            // Handle exit
            return 0;

        } else {
            printf("Invalid option. Please try again.\n");
        }
    }
}


int main(void)
{
    WSADATA wsa;
    SOCKET client_socket;
    struct sockaddr_in server_addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    client_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (client_socket == INVALID_SOCKET) {
        printf("Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(client_socket, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    printf("Connected to server.\n");

    int menu_choice = initial_menu(client_socket);

    if (menu_choice == 2) {
        if (log_in(client_socket) == 0) {
            printf("\nLogin completed. Welcome to the lobby!\n");
            lobby_menu(client_socket);
        }
    }

    closesocket(client_socket);
    WSACleanup();

    return 0;
}
