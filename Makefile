
CC = gcc

CFLAGS = -Wall -Wextra

all: server.exe client.exe


server.exe: src/server.c src/protocol.h src/game.c src/game.h
	$(CC) $(CFLAGS) -o server.exe src/server.c src/game.c -lws2_32

client.exe: src/client.c src/protocol.h src/game.c src/game.h
	$(CC) $(CFLAGS) -o client.exe src/client.c src/game.c -lws2_32


clean:
	del /Q server.exe client.exe 2>NUL
