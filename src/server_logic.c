//Users, challenges, games and rules.
//
//Organisation of this file:
//  1. Private state (tables and lock)
//  2. Helpers (messages, outbox, accounts)
//  3. Handlers, one per message type
//  4. The message table: which states allow each type, and its handler
//  5. Public functions (init, connect, handle_message, disconnect)
//
//To add a new message type:
//  1. write its handler in section 3, with the HANDLER signature,
//  2. add one row to the message table in section 4.
//Nothing else changes.
//
//Locking: every public function takes logic_lock itself.
//Handlers and helpers run with the lock already held and never touch it.

#include <stdio.h>
#include <string.h>
#include <windows.h> //CRITICAL_SECTION. No networking in this file

#include "server_logic.h"


// ================================================================
// 1. PRIVATE STATE
// ================================================================

static Account accounts[MAX_CLIENTS]; //Registered players, filled in order
static int account_count = 0;         //Slots 0 to account_count - 1 are used

static Session sessions[MAX_CLIENTS]; //Connected clients, the index is the session index

static CRITICAL_SECTION logic_lock;


// ================================================================
// 2. HELPERS
// ================================================================

// ---------- Messages ----------

//Builds a message. content may be NULL for an empty content.
//The content is cut if it is longer than MAX_CONTENT, never overflowing the buffer
static Message make_message(const char *type, char response,
                            char mode, const char *content)
{
    Message message = {0};

    snprintf(message.type, sizeof(message.type), "%s", type);
    message.response = response;
    message.mode = mode;
    message.receiver = 0;

    if (content != NULL) {
        snprintf(message.content, sizeof(message.content), "%s", content);
    }

    return message;
}

// ---------- Outbox ----------

