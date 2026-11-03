//Moves and gamecode. Structure and declarations


#ifndef GAME_H
#define GAME_H

#define MAX_MOVES 300 //Game ends after this many moves. Keeps the move list within MAX_CONTENT (protocol.h)

typedef struct {
    int id; // Unique identifier for the game
    int holes[12]; // 12 holes, 6 on either side
    int captures [2]; 

    int current_player; // 0 for player 1, 1 for player 2
    int finished;
    int move_count; //Number of moves played so far
} Game;

void init_game(Game *game);
void print_board(const Game *game);
int sow(Game *game, int hole_index);
int has_legal_move(const Game *game);
void finish_game(Game *game);
int check_game_over(Game *game);
int can_feed_opponent(const Game *game, int hole_index);

#endif