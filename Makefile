CC := gcc

# The service owns protocol builders, persistence and resource catalogs.  The
# emulator owns the CBE VM/UI and only forwards CBMS/WT bytes to that service.
# Keep their object directories separate: otherwise switching CBE_SERVER_ONLY
# or CBE_CLIENT_ONLY can silently reuse an object compiled for the other side.
MOCK_SERVER_FRAGMENTS := \
	src/server/mock-server.c \
	src/server/mock_server_core.c \
	src/server/mock_server_catalog.c \
	src/server/mock_server_mailbox.c \
	src/server/mock_server_role.c \
	src/server/mock_server_ranking.c \
	src/server/mock_server_equipment_npc.c \
	src/server/mock_server_scene_task.c \
	src/server/mock_server_scene_sync.c \
	src/server/mock_server_guild.c \
	src/server/mock_server_social.c \
	src/server/mock_server_battle.c \
	src/server/mock_server_arena.c \
	src/server/mock_server_interaction_login.c \
	src/server/mock_server_dispatch.c \
	src/server/mock_server_transport.c

# Modules with explicit interfaces that are linked as their own server
# objects.  Add new source files here only after their aggregation include is
# guarded by CBE_SERVER_SPLIT_OBJECTS and every cross-module dependency is
# declared in mock_server.h.
MOCK_SERVER_SPLIT_SOURCES := \
	src/server/mock_server_arena.c \
	src/server/mock_server_mailbox.c \
	src/server/mock_server_ranking.c

# Regression programs include server_main.c directly, so they must rebuild
# whenever its standalone-service boundary changes as well.
MOCK_SERVER_FRAGMENTS += src/server/mock_server.h

# Sources that are still textually aggregated into mock-server.c.  Independently
# linked modules stay in MOCK_SERVER_FRAGMENTS for the direct-include regression
# harness, but must not force the aggregation object to rebuild.
MOCK_SERVER_AGGREGATE_FRAGMENTS := $(filter-out src/server/mock-server.c $(MOCK_SERVER_SPLIT_SOURCES),$(MOCK_SERVER_FRAGMENTS))

CLIENT_SOURCES := \
	src/gifDecode.c \
	src/cbeParser.c \
	src/mystd.c \
	src/fontEngine.c \
	src/vmMalloc.c \
	src/fileIoEngine.c \
	src/lcd.c \
	src/automation_png.c \
	src/md5.c \
	src/main.c

SERVER_SOURCES := \
	src/gifDecode.c \
	src/mystd.c \
	src/mysql-client.c \
	src/md5.c \
	src/server_main.c \
	src/server/mock-server.c \
	$(MOCK_SERVER_SPLIT_SOURCES)

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
# The service is linked from multiple translation units.  `-fwhole-program`
# lets GCC internalize externally referenced helpers (for example GBK/UTF-8
# conversion in mystd.c), which makes a clean server build fail at link time.
SERVER_CFLAGS := $(CFLAGS)
UNICORN_LIB := Lib/unicorn-2.1.4/unicorn-import.lib
SDL2_DIR := Lib/sdl2-2.0.10
CLIENT_LDLIBS := -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32 \
	$(UNICORN_LIB) -L$(SDL2_DIR)/lib/ -lSDL2main -lSDL2
SERVER_LDLIBS := -lpthread -liconv -lm -lkernel32 -lws2_32 -ldbghelp

.PHONY: all build client server boundary-check content-update-manifest-regression scene-battle-monster-field18-regression city-scene-battle-mirror-regression instance-guide-direct-entry-regression admin-scene-battle-monster-layout-regression admin-dynamic-npc-id-regression admin-monster-picker-regression admin-role-timed-item-effect-regression registration-email-contract-regression mailbox-claim-backpack-refresh-regression battle-primary-stat-uncap-regression battle-derived-stat-uncap-regression zhongnan-taiyi-recovery-landing-regression direct-scene-challenge-progress-regression direct-scene-challenge-progress-client-regression first-login-equipment-attribute-bootstrap-regression equipment-enhancement-bootstrap-split-regression equipment-enhancement-bootstrap-delivery-regression startup-sce-direct-enter-test-gate-regression teleport-stone-scene-catalog-regression npc-crystal-synthesis-regression npc-quality-zero-equipment-recycle-regression battle-insight-followup-regression battle-insight-status-regression timed-item-status-icon-regression task-delivery-item-consumption-regression clean

