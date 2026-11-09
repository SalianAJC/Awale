
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "server_logic.h"

#define INITIAL_RATING START_RATING
#define TEST_K 32

typedef struct {
    const char *name;
    int session;
    unsigned int generation;
    int side;
} Player;

static int passed = 0;
static int failed = 0;

static void report(int ok, const char *label, const char *details)
{
    printf("%s  %-42s %s\n",
           ok ? "PASS" : "FAIL", label, details);

    if (ok)
        passed++;
    else
        failed++;
}

static void send_request(int session, const char *type,
                         const char *content, Outbox *outbox)
{
    Message request = {0};

    strcpy(request.type, type);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;

    if (content != NULL)
        snprintf(request.content, sizeof(request.content), "%s", content);

    memset(outbox, 0, sizeof(*outbox));
    logic_handle_message(session, &request, outbox);
}

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

static int connect_player(Player *player)
{
    player->session = logic_connect(&player->generation);
    return player->session < 0 ? -1 : 0;
}

static int register_player(Player *player)
{
    Outbox outbox;

    if (connect_player(player) != 0)
        return -1;

    send_request(player->session, MSG_REGISTER, player->name, &outbox);

    if (outbox.count < 1 ||
        outbox.entries[0].message.response != RESP_SUCCESS)
        return -1;

    send_request(player->session, MSG_LOGIN, player->name, &outbox);

    if (outbox.count < 1 ||
        strcmp(outbox.entries[0].message.type, MSG_LOGIN) != 0 ||
        outbox.entries[0].message.response != RESP_SUCCESS)
        return -1;

    return 0;
}

/* Login an account that already exists in the persistence file. */
static int login_existing_player(Player *player)
{
    Outbox outbox;

    if (connect_player(player) != 0)
        return -1;

    send_request(player->session, MSG_LOGIN, player->name, &outbox);

    if (outbox.count < 1 ||
        strcmp(outbox.entries[0].message.type, MSG_LOGIN) != 0 ||
        outbox.entries[0].message.response != RESP_SUCCESS)
        return -1;

    return 0;
}

static int start_game(Player *alice, Player *bob)
{
    Outbox outbox;

    send_request(alice->session, MSG_CHALLENGE, bob->name, &outbox);

    send_request(bob->session, MSG_ACCEPT, alice->name, &outbox);

    const OutboxEntry *start_alice =
        find_entry(&outbox, alice->session, MSG_GAME_START);

    const OutboxEntry *start_bob =
        find_entry(&outbox, bob->session, MSG_GAME_START);

    if (start_alice == NULL || start_bob == NULL)
        return -1;

    int game_id_alice, game_id_bob;
    char opponent[MAX_NAME + 1];

    if (sscanf(start_alice->message.content, "%d %d %32s",
               &game_id_alice, &alice->side, opponent) != 3)
        return -1;

    if (sscanf(start_bob->message.content, "%d %d %32s",
               &game_id_bob, &bob->side, opponent) != 3)
        return -1;

    if (game_id_alice != game_id_bob ||
        (alice->side != 0 && alice->side != 1) ||
        bob->side != 1 - alice->side)
        return -1;

    return 0;
}

/* Request the ranking and extract one player's rating. */
static int get_rating(int session, const char *username, int *rating)
{
    Outbox outbox;

    send_request(session, MSG_RANKING, "", &outbox);

    if (outbox.count != 1)
        return -1;

    const Message *reply = &outbox.entries[0].message;

    if (strcmp(reply->type, MSG_RANKING) != 0 ||
        reply->response != RESP_SUCCESS)
        return -1;

    char content[MAX_CONTENT + 1];
    snprintf(content, sizeof(content), "%s", reply->content);

    char *entry = strtok(content, ",");

    while (entry != NULL) {
        char *separator = strchr(entry, ':');

        if (separator != NULL) {
            *separator = '\0';

            char *value_text = separator + 1;
            char *end = NULL;
            long value = strtol(value_text, &end, 10);

            if (end != value_text && *end == '\0' &&
                strcmp(entry, username) == 0) {
                *rating = (int)value;
                return 0;
            }
        }

        entry = strtok(NULL, ",");
    }

    return -1;
}

/* Calculate the expected ratings after a decisive result. */
static void calculate_elo(int rating_a, int rating_b,
                          double score_a,
                          int *new_a, int *new_b)
{
    double expected_a =
        1.0 / (1.0 + pow(10.0, ((double)rating_b - rating_a) / 400.0));

    double expected_b = 1.0 - expected_a;
    double score_b = 1.0 - score_a;

    *new_a = (int)lround(rating_a + TEST_K * (score_a - expected_a));
    *new_b = (int)lround(rating_b + TEST_K * (score_b - expected_b));
}

static int check_ratings(Player *alice, Player *bob,
                         int expected_alice, int expected_bob,
                         const char *label)
{
    int alice_rating = -1;
    int bob_rating = -1;

    int ok_alice =
        get_rating(alice->session, alice->name, &alice_rating) == 0;

    int ok_bob =
        get_rating(bob->session, bob->name, &bob_rating) == 0;

    char details[256];

    snprintf(details, sizeof(details),
             "Alice: %d (expected %d), Bob: %d (expected %d)",
             alice_rating, expected_alice,
             bob_rating, expected_bob);

    int ok = ok_alice && ok_bob &&
             alice_rating == expected_alice &&
             bob_rating == expected_bob;

    report(ok, label, details);

    return ok ? 0 : -1;
}

