//Tests for registerUser (server_logic.c). No network involved.
//Build: gcc -Wall -Wextra -Iinclude tests/test_registration.c src/server_logic.c -o target/test_registration

#include <stdio.h>
#include <string.h>

#include "server_logic.h"

static int passed = 0;
static int failed = 0;
static int registered = 0; //Number of successful registrations so far
static int quiet = 0; //When set, only failures are printed

//Sends one registration request and checks the reply
static void check(const char *name, char expected, const char *label)
{
    Message request = {0};
    strcpy(request.type, MSG_REGISTER);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    request.receiver = 0;
    strcpy(request.content, name);

    Message reply = registerUser(&request);

    int ok = 1;

    if (reply.response != expected) {
        ok = 0;
    }

    if (strcmp(reply.type, MSG_REGISTER) != 0) { //A reply has the same type as the request
        ok = 0;
    }

    if (reply.mode != MODE_NONE) {
        ok = 0;
    }

    if (!ok || !quiet) {
        printf("%s  %-28s expected %c, got %c | type \"%s\" | mode '%c' | \"%s\"\n",
           ok ? "PASS" : "FAIL",
           label,
           expected,
           reply.response ? reply.response : '?',
           reply.type,
           reply.mode ? reply.mode : '?',
           reply.content);
    }

    if (ok) {
        passed++;
    } else {
        failed++;
    }

    if (reply.response == RESP_SUCCESS) {
        registered++;
    }
}

int main(void)
{
    char name[MAX_NAME + 2]; //Room for MAX_NAME + 1 characters and the '\0'

    //Normal cases
    check("Alice", RESP_SUCCESS, "normal");
    check("Bob", RESP_SUCCESS, "normal");

    //Duplicate. Also proves that Alice was really stored
    check("Alice", RESP_FAILURE, "duplicate");

    //Case sensitivity. strcmp is case-sensitive, so "alice" is a different user.
    //Change the expected value if you decide names are case-insensitive
    check("alice", RESP_SUCCESS, "different case");

    //Empty name
    check("", RESP_FAILURE, "empty");

    //Exactly MAX_NAME characters: allowed
    memset(name, 'a', MAX_NAME);
    name[MAX_NAME] = '\0';
    check(name, RESP_SUCCESS, "exactly MAX_NAME chars");

    //MAX_NAME + 1 characters: refused
    memset(name, 'b', MAX_NAME + 1);
    name[MAX_NAME + 1] = '\0';
    check(name, RESP_FAILURE, "MAX_NAME + 1 chars");

    //Illegal characters
    check("al ice", RESP_FAILURE, "space");
    check("bob!", RESP_FAILURE, "illegal character");
    check("bob|x", RESP_FAILURE, "separator |");
    check("a,b", RESP_FAILURE, "list separator ,");

    //Allowed special characters
    check("user_1", RESP_SUCCESS, "underscore and digit");

    //Fill the server. Must stay last: nobody can register after this
    int to_fill = MAX_CLIENTS - registered;
    int fill_failed = failed;
    quiet = 1;

    for (int i = 0; i < to_fill; i++) {
        snprintf(name, sizeof(name), "filler%d", i);
        check(name, RESP_SUCCESS, "filling the server");
    }

    quiet = 0;

    if (failed == fill_failed) {
        printf("      (%d filler registrations, all passed)\n", to_fill);
    }

    check("onemore", RESP_FAILURE, "server full");

    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}