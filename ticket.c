/* ticket.c - FIFO queue of support tickets (singly linked list) */
#include "pipechat.h"

#include <stdio.h>
#include <stdlib.h>

void ticket_queue_init(TicketQueue *queue)
{
    queue->head    = NULL;
    queue->tail    = NULL;
    queue->next_id = 1;
    queue->count   = 0;
}

Ticket *ticket_enqueue(TicketQueue *queue, const char *text)
{
    Ticket *ticket = calloc(1, sizeof(*ticket));
    if (ticket == NULL) {
        return NULL;
    }
    ticket->id         = queue->next_id++;
    ticket->status     = TICKET_OPEN;
    ticket->created_at = time(NULL);
    snprintf(ticket->text, sizeof(ticket->text), "%s", text);

    if (queue->tail != NULL) {
        queue->tail->next = ticket;
    } else {
        queue->head = ticket;
    }
    queue->tail = ticket;
    queue->count++;
    return ticket;
}

Ticket *ticket_find(const TicketQueue *queue, int id)
{
    for (Ticket *t = queue->head; t != NULL; t = t->next) {
        if (t->id == id) {
            return t;
        }
    }
    return NULL;
}

Ticket *ticket_advance_next(TicketQueue *queue)
{
    for (Ticket *t = queue->head; t != NULL; t = t->next) {
        if (t->status != TICKET_RESOLVED) {
            t->status = (t->status == TICKET_OPEN) ? TICKET_IN_PROGRESS
                                                   : TICKET_RESOLVED;
            return t;
        }
    }
    return NULL;
}

int ticket_pending_count(const TicketQueue *queue)
{
    int pending = 0;
    for (Ticket *t = queue->head; t != NULL; t = t->next) {
        if (t->status != TICKET_RESOLVED) {
            pending++;
        }
    }
    return pending;
}

int ticket_count_status(const TicketQueue *queue, TicketStatus status)
{
    int total = 0;
    for (const Ticket *t = queue->head; t != NULL; t = t->next) {
        if (t->status == status) {
            total++;
        }
    }
    return total;
}

void ticket_queue_free(TicketQueue *queue)
{
    Ticket *t = queue->head;
    while (t != NULL) {
        Ticket *next = t->next;
        free(t);
        t = next;
    }
    queue->head  = NULL;
    queue->tail  = NULL;
    queue->count = 0;
}

const char *ticket_status_name(TicketStatus status)
{
    switch (status) {
    case TICKET_OPEN:        return "OPEN";
    case TICKET_IN_PROGRESS: return "IN_PROGRESS";
    case TICKET_RESOLVED:    return "RESOLVED";
    }
    return "UNKNOWN";
}