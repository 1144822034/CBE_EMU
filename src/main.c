#define GDB_SERVER_SUPPORT_
#define GDI_LAYER_DEBUG_

#define DEBUG_PRINT(...) ((void)0)
#define TRACE_STARTUP_UI 0
#define TRACE_RESOURCE_IO 0
#define TRACE_LCD_TEXT 0
#define TRACE_LCD_SHAPES 0

#ifdef _WIN32
#include <direct.h>
#endif
#include <stdarg.h>

#include "main.h"
#include "lcd.h"
#include "vmFunc.c"
#include "hookRam.c"
#include "vmEvent.c"

#ifdef GDB_SERVER_SUPPORT
#include "gdb_client.c"
pthread_t gdb_server_mutex;

void readMemoryToGdb(unsigned int addr, unsigned int length, void *buffer)
{
    uc_mem_read(MTK, addr, buffer, length);
}
void writeMemoryToGdb(unsigned int addr, char value)
{
    uc_mem_write(MTK, addr, &value, 1);
}
void writeRegToGdb(u32 reg, u32 value)
{
    if (reg == 0)
        uc_reg_write(MTK, UC_ARM_REG_R0, &value);
    else if (reg == 1)
        uc_reg_write(MTK, UC_ARM_REG_R1, &value);
    else if (reg == 2)
        uc_reg_write(MTK, UC_ARM_REG_R2, &value);
    else if (reg == 3)
        uc_reg_write(MTK, UC_ARM_REG_R3, &value);
    else if (reg == 4)
        uc_reg_write(MTK, UC_ARM_REG_R4, &value);
    else if (reg == 5)
        uc_reg_write(MTK, UC_ARM_REG_R5, &value);
    else if (reg == 6)
        uc_reg_write(MTK, UC_ARM_REG_R6, &value);
    else if (reg == 7)
        uc_reg_write(MTK, UC_ARM_REG_R7, &value);
    else if (reg == 8)
        uc_reg_write(MTK, UC_ARM_REG_R8, &value);
    else if (reg == 9)
        uc_reg_write(MTK, UC_ARM_REG_R9, &value);
    else if (reg == 10)
        uc_reg_write(MTK, UC_ARM_REG_R10, &value);
    else if (reg == 11)
        uc_reg_write(MTK, UC_ARM_REG_R11, &value);
    else if (reg == 12)
        uc_reg_write(MTK, UC_ARM_REG_R12, &value);
    else if (reg == 13)
        uc_reg_write(MTK, UC_ARM_REG_R13, &value);
    else if (reg == 14)
        uc_reg_write(MTK, UC_ARM_REG_R14, &value);
    else if (reg == 15)
        uc_reg_write(MTK, UC_ARM_REG_R15, &value);
    else if (reg == 16)
        uc_reg_write(MTK, UC_ARM_REG_CPSR, &value);
}

void ReadRegsToGdb(int *regPtr)
{
    uc_reg_read(MTK, UC_ARM_REG_R0, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R1, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R2, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R3, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R4, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R5, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R6, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R7, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R8, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R9, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R10, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R11, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R12, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R13, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R14, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_R15, regPtr++);
    uc_reg_read(MTK, UC_ARM_REG_CPSR, regPtr++);
}
#endif

u32 Interrupt_Handler_Entry; // 中断入口地址

u8 ucs2Tmp[128] = {0}; // utf16-le转utf-8 缓存空间

FILE *SD_File_Handle;
pthread_mutex_t mutex; // 线程锁

u8 isStepNext = 0;

SDL_Keycode isKeyDown = SDLK_UNKNOWN;
pthread_t EmuThread;
pthread_t MainUpdareThread;

bool isMouseDown = false;
u8 currentProgramDir[256] = {0};

u32 stackCallback[17];

int simulatePress = 0;
int simulateKey = 0;
int simulateTouchPress = 0;
int simulateTouchDown = 0;
int simulateTouchUp = 0;
int simulateTouchDrag = 0;
int simulateTouchX = 0;
int simulateTouchY = 0;
static int g_vmInputOpen = 0;
static int g_vmInputPassword = 0;
static u32 g_vmInputCallback = 0;
static u32 g_vmInputBuffer = 0;
static u32 g_vmInputMaxLen = 0;
static u32 g_vmInputInputType = 0;
static int g_vmInputOverlayX = 12;
static int g_vmInputOverlayY = 348;
static int g_vmInputOverlayW = 216;
static int g_vmInputOverlayH = 22;
static int g_autoTouchConfigured = 0;
static int g_autoTouchFired = 0;
static u32 g_autoTouchTick = 0;
static int g_autoTouchX = 0;
static int g_autoTouchY = 0;
u32 screenStructChange = 0;
u32 screenStructNotifyLoadRes = 0;
u32 vmAddedScreen = 0;
static u32 g_screenStack[32];
static u32 g_screenStackCount = 0;
static u32 g_screenRemovedWithoutNext = 0;
static u32 g_screenResumeExisting = 0;
static u32 g_activeScreenRemovedThisFrame = 0;
static u32 g_currentScreenThis = 0;
u32 g_currentScreenModuleBase = 0;

u32 lastSprintfPtr = 0;
static u8 *g_cbeFileBuffer = NULL;
static u32 g_cbeFileSize = 0;

#define VM_SCHED_MAX_NET_TASKS 8
#define VM_SCHED_MAX_TIMERS 20
#define VM_SCHED_TIMER_BASE_ID 100
#define VM_SCHED_FRAME_MS 100

typedef struct
{
    u8 active;
    u8 fired;
    u8 downloadSnapshotValid;
    u8 downloadSnapshotState;
    u16 delayTicks;
    u32 eventType;
    u32 r0;
    u32 r1;
    u32 r2;
    u32 callback;
    u32 context;
    u8 downloadSnapshot[0x60];
} vm_net_task;

typedef struct
{
    u8 active;
    u32 connectId;
    u32 callback;
    u32 context;
} vm_net_channel;

typedef struct
{
    u8 active;
    u16 handle;
    u32 remainingTicks;
    u32 callback;
    u32 context;
} vm_timer_task;

static u32 g_schedulerTick = 0;
static vm_net_task g_netTasks[VM_SCHED_MAX_NET_TASKS];
static vm_net_channel g_netChannels[VM_SCHED_MAX_NET_TASKS];
static vm_timer_task g_timerTasks[VM_SCHED_MAX_TIMERS];
static u32 g_schedulerStartTicks = 0;
static u32 g_nextNetConnectId = 1;
static u8 g_netMockResponse[131072];
static u32 g_netMockResponseLen = 0;
static u32 g_netMockResponseOffset = 0;
static u32 g_netMockResponseVmPtr = 0;
static u8 g_netMockUpdateDelivered = 0;
static u32 g_netMockEnterGameOffset = 0;
static u32 g_netMockEnterGameChecksum = 0;
static u32 g_netUpLinkData = 0;
static u32 g_netDownLinkData = 0;
static u32 g_netCurrentObject = 0;
static const u32 VM_GAME_NET_BUSINESS_CALLBACK = 0x01012e4d;
static u8 g_lastStartupScreenState = 0xff;
static u32 g_lastStartupUpdateObj = 0xffffffff;
static u8 g_lastStartupProgress = 0xff;
static u8 g_lastStartupUpdateState = 0xff;
static u32 g_currentFontType = 0;

static uc_err add_manager_code_hooks(uc_engine *uc);
static void vm_net_trace(const char *fmt, ...);
static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);

static bool vm_address_in_range(u32 address, u32 begin, u32 size)
{
    return address >= begin && address < begin + size;
}

static bool vm_is_manager_func_stub_address(u32 address)
{
    if (vm_address_in_range(address, VM_MANAGER_FUNC_LIST_ADDRESS, VM_VIDEO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE - VM_MANAGER_FUNC_LIST_ADDRESS))
        return true;
    if (vm_address_in_range(address, VM_DL_PAY_FUNC_LIST_ADDRESS, VM_DL_IMAGE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE - VM_DL_PAY_FUNC_LIST_ADDRESS))
        return true;
    if (vm_address_in_range(address, VM_MF_MemoryBlock_FUNC_LIST_ADDRESS, VM_APPSTORE_FUNC_LIST_ADDRESS - VM_MF_MemoryBlock_FUNC_LIST_ADDRESS))
        return true;
    if (vm_address_in_range(address, VM_APPSTORE_FUNC_LIST_ADDRESS, VM_MANAGER_FUNC_LIST_SIZE))
        return true;
    return false;
}

static int vm_lcd_coord_from_reg(u32 value)
{
    return (int)(int16_t)(value & 0xffff);
}

static void vm_trace_lcd_shape(const char *apiName, int x, int y, int w, int h, u32 color)
{
#if TRACE_LCD_SHAPES
    if (y >= 35 && y <= 130)
        vm_net_trace("lcd_shape api=%s x=%d y=%d w=%d h=%d color=%08x last=%08x\n",
                     apiName, x, y, w, h, color, lastAddress);
#else
    (void)apiName;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
#endif
}

static void vm_lcd_draw_line(int x0, int y0, int x1, int y1, u16 color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        if (x0 >= 0 && x0 < LCD_WIDTH && y0 >= 0 && y0 < LCD_HEIGHT)
            ((u16 *)Lcd_Cache_Buffer)[y0 * LCD_PITCH + x0] = color;
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static int vm_lcd_try_unpack_packed_rect(u32 p0, u32 p1, int *x, int *y, int *w, int *h)
{
    if (((p0 | p1) & 0xffff0000u) == 0)
        return 0;

    int x0 = vm_lcd_coord_from_reg(p0);
    int y0 = vm_lcd_coord_from_reg(p0 >> 16);
    int x1 = vm_lcd_coord_from_reg(p1);
    int y1 = vm_lcd_coord_from_reg(p1 >> 16);

    if (x0 < -LCD_WIDTH || x0 > LCD_WIDTH * 2 ||
        x1 < -LCD_WIDTH || x1 > LCD_WIDTH * 2 ||
        y0 < -LCD_HEIGHT || y0 > LCD_HEIGHT * 2 ||
        y1 < -LCD_HEIGHT || y1 > LCD_HEIGHT * 2)
        return 0;

    if (x1 < x0)
    {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0)
    {
        int t = y0;
        y0 = y1;
        y1 = t;
    }

    *x = x0;
    *y = y0;
    *w = x1 - x0 + 1;
    *h = y1 - y0 + 1;
    return 1;
}

static u32 vm_df_get_resource_by_id(u32 id)
{
    return vm_DF_GetResourceByResourceID(id);
}

static u32 vm_df_get_resource_by_file_name(u32 namePtr)
{
    return vm_DF_GetResourceByFileName(namePtr);
}

static u32 vm_df_get_resource_name_by_id(u32 id)
{
    vm_DF_GetResourceNameByID(id);
    return 0;
}

static u32 vm_df_get_resource_id_by_file_name(u32 namePtr)
{
    return vm_DF_GetResourceIDByFileName(namePtr);
}

static u32 vm_df_get_t_resource(u32 namePtr, int stream)
{
    return stream ? vm_DF_GetStreamTResource(namePtr) : vm_DF_GetTResource(namePtr);
}

static void scheduler_normalize_startup_screen_state(void)
{
    u32 debugUiRoot = Global_R9 + 0x9928;
    u32 debugUiObj = 0;
    uc_mem_read(MTK, debugUiRoot + 0x10, &debugUiObj, 4);
    if (debugUiObj == 0)
        return;

    u8 state = 0;
    u32 updateObj = 0;
    u32 imageTable = 0;
    short imageIndexA = 0;
    short imageIndexB = 0;
    uc_mem_read(MTK, debugUiObj + 0x3d, &state, 1);
    uc_mem_read(MTK, debugUiObj + 0x140, &updateObj, 4);
    uc_mem_read(MTK, debugUiObj + 0x50, &imageTable, 4);
    uc_mem_read(MTK, debugUiObj + 0x34, &imageIndexA, 2);
    uc_mem_read(MTK, debugUiObj + 0x36, &imageIndexB, 2);
    if (state != g_lastStartupScreenState || updateObj != g_lastStartupUpdateObj)
    {
        u8 hasLocalUpdate = 0;
        u8 progress = 0;
        u8 updateState = 0;
        uc_mem_read(MTK, Global_R9 + 0x5496, &hasLocalUpdate, 1);
        uc_mem_read(MTK, Global_R9 + 0x5494, &progress, 1);
        uc_mem_read(MTK, Global_R9 + 0x4cb6, &updateState, 1);
        vm_net_trace("startup_screen obj=%08x state=%u updateObj=%08x hasLocalUpdate=%u progress=%u updateState=%u imageTable=%08x idx=%d,%d tick=%u\n",
                     debugUiObj, state, updateObj, hasLocalUpdate, progress, updateState, imageTable, imageIndexA, imageIndexB, g_schedulerTick);
        g_lastStartupScreenState = state;
        g_lastStartupUpdateObj = updateObj;
        g_lastStartupProgress = progress;
        g_lastStartupUpdateState = updateState;
    }
    else
    {
        u8 progress = 0;
        u8 updateState = 0;
        uc_mem_read(MTK, Global_R9 + 0x5494, &progress, 1);
        uc_mem_read(MTK, Global_R9 + 0x4cb6, &updateState, 1);
        if (progress != g_lastStartupProgress || updateState != g_lastStartupUpdateState)
        {
            vm_net_trace("startup_progress progress=%u updateState=%u state=%u tick=%u\n", progress, updateState, state, g_schedulerTick);
            g_lastStartupProgress = progress;
            g_lastStartupUpdateState = updateState;
        }
    }
    if (state == 10 && updateObj == 0)
    {
#if TRACE_STARTUP_UI
        vm_net_trace("startup_state_waiting state=10 updateObj=0 tick=%u\n", g_schedulerTick);
#endif
    }
}

static void scheduler_trace_startup_ui_object(const char *phase, u32 screenPtr)
{
#if TRACE_STARTUP_UI
    u32 debugUiRoot = Global_R9 + 0x9928;
    u32 debugUiObj = 0;
    u32 imageTable = 0;
    u32 item0 = 0;
    u32 item1 = 0;
    u32 item3 = 0;
    short imageIndexA = 0;
    short imageIndexB = 0;
    u8 state = 0;
    u32 entry0 = 0, entry4 = 0, entry8 = 0, entry12 = 0, entry16 = 0, entry20 = 0, entry24 = 0;
    if (screenPtr)
    {
        uc_mem_read(MTK, screenPtr + 0x00, &entry0, 4);
        uc_mem_read(MTK, screenPtr + 0x04, &entry4, 4);
        uc_mem_read(MTK, screenPtr + 0x08, &entry8, 4);
        uc_mem_read(MTK, screenPtr + 0x0c, &entry12, 4);
        uc_mem_read(MTK, screenPtr + 0x10, &entry16, 4);
        uc_mem_read(MTK, screenPtr + 0x14, &entry20, 4);
        uc_mem_read(MTK, screenPtr + 0x18, &entry24, 4);
    }
    uc_mem_read(MTK, debugUiRoot + 0x10, &debugUiObj, 4);
    if (debugUiObj == 0)
        return;
    uc_mem_read(MTK, debugUiObj + 0x50, &imageTable, 4);
    uc_mem_read(MTK, debugUiObj + 0x34, &imageIndexA, 2);
    uc_mem_read(MTK, debugUiObj + 0x36, &imageIndexB, 2);
    uc_mem_read(MTK, debugUiObj + 0x3d, &state, 1);
    if (imageTable)
    {
        uc_mem_read(MTK, imageTable + 0 * 4, &item0, 4);
        uc_mem_read(MTK, imageTable + 1 * 4, &item1, 4);
        uc_mem_read(MTK, imageTable + 3 * 4, &item3, 4);
    }
    vm_net_trace("startup_ui %s screen=%08x table=%08x,%08x,%08x,%08x,%08x,%08x,%08x obj=%08x state=%u imageTable=%08x idx=%d,%d items=%08x,%08x,%08x\n",
                 phase, screenPtr, entry0, entry4, entry8, entry12, entry16, entry20, entry24,
                 debugUiObj, state, imageTable, imageIndexA, imageIndexB, item0, item1, item3);
#else
    (void)phase;
    (void)screenPtr;
#endif
}

static uc_err vm_emu_start(u32 begin, u32 until);
static bool vm_is_pool_entry(u32 entry);
static void vm_restore_r9_for_entry(u32 entry);

static int vm_screen_stack_find(u32 screen)
{
    for (u32 i = 0; i < g_screenStackCount; ++i)
    {
        if (g_screenStack[i] == screen)
            return (int)i;
    }
    return -1;
}

static void vm_screen_stack_push(u32 screen)
{
    if (screen == 0)
        return;

    int existing = vm_screen_stack_find(screen);
    if (existing >= 0)
    {
        for (u32 i = (u32)existing; i + 1 < g_screenStackCount; ++i)
            g_screenStack[i] = g_screenStack[i + 1];
        g_screenStackCount--;
    }

    if (g_screenStackCount >= sizeof(g_screenStack) / sizeof(g_screenStack[0]))
    {
        memmove(g_screenStack, g_screenStack + 1, (sizeof(g_screenStack) / sizeof(g_screenStack[0]) - 1) * sizeof(g_screenStack[0]));
        g_screenStackCount--;
    }

    g_screenStack[g_screenStackCount++] = screen;
}

static bool vm_screen_stack_remove(u32 screen, u32 *newTop)
{
    int existing = vm_screen_stack_find(screen);
    if (existing < 0)
    {
        if (newTop)
            *newTop = g_screenStackCount ? g_screenStack[g_screenStackCount - 1] : 0;
        return false;
    }

    for (u32 i = (u32)existing; i + 1 < g_screenStackCount; ++i)
        g_screenStack[i] = g_screenStack[i + 1];
    g_screenStackCount--;

    if (newTop)
        *newTop = g_screenStackCount ? g_screenStack[g_screenStackCount - 1] : 0;
    return true;
}

u32 size_128mb = 1024 * 1024 * 128;
u32 size_32mb = 1024 * 1024 * 32;
u32 size_16mb = 1024 * 1024 * 16;
u32 size_8mb = 1024 * 1024 * 8;
u32 size_4mb = 1024 * 1024 * 4;
u32 size_1mb = 1024 * 1024;
u32 size_2kb = 1024 * 2;

/* initMtkSimalator 里 IDA XRAM 后备缓冲首址，供 Find* 在映像溢出段扫 magic */
static u8 *s_ida_xram_host = NULL;

u32 *isrStackPtr;
u32 isrStackList[100][17];

u32 buff1, buff2;
char *pp;

u32 sendCount;

void dumpVirtMemory(u32 addr, u32 len)
{
    uc_mem_read(MTK, addr, globalSprintfBuff, len);
    printf("dumpMemory[%x]\n", addr);
    for (u32 i = 0; i < len; i++)
    {
        printf(" %x ", globalSprintfBuff[i]);
    }
    printf("\n");
}

void vm_bx(u32 addr)
{
    u32 cpsr;
    uc_reg_read(MTK, UC_ARM_REG_CPSR, &cpsr);
    if (addr & 1)
        cpsr |= 0x20;
    else
        cpsr &= ~0x20u;
    uc_reg_write(MTK, UC_ARM_REG_CPSR, &cpsr);
    uc_reg_write(MTK, UC_ARM_REG_PC, &addr);
}

static u32 g_currentEmuEntry = 0;

static void normalize_program_exit_pc(u32 fallbackPc)
{
    u32 pc = 0;
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    if ((pc & ~1u) == PROGRAM_EXIT_ADDR)
    {
        if (fallbackPc != 0 && (fallbackPc & ~1u) != PROGRAM_EXIT_ADDR)
            uc_reg_write(MTK, UC_ARM_REG_PC, &fallbackPc);
        else if (lastAddress != 0 && (lastAddress & ~1u) != PROGRAM_EXIT_ADDR)
            uc_reg_write(MTK, UC_ARM_REG_PC, &lastAddress);
    }
}

static uc_err vm_emu_start(u32 begin, u32 until)
{
    g_currentEmuEntry = begin;
    if (Global_R9)
        vm_restore_r9_for_entry(begin);
    uc_err err = uc_emu_start(MTK, begin, until, 0, 0);
    normalize_program_exit_pc(begin);
    return err;
}

static uc_err vm_emu_start_count(u32 begin, u32 until, uint64_t count)
{
    g_currentEmuEntry = begin;
    if (Global_R9)
        vm_restore_r9_for_entry(begin);
    uc_err err = uc_emu_start(MTK, begin, until, 0, count);
    normalize_program_exit_pc(begin);
    return err;
}

static bool vm_is_pool_entry(u32 entry)
{
    u32 pc = entry & ~1u;
    return pc >= VM_Memory_Pool_ADDRESS && pc < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE;
}

static void vm_restore_r9_for_entry(u32 entry)
{
    u32 r9 = vm_is_pool_entry(entry) && g_currentScreenModuleBase ? g_currentScreenModuleBase : Global_R9;
    if (r9)
    {
        if (vm_is_pool_entry(entry))
        {
            uc_reg_write(MTK, UC_ARM_REG_R9, &r9);
            return;
        }
        uc_reg_write(MTK, UC_ARM_REG_R9, &r9);
    }
}

static uc_err vm_call4(u32 entry, u32 r0, u32 r1, u32 r2, u32 r3)
{
    u32 lr = PROGRAM_EXIT_ADDR | 1;
    vm_restore_r9_for_entry(entry);
    uc_reg_write(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_write(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_write(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_write(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_write(MTK, UC_ARM_REG_R3, &r3);
    return vm_emu_start(entry | 1, PROGRAM_EXIT_ADDR);
}

static uc_err vm_call4_preserve_regs(u32 entry, u32 r0, u32 r1, u32 r2, u32 r3)
{
    static const int preserveRegs[] = {
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_R2,
        UC_ARM_REG_R3,
        UC_ARM_REG_R4,
        UC_ARM_REG_R5,
        UC_ARM_REG_R6,
        UC_ARM_REG_R7,
        UC_ARM_REG_R8,
        UC_ARM_REG_R9,
        UC_ARM_REG_R10,
        UC_ARM_REG_R11,
        UC_ARM_REG_R12,
        UC_ARM_REG_SP,
        UC_ARM_REG_LR,
        UC_ARM_REG_CPSR,
    };
    u32 saved[mySizeOf(preserveRegs)] = {0};
    for (u32 i = 0; i < mySizeOf(preserveRegs); ++i)
        uc_reg_read(MTK, preserveRegs[i], &saved[i]);

    uc_err err = vm_call4(entry, r0, r1, r2, r3);

    for (u32 i = 0; i < mySizeOf(preserveRegs); ++i)
        uc_reg_write(MTK, preserveRegs[i], &saved[i]);
    return err;
}

static uc_err vm_call4_preserve_regs_clear_stack_args(u32 entry, u32 r0, u32 r1, u32 r2, u32 r3)
{
    static const int preserveRegs[] = {
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_R2,
        UC_ARM_REG_R3,
        UC_ARM_REG_R4,
        UC_ARM_REG_R5,
        UC_ARM_REG_R6,
        UC_ARM_REG_R7,
        UC_ARM_REG_R8,
        UC_ARM_REG_R9,
        UC_ARM_REG_R10,
        UC_ARM_REG_R11,
        UC_ARM_REG_R12,
        UC_ARM_REG_SP,
        UC_ARM_REG_LR,
        UC_ARM_REG_CPSR,
    };
    u32 saved[mySizeOf(preserveRegs)] = {0};
    for (u32 i = 0; i < mySizeOf(preserveRegs); ++i)
        uc_reg_read(MTK, preserveRegs[i], &saved[i]);

    u32 sp = saved[13] - 32;
    u8 zeroStackArgs[32] = {0};
    uc_mem_write(MTK, sp, zeroStackArgs, sizeof(zeroStackArgs));
    uc_reg_write(MTK, UC_ARM_REG_SP, &sp);

    uc_err err = vm_call4(entry, r0, r1, r2, r3);

    for (u32 i = 0; i < mySizeOf(preserveRegs); ++i)
        uc_reg_write(MTK, preserveRegs[i], &saved[i]);
    return err;
}

static void scheduler_prepare_screen_call(u32 screenThisPtr)
{
    if (screenThisPtr != g_currentScreenThis)
    {
        g_currentScreenThis = screenThisPtr;
        g_currentScreenModuleBase = 0;
    }
    /* CBM screens pass through vmAddScreen from dynamic code; capture that
     * caller's R9 there.  The screen object's first word is game data, not SB.
     */
}

static u32 scheduler_get_tick_ms(void)
{
    u32 now = SDL_GetTicks();
    if (g_schedulerStartTicks == 0)
        g_schedulerStartTicks = now;
    return now - g_schedulerStartTicks;
}

static u32 scheduler_start_timer(u32 delayMs, u32 callback, u32 context)
{
    if (callback == 0)
        return vm_set_call_result(0);
    for (u32 i = 0; i < VM_SCHED_MAX_TIMERS; ++i)
    {
        if (!g_timerTasks[i].active)
        {
            g_timerTasks[i].active = 1;
            g_timerTasks[i].handle = (u16)(VM_SCHED_TIMER_BASE_ID + i);
            g_timerTasks[i].remainingTicks = (delayMs + VM_SCHED_FRAME_MS - 1) / VM_SCHED_FRAME_MS;
            if (g_timerTasks[i].remainingTicks == 0)
                g_timerTasks[i].remainingTicks = 1;
            g_timerTasks[i].callback = callback;
            g_timerTasks[i].context = context;
            DEBUG_PRINT("[probe_timer] start handle=%u delay=%u cb=%x ctx=%x tick=%u\n", g_timerTasks[i].handle, delayMs, callback, context, g_schedulerTick);
            return vm_set_call_result(g_timerTasks[i].handle);
        }
    }
    printf("vMStartTimer: timer pool full\n");
    assert(0);
    return vm_set_call_result(0);
}

static u32 scheduler_stop_timer(u32 handle)
{
    if (handle >= VM_SCHED_TIMER_BASE_ID && handle < VM_SCHED_TIMER_BASE_ID + VM_SCHED_MAX_TIMERS)
    {
        vm_timer_task *task = &g_timerTasks[handle - VM_SCHED_TIMER_BASE_ID];
        task->active = 0;
        task->remainingTicks = 0;
        task->callback = 0;
        task->context = 0;
    }
    return vm_set_call_result(0);
}

static uc_err scheduler_dispatch_timers(void)
{
    for (u32 i = 0; i < VM_SCHED_MAX_TIMERS; ++i)
    {
        vm_timer_task *task = &g_timerTasks[i];
        if (!task->active)
            continue;
        if (task->remainingTicks > 0)
        {
            task->remainingTicks--;
            if (task->remainingTicks > 0)
                continue;
        }
        u32 callback = task->callback;
        u32 context = task->context;
        DEBUG_PRINT("[probe_timer] fire handle=%u cb=%x ctx=%x tick=%u\n", task->handle, callback, context, g_schedulerTick);
        task->active = 0;
        task->remainingTicks = 0;
        task->callback = 0;
        task->context = 0;
        uc_err err = vm_call4_preserve_regs(callback, context, 0, 0, 0);
        if (err != UC_ERR_OK)
            return err;
    }
    return UC_ERR_OK;
}

static void scheduler_register_net_channel(u32 connectId, u32 callback, u32 context)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netChannels[i].active && g_netChannels[i].connectId == connectId)
        {
            g_netChannels[i].callback = callback;
            g_netChannels[i].context = context;
            return;
        }
    }
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (!g_netChannels[i].active)
        {
            g_netChannels[i].active = 1;
            g_netChannels[i].connectId = connectId;
            g_netChannels[i].callback = callback;
            g_netChannels[i].context = context;
            return;
        }
    }
}

static vm_net_channel *scheduler_find_net_channel(u32 connectId)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netChannels[i].active && g_netChannels[i].connectId == connectId)
            return &g_netChannels[i];
    }
    return NULL;
}

static void scheduler_unregister_net_channel(u32 connectId)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netChannels[i].active && g_netChannels[i].connectId == connectId)
        {
            memset(&g_netChannels[i], 0, sizeof(g_netChannels[i]));
            return;
        }
    }
}

static void scheduler_queue_net_event(u32 eventType, u32 r0, u32 r1, u32 r2, u32 callback, u32 context)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netTasks[i].active && g_netTasks[i].eventType == eventType && g_netTasks[i].r0 == r0 && g_netTasks[i].r1 == r1 && g_netTasks[i].r2 == r2 && g_netTasks[i].callback == callback && g_netTasks[i].context == context)
            return;
    }
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (!g_netTasks[i].active)
        {
            g_netTasks[i].active = 1;
            g_netTasks[i].fired = 0;
            g_netTasks[i].delayTicks = eventType == 7 ? 0 : 6;
            g_netTasks[i].eventType = eventType;
            g_netTasks[i].r0 = r0;
            g_netTasks[i].r1 = r1;
            g_netTasks[i].r2 = r2;
            g_netTasks[i].callback = callback;
            g_netTasks[i].context = context;
            g_netTasks[i].downloadSnapshotValid = 0;
            g_netTasks[i].downloadSnapshotState = 0;
            memset(g_netTasks[i].downloadSnapshot, 0, sizeof(g_netTasks[i].downloadSnapshot));
            if (eventType == 7 && Global_R9)
            {
                u32 downloadBase = Global_R9 + 0x9584;
                if (uc_mem_read(MTK, downloadBase, g_netTasks[i].downloadSnapshot, sizeof(g_netTasks[i].downloadSnapshot)) == UC_ERR_OK)
                {
                    u32 bufferPtr = 0;
                    u32 received = 0;
                    u32 capacity = 0;
                    g_netTasks[i].downloadSnapshotValid = 1;
                    g_netTasks[i].downloadSnapshotState = g_netTasks[i].downloadSnapshot[0];
                    memcpy(&bufferPtr, g_netTasks[i].downloadSnapshot + 0x14, sizeof(bufferPtr));
                    memcpy(&received, g_netTasks[i].downloadSnapshot + 0x18, sizeof(received));
                    memcpy(&capacity, g_netTasks[i].downloadSnapshot + 0x28, sizeof(capacity));
                    vm_net_trace("queue_download_snapshot state=%u buffer=%08x received=%u capacity=%u base=%08x\n",
                                 g_netTasks[i].downloadSnapshotState, bufferPtr, received, capacity, downloadBase);
                }
            }
            DEBUG_PRINT("[probe_net] queue event=%u r0=%x r1=%x r2=%x cb=%x ctx=%x tick=%u last=%x\n", eventType, r0, r1, r2, callback, context, g_schedulerTick, lastAddress);
            vm_net_trace("queue_event event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x tick=%u last=%08x\n", eventType, r0, r1, r2, callback, context, g_schedulerTick, lastAddress);
            return;
        }
    }
}

static void scheduler_queue_net_task(u32 r0, u32 r1, u32 callback, u32 context)
{
    (void)r0;
    (void)r1;
    scheduler_queue_net_event(5, 0, 0, 0, callback, context);
}

static uc_err scheduler_dispatch_net_tasks(void)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        vm_net_task *task = &g_netTasks[i];
        if (!task->active)
            continue;
        if (task->delayTicks > 0)
        {
            task->delayTicks--;
            continue;
        }
        if (!task->fired && task->callback)
        {
            task->fired = 1;
            task->active = 0;
            DEBUG_PRINT("[probe_net_fire] event=%u r0=%x r1=%x r2=%x cb=%x ctx=%x tick=%u\n", task->eventType, task->r0, task->r1, task->r2, task->callback, task->context, g_schedulerTick);
            vm_net_trace("fire_event event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x tick=%u\n", task->eventType, task->r0, task->r1, task->r2, task->callback, task->context, g_schedulerTick);
            if (task->eventType == 7)
            {
                u32 netCb14 = 0;
                u32 netCb44 = 0;
                uc_mem_read(MTK, Global_R9 + 0x9588 + 0x14, &netCb14, 4);
                uc_mem_read(MTK, Global_R9 + 0x9588 + 0x44, &netCb44, 4);
                vm_net_trace("net_state_observe cb14=%08x cb44=%08x\n", netCb14, netCb44);
                if (task->downloadSnapshotValid && task->downloadSnapshotState == 2)
                {
                    u32 downloadBase = Global_R9 + 0x9584;
                    uc_mem_write(MTK, downloadBase, task->downloadSnapshot, sizeof(task->downloadSnapshot));
                    vm_net_trace("restore_download_snapshot state=%u base=%08x\n", task->downloadSnapshotState, downloadBase);
                }
            }
            uc_err err = vm_call4_preserve_regs_clear_stack_args(task->callback, task->r0, task->r1, task->r2, task->eventType);
            if (err != UC_ERR_OK)
                vm_net_trace("net_callback_error event=%u cb=%08x err=%u\n", task->eventType, task->callback, err);
            if (task->eventType == 7)
            {
                u8 updateFlags[4] = {0};
                u8 hasLocalUpdate = 0;
                u8 updateState = 0;
                u8 downloadState = 0;
                u8 updateIndex = 0;
                u16 updateRemain = 0;
                u16 updateDoneCount = 0;
                u32 downloadReceived = 0;
                u32 downloadTotal = 0;
                uc_mem_read(MTK, Global_R9 + 0x954c + 0x10, updateFlags, sizeof(updateFlags));
                uc_mem_read(MTK, Global_R9 + 0x954c + 0x8, &updateIndex, 1);
                uc_mem_read(MTK, Global_R9 + 0x954c + 0x8, &updateRemain, 2);
                uc_mem_read(MTK, Global_R9 + 0x954c + 0xa, &updateDoneCount, 2);
                uc_mem_read(MTK, Global_R9 + 0x5496, &hasLocalUpdate, 1);
                uc_mem_read(MTK, Global_R9 + 0x4cb6, &updateState, 1);
                uc_mem_read(MTK, Global_R9 + 0x95e8, &downloadState, 1);
                uc_mem_read(MTK, Global_R9 + 0x95e8 + 0x18, &downloadReceived, 4);
                uc_mem_read(MTK, Global_R9 + 0x95e8 + 0x28, &downloadTotal, 4);
                if (downloadState == 3 && updateFlags[0] == 0 && updateFlags[1] == 1 && updateFlags[2] == 1 && updateFlags[3] == 1)
                {
                    vm_net_trace("update_chunk_complete_observed flag=0 downloadState=3\n");
                }
                if (updateState == 2 && updateFlags[0] == 1 && updateFlags[1] == 1 && updateFlags[2] == 1 && updateFlags[3] == 1 &&
                    !g_netMockUpdateDelivered)
                {
                    g_netMockUpdateDelivered = 1;
                    vm_net_trace("startup_no_update_complete flags=1,1,1,1\n");
                }
                vm_net_trace("after_data_event updateState=%u downloadState=%u updateIndex=%u remain=%u done=%u received=%u total=%u hasLocalUpdate=%u flags=%u,%u,%u,%u\n",
                             updateState, downloadState, updateIndex, updateRemain, updateDoneCount, downloadReceived, downloadTotal, hasLocalUpdate, updateFlags[0], updateFlags[1], updateFlags[2], updateFlags[3]);
            }
            if (err != UC_ERR_OK)
                return err;
        }
    }
    return UC_ERR_OK;
}

