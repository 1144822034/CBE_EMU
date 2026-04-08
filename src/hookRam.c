#include "main.h"
#include <string.h>

bool hookInsnInvalid(uc_engine *uc, void *user_data);
void hookRamCallBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data);
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
    if (type == UC_MEM_WRITE && address == 0x54000E4)
    {
        printf("write 0x54000E4: %x", value);
        printf(" at %x\n", lastAddress);
    }
}
void hookRamErrorBack(uc_engine *uc, uc_mem_type type, uint64_t address, uint32_t size, int64_t value, u32 data)
{
    printf("地址无法访问:%x\n", address);
    dumpCpuInfo();
    while (1)
        ;
}
void hookCpuIntr(uc_engine *uc, uint32_t intno, void *user_data)
{
    printf("未处理的CPU中断:%x\n", intno);
    // u32 pc;
    // uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    // pc += 4;
    // uc_reg_write(uc, UC_ARM_REG_PC, &pc);

    vm_readStringByReg(UC_ARM_REG_R5, cbeTextString);
    printf("%s", cbeTextString);
    dumpCpuInfo();
    assert(0);
}

bool hookInsnInvalid(uc_engine *uc, void *user_data)
{
    u32 insn;
    u32 pc;
    u32 cpsr;

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

    /*
     * CPSR.T=0（ARM 状态）但 PC 指向 Thumb/Thumb-2 代码时会报 UC_ERR_INSN_INVALID。
     * 根因：中断返回路径（MOV PC,LR 而非 BX LR）、诊断/异常路径互用状态未同步 CPSR.T。
     * 覆盖所有 16-bit Thumb（如 BDF8 = POP {R3-R7,PC}）和 32-bit Thumb-2：
     * 只要当前是 ARM（T=0），直接置 T=1 让 Unicorn 以 Thumb 重新解码，最多尝试一次；
     * 若 Thumb 下仍无效，返回 false 停止模拟。
     */
    uc_reg_read(MTK, UC_ARM_REG_CPSR, &cpsr);
    if ((cpsr & 0x20u) == 0u)
    {
        static u32 arm_thumb_fix_log = 32u;
        u32 nc = cpsr | 0x20u;
        uc_reg_write(MTK, UC_ARM_REG_CPSR, &nc);
        if (arm_thumb_fix_log > 0u)
        {
            arm_thumb_fix_log--;
            printf("[UC] INSN_INVALID: T=0→1 继续 PC=%08x insn=%08x (mode=%02x)\n",
                   pc, insn, cpsr & 0x1fu);
        }
        return 1;
    }

    printf("指令无效:%x\n", insn);
    return 0;
}