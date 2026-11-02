//Message types and structures


/*
Player A --> Server

Message Types:

R : Register
L : login
M : message chat (limiting to 200 characters. Why 200?)
C : choose Player (wait, if B accepts. Start game. B rejects, end game)

Final
Message :
    Struct :
        Type : character
        Content : 200 bytes - String
        (Receiver : other player ?)
        Response : character 

*/


/*
Server --> Player B :

R : registration success
R : registration failed

L : login successful
L : login failed

M : Send the chat message

S : show players online (array of players ?)
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