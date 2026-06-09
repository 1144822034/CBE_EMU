#define GDB_SERVER_SUPPORT_
#define GDI_LAYER_DEBUG_

#define DEBUG_PRINT(...) ((void)0)
#define TRACE_STARTUP_UI 0
#define TRACE_RESOURCE_IO 0
#define TRACE_LCD_TEXT 1
#define TRACE_LCD_SHAPES 1

#ifdef _WIN32
#include <direct.h>
#endif
#include <stdarg.h>

#include "main.h"
#include "lcd.h"
void vm_stdout_trace(const char *fmt, ...);
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
u32 screenStructChange = 0;
u32 screenStructNotifyLoadRes = 0;
u32 vmAddedScreen = 0;
static u32 g_screenStack[32];
static u32 g_screenStackModuleBase[32];
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
static int g_netTaskDispatchDepth = 0;
static int g_netTaskDispatchSlot = -1;
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
static const u32 VM_GAME_NET_BUSINESS_CALLBACK = 0x01012e4d;
static u8 g_lastStartupScreenState = 0xff;
static u32 g_lastStartupUpdateObj = 0xffffffff;
static u8 g_lastStartupProgress = 0xff;
static u8 g_lastStartupUpdateState = 0xff;
static u32 g_currentFontType = 0;
static u8 g_netLastHandledValid = 0;
static u32 g_netLastHandledResponseLen = 0;
static char g_netLastHandledSource[64];
static char g_netLastHandledSummary[512];

static uc_err add_manager_code_hooks(uc_engine *uc);
static void vm_net_trace(const char *fmt, ...);
static void vm_net_packet_trace(const char *fmt, ...);
static bool vm_host_file_exists(const char *path);
static void vm_net_trace_title_login_state(const char *label);
static void vm_net_trace_title_child_manager_call(const char *label, u32 pc);
static void vm_net_trace_title_login_dispatch(const char *label, u32 pc);
static void vm_net_trace_title_role_path(const char *label, u32 pc);
static void vm_net_trace_title_rolelist_reader_methods(const char *label, u32 pc);
static void vm_net_trace_title_role_select_action(const char *label, u32 pc);
void vm_net_trace_title_login_write(uint64_t address, uint32_t size, int64_t value);
void vm_net_trace_shared_event_owner_write(uint64_t address, uint32_t size, int64_t value);
void vm_net_trace_current_net_object_write(uint64_t address, uint32_t size, int64_t value);
void vm_net_trace_scene_dispatch_gate_write(uint64_t address, uint32_t size, int64_t value);
void vm_net_trace_scene_loading_owner_write(uint64_t address, uint32_t size, int64_t value);
static void vm_format_trace_bytes_hex(const u8 *bytes, u32 count, char *out, size_t outCap);
static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
static uc_err scheduler_dispatch_net_tasks(void);
static uc_err scheduler_flush_post_vm_business_send_ready(const char *reason);

void vm_stdout_trace(const char *fmt, ...)
{
    static u8 s_session_started = 0;
    FILE *fp;
    va_list args;

#ifdef _WIN32
    _mkdir("logs");
#endif
    fp = fopen("logs/stdout_trace.log", "ab");
    if (!fp)
        return;
    if (!s_session_started)
    {
        fprintf(fp, "\n==== session_start channel=stdout ====\n");
        s_session_started = 1;
    }
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

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

static bool vm_trace_progress_strip_region_overlap(int x, int y, int w, int h)
{
    const int regionLeft = 16;
    const int regionTop = 200;
    const int regionRight = 224;
    const int regionBottom = 286;

    if (w <= 0 || h <= 0)
        return false;

    return x < regionRight &&
           x + w > regionLeft &&
           y < regionBottom &&
           y + h > regionTop;
}

static bool vm_trace_progress_strip_scene_settled(void)
{
    u8 sceneTickGate3 = 0;
    u8 sceneTickGate4 = 0;
    u8 load0 = 0;
    u8 load1 = 0;
    u8 load2 = 0;

    if (!Global_R9)
        return false;

    uc_mem_read(MTK, Global_R9 + 23655, &sceneTickGate3, 1);
    uc_mem_read(MTK, Global_R9 + 23656, &sceneTickGate4, 1);
    uc_mem_read(MTK, Global_R9 + 23673, &load0, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &load1, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &load2, 1);

    return sceneTickGate3 == 1 &&
           sceneTickGate4 == 1 &&
           load0 == 0 &&
           load1 == 0 &&
           load2 == 0;
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

static void vm_trace_progress_strip_wrapper_entry(const char *label, u32 pc)
{
    static u32 s_progressStripWrapperTraceCount = 0;
    static u32 s_progressStripWrapperLastTick = 0xffffffffu;
    u32 lr = 0;
    u32 image = 0;
    u32 srcX = 0;
    u32 srcY = 0;
    u32 width = 0;
    u32 height = 0;
    u32 dstX = 0;
    u32 dstY = 0;
    u32 sp = 0;

    if (label == NULL)
        return;
    if (!vm_trace_progress_strip_scene_settled())
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &image);
    uc_reg_read(MTK, UC_ARM_REG_R1, &srcX);
    uc_reg_read(MTK, UC_ARM_REG_R2, &srcY);
    uc_reg_read(MTK, UC_ARM_REG_R3, &width);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_mem_read(MTK, sp + 0x0, &height, 4);
    uc_mem_read(MTK, sp + 0x4, &dstX, 4);
    uc_mem_read(MTK, sp + 0x8, &dstY, 4);

    if (!vm_trace_progress_strip_region_overlap((int)(int16_t)(dstX & 0xffff),
                                                (int)(int16_t)(dstY & 0xffff),
                                                (int)(int16_t)(width & 0xffff),
                                                (int)(int16_t)(height & 0xffff)))
        return;

    {
        u32 caller = lr & ~1u;
        bool isCenterProgressStrip = (caller == 0x010044B2u);
        bool isLoadingGifWidget = (caller == 0x010460CAu);

        if (!isCenterProgressStrip && !isLoadingGifWidget)
            return;
    }

    if (s_progressStripWrapperTraceCount >= 48)
    {
        if (s_progressStripWrapperLastTick != g_schedulerTick)
        {
            s_progressStripWrapperLastTick = g_schedulerTick;
            vm_net_trace("trace_progress_strip_wrapper_tick label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u image=%08x dst=%d,%d\n",
                         label,
                         pc,
                         lr,
                         lr & ~1u,
                         lastAddress,
                         g_schedulerTick,
                         image,
                         vm_lcd_coord_from_reg(dstX),
                         vm_lcd_coord_from_reg(dstY));
        }
        return;
    }

    s_progressStripWrapperLastTick = g_schedulerTick;
    ++s_progressStripWrapperTraceCount;
    vm_net_trace("trace_progress_strip_wrapper label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u image=%08x src=%d,%d size=%d,%d dst=%d,%d count=%u\n",
                 label,
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 image,
                 vm_lcd_coord_from_reg(srcX),
                 vm_lcd_coord_from_reg(srcY),
                 vm_lcd_coord_from_reg(width),
                 vm_lcd_coord_from_reg(height),
                 vm_lcd_coord_from_reg(dstX),
                 vm_lcd_coord_from_reg(dstY),
                 s_progressStripWrapperTraceCount);
}

static void vm_trace_loading_gif_widget_draw_entry(u32 pc)
{
    static u32 s_loadingGifWidgetDrawTraceCount = 0;
    static u32 s_loadingGifWidgetDrawLastTick = 0xffffffffu;
    u32 lr = 0;
    u32 widget = 0;
    u32 argX = 0;
    u32 argY = 0;
    u32 argW = 0;
    u8 widgetFlag = 0;
    u8 widgetMode = 0;
    u8 globalGate = 0;
    u8 sceneGateA = 0;
    u8 sceneGateB = 0;
    u16 frameCounter = 0;
    u16 imageIndex = 0;
    int16_t overlayCounter = 0;

    if (!Global_R9)
        return;
    if (!vm_trace_progress_strip_scene_settled())
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &widget);
    uc_reg_read(MTK, UC_ARM_REG_R1, &argX);
    uc_reg_read(MTK, UC_ARM_REG_R2, &argY);
    uc_reg_read(MTK, UC_ARM_REG_R3, &argW);

    if (widget != Global_R9 + 0x60F4u)
        return;

    uc_mem_read(MTK, widget + 0x0A, &frameCounter, 2);
    uc_mem_read(MTK, widget + 0x0E, &imageIndex, 2);
    uc_mem_read(MTK, widget + 0x10, &widgetFlag, 1);
    uc_mem_read(MTK, widget + 0x12, &widgetMode, 1);
    uc_mem_read(MTK, Global_R9 + 0x5530, &globalGate, 1);
    uc_mem_read(MTK, Global_R9 + 0x5C67, &sceneGateA, 1);
    uc_mem_read(MTK, Global_R9 + 0x5C68, &sceneGateB, 1);
    uc_mem_read(MTK, Global_R9 + 0x9590, &overlayCounter, 2);

    if (s_loadingGifWidgetDrawTraceCount >= 64 &&
        s_loadingGifWidgetDrawLastTick == g_schedulerTick)
        return;

    s_loadingGifWidgetDrawLastTick = g_schedulerTick;
    if (s_loadingGifWidgetDrawTraceCount < 64)
        ++s_loadingGifWidgetDrawTraceCount;

    vm_net_trace("trace_loading_gif_widget_draw_entry pc=%08x lr=%08x caller=%08x last=%08x tick=%u widget=%08x flag10=%u gate5530=%u sceneGate=%u,%u overlayCounter=%d frame=%u imageIndex=%u mode12=%u args=%d,%d,%d activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 widget,
                 widgetFlag,
                 globalGate,
                 sceneGateA,
                 sceneGateB,
                 overlayCounter,
                 frameCounter,
                 imageIndex,
                 widgetMode,
                 vm_lcd_coord_from_reg(argX),
                 vm_lcd_coord_from_reg(argY),
                 vm_lcd_coord_from_reg(argW),
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_loadingGifWidgetDrawTraceCount);
}

