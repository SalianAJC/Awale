#ifndef PLAYER_H
#define PLAYER_H

#define MAX_NAME 32

typedef struct {
    int player_id;
    char username[MAX_NAME];
} Player;

#endif