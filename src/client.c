#include <stdio.h>
#include <winsock2.h>
#include <string.h>
#include <conio.h>//Reads unentered data

#include "network.h"
#include "game.h"

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


int challenge_player(SOCKET *client_socket){

    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;

    strcpy(message.type, MSG_LIST_ONLINE);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    message.content[0] = '\0';

    if (send_message(*client_socket, &message) != 0) {
        printf("Failed to request online players.\n");
        return -1;
    }

    if (receive_message(*client_socket, &response) != 0) {
        printf("Failed to receive online players.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_LIST_ONLINE) != 0 ||
        response.response != RESP_SUCCESS) {
        printf("Failed to retrieve online players.\n");
        return -1;
    }

    printf("\n===== ONLINE PLAYERS =====\n");
    printf("%s\n", response.content);

    printf("\nEnter the username of the player you want to challenge: ");

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

    memset(&message, 0, sizeof(message));

    strcpy(message.type, MSG_CHALLENGE);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(*client_socket, &message) != 0) {
        printf("Failed to send challenge request.\n");
        return -1;
    }

    if (receive_message(*client_socket, &response) != 0) {
        printf("Failed to receive challenge response.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_CHALLENGE) != 0) {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response == RESP_SUCCESS) {
        printf("Challenge sent to %s!\n", response.content);
        return 0;
    }

    printf("Challenge failed: %s\n", response.content);
    return -1;
}


int respond_to_challenge(SOCKET client_socket, char* challenger){

    char choice[16];
    Message message = {0};
    Message response;

    printf("\nAccept challenge from %s? (y/n): ", challenger);

    //Initial message checks, yes, no, or error

    if (fgets(choice, sizeof(choice), stdin) == NULL) {
        return -1;
    }

    if (choice[0] == 'y' || choice[0] == 'Y') {
        strcpy(message.type, MSG_ACCEPT);
    }

    else if (choice[0] == 'n' || choice[0] == 'N') {
        strcpy(message.type, MSG_REFUSE);
    }

    else {
        printf("Invalid choice.\n");
        return -1;
    }

    //Socket module

    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", challenger);

    //Response checks

    if (send_message(client_socket, &message) != 0) {
        printf("Failed to send challenge response.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0) {
        printf("Failed to receive server response.\n");
        return -1;
    }

    if (response.response != RESP_SUCCESS) {
        printf("Challenge response failed: %s\n", response.content);
        return -1;
    }

    if (strcmp(message.type, MSG_ACCEPT) == 0) {
        printf("Challenge accepted!\n");
    }
    else {
        printf("Challenge refused.\n");
    }

    return 0;
}


int socket_has_mail(SOCKET client_socket){

    fd_set readfds;

    struct timeval timeout; //We create a timeout for answers

    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; //Arbitrary max 100 milisecs

    int result = select(0, &readfds, NULL, NULL, &timeout);

    if (result == SOCKET_ERROR){
        return -1;
    }

    return result > 0;
}


int read_lobby_choice(char *choice, int size, SOCKET client_socket, Message *game_start) {

    int i = 0;

    while (1) {

        int socket_status = socket_has_mail(client_socket);

        if (socket_status == -1) {
            return -1;
        }

        if (socket_status == 1) {

            Message incoming = {0};

            if (receive_message(client_socket, &incoming) != 0) {
                return -1;
            }

            if (strcmp(incoming.type, MSG_CHALLENGED) == 0) {
                printf("\nYou have been challenged by %s!\n", incoming.content);

                if (respond_to_challenge(client_socket, incoming.content) != 0) {
                    printf("Could not respond to challenge.\n");
                }

                printf("\nChoose an option: ");

                for (int j = 0; j < i; j++) {
                    printf("%c", choice[j]);
                }
            }
            else if (strcmp(incoming.type, MSG_GAME_START) == 0) {
                *game_start = incoming;
                return 2;
            }
        }

        if (_kbhit()) {

            int ch = _getch();

            if (ch == '\r') {
                choice[i] = '\0';
                printf("\n");
                return 1;
            }

            if (ch == '\b') {

                if (i > 0) {
                    i--;
                    printf("\b \b");
                }

            } else if (ch >= 32 && ch <= 126 && i < size - 1) {

                choice[i++] = (char)ch;
                printf("%c", ch);
            }
        }
    }
}


int play_game(SOCKET client_socket, int game_id, int side, char *opponent){

    //Create a game, messages and play start
    Game game;
    Message message = {0};
    char input[16];
    int input_size = 0;
    int awaiting_update = 0;
    int resigning = 0;

    init_game(&game);
    game.id = game_id;


    //Presentation and initial board
    printf("\n===== AWALE GAME =====\n");
    printf("Playing against: %s\n", opponent);
    printf("Your side: %d\n", side);

    print_board(&game);

    if (game.current_player == side) {
        printf("\nEnter a hole (0-11), or R to resign.\n");
        printf("Your move: ");
    } else {
        printf("\nWaiting for opponent's move...\n");
    }


    //Playball!
    while (1) {

        int socket_status = socket_has_mail(client_socket);//Check mail

        if (socket_status == -1) {
            printf("\nConnection lost.\n");
            return -1;
        }

        if (socket_status == 1) { //Get the message

            Message incoming = {0};

            if (receive_message(client_socket, &incoming) != 0) {
                printf("\nFailed to receive game message.\n");
                return -1;
            }

            if (strcmp(incoming.type, MSG_BOARD_UPDATE) == 0) { //Update board

                int received;
                int hole;

                if (sscanf(incoming.content, "%d %d", &received, &hole) != 2) {
                    printf("\nInvalid board update.\n");
                    return -1;
                }

                if (received != game_id) {
                    continue;
                }

                if (sow(&game, hole) != 0) {
                    printf("\nBoard synchronization error.\n");
                    return -1;
                }

                awaiting_update = 0;
                input_size = 0;
                input[0] = '\0';

                printf("\n");
                print_board(&game);

                if (game.current_player == side) {
                    printf("\nYour move: ");
                } else {
                    printf("\nWaiting for opponent's move...\n");
                }

            }

            else if (strcmp(incoming.type, MSG_MOVE) == 0) {

                if (incoming.response == RESP_SUCCESS) {
                    printf("\nMove accepted. Waiting for board update...\n");
                } else {
                    printf("\nInvalid move: %s\n", incoming.content);
                    awaiting_update = 0;
                    input_size = 0;
                    input[0] = '\0';

                    if (game.current_player == side && !resigning) {
                        printf("\nYour move: ");
                    }
                }
            }

            else if (strcmp(incoming.type, MSG_RESIGN) == 0) {

                if (incoming.response != RESP_SUCCESS) {
                    printf("\nResignation failed: %s\n", incoming.content);
                    resigning = 0;
                    awaiting_update = 0;

                    if (game.current_player == side) {
                        printf("\nYour move: ");
                    }
                } else {
                    printf("\nResignation sent. Waiting for result...\n");
                }
            }

            else if (strcmp(incoming.type, MSG_GAME_OVER) == 0) {

                //game summary

                int received_game;
                int captures0;
                int captures1;
                char winner[MAX_NAME + 1];
                char reason[32];

                //Checks

                if (sscanf(incoming.content, "%d %d %d %32s %31s",
                           &received_game, &captures0, &captures1,
                           winner, reason) != 5) {
                    printf("\nInvalid game result.\n");
                    return -1;
                }

                if (received_game != game_id) {
                    continue;
                }

                //Announce final score and checks

                printf("\n===== GAME OVER =====\n");
                printf("Final score:\n");
                printf("Player 0: %d\n", captures0);
                printf("Player 1: %d\n", captures1);

                if (strcmp(winner, "draw") == 0) {
                    printf("The game ended in a draw!\n");
                } else {
                    printf("Winner: %s\n", winner);
                }

                printf("Reason: %s\n", reason);

                return 1;
            }

            else if (strcmp(incoming.type, MSG_OPPONENT_LEFT) == 0) {

                printf("\nOpponent left the game.\n");
                return 1;
            }
        }

        if (_kbhit() && !awaiting_update && !resigning &&
            game.current_player == side) { //Check if the keyboard has been pressed

            int ch = _getch();

            if (ch == '\r') {

                input[input_size] = '\0';

                if (strcmp(input, "r") == 0 || strcmp(input, "R") == 0) {

                    //Send resignation

                    memset(&message, 0, sizeof(message));

                    strcpy(message.type, MSG_RESIGN);
                    message.response = RESP_NONE;
                    message.mode = MODE_PLAYER;
                    message.receiver = 0;

                    //Checks

                    if (send_message(client_socket, &message) != 0) {
                        printf("\nFailed to send resignation.\n");
                        return -1;
                    }

                    resigning = 1;
                    awaiting_update = 1;

                    printf("\nWaiting for resignation response...\n");
                }

                else {

                    int hole;
                    char extra;

                    if (sscanf(input, "%d %c", &hole, &extra) != 1 ||
                        hole < 0 || hole > 11) {

                        printf("\nInvalid hole. Choose between 0 and 11.\n");
                        printf("\nYour move: ");

                    }

                    else {

                        //Send move

                        memset(&message, 0, sizeof(message));

                        strcpy(message.type, MSG_MOVE);
                        message.response = RESP_NONE;
                        message.mode = MODE_PLAYER;
                        message.receiver = 0;
                        snprintf(message.content, sizeof(message.content), "%d", hole);

                        if (send_message(client_socket, &message) != 0) {
                            printf("\nFailed to send move.\n");
                            return -1;
                        }

                        awaiting_update = 1;
                    }
                }

                input_size = 0;
                input[0] = '\0';
            }

            else if (ch == '\b') {

                if (input_size > 0) {
                    input_size--;
                    printf("\b \b");
                }
            }

            else if (ch >= 32 && ch <= 126 && input_size < 15) {

                input[input_size++] = (char)ch;
                printf("%c", ch);
            }
        }
    }
}


int lobby_menu(SOCKET client_socket, int *game_id, int *side, char *opponent, int opponent_size){

    char choice[16];
    Message message = {0};
    Message response;
    Message game_start = {0};
    int lobby_status;

    while (1) {

        printf("\n===== AWALE LOBBY =====\n");
        printf("1. Show online players\n");
        printf("2. Challenge a player\n");
        printf("3. Quit\n");
        printf("Choose an option: ");

        lobby_status = read_lobby_choice(choice, sizeof(choice), client_socket, &game_start);

        if (lobby_status == -1) {
            printf("Connection lost.\n");
            return 0;
        }

        if (lobby_status == 2) {

            char *opponent_start;

            if (sscanf(game_start.content, "%d %d", game_id, side) != 2) {
                printf("Invalid game start message.\n");
                return 0;
            }

            opponent_start = strchr(game_start.content, ' ');
            opponent_start = opponent_start ? strchr(opponent_start + 1, ' ') : NULL;

            if (opponent_start == NULL) {
                printf("Invalid opponent information.\n");
                return 0;
            }

            opponent_start++;
            snprintf(opponent, opponent_size, "%s", opponent_start);

            printf("\n===== GAME STARTED =====\n");
            printf("Game ID: %d\n", *game_id);
            printf("Your side: %d\n", *side);
            printf("Opponent: %s\n", opponent);

            return 2;
        }

        if (strcmp(choice, "1") == 0) {

            memset(&message, 0, sizeof(message));

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

        } else if (strcmp(choice, "2") == 0) {

            challenge_player(&client_socket);

        } else if (strcmp(choice, "3") == 0) {

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
    int game_id;
    int side;
    char opponent[MAX_NAME + 1];

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

            while (1) {

                int lobby_status = lobby_menu(client_socket, &game_id, &side,
                                               opponent, sizeof(opponent));

                if (lobby_status != 2) {
                    break;
                }

                if (play_game(client_socket, game_id, side, opponent) == -1) {
                    break;
                }
            }
        }
    }

    closesocket(client_socket);
    WSACleanup();

    return 0;
}