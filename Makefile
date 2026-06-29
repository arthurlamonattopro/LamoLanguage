CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Isrc -Isrc/lexer -Isrc/parser -Isrc/ast -Isrc/codegen -Isrc/semantic

SRCS = src/lamo_v2.c src/lexer/lexer.c src/parser/parser.c src/ast/ast.c src/codegen/codegen.c src/semantic/semantic.c
OBJS = $(SRCS:.c=.o)

ifeq ($(OS),Windows_NT)
EXEEXT = .exe
else
EXEEXT =
endif

TARGET = lamo$(EXEEXT)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
ifeq ($(OS),Windows_NT)
	@del /q /f $(OBJS) $(TARGET) *.c.output lamo_exec.c lamo_exec.exe lamo.exe 2>nul || echo done
else
	rm -f $(OBJS) $(TARGET) *.c.output lamo_exec.c lamo_exec lamo.exe
endif

test: $(TARGET)
ifeq ($(OS),Windows_NT)
	powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1
else
	sh tests/run_tests.sh ./$(TARGET)
endif
