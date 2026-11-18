CC      ?= gcc
MINGW   ?= x86_64-w64-mingw32-gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wshadow -Wno-unused-parameter
LDFLAGS ?=
LIBS     = -lX11 -lXext -lm

CORE    = src/main.c src/gfx.c src/font.c src/world.c src/game.c src/units.c \
          src/ai.c src/render.c src/hud.c
SRC = $(CORE) src/platform_x11.c
OBJ = $(SRC:.c=.o)
BIN = wod_c

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN) wod_c.exe

run: $(BIN)
	./$(BIN)

# Native Windows build (no SDL, no X11): Win32 GDI + DIB presentation.
windows:
	$(MINGW) $(CFLAGS) $(CORE) src/platform_win32.c -o wod_c.exe \
	    $(LDFLAGS) -lgdi32 -luser32 -lwinmm -lm -mwindows

.PHONY: all clean run windows
