#define GDB_SERVER_SUPPORT_
#define GDI_LAYER_DEBUG_

#define DEBUG_PRINT(...) ((void)0)

#ifdef _WIN32
#include <direct.h>
#endif
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

#include "main.h"
#include "lcd.h"
static bool vm_file_try_download_named_resource(const char *normalizedPath);
#include "vmFunc.c"
#include "hookRam.c"
#include "vmEvent.c"

#if defined(GDB_SERVER_SUPPORT) && !defined(CBE_PLATFORM_ANDROID)
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
static u32 g_curKeyDownState = 0;
static u32 g_curKeyState = 0;

static u32 vm_key_mask_from_code(int key)
{
    return (key >= 0 && key < 31) ? (1u << key) : 0;
}

static void vm_clear_key_down_state(void)
{
    g_curKeyDownState = 0;
}

static void vm_note_key_state_event(int key, int isPress)
{
    u32 mask = vm_key_mask_from_code(key);
    if (mask == 0)
        return;

    if (isPress)
    {
        g_curKeyDownState |= mask;
        g_curKeyState |= mask;
    }
    else
    {
        g_curKeyState &= ~mask;
    }
}

static u32 vm_fileio_sdcard_status(void)
{
    return 1;
}

static u32 vm_fileio_free_space(void)
{
    return 0x10000000u;
}
typedef enum
{
    VM_AUTOTEST_ACTION_TAP,
    VM_AUTOTEST_ACTION_WINDOW_TAP,
    VM_AUTOTEST_ACTION_KEY,
    VM_AUTOTEST_ACTION_HOLD_KEY
} vm_autotest_action_type;

typedef struct
{
    u32 atMs;
    vm_autotest_action_type type;
    int a;
    int b;
    int fired;
} vm_autotest_action;

static int g_autotestEnabled = 0;
static u32 g_autotestStartMs = 0;
static u32 g_autotestNextShotMs = 0;
static u32 g_autotestShotIntervalMs = 1000;
static u32 g_autotestMaxMs = 0;
static u32 g_autotestShotIndex = 0;
static vm_autotest_action g_autotestActions[64];
static u32 g_autotestActionCount = 0;
static int g_autotestTapReleasePending = 0;
static int g_autotestTapReleaseWindow = 0;
static u32 g_autotestTapReleaseMs = 0;
static int g_autotestTapReleaseX = 0;
static int g_autotestTapReleaseY = 0;
static int g_autotestKeyReleasePending = 0;
static u32 g_autotestKeyReleaseMs = 0;
static int g_autotestKeyReleaseSym = 0;
static FILE *g_autotestStateFile = NULL;
static int g_vmInputOpen = 0;
static int g_vmInputPassword = 0;
static u32 g_vmInputSerial = 0;
static u32 g_vmInputCallback = 0;
static u32 g_vmInputBuffer = 0;
static u32 g_vmInputTargetBuffer = 0;
static u32 g_vmInputMaxLen = 0;
static u32 g_vmInputInputType = 0;
static u32 g_vmInputPrompt = 0;
static u32 g_vmInputScratchBuffer = 0;
static u32 g_vmInputScratchBytes = 0;
static int g_vmInputOverlayX = 12;
static int g_vmInputOverlayY = 348;
static int g_vmInputOverlayW = 216;
static int g_vmInputOverlayH = 22;
static char g_vmInputComposition[64];
static int g_vmInputSdlTextInputWanted = 0;
static int g_vmInputSdlTextInputActive = 0;
u32 g_vmInputWatchUserBuf = 0;
u32 g_vmInputWatchUserBufLen = 0;
u32 g_vmInputWatchCallback = 0;
u32 g_vmInputWatchCallR9 = 0;
u32 g_vmInputWatchWriteCount = 0;
u32 screenStructChange = 0;
u32 screenStructNotifyLoadRes = 0;
u32 vmAddedScreen = 0;
static u32 g_screenStack[32];
static u32 g_screenStackParam[32];
static u32 g_screenStackModuleBase[32];
static u32 g_screenStackDataPackage[32];
static u8 g_screenStackFlags[32];
static u8 g_screenStackInited[32];
static u32 g_screenStackCount = 0;
static u32 g_screenRemovedWithoutNext = 0;
static u32 g_screenResumeExisting = 0;
static u32 g_screenEnterExistingNoCallback = 0;
static u32 g_activeScreenRemovedThisFrame = 0;
static u32 g_activeScreenRemovedThis = 0;
static u32 g_activeScreenRemovedModuleBase = 0;
static u32 g_activeScreenRemovedDataPackage = 0;
static u32 g_screenExitMode = 0;
static u32 g_screenLoadResourcePendingScreen = 0;
static u32 g_screenLoadResourcePendingParam = 0;
static u32 g_currentScreenThis = 0;
u32 g_currentScreenModuleBase = 0;
static u32 g_currentScreenDataPackage = 0;
static u32 g_dlSpBf = 0;
static u32 g_poolModuleR9s[16];
static u32 g_poolModuleR9Count = 0;
typedef struct
{
    u16 appId;
    u16 reserved;
    u32 buffer;
    u32 context;
    u32 spBf;
} vm_dl_loaded_app;
static vm_dl_loaded_app g_vmDlLoadedApps[16];
static u32 g_vmDlLoadedCount = 0;
static u16 g_vmDlCurrAppId = 0;
static u16 g_vmDlPreAppId = 0;
static u8 g_vmDlCurrType = 0;
static u32 g_screenRootExitArmed = 0;
static u32 g_screenRootExitPending = 0;
static u32 g_screenRootExitPendingRoot = 0;
static u32 g_screenRootExitPendingRemoved = 0;
static u32 g_screenRootExitPendingTick = 0;
static volatile u32 g_hostQuitRequested = 0;
static volatile u32 g_hostQuitCleanupStarted = 0;
static volatile u32 g_vmThreadFinished = 0;
static u32 g_appMainEntry = 0;
static u32 g_appExitEntry = 0;
static u8 g_wpayMockFlowActive = 0;
static u32 g_lastSceLoadCtx = 0;
static u32 g_lastSceLoadNamePtr = 0;
static char g_lastSceLoadName[96] = "-";

#define VM_SCREEN_EXIT_DESTROY 0
#define VM_SCREEN_EXIT_PAUSE 1
#define VM_SCREEN_EXIT_SKIP 2
#define VM_SCREEN_ROOT_EXIT_GRACE_TICKS 15

static bool g_vm_net_mock_pending_scene_save_valid = false;
static char g_vm_net_mock_pending_scene_save_scene[64];
static char g_vm_net_mock_pending_scene_save_reason[64];
static u16 g_vm_net_mock_pending_scene_save_x = 0;
static u16 g_vm_net_mock_pending_scene_save_y = 0;

static void vm_net_mock_save_player_pos_state(const char *scene, u16 x, u16 y, const char *reason);

u32 lastSprintfPtr = 0;
static u8 *g_cbeFileBuffer = NULL;
static u32 g_cbeFileSize = 0;

#define VM_SCHED_MAX_NET_TASKS 8
#define VM_SCHED_MAX_TIMERS 20
#define VM_SCHED_TIMER_BASE_ID 100
#define VM_SCHED_FRAME_MS 100

typedef struct
{
    u8 hasSceneTarget;
    u8 sceneSubtype;
    u8 sceneCompleteAfterCallback;
    u8 updateComplete;
    u16 sceneX;
    u16 sceneY;
    char scene[64];
    char updateName[64];
} vm_net_remote_observation;

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
    vm_net_remote_observation remoteObservation;
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
static int g_netTaskDispatchDepth = 0;
static int g_netTaskDispatchSlot = -1;
static vm_timer_task g_timerTasks[VM_SCHED_MAX_TIMERS];
static u32 g_schedulerStartTicks = 0;
static u32 g_nextNetConnectId = 1;
static u8 g_netMockResponse[131072];
static u32 g_netMockResponseLen = 0;
static u32 g_netMockResponseOffset = 0;
static u32 g_netMockResponseVmPtr = 0;
static bool g_netMockSplitProbe = false;
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
static u32 g_netUpLinkData = 0;
static u32 g_netDownLinkData = 0;
static u32 g_netCurrentObject = 0;
static u32 g_netDebugReadWindow = 0;
static const u32 VM_GAME_NET_BUSINESS_CALLBACK = 0x01012e4d;
static u8 g_lastStartupScreenState = 0xff;
static void vm_autotest_note(const char *fmt, ...);
static u32 g_lastStartupUpdateObj = 0xffffffff;
static u8 g_lastStartupProgress = 0xff;
static u8 g_lastStartupUpdateState = 0xff;
static u32 g_currentFontType = 0;
static u8 g_netLastHandledValid = 0;
static u32 g_netLastHandledResponseLen = 0;
static char g_netLastHandledSource[64];
static char g_netLastHandledSummary[512];
#ifdef CBE_SERVER_ONLY
static u8 g_mockServiceOnly = 1;
#else
static u8 g_mockServiceOnly = 0;
#endif
static u8 g_mockServiceWarnedUnavailable = 0;
static char g_mockServiceHost[64] = "127.0.0.1";
// static char g_mockServiceHost[64] = "23.141.172.143";
#ifdef CBE_SERVER_ONLY
static char g_mockServiceBindHost[64] = "0.0.0.0";
static char g_mockAdminBindHost[64] = "0.0.0.0";
#else
static char g_mockServiceBindHost[64] = "127.0.0.1";
static char g_mockAdminBindHost[64] = "127.0.0.1";
#endif
static u32 g_mockServiceClientId = 0;
static u16 g_mockServicePort = 19090;
static u16 g_mockAdminPort = 19091;
static u32 g_battleSubtype8InfoDstWatchBase = 0;
static u32 g_battleSubtype8InfoDstWatchLen = 0;
static u32 g_battleSubtype8InfoDstWatchTick = 0;
static u32 g_battleSubtype8InfoDstWriteLimitCount = 0;
static u32 g_mockBattleOperateSessionSerial = 0;
static u32 g_mockBattleOperateTurnCounter = 0;
u8 g_mockBattleOperateSessionArmed = 0;
static u8 g_mockBattleOperateSessionFinished = 0;
static u8 g_mockBattlePendingEnemyTurn = 0;
static u8 g_mockBattleAwaitingSettlement = 0;
static u8 g_mockBattleSceneMonsterStartActive = 0;
static u32 g_mockBattleRoleHpCurrent = 0;
static u32 g_mockBattleRoleHpMax = 0;
static u32 g_mockBattleRoleMpCurrent = 0;
static u32 g_mockBattleRoleMpMax = 0;
static u8 g_mockBattleEnemyCountCurrent = 1;
static u32 g_mockBattleEnemyHpSlots[3] = {0, 0, 0};
static u32 g_mockBattleEnemyHpMaxSlots[3] = {0, 0, 0};
static u32 g_mockBattleEnemyHpCurrent = 0;
static u32 g_mockBattleEnemyHpMax = 0;

static uc_err add_manager_code_hooks(uc_engine *uc);
static bool vm_host_file_exists(const char *path);
static bool vm_net_mock_current_screen_is_battle(void);
static void vm_autotest_note_role_attr_page_pc(u32 pc);
static void vm_autotest_note_attr_value_write(const char *source, u32 dst, u32 len);
static bool vm_net_mock_append_battle_status7_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_battle_terminal_subtype8_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_battle_terminal_case4_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_battle_terminal_case9_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_battle_terminal_case11_object(u8 *out, u32 outCap, u32 *pos);
static u32 vm_net_mock_build_battle_auto12_cancel_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap);
static u32 vm_net_mock_min_u32(u32 a, u32 b);
static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
static uc_err scheduler_dispatch_net_tasks(void);
static bool vm_net_mock_should_rearm_send_ready(void);
static u32 vm_net_mock_apply_remote_observation(
    const vm_net_remote_observation *observation);
static void vm_net_mock_finish_remote_observation(u32 sceneTargetSerial);
static uc_err scheduler_flush_post_vm_business_send_ready(const char *reason);
static u32 vm_dl_current_sp_bf(void);
static void vm_dl_note_sp_bf(u32 moduleR9, const char *reason);

static bool vm_address_in_range(u32 address, u32 begin, u32 size)
{
    return address >= begin && address < begin + size;
}

static bool vm_is_manager_func_stub_address(u32 address)
{
    if (vm_address_in_range(address, VM_FIXED_BASE_GAMEOLD_REGION_FUNC_ADDRESS,
                            VM_FIXED_BASE_GAMEOLD_REGION_FUNC_COUNT * 4))
        return true;
    if (vm_address_in_range(address, VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_ADDRESS,
                            VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_COUNT * 4))
        return true;
    if (vm_address_in_range(address, VM_FIXED_BASE_MANAGER_INIT_ADDRESS,
                            VM_FIXED_BASE_MANAGER_INIT_COUNT * 4))
        return true;
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

static void vm_try_write_zero(u32 ptr, u32 len)
{
    u32 off = 0;
    u8 zero[64] = {0};

    if (ptr == 0 || len == 0)
        return;
    while (off < len)
    {
        u32 chunk = SDL_min(len - off, (u32)sizeof(zero));
        if (uc_mem_write(MTK, ptr + off, zero, chunk) != UC_ERR_OK)
            break;
        off += chunk;
    }
}

static int vm_lcd_coord_from_reg(u32 value)
{
    return (int)(int16_t)(value & 0xffff);
}

static void vm_lcd_normalize_signed_rect(int *x, int *y, int *w, int *h)
{
    if (*w < 0)
    {
        *x += *w;
        *w = -*w;
    }
    if (*h < 0)
    {
        *y += *h;
        *h = -*h;
    }
}

static bool vm_lcd_clip_rect(int *x, int *y, int *w, int *h, int maxW, int maxH)
{
    vm_lcd_normalize_signed_rect(x, y, w, h);

    if (*x < 0)
    {
        *w += *x;
        *x = 0;
    }
    if (*y < 0)
    {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > maxW)
        *w = maxW - *x;
    if (*y + *h > maxH)
        *h = maxH - *y;

    return *w > 0 && *h > 0;
}

static bool vm_lcd_looks_like_fillrect_compat(u32 r0, u32 r1, u32 r2, u32 r3)
{
    int x = vm_lcd_coord_from_reg(r0);
    int y = vm_lcd_coord_from_reg(r1);
    int w = vm_lcd_coord_from_reg(r2);
    int h = vm_lcd_coord_from_reg(r3);

    return r0 <= 0xffffu &&
           x > -LCD_WIDTH &&
           x < LCD_WIDTH &&
           y > -LCD_HEIGHT &&
           y < LCD_HEIGHT &&
           w > -LCD_WIDTH &&
           w <= LCD_WIDTH &&
           h > -LCD_HEIGHT &&
           h <= LCD_HEIGHT;
}



static u32 vm_cd_rect_point(u32 left, u32 top, u32 right, u32 bottom, u32 x, u32 y)
{
    int px = (int)(int16_t)(x & 0xffff);
    int py = (int)(int16_t)(y & 0xffff);
    int l = (int)(int16_t)(left & 0xffff);
    int t = (int)(int16_t)(top & 0xffff);
    int r = (int)(int16_t)(right & 0xffff);
    int b = (int)(int16_t)(bottom & 0xffff);

    return (r >= px && l <= px && b >= py && t <= py) ? 1u : 0u;
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

static int vm_lcd_image_pitch_bytes(int width)
{
    return (((4 - width) & 3) + width) * PIXEL_PER_BYTE;
}

static bool vm_lcd_read_image_info(u32 imageInfo, u32 *pixels, int *width, int *height)
{
    u8 header[8];
    if (imageInfo == 0)
        return false;
    if (uc_mem_read(MTK, imageInfo, header, sizeof(header)) != UC_ERR_OK)
        return false;

    *pixels = vm_get_var(imageInfo);
    *width = (int)vm_get_var_short(imageInfo + 4);
    *height = (int)vm_get_var_short(imageInfo + 6);
    return *pixels != 0 && *width > 0 && *height > 0;
}

static void vm_lcd_call_draw_image_clip_ex(u32 imageInfo, int srcX, int srcY, int w, int h, int dstX, int dstY, bool alpha)
{
    u32 savedSp = 0;
    u32 tempSp = 0;
    u32 r0 = VM_screenImageStruct_ADDRESS;
    u32 r1 = imageInfo;
    u32 r2 = (u32)srcX;
    u32 r3 = (u32)srcY;

    if (w <= 0 || h <= 0)
        return;

    uc_reg_read(MTK, UC_ARM_REG_SP, &savedSp);
    tempSp = savedSp - 16;
    vm_set_var(tempSp, (u32)w);
    vm_set_var(tempSp + 4, (u32)h);
    vm_set_var(tempSp + 8, (u32)dstX);
    vm_set_var(tempSp + 12, (u32)dstY);

    uc_reg_write(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_write(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_write(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_write(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_write(MTK, UC_ARM_REG_SP, &tempSp);
    if (alpha)
        vm_vMDrawImageClipAndAlphaEx();
    else
        vM_DrawImageWithClipEx();
    uc_reg_write(MTK, UC_ARM_REG_SP, &savedSp);
}

static int vm_lcd_draw_image_to_screen(u32 imageInfo, int dstX, int dstY)
{
    u32 srcPixels = 0;
    int srcW = 0;
    int srcH = 0;
    int srcX = 0;
    int srcY = 0;
    int w;
    int h;
    u8 rowBuf[LCD_WIDTH * PIXEL_PER_BYTE];

    if (!vm_lcd_read_image_info(imageInfo, &srcPixels, &srcW, &srcH))
        return 0;
    if (dstX >= LCD_WIDTH || dstY >= LCD_HEIGHT)
        return 0;

    w = srcW;
    h = srcH;
    if (dstX < 0)
    {
        srcX = -dstX;
        w += dstX;
        dstX = 0;
    }
    if (dstY < 0)
    {
        srcY = -dstY;
        h += dstY;
        dstY = 0;
    }
    if (srcX >= srcW || srcY >= srcH || w <= 0 || h <= 0)
        return 1;
    if (srcX + w > srcW)
        w = srcW - srcX;
    if (srcY + h > srcH)
        h = srcH - srcY;
    if (dstX + w > LCD_WIDTH)
        w = LCD_WIDTH - dstX;
    if (dstY + h > LCD_HEIGHT)
        h = LCD_HEIGHT - dstY;
    if (w <= 0 || h <= 0)
        return 1;

    int srcPitch = vm_lcd_image_pitch_bytes(srcW);
    int dstPitch = LCD_PITCH * PIXEL_PER_BYTE;
    int copyBytes = w * PIXEL_PER_BYTE;
    if (copyBytes > (int)sizeof(rowBuf))
        return 0;

    for (int row = 0; row < h; ++row)
    {
        u32 srcOff = (u32)((srcY + row) * srcPitch + srcX * PIXEL_PER_BYTE);
        u32 dstOff = (u32)((dstY + row) * dstPitch + dstX * PIXEL_PER_BYTE);
        if (uc_mem_read(MTK, srcPixels + srcOff, rowBuf, copyBytes) != UC_ERR_OK)
            return 0;
        uc_mem_write(MTK, VM_screenImage_ADDRESS + dstOff, rowBuf, copyBytes);
        memcpy(Lcd_Cache_Buffer + dstOff, rowBuf, copyBytes);
    }
    return 1;
}

static int vm_lcd_draw_image_with_alpha_to_screen(u32 imageInfo, int dstX, int dstY)
{
    u32 srcPixels = 0;
    int srcW = 0;
    int srcH = 0;

    if (!vm_lcd_read_image_info(imageInfo, &srcPixels, &srcW, &srcH))
        return 0;
    (void)srcPixels;
    if (dstX >= LCD_WIDTH || dstY >= LCD_HEIGHT)
        return 0;
    vm_lcd_call_draw_image_clip_ex(imageInfo, 0, 0, srcW, srcH, dstX, dstY, true);
    return 1;
}

static u32 vm_lcd_pack_coord(int x, int y)
{
    return ((u32)(u16)y << 16) | (u16)x;
}

static int vm_lcd_draw_image_with_clip_packed(u32 imageInfo, u32 srcPacked, u32 dstStartPacked, u32 dstEndPacked, bool alpha)
{
    int srcX = vm_lcd_coord_from_reg(srcPacked);
    int srcY = vm_lcd_coord_from_reg(srcPacked >> 16);
    int dstX = vm_lcd_coord_from_reg(dstStartPacked);
    int dstY = vm_lcd_coord_from_reg(dstStartPacked >> 16);
    int endX = vm_lcd_coord_from_reg(dstEndPacked);
    int endY = vm_lcd_coord_from_reg(dstEndPacked >> 16);

    if (imageInfo == 0 ||
        endX <= dstX ||
        endY <= dstY ||
        dstX < 0 ||
        dstY < 0 ||
        dstX >= LCD_WIDTH - 1 ||
        dstY >= LCD_HEIGHT - 1)
    {
        return 0;
    }
    if (endX > LCD_WIDTH - 1)
        endX = LCD_WIDTH - 1;
    if (endY > LCD_HEIGHT - 1)
        endY = LCD_HEIGHT - 1;

    vm_lcd_call_draw_image_clip_ex(imageInfo, srcX, srcY, endX - dstX + 1, endY - dstY + 1, dstX, dstY, alpha);
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
            g_lastStartupProgress = progress;
            g_lastStartupUpdateState = updateState;
        }
    }
    if (state == 10 && updateObj == 0)
    {
    }
}


static uc_err vm_emu_start(u32 begin, u32 until);
static bool vm_is_pool_entry(u32 entry);
static void vm_restore_r9_for_entry(u32 entry);
static void vm_dl_note_sp_bf(u32 moduleR9, const char *reason);
static uc_err vm_run_host_quit_cleanup(u32 exitAddr, u32 thumbExitAddr);
static void vm_request_host_quit(const char *reason);
static void scheduler_prepare_screen_call(u32 screenThisPtr);
static void vm_close_open_files_for_restart(void);
static void vm_pool_module_remember_r9(u32 moduleR9);

static bool vm_is_writable_vm_range(u32 addr, u32 len)
{
    if (addr == 0 || len == 0 || addr + len < addr)
        return false;
    if (addr >= Program_Data_Address && addr + len <= Program_Data_Address + g_cbeInfo.headerInt4)
        return true;
    if (addr >= VM_Memory_Pool_ADDRESS && addr + len <= VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
        return true;
    if (addr >= STACK_ADDRESS && addr + len <= STACK_ADDRESS + 0x100000u)
        return true;
    return false;
}

static void vm_trace_screen_data_package_change(const char *phase, u32 screen, u32 oldDataPackage, u32 newDataPackage, u32 globalDataPackage)
{
    static u32 s_logCount = 0;
    if (oldDataPackage == newDataPackage || s_logCount >= 64)
        return;
    ++s_logCount;
    printf("[info][screen] dp_change phase=%s screen=%08x old=%08x new=%08x global=%08x caller=%08x current=%08x this=%08x depth=%u\n",
           phase ? phase : "-", screen, oldDataPackage, newDataPackage, globalDataPackage,
           lastAddress, vmAddedScreen, g_currentScreenThis, g_screenStackCount);
    vm_autotest_note("screen_dp_change phase=%s screen=%08x old=%08x new=%08x global=%08x caller=%08x current=%08x this=%08x depth=%u\n",
                     phase ? phase : "-", screen, oldDataPackage, newDataPackage, globalDataPackage,
                     lastAddress, vmAddedScreen, g_currentScreenThis, g_screenStackCount);
}

static int vm_screen_stack_find(u32 screen)
{
    for (u32 i = 0; i < g_screenStackCount; ++i)
    {
        if (g_screenStack[i] == screen)
            return (int)i;
    }
    return -1;
}

static int vm_screen_stack_find_related(u32 screen)
{
    int exact = vm_screen_stack_find(screen);
    if (exact >= 0)
        return exact;

    for (u32 i = 0; i < g_screenStackCount; ++i)
    {
        u32 table = g_screenStack[i];
        if (screen != 0 && screen + 0x18 == table)
            return (int)i;
        if (screen >= table && screen < table + 0x200)
            return (int)i;
        if (screen + 0x80 >= table && screen < table)
            return (int)i;
    }
    return -1;
}

static u32 vm_screen_stack_lookup_module_base(u32 screen)
{
    int existing = vm_screen_stack_find_related(screen);
    if (existing < 0)
        return 0;
    return g_screenStackModuleBase[(u32)existing];
}

static u32 vm_current_data_package(void)
{
    return vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
}

static void vm_restore_data_package(u32 dataPackage)
{
    if (dataPackage)
        vm_set_var(VM_DreamFactory_DataPackage_ADDRESS, dataPackage);
}

static u32 vm_screen_stack_lookup_data_package(u32 screen)
{
    int existing = vm_screen_stack_find_related(screen);
    if (existing < 0)
        return 0;
    return g_screenStackDataPackage[(u32)existing];
}

static void vm_screen_stack_update_data_package(u32 screen, u32 dataPackage)
{
    int existing = vm_screen_stack_find_related(screen);
    if (existing >= 0)
    {
        u32 oldDataPackage = g_screenStackDataPackage[(u32)existing];
        g_screenStackDataPackage[(u32)existing] = dataPackage;
        vm_trace_screen_data_package_change("stack-update", g_screenStack[(u32)existing],
                                            oldDataPackage, dataPackage, vm_current_data_package());
    }
}

static u32 vm_screen_default_call_param(u32 screen)
{
    if (screen == 0)
        return 0;
    if (screen >= Global_R9 && screen < Program_ROM_Address + Program_ROM_Mapped_Size)
        return screen - 0x18;
    if (vm_screen_stack_lookup_module_base(screen))
        return screen - 0x18;
    return 0;
}

static bool vm_screen_param_is_live(u32 param)
{
    u32 probe[3] = {0};
    if (param == 0)
        return false;
    if (uc_mem_read(MTK, param, probe, sizeof(probe)) != UC_ERR_OK)
        return false;
    return probe[0] != 0 || probe[1] != 0 || probe[2] != 0;
}

static u32 vm_screen_stack_lookup_param(u32 screen)
{
    u32 fallback = vm_screen_default_call_param(screen);
    int existing = vm_screen_stack_find_related(screen);
    if (existing >= 0 && g_screenStackParam[(u32)existing])
    {
        u32 param = g_screenStackParam[(u32)existing];
        if (fallback == 0 || vm_screen_param_is_live(param))
            return param;
    }
    return fallback;
}

static u32 vm_screen_stack_lookup_flags(u32 screen)
{
    int existing = vm_screen_stack_find_related(screen);
    if (existing < 0)
        return 1;
    return g_screenStackFlags[(u32)existing];
}

static void vm_screen_stack_push_with_data_package(u32 screen, u32 param, u32 flags, u32 moduleBase, u32 dataPackage);
static void vm_screen_stack_push(u32 screen, u32 param, u32 flags, u32 moduleBase);

static void vm_screen_stack_preserve_active_if_needed(void)
{
    u32 activeScreen = vmAddedScreen;
    u32 activeParam = g_currentScreenThis;
    if (activeScreen == 0 && activeParam)
        activeScreen = activeParam + 0x18;
    if (activeParam == 0)
        activeParam = vm_screen_stack_lookup_param(activeScreen);

    if (activeScreen == 0 || vm_screen_stack_find_related(activeScreen) >= 0)
        return;

    u32 moduleBase = g_currentScreenModuleBase ? g_currentScreenModuleBase : vm_screen_stack_lookup_module_base(activeScreen);
    u32 dataPackage = vm_current_data_package();
    if (dataPackage == 0)
        dataPackage = g_currentScreenDataPackage;
    vm_screen_stack_push_with_data_package(activeScreen, activeParam, 1, moduleBase, dataPackage);
    vm_autotest_note("screen_mgr preserve_active screen=%08x param=%08x module=%08x dp=%08x depth=%u\n",
                     activeScreen, activeParam, moduleBase, dataPackage, g_screenStackCount);
}

static bool vm_screen_is_entry_root(u32 screen)
{
    if (screen == 0)
        return false;
    return vm_get_var(screen) == 0 && vm_get_var(screen + 4) == 0;
}

static void vm_screen_root_exit_cancel(const char *reason)
{
    if (!g_screenRootExitPending)
        return;

    vm_autotest_note("screen_mgr root_exit_cancel reason=%s root=%08x removed=%08x tick=%u\n",
                     reason ? reason : "unknown",
                     g_screenRootExitPendingRoot,
                     g_screenRootExitPendingRemoved,
                     g_schedulerTick);
    g_screenRootExitPending = 0;
    g_screenRootExitPendingRoot = 0;
    g_screenRootExitPendingRemoved = 0;
    g_screenRootExitPendingTick = 0;
}

static void vm_screen_root_exit_arm_pending(u32 removedScreen, u32 rootScreen)
{
    g_screenRootExitPending = 1;
    g_screenRootExitPendingRoot = rootScreen;
    g_screenRootExitPendingRemoved = removedScreen;
    g_screenRootExitPendingTick = g_schedulerTick;
    vm_autotest_note("screen_mgr root_exit_pending caller=%08x removed=%08x root=%08x tick=%u\n",
                     lastAddress, removedScreen, rootScreen, g_schedulerTick);
}

static void vm_screen_root_exit_maybe_request(void)
{
    if (!g_screenRootExitPending || g_hostQuitRequested || g_hostQuitCleanupStarted)
        return;

    if (g_screenStackCount != 1 ||
        vmAddedScreen != g_screenRootExitPendingRoot ||
        !vm_screen_is_entry_root(g_screenRootExitPendingRoot))
    {
        vm_screen_root_exit_cancel("screen_changed");
        return;
    }

    if (g_schedulerTick - g_screenRootExitPendingTick < VM_SCREEN_ROOT_EXIT_GRACE_TICKS)
        return;

    vm_autotest_note("screen_mgr root_exit_confirm root=%08x removed=%08x depth=%u waited=%u\n",
                     g_screenRootExitPendingRoot,
                     g_screenRootExitPendingRemoved,
                     g_screenStackCount,
                     g_schedulerTick - g_screenRootExitPendingTick);
    g_screenRootExitPending = 0;
    vm_request_host_quit("screen_root_exit");
}

static bool vm_infer_battle_module_from_screen(u32 screen, u32 *codeBase, u32 *moduleR9)
{
    u32 init = 0;
    u32 destroy = 0;
    u32 logic = 0;
    u32 render = 0;
    u32 pause = 0;
    u32 resume = 0;
    u32 base = 0;

    if (screen == 0)
        return false;
    u8 probe = 0;
    if (uc_mem_read(MTK, screen, &probe, 1) != UC_ERR_OK ||
        uc_mem_read(MTK, screen + 20, &probe, 1) != UC_ERR_OK)
    {
        return false;
    }
    init = vm_get_var(screen);
    destroy = vm_get_var(screen + 4);
    logic = vm_get_var(screen + 8);
    render = vm_get_var(screen + 12);
    pause = vm_get_var(screen + 16);
    resume = vm_get_var(screen + 20);

    init &= ~1u;
    destroy &= ~1u;
    logic &= ~1u;
    render &= ~1u;
    pause &= ~1u;
    resume &= ~1u;
    if (init < 0xFE44u)
        return false;
    base = init - 0xFE44u;
    if (base < VM_Memory_Pool_ADDRESS || base >= VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
        return false;
    if (destroy != base + 0xEE0Eu ||
        logic != base + 0xED0Au ||
        render != base + 0xE51Au ||
        pause != base + 0xE3E0u ||
        resume != base + 0xE374u)
    {
        return false;
    }

    if (codeBase)
        *codeBase = base;
    if (moduleR9)
        *moduleR9 = base + 0x14000u;
    return true;
}

static void vm_screen_stack_push_with_data_package(u32 screen, u32 param, u32 flags, u32 moduleBase, u32 dataPackage)
{
    if (screen == 0)
        return;

    vm_screen_root_exit_cancel("stack_push");

    int existing = vm_screen_stack_find(screen);
    if (existing >= 0)
    {
        for (u32 i = (u32)existing; i + 1 < g_screenStackCount; ++i)
        {
            g_screenStack[i] = g_screenStack[i + 1];
            g_screenStackParam[i] = g_screenStackParam[i + 1];
            g_screenStackModuleBase[i] = g_screenStackModuleBase[i + 1];
            g_screenStackDataPackage[i] = g_screenStackDataPackage[i + 1];
            g_screenStackFlags[i] = g_screenStackFlags[i + 1];
            g_screenStackInited[i] = g_screenStackInited[i + 1];
        }
        g_screenStackCount--;
    }

    if (g_screenStackCount >= sizeof(g_screenStack) / sizeof(g_screenStack[0]))
    {
        memmove(g_screenStack, g_screenStack + 1, (sizeof(g_screenStack) / sizeof(g_screenStack[0]) - 1) * sizeof(g_screenStack[0]));
        memmove(g_screenStackParam, g_screenStackParam + 1, (sizeof(g_screenStackParam) / sizeof(g_screenStackParam[0]) - 1) * sizeof(g_screenStackParam[0]));
        memmove(g_screenStackModuleBase, g_screenStackModuleBase + 1, (sizeof(g_screenStackModuleBase) / sizeof(g_screenStackModuleBase[0]) - 1) * sizeof(g_screenStackModuleBase[0]));
        memmove(g_screenStackDataPackage, g_screenStackDataPackage + 1, (sizeof(g_screenStackDataPackage) / sizeof(g_screenStackDataPackage[0]) - 1) * sizeof(g_screenStackDataPackage[0]));
        memmove(g_screenStackFlags, g_screenStackFlags + 1, (sizeof(g_screenStackFlags) / sizeof(g_screenStackFlags[0]) - 1) * sizeof(g_screenStackFlags[0]));
        memmove(g_screenStackInited, g_screenStackInited + 1, (sizeof(g_screenStackInited) / sizeof(g_screenStackInited[0]) - 1) * sizeof(g_screenStackInited[0]));
        g_screenStackCount--;
    }

    g_screenStack[g_screenStackCount] = screen;
    g_screenStackParam[g_screenStackCount] = param;
    g_screenStackModuleBase[g_screenStackCount] = moduleBase;
    g_screenStackDataPackage[g_screenStackCount] = dataPackage;
    vm_trace_screen_data_package_change("stack-push", screen, 0, dataPackage, vm_current_data_package());
    g_screenStackFlags[g_screenStackCount] = (u8)flags;
    g_screenStackInited[g_screenStackCount] = 0;
    g_screenStackCount++;
    if (moduleBase != 0)
        vm_dl_note_sp_bf(moduleBase, "screen-push");
    if (moduleBase != 0 || g_screenStackCount >= 3)
        g_screenRootExitArmed = 1;
}

static void vm_screen_stack_push(u32 screen, u32 param, u32 flags, u32 moduleBase)
{
    vm_screen_stack_push_with_data_package(screen, param, flags, moduleBase, vm_current_data_package());
}

static void vm_screen_stack_replace_top(u32 screen, u32 param, u32 flags, u32 moduleBase)
{
    if (g_screenStackCount > 0)
        g_screenStackCount--;
    vm_screen_stack_push(screen, param, flags, moduleBase);
}

static bool vm_screen_stack_remove(u32 screen, u32 *newTop, u32 *newTopParam, u32 *newTopModuleBase, u32 *newTopDataPackage)
{
    int existing = vm_screen_stack_find_related(screen);
    if (existing < 0)
    {
        if (newTop)
            *newTop = g_screenStackCount ? g_screenStack[g_screenStackCount - 1] : 0;
        if (newTopParam)
            *newTopParam = g_screenStackCount ? g_screenStackParam[g_screenStackCount - 1] : 0;
        if (newTopModuleBase)
            *newTopModuleBase = g_screenStackCount ? g_screenStackModuleBase[g_screenStackCount - 1] : 0;
        if (newTopDataPackage)
            *newTopDataPackage = g_screenStackCount ? g_screenStackDataPackage[g_screenStackCount - 1] : 0;
        return false;
    }

    for (u32 i = (u32)existing; i + 1 < g_screenStackCount; ++i)
    {
        g_screenStack[i] = g_screenStack[i + 1];
        g_screenStackParam[i] = g_screenStackParam[i + 1];
        g_screenStackModuleBase[i] = g_screenStackModuleBase[i + 1];
        g_screenStackDataPackage[i] = g_screenStackDataPackage[i + 1];
        g_screenStackFlags[i] = g_screenStackFlags[i + 1];
        g_screenStackInited[i] = g_screenStackInited[i + 1];
    }
    g_screenStackCount--;

    if (newTop)
        *newTop = g_screenStackCount ? g_screenStack[g_screenStackCount - 1] : 0;
    if (newTopParam)
        *newTopParam = g_screenStackCount ? g_screenStackParam[g_screenStackCount - 1] : 0;
    if (newTopModuleBase)
        *newTopModuleBase = g_screenStackCount ? g_screenStackModuleBase[g_screenStackCount - 1] : 0;
    if (newTopDataPackage)
        *newTopDataPackage = g_screenStackCount ? g_screenStackDataPackage[g_screenStackCount - 1] : 0;
    return true;
}

u32 size_128mb = 1024 * 1024 * 128;
u32 size_32mb = 1024 * 1024 * 32;
u32 size_16mb = 1024 * 1024 * 16;
u32 size_8mb = 1024 * 1024 * 8;
u32 size_4mb = 1024 * 1024 * 4;
u32 size_1mb = 1024 * 1024;
u32 size_2kb = 1024 * 2;

static u32 vm_round_up_page(u32 value)
{
    return (value + 0xfff) & ~0xfffu;
}

static void vm_config_program_mapping(void)
{
    Program_ROM_Address = g_cbeInfo.headerInt1 ? g_cbeInfo.headerInt1 : ROM_ADDRESS;
    Program_Data_Address = g_cbeInfo.headerInt1 ? g_cbeInfo.headerInt3 : Program_ROM_Address + g_cbeInfo.headerInt2;

    if (g_cbeInfo.headerInt1 == 0)
    {
        Program_ROM_Mapped_Size = size_16mb;
        return;
    }

    u32 imageEnd = Program_Data_Address + g_cbeInfo.headerInt4;
    u32 codeEnd = Program_ROM_Address + g_cbeInfo.headerInt2;
    if (imageEnd < codeEnd)
        imageEnd = codeEnd;
    if (imageEnd <= Program_ROM_Address)
        Program_ROM_Mapped_Size = size_16mb;
    else
        Program_ROM_Mapped_Size = vm_round_up_page(imageEnd - Program_ROM_Address);
}

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
static u32 g_nativeAppInitEntry = 0;
static u32 g_nativeAppParserEntry = 0;
static u32 g_nativeSystemInfoPtr = 0;
static u32 g_nativePropertyInfoPtr = 0;
static u32 g_nativeDispatchTraceCount = 0;

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
    u32 cpsr = 0;
    g_currentEmuEntry = begin;
    if (Global_R9)
        vm_restore_r9_for_entry(begin);
    uc_reg_read(MTK, UC_ARM_REG_CPSR, &cpsr);
    if (begin & 1)
        cpsr |= 0x20;
    else
        cpsr &= ~0x20u;
    uc_reg_write(MTK, UC_ARM_REG_CPSR, &cpsr);
    uc_err err = uc_emu_start(MTK, begin, until, 0, 0);
    normalize_program_exit_pc(begin);
    return err;
}

static uc_err vm_emu_start_count(u32 begin, u32 until, uint64_t count)
{
    u32 cpsr = 0;
    g_currentEmuEntry = begin;
    if (Global_R9)
        vm_restore_r9_for_entry(begin);
    uc_reg_read(MTK, UC_ARM_REG_CPSR, &cpsr);
    if (begin & 1)
        cpsr |= 0x20;
    else
        cpsr &= ~0x20u;
    uc_reg_write(MTK, UC_ARM_REG_CPSR, &cpsr);
    uc_err err = uc_emu_start(MTK, begin, until, 0, count);
    normalize_program_exit_pc(begin);
    return err;
}

static bool vm_is_pool_entry(u32 entry)
{
    u32 pc = entry & ~1u;
    return pc >= VM_Memory_Pool_ADDRESS && pc < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE;
}

static void vm_dl_reset_state(void)
{
    memset(g_vmDlLoadedApps, 0, sizeof(g_vmDlLoadedApps));
    g_vmDlLoadedCount = 0;
    g_vmDlCurrAppId = 0;
    g_vmDlPreAppId = 0;
    g_vmDlCurrType = 0;
    g_dlSpBf = 0;
}

static int vm_dl_find_loaded_index_by_app_id(u16 appId)
{
    for (u32 i = 0; i < g_vmDlLoadedCount; ++i)
    {
        if (g_vmDlLoadedApps[i].appId == appId)
            return (int)i;
    }
    return -1;
}

static int vm_dl_find_loaded_index_by_sp_bf(u32 spBf)
{
    if (spBf == 0)
        return -1;
    for (u32 i = 0; i < g_vmDlLoadedCount; ++i)
    {
        if (g_vmDlLoadedApps[i].spBf == spBf)
            return (int)i;
    }
    return -1;
}

static int vm_dl_find_loaded_index_by_pc(u32 pc)
{
    pc &= ~1u;
    if (!vm_is_pool_entry(pc))
        return -1;
    for (u32 i = 0; i < g_vmDlLoadedCount; ++i)
    {
        u32 buffer = g_vmDlLoadedApps[i].buffer;
        u32 spBf = g_vmDlLoadedApps[i].spBf;
        if (buffer != 0 && spBf > buffer && pc >= buffer && pc < spBf)
            return (int)i;
    }
    return -1;
}

static int vm_dl_ensure_loaded_app(u16 appId)
{
    int idx = vm_dl_find_loaded_index_by_app_id(appId);
    if (idx >= 0)
        return idx;
    if (g_vmDlLoadedCount >= sizeof(g_vmDlLoadedApps) / sizeof(g_vmDlLoadedApps[0]))
        return -1;
    idx = (int)g_vmDlLoadedCount++;
    memset(&g_vmDlLoadedApps[idx], 0, sizeof(g_vmDlLoadedApps[idx]));
    g_vmDlLoadedApps[idx].appId = appId;
    return idx;
}

static u32 vm_dl_current_sp_bf(void)
{
    int idx;
    if (g_vmDlCurrAppId == 0)
        return 0;
    idx = vm_dl_find_loaded_index_by_app_id(g_vmDlCurrAppId);
    if (idx < 0)
        return 0;
    return g_vmDlLoadedApps[idx].spBf;
}

static void vm_dl_note_sp_bf(u32 moduleR9, const char *reason)
{
    static u32 s_traceCount = 0;
    if (moduleR9 < VM_Memory_Pool_ADDRESS ||
        moduleR9 >= VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        return;
    }
    if (g_dlSpBf == moduleR9)
        return;
    g_dlSpBf = moduleR9;
    if (g_autotestEnabled && s_traceCount < 32)
    {
        ++s_traceCount;
        vm_autotest_note("dl_sp_bf reason=%s r9=%08x count=%u\n",
                         reason ? reason : "-", moduleR9, s_traceCount);
    }
}

static void vm_dl_set_current_app(u16 appId, u8 dlType, const char *reason)
{
    if (g_vmDlCurrAppId != appId)
        g_vmDlPreAppId = g_vmDlCurrAppId;
    g_vmDlCurrAppId = appId;
    g_vmDlCurrType = dlType;
    if (g_vmDlCurrAppId != 0)
    {
        u32 spBf = vm_dl_current_sp_bf();
        if (spBf)
            vm_dl_note_sp_bf(spBf, reason);
    }
}

static void vm_dl_set_loaded_buffer(u16 appId, u32 buffer)
{
    int idx = vm_dl_ensure_loaded_app(appId);
    if (idx < 0)
        return;
    g_vmDlLoadedApps[idx].buffer = buffer;
}

static void vm_dl_set_loaded_sp_bf(u16 appId, u32 spBf)
{
    int idx = vm_dl_ensure_loaded_app(appId);
    if (idx < 0)
        return;
    g_vmDlLoadedApps[idx].spBf = spBf;
    if (g_vmDlCurrAppId == appId && spBf != 0)
        vm_dl_note_sp_bf(spBf, "dl-set-spbf");
}

static void vm_dl_set_loaded_context(u16 appId, u32 context)
{
    int idx = vm_dl_ensure_loaded_app(appId);
    if (idx < 0)
        return;
    g_vmDlLoadedApps[idx].context = context;
}

static u32 vm_dl_get_loaded_context(u16 appId)
{
    int idx = vm_dl_find_loaded_index_by_app_id(appId);
    if (idx < 0)
        return 0;
    return g_vmDlLoadedApps[idx].context;
}

static void vm_dl_remove_loaded_index(int idx)
{
    u16 removedAppId;
    if (idx < 0 || (u32)idx >= g_vmDlLoadedCount)
        return;
    removedAppId = g_vmDlLoadedApps[idx].appId;
    if ((u32)idx + 1 < g_vmDlLoadedCount)
    {
        memmove(&g_vmDlLoadedApps[idx], &g_vmDlLoadedApps[idx + 1],
                (g_vmDlLoadedCount - (u32)idx - 1) * sizeof(g_vmDlLoadedApps[0]));
    }
    memset(&g_vmDlLoadedApps[g_vmDlLoadedCount - 1], 0, sizeof(g_vmDlLoadedApps[0]));
    g_vmDlLoadedCount--;
    if (g_vmDlCurrAppId == removedAppId)
    {
        g_vmDlCurrAppId = 0;
        g_vmDlCurrType = 0;
        g_dlSpBf = 0;
    }
    if (g_vmDlPreAppId == removedAppId)
        g_vmDlPreAppId = 0;
}

static void vm_dl_unload_loaded_app(u16 appId)
{
    int idx = vm_dl_find_loaded_index_by_app_id(appId);
    if (idx >= 0)
        vm_dl_remove_loaded_index(idx);
}

static u32 vm_read_current_pool_r9(void)
{
    u32 r9 = 0;
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 >= VM_Memory_Pool_ADDRESS && r9 < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
        return r9;
    return 0;
}

static bool vm_pool_module_base_has_code(u32 codeBase)
{
    u8 probe[16];
    if (codeBase < VM_Memory_Pool_ADDRESS ||
        codeBase + sizeof(probe) > VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        return false;
    }
    if (uc_mem_read(MTK, codeBase, probe, sizeof(probe)) != UC_ERR_OK)
        return false;
    for (u32 i = 0; i < sizeof(probe); ++i)
    {
        if (probe[i] != 0)
            return true;
    }
    return false;
}

static bool vm_cbe_api_ptr_looks_callable(u32 target)
{
    u32 pc = target & ~1u;
    u32 dataEnd = Program_Data_Address + g_cbeInfo.headerInt4;
    u32 codeEnd = Program_ROM_Address + g_cbeInfo.headerInt2;

    if (pc == 0)
        return false;
    if (pc == PROGRAM_EXIT_ADDR || vm_address_in_range(pc, VM_NATIVE_DISPATCH_ADDRESS, 4))
        return true;
    if (vm_is_manager_func_stub_address(pc))
        return true;
    if (vm_is_pool_entry(pc))
        return true;
    if (Program_Data_Address && dataEnd >= Program_Data_Address &&
        pc >= Program_Data_Address && pc < dataEnd)
    {
        return false;
    }
    if (Program_ROM_Address && Program_Data_Address > Program_ROM_Address &&
        pc >= Program_ROM_Address && pc < Program_Data_Address)
    {
        return true;
    }
    if (Program_ROM_Address && codeEnd >= Program_ROM_Address &&
        pc >= Program_ROM_Address && pc < codeEnd)
    {
        return true;
    }
    return false;
}

static bool vm_pool_module_r9_matches_pc(u32 pc, u32 moduleR9)
{
    pc &= ~1u;
    if (moduleR9 < VM_Memory_Pool_ADDRESS ||
        moduleR9 >= VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        return false;
    }
    {
        int idx = vm_dl_find_loaded_index_by_sp_bf(moduleR9);
        if (idx >= 0)
        {
            u32 buffer = g_vmDlLoadedApps[idx].buffer;
            u32 spBf = g_vmDlLoadedApps[idx].spBf;
            return buffer != 0 && spBf > buffer && pc >= buffer && pc < spBf;
        }
    }
    return false;
}

static void vm_pool_module_remember_r9(u32 moduleR9)
{
    if (!vm_pool_module_r9_matches_pc(moduleR9 - 0x14000u, moduleR9))
        return;
    for (u32 i = 0; i < g_poolModuleR9Count; ++i)
    {
        if (g_poolModuleR9s[i] == moduleR9)
            return;
    }
    if (g_poolModuleR9Count >= sizeof(g_poolModuleR9s) / sizeof(g_poolModuleR9s[0]))
    {
        memmove(g_poolModuleR9s, g_poolModuleR9s + 1,
                (sizeof(g_poolModuleR9s) / sizeof(g_poolModuleR9s[0]) - 1) * sizeof(g_poolModuleR9s[0]));
        g_poolModuleR9Count--;
    }
    g_poolModuleR9s[g_poolModuleR9Count++] = moduleR9;
}

static bool vm_pool_battle_chat_context_valid(u32 moduleR9)
{
    u32 uiObj = 0;
    u32 widthFunc = 0;
    if (moduleR9 < VM_Memory_Pool_ADDRESS ||
        moduleR9 + 0x2020u >= VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        return false;
    }
    if (uc_mem_read(MTK, moduleR9 + 0x2018u, &uiObj, sizeof(uiObj)) != UC_ERR_OK)
        return false;
    if (uiObj == 0 || !vm_is_writable_vm_range(uiObj, 0x20))
        return false;
    if (uc_mem_read(MTK, uiObj + 0x18u, &widthFunc, sizeof(widthFunc)) != UC_ERR_OK)
        return false;
    return widthFunc != 0 &&
           (vm_is_pool_entry(widthFunc) ||
            (widthFunc >= Program_ROM_Address && widthFunc < Program_ROM_Address + Program_ROM_Mapped_Size));
}

static u32 vm_module_r9_for_pool_pc(u32 pc)
{
    u32 currentR9 = 0;
    int idx = -1;

    pc &= ~1u;
    if (!vm_is_pool_entry(pc))
        return 0;

    uc_reg_read(MTK, UC_ARM_REG_R9, &currentR9);
    if (currentR9 && vm_pool_module_r9_matches_pc(pc, currentR9))
        return currentR9;

    if (g_currentScreenModuleBase && vm_pool_module_r9_matches_pc(pc, g_currentScreenModuleBase))
        return g_currentScreenModuleBase;
    if (g_dlSpBf && vm_pool_module_r9_matches_pc(pc, g_dlSpBf))
        return g_dlSpBf;

    idx = vm_dl_find_loaded_index_by_pc(pc);
    if (idx >= 0)
    {
        if (g_vmDlCurrAppId != 0 && g_vmDlLoadedApps[idx].appId == g_vmDlCurrAppId)
            return g_vmDlLoadedApps[idx].spBf;
        return g_vmDlLoadedApps[idx].spBf;
    }
    if (g_currentScreenModuleBase &&
        g_currentScreenModuleBase >= VM_Memory_Pool_ADDRESS &&
        g_currentScreenModuleBase < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        return g_currentScreenModuleBase;
    }
    if (g_dlSpBf &&
        g_dlSpBf >= VM_Memory_Pool_ADDRESS &&
        g_dlSpBf < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        return g_dlSpBf;
    }
    return 0;
}

static void vm_restore_r9_for_entry(u32 entry)
{
    u32 r9 = vm_module_r9_for_pool_pc(entry);
    if (r9 == 0)
        r9 = vm_is_pool_entry(entry) && g_currentScreenModuleBase ? g_currentScreenModuleBase : Global_R9;
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

static void vm_restore_main_r9_for_rom_code(u32 pc)
{
    static u32 s_restoreMainR9LimitCount = 0;
    u32 currentR9 = 0;
    u32 normalizedPc = pc & ~1u;

    if (!Global_R9 || normalizedPc < Program_ROM_Address || normalizedPc >= Program_ROM_Address + Program_ROM_Mapped_Size)
        return;
    uc_reg_read(MTK, UC_ARM_REG_R9, &currentR9);
    if (currentR9 == Global_R9)
        return;
    uc_reg_write(MTK, UC_ARM_REG_R9, &Global_R9);
    if (s_restoreMainR9LimitCount < 64)
    {
        ++s_restoreMainR9LimitCount;
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
        UC_ARM_REG_PC,
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
        UC_ARM_REG_PC,
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

static void vm_request_host_quit(const char *reason)
{
    if (g_hostQuitRequested)
        return;

    g_hostQuitRequested = 1;
    printf("[info][host] quit requested: %s\n", reason ? reason : "unknown");
    vm_autotest_note("host_quit_request reason=%s\n", reason ? reason : "unknown");
}

static void scheduler_clear_pending_async_tasks(void)
{
    memset(g_timerTasks, 0, sizeof(g_timerTasks));
    memset(g_netTasks, 0, sizeof(g_netTasks));
    memset(g_netChannels, 0, sizeof(g_netChannels));
    g_netTaskDispatchDepth = 0;
    g_netTaskDispatchSlot = -1;
    g_netBusinessSendReadyDeferred = 0;
    g_netBusinessSendReadyRerun = 0;
    g_netBusinessSendReadyPostVm = 0;
    g_loginTail42AllocPending = 0;
    g_loginTail42FlushPending = 0;
    g_wpayMockFlowActive = 0;
}

static u32 vm_screen_call_param_for_quit(u32 screen, u32 savedParam)
{
    if (savedParam)
        return savedParam;
    return vm_screen_default_call_param(screen);
}

static uc_err vm_destroy_screen_for_quit(u32 screen, u32 param, u32 moduleBase, u32 dataPackage,
                                         u32 exitAddr, u32 thumbExitAddr,
                                         const char *kind)
{
    if (screen == 0)
        return UC_ERR_OK;

    u32 destroyEntry = vm_get_var(screen + 4);
    if (destroyEntry == 0)
        return UC_ERR_OK;

    u32 savedModuleBase = g_currentScreenModuleBase;
    u32 savedScreenThis = g_currentScreenThis;
    u32 savedDataPackage = g_currentScreenDataPackage;
    u32 savedGlobalDataPackage = vm_current_data_package();
    if (moduleBase)
        g_currentScreenModuleBase = moduleBase;
    if (dataPackage)
    {
        g_currentScreenDataPackage = dataPackage;
        vm_restore_data_package(dataPackage);
    }
    g_currentScreenThis = param;

    printf("[info][screen] quit destroy kind=%s screen=%08x this=%08x destroy=%08x module=%08x dp=%08x\n",
           kind ? kind : "screen", screen, param, destroyEntry, g_currentScreenModuleBase, dataPackage);
    vm_autotest_note("screen_quit_destroy kind=%s screen=%08x this=%08x destroy=%08x module=%08x dp=%08x\n",
                     kind ? kind : "screen", screen, param, destroyEntry, g_currentScreenModuleBase, dataPackage);

    uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
    scheduler_prepare_screen_call(param);
    uc_reg_write(MTK, UC_ARM_REG_R0, &param);
    uc_err err = vm_emu_start(destroyEntry, exitAddr);

    g_currentScreenThis = savedScreenThis;
    g_currentScreenModuleBase = savedModuleBase;
    g_currentScreenDataPackage = savedDataPackage;
    vm_restore_data_package(savedGlobalDataPackage);
    return err;
}

static void vm_clear_screen_state_after_quit(void)
{
    screenStructChange = 0;
    screenStructNotifyLoadRes = 0;
    vmAddedScreen = 0;
    memset(g_screenStack, 0, sizeof(g_screenStack));
    memset(g_screenStackParam, 0, sizeof(g_screenStackParam));
    memset(g_screenStackModuleBase, 0, sizeof(g_screenStackModuleBase));
    memset(g_screenStackDataPackage, 0, sizeof(g_screenStackDataPackage));
    memset(g_screenStackFlags, 0, sizeof(g_screenStackFlags));
    memset(g_screenStackInited, 0, sizeof(g_screenStackInited));
    g_screenStackCount = 0;
    g_screenRemovedWithoutNext = 1;
    g_screenResumeExisting = 0;
    g_screenEnterExistingNoCallback = 0;
    g_activeScreenRemovedThisFrame = 0;
    g_activeScreenRemovedThis = 0;
    g_activeScreenRemovedModuleBase = 0;
    g_activeScreenRemovedDataPackage = 0;
    g_screenExitMode = VM_SCREEN_EXIT_SKIP;
    g_screenLoadResourcePendingScreen = 0;
    g_screenLoadResourcePendingParam = 0;
    g_currentScreenThis = 0;
    g_currentScreenModuleBase = 0;
    g_currentScreenDataPackage = 0;
    vm_dl_reset_state();
    memset(g_poolModuleR9s, 0, sizeof(g_poolModuleR9s));
    g_poolModuleR9Count = 0;
    g_screenRootExitArmed = 0;
    g_screenRootExitPending = 0;
    g_screenRootExitPendingRoot = 0;
    g_screenRootExitPendingRemoved = 0;
    g_screenRootExitPendingTick = 0;
    vm_set_var(VM_SCREEN_nextSubTScreen_ADDRESS, 0);
}

static uc_err vm_run_app_exit_for_quit(u32 exitAddr, u32 thumbExitAddr)
{
    if (g_appExitEntry == 0)
        return UC_ERR_OK;

    printf("[info][app] quit exit entry=%08x\n", g_appExitEntry);
    vm_autotest_note("app_quit_exit entry=%08x\n", g_appExitEntry);

    u32 zero = 0;
    uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
    uc_reg_write(MTK, UC_ARM_REG_R0, &zero);
    uc_reg_write(MTK, UC_ARM_REG_R1, &zero);
    uc_reg_write(MTK, UC_ARM_REG_R2, &zero);
    uc_reg_write(MTK, UC_ARM_REG_R3, &zero);
    return vm_emu_start(g_appExitEntry, exitAddr);
}

static uc_err vm_run_host_quit_cleanup(u32 exitAddr, u32 thumbExitAddr)
{
    if (!g_hostQuitRequested || g_hostQuitCleanupStarted)
        return UC_ERR_OK;

    g_hostQuitCleanupStarted = 1;
    u32 screens[32];
    u32 params[32];
    u32 modules[32];
    u32 dataPackages[32];
    u32 count = g_screenStackCount;
    if (count > sizeof(screens) / sizeof(screens[0]))
        count = sizeof(screens) / sizeof(screens[0]);

    for (u32 i = 0; i < count; ++i)
    {
        screens[i] = g_screenStack[i];
        params[i] = vm_screen_call_param_for_quit(g_screenStack[i], g_screenStackParam[i]);
        modules[i] = g_screenStackModuleBase[i];
        dataPackages[i] = g_screenStackDataPackage[i];
    }

    bool activeInStack = false;
    if (vmAddedScreen)
    {
        for (u32 i = 0; i < count; ++i)
        {
            if (screens[i] == vmAddedScreen)
            {
                activeInStack = true;
                break;
            }
        }
        if (!activeInStack && count < sizeof(screens) / sizeof(screens[0]))
        {
            screens[count] = vmAddedScreen;
            params[count] = g_currentScreenThis ? g_currentScreenThis : vmAddedScreen;
            modules[count] = g_currentScreenModuleBase ? g_currentScreenModuleBase : vm_screen_stack_lookup_module_base(vmAddedScreen);
            dataPackages[count] = g_currentScreenDataPackage ? g_currentScreenDataPackage : vm_screen_stack_lookup_data_package(vmAddedScreen);
            ++count;
        }
    }

    printf("[info][screen] host quit cleanup begin depth=%u current=%08x\n", count, vmAddedScreen);
    vm_autotest_note("screen_quit_begin depth=%u current=%08x\n", count, vmAddedScreen);

    scheduler_clear_pending_async_tasks();

    u32 one = 1;
    vm_set_var(VM_SCREEN_isInQuit_ADDRESS, one);

    uc_err err = UC_ERR_OK;
    for (u32 i = count; i > 0; --i)
    {
        u32 idx = i - 1;
        err = vm_destroy_screen_for_quit(screens[idx], params[idx], modules[idx], dataPackages[idx],
                                         exitAddr, thumbExitAddr, "stack");
        if (err != UC_ERR_OK)
            break;
        int liveIndex = vm_screen_stack_find_related(screens[idx]);
        if (liveIndex >= 0)
            g_screenStack[(u32)liveIndex] = 0;
    }

    u32 zero = 0;
    vm_set_var(VM_SCREEN_isInQuit_ADDRESS, zero);
    vm_clear_screen_state_after_quit();
    if (err == UC_ERR_OK)
        err = vm_run_app_exit_for_quit(exitAddr, thumbExitAddr);
    vm_close_open_files_for_restart();

    if (err == UC_ERR_OK)
    {
        printf("[info][screen] host quit cleanup complete\n");
        vm_autotest_note("screen_quit_complete\n");
    }
    return err;
}

static void scheduler_prepare_screen_call(u32 screenThisPtr)
{
    u32 screenFuncPtr = screenThisPtr ? screenThisPtr + 0x18 : 0;
    if (screenThisPtr != g_currentScreenThis)
    {
        g_currentScreenThis = screenThisPtr;
        g_currentScreenModuleBase = vm_screen_stack_lookup_module_base(screenFuncPtr);
    }
    else if (g_currentScreenModuleBase == 0)
    {
        g_currentScreenModuleBase = vm_screen_stack_lookup_module_base(screenFuncPtr);
    }
    if (g_currentScreenModuleBase == 0)
        g_currentScreenModuleBase = vm_dl_current_sp_bf();
    if (g_currentScreenModuleBase)
        vm_dl_note_sp_bf(g_currentScreenModuleBase, "screen-prepare");
    u32 dataPackage = vm_screen_stack_lookup_data_package(screenFuncPtr);
    if (dataPackage)
    {
        u32 oldDataPackage = g_currentScreenDataPackage;
        vm_restore_data_package(dataPackage);
        g_currentScreenDataPackage = dataPackage;
        vm_trace_screen_data_package_change("prepare-stack", screenFuncPtr,
                                            oldDataPackage, dataPackage, vm_current_data_package());
    }
    else
    {
        u32 currentDataPackage = vm_current_data_package();
        if (currentDataPackage != 0)
        {
            u32 oldDataPackage = g_currentScreenDataPackage;
            g_currentScreenDataPackage = currentDataPackage;
            vm_trace_screen_data_package_change("prepare-current", screenThisPtr + 0x18,
                                                oldDataPackage, currentDataPackage, currentDataPackage);
        }
    }
}

static void scheduler_note_screen_data_package(u32 screenThisPtr)
{
    static u32 s_skipLogCount = 0;
    if (screenThisPtr == 0)
        return;
    u32 expectedScreen = screenThisPtr + 0x18;
    if (vmAddedScreen == 0 || vmAddedScreen != expectedScreen)
    {
        if (s_skipLogCount < 32)
        {
            ++s_skipLogCount;
            printf("[info][screen] dp_capture_skip expected=%08x current=%08x this=%08x caller=%08x depth=%u\n",
                   expectedScreen, vmAddedScreen, screenThisPtr, lastAddress, g_screenStackCount);
            vm_autotest_note("screen_dp_capture_skip expected=%08x current=%08x this=%08x caller=%08x depth=%u\n",
                             expectedScreen, vmAddedScreen, screenThisPtr, lastAddress, g_screenStackCount);
        }
        return;
    }
    u32 dataPackage = vm_current_data_package();
    if (dataPackage == 0)
        return;
    u32 oldDataPackage = g_currentScreenDataPackage;
    vm_trace_screen_data_package_change("capture-current", screenThisPtr + 0x18,
                                        oldDataPackage, dataPackage, dataPackage);
    g_currentScreenDataPackage = dataPackage;
    vm_screen_stack_update_data_package(screenThisPtr + 0x18, dataPackage);
}

static u32 scheduler_get_tick_ms(void)
{
    u32 now = SDL_GetTicks();
    if (g_schedulerStartTicks == 0)
        g_schedulerStartTicks = now;
    return now - g_schedulerStartTicks;
}

static u32 scheduler_effective_timer_delay(u32 delayMs, u32 callback)
{
    /* WPay Ker42 waits 15s after SMS success before polling confirm. */
    if (g_wpayMockFlowActive && callback == 0x05187781u && delayMs > 1000u)
    {
        printf("[info][wpay] timer_fast_forward raw_delay=%u delay=1000 cb=%08x\n",
               delayMs, callback);
        vm_autotest_note("wpay_timer_fast_forward raw_delay=%u delay=1000 cb=%08x\n",
                         delayMs, callback);
        return 1000u;
    }
    return delayMs;
}

static u32 scheduler_start_timer(u32 delayMs, u32 callback, u32 context)
{
    if (callback == 0)
        return vm_set_call_result(0);
    u32 effectiveDelayMs = scheduler_effective_timer_delay(delayMs, callback);
    for (u32 i = 0; i < VM_SCHED_MAX_TIMERS; ++i)
    {
        if (!g_timerTasks[i].active)
        {
            g_timerTasks[i].active = 1;
            g_timerTasks[i].handle = (u16)(VM_SCHED_TIMER_BASE_ID + i);
            g_timerTasks[i].remainingTicks = (effectiveDelayMs + VM_SCHED_FRAME_MS - 1) / VM_SCHED_FRAME_MS;
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


static void scheduler_queue_net_event(u32 eventType, u32 r0, u32 r1, u32 r2, u32 callback, u32 context)
{
    static u32 s_netQueueObserveCount = 0;
    u32 activeBefore = scheduler_count_active_net_tasks();
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netTasks[i].active && g_netTasks[i].eventType == eventType && g_netTasks[i].r0 == r0 && g_netTasks[i].r1 == r1 && g_netTasks[i].r2 == r2 && g_netTasks[i].callback == callback && g_netTasks[i].context == context)
        {
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
            memset(&g_netTasks[i].remoteObservation, 0,
                   sizeof(g_netTasks[i].remoteObservation));
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
                }
            }
            DEBUG_PRINT("[probe_net] queue event=%u r0=%x r1=%x r2=%x cb=%x ctx=%x tick=%u last=%x\n", eventType, r0, r1, r2, callback, context, g_schedulerTick, lastAddress);
            if (s_netQueueObserveCount < 100)
            {
                ++s_netQueueObserveCount;
                vm_autotest_note("net_queue event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x\n",
                                 eventType, r0, r1, r2, callback, context);
            }
            return;
        }
    }
}

static bool scheduler_attach_net_remote_observation(
    u32 eventType, u32 r0, u32 callback, u32 context,
    const vm_net_remote_observation *observation)
{
    if (observation == NULL)
        return false;
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        vm_net_task *task = &g_netTasks[i];
        if (!task->active || task->fired || task->eventType != eventType ||
            task->r0 != r0 || task->callback != callback ||
            task->context != context)
        {
            continue;
        }
        task->remoteObservation = *observation;
        return true;
    }
    return false;
}

static void scheduler_queue_net_task(u32 r0, u32 r1, u32 callback, u32 context)
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
                pending->delayTicks = 0;
            }
            if (businessFastPath && g_netTaskDispatchDepth > 0 && pending->delayTicks == 0)
                g_netBusinessSendReadyDeferred = 1;
            if (businessFastPath && g_netTaskDispatchDepth == 0 && pending->delayTicks == 0)
            {
                g_netBusinessSendReadyPostVm = 1;
            }
            continue;
        }
        scheduler_queue_net_event(5, 0, 0, 0, channel->callback, channel->context);
        pending = scheduler_find_pending_net_event(5, channel->callback, channel->context);
        if (pending && businessFastPath)
            pending->delayTicks = 0;
        queued++;
        if (businessFastPath && g_netTaskDispatchDepth > 0)
            g_netBusinessSendReadyDeferred = 1;
    }
    if (businessFastPath)
    {
        if (g_netTaskDispatchDepth == 0)
        {
            g_netBusinessSendReadyPostVm = 1;
        }
    }
}

static uc_err scheduler_flush_post_vm_business_send_ready(const char *reason)
{
    if (!g_netBusinessSendReadyPostVm || g_netTaskDispatchDepth != 0)
        return UC_ERR_OK;
    g_netBusinessSendReadyPostVm = 0;
    uc_err err = scheduler_dispatch_net_tasks();
    if (err != UC_ERR_OK)
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
    if (!g_netCurrentObject || !wrapperFlush)
    {
        return UC_ERR_OK;
    }
    uc_err err = vm_call4_preserve_regs_clear_stack_args(wrapperFlush, g_netCurrentObject, 0, 0, 0);
    if (err != UC_ERR_OK)
    return err;
}










static uc_err scheduler_dispatch_net_tasks(void)
{
    u32 activeBefore = scheduler_count_active_net_tasks();
    if (activeBefore)
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
            vm_net_remote_observation taskRemoteObservation =
                task->remoteObservation;
            u32 remoteSceneTargetClearSerial = 0;
            task->fired = 1;
            task->active = 0;
            g_netTaskDispatchDepth++;
            g_netTaskDispatchSlot = (int)i;
            DEBUG_PRINT("[probe_net_fire] event=%u r0=%x r1=%x r2=%x cb=%x ctx=%x tick=%u\n", taskEvent, taskR0, taskR1, taskR2, taskCallback, taskContext, g_schedulerTick);
            if (g_netDebugReadWindow)
            {
                printf("[info][network] net_fire slot=%u event=%u r0=%08x r1=%u r2=%u cb=%08x ctx=%08x depth=%u\n",
                       i, taskEvent, taskR0, taskR1, taskR2,
                       taskCallback, taskContext, g_netTaskDispatchDepth);
            }
            if (taskEvent == 7)
            {
                if (task->downloadSnapshotValid && task->downloadSnapshotState == 2)
                {
                    u32 downloadBase = Global_R9 + 0x9584;
                    uc_mem_write(MTK, downloadBase, task->downloadSnapshot, sizeof(task->downloadSnapshot));
                }
            }
            remoteSceneTargetClearSerial = vm_net_mock_apply_remote_observation(
                &taskRemoteObservation);
            uc_err err = vm_call4_preserve_regs_clear_stack_args(taskCallback, taskR0, taskR1, taskR2, taskEvent);
            if (g_netDebugReadWindow)
            {
                printf("[info][network] net_done slot=%u event=%u cb=%08x err=%u remaining_read=%u/%u\n",
                       i, taskEvent, taskCallback, err,
                       g_netMockResponseOffset, g_netMockResponseLen);
            }
            g_netTaskDispatchDepth--;
            g_netTaskDispatchSlot = -1;
            if (err == UC_ERR_OK)
                vm_net_mock_finish_remote_observation(
                    remoteSceneTargetClearSerial);
            if (err != UC_ERR_OK)
                return err;
            if (taskEvent == 7)
            {
                u8 updateFlags[4] = {0};
                u8 updateState = 0;
                uc_mem_read(MTK, Global_R9 + 0x954c + 0x10, updateFlags, sizeof(updateFlags));
                uc_mem_read(MTK, Global_R9 + 0x4cb6, &updateState, 1);
                if (updateState == 2 && updateFlags[0] == 1 && updateFlags[1] == 1 && updateFlags[2] == 1 && updateFlags[3] == 1 &&
                    !g_netMockUpdateDelivered)
                {
                    g_netMockUpdateDelivered = 1;
                }
                if (g_netTaskDispatchDepth == 0 && vm_net_mock_should_rearm_send_ready())
                    scheduler_queue_send_ready_for_active_channels("net_data_event");
            }
            if (err == UC_ERR_OK && g_netTaskDispatchDepth == 0 && g_netBusinessSendReadyPostVm)
            {
                err = scheduler_flush_post_vm_business_send_ready("net_callback_return");
            }
            if (err == UC_ERR_OK && g_netTaskDispatchDepth == 0 && g_loginTail42FlushPending)
                err = scheduler_flush_login_tail42_if_needed("net_callback_return");
            if (err != UC_ERR_OK)
                return err;
        }
    }
    if (g_netTaskDispatchDepth == 0 && g_netBusinessSendReadyDeferred && !g_netBusinessSendReadyRerun)
    {
        g_netBusinessSendReadyDeferred = 0;
        g_netBusinessSendReadyRerun = 1;
        uc_err rerunErr = scheduler_dispatch_net_tasks();
        g_netBusinessSendReadyRerun = 0;
        if (rerunErr != UC_ERR_OK)
            return rerunErr;
    }
    return UC_ERR_OK;
}

/* Android ships only the remote game client transport.  The desktop build
 * keeps the embedded service, persistence and web-admin implementation. */
#ifdef CBE_CLIENT_ONLY
#include "network-client.c"
#else
#include "mock-server.c"
#endif

#define VM_NAMED_RESOURCE_DOWNLOAD_MAX (1024u * 1024u)
#define VM_NAMED_RESOURCE_RESPONSE_CAP 8192u

static bool vm_named_resource_put_bytes(u8 *out, u32 outCap, u32 *pos,
                                        const void *data, u32 len)
{
    if (out == NULL || pos == NULL || data == NULL || *pos > outCap ||
        len > outCap - *pos)
    {
        return false;
    }
    memcpy(out + *pos, data, len);
    *pos += len;
    return true;
}

static bool vm_named_resource_put_be16(u8 *out, u32 outCap, u32 *pos,
                                       u16 value)
{
    u8 bytes[2] = {(u8)(value >> 8), (u8)value};
    return vm_named_resource_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

static bool vm_named_resource_put_field(u8 *out, u32 outCap, u32 *pos,
                                        const char *name, const u8 *value,
                                        u16 valueLen)
{
    u8 nameLen = 0;
    if (name == NULL || value == NULL || strlen(name) > 0xffu)
        return false;
    nameLen = (u8)strlen(name);
    return vm_named_resource_put_bytes(out, outCap, pos, &nameLen, 1) &&
           vm_named_resource_put_bytes(out, outCap, pos, name, nameLen) &&
           vm_named_resource_put_be16(out, outCap, pos, valueLen) &&
           vm_named_resource_put_bytes(out, outCap, pos, value, valueLen);
}

static bool vm_named_resource_put_u8_field(u8 *out, u32 outCap, u32 *pos,
                                           const char *name, u8 value)
{
    u8 encoded[3] = {0, 1, value};
    return vm_named_resource_put_field(out, outCap, pos, name, encoded,
                                       sizeof(encoded));
}

static bool vm_named_resource_put_u16_field(u8 *out, u32 outCap, u32 *pos,
                                            const char *name, u16 value)
{
    u8 encoded[4] = {0, 2, (u8)(value >> 8), (u8)value};
    return vm_named_resource_put_field(out, outCap, pos, name, encoded,
                                       sizeof(encoded));
}

static bool vm_named_resource_put_u32_field(u8 *out, u32 outCap, u32 *pos,
                                            const char *name, u32 value)
{
    u8 encoded[6] = {0, 4, (u8)(value >> 24), (u8)(value >> 16),
                     (u8)(value >> 8), (u8)value};
    return vm_named_resource_put_field(out, outCap, pos, name, encoded,
                                       sizeof(encoded));
}

static bool vm_named_resource_put_string_field(u8 *out, u32 outCap, u32 *pos,
                                               const char *name,
                                               const char *value)
{
    u8 encoded[260];
    u32 valueLen = value ? (u32)strlen(value) : 0;
    if (valueLen > sizeof(encoded) - 2)
        return false;
    encoded[0] = (u8)(valueLen >> 8);
    encoded[1] = (u8)valueLen;
    if (valueLen != 0)
        memcpy(encoded + 2, value, valueLen);
    return vm_named_resource_put_field(out, outCap, pos, name, encoded,
                                       (u16)(valueLen + 2));
}

static u32 vm_named_resource_build_chunk_request(const char *name, u32 start,
                                                 u8 *out, u32 outCap)
{
    /* Uplink WT packets have no object-count byte: their first object starts
     * at offset 4 and uses a 5-byte {major, kind, subtype, len16} header.
     * Downlink WT packets do carry objectCount at offset 4 and start at 5. */
    u32 pos = 9;
    if (out == NULL || outCap < pos || name == NULL || name[0] == 0 ||
        !vm_named_resource_put_string_field(out, outCap, &pos, "name", name) ||
        !vm_named_resource_put_u8_field(out, outCap, &pos, "type", 0) ||
        !vm_named_resource_put_u32_field(out, outCap, &pos, "start", start) ||
        !vm_named_resource_put_u16_field(out, outCap, &pos, "version", 1) ||
        !vm_named_resource_put_u8_field(out, outCap, &pos, "clientmiss", 1))
    {
        return 0;
    }
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    out[5] = 18;
    out[6] = 7;
    out[7] = (u8)((pos - 4) >> 8);
    out[8] = (u8)(pos - 4);
    return pos;
}

static bool vm_named_resource_response_field(const u8 *response,
                                             u32 responseLen,
                                             const char *field,
                                             const u8 **value,
                                             u16 *valueLen)
{
    u32 pos = 11;
    u32 objectEnd = 0;
    u32 fieldLen = field ? (u32)strlen(field) : 0;
    u32 packetLen = 0;
    u32 objectLen = 0;

    if (value)
        *value = NULL;
    if (valueLen)
        *valueLen = 0;
    if (response == NULL || responseLen < 11 || fieldLen == 0 ||
        response[0] != 'W' || response[1] != 'T' || response[4] == 0 ||
        response[5] != 1 || response[6] != 18 || response[7] != 7)
    {
        return false;
    }
    packetLen = ((u32)response[2] << 8) | response[3];
    objectLen = ((u32)response[9] << 8) | response[10];
    if (packetLen > responseLen || packetLen < 11 || objectLen < 6 ||
        objectLen > packetLen - 5)
    {
        return false;
    }
    objectEnd = 5 + objectLen;
    while (pos < objectEnd)
    {
        u32 nameLen = response[pos++];
        u16 encodedLen = 0;
        if (nameLen > objectEnd - pos || objectEnd - pos - nameLen < 2)
            return false;
        if (nameLen == fieldLen && memcmp(response + pos, field, fieldLen) == 0)
        {
            pos += nameLen;
            encodedLen = (u16)(((u16)response[pos] << 8) | response[pos + 1]);
            pos += 2;
            if (encodedLen > objectEnd - pos)
                return false;
            if (value)
                *value = response + pos;
            if (valueLen)
                *valueLen = encodedLen;
            return true;
        }
        pos += nameLen;
        encodedLen = (u16)(((u16)response[pos] << 8) | response[pos + 1]);
        pos += 2;
        if (encodedLen > objectEnd - pos)
            return false;
        pos += encodedLen;
    }
    return false;
}

static bool vm_named_resource_response_u8(const u8 *response, u32 responseLen,
                                          const char *field, u8 *value)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;
    if (!vm_named_resource_response_field(response, responseLen, field,
                                          &encoded, &encodedLen) ||
        encodedLen != 3 || encoded[0] != 0 || encoded[1] != 1)
    {
        return false;
    }
    if (value)
        *value = encoded[2];
    return true;
}

static bool vm_named_resource_response_u32(const u8 *response, u32 responseLen,
                                           const char *field, u32 *value)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;
    if (!vm_named_resource_response_field(response, responseLen, field,
                                          &encoded, &encodedLen) ||
        encodedLen != 6 || encoded[0] != 0 || encoded[1] != 4)
    {
        return false;
    }
    if (value)
    {
        *value = ((u32)encoded[2] << 24) | ((u32)encoded[3] << 16) |
                 ((u32)encoded[4] << 8) | encoded[5];
    }
    return true;
}

static bool vm_named_resource_response_blob(const u8 *response,
                                            u32 responseLen,
                                            const char *field,
                                            const u8 **data, u16 *dataLen)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;
    u16 nestedLen = 0;
    if (data)
        *data = NULL;
    if (dataLen)
        *dataLen = 0;
    if (!vm_named_resource_response_field(response, responseLen, field,
                                          &encoded, &encodedLen) ||
        encodedLen < 2)
    {
        return false;
    }
    nestedLen = (u16)(((u16)encoded[0] << 8) | encoded[1]);
    if ((u32)nestedLen + 2u != encodedLen)
        return false;
    if (data)
        *data = encoded + 2;
    if (dataLen)
        *dataLen = nestedLen;
    return true;
}

static bool vm_named_resource_leaf_is_safe(const char *leaf)
{
    const char *ext = NULL;
    size_t len = leaf ? strlen(leaf) : 0;
    if (len == 0 || len >= 128)
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char ch = (unsigned char)leaf[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
              ch == '.'))
        {
            return false;
        }
    }
    ext = strrchr(leaf, '.');
    return ext != NULL &&
           (_stricmp(ext, ".actor") == 0 || _stricmp(ext, ".gif") == 0);
}

static bool vm_file_try_download_named_resource(const char *normalizedPath)
{
    static bool active = false;
    u8 request[512];
    u8 response[VM_NAMED_RESOURCE_RESPONSE_CAP];
    char target[256];
    char temp[288];
    const char *leaf = NULL;
    FILE *fp = NULL;
    u32 start = 0;
    u32 totalSize = 0;
    u32 runningCrc = 0;
    bool ok = false;

    if (active || g_mockServiceOnly || g_mockServiceClientId == 0 ||
        normalizedPath == NULL || normalizedPath[0] == 0)
    {
        return false;
    }
    if (strncmp(normalizedPath, "JHOnlineData/", 13) == 0)
        leaf = normalizedPath + 13;
    else if (strchr(normalizedPath, '/') == NULL &&
             strchr(normalizedPath, '\\') == NULL)
        leaf = normalizedPath;
    if (!vm_named_resource_leaf_is_safe(leaf))
        return false;
    snprintf(target, sizeof(target), "%s", normalizedPath);
    if (snprintf(temp, sizeof(temp), "%s.cbe-download.tmp", target) >=
        (int)sizeof(temp))
    {
        return false;
    }

    active = true;
    vm_file_ensure_parent_dirs(target);
    fp = fopen(temp, "wb");
    if (fp == NULL)
        goto done;
    printf("[info][resource] resource_cache_miss_download_begin file=%s protocol=WT18/7 source=server-catalog\n",
           leaf);

    while (start < VM_NAMED_RESOURCE_DOWNLOAD_MAX)
    {
        const u8 *chunk = NULL;
        u16 chunkLen = 0;
        u8 result = 0;
        u32 responseLen = 0;
        u32 eventType = 7;
        u32 responseTotal = 0;
        u32 responseCrc = 0;
        u32 requestLen = vm_named_resource_build_chunk_request(
            leaf, start, request, sizeof(request));
        bool requestOk = false;
        if (requestLen == 0)
            break;
#ifdef CBE_CLIENT_ONLY
        requestOk = vm_client_remote_request(
            request, requestLen, response, sizeof(response), &responseLen,
            &eventType, NULL, NULL, 0, NULL);
#else
        requestOk = vm_net_mock_remote_request(
                        request, requestLen, response, sizeof(response),
                        &responseLen, &eventType, NULL, NULL, 0, NULL) != 0;
#endif
        if (!requestOk || eventType != 7 || responseLen == 0 ||
            !vm_named_resource_response_u8(response, responseLen, "result",
                                           &result) ||
            result != 1 ||
            !vm_named_resource_response_u32(response, responseLen, "totalsize",
                                            &responseTotal) ||
            responseTotal == 0 ||
            responseTotal > VM_NAMED_RESOURCE_DOWNLOAD_MAX ||
            !vm_named_resource_response_u32(response, responseLen, "crc",
                                            &responseCrc) ||
            !vm_named_resource_response_blob(response, responseLen, "data",
                                             &chunk, &chunkLen) ||
            chunkLen == 0 || start + chunkLen > responseTotal ||
            (totalSize != 0 && totalSize != responseTotal))
        {
            break;
        }
        totalSize = responseTotal;
        if (fwrite(chunk, 1, chunkLen, fp) != chunkLen)
            break;
        for (u32 i = 0; i < chunkLen; ++i)
            runningCrc += (u32)(int)(signed char)chunk[i];
        if (runningCrc != responseCrc)
            break;
        start += chunkLen;
        printf("[info][resource] resource_cache_download_chunk file=%s start=%u chunk=%u total=%u\n",
               leaf, start - chunkLen, (u32)chunkLen, totalSize);
        if (start == totalSize)
        {
            ok = true;
            break;
        }
    }

done:
    if (fp != NULL)
        fclose(fp);
    if (ok)
    {
        if (rename(temp, target) != 0)
            ok = false;
    }
    if (!ok)
        remove(temp);
    if (ok)
    {
        printf("[info][resource] resource_cache_download_complete file=%s bytes=%u crc=%u protocol=WT18/7 install=%s\n",
               leaf, totalSize, runningCrc, target);
    }
    else
    {
        printf("[warn][resource] resource_cache_download_failed file=%s received=%u total=%u protocol=WT18/7\n",
               leaf, start, totalSize);
    }
    active = false;
    return ok;
}

static uc_err scheduler_dispatch_input_event(vm_event *evt);

static u32 vm_net_queue_http_get_mock_response(u32 urlPtr, u32 callback, u32 context)
{
    char url[512];
    const u8 defaultBody[] = {1};
    const u8 wpayPaySuccessBody[] = {
        1,       /* WAPPAY packet ok */
        0, 1,    /* pay type */
        0, 1,    /* pay amount/count */
        1,       /* allow default handling */
        1,       /* auto confirm flag */
        0, 8,    /* SMS destination length */
        '1', '0', '6', '5', '8', '8', '8', '8',
        1,       /* message enabled */
        0, 5,    /* SMS body length */
        'P', 'A', 'Y', 'O', 'K',
        0, 1,    /* retry/progress interval, seconds */
        1,       /* consume-service packet ok */
        1,       /* consume-service default flag */
        1,       /* consume-service channel */
        0,       /* service id */
        0,       /* service name */
        0,       /* item name */
        0,       /* order id */
        0,       /* extra */
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0};
    const u8 *body = defaultBody;
    u32 bodyLen = (u32)sizeof(defaultBody);
    u32 responsePtr;

    vm_readStringByPtrLimited(urlPtr, (u8 *)url, sizeof(url));
    if (strstr(url, "ac=confirm&") != NULL)
    {
        g_wpayMockFlowActive = 0;
        scheduler_queue_net_event(1, 0, 0, 0, callback, context);
        scheduler_queue_net_event(0, 0, 0, 0, callback, context);
        printf("[info][network] http_get_mock url=%s events=1,0 cb=%08x ctx=%08x\n",
               url[0] ? url : "-", callback, context);
        vm_autotest_note("http_get_mock url=%s events=1,0 cb=%08x ctx=%08x\n",
                         url[0] ? url : "-", callback, context);
        return 1;
    }

    if (strstr(url, "ac=pay&") != NULL)
    {
        g_wpayMockFlowActive = 1;
        body = wpayPaySuccessBody;
        bodyLen = (u32)sizeof(wpayPaySuccessBody);
    }

    responsePtr = vm_net_mock_sync_buffer_to_vm(body, bodyLen);
    if (responsePtr == 0)
        return 0;

    scheduler_queue_net_event(0, responsePtr, bodyLen, bodyLen, callback, context);
    printf("[info][network] http_get_mock url=%s body=%u cb=%08x ctx=%08x\n",
           url[0] ? url : "-", bodyLen, callback, context);
    vm_autotest_note("http_get_mock url=%s body=%u cb=%08x ctx=%08x\n",
                     url[0] ? url : "-", bodyLen, callback, context);
    return responsePtr;
}

typedef struct
{
    char scene[64];
    u16 x;
    u16 y;
    u32 exitId;
    u32 serial;
    u8 valid;
} vm_scene_same_reenter_guard;

static vm_scene_same_reenter_guard g_sceneSameReenterGuard = {0};

static void vm_scene_same_reenter_clear(void)
{
    memset(&g_sceneSameReenterGuard, 0, sizeof(g_sceneSameReenterGuard));
}

static const vm_net_mock_scene_change_target *vm_active_scene_reenter_target(void)
{
    if (g_vm_net_mock_last_scene_change_target_valid)
        return &g_vm_net_mock_last_scene_change_target;
    vm_scene_same_reenter_clear();
    return NULL;
}

static bool vm_scene_same_reenter_matches_target(const vm_net_mock_scene_change_target *target)
{
    return target != NULL &&
           g_sceneSameReenterGuard.valid &&
           g_sceneSameReenterGuard.serial == g_vm_net_mock_last_scene_change_target_serial &&
           vm_net_mock_scene_names_equal_loose(g_sceneSameReenterGuard.scene, target->scene);
}

static void vm_scene_same_reenter_remember_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    vm_scene_same_reenter_clear();
    snprintf(g_sceneSameReenterGuard.scene, sizeof(g_sceneSameReenterGuard.scene),
             "%s", target->scene);
    g_sceneSameReenterGuard.x = target->x;
    g_sceneSameReenterGuard.y = target->y;
    g_sceneSameReenterGuard.exitId = target->exitId;
    g_sceneSameReenterGuard.serial = g_vm_net_mock_last_scene_change_target_serial;
    g_sceneSameReenterGuard.valid = 1;
}

static uc_err scheduler_dispatch_tscreen_event(u32 tScreenEventEntry, u32 screenPtr)
{
    simulateKey = 0;
    simulatePress = 0;
    vm_clear_key_down_state();
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
        vm_note_key_state_event(evt->r0, evt->r1);
        if (tScreenEventEntry == 0)
            return UC_ERR_OK;

        u32 keyMask = 0;
        keyMask = vm_key_mask_from_code(evt->r0);
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

        u32 touchEventType = evt->r0 == MR_MOUSE_UP ? 4 : (evt->r0 == MR_MOUSE_MOVE ? 5 : 3);
        u32 touchPtr = vm_malloc_var();
        vm_set_var(touchPtr, evt->r1);
        uc_err err = vm_call4(tScreenEventEntry, screenPtr, touchEventType, touchPtr, 0);
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

static void vm_input_update_sdl_text_rect(void)
{
    if (!window)
        return;
    int winW = LcdGetWindowWidth();
    int winH = LcdGetWindowHeight();
    int toolbarH = LcdGetToolbarHeight();
    int baseW = LcdGetViewWidth();
    int baseH = LcdGetViewHeight();
    SDL_Rect rect;
    SDL_GetWindowSize(window, &winW, &winH);
    LcdVmRectToWindowRect(g_vmInputOverlayX, g_vmInputOverlayY,
                          g_vmInputOverlayW, g_vmInputOverlayH, &rect);
    if (baseW > 0 && baseH > 0)
    {
        int viewH = winH - toolbarH;
        if (viewH < 1)
            viewH = 1;
        rect.x = rect.x * winW / baseW;
        rect.y = toolbarH + rect.y * viewH / baseH;
        rect.w = rect.w * winW / baseW;
        rect.h = rect.h * viewH / baseH;
    }
    SDL_SetTextInputRect(&rect);
}

static void vm_input_request_sdl_text_input(int open)
{
    g_vmInputSdlTextInputWanted = open ? 1 : 0;
}

static void vm_input_sync_sdl_text_input(void)
{
    if (g_vmInputSdlTextInputWanted && !g_vmInputSdlTextInputActive)
    {
        vm_input_update_sdl_text_rect();
        SDL_StartTextInput();
        g_vmInputSdlTextInputActive = 1;
    }
    else if (!g_vmInputSdlTextInputWanted && g_vmInputSdlTextInputActive)
    {
        SDL_StopTextInput();
        g_vmInputSdlTextInputActive = 0;
        g_vmInputComposition[0] = 0;
    }
    else if (g_vmInputSdlTextInputWanted && g_vmInputSdlTextInputActive)
    {
        vm_input_update_sdl_text_rect();
    }
}

static u32 vm_input_decode_utf8_char(const char **cursor)
{
    const unsigned char *s = (const unsigned char *)(cursor ? *cursor : NULL);
    u32 ch = 0;

    if (s == NULL || *s == 0)
        return 0;
    if (s[0] < 0x80)
    {
        ch = s[0];
        *cursor += 1;
        return ch;
    }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80)
    {
        ch = ((u32)(s[0] & 0x1F) << 6) |
             (u32)(s[1] & 0x3F);
        *cursor += 2;
        return ch;
    }
    if ((s[0] & 0xF0) == 0xE0 &&
        (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80)
    {
        ch = ((u32)(s[0] & 0x0F) << 12) |
             ((u32)(s[1] & 0x3F) << 6) |
             (u32)(s[2] & 0x3F);
        *cursor += 3;
        return ch;
    }
    if ((s[0] & 0xF8) == 0xF0 &&
        (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80)
    {
        ch = ((u32)(s[0] & 0x07) << 18) |
             ((u32)(s[1] & 0x3F) << 12) |
             ((u32)(s[2] & 0x3F) << 6) |
             (u32)(s[3] & 0x3F);
        *cursor += 4;
        return ch;
    }

    *cursor += 1;
    return 0;
}

static void vm_input_enqueue_utf8_text(const char *text)
{
    const char *cursor = text;
    while (cursor != NULL && *cursor)
    {
        u32 ch = vm_input_decode_utf8_char(&cursor);
        if (ch >= 0x20 && ch <= 0xffff)
            EnqueueVMEvent(VM_EVENT_INPUT_CHAR, ch, 0);
    }
}

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

static int vm_lcd_font_width_for_mode(u32 mode)
{
    return mode ? getFontWidth() : getFontCellWidth();
}

static int vm_lcd_measure_current_string(const u8 *gbkText)
{
    return mesureStringWidthWithGbkWidth((char *)gbkText, vm_lcd_current_gbk_width());
}

static int vm_lcd_measure_current_string_render_width(const u8 *gbkText)
{
    return mesureStringRenderWidthWithGbkWidth((char *)gbkText, vm_lcd_current_gbk_width());
}

static void vm_lcd_draw_current_string(u8 *gbkText, int x, int y, u16 color)
{
    int w = vm_lcd_measure_current_string_render_width(gbkText);
    int h = getFontHeight();
    vm_lcd_sync_vm_rect_to_cache(x, y, w, h);
    drawFontStringWithGbkWidth(gbkText, x, y, color, vm_lcd_current_gbk_width());
}

static void vm_lcd_sync_string_to_vm(const u8 *gbkText, int x, int y)
{
    int w = vm_lcd_measure_current_string_render_width(gbkText);
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
    utf8_to_gbk((u8 *)"SDL文本输入", hintGbk, sizeof(hintGbk));
    drawFontString(hintGbk, x + 4, y - 18, 0xffe0);
    drawFontString(sprintfBuff, x + 5, y + 4, 0xffff);
    if (!g_vmInputPassword && g_vmInputComposition[0] != 0)
    {
        u8 compositionGbk[64] = {0};
        utf8_to_gbk((u8 *)g_vmInputComposition, compositionGbk, sizeof(compositionGbk));
        int compositionX = x + 7 + mesureStringWidth((char *)sprintfBuff);
        if (compositionX < x + w - 12)
            drawFontString(compositionGbk, compositionX, y + 4, 0x9fe6);
    }
    if ((clock() / (CLOCKS_PER_SEC / 2)) % 2 == 0)
    {
        int caretX = x + 6 + mesureStringWidth((char *)sprintfBuff);
        if (caretX > x + w - 8)
            caretX = x + w - 8;
        vm_lcd_fill_rect_local(caretX, y + 4, 1, h - 8, 0xffff);
    }
}

static void vm_frame_delay(u32 ms) { SDL_Delay(ms); }

static void vm_lcd_update_with_input_overlay(void)
{
    uc_mem_read(MTK, VM_screenImage_ADDRESS, Lcd_Cache_Buffer, LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
#ifndef CBE_PLATFORM_NO_WINDOW
    vm_input_draw_overlay();
#endif
    UpdateLcd();
}

static void vm_debug_read_guest_cstr(u32 addr, char *out, size_t outCap)
{
    u8 ch = 0;
    size_t i = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (addr == 0)
        return;

    for (; i + 1 < outCap; ++i)
    {
        uc_mem_read(MTK, addr + (u32)i, &ch, 1);
        out[i] = (char)ch;
        if (ch == 0)
            return;
    }
    out[outCap - 1] = 0;
}

static void vm_debug_read_guest_ucs2_as_gbk(u32 addr, char *out, size_t outCap, u32 maxChars)
{
    u32 chars = 0;
    u32 bytes = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (addr == 0 || maxChars == 0)
        return;

    chars = vm_input_wcslen_limit(addr, maxChars);
    bytes = (chars + 1) * 2;
    if (bytes > mySizeOf(cbeTextString))
        bytes = mySizeOf(cbeTextString) & ~1u;
    if (bytes < 2)
        bytes = 2;
    if (uc_mem_read(MTK, addr, cbeTextString, bytes) != UC_ERR_OK)
        return;
    if (ucs2_to_gbk(cbeTextString, bytes, (u8 *)out, (u32)outCap) < 0)
        out[0] = 0;
}

static void vm_debug_log_login_input_state(const char *phase, u32 callback, u32 inputBuffer)
{
    u32 r9 = 0;
    u32 loginRecord = 0;
    u32 displayPassword = 0;
    u32 displayUser = 0;
    u32 editPassword = 0;
    u32 editUser = 0;
    u8 selected = 0;
    u8 loginFlag = 0;
    char displayPasswordText[64];
    char displayUserText[64];
    char recordUserText[64];
    char recordPasswordText[64];

    memset(displayPasswordText, 0, sizeof(displayPasswordText));
    memset(displayUserText, 0, sizeof(displayUserText));
    memset(recordUserText, 0, sizeof(recordUserText));
    memset(recordPasswordText, 0, sizeof(recordPasswordText));

    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0)
        return;
    uc_mem_read(MTK, r9 + 10772, &loginRecord, 4);
    uc_mem_read(MTK, r9 + 10812, &displayPassword, 4);
    uc_mem_read(MTK, r9 + 10816, &displayUser, 4);
    uc_mem_read(MTK, r9 + 10824, &editPassword, 4);
    uc_mem_read(MTK, r9 + 10828, &editUser, 4);
    uc_mem_read(MTK, r9 + 10731, &selected, 1);
    if (loginRecord != 0)
    {
        uc_mem_read(MTK, loginRecord, &loginFlag, 1);
        vm_debug_read_guest_cstr(loginRecord + 16, recordUserText, sizeof(recordUserText));
        vm_debug_read_guest_cstr(loginRecord + 48, recordPasswordText, sizeof(recordPasswordText));
    }
    vm_debug_read_guest_cstr(displayUser, displayUserText, sizeof(displayUserText));
    vm_debug_read_guest_cstr(displayPassword, displayPasswordText, sizeof(displayPasswordText));

    printf("[debug][vmInput] %s cb=%08x input=%08x target=%08x r9=%08x sel=%u flag=%u dispU=%08x dispP=%08x editU=%08x editP=%08x user='%s' pass='%s' recUser='%s' recPass='%s'\n",
           phase ? phase : "-",
           callback,
           inputBuffer,
           g_vmInputTargetBuffer,
           r9,
           selected,
           loginFlag,
           displayUser,
           displayPassword,
           editUser,
           editPassword,
           displayUserText,
           displayPasswordText,
           recordUserText,
           recordPasswordText);
}

static void vm_lcd_init_screen_image_struct(void)
{
    uc_mem_write(MTK, VM_screenImageStruct_ADDRESS, emptyBuff, 24);
    vm_set_var(VM_screenImageStruct_ADDRESS, VM_screenImage_ADDRESS);
    vm_set_var_short(VM_screenImageStruct_ADDRESS + 4, LCD_WIDTH);
    vm_set_var_short(VM_screenImageStruct_ADDRESS + 6, LCD_HEIGHT);
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

static u32 vm_input_buffer_bytes(u32 maxLen)
{
    if (maxLen == 0 || maxLen > 0xffff)
        return 0;
    return maxLen * 2;
}

static int vm_input_ensure_scratch_buffer(u32 bytes)
{
    if (bytes == 0)
        return 0;

    if (g_vmInputScratchBuffer && g_vmInputScratchBytes >= bytes)
        return 1;

    if (g_vmInputScratchBuffer)
    {
        vm_free(g_vmInputScratchBuffer);
        g_vmInputScratchBuffer = 0;
        g_vmInputScratchBytes = 0;
    }

    g_vmInputScratchBuffer = vm_malloc(bytes);
    g_vmInputScratchBytes = bytes;
    return g_vmInputScratchBuffer != 0;
}

static void vm_input_copy_guest_bytes(u32 dst, u32 src, u32 bytes)
{
    u32 copied = 0;

    while (copied < bytes)
    {
        u32 chunk = SDL_min(bytes - copied, (u32)mySizeOf(cbeTextString));
        if (uc_mem_read(MTK, src + copied, cbeTextString, chunk) != UC_ERR_OK)
            break;
        if (uc_mem_write(MTK, dst + copied, cbeTextString, chunk) != UC_ERR_OK)
            break;
        copied += chunk;
    }
}

static void vm_input_append_char(u32 ch)
{
    if (!g_vmInputOpen || !g_vmInputBuffer || g_vmInputMaxLen <= 1)
        return;

    if (ch < 0x20 || ch > 0xffff)
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
    u32 currentR9 = 0;
    u32 callbackR9 = 0;
    u32 displayUser = 0;
    vm_debug_log_login_input_state(result ? "finish-cancel-before" : "finish-ok-before", callback, buffer);
    vm_input_request_sdl_text_input(0);
    g_vmInputOpen = 0;
    g_vmInputComposition[0] = 0;

    if (!callback)
        return UC_ERR_OK;

    uc_reg_read(MTK, UC_ARM_REG_R9, &currentR9);
    if (currentR9 != 0)
        uc_mem_read(MTK, currentR9 + 10816, &displayUser, 4);
    callbackR9 = vm_module_r9_for_pool_pc(callback);
    if (callbackR9 == 0)
        callbackR9 = vm_is_pool_entry(callback) && g_currentScreenModuleBase ? g_currentScreenModuleBase : Global_R9;
    g_vmInputWatchUserBuf = displayUser;
    g_vmInputWatchUserBufLen = displayUser ? 64 : 0;
    g_vmInputWatchCallback = callback;
    g_vmInputWatchCallR9 = callbackR9;
    g_vmInputWatchWriteCount = 0;
    printf("[debug][vmInput] callback-call cb=%08x pool=%u callR9=%08x curR9=%08x dispU=%08x target=%08x input=%08x\n",
           callback,
           vm_is_pool_entry(callback) ? 1u : 0u,
           callbackR9,
           currentR9,
           displayUser,
           g_vmInputTargetBuffer,
           buffer);
    uc_err err = vm_call4_preserve_regs_clear_stack_args(callback, result ? 1 : 0, buffer, callback, 0);
    g_vmInputWatchUserBuf = 0;
    g_vmInputWatchUserBufLen = 0;
    g_vmInputWatchCallback = 0;
    g_vmInputWatchCallR9 = 0;
    g_vmInputWatchWriteCount = 0;
    g_vmInputCallback = 0;
    g_vmInputBuffer = 0;
    g_vmInputTargetBuffer = 0;
    g_vmInputMaxLen = 0;
    g_vmInputInputType = 0;
    g_vmInputPrompt = 0;
    g_vmInputPassword = 0;
    vm_debug_log_login_input_state(result ? "finish-cancel-after" : "finish-ok-after", callback, buffer);
    return err;
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
    u32 scratchBytes = 0;
    u32 copyUnits = 0;
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
    ++g_vmInputSerial;
    if (g_vmInputSerial == 0)
        g_vmInputSerial = 1;
    g_vmInputCallback = callback;
    g_vmInputMaxLen = maxLen & 0xffff;
    if (g_vmInputMaxLen == 0)
        g_vmInputMaxLen = maxLen;
    scratchBytes = vm_input_buffer_bytes(g_vmInputMaxLen);
    if (!vm_input_ensure_scratch_buffer(scratchBytes))
    {
        printf("[vmInput] failed to reserve scratch buffer maxLen=%u bytes=%u\n", g_vmInputMaxLen, scratchBytes);
        assert(0);
    }
    vm_try_write_zero(g_vmInputScratchBuffer, scratchBytes);
    copyUnits = vm_input_wcslen_limit(buffer, g_vmInputMaxLen > 0 ? g_vmInputMaxLen - 1 : 0);
    if (copyUnits > 0)
        vm_input_copy_guest_bytes(g_vmInputScratchBuffer, buffer, copyUnits * 2);
    g_vmInputTargetBuffer = buffer;
    g_vmInputBuffer = g_vmInputScratchBuffer;
    g_vmInputInputType = inputType & 0xff;
    g_vmInputPrompt = prompt;
    g_vmInputOverlayX = 12;
    g_vmInputOverlayY = password ? 372 : 344;
    g_vmInputOverlayW = 216;
    g_vmInputOverlayH = 22;
    g_vmInputComposition[0] = 0;
    vm_debug_log_login_input_state(password ? "open-pass" : "open-text", callback, buffer);
    vm_input_request_sdl_text_input(1);
    vm_set_call_result(1);
}

#ifdef CBE_PLATFORM_ANDROID
int cbeAndroidInputIsOpen(void)
{
    return g_vmInputOpen ? 1 : 0;
}

int cbeAndroidInputIsPassword(void)
{
    return g_vmInputPassword ? 1 : 0;
}

int cbeAndroidInputGetSerial(void)
{
    return (int)g_vmInputSerial;
}

int cbeAndroidInputGetMaxLen(void)
{
    return (int)g_vmInputMaxLen;
}

int cbeAndroidInputGetInputType(void)
{
    return (int)g_vmInputInputType;
}

const char *cbeAndroidInputGetTextUtf8(void)
{
    static char utf8[1024];
    u8 ucs2[512];
    u8 gbk[512];
    u32 maxChars;
    u32 len;
    u32 bytes;

    memset(utf8, 0, sizeof(utf8));
    memset(ucs2, 0, sizeof(ucs2));
    memset(gbk, 0, sizeof(gbk));
    if (!g_vmInputOpen || !g_vmInputBuffer || !MTK)
        return utf8;
    maxChars = g_vmInputMaxLen ? g_vmInputMaxLen : 255;
    if (maxChars > 255)
        maxChars = 255;
    len = vm_input_wcslen_limit(g_vmInputBuffer, maxChars);
    bytes = (len + 1) * 2;
    if (bytes > sizeof(ucs2))
        bytes = sizeof(ucs2);
    uc_mem_read(MTK, g_vmInputBuffer, ucs2, bytes);
    ucs2[sizeof(ucs2) - 2] = 0;
    ucs2[sizeof(ucs2) - 1] = 0;
    ucs2_to_gbk(ucs2, bytes, gbk, sizeof(gbk));
    gbk_to_utf8(gbk, (u8 *)utf8, sizeof(utf8));
    return utf8;
}

const char *cbeAndroidInputGetPromptUtf8(void)
{
    static char utf8[256];
    char gbk[128];
    memset(utf8, 0, sizeof(utf8));
    memset(gbk, 0, sizeof(gbk));
    if (g_vmInputPrompt && MTK)
        vm_debug_read_guest_ucs2_as_gbk(g_vmInputPrompt, gbk, sizeof(gbk), 63);
    if (gbk[0] != 0)
        gbk_to_utf8((u8 *)gbk, (u8 *)utf8, sizeof(utf8));
    if (utf8[0] == 0)
        snprintf(utf8, sizeof(utf8), "%s", g_vmInputPassword ? "请输入密码" : "请输入文本");
    return utf8;
}

void cbeAndroidInputSubmitUtf16(const unsigned short *text, int len, int cancelled)
{
    u32 copyLen;
    u32 maxChars;
    if (!g_vmInputOpen)
        return;
    if (!cancelled && g_vmInputBuffer && g_vmInputMaxLen > 0)
    {
        maxChars = g_vmInputMaxLen - 1;
        copyLen = len > 0 ? (u32)len : 0;
        if (copyLen > maxChars)
            copyLen = maxChars;
        for (u32 i = 0; i < copyLen; ++i)
            vm_input_write_u16(g_vmInputBuffer + i * 2, text ? text[i] : 0);
        vm_input_write_u16(g_vmInputBuffer + copyLen * 2, 0);
    }
    EnqueueVMEvent(VM_EVENT_INPUT_DONE, cancelled ? 1 : 0, 0);
}
#endif

static uc_err scheduler_tick(void)
{
    g_schedulerTick++;
    currentTime = clock();
    uc_err err = scheduler_dispatch_timers();
    if (err != UC_ERR_OK)
        return err;
    /* Host TCP completes on its worker thread.  Only the emulator thread may
     * allocate/write VM buffers and invoke the CBE network callback. */
    vm_net_mock_async_drain_completions();
    vm_net_mock_poll_push_if_due();
    err = scheduler_dispatch_net_tasks();
    if (err != UC_ERR_OK)
        return err;
    vm_screen_root_exit_maybe_request();
    return UC_ERR_OK;
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
    if (key == SDLK_F12 && type == MR_KEY_PRESS)
    {
        LcdCycleRotation();
        printf("[info][lcd] rotate=%s view=%dx%d window=%dx%d\n",
               LcdRotationName(LcdGetRotation()),
               LcdGetViewWidth(), LcdGetViewHeight(),
               LcdGetWindowWidth(), LcdGetWindowHeight());
        vm_lcd_update_with_input_overlay();
        return;
    }
    if (key >= 0x30 && key <= 0x39)
    { // 数字键盘1-9
        skey = key - 0x30;
    }
    else if (key == 0x77) // w
    {
        skey = 17; // 上
    }
    else if (key == 0x73) // s
    {
        skey = 18; // 下
    }
    else if (key == 0x61) // a
    {
        skey = 15; // 左
    }
    else if (key == 0x64) // d
    {
        skey = 16; // 右
    }

    else if (key == 0x66) // f
    {
        skey = 14; // OK
    }
    else if (key == 0x71) // q
    {
        skey = 12; // 左软
    }
    else if (key == 0x65) // e
    {
        skey = 13; // 右软
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

static void windowMouseEvent(int type, int windowX, int windowY)
{
    int x = windowX;
    int y = windowY;
    LcdWindowPointToVm(windowX, windowY, &x, &y);
    mouseEvent(type, x, y);
}

static void vm_autotest_release_tap(void)
{
    if (g_autotestTapReleaseWindow)
        windowMouseEvent(MR_MOUSE_UP, g_autotestTapReleaseX, g_autotestTapReleaseY);
    else
        mouseEvent(MR_MOUSE_UP, g_autotestTapReleaseX, g_autotestTapReleaseY);
    g_autotestTapReleasePending = 0;
    g_autotestTapReleaseWindow = 0;
}

static int vm_autotest_parse_u32(const char *text, u32 *value)
{
    char *end = NULL;
    unsigned long parsed;
    if (text == NULL || *text == 0 || value == NULL)
        return 0;
    parsed = strtoul(text, &end, 10);
    if (end == text || *end != 0)
        return 0;
    *value = (u32)parsed;
    return 1;
}

static int vm_mock_service_parse_host_port(const char *text, char *host, size_t hostCap, u16 *port)
{
    const char *colon = NULL;
    u32 parsedPort = 0;
    size_t hostLen = 0;

    if (text == NULL || *text == 0 || host == NULL || hostCap == 0 || port == NULL)
        return 0;

    colon = strrchr(text, ':');
    if (colon != NULL)
    {
        if (!vm_autotest_parse_u32(colon + 1, &parsedPort) || parsedPort == 0 || parsedPort > 65535u)
            return 0;
        hostLen = (size_t)(colon - text);
        if (hostLen == 0)
        {
            snprintf(host, hostCap, "127.0.0.1");
        }
        else
        {
            if (hostLen >= hostCap)
                hostLen = hostCap - 1;
            memcpy(host, text, hostLen);
            host[hostLen] = 0;
        }
        *port = (u16)parsedPort;
        return 1;
    }

    if (!vm_autotest_parse_u32(text, &parsedPort) || parsedPort == 0 || parsedPort > 65535u)
        return 0;
    snprintf(host, hostCap, "127.0.0.1");
    *port = (u16)parsedPort;
    return 1;
}

static void vm_autotest_add_tap(u32 atMs, int x, int y)
{
    if (g_autotestActionCount >= sizeof(g_autotestActions) / sizeof(g_autotestActions[0]))
        return;
    g_autotestActions[g_autotestActionCount].atMs = atMs;
    g_autotestActions[g_autotestActionCount].type = VM_AUTOTEST_ACTION_TAP;
    g_autotestActions[g_autotestActionCount].a = x;
    g_autotestActions[g_autotestActionCount].b = y;
    g_autotestActions[g_autotestActionCount].fired = 0;
    ++g_autotestActionCount;
}

static void vm_autotest_add_window_tap(u32 atMs, int x, int y)
{
    if (g_autotestActionCount >= sizeof(g_autotestActions) / sizeof(g_autotestActions[0]))
        return;
    g_autotestActions[g_autotestActionCount].atMs = atMs;
    g_autotestActions[g_autotestActionCount].type = VM_AUTOTEST_ACTION_WINDOW_TAP;
    g_autotestActions[g_autotestActionCount].a = x;
    g_autotestActions[g_autotestActionCount].b = y;
    g_autotestActions[g_autotestActionCount].fired = 0;
    ++g_autotestActionCount;
}

static void vm_autotest_add_key(u32 atMs, int keySym)
{
    if (g_autotestActionCount >= sizeof(g_autotestActions) / sizeof(g_autotestActions[0]))
        return;
    g_autotestActions[g_autotestActionCount].atMs = atMs;
    g_autotestActions[g_autotestActionCount].type = VM_AUTOTEST_ACTION_KEY;
    g_autotestActions[g_autotestActionCount].a = keySym;
    g_autotestActions[g_autotestActionCount].b = 0;
    g_autotestActions[g_autotestActionCount].fired = 0;
    ++g_autotestActionCount;
}

static void vm_autotest_add_hold_key(u32 atMs, int keySym, u32 durationMs)
{
    if (g_autotestActionCount >= sizeof(g_autotestActions) / sizeof(g_autotestActions[0]))
        return;
    if (durationMs == 0)
        durationMs = 80;
    g_autotestActions[g_autotestActionCount].atMs = atMs;
    g_autotestActions[g_autotestActionCount].type = VM_AUTOTEST_ACTION_HOLD_KEY;
    g_autotestActions[g_autotestActionCount].a = keySym;
    g_autotestActions[g_autotestActionCount].b = (int)durationMs;
    g_autotestActions[g_autotestActionCount].fired = 0;
    ++g_autotestActionCount;
}

static int vm_autotest_key_name_to_sym(const char *keyName, int *keySym)
{
    if (keyName == NULL || keySym == NULL)
        return 0;
    if (strlen(keyName) == 1)
    {
        *keySym = (int)keyName[0];
        return 1;
    }
    if (strcmp(keyName, "enter") == 0)
    {
        *keySym = SDLK_RETURN;
        return 1;
    }
    if (strcmp(keyName, "esc") == 0)
    {
        *keySym = SDLK_ESCAPE;
        return 1;
    }
    return 0;
}

static void vm_autotest_parse_actions(const char *script)
{
    char buffer[2048];
    char *token;
    if (script == NULL || *script == 0)
        return;
    snprintf(buffer, sizeof(buffer), "%s", script);
    token = strtok(buffer, ",;");
    while (token)
    {
        u32 atMs = 0;
        char type[16] = {0};
        int a = 0;
        int b = 0;
        u32 durationMs = 0;
        char keyName[32] = {0};
        int keySym = 0;
        if (sscanf(token, "%u:%15[^:]:%d:%d", &atMs, type, &a, &b) == 4 &&
            strcmp(type, "tap") == 0)
        {
            vm_autotest_add_tap(atMs, a, b);
        }
        else if (sscanf(token, "%u:%15[^:]:%d:%d", &atMs, type, &a, &b) == 4 &&
                 strcmp(type, "windowtap") == 0)
        {
            vm_autotest_add_window_tap(atMs, a, b);
        }
        else if (sscanf(token, "%u:%15[^:]:%31[^:]:%u", &atMs, type, keyName, &durationMs) == 4 &&
                 strcmp(type, "hold") == 0 &&
                 vm_autotest_key_name_to_sym(keyName, &keySym))
        {
            vm_autotest_add_hold_key(atMs, keySym, durationMs);
        }
        else if (sscanf(token, "%u:%15[^:]:%31s", &atMs, type, keyName) == 3 &&
                 strcmp(type, "key") == 0)
        {
            if (vm_autotest_key_name_to_sym(keyName, &keySym))
                vm_autotest_add_key(atMs, keySym);
        }
        token = strtok(NULL, ",;");
    }
}

static void vm_autotest_init(int argc, char *args[])
{
    const char *envAuto = getenv("CBE_AUTOTEST");
    const char *envShotMs = getenv("CBE_AUTOTEST_SHOT_MS");
    const char *envMaxMs = getenv("CBE_AUTOTEST_MAX_MS");
    const char *envActions = getenv("CBE_AUTOTEST_ACTIONS");
    if (envAuto && strcmp(envAuto, "0") != 0)
        g_autotestEnabled = 1;
    if (envShotMs)
        vm_autotest_parse_u32(envShotMs, &g_autotestShotIntervalMs);
    if (envMaxMs)
        vm_autotest_parse_u32(envMaxMs, &g_autotestMaxMs);
    if (envActions)
        vm_autotest_parse_actions(envActions);

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(args[i], "--autotest") == 0)
            g_autotestEnabled = 1;
        else if (strncmp(args[i], "--shot-ms=", 10) == 0)
            vm_autotest_parse_u32(args[i] + 10, &g_autotestShotIntervalMs);
        else if (strncmp(args[i], "--max-ms=", 9) == 0)
            vm_autotest_parse_u32(args[i] + 9, &g_autotestMaxMs);
        else if (strncmp(args[i], "--actions=", 10) == 0)
            vm_autotest_parse_actions(args[i] + 10);
    }

    if (g_autotestShotIntervalMs < 100)
        g_autotestShotIntervalMs = 100;
    if (g_autotestEnabled)
    {
#ifdef _WIN32
        _mkdir("autotest");
        _mkdir("autotest\\screens");
#else
        mkdir("autotest", 0755);
        mkdir("autotest/screens", 0755);
#endif
        g_autotestStateFile = fopen("autotest/state.txt", "w");
        printf("[info][autotest] enabled shot_ms=%u max_ms=%u actions=%u\n",
               g_autotestShotIntervalMs, g_autotestMaxMs, g_autotestActionCount);
    }
}

static void vm_autotest_note(const char *fmt, ...)
{
    va_list args;
    if (!g_autotestEnabled || g_autotestStateFile == NULL)
        return;
    va_start(args, fmt);
    vfprintf(g_autotestStateFile, fmt, args);
    va_end(args);
    fflush(g_autotestStateFile);
}

static void vm_mock_service_init_config(int argc, char *args[])
{
#ifdef CBE_CLIENT_ONLY
    const char *envEndpoint = getenv("CBE_SERVER_ENDPOINT");
    char parsedHost[64];
    u16 parsedPort = 0;
    (void)argc;
    (void)args;

    if (g_mockServiceClientId == 0)
    {
        g_mockServiceClientId = (u32)time(NULL) ^
                                (u32)(uintptr_t)&g_mockServiceClientId ^
                                ((u32)getpid() * 0x9e3779b9u);
        if (g_mockServiceClientId == 0)
            g_mockServiceClientId = 1;
    }
    if (envEndpoint != NULL && envEndpoint[0] != 0)
    {
        if (vm_mock_service_parse_host_port(envEndpoint, parsedHost,
                                            sizeof(parsedHost), &parsedPort))
        {
            snprintf(g_mockServiceHost, sizeof(g_mockServiceHost), "%s", parsedHost);
            g_mockServicePort = parsedPort;
        }
        else
        {
            printf("[warn][network] invalid CBE_SERVER_ENDPOINT=%s\n", envEndpoint);
        }
    }
    g_mockServiceOnly = 0;
    printf("[info][network] mode=android-client server=%s:%u\n",
           g_mockServiceHost, g_mockServicePort);
#else
    const char *envOnly = getenv("CBE_MOCK_SERVICE_ONLY");
    const char *envEndpoint = getenv("CBE_MOCK_SERVICE");
    const char *envBind = getenv("CBE_MOCK_SERVICE_BIND");
    const char *envLegacyPort = getenv("CBE_MOCK_SERVICE_PORT");
    const char *envLegacyRemote = getenv("CBE_MOCK_SERVICE_REMOTE");
    const char *envAdminBind = getenv("CBE_MOCK_ADMIN_BIND");
    const char *envAdminPort = getenv("CBE_MOCK_ADMIN_PORT");
    char parsedHost[64];
    u16 parsedPort = 0;

    if (envOnly && strcmp(envOnly, "0") != 0)
        g_mockServiceOnly = 1;

    if (envBind && envBind[0] != 0)
    {
        snprintf(g_mockServiceBindHost, sizeof(g_mockServiceBindHost), "%s", envBind);
    }
    if (envAdminBind && envAdminBind[0] != 0)
    {
        snprintf(g_mockAdminBindHost, sizeof(g_mockAdminBindHost), "%s", envAdminBind);
    }

    if (g_mockServiceClientId == 0)
    {
        /* Two emulator processes commonly start within the same second and
         * are loaded at the same image address. Include the process id so
         * simultaneous role logins cannot collapse into one service session.
         */
        g_mockServiceClientId = (u32)time(NULL) ^
                                (u32)(uintptr_t)&g_mockServiceClientId ^
                                ((u32)getpid() * 0x9e3779b9u);
        if (g_mockServiceClientId == 0)
            g_mockServiceClientId = 1;
    }

    if (envEndpoint)
    {
        if (vm_mock_service_parse_host_port(envEndpoint, parsedHost, sizeof(parsedHost), &parsedPort))
        {
            snprintf(g_mockServiceHost, sizeof(g_mockServiceHost), "%s", parsedHost);
            g_mockServicePort = parsedPort;
        }
        else
        {
            printf("[warn][mock-service] invalid CBE_MOCK_SERVICE=%s\n", envEndpoint);
        }
    }
    else if (envLegacyRemote)
    {
        if (vm_mock_service_parse_host_port(envLegacyRemote, parsedHost, sizeof(parsedHost), &parsedPort))
        {
            snprintf(g_mockServiceHost, sizeof(g_mockServiceHost), "%s", parsedHost);
            g_mockServicePort = parsedPort;
        }
        else
        {
            printf("[warn][mock-service] invalid CBE_MOCK_SERVICE_REMOTE=%s\n", envLegacyRemote);
        }
    }
    else if (envLegacyPort)
    {
        u32 portValue = 0;
        if (vm_autotest_parse_u32(envLegacyPort, &portValue) && portValue > 0 && portValue <= 65535u)
            g_mockServicePort = (u16)portValue;
        else
        {
            printf("[warn][mock-service] invalid CBE_MOCK_SERVICE_PORT=%s\n", envLegacyPort);
        }
    }

    if (envAdminPort)
    {
        u32 portValue = 0;
        if (vm_autotest_parse_u32(envAdminPort, &portValue) && portValue > 0 && portValue <= 65535u)
            g_mockAdminPort = (u16)portValue;
        else
            printf("[warn][mock-service] invalid CBE_MOCK_ADMIN_PORT=%s\n", envAdminPort);
    }

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(args[i], "--mock-service-only") == 0)
        {
            g_mockServiceOnly = 1;
        }
        else if (strncmp(args[i], "--mock-service-port=", 20) == 0)
        {
            u32 portValue = 0;
            if (vm_autotest_parse_u32(args[i] + 20, &portValue) && portValue > 0 && portValue <= 65535u)
                g_mockServicePort = (u16)portValue;
            else
                printf("[warn][mock-service] invalid mock-service-port=%s\n", args[i] + 20);
        }
        else if (strncmp(args[i], "--mock-admin-port=", 18) == 0)
        {
            u32 portValue = 0;
            if (vm_autotest_parse_u32(args[i] + 18, &portValue) && portValue > 0 && portValue <= 65535u)
                g_mockAdminPort = (u16)portValue;
            else
                printf("[warn][mock-service] invalid mock-admin-port=%s\n", args[i] + 18);
        }
        else if (strncmp(args[i], "--mock-admin-bind=", 18) == 0)
        {
            if (args[i][18] != 0)
                snprintf(g_mockAdminBindHost, sizeof(g_mockAdminBindHost), "%s", args[i] + 18);
            else
                printf("[warn][mock-service] invalid mock-admin-bind=<empty>\n");
        }
        else if (strncmp(args[i], "--mock-service=", 15) == 0)
        {
            if (vm_mock_service_parse_host_port(args[i] + 15, parsedHost, sizeof(parsedHost), &parsedPort))
            {
                snprintf(g_mockServiceHost, sizeof(g_mockServiceHost), "%s", parsedHost);
                g_mockServicePort = parsedPort;
            }
            else
            {
                printf("[warn][mock-service] invalid mock-service=%s\n", args[i] + 15);
            }
        }
        else if (strncmp(args[i], "--mock-service-bind=", 20) == 0)
        {
            if (args[i][20] != 0)
                snprintf(g_mockServiceBindHost, sizeof(g_mockServiceBindHost), "%s", args[i] + 20);
            else
                printf("[warn][mock-service] invalid mock-service-bind=<empty>\n");
        }
        else if (strncmp(args[i], "--mock-service-remote=", 22) == 0)
        {
            if (vm_mock_service_parse_host_port(args[i] + 22, parsedHost, sizeof(parsedHost), &parsedPort))
            {
                snprintf(g_mockServiceHost, sizeof(g_mockServiceHost), "%s", parsedHost);
                g_mockServicePort = parsedPort;
            }
            else
            {
                printf("[warn][mock-service] invalid mock-service-remote=%s\n", args[i] + 22);
            }
        }
    }

    if (g_mockServiceOnly)
    {
        printf("[info][mock-service] mode=server-only bind=%s:%u admin=%s:%u client-default=%s:%u\n",
               g_mockServiceBindHost,
               g_mockServicePort,
               g_mockAdminBindHost,
               g_mockAdminPort,
               g_mockServiceHost,
               g_mockServicePort);
    }
    else
    {
        printf("[info][mock-service] mode=emulator client=%s:%u auth=packet-driven required=service\n",
               g_mockServiceHost, g_mockServicePort);
    }
#endif
}

static void vm_autotest_format_mem_hex(u32 addr, u32 len, char *out, size_t outCap)
{
    static const char hex[] = "0123456789ABCDEF";
    u8 bytes[16];
    u32 count = len;
    size_t pos = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (count > sizeof(bytes))
        count = sizeof(bytes);
    if (addr == 0 || uc_mem_read(MTK, addr, bytes, count) != UC_ERR_OK)
    {
        snprintf(out, outCap, "?");
        return;
    }
    for (u32 i = 0; i < count && pos + 3 < outCap; ++i)
    {
        if (i != 0)
            out[pos++] = '-';
        out[pos++] = hex[bytes[i] >> 4];
        out[pos++] = hex[bytes[i] & 0x0F];
    }
    out[pos] = 0;
}

static void vm_autotest_read_ascii_preview(u32 addr, char *out, size_t outCap)
{
    size_t pos = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (addr == 0)
        return;
    while (pos + 1 < outCap)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, addr + (u32)pos, &ch, 1) != UC_ERR_OK || ch == 0)
            break;
        out[pos++] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.';
    }
    out[pos] = 0;
}

static void vm_autotest_note_format_preview(const char *source, u32 callerPc,
                                            u32 dstPtr, const char *fmt,
                                            u32 arg0, u32 arg1)
{
    char outText[64];
    char outHex[64];

    if (!g_autotestEnabled || fmt == NULL)
        return;
    if (strstr(fmt, "%d/%d") == NULL &&
        (callerPc < 0x01022000 || callerPc > 0x01023000))
    {
        return;
    }

    outText[0] = 0;
    outHex[0] = 0;
    vm_autotest_read_ascii_preview(dstPtr, outText, sizeof(outText));
    vm_autotest_format_mem_hex(dstPtr, 16, outHex, sizeof(outHex));
    vm_autotest_note("format_preview source=%s caller=%08x dst=%08x fmt=%s arg0=%d arg1=%d out=%s out_hex=%s\n",
                     source ? source : "?", callerPc, dstPtr, fmt, (int)arg0,
                     (int)arg1, outText, outHex);
}

static void vm_autotest_note_role_attr_page_pc(u32 pc)
{
    static u32 seen = 0;
    u32 actor = 0;
    u32 sceneObj = 0;
    u32 labelTable = 0;
    u32 valueBase = 0;
    u16 rowStride = 0;
    u8 visibleRows = 0;
    u32 scrollStart = 0;
    u32 actorLevel = 0;
    u32 actorExp = 0;
    u32 actorLastExp = 0;
    u32 actorNextExp = 0;
    u32 sceneLastExp = 0;
    u32 sceneCurExp = 0;
    u16 sceneNextExp = 0;
    char actorNameHex[64];

    if (!g_autotestEnabled || Global_R9 == 0 || pc != 0x010227C0)
        return;
    if (seen >= 6)
        return;
    ++seen;

    actorNameHex[0] = 0;
    (void)uc_mem_read(MTK, Global_R9 + 0x5CA4, &actor, sizeof(actor));
    (void)uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, sizeof(sceneObj));
    (void)uc_mem_read(MTK, Global_R9 + 0x635C, &labelTable, sizeof(labelTable));
    (void)uc_mem_read(MTK, Global_R9 + 0x6390, &valueBase, sizeof(valueBase));
    (void)uc_mem_read(MTK, Global_R9 + 0x631E, &rowStride, sizeof(rowStride));
    (void)uc_mem_read(MTK, Global_R9 + 0x62F6, &visibleRows, sizeof(visibleRows));
    (void)uc_mem_read(MTK, Global_R9 + 0x63A4, &scrollStart, sizeof(scrollStart));
    if (rowStride < 4 || rowStride > 128)
        rowStride = 20;

    if (actor != 0)
    {
        (void)uc_mem_read(MTK, actor + 172, &actorLevel, sizeof(actorLevel));
        (void)uc_mem_read(MTK, actor + 176, &actorExp, sizeof(actorExp));
        (void)uc_mem_read(MTK, actor + 180, &actorLastExp, sizeof(actorLastExp));
        (void)uc_mem_read(MTK, actor + 184, &actorNextExp, sizeof(actorNextExp));
        vm_autotest_format_mem_hex(actor + 68, 16, actorNameHex, sizeof(actorNameHex));
    }
    if (sceneObj != 0)
    {
        (void)uc_mem_read(MTK, sceneObj + 0x4E4, &sceneLastExp, sizeof(sceneLastExp));
        (void)uc_mem_read(MTK, sceneObj + 0x4E8, &sceneCurExp, sizeof(sceneCurExp));
        (void)uc_mem_read(MTK, sceneObj + 0x4FC, &sceneNextExp, sizeof(sceneNextExp));
    }

    vm_autotest_note("role_attr_page pc=%08x actor=%08x name_hex=%s level=%u exp=%u actor_last=%u actor_next=%u scene_last=%u scene_cur=%u scene_next=%u labels=%08x values=%08x stride=%u visible=%u scroll=%u count=%u\n",
                     pc, actor, actorNameHex, actorLevel, actorExp,
                     actorLastExp, actorNextExp, sceneLastExp, sceneCurExp,
                     sceneNextExp, labelTable, valueBase, rowStride,
                     visibleRows, scrollStart, seen);

    for (u32 i = 0; i < 20; ++i)
    {
        u32 labelPtr = 0;
        u32 valuePtr = valueBase + i * rowStride;
        char labelHex[64];
        char valueHex[64];
        char valueText[64];

        labelHex[0] = 0;
        valueHex[0] = 0;
        valueText[0] = 0;
        if (labelTable != 0)
            (void)uc_mem_read(MTK, labelTable + i * 4, &labelPtr, sizeof(labelPtr));
        vm_autotest_format_mem_hex(labelPtr, 16, labelHex, sizeof(labelHex));
        vm_autotest_format_mem_hex(valuePtr, 16, valueHex, sizeof(valueHex));
        vm_autotest_read_ascii_preview(valuePtr, valueText, sizeof(valueText));
        vm_autotest_note("role_attr_row index=%u label_ptr=%08x label_hex=%s value_ptr=%08x value=%s value_hex=%s\n",
                         i, labelPtr, labelHex, valuePtr, valueText, valueHex);
    }
}

static void vm_autotest_note_attr_value_write(const char *source, u32 dst, u32 len)
{
    static u32 seen = 0;
    u32 valueBase = 0;
    u16 rowStride = 0;
    u32 writeEnd = 0;

    if (!g_autotestEnabled || Global_R9 == 0 || dst == 0 || len == 0)
        return;
    if (seen >= 120)
        return;

    (void)uc_mem_read(MTK, Global_R9 + 0x6390, &valueBase, sizeof(valueBase));
    (void)uc_mem_read(MTK, Global_R9 + 0x631E, &rowStride, sizeof(rowStride));
    if (valueBase == 0 || rowStride < 4 || rowStride > 128)
        return;

    writeEnd = dst + len;
    if (writeEnd < dst)
        writeEnd = 0xffffffffu;

    for (u32 row = 0; row < 20; ++row)
    {
        u32 rowPtr = valueBase + row * rowStride;
        u32 rowEnd = rowPtr + rowStride;
        char valueHex[80];
        char valueText[80];

        if (writeEnd <= rowPtr || dst >= rowEnd)
            continue;

        valueHex[0] = 0;
        valueText[0] = 0;
        vm_autotest_format_mem_hex(rowPtr, rowStride < 20 ? rowStride : 20,
                                   valueHex, sizeof(valueHex));
        vm_autotest_read_ascii_preview(rowPtr, valueText, sizeof(valueText));
        ++seen;
        vm_autotest_note("attr_value_write source=%s dst=%08x len=%u row=%u row_ptr=%08x value=%s value_hex=%s count=%u\n",
                         source ? source : "?", dst, len, row, rowPtr,
                         valueText, valueHex, seen);
        if (seen >= 120)
            return;
    }
}

static void vm_autotest_note_startup_pc(u32 pc)
{
    static u32 seenEntry = 0;
    static u32 seenOpenPrep = 0;
    static u32 seenOpenCall = 0;
    static u32 seenOpenResult = 0;
    static u32 seenVersionRequest = 0;
    static u32 seenNetCallback = 0;
    static u32 seenTitleLoginParser = 0;
    static u32 seenTitleLoginDispatch = 0;
    static u32 seenTitleRoleListInit = 0;
    static u32 seenTitleRoleManageInit = 0;
    static u32 seenTitleRoleNetwork = 0;
    u32 startupState = 0;
    u32 netTask = 0;
    u16 waitTicks = 0;
    u32 startupObj = 0;

    if (!g_autotestEnabled || Global_R9 == 0)
        return;
    if (pc != 0x0103A77C && pc != 0x0103A7AC && pc != 0x0103A7BA &&
        pc != 0x0103A7C2 && pc != 0x0103B2D6 && pc != 0x0103B95A &&
        pc != 0x050816DC && pc != 0x05082D80 && pc != 0x05082A50 &&
        pc != 0x05082DBA && pc != 0x05083AD2 && pc != 0x050853EC)
        return;
    uc_mem_read(MTK, Global_R9 + 39224, &startupObj, 4);
    if (startupObj)
    {
        uc_mem_read(MTK, startupObj + 61, &startupState, 1);
        uc_mem_read(MTK, startupObj + 28, &netTask, 4);
        uc_mem_read(MTK, startupObj + 32, &waitTicks, 2);
    }

    if (pc == 0x0103A77C && !seenEntry)
    {
        seenEntry = 1;
        vm_autotest_note("startup_async_entry state=%u net_task=%08x wait=%u\n", startupState, netTask, waitTicks);
    }
    else if (pc == 0x0103A7AC && !seenOpenPrep)
    {
        seenOpenPrep = 1;
        vm_autotest_note("startup_async_open_prep state=%u net_task=%08x wait=%u\n", startupState, netTask, waitTicks);
    }
    else if (pc == 0x0103A7BA && !seenOpenCall)
    {
        seenOpenCall = 1;
        vm_autotest_note("startup_async_open_call state=%u net_task=%08x wait=%u\n", startupState, netTask, waitTicks);
    }
    else if (pc == 0x0103A7C2 && !seenOpenResult)
    {
        seenOpenResult = 1;
        vm_autotest_note("startup_async_open_result state=%u net_task=%08x wait=%u\n", startupState, netTask, waitTicks);
    }
    else if (pc == 0x0103B2D6 && !seenVersionRequest)
    {
        seenVersionRequest = 1;
        vm_autotest_note("send_version_update_request state=%u net_task=%08x wait=%u\n", startupState, netTask, waitTicks);
    }
    else if (pc == 0x0103B95A && !seenNetCallback)
    {
        seenNetCallback = 1;
        vm_autotest_note("startup_net_callback state=%u net_task=%08x wait=%u\n", startupState, netTask, waitTicks);
    }
    else if (pc == 0x050816DC && !seenTitleLoginParser)
    {
        seenTitleLoginParser = 1;
        vm_autotest_note("title_login_response_parser pc=%08x\n", pc);
    }
    else if ((pc == 0x05082D80 || pc == 0x05082A50) && !seenTitleLoginDispatch)
    {
        seenTitleLoginDispatch = 1;
        vm_autotest_note("title_login_dispatch pc=%08x\n", pc);
    }
    else if (pc == 0x05082DBA && !seenTitleRoleListInit)
    {
        seenTitleRoleListInit = 1;
        vm_autotest_note("title_role_list_init pc=%08x\n", pc);
    }
    else if (pc == 0x05083AD2 && !seenTitleRoleManageInit)
    {
        seenTitleRoleManageInit = 1;
        vm_autotest_note("title_role_manage_init pc=%08x\n", pc);
    }
    else if (pc == 0x050853EC && !seenTitleRoleNetwork)
    {
        seenTitleRoleNetwork = 1;
        vm_autotest_note("title_role_manage_network pc=%08x\n", pc);
    }
}

static void vm_autotest_note_scene_actor_parser_pc(u32 pc)
{
    static u32 seenActorMoveCase2 = 0;
    static u32 seenActorMoveUpdate = 0;
    static u32 seenActorOtherCase10 = 0;

    if (!g_autotestEnabled)
        return;
    if (pc == 0x01012B2E && seenActorMoveCase2 < 8)
    {
        ++seenActorMoveCase2;
        vm_autotest_note("scene_actor_parser case2_moveinfo pc=%08x count=%u\n",
                         pc, seenActorMoveCase2);
    }
    else if (pc == 0x01012A76 && seenActorMoveUpdate < 8)
    {
        u32 actorId = 0;
        u32 movePtr = 0;
        u32 moveLen = 0;
        u32 gridX = 0;
        u32 word0 = 0;
        u32 word44 = 0;
        u32 word48 = 0;
        u32 word52 = 0;
        u32 word56 = 0;
        ++seenActorMoveUpdate;
        uc_reg_read(MTK, UC_ARM_REG_R0, &actorId);
        uc_reg_read(MTK, UC_ARM_REG_R1, &movePtr);
        uc_reg_read(MTK, UC_ARM_REG_R2, &moveLen);
        uc_reg_read(MTK, UC_ARM_REG_R3, &gridX);
        if (movePtr != 0)
        {
            (void)uc_mem_read(MTK, movePtr, &word0, sizeof(word0));
            (void)uc_mem_read(MTK, movePtr + 44, &word44, sizeof(word44));
            (void)uc_mem_read(MTK, movePtr + 48, &word48, sizeof(word48));
            (void)uc_mem_read(MTK, movePtr + 52, &word52, sizeof(word52));
            (void)uc_mem_read(MTK, movePtr + 56, &word56, sizeof(word56));
        }
        vm_autotest_note("scene_actor_update_move actor=%u ptr=%08x len=%u gridX=%u raw0=%08x raw44=%u raw48=%u raw52=%u raw56=%u count=%u\n",
                         actorId, movePtr, moveLen, gridX, word0,
                         word44, word48, word52, word56, seenActorMoveUpdate);
    }
    else if (pc == 0x01012DD8 && seenActorOtherCase10 < 8)
    {
        ++seenActorOtherCase10;
        vm_autotest_note("scene_actor_parser case10_otherinfo pc=%08x count=%u\n",
                         pc, seenActorOtherCase10);
    }
}

static void vm_autotest_note_backpack_parser_pc(u32 pc)
{
    static u32 seenMainStatusEntry = 0;
    static u32 seenMainStatusCommit = 0;
    static u32 seenBusinessFollowup = 0;
    static u32 seenBusinessFallback = 0;
    static u32 seenBusinessFallbackCall = 0;
    static u32 seenMainItemAcquire = 0;
    static u32 seenMainItemAcquireDone = 0;
    static u32 seenMainItemOp = 0;
    static u32 seenMainItemOpDone = 0;
    static u32 seenModuleInit = 0;
    static u32 seenModuleInitManagers = 0;
    static u32 seenUiInit = 0;
    static u32 seenUiSyncCall = 0;
    static u32 seenUiRequest = 0;
    static u32 seenEntry = 0;
    static u32 seenCommit = 0;
    static u32 seenGridEntry = 0;
    static u32 seenGridCommit = 0;
    static u32 seenGridInsertEntry = 0;
    static u32 seenGridLoadResult = 0;
    static u32 seenGlobalNetEntry = 0;
    static u32 seenItemDeltaEntry = 0;
    static u32 seenItemDeltaApply = 0;
    static u32 seenBottomInit = 0;
    static u32 seenBottomInitDone = 0;
    static u32 seenBottomRender = 0;
    static u32 seenFullBackpackInit = 0;
    static u32 seenFullBackpackRender = 0;
    static u32 seenCbmRegister = 0;
    static u32 seenItemLookup = 0;
    static u32 seenEquipLookup = 0;
    u32 r9 = 0;

    if (!g_autotestEnabled)
        return;
    if (pc != 0x0102657A && pc != 0x010265E4 &&
        pc != 0x01012F7E && pc != 0x01012F8E && pc != 0x01012FA4 &&
        pc != 0x0101191A && pc != 0x010119DE &&
        pc != 0x01033544 && pc != 0x0103374E &&
        pc != 0x01039952 && pc != 0x01039AF8 &&
        pc != 0x0101918E && pc != 0x010191A2 &&
        pc != 0x0518164A && pc != 0x0518169C &&
        pc != 0x05182434 && pc != 0x0518248E && pc != 0x051824A4 &&
        pc != 0x0518418C && pc != 0x05184538 &&
        pc != 0x05184498 && pc != 0x051844DA &&
        pc != 0x051811CE &&
        pc != 0x05180D04 && pc != 0x05181094 &&
        pc != 0x05185B58 && pc != 0x05185BC6 &&
        pc != 0x05185FBE &&
        pc != 0x051865B6 &&
        pc != 0x05183E44 && pc != 0x0518251A)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0)
        r9 = Global_R9;

#define READ_MAIN_BACKPACK_STATE(manager_, count_, cap_, list_, item0_, seq0_, stack242_, stack272_) \
    do                                                                                              \
    {                                                                                               \
        if ((manager_) != 0)                                                                        \
        {                                                                                           \
            uc_mem_read(MTK, (manager_) + 36, &(count_), sizeof(count_));                           \
            uc_mem_read(MTK, (manager_) + 40, &(cap_), sizeof(cap_));                               \
            uc_mem_read(MTK, (manager_) + 32, &(list_), sizeof(list_));                              \
            if ((list_) != 0 && (count_) > 0)                                                       \
            {                                                                                       \
                uc_mem_read(MTK, (list_), &(item0_), sizeof(item0_));                               \
                uc_mem_read(MTK, (list_) + 276, &(seq0_), sizeof(seq0_));                           \
                uc_mem_read(MTK, (list_) + 242, &(stack242_), sizeof(stack242_));                   \
                uc_mem_read(MTK, (list_) + 272, &(stack272_), sizeof(stack272_));                   \
            }                                                                                       \
        }                                                                                           \
    } while (0)

    if (pc == 0x01012F7E && seenBusinessFollowup < 16)
    {
        u32 result = 0;
        u32 entryCount = 0;
        u32 fallback = 0;
        ++seenBusinessFollowup;
        uc_reg_read(MTK, UC_ARM_REG_R0, &result);
        if (Global_R9 != 0)
        {
            uc_mem_read(MTK, Global_R9 + 21904, &entryCount, sizeof(entryCount));
            uc_mem_read(MTK, Global_R9 + 23856, &fallback, sizeof(fallback));
        }
        vm_autotest_note("backpack_business_followup pc=%08x result=%u entries=%u fallback=%08x seen=%u\n",
                         pc, result, entryCount, fallback, seenBusinessFollowup);
    }
    else if (pc == 0x01012F8E && seenBusinessFallback < 16)
    {
        u32 fallback = 0;
        u32 r0 = 0;
        u32 r1 = 0;
        u32 r2 = 0;
        u32 r3 = 0;
        ++seenBusinessFallback;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        if (Global_R9 != 0)
            uc_mem_read(MTK, Global_R9 + 23856, &fallback, sizeof(fallback));
        vm_autotest_note("backpack_business_fallback pc=%08x fallback=%08x r0=%08x r1=%08x r2=%08x event=%u seen=%u\n",
                         pc, fallback, r0, r1, r2, r3, seenBusinessFallback);
    }
    else if (pc == 0x01012FA4 && seenBusinessFallbackCall < 16)
    {
        u32 fallback = 0;
        u32 r0 = 0;
        u32 r1 = 0;
        u32 r2 = 0;
        u32 r3 = 0;
        ++seenBusinessFallbackCall;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        if (Global_R9 != 0)
            uc_mem_read(MTK, Global_R9 + 23856, &fallback, sizeof(fallback));
        vm_autotest_note("backpack_business_fallback_call pc=%08x fallback=%08x r0=%08x r1=%08x r2=%08x event=%u seen=%u\n",
                         pc, fallback, r0, r1, r2, r3, seenBusinessFallbackCall);
    }
    else if (pc == 0x051865B6 && seenCbmRegister < 8)
    {
        u32 r0 = 0;
        u32 r1 = 0;
        u32 r2 = 0;
        u32 r3 = 0;
        u32 r5 = 0;
        u32 sp = 0;
        u32 arg4 = 0;
        u32 arg5 = 0;
        u32 apiTable = 0;
        ++seenCbmRegister;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp != 0)
        {
            uc_mem_read(MTK, sp, &arg4, sizeof(arg4));
            uc_mem_read(MTK, sp + 4, &arg5, sizeof(arg5));
        }
        if (r9 != 0)
            uc_mem_read(MTK, r9 + 8276, &apiTable, sizeof(apiTable));
        vm_autotest_note("backpack_cbm_register pc=%08x r9=%08x table=%08x init=%08x event=%08x logic=%08x render=%08x net=%08x target=%08x api=%08x count=%u\n",
                         pc, r9, r0, r1, r2, r3, arg4, arg5, r5, apiTable, seenCbmRegister);
    }
    else if (pc == 0x0102657A && seenMainStatusEntry < 8)
    {
        u32 object = 0;
        u32 kind = 0;
        u32 subtype = 0;
        ++seenMainStatusEntry;
        uc_reg_read(MTK, UC_ARM_REG_R0, &object);
        if (object != 0)
        {
            uc_mem_read(MTK, object + 4, &kind, sizeof(kind));
            uc_mem_read(MTK, object + 8, &subtype, sizeof(subtype));
        }
        vm_autotest_note("backpack_status25 entry pc=%08x object=%08x kind=%u subtype=%u r9=%08x count=%u\n",
                         pc, object, kind, subtype, r9, seenMainStatusEntry);
    }
    else if (pc == 0x010265E4 && seenMainStatusCommit < 8)
    {
        u32 business = 0;
        u16 maxnum = 0;
        u8 itemCount = 0;
        u32 itemId = 0;
        u8 stack = 0;
        ++seenMainStatusCommit;
        if (Global_R9 != 0)
        {
            uc_mem_read(MTK, Global_R9 + 21676, &business, sizeof(business));
            if (business != 0)
            {
                uc_mem_read(MTK, business + 546, &maxnum, sizeof(maxnum));
                uc_mem_read(MTK, business + 548, &itemCount, sizeof(itemCount));
                uc_mem_read(MTK, business + 549, &itemId, sizeof(itemId));
                uc_mem_read(MTK, business + 553, &stack, sizeof(stack));
            }
        }
        vm_autotest_note("backpack_status25 commit pc=%08x business=%08x maxnum=%u item_count=%u item0=%u stack=%u count=%u\n",
                         pc, business, maxnum, itemCount, itemId, stack, seenMainStatusCommit);
    }
    else if (pc == 0x0101191A && seenMainItemAcquire < 8)
    {
        u32 object = 0;
        u32 kind = 0;
        u32 subtype = 0;
        u32 manager = Global_R9 + 24640;
        u16 itemCount = 0;
        u16 itemCap = 0;
        u32 itemList = 0;
        u32 item0 = 0;
        u16 seq0 = 0;
        u16 stack242 = 0;
        u16 stack272 = 0;
        ++seenMainItemAcquire;
        uc_reg_read(MTK, UC_ARM_REG_R0, &object);
        if (object != 0)
        {
            uc_mem_read(MTK, object + 4, &kind, sizeof(kind));
            uc_mem_read(MTK, object + 8, &subtype, sizeof(subtype));
        }
        if (Global_R9 != 0)
            READ_MAIN_BACKPACK_STATE(manager, itemCount, itemCap, itemList, item0, seq0, stack242, stack272);
        vm_autotest_note("backpack_main_item_acquire entry pc=%08x object=%08x kind=%u subtype=%u manager=%08x count_before=%u cap=%u list=%08x item0=%u seq0=%u stack242=%u stack272=%u seen=%u\n",
                         pc, object, kind, subtype, manager, itemCount, itemCap,
                         itemList, item0, seq0, stack242, stack272, seenMainItemAcquire);
    }
    else if (pc == 0x010119DE && seenMainItemAcquireDone < 8)
    {
        u32 manager = Global_R9 + 24640;
        u32 gameItemManager = 0;
        u32 gameItemList = 0;
        u32 gameItemCount = 0;
        u16 itemCount = 0;
        u16 itemCap = 0;
        u32 itemList = 0;
        u32 item0 = 0;
        u16 seq0 = 0;
        u16 stack242 = 0;
        u16 stack272 = 0;
        ++seenMainItemAcquireDone;
        if (Global_R9 != 0)
        {
            READ_MAIN_BACKPACK_STATE(manager, itemCount, itemCap, itemList, item0, seq0, stack242, stack272);
            uc_mem_read(MTK, Global_R9 + 10324, &gameItemManager, sizeof(gameItemManager));
            if (gameItemManager != 0)
            {
                uc_mem_read(MTK, gameItemManager + 32, &gameItemList, sizeof(gameItemList));
                uc_mem_read(MTK, gameItemManager + 36, &gameItemCount, sizeof(gameItemCount));
            }
        }
        vm_autotest_note("backpack_main_item_acquire done pc=%08x manager=%08x count_after=%u cap=%u list=%08x item0=%u seq0=%u stack242=%u stack272=%u game_mgr=%08x game_list=%08x game_count=%u seen=%u\n",
                         pc, manager, itemCount, itemCap, itemList, item0, seq0,
                         stack242, stack272, gameItemManager, gameItemList,
                         gameItemCount, seenMainItemAcquireDone);
    }
    else if (pc == 0x01033544 && seenMainItemOp < 12)
    {
        u32 manager = 0;
        u32 object = 0;
        u32 kind = 0;
        u32 subtype = 0;
        u16 itemCount = 0;
        u16 itemCap = 0;
        u32 itemList = 0;
        u32 item0 = 0;
        u16 seq0 = 0;
        u16 stack242 = 0;
        u16 stack272 = 0;
        ++seenMainItemOp;
        uc_reg_read(MTK, UC_ARM_REG_R0, &manager);
        uc_reg_read(MTK, UC_ARM_REG_R1, &object);
        if (object != 0)
        {
            uc_mem_read(MTK, object + 4, &kind, sizeof(kind));
            uc_mem_read(MTK, object + 8, &subtype, sizeof(subtype));
        }
        if (manager != 0)
            READ_MAIN_BACKPACK_STATE(manager, itemCount, itemCap, itemList, item0, seq0, stack242, stack272);
        vm_autotest_note("backpack_main_item_op entry pc=%08x manager=%08x object=%08x kind=%u subtype=%u count=%u cap=%u list=%08x item0=%u seq0=%u stack242=%u stack272=%u seen=%u\n",
                         pc, manager, object, kind, subtype, itemCount, itemCap,
                         itemList, item0, seq0, stack242, stack272, seenMainItemOp);
    }
    else if (pc == 0x0103374E && seenMainItemOpDone < 8)
    {
        u32 manager = Global_R9 + 24640;
        u16 itemCount = 0;
        u16 itemCap = 0;
        u32 itemList = 0;
        u32 item0 = 0;
        u16 seq0 = 0;
        u16 stack242 = 0;
        u16 stack272 = 0;
        ++seenMainItemOpDone;
        if (Global_R9 != 0)
            READ_MAIN_BACKPACK_STATE(manager, itemCount, itemCap, itemList, item0, seq0, stack242, stack272);
        vm_autotest_note("backpack_main_item_op count_done pc=%08x manager=%08x count=%u cap=%u list=%08x item0=%u seq0=%u stack242=%u stack272=%u seen=%u\n",
                         pc, manager, itemCount, itemCap, itemList, item0,
                         seq0, stack242, stack272, seenMainItemOpDone);
    }
    else if (pc == 0x0518164A && seenModuleInit < 4)
    {
        ++seenModuleInit;
        vm_autotest_note("backpack_module_init entry pc=%08x r9=%08x seen=%u\n",
                         pc, r9, seenModuleInit);
    }
    else if (pc == 0x0518169C && seenModuleInitManagers < 4)
    {
        u32 itemManager = 0;
        u32 bottomManager = 0;
        ++seenModuleInitManagers;
        if (r9 != 0)
        {
            uc_mem_read(MTK, r9 + 10324, &itemManager, sizeof(itemManager));
            uc_mem_read(MTK, r9 + 10344, &bottomManager, sizeof(bottomManager));
        }
        vm_autotest_note("backpack_module_init managers pc=%08x r9=%08x item_mgr=%08x bottom_mgr=%08x seen=%u\n",
                         pc, r9, itemManager, bottomManager, seenModuleInitManagers);
    }
    else if (pc == 0x05182434 && seenUiInit < 8)
    {
        ++seenUiInit;
        vm_autotest_note("backpack_ui init pc=%08x r9=%08x count=%u\n", pc, r9, seenUiInit);
    }
    else if (pc == 0x0518248E && seenUiSyncCall < 8)
    {
        u32 syncFn = 0;
        u32 itemBase = 0;
        u32 itemCount = 0;
        u32 mainManager = Global_R9 + 24640;
        u16 mainCount = 0;
        u16 mainCap = 0;
        ++seenUiSyncCall;
        if (r9 != 0)
        {
            uc_mem_read(MTK, r9 + 11756, &syncFn, sizeof(syncFn));
            uc_mem_read(MTK, r9 + 10680, &itemBase, sizeof(itemBase));
            uc_mem_read(MTK, r9 + 10708, &itemCount, sizeof(itemCount));
        }
        if (Global_R9 != 0)
        {
            uc_mem_read(MTK, mainManager + 36, &mainCount, sizeof(mainCount));
            uc_mem_read(MTK, mainManager + 40, &mainCap, sizeof(mainCap));
        }
        vm_autotest_note("backpack_ui sync_call pc=%08x r9=%08x sync_fn=%08x local_count=%u local_base=%08x main_count=%u main_cap=%u seen=%u\n",
                         pc, r9, syncFn, itemCount, itemBase, mainCount, mainCap, seenUiSyncCall);
    }
    else if (pc == 0x051824A4 && seenUiRequest < 8)
    {
        u32 r0 = 0;
        u32 r1 = 0;
        u32 r2 = 0;
        u32 r3 = 0;
        ++seenUiRequest;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        vm_autotest_note("backpack_ui request_call pc=%08x r0=%u r1=%u r2=%u r3=%u r9=%08x count=%u\n",
                         pc, r0, r1, r2, r3, r9, seenUiRequest);
    }
    else if (pc == 0x0518418C && seenEntry < 8)
    {
        u32 object = 0;
        u32 kind = 0;
        u32 subtype = 0;
        ++seenEntry;
        uc_reg_read(MTK, UC_ARM_REG_R0, &object);
        if (object != 0)
        {
            uc_mem_read(MTK, object + 4, &kind, sizeof(kind));
            uc_mem_read(MTK, object + 8, &subtype, sizeof(subtype));
        }
        vm_autotest_note("backpack_parser entry pc=%08x object=%08x kind=%u subtype=%u r9=%08x count=%u\n",
                         pc, object, kind, subtype, r9, seenEntry);
    }
    else if ((pc == 0x05184498 || pc == 0x051844DA) &&
             (pc == 0x05184498 ? seenItemLookup < 16 : seenEquipLookup < 16))
    {
        u32 result = 0;
        u32 rowIndex = 0;
        u32 rowOffset = 0;
        u32 state = 0;
        u32 itemBase = 0;
        u32 itemId = 0;
        u32 seen = 0;
        if (pc == 0x05184498)
            seen = ++seenItemLookup;
        else
            seen = ++seenEquipLookup;
        uc_reg_read(MTK, UC_ARM_REG_R0, &result);
        uc_reg_read(MTK, UC_ARM_REG_R4, &rowIndex);
        uc_reg_read(MTK, UC_ARM_REG_R5, &rowOffset);
        uc_reg_read(MTK, UC_ARM_REG_R6, &state);
        if (state != 0)
            uc_mem_read(MTK, state + 0x40, &itemBase, sizeof(itemBase));
        if (itemBase != 0)
            uc_mem_read(MTK, itemBase + rowOffset, &itemId, sizeof(itemId));
        vm_autotest_note("backpack_parser dsh_lookup pc=%08x table=%s result=%u row=%u item_base=%08x item_id=%u count=%u\n",
                         pc, pc == 0x05184498 ? "item" : "equip",
                         result, rowIndex, itemBase, itemId, seen);
    }
    else if (pc == 0x05184538 && seenCommit < 8)
    {
        u32 itemCount = 0;
        u32 itemBase = 0;
        u32 itemId = 0;
        u32 price = 0;
        u16 stack242 = 0;
        u8 rowFlag278 = 0;
        u8 extra286 = 0;
        u8 extra287 = 0;
        u16 attr290 = 0;
        char nameHex[64];
        ++seenCommit;
        nameHex[0] = 0;
        if (r9 != 0)
        {
            uc_mem_read(MTK, r9 + 10708, &itemCount, sizeof(itemCount));
            uc_mem_read(MTK, r9 + 10680, &itemBase, sizeof(itemBase));
            if (itemBase != 0 && itemCount > 0)
            {
                uc_mem_read(MTK, itemBase, &itemId, sizeof(itemId));
                vm_autotest_format_mem_hex(itemBase + 4, 12, nameHex, sizeof(nameHex));
                uc_mem_read(MTK, itemBase + 20, &price, sizeof(price));
                uc_mem_read(MTK, itemBase + 242, &stack242, sizeof(stack242));
                uc_mem_read(MTK, itemBase + 278, &rowFlag278, sizeof(rowFlag278));
                uc_mem_read(MTK, itemBase + 286, &extra286, sizeof(extra286));
                uc_mem_read(MTK, itemBase + 287, &extra287, sizeof(extra287));
                uc_mem_read(MTK, itemBase + 290, &attr290, sizeof(attr290));
            }
        }
        vm_autotest_note("backpack_parser commit pc=%08x r9=%08x item_count=%u item_base=%08x item0=%u name_hex=%s price=%u stack242=%u flag278=%u extra286=%u extra287=%u attr290=%u count=%u\n",
                         pc, r9, itemCount, itemBase, itemId, nameHex, price,
                         stack242, rowFlag278, extra286, extra287, attr290,
                         seenCommit);
    }
    else if (pc == 0x01039952 && seenGridEntry < 8)
    {
        u32 object = 0;
        u32 kind = 0;
        u32 subtype = 0;
        ++seenGridEntry;
        uc_reg_read(MTK, UC_ARM_REG_R0, &object);
        if (object != 0)
        {
            uc_mem_read(MTK, object + 4, &kind, sizeof(kind));
            uc_mem_read(MTK, object + 8, &subtype, sizeof(subtype));
        }
        vm_autotest_note("backpack_grid entry pc=%08x object=%08x kind=%u subtype=%u r9=%08x count=%u\n",
                         pc, object, kind, subtype, r9, seenGridEntry);
    }
    else if (pc == 0x01039AF8 && seenGridCommit < 8)
    {
        u8 busy = 0xff;
        u32 busyPtr = 0;
        u32 manager = Global_R9 + 24640;
        u16 itemCount = 0;
        u16 itemCap = 0;
        u32 itemList = 0;
        u32 item0 = 0;
        u16 seq0 = 0;
        u16 stack242 = 0;
        u16 stack272 = 0;
        ++seenGridCommit;
        if (Global_R9 != 0)
        {
            busyPtr = Global_R9 + 21808;
            uc_mem_read(MTK, busyPtr, &busy, sizeof(busy));
            READ_MAIN_BACKPACK_STATE(manager, itemCount, itemCap, itemList, item0, seq0, stack242, stack272);
        }
        vm_autotest_note("backpack_grid commit pc=%08x r9=%08x busy_ptr=%08x busy=%u manager=%08x item_count=%u cap=%u list=%08x item0=%u seq0=%u stack242=%u stack272=%u count=%u\n",
                         pc, r9, busyPtr, busy, manager, itemCount, itemCap,
                         itemList, item0, seq0, stack242, stack272, seenGridCommit);
    }
    else if (pc == 0x0101918E && seenGridInsertEntry < 8)
    {
        u32 itemStruct = 0;
        u32 itemId = 0;
        u32 count = 0;
        u32 seq = 0;
        u32 stackPtr = 0;
        u32 item0 = 0;
        ++seenGridInsertEntry;
        uc_reg_read(MTK, UC_ARM_REG_R0, &itemStruct);
        uc_reg_read(MTK, UC_ARM_REG_R1, &itemId);
        uc_reg_read(MTK, UC_ARM_REG_R2, &count);
        uc_reg_read(MTK, UC_ARM_REG_R3, &seq);
        uc_reg_read(MTK, UC_ARM_REG_SP, &stackPtr);
        if (itemStruct != 0)
            uc_mem_read(MTK, itemStruct, &item0, sizeof(item0));
        vm_autotest_note("backpack_grid insert_entry pc=%08x item_struct=%08x item_id=%u count=%u seq=%u item0=%u sp=%08x seen=%u\n",
                         pc, itemStruct, itemId, count, seq, item0, stackPtr, seenGridInsertEntry);
    }
    else if (pc == 0x010191A2 && seenGridLoadResult < 8)
    {
        u32 loadResult = 0;
        u32 itemStruct = 0;
        u32 item0 = 0;
        u8 itemType = 0;
        ++seenGridLoadResult;
        uc_reg_read(MTK, UC_ARM_REG_R0, &loadResult);
        uc_reg_read(MTK, UC_ARM_REG_R4, &itemStruct);
        if (itemStruct != 0)
        {
            uc_mem_read(MTK, itemStruct, &item0, sizeof(item0));
            uc_mem_read(MTK, itemStruct + 282, &itemType, sizeof(itemType));
        }
        vm_autotest_note("backpack_grid load_result pc=%08x result=%u item_struct=%08x item0=%u type282=%u seen=%u\n",
                         pc, loadResult, itemStruct, item0, itemType, seenGridLoadResult);
    }
    else if (pc == 0x051811CE && seenGlobalNetEntry < 16)
    {
        u32 r0 = 0;
        u32 r1 = 0;
        u32 r2 = 0;
        u32 r3 = 0;
        ++seenGlobalNetEntry;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        vm_autotest_note("backpack_global_net entry pc=%08x r0=%08x r1=%08x r2=%08x event=%u r9=%08x count=%u\n",
                         pc, r0, r1, r2, r3, r9, seenGlobalNetEntry);
    }
    else if (pc == 0x05180D04 && seenItemDeltaEntry < 8)
    {
        u32 object = 0;
        u32 kind = 0;
        u32 subtype = 0;
        ++seenItemDeltaEntry;
        uc_reg_read(MTK, UC_ARM_REG_R1, &object);
        if (object != 0)
        {
            uc_mem_read(MTK, object + 4, &kind, sizeof(kind));
            uc_mem_read(MTK, object + 8, &subtype, sizeof(subtype));
        }
        vm_autotest_note("backpack_item_delta entry pc=%08x object=%08x kind=%u subtype=%u r9=%08x count=%u\n",
                         pc, object, kind, subtype, r9, seenItemDeltaEntry);
    }
    else if (pc == 0x05181094 && seenItemDeltaApply < 8)
    {
        ++seenItemDeltaApply;
        vm_autotest_note("backpack_item_delta apply pc=%08x r9=%08x count=%u\n",
                         pc, r9, seenItemDeltaApply);
    }
    else if (pc == 0x05185B58 && seenBottomInit < 8)
    {
        u32 manager = 0;
        u32 listHead = 0;
        ++seenBottomInit;
        if (r9 != 0)
        {
            uc_mem_read(MTK, r9 + 10344, &manager, sizeof(manager));
            if (manager != 0)
                uc_mem_read(MTK, manager, &listHead, sizeof(listHead));
        }
        vm_autotest_note("backpack_bottom init pc=%08x r9=%08x manager=%08x list_head=%08x count=%u\n",
                         pc, r9, manager, listHead, seenBottomInit);
    }
    else if (pc == 0x05185BC6 && seenBottomInitDone < 8)
    {
        u16 localCount = 0;
        u32 listHead = 0;
        ++seenBottomInitDone;
        if (r9 != 0)
        {
            uc_mem_read(MTK, r9 + 10624, &localCount, sizeof(localCount));
            uc_mem_read(MTK, r9 + 10724, &listHead, sizeof(listHead));
        }
        vm_autotest_note("backpack_bottom init_done pc=%08x r9=%08x local_count=%u list_head=%08x count=%u\n",
                         pc, r9, localCount, listHead, seenBottomInitDone);
    }
    else if (pc == 0x05185FBE && seenBottomRender < 12)
    {
        u32 statusPtr = 0;
        u32 statusUsed = 0;
        u16 localCount = 0;
        u32 listHead = 0;
        u8 nodeActive = 0;
        u32 nodeCount = 0;
        u32 nodeNext = 0;
        ++seenBottomRender;
        if (r9 != 0)
        {
            uc_mem_read(MTK, r9 + 10328, &statusPtr, sizeof(statusPtr));
            if (statusPtr != 0)
                uc_mem_read(MTK, statusPtr + 184, &statusUsed, sizeof(statusUsed));
            uc_mem_read(MTK, r9 + 10624, &localCount, sizeof(localCount));
            uc_mem_read(MTK, r9 + 10724, &listHead, sizeof(listHead));
            if (listHead != 0)
            {
                uc_mem_read(MTK, listHead + 98, &nodeActive, sizeof(nodeActive));
                uc_mem_read(MTK, listHead + 112, &nodeCount, sizeof(nodeCount));
                uc_mem_read(MTK, listHead + 136, &nodeNext, sizeof(nodeNext));
            }
        }
        vm_autotest_note("backpack_bottom render pc=%08x r9=%08x status_used=%u local_count=%u list_head=%08x active=%u node_count=%u next=%08x count=%u\n",
                         pc, r9, statusUsed, localCount, listHead,
                         nodeActive, nodeCount, nodeNext, seenBottomRender);
    }
    else if ((pc == 0x05183E44 || pc == 0x0518251A) &&
             (pc == 0x05183E44 ? seenFullBackpackInit < 8 : seenFullBackpackRender < 16))
    {
        u8 phase = 0;
        u32 itemCount = 0;
        u32 itemBase = 0;
        u32 itemId = 0;
        u16 stack = 0;
        u16 stackMax = 0;
        u8 extra286 = 0;
        u8 extra287 = 0;
        u32 price = 0;
        u32 gameItemManager = 0;
        u32 gameItemList = 0;
        u32 gameItemCount = 0;
        u32 gameItem0 = 0;
        u16 gameItem0Stack = 0;
        u32 seen = 0;
        if (pc == 0x05183E44)
            seen = ++seenFullBackpackInit;
        else
            seen = ++seenFullBackpackRender;
        if (r9 != 0)
        {
            uc_mem_read(MTK, r9 + 10622, &phase, sizeof(phase));
            uc_mem_read(MTK, r9 + 10708, &itemCount, sizeof(itemCount));
            uc_mem_read(MTK, r9 + 10680, &itemBase, sizeof(itemBase));
            uc_mem_read(MTK, r9 + 10324, &gameItemManager, sizeof(gameItemManager));
            if (gameItemManager != 0)
            {
                uc_mem_read(MTK, gameItemManager + 32, &gameItemList, sizeof(gameItemList));
                uc_mem_read(MTK, gameItemManager + 36, &gameItemCount, sizeof(gameItemCount));
                if (gameItemList != 0 && gameItemCount > 0)
                {
                    uc_mem_read(MTK, gameItemList, &gameItem0, sizeof(gameItem0));
                    uc_mem_read(MTK, gameItemList + 242, &gameItem0Stack, sizeof(gameItem0Stack));
                }
            }
            if (itemBase != 0 && itemCount > 0)
            {
                uc_mem_read(MTK, itemBase, &itemId, sizeof(itemId));
                uc_mem_read(MTK, itemBase + 20, &price, sizeof(price));
                uc_mem_read(MTK, itemBase + 242, &stack, sizeof(stack));
                uc_mem_read(MTK, itemBase + 244, &stackMax, sizeof(stackMax));
                uc_mem_read(MTK, itemBase + 286, &extra286, sizeof(extra286));
                uc_mem_read(MTK, itemBase + 287, &extra287, sizeof(extra287));
            }
        }
        vm_autotest_note("backpack_full %s pc=%08x r9=%08x phase=%u item_count=%u item_base=%08x item0=%u stack=%u stack_max=%u price=%u extra286=%u extra287=%u game_mgr=%08x game_count=%u game_list=%08x game_item0=%u game_stack=%u count=%u\n",
                         pc == 0x05183E44 ? "init_label" : "render_label",
                         pc, r9, phase, itemCount, itemBase, itemId, stack,
                         stackMax, price, extra286, extra287, gameItemManager,
                         gameItemCount, gameItemList, gameItem0, gameItem0Stack, seen);
    }

#undef READ_MAIN_BACKPACK_STATE
}

static void vm_autotest_note_shop_parser_pc(u32 pc)
{
    static u32 seenEntry = 0;
    static u32 seenRows = 0;
    static u32 seenItemId = 0;
    static u32 seenName = 0;
    static u32 seenDone = 0;
    u32 base = g_currentScreenModuleBase;
    u32 off = 0;
    u32 r9 = 0;

    if (!g_autotestEnabled || base == 0 || pc < base)
        return;
    off = pc - base;
    if (off != 0x7BC && off != 0x898 && off != 0x8D0 &&
        off != 0x8F8 && off != 0x9D6)
    {
        return;
    }

    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0)
        r9 = base;

    if (off == 0x7BC && seenEntry < 8)
    {
        u32 object = 0;
        u32 arg1 = 0;
        u32 page = 0;
        u16 total = 0;
        u16 start = 0;
        u16 loaded = 0;
        u16 last = 0;
        ++seenEntry;
        uc_reg_read(MTK, UC_ARM_REG_R0, &object);
        uc_reg_read(MTK, UC_ARM_REG_R1, &arg1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &page);
        if (page != 0)
        {
            uc_mem_read(MTK, page + 4, &total, sizeof(total));
            uc_mem_read(MTK, page + 6, &start, sizeof(start));
            uc_mem_read(MTK, page + 8, &loaded, sizeof(loaded));
            uc_mem_read(MTK, page + 0xA, &last, sizeof(last));
        }
        vm_autotest_note("shop_parser entry pc=%08x base=%08x r9=%08x object=%08x arg1=%08x page=%08x total=%u start=%u loaded=%u last=%u count=%u\n",
                         pc, base, r9, object, arg1, page, total, start,
                         loaded, last, seenEntry);
    }
    else if (off == 0x898 && seenRows < 16)
    {
        u32 rowCount = 0;
        u32 page = 0;
        u32 listBase = 0;
        u16 total = 0;
        u16 start = 0;
        ++seenRows;
        uc_reg_read(MTK, UC_ARM_REG_R0, &rowCount);
        uc_reg_read(MTK, UC_ARM_REG_R6, &page);
        uc_reg_read(MTK, UC_ARM_REG_R7, &listBase);
        if (page != 0)
        {
            uc_mem_read(MTK, page + 4, &total, sizeof(total));
            uc_mem_read(MTK, page + 6, &start, sizeof(start));
        }
        vm_autotest_note("shop_parser rows pc=%08x base=%08x page=%08x total=%u start=%u row_count=%u list_base=%08x count=%u\n",
                         pc, base, page, total, start, rowCount, listBase,
                         seenRows);
    }
    else if (off == 0x8D0 && seenItemId < 24)
    {
        u32 itemId = 0;
        u32 rowIndex = 0;
        u32 rowOffset = 0;
        u32 listBase = 0;
        u32 rowPtr = 0;
        ++seenItemId;
        uc_reg_read(MTK, UC_ARM_REG_R0, &itemId);
        uc_reg_read(MTK, UC_ARM_REG_R1, &rowOffset);
        uc_reg_read(MTK, UC_ARM_REG_R6, &rowIndex);
        uc_reg_read(MTK, UC_ARM_REG_R7, &listBase);
        rowPtr = listBase + rowOffset;
        vm_autotest_note("shop_parser item pc=%08x base=%08x row=%u row_ptr=%08x item_id=%u count=%u\n",
                         pc, base, rowIndex, rowPtr, itemId, seenItemId);
    }
    else if (off == 0x8F8 && seenName < 24)
    {
        u32 rowPtr = 0;
        u32 itemId = 0;
        u32 sp = 0;
        u32 nameLen = 0;
        char nameHex[64];
        ++seenName;
        nameHex[0] = 0;
        uc_reg_read(MTK, UC_ARM_REG_R4, &rowPtr);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp != 0)
            uc_mem_read(MTK, sp + 0x14, &nameLen, sizeof(nameLen));
        if (rowPtr != 0)
        {
            uc_mem_read(MTK, rowPtr, &itemId, sizeof(itemId));
            vm_autotest_format_mem_hex(rowPtr + 4, 12, nameHex, sizeof(nameHex));
        }
        vm_autotest_note("shop_parser name pc=%08x base=%08x row_ptr=%08x item_id=%u name_len=%u name_hex=%s count=%u\n",
                         pc, base, rowPtr, itemId, nameLen, nameHex, seenName);
    }
    else if (off == 0x9D6 && seenDone < 12)
    {
        u32 manager = r9 + 0x2838;
        u32 buyBase = 0;
        u32 sellBase = 0;
        u32 buyItem0 = 0;
        u32 sellItem0 = 0;
        char buyNameHex[64];
        char sellNameHex[64];
        ++seenDone;
        buyNameHex[0] = 0;
        sellNameHex[0] = 0;
        uc_mem_read(MTK, manager + 0x18, &buyBase, sizeof(buyBase));
        uc_mem_read(MTK, manager + 0x1C, &sellBase, sizeof(sellBase));
        if (buyBase != 0)
        {
            uc_mem_read(MTK, buyBase, &buyItem0, sizeof(buyItem0));
            vm_autotest_format_mem_hex(buyBase + 4, 12, buyNameHex, sizeof(buyNameHex));
        }
        if (sellBase != 0)
        {
            uc_mem_read(MTK, sellBase, &sellItem0, sizeof(sellItem0));
            vm_autotest_format_mem_hex(sellBase + 4, 12, sellNameHex, sizeof(sellNameHex));
        }
        vm_autotest_note("shop_parser done pc=%08x base=%08x r9=%08x buy_base=%08x buy0=%u buy_name=%s sell_base=%08x sell0=%u sell_name=%s count=%u\n",
                         pc, base, r9, buyBase, buyItem0, buyNameHex,
                         sellBase, sellItem0, sellNameHex, seenDone);
    }
}

static void vm_note_mmgame_transfer_parser_pc(u32 pc)
{
    static u32 seenResult16_3 = 0;
    static u32 seenResult16_2 = 0;
    static u32 seenSubBccCall = 0;
    static u32 seenSubBccEntry = 0;
    u32 object = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u32 result = 0;
    u32 index = 0;
    u32 getterRaw = 0;
    u32 getterString = 0;
    u32 getterU16 = 0;
    u32 getterInt = 0;
    u32 getterLen = 0;
    char objectHead[64];

    objectHead[0] = 0;

    if (pc != 0x05181250 && pc != 0x0518138E &&
        pc != 0x051813FE && pc != 0x05180BCC)
    {
        return;
    }

    if (pc == 0x05181250)
    {
        if (seenResult16_3 >= 32)
            return;
        ++seenResult16_3;
        uc_reg_read(MTK, UC_ARM_REG_R0, &result);
        uc_reg_read(MTK, UC_ARM_REG_R5, &object);
    }
    else if (pc == 0x0518138E)
    {
        if (seenResult16_2 >= 32)
            return;
        ++seenResult16_2;
        uc_reg_read(MTK, UC_ARM_REG_R0, &result);
        uc_reg_read(MTK, UC_ARM_REG_R5, &object);
    }
    else if (pc == 0x051813FE)
    {
        if (seenSubBccCall >= 32)
            return;
        ++seenSubBccCall;
        uc_reg_read(MTK, UC_ARM_REG_R0, &object);
        uc_reg_read(MTK, UC_ARM_REG_R1, &index);
    }
    else
    {
        if (seenSubBccEntry >= 32)
            return;
        ++seenSubBccEntry;
        uc_reg_read(MTK, UC_ARM_REG_R0, &object);
        uc_reg_read(MTK, UC_ARM_REG_R1, &index);
    }

    if (object != 0)
    {
        uc_mem_read(MTK, object + 4, &kind, sizeof(kind));
        uc_mem_read(MTK, object + 8, &subtype, sizeof(subtype));
        uc_mem_read(MTK, object + 0x28, &getterRaw, sizeof(getterRaw));
        uc_mem_read(MTK, object + 0x40, &getterString, sizeof(getterString));
        uc_mem_read(MTK, object + 0x44, &getterU16, sizeof(getterU16));
        uc_mem_read(MTK, object + 0x4C, &getterInt, sizeof(getterInt));
        uc_mem_read(MTK, object + 0x54, &getterLen, sizeof(getterLen));
        vm_autotest_format_mem_hex(object, 16, objectHead, sizeof(objectHead));
    }

    if (pc == 0x05181250 || pc == 0x0518138E)
    {
        printf("[info][mmgame] transfer_result pc=%08x subtype=%u result=%u object=%08x kind=%u getter_int=%08x head=%s count=%u\n",
               pc, subtype, result, object, kind, getterInt, objectHead,
               pc == 0x05181250 ? seenResult16_3 : seenResult16_2);
        vm_autotest_note("mmgame_transfer_result pc=%08x subtype=%u result=%u object=%08x kind=%u getter_int=%08x head=%s count=%u\n",
                         pc, subtype, result, object, kind, getterInt, objectHead,
                         pc == 0x05181250 ? seenResult16_3 : seenResult16_2);
        return;
    }

    printf("[info][mmgame] transfer_sub_bcc pc=%08x object=%08x kind=%u subtype=%u index=%u getters raw=%08x str=%08x u16=%08x int=%08x len=%08x head=%s count=%u\n",
           pc, object, kind, subtype, index, getterRaw, getterString, getterU16,
           getterInt, getterLen, objectHead,
           pc == 0x051813FE ? seenSubBccCall : seenSubBccEntry);
    vm_autotest_note("mmgame_transfer_sub_bcc pc=%08x object=%08x kind=%u subtype=%u index=%u getters raw=%08x str=%08x u16=%08x int=%08x len=%08x head=%s count=%u\n",
                     pc, object, kind, subtype, index, getterRaw, getterString,
                     getterU16, getterInt, getterLen, objectHead,
                     pc == 0x051813FE ? seenSubBccCall : seenSubBccEntry);
}

static void vm_note_stream_read_i16_pc(u32 pc)
{
    static u32 seenNullBlob = 0;
    u32 blob = 0;
    u32 reader = 0;
    u32 cursor = 0;
    u32 lr = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    char readerHead[64];

    if (pc != 0x01033A42)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &blob);
    if (blob != 0)
        return;
    if (seenNullBlob >= 16)
        return;
    ++seenNullBlob;

    readerHead[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_R1, &reader);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (reader != 0)
    {
        uc_mem_read(MTK, reader, &cursor, sizeof(cursor));
        vm_autotest_format_mem_hex(reader, 32, readerHead, sizeof(readerHead));
    }

    printf("[info][stream] read_i16_null_blob pc=%08x lr=%08x reader=%08x cursor=%u r2=%08x r3=%08x reader_head=%s count=%u\n",
           pc, lr, reader, cursor, r2, r3, readerHead, seenNullBlob);
    vm_autotest_note("stream_read_i16_null_blob pc=%08x lr=%08x reader=%08x cursor=%u r2=%08x r3=%08x reader_head=%s count=%u\n",
                     pc, lr, reader, cursor, r2, r3, readerHead, seenNullBlob);
}

static void vm_note_net_wrapper_pc(u32 pc)
{
    static u32 seen = 0;
    u32 r0 = 0;
    u32 r3 = 0;
    u32 r7 = 0;
    u32 wrapperState = 0;
    u32 wrapperCb = 0;
    u32 businessObj = 0;
    u32 businessState = 0;
    u32 businessCb = 0;
    u8 sceneReady = 0;

    if (!g_netDebugReadWindow)
        return;
    if (pc != 0x010348A8 && pc != 0x010348D8 && pc != 0x010348EC &&
        pc != 0x01012E64 && pc != 0x01012E84)
    {
        return;
    }
    if (seen >= 64)
        return;
    ++seen;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);

    if (pc == 0x010348A8)
    {
        if (r0)
        {
            uc_mem_read(MTK, r0 + 0x0c, &wrapperState, sizeof(wrapperState));
            uc_mem_read(MTK, r0 + 0x44, &wrapperCb, sizeof(wrapperCb));
        }
        printf("[info][network] net_wrapper_state pc=%08x event=%u obj=%08x state=%u cb=%08x\n",
               pc, r3, r0, wrapperState & 0xff, wrapperCb);
        return;
    }

    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 0x94a8, &businessObj, sizeof(businessObj));
        if (businessObj)
        {
            uc_mem_read(MTK, businessObj + 0x0c, &businessState, sizeof(businessState));
            uc_mem_read(MTK, businessObj + 0x14, &businessCb, sizeof(businessCb));
        }
        if (pc == 0x01012E64 || pc == 0x01012E84)
        {
            u32 sceneObj = 0;
            uc_mem_read(MTK, Global_R9 + 0x54ac, &sceneObj, sizeof(sceneObj));
            if (sceneObj)
                uc_mem_read(MTK, sceneObj + 0x164, &sceneReady, sizeof(sceneReady));
        }
    }

    if (pc == 0x010348D8 || pc == 0x010348EC)
    {
        printf("[info][network] net_wrapper_business pc=%08x event=%u biz=%08x state=%u cb=%08x r7=%08x\n",
               pc, r3, businessObj, businessState, businessCb, r7);
        return;
    }

    printf("[info][network] net_business_gate pc=%08x event=%u data=%08x biz=%08x state=%u cb=%08x scene_ready=%u r0=%08x\n",
           pc, r3, r0, businessObj, businessState, businessCb, sceneReady, r0);
}

static void vm_autotest_dump_scene_tables(u32 elapsedMs)
{
    static u32 nextDumpMs = 0;
    u32 sceneNodeBase = 0;
    u32 moveEntryBase = 0;
    u32 moveEntryCount = 0;
    u32 sceneObj = 0;
    u8 pending = 0;
    u8 ready = 0;
    u8 assetsReady = 0;
    u8 sceneState = 0;
    u16 parserState = 0;
    u16 currentX = 0;
    u16 currentY = 0;

    if (!g_autotestEnabled || Global_R9 == 0 || elapsedMs < nextDumpMs)
        return;
    nextDumpMs = elapsedMs + 3000;

    uc_mem_read(MTK, Global_R9 + 0x5CB0, &sceneNodeBase, sizeof(sceneNodeBase));
    uc_mem_read(MTK, Global_R9 + 0x5CE4, &moveEntryBase, sizeof(moveEntryBase));
    uc_mem_read(MTK, Global_R9 + 0x5D40, &moveEntryCount, sizeof(moveEntryCount));
    uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, sizeof(sceneObj));
    uc_mem_read(MTK, Global_R9 + 0x5C6B, &pending, sizeof(pending));
    uc_mem_read(MTK, Global_R9 + 0x5C67, &ready, sizeof(ready));
    uc_mem_read(MTK, Global_R9 + 0x5C68, &assetsReady, sizeof(assetsReady));
    uc_mem_read(MTK, Global_R9 + 0x4CB6, &sceneState, sizeof(sceneState));
    uc_mem_read(MTK, Global_R9 + 0x5C8E, &currentX, sizeof(currentX));
    uc_mem_read(MTK, Global_R9 + 0x5C90, &currentY, sizeof(currentY));
    if (sceneObj != 0)
        uc_mem_read(MTK, sceneObj + 0x1B4, &parserState, sizeof(parserState));
    vm_autotest_note("scene_probe elapsed=%u sceneObj=%08x sceneState=%u pending=%u ready=%u assets=%u parserState=%u pos=(%u,%u) sceneNodeBase=%08x moveEntryBase=%08x moveEntryCount=%u\n",
                     elapsedMs, sceneObj, sceneState, pending, ready, assetsReady, parserState, currentX, currentY,
                     sceneNodeBase, moveEntryBase, moveEntryCount);

    if (moveEntryBase != 0 && moveEntryCount < 64)
    {
        u32 limit = moveEntryCount < 12 ? moveEntryCount : 12;
        for (u32 i = 0; i < limit; ++i)
        {
            u32 entry = moveEntryBase + i * 32;
            u16 actorId = 0;
            u16 x = 0;
            u16 y = 0;
            u16 tx = 0;
            u16 ty = 0;
            u16 facing = 0;
            u16 pose = 0;
            u32 namePtr = 0;
            u8 moveState = 0;
            u8 kind = 0;
            u16 targetActorId = 0;
            u32 titlePtr = 0;
            uc_mem_read(MTK, entry + 0x00, &actorId, sizeof(actorId));
            uc_mem_read(MTK, entry + 0x02, &x, sizeof(x));
            uc_mem_read(MTK, entry + 0x04, &y, sizeof(y));
            uc_mem_read(MTK, entry + 0x06, &tx, sizeof(tx));
            uc_mem_read(MTK, entry + 0x08, &ty, sizeof(ty));
            uc_mem_read(MTK, entry + 0x0A, &facing, sizeof(facing));
            uc_mem_read(MTK, entry + 0x0C, &pose, sizeof(pose));
            uc_mem_read(MTK, entry + 0x10, &namePtr, sizeof(namePtr));
            uc_mem_read(MTK, entry + 0x16, &moveState, sizeof(moveState));
            uc_mem_read(MTK, entry + 0x17, &kind, sizeof(kind));
            uc_mem_read(MTK, entry + 0x18, &targetActorId, sizeof(targetActorId));
            uc_mem_read(MTK, entry + 0x1C, &titlePtr, sizeof(titlePtr));
            vm_autotest_note("scene_probe_move[%u] id=%u pos=(%u,%u) target=(%u,%u) facing=%u pose=%u namePtr=%08x state=%u kind=%u targetId=%u titlePtr=%08x\n",
                             i, actorId, x, y, tx, ty, facing, pose, namePtr, moveState, kind, targetActorId, titlePtr);
        }
    }

    if (sceneNodeBase != 0)
    {
        for (u32 i = 0; i < 8; ++i)
        {
            u32 node = sceneNodeBase + i * 340;
            u32 actorId = 0;
            u16 x = 0;
            u16 y = 0;
            u32 labelPtr = 0;
            u8 nodeKind = 0;
            u8 promptKind = 0;
            u8 active = 0;
            u8 visualVariant = 0;
            u8 visualGroup = 0;
            u8 targetX = 0;
            u8 targetY = 0;
            u32 battleX = 0;
            u32 battleY = 0;
            u32 battleHp = 0;
            u32 battleHpMax = 0;
            uc_mem_read(MTK, node + 0x64, &actorId, sizeof(actorId));
            uc_mem_read(MTK, node + 0x18, &x, sizeof(x));
            uc_mem_read(MTK, node + 0x1A, &y, sizeof(y));
            uc_mem_read(MTK, node + 0x44, &labelPtr, sizeof(labelPtr));
            uc_mem_read(MTK, node + 0x13B, &nodeKind, sizeof(nodeKind));
            uc_mem_read(MTK, node + 0x13C, &promptKind, sizeof(promptKind));
            uc_mem_read(MTK, node + 0x13F, &active, sizeof(active));
            uc_mem_read(MTK, node + 0x140, &visualVariant, sizeof(visualVariant));
            uc_mem_read(MTK, node + 0x141, &visualGroup, sizeof(visualGroup));
            uc_mem_read(MTK, node + 0x11E, &targetX, sizeof(targetX));
            uc_mem_read(MTK, node + 0x120, &targetY, sizeof(targetY));
            uc_mem_read(MTK, node + 0xB4, &battleHp, sizeof(battleHp));
            uc_mem_read(MTK, node + 0xBC, &battleHpMax, sizeof(battleHpMax));
            uc_mem_read(MTK, node + 0xF0, &battleX, sizeof(battleX));
            uc_mem_read(MTK, node + 0xF4, &battleY, sizeof(battleY));
            if (actorId != 0 || active != 0 || nodeKind != 0 || promptKind != 0)
            {
                vm_autotest_note("scene_probe_node[%u] actorId=%u pos=(%u,%u) battlePos=(%u,%u) battleHp=%u/%u labelPtr=%08x kind=%u prompt=%u active=%u visual=(%u,%u) target=(%u,%u)\n",
                                 i, actorId, x, y, battleX, battleY, battleHp, battleHpMax,
                                 labelPtr, nodeKind, promptKind, active, visualVariant,
                                 visualGroup, targetX, targetY);
            }
        }
    }
}

static bool vm_autotest_find_battle_screen(u32 *screenOut, u32 *codeBaseOut,
                                           u32 *moduleR9Out, u32 *inferredModuleR9Out)
{
    u32 screen = 0;
    u32 codeBase = 0;
    u32 inferredModuleR9 = 0;
    u32 stackModuleR9 = 0;

    if (vm_infer_battle_module_from_screen(vmAddedScreen, &codeBase, &inferredModuleR9))
    {
        stackModuleR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
        if (screenOut)
            *screenOut = vmAddedScreen;
        if (codeBaseOut)
            *codeBaseOut = codeBase;
        if (moduleR9Out)
            *moduleR9Out = stackModuleR9 != 0 ? stackModuleR9 : inferredModuleR9;
        if (inferredModuleR9Out)
            *inferredModuleR9Out = inferredModuleR9;
        return true;
    }

    for (u32 i = g_screenStackCount; i > 0; --i)
    {
        screen = g_screenStack[i - 1];
        if (!vm_infer_battle_module_from_screen(screen, &codeBase, &inferredModuleR9))
            continue;
        stackModuleR9 = g_screenStackModuleBase[i - 1];
        if (screenOut)
            *screenOut = screen;
        if (codeBaseOut)
            *codeBaseOut = codeBase;
        if (moduleR9Out)
            *moduleR9Out = stackModuleR9 != 0 ? stackModuleR9 : inferredModuleR9;
        if (inferredModuleR9Out)
            *inferredModuleR9Out = inferredModuleR9;
        return true;
    }

    return false;
}

static void vm_autotest_dump_battle_state(u32 elapsedMs)
{
    static u32 nextDumpMs = 0;
    u32 screen = 0;
    u32 codeBase = 0;
    u32 battleR9 = 0;
    u32 inferredR9 = 0;
    u32 uiObj = 0;
    u32 parserObj = 0;
    u32 parserCount = 0;
    u32 parserSlots = 0;
    u16 phase = 0;
    u32 side = 0;
    u8 cmd2 = 0;
    u8 cmd3 = 0;
    u8 cmd4 = 0;
    u8 cmd5 = 0;
    u8 ui986 = 0;
    u8 ui992 = 0;
    u8 ui1136 = 0;
    u8 ui1138 = 0;
    u8 ui1140 = 0;
    u8 ui1206 = 0;
    u8 ui1207 = 0;
    u8 ui1278 = 0;
    u8 actionAnimCount = 0;
    u8 actionDamageCount = 0;

    if (!g_autotestEnabled || Global_R9 == 0 || elapsedMs < nextDumpMs)
        return;
    nextDumpMs = elapsedMs + 3000;
    if (g_mockBattleOperateSessionArmed == 0 && g_mockBattleEnemyHpMax == 0)
        return;
    if (!vm_autotest_find_battle_screen(&screen, &codeBase, &battleR9, &inferredR9))
        return;

    uc_mem_read(MTK, battleR9 + 8272, &uiObj, sizeof(uiObj));
    uc_mem_read(MTK, battleR9 + 10340, &parserObj, sizeof(parserObj));
    uc_mem_read(MTK, battleR9 + 13412, &phase, sizeof(phase));
    uc_mem_read(MTK, battleR9 + 13488, &side, sizeof(side));
    uc_mem_read(MTK, battleR9 + 10522, &cmd2, sizeof(cmd2));
    uc_mem_read(MTK, battleR9 + 10523, &cmd3, sizeof(cmd3));
    uc_mem_read(MTK, battleR9 + 10524, &cmd4, sizeof(cmd4));
    uc_mem_read(MTK, battleR9 + 10525, &cmd5, sizeof(cmd5));
    uc_mem_read(MTK, battleR9 + 15804, &actionAnimCount, sizeof(actionAnimCount));
    uc_mem_read(MTK, battleR9 + 15814, &actionDamageCount, sizeof(actionDamageCount));
    if (parserObj != 0)
    {
        uc_mem_read(MTK, parserObj + 16, &parserCount, sizeof(parserCount));
        uc_mem_read(MTK, parserObj + 24, &parserSlots, sizeof(parserSlots));
    }
    if (uiObj != 0)
    {
        uc_mem_read(MTK, uiObj + 986, &ui986, sizeof(ui986));
        uc_mem_read(MTK, uiObj + 992, &ui992, sizeof(ui992));
        uc_mem_read(MTK, uiObj + 1136, &ui1136, sizeof(ui1136));
        uc_mem_read(MTK, uiObj + 1138, &ui1138, sizeof(ui1138));
        uc_mem_read(MTK, uiObj + 1140, &ui1140, sizeof(ui1140));
        uc_mem_read(MTK, uiObj + 1206, &ui1206, sizeof(ui1206));
        uc_mem_read(MTK, uiObj + 1207, &ui1207, sizeof(ui1207));
        uc_mem_read(MTK, uiObj + 1278, &ui1278, sizeof(ui1278));
    }

    vm_autotest_note("battle_probe elapsed=%u screen=%08x code=%08x r9=%08x inferredR9=%08x phase=%u side=%u cmd=%u/%u/%u/%u ui=%08x flags986=%u flags992=%u flags1136=%u flags1138=%u flags1140=%u flags1206=%u flags1207=%u flags1278=%u parser=%08x parserCount=%u parserSlots=%08x animCount=%u damageCount=%u\n",
                     elapsedMs, screen, codeBase, battleR9, inferredR9, phase, side,
                     cmd2, cmd3, cmd4, cmd5, uiObj, ui986, ui992, ui1136, ui1138,
                     ui1140, ui1206, ui1207, ui1278, parserObj, parserCount,
                     parserSlots, actionAnimCount, actionDamageCount);

    for (u32 i = 0; i < 3; ++i)
    {
        u32 slot = battleR9 + 10532 + i * 100;
        u8 active = 0;
        u8 type = 0;
        u8 actor = 0;
        u8 childCount = 0;
        u8 target = 0;
        u8 childFlag = 0;
        u8 childConsumed = 0;
        u8 tail0 = 0;
        u8 tail1 = 0;
        u8 tail2 = 0;
        u32 valueA = 0;
        u32 valueB = 0;
        u32 effect = 0;

        uc_mem_read(MTK, slot + 0, &active, sizeof(active));
        uc_mem_read(MTK, slot + 1, &type, sizeof(type));
        uc_mem_read(MTK, slot + 2, &actor, sizeof(actor));
        uc_mem_read(MTK, slot + 3, &childCount, sizeof(childCount));
        uc_mem_read(MTK, slot + 20, &childConsumed, sizeof(childConsumed));
        uc_mem_read(MTK, slot + 21, &target, sizeof(target));
        uc_mem_read(MTK, slot + 22, &childFlag, sizeof(childFlag));
        uc_mem_read(MTK, slot + 24, &valueA, sizeof(valueA));
        uc_mem_read(MTK, slot + 28, &valueB, sizeof(valueB));
        uc_mem_read(MTK, slot + 92, &effect, sizeof(effect));
        uc_mem_read(MTK, slot + 96, &tail0, sizeof(tail0));
        uc_mem_read(MTK, slot + 97, &tail1, sizeof(tail1));
        uc_mem_read(MTK, slot + 98, &tail2, sizeof(tail2));
        if (active != 0 || type != 0 || actor != 0 || childCount != 0 ||
            target != 0 || childFlag != 0 || valueA != 0 || valueB != 0 ||
            effect != 0 || tail0 != 0 || tail1 != 0 || tail2 != 0)
        {
            vm_autotest_note("battle_probe_action[%u] active=%u type=%u actor=%u childCount=%u childConsumed=%u target=%u childFlag=%u valueA=%u valueB=%u effect=%u tail=%u/%u/%u\n",
                             i, active, type, actor, childCount, childConsumed,
                             target, childFlag, valueA, valueB, effect,
                             tail0, tail1, tail2);
        }
    }

    for (u32 i = 0; i < 3; ++i)
    {
        u32 unit = battleR9 + 10520 + 1312 + i * 196;
        u32 id = 0;
        u32 kind = 0;
        u8 active = 0;
        u8 visualA = 0;
        u8 visualB = 0;
        u8 typeByte10 = 0;
        u8 flag1322 = 0;
        u8 flag1323 = 0;
        u32 state1324 = 0;
        u32 hp = 0;
        u32 hpMax = 0;
        u32 mp = 0;
        u32 mpMax = 0;
        u32 nameWord0 = 0;
        u32 spritePtr = 0;

        uc_mem_read(MTK, unit + 4, &id, sizeof(id));
        uc_mem_read(MTK, unit + 8, &visualA, sizeof(visualA));
        uc_mem_read(MTK, unit + 9, &visualB, sizeof(visualB));
        uc_mem_read(MTK, unit + 10, &typeByte10, sizeof(typeByte10));
        uc_mem_read(MTK, unit + 11, &active, sizeof(active));
        uc_mem_read(MTK, unit + 12, &kind, sizeof(kind));
        uc_mem_read(MTK, unit + 1322, &flag1322, sizeof(flag1322));
        uc_mem_read(MTK, unit + 1323, &flag1323, sizeof(flag1323));
        uc_mem_read(MTK, unit + 1324, &state1324, sizeof(state1324));
        uc_mem_read(MTK, unit + 16, &hp, sizeof(hp));
        uc_mem_read(MTK, unit + 20, &hpMax, sizeof(hpMax));
        uc_mem_read(MTK, unit + 24, &mp, sizeof(mp));
        uc_mem_read(MTK, unit + 28, &mpMax, sizeof(mpMax));
        uc_mem_read(MTK, unit + 54, &nameWord0, sizeof(nameWord0));
        uc_mem_read(MTK, unit + 88, &spritePtr, sizeof(spritePtr));
        if (id != 0 || active != 0 || hp != 0 || hpMax != 0)
        {
            vm_autotest_note("battle_probe_unit[%u] id=%u active=%u kind=%u type10=%u visual=(%u,%u) sprite=%08x name0=%08x flag1322=%u flag1323=%u state1324=%u hp=%d hpMax=%u mp=%d mpMax=%u\n",
                             i, id, active, kind, typeByte10, visualA, visualB,
                             spritePtr, nameWord0, flag1322, flag1323, state1324,
                             (int32_t)hp, hpMax,
                             (int32_t)mp, mpMax);
        }
    }
}

static void vm_autotest_save_screenshot(u32 elapsedMs)
{
    char path[128];
    SDL_Surface *sfc = SDL_GetWindowSurface(window);
    if (!sfc)
        return;
    snprintf(path, sizeof(path), "autotest/screens/%06u_%08u.bmp",
             g_autotestShotIndex++, elapsedMs);
    if (SDL_SaveBMP(sfc, path) != 0)
        printf("[warn][autotest] save_screenshot_failed path=%s err=%s\n", path, SDL_GetError());
}

static void vm_autotest_tick(void)
{
    if (!g_autotestEnabled)
        return;
    u32 now = SDL_GetTicks();
    if (g_autotestStartMs == 0)
    {
        g_autotestStartMs = now;
        g_autotestNextShotMs = 0;
    }
    u32 elapsed = now - g_autotestStartMs;

    vm_autotest_dump_scene_tables(elapsed);
    vm_autotest_dump_battle_state(elapsed);

    if (elapsed >= g_autotestNextShotMs)
    {
        vm_autotest_save_screenshot(elapsed);
        g_autotestNextShotMs = elapsed + g_autotestShotIntervalMs;
    }

    if (g_autotestTapReleasePending && elapsed >= g_autotestTapReleaseMs)
    {
        vm_autotest_release_tap();
    }
    if (g_autotestKeyReleasePending && elapsed >= g_autotestKeyReleaseMs)
    {
        keyEvent(MR_KEY_RELEASE, g_autotestKeyReleaseSym);
        g_autotestKeyReleasePending = 0;
    }

    for (u32 i = 0; i < g_autotestActionCount; ++i)
    {
        vm_autotest_action *action = &g_autotestActions[i];
        if (action->fired || elapsed < action->atMs)
            continue;
        action->fired = 1;
        if (action->type == VM_AUTOTEST_ACTION_TAP)
        {
            mouseEvent(MR_MOUSE_DOWN, action->a, action->b);
            g_autotestTapReleasePending = 1;
            g_autotestTapReleaseWindow = 0;
            g_autotestTapReleaseMs = elapsed + 80;
            g_autotestTapReleaseX = action->a;
            g_autotestTapReleaseY = action->b;
        }
        else if (action->type == VM_AUTOTEST_ACTION_WINDOW_TAP)
        {
            int vmX = action->a;
            int vmY = action->b;
            LcdWindowPointToVm(action->a, action->b, &vmX, &vmY);
            vm_autotest_note("autotest_windowtap window=(%d,%d) vm=(%d,%d) rotate=%s toolbar=%d\n",
                             action->a, action->b, vmX, vmY,
                             LcdRotationName(LcdGetRotation()),
                             LcdGetToolbarHeight());
            windowMouseEvent(MR_MOUSE_DOWN, action->a, action->b);
            g_autotestTapReleasePending = 1;
            g_autotestTapReleaseWindow = 1;
            g_autotestTapReleaseMs = elapsed + 80;
            g_autotestTapReleaseX = action->a;
            g_autotestTapReleaseY = action->b;
        }
        else if (action->type == VM_AUTOTEST_ACTION_KEY)
        {
            keyEvent(MR_KEY_PRESS, action->a);
            g_autotestKeyReleasePending = 1;
            g_autotestKeyReleaseMs = elapsed + 80;
            g_autotestKeyReleaseSym = action->a;
        }
        else if (action->type == VM_AUTOTEST_ACTION_HOLD_KEY)
        {
            keyEvent(MR_KEY_PRESS, action->a);
            g_autotestKeyReleasePending = 1;
            g_autotestKeyReleaseMs = elapsed + (u32)action->b;
            g_autotestKeyReleaseSym = action->a;
        }
    }

    if (g_autotestMaxMs > 0 && elapsed >= g_autotestMaxMs)
    {
        vm_autotest_note("autotest_quit_request elapsed=%u max_ms=%u\n", elapsed, g_autotestMaxMs);
        g_autotestMaxMs = 0;
        vm_request_host_quit("autotest");
    }
}


void loop()
{
    void *thread_ret;
    SDL_Event ev;
    bool isLoop = true;
    while (isLoop)
    {
        vm_input_sync_sdl_text_input();
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
            {
                vm_request_host_quit("window_close");
                break;
            }
            if (g_hostQuitRequested)
                continue;
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
                if (g_vmInputOpen)
                    break;
                if (isMouseDown)
                {
                    windowMouseEvent(MR_MOUSE_MOVE, ev.motion.x, ev.motion.y);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
            {
                int toolbarAction = LcdHandleToolbarMouseDown(ev.button.x, ev.button.y);
                if (toolbarAction)
                {
                    isMouseDown = false;
                    if (toolbarAction == 2)
                    {
                        printf("[info][lcd] rotate=%s view=%dx%d window=%dx%d\n",
                               LcdRotationName(LcdGetRotation()),
                               LcdGetViewWidth(), LcdGetViewHeight(),
                               LcdGetWindowWidth(), LcdGetWindowHeight());
                    }
                    vm_lcd_update_with_input_overlay();
                    break;
                }
                if (g_vmInputOpen)
                {
                    isMouseDown = false;
                    break;
                }
                isMouseDown = true;
                windowMouseEvent(MR_MOUSE_DOWN, ev.button.x, ev.button.y);
                break;
            }
            case SDL_MOUSEBUTTONUP:
                if (isMouseDown)
                    windowMouseEvent(MR_MOUSE_UP, ev.button.x, ev.button.y);
                isMouseDown = false;
                break;
            case SDL_TEXTINPUT:
                if (g_vmInputOpen)
                {
                    g_vmInputComposition[0] = 0;
                    vm_input_enqueue_utf8_text(ev.text.text);
                }
                break;
            case SDL_TEXTEDITING:
                if (g_vmInputOpen)
                {
                    snprintf(g_vmInputComposition, sizeof(g_vmInputComposition),
                             "%s", ev.edit.text);
                }
                break;
            }
        }
        vm_autotest_tick();
        if (g_vmThreadFinished)
            isLoop = false;
        SDL_Delay(16);
    }
    g_vmInputSdlTextInputWanted = 0;
    vm_input_sync_sdl_text_input();
#ifndef CBE_PLATFORM_ANDROID
    pthread_join(&EmuThread, &thread_ret);
#else
    (void)thread_ret;
#endif
    if (SD_File_Handle != NULL)
        fclose(SD_File_Handle);
    if (g_autotestStateFile != NULL)
        fclose(g_autotestStateFile);
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

#define LOAD_CBE_PATH "CBE/僵尸先生.CBE"//这个加载太慢了
#define LOAD_CBE_PATH "CBE/钻石迷情3.CBE"
#define LOAD_CBE_PATH "CBE/捕鱼猎人.CBE"
#define LOAD_CBE_PATH "CBE/枪之荣誉.CBE"
#define LOAD_CBE_PATH "CBE/鬼吹灯.CBE"
#define LOAD_CBE_PATH "CBE/战争机器.CBE"
#define LOAD_CBE_PATH "CBE/涂鸦跳跃.CBE"
#define LOAD_CBE_PATH "CBE/魔塔.CBE"
#define LOAD_CBE_PATH "CBE/孤岛.CBE"
#define LOAD_CBE_PATH "CBE/恶魔城.CBE"
#define LOAD_CBE_PATH "CBE/鬼吹灯.CBE"
#define LOAD_CBE_PATH "CBE/皇牌空战.CBE"
#define LOAD_CBE_PATH "CBE/涂鸦跳跃.CBE"
#define LOAD_CBE_PATH "CBE/血剑Online.CBE"
#define LOAD_CBE_PATH "CBE/愤怒的小鸟.CBE"
#define LOAD_CBE_PATH "CBE/歪歪猫发条城历险记V100.CBE"
#define LOAD_CBE_PATH "CBE/武林外传(新品).CBE"
#define LOAD_CBE_PATH "CBE/众神之战.CBE"
#define LOAD_CBE_PATH "CBE/恶魔城登录版.CBE"
#define LOAD_CBE_PATH "CBE/恶魔城登录版.CBE"
#define LOAD_CBE_PATH "CBE/江湖OL.CBE"

#ifdef CBE_PLATFORM_ANDROID
static char g_cbeLoadPathUtf8[260] = "CBE/江湖OL.cbe";
#else
static char g_cbeLoadPathUtf8[260] = LOAD_CBE_PATH;
#endif

static void vm_cbe_init_config(int argc, char *args[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (strncmp(args[i], "--cbe=", 6) == 0 && args[i][6] != 0)
        {
            snprintf(g_cbeLoadPathUtf8, sizeof(g_cbeLoadPathUtf8), "%s", args[i] + 6);
        }
    }
    printf("[info][cbe] selected=%s\n", g_cbeLoadPathUtf8);
}

static bool vm_cbe_uses_fixed_base_manager_abi(void)
{
    /* Older fixed-base images reserve manager-copy space after the actual
     * code.  Newer images map code exactly and call a single native dispatcher. */
    return g_cbeInfo.isBiggianProgram && g_cbeInfo.headerInt1 != 0 &&
           g_cbeInfo.headerInt2 > g_cbeInfo.codeLen;
}

typedef struct
{
    u32 directoryOffset;
    u32 funcBase;
    u32 funcCount;
    const char *name;
} vm_fixed_base_manager_spec;

/* Mobile Rainbow firmware exposes manager initializers in a sparse directory.
 * A fixed-base CBE calls the initializer, passing its own destination table in
 * R0.  The table lengths below come from the copy sizes in the CBE loader. */
static const vm_fixed_base_manager_spec g_fixedBaseManagerSpecs[] = {
    {0x00, VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS, 0x60 / 4, "fileio"},
    {0x08, VM_MANAGER_LCD_FUNC_LIST_ADDRESS, 0xfc / 4, "lcd"},
    {0x10, VM_MANAGER_TIMER_FUNC_LIST_ADDRESS, 0x18 / 4, "timer"},
    {0x18, VM_MANAGER_CTRL_FUNC_LIST_ADDRESS, 0x3c / 4, "ctrl"},
    {0x20, VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS, 0x48 / 4, "memory"},
    {0x28, VM_MANAGER_BILLING_FUNC_LIST_ADDRESS, 0x40 / 4, "billing"},
    {0x30, VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS, 0x2c / 4, "screen"},
    {0x38, VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS, 0x1c / 4, "network"},
    {0x40, VM_MANAGER_UCS2_FUNC_LIST_ADDRESS, 0x30 / 4, "ucs2"},
    {0x48, VM_SYS_MANAGER_FUNC_LIST_ADDRESS, 0x6c / 4, "sys"},
    {0x50, VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS, 0x58 / 4, "df-script"},
    {0x58, VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS, 0xa0 / 4, "game-lcd"},
    {0x60, VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS, 0xa0 / 4, "gameutil"},
    {0x68, VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS, 0x3c / 4, "df-engine"},
    {0x70, VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS, 0xf0 / 4, "netapp"},
    {0x78, VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS, 0x24 / 4, "audio"},
    {0x80, VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS, 0x27c / 4, "gameold"},
    {0x8c, VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS, 0x1c / 4, "sensor"},
};

static void vm_init_fixed_base_manager_directory(void)
{
    if (!vm_cbe_uses_fixed_base_manager_abi())
        return;

    uc_mem_write(MTK, VM_NATIVE_MANAGER_DIRECTORY_ADDRESS, emptyBuff, 0x100);
    for (u32 i = 0; i < sizeof(g_fixedBaseManagerSpecs) / sizeof(g_fixedBaseManagerSpecs[0]); ++i)
        vm_set_var(VM_NATIVE_MANAGER_DIRECTORY_ADDRESS + g_fixedBaseManagerSpecs[i].directoryOffset,
                   VM_FIXED_BASE_MANAGER_INIT_ADDRESS + i * 4);

    vm_set_var(VM_Manager_Table_ADDRESS + 8, VM_NATIVE_MANAGER_DIRECTORY_ADDRESS);
    vm_set_var(VM_Manager_Table_ADDRESS + 12, VM_LOG_NOOP_ADDRESS);
    vm_set_var(VM_Manager_Table_ADDRESS + 16, VM_CURR_APP_INFO_ADDRESS);
    printf("[info][cbe] manager_abi=fixed-base-big registry=%08x directory=%08x\n",
           VM_Manager_Table_ADDRESS,
           VM_NATIVE_MANAGER_DIRECTORY_ADDRESS);
}


static int vm_ascii_stricmp(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;
    if (a == NULL || b == NULL)
        return a == b ? 0 : (a ? 1 : -1);
    while (*a && *b)
    {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return (int)ca - (int)cb;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void vm_close_open_files_for_restart(void)
{
    for (u32 i = 0; i < 16; ++i)
    {
        if (openFileList[i] != NULL && openFileList[i] != VM_PSEUDO_DIR_HANDLE)
            fclose(openFileList[i]);
        openFileList[i] = NULL;
        openFileNames[i][0] = 0;
    }
}

static void vm_reset_runtime_state_for_restart(void)
{
    vm_net_mock_async_reset();
    if (ROM_MEMPOOL)
        memset(ROM_MEMPOOL, 0, Program_ROM_Mapped_Size);
    if (STACK_MEMPOOL)
        memset(STACK_MEMPOOL, 0, 0x100000u);
    if (PRAM_MEMPOOL)
        memset(PRAM_MEMPOOL, 0, 0x100000u);
    if (RAM_MEMPOOL)
        memset(RAM_MEMPOOL, 0, VM_MEMPOOL_TOTAL_SIZE);

    if (g_cbeFileBuffer)
    {
        uc_mem_write(MTK, Program_ROM_Address, g_cbeFileBuffer + g_cbeInfo.codeOffset, g_cbeInfo.codeLen);
        uc_mem_write(MTK, Program_Data_Address, g_cbeFileBuffer + g_cbeInfo.BssDataOffset, g_cbeInfo.BssDataLen);
    }

    vm_close_open_files_for_restart();
    InitVmEvent();
    InitVmMalloc();

    memset(g_netTasks, 0, sizeof(g_netTasks));
    memset(g_netChannels, 0, sizeof(g_netChannels));
    memset(g_timerTasks, 0, sizeof(g_timerTasks));
    g_schedulerTick = 0;
    g_schedulerStartTicks = 0;
    g_nextNetConnectId = 1;
    g_netTaskDispatchDepth = 0;
    g_netTaskDispatchSlot = -1;
    g_netMockResponseLen = 0;
    g_netMockResponseOffset = 0;
    g_netMockResponseVmPtr = 0;
    g_netUpLinkData = 0;
    g_netDownLinkData = 0;
    g_netCurrentObject = 0;
    g_netDebugReadWindow = 0;
    g_netBusinessSendReadyDeferred = 0;
    g_netBusinessSendReadyRerun = 0;
    g_netBusinessSendReadyPostVm = 0;
    g_loginTail42AllocPending = 0;
    g_loginTail42FlushPending = 0;

    screenStructChange = 0;
    screenStructNotifyLoadRes = 0;
    vmAddedScreen = 0;
    memset(g_screenStack, 0, sizeof(g_screenStack));
    memset(g_screenStackParam, 0, sizeof(g_screenStackParam));
    memset(g_screenStackModuleBase, 0, sizeof(g_screenStackModuleBase));
    memset(g_screenStackDataPackage, 0, sizeof(g_screenStackDataPackage));
    memset(g_screenStackFlags, 0, sizeof(g_screenStackFlags));
    memset(g_screenStackInited, 0, sizeof(g_screenStackInited));
    g_screenStackCount = 0;
    g_screenRemovedWithoutNext = 0;
    g_screenResumeExisting = 0;
    g_screenEnterExistingNoCallback = 0;
    g_activeScreenRemovedThisFrame = 0;
    g_activeScreenRemovedThis = 0;
    g_activeScreenRemovedModuleBase = 0;
    g_activeScreenRemovedDataPackage = 0;
    g_screenExitMode = VM_SCREEN_EXIT_DESTROY;
    g_screenLoadResourcePendingScreen = 0;
    g_screenLoadResourcePendingParam = 0;
    g_currentScreenThis = 0;
    g_currentScreenModuleBase = 0;
    g_currentScreenDataPackage = 0;
    vm_dl_reset_state();
    g_screenRootExitArmed = 0;
    g_screenRootExitPending = 0;
    g_screenRootExitPendingRoot = 0;
    g_screenRootExitPendingRemoved = 0;
    g_screenRootExitPendingTick = 0;
    g_hostQuitRequested = 0;
    g_hostQuitCleanupStarted = 0;
    g_vmThreadFinished = 0;
    g_appMainEntry = 0;
    g_appExitEntry = 0;

    simulatePress = 0;
    simulateKey = 0;
    simulateTouchPress = 0;
    simulateTouchDown = 0;
    simulateTouchUp = 0;
    simulateTouchDrag = 0;
    simulateTouchX = 0;
    simulateTouchY = 0;
    g_curKeyDownState = 0;
    g_curKeyState = 0;
    g_vmInputOpen = 0;
    g_vmInputPassword = 0;
    g_vmInputCallback = 0;
    g_vmInputBuffer = 0;
    g_vmInputTargetBuffer = 0;
    g_vmInputMaxLen = 0;
    g_vmInputInputType = 0;
    g_vmInputPrompt = 0;
    g_vmInputScratchBuffer = 0;
    g_vmInputScratchBytes = 0;
    g_vmInputComposition[0] = 0;
    g_vmInputSdlTextInputWanted = 0;
    g_vmInputSdlTextInputActive = 0;

    g_lastStartupScreenState = 0xff;
    g_lastStartupUpdateObj = 0xffffffff;
    g_lastStartupProgress = 0xff;
    g_lastStartupUpdateState = 0xff;
    g_currentFontType = 0;
    g_nativeAppInitEntry = 0;
    g_nativeAppParserEntry = 0;
    g_nativeSystemInfoPtr = 0;
    g_nativePropertyInfoPtr = 0;
    g_nativeDispatchTraceCount = 0;
    g_vm_img_app_data_package = 0;
    g_vm_img_inner_data_package = 0;
    g_vm_img_current_data_package = 0;

    changeTmp3 = VM_MANAGER_TABLE_ADDRESS;
    vm_set_var(VM_Manager_Table_ADDRESS + 8, changeTmp3);
    changeTmp3 = VM_LOG_NOOP_ADDRESS;
    vm_set_var(VM_Manager_Table_ADDRESS + 12, changeTmp3);
    changeTmp3 = VM_CURR_APP_INFO_ADDRESS;
    vm_set_var(VM_Manager_Table_ADDRESS + 16, changeTmp3);
    vm_initManagerTable();
    vm_init_fixed_base_manager_directory();

    Global_R9 = Program_Data_Address;
    uc_reg_write(MTK, UC_ARM_REG_R9, &Global_R9);
    changeTmp2 = STACK_ADDRESS + 0x100000u;
    uc_reg_write(MTK, UC_ARM_REG_SP, &changeTmp2);
    lastAddress = 1;
}

static u32 vm_math_sqrt_result(u32 value)
{
    int32_t signedValue = (int32_t)value;
    if (signedValue <= 0)
        return vm_set_call_result(0);

    uint64_t target = (uint32_t)signedValue;
    uint32_t lo = 1;
    uint32_t hi = 46340;
    uint32_t result = 0;
    while (lo <= hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        uint64_t square = (uint64_t)mid * (uint64_t)mid;
        if (square <= target)
        {
            result = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return vm_set_call_result(result);
}

static int vm_math_df_sin_value(int deg)
{
    double rad = (double)deg * 3.14159265358979323846 / 180.0;
    return (int)(sin(rad) * 4096.0);
}

static u32 vm_math_df_sin_result(u32 deg)
{
    return vm_set_call_result((u32)vm_math_df_sin_value((int32_t)deg));
}

static u32 vm_math_df_cos_result(u32 deg)
{
    return vm_math_df_sin_result(deg + 90);
}

static u32 vm_math_df_degree_result(u32 xValue, u32 yValue)
{
    int32_t x = (int32_t)xValue;
    int32_t y = (int32_t)yValue;
    int64_t lenSq = (int64_t)x * (int64_t)x + (int64_t)y * (int64_t)y;
    int32_t scaledY = (int32_t)((uint32_t)y << 12);
    int32_t begin;
    int32_t end;

    if (lenSq > 0)
    {
        uint64_t target = (uint64_t)lenSq;
        uint64_t len64 = (uint64_t)sqrt((double)target);
        while ((len64 + 1) <= 3037000499ULL && (len64 + 1) * (len64 + 1) <= target)
            ++len64;
        while (len64 * len64 > target)
            --len64;
        uint32_t len = len64 > 0xffffffffu ? 0xffffffffu : (uint32_t)len64;
        if (len > 0)
            scaledY /= (int32_t)len;
    }

    if (y < 0)
    {
        if (x > 0)
        {
            begin = 271;
            end = 359;
        }
        else
        {
            begin = 181;
            end = 270;
        }
    }
    else if (x < 0)
    {
        begin = 91;
        end = 180;
    }
    else
    {
        begin = 0;
        end = 90;
    }

    for (int32_t deg = begin; deg <= end; ++deg)
    {
        int s = vm_math_df_sin_value(deg);
        if (end == 90 || end == 359)
        {
            if (s >= scaledY)
                return vm_set_call_result((u32)deg);
        }
        else if (s <= scaledY)
        {
            return vm_set_call_result((u32)deg);
        }
    }
    return vm_set_call_result(0);
}

static int16_t vm_math_low_i16(u32 value)
{
    return (int16_t)(value & 0xffffu);
}

static int16_t vm_math_high_i16(u32 value)
{
    return (int16_t)((value >> 16) & 0xffffu);
}

static u32 vm_math_df_collection_test_result(u32 a1, u32 a2, u32 a3, u32 a4)
{
    int result = (int)vm_math_low_i16(a1) + (int)vm_math_low_i16(a2) > (int)vm_math_low_i16(a3) &&
                 (int)vm_math_low_i16(a3) + (int)vm_math_low_i16(a4) > (int)vm_math_low_i16(a1) &&
                 (int)vm_math_high_i16(a1) + (int)vm_math_high_i16(a2) > (int)vm_math_high_i16(a3) &&
                 (int)vm_math_high_i16(a3) + (int)vm_math_high_i16(a4) > (int)vm_math_high_i16(a1);
    return vm_set_call_result((u32)result);
}

static u32 vm_math_df_swap_val_result(u32 ptrA, u32 ptrB)
{
    u16 a = 0;
    u16 b = 0;
    if (ptrA && ptrB)
    {
        uc_mem_read(MTK, ptrA, &a, sizeof(a));
        uc_mem_read(MTK, ptrB, &b, sizeof(b));
        uc_mem_write(MTK, ptrA, &b, sizeof(b));
        uc_mem_write(MTK, ptrB, &a, sizeof(a));
    }
    return vm_set_call_result(ptrA);
}

static u32 vm_math_pow_float_result(u32 baseBits, u32 expBits)
{
    float base;
    float exp;
    float result;
    u32 resultBits;

    memcpy(&base, &baseBits, sizeof(base));
    memcpy(&exp, &expBits, sizeof(exp));
    result = powf(base, exp);
    memcpy(&resultBits, &result, sizeof(resultBits));
    return vm_set_call_result(resultBits);
}

static u32 vm_math_rand_result(void)
{
    static int seeded = 0;
    if (!seeded)
    {
        srand((unsigned int)time(NULL) ^ (unsigned int)SDL_GetTicks());
        seeded = 1;
    }
    return vm_set_call_result((u32)rand());
}

static int vm_bytes_contains(const char *haystack, const unsigned char *needle, size_t needleLen)
{
    size_t hayLen;
    if (haystack == NULL || needle == NULL || needleLen == 0)
        return 0;
    hayLen = strlen(haystack);
    if (hayLen < needleLen)
        return 0;
    for (size_t i = 0; i + needleLen <= hayLen; ++i)
    {
        if (memcmp((const unsigned char *)haystack + i, needle, needleLen) == 0)
            return 1;
    }
    return 0;
}

static vm_lcd_rotation vm_lcd_auto_rotation_for_current_cbe(void)
{
    static const unsigned char angryUtf8[] = {
        0xe6, 0x84, 0xa4, 0xe6, 0x80, 0x92, 0xe7, 0x9a,
        0x84, 0xe5, 0xb0, 0x8f, 0xe9, 0xb8, 0x9f
    };
    static const unsigned char angryGbk[] = {
        0xb7, 0xdf, 0xc5, 0xad, 0xb5, 0xc4, 0xd0, 0xa1, 0xc4, 0xf1
    };
    static const unsigned char zombieUtf8[] = {
        0xe5, 0x83, 0xb5, 0xe5, 0xb0, 0xb8, 0xe5, 0x85, 0x88, 0xe7, 0x94, 0x9f
    };
    static const unsigned char zombieGbk[] = {
        0xbd, 0xa9, 0xca, 0xac, 0xcf, 0xc8, 0xc9, 0xfa
    };

    if (vm_bytes_contains(LOAD_CBE_PATH, angryUtf8, sizeof(angryUtf8)) ||
        vm_bytes_contains(LOAD_CBE_PATH, angryGbk, sizeof(angryGbk)) ||
        strstr(LOAD_CBE_PATH, "Angry") != NULL ||
        strstr(LOAD_CBE_PATH, "angry") != NULL ||
        vm_bytes_contains(LOAD_CBE_PATH, zombieUtf8, sizeof(zombieUtf8)) ||
        vm_bytes_contains(LOAD_CBE_PATH, zombieGbk, sizeof(zombieGbk)))
        return VM_LCD_ROTATE_90_CCW;
    return VM_LCD_ROTATE_0;
}

static int vm_lcd_parse_rotation(const char *text, vm_lcd_rotation *rotation, int *isAuto)
{
    if (text == NULL || *text == 0)
        return 0;
    if (isAuto)
        *isAuto = 0;

    if (vm_ascii_stricmp(text, "auto") == 0)
    {
        if (rotation)
            *rotation = vm_lcd_auto_rotation_for_current_cbe();
        if (isAuto)
            *isAuto = 1;
        return 1;
    }
    if (vm_ascii_stricmp(text, "0") == 0 ||
        vm_ascii_stricmp(text, "none") == 0 ||
        vm_ascii_stricmp(text, "portrait") == 0)
    {
        if (rotation)
            *rotation = VM_LCD_ROTATE_0;
        return 1;
    }
    if (vm_ascii_stricmp(text, "right") == 0 ||
        vm_ascii_stricmp(text, "cw") == 0 ||
        vm_ascii_stricmp(text, "90") == 0)
    {
        if (rotation)
            *rotation = VM_LCD_ROTATE_90_CW;
        return 1;
    }
    if (vm_ascii_stricmp(text, "left") == 0 ||
        vm_ascii_stricmp(text, "ccw") == 0 ||
        vm_ascii_stricmp(text, "270") == 0 ||
        vm_ascii_stricmp(text, "-90") == 0 ||
        vm_ascii_stricmp(text, "landscape") == 0)
    {
        if (rotation)
            *rotation = VM_LCD_ROTATE_90_CCW;
        return 1;
    }
    if (vm_ascii_stricmp(text, "180") == 0 ||
        vm_ascii_stricmp(text, "flip") == 0)
    {
        if (rotation)
            *rotation = VM_LCD_ROTATE_180;
        return 1;
    }
    return 0;
}

static void vm_lcd_init_rotation_config(int argc, char *args[])
{
    vm_lcd_rotation rotation = vm_lcd_auto_rotation_for_current_cbe();
    const char *source = "auto";
    const char *envRotate = getenv("CBE_LCD_ROTATE");
    int isAuto = 0;

    if (envRotate != NULL)
    {
        if (vm_lcd_parse_rotation(envRotate, &rotation, &isAuto))
            source = isAuto ? "env:auto" : "env";
        else
            printf("[warn][lcd] invalid CBE_LCD_ROTATE=%s\n", envRotate);
    }

    for (int i = 1; i < argc; ++i)
    {
        const char *value = NULL;
        if (strncmp(args[i], "--rotate=", 9) == 0)
            value = args[i] + 9;
        else if (strncmp(args[i], "--lcd-rotate=", 13) == 0)
            value = args[i] + 13;

        if (value != NULL)
        {
            if (vm_lcd_parse_rotation(value, &rotation, &isAuto))
                source = isAuto ? "arg:auto" : "arg";
            else
                printf("[warn][lcd] invalid rotate option=%s\n", value);
        }
    }

    LcdSetRotation(rotation);
    printf("[info][lcd] rotate=%s source=%s view=%dx%d window=%dx%d\n",
           LcdRotationName(LcdGetRotation()), source,
           LcdGetViewWidth(), LcdGetViewHeight(),
           LcdGetWindowWidth(), LcdGetWindowHeight());
}


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

static void vm_note_sce_load_entry_pc(u32 pc)
{
    if (pc != 0x010140EC)
        return;

    u32 ctx = 0;
    u32 namePtr = 0;
    u32 dp = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
    char name[96] = "-";
    uc_reg_read(MTK, UC_ARM_REG_R0, &ctx);
    uc_reg_read(MTK, UC_ARM_REG_R1, &namePtr);
    if (namePtr)
        vm_read_path_string(namePtr, name, sizeof(name));
    g_lastSceLoadCtx = ctx;
    g_lastSceLoadNamePtr = namePtr;
    snprintf(g_lastSceLoadName, sizeof(g_lastSceLoadName), "%s", name);
    if (!g_autotestEnabled)
        return;
    vm_autotest_note("sce_load_entry pc=%08x ctx=%08x name_ptr=%08x name=%s df_pkg=%08x current=%08x this=%08x depth=%u\n",
                     pc, ctx, namePtr, name, dp, vmAddedScreen, g_currentScreenThis, g_screenStackCount);
}

static void vm_trace_read_guest_string(u32 ptr, char *out, size_t outSize)
{
    u8 first = 0;
    if (outSize == 0)
        return;
    snprintf(out, outSize, "-");
    if (ptr == 0)
        return;
    if (uc_mem_read(MTK, ptr, &first, 1) != UC_ERR_OK)
        return;
    vm_read_path_string(ptr, out, outSize);
    if (out[0] == 0)
        snprintf(out, outSize, "-");
}

static void vm_note_castlevania_wpay_pc(u32 pc)
{
    const char *phase = NULL;
    u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, lr = 0;
    u32 payBase = Global_R9 ? Global_R9 + 0x2c44 : 0;
    u32 savedLenOrPtr = 0;
    u32 savedText = 0;
    u32 savedFlag = 0;
    char s0[64];
    char s1[64];

    if (pc == 0x051812DA)
        phase = "wpay_http_pay";
    else if (pc == 0x05183F7C)
        phase = "wpay_http_confirm";
    else if (pc == 0x05186D9E)
        phase = "wpay_sms";
    else if (pc == 0x010026D8)
        phase = "game_pay_entry";
    else if (pc == 0x01002712)
        phase = "game_pay_check";
    else if (pc == 0x0100271A)
        phase = "game_pay_save";
    else if (pc == 0x01002734)
        phase = "game_pay_clear";
    else if (pc == 0x01002746)
        phase = "game_pay_call";
    else
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (payBase)
    {
        savedLenOrPtr = vm_get_var(payBase + 8);
        savedText = vm_get_var(payBase + 0xc);
        savedFlag = vm_get_var_byte(payBase + 0x10);
    }
    vm_trace_read_guest_string(r0, s0, sizeof(s0));
    vm_trace_read_guest_string(r1, s1, sizeof(s1));

    printf("[info][wpay_trace] %s pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x saved8=%08x savedc=%08x saved10=%u s0=%s s1=%s\n",
           phase, pc, r0, r1, r2, r3, lr, savedLenOrPtr, savedText, savedFlag, s0, s1);
    if (g_autotestEnabled)
    {
        vm_autotest_note("wpay_trace %s pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x saved8=%08x savedc=%08x saved10=%u s0=%s s1=%s\n",
                         phase, pc, r0, r1, r2, r3, lr, savedLenOrPtr, savedText, savedFlag, s0, s1);
    }
}

static void vm_note_stream_data_result(const char *manager, u32 caller, u32 resPtr,
                                       u32 a2, u32 a3, u32 a4, u32 result)
{
    bool interesting = resPtr == 0 || result == 0 ||
                       (caller >= 0x01014100 && caller <= 0x01014120);
    if (!interesting)
        return;

    char resHead[64] = "-";
    char outHead[64] = "-";
    u32 outLen = a4 ? vm_get_var(a4) : 0;
    if (resPtr)
        vm_autotest_format_mem_hex(resPtr, 12, resHead, sizeof(resHead));
    if (result)
        vm_autotest_format_mem_hex(result, 12, outHead, sizeof(outHead));
    if (resPtr == 0 || result == 0)
    {
        printf("[warn][resource] stream_data_result manager=%s caller=%08x sce_ctx=%08x sce_name_ptr=%08x sce_name=%s res=%08x result=%08x a2=%08x a3=%08x a4=%08x out_len=%u df_pkg=%08x current=%08x this=%08x depth=%u\n",
               manager ? manager : "-",
               caller, g_lastSceLoadCtx, g_lastSceLoadNamePtr, g_lastSceLoadName,
               resPtr, result, a2, a3, a4, outLen,
               vm_get_var(VM_DreamFactory_DataPackage_ADDRESS),
               vmAddedScreen, g_currentScreenThis, g_screenStackCount);
    }
    if (g_autotestEnabled)
    {
        vm_autotest_note("stream_data_result manager=%s caller=%08x res=%08x a2=%08x a3=%08x a4=%08x out_len=%u result=%08x res_head=%s out_head=%s df_pkg=%08x current=%08x this=%08x depth=%u\n",
                         manager ? manager : "-",
                         caller, resPtr, a2, a3, a4, outLen, result,
                         resHead, outHead,
                         vm_get_var(VM_DreamFactory_DataPackage_ADDRESS),
                         vmAddedScreen, g_currentScreenThis, g_screenStackCount);
    }
}

static bool vm_host_cbe_sibling_file_exists(const char *name)
{
    char path[256];
    if (vm_host_file_exists(name))
        return true;
    if (name == NULL || name[0] == 0 || strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
        return false;
    snprintf(path, sizeof(path), "CBE/%s", name);
    return vm_host_file_exists(path);
}

static void vm_storage_read_name(u32 namePtr, char *out, size_t outSize)
{
    size_t pos = 0;
    if (outSize == 0)
        return;
    out[0] = 0;
    if (namePtr == 0)
        return;
    while (pos + 1 < outSize)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, namePtr + (u32)pos, &ch, 1) != UC_ERR_OK || ch == 0)
            break;
        out[pos++] = (char)ch;
    }
    out[pos] = 0;
}

static void vm_storage_date_build_path(char *path, size_t pathSize, u32 namePtr)
{
    char appName[96];
    char rawName[96];
    char storeName[96];
    vm_persist_sanitize_name(LOAD_CBE_PATH, appName, sizeof(appName));
    vm_storage_read_name(namePtr, rawName, sizeof(rawName));
    if (rawName[0] == 0)
        snprintf(rawName, sizeof(rawName), "ptr_%08x", namePtr);
    vm_persist_sanitize_name(rawName, storeName, sizeof(storeName));
    snprintf(path, pathSize, "nvram/%s_storage_%s.bin", appName, storeName);
}

static bool vm_storage_date_is_login_record(const char *name)
{
    return name != NULL && strcmp(name, "mmorpg_LoginRecord") == 0;
}

static u32 vm_storage_date(u32 namePtr, u32 buffer, u32 len, u32 isRead)
{
    if (buffer == 0 || len == 0 || len > 0x100000)
        return vm_set_call_result(0);

    char rawName[96];
    char path[224];
    vm_storage_read_name(namePtr, rawName, sizeof(rawName));
    vm_storage_date_build_path(path, sizeof(path), namePtr);
    bool isLoginRecord = vm_storage_date_is_login_record(rawName);
    u32 effectiveBuffer = buffer;
    u32 effectiveLen = len;

    if (!isRead && isLoginRecord && len == sizeof(u32))
    {
        u32 recordPtr = vm_get_var(buffer);
        if (vm_is_writable_vm_range(recordPtr, 180))
        {
            effectiveBuffer = recordPtr;
            effectiveLen = 180;
            printf("[info][storage] compat save %s slot=%08x ptr=%08x len=%u->%u\n",
                   rawName, buffer, recordPtr, len, effectiveLen);
        }
    }

    u8 *tmp = SDL_malloc(effectiveLen);
    if (tmp == NULL)
        return vm_set_call_result(0);

    if (isRead)
    {
        u32 zeroOffset = 0;
        while (zeroOffset < len)
        {
            u32 zeroLen = SDL_min(len - zeroOffset, (u32)sizeof(emptyBuff));
            uc_mem_write(MTK, buffer + zeroOffset, emptyBuff, zeroLen);
            zeroOffset += zeroLen;
        }

        u32 readLen = vm_persist_read_file(path, tmp, len);
        if (isLoginRecord && len >= 180 && readLen > 0 && readLen < 180)
        {
            printf("[warn][storage] ignore truncated %s len=%u path=%s\n", rawName, readLen, path);
            readLen = 0;
        }
        if (readLen)
            uc_mem_write(MTK, buffer, tmp, readLen);
        SDL_free(tmp);
        return vm_set_call_result(readLen ? 1 : 0);
    }

    if (uc_mem_read(MTK, effectiveBuffer, tmp, effectiveLen) != UC_ERR_OK)
    {
        SDL_free(tmp);
        return vm_set_call_result(0);
    }
    u32 savedLen = vm_persist_write_file(path, tmp, effectiveLen);
    SDL_free(tmp);
    return vm_set_call_result(savedLen == effectiveLen ? 1 : 0);
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
    return vm_set_call_result(0);
}

static u32 vm_sys_set_setting_profile(u32 profile)
{
    u8 value[4];
    memcpy(value, &profile, sizeof(profile));
    char path[160];
    vm_persist_build_path(path, sizeof(path), "sys_profile", 0);
    u32 savedLen = vm_persist_write_file(path, value, sizeof(value));
    return vm_set_call_result(0);
}

static u32 vm_sys_get_setting_profile(void)
{
    u32 profile = 0;
    char path[160];
    vm_persist_build_path(path, sizeof(path), "sys_profile", 0);
    u32 readLen = vm_persist_read_file(path, (u8 *)&profile, sizeof(profile));
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
    return vm_set_call_result(0);
}

void RunArmProgram(void *param)
{
    uc_err p;
    u32 startAddr = (u32)param;
    g_vmThreadFinished = 0;
#ifdef GDB_SERVER_SUPPORT
    gdbTarget.running = 1;
    gdbTarget.breakpoints[gdbTarget.num_breakpoints++] = startAddr;
    readAllCpuRegFunc = ReadRegsToGdb;
    gdb_readMemFunc = readMemoryToGdb;
#endif
    p = UC_ERR_OK;
    startAddr = (u32)param;
    // 准备工作
    // 写入屏幕缓存数据
    vm_lcd_init_screen_image_struct();
    // DF_DataPackage_SetFullPaths()
    // 当前运行的文件名
    utf8_to_gbk(g_cbeLoadPathUtf8, cbeTextString, mySizeOf(cbeTextString));
    uc_mem_write(MTK, VM_DF_DataPackage_FilePath_ADDRESS, cbeTextString, 64);
    // DF_DataPackage_SetFileLens();
    vm_set_var(VM_DF_DataPackage_In_File_Length_ADDRESS, g_cbeInfo.DF_DataPacakge_Size);
    // DF_DataPackage_SetFileOffset()
    vm_set_var(VM_DF_DataPackage_In_File_Offset_ADDRESS, g_cbeInfo.DF_Data_Pacakage_Offset);
    changeTmp1 = 1;
    uc_mem_write(MTK, VM_DF_DataPackage_LoadType_ADDRESS, &changeTmp1, 1);
    // 第一次入口初始化
    changeTmp1 = (g_cbeInfo.headerInt1 && !vm_cbe_uses_fixed_base_manager_abi())
                     ? (VM_NATIVE_DISPATCH_ADDRESS | 1)
                     : VM_Manager_Table_ADDRESS;
    uc_reg_write(MTK, UC_ARM_REG_R0, &changeTmp1); // 传入Manager函数表指针地址

    u32 exitAddr = PROGRAM_EXIT_ADDR;
    u32 thumbExitAddr = PROGRAM_EXIT_ADDR | 1;
    uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
    p = vm_emu_start(startAddr + 1, exitAddr);        // thumb模式
    if (p == UC_ERR_OK && (!g_cbeInfo.headerInt1 || vm_cbe_uses_fixed_base_manager_abi()))
    {
        g_appMainEntry = vm_get_var(VM_Manager_Table_ADDRESS);
        g_appExitEntry = vm_get_var(VM_Manager_Table_ADDRESS + 4);
        printf("[info][app] main=%08x exit=%08x\n", g_appMainEntry, g_appExitEntry);
        vm_autotest_note("app_entries main=%08x exit=%08x\n", g_appMainEntry, g_appExitEntry);
    }

    if (p == UC_ERR_OK && g_cbeInfo.headerInt1 && !vm_cbe_uses_fixed_base_manager_abi())
    {
        /* The native logic callback performs a one-shot interface bootstrap on
         * its first invocation and deliberately returns before game logic. */
        if (g_nativeAppParserEntry)
        {
            uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
            p = vm_emu_start(g_nativeAppParserEntry, exitAddr);
        }
        if (g_nativeAppInitEntry)
        {
            changeTmp1 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &changeTmp1);
            uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
            if (p == UC_ERR_OK)
                p = vm_emu_start(g_nativeAppInitEntry, exitAddr);
        }
        while (p == UC_ERR_OK)
        {
            p = scheduler_tick();
            if (p != UC_ERR_OK)
                break;
            if (g_hostQuitRequested)
            {
                p = vm_run_host_quit_cleanup(exitAddr, thumbExitAddr);
                break;
            }
            if (g_nativeAppParserEntry)
            {
                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                p = vm_emu_start(g_nativeAppParserEntry, exitAddr);
                if (p != UC_ERR_OK)
                    break;
            }
            vm_lcd_update_with_input_overlay();
            vm_frame_delay(50);
        }
        if (p != UC_ERR_OK)
            printf("native app loop异常:%s\n", uc_strerror(p));
        g_vmThreadFinished = 1;
        return;
    }

    // 第二次初始化
    if (p == UC_ERR_OK)
    {
        startAddr = g_appMainEntry ? g_appMainEntry : vm_get_var(VM_Manager_Table_ADDRESS);
        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
        p = vm_emu_start(startAddr, exitAddr);
    }

    if (p == UC_ERR_OK)
    {
        if (screenStructChange != 1)
        {
            if (vmAddedScreen == 0)
            {
                printf("[info][screen] second init did not set an active screen, entering idle screen state\n");
                g_screenRemovedWithoutNext = 1;
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
                if (g_hostQuitRequested)
                {
                    p = vm_run_host_quit_cleanup(exitAddr, thumbExitAddr);
                    break;
                }
                if (screenStructChange == 1 || g_screenRemovedWithoutNext || vmAddedScreen == 0)
                    break;
                if (tScreenInitedPtr != vmAddedScreen)
                {
                    tScreenInitEntry = vm_get_var(vmAddedScreen);
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
                    }
                    tScreenInitedPtr = vmAddedScreen;
                    if (screenStructChange == 1 || g_screenRemovedWithoutNext || vmAddedScreen == 0)
                        break;
                }
                scheduler_normalize_startup_screen_state();
                tScreenEventEntry = vm_get_var(vmAddedScreen + 0x08);
                tScreenRenderEntry = vm_get_var(vmAddedScreen + 0x0c);
                tScreenResourceLoadEntry = vm_get_var(vmAddedScreen + 0x18);
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
                if (screenStructChange == 1 || g_screenRemovedWithoutNext || vmAddedScreen == 0)
                    break;
                if (vmAddedScreen != screenBeforeCallback)
                    continue;
                if (screenStructNotifyLoadRes == 1)
                {
                    screenStructNotifyLoadRes = 0;
                    if (tScreenResourceLoadEntry)
                    {
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
                if (screenStructChange == 1 || g_screenRemovedWithoutNext || vmAddedScreen == 0)
                    break;
                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                uc_reg_write(MTK, UC_ARM_REG_R0, &vmAddedScreen);
                p = vm_emu_start(tScreenRenderEntry, exitAddr);
                if (p != UC_ERR_OK)
                    break;
                vm_lcd_update_with_input_overlay();
                vm_frame_delay(50);
            }
        }
        while (p == UC_ERR_OK && !g_hostQuitCleanupStarted)
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
            if (g_hostQuitRequested)
            {
                p = vm_run_host_quit_cleanup(exitAddr, thumbExitAddr);
                break;
            }
            while (p == UC_ERR_OK && screenStructChange != 1 && g_screenRemovedWithoutNext)
            {
                p = scheduler_tick();
                if (p != UC_ERR_OK)
                    break;
                if (g_hostQuitRequested)
                {
                    p = vm_run_host_quit_cleanup(exitAddr, thumbExitAddr);
                    break;
                }
                if ((g_schedulerTick % 30) == 0)
                {
                    u8 waitUpdateState = 0;
                    u32 waitNet28 = 0;
                    u32 waitNet30 = 0;
                    u32 waitNextScreen = 0;
                    uc_mem_read(MTK, Global_R9 + 0x4cb6, &waitUpdateState, 1);
                    waitNet28 = vm_get_var(Global_R9 + 0x9588 + 0x28);
                    waitNet30 = vm_get_var(Global_R9 + 0x9588 + 0x30);
                    waitNextScreen = vm_get_var(VM_SCREEN_nextSubTScreen_ADDRESS);
                }
                vm_frame_delay(50);
            }
            if (p != UC_ERR_OK)
                break;
            if (g_hostQuitCleanupStarted)
                break;
            if (screenStructChange != 1)
                continue;
            if (screenStructChange == 1)
            {
                screenFuncPtr = vm_get_var(VM_SCREEN_nextSubTScreen_ADDRESS); // 得到screen函数表地址的指针
                if (screenFuncPtr == 0)
                {
                    printf("[info][screen] screen change without next screen, keep idle state\n");
                    screenStructChange = 0;
                    g_screenRemovedWithoutNext = 1;
                    vmAddedScreen = 0;
                    g_screenResumeExisting = 0;
                    g_screenEnterExistingNoCallback = 0;
                    g_currentScreenThis = 0;
                    g_currentScreenModuleBase = 0;
                    g_currentScreenDataPackage = 0;
                    continue;
                }
                u32 screenModuleBase = vm_screen_stack_lookup_module_base(screenFuncPtr);
                screenThisPtr = vm_screen_default_call_param(screenFuncPtr);
                if (screenThisPtr == 0)
                    screenThisPtr = vm_screen_stack_lookup_param(screenFuncPtr);
                if (screenModuleBase)
                    g_currentScreenModuleBase = screenModuleBase;
                screenInitEntry = vm_get_var(screenFuncPtr);
                screenDestoryEntry = vm_get_var(screenFuncPtr + 4);
                screenLogicEntry = vm_get_var(screenFuncPtr + 8);
                screenRenderEntry = vm_get_var(screenFuncPtr + 12);
                screenPauseEntry = vm_get_var(screenFuncPtr + 16);
                screenRemuseEntry = vm_get_var(screenFuncPtr + 20);
                screenResouceLoadEntry = vm_get_var(screenFuncPtr + 24);
                printf("[SCR_FUNC](init:%x,destory:%x,logic:%x,render:%x,pause:%x,remuse:%x,resLoad:%x)\n", screenInitEntry, screenDestoryEntry, screenLogicEntry, screenRenderEntry, screenPauseEntry, screenRemuseEntry, screenResouceLoadEntry);
                screenStructChange = 0;
                g_activeScreenRemovedThisFrame = 0;
            }

            uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
            scheduler_prepare_screen_call(screenThisPtr);
            uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
            if (g_screenResumeExisting)
            {
                if (screenRemuseEntry)
                    p = vm_emu_start(screenRemuseEntry, exitAddr);
                else
                    p = UC_ERR_OK;
                printf("ScreenResume Ok\n");
                g_screenResumeExisting = 0;
            }
            else if (g_screenEnterExistingNoCallback)
            {
                p = UC_ERR_OK;
                printf("ScreenResume skipped by isInQuit\n");
                g_screenEnterExistingNoCallback = 0;
            }
            else
            {
                if (screenInitEntry)
                    p = vm_emu_start(screenInitEntry, exitAddr);
                else
                    p = UC_ERR_OK;
                vm_autotest_note("screen_run kind=init caller=%08x this=%08x init=%08x logic=%08x render=%08x\n",
                                 lastAddress, screenThisPtr, screenInitEntry, screenLogicEntry, screenRenderEntry);
                printf("ScreenInit Ok\n");
            }
            if (p == UC_ERR_OK)
                scheduler_note_screen_data_package(screenThisPtr);
            if (p == UC_ERR_OK)
            {
                u32 _frameTick = SDL_GetTicks();
                while (true)
                {
                    bool clearTransientInputBeforeIdle = false;
                    p = scheduler_tick();
                    if (p != UC_ERR_OK)
                        break;
                    if (g_hostQuitRequested)
                    {
                        p = vm_run_host_quit_cleanup(exitAddr, thumbExitAddr);
                        break;
                    }
                    if (screenStructChange == 1)
                        break;
                    if (g_screenRemovedWithoutNext)
                        break;
                    if (screenStructNotifyLoadRes == 1)
                    {
                        screenStructNotifyLoadRes = 0;
                        u32 resScreen = g_screenLoadResourcePendingScreen ? g_screenLoadResourcePendingScreen : screenFuncPtr;
                        u32 resParam = g_screenLoadResourcePendingParam ? g_screenLoadResourcePendingParam : screenThisPtr;
                        u32 resEntry = resScreen == screenFuncPtr ? screenResouceLoadEntry : vm_get_var(resScreen + 24);
                        g_screenLoadResourcePendingScreen = 0;
                        g_screenLoadResourcePendingParam = 0;
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
                        scheduler_prepare_screen_call(resParam);
                        uc_reg_write(MTK, UC_ARM_REG_R0, &resParam);
                        if (resEntry)
                        {
                            p = vm_emu_start(resEntry, exitAddr);
                            if (p != UC_ERR_OK)
                            {
                                printf("SCR_ResourceLoad异常:%s\n", uc_strerror(p));
                                assert(0);
                            }
                            scheduler_note_screen_data_package(resParam);
                        }
                        else
                        {
                        }
                    }
                    if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                        break;
                    simulateKey = 0;
                    simulatePress = 0;
                    vm_clear_key_down_state();
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
                                vm_note_key_state_event(evt->r0, evt->r1);
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
                            if (screenThisPtr && screenLogicEntry)
                            {
                                u32 eventType = 0;
                                u32 eventArg = 0;
                                if (evt->event == VM_EVENT_KEYBOARD)
                                {
                                    u32 keyMask = vm_key_mask_from_code(evt->r0);
                                    eventType = evt->r1 ? 0 : 1;
                                    eventArg = vm_malloc_var();
                                    vm_set_var(eventArg, keyMask);
                                }
                                else
                                {
                                    eventType = evt->r0 == MR_MOUSE_UP ? 4 : (evt->r0 == MR_MOUSE_MOVE ? 5 : 3);
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
                                clearTransientInputBeforeIdle = true;
                                scheduler_note_screen_data_package(screenThisPtr);
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
                    if (clearTransientInputBeforeIdle)
                    {
                        /* Event logic already saw these one-shot flags; keep hold state for idle. */
                        vm_clear_key_down_state();
                        simulateTouchDown = 0;
                        simulateTouchUp = 0;
                        simulateTouchDrag = 0;
                    }
                    if (1)
                    {
                        if (screenLogicEntry && !vm_is_pool_entry(screenLogicEntry))
                        {
                            u32 idleEventType = 6;
                            u32 idleEventArg = 0;
                            uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                            scheduler_prepare_screen_call(screenThisPtr);
                            uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                            uc_reg_write(MTK, UC_ARM_REG_R1, &idleEventType);
                            uc_reg_write(MTK, UC_ARM_REG_R2, &idleEventArg);
                            p = vm_emu_start(screenLogicEntry, exitAddr);
                            if (p != UC_ERR_OK)
                            {
                                printf("SCR_Logic异常:%s\n", uc_strerror(p));
                                assert(0);
                            }
                            scheduler_note_screen_data_package(screenThisPtr);
                            p = scheduler_flush_post_vm_business_send_ready("screen_logic");
                            if (p != UC_ERR_OK)
                                break;
                            if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                                break;
                        }

                        if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                            break;
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                        scheduler_prepare_screen_call(screenThisPtr);
                        uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                        p = vm_emu_start(screenRenderEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            dumpCpuInfo();
                            printf("SCR_Render异常:%s\n", uc_strerror(p));
                            assert(0);
                        }
                        scheduler_note_screen_data_package(screenThisPtr);
                        if (screenStructChange == 1 || g_screenRemovedWithoutNext)
                            break;
                    }
                    vm_lcd_update_with_input_overlay();
                    u32 _now = SDL_GetTicks();
                    u32 _elapsed = _now - _frameTick;
                    if (_elapsed < 100)
                        SDL_Delay(100 - _elapsed);
                    _frameTick = SDL_GetTicks();
                }
                if (g_hostQuitCleanupStarted)
                    break;
                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                scheduler_prepare_screen_call(screenThisPtr);
                uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                if (g_activeScreenRemovedThisFrame)
                {
                    u32 removedThis = g_activeScreenRemovedThis ? g_activeScreenRemovedThis : screenThisPtr;
                    u32 removedModuleBase = g_activeScreenRemovedModuleBase;
                    u32 removedDataPackage = g_activeScreenRemovedDataPackage;
                    g_activeScreenRemovedThisFrame = 0;
                    g_activeScreenRemovedThis = 0;
                    g_activeScreenRemovedModuleBase = 0;
                    g_activeScreenRemovedDataPackage = 0;
                    if (screenDestoryEntry)
                    {
                        u32 savedScreenThis = g_currentScreenThis;
                        u32 savedModuleBase = g_currentScreenModuleBase;
                        u32 savedDataPackage = g_currentScreenDataPackage;
                        u32 savedGlobalDataPackage = vm_current_data_package();
                        g_currentScreenThis = removedThis;
                        if (removedModuleBase)
                            g_currentScreenModuleBase = removedModuleBase;
                        if (removedDataPackage)
                        {
                            u32 oldDataPackage = g_currentScreenDataPackage;
                            g_currentScreenDataPackage = removedDataPackage;
                            vm_restore_data_package(removedDataPackage);
                            vm_trace_screen_data_package_change("destroy-removed", removedThis + 0x18,
                                                                oldDataPackage, removedDataPackage, vm_current_data_package());
                        }
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                        uc_reg_write(MTK, UC_ARM_REG_R0, &removedThis);
                        p = vm_emu_start(screenDestoryEntry, exitAddr);
                        g_currentScreenThis = savedScreenThis;
                        vm_trace_screen_data_package_change("destroy-restore", removedThis + 0x18,
                                                            g_currentScreenDataPackage, savedDataPackage, savedGlobalDataPackage);
                        g_currentScreenModuleBase = savedModuleBase;
                        g_currentScreenDataPackage = savedDataPackage;
                        vm_restore_data_package(savedGlobalDataPackage);
                        if (p != UC_ERR_OK)
                        {
                            printf("SCR_Destory异常\n");
                            break;
                        }
                    }
                }
                else if (g_screenExitMode == VM_SCREEN_EXIT_SKIP)
                {
                }
                else if (g_screenExitMode == VM_SCREEN_EXIT_PAUSE)
                {
                    if (screenPauseEntry)
                    {
                        p = vm_emu_start(screenPauseEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            printf("SCR_Pause异常\n");
                            break;
                        }
                    }
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
                    }
                }
                g_screenExitMode = VM_SCREEN_EXIT_DESTROY;
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
    g_vmThreadFinished = 1;
    if (p != UC_ERR_OK)
        assert(0);
#ifdef GDB_SERVER_SUPPORT
    send_gdb_response(&clients[0], "S01");
#endif
}

#ifdef CBE_PLATFORM_ANDROID
static int g_cbeInitialized = 0;
static volatile int g_cbeRunning = 0;

int cbeInit(const char *rootPath)
#else
int main(int argc, char *args[])
#endif
{
#ifdef CBE_SERVER_ONLY
    const char *resourceRoot = getenv("CBE_RESOURCE_ROOT");
    char originalCwd[1024];
    char resourceCandidate[1200];
    bool resourceReady = false;

    /* Linux is a standalone authoritative service.  Do not load a client CBE
     * or initialize the Unicorn/LCD emulator before opening the game and web
     * listeners. */
    setvbuf(stdout, NULL, _IOFBF, 262144);
    setvbuf(stderr, NULL, _IONBF, 0);
    originalCwd[0] = 0;
    resourceCandidate[0] = 0;
    (void)getcwd(originalCwd, sizeof(originalCwd));
    for (int i = 1; i < argc; ++i)
    {
        if (strncmp(args[i], "--resource-root=", 16) == 0 && args[i][16] != 0)
            resourceRoot = args[i] + 16;
    }
    if (resourceRoot != NULL && resourceRoot[0] != 0)
    {
        static const char *suffixes[] = {
            "", "/web/fs/JHOnlineData", "/JHOnlineData", "/bin/JHOnlineData"
        };
        for (u32 i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
        {
            snprintf(resourceCandidate, sizeof(resourceCandidate), "%s%s",
                     resourceRoot, suffixes[i]);
            if (vm_net_mock_set_resource_dir(resourceCandidate))
            {
                resourceReady = true;
                break;
            }
        }
    }
    else
    {
        static const char *relativeCandidates[] = {
            "web/fs/JHOnlineData", "../web/fs/JHOnlineData",
            "JHOnlineData", "bin/JHOnlineData", "../bin/JHOnlineData"
        };
        for (u32 i = 0;
             i < sizeof(relativeCandidates) / sizeof(relativeCandidates[0]); ++i)
        {
            if (vm_net_mock_set_resource_dir(relativeCandidates[i]))
            {
                resourceReady = true;
                break;
            }
        }
#ifndef _WIN32
        if (!resourceReady)
        {
            char exePath[1024];
            ssize_t exeLen = readlink("/proc/self/exe", exePath,
                                      sizeof(exePath) - 1);
            if (exeLen > 0)
            {
                char *slash = NULL;
                exePath[exeLen] = 0;
                slash = strrchr(exePath, '/');
                if (slash != NULL)
                {
                    static const char *exeSuffixes[] = {
                        "/JHOnlineData", "/../web/fs/JHOnlineData",
                        "/../bin/JHOnlineData"
                    };
                    *slash = 0;
                    for (u32 i = 0;
                         i < sizeof(exeSuffixes) / sizeof(exeSuffixes[0]); ++i)
                    {
                        snprintf(resourceCandidate, sizeof(resourceCandidate),
                                 "%s%s", exePath, exeSuffixes[i]);
                        if (vm_net_mock_set_resource_dir(resourceCandidate))
                        {
                            resourceReady = true;
                            break;
                        }
                    }
                }
            }
        }
#endif
    }
    if (!resourceReady)
    {
        printf("[error][mock-service] resource root unresolved cwd=%s "
               "required=JHOnlineData/task.dsh "
               "hint=CBE_RESOURCE_ROOT-may-point-to-project-root-or-JHOnlineData\n",
               originalCwd[0] ? originalCwd : "<unknown>");
        return -1;
    }
    printf("[info][mock-service] resource_root=%s source=%s\n",
           vm_net_mock_resource_dir(),
           resourceRoot && resourceRoot[0] ? "configured" : "auto");
    vm_mock_service_init_config(argc, args);
    printf("[info][mock-service] starting server-only bind=%s:%u admin=%s:%u\n",
           g_mockServiceBindHost, g_mockServicePort,
           g_mockAdminBindHost, g_mockAdminPort);
    return vm_net_mock_service_run_forever(g_mockServiceBindHost,
                                           g_mockServicePort);
#else
    uc_err err;
    uc_hook hookHandle;
    bool bufferMockServiceStdout = false;
#ifdef CBE_PLATFORM_ANDROID
    int argc = 0;
    char **args = NULL;
    if (g_cbeInitialized)
        return 0;
    if (rootPath != NULL && rootPath[0] != 0 && chdir(rootPath) != 0)
    {
        printf("[error][android] failed to chdir root=%s\n", rootPath);
        return -1;
    }
#endif
    /* Windows mock-service-only used to make every trace line an immediate
     * filesystem write.  The first scene response emits many evidence lines;
     * synchronous writes delayed the NPC catalog and welcome message by
     * several seconds.  Buffer only the standalone service process and flush
     * after each response has already been sent.  Keep the emulator process
     * unbuffered so crash diagnostics remain immediate. */
#ifndef CBE_PLATFORM_ANDROID
    {
        const char *serviceOnlyEnv = getenv("CBE_MOCK_SERVICE_ONLY");
        bufferMockServiceStdout = serviceOnlyEnv != NULL &&
                                  serviceOnlyEnv[0] != 0 &&
                                  strcmp(serviceOnlyEnv, "0") != 0;
        for (int i = 1; i < argc && !bufferMockServiceStdout; ++i)
        {
            if (strcmp(args[i], "--mock-service-only") == 0)
                bufferMockServiceStdout = true;
        }
    }
#endif
    if (bufferMockServiceStdout)
        setvbuf(stdout, NULL, _IOFBF, 262144);
    else
        setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    vm_cbe_init_config(argc, args);
    vm_autotest_init(argc, args);
    vm_mock_service_init_config(argc, args);
    if (!g_mockServiceOnly)
        vm_lcd_init_rotation_config(argc, args);

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // while(1);

#ifndef CBE_PLATFORM_NO_WINDOW
    if (SDL_Init((g_mockServiceOnly ? SDL_INIT_TIMER : (SDL_INIT_VIDEO | SDL_INIT_TIMER))) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }
    /* 勿用 SDL_WINDOW_OPENGL：本工程用 GetWindowSurface 直接写像素，OpenGL 窗口下格式/stride 常异常 → 竖条花屏 */
    if (!g_mockServiceOnly)
    {
#ifdef GDI_LAYER_DEBUGf
        window = SDL_CreateWindow("moral i9 simulato", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH * 5, SCREEN_HEIGHT, 0);
#else
        window = SDL_CreateWindow("Cbe Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  LcdGetWindowWidth(), LcdGetWindowHeight(), 0);
#endif
        if (window == NULL)
        {
            printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
            return -1;
        }
        LcdApplyWindowSize();
        SDL_Surface *startupSurface = SDL_GetWindowSurface(window);
        if (startupSurface)
        {
            SDL_FillRect(startupSurface, NULL, SDL_MapRGB(startupSurface->format, 0, 0, 0));
            SDL_UpdateWindowSurface(window);
        }
    }
#endif

    InitVmEvent();

#ifdef CBE_HOST_UTF8_PATHS
    snprintf((char *)cbeTextString, mySizeOf(cbeTextString), "%s", g_cbeLoadPathUtf8);
#else
    utf8_to_gbk(g_cbeLoadPathUtf8, cbeTextString, mySizeOf(cbeTextString));
#endif
#ifndef CBE_PLATFORM_ANDROID
    if (!vm_host_file_exists((char *)cbeTextString))
    {
        char altPath[128];
        snprintf(altPath, sizeof(altPath), "bin/%s", cbeTextString);
        if (vm_host_file_exists(altPath))
        {
            chdir("bin");
            printf("[info][host] runtime cwd adjusted to ./bin\n");
        }
    }
#endif
    char *fileBuffer = readFile(cbeTextString, &changeTmp1);
    if (fileBuffer == NULL || changeTmp1 == 0)
    {
        printf("[error][cbe] failed to load %s\n", cbeTextString);
        return -1;
    }
    g_cbeFileBuffer = (u8 *)fileBuffer;
    g_cbeFileSize = changeTmp1;
    // 分析前150字节
    parseCbeHeader(fileBuffer, changeTmp1);
    vm_config_program_mapping();

    if (g_cbeInfo.isBiggianProgram)
        err = uc_open(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN, &MTK);
    else
        err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &MTK);

    if (err)
    {
        printf("Failed on uc_open() with error returned: %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    ROM_MEMPOOL = SDL_malloc(Program_ROM_Mapped_Size);
    STACK_MEMPOOL = SDL_malloc(size_4mb);
    PRAM_MEMPOOL = SDL_malloc(size_1mb);
    RAM_MEMPOOL = SDL_malloc(VM_MEMPOOL_TOTAL_SIZE);
    if (ROM_MEMPOOL)
        memset(ROM_MEMPOOL, 0, Program_ROM_Mapped_Size);

    err = uc_mem_map_ptr(MTK, Program_ROM_Address, Program_ROM_Mapped_Size, UC_PROT_ALL, ROM_MEMPOOL);
    err = uc_mem_map_ptr(MTK, STACK_ADDRESS, size_1mb, UC_PROT_ALL, STACK_MEMPOOL);
    err = uc_mem_map_ptr(MTK, VM_Manager_Table_ADDRESS, size_1mb, UC_PROT_ALL, PRAM_MEMPOOL);
    err = uc_mem_map_ptr(MTK, VM_FUNC_HK_TABLE_ADDRESS, size_1mb, UC_PROT_ALL, SDL_malloc(size_1mb));
    uc_mem_map_ptr(MTK, VM_Memory_Pool_ADDRESS, VM_MEMPOOL_TOTAL_SIZE, UC_PROT_ALL, RAM_MEMPOOL);
    uc_mem_map(MTK, PROGRAM_EXIT_ADDR, 0x1000, UC_PROT_ALL);

    InitVmMalloc();
    if (!g_mockServiceOnly)
    {
        InitLcd();
        vm_lcd_update_with_input_overlay();
        InitFontEngine();
    }

    if (err)
    {
        printf("Failed mem  Rom map: %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_CODE, hookCodeCallBack, 0, 0, 0xFFFFFFFF);
    if (err == UC_ERR_OK)
        err = add_manager_code_hooks(MTK);
    //    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_BLOCK, hookBlockCallBack, 0, 0, 0xFFFFFFFF);

    uc_hook_add(MTK, &hookHandle, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, hookRamCallBack, 0, 0, 0xFFFFFFFF);

    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_MEM_READ_UNMAPPED, hookRamErrorBack, 2, 0, 0xFFFFFFFF);
    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_MEM_WRITE_UNMAPPED, hookRamErrorBack, 3, 0, 0xFFFFFFFF);
    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_MEM_FETCH_UNMAPPED, hookRamErrorBack, 4, 0, 0xFFFFFFFF);

    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_INTR, hookCpuIntr, NULL, 1, 0);

    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_INSN_INVALID, hookInsnInvalid, 4, 0, 0xFFFFFFFF);

    if (err != UC_ERR_OK)
    {
        printf("add hook err %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    if (MTK != NULL)
    {
        // 写入code段
        uc_mem_write(MTK, Program_ROM_Address, fileBuffer + g_cbeInfo.codeOffset, g_cbeInfo.codeLen);

        printf("File Entry Point:0x%x loadBase:0x%x\n", g_cbeInfo.codeOffset, Program_ROM_Address);
        // 数据段起始位置放这里
        // codeSize = headerInt2 + headerInt4
        uc_mem_write(MTK, Program_Data_Address, fileBuffer + g_cbeInfo.BssDataOffset, g_cbeInfo.BssDataLen);
        printf("Data In Rom Address:0x%x - 0x%x\n", Program_Data_Address, Program_Data_Address + g_cbeInfo.headerInt4);

        changeTmp3 = VM_MANAGER_TABLE_ADDRESS;
        vm_set_var(VM_Manager_Table_ADDRESS + 8, changeTmp3); // vmManager函数表地址
        changeTmp3 = VM_LOG_NOOP_ADDRESS;
        vm_set_var(VM_Manager_Table_ADDRESS + 12, changeTmp3);
        changeTmp3 = VM_CURR_APP_INFO_ADDRESS;
        vm_set_var(VM_Manager_Table_ADDRESS + 16, changeTmp3); // vcurAppFileName全局变量地址

        vm_initManagerTable();
        vm_init_fixed_base_manager_directory();

        Global_R9 = Program_Data_Address;
        uc_reg_write(MTK, UC_ARM_REG_R9, &Global_R9); // r9写入数据段地址

        changeTmp2 = STACK_ADDRESS + size_1mb; // 映射栈内存
        uc_reg_write(MTK, UC_ARM_REG_SP, &changeTmp2);

#ifndef CBE_CLIENT_ONLY
        if (g_mockServiceOnly)
        {
            printf("[info][mock-service] loaded cbe=%s entry=%08x data=%08x port=%u admin_port=%u\n",
                    g_cbeLoadPathUtf8, Program_ROM_Address, Program_Data_Address,
                    g_mockServicePort, g_mockAdminPort);
            vm_net_mock_service_run_forever(g_mockServiceBindHost, g_mockServicePort);
            return 0;
        }
#endif

        changeTmp1 = Program_ROM_Address;

#ifdef CBE_PLATFORM_ANDROID
        g_cbeInitialized = 1;
        printf("Unicorn Engine Initialized\n");
#else
        // 启动emu线程
        pthread_create(&EmuThread, NULL, RunArmProgram, changeTmp1);
#ifdef GDB_SERVER_SUPPORT
        pthread_create(&gdb_server_mutex, NULL, gdb_server_main, NULL);
#endif
        printf("Unicorn Engine Initialized\n");

        loop();
        vm_net_mock_async_shutdown();
        vm_net_mock_service_notify_disconnect("host-loop-exit");
#endif
    }
    return 0;
#endif
}

#ifdef CBE_PLATFORM_ANDROID
int cbeRun(void)
{
    if (!g_cbeInitialized || g_cbeRunning)
        return -1;
    g_cbeRunning = 1;
    RunArmProgram((void *)(uintptr_t)Program_ROM_Address);
    g_cbeRunning = 0;
    return 0;
}

void cbeTaskListRun(void)
{
    if (!g_cbeInitialized)
        return;
    loop();
    vm_net_mock_async_shutdown();
    vm_net_mock_service_notify_disconnect("android-loop-exit");
}

void cbeShutdown(void)
{
    if (!g_cbeInitialized)
        return;
    vm_request_host_quit("android-destroy");
    EnqueueVMEvent(VM_EVENT_EXIT, 0, 0);
    g_vmInputSdlTextInputWanted = 0;
}

const char *cbeGetCpuInfoText(void)
{
    static char info[512];
    u32 r[10] = {0};
    u32 sp = 0;
    u32 pc = 0;
    u32 lr = 0;
    u32 cpsr = 0;
    if (MTK == NULL)
    {
        snprintf(info, sizeof(info), "CBE emulator is not initialized");
        return info;
    }
    uc_reg_read(MTK, UC_ARM_REG_R0, &r[0]);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r[1]);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r[2]);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r[3]);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r[4]);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r[5]);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r[6]);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r[7]);
    uc_reg_read(MTK, UC_ARM_REG_R8, &r[8]);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r[9]);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_CPSR, &cpsr);
    snprintf(info, sizeof(info),
             "r0:%x r1:%x r2:%x r3:%x r4:%x r5:%x r6:%x r7:%x r8:%x r9:%x\n"
             "sp:%x cpsr:%x thumb:%x mode:%x lr:%x pc:%x lastPc:%x irq:%x",
             r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9],
             sp, cpsr, (cpsr & 0x20) != 0, cpsr & 0x1f, lr, pc,
             lastAddress, irq_nested_count);
    return info;
}
#endif

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
            vm_set_var(tmp1 + tmp2 * 4, tmp3);
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
        vm_configManagerTableCount(tmp1, VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS, 12);
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
        if (tmp1)
        {
            for (tmp2 = 0; tmp2 < 43; tmp2++)
            {
                tmp3 = VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS + tmp2 * 4;
                vm_set_var(tmp1 + tmp2 * 4, tmp3);
            }
        }
        vm_set_call_result(0);
    }
    else if (idx == 15)
    {
        tmp1 = VM_MANAGER_NETWORK_TABLE_ADDRESS;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
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
            vm_set_var(tmp1 + 8 * 4, tmp3);
            tmp3 = VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS + 10 * 4;
            vm_set_var(tmp1 + 10 * 4, tmp3);
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
            vm_set_var(tmp1 + 60 * 4, tmp3);
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
            vm_set_var(tmp1 + tmp2 * 4, tmp3);
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
            for (tmp2 = 0; tmp2 < 52; tmp2++)
            {
                tmp3 = VM_MANAGER_FUNC_LIST_ADDRESS + tmp2 * 4;
                vm_set_var(tmp1 + tmp2 * 4, tmp3);
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

static bool hook_vm_native_dispatch_func(u32 address)
{
    if (((u32)address & ~1u) != VM_NATIVE_DISPATCH_ADDRESS)
        return false;

    u32 id = 0;
    u32 arg = 0;
    u32 lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_R0, &id);
    uc_reg_read(MTK, UC_ARM_REG_R1, &arg);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (g_nativeDispatchTraceCount < 96)
    {
        u32 r2 = 0;
        u32 r3 = 0;
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        printf("[trace][native-dispatch] n=%u id=%x arg=%x r2=%x r3=%x lr=%x\n",
               g_nativeDispatchTraceCount++, id, arg, r2, r3, lr);
    }

    if ((id >= Program_ROM_Address && id < Program_ROM_Address + Program_ROM_Mapped_Size) ||
        (id >= Program_Data_Address && id < Program_Data_Address + g_cbeInfo.headerInt4) ||
        (id >= VM_Memory_Pool_ADDRESS && id < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE))
    {
        vm_set_call_result(0);
    }
    else if (id == 0x79e)
    {
        if (arg)
        {
            /* Native ABI registers {logic, init, dispatcher}.  The second
             * callback runs once; the first callback is consumed per tick. */
            g_nativeAppParserEntry = vm_get_var(arg);
            g_nativeAppInitEntry = vm_get_var(arg + 4);
            vm_set_var(arg + 8, VM_NATIVE_DISPATCH_ADDRESS | 1);
        }
        vm_set_call_result(VM_NATIVE_DISPATCH_ADDRESS | 1);
    }
    else if (id == 0x52)
    {
        if (arg && g_cbeInfo.headerInt1)
        {
            /* Native app-object registration; this CBE reads the slot immediately
             * after the call to patch its init/parse callbacks. */
            vm_set_var(Program_Data_Address + 0x1724, arg);
        }
        vm_set_call_result(0);
    }
    else if (id == 0x8f || id == 0x8e || id == 0x97 || id == 0xac || id == 0x421)
    {
        vm_set_call_result(id);
    }
    else if (id == 0xb7 || id == 0xb8)
    {
        vm_set_call_result(0);
    }
    else if (id == 0x67 || id == 0x6b || id == 0x6e)
    {
        vm_set_call_result(0);
    }
    else if (id == 0x3ed)
    {
        if (arg)
            vm_set_var_byte(arg, 0);
        vm_set_call_result(0);
    }
    else if (id == 0x3ec || id == 0x3ee)
    {
        if (arg)
        {
            vm_set_var_byte(arg, 0);
            vm_set_var_byte(arg + 1, 0);
            vm_set_var_byte(arg + 2, 0);
            vm_set_var_byte(arg + 3, 0);
        }
        vm_set_call_result(0);
    }
    else if (id == 0x41a)
    {
        printf("[trace][native-dispatch] service=41a a0=%x a1=%x a2=%x\n",
               arg ? vm_get_var(arg) : 0,
               arg ? vm_get_var(arg + 4) : 0,
               arg ? vm_get_var(arg + 8) : 0);
        vm_set_call_result(id);
    }
    else if (id == 0x7d1)
    {
        if (arg)
        {
            u32 outPtr = vm_get_var(arg);
            u32 handle = vm_get_var(arg + 4);
            u32 size = vm_get_var(arg + 8);
            if (outPtr && size)
            {
                u8 zero[16] = {0};
                u32 clearLen = size < sizeof(zero) ? size : (u32)sizeof(zero);
                uc_mem_write(MTK, outPtr, zero, clearLen);
                if (handle == 0x8f && size >= 4)
                {
                    if (!g_nativeSystemInfoPtr)
                    {
                        g_nativeSystemInfoPtr = vm_malloc(0x400);
                        for (u32 off = 0; off < 0x400; off += sizeof(emptyBuff))
                            uc_mem_write(MTK, g_nativeSystemInfoPtr + off, emptyBuff, sizeof(emptyBuff));
                        /* Native SystemInfo embeds service tables.  The linked
                         * compatibility layer uses these three slots during its
                         * first real logic tick. */
                        vm_set_var(g_nativeSystemInfoPtr + 0x9c,
                                   VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS + 13 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0xa0,
                                   VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS + 14 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x24,
                                   VM_MANAGER_LCD_FUNC_LIST_ADDRESS + 9 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x58,
                                   VM_MANAGER_LCD_FUNC_LIST_ADDRESS + 19 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x70,
                                   VM_MANAGER_LCD_FUNC_LIST_ADDRESS + 5 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x74,
                                   VM_MANAGER_LCD_FUNC_LIST_ADDRESS + 5 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x78,
                                   VM_MANAGER_LCD_FUNC_LIST_ADDRESS + 6 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x20c,
                                   VM_MANAGER_STDIO_FUNC_LIST_ADDRESS);
                        vm_set_var(g_nativeSystemInfoPtr + 0x210,
                                   VM_MANAGER_STDIO_FUNC_LIST_ADDRESS + 1 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x214,
                                   VM_MANAGER_STDIO_FUNC_LIST_ADDRESS + 2 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0x22c,
                                   VM_MANAGER_STDIO_FUNC_LIST_ADDRESS + 8 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0xa4,
                                   VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS + 2 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0xa8,
                                   VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS + 1 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0xac,
                                   VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS);
                        vm_set_var(g_nativeSystemInfoPtr + 0xb0,
                                   VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS + 3 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0xb4,
                                   VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS + 4 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0xb8,
                                   VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS + 5 * 4);
                        vm_set_var(g_nativeSystemInfoPtr + 0xf0, VM_NATIVE_DISPATCH_ADDRESS | 1);
                    }
                    vm_set_var(outPtr, g_nativeSystemInfoPtr);
                }
                else if (handle == 0x8e && size >= 4)
                {
                    if (!g_nativePropertyInfoPtr)
                    {
                        g_nativePropertyInfoPtr = vm_malloc(0x100);
                        uc_mem_write(MTK, g_nativePropertyInfoPtr, emptyBuff, 0x100);
                        vm_set_var(g_nativePropertyInfoPtr + 0x14, VM_NATIVE_DISPATCH_ADDRESS | 1);
                    }
                    vm_set_var(outPtr, g_nativePropertyInfoPtr);
                }
                else if (handle == 0x41a && size >= 4)
                {
                    /* Native file-open result.  The sample asks for the
                     * optional external update file upinfo3.dat; absent files
                     * are reported with a negative handle. */
                    vm_set_var(outPtr, 0xffffffffu);
                }
            }
        }
        vm_set_call_result(0);
    }
    else
    {
        u32 r2 = 0;
        u32 r3 = 0;
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        printf("[impl]native dispatcher id:%x arg:%x r2:%x r3:%x lr:%x\n", id, arg, r2, r3, lr);
        assert(0);
    }

    vm_bx(lr);
    return true;
}

static bool hook_vm_fixed_base_manager_init_func(u32 address)
{
    u32 end = VM_FIXED_BASE_MANAGER_INIT_ADDRESS + VM_FIXED_BASE_MANAGER_INIT_COUNT * 4;
    if (address < VM_FIXED_BASE_MANAGER_INIT_ADDRESS || address >= end)
        return false;

    u32 idx = (address - VM_FIXED_BASE_MANAGER_INIT_ADDRESS) / 4;
    if (idx >= sizeof(g_fixedBaseManagerSpecs) / sizeof(g_fixedBaseManagerSpecs[0]))
        return false;

    u32 table = 0;
    u32 lr = 0;
    const vm_fixed_base_manager_spec *spec = &g_fixedBaseManagerSpecs[idx];
    uc_reg_read(MTK, UC_ARM_REG_R0, &table);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    vm_configManagerTableCount(table, spec->funcBase, spec->funcCount);
    printf("[info][cbe] manager_init name=%s table=%08x funcs=%u\n",
           spec->name, table, spec->funcCount);
    vm_set_call_result(table);
    vm_bx(lr);
    return true;
}

static bool hook_vm_native_system_time_func(u32 address)
{
    u32 end = VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS + VM_NATIVE_SYSTEM_TIME_FUNC_COUNT * 4;
    if (address < VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS || address >= end)
        return false;

    u32 idx = (address - VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS) / 4;
    time_t now = time(NULL);
    struct tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    u32 value = 0;
    if (idx == 0)
        value = (u32)localTime.tm_year + 1900;
    else if (idx == 1)
        value = (u32)localTime.tm_mon + 1;
    else if (idx == 2)
        value = (u32)localTime.tm_mday;
    else if (idx == 3)
        value = (u32)localTime.tm_hour;
    else if (idx == 4)
        value = (u32)localTime.tm_min;
    else if (idx == 5)
        value = (u32)localTime.tm_sec;
    vm_set_call_result(value);

    u32 lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    vm_bx(lr);
    return true;
}

/* Mobile Rainbow firmware 0x001F4552 creates a small picture-library object.
 * The fixed-base CBE currently uses its +0x28 method as a six-argument
 * rectangle fill primitive.  Keep the remaining method entries callable so
 * later fixed-base games fail softly while their exact contracts are added. */
static bool hook_vm_fixed_base_gameold_object_func(u32 address)
{
    u32 end = VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_ADDRESS +
              VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_COUNT * 4;
    if (address < VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_ADDRESS || address >= end)
        return false;

    u32 idx = (address - VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_ADDRESS) / 4;
    u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, sp = 0, lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (idx == 4)
    {
        int x = (int)(int16_t)(r1 & 0xffff);
        int y = (int)(int16_t)(r2 & 0xffff);
        int width = (int)(int16_t)(r3 & 0xffff);
        int height = (int)(int16_t)(vm_get_var(sp) & 0xffff);
        u16 color = (u16)(vm_get_var(sp + 4) & 0xffff);
        if (width > 0 && height > 0)
        {
            vm_lcd_fill_rect_local(x, y, width, height, color);
            vm_lcd_sync_cache_rect_to_vm(x, y, width, height);
        }
        vm_set_call_result(0);
    }
    else
    {
        printf("[warn][cbe] fixed_gameold_picture_method idx=%u "
               "r0=%08x r1=%08x r2=%08x r3=%08x\n",
               idx, r0, r1, r2, r3);
        vm_set_call_result(0);
    }

    vm_bx(lr);
    return true;
}

/* Region-set methods installed by Mobile Rainbow sub_1F688A. Rendering in the
 * emulator is immediate, but maintaining the guest-visible list/count keeps
 * the CBE's update and traversal decisions consistent with the firmware. */
static bool hook_vm_fixed_base_gameold_region_func(u32 address)
{
    u32 end = VM_FIXED_BASE_GAMEOLD_REGION_FUNC_ADDRESS +
              VM_FIXED_BASE_GAMEOLD_REGION_FUNC_COUNT * 4;
    if (address < VM_FIXED_BASE_GAMEOLD_REGION_FUNC_ADDRESS || address >= end)
        return false;

    u32 idx = (address - VM_FIXED_BASE_GAMEOLD_REGION_FUNC_ADDRESS) / 4;
    u32 r0 = 0, r1 = 0, r2 = 0, lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (idx == 0)
    {
        /* sub_1F6866: attach one of the two linked region sets. */
        if (r2 == 0)
            vm_set_var(r0 + 0x20, r1);
        else if (r2 == 1)
            vm_set_var(r0 + 0x24, r1);
        vm_set_call_result(r0);
    }
    else if (idx == 4)
    {
        /* sub_1F67FE flushes accumulated rectangles. Host drawing is already
         * immediate, so only reset the guest-visible used count. */
        vm_set_var(r0 + 0x04, 0);
        vm_set_call_result(0);
    }
    else if (idx == 5)
    {
        /* sub_1F6690 mode 2/4 appends a clipped x/y/w/h rectangle. */
        u32 used = vm_get_var(r0 + 0x04);
        u32 capacity = vm_get_var(r0 + 0x08);
        u32 entries = vm_get_var(r0 + 0x0c);
        if (r2 && entries && used < capacity)
        {
            int x = (int)(int16_t)vm_get_var_short(r2 + 0);
            int y = (int)(int16_t)vm_get_var_short(r2 + 2);
            int w = (int)(int16_t)vm_get_var_short(r2 + 4);
            int h = (int)(int16_t)vm_get_var_short(r2 + 6);
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
            if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
            if (w < 0) w = 0;
            if (h < 0) h = 0;
            u32 dst = vm_get_var(entries + used * 4);
            if (dst)
            {
                vm_set_var_short(dst + 0, (u16)x);
                vm_set_var_short(dst + 2, (u16)y);
                vm_set_var_short(dst + 4, (u16)w);
                vm_set_var_short(dst + 6, (u16)h);
                vm_set_var(r0 + 0x04, used + 1);
            }
        }
        vm_set_call_result(vm_get_var(r0 + 0x04));
    }
    else
    {
        vm_set_call_result(0);
    }

    vm_bx(lr);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        snprintf((char *)cbeTextString, mySizeOf(cbeTextString), "460001234567890");
        tmp3 = strlen((char *)cbeTextString) + 1;
        if (tmp2 && tmp2 < tmp3)
            tmp3 = tmp2;
        if (tmp1 && tmp3 > 0)
        {
            if (tmp3 <= strlen((char *)cbeTextString))
                cbeTextString[tmp3 - 1] = 0;
            uc_mem_write(MTK, tmp1, cbeTextString, tmp3);
        }
        vm_set_call_result(0);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
            uc_mem_write(MTK, tmp1, "V017", 4);
        vm_set_call_result(0);
    }
    else if (idx == 8)
    {
        vm_set_call_result(0);
    }
    else if (idx == 9)
    {
        vm_set_call_result(0);
    }
    else if (idx == 10)
    {
        vm_set_call_result(0);
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
        for (u32 off = 0; off < 0x80; off += 4)
        {
            u32 word = 0;
            if (uc_mem_read(MTK, sp + off, &word, 4) != UC_ERR_OK)
                break;
            printf("assert_stack[%02x]=%08x\n", off, word);
            if ((word >= Program_ROM_Address && word < Program_ROM_Address + Program_ROM_Mapped_Size))
            {
            }
        }
        dumpCpuInfo();
        fflush(stdout);
        assert(0);
    }
    else if (idx == 17)
    {
        // DEBUG_PRINT("[call]Coolbar_GetCoolbarDirPath\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
        {
            vm_set_var_short(tmp1, '.');
            vm_set_var_short(tmp1 + 2, '/');
            vm_set_var_short(tmp1 + 4, 0);
        }
        vm_set_call_result(4);
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
        tmp1 = 255;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 23)
    {
        DEBUG_PRINT("[call]vMIsSupportTP -> 0\n");
        vm_set_call_result(0);
    }
    else if (idx == 24)
    {
        DEBUG_PRINT("[call]CDownGetCompany\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        snprintf((char *)cbeTextString, mySizeOf(cbeTextString), "cbe_emu");
        if (tmp1 && tmp2)
            uc_mem_write(MTK, tmp1, cbeTextString, SDL_min(tmp2, (u32)strlen((char *)cbeTextString) + 1));
        vm_set_call_result(0);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_try_write_zero(tmp1, 1);
        vm_try_write_zero(tmp2, 2);
        vm_set_call_result(0);
    }
    else if (idx == 37)
    {
        vm_set_call_result(1);
    }
    else if (idx == 38)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_try_write_zero(tmp1, 1);
        vm_try_write_zero(tmp2, 2);
        vm_set_call_result(0);
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
        vm_input_request_sdl_text_input(0);
        g_vmInputOpen = 0;
        g_vmInputCallback = 0;
        g_vmInputBuffer = 0;
        g_vmInputTargetBuffer = 0;
        g_vmInputMaxLen = 0;
        g_vmInputInputType = 0;
        g_vmInputPrompt = 0;
        g_vmInputPassword = 0;
        g_vmInputComposition[0] = 0;
        vm_set_call_result(0);
    }
    else if (idx == 46)
    {
        vm_set_call_result(g_vmInputOpen ? 1 : 0);
    }
    else if (idx == 47)
    {
        vm_set_call_result((u32)rand());
    }
    else if (idx == 48)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        srand(tmp1);
        vm_set_call_result(0);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1)
            uc_mem_write(MTK, tmp1, "NIECHE00", 8);
        vm_set_call_result(0);
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
        /*
         * Jianghu OL is packaged for the Mstar WQVGA CoolBars profile.  The
         * CBE footer stores the same screen/platform pair as 0x0e/0x05.
         */
        vm_set_call_result(0x0e);
    }
    else if (idx == 65)
    {
        vm_set_call_result(5);
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
        vm_set_call_result((u32)g_vmDlPreAppId);
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
        tmp1 = g_dlSpBf ? g_dlSpBf : vm_dl_current_sp_bf();
        if (tmp1 == 0)
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
        u32 lr = 0;
        u32 sp = 0;
        u32 wrapperLr = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if ((lr & ~1u) == 0x01000b2eu && sp)
            uc_mem_read(MTK, sp + 4, &wrapperLr, sizeof(wrapperLr));
        /*
         * This slot behaves like an inner-app availability probe.  Older CBE
         * startup code in this emulator expects a positive answer, but the
         * Castlevania WPay purchase path calls it from 0x01018a12 before
         * attempting to execute a dynamic module.  With no WPay CBM installed,
         * report unavailable for that probe so the client stays on its normal
         * billing failure path instead of loading a zero-length module.
         */
        if ((wrapperLr & ~1u) == 0x01018a12u)
            vm_set_call_result(vm_host_cbe_sibling_file_exists("Wpay9990Ker42V100.CBM") ? 1 : 0);
        else
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
        vm_set_call_result(0);
    }
    else if (idx == 92)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_try_write_zero(tmp1, 32);
        vm_try_write_zero(tmp2, 32);
        vm_set_call_result(0);
    }
    else if (idx == 93)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_try_write_zero(tmp1, 32);
        vm_try_write_zero(tmp2, 32);
        vm_set_call_result(0);
    }
    else if (idx == 94)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_try_write_zero(tmp1, 32);
        vm_try_write_zero(tmp2, 32);
        vm_set_call_result(0);
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
        /* Match vm_DF_Malloc_IN: zero-length DreamFactory arrays still own a
         * writable sentinel.  JianghuOL's LayoutTextWithWordWrap relies on
         * this when clearing a task-detail text control with an empty string. */
        tmp3 = vm_malloc(tmp2 != 0 ? tmp2 : 2u);
        vm_set_var(tmp1, tmp3);
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 3)
    {
        DEBUG_PRINT("[call]DF_Free\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        tmp2 = vm_get_var(tmp1);
        vm_free(tmp2);
        tmp2 = 0;
        vm_set_var(tmp1, tmp2);
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
        tmp2 = vm_get_var(tmp1);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_MF_MemoryBlock_Release(tmp1);
        DEBUG_PRINT("[call]MF_MemoryBlock_Release\n");
    }
    else if (idx == 17)
    {
        DEBUG_PRINT("[call]DF_InitMemoryEx\n");
        vm_set_call_result(0);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        tmp1 = vm_lcd_font_width_for_mode(tmp1);
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
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);
        vm_readStringGbkByReg(UC_ARM_REG_R0, cbeTextString);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
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
        color = vm_get_var_short(tmp4);
        vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);

        // gbk_to_utf8(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
        // DEBUG_PRINT("[call]vMDrawStringEx(%d,%d,%s)\n", tmp2, tmp3, sprintfBuff);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);

        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 12)
    {
        u32 r0 = 0, r1 = 0;
        u8 firstChar = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        bool r0IsString = r0 && uc_mem_read(MTK, r0, &firstChar, 1) == UC_ERR_OK && firstChar != 0;
        u16 color = 0xffff;
        int x = 0;
        int y = 0;
        if (r0IsString)
        {
            uc_reg_write(MTK, UC_ARM_REG_R0, &r0);
            vm_readStringGbkByReg(UC_ARM_REG_R0, cbeTextString);
            color = (u16)vm_get_var(tmp4 + 4);
            x = vm_lcd_coord_from_reg(r1);
            y = vm_lcd_coord_from_reg(tmp2);
        }
        else
        {
            color = vm_get_var_short(tmp4 + 16);
            vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);
            x = vm_lcd_coord_from_reg(tmp2);
            y = vm_lcd_coord_from_reg(tmp3);
        }
        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        vm_set_call_result(1);
    }
    else if (idx == 13)
    {
        u32 r0 = 0, r1 = 0;
        u8 firstChar = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        bool r0IsString = r0 && uc_mem_read(MTK, r0, &firstChar, 1) == UC_ERR_OK && firstChar != 0;
        u16 color = 0xffff;
        int x = 0;
        int y = 0;
        if (r0IsString)
        {
            vm_readStringGbkByReg(UC_ARM_REG_R0, cbeTextString);
            color = (u16)vm_get_var(tmp4 + 4);
            x = vm_lcd_coord_from_reg(r1);
            y = vm_lcd_coord_from_reg(tmp2);
        }
        else
        {
            color = vm_get_var_short(tmp4 + 16);
            vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);
            x = vm_lcd_coord_from_reg(tmp2);
            y = vm_lcd_coord_from_reg(tmp3);
        }
        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        vm_set_call_result(1);
    }
    else if (idx == 14)
    {
        u32 r0 = 0, r1 = 0;
        u8 firstChar = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        bool r0IsString = r0 && uc_mem_read(MTK, r0, &firstChar, 1) == UC_ERR_OK && firstChar != 0;
        u16 color = 0xffff;
        int x = 0;
        int y = 0;
        if (r0IsString)
        {
            vm_readStringGbkByReg(UC_ARM_REG_R0, cbeTextString);
            color = (u16)vm_get_var(tmp4 + 4);
            x = vm_lcd_coord_from_reg(r1);
            y = vm_lcd_coord_from_reg(tmp2);
        }
        else
        {
            color = vm_get_var_short(tmp4 + 16);
            vm_readStringGbkByReg(UC_ARM_REG_R1, cbeTextString);
            x = vm_lcd_coord_from_reg(tmp2);
            y = vm_lcd_coord_from_reg(tmp3);
        }
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
        color = vm_get_var_short(tmp5);
        int x0 = vm_lcd_coord_from_reg(tmp1);
        int y0 = vm_lcd_coord_from_reg(tmp2);
        int x1 = vm_lcd_coord_from_reg(tmp3);
        int y1 = vm_lcd_coord_from_reg(tmp4);
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
            rectH = vm_get_var(tmp5);
            rectColor = vm_get_var(tmp5 + 4);
            x = vm_lcd_coord_from_reg(tmp1);
            y = vm_lcd_coord_from_reg(tmp2);
            w = vm_lcd_coord_from_reg(tmp3);
            h = vm_lcd_coord_from_reg(rectH);
        }
        u16 color = (u16)rectColor;
        if (vm_lcd_clip_rect(&x, &y, &w, &h, LCD_WIDTH, LCD_HEIGHT))
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
            rectH = vm_get_var(tmp4);
            rectColor = vm_get_var(tmp4 + 4);
        dstPixels = vm_get_var(dstImage);
        dstW = vm_get_var_short(dstImage + 4);
        dstH = vm_get_var_short(dstImage + 6);
        if (dstImage == VM_screenImageStruct_ADDRESS || dstPixels == 0 || dstW == 0 || dstH == 0 || dstW > LCD_WIDTH || dstH > LCD_HEIGHT)
        {
            dstPixels = VM_screenImage_ADDRESS;
            dstW = LCD_WIDTH;
            dstH = LCD_HEIGHT;
        }
        int x = vm_lcd_coord_from_reg(tmp1);
        int y = vm_lcd_coord_from_reg(tmp2);
        int w = vm_lcd_coord_from_reg(tmp3);
        int h = vm_lcd_coord_from_reg(rectH);
        u16 color = (u16)rectColor;
        if (vm_lcd_clip_rect(&x, &y, &w, &h, dstW, dstH))
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
            fillH = vm_get_var(tmp5);
            fillColor = vm_get_var(tmp5 + 4);
            x = vm_lcd_coord_from_reg(tmp1);
            y = vm_lcd_coord_from_reg(tmp2);
            w = vm_lcd_coord_from_reg(tmp3);
            h = vm_lcd_coord_from_reg(fillH);
        }
        if (vm_lcd_clip_rect(&x, &y, &w, &h, LCD_WIDTH, LCD_HEIGHT))
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
        u32 dstImage, dstPixels = 0, fillH = 0, fillColor = 0;
        u16 dstW = 0, dstH = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &dstImage);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
        fillH = vm_get_var(tmp4);
        fillColor = vm_get_var(tmp4 + 4);

        if (vm_lcd_looks_like_fillrect_compat(dstImage, tmp1, tmp2, tmp3))
        {
            int x = vm_lcd_coord_from_reg(dstImage);
            int y = vm_lcd_coord_from_reg(tmp1);
            int w = vm_lcd_coord_from_reg(tmp2);
            int h = vm_lcd_coord_from_reg(tmp3);
            if (vm_lcd_clip_rect(&x, &y, &w, &h, LCD_WIDTH, LCD_HEIGHT))
            {
                u16 color = (u16)fillH;
                for (int row = 0; row < h; row++)
                {
                    u32 off = (y + row) * LCD_WIDTH + x;
                    for (int col = 0; col < w; col++)
                        ((u16 *)Lcd_Cache_Buffer)[off + col] = color;
                    uc_mem_write(MTK, VM_screenImage_ADDRESS + off * 2, Lcd_Cache_Buffer + off * 2, w * 2);
                }
            }
            vm_set_call_result(1);
            uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
            vm_bx(tmp1);
            return true;
        }

        dstPixels = vm_get_var(dstImage);
        dstW = vm_get_var_short(dstImage + 4);
        dstH = vm_get_var_short(dstImage + 6);
        if (dstImage == VM_screenImageStruct_ADDRESS || dstPixels == 0 || dstW == 0 || dstH == 0 || dstW > LCD_WIDTH || dstH > LCD_HEIGHT)
        {
            dstPixels = VM_screenImage_ADDRESS;
            dstW = LCD_WIDTH;
            dstH = LCD_HEIGHT;
        }
        int x = vm_lcd_coord_from_reg(tmp1);
        int y = vm_lcd_coord_from_reg(tmp2);
        int w = vm_lcd_coord_from_reg(tmp3);
        int h = vm_lcd_coord_from_reg(fillH);
        if (vm_lcd_clip_rect(&x, &y, &w, &h, dstW, dstH))
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (tmp2 == 0)
        {
            vm_set_call_result(0);
        }
        else
        {
            vm_IMG_CreateImageFormResForVm(tmp1, tmp2);
            vm_set_call_result(1);
        }
    }
    else if (idx == 24)
    {
        vm_set_call_result(0);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp2 >> 16);
        vm_set_call_result(vm_lcd_draw_image_to_screen(tmp1, x, y));
    }
    else if (idx == 28)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
        vm_set_call_result(vm_lcd_draw_image_to_screen(tmp1, x, y));
    }
    else if (idx == 29)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp2 >> 16);
        vm_set_call_result(vm_lcd_draw_image_with_alpha_to_screen(tmp1, x, y));
    }
    else if (idx == 30)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_set_call_result(vm_lcd_draw_image_with_clip_packed(tmp1, tmp2, tmp3, tmp4, false));
    }
    else if (idx == 31)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);
        u32 h = vm_get_var(tmp5);
        u32 dstX = vm_get_var(tmp5 + 4);
        u32 dstY = vm_get_var(tmp5 + 8);
        u32 srcPacked = vm_lcd_pack_coord(vm_lcd_coord_from_reg(tmp2), vm_lcd_coord_from_reg(tmp3));
        u32 dstStart = vm_lcd_pack_coord(vm_lcd_coord_from_reg(dstX), vm_lcd_coord_from_reg(dstY));
        u32 dstEnd = vm_lcd_pack_coord(
            (int)(int16_t)(vm_lcd_coord_from_reg(dstX) + vm_lcd_coord_from_reg(tmp4) - 1),
            (int)(int16_t)(vm_lcd_coord_from_reg(dstY) + vm_lcd_coord_from_reg(h) - 1));
        vm_lcd_draw_image_with_clip_packed(tmp1, srcPacked, dstStart, dstEnd, false);
        vm_set_call_result(1);
    }
    else if (idx == 32)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_set_call_result(vm_lcd_draw_image_with_clip_packed(tmp1, tmp2, tmp3, tmp4, true));
    }
    else if (idx == 33)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);
        u32 h = vm_get_var(tmp5);
        u32 dstX = vm_get_var(tmp5 + 4);
        u32 dstY = vm_get_var(tmp5 + 8);
        u32 srcPacked = vm_lcd_pack_coord(vm_lcd_coord_from_reg(tmp2), vm_lcd_coord_from_reg(tmp3));
        u32 dstStart = vm_lcd_pack_coord(vm_lcd_coord_from_reg(dstX), vm_lcd_coord_from_reg(dstY));
        u32 dstEnd = vm_lcd_pack_coord(
            (int)(int16_t)(vm_lcd_coord_from_reg(dstX) + vm_lcd_coord_from_reg(tmp4) - 1),
            (int)(int16_t)(vm_lcd_coord_from_reg(dstY) + vm_lcd_coord_from_reg(h) - 1));
        vm_lcd_draw_image_with_clip_packed(tmp1, srcPacked, dstStart, dstEnd, true);
        vm_set_call_result(1);
    }
    else if (idx == 34)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        u16 width = 0;
        if (tmp1)
            width = vm_get_var_short(tmp1 + 4);
        vm_set_call_result(width);
    }
    else if (idx == 35)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        u16 height = 0;
        if (tmp1)
            height = vm_get_var_short(tmp1 + 6);
        vm_set_call_result(height);
    }
    else if (idx == 36)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1 && vm_get_var(tmp1))
        {
            vm_IMG_Destory(tmp1);
            vm_set_call_result(1);
        }
        else
        {
            vm_set_call_result(0);
        }
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
        if (g_vmInputWatchCallback)
        {
            char srcText[64];
            char dstText[64];
            memset(srcText, 0, sizeof(srcText));
            memset(dstText, 0, sizeof(dstText));
            vm_debug_read_guest_ucs2_as_gbk(tmp1, srcText, sizeof(srcText), 64);
            vm_debug_read_guest_cstr(tmp2, dstText, sizeof(dstText));
            printf("[debug][vmInput] lcd40 pc=%08x src=%08x dst=%08x max=%u srcText='%s' dstBefore='%s'\n",
                   lastAddress, tmp1, tmp2, tmp3, srcText, dstText);
        }
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
            memset(sprintfBuff, 0, outLen);
            int conv = ucs2_to_gbk(cbeTextString, srcBytes, sprintfBuff, outLen);
            if (conv < 0)
            {
                sprintfBuff[0] = 0;
            }
            u32 writeLen = (u32)strlen((char *)sprintfBuff) + 1;
            if (writeLen > outLen)
                writeLen = outLen;
            uc_mem_write(MTK, tmp2, sprintfBuff, writeLen);
            if (g_vmInputWatchCallback)
            {
                char dstAfter[64];
                memset(dstAfter, 0, sizeof(dstAfter));
                vm_debug_read_guest_cstr(tmp2, dstAfter, sizeof(dstAfter));
                printf("[debug][vmInput] lcd40-write dst=%08x wrote=%u conv=%d dstAfter='%s'\n",
                       tmp2, writeLen, conv, dstAfter);
            }
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_IMG_InitDataPage(tmp1, tmp2);
    }
    else if (idx == 46)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_IMG_InitInnerDataPageEx(tmp1, tmp2);
    }
    else if (idx == 47)
    {
        vm_IMG_ReleaseDataPage();
    }
    else if (idx == 48)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_IMG_InitDataPageEx(tmp1, tmp2, tmp3);
    }
    else if (idx == 49)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_IMG_CreateImageFormIdEx(tmp1, tmp2, tmp3);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_IMG_InitDataPageTxt(tmp1);
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

    u32 rel = address - VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS;
    if (rel == 0x80 || rel == 0x11c || rel == 0x11e)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);

        if (tmp2 && tmp3)
        {
            u32 clearLen = tmp3 > 0x260 ? 0x260 : tmp3;
            uc_mem_write(MTK, tmp2, emptyBuff, clearLen);
            vm_set_var(tmp2 + 4, 0xfedb1234);
        }
        vm_set_call_result(0);
    }
    else
    {
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
        DEBUG_PRINT("[call]cbfs_vm_file_rename\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_cbfs_vm_file_rename(tmp1, tmp2, tmp3);
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
        u32 lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        printf("[call]cbfs_vm_find_first r0=%08x r1=%08x r2=%08x lr=%08x\n", tmp1, tmp2, tmp3, lr);
        assert(0);
    }
    else if (idx == 15)
    {
        u32 lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        printf("[call]cbfs_vm_find_next r0=%08x r1=%08x r2=%08x lr=%08x\n", tmp1, tmp2, tmp3, lr);
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]cbfs_vm_find_close\n");
        assert(0);
    }
    else if (idx == 17)
    {
        vm_set_call_result(vm_fileio_free_space());
    }
    else if (idx == 18)
    {
        vm_set_call_result(vm_fileio_sdcard_status());
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
        vm_set_call_result(vm_fileio_free_space());
    }
    else if (idx == 29)
    {
        vm_set_call_result(vm_fileio_sdcard_status());
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
        vm_autotest_note_attr_value_write("stdio_memcpy", tmp1, tmp3);
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
        vm_autotest_note_attr_value_write("stdio_memset", tmp1, tmp3);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        DEBUG_PRINT("[call]sprintf(%x,%x,%x)\n", tmp1, tmp2, tmp3);
        vm_sprintf_return_buffer();
        if (g_autotestEnabled && tmp1 != 0)
        {
            vm_readStringByPtr(tmp1, sprintfBuff);
            vm_autotest_note_attr_value_write("stdio_sprintf", tmp1,
                                              (u32)strlen((char *)sprintfBuff) + 1);
        }
        if (g_autotestEnabled)
        {
            u32 arg1 = 0;
            vm_readStringByPtr(tmp2, cbeTextString);
            uc_reg_read(MTK, UC_ARM_REG_R3, &arg1);
            vm_autotest_note_format_preview("stdio", lastAddress, tmp1,
                                            (const char *)cbeTextString,
                                            tmp3, arg1);
        }
    }
    else if (idx == 5)
    {
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 6)
    {
        DEBUG_PRINT("[call]VmGetRand\n");
        vm_math_rand_result();
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
        vm_autotest_note_attr_value_write("stdio_strncpy", tmp1, tmp3);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 9)
    {
        DEBUG_PRINT("[call]strcpy\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_strcpy(tmp1, tmp2);
        if (g_autotestEnabled && tmp1 != 0)
        {
            vm_readStringByPtr(tmp1, sprintfBuff);
            vm_autotest_note_attr_value_write("stdio_strcpy", tmp1,
                                              (u32)strlen((char *)sprintfBuff) + 1);
        }
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (tmp1 && tmp2)
        {
            int dstLen = vm_strlen(tmp1);
            u32 copied = vm_guest_strcpy(tmp1 + dstLen, tmp2);
            vm_autotest_note_attr_value_write("stdio_strcat", tmp1 + dstLen,
                                              copied + 1);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_math_pow_float_result(tmp1, tmp2);
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

static u16 vm_ctrl_text_color(u32 color)
{
    return color ? (u16)color : 0xffff;
}

static void vm_ctrl_read_text(u32 textPtr, bool ucs2, u8 *out, size_t outSize)
{
    if (outSize == 0)
        return;
    out[0] = 0;
    if (textPtr == 0)
        return;

    if (!ucs2)
    {
        vm_readStringByPtrLimited(textPtr, out, outSize);
        return;
    }

    u32 ucs2Len = vm_input_wcslen_limit(textPtr, 0x1ff);
    u32 srcBytes = (ucs2Len + 1) * 2;
    if (srcBytes > mySizeOf(cbeTextString))
        srcBytes = mySizeOf(cbeTextString) & ~1u;
    if (uc_mem_read(MTK, textPtr, cbeTextString, srcBytes) != UC_ERR_OK)
        return;
    if (ucs2_to_gbk(cbeTextString, srcBytes, out, outSize) < 0)
        out[0] = 0;
}

static int vm_ctrl_image_height(u32 imageInfo, int fallbackHeight)
{
    u32 pixels = 0;
    int width = 0;
    int height = 0;
    if (vm_lcd_read_image_info(imageInfo, &pixels, &width, &height))
        return height;
    return fallbackHeight;
}

static void vm_ctrl_draw_text(u8 *text, int x, int y, u32 color)
{
    if (text == NULL || text[0] == 0)
        return;
    vm_lcd_draw_current_string(text, x, y, vm_ctrl_text_color(color));
    vm_lcd_sync_string_to_vm(text, x, y);
}

static u32 vm_ctrl_draw_win_title(u32 imageInfo, u32 textPtr, u32 color, int x, int y, bool explicitPos, bool ucs2)
{
    u8 text[256];
    int titleH = vm_ctrl_image_height(imageInfo, 30);

    if (imageInfo)
        vm_lcd_draw_image_to_screen(imageInfo, 0, 0);
    if (textPtr == 0)
        return imageInfo;

    vm_ctrl_read_text(textPtr, ucs2, text, sizeof(text));
    if (text[0] == 0)
        return 0;

    if (!explicitPos)
    {
        int textW = vm_lcd_measure_current_string_render_width(text);
        int textH = getFontHeight();
        x = textW >= 220 ? 10 : (LCD_WIDTH - textW) / 2;
        y = (titleH - textH) / 2;
        if (y < 0)
            y = 0;
    }
    vm_ctrl_draw_text(text, x, y, color);
    return 1;
}

static u32 vm_ctrl_draw_softkey_bar(u32 imageInfo, u32 leftPtr, u32 centerPtr, u32 rightPtr, u32 color, bool ucs2)
{
    u8 left[128];
    u8 center[128];
    u8 right[128];
    int barH = vm_ctrl_image_height(imageInfo, 27);
    int y;

    if (imageInfo)
        vm_lcd_draw_image_to_screen(imageInfo, 0, LCD_HEIGHT - barH);

    y = LCD_HEIGHT - barH + (barH - getFontHeight()) / 2;
    if (y < 0)
        y = LCD_HEIGHT - getFontHeight();

    vm_ctrl_read_text(leftPtr, ucs2, left, sizeof(left));
    vm_ctrl_read_text(centerPtr, ucs2, center, sizeof(center));
    vm_ctrl_read_text(rightPtr, ucs2, right, sizeof(right));

    if (left[0])
        vm_ctrl_draw_text(left, 10, y, color);
    if (center[0])
    {
        int w = vm_lcd_measure_current_string_render_width(center);
        vm_ctrl_draw_text(center, (LCD_WIDTH - w) / 2, y, color);
    }
    if (right[0])
    {
        int w = vm_lcd_measure_current_string_render_width(right);
        vm_ctrl_draw_text(right, LCD_WIDTH - 11 - w, y, color);
    }
    return 1;
}

static u32 vm_ctrl_tp_press_softkey_bar(u32 imageInfo, u32 textPtr, u32 xReg, u32 yReg, u32 pos, bool ucs2)
{
    u8 text[128];
    int x = vm_lcd_coord_from_reg(xReg);
    int y = vm_lcd_coord_from_reg(yReg);
    int barH = vm_ctrl_image_height(imageInfo, 27);
    int top = LCD_HEIGHT - barH;
    int bottom = LCD_HEIGHT - 1;
    int left = 0;
    int right = LCD_WIDTH - 1;
    int textW;

    if (textPtr == 0)
        return 0;
    vm_ctrl_read_text(textPtr, ucs2, text, sizeof(text));
    if (text[0] == 0)
        return 0;
    textW = vm_lcd_measure_current_string_render_width(text);

    if (pos == 0)
    {
        left = 0;
        right = textW + 10;
    }
    else if (pos == 1)
    {
        left = (230 - textW) / 2;
        right = left + textW + 10;
    }
    else if (pos == 2)
    {
        left = LCD_WIDTH - 11 - textW;
        right = LCD_WIDTH - 1;
    }

    return (x >= left && x <= right && y >= top && y <= bottom) ? 1 : 0;
}

static bool hook_vm_manager_ctrl_func(u32 address)
{
    if (!(address >= VM_MANAGER_CTRL_FUNC_LIST_ADDRESS && address < (VM_MANAGER_CTRL_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_CTRL_FUNC_LIST_ADDRESS) / 4;

    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
    uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
    uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);

    if (idx == 0)
    {
        vm_set_call_result(vm_ctrl_draw_softkey_bar(tmp1, tmp2, tmp3, tmp4, 0, false));
    }
    else if (idx == 1)
    {
        u32 color = vm_get_var(tmp5);
        vm_set_call_result(vm_ctrl_draw_softkey_bar(tmp1, tmp2, tmp3, tmp4, color, false));
    }
    else if (idx == 2)
    {
        vm_set_call_result(vm_ctrl_draw_win_title(tmp1, tmp2, 0, 0, 0, false, false));
    }
    else if (idx == 3)
    {
        u32 y = vm_get_var(tmp5);
        vm_set_call_result(vm_ctrl_draw_win_title(tmp1, tmp2, tmp3,
                                                  vm_lcd_coord_from_reg(tmp4),
                                                  vm_lcd_coord_from_reg(y),
                                                  true, false));
    }
    else if (idx == 4)
    {
        u32 pos = vm_get_var(tmp5);
        vm_set_call_result(vm_ctrl_tp_press_softkey_bar(tmp1, tmp2, tmp3, tmp4, pos, false));
    }
    else if (idx == 9)
    {
        vm_set_call_result(vm_ctrl_draw_win_title(tmp1, tmp2, tmp3, 0, 0, false, false));
    }
    else if (idx == 15)
    {
        vm_set_call_result(vm_ctrl_draw_softkey_bar(tmp1, tmp2, tmp3, tmp4, 0, true));
    }
    else if (idx == 16)
    {
        u32 color = vm_get_var(tmp5);
        vm_set_call_result(vm_ctrl_draw_softkey_bar(tmp1, tmp2, tmp3, tmp4, color, true));
    }
    else if (idx == 17)
    {
        u32 pos = vm_get_var(tmp5);
        vm_set_call_result(vm_ctrl_tp_press_softkey_bar(tmp1, tmp2, tmp3, tmp4, pos, true));
    }
    else if (idx == 18)
    {
        vm_set_call_result(vm_ctrl_draw_win_title(tmp1, tmp2, 0, 0, 0, false, true));
    }
    else if (idx == 19)
    {
        vm_set_call_result(vm_ctrl_draw_win_title(tmp1, tmp2, tmp3, 0, 0, false, true));
    }
    else if (idx == 20)
    {
        u32 y = vm_get_var(tmp5);
        vm_set_call_result(vm_ctrl_draw_win_title(tmp1, tmp2, tmp3,
                                                  vm_lcd_coord_from_reg(tmp4),
                                                  vm_lcd_coord_from_reg(y),
                                                  true, true));
    }
    else
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
    u32 netSp = 0;
    u32 netStackArg0 = 0;
    u32 netR4 = 0, netR5 = 0, netR6 = 0, netR7 = 0, netLr = 0;
    static u32 s_netManagerObserveCount = 0;

    u32 idx = (address - VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS) / 4;
    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
    uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
    uc_reg_read(MTK, UC_ARM_REG_R4, &netR4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &netR5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &netR6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &netR7);
    uc_reg_read(MTK, UC_ARM_REG_SP, &netSp);
    uc_reg_read(MTK, UC_ARM_REG_LR, &netLr);
    if (netSp)
        uc_mem_read(MTK, netSp, &netStackArg0, sizeof(netStackArg0));
    DEBUG_PRINT("[probe_net_idx] idx=%u r0=%x r1=%x r2=%x r3=%x last=%x\n", idx, tmp1, tmp2, tmp3, tmp4, lastAddress);
    if (s_netManagerObserveCount < 20)
    {
        ++s_netManagerObserveCount;
        vm_autotest_note("network_call idx=%u r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
                         idx, tmp1, tmp2, tmp3, tmp4, netLr);
    }
    if (idx == 0)
    {
        g_netCurrentObject = netR4;
        tmp5 = g_nextNetConnectId++;
        if (tmp5 == 0)
            tmp5 = g_nextNetConnectId++;
        if (tmp4)
            uc_mem_write(MTK, tmp4, &tmp5, 4);
        scheduler_register_net_channel(tmp5, tmp3, tmp4);
        u8 netState = 1;
        uc_mem_write(MTK, Global_R9 + 0x9588 + 0x0c, &netState, 1);
        scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
        vm_set_call_result(1);
    }
    else if (idx == 1)
    {
        if (netR4)
            g_netCurrentObject = netR4;
        vm_net_mock_on_send(tmp1, tmp3, tmp2);
        vm_set_call_result(tmp2);
    }
    else if (idx == 2)
    {
        scheduler_unregister_net_channel(tmp1);
        u8 netState = 0;
        uc_mem_write(MTK, Global_R9 + 0x9588 + 0x0c, &netState, 1);
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        bool callbackValid =
            (tmp2 >= Program_ROM_Address && tmp2 < Program_ROM_Address + Program_ROM_Mapped_Size) ||
            vm_is_pool_entry(tmp2);
        if (callbackValid && tmp3)
        {
            u32 connectIdOut = vm_is_writable_vm_range(netStackArg0, sizeof(tmp5)) ? netStackArg0 : 0;
            if (connectIdOut == 0 && vm_is_writable_vm_range(tmp3, sizeof(tmp5)))
                connectIdOut = tmp3;
            tmp5 = g_nextNetConnectId++;
            if (tmp5 == 0)
                tmp5 = g_nextNetConnectId++;
            if (connectIdOut)
                uc_mem_write(MTK, connectIdOut, &tmp5, 4);
            scheduler_register_net_channel(tmp5, tmp2, tmp3);
            vm_net_queue_http_get_mock_response(tmp1, tmp2, tmp3);
            vm_set_call_result(1);
        }
        else
        {
            tmp1 = vm_get_var(Global_R9 + 0x5a3c + 0x10);
            if (tmp1)
            {
                tmp2 = 0;
                uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
                vm_bx(tmp1);
                return true;
            }
            vm_set_call_result(0);
        }
    }
    else if (idx == 6 || idx == 7 || idx == 18)
    {
        scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
        vm_set_call_result(1);
    }
    else if (idx == 17)
    {
        scheduler_queue_net_task(tmp1, 0, tmp2, tmp3);
        vm_set_call_result(1);
    }
    else if (idx == 4 || idx == 19 || idx == 20 || idx == 29 || idx == 30)
    {
        vm_set_call_result(1);
    }
    else if (idx == 35)
    {
        g_netUpLinkData = 0;
        g_netDownLinkData = 0;
        g_netMockResponseOffset = 0;
        vm_set_call_result(0);
    }
    else if (idx == 36)
    {
        if (tmp1)
            uc_mem_write(MTK, tmp1, &g_netUpLinkData, 4);
        if (tmp2)
            uc_mem_write(MTK, tmp2, &g_netDownLinkData, 4);
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
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        tmp5 = vm_get_var(sp + 0x0);
        sp = vm_get_var(sp + 0x4);
        vm_set_call_result(vm_cd_rect_point(tmp1, tmp2, tmp3, tmp4, tmp5, sp));
    }
    else if (idx == 9)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_math_sqrt_result(tmp1);
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_set_var(VM_DreamFactory_DataPackage_ADDRESS, tmp1);
        vm_set_call_result(0);
    }
    else if (idx == 11)
    {
        tmp1 = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
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
        DEBUG_PRINT("[call]DF_ReadString2\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_ReadString2(tmp1, tmp2);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_math_df_degree_result(tmp1, tmp2);
    }
    else if (idx == 35)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_math_df_collection_test_result(tmp1, tmp2, tmp3, tmp4);
    }
    else if (idx == 36)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_math_df_swap_val_result(tmp1, tmp2);
    }
    else if (idx == 37)
    {
        DEBUG_PRINT("[call]DF_GetFormatString\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_readStringByPtr(tmp1, cbeTextString);
        vm_DF_GetFormatString();
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp5);
        vm_autotest_note_format_preview("df37", lastAddress, tmp5,
                                        (const char *)cbeTextString,
                                        tmp2, tmp3);
    }
    else if (idx == 38)
    {
        DEBUG_PRINT("[call]Storage_Date\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_storage_date(tmp1, tmp2, tmp3, tmp4);
    }
    else if (idx == 39)
    {
        printf("[call]vMstricmp\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_readStringByPtr(tmp1, cbeTextString);
        vm_readStringByPtr(tmp2, sprintfBuff);
        tmp1 = (u32)strcasecmp((char *)cbeTextString, (char *)sprintfBuff);
        vm_set_call_result(tmp1);
    }
    else if (idx == 40)
    {
        printf("[call]vMstrnicmp\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_readStringByPtr(tmp1, cbeTextString);
        vm_readStringByPtr(tmp2, sprintfBuff);
        tmp1 = (u32)strncasecmp((char *)cbeTextString, (char *)sprintfBuff, tmp3);
        vm_set_call_result(tmp1);
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
        vm_set_var(VM_DreamFactory_DataPackage_ADDRESS, 0);
        tmp1 = VM_MemoryBlock_PTR_ADDRESS;
        vm_set_var(VM_DreamFactory_MemoryBlock_ADDRESS, tmp1);
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
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        printf("[call]BILLING_Pay\n");
        vm_set_call_result(0);
    }
    else if (idx == 4)
    {
        printf("[call]BILLING_PayMoreTimes\n");
        vm_set_call_result(0);
    }
    else if (idx == 5)
    {
        printf("[call]BILLING_IsRegisterBillingInfo\n");
        vm_set_call_result(0);
    }
    else if (idx == 6)
    {
        printf("[call]BILLING_RegisterBillingInfo\n");
        vm_set_call_result(0);
    }
    else if (idx == 7)
    {
        printf("[call]BILLING_SetBillingStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 8)
    {
        printf("[call]BILLING_GetBillingStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 9)
    {
        printf("[call]BILLING_IsNeedPay\n");
        vm_set_call_result(1);
    }
    else if (idx == 10)
    {
        printf("[call]BILLING_IsInTryStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 11)
    {
        printf("[call]BIllING_OpenBillingPromptWin\n");
        vm_set_call_result(0);
    }
    else if (idx == 12)
    {
        printf("[call]CDownGetTryDay\n");
        vm_set_call_result(0);
    }
    else if (idx == 13)
    {
        printf("[call]BILLING_PayForCBB\n");
        vm_set_call_result(0);
    }
    else if (idx == 14)
    {
        printf("[call]BILLING_PayForPwd\n");
        vm_set_call_result(0);
    }
    else if (idx == 15)
    {
        printf("[call]CDownGetOption5\n");
        vm_set_call_result(0);
    }
    else if (idx == 16)
    {
        u32 sp = 0;
        u32 smsCallback = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp)
            uc_mem_read(MTK, sp + 8, &smsCallback, sizeof(smsCallback));
        printf("[call]Billing_SendSpecSms callback=%08x\n", smsCallback);
        vm_autotest_note("billing_send_spec_sms type=%u text=%08x text_len=%u dest=%08x callback=%08x\n",
                         tmp1, tmp2, tmp3, tmp4, smsCallback);
        if (smsCallback)
        {
            scheduler_queue_net_event(0, 1, 0, 0, smsCallback, 0);
        }
        vm_set_call_result(0);
    }
    else if (idx == 17)
    {
        printf("[call]Billing_CancelSms\n");
        vm_set_call_result(0);
    }
    else if (idx == 18)
    {
        printf("[call]CDownIsMonthApp\n");
        vm_set_call_result(0);
    }
    else if (idx == 19)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        if (!vm_host_cbe_sibling_file_exists("Wpay9990Ker42WqvgaV100.CBM"))
        {
            if (tmp2)
                vm_set_var(tmp2, 0);
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
            vm_set_call_result(1);
        }
    }
    else if (idx == 20)
    {
        printf("[call]CDownGetPayTimes\n");
        vm_set_call_result(0);
    }
    else if (idx == 21)
    {
        printf("[call]BILLING_NewMonthPay\n");
        vm_set_call_result(0);
    }
    else if (idx == 22)
    {
        printf("[call]BILLING_NewMonthCancel\n");
        vm_set_call_result(0);
    }
    else if (idx == 23)
    {
        printf("[call]BILLING_CleanAppMonthBillInfo\n");
        vm_set_call_result(0);
    }
    else if (idx == 24)
    {
        printf("[call]BILLING_GetValidDayByAppId\n");
        vm_set_call_result(0);
    }
    else if (idx == 25)
    {
        printf("[call]CDownGetBillSmsAddr\n");
        vm_set_call_result(0);
    }
    else if (idx == 26)
    {
        printf("[call]CDownGetBillSmsSuf\n");
        vm_set_call_result(0);
    }
    else if (idx == 27)
    {
        printf("[call]BILLING_Pay2\n");
        vm_set_call_result(0);
    }
    else if (idx == 28)
    {
        printf("[call]Billing_GetAppUsedStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 29)
    {
        printf("[call]Billing_SetAppUsedStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 30)
    {
        printf("[call]CDownGetPayTipContent\n");
        vm_set_call_result(0);
    }
    else if (idx == 31)
    {
        printf("[call]BILLING_Pay3\n");
        vm_set_call_result(0);
    }
    else if (idx == 32)
    {
        printf("[call]BILLING_SendRegisterSms\n");
        vm_set_call_result(0);
    }

    // result 区
    else if (idx == 33)
    {
        printf("[call]CDownGetOption8\n");
        vm_set_call_result(0);
    }
    else if (idx == 34)
    {
        printf("[call]CDownGetOption9\n");
        vm_set_call_result(0);
    }
    else if (idx == 35)
    {
        printf("[call]CDownGetOption10\n");
        vm_set_call_result(0);
    }
    else if (idx == 36)
    {
        printf("[call]BILLING_Register\n");
        vm_set_call_result(0);
    }
    else if (idx == 37)
    {
        printf("[call]BILLING_PayForCBB3\n");
        vm_set_call_result(0);
    }
    else if (idx == 38)
    {
        printf("[call]CDownIsWPay\n");
        vm_set_call_result(0);
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
        }
        vm_set_call_result(tmp1);
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_readStringByPtr(tmp2, cbeTextString);
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
        vm_set_call_result(tmp1);
    }
    else if (idx == 6)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp1 && tmp2)
        {
            for (u32 i = 0; i < tmp3; ++i)
            {
                u8 src = 0;
                u16 dst = 0;
                uc_mem_read(MTK, tmp2 + i, &src, 1);
                if (src)
                    dst = src;
                uc_mem_write(MTK, tmp1 + i * 2, &dst, 2);
                if (!src)
                {
                    for (++i; i < tmp3; ++i)
                        uc_mem_write(MTK, tmp1 + i * 2, &dst, 2);
                    break;
                }
            }
        }
        vm_set_call_result(tmp1);
    }
    else if (idx == 7)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp1 && tmp2)
        {
            for (u32 i = 0; i < tmp3; ++i)
            {
                u8 src = 0;
                u16 dst = 0;
                uc_mem_read(MTK, tmp2 + i, &src, 1);
                dst = src;
                uc_mem_write(MTK, tmp1 + i * 2, &dst, 2);
            }
        }
        vm_set_call_result(tmp1);
    }
    else if (idx == 8)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        tmp3 = 0;
        if (tmp1)
        {
            for (u32 off = 0; off < 0x10000; off += 2)
            {
                u16 ch = 0;
                uc_mem_read(MTK, tmp1 + off, &ch, 2);
                if (ch == (u16)tmp2)
                {
                    tmp3 = tmp1 + off;
                    break;
                }
                if (ch == 0)
                    break;
            }
        }
        vm_set_call_result(tmp3);
    }
    else if (idx == 9)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        int result = 0;
        if (tmp1 && tmp2)
        {
            for (u32 off = 0; off < 0x10000; off += 2)
            {
                u16 a = 0, b = 0;
                uc_mem_read(MTK, tmp1 + off, &a, 2);
                uc_mem_read(MTK, tmp2 + off, &b, 2);
                result = (int)a - (int)b;
                if (result || a == 0 || b == 0)
                    break;
            }
        }
        vm_set_call_result((u32)result);
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        int result = 0;
        if (tmp1 && tmp2)
        {
            for (u32 off = 0; off < 0x10000; off += 2)
            {
                u16 a = 0, b = 0;
                uc_mem_read(MTK, tmp1 + off, &a, 2);
                uc_mem_read(MTK, tmp2 + off, &b, 2);
                if (a >= 'a' && a <= 'z')
                    a = (u16)(a - 'a' + 'A');
                if (b >= 'a' && b <= 'z')
                    b = (u16)(b - 'a' + 'A');
                result = (int)a - (int)b;
                if (result || a == 0 || b == 0)
                    break;
            }
        }
        vm_set_call_result((u32)result);
    }
    else if (idx == 11)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        int result = 0;
        if (tmp1 && tmp2)
        {
            for (u32 i = 0; i < tmp3; ++i)
            {
                u16 a = 0, b = 0;
                uc_mem_read(MTK, tmp1 + i * 2, &a, 2);
                uc_mem_read(MTK, tmp2 + i * 2, &b, 2);
                result = (int)a - (int)b;
                if (result || a == 0 || b == 0)
                    break;
            }
        }
        vm_set_call_result((u32)result);
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
        vm_set_var(VM_SCREEN_nextSubTScreen_ADDRESS, tmp1);
        tmp2 = 0;
        vm_set_var(VM_SCREEN_isInQuit_ADDRESS, tmp2);
        vm_screen_stack_replace_top(tmp1, 0, 1, vm_screen_stack_lookup_module_base(tmp1));
        vmAddedScreen = tmp1;
        screenStructChange = 1;
        g_screenExitMode = VM_SCREEN_EXIT_DESTROY;
        g_screenRemovedWithoutNext = 0;
        g_screenEnterExistingNoCallback = 0;
        vm_set_call_result(VM_SCREEN_isInQuit_ADDRESS);
    }
    else if (idx == 1)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        if (tmp1 == 0)
            tmp1 = vmAddedScreen;
        g_screenLoadResourcePendingScreen = tmp1;
        g_screenLoadResourcePendingParam = vm_screen_stack_lookup_param(tmp1);
        screenStructNotifyLoadRes = tmp1 != 0;
        vm_set_call_result(0);
    }
    else if (idx == 2 || idx == 3)
    {
        bool acceptChange = true;
        u32 moduleBase = 0;
        const vm_net_mock_scene_change_target *activeTarget = NULL;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        if (idx == 2)
        {
            tmp2 = 0;
            tmp3 = 1;
        }
        if (tmp1 != 0 && tmp1 == vmAddedScreen && lastAddress == 0x01018150)
        {
            activeTarget = vm_active_scene_reenter_target();
            if (vm_net_mock_consume_update_completed_scene_reenter(activeTarget))
            {
                acceptChange = true;
                /* The update callback can call EnterSceneByMapName more than
                 * once before returning.  Refresh the serial-bound guard for
                 * the one accepted resource-completion re-entry so any later
                 * call in the same callback is suppressed. */
                vm_scene_same_reenter_remember_target(activeTarget);
            }
            else if (vm_scene_same_reenter_matches_target(activeTarget))
            {
                acceptChange = false;
                printf("[info][screen] screen_mgr same-suppressed caller=%08x serial=%u scene=%s pos=(%u,%u) exit=%u\n",
                       lastAddress,
                       g_vm_net_mock_last_scene_change_target_serial,
                       activeTarget ? activeTarget->scene : "",
                       activeTarget ? activeTarget->x : 0,
                       activeTarget ? activeTarget->y : 0,
                       activeTarget ? activeTarget->exitId : 0);
                vm_autotest_note("screen_mgr idx=%u type=same-suppressed caller=%08x screen=%08x added=%08x scene=%s pos=(%u,%u) exit=%u\n",
                                 idx, lastAddress, tmp1, vmAddedScreen,
                                 activeTarget ? activeTarget->scene : "",
                                 activeTarget ? activeTarget->x : 0,
                                 activeTarget ? activeTarget->y : 0,
                                 activeTarget ? activeTarget->exitId : 0);
            }
            else
            {
                vm_scene_same_reenter_remember_target(activeTarget);
            }
        }
        if (tmp1 != 0 && acceptChange)
        {
            moduleBase = vm_read_current_pool_r9();
            if (!moduleBase)
                moduleBase = vm_dl_current_sp_bf();
            if (!moduleBase)
                moduleBase = vm_screen_stack_lookup_module_base(tmp1);
            if (moduleBase)
                vm_dl_note_sp_bf(moduleBase, "screen-change");
            vm_autotest_note("screen_mgr idx=%u type=change caller=%08x screen=%08x param=%08x flags=%u old=%08x\n",
                             idx, lastAddress, tmp1, tmp2, tmp3, vmAddedScreen);
            vm_screen_stack_replace_top(tmp1, tmp2, tmp3, moduleBase);
            vmAddedScreen = tmp1;
            vm_set_var(VM_SCREEN_nextSubTScreen_ADDRESS, tmp1);
            tmp4 = 0;
            vm_set_var(VM_SCREEN_isInQuit_ADDRESS, tmp4);
            screenStructChange = 1;
            g_screenExitMode = VM_SCREEN_EXIT_DESTROY;
            g_screenRemovedWithoutNext = 0;
            g_screenEnterExistingNoCallback = 0;
        }
        vm_set_call_result(0);
    }
    else if (idx == 4 || idx == 5)
    {
        u32 moduleBase = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        u32 oldActiveScreen = vmAddedScreen;
        bool wasEmptyScreenStack = g_screenRemovedWithoutNext || vmAddedScreen == 0 || g_screenStackCount == 0;
        if (idx == 4)
        {
            tmp2 = 0;
            tmp3 = 1;
        }
        moduleBase = vm_read_current_pool_r9();
        if (!moduleBase)
            moduleBase = vm_dl_current_sp_bf();
        if (tmp1 != 0 && tmp1 != vmAddedScreen)
            vm_screen_stack_preserve_active_if_needed();
        if (moduleBase)
            vm_dl_note_sp_bf(moduleBase, "screen-add");
        vm_screen_stack_push(tmp1, tmp2, tmp3, moduleBase);
        if (tmp1 != 0)
            vmAddedScreen = tmp1;
        u32 startupObj = 0;
        if (Global_R9)
            startupObj = vm_get_var(Global_R9 + 0x9928 + 0x10);
        bool promoteAddScreen = tmp1 != 0 && (wasEmptyScreenStack ||
                                               g_currentScreenThis != 0 ||
                                               (startupObj == 0 && g_lastStartupScreenState != 0xff));
        if (promoteAddScreen)
        {
            vm_set_var(VM_SCREEN_nextSubTScreen_ADDRESS, tmp1);
            tmp4 = 0;
            vm_set_var(VM_SCREEN_isInQuit_ADDRESS, tmp4);
            screenStructChange = 1;
            g_screenExitMode = g_screenStackCount > 1 ? VM_SCREEN_EXIT_PAUSE : VM_SCREEN_EXIT_DESTROY;
            g_screenEnterExistingNoCallback = 0;
        }
        if (tmp1 != 0)
            g_screenRemovedWithoutNext = 0;
        vm_autotest_note("screen_mgr idx=%u type=add caller=%08x screen=%08x param=%08x flags=%u old=%08x this=%08x depth=%u\n",
                         idx, lastAddress, tmp1, tmp2, tmp3, oldActiveScreen, g_currentScreenThis, g_screenStackCount);
        vm_set_call_result(0);
    }
    else if (idx == 6)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        int removeIndex = vm_screen_stack_find_related(tmp1);
        bool removingCurrent = removeIndex >= 0 && g_screenStack[(u32)removeIndex] == vmAddedScreen;
        u32 removedThis = removingCurrent ? g_currentScreenThis : 0;
        u32 removedModuleBase = removingCurrent ? g_currentScreenModuleBase : 0;
        u32 removedDataPackage = removingCurrent ? g_currentScreenDataPackage : vm_screen_stack_lookup_data_package(tmp1);
        tmp3 = 0;
        tmp2 = 0;
        tmp5 = 0;
        u32 newTopDataPackage = 0;
        tmp4 = vm_screen_stack_remove(tmp1, &tmp3, &tmp2, &tmp5, &newTopDataPackage) ? 1 : 0;
        printf("[info][screen] screen_mgr remove requested=%08x current=%08x result=%u current_match=%u new_top=%08x module=%08x dp=%08x\n",
               tmp1, vmAddedScreen, tmp4, removingCurrent ? 1u : 0u, tmp3, tmp5, newTopDataPackage);
        if (tmp4 && removingCurrent && tmp3)
        {
            bool requestAppClose = g_screenRootExitArmed &&
                                   g_screenStackCount == 1 &&
                                   vm_screen_is_entry_root(tmp3) &&
                                   g_appExitEntry != 0 &&
                                   !g_hostQuitRequested &&
                                   !g_hostQuitCleanupStarted;
            u32 isInQuit = vm_get_var(VM_SCREEN_isInQuit_ADDRESS);
            g_activeScreenRemovedThisFrame = 1;
            g_activeScreenRemovedThis = removedThis;
            g_activeScreenRemovedModuleBase = removedModuleBase;
            g_activeScreenRemovedDataPackage = removedDataPackage;
            g_screenResumeExisting = isInQuit ? 0 : 1;
            g_screenEnterExistingNoCallback = isInQuit ? 1 : 0;
            g_screenExitMode = VM_SCREEN_EXIT_SKIP;
            vmAddedScreen = tmp3;
            g_currentScreenThis = tmp3 - 0x18;
            g_currentScreenModuleBase = tmp5;
            if (tmp5)
                vm_dl_note_sp_bf(tmp5, "screen-remove-newtop");
            vm_trace_screen_data_package_change("remove-newtop", tmp3,
                                                g_currentScreenDataPackage, newTopDataPackage, vm_current_data_package());
            g_currentScreenDataPackage = newTopDataPackage;
            vm_restore_data_package(newTopDataPackage);
            vm_set_var(VM_SCREEN_nextSubTScreen_ADDRESS, tmp3);
            vm_set_var(VM_SCREEN_isInQuit_ADDRESS, isInQuit);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
            if (requestAppClose)
                vm_screen_root_exit_arm_pending(tmp1, tmp3);
        }
        else if (tmp4 && removingCurrent)
        {
            g_activeScreenRemovedThisFrame = 1;
            g_activeScreenRemovedThis = removedThis;
            g_activeScreenRemovedModuleBase = removedModuleBase;
            g_activeScreenRemovedDataPackage = removedDataPackage;
            vmAddedScreen = 0;
            g_screenResumeExisting = 0;
            g_screenEnterExistingNoCallback = 0;
            g_screenRemovedWithoutNext = 1;
            g_screenExitMode = VM_SCREEN_EXIT_SKIP;
            g_currentScreenThis = 0;
            g_currentScreenModuleBase = 0;
            g_currentScreenDataPackage = 0;
            g_screenLoadResourcePendingScreen = 0;
            g_screenLoadResourcePendingParam = 0;
            screenStructNotifyLoadRes = 0;
            vm_set_var(VM_SCREEN_nextSubTScreen_ADDRESS, 0);
        }
        vm_set_call_result(tmp4);
    }
    else if (idx == 7 || idx == 8)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (idx == 7)
            tmp2 = 0;
        g_screenLoadResourcePendingScreen = tmp1;
        g_screenLoadResourcePendingParam = tmp2 ? tmp2 : vm_screen_stack_lookup_param(tmp1);
        screenStructNotifyLoadRes = tmp1 != 0;
        vm_set_call_result(0);
    }
    else if (idx == 9)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        int existing = vm_screen_stack_find_related(tmp1);
        tmp2 = existing >= 0 && g_screenStackCount > 0 && (u32)existing == g_screenStackCount - 1;
        vm_set_call_result(tmp2);
    }
    else if (idx == 10)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        int existing = vm_screen_stack_find_related(tmp1);
        tmp2 = existing == 0 && g_screenStackCount > 0;
        vm_set_call_result(tmp2);
    }
    else if (idx == 11)
    {
        vm_set_call_result(0);
    }
    else
    {
        printf("[impl]vmScreenManager调用位置:%d\n", idx);
        assert(0);
    }
screen_func_return:
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp5);
        vm_note_stream_data_result("game_lcd", lastAddress, tmp1, tmp2, tmp3, tmp4, tmp5);
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
        u32 lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        printf("[impl]vmNetAppManager idx=%u r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
               idx, tmp1, tmp2, tmp3, tmp4, lr);
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
        DEBUG_PRINT("[call]vMAudioPlayByData\n");
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        DEBUG_PRINT("[call]vMAudioPlayWithDataPackage\n");
        vm_set_call_result(0);
    }
    else if (idx == 4)
    {
        DEBUG_PRINT("[call]vMAudioPlayForGame(a1,a2)\n");
        tmp1 = 0; // pasue stop playing
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 5)
    {
        DEBUG_PRINT("[call]vMAudioPlayForApp\n");
        vm_set_call_result(0);
    }
    else if (idx == 6)
    {
        DEBUG_PRINT("[call]vMAudioPause\n");
        vm_set_call_result(0);
    }
    else if (idx == 7)
    {
        DEBUG_PRINT("[call]vMAudioResume\n");
        vm_set_call_result(0);
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
        DEBUG_PRINT("[call]vm_mp3PlayBystream\n");
        vm_set_call_result(0);
    }
    else if (idx == 11)
    {
        DEBUG_PRINT("[call]vm_mp3PauseByStream\n");
        vm_set_call_result(0);
    }
    else if (idx == 12)
    {
        DEBUG_PRINT("[call]vm_mp3ResumeByStream\n");
        vm_set_call_result(0);
    }
    else if (idx == 13)
    {
        DEBUG_PRINT("[call]vm_mp3StopBystream\n");
        vm_set_call_result(0);
    }
    else if (idx == 14)
    {
        DEBUG_PRINT("[call]vm_mp3PlayByFile\n");
        vm_set_call_result(0);
    }
    else if (idx == 15)
    {
        DEBUG_PRINT("[call]vm_mp3PauseByFile\n");
        vm_set_call_result(0);
    }
    else if (idx == 16)
    {
        DEBUG_PRINT("[call]vm_mp3ResumeByFile\n");
        vm_set_call_result(0);
    }
    else if (idx == 17)
    {
        DEBUG_PRINT("[call]vm_mp3StopByFile\n");
        vm_set_call_result(0);
    }
    else if (idx == 18)
    {
        DEBUG_PRINT("[call]vMAudioget_progress_time\n");
        vm_set_call_result(0);
    }
    else if (idx == 19)
    {
        DEBUG_PRINT("[call]vmMp3StreamInit\n");
        vm_set_call_result(0);
    }
    else if (idx == 20)
    {
        DEBUG_PRINT("[call]CB_AUD_StartPlay_Init\n");
        vm_set_call_result(0);
    }
    else if (idx == 21)
    {
        DEBUG_PRINT("[call]CB_AUD_StopPlay\n");
        vm_set_call_result(0);
    }
    else if (idx == 22)
    {
        DEBUG_PRINT("[call]CB_AUD_WriteVoiceData\n");
        vm_set_call_result(0);
    }
    else if (idx == 23)
    {
        DEBUG_PRINT("[call]vMStartAudioRecord_async\n");
        vm_set_call_result(0);
    }
    else if (idx == 24)
    {
        DEBUG_PRINT("[call]vMStopAudioRecord_async\n");
        vm_set_call_result(0);
    }
    else if (idx == 25)
    {
        DEBUG_PRINT("[call]vMSetAmrRecBS\n");
        vm_set_call_result(0);
    }
    else if (idx == 26)
    {
        DEBUG_PRINT("[call]vm_mp3PlayByFileEx\n");
        vm_set_call_result(0);
    }
    else if (idx == 27)
    {
        DEBUG_PRINT("[call]vMStartAudioRecordEx\n");
        vm_set_call_result(0);
    }
    else if (idx == 28)
    {
        DEBUG_PRINT("[call]vMStopAudioRecordEx\n");
        vm_set_call_result(0);
    }
    else if (idx == 29)
    {
        DEBUG_PRINT("[call]CB_AUD_StartPlay_InitEx\n");
        vm_set_call_result(0);
    }
    else if (idx == 30)
    {
        DEBUG_PRINT("[call]CB_AUD_StartPlayEx\n");
        vm_set_call_result(0);
    }
    else if (idx == 31)
    {
        DEBUG_PRINT("[call]CB_AUD_StopPlayEx\n");
        vm_set_call_result(0);
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
    else if ((idx == 2 || idx == 3) && vm_cbe_uses_fixed_base_manager_abi())
    {
        /* Mobile Rainbow sub_424D8/sub_425C4: draw an image region to the
         * implicit screen buffer. Slot 2 uses the transparent-pixel path. */
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); /* image */
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); /* source x */
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3); /* source y */
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4); /* width */
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        int height = (int)(int16_t)(vm_get_var(sp) & 0xffff);
        int dstX = (int)(int16_t)(vm_get_var(sp + 4) & 0xffff);
        int dstY = (int)(int16_t)(vm_get_var(sp + 8) & 0xffff);
        vm_lcd_call_draw_image_clip_ex(tmp1,
                                       (int)(int16_t)(tmp2 & 0xffff),
                                       (int)(int16_t)(tmp3 & 0xffff),
                                       (int)(int16_t)(tmp4 & 0xffff),
                                       height, dstX, dstY, idx == 3);
        vm_set_call_result(0);
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
        tmp2 = (g_curKeyDownState & tmp1) != 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 13)
    {
        // printf("[call]GAME_isKeyHold\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        tmp2 = (g_curKeyState & tmp1) != 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
    }
    else if (idx == 15 && vm_cbe_uses_fixed_base_manager_abi())
    {
        /* Mobile Rainbow sub_422EE stores x/y and x+w/y+h as the active
         * drawing clip. Host rendering already clips against the LCD bounds. */
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        printf("[info][cbe] gameold_clip_rect x=%d y=%d w=%d h=%d\n",
               (int)tmp1, (int)tmp2, (int)tmp3, (int)tmp4);
        vm_set_call_result(tmp2 + tmp4);
    }
    else if (idx == 16 && vm_cbe_uses_fixed_base_manager_abi())
    {
        /* Mobile Rainbow sub_4213C: image height. */
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_set_call_result(tmp1 ? vm_get_var_short(tmp1 + 6) : 0);
    }
    else if (idx == 17 && vm_cbe_uses_fixed_base_manager_abi())
    {
        /* Mobile Rainbow sub_42148: image width. */
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_set_call_result(tmp1 ? vm_get_var_short(tmp1 + 4) : 0);
    }
    else if (idx == 18 && vm_cbe_uses_fixed_base_manager_abi())
    {
        /* Mobile Rainbow sub_4216A is RGB888 -> RGB565 conversion only. */
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        u16 color = (u16)(((tmp1 & 0xf8) << 8) |
                          ((tmp2 & 0xfc) << 3) |
                          ((tmp3 & 0xf8) >> 3));
        vm_set_call_result(color);
    }
    else if (idx == 24)
    {
        DEBUG_PRINT("[call]GetStreamDataFormRes\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_GetStreamDataFormRes(tmp1, tmp2, tmp3, tmp4);
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp5);
        vm_note_stream_data_result("game_old", lastAddress, tmp1, tmp2, tmp3, tmp4, tmp5);
    }
    else if (idx == 51)
    {
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        tmp5 = vm_get_var(sp + 0x0);
        sp = vm_get_var(sp + 0x4);
        vm_set_call_result(vm_cd_rect_point(tmp1, tmp2, tmp3, tmp4, tmp5, sp));
    }
    else if (idx == 58)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_math_sqrt_result(tmp1);
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
        vm_screen_root_exit_cancel("old_screen_change");
        vm_set_var(VM_SCREEN_nextSubTScreen_ADDRESS, tmp1);
        tmp2 = vm_get_var(tmp1 + 8);
        DEBUG_PRINT("[call]SCREEN_ChangeScreen(%x)\n", tmp1);
        tmp1 = VM_SCREEN_isInQuit_ADDRESS;
        tmp2 = 0;
        vm_set_var(VM_SCREEN_isInQuit_ADDRESS, tmp2);
        screenStructChange = 1;
        g_screenExitMode = VM_SCREEN_EXIT_DESTROY;
        g_screenRemovedWithoutNext = 0;
        g_screenEnterExistingNoCallback = 0;
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
            entry0 = vm_get_var(screenPtr + 0x00);
            entry4 = vm_get_var(screenPtr + 0x04);
            entry8 = vm_get_var(screenPtr + 0x08);
            entry12 = vm_get_var(screenPtr + 0x0c);
            entry16 = vm_get_var(screenPtr + 0x10);
            entry20 = vm_get_var(screenPtr + 0x14);
            entry24 = vm_get_var(screenPtr + 0x18);
        }
        g_screenLoadResourcePendingScreen = 0;
        g_screenLoadResourcePendingParam = 0;
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
        vm_set_call_result(g_curKeyDownState);
    }
    else if (idx == 76 && vm_cbe_uses_fixed_base_manager_abi())
    {
        /* Mobile Rainbow sub_1F4552: initialize the picture-library object.
         * It allocates a 240-pixel scanline, a resource-id array, and a
         * picture-pointer array, then installs methods at +0x18..+0x50. */
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        u32 count = tmp2 & 0xffff;
        u32 scanline = vm_malloc(240 * 2);
        u32 resourceIds = vm_malloc(count * 2);
        u32 pictures = vm_malloc(count * 4);
        if (scanline)
            vm_try_write_zero(scanline, 240 * 2);
        if (resourceIds)
            vm_try_write_zero(resourceIds, count * 2);
        if (pictures)
            vm_try_write_zero(pictures, count * 4);
        vm_set_var(tmp1 + 0x00, scanline);
        vm_set_var_short(tmp1 + 0x08, (u16)count);
        vm_set_var(tmp1 + 0x0c, resourceIds);
        vm_set_var(tmp1 + 0x10, pictures);
        vm_set_var_short(tmp1 + 0x14, 0);
        for (u32 method = 0; method < VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_COUNT; ++method)
            vm_set_var(tmp1 + 0x18 + method * 4,
                       VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_ADDRESS + method * 4);
        printf("[info][cbe] gameold_picture_init context=%08x capacity=%u "
               "scanline=%08x ids=%08x pictures=%08x\n",
               tmp1, count, scanline, resourceIds, pictures);
        vm_set_call_result(scanline && resourceIds && pictures ? 1 : 0);
    }

    else if (idx == 80 && vm_cbe_uses_fixed_base_manager_abi())
    {
        /* Mobile Rainbow sub_1F688A: initialize a clipped region set. */
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); /* object */
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); /* packed x/y */
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3); /* packed w/h */
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4); /* owner A */
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        u32 ownerB = vm_get_var(sp);
        u32 capacity = vm_get_var(sp + 4);
        u32 entries = capacity ? vm_malloc(capacity * 4) : 0;
        if (entries)
            vm_try_write_zero(entries, capacity * 4);
        for (u32 i = 0; i < capacity && entries; ++i)
        {
            u32 rect = vm_malloc(8);
            if (rect)
                vm_try_write_zero(rect, 8);
            vm_set_var(entries + i * 4, rect);
        }
        vm_set_var(tmp1 + 0x04, 0);
        vm_set_var(tmp1 + 0x08, capacity);
        vm_set_var(tmp1 + 0x0c, entries);
        vm_set_var(tmp1 + 0x10, tmp4);
        vm_set_var(tmp1 + 0x14, ownerB);
        vm_set_var(tmp1 + 0x18, tmp2);
        vm_set_var(tmp1 + 0x1c, tmp3);
        vm_set_var(tmp1 + 0x20, 0);
        vm_set_var(tmp1 + 0x24, 0);
        for (u32 method = 0; method < VM_FIXED_BASE_GAMEOLD_REGION_FUNC_COUNT; ++method)
            vm_set_var(tmp1 + 0x28 + method * 4,
                       VM_FIXED_BASE_GAMEOLD_REGION_FUNC_ADDRESS + method * 4);
        if (entries && capacity)
        {
            u32 first = vm_get_var(entries);
            vm_set_var(first + 0, tmp2);
            vm_set_var(first + 4, tmp3);
            vm_set_var(tmp1 + 0x04, 1);
        }
        printf("[info][cbe] gameold_region_init context=%08x capacity=%u "
               "bounds=%08x/%08x entries=%08x\n",
               tmp1, capacity, tmp2, tmp3, entries);
        vm_set_call_result(entries ? 1 : 0);
    }

    else if (idx == 81)
    {
        // DreamFactory_DataPackage = 0;
        tmp1 = 0;
        vm_set_var(VM_DreamFactory_DataPackage_ADDRESS, tmp1);
        // MemoryBlockPtr = (int (*)())getMemoryBlockPtr();
        // DreamFactory_MemoryBlock = MemoryBlockPtr;
        tmp1 = VM_MemoryBlock_PTR_ADDRESS;
        vm_set_var(VM_DreamFactory_MemoryBlock_ADDRESS, tmp1);
        DEBUG_PRINT("[call]initDreamFactoryEngine\n");
    }
    else if (idx == 82)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_set_var(VM_DreamFactory_DataPackage_ADDRESS, tmp1);
        DEBUG_PRINT("[call]DF_SetDataPackage(%x)\n", tmp1);
    }
    else if (idx == 83)
    {
        DEBUG_PRINT("[call]DF_GetDataPackage\n");
        tmp1 = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_DF_WriteShort(tmp1, tmp2, tmp3);
        DEBUG_PRINT("[call]DF_WriteShort\n");
    }
    else if (idx == 97)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        vm_DF_WriteInt(tmp1, tmp2, tmp3);
        DEBUG_PRINT("[call]DF_WriteInt\n");
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
        DEBUG_PRINT("[call]DF_ReadString2\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_ReadString2(tmp1, tmp2);
    }
    else if (idx == 103)
    {
        DEBUG_PRINT("[call]DF_GetMemoryBlock\n");
        vm_DF_GetMemoryBlock();
    }
    else if (idx == 104)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_math_df_sin_result(tmp1);
    }
    else if (idx == 105)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_math_df_cos_result(tmp1);
    }
    else if (idx == 106)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_math_df_degree_result(tmp1, tmp2);
    }
    else if (idx == 107)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_math_df_collection_test_result(tmp1, tmp2, tmp3, tmp4);
    }
    else if (idx == 108)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_math_df_swap_val_result(tmp1, tmp2);
    }
    else if (idx == 109)
    {
        DEBUG_PRINT("[call]DF_GetFormatString\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_readStringByPtr(tmp1, cbeTextString);
        vm_DF_GetFormatString();
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp5);
        vm_autotest_note_format_preview("df109", lastAddress, tmp5,
                                        (const char *)cbeTextString,
                                        tmp2, tmp3);
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
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 139)
    {
        vm_math_rand_result();
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
        vm_autotest_note_attr_value_write("gameold_strncpy", tmp1, tmp3);
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 142)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_strcpy(tmp1, tmp2);
        if (g_autotestEnabled && tmp1 != 0)
        {
            vm_readStringByPtr(tmp1, sprintfBuff);
            vm_autotest_note_attr_value_write("gameold_strcpy", tmp1,
                                              (u32)strlen((char *)sprintfBuff) + 1);
        }
    }
    else if (idx == 143)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (tmp1 && tmp2)
        {
            int dstLen = vm_strlen(tmp1);
            u32 copied = vm_guest_strcpy(tmp1 + dstLen, tmp2);
            vm_autotest_note_attr_value_write("gameold_strcat", tmp1 + dstLen,
                                              copied + 1);
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 144)
    {
        printf("[call]atol\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_readStringByPtr(tmp1, cbeTextString);
        tmp1 = (u32)strtol((char *)cbeTextString, NULL, 10);
        vm_set_call_result(tmp1);
    }

    // result 区 (从 a1+144 开始)
    else if (idx == 145)
    {
        printf("[call]memmove\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp1 && tmp2 && tmp3)
        {
            u8 *moveBuf = malloc(tmp3);
            if (moveBuf)
            {
                if (uc_mem_read(MTK, tmp2, moveBuf, tmp3) == UC_ERR_OK)
                    uc_mem_write(MTK, tmp1, moveBuf, tmp3);
                free(moveBuf);
            }
        }
        vm_set_call_result(tmp1);
    }
    else if (idx == 146)
    {
        printf("[call]atoi\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_readStringByPtr(tmp1, cbeTextString);
        tmp1 = (u32)atoi((char *)cbeTextString);
        vm_set_call_result(tmp1);
    }
    else if (idx == 147)
    {
        printf("[call]BILLING_GetPayNumByAppId\n");
        vm_set_call_result(0);
    }
    else if (idx == 148)
    {
        printf("[call]BILLING_GetRemainDay\n");
        vm_set_call_result(0);
    }
    else if (idx == 149)
    {
        printf("[call]BILLING_Pay\n");
        vm_set_call_result(0);
    }
    else if (idx == 150)
    {
        printf("[call]BILLING_PayMoreTimes\n");
        vm_set_call_result(0);
    }
    else if (idx == 151)
    {
        printf("[call]BILLING_IsRegisterBillingInfo\n");
        vm_set_call_result(0);
    }
    else if (idx == 152)
    {
        printf("[call]BILLING_RegisterBillingInfo\n");
        vm_set_call_result(0);
    }
    else if (idx == 153)
    {
        printf("[call]BILLING_SetBillingStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 154)
    {
        printf("[call]BILLING_GetBillingStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 155)
    {
        printf("[call]BILLING_IsNeedPay\n");
        vm_set_call_result(1);
    }
    else if (idx == 156)
    {
        printf("[call]BILLING_IsInTryStatus\n");
        vm_set_call_result(0);
    }
    else if (idx == 157)
    {
        printf("[call]Game_OpenBillingPromptWin\n");
        vm_set_call_result(0);
    }
    else if (idx == 158)
    {
        printf("[call]vMstricmp\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_readStringByPtr(tmp1, cbeTextString);
        vm_readStringByPtr(tmp2, sprintfBuff);
        tmp1 = (u32)strcasecmp((char *)cbeTextString, (char *)sprintfBuff);
        vm_set_call_result(tmp1);
    }
    else
    {
        u32 lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        printf("[impl]vmGameOldManager idx=%u r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x\n",
               idx, tmp1, tmp2, tmp3, tmp4, lr);
        assert(0);
    }
    // bx lr实现
gameold_func_return:
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_ReleasePackage(tmp1, tmp2);
    }
    else if (idx == 3)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_DF_DataPackage_LoadFromTResource(tmp1, tmp2);
        printf("[call]DF_DataPackage_LoadFromTResource\n");
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_DF_DataPackage_LoadFormTCard(tmp1);
        printf("[call]DF_DataPackage_LoadFormTCard\n");
    }
    else if (idx == 5)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        printf("[info][cbe] df_do_loading package=%08x source=%08x mode=%u\n",
               tmp1, tmp2, tmp3);
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
        DEBUG_PRINT("[call]DF_DataPackage_GetFile\n");
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
        if (vm_cbe_uses_fixed_base_manager_abi())
        {
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            vm_DF_DataPackage_LoadFormTCardEx(tmp1, tmp2, tmp3);
            printf("[call]DF_DataPackage_LoadFormTCardEx\n");
        }
        else
        {
            vm_DF_DataPackage_InitTxt(tmp1, tmp2);
            printf("[call]DF_DataPackage_InitTxt\n");
        }
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        if (tmp2)
            vm_dl_set_loaded_context(g_vmDlCurrAppId, tmp1 ? vm_get_var(tmp1) : 0);
        else if (tmp1)
            vm_set_var(tmp1, vm_dl_get_loaded_context(g_vmDlCurrAppId));
        vm_set_call_result(0);
    }
    else if (idx == 1)
    {
        printf("[call]vlDlSetContextAddress\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        if (tmp3)
            vm_dl_set_loaded_context((u16)tmp1, tmp2 ? vm_get_var(tmp2) : 0);
        else if (tmp2)
            vm_set_var(tmp2, vm_dl_get_loaded_context((u16)tmp1));
        vm_set_call_result(0);
    }
    else if (idx == 2)
    {
        printf("[call]vlDlUnLoadCurrApp\n");
        vm_dl_unload_loaded_app(g_vmDlCurrAppId);
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        printf("[call]vlDlUnLoadApp\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_dl_unload_loaded_app((u16)tmp1);
        vm_set_call_result(0);
    }
    else if (idx == 4)
    {
        printf("[call]vmDlParseAndCopy\n");
        vm_set_call_result(0xffffffffu);
    }
    else if (idx == 5)
    {
        printf("[call]vlDlLoadApp\n");
        vm_set_call_result(0);
    }
    else if (idx == 6)
    {
        printf("[call]vlDlLoadAppEx\n");
        vm_set_call_result(0);
    }
    else if (idx == 7)
    {
        printf("[call]vlDlAppIsInDl\n");
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_set_call_result(vm_dl_find_loaded_index_by_app_id((u16)tmp1) >= 0 ? 1 : 0);
    }
    else if (idx == 8)
    {
        printf("[call]vmGetcurrInnerAppId\n");
        vm_set_call_result((u32)g_vmDlCurrAppId);
    }
    else if (idx == 9)
    {
        printf("[call]CBInnerInit_qqIn\n");
        vm_set_call_result(0);
    }
    else if (idx == 10)
    {
        printf("[call]VmGetCBEInfoByFileName\n");
        vm_set_call_result(0);
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

    u32 tmp1, tmp2, tmp3, lr;
    u32 idx = (address - VM_DL_PAY_FUNC_LIST_ADDRESS) / 4;

    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (idx == 7)
    {
        snprintf((char *)cbeTextString, mySizeOf(cbeTextString), "111111111111111");
        tmp3 = strlen((char *)cbeTextString) + 1;
        if (tmp2 && tmp2 < tmp3)
            tmp3 = tmp2;
        if (tmp1 && tmp3 > 0)
        {
            if (tmp3 <= strlen((char *)cbeTextString))
                cbeTextString[tmp3 - 1] = 0;
            uc_mem_write(MTK, tmp1, cbeTextString, tmp3);
        }
        vm_set_call_result(0);
    }
    else
    {
        vm_set_call_result(0);
    }

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

static void hook_vm_native_dispatch_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_native_dispatch_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_native_system_time_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_native_system_time_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_fixed_base_manager_init_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_fixed_base_manager_init_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_fixed_base_gameold_object_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_fixed_base_gameold_object_func((u32)address);
    lastAddress = (u32)address;
}

static void hook_vm_fixed_base_gameold_region_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    hook_vm_fixed_base_gameold_region_func((u32)address);
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
    ADD_MANAGER_CODE_HOOK_RANGE(VM_FIXED_BASE_GAMEOLD_REGION_FUNC_ADDRESS,
                                VM_FIXED_BASE_GAMEOLD_REGION_FUNC_ADDRESS +
                                    VM_FIXED_BASE_GAMEOLD_REGION_FUNC_COUNT * 4 - 1,
                                hook_vm_fixed_base_gameold_region_code_callback);
    ADD_MANAGER_CODE_HOOK_RANGE(VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_ADDRESS,
                                VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_ADDRESS +
                                    VM_FIXED_BASE_GAMEOLD_OBJECT_FUNC_COUNT * 4 - 1,
                                hook_vm_fixed_base_gameold_object_code_callback);
    ADD_MANAGER_CODE_HOOK_RANGE(VM_FIXED_BASE_MANAGER_INIT_ADDRESS,
                                VM_FIXED_BASE_MANAGER_INIT_ADDRESS + VM_FIXED_BASE_MANAGER_INIT_COUNT * 4 - 1,
                                hook_vm_fixed_base_manager_init_code_callback);
    ADD_MANAGER_CODE_HOOK_RANGE(VM_NATIVE_DISPATCH_ADDRESS, VM_NATIVE_DISPATCH_ADDRESS + 3, hook_vm_native_dispatch_code_callback);
    ADD_MANAGER_CODE_HOOK_RANGE(VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS,
                                VM_NATIVE_SYSTEM_TIME_FUNC_ADDRESS + VM_NATIVE_SYSTEM_TIME_FUNC_COUNT * 4 - 1,
                                hook_vm_native_system_time_code_callback);
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

    vm_restore_main_r9_for_rom_code((u32)address);
    vm_autotest_note_startup_pc((u32)address & ~1u);
    vm_autotest_note_scene_actor_parser_pc((u32)address & ~1u);
    vm_autotest_note_backpack_parser_pc((u32)address & ~1u);
    vm_autotest_note_shop_parser_pc((u32)address & ~1u);
    vm_autotest_note_role_attr_page_pc((u32)address & ~1u);
    vm_note_mmgame_transfer_parser_pc((u32)address & ~1u);
    vm_note_stream_read_i16_pc((u32)address & ~1u);
    vm_note_net_wrapper_pc((u32)address & ~1u);
    vm_note_sce_load_entry_pc((u32)address & ~1u);
    vm_note_castlevania_wpay_pc((u32)address & ~1u);

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
    if (address == VM_LOG_NOOP_ADDRESS)
    {
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
