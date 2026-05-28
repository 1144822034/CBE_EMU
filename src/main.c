#define GDB_SERVER_SUPPORT_
#define GDI_LAYER_DEBUG_

#define DEBUG_PRINT(...) ((void)0)

#include "main.h"
#include "lcd.h"
static u32 vm_load_resource_blob_from_cbe(const char *name);
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
u32 screenStructChange = 0;
u32 screenStructNotifyLoadRes = 0;
u32 vmAddedScreen = 0;

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
    u16 delayTicks;
    u32 r0;
    u32 r1;
    u32 callback;
    u32 context;
} vm_net_task;

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
static vm_timer_task g_timerTasks[VM_SCHED_MAX_TIMERS];
static u32 g_schedulerStartTicks = 0;
static u32 g_nextNetConnectId = 1;

static u32 vm_read_host_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 vm_load_resource_blob_from_cbe(const char *name)
{
    if (g_cbeFileBuffer == NULL || g_cbeInfo.DF_Data_Pacakage_Offset + 4 >= g_cbeFileSize)
        return 0;

    u32 dfBase = g_cbeInfo.DF_Data_Pacakage_Offset;
    u8 *rootBlock = g_cbeFileBuffer + dfBase;
    u32 rootSize = vm_read_host_le32(rootBlock);
    if (rootSize < 8 || dfBase + 4 + rootSize > g_cbeFileSize)
        return 0;

    u8 *root = rootBlock + 4;
    u32 childCount = vm_read_host_le32(root + 4);
    u32 pos = 8;
    for (u32 i = 1; i < childCount && pos + 5 <= rootSize; ++i)
    {
        u8 childNameLen = root[pos++];
        if (pos + childNameLen + 4 > rootSize)
            return 0;
        pos += childNameLen;
        u32 childOffset = vm_read_host_le32(root + pos);
        pos += 4;

        u32 childAbs = dfBase + childOffset;
        if (childAbs + 4 >= g_cbeFileSize)
            continue;
        u8 *childBlock = g_cbeFileBuffer + childAbs;
        u32 childSize = vm_read_host_le32(childBlock);
        if (childSize < 8 || childAbs + 4 + childSize > g_cbeFileSize)
            continue;

        u8 *child = childBlock + 4;
        u32 dataSize = vm_read_host_le32(child);
        u32 itemCount = vm_read_host_le32(child + 4);
        if (8 + itemCount * 4 > childSize)
            continue;
        u32 namesPos = 8 + itemCount * 4;
        u32 dataBase = childAbs + 4 + childSize;

        for (u32 n = 0; n < itemCount && namesPos < childSize; ++n)
        {
            u8 len = child[namesPos++];
            if (namesPos + len > childSize)
                break;
            const char *resName = (const char *)(child + namesPos);
            namesPos += len;
            if (strlen(name) == len && memcmp(resName, name, len) == 0)
            {
                u32 dataOffset = vm_read_host_le32(child + 8 + n * 4);
                u32 nextOffset = (n + 1 < itemCount) ? vm_read_host_le32(child + 8 + (n + 1) * 4) : dataSize;
                if (nextOffset <= dataOffset || dataBase + nextOffset > g_cbeFileSize)
                    return 0;
                u32 blobSize = nextOffset - dataOffset;
                u32 vmPtr = vm_malloc(blobSize);
                uc_mem_write(MTK, vmPtr, g_cbeFileBuffer + dataBase + dataOffset, blobSize);
                return vmPtr;
            }
        }
    }
    return 0;
}

static const char *vm_fake_resource_name_from_id(u32 id)
{
    switch (id)
    {
    case 0x70000000:
        return "flowerStyle.gif";
    case 0x70000001:
        return "loading.gif";
    case 0x70000002:
        return "UI2.gif";
    case 0x70000003:
        return "UI7.gif";
    case 0x70000004:
        return "UI8.gif";
    default:
        return NULL;
    }
}

static u32 vm_fake_resource_id_from_name(const char *name)
{
    if (strcmp(name, "flowerStyle.gif") == 0)
        return 0x70000000;
    if (strcmp(name, "loading.gif") == 0)
        return 0x70000001;
    if (strcmp(name, "UI2.gif") == 0)
        return 0x70000002;
    if (strcmp(name, "UI7.gif") == 0)
        return 0x70000003;
    if (strcmp(name, "UI8.gif") == 0)
        return 0x70000004;
    return 0;
}

static u32 vm_df_get_resource_by_id(u32 id)
{
    const char *name = vm_fake_resource_name_from_id(id);
    if (name)
        return vm_set_call_result(vm_load_resource_blob_from_cbe(name));
    return vm_DF_GetResourceByResourceID(id);
}

static u32 vm_df_get_resource_by_file_name(u32 namePtr)
{
    vm_readStringByPtr(namePtr, cbeTextString);
    if (vm_fake_resource_id_from_name(cbeTextString))
        return vm_set_call_result(vm_load_resource_blob_from_cbe(cbeTextString));
    return vm_DF_GetResourceByFileName(namePtr);
}

static u32 vm_df_get_resource_name_by_id(u32 id)
{
    const char *name = vm_fake_resource_name_from_id(id);
    if (name)
    {
        uc_mem_write(MTK, VM_DreamFactory_CharBuffer_ADDRESS, name, strlen(name) + 1);
        return vm_set_call_result(VM_DreamFactory_CharBuffer_ADDRESS);
    }
    vm_DF_GetResourceNameByID(id);
    return 0;
}

static u32 vm_df_get_resource_id_by_file_name(u32 namePtr)
{
    vm_readStringByPtr(namePtr, cbeTextString);
    u32 fakeId = vm_fake_resource_id_from_name(cbeTextString);
    if (fakeId)
        return vm_set_call_result(fakeId);
    return vm_DF_GetResourceIDByFileName(namePtr);
}

