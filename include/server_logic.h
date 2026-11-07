//Users, challenges, games and rules

#include "protocol.h"
#include <string.h>


#define MAX_NAME 32
#define MAX_PASS 32

typedef struct {
    int player_id;
    char username[MAX_NAME];
    char bio[BIO_SIZE];
    int friends_ids[MAX_FRIENDS];
    int session_index;
    char Flag; // P for private, ... for Public ? 
} Account;



Message registerUser(const Message *request);
