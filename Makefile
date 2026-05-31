CC := gcc

OBJS := obj/gifDecode.o obj/cbeParser.o obj/mystd.o obj/fontEngine.o obj/vmMalloc.o obj/fileIoEngine.o obj/lcd.o obj/main.o

UNICORN = Lib/unicorn-2.1.4/unicorn-import.lib

SDL2 = Lib/sdl2-2.0.10

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
obj/main.o: src/main.c src/vmFunc.c src/hookRam.c src/vmEvent.c
	$(CC) -g  -w -c src/main.c -o obj/main.o
obj/gifDecode.o: src/gifDecode.c
	$(CC) -g  -w -c src/gifDecode.c -o obj/gifDecode.o
build:
	$(CC) $(OBJS) -o bin/main.exe -g -w -lpthread -liconv -lm -lmingw32 -lkernel32 -Wall -lws2_32 -DNETWORK_SUPPORT $(UNICORN) -L$(SDL2)/lib/ -lSDL2main -lSDL2
