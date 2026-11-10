//End-to-end: watching games (LG, O, Ox) and private mode (Pv).

#include <stdio.h>
#include <string.h>

#include "e2e.h"

int main(void)
{
    static Client alice, bob, carol, dave, erin;
    static Client crowd[MAX_OBSERVERS];
    static char crowd_names[MAX_OBSERVERS][16];
    char details[512];
    char text[64];

    printf("-- Observing games and private mode\n");

    e2e_report(e2e_init() == 0, "logic_init", "");

    int ok = e2e_join(&alice, "alice") == 0 && e2e_join(&bob, "bob") == 0 &&
             e2e_join(&carol, "carol") == 0 && e2e_join(&dave, "dave") == 0 &&
             e2e_join(&erin, "erin") == 0;
    e2e_report(ok, "five players logged in", "");

    if (!ok) {
        return e2e_summary();
    }

    //------------------------------------------------------------
    printf("\n   listing games\n");

    e2e_send(&carol, MSG_LIST_GAMES, "");
    e2e_report(e2e_reply_is(&carol, MSG_LIST_GAMES, RESP_SUCCESS, ""), "no games: empty list", "");

    e2e_report(e2e_start_game(&alice, &bob) == 0, "game alice-bob starts", "");

    for (int i = 0; i < 4 && alice.in_game; i++) {
        e2e_play_one_move(&alice, &bob);
    }

    Client *s0 = alice.side == 0 ? &alice : &bob;
    Client *s1 = alice.side == 0 ? &bob : &alice;
    char expected[128];

    snprintf(expected, sizeof(expected), "%d:%s:%s:pub", alice.game_id, s0->name, s1->name);
    e2e_send(&carol, MSG_LIST_GAMES, "");
    const Message *r = e2e_reply(&carol, MSG_LIST_GAMES);
    e2e_report(e2e_reply_is(&carol, MSG_LIST_GAMES, RESP_SUCCESS, expected),
               "the game is listed as public", r ? r->content : "no reply");

    //------------------------------------------------------------
    printf("\n   bad requests\n");

    e2e_send(&carol, MSG_OBSERVE, "abc");
    e2e_report(e2e_reply_is(&carol, MSG_OBSERVE, RESP_FAILURE, "bad format"), "O abc: bad format", "");

    e2e_send(&carol, MSG_OBSERVE, "999");
    e2e_report(e2e_reply_is(&carol, MSG_OBSERVE, RESP_FAILURE, "no such game"), "O 999: no such game", "");

    e2e_send(&alice, MSG_OBSERVE, "1");
    e2e_report(e2e_reply_is(&alice, MSG_OBSERVE, RESP_FAILURE, "not allowed now"),
               "a player cannot observe during a game", "");

    e2e_send(&carol, MSG_STOP_OBSERVE, "");
    e2e_report(e2e_reply_is(&carol, MSG_STOP_OBSERVE, RESP_FAILURE, "not allowed now"),
               "Ox without watching: not allowed now", "");

    //------------------------------------------------------------
    printf("\n   watching a public game\n");

    snprintf(text, sizeof(text), "%d", alice.game_id);
    e2e_send(&carol, MSG_OBSERVE, text);
    r = e2e_reply(&carol, MSG_OBSERVE);

    e2e_report(r && r->response == RESP_SUCCESS && r->mode == MODE_OBSERVER && carol.observing,
               "carol watches: O|S with mode O", r ? r->content : "no reply");
    e2e_report(e2e_boards_equal(&carol.board, &alice.board) && carol.board_errors == 0,
               "replayed moves give the players' board", "");
    e2e_report(e2e_online_shows(&erin, "carol:O"), "LO shows carol:O", "");

    int bu = carol.bu_count;
    e2e_play_one_move(&alice, &bob);
    e2e_report(carol.bu_count == bu + 1 && carol.last_bu_mode == MODE_OBSERVER,
               "the observer receives Bu with mode O", "");
    e2e_report(e2e_boards_equal(&carol.board, &alice.board), "observer's board stays in sync", "");

    e2e_send(&carol, MSG_MOVE, "3");
    e2e_report(e2e_reply_is(&carol, MSG_MOVE, RESP_FAILURE, "not allowed now"), "observer cannot move", "");

    e2e_send(&carol, MSG_CHALLENGE, "dave");
    e2e_report(e2e_reply_is(&carol, MSG_CHALLENGE, RESP_FAILURE, "not allowed now"),
               "observer cannot challenge", "");

    e2e_send(&carol, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(&carol, MSG_OBSERVE, RESP_FAILURE, "not allowed now"),
               "observer cannot observe twice", "");

    e2e_send(&carol, MSG_CHAT, "alice nice move");
    e2e_report(e2e_reply_is(&carol, MSG_CHAT, RESP_SUCCESS, "alice") &&
               strcmp(alice.last_chat, "carol nice move") == 0,
               "observer can chat", alice.last_chat);

    e2e_send(&erin, MSG_CHALLENGE, "carol");
    e2e_report(e2e_reply_is(&erin, MSG_CHALLENGE, RESP_FAILURE, "user busy"),
               "an observer cannot be challenged", "");

    //------------------------------------------------------------
    printf("\n   stop watching\n");

    e2e_send(&carol, MSG_STOP_OBSERVE, "");
    e2e_report(e2e_reply_is(&carol, MSG_STOP_OBSERVE, RESP_SUCCESS, NULL) && !carol.observing,
               "Ox: back to the lobby", "");

    bu = carol.bu_count;
    e2e_play_one_move(&alice, &bob);
    e2e_report(carol.bu_count == bu, "no Bu after Ox", "");
    e2e_report(e2e_online_shows(&erin, "carol:L"), "LO shows carol:L again", "");

    //------------------------------------------------------------
    printf("\n   private mode\n");

    e2e_send(&alice, MSG_PRIVATE, "maybe");
    e2e_report(e2e_reply_is(&alice, MSG_PRIVATE, RESP_FAILURE, "bad format"), "Pv maybe: bad format", "");

    e2e_send(&alice, MSG_PRIVATE, "on");
    e2e_report(e2e_reply_is(&alice, MSG_PRIVATE, RESP_SUCCESS, "on"), "alice turns private mode on (in game)", "");

    snprintf(expected, sizeof(expected), "%d:%s:%s:priv", alice.game_id, s0->name, s1->name);
    e2e_send(&erin, MSG_LIST_GAMES, "");
    e2e_report(e2e_reply_is(&erin, MSG_LIST_GAMES, RESP_SUCCESS, expected), "the game is listed as private", "");

    e2e_send(&erin, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(&erin, MSG_OBSERVE, RESP_FAILURE, "game is private") && !erin.observing,
               "a stranger is refused", "");

    e2e_send(&alice, MSG_FRIEND_ADD, "dave");
    e2e_report(e2e_reply_is(&alice, MSG_FRIEND_ADD, RESP_SUCCESS, NULL), "alice and dave become friends", "");

    e2e_send(&dave, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(&dave, MSG_OBSERVE, RESP_SUCCESS, NULL) && dave.observing &&
               e2e_boards_equal(&dave.board, &alice.board),
               "a friend of alice can watch", "");

    e2e_send(&bob, MSG_PRIVATE, "on");
    e2e_send(&carol, MSG_FRIEND_ADD, "alice");
    e2e_send(&carol, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(&carol, MSG_OBSERVE, RESP_FAILURE, "game is private"),
               "both private: a friend of only one is refused", "");

    e2e_send(&alice, MSG_PRIVATE, "off");
    e2e_send(&bob, MSG_PRIVATE, "off");
    e2e_report(e2e_reply_is(&bob, MSG_PRIVATE, RESP_SUCCESS, "off"), "private mode off again", "");

    //------------------------------------------------------------
    printf("\n   observer limit\n");

    int joined = 1;    //dave is already watching

    for (int i = 0; i < MAX_OBSERVERS; i++) {
        snprintf(crowd_names[i], sizeof(crowd_names[i]), "fan%d", i);

        if (e2e_join(&crowd[i], crowd_names[i]) != 0) {
            break;
        }

        e2e_send(&crowd[i], MSG_OBSERVE, text);

        if (e2e_reply_is(&crowd[i], MSG_OBSERVE, RESP_SUCCESS, NULL)) {
            joined++;
        }
        else {
            break;
        }
    }

    Client *last = &crowd[MAX_OBSERVERS - 1];
    snprintf(details, sizeof(details), "%d observers accepted", joined);
    e2e_report(joined == MAX_OBSERVERS && e2e_reply_is(last, MSG_OBSERVE, RESP_FAILURE, "game full"),
               "observer number MAX+1 gets game full", details);

    //An observer leaving frees a place
    e2e_leave(&crowd[0]);
    e2e_send(last, MSG_OBSERVE, text);
    e2e_report(e2e_reply_is(last, MSG_OBSERVE, RESP_SUCCESS, NULL),
               "an observer disconnects: the place is free again", "");

    bu = dave.bu_count;
    e2e_report(e2e_play_one_move(&alice, &bob) == 0 && dave.bu_count == bu + 1,
               "the game goes on after an observer left", "");

    //------------------------------------------------------------
    printf("\n   end of the game\n");

    int go_d = dave.got_go;
    e2e_send(&bob, MSG_RESIGN, "");

    e2e_report(dave.got_go == go_d + 1 && dave.last_go_mode == MODE_OBSERVER && !dave.observing,
               "observers receive Go with mode O", dave.last_go);
    e2e_report(e2e_online_shows(&erin, "dave:L") && e2e_online_shows(&erin, "fan1:L"),
               "observers are back in the lobby", "");

    e2e_send(&dave, MSG_CHALLENGE, "erin");
    e2e_report(e2e_reply_is(&dave, MSG_CHALLENGE, RESP_SUCCESS, NULL), "and can challenge again", "");

    e2e_send(&erin, MSG_LIST_GAMES, "");
    e2e_report(e2e_reply_is(&erin, MSG_LIST_GAMES, RESP_SUCCESS, ""), "the list is empty again", "");

    return e2e_summary();
}
