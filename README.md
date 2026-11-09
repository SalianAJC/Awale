# Awalé: networked game server and client

A client/server Awalé (Oware) game in C. Players register with a username, see who is online, challenge each other, play full games with the rules checked by the server, chat, write a bio and manage a friends list.

Authors: Omar ______ and Samuel Villamil, INSA Lyon 4IF, Programmation Réseau.

---

## 1. Features

| Feature (from the brief) | Status |
|---|---|
| Awalé rules: board, sowing, captures, starving rule, end of game, winner | Done (`game.c`) |
| Register with a username | Done |
| List online players, with their state (lobby / in game) | Done |
| Challenge a player, accept or refuse | Done |
| Server picks who starts at random | Done |
| Server checks every move (turn, side, empty hole, starving rule) | Done |
| Several games at the same time | Done (up to 25) |
| Resign | Done |
| Disconnection during a game: the other player wins | Done |
| Chat, in the lobby and during a game | Done |
| Bio of up to 10 lines, readable by anyone | Done |
| Friends list (add, remove, list) | Done, saved to disk |
| Accounts kept between server restarts | Done (`users.txt`) |
| Observer mode, private mode, saved games, ELO ranking | Not implemented |

---

## 2. Building

The code is written for Windows (Winsock, Windows threads). Everything is built with the provided `Makefile` and placed in the `target/` folder.

**On Windows** (MinGW or MSYS2, with `gcc` and `make` in the `PATH`):

```
make
```

**On Linux** (cross-compiled for Windows, run with Wine):

```
sudo apt install mingw-w64 wine
make
```

The Makefile detects the system and chooses the compiler and the run command automatically.

| Command | Effect |
|---|---|
| `make` | Builds `target/server.exe` and `target/client.exe` |
| `make run-server` | Builds and starts the server |
| `make run-client` | Builds and starts a client |
| `make test` | Runs the protocol test (send and receive over a real socket) |
| `make test-logic` | Runs the server logic tests (registration, challenges, moves, chat) |
| `make clean` | Deletes `target/` |

---

## 3. Running

1. Start the server first: `make run-server`. It listens on port **8080** and prints a line for every connection, message and disconnection.
2. Start one client per player, each in its own terminal: `make run-client`. Clients connect to `127.0.0.1:8080`, so they must run on the same machine as the server.

To play, open at least three terminals: one server and two clients.

On Linux, if typing in the client misbehaves under Wine, start it in a Windows console instead: `wineconsole target/client.exe`.

---

## 4. Client user manual

### Start menu

```
1. Register     create an account (username: 1 to 32 letters, digits or _)
2. Login        log in with an existing username
3. Exit
```

There is no password: a username is enough to log in, but the same account cannot be logged in twice at the same time. After logging in, the client shows the list of online players.

### Lobby menu

```
1. Show online players     name:L = in the lobby, name:G = in a game
2. Challenge a player      type their username
3. Write your bio          up to 10 lines of 80 characters, empty line to finish
4. Read a player's bio
5. Add a friend
6. Remove a friend
7. Show friends
8. Chat with a player      type the username, then the message
9. Quit
```

**Receiving a challenge:** while you are in the lobby, a challenge appears as soon as it is sent: `You have been challenged by alice! Accept? (y/n)`. Answering `y` starts the game for both players. When a game starts, every other pending challenge involving either player is cancelled.

**Receiving a chat message:** it appears immediately as `[CHAT] alice hello`, in the lobby and during a game.

### Playing a game

When a game starts, the client shows the game number, your side and your opponent. **Side 0 always plays first**, and the server chose the sides at random.

The board is numbered like this:

```
Side 1 (holes 11 to 6, played right to left on screen)
Side 0 (holes 0 to 5)
```

You own holes **0 to 5** if you are side 0, and holes **6 to 11** if you are side 1.

| Type | Effect |
|---|---|
| a hole number (`0` to `11`) | play that hole, on your turn |
| `R` | resign: your opponent wins |
| `CH` | open the chat with your opponent; type `EXIT` to return to the game |

The server checks every move. A refused move shows the reason (`not your turn`, `illegal move`, `bad format`) and you can play again. After each move, both players see the new board and the scores.

