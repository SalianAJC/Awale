#!/usr/bin/env bash
# Network test: starts the real server, then runs the real client against it
# with different server addresses. Checks that the client reaches the server
# through 127.0.0.1 and through this machine's network address (what another
# computer would use), and handles a bad address or a stopped server.
#
# Run with: make test-network   (after make)

SERVER=target/server.exe
CLIENT=target/client.exe
PORT=8080
TMP=target/network_test
PASSED=0
FAILED=0

# Use Wine on Linux, run directly on Windows (Git Bash / MSYS2)
if command -v wine >/dev/null 2>&1; then
    RUN="wine"
else
    RUN=""
fi

report() {   # report <ok 0/1> <label> <details>
    if [ "$1" -eq 1 ]; then
        printf "PASS  %-50s %s\n" "$2" "$3"
        PASSED=$((PASSED + 1))
    else
        printf "FAIL  %-50s %s\n" "$2" "$3"
        FAILED=$((FAILED + 1))
    fi
}

# run_client <stdin text> <output file> [address]
# Feeds the menus from stdin, gives up after 20 seconds.
run_client() {
    printf "%b" "$1" | timeout 20 $RUN "$CLIENT" $3 > "$2" 2>/dev/null
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    pkill -x "server.exe" 2>/dev/null
    SERVER_PID=""
    sleep 1
}

trap stop_server EXIT

if [ ! -f "$SERVER" ] || [ ! -f "$CLIENT" ]; then
    echo "Build first: make"
    exit 1
fi

mkdir -p "$TMP"

echo "-- Starting the server"

if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then
    echo "Port $PORT is already in use: stop any running server first."
    exit 1
fi

# The server runs in target/network_test, so its users.txt stays there
(cd "$TMP" && exec $RUN "../server.exe" > server_log.txt 2>&1) &
SERVER_PID=$!

# Wait until it accepts connections (Wine can take a few seconds)
up=0
for i in $(seq 1 30); do
    if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then
        up=1
        break
    fi
    sleep 0.5
done

report $up "server listens on port $PORT" ""

if [ $up -eq 0 ]; then
    exit 1
fi

# A fresh username for every run: users.txt keeps old accounts
NAME="net_$(date +%H%M%S)"

#------------------------------------------------------------
echo
echo "-- No address: defaults to 127.0.0.1"

run_client "3\n" "$TMP/default.txt"
grep -q "Connecting to 127.0.0.1:$PORT" "$TMP/default.txt" && grep -q "Connected to server" "$TMP/default.txt"
report $((1 - $?)) "client without argument connects locally" ""

#------------------------------------------------------------
echo
echo "-- Explicit 127.0.0.1"

run_client "3\n" "$TMP/loopback.txt" 127.0.0.1
grep -q "Connected to server" "$TMP/loopback.txt"
report $((1 - $?)) "client 127.0.0.1 connects" ""

#------------------------------------------------------------
echo
echo "-- This machine's network address (what another computer would use)"

LAN_IP=$(hostname -I 2>/dev/null | awk '{print $1}')

if [ -z "$LAN_IP" ]; then
    echo "SKIP  no network address found (not connected to a network?)"
else
    run_client "3\n" "$TMP/lan.txt" "$LAN_IP"
    grep -q "Connecting to $LAN_IP:$PORT" "$TMP/lan.txt" && grep -q "Connected to server" "$TMP/lan.txt"
    report $((1 - $?)) "client $LAN_IP connects" ""

    # A real exchange over that address: register a new account
    run_client "1\n$NAME\n3\n" "$TMP/lan_register.txt" "$LAN_IP"
    grep -q "Registration successful" "$TMP/lan_register.txt"
    report $((1 - $?)) "register over $LAN_IP" "$NAME"

    # Same name again, from the loopback address: the server knows it
    run_client "1\n$NAME\n3\n" "$TMP/lan_duplicate.txt" 127.0.0.1
    grep -q "Username already exists" "$TMP/lan_duplicate.txt"
    report $((1 - $?)) "same server through both addresses" "duplicate refused"
fi

#------------------------------------------------------------
echo
echo "-- Bad addresses"

run_client "3\n" "$TMP/bad_text.txt" not_an_ip
grep -q "Invalid server address: not_an_ip" "$TMP/bad_text.txt"
report $((1 - $?)) "text address refused" ""

run_client "3\n" "$TMP/bad_numbers.txt" 300.1.2.3
grep -q "Invalid server address: 300.1.2.3" "$TMP/bad_numbers.txt"
report $((1 - $?)) "out-of-range address refused" ""

#------------------------------------------------------------
echo
echo "-- Server stopped"

stop_server

run_client "3\n" "$TMP/stopped.txt" 127.0.0.1
grep -q "Connection failed" "$TMP/stopped.txt" && ! grep -q "Connected to server" "$TMP/stopped.txt"
report $((1 - $?)) "no server: Connection failed, clean exit" ""

#------------------------------------------------------------
echo
echo "$PASSED passed, $FAILED failed"
echo "(client outputs are in $TMP/)"

[ $FAILED -eq 0 ]