static u32 vm_df_get_t_resource(u32 namePtr, int stream)
{
    vm_readStringByPtr(namePtr, cbeTextString);
    if (vm_fake_resource_id_from_name(cbeTextString))
    {
        u32 resource = vm_load_resource_blob_from_cbe(cbeTextString);
        uc_mem_write(MTK, VM_DreamFactoryResourceBuffer_ADDRESS, &resource, 4);
        return vm_set_call_result(resource);
    }
    return stream ? vm_DF_GetStreamTResource(namePtr) : vm_DF_GetTResource(namePtr);
}

static u32 vm_create_image_from_resource_name(const char *name)
{
    u32 namePtr = vm_malloc(strlen(name) + 1);
    u32 imageInfo = vm_malloc(0x80);
    uc_mem_write(MTK, namePtr, name, strlen(name) + 1);
    uc_mem_write(MTK, imageInfo, emptyBuff, 0x80);
    u32 dataPackage = 0;
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &dataPackage, 4);
    if (dataPackage == 0)
    {
        u32 dataPtr = vm_load_resource_blob_from_cbe(name);
        vm_free(namePtr);
        if (dataPtr == 0)
        {
            vm_free(imageInfo);
            return 0;
        }
        vm_IMG_CreateImageFormStream(dataPtr, imageInfo);
        u32 pixels = 0;
        uc_mem_read(MTK, imageInfo, &pixels, 4);
        if (pixels == 0)
        {
            vm_free(imageInfo);
            return 0;
        }
        return imageInfo;
    }
    int fileId = vm_DF_DataPackage_GetFileID(dataPackage, namePtr);
    u32 dataPtr = 0;
    if (fileId < 0)
    {
        dataPtr = vm_load_resource_blob_from_cbe(name);
    }
    else
    {
        dataPtr = vm_DF_DataPackage_GetFileByID(dataPackage, fileId);
    }
    vm_free(namePtr);
    if (dataPtr == 0 || dataPtr == (u32)-1)
    {
        vm_free(imageInfo);
        return 0;
    }
    vm_IMG_CreateImageFormStream(dataPtr, imageInfo);
    u32 pixels = 0;
    uc_mem_read(MTK, imageInfo, &pixels, 4);
    if (pixels == 0)
    {
        vm_free(imageInfo);
        return 0;
    }
    u32 widthFunc = VM_FAKE_IMAGE_WIDTH_FUNC_ADDRESS | 1;
    u32 heightFunc = VM_FAKE_IMAGE_HEIGHT_FUNC_ADDRESS | 1;
    u32 drawFunc = VM_FAKE_IMAGE_DRAW_FUNC_ADDRESS | 1;
    uc_mem_write(MTK, imageInfo + 0x20, &widthFunc, 4);
    uc_mem_write(MTK, imageInfo + 0x24, &heightFunc, 4);
    uc_mem_write(MTK, imageInfo + 0x40, &drawFunc, 4);
    uc_mem_write(MTK, imageInfo + 0x60, &widthFunc, 4);
    uc_mem_write(MTK, imageInfo + 0x64, &heightFunc, 4);
    uc_mem_write(MTK, imageInfo + 0x78, &drawFunc, 4);
    return imageInfo;
}

