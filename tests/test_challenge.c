//Tests for the challenge feature: C, Ch, Rf, A, Gs (spec 3.3).
//Only uses the public functions of server_logic, and reads the outbox.
//Build (Wine):
//  x86_64-w64-mingw32-gcc -Wall -Wextra -g -Iinclude tests/test_challenge.c src/server_logic.c src/game.c -o target/test_challenge.exe
//  WINEDEBUG=-all wine target/test_challenge.exe

#include <stdio.h>
#include <string.h>

#include "server_logic.h"

static int passed = 0;
static int failed = 0;

//One logged-in test player
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
    printf("%s  %-40s %s\n", ok ? "PASS" : "FAIL", label, details);

    if (ok) {
        passed++;
    } else {
        failed++;
    }
}

//Sends one request from a session and fills the outbox
static void send_request(int session, const char *type, const char *content, Outbox *outbox)
{
    Message request = {0};
    strcpy(request.type, type);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    strcpy(request.content, content);

    memset(outbox, 0, sizeof(*outbox));
    logic_handle_message(session, &request, outbox);
}

//Connects, registers and logs in a player. Returns 0 on success
static int login_player(Player *p)
{
    Outbox outbox;

    p->session = logic_connect(&p->generation);

    if (p->session < 0) {
        return -1;
    }

    send_request(p->session, MSG_REGISTER, p->name, &outbox);
    send_request(p->session, MSG_LOGIN, p->name, &outbox);

    if (outbox.count < 1 || outbox.entries[0].message.response != RESP_SUCCESS) {
        return -1;
    }

    return 0;
}

//Registers an account but leaves it offline
static void register_offline(const char *name)
{
    unsigned int g;
    Outbox outbox;
    int s = logic_connect(&g);

    send_request(s, MSG_REGISTER, name, &outbox);
    memset(&outbox, 0, sizeof(outbox));
    logic_disconnect(s, &outbox);
}

//Returns the entry sent to this session with this type, or NULL
static const OutboxEntry *find_entry(const Outbox *outbox, int session, const char *type)
{
    for (int i = 0; i < outbox->count; i++) {
        if (outbox->entries[i].session_index == session &&
            strcmp(outbox->entries[i].message.type, type) == 0) {
            return &outbox->entries[i];
        }
    }

    return NULL;
}

//Checks one entry: recipient, generation, response, mode, content (NULL = any content)
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

static void describe(const Outbox *outbox, char *buffer, size_t size)
{
    buffer[0] = '\0';
    snprintf(buffer, size, "%d entr%s:", outbox->count, outbox->count == 1 ? "y" : "ies");

    for (int i = 0; i < outbox->count && strlen(buffer) + 80 < size; i++) {
        const OutboxEntry *e = &outbox->entries[i];
        char one[MAX_CONTENT + 64];

        snprintf(one, sizeof(one), " [s%d %s|%c|%c|%s]",
                 e->session_index, e->message.type,
                 e->message.response ? e->message.response : '?',
                 e->message.mode ? e->message.mode : '?',
                 e->message.content);

        strncat(buffer, one, size - strlen(buffer) - 1);
    }
}

//A request that must fail: exactly one entry, back to the sender, F, the reason
static void expect_failure(const Player *from, const char *type, const char *content,
                           const char *reason, const char *label)
{
    Outbox outbox;
    char details[2048];

    send_request(from->session, type, content, &outbox);
    describe(&outbox, details, sizeof(details));

    int ok = outbox.count == 1 &&
             strcmp(outbox.entries[0].message.type, type) == 0 &&
             entry_is(&outbox.entries[0], from, RESP_FAILURE, MODE_NONE, reason);

    report(ok, label, details);
}

//Checks that the online list contains this exact entry, e.g. "alice:G"
static void expect_in_list(const Player *from, const char *item, const char *label)
{
    Outbox outbox;
    char details[2048];

    send_request(from->session, MSG_LIST_ONLINE, "", &outbox);

    const char *list = outbox.count > 0 ? outbox.entries[0].message.content : "";
    char padded[MAX_CONTENT + 3];
    char wanted[MAX_NAME + 8];

    //Surround with commas so "bob:L" does not match "bob:Lx" or "xbob:L"
    snprintf(padded, sizeof(padded), ",%s,", list);
    snprintf(wanted, sizeof(wanted), ",%s,", item);

    snprintf(details, sizeof(details), "looking for %s in \"%.1500s\"", item, list);
    report(strstr(padded, wanted) != NULL, label, details);
}

