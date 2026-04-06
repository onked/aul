CC = gcc
CFLAGS = -Wall -Wextra -g -static-libasan -Iinclude -Isrc/compiler

SOURCES = $(wildcard src/*.c) $(wildcard src/**/*.c)

aul: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o aul.exe