#include <signal.h>
#include <stdarg.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

#include "../main.h"
#include "../lcd.h"
#include "../vmEvent.h"
#include "emulator_runtime_internal.h"

/* Core emulator runtime: scheduler, VM service hooks, screen loop, and tracing. */
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
static int g_autoTouchConfigured = 0;
static int g_autoTouchFired = 0;
static u32 g_autoTouchTick = 0;
static int g_autoTouchX = 0;
static int g_autoTouchY = 0;
u32 screenStructChange = 0;
u32 screenStructNotifyLoadRes = 0;
u32 vmAddedScreen = 0;
static u32 g_screenStack[32];
static u32 g_screenStackModuleBase[32];
u32 g_screenStackCount = 0;
u32 g_screenRemovedWithoutNext = 0;
u32 g_screenResumeExisting = 0;
u32 g_activeScreenRemovedThisFrame = 0;
u32 g_currentScreenThis = 0;
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

u32 g_schedulerTick = 0;
static vm_net_task g_netTasks[VM_SCHED_MAX_NET_TASKS];
static vm_net_channel g_netChannels[VM_SCHED_MAX_NET_TASKS];
static int g_netTaskDispatchDepth = 0;
static int g_netTaskDispatchSlot = -1;
static vm_timer_task g_timerTasks[VM_SCHED_MAX_TIMERS];
static u32 g_schedulerStartTicks = 0;
u32 g_nextNetConnectId = 1;
static u8 g_netMockResponse[131072];
static u32 g_netMockResponseLen = 0;
u32 g_netMockResponseOffset = 0;
static u32 g_netMockResponseVmPtr = 0;
static u8 g_netMockUpdateDelivered = 0;
static u32 g_netMockEnterGameOffset = 0;
static u32 g_netMockEnterGameChecksum = 0;
static u8 g_loginVmCodeDumped = 0;
static u8 g_loginVmTouchCodeDumped = 0;
static u8 g_loginVmScreen67cCodeDumped = 0;
static u8 g_loginVmScreen687CodeDumped = 0;
static u8 g_netBusinessSendReadyDeferred = 0;
static u8 g_netBusinessSendReadyRerun = 0;
static u8 g_netBusinessSendReadyPostVm = 0;
static u8 g_loginTail42AllocPending = 0;
static u8 g_loginTail42FlushPending = 0;
u32 g_netUpLinkData = 0;
u32 g_netDownLinkData = 0;
u32 g_netCurrentObject = 0;
static const u32 VM_GAME_NET_BUSINESS_CALLBACK = 0x01012e4d;
u8 g_lastStartupScreenState = 0xff;
static u32 g_lastStartupUpdateObj = 0xffffffff;
static u8 g_lastStartupProgress = 0xff;
static u8 g_lastStartupUpdateState = 0xff;
u32 g_currentFontType = 0;
static u8 g_runtimeTileSkipLogged[256];
static u8 g_runtimeBootstrapTilesLogged = 0;
static u8 g_runtimeBootstrapTilesPatched = 0;
static u8 g_runtimePostPreloadActive = 0;
static u8 g_runtimePostPreloadExitLogged = 0;
static u32 g_runtimePostPreloadStartTick = 0;
static u32 g_runtimePostPreloadLastHeartbeatTick = 0;
static u32 g_runtimePostPreloadSceneTickCount = 0;
static u32 g_runtimePostPreloadStatusDrawCount = 0;
static u32 g_runtimePostPreloadTileDrawCount = 0;
static u32 g_runtimePostPreloadMissingTileCount = 0;
#define VM_CRASH_TRACE_RING_SIZE 128
typedef struct
{
    u32 tick;
    u32 pc;
    u32 lr;
    u32 last;
    u32 flags;
} vm_crash_trace_entry;
static vm_crash_trace_entry g_vmCrashTraceRing[VM_CRASH_TRACE_RING_SIZE];
static u32 g_vmCrashTraceWriteIndex = 0;
static u32 g_vmCrashTraceTotal = 0;
static u8 g_vmCrashTraceDumped = 0;

uc_err add_manager_code_hooks(uc_engine *uc);
void vm_net_trace(const char *fmt, ...);
static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
static uc_err scheduler_dispatch_net_tasks(void);
static uc_err scheduler_flush_post_vm_business_send_ready(const char *reason);
u32 vm_net_mock_env_u32(const char *name, u32 fallback);
static void vm_runtime_trace_post_preload_summary(const char *label);
static void vm_runtime_trace_post_preload_heartbeat(void);
static void vm_runtime_trace_post_preload_pc(u32 tracePc);
static void vm_runtime_trace_bootstrap_point(u32 tracePc);
static bool vm_runtime_skip_missing_tile_draw(u32 tracePc);
static void vm_trace_map_tile_state(const char *label, u32 tileId);
static void vm_crash_trace_record(u32 tracePc);
static void vm_crash_trace_dump(const char *reason);
void vm_install_crash_handlers(void);

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

void vm_trace_lcd_shape(const char *apiName, int x, int y, int w, int h, u32 color)
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

u32 vm_df_get_resource_by_id(u32 id)
{
    return vm_DF_GetResourceByResourceID(id);
}

u32 vm_df_get_resource_by_file_name(u32 namePtr)
{
    return vm_DF_GetResourceByFileName(namePtr);
}

u32 vm_df_get_resource_name_by_id(u32 id)
{
    vm_DF_GetResourceNameByID(id);
    return 0;
}

u32 vm_df_get_resource_id_by_file_name(u32 namePtr)
{
    return vm_DF_GetResourceIDByFileName(namePtr);
}

u32 vm_df_get_t_resource(u32 namePtr, int stream)
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

void vm_screen_stack_push(u32 screen, u32 moduleBase)
{
    if (screen == 0)
        return;

    int existing = vm_screen_stack_find(screen);
    if (existing >= 0)
    {
        for (u32 i = (u32)existing; i + 1 < g_screenStackCount; ++i)
        {
            g_screenStack[i] = g_screenStack[i + 1];
            g_screenStackModuleBase[i] = g_screenStackModuleBase[i + 1];
        }
        g_screenStackCount--;
    }

    if (g_screenStackCount >= sizeof(g_screenStack) / sizeof(g_screenStack[0]))
    {
        memmove(g_screenStack, g_screenStack + 1, (sizeof(g_screenStack) / sizeof(g_screenStack[0]) - 1) * sizeof(g_screenStack[0]));
        memmove(g_screenStackModuleBase, g_screenStackModuleBase + 1, (sizeof(g_screenStackModuleBase) / sizeof(g_screenStackModuleBase[0]) - 1) * sizeof(g_screenStackModuleBase[0]));
        g_screenStackCount--;
    }

    g_screenStack[g_screenStackCount] = screen;
    g_screenStackModuleBase[g_screenStackCount] = moduleBase;
    g_screenStackCount++;
}

bool vm_screen_stack_remove(u32 screen, u32 *newTop, u32 *newTopModuleBase)
{
    int existing = vm_screen_stack_find(screen);
    if (existing < 0)
    {
        if (newTop)
            *newTop = g_screenStackCount ? g_screenStack[g_screenStackCount - 1] : 0;
        if (newTopModuleBase)
            *newTopModuleBase = g_screenStackCount ? g_screenStackModuleBase[g_screenStackCount - 1] : 0;
        return false;
    }

    for (u32 i = (u32)existing; i + 1 < g_screenStackCount; ++i)
    {
        g_screenStack[i] = g_screenStack[i + 1];
        g_screenStackModuleBase[i] = g_screenStackModuleBase[i + 1];
    }
    g_screenStackCount--;

    if (newTop)
        *newTop = g_screenStackCount ? g_screenStack[g_screenStackCount - 1] : 0;
    if (newTopModuleBase)
        *newTopModuleBase = g_screenStackCount ? g_screenStackModuleBase[g_screenStackCount - 1] : 0;
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
    if (err != UC_ERR_OK && g_runtimePostPreloadActive && !g_runtimePostPreloadExitLogged)
    {
        vm_net_trace("post_preload_vm_emu_error entry=%08x until=%08x err=%u last=%08x tick=%u\n",
                     begin, until, err, lastAddress, g_schedulerTick);
        g_runtimePostPreloadExitLogged = 1;
        if (vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
            vm_runtime_trace_post_preload_summary("post_preload_vm_emu_error");
        g_runtimePostPreloadActive = 0;
    }
    normalize_program_exit_pc(begin);
    return err;
}

static uc_err vm_emu_start_count(u32 begin, u32 until, uint64_t count)
{
    g_currentEmuEntry = begin;
    if (Global_R9)
        vm_restore_r9_for_entry(begin);
    uc_err err = uc_emu_start(MTK, begin, until, 0, count);
    if (err != UC_ERR_OK && g_runtimePostPreloadActive && !g_runtimePostPreloadExitLogged)
    {
        vm_net_trace("post_preload_vm_emu_error entry=%08x until=%08x err=%u last=%08x tick=%u count=%llu\n",
                     begin, until, err, lastAddress, g_schedulerTick, (unsigned long long)count);
        g_runtimePostPreloadExitLogged = 1;
        if (vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
            vm_runtime_trace_post_preload_summary("post_preload_vm_emu_error");
        g_runtimePostPreloadActive = 0;
    }
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

uc_err vm_call4_preserve_regs(u32 entry, u32 r0, u32 r1, u32 r2, u32 r3)
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

u32 scheduler_get_tick_ms(void)
{
    u32 now = SDL_GetTicks();
    if (g_schedulerStartTicks == 0)
        g_schedulerStartTicks = now;
    return now - g_schedulerStartTicks;
}

u32 scheduler_start_timer(u32 delayMs, u32 callback, u32 context)
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

u32 scheduler_stop_timer(u32 handle)
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

void scheduler_register_net_channel(u32 connectId, u32 callback, u32 context)
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

void scheduler_unregister_net_channel(u32 connectId)
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

static u32 scheduler_count_active_net_tasks(void)
{
    u32 active = 0;
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netTasks[i].active)
            active++;
    }
    return active;
}

static void scheduler_trace_net_task_slots(const char *label)
{
    char line[512];
    int pos = snprintf(line, sizeof(line), "net_task_slots label=%s tick=%u",
                       label ? label : "?", g_schedulerTick);
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS && pos > 0 && pos < (int)sizeof(line); ++i)
    {
        if (!g_netTasks[i].active)
            continue;
        pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                        " slot%u[e=%u d=%u r1=%08x cb=%08x ctx=%08x]",
                        i,
                        g_netTasks[i].eventType,
                        g_netTasks[i].delayTicks,
                        g_netTasks[i].r1,
                        g_netTasks[i].callback,
                        g_netTasks[i].context);
    }
    if (pos > 0 && pos < (int)sizeof(line) - 1)
        line[pos++] = '\n';
    line[(pos > 0 && pos < (int)sizeof(line)) ? pos : (int)sizeof(line) - 1] = '\0';
    vm_net_trace("%s", line);
}

