#pragma once
#include "main.h"
#include "vmDefine.h"

typedef enum
{
    VM_EVENT_NONE,
    VM_EVENT_KEYBOARD,
    VM_EVENT_Timer_IRQ,
    VM_EVENT_MSDC_IO_OK,
    VM_EVENT_MSDC_IO_CALLBACK,
    VM_EVENT_RTC_IRQ,
    VM_EVENT_GPT_IRQ,
    VM_EVENT_SIM_IRQ,
    VM_EVENT_SIM_T0_TX_END,
    VM_EVENT_SIM_T0_RX_END,
    VM_EVENT_DMA_IRQ,
    VM_EVENT_MSDC_IRQ,
    VM_EVENT_UART_IRQ,
    VM_EVENT_LCD_IRQ,
    VM_EVENT_TS_ADC_DONE_IRQ,
    VM_EVENT_TOUCHSCREEN,
    VM_EVENT_INPUT_CHAR,
    VM_EVENT_INPUT_BACKSPACE,
    VM_EVENT_INPUT_DONE,
    VM_EVENT_L1SM_IRQ,
    VM_EVENT_L1D_DSP_IRQ,
    VM_EVENT_AUDIO_DSP_IRQ,
    VM_EVENT_I2C_IRQ,
    VM_EVENT_EXIT

} VM_EVENT;

typedef struct vm_event_
{
    u32 event;
    u32 r0;
    u32 r1;
} vm_event;

void InitVmEvent();
void handleVmEvent_EMU(uint64_t address);
int EnqueueVMEvent(u32 event, u32 r0, u32 r1);
vm_event *DequeueVMEvent();
