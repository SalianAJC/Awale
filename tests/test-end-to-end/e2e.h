//Shared tools for the end-to-end tests.
//
//Every client is simulated the way the real client works: it keeps its own copy of
//the board, starts it on Gs, and plays a move on it only when a Bu arrives.
//The tests only use the public interface of server_logic and read the outbox,
//so no network is involved.

#ifndef E2E_H
#define E2E_H

#include "server_logic.h"
#include "game.h"

typedef struct {
    const char *name;
    int session;
    unsigned int generation;

    //Game state, as the real client tracks it
    int in_game;
    int observing;                  //watching a game (after a successful O)
    int game_id;
    int side;
    char opponent[MAX_NAME + 1];
    Game board;

    //What this client received
    int got_go;                     //number of Go messages
    char last_go[MAX_CONTENT + 1];
    int bu_count;                   //number of Bu applied
    char last_bu_mode;              //mode of the last Bu (P for players, O for observers)
    char last_go_mode;              //mode of the last Go
    int board_errors;               //Bu that could not be applied to the board
    int chat_count;
    char last_chat[MAX_CONTENT + 1];
    int rf_count;
    char last_rf[MAX_CONTENT + 1];
} Client;

//The outbox of the last request
extern Outbox e2e_outbox;

// ---------- Results ----------

void e2e_report(int ok, const char *label, const char *details);
int e2e_summary(void); //Prints the totals, returns the exit code (0 = all passed)

// ---------- Setup and clients ----------

int e2e_init(void);                              //Deletes the test files, calls logic_init
int e2e_join(Client *c, const char *name);       //Connects, registers, logs in. 0 on success
int e2e_reconnect(Client *c);                    //New connection + login for an existing account
void e2e_leave(Client *c);                       //The client's socket closes

// ---------- Requests and replies ----------

void e2e_send(Client *c, const char *type, const char *content);
const Message *e2e_reply(const Client *c, const char *type);
int e2e_reply_is(const Client *c, const char *type, char response, const char *content);
void e2e_describe_outbox(char *buffer, size_t size);

// ---------- Games ----------

int e2e_start_game(Client *challenger, Client *accepter); //0 if both received their Gs
Client *e2e_to_move(Client *a, Client *b);                //Whose turn it is
int e2e_pick_legal_hole(const Client *c);                  //-1 if none
int e2e_play_one_move(Client *a, Client *b);              //0 if accepted
int e2e_boards_equal(const Game *x, const Game *y);
int e2e_parse_go(const char *content, int *id, int *cap0, int *cap1,
                 char *winner, char *reason);

// ---------- Lobby queries ----------

int e2e_rating_of(Client *asker, const char *name);       //-1 if not listed
int e2e_online_shows(Client *asker, const char *item);    //e.g. "alice:L"

#endif