static void vm_trace_lcd_shape(const char *apiName, int x, int y, int w, int h, u32 color)
{
#if TRACE_LCD_SHAPES
    static u32 s_progressStripShapeTraceCount = 0;
    if (apiName != NULL &&
        s_progressStripShapeTraceCount < 48 &&
        strstr(apiName, "Rect") != NULL &&
        vm_trace_progress_strip_scene_settled() &&
        vm_trace_progress_strip_region_overlap(x, y, w, h))
    {
        ++s_progressStripShapeTraceCount;
        vm_net_trace("trace_progress_strip_shape api=%s x=%d y=%d w=%d h=%d color=%08x last=%08x tick=%u count=%u\n",
                     apiName, x, y, w, h, color, lastAddress, g_schedulerTick, s_progressStripShapeTraceCount);
    }
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

static u32 vm_screen_stack_lookup_module_base(u32 screen)
{
    int existing = vm_screen_stack_find(screen);
    if (existing < 0)
        return 0;
    return g_screenStackModuleBase[(u32)existing];
}

static void vm_screen_stack_push(u32 screen, u32 moduleBase)
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

static bool vm_screen_stack_remove(u32 screen, u32 *newTop, u32 *newTopModuleBase)
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

static void vm_net_trace_enter_mode_gate(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 payMgr = 0;
    u32 payFn0 = 0;
    u32 payFn1 = 0;
    u32 payFn2 = 0;
    u32 flag21300 = 0;
    u32 flag21312 = 0;

    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    (void)uc_mem_read(MTK, Global_R9 + 19848, &payMgr, 4);
    if (payMgr)
    {
        (void)uc_mem_read(MTK, payMgr + 0, &payFn0, 4);
        (void)uc_mem_read(MTK, payMgr + 4, &payFn1, 4);
        (void)uc_mem_read(MTK, payMgr + 8, &payFn2, 4);
    }
    (void)uc_mem_read(MTK, Global_R9 + 21300, &flag21300, 1);
    (void)uc_mem_read(MTK, Global_R9 + 21312, &flag21312, 4);

    vm_net_trace("trace_enter_mode_gate label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " payMgr=%08x payFn0=%08x payFn1=%08x payFn2=%08x"
                 " flag21300=%u pending21312=%08x activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 payMgr,
                 payFn0,
                 payFn1,
                 payFn2,
                 flag21300,
                 flag21312,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_enter_mode_gate_result(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 r0 = 0;
    u32 payMgr = 0;

    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_mem_read(MTK, Global_R9 + 19848, &payMgr, 4);

    vm_net_trace("trace_enter_mode_gate_result label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " result=%08x payMgr=%08x activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 payMgr,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_wpay_update_gate(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 r0 = 0;
    u32 r4 = 0;
    u32 cbMgr = 0;
    u32 cb148 = 0;
    u32 auxMgr = 0;
    u32 auxCb356 = 0;
    u32 auxCb360 = 0;
    u32 flag23184 = 0;

    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    (void)uc_mem_read(MTK, Global_R9 + 19844, &cbMgr, 4);
    if (cbMgr)
        (void)uc_mem_read(MTK, cbMgr + 148, &cb148, 4);
    (void)uc_mem_read(MTK, Global_R9 + 19824, &auxMgr, 4);
    if (auxMgr)
    {
        (void)uc_mem_read(MTK, auxMgr + 356, &auxCb356, 4);
        (void)uc_mem_read(MTK, auxMgr + 360, &auxCb360, 4);
    }
    (void)uc_mem_read(MTK, Global_R9 + 23184, &flag23184, 1);

    vm_net_trace("trace_wpay_update_gate label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " r0=%08x r4=%08x cbMgr=%08x cb148=%08x auxMgr=%08x auxCb356=%08x auxCb360=%08x"
                 " flag23184=%u activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r4,
                 cbMgr,
                 cb148,
                 auxMgr,
                 auxCb356,
                 auxCb360,
                 flag23184,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_loading_flag_flow(const char *label, u32 pc, u32 lr, u32 arg0)
{
    u32 sceneObj = 0;
    u8 loadingFlagA = 0;
    u8 loadingFlagB = 0;
    u8 loadingFlagC = 0;
    u16 loadingMode = 0;
    u8 sceneLoad0 = 0;
    u8 sceneLoad4 = 0;
    u8 sceneLoad5 = 0;
    u8 sceneBusy = 0;
    u8 sceneResult = 0;

    if (!Global_R9)
        return;

    uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    uc_mem_read(MTK, Global_R9 + 23673, &loadingFlagA, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &loadingFlagB, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &loadingFlagC, 1);
    uc_mem_read(MTK, Global_R9 + 23682, &loadingMode, 2);

    if (sceneObj)
    {
        uc_mem_read(MTK, sceneObj + 2400, &sceneLoad0, 1);
        uc_mem_read(MTK, sceneObj + 2404, &sceneLoad4, 1);
        uc_mem_read(MTK, sceneObj + 2405, &sceneLoad5, 1);
        uc_mem_read(MTK, sceneObj + 2412, &sceneBusy, 1);
        uc_mem_read(MTK, sceneObj + 1208, &sceneResult, 1);
    }

    vm_net_trace("trace_loading_flags label=%s pc=%08x lr=%08x arg0=%08x loadFlags=%u,%u,%u loadMode=%u sceneObj=%08x scene2400=%u scene2404=%u scene2405=%u scene2412=%u sceneResult=%u tick=%u last=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 arg0,
                 loadingFlagA,
                 loadingFlagB,
                 loadingFlagC,
                 loadingMode,
                 sceneObj,
                 sceneLoad0,
                 sceneLoad4,
                 sceneLoad5,
                 sceneBusy,
                 sceneResult,
                 g_schedulerTick,
                 lastAddress);
}

static void vm_net_trace_loading_flag_change_if_needed(u32 pc)
{
    static u8 s_prevA = 0;
    static u8 s_prevB = 0;
    static u8 s_prevC = 0;
    static u16 s_prevMode = 0;
    static u32 s_prevSceneObj = 0;
    static int s_seen = 0;
    u32 sceneObj = 0;
    u32 lr = 0;
    u8 loadingFlagA = 0;
    u8 loadingFlagB = 0;
    u8 loadingFlagC = 0;
    u16 loadingMode = 0;

    if (!Global_R9)
        return;

    uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    uc_mem_read(MTK, Global_R9 + 23673, &loadingFlagA, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &loadingFlagB, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &loadingFlagC, 1);
    uc_mem_read(MTK, Global_R9 + 23682, &loadingMode, 2);

    if (!s_seen)
    {
        s_prevA = loadingFlagA;
        s_prevB = loadingFlagB;
        s_prevC = loadingFlagC;
        s_prevMode = loadingMode;
        s_prevSceneObj = sceneObj;
        s_seen = 1;
        return;
    }

    if (s_prevA == loadingFlagA &&
        s_prevB == loadingFlagB &&
        s_prevC == loadingFlagC &&
        s_prevMode == loadingMode &&
        s_prevSceneObj == sceneObj)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    vm_net_trace("trace_loading_flags_change pc=%08x lr=%08x prev=%u,%u,%u/%u/%08x now=%u,%u,%u/%u/%08x tick=%u last=%08x activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 s_prevA,
                 s_prevB,
                 s_prevC,
                 s_prevMode,
                 s_prevSceneObj,
                 loadingFlagA,
                 loadingFlagB,
                 loadingFlagC,
                 loadingMode,
                 sceneObj,
                 g_schedulerTick,
                 lastAddress,
                 vmAddedScreen,
                 g_currentScreenThis);
    vm_net_trace_loading_flag_flow("change_watch", pc, lr, 0);

    s_prevA = loadingFlagA;
    s_prevB = loadingFlagB;
    s_prevC = loadingFlagC;
    s_prevMode = loadingMode;
    s_prevSceneObj = sceneObj;
}

static void vm_net_trace_scene_tick_gate_change_if_needed(u32 pc)
{
    static u8 s_prev3 = 0;
    static u8 s_prev4 = 0;
    static int s_seen = 0;
    u32 lr = 0;
    u8 gate3 = 0;
    u8 gate4 = 0;

    if (!Global_R9)
        return;

    uc_mem_read(MTK, Global_R9 + 23655, &gate3, 1);
    uc_mem_read(MTK, Global_R9 + 23656, &gate4, 1);

    if (!s_seen)
    {
        s_prev3 = gate3;
        s_prev4 = gate4;
        s_seen = 1;
        return;
    }

    if (s_prev3 == gate3 && s_prev4 == gate4)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    vm_net_trace("trace_scene_tick_gate_change pc=%08x lr=%08x prev=%u,%u now=%u,%u tick=%u last=%08x activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 s_prev3,
                 s_prev4,
                 gate3,
                 gate4,
                 g_schedulerTick,
                 lastAddress,
                 vmAddedScreen,
                 g_currentScreenThis);
    s_prev3 = gate3;
    s_prev4 = gate4;
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
            u32 activeModuleBase = 0;
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
                activeModuleBase = g_currentScreenModuleBase ? g_currentScreenModuleBase : vm_screen_stack_lookup_module_base(vmAddedScreen);
                if (activeModuleBase)
                    vm_net_trace_title_login_state("pre_data_event_05017509");
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
                if (activeModuleBase)
                    vm_net_trace_title_login_state("post_data_event_05017509");
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

static bool vm_net_mock_get_object_string_field(const u8 *request, u32 requestLen, const char *field, char *value, u32 valueCap)
{
    u32 fieldLen = (u32)strlen(field);
    if (value && valueCap > 0)
        value[0] = 0;
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5 || value == NULL || valueCap == 0)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p + 2 > requestLen)
            return false;
        u16 valueLen = (u16)(((u16)request[p] << 8) | request[p + 1]);
        p += 2;
        if (p + valueLen > requestLen || valueLen < 2)
            return false;
        u16 blobLen = (u16)(((u16)request[p] << 8) | request[p + 1]);
        p += 2;
        if ((u32)blobLen + 2 != valueLen || p + blobLen > requestLen)
            return false;

        u32 copyLen = SDL_min((u32)blobLen, valueCap - 1);
        memcpy(value, request + p, copyLen);
        value[copyLen] = 0;
        return true;
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

static bool vm_net_mock_get_wt_header_kind_subtype(const u8 *request, u32 requestLen, u8 *kind, u8 *subtype)
{
    if (request == NULL || requestLen < 8)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;
    if (kind)
        *kind = request[5];
    if (subtype)
        *subtype = request[6];
    return true;
}

static bool vm_net_mock_request_contains_object(const u8 *request, u32 requestLen, u8 major, u8 kind, u8 subtype)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == major && object.kind == kind && object.subtype == subtype)
            return true;
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
static const char *vm_net_mock_actor_preview_image_name(u8 actorJob, u8 actorSex);

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

static u32 vm_net_mock_build_len_prefixed_resource_blob(
    const u8 *payload, u32 payloadLen, u8 *out, u32 outCap)
{
    if (payload == NULL || payloadLen == 0 || out == NULL)
        return 0;
    if (payloadLen + 4 > outCap)
        return 0;

    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    memcpy(out + 4, payload, payloadLen);
    return payloadLen + 4;
}

static bool vm_net_mock_string_has_non_ascii(const char *text)
{
    if (text == NULL)
        return false;
    while (*text)
    {
        if ((u8)*text >= 0x80)
            return true;
        ++text;
    }
    return false;
}

static bool vm_net_mock_named_payload_looks_like_resource_stream(
    const char *payloadName, u8 requestType, const u8 *payload, u32 payloadLen)
{
    u32 declaredLen = 0;

    if (payload == NULL || payloadLen == 0)
        return false;
    if (requestType != 1)
        return true;
    if (payloadName == NULL || payloadName[0] == 0)
        return true;
    if (strchr(payloadName, '.') != NULL)
        return true;
    if (!vm_net_mock_string_has_non_ascii(payloadName))
        return true;
    if (payloadLen < 4)
        return false;

    declaredLen = (u32)payload[0] |
                  ((u32)payload[1] << 8) |
                  ((u32)payload[2] << 16) |
                  ((u32)payload[3] << 24);
    return declaredLen > 0 && declaredLen <= payloadLen - 4;
}

static u32 vm_net_mock_load_named_update_payload(const char *payloadName, u8 *out, u32 outCap)
{
    if (payloadName == NULL || payloadName[0] == 0 || out == NULL || outCap == 0)
        return 0;

    char path[384];
    snprintf(path, sizeof(path), "JHOnlineData/%s", payloadName);
    return vm_net_mock_load_response_file(path, out, outCap);
}

static u32 vm_net_mock_load_resource_update_payload(
    const u8 *request, u32 requestLen, const char *requestedName, u8 *out, u32 outCap, const char **payloadName)
{
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);

    if (payloadName)
        *payloadName = (requestedName && requestedName[0]) ? requestedName : "MMORPGTempcbm";

    if (requestedName && requestedName[0])
    {
        if (strcmp(requestedName, "c00PenglaiXiandao_01") == 0)
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);

        if (strcmp(requestedName, "c_mock_missing_motion.actor") == 0 ||
            strcmp(requestedName, "mock_missing_motion.actor") == 0)
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);

        if (strcmp(requestedName, "mock_actor_image.gif") == 0)
            return vm_net_mock_build_minimal_actor_image_resource(out, outCap);

        u32 namedLen = vm_net_mock_load_named_update_payload(requestedName, out, outCap);
        if (namedLen > 0)
        {
            if (vm_net_mock_named_payload_looks_like_resource_stream(requestedName,
                                                                     haveRequestType ? requestType : 0,
                                                                     out,
                                                                     namedLen))
                return namedLen;
            vm_net_trace("mock_update_named_resource_reject_invalid_cache name=%s reqType=%u payloadLen=%u\n",
                         requestedName,
                         haveRequestType ? requestType : 0,
                         namedLen);
        }

        if ((!haveRequestType || requestType == 1) && outCap > 4)
        {
            u32 fallbackLen = vm_net_mock_load_update_payload(out + 4, outCap - 4);
            if (fallbackLen > 0)
            {
                u32 wrappedLen = vm_net_mock_build_len_prefixed_resource_blob(out + 4, fallbackLen, out, outCap);
                if (wrappedLen > 0)
                {
                    vm_net_trace("mock_update_named_resource_wrapper name=%s reqType=%u payloadLen=%u fileLen=%u\n",
                                 requestedName,
                                 haveRequestType ? requestType : 0,
                                 fallbackLen,
                                 wrappedLen);
                    return wrappedLen;
                }
            }
        }
    }

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

static void vm_net_trace(const char *fmt, ...)
{
    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_trace.log", "ab");
    if (fp == NULL)
        return;
    vm_trace_write_net_session_marker(fp);

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

static void vm_net_write_bytes_dump(FILE *fp, const char *tag, const u8 *data, u32 len)
{
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
}

static void vm_net_trace_bytes(const char *tag, const u8 *data, u32 len)
{
    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_trace.log", "ab");
    if (fp == NULL)
        return;
    vm_trace_write_net_session_marker(fp);
    vm_net_write_bytes_dump(fp, tag, data, len);
    fclose(fp);
}

static void vm_net_packet_trace_bytes(const char *tag, const u8 *data, u32 len)
{
    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_packets.log", "ab");
    if (fp == NULL)
        return;
    vm_trace_write_net_session_marker(fp);
    vm_net_write_bytes_dump(fp, tag, data, len);
    fclose(fp);
}

static void vm_net_packet_trace(const char *fmt, ...)
{
    static u8 s_packet_session_started = 0;

    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/net_packets.log", "ab");
    if (fp == NULL)
        return;
    if (!s_packet_session_started)
    {
        fprintf(fp, "\n==== session_start channel=net_packets ====\n");
        s_packet_session_started = 1;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

static void vm_net_packet_summary_append(char *out, size_t outCap, size_t *used, const char *fmt, ...)
{
    if (out == NULL || outCap == 0 || used == NULL || *used >= outCap)
        return;

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out + *used, outCap - *used, fmt, args);
    va_end(args);
    if (written < 0)
        return;
    size_t advance = (size_t)written;
    if (advance >= outCap - *used)
        *used = outCap - 1;
    else
        *used += advance;
}

static void vm_net_packet_build_request_summary(const u8 *request, u32 requestLen, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;

    if (request == NULL || requestLen == 0)
    {
        snprintf(out, outCap, "empty");
        return;
    }

    if (requestLen < 2 || request[0] != 'W' || request[1] != 'T')
    {
        snprintf(out, outCap, "raw len=%u", requestLen);
        return;
    }

    size_t used = 0;
    u8 headerKind = 0;
    u8 headerSubtype = 0;
    u32 offset = 4;
    u32 objectCount = 0;
    vm_net_mock_request_object object;

    (void)vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &headerKind, &headerSubtype);
    vm_net_packet_summary_append(out, outCap, &used, "WT len=%u hdr=%u/%u objs=",
                                 requestLen, headerKind, headerSubtype);

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u8 typeValue = 0;
        u8 resultValue = 0;
        u32 idValue = 0;
        bool hasType = vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &typeValue);
        bool hasResult = vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "result", &resultValue);
        bool hasId = vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &idValue);

        if (objectCount != 0)
            vm_net_packet_summary_append(out, outCap, &used, ",");
        if (objectCount >= 8)
        {
            vm_net_packet_summary_append(out, outCap, &used, "...");
            break;
        }

        vm_net_packet_summary_append(out, outCap, &used, "%u/%u/%u",
                                     (u32)object.major, (u32)object.kind, (u32)object.subtype);
        if (hasType || hasResult || hasId)
        {
            vm_net_packet_summary_append(out, outCap, &used, "(");
            if (hasType)
                vm_net_packet_summary_append(out, outCap, &used, "type=%u", (u32)typeValue);
            if (hasResult)
                vm_net_packet_summary_append(out, outCap, &used, "%sresult=%u", hasType ? "," : "", (u32)resultValue);
            if (hasId)
                vm_net_packet_summary_append(out, outCap, &used, "%sid=%u", (hasType || hasResult) ? "," : "", idValue);
            vm_net_packet_summary_append(out, outCap, &used, ")");
        }
        ++objectCount;
    }

    if (objectCount == 0)
        vm_net_packet_summary_append(out, outCap, &used, "<none>");
    vm_net_packet_summary_append(out, outCap, &used, " count=%u", objectCount);
}

static void vm_net_log_handled_packet(const char *source, const u8 *request, u32 requestLen, u32 responseLen)
{
    char summary[512];
    vm_net_packet_build_request_summary(request, requestLen, summary, sizeof(summary));
    vm_net_trace("handled_packet source=%s responseLen=%u %s\n",
                 source ? source : "unknown",
                 responseLen,
                 summary);
    snprintf(g_netLastHandledSource, sizeof(g_netLastHandledSource), "%s", source ? source : "unknown");
    snprintf(g_netLastHandledSummary, sizeof(g_netLastHandledSummary), "%s", summary);
    g_netLastHandledResponseLen = responseLen;
    g_netLastHandledValid = 1;
}

static void vm_net_packet_log_exchange(const u8 *request, u32 requestLen, const u8 *response, u32 responseLen)
{
    if (!g_netLastHandledValid)
        return;

    vm_net_packet_trace("游戏请求数据包\n");
    vm_net_packet_trace_bytes("send_payload", request, requestLen);
    vm_net_packet_trace("主机处理信息 tick=%u source=%s responseLen=%u %s\n",
                        g_schedulerTick,
                        g_netLastHandledSource,
                        g_netLastHandledResponseLen,
                        g_netLastHandledSummary);
    vm_net_packet_trace("主机响应数据包\n");
    vm_net_packet_trace_bytes("mock_response_payload", response, responseLen);
}

static void vm_net_log_unhandled_packet(const u8 *request, u32 requestLen)
{
    char summary[512];
    vm_net_packet_build_request_summary(request, requestLen, summary, sizeof(summary));
    vm_net_trace("unhandled_packet %s\n", summary);
    vm_net_trace_bytes("unhandled_packet_payload", request, requestLen);
    vm_net_packet_trace("游戏请求数据包\n");
    vm_net_packet_trace_bytes("send_payload", request, requestLen);
    vm_net_packet_trace("主机处理信息 unhandled_packet %s\n", summary);
    vm_net_packet_trace("主机响应数据包\n");
    vm_net_packet_trace("assert(0)\n");
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

static void vm_net_read_guest_ascii_label(u32 ptr, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (ptr == 0)
    {
        snprintf(out, outCap, "<null>");
        return;
    }
    if (vm_net_try_read_ascii(ptr, (u8 *)out, (u32)outCap))
        return;
    snprintf(out, outCap, "<nonascii:%08x>", ptr);
}

static void vm_net_read_guest_gbk_label(u32 ptr, char *out, size_t outCap)
{
    u8 raw[64];
    size_t i = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (ptr == 0)
    {
        snprintf(out, outCap, "<null>");
        return;
    }
    memset(raw, 0, sizeof(raw));
    if (uc_mem_read(MTK, ptr, raw, sizeof(raw) - 1) != UC_ERR_OK)
    {
        snprintf(out, outCap, "<bad:%08x>", ptr);
        return;
    }
    while (i < sizeof(raw) - 1 && raw[i] != 0)
        i++;
    raw[i] = 0;
    if (i == 0)
    {
        snprintf(out, outCap, "<empty>");
        return;
    }
    gbk_to_utf8(raw, out, outCap);
}

static void vm_net_read_guest_best_effort_label(u32 ptr, char *out, size_t outCap)
{
    char asciiBuf[96];

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;

    vm_net_read_guest_ascii_label(ptr, asciiBuf, sizeof(asciiBuf));
    if (strncmp(asciiBuf, "<nonascii:", 10) == 0)
    {
        vm_net_read_guest_gbk_label(ptr, out, outCap);
        if (out[0] != 0)
            return;
    }
    snprintf(out, outCap, "%s", asciiBuf);
}

static void vm_net_read_active_ui_name(char *out, size_t outCap)
{
    u32 uiObj = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (!Global_R9)
    {
        snprintf(out, outCap, "<no-r9>");
        return;
    }
    if (uc_mem_read(MTK, Global_R9 + 21676, &uiObj, 4) != UC_ERR_OK || uiObj == 0)
    {
        snprintf(out, outCap, "<no-ui>");
        return;
    }
    vm_net_read_guest_ascii_label(uiObj + 1141, out, outCap);
}

static void vm_net_read_active_ui_name_best_effort(char *out, size_t outCap)
{
    u32 uiObj = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (!Global_R9)
    {
        snprintf(out, outCap, "<no-r9>");
        return;
    }
    if (uc_mem_read(MTK, Global_R9 + 21676, &uiObj, 4) != UC_ERR_OK || uiObj == 0)
    {
        snprintf(out, outCap, "<no-ui>");
        return;
    }
    vm_net_read_guest_best_effort_label(uiObj + 1141, out, outCap);
}

static void vm_net_trace_resource_name_flow(const char *tag, u32 pc, u32 lr, u32 namePtr)
{
    char nameBuf[96];
    char uiName[96];
    vm_net_read_guest_ascii_label(namePtr, nameBuf, sizeof(nameBuf));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("%s pc=%08x lr=%08x last=%08x tick=%u namePtr=%08x name=%s uiName=%s\n",
                 tag, pc, lr, lastAddress, g_schedulerTick, namePtr, nameBuf, uiName);
}

static void vm_net_trace_resource_request_enqueue(const char *tag,
                                                  u32 pc,
                                                  u32 lr,
                                                  u32 requestType,
                                                  u32 namePtr,
                                                  u32 argA4,
                                                  u32 argA5,
                                                  u32 argA6,
                                                  u32 argA7,
                                                  u32 argA8)
{
    char nameBuf[96];
    char uiName[96];

    vm_net_read_guest_best_effort_label(namePtr, nameBuf, sizeof(nameBuf));
    vm_net_read_active_ui_name_best_effort(uiName, sizeof(uiName));
    vm_net_trace("%s pc=%08x lr=%08x last=%08x tick=%u reqType=%u namePtr=%08x name=%s a4=%08x a5=%08x a6=%08x a7=%08x a8=%08x uiName=%s activeScreen=%08x currentThis=%08x\n",
                 tag,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 requestType,
                 namePtr,
                 nameBuf,
                 argA4,
                 argA5,
                 argA6,
                 argA7,
                 argA8,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_update_request_prepare(u32 pc, u32 lr, u32 requestType)
{
    u32 pendingEntry = 0;
    u32 start = 0;
    u32 version = 0;
    u16 resourceType = 0;
    char nameBuf[96];
    char uiName[96];
    vm_net_read_guest_best_effort_label(0, nameBuf, sizeof(nameBuf));
    vm_net_read_active_ui_name_best_effort(uiName, sizeof(uiName));
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 38284, &pendingEntry, 4);
        uc_mem_read(MTK, Global_R9 + 38328, &version, 4);
    }
    if (pendingEntry)
    {
        uc_mem_read(MTK, pendingEntry, &resourceType, 2);
        uc_mem_read(MTK, pendingEntry + 52, &start, 4);
        vm_net_read_guest_best_effort_label(pendingEntry + 2, nameBuf, sizeof(nameBuf));
    }
    vm_net_trace("trace_update_request_prepare pc=%08x lr=%08x last=%08x tick=%u reqType=%u pending=%08x name=%s type=%u start=%u version=%u uiName=%s activeScreen=%08x currentThis=%08x\n",
                 pc, lr, lastAddress, g_schedulerTick, requestType, pendingEntry, nameBuf, (u32)resourceType, start, version, uiName, vmAddedScreen, g_currentScreenThis);
}

static void vm_net_trace_parse_actor_motion_entry(u32 pc, u32 lr, u32 arg0, u32 arg4, u32 arg8, u32 argC)
{
    char arg0Name[96];
    char arg4Name[96];
    char uiName[96];
    vm_net_read_guest_ascii_label(arg0, arg0Name, sizeof(arg0Name));
    vm_net_read_guest_ascii_label(arg4, arg4Name, sizeof(arg4Name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_parse_actor_motion_entry pc=%08x lr=%08x last=%08x tick=%u arg0=%08x name=%s arg4=%08x arg4Name=%s arg8=%08x argC=%08x uiName=%s\n",
                 pc, lr, lastAddress, g_schedulerTick, arg0, arg0Name, arg4, arg4Name, arg8, argC, uiName);
}

static void vm_net_trace_sub_10352ae_callsite(const char *tag, u32 pc, u32 lr, u32 r0, u32 r1, u32 r2, u32 stackArg0, u32 stackArg4, u32 stackArg8)
{
    char stack0Name[96];
    char uiName[96];
    vm_net_read_guest_ascii_label(stackArg0, stack0Name, sizeof(stack0Name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x stack0=%08x stack0Name=%s stack4=%08x stack8=%08x uiName=%s\n",
                 tag, pc, lr, lastAddress, g_schedulerTick, r0, r1, r2, stackArg0, stack0Name, stackArg4, stackArg8, uiName);
}

static void vm_net_trace_sub_1010228_entry(u32 pc, u32 lr)
{
    u32 srcObj = 0;
    u32 dstObj = 0;
    u32 field68 = 0;
    u8 flag5c74 = 0;
    u8 flag5c79 = 0;
    char copiedName[96];
    char uiName[96];

    copiedName[0] = 0;
    uiName[0] = 0;
    if (!Global_R9)
        return;

    uc_mem_read(MTK, Global_R9 + 23716, &srcObj, 4);
    uc_mem_read(MTK, Global_R9 + 23796, &dstObj, 4);
    uc_mem_read(MTK, Global_R9 + 23668, &flag5c74, 1);
    uc_mem_read(MTK, Global_R9 + 23673, &flag5c79, 1);
    if (srcObj)
    {
        uc_mem_read(MTK, srcObj + 36, &field68, 4);
        vm_net_read_guest_ascii_label(srcObj + 68, copiedName, sizeof(copiedName));
    }
    else
    {
        vm_net_read_guest_ascii_label(0, copiedName, sizeof(copiedName));
    }
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_sub_1010228_entry pc=%08x lr=%08x last=%08x tick=%u srcObj=%08x dstObj=%08x copiedName=%s field36=%08x flagsBefore=%u,%u uiName=%s activeScreen=%08x currentThis=%08x\n",
                 pc, lr, lastAddress, g_schedulerTick, srcObj, dstObj, copiedName, field68, flag5c74, flag5c79, uiName, vmAddedScreen, g_currentScreenThis);
}

static void vm_net_trace_sub_1010228_callsite(const char *label, u32 pc)
{
    u32 srcObj = 0;
    u32 dstObj = 0;
    u32 field36 = 0;
    u32 lr = 0;
    u8 flag5c74 = 0;
    u8 flag5c79 = 0;
    char copiedName[96];
    char uiName[96];

    copiedName[0] = 0;
    uiName[0] = 0;
    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, Global_R9 + 23716, &srcObj, 4);
    uc_mem_read(MTK, Global_R9 + 23796, &dstObj, 4);
    uc_mem_read(MTK, Global_R9 + 23668, &flag5c74, 1);
    uc_mem_read(MTK, Global_R9 + 23673, &flag5c79, 1);
    if (srcObj)
    {
        uc_mem_read(MTK, srcObj + 36, &field36, 4);
        vm_net_read_guest_ascii_label(srcObj + 68, copiedName, sizeof(copiedName));
    }
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_sub_1010228_callsite label=%s pc=%08x lr=%08x last=%08x tick=%u srcObj=%08x dstObj=%08x copiedName=%s field36=%08x flagsBefore=%u,%u uiName=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 srcObj,
                 dstObj,
                 copiedName,
                 field36,
                 flag5c74,
                 flag5c79,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_screen_callback(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 targetW0 = 0;
    u32 targetW1 = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (r1)
    {
        (void)uc_mem_read(MTK, r1, &targetW0, 4);
        (void)uc_mem_read(MTK, r1 + 4, &targetW1, 4);
    }

    vm_net_trace("trace_title_screen_callback label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " routerObj=%08x target=%08x arg2=%08x method=%08x targetW0=%08x targetW1=%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 targetW0,
                 targetW1,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_login_state(const char *label)
{
    u32 base = g_currentScreenModuleBase;
    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);
    if (!base)
    {
        vm_net_trace("trace_title_login_state label=%s base=00000000 activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     label ? label : "?",
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
        return;
    }

    u8 state0 = 0;
    u8 focus = 0;
    u8 saveDefault = 0;
    u16 mode = 0;
    u16 sel0 = 0;
    u16 sel1 = 0;
    u32 modeObjPtr = 0;
    u32 modeObjCb0 = 0;
    u32 modeObjCb4 = 0;
    u32 modeObjCb8 = 0;
    u32 modeObjCb10 = 0;
    u32 modeObjCb14 = 0;
    u32 choiceObj1 = 0;
    u32 choiceObj2 = 0;
    u32 target48 = 0;
    u32 target4c = 0;
    u32 target50 = 0;
    u32 target54 = 0;
    u32 choiceObj1Cb14 = 0;
    u32 choiceObj2Cb14 = 0;
    u32 target48Cb14 = 0;
    u32 target4cCb14 = 0;
    u32 target50Cb14 = 0;
    u32 target54Cb14 = 0;
    u32 listPtr = 0;
    u8 listCount = 0;
    u32 roleFamily = 0;

    (void)uc_mem_read(MTK, base + 10728, &state0, 1);
    (void)uc_mem_read(MTK, base + 10730, &focus, 1);
    (void)uc_mem_read(MTK, base + 10735, &saveDefault, 1);
    (void)uc_mem_read(MTK, base + 10748, &mode, 2);
    (void)uc_mem_read(MTK, base + 10750, &sel0, 2);
    (void)uc_mem_read(MTK, base + 10752, &sel1, 2);
    (void)uc_mem_read(MTK, base + 10444, &modeObjPtr, 4);
    (void)uc_mem_read(MTK, base + 10792, &choiceObj1, 4);
    (void)uc_mem_read(MTK, base + 10796, &choiceObj2, 4);
    (void)uc_mem_read(MTK, base + 10800, &target48, 4);
    (void)uc_mem_read(MTK, base + 10804, &target4c, 4);
    (void)uc_mem_read(MTK, base + 10808, &target50, 4);
    (void)uc_mem_read(MTK, base + 10812, &target54, 4);
    (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
    (void)uc_mem_read(MTK, base + 11108, &roleFamily, 4);
    if (modeObjPtr)
    {
        (void)uc_mem_read(MTK, modeObjPtr + 0x00, &modeObjCb0, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x04, &modeObjCb4, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x08, &modeObjCb8, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x10, &modeObjCb10, 4);
        (void)uc_mem_read(MTK, modeObjPtr + 0x14, &modeObjCb14, 4);
    }
    if (choiceObj1)
        (void)uc_mem_read(MTK, choiceObj1 + 0x14, &choiceObj1Cb14, 4);
    if (choiceObj2)
        (void)uc_mem_read(MTK, choiceObj2 + 0x14, &choiceObj2Cb14, 4);
    if (target48)
        (void)uc_mem_read(MTK, target48 + 0x14, &target48Cb14, 4);
    if (target4c)
        (void)uc_mem_read(MTK, target4c + 0x14, &target4cCb14, 4);
    if (target50)
        (void)uc_mem_read(MTK, target50 + 0x14, &target50Cb14, 4);
    if (target54)
        (void)uc_mem_read(MTK, target54 + 0x14, &target54Cb14, 4);
    if (listPtr)
        (void)uc_mem_read(MTK, listPtr, &listCount, 1);

    vm_net_trace("trace_title_login_state label=%s base=%08x state0=%u focus=%u saveDefault=%u mode=%u sel=%u,%u"
                 " modeObj=%08x cb0=%08x cb4=%08x cb8=%08x cb10=%08x cb14=%08x"
                 " choice1=%08x cb14=%08x choice2=%08x cb14=%08x"
                 " target48=%08x target4c=%08x target50=%08x target54=%08x"
                 " targetCb14=%08x,%08x,%08x,%08x"
                 " listPtr=%08x listCount=%u roleFamily=%u"
                 " activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                 label ? label : "?",
                 base,
                 state0,
                 focus,
                 saveDefault,
                 (u32)mode,
                 (u32)sel0,
                 (u32)sel1,
                 modeObjPtr,
                 modeObjCb0,
                 modeObjCb4,
                 modeObjCb8,
                 modeObjCb10,
                 modeObjCb14,
                 choiceObj1,
                 choiceObj1Cb14,
                 choiceObj2,
                 choiceObj2Cb14,
                 target48,
                 target4c,
                 target50,
                 target54,
                 target48Cb14,
                 target4cCb14,
                 target50Cb14,
                 target54Cb14,
                 listPtr,
                 (u32)listCount,
                 roleFamily,
                 vmAddedScreen,
                 g_currentScreenThis,
                 g_schedulerTick,
                 lastAddress);
}

static void vm_net_trace_scene_owner_site(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 ownerObj = 0;
    u32 ownerState = 0;
    u32 ownerCb14 = 0;
    u32 ownerCb18 = 0;
    u32 ownerCb1c = 0;
    u32 ownerCb20 = 0;
    u32 ownerCb24 = 0;
    u32 ownerCb28 = 0;
    u32 ownerCb2c = 0;
    u32 ownerCb30 = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (Global_R9)
    {
        (void)uc_mem_read(MTK, Global_R9 + 38056, &ownerObj, 4);
        if (ownerObj)
        {
            (void)uc_mem_read(MTK, ownerObj + 12, &ownerState, 4);
            (void)uc_mem_read(MTK, ownerObj + 20, &ownerCb14, 4);
            (void)uc_mem_read(MTK, ownerObj + 24, &ownerCb18, 4);
            (void)uc_mem_read(MTK, ownerObj + 28, &ownerCb1c, 4);
            (void)uc_mem_read(MTK, ownerObj + 32, &ownerCb20, 4);
            (void)uc_mem_read(MTK, ownerObj + 36, &ownerCb24, 4);
            (void)uc_mem_read(MTK, ownerObj + 40, &ownerCb28, 4);
            (void)uc_mem_read(MTK, ownerObj + 44, &ownerCb2c, 4);
            (void)uc_mem_read(MTK, ownerObj + 48, &ownerCb30, 4);
        }
    }

    vm_net_trace("trace_scene_owner_site label=%s pc=%08x lr=%08x last=%08x tick=%u ownerSlot=%08x ownerObj=%08x ownerState=%u"
                 " cb14=%08x cb18=%08x cb1c=%08x cb20=%08x cb24=%08x cb28=%08x cb2c=%08x cb30=%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 Global_R9 ? (Global_R9 + 38056) : 0,
                 ownerObj,
                 ownerState,
                 ownerCb14,
                 ownerCb18,
                 ownerCb1c,
                 ownerCb20,
                 ownerCb24,
                 ownerCb28,
                 ownerCb2c,
                 ownerCb30,
                 vmAddedScreen,
                 g_currentScreenThis);
}

void vm_net_trace_title_login_write(uint64_t address, uint32_t size, int64_t value)
{
    typedef struct
    {
        u32 offset;
        u32 width;
        const char *label;
    } title_field_watch;

    static const title_field_watch watches[] = {
        {10728, 1, "state0"},
        {10730, 1, "focus"},
        {10735, 1, "saveDefault"},
        {10737, 1, "slotLimit"},
        {10748, 2, "mode"},
        {10750, 2, "sel0"},
        {10752, 2, "sel1"},
        {10754, 1, "initDone"},
        {10756, 2, "ui56"},
        {10758, 2, "ui58"},
        {10760, 2, "ui60"},
        {10762, 2, "ui62"},
        {10764, 2, "ui64"},
        {10788, 4, "listPtr"},
        {10800, 4, "target48"},
        {10804, 4, "target4c"},
        {10808, 4, "target50"},
        {10812, 4, "target54"},
        {11046, 1, "roleReady"},
        {11108, 4, "roleFamily"},
        {11124, 4, "activeRoleIndex"},
    };

    u32 base = g_currentScreenModuleBase;
    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);
    if (!base || size == 0)
        return;

    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    for (u32 i = 0; i < mySizeOf(watches); ++i)
    {
        u32 fieldAddr = base + watches[i].offset;
        u32 fieldEnd = fieldAddr + watches[i].width;
        if (writeEnd <= fieldAddr || writeStart >= fieldEnd)
            continue;

        u32 oldValue = 0;
        u32 lowBits = watches[i].width * 8;
        u32 fieldMask = lowBits >= 32 ? 0xFFFFFFFFu : ((1u << lowBits) - 1u);
        u32 newValue = (u32)value & fieldMask;
        (void)uc_mem_read(MTK, fieldAddr, &oldValue, watches[i].width);

        vm_net_trace("trace_title_login_write label=%s addr=%08x size=%u old=%08x new=%08x"
                     " writeAddr=%08x writeSize=%u raw=%08x activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     watches[i].label,
                     fieldAddr,
                     watches[i].width,
                     oldValue & fieldMask,
                     newValue,
                     writeStart,
                     size,
                     (u32)value,
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
    }
}

void vm_net_trace_title_role_workspace_write(uint64_t address, uint32_t size, int64_t value)
{
    typedef struct
    {
        u32 offset;
        u32 width;
        const char *label;
    } workspace_watch;

    static const workspace_watch watches[] = {
        {0x00, 1, "roleCount"},
        {0x68, 4, "role0.id"},
        {0xB0, 4, "role0.level"},
        {0x144, 1, "role0.sex"},
        {0x145, 1, "role0.job"},
        {0x7FC, 4, "selectedRoleIdShadow"},
    };

    u32 base = g_currentScreenModuleBase;
    u32 listPtr = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    char roleName[96];

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);
    if (!base || size == 0)
        return;

    (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
    if (!listPtr)
        return;

    memset(roleName, 0, sizeof(roleName));

    for (u32 i = 0; i < mySizeOf(watches); ++i)
    {
        u32 fieldAddr = listPtr + watches[i].offset;
        u32 fieldEnd = fieldAddr + watches[i].width;
        if (writeEnd <= fieldAddr || writeStart >= fieldEnd)
            continue;

        u32 oldValue = 0;
        u32 lowBits = watches[i].width * 8;
        u32 fieldMask = lowBits >= 32 ? 0xFFFFFFFFu : ((1u << lowBits) - 1u);
        u32 newValue = (u32)value & fieldMask;
        (void)uc_mem_read(MTK, fieldAddr, &oldValue, watches[i].width);
        vm_net_read_guest_gbk_label(listPtr + 0x48, roleName, sizeof(roleName));

        vm_net_trace("trace_title_role_workspace_write label=%s addr=%08x size=%u old=%08x new=%08x"
                     " writeAddr=%08x writeSize=%u raw=%08x listPtr=%08x roleName=%s"
                     " activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     watches[i].label,
                     fieldAddr,
                     watches[i].width,
                     oldValue & fieldMask,
                     newValue,
                     writeStart,
                     size,
                     (u32)value,
                     listPtr,
                     roleName[0] ? roleName : "<none>",
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
    }

    if (!(writeEnd <= listPtr + 0x48 || writeStart >= listPtr + 0x68))
    {
        vm_net_read_guest_gbk_label(listPtr + 0x48, roleName, sizeof(roleName));
        vm_net_trace("trace_title_role_workspace_write label=role0.name addr=%08x size=%u"
                     " writeAddr=%08x writeSize=%u raw=%08x listPtr=%08x roleName=%s"
                     " activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                     listPtr + 0x48,
                     0x20u,
                     writeStart,
                     size,
                     (u32)value,
                     listPtr,
                     roleName[0] ? roleName : "<none>",
                     vmAddedScreen,
                     g_currentScreenThis,
                     g_schedulerTick,
                     lastAddress);
    }
}

void vm_net_trace_shared_event_owner_write(uint64_t address, uint32_t size, int64_t value)
{
    u32 ownerSlot = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 oldValue = 0;
    u32 newValue = 0;

    if (!Global_R9 || size == 0)
        return;

    ownerSlot = Global_R9 + 38056;
    if (writeEnd <= ownerSlot || writeStart >= ownerSlot + 4)
        return;

    (void)uc_mem_read(MTK, ownerSlot, &oldValue, 4);
    newValue = (u32)value;
    vm_net_trace("trace_shared_event_owner_write slot=%08x old=%08x new=%08x"
                 " writeAddr=%08x writeSize=%u raw=%08x activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                 ownerSlot,
                 oldValue,
                 newValue,
                 writeStart,
                 size,
                 (u32)value,
                 vmAddedScreen,
                 g_currentScreenThis,
                 g_schedulerTick,
                 lastAddress);
}

static void vm_net_trace_title_child_manager_call(const char *label, u32 pc)
{
    u32 r0 = 0;
    u32 r1 = 0;
    u32 lr = 0;
    u32 childPtr = 0;
    u32 cb4 = 0;
    u32 cb8 = 0;
    u32 cbC = 0;
    u32 cb10 = 0;
    u32 kind = 0;
    u32 subtype = 0;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (r0)
    {
        (void)uc_mem_read(MTK, r0, &childPtr, 4);
    }
    if (childPtr)
    {
        (void)uc_mem_read(MTK, childPtr + 0x04, &cb4, 4);
        (void)uc_mem_read(MTK, childPtr + 0x08, &cb8, 4);
        (void)uc_mem_read(MTK, childPtr + 0x0C, &cbC, 4);
        (void)uc_mem_read(MTK, childPtr + 0x10, &cb10, 4);
    }
    if (r1)
    {
        (void)uc_mem_read(MTK, r1 + 4, &kind, 4);
        (void)uc_mem_read(MTK, r1 + 8, &subtype, 4);
    }

    vm_net_trace("trace_title_child_manager_call label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " mgr=%08x arg=%08x child=%08x cb4=%08x cb8=%08x cbC=%08x cb10=%08x"
                 " packetKind=%u packetSubtype=%u activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 childPtr,
                 cb4,
                 cb8,
                 cbC,
                 cb10,
                 kind,
                 subtype,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_business_dispatch_state(const char *label, u32 pc)
{
    static u32 s_businessDispatchTraceCount = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 lr = 0;
    u32 sceneObj = 0;
    u8 dispatchGate = 0;
    u32 objectCount = 0;
    u32 objectBase = 0;

    if (s_businessDispatchTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, 4);
        if (sceneObj)
            uc_mem_read(MTK, sceneObj + 0x164, &dispatchGate, 1);
        uc_mem_read(MTK, Global_R9 + 0x5590, &objectCount, 4);
        uc_mem_read(MTK, Global_R9 + 0x5598, &objectBase, 4);
    }

    ++s_businessDispatchTraceCount;
    vm_net_trace("trace_business_dispatch_state label=%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x r3=%08x sceneObj=%08x dispatchGate=%u objectCount=%u objectBase=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 sceneObj,
                 dispatchGate,
                 objectCount,
                 objectBase,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_businessDispatchTraceCount);
}

static void vm_net_trace_business_dispatch_item(u32 pc)
{
    static u32 s_businessDispatchItemTraceCount = 0;
    u32 entry = 0;
    u32 index = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u32 lr = 0;

    if (s_businessDispatchItemTraceCount >= 96)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R4, &entry);
    uc_reg_read(MTK, UC_ARM_REG_R5, &index);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (entry)
    {
        uc_mem_read(MTK, entry + 4, &kind, 4);
        uc_mem_read(MTK, entry + 8, &subtype, 4);
    }

    ++s_businessDispatchItemTraceCount;
    vm_net_trace("trace_business_dispatch_item pc=%08x lr=%08x last=%08x tick=%u index=%u entry=%08x kind=%u subtype=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 index,
                 entry,
                 kind,
                 subtype,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_businessDispatchItemTraceCount);
}

static void vm_net_trace_tasktypes_case13_stream(const char *label, u32 pc)
{
    static u32 s_tasktypesCase13TraceCount = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 lr = 0;
    u32 streamCursor = 0;
    u32 blobObj = 0;
    u32 blobPtr = 0;
    u32 blobLen = 0;
    u8 bytes[16] = {0};
    char hex[64] = {0};

    if (s_tasktypesCase13TraceCount >= 64)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (r5)
    {
        uc_mem_read(MTK, r5 + 0x400, &streamCursor, 4);
        uc_mem_read(MTK, r5 + 0x408, &blobObj, 4);
    }
    if (blobObj)
    {
        uc_mem_read(MTK, blobObj + 4, &blobPtr, 4);
        uc_mem_read(MTK, blobObj + 8, &blobLen, 4);
    }
    if (blobPtr)
        uc_mem_read(MTK, blobPtr + streamCursor, bytes, sizeof(bytes));
    vm_format_trace_bytes_hex(bytes, sizeof(bytes), hex, sizeof(hex));

    ++s_tasktypesCase13TraceCount;
    vm_net_trace("trace_tasktypes_case13_stream label=%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x r4=%u r5=%08x r6=%08x r7=%08x cursor=%u blobObj=%08x blobPtr=%08x blobLen=%u next=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r4,
                 r5,
                 r6,
                 r7,
                 streamCursor,
                 blobObj,
                 blobPtr,
                 blobLen,
                 hex,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_tasktypesCase13TraceCount);
}

static void vm_net_trace_title_login_dispatch(const char *label, u32 pc)
{
    u32 base = g_currentScreenModuleBase;
    u32 r0 = 0;
    u32 lr = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u32 loginStatePtr = 0;
    u8 resultByte = 0;
    u32 target48 = 0;
    u32 target4c = 0;
    u32 target50 = 0;

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (r0)
    {
        (void)uc_mem_read(MTK, r0 + 4, &kind, 4);
        (void)uc_mem_read(MTK, r0 + 8, &subtype, 4);
    }
    if (base)
    {
        (void)uc_mem_read(MTK, base + 10780, &loginStatePtr, 4);
        (void)uc_mem_read(MTK, base + 10800, &target48, 4);
        (void)uc_mem_read(MTK, base + 10804, &target4c, 4);
        (void)uc_mem_read(MTK, base + 10808, &target50, 4);
    }
    if (loginStatePtr)
        (void)uc_mem_read(MTK, loginStatePtr + 1, &resultByte, 1);

    vm_net_trace("trace_title_login_dispatch label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " packet=%08x kind=%u subtype=%u loginState=%08x resultByte=%u"
                 " target48=%08x target4c=%08x target50=%08x activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 kind,
                 subtype,
                 loginStatePtr,
                 (u32)resultByte,
                 target48,
                 target4c,
                 target50,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_role_path(const char *label, u32 pc)
{
    u32 base = g_currentScreenModuleBase;
    u32 lr = 0;
    u32 r0 = 0;
    u32 modeObj = 0;
    u32 modeObjCb4 = 0;
    u32 modeObjCb8 = 0;
    u32 modeObjCb10 = 0;
    u32 selectedRoleIdShadow = 0;
    u32 firstRoleId = 0;
    u32 firstRoleLevel = 0;
    u8 firstRoleSex = 0;
    u8 firstRoleJob = 0;
    char firstRoleName[96];
    u32 listPtr = 0;
    u32 activeRoleIndex = 0;
    u32 roleFamily = 0;
    u8 state0 = 0;
    u8 ready = 0;
    u8 slotLimit = 0;
    u8 initDone = 0;
    u8 roleCount = 0;
    u8 tag1 = 0;
    u8 tag2 = 0;
    u16 mode = 0;
    u16 sel0 = 0;
    u16 sel1 = 0;
    u16 ui56 = 0;
    u16 ui58 = 0;
    u16 ui60 = 0;
    u16 ui62 = 0;
    u16 ui64 = 0;

    memset(firstRoleName, 0, sizeof(firstRoleName));

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);

    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (base)
    {
        (void)uc_mem_read(MTK, base + 10728, &state0, 1);
        (void)uc_mem_read(MTK, base + 10737, &slotLimit, 1);
        (void)uc_mem_read(MTK, base + 10748, &mode, 2);
        (void)uc_mem_read(MTK, base + 10750, &sel0, 2);
        (void)uc_mem_read(MTK, base + 10752, &sel1, 2);
        (void)uc_mem_read(MTK, base + 10754, &initDone, 1);
        (void)uc_mem_read(MTK, base + 10756, &ui56, 2);
        (void)uc_mem_read(MTK, base + 10758, &ui58, 2);
        (void)uc_mem_read(MTK, base + 10760, &ui60, 2);
        (void)uc_mem_read(MTK, base + 10762, &ui62, 2);
        (void)uc_mem_read(MTK, base + 10764, &ui64, 2);
        (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
        (void)uc_mem_read(MTK, base + 11046, &ready, 1);
        (void)uc_mem_read(MTK, base + 11108, &roleFamily, 4);
        (void)uc_mem_read(MTK, base + 11124, &activeRoleIndex, 4);
        (void)uc_mem_read(MTK, base + 10444, &modeObj, 4);
    }
    if (modeObj)
    {
        (void)uc_mem_read(MTK, modeObj + 0x04, &modeObjCb4, 4);
        (void)uc_mem_read(MTK, modeObj + 0x08, &modeObjCb8, 4);
        (void)uc_mem_read(MTK, modeObj + 0x10, &modeObjCb10, 4);
    }
    if (listPtr)
    {
        (void)uc_mem_read(MTK, listPtr, &roleCount, 1);
        (void)uc_mem_read(MTK, listPtr + 1, &tag1, 1);
        (void)uc_mem_read(MTK, listPtr + 2, &tag2, 1);
        (void)uc_mem_read(MTK, listPtr + 2044, &selectedRoleIdShadow, 4);
        if (roleCount > 0)
        {
            (void)uc_mem_read(MTK, listPtr + 104, &firstRoleId, 4);
            (void)uc_mem_read(MTK, listPtr + 176, &firstRoleLevel, 4);
            (void)uc_mem_read(MTK, listPtr + 324, &firstRoleSex, 1);
            (void)uc_mem_read(MTK, listPtr + 325, &firstRoleJob, 1);
            vm_net_read_guest_gbk_label(listPtr + 72, firstRoleName, sizeof(firstRoleName));
        }
    }

    vm_net_trace("trace_title_role_path label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " r0=%08x state0=%u ready=%u mode=%u sel=%u,%u"
                 " slotLimit=%u initDone=%u ui56=%u ui58=%u ui60=%u ui62=%u ui64=%u"
                 " listPtr=%08x roleCount=%u tag=%u,%u activeRoleIndex=%u roleFamily=%u"
                 " role0={id:%u lvl:%u sex:%u job:%u name:%s} selectedRoleIdShadow=%u"
                 " modeObj=%08x cb4=%08x cb8=%08x cb10=%08x activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 (u32)state0,
                 (u32)ready,
                 (u32)mode,
                 (u32)sel0,
                 (u32)sel1,
                 (u32)slotLimit,
                 (u32)initDone,
                 (u32)ui56,
                 (u32)ui58,
                 (u32)ui60,
                 (u32)ui62,
                 (u32)ui64,
                 listPtr,
                 (u32)roleCount,
                 (u32)tag1,
                 (u32)tag2,
                 activeRoleIndex,
                 roleFamily,
                 firstRoleId,
                 firstRoleLevel,
                 (u32)firstRoleSex,
                 (u32)firstRoleJob,
                 firstRoleName[0] ? firstRoleName : "<none>",
                 selectedRoleIdShadow,
                 modeObj,
                 modeObjCb4,
                 modeObjCb8,
                 modeObjCb10,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_rolelist_reader_methods(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 sp = 0;
    u32 readerBuf = 0;
    u32 fnV20 = 0;
    u32 fnV21 = 0;
    u32 fnV22 = 0;
    u32 fnV23 = 0;
    u32 fnV24 = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);

    readerBuf = sp + 0x08;
    (void)uc_mem_read(MTK, sp + 0x424, &fnV20, 4);
    (void)uc_mem_read(MTK, sp + 0x428, &fnV21, 4);
    (void)uc_mem_read(MTK, sp + 0x42C, &fnV22, 4);
    (void)uc_mem_read(MTK, sp + 0x430, &fnV23, 4);
    (void)uc_mem_read(MTK, sp + 0x434, &fnV24, 4);

    vm_net_trace("trace_title_rolelist_reader_methods label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " sp=%08x readerBuf=%08x v20=%08x v21=%08x v22=%08x v23=%08x v24=%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 sp,
                 readerBuf,
                 fnV20,
                 fnV21,
                 fnV22,
                 fnV23,
                 fnV24,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_title_role_select_action(const char *label, u32 pc)
{
    u32 base = g_currentScreenModuleBase;
    u32 lr = 0;
    u32 r0 = 0;
    u32 listPtr = 0;
    u32 selectedRoleIdShadow = 0;
    u32 firstRoleId = 0;
    u32 roleFamily = 0;
    u32 activeRoleIndex = 0;
    u8 roleCount = 0;
    u16 mode = 0;
    u16 sel0 = 0;
    u16 sel1 = 0;
    char firstRoleName[96];

    memset(firstRoleName, 0, sizeof(firstRoleName));

    if (!base && vmAddedScreen)
        base = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (!base && g_currentScreenThis)
        base = vm_screen_stack_lookup_module_base(g_currentScreenThis + 0x18);

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);

    if (base)
    {
        (void)uc_mem_read(MTK, base + 10748, &mode, 2);
        (void)uc_mem_read(MTK, base + 10750, &sel0, 2);
        (void)uc_mem_read(MTK, base + 10752, &sel1, 2);
        (void)uc_mem_read(MTK, base + 10788, &listPtr, 4);
        (void)uc_mem_read(MTK, base + 11108, &roleFamily, 4);
        (void)uc_mem_read(MTK, base + 11124, &activeRoleIndex, 4);
    }
    if (listPtr)
    {
        (void)uc_mem_read(MTK, listPtr, &roleCount, 1);
        (void)uc_mem_read(MTK, listPtr + 2044, &selectedRoleIdShadow, 4);
        if (roleCount > 0)
        {
            (void)uc_mem_read(MTK, listPtr + 104, &firstRoleId, 4);
            vm_net_read_guest_gbk_label(listPtr + 72, firstRoleName, sizeof(firstRoleName));
        }
    }

    vm_net_trace("trace_title_role_select_action label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " r0=%08x mode=%u sel=%u,%u roleFamily=%u activeRoleIndex=%u"
                 " listPtr=%08x roleCount=%u selectedRoleIdShadow=%u role0.id=%u role0.name=%s"
                 " activeScreen=%08x currentThis=%08x\n",
                 label ? label : "?",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 (u32)mode,
                 (u32)sel0,
                 (u32)sel1,
                 roleFamily,
                 activeRoleIndex,
                 listPtr,
                 (u32)roleCount,
                 selectedRoleIdShadow,
                 firstRoleId,
                 firstRoleName[0] ? firstRoleName : "<none>",
                 vmAddedScreen,
                 g_currentScreenThis);
}

void vm_net_trace_current_net_object_write(uint64_t address, uint32_t size, int64_t value)
{
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 callbackSlot = 0;
    u32 oldValue = 0;
    u32 newValue = 0;
    u32 callback0c = 0;
    u32 callback10 = 0;

    if (!g_netCurrentObject || size == 0)
        return;

    callbackSlot = g_netCurrentObject + 0x14;
    if (writeEnd <= callbackSlot || writeStart >= callbackSlot + 4)
        return;

    (void)uc_mem_read(MTK, callbackSlot - 8, &callback0c, 4);
    (void)uc_mem_read(MTK, callbackSlot - 4, &callback10, 4);
    (void)uc_mem_read(MTK, callbackSlot, &oldValue, 4);
    newValue = (u32)value;

    vm_net_trace("trace_current_net_object_write label=callback obj=%08x slot=%08x old=%08x new=%08x"
                 " peer0c=%08x peer10=%08x writeAddr=%08x writeSize=%u raw=%08x"
                 " activeScreen=%08x currentThis=%08x tick=%u last=%08x\n",
                 g_netCurrentObject,
                 callbackSlot,
                 oldValue,
                 newValue,
                 callback0c,
                 callback10,
                 writeStart,
                 size,
                 (u32)value,
                 vmAddedScreen,
                 g_currentScreenThis,
                 g_schedulerTick,
                 lastAddress);
}

void vm_net_trace_scene_dispatch_gate_write(uint64_t address, uint32_t size, int64_t value)
{
    static u32 s_sceneDispatchGateWriteTraceCount = 0;
    u32 sceneObj = 0;
    u32 gateSlot = 0;
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u8 oldValue = 0;
    u8 newValue = 0;
    u32 pc = 0;
    u32 lr = 0;
    u32 byteOffset = 0;

    if (!Global_R9 || size == 0 || s_sceneDispatchGateWriteTraceCount >= 64)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, 4) != UC_ERR_OK || sceneObj == 0)
        return;

    gateSlot = sceneObj + 0x164;
    if (writeEnd <= gateSlot || writeStart > gateSlot)
        return;

    byteOffset = gateSlot - writeStart;
    if (byteOffset >= 8)
        return;

    (void)uc_mem_read(MTK, gateSlot, &oldValue, 1);
    newValue = (u8)(((uint64_t)value >> (byteOffset * 8u)) & 0xffu);
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    ++s_sceneDispatchGateWriteTraceCount;
    vm_net_trace("trace_scene_dispatch_gate_write sceneObj=%08x slot=%08x old=%u new=%u writeAddr=%08x writeSize=%u raw=%08x pc=%08x lr=%08x last=%08x tick=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 sceneObj,
                 gateSlot,
                 oldValue,
                 newValue,
                 writeStart,
                 size,
                 (u32)value,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneDispatchGateWriteTraceCount);
}

void vm_net_trace_scene_loading_owner_write(uint64_t address, uint32_t size, int64_t value)
{
    static u32 s_sceneLoadingOwnerWriteTraceCount = 0;
    static const struct
    {
        u32 offset;
        u32 width;
        const char *name;
    } targets[] = {
        {0x552C, 4, "loadingState_R9_552C"},
        {0x5530, 1, "loadingGate_R9_5530"},
        {0x5531, 1, "loadingGateBlock_R9_5531"},
        {0x5540, 4, "loadingWaitObj_R9_5540"},
        {0x554C, 4, "loadingWaitCallback_R9_554C"},
        {0x5564, 4, "loadingWaitObjFlag_R9_5564"},
        {0x5C64, 1, "sceneTickFlag0_R9_5C64"},
        {0x5C65, 1, "sceneTickOneShot_R9_5C65"},
        {0x5C66, 1, "sceneTickResync_R9_5C66"},
        {0x5C67, 1, "sceneGateA_R9_5C67"},
        {0x5C68, 1, "sceneGateB_R9_5C68"},
        {0x9590, 2, "netManagerCount_R9_9590"},
    };
    u32 writeStart = (u32)address;
    u32 writeEnd = writeStart + size;
    u32 pc = 0;
    u32 lr = 0;

    if (!Global_R9 || size == 0 || s_sceneLoadingOwnerWriteTraceCount >= 256)
        return;

    for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        u32 slot = Global_R9 + targets[i].offset;
        u32 slotEnd = slot + targets[i].width;
        u32 byteOffset;
        u32 oldValue = 0;
        u32 newValue = 0;

        if (writeEnd <= slot || writeStart >= slotEnd || writeStart > slot)
            continue;

        byteOffset = slot - writeStart;
        if (byteOffset >= 8)
            continue;
        if (targets[i].width == 1)
        {
            u8 oldByte = 0;
            (void)uc_mem_read(MTK, slot, &oldByte, 1);
            oldValue = oldByte;
            newValue = (u8)(((uint64_t)value >> (byteOffset * 8u)) & 0xffu);
        }
        else if (targets[i].width == 2)
        {
            u16 oldWord = 0;
            (void)uc_mem_read(MTK, slot, &oldWord, 2);
            oldValue = oldWord;
            if (byteOffset <= 6)
                newValue = (u16)(((uint64_t)value >> (byteOffset * 8u)) & 0xffffu);
        }
        else
        {
            u32 oldDword = 0;
            (void)uc_mem_read(MTK, slot, &oldDword, 4);
            oldValue = oldDword;
            if (byteOffset <= 4)
                newValue = (u32)(((uint64_t)value >> (byteOffset * 8u)) & 0xffffffffu);
        }

        uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        ++s_sceneLoadingOwnerWriteTraceCount;
        vm_net_trace("trace_scene_loading_owner_write field=%s slot=%08x old=%u new=%u writeAddr=%08x writeSize=%u raw=%08x pc=%08x lr=%08x last=%08x tick=%u activeScreen=%08x currentThis=%08x count=%u\n",
                     targets[i].name,
                     slot,
                     oldValue,
                     newValue,
                     writeStart,
                     size,
                     (u32)value,
                     pc,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_sceneLoadingOwnerWriteTraceCount);
    }
}

static void vm_net_trace_status_tile_slots_if_needed(u32 pc)
{
    static u32 s_prevTileInfoBase = 0xffffffffu;
    static u8 s_prevStatus15 = 0xffu;
    static u8 s_prevStatus16 = 0xffu;
    static u8 s_prevStatus17 = 0xffu;
    static const u32 s_tileIds[] = {25, 26, 27, 28, 29, 30, 31, 65};
    static int s_prevTileIndex[8] = {
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff,
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};
    static u32 s_prevResPtr[8] = {
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    u32 mapMgr = 0;
    u32 tileInfoBase = 0;
    u32 resourceTable = 0;
    u32 resPtr[8] = {0};
    int tileIndex[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    u8 status15 = 0;
    u8 status16 = 0;
    u8 status17 = 0;
    u32 lr = 0;
    int i = 0;

    if (!Global_R9)
        return;

    uc_mem_read(MTK, Global_R9 + 23673, &status15, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &status16, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &status17, 1);
    uc_mem_read(MTK, Global_R9 + 39664, &mapMgr, 4);
    if (mapMgr)
    {
        uc_mem_read(MTK, mapMgr + 4, &tileInfoBase, 4);
        uc_mem_read(MTK, mapMgr, &resourceTable, 4);
        if (resourceTable)
            uc_mem_read(MTK, resourceTable + 0x10, &resourceTable, 4);
    }
    if (tileInfoBase)
    {
        for (i = 0; i < (int)(sizeof(s_tileIds) / sizeof(s_tileIds[0])); ++i)
        {
            short signedIndex = -1;
            uc_mem_read(MTK, tileInfoBase + s_tileIds[i] * 8 + 2, &signedIndex, 2);
            tileIndex[i] = (int)signedIndex;
            if (resourceTable && tileIndex[i] >= 0)
                uc_mem_read(MTK, resourceTable + (u32)tileIndex[i] * 4, &resPtr[i], 4);
        }
    }
    if (tileInfoBase == s_prevTileInfoBase &&
        status15 == s_prevStatus15 &&
        status16 == s_prevStatus16 &&
        status17 == s_prevStatus17)
    {
        int changed = 0;
        for (i = 0; i < (int)(sizeof(s_tileIds) / sizeof(s_tileIds[0])); ++i)
        {
            if (tileIndex[i] != s_prevTileIndex[i] || resPtr[i] != s_prevResPtr[i])
            {
                changed = 1;
                break;
            }
        }
        if (!changed)
            return;
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    s_prevTileInfoBase = tileInfoBase;
    s_prevStatus15 = status15;
    s_prevStatus16 = status16;
    s_prevStatus17 = status17;
    for (i = 0; i < (int)(sizeof(s_tileIds) / sizeof(s_tileIds[0])); ++i)
    {
        s_prevTileIndex[i] = tileIndex[i];
        s_prevResPtr[i] = resPtr[i];
    }
    vm_net_trace("trace_status_tile_slots pc=%08x lr=%08x last=%08x tick=%u mapMgr=%08x tileInfo=%08x flags=%u,%u,%u"
                 " 25=%d/%08x 26=%d/%08x 27=%d/%08x 28=%d/%08x 29=%d/%08x 30=%d/%08x 31=%d/%08x 65=%d/%08x"
                 " activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 mapMgr,
                 tileInfoBase,
                 (u32)status15,
                 (u32)status16,
                 (u32)status17,
                 tileIndex[0],
                 resPtr[0],
                 tileIndex[1],
                 resPtr[1],
                 tileIndex[2],
                 resPtr[2],
                 tileIndex[3],
                 resPtr[3],
                 tileIndex[4],
                 resPtr[4],
                 tileIndex[5],
                 resPtr[5],
                 tileIndex[6],
                 resPtr[6],
                 tileIndex[7],
                 resPtr[7],
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_status_bar_divide_site(u32 pc)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 currentValue = 0;
    u32 baseMax = 0;
    u32 displayMax = 0;
    u32 lr = 0;
    const char *label = "unknown";

    if (!Global_R9)
        return;

    hudState = Global_R9 + 0x5C64;

    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (pc == 0x101477E)
    {
        label = "primary";
        if (currentSceneNode)
        {
            uc_mem_read(MTK, currentSceneNode + 0xB4, &currentValue, 4);
            uc_mem_read(MTK, currentSceneNode + 0xBC, &baseMax, 4);
        }
        if (statusMeterNode)
            uc_mem_read(MTK, statusMeterNode + 0xC4, &displayMax, 4);
    }
    else if (pc == 0x10147AE)
    {
        label = "secondary";
        if (currentSceneNode)
        {
            uc_mem_read(MTK, currentSceneNode + 0xB8, &currentValue, 4);
            uc_mem_read(MTK, currentSceneNode + 0xC0, &baseMax, 4);
        }
        if (statusMeterNode)
            uc_mem_read(MTK, statusMeterNode + 0xC8, &displayMax, 4);
    }

    vm_net_trace("trace_status_bar_divide_site label=%s pc=%08x lr=%08x last=%08x tick=%u currentNode=%08x meterNode=%08x current=%u baseMax=%u displayMax=%u activeScreen=%08x currentThis=%08x\n",
                 label,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 statusMeterNode,
                 currentValue,
                 baseMax,
                 displayMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_status_bar_state_if_needed(u32 pc)
{
    static u32 s_prevCurrentNode = 0xffffffffu;
    static u32 s_prevMeterNode = 0xffffffffu;
    static u32 s_prevPrimaryCurrent = 0xffffffffu;
    static u32 s_prevSecondaryCurrent = 0xffffffffu;
    static u32 s_prevPrimaryBaseMax = 0xffffffffu;
    static u32 s_prevSecondaryBaseMax = 0xffffffffu;
    static u32 s_prevPrimaryDisplayMax = 0xffffffffu;
    static u32 s_prevSecondaryDisplayMax = 0xffffffffu;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 primaryCurrent = 0;
    u32 secondaryCurrent = 0;
    u32 primaryBaseMax = 0;
    u32 secondaryBaseMax = 0;
    u32 primaryDisplayMax = 0;
    u32 secondaryDisplayMax = 0;
    u32 lr = 0;

    if (!Global_R9)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    if (currentSceneNode)
    {
        uc_mem_read(MTK, currentSceneNode + 0xB4, &primaryCurrent, 4);
        uc_mem_read(MTK, currentSceneNode + 0xB8, &secondaryCurrent, 4);
        uc_mem_read(MTK, currentSceneNode + 0xBC, &primaryBaseMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC0, &secondaryBaseMax, 4);
    }
    if (statusMeterNode)
    {
        uc_mem_read(MTK, statusMeterNode + 0xC4, &primaryDisplayMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC8, &secondaryDisplayMax, 4);
    }

    if (currentSceneNode == s_prevCurrentNode &&
        statusMeterNode == s_prevMeterNode &&
        primaryCurrent == s_prevPrimaryCurrent &&
        secondaryCurrent == s_prevSecondaryCurrent &&
        primaryBaseMax == s_prevPrimaryBaseMax &&
        secondaryBaseMax == s_prevSecondaryBaseMax &&
        primaryDisplayMax == s_prevPrimaryDisplayMax &&
        secondaryDisplayMax == s_prevSecondaryDisplayMax)
    {
        return;
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    s_prevCurrentNode = currentSceneNode;
    s_prevMeterNode = statusMeterNode;
    s_prevPrimaryCurrent = primaryCurrent;
    s_prevSecondaryCurrent = secondaryCurrent;
    s_prevPrimaryBaseMax = primaryBaseMax;
    s_prevSecondaryBaseMax = secondaryBaseMax;
    s_prevPrimaryDisplayMax = primaryDisplayMax;
    s_prevSecondaryDisplayMax = secondaryDisplayMax;

    vm_net_trace("trace_status_bar_state pc=%08x lr=%08x last=%08x tick=%u currentNode=%08x meterNode=%08x primary=%u/%u/%u secondary=%u/%u/%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 statusMeterNode,
                 primaryCurrent,
                 primaryBaseMax,
                 primaryDisplayMax,
                 secondaryCurrent,
                 secondaryBaseMax,
                 secondaryDisplayMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_actorinfo_snapshot(u32 pc)
{
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u32 summaryId = 0;
    u32 primaryCurrent = 0;
    u32 secondaryCurrent = 0;
    u32 primaryBaseMax = 0;
    u32 secondaryBaseMax = 0;
    u32 primaryDisplayMax = 0;
    u32 secondaryDisplayMax = 0;
    u32 actorGap0CC0 = 0;
    u32 actorGap0CC4 = 0;
    u32 actorGap0CC8 = 0;
    u32 lr = 0;
    u16 summaryStatus = 0;
    char labelText[64];
    char shortLabel[64];
    char previewImage[64];
    char actorResource[64];
    char sceneKey[64];

    if (!Global_R9)
        return;

    memset(labelText, 0, sizeof(labelText));
    memset(shortLabel, 0, sizeof(shortLabel));
    memset(previewImage, 0, sizeof(previewImage));
    memset(actorResource, 0, sizeof(actorResource));
    memset(sceneKey, 0, sizeof(sceneKey));

    if (uc_mem_read(MTK, Global_R9 + 23720, &currentSceneNode, 4) != UC_ERR_OK || currentSceneNode == 0)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, currentSceneNode + 100, &actorId, 4);
    uc_mem_read(MTK, currentSceneNode + 104, &summaryId, 4);
    uc_mem_read(MTK, currentSceneNode + 180, &primaryCurrent, 4);
    uc_mem_read(MTK, currentSceneNode + 184, &secondaryCurrent, 4);
    uc_mem_read(MTK, currentSceneNode + 188, &primaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 192, &secondaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 196, &primaryDisplayMax, 4);
    uc_mem_read(MTK, currentSceneNode + 200, &secondaryDisplayMax, 4);
    uc_mem_read(MTK, currentSceneNode + 204, &actorGap0CC0, 4);
    uc_mem_read(MTK, currentSceneNode + 208, &actorGap0CC4, 4);
    uc_mem_read(MTK, currentSceneNode + 212, &actorGap0CC8, 4);
    uc_mem_read(MTK, currentSceneNode + 312, &summaryStatus, 2);
    vm_net_read_guest_best_effort_label(currentSceneNode + 108, labelText, sizeof(labelText));
    vm_net_read_guest_best_effort_label(currentSceneNode + 256, shortLabel, sizeof(shortLabel));
    vm_net_read_guest_best_effort_label(currentSceneNode + 266, previewImage, sizeof(previewImage));
    vm_net_read_guest_best_effort_label(currentSceneNode + 216, actorResource, sizeof(actorResource));
    vm_net_read_guest_best_effort_label(Global_R9 + 24134, sceneKey, sizeof(sceneKey));

    vm_net_trace("trace_scene_actorinfo_snapshot pc=%08x lr=%08x last=%08x tick=%u node=%08x actorId=%u summaryId=%u summaryStatus=%u label=%s short=%s preview=%s actor=%s scene=%s hp=%u/%u mp=%u/%u extraCC=%u,%u,%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 actorId,
                 summaryId,
                 (unsigned int)summaryStatus,
                 labelText,
                 shortLabel,
                 previewImage,
                 actorResource,
                 sceneKey,
                 primaryCurrent,
                 primaryBaseMax,
                 secondaryCurrent,
                 secondaryBaseMax,
                 actorGap0CC0,
                 actorGap0CC4,
                 actorGap0CC8,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_current_node_publish(u32 pc)
{
    static u32 s_sceneCurrentNodePublishTraceCount = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 snapshotNode = 0;
    u32 lr = 0;
    u32 currentActorId = 0;
    u32 snapshotActorId = 0;
    u32 currentDrawAt = 0;
    u32 currentStep = 0;
    u32 currentVisualResId = 0;
    u32 snapshotVisualResId = 0;
    u16 currentGridX = 0;
    u16 currentGridY = 0;
    u16 currentTargetX = 0;
    u16 currentTargetY = 0;
    u8 currentOccupied = 0;
    u8 currentNodeKind = 0;
    u8 currentVisualGroup = 0;
    u8 currentVisualVariant = 0;
    char currentLabel[64];
    char currentShort[32];
    char snapshotLabel[64];

    if (!Global_R9 || s_sceneCurrentNodePublishTraceCount >= 16)
        return;
    ++s_sceneCurrentNodePublishTraceCount;

    memset(currentLabel, 0, sizeof(currentLabel));
    memset(currentShort, 0, sizeof(currentShort));
    memset(snapshotLabel, 0, sizeof(snapshotLabel));

    hudState = Global_R9 + 0x5C64;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x44, &snapshotNode, 4);

    if (currentSceneNode)
    {
        uc_mem_read(MTK, currentSceneNode + 0x64, &currentActorId, 4);
        uc_mem_read(MTK, currentSceneNode + 0x24, &currentVisualResId, 4);
        uc_mem_read(MTK, currentSceneNode + 0x18, &currentGridX, 2);
        uc_mem_read(MTK, currentSceneNode + 0x1A, &currentGridY, 2);
        uc_mem_read(MTK, currentSceneNode + 0x11E, &currentTargetX, 2);
        uc_mem_read(MTK, currentSceneNode + 0x120, &currentTargetY, 2);
        uc_mem_read(MTK, currentSceneNode + 0x13B, &currentNodeKind, 1);
        uc_mem_read(MTK, currentSceneNode + 0x13F, &currentOccupied, 1);
        uc_mem_read(MTK, currentSceneNode + 0x140, &currentVisualGroup, 1);
        uc_mem_read(MTK, currentSceneNode + 0x141, &currentVisualVariant, 1);
        uc_mem_read(MTK, currentSceneNode + 0x148, &currentDrawAt, 4);
        uc_mem_read(MTK, currentSceneNode + 0x14C, &currentStep, 4);
        vm_net_read_guest_best_effort_label(currentSceneNode + 0x44, currentLabel, sizeof(currentLabel));
        vm_net_read_guest_best_effort_label(currentSceneNode + 0x100, currentShort, sizeof(currentShort));
    }
    if (snapshotNode)
    {
        uc_mem_read(MTK, snapshotNode + 0x64, &snapshotActorId, 4);
        uc_mem_read(MTK, snapshotNode + 0x24, &snapshotVisualResId, 4);
        vm_net_read_guest_best_effort_label(snapshotNode + 0x44, snapshotLabel, sizeof(snapshotLabel));
    }

    vm_net_trace("trace_scene_current_node_publish pc=%08x lr=%08x last=%08x tick=%u current=%08x snapshot=%08x"
                 " actor=%u/%u label=%s snapshotLabel=%s short=%s grid=%u,%u target=%u,%u"
                 " visual=%u,%u visualRes=%u/%u nodeKind=%u occupied=%u drawAt=%08x step=%08x"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 snapshotNode,
                 currentActorId,
                 snapshotActorId,
                 currentLabel,
                 snapshotLabel,
                 currentShort,
                 (unsigned int)currentGridX,
                 (unsigned int)currentGridY,
                 (unsigned int)currentTargetX,
                 (unsigned int)currentTargetY,
                 (unsigned int)currentVisualGroup,
                 (unsigned int)currentVisualVariant,
                 currentVisualResId,
                 snapshotVisualResId,
                 (unsigned int)currentNodeKind,
                 (unsigned int)currentOccupied,
                 currentDrawAt,
                 currentStep,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneCurrentNodePublishTraceCount);
}

static void vm_fixup_current_node_visual_res_if_ready(u32 pc, const char *label)
{
    static u32 s_sceneVisualResFixupTraceCount = 0;
    static u32 s_sceneVisualResMissingTraceCount = 0;
    static u32 s_sceneVisualResProbeTraceCount = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 visualResId = 0;
    u32 tableEntry = 0;
    u32 selectedIndex = 0;
    u32 slotEntries[6] = {0};
    u8 visualGroup = 0;
    u8 visualVariant = 0;

    if (!Global_R9)
    {
        if (s_sceneVisualResProbeTraceCount < 24)
        {
            ++s_sceneVisualResProbeTraceCount;
            vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                         " reason=no_r9 activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResProbeTraceCount);
        }
        return;
    }

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    if (!currentSceneNode)
    {
        if (s_sceneVisualResProbeTraceCount < 24)
        {
            ++s_sceneVisualResProbeTraceCount;
            vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                         " reason=no_current_node hudState=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         hudState,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResProbeTraceCount);
        }
        return;
    }

    uc_mem_read(MTK, currentSceneNode + 0x24, &visualResId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x140, &visualGroup, 1);
    uc_mem_read(MTK, currentSceneNode + 0x141, &visualVariant, 1);
    if (visualGroup == 0 || visualGroup > 2 || visualVariant > 2)
    {
        if (s_sceneVisualResProbeTraceCount < 24)
        {
            ++s_sceneVisualResProbeTraceCount;
            vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                         " reason=invalid_visual node=%08x visual=%u,%u visualRes=%08x"
                         " activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         currentSceneNode,
                         (unsigned int)visualGroup,
                         (unsigned int)visualVariant,
                         visualResId,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResProbeTraceCount);
        }
        return;
    }

    selectedIndex = (u32)visualGroup + ((u32)visualVariant << 1);
    uc_mem_read(MTK, currentSceneNode + 0x00, &slotEntries[0], sizeof(slotEntries));
    uc_mem_read(MTK, currentSceneNode + 4 * (selectedIndex - 1), &tableEntry, 4);
    if (s_sceneVisualResProbeTraceCount < 24)
    {
        ++s_sceneVisualResProbeTraceCount;
        vm_net_trace("trace_scene_visual_res_probe label=%s pc=%08x last=%08x tick=%u"
                     " reason=ready node=%08x visual=%u,%u selectedIndex=%u visualRes=%08x tableEntry=%08x"
                     " slots=%08x,%08x,%08x,%08x,%08x,%08x activeScreen=%08x currentThis=%08x count=%u\n",
                     label ? label : "unknown",
                     pc,
                     lastAddress,
                     g_schedulerTick,
                     currentSceneNode,
                     (unsigned int)visualGroup,
                     (unsigned int)visualVariant,
                     selectedIndex,
                     visualResId,
                     tableEntry,
                     slotEntries[0],
                     slotEntries[1],
                     slotEntries[2],
                     slotEntries[3],
                     slotEntries[4],
                     slotEntries[5],
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_sceneVisualResProbeTraceCount);
    }

    if (visualResId == 0 && tableEntry != 0)
    {
        uc_mem_write(MTK, currentSceneNode + 0x24, &tableEntry, 4);
        if (s_sceneVisualResFixupTraceCount < 24)
        {
            ++s_sceneVisualResFixupTraceCount;
            vm_net_trace("trace_scene_visual_res_fixup label=%s pc=%08x last=%08x tick=%u node=%08x"
                         " visual=%u,%u selectedIndex=%u tableEntry=%08x oldVisualRes=%08x"
                         " activeScreen=%08x currentThis=%08x count=%u\n",
                         label ? label : "unknown",
                         pc,
                         lastAddress,
                         g_schedulerTick,
                         currentSceneNode,
                         (unsigned int)visualGroup,
                         (unsigned int)visualVariant,
                         selectedIndex,
                         tableEntry,
                         visualResId,
                         vmAddedScreen,
                         g_currentScreenThis,
                         s_sceneVisualResFixupTraceCount);
        }
    }
    else if (visualResId == 0 && tableEntry == 0 && s_sceneVisualResMissingTraceCount < 24)
    {
        ++s_sceneVisualResMissingTraceCount;
        vm_net_trace("trace_scene_visual_res_still_missing label=%s pc=%08x last=%08x tick=%u node=%08x"
                     " visual=%u,%u selectedIndex=%u visualRes=%08x tableEntry=%08x"
                     " activeScreen=%08x currentThis=%08x count=%u\n",
                     label ? label : "unknown",
                     pc,
                     lastAddress,
                     g_schedulerTick,
                     currentSceneNode,
                     (unsigned int)visualGroup,
                     (unsigned int)visualVariant,
                     selectedIndex,
                     visualResId,
                     tableEntry,
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_sceneVisualResMissingTraceCount);
    }
}

static void vm_trace_scene_body_draw_dispatch(const char *label, u32 pc)
{
    static u32 s_sceneBodyDrawTraceCount = 0;
    u32 moveEntriesBase = 0;
    u32 moveEntryPtr = 0;
    u32 callbackObj = 0;
    u32 callbackFn = 0;
    u32 lr = 0;
    u32 r4 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u16 raw0 = 0;
    u16 raw2 = 0;
    u16 raw6 = 0;
    u16 raw8 = 0;
    u16 rawA = 0;
    u16 rawC = 0;
    u8 entryKind = 0;

    if (!Global_R9 || s_sceneBodyDrawTraceCount >= 24)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &callbackObj);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &callbackFn);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &moveEntriesBase, 4) != UC_ERR_OK || moveEntriesBase == 0)
        return;

    moveEntryPtr = moveEntriesBase + ((r4 & 0xFFFFu) * 32u);
    uc_mem_read(MTK, moveEntryPtr + 0x00, &raw0, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x02, &raw2, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x06, &raw6, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x08, &raw8, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x0A, &rawA, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x0C, &rawC, 2);
    uc_mem_read(MTK, moveEntryPtr + 0x17, &entryKind, 1);

    ++s_sceneBodyDrawTraceCount;
    vm_net_trace("trace_scene_body_draw_dispatch label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " moveIndex=%u entry=%08x kind=%u raw=%u,%u box=%u,%u,%u,%u"
                 " screen=%d,%d callbackObj=%08x callbackFn=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 (unsigned int)(r4 & 0xFFFFu),
                 moveEntryPtr,
                 (unsigned int)entryKind,
                 (unsigned int)raw0,
                 (unsigned int)raw2,
                 (unsigned int)raw6,
                 (unsigned int)raw8,
                 (unsigned int)rawA,
                 (unsigned int)rawC,
                 (int)r1,
                 (int)r2,
                 callbackObj,
                 callbackFn,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneBodyDrawTraceCount);
}

static void vm_trace_scene_current_node_draw_callbacks(const char *label, u32 pc)
{
    static u32 s_sceneCurrentNodeDrawTraceCount = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 nodePtr = 0;
    u32 callbackFn = 0;
    u32 lr = 0;
    u32 actorId = 0;
    u32 visualResId = 0;
    u32 drawAt = 0;
    u32 step = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;
    u8 visualGroup = 0;
    u8 visualVariant = 0;

    if (!Global_R9 || s_sceneCurrentNodeDrawTraceCount >= 24)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_reg_read(MTK, UC_ARM_REG_R0, &nodePtr);
    if (!currentSceneNode || nodePtr != currentSceneNode)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, currentSceneNode + 0x64, &actorId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x24, &visualResId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x18, &gridX, 2);
    uc_mem_read(MTK, currentSceneNode + 0x1A, &gridY, 2);
    uc_mem_read(MTK, currentSceneNode + 0x11E, &targetX, 2);
    uc_mem_read(MTK, currentSceneNode + 0x120, &targetY, 2);
    uc_mem_read(MTK, currentSceneNode + 0x140, &visualGroup, 1);
    uc_mem_read(MTK, currentSceneNode + 0x141, &visualVariant, 1);
    uc_mem_read(MTK, currentSceneNode + 0x148, &drawAt, 4);
    uc_mem_read(MTK, currentSceneNode + 0x14C, &step, 4);
    if (pc == 0x1014594)
        callbackFn = drawAt;
    else
        callbackFn = step;

    ++s_sceneCurrentNodeDrawTraceCount;
    vm_net_trace("trace_scene_current_node_draw label=%s pc=%08x lr=%08x last=%08x tick=%u"
                 " node=%08x actorId=%u visual=%u,%u visualRes=%08x grid=%u,%u target=%u,%u"
                 " screen=%d,%d callback=%08x drawAt=%08x step=%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 currentSceneNode,
                 actorId,
                 (unsigned int)visualGroup,
                 (unsigned int)visualVariant,
                 visualResId,
                 (unsigned int)gridX,
                 (unsigned int)gridY,
                 (unsigned int)targetX,
                 (unsigned int)targetY,
                 (int)r1,
                 (int)r2,
                 callbackFn,
                 drawAt,
                 step,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneCurrentNodeDrawTraceCount);
}

static void vm_trace_actor_move_entry_table(u32 pc)
{
    static u32 s_actorMoveEntryTableTraceCount = 0;
    u32 tableBasePtr = 0;
    u32 entryCount = 0;
    u32 lr = 0;
    u32 hudState = 0;
    u32 promptNamePtr = 0;
    u32 statusTextPtr = 0;
    char promptName[80];
    char statusText[80];

    if (!Global_R9 || s_actorMoveEntryTableTraceCount >= 8)
        return;

    memset(promptName, 0, sizeof(promptName));
    memset(statusText, 0, sizeof(statusText));
    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x74, &promptNamePtr, 4);
    uc_mem_read(MTK, hudState + 0x78, &statusTextPtr, 4);
    vm_net_read_guest_best_effort_label(promptNamePtr, promptName, sizeof(promptName));
    vm_net_read_guest_best_effort_label(statusTextPtr, statusText, sizeof(statusText));

    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &tableBasePtr, 4) != UC_ERR_OK || tableBasePtr == 0)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x5D40, &entryCount, 4) != UC_ERR_OK)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    ++s_actorMoveEntryTableTraceCount;
    vm_net_trace("trace_actor_move_entry_table pc=%08x lr=%08x last=%08x tick=%u table=%08x count=%u promptNamePtr=%08x promptName=%s statusTextPtr=%08x statusText=%s activeScreen=%08x currentThis=%08x snap=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 tableBasePtr,
                 entryCount,
                 promptNamePtr,
                 promptName,
                 statusTextPtr,
                 statusText,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMoveEntryTableTraceCount);

    for (u32 i = 0; i < entryCount && i < 4; ++i)
    {
        u32 entryPtr = tableBasePtr + i * 32u;
        u32 d[8] = {0};
        u8 kind = 0;
        uc_mem_read(MTK, entryPtr, d, sizeof(d));
        uc_mem_read(MTK, entryPtr + 0x17, &kind, 1);
        vm_net_trace("trace_actor_move_entry item=%u entry=%08x dwords=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x kind=%u activeScreen=%08x currentThis=%08x\n",
                     i,
                     entryPtr,
                     d[0],
                     d[1],
                     d[2],
                     d[3],
                     d[4],
                     d[5],
                     d[6],
                     d[7],
                     (unsigned int)kind,
                     vmAddedScreen,
                     g_currentScreenThis);
    }
}

static void vm_trace_scene_status_text_write(const char *fieldName, u32 pc)
{
    static u32 s_sceneStatusTextWriteTraceCount = 0;
    u32 lr = 0;
    u32 textPtr = 0;
    u32 promptNamePtr = 0;
    u32 statusTextPtr = 0;
    char text[96];
    char promptName[80];
    char statusText[80];

    if (!Global_R9 || s_sceneStatusTextWriteTraceCount >= 16)
        return;

    memset(text, 0, sizeof(text));
    memset(promptName, 0, sizeof(promptName));
    memset(statusText, 0, sizeof(statusText));
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &textPtr);
    uc_mem_read(MTK, Global_R9 + 0x5CD8, &promptNamePtr, 4);
    uc_mem_read(MTK, Global_R9 + 0x5CDC, &statusTextPtr, 4);
    vm_net_read_guest_best_effort_label(textPtr, text, sizeof(text));
    vm_net_read_guest_best_effort_label(promptNamePtr, promptName, sizeof(promptName));
    vm_net_read_guest_best_effort_label(statusTextPtr, statusText, sizeof(statusText));

    ++s_sceneStatusTextWriteTraceCount;
    vm_net_trace("trace_scene_status_text_write field=%s pc=%08x lr=%08x last=%08x tick=%u newPtr=%08x newText=%s oldPromptPtr=%08x oldPrompt=%s oldStatusPtr=%08x oldStatus=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 fieldName ? fieldName : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 textPtr,
                 text,
                 promptNamePtr,
                 promptName,
                 statusTextPtr,
                 statusText,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneStatusTextWriteTraceCount);
}

static void vm_trace_actor_move_entry_parser_entry(u32 pc)
{
    static u32 s_actorMoveEntryParserEntryTraceCount = 0;
    u32 lr = 0;
    u32 streamBase = 0;
    u32 posPtr = 0;
    u32 cursor = 0;

    if (s_actorMoveEntryParserEntryTraceCount >= 8)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &streamBase);
    uc_reg_read(MTK, UC_ARM_REG_R1, &posPtr);
    if (posPtr)
        uc_mem_read(MTK, posPtr, &cursor, 4);

    ++s_actorMoveEntryParserEntryTraceCount;
    vm_net_trace("trace_actor_move_entry_parser_entry pc=%08x lr=%08x caller=%08x last=%08x tick=%u stream=%08x posPtr=%08x cursor=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 streamBase,
                 posPtr,
                 cursor,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMoveEntryParserEntryTraceCount);
}

static void vm_trace_actor_motion_descriptor_context(u32 pc)
{
    static u32 s_actorMotionDescriptorContextTraceCount = 0;
    u32 lr = 0;
    u32 arg0 = 0;
    u32 arg1 = 0;
    u32 arg2 = 0;
    u32 arg3 = 0;
    u32 sp = 0;
    u32 a5 = 0;
    u32 a6 = 0;
    u32 a7 = 0;
    u32 a8 = 0;
    char nameBuf[96];

    if (s_actorMotionDescriptorContextTraceCount >= 8)
        return;

    memset(nameBuf, 0, sizeof(nameBuf));
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &arg0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &arg1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &arg2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &arg3);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_mem_read(MTK, sp + 0x0, &a5, 4);
    uc_mem_read(MTK, sp + 0x4, &a6, 4);
    uc_mem_read(MTK, sp + 0x8, &a7, 4);
    uc_mem_read(MTK, sp + 0xC, &a8, 4);
    if (a5)
        vm_net_read_guest_best_effort_label(a5, nameBuf, sizeof(nameBuf));

    ++s_actorMotionDescriptorContextTraceCount;
    vm_net_trace("trace_actor_motion_descriptor_context pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x r1=%08x r2=%08x r3=%08x a5=%08x name=%s a6=%08x a7=%08x a8=%08x"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 arg0,
                 arg1,
                 arg2,
                 arg3,
                 a5,
                 nameBuf,
                 a6,
                 a7,
                 a8,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionDescriptorContextTraceCount);
}

static void vm_trace_actor_move_entry_append(u32 pc)
{
    static u32 s_actorMoveEntryAppendTraceCount = 0;
    u32 tableBasePtr = 0;
    u32 entryCount = 0;
    u32 entryPtr = 0;
    u32 lr = 0;
    u32 d[8] = {0};
    u8 kind = 0;

    if (!Global_R9 || s_actorMoveEntryAppendTraceCount >= 16)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &tableBasePtr, 4) != UC_ERR_OK || tableBasePtr == 0)
        return;
    if (uc_mem_read(MTK, Global_R9 + 0x5D40, &entryCount, 4) != UC_ERR_OK)
        return;

    entryPtr = tableBasePtr + entryCount * 32u;
    uc_mem_read(MTK, entryPtr, d, sizeof(d));
    uc_mem_read(MTK, entryPtr + 0x17, &kind, 1);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    ++s_actorMoveEntryAppendTraceCount;
    vm_net_trace("trace_actor_move_entry_append pc=%08x lr=%08x last=%08x tick=%u table=%08x index=%u entry=%08x dwords=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x kind=%u activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 tableBasePtr,
                 entryCount,
                 entryPtr,
                 d[0],
                 d[1],
                 d[2],
                 d[3],
                 d[4],
                 d[5],
                 d[6],
                 d[7],
                 (unsigned int)kind,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMoveEntryAppendTraceCount);
}

static bool vm_should_skip_portal_move_entry_append(u32 pc)
{
    static u32 s_portalMoveEntrySkipTraceCount = 0;
    u32 tableBasePtr = 0;
    u32 entryCount = 0;
    u32 entryPtr = 0;
    u32 lr = 0;
    u16 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 boxLeft = 0;
    u16 boxTop = 0;
    u16 boxRight = 0;
    u16 boxBottom = 0;
    u8 kind = 0;

    if (!Global_R9)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &tableBasePtr, 4) != UC_ERR_OK || tableBasePtr == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5D40, &entryCount, 4) != UC_ERR_OK)
        return false;

    entryPtr = tableBasePtr + entryCount * 32u;
    uc_mem_read(MTK, entryPtr + 0x00, &actorId, 2);
    uc_mem_read(MTK, entryPtr + 0x02, &gridX, 2);
    uc_mem_read(MTK, entryPtr + 0x04, &gridY, 2);
    uc_mem_read(MTK, entryPtr + 0x06, &boxLeft, 2);
    uc_mem_read(MTK, entryPtr + 0x08, &boxTop, 2);
    uc_mem_read(MTK, entryPtr + 0x0A, &boxRight, 2);
    uc_mem_read(MTK, entryPtr + 0x0C, &boxBottom, 2);
    uc_mem_read(MTK, entryPtr + 0x17, &kind, 1);

    if (actorId != 1 ||
        gridX != 223 ||
        gridY != 382 ||
        boxLeft != 203 ||
        boxTop != 402 ||
        boxRight != 240 ||
        boxBottom != 422 ||
        kind != 2)
    {
        return false;
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (s_portalMoveEntrySkipTraceCount < 16)
    {
        ++s_portalMoveEntrySkipTraceCount;
        vm_net_trace("trace_portal_move_entry_append_skipped pc=%08x lr=%08x last=%08x tick=%u table=%08x index=%u entry=%08x actorId=%u grid=%u,%u box=%u,%u,%u,%u kind=%u activeScreen=%08x currentThis=%08x count=%u\n",
                     pc,
                     lr,
                     lastAddress,
                     g_schedulerTick,
                     tableBasePtr,
                     entryCount,
                     entryPtr,
                     (unsigned int)actorId,
                     (unsigned int)gridX,
                     (unsigned int)gridY,
                     (unsigned int)boxLeft,
                     (unsigned int)boxTop,
                     (unsigned int)boxRight,
                     (unsigned int)boxBottom,
                     (unsigned int)kind,
                     vmAddedScreen,
                     g_currentScreenThis,
                     s_portalMoveEntrySkipTraceCount);
    }
    return true;
}

