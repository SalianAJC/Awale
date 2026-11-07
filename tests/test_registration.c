//Tests for registration, through the public interface of server_logic only.
//No network involved: the test reads the outbox instead of a socket.
//Build: gcc -Wall -Wextra -Iinclude tests/test_registration.c src/server_logic.c -o target/test_registration
//(add src/game.c once server_logic.c uses the game)

#include <stdio.h>
#include <string.h>

#include "server_logic.h"

#ifndef MSG_ERROR
#define MSG_ERROR "E" //From spec 7. Remove this once it is in protocol.h
#endif

static int passed = 0;
static int failed = 0;
static int registered = 0; //Number of successful registrations so far
static int quiet = 0; //When set, only failures are printed

static int session; //The session used for most tests
static unsigned int generation; //Its generation, as returned by logic_connect

//Prints one result line and counts it
static void report(int ok, const char *label, const char *details)
{
    if (!ok || !quiet) {
        printf("%s  %-28s %s\n", ok ? "PASS" : "FAIL", label, details);
    }

    if (ok) {
        passed++;
    } else {
        failed++;
    }
}

//Sends one message from a session and checks that exactly one reply comes back,
//addressed to that session, with the expected type and response
static void check_reply(int from, unsigned int from_generation,
                        const char *type, const char *content,
                        const char *expected_type, char expected,
                        const char *label)
{
    Message request = {0};
    strcpy(request.type, type);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    request.receiver = 0;
    strcpy(request.content, content);

    Outbox outbox = {0};
    logic_handle_message(from, &request, &outbox);

    char details[MAX_CONTENT + 200];

    if (outbox.count != 1) {
        snprintf(details, sizeof(details),
                 "expected exactly 1 outbox entry, got %d", outbox.count);
        report(0, label, details);
        return;
    }

    const OutboxEntry *entry = &outbox.entries[0];
    const Message *reply = &entry->message;

    int ok = 1;

    if (entry->session_index != from) ok = 0; //The reply goes back to the sender
    if (entry->generation != from_generation) ok = 0; //...stamped with its current generation
    if (strcmp(reply->type, expected_type) != 0) ok = 0;
    if (reply->response != expected) ok = 0;
    if (reply->mode != MODE_NONE) ok = 0;

    snprintf(details, sizeof(details),
             "expected %c, got %c | to session %d gen %u | type \"%s\" | mode '%c' | \"%s\"",
             expected,
             reply->response ? reply->response : '?',
             entry->session_index,
             entry->generation,
             reply->type,
             reply->mode ? reply->mode : '?',
             reply->content);

    report(ok, label, details);

    if (strcmp(type, MSG_REGISTER) == 0 && reply->response == RESP_SUCCESS) {
        registered++;
    }
}

//Shortcut for a registration request from the main test session
static void check_register(const char *name, char expected, const char *label)
{
    check_reply(session, generation, MSG_REGISTER, name,
                MSG_REGISTER, expected, label);
}

