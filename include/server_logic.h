//Users, challenges, games and rules


#ifndef SERVER_LOGIC_H
#define SERVER_LOGIC_H

#include "protocol.h"

#define MAX_OUTBOX_ENTRIES (MAX_CLIENTS + 5)

typedef enum {
    STATE_OFFLINE,
    STATE_CONNECTED,
    STATE_IN_GAME,
    STATE_OBSERVING
} SessionState;

typedef struct {
    int player_id;
    char username[MAX_NAME + 1];
    char bio[BIO_SIZE + 1];
    int friends_ids[MAX_FRIENDS];
    int session_index;
    char Flag; // P for private, ... for Public ? 
} Account;

typedef struct {
    int in_use;
    int account_index;
    SessionState state;
    int challenge_target;
    int match_index;
    unsigned int generation;
} Session;

typedef struct {
    Message message;
    int session_index;
    unsigned int generation;
} OutboxEntry;

typedef struct {
    OutboxEntry entries[MAX_OUTBOX_ENTRIES];
    int count;
} Outbox;



// Public functions
Message registerUser(const Message *request);


/*
 * Returns the allocated session index, or -1 on failure.
 * Writes the generation only on success.
 * The generation pointer must not be NULL.
 */
int logic_init();

int logic_connect(unsigned int *generation);

void logic_handle_message(
    int session_index,
    const Message *request,
    Outbox *outbox);

void logic_disconnect(
    int session_index,
    Outbox *outbox);

#endif


