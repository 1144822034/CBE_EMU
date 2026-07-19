#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"
#ifdef CBE_PLATFORM_NO_WINDOW
#include "android_compat.h"
#endif
#if defined(CBE_PLATFORM_ANDROID)
#include <unicorn/unicorn.h>
#elif defined(CBE_PLATFORM_HEADLESS)
#include "../Lib/unicorn-2.1.4/unicorn/unicorn.h"
#else
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"
#include "../Lib/unicorn-2.1.4/unicorn/unicorn.h"
#endif
#include <pthread.h>
#include "fileIoEngine.h"
#include "vmMalloc.h"
#include "fontEngine.h"
#include "mystd.h"
#include "cbeParser.h"
/**
 * 定义
 * 0-1024 为栈空间
 * 1024-4096为代码空间
 */
void RunArmProgram(void *);
void hookBlockCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
void hookCodeCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
void hookRamCallBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
void SaveCpuContext(u32 *);
void RestoreCpuContext(u32 *stackPtr);
int utf16_len(char *utf16);
bool writeSDFile(u8 *Buffer, unsigned long long startPos, u32 size);
u8 *readSDFile(unsigned long long startPos, u32 size);
/* SDHC CSD：与 fat32.img 文件大小一致（扇区数，1024 对齐） */
void saveFlashFile();
void readFlashFile();
bool StartInterrupt(u32, u32);
void handleEvent_EMU(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
bool isIRQ_Disable(u32 cpsr);
bool isIrqMode(u32 cpsr);
void dumpCpuInfo();
void dumpVirtMemory(u32 addr, u32 len);
#ifdef CBE_PLATFORM_ANDROID
int cbeInit(const char *rootPath);
int cbeRun(void);
void cbeTaskListRun(void);
void cbeShutdown(void);
const char *cbeGetCpuInfoText(void);
int cbeAndroidInputIsOpen(void);
int cbeAndroidInputIsPassword(void);
int cbeAndroidInputGetSerial(void);
int cbeAndroidInputGetMaxLen(void);
int cbeAndroidInputGetInputType(void);
const char *cbeAndroidInputGetTextUtf8(void);
const char *cbeAndroidInputGetPromptUtf8(void);
void cbeAndroidInputSubmitUtf16(const unsigned short *text, int len, int cancelled);
#endif

u32 last_gpt1_interrupt_time;
u32 IRQ_MASK_SET_L_Data;
u32 IRQ_MASK_SET_H_Data;

u32 Global_R9;
u32 Program_ROM_Address;
u32 Program_ROM_Mapped_Size;
u32 Program_Data_Address;

u8 *ROM_MEMPOOL;
u8 *STACK_MEMPOOL;
u8 *PRAM_MEMPOOL;
u8 *RAM_MEMPOOL;

uc_engine *MTK;
u8 isBreakPointHit = 0;
u32 changeTmp = 0;
u32 changeTmp1 = 0;
u32 changeTmp2 = 0;
u32 changeTmp3 = 0;

SDL_Window *window;
u32 lastAddress = 1;

u8 emptyBuff[1024] = {0};
u8 globalSprintfBuff[256] = {0};
u8 sprintfBuff[256] = {0};
u8 cbeTextString[1024] = {0};
int irq_nested_count;
u32 irq_stack_ptr;
u32 debugType;

clock_t currentTime = 0;

int regs[] = {UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
              UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_R13, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR};

#ifdef GDB_SERVER_SUPPORT
/* GDB相关类型前向声明 */
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
#endif
