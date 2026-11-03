/* parent.c - ticket RAISE side (parent process)
 *
 * Raises tickets: reads what the customer types, stamps + encrypts it and sends it to the
 * child through pipe #1. Replies arrive on pipe #2 and are decrypted and
 * shown with the time they were sent. poll() watches stdin and the reply
 * pipe together, so updates appear while the user is typing (full duplex). */
#include "pipechat.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    int       to_child;
    int       shift;
    Logger   *logger;
    ChatStats stats;
} ParentContext;

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_sigint(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static void print_prompt(void)
{
    printf("You> ");
    fflush(stdout);
}

static void print_local_stats(const ParentContext *ctx)
{
    printf("\r\033[K--- Ticket Raise statistics ---\n");
    printf("Messages sent     : %lu (%lu bytes)\n",
           ctx->stats.messages_sent, ctx->stats.bytes_sent);
    printf("Replies received  : %lu (%lu bytes)\n",
           ctx->stats.messages_received, ctx->stats.bytes_received);
    printf("Session time      : %ld s\n", stats_uptime(&ctx->stats));
    if (ctx->shift != 0) {
        printf("Encryption        : Caesar cipher, shift %d\n", ctx->shift);
    } else {
        printf("Encryption        : off\n");
    }
}

static void show_reply(ParentContext *ctx, const char *wire, int reprint_prompt)
{
    char   text[IPC_LINE_MAX + 1];
    char   clock[16];
    time_t sent_at;

    msg_decode(wire, ctx->shift, &sent_at, text, sizeof(text));
    stats_record_received(&ctx->stats, strlen(text));
    logger_clock(sent_at, clock, sizeof(clock));

    printf("\r\033[K[%s] [Ticket Service] %s\n", clock, text);
    if (reprint_prompt) {
        print_prompt();
    }
}

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
    return text;
}

/* Returns 1 to keep running, 0 to quit. */
static int handle_user_line(ParentContext *ctx, char *raw)
{
    char *msg = trim(raw);

    if (*msg == '\0') {
        print_prompt();
        return 1;
    }
    if (strcmp(msg, "/quit") == 0 || strcmp(msg, "/exit") == 0) {
        return 0;
    }
    if (strcmp(msg, "/stats") == 0) {
        print_local_stats(ctx);           /* our half; child sends its half */
    }

    logger_write(ctx->logger, "RAISE", "%s", msg);
    if (msg_send(ctx->to_child, msg, ctx->shift) != 0) {
        fprintf(stderr, "\n[Ticket Raise] Ticket Service is not reachable.\n");
        return 0;
    }
    stats_record_sent(&ctx->stats, strlen(msg));
    print_prompt();
    return 1;
}

static void print_banner(int full_duplex, int shift)
{
    puts("=== Customer Support Ticket Raise (Parent) ===");
    puts(full_duplex ? "Mode: two-way (full duplex, two pipes)"
                     : "Mode: one-way (parent -> child only)");
    if (shift != 0) {
        printf("Messages are timestamped and Caesar-encrypted (shift %d)\n", shift);
    } else {
        puts("Messages are timestamped (encryption off)");
    }
    puts("Type a message to open a ticket. Commands: /help /list /stats "
         "/status <id> /resolve <id> /quit");
}

int run_parent(pid_t child_pid, int to_child, int from_child,
               Logger *logger, int shift)
{
    struct sigaction sa;
    ParentContext    ctx;
    LineReader       user_input;
    LineReader       replies;
    char             line[IPC_LINE_MAX + 1];
    char             summary[256];
    int              reply_open = (from_child >= 0);
    int              running    = 1;
    int              status     = 0;

    ctx.to_child = to_child;
    ctx.shift    = shift;
    ctx.logger   = logger;
    stats_init(&ctx.stats);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;        /* no SA_RESTART: poll() must wake up */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    ipc_reader_init(&user_input, STDIN_FILENO);
    ipc_reader_init(&replies, from_child);

    print_banner(reply_open, shift);
    print_prompt();

    while (running && !g_stop_requested) {
        struct pollfd fds[2];

        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = reply_open ? from_child : -1;   /* -1 => ignored by poll */
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        if (fds[0].revents & (POLLIN | POLLHUP)) {
            if (ipc_fill(&user_input) <= 0) {
                running = 0;                          /* Ctrl-D */
            }
            while (running && ipc_next_line(&user_input, line, sizeof(line))) {
                running = handle_user_line(&ctx, line);
            }
        }

        if (reply_open && (fds[1].revents & (POLLIN | POLLHUP))) {
            if (ipc_fill(&replies) <= 0) {
                reply_open = 0;
                running = 0;
                puts("\n[Ticket Raise] Ticket Service closed the connection.");
            }
            while (ipc_next_line(&replies, line, sizeof(line))) {
                show_reply(&ctx, line, 1);
            }
        }
    }

    /* Shutdown: closing our write end gives the child EOF, so it finishes the
     * queue, sends final updates and exits. We keep reading until it is done. */
    printf("\n[Ticket Raise] Closing session...\n");
    close(to_child);

    if (reply_open) {
        while (ipc_fill(&replies) > 0) {
            while (ipc_next_line(&replies, line, sizeof(line))) {
                show_reply(&ctx, line, 0);
            }
        }
        close(from_child);
    }

    print_local_stats(&ctx);
    stats_summary(&ctx.stats, summary, sizeof(summary));
    logger_write(logger, "STATS", "raise: %s", summary);

    if (waitpid(child_pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}