static void scheduler_queue_net_event(u32 eventType, u32 r0, u32 r1, u32 r2, u32 callback, u32 context)
{
    u32 activeBefore = scheduler_count_active_net_tasks();
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netTasks[i].active && g_netTasks[i].eventType == eventType && g_netTasks[i].r0 == r0 && g_netTasks[i].r1 == r1 && g_netTasks[i].r2 == r2 && g_netTasks[i].callback == callback && g_netTasks[i].context == context)
        {
            vm_net_trace("queue_event_duplicate slot=%u event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x tick=%u pending=%u dispatchDepth=%d dispatchSlot=%d\n",
                         i, eventType, r0, r1, r2, callback, context, g_schedulerTick, activeBefore, g_netTaskDispatchDepth, g_netTaskDispatchSlot);
            return;
        }
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
            vm_net_trace("queue_event slot=%u event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x tick=%u last=%08x pending_before=%u pending_after=%u dispatchDepth=%d dispatchSlot=%d\n",
                         i, eventType, r0, r1, r2, callback, context, g_schedulerTick, lastAddress, activeBefore, activeBefore + 1, g_netTaskDispatchDepth, g_netTaskDispatchSlot);
            if (g_netTaskDispatchDepth > 0 && g_netTaskDispatchSlot >= 0 && (int)i <= g_netTaskDispatchSlot)
                vm_net_trace("queue_event_deferred_next_tick slot=%u currentSlot=%d tick=%u\n", i, g_netTaskDispatchSlot, g_schedulerTick);
            scheduler_trace_net_task_slots("after_queue");
            return;
        }
    }
    vm_net_trace("queue_event_drop_full event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x tick=%u pending=%u\n",
                 eventType, r0, r1, r2, callback, context, g_schedulerTick, activeBefore);
    scheduler_trace_net_task_slots("drop_full");
}

void scheduler_queue_net_task(u32 r0, u32 r1, u32 callback, u32 context)
{
    (void)r0;
    (void)r1;
    scheduler_queue_net_event(5, 0, 0, 0, callback, context);
}

static bool scheduler_is_business_send_ready_phase(void)
{
    u32 businessDispatch = 0;
    u32 currentCallback = 0;
    if (Global_R9)
        uc_mem_read(MTK, Global_R9 + 21812, &businessDispatch, 4);
    if (g_netCurrentObject)
        uc_mem_read(MTK, g_netCurrentObject + 0x14, &currentCallback, 4);
    return businessDispatch == VM_GAME_NET_BUSINESS_CALLBACK ||
           currentCallback == VM_GAME_NET_BUSINESS_CALLBACK;
}

static vm_net_task *scheduler_find_pending_net_event(u32 eventType, u32 callback, u32 context)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (!g_netTasks[i].active)
            continue;
        if (g_netTasks[i].eventType == eventType &&
            g_netTasks[i].callback == callback &&
            g_netTasks[i].context == context)
            return &g_netTasks[i];
    }
    return NULL;
}

static void scheduler_queue_send_ready_for_active_channels(const char *reason)
{
    u32 queued = 0;
    u32 activeChannels = 0;
    bool businessFastPath = scheduler_is_business_send_ready_phase();
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        vm_net_channel *channel = &g_netChannels[i];
        if (!channel->active || !channel->callback)
            continue;
        activeChannels++;
        vm_net_task *pending = scheduler_find_pending_net_event(5, channel->callback, channel->context);
        if (pending)
        {
            if (businessFastPath && pending->delayTicks != 0)
            {
                vm_net_trace("queue_send_ready_accelerate reason=%s slot=%u connect=%u cb=%08x ctx=%08x oldDelay=%u tick=%u\n",
                             reason ? reason : "?", i, channel->connectId, channel->callback, channel->context, pending->delayTicks, g_schedulerTick);
                pending->delayTicks = 0;
            }
            vm_net_trace("queue_send_ready_skip reason=%s slot=%u connect=%u cb=%08x ctx=%08x tick=%u\n",
                         reason ? reason : "?", i, channel->connectId, channel->callback, channel->context, g_schedulerTick);
            if (businessFastPath && g_netTaskDispatchDepth > 0 && pending->delayTicks == 0)
                g_netBusinessSendReadyDeferred = 1;
            if (businessFastPath && g_netTaskDispatchDepth == 0 && pending->delayTicks == 0)
            {
                g_netBusinessSendReadyPostVm = 1;
                vm_net_trace("queue_send_ready_post_vm reason=%s slot=%u connect=%u cb=%08x ctx=%08x tick=%u\n",
                             reason ? reason : "?", i, channel->connectId, channel->callback, channel->context, g_schedulerTick);
            }
            continue;
        }
        vm_net_trace("queue_send_ready reason=%s slot=%u connect=%u cb=%08x ctx=%08x tick=%u dispatchDepth=%d\n",
                     reason ? reason : "?", i, channel->connectId, channel->callback, channel->context, g_schedulerTick, g_netTaskDispatchDepth);
        scheduler_queue_net_event(5, 0, 0, 0, channel->callback, channel->context);
        pending = scheduler_find_pending_net_event(5, channel->callback, channel->context);
        if (pending && businessFastPath)
            pending->delayTicks = 0;
        queued++;
        if (businessFastPath && g_netTaskDispatchDepth > 0)
            g_netBusinessSendReadyDeferred = 1;
    }
    if (activeChannels == 0)
        vm_net_trace("queue_send_ready_miss reason=%s tick=%u\n", reason ? reason : "?", g_schedulerTick);
    else if (queued == 0)
        vm_net_trace("queue_send_ready_all_pending reason=%s active=%u tick=%u\n",
                     reason ? reason : "?", activeChannels, g_schedulerTick);
    else if (businessFastPath)
    {
        vm_net_trace("queue_send_ready_business_fastpath reason=%s active=%u queued=%u tick=%u dispatchDepth=%d\n",
                     reason ? reason : "?", activeChannels, queued, g_schedulerTick, g_netTaskDispatchDepth);
        if (g_netTaskDispatchDepth == 0)
        {
            g_netBusinessSendReadyPostVm = 1;
            vm_net_trace("queue_send_ready_post_vm_business reason=%s active=%u queued=%u tick=%u\n",
                         reason ? reason : "?", activeChannels, queued, g_schedulerTick);
        }
    }
}

static uc_err scheduler_flush_post_vm_business_send_ready(const char *reason)
{
    if (!g_netBusinessSendReadyPostVm || g_netTaskDispatchDepth != 0)
        return UC_ERR_OK;
    g_netBusinessSendReadyPostVm = 0;
    vm_net_trace("post_vm_net_flush reason=%s tick=%u pending=%u\n",
                 reason ? reason : "?", g_schedulerTick, scheduler_count_active_net_tasks());
    uc_err err = scheduler_dispatch_net_tasks();
    if (err != UC_ERR_OK)
        vm_net_trace("post_vm_net_flush_error reason=%s err=%u tick=%u\n",
                     reason ? reason : "?", err, g_schedulerTick);
    return err;
}

