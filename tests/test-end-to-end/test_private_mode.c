//End-to-end: private mode with friends, as a demo would show it.

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob, carol, dave, erin;
    char text[16];

    printf("-- Private mode with friends\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&carol, "carol") == 0 && e2e_join(&dave, "dave") == 0 &&
             e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "five players logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    //------------------------------------------------------------
    printf("\n   alice goes private before playing\n");

    e2e_send(&alice, MSG_PRIVATE, "on");
    e2e_report(e2e_reply_is(&alice, MSG_PRIVATE, RESP_SUCCESS, "on"), "alice: Pv on", "");

    e2e_send(&alice, MSG_FRIEND_ADD, "carol");
    e2e_report(e2e_reply_is(&alice, MSG_FRIEND_ADD, RESP_SUCCESS, "carol"), "alice and carol are friends", "");

    e2e_report(e2e_start_game(&alice, &bob) == 0, "alice plays bob", "");
    snprintf(text, sizeof(text), "%d", alice.game_id);

    for (int i = 0; i < 6 && alice.in_game; i++) {
        e2e_play_one_move(&alice, &bob);
    }

    //------------------------------------------------------------
    printf("\n   who can watch\n");

    e2e_send(&dave, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(&dave, MSG_OBSERVE, RESP_FAILURE, "game is private") && !dave.observing,
               "dave (stranger) is refused", "");

    e2e_send(&carol, MSG_OBSERVE, text);
    e2e_report(carol.observing && e2e_boards_equal(&carol.board, &alice.board),
               "carol (friend) watches with the right board", "");

    //Dave asks alice by chat, alice adds him, dave tries again
    e2e_send(&dave, MSG_CHAT, "alice can I watch your game?");
    e2e_report(strcmp(alice.last_chat, "dave can I watch your game?") == 0,
               "dave asks alice by chat", alice.last_chat);

    e2e_send(&alice, MSG_FRIEND_ADD, "dave");
    e2e_send(&alice, MSG_CHAT, "dave sure, added you");
    e2e_report(strcmp(dave.last_chat, "alice sure, added you") == 0, "alice adds dave from inside the game", "");

    e2e_send(&dave, MSG_OBSERVE, text);
    e2e_report(dave.observing && e2e_boards_equal(&dave.board, &alice.board), "now dave can watch", "");

    //------------------------------------------------------------
    printf("\n   bob goes private too\n");

    e2e_send(&bob, MSG_PRIVATE, "on");
    e2e_send(&erin, MSG_FRIEND_ADD, "bob");
    e2e_send(&erin, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(&erin, MSG_OBSERVE, RESP_FAILURE, "game is private"),
               "erin, friend of bob only, is refused", "");

    e2e_send(&erin, MSG_FRIEND_ADD, "alice");
    e2e_send(&erin, MSG_OBSERVE, text);
    e2e_report(erin.observing, "friend of both: erin can watch", "");

    //------------------------------------------------------------
    printf("\n   the game goes on under watch\n");

    int bu_c = carol.bu_count, bu_d = dave.bu_count, bu_e = erin.bu_count;

    for (int i = 0; i < 5 && alice.in_game; i++) {
        e2e_play_one_move(&alice, &bob);
    }

    e2e_report(carol.bu_count == bu_c + 5 && dave.bu_count == bu_d + 5 && erin.bu_count == bu_e + 5,
               "all three observers get the 5 moves", "");
    e2e_report(e2e_boards_equal(&carol.board, &bob.board) && e2e_boards_equal(&dave.board, &bob.board) &&
               e2e_boards_equal(&erin.board, &bob.board),
               "all boards agree", "");

    //Observers chat among themselves
    e2e_send(&carol, MSG_CHAT, "dave alice is winning");
    e2e_report(strcmp(dave.last_chat, "carol alice is winning") == 0, "observers chat together", "");

    //------------------------------------------------------------
    printf("\n   unfriending\n");

    e2e_send(&dave, MSG_STOP_OBSERVE, "");
    e2e_send(&alice, MSG_FRIEND_REMOVE, "dave");
    e2e_send(&dave, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(&dave, MSG_OBSERVE, RESP_FAILURE, "game is private"),
               "removed friend cannot come back", "");

    //------------------------------------------------------------
    printf("\n   end of the game\n");

    e2e_send(&bob, MSG_RESIGN, "");
    e2e_report(carol.got_go == 1 && erin.got_go == 1 && dave.got_go == 0,
               "only the current observers get Go", "");
    e2e_report(!carol.observing && !erin.observing, "they are back in the lobby", "");

    //------------------------------------------------------------
    printf("\n   private mode off\n");

    e2e_send(&alice, MSG_PRIVATE, "off");
    e2e_send(&bob, MSG_PRIVATE, "off");
    e2e_report(e2e_start_game(&alice, &bob) == 0, "a new game", "");
    snprintf(text, sizeof(text), "%d", alice.game_id);

    e2e_send(&dave, MSG_OBSERVE, text);
    e2e_report(dave.observing, "public again: dave can watch", "");

    return e2e_summary();
}
