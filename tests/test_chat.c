
#include <stdio.h>
#include <string.h>

#include "server_logic.h"

static int passed = 0;
static int failed = 0;

// One logged-in test player
typedef struct {
    const char *name;
    int session;
    unsigned int generation;
} Player;

// ----------------------------------------------------------------
// Small tools
// ----------------------------------------------------------------

static void report(int ok, const char *label, const char *details)
{
    printf("%s  %-40s %s\n",
           ok ? "PASS" : "FAIL", label, details);

    if (ok) {
        passed++;
    } else {
        failed++;
    }
}

// Sends one request from a session and fills the outbox
static void send_request(int session, const char *type,
                         const char *content, Outbox *outbox)
{
    Message request = {0};

    strcpy(request.type, type);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    strcpy(request.content, content);

    memset(outbox, 0, sizeof(*outbox));
    logic_handle_message(session, &request, outbox);
}

// Connects, registers and logs in a player
static int login_player(Player *p)
{
    Outbox outbox;

    p->session = logic_connect(&p->generation);

    if (p->session < 0) {
        return -1;
    }

    send_request(p->session, MSG_REGISTER, p->name, &outbox);
    send_request(p->session, MSG_LOGIN, p->name, &outbox);

    if (outbox.count < 1 ||
        outbox.entries[0].message.response != RESP_SUCCESS) {
        return -1;
    }

    return 0;
}

// Returns the entry sent to this session with this type, or NULL
static const OutboxEntry *find_entry(const Outbox *outbox,
                                     int session, const char *type)
{
    for (int i = 0; i < outbox->count; i++) {
        if (outbox->entries[i].session_index == session &&
            strcmp(outbox->entries[i].message.type, type) == 0) {
            return &outbox->entries[i];
        }
    }

    return NULL;
}

// Checks recipient, generation, response, mode and content
static int entry_is(const OutboxEntry *e, const Player *to,
                    char response, char mode, const char *content)
{
    return e != NULL &&
           e->session_index == to->session &&
           e->generation == to->generation &&
           e->message.response == response &&
           e->message.mode == mode &&
           (content == NULL ||
            strcmp(e->message.content, content) == 0);
}

static void describe(const Outbox *outbox, char *buffer, size_t size)
{
    buffer[0] = '\0';

    snprintf(buffer, size, "%d entr%s:",
             outbox->count,
             outbox->count == 1 ? "y" : "ies");

    for (int i = 0; i < outbox->count && strlen(buffer) + 80 < size; i++) {
        const OutboxEntry *e = &outbox->entries[i];
        char one[MAX_CONTENT + 64];

        snprintf(one, sizeof(one), " [s%d %s|%c|%c|%s]",
                 e->session_index,
                 e->message.type,
                 e->message.response ? e->message.response : '?',
                 e->message.mode ? e->message.mode : '?',
                 e->message.content);

        strncat(buffer, one, size - strlen(buffer) - 1);
    }
}

// Checks a chat failure: exactly one failure reply to the sender
static void expect_chat_failure(const Player *from,
                                const char *content,
                                const char *reason,
                                const char *label)
{
    Outbox outbox;
    char details[2048];

    send_request(from->session, MSG_CHAT, content, &outbox);
    describe(&outbox, details, sizeof(details));

    int ok = outbox.count == 1 &&
             entry_is(&outbox.entries[0], from,
                      RESP_FAILURE, MODE_NONE, reason);

    report(ok, label, details);
}

// ----------------------------------------------------------------
// Tests
// ----------------------------------------------------------------

