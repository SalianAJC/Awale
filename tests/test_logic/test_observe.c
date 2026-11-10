// Logic tests for LG, O and Ox: exact replies, pushes, modes and generations.

#include <stdio.h>
#include <string.h>

#include "server_logic.h"
#include "game.h"

static int passed = 0;
static int failed = 0;

typedef struct {
    const char *name;
    int session;
    unsigned int generation;
} Player;

// One running game, as the test sees it
typedef struct {
    int id;
    Player *side[2];
    Game board;
} TestGame;

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

static void leave(Player *p, Outbox *outbox)
{
    memset(outbox, 0, sizeof(*outbox));
    logic_disconnect(p->session, outbox);
}

// Number of entries for this session with this type
static int count_for(const Outbox *o, const Player *p, const char *type)
{
    int n = 0;

    for (int i = 0; i < o->count; i++) {
        if (o->entries[i].session_index == p->session &&
            strcmp(o->entries[i].message.type, type) == 0)
            n++;
    }

    return n;
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

// Checks recipient, generation, response, mode and content (NULL = any)
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

static void describe(const Outbox *o, char *buffer, size_t size)
{
    snprintf(buffer, size, "%d entr%s:", o->count, o->count == 1 ? "y" : "ies");

    for (int i = 0; i < o->count && strlen(buffer) + 80 < size; i++) {
        const OutboxEntry *e = &o->entries[i];
        char one[MAX_CONTENT + 64];

        snprintf(one, sizeof(one), " [s%d %s|%c|%c|%.40s]",
                 e->session_index, e->message.type,
                 e->message.response ? e->message.response : '?',
                 e->message.mode ? e->message.mode : '?',
                 e->message.content);
        strncat(buffer, one, size - strlen(buffer) - 1);
    }
}

// Exactly one reply to the sender, with this response, mode and content
static void expect_only_reply(const Player *p, const char *type, const char *content,
                              char response, char mode, const char *expected,
                              const char *label)
{
    Outbox o;
    char details[2048];

    send_request(p, type, content, &o);
    describe(&o, details, sizeof(details));

    report(o.count == 1 &&
           strcmp(o.entries[0].message.type, type) == 0 &&
           entry_is(&o.entries[0], p, response, mode, expected),
           label, details);
}

// Challenge + accept. Fills g with the id and sides from the Gs pushes.
static int start_game(Player *a, Player *b, TestGame *g)
{
    Outbox o;

    send_request(a, MSG_CHALLENGE, b->name, &o);
    send_request(b, MSG_ACCEPT, a->name, &o);

    const OutboxEntry *ga = find_entry(&o, a, MSG_GAME_START);
    const OutboxEntry *gb = find_entry(&o, b, MSG_GAME_START);
    int id_a, side_a, id_b, side_b;

    if (ga == NULL || gb == NULL ||
        sscanf(ga->message.content, "%d %d", &id_a, &side_a) != 2 ||
        sscanf(gb->message.content, "%d %d", &id_b, &side_b) != 2 ||
        id_a != id_b || side_a == side_b)
        return -1;

    g->id = id_a;
    g->side[side_a] = a;
    g->side[side_b] = b;
    init_game(&g->board);

    return 0;
}

// Plays the first legal move for whoever is to move. Returns the hole, -1 if none.
static int play(TestGame *g, Outbox *o)
{
    int side = g->board.current_player;

    for (int hole = side * 6; hole < side * 6 + 6; hole++) {
        Game copy = g->board;

        if (sow(&copy, hole) == 0) {
            char text[16];

            snprintf(text, sizeof(text), "%d", hole);
            send_request(g->side[side], MSG_MOVE, text, o);

            if (find_entry(o, g->side[side], MSG_MOVE) != NULL &&
                find_entry(o, g->side[side], MSG_MOVE)->message.response == RESP_SUCCESS)
                g->board = copy;

            return hole;
        }
    }

    return -1;
}

static int online_shows(const Player *asker, const char *item)
{
    Outbox o;
    char list[MAX_CONTENT + 3];

    send_request(asker, MSG_LIST_ONLINE, "", &o);

    if (o.count != 1)
        return 0;

    snprintf(list, sizeof(list), ",%s,", o.entries[0].message.content);

    char needle[MAX_NAME + 8];
    snprintf(needle, sizeof(needle), ",%s,", item);

    return strstr(list, needle) != NULL;
}

// ----------------------------------------------------------------
// Tests
// ----------------------------------------------------------------

int main(void)
{
    Player alice = {"alice", 0, 0}, bob = {"bob", 0, 0}, carol = {"carol", 0, 0}, dave = {"dave", 0, 0};
    Player erin = {"erin", 0, 0}, frank = {"frank", 0, 0}, ghost = {"ghost", 0, 0};
    Player henry = {"henry", 0, 0};
    TestGame g1, g2;
    Outbox o;
    char details[2048];
    char text[MAX_CONTENT + 1];
    char id1[16], id2[16];

    printf("-- Setup\n");

    remove(USERS_FILE);
    remove(FRIENDS_FILE);
    report(logic_init() == 0, "logic_init", "");

    int ok = login_player(&alice) == 0 && login_player(&bob) == 0 &&
             login_player(&carol) == 0 && login_player(&dave) == 0 &&
             login_player(&erin) == 0 && login_player(&frank) == 0 &&
             login_player(&henry) == 0;
    report(ok, "seven players logged in", "");

    if (!ok)
        return 1;

    //------------------------------------------------------------
    printf("\n-- Before login\n");

    ghost.session = logic_connect(&ghost.generation);
    expect_only_reply(&ghost, MSG_LIST_GAMES, "", RESP_FAILURE, MODE_NONE, "not logged in", "LG before login");
    expect_only_reply(&ghost, MSG_OBSERVE, "1", RESP_FAILURE, MODE_NONE, "not logged in", "O before login");
    expect_only_reply(&ghost, MSG_STOP_OBSERVE, "", RESP_FAILURE, MODE_NONE, "not logged in", "Ox before login");
    leave(&ghost, &o);

    //------------------------------------------------------------
    printf("\n-- LG: list of games\n");

    expect_only_reply(&carol, MSG_LIST_GAMES, "", RESP_SUCCESS, MODE_NONE, "", "LG with no game: empty");

    report(start_game(&alice, &bob, &g1) == 0, "game 1 alice-bob starts", "");
    snprintf(id1, sizeof(id1), "%d", g1.id);

    snprintf(text, sizeof(text), "%d:%s:%s:pub", g1.id, g1.side[0]->name, g1.side[1]->name);
    expect_only_reply(&carol, MSG_LIST_GAMES, "", RESP_SUCCESS, MODE_NONE, text, "LG: one game, side 0 first, pub");

    report(start_game(&dave, &erin, &g2) == 0, "game 2 dave-erin starts", "");
    snprintf(id2, sizeof(id2), "%d", g2.id);

    snprintf(text, sizeof(text), "%d:%s:%s:pub,%d:%s:%s:pub",
             g1.id, g1.side[0]->name, g1.side[1]->name,
             g2.id, g2.side[0]->name, g2.side[1]->name);
    expect_only_reply(&carol, MSG_LIST_GAMES, "", RESP_SUCCESS, MODE_NONE, text, "LG: two games, comma separated");

    expect_only_reply(&alice, MSG_LIST_GAMES, "", RESP_SUCCESS, MODE_NONE, text, "LG works from inside a game");
    expect_only_reply(&carol, MSG_LIST_GAMES, "ignored", RESP_SUCCESS, MODE_NONE, text, "LG ignores its content");

    //------------------------------------------------------------
    printf("\n-- O: bad requests\n");

    expect_only_reply(&carol, MSG_OBSERVE, "", RESP_FAILURE, MODE_OBSERVER, "bad format", "O empty");
    expect_only_reply(&carol, MSG_OBSERVE, "abc", RESP_FAILURE, MODE_OBSERVER, "bad format", "O abc");
    expect_only_reply(&carol, MSG_OBSERVE, "0", RESP_FAILURE, MODE_OBSERVER, "bad format", "O 0");
    expect_only_reply(&carol, MSG_OBSERVE, "-1", RESP_FAILURE, MODE_OBSERVER, "bad format", "O -1");
    expect_only_reply(&carol, MSG_OBSERVE, "1x", RESP_FAILURE, MODE_OBSERVER, "bad format", "O 1x");
    expect_only_reply(&carol, MSG_OBSERVE, "1 2", RESP_FAILURE, MODE_OBSERVER, "bad format", "O with two numbers");
    expect_only_reply(&carol, MSG_OBSERVE, "99999999999", RESP_FAILURE, MODE_OBSERVER, "bad format", "O number too big");
    expect_only_reply(&carol, MSG_OBSERVE, "999", RESP_FAILURE, MODE_OBSERVER, "no such game", "O unknown game id");
    expect_only_reply(&alice, MSG_OBSERVE, id2, RESP_FAILURE, MODE_NONE, "not allowed now", "O from inside a game");
    report(online_shows(&frank, "carol:L"), "failed O leaves carol in the lobby", "");

    //------------------------------------------------------------
    printf("\n-- Ox: not watching\n");

    expect_only_reply(&carol, MSG_STOP_OBSERVE, "", RESP_FAILURE, MODE_NONE, "not allowed now", "Ox from the lobby");
    expect_only_reply(&alice, MSG_STOP_OBSERVE, "", RESP_FAILURE, MODE_NONE, "not allowed now", "Ox from inside a game");

    //------------------------------------------------------------
    printf("\n-- O: success with no move yet\n");

    snprintf(text, sizeof(text), "%d %s %s ", g1.id, g1.side[0]->name, g1.side[1]->name);
    expect_only_reply(&carol, MSG_OBSERVE, id1, RESP_SUCCESS, MODE_OBSERVER, text,
                      "O|S|O: \"id side0 side1 \" (empty moves)");
    report(online_shows(&frank, "carol:O"), "LO shows carol:O", "");

    //------------------------------------------------------------
    printf("\n-- Moves reach the observer\n");

    int h1 = play(&g1, &o);
    describe(&o, details, sizeof(details));
    snprintf(text, sizeof(text), "%d %d", g1.id, h1);

    ok = o.count == 4 &&
         count_for(&o, g1.side[0], MSG_BOARD_UPDATE) == 1 &&
         count_for(&o, g1.side[1], MSG_BOARD_UPDATE) == 1 &&
         entry_is(find_entry(&o, g1.side[0], MSG_BOARD_UPDATE), g1.side[0], RESP_NONE, MODE_PLAYER, text) &&
         entry_is(find_entry(&o, g1.side[1], MSG_BOARD_UPDATE), g1.side[1], RESP_NONE, MODE_PLAYER, text) &&
         entry_is(find_entry(&o, &carol, MSG_BOARD_UPDATE), &carol, RESP_NONE, MODE_OBSERVER, text);
    report(ok, "Mo: reply + Bu to both players (P) + observer (O)", details);

    report(count_for(&o, &dave, MSG_BOARD_UPDATE) == 0 && count_for(&o, &erin, MSG_BOARD_UPDATE) == 0 &&
           count_for(&o, &frank, MSG_BOARD_UPDATE) == 0,
           "nobody else gets the Bu", "");

    play(&g2, &o);
    report(count_for(&o, &carol, MSG_BOARD_UPDATE) == 0, "carol gets nothing from game 2", "");

    //A refused move sends nothing to the observer
    int to_move = g1.board.current_player;
    snprintf(text, sizeof(text), "%d", (1 - to_move) * 6);
    send_request(g1.side[1 - to_move], MSG_MOVE, text, &o);
    report(o.count == 1 && count_for(&o, &carol, MSG_BOARD_UPDATE) == 0,
           "a refused move sends nothing to the observer", "");

    //------------------------------------------------------------
    printf("\n-- O: success with moves already played\n");

    int h2 = play(&g1, &o);
    int h3 = play(&g1, &o);

    snprintf(text, sizeof(text), "%d %s %s %d,%d,%d", g1.id, g1.side[0]->name, g1.side[1]->name, h1, h2, h3);
    expect_only_reply(&frank, MSG_OBSERVE, id1, RESP_SUCCESS, MODE_OBSERVER, text,
                      "late observer gets the full move list");

    play(&g1, &o);
    describe(&o, details, sizeof(details));
    report(o.count == 5 && count_for(&o, &carol, MSG_BOARD_UPDATE) == 1 &&
           count_for(&o, &frank, MSG_BOARD_UPDATE) == 1,
           "two observers: both get the Bu", details);

    //------------------------------------------------------------
    printf("\n-- What an observer may and may not do\n");

    expect_only_reply(&carol, MSG_OBSERVE, id1, RESP_FAILURE, MODE_NONE, "not allowed now", "O while observing");
    expect_only_reply(&carol, MSG_OBSERVE, id2, RESP_FAILURE, MODE_NONE, "not allowed now", "O another game while observing");
    expect_only_reply(&carol, MSG_MOVE, "3", RESP_FAILURE, MODE_NONE, "not allowed now", "Mo while observing");
    expect_only_reply(&carol, MSG_RESIGN, "", RESP_FAILURE, MODE_NONE, "not allowed now", "Rs while observing");
    expect_only_reply(&carol, MSG_CHALLENGE, "dave", RESP_FAILURE, MODE_NONE, "not allowed now", "C while observing");
    expect_only_reply(&carol, MSG_ACCEPT, "dave", RESP_FAILURE, MODE_NONE, "not allowed now", "A while observing");
    expect_only_reply(&carol, MSG_REFUSE, "dave", RESP_FAILURE, MODE_NONE, "not allowed now", "Rf while observing");

    send_request(&carol, MSG_LIST_ONLINE, "", &o);
    report(o.count == 1 && o.entries[0].message.response == RESP_SUCCESS, "LO while observing", "");
    send_request(&carol, MSG_LIST_GAMES, "", &o);
    report(o.count == 1 && o.entries[0].message.response == RESP_SUCCESS, "LG while observing", "");
    send_request(&carol, MSG_RANKING, "", &o);
    report(o.count == 1 && o.entries[0].message.response == RESP_SUCCESS, "Rk while observing", "");
    expect_only_reply(&carol, MSG_WRITE_BIO, "watching", RESP_SUCCESS, MODE_NONE, "bio updated", "W while observing");
    expect_only_reply(&carol, MSG_READ_BIO, "alice", RESP_SUCCESS, MODE_NONE, "no bio", "Rb while observing");
    expect_only_reply(&carol, MSG_FRIEND_LIST, "", RESP_SUCCESS, MODE_NONE, "", "Fl while observing");
    expect_only_reply(&carol, MSG_PRIVATE, "off", RESP_SUCCESS, MODE_NONE, "off", "Pv while observing");

    expect_only_reply(&henry, MSG_CHALLENGE, "carol", RESP_FAILURE, MODE_NONE, "user busy", "nobody can challenge an observer");

    //------------------------------------------------------------
    printf("\n-- Chat with and between observers\n");

    send_request(&carol, MSG_CHAT, "alice nice move", &o);
    report(o.count == 2 && entry_is(find_entry(&o, &carol, MSG_CHAT), &carol, RESP_SUCCESS, MODE_NONE, "alice") &&
           entry_is(find_entry(&o, &alice, MSG_CHAT), &alice, RESP_NONE, MODE_NONE, "carol nice move"),
           "observer -> player", "");

    send_request(&bob, MSG_CHAT, "carol thanks for watching", &o);
    report(o.count == 2 && entry_is(find_entry(&o, &carol, MSG_CHAT), &carol, RESP_NONE, MODE_NONE, "bob thanks for watching"),
           "player -> observer", "");

    send_request(&carol, MSG_CHAT, "frank who wins?", &o);
    report(o.count == 2 && entry_is(find_entry(&o, &frank, MSG_CHAT), &frank, RESP_NONE, MODE_NONE, "carol who wins?"),
           "observer -> observer", "");

    send_request(&carol, MSG_CHAT, "dave hello other table", &o);
    report(o.count == 2 && entry_is(find_entry(&o, &dave, MSG_CHAT), &dave, RESP_NONE, MODE_NONE, "carol hello other table"),
           "observer -> player of another game", "");

    report(count_for(&o, &alice, MSG_CHAT) == 0 && count_for(&o, &bob, MSG_CHAT) == 0,
           "chat is private: the watched players see nothing", "");

    //------------------------------------------------------------
    printf("\n-- Ox\n");

    expect_only_reply(&carol, MSG_STOP_OBSERVE, "", RESP_SUCCESS, MODE_NONE, "", "Ox: one reply, nothing else");
    report(online_shows(&frank, "carol:L"), "carol is back in the lobby", "");
    expect_only_reply(&carol, MSG_STOP_OBSERVE, "", RESP_FAILURE, MODE_NONE, "not allowed now", "second Ox is refused");

    play(&g1, &o);
    report(count_for(&o, &carol, MSG_BOARD_UPDATE) == 0 && count_for(&o, &frank, MSG_BOARD_UPDATE) == 1,
           "after Ox: carol gets no Bu, frank still does", "");

    //Carol can watch again, and can watch the other game
    send_request(&carol, MSG_OBSERVE, id2, &o);
    report(o.count == 1 && o.entries[0].message.response == RESP_SUCCESS, "carol watches game 2 now", "");
    play(&g2, &o);
    report(count_for(&o, &carol, MSG_BOARD_UPDATE) == 1, "and receives game 2's Bu", "");
    play(&g1, &o);
    report(count_for(&o, &carol, MSG_BOARD_UPDATE) == 0, "but not game 1's", "");
    send_request(&carol, MSG_STOP_OBSERVE, "", &o);

    //Ox then challenge works
    send_request(&carol, MSG_CHALLENGE, "frank", &o);
    report(entry_is(find_entry(&o, &carol, MSG_CHALLENGE), &carol, RESP_FAILURE, MODE_NONE, "user busy"),
           "carol can challenge again (frank is busy watching)", "");

    //------------------------------------------------------------
    printf("\n-- Observer disconnects\n");

    leave(&frank, &o);
    describe(&o, details, sizeof(details));
    report(o.count == 0, "nobody is notified", details);

    play(&g1, &o);
    describe(&o, details, sizeof(details));
    report(o.count == 3, "next move: reply + 2 Bu only", details);

    //A new client gets frank's slot: it must never receive game 1's Bu
    Player newcomer = {"newcomer", 0, 0};
    login_player(&newcomer);
    report(newcomer.session == frank.session, "newcomer reuses the freed slot", "");
    play(&g1, &o);
    report(count_for(&o, &newcomer, MSG_BOARD_UPDATE) == 0, "the reused slot receives no Bu", "");

    //------------------------------------------------------------
    printf("\n-- Observer limit\n");

    static Player fans[MAX_OBSERVERS + 1];
    static char names[MAX_OBSERVERS + 1][16];
    int accepted = 0;

    for (int i = 0; i <= MAX_OBSERVERS; i++) {
        snprintf(names[i], sizeof(names[i]), "fan%d", i);
        fans[i].name = names[i];
        login_player(&fans[i]);
    }

    for (int i = 0; i < MAX_OBSERVERS; i++) {
        send_request(&fans[i], MSG_OBSERVE, id1, &o);

        if (o.count == 1 && o.entries[0].message.response == RESP_SUCCESS)
            accepted++;
    }

    snprintf(details, sizeof(details), "%d accepted", accepted);
    report(accepted == MAX_OBSERVERS, "MAX_OBSERVERS observers accepted", details);
    expect_only_reply(&fans[MAX_OBSERVERS], MSG_OBSERVE, id1, RESP_FAILURE, MODE_OBSERVER, "game full",
                      "one more: game full");
    send_request(&carol, MSG_OBSERVE, id2, &o);
    report(o.count == 1 && o.entries[0].message.response == RESP_SUCCESS, "another game is not affected", "");
    send_request(&carol, MSG_STOP_OBSERVE, "", &o);

    play(&g1, &o);
    describe(&o, details, sizeof(details));
    report(o.count == 3 + MAX_OBSERVERS, "a move reaches all of them", details);

    //Remove one in the middle (the last one is swapped into its slot)
    send_request(&fans[3], MSG_STOP_OBSERVE, "", &o);
    play(&g1, &o);

    int all_others = 1;

    for (int i = 0; i < MAX_OBSERVERS; i++) {
        int expected = i == 3 ? 0 : 1;

        if (count_for(&o, &fans[i], MSG_BOARD_UPDATE) != expected)
            all_others = 0;
    }

    report(all_others, "middle observer leaves: everyone else still served", "");

    send_request(&fans[MAX_OBSERVERS], MSG_OBSERVE, id1, &o);
    report(o.count == 1 && o.entries[0].message.response == RESP_SUCCESS, "the freed place can be taken", "");

    //------------------------------------------------------------
    printf("\n-- Game ends while watched\n");

    send_request(&carol, MSG_OBSERVE, id2, &o);
    Player *loser = g2.side[0];
    Player *winner = g2.side[1];

    send_request(loser, MSG_RESIGN, "", &o);
    describe(&o, details, sizeof(details));

    const OutboxEntry *go_c = find_entry(&o, &carol, MSG_GAME_OVER);
    const OutboxEntry *go_w = find_entry(&o, winner, MSG_GAME_OVER);
    char w_name[MAX_NAME + 1] = "", reason[16] = "";
    int gid = 0, c0, c1;

    report(entry_is(go_c, &carol, RESP_NONE, MODE_OBSERVER, NULL) &&
           entry_is(go_w, winner, RESP_NONE, MODE_PLAYER, NULL) &&
           strcmp(go_c->message.content, go_w->message.content) == 0,
           "resign: Go to the observer (O), same content", details);

    if (go_c != NULL)
        sscanf(go_c->message.content, "%d %d %d %32s %15s", &gid, &c0, &c1, w_name, reason);

    report(gid == g2.id && strcmp(w_name, winner->name) == 0 && strcmp(reason, "resign") == 0,
           "the observer's Go names the winner", go_c ? go_c->message.content : "");

    report(online_shows(&alice, "carol:L"), "the observer is back in the lobby", "");
    expect_only_reply(&carol, MSG_STOP_OBSERVE, "", RESP_FAILURE, MODE_NONE, "not allowed now", "Ox after the game ended");
    expect_only_reply(&carol, MSG_OBSERVE, id2, RESP_FAILURE, MODE_OBSERVER, "no such game", "O on a finished game");

    snprintf(text, sizeof(text), "%d:%s:%s:pub", g1.id, g1.side[0]->name, g1.side[1]->name);
    expect_only_reply(&carol, MSG_LIST_GAMES, "", RESP_SUCCESS, MODE_NONE, text, "LG drops the finished game");

    //A player leaving while watched (one fan makes room for carol)
    send_request(&fans[MAX_OBSERVERS], MSG_STOP_OBSERVE, "", &o);
    send_request(&carol, MSG_OBSERVE, id1, &o);
    report(o.count == 1 && o.entries[0].message.response == RESP_SUCCESS, "carol watches game 1", "");
    Player *leaver = g1.side[g1.board.current_player];
    Player *stayer = g1.side[1 - g1.board.current_player];

    leave(leaver, &o);
    describe(&o, details, sizeof(details));

    const OutboxEntry *left = find_entry(&o, &carol, MSG_GAME_OVER);
    report(entry_is(left, &carol, RESP_NONE, MODE_OBSERVER, NULL) &&
           strstr(left->message.content, " left") != NULL &&
           strstr(left->message.content, stayer->name) != NULL,
           "player leaves: observers get Go ... left", details);
    report(count_for(&o, leaver, MSG_GAME_OVER) == 0, "nothing for the leaver", "");
    report(count_for(&o, &fans[0], MSG_GAME_OVER) == 1 && count_for(&o, &fans[3], MSG_GAME_OVER) == 0,
           "every remaining observer is told, not the one who left", "");
    report(online_shows(&stayer[0], "fan0:L") && online_shows(&stayer[0], "carol:L"),
           "all observers back in the lobby", "");

    expect_only_reply(&carol, MSG_LIST_GAMES, "", RESP_SUCCESS, MODE_NONE, "", "no game left");

    //------------------------------------------------------------
    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}
