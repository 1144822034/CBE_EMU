#include "main.h"
#include <string.h>

bool hookInsnInvalid(uc_engine *uc, void *user_data);
void hookRamCallBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
bool hookRamErrorBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
void handleLcdReg(uint64_t address, u32 data, uint64_t value);
void handleTouchScreenReg(uint64_t address, u32 data, uint64_t value);

extern u8 g_mockBattleOperateSessionArmed;
extern u32 g_vmInputWatchUserBuf;
extern u32 g_vmInputWatchUserBufLen;
extern u32 g_vmInputWatchCallback;
extern u32 g_vmInputWatchCallR9;
extern u32 g_vmInputWatchWriteCount;
extern u32 g_hangupBattleStateWatchAddress;
extern u32 g_hangupBattleStateWatchGeneration;
extern u32 g_hangupBattleStateWatchWriteCount;
extern u32 g_hangupSceneModeWatchAddress;
extern u32 g_hangupSceneModeWatchWriteCount;
extern u32 g_hangupBusinessDelegateWatchAddress;
extern u32 g_hangupBusinessDelegateWatchWriteCount;
extern u32 g_vmAutomationGameLoadingGateWatchAddress;
extern u32 g_vmAutomationGameLoadingGateWatchWriteCount;

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
        wp_type = 1;
    else if (type == UC_MEM_READ)
        wp_type = 2;

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
    if (type == UC_MEM_WRITE && g_vmInputWatchUserBufLen != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_vmInputWatchUserBuf;
        u32 watchEnd = watchStart + g_vmInputWatchUserBufLen;
        if (start < watchEnd && end > watchStart)
        {
            if (g_vmInputWatchWriteCount < 24)
            {
                u32 pc = 0;
                u32 sp = 0;
                u32 r9 = 0;
                uc_reg_read(uc, UC_ARM_REG_PC, &pc);
                uc_reg_read(uc, UC_ARM_REG_SP, &sp);
                uc_reg_read(uc, UC_ARM_REG_R9, &r9);
                printf("[debug][vmInput] dispU-write #%u pc=%08x last=%08x addr=%08x size=%u value=%llx r9=%08x sp=%08x cb=%08x callR9=%08x\n",
                       g_vmInputWatchWriteCount + 1,
                       pc,
                       lastAddress,
                       start,
                       size,
                       value,
                       r9,
                       sp,
                       g_vmInputWatchCallback,
                       g_vmInputWatchCallR9);
            }
            ++g_vmInputWatchWriteCount;
        }
    }
    if (type == UC_MEM_WRITE && g_hangupBattleStateWatchAddress != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_hangupBattleStateWatchAddress;
        u32 watchEnd = watchStart + sizeof(u16);
        if (start < watchEnd && end > watchStart)
        {
            u32 pc = 0;
            FILE *trace;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            ++g_hangupBattleStateWatchWriteCount;
            trace = fopen("logs/hangup-protocol.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "[info][network] mock_hangup_battle_state_write "
                        "generation=%u count=%u pc=%08x last=%08x "
                        "addr=%08x size=%u value=%llx\n",
                        g_hangupBattleStateWatchGeneration,
                        g_hangupBattleStateWatchWriteCount, pc, lastAddress,
                        start, size, value);
                fflush(trace);
                fclose(trace);
            }
        }
    }
    if (type == UC_MEM_WRITE && g_hangupSceneModeWatchAddress != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_hangupSceneModeWatchAddress;
        u32 watchEnd = watchStart + sizeof(u8);
        if (start < watchEnd && end > watchStart)
        {
            u32 pc = 0;
            FILE *trace;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            ++g_hangupSceneModeWatchWriteCount;
            trace = fopen("logs/hangup-protocol.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "[info][network] mock_hangup_scene_mode_write "
                        "generation=%u count=%u pc=%08x last=%08x "
                        "addr=%08x size=%u value=%llx\n",
                        g_hangupBattleStateWatchGeneration,
                        g_hangupSceneModeWatchWriteCount, pc, lastAddress,
                        start, size, value);
                fflush(trace);
                fclose(trace);
            }
        }
    }
    if (type == UC_MEM_WRITE && g_hangupBusinessDelegateWatchAddress != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_hangupBusinessDelegateWatchAddress;
        u32 watchEnd = watchStart + sizeof(u32);
        if (start < watchEnd && end > watchStart)
        {
            u32 pc = 0;
            FILE *trace;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            ++g_hangupBusinessDelegateWatchWriteCount;
            trace = fopen("logs/hangup-protocol.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "[info][network] mock_hangup_business_delegate_write "
                        "generation=%u count=%u pc=%08x last=%08x "
                        "addr=%08x size=%u value=%llx\n",
                        g_hangupBattleStateWatchGeneration,
                        g_hangupBusinessDelegateWatchWriteCount, pc,
                        lastAddress, start, size, value);
                fflush(trace);
                fclose(trace);
            }
        }
    }
    if (type == UC_MEM_WRITE && g_vmAutomationGameLoadingGateWatchAddress != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_vmAutomationGameLoadingGateWatchAddress;
        u32 watchEnd = watchStart + sizeof(u8);
        if (start < watchEnd && end > watchStart)
        {
            u32 pc = 0;
            FILE *trace;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            ++g_vmAutomationGameLoadingGateWatchWriteCount;
            trace = fopen("logs/hangup-protocol.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "[info][network] mock_hangup_game_loading_gate_write "
                        "count=%u pc=%08x last=%08x addr=%08x size=%u value=%llx\n",
                        g_vmAutomationGameLoadingGateWatchWriteCount, pc,
                        lastAddress, start, size, value);
                fflush(trace);
                fclose(trace);
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
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_R2,
        UC_ARM_REG_R3,
        UC_ARM_REG_R4,
        UC_ARM_REG_R5,
        UC_ARM_REG_R6,
        UC_ARM_REG_R7,
    };
    for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i)
    {
        u32 ptr = 0;
        uc_reg_read(MTK, regs[i], &ptr);
        if ((ptr >= Program_ROM_Address && ptr < Program_ROM_Address + Program_ROM_Mapped_Size) ||
            (ptr >= VM_Memory_Pool_ADDRESS && ptr < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE))
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
    if (r5 >= Program_ROM_Address && r5 < Program_ROM_Address + Program_ROM_Mapped_Size)
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
