/* msg.c - timestamped + encrypted messages on top of the line protocol */
#include "pipechat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int msg_send(int fd, const char *text, int shift)
{
    char wire[IPC_LINE_MAX + 1];

    if (snprintf(wire, sizeof(wire), "%ld|%.*s",
                 (long)time(NULL), MSG_TEXT_MAX, text) < 0) {
        return -1;
    }
    caesar_encrypt(wire, shift);
    return ipc_write_line(fd, wire);
}

int msg_decode(const char *wire, int shift, time_t *stamp,
               char *text, size_t text_size)
{
    char  buf[IPC_LINE_MAX + 1];
    char *bar;

    snprintf(buf, sizeof(buf), "%s", wire);
    caesar_decrypt(buf, shift);

    bar = strchr(buf, '|');
    if (bar == NULL) {
        *stamp = time(NULL);
        snprintf(text, text_size, "%s", buf);
        return -1;
    }
    *bar   = '\0';
    *stamp = (time_t)strtol(buf, NULL, 10);
    snprintf(text, text_size, "%s", bar + 1);
    return 0;
}