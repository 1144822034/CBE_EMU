#define GDB_SERVER_SUPPORT_
#define GDI_LAYER_DEBUG_

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
int simulateTouchX = 0;
int simulateTouchY = 0;
u32 screenStructChange = 0;

u32 lastSprintfPtr = 0;

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

/**
 * @brief 按键事件
 * @param type 4=按下 5=松开
 * @param key 按键值
 */
void keyEvent(int type, int key)
{
    // printf("keyboard(%x,type=%d)\n", key, type);
    int skey = 0;
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

    int isPress = type == 1 ? 1 : 0;
    EnqueueVMEvent(VM_EVENT_TOUCHSCREEN, isPress, (x << 16) | y);
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
#define LOAD_CBE_PATH "CBE/恶魔城.CBE"
#define LOAD_CBE_PATH "CBE/鬼吹灯.CBE"
#define LOAD_CBE_PATH "CBE/孤岛.CBE"
#define LOAD_CBE_PATH "CBE/魔塔.CBE"

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
    uc_reg_write(MTK, UC_ARM_REG_LR, &exitAddr);          // 程序退出点
    p = uc_emu_start(MTK, startAddr + 1, exitAddr, 0, 0); // thumb模式
    // 第二次初始化
    if (p == UC_ERR_OK)
    {
        uc_mem_read(MTK, VM_Manager_Table_ADDRESS, &startAddr, 4);
        uc_reg_write(MTK, UC_ARM_REG_LR, &exitAddr);
        p = uc_emu_start(MTK, startAddr, exitAddr, 0, 0);
    }

    if (p == UC_ERR_OK)
    {
        while (true)
        {

            u32 screenFuncPtr;
            u32 screenInitEntry;
            u32 screenDestoryEntry;
            u32 screenLogicEntry;
            u32 screenRenderEntry;
            u32 screenPauseEntry;
            u32 screenRemuseEntry;
            u32 screenResouceLoadEntry;
            if (screenStructChange == 1)
            {
                uc_mem_read(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &screenFuncPtr, 4); // 得到screen函数表地址的指针
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

            uc_reg_write(MTK, UC_ARM_REG_LR, &exitAddr); // 程序退出点
            p = uc_emu_start(MTK, screenInitEntry, exitAddr, 0, 0);
            printf("ScreenInit Ok\n");
            if (p == UC_ERR_OK)
            {
                while (true)
                {
                    if (screenStructChange == 1)
                        break;
                    simulateKey = 0;
                    simulatePress = 0;
                    simulateTouchPress = 0;
                    simulateTouchX = 0;
                    simulateTouchY = 0;
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
                                simulateTouchPress = evt->r0;
                                simulateTouchX = (evt->r1 >> 16) & 0xffff;
                                simulateTouchY = evt->r1 & 0xffff;
                            }
                        }
                    }
                    if (1)
                    {
                        uc_reg_write(MTK, UC_ARM_REG_LR, &exitAddr);
                        p = uc_emu_start(MTK, screenLogicEntry, exitAddr, 0, 0);
                        if (p != UC_ERR_OK)
                        {
                            printf("SCR_Logic异常:%s\n", uc_strerror(p));
                            assert(0);
                        }

                        uc_reg_write(MTK, UC_ARM_REG_LR, &exitAddr);
                        p = uc_emu_start(MTK, screenRenderEntry, exitAddr, 0, 0);
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
                uc_reg_write(MTK, UC_ARM_REG_LR, &exitAddr);
                p = uc_emu_start(MTK, screenDestoryEntry, exitAddr, 0, 0);
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

    // SetConsoleOutputCP(CP_UTF8);

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

    InitVmEvent();

    char nameBuff[64] = LOAD_CBE_PATH;
    utf8_to_gbk(nameBuff, cbeTextString, mySizeOf(cbeTextString));
    char *fileBuffer = readFile(cbeTextString, &changeTmp1);
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

    InitVmMalloc();
    InitLcd();
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

        SDL_free(fileBuffer);

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
    u32 tmp1, tmp2, tmp3, tmp4;

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
        vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
        printf("[vm_log_trace]");
        vm_sprintf();
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_FUNC_LIST_ADDRESS && address < VM_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)
    {
        u32 idx = (address - VM_MANAGER_FUNC_LIST_ADDRESS) / 4;
        if (idx == 1)
        {
            tmp1 = VM_MANAGER_FILEIO_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetIoManager\n");
        }
        else if (idx == 3)
        {
            tmp1 = VM_MANAGER_LCD_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);

            printf("[call]vMGetLcdManager\n");
        }
        else if (idx == 5)
        {
            tmp1 = VM_MANAGER_TIMER_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);

            printf("[call]vMGetTimeManager\n");
        }
        else if (idx == 7)
        {
            tmp1 = VM_MANAGER_CTRL_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);

            printf("[call]vMGetCtrlManager\n");
        }
        else if (idx == 8)
        {
            // 传入指针写函数表
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMInitMemoryManager(%x)[%x]\n", tmp1, lastAddress);
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
            printf("[call]vMGetMemoryManager\n");
        }
        else if (idx == 11)
        {
            tmp1 = VM_MANAGER_BILLING_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetBillingManager\n");
        }
        else if (idx == 13)
        {
            tmp1 = VM_MANAGER_SCREEN_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetScreenManager\n");
        }
        else if (idx == 15)
        {
            tmp1 = VM_MANAGER_NETWORK_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetNetManager\n");
        }
        else if (idx == 17)
        {
            tmp1 = VM_MANAGER_UCS2_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetUcs2StrManager\n");
        }
        else if (idx == 19)
        {
            printf("[call]vMGetSysManager\n");
            // 返回sys函数表地址
            tmp1 = VM_SYS_MANAGER_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 21)
        {
            tmp1 = VM_MANAGER_DF_SCRIPT_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetDFScriptManager\n");
        }
        else if (idx == 23)
        {
            tmp1 = VM_MANAGER_GAME_LCD_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetGameLcdManager\n");
        }
        else if (idx == 25)
        {
            tmp1 = VM_MANAGER_GAME_UTIL_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetGameUtilManager\n");
        }
        else if (idx == 27)
        {
            tmp1 = VM_MANAGER_DF_ENGINE_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetDFEnginelManager\n");
        }
        else if (idx == 29)
        {
            tmp1 = VM_MANAGER_NETAPP_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetNetAppManager\n");
        }
        else if (idx == 31)
        {
            tmp1 = VM_MANAGER_AUDIO_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);

            printf("[call]vMGetAudioManager\n");
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

            printf("[call]vMInitGameManagerOld(%x,%X)[%x]\n", tmp1, tmp2, lastAddress);
        }
        else if (idx == 33)
        {
            tmp1 = VM_MANAGER_GAMEOLD_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetGameManagerOld\n");
        }
        else if (idx == 34)
        {
            printf("[call]vMInitManager\n");
        }
        else if (idx == 35)
        {
            printf("[call]vMInitGSensorManager\n");
        }
        else if (idx == 36)
        {
            tmp1 = VM_MANAGER_SENSOR_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);

            printf("[call]vMGetGSensorManager\n");
        }
        else if (idx == 37)
        {
            printf("[call]vMInitVmStdManager\n");
        }
        else if (idx == 38)
        {
            tmp1 = VM_MANAGER_STDIO_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);

            printf("[call]vMGetVmStdManager\n");
        }
        else if (idx == 39)
        {
            printf("[call]vMInitDlLoadManager\n");
        }
        else if (idx == 40)
        {
            printf("[call]vMGetDlLoadManager\n");
        }
        else if (idx == 41)
        {
            printf("[call]vMGetDlRsManager\n");
        }
        else if (idx == 42)
        {
            printf("[call]vMInitDlRsManager\n");
        }
        else if (idx == 43)
        {
            printf("[call]vMGetDlImageManager\n");
        }
        else if (idx == 44)
        {
            printf("[call]vMInitDlImageManager\n");
        }
        else if (idx == 45)
        {

            printf("[call]vMInitVmImManager\n");
        }
        else if (idx == 46)
        {
            tmp1 = VM_MANAGER_VMIM_TABLE_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetVmImManager\n");
        }
        else if (idx == 49)
        {
            printf("[call]VmInitVideoManager\n");
        }
        else if (idx == 50)
        {
            printf("[call]VmGetVideoManager\n");
        }
        else if (idx == 51)
        {
            printf("[call]VmGetDlWPayManager\n");
        }
        else
        {
            printf("[impl]vmManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_SYS_MANAGER_FUNC_LIST_ADDRESS && address < (VM_SYS_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_SYS_MANAGER_FUNC_LIST_ADDRESS) / 4;
        if (idx == 0)
        {
            printf("[call]vMIsSimReady\n");
        }
        else if (idx == 1)
        {
            printf("[call]vmSysIsHaveNetWork\n");
        }
        else if (idx == 2)
        {
            printf("[call]vMIsSystemReady\n");
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
            // printf("[call]vMGetOperator\n");
        }
        else if (idx == 4)
        {
            printf("[call]vMGetIMEI\n");
        }
        else if (idx == 5)
        {
            printf("[call]vMGetIMSI\n");
        }
        else if (idx == 6)
        {
            printf("[call]vMGetPrjName\n");
        }
        else if (idx == 7)
        {
            printf("[call]vMGetPrjVersion\n");
        }
        else if (idx == 8)
        {
            printf("[call]vMIsCallConnect\n");
        }
        else if (idx == 9)
        {
            printf("[call]vMIsDCopen\n");
        }
        else if (idx == 10)
        {
            printf("[call]vMGetStkCardStatus\n");
        }
        else if (idx == 11)
        {
            printf("[call]vMGetActiveSim\n");
        }
        else if (idx == 12)
        {
            printf("[call]vmGetCoolbarlistInit\n");
        }
        else if (idx == 13)
        {
            printf("[call]vmEnterWinSetFPS\n");
        }
        else if (idx == 14)
        {
            printf("[call]vmEnterWinClose\n");
        }
        else if (idx == 15)
        {
            printf("[call]vMAudioIsSupportInCb\n");
            tmp1 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 16)
        {
            vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
            printf("[call]vMAssert(%s)\n", cbeTextString);
            dumpCpuInfo();
            assert(0);
        }
        else if (idx == 17)
        {
            printf("[call]Coolbar_GetCoolbarDirPath\n");
            cbeTextString[0] = '.';
            cbeTextString[1] = '/';
            cbeTextString[2] = 0;
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_mem_write(MTK, tmp1, cbeTextString, 3);
        }
        else if (idx == 18)
        {
            printf("[call]CoolBarDynamicGetVerByAppID\n");
        }
        else if (idx == 19)
        {
            printf("[call]Res_GetCoolBarFullPath\n");
        }
        else if (idx == 20)
        {
            printf("[call]vMGSenserIsSupportInCb\n");
        }
        else if (idx == 21)
        {
            printf("[call]vMSysHandler\n");
        }
        else if (idx == 22)
        {
            printf("[call]vMGetKeyNum\n");
            tmp1 = 33;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 23)
        {
            printf("[call]vMIsSupportTP\n");
        }
        else if (idx == 24)
        {
            printf("[call]CDownGetCompany\n");
        }
        else if (idx == 25)
        {
            // ignore
            // printf("[call]CDownGetServicePhone\n");
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
        }
        else if (idx == 29)
        {
            printf("[call]CDownGetData\n");
        }
        else if (idx == 30)
        {
            // printf("[call]GetCoolBarKernelCurrentVersion(返回46)\n");
            tmp1 = 46;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 31)
        {
            printf("[call]CoolBar_DownLoad_GetFile\n");
        }
        else if (idx == 32)
        {
            printf("[call]CoolBar_DownLoad_Stop\n");
        }
        else if (idx == 33)
        {
            printf("[call]vmDlGetCurrAppId\n");
        }
        else if (idx == 34)
        {
            printf("[call]vmSysIsVisibleApp\n");
        }
        else if (idx == 35)
        {
            printf("[call]cDownSetForceUpdate\n");
        }
        else if (idx == 36)
        {
            printf("[call]cDownGetModeAndVersion\n");
        }
        else if (idx == 37)
        {
            printf("[call]mmiDynamicSetForceUpdate\n");
        }
        else if (idx == 38)
        {
            printf("[call]mmiDunamicGetModeAndVersion\n");
        }
        else if (idx == 39)
        {
            printf("[call]vMSwitchLog\n");
        }
        else if (idx == 40)
        {
            printf("[call]Coolbar_GetResStatus\n");
        }
        else if (idx == 41)
        {
            printf("[call]coolbar_Update_Tfold\n");
        }
        else if (idx == 42)
        {
            printf("[call]CoolBar_EnterDmIn\n");
        }
        else if (idx == 43)
        {
            printf("[call]vmInputText\n");
        }
        else if (idx == 44)
        {
            printf("[call]vmInputPassword\n");
        }
        else if (idx == 45)
        {
            printf("[call]vmInputClose\n");
        }
        else if (idx == 46)
        {
            printf("[call]vmInputIsOpen\n");
        }
        else if (idx == 47)
        {
            printf("[call]VmGetRand\n");
        }
        else if (idx == 48)
        {
            printf("[call]srand\n");
        }
        else if (idx == 49)
        {
            printf("[call]VmGetSmsCenterNum\n");
        }
        else if (idx == 50)
        {
            printf("[call]VmGetPrjCustom\n");
        }
        else if (idx == 51)
        {
            printf("[call]CBGetNetInfo\n");
        }
        else if (idx == 52)
        {
            printf("[call]VmGetCbNum\n");
        }
        else if (idx == 53)
        {
            printf("[call]LzssEncode\n");
        }
        else if (idx == 54)
        {
            printf("[call]LzssDecode\n");
        }
        else if (idx == 55)
        {
            printf("[call]DMenuUpdateMenu\n");
        }
        else if (idx == 56)
        {
            printf("[call]CDownUpdateMenu\n");
        }
        else if (idx == 57)
        {
            printf("[call]CbGetPlatfomName\n");
        }
        else if (idx == 58)
        {
            printf("[call]vMInnerAppInfo\n");
        }
        else if (idx == 59)
        {
            printf("[call]vMGetInnerAppIcon\n");
        }
        else if (idx == 60)
        {
            printf("[call]CDownGetAppType\n");
        }
        else if (idx == 61)
        {
            printf("[call]vmSetGQQRunings\n");
        }
        else if (idx == 62)
        {
            printf("[call]p_vmGetGQQRunings\n");
        }
        else if (idx == 63)
        {
            printf("[call]vmGetQQAddress\n");
        }
        else if (idx == 64)
        {
            printf("[call]GetCurrentScreenType\n");
        }
        else if (idx == 65)
        {
            printf("[call]GetCurrentPlatformType\n");
        }
        else if (idx == 66)
        {
            printf("[call]VmDlGetIMEI\n");
        }
        else if (idx == 68)
        {
            printf("[call]Net_get_DefaultLoginInfo\n");
        }
        else if (idx == 69)
        {
            printf("[call]Net_write_DefaultLoginInfo\n");
        }
        else if (idx == 70)
        {
            printf("[call]coolbar_GetAppNameByIdFromList\n");
        }
        else if (idx == 71)
        {
            printf("[call]coolbar_Update_DeleteAppInfo\n");
        }
        else if (idx == 72)
        {
            printf("[call]VmGetDMenuFileName\n");
        }
        else if (idx == 73)
        {
            printf("[call]VmGetCDownFileName\n");
        }
        else if (idx == 74)
        {
            printf("[call]GetCDownAppUrl\n");
        }
        else if (idx == 75)
        {
            printf("[call]vmDlGetPreAppId\n");
        }
        else if (idx == 76)
        {
            printf("[call]Coolbar_ParseDownDataFile\n");
        }
        else if (idx == 77)
        {
            printf("[call]CoolBar_DownLoad_CBE\n");
        }
        else if (idx == 78)
        {
            printf("[call]Coolbar_PreLoadAppEx\n");
        }
        else if (idx == 79)
        {
            printf("[call]vmDlGet_sp_bf\n");
        }
        else if (idx == 80)
        {
            // todo
            // printf("[call]vMGetGameWinState\n");
            tmp2 = 1; // pause remuse stop
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp2);
        }
        else if (idx == 81)
        {
            printf("[call]CDownGetHideText\n");
        }
        else if (idx == 82)
        {
            printf("[call]vMPhbMultiSelectEntry\n");
        }
        else if (idx == 83)
        {
            printf("[call]vmSetCurActiveSim\n");
        }
        else if (idx == 84)
        {
            printf("[call]vmGetAllSimStatus\n");
        }
        else if (idx == 85)
        {
            printf("[call]VmSendMMS\n");
        }
        else if (idx == 86)
        {
            printf("[call]VmGetFocusWinID\n");
        }
        else if (idx == 87)
        {
            printf("[call]VmIsWinOpen\n");
        }
        else if (idx == 88)
        {
            printf("[call]vmIsIdleWinFocus\n");
        }
        else if (idx == 89)
        {
            printf("[call]vmIsInnerApp\n");
            tmp1 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 90)
        {
            printf("[call]vmGetInnerAppVer\n");
        }
        else if (idx == 91)
        {
            printf("[call]VmGetMixMenuLength\n");
        }
        else if (idx == 92)
        {
            printf("[call]VmGetMixMenudata\n");
        }
        else if (idx == 93)
        {
            printf("[call]VmGetWpayCBMInfo\n");
        }
        else if (idx == 94)
        {
            printf("[call]VMGetCurVerAllInfo\n");
        }
        else if (idx == 95)
        {
            printf("[call]vMSetFpsSleepFlag\n");
        }
        else if (idx == 96)
        {
            printf("[call]cbSetSmsCenterNum\n");
        }
        else if (idx == 97)
        {
            printf("[call]vmGetBuildTime\n");
        }
        else if (idx == 98)
        {
            printf("[call]vmGetUsedTimes\n");
        }
        else if (idx == 99)
        {
            printf("[call]vmSysGetBatteryInfo\n");
        }
        else if (idx == 100)
        {
            printf("[call]vmSysSetLcdBright\n");
        }
        else if (idx == 101)
        {
            printf("[call]vmSysResetLcdBright\n");
        }
        else if (idx == 102)
        {
            printf("[call]vmSysSetPowerSaveMode\n");
        }
        else if (idx == 103)
        {
            printf("[call]vMGetOperatorMCC\n");
        }
        else if (idx == 104)
        {
            printf("[call]vMGetOperatorMNC\n");
        }
        else if (idx == 105)
        {
            printf("[call]VmSupportAppSotre\n");
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
        }
        else if (idx == 108)
        {
            printf("[call]vmSysStartVibration\n");
        }
        else if (idx == 109)
        {
            printf("[call]vmSysStopVibration\n");
        }
        else if (idx == 110)
        {
            printf("[call]vmSysOpenBrowser\n");
        }
        else if (idx == 111)
        {
            printf("[call]vmSysSetSettingProfile\n");
        }
        else if (idx == 112)
        {
            printf("[call]vmSysGetSettingProfile\n");
        }
        else if (idx == 113)
        {
            printf("[call]vmSysGetSettingProfileName\n");
        }
        else if (idx == 114)
        {
            printf("[call]vmSysSaveContactPerson\n");
        }
        else
        {

            printf("[impl]vmManagerSys调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS && address < (VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS) / 4;
        if (idx == 0)
        {
            printf("[call]DF_InitMemory\n");
        }
        else if (idx == 1)
        {
            printf("[call]DF_ReleaseMemory\n");
        }
        else if (idx == 2)
        {
            // 参数1申请的内存块地址，参数2申请的内存大小，返回1
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            // printf("[call]DF_Malloc_IN(%x,%x)\n", tmp1, tmp2);
            tmp3 = vm_malloc(tmp2);
            uc_mem_write(MTK, tmp1, &tmp3, 4);
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 3)
        {
            // printf("[call]DF_Free\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_mem_read(MTK, tmp1, &tmp2, 4);
            vm_free(tmp2);
            tmp2 = 0;
            uc_mem_write(MTK, tmp1, &tmp2, 4);
        }
        else if (idx == 4)
        {
            printf("[call]DF_Memory_gc\n");
        }
        else if (idx == 5)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // int* p_g_memoryBlock
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // size
            // printf("[call]initMemoryBlock(%x,%x)\n", tmp1, tmp2);
            vm_initMemoryBlock(tmp1, tmp2);
        }
        else if (idx == 6)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_MF_MemoryBlock_Malloc(tmp1, tmp2);
            // printf("[call]MF_MemoryBlock_Malloc(%x,%x)\n", tmp1, tmp2);
        }
        else if (idx == 7)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_MF_MemoryBlock_Reset(tmp1);
            printf("[call]MF_MemoryBlock_Reset\n");
        }
        else if (idx == 8)
        {
            // printf("[call]getMemoryBlockPtr\n");
            tmp1 = VM_MemoryBlock_PTR_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 9)
        {
            // todo
            // printf("[call]MF_InitGmemoryBlock\n");
            vm_initMemoryBlock();
        }
        else if (idx == 10)
        {
            // todo
            // printf("[call]MF_ReleaseGmemoryBlock\n");
            vm_MF_resetGmemoryBlock();
        }
        else if (idx == 11)
        {
            // todo
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // size
            // printf("[call]MF_resetGmemoryBlock\n");
            vm_MF_resetGmemoryBlock(tmp1);
        }
        else if (idx == 12)
        {
            // todo
            // printf("[call]MF_MallocGmemoryBlock\n");
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp3);
            tmp1 = VM_DreamFactory_MemoryBlock_ADDRESS;
            uc_mem_read(MTK, tmp1, &tmp2, 4);
            vm_MF_MemoryBlock_Malloc(tmp2, tmp3);
        }
        else if (idx == 13)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // size
            tmp2 = vm_malloc(tmp1);
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
            // printf("[call]GAME_Image_malloc\n");
        }
        else if (idx == 14)
        {
            // printf("[call]GAME_Image_free\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_free(tmp1);
            tmp1 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 15)
        {
            printf("[call]DF_Memory_AttachPointer\n");
        }
        else if (idx == 16)
        {
            printf("[call]MF_MemoryBlock_Release\n");
        }
        else if (idx == 17)
        {
            printf("[call]DF_InitMemoryEx\n");
        }
        else if (idx == 18)
        {
            printf("[call]GAME_Image_realloc\n");
        }
        else if (idx == 19)
        {
            printf("[call]DF_Malloc_debug\n");
        }
        else if (idx == 20)
        {
            printf("[call]mallocBigMen_debug\n");
        }
        else if (idx == 21)
        {
            printf("[call]CoolbarGetshareMemAlloced\n");
        }
        else if (idx == 82)
        {
            printf("[call]xx???\n");
        }
        else
        {
            printf("[impl]vmMemManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_LCD_FUNC_LIST_ADDRESS && address < (VM_MANAGER_LCD_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_LCD_FUNC_LIST_ADDRESS) / 4;
        idx += 1;
        if (idx == 1)
        {
            // printf("[call]vMGetCurrMainScreenImage\n");
            tmp1 = VM_screenImageStruct_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 2)
        {
            // printf("[call]vMGetLCDBuffer\n");
            tmp1 = VM_screenImage_ADDRESS;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 3)
        {
            printf("[call]vM_InvalidateLcd\n");
        }
        else if (idx == 4)
        {
            printf("[call]vMGetCurrFontType\n");
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 5)
        {
            // todo
            printf("[call]vMSetCurrFontType\n");
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 6)
        {
            // printf("[call]vMGetFontWidth\n");
            tmp1 = getFontWidth();
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 7)
        {
            tmp1 = getFontHeight();
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            // printf("[call]vMGetFontHeight\n");
        }
        else if (idx == 8)
        {
            vm_readStringGbkByReg(UC_ARM_REG_R0, cbeTextString);
            tmp1 = mesureStringWidth(cbeTextString);
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            // gbk_to_utf8(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
            // printf("[call]vMGetStringWidth(%d,%x)\n", tmp1, cbeTextString[0]);
            // tmp1 = mesureStringWidth(cbeTextString);
        }
        else if (idx == 9)
        {
            // todo
            // printf("[call]vMGetStringHeight\n");
            tmp1 = 18;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 10)
        {
            // todo
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            // uc_mem_read(MTK, tmp1, cbeTextString, 16);
            printf("[call]vMDrawString()\n");
            assert(0);
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
            // printf("[call]vMDrawStringEx(%d,%d,%s)\n", tmp2, tmp3, sprintfBuff);

            drawFontString(cbeTextString, tmp2, tmp3, color);
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 12)
        {
            printf("[call]vMDrawStringClipAlign\n");
        }
        else if (idx == 13)
        {
            printf("[call]vMDrawStringClip\n");
        }
        else if (idx == 14)
        {
            printf("[call]vMDrawStringRect\n");
        }
        else if (idx == 15)
        {
            printf("[call]vMDrawLineEx\n");
        }
        else if (idx == 16)
        {
            printf("[call]vMDrawLine\n");
        }
        else if (idx == 17)
        {
            printf("[call]vMDrawRect\n");
        }
        else if (idx == 18)
        {
            printf("[call]vMDrawRectEx\n");
        }
        else if (idx == 19)
        {
            printf("[call]vMFillRect\n");
        }
        else if (idx == 20)
        {
            printf("[call]vMFillRectEx\n");
        }
        else if (idx == 21)
        {
            printf("[call]vMFillRectWithImage\n");
        }
        else if (idx == 22)
        {
            printf("[call]vMFillRectWithImageEx\n");
        }
        else if (idx == 23)
        {
            printf("[call]vMCreateImage\n");
        }
        else if (idx == 24)
        {
            printf("[call]vMCreateImageFromInRes\n");
        }
        else if (idx == 25)
        {
            // vMDrawImageWithClipEx(p_mscreenImage ,ptr2 ,0:x? ,0:y? ,0xbc:x2 ,1:y2? ,0 ,3)
            // vMDrawImageWithClipEx(dst, src, sx, sy, w, h, dx, dy)原图的sx,sy，目标图的dx,dy
            // printf("[call]vMDrawImageWithClipEx\n");
            vM_DrawImageWithClipEx();
        }
        else if (idx == 26)
        {
            // printf("[call]vMDrawImageClipAndAlphaEx\n");
            vm_vMDrawImageClipAndAlphaEx();
        }
        else if (idx == 27)
        {
            printf("[call]vMDrawImage\n");
        }
        else if (idx == 28)
        {
            printf("[call]vMDrawImageEx\n");
        }
        else if (idx == 29)
        {
            printf("[call]vMDrawImageWithAlpha\n");
        }
        else if (idx == 30)
        {
            printf("[call]vMDrawImageWithClip\n");
        }
        else if (idx == 31)
        {
            printf("[call]vMDrawImageWithClip2\n");
        }
        else if (idx == 32)
        {
            printf("[call]vMDrawImageClipAndAlpha\n");
        }
        else if (idx == 33)
        {
            printf("[call]vMDrawImageClipAndAlpha2\n");
        }
        else if (idx == 34)
        {
            printf("[call]vMGetImageWidth\n");
        }
        else if (idx == 35)
        {
            printf("[call]vMGetImageHeight\n");
        }
        else if (idx == 36)
        {
            printf("[call]vMDestoryImage\n");
        }
        else if (idx == 37)
        {
            // printf("[call]vMIsBacklightOn\n");
            tmp2 = 1;
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp2);
        }
        else if (idx == 38)
        {
            printf("[call]vMCtrlBacklight\n");
            tmp2 = 1;
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp2);
        }
        else if (idx == 39)
        {
            printf("[call]vMGB2UCS2\n");
            vm_readStringUCS2ByReg(UC_ARM_REG_R0, cbeTextString);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            tmp1 = strlen_utf16(cbeTextString); // fixme gbk?ucs2?
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            gbk_to_unicode(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
            uc_mem_write(MTK, tmp2, sprintfBuff, strlen_gbk(sprintfBuff));
        }
        else if (idx == 40)
        {
            printf("[call]vMUCS2GB\n");
        }
        else if (idx == 41)
        {
            printf("[call]vMWinPointIsInRect\n");
        }
        else if (idx == 42)
        {
            printf("[call]vmResGetTxtWithDataPackage\n");
        }
        else if (idx == 43)
        {
            printf("[call]vmResGetDefTxt\n");
        }
        else if (idx == 44)
        {
            printf("[call]vmResGetTxtForGame\n");
        }
        else if (idx == 45)
        {
            printf("[call]IMG_InitDataPage\n");
        }
        else if (idx == 46)
        {
            printf("[call]IMG_InitInnerDataPageEx\n");
        }
        else if (idx == 47)
        {
            printf("[call]IMG_ReleaseDataPage\n");
        }
        else if (idx == 48)
        {
            printf("[call]IMG_InitDataPageEx\n");
        }
        else if (idx == 49)
        {
            printf("[call]IMG_CreateImageFormIdEx\n");
        }
        else if (idx == 50)
        {
            // printf("[call]IMG_CreateImageFormStream\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_IMG_CreateImageFormStream(tmp1, tmp2);
        }
        else if (idx == 51)
        {
            printf("[call]vMDrawStandardImage\n");
        }
        else if (idx == 52)
        {
            printf("[call]vMGetStandardImageDimension\n");
        }
        else if (idx == 53)
        {
            printf("[call]vMGetStandardImageType\n");
        }
        else if (idx == 54)
        {
            printf("[call]vMDrawStandardImageEx\n");
        }
        else if (idx == 55)
        {
            printf("[call]vMDrawStringBorder\n");
        }
        else if (idx == 56)
        {
            printf("[call]vMDrawStringClipAlignBorder\n");
        }
        else if (idx == 57)
        {
            printf("[call]vMDrawStringClipBorder\n");
        }
        else if (idx == 58)
        {
            printf("[call]IMG_InitDataPageTxt\n");
        }
        else if (idx == 59)
        {
            printf("[call]gddiAllocMemory\n");
        }
        else if (idx == 60)
        {
            printf("[call]gddiFreeMemory\n");
        }
        else if (idx == 61)
        {
            printf("[call]gddiImageData\n");
        }
        else if (idx == 62)
        {
            printf("[call]gddiRegImageCodecHandler\n");
        }
        else if (idx == 63)
        {
            printf("[call]vMGetCharWidth\n");
        }
        else if (idx == 64)
        {
            printf("[call]vMDrawUcs2String\n");
        }
        else if (idx == 65)
        {
            printf("[call]vMDrawUcs2StringBorder\n");
        }
        else if (idx == 66)
        {
            printf("[call]vMDrawUcs2StringEx\n");
        }
        else if (idx == 67)
        {
            printf("[call]vMDrawUcs2StringClipAlignBorder\n");
        }
        else if (idx == 68)
        {
            printf("[call]vMDrawUcs2StringClipAlign\n");
        }
        else if (idx == 69)
        {
            printf("[call]vMDrawUcs2StringClipBorder\n");
        }
        else if (idx == 70)
        {
            printf("[call]vMDrawUcs2StringClip\n");
        }
        else if (idx == 71)
        {
            printf("[call]vMDrawUcs2StringRect\n");
        }
        else if (idx == 72)
        {
            printf("[call]vMGetUcs2StringWidth\n");
        }
        else if (idx == 73)
        {
            printf("[call]vMGetUcs2StringHeight\n");
        }
        else if (idx == 74)
        {
            printf("[call]vMAllowBackLight\n");
        }
        else if (idx == 75)
        {
            printf("[call]vM_CB_GetIsNeedRefreshLcd\n");
        }
        else if (idx == 76)
        {
            printf("[call]vM_CB_SetIsNeedRefreshLcd\n");
        }
        else if (idx == 77)
        {
            printf("[call]vM_CB_LCD_InvalidateRect_Enable\n");
        }
        else if (idx == 78)
        {
            printf("[call]vM_CB_SetVideoIsNeedClosed\n");
        }
        else if (idx == 79)
        {
            printf("[call]vM_CB_GetVideoIsNeedClosed\n");
        }
        else if (idx == 80)
        {
            printf("[call]vMDrawUcs2StringRectEx\n");
        }

        // result 区（从 idx=81 开始）
        else if (idx == 81)
        {
            printf("[call]vMSetCbeFontDataPtr\n");
        }
        else if (idx == 82)
        {
            printf("[call]vMGetFontHeightEx\n");
        }
        else if (idx == 83)
        {
            printf("[call]vMGetFontWidthEx\n");
        }
        else if (idx == 84)
        {
            printf("[call]vMGetStringHeightEx\n");
        }
        else if (idx == 85)
        {
            printf("[call]vMGetStringWidthEx\n");
        }
        else if (idx == 86)
        {
            printf("[call]vMShowStringEx\n");
        }
        else if (idx == 87)
        {
            printf("[call]vMShowString\n");
        }
        else if (idx == 88)
        {
            printf("[call]vMShowStringClipAlign\n");
        }
        else if (idx == 89)
        {
            printf("[call]vMShowStringClip\n");
        }
        else if (idx == 90)
        {
            printf("[call]vMShowStringRect\n");
        }
        else if (idx == 91)
        {
            printf("[call]vM_InvalidateLcdEx\n");
        }
        else if (idx == 92)
        {
            printf("[call]vMSetFontSize\n");
        }
        else if (idx == 93)
        {
            printf("[call]vMResetFontSize\n");
        }
        else if (idx == 94)
        {
            printf("[call]vMUTF82UCS2\n");
        }
        else if (idx == 95)
        {
            printf("[call]vMUCS2UTF8\n");
        }
        else
        {
            printf("[impl]vmLcdManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS) / 4;
        idx += 1;
        if (idx == 1)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_write(MTK, UC_ARM_REG_R2, &tmp3);
            vm_cbfs_vm_file_open(tmp1, tmp2, tmp3);
            // printf("[call]cbfs_vm_file_open\n");
        }
        else if (idx == 2)
        {
            // printf("[call]cbfs_vm_file_close\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_cbfs_vm_file_close(tmp1);
        }
        else if (idx == 3)
        {
            printf("[call]vm_file_exist\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_cbfs_vm_file_exists(tmp1, tmp2);
        }
        else if (idx == 4)
        {
            printf("[call]vm_file_direxist\n");
        }
        else if (idx == 5)
        {
            printf("[call]cbfs_vm_file_read\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_write(MTK, UC_ARM_REG_R2, &tmp3);
            vm_cbfs_vm_file_read(tmp1, tmp2, tmp3);
        }
        else if (idx == 6)
        {
            printf("[call]cbfs_vm_file_write\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_write(MTK, UC_ARM_REG_R2, &tmp3);
            vm_cbfs_vm_file_write(tmp1, tmp2, tmp3);
        }
        else if (idx == 7)
        {
            printf("[call]cbfs_vm_file_seek\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_write(MTK, UC_ARM_REG_R2, &tmp3);
            vm_cbfs_vm_file_seek(tmp1, tmp2, tmp3);
        }
        else if (idx == 8)
        {
            printf("[call]cbfs_vm_file_tell\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_cbfs_vm_file_tell(tmp1);
        }
        else if (idx == 9)
        {
            printf("[call]cbfs_vm_file_getfilesize\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_cbfs_vm_file_getfilesize(tmp1);
        }
        else if (idx == 10)
        {
            printf("[call]cbfs_vm_file_delete\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_cbfs_vm_file_delete(tmp1, tmp2);
        }
        else if (idx == 11)
        {
            printf("[call]cbfs_vm_file_rename\n");
        }
        else if (idx == 12)
        {
            printf("[call]cbfs_vm_file_mkdir\n");
        }
        else if (idx == 13)
        {
            printf("[call]cbfs_vm_file_rmdir\n");
        }
        else if (idx == 14)
        {
            printf("[call]cbfs_vm_find_first\n");
        }
        else if (idx == 15)
        {
            printf("[call]cbfs_vm_find_next\n");
        }
        else if (idx == 16)
        {
            printf("[call]cbfs_vm_find_close\n");
        }
        else if (idx == 17)
        {
            printf("[call]vm_get_freespace\n");
        }
        else if (idx == 18)
        {
            printf("[call]vm_get_sdcardStatus\n");
        }
        else if (idx == 19)
        {
            printf("[call]vm_GetFilenameFromPath\n");
        }
        else if (idx == 20)
        {
            printf("[call]vm_file_getMp3Dir\n");
        }
        else if (idx == 21)
        {
            printf("[call]vm_file_getMp4Dir\n");
        }
        else if (idx == 22)
        {
            printf("[call]vm_file_getPicDir\n");
        }
        else if (idx == 23)
        {
            printf("[call]vm_file_getBookDir\n");
        }
        else if (idx == 24)
        {
            printf("[call]vm_file_truncate\n");
        }
        else if (idx == 25)
        {
            printf("[call]vm_file_RdWt\n");
        }
        else if (idx == 26)
        {
            printf("[call]vm_file_getSysDir\n");
        }
        else if (idx == 27)
        {
            printf("[call]vm_fmgr_select_entry\n");
        }
        else if (idx == 28)
        {
            printf("[call]vm_get_freespace_ex\n");
        }
        else if (idx == 29)
        {
            printf("[call]vm_get_sdcardStatusEx\n");
        }
        else if (idx == 30)
        {
            printf("[call]vm_get_fullname\n");
        }

        else
        {
            printf("[impl]vmFileIoManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_STDIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_STDIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_STDIO_FUNC_LIST_ADDRESS) / 4;
        idx += 1;
        if (idx == 1)
        {
            printf("[call]memcpy\n");
        }
        else if (idx == 2)
        {
            vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
            tmp1 = strlen(cbeTextString);
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            // printf("[call]strlen\n");
        }
        else if (idx == 3)
        {
            printf("[call]memset\n");
        }
        else if (idx == 4)
        {
            // printf("[call]sprintf(%x,%x,%x)\n", tmp1, tmp2, tmp3);
            vm_sprintf_return_buffer();
        }
        else if (idx == 5)
        {
            printf("[call]vm_log_trace\n");
        }
        else if (idx == 6)
        {
            // printf("[call]VmGetRand\n");
            tmp1 = currentTime;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 7)
        {
            printf("[call]vsprintf\n");
        }
        else if (idx == 8)
        {
            printf("[call]strncpy\n");
        }
        else if (idx == 9)
        {
            // printf("[call]strcpy\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_strcpy(tmp1, tmp2);
        }
        else if (idx == 10)
        {
            printf("[call]strcat\n");
        }
        else if (idx == 11)
        {
            printf("[call]atol\n");
        }
        else if (idx == 12)
        {
            printf("[call]memmove\n");
        }
        else if (idx == 13)
        {
            printf("[call]atoi\n");
        }
        else if (idx == 14)
        {
            printf("[call]vMpow\n");
        }
        else if (idx == 15)
        {
            printf("[call]strcmp\n");
        }
        else if (idx == 16)
        {
            printf("[call]memcmp\n");
        }
        else if (idx == 17)
        {
            printf("[call]strncmp\n");
        }
        else if (idx == 18)
        {
            printf("[call]setjmp\n");
        }
        else if (idx == 19)
        {
            printf("[call]longjmp\n");
        }
        else if (idx == 20)
        {
            printf("[call]atof\n");
        }
        else if (idx == 21)
        {
            printf("[call]vMstricmp\n");
        }
        else if (idx == 22)
        {
            printf("[call]vMstrnicmp\n");
        }
        else
        {
            printf("[impl]vmStdIoManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_TIMER_FUNC_LIST_ADDRESS && address < (VM_MANAGER_TIMER_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_TIMER_FUNC_LIST_ADDRESS) / 4;
        if (idx == 0)
        {
            printf("[call]vMStartTimer\n");
        }
        else if (idx == 1)
        {
            printf("[call]vMStopTimer\n");
        }
        else if (idx == 2)
        {
            printf("[call]vMGetTickCount\n");
            // 返回tickCount
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 3)
        {
            // 返回tickCount
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
            printf("[call]vMGetTotalSeconds\n");
        }
        else if (idx == 4)
        {
            // 返回tickCount
            tmp1 = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);

            printf("[call]vMGetCurrentTime\n");
        }
        else if (idx == 5)
        {
            printf("[call]vMSysSleep\n");
        }
        else if (idx == 6)
        {
            printf("[call]VmSetSysTime\n");
        }
        else if (idx == 7)
        {
            printf("[call]VmSetSysDate\n");
        }
        else if (idx == 8)
        {
            printf("[call]vMIncreaseTime\n");
        }
        else if (idx == 9)
        {
            printf("[call]vMDecreaseTime\n");
        }
        else
        {
            printf("[impl]vmTimerManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_CTRL_FUNC_LIST_ADDRESS && address < (VM_MANAGER_CTRL_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_CTRL_FUNC_LIST_ADDRESS) / 4;
        if (idx == -1)
        {
            printf("[call]\n");
        }
        else
        {
            printf("[impl]vmCtrlManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS && address < (VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS) / 4;
        if (idx == -1)
        {
            printf("[call]\n");
        }
        else
        {
            printf("[impl]vmNetWorkManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS && address < (VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS) / 4;
        idx += 1;
        if (idx == 2)
        {
            printf("[call]CdRectPoint\n");
        }
        else if (idx == 9)
        {
            printf("[call]Sqrt\n");
        }
        else if (idx == 10)
        {
            printf("[call]DF_SetDataPackage\n");
            assert(0);
        }
        else if (idx == 11)
        {
            printf("[call]DF_GetDataPackage\n");
            assert(0);
        }
        else if (idx == 12)
        {
            printf("[call]DF_GetResourceByResourceID\n");
            assert(0);
        }
        else if (idx == 13)
        {
            printf("[call]DF_GetResourceByFileName\n");
            assert(0);
        }
        else if (idx == 14)
        {
            printf("[call]DF_GetResourceNameByID\n");
            assert(0);
        }
        else if (idx == 15)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_DF_GetResourceIDByFileName(tmp1);
            // printf("[call]DF_GetResourceIDByFileName(%x)\n", tmp1);
        }
        else if (idx == 16)
        {
            // printf("[call]DF_GetTResource\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_DF_GetTResource(tmp1);
        }
        else if (idx == 17)
        {
            // printf("[call]DF_GetStreamTResource\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_DF_GetStreamTResource(tmp1);
        }
        else if (idx == 18)
        {
            printf("[call]DF_DataPackage_ShowFileList\n");
            assert(0);
        }
        else if (idx == 19)
        {
            // printf("[call]DF_String_Equal\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_DF_String_Equal(tmp1, tmp2);
        }
        else if (idx == 20)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            tmp1 = vm_DF_ReadShort(tmp1, tmp2);
            // printf("[call]DF_ReadShort(%x)\n", tmp1);
        }
        else if (idx == 21)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            tmp1 = vm_DF_ReadInt(tmp1, tmp2);
            // printf("[call]DF_ReadInt(%x)\n", tmp1);
        }
        else if (idx == 22)
        {
            printf("[call]DF_File_ReadShort\n");
        }
        else if (idx == 23)
        {
            printf("[call]DF_File_ReadInt\n");
        }
        else if (idx == 24)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            vm_DF_WriteShort(tmp1, tmp2, tmp3);
            // printf("[call]DF_WriteShort\n");
        }
        else if (idx == 25)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            vm_DF_WriteInt(tmp1, tmp2, tmp3);
            // printf("[call]DF_WriteInt\n");
        }
        else if (idx == 26)
        {
            // printf("[call]DF_ReadString\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_DF_ReadString(tmp1, tmp2);
        }
        else if (idx == 27)
        {
            printf("[call]DF_ReadStringEx\n");
        }
        else if (idx == 28)
        {
            printf("[call]DF_File_ReadString\n");
        }
        else if (idx == 29)
        {
            printf("[call]DF_File_ReadToBuffer\n");
        }
        else if (idx == 30)
        {
            printf("[call]DF_ReadString2\n");
        }
        else if (idx == 31)
        {
            // printf("[call]DF_GetMemoryBlock\n");
            vm_DF_GetMemoryBlock();
        }
        else if (idx == 32)
        {
            printf("[call]DF_Sin\n");
        }

        // result 区
        else if (idx == 33)
        {
            printf("[call]DF_Cos\n");
        }
        else if (idx == 34)
        {
            printf("[call]DF_Degree\n");
        }
        else if (idx == 35)
        {
            printf("[call]DF_CollectionTest\n");
        }
        else if (idx == 36)
        {
            printf("[call]DF_SwapVal\n");
        }
        else if (idx == 37)
        {
            printf("[call]DF_GetFormatString\n");
            // 返回一个字符串格式化后的内存地址
            // return DreamFactoryCharBuffer;
            vm_sprintf_return_buffer();
            // vm_DF_GetFormatString();//
        }
        else if (idx == 38)
        {
            // printf("[call]Storage_Date\n");
            // todo
            tmp1 = currentTime;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 39)
        {
            printf("[call]vMstricmp\n");
        }
        else if (idx == 40)
        {
            printf("[call]vMstrnicmp\n");
        }
        else
        {
            printf("[impl]vmGameUtilManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    // df
    else if (address >= VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS && address < (VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS) / 4;
        if (1)
        {
            printf("[impl]vmDfEngineManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
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
            printf("[call]BILLING_GetPayNumByAppId(%x,%x)[%x]\n", tmp1, tmp2, lastAddress);
            tmp1 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 2)
        {
            printf("[call]BILLING_GetRemainDay\n");
        }
        else if (idx == 3)
        {
            printf("[call]BILLING_Pay\n");
        }
        else if (idx == 4)
        {
            printf("[call]BILLING_PayMoreTimes\n");
        }
        else if (idx == 5)
        {
            printf("[call]BILLING_IsRegisterBillingInfo\n");
        }
        else if (idx == 6)
        {
            printf("[call]BILLING_RegisterBillingInfo\n");
        }
        else if (idx == 7)
        {
            printf("[call]BILLING_SetBillingStatus\n");
        }
        else if (idx == 8)
        {
            printf("[call]BILLING_GetBillingStatus\n");
        }
        else if (idx == 9)
        {
            printf("[call]BILLING_IsNeedPay\n");
        }
        else if (idx == 10)
        {
            printf("[call]BILLING_IsInTryStatus\n");
        }
        else if (idx == 11)
        {
            printf("[call]BIllING_OpenBillingPromptWin\n");
        }
        else if (idx == 12)
        {
            printf("[call]CDownGetTryDay\n");
        }
        else if (idx == 13)
        {
            printf("[call]BILLING_PayForCBB\n");
        }
        else if (idx == 14)
        {
            printf("[call]BILLING_PayForPwd\n");
        }
        else if (idx == 15)
        {
            printf("[call]CDownGetOption5\n");
        }
        else if (idx == 16)
        {
            printf("[call]Billing_SendSpecSms\n");
        }
        else if (idx == 17)
        {
            printf("[call]Billing_CancelSms\n");
        }
        else if (idx == 18)
        {
            printf("[call]CDownIsMonthApp\n");
        }
        else if (idx == 19)
        {
            printf("[call]CDownGetFileNameByAppID\n");
        }
        else if (idx == 20)
        {
            printf("[call]CDownGetPayTimes\n");
        }
        else if (idx == 21)
        {
            printf("[call]BILLING_NewMonthPay\n");
        }
        else if (idx == 22)
        {
            printf("[call]BILLING_NewMonthCancel\n");
        }
        else if (idx == 23)
        {
            printf("[call]BILLING_CleanAppMonthBillInfo\n");
        }
        else if (idx == 24)
        {
            printf("[call]BILLING_GetValidDayByAppId\n");
        }
        else if (idx == 25)
        {
            printf("[call]CDownGetBillSmsAddr\n");
        }
        else if (idx == 26)
        {
            printf("[call]CDownGetBillSmsSuf\n");
        }
        else if (idx == 27)
        {
            printf("[call]BILLING_Pay2\n");
        }
        else if (idx == 28)
        {
            printf("[call]Billing_GetAppUsedStatus\n");
        }
        else if (idx == 29)
        {
            printf("[call]Billing_SetAppUsedStatus\n");
        }
        else if (idx == 30)
        {
            printf("[call]CDownGetPayTipContent\n");
        }
        else if (idx == 31)
        {
            printf("[call]BILLING_Pay3\n");
        }
        else if (idx == 32)
        {
            printf("[call]BILLING_SendRegisterSms\n");
        }

        // result 区
        else if (idx == 33)
        {
            printf("[call]CDownGetOption8\n");
        }
        else if (idx == 34)
        {
            printf("[call]CDownGetOption9\n");
        }
        else if (idx == 35)
        {
            printf("[call]CDownGetOption10\n");
        }
        else if (idx == 36)
        {
            printf("[call]BILLING_Register\n");
        }
        else if (idx == 37)
        {
            printf("[call]BILLING_PayForCBB3\n");
        }
        else if (idx == 38)
        {
            printf("[call]CDownIsWPay\n");
        }
        else
        {
            printf("[impl]vmBillingManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }

    // ucs2
    else if (address >= VM_MANAGER_UCS2_FUNC_LIST_ADDRESS && address < (VM_MANAGER_UCS2_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_UCS2_FUNC_LIST_ADDRESS) / 4;
        idx += 1;
        if (idx == 1)
        {
            printf("[call]vmutStrlenUcs2\n");
            vm_readStringUCS2ByReg(UC_ARM_REG_R0, cbeTextString);
            tmp1 = strlen_utf16(cbeTextString); // fixme gbk?ucs2?
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 2)
        {
            printf("[call]vmutStrcpyUcs2\n");
        }
        else if (idx == 3)
        {
            printf("[call]vmutStrcatUcs2\n");
        }
        else if (idx == 4)
        {
            printf("[call]vmutExpandStrcpy\n");
        }
        else if (idx == 5)
        {
            printf("[call]vmutStrncpyUcs2\n");
        }
        else if (idx == 6)
        {
            printf("[call]vmutExpandStrncpy\n");
        }
        else if (idx == 7)
        {
            printf("[call]vmutExpandMemcpy\n");
        }
        else if (idx == 8)
        {
            printf("[call]vmutStrchrUcs2\n");
        }
        else if (idx == 9)
        {
            printf("[call]vmutStrcmpUcs2\n");
        }
        else if (idx == 10)
        {
            printf("[call]vmutStricmpUcs2\n");
        }
        else if (idx == 11)
        {
            printf("[call]vmutStrncmpUcs2\n");
        }
        else
        {
            printf("[impl]vmUCS2StrManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    //
    // df scr
    else if (address >= VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS && address < (VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS) / 4;
        if (idx == 0)
        {
            tmp1 = 0;
            printf("[impl]vmDfScript[0]\n", idx);
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else
        {
            printf("[impl]vmDfScriptManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
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
            // printf("[call]IMG_Destory\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_IMG_Destory(tmp1);
        }
        else if (idx == 20)
        {
            // printf("[call]GetStreamDataFormRes\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
            vm_GetStreamDataFormRes(tmp1, tmp2, tmp3, tmp4);
        }
        else
        {
            printf("[impl]vmGameLcdManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    //
    // net app
    else if (address >= VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS && address < (VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS) / 4;
        if (1)
        {
            printf("[impl]vmNetAppManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    //
    // audio
    else if (address >= VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS) / 4;
        idx += 1;
        if (idx == 1)
        {
            // printf("[call]vMAudioSetVolume\n");
            // void方法
        }
        else if (idx == 2)
        {
            printf("[call]vMAudioPlayByData\n");
        }
        else if (idx == 3)
        {
            printf("[call]vMAudioPlayWithDataPackage\n");
        }
        else if (idx == 4)
        {
            // printf("[call]vMAudioPlayForGame(a1,a2)\n");
            tmp1 = 0; // pasue stop playing
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 5)
        {
            printf("[call]vMAudioPlayForApp\n");
        }
        else if (idx == 6)
        {
            printf("[call]vMAudioPause\n");
        }
        else if (idx == 7)
        {
            printf("[call]vMAudioResume\n");
        }
        else if (idx == 8)
        {
            // todo
            // printf("[call]vMAudioStop\n");
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
        }
        else if (idx == 11)
        {
            printf("[call]vm_mp3PauseByStream\n");
        }
        else if (idx == 12)
        {
            printf("[call]vm_mp3ResumeByStream\n");
        }
        else if (idx == 13)
        {
            printf("[call]vm_mp3StopBystream\n");
        }
        else if (idx == 14)
        {
            printf("[call]vm_mp3PlayByFile\n");
        }
        else if (idx == 15)
        {
            printf("[call]vm_mp3PauseByFile\n");
        }
        else if (idx == 16)
        {
            printf("[call]vm_mp3ResumeByFile\n");
        }
        else if (idx == 17)
        {
            printf("[call]vm_mp3StopByFile\n");
        }
        else if (idx == 18)
        {
            printf("[call]vMAudioget_progress_time\n");
        }
        else if (idx == 19)
        {
            printf("[call]vmMp3StreamInit\n");
        }
        else if (idx == 20)
        {
            printf("[call]CB_AUD_StartPlay_Init\n");
        }
        else if (idx == 21)
        {
            printf("[call]CB_AUD_StopPlay\n");
        }
        else if (idx == 22)
        {
            printf("[call]CB_AUD_WriteVoiceData\n");
        }
        else if (idx == 23)
        {
            printf("[call]vMStartAudioRecord_async\n");
        }
        else if (idx == 24)
        {
            printf("[call]vMStopAudioRecord_async\n");
        }
        else if (idx == 25)
        {
            printf("[call]vMSetAmrRecBS\n");
        }
        else if (idx == 26)
        {
            printf("[call]vm_mp3PlayByFileEx\n");
        }
        else if (idx == 27)
        {
            printf("[call]vMStartAudioRecordEx\n");
        }
        else if (idx == 28)
        {
            printf("[call]vMStopAudioRecordEx\n");
        }
        else if (idx == 29)
        {
            printf("[call]CB_AUD_StartPlay_InitEx\n");
        }
        else if (idx == 30)
        {
            printf("[call]CB_AUD_StartPlayEx\n");
        }
        else if (idx == 31)
        {
            printf("[call]CB_AUD_StopPlayEx\n");
        }
        else
        {
            printf("[impl]vmAudioManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    //
    // sensor
    else if (address >= VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS && address < (VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS) / 4;
        if (1)
        {
            printf("[impl]vmSensorManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    //
    // vmim
    else if (address >= VM_MANAGER_VMIM_FUNC_LIST_ADDRESS && address < (VM_MANAGER_VMIM_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_VMIM_FUNC_LIST_ADDRESS) / 4;
        if (idx == 0)
        {
            printf("[call]vmDlFuncSms\n");
        }
        else if (idx == 1)
        {
            printf("[call]vmDlFuncMakeCall\n");
        }
        else if (idx == 2)
        {
            printf("[call]vmDlFuncNvRead\n");
        }
        else if (idx == 3)
        {
            printf("[call]vmDlFuncNvWrite\n");
        }
        else if (idx == 4)
        {
            printf("[call]vmDlFuncReleaseCall\n");
        }
        else if (idx == 5)
        {
            printf("[call]vmDlFuncMakeCallEx\n");
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
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
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
            // printf("[call]GAME_isKeyDown(%d)\n", tmp1);
            if (simulatePress == 1)
            {
                tmp2 = (tmp1 & (1 << simulateKey)) != 0; // 0按下
            }
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
        }
        else if (idx == 13)
        {
            printf("[call]GAME_isKeyHold\n");
        }
        else if (idx == 24)
        {
            // printf("[call]GetStreamDataFormRes\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
            uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
            vm_GetStreamDataFormRes(tmp1, tmp2, tmp3, tmp4);
        }
        else if (idx == 51)
        {
            printf("[call]CdRectPoint\n");
        }
        else if (idx == 58)
        {
            printf("[call]Sqrt\n");
        }
        else if (idx == 59)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1); // int* p_g_memoryBlock
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2); // size
            // printf("[call]initMemoryBlock(%x,%x)\n", tmp1, tmp2);
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
            tmp1 = VM_screenImageStruct_ADDRESS;
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
            uc_mem_read(MTK, tmp1 + 8, &tmp2, 4);
            printf("[call]SCREEN_ChangeScreen(%x) update_entry:%x\n", tmp1, tmp2);
            tmp1 = VM_SCREEN_isInQuit_ADDRESS;
            screenStructChange = 1;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 62)
        {
            printf("[call]SCREEN_NotifyLoadResource\n");
        }
        else if (idx == 63)
        {
            printf("[call]SCREEN_IsPointerHold\n");
            tmp1 = !simulateTouchPress;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 64)
        {
            // printf("[call]SCREEN_IsPointerDown(%d)\n", simulateTouchPress);
            tmp1 = simulateTouchPress; // 1按下
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 65)
        {
            printf("[call]SCREEN_IsPointerUp\n");
            tmp1 = !simulateTouchPress;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 66)
        {
            printf("[call]SCREEN_IsPointerDrag\n");
            tmp1 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 67)
        {
            // printf("[call]SCREEN_GetPointerX(%d)\n", simulateTouchX);
            tmp1 = simulateTouchX; // x坐标
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 68)
        {
            // printf("[call]SCREEN_GetPointerY(%d)\n", simulateTouchY);
            tmp1 = simulateTouchY; // y坐标
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 69)
        {
            printf("[call]Get_CurKeyDownState\n");
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
            printf("[call]initDreamFactoryEngine\n");
        }
        else if (idx == 82)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_mem_write(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
            // printf("[call]DF_SetDataPackage(%x)\n", tmp1);
        }
        else if (idx == 83)
        {
            // printf("[call]DF_GetDataPackage\n");
            uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
        }
        else if (idx == 84)
        {
            printf("[call]DF_GetResourceByResourceID\n");
        }
        else if (idx == 85)
        {
            printf("[call]DF_GetResourceByFileName\n");
        }
        else if (idx == 86)
        {
            printf("[call]DF_GetResourceNameByID\n");
        }
        else if (idx == 87)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_DF_GetResourceIDByFileName(tmp1);
            // printf("[call]DF_GetResourceIDByFileName(%x)\n", tmp1);
        }
        else if (idx == 88)
        {
            // printf("[call]DF_GetTResource\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_DF_GetTResource(tmp1);
        }
        else if (idx == 89)
        {
            // printf("[call]DF_GetStreamTResource\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_DF_GetStreamTResource(tmp1);
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
            // printf("[call]DF_ReadShort(%x)\n", tmp1);
        }
        else if (idx == 93)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_DF_ReadInt(tmp1, tmp2);
            // printf("[call]DF_ReadInt\n");
        }
        else if (idx == 94)
        {
            printf("[call]DF_File_ReadShort\n");
        }
        else if (idx == 95)
        {
            printf("[call]DF_File_ReadInt\n");
        }
        else if (idx == 96)
        {
            printf("[call]DF_WriteShort\n");
        }
        else if (idx == 97)
        {
            printf("[call]DF_WriteInt\n");
        }
        else if (idx == 98)
        {
            // printf("[call]DF_ReadString\n");
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_DF_ReadString(tmp1, tmp2);
        }
        else if (idx == 99)
        {
            printf("[call]DF_ReadStringEx\n");
        }
        else if (idx == 100)
        {
            printf("[call]DF_File_ReadString\n");
        }
        else if (idx == 101)
        {
            printf("[call]DF_File_ReadToBuffer\n");
        }
        else if (idx == 102)
        {
            printf("[call]DF_ReadString2\n");
        }
        else if (idx == 103)
        {
            // printf("[call]DF_GetMemoryBlock\n");
            vm_DF_GetMemoryBlock();
        }
        else if (idx == 104)
        {
            printf("[call]DF_Sin\n");
        }
        else if (idx == 105)
        {
            printf("[call]DF_Cos\n");
        }
        else if (idx == 106)
        {
            printf("[call]DF_Degree\n");
        }
        else if (idx == 107)
        {
            printf("[call]DF_CollectionTest\n");
        }
        else if (idx == 108)
        {
            printf("[call]DF_SwapVal\n");
        }
        else if (idx == 109)
        {
            printf("[call]DF_GetFormatString\n");
            vm_readStringByReg(UC_ARM_REG_R0, cbeTextString);
            tmp1 = VM_DreamFactory_CharBuffer_ADDRESS;
            tmp2 = strlen(cbeTextString);
            uc_mem_write(MTK, tmp1, cbeTextString, tmp2);
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
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
        }
        else if (idx == 135)
        {
            printf("[call]strlen\n");
        }
        else if (idx == 136)
        {
            printf("[call]memset\n");
        }
        else if (idx == 137)
        {
            printf("[call]sprintf\n");
        }
        else if (idx == 138)
        {
            printf("[call]vm_log_trace\n");
        }
        else if (idx == 139)
        {
            printf("[call]VmGetRand\n");
        }
        else if (idx == 140)
        {
            printf("[call]vsprintf\n");
        }
        else if (idx == 141)
        {
            printf("[call]strncpy\n");
        }
        else if (idx == 142)
        {
            // printf("[call]strcpy\n");
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp1);
            uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
            vm_strcpy(tmp1, tmp2);
        }
        else if (idx == 143)
        {
            printf("[call]strcat\n");
        }
        else if (idx == 144)
        {
            printf("[call]atol\n");
        }

        // result 区 (从 a1+144 开始)
        else if (idx == 145)
        {
            printf("[call]memmove\n");
        }
        else if (idx == 146)
        {
            printf("[call]atoi\n");
        }
        else if (idx == 147)
        {
            printf("[call]BILLING_GetPayNumByAppId\n");
        }
        else if (idx == 148)
        {
            printf("[call]BILLING_GetRemainDay\n");
        }
        else if (idx == 149)
        {
            printf("[call]BILLING_Pay\n");
        }
        else if (idx == 150)
        {
            printf("[call]BILLING_PayMoreTimes\n");
        }
        else if (idx == 151)
        {
            printf("[call]BILLING_IsRegisterBillingInfo\n");
        }
        else if (idx == 152)
        {
            printf("[call]BILLING_RegisterBillingInfo\n");
        }
        else if (idx == 153)
        {
            printf("[call]BILLING_SetBillingStatus\n");
        }
        else if (idx == 154)
        {
            printf("[call]BILLING_GetBillingStatus\n");
        }
        else if (idx == 155)
        {
            printf("[call]BILLING_IsNeedPay\n");
        }
        else if (idx == 156)
        {
            printf("[call]BILLING_IsInTryStatus\n");
        }
        else if (idx == 157)
        {
            printf("[call]Game_OpenBillingPromptWin\n");
        }
        else if (idx == 158)
        {
            printf("[call]vMstricmp\n");
        }
        else
        {
            printf("[impl]vmGameOldManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
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
            assert(0);
        }
        else if (idx == 7)
        {
            printf("[call]DF_DataPackage_GetFile\n");
            assert(0);
        }
        else if (idx == 8)
        {
            printf("[call]DF_DataPackage_GetFileByID\n");
            assert(0);
        }
        else if (idx == 9)
        {
            printf("[call]DF_DataPackage_GetFileNameByID\n");
            assert(0);
        }
        else if (idx == 10)
        {
            printf("[call]DF_DataPackage_GetFileID\n");
            assert(0);
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
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
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
            // printf("[call]MF_MemoryBlock_Malloc\n");
        }
        else if (idx == 2)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_MF_MemoryBlock_Reset(tmp1);
            // printf("[call]MF_MemoryBlock_Reset\n");
        }
        else if (idx == 3)
        {
            uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
            vm_MF_MemoryBlock_Release(tmp1);
            // printf("[call]MF_MemoryBlock_Release\n");
        }
        else
        {
            printf("[impl]MF_MemoryBlock_调用位置:%d\n", idx);
            assert(0);
        }

        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
    }
    else if (address >= VM_MANAGER_APPSTORE_TABLE_ADDRESS && address < (VM_MANAGER_APPSTORE_TABLE_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE))
    {
        u32 idx = (address - VM_MANAGER_APPSTORE_TABLE_ADDRESS) / 4;
        idx += 1;
        if (idx == 1)
        {
            printf("[call]VmAppStoreInstall\n");
        }
        else if (idx == 2)
        {
            printf("[call]VmAppStoreUninstallEx\n");
        }
        else if (idx == 3)
        {
            printf("[call]VmAppStoreUninstall\n");
        }
        else if (idx == 5)
        {
            printf("[call]VmAppStoreGetAppInfo\n");
        }
        else if (idx == 6)
        {
            printf("[call]VmAppStoreGetInstalledAppInfos\n");
        }
        else if (idx == 7)
        {
            printf("[call]VmAppStoreReleaseAppInfos\n");
        }
        else if (idx == 8)
        {
            printf("[call]VmAppStorePushMsg\n");
        }
        else if (idx == 9)
        {
            printf("[call]VmAppStoreRunJavaAp\n");
        }
        else if (idx == 10)
        {
            printf("[call]VmAppStoreRunJavaApEx\n");
        }
        else if (idx == 11)
        {
            printf("[call]VmAppStoreRunCbeAp\n");
        }
        else if (idx == 12)
        {
            printf("[call]VmAppStoreSupportJava\n");
        }
        else if (idx == 13)
        {
            printf("[call]VmAppStoreGetPath\n");
        }
        else if (idx == 14)
        {
            printf("[call]VmGetPhoneType\n");
        }
        else if (idx == 15)
        {
            printf("[call]VmGetAppNumByType\n");
        }
        else if (idx == 16)
        {
            printf("[call]VmGetPhoneSupportApType\n");
        }
        else if (idx == 17)
        {
            printf("[call]VmAppGetHasLocalIcon\n");
        }
        else if (idx == 18)
        {
            printf("[call]VmAppRunApByIdAndName\n");
        }
        else if (idx == 19)
        {
            printf("[call]VmAppAddShortCutMenu\n");
        }
        else if (idx == 20)
        {
            printf("[call]VmAppDelShortCutMenu\n");
        }
        else if (idx == 21)
        {
            printf("[call]CoolBar_DownLoad_AppFile\n");
        }
        else if (idx == 22)
        {
            printf("[call]GetApsDownAppUrl\n");
        }
        else if (idx == 23)
        {
            printf("[call]Coolbar_PreLoadAppByName\n");
        }
        else if (idx == 24)
        {
            printf("[call]Coolbar_ParseApsDownDataFile\n");
        }
        else if (idx == 25)
        {
            printf("[call]CoolBar_DownLoad_Stop\n");
        }
        else if (idx == 26)
        {
            printf("[call]VmAppQueryStcExist\n");
        }
        else if (idx == 27)
        {
            printf("[call]VmPreCheckInstallAppPlace\n");
        }
        else if (idx == 28)
        {
            printf("[call]VmGetInstallFileSystem\n");
        }
        else if (idx == 29)
        {
            printf("[call]VmSetInstallFileSystem\n");
        }
        else if (idx == 30)
        {
            printf("[call]VmSetRunAppFileSystem\n");
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
        }

        // result 区
        else if (idx == 33)
        {
            printf("[call]VmGetApsVerNum\n");
        }
        else if (idx == 34)
        {
            printf("[call]VmReadAppStoreCbeVersion\n");
        }
        else if (idx == 35)
        {
            printf("[call]VmWriteAppStoreCbeVersion\n");
        }
        else if (idx == 36)
        {
            printf("[call]VmGetSecurityCode\n");
        }
        else if (idx == 37)
        {
            printf("[call]VmGetSecurityCodeEx\n");
        }
        else if (idx == 38)
        {
            printf("[call]VmSetAppStoreName\n");
        }
        else if (idx == 39)
        {
            printf("[call]VmGetAppStoreName\n");
        }
        else
        {
            printf("[impl]vmAPPStoreManager调用位置:%d\n", idx);
            while (1)
                ;
        }
        // bx lr实现
        uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
        uc_reg_write(MTK, UC_ARM_REG_PC, &tmp1);
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
