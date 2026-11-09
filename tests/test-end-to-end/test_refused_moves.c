//End-to-end: moves and messages refused during a game, and their effect (none).

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob;
    char text[16];

    printf("-- Refused moves inside a game\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0;
    e2e_report(ok, "alice and bob logged in", "");

    if (!ok || e2e_start_game(&alice, &bob) != 0) {
        e2e_report(0, "game starts", "");
        return e2e_summary();
    }

    e2e_report(1, "game starts", "");

    Client *first = e2e_to_move(&alice, &bob);
    Client *second = first == &alice ? &bob : &alice;

    //Moving before your turn
    snprintf(text, sizeof(text), "%d", second->side * 6);
    e2e_send(second, MSG_MOVE, text);
    e2e_report(e2e_reply_is(second, MSG_MOVE, RESP_FAILURE, "not your turn"), "move out of turn", text);

    //A hole of the other side
    e2e_send(first, MSG_MOVE, text);
    e2e_report(e2e_reply_is(first, MSG_MOVE, RESP_FAILURE, "illegal move"), "hole on the opponent's side", text);

    //Not a hole number
    e2e_send(first, MSG_MOVE, "3x");
    e2e_report(e2e_reply_is(first, MSG_MOVE, RESP_FAILURE, "bad format"), "not a number", "3x");

    e2e_send(first, MSG_MOVE, "12");
    e2e_report(e2e_reply_is(first, MSG_MOVE, RESP_FAILURE, "bad format"), "hole 12 does not exist", "");

    e2e_send(first, MSG_MOVE, "-1");
    e2e_report(e2e_reply_is(first, MSG_MOVE, RESP_FAILURE, "bad format"), "negative hole", "");

    e2e_send(first, MSG_MOVE, "");
    e2e_report(e2e_reply_is(first, MSG_MOVE, RESP_FAILURE, "bad format"), "empty move", "");

    //None of these changed anything
    e2e_report(alice.bu_count == 0 && bob.bu_count == 0 &&
               e2e_boards_equal(&alice.board, &bob.board) &&
               alice.board.current_player == first->side,
               "refused moves change nothing", "");

    //A legal move is still accepted after all that
    e2e_report(e2e_play_one_move(&alice, &bob) == 0, "legal move accepted", "");

    //Play on until the player to move has an empty hole, then try it
    int empty = -1;
    Client *mover = e2e_to_move(&alice, &bob);

    for (int tries = 0; tries < 40 && alice.in_game && empty == -1; tries++) {
        mover = e2e_to_move(&alice, &bob);

        for (int h = mover->side * 6; h < mover->side * 6 + 6; h++) {
            if (mover->board.holes[h] == 0) {
                empty = h;
                break;
            }
        }

        if (empty == -1) {
            e2e_play_one_move(&alice, &bob);
        }
    }

    if (empty != -1) {
        int bu_before = alice.bu_count;

        snprintf(text, sizeof(text), "%d", empty);
        e2e_send(mover, MSG_MOVE, text);
        e2e_report(e2e_reply_is(mover, MSG_MOVE, RESP_FAILURE, "illegal move"), "empty hole refused", text);
        e2e_report(alice.bu_count == bu_before, "no board update after it", "");
    } else {
        printf("SKIP  empty hole refused                         (no empty hole found)\n");
    }

    //Messages that make no sense during a game
    e2e_send(first, MSG_CHALLENGE, second->name);
    e2e_report(e2e_reply_is(first, MSG_CHALLENGE, RESP_FAILURE, "not allowed now"), "challenge from a game", "");

    e2e_send(first, MSG_ACCEPT, second->name);
    e2e_report(e2e_reply_is(first, MSG_ACCEPT, RESP_FAILURE, "not allowed now"), "accept from a game", "");

    e2e_send(first, MSG_LOGIN, first->name);
    e2e_report(e2e_reply_is(first, MSG_LOGIN, RESP_FAILURE, "not allowed now"), "login again from a game", "");

    e2e_send(first, MSG_REGISTER, "newname");
    e2e_report(e2e_reply_is(first, MSG_REGISTER, RESP_FAILURE, "not allowed now"), "register from a game", "");

    //The game is still going normally
    e2e_report(alice.in_game && bob.in_game && e2e_boards_equal(&alice.board, &bob.board),
               "the game is unaffected", "");

    return e2e_summary();
}
