// Logic tests for Pv (private mode) and how it limits O, with friends.

#include <stdio.h>
#include <string.h>

#include "server_logic.h"

static int passed = 0;
static int failed = 0;

typedef struct {
    const char *name;
    int session;
    unsigned int generation;
} Player;

// ----------------------------------------------------------------
// Small tools
// ----------------------------------------------------------------

static void report(int ok, const char *label, const char *details)
{
    printf("%s  %-52s %s\n", ok ? "PASS" : "FAIL", label, details);

    if (ok)
        passed++;
    else
        failed++;
}

static void send_request(const Player *p, const char *type,
                         const char *content, Outbox *outbox)
{
    Message request = {0};

    strcpy(request.type, type);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    snprintf(request.content, sizeof(request.content), "%s", content);

    memset(outbox, 0, sizeof(*outbox));
    logic_handle_message(p->session, &request, outbox);
}

static int login_player(Player *p)
{
    Outbox outbox;

    p->session = logic_connect(&p->generation);

    if (p->session < 0)
        return -1;

    send_request(p, MSG_REGISTER, p->name, &outbox);
    send_request(p, MSG_LOGIN, p->name, &outbox);

    return outbox.count >= 1 &&
           outbox.entries[0].message.response == RESP_SUCCESS ? 0 : -1;
}

// Logs an existing account in again from a new connection
static int relogin(Player *p)
{
    Outbox outbox;

    p->session = logic_connect(&p->generation);
    send_request(p, MSG_LOGIN, p->name, &outbox);

    return outbox.count >= 1 &&
           outbox.entries[0].message.response == RESP_SUCCESS ? 0 : -1;
}

static void leave(Player *p)
{
    Outbox outbox;

    memset(&outbox, 0, sizeof(outbox));
    logic_disconnect(p->session, &outbox);
}

static const OutboxEntry *find_entry(const Outbox *o, const Player *p,
                                     const char *type)
{
    for (int i = 0; i < o->count; i++) {
        if (o->entries[i].session_index == p->session &&
            strcmp(o->entries[i].message.type, type) == 0)
            return &o->entries[i];
    }

    return NULL;
}

static int entry_is(const OutboxEntry *e, const Player *to,
                    char response, char mode, const char *content)
{
    return e != NULL &&
           e->session_index == to->session &&
           e->generation == to->generation &&
           e->message.response == response &&
           e->message.mode == mode &&
           (content == NULL || strcmp(e->message.content, content) == 0);
}

static void expect_only_reply(const Player *p, const char *type, const char *content,
                              char response, char mode, const char *expected,
                              const char *label)
{
    Outbox o;
    char details[MAX_CONTENT + 64];

    send_request(p, type, content, &o);
    snprintf(details, sizeof(details), "%d entries, first: %c|%c|%s", o.count,
             o.count ? o.entries[0].message.response : '?',
             o.count ? o.entries[0].message.mode : '?',
             o.count ? o.entries[0].message.content : "");

    report(o.count == 1 &&
           strcmp(o.entries[0].message.type, type) == 0 &&
           entry_is(&o.entries[0], p, response, mode, expected),
           label, details);
}

// Challenge + accept. Returns the game id, -1 on failure.
static int start_game(Player *a, Player *b)
{
    Outbox o;
    int id, side;

    send_request(a, MSG_CHALLENGE, b->name, &o);
    send_request(b, MSG_ACCEPT, a->name, &o);

    const OutboxEntry *gs = find_entry(&o, a, MSG_GAME_START);

    if (gs == NULL || sscanf(gs->message.content, "%d %d", &id, &side) != 2)
        return -1;

    return id;
}

// Tries O; returns 1 if accepted (and stops watching again), 0 if refused
static int can_watch(Player *viewer, int game_id, char *reason, size_t size)
{
    Outbox o;
    char id[16];

    snprintf(id, sizeof(id), "%d", game_id);
    send_request(viewer, MSG_OBSERVE, id, &o);

    const OutboxEntry *r = find_entry(&o, viewer, MSG_OBSERVE);

    snprintf(reason, size, "%s", r ? r->message.content : "no reply");

    if (r != NULL && r->message.response == RESP_SUCCESS) {
        send_request(viewer, MSG_STOP_OBSERVE, "", &o);
        return 1;
    }

    return 0;
}