static void vm_format_trace_bytes_hex(const u8 *bytes, u32 count, char *out, size_t outCap)
{
    u32 i = 0;
    size_t used = 0;

    if (!out || outCap == 0)
        return;
    out[0] = '\0';
    if (!bytes || count == 0)
    {
        snprintf(out, outCap, "empty");
        return;
    }

    for (i = 0; i < count && used + 4 < outCap; ++i)
    {
        int written = snprintf(out + used, outCap - used, "%s%02x", i ? " " : "", bytes[i]);
        if (written <= 0)
            break;
        used += (size_t)written;
        if (used >= outCap)
            break;
    }
}

static void vm_trace_actor_motion_callback_handoff(u32 pc)
{
    static u32 s_actorMotionCallbackHandoffTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 streamBase = 0;
    u32 parserBase = 0;
    u32 parserAux = 0;
    u32 callback = 0;
    u32 cursor = 0;
    u32 tailAddr = 0;
    u32 tuplePtr = 0;
    u32 tail20Ptr = 0;
    u16 count132 = 0;
    u16 countE = 0;
    u16 count8 = 0;
    u16 count600 = 0;
    u16 count20 = 0;
    u16 tailShorts[24] = {0};
    u16 tupleShorts[4] = {0};
    u8 tailBytes[48] = {0};
    u8 tail20Bytes[20] = {0};
    char tailHex[192];
    char tail20Hex[80];

    if (s_actorMotionCallbackHandoffTraceCount >= 8)
        return;

    memset(tailHex, 0, sizeof(tailHex));
    memset(tail20Hex, 0, sizeof(tail20Hex));
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R4, &streamBase);
    uc_reg_read(MTK, UC_ARM_REG_R5, &parserBase);
    uc_reg_read(MTK, UC_ARM_REG_R6, &parserAux);

    if (sp)
    {
        uc_mem_read(MTK, sp + 0x3C, &cursor, 4);
        uc_mem_read(MTK, sp + 0x7C, &callback, 4);
    }
    if (parserBase)
    {
        uc_mem_read(MTK, parserBase + 0x0A, &count132, 2);
        uc_mem_read(MTK, parserBase + 0x0E, &countE, 2);
        uc_mem_read(MTK, parserBase + 0x10, &count8, 2);
        uc_mem_read(MTK, parserBase + 0x12, &count600, 2);
        uc_mem_read(MTK, parserBase + 0x14, &count20, 2);
        uc_mem_read(MTK, parserBase + 1568, &tuplePtr, 4);
        uc_mem_read(MTK, parserBase + 1560, &tail20Ptr, 4);
    }

    if (streamBase)
    {
        tailAddr = streamBase + cursor;
        uc_mem_read(MTK, tailAddr, tailBytes, sizeof(tailBytes));
        uc_mem_read(MTK, tailAddr, tailShorts, sizeof(tailShorts));
        vm_format_trace_bytes_hex(tailBytes, (u32)sizeof(tailBytes), tailHex, sizeof(tailHex));
    }
    if (tuplePtr)
        uc_mem_read(MTK, tuplePtr, tupleShorts, sizeof(tupleShorts));
    if (tail20Ptr)
    {
        uc_mem_read(MTK, tail20Ptr, tail20Bytes, sizeof(tail20Bytes));
        vm_format_trace_bytes_hex(tail20Bytes, (u32)sizeof(tail20Bytes), tail20Hex, sizeof(tail20Hex));
    }

    ++s_actorMotionCallbackHandoffTraceCount;
    vm_net_trace("trace_actor_motion_callback_handoff pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " stream=%08x parser=%08x parserAux=%08x cursor=%u tail=%08x cb=%08x"
                 " counts132=%u countE=%u count8=%u count600=%u count20=%u"
                 " tuplePtr=%08x tuple0=%u,%u,%u,%u"
                 " tail20Ptr=%08x tail20=%s"
                 " tailShorts=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u tailHex=%s"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 streamBase,
                 parserBase,
                 parserAux,
                 cursor,
                 tailAddr,
                 callback,
                 (unsigned int)count132,
                 (unsigned int)countE,
                 (unsigned int)count8,
                 (unsigned int)count600,
                 (unsigned int)count20,
                 tuplePtr,
                 (unsigned int)tupleShorts[0],
                 (unsigned int)tupleShorts[1],
                 (unsigned int)tupleShorts[2],
                 (unsigned int)tupleShorts[3],
                 tail20Ptr,
                 tail20Hex,
                 (unsigned int)tailShorts[0],
                 (unsigned int)tailShorts[1],
                 (unsigned int)tailShorts[2],
                 (unsigned int)tailShorts[3],
                 (unsigned int)tailShorts[4],
                 (unsigned int)tailShorts[5],
                 (unsigned int)tailShorts[6],
                 (unsigned int)tailShorts[7],
                 (unsigned int)tailShorts[8],
                 (unsigned int)tailShorts[9],
                 (unsigned int)tailShorts[10],
                 (unsigned int)tailShorts[11],
                 (unsigned int)tailShorts[12],
                 (unsigned int)tailShorts[13],
                 (unsigned int)tailShorts[14],
                 (unsigned int)tailShorts[15],
                 (unsigned int)tailShorts[16],
                 (unsigned int)tailShorts[17],
                 (unsigned int)tailShorts[18],
                 (unsigned int)tailShorts[19],
                 (unsigned int)tailShorts[20],
                 (unsigned int)tailShorts[21],
                 (unsigned int)tailShorts[22],
                 (unsigned int)tailShorts[23],
                 tailHex,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionCallbackHandoffTraceCount);
}

