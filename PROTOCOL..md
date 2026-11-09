# Awale protocol specification

This document is the contract between the client and the server. It describes the protocol **as implemented** in the submitted version. Messages that were designed but not implemented are listed separately in section 9.

---

## 1. Wire format

One message is one line of text:

```
TYPE|RESPONSE|MODE|RECEIVER|CONTENT\n
```

| Field | Values |
|---|---|
| TYPE | 1 or 2 characters, see the message codes below |
| RESPONSE | `S` success reply, `F` failure reply, `-` not a reply (a request or a push) |
| MODE | `P` sent to a player of a game, `O` sent to an observer, `-` otherwise |
| RECEIVER | Reserved. Always `0`, in both directions. Players are always named by username |
| CONTENT | Type-specific text, at most 1024 characters (`MAX_CONTENT`). May be empty. May contain spaces and `\|`, never `\n` |

The receiver splits the line at the first four `|` only, so CONTENT may itself contain `|`. A message whose CONTENT contains `\n` is never sent: `send_message` refuses it.

Every field travels as text, never as a raw struct or pointer, so client and server never depend on each other's memory layout.

---

## 2. Global rules

### 2.1 Requests, replies and pushes

- **Request**: client to server, RESPONSE `-`.
- **Reply**: server to the client that sent the request. **Same TYPE** as the request, RESPONSE `S` or `F`. **Every request gets exactly one reply.**
- **Push**: server to a client that did not ask for it (a challenge, a move, a chat message, a game result). RESPONSE `-`. A client must be ready to receive a push at any moment.

### 2.2 Failure replies

The CONTENT of an `F` reply is a short English reason, meant to be shown to the user as is.

```
R|F|-|0|Username already exists
```

Replies produced before any handler runs:

| Case | Reply |
|---|---|
| Unknown TYPE | `E\|F\|-\|0\|unknown message` |
| Known TYPE, sender not logged in | `<TYPE>\|F\|-\|0\|not logged in` |
| Known TYPE, not allowed in the sender's current state | `<TYPE>\|F\|-\|0\|not allowed now` |

### 2.3 Inside CONTENT

- **Values are separated by one space**: `bob good luck`.
- When the last value is free text (chat), it is **everything after the previous value**, spaces and `|` included.
- **Lists use commas** between items and **colons** inside an item: `alice:L,bob:G`. No trailing comma. An empty list is an empty CONTENT.
- **Usernames**: 1 to 32 characters (`MAX_NAME`), letters, digits and `_` only, case-sensitive. They can therefore never contain a space, `,`, `:`, `;` or `|`.
- **Hole numbers**: absolute, `0` to `11`, the same numbering as `game.c`. Side 0 owns holes 0 to 5, side 1 owns holes 6 to 11.
- **Game ids**: positive integers assigned by the server, starting at 1, never reused while the server runs.

### 2.4 Session states

| State | Meaning |
|---|---|
| `CONNECTED` | Socket open, not logged in |
| `LOBBY` | Logged in, not playing |
| `IN_GAME` | Playing a game |
| `OBSERVING` | Watching a game (reserved, see section 9) |