typedef struct
{
    const char *name;
    const char *contains;
    const char *responseFile;
    const u8 *response;
    u32 responseLen;
} vm_net_mock_rule;

static bool vm_net_mock_request_contains(const u8 *request, u32 requestLen, const char *needle)
{
    u32 needleLen = needle ? (u32)strlen(needle) : 0;
    if (request == NULL || needleLen == 0 || requestLen < needleLen)
        return false;

    for (u32 i = 0; i + needleLen <= requestLen; ++i)
    {
        if (memcmp(request + i, needle, needleLen) == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_request_has_empty_field(const u8 *request, u32 requestLen, const char *field)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || requestLen < fieldLen + 4)
        return false;

    for (u32 i = 0; i + fieldLen + 4 <= requestLen; ++i)
    {
        if (memcmp(request + i, field, fieldLen) != 0)
            continue;
        u32 p = i + fieldLen;
        if (p + 4 <= requestLen && request[p] == 0 && request[p + 1] == 0x02 && request[p + 2] == 0 && request[p + 3] == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_get_object_u8_field(const u8 *request, u32 requestLen, const char *field, u8 *value)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p < requestLen && request[p] == 0)
            p++;
        if (p + 4 <= requestLen && request[p] == 0x03 && request[p + 1] == 0 && request[p + 2] == 1)
        {
            if (value)
                *value = request[p + 3];
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_get_object_u32_field(const u8 *request, u32 requestLen, const char *field, u32 *value)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 8)
        return false;

    for (u32 i = 0; i + fieldLen + 8 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p < requestLen && request[p] == 0)
            p++;
        if (p + 7 <= requestLen && request[p] == 0x06 && request[p + 1] == 0 && request[p + 2] == 4)
        {
            if (value)
                *value = ((u32)request[p + 3] << 24) | ((u32)request[p + 4] << 16) |
                         ((u32)request[p + 5] << 8) | (u32)request[p + 6];
            return true;
        }
    }
    return false;
}

static void vm_net_ensure_business_callback(const char *reason)
{
    if (g_netCurrentObject == 0)
        return;

    u32 callback = 0;
    u32 callbackAddr = g_netCurrentObject + 0x14;
    if (uc_mem_read(MTK, callbackAddr, &callback, 4) != UC_ERR_OK)
        return;
    if (callback != 0)
    {
        vm_net_trace("business_cb_existing obj=%08x cb=%08x reason=%s\n", g_netCurrentObject, callback, reason ? reason : "");
        return;
    }

    callback = VM_GAME_NET_BUSINESS_CALLBACK;
    uc_mem_write(MTK, callbackAddr, &callback, 4);
    vm_net_trace("business_cb_install obj=%08x cb=%08x reason=%s\n", g_netCurrentObject, callback, reason ? reason : "");
}

static u32 vm_net_mock_copy_response(const u8 *response, u32 responseLen, u8 *out, u32 outCap)
{
    if (response == NULL || responseLen == 0 || out == NULL || outCap == 0)
        return 0;
    u32 copyLen = responseLen < outCap ? responseLen : outCap;
    memcpy(out, response, copyLen);
    return copyLen;
}

static u32 vm_net_mock_load_response_file(const char *path, u8 *out, u32 outCap)
{
    if (path == NULL || out == NULL || outCap == 0)
        return 0;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    size_t len = fread(out, 1, outCap, fp);
    fclose(fp);
    return (u32)len;
}

static bool vm_net_mock_file_has_data(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size > 40;
}

static bool vm_net_mock_file_has_min_size(const char *path, long minSize)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size >= minSize;
}

static bool vm_host_file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

static u32 vm_alloc_host_string(const char *text)
{
    u32 len = (u32)strlen(text) + 1;
    u32 ptr = vm_malloc(len);
    if (ptr)
        uc_mem_write(MTK, ptr, text, len);
    return ptr;
}

static bool vm_net_mock_has_installed_update(void)
{
    return vm_net_mock_file_has_min_size("JHOnlineData/MMORPGTempcbm", 41) &&
           vm_net_mock_file_has_min_size("JHOnlineData/mmorpg_updateversioncbm", 40);
}

static bool vm_net_mock_buffer_has_nonzero(const u8 *data, u32 len)
{
    for (u32 i = 0; i < len; ++i)
    {
        if (data[i] != 0)
            return true;
    }
    return false;
}

static u32 vm_net_mock_signed_byte_sum(const u8 *data, u32 len)
{
    int sum = 0;
    for (u32 i = 0; i < len; ++i)
        sum += (signed char)data[i];
    return (u32)sum;
}

static u32 vm_net_mock_read_download_checksum(void)
{
    u32 value = 0;
    if (Global_R9)
        uc_mem_read(MTK, Global_R9 + 0x9584, &value, sizeof(value));
    return value;
}

static u32 vm_net_mock_load_update_payload(u8 *out, u32 outCap)
{
    static const char *payloadPaths[] = {
        "JHOnlineData/MMORPGTempcbm.mock",
        "JHOnlineData/mmBattleMstarWqvga.cbm",
        "JHOnlineData/mmGameMstarWqvga.cbm",
        "JHOnlineData/mmTitleMstarWqvga.cbm",
    };
    for (u32 i = 0; i < sizeof(payloadPaths) / sizeof(payloadPaths[0]); ++i)
    {
        u32 len = vm_net_mock_load_response_file(payloadPaths[i], out, outCap);
        if (len == 0)
            continue;
        if (len < 1024)
        {
            vm_net_trace("mock_update_payload skip_too_small path=%s len=%u\n", payloadPaths[i], len);
            continue;
        }
        if (!vm_net_mock_buffer_has_nonzero(out, len))
        {
            vm_net_trace("mock_update_payload skip_zero path=%s len=%u\n", payloadPaths[i], len);
            continue;
        }
        vm_net_trace("mock_update_payload path=%s len=%u\n", payloadPaths[i], len);
        return len;
    }
    return 0;
}

static void vm_net_mock_save_tempdata(const u8 *data, u32 len)
{
    if (data == NULL || len == 0)
        return;
    FILE *fp = fopen("JHOnlineData/mmorpgTempdata", "wb");
    if (fp == NULL)
        return;
    fwrite(data, 1, len, fp);
    u8 payload[65536];
    u32 payloadLen = vm_net_mock_load_update_payload(payload, sizeof(payload));
    if (payloadLen > 0)
    {
        fwrite(payload, 1, payloadLen, fp);
        if (payloadLen < sizeof(payload))
        {
            static const u8 zeros[256] = {0};
            u32 remain = (u32)sizeof(payload) - payloadLen;
            while (remain > 0)
            {
                u32 chunk = remain > sizeof(zeros) ? (u32)sizeof(zeros) : remain;
                fwrite(zeros, 1, chunk, fp);
                remain -= chunk;
            }
        }
    }
    fclose(fp);
    vm_net_trace("mock_save_tempdata headerLen=%u payloadLen=%u\n", len, payloadLen);
}

static void vm_net_trace(const char *fmt, ...)
{
    FILE *fp = fopen("net_trace.log", "a");
    if (fp == NULL)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

static void vm_net_trace_bytes(const char *tag, const u8 *data, u32 len)
{
    FILE *fp = fopen("net_trace.log", "a");
    if (fp == NULL)
        return;

    u32 dumpLen = len < 160 ? len : 160;
    fprintf(fp, "%s len=%u dump=%u hex=", tag, len, dumpLen);
    for (u32 i = 0; i < dumpLen; ++i)
        fprintf(fp, "%02x", data[i]);
    fprintf(fp, " ascii=");
    for (u32 i = 0; i < dumpLen; ++i)
    {
        u8 ch = data[i];
        fputc((ch >= 0x20 && ch <= 0x7e) ? ch : '.', fp);
    }
    fputc('\n', fp);
    fclose(fp);
}

static bool vm_net_try_read_ascii(u32 ptr, u8 *out, u32 outCap)
{
    if (ptr == 0 || out == NULL || outCap == 0)
        return false;

    memset(out, 0, outCap);
    for (u32 i = 0; i + 1 < outCap; ++i)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, ptr + i, &ch, 1) != UC_ERR_OK)
            return false;
        if (ch == 0)
        {
            out[i] = 0;
            return i > 0;
        }
        if (ch < 0x20 || ch > 0x7e)
            return false;
        out[i] = ch;
    }
    out[outCap - 1] = 0;
    return false;
}

static void vm_net_trace_mem(const char *tag, u32 ptr, u32 len)
{
    if (ptr == 0 || len == 0)
        return;
    u8 data[160];
    u32 dumpLen = len < sizeof(data) ? len : (u32)sizeof(data);
    if (uc_mem_read(MTK, ptr, data, dumpLen) != UC_ERR_OK)
    {
        vm_net_trace("%s ptr=%08x len=%u read_fail\n", tag, ptr, len);
        return;
    }
    char line[96];
    snprintf(line, sizeof(line), "%s ptr=%08x", tag, ptr);
    vm_net_trace_bytes(line, data, dumpLen);
}

static void vm_trace_lcd_text(const char *apiName, u32 idx, u32 strPtr, int x, int y, u16 color, const u8 *gbkText)
{
#if TRACE_LCD_TEXT
    if (y < 50 || y > 130)
        return;
    u32 lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    gbk_to_utf8((u8 *)gbkText, sprintfBuff, mySizeOf(sprintfBuff));
    int fullWidth = mesureStringWidth((char *)gbkText);
    int curWidth = mesureStringWidthWithGbkWidth((char *)gbkText, getFontWidth());
    int fontApiWidth = (g_currentFontType == 0) ? getFontCellWidth() : getFontWidth();
    vm_net_trace("lcd_text api=%s idx=%u ptr=%08x x=%d y=%d color=%04x fontType=%u fontApiW=%d fullW=%d curW=%d lr=%08x last=%08x text=%s\n",
                 apiName, idx, strPtr, x, y, color, g_currentFontType, fontApiWidth, fullWidth, curWidth, lr, lastAddress, sprintfBuff);
#else
    (void)apiName;
    (void)idx;
    (void)strPtr;
    (void)x;
    (void)y;
    (void)color;
    (void)gbkText;
#endif
}

static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    (void)size;
    (void)user_data;
    if (!g_currentScreenModuleBase)
        return;

    u32 tracePc = (u32)address & ~1u;
    u32 currentR9 = 0;
    uc_reg_read(uc, UC_ARM_REG_R9, &currentR9);
    if (currentR9 != g_currentScreenModuleBase)
    {
        if (tracePc >= 0x050175d0 && tracePc < 0x05017620)
            vm_net_trace("pool_r9_fix pc=%08x r9=%08x -> %08x entry=%08x\n",
                         tracePc, currentR9, g_currentScreenModuleBase, g_currentEmuEntry);
        uc_reg_write(uc, UC_ARM_REG_R9, &g_currentScreenModuleBase);
    }
}

static u32 vm_net_mock_build_response_from_rules(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    static const vm_net_mock_rule rules[] = {
        /* 服务器版本握手。可用 bin/net_mocks/version-handshake.bin 覆盖响应体。 */
        {"version-handshake", "version", "net_mocks/version-handshake.bin", NULL, 0},
    };

    for (u32 i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i)
    {
        const vm_net_mock_rule *rule = &rules[i];
        if (!vm_net_mock_request_contains(request, requestLen, rule->contains))
            continue;
        u32 fileLen = vm_net_mock_load_response_file(rule->responseFile, out, outCap);
        if (fileLen)
        {
            vm_net_trace("mock_rule name=%s source=file path=%s len=%u\n", rule->name, rule->responseFile, fileLen);
            return fileLen;
        }
        if (rule->response && rule->responseLen)
        {
            vm_net_trace("mock_rule name=%s source=builtin len=%u\n", rule->name, rule->responseLen);
            return vm_net_mock_copy_response(rule->response, rule->responseLen, out, outCap);
        }
        vm_net_trace("mock_rule name=%s source=fallback\n", rule->name);
        break;
    }
    return 0;
}

static bool vm_net_mock_put_bytes(u8 *out, u32 outCap, u32 *pos, const void *data, u32 len)
{
    if (*pos + len > outCap)
        return false;
    if (len)
        memcpy(out + *pos, data, len);
    *pos += len;
    return true;
}

static bool vm_net_mock_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value)
{
    return vm_net_mock_put_bytes(out, outCap, pos, &value, 1);
}

static bool vm_net_mock_put_be16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    u8 bytes[2] = {(u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

static bool vm_net_mock_put_be32(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    u8 bytes[4] = {(u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

static bool vm_net_mock_put_name(u8 *out, u32 outCap, u32 *pos, const char *name)
{
    u32 nameLen = (u32)strlen(name) + 1;
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen - 1) &&
           vm_net_mock_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_put_int_field(u8 *out, u32 outCap, u32 *pos, const char *name, u32 value)
{
    return vm_net_mock_put_name(out, outCap, pos, name) &&
           vm_net_mock_put_u8(out, outCap, pos, 0x06) &&
           vm_net_mock_put_be16(out, outCap, pos, 4) &&
           vm_net_mock_put_be32(out, outCap, pos, value);
}

static bool vm_net_mock_put_string_field(u8 *out, u32 outCap, u32 *pos, const char *name, const char *value)
{
    u32 valueLen = (u32)strlen(value);
    return vm_net_mock_put_name(out, outCap, pos, name) &&
           vm_net_mock_put_u8(out, outCap, pos, 0x10) &&
           vm_net_mock_put_be16(out, outCap, pos, (u16)valueLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, value, valueLen);
}

static bool vm_net_mock_put_object_entry(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *value, u16 valueLen)
{
    u32 nameLen = (u32)strlen(name);
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen) &&
           vm_net_mock_put_be16(out, outCap, pos, valueLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, value, valueLen);
}

static bool vm_net_mock_put_object_u8(u8 *out, u32 outCap, u32 *pos, const char *name, u8 value)
{
    u8 encoded[] = {0x00, 0x01, value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_u32(u8 *out, u32 outCap, u32 *pos, const char *name, u32 value)
{
    u8 encoded[] = {0x00, 0x04, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_blob(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *data, u16 dataLen)
{
    u32 nameLen = (u32)strlen(name);
    u16 valueLen = dataLen + 2;
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen) &&
           vm_net_mock_put_be16(out, outCap, pos, valueLen) &&
           vm_net_mock_put_be16(out, outCap, pos, dataLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, data, dataLen);
}

static bool vm_net_mock_put_object_string(u8 *out, u32 outCap, u32 *pos, const char *name, const char *value)
{
    return vm_net_mock_put_object_blob(out, outCap, pos, name, (const u8 *)value, (u16)strlen(value));
}

static bool vm_net_mock_begin_wt_object(u8 *out, u32 outCap, u32 *pos, u8 major, u8 kind, u8 subtype, u32 *objectStart)
{
    if (*pos + 6 > outCap)
        return false;
    if (objectStart)
        *objectStart = *pos;
    out[(*pos)++] = major;
    out[(*pos)++] = kind;
    out[(*pos)++] = subtype;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    return true;
}

static void vm_net_mock_finish_wt_object(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;
    out[objectStart + 4] = (u8)(objectLen >> 8);
    out[objectStart + 5] = (u8)objectLen;
}

static void vm_net_mock_finish_wt_packet(u8 *out, u32 pos, u8 objectCount)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = objectCount;
}

static u8 vm_net_mock_env_u8(const char *name, u8 fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(spec, &end, 0);
    if (end == spec)
        return fallback;
    return (u8)parsed;
}

static u32 vm_net_mock_build_version_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 result = 1;
    if (outCap < pos)
        return 0;

    if (g_netMockUpdateDelivered || vm_net_mock_has_installed_update())
    {
        g_netMockUpdateDelivered = 1;
        result = 0;
    }

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, 5, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    u8 objectCount = 1;
    if (result == 0)
    {
        /* CBE parses subtype 5 in handle_version_update_response and subtype 9
         * in startup_update_net_callback.  Keep both objects: removing subtype 9
         * leaves the startup screen stuck after update_state becomes 2.
         */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, 9, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "id", 0))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "code", 0))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 2;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_version_response result=%u objects=%u delivered=%u\n", result, objectCount, g_netMockUpdateDelivered);
    return pos;
}

static u32 vm_net_mock_build_update_chunk_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    static u8 updateData[131072];
    u32 updateLen = vm_net_mock_load_update_payload(updateData, sizeof(updateData));
    if (updateLen == 0)
        return 0;

    u32 start = 0;
    vm_net_mock_get_object_u32_field(request, requestLen, "start", &start);
    if (start >= updateLen)
        start = 0;

    u32 chunkLen = updateLen - start;
    if (chunkLen > 0x1000)
        chunkLen = 0x1000;
    u32 crc = vm_net_mock_signed_byte_sum(updateData, start + chunkLen);

    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", updateLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", "MMORPGTempcbm"))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", updateData + start, (u16)chunkLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 1);
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 6;
    out[8] = 0;
    vm_net_mock_finish_wt_object(out, 5, pos);
    vm_net_trace("mock_update_chunk_response start=%u chunk=%u totalsize=%u crc=%u\n", start, chunkLen, updateLen, crc);
    return pos;
}

static bool vm_net_mock_seq_put_u32(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 4) &&
           vm_net_mock_put_be32(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 1) &&
           vm_net_mock_put_u8(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_i16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 2) &&
           vm_net_mock_put_be16(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_string(u8 *out, u32 outCap, u32 *pos, const char *value)
{
    u16 len = value ? (u16)(strlen(value) + 1) : 1;
    return vm_net_mock_put_be16(out, outCap, pos, len) &&
           vm_net_mock_put_bytes(out, outCap, pos, value ? value : "", len);
}

static u32 vm_net_mock_build_pos_info(u8 *out, u32 outCap, u16 x, u16 y)
{
    u32 pos = 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, x))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, y))
        return 0;
    return pos;
}

static const char *vm_net_mock_actor_resource_name(u8 actorJob, u8 actorSex)
{
    static const char *actorNames[6] = {
        "h_warrior.actor",
        "hW_warrior.actor",
        "h_assassin.actor",
        "hW_assassin.actor",
        "h_mage.actor",
        "hW_mage.actor",
    };
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    u32 tableIndex = (u32)actorJob * 2u + (u32)actorSex - 1u;
    if (tableIndex >= sizeof(actorNames) / sizeof(actorNames[0]))
        tableIndex = 1;
    return actorNames[tableIndex];
}

static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char *roleName = "Codex";
    const char *secondName = "JHOnline";

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 10001))
        return 0;
    u8 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorJob))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorSex))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 10001))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, secondName))
        return 0;

    const u32 coreFields[] = {
        1,      // role index
        1,      // map or scene id
        120,    // x
        120,    // y
        100,    // hp
        100,    // max hp
        50,     // mp
        50,     // max mp
        0,      // money
        0,      // exp
        1,      // actor slot/state
        1,      // faction/job fallback
    };
    for (u32 i = 0; i < sizeof(coreFields) / sizeof(coreFields[0]); ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, coreFields[i]))
            return 0;
    }

    const u32 extraFields[] = {
        0,      // battle/state value
        0,      // initial scene/state id
    };
    for (u32 i = 0; i < sizeof(extraFields) / sizeof(extraFields[0]); ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, extraFields[i]))
            return 0;
    }
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return 0;

    const u32 detailFields1[] = {
        0,      // combat detail
        0,      // combat detail
    };
    for (u32 i = 0; i < sizeof(detailFields1) / sizeof(detailFields1[0]); ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, detailFields1[i]))
            return 0;
    }

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, "10001"))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, "Codex"))
        return 0;

    const u32 detailFields2[] = {
        0,      // guild/id field
        0,      // guild/id field
        0,      // guild/id field
    };
    for (u32 i = 0; i < sizeof(detailFields2) / sizeof(detailFields2[0]); ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, detailFields2[i]))
            return 0;
    }

    const char *actorResource = vm_net_mock_actor_resource_name(actorJob, actorSex);
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, actorResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, 0))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, actorResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, 0))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, 0))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_login_response(u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    g_netMockEnterGameOffset = 0;
    g_netMockEnterGameChecksum = 0;

    u8 actorInfo[512];
    memset(actorInfo, 0, sizeof(actorInfo));
    u32 actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
    if (actorInfoLen == 0)
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return 0;

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    out[5] = 1;
    u8 topType = 2;
    const char *topTypeSpec = getenv("CBE_LOGIN_TOP_TYPE");
    if (topTypeSpec && topTypeSpec[0])
    {
        unsigned parsed = 0;
        if (sscanf(topTypeSpec, "%u", &parsed) == 1)
            topType = (u8)parsed;
    }
    out[6] = 1;
    out[7] = topType;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    vm_net_trace("mock_login_response actorinfo_len=%u top=1,1,%u job=%u sex=%u actor=%s len=%u\n",
                 actorInfoLen,
                 topType,
                 vm_net_mock_env_u8("CBE_ACTOR_JOB", 1),
                 vm_net_mock_env_u8("CBE_ACTOR_SEX", 0),
                 vm_net_mock_actor_resource_name(vm_net_mock_env_u8("CBE_ACTOR_JOB", 1),
                                                 vm_net_mock_env_u8("CBE_ACTOR_SEX", 0)),
                 pos);
    return pos;
}

static u32 vm_net_mock_build_enter_game_response(u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    u8 payload[65535];
    const char *payloadName = "mmGameMstarWqvga.cbm";
    u32 payloadLen = vm_net_mock_load_response_file("JHOnlineData/mmGameMstarWqvga.cbm", payload, sizeof(payload));
    if (payloadLen == 0)
    {
        payloadName = "MMORPGTempcbm";
        payloadLen = vm_net_mock_load_response_file("JHOnlineData/MMORPGTempcbm", payload, sizeof(payload));
    }
    if (payloadLen == 0)
        return 0;

    if (g_netMockEnterGameOffset >= payloadLen)
    {
        g_netMockEnterGameOffset = 0;
        g_netMockEnterGameChecksum = 0;
    }

    u32 chunkLen = payloadLen - g_netMockEnterGameOffset;
    if (chunkLen > 1024)
        chunkLen = 1024;
    u32 chunkSum = vm_net_mock_signed_byte_sum(payload + g_netMockEnterGameOffset, chunkLen);
    u32 crc = g_netMockEnterGameChecksum + chunkSum;

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", payloadLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", (const u8 *)"\x00\x02\x00\x01", 4))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", payload + g_netMockEnterGameOffset, (u16)chunkLen))
        return 0;

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 7;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    vm_net_trace("mock_enter_game_response resource name=%s payloadLen=%u offset=%u chunkLen=%u checksumBefore=%08x chunkSum=%08x crc=%08x len=%u\n",
                 payloadName, payloadLen, g_netMockEnterGameOffset, chunkLen, g_netMockEnterGameChecksum, chunkSum, crc, pos);
    g_netMockEnterGameChecksum = crc;
    g_netMockEnterGameOffset += chunkLen;
    return pos;
}

static u32 vm_net_mock_build_game_type_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap, u8 requestType)
{
    u32 pos = 11;
    if (outCap < pos || request == NULL || requestLen < 8)
        return 0;
    u8 responseSub = request[7];
    if (requestType == 1)
        responseSub = 0x1a;
    else if (requestType == 2 || requestType == 3)
        responseSub = vm_net_mock_env_u8(requestType == 2 ? "CBE_GAME_TYPE2_SUB" : "CBE_GAME_TYPE3_SUB", 0x1a);

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", requestType))
        return 0;
    if (requestType == 1)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "npcnum", 0))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", "Codex"))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "money", 0))
            return 0;
    }
    else if (requestType == 2 || requestType == 3)
    {
        static const u8 emptyInfo[] = {0};
        u8 posInfo[8];
        u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), 120, 120);
        if (posInfoLen == 0)
            return 0;
        if (!vm_net_mock_put_object_blob(out, outCap, &pos, "info", emptyInfo, 0))
            return 0;
        if (!vm_net_mock_put_object_blob(out, outCap, &pos, "giftinfo", emptyInfo, 0))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "glamour", 0))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "scene", "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31"))
            return 0;
        if (!vm_net_mock_put_object_entry(out, outCap, &pos, "posinfo", posInfo, (u16)posInfoLen))
            return 0;
    }

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = request[4];
    out[5] = request[5];
    out[6] = 0x0a;
    out[7] = responseSub;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    vm_net_trace("mock_game_type_response type=%u responseSub=%u header=%02x,%02x,%02x,%02x len=%u\n",
                 requestType, responseSub, out[4], out[5], out[6], out[7], pos);
    return pos;
}

static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen == 0 || outCap == 0)
        return 0;

    /* First principle for protocol research: only emulate server/API behavior.
     * Do not advance CBE state here by forcing return values or writing globals.
     */
    u32 hookedLen = vm_net_mock_build_response_from_rules(request, requestLen, out, outCap);
    if (hookedLen)
        return hookedLen;

    if (vm_net_mock_request_contains(request, requestLen, "coreVer") &&
        vm_net_mock_request_contains(request, requestLen, "appVer") &&
        (vm_net_mock_request_contains(request, requestLen, "username") ||
         vm_net_mock_request_contains(request, requestLen, "userName")) &&
        vm_net_mock_request_contains(request, requestLen, "password") &&
        vm_net_mock_request_contains(request, requestLen, "imsi"))
    {
        vm_net_ensure_business_callback("login-request");
        hookedLen = vm_net_mock_load_response_file("net_mocks/login.bin", out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=file-login len=%u\n", hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_login_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-login len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "start") && vm_net_mock_request_contains(request, requestLen, "id"))
    {
        hookedLen = vm_net_mock_build_update_chunk_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-update-chunk len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    u8 gameRequestType = 0;
    if (vm_net_mock_get_object_u8_field(request, requestLen, "type", &gameRequestType) &&
        (gameRequestType == 1 || gameRequestType == 2 || gameRequestType == 3))
    {
        hookedLen = vm_net_mock_build_game_type_response(request, requestLen, out, outCap, gameRequestType);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-game-type type=%u len=%u\n", gameRequestType, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "version"))
    {
        hookedLen = vm_net_mock_build_version_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-version len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_load_response_file("net_mocks/default.bin", out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=file len=%u\n", hookedLen);
        return hookedLen;
    }

    if (requestLen >= 2 && request[0] == 'W' && request[1] == 'T')
    {
        vm_net_trace("mock_default source=echo-wt len=%u\n", requestLen);
        return vm_net_mock_copy_response(request, requestLen, out, outCap);
    }

    static const u8 ok[] = {'O', 'K', 0};
    if (outCap < sizeof(ok))
        return 0;
    memcpy(out, ok, sizeof(ok));
    return sizeof(ok);
}

static u32 vm_net_mock_sync_response_to_vm(void)
{
    if (g_netMockResponseLen == 0)
        return 0;

    g_netMockResponseVmPtr = vm_malloc(g_netMockResponseLen);
    if (g_netMockResponseVmPtr == 0)
        return 0;
    uc_mem_write(MTK, g_netMockResponseVmPtr, g_netMockResponse, g_netMockResponseLen);
    return g_netMockResponseVmPtr;
}

static void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
{
    if (dataPtr == 0 || dataLen == 0)
        return;

    u8 request[512];
    u32 readLen = dataLen < sizeof(request) ? dataLen : sizeof(request);
    uc_mem_read(MTK, dataPtr, request, readLen);
    vm_net_trace("send connect=%u dataPtr=%08x dataLen=%u readLen=%u\n", connectId, dataPtr, dataLen, readLen);
    vm_net_trace_bytes("send_payload", request, readLen);
    bool closeAfterData = vm_net_mock_request_contains(request, readLen, "version") &&
                          !vm_net_mock_request_contains(request, readLen, "start") &&
                          vm_net_mock_has_installed_update();

    g_netMockResponseLen = vm_net_mock_build_response(request, readLen, g_netMockResponse, sizeof(g_netMockResponse));
    g_netMockResponseOffset = 0;
    g_netUpLinkData += dataLen;
    if (g_netMockResponseLen == 0)
        return;
    u32 responsePtr = vm_net_mock_sync_response_to_vm();
    if (responsePtr == 0)
        return;
    g_netDownLinkData += g_netMockResponseLen;
    vm_net_trace("mock_response ptr=%08x len=%u\n", responsePtr, g_netMockResponseLen);
    vm_net_trace_bytes("mock_response_payload", g_netMockResponse, g_netMockResponseLen);

    vm_net_channel *channel = scheduler_find_net_channel(connectId);
    if (channel && channel->callback)
    {
        vm_net_trace("queue_data_event connect=%u cb=%08x ctx=%08x event=7 len=%u\n", connectId, channel->callback, channel->context, g_netMockResponseLen);
        scheduler_queue_net_event(7, responsePtr, g_netMockResponseLen, g_netMockResponseLen, channel->callback, channel->context);
        if (closeAfterData)
        {
            vm_net_trace("queue_close_event connect=%u cb=%08x ctx=%08x event=9\n", connectId, channel->callback, channel->context);
            scheduler_queue_net_event(9, 0, 0, 0, channel->callback, channel->context);
        }
    }
    else
    {
        vm_net_trace("queue_data_event_miss connect=%u\n", connectId);
    }
}

static u32 vm_net_mock_read_data(u32 dst, u32 dstLen)
{
    if (dst == 0 || dstLen == 0 || g_netMockResponseOffset >= g_netMockResponseLen)
    {
        vm_net_trace("getdata dst=%08x dstLen=%u copied=0 offset=%u len=%u\n", dst, dstLen, g_netMockResponseOffset, g_netMockResponseLen);
        return vm_set_call_result(0);
    }

    u32 remain = g_netMockResponseLen - g_netMockResponseOffset;
    u32 copyLen = dstLen < remain ? dstLen : remain;
    uc_mem_write(MTK, dst, g_netMockResponse + g_netMockResponseOffset, copyLen);
    g_netMockResponseOffset += copyLen;
    vm_net_trace("getdata dst=%08x dstLen=%u copied=%u offset=%u len=%u\n", dst, dstLen, copyLen, g_netMockResponseOffset, g_netMockResponseLen);
    return vm_set_call_result(copyLen);
}

static uc_err scheduler_dispatch_input_event(vm_event *evt);

static uc_err scheduler_dispatch_tscreen_event(u32 tScreenEventEntry, u32 screenPtr)
{
    simulateKey = 0;
    simulatePress = 0;
    simulateTouchDown = 0;
    simulateTouchUp = 0;
    simulateTouchDrag = 0;

    vm_event *evt = DequeueVMEvent();
    if (evt == NULL)
        return UC_ERR_OK;

    if (evt->event == VM_EVENT_INPUT_CHAR || evt->event == VM_EVENT_INPUT_BACKSPACE || evt->event == VM_EVENT_INPUT_DONE)
        return scheduler_dispatch_input_event(evt);

    if (evt->event == VM_EVENT_KEYBOARD)
    {
        simulateKey = evt->r0;
        simulatePress = evt->r1;
        if (tScreenEventEntry == 0)
            return UC_ERR_OK;

        u32 keyMask = 0;
        if (evt->r0 < 31)
            keyMask = 1u << evt->r0;
        u32 keyPtr = vm_malloc_var();
        vm_set_var(keyPtr, keyMask);
        uc_err err = vm_call4(tScreenEventEntry, screenPtr, evt->r1 ? 0 : 1, keyPtr, 0);
        vm_free_var(keyPtr);
        return err;
    }

    if (evt->event == VM_EVENT_TOUCHSCREEN)
    {
        simulateTouchPress = evt->r0 != MR_MOUSE_UP;
        simulateTouchDown = evt->r0 == MR_MOUSE_DOWN;
        simulateTouchUp = evt->r0 == MR_MOUSE_UP;
        simulateTouchDrag = evt->r0 == MR_MOUSE_MOVE;
        simulateTouchX = evt->r1 & 0xffff;
        simulateTouchY = (evt->r1 >> 16) & 0xffff;
        if (tScreenEventEntry == 0)
            return UC_ERR_OK;

        u32 touchPtr = vm_malloc_var();
        vm_set_var(touchPtr, evt->r1);
        uc_err err = vm_call4(tScreenEventEntry, screenPtr, evt->r0 == MR_MOUSE_UP ? 4 : 3, touchPtr, 0);
        vm_free_var(touchPtr);
        return err;
    }

    return UC_ERR_OK;
}

static u32 vm_input_read_u16(u32 addr)
{
    u16 value = 0;
    if (addr)
        uc_mem_read(MTK, addr, &value, sizeof(value));
    return value;
}

static void vm_input_write_u16(u32 addr, u16 value)
{
    if (addr)
        uc_mem_write(MTK, addr, &value, sizeof(value));
}

static u32 vm_input_wcslen_limit(u32 addr, u32 maxLen);

static void vm_lcd_fill_rect_local(int x, int y, int w, int h, u16 color)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > LCD_WIDTH)
        w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT)
        h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = 0; row < h; ++row)
    {
        u32 off = (y + row) * LCD_WIDTH + x;
        for (int col = 0; col < w; ++col)
            ((u16 *)Lcd_Cache_Buffer)[off + col] = color;
    }
}

