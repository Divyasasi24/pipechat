/* stats.c - chat statistics (counters per process) */
#include "pipechat.h"

#include <stdio.h>

void stats_init(ChatStats *stats)
{
    stats->start_time        = time(NULL);
    stats->messages_sent     = 0;
    stats->messages_received = 0;
    stats->bytes_sent        = 0;
    stats->bytes_received    = 0;
}

void stats_record_sent(ChatStats *stats, size_t bytes)
{
    stats->messages_sent++;
    stats->bytes_sent += (unsigned long)bytes;
}

void stats_record_received(ChatStats *stats, size_t bytes)
{
    stats->messages_received++;
    stats->bytes_received += (unsigned long)bytes;
}

long stats_uptime(const ChatStats *stats)
{
    return (long)(time(NULL) - stats->start_time);
}

void stats_summary(const ChatStats *stats, char *out, size_t out_size)
{
    snprintf(out, out_size,
             "sent %lu msg (%lu B), received %lu msg (%lu B), uptime %ld s",
             stats->messages_sent, stats->bytes_sent,
             stats->messages_received, stats->bytes_received,
             stats_uptime(stats));
}