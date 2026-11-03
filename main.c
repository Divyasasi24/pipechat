/* main.c - Pipe-Based Parent-Child Chat for a Customer Support Ticket
 * *          Processing Communication System (Linux)
 *   Parent = Ticket Raise, Child = Ticket Service
 *
 *   pipe #1 (parent -> child) : customer messages / commands
 *   pipe #2 (child -> parent) : ticket confirmations and status updates
 *
 * Extensions: message timestamping, chat statistics, Caesar-cipher encryption.
 */
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pipechat.h"

#define DEFAULT_LOG_FILE "chat.log"

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-l logfile] [-k shift] [-o] [-h]\n"
            "  -l logfile  chat log path (default: %s)\n"
            "  -k shift    Caesar cipher key 0-25, 0 = no encryption (default: %d)\n"
            "  -o          one-way mode: parent sends, child only prints (Level 1)\n"
            "  -h          show this help\n",
            prog, DEFAULT_LOG_FILE, DEFAULT_CAESAR_SHIFT);
}

int main(int argc, char *argv[])
{
    const char *log_path = DEFAULT_LOG_FILE;
    int         shift    = DEFAULT_CAESAR_SHIFT;
    int         one_way  = 0;
    int         opt;
    int         parent_to_child[2];
    int         child_to_parent[2] = { -1, -1 };
    Logger      logger;
    pid_t       pid;
    int         exit_code;

    while ((opt = getopt(argc, argv, "l:k:oh")) != -1) {
        switch (opt) {
        case 'l': log_path = optarg; break;
        case 'k':
            shift = atoi(optarg);
            if (shift < 0 || shift > 25) {
                fprintf(stderr, "Key must be between 0 and 25.\n");
                return 1;
            }
            break;
        case 'o': one_way = 1;       break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    signal(SIGPIPE, SIG_IGN);             /* handle EPIPE via write() errors */

    if (pipe(parent_to_child) < 0) {
        perror("pipe");
        return 1;
    }
    if (!one_way && pipe(child_to_parent) < 0) {
        perror("pipe");
        return 1;
    }
    if (logger_open(&logger, log_path) < 0) {
        perror(log_path);
        return 1;
    }

    fflush(NULL);                         /* avoid duplicated stdio buffers */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {                       /* ---- child: ticket service ---- */
        close(parent_to_child[1]);        /* child only reads pipe #1  */
        if (!one_way) {
            close(child_to_parent[0]);    /* child only writes pipe #2 */
        }
        exit_code = run_child(parent_to_child[0],
                              one_way ? -1 : child_to_parent[1], &logger, shift);
        logger_close(&logger);
        _exit(exit_code);
    }

    /* -------------------- parent: ticket raise ----------------------- */
    close(parent_to_child[0]);            /* parent only writes pipe #1 */
    if (!one_way) {
        close(child_to_parent[1]);        /* parent only reads pipe #2  */
    }

    logger_write(&logger, "SYSTEM", "session started (child pid %d, %s, caesar shift %d)",
                 (int)pid, one_way ? "one-way" : "two-way", shift);
    exit_code = run_parent(pid, parent_to_child[1],
                           one_way ? -1 : child_to_parent[0], &logger, shift);
    logger_write(&logger, "SYSTEM", "session ended (exit code %d)", exit_code);
    logger_close(&logger);

    if (logger_backup(log_path) == 0) {
        printf("[Ticket Raise] Log saved: %s (backup: %s.bak, permissions preserved)\n",
               log_path, log_path);
    }
    return exit_code;
}