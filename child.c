/* child.c - ticket SERVICE side (child process)
 *
 * Receives encrypted, timestamped messages from the parent. Plain text opens
 * a new ticket placed in a FIFO queue; a timer then advances tickets
 * OPEN -> IN_PROGRESS -> RESOLVED and pushes status updates back through
 * pipe #2. /stats reports chat and ticket statistics.                      */
#include "pipechat.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define POLL_TIMEOUT_MS         500
#define PROCESS_INTERVAL_SEC    4

typedef struct {
    int         out_fd;      /* -1 => one-way mode */
    int         shift;
    Logger     *logger;
    TicketQueue queue;
    ChatStats   stats;
} ChildContext;

static void send_reply(ChildContext *ctx, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void send_reply(ChildContext *ctx, const char *fmt, ...)
{
    char    text[MSG_TEXT_MAX + 1];
    va_list args;

    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    logger_write(ctx->logger, "SERVICE", "%s", text);
    if (ctx->out_fd >= 0) {
        if (msg_send(ctx->out_fd, text, ctx->shift) == 0) {
            stats_record_sent(&ctx->stats, strlen(text));
        }
    } else {
        char clock[16];
        logger_clock(time(NULL), clock, sizeof(clock));
        printf("[%s] [Ticket Service] %s\n", clock, text);
        fflush(stdout);
        stats_record_sent(&ctx->stats, strlen(text));
    }
}

static void announce_ticket(ChildContext *ctx, const Ticket *t, const char *prefix)
{
    char opened[16];

    logger_clock(t->created_at, opened, sizeof(opened));
    send_reply(ctx, "%sTicket #%d [%s] (opened %s): %s", prefix, t->id,
               ticket_status_name(t->status), opened, t->text);
}

static void cmd_help(ChildContext *ctx)
{
    send_reply(ctx, "Type any text to open a ticket.");
    send_reply(ctx, "/list  /stats  /status <id>  /resolve <id>  /quit");
}

static void cmd_list(ChildContext *ctx)
{
    if (ctx->queue.count == 0) {
        send_reply(ctx, "No tickets yet.");
        return;
    }
    for (const Ticket *t = ctx->queue.head; t != NULL; t = t->next) {
        announce_ticket(ctx, t, "");
    }
}

static void cmd_stats(ChildContext *ctx)
{
    send_reply(ctx, "--- Ticket Service statistics ---");
    send_reply(ctx, "Messages received : %lu (%lu bytes)",
               ctx->stats.messages_received, ctx->stats.bytes_received);
    send_reply(ctx, "Replies sent      : %lu (%lu bytes)",
               ctx->stats.messages_sent, ctx->stats.bytes_sent);
    send_reply(ctx, "Tickets           : %d total, %d open, %d in progress, %d resolved",
               ctx->queue.count,
               ticket_count_status(&ctx->queue, TICKET_OPEN),
               ticket_count_status(&ctx->queue, TICKET_IN_PROGRESS),
               ticket_count_status(&ctx->queue, TICKET_RESOLVED));
    send_reply(ctx, "Uptime            : %ld s", stats_uptime(&ctx->stats));
}

static void cmd_status(ChildContext *ctx, const char *arg)
{
    int     id;
    Ticket *t;

    if (sscanf(arg, "%d", &id) != 1 || (t = ticket_find(&ctx->queue, id)) == NULL) {
        send_reply(ctx, "Ticket not found. Usage: /status <id>");
        return;
    }
    announce_ticket(ctx, t, "");
}

static void cmd_resolve(ChildContext *ctx, const char *arg)
{
    int     id;
    Ticket *t;

    if (sscanf(arg, "%d", &id) != 1 || (t = ticket_find(&ctx->queue, id)) == NULL) {
        send_reply(ctx, "Ticket not found. Usage: /resolve <id>");
        return;
    }
    t->status = TICKET_RESOLVED;
    announce_ticket(ctx, t, "Closed by request - ");
}

static void handle_message(ChildContext *ctx, const char *line)
{
    if (strcmp(line, "/help") == 0) {
        cmd_help(ctx);
    } else if (strcmp(line, "/list") == 0) {
        cmd_list(ctx);
    } else if (strcmp(line, "/stats") == 0) {
        cmd_stats(ctx);
    } else if (strncmp(line, "/status ", 8) == 0) {
        cmd_status(ctx, line + 8);
    } else if (strncmp(line, "/resolve ", 9) == 0) {
        cmd_resolve(ctx, line + 9);
    } else if (line[0] == '/') {
        send_reply(ctx, "Unknown command. Type /help.");
    } else {
        Ticket *t = ticket_enqueue(&ctx->queue, line);
        if (t == NULL) {
            send_reply(ctx, "Internal error: could not create ticket.");
            return;
        }
        send_reply(ctx, "Ticket #%d created (%s). Tickets pending: %d",
                   t->id, ticket_status_name(t->status),
                   ticket_pending_count(&ctx->queue));
    }
}

static void process_next_ticket(ChildContext *ctx)
{
    Ticket *t = ticket_advance_next(&ctx->queue);
    if (t != NULL) {
        announce_ticket(ctx, t, "Update: ");
    }
}

/* Decrypt one wire line, log it (plain text + what travelled on the pipe). */
static void receive_wire(ChildContext *ctx, const char *wire)
{
    char   text[IPC_LINE_MAX + 1];
    char   clock[16];
    time_t sent_at;

    msg_decode(wire, ctx->shift, &sent_at, text, sizeof(text));
    stats_record_received(&ctx->stats, strlen(text));
    logger_clock(sent_at, clock, sizeof(clock));
    logger_write(ctx->logger, "RECEIVED", "%s  [sent %s, wire: %s]",
                 text, clock, wire);
    handle_message(ctx, text);
}

int run_child(int in_fd, int out_fd, Logger *logger, int shift)
{
    ChildContext ctx;
    LineReader   reader;
    char         line[IPC_LINE_MAX + 1];
    char         summary[256];
    time_t       last_tick = time(NULL);

    signal(SIGINT, SIG_IGN);              /* parent coordinates shutdown */

    ctx.out_fd = out_fd;
    ctx.shift  = shift;
    ctx.logger = logger;
    ticket_queue_init(&ctx.queue);
    stats_init(&ctx.stats);
    ipc_reader_init(&reader, in_fd);

    send_reply(&ctx, "Ticket Service online. Type /help for commands.");

    for (;;) {
        struct pollfd pfd = { .fd = in_fd, .events = POLLIN, .revents = 0 };
        int           rc  = poll(&pfd, 1, POLL_TIMEOUT_MS);

        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc > 0) {
            if (ipc_fill(&reader) <= 0) {
                break;                    /* parent closed the pipe */
            }
            while (ipc_next_line(&reader, line, sizeof(line))) {
                receive_wire(&ctx, line);
            }
        }
        if (time(NULL) - last_tick >= PROCESS_INTERVAL_SEC) {
            process_next_ticket(&ctx);
            last_tick = time(NULL);
        }
    }

    /* EOF from parent: finish whatever is still pending, then report. */
    send_reply(&ctx, "Session ending - flushing %d pending ticket(s).",
               ticket_pending_count(&ctx.queue));
    while (ticket_pending_count(&ctx.queue) > 0) {
        process_next_ticket(&ctx);
    }
    cmd_stats(&ctx);
    send_reply(&ctx, "Ticket Service offline. Tickets handled this session: %d.",
               ctx.queue.count);

    stats_summary(&ctx.stats, summary, sizeof(summary));
    logger_write(logger, "STATS", "service: %s", summary);

    ticket_queue_free(&ctx.queue);
    close(in_fd);
    if (out_fd >= 0) {
        close(out_fd);
    }
    return 0;
}