//Adds a message for a session, stamped with that session's current generation
static void outbox_add(Outbox *outbox, int recipient, const Message *message)
{
    if (recipient < 0 || recipient >= MAX_CLIENTS) {
        printf("Error: outbox_add with invalid session %d. Message dropped.\n", recipient);
        return;
    }

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

//Shortcut: builds a reply and adds it for one session
static void reply(Outbox *outbox, int recipient, const char *type,
                  char response, const char *content)
{
    Message message = make_message(type, response, MODE_NONE, content);
    outbox_add(outbox, recipient, &message);
}

// ---------- Accounts ----------

//Returns the index of the account with this username, or -1 if none
static int find_account(const char *username)
{
    for (int i = 0; i < account_count; i++) {
        if (strcmp(accounts[i].username, username) == 0) {
            return i;
        }
    }

    return -1;
}

//Defined with the LO handler, used by login too
static void build_online_list(char *buffer, size_t size);

//1 if the username only contains letters, digits and '_'
static int has_only_legal_characters(const char *username)
{
    for (int i = 0; username[i] != '\0'; i++) {
        char c = username[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_')) {
            return 0;
        }
    }

    return 1;
}


// ================================================================
// 3. HANDLERS
// ================================================================
//
//Every handler has the same signature:
//  session : index of the session that sent the request
//  request : the message received
//  outbox  : where to add the reply and any pushes
//The dispatcher has already checked that the type is allowed in the session's state.
//A handler must add exactly one reply to the sender.

typedef void (*Handler)(int session, const Message *request, Outbox *outbox);


// ---------- R : register ----------

static void handle_register(int session, const Message *request, Outbox *outbox)
{
    const char *username = request->content;
    size_t length = strlen(username);

    //Checks that need no data first, then the ones that scan the tables

    if (length == 0) {
        reply(outbox, session, MSG_REGISTER, RESP_FAILURE, "Username can't be empty");
        return;
    }

    if (length > MAX_NAME) {
        char reason[64];
        snprintf(reason, sizeof(reason), "Username can't exceed %d characters", MAX_NAME);
        reply(outbox, session, MSG_REGISTER, RESP_FAILURE, reason);
        return;
    }

    if (!has_only_legal_characters(username)) {
        reply(outbox, session, MSG_REGISTER, RESP_FAILURE, "Username contains illegal characters");
        return;
    }

    if (find_account(username) != -1) {
        reply(outbox, session, MSG_REGISTER, RESP_FAILURE, "Username already exists");
        return;
    }

    if (account_count >= MAX_CLIENTS) {
        reply(outbox, session, MSG_REGISTER, RESP_FAILURE, "Server is full");
        return;
    }

    //All checks passed: create the account. Registering does not log in
    Account *account = &accounts[account_count];

    memset(account, 0, sizeof(*account));
    account->player_id = account_count + 1;
    account->session_index = -1; //Offline until login
    memcpy(account->username, username, length + 1); //length <= MAX_NAME was checked above

    account_count++;

    reply(outbox, session, MSG_REGISTER, RESP_SUCCESS, "registered");
}


// ---------- L : login ----------

static void handle_login(int session, const Message *request, Outbox *outbox)
{
    const char *username = request->content;
    size_t length = strlen(username);

    //Checks that need no data first, then the ones that scan the tables

    if (length == 0) {
        reply(outbox, session, MSG_LOGIN, RESP_FAILURE, "Username can't be empty");
        return;
    }

    if (length > MAX_NAME) {
        char reason[64];
        snprintf(reason, sizeof(reason), "Username can't exceed %d characters", MAX_NAME);
        reply(outbox, session, MSG_LOGIN, RESP_FAILURE, reason);
        return;
    }

    if (!has_only_legal_characters(username)) {
        reply(outbox, session, MSG_LOGIN, RESP_FAILURE, "Username contains illegal characters");
        return;
    }

    //Look up the account
    int account_index = find_account(username);

    if (account_index == -1) {
        reply(outbox, session, MSG_LOGIN, RESP_FAILURE, "unknown user");
        return;
    }

    //Someone is already logged in as this account, from another client
    if (accounts[account_index].session_index != -1) {
        reply(outbox, session, MSG_LOGIN, RESP_FAILURE, "already connected");
        return;
    }

    //Link the account and the session, in both directions
    accounts[account_index].session_index = session;
    sessions[session].account_index = account_index;
    sessions[session].state = STATE_LOBBY;

    reply(outbox, session, MSG_LOGIN, RESP_SUCCESS, username);

    //Then push the online list to the new player (spec 3.1)
    char list[MAX_CONTENT + 1];
    build_online_list(list, sizeof(list));

    reply(outbox, session, MSG_PLAYERS_ONLINE, RESP_NONE, list);
}

// ---------- LO : list online players ----------

//Letter used for a state in the online list: L lobby, G in game, O observing
static char state_letter(SessionState state)
{
    switch (state) {
        case STATE_LOBBY:     return 'L';
        case STATE_IN_GAME:   return 'G';
        case STATE_OBSERVING: return 'O';
        case STATE_CONNECTED: return 'C'; //Never listed: not logged in
    }

    return '?';
}

//Fills buffer with the online players, as "name:L,name:G,...", in session order.
//Shared by LO and by the S push after login.
//Stops before an entry that would not fit, so a name is never cut in half
static void build_online_list(char *buffer, size_t size)
{
    buffer[0] = '\0';

    for (int i = 0; i < MAX_CLIENTS; i++) {
        const Session *s = &sessions[i];

        //Free slot, or connected but not logged in
        if (!s->in_use || s->account_index == -1) {
            continue;
        }

        const Account *user = &accounts[s->account_index];

        //The comma goes before every entry except the first: no trailing comma
        char entry[1 + MAX_NAME + 2 + 1];
        snprintf(entry, sizeof(entry), "%s%s%c%c",
                 buffer[0] != '\0' ? "," : "",
                 user->username,
                 ':',
                 state_letter(s->state));

        //No room left for the whole entry (and the '\0'): stop the list here
        if (strlen(entry) >= size - strlen(buffer)) {
            break;
        }

        strcat(buffer, entry);
    }
}

static void handle_list_online(int session, const Message *request, Outbox *outbox)
{
    (void)request; //LO has no content

    char list[MAX_CONTENT + 1];
    build_online_list(list, sizeof(list));

    reply(outbox, session, MSG_LIST_ONLINE, RESP_SUCCESS, list);
}


// ================================================================
// 4. MESSAGE TABLE
// ================================================================
//
//For each message type: the states in which it is allowed (spec 2.4), and its handler.
//States are combined as bits: ALLOW(STATE_LOBBY) | ALLOW(STATE_IN_GAME) means "in the lobby or in a game".
//A type missing from this table gets E|F|-|0|unknown message.

#define ALLOW(state) (1u << (state))

#define ALLOW_BEFORE_LOGIN (ALLOW(STATE_CONNECTED))
#define ALLOW_LOGGED_IN    (ALLOW(STATE_LOBBY) | ALLOW(STATE_IN_GAME) | ALLOW(STATE_OBSERVING))
#define ALLOW_LOBBY        (ALLOW(STATE_LOBBY))
#define ALLOW_IN_GAME      (ALLOW(STATE_IN_GAME))
#define ALLOW_OBSERVING    (ALLOW(STATE_OBSERVING))

typedef struct {
    const char *type;
    unsigned int allowed_states;
    Handler handler;
} MessageRule;

static const MessageRule message_table[] = {
    //Account
    { MSG_REGISTER,    ALLOW_BEFORE_LOGIN, handle_register },
    { MSG_LOGIN,       ALLOW_BEFORE_LOGIN, handle_login },

    //Lobby
    { MSG_LIST_ONLINE, ALLOW_LOGGED_IN,    handle_list_online },

    //Challenge
    //{ MSG_CHALLENGE,   ALLOW_LOBBY,        handle_challenge },
    //{ MSG_ACCEPT,      ALLOW_LOBBY,        handle_accept },
    //{ MSG_REFUSE,      ALLOW_LOBBY,        handle_refuse },

    //Game
    //{ MSG_MOVE,        ALLOW_IN_GAME,      handle_move },

    //Chat
    //{ MSG_CHAT,        ALLOW_LOGGED_IN,    handle_chat },
};

#define MESSAGE_TABLE_SIZE ((int)(sizeof(message_table) / sizeof(message_table[0])))

//Returns the rule for this type, or NULL if the type is unknown
static const MessageRule *find_rule(const char *type)
{
    for (int i = 0; i < MESSAGE_TABLE_SIZE; i++) {
        if (strcmp(message_table[i].type, type) == 0) {
            return &message_table[i];
        }
    }

    return NULL;
}


// ================================================================
// 5. PUBLIC FUNCTIONS
// ================================================================

int logic_init(void)
{
    InitializeCriticalSection(&logic_lock);

    //No other thread exists yet, so no lock is needed here
    account_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].in_use = 0;
        sessions[i].account_index = -1;
        sessions[i].state = STATE_CONNECTED;
        sessions[i].challenge_target = -1;
        sessions[i].match_index = -1;
        sessions[i].generation = 0;
    }

    return 0;
}