static void vm_trace_actor_motion_open_result(u32 pc)
{
    static u32 s_actorMotionOpenResultTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 stream = 0;
    u32 namePtr = 0;
    u32 mode = 0;
    u32 callback = 0;
    char name[128];
    char uiName[96];

    if (s_actorMotionOpenResultTraceCount >= 16)
        return;

    name[0] = '\0';
    uiName[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R4, &stream);
    if (sp)
    {
        uc_mem_read(MTK, sp + 0x70, &namePtr, 4);
        uc_mem_read(MTK, sp + 0x74, &mode, 4);
        uc_mem_read(MTK, sp + 0x7C, &callback, 4);
    }
    vm_net_read_guest_best_effort_label(namePtr, name, sizeof(name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));

    ++s_actorMotionOpenResultTraceCount;
    vm_net_trace("trace_actor_motion_open_result pc=%08x lr=%08x last=%08x tick=%u stream=%08x namePtr=%08x name=%s mode=%u callback=%08x branch=%s uiName=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 stream,
                 namePtr,
                 name,
                 mode,
                 callback,
                 stream ? "local_open_success_direct_parse" : "local_open_fail_enqueue",
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionOpenResultTraceCount);
}

static void vm_trace_actor_motion_enqueue_fallback(u32 pc)
{
    static u32 s_actorMotionEnqueueTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 namePtr = 0;
    u32 mode = 0;
    u32 callback = 0;
    u16 managerCount = 0;
    char name[128];
    char uiName[96];

    if (s_actorMotionEnqueueTraceCount >= 16)
        return;

    name[0] = '\0';
    uiName[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (sp)
    {
        uc_mem_read(MTK, sp + 0x70, &namePtr, 4);
        uc_mem_read(MTK, sp + 0x74, &mode, 4);
        uc_mem_read(MTK, sp + 0x7C, &callback, 4);
    }
    if (Global_R9)
        uc_mem_read(MTK, Global_R9 + 0x9590, &managerCount, 2);
    vm_net_read_guest_best_effort_label(namePtr, name, sizeof(name));
    vm_net_read_active_ui_name(uiName, sizeof(uiName));

    ++s_actorMotionEnqueueTraceCount;
    vm_net_trace("trace_actor_motion_enqueue_fallback pc=%08x lr=%08x last=%08x tick=%u r0=%08x r1=%08x r2=%08x r3=%08x namePtr=%08x name=%s mode=%u callback=%08x managerCountBefore=%u uiName=%s activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 namePtr,
                 name,
                 mode,
                 callback,
                 (unsigned int)managerCount,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_actorMotionEnqueueTraceCount);
}

static void vm_trace_manager_replay_entry(u32 pc)
{
    static u32 s_managerReplayTraceCount = 0;
    u32 lr = 0;
    u32 manager = 0;
    u32 current = 0;
    u16 managerCount = 0;
    u16 recordType = 0;
    u8 callbackChoice = 0;
    u32 currentActorName = 0;
    u32 vargR3 = 0;
    u32 n19202288 = 0;
    u32 contextA = 0;
    u32 contextB = 0;
    u8 nameBytes[30] = {0};
    char nameHex[128];
    char name[128];

    if (s_managerReplayTraceCount >= 16 || !Global_R9)
        return;

    nameHex[0] = '\0';
    name[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    manager = Global_R9 + 0x9588;
    uc_mem_read(MTK, manager + 4, &current, 4);
    uc_mem_read(MTK, manager + 8, &managerCount, 2);
    if (current)
    {
        uc_mem_read(MTK, current, &recordType, 2);
        uc_mem_read(MTK, current + 2, nameBytes, sizeof(nameBytes));
        uc_mem_read(MTK, current + 0x24, &currentActorName, 4);
        uc_mem_read(MTK, current + 0x28, &vargR3, 4);
        uc_mem_read(MTK, current + 0x2C, &n19202288, 4);
        uc_mem_read(MTK, current + 0x30, &callbackChoice, 1);
        uc_mem_read(MTK, current + 0x3C, &contextA, 4);
        uc_mem_read(MTK, current + 0x40, &contextB, 4);
        vm_format_trace_bytes_hex(nameBytes, (u32)sizeof(nameBytes), nameHex, sizeof(nameHex));
        vm_net_read_guest_best_effort_label(current + 2, name, sizeof(name));
    }

    ++s_managerReplayTraceCount;
    vm_net_trace("trace_manager_replay_entry pc=%08x lr=%08x last=%08x tick=%u manager=%08x managerCount=%u current=%08x recordType=%u name=%s nameHex=%s currentActorName=%08x vargR3=%08x n19202288=%08x callbackChoice=%u context=%08x,%08x activeScreen=%08x currentThis=%08x count=%u\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 manager,
                 (unsigned int)managerCount,
                 current,
                 (unsigned int)recordType,
                 name,
                 nameHex,
                 currentActorName,
                 vargR3,
                 n19202288,
                 (unsigned int)callbackChoice,
                 contextA,
                 contextB,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_managerReplayTraceCount);
}

static void vm_trace_scene_message_request(const char *label, u32 pc)
{
    static u32 s_sceneMessageTraceCount = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 stack0 = 0;
    u32 stack4 = 0;
    u32 stack8 = 0;
    u32 stack12 = 0;
    u32 messageQueueMode = 0;
    u32 messageActive = 0;
    u32 pendingMessageCount = 0;
    u8 sceneGateA = 0;
    u8 sceneGateB = 0;
    u8 globalGate = 0;
    u8 widgetFlag = 0;
    u16 widgetFrame = 0;
    int16_t managerCount = 0;
    u8 textBytes[32] = {0};
    char text[128];
    char textHex[128];

    if (s_sceneMessageTraceCount >= 48 || !Global_R9)
        return;

    text[0] = '\0';
    textHex[0] = '\0';
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (sp)
    {
        (void)uc_mem_read(MTK, sp, &stack0, 4);
        (void)uc_mem_read(MTK, sp + 4, &stack4, 4);
        (void)uc_mem_read(MTK, sp + 8, &stack8, 4);
        (void)uc_mem_read(MTK, sp + 12, &stack12, 4);
    }
    if (r0)
    {
        (void)uc_mem_read(MTK, r0, textBytes, sizeof(textBytes));
        vm_format_trace_bytes_hex(textBytes, (u32)sizeof(textBytes), textHex, sizeof(textHex));
        vm_net_read_guest_best_effort_label(r0, text, sizeof(text));
    }

    (void)uc_mem_read(MTK, Global_R9 + 0x5D44, &messageQueueMode, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5D80, &messageActive, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5D84, &pendingMessageCount, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C67, &sceneGateA, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C68, &sceneGateB, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5530, &globalGate, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x10, &widgetFlag, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x0A, &widgetFrame, 2);
    (void)uc_mem_read(MTK, Global_R9 + 0x9590, &managerCount, 2);

    ++s_sceneMessageTraceCount;
    vm_net_trace("trace_scene_message_request label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " textPtr=%08x text=%s textHex=%s cb1=%08x cb2=%08x arg3=%08x stack=%08x,%08x,%08x,%08x"
                 " messageQueueMode=%u messageActive=%u pendingMessage=%u sceneGate=%u,%u gate5530=%u widgetFlag=%u widgetFrame=%u managerCount=%d"
                 " activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 text,
                 textHex,
                 r1,
                 r2,
                 r3,
                 stack0,
                 stack4,
                 stack8,
                 stack12,
                 messageQueueMode,
                 messageActive,
                 pendingMessageCount,
                 sceneGateA,
                 sceneGateB,
                 globalGate,
                 widgetFlag,
                 widgetFrame,
                 managerCount,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneMessageTraceCount);
}

static void vm_trace_scene_loading_callback_gate(const char *label, u32 pc)
{
    static u32 s_sceneLoadingCallbackTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 waitObj = 0;
    u32 waitObjFlag10 = 0;
    u32 waitCallback = 0;
    u32 loadingState = 0;
    u8 gate5530 = 0;
    u8 gate5531 = 0;
    u8 sceneGateA = 0;
    u8 sceneGateB = 0;
    u8 widgetFlag = 0;
    u16 widgetFrame = 0;
    int16_t managerCount = 0;

    if (s_sceneLoadingCallbackTraceCount >= 160 || !Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_mem_read(MTK, Global_R9 + 0x5540, &waitObj, 4);
    if (waitObj)
        (void)uc_mem_read(MTK, waitObj + 0x10, &waitObjFlag10, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x554C, &waitCallback, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x552C, &loadingState, 4);
    (void)uc_mem_read(MTK, Global_R9 + 0x5530, &gate5530, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5531, &gate5531, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C67, &sceneGateA, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x5C68, &sceneGateB, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x10, &widgetFlag, 1);
    (void)uc_mem_read(MTK, Global_R9 + 0x60F4 + 0x0A, &widgetFrame, 2);
    (void)uc_mem_read(MTK, Global_R9 + 0x9590, &managerCount, 2);

    if (pc == 0x1013BDC && waitObjFlag10 == 0 && gate5530 == 0 && gate5531 == 0 && widgetFrame < 280)
        return;

    ++s_sceneLoadingCallbackTraceCount;
    vm_net_trace("trace_scene_loading_callback_gate label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " r0=%08x waitObj=%08x waitObjFlag10=%u waitCallback=%08x loadingState=%u gate5530=%u gate5531=%u"
                 " sceneGate=%u,%u widgetFlag=%u widgetFrame=%u managerCount=%d activeScreen=%08x currentThis=%08x count=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 waitObj,
                 waitObjFlag10,
                 waitCallback,
                 loadingState,
                 gate5530,
                 gate5531,
                 sceneGateA,
                 sceneGateB,
                 widgetFlag,
                 widgetFrame,
                 managerCount,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_sceneLoadingCallbackTraceCount);
}

static void vm_trace_alloc_outgoing_game_event(const char *label, u32 pc)
{
    static u32 s_allocOutgoingTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 eventObj = 0;
    u32 eventCount = 0;
    u32 eventCap = 0;
    u32 eventBase = 0;
    u8 eventByte5 = 0;

    if (s_allocOutgoingTraceCount >= 160 || !Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);

    (void)uc_mem_read(MTK, Global_R9 + 0x5540, &eventObj, 4);
    if (eventObj)
    {
        (void)uc_mem_read(MTK, eventObj + 0x05, &eventByte5, 1);
        (void)uc_mem_read(MTK, eventObj + 0x10, &eventCount, 4);
        (void)uc_mem_read(MTK, eventObj + 0x14, &eventCap, 4);
        (void)uc_mem_read(MTK, eventObj + 0x18, &eventBase, 4);
    }

    ++s_allocOutgoingTraceCount;
    vm_net_trace("trace_alloc_outgoing_game_event label=%s pc=%08x lr=%08x caller=%08x last=%08x tick=%u"
                 " regs=%08x,%08x,%08x,%08x r4=%08x r5=%08x eventObj=%08x byte5=%u count=%u cap=%u base=%08x"
                 " activeScreen=%08x currentThis=%08x countTrace=%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 r1,
                 r2,
                 r3,
                 r4,
                 r5,
                 eventObj,
                 eventByte5,
                 eventCount,
                 eventCap,
                 eventBase,
                 vmAddedScreen,
                 g_currentScreenThis,
                 s_allocOutgoingTraceCount);
}

static void vm_net_trace_status_meter_rebuild_site(u32 pc)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 n2 = 0;
    u32 sourceHead = 0;
    u32 currentPrimaryBaseMax = 0;
    u32 currentSecondaryBaseMax = 0;
    u32 currentPrimaryDisplayMax = 0;
    u32 currentSecondaryDisplayMax = 0;
    u32 meterPrimaryBaseMax = 0;
    u32 meterSecondaryBaseMax = 0;
    u32 meterPrimaryDisplayMax = 0;
    u32 meterSecondaryDisplayMax = 0;
    u32 pendingPrimaryDisplayMax = 0;
    u32 pendingSecondaryDisplayMax = 0;
    const char *label = "unknown";

    if (!Global_R9)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    uc_mem_read(MTK, Global_R9 + 0x6048, &sourceHead, 4);

    if (currentSceneNode)
    {
        uc_mem_read(MTK, currentSceneNode + 0xBC, &currentPrimaryBaseMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC0, &currentSecondaryBaseMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC4, &currentPrimaryDisplayMax, 4);
        uc_mem_read(MTK, currentSceneNode + 0xC8, &currentSecondaryDisplayMax, 4);
    }
    if (statusMeterNode)
    {
        uc_mem_read(MTK, statusMeterNode + 0xBC, &meterPrimaryBaseMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC0, &meterSecondaryBaseMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC4, &meterPrimaryDisplayMax, 4);
        uc_mem_read(MTK, statusMeterNode + 0xC8, &meterSecondaryDisplayMax, 4);
    }

    if (pc == 0x100FF26)
    {
        label = "seed_gate";
        uc_reg_read(MTK, UC_ARM_REG_R6, &n2);
    }
    else if (pc == 0x10100D2 || pc == 0x10101E8)
    {
        label = (pc == 0x10100D2) ? "writeback_early" : "writeback_common";
        if (sp)
        {
            uc_mem_read(MTK, sp + 0x70, &pendingPrimaryDisplayMax, 4);
            uc_mem_read(MTK, sp + 0x6C, &pendingSecondaryDisplayMax, 4);
        }
    }
    else
    {
        return;
    }

    vm_net_trace("trace_status_meter_rebuild_site label=%s pc=%08x lr=%08x last=%08x tick=%u n2=%u sourceHead=%08x currentNode=%08x meterNode=%08x currentBase=%u/%u currentDisplay=%u/%u meterBase=%u/%u meterDisplay=%u/%u pendingDisplay=%u/%u activeScreen=%08x currentThis=%08x\n",
                 label,
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 n2,
                 sourceHead,
                 currentSceneNode,
                 statusMeterNode,
                 currentPrimaryBaseMax,
                 currentSecondaryBaseMax,
                 currentPrimaryDisplayMax,
                 currentSecondaryDisplayMax,
                 meterPrimaryBaseMax,
                 meterSecondaryBaseMax,
                 meterPrimaryDisplayMax,
                 meterSecondaryDisplayMax,
                 pendingPrimaryDisplayMax,
                 pendingSecondaryDisplayMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_scene_seed_status_meter_displaymax_fallback(u32 pc)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 n2 = 0;
    u32 sourceHead = 0;
    u32 currentPrimaryBaseMax = 0;
    u32 currentSecondaryBaseMax = 0;
    u32 currentPrimaryDisplayMax = 0;
    u32 currentSecondaryDisplayMax = 0;
    u32 meterPrimaryDisplayMax = 0;
    u32 meterSecondaryDisplayMax = 0;

    if (pc != 0x100FF26 || !Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R6, &n2);
    if (n2 != 1)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    uc_mem_read(MTK, Global_R9 + 0x6048, &sourceHead, 4);

    if (!currentSceneNode || !statusMeterNode || sourceHead != 0)
        return;

    uc_mem_read(MTK, currentSceneNode + 0xBC, &currentPrimaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC0, &currentSecondaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC4, &currentPrimaryDisplayMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC8, &currentSecondaryDisplayMax, 4);
    uc_mem_read(MTK, statusMeterNode + 0xC4, &meterPrimaryDisplayMax, 4);
    uc_mem_read(MTK, statusMeterNode + 0xC8, &meterSecondaryDisplayMax, 4);

    if (meterPrimaryDisplayMax != 0 || meterSecondaryDisplayMax != 0)
        return;
    if (currentPrimaryBaseMax == 0 && currentSecondaryBaseMax == 0)
        return;

    uc_mem_write(MTK, statusMeterNode + 0xC4, &currentPrimaryBaseMax, 4);
    uc_mem_write(MTK, statusMeterNode + 0xC8, &currentSecondaryBaseMax, 4);
    uc_mem_write(MTK, currentSceneNode + 0xC4, &currentPrimaryBaseMax, 4);
    uc_mem_write(MTK, currentSceneNode + 0xC8, &currentSecondaryBaseMax, 4);

    vm_net_trace("trace_status_meter_seed_fallback pc=%08x last=%08x tick=%u n2=%u sourceHead=%08x currentNode=%08x meterNode=%08x seedDisplay=%u/%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lastAddress,
                 g_schedulerTick,
                 n2,
                 sourceHead,
                 currentSceneNode,
                 statusMeterNode,
                 currentPrimaryBaseMax,
                 currentSecondaryBaseMax,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_loading_overlay_call(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 r0 = 0;
    u32 stateByte = 0;
    u32 sceneObj = 0;
    u8 sceneTickGate3 = 0;
    u8 sceneTickGate4 = 0;
    char uiName[96];
    uiName[0] = 0;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 19638, &stateByte, 1);
        uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
        uc_mem_read(MTK, Global_R9 + 23655, &sceneTickGate3, 1);
        uc_mem_read(MTK, Global_R9 + 23656, &sceneTickGate4, 1);
    }
    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    vm_net_trace("trace_loading_overlay_call label=%s pc=%08x lr=%08x last=%08x tick=%u r0=%08x overlayState=%u sceneTickGate=%u,%u sceneObj=%08x uiName=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 r0,
                 stateByte,
                 sceneTickGate3,
                 sceneTickGate4,
                 sceneObj,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static bool vm_is_scene_bootstrap_loading_overlay_caller(u32 lr)
{
    switch (lr & ~1u)
    {
    case 0x100F826:
    case 0x100F89C:
    case 0x101359C:
    case 0x101366A:
    case 0x10136BC:
    case 0x1013776:
    case 0x10137B0:
    case 0x10137BE:
    case 0x10137E4:
    case 0x10137F6:
        return true;
    default:
        return false;
    }
}

static void vm_trace_scene_bootstrap_loading_overlay_candidate(u32 pc, u32 lr)
{
    static u32 s_overlayCandidateTraceCount = 0;
    u8 sceneTickGate3 = 0;
    u8 sceneTickGate4 = 0;
    u8 load0 = 0;
    u8 load1 = 0;
    u8 load2 = 0;

    if (s_overlayCandidateTraceCount >= 24)
        return;
    ++s_overlayCandidateTraceCount;
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 23655, &sceneTickGate3, 1);
        uc_mem_read(MTK, Global_R9 + 23656, &sceneTickGate4, 1);
        uc_mem_read(MTK, Global_R9 + 23673, &load0, 1);
        uc_mem_read(MTK, Global_R9 + 23674, &load1, 1);
        uc_mem_read(MTK, Global_R9 + 23675, &load2, 1);
    }
    vm_net_trace("trace_loading_overlay_candidate pc=%08x lr=%08x caller=%08x last=%08x tick=%u sceneTickGate=%u,%u loadFlags=%u,%u,%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lr & ~1u,
                 lastAddress,
                 g_schedulerTick,
                 sceneTickGate3,
                 sceneTickGate4,
                 load0,
                 load1,
                 load2,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_runtime_tick(const char *label, u32 pc)
{
    static u32 s_sceneTickTraceCount = 0;
    u32 lr = 0;
    u32 r0 = 0;
    u32 r4 = 0;
    u32 r6 = 0;
    u32 sceneObj = 0;
    u32 sceneSubObj = 0;
    u8 mode1 = 0;
    u8 pendingResync = 0;
    u8 tickFlag0 = 0;
    u8 tickFlag1 = 0;
    u8 tickFlag2 = 0;
    u8 tickGate3 = 0;
    u8 tickGate4 = 0;
    u8 auxGateD = 0;
    u8 sceneTickGate3 = 0;
    u8 sceneTickGate4 = 0;
    u8 load0 = 0;
    u8 load1 = 0;
    u8 load2 = 0;
    char uiName[96];

    if (s_sceneTickTraceCount >= 320 || !Global_R9)
        return;

    uiName[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    uc_mem_read(MTK, Global_R9 + 21676, &sceneObj, 4);
    uc_mem_read(MTK, Global_R9 + 23655, &sceneTickGate3, 1);
    uc_mem_read(MTK, Global_R9 + 23656, &sceneTickGate4, 1);
    uc_mem_read(MTK, Global_R9 + 23673, &load0, 1);
    uc_mem_read(MTK, Global_R9 + 23674, &load1, 1);
    uc_mem_read(MTK, Global_R9 + 23675, &load2, 1);
    if (sceneObj)
    {
        uc_mem_read(MTK, sceneObj + 1, &mode1, 1);
        uc_mem_read(MTK, sceneObj + 2, &pendingResync, 1);
        uc_mem_read(MTK, sceneObj + 0x40, &sceneSubObj, 4);
    }
    if (r4)
    {
        uc_mem_read(MTK, r4 + 0, &tickFlag0, 1);
        uc_mem_read(MTK, r4 + 1, &tickFlag1, 1);
        uc_mem_read(MTK, r4 + 2, &tickFlag2, 1);
        uc_mem_read(MTK, r4 + 3, &tickGate3, 1);
        uc_mem_read(MTK, r4 + 4, &tickGate4, 1);
    }
    if (r6)
        uc_mem_read(MTK, r6 + 0x0D, &auxGateD, 1);

    if (label && strcmp(label, "oneshot_sync_check") == 0 && tickFlag1 == 0 && load0 == 0)
        return;

    vm_net_read_active_ui_name(uiName, sizeof(uiName));
    ++s_sceneTickTraceCount;
    vm_net_trace("trace_scene_runtime_tick label=%s pc=%08x lr=%08x last=%08x tick=%u seq=%u r0=%08x r4=%08x r6=%08x"
                 " tickFlags=%u,%u,%u,%u,%u auxGateD=%u"
                 " sceneObj=%08x subObj=%08x mode1=%u pendingResync=%u sceneTickGate=%u,%u loadFlags=%u,%u,%u"
                 " uiName=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 s_sceneTickTraceCount,
                 r0,
                 r4,
                 r6,
                 tickFlag0,
                 tickFlag1,
                 tickFlag2,
                 tickGate3,
                 tickGate4,
                 auxGateD,
                 sceneObj,
                 sceneSubObj,
                 mode1,
                 pendingResync,
                 sceneTickGate3,
                 sceneTickGate4,
                 load0,
                 load1,
                 load2,
                 uiName,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_draw_map_tile_entry(u32 pc)
{
    static u32 s_drawMapTileAnomalyCount = 0;
    u32 lr = 0;
    u32 tileId = 0;
    u32 x = 0;
    u32 y = 0;
    u32 w = 0;
    u32 h = 0;
    u32 repeat = 0;
    u32 mapMgr = 0;
    u32 tileInfoBase = 0;
    u32 resourceTable = 0;
    u32 resourcePtr = 0;
    int tileIndex = -1;
    u16 imgW = 0;
    u16 imgH = 0;

    if (!Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_read(MTK, UC_ARM_REG_R0, &tileId);
    uc_reg_read(MTK, UC_ARM_REG_R1, &x);
    uc_reg_read(MTK, UC_ARM_REG_R2, &y);
    uc_reg_read(MTK, UC_ARM_REG_R3, &w);

    {
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp)
        {
            uc_mem_read(MTK, sp, &h, 4);
            uc_mem_read(MTK, sp + 4, &repeat, 4);
        }
    }

    uc_mem_read(MTK, Global_R9 + 39664, &mapMgr, 4);
    if (mapMgr)
    {
        uc_mem_read(MTK, mapMgr + 4, &tileInfoBase, 4);
        uc_mem_read(MTK, mapMgr, &resourceTable, 4);
        if (resourceTable)
            uc_mem_read(MTK, resourceTable + 0x10, &resourceTable, 4);
    }
    if (tileInfoBase && tileId < 1024)
    {
        short signedIndex = -1;
        uc_mem_read(MTK, tileInfoBase + tileId * 8 + 2, &signedIndex, 2);
        tileIndex = (int)signedIndex;
        if (tileIndex >= 0 && resourceTable)
        {
            uc_mem_read(MTK, resourceTable + (u32)tileIndex * 4, &resourcePtr, 4);
            if (resourcePtr)
            {
                uc_mem_read(MTK, resourcePtr + 4, &imgW, 2);
                uc_mem_read(MTK, resourcePtr + 6, &imgH, 2);
            }
        }
    }

    if (resourcePtr != 0 && tileIndex >= 0)
        return;
    if (s_drawMapTileAnomalyCount >= 32)
        return;

    ++s_drawMapTileAnomalyCount;
    vm_net_trace("trace_draw_map_tile_entry pc=%08x lr=%08x last=%08x tick=%u tileId=%u x=%u y=%u w=%u h=%u repeat=%u mapMgr=%08x tileInfo=%08x tileIndex=%d resTable=%08x resPtr=%08x img=%ux%u activeScreen=%08x currentThis=%08x\n",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 tileId,
                 x,
                 y,
                 w,
                 h,
                 repeat,
                 mapMgr,
                 tileInfoBase,
                 tileIndex,
                 resourceTable,
                 resourcePtr,
                 (u32)imgW,
                 (u32)imgH,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_piclib_slot_load(const char *label, u32 pc)
{
    u32 lr = 0;
    u32 piclib = 0;
    u32 tileId = 0;
    u32 namesBase = 0;
    u32 tileInfoBase = 0;
    u32 namePtr = 0;
    int tileIndex = -1;
    u8 loadedFlag = 0;
    char nameBuf[96];

    nameBuf[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);

    if (pc == 0x10433BA || pc == 0x10433DE)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &piclib);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tileId);
    }
    else
    {
        u32 r4 = 0;
        u32 r5 = 0;
        uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
        uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
        piclib = r4;
        tileId = r5 >> 3;
    }

    if (tileId != 25 && tileId != 26 && tileId != 29 &&
        tileId != 30 && tileId != 31 && tileId != 65)
        return;

    if (piclib)
    {
        uc_mem_read(MTK, piclib + 4, &tileInfoBase, 4);
        uc_mem_read(MTK, piclib + 8, &namesBase, 4);
        if (tileInfoBase)
        {
            short signedIndex = -1;
            uc_mem_read(MTK, tileInfoBase + tileId * 8, &loadedFlag, 1);
            uc_mem_read(MTK, tileInfoBase + tileId * 8 + 2, &signedIndex, 2);
            tileIndex = (int)signedIndex;
        }
        if (namesBase)
            uc_mem_read(MTK, namesBase + tileId * 4, &namePtr, 4);
    }
    if (namePtr)
        vm_net_read_guest_ascii_label(namePtr, nameBuf, sizeof(nameBuf));

    vm_net_trace("trace_piclib_slot_load label=%s pc=%08x lr=%08x last=%08x tick=%u piclib=%08x tileId=%u loaded=%u tileIndex=%d namesBase=%08x namePtr=%08x name=%s activeScreen=%08x currentThis=%08x\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 piclib,
                 tileId,
                 (u32)loadedFlag,
                 tileIndex,
                 namesBase,
                 namePtr,
                 nameBuf,
                 vmAddedScreen,
                 g_currentScreenThis);
}

static void vm_net_trace_scene_colnames_entry(u32 pc, u32 lr, u32 packet)
{
    u32 colnum = 0;
    u32 colblob = 0;
    if (packet)
    {
        uc_mem_read(MTK, packet + 76, &colnum, 4);
        uc_mem_read(MTK, packet + 40, &colblob, 4);
    }
    vm_net_trace("trace_scene_colnames_entry pc=%08x lr=%08x last=%08x tick=%u packet=%08x rawFnColnum=%08x rawFnColnames=%08x\n",
                 pc, lr, lastAddress, g_schedulerTick, packet, colnum, colblob);
}

static void vm_net_trace_scene_colnames_item(u32 pc, u32 lr, u32 index, u32 len, u32 ptr)
{
    char nameBuf[96];
    vm_net_read_guest_ascii_label(ptr, nameBuf, sizeof(nameBuf));
    vm_net_trace("trace_scene_colnames_item pc=%08x lr=%08x last=%08x tick=%u index=%u len=%u ptr=%08x text=%s\n",
                 pc, lr, lastAddress, g_schedulerTick, index, len, ptr, nameBuf);
}

static void vm_net_trace_scene_list_return(const char *label, u32 pc, u32 lr)
{
    u32 packet = 0;
    u32 kind = 0;
    u32 subtype = 0;
    u8 sceneGate = 0;
    u8 sceneState = 0;
    u8 load0 = 0;
    u8 load1 = 0;
    u8 load2 = 0;
    uc_reg_read(MTK, UC_ARM_REG_R0, &packet);
    if (packet)
    {
        uc_mem_read(MTK, packet + 4, &kind, 4);
        uc_mem_read(MTK, packet + 8, &subtype, 4);
    }
    if (Global_R9)
    {
        uc_mem_read(MTK, Global_R9 + 21900, &sceneGate, 1);
        uc_mem_read(MTK, Global_R9 + 21896, &sceneState, 1);
        uc_mem_read(MTK, Global_R9 + 23673, &load0, 1);
        uc_mem_read(MTK, Global_R9 + 23674, &load1, 1);
        uc_mem_read(MTK, Global_R9 + 23675, &load2, 1);
    }
    vm_net_trace("trace_scene_list_return label=%s pc=%08x lr=%08x last=%08x tick=%u packet=%08x kind=%u subtype=%u sceneGate=%u sceneState=%u loadFlags=%u,%u,%u\n",
                 label ? label : "unknown",
                 pc,
                 lr,
                 lastAddress,
                 g_schedulerTick,
                 packet,
                 kind,
                 subtype,
                 sceneGate,
                 sceneState,
                 load0,
                 load1,
                 load2);
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

static void vm_trace_lcd_text_call(const char *apiName, u32 idx, u32 strPtr, u32 r0, u32 r1, u32 r2, u32 r3, u32 sp, int x, int y, u16 color, const u8 *gbkText)
{
#if TRACE_LCD_TEXT
    if (y < -32 || y > 320)
        return;
    u32 lr = 0;
    u32 st[8] = {0};
    u32 savedRegs[9] = {0};
    u32 guestCaller = 0;
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, sp, st, sizeof(st));
    uc_mem_read(MTK, sp + 0xCC, savedRegs, sizeof(savedRegs));
    guestCaller = savedRegs[8];
    gbk_to_utf8((u8 *)gbkText, sprintfBuff, mySizeOf(sprintfBuff));
    int fullWidth = mesureStringWidth((char *)gbkText);
    int curWidth = mesureStringWidthWithGbkWidth((char *)gbkText, getFontWidth());
    int fontApiWidth = (g_currentFontType == 0) ? getFontCellWidth() : getFontWidth();
    vm_net_trace("lcd_text_call api=%s idx=%u ptr=%08x x=%d y=%d color=%04x fontType=%u fontApiW=%d fullW=%d curW=%d lr=%08x last=%08x guestCaller=%08x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x st=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x saved=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x text=%s\n",
                 apiName, idx, strPtr, x, y, color, g_currentFontType, fontApiWidth, fullWidth, curWidth, lr, lastAddress,
                 guestCaller,
                 r0, r1, r2, r3, sp, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7],
                 savedRegs[0], savedRegs[1], savedRegs[2], savedRegs[3], savedRegs[4], savedRegs[5], savedRegs[6], savedRegs[7], savedRegs[8],
                 sprintfBuff);
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

static bool vm_net_mock_put_object_ascii_digit(u8 *out, u32 outCap, u32 *pos, const char *name, u8 digit)
{
    return vm_net_mock_put_object_u8(out, outCap, pos, name, (u8)('0' + digit));
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

static bool vm_net_mock_put_object_raw(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *data, u16 dataLen)
{
    return vm_net_mock_put_object_entry(out, outCap, pos, name, data, dataLen);
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

static u32 vm_net_mock_env_u32(const char *name, u32 fallback)
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
    char requestName[256];
    requestName[0] = 0;
    bool haveRequestName = vm_net_mock_get_object_string_field(request, requestLen, "name", requestName, sizeof(requestName)) &&
                           requestName[0] != 0;
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);
    const char *payloadName = NULL;
    u32 updateLen = vm_net_mock_load_resource_update_payload(request,
                                                             requestLen,
                                                             haveRequestName ? requestName : NULL,
                                                             updateData,
                                                             sizeof(updateData),
                                                             &payloadName);
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
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", haveRequestType ? requestType : 0))
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
    vm_net_trace("mock_update_chunk_response reqName=%s reqType=%u name=%s type=%u start=%u chunk=%u totalsize=%u crc=%u\n",
                 haveRequestName ? requestName : "<empty>",
                 haveRequestType ? requestType : 0,
                 payloadName ? payloadName : "MMORPGTempcbm",
                 haveRequestType ? requestType : 0,
                 start,
                 chunkLen,
                 updateLen,
                 crc);
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

static bool vm_net_mock_seq_put_string_list(
    u8 *out, u32 outCap, u32 *pos, const char *const *values, u32 count)
{
    u32 i = 0;
    for (i = 0; i < count; ++i)
    {
        if (!vm_net_mock_seq_put_string(out, outCap, pos, values[i]))
            return false;
    }
    return true;
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

static u16 vm_net_mock_scene_spawn_x(void)
{
    return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_X", 223);
}

static u16 vm_net_mock_scene_spawn_y(void)
{
    return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_Y", 382);
}

static const char *vm_net_mock_default_scene_name(void)
{
    return "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31"; /* GBK: c00PengLaiXianDao_01 */
}

static const char *vm_net_mock_scene_key_name(void)
{
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    /*
     * This key is copied by parse_actorinfo_response() into R9+0x5E46 and later
     * reused as the mode-10 descriptor name by scene_rebuild_runtime_nodes().
     * The ASCII experiment proved the update path, but also bypassed the local
     * .sce scene descriptor and left the map background black when cached.
     * Keep the default aligned with 30/1.scene and use CBE_SCENE_KEY for
     * non-colliding descriptor experiments.
     */
    return vm_net_mock_default_scene_name();
}

static const char *vm_net_mock_default_scene_title(void)
{
    return "\xc5\xee\xc0\xb3\x2d\xcd\xad\xc8\xb8\xcc\xa8"; /* GBK: PengLai-TongQueTai */
}

static const char *vm_net_mock_fb_target_info_text(void)
{
    const char *overrideInfo = vm_net_mock_env_str("CBE_FB_TARGET_INFO", "");
    if (overrideInfo != NULL && overrideInfo[0] != 0)
        return overrideInfo;
    return vm_net_mock_default_scene_title();
}

static bool vm_net_mock_put_scene_fields_with(u8 *out, u32 outCap, u32 *pos,
                                               bool includeResult, bool includeType, u8 requestType,
                                               const char *sceneName, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    if (posInfoLen == 0)
        return false;
    if (includeResult && !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (includeType && !vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", sceneName ? sceneName : vm_net_mock_default_scene_name()))
        return false;
    return vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen);
}

static bool vm_net_mock_put_scene_ack_without_posinfo(u8 *out, u32 outCap, u32 *pos,
                                                      u8 requestType, const char *sceneName)
{
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    return vm_net_mock_put_object_string(out, outCap, pos, "scene",
                                         sceneName ? sceneName : vm_net_mock_default_scene_name());
}

static bool vm_net_mock_put_scene_fields(u8 *out, u32 outCap, u32 *pos, bool includeResult, bool includeType, u8 requestType)
{
    return vm_net_mock_put_scene_fields_with(out, outCap, pos,
                                             includeResult, includeType, requestType,
                                             vm_net_mock_default_scene_name(),
                                             vm_net_mock_scene_spawn_x(),
                                             vm_net_mock_scene_spawn_y());
}

typedef struct
{
    char scene[64];
    u16 x;
    u16 y;
    u32 exitId;
} vm_net_mock_scene_change_target;

static vm_net_mock_scene_change_target g_vm_net_mock_last_scene_change_target;
static bool g_vm_net_mock_last_scene_change_target_valid = false;
static u8 g_vm_net_mock_last_scene_change_fb4_type = 1;

static void vm_net_mock_remember_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_scene_change_target = *target;
    g_vm_net_mock_last_scene_change_target_valid = true;
}

static void vm_net_mock_get_scene_change_target(const u8 *request, u32 requestLen,
                                                vm_net_mock_scene_change_target *target)
{
    char mapId[64];
    u32 exitId = 0;
    memset(target, 0, sizeof(*target));
    snprintf(target->scene, sizeof(target->scene), "%s", vm_net_mock_default_scene_name());
    target->x = vm_net_mock_scene_spawn_x();
    target->y = vm_net_mock_scene_spawn_y();
    target->exitId = 0;

    if (!vm_net_mock_get_object_string_field(request, requestLen, "mapID", mapId, sizeof(mapId)))
        return;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId);
    target->exitId = exitId;

    if (mapId[0] != 0)
        snprintf(target->scene, sizeof(target->scene), "%s", mapId);

    if (strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0)
    {
        if (exitId == 1)
        {
            target->x = 128;
            target->y = 45;
        }
        else
        {
            target->x = 396;
            target->y = 473;
        }
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31") == 0)
    {
        target->x = 223;
        target->y = 382;
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
    {
        target->x = 105;
        target->y = (exitId == 1) ? 58 : 395;
    }
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

static bool vm_net_mock_append_misc_player_sync8_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    static const u8 seqOne[] = {0x00, 0x02, 0x00, 0x01};
    /*
     * Focused protocol experiment for the bundled post-login 1/7/7 type=1
     * request. Static analysis shows top-level 7 -> local subtype 8 with
     * type=4/result=1 is one of the few branches that can seed the empty
     * type=15 HUD source head and immediately trigger rebuild(2)->rebuild(1).
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 8, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 4))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "seq", seqOne, sizeof(seqOne)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_misc_player_type_object(u8 *out, u32 outCap, u32 *pos, u8 subtype)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (subtype == 20)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
    }
    else if (subtype == 32)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
            return false;
    }
    else
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneName, u16 spawnX, u16 spawnY)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, pos, false, false, 0,
                                           sceneName, spawnX, spawnY))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_scene_enter_object_for_scene(out, outCap, pos,
                                                          vm_net_mock_default_scene_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_append_scene_room_npc_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 roomList[128];
    u8 colNames[64];
    u32 roomListLen = 0;
    u32 colNamesLen = 0;
    static const char *const roomColumns[] = {"id", "room", "scene", "state"};
    static const u8 emptyNpcInfo[] = {0};
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roomColumns, 4))
        return false;
    if (!vm_net_mock_seq_put_u32(roomList, sizeof(roomList), &roomListLen, 1001))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "1001"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Penglai"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Scene"))
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 3, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "curpage", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "pagenum", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "roomnum", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "roomlist", roomList, (u16)roomListLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "npcinfo", emptyNpcInfo, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_room_roles_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 rolesInfo[128];
    u8 colNames[64];
    u32 rolesInfoLen = 0;
    u32 colNamesLen = 0;
    static const char *const roleColumns[] = {"name", "job", "level", "state"};
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roleColumns, 4))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Codex"))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Warrior"))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Lv1"))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Ready"))
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 7, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roomid", 1001))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "rolenum", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "rolesinfo", rolesInfo, (u16)rolesInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly);

static u32 vm_net_mock_build_group_type1_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen < 9 || outCap < 5)
        return 0;

    u32 leadId = 10001;
    bool hasGroup10 = false;
    bool hasType1 = false;
    bool enableMiscSync8 = vm_net_mock_env_u8("CBE_GROUP_TYPE1_MISC_SYNC8", 0) != 0;
    bool includeRoomNpc = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_NPC", 0) != 0;
    bool includeRoomRoles = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_ROLES", 0) != 0;
    bool includeFbTargetClear = false;
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
    u8 objectCount = 0;
    if (!vm_net_mock_append_group_info_object(out, outCap, &pos, leadId))
        return 0;
    objectCount += 1;
    if (hasType1 && !vm_net_mock_append_type1_object(out, outCap, &pos))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && enableMiscSync8 && !vm_net_mock_append_misc_player_sync8_object(out, outCap, &pos))
        return 0;
    if (hasType1 && enableMiscSync8)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 20))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 32))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && includeRoomNpc && !vm_net_mock_append_scene_room_npc_object(out, outCap, &pos))
        return 0;
    if (hasType1 && includeRoomNpc)
        objectCount += 1;
    if (hasType1 && includeRoomRoles && !vm_net_mock_append_scene_room_roles_object(out, outCap, &pos))
        return 0;
    if (hasType1 && includeRoomRoles)
        objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_group_type1_response leadid=%u objects=%u miscSync8=%u bundledType2Type3=%u sceneEnter=%u sceneRoomNpc=%u sceneRoomRoles=%u bundledSceneFollowup=%u bundledFbTargetClear=%u len=%u\n",
                 leadId,
                 objectCount,
                 (hasType1 && enableMiscSync8) ? 1u : 0u,
                 hasType1 ? 1u : 0u,
                 0u,
                 (hasType1 && includeRoomNpc) ? 1u : 0u,
                 (hasType1 && includeRoomRoles) ? 1u : 0u,
                 0u,
                 includeFbTargetClear ? 1u : 0u,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_scene_change_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_scene_change_target target;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_remember_scene_change_target(&target);
    vm_net_trace("mock_scene_change_response scene=%s exitId=%u pos=%u,%u len=%u\n",
                 target.scene, target.exitId, target.x, target.y, pos);
    return pos;
}

static bool vm_net_mock_append_login_tail_skill_objects(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0c, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "learnednum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "learnedskill", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_books42_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_empty11_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 11, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_result12_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneKey, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    u32 objectStart = 0;
    if (posInfoLen == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name", sceneKey))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_result12_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_fb_target_result12_for_scene(out, outCap, pos,
                                                          vm_net_mock_scene_key_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_append_fb_target_result4_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 typeValue, const char *infoText)
{
    u32 objectStart = 0;
    if (infoText == NULL)
        infoText = "";
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "min", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", typeValue))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "info", infoText))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_scene_change_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSkill = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool splitScenePosCommit = true;
    u8 fb4Type = 1;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_scene_change_target target;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 4)
        {
            (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &fb4Type);
            break;
        }
    }
    if (splitScenePosCommit)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    else
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    if (needFb11)
    {
        if (!splitScenePosCommit)
        {
            if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
                return 0;
            objectCount += 1;
        }
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (needFb4)
    {
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
    }
    if (needSkill)
    {
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos))
            return 0;
        objectCount += 2;
    }
    else if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (!splitScenePosCommit)
    {
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    vm_net_trace("mock_scene_change_combo_response objects=%u skill=%u books=%u fb11=%u fb11empty=%u fb12=%u fb4=%u fb4type=%u taskSubset=0 sceneChangeResult=%u trailingSceneEnter=%u deferredSceneCompletion=%u scene=%s exitId=%u pos=%u,%u len=%u\n",
                 objectCount,
                 needSkill ? 1u : 0u,
                 needBooks ? 1u : 0u,
                 needFb11 ? 1u : 0u,
                 needFb11 ? 1u : 0u,
                 (needFb11 && !splitScenePosCommit) ? 1u : 0u,
                 needFb4 ? 1u : 0u,
                 fb4Type,
                 1u,
                 splitScenePosCommit ? 0u : 1u,
                 splitScenePosCommit ? 1u : 0u,
                 target.scene,
                 target.exitId,
                 target.x,
                 target.y,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_type27_followup_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSceneDefault = vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    u8 fb4Type = 1;
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 objectStart = 0;

    if (outCap < pos || !needFb4)
        return 0;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 4)
        {
            (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &fb4Type);
            break;
        }
    }

    if (needSceneDefault)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 12, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    if (needFb11)
    {
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, ""))
        return 0;
    objectCount += 1;

    if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_type27_followup_combo_response objects=%u sceneDefault=%u fb11=%u fb4type=%u books=%u len=%u\n",
                 objectCount, needSceneDefault ? 1u : 0u, needFb11 ? 1u : 0u, fb4Type, needBooks ? 1u : 0u, pos);
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

static bool vm_net_mock_is_scene_resource_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    bool needSkillBooks = false;
    bool needTaskFamily = false;
    if (request == NULL || (requestLen != 49 && requestLen != 39))
        return false;
    needSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (needSkillBooks &&
        (!vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
         !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42)))
        return false;
    needTaskFamily = vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    if (!needTaskFamily)
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_is_scene_task_subset_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    if (request == NULL || requestLen < 24)
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5))
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_append_taskinfo_empty1_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "taskinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_tasktypes_empty13_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u32 blobPos = 0;
    u8 blob[64];

    memset(blob, 0, sizeof(blob));
    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u8(blob, sizeof(blob), &blobPos, 0))
            return false;
        if (!vm_net_mock_seq_put_string(blob, sizeof(blob), &blobPos, ""))
            return false;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 13, &objectStart))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "tasktypes", blob, (u16)blobPos))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_taskaction_empty14_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "action", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "tasknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "taskinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_actor_other_empty10_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "othernum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "otherinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_is_actor_other_only10_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_scene_interaction_followup_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen != 61)
        return false;
    return vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 14) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 4) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 5) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 14, 6);
}

static u32 vm_net_mock_build_actor_other_only10_response(u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_actor_other_only10_response othernum=0 len=%u\n", pos);
    return pos;
}

static bool vm_net_mock_is_actor_moveinfo_upload_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 1)
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "moveinfo"))
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_actor_moveinfo_ack_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (outCap < pos || !vm_net_mock_is_actor_moveinfo_upload_request(request, requestLen))
        return 0;
    /*
     * net_handle_actor_move_info() only reads moveinfo for subtype 2. Subtype 1
     * falls through the default branch at 0x01012B0C, so keep this upload ack
     * empty until the live server semantics are recovered.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 1, &objectStart))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_actor_moveinfo_ack_response subtype=1 empty len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_scene_interaction_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (outCap < pos || !vm_net_mock_is_scene_interaction_followup_request(request, requestLen))
        return 0;
    /*
     * The live post-scene interaction request bundles 14/14,14/4,14/5,14/6,
     * but main business response dispatch has no kind-14 consumer. Answer only
     * the confirmed 2/10 branch so net_handle_actor_move_info() refreshes the
     * compact current-node tuple and the unknown 14-family remains evidence.
     */
    if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_scene_interaction_followup_response objects=1 othernum=0 ignoredReq14=14,4,5,6 len=%u\n",
                 pos);
    return pos;
}

static bool vm_net_mock_append_info_banner_clear12_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly)
{
    /*
     * In the first scene-enter dispatch window we have a confirmed practical
     * limit of 10 objects.  Keep the skill/book pair plus empty 6/1 taskinfo
     * in this window; omitting either causes the client to immediately request
     * the large 12/1+7/42+6/*+2/10+25/5 follow-up after 30/1 closes the gate.
     */
    if (includeSkillBooks)
    {
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, pos))
            return false;
        *objectCount += 2;
    }

    if (!vm_net_mock_append_taskinfo_empty1_object(out, outCap, pos))
        return false;
    *objectCount += 1;

    /*
     * Case 13 loops over six tasktype slots. Each slot is a tagged i8 type id
     * followed by a len16 C string; use raw field payload so the stream cursor
     * starts directly at the first record.
     */
    if (includeTaskLists)
    {
        if (!vm_net_mock_append_tasktypes_empty13_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_taskaction_empty14_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    if (includeActorOther)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    if (includeInfoBanner)
    {
        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    /*
     * Earlier experiments showed the 27/12+27/11 family suppresses the large
     * WT49 follow-up, while 27/11 alone does not set the needed fb state. Pair
     * 27/12 and 27/11 with 27/4 ready/finalize so the gate is restored in the
     * same follow-up scan.
     */
    if (includeFbTargetClear)
    {
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1, ""))
            return false;
        *objectCount += 1;
    }
    else if (includeFbTargetSeedOnly)
    {
        /*
         * Hypothesis under test: after the requested WT49 resource/task/other
         * family is answered, run the complete fb-target trio in one follow-up
         * scan. 27/11 alone leaves sceneGateA at 0, but 27/4 immediately after
         * it restores the gate pair; use a non-empty info string so case 27/4
         * also exercises its scene-object info side effects.
         */
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1,
                                                        vm_net_mock_fb_target_info_text()))
            return false;
        *objectCount += 1;
    }

    return true;
}

static u32 vm_net_mock_build_scene_resource_followup_response(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    bool includeSkillBooks = false;
    if (outCap < pos || !vm_net_mock_is_scene_resource_followup_request(request, requestLen))
        return 0;

    includeSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                        vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           includeSkillBooks, true, true,
                                                           includeSkillBooks ? false : true, false,
                                                           includeSkillBooks))
        return 0;
    if (!vm_net_mock_append_scene_enter_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_scene_resource_followup_response objects=%u skillBooks=%u taskinfo=empty tasktypes=6xempty task14=zero othernum=0 result25_5=%u fbFull12_11_4Info=%u trailingSceneEnter=1 len=%u\n",
                 objectCount, includeSkillBooks ? 1u : 0u, includeSkillBooks ? 0u : 1u,
                 includeSkillBooks ? 1u : 0u, pos);
    return pos;
}

static u32 vm_net_mock_build_scene_task_subset_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    bool completeDeferredScene = g_vm_net_mock_last_scene_change_target_valid;
    if (outCap < pos || !vm_net_mock_is_scene_task_subset_followup_request(request, requestLen))
        return 0;

    /*
     * This post-scene-change request is the WT49 task/other/banner subset
     * without skill/book objects. Experiments that moved scene completion into
     * this response caused either bounce-back or late gate-off, so keep this
     * response parser-safe and side-family only.
     */
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           false, true, true, true, false, false))
        return 0;
    if (completeDeferredScene)
    {
        const vm_net_mock_scene_change_target *target = &g_vm_net_mock_last_scene_change_target;
        u32 objectStart = 0;
        if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target->scene, target->x, target->y))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                        g_vm_net_mock_last_scene_change_fb4_type,
                                                        vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2,
                                               target->scene, target->x, target->y))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        g_vm_net_mock_last_scene_change_target_valid = false;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_trace("mock_scene_task_subset_followup_response objects=%u taskinfo=empty tasktypes=6xempty task14=zero othernum=0 result25_5=4 trailingSceneEnter=0 deferredSceneChangeResult=%u deferredSceneCompletion=%u lateDispatchExpected=%u len=%u\n",
                 objectCount,
                 completeDeferredScene ? 1u : 0u,
                 completeDeferredScene ? 1u : 0u,
                 completeDeferredScene ? 0u : 1u,
                 pos);
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
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos))
        return 0;

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