static uc_err scheduler_flush_login_tail42_if_needed(const char *reason)
{
    if (!g_loginTail42FlushPending || g_netTaskDispatchDepth != 0)
        return UC_ERR_OK;
    g_loginTail42FlushPending = 0;
    u32 wrapperFlush = 0;
    if (g_netCurrentObject)
        uc_mem_read(MTK, g_netCurrentObject + 0x2c, &wrapperFlush, 4);
    vm_net_trace("login_tail42_flush reason=%s tick=%u pending=%u currentObj=%08x wrapperFlush=%08x\n",
                 reason ? reason : "?", g_schedulerTick, scheduler_count_active_net_tasks(), g_netCurrentObject, wrapperFlush);
    if (!g_netCurrentObject || !wrapperFlush)
    {
        vm_net_trace("login_tail42_flush_skip reason=%s tick=%u currentObj=%08x wrapperFlush=%08x\n",
                     reason ? reason : "?", g_schedulerTick, g_netCurrentObject, wrapperFlush);
        return UC_ERR_OK;
    }
    uc_err err = vm_call4_preserve_regs_clear_stack_args(wrapperFlush, g_netCurrentObject, 0, 0, 0);
    if (err != UC_ERR_OK)
        vm_net_trace("login_tail42_flush_error reason=%s err=%u tick=%u\n",
                     reason ? reason : "?", err, g_schedulerTick);
    return err;
}

static void vm_net_trace_runtime_state(const char *label)
{
    u32 businessDispatch = 0;
    u32 businessCtx = 0;
    u32 sceneObj = 0;
    u32 sceneVtable = 0;
    u8 sceneResult = 0;
    u8 sceneGate = 0;
    u8 loadingFlagA = 0;
    u8 loadingFlagB = 0;
    u8 loadingFlagC = 0;
    u16 sceneState = 0;
    u16 loadingMode = 0;

    uc_mem_read(MTK, Global_R9 + 21812, &businessDispatch, 4);
    uc_mem_read(MTK, Global_R9 + 21804, &businessCtx, 4);
    uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    uc_mem_read(MTK, Global_R9 + 23673, &loadingFlagA, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &loadingFlagB, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &loadingFlagC, 1);
    uc_mem_read(MTK, Global_R9 + 23682, &loadingMode, 2);
    if (sceneObj)
    {
        uc_mem_read(MTK, sceneObj, &sceneVtable, 4);
        uc_mem_read(MTK, sceneObj + 356, &sceneGate, 1);
        uc_mem_read(MTK, sceneObj + 436, &sceneState, 2);
        uc_mem_read(MTK, sceneObj + 1208, &sceneResult, 1);
    }

    vm_net_trace("runtime_state label=%s dispatch=%08x ctx=%08x sceneObj=%08x vtbl=%08x sceneGate=%u sceneState=%u sceneResult=%u loadFlags=%u,%u,%u loadMode=%u activeScreen=%08x currentThis=%08x module=%08x\n",
                 label ? label : "?",
                 businessDispatch,
                 businessCtx,
                 sceneObj,
                 sceneVtable,
                 sceneGate,
                 sceneState,
                 sceneResult,
                 loadingFlagA,
                 loadingFlagB,
                 loadingFlagC,
                 loadingMode,
                 vmAddedScreen,
                 g_currentScreenThis,
                 g_currentScreenModuleBase);
}

static uc_err scheduler_dispatch_net_tasks(void)
{
    u32 activeBefore = scheduler_count_active_net_tasks();
    if (activeBefore)
        scheduler_trace_net_task_slots("dispatch_begin");
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
            u32 taskEvent = task->eventType;
            u32 taskR0 = task->r0;
            u32 taskR1 = task->r1;
            u32 taskR2 = task->r2;
            u32 taskCallback = task->callback;
            u32 taskContext = task->context;
            task->fired = 1;
            task->active = 0;
            g_netTaskDispatchDepth++;
            g_netTaskDispatchSlot = (int)i;
            DEBUG_PRINT("[probe_net_fire] event=%u r0=%x r1=%x r2=%x cb=%x ctx=%x tick=%u\n", taskEvent, taskR0, taskR1, taskR2, taskCallback, taskContext, g_schedulerTick);
            vm_net_trace("fire_event slot=%u event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x tick=%u pending_after_pop=%u\n",
                         i, taskEvent, taskR0, taskR1, taskR2, taskCallback, taskContext, g_schedulerTick, scheduler_count_active_net_tasks());
            if (taskEvent == 7)
            {
                u32 netCb14 = 0;
                u32 netCb44 = 0;
                uc_mem_read(MTK, Global_R9 + 0x9588 + 0x14, &netCb14, 4);
                uc_mem_read(MTK, Global_R9 + 0x9588 + 0x44, &netCb44, 4);
                vm_net_trace("net_state_observe cb14=%08x cb44=%08x\n", netCb14, netCb44);
                vm_net_trace_runtime_state("pre_data_event");
                if (task->downloadSnapshotValid && task->downloadSnapshotState == 2)
                {
                    u32 downloadBase = Global_R9 + 0x9584;
                    uc_mem_write(MTK, downloadBase, task->downloadSnapshot, sizeof(task->downloadSnapshot));
                    vm_net_trace("restore_download_snapshot state=%u base=%08x\n", task->downloadSnapshotState, downloadBase);
                }
            }
            uc_err err = vm_call4_preserve_regs_clear_stack_args(taskCallback, taskR0, taskR1, taskR2, taskEvent);
            g_netTaskDispatchDepth--;
            g_netTaskDispatchSlot = -1;
            if (err != UC_ERR_OK)
                vm_net_trace("net_callback_error event=%u cb=%08x err=%u\n", taskEvent, taskCallback, err);
            if (taskEvent == 7)
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
                vm_net_trace_runtime_state("post_data_event");
                vm_net_trace("after_data_event updateState=%u downloadState=%u updateIndex=%u remain=%u done=%u received=%u total=%u hasLocalUpdate=%u flags=%u,%u,%u,%u\n",
                             updateState, downloadState, updateIndex, updateRemain, updateDoneCount, downloadReceived, downloadTotal, hasLocalUpdate, updateFlags[0], updateFlags[1], updateFlags[2], updateFlags[3]);
            }
            if (err == UC_ERR_OK && g_netTaskDispatchDepth == 0 && g_netBusinessSendReadyPostVm)
            {
                vm_net_trace("net_callback_post_vm_ready event=%u cb=%08x tick=%u pending=%u\n",
                             taskEvent, taskCallback, g_schedulerTick, scheduler_count_active_net_tasks());
                err = scheduler_flush_post_vm_business_send_ready("net_callback_return");
            }
            if (err == UC_ERR_OK && g_netTaskDispatchDepth == 0 && g_loginTail42FlushPending)
                err = scheduler_flush_login_tail42_if_needed("net_callback_return");
            if (g_netTasks[i].active)
                vm_net_trace("fire_event_slot_reused slot=%u event=%u r1=%08x cb=%08x tick=%u\n",
                             i, g_netTasks[i].eventType, g_netTasks[i].r1, g_netTasks[i].callback, g_schedulerTick);
            if (err != UC_ERR_OK)
                return err;
        }
    }
    if (scheduler_count_active_net_tasks())
        scheduler_trace_net_task_slots("dispatch_end");
    if (g_netTaskDispatchDepth == 0 && g_netBusinessSendReadyDeferred && !g_netBusinessSendReadyRerun)
    {
        g_netBusinessSendReadyDeferred = 0;
        g_netBusinessSendReadyRerun = 1;
        vm_net_trace("dispatch_rerun_business_send_ready tick=%u pending=%u\n",
                     g_schedulerTick, scheduler_count_active_net_tasks());
        uc_err rerunErr = scheduler_dispatch_net_tasks();
        g_netBusinessSendReadyRerun = 0;
        if (rerunErr != UC_ERR_OK)
            return rerunErr;
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

typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    const u8 *payload;
    u16 payloadLen;
} vm_net_mock_request_object;

static bool vm_net_mock_next_request_object(const u8 *request, u32 requestLen, u32 *offset, vm_net_mock_request_object *object)
{
    if (request == NULL || offset == NULL || *offset < 4 || *offset + 5 > requestLen)
        return false;

    u32 objectStart = *offset;
    u16 objectLen = (u16)(((u16)request[objectStart + 3] << 8) | request[objectStart + 4]);
    if (objectLen < 5 || objectStart + objectLen > requestLen)
        return false;

    if (object)
    {
        object->major = request[objectStart];
        object->kind = request[objectStart + 1];
        object->subtype = request[objectStart + 2];
        object->payload = request + objectStart + 5;
        object->payloadLen = (u16)(objectLen - 5);
    }
    *offset = objectStart + objectLen;
    return true;
}

