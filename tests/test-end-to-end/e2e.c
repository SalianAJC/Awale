//Shared tools for the end-to-end tests. See e2e.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e2e.h"

#define MAX_TEST_CLIENTS 16

Outbox e2e_outbox;

static Client *clients[MAX_TEST_CLIENTS];
static int client_count = 0;

static int passed = 0;
static int failed = 0;

// ----------------------------------------------------------------
// Results
// ----------------------------------------------------------------

void e2e_report(int ok, const char *label, const char *details)
{
    printf("%s  %-46s %s\n", ok ? "PASS" : "FAIL", label, details);

    if (ok) {
        passed++;
    } else {
        failed++;
    }
}

int e2e_summary(void)
{
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// ----------------------------------------------------------------
// Delivering messages to the simulated clients
// ----------------------------------------------------------------

static Client *client_of_session(int session)
{
    for (int i = 0; i < client_count; i++) {
        if (clients[i]->session == session) {
            return clients[i];
        }
    }

    return NULL;
}

static void deliver(void)
{
    for (int i = 0; i < e2e_outbox.count; i++) {
        const OutboxEntry *e = &e2e_outbox.entries[i];
        const Message *m = &e->message;
        Client *c = client_of_session(e->session_index);

        //Like send_outbox in server.c: an old generation is never delivered
        if (c == NULL || e->generation != c->generation) {
            continue;
        }

        if (strcmp(m->type, MSG_GAME_START) == 0) {
            if (sscanf(m->content, "%d %d %32s", &c->game_id, &c->side, c->opponent) == 3) {
                init_game(&c->board);
                c->in_game = 1;
            }
        }
        else if (strcmp(m->type, MSG_OBSERVE) == 0 && m->response == RESP_SUCCESS) {
            //Start watching: fresh board, then replay the moves played so far
            char side0[MAX_NAME + 1], side1[MAX_NAME + 1];
            int consumed = 0;

            if (sscanf(m->content, "%d %32s %32s%n", &c->game_id, side0, side1, &consumed) >= 3) {
                init_game(&c->board);
                c->observing = 1;

                const char *p = m->content + consumed;

                while (*p == ' ') {
                    p++;
                }

                while (*p != '\0') {
                    int hole = atoi(p);

                    if (sow(&c->board, hole) != 0) {
                        c->board_errors++;
                    }

                    while (*p != '\0' && *p != ',') {
                        p++;
                    }

                    if (*p == ',') {
                        p++;
                    }
                }
            }
        }
        else if (strcmp(m->type, MSG_STOP_OBSERVE) == 0 && m->response == RESP_SUCCESS) {
            c->observing = 0;
        }
        else if (strcmp(m->type, MSG_BOARD_UPDATE) == 0) {
            int id, hole;

            if (sscanf(m->content, "%d %d", &id, &hole) == 2 &&
                (c->in_game || c->observing) && id == c->game_id) {
                c->bu_count++;
                c->last_bu_mode = m->mode;

                if (sow(&c->board, hole) != 0) {
                    c->board_errors++;
                }
            }
        }
        else if (strcmp(m->type, MSG_GAME_OVER) == 0) {
            c->got_go++;
            c->in_game = 0;
            c->observing = 0;
            c->last_go_mode = m->mode;
            snprintf(c->last_go, sizeof(c->last_go), "%s", m->content);
        }
        else if (strcmp(m->type, MSG_CHAT) == 0 && m->response == RESP_NONE) {
            c->chat_count++;
            snprintf(c->last_chat, sizeof(c->last_chat), "%s", m->content);
        }
        else if (strcmp(m->type, MSG_REFUSE) == 0 && m->response == RESP_NONE) {
            c->rf_count++;
            snprintf(c->last_rf, sizeof(c->last_rf), "%s", m->content);
        }
    }
}

// ----------------------------------------------------------------
// Setup and clients
// ----------------------------------------------------------------

int e2e_init(void)
{
    remove(USERS_FILE);
    remove(FRIENDS_FILE);

    return logic_init();
}

static int connect_client(Client *c)
{
    c->session = logic_connect(&c->generation);
    c->in_game = 0;
    c->observing = 0;

    if (c->session < 0) {
        return -1;
    }

    //Register the struct once, even if it reconnects later
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == c) {
            return 0;
        }
    }

    if (client_count >= MAX_TEST_CLIENTS) {
        return -1;
    }

    clients[client_count++] = c;
    return 0;
}

int e2e_join(Client *c, const char *name)
{
    memset(c, 0, sizeof(*c));
    c->name = name;

    if (connect_client(c) != 0) {
        return -1;
    }

    e2e_send(c, MSG_REGISTER, name);
    e2e_send(c, MSG_LOGIN, name);

    return e2e_reply_is(c, MSG_LOGIN, RESP_SUCCESS, name) ? 0 : -1;
}

int e2e_reconnect(Client *c)
{
    if (connect_client(c) != 0) {
        return -1;
    }

    e2e_send(c, MSG_LOGIN, c->name);

    return e2e_reply_is(c, MSG_LOGIN, RESP_SUCCESS, c->name) ? 0 : -1;
}

void e2e_leave(Client *c)
{
    memset(&e2e_outbox, 0, sizeof(e2e_outbox));
    logic_disconnect(c->session, &e2e_outbox);
    deliver();

    //The client is gone: nothing more is delivered to it
    c->session = -100;
    c->in_game = 0;
    c->observing = 0;
}

