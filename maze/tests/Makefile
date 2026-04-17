# CUnit test suite — Team 3F capstone
# Place in maze/tests/
# Usage: make

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
LIBS   = -lcunit -lm

TARGET = test_maze
SRC    = test_maze.c

.PHONY: all clean

all: $(TARGET)
	@echo "\n── Running CUnit test suite ──"
	./$(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET)
