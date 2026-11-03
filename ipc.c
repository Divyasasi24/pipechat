/* ipc.c - line-based helpers on top of pipe file descriptors */
#include "pipechat.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

void ipc_reader_init(LineReader *reader, int fd)
{
    reader->fd  = fd;
    reader->len = 0;
}

ssize_t ipc_fill(LineReader *reader)
{
    size_t  space = sizeof(reader->buf) - reader->len;
    ssize_t n;

    if (space == 0) {
        return 1; /* buffer full: caller must drain it first */
    }
    do {
        n = read(reader->fd, reader->buf + reader->len, space);
    } while (n < 0 && errno == EINTR);

    if (n > 0) {
        reader->len += (size_t)n;
    }
    return n;
}

int ipc_next_line(LineReader *reader, char *out, size_t out_size)
{
    char  *newline = memchr(reader->buf, '\n', reader->len);
    size_t line_len;
    size_t consume;

    if (newline != NULL) {
        line_len = (size_t)(newline - reader->buf);
        consume  = line_len + 1;
    } else if (reader->len == sizeof(reader->buf)) {
        line_len = reader->len;           /* over-long line: force a split */
        consume  = reader->len;
    } else {
        return 0;
    }

    if (line_len >= out_size) {
        line_len = out_size - 1;
    }
    memcpy(out, reader->buf, line_len);
    out[line_len] = '\0';
    if (line_len > 0 && out[line_len - 1] == '\r') {
        out[line_len - 1] = '\0';
    }

    memmove(reader->buf, reader->buf + consume, reader->len - consume);
    reader->len -= consume;
    return 1;
}

int ipc_write_line(int fd, const char *text)
{
    char   buf[IPC_LINE_MAX + 2];
    size_t len = strlen(text);
    size_t off = 0;

    if (len > IPC_LINE_MAX) {
        len = IPC_LINE_MAX;
    }
    memcpy(buf, text, len);
    buf[len++] = '\n';

    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;                    /* EPIPE etc. */
        }
        off += (size_t)n;
    }
    return 0;
}