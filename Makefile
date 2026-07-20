CC := gcc

COMMON_SOURCES := \
	src/gifDecode.c \
	src/cbeParser.c \
	src/mystd.c \
	src/fontEngine.c \
	src/vmMalloc.c \
	src/fileIoEngine.c \
	src/lcd.c \
	src/mysql-client.c \
	src/main.c

ifeq ($(OS),Windows_NT)
OBJDIR := obj
TARGET := bin/main.exe
OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(COMMON_SOURCES)) $(OBJDIR)/resource.o
CPPFLAGS += -DNETWORK_SUPPORT
CFLAGS += -g -w
UNICORN_LIB := Lib/unicorn-2.1.4/unicorn-import.lib
SDL2_DIR := Lib/sdl2-2.0.10
LDLIBS += -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32 \
	$(UNICORN_LIB) -L$(SDL2_DIR)/lib/ -lSDL2main -lSDL2
else
# Linux deliberately produces only the authoritative headless service.  It
# does not link SDL/Unicorn or build/run the emulator client.
OBJDIR := obj/linux-server
TARGET := bin/jh-online-server
SERVER_SOURCES := \
	src/gifDecode.c \
	src/mystd.c \
	src/mysql-client.c \
	src/main.c \
	src/server-headless.c
OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SERVER_SOURCES))
CPPFLAGS += -DNETWORK_SUPPORT -DCBE_SERVER_ONLY
CFLAGS += -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w
LDFLAGS += -Wl,--gc-sections
LDLIBS += -lpthread -lm
endif

.PHONY: all build clean

all: build

build: $(TARGET)

$(OBJDIR)/main.o: src/main.c src/mock-server.c src/web_admin_server.c \
	src/web_admin_monsters.inc.c \
	src/mysql-client.h src/vmFunc.c src/hookRam.c src/vmEvent.c src/config.h

$(OBJDIR)/mystd.o: src/mystd.c src/mystd.h src/config.h

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ifeq ($(OS),Windows_NT)
$(OBJDIR)/resource.o: resource.rc | $(OBJDIR)
	windres $< -O coff -o $@

$(OBJDIR):
	@if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"

bin:
	@if not exist "bin" mkdir "bin"
else
$(OBJDIR):
	mkdir -p "$(OBJDIR)"

bin:
	mkdir -p bin
endif

$(TARGET): $(OBJS) | bin
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

clean:
ifeq ($(OS),Windows_NT)
	-del /Q "$(OBJDIR)\*.o" "$(TARGET)" 2>NUL
else
	rm -rf "$(OBJDIR)" "$(TARGET)"
endif
