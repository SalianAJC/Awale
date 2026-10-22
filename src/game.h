//Moves and gamecode. Structure and declarations


#ifndef GAME_H
#define GAME_H

typedef struct {
    int holes[12]; // 12 holes, 6 on either side
    int captures [2]; 

    int current_player; // 0 for player 1, 1 for player 2
    int finished;
} Game;

void init_game(Game *game);
void print_board(const Game *game);
int sow(Game *game, int hole_index);
int has_legal_move(const Game *game);
void finish_game(Game *game);
int check_game_over(Game *game)

#endif