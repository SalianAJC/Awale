//End-to-end: a whole game watched to the end by observers who join at different times.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

#define MOVE_LIMIT 400

int main(void)
{
    static Client alice, bob, carol, dave, erin, frank;
    char details[512];
    char text[16];

    printf("-- Watching a full game\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&carol, "carol") == 0 && e2e_join(&dave, "dave") == 0 &&
             e2e_join(&erin, "erin") == 0 && e2e_join(&frank, "frank") == 0;
    e2e_report(ok, "six players logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    int rating_c = e2e_rating_of(&frank, "carol");

    e2e_report(e2e_start_game(&alice, &bob) == 0, "game alice-bob starts", "");
    snprintf(text, sizeof(text), "%d", alice.game_id);

    //Carol watches from the very first move
    e2e_send(&carol, MSG_OBSERVE, text);
    e2e_report(carol.observing && e2e_boards_equal(&carol.board, &alice.board),
               "carol watches from the start (initial board)", "");

    int moves = 0;
    int out_of_sync = 0;
    int dave_joined_at = -1;
    int erin_rejoins = 0;
    int erin_resync_ok = 1;
    int bu_before_c = carol.bu_count;

    while (alice.in_game && moves < MOVE_LIMIT) {
        if (e2e_play_one_move(&alice, &bob) != 0) {
            break;
        }

        moves++;

        //Dave arrives after 10 moves
        if (moves == 10) {
            e2e_send(&dave, MSG_OBSERVE, text);
            dave_joined_at = moves;

            if (!dave.observing || !e2e_boards_equal(&dave.board, &alice.board)) {
                out_of_sync = 1;
            }
        }

        //Erin keeps leaving and coming back: each time the move list must rebuild the board
        if (moves % 7 == 3 && alice.in_game) {
            if (erin.observing) {
                e2e_send(&erin, MSG_STOP_OBSERVE, "");
            }
            else {
                e2e_send(&erin, MSG_OBSERVE, text);
                erin_rejoins++;

                if (!erin.observing || !e2e_boards_equal(&erin.board, &alice.board)) {
                    erin_resync_ok = 0;
                }
            }
        }

        if (alice.in_game &&
            (!e2e_boards_equal(&carol.board, &alice.board) ||
             (dave.observing && !e2e_boards_equal(&dave.board, &alice.board)) ||
             (erin.observing && !e2e_boards_equal(&erin.board, &alice.board)))) {
            out_of_sync = 1;
        }
    }

    snprintf(details, sizeof(details), "%d moves", moves);
    e2e_report(!alice.in_game && !bob.in_game, "the game reached its end", details);

    snprintf(details, sizeof(details), "carol applied %d Bu for %d moves", carol.bu_count - bu_before_c, moves);
    e2e_report(carol.bu_count - bu_before_c == moves, "carol saw every move", details);
    e2e_report(!out_of_sync, "observer boards in sync after every move", "");
    e2e_report(dave_joined_at == 10, "dave joined mid-game with the right board", "");

    snprintf(details, sizeof(details), "%d rejoins", erin_rejoins);
    e2e_report(erin_rejoins >= 2 && erin_resync_ok, "erin rejoins several times, board rebuilt each time", details);

    e2e_report(carol.board_errors + dave.board_errors + erin.board_errors == 0,
               "no Bu was illegal on an observer's board", "");

    //Everyone gets the same Go
    e2e_report(carol.got_go == 1 && dave.got_go == 1 &&
               strcmp(carol.last_go, alice.last_go) == 0 && strcmp(dave.last_go, alice.last_go) == 0,
               "observers get the same Go as the players", carol.last_go);
    e2e_report(carol.last_go_mode == MODE_OBSERVER && alice.last_go_mode == MODE_PLAYER,
               "Go mode: O for observers, P for players", "");

    int id, c0, c1;
    char winner[MAX_NAME + 1] = "", reason[16] = "";

    e2e_parse_go(carol.last_go, &id, &c0, &c1, winner, reason);
    e2e_report(c0 == carol.board.captures[0] && c1 == carol.board.captures[1],
               "the Go scores match the observer's own board", "");
    e2e_report(strcmp(reason, "end") == 0, "reason: end", reason);

    e2e_report(!carol.observing && !dave.observing && !erin.observing, "nobody is observing any more", "");
    e2e_report(e2e_online_shows(&frank, "carol:L") && e2e_online_shows(&frank, "dave:L") &&
               e2e_online_shows(&frank, "erin:L"),
               "all observers back in the lobby", "");

    e2e_report(e2e_rating_of(&frank, "carol") == rating_c, "watching does not change ratings", "");

    //The observers can now play each other
    e2e_report(e2e_start_game(&carol, &dave) == 0, "two former observers start their own game", "");

    snprintf(text, sizeof(text), "%d", carol.game_id);
    e2e_send(&alice, MSG_OBSERVE, text);
    e2e_report(alice.observing, "and a former player watches them", "");

    return e2e_summary();
}
