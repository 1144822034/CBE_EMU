CC := gcc

OBJS := obj/gifDecode.o obj/cbeParser.o obj/mystd.o obj/fontEngine.o obj/vmMalloc.o obj/fileIoEngine.o obj/lcd.o obj/vmFunc.o obj/hookRam.o obj/vmEvent.o obj/core/emulator_state.o obj/runtime/emulator_services.o obj/runtime/emulator_managers.o obj/runtime/emulator_manager_lcd.o obj/runtime/emulator_manager_fileio.o obj/runtime/emulator_manager_stdio.o obj/runtime/emulator_manager_timer.o obj/runtime/emulator_manager_ctrl.o obj/runtime/emulator_manager_network.o obj/runtime/emulator_manager_screen.o obj/runtime/emulator_manager_game_util.o obj/runtime/emulator_manager_df_engine.o obj/runtime/emulator_manager_df_script.o obj/runtime/emulator_manager_gameold.o obj/runtime/emulator_manager_game_lcd.o obj/runtime/emulator_manager_audio.o obj/runtime/emulator_manager_netapp.o obj/runtime/emulator_manager_sensor.o obj/runtime/emulator_manager_vmim.o obj/runtime/emulator_manager_appstore.o obj/runtime/emulator_manager_dl.o obj/runtime/emulator_manager_video.o obj/runtime/emulator_runtime.o obj/main.o

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
obj/vmFunc.o: src/vmFunc.c
	$(CC) -g  -w -c src/vmFunc.c -o obj/vmFunc.o
obj/hookRam.o: src/hookRam.c
	$(CC) -g  -w -c src/hookRam.c -o obj/hookRam.o
obj/vmEvent.o: src/vmEvent.c
	$(CC) -g  -w -c src/vmEvent.c -o obj/vmEvent.o
obj/core/emulator_state.o: src/core/emulator_state.c
	cmd /c if not exist obj\\core mkdir obj\\core
	$(CC) -g  -w -c src/core/emulator_state.c -o obj/core/emulator_state.o
obj/runtime/emulator_services.o: src/runtime/emulator_services.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_services.c -o obj/runtime/emulator_services.o
obj/runtime/emulator_managers.o: src/runtime/emulator_managers.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_managers.c -o obj/runtime/emulator_managers.o
obj/runtime/emulator_manager_lcd.o: src/runtime/emulator_manager_lcd.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_lcd.c -o obj/runtime/emulator_manager_lcd.o
obj/runtime/emulator_manager_fileio.o: src/runtime/emulator_manager_fileio.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_fileio.c -o obj/runtime/emulator_manager_fileio.o
obj/runtime/emulator_manager_stdio.o: src/runtime/emulator_manager_stdio.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_stdio.c -o obj/runtime/emulator_manager_stdio.o
obj/runtime/emulator_manager_timer.o: src/runtime/emulator_manager_timer.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_timer.c -o obj/runtime/emulator_manager_timer.o
obj/runtime/emulator_manager_ctrl.o: src/runtime/emulator_manager_ctrl.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_ctrl.c -o obj/runtime/emulator_manager_ctrl.o
obj/runtime/emulator_manager_network.o: src/runtime/emulator_manager_network.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_network.c -o obj/runtime/emulator_manager_network.o
obj/runtime/emulator_manager_screen.o: src/runtime/emulator_manager_screen.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_screen.c -o obj/runtime/emulator_manager_screen.o
obj/runtime/emulator_manager_game_util.o: src/runtime/emulator_manager_game_util.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_game_util.c -o obj/runtime/emulator_manager_game_util.o
obj/runtime/emulator_manager_df_engine.o: src/runtime/emulator_manager_df_engine.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_df_engine.c -o obj/runtime/emulator_manager_df_engine.o
obj/runtime/emulator_manager_df_script.o: src/runtime/emulator_manager_df_script.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_df_script.c -o obj/runtime/emulator_manager_df_script.o
obj/runtime/emulator_manager_gameold.o: src/runtime/emulator_manager_gameold.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_gameold.c -o obj/runtime/emulator_manager_gameold.o
obj/runtime/emulator_manager_game_lcd.o: src/runtime/emulator_manager_game_lcd.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_game_lcd.c -o obj/runtime/emulator_manager_game_lcd.o
obj/runtime/emulator_manager_audio.o: src/runtime/emulator_manager_audio.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_audio.c -o obj/runtime/emulator_manager_audio.o
obj/runtime/emulator_manager_netapp.o: src/runtime/emulator_manager_netapp.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_netapp.c -o obj/runtime/emulator_manager_netapp.o
obj/runtime/emulator_manager_sensor.o: src/runtime/emulator_manager_sensor.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_sensor.c -o obj/runtime/emulator_manager_sensor.o
obj/runtime/emulator_manager_vmim.o: src/runtime/emulator_manager_vmim.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_vmim.c -o obj/runtime/emulator_manager_vmim.o
obj/runtime/emulator_manager_appstore.o: src/runtime/emulator_manager_appstore.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_appstore.c -o obj/runtime/emulator_manager_appstore.o
obj/runtime/emulator_manager_dl.o: src/runtime/emulator_manager_dl.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_dl.c -o obj/runtime/emulator_manager_dl.o
obj/runtime/emulator_manager_video.o: src/runtime/emulator_manager_video.c src/runtime/emulator_runtime_internal.h
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_manager_video.c -o obj/runtime/emulator_manager_video.o
obj/runtime/emulator_runtime.o: src/runtime/emulator_runtime.c
	cmd /c if not exist obj\\runtime mkdir obj\\runtime
	$(CC) -g  -w -c src/runtime/emulator_runtime.c -o obj/runtime/emulator_runtime.o
obj/main.o: src/main.c
	$(CC) -g  -w -c src/main.c -o obj/main.o
obj/gifDecode.o: src/gifDecode.c
	$(CC) -g  -w -c src/gifDecode.c -o obj/gifDecode.o
build:
	$(CC) $(OBJS) -o bin/main.exe -g -w -lpthread -liconv -lm -lmingw32 -lkernel32 -Wall -lws2_32 -DNETWORK_SUPPORT $(UNICORN) -L$(SDL2)/lib/ -lSDL2main -lSDL2
