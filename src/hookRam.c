#include "main.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* #region agent log */
#ifndef AGENT_DBG191169_LOG_DEFINED
#define AGENT_DBG191169_LOG_DEFINED
/* Watch [base60/64 + 0x600 + 0x1c] writes that fill battle/scene mapctrl. */
static u32 g_agent_watch_mapctrl_slot60 = 0;
static u32 g_agent_watch_mapctrl_slot64 = 0;
static u32 g_agent_watch_mapctrl_logs = 0;
/* ScreenInit blx target lives at *(Global_R9 + 0x5c50); catch corruptors. */
static u32 g_agent_watch_screeninit_fptr = 0;
static u32 g_agent_watch_screeninit_fptr_logs = 0;
static u32 g_agent_watch_screeninit_fptr_last = 0;
static void agent_dbg191169_log(const char *hypothesisId, const char *location,
                                const char *message, const char *dataJson)
{
    FILE *fp = fopen("d:/works/my/jianghu/CBE_EMU_pay/debug-0b451a.log", "a");
    if (fp == NULL)
        return;
    fprintf(fp,
            "{\"sessionId\":\"0b451a\",\"runId\":\"encounter-repreflight\","
            "\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\","
            "\"data\":%s,\"timestamp\":%lld}\n",
            hypothesisId ? hypothesisId : "",
            location ? location : "",
            message ? message : "",
            dataJson ? dataJson : "{}",
            (long long)time(NULL) * 1000LL);
    fclose(fp);
}
#endif
/* #endregion */

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
    /* #region agent log */
    if (type == UC_MEM_WRITE)
    {
        u32 addr32 = (u32)address;
        if (size >= 4 && g_agent_watch_mapctrl_logs < 48 &&
            ((g_agent_watch_mapctrl_slot60 != 0 &&
              addr32 == g_agent_watch_mapctrl_slot60) ||
             (g_agent_watch_mapctrl_slot64 != 0 &&
              addr32 == g_agent_watch_mapctrl_slot64)))
        {
            u32 pc = 0, lr = 0;
            char dataBuf[220];
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            uc_reg_read(uc, UC_ARM_REG_LR, &lr);
            snprintf(dataBuf, sizeof(dataBuf),
                     "{\"addr\":%u,\"value\":%u,\"pc\":%u,\"lr\":%u,"
                     "\"slot\":%u,\"size\":%u}",
                     addr32, (u32)value, pc, lr,
                     (addr32 == g_agent_watch_mapctrl_slot64) ? 64u : 60u,
                     size);
            agent_dbg191169_log("Q", "hookRam.c:mapctrl_slot_write",
                                "cbe_mapctrl_slot_write", dataBuf);
            g_agent_watch_mapctrl_logs++;
        }
        /* Wide overlap: only emit when slot word changes or direct hit. */
        if (g_agent_watch_screeninit_fptr != 0 &&
            g_agent_watch_screeninit_fptr_logs < 64)
        {
            u32 watch = g_agent_watch_screeninit_fptr;
            u32 w0 = watch - 256u;
            u32 w1 = watch + 64u;
            u32 end = addr32 + (size ? size : 1u);
            if (addr32 < w1 && end > w0)
            {
                u32 pc = 0, lr = 0, cur = 0, hit = 0;
                uc_reg_read(uc, UC_ARM_REG_PC, &pc);
                uc_reg_read(uc, UC_ARM_REG_LR, &lr);
                uc_mem_read(uc, watch, &cur, 4);
                hit = (addr32 <= watch && end > watch) ? 1u : 0u;
                if (hit || cur != g_agent_watch_screeninit_fptr_last)
                {
                    char dataBuf[260];
                    snprintf(dataBuf, sizeof(dataBuf),
                             "{\"addr\":%u,\"value\":%u,\"pc\":%u,\"lr\":%u,"
                             "\"size\":%u,\"slot\":%u,\"cur\":%u,\"hit\":%u}",
                             addr32, (u32)value, pc, lr, size, watch, cur,
                             hit);
                    agent_dbg191169_log("V", "hookRam.c:screeninit_fptr_write",
                                        "cbe_screeninit_fptr_write", dataBuf);
                    g_agent_watch_screeninit_fptr_logs++;
                    g_agent_watch_screeninit_fptr_last = cur;
                }
            }
        }
    }
    /* #endregion */
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
    printf("[warn][mem] unmapped access soft-failed (host continues)\n");
    fflush(stdout);
    /* #region agent log */
    {
        u32 pc = 0, lr = 0, r0 = 0, r1 = 0, r2 = 0, r5 = 0, r6 = 0;
        char dataBuf[240];
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R1, &r1);
        uc_reg_read(uc, UC_ARM_REG_R2, &r2);
        uc_reg_read(uc, UC_ARM_REG_R5, &r5);
        uc_reg_read(uc, UC_ARM_REG_R6, &r6);
        snprintf(dataBuf, sizeof(dataBuf),
                 "{\"fault\":%u,\"pc\":%u,\"lr\":%u,\"r0\":%u,\"r1\":%u,"
                 "\"r2\":%u,\"r5\":%u,\"r6\":%u,\"type\":%u}",
                 (u32)address, pc, lr, r0, r1, r2, r5, r6, (u32)type);
        agent_dbg191169_log("A", "hookRam.c:hookRamErrorBack", "mem_fault",
                            dataBuf);
        /* Tile-render fault after stale +0x64 SetMapCtrl (dream post-outdoor). */
        if (pc == 0x01005474u)
        {
            u32 r4b = 0, table18 = 0, mc1c = 0;
            char tileBuf[200];
            uc_reg_read(uc, UC_ARM_REG_R4, &r4b);
            if (r4b != 0)
            {
                uc_mem_read(uc, r4b + 0x18u, &table18, 4);
                uc_mem_read(uc, r4b + 0x1cu, &mc1c, 4);
            }
            snprintf(tileBuf, sizeof(tileBuf),
                     "{\"r4\":%u,\"table18\":%u,\"mc1c\":%u,"
                     "\"fault\":%u,\"lr\":%u}",
                     r4b, table18, mc1c, (u32)address, lr);
            agent_dbg191169_log("R", "hookRam.c:tile_fault",
                                "cbe_tile_render_fault", tileBuf);
        }
        /* Dump live-node slots near SetMapCtrlViewport faults (dream battle). */
        if (pc == 0x01046C48u || pc == 0x01046c48u)
        {
            extern u32 Global_R9;
            u32 sceneNodeBase = 0;
            char nodeBuf[320];
            /* CBE: historic caller lr=0x01014E77 loads mapctrl from
             * *( [r4+0x60] + 0x600 + 0x1c ) before blx vt+4. */
            {
                u32 r4b = 0, mapBase = 0, mapCtrl = 0, sceneMap = 0;
                u8 flag1 = 0;
                char chainBuf[280];
                uc_reg_read(uc, UC_ARM_REG_R4, &r4b);
                if (r4b != 0)
                {
                    uc_mem_read(uc, r4b + 1u, &flag1, 1);
                    uc_mem_read(uc, r4b + 0x60u, &mapBase, 4);
                    if (mapBase != 0)
                        uc_mem_read(uc, mapBase + 0x600u + 0x1cu, &mapCtrl, 4);
                }
                if (r0 != 0)
                    uc_mem_read(uc, r0 + 0x1C30u, &sceneMap, 4);
                snprintf(chainBuf, sizeof(chainBuf),
                         "{\"lr\":%u,\"r0\":%u,\"r1_after_add40\":%u,\"r4\":%u,"
                         "\"flag1\":%u,\"mapBase\":%u,\"slot_mapCtrl\":%u,"
                         "\"scene_1C30\":%u}",
                         lr, r0, r1, r4b, (u32)flag1, mapBase, mapCtrl,
                         sceneMap);
                agent_dbg191169_log("C", "hookRam.c:setmapctrl_chain",
                                    "cbe_setmapctrl_fault_chain", chainBuf);
            }
            if (Global_R9 != 0 &&
                uc_mem_read(uc, Global_R9 + 0x5CB0, &sceneNodeBase,
                            sizeof(sceneNodeBase)) == UC_ERR_OK &&
                sceneNodeBase != 0)
            {
                u32 i;
                for (i = 0; i < 25u; ++i)
                {
                    u32 node = sceneNodeBase + i * 340u;
                    u32 actorId = 0;
                    u16 x = 0, y = 0;
                    u8 nodeKind = 0, active = 0;
                    u32 battleX = 0, battleY = 0, battleHp = 0;
                    uc_mem_read(uc, node + 0x64, &actorId, sizeof(actorId));
                    uc_mem_read(uc, node + 0x18, &x, sizeof(x));
                    uc_mem_read(uc, node + 0x1A, &y, sizeof(y));
                    uc_mem_read(uc, node + 0x13B, &nodeKind, sizeof(nodeKind));
                    uc_mem_read(uc, node + 0x13F, &active, sizeof(active));
                    uc_mem_read(uc, node + 0xF0, &battleX, sizeof(battleX));
                    uc_mem_read(uc, node + 0xF4, &battleY, sizeof(battleY));
                    uc_mem_read(uc, node + 0xB4, &battleHp, sizeof(battleHp));
                    if (actorId == 0 && active == 0 && nodeKind == 0)
                        continue;
                    snprintf(nodeBuf, sizeof(nodeBuf),
                             "{\"i\":%u,\"actor\":%u,\"kind\":%u,\"active\":%u,"
                             "\"pos\":[%u,%u],\"bpos\":[%u,%u],\"hp\":%u}",
                             i, actorId, nodeKind, active, x, y, battleX,
                             battleY, battleHp);
                    agent_dbg191169_log("N", "hookRam.c:live_node",
                                        "dream_fault_live_node", nodeBuf);
                }
            }
        }
    }
    /* #endregion */
    uc_emu_stop(uc);
    return true;
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