static void vm_lcd_sync_cache_rect_to_vm(int x, int y, int w, int h)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > LCD_WIDTH)
        w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT)
        h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = 0; row < h; ++row)
    {
        u32 off = (y + row) * LCD_WIDTH + x;
        uc_mem_write(MTK, VM_screenImage_ADDRESS + off * 2, Lcd_Cache_Buffer + off * 2, w * 2);
    }
}

static void vm_lcd_sync_vm_rect_to_cache(int x, int y, int w, int h)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > LCD_WIDTH)
        w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT)
        h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = 0; row < h; ++row)
    {
        u32 off = (y + row) * LCD_WIDTH + x;
        uc_mem_read(MTK, VM_screenImage_ADDRESS + off * 2, Lcd_Cache_Buffer + off * 2, w * 2);
    }
}

static int vm_lcd_current_gbk_width(void)
{
    return getFontWidth();
}

static int vm_lcd_measure_current_string(const u8 *gbkText)
{
    return mesureStringWidthWithGbkWidth((char *)gbkText, vm_lcd_current_gbk_width());
}

static void vm_lcd_draw_current_string(u8 *gbkText, int x, int y, u16 color)
{
    int w = vm_lcd_measure_current_string(gbkText);
    int h = getFontHeight();
    vm_lcd_sync_vm_rect_to_cache(x, y, w, h);
    drawFontStringWithGbkWidth(gbkText, x, y, color, vm_lcd_current_gbk_width());
}

static void vm_lcd_sync_string_to_vm(const u8 *gbkText, int x, int y)
{
    int w = vm_lcd_measure_current_string(gbkText);
    int h = getFontHeight();
    vm_lcd_sync_cache_rect_to_vm(x, y, w, h);
}

static void vm_lcd_draw_rect_local(int x, int y, int w, int h, u16 color)
{
    vm_lcd_fill_rect_local(x, y, w, 1, color);
    vm_lcd_fill_rect_local(x, y + h - 1, w, 1, color);
    vm_lcd_fill_rect_local(x, y, 1, h, color);
    vm_lcd_fill_rect_local(x + w - 1, y, 1, h, color);
}

static void vm_input_draw_overlay(void)
{
    if (!g_vmInputOpen || !g_vmInputBuffer)
        return;

    u32 len = vm_input_wcslen_limit(g_vmInputBuffer, g_vmInputMaxLen ? g_vmInputMaxLen : 0x100);
    u32 srcBytes = (len + 1) * 2;
    if (srcBytes > mySizeOf(cbeTextString))
        srcBytes = mySizeOf(cbeTextString);
    uc_mem_read(MTK, g_vmInputBuffer, cbeTextString, srcBytes);

    memset(sprintfBuff, 0, mySizeOf(sprintfBuff));
    if (g_vmInputPassword)
    {
        u32 maskLen = len < 30 ? len : 30;
        memset(sprintfBuff, '*', maskLen);
        sprintfBuff[maskLen] = 0;
    }
    else
    {
        ucs2_to_gbk(cbeTextString, srcBytes, sprintfBuff, mySizeOf(sprintfBuff));
    }

    int x = g_vmInputOverlayX;
    int y = g_vmInputOverlayY;
    int w = g_vmInputOverlayW;
    int h = g_vmInputOverlayH;
    vm_lcd_fill_rect_local(x, y, w, h, 0x0148);
    vm_lcd_draw_rect_local(x, y, w, h, 0x9fe6);
    vm_lcd_draw_rect_local(x + 1, y + 1, w - 2, h - 2, 0x2b6d);
    u8 hintGbk[64] = {0};
    utf8_to_gbk((u8 *)"已进入输入状态", hintGbk, sizeof(hintGbk));
    drawFontString(hintGbk, x + 4, y - 18, 0xffe0);
    drawFontString(sprintfBuff, x + 5, y + 4, 0xffff);
    if ((clock() / (CLOCKS_PER_SEC / 2)) % 2 == 0)
    {
        int caretX = x + 6 + mesureStringWidth((char *)sprintfBuff);
        if (caretX > x + w - 8)
            caretX = x + w - 8;
        vm_lcd_fill_rect_local(caretX, y + 4, 1, h - 8, 0xffff);
    }
}

static void vm_lcd_update_with_input_overlay(void)
{
    uc_mem_read(MTK, VM_screenImage_ADDRESS, Lcd_Cache_Buffer, LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
    vm_input_draw_overlay();
    UpdateLcd();
}

static u32 vm_input_wcslen_limit(u32 addr, u32 maxLen)
{
    if (!addr || maxLen == 0)
        return 0;

    for (u32 i = 0; i < maxLen; ++i)
    {
        if (vm_input_read_u16(addr + i * 2) == 0)
            return i;
    }
    return maxLen;
}

static void vm_input_append_char(u32 ch)
{
    if (!g_vmInputOpen || !g_vmInputBuffer || g_vmInputMaxLen <= 1)
        return;

    if (ch < 0x20 || ch > 0x7e)
        return;

    u32 len = vm_input_wcslen_limit(g_vmInputBuffer, g_vmInputMaxLen);
    if (len + 1 >= g_vmInputMaxLen)
        return;

    vm_input_write_u16(g_vmInputBuffer + len * 2, (u16)ch);
    vm_input_write_u16(g_vmInputBuffer + (len + 1) * 2, 0);
}

static void vm_input_backspace(void)
{
    if (!g_vmInputOpen || !g_vmInputBuffer || g_vmInputMaxLen == 0)
        return;

    u32 len = vm_input_wcslen_limit(g_vmInputBuffer, g_vmInputMaxLen);
    if (len == 0)
        return;

    vm_input_write_u16(g_vmInputBuffer + (len - 1) * 2, 0);
}

static uc_err vm_input_finish(u32 result)
{
    if (!g_vmInputOpen)
        return UC_ERR_OK;

    u32 callback = g_vmInputCallback;
    u32 buffer = g_vmInputBuffer;
    g_vmInputOpen = 0;
    g_vmInputCallback = 0;
    g_vmInputBuffer = 0;
    g_vmInputMaxLen = 0;
    g_vmInputInputType = 0;
    g_vmInputPassword = 0;

    if (!callback)
        return UC_ERR_OK;

    return vm_call4_preserve_regs(callback, result ? 1 : 0, buffer, callback, 0);
}

static uc_err scheduler_dispatch_input_event(vm_event *evt)
{
    if (evt->event == VM_EVENT_INPUT_CHAR)
    {
        vm_input_append_char(evt->r0);
        return UC_ERR_OK;
    }
    if (evt->event == VM_EVENT_INPUT_BACKSPACE)
    {
        vm_input_backspace();
        return UC_ERR_OK;
    }
    if (evt->event == VM_EVENT_INPUT_DONE)
        return vm_input_finish(evt->r0);

    return UC_ERR_OK;
}

static void vm_input_open(u32 callback, u32 param, int password)
{
    if (!callback || !param)
    {
        printf("[vmInput] invalid callback=%08x param=%08x\n", callback, param);
        assert(0);
    }

    u32 buffer = 0;
    u32 maxLen = 0;
    u32 prompt = 0;
    u32 inputType = 0;
    uc_mem_read(MTK, param, &buffer, 4);
    uc_mem_read(MTK, param + 4, &maxLen, 4);
    uc_mem_read(MTK, param + 8, &prompt, 4);
    uc_mem_read(MTK, param + 12, &inputType, 4);

    if (!buffer || maxLen == 0)
    {
        printf("[vmInput] invalid buffer=%08x maxLen=%u param=%08x\n", buffer, maxLen, param);
        assert(0);
    }

    g_vmInputOpen = 1;
    g_vmInputPassword = password ? 1 : 0;
    g_vmInputCallback = callback;
    g_vmInputBuffer = buffer;
    g_vmInputMaxLen = maxLen & 0xffff;
    if (g_vmInputMaxLen == 0)
        g_vmInputMaxLen = maxLen;
    g_vmInputInputType = inputType & 0xff;
    g_vmInputOverlayX = 12;
    g_vmInputOverlayY = password ? 372 : 344;
    g_vmInputOverlayW = 216;
    g_vmInputOverlayH = 22;
    vm_set_call_result(1);
}

static uc_err scheduler_tick(void)
{
    g_schedulerTick++;
    currentTime = clock();
    uc_err err = scheduler_dispatch_timers();
    if (err != UC_ERR_OK)
        return err;
    return scheduler_dispatch_net_tasks();
}

/**
 * @brief 按键事件
 * @param type 4=按下 5=松开
 * @param key 按键值
 */
void keyEvent(int type, int key)
{
    // printf("keyboard(%x,type=%d)\n", key, type);
    int skey = -1;
    // F5导出Cpu信息
    if (key == 0x4000003e && type == 4)
    {
        dumpCpuInfo();
    }
    if (key >= 0x30 && key <= 0x39)
    { // 数字键盘1-9
        skey = key - 0x30;
    }
    else if (key == 0x77) // w
    {
        skey = 10; // 上
    }
    else if (key == 0x73) // s
    {
        skey = 11; // 下
    }
    else if (key == 0x61) // a
    {
        skey = 12; // 左
    }
    else if (key == 0x64) // d
    {
        skey = 13; // 右
    }

    else if (key == 0x66) // f
    {
        skey = 14; // OK
    }
    else if (key == 0x71) // q
    {
        skey = 15; // 左软
    }
    else if (key == 0x65) // e
    {
        skey = 16; // 右软
    }
    else if (key == 0x7a) // z
    {
        skey = 17; // 拨号
    }
    else if (key == 0x63) // c
    {
        skey = 18; // 挂机
    }

    else if (key == 0x6e) // n
    {
        skey = 19; // *
    }
    else if (key == 0x6d) // m
    {
        skey = 20; // #
    }
    int isPress = type == 4 ? 1 : 0;
    if (skey != -1)
    {
        EnqueueVMEvent(VM_EVENT_KEYBOARD, skey, isPress);
    }
}

// 1按下3弹起
void mouseEvent(int type, int x, int y)
{
    if (x < 0)
        x = 0;
    else if (x > 239)
        x = 239;
    if (y < 0)
        y = 0;
    else if (y > 399)
        y = 399;

    EnqueueVMEvent(VM_EVENT_TOUCHSCREEN, type, (y << 16) | x);
}

static void scheduler_configure_auto_touch(void)
{
    if (g_autoTouchConfigured)
        return;
    g_autoTouchConfigured = 1;

    const char *spec = getenv("CBE_AUTO_TOUCH");
    if (spec == NULL || spec[0] == 0)
        return;

    unsigned tick = 300;
    int x = LCD_WIDTH / 2;
    int y = LCD_HEIGHT - 45;
    sscanf(spec, "%u,%d,%d", &tick, &x, &y);
    g_autoTouchTick = tick;
    g_autoTouchX = x;
    g_autoTouchY = y;
}

static void scheduler_fire_auto_touch(void)
{
    scheduler_configure_auto_touch();
    if (g_autoTouchTick == 0 || g_autoTouchFired || g_schedulerTick < g_autoTouchTick)
        return;
    g_autoTouchFired = 1;
    vm_net_trace("auto_touch tick=%u x=%d y=%d\n", g_schedulerTick, g_autoTouchX, g_autoTouchY);
    EnqueueVMEvent(VM_EVENT_TOUCHSCREEN, MR_MOUSE_DOWN, (g_autoTouchY << 16) | (g_autoTouchX & 0xffff));
    EnqueueVMEvent(VM_EVENT_TOUCHSCREEN, MR_MOUSE_UP, (g_autoTouchY << 16) | (g_autoTouchX & 0xffff));
}

void loop()
{
    void *thread_ret;
    SDL_Event ev;
    bool isLoop = true;
    while (isLoop)
    {
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
            {
                isLoop = false;
                break;
            }
            switch (ev.type)
            {
            case SDL_KEYDOWN:
                if (g_vmInputOpen)
                {
                    SDL_Keycode key = ev.key.keysym.sym;
                    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
                        EnqueueVMEvent(VM_EVENT_INPUT_DONE, 0, 0);
                    else if (key == SDLK_ESCAPE)
                        EnqueueVMEvent(VM_EVENT_INPUT_DONE, 1, 0);
                    else if (key == SDLK_BACKSPACE)
                        EnqueueVMEvent(VM_EVENT_INPUT_BACKSPACE, 0, 0);
                    else if (key >= 0x20 && key <= 0x7e)
                        EnqueueVMEvent(VM_EVENT_INPUT_CHAR, key, 0);
                    break;
                }
                if (isKeyDown == SDLK_UNKNOWN)
                {
                    isKeyDown = ev.key.keysym.sym;
                    keyEvent(MR_KEY_PRESS, ev.key.keysym.sym);
                }
                break;
            case SDL_KEYUP:
                if (g_vmInputOpen)
                    break;
                if (isKeyDown == ev.key.keysym.sym)
                {
                    isKeyDown = SDLK_UNKNOWN;
                    keyEvent(MR_KEY_RELEASE, ev.key.keysym.sym);
                }
                break;
            case SDL_MOUSEMOTION:
                if (isMouseDown)
                {
                    mouseEvent(MR_MOUSE_MOVE, ev.motion.x, ev.motion.y);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                isMouseDown = true;
                mouseEvent(MR_MOUSE_DOWN, ev.button.x, ev.button.y);
                break;
            case SDL_MOUSEBUTTONUP:
                isMouseDown = false;
                mouseEvent(MR_MOUSE_UP, ev.button.x, ev.button.y);
                break;
            }
        }
    }
    pthread_join(&EmuThread, &thread_ret);
    pthread_join(&MainUpdareThread, &thread_ret);
    if (SD_File_Handle != NULL)
        fclose(SD_File_Handle);
    SD_File_Handle = NULL;
}

void dumpCpuInfo()
{
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 r8 = 0;
    u32 r9 = 0;
    u32 msp = 0;
    u32 pc = 0;
    u32 lr = 0;
    u32 cpsr = 0;
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_SP, &msp);
    uc_reg_read(MTK, UC_ARM_REG_CPSR, &cpsr);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_R8, &r8);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    printf("r0:%x r1:%x r2:%x r3:%x r4:%x r5:%x r6:%x r7:%x r8:%x r9:%x\n", r0, r1, r2, r3, r4, r5, r6, r7, r8, r9);
    printf("msp:%x cpsr:%x(thumb:%x)(mode:%x) lr:%x pc:%x lastPc:%x irq_c(%x)\n", msp, cpsr, (cpsr & 0x20) > 0, cpsr & 0x1f, lr, pc, lastAddress, irq_nested_count);
    printf("------------\n");
}

u8 *SimpleRamMatch(u8 *start, u8 *end, u8 *matchStart, int matchLen)
{
    u8 ii;
    while (start < end)
    {
        for (ii = 0; ii < matchLen; ii++)
        {
            if (*(start + ii) != *(matchStart + ii))
            {
                break;
            }
        }
        if (ii == matchLen)
            break;
        start++;
    }
    if (ii == matchLen)
        return start;
    else
        return NULL;
}

#define LOAD_CBE_PATH "CBE/愤怒的小鸟.CBE"
#define LOAD_CBE_PATH "CBE/众神之战.CBE"
#define LOAD_CBE_PATH "CBE/钻石迷情3.CBE"
#define LOAD_CBE_PATH "CBE/枪之荣誉.CBE"
#define LOAD_CBE_PATH "CBE/僵尸先生.CBE"
#define LOAD_CBE_PATH "CBE/捕鱼猎人.CBE"
#define LOAD_CBE_PATH "CBE/战争机器.CBE"
#define LOAD_CBE_PATH "CBE/魔塔.CBE"
#define LOAD_CBE_PATH "CBE/孤岛.CBE"
#define LOAD_CBE_PATH "CBE/恶魔城.CBE"
#define LOAD_CBE_PATH "CBE/鬼吹灯.CBE"
#define LOAD_CBE_PATH "CBE/皇牌空战.CBE"
#define LOAD_CBE_PATH "CBE/涂鸦跳跃.CBE"
#define LOAD_CBE_PATH "CBE/江湖OL.CBE"

static void vm_persist_ensure_dir(void)
{
#ifdef _WIN32
    _mkdir("nvram");
#else
    mkdir("nvram", 0755);
#endif
}

