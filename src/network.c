
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "network.h"

// Send every byte, even if send() sends only part of the buffer.
static int send_all(SOCKET socket, const char *buffer, int length)
{
    int sent = 0;

    while (sent < length) {
        int n = send(socket, buffer + sent, length - sent, 0);

        if (n <= 0) {
            return -1;
        }

        sent += n;
    }

    return 0;
}

// Send one complete message.
int send_message(SOCKET socket, const Message *message)
{
    char buffer[MAX_LINE + 1];
    int length;

    if (message == NULL) {
        return -1;
    }

    size_t type_len = strlen(message->type);
    size_t content_len = strlen(message->content);

    if (type_len < 1 || type_len > MAX_TYPE) return -1;
    if (content_len > MAX_CONTENT) return -1;

    // TYPE and CONTENT cannot contain the message-ending character.
    // TYPE cannot contain the field separator.
    if (strchr(message->type, MSG_END) != NULL ||
        strchr(message->type, MSG_SEP) != NULL ||
        strchr(message->content, MSG_END) != NULL) {
        return -1;
    }

    length = snprintf(buffer, sizeof(buffer),
                      "%s%c%c%c%c%c%d%c%s%c",
                      message->type,
                      MSG_SEP,
                      message->response,
                      MSG_SEP,
                      message->mode,
                      MSG_SEP,
                      message->receiver,
                      MSG_SEP,
                      message->content,
                      MSG_END);

    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        return -1;
    }

    return send_all(socket, buffer, length);
}

// Receive and parse one complete message.
int receive_message(SOCKET socket, Message *message)
{
    if (message == NULL) {
        return -1;
    }

    char buffer[MAX_LINE + 1];
    int length = 0;

    // Read until the message-ending character.
    while (length < MAX_LINE) {
        char c;
        int result = recv(socket, &c, 1, 0);

        if (result <= 0) {
            return -1;
        }

        if (c == MSG_END) {
            break;
        }

        buffer[length++] = c;
    }

    if (length == MAX_LINE) {
        return -1;
    }

    buffer[length] = '\0';

    // Split at the first four separators.
    // Any separators inside CONTENT remain untouched.
    char *fields[5];
    fields[0] = buffer;

    int field_count = 1;

    for (int i = 0; i < length; i++) {
        if (buffer[i] == MSG_SEP && field_count < 5) {
            buffer[i] = '\0';
            fields[field_count++] = &buffer[i + 1];
        }
    }

    if (field_count != 5) {
        return -1;
    }

    size_t type_len = strlen(fields[0]);
    size_t content_len = strlen(fields[4]);

    if (type_len < 1 || type_len > MAX_TYPE) return -1;
    if (strlen(fields[1]) != 1) return -1;
    if (strlen(fields[2]) != 1) return -1;
    if (content_len > MAX_CONTENT) return -1;

    // Convert RECEIVER from text to an integer.
    char *end;
    errno = 0;

    long receiver = strtol(fields[3], &end, 10);

    if (errno == ERANGE) return -1;
    if (fields[3][0] == '\0' || *end != '\0') return -1;
    if (receiver < INT_MIN || receiver > INT_MAX) return -1;

    // Validate RESPONSE.
    if (fields[1][0] != RESP_SUCCESS &&
        fields[1][0] != RESP_FAILURE &&
        fields[1][0] != RESP_NONE) {
        return -1;
    }

    // Validate MODE.
    if (fields[2][0] != MODE_OBSERVER &&
        fields[2][0] != MODE_PLAYER &&
        fields[2][0] != MODE_NONE) {
        return -1;
    }

    // Copy the validated fields into the Message structure.
    memcpy(message->type, fields[0], type_len + 1);
    message->response = fields[1][0];
    message->mode = fields[2][0];
    message->receiver = (int)receiver;
    memcpy(message->content, fields[4], content_len + 1);

    return 0;
}