static const char *vm_net_mock_actor_preview_image_name(u8 actorJob, u8 actorSex)
{
    const char *overrideName = vm_net_mock_env_str("CBE_ACTOR_PREVIEW_IMAGE", "");
    (void)actorJob;
    (void)actorSex;
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    return "";
}

static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
    const char *displayName = "JHOnline";
    u32 roleLevel = vm_net_mock_env_u8("CBE_ROLE_LEVEL", 1);
    u32 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u32 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    u32 visualVariant = 0;
    u32 visualGroup = 0;
    u32 primaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_HP_CURRENT",
                                             vm_net_mock_env_u32("CBE_ACTOR_HP", 120));
    u32 primaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_HP_MAX", 120);
    u32 secondaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_MP_CURRENT",
                                               vm_net_mock_env_u32("CBE_ACTOR_MP", 100));
    u32 secondaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_MP_MAX", 100);
    u32 actorSummaryValue = vm_net_mock_env_u32("CBE_ACTOR_SUMMARY_VALUE", 0);
    u32 actorGap09C0 = vm_net_mock_env_u32("CBE_ACTOR_GAP09C0", 0);
    u32 actorSummaryStatus = vm_net_mock_env_u32("CBE_ACTOR_STATUS_WORD", roleLevel);
    u8 actorStateByte0 = vm_net_mock_env_u8("CBE_ACTOR_STATE_BYTE0", 0);
    u8 actorStateByte1 = vm_net_mock_env_u8("CBE_ACTOR_STATE_BYTE1", 0);
    u8 actorTargetX = 12;
    u8 actorTargetY = 10;
    u8 actorField11E = 0;
    u8 actorField120 = 0;
    const char *shortLabel = vm_net_mock_env_str("CBE_ACTOR_SHORT_LABEL", "10001");
    const char *actorResource = NULL;
    const char *sceneKey = NULL;
    u16 actorResourceArg = 0;
    u16 actorGridX = 0;
    u16 actorGridY = 0;
    u16 motionResourceArg0 = 0;
    u16 motionResourceArg1 = 0;
    u32 primaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_HP_DISPLAY_MAX", primaryBaseMax);
    u32 secondaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_MP_DISPLAY_MAX", secondaryBaseMax);
    u32 actorGap0CC0 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC0", 0);
    u32 actorGap0CC4 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC4", 0);
    u32 actorGap0CC8 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC8", 0);

    if (roleLevel == 0)
        roleLevel = 1;
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    if (primaryBaseMax == 0)
        primaryBaseMax = 120;
    if (secondaryBaseMax == 0)
        secondaryBaseMax = 100;
    if (primaryCurrent > primaryBaseMax)
        primaryCurrent = primaryBaseMax;
    if (secondaryCurrent > secondaryBaseMax)
        secondaryCurrent = secondaryBaseMax;
    if (primaryDisplayMax == 0)
        primaryDisplayMax = primaryBaseMax;
    if (secondaryDisplayMax == 0)
        secondaryDisplayMax = secondaryBaseMax;

    /*
     * Fresh scene-enter keeps the compact title-facing knobs but the scene-side
     * portrait/widget picker is not fully 0-based. Static `scene_runtime_init_
     * and_sync()` now proves the portrait index is `visualGroup + 2*visualVariant - 1`,
     * so `visualVariant` remains 0-based (`job-1`) while `visualGroup` must stay
     * in the original `1..2` family rather than `0..1`.
     */
    visualVariant = actorJob - 1;
    visualGroup = actorSex + 1;

    actorTargetX = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_X", 12);
    actorTargetY = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_Y", 10);
    actorGridX = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_X", vm_net_mock_scene_spawn_x());
    actorGridY = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_Y", vm_net_mock_scene_spawn_y());
    actorField11E = vm_net_mock_env_u8("CBE_ACTOR_BYTE_11E", actorTargetX);
    actorField120 = vm_net_mock_env_u8("CBE_ACTOR_BYTE_120", actorTargetY);
    actorResource = vm_net_mock_actor_resource_name((u8)actorJob, (u8)actorSex);
    sceneKey = vm_net_mock_scene_key_name();
    actorResourceArg = (u16)vm_net_mock_env_u32("CBE_ACTOR_RESOURCE_ARG", 0);
    motionResourceArg0 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG0", actorGridX);
    motionResourceArg1 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG1", actorGridY);

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 10001))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualVariant))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualGroup))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 10001))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, displayName))
        return 0;

    /*
     * Fresh scene-enter (`parse_actorinfo_response(a2==0)`) does not match the
     * older guessed "level/current/base/..." order directly. The confirmed
     * stream from the second string onward is:
     *   tagged u32 summaryStatus (stored into a word slot)
     *   u32 primaryCurrent
     *   u32 primaryBaseMax
     *   u32 secondaryCurrent
     *   u32 secondaryBaseMax
     *   u32 extra132
     *   six truncated u32 -> word fields
     *   u32 summary176
     *   u32 gap09C0
     *   u8 state0
     *   u8 state1
     *   u32 primaryDisplayMax
     *   u32 secondaryDisplayMax
     *   u8 targetPosX
     *   u8 targetPosY
     *   str shortLabel
     *   str previewImage
     *   u32 gap0CC0
     *   u32 gap0CC4
     *   u32 gap0CC8
     *   str actorResource
     *   i16 actorResourceArg
     *   str sceneKey
     *   i16 motionArg0
     *   i16 motionArg1
     */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryStatus))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, vm_net_mock_env_u32("CBE_ACTOR_EXTRA132", 0)))
        return 0;

    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 0))
            return 0;
    }

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryValue))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap09C0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorStateByte0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorStateByte1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField11E))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField120))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, shortLabel))
        return 0;
    /*
     * This 20-byte mid-body string lands at actor+0x10A.  The final scene
     * consumer seen so far is `scene_draw_node_overhead_overlay()`, which treats
     * a non-empty value as an optional named overhead badge/icon. Do not seed it
     * with the actor's walk GIF; that makes body art appear as stray fragments.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                    vm_net_mock_actor_preview_image_name((u8)actorJob, (u8)actorSex)))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC0))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC4))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC8))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, actorResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, actorResourceArg))
        return 0;
    /*
     * parse_actorinfo_response copies this second trailing string directly into
     * R9+0x5E46, and fresh scene-enter later reuses that buffer as
     * currentMapIdText / the persistent +1141 scene-text slot. Keep it separate
     * from actorResource and the trailing grid/pose-like word pair.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, sceneKey))
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

static u32 vm_net_mock_build_title_role_list_actorinfo(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char roleName[] = "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
    u32 roleId = 10001;
    u16 roleLevel = (u16)vm_net_mock_env_u8("CBE_ROLE_LEVEL", 1);
    u8 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);

    if (roleLevel == 0)
        roleLevel = 1;
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    /*
     * title target50_render() derives a role portrait table index from the
     * compact role-list record as `job * 2 + (sex - 1)`, so the title-side
     * role-list contract expects `sex` to be 1-based here. A zero value wraps
     * into 0xFF and drives the render path into an invalid callback slot.
     */
    if (actorSex < 1 || actorSex > 2)
        actorSex = 1;

    /*
     * mmTitleMstarWqvga.cbm sub_3544() parses its actorinfo field as:
     *   count, then repeated (tagged u32, tagged u8, tagged u8, len16+cstr, tagged i16).
     * The field name collides with later in-scene actorinfo, but the title-side
     * payload is a compact role-list entry table instead of the large player blob.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorJob))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorSex))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, roleLevel))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_login_serverinfo_blob(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char serverName[] = "\xb2\xe2\xca\xd4\xd2\xbb\xc7\xf8"; /* GBK: ce shi yi qu */
    const char serverLabel[] = "\xcd\xc6\xbc\xf6"; /* GBK: tui jian */

    if (!vm_net_mock_seq_put_string(out, outCap, &pos, serverName))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, serverLabel))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 0))
        return 0;

    return pos;
}

