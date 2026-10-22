//Moves, objects and game structure. Function executor

#include <stdio.h>
#include "game.h"

void init_game(Game *game) {
    for (int i = 0; i < 12; i++) {
        game->holes[i] = 4; // Each hole starts with 4 seeds
    }
    game->captures[0] = 0; //Each player starts with 0 captures
    game->captures[1] = 0;//Same here
    game->current_player = 0; // Player 1 starts
    game->finished = 0; // Game is not finished
}


void print_board(const Game *game)
{
    printf("Player 1's side:\n");

    for (int i = 11; i >= 6; i--) {
        printf("Hole %d: %d seeds\n", i, game->holes[i]); //Print the number of seeds in each hole for player 2 with hole index
    }
    printf("\n");
    printf("Player 0's side:\n");
    for (int i = 0; i < 6; i++) {
        printf("Hole %d: %d seeds\n", i, game->holes[i]); //Print the number of seeds in each hole for player 2 with hole index
    }
    printf("\n");
    printf("Score 0: %d | Score 1: %d\n",
           game->captures[0], game->captures[1]); //Print score

    printf("\n");
    printf("Current player: %d\n", game->current_player);
}

int sow(Game *game, int hole_index) {
    
    if (game == NULL || game->finished) {
        return -1;
    }

    if (hole_index < 0 || hole_index >= 12) { //Error management: Cannot use holes that don't exist
        return -1; // Invalid hole index
    }

    int first_hole = game ->current_player * 6; // Calculate the first hole index for the current player    
    int last_hole = first_hole + 5; // Calculate the last hole index for the current player
    if (hole_index < first_hole || hole_index > last_hole) {
    return -1;
}

    int seeds = game->holes[hole_index];
    if (seeds == 0) { //Error management: Cannot sow from an empty hole
        return -1;
    }

    game->holes[hole_index] = 0; // Remove seeds from the selected hole
    int current_index = hole_index;

    while (seeds > 0) {
        current_index = (current_index + 1) % 12; // Move to the next hole

        //Skipping the original hole

        if (current_index == hole_index) {
            continue; // Skip the original hole
        }

        game->holes[current_index]++; //Add a seed to the next hole
        seeds--; // Decrease the number of seeds to sow
    }

    // Check for captures


    //Create opponent's field
    int opponent_start = (1 - game->current_player) * 6; //Depending on the current player it starts on 6 or 0
    int opponent_end = opponent_start + 5; //Calculate the last hole index for the opponent

    while (current_index>=opponent_start && current_index <= opponent_end && (game->holes[current_index] ==2 || game->holes[current_index] == 3)) //While within enemy territory, and having two or three seeds on the given index
    {
        game->captures[game->current_player] += game->holes[current_index]; //Add the captured seeds to the current player's score
        game->holes[current_index] = 0; //Remove the captured seeds from the hole
        current_index--; //Move to the previous hole until we are out of enemy territory or the hole doesn't have 2 or 3 seeds
    }


    // Switch players
    game->current_player = 1 - game->current_player;

    //check game end

    check_game_over(game);

    return 0; // Weee, we sowed
}


int has_legal_move(const Game *game) {
    if (game == NULL || game->finished) {
        return 0; // No legal moves if the game is finished or game is NULL
    }

    int first_hole = game->current_player * 6; // Calculate the first hole index for the current player    
    int last_hole = first_hole + 5; // Calculate the last hole index for the current player

    for (int i = first_hole; i <= last_hole; i++) { //For every hole in the current player's territory, check if there is at least one seed to move
        if (game->holes[i] > 0) {
            return 1; // Found a legal move, having at least one seed to move
        }
    }

    return 0; // No legal moves found
}

void finish_game(Game *game) {
    if (game == NULL) {
        return; // Do nothing if game is NULL
    }

    //Collect the remaining seeds

    for (int i = 0; i < 6; i++) {
        game->captures[0] += game->holes[i]; //Add the remaining seeds to player 0's score
        game->holes[i] = 0; //Remove the remaining seeds from the holes


        game ->captures[1] +=game->holes[i+6]; //Add the remaining seeds to player 1's score
        game->holes[i+6] = 0; //Remove the remaining seeds from the holes
    }

    game->finished = 1; // Mark the game as finished
}

int check_game_over(Game *game){
    if (game == NULL) {
        return 0; // Game is not over if game is NULL
    }

    //Check victory by majority
    if (game->captures[0]>25 || game->captures[1]>25) {
        finish_game(game);
        return 1; // Game is over
    }

    if (!has_legal_move(game)) { //Check if the current player has any legal moves
        finish_game(game);
        return 1; // Game is over
    }

    return 0;
}