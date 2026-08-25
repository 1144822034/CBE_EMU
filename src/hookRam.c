#include "main.h"
#include <stdlib.h>
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
extern u32 g_vmSceneInputCallbackLastObserved;
extern u32 g_hangupBattleStateWatchAddress;
extern u32 g_hangupBattleStateWatchGeneration;
extern u32 g_hangupBattleStateWatchWriteCount;
extern u32 g_hangupSceneModeWatchAddress;
extern u32 g_hangupSceneModeWatchWriteCount;
extern u32 g_hangupBusinessDelegateWatchAddress;
extern u32 g_hangupBusinessDelegateWatchWriteCount;
extern u32 g_hangupAutoCandidateWatchAddress;
extern u32 g_hangupAutoCandidateWatchWriteCount;
extern u32 g_hangupAutoBattleInitWatchAddress;
extern u32 g_hangupAutoBattleInitWatchWriteCount;
extern u32 g_vmAutomationGameLoadingGateWatchAddress;
extern u32 g_vmAutomationGameLoadingGateWatchWriteCount;
extern u32 g_vmAutomationBattleAutoFlagWatchAddress;
extern u32 g_vmAutomationBattleOverlayWatchAddress;
extern u32 g_vmAutomationBattlePhaseWatchAddress;
extern u32 g_vmAutomationBattleAutoFlagWatchWriteCount;
extern u32 g_vmAutomationBattleOverlayWatchWriteCount;
extern u32 g_vmAutomationBattlePhaseWatchWriteCount;
extern u8 g_shopReturnForensicsActive;
extern u32 g_shopReturnForensicsGateWatchAddress;
extern void vm_shop_return_forensics_note_gate_write(uc_engine *uc,
                                                      uint64_t address,
                                                      uint32_t size,
                                                      int64_t value);
/* Armed by a CBE code-hook only after the client creates its own item
 * controller.  This remains an observation-only memory watch. */
extern u32 g_vmEquipmentEnhanceRulesWatchAddress;
extern u32 g_vmEquipmentEnhanceRulesWatchWriteCount;
/* The scene movement ticker loads its map controller from Global_R9+0x9540.
 * Keep this diagnosis read-only: an invalid pointer here crashes later in
 * UpdateSpriteMovement and otherwise loses the instruction that first wrote
 * the bad state. */
static u32 g_vmMapControllerWatchWriteCount;

/* SceneTickUpdatePositions(0x010163A4) dispatches raw input through this
 * three-word callback group.  The startup test-map regression reaches the
 * dispatcher with the first and third slots clear, so retain the first guest
 * writes that establish or clear them.  This hook only observes Unicorn's
 * write notification and never changes guest registers, memory, or flow. */
static u32 g_vmSceneInputDelegateWatchWriteCount;

/* The scene input dispatcher gates auto-battle on Global_R9+23682.  The
 * startup SCE investigation needs the native writer, not a host-written
 * substitute, so retain only a bounded write history when explicitly enabled.
 */
static u32 g_vmSceneControlStateWatchWriteCount;

static void vm_trace_map_controller_writer_context(uc_engine *uc, u32 pc,
                                                   u32 cursorRef)
{
    u32 outerSp;
    u32 caller = 0;
    u32 destination = 0;
    u32 cursor = 0;
    u32 format = 0;
    u32 arg2 = 0;
    u32 arg3 = 0;
    u8 formatBytes[48] = {0};
    u32 formatLength = 0;
    FILE *trace;

    /* fmt_sprintf_like builds its cursor cell at outer_sp+0x3c. Its entry
     * PUSH {R0-R3} values and saved LR remain at fixed offsets while the
     * formatter calls WriteByteToStream. */
    if (pc != 0x0104E0FEu || cursorRef < 0x3cu)
        return;
    outerSp = cursorRef - 0x3cu;
    if (uc_mem_read(uc, cursorRef, &cursor, sizeof(cursor)) != UC_ERR_OK ||
        uc_mem_read(uc, outerSp + 0x4cu, &caller, sizeof(caller)) != UC_ERR_OK ||
        uc_mem_read(uc, outerSp + 0x50u, &destination,
                    sizeof(destination)) != UC_ERR_OK ||
        uc_mem_read(uc, outerSp + 0x54u, &format, sizeof(format)) != UC_ERR_OK ||
        uc_mem_read(uc, outerSp + 0x58u, &arg2, sizeof(arg2)) != UC_ERR_OK ||
        uc_mem_read(uc, outerSp + 0x5cu, &arg3, sizeof(arg3)) != UC_ERR_OK)
    {
        return;
    }
    if (format != 0 &&
        uc_mem_read(uc, format, formatBytes, sizeof(formatBytes)) == UC_ERR_OK)
    {
        formatLength = (u32)sizeof(formatBytes);
    }
    trace = fopen("logs/map-controller-forensics.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "map_controller_writer_context pc=%08x caller=%08x "
            "dest_initial=%08x cursor=%08x cursor_ref=%08x format=%08x "
            "arg2=%08x arg3=%08x format_head=",
            pc, caller, destination, cursor, cursorRef, format, arg2, arg3);
    for (u32 i = 0; i < formatLength; ++i)
        fprintf(trace, "%02x%s", formatBytes[i],
                i + 1u < formatLength ? "-" : "");
    fputc('\n', trace);
    fflush(trace);
    fclose(trace);
}

