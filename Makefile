CC = gcc
CFLAGS = -Wall -Wextra -g -static-libasan -Iinclude -Isrc/compiler

SOURCES = $(wildcard src/*.c) $(wildcard src/**/*.c)

aul: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o aul.exe

aulfast: CFLAGS = -Wall -Wextra -O2 -Iinclude -Isrc/compiler
aulfast: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o aulfast.exe