static bool vm_net_mock_get_first_object_kind_subtype(const u8 *request, u32 requestLen, u8 *kind, u8 *subtype)
{
    if (request == NULL || requestLen < 11)
        return false;
    if (request[0] != 'W' || request[1] != 'T' || request[4] == 0)
        return false;
    if (kind)
        *kind = request[6];
    if (subtype)
        *subtype = request[7];
    return true;
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

static long vm_net_mock_file_size(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    fclose(fp);
    return size;
}

static u32 vm_net_mock_file_checksum(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    u32 sum = 2166136261u;
    int ch;
    while ((ch = fgetc(fp)) != EOF)
    {
        sum ^= (u8)ch;
        sum *= 16777619u;
    }
    fclose(fp);
    return sum;
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
    long installedSize = vm_net_mock_file_size("JHOnlineData/MMORPGTempcbm");
    long gameSize = vm_net_mock_file_size("JHOnlineData/mmGameMstarWqvga.cbm");
    if (installedSize < 41 || gameSize < 41 ||
        installedSize != gameSize ||
        !vm_net_mock_file_has_min_size("JHOnlineData/mmorpg_updateversioncbm", 40))
    {
        vm_net_trace("mock_update_installed no installedSize=%ld gameSize=%ld version=%d\n",
                     installedSize, gameSize,
                     vm_net_mock_file_has_min_size("JHOnlineData/mmorpg_updateversioncbm", 40) ? 1 : 0);
        return false;
    }

    u32 installedHash = vm_net_mock_file_checksum("JHOnlineData/MMORPGTempcbm");
    u32 gameHash = vm_net_mock_file_checksum("JHOnlineData/mmGameMstarWqvga.cbm");
    bool ok = installedHash == gameHash;
    vm_net_trace("mock_update_installed %s installedSize=%ld gameSize=%ld installedHash=%08x gameHash=%08x\n",
                 ok ? "yes" : "no", installedSize, gameSize, installedHash, gameHash);
    return ok;
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
        "JHOnlineData/mmGameMstarWqvga.cbm",
        "JHOnlineData/mmBattleMstarWqvga.cbm",
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

static u32 vm_net_mock_actor_resource_index(u8 actorJob, u8 actorSex)
{
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    return (u32)(actorJob - 1u) * 2u + (u32)actorSex;
}

static bool vm_net_mock_put_bytes(u8 *out, u32 outCap, u32 *pos, const void *data, u32 len);
static bool vm_net_mock_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value);
static u8 vm_net_mock_env_u8(const char *name, u8 fallback);
static const char *vm_net_mock_env_str(const char *name, const char *fallback);

static const char *vm_net_mock_actor_ui_motion_name(u8 actorJob, u8 actorSex)
{
    static const char *uiActorNames[6] = {
        "ui_h_war.actor",
        "ui_hw_war.actor",
        "ui_h_ass.actor",
        "ui_hw_ass.actor",
        "ui_h_mag.actor",
        "ui_hw_mag.actor",
    };
    u32 tableIndex = vm_net_mock_actor_resource_index(actorJob, actorSex);
    if (tableIndex >= sizeof(uiActorNames) / sizeof(uiActorNames[0]))
        tableIndex = 0;
    return uiActorNames[tableIndex];
}

static bool vm_net_mock_put_le16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    if (*pos + 2 > outCap)
        return false;
    out[(*pos)++] = (u8)(value & 0xff);
    out[(*pos)++] = (u8)(value >> 8);
    return true;
}

static u32 vm_net_mock_build_minimal_actor_motion_resource(u8 *out, u32 outCap)
{
    /*
     * File layout expected by sub_100D48A: little-endian resource payload size,
     * followed by a resource stream.  Type 0xf1 is a mock-only raw DF stream.
     * sub_100D6E2 parses this stream as:
     * skipped dword, width/height, image-entry count, image name, frame bounds,
     * then three optional table counts.  Keep one real image entry so scene
     * rendering owns a valid image object while the unknown tables stay empty.
     */
    u8 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    const char *innerActor = vm_net_mock_env_str("CBE_ACTOR_MOTION_IMAGE_RESOURCE",
                                                 vm_net_mock_actor_ui_motion_name(actorJob, actorSex));
    u32 innerLen = (u32)strlen(innerActor);
    if (innerLen == 0 || innerLen > 0xff)
        return 0;

    u8 descriptor[128];
    u32 descPos = 0;
    memset(descriptor, 0, sizeof(descriptor));
    if (!vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 67) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 293) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 1) ||
        !vm_net_mock_put_u8(descriptor, sizeof(descriptor), &descPos, (u8)innerLen) ||
        !vm_net_mock_put_bytes(descriptor, sizeof(descriptor), &descPos, (const u8 *)innerActor, innerLen) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 67) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 293) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0))
        return 0;

    u32 payloadLen = 1 + descPos;
    u32 fileLen = 4 + payloadLen;
    if (outCap < fileLen)
        return 0;
    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    out[4] = 0xf1;
    memcpy(out + 5, descriptor, descPos);
    vm_net_trace("mock_motion_wrapper job=%u sex=%u inner=%s len=%u\n", actorJob, actorSex, innerActor, fileLen);
    return fileLen;
}

static u32 vm_net_mock_build_minimal_actor_image_resource(u8 *out, u32 outCap)
{
    /*
     * sub_100D564 parses this stream as an image-sequence descriptor:
     * image resource names, cell width/height, sheet width/height, then a
     * cell-index table.  The actual bitmap remains the original game GIF.
     */
    static const u8 descriptor[] = {
        0x01, 0x00, 0x00, 0x00,                         /* image resource count */
        0x12, 'h', '_', 'w', 'a', 'r', 'r', 'i', 'o',
        'r', 'w', 'a', 'l', 'k', '2', '.', 'g', 'i', 'f',
        0x43, 0x00, 0x00, 0x00,                         /* cell width */
        0x25, 0x01, 0x00, 0x00,                         /* cell height */
        0x43, 0x00, 0x00, 0x00,                         /* sheet width */
        0x25, 0x01, 0x00, 0x00,                         /* sheet height */
        0x00, 0x00, 0x00, 0x00,                         /* single cell index */
    };
    u32 payloadLen = 1 + (u32)sizeof(descriptor);
    u32 fileLen = 4 + payloadLen;
    if (outCap < fileLen)
        return 0;
    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    out[4] = 0xf1;
    memcpy(out + 5, descriptor, sizeof(descriptor));
    return fileLen;
}

static u32 vm_net_mock_load_resource_update_payload(const u8 *request, u32 requestLen, u8 *out, u32 outCap, const char **payloadName)
{
    if (payloadName)
        *payloadName = "MMORPGTempcbm";

    if (vm_net_mock_request_contains(request, requestLen, "c_mock_missing_motion.actor") ||
        vm_net_mock_request_contains(request, requestLen, "mock_missing_motion.actor"))
    {
        if (payloadName)
            *payloadName = "c_mock_missing_motion.actor";
        return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);
    }

    if (vm_net_mock_request_contains(request, requestLen, "mock_actor_image.gif"))
    {
        if (payloadName)
            *payloadName = "mock_actor_image.gif";
        return vm_net_mock_build_minimal_actor_image_resource(out, outCap);
    }

    return vm_net_mock_load_update_payload(out, outCap);
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

void vm_net_trace(const char *fmt, ...)
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

static void vm_crash_trace_record(u32 tracePc)
{
    if (!g_runtimePostPreloadActive && !g_runtimeBootstrapTilesLogged)
        return;

    vm_crash_trace_entry *entry = &g_vmCrashTraceRing[g_vmCrashTraceWriteIndex];
    memset(entry, 0, sizeof(*entry));
    entry->tick = g_schedulerTick;
    entry->pc = tracePc;
    entry->last = lastAddress;
    entry->flags = (g_runtimePostPreloadActive ? 1u : 0u) |
                   (g_runtimeBootstrapTilesLogged ? 2u : 0u);
    if (MTK)
        uc_reg_read(MTK, UC_ARM_REG_LR, &entry->lr);

    g_vmCrashTraceWriteIndex = (g_vmCrashTraceWriteIndex + 1) % VM_CRASH_TRACE_RING_SIZE;
    g_vmCrashTraceTotal++;
}

