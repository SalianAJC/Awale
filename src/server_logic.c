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
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <windows.h>
#include <math.h>

#include "match.h"
#include "server_logic.h"

static int save_all_accounts(void);
// ================================================================
// 1. PRIVATE STATE
// ================================================================

static Account accounts[MAX_CLIENTS];
static int account_count = 0;
static int next_player_id = 1;

static Match matches[MAX_GAMES];
static Session sessions[MAX_CLIENTS];

static int next_game_id = 1;

static CRITICAL_SECTION logic_lock;

// ================================================================
// 2. HELPERS
// ================================================================

// ---------- Messages ----------

static int update_ratings(int a, int b, double score_a)
{
    if (a < 0 || a >= account_count ||
        b < 0 || b >= account_count ||
        a == b)
    {
        return -1;
    }

    int old_a = accounts[a].rating;
    int old_b = accounts[b].rating;

    double expected_a =
        1.0 / (1.0 + pow(10.0, ((double)old_b - old_a) / 400.0));

    double expected_b = 1.0 - expected_a;
    double score_b = 1.0 - score_a;

    int new_a = (int)lround(
        old_a + ELO_K * (score_a - expected_a)
    );

    int new_b = (int)lround(
        old_b + ELO_K * (score_b - expected_b)
    );

    accounts[a].rating = new_a;
    accounts[b].rating = new_b;

    if (save_all_accounts() != 0)
    {
        /* Restore the old ratings if saving failed. */
        accounts[a].rating = old_a;
        accounts[b].rating = old_b;

        printf("Error: could not save updated ELO ratings.\n");
        return -1;
    }

    return 0;
}

// Builds a message. Content may be NULL.
// Content is truncated if it exceeds the message buffer.
static Message make_message(
    const char *type,
    char response,
    char mode,
    const char *content)
{
    Message message = {0};

    snprintf(message.type, sizeof(message.type), "%s", type);

    message.response = response;
    message.mode = mode;
    message.receiver = 0;

    if (content != NULL)
    {
        snprintf(
            message.content,
            sizeof(message.content),
            "%s",
            content);
    }

    return message;
}

static int split_first_word(const char *content,
                            char *word, size_t word_size,
                            const char **rest)
{
    const char *space;
    size_t length;

    /* Find the first space */
    space = strchr(content, ' ');

    if (space == NULL)
        return -1;

    /* Calculate the first word's length */
    length = (size_t)(space - content);

    /* Reject an empty word or a word that does not fit */
    if (length == 0 || length >= word_size)
        return -1;

    /* Copy the first word */
    memcpy(word, content, length);
    word[length] = '\0';

    /* Point to everything after the first space */
    *rest = space + 1;

    return 0;
}

// Main Game
static int parse_int(const char *text, int min, int max, int *out)
{
    char *end;
    long value;

    if (text == NULL || text[0] == '\0' || out == NULL)
        return -1;

    errno = 0;
    value = strtol(text, &end, 10);

    if (*end != '\0' || errno == ERANGE || value < min || value > max)
        return -1;

    *out = (int)value;
    return 0;
}

static int side_of(int m, int account)
{
    if (matches[m].side_account[0] == account)
        return 0;

    return 1;
}

static int opponent_of(int m, int account)
{
    int side = side_of(m, account);

    return matches[m].side_account[1 - side];
}

static int find_match_by_game_id(int id)
{
    for (int i = 0; i < MAX_GAMES; i++)
    {
        if (matches[i].in_use && matches[i].game_id == id)
            return i;
    }

    return -1;
}

// ---------- Outbox ----------

// Adds a message for a session, stamped with its current generation.
static void outbox_add(
    Outbox *outbox,
    int recipient,
    const Message *message)
{
    if (recipient < 0 || recipient >= MAX_CLIENTS)
    {
        printf(
            "Error: outbox_add with invalid session %d. Message dropped.\n",
            recipient);
        return;
    }

    if (outbox->count >= MAX_OUTBOX_ENTRIES)
    {
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
    const char *content)
{
    if (session < 0 || session >= MAX_CLIENTS)
    {
        return;
    }

    Message message = make_message(
        type,
        response,
        mode,
        content);

    outbox_add(outbox, session, &message);
}

// Sends a push message.
static void push(
    Outbox *outbox,
    int session,
    const char *type,
    char mode,
    const char *content)
{
    send_to(
        outbox,
        session,
        type,
        mode,
        RESP_NONE,
        content);
}

// Sends a reply. Replies use MODE_NONE.
static void reply(
    Outbox *outbox,
    int session,
    const char *type,
    char response,
    const char *content)
{
    send_to(
        outbox,
        session,
        type,
        MODE_NONE,
        response,
        content);
}

static int find_free_match()
{
    int m = -1; // assume server is full
    for (int i = 0; i < MAX_GAMES; i++)
    {
        int status = matches[i].in_use;
        if (status == 0)
        { // found a free match
            return i;
        }
    }
    return m;
}

static void cancel_challenges_to(int account, Outbox *outbox)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (sessions[i].in_use == 0)
        {
            continue;
        }
        if (sessions[i].challenge_target == account)
        {
            sessions[i].challenge_target = -1;
            push(outbox, i, MSG_REFUSE, MODE_NONE, accounts[account].username);
        }
    }
}

