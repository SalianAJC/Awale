//End-to-end: two games at the same time, kept separate, with chat across them.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob, carol, dave, erin;
    char details[512];

    printf("-- Two games at the same time\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&carol, "carol") == 0 && e2e_join(&dave, "dave") == 0 &&
             e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "five players logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    e2e_report(e2e_start_game(&alice, &bob) == 0, "game alice-bob starts", "");
    e2e_report(e2e_start_game(&carol, &dave) == 0, "game carol-dave starts", "");
    e2e_report(alice.game_id != carol.game_id, "different game ids", "");

    e2e_report(e2e_online_shows(&erin, "alice:G") && e2e_online_shows(&erin, "bob:G") &&
               e2e_online_shows(&erin, "carol:G") && e2e_online_shows(&erin, "dave:G") &&
               e2e_online_shows(&erin, "erin:L"),
               "the list shows who is playing", "");

    //Interleave moves of both games
    int leaked = 0;
    int moves = 0;

    for (int i = 0; i < 20 && alice.in_game && carol.in_game; i++) {
        int bu_c = carol.bu_count, bu_d = dave.bu_count;

        e2e_play_one_move(&alice, &bob);

        if (carol.bu_count != bu_c || dave.bu_count != bu_d) {
            leaked = 1;
        }

        int bu_a = alice.bu_count, bu_b = bob.bu_count;

        e2e_play_one_move(&carol, &dave);

        if (alice.bu_count != bu_a || bob.bu_count != bu_b) {
            leaked = 1;
        }

        moves += 2;
    }

    snprintf(details, sizeof(details), "%d moves interleaved", moves);
    e2e_report(!leaked, "moves never reach the other game", details);
    e2e_report(e2e_boards_equal(&alice.board, &bob.board) &&
               e2e_boards_equal(&carol.board, &dave.board) &&
               alice.board_errors + bob.board_errors + carol.board_errors + dave.board_errors == 0,
               "each pair of boards stays in sync", "");
    e2e_report(!e2e_boards_equal(&alice.board, &carol.board), "the two boards are different", "");

    //Chat with the opponent, with the other table, and from the lobby
    e2e_send(&alice, MSG_CHAT, "bob good luck");
    e2e_report(e2e_reply_is(&alice, MSG_CHAT, RESP_SUCCESS, "bob") &&
               strcmp(bob.last_chat, "alice good luck") == 0,
               "chat to the opponent during a game", bob.last_chat);

    e2e_send(&dave, MSG_CHAT, "alice hi from the other table");
    snprintf(details, sizeof(details), "\"%.200s\"", alice.last_chat);
    e2e_report(strcmp(alice.last_chat, "dave hi from the other table") == 0,
               "chat between two games", details);

    e2e_send(&erin, MSG_CHAT, "carol are you winning?");
    snprintf(details, sizeof(details), "\"%.200s\"", carol.last_chat);
    e2e_report(strcmp(carol.last_chat, "erin are you winning?") == 0,
               "chat from the lobby to a player in a game", details);

    //Chat does not disturb the games
    e2e_report(alice.in_game && carol.in_game, "games continue after chatting", "");

    //Nobody can challenge a busy player
    e2e_send(&erin, MSG_CHALLENGE, "alice");
    e2e_report(e2e_reply_is(&erin, MSG_CHALLENGE, RESP_FAILURE, "user busy"), "challenge a player in a game", "");

    //Ending one game leaves the other running
    e2e_send(&bob, MSG_RESIGN, "");
    e2e_report(!alice.in_game && !bob.in_game, "game alice-bob ended", "");
    e2e_report(carol.in_game && dave.in_game && carol.got_go == 0 && dave.got_go == 0,
               "game carol-dave still running, no Go", "");

    int bu_c = carol.bu_count;
    e2e_report(e2e_play_one_move(&carol, &dave) == 0 && carol.bu_count == bu_c + 1,
               "carol-dave can keep playing", "");

    //The free players can start a third game while the second runs
    e2e_report(e2e_start_game(&alice, &erin) == 0, "alice starts a new game with erin", "");
    e2e_report(alice.game_id != carol.game_id && alice.game_id > carol.game_id,
               "it gets a new id", "");

    return e2e_summary();
}
