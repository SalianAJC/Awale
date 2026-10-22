
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "game.h"

int main(void)
{
    Game game;
    int hole_index;
    int result;
    char input[100];
    char *end;
    long value;

    init_game(&game);

    while (!game.finished) {
        print_board(&game);

        printf("\nPlayer %d, choose a hole (0-11), or q to quit: ",
               game.current_player + 1);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\nInput ended. Exiting game.\n");
            return 0;
        }

        // Allow the player to quit
        if (input[0] == 'q' || input[0] == 'Q') {
            printf("Game abandoned.\n");
            return 0;
        }

        // Convert input into a number
        value = strtol(input, &end, 10);

        // Reject invalid input
        if (end == input) {
            printf("Invalid input. Enter a number.\n");
            continue;
        }

        while (isspace((unsigned char)*end)) {
            end++;
        }

        if (*end != '\0') {
            printf("Invalid input. Enter a single number.\n");
            continue;
        }

        if (value < 0 || value > 11) {
            printf("Invalid hole. Choose between 0 and 11.\n");
            continue;
        }

        hole_index = (int)value;

        result = sow(&game, hole_index);

        if (result != 0) {
            printf("Invalid move. Try again.\n");
            continue;
        }
    }

    print_board(&game);

    printf("\nGame over!\n");

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

    return 0;
}
