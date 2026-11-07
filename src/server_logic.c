
// Users, challenges, games and rules

#include "server_logic.h"
#include <stdio.h>
#include <string.h>

// Private server state
static int actual_count = 0;
static Account Active_players[MAX_CLIENTS];

// Private helper declarations
static int gen_id(int a);
static int isUsernameEmpty(const char *username);
static int isUsernameTooLong(const char *username);
static int isServerFull(void);
static int usernameExists(const char *username);
static int hasIllegalCharacters(const char *username);


// -------------------------------- REGISTRATION --------------------------------

Message registerUser(const Message *request) {

    Message response = {0};

    // Initialise fields that are the same for every response
    strcpy(response.type, MSG_REGISTER);
    response.mode = MODE_NONE;

    // Extract registration information
    const char *username = request->content;

    // Failure cases

    if (isUsernameEmpty(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username can't be empty");
        return response;
    }

    if (isUsernameTooLong(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username can't exceed 32 characters");
        return response;
    }

    if (isServerFull()) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Server is full");
        return response;
    }

    if (usernameExists(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username already exists");
        return response;
    }

    if (hasIllegalCharacters(username)) {
        response.response = RESP_FAILURE;
        strcpy(response.content, "Username contains illegal characters");
        return response;
    }

    // All requirements passed: continue to register

    int id = gen_id(actual_count);

    Account player = {0};
    player.player_id = id;
    player.session_index = -1;

    strcpy(player.username, username);
    strcpy(response.content, "registered");

    // Add the user and increment count

    Active_players[actual_count] = player;
    actual_count++;

    // Prepare success response

    response.response = RESP_SUCCESS;
    response.receiver = id;

    return response;
}


// -------------------------------- PRIVATE HELPERS --------------------------------

// Generate player ID
static int gen_id(int a) {
    return a + 1;
}

// Check if username is empty
static int isUsernameEmpty(const char *username) {
    return strlen(username) == 0;
}

// Check if username exceeds MAX_NAME
static int isUsernameTooLong(const char *username) {
    return strlen(username) > MAX_NAME;
}

// Check if server is full
static int isServerFull(void) {
    return actual_count >= MAX_CLIENTS;
}

// Check if username already exists
static int usernameExists(const char *username) {
    for (int i = 0; i < actual_count; i++) {
        if (strcmp(Active_players[i].username, username) == 0) {
            return 1;
        }
    }

    return 0;
}

// Check for illegal characters
static int hasIllegalCharacters(const char *username) {
    for (int i = 0; username[i] != '\0'; i++) {
        char c = username[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_')) {
            return 1;
        }
    }

    return 0;
}