int main(void)
{
    Player alice = { "alice", 0, 0 };
    Player bob = { "bob", 0, 0 };

    Outbox outbox;
    char details[2048];

    //------------------------------------------------------------
    printf("-- Setup\n");

    report(logic_init() == 0, "logic_init", "");

    int ok = login_player(&alice) == 0 &&
             login_player(&bob) == 0;

    report(ok, "alice and bob logged in", "");

    if (!ok) {
        printf("\nCannot continue without the players.\n");
        return 1;
    }

    //------------------------------------------------------------
    printf("\n-- M: bad format\n");

    expect_chat_failure(&alice, "bob",
                        "bad format", "M without a space");

    expect_chat_failure(&alice, " bob hello",
                        "bad format", "M with empty recipient");

    //------------------------------------------------------------
    printf("\n-- M: empty message\n");

    expect_chat_failure(&alice, "bob ",
                        "empty message", "M with empty text");

    //------------------------------------------------------------
    printf("\n-- M: message too long\n");

    char long_message[MAX_CHAT + 2];

    long_message[0] = 'b';
    long_message[1] = 'o';
    long_message[2] = 'b';
    long_message[3] = ' ';

    memset(long_message + 4, 'a', MAX_CHAT + 1);
    long_message[4 + MAX_CHAT + 1] = '\0';

    expect_chat_failure(&alice, long_message,
                        "message too long", "M with MAX_CHAT + 1 characters");

    //------------------------------------------------------------
    printf("\n-- M: unknown user\n");

    expect_chat_failure(&alice, "zoe hello",
                        "unknown user", "M to unknown user");

    //------------------------------------------------------------
    printf("\n-- M: offline user\n");

    Player carol = { "carol", 0, 0 };

    ok = login_player(&carol) == 0;

    report(ok, "carol registered and logged in", "");

    if (!ok) {
        printf("\nCannot continue without Carol.\n");
        return 1;
    }

    memset(&outbox, 0, sizeof(outbox));
    logic_disconnect(carol.session, &outbox);

    expect_chat_failure(&alice, "carol hello",
                        "user offline", "M to offline user");

    //------------------------------------------------------------
    printf("\n-- M: successful delivery\n");

    send_request(alice.session, MSG_CHAT,
                 "bob hi there", &outbox);

    describe(&outbox, details, sizeof(details));

    ok = outbox.count == 2 &&
         entry_is(&outbox.entries[0], &alice,
                  RESP_SUCCESS, MODE_NONE, "bob") &&
         entry_is(&outbox.entries[1], &bob,
                  RESP_NONE, MODE_NONE, "alice hi there");

    report(ok, "M success: reply + push to Bob", details);

    //------------------------------------------------------------
    printf("\n-- M: preserve spaces and | characters\n");

    send_request(alice.session, MSG_CHAT,
                 "bob good luck | have fun", &outbox);

    describe(&outbox, details, sizeof(details));

    ok = outbox.count == 2 &&
         entry_is(&outbox.entries[0], &alice,
                  RESP_SUCCESS, MODE_NONE, "bob") &&
         entry_is(&outbox.entries[1], &bob,
                  RESP_NONE, MODE_NONE,
                  "alice good luck | have fun");

    report(ok, "M preserves spaces and |", details);

    //------------------------------------------------------------
    printf("\n-- M: exactly MAX_CHAT characters\n");

    char exact_message[MAX_NAME + 1 + MAX_CHAT + 1];
    char expected[MAX_NAME + 1 + MAX_CHAT + 1];

    strcpy(exact_message, "bob ");

    memset(exact_message + 4, 'a', MAX_CHAT);
    exact_message[4 + MAX_CHAT] = '\0';

    snprintf(expected, sizeof(expected), "alice %s",
             exact_message + 4);

    send_request(alice.session, MSG_CHAT,
                 exact_message, &outbox);

    describe(&outbox, details, sizeof(details));

    ok = outbox.count == 2 &&
         entry_is(&outbox.entries[0], &alice,
                  RESP_SUCCESS, MODE_NONE, "bob") &&
         entry_is(&outbox.entries[1], &bob,
                  RESP_NONE, MODE_NONE, expected);

    report(ok, "M accepts exactly MAX_CHAT characters", details);

    //------------------------------------------------------------
    printf("\n-- M: sending to yourself\n");

    send_request(alice.session, MSG_CHAT,
                 "alice hello myself", &outbox);

    describe(&outbox, details, sizeof(details));

    ok = outbox.count == 2 &&
         entry_is(&outbox.entries[0], &alice,
                  RESP_SUCCESS, MODE_NONE, "alice") &&
         entry_is(&outbox.entries[1], &alice,
                  RESP_NONE, MODE_NONE, "alice hello myself");

    report(ok, "M allows messaging yourself", details);

    //------------------------------------------------------------
    printf("\n%d passed, %d failed\n", passed, failed);

    return failed == 0 ? 0 : 1;
}