$(SERVER_OBJDIR)/%-regression.exe: SERVER_CPPFLAGS += -DCBE_SERVER_TEST_INCLUDE_IMPLEMENTATION
$(SERVER_OBJDIR)/server/mock-server.o: SERVER_CPPFLAGS += -DCBE_SERVER_SPLIT_OBJECTS

all: build
build: client server
client: $(CLIENT_TARGET)
server: $(SERVER_TARGET)
boundary-check: build
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-service-boundary.ps1

npc-crystal-synthesis-regression: $(SERVER_OBJDIR)/npc-crystal-synthesis-regression.exe

$(SERVER_OBJDIR)/npc-crystal-synthesis-regression.exe: scripts/npc-crystal-synthesis-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

npc-quality-zero-equipment-recycle-regression: $(SERVER_OBJDIR)/npc-quality-zero-equipment-recycle-regression.exe

$(SERVER_OBJDIR)/npc-quality-zero-equipment-recycle-regression.exe: scripts/npc-quality-zero-equipment-recycle-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

battle-insight-followup-regression: $(SERVER_OBJDIR)/battle-insight-followup-regression.exe

battle-insight-status-regression: $(SERVER_OBJDIR)/battle-insight-status-regression.exe

timed-item-status-icon-regression: $(SERVER_OBJDIR)/timed-item-status-icon-regression.exe

$(SERVER_OBJDIR)/timed-item-status-icon-regression.exe: scripts/timed-item-status-icon-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

$(SERVER_OBJDIR)/battle-insight-followup-regression.exe: scripts/battle-insight-followup-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

$(SERVER_OBJDIR)/battle-insight-status-regression.exe: scripts/battle-insight-status-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

direct-scene-challenge-progress-regression: $(SERVER_OBJDIR)/direct-scene-challenge-progress-regression.exe

$(SERVER_OBJDIR)/direct-scene-challenge-progress-regression.exe: scripts/direct-scene-challenge-progress-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

task-delivery-item-consumption-regression: $(SERVER_OBJDIR)/task-delivery-item-consumption-regression.exe

$(SERVER_OBJDIR)/task-delivery-item-consumption-regression.exe: scripts/task-delivery-item-consumption-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

content-update-manifest-regression: $(SERVER_OBJDIR)/content-update-manifest-regression.exe

$(SERVER_OBJDIR)/content-update-manifest-regression.exe: scripts/content-update-manifest-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

scene-battle-monster-field18-regression: $(SERVER_OBJDIR)/scene-battle-monster-field18-regression.exe

$(SERVER_OBJDIR)/scene-battle-monster-field18-regression.exe: scripts/scene-battle-monster-field18-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

city-scene-battle-mirror-regression: $(SERVER_OBJDIR)/city-scene-battle-mirror-regression.exe

$(SERVER_OBJDIR)/city-scene-battle-mirror-regression.exe: scripts/city-scene-battle-mirror-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

instance-guide-direct-entry-regression: $(SERVER_OBJDIR)/instance-guide-direct-entry-regression.exe

$(SERVER_OBJDIR)/instance-guide-direct-entry-regression.exe: scripts/instance-guide-direct-entry-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

admin-scene-battle-monster-layout-regression: $(SERVER_OBJDIR)/admin-scene-battle-monster-layout-regression.exe

$(SERVER_OBJDIR)/admin-scene-battle-monster-layout-regression.exe: scripts/admin-scene-battle-monster-layout-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

admin-dynamic-npc-id-regression: $(SERVER_OBJDIR)/admin-dynamic-npc-id-regression.exe

$(SERVER_OBJDIR)/admin-dynamic-npc-id-regression.exe: scripts/admin-dynamic-npc-id-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

admin-monster-picker-regression: $(SERVER_OBJDIR)/admin-monster-picker-regression.exe

$(SERVER_OBJDIR)/admin-monster-picker-regression.exe: scripts/admin-monster-picker-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

admin-task-target-list-regression: $(SERVER_OBJDIR)/admin-task-target-list-regression.exe

$(SERVER_OBJDIR)/admin-task-target-list-regression.exe: scripts/admin-task-target-list-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/web_admin_server.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

admin-task-id-allocation-regression: $(SERVER_OBJDIR)/admin-task-id-allocation-regression.exe

$(SERVER_OBJDIR)/admin-task-id-allocation-regression.exe: scripts/admin-task-id-allocation-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/web_admin_server.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

admin-npc-task-picker-regression: $(SERVER_OBJDIR)/admin-npc-task-picker-regression.exe

$(SERVER_OBJDIR)/admin-npc-task-picker-regression.exe: scripts/admin-npc-task-picker-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/web_admin_server.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

