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
    return -1;}

    int opponent_has_seeds = 0; //Check that the rival hasn't starved

    int opponent_start = (1 - game->current_player) * 6;
    int opponent_end = opponent_start + 5;

    for (int i = opponent_start; i <= opponent_end; i++) {
        if (game->holes[i] > 0) {
            opponent_has_seeds = 1;
            break;
        }
    }

    if (!opponent_has_seeds) {
    if (has_legal_move(game) && !can_feed_opponent(game, hole_index)) {
        return -1;
    }
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

int capture_index = current_index; //Starter hole
int captured_seeds = 0; //Counter for the captured seeds

while (capture_index >= opponent_start &&
       capture_index <= opponent_end &&
       (game->holes[capture_index] == 2 ||
        game->holes[capture_index] == 3)) {
    captured_seeds += game->holes[capture_index];
    capture_index--;
}//Whilst in enemy territory, and the hole has 2 or 3 seeds, we capture them and move to the previous hole
//This is a check to see if caputres are legal, as one can not starve out the oponent. We count the amount of seeds that theoretically we could capture in any given move


int opponent_seeds = 0;//Counter for the seeds in the opponent's holes

for (int i = opponent_start; i <= opponent_end; i++) {
    opponent_seeds += game->holes[i];
}//Sum the seeds in the opponent's holes

if (opponent_seeds > captured_seeds) {//Knowing it's legal, we proceed to capture the seeds
    // Check for captures

    while (current_index>=opponent_start && current_index <= opponent_end && (game->holes[current_index] ==2 || game->holes[current_index] == 3)) //While within enemy territory, and having two or three seeds on the given index
    {
        game->captures[game->current_player] += game->holes[current_index]; //Add the captured seeds to the current player's score
        game->holes[current_index] = 0; //Remove the captured seeds from the hole
        current_index--; //Move to the previous hole until we are out of enemy territory or the hole doesn't have 2 or 3 seeds
    }
}


    // Switch players
    game->current_player = 1 - game->current_player;

    //check game end

    check_game_over(game);

    return 0; // Weee, we sowed
}



int can_feed_opponent(const Game *game, int hole_index)//Make sure that the other player always has seeds to sow
{
    if (game == NULL || hole_index < 0 || hole_index >= 12) //Error management: Cannot use holes that don't exist
        return 0;

    int first_hole = game->current_player * 6;
    int last_hole = first_hole + 5;

    if (hole_index < first_hole || hole_index > last_hole)
        return 0;

    int seeds = game->holes[hole_index]; //Seeds in the selected hole

    if (seeds <= 0) //if there's no seeds we can't feed the opponent
        return 0;

    int opponent_start = (1 - game->current_player) * 6;
    int opponent_end = opponent_start + 5; //calculate enemy's territory

    int simulated_holes[12];

    for (int i = 0; i < 12; i++) {
        simulated_holes[i] = game->holes[i]; //Simulate the sowing process to see if it can feed the opponent
    }

    simulated_holes[hole_index] = 0;
    int current_index = hole_index;

    while (seeds > 0) {//While the seeds exist,
        current_index = (current_index + 1) % 12;//move to the next hole

        if (current_index == hole_index) {
            continue;
        }//Skip the original hole

        simulated_holes[current_index]++;
        seeds--;//Add a seed to the next hole
    }

    for (int i = opponent_start; i <= opponent_end; i++) {
        if (simulated_holes[i] > 0) {
            return 1;
        }//If the opponent has seeds after the sowing, return true
    }

    return 0;
}




int has_legal_move(const Game *game)
{
    if (game == NULL || game->finished)
        return 0;

    int first_hole = game->current_player * 6;
    int last_hole = first_hole + 5;

    int opponent_start = (1 - game->current_player) * 6;
    int opponent_has_seeds = 0;

    for (int i = opponent_start; i < opponent_start + 6; i++) {
        if (game->holes[i] > 0) {
            opponent_has_seeds = 1;
            break;
        }
    }

    for (int i = first_hole; i <= last_hole; i++) {
        if (game->holes[i] <= 0)
            continue;

        if (opponent_has_seeds)
            return 1;

        if (can_feed_opponent(game, i))
            return 1;
    }

    return 0;
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
    if (game->captures[0]>=25 || game->captures[1]>=25) {
        finish_game(game);
        return 1; // Game is over
    }

    if (!has_legal_move(game)) { //Check if the current player has any legal moves
        finish_game(game);
        return 1; // Game is over
    }

    return 0;
}