static u8 vm_net_mock_resolve_login_success_subtype(u8 requestSubtype)
{
    const char *rawSubtype = vm_net_mock_env_str("CBE_LOGIN_TOP_TYPE", "");
    u8 actorSubtype = requestSubtype;

    if (rawSubtype != NULL && rawSubtype[0] != 0)
        actorSubtype = vm_net_mock_env_u8("CBE_LOGIN_TOP_TYPE", requestSubtype);

    if (actorSubtype != 1 && actorSubtype != 2 && actorSubtype != 3 && actorSubtype != 6)
        actorSubtype = 6;

    return actorSubtype;
}

static bool vm_net_mock_append_login_success_object(u8 *out,
                                                    u32 outCap,
                                                    u32 *pos,
                                                    u8 actorSubtype,
                                                    bool includeActorInfo,
                                                    u32 *actorInfoLenOut)
{
    u8 actorInfo[512];
    u32 actorInfoLen = 0;

    if (includeActorInfo)
    {
        memset(actorInfo, 0, sizeof(actorInfo));
        actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
        if (actorInfoLen == 0)
            return false;
    }

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
    if (includeActorInfo)
    {
        if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
            return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    return true;
}

static bool vm_net_mock_append_actorinfo_object(u8 *out, u32 outCap, u32 *pos, u32 *actorInfoLenOut)
{
    return vm_net_mock_append_login_success_object(out, outCap, pos, 1, true, actorInfoLenOut);
}

static u32 vm_net_mock_build_login_primary_validation_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[128];
    u32 serverInfoLen = 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(serverInfo, sizeof(serverInfo));
    if (serverInfoLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "servernum", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_login_primary_validation_response top=1,1,1 servernum=1 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_login_failure_response(u8 requestSubtype, const char *information, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 responseSubtype = requestSubtype ? requestSubtype : 1;
    const char *message = information ? information : "password error";

    if (outCap < pos)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, responseSubtype, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 2))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "information", message))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_login_failure_response top=1,1,%u result=2 info=%s len=%u\n",
                 responseSubtype, message, pos);
    return pos;
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

