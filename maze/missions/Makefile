# missions/Makefile
# Builds the mission dashboard tool.
#
# Default build uses hiredis (Redis client library).
# To build without Redis dependency:
#   make NO_REDIS=1

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

TARGET := mission_dashboard
SRC := mission_dashboard.c

# SDL2 flags
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LDFLAGS := $(shell sdl2-config --libs)

ifeq ($(NO_REDIS),1)
  CFLAGS += -DNO_REDIS
  LDLIBS := -lSDL2_ttf
else
  LDLIBS := -lhiredis -lSDL2_ttf
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SRC) -o $(TARGET) $(LDLIBS) $(SDL_LDFLAGS)

clean:
	rm -f $(TARGET)
