CC      ?= gcc
MINGW   ?= x86_64-w64-mingw32-gcc
UNAME_S := $(shell uname -s)
SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   ?= $(shell sdl2-config --libs 2>/dev/null)
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wshadow -Wno-unused-parameter
LDFLAGS ?=
LIBS     = -lX11 -lXext -lm

CORE    = src/main.c src/gfx.c src/font.c src/world.c src/game.c src/units.c \
          src/ai.c src/render.c src/hud.c src/net.c
SRC = $(CORE) src/platform_x11.c
OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)
BIN = wod_c

ifeq ($(UNAME_S),Darwin)
all: macos
else
all: $(BIN)
endif

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

clean:
	rm -f $(OBJ) $(DEP) $(BIN) wod_c.exe wod_c_sdl src/platform_cocoa.o src/platform_cocoa.d

run: $(BIN)
	./$(BIN)

# SDL2 build: this is the macOS path (brew install sdl2), and also works on Linux.
sdl:
	@if [ -z "$(SDL_CFLAGS)" ]; then \
	    echo "SDL2 not found: install libsdl2-dev"; exit 1; fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(CORE) src/platform_sdl.c -o wod_c_sdl \
	    $(LDFLAGS) $(SDL_LIBS) -lm

# Native macOS build: Cocoa window/view, no SDL or Homebrew dependency.
macos: $(CORE:.c=.o) src/platform_cocoa.o
	$(CC) $(CORE:.c=.o) src/platform_cocoa.o -o wod_c -framework Cocoa -lm

src/platform_cocoa.o: src/platform_cocoa.m
	$(CC) -O2 -Wall -Wextra -Wno-deprecated-declarations -MMD -MP -c $< -o $@

-include src/platform_cocoa.d

# Optional fallback for macOS (and a general-purpose SDL2 build).
macos-sdl:
	@if [ -z "$(SDL_CFLAGS)" ]; then \
	    echo "SDL2 not found: run 'brew install sdl2'"; exit 1; fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(CORE) src/platform_sdl.c -o wod_c_sdl \
	    $(LDFLAGS) $(SDL_LIBS) -lm

# Native Windows build (no SDL, no X11): Win32 GDI + DIB presentation.
windows:
	$(MINGW) $(CFLAGS) $(CORE) src/platform_win32.c -o wod_c.exe \
	    $(LDFLAGS) -lgdi32 -luser32 -lwinmm -lws2_32 -lm -mwindows

.PHONY: all clean run windows sdl macos macos-sdl
