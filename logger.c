/* logger.c - timestamped chat/audit log with permission preservation */
#define _GNU_SOURCE
#include "pipechat.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void logger_timestamp(char *out, size_t out_size)
{
    time_t    now = time(NULL);
    struct tm tm_now;

    localtime_r(&now, &tm_now);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &tm_now);
}

void logger_format_clock(time_t when, char *out, size_t out_size)
{
    struct tm tm_when;

    localtime_r(&when, &tm_when);
    strftime(out, out_size, "%H:%M:%S", &tm_when);
}

int logger_open(Logger *logger, const char *path)
{
    struct stat st;
    int         existed = (stat(path, &st) == 0);
    int         fd = open(path, O_WRONLY | O_APPEND | O_CREAT, LOG_DEFAULT_MODE);

    if (fd < 0) {
        return -1;
    }
    if (!existed) {
        /* make the mode independent of the user's umask */
        if (fchmod(fd, LOG_DEFAULT_MODE) != 0) {
            /* non-fatal */
        }
    }
    logger->fd = fd;
    return 0;
}

void logger_write(Logger *logger, const char *who, const char *fmt, ...)
{
    char    body[512];
    char    line[768];
    char    stamp[32];
    va_list args;

    if (logger->fd < 0) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    logger_timestamp(stamp, sizeof(stamp));
    int len = snprintf(line, sizeof(line), "[%s] [pid %d] %-8s %s\n",
                       stamp, (int)getpid(), who, body);
    if (len > 0) {
        size_t to_write = ((size_t)len < sizeof(line)) ? (size_t)len
                                                       : sizeof(line) - 1;
        /* one write() on an O_APPEND fd keeps lines from both processes intact */
        if (write(logger->fd, line, to_write) < 0) {
            /* logging must never kill the chat */
        }
    }
}

void logger_close(Logger *logger)
{
    if (logger->fd >= 0) {
        close(logger->fd);
        logger->fd = -1;
    }
}

int logger_backup(const char *path)
{
    char        dst_path[1024];
    char        buf[4096];
    struct stat st;
    ssize_t     n;
    int         src;
    int         dst;

    snprintf(dst_path, sizeof(dst_path), "%s.bak", path);

    src = open(path, O_RDONLY);
    if (src < 0) {
        return -1;
    }
    if (fstat(src, &st) != 0) {
        close(src);
        return -1;
    }
    dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (dst < 0) {
        close(src);
        return -1;
    }

    while ((n = read(src, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(dst, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(src);
                close(dst);
                return -1;
            }
            off += w;
        }
    }

    /* preserve permissions, ownership and timestamps */
    fchmod(dst, st.st_mode & 07777);
    if (fchown(dst, st.st_uid, st.st_gid) != 0) {
        /* only root can always do this; ignore otherwise */
    }
    struct timespec times[2] = { st.st_atim, st.st_mtim };
    futimens(dst, times);

    close(src);
    close(dst);
    return (n < 0) ? -1 : 0;
}

void logger_clock(time_t when, char *out, size_t out_size)
{
    struct tm tm_when;

    localtime_r(&when, &tm_when);
    strftime(out, out_size, "%H:%M:%S", &tm_when);
}