The game ends when a player has captured 25 seeds or more, when the player to move has no legal move, after 300 moves, after a resignation, or when a player disconnects. In the first three cases, each player keeps the seeds left on their side. Both players see the final scores, the winner and the reason, then return to the lobby and can play again.

---

## 5. Server

The server needs no input: start it and leave it running. It writes two files in the folder it is started from:

| File | Contents |
|---|---|
| `users.txt` | one line per account: `id\|username\|rating` |
| `friends.txt` | one line per account: `username\|friend1\|friend2...` |

Both are loaded when the server starts, so accounts and friends survive a restart. **To start from a clean state, stop the server and delete both files.** Bios are kept in memory only and are lost on restart.

Limits: 50 connected clients, 50 accounts, 25 games at the same time, 20 friends per player.

---

## 6. Design

### Files

| File | Role |
|---|---|
| `src/server.c` | Sockets and threads only: accepts clients, one thread per client, receives and sends messages |
| `src/server_logic.c` | All server decisions: accounts, sessions, challenges, games, chat, bio, friends. No networking code |
| `src/client.c` | The client: menus, keyboard, display |
| `src/network.c` | `send_message` / `receive_message`: turn a `Message` into a line of text and back |
| `src/game.c` | Awalé rules, shared by the server and the client |
| `include/protocol.h` | The message format, message codes and limits: the contract between client and server |

### Protocol

Every message is one line of text with five fields:

```
TYPE|RESPONSE|MODE|RECEIVER|CONTENT\n
```

For example `C|-|-|0|bob` (challenge bob), the reply `C|S|-|0|bob`, and the push to Bob `Ch|-|-|0|alice`. Every request gets exactly one reply (`S` success or `F` failure with a reason). The server also sends **pushes** at any time (a challenge, a move, a chat message), which the client handles as they arrive. The full specification of every message is in `PROTOCOL.md`.

Clients only ever name players by username. The server is the only one to decide anything: clients send requests, and the server checks and answers them.

### Server architecture

- **One thread per client.** `server.c` receives a message and calls `logic_handle_message`.
- **An outbox.** The logic never sends anything itself. It fills an outbox with every message the request produces (the reply, plus pushes to other players), and `server.c` sends them afterwards. The logic can therefore be tested without any network: the tests call it directly and read the outbox.
- **A message table.** Each message type is one row: its code, the states in which it is allowed (before login, in the lobby, in a game) and its handler function. Adding a feature means writing one handler and adding one row. Wrong-state and unknown messages are refused in one place.
- **Locks.** All logic data is protected by one lock, and the socket table in `server.c` by another, always taken in the same order, so threads cannot corrupt shared data or deadlock.
- **Generations.** Each session slot has a counter increased on every new connection, so a message meant for a player who just left can never be delivered to a new client who reused the same slot.
- **Moves.** The server runs `game.c` to check each move, then sends only the hole played. Both clients replay it with the same `game.c`, so all copies of the board stay identical.

### Deliberate choices

- **No passwords**, as the brief only asks for a username.
- **A disconnected player loses the game.** A game in progress cannot be resumed after a disconnection. The opponent is told immediately (`left`).
- **Friendship is mutual**: adding a friend adds you to their list too.

---

## 7. Tests

`make test-logic` runs four test programs that call the server logic directly, with no network, and check every reply and push:

| Test | Checks |
|---|---|
| `test_registration` | usernames (empty, too long, illegal characters, duplicates), login, online list, server full, sessions |
| `test_challenge` | challenge, refuse, accept, cancelled challenges, sides, two games at once, disconnection |
| `test_move` | bad format, wrong turn, wrong side, empty hole, alternating turns, board updates |
| `test_chat` | delivery to the right player, spaces and `\|` kept in the text, length limit, messaging yourself |

The tests use their own data files in `target/`, so they never touch the server's `users.txt`.

---

## 8. Known limitations

- The client connects to `127.0.0.1` only: all players must be on the same machine as the server.
- Bios are not saved to disk.
- The online list is shown in its raw form (`alice:L,bob:G`).
- Observer mode, private games, game history and ratings are not implemented.
