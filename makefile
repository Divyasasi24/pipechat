CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g
TARGET  = pipechat
SRCS    = main.c parent.c child.c ipc.c crypto.c msg.c stats.c ticket.c logger.c
OBJS    = $(SRCS:.c=.o)
HDRS    = pipechat.h

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) chat.log chat.log.bak