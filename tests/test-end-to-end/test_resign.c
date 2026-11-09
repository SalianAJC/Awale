//End-to-end: resigning in the middle of a game, by either player.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

//Plays a few moves, then `quitter` resigns. Checks the result
static void resign_game(Client *quitter, Client *other, Client *watcher, int moves_first)
{
    char details[512];
    char label[96];

    if (e2e_start_game(quitter, other) != 0) {
        e2e_report(0, "game starts", "");
        return;
    }

    for (int i = 0; i < moves_first && quitter->in_game; i++) {
        e2e_play_one_move(quitter, other);
    }

    int cap0 = quitter->board.captures[0];
    int cap1 = quitter->board.captures[1];
    int go_q = quitter->got_go;
    int go_o = other->got_go;

    e2e_send(quitter, MSG_RESIGN, "");
    e2e_describe_outbox(details, sizeof(details));

    snprintf(label, sizeof(label), "%s resigns after %d moves", quitter->name, moves_first);
    e2e_report(e2e_reply_is(quitter, MSG_RESIGN, RESP_SUCCESS, NULL), label, details);

    e2e_report(quitter->got_go == go_q + 1 && other->got_go == go_o + 1,
               "both players receive one Go", "");

    int id, c0, c1;
    char winner[MAX_NAME + 1] = "", reason[16] = "";
    int parsed = e2e_parse_go(other->last_go, &id, &c0, &c1, winner, reason);

    snprintf(details, sizeof(details), "\"%.200s\"", other->last_go);
    e2e_report(parsed && strcmp(winner, other->name) == 0 && strcmp(reason, "resign") == 0,
               "the opponent wins, reason resign", details);
    e2e_report(parsed && c0 == cap0 && c1 == cap1, "Go keeps the scores so far", details);

    char item_q[MAX_NAME + 3], item_o[MAX_NAME + 3];
    snprintf(item_q, sizeof(item_q), "%s:L", quitter->name);
    snprintf(item_o, sizeof(item_o), "%s:L", other->name);

    e2e_report(e2e_online_shows(watcher, item_q) && e2e_online_shows(watcher, item_o),
               "both back in the lobby", "");

    e2e_send(quitter, MSG_MOVE, "0");
    e2e_report(e2e_reply_is(quitter, MSG_MOVE, RESP_FAILURE, "not allowed now"), "no move after the game", "");

    e2e_send(quitter, MSG_RESIGN, "");
    e2e_report(e2e_reply_is(quitter, MSG_RESIGN, RESP_FAILURE, "not allowed now"), "no resign after the game", "");
}

int main(void)
{
    static Client alice, bob, erin;

    printf("-- Resignation\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "alice, bob and erin logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    printf("\n   resign before any move\n");
    resign_game(&alice, &bob, &erin, 0);

    printf("\n   resign mid-game\n");
    resign_game(&bob, &alice, &erin, 8);

    //Ratings after one loss each: back where they started (within rounding)
    int ra = e2e_rating_of(&erin, "alice");
    int rb = e2e_rating_of(&erin, "bob");
    char details[96];

    snprintf(details, sizeof(details), "alice %d, bob %d", ra, rb);
    e2e_report(ra + rb >= 2 * START_RATING - 1 && ra + rb <= 2 * START_RATING + 1,
               "rating points conserved over both games", details);

    return e2e_summary();
}
