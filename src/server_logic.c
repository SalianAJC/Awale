// Users, challenges, games and rules

#include "server_logic.h"
#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <string.h>

// Private server state
int actual_count = 0;
Session sessions[MAX_CLIENTS];
Account Active_players[MAX_CLIENTS];

CRITICAL_SECTION logic_lock;


// Private helper declarations
static int gen_id(int a);
static int isUsernameEmpty(const char *username);
static int isUsernameTooLong(const char *username);
static int isServerFull(void);
static int usernameExists(const char *username);
static int hasIllegalCharacters(const char *username);

static void outbox_add(Outbox *outbox, int recipient,
                       const Message *message);

static Message make_message(const char *type, char response,
                            char mode, const char *content);


// -------------------------------- INITIALIZATION --------------------------------

int logic_init(void)
{
    InitializeCriticalSection(&logic_lock);

    EnterCriticalSection(&logic_lock);

    actual_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].in_use = 0;
        sessions[i].account_index = -1;
        sessions[i].challenge_target = -1;
        sessions[i].match_index = -1;
        sessions[i].state = STATE_CONNECTED;
        sessions[i].generation = 0;
    }

    LeaveCriticalSection(&logic_lock);

    return 0;
}


// -------------------------------- CONNECTION --------------------------------

int logic_connect(unsigned int *generation)
{
    if (generation == NULL) {
        return -1;
    }

    EnterCriticalSection(&logic_lock);

    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (sessions[i].in_use == 0) {

            // Found a free session
            sessions[i].in_use = 1;
            sessions[i].account_index = -1;
            sessions[i].challenge_target = -1;
            sessions[i].match_index = -1;
            sessions[i].generation++;
            *generation = sessions[i].generation;
            sessions[i].state = STATE_CONNECTED;

            LeaveCriticalSection(&logic_lock);

            return i;
        }
    }

    // No free session
    LeaveCriticalSection(&logic_lock);

    return -1;
}


// -------------------------------- OUTBOX --------------------------------

static void outbox_add(Outbox *outbox, int recipient,
                       const Message *message)
{
    if (outbox->count >= MAX_OUTBOX_ENTRIES) {
        printf("Error: outbox is full. Message dropped.\n");
        return;
    }

    OutboxEntry *entry = &outbox->entries[outbox->count];

    entry->message = *message;
    entry->session_index = recipient;
    entry->generation = sessions[recipient].generation;

    outbox->count++;
}


// -------------------------------- MESSAGE HELPER --------------------------------

static Message make_message(const char *type, char response,
                            char mode, const char *content)
{
    Message message = {0};

    strcpy(message.type, type);

    message.response = response;
    message.mode = mode;
    message.receiver = 0;

    if (content != NULL) {
        strcpy(message.content, content);
    }

    return message;
}


// -------------------------------- MESSAGE HANDLING --------------------------------

void logic_handle_message(int session_index,
                          const Message *request,
                          Outbox *outbox)
{
    if (request == NULL || outbox == NULL) {
        return;
    }



    EnterCriticalSection(&logic_lock);

    // Check session index
    if (session_index < 0 || session_index >= MAX_CLIENTS) {
        printf("Error: invalid session index.\n");
        goto end;
    }

    // Check whether session is in use
    if (sessions[session_index].in_use == 0) {
        printf("Error: session is not in use.\n");
        goto end;
    }

    SessionState state = sessions[session_index].state;


    // -------------------------------- STATE CHECK --------------------------------

    // For now, only registration is implemented.
    if (strcmp(request->type, MSG_REGISTER) == 0) {

        if (state != STATE_CONNECTED) {

            Message response = make_message(
                MSG_REGISTER,
                RESP_FAILURE,
                MODE_NONE,
                "not allowed now"
            );

            outbox_add(outbox, session_index, &response);

            goto end;
        }
    }

    else {

        // Unknown or not-yet-implemented message
        Message response = make_message(
            MSG_ERROR,
            RESP_FAILURE,
            MODE_NONE,
            "unknown message"
        );

        outbox_add(outbox, session_index, &response);

        goto end;
    }


    // -------------------------------- DISPATCH --------------------------------

    if (strcmp(request->type, MSG_REGISTER) == 0) {

        Message response = registerUser(request);

        outbox_add(outbox, session_index, &response);
    }


end:

    LeaveCriticalSection(&logic_lock);
}