static bool vm_net_mock_append_title_mode15_result_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    /*
     * The active R9+0x29E8 title-local state machine has a narrower confirmed
     * gate: its type=1/subtype=15 success path goes straight to sub_39A4(),
     * which is the same mode<-1 transition we already correlated with the
     * role-selection state.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, 15, &objectStart))
        return false;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, pos, "result", 1))
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

static u32 vm_net_mock_build_title_mode15_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_title_mode15_response result=1 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_title_rolelist_stage_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 actorinfo[128];
    u32 actorinfoLen = 0;

    if (outCap < pos)
        return 0;

    actorinfoLen = vm_net_mock_build_title_role_list_actorinfo(actorinfo, sizeof(actorinfo));
    if (actorinfoLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 16, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorinfo, (u16)actorinfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_title_rolelist_stage_response top=1,1,16+1,1,4 actorinfo_len=%u len=%u\n",
                 actorinfoLen, pos);
    return pos;
}

static u32 vm_net_mock_build_title_role_select_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 actorId = 10001;
    u32 actorInfoLen = 0;

    if (outCap < pos)
        return 0;

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "actorID", &actorId);

    /* A minimal {result, actorID} subtype-6 ack is accepted by
     * role_manage_screen_handle_network(case 6), but as soon as subtype-15
     * pushes the client into the next bootstrap path the main scene-side logic
     * also sees this subtype-6 object. Static scene_runtime_init_and_sync()
     * treats subtype 6 as a full scene-enter family packet and reads fields
     * such as revivetype/type/practiseflag/pcimg/.../actorinfo from it. Reuse
     * the existing full subtype-6 success shell here so the follow-up subtype-15
     * experiment is not blocked by a missing scene-enter payload contract.
     */
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 6, true, &actorInfoLen))
        return 0;

    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_title_role_select_response top=1,1,6(full)+1,1,15 actorID=%u actorinfo_len=%u len=%u\n",
                 actorId,
                 actorInfoLen,
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_alt12_success_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (outCap < pos)
        return 0;

    /*
     * The live login tap currently sends a top-level 1/1/12 request. Static
     * title RE already has a dedicated wrapper for that alternate success path
     * (`sub_2A50 -> login_alt_result_dispatch -> login_stage_success_dispatch`),
     * so keep this response minimal and do not pre-inject later actorinfo here.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_trace("mock_login_alt12_success_response top=1,1,12 len=%u\n", pos);
    return pos;
}

static u32 vm_net_mock_build_login_actor_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    g_netMockEnterGameOffset = 0;
    g_netMockEnterGameChecksum = 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    u8 logJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 logSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    vm_net_trace("mock_login_actor_response actorinfo_len=%u top=1,1,%u job=%u sex=%u preview=%s actor=%s len=%u\n",
                 actorInfoLen,
                 actorSubtype,
                 logJob,
                 logSex,
                 vm_net_mock_actor_preview_image_name(logJob, logSex),
                 vm_net_mock_actor_resource_name(logJob, logSex),
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_role_list_only_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;

    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 1, false, NULL))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_login_role_list_response mode=login-success-plus-rolelist roles=1 maxroles=3 num=1 allpgs=1 len=%u\n",
                 pos);
    return pos;
}

static u32 vm_net_mock_build_login_actor_role_list_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_net_trace("mock_login_role_list_response mode=actorinfo-plus-rolelist actorinfo_len=%u top=1,1,%u roles=1 maxroles=3 num=1 allpgs=1 len=%u\n",
                 actorInfoLen, actorSubtype, pos);
    return pos;
}

static u32 vm_net_mock_build_login_response(const u8 *request, u32 requestLen, u8 requestSubtype, u8 *out, u32 outCap)
{
    const char *mode = vm_net_mock_env_str("CBE_LOGIN_RESPONSE", "");
    char userName[64];
    bool haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName", userName, sizeof(userName));
    if (!haveUserName)
        haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username", userName, sizeof(userName));

    if (haveUserName && strcmp(userName, "1234") == 0)
    {
        vm_net_trace("mock_login_account_gate account=%s requestSubtype=%u result=2\n",
                     userName, requestSubtype);
        return vm_net_mock_build_login_failure_response(requestSubtype, "password error", out, outCap);
    }

    vm_net_trace("mock_login_account_gate account=%s requestSubtype=%u result=1\n",
                 haveUserName ? userName : "<missing>",
                 requestSubtype);

    if (requestSubtype == 1 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0 ||
         strcmp(mode, "primary-validate") == 0))
    {
        return vm_net_mock_build_login_primary_validation_response(out, outCap);
    }

    if (requestSubtype == 12 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0))
    {
        return vm_net_mock_build_login_alt12_success_response(out, outCap);
    }

    /* Current best static reading is staged rather than same-packet mixing:
     * keep the normal login-success object intact here, then optionally queue
     * a separate title role-list stage-4 response immediately after login.
     * Retain the older mixed object modes for regression comparison.
     */
    if (mode == NULL || mode[0] == 0 ||
        strcmp(mode, "staged-rolelist") == 0 ||
        strcmp(mode, "staged") == 0 ||
        strcmp(mode, "roles") == 0 ||
        strcmp(mode, "rolelist") == 0)
    {
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "roles+actor") == 0 ||
        strcmp(mode, "actor+roles") == 0 ||
        strcmp(mode, "roles-first") == 0)
    {
        return vm_net_mock_build_login_actor_role_list_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "actor") == 0)
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);

    return vm_net_mock_build_login_role_list_only_response(out, outCap);
}

