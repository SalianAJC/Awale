
#ifndef MATCH_H
#define MATCH_H

#include "game.h"

typedef struct {
    Game game;
    int player_ids[2];
    int active;
} Match;

#endif