static void vm_crash_trace_dump(const char *reason)
{
    if (g_vmCrashTraceDumped)
        return;

    g_vmCrashTraceDumped = 1;
    vm_net_trace("host_crash_trace_begin reason=%s tick=%u last=%08x postActive=%u bootstrapLogged=%u total=%u writeIndex=%u\n",
                 reason ? reason : "?",
                 g_schedulerTick,
                 lastAddress,
                 g_runtimePostPreloadActive,
                 g_runtimeBootstrapTilesLogged,
                 g_vmCrashTraceTotal,
                 g_vmCrashTraceWriteIndex);

    if (g_runtimePostPreloadActive && vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
        vm_runtime_trace_post_preload_summary(reason ? reason : "host_crash_trace");

    u32 count = g_vmCrashTraceTotal < VM_CRASH_TRACE_RING_SIZE ? g_vmCrashTraceTotal : VM_CRASH_TRACE_RING_SIZE;
    u32 start = (g_vmCrashTraceWriteIndex + VM_CRASH_TRACE_RING_SIZE - count) % VM_CRASH_TRACE_RING_SIZE;
    for (u32 i = 0; i < count; ++i)
    {
        const vm_crash_trace_entry *entry = &g_vmCrashTraceRing[(start + i) % VM_CRASH_TRACE_RING_SIZE];
        vm_net_trace("host_crash_trace[%u] tick=%u pc=%08x lr=%08x last=%08x flags=%u\n",
                     i,
                     entry->tick,
                     entry->pc,
                     entry->lr,
                     entry->last,
                     entry->flags);
    }
    vm_net_trace("host_crash_trace_end reason=%s\n", reason ? reason : "?");
}

static void vm_signal_crash_handler(int sig)
{
    const char *reason = "host_signal_unknown";
    if (sig == SIGABRT)
        reason = "host_signal_abort";
    else if (sig == SIGSEGV)
        reason = "host_signal_segv";
    else if (sig == SIGILL)
        reason = "host_signal_ill";
    else if (sig == SIGFPE)
        reason = "host_signal_fpe";

    vm_net_trace("host_signal sig=%d reason=%s tick=%u last=%08x\n",
                 sig, reason, g_schedulerTick, lastAddress);
    vm_crash_trace_dump(reason);
    signal(sig, SIG_DFL);
    raise(sig);
}

#ifdef _WIN32
static LONG WINAPI vm_unhandled_exception_filter(EXCEPTION_POINTERS *info)
{
    unsigned long code = 0;
    void *addr = NULL;
    if (info && info->ExceptionRecord)
    {
        code = info->ExceptionRecord->ExceptionCode;
        addr = info->ExceptionRecord->ExceptionAddress;
    }
    vm_net_trace("host_exception code=%08lx addr=%p tick=%u last=%08x\n",
                 code, addr, g_schedulerTick, lastAddress);
    vm_crash_trace_dump("host_unhandled_exception");
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void vm_install_crash_handlers(void)
{
    signal(SIGABRT, vm_signal_crash_handler);
    signal(SIGSEGV, vm_signal_crash_handler);
    signal(SIGILL, vm_signal_crash_handler);
    signal(SIGFPE, vm_signal_crash_handler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(vm_unhandled_exception_filter);
#endif
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

void vm_trace_lcd_text(const char *apiName, u32 idx, u32 strPtr, int x, int y, u16 color, const u8 *gbkText)
{
#if TRACE_LCD_TEXT
    if (y < -32 || y > 320)
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

void vm_trace_lcd_text_call(const char *apiName, u32 idx, u32 strPtr, u32 r0, u32 r1, u32 r2, u32 r3, u32 sp, int x, int y, u16 color, const u8 *gbkText)
{
#if TRACE_LCD_TEXT
    if (y < -32 || y > 320)
        return;
    u32 lr = 0;
    u32 st[8] = {0};
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, sp, st, sizeof(st));
    gbk_to_utf8((u8 *)gbkText, sprintfBuff, mySizeOf(sprintfBuff));
    int fullWidth = mesureStringWidth((char *)gbkText);
    int curWidth = mesureStringWidthWithGbkWidth((char *)gbkText, getFontWidth());
    int fontApiWidth = (g_currentFontType == 0) ? getFontCellWidth() : getFontWidth();
    vm_net_trace("lcd_text_call api=%s idx=%u ptr=%08x x=%d y=%d color=%04x fontType=%u fontApiW=%d fullW=%d curW=%d lr=%08x last=%08x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x st=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x text=%s\n",
                 apiName, idx, strPtr, x, y, color, g_currentFontType, fontApiWidth, fullWidth, curWidth, lr, lastAddress,
                 r0, r1, r2, r3, sp, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7], sprintfBuff);
#else
    (void)apiName;
    (void)idx;
    (void)strPtr;
    (void)r0;
    (void)r1;
    (void)r2;
    (void)r3;
    (void)sp;
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

u32 vm_net_mock_env_u32(const char *name, u32 fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(spec, &end, 0);
    if (end == spec)
        return fallback;
    return (u32)parsed;
}

static const char *vm_net_mock_env_str(const char *name, const char *fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    return spec;
}

typedef struct
{
    u32 mapMgr;
    u32 imageList;
    u32 imageTable;
    u32 tileTable;
    u16 tileState;
    u16 tileIndex;
    u32 selectedImage;
} vm_map_tile_state;

static bool vm_read_map_tile_state(u32 tileId, vm_map_tile_state *state)
{
    if (state == NULL)
        return false;
    memset(state, 0, sizeof(*state));
    state->tileIndex = 0xffff;
    if (Global_R9 == 0 || tileId > 0x1ff)
        return false;

    uc_mem_read(MTK, Global_R9 + 39664, &state->mapMgr, 4);
    if (state->mapMgr == 0)
        return false;

    uc_mem_read(MTK, state->mapMgr, &state->imageList, 4);
    uc_mem_read(MTK, state->mapMgr + 4, &state->tileTable, 4);
    if (state->imageList)
        uc_mem_read(MTK, state->imageList + 16, &state->imageTable, 4);
    if (state->tileTable)
    {
        uc_mem_read(MTK, state->tileTable + tileId * 8, &state->tileState, 2);
        uc_mem_read(MTK, state->tileTable + tileId * 8 + 2, &state->tileIndex, 2);
    }
    if (state->imageTable && state->tileIndex != 0xffff)
        uc_mem_read(MTK, state->imageTable + state->tileIndex * 4, &state->selectedImage, 4);
    return true;
}

static bool vm_map_tile_state_missing(const vm_map_tile_state *state)
{
    return state == NULL || state->tileIndex == 0xffff || state->selectedImage == 0;
}

static bool vm_should_log_post_preload_counter(u32 count)
{
    return count <= 4 || (count != 0 && (count & (count - 1)) == 0);
}

static bool vm_copy_map_tile_entry(u32 dstTileId, u32 srcTileId, vm_map_tile_state *srcStateOut)
{
    vm_map_tile_state dstState;
    vm_map_tile_state srcState;
    if (!vm_read_map_tile_state(dstTileId, &dstState) || !vm_read_map_tile_state(srcTileId, &srcState))
        return false;
    if (!dstState.tileTable || !srcState.tileTable || vm_map_tile_state_missing(&srcState))
        return false;

    u8 entry[8] = {0};
    if (uc_mem_read(MTK, srcState.tileTable + srcTileId * 8, entry, sizeof(entry)) != UC_ERR_OK)
        return false;
    if (uc_mem_write(MTK, dstState.tileTable + dstTileId * 8, entry, sizeof(entry)) != UC_ERR_OK)
        return false;
    if (srcStateOut)
        *srcStateOut = srcState;
    return true;
}

static bool vm_backfill_map_tile_from_candidates(const char *label, u32 dstTileId, const u32 *candidateTileIds, u32 candidateCount)
{
    vm_map_tile_state dstState;
    if (!vm_read_map_tile_state(dstTileId, &dstState))
        return false;
    if (!vm_map_tile_state_missing(&dstState))
        return true;

    for (u32 i = 0; i < candidateCount; ++i)
    {
        vm_map_tile_state srcState;
        u32 srcTileId = candidateTileIds[i];
        if (!vm_copy_map_tile_entry(dstTileId, srcTileId, &srcState))
            continue;
        vm_net_trace("tile_backfill label=%s tile=%u fallback=%u fallbackState=%u fallbackIndex=%u fallbackImage=%08x\n",
                     label ? label : "?",
                     dstTileId,
                     srcTileId,
                     srcState.tileState,
                     srcState.tileIndex,
                     srcState.selectedImage);
        return true;
    }
    return false;
}

static void vm_runtime_patch_bootstrap_tiles(void)
{
    if (!vm_net_mock_env_u32("CBE_BACKFILL_BOOTSTRAP_TILES", 1) || g_runtimeBootstrapTilesPatched)
        return;

    static const u32 tile25Fallbacks[] = {24, 27, 28, 39, 40, 41};
    static const u32 tile26Fallbacks[] = {27, 24, 28, 41, 40, 39};

    g_runtimeBootstrapTilesPatched = 1;
    vm_backfill_map_tile_from_candidates("post_tile_preload", 25, tile25Fallbacks, mySizeOf(tile25Fallbacks));
    vm_backfill_map_tile_from_candidates("post_tile_preload", 26, tile26Fallbacks, mySizeOf(tile26Fallbacks));
}

static void vm_runtime_arm_post_preload_window(void)
{
    g_runtimePostPreloadActive = 1;
    g_runtimePostPreloadExitLogged = 0;
    g_vmCrashTraceDumped = 0;
    g_vmCrashTraceWriteIndex = 0;
    g_vmCrashTraceTotal = 0;
    memset(g_vmCrashTraceRing, 0, sizeof(g_vmCrashTraceRing));
    g_runtimePostPreloadStartTick = g_schedulerTick;
    g_runtimePostPreloadLastHeartbeatTick = g_schedulerTick;
    g_runtimePostPreloadSceneTickCount = 0;
    g_runtimePostPreloadStatusDrawCount = 0;
    g_runtimePostPreloadTileDrawCount = 0;
    g_runtimePostPreloadMissingTileCount = 0;

    if (vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
        vm_runtime_trace_post_preload_summary("post_preload_window_armed");
}

static void vm_runtime_trace_post_preload_summary(const char *label)
{
    vm_net_trace("post_preload_summary label=%s tick=%u since=%u sceneTicks=%u statusDraws=%u tileDraws=%u missingTiles=%u\n",
                 label ? label : "?",
                 g_schedulerTick,
                 g_schedulerTick - g_runtimePostPreloadStartTick,
                 g_runtimePostPreloadSceneTickCount,
                 g_runtimePostPreloadStatusDrawCount,
                 g_runtimePostPreloadTileDrawCount,
                 g_runtimePostPreloadMissingTileCount);
    vm_net_trace_runtime_state(label ? label : "post_preload_summary");
}

static void vm_runtime_trace_post_preload_heartbeat(void)
{
    if (!g_runtimePostPreloadActive || !vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
        return;

    if (g_schedulerTick - g_runtimePostPreloadLastHeartbeatTick < 30)
        return;

    g_runtimePostPreloadLastHeartbeatTick = g_schedulerTick;
    vm_runtime_trace_post_preload_summary("post_preload_heartbeat");
}

static void vm_runtime_trace_post_preload_pc(u32 tracePc)
{
    if (!g_runtimePostPreloadActive || !vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
        return;

    if (tracePc == 0x101517A)
    {
        g_runtimePostPreloadSceneTickCount++;
        if (vm_should_log_post_preload_counter(g_runtimePostPreloadSceneTickCount))
            vm_runtime_trace_post_preload_summary("post_preload_scene_tick");
        return;
    }

    if (tracePc == 0x101466A)
    {
        g_runtimePostPreloadStatusDrawCount++;
        if (vm_should_log_post_preload_counter(g_runtimePostPreloadStatusDrawCount))
        {
            vm_runtime_trace_post_preload_summary("post_preload_status_draw");
            vm_trace_map_tile_state("post_preload_status_draw", 25);
            vm_trace_map_tile_state("post_preload_status_draw", 26);
        }
        return;
    }

    if (tracePc == 0x100E8B0)
    {
        u32 tileId = 0;
        u32 lr = 0;
        vm_map_tile_state state;

        uc_reg_read(MTK, UC_ARM_REG_R0, &tileId);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        g_runtimePostPreloadTileDrawCount++;

        if (!vm_read_map_tile_state(tileId, &state))
        {
            vm_net_trace("post_preload_tile_draw tile=%u unreadable lr=%08x count=%u\n",
                         tileId, lr, g_runtimePostPreloadTileDrawCount);
            return;
        }

        if (vm_map_tile_state_missing(&state))
            g_runtimePostPreloadMissingTileCount++;

        if (vm_map_tile_state_missing(&state) || g_runtimePostPreloadTileDrawCount <= 24)
        {
            vm_net_trace("post_preload_tile_draw tile=%u state=%u tileIndex=%u selectedImage=%08x lr=%08x count=%u missing=%u\n",
                         tileId,
                         state.tileState,
                         state.tileIndex,
                         state.selectedImage,
                         lr,
                         g_runtimePostPreloadTileDrawCount,
                         vm_map_tile_state_missing(&state) ? 1 : 0);
        }
    }
}

static void vm_trace_map_tile_state(const char *label, u32 tileId)
{
    vm_map_tile_state state;
    if (!vm_read_map_tile_state(tileId, &state))
    {
        vm_net_trace("tile_state label=%s tile=%u unavailable r9=%08x\n",
                     label ? label : "?", tileId, Global_R9);
        return;
    }

    vm_net_trace("tile_state label=%s tile=%u state=%u tileIndex=%u selectedImage=%08x mapMgr=%08x tileTable=%08x imageTable=%08x\n",
                 label ? label : "?",
                 tileId,
                 state.tileState,
                 state.tileIndex,
                 state.selectedImage,
                 state.mapMgr,
                 state.tileTable,
                 state.imageTable);
}

static void vm_runtime_trace_bootstrap_point(u32 tracePc)
{
    if (tracePc != 0x10137BA)
        return;

    vm_runtime_patch_bootstrap_tiles();

    if (vm_net_mock_env_u32("CBE_TRACE_BOOTSTRAP_TILES", 0) && !g_runtimeBootstrapTilesLogged)
    {
        g_runtimeBootstrapTilesLogged = 1;
        vm_net_trace_runtime_state("post_tile_preload");
        vm_trace_map_tile_state("post_tile_preload", 25);
        vm_trace_map_tile_state("post_tile_preload", 26);
    }

    vm_runtime_arm_post_preload_window();
}

static bool vm_runtime_skip_missing_tile_draw(u32 tracePc)
{
    if (tracePc != 0x100E8B0 || !vm_net_mock_env_u32("CBE_SKIP_MISSING_TILE_DRAW", 0))
        return false;

    u32 tileId = 0;
    uc_reg_read(MTK, UC_ARM_REG_R0, &tileId);

    if (tileId == 25 || tileId == 26)
        vm_runtime_patch_bootstrap_tiles();

    vm_map_tile_state state;
    if (!vm_read_map_tile_state(tileId, &state) || !vm_map_tile_state_missing(&state))
        return false;

    u32 lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (tileId < sizeof(g_runtimeTileSkipLogged))
    {
        if (!g_runtimeTileSkipLogged[tileId])
        {
            g_runtimeTileSkipLogged[tileId] = 1;
            vm_net_trace("skip_missing_tile_draw tile=%u state=%u tileIndex=%u selectedImage=%08x lr=%08x caller=%08x\n",
                         tileId, state.tileState, state.tileIndex, state.selectedImage, lr, lastAddress);
        }
    }
    else
    {
        vm_net_trace("skip_missing_tile_draw tile=%u state=%u tileIndex=%u selectedImage=%08x lr=%08x caller=%08x\n",
                     tileId, state.tileState, state.tileIndex, state.selectedImage, lr, lastAddress);
    }

    u32 zero = 0;
    uc_reg_write(MTK, UC_ARM_REG_R0, &zero);
    vm_bx(lr);
    return true;
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
    const char *payloadName = NULL;
    u32 updateLen = vm_net_mock_load_resource_update_payload(request, requestLen, updateData, sizeof(updateData), &payloadName);
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

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", updateLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", (const u8 *)"\x00\x01", 2))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName ? payloadName : "MMORPGTempcbm"))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", updateData + start, (u16)chunkLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 1);
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 7;
    out[8] = 0;
    vm_net_mock_finish_wt_object(out, 5, pos);
    vm_net_trace("mock_update_chunk_response name=%s start=%u chunk=%u totalsize=%u crc=%u\n",
                 payloadName ? payloadName : "MMORPGTempcbm", start, chunkLen, updateLen, crc);
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

static const char *vm_net_mock_default_scene_name(void)
{
    return "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31"; /* GBK: c00PengLaiXianDao_01 */
}

static bool vm_net_mock_put_scene_fields(u8 *out, u32 outCap, u32 *pos, bool includeResult, bool includeType, u8 requestType)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), 120, 120);
    if (posInfoLen == 0)
        return false;
    if (includeResult && !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (includeType && !vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", vm_net_mock_default_scene_name()))
        return false;
    return vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen);
}

static bool vm_net_mock_append_group_info_object(u8 *out, u32 outCap, u32 *pos, u32 leadId)
{
    static const u8 emptyGroupInfo[] = {0};
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "num", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", emptyGroupInfo, 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "leadid", leadId))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_type1_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x1a, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", 0))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name", "Codex"))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "money", 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_group_type1_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen < 9 || outCap < 5)
        return 0;

    u32 leadId = 10001;
    bool hasGroup10 = false;
    bool hasType1 = false;
    u32 offset = 4;
    vm_net_mock_request_object object;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.kind == 5 && object.subtype == 10)
        {
            hasGroup10 = true;
            vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &leadId);
        }
        else if (object.kind == 7 && object.subtype == 7)
        {
            u8 requestType = 0;
            if (vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &requestType) && requestType == 1)
                hasType1 = true;
        }
    }

    if (!hasGroup10)
        return 0;

    u32 pos = 5;
    if (!vm_net_mock_append_group_info_object(out, outCap, &pos, leadId))
        return 0;
    if (hasType1 && !vm_net_mock_append_type1_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, hasType1 ? 2 : 1);
    vm_net_trace("mock_group_type1_response leadid=%u objects=%u len=%u\n", leadId, hasType1 ? 2u : 1u, pos);
    return pos;
}

