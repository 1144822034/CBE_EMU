CC := gcc

# The service owns protocol builders, persistence and resource catalogs.  The
# emulator owns the CBE VM/UI and only forwards CBMS/WT bytes to that service.
# Keep their object directories separate: otherwise switching CBE_SERVER_ONLY
# or CBE_CLIENT_ONLY can silently reuse an object compiled for the other side.
MOCK_SERVER_FRAGMENTS := \
	src/server/mock-server.c \
	src/server/mock_server_core.c \
	src/server/mock_server_catalog.c \
	src/server/mock_server_role.c \
	src/server/mock_server_equipment_npc.c \
	src/server/mock_server_scene_task.c \
	src/server/mock_server_scene_sync.c \
	src/server/mock_server_guild.c \
	src/server/mock_server_social.c \
	src/server/mock_server_battle.c \
	src/server/mock_server_interaction_login.c \
	src/server/mock_server_dispatch.c \
	src/server/mock_server_transport.c

CLIENT_SOURCES := \
	src/gifDecode.c \
	src/cbeParser.c \
	src/mystd.c \
	src/fontEngine.c \
	src/vmMalloc.c \
	src/fileIoEngine.c \
	src/lcd.c \
	src/md5.c \
	src/main.c

SERVER_SOURCES := \
	src/gifDecode.c \
	src/mystd.c \
	src/mysql-client.c \
	src/md5.c \
	src/main.c

ifeq ($(OS),Windows_NT)
CLIENT_OBJDIR := obj/client
SERVER_OBJDIR := obj/server
CLIENT_TARGET := bin/main.exe
SERVER_TARGET := bin/jh-online-server.exe
CLIENT_OBJS := $(patsubst src/%.c,$(CLIENT_OBJDIR)/%.o,$(CLIENT_SOURCES)) $(CLIENT_OBJDIR)/resource.o
SERVER_OBJS := $(patsubst src/%.c,$(SERVER_OBJDIR)/%.o,$(SERVER_SOURCES))

CLIENT_CPPFLAGS := -DNETWORK_SUPPORT -DCBE_CLIENT_ONLY
SERVER_CPPFLAGS := -DNETWORK_SUPPORT -DCBE_SERVER_ONLY
CFLAGS += -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w
LDFLAGS += -Wl,--gc-sections
SERVER_CFLAGS := $(CFLAGS) -fwhole-program
UNICORN_LIB := Lib/unicorn-2.1.4/unicorn-import.lib
SDL2_DIR := Lib/sdl2-2.0.10
CLIENT_LDLIBS := -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32 \
	$(UNICORN_LIB) -L$(SDL2_DIR)/lib/ -lSDL2main -lSDL2
SERVER_LDLIBS := -lpthread -liconv -lm -lkernel32 -lws2_32

.PHONY: all build client server boundary-check clean

all: build
build: client server
client: $(CLIENT_TARGET)
server: $(SERVER_TARGET)
boundary-check: build
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-service-boundary.ps1

$(CLIENT_OBJDIR)/main.o: src/main.c src/network-client.c src/md5.h \
	src/vmFunc.c src/hookRam.c src/vmEvent.c src/config.h
$(SERVER_OBJDIR)/main.o: src/main.c $(MOCK_SERVER_FRAGMENTS) src/web_admin_server.c \
	src/web_payment.inc.c src/md5.h src/web_admin_monsters.inc.c \
	src/mysql-client.h src/vmFunc.c src/hookRam.c src/vmEvent.c src/config.h

$(CLIENT_OBJDIR)/%.o: src/%.c | $(CLIENT_OBJDIR)
	$(CC) $(CLIENT_CPPFLAGS) $(CFLAGS) -c $< -o $@
$(SERVER_OBJDIR)/%.o: src/%.c | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) -c $< -o $@

$(CLIENT_OBJDIR)/resource.o: resource.rc | $(CLIENT_OBJDIR)
	windres $< -O coff -o $@

$(CLIENT_OBJDIR):
	mkdir -p "$(CLIENT_OBJDIR)"
$(SERVER_OBJDIR):
	mkdir -p "$(SERVER_OBJDIR)"
bin:
	mkdir -p bin

$(CLIENT_TARGET): $(CLIENT_OBJS) | bin
	$(CC) $(LDFLAGS) $(CLIENT_OBJS) -o $@ $(CLIENT_LDLIBS)
$(SERVER_TARGET): $(SERVER_OBJS) | bin
	$(CC) $(LDFLAGS) $(SERVER_OBJS) -o $@ $(SERVER_LDLIBS)

clean:
	rm -rf "$(CLIENT_OBJDIR)" "$(SERVER_OBJDIR)" "$(CLIENT_TARGET)" "$(SERVER_TARGET)"

else
# Linux deliberately produces only the authoritative headless service.  It
# does not link SDL/Unicorn or build/run the emulator client.
SERVER_OBJDIR := obj/linux-server
SERVER_TARGET := bin/jh-online-server
SERVER_OBJS := $(patsubst src/%.c,$(SERVER_OBJDIR)/%.o,$(SERVER_SOURCES))
SERVER_CPPFLAGS := -DNETWORK_SUPPORT -DCBE_SERVER_ONLY
CFLAGS += -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w
LDFLAGS += -Wl,--gc-sections
SERVER_CFLAGS := $(CFLAGS) -fwhole-program
SERVER_LDLIBS := -lpthread -lm

.PHONY: all build server boundary-check clean
all: build
build: server
server: $(SERVER_TARGET)
boundary-check: build
	@echo "boundary-check: Linux builds the service target only; run the Windows dual-target check in CI."

$(SERVER_OBJDIR)/main.o: src/main.c $(MOCK_SERVER_FRAGMENTS) src/web_admin_server.c \
	src/web_payment.inc.c src/md5.h src/web_admin_monsters.inc.c \
	src/mysql-client.h src/vmFunc.c src/hookRam.c src/vmEvent.c src/config.h

$(SERVER_OBJDIR)/%.o: src/%.c | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) -c $< -o $@
$(SERVER_OBJDIR):
	mkdir -p "$(SERVER_OBJDIR)"
bin:
	mkdir -p bin
$(SERVER_TARGET): $(SERVER_OBJS) | bin
	$(CC) $(LDFLAGS) $(SERVER_OBJS) -o $@ $(SERVER_LDLIBS)
clean:
	rm -rf "$(SERVER_OBJDIR)" "$(SERVER_TARGET)"
endif