// Returns the pub/priv marker of a game in LG, or "" if not listed
static const char *visibility(Player *asker, int game_id)
{
    static char marker[8];
    Outbox o;
    char list[MAX_CONTENT + 1];

    marker[0] = '\0';
    send_request(asker, MSG_LIST_GAMES, "", &o);

    if (o.count != 1)
        return marker;

    snprintf(list, sizeof(list), "%s", o.entries[0].message.content);

    for (char *entry = strtok(list, ","); entry != NULL; entry = strtok(NULL, ",")) {
        int id;
        char p0[MAX_NAME + 1], p1[MAX_NAME + 1], vis[8];

        if (sscanf(entry, "%d:%32[^:]:%32[^:]:%7s", &id, p0, p1, vis) == 4 && id == game_id)
            snprintf(marker, sizeof(marker), "%s", vis);
    }

    return marker;
}

// ----------------------------------------------------------------
// Tests
// ----------------------------------------------------------------

int main(void)
{
    Player alice = {"alice", 0, 0}, bob = {"bob", 0, 0}, carol = {"carol", 0, 0}, dave = {"dave", 0, 0};
    Player erin = {"erin", 0, 0}, frank = {"frank", 0, 0}, ghost = {"ghost", 0, 0};
    Outbox o;
    char reason[MAX_CONTENT + 1];

    printf("-- Setup\n");

    remove(USERS_FILE);
    remove(FRIENDS_FILE);
    report(logic_init() == 0, "logic_init", "");

    int ok = login_player(&alice) == 0 && login_player(&bob) == 0 &&
             login_player(&carol) == 0 && login_player(&dave) == 0 &&
             login_player(&erin) == 0 && login_player(&frank) == 0;
    report(ok, "six players logged in", "");

    if (!ok)
        return 1;

    //------------------------------------------------------------
    printf("\n-- Pv: format\n");

    ghost.session = logic_connect(&ghost.generation);
    expect_only_reply(&ghost, MSG_PRIVATE, "on", RESP_FAILURE, MODE_NONE, "not logged in", "Pv before login");
    leave(&ghost);

    expect_only_reply(&alice, MSG_PRIVATE, "", RESP_FAILURE, MODE_NONE, "bad format", "Pv empty");
    expect_only_reply(&alice, MSG_PRIVATE, "maybe", RESP_FAILURE, MODE_NONE, "bad format", "Pv maybe");
    expect_only_reply(&alice, MSG_PRIVATE, "ON", RESP_FAILURE, MODE_NONE, "bad format", "Pv is case sensitive (ON)");
    expect_only_reply(&alice, MSG_PRIVATE, " on", RESP_FAILURE, MODE_NONE, "bad format", "Pv with a leading space");
    expect_only_reply(&alice, MSG_PRIVATE, "on ", RESP_FAILURE, MODE_NONE, "bad format", "Pv with a trailing space");
    expect_only_reply(&alice, MSG_PRIVATE, "onoff", RESP_FAILURE, MODE_NONE, "bad format", "Pv onoff");

    expect_only_reply(&alice, MSG_PRIVATE, "on", RESP_SUCCESS, MODE_NONE, "on", "Pv on");
    expect_only_reply(&alice, MSG_PRIVATE, "on", RESP_SUCCESS, MODE_NONE, "on", "Pv on twice is fine");
    expect_only_reply(&alice, MSG_PRIVATE, "off", RESP_SUCCESS, MODE_NONE, "off", "Pv off");
    expect_only_reply(&alice, MSG_PRIVATE, "off", RESP_SUCCESS, MODE_NONE, "off", "Pv off twice is fine");

    //------------------------------------------------------------
    printf("\n-- Public game\n");

    int g = start_game(&alice, &bob);
    report(g > 0, "game alice-bob starts", "");
    report(strcmp(visibility(&carol, g), "pub") == 0, "LG: pub", visibility(&carol, g));
    report(can_watch(&carol, g, reason, sizeof(reason)), "anyone can watch a public game", reason);

    //------------------------------------------------------------
    printf("\n-- One player private (set during the game)\n");

    expect_only_reply(&alice, MSG_PRIVATE, "on", RESP_SUCCESS, MODE_NONE, "on", "Pv on from inside a game");
    report(strcmp(visibility(&carol, g), "priv") == 0, "LG: priv as soon as one player is private", "");
    report(strcmp(visibility(&alice, g), "priv") == 0, "the players see priv too", "");

    report(!can_watch(&carol, g, reason, sizeof(reason)) && strcmp(reason, "game is private") == 0,
           "a stranger is refused: game is private", reason);

    //Friendship is mutual: either side can create it
    send_request(&dave, MSG_FRIEND_ADD, "alice", &o);
    report(entry_is(find_entry(&o, &dave, MSG_FRIEND_ADD), &dave, RESP_SUCCESS, MODE_NONE, "alice"),
           "dave adds alice as a friend", "");
    report(can_watch(&dave, g, reason, sizeof(reason)), "a friend of the private player can watch", reason);

    send_request(&bob, MSG_FRIEND_ADD, "erin", &o);
    report(!can_watch(&erin, g, reason, sizeof(reason)),
           "a friend of the public player only: refused", reason);

    //------------------------------------------------------------
    printf("\n-- Both players private\n");

    send_request(&bob, MSG_PRIVATE, "on", &o);
    report(!can_watch(&dave, g, reason, sizeof(reason)), "friend of alice only: refused", reason);
    report(!can_watch(&erin, g, reason, sizeof(reason)), "friend of bob only: refused", reason);

    send_request(&frank, MSG_FRIEND_ADD, "alice", &o);
    send_request(&frank, MSG_FRIEND_ADD, "bob", &o);
    report(can_watch(&frank, g, reason, sizeof(reason)), "friend of both: allowed", reason);
    report(!can_watch(&carol, g, reason, sizeof(reason)), "stranger: still refused", reason);

    //------------------------------------------------------------
    printf("\n-- Removing a friend\n");

    send_request(&alice, MSG_FRIEND_REMOVE, "frank", &o);
    report(entry_is(find_entry(&o, &alice, MSG_FRIEND_REMOVE), &alice, RESP_SUCCESS, MODE_NONE, "frank"),
           "alice removes frank", "");
    report(!can_watch(&frank, g, reason, sizeof(reason)), "frank can no longer watch", reason);

    send_request(&frank, MSG_FRIEND_ADD, "alice", &o);
    report(can_watch(&frank, g, reason, sizeof(reason)), "friends again: allowed again", reason);

    //------------------------------------------------------------
    printf("\n-- Turning private mode off\n");

    send_request(&alice, MSG_PRIVATE, "off", &o);
    report(strcmp(visibility(&carol, g), "priv") == 0, "alice off, bob still on: priv", "");
    report(!can_watch(&carol, g, reason, sizeof(reason)), "stranger still refused", reason);

    send_request(&bob, MSG_PRIVATE, "off", &o);
    report(strcmp(visibility(&carol, g), "pub") == 0, "both off: pub", "");
    report(can_watch(&carol, g, reason, sizeof(reason)), "stranger allowed again", reason);

    //------------------------------------------------------------
    printf("\n-- Private mode and someone already watching\n");

    char id[16];
    snprintf(id, sizeof(id), "%d", g);
    send_request(&carol, MSG_OBSERVE, id, &o);
    send_request(&alice, MSG_PRIVATE, "on", &o);

    report(o.count == 1, "Pv on sends nothing to current observers", "");

    //Current behaviour: an observer already in stays until Ox or game end
    send_request(&carol, MSG_LIST_ONLINE, "", &o);
    report(o.count == 1 && strstr(o.entries[0].message.content, "carol:O") != NULL,
           "carol keeps watching (checked only at O time)", "");

    send_request(&carol, MSG_STOP_OBSERVE, "", &o);
    report(!can_watch(&carol, g, reason, sizeof(reason)), "after Ox she cannot come back", reason);

    //------------------------------------------------------------
    printf("\n-- Private mode only affects watching\n");

    send_request(&carol, MSG_CHAT, "alice can I watch?", &o);
    report(entry_is(find_entry(&o, &alice, MSG_CHAT), &alice, RESP_NONE, MODE_NONE, "carol can I watch?"),
           "a stranger can still chat with a private player", "");

    send_request(&carol, MSG_READ_BIO, "alice", &o);
    report(entry_is(find_entry(&o, &carol, MSG_READ_BIO), &carol, RESP_SUCCESS, MODE_NONE, NULL),
           "a stranger can still read the bio", "");

    send_request(&carol, MSG_LIST_ONLINE, "", &o);
    report(o.count == 1 && strstr(o.entries[0].message.content, "alice:G") != NULL,
           "a private player still shows in LO", "");

    //End the game, then a private player can still be challenged and play
    send_request(&bob, MSG_RESIGN, "", &o);
    int g2 = start_game(&carol, &alice);
    report(g2 > 0, "a private player can be challenged and play", "");
    report(strcmp(visibility(&erin, g2), "priv") == 0, "her new game is private from the start", "");
    report(!can_watch(&erin, g2, reason, sizeof(reason)), "a stranger cannot watch it", reason);
    report(can_watch(&dave, g2, reason, sizeof(reason)), "her friend can", reason);

    //------------------------------------------------------------
    printf("\n-- Private mode across reconnection\n");

    send_request(&carol, MSG_RESIGN, "", &o);
    leave(&alice);
    report(relogin(&alice) == 0, "alice logs in again", "");

    int g3 = start_game(&alice, &bob);
    report(strcmp(visibility(&erin, g3), "priv") == 0,
           "private mode is kept while the server runs", visibility(&erin, g3));

    //------------------------------------------------------------
    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}