static int check_ranking(Player *player,
                         const char *first,
                         const char *second)
{
    Outbox outbox;

    send_request(player->session, MSG_RANKING, "", &outbox);

    if (outbox.count != 1)
        return -1;

    const Message *reply = &outbox.entries[0].message;

    if (strcmp(reply->type, MSG_RANKING) != 0 ||
        reply->response != RESP_SUCCESS)
        return -1;

    char *first_pos = strstr(reply->content, first);
    char *second_pos = strstr(reply->content, second);

    return first_pos != NULL && second_pos != NULL &&
           first_pos < second_pos ? 0 : -1;
}

static void disconnect_player(Player *player)
{
    Outbox outbox;
    memset(&outbox, 0, sizeof(outbox));
    logic_disconnect(player->session, &outbox);
}

int main(void)
{
    Player alice = { "Alice", -1, 0, -1 };
    Player bob = { "Bob", -1, 0, -1 };

    Outbox outbox;
    char details[256];

    int alice_rating = -1;
    int bob_rating = -1;

    printf("-- ELO tests\n");

    /* Start with clean persistence files. */
    remove(USERS_FILE);
    remove(FRIENDS_FILE);

    report(logic_init() == 0, "Initialize server logic", "");

    /* Register and log in both players. */
    int registered =
        register_player(&alice) == 0 &&
        register_player(&bob) == 0;

    report(registered, "Register and log in players", "");

    if (!registered)
        return 1;

    /* Test initial ratings. */
    check_ratings(&alice, &bob,
                  INITIAL_RATING, INITIAL_RATING,
                  "New accounts start at 1200");

    /* =====================================================
       TEST 1: RESIGNATION
       Alice resigns, so Bob wins.
       Equal ratings: Bob +16, Alice -16.
       ===================================================== */

    int started = start_game(&alice, &bob) == 0;

    report(started, "Start first game", "");

    if (!started)
        return 1;

    send_request(alice.session, MSG_RESIGN, "", &outbox);

    int ended =
        find_entry(&outbox, alice.session, MSG_GAME_OVER) != NULL &&
        find_entry(&outbox, bob.session, MSG_GAME_OVER) != NULL;

    report(ended, "Resignation ends the game", "");

    if (!ended)
        return 1;

    check_ratings(&alice, &bob,
                  INITIAL_RATING - 16,
                  INITIAL_RATING + 16,
                  "Winner gains 16, loser loses 16");

    /* =====================================================
       TEST 2: UPSET VICTORY
       Alice is now lower-rated.
       Bob resigns, so Alice wins.
       ===================================================== */

    int old_alice = INITIAL_RATING - 16;
    int old_bob = INITIAL_RATING + 16;
    int expected_alice, expected_bob;

    calculate_elo(old_alice, old_bob, 1.0,
                  &expected_alice, &expected_bob);

    started = start_game(&alice, &bob) == 0;

    report(started, "Start second game", "");

    if (!started)
        return 1;

    send_request(bob.session, MSG_RESIGN, "", &outbox);

    ended =
        find_entry(&outbox, alice.session, MSG_GAME_OVER) != NULL &&
        find_entry(&outbox, bob.session, MSG_GAME_OVER) != NULL;

    report(ended, "Second game ends after resignation", "");

    if (!ended)
        return 1;

    check_ratings(&alice, &bob,
                  expected_alice, expected_bob,
                  "Lower-rated player wins");

    /* =====================================================
       TEST 3: DISCONNECT
       Bob disconnects during a game.
       Alice should win.
       ===================================================== */

    old_alice = expected_alice;
    old_bob = expected_bob;

    calculate_elo(old_alice, old_bob, 1.0,
                  &expected_alice, &expected_bob);

    started = start_game(&alice, &bob) == 0;

    report(started, "Start third game", "");

    if (!started)
        return 1;

    memset(&outbox, 0, sizeof(outbox));
    logic_disconnect(bob.session, &outbox);

    ended =
        find_entry(&outbox, alice.session, MSG_GAME_OVER) != NULL;

    report(ended, "Opponent disconnect ends the game", "");

    if (!ended)
        return 1;

    /* Bob is disconnected, so inspect Alice's ranking response
       and verify her rating. Bob's rating is checked after relogin. */
    if (get_rating(alice.session, alice.name, &alice_rating) == 0) {
        snprintf(details, sizeof(details),
                 "Alice: %d (expected %d)",
                 alice_rating, expected_alice);

        report(alice_rating == expected_alice,
               "Disconnect victory updates winner rating", details);
    } else {
        report(0, "Disconnect victory updates winner rating",
               "Could not retrieve Alice's rating");
    }

    /* Disconnect Alice too. */
    disconnect_player(&alice);

    /* =====================================================
       TEST 4: PERSISTENCE
       Reinitialize logic and log in existing accounts.
       ===================================================== */

    int init_again = logic_init() == 0;

    report(init_again, "Reinitialize server logic", "");

    if (!init_again)
        return 1;

    int relogged =
        login_existing_player(&alice) == 0 &&
        login_existing_player(&bob) == 0;

    report(relogged, "Log in existing accounts", "");

    if (!relogged)
        return 1;

    check_ratings(&alice, &bob,
                  expected_alice, expected_bob,
                  "Ratings persist after reinitialization");

    /* =====================================================
       TEST 5: RANKING ORDER
       ===================================================== */

    int ranking_ok = check_ranking(&alice, "Alice:", "Bob:");

    snprintf(details, sizeof(details),
             "Expected Alice before Bob");

    report(ranking_ok == 0,
           "Ranking sorted by current ratings", details);

    /* Cleanup. */
    disconnect_player(&alice);
    disconnect_player(&bob);

    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}
