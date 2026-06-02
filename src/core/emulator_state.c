#include "../main.h"

/* Shared emulator process state that is referenced across translation units. */
u32 last_gpt1_interrupt_time;
u32 IRQ_MASK_SET_L_Data;
u32 IRQ_MASK_SET_H_Data;

u32 Global_R9;

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
