// Users, challenges, games and rules.
//
// Organisation:
//  1. Private state
//  2. Helpers
//  3. Handlers
//  4. Message table
//  5. Public functions
//
// Locking:
// Every public function takes logic_lock itself.
// Handlers and helpers run with the lock already held.

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "match.h"
#include "server_logic.h"


// ================================================================
// 1. PRIVATE STATE
// ================================================================

static Account accounts[MAX_CLIENTS];
static int account_count = 0;

static Match matches[MAX_GAMES];
static Session sessions[MAX_CLIENTS];

static int next_game_id = 1;

static CRITICAL_SECTION logic_lock;


// ================================================================
// 2. HELPERS
// ================================================================

// ---------- Messages ----------

// Builds a message. Content may be NULL.
// Content is truncated if it exceeds the message buffer.
static Message make_message(
    const char *type,
    char response,
    char mode,
    const char *content
)
{
    Message message = {0};

    snprintf(message.type, sizeof(message.type), "%s", type);

    message.response = response;
    message.mode = mode;
    message.receiver = 0;

    if (content != NULL) {
        snprintf(
            message.content,
            sizeof(message.content),
            "%s",
            content
        );
    }

    return message;
}




// ---------- Outbox ----------

// Adds a message for a session, stamped with its current generation.
static void outbox_add(
    Outbox *outbox,
    int recipient,
    const Message *message
)
{
    if (recipient < 0 || recipient >= MAX_CLIENTS) {
        printf(
            "Error: outbox_add with invalid session %d. Message dropped.\n",
            recipient
        );
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


// Builds a message and adds it to the recipient's outbox.
static void send_to(
    Outbox *outbox,
    int session,
    const char *type,
    char mode,
    char response,
    const char *content
)
{
    if (session < 0 || session >= MAX_CLIENTS) {
        return;
    }

    Message message = make_message(
        type,
        response,
        mode,
        content
    );

    outbox_add(outbox, session, &message);
}


// Sends a push message.
static void push(
    Outbox *outbox,
    int session,
    const char *type,
    char mode,
    const char *content
)
{
    send_to(
        outbox,
        session,
        type,
        mode,
        RESP_NONE,
        content
    );
}


// Sends a reply. Replies use MODE_NONE.
static void reply(
    Outbox *outbox,
    int session,
    const char *type,
    char response,
    const char *content
)
{
    send_to(
        outbox,
        session,
        type,
        MODE_NONE,
        response,
        content
    );
}

static int find_free_match(){
    int m = -1; // assume server is full
    for (int i = 0; i < MAX_GAMES; i++)
    {
        int status = matches[i].in_use;
        if (status == 0){ // found a free match
            return i;
        }
    }
    return m;

}

static void cancel_challenges_to(int account, Outbox *outbox){
    for (int i = 0; i < MAX_CLIENTS; i++){
        if (sessions[i].in_use == 0){
            continue;
        }
        if (sessions[i].challenge_target == account){
            sessions[i].challenge_target = -1;
            push(outbox, i, MSG_REFUSE, MODE_NONE, accounts[account].username);
        }
    }
}

// ---------- Accounts ----------

// Returns the account index, or -1 if the username doesn't exist.
static int find_account(const char *username)
{
    for (int i = 0; i < account_count; i++) {
        if (strcmp(accounts[i].username, username) == 0) {
            return i;
        }
    }

    return -1;
}


// Returns 1 if the username contains only letters, digits and '_'.
static int has_only_legal_characters(const char *username)
{
    for (int i = 0; username[i] != '\0'; i++) {
        char c = username[i];

        if (!(
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_'
        )) {
            return 0;
        }
    }

    return 1;
}


// Defined with the online-list handler, also used by login.
static void build_online_list(char *buffer, size_t size);


// ================================================================
// 3. HANDLERS
// ================================================================
//
// Every handler has the same signature:
//
// session : session index of the sender
// request : received message
// outbox  : replies and pushes to send
//
// The dispatcher has already checked the session state.
// Each handler must add exactly one reply to the sender.

typedef void (*Handler)(
    int session,
    const Message *request,
    Outbox *outbox
);


// ---------- R : register ----------

static void handle_register(
    int session,
    const Message *request,
    Outbox *outbox
)
{
    const char *username = request->content;
    size_t length = strlen(username);

    if (length == 0) {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Username can't be empty"
        );
        return;
    }

    if (length > MAX_NAME) {
        char reason[64];

        snprintf(
            reason,
            sizeof(reason),
            "Username can't exceed %d characters",
            MAX_NAME
        );

        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            reason
        );
        return;
    }

    if (!has_only_legal_characters(username)) {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Username contains illegal characters"
        );
        return;
    }

    if (find_account(username) != -1) {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Username already exists"
        );
        return;
    }

    if (account_count >= MAX_CLIENTS) {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Server is full"
        );
        return;
    }

    // Create the account. Registration does not log in.
    Account *account = &accounts[account_count];

    memset(account, 0, sizeof(*account));

    account->player_id = account_count + 1;
    account->session_index = -1;
    account->rating = START_RATING;

    memcpy(account->username, username, length + 1);

    account_count++;

    reply(
        outbox,
        session,
        MSG_REGISTER,
        RESP_SUCCESS,
        "registered"
    );
}


