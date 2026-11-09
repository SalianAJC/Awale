
# ==========================================
# COMPILER AND ENVIRONMENT
# ==========================================

ifeq ($(OS),Windows_NT)
    CC = gcc
    RUN =
    PATH_PREFIX =
    RM = del /Q target\*.exe target\*.o target\*.d 2>nul
else
    CC = x86_64-w64-mingw32-gcc
    RUN = wine
    PATH_PREFIX = ./
    RM = rm -f target/*.exe target/*.o target/*.d
endif

CFLAGS = -Wall -Wextra -g -Iinclude -MMD -MP
LDLIBS = -lws2_32 -lm

SRC_DIR = src
INC_DIR = include
TARGET_DIR = target

# ==========================================
# SERVER AND CLIENT
# ==========================================

SERVER_SRC = $(SRC_DIR)/server.c $(SRC_DIR)/server_logic.c $(SRC_DIR)/game.c $(SRC_DIR)/network.c
CLIENT_SRC = $(SRC_DIR)/client.c $(SRC_DIR)/network.c $(SRC_DIR)/game.c

SERVER = $(TARGET_DIR)/server.exe
CLIENT = $(TARGET_DIR)/client.exe

all: $(SERVER) $(CLIENT)

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(SERVER): $(SERVER_SRC) | $(TARGET_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(CLIENT): $(CLIENT_SRC) | $(TARGET_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# ==========================================
# RUN APPLICATION
# ==========================================

run: all
	$(RUN) $(PATH_PREFIX)$(SERVER)

run-server: $(SERVER)
	$(RUN) $(PATH_PREFIX)$(SERVER)

run-client: $(CLIENT)
	$(RUN) $(PATH_PREFIX)$(CLIENT)

# ==========================================
# PROTOCOL TEST
# ==========================================

TEST_SRC = tests/test_protocol.c $(SRC_DIR)/network.c
TEST = $(TARGET_DIR)/test_protocol.exe

$(TEST): $(TEST_SRC) | $(TARGET_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test: $(TEST)
	$(RUN) $(PATH_PREFIX)$(TEST)

# ==========================================
# SERVER LOGIC TESTS
# ==========================================

TEST_LOGIC_DIR = tests/test_logic
TEST_LOGIC_SRC = $(SRC_DIR)/server_logic.c $(SRC_DIR)/game.c

# Automatically discover all test_*.c files.
TEST_LOGIC_FILES = $(wildcard $(TEST_LOGIC_DIR)/test_*.c)

# Generate one executable per test source.
TEST_LOGIC_BINS = $(patsubst $(TEST_LOGIC_DIR)/%.c,$(TARGET_DIR)/%.exe,$(TEST_LOGIC_FILES))

# Compile each test with separate persistence files.
$(TARGET_DIR)/%.exe: $(TEST_LOGIC_DIR)/%.c $(TEST_LOGIC_SRC) | $(TARGET_DIR)
	$(CC) $(CFLAGS) \
		-DUSERS_FILE=\"$(TARGET_DIR)/$*_users.txt\" \
		-DFRIENDS_FILE=\"$(TARGET_DIR)/$*_friends.txt\" \
		$^ -o $@ $(LDLIBS)

# Compile and run every logic test.
test-logic: $(TEST_LOGIC_BINS)
	@set -e; \
	if [ -z "$(TEST_LOGIC_BINS)" ]; then \
		echo "No logic tests found in $(TEST_LOGIC_DIR)/"; \
		exit 1; \
	fi; \
	for test in $(TEST_LOGIC_BINS); do \
		echo ""; \
		echo "========================================"; \
		echo "Running $$test"; \
		echo "========================================"; \
		$(RUN) $(PATH_PREFIX)$$test; \
	done; \
	echo ""; \
	echo "All logic tests passed."

# ==========================================
# CLEAN
# ==========================================

clean:
	$(RM)

.PHONY: all run run-server run-client test test-logic clean