static void scheduler_prepare_debug_picture_library(void)
{
    u32 debugUiRoot = Global_R9 + 0x9928;
    u32 debugUiObj = 0;
    u32 debugItems = 0;
    uc_mem_read(MTK, debugUiRoot + 0x10, &debugUiObj, 4);
    if (debugUiObj == 0)
        return;

    u16 defaultItemIndex = 2; // UI7.gif, used by the lower-right cancel button.
    uc_mem_write(MTK, debugUiObj + 0x38, &defaultItemIndex, 2);

    u32 widthFunc = VM_FAKE_IMAGE_WIDTH_FUNC_ADDRESS | 1;
    u32 heightFunc = VM_FAKE_IMAGE_HEIGHT_FUNC_ADDRESS | 1;
    u32 drawFunc = VM_FAKE_IMAGE_DRAW_FUNC_ADDRESS | 1;
    u32 lookupFunc = VM_FAKE_IMAGE_LOOKUP_FUNC_ADDRESS | 1;

    u32 callback = 0;
    uc_mem_read(MTK, debugUiObj + 0x20, &callback, 4);
    if (callback == 0)
    {
        uc_mem_write(MTK, debugUiObj + 0x20, &widthFunc, 4);
        uc_mem_write(MTK, debugUiObj + 0x24, &heightFunc, 4);
        uc_mem_write(MTK, debugUiObj + 0x60, &widthFunc, 4);
        uc_mem_write(MTK, debugUiObj + 0x64, &heightFunc, 4);
        uc_mem_write(MTK, debugUiObj + 0x78, &drawFunc, 4);
    }

    uc_mem_read(MTK, debugUiObj + 0x50, &debugItems, 4);
    u32 currentLookupFunc = 0;
    uc_mem_read(MTK, debugUiObj + 0x5c, &currentLookupFunc, 4);
    if (debugItems != 0 && currentLookupFunc == lookupFunc)
        return;

    const char *names[] = {"UI2.gif", "UI8.gif", "UI7.gif", "flowerStyle.gif", "loading.gif"};
    u32 itemTable = vm_malloc(sizeof(names) / sizeof(names[0]) * 4);
    u32 itemCount = 0;
    for (u32 n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
    {
        u32 imageInfo = vm_create_image_from_resource_name(names[n]);
        if (imageInfo)
        {
            uc_mem_write(MTK, itemTable + itemCount * 4, &imageInfo, 4);
            itemCount++;
        }
    }
    if (itemCount == 0)
    {
        u32 imageInfo = vm_malloc(0x80);
        u32 pixelBuffer = vm_malloc(240 * 32 * 2);
        u16 imageWidth = 240;
        u16 imageHeight = 32;
        for (u32 off = 0; off < 240 * 32 * 2; off += sizeof(emptyBuff))
            uc_mem_write(MTK, pixelBuffer + off, emptyBuff, SDL_min((u32)sizeof(emptyBuff), 240 * 32 * 2 - off));
        uc_mem_write(MTK, imageInfo, emptyBuff, 0x80);
        uc_mem_write(MTK, imageInfo, &pixelBuffer, 4);
        uc_mem_write(MTK, imageInfo + 4, &imageWidth, 2);
        uc_mem_write(MTK, imageInfo + 6, &imageHeight, 2);
        uc_mem_write(MTK, itemTable, &imageInfo, 4);
        itemCount = 1;
    }
    if (itemCount == 1)
    {
        u32 imageInfo = 0;
        uc_mem_read(MTK, itemTable, &imageInfo, 4);
        for (u32 n = 1; n < 4; ++n)
            uc_mem_write(MTK, itemTable + n * 4, &imageInfo, 4);
        itemCount = 4;
    }
    uc_mem_write(MTK, debugUiObj + 0x50, &itemTable, 4);
    uc_mem_write(MTK, debugUiObj + 0x54, &itemCount, 4);
    uc_mem_write(MTK, debugUiObj + 0x58, &itemCount, 4);
    uc_mem_write(MTK, debugUiObj + 0x5c, &lookupFunc, 4);
    uc_mem_write(MTK, debugUiObj + 0x60, &widthFunc, 4);
    uc_mem_write(MTK, debugUiObj + 0x64, &heightFunc, 4);
    uc_mem_write(MTK, debugUiObj + 0x78, &drawFunc, 4);
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
    uc_mem_read(MTK, debugUiObj + 0x3d, &state, 1);
    uc_mem_read(MTK, debugUiObj + 0x140, &updateObj, 4);
    if (state == 10 && updateObj == 0)
    {
        u8 hasLocalUpdate = 0;
        u8 nextState = 11;
        uc_mem_read(MTK, Global_R9 + 0x5496, &hasLocalUpdate, 1);
        if (hasLocalUpdate == 1)
            nextState = 9;
        uc_mem_write(MTK, debugUiObj + 0x3d, &nextState, 1);
    }
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
    uc_err err = uc_emu_start(MTK, begin, until, 0, 0);
    normalize_program_exit_pc(begin);
    return err;
}

static uc_err vm_emu_start_count(u32 begin, u32 until, uint64_t count)
{
    g_currentEmuEntry = begin;
    uc_err err = uc_emu_start(MTK, begin, until, 0, count);
    normalize_program_exit_pc(begin);
    return err;
}

static uc_err vm_call4(u32 entry, u32 r0, u32 r1, u32 r2, u32 r3)
{
    u32 lr = PROGRAM_EXIT_ADDR | 1;
    uc_reg_write(MTK, UC_ARM_REG_LR, &lr);
    uc_reg_write(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_write(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_write(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_write(MTK, UC_ARM_REG_R3, &r3);
    return vm_emu_start(entry | 1, PROGRAM_EXIT_ADDR);
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
        uc_err err = vm_call4(callback, context, 0, 0, 0);
        if (err != UC_ERR_OK)
            return err;
    }
    return UC_ERR_OK;
}

static void scheduler_queue_net_task(u32 r0, u32 r1, u32 callback, u32 context)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netTasks[i].active && g_netTasks[i].callback == callback && g_netTasks[i].context == context)
            return;
    }
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (!g_netTasks[i].active)
        {
            g_netTasks[i].active = 1;
            g_netTasks[i].fired = 0;
            g_netTasks[i].delayTicks = 6;
            g_netTasks[i].r0 = r0;
            g_netTasks[i].r1 = r1;
            g_netTasks[i].callback = callback;
            g_netTasks[i].context = context;
            DEBUG_PRINT("[probe_net] queue r0=%x r1=%x cb=%x ctx=%x tick=%u last=%x\n", r0, r1, callback, context, g_schedulerTick, lastAddress);
            return;
        }
    }
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
            DEBUG_PRINT("[probe_net] fire r0=%x r1=%x cb=%x ctx=%x tick=%u\n", task->r0, task->r1, task->callback, task->context, g_schedulerTick);
            u32 netCurrentReqAddr = Global_R9 + 0x9588 + 4;
            u32 oldCurrentReq = 0;
            uc_mem_read(MTK, netCurrentReqAddr, &oldCurrentReq, 4);
            u32 request = task->context - 4;
            uc_mem_write(MTK, netCurrentReqAddr, &request, 4);
            uc_err err = vm_call4(task->callback, 0, 0, 0, 5);
            uc_mem_write(MTK, netCurrentReqAddr, &oldCurrentReq, 4);
            if (err != UC_ERR_OK)
                return err;
        }
    }
    return UC_ERR_OK;
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
                if (isKeyDown == SDLK_UNKNOWN)
                {
                    isKeyDown = ev.key.keysym.sym;
                    keyEvent(MR_KEY_PRESS, ev.key.keysym.sym);
                }
                break;
            case SDL_KEYUP:
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
    printf("r0:%x r1:%x r2:%x r3:%x r4:%x r5:%x r6:%x r7:%x r8:%x r9:%x\n", r0, r1, r2, r3, r4);
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
    uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);     // 程序退出点
    p = vm_emu_start(startAddr + 1, exitAddr); // thumb模式

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
            u32 tScreenUpdateEntry = 0;
            u32 tScreenEventEntry = 0;
            u32 tScreenInitEntry = 0;
            u32 tScreenInitedPtr = 0;
            while (p == UC_ERR_OK && screenStructChange != 1)
            {
                p = scheduler_tick();
                if (p != UC_ERR_OK)
                    break;
                if (tScreenInitedPtr != vmAddedScreen)
                {
                    scheduler_prepare_debug_picture_library();
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
                    }
                    tScreenInitedPtr = vmAddedScreen;
                }
                scheduler_prepare_debug_picture_library();
                scheduler_normalize_startup_screen_state();
                u32 debugUiRoot = Global_R9 + 0x9928;
                u32 debugUiObj = 0;
                u32 debugItems = 0;
                uc_mem_read(MTK, debugUiRoot + 0x10, &debugUiObj, 4);
                if (debugUiObj)
                {
                    u16 defaultItemIndex = 2; // UI7.gif, used by the lower-right cancel button.
                    uc_mem_write(MTK, debugUiObj + 0x38, &defaultItemIndex, 2);
                    u32 callback = 0;
                            uc_mem_read(MTK, debugUiObj + 0x20, &callback, 4);
                    if (callback == 0)
                    {
                        u32 widthFunc = VM_FAKE_IMAGE_WIDTH_FUNC_ADDRESS | 1;
                        u32 heightFunc = VM_FAKE_IMAGE_HEIGHT_FUNC_ADDRESS | 1;
                        u32 drawFunc = VM_FAKE_IMAGE_DRAW_FUNC_ADDRESS | 1;
                        uc_mem_write(MTK, debugUiObj + 0x20, &widthFunc, 4);
                        uc_mem_write(MTK, debugUiObj + 0x24, &heightFunc, 4);
                        uc_mem_write(MTK, debugUiObj + 0x60, &widthFunc, 4);
                        uc_mem_write(MTK, debugUiObj + 0x64, &heightFunc, 4);
                        uc_mem_write(MTK, debugUiObj + 0x78, &drawFunc, 4);
                    }
                    uc_mem_read(MTK, debugUiObj + 0x50, &debugItems, 4);
                    if (debugItems == 0)
                    {
                        const char *names[] = {"UI2.gif", "UI8.gif", "UI7.gif", "flowerStyle.gif"};
                        u32 itemTable = vm_malloc(16);
                        u32 itemCount = 0;
                        u32 widthFunc = VM_FAKE_IMAGE_WIDTH_FUNC_ADDRESS | 1;
                        u32 heightFunc = VM_FAKE_IMAGE_HEIGHT_FUNC_ADDRESS | 1;
                        u32 drawFunc = VM_FAKE_IMAGE_DRAW_FUNC_ADDRESS | 1;
                        u32 lookupFunc = VM_FAKE_IMAGE_LOOKUP_FUNC_ADDRESS | 1;
                        for (u32 n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
                        {
                            u32 imageInfo = vm_create_image_from_resource_name(names[n]);
                            if (imageInfo)
                            {
                                uc_mem_write(MTK, itemTable + itemCount * 4, &imageInfo, 4);
                                itemCount++;
                            }
                        }
                        if (itemCount == 0)
                        {
                            u32 imageInfo = vm_malloc(0x80);
                            u32 pixelBuffer = vm_malloc(240 * 32 * 2);
                            u16 imageWidth = 240;
                            u16 imageHeight = 32;
                            for (u32 off = 0; off < 240 * 32 * 2; off += sizeof(emptyBuff))
                                uc_mem_write(MTK, pixelBuffer + off, emptyBuff, SDL_min((u32)sizeof(emptyBuff), 240 * 32 * 2 - off));
                            uc_mem_write(MTK, imageInfo, emptyBuff, 0x80);
                            uc_mem_write(MTK, imageInfo, &pixelBuffer, 4);
                            uc_mem_write(MTK, imageInfo + 4, &imageWidth, 2);
                            uc_mem_write(MTK, imageInfo + 6, &imageHeight, 2);
                            uc_mem_write(MTK, itemTable, &imageInfo, 4);
                            itemCount = 1;
                        }
                        if (itemCount == 1)
                        {
                            u32 imageInfo = 0;
                            uc_mem_read(MTK, itemTable, &imageInfo, 4);
                            for (u32 n = 1; n < 4; ++n)
                                uc_mem_write(MTK, itemTable + n * 4, &imageInfo, 4);
                            itemCount = 4;
                        }
                        uc_mem_write(MTK, debugUiObj + 0x50, &itemTable, 4);
                        uc_mem_write(MTK, debugUiObj + 0x54, &itemCount, 4);
                        uc_mem_write(MTK, debugUiObj + 0x58, &itemCount, 4);
                        uc_mem_write(MTK, debugUiObj + 0x5c, &lookupFunc, 4);
                        uc_mem_write(MTK, debugUiObj + 0x60, &widthFunc, 4);
                        uc_mem_write(MTK, debugUiObj + 0x64, &heightFunc, 4);
                        uc_mem_write(MTK, debugUiObj + 0x78, &drawFunc, 4);
                    }
                }
                uc_mem_read(MTK, vmAddedScreen + 0x04, &tScreenUpdateEntry, 4);
                uc_mem_read(MTK, vmAddedScreen + 0x08, &tScreenEventEntry, 4);
                uc_mem_read(MTK, vmAddedScreen + 0x0c, &tScreenRenderEntry, 4);
                if (tScreenRenderEntry == 0)
                {
                    printf("TScreen未设置render入口\n");
                    assert(0);
                }
                p = scheduler_dispatch_tscreen_event(tScreenEventEntry, vmAddedScreen);
                if (p != UC_ERR_OK)
                {
                    printf("TScreen event异常:%s\n", uc_strerror(p));
                    break;
                }
                if (screenStructChange == 1)
                    break;
                if (tScreenUpdateEntry)
                {
                    uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                    uc_reg_write(MTK, UC_ARM_REG_R0, &vmAddedScreen);
                    p = vm_emu_start(tScreenUpdateEntry, exitAddr);
                    if (p != UC_ERR_OK)
                    {
                        printf("TScreen update异常:%s\n", uc_strerror(p));
                        break;
                    }
                    if (screenStructChange == 1)
                        break;
                }
                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                uc_reg_write(MTK, UC_ARM_REG_R0, &vmAddedScreen);
                p = vm_emu_start(tScreenRenderEntry, exitAddr);
                if (p != UC_ERR_OK)
                    break;
                UpdateLcd();
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
            if (screenStructChange == 1)
            {
                uc_mem_read(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &screenFuncPtr, 4); // 得到screen函数表地址的指针
                if (screenFuncPtr >= Global_R9 && screenFuncPtr < ROM_ADDRESS + size_16mb)
                    screenThisPtr = screenFuncPtr - 0x94;
                uc_mem_read(MTK, screenFuncPtr, &screenInitEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 4, &screenDestoryEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 8, &screenLogicEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 12, &screenRenderEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 16, &screenPauseEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 20, &screenRemuseEntry, 4);
                uc_mem_read(MTK, screenFuncPtr + 24, &screenResouceLoadEntry, 4);
                printf("[SCR_FUNC](init:%x,destory:%x,logic:%x,render:%x,pause:%x,remuse:%x,resLoad:%x)\n", screenInitEntry, screenDestoryEntry, screenLogicEntry, screenRenderEntry, screenPauseEntry, screenRemuseEntry, screenResouceLoadEntry);
                screenStructChange = 0;
            }

            uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
            if (screenThisPtr)
                uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
            p = vm_emu_start(screenInitEntry, exitAddr);
            printf("ScreenInit Ok\n");
            if (p == UC_ERR_OK)
            {
                while (true)
                {
                    p = scheduler_tick();
                    if (p != UC_ERR_OK)
                        break;
                    if (screenStructChange == 1)
                        break;
                    if (screenStructNotifyLoadRes == 1)
                    {
                        screenStructNotifyLoadRes = 0;
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr); // 程序退出点
                        if (screenThisPtr)
                            uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                        p = vm_emu_start(screenResouceLoadEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            printf("SCR_ResourceLoad异常:%s\n", uc_strerror(p));
                            assert(0);
                        }
                    }
                    simulateKey = 0;
                    simulatePress = 0;
                    simulateTouchDown = 0;
                    simulateTouchUp = 0;
                    simulateTouchDrag = 0;
                    vm_event *evt = DequeueVMEvent();
                    if (evt != NULL)
                    {
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
                        }
                    }
                    if (1)
                    {
                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                        if (screenThisPtr)
                            uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                        p = vm_emu_start(screenLogicEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            printf("SCR_Logic异常:%s\n", uc_strerror(p));
                            assert(0);
                        }

                        uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                        if (screenThisPtr)
                            uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                        p = vm_emu_start(screenRenderEntry, exitAddr);
                        if (p != UC_ERR_OK)
                        {
                            dumpCpuInfo();
                            printf("SCR_Render异常:%s\n", uc_strerror(p));
                            assert(0);
                        }
                    }
                    UpdateLcd();
                    SDL_Delay(100);
                }
                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                if (screenThisPtr)
                    uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                p = vm_emu_start(screenDestoryEntry, exitAddr);
                if (p != UC_ERR_OK)
                {
                    printf("SCR_Destory异常\n");
                    break;
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
    UpdateLcd();
    InitFontEngine();

    if (err)
    {
        printf("Failed mem  Rom map: %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    err = uc_hook_add(MTK, &trace, UC_HOOK_CODE, hookCodeCallBack, 0, 0, 0xFFFFFFFF);
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

void hookCodeCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

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
    if (((u32)address & ~1u) == 0x103b07c)
    {
        vm_set_call_result(0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
        return;
    }
    if (((u32)address & ~1u) == 0x103a02e)
    {
        vm_set_call_result(0);
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
        return;
    }
    if (((u32)address & ~1u) == VM_FAKE_IMAGE_WIDTH_FUNC_ADDRESS)
    {
        u32 picLib = 0;
        u32 index = 0;
        u32 itemTable = 0;
        u32 imageObj = 0;
        u16 imageWidth = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &picLib);
        uc_reg_read(MTK, UC_ARM_REG_R1, &index);
        if (picLib)
        {
            uc_mem_read(MTK, picLib + 0x10, &itemTable, 4);
            if (itemTable)
                uc_mem_read(MTK, itemTable + index * 4, &imageObj, 4);
        }
        if (imageObj == 0)
            imageObj = picLib;
        if (imageObj)
            uc_mem_read(MTK, imageObj + 4, &imageWidth, 2);
        tmp1 = (imageWidth > 0 && imageWidth <= LCD_WIDTH) ? imageWidth : 240;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
        return;
    }
    if (((u32)address & ~1u) == VM_FAKE_IMAGE_HEIGHT_FUNC_ADDRESS)
    {
        u32 picLib = 0;
        u32 index = 0;
        u32 itemTable = 0;
        u32 imageObj = 0;
        u16 imageHeight = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &picLib);
        uc_reg_read(MTK, UC_ARM_REG_R1, &index);
        if (picLib)
        {
            uc_mem_read(MTK, picLib + 0x10, &itemTable, 4);
            if (itemTable)
                uc_mem_read(MTK, itemTable + index * 4, &imageObj, 4);
        }
        if (imageObj == 0)
            imageObj = picLib;
        if (imageObj)
            uc_mem_read(MTK, imageObj + 6, &imageHeight, 2);
        tmp1 = (imageHeight > 0 && imageHeight <= LCD_HEIGHT) ? imageHeight : 32;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
        return;
    }
    if (((u32)address & ~1u) == VM_FAKE_IMAGE_DRAW_FUNC_ADDRESS)
    {
        u32 picLib = 0;
        u32 index = 0;
        u32 itemTable = 0;
        u32 imageObj = 0;
        u32 pixels = 0;
        u16 imageWidth = 0;
        u16 imageHeight = 0;
        int dstX = 0;
        int dstY = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &picLib);
        uc_reg_read(MTK, UC_ARM_REG_R1, &index);
        uc_reg_read(MTK, UC_ARM_REG_R2, &dstX);
        uc_reg_read(MTK, UC_ARM_REG_R3, &dstY);
        if (picLib)
        {
            uc_mem_read(MTK, picLib + 0x10, &itemTable, 4);
            if (itemTable)
                uc_mem_read(MTK, itemTable + index * 4, &imageObj, 4);
        }
        if (imageObj == 0)
            imageObj = picLib;
        if (imageObj)
        {
            uc_mem_read(MTK, imageObj, &pixels, 4);
            uc_mem_read(MTK, imageObj + 4, &imageWidth, 2);
            uc_mem_read(MTK, imageObj + 6, &imageHeight, 2);
        }
        if (pixels && imageWidth && imageHeight)
        {
            u32 srcPitch = (((4 - imageWidth) & 3) + imageWidth) * 2;
            int startX = dstX < 0 ? -dstX : 0;
            int startY = dstY < 0 ? -dstY : 0;
            int copyW = imageWidth - startX;
            int copyH = imageHeight - startY;
            if (dstX + startX + copyW > LCD_WIDTH)
                copyW = LCD_WIDTH - (dstX + startX);
            if (dstY + startY + copyH > LCD_HEIGHT)
                copyH = LCD_HEIGHT - (dstY + startY);
            if (copyW > 0 && copyH > 0)
            {
                u8 rowBuffer[480];
                for (int row = 0; row < copyH; ++row)
                {
                    u32 srcOff = (startY + row) * srcPitch + startX * 2;
                    u32 dstOff = ((dstY + startY + row) * LCD_WIDTH + dstX + startX) * 2;
                    uc_mem_read(MTK, pixels + srcOff, rowBuffer, copyW * 2);
                    u16 *dst = (u16 *)(Lcd_Cache_Buffer + dstOff);
                    u16 *src = (u16 *)rowBuffer;
                    for (int col = 0; col < copyW; ++col)
                    {
                        if (src[col] != 0)
                            dst[col] = src[col];
                    }
                    uc_mem_write(MTK, VM_screenImage_ADDRESS + dstOff, Lcd_Cache_Buffer + dstOff, copyW * 2);
                }
            }
        }
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
        return;
    }
    if (((u32)address & ~1u) == VM_FAKE_IMAGE_LOOKUP_FUNC_ADDRESS)
    {
        u32 namePtr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R1, &namePtr);
        tmp1 = 0;
        if (namePtr)
        {
            vm_readStringByPtr(namePtr, cbeTextString);
            if (strcmp((char *)cbeTextString, "UI8.gif") == 0)
                tmp1 = 1;
            else if (strcmp((char *)cbeTextString, "UI7.gif") == 0)
                tmp1 = 2;
            else if (strcmp((char *)cbeTextString, "flowerStyle.gif") == 0)
                tmp1 = 3;
            else if (strcmp((char *)cbeTextString, "loading.gif") == 0)
                tmp1 = 4;
        }
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
        return;
    }
    if (address == 0x10189e4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp1);
        if (tmp1 == 0)
        {
            tmp1 = 240;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            tmp1 = 0x10189e7;
            vm_bx(tmp1);
            return;
        }
    }
    if (address == 0x10189f2)
    {
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp1);
        if (tmp1 == 0)
        {
            tmp1 = 32;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            tmp1 = 0x10189f5;
            vm_bx(tmp1);
            return;
        }
    }
    if (address == 0x1039e2c || address == 0x1039e3a)
    {
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp1);
        if (tmp1 == 0)
        {
            tmp1 = (address == 0x1039e2c) ? 240 : 32;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            tmp1 = ((u32)address + 2) | 1;
            vm_bx(tmp1);
            return;
        }
    }
    if (address == 0x1018946 || address == 0x1018966 || address == 0x1018984 || address == 0x10189a4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R6, &tmp1);
        if (tmp1 == 0)
        {
            tmp2 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
            tmp2 = ((u32)address + 2) | 1;
            vm_bx(tmp2);
            return;
        }
    }
    if (address == 0x103ab4c || address == 0x103ab9a || address == 0x103abe0 || address == 0x103ac1a || address == 0x103ac6e)
    {
        uc_reg_read(MTK, UC_ARM_REG_R6, &tmp1);
        if (tmp1 == 0)
        {
            tmp2 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
            tmp2 = ((u32)address + 2) | 1;
            vm_bx(tmp2);
            return;
        }
    }
    if (address == 0x10025c6)
    {
        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp1);
        uc_mem_read(MTK, tmp1 + 12, &tmp2, 4);
        if ((tmp2 & ~1u) == PROGRAM_EXIT_ADDR)
        {
            uc_emu_stop(MTK);
            return;
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
    else if (address >= VM_MANAGER_FUNC_LIST_ADDRESS && address < VM_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)
    {
        u32 idx = (address - VM_MANAGER_FUNC_LIST_ADDRESS) / 4;
        if (idx == 1)
        {
            tmp1 = VM_MANAGER_FILEIO_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetIoManager\n");
        }
        else if (idx == 3)
        {
            tmp1 = VM_MANAGER_LCD_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetLcdManager\n");
        }
        else if (idx == 5)
        {
            tmp1 = VM_MANAGER_TIMER_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetTimeManager\n");
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
        else if (idx == 11)
        {
            tmp1 = VM_MANAGER_BILLING_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetBillingManager\n");
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
                    uc_mem_write(MTK, tmp1 + tmp2 * 4, &tmp3, 4);
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
        else if (idx == 17)
        {
            tmp1 = VM_MANAGER_UCS2_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetUcs2StrManager\n");
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
        else if (idx == 23)
        {
            tmp1 = VM_MANAGER_GAME_LCD_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetGameLcdManager\n");
        }
        else if (idx == 25)
        {
            tmp1 = VM_MANAGER_GAME_UTIL_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetGameUtilManager\n");
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
            printf("[call]vMInitManager\n");
            assert(0);
        }
        else if (idx == 35)
        {
            printf("[call]vMInitGSensorManager\n");
            assert(0);
        }
        else if (idx == 36)
        {
            tmp1 = VM_MANAGER_SENSOR_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetGSensorManager\n");
        }
        else if (idx == 37)
        {
            printf("[call]vMInitVmStdManager\n");
            assert(0);
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
            printf("[call]vMInitVmImManager\n");
            assert(0);
        }
        else if (idx == 46)
        {
            tmp1 = VM_MANAGER_VMIM_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            DEBUG_PRINT("[call]vMGetVmImManager\n");
        }
        else if (idx == 49)
        {
            printf("[call]VmInitVideoManager\n");
            assert(0);
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
    }
    else if (address >= VM_SYS_MANAGER_FUNC_LIST_ADDRESS && address < (VM_SYS_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
            vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
            uc_reg_read(MTK, UC_ARM_REG_R1, &line);
            uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
            printf("[call]vMAssert(%s:%u, lr:%x, last:%x)\n", cbeTextString, line, lr, lastAddress);
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
            printf("[call]vmInputText\n");
            assert(0);
        }
        else if (idx == 44)
        {
            printf("[call]vmInputPassword\n");
            assert(0);
        }
        else if (idx == 45)
        {
            printf("[call]vmInputClose\n");
            assert(0);
        }
        else if (idx == 46)
        {
            printf("[call]vmInputIsOpen\n");
            assert(0);
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
            printf("[call]Net_get_DefaultLoginInfo\n");
            assert(0);
        }
        else if (idx == 69)
        {
            printf("[call]Net_write_DefaultLoginInfo\n");
            assert(0);
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
            printf("[call]vmDlGet_sp_bf\n");
            assert(0);
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
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
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
            printf("[call]vmSysSetSettingProfile\n");
            assert(0);
        }
        else if (idx == 112)
        {
            printf("[call]vmSysGetSettingProfile\n");
            assert(0);
        }
        else if (idx == 113)
        {
            printf("[call]vmSysGetSettingProfileName\n");
            assert(0);
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
    }
    else if (address >= VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS && address < (VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    else if (address >= VM_MANAGER_LCD_FUNC_LIST_ADDRESS && address < (VM_MANAGER_LCD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
            UpdateLcd();
            vm_set_call_result(0);
        }
        else if (idx == 4)
        {
            DEBUG_PRINT("[call]vMGetCurrFontType\n");
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 5)
        {
            // todo
            DEBUG_PRINT("[call]vMSetCurrFontType\n");
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
            tmp1 = mesureStringWidth(cbeTextString);
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            // gbk_to_utf8(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
            DEBUG_PRINT("[call]vMGetStringWidth(%d,%x)\n", tmp1, cbeTextString[0]);
            // tmp1 = mesureStringWidth(cbeTextString);
        }
        else if (idx == 9)
        {
            // todo
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
            drawFontString(cbeTextString, tmp2, tmp3, (u16)tmp4);
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

            drawFontString(cbeTextString, tmp2, tmp3, color);
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
            drawFontString(cbeTextString, tmp2, tmp3, color);
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
            drawFontString(cbeTextString, tmp2, tmp3, color);
            vm_set_call_result(1);
        }
        else if (idx == 14)
        {
            printf("[call]vMDrawStringRect\n");
            assert(0);
        }
        else if (idx == 15)
        {
            printf("[call]vMDrawLineEx\n");
            assert(0);
        }
        else if (idx == 16)
        {
            printf("[call]vMDrawLine\n");
            assert(0);
        }
        else if (idx == 17)
        {
            u32 rectH, rectColor;
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
            uc_reg_read(MTK, UC_ARM_REG_SP, &tmp5);
            uc_mem_read(MTK, tmp5, &rectH, 4);
            uc_mem_read(MTK, tmp5 + 4, &rectColor, 4);
            int x = (int)tmp1;
            int y = (int)tmp2;
            int w = (int)tmp3;
            int h = (int)rectH;
            u16 color = (u16)rectColor;
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
            int x = (int)tmp1;
            int y = (int)tmp2;
            int w = (int)tmp3;
            int h = (int)rectH;
            u16 color = (u16)rectColor;
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
            u32 fillH, fillColor;
            uc_mem_read(MTK, tmp5, &fillH, 4);
            uc_mem_read(MTK, tmp5 + 4, &fillColor, 4);
            int x = (int)tmp1;
            int y = (int)tmp2;
            int w = (int)tmp3;
            int h = (int)fillH;
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
            int x = (int)tmp1;
            int y = (int)tmp2;
            int w = (int)tmp3;
            int h = (int)fillH;
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
            printf("[call]vMUCS2GB\n");
            assert(0);
        }
        else if (idx == 41)
        {
            printf("[call]vMWinPointIsInRect\n");
            assert(0);
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
            printf("[call]vMDrawStringBorder\n");
            assert(0);
        }
        else if (idx == 56)
        {
            printf("[call]vMDrawStringClipAlignBorder\n");
            assert(0);
        }
        else if (idx == 57)
        {
            assert(0);
            printf("[call]vMDrawStringClipBorder\n");
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
    }
    else if (address >= VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
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
            printf("[call]vm_file_truncate\n");
            assert(0);
        }
        else if (idx == 25)
        {
            printf("[call]vm_file_RdWt\n");
            assert(0);
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
    }
    else if (address >= VM_MANAGER_STDIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_STDIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_STDIO_FUNC_LIST_ADDRESS) / 4;
        idx += 1;
        if (idx == 1)
        {
            printf("[call]memcpy\n");
            assert(0);
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
            printf("[call]memset\n");
            assert(0);
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
            printf("[call]strncpy\n");
            assert(0);
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
            printf("[call]strcat\n");
            assert(0);
        }
        else if (idx == 11)
        {
            printf("[call]atol\n");
            assert(0);
        }
        else if (idx == 12)
        {
            printf("[call]memmove\n");
            assert(0);
        }
        else if (idx == 13)
        {
            printf("[call]atoi\n");
            assert(0);
        }
        else if (idx == 14)
        {
            printf("[call]vMpow\n");
            assert(0);
        }
        else if (idx == 15)
        {
            printf("[call]strcmp\n");
            assert(0);
        }
        else if (idx == 16)
        {
            printf("[call]memcmp\n");
            assert(0);
        }
        else if (idx == 17)
        {
            printf("[call]strncmp\n");
            assert(0);
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
    }
    else if (address >= VM_MANAGER_TIMER_FUNC_LIST_ADDRESS && address < (VM_MANAGER_TIMER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    else if (address >= VM_MANAGER_CTRL_FUNC_LIST_ADDRESS && address < (VM_MANAGER_CTRL_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_CTRL_FUNC_LIST_ADDRESS) / 4;

        {
            printf("[impl]vmCtrlManager调用位置:%d\n", idx);
            assert(0);
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
    }
    else if (address >= VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS && address < (VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS) / 4;
        DEBUG_PRINT("[probe_net_idx] idx=%u last=%x\n", idx, lastAddress);
        if (idx == 0)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
            tmp5 = g_nextNetConnectId++;
            if (tmp5 == 0)
                tmp5 = g_nextNetConnectId++;
            if (tmp4)
                uc_mem_write(MTK, tmp4, &tmp5, 4);
            scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
            vm_set_call_result(1);
        }
        else if (idx == 1)
        {
            vm_set_call_result(0);
        }
        else if (idx == 2)
        {
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
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
            scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
            vm_set_call_result(1);
        }
        else if (idx == 17)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            scheduler_queue_net_task(tmp1, 0, tmp2, tmp3);
            vm_set_call_result(1);
        }
        else if (idx == 4 || idx == 19 || idx == 29 || idx == 30)
        {
            vm_set_call_result(1);
        }
        else if (idx == 5 || idx == 12 || idx == 13 || idx == 21 || idx == 24 || idx == 25 || idx == 33 || idx == 34 || idx == 36 || idx == 37 || idx == 39 || idx == 41 || idx == 42)
        {
            vm_set_call_result(0);
        }
        else if (idx == 8 || idx == 9 || idx == 10 || idx == 11 || idx == 14 || idx == 15 || idx == 16 || idx == 22 || idx == 23 || idx == 26 || idx == 27 || idx == 28 || idx == 31 || idx == 32 || idx == 35 || idx == 38 || idx == 40)
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
    }
    else if (address >= VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS && address < (VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    // df
    else if (address >= VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS && address < (VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    //
    else if (address >= VM_MANAGER_BILLING_FUNC_LIST_ADDRESS && address < (VM_MANAGER_BILLING_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
            // DEBUG_PRINT("[call]CDownGetFileNameByAppID\n");
            tmp1 = 0x104fd48; // "Wpay9990Ker42WqvgaV100.CBM"
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
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
    }

    // ucs2
    else if (address >= VM_MANAGER_UCS2_FUNC_LIST_ADDRESS && address < (VM_MANAGER_UCS2_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
            printf("[call]vmutStrcpyUcs2\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // dst
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // src
            vm_readStringUCS2ByReg(UC_ARM_REG_R1, cbeTextString);
            uc_mem_write(MTK, tmp1, cbeTextString, (strlen_utf16(cbeTextString) + 1) * 2);
        }
        else if (idx == 3)
        {
            printf("[call]vmutStrcatUcs2\n");
            assert(0);
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
            printf("[call]vmutStrncpyUcs2\n");
            assert(0);
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
    }
    else if (address >= VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS && address < (VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS) / 4;
        if (idx == 0)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
            tmp2 = 0;
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
            screenStructChange = 1;
            vm_set_call_result(VM_SCREEN_isInQuit_ADDRESS);
        }
        else if (idx == 4)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vmAddedScreen = tmp1;
            vm_set_call_result(0);
        }
        else
        {
            printf("[impl]vmScreenManager调用位置:%d\n", idx);
            assert(0);
        }
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
    }
    //
    // df scr
    else if (address >= VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS && address < (VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    //
    // game lcd
    else if (address >= VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS && address < (VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    //
    // net app
    else if (address >= VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS && address < (VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    //
    // audio
    else if (address >= VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    //
    // sensor
    else if (address >= VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS && address < (VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS) / 4;
        if (1)
        {
            printf("[impl]vmSensorManager调用位置:%d\n", idx);
            assert(0);
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        vm_bx(tmp1);
    }
    //
    // vmim
    else if (address >= VM_MANAGER_VMIM_FUNC_LIST_ADDRESS && address < (VM_MANAGER_VMIM_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
            tmp2 = vm_get_var(tmp1 + 4);
            tmp3 = vm_get_var_short(tmp1 + 8);
            if (tmp2 && tmp3)
                uc_mem_write(MTK, tmp2, emptyBuff, tmp3);
            vm_set_call_result(0);
        }
        else if (idx == 3)
        {
            vm_set_call_result(0);
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
    }
    //
    else if (address >= VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS && address < (VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
            printf("[call]strncpy\n");
            assert(0);
        }
        else if (idx == 142)
        {
            DEBUG_PRINT("[call]strcpy\n");
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_strcpy(tmp1, tmp2);
        }
        else if (idx == 143)
        {
            printf("[call]strcat\n");
            assert(0);
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
    }
    else if (address >= VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS && address < (VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
            printf("[call]DF_DataPackage_ReleasePackage\n");
            assert(0);
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
    }
    else if (address >= VM_MF_MemoryBlock_FUNC_LIST_ADDRESS && address < (VM_MF_MemoryBlock_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    else if (address >= VM_APPSTORE_FUNC_LIST_ADDRESS && address < (VM_APPSTORE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    else if (address >= VM_DL_LOAD_FUNC_LIST_ADDRESS && address < (VM_DL_LOAD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    else if (address >= VM_DL_RS_FUNC_LIST_ADDRESS && address < (VM_DL_RS_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    else if (address >= VM_DL_IMAGE_FUNC_LIST_ADDRESS && address < (VM_DL_IMAGE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
    }
    else if (address >= VM_VIDEO_FUNC_LIST_ADDRESS && address < (VM_VIDEO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
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