// ---------- L : login ----------

static void handle_login(
    int session,
    const Message *request,
    Outbox *outbox
)
{
    const char *username = request->content;
    size_t length = strlen(username);

    if (length == 0) {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "Username can't be empty"
        );
        return;
    }

    if (length > MAX_NAME) {
        char reason[64];

        snprintf(
            reason,
            sizeof(reason),
            "Username can't exceed %d characters",
            MAX_NAME
        );

        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            reason
        );
        return;
    }

    if (!has_only_legal_characters(username)) {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "Username contains illegal characters"
        );
        return;
    }

    int account_index = find_account(username);

    if (account_index == -1) {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "unknown user"
        );
        return;
    }

    if (accounts[account_index].session_index != -1) {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "already connected"
        );
        return;
    }

    // Link the account and session in both directions.
    accounts[account_index].session_index = session;
    sessions[session].account_index = account_index;
    sessions[session].state = STATE_LOBBY;

    reply(
        outbox,
        session,
        MSG_LOGIN,
        RESP_SUCCESS,
        username
    );

    // Push the current online list to the newly logged-in player.
    char list[MAX_CONTENT + 1];

    build_online_list(list, sizeof(list));

    push(
        outbox,
        session,
        MSG_PLAYERS_ONLINE,
        MODE_NONE,
        list
    );
}


// ---------- LO : list online players ----------

// State letters:
// L = lobby
// G = in game
// O = observing
static char state_letter(SessionState state)
{
    switch (state) {
        case STATE_LOBBY:
            return 'L';

        case STATE_IN_GAME:
            return 'G';

        case STATE_OBSERVING:
            return 'O';

        case STATE_CONNECTED:
            return 'C';
    }

    return '?';
}


// Builds a list such as:
// alice:L,bob:G,carol:O
//
// Connected but not logged-in sessions are excluded.
// An entry is never partially written.
static void build_online_list(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }

    buffer[0] = '\0';

    for (int i = 0; i < MAX_CLIENTS; i++) {
        const Session *session = &sessions[i];

        if (!session->in_use || session->account_index == -1) {
            continue;
        }

        if (
            session->account_index < 0 ||
            session->account_index >= account_count
        ) {
            continue;
        }

        const Account *user = &accounts[session->account_index];

        char entry[1 + MAX_NAME + 2 + 1];

        snprintf(
            entry,
            sizeof(entry),
            "%s%s:%c",
            buffer[0] != '\0' ? "," : "",
            user->username,
            state_letter(session->state)
        );

        size_t current_length = strlen(buffer);
        size_t entry_length = strlen(entry);

        // Leave room for the terminating '\0'.
        if (entry_length >= size - current_length) {
            break;
        }

        strcat(buffer, entry);
    }
}


static void handle_list_online(
    int session,
    const Message *request,
    Outbox *outbox
)
{
    (void)request;

    char list[MAX_CONTENT + 1];

    build_online_list(list, sizeof(list));

    reply(
        outbox,
        session,
        MSG_LIST_ONLINE,
        RESP_SUCCESS,
        list
    );
}


