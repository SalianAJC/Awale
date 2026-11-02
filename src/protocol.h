//Message types and structures


/*
Player A --> Server

Message Types:

R : Register
L : login
M : message chat (limiting to 200 characters. Why 200?)
C : choose Player (wait, if B accepts. Start game. B rejects, no game)
O : observe (so also keep a list of all ongoing games)
W : write a bio
R : review game

Final
Message :
    Struct messageClient :
        Type : string
        Content : 200 bytes - String
        (Receiver : other player ?)
        Response : char (S - success / F - fail...etc) 
        Mode : char - (O : observer - P : player)

*/


/*
    Struct messageServer :
        Type : string
        Content : 200 bytes - String
        (Receiver : other player ?)
        Response : char (S - success / F - fail...etc) 
        Mode : char - (O : observer - P : player)

*/

/*
Server --> Player B :

R : registration success
R : registration failed

L : login successful
L : login failed

M : Send the chat message

S : show players online (array of players ?)

Sh : show historic game (Filter by user's games or list overall recent games. Using a hashmap)
*/

/*
Ideas for data structures :
    - Struct for the message (two different, one for player one for server)
    - An array of active games

Questions : 
    - What operations happen where ? (Client or server)
    Checking legal moves (on the client)
    ....what else ?
*/


/*
Player Struct :
    Already exist
    int player_id
    char username[MAX_NAME]

    to add
    char bio[10] (imposed by the teacher)
    games arr[]
*/