Which messages are allowed in which state (the server's message table):

| Message | CONNECTED | LOBBY | IN_GAME | OBSERVING |
|---|:-:|:-:|:-:|:-:|
| `R`, `L` | yes | | | |
| `LO`, `M`, `W`, `Rb`, `Fa`, `Fr`, `Fl`, `Rk` | | yes | yes | yes |
| `C`, `A`, `Rf` | | yes | | |
| `Mo`, `Rs` | | | yes | |

### 2.5 Sides and who starts

`game.c` always starts with `current_player = 0`, so **side 0 always plays first**. When a game starts, the server decides at random (`rand() % 2`) which player gets side 0.

---

## 3. Account

There are **no passwords**: the brief only asks for a username. The same account cannot be logged in from two clients at once.

### `R` Register

Creates an account. Does **not** log in.

- Allowed: `CONNECTED`
- Request: `R|-|-|0|<username>`
- Success: `R|S|-|0|registered`

Failures, checked in this order:

| Condition | Reason |
|---|---|
| empty name | `Username can't be empty` |
| more than 32 characters | `Username can't exceed 32 characters` |
| a character other than a letter, digit or `_` | `Username contains illegal characters` |
| name already registered | `Username already exists` |
| 50 accounts already exist (`MAX_CLIENTS`) | `Server is full` |
| `users.txt` could not be written | `could not save account` |

The new account starts with an ELO rating of 1200.

### `L` Login

- Allowed: `CONNECTED`
- Request: `L|-|-|0|<username>`
- Success: `L|S|-|0|<username>`, **immediately followed by an `S` push** with the online list
- State change: `CONNECTED` → `LOBBY`

Failures: the same three username checks as `R`, then:

| Condition | Reason |
|---|---|
| no such account | `unknown user` |
| the account is already logged in from another client | `already connected` |

### `S` Online players (push)

Sent to a client right after its successful login. CONTENT has the same format as the `LO` reply.

```
S|-|-|0|alice:L,bob:G
```

### Logout

There is no logout message: the client closes its socket (section 7).

---

## 4. Lobby

### `LO` List online players

- Request: `LO|-|-|0|` (empty content)
- Success: `LO|S|-|0|<name>:<state>,<name>:<state>,...`
- State letters: `L` in the lobby, `G` in a game, `O` observing
- Only logged-in clients are listed, in session order, including the requester. The list stops before an entry that would not fit in 1024 characters, so a name is never cut in half.

```
LO|S|-|0|alice:L,bob:G,carol:L
```

---

## 5. Challenge

Each player has **at most one outgoing challenge** at a time, but may receive several.

### `C` Challenge

- Allowed: `LOBBY`
- Request: `C|-|-|0|<username of B>`
- Success: `C|S|-|0|<B>`, and a `Ch` push to B

Failures, in this order:

| Condition | Reason |
|---|---|
| the sender already has an outgoing challenge | `challenge already pending` |
| no such account | `unknown user` |
| the sender challenges themselves | `cannot challenge yourself` |
| B is offline | `user offline` |
| B is not in the lobby | `user busy` |

### `Ch` Challenged (push)

```
Ch|-|-|0|<A>
```

### `Rf` Refuse

- Allowed: `LOBBY`
- Request (from B): `Rf|-|-|0|<A>`
- Success: `Rf|S|-|0|<A>`, and an `Rf` push to A: `Rf|-|-|0|<B>`
- Failure: `no challenge from this user` (A unknown, offline, or did not challenge B)

### `A` Accept

- Allowed: `LOBBY`
- Request (from B): `A|-|-|0|<A>`
- Success: `A|S|-|0|<A>`, then a `Gs` push to each player
- State change: both players `LOBBY` → `IN_GAME`

Failures, in this order:

| Condition | Reason |
|---|---|
| A unknown, offline, or did not challenge B | `no challenge from this user` |
| A is no longer in the lobby | `user busy` |
| 25 games already running (`MAX_GAMES`) | `server full` |

**Cancellations.** When a game starts, every other pending challenge involving A or B is cancelled. Anyone who had challenged A or B receives an `Rf` push from that player, as if refused. B's own outgoing challenge, if any, is dropped without a message.

### `Gs` Game start (push)

Sent to both players with mode `P`. Each player receives **their own** side.

```
Gs|-|P|0|<game_id> <your_side> <opponent>
```

```
to alice:  Gs|-|P|0|3 1 bob       alice is side 1
to bob:    Gs|-|P|0|3 0 alice     bob is side 0, so bob plays first
```

The board is not sent: every game starts with 4 seeds in each hole.

---

## 6. Game

### `Mo` Move

- Allowed: `IN_GAME`
- Request: `Mo|-|P|0|<hole>`, hole `0` to `11`
- Success: `Mo|S|P|0|<hole>`. This is only an acknowledgement: **clients apply a move only when the `Bu` push arrives**, the mover included.
- Then: a `Bu` push to both players, then a `Go` push if the move ended the game

Failures, in this order (replies use mode `P`):

| Condition | Reason |
|---|---|
| not a whole number from 0 to 11 | `bad format` |
| it is not the sender's turn | `not your turn` |
| `sow()` refuses it: hole on the wrong side, empty hole, the opponent would be starved, or 300 moves reached | `illegal move` |

### `Bu` Board update (push)

```
Bu|-|P|0|<game_id> <hole>
```

Sent to both players after every legal move. Every receiver plays the move with `sow()` on its own copy of the game. Server and clients run the same `game.c`, so all copies of the board stay identical, and only one number travels per move.

### `Rs` Resign

- Allowed: `IN_GAME`
- Request: `Rs|-|P|0|`
- Success: `Rs|S|P|0|`, then a `Go` push to both players, reason `resign`, the opponent wins

### `Go` Game over (push)

```
Go|-|P|0|<game_id> <captures_side0> <captures_side1> <winner> <reason>
```

- `<winner>` is a username, or `draw`.
- `<reason>`:

| Reason | Meaning |
|---|---|
| `end` | Ended by the rules (`check_game_over` in `game.c`): a player captured 25 seeds or more, the player to move has no legal move, or 300 moves were played. Each player then keeps the seeds left on their side |
| `resign` | A player resigned |
| `left` | A player disconnected (section 7) |

- Sent to both players, except one who has just disconnected.
- The ELO ratings are updated before it is sent (section 8).
- State change: both players go back to `LOBBY` and can challenge again.

```
Go|-|P|0|3 27 21 alice end
```

---

## 7. Disconnection

When `receive_message` fails on a client's socket, the server treats the client as gone:

1. **In a game**: the game ends and the remaining player wins. They receive `Go ... <winner> left`. The ratings are updated: leaving counts as a loss.
2. **Challenges**: everyone who had challenged the leaver receives an `Rf` push from them. The leaver's own outgoing challenge is dropped.
3. The account is kept and marked offline. The player can log in again from a new client.

**A game in progress cannot be resumed after a disconnection.** This is a deliberate choice: it keeps the server's state simple, and the remaining player is never left waiting.

---

## 8. Chat, bio, friends, ranking

### `M` Chat

The same message in the lobby and in a game. The client always names the recipient. In a game, the client fills in the opponent's name itself, so the user only types the text.

- Allowed: any logged-in state
- Request: `M|-|-|0|<to> <text>`
- Success: `M|S|-|0|<to>`, and a push to the recipient: `M|-|-|0|<from> <text>`
- The **server** writes the sender's name, so nobody can impersonate another player.
- Sending a message to yourself is allowed.

Failures, in this order:

| Condition | Reason |
|---|---|
| no space, empty name, or name longer than 32 characters | `bad format` |
| empty text | `empty message` |
| text longer than 200 characters (`MAX_CHAT`) | `message too long` |
| no such account | `unknown user` |
| recipient offline | `user offline` |

```
alice -> server   M|-|-|0|bob good luck | have fun
server -> alice   M|S|-|0|bob
server -> bob     M|-|-|0|alice good luck | have fun
```

### `W` Write bio

A bio is up to **10 lines** of up to **80 characters**. Since `\n` ends a message, **lines are separated by `;`**, on the wire and in storage.

- Request: `W|-|-|0|<line1>;<line2>;...`
- Success: `W|S|-|0|bio updated`. A new bio replaces the old one. An empty CONTENT erases the bio.

| Condition | Reason |
|---|---|
| longer than the bio buffer | `bio too long` |
| an empty line (`a;;b`) | `empty bio line` |
| more than 10 lines | `too many bio lines` |
| a line longer than 80 characters | `bio line too long` |

Bios are kept in memory only, and are lost when the server restarts.

### `Rb` Read bio

- Request: `Rb|-|-|0|<username>`
- Success: `Rb|S|-|0|<bio>`, with lines still separated by `;`. The **client** turns each `;` into a line break when it prints the bio. If the player has no bio: `Rb|S|-|0|no bio`.
- Failure: `unknown user`
- The player does not need to be online.

```
Rb|S|-|0|Hi, I'm Alice;I play Awale every day
```

### `Fa`, `Fr`, `Fl` Friends

Friendship is **mutual**: adding Bob adds Bob to your list and you to his, without his confirmation. Removing works the same way on both lists. Friends are saved to `friends.txt`.

| Message | Request | Success | Failures |
|---|---|---|---|
| `Fa` add | `Fa\|-\|-\|0\|<username>` | `Fa\|S\|-\|0\|<username>` | `unknown user`, `cannot add yourself`, `already friends`, `friend list full` (20 per player), `could not save friends` |
| `Fr` remove | `Fr\|-\|-\|0\|<username>` | `Fr\|S\|-\|0\|<username>` | `unknown user`, `cannot remove yourself`, `not friends`, `could not save friends` |
| `Fl` list | `Fl\|-\|-\|0\|` | `Fl\|S\|-\|0\|<name>,<name>,...` | none |

### `Rk` Ranking

- Request: `Rk|-|-|0|`
- Success: `Rk|S|-|0|<name>:<rating>,<name>:<rating>,...`, every registered account (online or not), highest rating first. The list stops before an entry that would not fit in 1024 characters.

```
Rk|S|-|0|bob:1216,carol:1200,alice:1184
```

**ELO rules.** Every account starts at 1200. At the end of every game, whatever the reason, the server updates both players with K = 32:

```
expected score of A:  Ea = 1 / (1 + 10^((Rb - Ra) / 400))
actual score of A:    Sa = 1 (win), 0.5 (draw), 0 (loss)
new rating of A:      Ra' = Ra + 32 × (Sa − Ea), rounded to the nearest integer
```

B is updated symmetrically, from the ratings before the game. A resignation and a disconnection both count as a loss. Ratings are saved to `users.txt` after every game.

---

## 9. Designed but not implemented

These codes exist in `protocol.h` and were specified during the design, but no handler implements them. The server answers them with `E|F|-|0|unknown message`.

| Code | Purpose |
|---|---|
| `LG` | List running games: `<id>:<side0>:<side1>:<pub\|priv>,...` |
| `O`, `Ox` | Start / stop observing a game. Observers would receive `Bu` and `Go` with mode `O` |
| `Pv` | Private mode on/off: only friends may observe your games |
| `Hl`, `RG` | List finished games, and replay one move by move |

The server is already prepared for observers: games keep their move list and an observer list, `Bu` and `Go` are sent to observers too, and a disconnecting observer is removed from its game.

Unused codes: `Od` (replaced by `Go` with reason `left`) and `Sh` (`RG` would reply with its own type).

---

## 10. What the server stores

All of this lives in `server_logic.c`, protected by one lock.

**Account** (kept after disconnecting, at most 50)
- player id, username, ELO rating: saved in `users.txt` as `id|username|rating`
- friends (player ids, mutual): saved in `friends.txt` as `username|friend1|friend2...`
- bio: memory only
- session index, or -1 if offline

Both files are loaded when the server starts. Deleting them, with the server stopped, resets every account.

**Session** (one per connected client, at most 50)
- account index (-1 if not logged in), state, outgoing challenge (-1 if none), current game (-1 if none)
- a generation counter, increased on every new connection, so a message meant for a client who just left is never delivered to a new client reusing the same slot

**Match** (one per running game, at most 25)
- game id, the `Game` from `game.h`, the account on each side, the moves played, the observers

---

## 11. Example scenarios

### 11.1 Register, log in, list

```
alice -> R|-|-|0|alice          <- R|S|-|0|registered
alice -> L|-|-|0|alice          <- L|S|-|0|alice
                                <- S|-|-|0|alice:L,bob:L
alice -> LO|-|-|0|              <- LO|S|-|0|alice:L,bob:L
```

### 11.2 Challenge, game, resignation

```
alice -> C|-|-|0|bob            <- alice: C|S|-|0|bob
                                <- bob:   Ch|-|-|0|alice
bob   -> A|-|-|0|alice          <- bob:   A|S|-|0|alice
                                <- alice: Gs|-|P|0|1 1 bob
                                <- bob:   Gs|-|P|0|1 0 alice
bob   -> Mo|-|P|0|8             <- bob:   Mo|F|P|0|illegal move
bob   -> Mo|-|P|0|2             <- bob:   Mo|S|P|0|2
                                <- alice: Bu|-|P|0|1 2
                                <- bob:   Bu|-|P|0|1 2
bob   -> Mo|-|P|0|3             <- bob:   Mo|F|P|0|not your turn
alice -> Rs|-|P|0|              <- alice: Rs|S|P|0|
                                <- alice: Go|-|P|0|1 0 0 bob resign
                                <- bob:   Go|-|P|0|1 0 0 bob resign
```

### 11.3 Challenge refused

```
alice -> C|-|-|0|bob            <- alice: C|S|-|0|bob
                                <- bob:   Ch|-|-|0|alice
bob   -> Rf|-|-|0|alice         <- bob:   Rf|S|-|0|alice
                                <- alice: Rf|-|-|0|bob
```

### 11.4 A player disconnects mid-game

```
(alice's socket closes)         <- bob:   Go|-|P|0|1 12 9 bob left
```
