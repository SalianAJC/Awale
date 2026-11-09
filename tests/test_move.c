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
    int side;
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
static void send_request(int session, const char *type,
                         const char *content, Outbox *outbox)
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

    if (outbox.count < 1 ||
        outbox.entries[0].message.response != RESP_SUCCESS) {
        return -1;
    }

    return 0;
}

//Returns the entry sent to this session with this type, or NULL
static const OutboxEntry *find_entry(const Outbox *outbox,
                                     int session, const char *type)
{
    for (int i = 0; i < outbox->count; i++) {
        if (outbox->entries[i].session_index == session &&
            strcmp(outbox->entries[i].message.type, type) == 0) {
            return &outbox->entries[i];
        }
    }

    return NULL;
}

//Checks recipient, generation, response, mode and content
static int entry_is(const OutboxEntry *e, const Player *to,
                    char response, char mode, const char *content)
{
    return e != NULL &&
           e->session_index == to->session &&
           e->generation == to->generation &&
           e->message.response == response &&
           e->message.mode == mode &&
           (content == NULL ||
            strcmp(e->message.content, content) == 0);
}

static void describe(const Outbox *outbox, char *buffer, size_t size)
{
    buffer[0] = '\0';

    snprintf(buffer, size, "%d entr%s:",
             outbox->count,
             outbox->count == 1 ? "y" : "ies");

    for (int i = 0; i < outbox->count && strlen(buffer) + 80 < size; i++) {
        const OutboxEntry *e = &outbox->entries[i];
        char one[MAX_CONTENT + 64];

        snprintf(one, sizeof(one), " [s%d %s|%c|%c|%s]",
                 e->session_index,
                 e->message.type,
                 e->message.response ? e->message.response : '?',
                 e->message.mode ? e->message.mode : '?',
                 e->message.content);

        strncat(buffer, one, size - strlen(buffer) - 1);
    }
}

//A failed move must return one Mo failure in mode P
static void expect_move_failure(const Player *from, const char *content,
                                const char *reason, const char *label)
{
    Outbox outbox;
    char details[2048];

    send_request(from->session, MSG_MOVE, content, &outbox);
    describe(&outbox, details, sizeof(details));

    int ok = outbox.count == 1 &&
             entry_is(&outbox.entries[0], from,
                      RESP_FAILURE, MODE_PLAYER, reason);

    report(ok, label, details);
}

//Starts a game between alice and bob and reads their sides from Gs
static int start_game(Player *alice, Player *bob)
{
    Outbox outbox;
    const OutboxEntry *gs_alice;
    const OutboxEntry *gs_bob;

    send_request(alice->session, MSG_CHALLENGE, bob->name, &outbox);

    send_request(bob->session, MSG_ACCEPT, alice->name, &outbox);

    gs_alice = find_entry(&outbox, alice->session, MSG_GAME_START);
    gs_bob = find_entry(&outbox, bob->session, MSG_GAME_START);

    if (gs_alice == NULL || gs_bob == NULL) {
        return -1;
    }

    int id_a, id_b;
    char opponent_a[MAX_NAME + 1];
    char opponent_b[MAX_NAME + 1];

    if (sscanf(gs_alice->message.content, "%d %d %32s",
               &id_a, &alice->side, opponent_a) != 3) {
        return -1;
    }

    if (sscanf(gs_bob->message.content, "%d %d %32s",
               &id_b, &bob->side, opponent_b) != 3) {
        return -1;
    }

    if (id_a != id_b ||
        (alice->side != 0 && alice->side != 1) ||
        bob->side != 1 - alice->side) {
        return -1;
    }

    return 0;
}

// ----------------------------------------------------------------
// Tests
// ----------------------------------------------------------------

int main(void)
{
    Player alice = { "alice", 0, 0, -1 };
    Player bob = { "bob", 0, 0, -1 };

    Outbox outbox;
    char details[2048];

    //------------------------------------------------------------
    printf("-- Setup\n");

    report(logic_init() == 0, "logic_init", "");

    int ok = login_player(&alice) == 0 &&
             login_player(&bob) == 0;

    report(ok, "alice and bob logged in", "");

    if (!ok) {
        printf("\nCannot continue without the players.\n");
        return 1;
    }

    ok = start_game(&alice, &bob) == 0;

    snprintf(details, sizeof(details),
             "alice side %d, bob side %d",
             alice.side, bob.side);

    report(ok, "challenge and accept: game started", details);

    if (!ok) {
        printf("\nCannot continue without a game.\n");
        return 1;
    }

    //Game always starts with side 0
    Player *first = alice.side == 0 ? &alice : &bob;
    Player *second = alice.side == 1 ? &alice : &bob;

    //------------------------------------------------------------
    printf("\n-- Mo: bad format\n");

    expect_move_failure(first, "x", "bad format", "Mo x");
    expect_move_failure(first, "-1", "bad format", "Mo -1");
    expect_move_failure(first, "12", "bad format", "Mo 12");

    //------------------------------------------------------------
    printf("\n-- Mo: turn and legality checks\n");

    //Side 1 must not move before side 0
    expect_move_failure(second, "6", "not your turn",
                        "Mo from player whose turn it is not");

    //Side 0 cannot play a hole belonging to side 1
    expect_move_failure(first, "6", "illegal move",
                        "Mo on opponent's side");

    //------------------------------------------------------------
    printf("\n-- Mo: successful move\n");

    send_request(first->session, MSG_MOVE, "0", &outbox);
    describe(&outbox, details, sizeof(details));

    ok = outbox.count == 3 &&
         entry_is(find_entry(&outbox, first->session, MSG_MOVE),
                  first, RESP_SUCCESS, MODE_PLAYER, "0") &&
         entry_is(find_entry(&outbox, first->session, MSG_BOARD_UPDATE),
                  first, RESP_NONE, MODE_PLAYER, "1 0") &&
         entry_is(find_entry(&outbox, second->session, MSG_BOARD_UPDATE),
                  second, RESP_NONE, MODE_PLAYER, "1 0");

    report(ok, "Mo success: reply + Bu to both players", details);

    //------------------------------------------------------------
    printf("\n-- Mo: turns alternate\n");

    //Side 0 has just played, so it cannot play again
    expect_move_failure(first, "1", "not your turn",
                        "Mo twice in a row");

    //Side 1 can now play
    send_request(second->session, MSG_MOVE, "6", &outbox);
    describe(&outbox, details, sizeof(details));

    ok = outbox.count == 3 &&
         entry_is(find_entry(&outbox, second->session, MSG_MOVE),
                  second, RESP_SUCCESS, MODE_PLAYER, "6") &&
         entry_is(find_entry(&outbox, first->session, MSG_BOARD_UPDATE),
                  first, RESP_NONE, MODE_PLAYER, "1 6") &&
         entry_is(find_entry(&outbox, second->session, MSG_BOARD_UPDATE),
                  second, RESP_NONE, MODE_PLAYER, "1 6");

    report(ok, "Second player can move", details);

    //------------------------------------------------------------
    printf("\n-- Mo: empty hole\n");

    //After playing hole 0, side 0's hole 0 is empty.
    //Side 0 gets the turn again after side 1 plays.
    send_request(first->session, MSG_MOVE, "0", &outbox);

    ok = outbox.count == 1 &&
         entry_is(&outbox.entries[0], first,
                  RESP_FAILURE, MODE_PLAYER, "illegal move");

    describe(&outbox, details, sizeof(details));
    report(ok, "Mo from an empty hole", details);

    //------------------------------------------------------------

    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}