static void vm_trace_map_controller_write(u32 count, u32 pc, u32 lr,
                                          u32 address, u32 size,
                                          uint64_t value, u32 previous,
                                          u32 r0, u32 r1, u32 r2, u32 r3,
                                          u32 sp, const u32 *stackWords,
                                          u32 stackWordCount)
{
    FILE *trace = fopen("logs/map-controller-forensics.log", "ab");

    if (trace == NULL)
        return;
    fprintf(trace,
            "map_controller_write count=%u pc=%08x lr=%08x last=%08x "
            "addr=%08x size=%u value=%llx previous=%08x "
            "r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x",
            count, pc, lr, lastAddress, address, size, value, previous,
            r0, r1, r2, r3, sp);
    for (u32 i = 0; i < stackWordCount; ++i)
        fprintf(trace, " stack%u=%08x", i, stackWords[i]);
    fputc('\n', trace);
    fflush(trace);
    fclose(trace);
}

#ifdef GDB_SERVER_SUPPORT
/* 前向声明 - 这些在gdb_client.c中定义 */
extern TargetSystem gdbTarget;
extern GDBClient clients[1];
extern void send_gdb_response(GDBClient *client, const char *response);
extern int check_watchpoints(unsigned int addr, unsigned int size, int type);
#endif

/* Isolated auto-battle regressions need the client-owned terminal predicate
 * (`game+1140 || game+1136`) and its phase write sequence.  These watches are
 * armed only by the dedicated automation scenarios after native battle start;
 * they are observational and do not alter emulated memory or scheduling. */
