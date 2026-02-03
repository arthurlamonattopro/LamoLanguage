CC = gcc
CFLAGS = -Wall -Wextra -std=c99

SRCS = lamo_v2.c lexer_v2.c parser_v2.c ast.c codegen.c
OBJS = $(SRCS:.c=.o)

TARGET = lamo

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.c.output