static void vm_persist_sanitize_name(const char *src, char *dst, size_t dstSize)
{
    size_t pos = 0;
    if (dstSize == 0)
        return;

    for (size_t i = 0; src && src[i] && pos + 1 < dstSize; ++i)
    {
        char ch = src[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')
            dst[pos++] = ch;
        else
            dst[pos++] = '_';
    }
    dst[pos] = 0;
}

static void vm_persist_build_path(char *path, size_t pathSize, const char *kind, u32 slot)
{
    char appName[96];
    vm_persist_sanitize_name(LOAD_CBE_PATH, appName, sizeof(appName));
    snprintf(path, pathSize, "nvram/%s_%s_%08x.bin", appName, kind, slot);
}

static u32 vm_persist_read_file(const char *path, u8 *buffer, u32 size)
{
    if (path == NULL || buffer == NULL || size == 0)
        return 0;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    size_t readLen = fread(buffer, 1, size, fp);
    fclose(fp);
    return (u32)readLen;
}

static u32 vm_persist_write_file(const char *path, const u8 *buffer, u32 size)
{
    if (path == NULL || buffer == NULL || size == 0)
        return 0;

    vm_persist_ensure_dir();
    FILE *fp = fopen(path, "wb");
    if (fp == NULL)
        return 0;

    size_t writeLen = fwrite(buffer, 1, size, fp);
    fclose(fp);
    return (u32)writeLen;
}

static void vm_storage_trace(const char *fmt, ...)
{
    FILE *fp = fopen("storage_trace.log", "a");
    if (fp == NULL)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

static u32 vm_nv_read(u32 reqPtr)
{
    if (reqPtr == 0)
        return vm_set_call_result(0);

    u32 slot = vm_get_var(reqPtr);
    u32 dst = vm_get_var(reqPtr + 4);
    u32 size = vm_get_var_short(reqPtr + 8);
    if (dst == 0 || size == 0)
        return vm_set_call_result(0);

    char path[160];
    u8 buffer[1024];
    u32 readLen = size < sizeof(buffer) ? size : sizeof(buffer);
    vm_persist_build_path(path, sizeof(path), "nv", slot);
    readLen = vm_persist_read_file(path, buffer, readLen);
    if (readLen)
        uc_mem_write(MTK, dst, buffer, readLen);
    if (readLen < size)
    {
        u32 zeroOffset = readLen;
        while (zeroOffset < size)
        {
            u32 zeroLen = SDL_min(size - zeroOffset, (u32)sizeof(emptyBuff));
            uc_mem_write(MTK, dst + zeroOffset, emptyBuff, zeroLen);
            zeroOffset += zeroLen;
        }
    }

    DEBUG_PRINT("[call]vmDlFuncNvRead slot=%x size=%u read=%u\n", slot, size, readLen);
    vm_storage_trace("nv_read req=%08x slot=%08x dst=%08x size=%u read=%u path=%s\n", reqPtr, slot, dst, size, readLen, path);
    return vm_set_call_result(0);
}

static u32 vm_nv_write(u32 reqPtr)
{
    if (reqPtr == 0)
        return vm_set_call_result(0);

    u32 slot = vm_get_var(reqPtr);
    u32 src = vm_get_var(reqPtr + 4);
    u32 size = vm_get_var_short(reqPtr + 8);
    if (src == 0 || size == 0)
        return vm_set_call_result(0);

    char path[160];
    u8 buffer[1024];
    u32 writeLen = size < sizeof(buffer) ? size : sizeof(buffer);
    uc_mem_read(MTK, src, buffer, writeLen);
    vm_persist_build_path(path, sizeof(path), "nv", slot);
    u32 savedLen = vm_persist_write_file(path, buffer, writeLen);

    DEBUG_PRINT("[call]vmDlFuncNvWrite slot=%x size=%u saved=%u\n", slot, size, savedLen);
    vm_storage_trace("nv_write req=%08x slot=%08x src=%08x size=%u saved=%u path=%s\n", reqPtr, slot, src, size, savedLen, path);
    return vm_set_call_result(0);
}

static u32 vm_sys_set_setting_profile(u32 profile)
{
    u8 value[4];
    memcpy(value, &profile, sizeof(profile));
    char path[160];
    vm_persist_build_path(path, sizeof(path), "sys_profile", 0);
    u32 savedLen = vm_persist_write_file(path, value, sizeof(value));
    vm_storage_trace("sys_set_profile profile=%u saved=%u path=%s\n", profile, savedLen, path);
    return vm_set_call_result(0);
}

static u32 vm_sys_get_setting_profile(void)
{
    u32 profile = 0;
    char path[160];
    vm_persist_build_path(path, sizeof(path), "sys_profile", 0);
    u32 readLen = vm_persist_read_file(path, (u8 *)&profile, sizeof(profile));
    vm_storage_trace("sys_get_profile profile=%u read=%u path=%s\n", profile, readLen, path);
    return vm_set_call_result(profile);
}

static u32 vm_sys_get_setting_profile_name(u32 profile, u32 dst, u32 dstLen)
{
    static const char *names[] = {"Normal", "Silent", "Meeting", "Outdoor"};
    const char *name = names[0];
    if (profile < sizeof(names) / sizeof(names[0]))
        name = names[profile];
    if (dst && dstLen)
    {
        u32 len = (u32)strlen(name) + 1;
        if (len > dstLen)
            len = dstLen;
        uc_mem_write(MTK, dst, name, len);
    }
    vm_storage_trace("sys_get_profile_name profile=%u dst=%08x len=%u name=%s\n", profile, dst, dstLen, name);
    return vm_set_call_result(0);
}

void RunArmProgram(void *param)
{
    uc_err p;
    u32 startAddr = (u32)param;
#ifdef GDB_SERVER_SUPPORT
    gdbTarget.running = 1;
    gdbTarget.breakpoints[gdbTarget.num_breakpoints++] = startAddr;
    readAllCpuRegFunc = ReadRegsToGdb;
    gdb_readMemFunc = readMemoryToGdb;
#endif
    // 准备工作
    // 写入屏幕缓存数据
    changeTmp1 = VM_screenImage_ADDRESS;
    uc_mem_write(MTK, VM_screenImageStruct_ADDRESS, &changeTmp1, 4);
    changeTmp1 = LCD_WIDTH;
    uc_mem_write(MTK, VM_screenImageStruct_ADDRESS + 2, &changeTmp1, 2);
    changeTmp1 = LCD_HEIGHT;
    uc_mem_write(MTK, VM_screenImageStruct_ADDRESS + 4, &changeTmp1, 2);
    changeTmp1 = 6;
    uc_mem_write(MTK, VM_screenImageStruct_ADDRESS + 5, &changeTmp1, 1);
    // DF_DataPackage_SetFullPaths()
    // 当前运行的文件名
    char nameBuff[64] = LOAD_CBE_PATH;
    utf8_to_gbk(nameBuff, cbeTextString, mySizeOf(cbeTextString));
    uc_mem_write(MTK, VM_DF_DataPackage_FilePath_ADDRESS, cbeTextString, 64);
    // DF_DataPackage_SetFileLens();
    uc_mem_write(MTK, VM_DF_DataPackage_In_File_Length_ADDRESS, &g_cbeInfo.DF_DataPacakge_Size, 4);
    // DF_DataPackage_SetFileOffset()
    uc_mem_write(MTK, VM_DF_DataPackage_In_File_Offset_ADDRESS, &g_cbeInfo.DF_Data_Pacakage_Offset, 4);
    changeTmp1 = 1;
    uc_mem_write(MTK, VM_DF_DataPackage_LoadType_ADDRESS, &changeTmp1, 1);
    // 第一次入口初始化
    changeTmp1 = VM_Manager_Table_ADDRESS;
    uc_reg_write(MTK, UC_ARM_REG_R0, &changeTmp1); // 传入Manager函数表指针地址

    u32 exitAddr = PROGRAM_EXIT_ADDR;
    u32 thumbExitAddr = PROGRAM_EXIT_ADDR | 1;
    uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
    p = vm_emu_start(startAddr + 1, exitAddr);        // thumb模式

    // 第二次初始化
    if (p == UC_ERR_OK)
    {
        uc_mem_read(MTK, VM_Manager_Table_ADDRESS, &startAddr, 4);
        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
        p = vm_emu_start(startAddr, exitAddr);
    }

    if (p == UC_ERR_OK)
    {
        if (screenStructChange != 1)
        {
            if (vmAddedScreen == 0)
            {
                printf("第二次初始化未设置Screen，停止运行以避免执行未初始化入口\n");
                assert(0);
            }
            u32 tScreenRenderEntry = 0;
            u32 tScreenEventEntry = 0;
            u32 tScreenInitEntry = 0;
            u32 tScreenResourceLoadEntry = 0;
            u32 tScreenInitedPtr = 0;
            while (p == UC_ERR_OK && screenStructChange != 1)
            {
                p = scheduler_tick();
                if (p != UC_ERR_OK)
                    break;
                scheduler_fire_auto_touch();
                if (tScreenInitedPtr != vmAddedScreen)
                {
                    uc_mem_read(MTK, vmAddedScreen, &tScreenInitEntry, 4);
                    if (tScreenInitEntry)
                    {
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                        uc_reg_write(MTK, UC_ARM_REG_R0, &vmAddedScreen);
                        p = vm_emu_start(tScreenInitEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            printf("TScreen init异常:%s\n", uc_strerror(p));
                            break;
                        }
                        scheduler_trace_startup_ui_object("after_init", vmAddedScreen);
                    }
                    tScreenInitedPtr = vmAddedScreen;
                }
                scheduler_normalize_startup_screen_state();
                uc_mem_read(MTK, vmAddedScreen + 0x08, &tScreenEventEntry, 4);
                uc_mem_read(MTK, vmAddedScreen + 0x0c, &tScreenRenderEntry, 4);
                uc_mem_read(MTK, vmAddedScreen + 0x18, &tScreenResourceLoadEntry, 4);
                if (tScreenRenderEntry == 0)
                {
                    printf("TScreen未设置render入口\n");
                    assert(0);
                }
                u32 screenBeforeCallback = vmAddedScreen;
                p = scheduler_dispatch_tscreen_event(tScreenEventEntry, vmAddedScreen);
                if (p != UC_ERR_OK)
                {
                    printf("TScreen event异常:%s\n", uc_strerror(p));
                    break;
                }
                if (screenStructChange == 1)
                    break;
                if (vmAddedScreen != screenBeforeCallback)
                    continue;
                if (screenStructNotifyLoadRes == 1)
                {
                    screenStructNotifyLoadRes = 0;
                    if (tScreenResourceLoadEntry)
                    {
                        vm_net_trace("tscreen_resource_load entry=%08x screen=%08x\n", tScreenResourceLoadEntry, vmAddedScreen);
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                        uc_reg_write(MTK, UC_ARM_REG_R0, &vmAddedScreen);
                        p = vm_emu_start(tScreenResourceLoadEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            printf("TScreen resource load异常:%s\n", uc_strerror(p));
                            break;
                        }
                    }
                }
                scheduler_trace_startup_ui_object("before_render", vmAddedScreen);
                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                uc_reg_write(MTK, UC_ARM_REG_R0, &vmAddedScreen);
                p = vm_emu_start(tScreenRenderEntry, exitAddr);
                if (p != UC_ERR_OK)
                    break;
                vm_lcd_update_with_input_overlay();
                SDL_Delay(100);
            }
        }
        while (p == UC_ERR_OK)
        {

            u32 screenFuncPtr;
            u32 screenInitEntry;
            u32 screenDestoryEntry;
            u32 screenLogicEntry;
            u32 screenRenderEntry;
            u32 screenPauseEntry;
            u32 screenRemuseEntry;
            u32 screenResouceLoadEntry;
            u32 screenThisPtr = 0;
            while (p == UC_ERR_OK && screenStructChange != 1 && g_screenRemovedWithoutNext)
            {
                p = scheduler_tick();
                if (p != UC_ERR_OK)
                    break;
                if ((g_schedulerTick % 30) == 0)
                {
                    u8 waitUpdateState = 0;
                    u32 waitNet28 = 0;
                    u32 waitNet30 = 0;
                    u32 waitNextScreen = 0;
                    uc_mem_read(MTK, Global_R9 + 0x4cb6, &waitUpdateState, 1);
                    uc_mem_read(MTK, Global_R9 + 0x9588 + 0x28, &waitNet28, 4);
                    uc_mem_read(MTK, Global_R9 + 0x9588 + 0x30, &waitNet30, 4);
                    uc_mem_read(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &waitNextScreen, 4);
                    vm_net_trace("screen_wait_no_active updateState=%u nextScreen=%08x net28=%08x net30=%08x tick=%u\n",
                                 waitUpdateState, waitNextScreen, waitNet28, waitNet30, g_schedulerTick);
                }
                SDL_Delay(100);
            }
            if (p != UC_ERR_OK)
                break;
            if (screenStructChange != 1)
                continue;
            if (screenStructChange == 1)
            {
                uc_mem_read(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &screenFuncPtr, 4); // 得到screen函数表地址的指针
                if (screenFuncPtr >= Global_R9 && screenFuncPtr < ROM_ADDRESS + size_16mb)
                {
                    screenThisPtr = screenFuncPtr - 0x18;
                }
                uc_mem_read(MTK, screenFuncPtr, &screenInitEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 4, &screenDestoryEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 8, &screenLogicEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 12, &screenRenderEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 16, &screenPauseEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 20, &screenRemuseEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 24, &screenResouceLoadEntry, 4);
                printf("[SCR_FUNC](init:%x,destory:%x,logic:%x,render:%x,pause:%x,remuse:%x,resLoad:%x)\n", screenInitEntry, screenDestoryEntry, screenLogicEntry, screenRenderEntry, screenPauseEntry, screenRemuseEntry, screenResouceLoadEntry);
                screenStructChange = 0;
                g_activeScreenRemovedThisFrame = 0;
            }

            uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
            if (screenThisPtr)
            {
                scheduler_prepare_screen_call(screenThisPtr);
                uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
            }
            if (g_screenResumeExisting)
            {
                if (screenRemuseEntry)
                    p = vm_emu_start(screenRemuseEntry, exitAddr);
                else
                    p = UC_ERR_OK;
                printf("ScreenResume Ok\n");
                vm_net_trace("screen_resume_existing screen=%08x this=%08x resume=%08x\n",
                             screenFuncPtr, screenThisPtr, screenRemuseEntry);
                g_screenResumeExisting = 0;
            }
            else
            {
                p = vm_emu_start(screenInitEntry, exitAddr);
                printf("ScreenInit Ok\n");
            }
            if (p == UC_ERR_OK)
            {
                while (true)
                {
                    p = scheduler_tick();
                    if (p != UC_ERR_OK)
                        break;
                    scheduler_fire_auto_touch();
                    if (screenStructChange == 1)
                        break;
                    if (g_screenRemovedWithoutNext)
                        break;
                    if (screenStructNotifyLoadRes == 1)
                    {
                        screenStructNotifyLoadRes = 0;
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
                        if (screenThisPtr)
                        {
                            scheduler_prepare_screen_call(screenThisPtr);
                            uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                        }
                        p = vm_emu_start(screenResouceLoadEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            printf("SCR_ResourceLoad异常:%s\n", uc_strerror(p));
                            assert(0);
                        }
                    }
                    if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                        break;
                    simulateKey = 0;
                    simulatePress = 0;
                    simulateTouchDown = 0;
                    simulateTouchUp = 0;
                    simulateTouchDrag = 0;
                    vm_event *evt = DequeueVMEvent();
                    if (evt != NULL)
                    {
                        if (evt->event == VM_EVENT_INPUT_CHAR || evt->event == VM_EVENT_INPUT_BACKSPACE || evt->event == VM_EVENT_INPUT_DONE)
                        {
                            p = scheduler_dispatch_input_event(evt);
                            if (p != UC_ERR_OK)
                            {
                                printf("SCR_Input异常:%s\n", uc_strerror(p));
                                assert(0);
                            }
                        }
                        if (evt->event == VM_EVENT_KEYBOARD || evt->event == VM_EVENT_TOUCHSCREEN)
                        {
                            if (evt->event == VM_EVENT_KEYBOARD)
                            {
                                simulateKey = evt->r0;
                                simulatePress = evt->r1;
                            }
                            if (evt->event == VM_EVENT_TOUCHSCREEN)
                            {
                                simulateTouchPress = evt->r0 != MR_MOUSE_UP;
                                simulateTouchDown = evt->r0 == MR_MOUSE_DOWN;
                                simulateTouchUp = evt->r0 == MR_MOUSE_UP;
                                simulateTouchDrag = evt->r0 == MR_MOUSE_MOVE;
                                simulateTouchX = evt->r1 & 0xffff;
                                simulateTouchY = (evt->r1 >> 16) & 0xffff;
                            }
                            if (screenThisPtr && vm_is_pool_entry(screenLogicEntry))
                            {
                                u32 eventType = 0;
                                u32 eventArg = 0;
                                if (evt->event == VM_EVENT_KEYBOARD)
                                {
                                    u32 keyMask = evt->r0 < 31 ? (1u << evt->r0) : 0;
                                    eventType = evt->r1 ? 0 : 1;
                                    eventArg = vm_malloc_var();
                                    vm_set_var(eventArg, keyMask);
                                }
                                else
                                {
                                    eventType = evt->r0 == MR_MOUSE_UP ? 4 : 3;
                                    eventArg = vm_malloc_var();
                                    vm_set_var(eventArg, evt->r1);
                                }
                                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                                scheduler_prepare_screen_call(screenThisPtr);
                                uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                                uc_reg_write(MTK, UC_ARM_REG_R1, &eventType);
                                uc_reg_write(MTK, UC_ARM_REG_R2, &eventArg);
                                p = vm_emu_start(screenLogicEntry, exitAddr);
                                if (evt->event == VM_EVENT_KEYBOARD || evt->event == VM_EVENT_TOUCHSCREEN)
                                    vm_free_var(eventArg);
                                if (p != UC_ERR_OK)
                                {
                                    printf("SCR_Event异常:%s\n", uc_strerror(p));
                                    assert(0);
                                }
                                if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                                    break;
                            }
                        }
                    }
                    if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                        break;
                    if (1)
                    {
                        if (!vm_is_pool_entry(screenLogicEntry))
                        {
                            uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                            if (screenThisPtr)
                            {
                                scheduler_prepare_screen_call(screenThisPtr);
                                uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                            }
                            p = vm_emu_start(screenLogicEntry, exitAddr);
                            if (p != UC_ERR_OK)
                            {
                                printf("SCR_Logic异常:%s\n", uc_strerror(p));
                                assert(0);
                            }
                            if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                                break;
                        }

                        if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                            break;
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                        if (screenThisPtr)
                        {
                            scheduler_prepare_screen_call(screenThisPtr);
                            uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                        }
                        p = vm_emu_start(screenRenderEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            dumpCpuInfo();
                            printf("SCR_Render异常:%s\n", uc_strerror(p));
                            assert(0);
                        }
                        if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                            break;
                    }
                    vm_lcd_update_with_input_overlay();
                    SDL_Delay(100);
                }
                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                if (screenThisPtr)
                {
                    scheduler_prepare_screen_call(screenThisPtr);
                    uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                }
                if (g_activeScreenRemovedThisFrame)
                {
                    vm_net_trace("screen_destroy_skip_removed screen=%08x destroy=%08x this=%08x\n",
                                 screenFuncPtr, screenDestoryEntry, screenThisPtr);
                    g_activeScreenRemovedThisFrame = 0;
                }
                else
                {
                    if (screenDestoryEntry)
                    {
                        p = vm_emu_start(screenDestoryEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            printf("SCR_Destory异常\n");
                            break;
                        }
                    }
                    else
                    {
                        vm_net_trace("screen_destroy_skip_null screen=%08x this=%08x\n",
                                     screenFuncPtr, screenThisPtr);
                    }
                }
            }
            else
            {
                printf("SCR_Init异常\n");
                break;
            }
        }
    }
    else
        printf("入口初始化失败\n");

    if (p == UC_ERR_READ_UNMAPPED)
        printf("模拟错误：此处内存不可读\n");
    else if (p == UC_ERR_WRITE_UNMAPPED)
        printf("模拟错误：此处内存不可写\n");
    else if (p == UC_ERR_FETCH_UNMAPPED)
        printf("模拟错误：此处内存不可执行\n");
    else if (p != UC_ERR_OK)
        printf("模拟错误：(未处理)%s\n", uc_strerror(p));
    else
        printf("程序已正常退出\n");

    dumpCpuInfo();
    if (p != UC_ERR_OK)
        assert(0);
#ifdef GDB_SERVER_SUPPORT
    send_gdb_response(&clients[0], "S01");
#endif
}

void MainUpdateTask()
{
    while (1)
    {
        currentTime = clock();

#ifdef GDB_SERVER_SUPPORT
        if (gdbTarget.running == 0)
        {
            usleep(1000);
            continue;
        }
#endif
        usleep(1000);
    }
}

int main(int argc, char *args[])
{

    uc_err err;
    uc_hook trace;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // SetConsoleOutputCP(CP_UTF8);
    // while(1);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }
    /* 勿用 SDL_WINDOW_OPENGL：本工程用 GetWindowSurface 直接写像素，OpenGL 窗口下格式/stride 常异常 → 竖条花屏 */
#ifdef GDI_LAYER_DEBUG
    window = SDL_CreateWindow("moral i9 simulato", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH * 5, SCREEN_HEIGHT, 0);
#else
    window = SDL_CreateWindow("Cbe Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 240, 400, 0);
#endif
    if (window == NULL)
    {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }
    SDL_Surface *startupSurface = SDL_GetWindowSurface(window);
    if (startupSurface)
    {
        SDL_FillRect(startupSurface, NULL, SDL_MapRGB(startupSurface->format, 0, 0, 0));
        SDL_UpdateWindowSurface(window);
    }

    InitVmEvent();

    char nameBuff[64] = LOAD_CBE_PATH;
    utf8_to_gbk(nameBuff, cbeTextString, mySizeOf(cbeTextString));
    char *fileBuffer = readFile(cbeTextString, &changeTmp1);
    g_cbeFileBuffer = (u8 *)fileBuffer;
    g_cbeFileSize = changeTmp1;
    // 分析前150字节
    parseCbeHeader(fileBuffer, changeTmp1);

    if (g_cbeInfo.isBiggianProgram)
        err = uc_open(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN, &MTK);
    else
        err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &MTK);

    if (err)
    {
        printf("Failed on uc_open() with error returned: %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    ROM_MEMPOOL = SDL_malloc(size_16mb);
    STACK_MEMPOOL = SDL_malloc(size_4mb);
    PRAM_MEMPOOL = SDL_malloc(size_1mb);
    RAM_MEMPOOL = SDL_malloc(VM_MEMPOOL_TOTAL_SIZE);

    err = uc_mem_map_ptr(MTK, ROM_ADDRESS, size_16mb, UC_PROT_ALL, ROM_MEMPOOL);
    err = uc_mem_map_ptr(MTK, STACK_ADDRESS, size_1mb, UC_PROT_ALL, STACK_MEMPOOL);
    err = uc_mem_map_ptr(MTK, VM_Manager_Table_ADDRESS, size_1mb, UC_PROT_ALL, PRAM_MEMPOOL);
    err = uc_mem_map_ptr(MTK, VM_FUNC_HK_TABLE_ADDRESS, size_1mb, UC_PROT_ALL, SDL_malloc(size_1mb));
    uc_mem_map_ptr(MTK, VM_Memory_Pool_ADDRESS, VM_MEMPOOL_TOTAL_SIZE, UC_PROT_ALL, RAM_MEMPOOL);
    uc_mem_map(MTK, PROGRAM_EXIT_ADDR, 0x1000, UC_PROT_ALL);

    InitVmMalloc();
    InitLcd();
    vm_lcd_update_with_input_overlay();
    InitFontEngine();

    if (err)
    {
        printf("Failed mem  Rom map: %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    err = uc_hook_add(MTK, &trace, UC_HOOK_CODE, hookCodeCallBack, 0, 0, 0xFFFFFFFF);
    if (err == UC_ERR_OK)
        err = add_manager_code_hooks(MTK);
    //    err = uc_hook_add(MTK, &trace, UC_HOOK_BLOCK, hookBlockCallBack, 0, 0, 0xFFFFFFFF);

    uc_hook_add(MTK, &trace, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, hookRamCallBack, 0, 0, 0xFFFFFFFF);

    err = uc_hook_add(MTK, &trace, UC_HOOK_MEM_READ_UNMAPPED, hookRamErrorBack, 2, 0, 0xFFFFFFFF);
    err = uc_hook_add(MTK, &trace, UC_HOOK_MEM_WRITE_UNMAPPED, hookRamErrorBack, 3, 0, 0xFFFFFFFF);
    err = uc_hook_add(MTK, &trace, UC_HOOK_MEM_FETCH_UNMAPPED, hookRamErrorBack, 4, 0, 0xFFFFFFFF);

    err = uc_hook_add(MTK, &trace, UC_HOOK_INTR, hookCpuIntr, NULL, 1, 0);

    err = uc_hook_add(MTK, &trace, UC_HOOK_INSN_INVALID, hookInsnInvalid, 4, 0, 0xFFFFFFFF);

    if (err != UC_ERR_OK)
    {
        printf("add hook err %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    if (MTK != NULL)
    {
        // 写入code段
        uc_mem_write(MTK, ROM_ADDRESS, fileBuffer + g_cbeInfo.codeOffset, g_cbeInfo.codeLen);

        printf("File Entry Point:0x%x\n", g_cbeInfo.codeOffset);
        // 数据段起始位置放这里
        // codeSize = headerInt2 + headerInt4
        uc_mem_write(MTK, ROM_ADDRESS + g_cbeInfo.headerInt2, fileBuffer + g_cbeInfo.BssDataOffset, g_cbeInfo.BssDataLen);
        printf("Data In Rom Address:0x%x - 0x%x\n", ROM_ADDRESS + g_cbeInfo.headerInt2, ROM_ADDRESS + g_cbeInfo.headerInt2 + g_cbeInfo.headerInt4);

        changeTmp3 = VM_MANAGER_TABLE_ADDRESS;
        uc_mem_write(MTK, VM_Manager_Table_ADDRESS + 8, &changeTmp3, 4); // vmManager函数表地址
        changeTmp3 = VM_LOG_TRACE_ADDRESS;
        uc_mem_write(MTK, VM_Manager_Table_ADDRESS + 12, &changeTmp3, 4); // vm_log_trace函数地址
        changeTmp3 = VM_CURR_APP_INFO_ADDRESS;
        uc_mem_write(MTK, VM_Manager_Table_ADDRESS + 16, &changeTmp3, 4); // vcurAppFileName全局变量地址

        vm_initManagerTable();

        Global_R9 = ROM_ADDRESS + g_cbeInfo.headerInt2;
        uc_reg_write(MTK, UC_ARM_REG_R9, &Global_R9); // r9写入数据段地址

        changeTmp2 = STACK_ADDRESS + size_1mb; // 映射栈内存
        uc_reg_write(MTK, UC_ARM_REG_SP, &changeTmp2);

        // 启动emu线程
        changeTmp1 = ROM_ADDRESS;

        pthread_create(&EmuThread, NULL, RunArmProgram, changeTmp1);
        pthread_create(&MainUpdareThread, NULL, MainUpdateTask, NULL);
#ifdef GDB_SERVER_SUPPORT
        pthread_create(&gdb_server_mutex, NULL, gdb_server_main, NULL);
#endif
        printf("Unicorn Engine Initialized\n");

        loop();
    }
    return 0;
}

// 是否禁用IRQ中断
bool isIRQ_Disable(u32 cpsr)
{
    return (cpsr & (1 << 7));
}
bool isIrqMode(u32 cpsr)
{
    return (cpsr & 0xFFFFFFE0 | 0x12) == cpsr;
}
void hookBlockCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    /* 不再使用 block hook 投递事件；改用 uc_emu_start timeout 机制。
     * 保留空回调以兼容 uc_hook_add 注册。 */
    handleVmEvent_EMU(address);
}

/**
 * pc指针指向此地址时执行(未执行此地址的指令)
 */

static bool hook_vm_manager_func(u32 address)
{
    if (!(address >= VM_MANAGER_FUNC_LIST_ADDRESS && address < VM_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS, 30);
        vm_set_call_result(tmp1);
    }
    else if (idx == 1)
    {
        tmp1 = VM_MANAGER_FILEIO_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetIoManager\n");
    }
    else if (idx == 2)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_LCD_FUNC_LIST_ADDRESS, 95);
        vm_set_call_result(tmp1);
    }
    else if (idx == 3)
    {
        tmp1 = VM_MANAGER_LCD_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetLcdManager\n");
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_TIMER_FUNC_LIST_ADDRESS, 10);
        vm_set_call_result(tmp1);
    }
    else if (idx == 5)
    {
        tmp1 = VM_MANAGER_TIMER_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetTimeManager\n");
    }
    else if (idx == 6)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_CTRL_FUNC_LIST_ADDRESS, 21);
        vm_set_call_result(tmp1);
    }
    else if (idx == 7)
    {
        tmp1 = VM_MANAGER_CTRL_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetCtrlManager\n");
    }
    else if (idx == 8)
    {
        // 传入指针写函数表
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMInitMemoryManager(%x)[%x]\n", tmp1, lastAddress);
        for (tmp2 = 0; tmp2 < 27; tmp2++)
        {
            tmp3 = VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS + tmp2 * 4;
            uc_mem_write(MTK, tmp1 + tmp2 * 4, &tmp3, 4);
        }
    }
    else if (idx == 9)
    {
        tmp1 = VM_MEMORY_MANAGER_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetMemoryManager\n");
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_BILLING_FUNC_LIST_ADDRESS, 38);
        vm_set_call_result(tmp1);
    }
    else if (idx == 11)
    {
        tmp1 = VM_MANAGER_BILLING_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetBillingManager\n");
    }
    else if (idx == 12)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS, 11);
        vm_set_call_result(tmp1);
    }
    else if (idx == 13)
    {
        tmp1 = VM_MANAGER_SCREEN_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetScreenManager\n");
    }
    else if (idx == 14)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_net_trace("manager_init_net table=%08x last=%08x\n", tmp1, lastAddress);
        if (tmp1)
        {
            for (tmp2 = 0; tmp2 < 43; tmp2++)
            {
                tmp3 = VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS + tmp2 * 4;
                uc_mem_write(MTK, tmp1 + tmp2 * 4, &tmp3, 4);
            }
        }
        vm_set_call_result(0);
    }
    else if (idx == 15)
    {
        tmp1 = VM_MANAGER_NETWORK_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        vm_net_trace("manager_get_net table=%08x last=%08x\n", tmp1, lastAddress);
        DEBUG_PRINT("[call]vMGetNetManager\n");
    }
    else if (idx == 16)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_UCS2_FUNC_LIST_ADDRESS, 11);
        vm_set_call_result(tmp1);
    }
    else if (idx == 17)
    {
        tmp1 = VM_MANAGER_UCS2_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetUcs2StrManager\n");
    }
    else if (idx == 18)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_SYS_MANAGER_FUNC_LIST_ADDRESS, 115);
        vm_set_call_result(tmp1);
    }
    else if (idx == 19)
    {
        DEBUG_PRINT("[call]vMGetSysManager\n");
        // 返回sys函数表地址
        tmp1 = VM_SYS_MANAGER_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 20)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
            uc_mem_write(MTK, tmp1, emptyBuff, VM_MANAGER_TABLE_SIZE);
        vm_set_call_result(0);
    }
    else if (idx == 21)
    {
        uc_mem_write(MTK, VM_MANAGER_DF_SCRIPT_TABLE_ADDRESS, emptyBuff, VM_MANAGER_TABLE_SIZE);
        tmp1 = VM_MANAGER_DF_SCRIPT_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetDFScriptManager\n");
    }
    else if (idx == 22)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS, 24);
        vm_set_call_result(tmp1);
    }
    else if (idx == 23)
    {
        tmp1 = VM_MANAGER_GAME_LCD_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetGameLcdManager\n");
    }
    else if (idx == 24)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS, 40);
        vm_set_call_result(tmp1);
    }
    else if (idx == 25)
    {
        tmp1 = VM_MANAGER_GAME_UTIL_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetGameUtilManager\n");
    }
    else if (idx == 26)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
        {
            tmp3 = VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS + 8 * 4;
            uc_mem_write(MTK, tmp1 + 8 * 4, &tmp3, 4);
            tmp3 = VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS + 10 * 4;
            uc_mem_write(MTK, tmp1 + 10 * 4, &tmp3, 4);
        }
        vm_set_call_result(tmp1);
    }
    else if (idx == 27)
    {
        tmp1 = VM_MANAGER_DF_ENGINE_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetDFEnginelManager\n");
    }
    else if (idx == 28)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
        {
            tmp3 = VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS + 60 * 4;
            uc_mem_write(MTK, tmp1 + 60 * 4, &tmp3, 4);
        }
        vm_set_call_result(0);
    }
    else if (idx == 29)
    {
        tmp1 = VM_MANAGER_NETAPP_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetNetAppManager\n");
    }
    else if (idx == 30)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS, 31);
        vm_set_call_result(tmp1);
    }
    else if (idx == 31)
    {
        tmp1 = VM_MANAGER_AUDIO_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetAudioManager\n");
    }
    else if (idx == 32)
    {
        // 传入指针写函数表
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        for (tmp2 = 0; tmp2 < 144; tmp2++)
        {
            tmp3 = VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS + tmp2 * 4;
            uc_mem_write(MTK, tmp1 + tmp2 * 4, &tmp3, 4);
        }
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);

        DEBUG_PRINT("[call]vMInitGameManagerOld(%x,%X)[%x]\n", tmp1, tmp2, lastAddress);
    }
    else if (idx == 33)
    {
        tmp1 = VM_MANAGER_GAMEOLD_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetGameManagerOld\n");
    }
    else if (idx == 34)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
        {
            for (tmp2 = 0; tmp2 < 40; tmp2++)
            {
                tmp3 = VM_MANAGER_FUNC_LIST_ADDRESS + tmp2 * 4;
                uc_mem_write(MTK, tmp1 + tmp2 * 4, &tmp3, 4);
            }
        }
        vm_set_call_result(0);
    }
    else if (idx == 35)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS, 11);
        vm_set_call_result(tmp1);
    }
    else if (idx == 36)
    {
        tmp1 = VM_MANAGER_SENSOR_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetGSensorManager\n");
    }
    else if (idx == 37)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_STDIO_FUNC_LIST_ADDRESS, 22);
        vm_set_call_result(tmp1);
    }
    else if (idx == 38)
    {
        tmp1 = VM_MANAGER_STDIO_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetVmStdManager\n");
    }
    else if (idx == 39)
    {
        printf("[call]vMInitDlLoadManager\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_InitDlLoadManager(tmp1);
    }
    else if (idx == 40)
    {
        printf("[call]vMGetDlLoadManager\n");
        vm_set_call_result(VM_DL_LOAD_MANAGER_ADDRESS);
    }
    else if (idx == 41)
    {
        vm_set_call_result(VM_DL_RS_MANAGER_ADDRESS);
    }
    else if (idx == 42)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_InitDlRsManager(tmp1);
    }
    else if (idx == 43)
    {
        vm_set_call_result(VM_DL_IMAGE_MANAGER_ADDRESS);
    }
    else if (idx == 44)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_InitDlImageManager(tmp1);
    }
    else if (idx == 45)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_MANAGER_VMIM_FUNC_LIST_ADDRESS, 6);
        vm_set_call_result(tmp1);
    }
    else if (idx == 46)
    {
        tmp1 = VM_MANAGER_VMIM_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetVmImManager\n");
    }
    else if (idx == 49)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_configManagerTableCount(tmp1, VM_VIDEO_FUNC_LIST_ADDRESS, 38);
        vm_set_call_result(tmp1);
    }
    else if (idx == 50)
    {
        printf("[call]VmGetVideoManager\n");
        vm_set_call_result(VM_VIDEO_MANAGER_ADDRESS);
    }
    else if (idx == 51)
    {
        printf("[call]VmGetDlWPayManager\n");
        vm_set_call_result(VM_DL_PAY_MANAGER_ADDRESS);
    }
    else
    {
        printf("[impl]vmManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_sys_manager_func(u32 address)
{
    if (!(address >= VM_SYS_MANAGER_FUNC_LIST_ADDRESS && address < (VM_SYS_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_SYS_MANAGER_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        printf("[call]vMIsSimReady\n");
        assert(0);
    }
    else if (idx == 1)
    {
        printf("[call]vmSysIsHaveNetWork\n");
        assert(0);
    }
    else if (idx == 2)
    {
        printf("[call]vMIsSystemReady\n");
        assert(0);
    }
    else if (idx == 3)
    {
        /*
ven_setting_getDefaultSIM(3, &v2, v1);
if ( ven_util_getMccMnc((unsigned __int8)v2, &v4, &n2) )
return 1;
vm_log_trace("[coolbar] xxl: master sim get MCC %d, MNC %d", (unsigned __int16)v4, (unsigned __int16)n2);
if ( !(_WORD)n2 || (unsigned __int16)n2 == 2 )
return 2;
if ( (unsigned __int16)n2 == 1 )
return 3;
return 4;
        */
        tmp1 = 3;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetOperator\n");
    }
    else if (idx == 4)
    {
        DEBUG_PRINT("[call]vMGetIMEI\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        snprintf((char *)cbeTextString, mySizeOf(cbeTextString), "111111111111111");
        tmp3 = strlen((char *)cbeTextString) + 1;
        if (tmp2 && tmp2 < tmp3)
            tmp3 = tmp2;
        if (tmp3 > 0)
        {
            if (tmp3 <= strlen((char *)cbeTextString))
                cbeTextString[tmp3 - 1] = 0;
            uc_mem_write(MTK, tmp1, cbeTextString, tmp3);
        }
        vm_set_call_result(0);
    }
    else if (idx == 5)
    {
        printf("[call]vMGetIMSI\n");
        assert(0);
    }
    else if (idx == 6)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        snprintf((char *)cbeTextString, mySizeOf(cbeTextString), "N6206");
        tmp3 = strlen((char *)cbeTextString) + 1;
        if (tmp2 && tmp2 < tmp3)
            tmp3 = tmp2;
        if (tmp3 > 0)
        {
            if (tmp3 <= strlen((char *)cbeTextString))
                cbeTextString[tmp3 - 1] = 0;
            uc_mem_write(MTK, tmp1, cbeTextString, tmp3);
        }
        vm_set_call_result(0);
    }
    else if (idx == 7)
    {
        printf("[call]vMGetPrjVersion\n");
        assert(0);
    }
    else if (idx == 8)
    {
        printf("[call]vMIsCallConnect\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]vMIsDCopen\n");
        assert(0);
    }
    else if (idx == 10)
    {
        printf("[call]vMGetStkCardStatus\n");
        assert(0);
    }
    else if (idx == 11)
    {
        printf("[call]vMGetActiveSim\n");
        assert(0);
    }
    else if (idx == 12)
    {
        printf("[call]vmGetCoolbarlistInit\n");
        assert(0);
    }
    else if (idx == 13)
    {
        vm_set_call_result(0);
    }
    else if (idx == 14)
    {
        vm_set_call_result(0);
    }
    else if (idx == 15)
    {
        DEBUG_PRINT("[call]vMAudioIsSupportInCb\n");
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 16)
    {
        u32 line = 0;
        u32 lr = 0;
        u32 sp = 0;
        vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
        uc_reg_read(MTK, UC_ARM_REG_R1, &line);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        printf("[call]vMAssert(%s:%u, lr:%x, sp:%x, last:%x)\n", cbeTextString, line, lr, sp, lastAddress);
        vm_net_trace("vMAssert file=%s line=%u lr=%08x sp=%08x last=%08x\n", cbeTextString, line, lr, sp, lastAddress);
        for (u32 off = 0; off < 0x80; off += 4)
        {
            u32 word = 0;
            if (uc_mem_read(MTK, sp + off, &word, 4) != UC_ERR_OK)
                break;
            printf("assert_stack[%02x]=%08x\n", off, word);
            if ((word >= 0x01000000 && word < 0x01100000) ||
                (word >= 0x1000000 && word < 0x1100000) ||
                (word >= ROM_ADDRESS && word < ROM_ADDRESS + 0x800000))
            {
                vm_net_trace("vMAssert_stack off=%02x word=%08x\n", off, word);
            }
        }
        dumpCpuInfo();
        fflush(stdout);
        assert(0);
    }
    else if (idx == 17)
    {
        // DEBUG_PRINT("[call]Coolbar_GetCoolbarDirPath\n");
        cbeTextString[0] = '.';
        cbeTextString[1] = 0;
        cbeTextString[2] = '/';
        cbeTextString[3] = 0;
        cbeTextString[4] = 0;
        cbeTextString[5] = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_mem_write(MTK, tmp1, cbeTextString, 6);
    }
    else if (idx == 18)
    {
        printf("[call]CoolBarDynamicGetVerByAppID\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]Res_GetCoolBarFullPath\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]vMGSenserIsSupportInCb\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]vMSysHandler\n");
        assert(0);
    }
    else if (idx == 22)
    {
        DEBUG_PRINT("[call]vMGetKeyNum\n");
        tmp1 = 33;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 23)
    {
        printf("[call]vMIsSupportTP\n");
        assert(0);
    }
    else if (idx == 24)
    {
        printf("[call]CDownGetCompany\n");
        assert(0);
    }
    else if (idx == 25)
    {
        // ignore
        DEBUG_PRINT("[call]CDownGetServicePhone\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        u8 *s = cbeTextString;
        *s++ = 'c';
        *s++ = 'b';
        *s++ = 'e';
        *s++ = '_';
        *s++ = 'e';
        *s++ = 'm';
        *s++ = 'u';
        *s++ = '\0';
        uc_mem_write(MTK, tmp1, cbeTextString, tmp2);
    }
    else if (idx == 26)
    {
        printf("[call]vMIsSupportIdleMenu\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]CDownGetData\n");
        assert(0);
    }
    else if (idx == 30)
    {
        DEBUG_PRINT("[call]GetCoolBarKernelCurrentVersion(返回46)\n");
        tmp1 = 46;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 31)
    {
        printf("[call]CoolBar_DownLoad_GetFile\n");
        assert(0);
    }
    else if (idx == 32)
    {
        printf("[call]CoolBar_DownLoad_Stop\n");
        assert(0);
    }
    else if (idx == 33)
    {
        vm_set_call_result(0x3ea);
    }
    else if (idx == 34)
    {
        printf("[call]vmSysIsVisibleApp\n");
        assert(0);
    }
    else if (idx == 35)
    {
        printf("[call]cDownSetForceUpdate\n");
        assert(0);
    }
    else if (idx == 36)
    {
        printf("[call]cDownGetModeAndVersion\n");
        assert(0);
    }
    else if (idx == 37)
    {
        printf("[call]mmiDynamicSetForceUpdate\n");
        assert(0);
    }
    else if (idx == 38)
    {
        printf("[call]mmiDunamicGetModeAndVersion\n");
        assert(0);
    }
    else if (idx == 39)
    {
        printf("[call]vMSwitchLog\n");
        assert(0);
    }
    else if (idx == 40)
    {
        printf("[call]Coolbar_GetResStatus\n");
        assert(0);
    }
    else if (idx == 41)
    {
        printf("[call]coolbar_Update_Tfold\n");
        assert(0);
    }
    else if (idx == 42)
    {
        printf("[call]CoolBar_EnterDmIn\n");
        assert(0);
    }
    else if (idx == 43)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_input_open(tmp1, tmp2, 0);
    }
    else if (idx == 44)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_input_open(tmp1, tmp2, 1);
    }
    else if (idx == 45)
    {
        g_vmInputOpen = 0;
        g_vmInputCallback = 0;
        g_vmInputBuffer = 0;
        g_vmInputMaxLen = 0;
        g_vmInputInputType = 0;
        g_vmInputPassword = 0;
        vm_set_call_result(0);
    }
    else if (idx == 46)
    {
        vm_set_call_result(g_vmInputOpen ? 1 : 0);
    }
    else if (idx == 47)
    {
        printf("[call]VmGetRand\n");
        assert(0);
    }
    else if (idx == 48)
    {
        printf("[call]srand\n");
        assert(0);
    }
    else if (idx == 49)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        snprintf((char *)cbeTextString, mySizeOf(cbeTextString), "+8613800100500");
        uc_mem_write(MTK, tmp1, cbeTextString, strlen((char *)cbeTextString) + 1);
        vm_set_call_result(0);
    }
    else if (idx == 50)
    {
        printf("[call]VmGetPrjCustom\n");
        assert(0);
    }
    else if (idx == 51)
    {
        printf("[call]CBGetNetInfo\n");
        assert(0);
    }
    else if (idx == 52)
    {
        printf("[call]VmGetCbNum\n");
        assert(0);
    }
    else if (idx == 53)
    {
        printf("[call]LzssEncode\n");
        assert(0);
    }
    else if (idx == 54)
    {
        printf("[call]LzssDecode\n");
        assert(0);
    }
    else if (idx == 55)
    {
        printf("[call]DMenuUpdateMenu\n");
        assert(0);
    }
    else if (idx == 56)
    {
        printf("[call]CDownUpdateMenu\n");
        assert(0);
    }
    else if (idx == 57)
    {
        printf("[call]CbGetPlatfomName\n");
        assert(0);
    }
    else if (idx == 58)
    {
        printf("[call]vMInnerAppInfo\n");
        assert(0);
    }
    else if (idx == 59)
    {
        printf("[call]vMGetInnerAppIcon\n");
        assert(0);
    }
    else if (idx == 60)
    {
        printf("[call]CDownGetAppType\n");
        assert(0);
    }
    else if (idx == 61)
    {
        printf("[call]vmSetGQQRunings\n");
        assert(0);
    }
    else if (idx == 62)
    {
        printf("[call]p_vmGetGQQRunings\n");
        assert(0);
    }
    else if (idx == 63)
    {
        printf("[call]vmGetQQAddress\n");
        assert(0);
    }
    else if (idx == 64)
    {
        printf("[call]GetCurrentScreenType\n");
        assert(0);
    }
    else if (idx == 65)
    {
        vm_set_call_result(0);
    }
    else if (idx == 66)
    {
        printf("[call]VmDlGetIMEI\n");
        assert(0);
    }
    else if (idx == 68)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (tmp1 && tmp2)
            uc_mem_write(MTK, tmp1, emptyBuff, tmp2 > sizeof(emptyBuff) ? sizeof(emptyBuff) : tmp2);
        vm_set_call_result(0);
    }
    else if (idx == 69)
    {
        vm_set_call_result(0);
    }
    else if (idx == 70)
    {
        printf("[call]coolbar_GetAppNameByIdFromList\n");
        assert(0);
    }
    else if (idx == 71)
    {
        printf("[call]coolbar_Update_DeleteAppInfo\n");
        assert(0);
    }
    else if (idx == 72)
    {
        printf("[call]VmGetDMenuFileName\n");
        assert(0);
    }
    else if (idx == 73)
    {
        printf("[call]VmGetCDownFileName\n");
        assert(0);
    }
    else if (idx == 74)
    {
        printf("[call]GetCDownAppUrl\n");
        assert(0);
    }
    else if (idx == 75)
    {
        printf("[call]vmDlGetPreAppId\n");
        assert(0);
    }
    else if (idx == 76)
    {
        printf("[call]Coolbar_ParseDownDataFile\n");
        assert(0);
    }
    else if (idx == 77)
    {
        printf("[call]CoolBar_DownLoad_CBE\n");
        assert(0);
    }
    else if (idx == 78)
    {
        printf("[call]Coolbar_PreLoadAppEx\n");
        assert(0);
    }
    else if (idx == 79)
    {
        tmp1 = Global_R9;
        vm_set_call_result(tmp1);
    }
    else if (idx == 80)
    {
        // todo
        DEBUG_PRINT("[call]vMGetGameWinState\n");
        tmp2 = 1; // running
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 81)
    {
        printf("[call]CDownGetHideText\n");
        assert(0);
    }
    else if (idx == 82)
    {
        printf("[call]vMPhbMultiSelectEntry\n");
        assert(0);
    }
    else if (idx == 83)
    {
        printf("[call]vmSetCurActiveSim\n");
        assert(0);
    }
    else if (idx == 84)
    {
        printf("[call]vmGetAllSimStatus\n");
        assert(0);
    }
    else if (idx == 85)
    {
        printf("[call]VmSendMMS\n");
        assert(0);
    }
    else if (idx == 86)
    {
        printf("[call]VmGetFocusWinID\n");
        assert(0);
    }
    else if (idx == 87)
    {
        printf("[call]VmIsWinOpen\n");
        assert(0);
    }
    else if (idx == 88)
    {
        printf("[call]vmIsIdleWinFocus\n");
        assert(0);
    }
    else if (idx == 89)
    {
        // DEBUG_PRINT("[call]vmIsInnerApp\n");
        // 这里只能返回1，要不然就会启动别的没安装的CBE文件
        vm_set_call_result(1);
    }
    else if (idx == 90)
    {
        // DEBUG_PRINT("[call]vmGetInnerAppVer\n");
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 91)
    {
        printf("[call]VmGetMixMenuLength\n");
        assert(0);
    }
    else if (idx == 92)
    {
        printf("[call]VmGetMixMenudata\n");
        assert(0);
    }
    else if (idx == 93)
    {
        printf("[call]VmGetWpayCBMInfo\n");
        assert(0);
    }
    else if (idx == 94)
    {
        printf("[call]VMGetCurVerAllInfo\n");
        assert(0);
    }
    else if (idx == 95)
    {
        printf("[call]vMSetFpsSleepFlag\n");
        assert(0);
    }
    else if (idx == 96)
    {
        printf("[call]cbSetSmsCenterNum\n");
        assert(0);
    }
    else if (idx == 97)
    {
        printf("[call]vmGetBuildTime\n");
        assert(0);
    }
    else if (idx == 98)
    {
        printf("[call]vmGetUsedTimes\n");
        assert(0);
    }
    else if (idx == 99)
    {
        printf("[call]vmSysGetBatteryInfo\n");
        assert(0);
    }
    else if (idx == 100)
    {
        printf("[call]vmSysSetLcdBright\n");
        assert(0);
    }
    else if (idx == 101)
    {
        printf("[call]vmSysResetLcdBright\n");
        assert(0);
    }
    else if (idx == 102)
    {
        printf("[call]vmSysSetPowerSaveMode\n");
        assert(0);
    }
    else if (idx == 103)
    {
        printf("[call]vMGetOperatorMCC\n");
        assert(0);
    }
    else if (idx == 104)
    {
        printf("[call]vMGetOperatorMNC\n");
        assert(0);
    }
    else if (idx == 105)
    {
        printf("[call]VmSupportAppSotre\n");
        assert(0);
    }
    else if (idx == 106)
    {
        //  printf("[call]CDownGetCompanyEx\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        u8 *s = cbeTextString;
        *s++ = '\0';
        uc_mem_write(MTK, tmp1, cbeTextString, tmp2);
    }
    else if (idx == 107)
    {
        printf("[call]vmSysGetCurrLcdLightInfo\n");
        assert(0);
    }
    else if (idx == 108)
    {
        printf("[call]vmSysStartVibration\n");
        assert(0);
    }
    else if (idx == 109)
    {
        printf("[call]vmSysStopVibration\n");
        assert(0);
    }
    else if (idx == 110)
    {
        printf("[call]vmSysOpenBrowser\n");
        assert(0);
    }
    else if (idx == 111)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_sys_set_setting_profile(tmp1);
    }
    else if (idx == 112)
    {
        vm_sys_get_setting_profile();
    }
    else if (idx == 113)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_sys_get_setting_profile_name(tmp1, tmp2, tmp3);
    }
    else if (idx == 114)
    {
        printf("[call]vmSysSaveContactPerson\n");
        assert(0);
    }
    else
    {

        printf("[impl]vmManagerSys调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_memory_manager_func(u32 address)
{
    if (!(address >= VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS && address < (VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        DEBUG_PRINT("[call]DF_InitMemory\n");
        vm_set_call_result(0);
    }
    else if (idx == 1)
    {
        DEBUG_PRINT("[call]DF_ReleaseMemory\n");
        vm_set_call_result(0);
    }
    else if (idx == 2)
    {
        // 参数1申请的内存块地址，参数2申请的内存大小，返回1
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        DEBUG_PRINT("[call]DF_Malloc_IN(%x,%x)\n", tmp1, tmp2);
        tmp3 = vm_malloc(tmp2);
        vm_net_trace("mem_mgr idx=2 DF_Malloc_IN dst=%08x size=%u out=%08x last=%08x\n",
                     tmp1, tmp2, tmp3, lastAddress);
        uc_mem_write(MTK, tmp1, &tmp3, 4);
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 3)
    {
        DEBUG_PRINT("[call]DF_Free\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_mem_read(MTK, tmp1, &tmp2, 4);
        vm_free(tmp2);
        tmp2 = 0;
        uc_mem_write(MTK, tmp1, &tmp2, 4);
    }
    else if (idx == 4)
    {
        DEBUG_PRINT("[call]DF_Memory_gc\n");
        vm_set_call_result(0);
    }
    else if (idx == 5)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // int* p_g_memoryBlock
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // size
        DEBUG_PRINT("[call]initMemoryBlock(%x,%x)\n", tmp1, tmp2);
        vm_initMemoryBlock(tmp1, tmp2);
    }
    else if (idx == 6)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_net_trace("mem_mgr idx=6 MF_MemoryBlock_Malloc block=%08x size=%u last=%08x\n",
                     tmp1, tmp2, lastAddress);
        vm_MF_MemoryBlock_Malloc(tmp1, tmp2);
        DEBUG_PRINT("[call]MF_MemoryBlock_Malloc(%x,%x)\n", tmp1, tmp2);
    }
    else if (idx == 7)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_MF_MemoryBlock_Reset(tmp1);
        DEBUG_PRINT("[call]MF_MemoryBlock_Reset\n");
    }
    else if (idx == 8)
    {
        DEBUG_PRINT("[call]getMemoryBlockPtr\n");
        tmp1 = VM_MemoryBlock_PTR_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 9)
    {
        DEBUG_PRINT("[call]MF_InitGmemoryBlock\n");
        vm_initMemoryBlock(VM_MemoryBlock_PTR_ADDRESS, VM_MemoryBlock_SIZE);
        vm_set_call_result(VM_MemoryBlock_PTR_ADDRESS);
    }
    else if (idx == 10)
    {
        // todo
        DEBUG_PRINT("[call]MF_ReleaseGmemoryBlock\n");
        vm_MF_resetGmemoryBlock();
    }
    else if (idx == 11)
    {
        // todo
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // size
        DEBUG_PRINT("[call]MF_resetGmemoryBlock\n");
        vm_MF_resetGmemoryBlock(tmp1);
    }
    else if (idx == 12)
    {
        // todo
        DEBUG_PRINT("[call]MF_MallocGmemoryBlock\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp3);
        tmp1 = VM_DreamFactory_MemoryBlock_ADDRESS;
        uc_mem_read(MTK, tmp1, &tmp2, 4);
        vm_MF_MemoryBlock_Malloc(tmp2, tmp3);
    }
    else if (idx == 13)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // size
        tmp2 = vm_malloc(tmp1);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 14)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
            vm_free(tmp1);
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 15)
    {
        printf("[call]DF_Memory_AttachPointer\n");
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]MF_MemoryBlock_Release\n");
        assert(0);
    }
    else if (idx == 17)
    {
        printf("[call]DF_InitMemoryEx\n");
        assert(0);
    }
    else if (idx == 18)
    {
        printf("[call]GAME_Image_realloc\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]DF_Malloc_debug\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]mallocBigMen_debug\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]CoolbarGetshareMemAlloced\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmMemManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_lcd_func(u32 address)
{
    if (!(address >= VM_MANAGER_LCD_FUNC_LIST_ADDRESS && address < (VM_MANAGER_LCD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_LCD_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        // DEBUG_PRINT("[call]vMGetCurrMainScreenImage\n");
        tmp1 = VM_screenImageStruct_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 2)
    {
        DEBUG_PRINT("[call]vMGetLCDBuffer\n");
        tmp1 = VM_screenImage_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 3)
    {
        vm_lcd_update_with_input_overlay();
        vm_set_call_result(0);
    }
    else if (idx == 4)
    {
        DEBUG_PRINT("[call]vMGetCurrFontType\n");
        tmp1 = g_currentFontType;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 5)
    {
        DEBUG_PRINT("[call]vMSetCurrFontType\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        g_currentFontType = tmp1;
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 6)
    {
        DEBUG_PRINT("[call]vMGetFontWidth\n");
        tmp1 = getFontWidth();
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 7)
    {
        tmp1 = getFontHeight();
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]vMGetFontHeight\n");
    }
    else if (idx == 8)
    {
        vm_readStringGbkByReg(UC_ARM_REG_R0, cbeTextString);
        tmp1 = vm_lcd_measure_current_string(cbeTextString);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        // gbk_to_utf8(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
        DEBUG_PRINT("[call]vMGetStringWidth(%d,%x)\n", tmp1, cbeTextString[0]);
        // tmp1 = mesureStringWidth(cbeTextString);
    }
    else if (idx == 9)
    {
        DEBUG_PRINT("[call]vMGetStringHeight\n");
        tmp1 = 18;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_readStringGbkByReg(UC_ARM_REG_R0, cbeTextString);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
        vm_trace_lcd_text("vMDrawString", idx, tmp1, x, y, (u16)tmp4, cbeTextString);
        vm_lcd_draw_current_string(cbeTextString, x, y, (u16)tmp4);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 11)
    {
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        u16 color;
        uc_mem_read(MTK, tmp4, &color, 2);
        vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);

        // gbk_to_utf8(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
        DEBUG_PRINT("[call]vMDrawStringEx(%d,%d,%s)\n", tmp2, tmp3, sprintfBuff);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
        vm_trace_lcd_text("vMDrawStringEx", idx, tmp1, x, y, color, cbeTextString);

        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 12)
    {
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        u16 color = 0xffff;
        uc_mem_read(MTK, tmp4 + 16, &color, 2);
        vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
        vm_trace_lcd_text("vMShowStringClipAlign", idx, tmp1, x, y, color, cbeTextString);
        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        vm_set_call_result(1);
    }
    else if (idx == 13)
    {
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        u16 color = 0xffff;
        uc_mem_read(MTK, tmp4 + 16, &color, 2);
        vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
        vm_trace_lcd_text("vMShowStringClip", idx, tmp1, x, y, color, cbeTextString);
        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        vm_set_call_result(1);
    }
    else if (idx == 14)
    {
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        u16 color = 0xffff;
        uc_mem_read(MTK, tmp4 + 16, &color, 2);
        vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
        vm_trace_lcd_text("vMShowStringRect", idx, tmp1, x, y, color, cbeTextString);
        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        vm_set_call_result(1);
    }
    else if (idx == 15)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);
        u16 color = 0xffff;
        uc_mem_read(MTK, tmp5, &color, 2);
        int x0 = vm_lcd_coord_from_reg(tmp1);
        int y0 = vm_lcd_coord_from_reg(tmp2);
        int x1 = vm_lcd_coord_from_reg(tmp3);
        int y1 = vm_lcd_coord_from_reg(tmp4);
        vm_trace_lcd_shape("vMDrawLine", x0, y0, x1, y1, color);
        vm_lcd_draw_line(x0, y0, x1, y1, color);
        int sx = x0 < x1 ? x0 : x1;
        int sy = y0 < y1 ? y0 : y1;
        int ex = x0 > x1 ? x0 : x1;
        int ey = y0 > y1 ? y0 : y1;
        vm_lcd_sync_cache_rect_to_vm(sx, sy, ex - sx + 1, ey - sy + 1);
        vm_set_call_result(1);
    }
    else if (idx == 16)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        int x0 = vm_lcd_coord_from_reg(tmp1);
        int y0 = vm_lcd_coord_from_reg(tmp1 >> 16);
        int x1 = vm_lcd_coord_from_reg(tmp2);
        int y1 = vm_lcd_coord_from_reg(tmp2 >> 16);
        u16 color = (u16)tmp3;
        vm_trace_lcd_shape("vMDrawLineEx", x0, y0, x1, y1, color);
        vm_lcd_draw_line(x0, y0, x1, y1, color);
        int sx = x0 < x1 ? x0 : x1;
        int sy = y0 < y1 ? y0 : y1;
        int ex = x0 > x1 ? x0 : x1;
        int ey = y0 > y1 ? y0 : y1;
        vm_lcd_sync_cache_rect_to_vm(sx, sy, ex - sx + 1, ey - sy + 1);
        vm_set_call_result(1);
    }
    else if (idx == 17)
    {
        u32 rectH = 0, rectColor = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);
        int x, y, w, h;
        if (vm_lcd_try_unpack_packed_rect(tmp1, tmp2, &x, &y, &w, &h))
        {
            rectColor = tmp3;
        }
        else
        {
            uc_mem_read(MTK, tmp5, &rectH, 4);
            uc_mem_read(MTK, tmp5 + 4, &rectColor, 4);
            x = vm_lcd_coord_from_reg(tmp1);
            y = vm_lcd_coord_from_reg(tmp2);
            w = vm_lcd_coord_from_reg(tmp3);
            h = (int)rectH;
        }
        u16 color = (u16)rectColor;
        vm_trace_lcd_shape("vMDrawRect", x, y, w, h, rectColor);
        if (w > 0 && h > 0)
        {
            if (x < 0)
            {
                w += x;
                x = 0;
            }
            if (y < 0)
            {
                h += y;
                y = 0;
            }
            if (x + w > LCD_WIDTH)
                w = LCD_WIDTH - x;
            if (y + h > LCD_HEIGHT)
                h = LCD_HEIGHT - y;
        }
        if (w > 0 && h > 0)
        {
            u16 *rowBuf = (u16 *)cbeTextString;
            for (int col = 0; col < w; col++)
                rowBuf[col] = color;
            u32 top = y * LCD_WIDTH + x;
            uc_mem_write(MTK, VM_screenImage_ADDRESS + top * 2, rowBuf, w * 2);
            for (int col = 0; col < w; col++)
                ((u16 *)Lcd_Cache_Buffer)[top + col] = color;
            if (h > 1)
            {
                u32 bottom = (y + h - 1) * LCD_WIDTH + x;
                uc_mem_write(MTK, VM_screenImage_ADDRESS + bottom * 2, rowBuf, w * 2);
                for (int col = 0; col < w; col++)
                    ((u16 *)Lcd_Cache_Buffer)[bottom + col] = color;
            }
            for (int row = 1; row < h - 1; row++)
            {
                u32 left = (y + row) * LCD_WIDTH + x;
                ((u16 *)Lcd_Cache_Buffer)[left] = color;
                uc_mem_write(MTK, VM_screenImage_ADDRESS + left * 2, &color, 2);
                if (w > 1)
                {
                    u32 right = left + w - 1;
                    ((u16 *)Lcd_Cache_Buffer)[right] = color;
                    uc_mem_write(MTK, VM_screenImage_ADDRESS + right * 2, &color, 2);
                }
            }
        }
        vm_set_call_result(1);
    }
    else if (idx == 18)
    {
        u32 dstImage, dstPixels, rectH, rectColor;
        u16 dstW, dstH;
        uc_reg_read(MTK, UC_ARM_REG_R0, &dstImage);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        uc_mem_read(MTK, tmp4, &rectH, 4);
        uc_mem_read(MTK, tmp4 + 4, &rectColor, 4);
        uc_mem_read(MTK, dstImage, &dstPixels, 4);
        uc_mem_read(MTK, dstImage + 4, &dstW, 2);
        uc_mem_read(MTK, dstImage + 6, &dstH, 2);
        if (dstImage == VM_screenImageStruct_ADDRESS || dstPixels == 0 || dstW == 0 || dstH == 0 || dstW > LCD_WIDTH || dstH > LCD_HEIGHT)
        {
            dstPixels = VM_screenImage_ADDRESS;
            dstW = LCD_WIDTH;
            dstH = LCD_HEIGHT;
        }
        int x = vm_lcd_coord_from_reg(tmp1);
        int y = vm_lcd_coord_from_reg(tmp2);
        int w = vm_lcd_coord_from_reg(tmp3);
        int h = (int)rectH;
        u16 color = (u16)rectColor;
        vm_trace_lcd_shape("vMDrawRectEx", x, y, w, h, rectColor);
        if (w > 0 && h > 0)
        {
            if (x < 0)
            {
                w += x;
                x = 0;
            }
            if (y < 0)
            {
                h += y;
                y = 0;
            }
            if (x + w > dstW)
                w = dstW - x;
            if (y + h > dstH)
                h = dstH - y;
        }
        if (w > 0 && h > 0)
        {
            u16 *rowBuf = (u16 *)cbeTextString;
            for (int col = 0; col < w; col++)
                rowBuf[col] = color;
            u32 top = y * dstW + x;
            uc_mem_write(MTK, dstPixels + top * 2, rowBuf, w * 2);
            if (dstPixels == VM_screenImage_ADDRESS && dstW == LCD_WIDTH)
                for (int col = 0; col < w; col++)
                    ((u16 *)Lcd_Cache_Buffer)[top + col] = color;
            if (h > 1)
            {
                u32 bottom = (y + h - 1) * dstW + x;
                uc_mem_write(MTK, dstPixels + bottom * 2, rowBuf, w * 2);
                if (dstPixels == VM_screenImage_ADDRESS && dstW == LCD_WIDTH)
                    for (int col = 0; col < w; col++)
                        ((u16 *)Lcd_Cache_Buffer)[bottom + col] = color;
            }
            for (int row = 1; row < h - 1; row++)
            {
                u32 left = (y + row) * dstW + x;
                uc_mem_write(MTK, dstPixels + left * 2, &color, 2);
                if (dstPixels == VM_screenImage_ADDRESS && dstW == LCD_WIDTH)
                    ((u16 *)Lcd_Cache_Buffer)[left] = color;
                if (w > 1)
                {
                    u32 right = left + w - 1;
                    uc_mem_write(MTK, dstPixels + right * 2, &color, 2);
                    if (dstPixels == VM_screenImage_ADDRESS && dstW == LCD_WIDTH)
                        ((u16 *)Lcd_Cache_Buffer)[right] = color;
                }
            }
        }
        vm_set_call_result(1);
    }
    else if (idx == 19)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);
        u32 fillH = 0, fillColor = 0;
        int x, y, w, h;
        if (vm_lcd_try_unpack_packed_rect(tmp1, tmp2, &x, &y, &w, &h))
        {
            fillColor = tmp3;
        }
        else
        {
            uc_mem_read(MTK, tmp5, &fillH, 4);
            uc_mem_read(MTK, tmp5 + 4, &fillColor, 4);
            x = vm_lcd_coord_from_reg(tmp1);
            y = vm_lcd_coord_from_reg(tmp2);
            w = vm_lcd_coord_from_reg(tmp3);
            h = (int)fillH;
        }
        vm_trace_lcd_shape("vMFillRect", x, y, w, h, fillColor);
        if (x < 0)
        {
            w += x;
            x = 0;
        }
        if (y < 0)
        {
            h += y;
            y = 0;
        }
        if (x + w > LCD_WIDTH)
            w = LCD_WIDTH - x;
        if (y + h > LCD_HEIGHT)
            h = LCD_HEIGHT - y;
        if (w > 0 && h > 0)
        {
            u16 color = (u16)fillColor;
            for (int row = 0; row < h; row++)
            {
                u32 off = (y + row) * LCD_WIDTH + x;
                for (int col = 0; col < w; col++)
                    ((u16 *)Lcd_Cache_Buffer)[off + col] = color;
                uc_mem_write(MTK, VM_screenImage_ADDRESS + off * 2, Lcd_Cache_Buffer + off * 2, w * 2);
            }
        }
        vm_set_call_result(1);
    }
    else if (idx == 20)
    {
        u32 dstImage, dstPixels, fillH, fillColor;
        u16 dstW, dstH;
        uc_reg_read(MTK, UC_ARM_REG_R0, &dstImage);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        uc_mem_read(MTK, tmp4, &fillH, 4);
        uc_mem_read(MTK, tmp4 + 4, &fillColor, 4);
        uc_mem_read(MTK, dstImage, &dstPixels, 4);
        uc_mem_read(MTK, dstImage + 4, &dstW, 2);
        uc_mem_read(MTK, dstImage + 6, &dstH, 2);
        if (dstImage == VM_screenImageStruct_ADDRESS || dstPixels == 0 || dstW == 0 || dstH == 0 || dstW > LCD_WIDTH || dstH > LCD_HEIGHT)
        {
            dstPixels = VM_screenImage_ADDRESS;
            dstW = LCD_WIDTH;
            dstH = LCD_HEIGHT;
        }
        int x = vm_lcd_coord_from_reg(tmp1);
        int y = vm_lcd_coord_from_reg(tmp2);
        int w = vm_lcd_coord_from_reg(tmp3);
        int h = (int)fillH;
        vm_trace_lcd_shape("vMFillRectEx", x, y, w, h, fillColor);
        if (x < 0)
        {
            w += x;
            x = 0;
        }
        if (y < 0)
        {
            h += y;
            y = 0;
        }
        if (x + w > dstW)
            w = dstW - x;
        if (y + h > dstH)
            h = dstH - y;
        if (w > 0 && h > 0)
        {
            u16 color = (u16)fillColor;
            u16 *rowBuf = (u16 *)cbeTextString;
            for (int row = 0; row < h; row++)
            {
                u32 off = (y + row) * dstW + x;
                for (int col = 0; col < w; col++)
                    rowBuf[col] = color;
                if (dstPixels == VM_screenImage_ADDRESS && dstW == LCD_WIDTH)
                {
                    for (int col = 0; col < w; col++)
                        ((u16 *)Lcd_Cache_Buffer)[off + col] = color;
                }
                uc_mem_write(MTK, dstPixels + off * 2, rowBuf, w * 2);
            }
        }
        vm_set_call_result(1);
    }
    else if (idx == 21)
    {
        printf("[call]vMFillRectWithImage\n");
        assert(0);
    }
    else if (idx == 22)
    {
        printf("[call]vMFillRectWithImageEx\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]vMCreateImage\n");
        assert(0);
    }
    else if (idx == 24)
    {
        printf("[call]vMCreateImageFromInRes\n");
        assert(0);
    }
    else if (idx == 25)
    {
        // vMDrawImageWithClipEx(p_mscreenImage ,ptr2 ,0:x? ,0:y? ,0xbc:x2 ,1:y2? ,0 ,3)
        // vMDrawImageWithClipEx(dst, src, sx, sy, w, h, dx, dy)原图的sx,sy，目标图的dx,dy
        vM_DrawImageWithClipEx();
    }
    else if (idx == 26)
    {
        vm_vMDrawImageClipAndAlphaEx();
    }
    else if (idx == 27)
    {
        printf("[call]vMDrawImage\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]vMDrawImageEx\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]vMDrawImageWithAlpha\n");
        assert(0);
    }
    else if (idx == 30)
    {
        printf("[call]vMDrawImageWithClip\n");
        assert(0);
    }
    else if (idx == 31)
    {
        printf("[call]vMDrawImageWithClip2\n");
        assert(0);
    }
    else if (idx == 32)
    {
        printf("[call]vMDrawImageClipAndAlpha\n");
        assert(0);
    }
    else if (idx == 33)
    {
        printf("[call]vMDrawImageClipAndAlpha2\n");
        assert(0);
    }
    else if (idx == 34)
    {
        printf("[call]vMGetImageWidth\n");
        assert(0);
    }
    else if (idx == 35)
    {
        printf("[call]vMGetImageHeight\n");
        assert(0);
    }
    else if (idx == 36)
    {
        printf("[call]vMDestoryImage\n");
        assert(0);
    }
    else if (idx == 37)
    {
        DEBUG_PRINT("[call]vMIsBacklightOn\n");
        tmp2 = 1;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 38)
    {
        DEBUG_PRINT("[call]vMCtrlBacklight\n");
        tmp2 = 1;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 39)
    {
        // DEBUG_PRINT("[call]vMGB2UCS2\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
        gbk_to_unicode(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
        tmp3 = strlen_utf16((u16 *)sprintfBuff);
        uc_mem_write(MTK, tmp2, sprintfBuff, (tmp3 + 1) * 2);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp3);
    }
    else if (idx == 40)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // src UCS2
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // dst GBK
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3); // dst max bytes
        if (tmp1 == 0 || tmp2 == 0 || tmp3 == 0 || tmp3 > 0xfff0)
        {
            vm_set_call_result(0);
        }
        else
        {
            u32 ucs2Len = vm_input_wcslen_limit(tmp1, 0x7ff8);
            u32 srcBytes = (ucs2Len + 1) * 2;
            if (srcBytes > mySizeOf(cbeTextString))
                srcBytes = mySizeOf(cbeTextString);
            uc_mem_read(MTK, tmp1, cbeTextString, srcBytes);

            u32 outLen = tmp3;
            if (outLen > mySizeOf(sprintfBuff))
                outLen = mySizeOf(sprintfBuff);
            int conv = ucs2_to_gbk(cbeTextString, srcBytes, sprintfBuff, outLen);
            if (conv < 0)
            {
                sprintfBuff[0] = 0;
                outLen = 1;
            }
            uc_mem_write(MTK, tmp2, sprintfBuff, outLen);
            vm_set_call_result(strlen((char *)sprintfBuff));
        }
    }
    else if (idx == 41)
    {
        vm_set_call_result(0);
    }
    else if (idx == 42)
    {
        printf("[call]vmResGetTxtWithDataPackage\n");
        assert(0);
    }
    else if (idx == 43)
    {
        printf("[call]vmResGetDefTxt\n");
        assert(0);
    }
    else if (idx == 44)
    {
        printf("[call]vmResGetTxtForGame\n");
        assert(0);
    }
    else if (idx == 45)
    {
        printf("[call]IMG_InitDataPage\n");
        assert(0);
    }
    else if (idx == 46)
    {
        printf("[call]IMG_InitInnerDataPageEx\n");
        assert(0);
    }
    else if (idx == 47)
    {
        printf("[call]IMG_ReleaseDataPage\n");
        assert(0);
    }
    else if (idx == 48)
    {
        printf("[call]IMG_InitDataPageEx\n");
        assert(0);
    }
    else if (idx == 49)
    {
        printf("[call]IMG_CreateImageFormIdEx\n");
        assert(0);
    }
    else if (idx == 50)
    {
        DEBUG_PRINT("[call]IMG_CreateImageFormStream\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_IMG_CreateImageFormStream(tmp1, tmp2);
    }
    else if (idx == 51)
    {
        printf("[call]vMDrawStandardImage\n");
        assert(0);
    }
    else if (idx == 52)
    {
        printf("[call]vMGetStandardImageDimension\n");
        assert(0);
    }
    else if (idx == 53)
    {
        printf("[call]vMGetStandardImageType\n");
        assert(0);
    }
    else if (idx == 54)
    {
        printf("[call]vMDrawStandardImageEx\n");
        assert(0);
    }
    else if (idx == 55)
    {
        vm_set_call_result(1);
    }
    else if (idx == 56)
    {
        vm_set_call_result(1);
    }
    else if (idx == 57)
    {
        vm_set_call_result(1);
    }
    else if (idx == 58)
    {
        printf("[call]IMG_InitDataPageTxt\n");
        assert(0);
    }
    else if (idx == 59)
    {
        assert(0);
        printf("[call]gddiAllocMemory\n");
    }
    else if (idx == 60)
    {
        printf("[call]gddiFreeMemory\n");
        assert(0);
    }
    else if (idx == 61)
    {
        printf("[call]gddiImageData\n");
        assert(0);
    }
    else if (idx == 62)
    {
        printf("[call]gddiRegImageCodecHandler\n");
        assert(0);
    }
    else if (idx == 63)
    {
        DEBUG_PRINT("[call]vMGetCharWidth\n");
        tmp1 = getFontWidth();
        vm_set_call_result(tmp1);
    }
    else if (idx == 64)
    {
        printf("[call]vMDrawUcs2String\n");
        assert(0);
    }
    else if (idx == 65)
    {
        printf("[call]vMDrawUcs2StringBorder\n");
        assert(0);
    }
    else if (idx == 66)
    {
        printf("[call]vMDrawUcs2StringEx\n");
        assert(0);
    }
    else if (idx == 67)
    {
        printf("[call]vMDrawUcs2StringClipAlignBorder\n");
        assert(0);
    }
    else if (idx == 68)
    {
        printf("[call]vMDrawUcs2StringClipAlign\n");
        assert(0);
    }
    else if (idx == 69)
    {
        printf("[call]vMDrawUcs2StringClipBorder\n");
        assert(0);
    }
    else if (idx == 70)
    {
        printf("[call]vMDrawUcs2StringClip\n");
        assert(0);
    }
    else if (idx == 71)
    {
        printf("[call]vMDrawUcs2StringRect\n");
        assert(0);
    }
    else if (idx == 72)
    {
        printf("[call]vMGetUcs2StringWidth\n");
        assert(0);
    }
    else if (idx == 73)
    {
        printf("[call]vMGetUcs2StringHeight\n");
        assert(0);
    }
    else if (idx == 74)
    {
        DEBUG_PRINT("[call]vMAllowBackLight\n");
        vm_set_call_result(0);
    }
    else if (idx == 75)
    {
        printf("[call]vM_CB_GetIsNeedRefreshLcd\n");
        assert(0);
    }
    else if (idx == 76)
    {
        printf("[call]vM_CB_SetIsNeedRefreshLcd\n");
        assert(0);
    }
    else if (idx == 77)
    {
        printf("[call]vM_CB_LCD_InvalidateRect_Enable\n");
        assert(0);
    }
    else if (idx == 78)
    {
        printf("[call]vM_CB_SetVideoIsNeedClosed\n");
        assert(0);
    }
    else if (idx == 79)
    {
        printf("[call]vM_CB_GetVideoIsNeedClosed\n");
        assert(0);
    }
    else if (idx == 80)
    {
        printf("[call]vMDrawUcs2StringRectEx\n");
        assert(0);
    }

    // result 区（从 idx=81 开始）
    else if (idx == 81)
    {
        printf("[call]vMSetCbeFontDataPtr\n");
        assert(0);
    }
    else if (idx == 82)
    {
        printf("[call]vMGetFontHeightEx\n");
        assert(0);
    }
    else if (idx == 83)
    {
        printf("[call]vMGetFontWidthEx\n");
        assert(0);
    }
    else if (idx == 84)
    {
        printf("[call]vMGetStringHeightEx\n");
        assert(0);
    }
    else if (idx == 85)
    {
        printf("[call]vMGetStringWidthEx\n");
        assert(0);
    }
    else if (idx == 86)
    {
        printf("[call]vMShowStringEx\n");
        assert(0);
    }
    else if (idx == 87)
    {
        printf("[call]vMShowString\n");
        assert(0);
    }
    else if (idx == 88)
    {
        printf("[call]vMShowStringClipAlign\n");
        assert(0);
    }
    else if (idx == 89)
    {
        printf("[call]vMShowStringClip\n");
        assert(0);
    }
    else if (idx == 90)
    {
        printf("[call]vMShowStringRect\n");
        assert(0);
    }
    else if (idx == 91)
    {
        printf("[call]vM_InvalidateLcdEx\n");
        assert(0);
    }
    else if (idx == 92)
    {
        vm_set_call_result(0);
    }
    else if (idx == 93)
    {
        vm_set_call_result(0);
    }
    else if (idx == 94)
    {
        printf("[call]vMUTF82UCS2\n");
        assert(0);
    }
    else if (idx == 95)
    {
        printf("[call]vMUCS2UTF8\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmLcdManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_fileio_func(u32 address)
{
    if (!(address >= VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_cbfs_vm_file_open(tmp1, tmp2, tmp3);
        DEBUG_PRINT("[call]cbfs_vm_file_open\n");
    }
    else if (idx == 2)
    {
        DEBUG_PRINT("[call]cbfs_vm_file_close\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_cbfs_vm_file_close(tmp1);
    }
    else if (idx == 3)
    {
        DEBUG_PRINT("[call]vm_file_exist\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_cbfs_vm_file_exists(tmp1, tmp2);
    }
    else if (idx == 4)
    {
        DEBUG_PRINT("[call]vm_file_direxist\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // 盘符,数字1,2,3
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_dir_exists(tmp2);
    }
    else if (idx == 5)
    {
        DEBUG_PRINT("[call]cbfs_vm_file_read\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_cbfs_vm_file_read(tmp1, tmp2, tmp3);
    }
    else if (idx == 6)
    {
        DEBUG_PRINT("[call]cbfs_vm_file_write\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_cbfs_vm_file_write(tmp1, tmp2, tmp3);
    }
    else if (idx == 7)
    {
        DEBUG_PRINT("[call]cbfs_vm_file_seek\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_cbfs_vm_file_seek(tmp1, tmp2, tmp3);
    }
    else if (idx == 8)
    {
        DEBUG_PRINT("[call]cbfs_vm_file_tell\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_cbfs_vm_file_tell(tmp1);
    }
    else if (idx == 9)
    {
        DEBUG_PRINT("[call]cbfs_vm_file_getfilesize\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_cbfs_vm_file_getfilesize(tmp1);
    }
    else if (idx == 10)
    {
        DEBUG_PRINT("[call]cbfs_vm_file_delete\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_cbfs_vm_file_delete(tmp1, tmp2);
    }
    else if (idx == 11)
    {
        printf("[call]cbfs_vm_file_rename\n");
        assert(0);
    }
    else if (idx == 12)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_set_call_result(0);
    }
    else if (idx == 13)
    {
        printf("[call]cbfs_vm_file_rmdir\n");
        assert(0);
    }
    else if (idx == 14)
    {
        printf("[call]cbfs_vm_find_first\n");
        assert(0);
    }
    else if (idx == 15)
    {
        printf("[call]cbfs_vm_find_next\n");
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]cbfs_vm_find_close\n");
        assert(0);
    }
    else if (idx == 17)
    {
        printf("[call]vm_get_freespace\n");
        assert(0);
    }
    else if (idx == 18)
    {
        printf("[call]vm_get_sdcardStatus\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]vm_GetFilenameFromPath\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]vm_file_getMp3Dir\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]vm_file_getMp4Dir\n");
        assert(0);
    }
    else if (idx == 22)
    {
        printf("[call]vm_file_getPicDir\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]vm_file_getBookDir\n");
        assert(0);
    }
    else if (idx == 24)
    {
        vm_set_call_result(0);
    }
    else if (idx == 25)
    {
        vm_set_call_result(0);
    }
    else if (idx == 26)
    {
        printf("[call]vm_file_getSysDir\n");
        assert(0);
    }
    else if (idx == 27)
    {
        printf("[call]vm_fmgr_select_entry\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]vm_get_freespace_ex\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]vm_get_sdcardStatusEx\n");
        assert(0);
    }
    else if (idx == 30)
    {
        printf("[call]vm_get_fullname\n");
        assert(0);
    }

    else
    {
        printf("[impl]vmFileIoManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_stdio_func(u32 address)
{
    if (!(address >= VM_MANAGER_STDIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_STDIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_STDIO_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp1 && tmp2 && tmp3)
            vm_memcpy(tmp1, tmp2, tmp3);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 2)
    {
        vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
        tmp1 = strlen(cbeTextString);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        DEBUG_PRINT("[call]strlen\n");
    }
    else if (idx == 3)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp1 && tmp3)
        {
            u8 fill[256];
            memset(fill, tmp2 & 0xff, sizeof(fill));
            for (u32 off = 0; off < tmp3; off += sizeof(fill))
                uc_mem_write(MTK, tmp1 + off, fill, SDL_min((u32)sizeof(fill), tmp3 - off));
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        DEBUG_PRINT("[call]sprintf(%x,%x,%x)\n", tmp1, tmp2, tmp3);
        vm_sprintf_return_buffer();
    }
    else if (idx == 5)
    {
        printf("[call]vm_log_trace\n");
        assert(0);
    }
    else if (idx == 6)
    {
        DEBUG_PRINT("[call]VmGetRand\n");
        tmp1 = currentTime;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 7)
    {
        printf("[call]vsprintf\n");
        assert(0);
    }
    else if (idx == 8)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp1 && tmp2 && tmp3)
        {
            u8 ch = 0;
            u32 i = 0;
            for (; i < tmp3; ++i)
            {
                uc_mem_read(MTK, tmp2 + i, &ch, 1);
                uc_mem_write(MTK, tmp1 + i, &ch, 1);
                if (ch == 0)
                    break;
            }
            if (i < tmp3)
            {
                u8 zero[64] = {0};
                for (++i; i < tmp3; i += sizeof(zero))
                    uc_mem_write(MTK, tmp1 + i, zero, SDL_min((u32)sizeof(zero), tmp3 - i));
            }
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 9)
    {
        DEBUG_PRINT("[call]strcpy\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_strcpy(tmp1, tmp2);
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (tmp1 && tmp2)
        {
            int dstLen = vm_strlen(tmp1);
            vm_readStringByPtr(tmp2, cbeTextString);
            uc_mem_write(MTK, tmp1 + dstLen, cbeTextString, strlen(cbeTextString) + 1);
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 11)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_readStringByPtr(tmp1, cbeTextString);
        tmp1 = (u32)strtol((char *)cbeTextString, NULL, 10);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 12)
    {
        printf("[call]memmove\n");
        assert(0);
    }
    else if (idx == 13)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_readStringByPtr(tmp1, cbeTextString);
        tmp1 = (u32)atoi((char *)cbeTextString);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 14)
    {
        printf("[call]vMpow\n");
        assert(0);
    }
    else if (idx == 15)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        u8 strA[1024] = {0};
        u8 strB[1024] = {0};
        if (tmp1)
            vm_readStringByPtr(tmp1, strA);
        if (tmp2)
            vm_readStringByPtr(tmp2, strB);
        tmp1 = (u32)strcmp((char *)strA, (char *)strB);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 16)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        int cmp = 0;
        for (u32 i = 0; i < tmp3; ++i)
        {
            u8 a = 0, b = 0;
            uc_mem_read(MTK, tmp1 + i, &a, 1);
            uc_mem_read(MTK, tmp2 + i, &b, 1);
            if (a != b)
            {
                cmp = (int)a - (int)b;
                break;
            }
        }
        tmp1 = (u32)cmp;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 17)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        int cmp = 0;
        for (u32 i = 0; i < tmp3; ++i)
        {
            u8 a = 0, b = 0;
            uc_mem_read(MTK, tmp1 + i, &a, 1);
            uc_mem_read(MTK, tmp2 + i, &b, 1);
            if (a != b || a == 0 || b == 0)
            {
                cmp = (int)a - (int)b;
                break;
            }
        }
        tmp1 = (u32)cmp;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 18)
    {
        printf("[call]setjmp\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]longjmp\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]atof\n");
        assert(0);
    }
    else if (idx == 21)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_readStringByPtr(tmp1, cbeTextString);
        vm_readStringByPtr(tmp2, sprintfBuff);
        tmp3 = strcasecmp((char *)cbeTextString, (char *)sprintfBuff);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp3);
    }
    else if (idx == 22)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_readStringByPtr(tmp1, cbeTextString);
        vm_readStringByPtr(tmp2, sprintfBuff);
        tmp4 = strncasecmp((char *)cbeTextString, (char *)sprintfBuff, tmp3);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp4);
    }
    else
    {
        printf("[impl]vmStdIoManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_timer_func(u32 address)
{
    if (!(address >= VM_MANAGER_TIMER_FUNC_LIST_ADDRESS && address < (VM_MANAGER_TIMER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_TIMER_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        scheduler_start_timer(tmp1, tmp2, tmp3);
    }
    else if (idx == 1)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        scheduler_stop_timer(tmp1);
    }
    else if (idx == 2)
    {
        tmp1 = scheduler_get_tick_ms();
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 3)
    {
        tmp1 = (u32)time(NULL);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 4)
    {
        tmp1 = (u32)time(NULL);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 5)
    {
        vm_set_call_result(0);
    }
    else if (idx == 6)
    {
        printf("[call]VmSetSysTime\n");
        assert(0);
    }
    else if (idx == 7)
    {
        printf("[call]VmSetSysDate\n");
        assert(0);
    }
    else if (idx == 8)
    {
        printf("[call]vMIncreaseTime\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]vMDecreaseTime\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmTimerManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_ctrl_func(u32 address)
{
    if (!(address >= VM_MANAGER_CTRL_FUNC_LIST_ADDRESS && address < (VM_MANAGER_CTRL_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_CTRL_FUNC_LIST_ADDRESS) / 4;

    {
        printf("[impl]vmCtrlManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_network_func(u32 address)
{
    if (!(address >= VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS && address < (VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;
    u32 netR4 = 0, netR5 = 0, netR6 = 0, netR7 = 0, netLr = 0;

    u32 idx = (address - VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS) / 4;
    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
    uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
    uc_reg_read(MTK, UC_ARM_REG_R4, &netR4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &netR5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &netR6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &netR7);
    uc_reg_read(MTK, UC_ARM_REG_LR, &netLr);
    DEBUG_PRINT("[probe_net_idx] idx=%u r0=%x r1=%x r2=%x r3=%x last=%x\n", idx, tmp1, tmp2, tmp3, tmp4, lastAddress);
    vm_net_trace("net_idx idx=%u r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x lr=%08x last=%08x\n",
                 idx, tmp1, tmp2, tmp3, tmp4, netR4, netR5, netR6, netR7, netLr, lastAddress);
    if (idx == 0)
    {
        g_netCurrentObject = netR4;
        tmp5 = g_nextNetConnectId++;
        if (tmp5 == 0)
            tmp5 = g_nextNetConnectId++;
        if (tmp4)
            uc_mem_write(MTK, tmp4, &tmp5, 4);
        scheduler_register_net_channel(tmp5, tmp3, tmp4);
        vm_net_trace("open_channel host=%08x type=%u cb=%08x ctx=%08x connect=%u last=%08x\n", tmp1, tmp2, tmp3, tmp4, tmp5, lastAddress);
        u8 netState = 1;
        uc_mem_write(MTK, Global_R9 + 0x9588 + 0x0c, &netState, 1);
        scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
        vm_set_call_result(1);
    }
    else if (idx == 1)
    {
        if (netR4)
            g_netCurrentObject = netR4;
        vm_net_trace("send_call connect=%u len=%u data=%08x r3=%08x last=%08x\n", tmp1, tmp2, tmp3, tmp4, lastAddress);
        vm_net_mock_on_send(tmp1, tmp3, tmp2);
        vm_set_call_result(tmp2);
    }
    else if (idx == 2)
    {
        vm_net_trace("close_channel connect=%u last=%08x\n", tmp1, lastAddress);
        scheduler_unregister_net_channel(tmp1);
        u8 netState = 0;
        uc_mem_write(MTK, Global_R9 + 0x9588 + 0x0c, &netState, 1);
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        tmp1 = vm_get_var(Global_R9 + 0x5a3c + 0x10);
        if (tmp1)
        {
            tmp2 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
            vm_bx(tmp1);
            return;
        }
        vm_set_call_result(0);
    }
    else if (idx == 6 || idx == 7 || idx == 18)
    {
        vm_net_trace("open_channel_ex idx=%u r0=%08x r1=%08x cb=%08x ctx=%08x last=%08x\n", idx, tmp1, tmp2, tmp3, tmp4, lastAddress);
        scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
        vm_set_call_result(1);
    }
    else if (idx == 17)
    {
        vm_net_trace("http_get_ex r0=%08x cb=%08x ctx=%08x last=%08x\n", tmp1, tmp2, tmp3, lastAddress);
        scheduler_queue_net_task(tmp1, 0, tmp2, tmp3);
        vm_set_call_result(1);
    }
    else if (idx == 4 || idx == 19 || idx == 20 || idx == 29 || idx == 30)
    {
        vm_net_trace("net_success_stub idx=%u r0=%08x r1=%08x r2=%08x r3=%08x last=%08x\n", idx, tmp1, tmp2, tmp3, tmp4, lastAddress);
        vm_set_call_result(1);
    }
    else if (idx == 35)
    {
        g_netUpLinkData = 0;
        g_netDownLinkData = 0;
        g_netMockResponseOffset = 0;
        vm_net_trace("net_data_reset\n");
        vm_set_call_result(0);
    }
    else if (idx == 36)
    {
        if (tmp1)
            uc_mem_write(MTK, tmp1, &g_netUpLinkData, 4);
        if (tmp2)
            uc_mem_write(MTK, tmp2, &g_netDownLinkData, 4);
        vm_net_trace("net_get_data up=%u down=%u upPtr=%08x downPtr=%08x\n", g_netUpLinkData, g_netDownLinkData, tmp1, tmp2);
        vm_set_call_result(g_netDownLinkData);
    }
    else if (idx == 5 || idx == 12 || idx == 13 || idx == 21 || idx == 24 || idx == 25 || idx == 33 || idx == 34 || idx == 37 || idx == 39 || idx == 41 || idx == 42)
    {
        vm_set_call_result(0);
    }
    else if (idx == 8 || idx == 9 || idx == 10 || idx == 11 || idx == 14 || idx == 15 || idx == 16 || idx == 22 || idx == 23 || idx == 26 || idx == 27 || idx == 28 || idx == 31 || idx == 32 || idx == 38 || idx == 40)
    {
        vm_set_call_result(0);
    }
    else
    {
        printf("[impl]vmNetWorkManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_game_util_func(u32 address)
{
    if (!(address >= VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS && address < (VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 2)
    {
        printf("[call]CdRectPoint\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]Sqrt\n");
        assert(0);
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_mem_write(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
        vm_set_call_result(0);
    }
    else if (idx == 11)
    {
        uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 12)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_resource_by_id(tmp1);
    }
    else if (idx == 13)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_resource_by_file_name(tmp1);
    }
    else if (idx == 14)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_resource_name_by_id(tmp1);
    }
    else if (idx == 15)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        tmp1 = vm_df_get_resource_id_by_file_name(tmp1);
    }
    else if (idx == 16)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_t_resource(tmp1, 0);
    }
    else if (idx == 17)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_t_resource(tmp1, 1);
    }
    else if (idx == 18)
    {
        printf("[call]DF_DataPackage_ShowFileList\n");
        assert(0);
    }
    else if (idx == 19)
    {
        DEBUG_PRINT("[call]DF_String_Equal\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_String_Equal(tmp1, tmp2);
    }
    else if (idx == 20)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        tmp1 = vm_DF_ReadShort(tmp1, tmp2);
        DEBUG_PRINT("[call]DF_ReadShort(%x)\n", tmp1);
    }
    else if (idx == 21)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        tmp1 = vm_DF_ReadInt(tmp1, tmp2);
        DEBUG_PRINT("[call]DF_ReadInt(%x)\n", tmp1);
    }
    else if (idx == 22)
    {
        printf("[call]DF_File_ReadShort\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]DF_File_ReadInt\n");
        assert(0);
    }
    else if (idx == 24)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_DF_WriteShort(tmp1, tmp2, tmp3);
        DEBUG_PRINT("[call]DF_WriteShort\n");
    }
    else if (idx == 25)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_DF_WriteInt(tmp1, tmp2, tmp3);
        DEBUG_PRINT("[call]DF_WriteInt\n");
    }
    else if (idx == 26)
    {
        DEBUG_PRINT("[call]DF_ReadString\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_ReadString(tmp1, tmp2);
    }
    else if (idx == 27)
    {
        printf("[call]DF_ReadStringEx\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]DF_File_ReadString\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]DF_File_ReadToBuffer\n");
        assert(0);
    }
    else if (idx == 30)
    {
        printf("[call]DF_ReadString2\n");
        assert(0);
    }
    else if (idx == 31)
    {
        DEBUG_PRINT("[call]DF_GetMemoryBlock\n");
        vm_DF_GetMemoryBlock();
    }
    else if (idx == 32)
    {
        DEBUG_PRINT("[call]DF_Sin\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_DF_Sin(tmp1);
    }

    // result 区
    else if (idx == 33)
    {
        DEBUG_PRINT("[call]DF_Cos\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_DF_Sin(tmp1 + 90);
    }
    else if (idx == 34)
    {
        printf("[call]DF_Degree\n");
        assert(0);
    }
    else if (idx == 35)
    {
        printf("[call]DF_CollectionTest\n");
        assert(0);
    }
    else if (idx == 36)
    {
        printf("[call]DF_SwapVal\n");
        assert(0);
    }
    else if (idx == 37)
    {
        DEBUG_PRINT("[call]DF_GetFormatString\n");
        vm_DF_GetFormatString();
    }
    else if (idx == 38)
    {
        DEBUG_PRINT("[call]Storage_Date\n");
        // todo
        tmp1 = currentTime;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 39)
    {
        printf("[call]vMstricmp\n");
        assert(0);
    }
    else if (idx == 40)
    {
        printf("[call]vMstrnicmp\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmGameUtilManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_df_engine_func(u32 address)
{
    if (!(address >= VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS && address < (VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS) / 4;
    if (idx == 8)
    {
        tmp1 = 0;
        uc_mem_write(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
        tmp1 = VM_MemoryBlock_PTR_ADDRESS;
        uc_mem_write(MTK, VM_DreamFactory_MemoryBlock_ADDRESS, &tmp1, 4);
        vm_set_call_result(0);
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_initDFDataPackage(tmp1, tmp2);
    }
    else
    {
        printf("[impl]vmDfEngineManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_billing_func(u32 address)
{
    if (!(address >= VM_MANAGER_BILLING_FUNC_LIST_ADDRESS && address < (VM_MANAGER_BILLING_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_BILLING_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 2)
    {
        printf("[call]BILLING_GetRemainDay\n");
        assert(0);
    }
    else if (idx == 3)
    {
        printf("[call]BILLING_Pay\n");
        assert(0);
    }
    else if (idx == 4)
    {
        printf("[call]BILLING_PayMoreTimes\n");
        assert(0);
    }
    else if (idx == 5)
    {
        printf("[call]BILLING_IsRegisterBillingInfo\n");
        assert(0);
    }
    else if (idx == 6)
    {
        printf("[call]BILLING_RegisterBillingInfo\n");
        assert(0);
    }
    else if (idx == 7)
    {
        printf("[call]BILLING_SetBillingStatus\n");
        assert(0);
    }
    else if (idx == 8)
    {
        printf("[call]BILLING_GetBillingStatus\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]BILLING_IsNeedPay\n");
        assert(0);
    }
    else if (idx == 10)
    {
        printf("[call]BILLING_IsInTryStatus\n");
        assert(0);
    }
    else if (idx == 11)
    {
        printf("[call]BIllING_OpenBillingPromptWin\n");
        assert(0);
    }
    else if (idx == 12)
    {
        printf("[call]CDownGetTryDay\n");
        assert(0);
    }
    else if (idx == 13)
    {
        printf("[call]BILLING_PayForCBB\n");
        assert(0);
    }
    else if (idx == 14)
    {
        printf("[call]BILLING_PayForPwd\n");
        assert(0);
    }
    else if (idx == 15)
    {
        printf("[call]CDownGetOption5\n");
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]Billing_SendSpecSms\n");
        assert(0);
    }
    else if (idx == 17)
    {
        printf("[call]Billing_CancelSms\n");
        assert(0);
    }
    else if (idx == 18)
    {
        printf("[call]CDownIsMonthApp\n");
        assert(0);
    }
    else if (idx == 19)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        if (!vm_host_file_exists("Wpay9990Ker42WqvgaV100.CBM"))
        {
            if (tmp2)
                vm_set_var(tmp2, 0);
            vm_net_trace("CDownGetFileNameByAppID appid=%u missing Wpay9990Ker42WqvgaV100.CBM -> 0\n", tmp1);
            vm_set_call_result(0);
        }
        else
        {
            u32 namePtr = vm_alloc_host_string("Wpay9990Ker42Wqvga");
            u8 type = 0;
            u16 version = 100;
            if (tmp2)
                uc_mem_write(MTK, tmp2, &namePtr, 4);
            if (tmp3)
                uc_mem_write(MTK, tmp3, &type, 1);
            if (tmp4)
                uc_mem_write(MTK, tmp4, &version, 2);
            vm_net_trace("CDownGetFileNameByAppID appid=%u nameOut=%08x typeOut=%08x verOut=%08x name=%08x version=%u\n", tmp1, tmp2, tmp3, tmp4, namePtr, version);
            vm_set_call_result(1);
        }
    }
    else if (idx == 20)
    {
        printf("[call]CDownGetPayTimes\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]BILLING_NewMonthPay\n");
        assert(0);
    }
    else if (idx == 22)
    {
        printf("[call]BILLING_NewMonthCancel\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]BILLING_CleanAppMonthBillInfo\n");
        assert(0);
    }
    else if (idx == 24)
    {
        printf("[call]BILLING_GetValidDayByAppId\n");
        assert(0);
    }
    else if (idx == 25)
    {
        printf("[call]CDownGetBillSmsAddr\n");
        assert(0);
    }
    else if (idx == 26)
    {
        printf("[call]CDownGetBillSmsSuf\n");
        assert(0);
    }
    else if (idx == 27)
    {
        printf("[call]BILLING_Pay2\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]Billing_GetAppUsedStatus\n");
        assert(0);
    }
    else if (idx == 29)
    {
        assert(0);
        printf("[call]Billing_SetAppUsedStatus\n");
    }
    else if (idx == 30)
    {
        printf("[call]CDownGetPayTipContent\n");
        assert(0);
    }
    else if (idx == 31)
    {
        printf("[call]BILLING_Pay3\n");
        assert(0);
    }
    else if (idx == 32)
    {
        printf("[call]BILLING_SendRegisterSms\n");
        assert(0);
    }

    // result 区
    else if (idx == 33)
    {
        printf("[call]CDownGetOption8\n");
        assert(0);
    }
    else if (idx == 34)
    {
        printf("[call]CDownGetOption9\n");
        assert(0);
    }
    else if (idx == 35)
    {
        printf("[call]CDownGetOption10\n");
        assert(0);
    }
    else if (idx == 36)
    {
        printf("[call]BILLING_Register\n");
        assert(0);
    }
    else if (idx == 37)
    {
        printf("[call]BILLING_PayForCBB3\n");
        assert(0);
    }
    else if (idx == 38)
    {
        printf("[call]CDownIsWPay\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmBillingManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_ucs2_func(u32 address)
{
    if (!(address >= VM_MANAGER_UCS2_FUNC_LIST_ADDRESS && address < (VM_MANAGER_UCS2_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_UCS2_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        vm_readStringUCS2ByReg(UC_ARM_REG_R0, cbeTextString);
        tmp1 = strlen_utf16(cbeTextString);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 2)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // dst
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // src
        vm_readStringUCS2ByReg(UC_ARM_REG_R1, cbeTextString);
        uc_mem_write(MTK, tmp1, cbeTextString, (strlen_utf16(cbeTextString) + 1) * 2);
        vm_storage_trace("ucs2_strcpy dst=%08x src=%08x chars=%u lr=%08x\n", tmp1, tmp2, strlen_utf16(cbeTextString), lastAddress);
        vm_set_call_result(tmp1);
    }
    else if (idx == 3)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // dst
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // src
        u32 dstChars = 0;
        u16 ch = 0;
        if (tmp1 && tmp2)
        {
            while (dstChars < 512)
            {
                uc_mem_read(MTK, tmp1 + dstChars * 2, &ch, 2);
                if (ch == 0)
                    break;
                dstChars++;
            }
            vm_readStringUCS2ByReg(UC_ARM_REG_R1, cbeTextString);
            u32 srcChars = strlen_utf16((u16 *)cbeTextString);
            uc_mem_write(MTK, tmp1 + dstChars * 2, cbeTextString, (srcChars + 1) * 2);
            vm_storage_trace("ucs2_strcat dst=%08x src=%08x dstChars=%u srcChars=%u lr=%08x\n", tmp1, tmp2, dstChars, srcChars, lastAddress);
        }
        vm_set_call_result(tmp1);
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_readStringByPtr(tmp2, cbeTextString);
        vm_storage_trace("gbk_to_ucs2 dst=%08x src=%08x text=%s lr=%08x\n", tmp1, tmp2, cbeTextString, lastAddress);
        gbk_to_unicode(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
        tmp3 = strlen_utf16((u16 *)sprintfBuff);
        uc_mem_write(MTK, tmp1, sprintfBuff, (tmp3 + 1) * 2);
        vm_set_call_result(tmp1);
    }
    else if (idx == 5)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // dst
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // src
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3); // char count
        if (tmp1 && tmp2 && tmp3)
        {
            u32 i = 0;
            u16 ch = 0;
            for (; i < tmp3; ++i)
            {
                uc_mem_read(MTK, tmp2 + i * 2, &ch, 2);
                uc_mem_write(MTK, tmp1 + i * 2, &ch, 2);
                if (ch == 0)
                    break;
            }
            if (i < tmp3)
            {
                ch = 0;
                for (++i; i < tmp3; ++i)
                    uc_mem_write(MTK, tmp1 + i * 2, &ch, 2);
            }
        }
        vm_storage_trace("ucs2_strncpy dst=%08x src=%08x count=%u lr=%08x\n", tmp1, tmp2, tmp3, lastAddress);
        vm_set_call_result(tmp1);
    }
    else if (idx == 6)
    {
        assert(0);
        printf("[call]vmutExpandStrncpy\n");
    }
    else if (idx == 7)
    {
        printf("[call]vmutExpandMemcpy\n");
        assert(0);
    }
    else if (idx == 8)
    {
        printf("[call]vmutStrchrUcs2\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]vmutStrcmpUcs2\n");
        assert(0);
    }
    else if (idx == 10)
    {
        printf("[call]vmutStricmpUcs2\n");
        assert(0);
    }
    else if (idx == 11)
    {
        printf("[call]vmutStrncmpUcs2\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmUCS2StrManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_screen_func(u32 address)
{
    if (!(address >= VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS && address < (VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_net_trace("screen_manager idx=0 change r0=%08x last=%08x\n", tmp1, lastAddress);
        uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
        tmp2 = 0;
        uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
        screenStructChange = 1;
        g_screenRemovedWithoutNext = 0;
        vm_set_call_result(VM_SCREEN_isInQuit_ADDRESS);
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vmAddedScreen = tmp1;
        vm_screen_stack_push(tmp1);
        vm_net_trace("screen_manager idx=4 add r0=%08x depth=%u last=%08x\n", tmp1, g_screenStackCount, lastAddress);
        if (lastAddress >= VM_Memory_Pool_ADDRESS && lastAddress < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
        {
            u32 moduleBase = 0;
            uc_reg_read(MTK, UC_ARM_REG_R9, &moduleBase);
            if (moduleBase)
            {
                g_currentScreenThis = tmp1 - 0x18;
                g_currentScreenModuleBase = moduleBase;
                vm_net_trace("screen_manager idx=4 module_context this=%08x r9=%08x last=%08x\n",
                             g_currentScreenThis, g_currentScreenModuleBase, lastAddress);
            }
        }
        u32 startupObj = 0;
        uc_mem_read(MTK, Global_R9 + 0x9928 + 0x10, &startupObj, 4);
        if (startupObj == 0 && tmp1 != 0 && g_lastStartupScreenState != 0xff)
        {
            /* Firmware's screen manager maintains a focused screen stack.  The
             * emulator still has a single active screen slot, so after startup
             * vmAddScreen promotes the newly added screen to the active slot.
             */
            tmp2 = 0;
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
            vm_net_trace("screen_manager idx=4 promote_to_change screen=%08x last=%08x\n", tmp1, lastAddress);
        }
        vm_set_call_result(0);
    }
    else if (idx == 5)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        /*
         * The game passes both screen table pointers and owner/object pointers
         * here.  Treating it as an exact stack contains check made valid screens
         * look absent (for example table+0xe8 during login).  Firmware accepts
         * this as a successful screen-manager query, so keep the permissive
         * result and use the stack only as debug state.
         */
        vm_net_trace("screen_manager idx=5 query r0=%08x r1=%08x ret=1 depth=%u last=%08x\n", tmp1, tmp2, g_screenStackCount, lastAddress);
        vm_set_call_result(1);
    }
    else if (idx == 6)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        tmp3 = 0;
        tmp4 = vm_screen_stack_remove(tmp1, &tmp3) ? 1 : 0;
        vm_net_trace("screen_manager idx=6 remove r0=%08x r1=%08x ret=%u newTop=%08x depth=%u last=%08x\n",
                     tmp1, tmp2, tmp4, tmp3, g_screenStackCount, lastAddress);
        if (tmp4 && tmp1 == vmAddedScreen && tmp3)
        {
            g_activeScreenRemovedThisFrame = 1;
            g_screenResumeExisting = 1;
            vmAddedScreen = tmp3;
            tmp5 = 0;
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp3, 4);
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp5, 4);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
            vm_net_trace("screen_manager idx=6 promote_current_remove screen=%08x depth=%u resume=%u last=%08x\n",
                         tmp3, g_screenStackCount, g_screenResumeExisting, lastAddress);
        }
        else if (tmp4 && tmp1 == vmAddedScreen)
        {
            g_activeScreenRemovedThisFrame = 1;
            vmAddedScreen = 0;
            g_screenRemovedWithoutNext = 1;
            vm_net_trace("screen_manager idx=6 active_removed_wait depth=%u last=%08x\n",
                         g_screenStackCount, lastAddress);
        }
        vm_set_call_result(tmp4);
    }
    else
    {
        printf("[impl]vmScreenManager调用位置:%d\n", idx);
        assert(0);
    }
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_df_script_func(u32 address)
{
    if (!(address >= VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS && address < (VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else
    {
        printf("[impl]vmDfScriptManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_game_lcd_func(u32 address)
{
    if (!(address >= VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS && address < (VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        printf("[call]IMG_CreateImageFormRes\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_IMG_CreateImageFormRes(tmp1);
    }
    else if (idx == 11)
    {
        DEBUG_PRINT("[call]IMG_Destory\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_IMG_Destory(tmp1);
    }
    else if (idx == 20)
    {
        DEBUG_PRINT("[call]GetStreamDataFormRes\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_GetStreamDataFormRes(tmp1, tmp2, tmp3, tmp4);
    }
    else
    {
        printf("[impl]vmGameLcdManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_netapp_func(u32 address)
{
    if (!(address >= VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS && address < (VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS) / 4;
    if (idx == 60)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        tmp4 = 0;
        if (tmp1)
            uc_mem_write(MTK, tmp1, &tmp4, 2);
        if (tmp2)
            uc_mem_write(MTK, tmp2, &tmp4, 2);
        if (tmp3)
            uc_mem_write(MTK, tmp3, &tmp4, 1);
        vm_set_call_result(0);
    }
    else
    {
        printf("[impl]vmNetAppManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_audio_func(u32 address)
{
    if (!(address >= VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        DEBUG_PRINT("[call]vMAudioSetVolume\n");
        // void方法
    }
    else if (idx == 2)
    {
        printf("[call]vMAudioPlayByData\n");
        assert(0);
    }
    else if (idx == 3)
    {
        printf("[call]vMAudioPlayWithDataPackage\n");
        assert(0);
    }
    else if (idx == 4)
    {
        DEBUG_PRINT("[call]vMAudioPlayForGame(a1,a2)\n");
        tmp1 = 0; // pasue stop playing
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 5)
    {
        printf("[call]vMAudioPlayForApp\n");
        assert(0);
    }
    else if (idx == 6)
    {
        printf("[call]vMAudioPause\n");
        assert(0);
    }
    else if (idx == 7)
    {
        printf("[call]vMAudioResume\n");
        assert(0);
    }
    else if (idx == 8)
    {
        // todo
        DEBUG_PRINT("[call]vMAudioStop\n");
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 9)
    {
        // todo
        //  printf("[call]vMAduioGetState\n");
        tmp1 = 1; // pasue stop playing
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 10)
    {
        printf("[call]vm_mp3PlayBystream\n");
        assert(0);
    }
    else if (idx == 11)
    {
        printf("[call]vm_mp3PauseByStream\n");
        assert(0);
    }
    else if (idx == 12)
    {
        printf("[call]vm_mp3ResumeByStream\n");
        assert(0);
    }
    else if (idx == 13)
    {
        printf("[call]vm_mp3StopBystream\n");
        assert(0);
    }
    else if (idx == 14)
    {
        printf("[call]vm_mp3PlayByFile\n");
        assert(0);
    }
    else if (idx == 15)
    {
        printf("[call]vm_mp3PauseByFile\n");
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]vm_mp3ResumeByFile\n");
        assert(0);
    }
    else if (idx == 17)
    {
        printf("[call]vm_mp3StopByFile\n");
        assert(0);
    }
    else if (idx == 18)
    {
        printf("[call]vMAudioget_progress_time\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]vmMp3StreamInit\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]CB_AUD_StartPlay_Init\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]CB_AUD_StopPlay\n");
        assert(0);
    }
    else if (idx == 22)
    {
        printf("[call]CB_AUD_WriteVoiceData\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]vMStartAudioRecord_async\n");
        assert(0);
    }
    else if (idx == 24)
    {
        printf("[call]vMStopAudioRecord_async\n");
        assert(0);
    }
    else if (idx == 25)
    {
        printf("[call]vMSetAmrRecBS\n");
        assert(0);
    }
    else if (idx == 26)
    {
        printf("[call]vm_mp3PlayByFileEx\n");
        assert(0);
    }
    else if (idx == 27)
    {
        printf("[call]vMStartAudioRecordEx\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]vMStopAudioRecordEx\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]CB_AUD_StartPlay_InitEx\n");
        assert(0);
    }
    else if (idx == 30)
    {
        printf("[call]CB_AUD_StartPlayEx\n");
        assert(0);
    }
    else if (idx == 31)
    {
        printf("[call]CB_AUD_StopPlayEx\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmAudioManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_sensor_func(u32 address)
{
    if (!(address >= VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS && address < (VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS) / 4;
    if (1)
    {
        printf("[impl]vmSensorManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_vmim_func(u32 address)
{
    if (!(address >= VM_MANAGER_VMIM_FUNC_LIST_ADDRESS && address < (VM_MANAGER_VMIM_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_VMIM_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        printf("[call]vmDlFuncSms\n");
        assert(0);
    }
    else if (idx == 1)
    {
        printf("[call]vmDlFuncMakeCall\n");
        assert(0);
    }
    else if (idx == 2)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_nv_read(tmp1);
    }
    else if (idx == 3)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_nv_write(tmp1);
    }
    else if (idx == 4)
    {
        printf("[call]vmDlFuncReleaseCall\n");
        assert(0);
    }
    else if (idx == 5)
    {
        printf("[call]vmDlFuncMakeCallEx\n");
        assert(0);
    }
    else if (idx == 6)
    {
        printf("[call]vmDlFuncGetApsManager\n");
        tmp1 = VM_MANAGER_APPSTORE_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }

    else
    {
        printf("[impl]vmVmImManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_manager_gameold_func(u32 address)
{
    if (!(address >= VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS && address < (VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        printf("[call]IMG_CreateImageFormRes\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_IMG_CreateImageFormRes(tmp1);
    }
    else if (idx == 11)
    {
        printf("[call]IMG_Destory\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_IMG_Destory(tmp1);
    }
    else if (idx == 12)
    {
        tmp2 = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        // DEBUG_PRINT("[call]GAME_isKeyDown(%d)\n", tmp1);
        if (simulatePress == 1)
        {
            tmp2 = (tmp1 & (1 << simulateKey)) != 0; // 0按下
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 13)
    {
        // printf("[call]GAME_isKeyHold\n");
        vm_set_call_result(0);
    }
    else if (idx == 24)
    {
        DEBUG_PRINT("[call]GetStreamDataFormRes\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_GetStreamDataFormRes(tmp1, tmp2, tmp3, tmp4);
    }
    else if (idx == 51)
    {
        printf("[call]CdRectPoint\n");
        assert(0);
    }
    else if (idx == 58)
    {
        printf("[call]Sqrt\n");
        assert(0);
    }
    else if (idx == 59)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // int* p_g_memoryBlock
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // size
        DEBUG_PRINT("[call]initMemoryBlock(%x,%x)\n", tmp1, tmp2);
        vm_initMemoryBlock(tmp1, tmp2);
    }
    else if (idx == 61)
    {
        // p_isInQuit = &isInQuit;
        // ::nextSubTScreen = nextSubTScreen;
        // return p_isInQuit;
        // 传入一个函数表地址  执行顺序 0 -> 8 -> 12 -> 8 -> 12 -> 4

        // MEMORY:016E4380 DCD 0x16C65FD Init
        // MEMORY:016E4384 DCD 0x16C6357 Distroy
        // MEMORY:016E4388 DCD 0x16C61D1 Logic
        // MEMORY:016E438C DCD 0x16C4D61 Render
        // MEMORY:016E4390 DCD 0x16C4D59 Pause
        // MEMORY:016E4394 DCD 0x16C4D3B Remuse
        // MEMORY:016E4398 DCD 0x16C4D2D LoadResource
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
        uc_mem_read(MTK, tmp1 + 8, &tmp2, 4);
        DEBUG_PRINT("[call]SCREEN_ChangeScreen(%x)\n", tmp1);
        tmp1 = VM_SCREEN_isInQuit_ADDRESS;
        tmp2 = 0;
        uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
        screenStructChange = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 62)
    {
        u32 lr = 0;
        u32 screenPtr = vmAddedScreen;
        u32 entry0 = 0, entry4 = 0, entry8 = 0, entry12 = 0, entry16 = 0, entry20 = 0, entry24 = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (screenPtr)
        {
            uc_mem_read(MTK, screenPtr + 0x00, &entry0, 4);
            uc_mem_read(MTK, screenPtr + 0x04, &entry4, 4);
            uc_mem_read(MTK, screenPtr + 0x08, &entry8, 4);
            uc_mem_read(MTK, screenPtr + 0x0c, &entry12, 4);
            uc_mem_read(MTK, screenPtr + 0x10, &entry16, 4);
            uc_mem_read(MTK, screenPtr + 0x14, &entry20, 4);
            uc_mem_read(MTK, screenPtr + 0x18, &entry24, 4);
        }
        vm_net_trace("screen_notify_load_res lr=%08x screen=%08x table=%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                     lr, screenPtr, entry0, entry4, entry8, entry12, entry16, entry20, entry24);
        screenStructNotifyLoadRes = 1;
        DEBUG_PRINT("[call]SCREEN_NotifyLoadResource(entry:0x%x)\n", tmp2);
    }
    else if (idx == 63)
    {
        // DEBUG_PRINT("[call]SCREEN_IsPointerHold\n");
        tmp1 = simulateTouchPress;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 64)
    {
        // DEBUG_PRINT("[call]SCREEN_IsPointerDown(%d)\n", simulateTouchPress);
        tmp1 = simulateTouchDown;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 65)
    {
        // DEBUG_PRINT("[call]SCREEN_IsPointerUp\n");
        tmp1 = simulateTouchUp;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 66)
    {
        // DEBUG_PRINT("[call]SCREEN_IsPointerDrag\n");
        tmp1 = simulateTouchDrag;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 67)
    {
        // DEBUG_PRINT("[call]SCREEN_GetPointerX(%d)\n", simulateTouchX);
        tmp1 = simulateTouchX; // x坐标
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 68)
    {
        // DEBUG_PRINT("[call]SCREEN_GetPointerY(%d)\n", simulateTouchY);
        tmp1 = simulateTouchY; // y坐标
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 69)
    {
        printf("[call]Get_CurKeyDownState\n");
        assert(0);
    }

    else if (idx == 81)
    {
        // DreamFactory_DataPackage = 0;
        tmp1 = 0;
        uc_mem_write(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
        // MemoryBlockPtr = (int (*)())getMemoryBlockPtr();
        // DreamFactory_MemoryBlock = MemoryBlockPtr;
        tmp1 = VM_MemoryBlock_PTR_ADDRESS;
        uc_mem_write(MTK, VM_DreamFactory_MemoryBlock_ADDRESS, &tmp1, 4);
        DEBUG_PRINT("[call]initDreamFactoryEngine\n");
    }
    else if (idx == 82)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_mem_write(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
        DEBUG_PRINT("[call]DF_SetDataPackage(%x)\n", tmp1);
    }
    else if (idx == 83)
    {
        DEBUG_PRINT("[call]DF_GetDataPackage\n");
        uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 84)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_resource_by_id(tmp1);
    }
    else if (idx == 85)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_resource_by_file_name(tmp1);
    }
    else if (idx == 86)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_resource_name_by_id(tmp1);
    }
    else if (idx == 87)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_resource_id_by_file_name(tmp1);
    }
    else if (idx == 88)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_t_resource(tmp1, 0);
    }
    else if (idx == 89)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_df_get_t_resource(tmp1, 1);
    }
    else if (idx == 90)
    {
        printf("[call]DF_DataPackage_ShowFileList\n");
        assert(0);
    }
    else if (idx == 91)
    {
        printf("[call]DF_String_Equal\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_String_Equal(tmp1, tmp2);
    }
    else if (idx == 92)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        tmp1 = vm_DF_ReadShort(tmp1, tmp2);
        DEBUG_PRINT("[call]DF_ReadShort(%x)\n", tmp1);
    }
    else if (idx == 93)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_ReadInt(tmp1, tmp2);
        DEBUG_PRINT("[call]DF_ReadInt\n");
    }
    else if (idx == 94)
    {
        printf("[call]DF_File_ReadShort\n");
        assert(0);
    }
    else if (idx == 95)
    {
        printf("[call]DF_File_ReadInt\n");
        assert(0);
    }
    else if (idx == 96)
    {
        printf("[call]DF_WriteShort\n");
        assert(0);
    }
    else if (idx == 97)
    {
        printf("[call]DF_WriteInt\n");
        assert(0);
    }
    else if (idx == 98)
    {
        DEBUG_PRINT("[call]DF_ReadString\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_ReadString(tmp1, tmp2);
    }
    else if (idx == 99)
    {
        printf("[call]DF_ReadStringEx\n");
        assert(0);
    }
    else if (idx == 100)
    {
        printf("[call]DF_File_ReadString\n");
        assert(0);
    }
    else if (idx == 101)
    {
        printf("[call]DF_File_ReadToBuffer\n");
        assert(0);
    }
    else if (idx == 102)
    {
        printf("[call]DF_ReadString2\n");
        assert(0);
    }
    else if (idx == 103)
    {
        DEBUG_PRINT("[call]DF_GetMemoryBlock\n");
        vm_DF_GetMemoryBlock();
    }
    else if (idx == 104)
    {
        printf("[call]DF_Sin\n");
        assert(0);
    }
    else if (idx == 105)
    {
        printf("[call]DF_Cos\n");
        assert(0);
    }
    else if (idx == 106)
    {
        printf("[call]DF_Degree\n");
        assert(0);
    }
    else if (idx == 107)
    {
        printf("[call]DF_CollectionTest\n");
        assert(0);
    }
    else if (idx == 108)
    {
        printf("[call]DF_SwapVal\n");
        assert(0);
    }
    else if (idx == 109)
    {
        DEBUG_PRINT("[call]DF_GetFormatString\n");
        vm_DF_GetFormatString();
    }
    else if (idx == 111)
    {
        // 传入指针写函数表
        printf("[call]initDFDataPackage\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // size
        vm_initDFDataPackage(tmp1, tmp2);
    }

    else if (idx == 134)
    {
        printf("[call]memcpy\n");
        assert(0);
    }
    else if (idx == 135)
    {
        printf("[call]strlen\n");
        assert(0);
    }
    else if (idx == 136)
    {
        printf("[call]memset\n");
        assert(0);
    }
    else if (idx == 137)
    {
        printf("[call]sprintf\n");
        assert(0);
    }
    else if (idx == 138)
    {
        printf("[call]vm_log_trace\n");
        assert(0);
    }
    else if (idx == 139)
    {
        printf("[call]VmGetRand\n");
        assert(0);
    }
    else if (idx == 140)
    {
        printf("[call]vsprintf\n");
        assert(0);
    }
    else if (idx == 141)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp1 && tmp2 && tmp3)
        {
            u8 ch = 0;
            u32 i = 0;
            for (; i < tmp3; ++i)
            {
                uc_mem_read(MTK, tmp2 + i, &ch, 1);
                uc_mem_write(MTK, tmp1 + i, &ch, 1);
                if (ch == 0)
                    break;
            }
            if (i < tmp3)
            {
                ch = 0;
                for (++i; i < tmp3; ++i)
                    uc_mem_write(MTK, tmp1 + i, &ch, 1);
            }
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 142)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_strcpy(tmp1, tmp2);
    }
    else if (idx == 143)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (tmp1 && tmp2)
        {
            int dstLen = vm_strlen(tmp1);
            vm_readStringByPtr(tmp2, cbeTextString);
            uc_mem_write(MTK, tmp1 + dstLen, cbeTextString, strlen((char *)cbeTextString) + 1);
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 144)
    {
        printf("[call]atol\n");
        assert(0);
    }

    // result 区 (从 a1+144 开始)
    else if (idx == 145)
    {
        printf("[call]memmove\n");
        assert(0);
    }
    else if (idx == 146)
    {
        printf("[call]atoi\n");
        assert(0);
    }
    else if (idx == 147)
    {
        printf("[call]BILLING_GetPayNumByAppId\n");
        assert(0);
    }
    else if (idx == 148)
    {
        printf("[call]BILLING_GetRemainDay\n");
        assert(0);
    }
    else if (idx == 149)
    {
        printf("[call]BILLING_Pay\n");
        assert(0);
    }
    else if (idx == 150)
    {
        printf("[call]BILLING_PayMoreTimes\n");
        assert(0);
    }
    else if (idx == 151)
    {
        printf("[call]BILLING_IsRegisterBillingInfo\n");
        assert(0);
    }
    else if (idx == 152)
    {
        printf("[call]BILLING_RegisterBillingInfo\n");
        assert(0);
    }
    else if (idx == 153)
    {
        printf("[call]BILLING_SetBillingStatus\n");
        assert(0);
    }
    else if (idx == 154)
    {
        printf("[call]BILLING_GetBillingStatus\n");
        assert(0);
    }
    else if (idx == 155)
    {
        printf("[call]BILLING_IsNeedPay\n");
        assert(0);
    }
    else if (idx == 156)
    {
        printf("[call]BILLING_IsInTryStatus\n");
        assert(0);
    }
    else if (idx == 157)
    {
        printf("[call]Game_OpenBillingPromptWin\n");
        assert(0);
    }
    else if (idx == 158)
    {
        printf("[call]vMstricmp\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmGameOldManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_df_datapackage_func(u32 address)
{
    if (!(address >= VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS && address < (VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_LoadPackage(tmp1, tmp2);
        printf("[call]DF_DataPackage_LoadPackage\n");
    }
    else if (idx == 2)
    {
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        printf("[call]DF_DataPackage_LoadFromTResource\n");
        assert(0);
    }
    else if (idx == 4)
    {
        printf("[call]DF_DataPackage_LoadFormTCard\n");
        assert(0);
    }
    else if (idx == 5)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        VM_DF_DataPackage_DoLoading(tmp1, tmp2, tmp3);
        printf("[call]DF_DataPackage_DoLoading\n");
    }
    else if (idx == 6)
    {
        printf("[call]DF_DataPackage_LocateDataPackage\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_LocateDataPackage(tmp1, tmp2);
    }
    else if (idx == 7)
    {
        printf("[call]DF_DataPackage_GetFile\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_GetFile(tmp1, tmp2);
    }
    else if (idx == 8)
    {
        printf("[call]DF_DataPackage_GetFileByID\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_GetFileByID(tmp1, tmp2);
    }
    else if (idx == 9)
    {
        printf("[call]DF_DataPackage_GetFileNameByID\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_GetFileNameByID(tmp1, tmp2);
    }
    else if (idx == 10)
    {
        printf("[call]DF_DataPackage_GetFileID\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_GetFileID(tmp1, tmp2);
    }
    else if (idx == 11)
    {
        printf("[call]DF_DataPackage_ShowFileList\n");
        assert(0);
    }
    else if (idx == 12)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_InitTxt(tmp1, tmp2);
        printf("[call]DF_DataPackage_InitTxt\n");
    }

    else
    {
        printf("[impl]DF_PACKAGE_调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_mf_memoryblock_func(u32 address)
{
    if (!(address >= VM_MF_MemoryBlock_FUNC_LIST_ADDRESS && address < (VM_MF_MemoryBlock_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MF_MemoryBlock_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_MF_MemoryBlock_Malloc(tmp1, tmp2);
        DEBUG_PRINT("[call]MF_MemoryBlock_Malloc\n");
    }
    else if (idx == 2)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_MF_MemoryBlock_Reset(tmp1);
        DEBUG_PRINT("[call]MF_MemoryBlock_Reset\n");
    }
    else if (idx == 3)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_MF_MemoryBlock_Release(tmp1);
        DEBUG_PRINT("[call]MF_MemoryBlock_Release\n");
    }
    else
    {
        printf("[impl]MF_MemoryBlock_调用位置:%d\n", idx);
        assert(0);
    }

    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_appstore_func(u32 address)
{
    if (!(address >= VM_APPSTORE_FUNC_LIST_ADDRESS && address < (VM_APPSTORE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_APPSTORE_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        printf("[call]VmAppStoreInstall\n");
        assert(0);
    }
    else if (idx == 2)
    {
        printf("[call]VmAppStoreUninstallEx\n");
        assert(0);
    }
    else if (idx == 3)
    {
        printf("[call]VmAppStoreUninstall\n");
        assert(0);
    }
    else if (idx == 5)
    {
        printf("[call]VmAppStoreGetAppInfo\n");
        assert(0);
    }
    else if (idx == 6)
    {
        printf("[call]VmAppStoreGetInstalledAppInfos\n");
        assert(0);
    }
    else if (idx == 7)
    {
        printf("[call]VmAppStoreReleaseAppInfos\n");
        assert(0);
    }
    else if (idx == 8)
    {
        printf("[call]VmAppStorePushMsg\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]VmAppStoreRunJavaAp\n");
        assert(0);
    }
    else if (idx == 10)
    {
        printf("[call]VmAppStoreRunJavaApEx\n");
        assert(0);
    }
    else if (idx == 11)
    {
        printf("[call]VmAppStoreRunCbeAp\n");
        assert(0);
    }
    else if (idx == 12)
    {
        printf("[call]VmAppStoreSupportJava\n");
        assert(0);
    }
    else if (idx == 13)
    {
        printf("[call]VmAppStoreGetPath\n");
        assert(0);
    }
    else if (idx == 14)
    {
        printf("[call]VmGetPhoneType\n");
        assert(0);
    }
    else if (idx == 15)
    {
        printf("[call]VmGetAppNumByType\n");
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]VmGetPhoneSupportApType\n");
        assert(0);
    }
    else if (idx == 17)
    {
        printf("[call]VmAppGetHasLocalIcon\n");
        assert(0);
    }
    else if (idx == 18)
    {
        printf("[call]VmAppRunApByIdAndName\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]VmAppAddShortCutMenu\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]VmAppDelShortCutMenu\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]CoolBar_DownLoad_AppFile\n");
        assert(0);
    }
    else if (idx == 22)
    {
        printf("[call]GetApsDownAppUrl\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]Coolbar_PreLoadAppByName\n");
        assert(0);
    }
    else if (idx == 24)
    {
        printf("[call]Coolbar_ParseApsDownDataFile\n");
        assert(0);
    }
    else if (idx == 25)
    {
        printf("[call]CoolBar_DownLoad_Stop\n");
        assert(0);
    }
    else if (idx == 26)
    {
        printf("[call]VmAppQueryStcExist\n");
        assert(0);
    }
    else if (idx == 27)
    {
        printf("[call]VmPreCheckInstallAppPlace\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]VmGetInstallFileSystem\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]VmSetInstallFileSystem\n");
        assert(0);
    }
    else if (idx == 30)
    {
        printf("[call]VmSetRunAppFileSystem\n");
        assert(0);
    }
    else if (idx == 31)
    {
        printf("[call]VmGetRunAppFileSystem\n");
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 32)
    {
        printf("[call]VmAutoSelectAppDownPlace\n");
        assert(0);
    }

    // result 区
    else if (idx == 33)
    {
        printf("[call]VmGetApsVerNum\n");
        assert(0);
    }
    else if (idx == 34)
    {
        printf("[call]VmReadAppStoreCbeVersion\n");
        assert(0);
    }
    else if (idx == 35)
    {
        printf("[call]VmWriteAppStoreCbeVersion\n");
        assert(0);
    }
    else if (idx == 36)
    {
        printf("[call]VmGetSecurityCode\n");
        assert(0);
    }
    else if (idx == 37)
    {
        printf("[call]VmGetSecurityCodeEx\n");
        assert(0);
    }
    else if (idx == 38)
    {
        printf("[call]VmSetAppStoreName\n");
        assert(0);
    }
    else if (idx == 39)
    {
        printf("[call]VmGetAppStoreName\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmAPPStoreManager调用位置:%d\n", idx);
        assert(0);
    }
    // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_dl_load_func(u32 address)
{
    if (!(address >= VM_DL_LOAD_FUNC_LIST_ADDRESS && address < (VM_DL_LOAD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_DL_LOAD_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        printf("[call]vlDlSetCurrContextAddress\n");
        assert(0);
    }
    else if (idx == 1)
    {
        printf("[call]vlDlSetContextAddress\n");
        assert(0);
    }
    else if (idx == 2)
    {
        printf("[call]vlDlUnLoadCurrApp\n");
        assert(0);
    }
    else if (idx == 3)
    {
        printf("[call]vlDlUnLoadApp\n");
        assert(0);
    }
    else if (idx == 4)
    {
        printf("[call]vmDlParseAndCopy\n");
        assert(0);
    }
    else if (idx == 5)
    {
        printf("[call]vlDlLoadApp\n");
        assert(0);
    }
    else if (idx == 6)
    {
        printf("[call]vlDlLoadAppEx\n");
        assert(0);
    }
    else if (idx == 7)
    {
        printf("[call]vlDlAppIsInDl\n");
        assert(0);
    }
    else if (idx == 8)
    {
        printf("[call]vmGetcurrInnerAppId\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]CBInnerInit_qqIn\n");
        assert(0);
    }
    else if (idx == 10)
    {
        printf("[call]VmGetCBEInfoByFileName\n");
        assert(0);
    }
    else
    {
        printf("vmDLLoadManager位置:%d\n", idx);
        assert(0);
    } // bx lr实现
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_dl_pay_func(u32 address)
{
    if (!(address >= VM_DL_PAY_FUNC_LIST_ADDRESS && address < (VM_DL_PAY_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1;
    u32 idx = (address - VM_DL_PAY_FUNC_LIST_ADDRESS) / 4;

    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    vm_net_trace("dl_wpay_call idx=%u r0=%08x last=%08x\n", idx, tmp1, lastAddress);
    vm_set_call_result(0);

    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_dl_rs_func(u32 address)
{
    if (!(address >= VM_DL_RS_FUNC_LIST_ADDRESS && address < (VM_DL_RS_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_DL_RS_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        vm_DF_GetDataPackage();
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_IMG_CreateImageFormStream(tmp1, tmp2);
    }
    else if (idx == 16)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
            vm_free(tmp1);
        vm_set_call_result(0);
    }
    else if (idx == 17)
    {
        vm_set_call_result(VM_DF_DataPackage_In_File_Offset_ADDRESS);
    }
    else if (idx == 18)
    {
        vm_set_call_result(VM_DF_DataPackage_FilePath_ADDRESS);
    }
    else if (idx == 19)
    {
        vm_set_call_result(VM_DF_DataPackage_In_File_Length_ADDRESS);
    }
    else
    {
        printf("[impl]vmDlRsManager调用位置:%d\n", idx);
        assert(0);
    }
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_dl_image_func(u32 address)
{
    if (!(address >= VM_DL_IMAGE_FUNC_LIST_ADDRESS && address < (VM_DL_IMAGE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_DL_IMAGE_FUNC_LIST_ADDRESS) / 4;
    if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        tmp2 = vm_malloc(tmp1);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 5)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
            vm_free(tmp1);
        vm_set_call_result(0);
    }
    else
    {
        printf("[impl]vmDlImageManager调用位置:%d\n", idx);
        assert(0);
    }
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}

static bool hook_vm_video_func(u32 address)
{
    if (!(address >= VM_VIDEO_FUNC_LIST_ADDRESS && address < (VM_VIDEO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_VIDEO_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        printf("[call]vM_CB_ISP_ServiceOpen\n");
        assert(0);
    }
    else if (idx == 1)
    {
        printf("[call]vM_CB_ISP_ServiceClose\n");
        assert(0);
    }
    else if (idx == 2)
    {
        printf("[call]vM_CB_VideoParamSet\n");
        assert(0);
    }
    else if (idx == 3)
    {
        printf("[call]vM_CB_VideoAFrameDisp\n");
        assert(0);
    }
    else if (idx == 4)
    {
        printf("[call]vM_CB_Video_IRAM_mem_Init\n");
        assert(0);
    }
    else if (idx == 5)
    {
        printf("[call]vM_CB_Video_IRAM_mem_malloc\n");
        assert(0);
    }
    else if (idx == 6)
    {
        printf("[call]vM_CB_Video_IRAM_mem_Free\n");
        assert(0);
    }
    else if (idx == 7)
    {
        printf("[call]vM_CB_Video_Iram_mem_Close\n");
        assert(0);
    }
    else if (idx == 8)
    {
        printf("[call]vMOpenVideoMedia\n");
        assert(0);
    }
    else if (idx == 9)
    {
        printf("[call]vMCloseVideoMedia\n");
        assert(0);
    }
    else if (idx == 10)
    {
        printf("[call]vMStartCameraCaptureImage\n");
        assert(0);
    }
    else if (idx == 11)
    {
        printf("[call]vMStopCameraCaptureImage\n");
        assert(0);
    }
    else if (idx == 12)
    {
        printf("[call]vMIsDoubleCamera\n");
        assert(0);
    }
    else if (idx == 13)
    {
        printf("[call]vMSetCamearaLocation\n");
        assert(0);
    }
    else if (idx == 14)
    {
        printf("[call]CoolbarVideoDec_Init\n");
        assert(0);
    }
    else if (idx == 15)
    {
        printf("[call]CoolbarVideoDecoding_Frame\n");
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]CoolbarVideoDec_Output\n");
        assert(0);
    }
    else if (idx == 17)
    {
        printf("[call]CoolbarVideoDec_OutputEx\n");
        assert(0);
    }
    else if (idx == 18)
    {
        printf("[call]CoolbarVideoDec_Parm\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]CoolbarVideoDec_ParmEx\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]CoolbarVideoDec_Close\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]CoolbarVideoEnc_Init\n");
        assert(0);
    }
    else if (idx == 22)
    {
        printf("[call]CoolbarVideoEnc_frame\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]CoolbarVideoEnc_Close\n");
        assert(0);
    }
    else if (idx == 24)
    {
        printf("[call]CoolbarVideoEnc_ExWH\n");
        assert(0);
    }
    else if (idx == 25)
    {
        printf("[call]CoolBar_VPP_PicScaling\n");
        assert(0);
    }
    else if (idx == 26)
    {
        printf("[call]CoolBar_VPP_PicUpturn\n");
        assert(0);
    }
    else if (idx == 27)
    {
        printf("[call]vMSetCamSampTime\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]vMIsVideoPlayerRun\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]vMPlayVideo\n");
        assert(0);
    }
    else if (idx == 30)
    {
        printf("[call]vMRegPlayVideoCb\n");
        assert(0);
    }
    else if (idx == 31)
    {
        printf("[call]vMGetMediaFileInfo\n");
        assert(0);
    }
    else if (idx == 32)
    {
        printf("[call]vMPlayStreamingVideo\n");
        assert(0);
    }
    else if (idx == 33)
    {
        printf("[call]vMStopStreamingVideo\n");
        assert(0);
    }
    else if (idx == 34)
    {
        printf("[call]vMSetDownloadInterface\n");
        assert(0);
    }
    else if (idx == 35)
    {
        printf("[call]vMGetStreamingVideoHandle\n");
        assert(0);
    }
    else if (idx == 36)
    {
        printf("[call]vMGetStreamingInfo\n");
        assert(0);
    }
    else if (idx == 37)
    {
        printf("[call]vMStreamingFileChange\n");
        assert(0);
    }
    else
    {
        printf("vmVideoManager位置:%d \n", idx);
        assert(0);
    }
    return true;
}
static void hook_vm_manager_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_sys_manager_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_sys_manager_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_memory_manager_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_memory_manager_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_lcd_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_lcd_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_fileio_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_fileio_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_stdio_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_stdio_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_timer_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_timer_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_ctrl_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_ctrl_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_network_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_network_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_game_util_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_game_util_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_df_engine_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_df_engine_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_billing_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_billing_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_ucs2_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_ucs2_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_screen_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_screen_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_df_script_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_df_script_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_game_lcd_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_game_lcd_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_netapp_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_netapp_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_audio_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_audio_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_sensor_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_sensor_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_vmim_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_vmim_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_manager_gameold_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_manager_gameold_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_df_datapackage_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_df_datapackage_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_mf_memoryblock_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_mf_memoryblock_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_appstore_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_appstore_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_dl_load_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_dl_load_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_dl_pay_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_dl_pay_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_dl_rs_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_dl_rs_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_dl_image_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_dl_image_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_video_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_video_func((u32)address);
    lastAddress = (u32)address;
}

static uc_err add_manager_code_hooks(uc_engine *uc)
{
    uc_hook hook;
    uc_err err;
    err = uc_hook_add(uc, &hook, UC_HOOK_CODE, hook_vm_pool_code_callback, NULL,
                      VM_Memory_Pool_ADDRESS, VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE - 1);
    if (err != UC_ERR_OK)
        return err;
#define ADD_MANAGER_CODE_HOOK_RANGE(begin, end, cb)                       \
    do                                                                    \
    {                                                                     \
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE, cb, NULL, begin, end); \
        if (err != UC_ERR_OK)                                             \
            return err;                                                   \
    } while (0)
#define ADD_MANAGER_CODE_HOOK(begin, cb) ADD_MANAGER_CODE_HOOK_RANGE(begin, begin + VM_MANAGER_FUNC_LIST_SIZE - 1, cb)

    ADD_MANAGER_CODE_HOOK(VM_MANAGER_FUNC_LIST_ADDRESS, hook_vm_manager_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_SYS_MANAGER_FUNC_LIST_ADDRESS, hook_vm_sys_manager_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS, hook_vm_memory_manager_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_LCD_FUNC_LIST_ADDRESS, hook_vm_manager_lcd_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS, hook_vm_manager_fileio_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_STDIO_FUNC_LIST_ADDRESS, hook_vm_manager_stdio_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_TIMER_FUNC_LIST_ADDRESS, hook_vm_manager_timer_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_CTRL_FUNC_LIST_ADDRESS, hook_vm_manager_ctrl_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS, hook_vm_manager_network_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS, hook_vm_manager_game_util_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS, hook_vm_manager_df_engine_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_BILLING_FUNC_LIST_ADDRESS, hook_vm_manager_billing_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_UCS2_FUNC_LIST_ADDRESS, hook_vm_manager_ucs2_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS, hook_vm_manager_screen_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS, hook_vm_manager_df_script_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS, hook_vm_manager_game_lcd_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS, hook_vm_manager_netapp_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS, hook_vm_manager_audio_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS, hook_vm_manager_sensor_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_VMIM_FUNC_LIST_ADDRESS, hook_vm_manager_vmim_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS, hook_vm_manager_gameold_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS, hook_vm_df_datapackage_code_callback);
    ADD_MANAGER_CODE_HOOK_RANGE(VM_MF_MemoryBlock_FUNC_LIST_ADDRESS, VM_APPSTORE_FUNC_LIST_ADDRESS - 1, hook_vm_mf_memoryblock_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_APPSTORE_FUNC_LIST_ADDRESS, hook_vm_appstore_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_DL_LOAD_FUNC_LIST_ADDRESS, hook_vm_dl_load_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_DL_PAY_FUNC_LIST_ADDRESS, hook_vm_dl_pay_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_DL_RS_FUNC_LIST_ADDRESS, hook_vm_dl_rs_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_DL_IMAGE_FUNC_LIST_ADDRESS, hook_vm_dl_image_code_callback);
    ADD_MANAGER_CODE_HOOK(VM_VIDEO_FUNC_LIST_ADDRESS, hook_vm_video_code_callback);
#undef ADD_MANAGER_CODE_HOOK
#undef ADD_MANAGER_CODE_HOOK_RANGE
    return UC_ERR_OK;
}

void hookCodeCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    if (vm_is_manager_func_stub_address((u32)address))
        return;

#ifdef GDB_SERVER_SUPPORT
    tmp2 = gdbTarget.simulate_pc_count;
    if (tmp2 == 0)
    {
        for (tmp1 = 0; tmp1 < gdbTarget.num_breakpoints; tmp1++)
        {
            if (gdbTarget.breakpoints[tmp1] == address)
            {
                gdbTarget.running = 0;
                gdbTarget.last_stop_reason = 0x05;
                tmp2 = 1;
                break;
            }
        }
    }
    else
    {
        gdbTarget.running = 0;
        gdbTarget.simulate_pc_count--;
        gdbTarget.last_stop_reason = 0x05;
    }
    if (tmp2)
    {
        char response[32];
        sprintf(response, "S%02x", gdbTarget.last_stop_reason);
        send_gdb_response(&clients[0], response);
        while (gdbTarget.running == 0)
            ;
    }
#endif
    if (((u32)address & ~1u) == PROGRAM_EXIT_ADDR)
    {
        normalize_program_exit_pc(g_currentEmuEntry);
        uc_emu_stop(MTK);
        return;
    }
    u32 tracePc = (u32)address & ~1u;
    if (tracePc == 0x10035a2 || tracePc == 0x100369c || tracePc == 0x1003cfc || tracePc == 0x103489a || tracePc == 0x103b2d6 ||
        tracePc == 0x103b45a || tracePc == 0x103b4aa || tracePc == 0x103b4f4 || tracePc == 0x103b51a ||
        tracePc == 0x103b584 || tracePc == 0x103b59a || tracePc == 0x103b620 || tracePc == 0x103b684 ||
        tracePc == 0x1039dc8 || tracePc == 0x1039df4 || tracePc == 0x1039e12 ||
        tracePc == 0x103a77c || tracePc == 0x103a784 || tracePc == 0x103a796 || tracePc == 0x103a7ac ||
        tracePc == 0x103a7ba || tracePc == 0x103a7c2 || tracePc == 0x103a7c6 ||
        tracePc == 0x103ac84 || tracePc == 0x103ac88 || tracePc == 0x103ac90 ||
        tracePc == 0x103ac9c || tracePc == 0x103ac9e ||
        tracePc == 0x10348a8 || tracePc == 0x10348b6 || tracePc == 0x10348c0 ||
        tracePc == 0x10348d8 || tracePc == 0x10348de || tracePc == 0x10348ec ||
        tracePc == 0x10346e0 || tracePc == 0x103467a || tracePc == 0x10346c6 || tracePc == 0x10346cc ||
        tracePc == 0x10346d0 || tracePc == 0x10346dc || tracePc == 0x1033b9a || tracePc == 0x1033c30 ||
        tracePc == 0x1033c6c || tracePc == 0x1033cac || tracePc == 0x1033cf2 || tracePc == 0x1033d40 ||
        tracePc == 0x1033e56 ||
        tracePc == 0x1034922 || tracePc == 0x103492a || tracePc == 0x103493c || tracePc == 0x1034960 ||
        tracePc == 0x1033c64 || tracePc == 0x1033c68 || tracePc == 0x1037472 || tracePc == 0x10374a0 ||
        tracePc == 0x10374a4 || tracePc == 0x10374ac || tracePc == 0x1037552 || tracePc == 0x1037554 ||
        tracePc == 0x103755e || tracePc == 0x103757e || tracePc == 0x1037588 || tracePc == 0x1037598 ||
        tracePc == 0x10372d6 || tracePc == 0x103730e || tracePc == 0x1037318 || tracePc == 0x1037324 ||
        tracePc == 0x1037334 || tracePc == 0x1037348 || tracePc == 0x1037358 || tracePc == 0x1037378 ||
        tracePc == 0x1037386 || tracePc == 0x1037388 || tracePc == 0x1037398 || tracePc == 0x10373aa ||
        tracePc == 0x10373b8 || tracePc == 0x10373be || tracePc == 0x10373ce || tracePc == 0x10373da ||
        tracePc == 0x10373ea || tracePc == 0x103743e || tracePc == 0x103745a ||
        tracePc == 0x1033e88 || tracePc == 0x1034018 || tracePc == 0x1034130 || tracePc == 0x1034296 ||
        tracePc == 0x1034412 || tracePc == 0x1034442 || tracePc == 0x1034486 || tracePc == 0x10344c4 ||
        tracePc == 0x10345e6 || tracePc == 0x1034754 || tracePc == 0x1034772 ||
        tracePc == 0x103745e || tracePc == 0x1044a12 || tracePc == 0x1044a54 || tracePc == 0x1044aa8 ||
        tracePc == 0x1044948 || tracePc == 0x10449f4 || tracePc == 0x10449fc || tracePc == 0x1044a00 ||
        tracePc == 0x1046c20 || tracePc == 0x1046c48 || tracePc == 0x1046c4e || tracePc == 0x1046c58 ||
        tracePc == 0x1046c2c || tracePc == 0x1046c32 ||
        tracePc == 0x100bd08 || tracePc == 0x100bda8 || tracePc == 0x100bf16 || tracePc == 0x100bf24 ||
        tracePc == 0x100d6e2 || tracePc == 0x100d6fa || tracePc == 0x100d700 || tracePc == 0x100d702 ||
        tracePc == 0x100d7b4 || tracePc == 0x100d7c4 || tracePc == 0x100d7d2 || tracePc == 0x100d7d4 ||
        tracePc == 0x100d85c || tracePc == 0x100d864 || tracePc == 0x100d95e || tracePc == 0x100da82 ||
        tracePc == 0x100da90 || tracePc == 0x100db2c || tracePc == 0x100db46 || tracePc == 0x100db4a ||
        tracePc == 0x100d262 || tracePc == 0x100d272 || tracePc == 0x100d2a0 || tracePc == 0x100d2aa ||
        tracePc == 0x1003738 || tracePc == 0x1003758 || tracePc == 0x100375c || tracePc == 0x1003760 ||
        tracePc == 0x1003764 || tracePc == 0x1003768 || tracePc == 0x100376a ||
        tracePc == 0x100f8f4 || tracePc == 0x100f8fa ||
        tracePc == 0x10352ae || tracePc == 0x10352d4 || tracePc == 0x10352d6 ||
        tracePc == 0x10352e0 || tracePc == 0x10352f0 ||
        tracePc == 0x1046c60 || tracePc == 0x1046c80 || tracePc == 0x1046cc2 ||
        tracePc == 0x1046cc8 || tracePc == 0x1046d00 ||
        tracePc == 0x501754e || tracePc == 0x5017554 || tracePc == 0x5017562 || tracePc == 0x501757a ||
        tracePc == 0x5017584 || tracePc == 0x5017590 || tracePc == 0x50175ac || tracePc == 0x50175c2 ||
        tracePc == 0x50175ca || tracePc == 0x50175ec ||
        (tracePc >= 0x100fa88 && tracePc <= 0x100fd30) ||
        tracePc == 0x10136dc || tracePc == 0x10136de || tracePc == 0x10136e0 ||
        tracePc == 0x1013700 || tracePc == 0x1013712 || tracePc == 0x10137aa ||
        tracePc == 0x1013642 || tracePc == 0x1013646 ||
        tracePc == 0x100015c || tracePc == 0x1000164 || tracePc == 0x1000166 ||
        tracePc == 0x1000172 || tracePc == 0x100017c ||
        tracePc == 0x1014e5e || tracePc == 0x1014e64 || tracePc == 0x1014e66 ||
        tracePc == 0x1014e70 || tracePc == 0x1014e74 || tracePc == 0x1014e76 ||
        (tracePc >= 0x10114fc && tracePc <= 0x10118e0) ||
        (tracePc >= 0x1012e4c && tracePc <= 0x1013050) ||
        tracePc == 0x1004f54 || tracePc == 0x1004f8a || tracePc == 0x1004e9c)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, sp = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
        uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
        uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
        uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace("pc_trace pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n", tracePc, r0, r1, r2, r3, lr);
        if (tracePc == 0x10348a8 || tracePc == 0x10348b6 || tracePc == 0x10348c0 ||
            tracePc == 0x10348d8 || tracePc == 0x10348de || tracePc == 0x10348ec)
        {
            u32 netObj = 0;
            u32 cb14 = 0, cb44 = 0, state = 0;
            if (Global_R9)
                netObj = Global_R9 + 0x9588;
            if (netObj)
            {
                uc_mem_read(MTK, netObj + 0x0c, &state, 4);
                uc_mem_read(MTK, netObj + 0x14, &cb14, 4);
                uc_mem_read(MTK, netObj + 0x44, &cb44, 4);
            }
            u32 curState = 0, curCb14 = 0, curCb44 = 0;
            if (g_netCurrentObject)
            {
                uc_mem_read(MTK, g_netCurrentObject + 0x0c, &curState, 4);
                uc_mem_read(MTK, g_netCurrentObject + 0x14, &curCb14, 4);
                uc_mem_read(MTK, g_netCurrentObject + 0x44, &curCb44, 4);
            }
            vm_net_trace("net_wrapper_state pc=%08x mgr=%08x mgrState=%u mgrCb14=%08x mgrCb44=%08x cur=%08x curState=%u curCb14=%08x curCb44=%08x r7=%08x\n",
                         tracePc, netObj, state, cb14, cb44, g_netCurrentObject, curState, curCb14, curCb44, r7);
        }
        if (tracePc == 0x103b15a || tracePc == 0x103b162 || tracePc == 0x103b176 || tracePc == 0x103b198)
        {
            u32 startupObj = 0;
            u32 updateInfo = 0;
            u8 localFlag = 0xff;
            u8 state = 0xff;
            u16 stateDetail = 0;
            u8 hasLocalUpdate = 0;
            u8 updateState = 0;
            uc_mem_read(MTK, Global_R9 + 0x9928 + 0x10, &startupObj, 4);
            uc_mem_read(MTK, Global_R9 + 0x9928 + 0x0c, &updateInfo, 4);
            uc_mem_read(MTK, Global_R9 + 0x5496, &hasLocalUpdate, 1);
            uc_mem_read(MTK, Global_R9 + 0x4cb6, &updateState, 1);
            if (startupObj)
            {
                uc_mem_read(MTK, startupObj + 0x140, &localFlag, 1);
                uc_mem_read(MTK, startupObj + 0x3d, &state, 1);
                uc_mem_read(MTK, startupObj + 0x160, &stateDetail, 2);
            }
            vm_net_trace("startup_install_decision pc=%08x startupObj=%08x updateInfo=%08x localFlag=%u state=%u detail=%04x hasLocalUpdate=%u updateState=%u r0=%08x r1=%08x lr=%08x\n",
                         tracePc, startupObj, updateInfo, localFlag, state, stateDetail, hasLocalUpdate, updateState, r0, r1, lr);
        }
        if (tracePc == 0x1039dc8 || tracePc == 0x1039df4 || tracePc == 0x1039e12 ||
            tracePc == 0x103a77c || tracePc == 0x103a784 || tracePc == 0x103a796 || tracePc == 0x103a7ac ||
            tracePc == 0x103a7ba || tracePc == 0x103a7c2 || tracePc == 0x103a7c6 ||
            tracePc == 0x103ac84 || tracePc == 0x103ac88 || tracePc == 0x103ac90 ||
            tracePc == 0x103ac9c || tracePc == 0x103ac9e)
        {
            u32 startupObj = 0, updateWidget = 0, taskHandle = 0, loaderObj = 0, loaderInner = 0, loaderInner16 = 0;
            u8 state = 0xff, updateState = 0xff, hasLocalUpdate = 0xff;
            u16 waitTicks = 0, spinnerTicks = 0;
            if (Global_R9)
            {
                uc_mem_read(MTK, Global_R9 + 0x9928 + 0x10, &startupObj, 4);
                uc_mem_read(MTK, Global_R9 + 0x4cb6, &updateState, 1);
                uc_mem_read(MTK, Global_R9 + 0x5496, &hasLocalUpdate, 1);
                uc_mem_read(MTK, Global_R9 + 0x9944, &loaderObj, 4);
            }
            if (startupObj)
            {
                uc_mem_read(MTK, startupObj + 0x1c, &taskHandle, 4);
                uc_mem_read(MTK, startupObj + 0x3d, &state, 1);
                uc_mem_read(MTK, startupObj + 0x9e, &waitTicks, 2);
                uc_mem_read(MTK, startupObj + 0x20, &spinnerTicks, 2);
                uc_mem_read(MTK, startupObj + 0x140, &updateWidget, 4);
            }
            if (loaderObj)
            {
                uc_mem_read(MTK, loaderObj + 0x20, &loaderInner, 4);
                if (loaderInner)
                    uc_mem_read(MTK, loaderInner + 0x10, &loaderInner16, 4);
            }
            vm_net_trace("startup_async_flow pc=%08x startupObj=%08x state=%u wait=%u spin=%u task=%08x updateWidget=%08x loader=%08x inner=%08x inner16=%08x hasLocalUpdate=%u updateState=%u r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
                         tracePc, startupObj, state, waitTicks, spinnerTicks, taskHandle, updateWidget,
                         loaderObj, loaderInner, loaderInner16, hasLocalUpdate, updateState, r0, r1, r2, r3, lr);
        }
        if (tracePc == 0x100015c || tracePc == 0x1000164 || tracePc == 0x1000166 ||
            tracePc == 0x1000172 || tracePc == 0x100017c)
        {
            u32 owner = r4;
            u32 manager = 0;
            u32 selectedActor = r0;
            u32 tableBase = 0;
            u32 currentActor = 0;
            u32 current5c = 0, current64 = 0;
            u32 actor600 = 0, actor610 = 0, actor61c = 0, actor624 = 0, actor630 = 0, actor640 = 0;
            if (owner)
            {
                uc_mem_read(MTK, owner + 0x08, &manager, 4);
                uc_mem_read(MTK, owner + 0x5c, &current5c, 4);
                uc_mem_read(MTK, owner + 0x60, &currentActor, 4);
                uc_mem_read(MTK, owner + 0x64, &current64, 4);
            }
            if (!currentActor && (tracePc == 0x1000166 || tracePc == 0x1000172 || tracePc == 0x100017c))
                currentActor = selectedActor;
            if (currentActor)
            {
                uc_mem_read(MTK, currentActor + 0x600, &actor600, 4);
                uc_mem_read(MTK, currentActor + 0x610, &actor610, 4);
                uc_mem_read(MTK, currentActor + 0x61c, &actor61c, 4);
                uc_mem_read(MTK, currentActor + 0x624, &actor624, 4);
                uc_mem_read(MTK, currentActor + 0x630, &actor630, 4);
                uc_mem_read(MTK, currentActor + 0x640, &actor640, 4);
            }
            if (Global_R9)
            {
                u32 actorMgr = 0;
                uc_mem_read(MTK, Global_R9 + 0x40, &actorMgr, 4);
                if (actorMgr)
                    uc_mem_read(MTK, actorMgr + 0x58, &tableBase, 4);
            }
            vm_net_trace("scene_ctor pc=%08x owner=%08x manager=%08x selected=%08x s5c=%08x s60=%08x s64=%08x table=%08x a600=%08x a610=%08x a61c=%08x a624=%08x a630=%08x a640=%08x lr=%08x\n",
                         tracePc, owner, manager, selectedActor, current5c, currentActor, current64, tableBase,
                         actor600, actor610, actor61c, actor624, actor630, actor640, lr);
        }
        if (tracePc == 0x100fad2 || tracePc == 0x100fade || tracePc == 0x10136dc || tracePc == 0x10136de ||
            tracePc == 0x10136e0 || tracePc == 0x1013700 || tracePc == 0x1013712 || tracePc == 0x10137aa)
        {
            u32 sceneAddr = 0;
            u32 actorInfo = 0;
            u32 chosen = 0;
            u32 tableBase = 0;
            u32 chosenWords[11] = {0};
            u8 genderOrSlot = 0xff;
            u8 professionOrSex = 0xff;
            if (Global_R9)
            {
                sceneAddr = Global_R9 + 0x5c64;
                uc_mem_read(MTK, sceneAddr + 0x44, &actorInfo, 4);
                uc_mem_read(MTK, sceneAddr + 0x58, &tableBase, 4);
                uc_mem_read(MTK, sceneAddr + 0x5c, &chosen, 4);
            }
            if (actorInfo)
            {
                uc_mem_read(MTK, actorInfo + 0x140, &genderOrSlot, 1);
                uc_mem_read(MTK, actorInfo + 0x141, &professionOrSex, 1);
            }
            if (chosen)
                uc_mem_read(MTK, chosen, chosenWords, sizeof(chosenWords));
            vm_net_trace("actor_choice_state pc=%08x scene=%08x actorInfo=%08x table=%08x chosen=%08x bytes=%u,%u entry=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x r0=%08x r1=%08x r2=%08x lr=%08x\n",
                         tracePc, sceneAddr, actorInfo, tableBase, chosen, genderOrSlot, professionOrSex,
                         chosenWords[0], chosenWords[1], chosenWords[2], chosenWords[3], chosenWords[4], chosenWords[5],
                         chosenWords[6], chosenWords[7], chosenWords[8], chosenWords[9], chosenWords[10],
                         r0, r1, r2, lr);
            if (tracePc == 0x10136e0 && tableBase)
            {
                for (u32 i = 0; i < 6; ++i)
                {
                    u32 entryAddr = tableBase + i * 0x2c;
                    u32 entryWords[11] = {0};
                    if (uc_mem_read(MTK, entryAddr, entryWords, sizeof(entryWords)) == UC_ERR_OK)
                    {
                        vm_net_trace("actor_choice_table idx=%u entry=%08x words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                                     i, entryAddr,
                                     entryWords[0], entryWords[1], entryWords[2], entryWords[3], entryWords[4], entryWords[5],
                                     entryWords[6], entryWords[7], entryWords[8], entryWords[9], entryWords[10]);
                        if (entryWords[3])
                            vm_net_trace_mem("actor_choice_entry3", entryWords[3], 96);
                    }
                }
            }
            if (tracePc == 0x10136e0 && chosenWords[3])
                vm_net_trace_mem("actor_choice_chosen3", chosenWords[3], 128);
        }
        if (tracePc >= 0x100fac4 && tracePc <= 0x100fd24)
        {
            u32 actorBase = r5;
            u32 actorExt = r5 ? r5 + 0x80 : 0;
            u32 actor64 = 0, actor68 = 0, actor134 = 0, actor138 = 0, actorExt4 = 0, actorExt1c = 0, actorExt30 = 0, actorExt34 = 0, actorExt38 = 0, actorExt3c = 0;
            u8 byte140 = 0xff, byte141 = 0xff;
            if (actorBase)
            {
                uc_mem_read(MTK, actorBase + 0x64, &actor64, 4);
                uc_mem_read(MTK, actorBase + 0x68, &actor68, 4);
                uc_mem_read(MTK, actorBase + 0x134, &actor134, 4);
                uc_mem_read(MTK, actorBase + 0x138, &actor138, 4);
                uc_mem_read(MTK, actorBase + 0x140, &byte140, 1);
                uc_mem_read(MTK, actorBase + 0x141, &byte141, 1);
            }
            if (actorExt)
            {
                uc_mem_read(MTK, actorExt + 0x04, &actorExt4, 4);
                uc_mem_read(MTK, actorExt + 0x1c, &actorExt1c, 4);
                uc_mem_read(MTK, actorExt + 0x30, &actorExt30, 4);
                uc_mem_read(MTK, actorExt + 0x34, &actorExt34, 4);
                uc_mem_read(MTK, actorExt + 0x38, &actorExt38, 4);
                uc_mem_read(MTK, actorExt + 0x3c, &actorExt3c, 4);
            }
            vm_net_trace("actorinfo_parse pc=%08x actor=%08x lastRet=%08x b140=%u b141=%u f64=%u f68=%u f134=%08x f138=%08x e4=%u e1c=%u e30=%u e34=%u e38=%u e3c=%u r4=%08x r6=%08x r7=%08x lr=%08x\n",
                         tracePc, actorBase, r0, byte140, byte141, actor64, actor68, actor134, actor138,
                         actorExt4, actorExt1c, actorExt30, actorExt34, actorExt38, actorExt3c, r4, r6, r7, lr);
        }
        if (tracePc >= 0x10114fc && tracePc <= 0x10118e0)
        {
            u32 entryType = 0, entrySub = 0, entryField40 = 0, entryField44 = 0, entryField4c = 0, entryField54 = 0;
            u32 sceneObj = 0, sceneInfoHandler = 0;
            if (r4)
            {
                uc_mem_read(MTK, r4 + 0x04, &entryType, 4);
                uc_mem_read(MTK, r4 + 0x08, &entrySub, 4);
                uc_mem_read(MTK, r4 + 0x40, &entryField40, 4);
                uc_mem_read(MTK, r4 + 0x44, &entryField44, 4);
                uc_mem_read(MTK, r4 + 0x4c, &entryField4c, 4);
                uc_mem_read(MTK, r4 + 0x54, &entryField54, 4);
            }
            if (Global_R9)
            {
                uc_mem_read(MTK, Global_R9 + 0x5ca4, &sceneObj, 4);
                if (sceneObj)
                    uc_mem_read(MTK, sceneObj + 0x13c, &sceneInfoHandler, 4);
            }
            const char *fieldName = "<non-string>";
            if (vm_net_try_read_ascii(r1, cbeTextString, mySizeOf(cbeTextString)))
                fieldName = (const char *)cbeTextString;
            vm_net_trace("game_type_branch pc=%08x entry=%08x type=%u sub=%u name=%s r0=%08x r1=%08x r2=%08x r3=%08x vtbl=%08x,%08x,%08x,%08x sceneObj=%08x infoHandler=%08x lr=%08x\n",
                         tracePc, r4, entryType, entrySub, fieldName, r0, r1, r2, r3,
                         entryField40, entryField44, entryField4c, entryField54, sceneObj, sceneInfoHandler, lr);
        }
        if (tracePc == 0x100d6e2 || tracePc == 0x100d6fa || tracePc == 0x100d700 || tracePc == 0x100d702 ||
            tracePc == 0x100d7b4 || tracePc == 0x100d7c4 || tracePc == 0x100d7d2 || tracePc == 0x100d7d4 ||
            tracePc == 0x100d85c || tracePc == 0x100d864 || tracePc == 0x100d95e || tracePc == 0x100da82 ||
            tracePc == 0x100da90 || tracePc == 0x100db2c || tracePc == 0x100db46 || tracePc == 0x100db4a)
        {
            u32 actor600 = r5 ? r5 + 0x600 : 0;
            u32 actor610 = 0, actor61c = 0, actor620 = 0;
            if (actor600)
            {
                uc_mem_read(MTK, actor600 + 0x10, &actor610, 4);
                uc_mem_read(MTK, actor600 + 0x1c, &actor61c, 4);
                uc_mem_read(MTK, actor600 + 0x20, &actor620, 4);
            }
            const char *actorName = "<non-string>";
            u32 stackedName = 0;
            u32 stackedRaw[4] = {0};
            uc_mem_read(MTK, sp + 0x70, &stackedName, 4);
            uc_mem_read(MTK, sp + 0x70, stackedRaw, sizeof(stackedRaw));
            if (vm_net_try_read_ascii(stackedName, cbeTextString, mySizeOf(cbeTextString)))
                actorName = (const char *)cbeTextString;
            vm_net_trace("actor_construct_flow pc=%08x actor=%08x namePtr=%08x name=%s raw70=%08x,%08x,%08x,%08x a610=%08x a61c=%08x a620=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
                         tracePc, r5, stackedName, actorName, stackedRaw[0], stackedRaw[1], stackedRaw[2], stackedRaw[3],
                         actor610, actor61c, actor620, r0, r1, r2, r3, lr);
            if (tracePc == 0x100d700 || tracePc == 0x100d702 || tracePc == 0x100d7d2 || tracePc == 0x100db4a)
                vm_net_trace_mem("actor_construct_name_object", stackedName, 96);
        }
        if (tracePc == 0x1014e70 || tracePc == 0x1014e74 || tracePc == 0x1046c2c || tracePc == 0x1046c32)
        {
            u32 obj1c = 0, obj20 = 0, obj24 = 0, obj28 = 0, obj2c = 0, obj30 = 0, obj34 = 0;
            u32 scene60 = 0, sceneActor61c = 0;
            if (r0)
            {
                uc_mem_read(MTK, r0 + 0x1c, &obj1c, 4);
                uc_mem_read(MTK, r0 + 0x20, &obj20, 4);
                uc_mem_read(MTK, r0 + 0x24, &obj24, 4);
                uc_mem_read(MTK, r0 + 0x28, &obj28, 4);
                uc_mem_read(MTK, r0 + 0x2c, &obj2c, 4);
                uc_mem_read(MTK, r0 + 0x30, &obj30, 4);
                uc_mem_read(MTK, r0 + 0x34, &obj34, 4);
            }
            if (r4)
                uc_mem_read(MTK, r4 + 0x60, &scene60, 4);
            if (scene60)
                uc_mem_read(MTK, scene60 + 0x61c, &sceneActor61c, 4);
            vm_net_trace("battle_call_arg pc=%08x scene=%08x sceneActor=%08x sceneActor61c=%08x r0=%08x r1=%08x r2=%08x r3=%08x fields=%08x,%08x,%08x,%08x,%08x,%08x,%08x lr=%08x\n",
                         tracePc, r4, scene60, sceneActor61c, r0, r1, r2, r3, obj1c, obj20, obj24, obj28, obj2c, obj30, obj34, lr);
        }
        if (tracePc == 0x100f8f4 || tracePc == 0x100f8fa ||
            tracePc == 0x10352ae || tracePc == 0x10352d4 || tracePc == 0x10352d6 ||
            tracePc == 0x10352e0 || tracePc == 0x10352f0 ||
            tracePc == 0x1046c60 || tracePc == 0x1046c80 || tracePc == 0x1046cc2 ||
            tracePc == 0x1046cc8 || tracePc == 0x1046d00)
        {
            u32 scene = Global_R9 ? Global_R9 + 0x5c64 : 0;
            u32 scene60 = 0;
            u32 actor600 = 0, actor610 = 0, actor61c = 0, actor630 = 0;
            u32 actor658 = 0, actor670 = 0, actor674 = 0;
            u32 draw1c34 = 0;
            if (scene)
                uc_mem_read(MTK, scene + 0x60, &scene60, 4);
            if (scene60)
            {
                uc_mem_read(MTK, scene60 + 0x600, &actor600, 4);
                uc_mem_read(MTK, scene60 + 0x610, &actor610, 4);
                uc_mem_read(MTK, scene60 + 0x61c, &actor61c, 4);
                uc_mem_read(MTK, scene60 + 0x630, &actor630, 4);
                uc_mem_read(MTK, scene60 + 0x658, &actor658, 4);
                uc_mem_read(MTK, scene60 + 0x670, &actor670, 4);
                uc_mem_read(MTK, scene60 + 0x674, &actor674, 4);
                uc_mem_read(MTK, scene60 + 0x1c34, &draw1c34, 4);
            }
            vm_net_trace("scene_runtime_init pc=%08x scene=%08x scene60=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x a600=%08x a610=%08x a61c=%08x a630=%08x a658=%08x a670=%08x a674=%08x d1c34=%08x lr=%08x\n",
                         tracePc, scene, scene60, r0, r1, r2, r3, r4, r5, r6, r7,
                         actor600, actor610, actor61c, actor630, actor658, actor670, actor674, draw1c34, lr);
        }
        if (tracePc == 0x1014e5e || tracePc == 0x1014e64 || tracePc == 0x1014e66 || tracePc == 0x1014e76)
        {
            u32 scene40 = 0, scene44 = 0, scene48 = 0, scene60 = 0;
            u32 actor600 = 0, actor610 = 0, actor61c = 0, actor630 = 0;
            u16 actorX = 0, actorY = 0;
            if (r4)
            {
                uc_mem_read(MTK, r4 + 0x40, &scene40, 4);
                uc_mem_read(MTK, r4 + 0x44, &scene44, 4);
                uc_mem_read(MTK, r4 + 0x48, &scene48, 4);
                uc_mem_read(MTK, r4 + 0x60, &scene60, 4);
            }
            if (scene60)
            {
                uc_mem_read(MTK, scene60 + 0x0a, &actorX, 2);
                uc_mem_read(MTK, scene60 + 0x0c, &actorY, 2);
                uc_mem_read(MTK, scene60 + 0x600, &actor600, 4);
                uc_mem_read(MTK, scene60 + 0x610, &actor610, 4);
                uc_mem_read(MTK, scene60 + 0x61c, &actor61c, 4);
                uc_mem_read(MTK, scene60 + 0x630, &actor630, 4);
            }
            vm_net_trace("scene_actor_state pc=%08x scene=%08x s40=%08x s44=%08x s48=%08x actor=%08x actorXY=%u,%u actor600=%08x actor610=%08x actor61c=%08x actor630=%08x r0=%08x r1=%08x r2=%08x lr=%08x\n",
                         tracePc, r4, scene40, scene44, scene48, scene60, actorX, actorY, actor600, actor610, actor61c, actor630, r0, r1, r2, lr);
        }
        if (tracePc == 0x1004f8a || tracePc == 0x1004e9c)
        {
            u32 actor = (tracePc == 0x1004e9c) ? r0 : r4;
            u32 actorData = 0, animTable = 0;
            if (actor)
                uc_mem_read(MTK, actor + 0x0c, &actorData, 4);
            if (actorData)
                uc_mem_read(MTK, actorData + 0x0c, &animTable, 4);
            vm_net_trace("actor_trace pc=%08x actor=%08x actorData=%08x animTable=%08x lr=%08x\n",
                         tracePc, actor, actorData, animTable, lr);
        }
        if (tracePc == 0x1013642 || tracePc == 0x1013646)
        {
            u32 responseObj = 0, responseCursor = 0, responseState = 0;
            if (Global_R9)
            {
                responseObj = Global_R9 + 0x18;
                uc_mem_read(MTK, responseObj + 0x18, &responseCursor, 4);
                uc_mem_read(MTK, responseObj + 0x20, &responseState, 4);
            }
            vm_net_trace("response_flush_state pc=%08x ret=%08x obj=%08x cursor=%08x state=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
                         tracePc, r0, responseObj, responseCursor, responseState, r1, r2, r3, lr);
        }
        if (tracePc == 0x1044a12 && r1)
        {
            const char *path = "<non-string>";
            if (vm_net_try_read_ascii(r1, cbeTextString, mySizeOf(cbeTextString)))
                path = (const char *)cbeTextString;
            vm_net_trace("cbm_loader_request cb=%08x path=%s pathPtr=%08x lr=%08x\n", r0, path, r1, lr);
        }
        if (tracePc == 0x10449f4 || tracePc == 0x100bd08 || tracePc == 0x100bda8 ||
            tracePc == 0x100bf16 || tracePc == 0x100bf24)
        {
            const char *path = "<non-string>";
            if (vm_net_try_read_ascii(r0, cbeTextString, mySizeOf(cbeTextString)))
                path = (const char *)cbeTextString;
            vm_net_trace("cbm_load_flow pc=%08x path_or_r0=%s r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
                         tracePc, path, r0, r1, r2, r3, lr);
        }
        if (tracePc == 0x100d262 || tracePc == 0x100d272 || tracePc == 0x100d2a0 || tracePc == 0x100d2aa)
        {
            u32 listBase = 0, listCount = 0, listCapOrSize = 0;
            if (r6)
            {
                uc_mem_read(MTK, r6 + 0x14, &listCount, 4);
                uc_mem_read(MTK, r6 + 0x18, &listCapOrSize, 4);
                uc_mem_read(MTK, r6 + 0x20, &listBase, 4);
            }
            const char *name = "<non-string>";
            if (vm_net_try_read_ascii(r5, cbeTextString, mySizeOf(cbeTextString)))
                name = (const char *)cbeTextString;
            vm_net_trace("string_table_flow pc=%08x name=%s r5=%08x r6=%08x count=%u cap=%u base=%08x r0=%08x r1=%08x r2=%08x lr=%08x\n",
                         tracePc, name, r5, r6, listCount, listCapOrSize, listBase, r0, r1, r2, lr);
        }
        if (tracePc == 0x1033b9a && r1)
        {
            u8 bytes[64] = {0};
            if (uc_mem_read(MTK, r1, bytes, sizeof(bytes)) == UC_ERR_OK)
                vm_net_trace_bytes("parse_object_input", bytes, sizeof(bytes));
        }
        if ((tracePc == 0x1033c6c || tracePc == 0x1033cac || tracePc == 0x1033cf2 ||
             tracePc == 0x1033d40 || tracePc == 0x1033e56) &&
            r1)
        {
            const char *fieldName = "<non-string>";
            if (vm_net_try_read_ascii(r1, cbeTextString, mySizeOf(cbeTextString)))
                fieldName = (const char *)cbeTextString;
            vm_net_trace("parse_field pc=%08x name=%s mode=%s r1=%08x r2=%08x lr=%08x\n",
                         tracePc,
                         fieldName,
                         tracePc == 0x1033c6c ? "i8" :
                         tracePc == 0x1033cac ? "i16" :
                         tracePc == 0x1033cf2 ? "i32" :
                         tracePc == 0x1033d40 ? "put_i8" : "put_i16",
                         r1,
                         r2,
                         lr);
        }
        if ((tracePc == 0x1033e88 || tracePc == 0x1034018 || tracePc == 0x1034130 ||
             tracePc == 0x1034296 || tracePc == 0x1034412 || tracePc == 0x1034442 ||
             tracePc == 0x1034486 || tracePc == 0x10344c4 || tracePc == 0x10345e6 ||
             tracePc == 0x1034754 || tracePc == 0x1034772) &&
            r1)
        {
            const char *fieldName = "<non-string>";
            if (vm_net_try_read_ascii(r1, cbeTextString, mySizeOf(cbeTextString)))
                fieldName = (const char *)cbeTextString;
            vm_net_trace("object_accessor pc=%08x name=%s r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
                         tracePc, fieldName, r0, r1, r2, r3, lr);
            if (tracePc == 0x1034772)
            {
                u32 target = 0;
                u32 words[12] = {0};
                uc_mem_read(MTK, r0 + 0x10, &target, 4);
                uc_mem_read(MTK, r0, words, sizeof(words));
                vm_net_trace("event_dispatch_call obj=%08x target=%08x args=%08x,%08x,%08x lr=%08x words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                             r0, target, r1, r2, r3, lr,
                             words[0], words[1], words[2], words[3], words[4], words[5],
                             words[6], words[7], words[8], words[9], words[10], words[11]);
            }
        }
        if ((tracePc == 0x1037386 || tracePc == 0x1037388 || tracePc == 0x1037398 ||
             tracePc == 0x10373be || tracePc == 0x10373ce || tracePc == 0x10373da ||
             tracePc == 0x10373ea || tracePc == 0x103743e || tracePc == 0x103745a) &&
            r0)
        {
            u8 bytes[32] = {0};
            if (uc_mem_read(MTK, r0, bytes, sizeof(bytes)) == UC_ERR_OK)
                vm_net_trace_bytes("resource_r0_preview", bytes, sizeof(bytes));
        }
        if (tracePc == 0x1046c20 || tracePc == 0x1046c48 || tracePc == 0x1046c4e || tracePc == 0x1046c58)
        {
            u32 actorTable = 0, imageList = 0, imagePtr = 0;
            short frameIndex = 0;
            if (r7)
                uc_mem_read(MTK, r7 + 0x34, &actorTable, 4);
            if (actorTable)
                uc_mem_read(MTK, actorTable + 0x10, &imageList, 4);
            if (r4)
                uc_mem_read(MTK, r4, &frameIndex, 2);
            if (imageList)
                uc_mem_read(MTK, imageList + ((u16)frameIndex) * 4, &imagePtr, 4);
            vm_net_trace("battle_draw_state pc=%08x r4=%08x r7=%08x frame=%d actorTable=%08x imageList=%08x imagePtr=%08x lr=%08x\n",
                         tracePc, r4, r7, frameIndex, actorTable, imageList, imagePtr, lr);
        }
    }
    // if (address == ROM_ADDRESS + 0x3C72 - 0x98)
    // {
    //     uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    //     printf("logic n2:%x\n", tmp1);
    //     // tmp1 = 4;
    //     // uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    // }
    // if (address == 0x10141D2)
    // {
    //     dumpCpuInfo();
    //     assert(0);
    // }
    if (address == VM_LOG_TRACE_ADDRESS)
    {
        // vm_log_trace only emits firmware-side diagnostics; keep it silent during normal emulation.
        // vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
        // printf("[vm_log_trace]");
        // vm_sprintf();
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
    }
    lastAddress = address;
    // printf("pc:%x\n", address);
}

static u32 irq_inject_count = 0;

bool StartInterrupt(u32 irq_line, u32 lastAddr)
{
    u32 tmp, mode;
    u32 tmp2;
    bool flag = false;
    if (irq_line < 32)
    {
        flag = (IRQ_MASK_SET_L_Data & (1 << irq_line));
    }
    else
    {
        flag = (IRQ_MASK_SET_H_Data & (1 << (irq_line - 32)));
    }
    if (flag)
    {
        tmp = (irq_line << 2);
        uc_mem_write(MTK, 0x34001840, &tmp, 4);
        uc_reg_read(MTK, UC_ARM_REG_CPSR, &tmp);
        if (!isIRQ_Disable(tmp))
        {
            u32 thumb = tmp & 0x20;
            tmp2 = (tmp & 0xFFFFFFE0) | 0x12; // IRQ模式
            tmp2 = tmp2 | 0xC0;               // IRQ/FIQ Disable
            uc_reg_write(MTK, UC_ARM_REG_CPSR, &tmp2);
            uc_reg_write(MTK, UC_ARM_REG_SPSR, &tmp);

            tmp = lastAddr + 4;
            uc_reg_write(MTK, UC_ARM_REG_LR, &tmp);
            uc_reg_write(MTK, UC_ARM_REG_PC, &Interrupt_Handler_Entry);
            return true;
        }
    }
    return false;
}