// -------------------------------- REGISTRATION --------------------------------




/*
logic_disconnect(int session_index, Outbox *outbox)
  Called by disconnect_client when a client leaves or thread setup failed.
  The outbox arrives empty.
  Under the logic lock: if logged in, sets the account's session_index = -1
  (the account is kept); frees the slot: in_use = 0, links = -1,
  generation kept (the next connect increments it).
  Later: Go "left" to the opponent and observers, Rf for pending challenges.
  The caller sends the outbox, clears the socket, then closes it.

*/

void logic_disconnect(int session_index, Outbox *outbox)
{
    if (outbox == NULL) return;

    EnterCriticalSection(&logic_lock);

    if (session_index < 0 || session_index >= MAX_CLIENTS)
        goto end;

    if (sessions[session_index].in_use == 0)
        goto end;

    int account_index = sessions[session_index].account_index;

    // Mark the account as offline, but keep it.
    if (account_index >= 0 && account_index < actual_count)
        Active_players[account_index].session_index = -1;

    // Free the session slot.
    sessions[session_index].in_use = 0;
    sessions[session_index].account_index = -1;
    sessions[session_index].challenge_target = -1;
    sessions[session_index].match_index = -1;
    sessions[session_index].state = STATE_CONNECTED;

    // Generation is kept for the next connection.

end:
    LeaveCriticalSection(&logic_lock);
}





//---------------------------------FUNCTIONALITIES-----------------------------------




// ------ Registering
Message registerUser(const Message *request)
{
    Message response = {0};

    // Initialise fields that are the same for every response
    strcpy(response.type, MSG_REGISTER);
    response.mode = MODE_NONE;
    response.receiver = 0;

    // Extract registration information
    const char *username = request->content;

    // Failure cases

    if (isUsernameEmpty(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username can't be empty");
        return response;
    }

    if (isUsernameTooLong(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username can't exceed 32 characters");
        return response;
    }

    if (isServerFull()) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Server is full");
        return response;
    }

    if (usernameExists(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username already exists");
        return response;
    }

    if (hasIllegalCharacters(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username contains illegal characters");
        return response;
    }

    // All requirements passed: continue to register

    int id = gen_id(actual_count);

    Account player = {0};

    player.player_id = id;
    player.session_index = -1;

    strcpy(player.username, username);

    // Add the user and increment count
    Active_players[actual_count] = player;
    actual_count++;

    // Prepare success response
    strcpy(response.content, "registered");
    response.response = RESP_SUCCESS;

    return response;
}




// -------------------------------- PRIVATE HELPERS --------------------------------

// Generate player ID
static int gen_id(int a)
{
    return a + 1;
}


// Check if username is empty
static int isUsernameEmpty(const char *username)
{
    return strlen(username) == 0;
}


// Check if username exceeds MAX_NAME
static int isUsernameTooLong(const char *username)
{
    return strlen(username) > MAX_NAME;
}


// Check if server is full
static int isServerFull(void)
{
    return actual_count >= MAX_CLIENTS;
}


// Check if username already exists
static int usernameExists(const char *username)
{
    for (int i = 0; i < actual_count; i++) {

        if (strcmp(Active_players[i].username, username) == 0) {
            return 1;
        }
    }

    return 0;
}


// Check for illegal characters
static int hasIllegalCharacters(const char *username)
{
    for (int i = 0; username[i] != '\0'; i++) {

        char c = username[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_')) {

            return 1;
        }
    }

    return 0;
}