static u32 vm_net_mock_build_scene_change_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields(out, outCap, &pos, true, true, 2))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_scene_change_response scene=c00PenglaiXiandao_01 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_scene_default_event_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 12, &objectStart))
        return 0;
    /*
     * scene_runtime_tick() sends the empty 0x19/5 scene-default event request.
     * The static 0x19 dispatcher only handles subtype 11/12 replies, so do not
     * bounce the request back unchanged. A minimal subtype-12/result=4 reply
     * keeps the local scene-notify state machine on its "clear/no popup" path.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_scene_default_event_response result=4 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_short_wt_control_echo_response(const u8 *request, u32 requestLen,
                                                            u8 kind, u8 subtype,
                                                            u8 *out, u32 outCap)
{
    /*
     * 0x63/1 sits on the startup/login bridge. Dropping it leaves the progress
     * spinner running forever; while the exact protocol is still unknown, the
     * client does continue when it receives a short control ack back.
     */
    vm_net_trace("mock_short_wt_control_echo kind=%u subtype=%u len=%u\n",
                 kind, subtype, requestLen);
    return vm_net_mock_copy_response(request, requestLen, out, outCap);
}

static bool vm_net_mock_is_login_tail_skill_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen != 14)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;

    u32 offset = 4;
    vm_net_mock_request_object object;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x0c || object.subtype != 1 || object.payloadLen != 0)
        return false;

    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42 || object.payloadLen != 0)
        return false;

    return offset == requestLen;
}

static u32 vm_net_mock_build_login_tail_skill_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x0c, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "learnednum", 0))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "learnedskill", NULL, 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 42, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "booknum", 0))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "booksinfo", NULL, 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_login_tail_skill_response learnednum=0 learnedskill=0 booknum=0 booksinfo=0 len=%u\n", pos);
    return pos;
}

static bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen, u8 kind, u8 subtype)
{
    if (request == NULL || requestLen != 9)
        return false;
    return request[0] == 'W' &&
           request[1] == 'T' &&
           request[2] == 0 &&
           request[3] == 9 &&
           request[4] == 1 &&
           request[5] == kind &&
           request[6] == subtype &&
           request[7] == 0 &&
           request[8] == 5;
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
    u32 tableIndex = vm_net_mock_actor_resource_index(actorJob, actorSex);
    if (tableIndex >= sizeof(actorNames) / sizeof(actorNames[0]))
        tableIndex = 0;
    return actorNames[tableIndex];
}

static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
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
        vm_net_mock_env_u32("CBE_ACTOR_SCENE_STATE", 0),
        vm_net_mock_env_u32("CBE_ACTOR_SCENE_ARG", 0),
        vm_net_mock_env_u32("CBE_ACTOR_POS_X", 120),
        vm_net_mock_env_u32("CBE_ACTOR_POS_Y", 120),
        vm_net_mock_env_u32("CBE_ACTOR_HP", 100),
        vm_net_mock_env_u32("CBE_ACTOR_HP_MAX", 100),
        vm_net_mock_env_u32("CBE_ACTOR_MP", 50),
        vm_net_mock_env_u32("CBE_ACTOR_MP_MAX", 50),
        vm_net_mock_env_u32("CBE_ACTOR_MONEY", 0),
        vm_net_mock_env_u32("CBE_ACTOR_EXP", 0),
        vm_net_mock_env_u32("CBE_ACTOR_SLOT_STATE", 0),
        vm_net_mock_env_u32("CBE_ACTOR_FACTION_FALLBACK", 0),
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
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
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
    const char *motionResource = vm_net_mock_env_str("CBE_ACTOR_MOTION_RESOURCE", "mock_missing_motion.actor");
    u16 actorResourceArg = (u16)vm_net_mock_env_u32("CBE_ACTOR_RESOURCE_ARG", 0);
    /*
     * parse_actorinfo_response stores these trailing i16 values at actor+0xF0/+0xF4,
     * and sub_1012FB4 passes them directly into sub_100F7A6(mode=1).
     * The alternate local branch in sub_100F7A6 uses a built-in 50,50 fallback,
     * so keep the default mock data aligned with that baseline unless overridden.
     */
    u16 motionResourceArg0 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG0", 50);
    u16 motionResourceArg1 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG1", 50);
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, actorResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, actorResourceArg))
        return 0;
    /*
     * This second resource is parsed by sub_100D6E2 as a motion descriptor.
     * The normal .actor packages are image-sequence resources, so provide a
     * small server-side descriptor and let CBE load the referenced real images.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, motionResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg0))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg1))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_role_list_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
    u32 roleId = 10001;
    u32 roleLevel = vm_net_mock_env_u8("CBE_ROLE_LEVEL", 1);

    if (roleLevel == 0)
        roleLevel = 1;

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleLevel))
        return 0;

    return pos;
}

static bool vm_net_mock_append_actorinfo_object(u8 *out, u32 outCap, u32 *pos, u32 *actorInfoLenOut)
{
    u8 actorInfo[512];
    u8 actorSubtype = vm_net_mock_env_u8("CBE_LOGIN_TOP_TYPE", 6);
    memset(actorInfo, 0, sizeof(actorInfo));
    u32 actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
    if (actorInfoLen == 0)
        return false;
    if (actorSubtype != 2 && actorSubtype != 3 && actorSubtype != 6)
        actorSubtype = 6;

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, actorSubtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "revivetype", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "ruffianflag", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 0))
        return false;
    if (actorSubtype == 6)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "practiseflag", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expbook", 0))
            return false;
        if (!vm_net_mock_put_object_string(out, outCap, pos, "practiseinfo", ""))
            return false;
    }
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", 0))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    return true;
}

static bool vm_net_mock_append_role_list_object(u8 *out, u32 outCap, u32 *pos)
{
    u8 roleInfo[128];
    memset(roleInfo, 0, sizeof(roleInfo));
    u32 roleInfoLen = vm_net_mock_build_role_list_info(roleInfo, sizeof(roleInfo));
    if (roleInfoLen == 0)
        return false;

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x20, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roles", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "maxroles", 3))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "allpgs", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "num", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "roleinfo", roleInfo, (u16)roleInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_role_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_role_list_response roles=1 maxroles=3 num=1 allpgs=1 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_login_actor_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;

    g_netMockEnterGameOffset = 0;
    g_netMockEnterGameChecksum = 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_actorinfo_object(out, outCap, &pos, &actorInfoLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    u8 actorSubtype = vm_net_mock_env_u8("CBE_LOGIN_TOP_TYPE", 6);
    if (actorSubtype != 2 && actorSubtype != 3 && actorSubtype != 6)
        actorSubtype = 6;
    u8 logJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 logSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    vm_net_trace("mock_login_actor_response actorinfo_len=%u top=1,1,%u job=%u sex=%u actor=%s len=%u\n",
                 actorInfoLen,
                 actorSubtype,
                 logJob,
                 logSex,
                 vm_net_mock_actor_resource_name(logJob, logSex),
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_role_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;

    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;
    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_actorinfo_object(out, outCap, &pos, &actorInfoLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    u8 actorSubtype = vm_net_mock_env_u8("CBE_LOGIN_TOP_TYPE", 6);
    if (actorSubtype != 2 && actorSubtype != 3 && actorSubtype != 6)
        actorSubtype = 6;
    vm_net_trace("mock_login_role_list_response order=rolelist-first actorinfo_len=%u top=1,1,%u roles=1 maxroles=3 num=1 allpgs=1 len=%u\n",
                 actorInfoLen, actorSubtype, pos);
    return pos;
}

static u32 vm_net_mock_build_login_response(u8 *out, u32 outCap)
{
    const char *mode = getenv("CBE_LOGIN_RESPONSE");
    if (mode && strcmp(mode, "roles") == 0)
        return vm_net_mock_build_login_role_list_response(out, outCap);
    return vm_net_mock_build_login_actor_response(out, outCap);
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
    u8 responseType = 0x0a;
    u8 responseSub = request[7];
    if (requestType == 1)
        responseSub = 0x1a;
    else if (requestType == 2 || requestType == 3)
    {
        /*
         * Keep the later explicit maptype/mapID/exitID request as the first
         * actual scene transition. Returning scene 0x1e packets here advances
         * the local scene object too early, then clears sceneGate before the
         * default-scene 0x19/12 reply arrives.
         */
        responseType = 7;
        responseSub = (requestType == 2) ? 20 : 32;
    }

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (requestType == 1)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", requestType))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "npcnum", 0))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", "Codex"))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "money", 0))
            return 0;
    }
    else if (requestType == 2)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "pcimg", 0))
            return 0;
    }
    else if (requestType == 3)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "expcard", 0))
            return 0;
    }

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = request[4];
    out[5] = request[5];
    out[6] = responseType;
    out[7] = responseSub;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    vm_net_trace("mock_game_type_response type=%u responseType=%u responseSub=%u scene=c00PenglaiXiandao_01 header=%02x,%02x,%02x,%02x len=%u\n",
                 requestType, responseType, responseSub, out[4], out[5], out[6], out[7], pos);
    return pos;
}

static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen == 0 || outCap == 0)
        return 0;

    /* First principle for protocol research: only emulate server/API behavior.
     * Do not advance CBE state here by forcing return values or writing globals.
     */
    u32 hookedLen = 0;

    if (vm_net_mock_request_contains(request, requestLen, "start") &&
        (vm_net_mock_request_contains(request, requestLen, "id") ||
         vm_net_mock_request_contains(request, requestLen, "name")))
    {
        hookedLen = vm_net_mock_build_update_chunk_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-update-chunk len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_response_from_rules(request, requestLen, out, outCap);
    if (hookedLen)
        return hookedLen;

    if (vm_net_mock_request_contains(request, requestLen, "maptype") &&
        vm_net_mock_request_contains(request, requestLen, "mapID") &&
        vm_net_mock_request_contains(request, requestLen, "exitID"))
    {
        hookedLen = vm_net_mock_build_scene_change_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-scene-change len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-scene-default-event len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x63, 1))
    {
        hookedLen = vm_net_mock_build_short_wt_control_echo_response(request, requestLen, 0x63, 1, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-short-63-1-echo len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_login_tail_skill_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_login_tail_skill_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-login-tail-skill len=%u\n", hookedLen);
            return hookedLen;
        }
    }

    u8 requestKind = 0;
    u8 requestSubtype = 0;
    if (vm_net_mock_get_first_object_kind_subtype(request, requestLen, &requestKind, &requestSubtype))
    {
        if (requestKind == 0x0a && requestSubtype == 0x20)
        {
            hookedLen = vm_net_mock_build_role_list_response(out, outCap);
            if (hookedLen)
            {
                vm_net_trace("mock_default source=builtin-role-list len=%u\n", hookedLen);
                return hookedLen;
            }
        }
    }

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

    u8 gameRequestType = 0;
    hookedLen = vm_net_mock_build_group_type1_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-group-type1 len=%u\n", hookedLen);
        return hookedLen;
    }

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