// ----------------------------------------------------------------
// Requests and replies
// ----------------------------------------------------------------

void e2e_send(Client *c, const char *type, const char *content)
{
    Message request = {0};

    strcpy(request.type, type);
    request.response = RESP_NONE;
    request.mode = MODE_NONE;
    snprintf(request.content, sizeof(request.content), "%s", content);

    memset(&e2e_outbox, 0, sizeof(e2e_outbox));
    logic_handle_message(c->session, &request, &e2e_outbox);
    deliver();
}

const Message *e2e_reply(const Client *c, const char *type)
{
    for (int i = 0; i < e2e_outbox.count; i++) {
        const OutboxEntry *e = &e2e_outbox.entries[i];

        if (e->session_index == c->session &&
            strcmp(e->message.type, type) == 0 &&
            e->message.response != RESP_NONE) {
            return &e->message;
        }
    }

    return NULL;
}

int e2e_reply_is(const Client *c, const char *type, char response, const char *content)
{
    const Message *r = e2e_reply(c, type);

    return r != NULL && r->response == response &&
           (content == NULL || strcmp(r->content, content) == 0);
}

void e2e_describe_outbox(char *buffer, size_t size)
{
    snprintf(buffer, size, "%d entries:", e2e_outbox.count);

    for (int i = 0; i < e2e_outbox.count && strlen(buffer) + 100 < size; i++) {
        const OutboxEntry *e = &e2e_outbox.entries[i];
        char one[160];

        snprintf(one, sizeof(one), " [s%d %s|%c|%.60s]", e->session_index, e->message.type,
                 e->message.response ? e->message.response : '?', e->message.content);
        strncat(buffer, one, size - strlen(buffer) - 1);
    }
}

// ----------------------------------------------------------------
// Games
// ----------------------------------------------------------------

int e2e_start_game(Client *challenger, Client *accepter)
{
    e2e_send(challenger, MSG_CHALLENGE, accepter->name);

    if (!e2e_reply_is(challenger, MSG_CHALLENGE, RESP_SUCCESS, NULL)) {
        return -1;
    }

    e2e_send(accepter, MSG_ACCEPT, challenger->name);

    if (!challenger->in_game || !accepter->in_game ||
        challenger->game_id != accepter->game_id ||
        challenger->side != 1 - accepter->side) {
        return -1;
    }

    return 0;
}

Client *e2e_to_move(Client *a, Client *b)
{
    return a->board.current_player == a->side ? a : b;
}

//A small fixed pseudo-random generator: varied but repeatable games
static unsigned int seed = 12345;

static unsigned int next_random(void)
{
    seed = seed * 1103515245u + 12345u;
    return (seed / 65536u) % 32768u;
}

int e2e_pick_legal_hole(const Client *c)
{
    int first = c->side * 6;
    int start = (int)(next_random() % 6);

    for (int k = 0; k < 6; k++) {
        int hole = first + (start + k) % 6;
        Game copy = c->board;

        if (sow(&copy, hole) == 0) {
            return hole;
        }
    }

    return -1;
}

int e2e_play_one_move(Client *a, Client *b)
{
    Client *mover = e2e_to_move(a, b);
    int hole = e2e_pick_legal_hole(mover);
    char text[16];

    if (hole < 0) {
        return -1;
    }

    snprintf(text, sizeof(text), "%d", hole);
    e2e_send(mover, MSG_MOVE, text);

    return e2e_reply_is(mover, MSG_MOVE, RESP_SUCCESS, text) ? 0 : -1;
}

int e2e_boards_equal(const Game *x, const Game *y)
{
    return memcmp(x->holes, y->holes, sizeof(x->holes)) == 0 &&
           x->captures[0] == y->captures[0] &&
           x->captures[1] == y->captures[1] &&
           x->current_player == y->current_player &&
           x->finished == y->finished;
}

int e2e_parse_go(const char *content, int *id, int *cap0, int *cap1,
                 char *winner, char *reason)
{
    return sscanf(content, "%d %d %d %32s %15s", id, cap0, cap1, winner, reason) == 5;
}

// ----------------------------------------------------------------
// Lobby queries
// ----------------------------------------------------------------

int e2e_rating_of(Client *asker, const char *name)
{
    e2e_send(asker, MSG_RANKING, "");

    const Message *r = e2e_reply(asker, MSG_RANKING);

    if (r == NULL) {
        return -1;
    }

    char padded[MAX_CONTENT + 3];
    char wanted[MAX_NAME + 3];

    snprintf(padded, sizeof(padded), ",%s", r->content);
    snprintf(wanted, sizeof(wanted), ",%s:", name);

    const char *at = strstr(padded, wanted);

    return at != NULL ? atoi(at + strlen(wanted)) : -1;
}

int e2e_online_shows(Client *asker, const char *item)
{
    e2e_send(asker, MSG_LIST_ONLINE, "");

    const Message *r = e2e_reply(asker, MSG_LIST_ONLINE);

    if (r == NULL) {
        return 0;
    }

    char padded[MAX_CONTENT + 3];
    char wanted[MAX_NAME + 8];

    snprintf(padded, sizeof(padded), ",%s,", r->content);
    snprintf(wanted, sizeof(wanted), ",%s,", item);

    return strstr(padded, wanted) != NULL;
}
