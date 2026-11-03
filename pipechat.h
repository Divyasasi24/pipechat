/* pipechat.h - single combined header for the whole project
 *
 * Sections: constants | ipc | crypto | messages | stats | tickets | logger |
 *           parent (ticket raise) | child (ticket service)
 */
#ifndef PIPECHAT_H
#define PIPECHAT_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/* ------------------------------ constants ------------------------------- */
#define IPC_LINE_MAX          512                  /* max bytes per pipe line   */
#define MSG_TEXT_MAX          (IPC_LINE_MAX - 32)  /* max chat text per message */
#define TICKET_TEXT_MAX       200
#define LOG_DEFAULT_MODE      0640
#define DEFAULT_CAESAR_SHIFT  3

/* --------------------------------- ipc ---------------------------------- */
/* Accumulates raw bytes from an fd and hands back complete lines. */
typedef struct {
    int    fd;
    char   buf[IPC_LINE_MAX];
    size_t len;
} LineReader;

void    ipc_reader_init(LineReader *reader, int fd);
/* One read() on the fd. Returns bytes read, 0 on EOF, -1 on error. */
ssize_t ipc_fill(LineReader *reader);
/* Pops one complete line (without '\n') into out. Returns 1 if a line was
 * produced, 0 otherwise. out must hold at least IPC_LINE_MAX + 1 bytes.   */
int     ipc_next_line(LineReader *reader, char *out, size_t out_size);
/* Writes text + '\n' completely (handles partial writes / EINTR). */
int     ipc_write_line(int fd, const char *text);

/* ------------------------- crypto (Caesar cipher) ------------------------ */
/* Shifts letters only (A-Z, a-z); digits, spaces and symbols are unchanged.
 * A shift of 0 means "no encryption". Educational use only - not secure.   */
void caesar_encrypt(char *text, int shift);
void caesar_decrypt(char *text, int shift);

/* ------------------- messages (timestamp + encryption) ------------------- */
/* Wire format:  "<unix-time>|<text>"  then the whole line is Caesar-encrypted. */
int msg_send(int fd, const char *text, int shift);
/* Decrypts and splits a wire line. Returns 0 on success, -1 if the line had
 * no timestamp (then the stamp is "now" and text is the decrypted line).    */
int msg_decode(const char *wire, int shift, time_t *stamp,
               char *text, size_t text_size);

/* -------------------------------- stats --------------------------------- */
typedef struct {
    time_t        start_time;
    unsigned long messages_sent;
    unsigned long messages_received;
    unsigned long bytes_sent;
    unsigned long bytes_received;
} ChatStats;

void stats_init(ChatStats *stats);
void stats_record_sent(ChatStats *stats, size_t bytes);
void stats_record_received(ChatStats *stats, size_t bytes);
long stats_uptime(const ChatStats *stats);
void stats_summary(const ChatStats *stats, char *out, size_t out_size);

/* ------------------------------- tickets -------------------------------- */
typedef enum {
    TICKET_OPEN = 0,
    TICKET_IN_PROGRESS,
    TICKET_RESOLVED
} TicketStatus;

typedef struct Ticket {
    int            id;
    char           text[TICKET_TEXT_MAX];
    TicketStatus   status;
    time_t         created_at;
    struct Ticket *next;
} Ticket;

typedef struct {
    Ticket *head;
    Ticket *tail;
    int     next_id;
    int     count;
} TicketQueue;

void        ticket_queue_init(TicketQueue *queue);
Ticket     *ticket_enqueue(TicketQueue *queue, const char *text);
Ticket     *ticket_find(const TicketQueue *queue, int id);
/* Moves the oldest unresolved ticket one step forward (OPEN->IN_PROGRESS->
 * RESOLVED). Returns that ticket, or NULL if nothing is pending.          */
Ticket     *ticket_advance_next(TicketQueue *queue);
int         ticket_pending_count(const TicketQueue *queue);
int         ticket_count_status(const TicketQueue *queue, TicketStatus status);
void        ticket_queue_free(TicketQueue *queue);
const char *ticket_status_name(TicketStatus status);

/* -------------------------------- logger -------------------------------- */
typedef struct {
    int fd;
} Logger;

/* Opens (append) or creates the log. A NEW file gets LOG_DEFAULT_MODE; an
 * EXISTING file keeps whatever permissions it already has.               */
int  logger_open(Logger *logger, const char *path);
void logger_write(Logger *logger, const char *who, const char *fmt, ...)
     __attribute__((format(printf, 3, 4)));
void logger_close(Logger *logger);
/* Copies path -> path.bak preserving mode, owner and timestamps. */
int  logger_backup(const char *path);
/* "YYYY-MM-DD HH:MM:SS" for the current time (out >= 20 bytes). */
void logger_timestamp(char *out, size_t out_size);
/* "HH:MM:SS" for any time value (out >= 9 bytes). */
void logger_clock(time_t when, char *out, size_t out_size);

/* ---------------------- parent: ticket RAISE ------------------------- */
/* to_child: write end of pipe #1.  from_child: read end of pipe #2, or -1 in
 * one-way mode.  shift: Caesar key (0 = off).  Returns the exit code.      */
int run_parent(pid_t child_pid, int to_child, int from_child,
               Logger *logger, int shift);

/* ---------------------- child: ticket SERVICE ------------------------- */
/* in_fd: read end of pipe #1.  out_fd: write end of pipe #2, or -1 in
 * one-way mode (replies are then printed on the terminal).                 */
int run_child(int in_fd, int out_fd, Logger *logger, int shift);

#endif /* PIPECHAT_H */