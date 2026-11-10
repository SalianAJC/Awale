
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <string.h>
#include <conio.h> //Reads unentered data

#define STRINGIFY_INNER(x) #x
#define STRINGIFY(x) STRINGIFY_INNER(x)

#include "network.h"
#include "game.h"

/* ============================================================
   PRIVATE MODE
   ============================================================ */

int toggle_private(SOCKET client_socket)
{
    Message request = {0};
    Message response;
    char choice[16];

    printf("Enable private mode? (on/off): ");

    if (fgets(choice, sizeof(choice), stdin) == NULL)
        return -1;

    choice[strcspn(choice, "\r\n")] = '\0';

    if (strcmp(choice, "on") != 0 &&
        strcmp(choice, "off") != 0)
    {
        printf("Please enter 'on' or 'off'.\n");
        return 0;
    }

    strcpy(request.type, MSG_PRIVATE);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    request.receiver = 0;
    snprintf(request.content, sizeof(request.content), "%s", choice);

    if (send_message(client_socket, &request) != 0)
    {
        printf("Failed to send private mode request.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive private mode response.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_PRIVATE) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response == RESP_SUCCESS)
        printf("Private mode is now %s.\n", choice);
    else
        printf("Could not change private mode: %s\n", response.content);

    return 0;
}

/* ============================================================
   LIST ACTIVE GAMES
   ============================================================ */

int list_games(SOCKET client_socket)
{
    Message request = {0};
    Message response;

    strcpy(request.type, MSG_LIST_GAMES);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    request.receiver = 0;
    request.content[0] = '\0';

    if (send_message(client_socket, &request) != 0)
    {
        printf("Failed to request active games.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive active games.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_LIST_GAMES) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response != RESP_SUCCESS)
    {
        printf("Could not retrieve games: %s\n", response.content);
        return 0;
    }

    printf("\n========== ACTIVE GAMES ==========\n");

    if (response.content[0] == '\0')
    {
        printf("No games are currently being played.\n");
        return 0;
    }

    char content[MAX_CONTENT + 1];
    snprintf(content, sizeof(content), "%s", response.content);

    char *entry = strtok(content, ",");

    while (entry != NULL)
    {
        int id;
        char player0[MAX_NAME + 1];
        char player1[MAX_NAME + 1];
        char visibility[8];

        if (sscanf(entry, "%d:%32[^:]:%32[^:]:%7s",
                   &id, player0, player1, visibility) == 4)
        {
            printf("Game %d: %s vs %s [%s]\n",
                   id, player0, player1, visibility);
        }
        else
        {
            printf("Invalid game entry: %s\n", entry);
        }

        entry = strtok(NULL, ",");
    }

    printf("==================================\n");

    return 0;
}

/* ============================================================
   REQUEST TO OBSERVE A GAME
   ============================================================ */

int watch_request(SOCKET client_socket, Message *initial_state)
{
    Message request = {0};
    char input[32];
    char *end;
    long game_id;

    if (initial_state == NULL)
        return -1;

    printf("Enter the game ID you want to watch: ");

    if (fgets(input, sizeof(input), stdin) == NULL)
        return -1;

    input[strcspn(input, "\r\n")] = '\0';

    game_id = strtol(input, &end, 10);

    if (input[0] == '\0' || *end != '\0' ||
        game_id < 1 || game_id > 2147483647L)
    {
        printf("Invalid game ID.\n");
        return 0;
    }

    strcpy(request.type, MSG_OBSERVE);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    request.receiver = 0;

    snprintf(request.content, sizeof(request.content),
             "%ld", game_id);

    if (send_message(client_socket, &request) != 0)
    {
        printf("Failed to send observation request.\n");
        return -1;
    }

    if (receive_message(client_socket, initial_state) != 0)
    {
        printf("Failed to receive observation response.\n");
        return -1;
    }

    if (strcmp(initial_state->type, MSG_OBSERVE) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (initial_state->response != RESP_SUCCESS)
    {
        printf("Cannot watch this game: %s\n",
               initial_state->content);
        return 0;
    }

    if (initial_state->mode != MODE_OBSERVER)
    {
        printf("Invalid observer response mode.\n");
        return -1;
    }

    return 1;
}

/* ============================================================
   OBSERVER: PARSE ONE SPACE-SEPARATED TOKEN
   ============================================================ */

static int observer_read_token(char **cursor,
                               char *destination,
                               size_t destination_size)
{
    size_t length = 0;

    while (**cursor == ' ')
        (*cursor)++;

    if (**cursor == '\0')
        return 0;

    while (**cursor != '\0' && **cursor != ' ')
    {
        if (length + 1 >= destination_size)
            return 0;

        destination[length++] = **cursor;
        (*cursor)++;
    }

    destination[length] = '\0';
    return 1;
}

/* ============================================================
   OBSERVER: REPLAY THE MOVE HISTORY
   ============================================================ */

static int replay_observer_moves(Game *game, char *moves)
{
    if (moves == NULL || moves[0] == '\0')
        return 0;

    char *token = strtok(moves, ",");

    while (token != NULL)
    {
        char *end;
        long hole = strtol(token, &end, 10);

        if (token[0] == '\0' || *end != '\0' ||
            hole < 0 || hole > 11)
        {
            printf("Invalid move in game history: %s\n", token);
            return -1;
        }

        if (sow(game, (int)hole) != 0)
        {
            printf("Could not replay move %ld.\n", hole);
            return -1;
        }

        token = strtok(NULL, ",");
    }

    return 0;
}

/* ============================================================
   OBSERVE A GAME
   ============================================================ */

int observe_game(SOCKET client_socket, const Message *initial_state)
{
    Game game;
    char content[MAX_CONTENT + 1];
    char player0[MAX_NAME + 1];
    char player1[MAX_NAME + 1];
    char move_history[MAX_CONTENT + 1];
    char *cursor;
    char game_id_text[16];
    char *end;
    long game_id;
    int stop_requested = 0;

    if (initial_state == NULL)
        return -1;

    /*
     * Expected server content:
     *
     * game_id player0 player1 move1,move2,move3,...
     *
     * The move list can be empty.
     */

    snprintf(content, sizeof(content), "%s", initial_state->content);
    cursor = content;

    if (!observer_read_token(&cursor,
                             game_id_text,
                             sizeof(game_id_text)) ||
        !observer_read_token(&cursor,
                             player0,
                             sizeof(player0)) ||
        !observer_read_token(&cursor,
                             player1,
                             sizeof(player1)))
    {
        printf("Invalid initial observer state.\n");
        return -1;
    }

    game_id = strtol(game_id_text, &end, 10);

    if (*end != '\0' || game_id < 1 || game_id > 2147483647L)
    {
        printf("Invalid game ID in observer state.\n");
        return -1;
    }

    while (*cursor == ' ')
        cursor++;

    snprintf(move_history, sizeof(move_history), "%s", cursor);

    init_game(&game);
    game.id = (int)game_id;

    if (replay_observer_moves(&game, move_history) != 0)
    {
        printf("Could not reconstruct the current board.\n");
        return -1;
    }

    printf("\n========== OBSERVING GAME %d ==========\n", game.id);
    printf("%s vs %s\n", player0, player1);
    printf("You are an observer. You cannot play moves.\n");
    printf("Press Q to stop observing.\n\n");

    print_board(&game);

    while (1)
    {
        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int socket_status = select(
            0,
            &readfds,
            NULL,
            NULL,
            &timeout);

        if (socket_status == -1)
        {
            printf("\nConnection lost while observing.\n");
            return -1;
        }

        if (socket_status == 1)
        {
            Message incoming = {0};

            if (receive_message(client_socket, &incoming) != 0)
            {
                printf("\nFailed to receive observer update.\n");
                return -1;
            }

            /* A new move was played. */
            if (strcmp(incoming.type, MSG_BOARD_UPDATE) == 0 &&
                incoming.mode == MODE_OBSERVER)
            {
                int received_game;
                int hole;

                if (sscanf(incoming.content, "%d %d",
                           &received_game, &hole) != 2)
                {
                    printf("Invalid observer board update.\n");
                    return -1;
                }

                if (received_game != game.id)
                    continue;

                if (sow(&game, hole) != 0)
                {
                    printf("Observer board synchronization error.\n");
                    return -1;
                }

                printf("\n--- New move: hole %d ---\n", hole);
                print_board(&game);
            }

            else if (strcmp(incoming.type, MSG_CHAT) == 0)
            {
                printf("[CHAT] %s\n", incoming.content);
            }

            /* The observed game has ended. */
            else if (strcmp(incoming.type, MSG_GAME_OVER) == 0 &&
                     incoming.mode == MODE_OBSERVER)
            {
                int received_game;
                int captures0;
                int captures1;
                char winner[MAX_NAME + 1];
                char reason[32];

                if (sscanf(incoming.content,
                           "%d %d %d %32s %31s",
                           &received_game,
                           &captures0,
                           &captures1,
                           winner,
                           reason) != 5)
                {
                    printf("Invalid game result.\n");
                    return -1;
                }

                if (received_game != game.id)
                    continue;

                printf("\n========== GAME OVER ==========\n");
                printf("Player 0: %d seeds\n", captures0);
                printf("Player 1: %d seeds\n", captures1);

                if (strcmp(winner, "draw") == 0)
                    printf("The game ended in a draw.\n");
                else
                    printf("Winner: %s\n", winner);

                printf("Reason: %s\n", reason);
                printf("Returning to lobby.\n");

                return 1;
            }

            /* The server acknowledged our request to stop. */
            else if (strcmp(incoming.type, MSG_STOP_OBSERVE) == 0)
            {
                if (incoming.response == RESP_SUCCESS)
                {
                    printf("\nStopped observing.\n");
                    return 1;
                }

                printf("Could not stop observing: %s\n",
                       incoming.content);
                stop_requested = 0;
            }
        }

        /* Q requests that the server remove this observer. */
        if (!stop_requested && _kbhit())
        {
            int ch = _getch();

            if (ch == 'q' || ch == 'Q')
            {
                Message request = {0};

                strcpy(request.type, MSG_STOP_OBSERVE);
                request.response = RESP_NONE;
                request.mode = MODE_OBSERVER;
                request.receiver = 0;
                request.content[0] = '\0';

                if (send_message(client_socket, &request) != 0)
                {
                    printf("Failed to send stop-observing request.\n");
                    return -1;
                }

                stop_requested = 1;
                printf("\nStopping observation...\n");
            }
        }
    }
}

int register_acc(SOCKET client_socket)
{
    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;

    printf("Enter username: ");

    if (fgets(username, sizeof(username), stdin) == NULL)
    {
        printf("Error reading username.\n");
        return -1;
    }

    size_t length = strlen(username);

    if (length > 0 && username[length - 1] == '\n')
    {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }

        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0')
    {
        printf("Username cannot be empty.\n");
        return -1;
    }

    strcpy(message.type, MSG_REGISTER);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(client_socket, &message) != 0)
    {
        printf("Failed to send registration request.\n");
        return -2;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive registration response.\n");
        return -2;
    }

    if (strcmp(response.type, MSG_REGISTER) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response == RESP_SUCCESS)
    {
        printf("Registration successful!\n");
        return 0;
    }
    else
    {
        printf("Registration failed: %s\n", response.content);
        return -1;
    }
}

int log_in(SOCKET client_socket)
{
    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;
    Message onl_list;

    printf("Enter username: ");

    if (fgets(username, sizeof(username), stdin) == NULL)
    {
        printf("Error reading username.\n");
        return -1;
    }

    size_t length = strlen(username);

    if (length > 0 && username[length - 1] == '\n')
    {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }

        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0')
    {
        printf("Username cannot be empty.\n");
        return -1;
    }

    strcpy(message.type, MSG_LOGIN);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(client_socket, &message) != 0)
    {
        printf("Failed to send login request.\n");
        return -2;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive login response.\n");
        return -2;
    }

    if (strcmp(response.type, MSG_LOGIN) != 0)
    {
        printf("Unexpected server response.\n");
        return -2;
    }

    if (response.response != RESP_SUCCESS)
    {
        printf("Login failed: %s\n", response.content);
        return -1;
    }

    printf("Login successful!\n");
    printf("Welcome, %s!\n", username);

    if (receive_message(client_socket, &onl_list) != 0)
    {
        printf("Failed to receive online players list.\n");
        return -2;
    }

    if (strcmp(onl_list.type, MSG_PLAYERS_ONLINE) == 0)
    {
        printf("\n===== ONLINE PLAYERS =====\n");
        printf("%s\n", onl_list.content);
    }
    else
    {
        printf("Unexpected online players response.\n");
        return -2;
    }

    return 0;
}

int challenge_player(SOCKET *client_socket)
{

    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;

    strcpy(message.type, MSG_LIST_ONLINE);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    message.content[0] = '\0';

    if (send_message(*client_socket, &message) != 0)
    {
        printf("Failed to request online players.\n");
        return -1;
    }

    if (receive_message(*client_socket, &response) != 0)
    {
        printf("Failed to receive online players.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_LIST_ONLINE) != 0 ||
        response.response != RESP_SUCCESS)
    {
        printf("Failed to retrieve online players.\n");
        return -1;
    }

    printf("\n===== ONLINE PLAYERS =====\n");
    printf("%s\n", response.content);

    printf("\nEnter the username of the player you want to challenge: ");

    if (fgets(username, sizeof(username), stdin) == NULL)
    {
        printf("Error reading username.\n");
        return -1;
    }

    size_t length = strlen(username);

    if (length > 0 && username[length - 1] == '\n')
    {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }

        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0')
    {
        printf("Username cannot be empty.\n");
        return -1;
    }

    memset(&message, 0, sizeof(message));

    strcpy(message.type, MSG_CHALLENGE);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(*client_socket, &message) != 0)
    {
        printf("Failed to send challenge request.\n");
        return -1;
    }

    if (receive_message(*client_socket, &response) != 0)
    {
        printf("Failed to receive challenge response.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_CHALLENGE) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response == RESP_SUCCESS)
    {
        printf("Challenge sent to %s!\n", response.content);
        return 0;
    }

    printf("Challenge failed: %s\n", response.content);
    return -1;
}

int respond_to_challenge(SOCKET client_socket, char *challenger)
{

    char choice[16];
    Message message = {0};
    Message response;

    printf("\nAccept challenge from %s? (y/n): ", challenger);

    // Initial message checks, yes, no, or error

    if (fgets(choice, sizeof(choice), stdin) == NULL)
    {
        return -1;
    }

    if (choice[0] == 'y' || choice[0] == 'Y')
    {
        strcpy(message.type, MSG_ACCEPT);
    }

    else if (choice[0] == 'n' || choice[0] == 'N')
    {
        strcpy(message.type, MSG_REFUSE);
    }

    else
    {
        printf("Invalid choice.\n");
        return -1;
    }

    // Socket module

    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", challenger);

    // Response checks

    if (send_message(client_socket, &message) != 0)
    {
        printf("Failed to send challenge response.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive server response.\n");
        return -1;
    }

    if (response.response != RESP_SUCCESS)
    {
        printf("Challenge response failed: %s\n", response.content);
        return -1;
    }

    if (strcmp(message.type, MSG_ACCEPT) == 0)
    {
        printf("Challenge accepted!\n");
    }
    else
    {
        printf("Challenge refused.\n");
    }

    return 0;
}
int show_ranking(SOCKET client_socket)
{
    Message request = {0};
    Message response;

    strcpy(request.type, MSG_RANKING);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    request.receiver = 0;

    if (send_message(client_socket, &request) != 0)
    {
        printf("Failed to request the ranking.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive the ranking.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_RANKING) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response != RESP_SUCCESS)
    {
        printf("Could not retrieve ranking: %s\n",
               response.content);
        return -1;
    }

    printf("\n===== PLAYER RANKING =====\n");

    if (response.content[0] == '\0')
    {
        printf("No registered players.\n");
    }
    else
    {
        printf("%s\n", response.content);
    }

    return 0;
}

int socket_has_mail(SOCKET client_socket)
{

    fd_set readfds;

    struct timeval timeout; // We create a timeout for answers

    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // Arbitrary max 100 milisecs

    int result = select(0, &readfds, NULL, NULL, &timeout);

    if (result == SOCKET_ERROR)
    {
        return -1;
    }

    return result > 0;
}

int read_lobby_choice(char *choice, int size, SOCKET client_socket, Message *game_start)
{

    int i = 0;

    while (1)
    {

        int socket_status = socket_has_mail(client_socket);

        if (socket_status == -1)
        {
            return -1;
        }

        if (socket_status == 1)
        {

            Message incoming = {0};

            if (receive_message(client_socket, &incoming) != 0)
            {
                return -1;
            }

            if (strcmp(incoming.type, MSG_CHALLENGED) == 0)
            {
                printf("\nYou have been challenged by %s!\n", incoming.content);

                if (respond_to_challenge(client_socket, incoming.content) != 0)
                {
                    printf("Could not respond to challenge.\n");
                }

                printf("\nChoose an option: ");

                for (int j = 0; j < i; j++)
                {
                    printf("%c", choice[j]);
                }
            }
            else if (strcmp(incoming.type, MSG_CHAT) == 0)
            {
                if (incoming.response == RESP_NONE)
                {
                    printf("\n[CHAT] %s\n", incoming.content);
                }
                else if (incoming.response == RESP_SUCCESS)
                {
                    printf("\n[CHAT] Message sent.\n");
                }
                else
                {
                    printf("\n[CHAT] Failed: %s\n", incoming.content);
                }

                printf("\nChoose an option: ");
                for (int j = 0; j < i; j++)
                {
                    printf("%c", choice[j]);
                }
            }
            else if (strcmp(incoming.type, MSG_REFUSE) == 0 &&
                     incoming.response == RESP_NONE)
            {
                printf("\nYour challenge to %s was refused or cancelled.\n",
                       incoming.content);

                printf("\nChoose an option: ");
                for (int j = 0; j < i; j++)
                {
                    printf("%c", choice[j]);
                }
            }
            else if (strcmp(incoming.type, MSG_GAME_START) == 0)
            {
                *game_start = incoming;
                return 2;
            }
        }

        if (_kbhit())
        {

            int ch = _getch();

            if (ch == '\r')
            {
                choice[i] = '\0';
                printf("\n");
                return 1;
            }

            if (ch == '\b')
            {

                if (i > 0)
                {
                    i--;
                    printf("\b \b");
                }
            }
            else if (ch >= 32 && ch <= 126 && i < size - 1)
            {

                choice[i++] = (char)ch;
                printf("%c", ch);
            }
        }
    }
}

int play_game(SOCKET client_socket, int game_id, int side, char *opponent)
{

    // Create a game, messages and play start
    Game game;
    Message message = {0};
    char input[MAX_CONTENT + 1];
    int input_size = 0;
    int awaiting_update = 0;
    int resigning = 0;
    int chat_mode = 0;

    init_game(&game);
    game.id = game_id;

    // Presentation and initial board
    printf("\n===== AWALE GAME =====\n");
    printf("Playing against: %s\n", opponent);
    printf("Your side: %d\n", side);

    print_board(&game);

    if (game.current_player == side)
    {
        printf("\nEnter a hole (0-11), R to resign, or CH to chat.\n");
        printf("Your move: ");
    }
    else
    {
        printf("\nWaiting for opponent's move...\n");
        printf("Type CH to open the chat.\n");
    }

    // Playball!
    while (1)
    {

        int socket_status = socket_has_mail(client_socket); // Check mail

        if (socket_status == -1)
        {
            printf("\nConnection lost.\n");
            return -1;
        }

        if (socket_status == 1)
        { // Get the message

            Message incoming = {0};

            if (receive_message(client_socket, &incoming) != 0)
            {
                printf("\nFailed to receive game message.\n");
                return -1;
            }

            // Receive chat messages
            if (strcmp(incoming.type, MSG_CHAT) == 0)
            {

                if (incoming.response == RESP_NONE)
                {
                    printf("\n[CHAT] %s\n", incoming.content);
                }
                else if (incoming.response == RESP_SUCCESS)
                {
                    printf("\n[CHAT] Message sent.\n");
                }
                else
                {
                    printf("\n[CHAT] Failed: %s\n", incoming.content);
                }

                // Restore current prompt
                if (chat_mode)
                {
                    printf("Chat: ");
                    if (input_size > 0)
                    {
                        fwrite(input, 1, input_size, stdout);
                    }
                }
                else if (game.current_player == side)
                {
                    printf("Your move: ");
                }
                else
                {
                    printf("Waiting for opponent's move...\n");
                    printf("Type CH to open the chat.\n");
                }
            }

            else if (strcmp(incoming.type, MSG_BOARD_UPDATE) == 0)
            { // Update board

                int received;
                int hole;

                if (sscanf(incoming.content, "%d %d", &received, &hole) != 2)
                {
                    printf("\nInvalid board update.\n");
                    return -1;
                }

                if (received != game_id)
                {
                    continue;
                }

                if (sow(&game, hole) != 0)
                {
                    printf("\nBoard synchronization error.\n");
                    return -1;
                }

                awaiting_update = 0;

                if (!chat_mode)
                {
                    input_size = 0;
                    input[0] = '\0';
                }

                printf("\n");
                print_board(&game);

                // Keep chat open during board updates
                if (chat_mode)
                {
                    printf("\nChat: ");
                    if (input_size > 0)
                    {
                        fwrite(input, 1, input_size, stdout);
                    }
                }
                else if (game.current_player == side)
                {
                    printf("\nYour move: ");
                }
                else
                {
                    printf("\nWaiting for opponent's move...\n");
                    printf("Type CH to open the chat.\n");
                }
            }

            else if (strcmp(incoming.type, MSG_MOVE) == 0)
            {

                if (incoming.response == RESP_SUCCESS)
                {
                    printf("\nMove accepted. Waiting for board update...\n");
                }
                else
                {
                    printf("\nInvalid move: %s\n", incoming.content);
                    awaiting_update = 0;
                    input_size = 0;
                    input[0] = '\0';

                    if (game.current_player == side && !resigning)
                    {
                        printf("\nYour move: ");
                    }
                }
            }

            else if (strcmp(incoming.type, MSG_RESIGN) == 0)
            {

                if (incoming.response != RESP_SUCCESS)
                {
                    printf("\nResignation failed: %s\n", incoming.content);
                    resigning = 0;
                    awaiting_update = 0;

                    if (game.current_player == side)
                    {
                        printf("\nYour move: ");
                    }
                }
                else
                {
                    printf("\nResignation sent. Waiting for result...\n");
                }
            }

            else if (strcmp(incoming.type, MSG_GAME_OVER) == 0)
            {

                // game summary

                int received_game;
                int captures0;
                int captures1;
                char winner[MAX_NAME + 1];
                char reason[32];

                // Checks

                if (sscanf(incoming.content, "%d %d %d %32s %31s",
                           &received_game, &captures0, &captures1,
                           winner, reason) != 5)
                {
                    printf("\nInvalid game result.\n");
                    return -1;
                }

                if (received_game != game_id)
                {
                    continue;
                }

                // Announce final score and checks

                printf("\n===== GAME OVER =====\n");
                printf("Final score:\n");
                printf("Player 0: %d\n", captures0);
                printf("Player 1: %d\n", captures1);

                if (strcmp(winner, "draw") == 0)
                {
                    printf("The game ended in a draw!\n");
                }
                else
                {
                    printf("Winner: %s\n", winner);
                }

                printf("Reason: %s\n", reason);

                return 1;
            }

            else if (strcmp(incoming.type, MSG_OPPONENT_LEFT) == 0)
            {

                printf("\nOpponent left the game.\n");
                return 1;
            }
        }

        // Read keyboard input without blocking socket messages
        if (_kbhit() && !awaiting_update && !resigning)
        {

            int ch = _getch();

            if (ch == '\r')
            {

                input[input_size] = '\0';

                // Chat mode
                if (chat_mode)
                {

                    // Exit chat
                    if (strcmp(input, "EXIT") == 0 ||
                        strcmp(input, "exit") == 0)
                    {

                        chat_mode = 0;
                        printf("\nLeaving chat.\n");

                        if (game.current_player == side)
                        {
                            printf("Your move: ");
                        }
                        else
                        {
                            printf("Waiting for opponent's move...\n");
                            printf("Type CH to open the chat.\n");
                        }
                    }

                    // Send chat message
                    else if (input_size > 0)
                    {

                        char chat_content[MAX_NAME + MAX_CHAT + 2];

                        memset(&message, 0, sizeof(message));

                        strcpy(message.type, MSG_CHAT);
                        message.response = RESP_NONE;
                        message.mode = MODE_PLAYER;
                        message.receiver = 0;

                        snprintf(chat_content, sizeof(chat_content),
                                 "%s %s", opponent, input);

                        snprintf(message.content, sizeof(message.content),
                                 "%s", chat_content);

                        if (send_message(client_socket, &message) != 0)
                        {
                            printf("\nFailed to send chat message.\n");
                            return -1;
                        }
                    }

                    if (chat_mode)
                    {
                        printf("Chat: ");
                    }
                }

                // Open chat
                else if (strcmp(input, "CH") == 0 ||
                         strcmp(input, "ch") == 0)
                {

                    chat_mode = 1;
                    printf("\n===== GAME CHAT =====\n");
                    printf("Type EXIT to return to the game.\n");
                    printf("Chat: ");
                }

                else if (game.current_player != side)
                {

                    printf("\nIt's not your turn.\n");
                    printf("Waiting for opponent's move...\n");
                    printf("Type CH to open the chat.\n");
                }

                else if (strcmp(input, "r") == 0 ||
                         strcmp(input, "R") == 0)
                {

                    // Send resignation

                    memset(&message, 0, sizeof(message));

                    strcpy(message.type, MSG_RESIGN);
                    message.response = RESP_NONE;
                    message.mode = MODE_PLAYER;
                    message.receiver = 0;

                    // Checks

                    if (send_message(client_socket, &message) != 0)
                    {
                        printf("\nFailed to send resignation.\n");
                        return -1;
                    }

                    resigning = 1;
                    awaiting_update = 1;

                    printf("\nWaiting for resignation response...\n");
                }

                else
                {

                    int hole;
                    char extra;

                    if (sscanf(input, "%d %c", &hole, &extra) != 1 ||
                        hole < 0 || hole > 11)
                    {

                        printf("\nInvalid hole. Choose between 0 and 11.\n");
                        printf("\nYour move: ");
                    }

                    else
                    {

                        // Send move

                        memset(&message, 0, sizeof(message));

                        strcpy(message.type, MSG_MOVE);
                        message.response = RESP_NONE;
                        message.mode = MODE_PLAYER;
                        message.receiver = 0;
                        snprintf(message.content, sizeof(message.content), "%d", hole);

                        if (send_message(client_socket, &message) != 0)
                        {
                            printf("\nFailed to send move.\n");
                            return -1;
                        }

                        awaiting_update = 1;
                    }
                }

                input_size = 0;
                input[0] = '\0';
            }

            else if (ch == '\b')
            {

                if (input_size > 0)
                {
                    input_size--;
                    printf("\b \b");
                }
            }

            else if (ch >= 32 && ch <= 126 &&
                     input_size < (chat_mode ? MAX_CHAT : 15))
            {

                input[input_size++] = (char)ch;
                printf("%c", ch);
            }
        }
    }
}

int write_bio(SOCKET client_socket)
{
    char bio[MAX_CONTENT + 1] = ""; // Max content
    char line[MAX_BIO_LINE + 2];    // Max bio capacity (10 ascii)
    Message message = {0};
    Message response;
    int lines = 0;

    // Menu setup

    printf("\n===== WRITE YOUR BIO =====\n");
    printf("Write up to %d lines, with a maximum of %d characters each.\n",
           MAX_BIO_LINES, MAX_BIO_LINE);
    printf("Press Enter on an empty line to finish.\n");

    // Read bio lines
    while (lines < MAX_BIO_LINES)
    {
        printf("Line %d: ", lines + 1);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            return -1;
        }

        size_t length = strlen(line);

        if (length > 0 && line[length - 1] == '\n')
        {
            line[length - 1] = '\0';
            length--;
        }
        else if (!feof(stdin))
        {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF)
            {
            }
            printf("Line is too long.\n");
            return -1;
        }

        if (length == 0)
        {
            break;
        }

        if (lines > 0)
        {
            strcat(bio, ";");
        }

        strcat(bio, line);
        lines++;
    }

    // Send bio
    strcpy(message.type, MSG_WRITE_BIO);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", bio);

    // Checks

    if (send_message(client_socket, &message) != 0)
    {
        printf("Failed to send bio.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive bio response.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_WRITE_BIO) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    if (response.response == RESP_SUCCESS)
    {
        printf("Bio updated successfully!\n");
        return 0;
    }

    printf("Bio update failed: %s\n", response.content);
    return -1;
}

// Bio reader

int read_bio(SOCKET client_socket)
{
    char username[MAX_NAME + 2]; // Username
    Message message = {0};
    Message response;

    printf("Enter the username whose bio you want to read: ");

    if (fgets(username, sizeof(username), stdin) == NULL)
    {
        return -1;
    }

    size_t length = strlen(username);

    // Checks

    if (length > 0 && username[length - 1] == '\n')
    {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }
        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0')
    {
        printf("Username cannot be empty.\n");
        return -1;
    }

    // Send bio request
    strcpy(message.type, MSG_READ_BIO);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    // Checks
    if (send_message(client_socket, &message) != 0)
    {
        printf("Failed to send bio request.\n");
        return -1;
    }

    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive bio response.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_READ_BIO) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    printf("\n===== PLAYER BIO =====\n");

    /* Convert ';' into a display newline. */
    for (size_t i = 0; response.content[i] != '\0'; i++)
    {
        putchar(response.content[i] == ';' ? '\n' : response.content[i]);
    }
    putchar('\n');

    return 0;
}

// Adding a friend

int add_friend(SOCKET client_socket)
{
    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;

    printf("Enter the username of the player to add: ");

    if (fgets(username, sizeof(username), stdin) == NULL)
        return -1;

    size_t length = strlen(username);

    if (length > 0 && username[length - 1] == '\n')
    {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }
        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0')
    {
        printf("Username cannot be empty.\n");
        return -1;
    }

    strcpy(message.type, MSG_FRIEND_ADD);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(client_socket, &message) != 0 ||
        receive_message(client_socket, &response) != 0)
    {
        printf("Communication error.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_FRIEND_ADD) != 0)
        return -1;

    if (response.response == RESP_SUCCESS)
    {
        printf("%s is now your friend!\n", response.content);
        return 0;
    }

    printf("Could not add friend: %s\n", response.content);
    return -1;
}

int remove_friend(SOCKET client_socket)
{
    char username[MAX_NAME + 2];
    Message message = {0};
    Message response;

    printf("Enter the username of the friend to remove: ");

    if (fgets(username, sizeof(username), stdin) == NULL)
        return -1;

    size_t length = strlen(username);

    if (length > 0 && username[length - 1] == '\n')
    {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }
        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0')
    {
        printf("Username cannot be empty.\n");
        return -1;
    }

    strcpy(message.type, MSG_FRIEND_REMOVE);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", username);

    if (send_message(client_socket, &message) != 0 ||
        receive_message(client_socket, &response) != 0)
    {
        printf("Communication error.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_FRIEND_REMOVE) != 0)
        return -1;

    if (response.response == RESP_SUCCESS)
    {
        printf("%s has been removed from your friends.\n", response.content);
        return 0;
    }

    printf("Could not remove friend: %s\n", response.content);
    return -1;
}

int show_friends(SOCKET client_socket)
{
    Message message = {0};
    Message response;

    strcpy(message.type, MSG_FRIEND_LIST);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;

    if (send_message(client_socket, &message) != 0 ||
        receive_message(client_socket, &response) != 0)
    {
        printf("Communication error.\n");
        return -1;
    }

    if (strcmp(response.type, MSG_FRIEND_LIST) != 0)
        return -1;

    if (response.response == RESP_SUCCESS)
    {
        printf("\n===== YOUR FRIENDS =====\n");

        if (response.content[0] == '\0')
            printf("You have no friends yet.\n");
        else
            printf("%s\n", response.content);

        return 0;
    }

    printf("Could not retrieve friends: %s\n", response.content);
    return -1;
}

static int lobby_chat(SOCKET client_socket)
{
    char username[MAX_NAME + 2];
    char text[MAX_CHAT + 2];
    char content[MAX_NAME + MAX_CHAT + 2];
    Message message = {0};
    size_t length;

    printf("Send chat to (username): ");
    if (fgets(username, sizeof(username), stdin) == NULL)
        return -1;

    length = strlen(username);
    if (length > 0 && username[length - 1] == '\n')
    {
        username[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }
        printf("Username is too long.\n");
        return -1;
    }

    if (username[0] == '\0' || strlen(username) > MAX_NAME)
    {
        printf("Invalid username.\n");
        return -1;
    }

    printf("Message: ");
    if (fgets(text, sizeof(text), stdin) == NULL)
        return -1;

    length = strlen(text);
    if (length > 0 && text[length - 1] == '\n')
    {
        text[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }
        printf("Message is too long.\n");
        return -1;
    }

    if (text[0] == '\0')
    {
        printf("Message cannot be empty.\n");
        return -1;
    }

    if (strlen(text) > MAX_CHAT)
    {
        printf("Message is too long.\n");
        return -1;
    }

    snprintf(content, sizeof(content), "%s %s", username, text);

    strcpy(message.type, MSG_CHAT);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    snprintf(message.content, sizeof(message.content), "%s", content);

    if (send_message(client_socket, &message) != 0)
    {
        printf("Failed to send chat message.\n");
        return -1;
    }

    printf("Chat request sent. The server will confirm it in the lobby.\n");
    return 0;
}

int lobby_menu(SOCKET client_socket, int *game_id, int *side, char *opponent, int opponent_size)
{

    char choice[16];
    Message message = {0};
    Message response;
    Message game_start = {0};
    int lobby_status;

    while (1)
    {

        printf("\n===== LOBBY =====\n");
        printf("1. Show online players\n");
        printf("2. Challenge a player\n");
        printf("3. Write your bio\n");
        printf("4. Read a player's bio\n");
        printf("5. Add a friend\n");
        printf("6. Remove a friend\n");
        printf("7. Show friends\n");
        printf("8. Chat with a player\n");
        printf("9. Show player ranking\n");
        printf("10. Toggle private mode\n");
        printf("11. List active games\n");
        printf("12. Watch a game\n");
        printf("13. Quit\n");
        printf("Choose an option: ");

        lobby_status = read_lobby_choice(choice, sizeof(choice), client_socket, &game_start);

        if (lobby_status == -1)
        {
            printf("Connection lost.\n");
            return 0;
        }

        if (lobby_status == 2)
        {

            char *opponent_start;

            if (sscanf(game_start.content, "%d %d", game_id, side) != 2)
            {
                printf("Invalid game start message.\n");
                return 0;
            }

            opponent_start = strchr(game_start.content, ' ');
            opponent_start = opponent_start ? strchr(opponent_start + 1, ' ') : NULL;

            if (opponent_start == NULL)
            {
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

        if (strcmp(choice, "1") == 0)
        {

            memset(&message, 0, sizeof(message));

            strcpy(message.type, MSG_LIST_ONLINE);
            message.response = RESP_NONE;
            message.mode = MODE_NONE;
            message.receiver = 0;
            message.content[0] = '\0';

            if (send_message(client_socket, &message) != 0)
            {
                printf("Failed to request online players.\n");
                return 0;
            }

            if (receive_message(client_socket, &response) != 0)
            {
                printf("Failed to receive online players.\n");
                return 0;
            }

            if (strcmp(response.type, MSG_LIST_ONLINE) == 0 &&
                response.response == RESP_SUCCESS)
            {

                printf("\n===== ONLINE PLAYERS =====\n");
                printf("%s\n", response.content);
            }
            else
            {
                printf("Failed to retrieve online players.\n");
            }
        }
        else if (strcmp(choice, "2") == 0)
        {
            challenge_player(&client_socket);
        }
        else if (strcmp(choice, "3") == 0)
        {
            write_bio(client_socket);
        }
        else if (strcmp(choice, "4") == 0)
        {
            read_bio(client_socket);
        }
        else if (strcmp(choice, "5") == 0)
        {
            add_friend(client_socket);
        }
        else if (strcmp(choice, "6") == 0)
        {
            remove_friend(client_socket);
        }
        else if (strcmp(choice, "7") == 0)
        {
            show_friends(client_socket);
        }
        else if (strcmp(choice, "8") == 0)
        {
            lobby_chat(client_socket);
        }

        else if (strcmp(choice, "9") == 0)
        {
            show_ranking(client_socket);
        }
        else if (strcmp(choice, "10") == 0)
        {
            toggle_private(client_socket);
        }
        else if (strcmp(choice, "11") == 0)
        {
            list_games(client_socket);
        }
        else if (strcmp(choice, "12") == 0)
        {
            Message initial_state = {0};

            int status = watch_request(client_socket, &initial_state);

            if (status == 1)
            {
                int observe_status =
                    observe_game(client_socket, &initial_state);

                if (observe_status == -1)
                    return 0;
            }
            else if (status == -1)
            {
                return 0;
            }
        }
        else if (strcmp(choice, "13") == 0)
        {
            return 0;
        }
    }
}

int initial_menu(SOCKET client_socket)
{

    char choice[16];
    int registration_status;

    while (1)
    {
        printf("Welcome to the game!\n");
        printf("Sam And Omar present:\n");
        printf("\n===== AWALE =====\n");

        printf("1. Register\n");
        printf("2. Login\n");
        printf("3. Exit\n");
        printf("Choose an option: ");

        if (fgets(choice, sizeof(choice), stdin) == NULL)
        {
            printf("Error reading input.\n");
            return 0;
        }

        // Process the user's choice
        if (strcmp(choice, "1\n") == 0)
        {
            // Handle registration
            registration_status = register_acc(client_socket);

            if (registration_status == -2)
            {
                return -1;
            }

            if (registration_status == 0)
            {
                printf("\nReturning to main menu...\n");
            }
            continue;
        }
        else if (strcmp(choice, "2\n") == 0)
        {
            return 2;
        }
        else if (strcmp(choice, "3\n") == 0)
        {
            // Handle exit
            return 0;
        }
        else
        {
            printf("Invalid option. Please try again.\n");
        }
    }
}

int request_ranking(SOCKET client_socket)
{
    Message message = {0};
    Message response;

    // Build ranking request
    strcpy(message.type, MSG_RANKING);
    message.response = RESP_NONE;
    message.mode = MODE_NONE;
    message.receiver = 0;
    message.content[0] = '\0';

    // Send request
    if (send_message(client_socket, &message) != 0)
    {
        printf("Failed to send ranking request.\n");
        return -1;
    }

    // Receive response
    if (receive_message(client_socket, &response) != 0)
    {
        printf("Failed to receive ranking response.\n");
        return -1;
    }

    // Check response type
    if (strcmp(response.type, MSG_RANKING) != 0)
    {
        printf("Unexpected server response.\n");
        return -1;
    }

    // Check success
    if (response.response != RESP_SUCCESS)
    {
        printf("Failed to retrieve ranking: %s\n", response.content);
        return -1;
    }

    // Display ranking
    printf("\n========== AWALE RANKING ==========\n");
    printf("%-5s %-20s %s\n", "Rank", "Username", "Rating");
    printf("-----------------------------------\n");

    char content[MAX_CONTENT];
    snprintf(content, sizeof(content), "%s", response.content);

    int rank = 1;
    char *entry = strtok(content, ",");

    while (entry != NULL)
    {
        char username[MAX_NAME + 1];
        int rating;

        if (sscanf(entry, "%" STRINGIFY(MAX_NAME) "[^:]:%d",
                   username, &rating) == 2)
        {
            printf("%-5d %-20s %d\n", rank, username, rating);
            rank++;
        }

        entry = strtok(NULL, ",");
    }

    printf("===================================\n");

    return 0;
}

int main(void)
{
    WSADATA wsa;
    SOCKET client_socket;
    struct sockaddr_in server_addr;
    int game_id;
    int side;
    char opponent[MAX_NAME + 1];
    int menu_choice;
    int login_status;
    int logged_in = 0;

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
            else
            {
                printf("\nConnection lost. Exiting.\n");
                break;
            }
        }
    }

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