int logic_connect(unsigned int *generation)
{
    if (generation == NULL) {
        return -1;
    }

    int index = -1;

    EnterCriticalSection(&logic_lock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!sessions[i].in_use) {
            index = i;
            break;
        }
    }

    if (index != -1) {
        Session *s = &sessions[index];

        s->in_use = 1;
        s->account_index = -1;
        s->state = STATE_CONNECTED;
        s->challenge_target = -1;
        s->match_index = -1;
        s->generation++;

        *generation = s->generation;
    }

    LeaveCriticalSection(&logic_lock);

    return index;
}


void logic_handle_message(int session_index, const Message *request, Outbox *outbox)
{
    if (request == NULL || outbox == NULL) {
        return;
    }

    EnterCriticalSection(&logic_lock);

    if (session_index < 0 || session_index >= MAX_CLIENTS ||
        !sessions[session_index].in_use) {
        printf("Error: message from invalid session %d.\n", session_index);
        goto end;
    }

    const MessageRule *rule = find_rule(request->type);

    //Unknown type
    if (rule == NULL) {
        reply(outbox, session_index, MSG_ERROR, RESP_FAILURE, "unknown message");
        goto end;
    }

    //Known type, wrong state
    SessionState state = sessions[session_index].state;

    if ((rule->allowed_states & ALLOW(state)) == 0) {
        const char *reason = (state == STATE_CONNECTED) ? "not logged in" : "not allowed now";
        reply(outbox, session_index, rule->type, RESP_FAILURE, reason);
        goto end;
    }

    //Allowed: run the handler
    rule->handler(session_index, request, outbox);

end:
    LeaveCriticalSection(&logic_lock);
}


void logic_disconnect(int session_index, Outbox *outbox)
{
    (void)outbox; //Will receive Go "left" and Rf pushes (spec section 4)

    if (outbox == NULL) {
        return;
    }

    EnterCriticalSection(&logic_lock);

    if (session_index < 0 || session_index >= MAX_CLIENTS ||
        !sessions[session_index].in_use) {
        goto end;
    }

    Session *s = &sessions[session_index];

    //Mark the account offline, but keep it
    if (s->account_index >= 0 && s->account_index < account_count) {
        accounts[s->account_index].session_index = -1;
    }

    //TODO (spec section 4): end the game, remove from observers, cancel challenges

    //Free the slot. The generation is kept for the next connection
    s->in_use = 0;
    s->account_index = -1;
    s->state = STATE_CONNECTED;
    s->challenge_target = -1;
    s->match_index = -1;

end:
    LeaveCriticalSection(&logic_lock);
}