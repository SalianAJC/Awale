//Message types and structures. Shared contract between the client and the server

/*

    TYPE|RESPONSE|MODE|RECEIVER|CONTENT\n

    TYPE     : message code, 1 to 2 characters (see the codes below)
    RESPONSE : S (success), F (failure) or - (not a reply)
    MODE     : O (observer), P (player) or - (not relevant)
    RECEIVER : player id written as text, 0 = not required
    CONTENT  : type-specific text, at most MAX_CONTENT characters

The receiver cuts the message at the first 4 '|' only, so CONTENT may contain '|'.
CONTENT must never contain '\n', since '\n' ends the message.

Every field travels as text, never as a raw struct or a pointer.
*/

#ifndef PROTOCOL_H
#define PROTOCOL_H

//Network

#define SERVER_PORT 8080

#define MSG_END '\n' //End of a message
#define MSG_SEP '|' //Separator between fields

//Limits

#define MAX_TYPE 2 //Message codes are 1 or 2 characters
#define MAX_CONTENT 1024 //Large enough for the move list sent by O and Sh (MAX_MOVES in game.h, up to 3 characters per move)
#define MAX_CHAT 200 //Chat messages, checked by the server
#define MAX_NAME 32 //Username
#define MAX_PASSWORD 32
#define BIO_SIZE 10 //10 ASCII characters
#define MAX_CLIENTS 50
#define MAX_GAMES 25
#define MAX_OBSERVERS 10 //Per game
#define MAX_FRIENDS 20 //Per player
#define MAX_LIST 50 //Max entries sent in one LO or LG reply

#define MAX_LINE (MAX_TYPE + 1 + 1 + 1 + 1 + 1 + 1 + 11 + 1 + MAX_CONTENT + 1) //Longest possible message, separators and '\n' included

//Response values

#define RESP_SUCCESS 'S'
#define RESP_FAILURE 'F'
#define RESP_NONE '-' //The message is not a reply

//Mode values

#define MODE_OBSERVER 'O'
#define MODE_PLAYER 'P'
#define MODE_NONE '-'

//Message codes. C->S = client to server, S->C = server to client

//Account
#define MSG_REGISTER "R" //C->S : username, password
#define MSG_LOGIN "L" //C->S : username, password
#define MSG_ERROR "E"

//Lobby
#define MSG_LIST_ONLINE "LO" //C->S request, S->C reply : list of online players
#define MSG_PLAYERS_ONLINE "S" //S->C : players online, sent after a login
#define MSG_LIST_GAMES "LG" //C->S request, S->C reply : list of ongoing games

//Challenge
#define MSG_CHALLENGE "C" //C->S (from A) : username of B
#define MSG_CHALLENGED "Ch" //S->C (to B) : username of A
#define MSG_ACCEPT "A" //C->S (from B) : username of A
#define MSG_REFUSE "Rf" //C->S (from B) : username of A
#define MSG_GAME_START "Gs" //S->C (to both) : game_id and who plays first. No board, every game starts with 4 seeds per hole

//Game
#define MSG_MOVE "Mo" //C->S : the hole played. The server checks it with sow()
#define MSG_BOARD_UPDATE "Bu" //S->C (to opponent and observers) : the move played only
#define MSG_GAME_OVER "Go" //S->C (to both) : result
#define MSG_OPPONENT_LEFT "Od" //S->C (to the remaining player) : back to the lobby

//Spectating and history
#define MSG_OBSERVE "O" //C->S : game_id. Reply : who played first and the moves so far
#define MSG_REVIEW_GAME "RG" //C->S : game_id
#define MSG_GAME_HISTORY "Sh" //S->C : who played first and the list of moves

//Chat and bio
#define MSG_CHAT "M" //C->S then S->C to the opponent : text, at most MAX_CHAT characters
#define MSG_WRITE_BIO "W" //C->S : bio text
#define MSG_READ_BIO "Rb" //C->S request, S->C reply : username, then the bio ("no bio" if empty)


// Resign, challenge, observe and Friedns
#define MSG_RESIGN "Rs" // in game, give up
#define MSG_STOP_OBSERVE "Ox" //stop watching
#define MSG_CHAT "M"
#define MAX_CHAT 200
#define MSG_FRIEND ADD "Fa"
#define MSG_FRIEND_REMOVE "Fr"
#define MSG_FRIEND_LIST "Fl"
#define MSG_PRIVATE "Pv"
#define MSG_HISTORY_LIST "Hl"
#define MSG_RANKING "Rk"
#define MAX_BIO_LINES 10
#define MAX_BIO_LINE 80
#define BIO_SEP ';'
#define MAX_SAVED_GAMES 100
#define START_RATING 1200
//Envelope

typedef struct {
    char type[MAX_TYPE + 1]; //Message code, +1 for '\0'
    char response; //RESP_SUCCESS, RESP_FAILURE or RESP_NONE
    char mode; //MODE_OBSERVER, MODE_PLAYER or MODE_NONE
    int receiver; //Target player id, 0 = not required
    char content[MAX_CONTENT + 1]; //Type-specific text, +1 for '\0'
} Message;


#endif