// ----------------------------------------------------------------
// Tests
// ----------------------------------------------------------------

int main(void)
{
    Player alice = { "alice", 0, 0 };
    Player bob   = { "bob",   0, 0 };
    Player carol = { "carol", 0, 0 };
    Player dave  = { "dave",  0, 0 };
    Player frank = { "frank", 0, 0 };

    Outbox outbox;
    char details[2048];

    //------------------------------------------------------------
    printf("-- Setup\n");

    report(logic_init() == 0, "logic_init", "");

    int ok = login_player(&alice) == 0 && login_player(&bob) == 0 &&
             login_player(&carol) == 0 && login_player(&dave) == 0;
    report(ok, "alice, bob, carol, dave logged in", "");

    if (!ok) {
        printf("\nCannot continue without the players.\n");
        return 1;
    }

    register_offline("eve");

    //------------------------------------------------------------
    printf("\n-- C: challenge\n");

    expect_failure(&alice, MSG_CHALLENGE, "zoe", "unknown user", "C unknown user");
    expect_failure(&alice, MSG_CHALLENGE, "alice", "cannot challenge yourself", "C yourself");
    expect_failure(&alice, MSG_CHALLENGE, "eve", "user offline", "C offline user");

    //Success: reply to alice, Ch push to bob
    send_request(alice.session, MSG_CHALLENGE, "bob", &outbox);
    describe(&outbox, details, sizeof(details));
    ok = outbox.count == 2 &&
         entry_is(find_entry(&outbox, alice.session, MSG_CHALLENGE), &alice, RESP_SUCCESS, MODE_NONE, "bob") &&
         entry_is(find_entry(&outbox, bob.session, MSG_CHALLENGED), &bob, RESP_NONE, MODE_NONE, "alice");
    report(ok, "C success: reply + Ch to bob", details);

    expect_failure(&alice, MSG_CHALLENGE, "carol", "challenge already pending", "C while one is pending");

    //------------------------------------------------------------
    printf("\n-- Rf: refuse\n");

    expect_failure(&carol, MSG_REFUSE, "alice", "no challenge from this user", "Rf, alice challenged bob not carol");
    expect_failure(&bob, MSG_REFUSE, "zoe", "no challenge from this user", "Rf unknown user");

    send_request(bob.session, MSG_REFUSE, "alice", &outbox);
    describe(&outbox, details, sizeof(details));
    ok = outbox.count == 2 &&
         entry_is(find_entry(&outbox, bob.session, MSG_REFUSE), &bob, RESP_SUCCESS, MODE_NONE, "alice") &&
         entry_is(find_entry(&outbox, alice.session, MSG_REFUSE), &alice, RESP_NONE, MODE_NONE, "bob");
    report(ok, "Rf success: reply + Rf push to alice", details);

    expect_failure(&bob, MSG_REFUSE, "alice", "no challenge from this user", "Rf twice");

    //Alice's challenge was cleared, she can challenge again
    send_request(alice.session, MSG_CHALLENGE, "bob", &outbox);
    report(entry_is(find_entry(&outbox, alice.session, MSG_CHALLENGE), &alice, RESP_SUCCESS, MODE_NONE, "bob"),
           "C again after a refusal", "");

    //------------------------------------------------------------
    printf("\n-- A: accept\n");

    expect_failure(&carol, MSG_ACCEPT, "alice", "no challenge from this user", "A without a challenge");
    expect_failure(&bob, MSG_ACCEPT, "zoe", "no challenge from this user", "A unknown user");

    //Two more pending challenges, to check the cancellations:
    //dave challenges alice, carol challenges bob
    send_request(dave.session, MSG_CHALLENGE, "alice", &outbox);
    report(outbox.count == 2, "dave challenges alice (setup)", "");
    send_request(carol.session, MSG_CHALLENGE, "bob", &outbox);
    report(outbox.count == 2, "carol challenges bob (setup)", "");

    //Bob accepts alice
    send_request(bob.session, MSG_ACCEPT, "alice", &outbox);
    describe(&outbox, details, sizeof(details));

    report(outbox.count == 5, "A success: 5 entries", details);

    report(entry_is(find_entry(&outbox, bob.session, MSG_ACCEPT), &bob, RESP_SUCCESS, MODE_NONE, "alice"),
           "A reply to bob", "");

    const OutboxEntry *gs_a = find_entry(&outbox, alice.session, MSG_GAME_START);
    const OutboxEntry *gs_b = find_entry(&outbox, bob.session, MSG_GAME_START);

    report(entry_is(gs_a, &alice, RESP_NONE, MODE_PLAYER, NULL), "Gs to alice (mode P)", "");
    report(entry_is(gs_b, &bob, RESP_NONE, MODE_PLAYER, NULL), "Gs to bob (mode P)", "");

    if (gs_a != NULL && gs_b != NULL) {
        int id_a = -1, id_b = -2, side_a = -1, side_b = -1;
        char opp_a[MAX_NAME + 1] = "", opp_b[MAX_NAME + 1] = "";

        int n_a = sscanf(gs_a->message.content, "%d %d %32s", &id_a, &side_a, opp_a);
        int n_b = sscanf(gs_b->message.content, "%d %d %32s", &id_b, &side_b, opp_b);

        snprintf(details, sizeof(details), "alice \"%.100s\", bob \"%.100s\"",
                 gs_a->message.content, gs_b->message.content);

        report(n_a == 3 && n_b == 3, "Gs format: <id> <side> <opponent>", details);
        report(id_a == id_b && id_a >= 1, "Gs same game id, >= 1", details);
        report((side_a == 0 && side_b == 1) || (side_a == 1 && side_b == 0),
               "Gs one player per side", details);
        report(strcmp(opp_a, "bob") == 0 && strcmp(opp_b, "alice") == 0,
               "Gs names the opponent", details);
    }

    //Cancellations: dave's challenge to alice, carol's challenge to bob
    report(entry_is(find_entry(&outbox, dave.session, MSG_REFUSE), &dave, RESP_NONE, MODE_NONE, "alice"),
           "dave gets Rf from alice (cancelled)", "");
    report(entry_is(find_entry(&outbox, carol.session, MSG_REFUSE), &carol, RESP_NONE, MODE_NONE, "bob"),
           "carol gets Rf from bob (cancelled)", "");
    report(find_entry(&outbox, alice.session, MSG_REFUSE) == NULL,
           "alice gets no Rf for her own game", "");

    //------------------------------------------------------------
    printf("\n-- After the game started\n");

    expect_in_list(&carol, "alice:G", "alice is in game (LO)");
    expect_in_list(&carol, "bob:G", "bob is in game (LO)");
    expect_in_list(&carol, "carol:L", "carol is still in the lobby (LO)");

    expect_failure(&carol, MSG_CHALLENGE, "alice", "user busy", "C to a player in game");
    expect_failure(&alice, MSG_CHALLENGE, "carol", "not allowed now", "C from a player in game");

    //Carol's challenge was cancelled, so she can challenge again
    send_request(carol.session, MSG_CHALLENGE, "dave", &outbox);
    report(find_entry(&outbox, carol.session, MSG_CHALLENGE) != NULL &&
           find_entry(&outbox, carol.session, MSG_CHALLENGE)->message.response == RESP_SUCCESS,
           "carol can challenge again", "");

    //A second game at the same time: dave accepts carol
    send_request(dave.session, MSG_ACCEPT, "carol", &outbox);
    gs_a = find_entry(&outbox, carol.session, MSG_GAME_START);
    snprintf(details, sizeof(details), "\"%.100s\"", gs_a ? gs_a->message.content : "(none)");
    report(gs_a != NULL && gs_a->message.content[0] == '2', "second game gets id 2", details);

    //------------------------------------------------------------
    printf("\n-- Disconnect cancels challenges to the leaver\n");

    if (login_player(&frank) != 0) {
        report(0, "frank logged in", "");
    } else {
        Player greg = { "greg", 0, 0 };
        login_player(&greg);

        //greg challenges frank, then frank leaves
        send_request(greg.session, MSG_CHALLENGE, "frank", &outbox);

        memset(&outbox, 0, sizeof(outbox));
        logic_disconnect(frank.session, &outbox);
        describe(&outbox, details, sizeof(details));

        report(entry_is(find_entry(&outbox, greg.session, MSG_REFUSE), &greg, RESP_NONE, MODE_NONE, "frank"),
               "greg gets Rf from frank", details);

        //greg is free to challenge again
        expect_failure(&greg, MSG_CHALLENGE, "frank", "user offline", "greg can challenge again");
    }

    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}