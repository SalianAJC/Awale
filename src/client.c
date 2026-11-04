
 //user interface and client code

#include <stdio.h>
#include <winsock2.h>
#include <conio.h>
#include <string.h>

#include "network.h"

int main(void)
{
    WSADATA wsa;
    SOCKET client_socket; //client socket
    struct sockaddr_in server_addr;//adress
    Message message;//message 
    Message received_message; //received message
    char input[MAX_CHAT + 1];
    int input_length = 0;
    int running = 1;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    client_socket = socket(AF_INET, SOCK_STREAM, 0);//We create a socket and normalize it to 0

    if (client_socket == INVALID_SOCKET) { //If it's invalid kill
        printf("Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr)); //We use the server address to 0 to avoid garbage values
    server_addr.sin_family = AF_INET;//We set the address family to AF_INET for IPv4
    server_addr.sin_port = htons(SERVER_PORT);//Set the port to 8080
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");//Set the IP address to localhost

    if (connect(client_socket, (struct sockaddr *)&server_addr, //if the connection fails we die
                sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    printf("Connected to server.\n");
    printf("Type a message and press Enter. Use /quit to exit.\n");

    while (running) { //Main loop to keep the client running
        fd_set read_set;//Set of sockets to monitor for incoming data
        struct timeval timeout;//timeout for selection

        FD_ZERO(&read_set); //Clear the read_set
        FD_SET(client_socket, &read_set);//Add the client socket to the read_set

        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ready = select(0, &read_set, NULL, NULL, &timeout);//Wait for incoming data or timeout

        if (ready == SOCKET_ERROR) {
            printf("Select failed: %d\n", WSAGetLastError());
            break;
        }

        if (ready > 0 && FD_ISSET(client_socket, &read_set)) {
            if (receive_message(client_socket, &received_message) != 0) {//If we receive a message we print it
                printf("\nServer disconnected or communication failed.\n");
                break;
            }

            printf("\nServer: %s | %c | %c | %d | %s\n",
                   received_message.type,
                   received_message.response,
                   received_message.mode,
                   received_message.receiver,
                   received_message.content);
        }

        while (_kbhit()) {//Check if a key has been pressed
            int ch = _getch();//Get the character that was just pressed

            if (ch == '\r') {//If it's a carriage return we send the message
                putchar('\n');//Print a new line
                input[input_length] = '\0';//Null terminate the input string

                if (strcmp(input, "/quit") == 0) {//If the input is /quit we exit
                    running = 0;
                    break;
                }

                if (input_length > 0) {//If the imput is smaller than 0
                    strcpy(message.type, MSG_CHAT);//We set the message type to chat
                    message.response = RESP_NONE;//We set the response to none
                    message.mode = MODE_NONE;//NO mode
                    message.receiver = 0;//We set the receiver to 0
                    strcpy(message.content, input);//Copy message content

                    if (send_message(client_socket, &message) != 0) {//Failure check
                        printf("Failed to send message.\n");
                        running = 0;
                        break;
                    }
                }

                input_length = 0;
            }
            else if (ch == '\b') {
                if (input_length > 0) {
                    input_length--;
                    printf("\b \b");
                }
            }
            else if (ch >= 32 && ch <= 126) {
                if (input_length < MAX_CHAT) {
                    input[input_length++] = (char)ch;
                    putchar(ch);
                }
            }
        }
    }

    closesocket(client_socket);
    WSACleanup();

    return 0;
}
