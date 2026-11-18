CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wshadow -Wno-unused-parameter
LDFLAGS ?=
LIBS     = -lX11 -lm

SRC = src/main.c src/gfx.c src/font.c src/world.c src/game.c src/units.c src/ai.c src/render.c src/hud.c
OBJ = $(SRC:.c=.o)
BIN = wod_c

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

run: $(BIN)
	./$(BIN)

.PHONY: all clean run
