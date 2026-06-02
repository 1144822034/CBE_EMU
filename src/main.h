#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"
#include "../Lib/unicorn-2.1.4/unicorn/unicorn.h"
#include "config.h"
#include "fileIoEngine.h"
#include "vmMalloc.h"
#include "fontEngine.h"
#include "mystd.h"
#include "cbeParser.h"

#define DEBUG_PRINT(...) ((void)0)
#define TRACE_STARTUP_UI 0
#define TRACE_RESOURCE_IO 0
#define TRACE_LCD_TEXT 1
#define TRACE_LCD_SHAPES 1

/* 当前默认启动的 CBE，保持原工程行为不变。 */
#define LOAD_CBE_PATH "CBE/江湖OL.CBE"

void RunArmProgram(void *);
void MainUpdateTask();
void loop(void);

void hookBlockCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
void hookCodeCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
void hookRamCallBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
bool hookRamErrorBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
void hookCpuIntr(uc_engine *uc, uint32_t intno, void *user_data);
bool hookInsnInvalid(uc_engine *uc, void *user_data);

void SaveCpuContext(u32 *);
void RestoreCpuContext(u32 *stackPtr);
int utf16_len(char *utf16);
bool writeSDFile(u8 *Buffer, unsigned long long startPos, u32 size);
u8 *readSDFile(unsigned long long startPos, u32 size);
void saveFlashFile(void);
void readFlashFile(void);
bool StartInterrupt(u32, u32);
void handleEvent_EMU(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
bool isIRQ_Disable(u32 cpsr);
bool isIrqMode(u32 cpsr);
void dumpCpuInfo(void);
void dumpVirtMemory(u32 addr, u32 len);
bool vm_trace_verbose_enabled(void);
void vm_install_crash_handlers(void);
void vm_lcd_update_with_input_overlay(void);
uc_err add_manager_code_hooks(uc_engine *uc);

extern u32 last_gpt1_interrupt_time;
extern u32 IRQ_MASK_SET_L_Data;
extern u32 IRQ_MASK_SET_H_Data;

extern u32 Global_R9;

extern u8 *ROM_MEMPOOL;
extern u8 *STACK_MEMPOOL;
extern u8 *PRAM_MEMPOOL;
extern u8 *RAM_MEMPOOL;

extern uc_engine *MTK;
extern u8 isBreakPointHit;
extern u32 changeTmp;
extern u32 changeTmp1;
extern u32 changeTmp2;
extern u32 changeTmp3;

extern SDL_Window *window;
extern u32 lastAddress;

extern u8 emptyBuff[1024];
extern u8 globalSprintfBuff[256];
extern u8 sprintfBuff[256];
extern u8 cbeTextString[1024];
extern u32 size_16mb;
extern u32 size_4mb;
extern u32 size_1mb;
extern int irq_nested_count;
extern u32 irq_stack_ptr;
extern u32 debugType;

extern clock_t currentTime;
extern int regs[];

extern u32 Interrupt_Handler_Entry;
extern u8 ucs2Tmp[128];
extern FILE *SD_File_Handle;
extern pthread_mutex_t mutex;
extern u8 isStepNext;
extern SDL_Keycode isKeyDown;
extern pthread_t EmuThread;
extern pthread_t MainUpdareThread;
extern bool isMouseDown;
extern u8 currentProgramDir[256];
extern u32 stackCallback[17];
extern int simulatePress;
extern int simulateKey;
extern int simulateTouchPress;
extern int simulateTouchDown;
extern int simulateTouchUp;
extern int simulateTouchDrag;
extern int simulateTouchX;
extern int simulateTouchY;
extern u32 screenStructChange;
extern u32 screenStructNotifyLoadRes;
extern u32 vmAddedScreen;
extern u32 g_currentScreenModuleBase;
extern u32 lastSprintfPtr;

#ifdef GDB_SERVER_SUPPORT
typedef enum
{
    WATCHPOINT_NONE = 0,
    WATCHPOINT_WRITE = 1,
    WATCHPOINT_READ = 2,
    WATCHPOINT_ACCESS = 3
} WatchpointType;

typedef struct
{
    unsigned int addr;
    unsigned int size;
    WatchpointType type;
    int enabled;
    int hit_count;
} Watchpoint;

typedef struct
{
    unsigned int registers[32];
    unsigned char memory[0x10000];
    unsigned int pc;
    int running;
    unsigned int breakpoints[32];
    int num_breakpoints;
    int simulate_pc_count;
    unsigned int last_stop_reason;
    Watchpoint watchpoints[16];
    int num_watchpoints;
    int step_mode;
    unsigned int step_start_addr;
    unsigned int step_start_sp;
    char breakpoint_conditions[32][256];
    int breakpoint_conditional[32];
} TargetSystem;

typedef struct
{
    int socket;
    void *thread;
    TargetSystem *gdbTarget;
    int active;
} GDBClient;

void readMemoryToGdb(unsigned int addr, unsigned int length, void *buffer);
void writeMemoryToGdb(unsigned int addr, char value);
void writeRegToGdb(u32 reg, u32 value);
void ReadRegsToGdb(int *regPtr);
#endif