admin-role-timed-item-effect-regression: $(SERVER_OBJDIR)/admin-role-timed-item-effect-regression.exe

$(SERVER_OBJDIR)/admin-role-timed-item-effect-regression.exe: scripts/admin-role-timed-item-effect-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

registration-email-contract-regression: $(SERVER_OBJDIR)/registration-email-contract-regression.exe

$(SERVER_OBJDIR)/registration-email-contract-regression.exe: scripts/registration-email-contract-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/web_admin_server.c src/web_registration.inc.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

mailbox-claim-backpack-refresh-regression: $(SERVER_OBJDIR)/mailbox-claim-backpack-refresh-regression.exe

$(SERVER_OBJDIR)/mailbox-claim-backpack-refresh-regression.exe: scripts/mailbox-claim-backpack-refresh-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

battle-primary-stat-uncap-regression: $(SERVER_OBJDIR)/battle-primary-stat-uncap-regression.exe

$(SERVER_OBJDIR)/battle-primary-stat-uncap-regression.exe: scripts/battle-primary-stat-uncap-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

battle-derived-stat-uncap-regression: $(SERVER_OBJDIR)/battle-derived-stat-uncap-regression.exe

$(SERVER_OBJDIR)/battle-derived-stat-uncap-regression.exe: scripts/battle-derived-stat-uncap-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

zhongnan-taiyi-recovery-landing-regression: $(SERVER_OBJDIR)/zhongnan-taiyi-recovery-landing-regression.exe

$(SERVER_OBJDIR)/zhongnan-taiyi-recovery-landing-regression.exe: scripts/zhongnan-taiyi-recovery-landing-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

direct-scene-challenge-progress-client-regression: $(CLIENT_OBJDIR)/direct-scene-challenge-progress-client-regression.exe

$(CLIENT_OBJDIR)/direct-scene-challenge-progress-client-regression.exe: scripts/direct-scene-challenge-progress-client-regression.c $(MOCK_SERVER_FRAGMENTS) src/main.c src/network-client.c src/md5.h | $(CLIENT_OBJDIR)
	$(CC) $(CLIENT_CPPFLAGS) $(CFLAGS) $< src/gifDecode.c src/cbeParser.c src/mystd.c src/fontEngine.c src/vmMalloc.c src/fileIoEngine.c src/lcd.c src/automation_png.c src/md5.c -o $@ $(CLIENT_LDLIBS)

first-login-equipment-attribute-bootstrap-regression: $(SERVER_OBJDIR)/first-login-equipment-attribute-bootstrap-regression.exe

$(SERVER_OBJDIR)/first-login-equipment-attribute-bootstrap-regression.exe: scripts/first-login-equipment-attribute-bootstrap-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

equipment-enhancement-bootstrap-split-regression: $(CLIENT_OBJDIR)/equipment-enhancement-bootstrap-split-regression.exe

$(CLIENT_OBJDIR)/equipment-enhancement-bootstrap-split-regression.exe: scripts/equipment-enhancement-bootstrap-split-regression.c $(MOCK_SERVER_FRAGMENTS) src/main.c src/network-client.c src/md5.h | $(CLIENT_OBJDIR)
	$(CC) $(CLIENT_CPPFLAGS) $(CFLAGS) $< src/gifDecode.c src/cbeParser.c src/mystd.c src/fontEngine.c src/vmMalloc.c src/fileIoEngine.c src/lcd.c src/automation_png.c src/md5.c -o $@ $(CLIENT_LDLIBS)

equipment-enhancement-bootstrap-delivery-regression: $(CLIENT_OBJDIR)/equipment-enhancement-bootstrap-delivery-regression.exe

$(CLIENT_OBJDIR)/equipment-enhancement-bootstrap-delivery-regression.exe: scripts/equipment-enhancement-bootstrap-delivery-regression.c $(MOCK_SERVER_FRAGMENTS) src/main.c src/network-client.c src/md5.h | $(CLIENT_OBJDIR)
	$(CC) $(CLIENT_CPPFLAGS) $(CFLAGS) $< src/gifDecode.c src/cbeParser.c src/mystd.c src/fontEngine.c src/vmMalloc.c src/fileIoEngine.c src/lcd.c src/automation_png.c src/md5.c -o $@ $(CLIENT_LDLIBS)

startup-sce-direct-enter-test-gate-regression: $(SERVER_OBJDIR)/startup-sce-direct-enter-test-gate-regression.exe