// ---------- Accounts ----------

// Returns the account index, or -1 if the username doesn't exist.
static int find_account(const char *username)
{
    for (int i = 0; i < account_count; i++)
    {
        if (strcmp(accounts[i].username, username) == 0)
        {
            return i;
        }
    }

    return -1;
}

// Finds an account by player id within the txt

static int find_account_by_id(int player_id)
{
    for (int i = 0; i < account_count; i++)
    {
        if (accounts[i].player_id == player_id)
            return i;
    }

    return -1;
}

static int find_friend_slot(const Account *account, int friend_id)
{
    for (int i = 0; i < MAX_FRIENDS; i++)
    {
        if (account->friends_ids[i] == friend_id)
            return i;
    }

    return -1;
}

static int find_free_friend_slot(const Account *account)
{
    for (int i = 0; i < MAX_FRIENDS; i++)
    {
        if (account->friends_ids[i] == 0)
            return i;
    }

    return -1;
}

// Loads the friends list

static int load_friends(void)
{
    FILE *file = fopen(FRIENDS_FILE, "r");

    if (file == NULL)
    {
        if (errno == ENOENT)
            return 0;

        return -1;
    }

    char line[1024];

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *username = strtok(line, "|\r\n");

        if (username == NULL)
            continue;

        int account_index = find_account(username);

        if (account_index == -1)
            continue;

        char *friend_name;

        while ((friend_name = strtok(NULL, "|\r\n")) != NULL)
        {
            int friend_index = find_account(friend_name);

            if (friend_index == -1 || friend_index == account_index)
                continue;

            Account *account = &accounts[account_index];
            int friend_id = accounts[friend_index].player_id;
            int exists = 0;

            for (int i = 0; i < MAX_FRIENDS; i++)
            {
                if (account->friends_ids[i] == friend_id)
                {
                    exists = 1;
                    break;
                }
            }

            if (exists)
                continue;

            for (int i = 0; i < MAX_FRIENDS; i++)
            {
                if (account->friends_ids[i] == 0)
                {
                    account->friends_ids[i] = friend_id;
                    break;
                }
            }
        }
    }

    fclose(file);

    return 0;
}

// Save friends

static int save_friends(void)
{
    FILE *file = fopen(FRIENDS_FILE, "w");

    if (file == NULL)
        return -1;

    for (int i = 0; i < account_count; i++)
    {
        fprintf(file, "%s", accounts[i].username);

        for (int j = 0; j < MAX_FRIENDS; j++)
        {
            int friend_id = accounts[i].friends_ids[j];

            if (friend_id == 0)
                continue;

            int friend_index = find_account_by_id(friend_id);

            if (friend_index != -1)
            {
                fprintf(file, "|%s", accounts[friend_index].username);
            }
        }

        fprintf(file, "\n");
    }

    if (fclose(file) != 0)
        return -1;

    return 0;
}

// Loads accounts from the local txt file to avoid constant registration

static int has_only_legal_characters(const char *username);

static int load_accounts(void)
{
    FILE *file = fopen(USERS_FILE, "r");

    if (file == NULL)
    {
        if (errno == ENOENT)
            return 0;

        return -1;
    }

    char line[128];
    int max_id = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        int id;
        int rating;
        char username[MAX_NAME + 1];
        char extra;

        if (sscanf(line, "%d|%32[^|]|%d %c",
                   &id, username, &rating, &extra) != 3)
            continue;

        if (id <= 0 || rating < 0)
            continue;

        if (strlen(username) == 0 ||
            !has_only_legal_characters(username))
            continue;

        if (account_count >= MAX_CLIENTS)
            break;

        if (find_account(username) != -1)
            continue;

        Account *account = &accounts[account_count];

        memset(account, 0, sizeof(*account));

        account->player_id = id;
        account->session_index = -1;
        account->rating = rating;

        memcpy(account->username, username, strlen(username) + 1);

        if (id > max_id)
            max_id = id;

        account_count++;
    }

    fclose(file);

    next_player_id = max_id + 1;

    return 0;
}

// Saves an account to the txt databank

