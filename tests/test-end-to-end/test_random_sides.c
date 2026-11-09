//End-to-end: the server picks who starts at random, and side 0 always moves first.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

#define GAMES 30

int main(void)
{
    static Client alice, bob;
    char details[128];

    printf("-- Who starts is random\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0;
    e2e_report(ok, "alice and bob logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    int alice_side0 = 0;
    int side0_moves_first = 1;
    int played = 0;

    for (int i = 0; i < GAMES; i++) {
        if (e2e_start_game(&alice, &bob) != 0) {
            break;
        }

        played++;

        if (alice.side == 0) {
            alice_side0++;
        }

        //The side-0 player is the one allowed to move first
        Client *side1 = alice.side == 1 ? &alice : &bob;
        char text[16];

        snprintf(text, sizeof(text), "%d", side1->side * 6);
        e2e_send(side1, MSG_MOVE, text);

        if (!e2e_reply_is(side1, MSG_MOVE, RESP_FAILURE, "not your turn")) {
            side0_moves_first = 0;
        }

        e2e_send(&alice, MSG_RESIGN, "");
    }

    snprintf(details, sizeof(details), "%d games played", played);
    e2e_report(played == GAMES, "all games started", details);

    snprintf(details, sizeof(details), "alice was side 0 in %d of %d games", alice_side0, played);
    e2e_report(alice_side0 > 0 && alice_side0 < played, "both players get to start", details);

    e2e_report(side0_moves_first, "side 0 always moves first", "");

    return e2e_summary();
}
