
#include <stdio.h>
#include "game.h"

int main(void)
{
    Game game; //Initialize the game 
    int hole_index;
    int result;

    init_game(&game); //Initialize the game state

    while (!game.finished) { //While the game is not finished, keep playing
        print_board(&game);

printf("Game over!\n");

printf("Final score: Player 1: %d | Player 2: %d\n",
       game.captures[0],
       game.captures[1]);

if (game.captures[0] > game.captures[1]) {
    printf("Player 1 wins!\n");
}
else if (game.captures[1] > game.captures[0]) {
    printf("Player 2 wins!\n");
}
else {
    printf("Draw!\n");
}
    }

    print_board(&game);
    printf("Game over!\n");

    return 0;
}