static int save_account(const Account *account)
{
    FILE *file = fopen(USERS_FILE, "a");

    if (file == NULL)
        return -1;

    int result = fprintf(
        file,
        "%d|%s|%d\n",
        account->player_id,
        account->username,
        account->rating);

    if (result < 0)
    {
        fclose(file);
        return -1;
    }

    if (fclose(file) != 0)
        return -1;

    return 0;
}

static int save_all_accounts(void)
{
    char temp_path[sizeof(USERS_FILE) + 5];

    snprintf(temp_path, sizeof(temp_path), "%s.tmp", USERS_FILE);

    FILE *file = fopen(temp_path, "w");

    if (file == NULL)
        return -1;

    for (int i = 0; i < account_count; i++)
    {
        if (fprintf(file, "%d|%s|%d\n",
                    accounts[i].player_id,
                    accounts[i].username,
                    accounts[i].rating) < 0)
        {
            fclose(file);
            remove(temp_path);
            return -1;
        }
    }

    if (fclose(file) != 0)
    {
        remove(temp_path);
        return -1;
    }

    if (!MoveFileExA(temp_path, USERS_FILE,
                     MOVEFILE_REPLACE_EXISTING |
                     MOVEFILE_WRITE_THROUGH))
    {
        remove(temp_path);
        return -1;
    }

    return 0;
}

// Returns 1 if the username contains only letters, digits and '_'.
static int has_only_legal_characters(const char *username)
{
    for (int i = 0; username[i] != '\0'; i++)
    {
        char c = username[i];

        if (!(
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_'))
        {
            return 0;
        }
    }

    return 1;
}

// Defined with the online-list handler, also used by login.
static void build_online_list(char *buffer, size_t size);

static void notify_match(Outbox *outbox, int m,
                         const char *type, const char *content,
                         int skip_session)
{
    for (int i = 0; i < 2; i++)
    {
        int account = matches[m].side_account[i];
        int session = accounts[account].session_index;

        if (session == -1 || session == skip_session)
            continue;

        push(outbox, session, type, MODE_PLAYER, content);
    }

    for (int i = 0; i < matches[m].observer_count; i++)
    {
        int session = matches[m].observers[i];

        if (session == skip_session)
            continue;

        push(outbox, session, type, MODE_OBSERVER, content);
    }
}
static void end_match(Outbox *outbox, int m,
                      int winner_account, const char *reason,
                      int skip_session)
{
    char content[MAX_CONTENT];
    const char *winner = "draw";

    int a = matches[m].side_account[0];
    int b = matches[m].side_account[1];

    double score_a;

    if (winner_account == -1)
        score_a = 0.5;       /* Draw */
    else if (winner_account == a)
        score_a = 1.0;       /* Player A wins */
    else
        score_a = 0.0;       /* Player B wins */

    update_ratings(a, b, score_a);

    snprintf(content, sizeof(content), "%d %d %d %s %s",
             matches[m].game_id,
             matches[m].game.captures[0],
             matches[m].game.captures[1],
             winner,
             reason);

    notify_match(outbox, m, MSG_GAME_OVER, content, skip_session);

    for (int i = 0; i < 2; i++)
    {
        int account = matches[m].side_account[i];
        int session = accounts[account].session_index;

        if (session != -1)
        {
            sessions[session].state = STATE_LOBBY;
            sessions[session].match_index = -1;
        }
    }

    for (int i = 0; i < matches[m].observer_count; i++)
    {
        int session = matches[m].observers[i];

        if (session != skip_session)
        {
            sessions[session].state = STATE_LOBBY;
            sessions[session].match_index = -1;
        }
    }

    matches[m].in_use = 0;
    matches[m].observer_count = 0;
}
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
    Outbox *outbox);

// ---------- R : register ----------

static void handle_register(
    int session,
    const Message *request,
    Outbox *outbox)
{
    const char *username = request->content;
    size_t length = strlen(username);

    if (length == 0)
    {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Username can't be empty");
        return;
    }

    if (length > MAX_NAME)
    {
        char reason[64];

        snprintf(
            reason,
            sizeof(reason),
            "Username can't exceed %d characters",
            MAX_NAME);

        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            reason);
        return;
    }

    if (!has_only_legal_characters(username))
    {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Username contains illegal characters");
        return;
    }

    if (find_account(username) != -1)
    {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Username already exists");
        return;
    }

    if (account_count >= MAX_CLIENTS)
    {
        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "Server is full");
        return;
    }

    // Create the account. Registration does not log in.
    Account *account = &accounts[account_count];

    memset(account, 0, sizeof(*account));

    account->player_id = next_player_id;
    account->session_index = -1;
    account->rating = START_RATING;

    memcpy(account->username, username, length + 1);

    if (save_account(account) != 0)
    {
        memset(account, 0, sizeof(*account));

        reply(
            outbox,
            session,
            MSG_REGISTER,
            RESP_FAILURE,
            "could not save account");
        return;
    }

    account_count++;
    next_player_id++;

    reply(
        outbox,
        session,
        MSG_REGISTER,
        RESP_SUCCESS,
        "registered");
}

