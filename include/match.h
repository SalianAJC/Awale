
#ifndef MATCH_H

#define MATCH_H
#include "game.h"
#include "protocol.h"
typedef struct {
    Game game;
    int in_use;
    int game_id;
    int side_account[2];
    int moves[MAX_MOVES];
    int move_count;
    int observers[MAX_OBSERVERS];
    int observer_count;
} Match;

#endif