void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
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
    bool immediateFlushAfterData = vm_net_mock_is_short_wt_control_packet(request, readLen, 0x19, 5);

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
        if (immediateFlushAfterData && g_netTaskDispatchDepth == 0)
        {
            vm_net_trace("queue_data_event_immediate_flush connect=%u cb=%08x ctx=%08x len=%u tick=%u\n",
                         connectId, channel->callback, channel->context, g_netMockResponseLen, g_schedulerTick);
            uc_err flushErr = scheduler_dispatch_net_tasks();
            if (flushErr != UC_ERR_OK)
                vm_net_trace("queue_data_event_immediate_flush_error err=%u\n", flushErr);
        }
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
        vm_net_trace("touch_dispatch entry=%08x screen=%08x type=%u x=%u y=%u path=tscreen\n",
                     tScreenEventEntry, screenPtr, evt->r0, simulateTouchX, simulateTouchY);
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

static uc_err scheduler_tick(void)
{
    g_schedulerTick++;
    vm_runtime_trace_post_preload_heartbeat();
    currentTime = clock();
    uc_err err = scheduler_dispatch_timers();
    if (err != UC_ERR_OK)
    {
        if (g_runtimePostPreloadActive && !g_runtimePostPreloadExitLogged)
        {
            vm_net_trace("post_preload_scheduler_error phase=timers err=%u last=%08x tick=%u\n",
                         err, lastAddress, g_schedulerTick);
            g_runtimePostPreloadExitLogged = 1;
            if (vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
                vm_runtime_trace_post_preload_summary("post_preload_scheduler_error");
            g_runtimePostPreloadActive = 0;
        }
        return err;
    }
    err = scheduler_dispatch_net_tasks();
    if (err != UC_ERR_OK && g_runtimePostPreloadActive && !g_runtimePostPreloadExitLogged)
    {
        vm_net_trace("post_preload_scheduler_error phase=net err=%u last=%08x tick=%u\n",
                     err, lastAddress, g_schedulerTick);
        g_runtimePostPreloadExitLogged = 1;
        if (vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
            vm_runtime_trace_post_preload_summary("post_preload_scheduler_error");
        g_runtimePostPreloadActive = 0;
    }
    return err;
}

/**
 * @brief 按键事件
 * @param type 4=按下 5=松开
 * @param key 按键值
 */
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
    u8 screenImageHeader[24] = {0};
    changeTmp1 = VM_screenImage_ADDRESS;
    memcpy(screenImageHeader, &changeTmp1, 4);
    u16 screenImageWidth = LCD_WIDTH;
    u16 screenImageHeight = LCD_HEIGHT;
    memcpy(screenImageHeader + 4, &screenImageWidth, 2);
    memcpy(screenImageHeader + 6, &screenImageHeight, 2);
    screenImageHeader[8] = 0;
    uc_mem_write(MTK, VM_screenImageStruct_ADDRESS, screenImageHeader, sizeof(screenImageHeader));
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
                p = scheduler_flush_post_vm_business_send_ready("tscreen_event");
                if (p != UC_ERR_OK)
                    break;
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
                if (screenInitEntry)
                    p = vm_emu_start(screenInitEntry, exitAddr);
                else
                    p = UC_ERR_OK;
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
                        if (screenResouceLoadEntry)
                        {
                            p = vm_emu_start(screenResouceLoadEntry, exitAddr);
                            if (p != UC_ERR_OK)
                            {
                                printf("SCR_ResourceLoad异常:%s\n", uc_strerror(p));
                                assert(0);
                            }
                        }
                        else
                        {
                            vm_net_trace("screen_resource_load_skip_null screen=%08x this=%08x\n",
                                         screenFuncPtr, screenThisPtr);
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
                                vm_net_trace("touch_dispatch entry=%08x screen=%08x type=%u x=%u y=%u path=logic pool=%u\n",
                                             screenLogicEntry, screenThisPtr, evt->r0, simulateTouchX, simulateTouchY,
                                             vm_is_pool_entry(screenLogicEntry) ? 1 : 0);
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
                                p = scheduler_flush_post_vm_business_send_ready("screen_logic_input");
                                if (p != UC_ERR_OK)
                                    break;
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
                            p = scheduler_flush_post_vm_business_send_ready("screen_logic");
                            if (p != UC_ERR_OK)
                                break;
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
        vm_input_reset();
        vm_set_call_result(0);
    }
    else if (idx == 46)
    {
        vm_set_call_result(vm_input_is_open());
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
    return vm_manager_lcd_handle(address);
}

static bool hook_vm_manager_fileio_func(u32 address)
{
    return vm_manager_fileio_handle(address);
}

static bool hook_vm_manager_stdio_func(u32 address)
{
    return vm_manager_stdio_handle(address);
}

static bool hook_vm_manager_timer_func(u32 address)
{
    return vm_manager_timer_handle(address);
}

static bool hook_vm_manager_ctrl_func(u32 address)
{
    return vm_manager_ctrl_handle(address);
}

static bool hook_vm_manager_network_func(u32 address)
{
    return vm_manager_network_handle(address);
}

static bool hook_vm_manager_game_util_func(u32 address)
{
    return vm_manager_game_util_handle(address);
}

static bool hook_vm_manager_df_engine_func(u32 address)
{
    return vm_manager_df_engine_handle(address);
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
    return vm_manager_screen_handle(address);
}

static bool hook_vm_manager_df_script_func(u32 address)
{
    return vm_manager_df_script_handle(address);
}

static bool hook_vm_manager_game_lcd_func(u32 address)
{
    return vm_manager_game_lcd_handle(address);
}

static bool hook_vm_manager_netapp_func(u32 address)
{
    return vm_manager_netapp_handle(address);
}

static bool hook_vm_manager_audio_func(u32 address)
{
    return vm_manager_audio_handle(address);
}

static bool hook_vm_manager_sensor_func(u32 address)
{
    return vm_manager_sensor_handle(address);
}

static bool hook_vm_manager_vmim_func(u32 address)
{
    return vm_manager_vmim_handle(address);
}

static bool hook_vm_manager_gameold_func(u32 address)
{
    return vm_manager_gameold_handle(address);
}

static bool vm_runtime_stub_manager_call(const char *label, const char *envName, u32 address, u32 base, u32 count)
{
    if (!(address >= base && address < (base + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 idx = (address - base) / 4;
    u32 tmp1 = 0, tmp2 = 0, tmp3 = 0, tmp4 = 0, tmp5 = 0;

    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
    uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp5);

    if (count == 0 || idx < count)
    {
        if (!vm_net_mock_env_u32(envName, 1))
        {
            printf("[call]%s idx=%u\n", label, idx);
            assert(0);
        }

        vm_net_trace("%s_stub idx=%u addr=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x last=%08x tick=%u\n",
                     label, idx, address, tmp1, tmp2, tmp3, tmp4, tmp5, lastAddress, g_schedulerTick);
        vm_set_call_result(0);
        vm_bx(tmp5);
        return true;
    }

    printf("[impl]%s调用位置:%u\n", label, idx);
    assert(0);
    return true;
}

static bool hook_vm_df_datapackage_func(u32 address)
{
    return vm_runtime_stub_manager_call("vmDfDataPackageManager", "CBE_STUB_DF_DATAPACKAGE_MANAGER", address, VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS, 1);
}

static bool hook_vm_mf_memoryblock_func(u32 address)
{
    return vm_runtime_stub_manager_call("vmMfMemoryBlockManager", "CBE_STUB_MEMORYBLOCK_MANAGER", address, VM_MF_MemoryBlock_FUNC_LIST_ADDRESS, 16);
}

static bool hook_vm_appstore_func(u32 address)
{
    return vm_appstore_handle(address);
}

static bool hook_vm_dl_load_func(u32 address)
{
    return vm_dl_load_handle(address);
}

static bool hook_vm_dl_pay_func(u32 address)
{
    return vm_dl_pay_handle(address);
}

static bool hook_vm_dl_rs_func(u32 address)
{
    return vm_dl_rs_handle(address);
}

static bool hook_vm_dl_image_func(u32 address)
{
    return vm_dl_image_handle(address);
}

static bool hook_vm_video_func(u32 address)
{
    return vm_video_handle(address);
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
    lastAddress = (u32)address;
    hook_vm_video_func((u32)address);
}

uc_err add_manager_code_hooks(uc_engine *uc)
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
    u32 tracePc = (u32)address & ~1u;

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
    vm_crash_trace_record(tracePc);
    if (tracePc == PROGRAM_EXIT_ADDR)
    {
        if (g_runtimePostPreloadActive && !g_runtimePostPreloadExitLogged)
        {
            vm_net_trace("post_preload_program_exit last=%08x tick=%u\n", lastAddress, g_schedulerTick);
            g_runtimePostPreloadExitLogged = 1;
            if (vm_net_mock_env_u32("CBE_TRACE_POST_PRELOAD", 1))
                vm_runtime_trace_post_preload_summary("post_preload_program_exit");
            g_runtimePostPreloadActive = 0;
        }
        normalize_program_exit_pc(g_currentEmuEntry);
        uc_emu_stop(MTK);
        return;
    }
    vm_runtime_trace_bootstrap_point(tracePc);
    vm_runtime_trace_post_preload_pc(tracePc);
    if (vm_runtime_skip_missing_tile_draw(tracePc))
    {
        lastAddress = tracePc;
        return;
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
    if (tracePc == VM_LOG_TRACE_ADDRESS)
    {
        // vm_log_trace only emits firmware-side diagnostics; keep it silent during normal emulation.
        // vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
        printf("[vm_log_trace]");
        vm_sprintf();
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
    }
    lastAddress = tracePc;
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