// C : Challenge 
static void handle_challenge(int session, const Message *request, Outbox *outbox){
    
    int me = sessions[session].account_index;
    int b = find_account(request->content);


    char content[MAX_CONTENT];

    if (sessions[session].challenge_target != -1){ // sender already has an outgoing challenge
        // construct content
        snprintf(content,sizeof(content),"%s","challenge already pending");

        // reply to offline player
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);


        //quit the function
        return; 
    }

    if (b == -1){ // challenge sent to offline player
        // construct content
        snprintf(content,sizeof(content),"%s","unknown user");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return; 
    }

    if (b == me){ // user tried to challenge themselves
        // construct content
        snprintf(content,sizeof(content),"%s","cannot challenge yourself");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return; 
    }

    int bs = accounts[b].session_index;
    if (bs == -1){ // user tried to challenge an offline user
        // construct content
        snprintf(content,sizeof(content),"%s","user offline");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return; 
    }

    if (sessions[bs].state != STATE_LOBBY){ // user tried to challenge a busy user
        // construct content
        snprintf(content,sizeof(content),"%s","user busy");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return; 
    }

    // In case of success
    
    //Set the session challenge to b
    sessions[session].challenge_target = b;
    reply(outbox, session, MSG_CHALLENGE, RESP_SUCCESS, accounts[b].username);
    push(outbox, bs, MSG_CHALLENGED, MODE_NONE, accounts[me].username);

}

static void handle_refuse(int session, const Message *request, Outbox *outbox) {

    // Here, sender is b, receiver is a

    int b = sessions[session].account_index;
    int a = find_account(request->content);

    if (a == -1) {
        reply(outbox, session, MSG_REFUSE, RESP_FAILURE, "no challenge from this user");
        return;
    }

    int as = accounts[a].session_index;

    if (as == -1 || sessions[as].challenge_target != b) {
        reply(outbox, session, MSG_REFUSE, RESP_FAILURE, "no challenge from this user");
        return;
    }

    // Clear the challenger's pending challenge.
    sessions[as].challenge_target = -1;

    // Reply to the refusing player.
    reply(outbox, session, MSG_REFUSE, RESP_SUCCESS, accounts[a].username);

    // Notify the original challenger.
    push(outbox, as, MSG_REFUSE, MODE_NONE, accounts[b].username);
}

static void handle_accept(int session, const Message *request, Outbox *outbox){

    // Same here. Sender is b, Receiver is a
    int b = sessions[session].account_index;
    int a = find_account(request->content);

    if (a == -1) {
        reply(outbox, session, MSG_ACCEPT, RESP_FAILURE, "no challenge from this user");
        return;
    }

    int as = accounts[a].session_index;

    if (as == -1 || sessions[as].challenge_target != b) {
        reply(outbox, session, MSG_ACCEPT, RESP_FAILURE, "no challenge from this user");
        return;
    }

    if (sessions[as].state != STATE_LOBBY){
        reply(outbox, session, MSG_ACCEPT, RESP_FAILURE, "user busy");
        return;
    }

    int m = find_free_match();
    if (m == -1 ){
        reply(outbox, session, MSG_ACCEPT, RESP_FAILURE, "server full");
        return;
    }

    // Success. Start a game
    matches[m].in_use = 1;
    matches[m].game_id = next_game_id++;
    init_game(&matches[m].game);
    matches[m].move_count = 0;
    matches[m].observer_count = 0;

    // Assign sides
    int side_a;
    int side_b;

    if (rand() % 2 == 0){
        side_a = 0;
        side_b = 1;
    } else {
        side_a = 1;
        side_b = 0;
    }

    matches[m].side_account[side_a] = a;
    matches[m].side_account[side_b] = b;

    // Update both sessions
    sessions[as].state = STATE_IN_GAME;
    sessions[as].match_index = m;
    sessions[as].challenge_target = -1;

    sessions[session].state = STATE_IN_GAME;
    sessions[session].match_index = m;
    sessions[session].challenge_target = -1;

    // Cancel other pending challenges
    cancel_challenges_to(a, outbox);
    cancel_challenges_to(b, outbox);

    // Reply to the accepting player
    reply(outbox, session, MSG_ACCEPT, RESP_SUCCESS, accounts[a].username);

    // Notify both players
    char content_a[MAX_CONTENT];
    char content_b[MAX_CONTENT];

    snprintf(content_a, sizeof(content_a), "%d %d %s",
        matches[m].game_id, side_a, accounts[b].username);

    snprintf(content_b, sizeof(content_b), "%d %d %s",
        matches[m].game_id, side_b, accounts[a].username);

    push(outbox, as, MSG_GAME_START, MODE_PLAYER, content_a);
    push(outbox, session, MSG_GAME_START, MODE_PLAYER, content_b);
}

// ================================================================
// 4. MESSAGE TABLE
// ================================================================

#define ALLOW(state) (1u << (state))

#define ALLOW_BEFORE_LOGIN \
    (ALLOW(STATE_CONNECTED))

#define ALLOW_LOGGED_IN \
    (ALLOW(STATE_LOBBY) | ALLOW(STATE_IN_GAME) | ALLOW(STATE_OBSERVING))