static void vm_automation_trace_battle_watch_write(uc_engine *uc,
                                                   uint64_t address,
                                                   uint32_t size,
                                                   int64_t value,
                                                   const char *field,
                                                   u32 watchAddress,
                                                   u32 watchSize,
                                                   u32 *writeCount)
{
    u32 start = (u32)address;
    u32 end = start + size;
    u32 pc = 0;
    FILE *trace;

    if (watchAddress == 0 || writeCount == NULL ||
        start >= watchAddress + watchSize || end <= watchAddress)
    {
        return;
    }
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    ++*writeCount;
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] automation_hangup_battle_watch_write "
            "field=%s count=%u pc=%08x last=%08x addr=%08x size=%u value=%llx\n",
            field ? field : "-", *writeCount, pc, lastAddress,
            start, size, value);
    fflush(trace);
    fclose(trace);
}

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
    if (type == UC_MEM_WRITE && Global_R9 != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = Global_R9 + 0x9540u;
        u32 delegateStart = Global_R9 + 0x5D24u;
        u32 sceneControlStateStart = Global_R9 + 23682u;
        if (start < sceneControlStateStart + sizeof(u16) &&
            end > sceneControlStateStart &&
            g_vmSceneControlStateWatchWriteCount < 64u)
        {
            const char *enabled = getenv("CBE_TRACE_SCENE_BATTLE_CONTROL_STATE");

            if (enabled != NULL && strcmp(enabled, "1") == 0)
            {
                u32 pc = 0;
                u32 lr = 0;
                u32 r0 = 0;
                u32 r1 = 0;
                u32 r2 = 0;
                u32 r3 = 0;
                u32 sp = 0;
                u32 stackWords[12] = {0};
                u16 prior = 0;
                FILE *trace = NULL;

                (void)uc_mem_read(uc, sceneControlStateStart, &prior,
                                  sizeof(prior));
                (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
                (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
                (void)uc_reg_read(uc, UC_ARM_REG_R0, &r0);
                (void)uc_reg_read(uc, UC_ARM_REG_R1, &r1);
                (void)uc_reg_read(uc, UC_ARM_REG_R2, &r2);
                (void)uc_reg_read(uc, UC_ARM_REG_R3, &r3);
                (void)uc_reg_read(uc, UC_ARM_REG_SP, &sp);
                if (sp != 0)
                    (void)uc_mem_read(uc, sp, stackWords, sizeof(stackWords));
                ++g_vmSceneControlStateWatchWriteCount;
                trace = fopen("logs/scene-battle-collision.log", "ab");
                if (trace != NULL)
                {
                    fprintf(trace,
                            "scene_battle_control_state_write count=%u "
                            "pc=%08x lr=%08x addr=%08x size=%u value=%llx "
                            "prior=%u regs=%08x,%08x,%08x,%08x r9=%08x "
                            "sp=%08x stack=%08x,%08x,%08x,%08x,%08x,%08x,"
                            "%08x,%08x,%08x,%08x,%08x,%08x\n",
                            g_vmSceneControlStateWatchWriteCount, pc, lr,
                            start, size, value, (unsigned)prior,
                            r0, r1, r2, r3, Global_R9, sp,
                            stackWords[0], stackWords[1], stackWords[2],
                            stackWords[3], stackWords[4], stackWords[5],
                            stackWords[6], stackWords[7], stackWords[8],
                            stackWords[9], stackWords[10], stackWords[11]);
                    fclose(trace);
                }
            }
        }

        if (start < delegateStart + 12u && end > delegateStart &&
            g_vmSceneInputDelegateWatchWriteCount < 48u)
        {
            static const char *const delegateNames[3] = {
                "control_delegate", "input_callback", "touch_delegate"
            };
            u32 pc = 0;
            u32 lr = 0;
            u32 slot = (start - delegateStart) / sizeof(u32);
            u32 prior = 0;
            FILE *trace = NULL;

            if (slot >= 3u)
                slot = 2u;
            if (slot == 1u && value != 0)
                g_vmSceneInputCallbackLastObserved = (u32)value;
            (void)uc_mem_read(uc, delegateStart + slot * sizeof(u32),
                              &prior, sizeof(prior));
            (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
            ++g_vmSceneInputDelegateWatchWriteCount;
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_input_delegate_write count=%u field=%s "
                        "pc=%08x lr=%08x addr=%08x size=%u value=%llx "
                        "prior=%08x r9=%08x\n",
                        g_vmSceneInputDelegateWatchWriteCount,
                        delegateNames[slot], pc, lr, start, size, value,
                        prior, Global_R9);
                fclose(trace);
            }
        }

        if (start < watchStart + sizeof(u32) && end > watchStart &&
            g_vmMapControllerWatchWriteCount < 32u)
        {
            u32 pc = 0;
            u32 lr = 0;
            u32 previous = 0;
            u32 r0 = 0;
            u32 r1 = 0;
            u32 r2 = 0;
            u32 r3 = 0;
            u32 sp = 0;
            u32 stackWords[12] = {0};
            u32 stackWordCount = 0;

            (void)uc_mem_read(uc, watchStart, &previous, sizeof(previous));
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            uc_reg_read(uc, UC_ARM_REG_LR, &lr);
            uc_reg_read(uc, UC_ARM_REG_R0, &r0);
            uc_reg_read(uc, UC_ARM_REG_R1, &r1);
            uc_reg_read(uc, UC_ARM_REG_R2, &r2);
            uc_reg_read(uc, UC_ARM_REG_R3, &r3);
            uc_reg_read(uc, UC_ARM_REG_SP, &sp);
            vm_trace_map_controller_writer_context(uc, pc, r1);
            if (sp != 0 &&
                uc_mem_read(uc, sp, stackWords, sizeof(stackWords)) == UC_ERR_OK)
            {
                stackWordCount = (u32)(sizeof(stackWords) / sizeof(stackWords[0]));
            }
            ++g_vmMapControllerWatchWriteCount;
            printf("[info][map-controller] pointer_write count=%u pc=%08x lr=%08x last=%08x addr=%08x size=%u value=%llx previous=%08x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x\\n",
                   g_vmMapControllerWatchWriteCount, pc, lr, lastAddress,
                   start, size, value, previous, r0, r1, r2, r3, sp);
            vm_trace_map_controller_write(g_vmMapControllerWatchWriteCount,
                                          pc, lr, start, size,
                                          (uint64_t)value, previous,
                                          r0, r1, r2, r3, sp,
                                          stackWords, stackWordCount);
        }
    }
    if (type == UC_MEM_WRITE && g_shopReturnForensicsActive &&
        g_shopReturnForensicsGateWatchAddress != 0)
    {
        vm_shop_return_forensics_note_gate_write(uc, address, size, value);
    }
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
            u32 lr = 0;
            u32 r0 = 0;
            u32 sp = 0;
            u32 stackWords[4] = {0, 0, 0, 0};
            FILE *trace;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            uc_reg_read(uc, UC_ARM_REG_LR, &lr);
            uc_reg_read(uc, UC_ARM_REG_R0, &r0);
            uc_reg_read(uc, UC_ARM_REG_SP, &sp);
            if (sp != 0)
                (void)uc_mem_read(uc, sp, stackWords, sizeof(stackWords));
            ++g_hangupBusinessDelegateWatchWriteCount;
            trace = fopen("logs/hangup-protocol.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "[info][network] mock_hangup_business_delegate_write "
                        "generation=%u count=%u pc=%08x lr=%08x r0=%08x "
                        "sp=%08x stack=%08x,%08x,%08x,%08x last=%08x "
                        "addr=%08x size=%u value=%llx\n",
                        g_hangupBattleStateWatchGeneration,
                        g_hangupBusinessDelegateWatchWriteCount, pc, lr, r0,
                        sp, stackWords[0], stackWords[1], stackWords[2],
                        stackWords[3], lastAddress, start, size, value);
                fflush(trace);
                fclose(trace);
            }
        }
    }
    /* This is armed only while mmBattle is constructing the automatic-action
     * list.  It records the native writer of the candidate-count byte; it
     * never changes the guest state or the pending write. */
    if (type == UC_MEM_WRITE && g_hangupAutoCandidateWatchAddress != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_hangupAutoCandidateWatchAddress;
        u32 watchEnd = watchStart + sizeof(u8);
        if (start < watchEnd && end > watchStart &&
            g_hangupAutoCandidateWatchWriteCount < 24u)
        {
            u32 pc = 0;
            u32 lr = 0;
            u32 r0 = 0;
            u32 r1 = 0;
            u32 r2 = 0;
            u32 r3 = 0;
            u32 r9 = 0;
            FILE *trace;
            (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
            (void)uc_reg_read(uc, UC_ARM_REG_R0, &r0);
            (void)uc_reg_read(uc, UC_ARM_REG_R1, &r1);
            (void)uc_reg_read(uc, UC_ARM_REG_R2, &r2);
            (void)uc_reg_read(uc, UC_ARM_REG_R3, &r3);
            (void)uc_reg_read(uc, UC_ARM_REG_R9, &r9);
            ++g_hangupAutoCandidateWatchWriteCount;
            trace = fopen("logs/hangup-protocol.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "[info][network] mock_hangup_auto_candidate_write "
                        "count=%u pc=%08x lr=%08x last=%08x addr=%08x "
                        "size=%u value=%llx r0=%08x r1=%08x r2=%08x "
                        "r3=%08x r9=%08x\n",
                        g_hangupAutoCandidateWatchWriteCount, pc, lr,
                        lastAddress, start, size, value, r0, r1, r2, r3, r9);
                fflush(trace);
                fclose(trace);
            }
        }
    }
    if (type == UC_MEM_WRITE && g_hangupAutoBattleInitWatchAddress != 0)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_hangupAutoBattleInitWatchAddress;
        u32 watchEnd = watchStart + sizeof(u8);
        if (start < watchEnd && end > watchStart &&
            g_hangupAutoBattleInitWatchWriteCount < 24u)
        {
            u32 pc = 0;
            u32 lr = 0;
            FILE *trace;
            (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
            ++g_hangupAutoBattleInitWatchWriteCount;
            trace = fopen("logs/hangup-protocol.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "[info][network] mock_hangup_auto_battle_init_write "
                        "count=%u pc=%08x lr=%08x last=%08x addr=%08x "
                        "size=%u value=%llx\n",
                        g_hangupAutoBattleInitWatchWriteCount, pc, lr,
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
    if (type == UC_MEM_WRITE)
    {
        u32 start = (u32)address;
        u32 end = start + size;
        u32 watchStart = g_vmEquipmentEnhanceRulesWatchAddress;

        if (watchStart != 0 && start < watchStart + sizeof(u32) &&
            end > watchStart)
        {
            if (g_vmEquipmentEnhanceRulesWatchWriteCount < 16)
            {
                u32 pc = 0;
                u32 lr = 0;
                u32 sp = 0;
                u32 savedLr = 0;
                u32 callerLr = 0;
                u32 r0 = 0;
                u32 r1 = 0;
                uc_reg_read(uc, UC_ARM_REG_PC, &pc);
                uc_reg_read(uc, UC_ARM_REG_LR, &lr);
                uc_reg_read(uc, UC_ARM_REG_SP, &sp);
                uc_reg_read(uc, UC_ARM_REG_R0, &r0);
                uc_reg_read(uc, UC_ARM_REG_R1, &r1);
                if (sp != 0)
                {
                    (void)uc_mem_read(uc, sp, &savedLr, sizeof(savedLr));
                    /* AllocBufIfNull saves r4,r5,r7,lr before it calls the
                     * zeroing helper, so its own caller is four words above
                     * the helper's saved LR. */
                    (void)uc_mem_read(uc, sp + 16u, &callerLr,
                                      sizeof(callerLr));
                }
                printf("[info][equipment] enhance_rule_table_pointer_write count=%u pc=%08x lr=%08x saved_lr=%08x caller_lr=%08x sp=%08x last=%08x addr=%08x size=%u value=%llx r0=%08x r1=%08x\\n",
                       g_vmEquipmentEnhanceRulesWatchWriteCount + 1, pc,
                       lr, savedLr, callerLr, sp, lastAddress, start, size, value,
                       r0, r1);
            }
            ++g_vmEquipmentEnhanceRulesWatchWriteCount;
        }
        vm_automation_trace_battle_watch_write(
            uc, address, size, value, "auto-flag",
            g_vmAutomationBattleAutoFlagWatchAddress, sizeof(u8),
            &g_vmAutomationBattleAutoFlagWatchWriteCount);
        vm_automation_trace_battle_watch_write(
            uc, address, size, value, "overlay",
            g_vmAutomationBattleOverlayWatchAddress, sizeof(u8),
            &g_vmAutomationBattleOverlayWatchWriteCount);
        vm_automation_trace_battle_watch_write(
            uc, address, size, value, "phase",
            g_vmAutomationBattlePhaseWatchAddress, sizeof(u16),
            &g_vmAutomationBattlePhaseWatchWriteCount);
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
    u32 faultRegs[10] = {0};
    u32 sp = 0;
    u32 lr = 0;
    u32 pc = 0;
    u32 cpsr = 0;
    u32 stackWords[16] = {0};
    u32 stackWordCount = 0;
    int faultRegIds[10] = {
        UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9,
    };
    FILE *faultTrace = NULL;

    (void)data;
    for (u32 i = 0; i < 10u; ++i)
        (void)uc_reg_read(uc, faultRegIds[i], &faultRegs[i]);
    (void)uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    (void)uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    (void)uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
    if (sp >= STACK_ADDRESS && sp <= STACK_ADDRESS + 0x100000u &&
        uc_mem_read(uc, sp, stackWords, sizeof(stackWords)) == UC_ERR_OK)
    {
        stackWordCount = (u32)(sizeof(stackWords) / sizeof(stackWords[0]));
    }
    faultTrace = fopen("logs/guest-memory-fault.log", "ab");
    if (faultTrace != NULL)
    {
        fprintf(faultTrace,
                "guest_memory_fault address=%llx type=%d size=%u value=%llx "
                "pc=%08x last=%08x lr=%08x sp=%08x cpsr=%08x",
                (unsigned long long)address, (int)type, size,
                (unsigned long long)(uint64_t)value, pc, lastAddress, lr, sp,
                cpsr);
        for (u32 i = 0; i < 10u; ++i)
            fprintf(faultTrace, " r%u=%08x", i, faultRegs[i]);
        for (u32 i = 0; i < stackWordCount; ++i)
            fprintf(faultTrace, " stack%u=%08x", i, stackWords[i]);
        fputc('\n', faultTrace);
        fflush(faultTrace);
        fclose(faultTrace);
    }
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