int main(void)
{
    char name[MAX_NAME + 2]; //Room for MAX_NAME + 1 characters and the '\0'
    char details[200];

    //------------------------------------------------------------
    printf("-- Setup\n");

    report(logic_init() == 0, "logic_init", "must return 0");

    session = logic_connect(&generation);
    snprintf(details, sizeof(details), "session %d, generation %u", session, generation);
    report(session >= 0 && session < MAX_CLIENTS, "logic_connect", details);

    if (session < 0) {
        printf("\nCannot continue without a session.\n");
        return 1;
    }

    //------------------------------------------------------------
    printf("\n-- Registration\n");

    check_register("Alice", RESP_SUCCESS, "normal");
    check_register("Bob", RESP_SUCCESS, "normal");

    //Duplicate. Also proves that Alice was really stored
    check_register("Alice", RESP_FAILURE, "duplicate");

    //Case sensitivity. strcmp is case-sensitive, so "alice" is a different user.
    //Change the expected value if you decide names are case-insensitive
    check_register("alice", RESP_SUCCESS, "different case");

    check_register("", RESP_FAILURE, "empty");

    //Exactly MAX_NAME characters: allowed
    memset(name, 'a', MAX_NAME);
    name[MAX_NAME] = '\0';
    check_register(name, RESP_SUCCESS, "exactly MAX_NAME chars");

    //MAX_NAME + 1 characters: refused
    memset(name, 'b', MAX_NAME + 1);
    name[MAX_NAME + 1] = '\0';
    check_register(name, RESP_FAILURE, "MAX_NAME + 1 chars");

    check_register("al ice", RESP_FAILURE, "space");
    check_register("bob!", RESP_FAILURE, "illegal character");
    check_register("bob|x", RESP_FAILURE, "separator |");
    check_register("a,b", RESP_FAILURE, "list separator ,");
    check_register("user_1", RESP_SUCCESS, "underscore and digit");

    //------------------------------------------------------------
    printf("\n-- Dispatcher (spec 2.2 and 2.4)\n");

    //A type the server does not know gets an E failure
    check_reply(session, generation, "Zz", "", MSG_ERROR, RESP_FAILURE,
                "unknown type");

    //Lobby messages are refused before login
    check_reply(session, generation, MSG_LIST_ONLINE, "", MSG_LIST_ONLINE, RESP_FAILURE,
                "LO before login");

    //------------------------------------------------------------
    printf("\n-- Sessions\n");

    //A second client gets a different slot
    unsigned int generation2 = 0;
    int session2 = logic_connect(&generation2);
    snprintf(details, sizeof(details), "session %d (first was %d)", session2, session);
    report(session2 >= 0 && session2 != session, "second connect", details);

    //Its replies go to it, not to the first session
    check_reply(session2, generation2, MSG_REGISTER, "Carol",
                MSG_REGISTER, RESP_SUCCESS, "reply to second session");

    //Disconnect it, reconnect: the generation must change if the slot is reused
    Outbox outbox = {0};
    logic_disconnect(session2, &outbox);
    snprintf(details, sizeof(details), "%d outbox entries", outbox.count);
    report(outbox.count == 0, "disconnect, not logged in", details);

    unsigned int generation3 = 0;
    int session3 = logic_connect(&generation3);

    if (session3 == session2) {
        snprintf(details, sizeof(details), "slot %d reused, gen %u -> %u",
                 session3, generation2, generation3);
        report(generation3 != generation2, "generation changes on reuse", details);
    } else {
        snprintf(details, sizeof(details), "slot not reused (%d), nothing to check", session3);
        report(session3 >= 0, "generation changes on reuse", details);
    }

    //The account survives the disconnect: Carol is still taken
    check_reply(session3, generation3, MSG_REGISTER, "Carol",
                MSG_REGISTER, RESP_FAILURE, "account kept after leaving");

    logic_disconnect(session3, &outbox);

    //Fill every session slot, then one more must be refused
    int connected = 1; //The main test session is still connected
    int connect_failed = 0;

    for (int i = connected; i < MAX_CLIENTS; i++) {
        unsigned int g;

        if (logic_connect(&g) < 0) {
            connect_failed = 1;
        }
    }

    report(!connect_failed, "connect up to MAX_CLIENTS", "");

    unsigned int g_extra = 12345;
    int extra = logic_connect(&g_extra);
    snprintf(details, sizeof(details), "got %d, generation left at %u", extra, g_extra);
    report(extra == -1 && g_extra == 12345, "connect when full", details);

    //------------------------------------------------------------
    printf("\n-- Server full (accounts)\n");

    //Must stay last: nobody can register after this
    int to_fill = MAX_CLIENTS - registered;
    int fill_failed = failed;
    quiet = 1;

    for (int i = 0; i < to_fill; i++) {
        snprintf(name, sizeof(name), "filler%d", i);
        check_register(name, RESP_SUCCESS, "filling the server");
    }

    quiet = 0;

    if (failed == fill_failed) {
        printf("      (%d filler registrations, all passed)\n", to_fill);
    }

    check_register("onemore", RESP_FAILURE, "server full");

    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}