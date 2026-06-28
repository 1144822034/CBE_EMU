CC := gcc

OBJS := obj/gifDecode.o obj/cbeParser.o obj/mystd.o obj/fontEngine.o obj/vmMalloc.o obj/fileIoEngine.o obj/lcd.o obj/main.o

UNICORN = Lib/unicorn-2.1.4/unicorn-import.lib

SDL2 = Lib/sdl2-2.0.10

EMSDK ?= E:/emsdk-main
EMCC ?= $(EMSDK)/upstream/emscripten/emcc.bat
export PYTHONUTF8 := 1
WASM_BUILD_DIR := web/build
WASM_FS_DIR := web/fs
WASM_UNICORN := Lib/unicorn-wasm/libunicorn.a
WASM_LEGACY_JS := web/emscripten-legacy-setjmp.js
WASM_PATH_ALIASES := $(WASM_BUILD_DIR)/wasm_path_aliases.inc
WASM_SRCS := src/gifDecode.c src/cbeParser.c src/mystd.c src/fontEngine.c src/vmMalloc.c src/fileIoEngine.c src/lcd.c src/main.c
WASM_INCLUDED_SRCS := src/mock-server.c src/vmFunc.c src/hookRam.c src/vmEvent.c
WASM_COMPAT_FLAGS := -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=return-mismatch -Wno-error=return-type -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
WASM_CFLAGS := -std=gnu11 -Os -w $(WASM_COMPAT_FLAGS) -D__USE_MINGW_ANSI_STDIO=1 -Isrc
WASM_SETTINGS := -s EXPORTED_FUNCTIONS="['_main','_malloc','_free']" -s ALLOW_MEMORY_GROWTH=1 -s ALLOW_TABLE_GROWTH=1 -s USE_SDL=2 -s USE_ZLIB=1 -s WASM=1 -s FORCE_FILESYSTEM=1 -s SUPPORT_LONGJMP=emscripten -s DISABLE_EXCEPTION_THROWING=0 -s ASYNCIFY=1 -s ASYNCIFY_STACK_SIZE=65536

# -Wl,-subsystem,windows gets rid of the console window
# gcc  -o main.exe main.c -lmingw32 -Wl,-subsystem,windows -L./lib -lSDL2main -lSDL2
# -mwindows 关闭控制台窗口
# -lwinhttp http通信库
all: $(OBJS) build

obj/cbeParser.o: src/cbeParser.c
	$(CC) -g  -w -c src/cbeParser.c -o obj/cbeParser.o
obj/mystd.o: src/mystd.c
	$(CC) -g  -w -c src/mystd.c -o obj/mystd.o
obj/fontEngine.o: src/fontEngine.c
	$(CC) -g  -w -c src/fontEngine.c -o obj/fontEngine.o
obj/vmMalloc.o: src/vmMalloc.c
	$(CC) -g  -w -c src/vmMalloc.c -o obj/vmMalloc.o
obj/fileIoEngine.o: src/fileIoEngine.c
	$(CC) -g  -w -c src/fileIoEngine.c -o obj/fileIoEngine.o
obj/lcd.o: src/lcd.c
	$(CC) -g  -w -c src/lcd.c -o obj/lcd.o
obj/main.o: src/main.c src/mock-server.c src/vmFunc.c src/hookRam.c src/vmEvent.c
	$(CC) -g  -w -c src/main.c -o obj/main.o
obj/gifDecode.o: src/gifDecode.c
	$(CC) -g  -w -c src/gifDecode.c -o obj/gifDecode.o
build:
	$(CC) $(OBJS) -o bin/main.exe -g -w -lpthread -liconv -lm -lmingw32 -lkernel32 -Wall -lws2_32 -DNETWORK_SUPPORT $(UNICORN) -L$(SDL2)/lib/ -lSDL2main -lSDL2

.PHONY: wasm wasm-assets clean-wasm

wasm: wasm-assets $(WASM_BUILD_DIR)/cbe-emu.js

wasm-assets:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-wasm-assets.ps1

$(WASM_PATH_ALIASES): wasm-assets

$(WASM_BUILD_DIR)/cbe-emu.js: $(WASM_SRCS) $(WASM_INCLUDED_SRCS) $(WASM_UNICORN) $(WASM_LEGACY_JS) $(WASM_PATH_ALIASES) scripts/prepare-wasm-assets.ps1 Makefile
	powershell -NoProfile -ExecutionPolicy Bypass -Command "New-Item -ItemType Directory -Force -Path '$(WASM_BUILD_DIR)' | Out-Null"
	$(EMCC) $(WASM_CFLAGS) $(WASM_SRCS) $(WASM_UNICORN) --js-library $(WASM_LEGACY_JS) --preload-file $(WASM_FS_DIR)@/ $(WASM_SETTINGS) -o $(WASM_BUILD_DIR)/cbe-emu.js

clean-wasm:
	powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-Item -Recurse -Force '$(WASM_BUILD_DIR)','$(WASM_FS_DIR)' -ErrorAction SilentlyContinue"
