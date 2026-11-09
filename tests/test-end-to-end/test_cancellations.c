//End-to-end: what happens to pending challenges when a game starts.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob, carol, dave, erin;

    printf("-- Pending challenges when a game starts\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&carol, "carol") == 0 && e2e_join(&dave, "dave") == 0 &&
             e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "five players logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    //Three challenges pending at once:
    //carol -> alice, erin -> bob, and dave -> alice (the one that will be accepted)
    e2e_send(&carol, MSG_CHALLENGE, "alice");
    e2e_report(e2e_reply_is(&carol, MSG_CHALLENGE, RESP_SUCCESS, NULL), "carol challenges alice", "");

    e2e_send(&erin, MSG_CHALLENGE, "bob");
    e2e_report(e2e_reply_is(&erin, MSG_CHALLENGE, RESP_SUCCESS, NULL), "erin challenges bob", "");

    e2e_send(&dave, MSG_CHALLENGE, "alice");
    e2e_report(e2e_reply_is(&dave, MSG_CHALLENGE, RESP_SUCCESS, NULL), "dave challenges alice too", "");

    //Alice accepts dave
    int rf_c = carol.rf_count, rf_e = erin.rf_count, rf_d = dave.rf_count;

    e2e_send(&alice, MSG_ACCEPT, "dave");
    e2e_report(alice.in_game && dave.in_game && alice.game_id == dave.game_id,
               "alice accepts dave: game starts", "");

    e2e_report(carol.rf_count == rf_c + 1 && strcmp(carol.last_rf, "alice") == 0,
               "carol's challenge to alice is cancelled", carol.last_rf);
    e2e_report(dave.rf_count == rf_d, "dave gets no refusal for his own game", "");
    e2e_report(erin.rf_count == rf_e, "erin's challenge to bob is untouched", "");

    //Alice cannot accept carol any more
    e2e_send(&alice, MSG_ACCEPT, "carol");
    e2e_report(e2e_reply_is(&alice, MSG_ACCEPT, RESP_FAILURE, "not allowed now"), "alice cannot accept carol now", "");

    //Carol is free to challenge someone else
    e2e_send(&carol, MSG_CHALLENGE, "erin");
    e2e_report(e2e_reply_is(&carol, MSG_CHALLENGE, RESP_FAILURE, "challenge already pending") == 0,
               "carol's outgoing challenge was cleared", "");

    //Bob accepts erin: a second game, and carol's challenge to erin is cancelled
    rf_c = carol.rf_count;

    e2e_send(&bob, MSG_ACCEPT, "erin");
    e2e_report(bob.in_game && erin.in_game, "bob accepts erin: second game starts", "");
    e2e_report(carol.rf_count == rf_c + 1 && strcmp(carol.last_rf, "erin") == 0,
               "carol's challenge to erin is cancelled", carol.last_rf);

    //Refusing still works normally afterwards
    e2e_send(&alice, MSG_RESIGN, "");

    e2e_send(&carol, MSG_CHALLENGE, "dave");
    rf_c = carol.rf_count;
    e2e_send(&dave, MSG_REFUSE, "carol");
    e2e_report(e2e_reply_is(&dave, MSG_REFUSE, RESP_SUCCESS, "carol") &&
               carol.rf_count == rf_c + 1 && strcmp(carol.last_rf, "dave") == 0,
               "a plain refusal reaches the challenger", carol.last_rf);

    return e2e_summary();
}
