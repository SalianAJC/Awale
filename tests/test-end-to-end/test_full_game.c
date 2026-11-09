//End-to-end: a whole game played to its end by the rules.
//Checks every move, both client boards, the game over, the winner and the ratings.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob, erin;
    char details[512];

    printf("-- Full game played to the end\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "alice, bob and erin logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    int rating_a = e2e_rating_of(&erin, "alice");
    int rating_b = e2e_rating_of(&erin, "bob");

    ok = e2e_start_game(&alice, &bob) == 0;
    e2e_report(ok, "game starts, one player per side", "");

    if (!ok) {
        return e2e_summary();
    }

    int moves = 0;
    int refused = 0;
    int bu_ok = 1;
    int sync_ok = 1;

    while (alice.in_game && bob.in_game && moves < MAX_MOVES + 5) {
        int bu_a = alice.bu_count;
        int bu_b = bob.bu_count;

        if (e2e_play_one_move(&alice, &bob) != 0) {
            refused++;
            break;
        }

        moves++;

        if (alice.bu_count != bu_a + 1 || bob.bu_count != bu_b + 1) {
            bu_ok = 0;
        }

        if (!e2e_boards_equal(&alice.board, &bob.board)) {
            sync_ok = 0;
        }
    }

    snprintf(details, sizeof(details), "%d moves", moves);
    e2e_report(refused == 0, "every legal move accepted", details);
    e2e_report(bu_ok, "each move: one Bu to each player", "");
    e2e_report(sync_ok && alice.board_errors == 0 && bob.board_errors == 0,
               "both client boards stay identical", "");

    e2e_report(alice.got_go == 1 && bob.got_go == 1, "both players receive exactly one Go", "");
    e2e_report(strcmp(alice.last_go, bob.last_go) == 0, "same Go content for both", alice.last_go);

    int id, cap0, cap1;
    char winner[MAX_NAME + 1] = "", reason[16] = "";
    int parsed = e2e_parse_go(alice.last_go, &id, &cap0, &cap1, winner, reason);

    e2e_report(parsed && strcmp(reason, "end") == 0, "reason is end", alice.last_go);
    e2e_report(parsed && id == alice.game_id, "Go carries the game id", "");
    e2e_report(parsed && cap0 == alice.board.captures[0] && cap1 == alice.board.captures[1],
               "Go scores match the client board", "");
    e2e_report(cap0 + cap1 <= 48, "no seed created: total captures <= 48", "");

    const char *side0 = alice.side == 0 ? "alice" : "bob";
    const char *side1 = alice.side == 1 ? "alice" : "bob";
    const char *expected = cap0 > cap1 ? side0 : cap1 > cap0 ? side1 : "draw";

    snprintf(details, sizeof(details), "expected %s, got %s (%d-%d)", expected, winner, cap0, cap1);
    e2e_report(strcmp(winner, expected) == 0, "winner matches the scores", details);

    e2e_report(e2e_online_shows(&erin, "alice:L") && e2e_online_shows(&erin, "bob:L"),
               "both players back in the lobby", "");

    int new_a = e2e_rating_of(&erin, "alice");
    int new_b = e2e_rating_of(&erin, "bob");
    int sum_change = (new_a + new_b) - (rating_a + rating_b);

    snprintf(details, sizeof(details), "alice %d->%d, bob %d->%d", rating_a, new_a, rating_b, new_b);

    if (strcmp(expected, "draw") == 0) {
        e2e_report(new_a == rating_a && new_b == rating_b, "draw between equals: no change", details);
    } else {
        int winner_old = strcmp(expected, "alice") == 0 ? rating_a : rating_b;
        int winner_new = strcmp(expected, "alice") == 0 ? new_a : new_b;

        e2e_report(winner_new > winner_old, "winner's rating goes up", details);
    }

    e2e_report(sum_change >= -1 && sum_change <= 1, "rating points are conserved", details);

    //A rematch works straight away
    e2e_report(e2e_start_game(&bob, &alice) == 0, "rematch starts", "");
    e2e_report(bob.game_id == id + 1, "rematch gets the next game id", "");

    return e2e_summary();
}
