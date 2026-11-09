//End-to-end: players leaving, in a game, in the lobby, and before logging in.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob, carol, erin, ghost;
    char details[512];

    printf("-- Disconnections\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&carol, "carol") == 0 && e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "four players logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    //------------------------------------------------------------
    printf("\n   a player leaves mid-game\n");

    e2e_report(e2e_start_game(&alice, &bob) == 0, "game starts", "");

    for (int i = 0; i < 6 && alice.in_game; i++) {
        e2e_play_one_move(&alice, &bob);
    }

    int rating_b = e2e_rating_of(&erin, "bob");
    int go_a = alice.got_go;

    e2e_leave(&bob);
    e2e_describe_outbox(details, sizeof(details));

    e2e_report(alice.got_go == go_a + 1 && e2e_outbox.count == 1,
               "only the remaining player receives Go", details);

    int id, c0, c1;
    char winner[MAX_NAME + 1] = "", reason[16] = "";
    int parsed = e2e_parse_go(alice.last_go, &id, &c0, &c1, winner, reason);

    snprintf(details, sizeof(details), "\"%.200s\"", alice.last_go);
    e2e_report(parsed && strcmp(winner, "alice") == 0 && strcmp(reason, "left") == 0,
               "remaining player wins, reason left", details);

    e2e_report(e2e_online_shows(&erin, "alice:L") &&
               !e2e_online_shows(&erin, "bob:L") && !e2e_online_shows(&erin, "bob:G"),
               "leaver gone, winner back in the lobby", "");

    int new_b = e2e_rating_of(&erin, "bob");
    snprintf(details, sizeof(details), "bob %d->%d", rating_b, new_b);
    e2e_report(new_b < rating_b, "leaving counts as a loss", details);

    e2e_send(&alice, MSG_CHALLENGE, "bob");
    e2e_report(e2e_reply_is(&alice, MSG_CHALLENGE, RESP_FAILURE, "user offline"), "the leaver is offline", "");

    //------------------------------------------------------------
    printf("\n   the leaver comes back\n");

    e2e_report(e2e_reconnect(&bob) == 0, "bob logs in again from a new client", "");
    e2e_report(e2e_online_shows(&erin, "bob:L"), "bob is listed again", "");
    e2e_report(e2e_start_game(&alice, &bob) == 0, "and can play a new game", "");
    e2e_send(&alice, MSG_RESIGN, "");

    //------------------------------------------------------------
    printf("\n   a player leaves with challenges pending\n");

    e2e_send(&carol, MSG_CHALLENGE, "alice");      //carol challenges alice
    int rf_c = carol.rf_count;

    e2e_leave(&alice);

    e2e_report(carol.rf_count == rf_c + 1 && strcmp(carol.last_rf, "alice") == 0,
               "the challenger is told (Rf from the leaver)", carol.last_rf);

    e2e_send(&carol, MSG_CHALLENGE, "erin");
    e2e_report(e2e_reply_is(&carol, MSG_CHALLENGE, RESP_SUCCESS, "erin"),
               "the challenger is free again", "");

    //The challenged one leaves: the challenge simply disappears
    e2e_leave(&carol);
    e2e_send(&erin, MSG_ACCEPT, "carol");
    e2e_report(e2e_reply_is(&erin, MSG_ACCEPT, RESP_FAILURE, "no challenge from this user"),
               "a challenge from someone who left cannot be accepted", "");

    //------------------------------------------------------------
    printf("\n   a client leaves before logging in\n");

    memset(&ghost, 0, sizeof(ghost));
    ghost.name = "ghost";

    unsigned int g;
    int ghost_session = logic_connect(&g);
    ghost.session = ghost_session;
    ghost.generation = g;

    memset(&e2e_outbox, 0, sizeof(e2e_outbox));
    logic_disconnect(ghost_session, &e2e_outbox);
    e2e_describe_outbox(details, sizeof(details));

    e2e_report(e2e_outbox.count == 0, "nobody is notified", details);
    e2e_report(e2e_online_shows(&erin, "erin:L"), "the others are unaffected", "");

    return e2e_summary();
}
