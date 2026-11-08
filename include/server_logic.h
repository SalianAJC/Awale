//Users, challenges, games and rules.
//Public interface of the server logic. server.c and the tests only use what is declared here.

#ifndef SERVER_LOGIC_H
#define SERVER_LOGIC_H

#include "protocol.h"

//At most one message per connected client, plus a few extra for the sender
//(for example the login reply followed by the S push)
#define MAX_OUTBOX_ENTRIES (MAX_CLIENTS + 5)

//State of a connection. STATE_CONNECTED comes first so that a zeroed session means "not logged in"
typedef enum {
    STATE_CONNECTED, //Connected, not logged in
    STATE_LOBBY,     //Logged in, not playing, not observing
    STATE_IN_GAME,   //Playing a game
    STATE_OBSERVING  //Watching a game
} SessionState;

//A registered player. Kept after the player disconnects
typedef struct {
    int player_id;
    char username[MAX_NAME + 1];
    char bio[BIO_SIZE + 1];
    int friends_ids[MAX_FRIENDS];
    int session_index; //Session the player is connected on, -1 = offline
    char Flag; //P for private, ... for Public ?
} Account;

//One connection. One slot per connected client, the slot number is the session index
typedef struct {
    int in_use;               //0 = free slot
    int account_index;        //Logged-in account, -1 = not logged in
    SessionState state;
    int challenge_target;     //Account challenged by this client, -1 = none
    int match_index;          //Game played or watched, -1 = none
    unsigned int generation;  //Incremented each time a new client takes the slot
} Session;

//One message to send, and who it is for
typedef struct {
    Message message;
    int session_index;
    unsigned int generation; //Generation of the recipient when the message was created
} OutboxEntry;

//Every message produced by one request
typedef struct {
    OutboxEntry entries[MAX_OUTBOX_ENTRIES];
    int count;
} Outbox;


/*
 * logic_init
 *   Called once by main, before the accept loop.
 *   Initializes the logic lock and resets every table.
 *   Returns 0 on success, -1 on failure.
 */
int logic_init(void);

/*
 * logic_connect
 *   Called by the accept loop after accept, while server.c holds socket_lock.
 *   Allocates a free session slot in STATE_CONNECTED and increments its generation.
 *   Returns the session index, or -1 if every slot is taken.
 *   Writes the generation only on success. generation must not be NULL.
 *   Sends nothing.
 */
int logic_connect(unsigned int *generation);

/*
 * logic_handle_message
 *   Called by a client thread for each message received.
 *   The caller passes an empty outbox. This function only appends to it.
 *   Checks the state rules, runs the handler for the message type,
 *   or replies E|F|-|0|unknown message.
 *   Always produces exactly one reply to the sender, plus any pushes.
 *   The caller sends the outbox afterwards.
 */
void logic_handle_message(int session_index,
                          const Message *request,
                          Outbox *outbox);

/*
 * logic_disconnect
 *   Called by disconnect_client when a client leaves, or when thread setup failed.
 *   The caller passes an empty outbox.
 *   Marks the account offline (the account is kept) and frees the session slot.
 *   The generation is kept: the next logic_connect on this slot increments it.
 *   Later: Go "left" to the opponent and observers, Rf for pending challenges.
 *   The caller sends the outbox, clears the socket, then closes it.
 */
void logic_disconnect(int session_index, Outbox *outbox);

#endif