// ---------- L : login ----------

static void handle_login(
    int session,
    const Message *request,
    Outbox *outbox)
{
    const char *username = request->content;
    size_t length = strlen(username);

    if (length == 0)
    {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "Username can't be empty");
        return;
    }

    if (length > MAX_NAME)
    {
        char reason[64];

        snprintf(
            reason,
            sizeof(reason),
            "Username can't exceed %d characters",
            MAX_NAME);

        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            reason);
        return;
    }

    if (!has_only_legal_characters(username))
    {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "Username contains illegal characters");
        return;
    }

    int account_index = find_account(username);

    if (account_index == -1)
    {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "unknown user");
        return;
    }

    if (accounts[account_index].session_index != -1)
    {
        reply(
            outbox,
            session,
            MSG_LOGIN,
            RESP_FAILURE,
            "already connected");
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
        username);

    // Push the current online list to the newly logged-in player.
    char list[MAX_CONTENT + 1];

    build_online_list(list, sizeof(list));

    push(
        outbox,
        session,
        MSG_PLAYERS_ONLINE,
        MODE_NONE,
        list);
}

// ---------- LO : list online players ----------

// State letters:
// L = lobby
// G = in game
// O = observing
static char state_letter(SessionState state)
{
    switch (state)
    {
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
    if (buffer == NULL || size == 0)
    {
        return;
    }

    buffer[0] = '\0';

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        const Session *session = &sessions[i];

        if (!session->in_use || session->account_index == -1)
        {
            continue;
        }

        if (
            session->account_index < 0 ||
            session->account_index >= account_count)
        {
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
            state_letter(session->state));

        size_t current_length = strlen(buffer);
        size_t entry_length = strlen(entry);

        // Leave room for the terminating '\0'.
        if (entry_length >= size - current_length)
        {
            break;
        }

        strcat(buffer, entry);
    }
}

static void handle_list_online(
    int session,
    const Message *request,
    Outbox *outbox)
{
    (void)request;

    char list[MAX_CONTENT + 1];

    build_online_list(list, sizeof(list));

    reply(
        outbox,
        session,
        MSG_LIST_ONLINE,
        RESP_SUCCESS,
        list);
}

// C : Challenge
static void handle_challenge(int session, const Message *request, Outbox *outbox)
{

    int me = sessions[session].account_index;
    int b = find_account(request->content);

    char content[MAX_CONTENT];

    if (sessions[session].challenge_target != -1)
    { // sender already has an outgoing challenge
        // construct content
        snprintf(content, sizeof(content), "%s", "challenge already pending");

        // reply to offline player
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // quit the function
        return;
    }

    if (b == -1)
    { // challenge sent to offline player
        // construct content
        snprintf(content, sizeof(content), "%s", "unknown user");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return;
    }

    if (b == me)
    { // user tried to challenge themselves
        // construct content
        snprintf(content, sizeof(content), "%s", "cannot challenge yourself");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return;
    }

    int bs = accounts[b].session_index;
    if (bs == -1)
    { // user tried to challenge an offline user
        // construct content
        snprintf(content, sizeof(content), "%s", "user offline");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return;
    }

    if (sessions[bs].state != STATE_LOBBY)
    { // user tried to challenge a busy user
        // construct content
        snprintf(content, sizeof(content), "%s", "user busy");
        reply(outbox, session, MSG_CHALLENGE, RESP_FAILURE, content);

        // reply
        return;
    }

    // In case of success

    // Set the session challenge to b
    sessions[session].challenge_target = b;
    reply(outbox, session, MSG_CHALLENGE, RESP_SUCCESS, accounts[b].username);
    push(outbox, bs, MSG_CHALLENGED, MODE_NONE, accounts[me].username);
}