static bool vm_net_mock_login_mode_queues_title_mode15(void)
{
    const char *mode = vm_net_mock_env_str("CBE_LOGIN_RESPONSE", "");
    return mode == NULL || mode[0] == 0 ||
           strcmp(mode, "staged-rolelist") == 0 ||
           strcmp(mode, "staged") == 0 ||
           strcmp(mode, "roles") == 0 ||
           strcmp(mode, "rolelist") == 0;
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
    u8 responseType = request[6];
    u8 responseSub = request[7];
    if (requestType == 1)
        responseSub = 0x1a;
    else if (requestType == 2)
        responseSub = 20;
    else if (requestType == 3)
        responseSub = 32;

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
            vm_net_log_handled_packet("builtin-update-chunk", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_response_from_rules(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("rule", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_request_contains(request, requestLen, "maptype") &&
        vm_net_mock_request_contains(request, requestLen, "mapID") &&
        vm_net_mock_request_contains(request, requestLen, "exitID"))
    {
        hookedLen = vm_net_mock_build_scene_change_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-scene-change len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-scene-change", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4))
    {
        hookedLen = vm_net_mock_build_type27_followup_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-type27-followup len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-type27-followup", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-scene-default-event len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-scene-default-event", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x63, 1))
    {
        hookedLen = vm_net_mock_build_short_wt_control_echo_response(request, requestLen, 0x63, 1, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-short-63-1-echo len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-short-63-1-echo", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_scene_resource_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-scene-resource-followup len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-scene-resource-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_task_subset_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-scene-task-subset-followup len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-scene-task-subset-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_interaction_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-scene-interaction-followup len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-scene-interaction-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_actor_moveinfo_ack_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-actor-moveinfo-ack len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-actor-moveinfo-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_actor_other_only10_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_actor_other_only10_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-actor-other-only10 len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-actor-other-only10", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_login_tail_skill_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_login_tail_skill_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-login-tail-skill len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-login-tail-skill", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 requestKind = 0;
    u8 requestSubtype = 0;
    if (vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &requestKind, &requestSubtype))
    {
        if (requestKind == 1 && requestSubtype == 6)
        {
            hookedLen = vm_net_mock_build_title_role_select_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_trace("mock_default source=builtin-title-role-select len=%u\n", hookedLen);
                vm_net_log_handled_packet("builtin-title-role-select", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (requestKind == 1 && requestSubtype == 16)
        {
            hookedLen = vm_net_mock_build_title_rolelist_stage_response(out, outCap);
            if (hookedLen)
            {
                vm_net_trace("mock_default source=builtin-title-rolelist-stage len=%u\n", hookedLen);
                vm_net_log_handled_packet("builtin-title-rolelist-stage", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (requestKind == 0x0a && requestSubtype == 0x20)
        {
            hookedLen = vm_net_mock_build_role_list_response(out, outCap);
            if (hookedLen)
            {
                vm_net_trace("mock_default source=builtin-role-list len=%u\n", hookedLen);
                vm_net_log_handled_packet("builtin-role-list", request, requestLen, hookedLen);
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
            vm_net_log_handled_packet("file-login", request, requestLen, hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_login_response(request, requestLen, requestSubtype, out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-login len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-login", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 gameRequestType = 0;
    hookedLen = vm_net_mock_build_group_type1_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_trace("mock_default source=builtin-group-type1 len=%u\n", hookedLen);
        vm_net_log_handled_packet("builtin-group-type1", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_get_object_u8_field(request, requestLen, "type", &gameRequestType) &&
        (gameRequestType == 1 || gameRequestType == 2 || gameRequestType == 3))
    {
        hookedLen = vm_net_mock_build_game_type_response(request, requestLen, out, outCap, gameRequestType);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-game-type type=%u len=%u\n", gameRequestType, hookedLen);
            vm_net_log_handled_packet("builtin-game-type", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "version"))
    {
        hookedLen = vm_net_mock_build_version_response(out, outCap);
        if (hookedLen)
        {
            vm_net_trace("mock_default source=builtin-version len=%u\n", hookedLen);
            vm_net_log_handled_packet("builtin-version", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    vm_net_log_unhandled_packet(request, requestLen);
    assert(0);
    return 0;
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

static u32 vm_net_mock_sync_buffer_to_vm(const u8 *buffer, u32 bufferLen)
{
    u32 responsePtr = 0;
    if (buffer == NULL || bufferLen == 0)
        return 0;
    responsePtr = vm_malloc(bufferLen);
    if (responsePtr == 0)
        return 0;
    uc_mem_write(MTK, responsePtr, buffer, bufferLen);
    return responsePtr;
}

static void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
{
    if (dataPtr == 0 || dataLen == 0)
        return;

    g_netLastHandledValid = 0;
    g_netLastHandledResponseLen = 0;
    g_netLastHandledSource[0] = 0;
    g_netLastHandledSummary[0] = 0;

    u8 request[512];
    u32 readLen = dataLen < sizeof(request) ? dataLen : sizeof(request);
    uc_mem_read(MTK, dataPtr, request, readLen);
    vm_net_trace("send connect=%u dataPtr=%08x dataLen=%u readLen=%u\n", connectId, dataPtr, dataLen, readLen);
    bool closeAfterData = vm_net_mock_request_contains(request, readLen, "version") &&
                          !vm_net_mock_request_contains(request, readLen, "start") &&
                          vm_net_mock_has_installed_update();
    /*
     * Empty 25/5 scene-default sends are emitted from inside the client's wait
     * callback. Dispatching the response reentrantly lets the callback clear
     * R9+0x5531 before the send wrapper writes it back to 1, leaving later WT
     * requests blocked. Queue it like a normal async network response instead.
     */
    bool immediateFlushAfterData = false;
    bool queueTitleRoleStage4 = false;
    bool queueTitleRoleStage4AfterMain = false;
    u32 queuedRoleStage4Len = 0;
    u32 queuedRoleStage4Ptr = 0;
    u8 queuedRoleStage4[256];
    u8 requestKind = 0;
    u8 requestSubtype = 0;

    memset(queuedRoleStage4, 0, sizeof(queuedRoleStage4));
    vm_net_mock_get_wt_header_kind_subtype(request, readLen, &requestKind, &requestSubtype);

    g_netMockResponseLen = vm_net_mock_build_response(request, readLen, g_netMockResponse, sizeof(g_netMockResponse));
    g_netMockResponseOffset = 0;
    g_netUpLinkData += dataLen;
    if (g_netMockResponseLen == 0)
        return;

    if (vm_net_mock_login_mode_queues_title_mode15() &&
        requestKind == 1 && requestSubtype == 12 &&
        vm_net_mock_request_contains(request, readLen, "coreVer") &&
        vm_net_mock_request_contains(request, readLen, "appVer") &&
        (vm_net_mock_request_contains(request, readLen, "username") ||
         vm_net_mock_request_contains(request, readLen, "userName")) &&
        vm_net_mock_request_contains(request, readLen, "password") &&
        vm_net_mock_request_contains(request, readLen, "imsi"))
    {
        queuedRoleStage4Len = vm_net_mock_build_title_mode15_response(queuedRoleStage4, sizeof(queuedRoleStage4));
        if (queuedRoleStage4Len != 0)
        {
            queuedRoleStage4Ptr = vm_net_mock_sync_buffer_to_vm(queuedRoleStage4, queuedRoleStage4Len);
            if (queuedRoleStage4Ptr != 0)
            {
                queueTitleRoleStage4 = true;
                queueTitleRoleStage4AfterMain = (requestKind == 1 && requestSubtype == 12);
            }
        }
    }

    u32 responsePtr = vm_net_mock_sync_response_to_vm();
    if (responsePtr == 0)
        return;
    g_netDownLinkData += g_netMockResponseLen;
    vm_net_trace("mock_response ptr=%08x len=%u\n", responsePtr, g_netMockResponseLen);
    vm_net_packet_log_exchange(request, readLen, g_netMockResponse, g_netMockResponseLen);

    vm_net_channel *channel = scheduler_find_net_channel(connectId);
    if (channel && channel->callback)
    {
        if (queueTitleRoleStage4 && !queueTitleRoleStage4AfterMain)
        {
            vm_net_trace("queue_login_followup_event connect=%u cb=%08x ctx=%08x event=7 len=%u label=title-mode15\n",
                         connectId, channel->callback, channel->context, queuedRoleStage4Len);
            scheduler_queue_net_event(7,
                                      queuedRoleStage4Ptr,
                                      queuedRoleStage4Len,
                                      queuedRoleStage4Len,
                                      channel->callback,
                                      channel->context);
        }
        vm_net_trace("queue_data_event connect=%u cb=%08x ctx=%08x event=7 len=%u\n", connectId, channel->callback, channel->context, g_netMockResponseLen);
        scheduler_queue_net_event(7, responsePtr, g_netMockResponseLen, g_netMockResponseLen, channel->callback, channel->context);
        if (queueTitleRoleStage4 && queueTitleRoleStage4AfterMain)
        {
            vm_net_trace("queue_login_followup_event connect=%u cb=%08x ctx=%08x event=7 len=%u label=title-mode15-after-main\n",
                         connectId, channel->callback, channel->context, queuedRoleStage4Len);
            scheduler_queue_net_event(7,
                                      queuedRoleStage4Ptr,
                                      queuedRoleStage4Len,
                                      queuedRoleStage4Len,
                                      channel->callback,
                                      channel->context);
        }
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

static int vm_lcd_font_width_for_mode(u32 mode)
{
    return mode ? getFontWidth() : getFontCellWidth();
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

    vm_net_trace("mouse_event type=%d x=%d y=%d\n", type, x, y);
    EnqueueVMEvent(VM_EVENT_TOUCHSCREEN, type, (y << 16) | x);
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
    vm_stdout_trace("r0:%x r1:%x r2:%x r3:%x r4:%x r5:%x r6:%x r7:%x r8:%x r9:%x\n", r0, r1, r2, r3, r4, r5, r6, r7, r8, r9);
    vm_stdout_trace("msp:%x cpsr:%x(thumb:%x)(mode:%x) lr:%x pc:%x lastPc:%x irq_c(%x)\n", msp, cpsr, (cpsr & 0x20) > 0, cpsr & 0x1f, lr, pc, lastAddress, irq_nested_count);
    vm_stdout_trace("------------\n");
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
    vm_trace_ensure_log_dir();

    FILE *fp = fopen("logs/storage_trace.log", "ab");
    if (fp == NULL)
        return;
    vm_trace_write_storage_session_marker(fp);

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
                vm_net_trace("screen_func_table screen=%08x this=%08x init=%08x destroy=%08x logic=%08x render=%08x pause=%08x resume=%08x resLoad=%08x tick=%u last=%08x\n",
                             screenFuncPtr,
                             screenThisPtr,
                             screenInitEntry,
                             screenDestoryEntry,
                             screenLogicEntry,
                             screenRenderEntry,
                             screenPauseEntry,
                             screenRemuseEntry,
                             screenResouceLoadEntry,
                             g_schedulerTick,
                             lastAddress);
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
                vm_stdout_trace("ScreenResume Ok\n");
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
                vm_stdout_trace("ScreenInit Ok\n");
            }
            if (p == UC_ERR_OK)
            {
                while (true)
                {
                    p = scheduler_tick();
                    if (p != UC_ERR_OK)
                        break;
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
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_stdout_trace("manager_stub api=VmGetMixMenuLength idx=91 r0=%08x r1=%08x r2=%08x r3=%08x last=%08x tick=%u result=0\n",
                        tmp1, tmp2, tmp3, tmp4, lastAddress, g_schedulerTick);
        vm_set_call_result(0);
    }
    else if (idx == 92)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_stdout_trace("manager_stub api=VmGetMixMenudata idx=92 r0=%08x r1=%08x r2=%08x r3=%08x last=%08x tick=%u result=0\n",
                        tmp1, tmp2, tmp3, tmp4, lastAddress, g_schedulerTick);
        vm_try_write_zero(tmp1, 32);
        vm_try_write_zero(tmp2, 32);
        vm_set_call_result(0);
    }
    else if (idx == 93)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_stdout_trace("manager_stub api=VmGetWpayCBMInfo idx=93 r0=%08x r1=%08x r2=%08x r3=%08x last=%08x tick=%u result=0\n",
                        tmp1, tmp2, tmp3, tmp4, lastAddress, g_schedulerTick);
        vm_try_write_zero(tmp1, 32);
        vm_try_write_zero(tmp2, 32);
        vm_set_call_result(0);
    }
    else if (idx == 94)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        vm_stdout_trace("manager_stub api=VMGetCurVerAllInfo idx=94 r0=%08x r1=%08x r2=%08x r3=%08x last=%08x tick=%u result=0\n",
                        tmp1, tmp2, tmp3, tmp4, lastAddress, g_schedulerTick);
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
        // vm_trace_lcd_text("vMDrawString", idx, tmp1, x, y, (u16)tmp4, cbeTextString);
        // vm_trace_lcd_text_call("vMDrawString", idx, tmp1, tmp1, tmp2, tmp3, tmp4, tmp5, x, y, (u16)tmp4, cbeTextString);
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
        // DEBUG_PRINT("[call]vMDrawStringEx(%d,%d,%s)\n", tmp2, tmp3, sprintfBuff);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
        int x = vm_lcd_coord_from_reg(tmp2);
        int y = vm_lcd_coord_from_reg(tmp3);
        // vm_trace_lcd_text("vMDrawStringEx", idx, tmp1, x, y, color, cbeTextString);
        // vm_trace_lcd_text_call("vMDrawStringEx", idx, tmp1, tmp1, tmp2, tmp3, color, tmp4, x, y, color, cbeTextString);

        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 12)
    {
        u32 r0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
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
        vm_trace_lcd_text_call("vMShowStringClipAlign", idx, tmp1, r0, tmp1, tmp2, tmp3, tmp4, x, y, color, cbeTextString);
        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        vm_set_call_result(1);
    }
    else if (idx == 13)
    {
        u32 r0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
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
        vm_trace_lcd_text_call("vMShowStringClip", idx, tmp1, r0, tmp1, tmp2, tmp3, tmp4, x, y, color, cbeTextString);
        vm_lcd_draw_current_string(cbeTextString, x, y, color);
        vm_lcd_sync_string_to_vm(cbeTextString, x, y);
        vm_set_call_result(1);
    }
    else if (idx == 14)
    {
        u32 r0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
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
        vm_trace_lcd_text_call("vMShowStringRect", idx, tmp1, r0, tmp1, tmp2, tmp3, tmp4, x, y, color, cbeTextString);
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
            h = vm_lcd_coord_from_reg(rectH);
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
        int h = vm_lcd_coord_from_reg(rectH);
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
            h = vm_lcd_coord_from_reg(fillH);
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
        int h = vm_lcd_coord_from_reg(fillH);
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
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        uc_mem_read(MTK, sp + 0x0, &tmp5, 4);
        uc_mem_read(MTK, sp + 0x4, &sp, 4);
        vm_set_call_result(vm_cd_rect_point(tmp1, tmp2, tmp3, tmp4, tmp5, sp));
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
    else if (idx == 2)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        if (tmp1 != 0 && tmp1 != vmAddedScreen)
        {
            vm_net_trace("screen_manager idx=2 change r0=%08x r1=%08x r2=%08x r3=%08x depth=%u active=%08x last=%08x\n",
                         tmp1, tmp2, tmp3, tmp4, g_screenStackCount, vmAddedScreen, lastAddress);
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
            tmp2 = 0;
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
        }
        else
        {
            vm_net_trace("screen_manager idx=2 same_current r0=%08x r1=%08x r2=%08x r3=%08x depth=%u active=%08x last=%08x\n",
                         tmp1, tmp2, tmp3, tmp4, g_screenStackCount, vmAddedScreen, lastAddress);
        }
        vm_set_call_result(VM_SCREEN_isInQuit_ADDRESS);
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vmAddedScreen = tmp1;
        u32 moduleBase = 0;
        if (lastAddress >= VM_Memory_Pool_ADDRESS && lastAddress < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
        {
            uc_reg_read(MTK, UC_ARM_REG_R9, &moduleBase);
        }
        vm_screen_stack_push(tmp1, moduleBase);
        vm_net_trace("screen_manager idx=4 add r0=%08x depth=%u moduleBase=%08x last=%08x\n", tmp1, g_screenStackCount, moduleBase, lastAddress);
        if (moduleBase)
        {
            g_currentScreenThis = tmp1 - 0x18;
            g_currentScreenModuleBase = moduleBase;
            vm_net_trace("screen_manager idx=4 module_context this=%08x r9=%08x last=%08x\n",
                         g_currentScreenThis, g_currentScreenModuleBase, lastAddress);
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
        tmp5 = 0;
        tmp4 = vm_screen_stack_remove(tmp1, &tmp3, &tmp5) ? 1 : 0;
        vm_net_trace("screen_manager idx=6 remove r0=%08x r1=%08x ret=%u newTop=%08x newTopModule=%08x depth=%u last=%08x\n",
                     tmp1, tmp2, tmp4, tmp3, tmp5, g_screenStackCount, lastAddress);
        if (tmp4 && tmp1 == vmAddedScreen && tmp3)
        {
            g_activeScreenRemovedThisFrame = 1;
            g_screenResumeExisting = 1;
            vmAddedScreen = tmp3;
            g_currentScreenThis = tmp3 - 0x18;
            g_currentScreenModuleBase = tmp5;
            u32 isInQuit = 0;
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp3, 4);
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &isInQuit, 4);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
            vm_net_trace("screen_manager idx=6 promote_current_remove screen=%08x moduleBase=%08x depth=%u resume=%u last=%08x\n",
                         tmp3, g_currentScreenModuleBase, g_screenStackCount, g_screenResumeExisting, lastAddress);
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
        u32 sp = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        uc_mem_read(MTK, sp + 0x0, &tmp5, 4);
        uc_mem_read(MTK, sp + 0x4, &sp, 4);
        vm_set_call_result(vm_cd_rect_point(tmp1, tmp2, tmp3, tmp4, tmp5, sp));
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

    u32 tmp1, tmp2, lr;
    u32 idx = (address - VM_DL_PAY_FUNC_LIST_ADDRESS) / 4;

    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    vm_net_trace("dl_wpay_call idx=%u r0=%08x r1=%08x lr=%08x last=%08x tick=%u result=%08x\n",
                 idx, tmp1, tmp2, lr, lastAddress, g_schedulerTick, 0u);
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

    // vm_net_trace_loading_flag_change_if_needed((u32)address);
    // vm_net_trace_scene_tick_gate_change_if_needed((u32)address);
    // vm_net_trace_status_bar_state_if_needed((u32)address);

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
    if (address == VM_LOG_TRACE_ADDRESS)
    {
        // vm_log_trace only emits firmware-side diagnostics; keep it silent during normal emulation.
        // vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
        printf("[vm_log_trace]");
        vm_sprintf();
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
    }
    if (address == 0x1003568)
    {
        u32 lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (vm_is_scene_bootstrap_loading_overlay_caller(lr))
            vm_trace_scene_bootstrap_loading_overlay_candidate((u32)address, lr);
    }
    if (address == 0x100F9B4)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace("trace_scene_send_map_enter_request maptype=%u mapArg=%08x extra=%08x lr=%08x last=%08x tick=%u\n",
                     r0, r1, r2, lr, lastAddress, g_schedulerTick);
    }
    if (address == 0x100D2BE)
    {
        u32 r0 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_resource_name_flow("trace_resource_open_helper", (u32)address, lr, r0);
    }
    if (address == 0x100D6E2)
    {
        u32 lr = 0, r0 = 0, sp = 0;
        u32 arg4 = 0, arg8 = 0, argC = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        uc_mem_read(MTK, sp, &arg4, 4);
        uc_mem_read(MTK, sp + 4, &arg8, 4);
        uc_mem_read(MTK, sp + 8, &argC, 4);
        vm_net_trace_parse_actor_motion_entry((u32)address, lr, r0, arg4, arg8, argC);
    }
    if (address == 0x100FD2E)
        vm_net_trace_scene_actorinfo_snapshot((u32)address);
    if (address == 0x100D6E2 || address == 0x100DB2C)
        vm_trace_actor_motion_descriptor_context((u32)address);
    if (address == 0x100D702)
        vm_trace_actor_motion_open_result((u32)address);
    if (address == 0x100DB4A || address == 0x100DB74)
        vm_trace_actor_motion_enqueue_fallback((u32)address);
    if (address == 0x100DB2C)
        vm_trace_actor_motion_callback_handoff((u32)address);
    if (address == 0x100F094)
        vm_trace_actor_move_entry_parser_entry((u32)address);
    if (address == 0x100F6A0)
        vm_trace_scene_status_text_write("promptName_0x12_R9_5CD8", (u32)address);
    if (address == 0x100F68A)
        vm_trace_scene_status_text_write("statusText_0x16_R9_5CDC", (u32)address);
    if (address == 0x100F7DC)
        vm_net_trace_scene_current_node_publish((u32)address);
    if (address == 0x100F8F6 || address == 0x10368C4)
    {
        u32 lr = 0, sp = 0, r0 = 0, r1 = 0, r2 = 0;
        u32 stack0 = 0, stack4 = 0, stack8 = 0;
        const char *tag = (address == 0x100F8F6) ? "trace_sub_10352ae_call_from_scene_rebuild" : "trace_sub_10352ae_call_from_1036768";
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_mem_read(MTK, sp, &stack0, 4);
        uc_mem_read(MTK, sp + 4, &stack4, 4);
        uc_mem_read(MTK, sp + 8, &stack8, 4);
        vm_net_trace_sub_10352ae_callsite(tag, (u32)address, lr, r0, r1, r2, stack0, stack4, stack8);
    }
    if (address == 0x1036768)
        vm_trace_manager_replay_entry((u32)address);
    if (address == 0x100D48A)
    {
        u32 r0 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_resource_name_flow("trace_load_resource_stream_entry", (u32)address, lr, r0);
    }
    if (address == 0x100D534)
    {
        u32 r0 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_resource_name_flow("trace_resource_stream_wrapper_entry", (u32)address, lr, r0);
    }
    if (address == 0x100D570 || address == 0x100D6FC || address == 0x100DB9C)
    {
        u32 r0 = 0, lr = 0;
        const char *label = "trace_resource_stream_callsite";
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (address == 0x100D570)
            label = "trace_resource_stream_call_from_d564";
        else if (address == 0x100D6FC)
            label = "trace_resource_stream_call_from_actor_motion";
        else if (address == 0x100DB9C)
            label = "trace_resource_stream_call_from_db82";
        vm_net_trace_resource_name_flow(label, (u32)address, lr, r0);
    }
    if (address == 0x1043206)
    {
        u32 r1 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_resource_name_flow("trace_piclib_resource_dispatch", (u32)address, lr, r1);
    }
    if (address == 0x10433BA || address == 0x10433DE || address == 0x1043420 || address == 0x1043434)
    {
        const char *label = "piclib_slot_unknown";
        if (address == 0x10433BA)
            label = "piclib_slot_entry";
        else if (address == 0x10433DE)
            label = "piclib_slot_before_open";
        else if (address == 0x1043420)
            label = "piclib_slot_load_success";
        else if (address == 0x1043434)
            label = "piclib_slot_request_remote";
        vm_net_trace_piclib_slot_load(label, (u32)address);
    }
    if (address == 0x1044DA0)
    {
        u32 r2 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_resource_name_flow("trace_resource_pending_lookup", (u32)address, lr, r2);
    }
    if (address == 0x1044EF6)
    {
        u32 r2 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_resource_name_flow("trace_resource_pending_mark", (u32)address, lr, r2);
    }
    if (address == 0x100ED20)
    {
        u32 r0 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace("trace_scene_send_type27_followup_request type=%u lr=%08x last=%08x tick=%u\n",
                     r0 & 0xFFu, lr, lastAddress, g_schedulerTick);
    }
    if (address == 0x100E2E4 || address == 0x100E302)
    {
        const char *label = address == 0x100E2E4 ? "entry" : "after_fields";
        vm_trace_alloc_outgoing_game_event(label, (u32)address);
    }
    if (address == 0x1010594)
    {
        u32 lr = 0;
        u32 objectCount = 0;
        u32 objectBase = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (Global_R9)
        {
            uc_mem_read(MTK, Global_R9 + 21904, &objectCount, 4);
            uc_mem_read(MTK, Global_R9 + 21912, &objectBase, 4);
        }
        vm_net_trace("trace_followup_scan_enter objects=%u base=%08x lr=%08x last=%08x tick=%u\n",
                     objectCount, objectBase, lr, lastAddress, g_schedulerTick);
    }
    if (address == 0x1013594)
    {
        vm_net_trace_sub_1010228_callsite("scene_runtime_init_and_sync", (u32)address);
    }
    if (address == 0x100AFCA || address == 0x1002538 || address == 0x10025CE || address == 0x100D04A)
    {
        const char *label = "enter_gate_unknown";
        if (address == 0x100AFCA)
            label = "sub_100AFCA";
        else if (address == 0x1002538)
            label = "sub_1002538";
        else if (address == 0x10025CE)
            label = "sub_10025CE";
        else if (address == 0x100D04A)
            label = "sub_100D04A";
        vm_net_trace_enter_mode_gate(label, (u32)address);
    }
    if (address == 0x100AFE4 || address == 0x100AFF2 || address == 0x100AFE6 || address == 0x100AFF4)
    {
        const char *label = "sub_100AFCA_gate";
        if (address == 0x100AFE4)
            label = "sub_100AFCA_before_call_1002_1";
        else if (address == 0x100AFF2)
            label = "sub_100AFCA_before_call_1002_3";
        else if (address == 0x100AFE6)
            label = "sub_100AFCA_after_call_1002_1";
        else if (address == 0x100AFF4)
            label = "sub_100AFCA_after_call_1002_3";
        vm_net_trace_enter_mode_gate_result(label, (u32)address);
    }
    if (address == 0x100D052 || address == 0x100D064)
    {
        const char *label = address == 0x100D052 ? "sub_100D04A_after_sub_100B95C" : "sub_100D04A_after_sub_1000648";
        vm_net_trace_wpay_update_gate(label, (u32)address);
    }
    if (address == 0x1003856 || address == 0x1034922)
    {
        const char *label = address == 0x1003856 ? "scene_system_bootstrap" : "shared_event_owner_init";
        vm_net_trace_scene_owner_site(label, (u32)address);
    }
    if (address == 0x1048FCA || address == 0x1048FB4)
    {
        const char *label = address == 0x1048FCA ? "manager_call_cb10" : "manager_call_cb8";
        vm_net_trace_title_child_manager_call(label, (u32)address);
    }
    if (address == 0x05019950 || address == 0x05018F90 || address == 0x05018FA4 || address == 0x05018FB8 || address == 0x05018FC2)
    {
        const char *label = "title_login_dispatch_unknown";
        if (address == 0x05019950)
            label = "login_primary_dispatch_entry";
        else if (address == 0x05018F90)
            label = "login_result_dispatch_entry";
        else if (address == 0x05018FA4)
            label = "login_result_dispatch_success";
        else if (address == 0x05018FB8)
            label = "login_result_dispatch_fail2";
        else if (address == 0x05018FC2)
            label = "login_result_dispatch_fail6";
        vm_net_trace_title_login_dispatch(label, (u32)address);
    }
    if (address == 0x0501A114 || address == 0x0501A6A2 || address == 0x0501AE60 || address == 0x0501AE88 || address == 0x0501B2C4 || address == 0x0501BFBC)
    {
        const char *label = "title_role_path_unknown";
        if (address == 0x0501A114)
            label = "target4c_handle_network";
        else if (address == 0x0501A6A2)
            label = "target50_init";
        else if (address == 0x0501AE60)
            label = "target50_dispatch_mode_input";
        else if (address == 0x0501AE88)
            label = "target50_handle_input";
        else if (address == 0x0501B2C4)
            label = "target50_render";
        else if (address == 0x0501BFBC)
            label = "target50_handle_network";
        vm_net_trace_title_role_path(label, (u32)address);
    }
    if (address == 0x0501A20E || address == 0x0501A22A)
    {
        const char *label = (address == 0x0501A20E) ? "rolelist_reader_methods_ready" : "rolelist_reader_first_record";
        vm_net_trace_title_rolelist_reader_methods(label, (u32)address);
    }
    if (address == 0x0501A574 || address == 0x0501A5CC || address == 0x0501AA36)
    {
        const char *label = "role_select_unknown";
        if (address == 0x0501A574)
            label = "role_manage_enter_action_menu";
        else if (address == 0x0501A5CC)
            label = "role_manage_select_role_slot";
        else if (address == 0x0501AA36)
            label = "role_manage_submit_selected_role";
        vm_net_trace_title_role_select_action(label, (u32)address);
    }
    if (address == 0x10124AA)
    {
        vm_net_trace_sub_1010228_callsite("net_handle_group_info", (u32)address);
    }
    if (address == 0x1012E4C)
        vm_net_trace_business_dispatch_state("entry", (u32)address);
    if (address == 0x1012E88)
        vm_net_trace_business_dispatch_state("after_unpack_ok", (u32)address);
    if (address == 0x1012E9E)
        vm_net_trace_business_dispatch_item((u32)address);
    if (address == 0x104789E)
        vm_net_trace_tasktypes_case13_stream("after_get_tasktypes_before_stream_init", (u32)address);
    if (address == 0x10478CE)
        vm_net_trace_tasktypes_case13_stream("before_peek_name_len", (u32)address);
    if (address == 0x10478E2)
        vm_net_trace_tasktypes_case13_stream("after_read_name_ptr", (u32)address);
    if (address == 0x10478EE)
        vm_net_trace_tasktypes_case13_stream("before_memcpy_name", (u32)address);
    if (address == 0x1012F42)
        vm_net_trace_business_dispatch_state("early_gate_off", (u32)address);
    if (address == 0x1012F82)
        vm_net_trace_business_dispatch_state("unpack_error", (u32)address);
    if (address == 0x1012F8E)
        vm_net_trace_business_dispatch_state("fallback_exit", (u32)address);
    if (address == 0x1010228 || address == 0x101022A)
    {
        u32 lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_sub_1010228_entry((u32)address, lr);
    }
    if (address == 0x1003568 || address == 0x100337C || address == 0x1003CFC)
    {
        const char *label = "unknown";
        if (address == 0x1003568)
            label = "sub_1003568";
        else if (address == 0x100337C)
            label = "sub_100337C";
        else if (address == 0x1003CFC)
            label = "sub_1003CFC";
        vm_net_trace_loading_overlay_call(label, (u32)address);
    }
    if (address == 0x23D4 || address == 0x248A || address == 0x1984 || address == 0x1994)
    {
        const char *label = "unknown";
        if (address == 0x23D4)
            label = "login_main_success_target4c";
        else if (address == 0x248A)
            label = "login_rolelist_success_target50";
        else if (address == 0x1984)
            label = "login_stage_success_target48";
        else if (address == 0x1994)
            label = "login_stage_success_target4c";
        vm_net_trace_title_screen_callback(label, (u32)address);
    }
    if (address == 0x100E8B0)
    {
        vm_net_trace_draw_map_tile_entry((u32)address);
    }
    if (address == 0x1014D3A || address == 0x1014E54 || address == 0x1014EE6 ||
        address == 0x1015158 || address == 0x101517A || address == 0x101517E)
    {
        const char *label = "unknown";
        if (address == 0x1014D3A)
            label = "entry";
        else if (address == 0x1014E54)
            label = "oneshot_sync_check";
        else if (address == 0x1014EE6)
            label = "oneshot_sync_clear";
        else if (address == 0x1015158)
            label = "draw_pass";
        else if (address == 0x101517A)
            label = "status_panels";
        else if (address == 0x101517E)
            label = "actor_pass";
        vm_net_trace_scene_runtime_tick(label, (u32)address);
        if (address == 0x101517E)
            vm_fixup_current_node_visual_res_if_ready((u32)address, label);
    }
    if (address == 0x10144D0 || address == 0x1014510)
    {
        const char *label = address == 0x10144D0 ? "type2_body" : "normal_body";
        vm_trace_scene_body_draw_dispatch(label, (u32)address);
    }
    if (address == 0x1014594 || address == 0x10145AC)
    {
        const char *label = address == 0x1014594 ? "current_node_drawAt" : "current_node_step";
        vm_trace_scene_current_node_draw_callbacks(label, (u32)address);
    }
    if (address == 0x1004150 || address == 0x1004226)
    {
        const char *label = address == 0x1004150 ? "sub_1004150" : "sub_1004226";
        vm_trace_progress_strip_wrapper_entry(label, (u32)address);
    }
    if (address == 0x10461A8)
    {
        vm_trace_loading_gif_widget_draw_entry((u32)address);
    }
    if (address == 0x1013C28 || address == 0x1013C52 || address == 0x1013C84 || address == 0x10110E6 || address == 0x1011112 || address == 0x1011120 || address == 0x101037E)
    {
        const char *label = "unknown";
        if (address == 0x1013C28)
            label = "scene_wait_callback_message";
        else if (address == 0x1013C52)
            label = "scene_state3_message";
        else if (address == 0x1013C84)
            label = "scene_widget_timeout_message";
        else if (address == 0x10110E6)
            label = "message_request_entry";
        else if (address == 0x1011112)
            label = "message_queue_branch";
        else if (address == 0x1011120)
            label = "message_box_branch";
        else if (address == 0x101037E)
            label = "queued_message_insert";
        vm_trace_scene_message_request(label, (u32)address);
    }
    if (address == 0x1013BDC || address == 0x1013C00 || address == 0x1013C02 || address == 0x1013C04)
    {
        const char *label = "unknown";
        if (address == 0x1013BDC)
            label = "loading_callback_entry";
        else if (address == 0x1013C00)
            label = "loading_callback_load_ptr";
        else if (address == 0x1013C02)
            label = "loading_callback_before_blx";
        else if (address == 0x1013C04)
            label = "loading_callback_after_blx";
        vm_trace_scene_loading_callback_gate(label, (u32)address);
    }
    if (address == 0x100F78E)
    {
        vm_trace_actor_move_entry_table((u32)address);
    }
    if (address == 0x100F468)
    {
        vm_trace_actor_move_entry_append((u32)address);
    }
    if (address == 0x101477E || address == 0x10147AE)
    {
        vm_net_trace_status_bar_divide_site((u32)address);
    }
    if (address == 0x100FF26 || address == 0x10100D2 || address == 0x10101E8)
    {
        vm_scene_seed_status_meter_displaymax_fallback((u32)address);
        vm_net_trace_status_meter_rebuild_site((u32)address);
    }
    if (address == 0x1039180)
    {
        u32 lr = 0, r0 = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        vm_net_trace_scene_colnames_entry((u32)address, lr, r0);
    }
    if (address == 0x10391E8)
    {
        u32 lr = 0, r1 = 0, r2 = 0, r4 = 0;
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
        vm_net_trace_scene_colnames_item((u32)address, lr, r4, r2, r1);
    }
    if (address == 0x10391F8 || address == 0x1039372 || address == 0x1039526)
    {
        u32 lr = 0;
        const char *label = "scene_colnames_return";
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (address == 0x1039372)
            label = "scene_roomlist_return";
        else if (address == 0x1039526)
            label = "scene_roles_return";
        vm_net_trace_scene_list_return(label, (u32)address, lr);
    }
    if (address == 0x10105C6 || address == 0x10106D8 || address == 0x1010706)
    {
        u32 entry = 0;
        u32 kind = 0;
        u32 subtype = 0;
        u32 lr = 0;
        const char *label = "unknown";
        uc_reg_read(MTK, UC_ARM_REG_R4, &entry);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (entry)
        {
            uc_mem_read(MTK, entry + 4, &kind, 4);
            uc_mem_read(MTK, entry + 8, &subtype, 4);
        }
        if (address == 0x10105C6)
            label = "case12_1";
        else if (address == 0x10106D8)
            label = "case27_11";
        else if (address == 0x1010706)
            label = "case27_4";
        vm_net_trace("trace_followup_case label=%s pc=%08x entry=%08x kind=%u subtype=%u lr=%08x last=%08x tick=%u\n",
                     label, (u32)address, entry, kind, subtype, lr, lastAddress, g_schedulerTick);
    }
    if (address == 0x1011C88 || address == 0x10114FC || address == 0x1010950 || address == 0x1039B8A || address == 0x1010BC0 || address == 0x1039222 || address == 0x1039430)
    {
        u32 r0 = 0, lr = 0, subtype = 0, kind = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (r0)
        {
            uc_mem_read(MTK, r0 + 4, &kind, 4);
            uc_mem_read(MTK, r0 + 8, &subtype, 4);
        }
        vm_net_trace("trace_business_handler pc=%08x packet=%08x kind=%u subtype=%u lr=%08x last=%08x tick=%u\n",
                     (u32)address, r0, kind, subtype, lr, lastAddress, g_schedulerTick);
    }
    if (address == 0x1036C66)
    {
        u32 r0 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (r0 == 1 || r0 == 2 || r0 == 4)
            vm_net_trace_update_request_prepare((u32)address, lr, r0);
    }
    if (address == 0x10365F0)
    {
        u32 r0 = 0, r1 = 0, r2 = 0, r3 = 0, lr = 0, sp = 0;
        u32 stack0 = 0, stack4 = 0, stack8 = 0, stack12 = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp)
        {
            (void)uc_mem_read(MTK, sp, &stack0, 4);
            (void)uc_mem_read(MTK, sp + 4, &stack4, 4);
            (void)uc_mem_read(MTK, sp + 8, &stack8, 4);
            (void)uc_mem_read(MTK, sp + 12, &stack12, 4);
        }
        vm_net_trace_resource_request_enqueue("trace_resource_request_enqueue",
                                              (u32)address,
                                              lr,
                                              r0,
                                              r2,
                                              r3,
                                              stack0,
                                              stack4,
                                              stack8,
                                              stack12);
    }
    if (address == 0x10366AC)
    {
        u32 r0 = 0, r1 = 0, lr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        vm_net_trace_resource_request_enqueue("trace_resource_request_mark",
                                              (u32)address,
                                              lr,
                                              r0,
                                              r1,
                                              0,
                                              0,
                                              0,
                                              0,
                                              3);
    }
    if (address == 0x101A97C || address == 0x101AED2 || address == 0x101D2D6 || address == 0x101D4B4 || address == 0x101EC62)
    {
        u32 r0 = 0, lr = 0;
        const char *label = "loading_unknown";
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (address == 0x101A97C)
            label = "loading_flag_entry_101A97C";
        else if (address == 0x101AED2)
            label = "loading_flag_entry_101AED2";
        else if (address == 0x101D2D6)
            label = "loading_flag_entry_101D2D6";
        else if (address == 0x101D4B4)
            label = "loading_flag_entry_101D4B4";
        else if (address == 0x101EC62)
            label = "loading_flag_entry_101EC62";
        vm_net_trace_loading_flag_flow(label, (u32)address, lr, r0);
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
