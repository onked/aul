CC = gcc
CFLAGS = -Wall -Wextra -g -static-libasan -Iinclude

SOURCES = $(wildcard src/*.c)

aul: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o aul.exe