static void handle_refuse(int session, const Message *request, Outbox *outbox)
{

    // Here, sender is b, receiver is a

    int b = sessions[session].account_index;
    int a = find_account(request->content);

    if (a == -1)
    {
        reply(outbox, session, MSG_REFUSE, RESP_FAILURE, "no challenge from this user");
        return;
    }

    int as = accounts[a].session_index;

    if (as == -1 || sessions[as].challenge_target != b)
    {
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

static void handle_accept(int session, const Message *request, Outbox *outbox)
{

    // Same here. Sender is b, Receiver is a
    int b = sessions[session].account_index;
    int a = find_account(request->content);

    if (a == -1)
    {
        reply(outbox, session, MSG_ACCEPT, RESP_FAILURE, "no challenge from this user");
        return;
    }

    int as = accounts[a].session_index;

    if (as == -1 || sessions[as].challenge_target != b)
    {
        reply(outbox, session, MSG_ACCEPT, RESP_FAILURE, "no challenge from this user");
        return;
    }

    if (sessions[as].state != STATE_LOBBY)
    {
        reply(outbox, session, MSG_ACCEPT, RESP_FAILURE, "user busy");
        return;
    }

    int m = find_free_match();
    if (m == -1)
    {
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

    if (rand() % 2 == 0)
    {
        side_a = 0;
        side_b = 1;
    }
    else
    {
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

static void handle_move(int session_index, const Message *message,
                        Outbox *outbox)
{
    Session *session = &sessions[session_index];
    int me = session->account_index;
    int m = session->match_index;
    int side;
    int hole;
    Game *g;
    char content[MAX_CONTENT];

    if (m < 0 || m >= MAX_GAMES || !matches[m].in_use)
    {
        send_to(outbox, session_index, MSG_MOVE,
                MODE_PLAYER, RESP_FAILURE, "illegal move");
        return;
    }

    g = &matches[m].game;
    side = side_of(m, me);

    // 1. Check the format
    if (parse_int(message->content, 0, 11, &hole) != 0)
    {
        send_to(outbox, session_index, MSG_MOVE,
                MODE_PLAYER, RESP_FAILURE, "bad format");
        return;
    }

    // 2. Check whose turn it is
    if (g->current_player != side)
    {
        send_to(outbox, session_index, MSG_MOVE,
                MODE_PLAYER, RESP_FAILURE, "not your turn");
        return;
    }

    // 3. Check whether the move is legal
    if (matches[m].move_count >= MAX_MOVES || sow(g, hole) != 0)
    {
        send_to(outbox, session_index, MSG_MOVE,
                MODE_PLAYER, RESP_FAILURE, "illegal move");
        return;
    }

    // Record the move
    matches[m].moves[matches[m].move_count++] = hole;

    // Reply to the player who moved
    snprintf(content, sizeof(content), "%d", hole);
    send_to(outbox, session_index, MSG_MOVE,
            MODE_PLAYER, RESP_SUCCESS, content);

    // Notify both players and observers
    snprintf(content, sizeof(content), "%d %d",
             matches[m].game_id, hole);

    notify_match(outbox, m, MSG_BOARD_UPDATE, content, -1);

    // End the game if necessary
    if (g->finished)
    {
        int winner = -1;

        if (g->captures[0] > g->captures[1])
            winner = matches[m].side_account[0];
        else if (g->captures[1] > g->captures[0])
            winner = matches[m].side_account[1];

        end_match(outbox, m, winner, "end", -1);
    }
}

static void handle_resign(int session_index, const Message *message,
                          Outbox *outbox)
{
    Session *session = &sessions[session_index];
    int me = session->account_index;
    int m = session->match_index;
    int winner;

    (void)message;

    if (m < 0 || m >= MAX_GAMES || !matches[m].in_use)
    {
        reply(outbox, session_index, MSG_RESIGN,
              RESP_FAILURE, "not in a game");
        return;
    }

    winner = opponent_of(m, me);

    send_to(outbox, session_index, MSG_RESIGN,
            MODE_PLAYER, RESP_SUCCESS, "");

    end_match(outbox, m, winner, "resign", -1);
}

static void handle_chat(int session_index, const Message *message,
                        Outbox *outbox)
{
    Session *session = &sessions[session_index];
    int me = session->account_index;

    char to[MAX_NAME + 1];
    const char *text;
    int recipient;
    char out[MAX_NAME + 1 + MAX_CHAT + 1];

    /* Extract recipient and message text */
    if (split_first_word(message->content, to, sizeof(to), &text) != 0)
    {
        reply(outbox, session_index, MSG_CHAT,
              RESP_FAILURE, "bad format");
        return;
    }

    /* Check that the message is not empty */
    if (text[0] == '\0')
    {
        reply(outbox, session_index, MSG_CHAT,
              RESP_FAILURE, "empty message");
        return;
    }

    /* Check maximum message length */
    if (strlen(text) > MAX_CHAT)
    {
        reply(outbox, session_index, MSG_CHAT,
              RESP_FAILURE, "message too long");
        return;
    }

    /* Find recipient account */
    recipient = find_account(to);

    if (recipient == -1)
    {
        reply(outbox, session_index, MSG_CHAT,
              RESP_FAILURE, "unknown user");
        return;
    }

    /* Check that recipient is online */
    if (accounts[recipient].session_index == -1)
    {
        reply(outbox, session_index, MSG_CHAT,
              RESP_FAILURE, "user offline");
        return;
    }

    /* Build message using the sender's actual username */
    snprintf(out, sizeof(out), "%s %s",
             accounts[me].username, text);

    /* Confirm to sender */
    reply(outbox, session_index, MSG_CHAT,
          RESP_SUCCESS, to);

    /* Deliver message to recipient */
    push(outbox, accounts[recipient].session_index,
         MSG_CHAT, MODE_NONE, out);
}

static void handle_friend_add(int session, const Message *request,
                              Outbox *outbox)
{
    int me = sessions[session].account_index;
    int target = find_account(request->content);

    if (target == -1)
    {
        reply(outbox, session, MSG_FRIEND_ADD,
              RESP_FAILURE, "unknown user");
        return;
    }

    if (target == me)
    {
        reply(outbox, session, MSG_FRIEND_ADD,
              RESP_FAILURE, "cannot add yourself");
        return;
    }

    int my_id = accounts[me].player_id;
    int target_id = accounts[target].player_id;

    int my_slot = find_friend_slot(&accounts[me], target_id);
    int target_slot = find_friend_slot(&accounts[target], my_id);

    if (my_slot != -1 && target_slot != -1)
    {
        reply(outbox, session, MSG_FRIEND_ADD,
              RESP_FAILURE, "already friends");
        return;
    }

    int new_my_slot = my_slot == -1;
    int new_target_slot = target_slot == -1;

    if (new_my_slot)
        my_slot = find_free_friend_slot(&accounts[me]);

    if (new_target_slot)
        target_slot = find_free_friend_slot(&accounts[target]);

    if (my_slot == -1 || target_slot == -1)
    {
        reply(outbox, session, MSG_FRIEND_ADD,
              RESP_FAILURE, "friend list full");
        return;
    }

    accounts[me].friends_ids[my_slot] = target_id;
    accounts[target].friends_ids[target_slot] = my_id;

    if (save_friends() != 0)
    {
        if (new_my_slot)
            accounts[me].friends_ids[my_slot] = 0;

        if (new_target_slot)
            accounts[target].friends_ids[target_slot] = 0;

        reply(outbox, session, MSG_FRIEND_ADD,
              RESP_FAILURE, "could not save friends");
        return;
    }

    reply(outbox, session, MSG_FRIEND_ADD,
          RESP_SUCCESS, accounts[target].username);
}

static void handle_friend_remove(int session, const Message *request,
                                 Outbox *outbox)
{
    int me = sessions[session].account_index;
    int target = find_account(request->content);

    if (target == -1)
    {
        reply(outbox, session, MSG_FRIEND_REMOVE,
              RESP_FAILURE, "unknown user");
        return;
    }

    if (target == me)
    {
        reply(outbox, session, MSG_FRIEND_REMOVE,
              RESP_FAILURE, "cannot remove yourself");
        return;
    }

    int my_id = accounts[me].player_id;
    int target_id = accounts[target].player_id;

    int my_slot = find_friend_slot(&accounts[me], target_id);
    int target_slot = find_friend_slot(&accounts[target], my_id);

    if (my_slot == -1 && target_slot == -1)
    {
        reply(outbox, session, MSG_FRIEND_REMOVE,
              RESP_FAILURE, "not friends");
        return;
    }

    if (my_slot != -1)
        accounts[me].friends_ids[my_slot] = 0;

    if (target_slot != -1)
        accounts[target].friends_ids[target_slot] = 0;

    if (save_friends() != 0)
    {
        if (my_slot != -1)
            accounts[me].friends_ids[my_slot] = target_id;

        if (target_slot != -1)
            accounts[target].friends_ids[target_slot] = my_id;

        reply(outbox, session, MSG_FRIEND_REMOVE,
              RESP_FAILURE, "could not save friends");
        return;
    }

    reply(outbox, session, MSG_FRIEND_REMOVE,
          RESP_SUCCESS, accounts[target].username);
}

static void handle_friend_list(int session, const Message *request,
                               Outbox *outbox)
{
    (void)request;

    int me = sessions[session].account_index;
    char list[MAX_CONTENT + 1] = "";

    for (int i = 0; i < MAX_FRIENDS; i++)
    {
        int friend_id = accounts[me].friends_ids[i];

        if (friend_id == 0)
            continue;

        int friend_index = find_account_by_id(friend_id);

        if (friend_index == -1)
            continue;

        size_t current_length = strlen(list);

        snprintf(
            list + current_length,
            sizeof(list) - current_length,
            "%s%s",
            current_length > 0 ? "," : "",
            accounts[friend_index].username);
    }

    reply(outbox, session, MSG_FRIEND_LIST,
          RESP_SUCCESS, list);
}

static void handle_write_bio(int session, const Message *request,
                             Outbox *outbox)
{
    int me = sessions[session].account_index;
    const char *bio = request->content;
    size_t length = strlen(bio);
    int lines = 0;
    size_t line_length = 0;

    // Validate bio format
    if (length >= sizeof(accounts[me].bio))
    {
        reply(outbox, session, MSG_WRITE_BIO,
              RESP_FAILURE, "bio too long");
        return;
    }

    for (size_t i = 0; i <= length; i++)
    {
        if (bio[i] == ';' || bio[i] == '\0')
        {
            if (line_length == 0 && length > 0)
            {
                reply(outbox, session, MSG_WRITE_BIO,
                      RESP_FAILURE, "empty bio line");
                return;
            }

            lines++;
            line_length = 0;

            if (lines > MAX_BIO_LINES)
            {
                reply(outbox, session, MSG_WRITE_BIO,
                      RESP_FAILURE, "too many bio lines");
                return;
            }
        }
        else
        {
            line_length++;

            if (line_length > MAX_BIO_LINE)
            {
                reply(outbox, session, MSG_WRITE_BIO,
                      RESP_FAILURE, "bio line too long");
                return;
            }
        }
    }

    // Save bio
    memcpy(accounts[me].bio, bio, length + 1);

    reply(outbox, session, MSG_WRITE_BIO,
          RESP_SUCCESS, "bio updated");
}

static void handle_read_bio(int session, const Message *request,
                            Outbox *outbox)
{
    int account = find_account(request->content);
    char bio[MAX_CONTENT + 1];

    // Check requested account
    if (account == -1)
    {
        reply(outbox, session, MSG_READ_BIO,
              RESP_FAILURE, "unknown user");
        return;
    }

    if (accounts[account].bio[0] == '\0')
    {
        reply(outbox, session, MSG_READ_BIO,
              RESP_SUCCESS, "no bio");
        return;
    }

    // Restore line breaks
    size_t j = 0;

    for (size_t i = 0; accounts[account].bio[i] != '\0'; i++)
    {
        if (j >= MAX_CONTENT)
            break;

        bio[j++] = accounts[account].bio[i] == ';'
                       ? '\n'
                       : accounts[account].bio[i];
    }

    /*
     * Keep the line separator on the wire. Newlines terminate protocol
     * messages, so the client converts ';' to a display newline.
     */
    snprintf(bio, sizeof(bio), "%s", accounts[account].bio);

    reply(outbox, session, MSG_READ_BIO,
          RESP_SUCCESS, bio);
}

static void handle_ranking(int session,
                           const Message *request,
                           Outbox *outbox)
{
    (void)request;

    int order[MAX_CLIENTS];

    /* Start with the account indices. */
    for (int i = 0; i < account_count; i++)
        order[i] = i;

    /* Sort from highest rating to lowest rating. */
    for (int i = 0; i < account_count - 1; i++)
    {
        for (int j = i + 1; j < account_count; j++)
        {
            if (accounts[order[j]].rating >
                accounts[order[i]].rating)
            {
                int temp = order[i];
                order[i] = order[j];
                order[j] = temp;
            }
        }
    }

    char ranking[MAX_CONTENT + 1] = "";
    size_t used = 0;

    for (int i = 0; i < account_count; i++)
    {
        int index = order[i];

        int written = snprintf(
            ranking + used,
            sizeof(ranking) - used,
            "%s%s:%d",
            used > 0 ? "," : "",
            accounts[index].username,
            accounts[index].rating
        );

        if (written < 0 ||
            (size_t)written >= sizeof(ranking) - used)
        {
            break;
        }

        used += (size_t)written;
    }

    reply(
        outbox,
        session,
        MSG_RANKING,
        RESP_SUCCESS,
        ranking
    );
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

typedef struct
{
    const char *type;
    unsigned int allowed_states;
    Handler handler;
} MessageRule;

static const MessageRule message_table[] = {
    // Account
    {MSG_REGISTER, ALLOW_BEFORE_LOGIN, handle_register},
    {MSG_LOGIN, ALLOW_BEFORE_LOGIN, handle_login},

    // Lobby
    {MSG_LIST_ONLINE, ALLOW_LOGGED_IN, handle_list_online},

    // Challenge
    {MSG_CHALLENGE, ALLOW_LOBBY, handle_challenge},
    {MSG_ACCEPT, ALLOW_LOBBY, handle_accept},
    {MSG_REFUSE, ALLOW_LOBBY, handle_refuse},

    // Game
    {MSG_MOVE, ALLOW_IN_GAME, handle_move},
    {MSG_RESIGN, ALLOW_IN_GAME, handle_resign},

    // Chat
    {MSG_CHAT, ALLOW_LOGGED_IN, handle_chat},

    // Friends
    {MSG_FRIEND_ADD, ALLOW_LOGGED_IN, handle_friend_add},
    {MSG_FRIEND_REMOVE, ALLOW_LOGGED_IN, handle_friend_remove},
    {MSG_FRIEND_LIST, ALLOW_LOGGED_IN, handle_friend_list},
    // Bio
    {MSG_WRITE_BIO, ALLOW_LOGGED_IN, handle_write_bio},
    {MSG_READ_BIO, ALLOW_LOGGED_IN, handle_read_bio},

    //Elo
    {MSG_RANKING, ALLOW_LOGGED_IN, handle_ranking },
};

#define MESSAGE_TABLE_SIZE \
    ((int)(sizeof(message_table) / sizeof(message_table[0])))

// Returns the rule for this type, or NULL if unknown.
static const MessageRule *find_rule(const char *type)
{
    for (int i = 0; i < MESSAGE_TABLE_SIZE; i++)
    {
        if (strcmp(message_table[i].type, type) == 0)
        {
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
    next_player_id = 1;

    if (load_accounts() != 0)
    {
        DeleteCriticalSection(&logic_lock);
        return -1;
    }

    if (load_friends() != 0)
    {
        DeleteCriticalSection(&logic_lock);
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        sessions[i].in_use = 0;
        sessions[i].account_index = -1;
        sessions[i].state = STATE_CONNECTED;
        sessions[i].challenge_target = -1;
        sessions[i].match_index = -1;
        sessions[i].generation = 0;
    }

    for (int i = 0; i < MAX_GAMES; i++)
    {
        matches[i].in_use = 0;
    }

    next_game_id = 1;

    return 0;
}

int logic_connect(unsigned int *generation)
{
    if (generation == NULL)
    {
        return -1;
    }

    int index = -1;

    EnterCriticalSection(&logic_lock);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!sessions[i].in_use)
        {
            index = i;
            break;
        }
    }

    if (index != -1)
    {
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
    Outbox *outbox)
{
    if (request == NULL || outbox == NULL)
    {
        return;
    }

    EnterCriticalSection(&logic_lock);

    if (
        session_index < 0 ||
        session_index >= MAX_CLIENTS ||
        !sessions[session_index].in_use)
    {
        printf(
            "Error: message from invalid session %d.\n",
            session_index);
        goto end;
    }

    const MessageRule *rule = find_rule(request->type);

    // Unknown message type.
    if (rule == NULL)
    {
        reply(
            outbox,
            session_index,
            MSG_ERROR,
            RESP_FAILURE,
            "unknown message");
        goto end;
    }

    // Check whether the message is allowed in this state.
    SessionState state = sessions[session_index].state;

    if ((rule->allowed_states & ALLOW(state)) == 0)
    {
        const char *reason =
            (state == STATE_CONNECTED)
                ? "not logged in"
                : "not allowed now";

        reply(
            outbox,
            session_index,
            rule->type,
            RESP_FAILURE,
            reason);

        goto end;
    }

    // The message is allowed. Run its handler.
    rule->handler(session_index, request, outbox);

end:
    LeaveCriticalSection(&logic_lock);
}

void logic_disconnect(int session_index, Outbox *outbox)
{
    if (outbox == NULL)
    {
        return;
    }

    EnterCriticalSection(&logic_lock);

    if (
        session_index < 0 ||
        session_index >= MAX_CLIENTS ||
        !sessions[session_index].in_use)
    {
        goto end;
    }

    Session *session = &sessions[session_index];

    // If the player disconnects during a game,
    // the opponent wins.
    if (session->state == STATE_IN_GAME)
    {
        int m = session->match_index;
        int me = session->account_index;

        if (
            m >= 0 &&
            m < MAX_GAMES &&
            matches[m].in_use &&
            me >= 0 &&
            me < account_count)
        {
            int winner = opponent_of(m, me);

            end_match(outbox, m, winner, "left", session_index);
        }
    }
    // If the player is observing, remove them from the match.
    else if (session->state == STATE_OBSERVING)
    {
        int m = session->match_index;

        if (m >= 0 && m < MAX_GAMES && matches[m].in_use)
        {
            for (int i = 0; i < matches[m].observer_count; i++)
            {
                if (matches[m].observers[i] == session_index)
                {
                    matches[m].observers[i] =
                        matches[m].observers[matches[m].observer_count - 1];

                    matches[m].observer_count--;
                    break;
                }
            }
        }
    }

    // Mark the account offline, but keep the account itself.
    int account = session->account_index;

    if (account >= 0 && account < account_count)
    {
        accounts[account].session_index = -1;

        // Cancel challenges targeting this player.
        cancel_challenges_to(account, outbox);
    }

    // Clear any outgoing challenge.
    session->challenge_target = -1;

    // Free the session slot.
    // Keep its generation for the next connection.
    session->in_use = 0;
    session->account_index = -1;
    session->state = STATE_CONNECTED;
    session->challenge_target = -1;
    session->match_index = -1;

end:
    LeaveCriticalSection(&logic_lock);
}
