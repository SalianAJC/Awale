//End-to-end: observers and players leaving while a game is watched.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob, carol, dave, erin, zoe;
    char details[512];
    char text[16];

    printf("-- Leaving while watched\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&carol, "carol") == 0 && e2e_join(&dave, "dave") == 0 &&
             e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "five players logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    e2e_report(e2e_start_game(&alice, &bob) == 0, "game alice-bob starts", "");
    snprintf(text, sizeof(text), "%d", alice.game_id);

    e2e_send(&carol, MSG_OBSERVE, text);
    e2e_send(&dave, MSG_OBSERVE, text);
    e2e_report(carol.observing && dave.observing, "carol and dave watch", "");

    //------------------------------------------------------------
    printf("\n   an observer's connection drops\n");

    e2e_leave(&carol);
    e2e_describe_outbox(details, sizeof(details));
    e2e_report(e2e_outbox.count == 0, "nobody is notified", details);

    int bu_d = dave.bu_count;
    e2e_report(e2e_play_one_move(&alice, &bob) == 0 && dave.bu_count == bu_d + 1,
               "the game and the other observer carry on", "");

    e2e_report(!e2e_online_shows(&erin, "carol:O") && !e2e_online_shows(&erin, "carol:L"),
               "carol is no longer listed", "");

    //------------------------------------------------------------
    printf("\n   a new client takes the freed slot\n");

    e2e_report(e2e_join(&zoe, "zoe") == 0, "zoe connects", "");
    snprintf(details, sizeof(details), "zoe session %d, carol was %d", zoe.session, carol.session);

    int bu_z = zoe.bu_count;
    e2e_play_one_move(&alice, &bob);
    e2e_report(zoe.bu_count == bu_z, "zoe receives none of carol's game", details);

    //------------------------------------------------------------
    printf("\n   the observer comes back\n");

    e2e_report(e2e_reconnect(&carol) == 0, "carol logs in again", "");
    e2e_report(!carol.observing && e2e_online_shows(&erin, "carol:L"), "she starts in the lobby", "");

    e2e_send(&carol, MSG_OBSERVE, text);
    e2e_report(carol.observing && e2e_boards_equal(&carol.board, &alice.board),
               "and can watch again with the current board", "");

    //------------------------------------------------------------
    printf("\n   a player leaves while watched\n");

    int go_c = carol.got_go, go_d = dave.got_go;

    e2e_leave(&bob);

    int id, c0, c1;
    char winner[MAX_NAME + 1] = "", reason[16] = "";
    e2e_parse_go(carol.last_go, &id, &c0, &c1, winner, reason);

    e2e_report(carol.got_go == go_c + 1 && dave.got_go == go_d + 1,
               "both observers get Go", carol.last_go);
    e2e_report(strcmp(winner, "alice") == 0 && strcmp(reason, "left") == 0,
               "winner alice, reason left", "");
    e2e_report(carol.last_go_mode == MODE_OBSERVER && alice.last_go_mode == MODE_PLAYER, "modes O and P", "");
    e2e_report(e2e_online_shows(&erin, "carol:L") && e2e_online_shows(&erin, "dave:L") &&
               e2e_online_shows(&erin, "alice:L"),
               "observers and winner back in the lobby", "");

    e2e_send(&erin, MSG_LIST_GAMES, "");
    e2e_report(e2e_reply_is(&erin, MSG_LIST_GAMES, RESP_SUCCESS, ""), "the game is gone from LG", "");

    //------------------------------------------------------------
    printf("\n   an observer leaves right after the game ended\n");

    e2e_report(e2e_reconnect(&bob) == 0 && e2e_start_game(&alice, &bob) == 0, "a new game", "");
    snprintf(text, sizeof(text), "%d", alice.game_id);
    e2e_send(&dave, MSG_OBSERVE, text);
    e2e_send(&alice, MSG_RESIGN, "");
    e2e_report(!dave.observing, "dave back in the lobby after Go", "");

    e2e_leave(&dave);
    e2e_describe_outbox(details, sizeof(details));
    e2e_report(e2e_outbox.count == 0, "leaving from the lobby notifies nobody", details);

    e2e_report(e2e_start_game(&alice, &bob) == 0, "the server still works", "");

    return e2e_summary();
}