#define ALLOW_LOBBY \
    (ALLOW(STATE_LOBBY))

#define ALLOW_IN_GAME \
    (ALLOW(STATE_IN_GAME))

#define ALLOW_OBSERVING \
    (ALLOW(STATE_OBSERVING))


typedef struct {
    const char *type;
    unsigned int allowed_states;
    Handler handler;
} MessageRule;


static const MessageRule message_table[] = {
    // Account
    { MSG_REGISTER,    ALLOW_BEFORE_LOGIN, handle_register },
    { MSG_LOGIN,       ALLOW_BEFORE_LOGIN, handle_login },

    // Lobby
    { MSG_LIST_ONLINE, ALLOW_LOGGED_IN, handle_list_online },

    // Challenge
    { MSG_CHALLENGE, ALLOW_LOBBY, handle_challenge },
    { MSG_ACCEPT,    ALLOW_LOBBY, handle_accept },
    { MSG_REFUSE,    ALLOW_LOBBY, handle_refuse },

    // Game
    // { MSG_MOVE,      ALLOW_IN_GAME, handle_move },

    // Chat
    // { MSG_CHAT,      ALLOW_LOGGED_IN, handle_chat },
};


#define MESSAGE_TABLE_SIZE \
    ((int)(sizeof(message_table) / sizeof(message_table[0])))


// Returns the rule for this type, or NULL if unknown.
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

    // No other threads exist yet, so no lock is needed here.
    account_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].in_use = 0;
        sessions[i].account_index = -1;
        sessions[i].state = STATE_CONNECTED;
        sessions[i].challenge_target = -1;
        sessions[i].match_index = -1;
        sessions[i].generation = 0;
    }

    for (int i = 0; i < MAX_GAMES; i++) {
        matches[i].in_use = 0;
    }

    next_game_id = 1;

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
        Session *session = &sessions[index];

        session->in_use = 1;
        session->account_index = -1;
        session->state = STATE_CONNECTED;
        session->challenge_target = -1;
        session->match_index = -1;

        session->generation++;

        *generation = session->generation;
    }

    LeaveCriticalSection(&logic_lock);

    return index;
}


void logic_handle_message(
    int session_index,
    const Message *request,
    Outbox *outbox
)
{
    if (request == NULL || outbox == NULL) {
        return;
    }

    EnterCriticalSection(&logic_lock);

    if (
        session_index < 0 ||
        session_index >= MAX_CLIENTS ||
        !sessions[session_index].in_use
    ) {
        printf(
            "Error: message from invalid session %d.\n",
            session_index
        );
        goto end;
    }

    const MessageRule *rule = find_rule(request->type);

    // Unknown message type.
    if (rule == NULL) {
        reply(
            outbox,
            session_index,
            MSG_ERROR,
            RESP_FAILURE,
            "unknown message"
        );
        goto end;
    }

    // Check whether the message is allowed in this state.
    SessionState state = sessions[session_index].state;

    if ((rule->allowed_states & ALLOW(state)) == 0) {
        const char *reason =
            (state == STATE_CONNECTED)
                ? "not logged in"
                : "not allowed now";

        reply(
            outbox,
            session_index,
            rule->type,
            RESP_FAILURE,
            reason
        );

        goto end;
    }

    // The message is allowed. Run its handler.
    rule->handler(session_index, request, outbox);

end:
    LeaveCriticalSection(&logic_lock);
}


void logic_disconnect(int session_index, Outbox *outbox)
{
    (void)outbox; // Used later for disconnect notifications.

    if (outbox == NULL) {
        return;
    }

    EnterCriticalSection(&logic_lock);

    if (
        session_index < 0 ||
        session_index >= MAX_CLIENTS ||
        !sessions[session_index].in_use
    ) {
        goto end;
    }

    Session *session = &sessions[session_index];

    // Mark the account offline, but keep the account itself.
    if (
        session->account_index >= 0 &&
        session->account_index < account_count
    ) {
        accounts[session->account_index].session_index = -1;
    }

    // TODO:
    // End the game, remove observers and cancel challenges.
    

    // Cancel challenges targeting this player
    int account = session->account_index;
    cancel_challenges_to(account, outbox);
    session->challenge_target = -1;


    // Free the session slot. Keep its generation for the next connection.
    session->in_use = 0;
    session->account_index = -1;
    session->state = STATE_CONNECTED;
    session->challenge_target = -1;
    session->match_index = -1;

end:
    LeaveCriticalSection(&logic_lock);
}