$(SERVER_OBJDIR)/startup-sce-direct-enter-test-gate-regression.exe: scripts/startup-sce-direct-enter-test-gate-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

teleport-stone-scene-catalog-regression: $(SERVER_OBJDIR)/teleport-stone-scene-catalog-regression.exe

$(SERVER_OBJDIR)/teleport-stone-scene-catalog-regression.exe: scripts/teleport-stone-scene-catalog-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

$(CLIENT_OBJDIR)/main.o: src/main.c $(MOCK_SERVER_FRAGMENTS) src/network-client.c src/md5.h \
	src/vmFunc.c src/hookRam.c src/vmEvent.c src/config.h
$(SERVER_OBJDIR)/server_main.o: src/server_main.c src/server/mock_server.h \
	src/main.h src/gifDecode.h src/md5.h src/config.h
$(SERVER_OBJDIR)/server/mock-server.o: src/server/mock-server.c src/server/mock_server.h \
	$(MOCK_SERVER_AGGREGATE_FRAGMENTS) src/web_admin_server.c src/web_payment.inc.c \
	src/web_registration.inc.c src/web_admin_monsters.inc.c src/web_admin_chests.inc.c \
	src/web_admin_global_rewards.inc.c src/web_admin_designations.inc.c \
	src/mysql-client.h src/md5.h src/config.h
$(patsubst src/%.c,$(SERVER_OBJDIR)/%.o,$(MOCK_SERVER_SPLIT_SOURCES)): \
	src/server/mock_server.h src/mysql-client.h src/config.h

$(CLIENT_OBJDIR)/%.o: src/%.c | $(CLIENT_OBJDIR)
	mkdir -p "$(@D)"
	$(CC) $(CLIENT_CPPFLAGS) $(CFLAGS) -c $< -o $@
$(SERVER_OBJDIR)/%.o: src/%.c | $(SERVER_OBJDIR)
	mkdir -p "$(@D)"
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
# Keep external helper symbols visible across the separately compiled service
# objects; see the Windows target above for the clean-build rationale.
SERVER_CFLAGS := $(CFLAGS)
SERVER_LDLIBS := -lpthread -lm

.PHONY: all build server boundary-check registration-email-contract-regression mailbox-claim-backpack-refresh-regression clean
$(SERVER_OBJDIR)/%-regression: SERVER_CPPFLAGS += -DCBE_SERVER_TEST_INCLUDE_IMPLEMENTATION
$(SERVER_OBJDIR)/server/mock-server.o: SERVER_CPPFLAGS += -DCBE_SERVER_SPLIT_OBJECTS
all: build
build: server
server: $(SERVER_TARGET)
boundary-check: build
	@echo "boundary-check: Linux builds the service target only; run the Windows dual-target check in CI."

registration-email-contract-regression: $(SERVER_OBJDIR)/registration-email-contract-regression

$(SERVER_OBJDIR)/registration-email-contract-regression: scripts/registration-email-contract-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/web_admin_server.c src/web_registration.inc.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

mailbox-claim-backpack-refresh-regression: $(SERVER_OBJDIR)/mailbox-claim-backpack-refresh-regression

$(SERVER_OBJDIR)/mailbox-claim-backpack-refresh-regression: scripts/mailbox-claim-backpack-refresh-regression.c $(MOCK_SERVER_FRAGMENTS) src/server_main.c src/mysql-client.h src/md5.h | $(SERVER_OBJDIR)
	$(CC) $(SERVER_CPPFLAGS) $(SERVER_CFLAGS) $< src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o $@ $(SERVER_LDLIBS)

$(SERVER_OBJDIR)/server_main.o: src/server_main.c src/server/mock_server.h \
	src/main.h src/gifDecode.h src/md5.h src/config.h
$(SERVER_OBJDIR)/server/mock-server.o: src/server/mock-server.c src/server/mock_server.h \
	$(MOCK_SERVER_AGGREGATE_FRAGMENTS) src/web_admin_server.c src/web_payment.inc.c \
	src/web_registration.inc.c src/web_admin_monsters.inc.c src/web_admin_chests.inc.c \
	src/web_admin_global_rewards.inc.c src/web_admin_designations.inc.c \
	src/mysql-client.h src/md5.h src/config.h
$(patsubst src/%.c,$(SERVER_OBJDIR)/%.o,$(MOCK_SERVER_SPLIT_SOURCES)): \
	src/server/mock_server.h src/mysql-client.h src/config.h

$(SERVER_OBJDIR)/%.o: src/%.c | $(SERVER_OBJDIR)
	mkdir -p "$(@D)"
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
