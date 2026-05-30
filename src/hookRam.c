#include "main.h"
#include <string.h>

bool hookInsnInvalid(uc_engine *uc, void *user_data);
void hookRamCallBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
bool hookRamErrorBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
void handleLcdReg(uint64_t address, u32 data, uint64_t value);
void handleTouchScreenReg(uint64_t address, u32 data, uint64_t value);

#ifdef GDB_SERVER_SUPPORT
/* 前向声明 - 这些在gdb_client.c中定义 */
extern TargetSystem gdbTarget;
extern GDBClient clients[1];
extern void send_gdb_response(GDBClient *client, const char *response);
extern int check_watchpoints(unsigned int addr, unsigned int size, int type);
#endif

void hookRamCallBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data)
{
#ifdef GDB_SERVER_SUPPORT
    int wp_type = 0;
    if (type == UC_MEM_WRITE)
    {
        wp_type = 1;
    }
    else if (type == UC_MEM_READ)
    {
        wp_type = 2;
    }

    if (wp_type != 0 && check_watchpoints(address, size, wp_type))
    {
        gdbTarget.running = 0;
        gdbTarget.last_stop_reason = 0x0A;
        char response[32];
        sprintf(response, "S%02x", gdbTarget.last_stop_reason);
        send_gdb_response(&clients[0], response);
        while (gdbTarget.running == 0)
            ;
    }
#endif
    if (type == UC_MEM_WRITE && Global_R9 != 0)
    {
        struct WatchGlobal
        {
            u32 offset;
            const char *name;
        };
        static const struct WatchGlobal watchGlobals[] = {
            {0x4cb6, "update_state"},
            {0x5494, "startup_progress"},
            {0x5496, "has_local_update"},
            {0x54b0, "startup_dispatch_flag"},
            {0x95e8, "download_state"},
            {0x955c, "update_flag0"},
            {0x955d, "update_flag1"},
            {0x955e, "update_flag2"},
            {0x955f, "update_flag3"},
        };
        for (unsigned i = 0; i < sizeof(watchGlobals) / sizeof(watchGlobals[0]); ++i)
        {
            u32 watchAddr = Global_R9 + watchGlobals[i].offset;
            if (address <= watchAddr && watchAddr < address + size)
            {
                u32 pc = 0, lr = 0;
                u8 oldValue = 0;
                u8 newValue = (u8)(value >> ((watchAddr - (u32)address) * 8));
                uc_reg_read(uc, UC_ARM_REG_PC, &pc);
                uc_reg_read(uc, UC_ARM_REG_LR, &lr);
                uc_mem_read(uc, watchAddr, &oldValue, 1);
                FILE *fp = fopen("net_trace.log", "a");
                if (fp)
                {
                    fprintf(fp, "global_write name=%s off=%04x addr=%08x size=%u old=%u new=%u pc=%08x lr=%08x last=%08x\n",
                            watchGlobals[i].name, watchGlobals[i].offset, (u32)address, size, oldValue, newValue, pc, lr, lastAddress);
                    fclose(fp);
                }
            }
        }
        u32 debugUiObj = 0;
        if (uc_mem_read(uc, Global_R9 + 0x9928 + 0x10, &debugUiObj, 4) == UC_ERR_OK && debugUiObj != 0)
        {
            u32 stateAddr = debugUiObj + 0x3d;
            if (address <= stateAddr && stateAddr < address + size)
            {
                u32 pc = 0, lr = 0;
                u8 oldState = 0;
                u8 newState = (u8)(value >> ((stateAddr - (u32)address) * 8));
                uc_reg_read(uc, UC_ARM_REG_PC, &pc);
                uc_reg_read(uc, UC_ARM_REG_LR, &lr);
                uc_mem_read(uc, stateAddr, &oldState, 1);
                FILE *fp = fopen("net_trace.log", "a");
                if (fp)
                {
                    fprintf(fp, "startup_state_write addr=%08x size=%u old=%u new=%u pc=%08x lr=%08x last=%08x\n",
                            (u32)address, size, oldState, newState, pc, lr, lastAddress);
                    fclose(fp);
                }
            }
        }
    }
    // if (type == UC_MEM_WRITE && ((address == 0x10353C0)))
    // {
    //     printf("write[%x:", address);
    //     printf("%x]", value);
    //     printf(" at %x\n", lastAddress);
    // }
}
bool hookRamErrorBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data)
{
    printf("地址无法访问:%x type:%d size:%u value:%llx\n", address, type, size, value);
    dumpCpuInfo();
    int regs[] = {
        UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    };
    for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i)
    {
        u32 ptr = 0;
        uc_reg_read(MTK, regs[i], &ptr);
        if (ptr >= ROM_ADDRESS && ptr < ROM_ADDRESS + 0x1000000)
        {
            printf("------------\nr%u object dump at %08x\n", i, ptr);
            dumpVirtMemory(ptr, 96);
        }
    }
    u32 sp;
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    if (sp >= STACK_ADDRESS && sp <= STACK_ADDRESS + 0x100000)
        dumpVirtMemory(sp - 64, 128);
    assert(0);
    return false;
}
void hookCpuIntr(uc_engine *uc, uint32_t intno, void *user_data)
{
    if (intno == 2)
    {
        u32 reason = 0;
        u32 arg = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &reason);
        uc_reg_read(uc, UC_ARM_REG_R1, &arg);
        if (reason == 3)
        {
            u8 ch = 0;
            uc_mem_read(uc, arg, &ch, 1);
            putchar(ch);
            return;
        }
        if (reason == 4)
        {
            vm_readStringByPtr(arg, cbeTextString);
            printf("%s", cbeTextString);
            return;
        }
    }
    printf("未处理的CPU中断:%x\n", intno);
    if (intno == 2)
    {
        u32 reason = 0;
        u32 arg = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &reason);
        uc_reg_read(uc, UC_ARM_REG_R1, &arg);
        printf("semihosting reason:%x arg:%x\n", reason, arg);
    }
    // u32 pc;
    // uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    // pc += 4;
    // uc_reg_write(uc, UC_ARM_REG_PC, &pc);

    dumpCpuInfo();
    u32 sp;
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    if (sp >= STACK_ADDRESS && sp <= STACK_ADDRESS + 0x100000)
        dumpVirtMemory(sp, 128);
    u32 r5;
    uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    if (r5 >= ROM_ADDRESS && r5 < ROM_ADDRESS + 0x1000000)
    {
        vm_readStringByReg(UC_ARM_REG_R5, cbeTextString);
        printf("%s", cbeTextString);
    }
    assert(0);
}

bool hookInsnInvalid(uc_engine *uc, void *user_data)
{
    u32 insn;
    u32 pc;

    (void)uc;
    (void)user_data;

    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_mem_read(MTK, pc, &insn, 4);

    /* MRC/MCR 等协处理器指令：静默跳过 */
    if (pc == 0x7C322C || pc == 0x7C3238)
    {
        printf("mrc指令:%x\n", insn);
        return 0;
    }

    printf("指令无效:%x\n", insn);
    dumpCpuInfo();
    assert(0);
    return 0;
}
