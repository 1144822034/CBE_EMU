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
#include "screen_lifecycle.h"
#ifdef CBE_CLIENT_ONLY
#include "automation_png.h"
#endif
static bool vm_file_try_download_named_resource(const char *normalizedPath);
static void vm_automation_note_scene_number_draw(const char *atlas, bool alpha,
                                                  int width, int height,
                                                  int dstX, int dstY,
                                                  u32 callerReturn);
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

/*
 * Scenario automation is intentionally separate from the legacy `--autotest`
 * time script below.  It may observe VM state/PC/packets, but every action is
 * still delivered through keyEvent()/mouseEvent() and therefore through the
 * normal VM hardware-event queue.
 */
typedef enum
{
    VM_AUTOMATION_SCENARIO_NONE = 0,
    VM_AUTOMATION_SCENARIO_SHOP_RETURN_HANGUP,
    /* Control for the shop-return regression.  It uses the identical scene
     * control and battle assertion, but intentionally omits the shop round
     * trip so the first differing client lifecycle edge is observable. */
    VM_AUTOMATION_SCENARIO_DIRECT_HANGUP,
    /* Starts from a clean isolated cache and proves the native title-module
     * 18/6 update transaction reaches its installed terminal state. */
    VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE,
    /* A read-only visual/protocol probe for the authored n_telestone scene.
     * It deliberately stops at the native scene boundary; it does not fake a
     * 16/1 request or call the mmGame action callback directly. */
    VM_AUTOMATION_SCENARIO_SCENE_TELEPORT_STONE_PROBE,
    /* Enters the player-1-captured 梦境三层 resource fixture and waits for a
     * real map-layer numeric draw. It sends no scene action after the native
     * scene-ready boundary. */
    VM_AUTOMATION_SCENARIO_DREAM_CLOCK_PROBE,
    /* Replays the observed NPC 30406 entry route through the client's own
     * nearest-NPC prompt and task-hall selection before observing the same
     * dream-scene number region.  It is deliberately separate from the
     * direct-login control above. */
    VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE,
    /* Opens the visible equipment toolbar icon exactly once after the native
     * scene boundary, then waits for CalcEquipStatBonus' read-only table
     * capture.  It is a data-forensics scenario, not a synthetic stat test. */
    VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_RULES_PROBE,
    /* A one-step discovery companion for the rules probe.  It opens the
     * native backpack toolbar target with a role that owns one enhanced
     * armor instance, captures the rendered list, and stops.  It exists to
     * establish the real screen/target contract before scheduling any
     * enhancement-menu touch in the rules probe. */
    VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE,
    /* Traverses the real backpack enhancement entry only through the first
     * 29/1 preview response.  It never presses the material submit button,
     * so the isolated role's enhancement level and inventory remain intact. */
    VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE,
    /* Isolated battle regressions.  Both begin with the same native scene
     * hangup control as the direct control; the first presses the visible
     * battle auto-cancel target, the second waits for a three-enemy round to
     * close natively and enter the next hangup round. */
    VM_AUTOMATION_SCENARIO_HANGUP_AUTO_CANCEL,
    VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL,
    /* Proves the native scene-hangup exit contract without any reward-panel
     * input: 25/2(result=1,type=1) must reach mmBattle:0x8996, then the
     * final action reaches 0x5E92 and emits the client's own 25/5. */
    VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT
} vm_automation_scenario;

typedef enum
{
    VM_AUTOMATION_STAGE_BOOT_CONFIRM = 0,
    VM_AUTOMATION_STAGE_WAIT_TITLE_MODULE_UPDATE,
    VM_AUTOMATION_STAGE_WAIT_TIMED_TITLE_BOOTSTRAP,
    VM_AUTOMATION_STAGE_WAIT_TITLE_LOGIN_DISPATCH,
    VM_AUTOMATION_STAGE_WAIT_ROLE_LIST,
    VM_AUTOMATION_STAGE_WAIT_INITIAL_SCENE,
    VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW,
    VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_PROMPT,
    VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_DIALOG,
    VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_TARGET_SCENE,
    VM_AUTOMATION_STAGE_WAIT_EQUIPMENT_ENHANCE_RULES,
    VM_AUTOMATION_STAGE_WAIT_SHOP_OPEN,
    VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN,
    VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN_PRE_HANGUP_CAPTURE,
    VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE,
    VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE_SCREEN,
    VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_VISIBLE,
    VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_CANCEL_RESPONSE,
    VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_TERMINAL,
    VM_AUTOMATION_STAGE_WAIT_HANGUP_NATIVE_AUTO_EXIT,
    VM_AUTOMATION_STAGE_PASSED,
    VM_AUTOMATION_STAGE_FAILED
} vm_automation_stage;

typedef struct
{
    vm_automation_scenario scenario;
    vm_automation_stage stage;
    u8 active;
    u8 finished;
    u8 timedTitleDriver;
    u8 titleUpdateCompleteSeen;
    u8 titleScreenInitialized;
    u8 titleLoginResponseSeen;
    u8 titleLoginResponseCaptured;
    u8 titleModuleUpdateChunkSeen;
    u8 titleModuleUpdateCompleted;
    u8 titleModuleUpdateLifecycleRejectSeen;
    u8 titleRoleListSeen;
    u8 initialScenePacketSeen;
    u8 dreamClockProbeArmed;
    u8 dreamClockTopRightNumberSeen;
    u8 dreamClockCandidateDrawCount;
    u8 dreamNpcPromptPcSeen;
    u8 dreamNpcPromptConfirmSent;
    u8 dreamNpcDialogParserPcSeen;
    u8 dreamNpcDialogConfirmSent;
    u8 dreamNpcInstanceEnterResponseSeen;
    u8 dreamNpcTargetScenePacketSeen;
    u8 shopStatusSeen;
    u8 shopMoneySeen;
    u8 shopReturnSeen;
    u8 shopReturnPreHangupCaptured;
    /* A 30/2 carrying posinfo is a true scene re-enter: CBE replaces the
     * scene screen, then scene_runtime_init_and_sync emits its next 25/5
     * subset request.  Both native lifecycle edges must complete before this
     * regression sends the Hangup touch. */
    u8 shopReturnSceneReinitSeen;
    u8 shopReturnFollowupSeen;
    u8 hangupBattleResponseSeen;
    u8 hangupAutoEnableResponseSeen;
    u8 hangupAutoDisableResponseSeen;
    u8 hangupSettlementResponseSeen;
    u8 hangupNativeAutoExitResponseSeen;
    u8 hangupNativeAutoExitParserSeen;
    u8 hangupNativeAutoExitPcSeen;
    u8 hangupNativeManualExitPcSeen;
    u8 hangupNativeExitUplinkSeen;
    u8 hangupAutoVisibleCaptured;
    u8 battleStartHandlerSeen;
    u8 battleSceneCharListSeen;
    u8 equipmentEnhanceDetailTapSent;
    u8 equipmentEnhanceConfirmTapSent;
    u8 equipmentEnhanceStage1ResponseSeen;
    u8 equipmentEnhanceBackpackGridCommitted;
    u8 equipmentEnhanceBackpackCategoryBefore;
    u8 equipmentEnhanceBackpackCategoryCurrent;
    u8 equipmentEnhanceEquipmentCategoryRendered;
    u8 capturePending;
    u8 captureLabelIndex;
    u8 exitRequested;
    u32 renderFrames;
    u32 hangupSettlementInputCount;
    u32 hangupSettlementActionResponseCount;
    u32 hangupSettlementResponseCount;
    u32 hangupNativeAutoExitResponseSequence;
    u32 hangupNativeAutoExitParserFrame;
    u32 hangupNativeAutoExitPcFrame;
    u32 hangupNativeManualExitPcFrame;
    u32 hangupNativeExitUplinkFrame;
    u32 stageFrame;
    u32 stageStartedMs;
    u32 totalStartedMs;
    u32 titleUpdateFrame;
    u32 titleModuleUpdateTotalSize;
    u32 titleModuleUpdateChecksum;
    u32 initialScenePacketFrame;
    u32 dreamClockProbeArmFrame;
    u32 dreamClockLastCandidateFrame;
    u32 dreamClockLastCandidateCallerReturn;
    u32 dreamNpcInstanceEnterResponseSequence;
    u32 dreamNpcTargetScenePacketSequence;
    u32 dreamNpcTargetScenePacketFrame;
    int dreamClockLastCandidateX;
    int dreamClockLastCandidateY;
    u32 titleScreenInitFrame;
    /* These are observed screen descriptors, not hard-coded client addresses.
     * The return touch must be delivered only after the actual 30/2 callback
     * has restored the same scene owner that accepted the first toolbar tap. */
    u32 initialSceneScreen;
    u32 shopScreen;
    /* A 14/14 + 14/4 packet observed at the host transport boundary is not
     * yet an interactive shop.  Record its frame and wait for the owning
     * shop screen to render after the guest callback before sending Back. */
    u32 shopDataPacketFrame;
    u32 shopDataPacketSequence;
    u32 shopReturnPacketFrame;
    u32 shopReturnPacketSequence;
    u32 shopReturnSceneReinitFrame;
    u32 shopReturnFollowupFrame;
    u32 shopReturnFollowupSequence;
    u32 battleStartHandlerFrame;
    u32 battleSceneCharListFrame;
    u32 equipmentEnhanceDetailTapFrame;
    u32 equipmentEnhanceBackpackGridCommittedFrame;
    u32 equipmentEnhanceEquipmentCategoryRenderedFrame;
    u32 equipmentEnhanceStage1ResponseFrame;
    u32 equipmentEnhanceStage1ResponseSequence;
    u32 battleModuleSpBf;
    u32 battleModuleCodeBase;
    u32 hangupBattleResponseCount;
    u32 battleActionResponseCount;
    u32 captureIndex;
    u32 inputCount;
    u32 timedInputCount;
    u32 maxSteps;
    u32 totalTimeoutMs;
    u32 stepTimeoutMs;
    char artifactDir[512];
    char pendingCaptureLabel[48];
} vm_automation_state;

static vm_automation_state g_vmAutomation;

static u8 g_vmEquipmentEnhanceRulesCaptured = 0;
u32 g_vmEquipmentEnhanceRulesWatchAddress = 0;
u32 g_vmEquipmentEnhanceRulesWatchWriteCount = 0;
static const char *vm_automation_scenario_name(void);
static void vm_automation_note_startup_pc(u32 pc);
static void vm_automation_note_screen_init(u32 screen, u32 initEntry,
                                           u32 logicEntry, u32 renderEntry);
static void vm_automation_note_network_response(const u8 *packet, u32 packetLen,
                                                 u32 eventType, u32 sequence);
static void vm_automation_note_uplink(const u8 *packet, u32 packetLen);
static void vm_automation_note_dream_npc_entry_pc(u32 pc);
/* Startup-module update investigation only.  The helper is read-only and is
 * called exclusively from the opt-in automation trace path. */
static void vm_autotest_trace_update_state(const char *phase, u32 sequence,
                                           const u8 *packet, u32 packetLen);
static void vm_autotest_trace_update_guest_callback(const char *phase,
                                                     u32 responsePtr,
                                                     u32 responseLen);
static void vm_automation_note_battle_handler_pc(u32 localPc,
                                                  u32 moduleSpBf);
static void vm_automation_note_battle_native_exit_pc(u32 pc);
static void vm_automation_note_battle_scene_char_list(u32 sequence,
                                                       u32 localPc);
static void vm_automation_render_complete(void);
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
/* A read-only trace anchor for the currently validated mmGame code image.
 * It is set only after the live sub_604/sub_8A8 pair matches the installed
 * module's instruction fingerprints. */
static u32 g_vmTraceMmGameInputCodeBase = 0;
/* Latest nonzero client write to Global_R9+0x5D28, retained only as a
 * read-only instruction-trace anchor.  hookRam.c never feeds it back into
 * guest memory or dispatch. */
u32 g_vmSceneInputCallbackLastObserved = 0;
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
#ifdef CBE_PLATFORM_ANDROID
static volatile int g_cbeLastRunStatus = UC_ERR_OK;
#endif
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
    u8 hasHangupBattleStart;
    u8 hangupBattleStartDirect;
    u8 hangupResponseObjectCount;
    u8 hangupResponseParsedCount;
    u8 reserved0;
    u32 hangupResponseSequence;
    u32 hangupResponseLength;
} vm_net_remote_observation;

typedef struct
{
    u8 active;
    u8 fired;
    u8 deferredToNextTick;
    u16 delayTicks;
    /* A remote frame may be received while a preceding frame from the same
     * transaction is still queued. This is a host scheduler boundary, not
     * guest state: do not dispatch before this scheduler tick. */
    u32 notBeforeTick;
    u32 eventType;
    u32 r0;
    u32 r1;
    u32 r2;
    u32 callback;
    u32 context;
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
static u32 g_netMockEnterGameOffset = 0;
static u32 g_netMockEnterGameChecksum = 0;
static u8 g_loginVmCodeDumped = 0;
static u8 g_loginVmTouchCodeDumped = 0;
static u8 g_loginVmScreen67cCodeDumped = 0;
static u8 g_loginVmScreen687CodeDumped = 0;
static u32 g_netUpLinkData = 0;
static u32 g_netDownLinkData = 0;
static u32 g_netCurrentObject = 0;
static u32 g_netDebugReadWindow = 0;
/* Temporary, read-only evidence window for the manual enhancement-shop
 * return reproduction.  It is armed only after the client receives the
 * exact mall catalog sequence, then records a bounded set of transport and
 * touch-gate observations. */
u8 g_shopReturnForensicsActive = 0;
u32 g_shopReturnForensicsGateWatchAddress = 0;
u32 g_shopReturnForensicsGateWriteCount = 0;
static u32 g_shopReturnForensicsTraceCount = 0;
static u32 g_shopReturnForensicsCatalogSequence = 0;
static u8 g_shopReturnForensicsActorQuerySeen = 0;
static u32 g_shopReturnForensicsCatalogScreen = 0;
static u32 g_shopReturnForensicsReturnLogicEntry = 0;
static u32 g_shopReturnForensicsReturnLogicEntryHits = 0;
static u32 g_shopReturnForensicsReturnLogicGateHits = 0;
static const u32 VM_GAME_NET_BUSINESS_CALLBACK = 0x01012e4d;
/* Read-only memory-watch target armed immediately after JianghuOL.CBE's
 * HandleBattleEnterReq writes its state=3 marker.  hookRam.c only observes
 * writes that overlap this address; it never changes guest memory. */
u32 g_hangupBattleStateWatchAddress = 0;
u32 g_hangupBattleStateWatchGeneration = 0;
u32 g_hangupBattleStateWatchWriteCount = 0;
u32 g_hangupSceneModeWatchAddress = 0;
u32 g_hangupSceneModeWatchWriteCount = 0;
u32 g_hangupBusinessDelegateWatchAddress = 0;
u32 g_hangupBusinessDelegateWatchWriteCount = 0;
u32 g_hangupAutoCandidateWatchAddress = 0;
u32 g_hangupAutoCandidateWatchWriteCount = 0;
u32 g_hangupAutoBattleInitWatchAddress = 0;
u32 g_hangupAutoBattleInitWatchWriteCount = 0;
/* Automation-only, read-only watch for the shared game-context flag consumed
 * by mmBattle:BattleScene_MainLoop at gameState+1133.  It is armed before
 * the shop round trip so the actual writer can be attributed to a client
 * lifecycle transition rather than inferred from the later stuck battle UI. */
u32 g_vmAutomationGameLoadingGateWatchAddress = 0;
u32 g_vmAutomationGameLoadingGateWatchWriteCount = 0;
/* Auto-battle regressions arm these only after the native 4/5 start parser
 * has entered mmBattle.  hookRam.c records writes without changing the
 * packet, battle state, or input sequence. */
u32 g_vmAutomationBattleAutoFlagWatchAddress = 0;
u32 g_vmAutomationBattleOverlayWatchAddress = 0;
u32 g_vmAutomationBattlePhaseWatchAddress = 0;
u32 g_vmAutomationBattleAutoFlagWatchWriteCount = 0;
u32 g_vmAutomationBattleOverlayWatchWriteCount = 0;
u32 g_vmAutomationBattlePhaseWatchWriteCount = 0;
static u32 g_hangupTransitionTraceStepCount = 0;
/* Capture the CBM caller context before ROM dispatch restores the main CBE
 * R9.  It is used only to map the observed CleanupPaymentCb caller back to
 * the module-local offset that owns the transition. */
static u32 g_hangupCleanupCallerLr = 0;
static u32 g_hangupCleanupCallerR9 = 0;
static u32 g_hangupBattleModuleTraceCount = 0;
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
#if defined(CBE_PLATFORM_ANDROID)
static char g_mockServiceHost[64] = "121.40.139.236";
#else
static char g_mockServiceHost[64] = "192.168.0.108";
#endif
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
/* The native 4/11 acknowledgement only changes the client's battle input
 * state.  The client timer subsequently sends its empty 4/12 replay request;
 * the service therefore retains the last accepted replayable choice for the
 * active account/role and answers that real request through the normal 4/2
 * action builders. */
static u8 g_vm_net_mock_battle_auto_enabled = 0;
static u8 g_vm_net_mock_battle_auto_last_operation_valid = 0;
static u32 g_vm_net_mock_battle_auto_last_operation_role_id = 0;
static u32 g_vm_net_mock_battle_auto_last_operation_index = 0;
static u32 g_vm_net_mock_battle_auto_last_operation_operate = 0;
/* Request-local guard: a 4/12 replay is routed through the same 4/2
 * builders but must not manufacture a new remembered manual selection. */
static u8 g_vm_net_mock_battle_auto_replay_inflight = 0;
/* Request-local observation written by the common 4/6 builder and consumed
 * by terminal action-display ordering only.  It is deliberately not role
 * state: the value never survives a request/context boundary. */
static u8 g_vm_net_mock_battle_action6_emitted_count = 0;
/* The scene-hangup terminal boundary must not share the network event that
 * enqueues the final 4/6 action list.  The visible 4/7 settlement remains
 * inline with that action so the client's type-3 death callback can consume
 * it; after the player confirms the panel, the native 25/5 request becomes
 * the lifecycle boundary.  This timestamp preserves the action-display
 * delay for non-panel terminal paths. */
static u32 g_vm_net_mock_battle_terminal_close_not_before_tick = 0;
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
static void vm_autotest_arm_equipment_enhance_rules_watch(void);
static void vm_autotest_note_equipment_enhance_rules_pc(u32 pc);
static bool vm_net_mock_append_battle_terminal_subtype8_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_battle_terminal_case4_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_battle_terminal_case9_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_battle_terminal_case11_object(u8 *out, u32 outCap, u32 *pos);
static u32 vm_net_mock_build_battle_auto11_toggle_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap);
static u32 vm_net_mock_build_battle_auto12_replay_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap);
static u32 vm_net_mock_min_u32(u32 a, u32 b);
static uc_err scheduler_dispatch_net_tasks(void);
/* Root-cause forensic probe for a native 4/7 settlement.  It is intentionally
 * read-only: the packet, callback and guest state remain client-owned. */
static void vm_hangup_vital_forensics_capture_response(const u8 *packet,
                                                        u32 packetLen,
                                                        u32 eventType,
                                                        u32 sequence,
                                                        u32 responsePtr,
                                                        u32 callback);
static void vm_hangup_vital_forensics_callback_begin(u32 eventType,
                                                      u32 responsePtr,
                                                      u32 callback);
static void vm_hangup_vital_forensics_callback_end(u32 eventType,
                                                    u32 responsePtr,
                                                    u32 callback);
static void vm_hangup_vital_forensics_note_pc(u32 pc);
/* Narrow, read-only evidence for battle-insight use responses.  The probe is
 * armed only by 25/6, 25/7, or the observed 2/10 follow-up responses, records
 * whether 25/6 also contains 1/1/6, and never participates in packet delivery
 * or guest-state mutation. */
static void vm_battle_insight_forensics_capture_response(const u8 *packet,
                                                          u32 packetLen,
                                                          u32 eventType,
                                                          u32 sequence,
                                                          u32 responsePtr,
                                                          u32 callback,
                                                          u32 context,
                                                          u32 connectId);
static void vm_battle_insight_forensics_callback_begin(u32 eventType,
                                                        u32 responsePtr,
                                                        u32 callback,
                                                        u32 context);
static void vm_battle_insight_forensics_callback_end(u32 eventType,
                                                      u32 responsePtr,
                                                      u32 callback,
                                                      u32 context,
                                                      uc_err callbackErr);
static void vm_battle_insight_forensics_note_pc(u32 pc);
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

/*
 * Normal client calls all return through PROGRAM_EXIT_ADDR.  Unicorn 2.1.4
 * has a native exit-address facility for this platform concern; using it
 * avoids making every instruction in a loaded CBM cross a host code hook just
 * to recognize that one address.  Failure is non-fatal to compatibility: the
 * caller keeps the historical full-range code hook in that case.
 */
static bool vm_enable_program_exit_control(uc_engine *uc)
{
    uint64_t exits[1] = { PROGRAM_EXIT_ADDR };
    uc_err err;

    err = uc_ctl_exits_enable(uc);
    if (err != UC_ERR_OK)
    {
        printf("[warn][emu] explicit-exit enable failed: %u (%s); using code hook\n",
               err, uc_strerror(err));
        return false;
    }
    err = uc_ctl_set_exits(uc, exits, sizeof(exits) / sizeof(exits[0]));
    if (err != UC_ERR_OK)
    {
        printf("[warn][emu] explicit-exit configure failed: %u (%s); using code hook\n",
               err, uc_strerror(err));
        (void)uc_ctl_exits_disable(uc);
        return false;
    }
    return true;
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
    g_vmTraceMmGameInputCodeBase = 0;
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

/* The load-manager ABI is the authority for a CBM's code and static-data
 * bases.  Keep a bounded, read-only record while that contract is recovered;
 * do not derive either base from an arbitrary screen callback. */
static void vm_dl_trace_loader_call(const char *phase, u32 index)
{
    static u32 traceCount = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r9 = 0;
    u32 lr = 0;
    FILE *trace;

    if (traceCount >= 96)
        return;
    uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    trace = fopen("logs/dl-context-trace.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "[info][dl-context] count=%u tick=%u phase=%s idx=%u "
            "r0=%08x r1=%08x r2=%08x r3=%08x r9=%08x lr=%08x "
            "current_app=%u current_spbf=%08x remembered_spbf=%08x "
            "loaded=%u",
            traceCount, g_schedulerTick, phase ? phase : "-", index,
            r0, r1, r2, r3, r9, lr, g_vmDlCurrAppId,
            vm_dl_current_sp_bf(), g_dlSpBf, g_vmDlLoadedCount);
    for (u32 i = 0; i < g_vmDlLoadedCount; ++i)
    {
        fprintf(trace, " app[%u]=%u:%08x/%08x/%08x", i,
                g_vmDlLoadedApps[i].appId, g_vmDlLoadedApps[i].buffer,
                g_vmDlLoadedApps[i].spBf,
                g_vmDlLoadedApps[i].context);
    }
    fputc('\n', trace);
    fflush(trace);
    fclose(trace);
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

/* The client transport may need to deliver two parser transactions that came
 * from one remote reply.  Reserve queue capacity before accepting either one:
 * delivering only the first transaction would make the guest observe a
 * partial protocol reply. */
static bool scheduler_has_free_net_task_slots(u32 required)
{
    u32 freeSlots = 0;

    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (!g_netTasks[i].active)
            ++freeSlots;
    }
    return freeSlots >= required;
}


static void vm_shop_return_forensics_log(const char *phase, u32 eventType,
                                         u32 r0, u32 callback, u32 context)
{
    u8 gate = 0xff;
    u32 managerCallback = 0;
    u32 touchCallback = 0;
    FILE *trace;

    if (!g_shopReturnForensicsActive ||
        g_shopReturnForensicsTraceCount >= 160)
    {
        return;
    }
    if (Global_R9 != 0)
    {
        (void)uc_mem_read(MTK, Global_R9 + 0x9594, &gate, sizeof(gate));
        (void)uc_mem_read(MTK, Global_R9 + 0x95cc, &managerCallback,
                          sizeof(managerCallback));
        (void)uc_mem_read(MTK, Global_R9 + 0x95e0, &touchCallback,
                          sizeof(touchCallback));
    }
    trace = fopen("logs/shop-return-input-v2.log", "ab");
    if (trace == NULL)
        return;
    ++g_shopReturnForensicsTraceCount;
    fprintf(trace,
            "[info][shop-return-v2] count=%u tick=%u phase=%s event=%u "
            "r0=%08x callback=%08x context=%08x gate=%u manager_cb=%08x "
            "touch_cb=%08x active=%08x top=%08x depth=%u catalog_seq=%u "
            "actor_query=%u last=%08x\n",
            g_shopReturnForensicsTraceCount, g_schedulerTick,
            phase ? phase : "-", eventType, r0, callback, context, gate,
            managerCallback, touchCallback, g_currentScreenThis,
            g_screenStackCount ? g_screenStack[g_screenStackCount - 1] : 0,
            g_screenStackCount, g_shopReturnForensicsCatalogSequence,
            g_shopReturnForensicsActorQuerySeen, lastAddress);
    fflush(trace);
    fclose(trace);
}

static void vm_shop_return_forensics_arm_from_catalog(u32 sequence)
{
    if (Global_R9 == 0)
        return;
    g_shopReturnForensicsActive = 1;
    g_shopReturnForensicsGateWatchAddress = Global_R9 + 0x9594;
    g_shopReturnForensicsGateWriteCount = 0;
    g_shopReturnForensicsTraceCount = 0;
    g_shopReturnForensicsCatalogSequence = sequence;
    g_shopReturnForensicsActorQuerySeen = 0;
    g_shopReturnForensicsCatalogScreen = g_currentScreenThis;
    g_shopReturnForensicsReturnLogicEntry = 0;
    g_shopReturnForensicsReturnLogicEntryHits = 0;
    g_shopReturnForensicsReturnLogicGateHits = 0;
    vm_shop_return_forensics_log("catalog-arm", 7, 0, 0, 0);
}

static void vm_shop_return_forensics_note_uplink(const u8 *packet,
                                                 u32 packetLen,
                                                 u32 connectId)
{
    u16 objectLen;

    if (!g_shopReturnForensicsActive || packet == NULL || packetLen < 9 ||
        packet[0] != 'W' || packet[1] != 'T' || packet[4] != 1 ||
        packet[5] != 1 || packet[6] != 14)
    {
        return;
    }
    objectLen = (u16)(((u16)packet[7] << 8) | packet[8]);
    if (objectLen < 5 || objectLen + 4 != packetLen)
        return;
    g_shopReturnForensicsActorQuerySeen = 1;
    vm_shop_return_forensics_log("actor-query-uplink", 0, connectId, 0, 0);
}

static void vm_shop_return_forensics_note_downlink(const u8 *packet,
                                                   u32 packetLen,
                                                   u32 eventType,
                                                   u32 sequence,
                                                   u32 connectId)
{
    u16 declaredLen;
    u16 objectLen;
    u32 offset;
    static const u8 subtypes[4] = {14, 4, 5, 6};

    if (packet == NULL || packetLen < 11 || packet[0] != 'W' ||
        packet[1] != 'T')
    {
        return;
    }
    declaredLen = (u16)(((u16)packet[2] << 8) | packet[3]);
    if (declaredLen != packetLen || packet[4] < 4)
        return;
    offset = 5;
    u32 catalogIndex = 0;
    while (offset < packetLen)
    {
        if (offset + 6 > packetLen)
            return;
        objectLen = (u16)(((u16)packet[offset + 4] << 8) |
                          packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return;
        if (packet[offset] == 1 && packet[offset + 1] == 14 &&
            packet[offset + 2] == subtypes[catalogIndex])
        {
            ++catalogIndex;
            if (catalogIndex == 4)
            {
                (void)eventType;
                (void)connectId;
                vm_shop_return_forensics_arm_from_catalog(sequence);
                return;
            }
        }
        else
        {
            catalogIndex = packet[offset] == 1 && packet[offset + 1] == 14 &&
                           packet[offset + 2] == subtypes[0] ? 1 : 0;
        }
        offset += objectLen;
    }
}

void vm_shop_return_forensics_note_gate_write(uc_engine *uc, uint64_t address,
                                              uint32_t size, int64_t value)
{
    u32 start = (u32)address;
    u32 end = start + size;
    u32 pc = 0;
    u32 lr = 0;
    FILE *trace;

    if (!g_shopReturnForensicsActive ||
        g_shopReturnForensicsGateWatchAddress == 0 ||
        start >= g_shopReturnForensicsGateWatchAddress + sizeof(u8) ||
        end <= g_shopReturnForensicsGateWatchAddress ||
        g_shopReturnForensicsGateWriteCount >= 32)
    {
        return;
    }
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    ++g_shopReturnForensicsGateWriteCount;
    trace = fopen("logs/shop-return-input-v2.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][shop-return-v2] gate-write count=%u tick=%u pc=%08x "
            "lr=%08x last=%08x addr=%08x size=%u value=%llx\n",
            g_shopReturnForensicsGateWriteCount, g_schedulerTick, pc, lr,
            lastAddress, start, size, value);
    fflush(trace);
    fclose(trace);
}

static void vm_shop_return_forensics_note_pc(u32 pc)
{
    u32 inputA = 0;
    u32 inputB = 0;

    if (!g_shopReturnForensicsActive || pc != 0x01003d3c)
        return;
    uc_reg_read(MTK, UC_ARM_REG_R1, &inputA);
    uc_reg_read(MTK, UC_ARM_REG_R2, &inputB);
    vm_shop_return_forensics_log("touch-dispatch", 0, inputA, inputB, 0);
}

/* The returned mmGame screen has a distinct logic entry from the mall screen.
 * Observe its actual module-relative state read in-flight; this trace never
 * changes the guest registers or memory. */
static void vm_shop_return_forensics_note_mmgame_input_pc(u32 pc)
{
    u32 r9 = 0;
    u32 r6 = 0;
    u32 manager = 0;
    u8 gate = 0xff;
    int16_t pending = 0;
    FILE *trace;

    if (!g_shopReturnForensicsActive ||
        g_shopReturnForensicsReturnLogicEntry == 0 ||
        g_shopReturnForensicsTraceCount >= 160)
    {
        return;
    }

    if (pc == (g_shopReturnForensicsReturnLogicEntry & ~1u) &&
        g_shopReturnForensicsReturnLogicEntryHits < 16)
    {
        uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        ++g_shopReturnForensicsReturnLogicEntryHits;
        vm_shop_return_forensics_log("mmgame-input-entry", 0, r9,
                                      g_shopReturnForensicsReturnLogicEntry,
                                      g_currentScreenModuleBase);
        return;
    }

    /* At +8 the observed mmGame logic has materialized r6 = module_global.
     * Its next instructions read [r6+0x20], then the manager +8/+0xc gates. */
    if (pc != ((g_shopReturnForensicsReturnLogicEntry & ~1u) + 8u) ||
        g_shopReturnForensicsReturnLogicGateHits >= 16)
    {
        return;
    }

    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    if (r6 != 0)
    {
        (void)uc_mem_read(MTK, r6 + 0x20u, &manager, sizeof(manager));
        if (manager != 0)
        {
            (void)uc_mem_read(MTK, manager + 8u, &pending, sizeof(pending));
            (void)uc_mem_read(MTK, manager + 12u, &gate, sizeof(gate));
        }
    }
    trace = fopen("logs/shop-return-input-v2.log", "ab");
    if (trace == NULL)
        return;
    ++g_shopReturnForensicsReturnLogicGateHits;
    ++g_shopReturnForensicsTraceCount;
    fprintf(trace,
            "[info][shop-return-v2] count=%u tick=%u phase=mmgame-input-gate "
            "module=mmGameMstarWqvga.cbm pc=%08x local=%08x r9=%08x "
            "screen_module_r9=%08x r6=%08x manager=%08x pending=%d gate=%u\n",
            g_shopReturnForensicsTraceCount, g_schedulerTick, pc,
            r9 >= 0x14000u ? pc - (r9 - 0x14000u) : 0xffffffffu, r9,
            g_currentScreenModuleBase, r6, manager, pending, gate);
    fflush(trace);
    fclose(trace);
}

static bool scheduler_queue_net_event(u32 eventType, u32 r0, u32 r1, u32 r2,
                                      u32 callback, u32 context)
{
    static u32 s_netQueueObserveCount = 0;
    u32 activeBefore = scheduler_count_active_net_tasks();
    if (eventType == 5 || eventType == 7 || eventType == 8 || eventType == 9)
    {
        vm_shop_return_forensics_log("net-queue-request", eventType, r0,
                                      callback, context);
    }
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netTasks[i].active && g_netTasks[i].eventType == eventType && g_netTasks[i].r0 == r0 && g_netTasks[i].r1 == r1 && g_netTasks[i].r2 == r2 && g_netTasks[i].callback == callback && g_netTasks[i].context == context)
        {
            if (eventType == 5 || eventType == 7 || eventType == 8 ||
                eventType == 9)
            {
                vm_shop_return_forensics_log("net-queue-dedup", eventType,
                                              r0, callback, context);
            }
            return true;
        }
    }
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (!g_netTasks[i].active)
        {
            g_netTasks[i].active = 1;
            g_netTasks[i].fired = 0;
            g_netTasks[i].deferredToNextTick = 0;
            g_netTasks[i].delayTicks = eventType == 7 ? 0 : 6;
            g_netTasks[i].notBeforeTick = g_schedulerTick;
            g_netTasks[i].eventType = eventType;
            g_netTasks[i].r0 = r0;
            g_netTasks[i].r1 = r1;
            g_netTasks[i].r2 = r2;
            g_netTasks[i].callback = callback;
            g_netTasks[i].context = context;
            memset(&g_netTasks[i].remoteObservation, 0,
                   sizeof(g_netTasks[i].remoteObservation));
            DEBUG_PRINT("[probe_net] queue event=%u r0=%x r1=%x r2=%x cb=%x ctx=%x tick=%u last=%x\n", eventType, r0, r1, r2, callback, context, g_schedulerTick, lastAddress);
            if (s_netQueueObserveCount < 100)
            {
                ++s_netQueueObserveCount;
                vm_autotest_note("net_queue event=%u r0=%08x r1=%08x r2=%08x cb=%08x ctx=%08x\n",
                                 eventType, r0, r1, r2, callback, context);
            }
            if (eventType == 5 || eventType == 7 || eventType == 8 ||
                eventType == 9)
            {
                vm_shop_return_forensics_log("net-queue-accepted", eventType,
                                              r0, callback, context);
            }
            return true;
        }
    }
    if (eventType == 5 || eventType == 7 || eventType == 8 || eventType == 9)
    {
        vm_shop_return_forensics_log("net-queue-full", eventType, r0,
                                      callback, context);
    }
    return false;
}

/* The scheduler task table is only mutated on the emulator thread.  Once two
 * vacant entries have been observed, the two calls below cannot race with a
 * producer.  The response ranges start at distinct nonzero pointers, so
 * neither request can be de-duplicated against the other. */
static bool scheduler_queue_net_event_pair_atomic(
    u32 firstEvent, u32 firstR0, u32 firstR1, u32 firstR2,
    u32 secondEvent, u32 secondR0, u32 secondR1, u32 secondR2,
    u32 callback, u32 context)
{
    if (firstR0 == 0 || secondR0 == 0 || firstR0 == secondR0 ||
        !scheduler_has_free_net_task_slots(2))
    {
        return false;
    }
    if (!scheduler_queue_net_event(firstEvent, firstR0, firstR1, firstR2,
                                   callback, context))
    {
        return false;
    }
    /* Capacity was reserved before the first insertion.  A false result here
     * would mean an internal scheduler invariant was violated; do not expose
     * a partial response as a recoverable protocol state. */
    if (!scheduler_queue_net_event(secondEvent, secondR0, secondR1, secondR2,
                                   callback, context))
    {
        printf("[error][network] net_pair_queue_invariant_failed first=%u/%08x second=%u/%08x callback=%08x context=%08x\n",
               firstEvent, firstR0, secondEvent, secondR0, callback, context);
        return false;
    }
    return true;
}

/* Preserve a real transport boundary when one remote completion contains two
 * independent data frames. Recursive scheduler flushes may run before the
 * current tick returns, so delayTicks alone cannot represent this boundary. */
static bool scheduler_defer_net_event_to_next_tick(u32 eventType, u32 r0,
                                                    u32 callback, u32 context)
{
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        vm_net_task *task = &g_netTasks[i];
        if (!task->active || task->fired || task->eventType != eventType ||
            task->r0 != r0 || task->callback != callback ||
            task->context != context)
        {
            continue;
        }
        task->notBeforeTick = g_schedulerTick + 1u;
        task->deferredToNextTick = 1;
        return true;
    }
    return false;
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

/* Only active while scheduler_dispatch_net_tasks invokes the guest callback
 * for the uniquely identified hangup-start response.  This is host-side,
 * read-only forensics: it observes which branch the existing CBE parser
 * takes, and never changes the packet, registers, or CBE state. */
typedef struct
{
    u8 active;
    u8 packetInitEntered;
    u8 packetGuardReturned;
    u8 packetUnpackError;
    u8 businessFollowup;
    u8 businessFallback;
    u8 actorMoveCase;
    u8 typeResponseCase;
    u8 entryCount;
    u8 battleStartReadySeen;
    u32 sequence;
    u32 mmBattleCodeBase;
    u32 entrySwitchWords[4];
} vm_hangup_protocol_parser_trace;

static vm_hangup_protocol_parser_trace g_hangupProtocolParserTrace;

/* HandleBattleStartMsg(0x66CC) publishes its ready flag while the event-7
 * callback is still on the stack.  BattleScene_MainLoop consumes that flag on
 * a later render.  Retain only the relocation base and sequence for that
 * narrow, read-only post-callback observation window. */
typedef struct
{
    u8 active;
    u8 mainLoopSeen;
    u8 firstPoolPcSeen;
    u8 charListGateSeen;
    u8 loadingDialogDrawSeen;
    u8 drawMainSeen;
    u8 frameDelegateSeen;
    u8 sceneTickEntrySeen;
    u8 sceneTickReadySeen;
    u8 sceneTickLoadingFallbackSeen;
    u8 battleCompletionCheckSeen;
    u8 battleCompletionPresentSeen;
    u8 postDelegateStateSeen;
    u32 sequence;
    u32 codeBase;
} vm_hangup_battle_render_trace;

static vm_hangup_battle_render_trace g_hangupBattleRenderTrace;

static void vm_hangup_protocol_parser_trace_begin(
    const vm_net_remote_observation *observation)
{
    memset(&g_hangupProtocolParserTrace, 0,
           sizeof(g_hangupProtocolParserTrace));
    if (observation == NULL || !observation->hasHangupBattleStart)
        return;
    /* Keep a completed start response's render observation alive across
     * ordinary scene-poll/auto-action callbacks.  Only a new 4/5 start can
     * supersede that lifecycle window. */
    memset(&g_hangupBattleRenderTrace, 0,
           sizeof(g_hangupBattleRenderTrace));
    g_hangupProtocolParserTrace.active = 1;
    g_hangupProtocolParserTrace.sequence =
        observation->hangupResponseSequence;
    /* The state watch is armed before module/screen transitions and therefore
     * sees unrelated event-7 packets.  The mmBattle boundary belongs only to
     * this identified hangup-start callback; start its budget here. */
    g_hangupBattleModuleTraceCount = 0;
}

static void vm_hangup_protocol_parser_trace_end(
    const vm_net_remote_observation *observation)
{
    FILE *trace;
    u8 renderArmed = 0;
    if (g_hangupProtocolParserTrace.active &&
        g_hangupProtocolParserTrace.battleStartReadySeen &&
        g_hangupProtocolParserTrace.mmBattleCodeBase != 0)
    {
        g_hangupBattleRenderTrace.active = 1;
        g_hangupBattleRenderTrace.sequence =
            g_hangupProtocolParserTrace.sequence;
        g_hangupBattleRenderTrace.codeBase =
            g_hangupProtocolParserTrace.mmBattleCodeBase;
        renderArmed = 1;
    }
    trace = NULL;
    if (g_hangupProtocolParserTrace.active ||
        (observation != NULL && observation->hasHangupBattleStart))
    {
        trace = fopen("logs/hangup-protocol.log", "ab");
    }
    if (trace != NULL)
    {
        fprintf(trace,
                "[info][network] mock_hangup_mmBattle_render_arm sequence=%u "
                "parser_active=%u ready=%u code_base=%08x armed=%u\n",
                g_hangupProtocolParserTrace.sequence,
                g_hangupProtocolParserTrace.active,
                g_hangupProtocolParserTrace.battleStartReadySeen,
                g_hangupProtocolParserTrace.mmBattleCodeBase, renderArmed);
        fflush(trace);
        fclose(trace);
    }
    memset(&g_hangupProtocolParserTrace, 0,
           sizeof(g_hangupProtocolParserTrace));
    /* The request-side state watch is armed before the asynchronous response
     * is queued.  Unrelated event-7 callbacks can run in between; they must
     * not disarm the watch or the first client write that resets the battle
     * state is lost.  Only the matching direct hangup response owns the end
     * of this forensic window. */
    if (observation != NULL && observation->hasHangupBattleStart &&
        observation->hangupBattleStartDirect)
    {
        g_hangupBattleStateWatchAddress = 0;
        g_hangupSceneModeWatchAddress = 0;
        g_hangupBusinessDelegateWatchAddress = 0;
        g_hangupAutoCandidateWatchAddress = 0;
        g_hangupAutoBattleInitWatchAddress = 0;
    }
}

static void vm_hangup_battle_state_watch_note_pc(u32 pc)
{
    u32 r9 = 0;
    u16 state = 0xffff;
    FILE *trace;

    /* HandleBattleEnterReq: the STRH at 0x01015EBE stores 3; the next
     * instruction is 0x01015EC2.  Arm only after that real client write. */
    if (pc != 0x01015EC2)
        return;
    uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0)
        r9 = Global_R9;
    if (r9 == 0)
        return;
    g_hangupBattleStateWatchAddress = r9 + 23682;
    g_hangupBattleStateWatchWriteCount = 0;
    g_hangupSceneModeWatchAddress = r9 + 19638;
    g_hangupSceneModeWatchWriteCount = 0;
    g_hangupBusinessDelegateWatchAddress = r9 + 23856;
    g_hangupBusinessDelegateWatchWriteCount = 0;
    g_hangupTransitionTraceStepCount = 0;
    g_hangupBattleModuleTraceCount = 0;
    ++g_hangupBattleStateWatchGeneration;
    (void)uc_mem_read(MTK, g_hangupBattleStateWatchAddress,
                      &state, sizeof(state));
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_battle_state_arm generation=%u "
            "addr=%08x state=%u pc=%08x\n",
            g_hangupBattleStateWatchGeneration,
            g_hangupBattleStateWatchAddress, state, pc);
    fflush(trace);
    fclose(trace);
}

/* SetMapCtrlField6180 is the CBE-side callback setter used while a screen
 * module is selected.  The previous write watch begins only after
 * HandleBattleEnterReq, but this setter may install the battle callback
 * earlier in the transition.  Observe this single instruction directly so
 * the module caller can be attributed without changing its callback, state,
 * packet, or timing. */
static void vm_hangup_delegate_register_trace_note_pc(u32 pc)
{
    u32 r0 = 0;
    u32 lr = 0;
    u32 r9 = 0;
    u32 sp = 0;
    u32 stackWords[4] = {0, 0, 0, 0};
    FILE *trace;

    if (pc != 0x0101808EU)
        return;
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    if (sp != 0)
        (void)uc_mem_read(MTK, sp, stackWords, sizeof(stackWords));
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_business_delegate_setter "
            "pc=%08x callback=%08x lr=%08x r9=%08x sp=%08x "
            "stack=%08x,%08x,%08x,%08x\n",
            pc, r0, lr, r9, sp, stackWords[0], stackWords[1],
            stackWords[2], stackWords[3]);
    fflush(trace);
    fclose(trace);
}

/* The missing summary panel must be selected by the game's own UI dispatch,
 * not by host-side state changes.  These five mmBattle entry points cover
 * that selection and its native 25-series request/response handlers.  Keep
 * this probe read-only and tightly capped: it records whether the real input
 * path reaches them, with the game flags it observes at entry. */
static void vm_hangup_ui_dispatch_trace_note_pc(u32 pc)
{
    static u8 entryCount[5] = {0, 0, 0, 0, 0};
    static const u32 localPcs[5] = {0x1064Au, 0xA4D4u, 0xAB76u, 0xBFE6u, 0x8996u};
    u32 codeBase = g_hangupProtocolParserTrace.mmBattleCodeBase;
    u32 localPc;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r9 = 0;
    u32 lr = 0;
    u32 gameState = 0;
    u8 gameFlags[5] = {0xff, 0xff, 0xff, 0xff, 0xff};
    FILE *trace;
    u32 index;

    if (codeBase == 0)
        codeBase = g_hangupBattleRenderTrace.codeBase;
    if (codeBase == 0 || !vm_is_pool_entry(pc) || pc < codeBase)
        return;
    localPc = pc - codeBase;
    for (index = 0; index < (u32)(sizeof(localPcs) / sizeof(localPcs[0])); ++index)
    {
        if (localPc == localPcs[index])
            break;
    }
    if (index == (u32)(sizeof(localPcs) / sizeof(localPcs[0])) ||
        entryCount[index] >= 4)
    {
        return;
    }
    ++entryCount[index];
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    (void)uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    (void)uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (r9 != 0 &&
        uc_mem_read(MTK, r9 + 8272u, &gameState, sizeof(gameState)) == UC_ERR_OK &&
        gameState != 0)
    {
        (void)uc_mem_read(MTK, gameState + 1136u, gameFlags,
                          sizeof(gameFlags));
    }
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_ui_dispatch entry=%u local_pc=%04x "
            "base=%08x args=%08x/%08x/%08x/%08x r9=%08x lr=%08x "
            "game=%08x flags=%u/%u/%u/%u/%u\n",
            entryCount[index], localPc, codeBase, r0, r1, r2, r3, r9, lr,
            gameState, gameFlags[0], gameFlags[1], gameFlags[2],
            gameFlags[3], gameFlags[4]);
    fflush(trace);
    fclose(trace);
}

/* The CBE arithmetic exception is raised inside RandRange when its inclusive
 * upper bound is below its lower bound.  Record the original caller and
 * bounds once per process; this is a read-only crash forensics point and does
 * not alter the calculation or allow the guest to continue past it. */
static void vm_hangup_rand_range_trace_note_pc(u32 pc)
{
    static u8 captured = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r9 = 0;
    u32 lr = 0;
    u32 gameState = 0;
    u8 gameFlags[5] = {0xff, 0xff, 0xff, 0xff, 0xff};
    FILE *trace;

    if (captured || pc != 0x01004CC2u)
        return;
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    /* RandRange divides by (upper - lower + 1).  Capture only the exact
     * empty inclusive range that makes that divisor zero, not harmless
     * startup randomisation. */
    if (r1 + 1u != r0)
        return;
    captured = 1;
    (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (r9 != 0 &&
        uc_mem_read(MTK, r9 + 8272u, &gameState, sizeof(gameState)) == UC_ERR_OK &&
        gameState != 0)
    {
        (void)uc_mem_read(MTK, gameState + 1136u, gameFlags,
                          sizeof(gameFlags));
    }
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_rand_range caller=%08x bounds=%d/%d "
            "r9=%08x game=%08x flags=%u/%u/%u/%u/%u\n",
            lr, (int)r0, (int)r1, r9, gameState, gameFlags[0],
            gameFlags[1], gameFlags[2], gameFlags[3], gameFlags[4]);
    fflush(trace);
    fclose(trace);
}

/*
 * `combatinfo` is parsed in the mmBattle 0x78E0-0x79DA settlement range.
 * The service already records the authoritative per-battle reward before it
 * sends that field, but the visible panel is still assigning the decoded
 * words incorrectly.  Observe the guest's own writes immediately after that
 * parser runs.  This is intentionally read-only and bounded: it neither
 * changes the stream, client state, registers, nor any UI/battle scheduling.
 *
 * Keep the values anonymous (slot520, etc.) here.  Their semantics must come
 * from the client's actual writes, not from a guessed wire layout.
 */
typedef struct
{
    u8 active;
    u8 sampleCount;
    u8 valid;
    u32 codeBase;
    u32 game;
    u16 slot516;
    u16 slot518;
    u32 slot520;
    u32 slot524;
    u32 slot528;
    u32 slot532;
    u16 slot536;
    u16 slot538;
    u8 slot1205;
} vm_hangup_combatinfo_read_trace;

static vm_hangup_combatinfo_read_trace g_hangupCombatinfoReadTrace;

/* The packaged CBM is packed, while this exact parser executes from the VM
 * pool.  Export only the small, already-loaded instruction range needed to
 * resolve its cursor advances.  The artifact is a read-only diagnostic code
 * slice; it contains neither player state nor packet payloads. */
static void vm_hangup_combatinfo_dump_parser_once(u32 codeBase)
{
    static u8 attempted = 0;
    u8 code[0x180];
    FILE *dump;
    FILE *trace;

    if (attempted || codeBase == 0)
        return;
    attempted = 1;
    if (uc_mem_read(MTK, codeBase + 0x78E0u, code, sizeof(code)) != UC_ERR_OK)
        return;
    dump = fopen("logs/hangup-combatinfo-parser-current.bin", "wb");
    if (dump == NULL)
        return;
    if (fwrite(code, 1, sizeof(code), dump) != sizeof(code))
    {
        fclose(dump);
        return;
    }
    fflush(dump);
    fclose(dump);
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_combatinfo_parser_dump "
            "base=%08x local=78e0-7a5f bytes=%u path=logs/hangup-combatinfo-parser-current.bin\n",
            codeBase, (u32)sizeof(code));
    fflush(trace);
    fclose(trace);
}

/* At 0x7908 the initialized stream object is in R4 and its generic numeric
 * reader is the vtable entry at R5+0x20.  Its implementation owns the wire
 * cursor contract, so export only that function's loaded instructions once.
 * This remains a host-side read of executable pages. */
static void vm_hangup_combatinfo_dump_reader_once(void)
{
    static u8 attempted = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r6 = 0;
    u32 r7 = 0;
    u32 reader = 0;
    u8 code[0x100];
    FILE *dump;
    FILE *trace;

    if (attempted)
        return;
    attempted = 1;
    (void)uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    (void)uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    (void)uc_reg_read(MTK, UC_ARM_REG_R6, &r6);
    (void)uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    if (r5 == 0 ||
        uc_mem_read(MTK, r5 + 0x20u, &reader, sizeof(reader)) != UC_ERR_OK ||
        reader == 0 ||
        uc_mem_read(MTK, reader & ~1u, code, sizeof(code)) != UC_ERR_OK)
    {
        return;
    }
    dump = fopen("logs/hangup-combatinfo-reader-current.bin", "wb");
    if (dump == NULL)
        return;
    if (fwrite(code, 1, sizeof(code), dump) != sizeof(code))
    {
        fclose(dump);
        return;
    }
    fflush(dump);
    fclose(dump);
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_combatinfo_reader_dump "
            "r4=%08x r5=%08x r6=%08x r7=%08x reader=%08x bytes=%u "
            "path=logs/hangup-combatinfo-reader-current.bin\n",
            r4, r5, r6, r7, reader, (u32)sizeof(code));
    fflush(trace);
    fclose(trace);
}

static void vm_hangup_combatinfo_read_trace_note_pc(u32 pc)
{
    vm_hangup_combatinfo_read_trace current;
    u32 codeBase = g_hangupProtocolParserTrace.mmBattleCodeBase;
    u32 localPc;
    u32 r9 = 0;
    u32 game = 0;
    FILE *trace;
    int changed;

    if (codeBase == 0)
        codeBase = g_hangupBattleRenderTrace.codeBase;
    if (codeBase == 0 || !vm_is_pool_entry(pc) || pc < codeBase)
        return;
    localPc = pc - codeBase;
    if (localPc == 0x78E0u)
    {
        memset(&g_hangupCombatinfoReadTrace, 0,
               sizeof(g_hangupCombatinfoReadTrace));
        g_hangupCombatinfoReadTrace.active = 1;
        g_hangupCombatinfoReadTrace.codeBase = codeBase;
        vm_hangup_combatinfo_dump_parser_once(codeBase);
    }
    if (!g_hangupCombatinfoReadTrace.active ||
        g_hangupCombatinfoReadTrace.codeBase != codeBase ||
        localPc < 0x78E0u || localPc > 0x7A20u ||
        g_hangupCombatinfoReadTrace.sampleCount >= 14u)
    {
        return;
    }
    if (localPc == 0x7908u)
        vm_hangup_combatinfo_dump_reader_once();
    (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0 ||
        uc_mem_read(MTK, r9 + 8272u, &game, sizeof(game)) != UC_ERR_OK ||
        game == 0)
    {
        return;
    }
    memset(&current, 0, sizeof(current));
    current.active = 1;
    current.codeBase = codeBase;
    current.game = game;
    if (uc_mem_read(MTK, game + 516u, &current.slot516,
                    sizeof(current.slot516)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 518u, &current.slot518,
                    sizeof(current.slot518)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 520u, &current.slot520,
                    sizeof(current.slot520)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 524u, &current.slot524,
                    sizeof(current.slot524)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 528u, &current.slot528,
                    sizeof(current.slot528)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 532u, &current.slot532,
                    sizeof(current.slot532)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 536u, &current.slot536,
                    sizeof(current.slot536)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 538u, &current.slot538,
                    sizeof(current.slot538)) != UC_ERR_OK ||
        uc_mem_read(MTK, game + 1205u, &current.slot1205,
                    sizeof(current.slot1205)) != UC_ERR_OK)
    {
        return;
    }
    changed = !g_hangupCombatinfoReadTrace.valid ||
        current.game != g_hangupCombatinfoReadTrace.game ||
        current.slot516 != g_hangupCombatinfoReadTrace.slot516 ||
        current.slot518 != g_hangupCombatinfoReadTrace.slot518 ||
        current.slot520 != g_hangupCombatinfoReadTrace.slot520 ||
        current.slot524 != g_hangupCombatinfoReadTrace.slot524 ||
        current.slot528 != g_hangupCombatinfoReadTrace.slot528 ||
        current.slot532 != g_hangupCombatinfoReadTrace.slot532 ||
        current.slot536 != g_hangupCombatinfoReadTrace.slot536 ||
        current.slot538 != g_hangupCombatinfoReadTrace.slot538 ||
        current.slot1205 != g_hangupCombatinfoReadTrace.slot1205;
    if (!changed)
        return;
    current.valid = 1;
    current.sampleCount = g_hangupCombatinfoReadTrace.sampleCount + 1u;
    g_hangupCombatinfoReadTrace = current;
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_combatinfo_client_write "
            "local_pc=%04x r9=%08x game=%08x slots="
            "%u/%u/%u/%u/%u/%u/%u/%u/%u sample=%u\n",
            localPc, r9, game, current.slot516, current.slot518,
            current.slot520, current.slot524, current.slot528,
            current.slot532, current.slot536, current.slot538,
            current.slot1205, current.sampleCount);
    fflush(trace);
    fclose(trace);
}

/* The reported null-address fault is mmBattle+0x2908, reached while the
 * native 25/2 candidate builder is still running (before any 4/7 settlement
 * can be parsed).  Capture the exact instruction window and the candidate
 * node immediately before executing it.  This does not alter the object,
 * callback, scene list, registers or input path. */
static void vm_hangup_candidate_fault_trace_note_pc(u32 pc)
{
    int appIndex;
    u32 codeBase = 0;
    u32 regs[8] = {0};
    u32 lr = 0;
    u8 code[0x80];
    u8 argumentObject[0x80];
    u8 node[0x40];
    FILE *dump;
    FILE *trace;

    /* A native 25/2 can begin after the previous battle trace was retired.
     * Its owner is still unambiguous: derive the base from the loaded module
     * containing the current PC, rather than borrowing a prior callback's
     * lifecycle state. */
    appIndex = vm_dl_find_loaded_index_by_pc(pc);
    if (appIndex >= 0)
        codeBase = g_vmDlLoadedApps[appIndex].buffer;
    if (codeBase == 0 || pc != codeBase + 0x2908u)
        return;
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &regs[0]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &regs[1]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R2, &regs[2]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R3, &regs[3]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R4, &regs[4]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R5, &regs[5]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R6, &regs[6]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R7, &regs[7]);
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    memset(argumentObject, 0, sizeof(argumentObject));
    memset(node, 0, sizeof(node));
    if (regs[2] != 0)
        (void)uc_mem_read(MTK, regs[2], argumentObject,
                          sizeof(argumentObject));
    if (regs[4] != 0)
        (void)uc_mem_read(MTK, regs[4], node, sizeof(node));
    if (uc_mem_read(MTK, codeBase + 0x28C0u, code, sizeof(code)) == UC_ERR_OK)
    {
        dump = fopen("logs/hangup-candidate-fault-current.bin", "wb");
        if (dump != NULL)
        {
            (void)fwrite(code, 1, sizeof(code), dump);
            (void)fwrite(argumentObject, 1, sizeof(argumentObject), dump);
            (void)fwrite(node, 1, sizeof(node), dump);
            fflush(dump);
            fclose(dump);
        }
    }
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_candidate_fault_preexec "
            "local_pc=2908 app=%u base=%08x regs=%08x/%08x/%08x/%08x/"
            "%08x/%08x/%08x/%08x lr=%08x node=%02x%02x%02x%02x"
            "%02x%02x%02x%02x path=logs/hangup-candidate-fault-current.bin\n",
            g_vmDlLoadedApps[appIndex].appId, codeBase, regs[0], regs[1],
            regs[2], regs[3], regs[4], regs[5],
            regs[6], regs[7], lr, node[0], node[1], node[2], node[3],
            node[4], node[5], node[6], node[7]);
    fflush(trace);
    fclose(trace);
}

/* mmBattle:sub_716E chooses the next automatic action from the byte at
 * module+13392.  Its zero value is the first invalid state observed at the
 * crash.  Arm observation-only watches as the response callback begins, so
 * both the candidate rebuild and the preceding battle-control initializer
 * retain their original writer. */
static void vm_hangup_auto_candidate_watch_note_pc(u32 pc)
{
    u32 codeBase = g_hangupProtocolParserTrace.mmBattleCodeBase;
    u32 r9 = 0;
    u32 current = 0xff;
    FILE *trace;

    if (codeBase == 0)
        codeBase = g_hangupBattleRenderTrace.codeBase;
    if (codeBase == 0 || pc != codeBase + 0x17ACu)
        return;
    (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
    if (r9 == 0)
        return;
    g_hangupAutoCandidateWatchAddress = r9 + 13392u;
    g_hangupAutoCandidateWatchWriteCount = 0;
    g_hangupAutoBattleInitWatchAddress = r9 + 10303u;
    g_hangupAutoBattleInitWatchWriteCount = 0;
    (void)uc_mem_read(MTK, g_hangupAutoCandidateWatchAddress,
                      &current, sizeof(current));
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_auto_candidate_watch_arm "
            "module=%08x candidate_addr=%08x candidate_initial=%u "
            "init_addr=%08x\n",
            r9, g_hangupAutoCandidateWatchAddress, current & 0xffu,
            g_hangupAutoBattleInitWatchAddress);
    fflush(trace);
    fclose(trace);
}

static void vm_hangup_transition_capture_pre_restore(u32 pc)
{
    if (g_hangupBattleStateWatchAddress == 0 || pc != 0x010448DE)
        return;
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &g_hangupCleanupCallerLr);
    (void)uc_reg_read(MTK, UC_ARM_REG_R9, &g_hangupCleanupCallerR9);
}

static void vm_hangup_format_code_bytes(const u8 *bytes, u32 length,
                                        char *out, u32 outCap)
{
    u32 pos = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (bytes == NULL)
        return;
    for (u32 i = 0; i < length && pos + 2 < outCap; ++i)
    {
        int written = snprintf(out + pos, outCap - pos, "%02x", bytes[i]);
        if (written != 2)
            break;
        pos += 2;
    }
}

static void vm_hangup_transition_trace_note_pc(u32 pc)
{
    u16 battleState = 0xffff;
    u8 sceneMode = 0xff;
    u32 businessDelegate = 0;
    u32 paymentCallback = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 callerModuleR9 = 0;
    u32 callerCodeBase = 0;
    u32 callerLocalOffset = 0xffffffffu;
    u32 paymentLocalOffset = 0xffffffffu;
    u32 callerCodeStart = 0;
    u8 callerCodeBytes[32] = {0};
    u8 paymentCodeBytes[16] = {0};
    char callerCodeHex[sizeof(callerCodeBytes) * 2 + 1] = {0};
    char paymentCodeHex[sizeof(paymentCodeBytes) * 2 + 1] = {0};
    u32 stackWords[6] = {0};
    char sceneName[33] = {0};
    char modulePath[64] = {0};
    u16 modulePendingMode = 0xffff;
    u32 activeScreen = 0;
    FILE *trace;

    if (g_hangupBattleStateWatchAddress == 0 ||
        g_hangupTransitionTraceStepCount >= 64)
    {
        return;
    }
    switch (pc)
    {
    case 0x01015EC2: /* HandleBattleEnterReq has committed battle state 3. */
    case 0x010037A6: /* CleanupPaymentCbWrap entry; LR identifies the real owner. */
    case 0x01015EE0: /* HandleBattleEnterReq calls CleanupPaymentCbWrap(5). */
    case 0x01012DC6: /* actor move-info case 9 requests scene mode 3/4. */
    case 0x01018296: /* scene trigger countdown requests scene mode 3/4. */
    case 0x010448DE: /* CleanupPaymentCb entry before SetSceneFlag19638. */
    case 0x010448E6: /* SetSceneFlag19638(5) has returned. */
    case 0x010448F0: /* Invoke the registered payment/module cleanup callback. */
    case 0x01003CFC: /* ProcessSceneState entry. */
    case 0x01003D0C: /* ProcessSceneState is about to consume scene mode. */
    case 0x0100369C: /* HandleSceneTransition entry. */
    case 0x010036EA: /* Transition ownership branch. */
    case 0x01003768: /* Transition module callback invocation. */
    case 0x01036404: /* FormatSaveDataPath: R0 is the selected CBM slot. */
    case 0x01044A12: /* LoadPayCBMAsset: R1 is the actual CBM path. */
    case 0x0101809C: /* EnterSceneByMapName entry. */
    case 0x01018142: /* EnterSceneByMapName requests mode 3/4. */
        break;
    default:
        return;
    }
    (void)uc_mem_read(MTK, g_hangupBattleStateWatchAddress,
                      &battleState, sizeof(battleState));
    if (g_hangupSceneModeWatchAddress != 0)
        (void)uc_mem_read(MTK, g_hangupSceneModeWatchAddress,
                          &sceneMode, sizeof(sceneMode));
    if (g_hangupBusinessDelegateWatchAddress != 0)
        (void)uc_mem_read(MTK, g_hangupBusinessDelegateWatchAddress,
                          &businessDelegate, sizeof(businessDelegate));
    if (g_hangupBattleStateWatchAddress != 0)
    {
        const u32 traceR9 =
            g_hangupBattleStateWatchAddress - 23682u;
        (void)uc_mem_read(MTK, traceR9 + 10304u,
                          &modulePendingMode, sizeof(modulePendingMode));
        (void)uc_mem_read(MTK, traceR9 + 24304u,
                          &activeScreen, sizeof(activeScreen));
    }
    /* CleanupPaymentCb reads the callback at R9+39732.  Derive it from the
     * already verified request-side R9 watch instead of consulting host-side
     * module bookkeeping, which may refer to a different screen generation. */
    if (g_hangupBattleStateWatchAddress != 0)
        (void)uc_mem_read(MTK,
                          g_hangupBattleStateWatchAddress + (39732u - 23682u),
                          &paymentCallback, sizeof(paymentCallback));
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    (void)uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    (void)uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    if (sp != 0)
        (void)uc_mem_read(MTK, sp, stackWords, sizeof(stackWords));
    if (pc == 0x0101809C && r0 != 0)
    {
        u32 sceneLen = r1;
        if (sceneLen > sizeof(sceneName) - 1)
            sceneLen = sizeof(sceneName) - 1;
        (void)uc_mem_read(MTK, r0, sceneName, sceneLen);
        sceneName[sceneLen] = 0;
    }
    if (pc == 0x01044A12 && r1 != 0)
    {
        for (u32 i = 0; i + 1 < sizeof(modulePath); ++i)
        {
            if (uc_mem_read(MTK, r1 + i, &modulePath[i], 1) != UC_ERR_OK ||
                modulePath[i] == 0)
            {
                modulePath[i] = 0;
                break;
            }
        }
        modulePath[sizeof(modulePath) - 1] = 0;
    }
    if ((pc == 0x010448DE || pc == 0x010448E6 || pc == 0x010448F0) &&
        g_hangupCleanupCallerLr == lr &&
        g_hangupCleanupCallerR9 >= VM_Memory_Pool_ADDRESS + 0x14000u &&
        g_hangupCleanupCallerR9 < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        callerModuleR9 = g_hangupCleanupCallerR9;
        callerCodeBase = callerModuleR9 - 0x14000u;
        if ((lr & ~1u) >= callerCodeBase)
            callerLocalOffset = (lr & ~1u) - callerCodeBase;
        if ((paymentCallback & ~1u) >= callerCodeBase)
            paymentLocalOffset = (paymentCallback & ~1u) - callerCodeBase;
    }
    if (pc == 0x010448DE && vm_is_pool_entry(lr & ~1u))
    {
        callerCodeStart = (lr & ~1u) >= 16u ? (lr & ~1u) - 16u : 0;
        if (callerCodeStart != 0 &&
            uc_mem_read(MTK, callerCodeStart, callerCodeBytes,
                        sizeof(callerCodeBytes)) == UC_ERR_OK)
        {
            vm_hangup_format_code_bytes(callerCodeBytes,
                                        sizeof(callerCodeBytes),
                                        callerCodeHex, sizeof(callerCodeHex));
        }
        if (vm_is_pool_entry(paymentCallback & ~1u) &&
            uc_mem_read(MTK, paymentCallback & ~1u, paymentCodeBytes,
                        sizeof(paymentCodeBytes)) == UC_ERR_OK)
        {
            vm_hangup_format_code_bytes(paymentCodeBytes,
                                        sizeof(paymentCodeBytes),
                                        paymentCodeHex, sizeof(paymentCodeHex));
        }
    }
    ++g_hangupTransitionTraceStepCount;
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_transition_step "
            "generation=%u step=%u pc=%08x battle_state=%u "
            "scene_mode=%u business_cb=%08x payment_cb=%08x lr=%08x sp=%08x "
            "captured_r9=%08x module_r9=%08x code_base=%08x "
            "lr_off=%08x payment_off=%08x code_start=%08x "
            "caller_code=%s payment_code=%s "
            "args=%08x/%08x/%08x/%08x stack=%08x,%08x,%08x,%08x,%08x,%08x "
            "pending_mode=%u active_screen=%08x scene=%s module_path=%s\n",
            g_hangupBattleStateWatchGeneration,
            g_hangupTransitionTraceStepCount, pc, battleState,
            sceneMode, businessDelegate, paymentCallback, lr, sp,
            g_hangupCleanupCallerR9, callerModuleR9, callerCodeBase,
            callerLocalOffset, paymentLocalOffset, callerCodeStart,
            callerCodeHex[0] ? callerCodeHex : "-",
            paymentCodeHex[0] ? paymentCodeHex : "-",
            r0, r1, r2, r3,
            stackWords[0], stackWords[1], stackWords[2], stackWords[3],
            stackWords[4], stackWords[5], modulePendingMode, activeScreen,
            sceneName[0] ? sceneName : "-",
            modulePath[0] ? modulePath : "-");
    fflush(trace);
    fclose(trace);
}

static void vm_hangup_protocol_parser_trace_note_pc(u32 pc)
{
    u32 entry = 0;
    u32 switchWord = 0;

    if (!g_hangupProtocolParserTrace.active)
        return;
    switch (pc)
    {
    case 0x01012E72: /* net_business_dispatch_by_subcmd: packet gate returns */
        g_hangupProtocolParserTrace.packetGuardReturned = 1;
        break;
    case 0x01012E80: /* event_packet_init enters */
        g_hangupProtocolParserTrace.packetInitEntered = 1;
        break;
    case 0x01012EAC: /* switch (*(entry + 4)) */
        uc_reg_read(MTK, UC_ARM_REG_R4, &entry);
        if (entry != 0 &&
            g_hangupProtocolParserTrace.entryCount <
                sizeof(g_hangupProtocolParserTrace.entrySwitchWords) /
                    sizeof(g_hangupProtocolParserTrace.entrySwitchWords[0]) &&
            uc_mem_read(MTK, entry + 4, &switchWord,
                        sizeof(switchWord)) == UC_ERR_OK)
        {
            g_hangupProtocolParserTrace.entrySwitchWords[
                g_hangupProtocolParserTrace.entryCount++] = switchWord;
        }
        break;
    case 0x01012ED0: /* case 2: actor/move */
        g_hangupProtocolParserTrace.actorMoveCase = 1;
        break;
    case 0x01012EE0: /* case 4: type response */
        g_hangupProtocolParserTrace.typeResponseCase = 1;
        break;
    case 0x01012F78:
        g_hangupProtocolParserTrace.businessFollowup = 1;
        break;
    case 0x01012F8A:
        g_hangupProtocolParserTrace.packetUnpackError = 1;
        break;
    case 0x01012F8E:
        g_hangupProtocolParserTrace.businessFallback = 1;
        break;
    default:
        break;
    }
}

/*
 * The on-disk CBM is a packed module rather than the code image executed in
 * the VM pool.  This opt-in probe preserves the post-loader image for static
 * analysis.  It is deliberately one-shot and read-only: the guest module,
 * packet queue, registers and input queue are never changed.
 */
#define VM_HANGUP_FORENSICS_BATTLE_MODULE_SIZE 0x1614fu
#define VM_HANGUP_FORENSICS_BATTLE_MODULE_CHUNK 4096u

static void vm_hangup_forensics_dump_loaded_battle_module_once(u32 codeBase)
{
    const char *path = getenv("CBE_FORENSICS_BATTLE_MODULE_DUMP");
    static u8 attempted = 0;
    u8 buffer[VM_HANGUP_FORENSICS_BATTLE_MODULE_CHUNK];
    u32 offset = 0;
    FILE *dump;
    FILE *existing;
    FILE *trace;

    if (attempted || path == NULL || path[0] == 0 || codeBase == 0)
        return;
    attempted = 1;
    existing = fopen(path, "rb");
    if (existing != NULL)
    {
        fclose(existing);
        goto trace_failure;
    }
    dump = fopen(path, "wb");
    if (dump == NULL)
        goto trace_failure;

    while (offset < VM_HANGUP_FORENSICS_BATTLE_MODULE_SIZE)
    {
        u32 chunk = VM_HANGUP_FORENSICS_BATTLE_MODULE_SIZE - offset;

        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);
        if (uc_mem_read(MTK, codeBase + offset, buffer, chunk) != UC_ERR_OK ||
            fwrite(buffer, 1, chunk, dump) != chunk)
        {
            fclose(dump);
            goto trace_failure;
        }
        offset += chunk;
    }
    fclose(dump);
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace != NULL)
    {
        fprintf(trace,
                "[info][forensics] mmBattle_runtime_dump path=%s code_base=%08x bytes=%u trigger=local-7bd0 read_only=1\n",
                path, codeBase, VM_HANGUP_FORENSICS_BATTLE_MODULE_SIZE);
        fflush(trace);
        fclose(trace);
    }
    return;

trace_failure:
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace != NULL)
    {
        fprintf(trace,
                "[warn][forensics] mmBattle_runtime_dump_failed path=%s code_base=%08x trigger=local-7bd0\n",
                path, codeBase);
        fflush(trace);
        fclose(trace);
    }
}

/*
 * Read-only mmBattle boundary for the post-shop hangup investigation.
 *
 * The mmBattle callback keeps the main CBE R9 while its code is relocated into
 * the VM pool, so R9 cannot identify the CBM code base.  The installed business
 * callback is LoadBattleResourceData (local 0x17AC); derive the relocation base
 * from that confirmed entry instead.  This lets us prove whether the packet
 * callback and native HandleBattleStartMsg(0x66CC) receive the 4/5 object.  The
 * final snapshot also records the exact scene-node slot selected by subtype 5.
 * Nothing in this probe changes guest memory, registers, packet bytes, or event
 * ordering.
 */
static void vm_hangup_battle_module_trace_note_pc(u32 pc)
{
    u32 moduleR9 = 0;
    u32 codeBase = 0;
    u32 localPc = 0;
    u32 businessCallback = 0;
    u32 sp = 0;
    u32 regs[8] = {0};
    u32 stackWords[6] = {0};
    u32 sceneTable = 0;
    u32 nodeBase = 0;
    u32 nodeX = 0;
    u32 nodeY = 0;
    u16 leftCount = 0xffff;
    u16 targetIndex = 0xffff;
    u16 subtype = 0xffff;
    u16 ready = 0xffff;
    u8 nodeKind = 0xff;
    u8 nodeActive = 0xff;
    u32 objectKind = 0xffffffffu;
    u32 objectSubtype = 0xffffffffu;
    static const u8 mmBattleCallbackPrologue[8] = {
        0xf0, 0xb5, 0xff, 0xb0, 0xff, 0xb0, 0xd3, 0xb0};
    u8 callbackPrologue[sizeof(mmBattleCallbackPrologue)] = {0};
    FILE *trace;

    /* The request-side main-CBE watch is useful for the shop module
     * transition, but a direct scene hangup takes a different native entry
     * and does not arm it.  The uniquely identified 4/5 callback already
     * bounds this probe to one response, so requiring the unrelated watch
     * would hide the control-path snapshot we need for comparison. */
    if (!g_hangupProtocolParserTrace.active ||
        g_hangupBattleModuleTraceCount >= 48 || !vm_is_pool_entry(pc))
    {
        return;
    }
    (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
    if (Global_R9 == 0 ||
        uc_mem_read(MTK, Global_R9 + 23856u, &businessCallback,
                    sizeof(businessCallback)) != UC_ERR_OK ||
        !vm_is_pool_entry(businessCallback & ~1u) ||
        (businessCallback & ~1u) < 0x17ACu ||
        uc_mem_read(MTK, businessCallback & ~1u, callbackPrologue,
                    sizeof(callbackPrologue)) != UC_ERR_OK ||
        memcmp(callbackPrologue, mmBattleCallbackPrologue,
               sizeof(callbackPrologue)) != 0)
    {
        return;
    }
    codeBase = (businessCallback & ~1u) - 0x17ACu;
    if (pc < codeBase)
        return;
    localPc = pc - codeBase;
    if (localPc == 0x7BD0u)
        vm_hangup_forensics_dump_loaded_battle_module_once(codeBase);
    g_hangupProtocolParserTrace.mmBattleCodeBase = codeBase;
    if (localPc == 0x6BEEu)
        g_hangupProtocolParserTrace.battleStartReadySeen = 1;
    {
        int appIndex = vm_dl_find_loaded_index_by_pc(pc);
        u32 moduleSpBf = appIndex >= 0 ? g_vmDlLoadedApps[appIndex].spBf : 0;

        if (g_vmAutomation.active && localPc == 0x66CCu)
            g_vmAutomation.battleModuleCodeBase = codeBase;
        vm_automation_note_battle_handler_pc(localPc, moduleSpBf);
    }
    switch (localPc)
    {
    case 0x17AC: /* LoadBattleResourceData callback entry. */
    case 0x17B4: /* Event must be 7. */
    case 0x17B6: /* Branches away when event is not 7. */
    case 0x1810: /* Compare parsed object kind with 4. */
    case 0x1812: /* Branches away when object kind is not 4. */
    case 0x1820: /* About to dispatch a kind-4 object. */
    case 0x7BD0: /* HandleServerBattleCmd entry. */
    case 0x7DF0: /* Subtype 5/10 calls HandleBattleStartMsg. */
    case 0x66CC: /* HandleBattleStartMsg entry. */
    case 0x6726: /* Counts/subtype have been decoded. */
    case 0x674E: /* Scene target index/x/y have been decoded. */
    case 0x6792: /* Coordinate scan selected a replacement live node. */
    case 0x67BA: /* Scene target selection is complete. */
    case 0x6BEA: /* About to publish battle-start-ready = 1. */
    case 0x6BEE: /* HandleBattleStartMsg return. */
        break;
    default:
        return;
    }

    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &regs[0]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &regs[1]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R2, &regs[2]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R3, &regs[3]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R4, &regs[4]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R5, &regs[5]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R6, &regs[6]);
    (void)uc_reg_read(MTK, UC_ARM_REG_R7, &regs[7]);
    (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    if (sp != 0)
        (void)uc_mem_read(MTK, sp, stackWords, sizeof(stackWords));
    (void)uc_mem_read(MTK, moduleR9 + 13418u,
                      &leftCount, sizeof(leftCount));
    (void)uc_mem_read(MTK, moduleR9 + 13422u,
                      &targetIndex, sizeof(targetIndex));
    (void)uc_mem_read(MTK, moduleR9 + 13424u,
                      &subtype, sizeof(subtype));
    (void)uc_mem_read(MTK, moduleR9 + 13426u,
                      &ready, sizeof(ready));
    (void)uc_mem_read(MTK, moduleR9 + 13476u,
                      &sceneTable, sizeof(sceneTable));
    if (sceneTable != 0 && targetIndex < 25u)
    {
        nodeBase = sceneTable + 340u * targetIndex;
        (void)uc_mem_read(MTK, nodeBase + 240u, &nodeX, sizeof(nodeX));
        (void)uc_mem_read(MTK, nodeBase + 244u, &nodeY, sizeof(nodeY));
        (void)uc_mem_read(MTK, nodeBase + 315u, &nodeKind, sizeof(nodeKind));
        (void)uc_mem_read(MTK, nodeBase + 319u, &nodeActive, sizeof(nodeActive));
    }
    /* At the object switch R5 is the 88-byte parsed entry.  From the kind-4
     * dispatcher onward the same entry is argument R1. */
    {
        u32 object = localPc == 0x1810 || localPc == 0x1812 ||
                             localPc == 0x1820
                         ? regs[5]
                         : regs[1];
        if (object != 0)
        {
            (void)uc_mem_read(MTK, object + 4u,
                              &objectKind, sizeof(objectKind));
            (void)uc_mem_read(MTK, object + 8u,
                              &objectSubtype, sizeof(objectSubtype));
        }
    }

    ++g_hangupBattleModuleTraceCount;
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_mmBattle_start "
            "generation=%u step=%u local_pc=%04x code_base=%08x "
            "business_cb=%08x r9=%08x "
            "args=%08x/%08x/%08x/%08x regs4567=%08x/%08x/%08x/%08x "
            "stack=%08x,%08x,%08x,%08x,%08x,%08x "
            "object=%u/%u "
            "left=%u subtype=%u target=%u ready=%u scene_table=%08x "
            "node=%08x kind=%u active=%u pos=(%u,%u)\n",
            g_hangupBattleStateWatchGeneration,
            g_hangupBattleModuleTraceCount,
            localPc, codeBase, businessCallback, moduleR9,
            regs[0], regs[1], regs[2], regs[3],
            regs[4], regs[5], regs[6], regs[7],
            stackWords[0], stackWords[1], stackWords[2],
            stackWords[3], stackWords[4], stackWords[5],
            objectKind, objectSubtype,
            leftCount, subtype, targetIndex, ready, sceneTable,
            nodeBase, nodeKind, nodeActive, nodeX, nodeY);
    fflush(trace);
    fclose(trace);
}

/* This is deliberately separate from the event callback trace above.  The
 * 4/5 parser has returned before BattleScene_MainLoop can consume its ready
 * state.  IDA identifies local 0x62C as the call boundary for
 * BattleScene_CreateCharList; reaching it proves that the native battle UI,
 * rather than merely the wire parser, progressed beyond the fetch overlay. */
static void vm_hangup_battle_render_trace_note_pc(u32 pc)
{
    u32 localPc;
    FILE *trace;

    if (!g_hangupBattleRenderTrace.active)
    {
        return;
    }
    /* BattleScene_MainLoop delegates each render to CBE's
     * scene_runtime_tick(0x01014D30).  Its first readiness gate is independent
     * of the battle packet: R9+0x5C64+3/+4 must both be one, otherwise it
     * calls DrawLoadingScreen0 at 0x01014D80.  Observe that native branch
     * directly so a missing DrawBattleSceneMain is attributed to its owner. */
    if (pc == 0x01014D5Eu && !g_hangupBattleRenderTrace.sceneTickEntrySeen)
    {
        u32 r9 = 0;
        u8 sceneFlags[5] = {0xff, 0xff, 0xff, 0xff, 0xff};

        g_hangupBattleRenderTrace.sceneTickEntrySeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        if (r9 != 0)
            (void)uc_mem_read(MTK, r9 + 0x5C64u, sceneFlags,
                              sizeof(sceneFlags));
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_scene_tick_ready_gate "
                    "sequence=%u pc=%08x r9=%08x flags=%u/%u/%u/%u/%u\n",
                    g_hangupBattleRenderTrace.sequence, pc, r9,
                    sceneFlags[0], sceneFlags[1], sceneFlags[2],
                    sceneFlags[3], sceneFlags[4]);
            fflush(trace);
            fclose(trace);
        }
    }
    else if (pc == 0x01014E54u &&
             !g_hangupBattleRenderTrace.sceneTickReadySeen)
    {
        g_hangupBattleRenderTrace.sceneTickReadySeen = 1;
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_scene_tick_ready "
                    "sequence=%u pc=%08x action=continue-scene-tick\n",
                    g_hangupBattleRenderTrace.sequence, pc);
            fflush(trace);
            fclose(trace);
        }
    }
    else if (pc == 0x01014D80u &&
             !g_hangupBattleRenderTrace.sceneTickLoadingFallbackSeen)
    {
        g_hangupBattleRenderTrace.sceneTickLoadingFallbackSeen = 1;
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_scene_tick_loading_fallback "
                    "sequence=%u pc=%08x action=DrawLoadingScreen0\n",
                    g_hangupBattleRenderTrace.sequence, pc);
            fflush(trace);
            fclose(trace);
        }
    }
    if (!vm_is_pool_entry(pc) || pc < g_hangupBattleRenderTrace.codeBase)
        return;
    localPc = pc - g_hangupBattleRenderTrace.codeBase;
    if (!g_hangupBattleRenderTrace.firstPoolPcSeen)
    {
        g_hangupBattleRenderTrace.firstPoolPcSeen = 1;
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_render_first "
                    "sequence=%u pc=%08x local_pc=%04x code_base=%08x\n",
                    g_hangupBattleRenderTrace.sequence, pc, localPc,
                    g_hangupBattleRenderTrace.codeBase);
            fflush(trace);
            fclose(trace);
        }
    }
    if (localPc == 0x5FAu && !g_hangupBattleRenderTrace.mainLoopSeen)
    {
        u32 r9 = 0;
        u32 sceneState = 0;
        u32 sceneObject = 0;
        u32 sceneBusy = 0;
        u32 battleListSource = 0;
        u16 modalState = 0xffff;
        u8 charListPending = 0xff;
        u8 loaderState = 0xff;
        u8 battleLoadingVisible = 0xff;
        u8 battleLoadingProgress = 0xff;
        g_hangupBattleRenderTrace.mainLoopSeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        if (r9 != 0)
        {
            (void)uc_mem_read(MTK, r9 + 10372u,
                              &sceneState, sizeof(sceneState));
            (void)uc_mem_read(MTK, r9 + 10328u,
                              &sceneObject, sizeof(sceneObject));
            (void)uc_mem_read(MTK, r9 + 10364u,
                              &battleListSource, sizeof(battleListSource));
            (void)uc_mem_read(MTK, r9 + 10303u,
                              &charListPending, sizeof(charListPending));
            (void)uc_mem_read(MTK, r9 + 10304u,
                              &modalState, sizeof(modalState));
            if (sceneObject != 0)
                (void)uc_mem_read(MTK, sceneObject + 8u,
                                  &sceneBusy, sizeof(sceneBusy));
            (void)uc_mem_read(MTK, r9 + 8272u,
                              &sceneObject, sizeof(sceneObject));
            if (sceneObject != 0)
                (void)uc_mem_read(MTK, sceneObject + 872u,
                                  &loaderState, sizeof(loaderState));
            /* mmBattle:DrawBattleChatBubble(0xA818) tests this byte before
             * drawing the GBK literal \"获取数据...\" at 0xAAC4: zero takes
             * the loading branch.  This is a read-only post-parser sample of
             * the actual UI contract, not a guessed screen pointer. */
            (void)uc_mem_read(MTK, r9 + 16480u,
                              &battleLoadingVisible,
                              sizeof(battleLoadingVisible));
            (void)uc_mem_read(MTK, r9 + 16476u,
                              &battleLoadingProgress,
                              sizeof(battleLoadingProgress));
        }
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_render_main_loop "
                    "sequence=%u local_pc=%04x code_base=%08x r9=%08x "
                    "state=%u scene_busy=%u char_list_pending=%u modal=%u "
                    "list_source=%08x loader_state=%u "
                    "battle_loading_visible=%u battle_loading_progress=%u\n",
                    g_hangupBattleRenderTrace.sequence, localPc,
                    g_hangupBattleRenderTrace.codeBase, r9, sceneState,
                    sceneBusy, charListPending, modalState, battleListSource, loaderState,
                    battleLoadingVisible, battleLoadingProgress);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if (localPc == 0x656u && !g_hangupBattleRenderTrace.frameDelegateSeen)
    {
        u32 r0 = 0;
        u32 r9 = 0;
        u32 gameState = 0;
        u32 frameDelegate = 0;
        u8 overlayState = 0xff;
        u8 overlayAnimation = 0xff;
        u8 overlayTransition = 0xff;
        u8 sceneSlotCount = 0;
        u32 i;

        g_hangupBattleRenderTrace.frameDelegateSeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        if (r9 != 0)
        {
            (void)uc_mem_read(MTK, r9 + 8272u, &gameState,
                              sizeof(gameState));
            if (gameState != 0)
            {
                (void)uc_mem_read(MTK, gameState + 4u, &frameDelegate,
                                  sizeof(frameDelegate));
                (void)uc_mem_read(MTK, gameState + 1136u, &overlayState,
                                  sizeof(overlayState));
                (void)uc_mem_read(MTK, gameState + 1138u,
                                  &overlayAnimation,
                                  sizeof(overlayAnimation));
                (void)uc_mem_read(MTK, gameState + 1140u,
                                  &overlayTransition,
                                  sizeof(overlayTransition));
            }
        }
        if (r0 != 0)
        {
            for (i = 0; i < 6; ++i)
            {
                u32 slotObject = 0;
                (void)uc_mem_read(MTK, r0 + 1348u + i * 196u,
                                  &slotObject, sizeof(slotObject));
                if (slotObject != 0)
                    ++sceneSlotCount;
            }
        }
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            /* MainLoop's first normal-path operation is the game's frame
             * delegate at gameState+4.  DrawBattleSceneMain is reached from
             * that delegate, not by a direct CBM call, so this is the owner
             * of the next observable lifecycle boundary. */
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_frame_delegate "
                    "sequence=%u local_pc=%04x r0=%08x r9=%08x game=%08x "
                    "delegate=%08x overlay=%u/%u/%u scene_slots=%u\n",
                    g_hangupBattleRenderTrace.sequence, localPc, r0, r9,
                    gameState, frameDelegate, overlayState, overlayAnimation,
                    overlayTransition, sceneSlotCount);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if (localPc == 0x622u && !g_hangupBattleRenderTrace.charListGateSeen)
    {
        u32 r0 = 0;
        u32 r9 = 0;
        g_hangupBattleRenderTrace.charListGateSeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_render_gate "
                    "sequence=%u r0=%u r9=%08x expected=1\n",
                    g_hangupBattleRenderTrace.sequence, r0, r9);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if (localPc == 0x71BAu &&
        !g_hangupBattleRenderTrace.battleCompletionCheckSeen)
    {
        u32 r9 = 0;
        u32 startPending = 0xffffffffu;
        u16 startReady = 0xffffu;
        u16 modalState = 0xffffu;

        g_hangupBattleRenderTrace.battleCompletionCheckSeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        if (r9 != 0)
        {
            (void)uc_mem_read(MTK, r9 + 13484u, &startPending,
                              sizeof(startPending));
            (void)uc_mem_read(MTK, r9 + 13426u, &startReady,
                              sizeof(startReady));
            (void)uc_mem_read(MTK, r9 + 10304u, &modalState,
                              sizeof(modalState));
        }
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            /* mmBattle:sub_71BA is the first owner-side completion test for
             * HandleBattleStartMsg.  A true result authorizes MainLoop's
             * 0x9B0/0xA62 modal handoff to BattleScene_DrawMain. */
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_start_completion "
                    "sequence=%u local_pc=%04x r9=%08x pending=%u ready=%u "
                    "modal=%u\n",
                    g_hangupBattleRenderTrace.sequence, localPc, r9,
                    startPending, startReady, modalState);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if (localPc == 0x65Cu && !g_hangupBattleRenderTrace.postDelegateStateSeen)
    {
        u32 r9 = 0;
        u32 gameState = 0;
        u16 sceneBusy = 0xffff;
        u16 actionPending = 0xffff;
        u8 autoRevive = 0xff;
        u8 actionPrompt = 0xff;
        u8 menuPending = 0xff;
        u8 gameLoadingGate = 0xff;
        u8 gameOverlay = 0xff;
        u8 gameWait = 0xff;
        u8 inputThrottle = 0xff;

        g_hangupBattleRenderTrace.postDelegateStateSeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        if (r9 != 0)
        {
            u32 sceneObject = 0;
            (void)uc_mem_read(MTK, r9 + 10328u, &sceneObject,
                              sizeof(sceneObject));
            if (sceneObject != 0)
                (void)uc_mem_read(MTK, sceneObject + 8u, &sceneBusy,
                                  sizeof(sceneBusy));
            (void)uc_mem_read(MTK, r9 + 10306u, &actionPending,
                              sizeof(actionPending));
            (void)uc_mem_read(MTK, r9 + 13403u, &autoRevive,
                              sizeof(autoRevive));
            (void)uc_mem_read(MTK, r9 + 10298u, &actionPrompt,
                              sizeof(actionPrompt));
            (void)uc_mem_read(MTK, r9 + 10299u, &inputThrottle,
                              sizeof(inputThrottle));
            (void)uc_mem_read(MTK, r9 + 8272u, &gameState,
                              sizeof(gameState));
            if (gameState != 0)
            {
                (void)uc_mem_read(MTK, gameState + 985u, &menuPending,
                                  sizeof(menuPending));
                (void)uc_mem_read(MTK, gameState + 1133u,
                                  &gameLoadingGate,
                                  sizeof(gameLoadingGate));
                (void)uc_mem_read(MTK, gameState + 1136u, &gameOverlay,
                                  sizeof(gameOverlay));
                (void)uc_mem_read(MTK, gameState + 1206u, &gameWait,
                                  sizeof(gameWait));
            }
        }
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            /* Snapshot every MainLoop early-return guard immediately after
             * the CBE frame delegate returns.  This identifies the first
             * guard that prevents the subsequent sub_71BA start handoff. */
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_post_delegate "
                    "sequence=%u local_pc=%04x r9=%08x busy=%u action=%u "
                    "auto=%u prompt=%u menu=%u loading_gate=%u overlay=%u "
                    "wait=%u throttle=%u\n",
                    g_hangupBattleRenderTrace.sequence, localPc, r9,
                    sceneBusy, actionPending, autoRevive, actionPrompt,
                    menuPending, gameLoadingGate, gameOverlay, gameWait,
                    inputThrottle);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if ((localPc == 0x9B0u || localPc == 0xA62u) &&
        !g_hangupBattleRenderTrace.battleCompletionPresentSeen)
    {
        g_hangupBattleRenderTrace.battleCompletionPresentSeen = 1;
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_start_handoff "
                    "sequence=%u local_pc=%04x action=modal-ready\n",
                    g_hangupBattleRenderTrace.sequence, localPc);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if (localPc == 0xA818u && !g_hangupBattleRenderTrace.loadingDialogDrawSeen)
    {
        u32 r9 = 0;
        u32 lr = 0;
        u8 loadingVisible = 0xff;
        u8 loadingProgress = 0xff;

        g_hangupBattleRenderTrace.loadingDialogDrawSeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (r9 != 0)
        {
            (void)uc_mem_read(MTK, r9 + 16480u, &loadingVisible,
                              sizeof(loadingVisible));
            (void)uc_mem_read(MTK, r9 + 16476u, &loadingProgress,
                              sizeof(loadingProgress));
        }
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            /* IDA: mmBattle DrawBattleChatBubble 0xA818, where the zero
             * loadingVisible branch uses the GBK \"获取数据...\" string at
             * 0xAAC4. Record only its first post-start render invocation. */
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_loading_draw "
                    "sequence=%u local_pc=%04x r9=%08x lr=%08x "
                    "visible=%u progress=%u\n",
                    g_hangupBattleRenderTrace.sequence, localPc, r9, lr,
                    loadingVisible, loadingProgress);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if (localPc == 0x5444u && !g_hangupBattleRenderTrace.drawMainSeen)
    {
        u32 r0 = 0;
        u32 r9 = 0;
        u32 lr = 0;
        u32 battlePhase = 0;
        u32 modalState = 0;
        u8 battleMode = 0xff;
        u8 battleAnimPhase = 0xff;
        u8 selectedSlot = 0xff;
        u8 slotCount = 0;
        u32 slotObject[6] = {0};
        u32 i;

        g_hangupBattleRenderTrace.drawMainSeen = 1;
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &r9);
        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        if (r9 != 0)
        {
            (void)uc_mem_read(MTK, r9 + 13412u, &battlePhase,
                              sizeof(battlePhase));
            (void)uc_mem_read(MTK, r9 + 10304u, &modalState,
                              sizeof(modalState));
            (void)uc_mem_read(MTK, r9 + 13403u, &battleMode,
                              sizeof(battleMode));
            (void)uc_mem_read(MTK, r9 + 13401u, &battleAnimPhase,
                              sizeof(battleAnimPhase));
            (void)uc_mem_read(MTK, r9 + 13394u, &selectedSlot,
                              sizeof(selectedSlot));
        }
        /* BattleScene_DrawMain indexes six 196-byte character slots at
         * a1+1348.  A render that has no live slot cannot replace the
         * preceding loading overlay, so sample the native input directly
         * rather than infer readiness from a later UI string. */
        if (r0 != 0)
        {
            for (i = 0; i < 6; ++i)
            {
                (void)uc_mem_read(MTK, r0 + 1348u + i * 196u,
                                  &slotObject[i], sizeof(slotObject[i]));
                if (slotObject[i] != 0)
                    ++slotCount;
            }
        }
        trace = fopen("logs/hangup-protocol.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_mmBattle_draw_main "
                    "sequence=%u local_pc=%04x r0=%08x r9=%08x lr=%08x "
                    "phase=%u modal=%u mode=%u anim_phase=%u selected=%u "
                    "slot_count=%u slots=%08x/%08x/%08x/%08x/%08x/%08x\n",
                    g_hangupBattleRenderTrace.sequence, localPc, r0, r9, lr,
                    battlePhase, modalState, battleMode, battleAnimPhase,
                    selectedSlot, slotCount, slotObject[0], slotObject[1],
                    slotObject[2], slotObject[3], slotObject[4],
                    slotObject[5]);
            fflush(trace);
            fclose(trace);
        }
        return;
    }
    if (localPc != 0x62Cu)
        return;

    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace != NULL)
    {
        fprintf(trace,
                "[info][network] mock_hangup_mmBattle_render sequence=%u "
                "local_pc=%04x code_base=%08x main_loop=%u action=char-list\n",
                g_hangupBattleRenderTrace.sequence, localPc,
                g_hangupBattleRenderTrace.codeBase,
                g_hangupBattleRenderTrace.mainLoopSeen);
        fflush(trace);
        fclose(trace);
    }
    vm_automation_note_battle_scene_char_list(
        g_hangupBattleRenderTrace.sequence, localPc);
    g_hangupBattleRenderTrace.active = 0;
}

/* The multiplayer launcher sets the process directory to one player's
 * profile, where logs/ already exists.  Keep the post-shop hangup probe
 * outside the server log so client parser evidence cannot be confused with
 * server-side auto-battle scheduling. */
static void vm_net_append_hangup_protocol_trace(
    const char *phase, const vm_net_remote_observation *observation,
    u32 eventType, u32 responsePtr, u32 callback, u16 battleEntryState,
    u32 activeScreen, uc_err callbackErr)
{
    FILE *trace;
    u8 managerState = 0;
    u32 managerCallback = 0;
    u32 dispatchObject = 0;
    u32 dispatchState = 0;
    u32 dispatchCallback = 0;
    u32 businessCallback = 0;
    u32 traceR9 = Global_R9;
    u16 watchedBattleEntryState = 0xffff;
    u32 watchedBattleStateAddress = g_hangupBattleStateWatchAddress;
    u32 watchedBattleStateWrites = g_hangupBattleStateWatchWriteCount;
    u8 parserPacketInit = 0;
    u8 parserGuardReturn = 0;
    u8 parserUnpackError = 0;
    u8 parserFollowup = 0;
    u8 parserFallback = 0;
    u8 parserActorMove = 0;
    u8 parserTypeResponse = 0;
    u8 parserEntryCount = 0;
    u32 parserEntries[4] = {0};
    u8 businessCodeBytes[32] = {0};
    char businessCodeHex[sizeof(businessCodeBytes) * 2 + 1] = {0};

    if (observation == NULL || !observation->hasHangupBattleStart)
        return;
    if (Global_R9 != 0)
    {
        /* net_wrapper_event_dispatch (0x0103489B) dispatches through these
         * two CBE-owned callback slots.  Read them only to identify which
         * normal parser chain receives the already queued response. */
        (void)uc_mem_read(MTK, Global_R9 + 38280 + 12,
                          &managerState, sizeof(managerState));
        (void)uc_mem_read(MTK, Global_R9 + 38280 + 68,
                          &managerCallback, sizeof(managerCallback));
        (void)uc_mem_read(MTK, Global_R9 + 38056,
                          &dispatchObject, sizeof(dispatchObject));
        (void)uc_mem_read(MTK, Global_R9 + 23856,
                          &businessCallback, sizeof(businessCallback));
        if (vm_is_pool_entry(businessCallback & ~1u) &&
            uc_mem_read(MTK, businessCallback & ~1u,
                        businessCodeBytes,
                        sizeof(businessCodeBytes)) == UC_ERR_OK)
        {
            vm_hangup_format_code_bytes(businessCodeBytes,
                                        sizeof(businessCodeBytes),
                                        businessCodeHex,
                                        sizeof(businessCodeHex));
        }
        if (dispatchObject != 0)
        {
            (void)uc_mem_read(MTK, dispatchObject + 12,
                              &dispatchState, sizeof(dispatchState));
            (void)uc_mem_read(MTK, dispatchObject + 20,
                              &dispatchCallback, sizeof(dispatchCallback));
        }
    }
    if (watchedBattleStateAddress != 0)
    {
        (void)uc_mem_read(MTK, watchedBattleStateAddress,
                          &watchedBattleEntryState,
                          sizeof(watchedBattleEntryState));
    }
    if (g_hangupProtocolParserTrace.active && observation != NULL &&
        g_hangupProtocolParserTrace.sequence ==
            observation->hangupResponseSequence)
    {
        parserPacketInit = g_hangupProtocolParserTrace.packetInitEntered;
        parserGuardReturn = g_hangupProtocolParserTrace.packetGuardReturned;
        parserUnpackError = g_hangupProtocolParserTrace.packetUnpackError;
        parserFollowup = g_hangupProtocolParserTrace.businessFollowup;
        parserFallback = g_hangupProtocolParserTrace.businessFallback;
        parserActorMove = g_hangupProtocolParserTrace.actorMoveCase;
        parserTypeResponse = g_hangupProtocolParserTrace.typeResponseCase;
        parserEntryCount = g_hangupProtocolParserTrace.entryCount;
        memcpy(parserEntries, g_hangupProtocolParserTrace.entrySwitchWords,
               sizeof(parserEntries));
    }
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][network] mock_hangup_response_callback phase=%s "
            "seq=%u event=%u response=%u ptr=%08x cb=%08x "
            "battle_entry_state=%u active_screen=%08x callback_err=%u "
            "manager=%u/%08x dispatch=%08x:%u/%08x business_cb=%08x "
            "business_code=%s "
            "r9=%08x watch=%08x/%u/%u "
            "parser=init:%u,guard:%u,unpack:%u,followup:%u,fallback:%u,"
            "case2:%u,case4:%u,entries:%u[%u,%u,%u,%u]\n",
            phase ? phase : "-",
            observation->hangupResponseSequence,
            eventType,
            observation->hangupResponseLength,
            responsePtr,
            callback,
            battleEntryState,
            activeScreen,
            (u32)callbackErr,
            managerState,
            managerCallback,
            dispatchObject,
            dispatchState,
            dispatchCallback,
            businessCallback,
            businessCodeHex[0] ? businessCodeHex : "-",
            traceR9,
            watchedBattleStateAddress,
            watchedBattleEntryState,
            watchedBattleStateWrites,
            parserPacketInit,
            parserGuardReturn,
            parserUnpackError,
            parserFollowup,
            parserFallback,
            parserActorMove,
            parserTypeResponse,
            parserEntryCount,
            parserEntries[0],
            parserEntries[1],
            parserEntries[2],
            parserEntries[3]);
    fflush(trace);
    fclose(trace);
}

/*
 * Read-only probe for the post-shop hangup stall.  The actual multiplayer
 * launchers run this CBE_EMU client binary, so record the normal event-7
 * callback boundary for the uniquely shaped 2/10+2/2+4/5+4/11 response.
 * HandleBattleEnterReq (0x01015E14) writes Global_R9+23682 before sending;
 * +24304 is the current screen pointer.  No CBE state or event ordering is
 * modified here.
 */
static void scheduler_trace_hangup_battle_response_callback(
    const char *phase, const vm_net_remote_observation *observation,
    u32 eventType, u32 responsePtr, u32 callback, uc_err callbackErr)
{
    u16 battleEntryState = 0xffff;
    u32 activeScreen = 0;

    if (observation == NULL || !observation->hasHangupBattleStart)
        return;
    if (Global_R9 != 0)
    {
        (void)uc_mem_read(MTK, Global_R9 + 23682,
                          &battleEntryState, sizeof(battleEntryState));
        (void)uc_mem_read(MTK, Global_R9 + 24304,
                          &activeScreen, sizeof(activeScreen));
    }
    printf("[info][network] mock_hangup_response_callback phase=%s "
           "seq=%u event=%u response=%u ptr=%08x cb=%08x "
           "battle_entry_state=%u active_screen=%08x callback_err=%u\n",
           phase ? phase : "-",
           observation->hangupResponseSequence,
           eventType,
           observation->hangupResponseLength,
           responsePtr,
           callback,
           battleEntryState,
           activeScreen,
           (u32)callbackErr);
    vm_net_append_hangup_protocol_trace(
        phase, observation, eventType, responsePtr, callback,
        battleEntryState, activeScreen, callbackErr);
}

static void scheduler_queue_net_task(u32 r0, u32 r1, u32 callback, u32 context)
{
    (void)r0;
    (void)r1;
    scheduler_queue_net_event(5, 0, 0, 0, callback, context);
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


static uc_err scheduler_dispatch_net_tasks(void)
{
    u32 activeBefore = scheduler_count_active_net_tasks();
    if (activeBefore)
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        vm_net_task *task = &g_netTasks[i];
        if (!task->active)
            continue;
        if (task->notBeforeTick > g_schedulerTick)
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
            if (task->deferredToNextTick)
            {
                printf("[info][network] deferred_data_event_dispatch "
                       "event=%u resp=%u tick=%u eligible_tick=%u "
                       "cb=%08x ctx=%08x\n",
                       taskEvent, taskR1, g_schedulerTick,
                       task->notBeforeTick, taskCallback, taskContext);
            }
            task->fired = 1;
            task->active = 0;
            g_netTaskDispatchDepth++;
            g_netTaskDispatchSlot = (int)i;
            if (g_autotestEnabled && (taskEvent == 5u || taskEvent == 7u ||
                                      taskEvent == 9u))
            {
                vm_autotest_note("net_fire event=%u r0=%08x r1=%08x cb=%08x ctx=%08x\n",
                                 taskEvent, taskR0, taskR1, taskCallback,
                                 taskContext);
            }
            DEBUG_PRINT("[probe_net_fire] event=%u r0=%x r1=%x r2=%x cb=%x ctx=%x tick=%u\n", taskEvent, taskR0, taskR1, taskR2, taskCallback, taskContext, g_schedulerTick);
            if (g_netDebugReadWindow)
            {
                printf("[info][network] net_fire slot=%u event=%u r0=%08x r1=%u r2=%u cb=%08x ctx=%08x depth=%u\n",
                       i, taskEvent, taskR0, taskR1, taskR2,
                       taskCallback, taskContext, g_netTaskDispatchDepth);
            }
            vm_autotest_trace_update_guest_callback("callback-begin", taskR0,
                                                     taskR1);
            scheduler_trace_hangup_battle_response_callback(
                "begin", &taskRemoteObservation, taskEvent, taskR0,
                taskCallback, UC_ERR_OK);
            vm_hangup_vital_forensics_callback_begin(taskEvent, taskR0,
                                                      taskCallback);
            vm_battle_insight_forensics_callback_begin(taskEvent, taskR0,
                                                        taskCallback, taskContext);
            vm_hangup_protocol_parser_trace_begin(&taskRemoteObservation);
            uc_err err = vm_call4_preserve_regs_clear_stack_args(taskCallback, taskR0, taskR1, taskR2, taskEvent);
            scheduler_trace_hangup_battle_response_callback(
                "end", &taskRemoteObservation, taskEvent, taskR0,
                taskCallback, err);
            vm_hangup_vital_forensics_callback_end(taskEvent, taskR0,
                                                    taskCallback);
            vm_battle_insight_forensics_callback_end(taskEvent, taskR0,
                                                      taskCallback, taskContext, err);
            vm_autotest_trace_update_guest_callback("callback-end", taskR0,
                                                     taskR1);
            vm_hangup_protocol_parser_trace_end(&taskRemoteObservation);
            if (g_netDebugReadWindow)
            {
                printf("[info][network] net_done slot=%u event=%u cb=%08x err=%u remaining_read=%u/%u\n",
                       i, taskEvent, taskCallback, err,
                       g_netMockResponseOffset, g_netMockResponseLen);
            }
            g_netTaskDispatchDepth--;
            g_netTaskDispatchSlot = -1;
            if (err != UC_ERR_OK)
                return err;
        }
    }
    return UC_ERR_OK;
}

/* Android ships only the remote game client transport.  The desktop build
 * keeps the embedded service, persistence and web-admin implementation. */
#ifdef CBE_CLIENT_ONLY
#include "network-client.c"
#else
#include "server/mock-server.c"
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
    vm_actor_resource_trace("named-download-enter", leaf, 0, -1, 0);
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
            &eventType, NULL, NULL, 0, NULL, NULL);
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

/*
 * This probe is deliberately generic: operators enable it only while
 * reproducing a world-map input delay.  It does not identify a map screen by
 * guest state and does not alter event ordering.  The record instead separates
 * time already spent waiting in the host event queue from time spent in the
 * existing scheduler, CBE screen callbacks, render, and local file reads.
 */
static void vm_trace_scene_battle_host_input(const char *route,
                                             const char *phase,
                                             const vm_event *evt,
                                             u32 screenPtr,
                                             u32 entry,
                                             u32 eventType,
                                             u32 eventArg,
                                             u32 eventArgValue,
                                             uc_err result)
{
    static int enabled = -1;
    static u32 traceCount = 0;
    const char *setting;
    FILE *trace;

    if (evt == NULL || (evt->event != VM_EVENT_KEYBOARD &&
                        evt->event != VM_EVENT_TOUCHSCREEN))
    {
        return;
    }
    if (enabled < 0)
    {
        setting = getenv("CBE_TRACE_SCENE_BATTLE_COLLISION");
        enabled = setting != NULL && setting[0] != 0 &&
                  strcmp(setting, "0") != 0 &&
                  strcmp(setting, "off") != 0 &&
                  strcmp(setting, "false") != 0;
    }
    if (!enabled || traceCount >= 256u)
        return;

    trace = fopen("logs/scene-battle-collision.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_battle_host_input route=%s phase=%s call=%u "
            "event=%u code=%08x state=%08x screen=%08x entry=%08x "
            "type=%u arg=%08x arg_value=%08x result=%u scene=%s\n",
            route, phase, traceCount, evt->event, evt->r0, evt->r1,
            screenPtr, entry, eventType, eventArg, eventArgValue,
            (unsigned)result, g_lastSceLoadName[0] ? g_lastSceLoadName : "-");
    fclose(trace);
}

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
        vm_trace_scene_battle_host_input("tscreen-event", "before", evt,
                                         screenPtr, tScreenEventEntry,
                                         evt->r1 ? 0u : 1u, keyPtr, keyMask,
                                         UC_ERR_OK);
        uc_err err = vm_call4(tScreenEventEntry, screenPtr, evt->r1 ? 0 : 1, keyPtr, 0);
        vm_trace_scene_battle_host_input("tscreen-event", "after", evt,
                                         screenPtr, tScreenEventEntry,
                                         evt->r1 ? 0u : 1u, keyPtr, keyMask,
                                         err);
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
        vm_trace_scene_battle_host_input("tscreen-event", "before", evt,
                                         screenPtr, tScreenEventEntry,
                                         touchEventType, touchPtr, evt->r1,
                                         UC_ERR_OK);
        uc_err err = vm_call4(tScreenEventEntry, screenPtr, touchEventType, touchPtr, 0);
        vm_trace_scene_battle_host_input("tscreen-event", "after", evt,
                                         screenPtr, tScreenEventEntry,
                                         touchEventType, touchPtr, evt->r1,
                                         err);
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
    /* This is the render-complete boundary.  Scenario evidence copies the
     * emulator-owned LCD cache here; it never samples the desktop window. */
    vm_automation_render_complete();
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
    /* The edit buffer is UCS-2, but the legacy input descriptor stores its
     * prompt as a GBK char pointer.  Reading this field as UCS-2 combines each
     * pair of GBK bytes into an unrelated Unicode code point and garbles the
     * Android dialog title. */
    if (g_vmInputPrompt && MTK)
        vm_debug_read_guest_cstr(g_vmInputPrompt, gbk, sizeof(gbk));
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
        vm_shop_return_forensics_log("input-enqueue-key", VM_EVENT_KEYBOARD,
                                      (u32)skey, (u32)isPress, 0);
    }
}

/*
 * SDL's key repeat latch belongs to the host, not to CBE.  A key can open the
 * native text editor while its SDL_KEYUP is still pending; keep text mode from
 * forwarding that release to CBE, but always retire the matching host latch.
 * Otherwise the old opening key permanently blocks every later SDL_KEYDOWN.
 */
static void vm_host_handle_key_up(SDL_Keycode key)
{
    if (isKeyDown != key)
        return;
    isKeyDown = SDLK_UNKNOWN;
    if (!g_vmInputOpen)
        keyEvent(MR_KEY_RELEASE, key);
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
    vm_shop_return_forensics_log("input-enqueue-touch", VM_EVENT_TOUCHSCREEN,
                                  (u32)type, (u32)((y << 16) | x), 0);
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

static void vm_automation_note_uplink(const u8 *packet, u32 packetLen)
{
    if (!g_vmAutomation.active ||
        g_vmAutomation.scenario !=
            VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT ||
        g_vmAutomation.hangupNativeExitUplinkSeen || packet == NULL ||
        packetLen < 9u || packet[0] != 'W' || packet[1] != 'T' ||
        (((u32)packet[2] << 8) | packet[3]) != packetLen ||
        packet[4] != 1 || packet[5] != 25 || packet[6] != 5)
    {
        return;
    }

    g_vmAutomation.hangupNativeExitUplinkSeen = 1;
    g_vmAutomation.hangupNativeExitUplinkFrame = g_vmAutomation.renderFrames;
    /* A fresh 0x66CC visit after this client-owned request is the only
     * accepted next-battle parser boundary. */
    g_vmAutomation.battleStartHandlerSeen = 0;
    g_vmAutomation.battleSceneCharListSeen = 0;
    g_vmAutomation.battleStartHandlerFrame = 0;
    g_vmAutomation.battleSceneCharListFrame = 0;
    vm_autotest_note("automation_hangup_native_auto_exit_uplink wt=25/5 "
                     "frame=%u reentry_handler_reset=1\n",
                     g_vmAutomation.hangupNativeExitUplinkFrame);
}

static const char *vm_automation_stage_name(vm_automation_stage stage)
{
    switch (stage)
    {
    case VM_AUTOMATION_STAGE_BOOT_CONFIRM: return "boot-confirm";
    case VM_AUTOMATION_STAGE_WAIT_TITLE_MODULE_UPDATE:
        return "wait-title-module-update";
    case VM_AUTOMATION_STAGE_WAIT_TIMED_TITLE_BOOTSTRAP: return "wait-timed-title-bootstrap";
    case VM_AUTOMATION_STAGE_WAIT_TITLE_LOGIN_DISPATCH: return "wait-title-login";
    case VM_AUTOMATION_STAGE_WAIT_ROLE_LIST: return "wait-role-list";
    case VM_AUTOMATION_STAGE_WAIT_INITIAL_SCENE: return "wait-initial-scene";
    case VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW:
        return "wait-dream-map-number-draw";
    case VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_PROMPT:
        return "wait-dream-npc-prompt";
    case VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_DIALOG:
        return "wait-dream-npc-dialog";
    case VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_TARGET_SCENE:
        return "wait-dream-npc-target-scene";
    case VM_AUTOMATION_STAGE_WAIT_EQUIPMENT_ENHANCE_RULES:
        return "wait-equipment-enhance-rules";
    case VM_AUTOMATION_STAGE_WAIT_SHOP_OPEN: return "wait-shop-open";
    case VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN: return "wait-shop-return";
    case VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN_PRE_HANGUP_CAPTURE:
        return "wait-shop-return-pre-hangup-capture";
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE: return "wait-hangup-battle";
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE_SCREEN: return "wait-hangup-battle-screen";
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_VISIBLE:
        return "wait-hangup-auto-visible";
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_CANCEL_RESPONSE:
        return "wait-hangup-auto-cancel-response";
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_TERMINAL:
        return "wait-hangup-auto-terminal";
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_NATIVE_AUTO_EXIT:
        return "wait-hangup-native-auto-exit";
    case VM_AUTOMATION_STAGE_PASSED: return "passed";
    case VM_AUTOMATION_STAGE_FAILED: return "failed";
    default: return "none";
    }
}

static const char *vm_automation_scenario_name(void)
{
    switch (g_vmAutomation.scenario)
    {
    case VM_AUTOMATION_SCENARIO_SHOP_RETURN_HANGUP:
        return "shop-return-hangup-v1";
    case VM_AUTOMATION_SCENARIO_DIRECT_HANGUP:
        return "direct-hangup-control-v1";
    case VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE:
        return "title-module-update-v1";
    case VM_AUTOMATION_SCENARIO_SCENE_TELEPORT_STONE_PROBE:
        return "scene-teleport-stone-probe-v1";
    case VM_AUTOMATION_SCENARIO_DREAM_CLOCK_PROBE:
        return "dream-clock-probe-v1";
    case VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE:
        return "dream-npc-entry-clock-probe-v1";
    case VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_RULES_PROBE:
        return "equipment-enhance-rules-probe-v1";
    case VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE:
        return "equipment-enhance-bag-probe-v1";
    case VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE:
        return "equipment-enhance-stage1-probe-v1";
    case VM_AUTOMATION_SCENARIO_HANGUP_AUTO_CANCEL:
        return "hangup-auto-cancel-v1";
    case VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL:
        return "hangup-auto-terminal-v1";
    case VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT:
        return "hangup-native-auto-exit-v1";
    default:
        return "none";
    }
}

static bool vm_automation_scenario_uses_direct_hangup(void)
{
    return g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_DIRECT_HANGUP ||
           g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_HANGUP_AUTO_CANCEL ||
           g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL ||
           g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT;
}

static void vm_automation_write_result(const char *result, const char *reason)
{
    char path[640];
    FILE *stream;
    time_t now;

    if (!g_vmAutomation.active || g_vmAutomation.artifactDir[0] == 0)
        return;
    snprintf(path, sizeof(path), "%s/result.json", g_vmAutomation.artifactDir);
    stream = fopen(path, "wb");
    if (stream == NULL)
        return;
    now = time(NULL);
    fprintf(stream,
            "{\n"
            "  \"scenario\": \"%s\",\n"
            "  \"result\": \"%s\",\n"
            "  \"reason\": \"%s\",\n"
            "  \"stage\": \"%s\",\n"
            "  \"render_frames\": %u,\n"
            "  \"input_count\": %u,\n"
            "  \"timed_input_count\": %u,\n"
            "  \"title_module_total_size\": %u,\n"
            "  \"title_module_checksum\": %u,\n"
            "  \"title_module_lifecycle_reject\": %u,\n"
            "  \"dream_clock_candidate_draw_count\": %u,\n"
            "  \"dream_clock_last_candidate_frame\": %u,\n"
            "  \"dream_clock_last_candidate_dst\": [%d, %d],\n"
            "  \"dream_clock_last_candidate_caller_return\": \"%08x\",\n"
            "  \"dream_npc_prompt_pc_seen\": %u,\n"
            "  \"dream_npc_dialog_parser_pc_seen\": %u,\n"
            "  \"dream_npc_instance_enter_response_seen\": %u,\n"
            "  \"dream_npc_instance_enter_response_sequence\": %u,\n"
            "  \"dream_npc_target_scene_packet_seen\": %u,\n"
            "  \"dream_npc_target_scene_packet_sequence\": %u,\n"
            "  \"timestamp_unix\": %lld\n"
            "}\n",
            vm_automation_scenario_name(), result ? result : "unknown",
            reason ? reason : "unknown",
            vm_automation_stage_name(g_vmAutomation.stage),
            g_vmAutomation.renderFrames, g_vmAutomation.inputCount,
            g_vmAutomation.timedInputCount,
            g_vmAutomation.titleModuleUpdateTotalSize,
            g_vmAutomation.titleModuleUpdateChecksum,
            g_vmAutomation.titleModuleUpdateLifecycleRejectSeen,
            g_vmAutomation.dreamClockCandidateDrawCount,
            g_vmAutomation.dreamClockLastCandidateFrame,
            g_vmAutomation.dreamClockLastCandidateX,
            g_vmAutomation.dreamClockLastCandidateY,
            g_vmAutomation.dreamClockLastCandidateCallerReturn,
            g_vmAutomation.dreamNpcPromptPcSeen,
            g_vmAutomation.dreamNpcDialogParserPcSeen,
            g_vmAutomation.dreamNpcInstanceEnterResponseSeen,
            g_vmAutomation.dreamNpcInstanceEnterResponseSequence,
            g_vmAutomation.dreamNpcTargetScenePacketSeen,
            g_vmAutomation.dreamNpcTargetScenePacketSequence,
            (long long)now);
    fclose(stream);
}

static void vm_automation_request_capture(const char *label)
{
    if (!g_vmAutomation.active || label == NULL ||
        g_vmAutomation.capturePending)
        return;
    snprintf(g_vmAutomation.pendingCaptureLabel,
             sizeof(g_vmAutomation.pendingCaptureLabel), "%s", label);
    g_vmAutomation.capturePending = 1;
}

static void vm_automation_capture_internal_lcd(void)
{
#ifdef CBE_CLIENT_ONLY
    char pngPath[640];
    char jsonPath[640];
    FILE *metadata;
    u32 pc = 0;
    time_t now;

    if (!g_vmAutomation.active || !g_vmAutomation.capturePending ||
        g_vmAutomation.artifactDir[0] == 0 || Lcd_Cache_Buffer == NULL)
        return;
    g_vmAutomation.capturePending = 0;
    ++g_vmAutomation.captureIndex;
    if (strcmp(g_vmAutomation.pendingCaptureLabel,
               "shop-return-pre-hangup") == 0)
    {
        /* This is deliberately set at the LCD export boundary rather than
         * when the capture is requested.  The next input is therefore not
         * enqueued until a complete scene frame exists that precedes it. */
        g_vmAutomation.shopReturnPreHangupCaptured = 1;
    }
    else if (strcmp(g_vmAutomation.pendingCaptureLabel,
                    "hangup-auto-visible") == 0)
    {
        /* The cancel input is released only after the native battle frame
         * containing the automatic controls has actually been exported. */
        g_vmAutomation.hangupAutoVisibleCaptured = 1;
    }
    snprintf(pngPath, sizeof(pngPath), "%s/frames/%03u_%s.png",
             g_vmAutomation.artifactDir, g_vmAutomation.captureIndex,
             g_vmAutomation.pendingCaptureLabel);
    snprintf(jsonPath, sizeof(jsonPath), "%s/frames/%03u_%s.json",
             g_vmAutomation.artifactDir, g_vmAutomation.captureIndex,
             g_vmAutomation.pendingCaptureLabel);
    if (!automation_png_write_rgb565(pngPath, Lcd_Cache_Buffer,
                                     LCD_WIDTH, LCD_HEIGHT))
    {
        vm_autotest_note("automation_capture failed label=%s path=%s\n",
                         g_vmAutomation.pendingCaptureLabel, pngPath);
        return;
    }
    (void)uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    now = time(NULL);
    metadata = fopen(jsonPath, "wb");
    if (metadata != NULL)
    {
        fprintf(metadata,
                "{\n"
                "  \"scenario\": \"%s\",\n"
                "  \"frame\": %u,\n"
                "  \"resolution\": \"%ux%u\",\n"
                "  \"pixel_format\": \"RGB565LE source; PNG RGB8 export\",\n"
                "  \"trigger\": \"%s\",\n"
                "  \"stage\": \"%s\",\n"
                "  \"active_screen\": \"%08x\",\n"
                "  \"module_base\": \"%08x\",\n"
                "  \"pc\": \"%08x\",\n"
                "  \"timestamp_unix\": %lld\n"
                "}\n",
                vm_automation_scenario_name(), g_vmAutomation.renderFrames,
                LCD_WIDTH, LCD_HEIGHT,
                g_vmAutomation.pendingCaptureLabel,
                vm_automation_stage_name(g_vmAutomation.stage),
                vmAddedScreen, g_currentScreenModuleBase, pc & ~1u,
                (long long)now);
        fclose(metadata);
    }
    vm_autotest_note("automation_capture frame=%u label=%s png=%s screen=%08x module=%08x pc=%08x\n",
                     g_vmAutomation.renderFrames,
                     g_vmAutomation.pendingCaptureLabel, pngPath,
                     vmAddedScreen, g_currentScreenModuleBase, pc & ~1u);
#endif
}

static void vm_automation_set_stage(vm_automation_stage stage, const char *reason)
{
    u32 gameState = 0;
    FILE *trace;

    if (!g_vmAutomation.active || g_vmAutomation.finished)
        return;
    g_vmAutomation.stage = stage;
    g_vmAutomation.stageStartedMs = SDL_GetTicks();
    g_vmAutomation.stageFrame = g_vmAutomation.renderFrames;
    vm_autotest_note("automation_stage stage=%s reason=%s frame=%u inputs=%u\n",
                     vm_automation_stage_name(stage), reason ? reason : "-",
                     g_vmAutomation.renderFrames, g_vmAutomation.inputCount);
    if ((g_vmAutomation.scenario != VM_AUTOMATION_SCENARIO_SHOP_RETURN_HANGUP &&
         g_vmAutomation.scenario != VM_AUTOMATION_SCENARIO_DIRECT_HANGUP) ||
        g_vmAutomationGameLoadingGateWatchAddress != 0 || Global_R9 == 0)
    {
        return;
    }
    /* This is intentionally armed at the first automation state change after
     * the scene runtime exists, before the toolbar opens the shop.  It records
     * all writes through the one user-authorized reproduction, but it does
     * not observe or affect ordinary client sessions. */
    if (uc_mem_read(MTK, Global_R9 + 8272u, &gameState,
                    sizeof(gameState)) != UC_ERR_OK || gameState == 0)
    {
        return;
    }
    g_vmAutomationGameLoadingGateWatchAddress = gameState + 1133u;
    g_vmAutomationGameLoadingGateWatchWriteCount = 0;
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace != NULL)
    {
        fprintf(trace,
                "[info][network] mock_hangup_game_loading_gate_arm "
                "stage=%s global_r9=%08x game=%08x addr=%08x\n",
                vm_automation_stage_name(stage), Global_R9, gameState,
                g_vmAutomationGameLoadingGateWatchAddress);
        fflush(trace);
        fclose(trace);
    }
}

static void vm_automation_finish(int passed, const char *reason)
{
    if (!g_vmAutomation.active || g_vmAutomation.finished)
        return;
    g_vmAutomation.finished = 1;
    g_vmAutomation.stage = passed ? VM_AUTOMATION_STAGE_PASSED :
                                   VM_AUTOMATION_STAGE_FAILED;
    vm_automation_request_capture(passed ? "pass" : "failure");
    vm_automation_write_result(passed ? "passed" : "failed", reason);
    vm_autotest_note("automation_result result=%s reason=%s\n",
                     passed ? "passed" : "failed", reason ? reason : "-");
    /* vm_automation_render_complete asks the owned process to exit only after
     * the requested LCD-only evidence frame has been exported. */
}

/* The initial NPC prompt is client-created in scene_runtime_tick after
 * FindNearestNPCWrapper returns a non-null node.  The isolated fixture has
 * exactly one dynamic NPC (30406) at the role's starting point, so the two
 * fixed CBE PCs below form a narrow input trigger rather than a coordinate
 * search: +0x15154 creates that prompt and 0x010380E8 begins parsing its
 * first 26/1 dialog response.  This helper only records those PC boundaries;
 * vm_automation_tick later supplies the two ordinary confirm key events.
 */
static void vm_automation_note_dream_npc_entry_pc(u32 pc)
{
    if (!g_vmAutomation.active || g_vmAutomation.finished ||
        g_vmAutomation.scenario !=
            VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE)
    {
        return;
    }
    if (g_vmAutomation.stage == VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_PROMPT &&
        pc == 0x01015154u && !g_vmAutomation.dreamNpcPromptPcSeen &&
        g_vmAutomation.initialSceneScreen != 0 &&
        vmAddedScreen == g_vmAutomation.initialSceneScreen)
    {
        g_vmAutomation.dreamNpcPromptPcSeen = 1;
        vm_autotest_note("automation_dream_npc_trigger binary=JianghuOL.CBE "
                         "local_pc=0x015154 hit=1 state=origin-scene-ready "
                         "screen=%08x frame=%u\n",
                         vmAddedScreen, g_vmAutomation.renderFrames);
        return;
    }
    if (g_vmAutomation.stage == VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_DIALOG &&
        pc == 0x010380E8u && !g_vmAutomation.dreamNpcDialogParserPcSeen)
    {
        g_vmAutomation.dreamNpcDialogParserPcSeen = 1;
        vm_autotest_note("automation_dream_npc_trigger binary=JianghuOL.CBE "
                         "local_pc=0x0380e8 hit=1 state=first-26-1-dialog "
                         "frame=%u\n",
                         g_vmAutomation.renderFrames);
    }
}

/* The numeric atlas may serve multiple gameplay effects, so this does not
 * label a draw as a timer.  It merely recognizes the screenshot-derived map
 * rectangle after the native 梦境 scene shell is ready, captures the LCD, and
 * retains the real CBE caller for later classification.  No guest memory,
 * rendering parameter, callback, packet, or input is modified here. */
static void vm_automation_note_scene_number_draw(const char *atlas, bool alpha,
                                                  int width, int height,
                                                  int dstX, int dstY,
                                                  u32 callerReturn)
{
    const bool inScreenshotMapRect = dstX >= 160 && dstX <= 232 &&
                                     dstY >= 62 && dstY <= 105;

    if (!g_vmAutomation.active || g_vmAutomation.finished ||
        (g_vmAutomation.scenario != VM_AUTOMATION_SCENARIO_DREAM_CLOCK_PROBE &&
         g_vmAutomation.scenario !=
             VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE) ||
        g_vmAutomation.stage != VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW ||
        !g_vmAutomation.dreamClockProbeArmed || atlas == NULL ||
        strcmp(atlas, "combat-number-atlas") != 0 || !inScreenshotMapRect ||
        g_vmAutomation.initialSceneScreen == 0 ||
        vmAddedScreen != g_vmAutomation.initialSceneScreen)
    {
        return;
    }

    if (g_vmAutomation.dreamClockLastCandidateFrame == 0 ||
        g_vmAutomation.renderFrames >
            g_vmAutomation.dreamClockLastCandidateFrame + 1u)
    {
        g_vmAutomation.dreamClockCandidateDrawCount = 0;
    }
    if (g_vmAutomation.dreamClockCandidateDrawCount < 0xffu)
        ++g_vmAutomation.dreamClockCandidateDrawCount;
    g_vmAutomation.dreamClockLastCandidateFrame = g_vmAutomation.renderFrames;
    g_vmAutomation.dreamClockLastCandidateCallerReturn = callerReturn;
    g_vmAutomation.dreamClockLastCandidateX = dstX;
    g_vmAutomation.dreamClockLastCandidateY = dstY;
    vm_autotest_note("automation_dream_map_number_draw atlas=%s alpha=%u "
                     "clip=%dx%d dst=(%d,%d) caller_return=%08x frame=%u "
                     "group_count=%u\n",
                     atlas, alpha ? 1u : 0u, width, height, dstX, dstY,
                     callerReturn, g_vmAutomation.renderFrames,
                     g_vmAutomation.dreamClockCandidateDrawCount);

    /* Four mm:ss glyphs normally arrive as separate clipped blits.  A future
     * client may batch the row into one wide blit, so accept that equally
     * observable rendering fact without synthesizing any scene interaction. */
    if (g_vmAutomation.dreamClockCandidateDrawCount >= 3u || width >= 24)
    {
        g_vmAutomation.dreamClockTopRightNumberSeen = 1;
        vm_automation_request_capture("dream-map-number-observed");
        vm_automation_finish(1, "dream-map-number-draw-observed");
    }
}

static int vm_automation_issue_key(int key, const char *trigger)
{
    if (!g_vmAutomation.active || g_vmAutomation.finished ||
        g_autotestKeyReleasePending || g_autotestTapReleasePending)
        return 0;
    keyEvent(MR_KEY_PRESS, key);
    g_autotestKeyReleasePending = 1;
    g_autotestKeyReleaseMs = (SDL_GetTicks() - g_autotestStartMs) + 80u;
    g_autotestKeyReleaseSym = key;
    ++g_vmAutomation.inputCount;
    vm_autotest_note("automation_input type=key key=%d trigger=%s frame=%u\n",
                     key, trigger ? trigger : "-", g_vmAutomation.renderFrames);
    return 1;
}

static int vm_automation_issue_tap(int x, int y, const char *trigger)
{
    if (!g_vmAutomation.active || g_vmAutomation.finished ||
        g_autotestKeyReleasePending || g_autotestTapReleasePending)
        return 0;
    mouseEvent(MR_MOUSE_DOWN, x, y);
    g_autotestTapReleasePending = 1;
    g_autotestTapReleaseWindow = 0;
    g_autotestTapReleaseMs = (SDL_GetTicks() - g_autotestStartMs) + 80u;
    g_autotestTapReleaseX = x;
    g_autotestTapReleaseY = y;
    ++g_vmAutomation.inputCount;
    vm_autotest_note("automation_input type=tap point=(%d,%d) trigger=%s frame=%u\n",
                     x, y, trigger ? trigger : "-", g_vmAutomation.renderFrames);
    return 1;
}

/* The backpack screen keeps its currently selected category in the screen
 * object pointed to by R9+0x5D08.  This is a read-only automation observation
 * taken from the native renderer's data contract; it is deliberately not a
 * screen transition or a replacement for a rendered input event. */
static int vm_automation_read_backpack_category(u8 *categoryOut,
                                                u32 *screenOut)
{
    u32 screen = 0;
    u8 category = 0;

    if (categoryOut == NULL)
        return 0;
    *categoryOut = 0;
    if (screenOut != NULL)
        *screenOut = 0;
    if (Global_R9 == 0 ||
        uc_mem_read(MTK, Global_R9 + 23816u, &screen, sizeof(screen)) !=
            UC_ERR_OK ||
        screen == 0 ||
        uc_mem_read(MTK, screen + 283u, &category, sizeof(category)) !=
            UC_ERR_OK)
    {
        return 0;
    }
    *categoryOut = category;
    if (screenOut != NULL)
        *screenOut = screen;
    return 1;
}

static void vm_automation_cancel_pending_timed_inputs(const char *reason)
{
    u32 cancelled = 0;

    for (u32 i = 0; i < g_autotestActionCount; ++i)
    {
        if (!g_autotestActions[i].fired)
        {
            g_autotestActions[i].fired = 1;
            ++cancelled;
        }
    }
    if (cancelled != 0)
        vm_autotest_note("automation_timed_input_cancelled count=%u reason=%s\n",
                         cancelled, reason ? reason : "-");
}

static void vm_automation_note_startup_pc(u32 pc)
{
    /* Startup/title CBMs are allocated dynamically.  Do not use a previous
     * process's pool address as an automation trigger.  Title advancement is
     * instead armed by the native 18/9 update-completion object plus a later
     * render boundary in vm_automation_tick(). */
    (void)pc;
}

static void vm_automation_note_screen_init(u32 screen, u32 initEntry,
                                           u32 logicEntry, u32 renderEntry)
{
    if (!g_vmAutomation.active)
        return;
    if (!g_vmAutomation.titleScreenInitialized &&
        g_vmAutomation.titleUpdateCompleteSeen)
    {
        /* The first screen initialized after the startup update parser has
         * completed is the native title menu.  This lifecycle boundary is
         * owned by the client's existing screen manager, unlike dynamically
         * allocated function addresses from an earlier probe. */
        g_vmAutomation.titleScreenInitialized = 1;
        g_vmAutomation.titleScreenInitFrame = g_vmAutomation.renderFrames;
        vm_autotest_note("automation_title_screen_initialized screen=%08x init=%08x logic=%08x render=%08x frame=%u\n",
                         screen, initEntry, logicEntry, renderEntry,
                         g_vmAutomation.renderFrames);
    }
    if (g_vmAutomation.shopReturnSeen &&
        !g_vmAutomation.shopReturnSceneReinitSeen &&
        screen != 0 && screen == g_vmAutomation.initialSceneScreen &&
        screen != g_vmAutomation.shopScreen)
    {
        g_vmAutomation.shopReturnSceneReinitSeen = 1;
        g_vmAutomation.shopReturnSceneReinitFrame =
            g_vmAutomation.renderFrames;
        vm_autotest_note("automation_shop_return_scene_reinit screen=%08x init=%08x logic=%08x render=%08x return_seq=%u frame=%u\n",
                         screen, initEntry, logicEntry, renderEntry,
                         g_vmAutomation.shopReturnPacketSequence,
                         g_vmAutomation.renderFrames);
    }
}

static void vm_automation_note_battle_handler_pc(u32 localPc,
                                                  u32 moduleSpBf)
{
    if (g_vmAutomation.active && localPc == 0x66CCu &&
        !g_vmAutomation.battleStartHandlerSeen)
    {
        u32 gameState = 0;

        g_vmAutomation.battleStartHandlerSeen = 1;
        g_vmAutomation.battleStartHandlerFrame = g_vmAutomation.renderFrames;
        g_vmAutomation.battleModuleSpBf = moduleSpBf;
        vm_autotest_note("automation_battle_handler local_pc=0x66cc module_spbf=%08x "
                         "frame=%u screen=%08x this=%08x\n",
                         moduleSpBf,
                         g_vmAutomation.renderFrames, vmAddedScreen,
                         g_currentScreenThis);
        if ((g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_HANGUP_AUTO_CANCEL ||
             g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL ||
             g_vmAutomation.scenario ==
                 VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT) &&
            Global_R9 != 0 &&
            uc_mem_read(MTK, Global_R9 + 8272u, &gameState,
                        sizeof(gameState)) == UC_ERR_OK &&
            gameState != 0)
        {
            /* Track the native automatic flag and phase around the final
             * action.  These forensic reads distinguish the visible 4/7
             * reward-panel boundary from the unrelated 4/9 and revival 4/8
             * branches; they never write client state. */
            g_vmAutomationBattleAutoFlagWatchAddress = gameState + 1140u;
            g_vmAutomationBattleOverlayWatchAddress = gameState + 1136u;
            g_vmAutomationBattlePhaseWatchAddress = Global_R9 + 13412u;
            g_vmAutomationBattleAutoFlagWatchWriteCount = 0;
            g_vmAutomationBattleOverlayWatchWriteCount = 0;
            g_vmAutomationBattlePhaseWatchWriteCount = 0;
            vm_autotest_note("automation_hangup_battle_watch_arm game=%08x auto=%08x overlay=%08x phase=%08x frame=%u\n",
                             gameState,
                             g_vmAutomationBattleAutoFlagWatchAddress,
                             g_vmAutomationBattleOverlayWatchAddress,
                             g_vmAutomationBattlePhaseWatchAddress,
                             g_vmAutomation.renderFrames);
        }
    }
}

static void vm_automation_note_battle_native_exit_pc(u32 pc)
{
    u32 localPc;
    u32 codeBase;

    if (!g_vmAutomation.active ||
        g_vmAutomation.scenario !=
            VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT)
    {
        return;
    }
    codeBase = g_vmAutomation.battleModuleCodeBase;
    if (codeBase == 0)
    {
        int appIndex;

        /* The 25/2 parser can run before the first observed 0x66CC callback.
         * Derive a candidate base only from the loaded module containing this
         * PC, and only after the exact response object was queued. */
        if (!g_vmAutomation.hangupNativeAutoExitResponseSeen)
            return;
        appIndex = vm_dl_find_loaded_index_by_pc(pc);
        if (appIndex < 0)
            return;
        codeBase = g_vmDlLoadedApps[appIndex].buffer;
        if (codeBase == 0 || pc < codeBase)
            return;
        localPc = pc - codeBase;
        if (localPc != 0x8996u && localPc != 0x5E92u &&
            localPc != 0x60C8u)
        {
            return;
        }
        g_vmAutomation.battleModuleCodeBase = codeBase;
    }
    else
    {
        if (pc < codeBase)
            return;
        localPc = pc - codeBase;
    }

    if (localPc == 0x8996u &&
        !g_vmAutomation.hangupNativeAutoExitParserSeen)
    {
        g_vmAutomation.hangupNativeAutoExitParserSeen = 1;
        g_vmAutomation.hangupNativeAutoExitParserFrame =
            g_vmAutomation.renderFrames;
        vm_autotest_note("automation_hangup_native_auto_exit_parser "
                         "local_pc=0x8996 code_base=%08x frame=%u\n",
                         codeBase,
                         g_vmAutomation.hangupNativeAutoExitParserFrame);
    }
    else if (localPc == 0x5E92u &&
             !g_vmAutomation.hangupNativeAutoExitPcSeen)
    {
        g_vmAutomation.hangupNativeAutoExitPcSeen = 1;
        g_vmAutomation.hangupNativeAutoExitPcFrame =
            g_vmAutomation.renderFrames;
        vm_autotest_note("automation_hangup_native_auto_exit_pc "
                         "local_pc=0x5e92 code_base=%08x frame=%u\n",
                         codeBase,
                         g_vmAutomation.hangupNativeAutoExitPcFrame);
    }
    else if (localPc == 0x60C8u &&
             !g_vmAutomation.hangupNativeManualExitPcSeen)
    {
        g_vmAutomation.hangupNativeManualExitPcSeen = 1;
        g_vmAutomation.hangupNativeManualExitPcFrame =
            g_vmAutomation.renderFrames;
        vm_autotest_note("automation_hangup_native_auto_exit_manual_pc "
                         "local_pc=0x60c8 code_base=%08x frame=%u\n",
                         codeBase,
                         g_vmAutomation.hangupNativeManualExitPcFrame);
    }
}

static void vm_automation_note_battle_scene_char_list(u32 sequence,
                                                       u32 localPc)
{
    if (!g_vmAutomation.active || !g_vmAutomation.battleStartHandlerSeen ||
        g_vmAutomation.battleSceneCharListSeen)
    {
        return;
    }
    g_vmAutomation.battleSceneCharListSeen = 1;
    g_vmAutomation.battleSceneCharListFrame = g_vmAutomation.renderFrames;
    vm_autotest_note("automation_battle_scene_char_list sequence=%u local_pc=%04x "
                     "frame=%u screen=%08x this=%08x\n",
                     sequence, localPc, g_vmAutomation.renderFrames,
                     vmAddedScreen, g_currentScreenThis);
}

/* Automation receives wire responses before guest memory is populated.  The
 * server response-object grammar is a six-byte header followed by named
 * entries.  Keep this tiny decoder local to the test harness: it reads only
 * the `type` flag from the already validated packet and is never used by the
 * game/client protocol path. */
static bool vm_automation_response_object_u8_field(const u8 *payload,
                                                   u16 payloadLen,
                                                   const char *field,
                                                   u8 *valueOut)
{
    u32 pos = 0;
    size_t fieldLen;

    if (payload == NULL || field == NULL || valueOut == NULL)
        return false;
    fieldLen = strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff)
        return false;
    while (pos < payloadLen)
    {
        u32 nameLen = payload[pos++];
        u16 encodedLen;

        if (nameLen > payloadLen - pos || payloadLen - pos - nameLen < 2u)
            return false;
        if (nameLen == fieldLen &&
            memcmp(payload + pos, field, fieldLen) == 0)
        {
            pos += nameLen;
            encodedLen = (u16)(((u16)payload[pos] << 8) | payload[pos + 1]);
            pos += 2;
            if (encodedLen != 3 || encodedLen > payloadLen - pos ||
                payload[pos] != 0 || payload[pos + 1] != 1)
            {
                return false;
            }
            *valueOut = payload[pos + 2];
            return true;
        }
        pos += nameLen;
        encodedLen = (u16)(((u16)payload[pos] << 8) | payload[pos + 1]);
        pos += 2;
        if (encodedLen > payloadLen - pos)
            return false;
        pos += encodedLen;
    }
    return false;
}

/* The normal battle client receives 4/7 as a named-object payload.  This
 * decoder exists solely for the forensic trace below: it inspects the already
 * validated network bytes before the guest parser owns them. */
static bool vm_hangup_response_object_u32_field(const u8 *payload,
                                                u16 payloadLen,
                                                const char *field,
                                                u32 *valueOut)
{
    u32 pos = 0;
    size_t fieldLen;

    if (payload == NULL || field == NULL || valueOut == NULL)
        return false;
    fieldLen = strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff)
        return false;
    while (pos < payloadLen)
    {
        u32 nameLen = payload[pos++];
        u16 encodedLen;

        if (nameLen > payloadLen - pos || payloadLen - pos - nameLen < 2u)
            return false;
        if (nameLen == fieldLen &&
            memcmp(payload + pos, field, fieldLen) == 0)
        {
            pos += nameLen;
            encodedLen = (u16)(((u16)payload[pos] << 8) | payload[pos + 1]);
            pos += 2;
            if (encodedLen != 6 || encodedLen > payloadLen - pos ||
                payload[pos] != 0 || payload[pos + 1] != 4)
            {
                return false;
            }
            *valueOut = ((u32)payload[pos + 2] << 24) |
                        ((u32)payload[pos + 3] << 16) |
                        ((u32)payload[pos + 4] << 8) |
                        (u32)payload[pos + 5];
            return true;
        }
        pos += nameLen;
        encodedLen = (u16)(((u16)payload[pos] << 8) | payload[pos + 1]);
        pos += 2;
        if (encodedLen > payloadLen - pos)
            return false;
        pos += encodedLen;
    }
    return false;
}

typedef struct
{
    u8 active;
    u8 callbackBeginSeen;
    u8 callbackEndSeen;
    u8 sceneHudSeen;
    u32 sequence;
    u32 recoveryHp;
    u32 recoveryMp;
    u32 responsePtr;
    u32 callback;
} vm_hangup_vital_forensics_trace;

static vm_hangup_vital_forensics_trace g_vmHangupVitalForensics;

/* scene_rebuild_status_meter_node(0x0100FED8) walks this list from
 * R9+0x6048.  These are read-only copies of the already parsed equipment
 * records; logging their two direct vital fields lets us reconcile the
 * client's actual meter calculation with the server's catalog projection. */
static void vm_hangup_vital_forensics_log_hud_equipment(FILE *trace)
{
    u32 item = 0;
    u32 totalHp = 0;
    u32 totalMp = 0;

    if (trace == NULL || MTK == NULL || Global_R9 == 0 ||
        uc_mem_read(MTK, Global_R9 + 0x6048u, &item, sizeof(item)) != UC_ERR_OK)
    {
        return;
    }
    for (u32 index = 0; item != 0 && index < 16u; ++index)
    {
        u32 hpBonus = 0;
        u32 mpBonus = 0;
        u32 next = 0;
        u16 durability = 0;
        u8 category = 0;
        u8 originalCategory = 0;
        u8 enhanceLevel = 0;
        u8 effectCount = 0;

        if (uc_mem_read(MTK, item + 0x04u, &hpBonus, sizeof(hpBonus)) != UC_ERR_OK ||
            uc_mem_read(MTK, item + 0x08u, &mpBonus, sizeof(mpBonus)) != UC_ERR_OK ||
            uc_mem_read(MTK, item + 0x110u, &durability, sizeof(durability)) != UC_ERR_OK ||
            uc_mem_read(MTK, item + 0x11Au, &category, sizeof(category)) != UC_ERR_OK ||
            uc_mem_read(MTK, item + 0x11Bu, &originalCategory,
                        sizeof(originalCategory)) != UC_ERR_OK ||
            uc_mem_read(MTK, item + 0x11Eu, &enhanceLevel,
                        sizeof(enhanceLevel)) != UC_ERR_OK ||
            uc_mem_read(MTK, item + 0x120u, &effectCount,
                        sizeof(effectCount)) != UC_ERR_OK ||
            uc_mem_read(MTK, item + 0x140u, &next, sizeof(next)) != UC_ERR_OK)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_vital_trace equipment=unreadable "
                    "index=%u ptr=%08x\n", index, item);
            break;
        }
        if (durability != 0)
        {
            totalHp += hpBonus;
            totalMp += mpBonus;
        }
        fprintf(trace,
                "[info][network] mock_hangup_vital_trace equipment=%u ptr=%08x "
                "bonus=%u/%u durability=%u category=%u/%u enhance=%u effects=%u\n",
                index, item, hpBonus, mpBonus, durability, category,
                originalCategory, enhanceLevel, effectCount);
        item = next;
    }
    fprintf(trace,
            "[info][network] mock_hangup_vital_trace equipment_direct_total=%u/%u\n",
            totalHp, totalMp);
}

/* Snapshot the scene actor fields and the separate meter-cap object consumed
 * by scene_draw_status_panels(0x0101466A).  The HUD scales node +0xB4/+0xB8
 * against meter +0xC4/+0xC8, rather than against the node's base maxima.
 * Nothing in this helper writes guest memory, invokes a guest function, or
 * affects event scheduling. */
static void vm_hangup_vital_forensics_log_scene_nodes(const char *phase)
{
    FILE *trace;
    u32 sceneNodeBase = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 statusMeterHpMax = 0;
    u32 statusMeterMpMax = 0;

    if (!g_vmHangupVitalForensics.active)
        return;
    trace = fopen("logs/hangup-protocol.log", "ab");
    if (trace == NULL)
        return;
    if (MTK == NULL || Global_R9 == 0 ||
        uc_mem_read(MTK, Global_R9 + 0x5CB0u, &sceneNodeBase,
                    sizeof(sceneNodeBase)) != UC_ERR_OK ||
        sceneNodeBase == 0)
    {
        fprintf(trace,
                "[info][network] mock_hangup_vital_trace phase=%s seq=%u "
                "scene_nodes=unavailable r9=%08x base=%08x delta=%u/%u\n",
                phase ? phase : "-", g_vmHangupVitalForensics.sequence,
                Global_R9, sceneNodeBase, g_vmHangupVitalForensics.recoveryHp,
                g_vmHangupVitalForensics.recoveryMp);
        fflush(trace);
        fclose(trace);
        return;
    }
    if (uc_mem_read(MTK, Global_R9 + 0x5CA4u, &currentSceneNode,
                    sizeof(currentSceneNode)) == UC_ERR_OK &&
        uc_mem_read(MTK, Global_R9 + 0x5CACu, &statusMeterNode,
                    sizeof(statusMeterNode)) == UC_ERR_OK &&
        statusMeterNode != 0)
    {
        (void)uc_mem_read(MTK, statusMeterNode + 0xC4u,
                          &statusMeterHpMax, sizeof(statusMeterHpMax));
        (void)uc_mem_read(MTK, statusMeterNode + 0xC8u,
                          &statusMeterMpMax, sizeof(statusMeterMpMax));
    }
    fprintf(trace,
            "[info][network] mock_hangup_vital_trace phase=%s seq=%u "
            "hud_node=%08x meter=%08x bar_max=%u/%u delta=%u/%u\n",
            phase ? phase : "-", g_vmHangupVitalForensics.sequence,
            currentSceneNode, statusMeterNode,
            statusMeterHpMax, statusMeterMpMax,
            g_vmHangupVitalForensics.recoveryHp,
            g_vmHangupVitalForensics.recoveryMp);
    vm_hangup_vital_forensics_log_hud_equipment(trace);
    for (u32 i = 0; i < 8; ++i)
    {
        u32 node = sceneNodeBase + i * 340u;
        u32 roleId = 0;
        u32 hp = 0;
        u32 mp = 0;
        u32 hpMax = 0;
        u32 mpMax = 0;

        if (uc_mem_read(MTK, node + 0x64u, &roleId, sizeof(roleId)) != UC_ERR_OK ||
            uc_mem_read(MTK, node + 0xB4u, &hp, sizeof(hp)) != UC_ERR_OK ||
            uc_mem_read(MTK, node + 0xB8u, &mp, sizeof(mp)) != UC_ERR_OK ||
            uc_mem_read(MTK, node + 0xBCu, &hpMax, sizeof(hpMax)) != UC_ERR_OK ||
            uc_mem_read(MTK, node + 0xC0u, &mpMax, sizeof(mpMax)) != UC_ERR_OK)
        {
            continue;
        }
        if (roleId == 0 && hp == 0 && mp == 0 && hpMax == 0 && mpMax == 0)
            continue;
        fprintf(trace,
                "[info][network] mock_hangup_vital_trace phase=%s seq=%u "
                "node=%u actor=%u hp=%u/%u mp=%u/%u delta=%u/%u\n",
                phase ? phase : "-", g_vmHangupVitalForensics.sequence,
                i, roleId, hp, hpMax, mp, mpMax,
                g_vmHangupVitalForensics.recoveryHp,
                g_vmHangupVitalForensics.recoveryMp);
    }
    fflush(trace);
    fclose(trace);
}

static void vm_hangup_vital_forensics_capture_response(const u8 *packet,
                                                        u32 packetLen,
                                                        u32 eventType,
                                                        u32 sequence,
                                                        u32 responsePtr,
                                                        u32 callback)
{
    u32 offset = 5;
    u8 objectCount;
    u32 recoveryHp = 0;
    u32 recoveryMp = 0;
    bool foundSettlement = false;

    if (packet == NULL || eventType != 7 || packetLen < 5 ||
        packet[0] != 'W' || packet[1] != 'T' ||
        (((u32)packet[2] << 8) | packet[3]) != packetLen)
    {
        return;
    }
    objectCount = packet[4];
    for (u8 index = 0; index < objectCount; ++index)
    {
        u16 objectLen;

        if (offset + 6u > packetLen)
            return;
        objectLen = (u16)(((u16)packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return;
        if (packet[offset] == 1 && packet[offset + 1] == 4 &&
            packet[offset + 2] == 7 &&
            vm_hangup_response_object_u32_field(packet + offset + 6u,
                                                 (u16)(objectLen - 6u),
                                                 "hp", &recoveryHp) &&
            vm_hangup_response_object_u32_field(packet + offset + 6u,
                                                 (u16)(objectLen - 6u),
                                                 "mp", &recoveryMp))
        {
            foundSettlement = true;
            break;
        }
        offset += objectLen;
    }
    if (!foundSettlement)
        return;

    memset(&g_vmHangupVitalForensics, 0, sizeof(g_vmHangupVitalForensics));
    g_vmHangupVitalForensics.active = 1;
    g_vmHangupVitalForensics.sequence = sequence;
    g_vmHangupVitalForensics.recoveryHp = recoveryHp;
    g_vmHangupVitalForensics.recoveryMp = recoveryMp;
    g_vmHangupVitalForensics.responsePtr = responsePtr;
    g_vmHangupVitalForensics.callback = callback;
    {
        FILE *trace = fopen("logs/hangup-protocol.log", "ab");
        u32 objectOffset = 5;

        if (trace != NULL)
        {
            fprintf(trace,
                    "[info][network] mock_hangup_terminal_packet "
                    "sequence=%u objects=%u order=",
                    sequence, objectCount);
            for (u8 index = 0; index < objectCount; ++index)
            {
                u16 objectLen;

                if (objectOffset + 6u > packetLen)
                {
                    fprintf(trace, " malformed");
                    break;
                }
                objectLen = (u16)(((u16)packet[objectOffset + 4] << 8) |
                                  packet[objectOffset + 5]);
                if (objectLen < 6u || objectOffset + objectLen > packetLen)
                {
                    fprintf(trace, " malformed");
                    break;
                }
                fprintf(trace, "%s%u/%u[%u]",
                        index == 0 ? "" : ",",
                        packet[objectOffset + 1], packet[objectOffset + 2],
                        objectLen);
                objectOffset += objectLen;
            }
            fprintf(trace, "\n");
            fflush(trace);
            fclose(trace);
        }
    }
    vm_hangup_vital_forensics_log_scene_nodes("queued-before-4-7");
}

static void vm_hangup_vital_forensics_callback_begin(u32 eventType,
                                                      u32 responsePtr,
                                                      u32 callback)
{
    if (!g_vmHangupVitalForensics.active || eventType != 7 ||
        g_vmHangupVitalForensics.responsePtr != responsePtr ||
        g_vmHangupVitalForensics.callback != callback)
    {
        return;
    }
    g_vmHangupVitalForensics.callbackBeginSeen = 1;
    vm_hangup_vital_forensics_log_scene_nodes("callback-before-4-7");
}

static void vm_hangup_vital_forensics_callback_end(u32 eventType,
                                                    u32 responsePtr,
                                                    u32 callback)
{
    if (!g_vmHangupVitalForensics.active || eventType != 7 ||
        !g_vmHangupVitalForensics.callbackBeginSeen ||
        g_vmHangupVitalForensics.responsePtr != responsePtr ||
        g_vmHangupVitalForensics.callback != callback)
    {
        return;
    }
    g_vmHangupVitalForensics.callbackEndSeen = 1;
    vm_hangup_vital_forensics_log_scene_nodes("callback-after-4-7");
}

static void vm_hangup_vital_forensics_note_pc(u32 pc)
{
    if (!g_vmHangupVitalForensics.active ||
        !g_vmHangupVitalForensics.callbackEndSeen ||
        g_vmHangupVitalForensics.sceneHudSeen || pc != 0x0101466Au)
    {
        return;
    }
    g_vmHangupVitalForensics.sceneHudSeen = 1;
    vm_hangup_vital_forensics_log_scene_nodes("scene-hud-after-4-7");
    g_vmHangupVitalForensics.active = 0;
}

/*
 * Battle-insight status forensic window
 *
 * The 25/6 item-use callback and the generic 1/1 state parser are separate
 * CBE paths.  The server can legally return bytes only through the callback
 * registered by the guest for this request; this probe records whether that
 * very callback reaches the generic subtype-6 / expbook branch.  It does not
 * select a callback, inspect a guest global, or alter any event, register,
 * packet byte, or CBE memory.
 */
typedef struct
{
    u8 active;
    u8 callbackActive;
    u8 callbackBeginSeen;
    u8 callbackEndSeen;
    u8 timedUseHandlerSeen;
    u8 genericStateDispatchSeen;
    u8 subtype6BranchSeen;
    u8 expbookFieldSeen;
    u8 statusMeterRebuildSeen;
    u8 status6Attached;
    u8 objectCount;
    u8 firstKind;
    u8 firstSubtype;
    u8 maxnumGetterSeen;
    u32 sequence;
    u32 responsePtr;
    u32 callback;
    u32 context;
    u32 connectId;
    u32 responseLen;
    u32 maxnumGetterValue;
} vm_battle_insight_forensics_trace;

static vm_battle_insight_forensics_trace g_vmBattleInsightForensics;

static bool vm_battle_insight_forensics_is_refresh_candidate_response(
    const u8 *packet, u32 packetLen, u8 *status6Attached,
    const char **phaseOut)
{
    u32 firstLen;
    u32 secondOffset;

    if (status6Attached != NULL)
        *status6Attached = 0;
    if (phaseOut != NULL)
        *phaseOut = NULL;
    if (packet == NULL || packetLen < 11u || packet[0] != 'W' ||
        packet[1] != 'T' || (((u32)packet[2] << 8) | packet[3]) != packetLen ||
        packet[4] == 0u || packet[5] != 1u)
    {
        return false;
    }
    firstLen = ((u32)packet[9] << 8) | packet[10];
    if (firstLen < 6u || 5u + firstLen > packetLen)
        return false;
    if (packet[6] == 25u && packet[7] == 6u)
    {
        if (phaseOut != NULL)
            *phaseOut = "queued-25-6";
        if (packet[4] < 2u || 5u + firstLen + 6u > packetLen)
            return true;
        secondOffset = 5u + firstLen;
        if (packet[secondOffset] == 1u && packet[secondOffset + 1u] == 1u &&
            packet[secondOffset + 2u] == 6u && status6Attached != NULL)
        {
            *status6Attached = 1;
        }
        return true;
    }
    if (packet[6] == 25u && packet[7] == 7u)
    {
        if (phaseOut != NULL)
            *phaseOut = "queued-25-7";
        return true;
    }
    if (packet[6] == 2u && packet[7] == 10u)
    {
        if (phaseOut != NULL)
            *phaseOut = "queued-2-10";
        return true;
    }
    return false;
}

static void vm_battle_insight_forensics_append(const char *phase, u32 pc,
                                                uc_err callbackErr)
{
    FILE *trace;

    trace = fopen("logs/battle-insight-status-trace.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "[info][forensics] battle_insight_status phase=%s seq=%u "
            "connect=%u response=%08x bytes=%u callback=%08x context=%08x "
            "cbe=JianghuOL.CBE local_pc=%08x event=7 "
            "objects=%u first=1/%u/%u appended_status6=%u handler25_6=%u maxnum_get=%u generic_1_1=%u "
            "subtype6=%u expbook=%u "
            "status_rebuild=%u callback_err=%u packet_unchanged=1\n",
            phase ? phase : "-", g_vmBattleInsightForensics.sequence,
            g_vmBattleInsightForensics.connectId,
            g_vmBattleInsightForensics.responsePtr,
            g_vmBattleInsightForensics.responseLen,
            g_vmBattleInsightForensics.callback,
            g_vmBattleInsightForensics.context, pc,
            g_vmBattleInsightForensics.objectCount,
            g_vmBattleInsightForensics.firstKind,
            g_vmBattleInsightForensics.firstSubtype,
            g_vmBattleInsightForensics.status6Attached,
            g_vmBattleInsightForensics.timedUseHandlerSeen,
            g_vmBattleInsightForensics.maxnumGetterValue,
            g_vmBattleInsightForensics.genericStateDispatchSeen,
            g_vmBattleInsightForensics.subtype6BranchSeen,
            g_vmBattleInsightForensics.expbookFieldSeen,
            g_vmBattleInsightForensics.statusMeterRebuildSeen,
            (u32)callbackErr);
    fflush(trace);
    fclose(trace);
}

static void vm_battle_insight_forensics_capture_response(const u8 *packet,
                                                          u32 packetLen,
                                                          u32 eventType,
                                                          u32 sequence,
                                                          u32 responsePtr,
                                                          u32 callback,
                                                          u32 context,
                                                          u32 connectId)
{
    u8 status6Attached = 0;
    const char *phase = NULL;

    if (eventType != 7u || callback == 0 ||
        !vm_battle_insight_forensics_is_refresh_candidate_response(
            packet, packetLen, &status6Attached, &phase))
    {
        return;
    }
    memset(&g_vmBattleInsightForensics, 0, sizeof(g_vmBattleInsightForensics));
    g_vmBattleInsightForensics.active = 1;
    g_vmBattleInsightForensics.sequence = sequence;
    g_vmBattleInsightForensics.responsePtr = responsePtr;
    g_vmBattleInsightForensics.callback = callback;
    g_vmBattleInsightForensics.context = context;
    g_vmBattleInsightForensics.connectId = connectId;
    g_vmBattleInsightForensics.responseLen = packetLen;
    g_vmBattleInsightForensics.objectCount = packet[4];
    g_vmBattleInsightForensics.firstKind = packet[6];
    g_vmBattleInsightForensics.firstSubtype = packet[7];
    g_vmBattleInsightForensics.status6Attached = status6Attached;
    vm_battle_insight_forensics_append(phase, 0u, UC_ERR_OK);
}

static void vm_battle_insight_forensics_callback_begin(u32 eventType,
                                                        u32 responsePtr,
                                                        u32 callback,
                                                        u32 context)
{
    if (!g_vmBattleInsightForensics.active || eventType != 7u ||
        g_vmBattleInsightForensics.responsePtr != responsePtr ||
        g_vmBattleInsightForensics.callback != callback ||
        g_vmBattleInsightForensics.context != context)
    {
        return;
    }
    g_vmBattleInsightForensics.callbackActive = 1;
    g_vmBattleInsightForensics.callbackBeginSeen = 1;
    vm_battle_insight_forensics_append("callback-begin", 0u, UC_ERR_OK);
}

static void vm_battle_insight_forensics_callback_end(u32 eventType,
                                                      u32 responsePtr,
                                                      u32 callback,
                                                      u32 context,
                                                      uc_err callbackErr)
{
    if (!g_vmBattleInsightForensics.active || !g_vmBattleInsightForensics.callbackActive ||
        eventType != 7u || g_vmBattleInsightForensics.responsePtr != responsePtr ||
        g_vmBattleInsightForensics.callback != callback ||
        g_vmBattleInsightForensics.context != context)
    {
        return;
    }
    g_vmBattleInsightForensics.callbackActive = 0;
    g_vmBattleInsightForensics.callbackEndSeen = 1;
    vm_battle_insight_forensics_append("callback-end", 0u, callbackErr);
    g_vmBattleInsightForensics.active = 0;
}

static void vm_battle_insight_forensics_note_pc(u32 pc)
{
    u8 *seen = NULL;

    if (!g_vmBattleInsightForensics.active ||
        !g_vmBattleInsightForensics.callbackActive)
    {
        return;
    }
    /* The prior BLX at 0x0102659C has just returned the native maxnum
     * accessor's value in R0.  Read it only for this armed 25/6 response;
     * do not write the register, packet, or client state. */
    if (pc == 0x0102659Eu)
    {
        if (!g_vmBattleInsightForensics.maxnumGetterSeen)
        {
            (void)uc_reg_read(MTK, UC_ARM_REG_R0,
                              &g_vmBattleInsightForensics.maxnumGetterValue);
            g_vmBattleInsightForensics.maxnumGetterSeen = 1;
            vm_battle_insight_forensics_append("maxnum-get", pc, UC_ERR_OK);
        }
        return;
    }
    switch (pc)
    {
    case 0x01026574u: /* native 25/6 response handler */
        seen = &g_vmBattleInsightForensics.timedUseHandlerSeen;
        break;
    case 0x010132F8u: /* generic kind-1 state-object dispatcher */
        seen = &g_vmBattleInsightForensics.genericStateDispatchSeen;
        break;
    case 0x01013398u: /* subtype-6 branch within that dispatcher */
        seen = &g_vmBattleInsightForensics.subtype6BranchSeen;
        break;
    case 0x010133CCu: /* object field accessor for literal "expbook" */
        seen = &g_vmBattleInsightForensics.expbookFieldSeen;
        break;
    case 0x01013594u: /* status-meter rebuild following state dispatch */
        seen = &g_vmBattleInsightForensics.statusMeterRebuildSeen;
        break;
    default:
        return;
    }
    if (*seen)
        return;
    *seen = 1;
    vm_battle_insight_forensics_append("pc-hit", pc, UC_ERR_OK);
}

static void vm_automation_note_network_response(const u8 *packet, u32 packetLen,
                                                 u32 eventType, u32 sequence)
{
    u32 offset = 5;
    u8 objectCount;
    u8 sawTaskSubset = 0;
    u8 sawTitleUpdateComplete = 0;
    u8 sawTitleLoginResponse = 0;
    u8 sawShopStatus = 0;
    u8 sawShopMoney = 0;
    u8 sawSceneComplete = 0;
    u8 sawHangup = 0;
    u8 sawAutoEnable = 0;
    u8 sawAutoDisable = 0;
    u8 sawNativeAutoExit = 0;
    u8 sawSettlement = 0;
    u8 sawBattleAction = 0;
    u8 sawActorOtherAck = 0;
    u8 sawModuleUpdateChunk = 0;
    u8 sawEquipmentEnhanceStage1 = 0;
    u8 sawDreamNpcInstanceEnter = 0;

    if (!g_vmAutomation.active ||
        packet == NULL || eventType != 7 ||
        packetLen < 5 || packet[0] != 'W' || packet[1] != 'T' ||
        (((u32)packet[2] << 8) | packet[3]) != packetLen)
        return;
    objectCount = packet[4];
    for (u8 index = 0; index < objectCount; ++index)
    {
        u16 objectLen;
        u8 kind;
        u8 subtype;
        if (offset + 6u > packetLen)
            return;
        objectLen = (u16)(((u16)packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return;
        kind = packet[offset + 1];
        subtype = packet[offset + 2];
        if (kind == 25 && subtype == 5)
            sawTaskSubset = 1;
        if (kind == 18 && subtype == 9)
            sawTitleUpdateComplete = 1;
        if (kind == 18 && subtype == 6)
            sawModuleUpdateChunk = 1;
        if (kind == 1 && subtype == 12)
            sawTitleLoginResponse = 1;
        if (kind == 14 && subtype == 14)
            sawShopStatus = 1;
        if (kind == 14 && subtype == 4)
            sawShopMoney = 1;
        if (kind == 30 && subtype == 2)
            sawSceneComplete = 1;
        if (kind == 30 && subtype == 1)
            sawDreamNpcInstanceEnter = 1;
        if (kind == 2 && subtype == 10)
            sawActorOtherAck = 1;
        if (kind == 29 && subtype == 1)
            sawEquipmentEnhanceStage1 = 1;
        if (kind == 4 && subtype == 5)
            sawHangup = 1;
        if (kind == 4 && subtype == 6)
            sawBattleAction = 1;
        /* A successful scene-hangup victory must present the native 4/7
         * settlement panel.  4/8 is the revival packet and is deliberately
         * not accepted as a normal-terminal signal. */
        if (kind == 4 && subtype == 7)
            sawSettlement = 1;
        if (kind == 4 && subtype == 11)
        {
            u8 autoType = 0xff;
            if (vm_automation_response_object_u8_field(
                    packet + offset + 6u, (u16)(objectLen - 6u),
                    "type", &autoType))
            {
                if (autoType == 1)
                    sawAutoEnable = 1;
                else if (autoType == 0)
                    sawAutoDisable = 1;
            }
        }
        if (kind == 25 && subtype == 2)
        {
            u8 result = 0;
            u8 type = 0;

            if (vm_automation_response_object_u8_field(
                    packet + offset + 6u, (u16)(objectLen - 6u),
                    "result", &result) &&
                vm_automation_response_object_u8_field(
                    packet + offset + 6u, (u16)(objectLen - 6u),
                    "type", &type) &&
                result == 1 && type == 1)
            {
                sawNativeAutoExit = 1;
            }
        }
        offset += objectLen;
    }
    if (offset != packetLen)
        return;

    if (sawModuleUpdateChunk)
    {
        /* The update trace is intentionally automation-only and read-only.
         * It captures the client-owned resume state before its network
         * callback parses this 18/6 packet, so an interrupted module update
         * can be attributed to the first failing lifecycle transition. */
        vm_autotest_trace_update_state("response-queued", sequence,
                                       packet, packetLen);
    }
    if (sawDreamNpcInstanceEnter &&
        g_vmAutomation.scenario ==
            VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE &&
        !g_vmAutomation.dreamNpcInstanceEnterResponseSeen)
    {
        g_vmAutomation.dreamNpcInstanceEnterResponseSeen = 1;
        g_vmAutomation.dreamNpcInstanceEnterResponseSequence = sequence;
        vm_autotest_note("automation_dream_npc_instance_enter_response "
                         "seq=%u frame=%u object=30/1\n",
                         sequence, g_vmAutomation.renderFrames);
    }
    if (sawTaskSubset)
    {
        g_vmAutomation.initialScenePacketSeen = 1;
        g_vmAutomation.initialScenePacketFrame = g_vmAutomation.renderFrames;
        if (g_vmAutomation.scenario ==
                VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE &&
            g_vmAutomation.dreamNpcInstanceEnterResponseSeen &&
            !g_vmAutomation.dreamNpcTargetScenePacketSeen)
        {
            g_vmAutomation.dreamNpcTargetScenePacketSeen = 1;
            g_vmAutomation.dreamNpcTargetScenePacketSequence = sequence;
            g_vmAutomation.dreamNpcTargetScenePacketFrame =
                g_vmAutomation.renderFrames;
            vm_autotest_note("automation_dream_npc_target_scene_packet "
                             "seq=%u frame=%u source=25-5-after-30-1\n",
                             sequence, g_vmAutomation.renderFrames);
        }
    }
    if (sawTitleUpdateComplete && !g_vmAutomation.titleUpdateCompleteSeen)
    {
        g_vmAutomation.titleUpdateCompleteSeen = 1;
        g_vmAutomation.titleUpdateFrame = g_vmAutomation.renderFrames;
    }
    if (sawTitleLoginResponse)
        g_vmAutomation.titleLoginResponseSeen = 1;
    if (sawShopStatus)
        g_vmAutomation.shopStatusSeen = 1;
    if (sawShopMoney)
        g_vmAutomation.shopMoneySeen = 1;
    if (g_vmAutomation.shopStatusSeen && g_vmAutomation.shopMoneySeen &&
        g_vmAutomation.shopDataPacketFrame == 0)
    {
        g_vmAutomation.shopDataPacketFrame = g_vmAutomation.renderFrames;
        g_vmAutomation.shopDataPacketSequence = sequence;
    }
    if (sawSceneComplete && g_vmAutomation.shopStatusSeen &&
        !g_vmAutomation.shopReturnSeen)
    {
        /* Transport observation is deliberately not UI readiness: the guest
         * callback still owns removal of mmShop and restoration of mmGame.
         * Preserve this boundary so the following touch cannot be queued to
         * the outgoing shop screen. */
        g_vmAutomation.shopReturnSeen = 1;
        g_vmAutomation.shopReturnPacketFrame = g_vmAutomation.renderFrames;
        g_vmAutomation.shopReturnPacketSequence = sequence;
    }
    if (sawTaskSubset && g_vmAutomation.shopReturnSeen &&
        sequence > g_vmAutomation.shopReturnPacketSequence &&
        !g_vmAutomation.shopReturnFollowupSeen)
    {
        /* This 25/5 comes from the replacement scene shell, not the
         * outgoing shop.  Arrival is still not UI readiness: the state
         * machine also requires ScreenInit and two later render boundaries. */
        g_vmAutomation.shopReturnFollowupSeen = 1;
        g_vmAutomation.shopReturnFollowupFrame = g_vmAutomation.renderFrames;
        g_vmAutomation.shopReturnFollowupSequence = sequence;
        vm_autotest_note("automation_shop_return_followup seq=%u frame=%u\n",
                         sequence, g_vmAutomation.renderFrames);
    }
    if (sawHangup)
    {
        char packetPath[640];
        FILE *packetFile;

        g_vmAutomation.hangupBattleResponseSeen = 1;
        ++g_vmAutomation.hangupBattleResponseCount;
        /* The shop-return and direct controls must be compared byte-for-byte
         * before attributing a later screen-lifecycle split to the client.
         * Persist only the uniquely identified 4/5 start response inside the
         * isolated artifact directory; this never feeds back into transport. */
        if (g_vmAutomation.artifactDir[0] != 0)
        {
            snprintf(packetPath, sizeof(packetPath),
                     "%s/hangup-response-%u.wt", g_vmAutomation.artifactDir,
                     sequence);
            packetFile = fopen(packetPath, "wb");
            if (packetFile != NULL)
            {
                (void)fwrite(packet, 1, packetLen, packetFile);
                fclose(packetFile);
                vm_autotest_note("automation_hangup_response_saved seq=%u len=%u path=%s\n",
                                 sequence, packetLen, packetPath);
            }
        }
    }
    if (sawAutoEnable)
        g_vmAutomation.hangupAutoEnableResponseSeen = 1;
    if (sawAutoDisable)
        g_vmAutomation.hangupAutoDisableResponseSeen = 1;
    if (sawNativeAutoExit &&
        g_vmAutomation.scenario ==
            VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT &&
        !g_vmAutomation.hangupNativeAutoExitResponseSeen)
    {
        g_vmAutomation.hangupNativeAutoExitResponseSeen = 1;
        g_vmAutomation.hangupNativeAutoExitResponseSequence = sequence;
        vm_autotest_note("automation_hangup_native_auto_exit_response "
                         "seq=%u result=1 type=1 frame=%u\n",
                         sequence, g_vmAutomation.renderFrames);
    }
    if (sawBattleAction)
    {
        ++g_vmAutomation.battleActionResponseCount;
        vm_autotest_note("automation_hangup_action_response seq=%u count=%u frame=%u\n",
                         sequence, g_vmAutomation.battleActionResponseCount,
                         g_vmAutomation.renderFrames);
    }
    if (sawEquipmentEnhanceStage1 &&
        g_vmAutomation.scenario ==
            VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE &&
        !g_vmAutomation.equipmentEnhanceStage1ResponseSeen)
    {
        g_vmAutomation.equipmentEnhanceStage1ResponseSeen = 1;
        g_vmAutomation.equipmentEnhanceStage1ResponseFrame =
            g_vmAutomation.renderFrames;
        g_vmAutomation.equipmentEnhanceStage1ResponseSequence = sequence;
        vm_autotest_note("automation_equipment_enhance_stage1_response seq=%u frame=%u\n",
                         sequence, g_vmAutomation.renderFrames);
    }
    if (sawSettlement)
    {
        g_vmAutomation.hangupSettlementResponseSeen = 1;
        ++g_vmAutomation.hangupSettlementResponseCount;
        g_vmAutomation.hangupSettlementInputCount = g_vmAutomation.inputCount;
        g_vmAutomation.hangupSettlementActionResponseCount =
            g_vmAutomation.battleActionResponseCount;
        vm_autotest_note("automation_hangup_settlement seq=%u count=%u inputs=%u frame=%u\n",
                         sequence, g_vmAutomation.hangupSettlementResponseCount,
                         g_vmAutomation.inputCount, g_vmAutomation.renderFrames);
    }
    vm_autotest_note("automation_packet seq=%u title_update=%u title_login=%u task_subset=%u shop=%u/%u scene_enter=%u scene_complete=%u actor_other_ack=%u enhance_stage1=%u hangup=%u auto=%u/%u native_exit25_2=%u settlement=%u hangup_count=%u action_count=%u\n",
                     sequence, sawTitleUpdateComplete, sawTitleLoginResponse, sawTaskSubset, sawShopStatus, sawShopMoney,
                     sawDreamNpcInstanceEnter, sawSceneComplete, sawActorOtherAck, sawEquipmentEnhanceStage1, sawHangup, sawAutoEnable, sawAutoDisable,
                     sawNativeAutoExit, sawSettlement, g_vmAutomation.hangupBattleResponseCount,
                     g_vmAutomation.battleActionResponseCount);
}

static void vm_automation_tick(void)
{
    u32 now;
    u32 elapsed;

    if (!g_vmAutomation.active || g_vmAutomation.finished)
        return;
    now = SDL_GetTicks();
    if (g_vmAutomation.totalStartedMs == 0)
    {
        g_vmAutomation.totalStartedMs = now;
        g_vmAutomation.stageStartedMs = now;
    }
    elapsed = now - g_vmAutomation.totalStartedMs;
    if (elapsed > g_vmAutomation.totalTimeoutMs)
    {
        vm_automation_finish(0, "total-timeout");
        return;
    }
    {
        u32 stageTimeoutMs =
            g_vmAutomation.stage == VM_AUTOMATION_STAGE_WAIT_TIMED_TITLE_BOOTSTRAP
                ? 60000u : g_vmAutomation.stepTimeoutMs;
        if (g_vmAutomation.stage == VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_TERMINAL)
            stageTimeoutMs = 45000u;
        else if (g_vmAutomation.stage ==
                 VM_AUTOMATION_STAGE_WAIT_HANGUP_NATIVE_AUTO_EXIT)
            stageTimeoutMs = 60000u;
        else if (g_vmAutomation.stage ==
                 VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW)
            stageTimeoutMs = 75000u;
        else if (g_vmAutomation.stage ==
                 VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_CANCEL_RESPONSE)
            stageTimeoutMs = 30000u;
        if (now - g_vmAutomation.stageStartedMs > stageTimeoutMs)
        {
            if (g_vmAutomation.stage ==
                VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW)
            {
                vm_automation_request_capture("dream-map-number-not-observed");
                vm_automation_finish(0, "dream-map-number-not-observed");
                return;
            }
            vm_automation_finish(0, "stage-timeout");
            return;
        }
    }

    switch (g_vmAutomation.stage)
    {
    case VM_AUTOMATION_STAGE_WAIT_TITLE_MODULE_UPDATE:
        if (g_vmAutomation.titleModuleUpdateLifecycleRejectSeen)
        {
            vm_automation_finish(0, "title-module-update-lifecycle-reject");
        }
        else if (g_vmAutomation.titleModuleUpdateCompleted)
        {
            vm_automation_request_capture("title-module-update-installed");
            vm_automation_finish(1, "native-title-module-18-6-install-complete");
        }
        break;
    case VM_AUTOMATION_STAGE_BOOT_CONFIRM:
        if (g_vmAutomation.renderFrames >= 2 &&
            vm_automation_issue_key('f', "two-rendered-boot-frames"))
            vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_TITLE_LOGIN_DISPATCH,
                                    "boot-confirm-sent");
        break;
    case VM_AUTOMATION_STAGE_WAIT_TIMED_TITLE_BOOTSTRAP:
        /* The runner declares the proven title input sequence as one-shot
         * scheduled hardware events.  Do not infer login success from those
         * timestamps: advancement starts only after the real initial 25/5
         * scene packet and two further render boundaries. */
        if (g_vmAutomation.initialScenePacketSeen &&
            g_vmAutomation.renderFrames >= g_vmAutomation.initialScenePacketFrame + 2u)
        {
            vm_automation_cancel_pending_timed_inputs("initial-scene-packet-proven");
            vm_automation_request_capture(
                g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_SCENE_TELEPORT_STONE_PROBE
                    ? "teleport-stone-scene-ready" : "scene-ready");
            if (g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_SCENE_TELEPORT_STONE_PROBE)
            {
                /* This probe proves only the client-owned scene bootstrap and
                 * captures the authored marker scene.  It deliberately does
                 * not manufacture the next scene interaction request. */
                vm_automation_finish(1, "initial-scene-25-5-and-rendered-frame");
            }
            else if (g_vmAutomation.scenario ==
                     VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE)
            {
                /* The origin scene contains one fixture-owned 30406 at the
                 * role's coordinates.  No world-coordinate tap is guessed:
                 * the following two confirm keys wait for the declared CBE
                 * prompt/parser PCs in vm_automation_note_dream_npc_entry_pc.
                 */
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                vm_automation_request_capture("dream-npc-origin-scene-ready");
                vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_PROMPT,
                                        "origin-25-5-and-rendered-frame");
            }
            else if (g_vmAutomation.scenario ==
                     VM_AUTOMATION_SCENARIO_DREAM_CLOCK_PROBE)
            {
                /* The exact observed numeric rectangle is monitored only
                 * after a normal scene 25/5 and two LCD frames. No map tap,
                 * movement, combat action, request, or callback is issued. */
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                g_vmAutomation.dreamClockProbeArmed = 1;
                g_vmAutomation.dreamClockProbeArmFrame =
                    g_vmAutomation.renderFrames;
                vm_automation_request_capture("dream-scene-ready");
                vm_automation_set_stage(
                    VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW,
                    "native-scene-25-5-and-rendered-frame");
            }
            else if (g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_RULES_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
            {
                /* The rules probe starts from the native equipment screen;
                 * its companion starts from the backpack so the real
                 * enhanced instance can be selected.  Both actions remain
                 * ordinary touchscreen events after the scene boundary. */
                const int targetX =
                    (g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
                        ? 132 : 102;
                const char *targetName =
                    (g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
                        ? "scene-toolbar-backpack-icon"
                        : "scene-toolbar-equipment-icon";
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                vm_automation_request_capture("equipment-enhance-probe-scene");
                if (vm_automation_issue_tap(targetX, 44, targetName))
                {
                    vm_automation_set_stage(
                        VM_AUTOMATION_STAGE_WAIT_EQUIPMENT_ENHANCE_RULES,
                        (g_vmAutomation.scenario ==
                             VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                         g_vmAutomation.scenario ==
                             VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
                            ? "backpack-toolbar-tapped"
                            : "equipment-toolbar-tapped");
                }
            }
            else if (vm_automation_scenario_uses_direct_hangup())
            {
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                vm_automation_request_capture("direct-hangup-scene");
                if (vm_automation_issue_tap(50, 350, "scene-hangup-control"))
                    vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE,
                                            "direct-hangup-control-tapped");
            }
            else if (vm_automation_issue_tap(
                         224, 44, "scene-toolbar-rightmost-shop-icon"))
            {
                /* The user has fixed the UI contract for this regression:
                 * this is the visible, right-most scene toolbar icon, whose
                 * native action opens mmShop.  It replaces the disproved
                 * equipment-icon probe at (102,44); it is one hardware
                 * event, never a coordinate sweep or a synthetic request. */
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                vm_autotest_note("automation_scene_owner phase=initial screen=%08x this=%08x frame=%u\n",
                                 vmAddedScreen, g_currentScreenThis,
                                 g_vmAutomation.renderFrames);
                vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_SHOP_OPEN,
                                        "rightmost-shop-icon-tapped");
            }
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_TITLE_LOGIN_DISPATCH:
        if (g_vmAutomation.titleScreenInitialized &&
            g_vmAutomation.renderFrames >= g_vmAutomation.titleScreenInitFrame + 2u &&
            /* The rendered title menu's visible first row is 开始游戏.  Use
             * its center through the existing touchscreen event queue; this
             * is one declared target, not a candidate click sweep. */
            vm_automation_issue_tap(120, 252,
                                    "title-visible-start-game-row-center"))
            vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_ROLE_LIST,
                                    "title-start-row-tapped");
        break;
    case VM_AUTOMATION_STAGE_WAIT_ROLE_LIST:
        if (g_vmAutomation.titleLoginResponseSeen &&
            !g_vmAutomation.titleLoginResponseCaptured)
        {
            vm_automation_request_capture("title-login-response");
            g_vmAutomation.titleLoginResponseCaptured = 1;
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_INITIAL_SCENE:
        if (g_vmAutomation.initialScenePacketSeen &&
            g_vmAutomation.renderFrames >= g_vmAutomation.stageFrame + 2u)
        {
            vm_automation_request_capture("scene-ready");
            if (vm_automation_scenario_uses_direct_hangup())
            {
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                vm_automation_request_capture("direct-hangup-scene");
                if (vm_automation_issue_tap(50, 350, "scene-hangup-control"))
                    vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE,
                                            "direct-hangup-control-tapped");
            }
            else if (g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_RULES_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
            {
                const int targetX =
                    (g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
                        ? 132 : 102;
                const char *targetName =
                    (g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
                        ? "scene-toolbar-backpack-icon"
                        : "scene-toolbar-equipment-icon";
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                vm_automation_request_capture("equipment-enhance-probe-scene");
                if (vm_automation_issue_tap(targetX, 44, targetName))
                {
                    vm_automation_set_stage(
                        VM_AUTOMATION_STAGE_WAIT_EQUIPMENT_ENHANCE_RULES,
                        (g_vmAutomation.scenario ==
                             VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE ||
                         g_vmAutomation.scenario ==
                             VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
                            ? "backpack-toolbar-tapped"
                            : "equipment-toolbar-tapped");
                }
            }
            else if (vm_automation_issue_tap(
                    224, 44, "scene-toolbar-rightmost-shop-icon"))
            {
                g_vmAutomation.initialSceneScreen = vmAddedScreen;
                vm_autotest_note("automation_scene_owner phase=initial screen=%08x this=%08x frame=%u\n",
                                 vmAddedScreen, g_currentScreenThis,
                                 g_vmAutomation.renderFrames);
                vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_SHOP_OPEN,
                                        "rightmost-shop-icon-tapped");
            }
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW:
        /* Completion is driven only by vm_automation_note_scene_number_draw.
         * The bounded timeout above records a negative observation. */
        break;
    case VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_PROMPT:
        if (g_vmAutomation.dreamNpcPromptPcSeen &&
            !g_vmAutomation.dreamNpcPromptConfirmSent &&
            vm_automation_issue_key('f',
                                    "JianghuOL.CBE+0x015154-nearest-npc-prompt"))
        {
            g_vmAutomation.dreamNpcPromptConfirmSent = 1;
            vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_DIALOG,
                                    "npc-prompt-confirm-key-sent");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_DIALOG:
        if (g_vmAutomation.dreamNpcDialogParserPcSeen &&
            !g_vmAutomation.dreamNpcDialogConfirmSent &&
            vm_automation_issue_key('f',
                                    "JianghuOL.CBE+0x0380e8-first-dialog-option"))
        {
            g_vmAutomation.dreamNpcDialogConfirmSent = 1;
            vm_automation_set_stage(
                VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_TARGET_SCENE,
                "npc-enter-instance-option-confirmed");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_DREAM_NPC_TARGET_SCENE:
        if (g_vmAutomation.dreamNpcInstanceEnterResponseSeen &&
            g_vmAutomation.dreamNpcTargetScenePacketSeen &&
            g_vmAutomation.renderFrames >=
                g_vmAutomation.dreamNpcTargetScenePacketFrame + 2u)
        {
            /* This is the client-owned 30/1 -> scene-load -> 25/5 boundary.
             * The number observer begins only after it, and still injects no
             * combat, map, or timer input. */
            g_vmAutomation.initialSceneScreen = vmAddedScreen;
            g_vmAutomation.dreamClockProbeArmed = 1;
            g_vmAutomation.dreamClockProbeArmFrame =
                g_vmAutomation.renderFrames;
            vm_automation_request_capture("dream-npc-target-scene-ready");
            vm_automation_set_stage(
                VM_AUTOMATION_STAGE_WAIT_DREAM_CLOCK_DRAW,
                "npc-30-1-followed-by-25-5-and-rendered-frame");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_EQUIPMENT_ENHANCE_RULES:
        if (g_vmAutomation.scenario ==
            VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE)
        {
            /* The fixture contains exactly one un-equipped sword.  These are
             * the three visible, ordered controls on its native route:
             * backpack's 装备 tab, its first list row, then the detail page's
             * 强化 button.  The scenario stops at 29/1; no material action
             * is scheduled after the preview page is rendered. */
            if (!g_vmAutomation.equipmentEnhanceDetailTapSent &&
                g_vmAutomation.renderFrames >= g_vmAutomation.stageFrame + 2u)
            {
                u8 category = 0;
                u32 screen = 0;

                vm_automation_request_capture(
                    "equipment-enhance-stage1-backpack-default-tab-rendered");
                if (vm_automation_read_backpack_category(&category, &screen))
                {
                    g_vmAutomation.equipmentEnhanceBackpackCategoryBefore =
                        category;
                    g_vmAutomation.equipmentEnhanceBackpackCategoryCurrent =
                        category;
                    vm_autotest_note(
                        "automation_equipment_enhance_category_before "
                        "screen=%08x category=%u frame=%u\n",
                        screen, category, g_vmAutomation.renderFrames);
                }
                else
                {
                    vm_autotest_note(
                        "automation_equipment_enhance_category_before "
                        "unavailable frame=%u\n",
                        g_vmAutomation.renderFrames);
                }
                if (vm_automation_issue_tap(58, 106,
                                            "backpack-equipment-category-tab"))
                {
                    g_vmAutomation.equipmentEnhanceDetailTapSent = 1;
                    g_vmAutomation.equipmentEnhanceDetailTapFrame =
                        g_vmAutomation.renderFrames;
                }
            }
            else if (g_vmAutomation.equipmentEnhanceDetailTapSent &&
                     !g_vmAutomation.equipmentEnhanceEquipmentCategoryRendered)
            {
                u8 category = 0;
                u32 screen = 0;

                if (vm_automation_read_backpack_category(&category, &screen))
                {
                    g_vmAutomation.equipmentEnhanceBackpackCategoryCurrent =
                        category;
                    if (category !=
                        g_vmAutomation.equipmentEnhanceBackpackCategoryBefore)
                    {
                        g_vmAutomation.equipmentEnhanceEquipmentCategoryRendered =
                            1;
                        g_vmAutomation
                            .equipmentEnhanceEquipmentCategoryRenderedFrame =
                            g_vmAutomation.renderFrames;
                        vm_autotest_note(
                            "automation_equipment_enhance_category_changed "
                            "screen=%08x from=%u to=%u frame=%u\n",
                            screen,
                            g_vmAutomation.equipmentEnhanceBackpackCategoryBefore,
                            category, g_vmAutomation.renderFrames);
                        vm_automation_request_capture(
                            "equipment-enhance-stage1-equipment-category-rendered");
                    }
                }
            }
            else if (g_vmAutomation.equipmentEnhanceDetailTapSent &&
                     g_vmAutomation.equipmentEnhanceEquipmentCategoryRendered &&
                     !g_vmAutomation.equipmentEnhanceConfirmTapSent &&
                     g_vmAutomation.equipmentEnhanceBackpackGridCommitted &&
                     g_vmAutomation.renderFrames >=
                         g_vmAutomation
                             .equipmentEnhanceEquipmentCategoryRenderedFrame + 2u)
            {
                vm_automation_request_capture(
                    "equipment-enhance-stage1-equipment-list-rendered");
                if (vm_automation_issue_tap(28, 128,
                                            "backpack-first-equipment-row"))
                {
                    g_vmAutomation.equipmentEnhanceConfirmTapSent = 1;
                    g_vmAutomation.equipmentEnhanceDetailTapFrame =
                        g_vmAutomation.renderFrames;
                }
            }
            else if (g_vmAutomation.equipmentEnhanceConfirmTapSent &&
                     !g_vmAutomation.equipmentEnhanceStage1ResponseSeen &&
                     g_vmAutomation.renderFrames >=
                         g_vmAutomation.equipmentEnhanceDetailTapFrame + 2u)
            {
                vm_automation_request_capture(
                    "equipment-enhance-stage1-detail-rendered");
                if (vm_automation_issue_tap(120, 372,
                                            "backpack-equipment-strengthen-button"))
                {
                    /* No further visible input is scheduled after this one.
                     * The high watermark prevents a repeated strengthen tap
                     * while the ordinary network callback owns the screen. */
                    g_vmAutomation.equipmentEnhanceDetailTapFrame =
                        g_vmAutomation.renderFrames + 0x40000000u;
                }
            }
            else if (g_vmAutomation.equipmentEnhanceStage1ResponseSeen &&
                     g_vmAutomation.renderFrames >=
                         g_vmAutomation.equipmentEnhanceStage1ResponseFrame + 2u)
            {
                vm_automation_request_capture(
                    "equipment-enhance-stage1-preview-rendered");
                vm_automation_finish(1,
                    "native-backpack-strengthen-29-1-preview-rendered");
            }
            break;
        }
        if (g_vmAutomation.scenario ==
            VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE)
        {
            /* The native backpack opens on 道具.  The isolated fixture owns
             * one armor, so select its visible 装备 category once and then
             * stop at the rendered list.  This is still discovery only: it
             * does not select an item or emit an enhancement request. */
            if (!g_vmAutomation.equipmentEnhanceDetailTapSent &&
                g_vmAutomation.renderFrames >= g_vmAutomation.stageFrame + 2u)
            {
                vm_automation_request_capture(
                    "equipment-enhance-backpack-default-tab-rendered");
                if (vm_automation_issue_tap(58, 106,
                                            "backpack-equipment-category-tab"))
                {
                    g_vmAutomation.equipmentEnhanceDetailTapSent = 1;
                    g_vmAutomation.equipmentEnhanceDetailTapFrame =
                        g_vmAutomation.renderFrames;
                }
            }
            else if (g_vmAutomation.equipmentEnhanceDetailTapSent &&
                     g_vmAutomation.renderFrames >=
                         g_vmAutomation.equipmentEnhanceDetailTapFrame + 2u)
            {
                vm_automation_request_capture(
                    "equipment-enhance-backpack-equipment-tab-rendered");
                vm_automation_finish(1, "native-backpack-equipment-tab-rendered");
            }
            break;
        }
        /* The first touch has already opened the native equipment list.  Its
         * render path does not calculate an individual item's enhanced
         * attack/armor yet; after that state has crossed two real render
         * boundaries, select the visible equipped-weapon slot once. */
        if (!g_vmAutomation.equipmentEnhanceDetailTapSent &&
            g_vmAutomation.renderFrames >= g_vmAutomation.stageFrame + 2u)
        {
            vm_automation_request_capture("equipment-enhance-list-rendered");
            if (vm_automation_issue_tap(166, 127,
                                        "equipment-visible-weapon-slot"))
            {
                g_vmAutomation.equipmentEnhanceDetailTapSent = 1;
                g_vmAutomation.equipmentEnhanceDetailTapFrame =
                    g_vmAutomation.renderFrames;
            }
        }
        /* Selection itself proves that the native item-detail path has run,
         * but it does not request the equipment action menu.  Press the one
         * visible Confirm control only after the detail has rendered.  This
         * is the documented client route into its 强化 action; it is not a
         * direct handler invocation or synthetic 1/29 request. */
        else if (g_vmAutomation.equipmentEnhanceDetailTapSent &&
                 !g_vmAutomation.equipmentEnhanceConfirmTapSent &&
                 g_vmAutomation.renderFrames >=
                     g_vmAutomation.equipmentEnhanceDetailTapFrame + 2u)
        {
            vm_automation_request_capture("equipment-enhance-detail-rendered");
            if (vm_automation_issue_tap(34, 382,
                                        "equipment-detail-confirm-action-menu"))
            {
                g_vmAutomation.equipmentEnhanceConfirmTapSent = 1;
            }
        }
        else if (g_vmEquipmentEnhanceRulesCaptured)
        {
            vm_automation_request_capture("equipment-enhance-rules-captured");
            vm_automation_finish(1,
                                 "client-calcequipstatbonus-table-captured");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_SHOP_OPEN:
        /* The generic loading stack is also a non-scene screen.  Bind the
         * shop owner only after the real 14/14 + 14/4 callback has arrived;
         * before that point `vmAddedScreen` is still the transition screen. */
        if (g_vmAutomation.shopStatusSeen && g_vmAutomation.shopMoneySeen &&
            g_vmAutomation.shopScreen == 0 && vmAddedScreen != 0 &&
            vmAddedScreen != g_vmAutomation.initialSceneScreen)
        {
            g_vmAutomation.shopScreen = vmAddedScreen;
            vm_autotest_note("automation_scene_owner phase=shop screen=%08x this=%08x frame=%u\n",
                             vmAddedScreen, g_currentScreenThis,
                             g_vmAutomation.renderFrames);
        }
        if (g_vmAutomation.shopStatusSeen && g_vmAutomation.shopMoneySeen &&
            g_vmAutomation.shopScreen != 0 &&
            vmAddedScreen == g_vmAutomation.shopScreen &&
            g_vmAutomation.renderFrames > g_vmAutomation.shopDataPacketFrame &&
            vm_automation_issue_key('e', "mmShop-data-callback-rendered"))
        {
            vm_autotest_note("automation_shop_ready screen=%08x this=%08x response_seq=%u response_frame=%u render_frame=%u\n",
                             vmAddedScreen, g_currentScreenThis,
                             g_vmAutomation.shopDataPacketSequence,
                             g_vmAutomation.shopDataPacketFrame,
                             g_vmAutomation.renderFrames);
            vm_automation_request_capture("shop-data-ready");
            vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN,
                                    "shop-back-sent");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN:
        /* A shop return now carries 30/2+posinfo.  CBE consequently performs
         * a true scene re-enter, so wait for its ScreenInit plus its later
         * scene-runtime 25/5 follow-up.  This observes the natural lifecycle
         * rather than using a fixed delay or treating packet arrival as UI
         * readiness. */
        if (g_vmAutomation.shopReturnSeen &&
            g_vmAutomation.shopReturnSceneReinitSeen &&
            g_vmAutomation.shopReturnFollowupSeen &&
            g_vmAutomation.initialSceneScreen != 0 &&
            g_vmAutomation.initialSceneScreen != g_vmAutomation.shopScreen &&
            vmAddedScreen == g_vmAutomation.initialSceneScreen &&
            g_vmAutomation.renderFrames >=
                g_vmAutomation.shopReturnSceneReinitFrame + 2u &&
            g_vmAutomation.renderFrames >=
                g_vmAutomation.shopReturnFollowupFrame + 2u)
        {
            vm_autotest_note("automation_scene_owner phase=shop-return-settled "
                             "screen=%08x this=%08x return_seq=%u return_frame=%u "
                             "reinit_frame=%u followup_seq=%u followup_frame=%u render_frame=%u\n",
                             vmAddedScreen, g_currentScreenThis,
                             g_vmAutomation.shopReturnPacketSequence,
                             g_vmAutomation.shopReturnPacketFrame,
                             g_vmAutomation.shopReturnSceneReinitFrame,
                             g_vmAutomation.shopReturnFollowupSequence,
                             g_vmAutomation.shopReturnFollowupFrame,
                             g_vmAutomation.renderFrames);
            vm_automation_request_capture("shop-return-pre-hangup");
            vm_automation_set_stage(
                VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN_PRE_HANGUP_CAPTURE,
                "shop-return-pre-hangup-capture-requested");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_SHOP_RETURN_PRE_HANGUP_CAPTURE:
        if (g_vmAutomation.shopReturnPreHangupCaptured &&
            vm_automation_issue_tap(50, 350, "scene-hangup-control"))
        {
            vm_automation_set_stage(VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE,
                                    "hangup-control-tapped-after-pre-capture");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE:
        if (g_vmAutomation.hangupBattleResponseSeen &&
            g_vmAutomation.battleStartHandlerSeen)
        {
            /* 0x66CC proves the battle packet grammar only.  The regression
             * remained on 获取数据 after that parser returned, so wait for
             * BattleScene_MainLoop's real character-list creation boundary. */
            vm_automation_set_stage(
                VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE_SCREEN,
                "hangup-4-5-reached-mmBattle-0x66CC");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_BATTLE_SCREEN:
        /* BattleScene_CreateCharList is not a required first-frame path:
         * the direct control reaches BattleScene_DrawMain with populated
         * slots without ever visiting it.  The real terminal assertion is
         * therefore the native main-draw entry after the start handoff, which
         * is absent on the shop-return failure because MainLoop returns at
         * the game loading gate before sub_71BA. */
        if (g_hangupBattleRenderTrace.drawMainSeen &&
            g_hangupBattleRenderTrace.battleCompletionPresentSeen)
        {
            if (g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_HANGUP_AUTO_CANCEL)
            {
                if (g_vmAutomation.hangupAutoEnableResponseSeen)
                {
                    vm_automation_request_capture("hangup-auto-visible");
                    vm_automation_set_stage(
                        VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_VISIBLE,
                        "native-auto-controls-rendered");
                }
            }
            else if (g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL ||
                     g_vmAutomation.scenario ==
                         VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT)
            {
                if (g_vmAutomation.hangupAutoEnableResponseSeen)
                {
                    vm_automation_request_capture(
                        g_vmAutomation.scenario ==
                                VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL
                            ? "hangup-auto-terminal-start"
                            : "hangup-native-auto-exit-start");
                    vm_automation_set_stage(
                        g_vmAutomation.scenario ==
                                VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL
                            ? VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_TERMINAL
                            : VM_AUTOMATION_STAGE_WAIT_HANGUP_NATIVE_AUTO_EXIT,
                        "three-enemy-auto-round-rendered");
                }
            }
            else
            {
                vm_automation_request_capture("hangup-battle-started");
                vm_automation_finish(1,
                                     "hangup-battle-main-draw-after-4-5");
            }
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_VISIBLE:
        /* The red X at VM `(217,75)` is the right-side target visible in the
         * exported 240x400 LCD frame.  Desktop window coordinates include the
         * title/frame offset and therefore are not VM touch coordinates.  The
         * target is the touch equivalent of BattleScene_HandleInput(0x6258)'s
         * cancel branch, emitted once only after the frame above proves that
         * the native control owns the touch. */
        if (g_vmAutomation.hangupAutoVisibleCaptured &&
            vm_automation_issue_tap(217, 75, "battle-auto-visible-red-x"))
        {
            vm_automation_set_stage(
                VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_CANCEL_RESPONSE,
                "battle-auto-cancel-tapped");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_CANCEL_RESPONSE:
        if (g_vmAutomation.hangupAutoDisableResponseSeen)
        {
            vm_automation_request_capture("hangup-auto-cancelled");
            vm_automation_finish(1,
                                 "native-4-11-type-0-response-received");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_AUTO_TERMINAL:
        /* The regression boundary is the visible native reward panel.  It
         * must not tap through that panel: its confirmation is player input,
         * which subsequently emits the client-owned 25/5 request. */
        if (g_vmAutomation.hangupSettlementResponseSeen)
        {
            vm_automation_request_capture("hangup-auto-reward-panel");
            vm_automation_finish(1,
                                 "native-4-7-reward-panel-received");
        }
        break;
    case VM_AUTOMATION_STAGE_WAIT_HANGUP_NATIVE_AUTO_EXIT:
        /* This scenario adds no reward-panel input.  The automatic close is
         * acceptable only when the exact server object reaches 0x8996, the
         * distinct DrawBattleMain sender reaches 0x5E92, and the resulting
         * client 25/5 leads to a fresh scene-poll battle parser/action. */
        if (!g_vmAutomation.hangupNativeAutoExitResponseSeen)
        {
            vm_automation_request_capture("hangup-native-auto-exit-missing-25-2");
            vm_automation_finish(0, "hangup-start-response-missing-25-2-result-1-type-1");
            break;
        }
        if (g_vmAutomation.hangupNativeManualExitPcSeen)
        {
            vm_automation_request_capture("hangup-native-auto-exit-manual-path");
            vm_automation_finish(0, "native-auto-exit-visited-manual-0x60c8");
            break;
        }
        if (g_vmAutomation.hangupNativeAutoExitParserSeen &&
            g_vmAutomation.hangupSettlementResponseSeen &&
            g_vmAutomation.hangupNativeAutoExitPcSeen &&
            g_vmAutomation.hangupNativeExitUplinkSeen &&
            g_vmAutomation.hangupBattleResponseCount >= 2u &&
            g_vmAutomation.battleStartHandlerSeen &&
            g_vmAutomation.battleActionResponseCount >
                g_vmAutomation.hangupSettlementActionResponseCount &&
            g_vmAutomation.inputCount ==
                g_vmAutomation.hangupSettlementInputCount)
        {
            vm_autotest_note("automation_hangup_native_auto_exit_complete "
                             "start_seq=%u parser_frame=%u exit_frame=%u "
                             "uplink_frame=%u inputs=%u next_starts=%u actions=%u\n",
                             g_vmAutomation.hangupNativeAutoExitResponseSequence,
                             g_vmAutomation.hangupNativeAutoExitParserFrame,
                             g_vmAutomation.hangupNativeAutoExitPcFrame,
                             g_vmAutomation.hangupNativeExitUplinkFrame,
                             g_vmAutomation.inputCount,
                             g_vmAutomation.hangupBattleResponseCount,
                             g_vmAutomation.battleActionResponseCount);
            vm_automation_request_capture("hangup-native-auto-exit-continued");
            vm_automation_finish(
                1, "native-25-2-0x8996-0x5e92-25-5-scene-poll-next-4-5-4-6");
        }
        break;
    default:
        break;
    }
}

static void vm_automation_render_complete(void)
{
    if (!g_vmAutomation.active)
        return;
    ++g_vmAutomation.renderFrames;
    vm_automation_capture_internal_lcd();
    if (g_vmAutomation.finished && !g_vmAutomation.capturePending &&
        !g_vmAutomation.exitRequested)
    {
        g_vmAutomation.exitRequested = 1;
        vm_request_host_quit("automation-scenario-complete");
    }
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

static int vm_mock_service_copy_host_slice(const char *text, size_t textLen,
                                           char *host, size_t hostCap)
{
    if (text == NULL || textLen == 0 || host == NULL || hostCap == 0 ||
        textLen >= hostCap)
    {
        return 0;
    }
    memcpy(host, text, textLen);
    host[textLen] = 0;
    return 1;
}

/*
 * Read-only ordering trace for same-screen AddScreen re-entry.  The actor
 * table pointer distinguishes a completed scene teardown from a callback that
 * merely selected DESTROY but has not executed it yet.
 */
static void vm_trace_screen_lifecycle_order(const char *phase, u32 screen,
                                            u32 screenThis, u32 callback,
                                            u32 replacesActive)
{
    static u32 traceCount = 0;
    const char *enabled = getenv("CBE_TRACE_SCREEN_LIFECYCLE_ORDER");
    u32 actorArray = 0;
    u32 logicEntry = 0;
    u32 renderEntry = 0;
    u8 actorCount = 0;
    FILE *trace = NULL;

    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0 ||
        traceCount >= 256u)
    {
        return;
    }
    if (MTK != NULL && Global_R9 != 0)
    {
        (void)uc_mem_read(MTK, Global_R9 + 0x5CB4u, &actorArray,
                          sizeof(actorArray));
        (void)uc_mem_read(MTK, Global_R9 + 0x5C73u, &actorCount,
                          sizeof(actorCount));
    }
    if (screen != 0)
    {
        /* `screen` is the already-selected function table.  Record its
         * entries only for this bounded lifecycle trace; this does not feed
         * dispatch or alter any guest state. */
        logicEntry = vm_get_var(screen + 8u);
        renderEntry = vm_get_var(screen + 12u);
    }
    trace = fopen("logs/screen-lifecycle-order.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "screen_lifecycle_order seq=%u phase=%s caller=%08x screen=%08x "
            "screen_this=%08x callback=%08x logic=%08x render=%08x "
            "active=%08x active_this=%08x "
            "change=%u exit_mode=%u replaces_active=%u stack_depth=%u "
            "actor_array=%08x actor_count=%u net_depth=%d net_slot=%d\n",
            traceCount, phase ? phase : "-", lastAddress, screen, screenThis,
            callback, logicEntry, renderEntry, vmAddedScreen, g_currentScreenThis, screenStructChange,
            g_screenExitMode, replacesActive, g_screenStackCount, actorArray,
            (unsigned)actorCount, g_netTaskDispatchDepth,
            g_netTaskDispatchSlot);
    fclose(trace);
}

static void vm_trace_screen_manager_decision(
    u32 idx, u32 guestLr, u32 requestedScreen, u32 oldActiveScreen,
    u32 param, u32 flags, bool sameActiveRequest, bool acceptChange)
{
    static u32 traceCount = 0;
    const char *enabled = getenv("CBE_TRACE_SCREEN_LIFECYCLE_ORDER");
    FILE *trace = NULL;

    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0 ||
        traceCount >= 256u)
    {
        return;
    }
    trace = fopen("logs/screen-lifecycle-order.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "screen_manager_decision seq=%u idx=%u caller=%08x guest_lr=%08x "
            "requested=%08x active=%08x param=%08x flags=%u same_active=%u "
            "accept=%u result_exit_mode=%u "
            "net_depth=%d net_slot=%d\n",
            traceCount, idx, lastAddress, guestLr, requestedScreen,
            oldActiveScreen, param, flags, sameActiveRequest ? 1u : 0u,
            acceptChange ? 1u : 0u,
            acceptChange && requestedScreen != 0 ? VM_SCREEN_EXIT_DESTROY
                                                  : g_screenExitMode,
            g_netTaskDispatchDepth, g_netTaskDispatchSlot);
    fclose(trace);
}

/* Accept the endpoint forms used by both environment configuration and the
 * compiled-in g_mockServiceHost default:
 *
 *   host:port       conventional IPv4/DNS endpoint
 *   [ipv6]:port     explicit IPv6 endpoint
 *   host / ipv6     host only; preserve the caller's existing port
 *   port            legacy localhost shorthand
 */
static int vm_mock_service_parse_host_port(const char *text, char *host,
                                           size_t hostCap, u16 *port)
{
    const char *firstColon = NULL;
    const char *colon = NULL;
    const char *closeBracket = NULL;
    u32 parsedPort = 0;
    size_t hostLen = 0;

    if (text == NULL || *text == 0 || host == NULL || hostCap == 0 || port == NULL)
        return 0;

    if (text[0] == '[')
    {
        closeBracket = strchr(text + 1, ']');
        if (closeBracket == NULL || closeBracket == text + 1)
            return 0;
        hostLen = (size_t)(closeBracket - (text + 1));
        if (closeBracket[1] == 0)
            return vm_mock_service_copy_host_slice(text + 1, hostLen, host, hostCap);
        if (closeBracket[1] != ':' ||
            !vm_autotest_parse_u32(closeBracket + 2, &parsedPort) ||
            parsedPort == 0 || parsedPort > 65535u ||
            !vm_mock_service_copy_host_slice(text + 1, hostLen, host, hostCap))
        {
            return 0;
        }
        *port = (u16)parsedPort;
        return 1;
    }

    firstColon = strchr(text, ':');
    colon = strrchr(text, ':');
    if (colon != NULL && colon == firstColon)
    {
        if (!vm_autotest_parse_u32(colon + 1, &parsedPort) ||
            parsedPort == 0 || parsedPort > 65535u)
        {
            return 0;
        }
        hostLen = (size_t)(colon - text);
        if (hostLen == 0)
            snprintf(host, hostCap, "127.0.0.1");
        else if (!vm_mock_service_copy_host_slice(text, hostLen, host, hostCap))
            return 0;
        *port = (u16)parsedPort;
        return 1;
    }

    if (colon != NULL)
    {
        /* Multiple colons are an unbracketed IPv6 host without a port. */
        return vm_mock_service_copy_host_slice(text, strlen(text), host, hostCap);
    }

    if (vm_autotest_parse_u32(text, &parsedPort))
    {
        if (parsedPort == 0 || parsedPort > 65535u)
            return 0;
        snprintf(host, hostCap, "127.0.0.1");
        *port = (u16)parsedPort;
        return 1;
    }

    return vm_mock_service_copy_host_slice(text, strlen(text), host, hostCap);
}

static int vm_mock_service_apply_configured_host_port(void)
{
    char parsedHost[sizeof(g_mockServiceHost)];
    u16 parsedPort = g_mockServicePort;

    if (!vm_mock_service_parse_host_port(g_mockServiceHost, parsedHost,
                                         sizeof(parsedHost), &parsedPort))
    {
        return 0;
    }
    snprintf(g_mockServiceHost, sizeof(g_mockServiceHost), "%s", parsedHost);
    g_mockServicePort = parsedPort;
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

static void vm_automation_init_config(int argc, char *args[])
{
    const char *scenario = getenv("CBE_AUTOMATION_SCENARIO");
    const char *artifactDir = getenv("CBE_AUTOMATION_ARTIFACTS");
    const char *titleDriver = getenv("CBE_AUTOMATION_TITLE_DRIVER");

    for (int i = 1; i < argc; ++i)
    {
        if (strncmp(args[i], "--automation-scenario=", 22) == 0)
            scenario = args[i] + 22;
        else if (strncmp(args[i], "--automation-artifacts=", 23) == 0)
            artifactDir = args[i] + 23;
        else if (strncmp(args[i], "--automation-title-driver=", 26) == 0)
            titleDriver = args[i] + 26;
    }
    if (scenario == NULL)
        return;
    vm_automation_scenario parsedScenario = VM_AUTOMATION_SCENARIO_NONE;
    if (strcmp(scenario, "shop-return-hangup-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_SHOP_RETURN_HANGUP;
    else if (strcmp(scenario, "direct-hangup-control-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_DIRECT_HANGUP;
    else if (strcmp(scenario, "title-module-update-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE;
    else if (strcmp(scenario, "scene-teleport-stone-probe-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_SCENE_TELEPORT_STONE_PROBE;
    else if (strcmp(scenario, "dream-clock-probe-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_DREAM_CLOCK_PROBE;
    else if (strcmp(scenario, "dream-npc-entry-clock-probe-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE;
    else if (strcmp(scenario, "equipment-enhance-rules-probe-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_RULES_PROBE;
    else if (strcmp(scenario, "equipment-enhance-bag-probe-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE;
    else if (strcmp(scenario, "equipment-enhance-stage1-probe-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE;
    else if (strcmp(scenario, "hangup-auto-cancel-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_HANGUP_AUTO_CANCEL;
    else if (strcmp(scenario, "hangup-auto-terminal-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_HANGUP_AUTO_TERMINAL;
    else if (strcmp(scenario, "hangup-native-auto-exit-v1") == 0)
        parsedScenario = VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT;
    else
        return;
    if (artifactDir == NULL || artifactDir[0] == 0 ||
        strlen(artifactDir) >= sizeof(g_vmAutomation.artifactDir))
    {
        printf("[error][automation] scenario=%s requires an existing --automation-artifacts directory\n",
               scenario);
        return;
    }
    memset(&g_vmAutomation, 0, sizeof(g_vmAutomation));
    g_vmAutomation.scenario = parsedScenario;
    g_vmAutomation.timedTitleDriver =
        titleDriver != NULL && strcmp(titleDriver, "timed-title-v1") == 0;
    g_vmAutomation.stage = parsedScenario == VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE
        ? VM_AUTOMATION_STAGE_WAIT_TITLE_MODULE_UPDATE
        : (g_vmAutomation.timedTitleDriver
            ? VM_AUTOMATION_STAGE_WAIT_TIMED_TITLE_BOOTSTRAP
            : VM_AUTOMATION_STAGE_BOOT_CONFIRM);
    g_vmAutomation.active = 1;
    g_vmAutomation.maxSteps = parsedScenario == VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE
        ? 1u : (parsedScenario == VM_AUTOMATION_SCENARIO_HANGUP_NATIVE_AUTO_EXIT
            ? 14u : (parsedScenario == VM_AUTOMATION_SCENARIO_SCENE_TELEPORT_STONE_PROBE
                ? 4u : (parsedScenario == VM_AUTOMATION_SCENARIO_DREAM_CLOCK_PROBE
                    ? 6u : (parsedScenario ==
                             VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE
                        ? 9u : (g_vmAutomation.timedTitleDriver ? 13u : 10u)))));
    g_vmAutomation.totalTimeoutMs = parsedScenario == VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE
        ? 30000u : (parsedScenario == VM_AUTOMATION_SCENARIO_SCENE_TELEPORT_STONE_PROBE
            ? 75000u : (parsedScenario == VM_AUTOMATION_SCENARIO_DREAM_CLOCK_PROBE
                ? 150000u : (parsedScenario ==
                             VM_AUTOMATION_SCENARIO_DREAM_NPC_ENTRY_CLOCK_PROBE
                    ? 200000u : (g_vmAutomation.timedTitleDriver ? 180000u : 120000u))));
    g_vmAutomation.stepTimeoutMs = parsedScenario == VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE
        ? 30000u : 15000u;
    snprintf(g_vmAutomation.artifactDir, sizeof(g_vmAutomation.artifactDir),
             "%s", artifactDir);
    g_autotestEnabled = 1;
}

static void vm_autotest_init(int argc, char *args[])
{
    const char *envAuto = getenv("CBE_AUTOTEST");
    const char *envShotMs = getenv("CBE_AUTOTEST_SHOT_MS");
    const char *envMaxMs = getenv("CBE_AUTOTEST_MAX_MS");
    const char *envActions = getenv("CBE_AUTOTEST_ACTIONS");

    vm_automation_init_config(argc, args);
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
        char statePath[640];
#ifdef _WIN32
        if (!g_vmAutomation.active)
        {
            _mkdir("autotest");
            _mkdir("autotest\\screens");
        }
#else
        if (!g_vmAutomation.active)
        {
            mkdir("autotest", 0755);
            mkdir("autotest/screens", 0755);
        }
#endif
        if (g_vmAutomation.active)
        {
            snprintf(statePath, sizeof(statePath), "%s/automation.log",
                     g_vmAutomation.artifactDir);
            g_autotestStateFile = fopen(statePath, "wb");
            printf("[info][automation] scenario=%s artifacts=%s max_steps=%u total_timeout_ms=%u step_timeout_ms=%u input_release_ms=80\n",
                   vm_automation_scenario_name(), g_vmAutomation.artifactDir,
                   g_vmAutomation.maxSteps,
                   g_vmAutomation.totalTimeoutMs, g_vmAutomation.stepTimeoutMs);
            vm_autotest_note("automation_start scenario=%s max_steps=%u total_timeout_ms=%u step_timeout_ms=%u input_release_ms=80\n",
                             vm_automation_scenario_name(), g_vmAutomation.maxSteps,
                             g_vmAutomation.totalTimeoutMs,
                             g_vmAutomation.stepTimeoutMs);
            vm_automation_request_capture("boot");
        }
        else
        {
            g_autotestStateFile = fopen("autotest/state.txt", "w");
            printf("[info][autotest] enabled shot_ms=%u max_ms=%u actions=%u\n",
                   g_autotestShotIntervalMs, g_autotestMaxMs, g_autotestActionCount);
        }
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
    if (!vm_mock_service_apply_configured_host_port())
    {
        printf("[warn][mock-service] invalid compiled default host=%s\n",
               g_mockServiceHost);
    }
#ifdef CBE_CLIENT_ONLY
    const char *envEndpoint = getenv("CBE_SERVER_ENDPOINT");
    char parsedHost[64];
    u16 parsedPort = g_mockServicePort;
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
    u16 parsedPort = g_mockServicePort;

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

/*
 * CalcEquipStatBonus is the only confirmed consumer of the client-side
 * equipment-enhancement rule table.  The table is deliberately observed here
 * rather than reproduced in the service: until its sixteen entries are read
 * from the running Jianghu client, a server-side enhancement formula would be
 * a guess.  This is diagnostic-only and is gated by CBE_AUTOTEST; it performs
 * guest-memory reads only and never changes CBE state.  The hook observes
 * the primary-loop setup at 0x01028B5C, which both ordinary weapon/armor
 * callers and the optional secondary calculation share.
 */
static void vm_autotest_arm_equipment_enhance_rules_watch(void)
{
    u32 itemCtrl = 0;

    /* This runs after the normal ROM R9 restoration.  Arming on the first
     * observable item controller lets the memory hook see a startup-time
     * initialization, which the later CalcEquipStatBonus consumer cannot. */
    if (MTK == NULL || g_vmEquipmentEnhanceRulesWatchAddress != 0 ||
        Global_R9 == 0)
    {
        return;
    }
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &itemCtrl,
                    sizeof(itemCtrl)) != UC_ERR_OK || itemCtrl == 0)
    {
        return;
    }
    g_vmEquipmentEnhanceRulesWatchAddress = itemCtrl + 0x584;
    printf("[info][equipment] enhance_rule_table_watch_arm global_r9=%08x item_ctrl=%08x pointer_addr=%08x\n",
           Global_R9, itemCtrl, g_vmEquipmentEnhanceRulesWatchAddress);
    vm_autotest_note("equipment_enhance_rules_pointer_watch_arm global_r9=%08x item_ctrl=%08x address=%08x\\n",
                     Global_R9, itemCtrl,
                     g_vmEquipmentEnhanceRulesWatchAddress);
}

static void vm_autotest_note_equipment_enhance_rules_pc(u32 pc)
{
    static u32 reportedTable = 0;
    static u32 entryCount = 0;
    static u32 manualTraceCount = 0;
    u32 itemCtrl = 0;
    u32 ruleTable = 0;
    u32 baseStat = 0;
    u32 enhanceLevel = 0;
    u32 outputPtr = 0;
    u16 seededTotal = 0;
    u32 flatBonus = 0;
    int32_t percentBonus = 0;
    uc_err itemCtrlRead = UC_ERR_OK;
    uc_err ruleTableRead = UC_ERR_OK;

    /* 0x01028B5C is reached by the calculator's primary loop on every
     * invocation.  The former 0x01028BCE is inside its optional secondary
     * calculation (`a6 == 1`) and is therefore not reached by normal scene
     * weapon/armor reconstruction. */
    if (pc != 0x01028B5C)
        return;
    ++entryCount;
    uc_reg_read(MTK, UC_ARM_REG_R0, &outputPtr);
    uc_reg_read(MTK, UC_ARM_REG_R2, &baseStat);
    uc_reg_read(MTK, UC_ARM_REG_R3, &enhanceLevel);
    if (outputPtr != 0)
        (void)uc_mem_read(MTK, outputPtr, &seededTotal, sizeof(seededTotal));
    if (entryCount == 1)
    {
        vm_autotest_note("equipment_enhance_rules_entry pc=%08x global_r9=%08x\n",
                         pc, Global_R9);
    }
    if (Global_R9 == 0)
        return;
    itemCtrlRead = uc_mem_read(MTK, Global_R9 + 0x54AC,
                               &itemCtrl, sizeof(itemCtrl));
    if (itemCtrlRead == UC_ERR_OK && itemCtrl != 0)
    {
        ruleTableRead = uc_mem_read(MTK, itemCtrl + 0x584,
                                    &ruleTable, sizeof(ruleTable));
    }
    if (entryCount == 1)
    {
        vm_autotest_note("equipment_enhance_rules_pointer_probe ctrl_read=%d ctrl=%08x table_read=%d table=%08x\n",
                         (int)itemCtrlRead, itemCtrl, (int)ruleTableRead,
                         ruleTable);
    }
    /* Normal manual play needs a compact trace too.  Only enhanced items
     * are emitted and the count is capped, so ordinary scene reconstruction
     * cannot turn this into a high-frequency client log. */
    if (enhanceLevel != 0 && manualTraceCount < 32)
    {
        u32 steps = enhanceLevel > 16u ? 16u : enhanceLevel;

        if (ruleTableRead == UC_ERR_OK && ruleTable != 0)
        {
            for (u32 i = 0; i < steps; ++i)
            {
                u8 raw[4] = {0, 0, 0, 0};
                int16_t percent = 0;

                if (uc_mem_read(MTK, ruleTable + i * 4u, raw,
                                sizeof(raw)) != UC_ERR_OK)
                    break;
                percent = (int16_t)((u16)raw[2] | ((u16)raw[3] << 8));
                flatBonus += raw[0];
                percentBonus += ((int32_t)percent * (int32_t)baseStat) / 100;
            }
        }
        printf("[info][equipment] enhance_calc client=CalcEquipStatBonus call=%u pc=%08x output=%08x seeded=%u base=%u level=%u table=%08x rule_state=%s flat_add=%u percent_add=%d total=%d\n",
               manualTraceCount + 1u, pc, outputPtr, seededTotal, baseStat,
               enhanceLevel, ruleTable,
               ruleTableRead == UC_ERR_OK && ruleTable != 0 ? "ready" : "missing",
               flatBonus, percentBonus,
               (int)seededTotal + (int)flatBonus + percentBonus);
        ++manualTraceCount;
    }
    if (itemCtrlRead != UC_ERR_OK || itemCtrl == 0 ||
        ruleTableRead != UC_ERR_OK || ruleTable == 0 ||
        ruleTable == reportedTable)
    {
        return;
    }
    if (uc_mem_read(MTK, ruleTable, &baseStat, sizeof(baseStat)) != UC_ERR_OK)
        return;

    /* At the calculator entry, R2/R3 are the equipment base stat and current
     * enhancement level.  Read them only to bind the captured table to the
     * proven call contract. */
    uc_reg_read(MTK, UC_ARM_REG_R2, &baseStat);
    uc_reg_read(MTK, UC_ARM_REG_R3, &enhanceLevel);
    reportedTable = ruleTable;
    printf("[info][equipment] enhance_rule_table_ready client=CalcEquipStatBonus pc=%08x item_ctrl=%08x table=%08x base=%u level=%u entries=16\n",
           pc, itemCtrl, ruleTable, baseStat, enhanceLevel);
    vm_autotest_note("equipment_enhance_rules pc=%08x item_ctrl=%08x table=%08x base=%u level=%u entries=16\\n",
                     pc, itemCtrl, ruleTable, baseStat, enhanceLevel);

    for (u32 i = 0; i < 16; ++i)
    {
        u8 raw[4];
        int16_t percent = 0;

        if (uc_mem_read(MTK, ruleTable + i * 4, raw, sizeof(raw)) != UC_ERR_OK)
        {
            vm_autotest_note("equipment_enhance_rule index=%u read=failed\\n", i);
            return;
        }
        percent = (int16_t)((u16)raw[2] | ((u16)raw[3] << 8));
        vm_autotest_note("equipment_enhance_rule index=%u flat=%u percent=%d raw=%02x%02x%02x%02x\\n",
                         i, raw[0], (int)percent, raw[0], raw[1], raw[2], raw[3]);
    }
    g_vmEquipmentEnhanceRulesCaptured = 1;
}

/* Read-only update-state capture for the isolated automation investigation.
 * These offsets are the client module-update context established by
 * send_update_chunk_request (JianghuOL.CBE:0x01036D80).  This code never
 * changes guest memory, registers, packets, or guest control flow. */
static void vm_autotest_trace_update_state(const char *phase, u32 sequence,
                                           const u8 *packet, u32 packetLen)
{
    const u32 updateBaseOffset = 38220u;
    u16 versions[4] = {0, 0, 0, 0};
    u8 slot = 0;
    u32 start = 0;
    u32 tempFile = 0;
    u32 checksum = 0;
    u8 requestState = 0;
    u8 requestPending = 0;
    u32 buffer = 0;
    u32 bufferOffset = 0;
    u8 bufferBlocks = 0;
    u32 totalSize = 0;
    u32 capacity = 0;
    u8 loaderState = 0;
    u16 loaderVersion = 0;
    u32 loaderBuffer = 0;
    u8 kind = 0;
    u8 subtype = 0;
    u32 base;

    if (!g_autotestEnabled || Global_R9 == 0)
        return;
    if (packet != NULL && packetLen >= 8u &&
        packet[0] == 'W' && packet[1] == 'T')
    {
        kind = packet[6];
        subtype = packet[7];
    }
    base = Global_R9 + updateBaseOffset;
    if (uc_mem_read(MTK, base, versions, sizeof(versions)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 8u, &slot, sizeof(slot)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 12u, &start, sizeof(start)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 20u, &tempFile, sizeof(tempFile)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 56u, &checksum, sizeof(checksum)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 72u, &requestState, sizeof(requestState)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 73u, &requestPending, sizeof(requestPending)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 80u, &buffer, sizeof(buffer)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 84u, &bufferOffset, sizeof(bufferOffset)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 88u, &bufferBlocks, sizeof(bufferBlocks)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 96u, &totalSize, sizeof(totalSize)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 100u, &capacity, sizeof(capacity)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 156u, &loaderState, sizeof(loaderState)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 158u, &loaderVersion, sizeof(loaderVersion)) != UC_ERR_OK ||
        uc_mem_read(MTK, base + 160u, &loaderBuffer, sizeof(loaderBuffer)) != UC_ERR_OK)
    {
        vm_autotest_note("update_trace phase=%s seq=%u read=failed r9=%08x\\n",
                         phase != NULL ? phase : "unknown", sequence, Global_R9);
        return;
    }
    vm_autotest_note("update_trace phase=%s seq=%u wire=%u kind=%u/%u slot=%u versions=%u/%u/%u/%u start=%u checksum=%u request=%u/%u temp=%08x buffer=%08x offset=%u blocks=%u total=%u cap=%u loader=%u/%u loaderbuf=%08x\\n",
                     phase != NULL ? phase : "unknown", sequence, packetLen,
                     kind, subtype, slot, versions[0], versions[1], versions[2],
                     versions[3], start, checksum, requestState, requestPending,
                     tempFile, buffer, bufferOffset, bufferBlocks, totalSize,
                     capacity, loaderState, loaderVersion, loaderBuffer);
    /* The test only observes the same updater context the client owns.  A
     * pass requires an actual 18/6 response followed by the installed native
     * terminal state; no fixed delay or title-screen side effect is treated
     * as evidence that the transaction succeeded. */
    if (g_vmAutomation.active &&
        g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE &&
        slot == 1u && totalSize != 0)
    {
        if (packet != NULL && kind == 18u && subtype == 6u)
            g_vmAutomation.titleModuleUpdateChunkSeen = 1;
        if (g_vmAutomation.titleModuleUpdateChunkSeen && phase != NULL &&
            strcmp(phase, "callback-end") == 0 &&
            requestState == 0 && requestPending == 0 &&
            tempFile == 0xffffffffu && buffer == 0 && start == 0 &&
            checksum != 0)
        {
            g_vmAutomation.titleModuleUpdateCompleted = 1;
            g_vmAutomation.titleModuleUpdateTotalSize = totalSize;
            g_vmAutomation.titleModuleUpdateChecksum = checksum;
        }
    }
}

static void vm_autotest_trace_update_guest_callback(const char *phase,
                                                     u32 responsePtr,
                                                     u32 responseLen)
{
    u8 header[11];
    u32 declaredLen;

    if (!g_autotestEnabled || responsePtr == 0 || responseLen < sizeof(header))
        return;
    if (uc_mem_read(MTK, responsePtr, header, sizeof(header)) != UC_ERR_OK ||
        header[0] != 'W' || header[1] != 'T' || header[4] == 0 ||
        header[6] != 18 || header[7] != 6)
        return;
    declaredLen = ((u32)header[2] << 8) | header[3];
    if (declaredLen != responseLen)
    {
        vm_autotest_note("update_trace phase=%s callback_len_mismatch ptr=%08x event_len=%u wire_len=%u\\n",
                         phase != NULL ? phase : "unknown", responsePtr,
                         responseLen, declaredLen);
    }
    vm_autotest_trace_update_state(phase, 0, header, responseLen);
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
    static u32 seenModuleUpdateReject = 0;
    static u32 seenModuleUpdateFlush = 0;
    static u32 seenModuleUpdateRequest = 0;
    u32 startupState = 0;
    u32 netTask = 0;
    u16 waitTicks = 0;
    u32 startupObj = 0;

    vm_automation_note_startup_pc(pc);

    if (!g_autotestEnabled || Global_R9 == 0)
        return;
    if (pc != 0x0103A77C && pc != 0x0103A7AC && pc != 0x0103A7BA &&
        pc != 0x0103A7C2 && pc != 0x0103B2D6 && pc != 0x0103B95A &&
        pc != 0x01036C66 && pc != 0x01036F48 && pc != 0x010373DA &&
        pc != 0x010375A0 &&
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
    else if (pc == 0x01036C66 && seenModuleUpdateRequest < 16u)
    {
        ++seenModuleUpdateRequest;
        vm_autotest_trace_update_state("chunk-request-builder",
                                       seenModuleUpdateRequest, NULL, 0);
    }
    else if (pc == 0x01036F48 && seenModuleUpdateFlush < 8u)
    {
        ++seenModuleUpdateFlush;
        vm_autotest_trace_update_state("chunk-tempfile-flush",
                                       seenModuleUpdateFlush, NULL, 0);
    }
    else if (pc == 0x010373DA && seenModuleUpdateReject < 8u)
    {
        u32 calculated = 0;
        u32 advertised = 0;

        ++seenModuleUpdateReject;
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &calculated);
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &advertised);
        vm_autotest_note("update_trace reject count=%u calculated_or_loader=%u advertised_crc=%u\n",
                         seenModuleUpdateReject, calculated, advertised);
        vm_autotest_trace_update_state("chunk-reject", seenModuleUpdateReject,
                                       NULL, 0);
    }
    else if (pc == 0x010375A0 && seenModuleUpdateReject < 8u)
    {
        u32 eventType = 0;

        (void)uc_reg_read(MTK, UC_ARM_REG_R5, &eventType);
        ++seenModuleUpdateReject;
        vm_autotest_note("update_trace lifecycle_reject count=%u event=%u\n",
                         seenModuleUpdateReject, eventType);
        if (g_vmAutomation.active &&
            g_vmAutomation.scenario == VM_AUTOMATION_SCENARIO_TITLE_MODULE_UPDATE)
            g_vmAutomation.titleModuleUpdateLifecycleRejectSeen = 1;
        vm_autotest_trace_update_state("lifecycle-reject", seenModuleUpdateReject,
                                       NULL, 0);
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
    static u32 seenBackpackRenderFilter = 0;
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
    static u32 seenItemCountStream = 0;
    u32 r9 = 0;

    if (!g_autotestEnabled)
        return;
    if (pc != 0x0102657A && pc != 0x010265E4 &&
        pc != 0x01012F7E && pc != 0x01012F8E && pc != 0x01012FA4 &&
        pc != 0x0101191A && pc != 0x010119DE &&
        pc != 0x01033544 && pc != 0x0103374E &&
        pc != 0x01033626 && pc != 0x010336AE &&
        pc != 0x010336B6 && pc != 0x010336C4 &&
        pc != 0x01033728 &&
        pc != 0x01039952 && pc != 0x01039AF8 &&
        pc != 0x0101918E && pc != 0x010191A2 && pc != 0x01028178 &&
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

    if (pc == 0x01028178 &&
        g_vmAutomation.scenario ==
            VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_BAG_PROBE &&
        seenBackpackRenderFilter < 8)
    {
        u8 globalCategory = 0;
        u8 screenCategory = 0;
        u8 itemCategory = 0;
        u32 screen = 0;
        u32 item = 0;

        ++seenBackpackRenderFilter;
        if (r9 != 0)
        {
            (void)uc_mem_read(MTK, r9 + 25420, &globalCategory,
                              sizeof(globalCategory));
            (void)uc_mem_read(MTK, r9 + 23816, &screen, sizeof(screen));
            (void)uc_mem_read(MTK, r9 + 25480, &item, sizeof(item));
        }
        if (screen != 0)
            (void)uc_mem_read(MTK, screen + 283, &screenCategory,
                              sizeof(screenCategory));
        if (item != 0)
            (void)uc_mem_read(MTK, item + 282, &itemCategory,
                              sizeof(itemCategory));
        vm_autotest_note("backpack_render_filter pc=%08x global_category=%u screen=%08x screen_category=%u item=%08x item_category=%u seen=%u\n",
                         pc, globalCategory, screen, screenCategory, item,
                         itemCategory, seenBackpackRenderFilter);
    }

    /* 7/11 quantity-stream evidence.  The hook points are deliberately
     * immediately before/after the CBE reader calls, so this records the
     * parser's interpretation without changing the reader, item manager, or
     * response bytes.  This is needed to distinguish a malformed stream from
     * a valid stream whose sequence resolves to a different client row. */
    if ((pc == 0x01033626 || pc == 0x010336AE || pc == 0x010336B6 ||
         pc == 0x010336C4 || pc == 0x01033728) &&
        seenItemCountStream < 96)
    {
        u32 sp = 0;
        u32 r0 = 0;
        u32 r1 = 0;
        u32 r2 = 0;
        u32 r3 = 0;
        u32 rowCount = 0;
        u32 rowIndex = 0;
        u32 streamSeq = 0;
        u32 streamCount = 0;
        u32 item = 0;
        u32 itemType = 0;
        u32 itemCategory = 0;
        u16 stack242 = 0;
        u16 stack272 = 0;

        ++seenItemCountStream;
        uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
        /* var_450 is at SP, var_44C at SP+4, and the loop index is R6. */
        uc_mem_read(MTK, sp, &rowCount, sizeof(rowCount));
        uc_mem_read(MTK, sp + 4, &streamSeq, sizeof(streamSeq));
        uc_reg_read(MTK, UC_ARM_REG_R6, &rowIndex);
        if (pc == 0x010336AE)
            streamSeq = r0;
        if (pc == 0x010336B6)
            streamCount = r0;
        if (pc == 0x010336C4)
        {
            item = r0;
            if (item != 0)
            {
                uc_mem_read(MTK, item, &itemType, sizeof(itemType));
                uc_mem_read(MTK, item + 0x11A, &itemCategory,
                            sizeof(itemCategory));
                uc_mem_read(MTK, item + 242, &stack242, sizeof(stack242));
                uc_mem_read(MTK, item + 272, &stack272, sizeof(stack272));
            }
        }
        if (pc == 0x01033728)
        {
            item = r0;
            if (item != 0)
            {
                uc_mem_read(MTK, item, &itemType, sizeof(itemType));
                uc_mem_read(MTK, item + 0x11A, &itemCategory,
                            sizeof(itemCategory));
                uc_mem_read(MTK, item + 242, &stack242, sizeof(stack242));
                uc_mem_read(MTK, item + 272, &stack272, sizeof(stack272));
            }
        }
        vm_autotest_note("backpack_7_11_parser pc=%08x sp=%08x r0=%08x r1=%08x r2=%08x r3=%08x row_count=%u row_index=%u seq=%u count=%u item=%08x item_type=%08x category=%u stack242=%u stack272=%u seen=%u\n",
                         pc, sp, r0, r1, r2, r3, rowCount, rowIndex,
                         streamSeq, streamCount, item, itemType,
                         itemCategory, stack242, stack272,
                         seenItemCountStream);
    }

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

    /* Keep a complete, bounded snapshot of the client main-item manager.  A
     * single item0 observation cannot reveal whether the server sequence is
     * resolving to the wrong physical row when two stacks share an item id. */
    if ((pc == 0x01033544 || pc == 0x0103374E || pc == 0x01039952 ||
         pc == 0x01039AF8) && seenItemCountStream < 96)
    {
        u32 manager = 0;
        u32 list = 0;
        u16 count = 0;
        u16 cap = 0;
        u32 scanCount = 0;
        u32 scanCap = 0;

        if (pc == 0x01033544)
            uc_reg_read(MTK, UC_ARM_REG_R0, &manager);
        else if (Global_R9 != 0)
            manager = Global_R9 + 24640;
        if (manager != 0)
        {
            uc_mem_read(MTK, manager + 32, &list, sizeof(list));
            uc_mem_read(MTK, manager + 36, &count, sizeof(count));
            uc_mem_read(MTK, manager + 40, &cap, sizeof(cap));
        }
        scanCount = count;
        scanCap = cap;
        if (scanCount > 80)
            scanCount = 80;
        vm_autotest_note("backpack_main_rows phase=%s pc=%08x manager=%08x list=%08x count=%u cap=%u\n",
                         pc == 0x01033544 ? "item-op" :
                         (pc == 0x01039952 ? "grid-parser" : "grid-done"),
                         pc, manager, list, count, cap);
        for (u32 i = 0; i < scanCount; ++i)
        {
            u32 item = 0;
            u32 itemId = 0;
            u16 itemCount = 0;
            u16 itemSeq = 0;
            u8 itemCategory = 0;
            /* The manager's +32 value is the base of a contiguous array of
             * 324-byte item records, not an array of pointers.  Treating it
             * as list+i*4 reads the first four bytes of each record as an
             * address and makes the evidence unusable (and can itself hit
             * unmapped memory near the parser). */
            item = list + i * 324u;
            if (list == 0 ||
                uc_mem_read(MTK, item, &itemId, sizeof(itemId)) != UC_ERR_OK ||
                itemId == 0)
                continue;
            (void)uc_mem_read(MTK, item + 242, &itemCount, sizeof(itemCount));
            (void)uc_mem_read(MTK, item + 276, &itemSeq, sizeof(itemSeq));
            (void)uc_mem_read(MTK, item + 282, &itemCategory, sizeof(itemCategory));
            vm_autotest_note("backpack_main_row index=%u ptr=%08x item=%u seq=%u count=%u category=%u\n",
                             i, item, itemId, itemSeq, itemCount, itemCategory);
        }
    }

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
        if (g_vmAutomation.scenario ==
                VM_AUTOMATION_SCENARIO_EQUIPMENT_ENHANCE_STAGE1_PROBE &&
            !g_vmAutomation.equipmentEnhanceBackpackGridCommitted &&
            itemCount != 0 && item0 == 1101u && seq0 == 9u)
        {
            /* The stage-1 probe must not treat the network queue as a
             * rendered backpack.  This PC runs only after the real 30/21
             * object became a client item row, so the following touch is
             * still ordinary UI input but cannot race the parser. */
            g_vmAutomation.equipmentEnhanceBackpackGridCommitted = 1;
            g_vmAutomation.equipmentEnhanceBackpackGridCommittedFrame =
                g_vmAutomation.renderFrames;
            vm_autotest_note("automation_equipment_enhance_backpack_grid_committed frame=%u item=%u seq=%u\n",
                             g_vmAutomation.renderFrames, item0, seq0);
        }
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

/*
 * mmGame is loaded at a variable pool address.  Identify sub_11CE from its
 * immutable entry bytes before interpreting a local PC as one of its branches.
 */
static bool vm_identify_mmgame_transfer_pc(u32 pc, u32 *codeBase, u32 *localPc)
{
    static const u8 parserEntry[] = {0xF0, 0xB5, 0xFF, 0xB0, 0xFF, 0xB0, 0xA9, 0xB0};
    static const u32 transferLocals[] = {0x1250u, 0x138Eu, 0x13FEu, 0x0BCCu};
    u8 entryBytes[sizeof(parserEntry)];

    for (u32 i = 0; i < sizeof(transferLocals) / sizeof(transferLocals[0]); ++i)
    {
        u32 candidateBase;

        if (pc < transferLocals[i])
            continue;
        candidateBase = pc - transferLocals[i];
        if ((candidateBase & 0xFu) != 0 ||
            candidateBase < VM_Memory_Pool_ADDRESS ||
            candidateBase + 0x11CEu + sizeof(parserEntry) > VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
        {
            continue;
        }
        if (uc_mem_read(MTK, candidateBase + 0x11CEu, entryBytes, sizeof(entryBytes)) != UC_ERR_OK ||
            memcmp(entryBytes, parserEntry, sizeof(parserEntry)) != 0)
        {
            continue;
        }

        if (codeBase)
            *codeBase = candidateBase;
        if (localPc)
            *localPc = transferLocals[i];
        return true;
    }
    return false;
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
    u32 codeBase = 0;
    u32 localPc = 0;
    char objectHead[64];

    objectHead[0] = 0;

    if (!vm_identify_mmgame_transfer_pc(pc, &codeBase, &localPc))
        return;

    if (localPc == 0x1250u)
    {
        if (seenResult16_3 >= 32)
            return;
        ++seenResult16_3;
        uc_reg_read(MTK, UC_ARM_REG_R0, &result);
        uc_reg_read(MTK, UC_ARM_REG_R5, &object);
    }
    else if (localPc == 0x138Eu)
    {
        if (seenResult16_2 >= 32)
            return;
        ++seenResult16_2;
        uc_reg_read(MTK, UC_ARM_REG_R0, &result);
        uc_reg_read(MTK, UC_ARM_REG_R5, &object);
    }
    else if (localPc == 0x13FEu)
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

    if (localPc == 0x1250u || localPc == 0x138Eu)
    {
        printf("[info][mmgame] transfer_result pc=%08x base=%08x local=%04x subtype=%u result=%u object=%08x kind=%u getter_int=%08x head=%s count=%u\n",
               pc, codeBase, localPc, subtype, result, object, kind, getterInt, objectHead,
               localPc == 0x1250u ? seenResult16_3 : seenResult16_2);
        vm_autotest_note("mmgame_transfer_result pc=%08x base=%08x local=%04x subtype=%u result=%u object=%08x kind=%u getter_int=%08x head=%s count=%u\n",
                         pc, codeBase, localPc, subtype, result, object, kind, getterInt, objectHead,
                         localPc == 0x1250u ? seenResult16_3 : seenResult16_2);
        return;
    }

    printf("[info][mmgame] transfer_sub_bcc pc=%08x base=%08x local=%04x object=%08x kind=%u subtype=%u index=%u getters raw=%08x str=%08x u16=%08x int=%08x len=%08x head=%s count=%u\n",
           pc, codeBase, localPc, object, kind, subtype, index, getterRaw, getterString, getterU16,
           getterInt, getterLen, objectHead,
           localPc == 0x13FEu ? seenSubBccCall : seenSubBccEntry);
    vm_autotest_note("mmgame_transfer_sub_bcc pc=%08x base=%08x local=%04x object=%08x kind=%u subtype=%u index=%u getters raw=%08x str=%08x u16=%08x int=%08x len=%08x head=%s count=%u\n",
                     pc, codeBase, localPc, object, kind, subtype, index, getterRaw, getterString,
                     getterU16, getterInt, getterLen, objectHead,
                     localPc == 0x13FEu ? seenSubBccCall : seenSubBccEntry);
}

static void vm_trace_mmgame_transfer_image_at_crash(void)
{
    static u32 scanCount = 0;
    static const u8 parserEntry[] = {0xF0, 0xB5, 0xFF, 0xB0, 0xFF, 0xB0, 0xA9, 0xB0};
    static const u8 result16_2[] = {0x02, 0x28, 0x12, 0xD1};
    static const u8 subBccCall[] = {0xFF, 0xF7, 0xE5, 0xFB};
    static const u8 subBccEntry[] = {0xF0, 0xB5, 0xFF, 0xB0, 0x04, 0x1C, 0x0E, 0x1C};
    u8 page[0x1000];
    u32 matches = 0;

    if (scanCount >= 1)
        return;
    ++scanCount;

    for (u32 pageBase = VM_Memory_Pool_ADDRESS;
         pageBase < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE;
         pageBase += sizeof(page))
    {
        if (uc_mem_read(MTK, pageBase, page, sizeof(page)) != UC_ERR_OK)
            continue;
        for (u32 pageOffset = 0xEu;
             pageOffset + sizeof(parserEntry) <= sizeof(page);
             pageOffset += 0x10u)
        {
            u32 entry = pageBase + pageOffset;
            u32 codeBase;
            u8 branchBytes[sizeof(subBccEntry)];

            if (memcmp(page + pageOffset, parserEntry, sizeof(parserEntry)) != 0 ||
                entry < 0x11CEu)
            {
                continue;
            }
            codeBase = entry - 0x11CEu;
            if (codeBase < VM_Memory_Pool_ADDRESS ||
                codeBase + 0x13FEu + sizeof(branchBytes) > VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE ||
                uc_mem_read(MTK, codeBase + 0x138Eu, branchBytes, sizeof(result16_2)) != UC_ERR_OK ||
                memcmp(branchBytes, result16_2, sizeof(result16_2)) != 0 ||
                uc_mem_read(MTK, codeBase + 0x13FEu, branchBytes, sizeof(subBccCall)) != UC_ERR_OK ||
                memcmp(branchBytes, subBccCall, sizeof(subBccCall)) != 0 ||
                uc_mem_read(MTK, codeBase + 0x0BCCu, branchBytes, sizeof(subBccEntry)) != UC_ERR_OK ||
                memcmp(branchBytes, subBccEntry, sizeof(subBccEntry)) != 0)
            {
                continue;
            }
            printf("[info][mmgame] transfer_image crash_scan base=%08x result16_2=%08x sub_bcc_call=%08x sub_bcc=%08x\n",
                   codeBase, codeBase + 0x138Eu, codeBase + 0x13FEu, codeBase + 0x0BCCu);
            vm_autotest_note("mmgame_transfer_image crash_scan base=%08x result16_2=%08x sub_bcc_call=%08x sub_bcc=%08x\n",
                             codeBase, codeBase + 0x138Eu, codeBase + 0x13FEu, codeBase + 0x0BCCu);
            ++matches;
        }
    }
    printf("[info][mmgame] transfer_image crash_scan_done matches=%u\n", matches);
    vm_autotest_note("mmgame_transfer_image crash_scan_done matches=%u\n", matches);
}

/*
 * A category-15 item bypasses TimerControl_ProcessItem's logical backpack
 * capacity check.  When its 74 physical records are already full, the
 * firmware calls UpdateActorHpMpDisplay(NULL).  This bounded, read-only probe
 * records the source row and manager occupancy before that decision so a
 * protocol response can be attributed without changing client state.
 */
static void vm_note_timer_control_item_pc(u32 pc)
{
    static u32 observations = 0;
    u32 manager = 0;
    u32 item = 0;
    u32 slots = 0;
    u32 itemId = 0;
    u16 logicalCount = 0;
    u16 logicalCapacity = 0;
    u16 physicalCapacity = 0;
    u16 amount = 0;
    u16 maxAmount = 0;
    u16 seq = 0;
    u8 itemType = 0;
    u32 occupied = 0;
    u32 empty = 0;

    if (pc != 0x01032EB8u || observations >= 24u)
        return;
    uc_reg_read(MTK, UC_ARM_REG_R0, &manager);
    uc_reg_read(MTK, UC_ARM_REG_R1, &item);
    if (manager == 0 || item == 0 ||
        uc_mem_read(MTK, item, &itemId, sizeof(itemId)) != UC_ERR_OK ||
        uc_mem_read(MTK, item + 242u, &amount, sizeof(amount)) != UC_ERR_OK ||
        uc_mem_read(MTK, item + 244u, &maxAmount, sizeof(maxAmount)) != UC_ERR_OK ||
        uc_mem_read(MTK, item + 276u, &seq, sizeof(seq)) != UC_ERR_OK ||
        uc_mem_read(MTK, item + 282u, &itemType, sizeof(itemType)) != UC_ERR_OK ||
        itemType != 15u ||
        uc_mem_read(MTK, manager + 32u, &slots, sizeof(slots)) != UC_ERR_OK ||
        uc_mem_read(MTK, manager + 36u, &logicalCount,
                    sizeof(logicalCount)) != UC_ERR_OK ||
        uc_mem_read(MTK, manager + 38u, &logicalCapacity,
                    sizeof(logicalCapacity)) != UC_ERR_OK ||
        uc_mem_read(MTK, manager + 40u, &physicalCapacity,
                    sizeof(physicalCapacity)) != UC_ERR_OK)
    {
        return;
    }
    if (physicalCapacity > 128u)
        return;
    for (u32 slot = 0; slot < physicalCapacity; ++slot)
    {
        u32 slotId = 0;

        if (uc_mem_read(MTK, slots + slot * 324u, &slotId,
                        sizeof(slotId)) != UC_ERR_OK)
        {
            return;
        }
        if ((int32_t)slotId < 0)
            ++empty;
        else
            ++occupied;
    }
    ++observations;
    printf("[info][item-timer] category15_insert count=%u item=%u seq=%u amount=%u max=%u manager=%08x logical=%u/%u physical=%u occupied=%u empty=%u\\n",
           observations, itemId, seq, amount, maxAmount, manager,
           logicalCount, logicalCapacity, physicalCapacity, occupied, empty);
    vm_autotest_note("item_timer_category15_insert count=%u item=%u seq=%u amount=%u max=%u logical=%u/%u physical=%u occupied=%u empty=%u evidence=JianghuOL.CBE:0x01032EB8\\n",
                     observations, itemId, seq, amount, maxAmount,
                     logicalCount, logicalCapacity, physicalCapacity,
                     occupied, empty);
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
    char lrHead[64];

    if (pc != 0x01033A42)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R0, &blob);
    if (blob != 0)
        return;
    if (seenNullBlob >= 16)
        return;
    ++seenNullBlob;

    vm_trace_mmgame_transfer_image_at_crash();

    readerHead[0] = 0;
    lrHead[0] = 0;
    uc_reg_read(MTK, UC_ARM_REG_R1, &reader);
    uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (reader != 0)
    {
        uc_mem_read(MTK, reader, &cursor, sizeof(cursor));
        vm_autotest_format_mem_hex(reader, 32, readerHead, sizeof(readerHead));
    }
    if (lr >= 4)
        vm_autotest_format_mem_hex(lr - 4, 16, lrHead, sizeof(lrHead));

    printf("[info][stream] read_i16_null_blob pc=%08x lr=%08x reader=%08x cursor=%u r2=%08x r3=%08x reader_head=%s lr_head=%s count=%u\n",
           pc, lr, reader, cursor, r2, r3, readerHead, lrHead, seenNullBlob);
    vm_autotest_note("stream_read_i16_null_blob pc=%08x lr=%08x reader=%08x cursor=%u r2=%08x r3=%08x reader_head=%s lr_head=%s count=%u\n",
                     pc, lr, reader, cursor, r2, r3, readerHead, lrHead, seenNullBlob);
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
            u32 battleMp = 0;
            u32 battleHpMax = 0;
            u32 battleMpMax = 0;
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
            uc_mem_read(MTK, node + 0xB8, &battleMp, sizeof(battleMp));
            uc_mem_read(MTK, node + 0xBC, &battleHpMax, sizeof(battleHpMax));
            uc_mem_read(MTK, node + 0xC0, &battleMpMax, sizeof(battleMpMax));
            uc_mem_read(MTK, node + 0xF0, &battleX, sizeof(battleX));
            uc_mem_read(MTK, node + 0xF4, &battleY, sizeof(battleY));
            if (actorId != 0 || active != 0 || nodeKind != 0 || promptKind != 0)
            {
                vm_autotest_note("scene_probe_node[%u] actorId=%u pos=(%u,%u) battlePos=(%u,%u) battleHp=%u/%u battleMp=%u/%u labelPtr=%08x kind=%u prompt=%u active=%u visual=(%u,%u) target=(%u,%u)\n",
                                 i, actorId, x, y, battleX, battleY, battleHp, battleHpMax,
                                 battleMp, battleMpMax,
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
#ifdef CBE_CLIENT_ONLY
    if (Lcd_Cache_Buffer == NULL)
        return;
    snprintf(path, sizeof(path), "autotest/screens/%06u_%08u.png",
             g_autotestShotIndex++, elapsedMs);
    if (!automation_png_write_rgb565(path, Lcd_Cache_Buffer,
                                     LCD_WIDTH, LCD_HEIGHT))
        printf("[warn][autotest] save_lcd_png_failed path=%s\n", path);
#else
    (void)elapsedMs;
    (void)path;
#endif
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
    vm_automation_tick();

    if (!g_vmAutomation.active && elapsed >= g_autotestNextShotMs)
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
        if (g_vmAutomation.active)
        {
            const char *kind = action->type == VM_AUTOTEST_ACTION_TAP ? "tap" :
                               action->type == VM_AUTOTEST_ACTION_WINDOW_TAP ? "windowtap" :
                               action->type == VM_AUTOTEST_ACTION_HOLD_KEY ? "hold-key" : "key";
            vm_autotest_note("automation_timed_input kind=%s planned_ms=%u actual_ms=%u a=%d b=%d one_shot=1\n",
                             kind, action->atMs, elapsed, action->a, action->b);
            ++g_vmAutomation.timedInputCount;
        }
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
                vm_host_handle_key_up(ev.key.keysym.sym);
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

/*
 * Read-only SCE entity-callback probe.  LoadSceneDataFromStream parses the
 * SCE2 header and prop layer itself, then invokes a scene-specific callback
 * at 0x010064B2 for the trailing scene entity data.  A generated scene can
 * be byte-valid yet fail to produce a live node, so the callback target is
 * the first contract boundary to inspect.  The layer-controller entry and
 * loader entry are traced too: this distinguishes an upstream null callback
 * from a scene path that never invokes this loader.  This probe deliberately
 * does not alter guest registers, memory, PC, or callback timing; it only
 * records normal call arguments.
 *
 * CBE_TRACE_SCE_ENTITY_CALLBACK records the callback boundary and one selected
 * node ID. CBE_TRACE_SCE_ENTITY_LOADER separately enables the high-volume
 * layer/loader entries. Inspect logs/sce-entity-callback.log; the trace is kept
 * separate from production network logs so it can be removed after the
 * callback grammar is recovered.
 */
static void vm_trace_sce_entity_callback_pc(u32 pc)
{
    const char *enabled = NULL;
    const char *loaderEnabled = NULL;
    u32 stream = 0;
    u32 offsetPtr = 0;
    u32 callback = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 stackArgs[4];
    u32 streamOffset = 0;
    u8 header[96];
    char mapName[80];
    size_t mapNameLen = 0;
    FILE *trace = NULL;

    if ((pc != 0x01006204 && pc != 0x010064B2 && pc != 0x010067F0) ||
        MTK == NULL)
        return;
    enabled = getenv("CBE_TRACE_SCE_ENTITY_CALLBACK");
    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0)
    {
        return;
    }
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &stream);
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &offsetPtr);
    (void)uc_reg_read(MTK, UC_ARM_REG_R2, &callback);
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (pc == 0x010067F0)
    {
        loaderEnabled = getenv("CBE_TRACE_SCE_ENTITY_LOADER");
        if (loaderEnabled == NULL || loaderEnabled[0] == 0 ||
            strcmp(loaderEnabled, "0") == 0 ||
            strcmp(loaderEnabled, "off") == 0 ||
            strcmp(loaderEnabled, "false") == 0)
        {
            return;
        }
        memset(stackArgs, 0, sizeof(stackArgs));
        (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp != 0)
            (void)uc_mem_read(MTK, sp, stackArgs, sizeof(stackArgs));
        trace = fopen("logs/sce-entity-callback.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "sce_layer_init entry pc=%08x controller=%08x arg1=%08x "
                    "arg2=%08x arg3=%08x stream_resource=%08x callback=%08x "
                    "lr=%08x\n",
                    pc, stream, offsetPtr, callback, lr,
                    stackArgs[0], stackArgs[3], lr);
            fclose(trace);
        }
        printf("[info][scene] sce_layer_init entry pc=%08x controller=%08x "
               "stream_resource=%08x callback=%08x lr=%08x\n",
               pc, stream, stackArgs[0], stackArgs[3], lr);
        return;
    }
    if (pc == 0x01006204)
    {
        loaderEnabled = getenv("CBE_TRACE_SCE_ENTITY_LOADER");
        if (loaderEnabled == NULL || loaderEnabled[0] == 0 ||
            strcmp(loaderEnabled, "0") == 0 ||
            strcmp(loaderEnabled, "off") == 0 ||
            strcmp(loaderEnabled, "false") == 0)
        {
            return;
        }
        trace = fopen("logs/sce-entity-callback.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "sce_entity_loader entry pc=%08x controller=%08x resource=%08x "
                    "callback=%08x lr=%08x\n",
                    pc, stream, offsetPtr, callback, lr);
            fclose(trace);
        }
        printf("[info][scene] sce_entity_loader entry pc=%08x controller=%08x "
               "resource=%08x callback=%08x lr=%08x\n",
               pc, stream, offsetPtr, callback, lr);
        return;
    }
    if (offsetPtr != 0)
        (void)uc_mem_read(MTK, offsetPtr, &streamOffset, sizeof(streamOffset));
    memset(header, 0, sizeof(header));
    if (stream != 0)
        (void)uc_mem_read(MTK, stream, header, sizeof(header));
    snprintf(mapName, sizeof(mapName), "-");
    if (memcmp(header, "SCE2", 4) == 0 && header[10] != 0 &&
        11u + header[10] <= sizeof(header))
    {
        mapNameLen = header[10];
        if (mapNameLen >= sizeof(mapName))
            mapNameLen = sizeof(mapName) - 1u;
        memcpy(mapName, header + 11, mapNameLen);
        mapName[mapNameLen] = 0;
    }
    trace = fopen("logs/sce-entity-callback.log", "ab");
    if (trace != NULL)
    {
        fprintf(trace,
                "sce_entity_callback before-blx pc=%08x callback=%08x "
                "stream=%08x offset_ptr=%08x offset=%u lr=%08x map=%s "
                "next=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                pc, callback, stream, offsetPtr, streamOffset, lr, mapName,
                streamOffset < sizeof(header) ? header[streamOffset] : 0,
                streamOffset + 1u < sizeof(header) ? header[streamOffset + 1u] : 0,
                streamOffset + 2u < sizeof(header) ? header[streamOffset + 2u] : 0,
                streamOffset + 3u < sizeof(header) ? header[streamOffset + 3u] : 0,
                streamOffset + 4u < sizeof(header) ? header[streamOffset + 4u] : 0,
                streamOffset + 5u < sizeof(header) ? header[streamOffset + 5u] : 0,
                streamOffset + 6u < sizeof(header) ? header[streamOffset + 6u] : 0,
                streamOffset + 7u < sizeof(header) ? header[streamOffset + 7u] : 0);
        fclose(trace);
    }
    printf("[info][scene] sce_entity_callback pc=%08x callback=%08x stream=%08x "
           "offset=%u lr=%08x map=%s\n",
           pc, callback, stream, streamOffset, lr, mapName);
}

/*
 * Read-only proof for the action13 contract.  SendNPCInteractReq scans this
 * exact table (R9+0x5CB0, 25 rows of 340 bytes) for an active node whose
 * +100 actor ID equals R0, then writes that row as the 1/4/1 index.  Logging
 * the table at its entry lets us distinguish a missing resource reload from a
 * malformed SCE entity without manufacturing an index or changing node state.
 */
static void vm_trace_scene_challenge_node_table_pc(u32 pc)
{
    const char *enabled = NULL;
    u32 requestedId = 0;
    u32 nodeBase = 0;
    char rows[1536];
    size_t used = 0;
    FILE *trace = NULL;

    if (pc != 0x01037ED4 || MTK == NULL || Global_R9 == 0)
        return;
    enabled = getenv("CBE_TRACE_SCE_ENTITY_CALLBACK");
    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0)
    {
        return;
    }
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &requestedId);
    if (uc_mem_read(MTK, Global_R9 + 0x5CB0u, &nodeBase, sizeof(nodeBase)) != UC_ERR_OK ||
        nodeBase == 0)
    {
        return;
    }
    rows[0] = 0;
    for (u32 index = 0; index < 25; ++index)
    {
        u32 node = nodeBase + index * 340u;
        u32 actorId = 0;
        u16 x = 0;
        u16 y = 0;
        u8 active = 0;
        u8 kind = 0;
        int written = 0;

        if (uc_mem_read(MTK, node + 319u, &active, sizeof(active)) != UC_ERR_OK ||
            active == 0)
        {
            continue;
        }
        (void)uc_mem_read(MTK, node + 315u, &kind, sizeof(kind));
        (void)uc_mem_read(MTK, node + 100u, &actorId, sizeof(actorId));
        (void)uc_mem_read(MTK, node + 24u, &x, sizeof(x));
        (void)uc_mem_read(MTK, node + 26u, &y, sizeof(y));
        if (used >= sizeof(rows))
            break;
        written = snprintf(rows + used, sizeof(rows) - used,
                           "%s%u:k%u:id%u@%u,%u%s",
                           used == 0 ? "" : ";", index, kind, actorId, x, y,
                           actorId == requestedId ? "*" : "");
        if (written < 0 || (size_t)written >= sizeof(rows) - used)
        {
            used = sizeof(rows) - 1u;
            rows[used] = 0;
            break;
        }
        used += (size_t)written;
    }
    trace = fopen("logs/sce-entity-callback.log", "ab");
    if (trace != NULL)
    {
        fprintf(trace,
                "scene_challenge_nodes pc=%08x requested=%u r9=%08x base=%08x active=[%s]\n",
                pc, requestedId, Global_R9, nodeBase, rows[0] ? rows : "-");
        fclose(trace);
    }
    printf("[info][scene] scene_challenge_nodes requested=%u active=[%s]\n",
           requestedId, rows[0] ? rows : "-");
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

/*
 * Read-only follow-up for a generated scene battle entity.  The scene
 * interaction trace proves whether the live node table contains the target,
 * but cannot distinguish a parser that rejects the raw record from one that
 * reaches the common node factory and subsequently removes the node.  Every
 * normal scene-node creation goes through scene_node_find_or_create at this
 * ROM entry.  The probe is limited to one explicitly selected actor ID
 * (CBE_TRACE_SCE_NODE_ACTOR_ID, default 1000) so it does not turn ordinary
 * movement into a high-volume trace.
 *
 * This only reads ABI arguments, stack arguments and LR at function entry.
 * It never changes guest memory, registers, PC/LR, callback order, or the
 * returned node.  It is therefore suitable for proving the first missing
 * state transition in the static SCE -> live kind-2 node path.
 */
static void vm_trace_scene_node_create_pc(u32 pc)
{
    const char *enabled = NULL;
    const char *targetText = NULL;
    u32 targetActorId = 1000u;
    u32 actorId = 0;
    u32 x = 0;
    u32 y = 0;
    u32 visualGroup = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 stackArgs[11];
    char label[96];
    char shortLabel[96];
    char longLabel[96];
    FILE *trace = NULL;

    if (pc != 0x0100EFC4 || MTK == NULL)
        return;
    enabled = getenv("CBE_TRACE_SCE_ENTITY_CALLBACK");
    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0)
    {
        return;
    }
    targetText = getenv("CBE_TRACE_SCE_NODE_ACTOR_ID");
    if (targetText != NULL && targetText[0] != 0)
    {
        char *end = NULL;
        unsigned long parsed = strtoul(targetText, &end, 0);
        if (end != targetText && end != NULL && *end == 0 && parsed <= UINT32_MAX)
            targetActorId = (u32)parsed;
    }
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &actorId);
    if (actorId != targetActorId)
        return;
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &x);
    (void)uc_reg_read(MTK, UC_ARM_REG_R2, &y);
    (void)uc_reg_read(MTK, UC_ARM_REG_R3, &visualGroup);
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    memset(stackArgs, 0, sizeof(stackArgs));
    if (sp != 0)
        (void)uc_mem_read(MTK, sp, stackArgs, sizeof(stackArgs));
    vm_trace_read_guest_string(stackArgs[1], label, sizeof(label));
    vm_trace_read_guest_string(stackArgs[3], shortLabel, sizeof(shortLabel));
    vm_trace_read_guest_string(stackArgs[9], longLabel, sizeof(longLabel));
    trace = fopen("logs/sce-entity-callback.log", "ab");
    if (trace != NULL)
    {
        fprintf(trace,
                "scene_node_create entry pc=%08x actor=%u pos=%u,%u group=%u "
                "variant=%u label=%s/%u short=%s/%u target=%u,%u flags=%u,%u "
                "long=%s/%u lr=%08x sp=%08x\n",
                pc, actorId, x, y, visualGroup, stackArgs[0], label, stackArgs[2],
                shortLabel, stackArgs[4], stackArgs[5], stackArgs[6], stackArgs[7],
                stackArgs[8], longLabel, stackArgs[10], lr, sp);
        fclose(trace);
    }
    printf("[info][scene] scene_node_create actor=%u pos=%u,%u group=%u variant=%u "
           "lr=%08x\n",
           actorId, x, y, visualGroup, stackArgs[0], lr);
}

/*
 * Read-only boundary trace for the client-native challenge route.  The
 * scene's +108 vtable slot is TriggerAutoBattle, while the legacy task-hall
 * action-13 path enters SendNPCInteractReq through task_hall_activate_selected
 * entry.  Recording both boundaries, including the selected task slot, lets
 * a native-vs-test comparison prove whether the test node ever becomes an
 * action entry.  This probe never changes guest state or input timing.
 */
static void vm_trace_action13_boundary_pc(u32 pc)
{
    const char *enabled = NULL;
    FILE *trace = NULL;
    u32 lr = 0;
    u32 r0 = 0;
    u32 slotIndex = 0;
    u32 slot = 0;
    u8 action = 0;
    u32 value = 0;
    u32 nodeBase = 0;
    u32 matchedIndex = UINT32_MAX;

    if (MTK == NULL ||
        (pc != 0x010492B0u && pc != 0x01037ED4u))
        return;
    enabled = getenv("CBE_TRACE_SCE_ENTITY_CALLBACK");
    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0)
        return;

    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    if (Global_R9 != 0)
    {
        (void)uc_mem_read(MTK, Global_R9 + 40070u, &slotIndex,
                          sizeof(slotIndex));
        slotIndex = (slotIndex >> 24) & 0xffu;
        if (slotIndex < 32u)
        {
            slot = Global_R9 + 38444u + slotIndex * 64u;
            (void)uc_mem_read(MTK, slot + 44u, &action, sizeof(action));
            (void)uc_mem_read(MTK, slot + 48u, &value, sizeof(value));
        }
        (void)uc_mem_read(MTK, Global_R9 + 23728u, &nodeBase,
                          sizeof(nodeBase));
    }
    if (pc == 0x01037ED4u && nodeBase != 0)
    {
        for (u32 index = 0; index < 25u; ++index)
        {
            u8 active = 0;
            u32 actor = 0;
            u32 node = nodeBase + index * 340u;
            (void)uc_mem_read(MTK, node + 319u, &active, sizeof(active));
            (void)uc_mem_read(MTK, node + 100u, &actor, sizeof(actor));
            if (active != 0 && actor == r0)
            {
                matchedIndex = index;
                break;
            }
        }
    }
    trace = fopen("logs/sce-entity-callback.log", "ab");
    if (trace == NULL)
        return;
    fprintf(trace,
            "action13_boundary pc=%08x lr=%08x actor_arg=%u slot_index=%u "
            "slot=%08x action=%u value=%u node_base=%08x matched_index=%u "
            "scene=%s\n",
            pc, lr, r0, slotIndex, slot, (unsigned)action, value, nodeBase,
            matchedIndex, g_lastSceLoadName[0] ? g_lastSceLoadName : "-");
    fclose(trace);
}

/*
 * Read-only probe for the native scene-monster collision function.
 * Runtime evidence ties TriggerAutoBattle to the real scene WT 4/1 sender.
 * Trace one actor's scan/callback state and the owning scene-control dispatch.
 * No guest state, registers, callbacks, or timing are changed.
 */
static void vm_trace_scene_battle_collision_pc(u32 pc)
{
    static u32 triggerEntrySequence = 0;
    static u32 tracedLogicEntry = 0;
    static u32 tracedModuleR9 = 0;
    static u32 logicTargets[7];
    static u32 logicTargetCalls[7];
    static u32 sceneModuleCodeBase = 0;
    /* The input callback base is independently proven by the sub_604 and
     * sub_8A8 instruction fingerprints.  The active scene tick is at
     * module+0x566, rather than at the fingerprint's +0x604 address. */
    static u32 inputDispatchModuleBase = 0;
    static u32 sceneControlObject = 0;
    static u32 sceneControlCallback = 0;
    static u32 sceneControlCallbackCalls = 0;
    static u32 actionDispatchTraceCount = 0;
    static u32 actionRouteTraceCount = 0;
    static u32 actionCallbackTraceCount = 0;
    static u32 inputRegistryTarget = 0;
    static u32 inputDispatchCallback = 0;
    /* A battle transition can replace vmAddedScreen before the registered
     * action callback tail-calls TriggerAutoBattle.  Keep a tiny, proven-only
     * history so the instruction hook can still observe that callback after
     * the screen-local trace state is reset. */
    static u32 retainedInputDispatchCallbacks[4] = {0};
    static u32 retainedInputDispatchBases[4] = {0};
    static u32 retainedInputDispatchNext = 0;
    static u32 mainInputDispatcher = 0;
    static u32 mainRenderDispatcher = 0;
    static u32 mainSceneStateSetter = 0;
    static u32 inputRegistryTraceCount = 0;
    static u32 mainInputDispatcherTraceCount = 0;
    static u32 mainRenderDispatcherTraceCount = 0;
    static u32 mainSceneStateSetterTraceCount = 0;
    static u32 sceneResponseDispatchTraceCount = 0;
    static u32 sceneResponseEnterTraceCount = 0;
    static u32 lastLiveInputDispatchCallback = UINT32_MAX;
    static u32 lastMainActionCallback = UINT32_MAX;
    static bool inputDispatchSlotSeen = false;
    static bool logicPositionSeen = false;
    static int16_t lastLogicPlayerX = 0;
    static int16_t lastLogicPlayerY = 0;
    static bool collisionPending = false;
    static u32 collisionSequence = 0;
    static u32 collisionTraceCount = 0;
    static u32 collisionPendingActor = 0;
    static u32 collisionPendingIndex = UINT32_MAX;
    static u32 collisionPendingLr = 0;
    static u32 collisionPendingNode = 0;
    static int16_t collisionPendingPlayerX = 0;
    static int16_t collisionPendingPlayerY = 0;
    static int16_t collisionPendingNodeX = 0;
    static int16_t collisionPendingNodeY = 0;
    static u32 lastNodeBase = 0;
    static int16_t lastEntryX = 0;
    static int16_t lastEntryY = 0;
    static u32 lastEntryGates = UINT32_MAX;
    static bool entrySeen = false;
    static int16_t lastBeforeX[25];
    static int16_t lastBeforeY[25];
    static bool beforeSeen[25];
    static int16_t lastAfterX[25];
    static int16_t lastAfterY[25];
    static u8 lastAfterResult[25];
    static bool afterSeen[25];
    static bool apiSlotLogged = false;
    static u32 candidateActiveLogic = 0;
    static u32 candidateActiveLogicLocal = UINT32_MAX;
    static int candidateActiveLogicAppIndex = -1;
    const char *enabled = NULL;
    const char *targetText = NULL;
    u32 targetActorId = 1000u;
    u32 nodeBase = 0;
    u32 playerNode = 0;
    u32 engine = 0;
    FILE *trace = NULL;
    u32 activeLogic = 0;
    u32 activeLogicLocal = UINT32_MAX;
    int activeLogicAppIndex = -1;
    u32 retainedInputDispatchBase = 0;
    bool retainedInputDispatchEntry = false;
    u32 observedInputDispatchCallback = 0;
    bool observedInputDispatchEntry = false;
    static u32 activeLogicScreen = 0;
    static int traceEnabled = -1;
    static u32 configuredTargetActorId = 1000u;

    if (MTK == NULL)
        return;
    if (traceEnabled < 0)
    {
        enabled = getenv("CBE_TRACE_SCENE_BATTLE_COLLISION");
        traceEnabled = enabled != NULL && enabled[0] != 0 &&
                       strcmp(enabled, "0") != 0 &&
                       strcmp(enabled, "off") != 0 &&
                       strcmp(enabled, "false") != 0;
        targetText = getenv("CBE_TRACE_SCE_NODE_ACTOR_ID");
        if (targetText != NULL && targetText[0] != 0)
        {
            char *end = NULL;
            unsigned long parsed = strtoul(targetText, &end, 0);
            if (end != targetText && end != NULL && *end == 0 &&
                parsed <= UINT32_MAX)
                configuredTargetActorId = (u32)parsed;
        }
    }
    if (!traceEnabled)
        return;
    targetActorId = configuredTargetActorId;
    /* Resolving the current screen's module address is expensive.  It only
     * needs to happen when the active screen changes; the normal instruction
     * hook must stay constant-time between those transitions. */
    if (vmAddedScreen != activeLogicScreen)
    {
        u32 startupMainApi = 0;
        u32 startupInputRegistryTarget = 0;

        activeLogicScreen = vmAddedScreen;
        tracedLogicEntry = 0;
        tracedModuleR9 = 0;
        sceneControlCallback = 0;
        sceneModuleCodeBase = 0;
        inputDispatchModuleBase = 0;
        actionDispatchTraceCount = 0;
        actionRouteTraceCount = 0;
        actionCallbackTraceCount = 0;
        inputRegistryTarget = 0;
        inputDispatchCallback = 0;
        mainInputDispatcher = 0;
        mainRenderDispatcher = 0;
        mainSceneStateSetter = 0;
        inputRegistryTraceCount = 0;
        mainInputDispatcherTraceCount = 0;
        mainRenderDispatcherTraceCount = 0;
        mainSceneStateSetterTraceCount = 0;
        sceneResponseDispatchTraceCount = 0;
        sceneResponseEnterTraceCount = 0;
        lastLiveInputDispatchCallback = UINT32_MAX;
        lastMainActionCallback = UINT32_MAX;
        inputDispatchSlotSeen = false;
        /* sub_1444 can register its input callback during the first screen
         * lifecycle before scene logic has reached sub_604.  The main CBE API
         * table is already stable at Global_R9+0x2054, so seed the setter
         * address at the screen boundary without reading or changing module
         * state. */
        if (Global_R9 != 0)
        {
            (void)uc_mem_read(MTK, Global_R9 + 0x2054u, &startupMainApi,
                              sizeof(startupMainApi));
            if (startupMainApi != 0)
            {
                (void)uc_mem_read(MTK, startupMainApi + 52u,
                                  &startupInputRegistryTarget,
                                  sizeof(startupInputRegistryTarget));
            }
        }
        inputRegistryTarget = startupInputRegistryTarget;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_input_registry phase=screen-change "
                    "screen=%08x main_api=%08x setter=%08x scene=%s\n",
                    vmAddedScreen, startupMainApi, inputRegistryTarget,
                    g_lastSceLoadName[0] ? g_lastSceLoadName : "-");
            fclose(trace);
        }
        candidateActiveLogic = vmAddedScreen != 0
                                   ? vm_get_var(vmAddedScreen + 8u) & ~1u
                                   : 0;
        candidateActiveLogicAppIndex =
            vm_dl_find_loaded_index_by_pc(candidateActiveLogic);
        candidateActiveLogicLocal = UINT32_MAX;
        if (candidateActiveLogicAppIndex >= 0)
        {
            candidateActiveLogicLocal =
                candidateActiveLogic -
                g_vmDlLoadedApps[candidateActiveLogicAppIndex].buffer;
        }
    }
    if (tracedLogicEntry == 0)
    {
        activeLogic = candidateActiveLogic;
        activeLogicAppIndex = candidateActiveLogicAppIndex;
        activeLogicLocal = candidateActiveLogicLocal;
    }
    else
    {
        activeLogic = tracedLogicEntry;
    }

    /* A normal wilderness scene can reach its module+0x566 tick before (or
     * without) a fresh api+52 input-registration observation.  The separate
     * module+0x604 and +0x8A8 fingerprints validate that inferred code image.
     * This derives trace addresses from executable bytes only; it never
     * publishes an address back into guest state. */
    if (activeLogic >= 0x566u)
    {
        static const u8 logicFingerprint[8] = {
            0x10u, 0xB5u, 0x84u, 0x4Cu, 0x01u, 0x21u, 0x4Cu, 0x44u};
        static const u8 actionFingerprint[8] = {
            0xFEu, 0xB5u, 0x02u, 0x1Cu, 0xEBu, 0x48u, 0x0Cu, 0x1Cu};
        u32 inferredBase = activeLogic - 0x566u;
        u8 observedLogic[sizeof(logicFingerprint)] = {0};
        u8 observedAction[sizeof(actionFingerprint)] = {0};

        if (uc_mem_read(MTK, inferredBase + 0x604u, observedLogic,
                        sizeof(observedLogic)) == UC_ERR_OK &&
            uc_mem_read(MTK, inferredBase + 0x8A8u, observedAction,
                        sizeof(observedAction)) == UC_ERR_OK &&
            memcmp(observedLogic, logicFingerprint,
                   sizeof(logicFingerprint)) == 0 &&
            memcmp(observedAction, actionFingerprint,
                   sizeof(actionFingerprint)) == 0 &&
            inputDispatchModuleBase != inferredBase)
        {
            inputDispatchModuleBase = inferredBase;
            g_vmTraceMmGameInputCodeBase = inferredBase;
            actionDispatchTraceCount = 0;
            actionRouteTraceCount = 0;
            actionCallbackTraceCount = 0;
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_scheduler phase=module-base-inferred "
                        "source=active-logic logic=%08x module_base=%08x "
                        "fingerprint=tick_566+sub_604+sub_8A8 scene=%s\n",
                        activeLogic, inputDispatchModuleBase,
                        g_lastSceLoadName);
                fclose(trace);
            }
        }
    }

    for (u32 retainedIndex = 0;
         retainedIndex < mySizeOf(retainedInputDispatchCallbacks);
         ++retainedIndex)
    {
        if (retainedInputDispatchCallbacks[retainedIndex] != 0 &&
            pc == (retainedInputDispatchCallbacks[retainedIndex] & ~1u))
        {
            retainedInputDispatchEntry = true;
            retainedInputDispatchBase =
                retainedInputDispatchBases[retainedIndex];
            break;
        }
    }
    observedInputDispatchCallback = g_vmSceneInputCallbackLastObserved;
    observedInputDispatchEntry = observedInputDispatchCallback != 0 &&
                                  pc ==
                                      (observedInputDispatchCallback & ~1u);

    if (pc != 0x0100F5B4u && pc != 0x010183A0u &&
        pc != 0x010184D2u && pc != 0x010184D4u &&
        pc != 0x01004CE8u && pc != 0x01004DC8u &&
        pc != activeLogic && pc != (sceneControlCallback & ~1u) &&
        pc != (inputRegistryTarget & ~1u) &&
        pc != (inputDispatchCallback & ~1u) &&
        pc != (mainInputDispatcher & ~1u) &&
        pc != (mainRenderDispatcher & ~1u) &&
        pc != (mainSceneStateSetter & ~1u) &&
        !retainedInputDispatchEntry &&
        !observedInputDispatchEntry &&
        pc != inputDispatchModuleBase + 0x8A8u &&
        pc != inputDispatchModuleBase + 0xAC4u &&
        pc != inputDispatchModuleBase + 0x68Eu &&
        pc != inputDispatchModuleBase + 0xBCCu &&
        pc != inputDispatchModuleBase + 0x11CEu)
    {
        bool dynamicTarget = false;

        for (u32 targetIndex = 0; targetIndex < mySizeOf(logicTargets);
             ++targetIndex)
        {
            if (logicTargets[targetIndex] != 0 &&
                pc == (logicTargets[targetIndex] & ~1u))
            {
                dynamicTarget = true;
                break;
            }
        }
        if (!dynamicTarget)
            return;
    }

    if (inputRegistryTarget != 0 &&
        pc == (inputRegistryTarget & ~1u) &&
        inputRegistryTraceCount < 12u)
    {
        u32 lr = 0;
        u32 callback = 0;
        u32 moduleR9 = 0;
        u32 priorCallback = 0;
        u32 callbackBase = 0;
        u32 callbackLocal = UINT32_MAX;
        u16 callbackAppId = 0;
        int callbackAppIndex = -1;
        const char *callbackOwner = "unmapped";
        static const u8 logicFingerprint[8] = {
            0x10u, 0xB5u, 0x84u, 0x4Cu, 0x01u, 0x21u, 0x4Cu, 0x44u};
        static const u8 actionFingerprint[8] = {
            0xFEu, 0xB5u, 0x02u, 0x1Cu, 0xEBu, 0x48u, 0x0Cu, 0x1Cu};
        u8 observedLogic[sizeof(logicFingerprint)] = {0};
        u8 observedAction[sizeof(actionFingerprint)] = {0};
        u32 registeredModuleBase = 0;

        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &callback);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
        if (moduleR9 != 0)
        {
            (void)uc_mem_read(MTK, moduleR9 + 0x5D28u, &priorCallback,
                              sizeof(priorCallback));
        }
        callbackAppIndex = vm_dl_find_loaded_index_by_pc(callback);
        if (callbackAppIndex >= 0)
        {
            callbackOwner = "loaded-cbm";
            callbackBase = g_vmDlLoadedApps[callbackAppIndex].buffer;
            callbackAppId = g_vmDlLoadedApps[callbackAppIndex].appId;
            if (callbackBase != 0 && (callback & ~1u) >= callbackBase)
                callbackLocal = (callback & ~1u) - callbackBase;
        }
        /* sub_1444 registers sub_8A8 before the first scene logic tick. The
         * same sub_604/sub_8A8 fingerprints used for the proven trigger-LR
         * path turn that callback into a module base without writing it back
         * to the client or assuming a particular pool allocation. */
        if (callback >= 0x8A8u)
        {
            registeredModuleBase = (callback & ~1u) - 0x8A8u;
            if (uc_mem_read(MTK, registeredModuleBase + 0x604u,
                            observedLogic, sizeof(observedLogic)) == UC_ERR_OK &&
                uc_mem_read(MTK, registeredModuleBase + 0x8A8u,
                            observedAction, sizeof(observedAction)) == UC_ERR_OK &&
                memcmp(observedLogic, logicFingerprint,
                       sizeof(logicFingerprint)) == 0 &&
                memcmp(observedAction, actionFingerprint,
                       sizeof(actionFingerprint)) == 0)
            {
                inputDispatchModuleBase = registeredModuleBase;
                g_vmTraceMmGameInputCodeBase = registeredModuleBase;
                inputDispatchCallback = callback;
                {
                    bool retained = false;

                    for (u32 retainedIndex = 0;
                         retainedIndex <
                         mySizeOf(retainedInputDispatchCallbacks);
                         ++retainedIndex)
                    {
                        if (retainedInputDispatchCallbacks[retainedIndex] ==
                            (callback & ~1u))
                        {
                            retainedInputDispatchBases[retainedIndex] =
                                registeredModuleBase;
                            retained = true;
                            break;
                        }
                    }
                    if (!retained)
                    {
                        u32 retainedIndex = retainedInputDispatchNext++ %
                                            mySizeOf(retainedInputDispatchCallbacks);

                        retainedInputDispatchCallbacks[retainedIndex] =
                            callback & ~1u;
                        retainedInputDispatchBases[retainedIndex] =
                            registeredModuleBase;
                    }
                }
                actionDispatchTraceCount = 0;
                actionRouteTraceCount = 0;
                actionCallbackTraceCount = 0;
            }
            else
            {
                registeredModuleBase = 0;
            }
        }
        ++inputRegistryTraceCount;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_input_registry phase=api+52-call call=%u "
                    "pc=%08x lr=%08x callback=%08x prior_callback=%08x "
                    "callback_owner=%s callback_app=%u callback_base=%08x "
                    "registered_module_base=%08x active_logic=%08x "
                    "module_r9=%08x known_scene_module_base=%08x "
                    "callback_local=%08x scene=%s\n",
                    inputRegistryTraceCount, pc, lr, callback, priorCallback,
                    callbackOwner, (unsigned)callbackAppId, callbackBase,
                    registeredModuleBase, activeLogic, moduleR9,
                    inputDispatchModuleBase, callbackLocal,
                    g_lastSceLoadName);
            fclose(trace);
        }
        return;
    }

    if (observedInputDispatchEntry || retainedInputDispatchEntry ||
        (inputDispatchModuleBase != 0 &&
         (pc == inputDispatchModuleBase + 0x8A8u ||
          pc == inputDispatchModuleBase + 0xAC4u ||
          pc == inputDispatchModuleBase + 0x68Eu)))
    {
        const char *phase = NULL;
        u32 *phaseCount = NULL;
        u32 phaseLimit = 0;
        u32 actionModuleBase = retainedInputDispatchEntry
                                   ? retainedInputDispatchBase
                                   : inputDispatchModuleBase;
        u32 lr = 0;
        u32 regs[5] = {0, 0, 0, 0, 0};
        u32 moduleR9 = 0;
        u32 mainApi = 0;
        u32 mainCallback = 0;
        u32 privateObject = 0;
        u32 privateCallback = 0;

        if (observedInputDispatchEntry)
        {
            phase = "input-dispatch-slot-write";
            phaseCount = &actionDispatchTraceCount;
            phaseLimit = 24u;
        }
        else if (retainedInputDispatchEntry)
        {
            phase = "input-dispatch-retained";
            phaseCount = &actionDispatchTraceCount;
            phaseLimit = 24u;
        }
        else if (pc == inputDispatchModuleBase + 0x8A8u)
        {
            phase = "input-dispatch";
            phaseCount = &actionDispatchTraceCount;
            phaseLimit = 24u;
        }
        else if (pc == inputDispatchModuleBase + 0xAC4u)
        {
            phase = "touch-route-before-call";
            phaseCount = &actionRouteTraceCount;
            phaseLimit = 8u;
        }
        else
        {
            phase = "touch-callback-entry";
            phaseCount = &actionCallbackTraceCount;
            phaseLimit = 8u;
        }
        if (*phaseCount >= phaseLimit)
            return;

        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &regs[0]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &regs[1]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R2, &regs[2]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R3, &regs[3]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R4, &regs[4]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
        if (moduleR9 != 0)
        {
            (void)uc_mem_read(MTK, moduleR9 + 0x2054u, &mainApi,
                              sizeof(mainApi));
            (void)uc_mem_read(MTK, moduleR9 + 0x2850u, &privateObject,
                              sizeof(privateObject));
        }
        if (mainApi != 0)
        {
            (void)uc_mem_read(MTK, mainApi + 68u, &mainCallback,
                              sizeof(mainCallback));
        }
        if (privateObject != 0)
        {
            (void)uc_mem_read(MTK, privateObject + 20u, &privateCallback,
                              sizeof(privateCallback));
        }

        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            ++*phaseCount;
            fprintf(trace,
                    "scene_battle_action phase=%s call=%u pc=%08x lr=%08x "
                    "regs=%08x,%08x,%08x,%08x,%08x logic=%08x "
                    "module_base=%08x module_r9=%08x main_api=%08x "
                    "main_callback=%08x private_object=%08x "
                    "private_callback=%08x private_context=%08x scene=%s\n",
                    phase, *phaseCount, pc, lr, regs[0], regs[1], regs[2],
                    regs[3], regs[4], activeLogic, actionModuleBase,
                    moduleR9, mainApi, mainCallback, privateObject,
                    privateCallback,
                    moduleR9 != 0 ? moduleR9 + 0x2D44u : 0,
                    g_lastSceLoadName);
            fclose(trace);
        }
        return;
    }

    if (inputDispatchModuleBase != 0 &&
        (pc == inputDispatchModuleBase + 0x11CEu ||
         pc == inputDispatchModuleBase + 0xBCCu))
    {
        const char *phase = pc == inputDispatchModuleBase + 0x11CEu
                                ? "response-dispatch"
                                : "scene-enter-object";
        u32 *phaseCount = pc == inputDispatchModuleBase + 0x11CEu
                              ? &sceneResponseDispatchTraceCount
                              : &sceneResponseEnterTraceCount;
        u32 lr = 0;
        u32 regs[4] = {0, 0, 0, 0};
        u32 moduleR9 = 0;
        u32 mainApi = 0;
        u32 sceneEnterCallback = 0;

        if (*phaseCount >= 24u)
            return;
        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &regs[0]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &regs[1]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R2, &regs[2]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R3, &regs[3]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
        if (moduleR9 != 0)
        {
            (void)uc_mem_read(MTK, moduleR9 + 0x2054u, &mainApi,
                              sizeof(mainApi));
        }
        if (mainApi != 0)
        {
            (void)uc_mem_read(MTK, mainApi + 116u, &sceneEnterCallback,
                              sizeof(sceneEnterCallback));
        }
        ++*phaseCount;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_lifecycle phase=%s call=%u pc=%08x lr=%08x "
                    "regs=%08x,%08x,%08x,%08x main_api=%08x "
                    "scene_enter_callback=%08x scene=%s\n",
                    phase, *phaseCount, pc, lr, regs[0], regs[1], regs[2],
                    regs[3], mainApi, sceneEnterCallback, g_lastSceLoadName);
            fclose(trace);
        }
        return;
    }

    if (mainInputDispatcher != 0 &&
        pc == (mainInputDispatcher & ~1u) &&
        mainInputDispatcherTraceCount < 24u)
    {
        u32 lr = 0;
        u32 regs[4] = {0, 0, 0, 0};
        u32 moduleR9 = 0;
        u32 controlDelegate = 0;
        u32 liveCallback = 0;
        u32 touchDelegate = 0;
        u16 sceneControlState = 0;
        u8 sceneLifecycleState = 0;

        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &regs[0]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &regs[1]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R2, &regs[2]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R3, &regs[3]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
        if (moduleR9 != 0)
        {
            (void)uc_mem_read(MTK, moduleR9 + 23844u, &controlDelegate,
                              sizeof(controlDelegate));
            (void)uc_mem_read(MTK, moduleR9 + 0x5D28u, &liveCallback,
                              sizeof(liveCallback));
            (void)uc_mem_read(MTK, moduleR9 + 23852u, &touchDelegate,
                              sizeof(touchDelegate));
            (void)uc_mem_read(MTK, moduleR9 + 23682u, &sceneControlState,
                              sizeof(sceneControlState));
            (void)uc_mem_read(MTK, moduleR9 + 19638u, &sceneLifecycleState,
                              sizeof(sceneLifecycleState));
        }
        ++mainInputDispatcherTraceCount;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_input_dispatch phase=entry call=%u "
                    "pc=%08x lr=%08x regs=%08x,%08x,%08x,%08x "
                    "control_delegate=%08x input_callback=%08x "
                    "touch_delegate=%08x control_state=%u lifecycle_state=%u "
                    "scene=%s\n",
                    mainInputDispatcherTraceCount, pc, lr, regs[0], regs[1],
                    regs[2], regs[3], controlDelegate, liveCallback, touchDelegate,
                    (unsigned)sceneControlState, (unsigned)sceneLifecycleState,
                    g_lastSceLoadName);
            fclose(trace);
        }
        return;
    }

    if (mainRenderDispatcher != 0 &&
        pc == (mainRenderDispatcher & ~1u) &&
        mainRenderDispatcherTraceCount < 48u)
    {
        u32 lr = 0;
        u32 moduleR9 = 0;
        u32 controlDelegate = 0;
        u32 liveCallback = 0;
        u32 touchDelegate = 0;
        u16 sceneControlState = 0;
        u8 sceneLifecycleState = 0;

        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
        if (moduleR9 != 0)
        {
            (void)uc_mem_read(MTK, moduleR9 + 23844u, &controlDelegate,
                              sizeof(controlDelegate));
            (void)uc_mem_read(MTK, moduleR9 + 0x5D28u, &liveCallback,
                              sizeof(liveCallback));
            (void)uc_mem_read(MTK, moduleR9 + 23852u, &touchDelegate,
                              sizeof(touchDelegate));
            (void)uc_mem_read(MTK, moduleR9 + 23682u, &sceneControlState,
                              sizeof(sceneControlState));
            (void)uc_mem_read(MTK, moduleR9 + 19638u, &sceneLifecycleState,
                              sizeof(sceneLifecycleState));
        }
        ++mainRenderDispatcherTraceCount;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_render_dispatch phase=entry call=%u "
                    "pc=%08x lr=%08x control_delegate=%08x input_callback=%08x "
                    "touch_delegate=%08x control_state=%u lifecycle_state=%u scene=%s\n",
                    mainRenderDispatcherTraceCount, pc, lr, controlDelegate,
                    liveCallback, touchDelegate, (unsigned)sceneControlState,
                    (unsigned)sceneLifecycleState, g_lastSceLoadName);
            fclose(trace);
        }
        return;
    }

    if (mainSceneStateSetter != 0 &&
        pc == (mainSceneStateSetter & ~1u) &&
        mainSceneStateSetterTraceCount < 24u)
    {
        u32 lr = 0;
        u32 sp = 0;
        u32 nextState = 0;
        u32 moduleR9 = 0;
        u32 stackWords[12] = {0};
        u16 priorState = 0;

        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &nextState);
        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
        if (sp != 0)
            (void)uc_mem_read(MTK, sp, stackWords, sizeof(stackWords));
        if (moduleR9 != 0)
        {
            (void)uc_mem_read(MTK, moduleR9 + 23682u, &priorState,
                              sizeof(priorState));
        }
        ++mainSceneStateSetterTraceCount;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_control_state phase=setter-entry call=%u "
                    "pc=%08x lr=%08x prior=%u next=%u sp=%08x "
                    "stack=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,"
                    "%08x,%08x,%08x,%08x scene=%s\n",
                    mainSceneStateSetterTraceCount, pc, lr,
                    (unsigned)priorState, nextState, sp,
                    stackWords[0], stackWords[1], stackWords[2], stackWords[3],
                    stackWords[4], stackWords[5], stackWords[6], stackWords[7],
                    stackWords[8], stackWords[9], stackWords[10], stackWords[11],
                    g_lastSceLoadName);
            fclose(trace);
        }
        return;
    }

    if (tracedLogicEntry != 0)
    {
        static const char *const targetNames[7] = {
            "main-api+1080", "main-api+324", "private+80",
            "main-api+2480", "main-api+244", "main-api+1108",
            "control-object+48"};

        for (u32 targetIndex = 0; targetIndex < mySizeOf(logicTargets);
             ++targetIndex)
        {
            u32 target = logicTargets[targetIndex] & ~1u;
            if (target == 0 || pc != target ||
                logicTargetCalls[targetIndex] >= 4u)
            {
                continue;
            }

            u32 lr = 0;
            u32 args[4] = {0, 0, 0, 0};
            (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
            (void)uc_reg_read(MTK, UC_ARM_REG_R0, &args[0]);
            (void)uc_reg_read(MTK, UC_ARM_REG_R1, &args[1]);
            (void)uc_reg_read(MTK, UC_ARM_REG_R2, &args[2]);
            (void)uc_reg_read(MTK, UC_ARM_REG_R3, &args[3]);
            ++logicTargetCalls[targetIndex];
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_scheduler phase=logic-target-call "
                        "slot=%s call=%u pc=%08x lr=%08x args="
                        "%08x,%08x,%08x,%08x logic=%08x logic_local=%08x "
                        "module_r9=%08x caller_local=%08x scene=%s\n",
                        targetNames[targetIndex],
                        logicTargetCalls[targetIndex], pc, lr, args[0],
                        args[1], args[2], args[3], activeLogic,
                        activeLogicLocal, tracedModuleR9,
                        sceneModuleCodeBase != 0 && (lr & ~1u) >= sceneModuleCodeBase
                            ? (lr & ~1u) - sceneModuleCodeBase
                            : UINT32_MAX,
                        g_lastSceLoadName);
                fclose(trace);
            }
            break;
        }
    }

    if (sceneControlCallback != 0 &&
        pc == (sceneControlCallback & ~1u) &&
        sceneControlCallbackCalls < 4u)
    {
        u32 lr = 0;
        u32 args[4] = {0, 0, 0, 0};

        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &args[0]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &args[1]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R2, &args[2]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R3, &args[3]);
        ++sceneControlCallbackCalls;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_scheduler phase=control-callback call=%u "
                    "pc=%08x lr=%08x caller_local=%08x object=%08x args="
                    "%08x,%08x,%08x,%08x scene=%s\n",
                    sceneControlCallbackCalls, pc, lr,
                    sceneModuleCodeBase != 0 && (lr & ~1u) >= sceneModuleCodeBase
                        ? (lr & ~1u) - sceneModuleCodeBase
                        : UINT32_MAX,
                    sceneControlObject, args[0], args[1], args[2], args[3],
                    g_lastSceLoadName);
            fclose(trace);
        }
    }

    if (pc == activeLogic && activeLogic != 0)
    {
        u32 moduleR9 = 0;
        u32 mainApi = 0;
        u32 liveInputDispatchCallback = 0;
        u32 mainActionCallback = 0;
        u32 nextMainInputDispatcher = 0;
        u32 nextMainRenderDispatcher = 0;
        u32 privateObject = 0;
        u32 controlCallback = 0;
        u32 triggerSlot = 0;
        u32 nextInputRegistryTarget = 0;
        u32 nextTargets[7] = {0, 0, 0, 0, 0, 0, 0};

        (void)uc_reg_read(MTK, UC_ARM_REG_R9, &moduleR9);
        if (moduleR9 != 0)
        {
            (void)uc_mem_read(MTK, moduleR9 + 0x2054u, &mainApi,
                              sizeof(mainApi));
            (void)uc_mem_read(MTK, moduleR9 + 0x285Cu, &privateObject,
                              sizeof(privateObject));
            (void)uc_mem_read(MTK, moduleR9 + 0x5D28u,
                              &liveInputDispatchCallback,
                              sizeof(liveInputDispatchCallback));
        }
        if (mainApi != 0)
        {
            (void)uc_mem_read(MTK, mainApi + 108u, &triggerSlot,
                              sizeof(triggerSlot));
            (void)uc_mem_read(MTK, mainApi + 68u, &mainActionCallback,
                              sizeof(mainActionCallback));
            (void)uc_mem_read(MTK, mainApi + 88u,
                              &nextMainInputDispatcher,
                              sizeof(nextMainInputDispatcher));
            (void)uc_mem_read(MTK, mainApi + 112u,
                              &nextMainRenderDispatcher,
                              sizeof(nextMainRenderDispatcher));
            (void)uc_mem_read(MTK, mainApi + 52u,
                              &nextInputRegistryTarget,
                              sizeof(nextInputRegistryTarget));
        }
        if ((triggerSlot & ~1u) != 0x010183A0u)
            return;
        if (!inputDispatchSlotSeen ||
            liveInputDispatchCallback != lastLiveInputDispatchCallback ||
            mainActionCallback != lastMainActionCallback)
        {
            inputDispatchSlotSeen = true;
            lastLiveInputDispatchCallback = liveInputDispatchCallback;
            lastMainActionCallback = mainActionCallback;
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_input_registry phase=live-slot "
                        "logic=%08x module_r9=%08x main_api=%08x "
                        "live_callback=%08x registered_callback=%08x "
                        "input_module_base=%08x main_action_callback=%08x "
                        "main_input_dispatcher=%08x main_render_dispatcher=%08x "
                        "scene=%s\n",
                        activeLogic, moduleR9, mainApi,
                        liveInputDispatchCallback, inputDispatchCallback,
                        inputDispatchModuleBase, mainActionCallback,
                        nextMainInputDispatcher, nextMainRenderDispatcher,
                        g_lastSceLoadName);
                fclose(trace);
            }
        }
        if (mainInputDispatcher != nextMainInputDispatcher)
        {
            mainInputDispatcher = nextMainInputDispatcher;
            mainInputDispatcherTraceCount = 0;
        }
        if (mainRenderDispatcher != nextMainRenderDispatcher)
        {
            mainRenderDispatcher = nextMainRenderDispatcher;
            mainRenderDispatcherTraceCount = 0;
        }
        if (mainSceneStateSetter != mainActionCallback)
        {
            mainSceneStateSetter = mainActionCallback;
            mainSceneStateSetterTraceCount = 0;
        }
        if (mainApi != 0)
        {
            (void)uc_mem_read(MTK, mainApi + 1080u, &nextTargets[0],
                              sizeof(nextTargets[0]));
            (void)uc_mem_read(MTK, mainApi + 324u, &nextTargets[1],
                              sizeof(nextTargets[1]));
            (void)uc_mem_read(MTK, mainApi + 2480u, &nextTargets[3],
                              sizeof(nextTargets[3]));
            (void)uc_mem_read(MTK, mainApi + 244u, &nextTargets[4],
                              sizeof(nextTargets[4]));
            (void)uc_mem_read(MTK, mainApi + 1108u, &nextTargets[5],
                              sizeof(nextTargets[5]));
        }
        if (privateObject != 0)
        {
            (void)uc_mem_read(MTK, privateObject + 80u, &nextTargets[2],
                              sizeof(nextTargets[2]));
        }
        if (privateObject == 0)
            return;
        (void)uc_mem_read(MTK, moduleR9 + 0x2DA8u + 72u,
                          &controlCallback, sizeof(controlCallback));
        (void)uc_mem_read(MTK, moduleR9 + 0x2DA8u + 48u,
                          &nextTargets[6], sizeof(nextTargets[6]));
        if (activeLogic != tracedLogicEntry || moduleR9 != tracedModuleR9 ||
            memcmp(logicTargets, nextTargets, sizeof(logicTargets)) != 0 ||
            sceneControlCallback != controlCallback)
        {
            tracedLogicEntry = activeLogic;
            tracedModuleR9 = moduleR9;
            u32 nextSceneModuleCodeBase = activeLogic >= 0x566u
                                              ? activeLogic - 0x566u
                                              : 0;
            if (sceneModuleCodeBase != nextSceneModuleCodeBase)
            {
                actionDispatchTraceCount = 0;
                actionRouteTraceCount = 0;
                actionCallbackTraceCount = 0;
            }
            sceneModuleCodeBase = nextSceneModuleCodeBase;
            if (inputRegistryTarget != nextInputRegistryTarget)
            {
                inputRegistryTarget = nextInputRegistryTarget;
                inputRegistryTraceCount = 0;
            }
            sceneControlObject = moduleR9 + 0x2DA8u;
            sceneControlCallback = controlCallback;
            sceneControlCallbackCalls = 0;
            memcpy(logicTargets, nextTargets, sizeof(logicTargets));
            memset(logicTargetCalls, 0, sizeof(logicTargetCalls));
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_scheduler phase=logic-api-map "
                        "logic=%08x logic_local=%08x app=%u module_r9=%08x "
                        "module_code_base=%08x main_api=%08x trigger_slot=%08x "
                        "input_registry_target=%08x "
                        "private_object=%08x control_object=%08x "
                        "control_callback=%08x targets="
                        "%08x,%08x,%08x,%08x,%08x,%08x,%08x scene=%s\n",
                        activeLogic, activeLogicLocal,
                        activeLogicAppIndex >= 0
                            ? (unsigned)g_vmDlLoadedApps[activeLogicAppIndex].appId
                            : 0u,
                        moduleR9, sceneModuleCodeBase, mainApi, triggerSlot,
                        inputRegistryTarget,
                        privateObject, sceneControlObject,
                        sceneControlCallback,
                        logicTargets[0], logicTargets[1], logicTargets[2],
                        logicTargets[3], logicTargets[4], logicTargets[5],
                        logicTargets[6],
                        g_lastSceLoadName);
                fclose(trace);
            }
        }

        {
            u32 liveNodeBase = 0;
            u32 livePlayerNode = 0;
            u32 nearestActor = 0;
            u32 nearestIndex = UINT32_MAX;
            u32 nearestDistance = UINT32_MAX;
            int16_t playerX = 0;
            int16_t playerY = 0;
            int16_t nearestX = 0;
            int16_t nearestY = 0;

            (void)uc_mem_read(MTK, Global_R9 + 0x5CB0u, &liveNodeBase,
                              sizeof(liveNodeBase));
            (void)uc_mem_read(MTK, Global_R9 + 0x5CA4u, &livePlayerNode,
                              sizeof(livePlayerNode));
            if (liveNodeBase != 0 && livePlayerNode != 0)
            {
                (void)uc_mem_read(MTK, livePlayerNode + 24u, &playerX,
                                  sizeof(playerX));
                (void)uc_mem_read(MTK, livePlayerNode + 26u, &playerY,
                                  sizeof(playerY));
                for (u32 index = 0; index < 25u; ++index)
                {
                    u32 node = liveNodeBase + index * 340u;
                    u32 actorId = 0;
                    int16_t x = 0;
                    int16_t y = 0;
                    u8 kind = 0;
                    u8 occupied = 0;
                    int32_t dx = 0;
                    int32_t dy = 0;
                    u32 distance = 0;

                    (void)uc_mem_read(MTK, node + 100u, &actorId,
                                      sizeof(actorId));
                    (void)uc_mem_read(MTK, node + 315u, &kind,
                                      sizeof(kind));
                    (void)uc_mem_read(MTK, node + 319u, &occupied,
                                      sizeof(occupied));
                    if (actorId != targetActorId || kind != 2 || occupied == 0)
                        continue;
                    (void)uc_mem_read(MTK, node + 24u, &x, sizeof(x));
                    (void)uc_mem_read(MTK, node + 26u, &y, sizeof(y));
                    dx = (int32_t)x - playerX;
                    dy = (int32_t)y - playerY;
                    distance = (u32)(dx * dx + dy * dy);
                    if (nearestIndex == UINT32_MAX || distance < nearestDistance)
                    {
                        nearestActor = actorId;
                        nearestIndex = index;
                        nearestDistance = distance;
                        nearestX = x;
                        nearestY = y;
                    }
                }
                if (nearestIndex != UINT32_MAX &&
                    (!logicPositionSeen || playerX != lastLogicPlayerX ||
                     playerY != lastLogicPlayerY))
                {
                    u32 unknownOffset23852 = 0;
                    u32 sceneBattleGate = 0;
                    (void)uc_mem_read(MTK, Global_R9 + 23852u,
                                      &unknownOffset23852,
                                      sizeof(unknownOffset23852));
                    (void)uc_mem_read(MTK, Global_R9 + 23768u,
                                      &sceneBattleGate,
                                      sizeof(sceneBattleGate));
                    logicPositionSeen = true;
                    lastLogicPlayerX = playerX;
                    lastLogicPlayerY = playerY;
                    trace = fopen("logs/scene-battle-collision.log", "ab");
                    if (trace != NULL)
                    {
                        fprintf(trace,
                                "scene_battle_scheduler phase=logic-position "
                                "logic=%08x player_node=%08x player=%d,%d "
                                "nearest_actor=%u nearest_index=%u nearest=%d,%d "
                                "distance2=%u offset23852=%08x battle_gate=%08x "
                                "scene=%s\n",
                                activeLogic, livePlayerNode, (int)playerX,
                                (int)playerY, nearestActor, nearestIndex,
                                (int)nearestX, (int)nearestY, nearestDistance,
                                unknownOffset23852, sceneBattleGate,
                                g_lastSceLoadName);
                        fclose(trace);
                    }
                }
            }
        }
        return;
    }

    if (Global_R9 == 0 ||
        (pc != 0x0100F5B4 && pc != 0x010183A0 &&
         pc != 0x010184D2 && pc != 0x010184D4 &&
         pc != 0x01004CE8 && pc != 0x01004DC8))
    {
        return;
    }
    (void)uc_mem_read(MTK, Global_R9 + 0x5CB0u, &nodeBase, sizeof(nodeBase));
    (void)uc_mem_read(MTK, Global_R9 + 0x5CA4u, &playerNode, sizeof(playerNode));
    (void)uc_mem_read(MTK, Global_R9 + 0x54ACu, &engine, sizeof(engine));
    if (nodeBase != lastNodeBase)
    {
        lastNodeBase = nodeBase;
        triggerEntrySequence = 0;
        logicPositionSeen = false;
        entrySeen = false;
        memset(beforeSeen, 0, sizeof(beforeSeen));
        memset(afterSeen, 0, sizeof(afterSeen));
        apiSlotLogged = false;
    }

    if (pc == 0x010183A0 && triggerEntrySequence < 2u)
    {
        u32 lr = 0;
        u32 args[4] = {0, 0, 0, 0};
        u32 caller = 0;
        u32 callerBase = 0;
        u32 callerLocal = UINT32_MAX;
        u16 callerAppId = 0;
        const char *callerModule = "unknown";
        int callerAppIndex = -1;
        u32 liveInputCallback = 0;
        u32 callbackModuleBase = 0;
        u32 callbackLocal = UINT32_MAX;
        static const u8 logicFingerprint[8] = {
            0x10u, 0xB5u, 0x84u, 0x4Cu, 0x01u, 0x21u, 0x4Cu, 0x44u};
        static const u8 actionFingerprint[8] = {
            0xFEu, 0xB5u, 0x02u, 0x1Cu, 0xEBu, 0x48u, 0x0Cu, 0x1Cu};
        u8 observedLogic[sizeof(logicFingerprint)] = {0};
        u8 observedAction[sizeof(actionFingerprint)] = {0};

        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &args[0]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &args[1]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R2, &args[2]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R3, &args[3]);
        caller = lr & ~1u;
        (void)uc_mem_read(MTK, Global_R9 + 0x5D28u,
                          &liveInputCallback, sizeof(liveInputCallback));
        if (liveInputCallback >= 0x8A8u)
        {
            callbackModuleBase = (liveInputCallback & ~1u) - 0x8A8u;
            if (uc_mem_read(MTK, callbackModuleBase + 0x604u,
                            observedLogic, sizeof(observedLogic)) == UC_ERR_OK &&
                uc_mem_read(MTK, callbackModuleBase + 0x8A8u,
                            observedAction, sizeof(observedAction)) == UC_ERR_OK &&
                memcmp(observedLogic, logicFingerprint,
                       sizeof(logicFingerprint)) == 0 &&
                memcmp(observedAction, actionFingerprint,
                       sizeof(actionFingerprint)) == 0)
            {
                bool retained = false;

                callbackLocal = (liveInputCallback & ~1u) - callbackModuleBase;
                inputDispatchModuleBase = callbackModuleBase;
                inputDispatchCallback = liveInputCallback;
                g_vmTraceMmGameInputCodeBase = callbackModuleBase;
                for (u32 retainedIndex = 0;
                     retainedIndex < mySizeOf(retainedInputDispatchCallbacks);
                     ++retainedIndex)
                {
                    if (retainedInputDispatchCallbacks[retainedIndex] ==
                        (liveInputCallback & ~1u))
                    {
                        retainedInputDispatchBases[retainedIndex] =
                            callbackModuleBase;
                        retained = true;
                        break;
                    }
                }
                if (!retained)
                {
                    u32 retainedIndex = retainedInputDispatchNext++ %
                                        mySizeOf(retainedInputDispatchCallbacks);

                    retainedInputDispatchCallbacks[retainedIndex] =
                        liveInputCallback & ~1u;
                    retainedInputDispatchBases[retainedIndex] =
                        callbackModuleBase;
                }
            }
            else
            {
                callbackModuleBase = 0;
            }
        }
        if (caller >= Program_ROM_Address &&
            caller < Program_ROM_Address + Program_ROM_Mapped_Size)
        {
            callerModule = "JianghuOL.CBE";
            callerBase = Program_ROM_Address;
            callerLocal = caller - callerBase;
        }
        else
        {
            callerAppIndex = vm_dl_find_loaded_index_by_pc(caller);
            if (callerAppIndex >= 0)
            {
                callerModule = "loaded-cbm";
                callerBase = g_vmDlLoadedApps[callerAppIndex].buffer;
                callerLocal = caller - callerBase;
                callerAppId = g_vmDlLoadedApps[callerAppIndex].appId;
            }
        }
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            ++triggerEntrySequence;
            fprintf(trace,
                    "scene_battle_trigger phase=entry sequence=%u pc=%08x "
                    "lr=%08x caller=%08x caller_module=%s caller_app=%u "
                    "caller_base=%08x caller_local=%08x args="
                    "%08x,%08x,%08x,%08x scene=%s screen=%08x "
                    "screen_this=%08x screen_module=%08x active_logic=%08x "
                    "logic_base=%08x caller_logic_local=%08x node_base=%08x "
                    "player_node=%08x input_callback=%08x "
                    "input_callback_base=%08x input_callback_local=%08x\n",
                    triggerEntrySequence, pc, lr, caller, callerModule,
                    (unsigned)callerAppId, callerBase, callerLocal,
                    args[0], args[1], args[2], args[3], g_lastSceLoadName,
                    vmAddedScreen, g_currentScreenThis,
                    g_currentScreenModuleBase, activeLogic,
                    sceneModuleCodeBase,
                    sceneModuleCodeBase != 0 && caller >= sceneModuleCodeBase
                        ? caller - sceneModuleCodeBase
                        : UINT32_MAX,
                    nodeBase, playerNode, liveInputCallback, callbackModuleBase,
                    callbackLocal);
            fclose(trace);
        }
    }

    if (pc == 0x0100F5B4)
    {
        u32 sp = 0;
        u32 node = 0;
        u32 actorId = 0;
        u32 collisionFn = 0;
        u32 actorResourcePtr = 0;
        u32 worldX = 0;
        u32 worldY = 0;
        int16_t x = 0;
        int16_t y = 0;
        u8 tail[14];
        char actorResource[96];

        (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
        if (sp == 0 ||
            uc_mem_read(MTK, sp + 0x2Cu, &node, sizeof(node)) != UC_ERR_OK ||
            node == 0 ||
            uc_mem_read(MTK, node + 100u, &actorId, sizeof(actorId)) != UC_ERR_OK ||
            actorId != targetActorId)
        {
            return;
        }
        memset(tail, 0, sizeof(tail));
        (void)uc_mem_read(MTK, node + 24u, &x, sizeof(x));
        (void)uc_mem_read(MTK, node + 26u, &y, sizeof(y));
        (void)uc_mem_read(MTK, node + 64u, &collisionFn, sizeof(collisionFn));
        (void)uc_mem_read(MTK, node + 240u, &worldX, sizeof(worldX));
        (void)uc_mem_read(MTK, node + 244u, &worldY, sizeof(worldY));
        (void)uc_mem_read(MTK, node + 248u, &actorResourcePtr,
                          sizeof(actorResourcePtr));
        (void)uc_mem_read(MTK, node + 314u, tail, sizeof(tail));
        vm_trace_read_guest_string(actorResourcePtr, actorResource,
                                   sizeof(actorResource));
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_node phase=parse-complete pc=%08x actor=%u "
                    "node=%08x pos=%d,%d world=%d,%d collision=%08x "
                    "resource_ptr=%08x resource=%s tail314="
                    "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                    pc, actorId, node, (int)x, (int)y, (int)(int32_t)worldX,
                    (int)(int32_t)worldY, collisionFn, actorResourcePtr,
                    actorResource,
                    tail[0], tail[1], tail[2], tail[3], tail[4], tail[5],
                    tail[6], tail[7], tail[8], tail[9], tail[10], tail[11],
                    tail[12], tail[13]);
            fclose(trace);
        }
        if (!apiSlotLogged)
        {
            u32 apiTable = 0;
            u32 triggerSlot = UINT32_MAX;

            (void)uc_mem_read(MTK, Global_R9 + 8276u, &apiTable,
                              sizeof(apiTable));
            if (apiTable != 0)
            {
                for (u32 offset = 0; offset < 4096u; offset += 4u)
                {
                    u32 candidate = 0;
                    if (uc_mem_read(MTK, apiTable + offset, &candidate,
                                    sizeof(candidate)) != UC_ERR_OK)
                    {
                        break;
                    }
                    if ((candidate & ~1u) == 0x010183A0u)
                    {
                        triggerSlot = offset;
                        break;
                    }
                }
            }
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_scheduler phase=api-slot actor=%u "
                        "api=%08x trigger_slot=%u trigger_found=%u\n",
                        actorId, apiTable,
                        triggerSlot == UINT32_MAX ? 0u : triggerSlot,
                        triggerSlot == UINT32_MAX ? 0u : 1u);
                fclose(trace);
            }
            apiSlotLogged = true;
        }
        printf("[info][scene] scene_battle_node actor=%u node=%08x pos=%d,%d "
               "kind=%u occupied=%u collision=%08x resource=%s\n",
               actorId, node, (int)x, (int)y, tail[1], tail[5], collisionFn,
               actorResource);
        return;
    }

    if (nodeBase == 0 || playerNode == 0)
        return;
    if (pc == 0x01004CE8)
    {
        u32 args[4] = {0, 0, 0, 0};
        u32 lr = 0;
        u32 matchedNode = 0;
        u32 matchedActor = 0;
        u32 matchedIndex = UINT32_MAX;
        int16_t playerX = 0;
        int16_t playerY = 0;
        int16_t nodeX = 0;
        int16_t nodeY = 0;

        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &args[0]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &args[1]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R2, &args[2]);
        (void)uc_reg_read(MTK, UC_ARM_REG_R3, &args[3]);
        (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
        for (u32 index = 0; index < 25u; ++index)
        {
            u32 node = nodeBase + index * 340u;
            u8 kind = 0;
            u8 occupied = 0;

            if (args[0] != node && args[1] != node)
                continue;
            (void)uc_mem_read(MTK, node + 315u, &kind, sizeof(kind));
            (void)uc_mem_read(MTK, node + 319u, &occupied, sizeof(occupied));
            if (kind != 2 || occupied == 0)
                continue;
            matchedNode = node;
            matchedIndex = index;
            (void)uc_mem_read(MTK, node + 100u, &matchedActor,
                              sizeof(matchedActor));
            break;
        }
        if (matchedNode == 0)
            return;
        (void)uc_mem_read(MTK, playerNode + 24u, &playerX, sizeof(playerX));
        (void)uc_mem_read(MTK, playerNode + 26u, &playerY, sizeof(playerY));
        (void)uc_mem_read(MTK, matchedNode + 24u, &nodeX, sizeof(nodeX));
        (void)uc_mem_read(MTK, matchedNode + 26u, &nodeY, sizeof(nodeY));
        collisionPending = true;
        collisionPendingActor = matchedActor;
        collisionPendingIndex = matchedIndex;
        collisionPendingLr = lr;
        collisionPendingNode = matchedNode;
        collisionPendingPlayerX = playerX;
        collisionPendingPlayerY = playerY;
        collisionPendingNodeX = nodeX;
        collisionPendingNodeY = nodeY;
        ++collisionSequence;
        if (collisionTraceCount < 128u)
        {
            ++collisionTraceCount;
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_collision phase=map-collision-entry "
                        "sequence=%u pc=%08x lr=%08x caller_local=%08x "
                        "actor=%u index=%u node=%08x player=%d,%d node_pos=%d,%d "
                        "args=%08x,%08x,%08x,%08x scene=%s\n",
                        collisionSequence, pc, lr,
                        sceneModuleCodeBase != 0 && (lr & ~1u) >= sceneModuleCodeBase
                            ? (lr & ~1u) - sceneModuleCodeBase
                            : UINT32_MAX,
                        matchedActor, matchedIndex, matchedNode, (int)playerX,
                        (int)playerY, (int)nodeX, (int)nodeY, args[0], args[1],
                        args[2], args[3], g_lastSceLoadName);
                fclose(trace);
            }
        }
        return;
    }
    if (pc == 0x01004DC8)
    {
        u32 result = 0;

        if (!collisionPending)
            return;
        (void)uc_reg_read(MTK, UC_ARM_REG_R0, &result);
        if (collisionTraceCount < 128u)
        {
            ++collisionTraceCount;
            trace = fopen("logs/scene-battle-collision.log", "ab");
            if (trace != NULL)
            {
                fprintf(trace,
                        "scene_battle_collision phase=map-collision-return "
                        "sequence=%u pc=%08x lr=%08x actor=%u index=%u "
                        "node=%08x player=%d,%d node_pos=%d,%d result=%u scene=%s\n",
                        collisionSequence, pc, collisionPendingLr,
                        collisionPendingActor, collisionPendingIndex,
                        collisionPendingNode, (int)collisionPendingPlayerX,
                        (int)collisionPendingPlayerY, (int)collisionPendingNodeX,
                        (int)collisionPendingNodeY, result != 0 ? 1u : 0u,
                        g_lastSceLoadName);
                fclose(trace);
            }
        }
        collisionPending = false;
        return;
    }
    if (pc == 0x010183A0)
    {
        u32 nearestNode = 0;
        u32 nearestIndex = UINT32_MAX;
        u32 nearestDistance = UINT32_MAX;
        u32 callback = 0;
        u32 cooldown = 0;
        u32 pending = 0;
        u32 uiState = 0;
        int16_t playerX = 0;
        int16_t playerY = 0;
        u8 busy = 0;
        u8 modalA = 0;
        u8 modalB = 0;
        u8 engine978 = 0;
        u8 engine921 = 0;
        u8 engine988Positive = 0;
        u32 gateFingerprint = 0;

        (void)uc_mem_read(MTK, playerNode + 24u, &playerX, sizeof(playerX));
        (void)uc_mem_read(MTK, playerNode + 26u, &playerY, sizeof(playerY));
        for (u32 index = 0; index < 25u; ++index)
        {
            u32 node = nodeBase + index * 340u;
            u32 actorId = 0;
            int16_t x = 0;
            int16_t y = 0;
            u8 kind = 0;
            u8 occupied = 0;
            int32_t dx = 0;
            int32_t dy = 0;
            u32 distance = 0;

            (void)uc_mem_read(MTK, node + 100u, &actorId, sizeof(actorId));
            (void)uc_mem_read(MTK, node + 315u, &kind, sizeof(kind));
            (void)uc_mem_read(MTK, node + 319u, &occupied, sizeof(occupied));
            if (actorId != targetActorId || kind != 2 || occupied == 0)
                continue;
            (void)uc_mem_read(MTK, node + 24u, &x, sizeof(x));
            (void)uc_mem_read(MTK, node + 26u, &y, sizeof(y));
            dx = (int32_t)x - playerX;
            dy = (int32_t)y - playerY;
            distance = (u32)(dx * dx + dy * dy);
            if (nearestNode == 0 || distance < nearestDistance)
            {
                nearestNode = node;
                nearestIndex = index;
                nearestDistance = distance;
            }
        }
        if (nearestNode == 0)
            return;
        (void)uc_mem_read(MTK, Global_R9 + 23768u, &callback, sizeof(callback));
        (void)uc_mem_read(MTK, Global_R9 + 23944u, &cooldown, sizeof(cooldown));
        (void)uc_mem_read(MTK, Global_R9 + 23936u, &pending, sizeof(pending));
        (void)uc_mem_read(MTK, Global_R9 + 23876u, &uiState, sizeof(uiState));
        (void)uc_mem_read(MTK, Global_R9 + 24836u, &busy, sizeof(busy));
        (void)uc_mem_read(MTK, Global_R9 + 38013u, &modalA, sizeof(modalA));
        (void)uc_mem_read(MTK, Global_R9 + 38014u, &modalB, sizeof(modalB));
        if (engine != 0)
        {
            int32_t engine988 = 0;
            (void)uc_mem_read(MTK, engine + 978u, &engine978, sizeof(engine978));
            (void)uc_mem_read(MTK, engine + 921u, &engine921, sizeof(engine921));
            (void)uc_mem_read(MTK, engine + 988u, &engine988, sizeof(engine988));
            engine988Positive = engine988 > 0 ? 1u : 0u;
        }
        gateFingerprint = (callback != 0 ? 1u : 0u) |
                          (cooldown != 0 ? 2u : 0u) |
                          (pending != 0 ? 4u : 0u) |
                          (uiState == 1 ? 8u : 0u) |
                          ((u32)busy << 4) | ((u32)modalA << 5) |
                          ((u32)modalB << 6) | ((u32)engine978 << 7) |
                          ((u32)engine921 << 8) |
                          ((u32)engine988Positive << 9);
        if (entrySeen && playerX == lastEntryX && playerY == lastEntryY &&
            gateFingerprint == lastEntryGates)
        {
            return;
        }
        entrySeen = true;
        lastEntryX = playerX;
        lastEntryY = playerY;
        lastEntryGates = gateFingerprint;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_collision phase=scan-entry pc=%08x actor=%u "
                    "player=%d,%d nearest_index=%u nearest_node=%08x "
                    "distance2=%u callback=%08x gates=%08x cooldown=%u "
                    "pending=%u ui=%u busy=%u modal=%u,%u engine=%u,%u,%u\n",
                    pc, targetActorId, (int)playerX, (int)playerY,
                    nearestIndex, nearestNode, nearestDistance, callback,
                    gateFingerprint, cooldown, pending, uiState, busy, modalA,
                    modalB, engine978, engine921, engine988Positive);
            fclose(trace);
        }
        return;
    }

    {
        u32 index = 0;
        u32 node = 0;
        u32 actorId = 0;
        u32 collisionFn = 0;
        u32 result = 0;
        int16_t playerX = 0;
        int16_t playerY = 0;
        int16_t nodeX = 0;
        int16_t nodeY = 0;
        bool shouldLog = false;

        (void)uc_reg_read(MTK, UC_ARM_REG_R4, &index);
        if (index >= 25u)
            return;
        node = nodeBase + index * 340u;
        (void)uc_mem_read(MTK, node + 100u, &actorId, sizeof(actorId));
        if (actorId != targetActorId)
            return;
        (void)uc_mem_read(MTK, playerNode + 24u, &playerX, sizeof(playerX));
        (void)uc_mem_read(MTK, playerNode + 26u, &playerY, sizeof(playerY));
        (void)uc_mem_read(MTK, node + 24u, &nodeX, sizeof(nodeX));
        (void)uc_mem_read(MTK, node + 26u, &nodeY, sizeof(nodeY));
        if (pc == 0x010184D2)
        {
            (void)uc_reg_read(MTK, UC_ARM_REG_R7, &collisionFn);
            shouldLog = !beforeSeen[index] || lastBeforeX[index] != playerX ||
                        lastBeforeY[index] != playerY;
            beforeSeen[index] = true;
            lastBeforeX[index] = playerX;
            lastBeforeY[index] = playerY;
        }
        else
        {
            (void)uc_reg_read(MTK, UC_ARM_REG_R0, &result);
            shouldLog = !afterSeen[index] || lastAfterX[index] != playerX ||
                        lastAfterY[index] != playerY ||
                        lastAfterResult[index] != (u8)(result != 0);
            afterSeen[index] = true;
            lastAfterX[index] = playerX;
            lastAfterY[index] = playerY;
            lastAfterResult[index] = (u8)(result != 0);
            (void)uc_mem_read(MTK, node + 64u, &collisionFn,
                              sizeof(collisionFn));
        }
        if (!shouldLog)
            return;
        trace = fopen("logs/scene-battle-collision.log", "ab");
        if (trace != NULL)
        {
            fprintf(trace,
                    "scene_battle_collision phase=%s pc=%08x actor=%u index=%u "
                    "player=%d,%d node=%d,%d collision=%08x result=%u\n",
                    pc == 0x010184D2 ? "before-callback" : "after-callback",
                    pc, actorId, index, (int)playerX, (int)playerY,
                    (int)nodeX, (int)nodeY, collisionFn,
                    pc == 0x010184D4 ? (result != 0 ? 1u : 0u) : UINT32_MAX);
            fclose(trace);
        }
    }
}

static void vm_trace_scene_battle_uplink(u32 dataPtr, u32 dataLen, u32 lr,
                                         u32 sp, u32 r4, u32 r5, u32 r6,
                                         u32 r7)
{
    static u32 sequence = 0;
    const char *enabled = NULL;
    u8 packet[512];
    u8 kind = 0;
    u8 subtype = 0;
    u32 packetLen = 0;
    char packetHex[sizeof(packet) * 2u + 1u];
    static const char hex[] = "0123456789ABCDEF";
    u32 stackWords[24];
    char callers[768];
    size_t used = 0;
    FILE *trace = NULL;

    enabled = getenv("CBE_TRACE_SCENE_BATTLE_COLLISION");
    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0 ||
        MTK == NULL || dataPtr == 0 || dataLen == 0)
    {
        return;
    }
    packetLen = dataLen < sizeof(packet) ? dataLen : sizeof(packet);
    if (uc_mem_read(MTK, dataPtr, packet, packetLen) != UC_ERR_OK ||
        packetLen < 8 || packet[0] != 'W' || packet[1] != 'T')
    {
        return;
    }
    kind = packet[5];
    subtype = packet[6];
    if (kind != 4 || subtype != 1)
    {
        return;
    }
    for (u32 index = 0; index < packetLen; ++index)
    {
        packetHex[index * 2u] = hex[packet[index] >> 4];
        packetHex[index * 2u + 1u] = hex[packet[index] & 0x0fu];
    }
    packetHex[packetLen * 2u] = 0;

    memset(stackWords, 0, sizeof(stackWords));
    if (sp != 0)
        (void)uc_mem_read(MTK, sp, stackWords, sizeof(stackWords));
    callers[0] = 0;
    for (u32 index = 0; index < mySizeOf(stackWords); ++index)
    {
        u32 candidate = stackWords[index] & ~1u;
        u32 base = 0;
        u32 local = UINT32_MAX;
        u16 appId = 0;
        const char *module = NULL;
        int appIndex = -1;
        int written = 0;

        if (candidate >= Program_ROM_Address &&
            candidate < Program_ROM_Address + Program_ROM_Mapped_Size)
        {
            module = "JianghuOL.CBE";
            base = Program_ROM_Address;
            local = candidate - base;
        }
        else
        {
            appIndex = vm_dl_find_loaded_index_by_pc(candidate);
            if (appIndex >= 0)
            {
                module = "loaded-cbm";
                base = g_vmDlLoadedApps[appIndex].buffer;
                local = candidate - base;
                appId = g_vmDlLoadedApps[appIndex].appId;
            }
        }
        if (module == NULL || used >= sizeof(callers))
            continue;
        written = snprintf(callers + used, sizeof(callers) - used,
                           "%s%u:%08x:%s:%u:%08x",
                           used == 0 ? "" : ";", index, candidate, module,
                           (unsigned)appId, local);
        if (written < 0 || (size_t)written >= sizeof(callers) - used)
        {
            used = sizeof(callers) - 1u;
            callers[used] = 0;
            break;
        }
        used += (size_t)written;
    }

    trace = fopen("logs/scene-battle-collision.log", "ab");
    if (trace == NULL)
        return;
    ++sequence;
    fprintf(trace,
            "scene_battle_uplink phase=wt-4-1 sequence=%u len=%u data=%08x hex=%s "
            "network_lr=%08x network_sp=%08x regs=%08x,%08x,%08x,%08x "
            "scene=%s screen=%08x screen_this=%08x screen_module=%08x "
            "stack_callers=[%s]\n",
            sequence, dataLen, dataPtr, packetHex, lr, sp, r4, r5, r6, r7,
            g_lastSceLoadName, vmAddedScreen, g_currentScreenThis,
            g_currentScreenModuleBase, callers[0] ? callers : "-");
    fclose(trace);
}

static void vm_trace_actor_scene_capacity_table(FILE *trace, const char *name,
                                                u32 tablePtrAddress,
                                                u32 rowCount)
{
    u32 base = 0;
    u32 blocked = 0;

    if (trace == NULL || name == NULL || MTK == NULL || tablePtrAddress == 0 ||
        uc_mem_read(MTK, tablePtrAddress, &base, sizeof(base)) != UC_ERR_OK ||
        base == 0)
    {
        if (trace != NULL)
            fprintf(trace, " table=%s ptr_addr=%08x base=%08x unreadable\n",
                    name ? name : "-", tablePtrAddress, base);
        return;
    }

    if (rowCount > 25u)
        rowCount = 25u;
    fprintf(trace, " table=%s ptr_addr=%08x base=%08x capacity=%u rows=[",
            name, tablePtrAddress, base, rowCount);
    for (u32 index = 0; index < rowCount; ++index)
    {
        u32 node = base + index * 340u;
        u32 actorId = 0;
        int16_t x = 0;
        int16_t y = 0;
        u8 kind = 0;
        u8 occupied = 0;

        (void)uc_mem_read(MTK, node + 100u, &actorId, sizeof(actorId));
        (void)uc_mem_read(MTK, node + 24u, &x, sizeof(x));
        (void)uc_mem_read(MTK, node + 26u, &y, sizeof(y));
        (void)uc_mem_read(MTK, node + 315u, &kind, sizeof(kind));
        (void)uc_mem_read(MTK, node + 319u, &occupied, sizeof(occupied));
        if (occupied != 0 || kind == 2)
            ++blocked;
        fprintf(trace, "%s%u:o%u:k%u:id%d@%d,%d",
                index == 0 ? "" : ";", index, occupied, kind,
                (int)(int32_t)actorId, (int)x, (int)y);
    }
    fprintf(trace, "] blocked=%u free=%u\n", blocked, rowCount - blocked);
}

/* The current mmGame base is not reported by the incomplete load-manager
 * table.  It is usable for trace attribution only while the exact live
 * sub_604/sub_8A8 fingerprint that established it still exists. */
static bool vm_trace_mmgame_input_base_is_live(u32 base)
{
    static const u8 logicFingerprint[8] = {
        0x10u, 0xB5u, 0x84u, 0x4Cu, 0x01u, 0x21u, 0x4Cu, 0x44u};
    static const u8 actionFingerprint[8] = {
        0xFEu, 0xB5u, 0x02u, 0x1Cu, 0xEBu, 0x48u, 0x0Cu, 0x1Cu};
    u8 observedLogic[sizeof(logicFingerprint)] = {0};
    u8 observedAction[sizeof(actionFingerprint)] = {0};

    if (MTK == NULL ||
        base < VM_Memory_Pool_ADDRESS ||
        base > VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE - 0x8B0u)
    {
        return false;
    }
    return uc_mem_read(MTK, base + 0x604u, observedLogic,
                       sizeof(observedLogic)) == UC_ERR_OK &&
           uc_mem_read(MTK, base + 0x8A8u, observedAction,
                       sizeof(observedAction)) == UC_ERR_OK &&
           memcmp(observedLogic, logicFingerprint,
                  sizeof(logicFingerprint)) == 0 &&
           memcmp(observedAction, actionFingerprint,
                  sizeof(actionFingerprint)) == 0;
}

/* Format a guest return address without changing its owner or execution.
 * The scene-array probe uses this only to compare the lifecycle caller that
 * reaches init against the caller that reaches the real free routine. */
static void vm_trace_format_guest_callsite(char *out, size_t outSize, u32 address)
{
    u32 candidate = address & ~1u;
    u32 base = 0;
    u32 local = UINT32_MAX;
    u16 appId = 0;
    const char *module = NULL;
    int appIndex = -1;

    if (out == NULL || outSize == 0)
        return;
    out[0] = 0;
    if (candidate >= Program_ROM_Address &&
        candidate < Program_ROM_Address + Program_ROM_Mapped_Size)
    {
        module = "JianghuOL.CBE";
        base = Program_ROM_Address;
        local = candidate - base;
    }
    else
    {
        /* The installed mmGame code is 48,858 bytes including its 0x9A-byte
         * container header.  Its live payload is page-aligned by the VM, so
         * a 0xC000 window admits every real code offset while excluding the
         * next pool allocation. */
        if (g_vmTraceMmGameInputCodeBase != 0 &&
            candidate >= g_vmTraceMmGameInputCodeBase &&
            candidate - g_vmTraceMmGameInputCodeBase < 0xC000u &&
            vm_trace_mmgame_input_base_is_live(g_vmTraceMmGameInputCodeBase))
        {
            module = "mmGameMstarWqvga.cbm";
            base = g_vmTraceMmGameInputCodeBase;
            local = candidate - base;
        }
        else
        {
            appIndex = vm_dl_find_loaded_index_by_pc(candidate);
            if (appIndex >= 0)
            {
                module = "loaded-cbm";
                base = g_vmDlLoadedApps[appIndex].buffer;
                local = candidate - base;
                appId = g_vmDlLoadedApps[appIndex].appId;
            }
        }
    }
    if (module == NULL)
    {
        snprintf(out, outSize, "unmapped:%08x", candidate);
        return;
    }
    snprintf(out, outSize, "%s:%u:%08x:%08x", module,
             (unsigned)appId, base, local);
}

/*
 * Read-only probe for the Actor motion-descriptor allocation failure at
 * JianghuOL.CBE:0x0100DA4E.  The parser stores a stream-provided count at
 * descriptor +0x10, then allocates one scene node per entry.  The ordinary
 * path uses a fixed 25-slot scene table.  Resources whose names begin with
 * `b` use the separately allocated battle/background actor array, whose
 * capacity comes from the first descriptor that allocated it.
 *
 * Enable with CBE_TRACE_ACTOR_SCENE_CAPACITY=1.  The default cap is 128
 * records and can be lowered with CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX.  This
 * probe only reads registers, stack arguments, descriptors and node tables;
 * it never changes guest state or allocation results.
 */
static void vm_trace_actor_scene_capacity_pc(u32 pc)
{
    static u32 traceCount = 0;
    static u32 battleArrayCapacity = 0;
    const char *enabled = NULL;
    const char *maxText = NULL;
    const char *phase = NULL;
    u32 maxRecords = 128u;
    u32 r0 = 0;
    u32 r1 = 0;
    u32 r2 = 0;
    u32 r3 = 0;
    u32 r4 = 0;
    u32 r5 = 0;
    u32 r7 = 0;
    u32 lr = 0;
    u32 sp = 0;
    u32 descriptor = 0;
    u32 resourcePtr = 0;
    u32 childData = 0;
    u32 loopIndex = UINT32_MAX;
    int forceNullResult = 0;
    int16_t descriptorCount = 0;
    int16_t pendingCount = 0;
    char resource[128];
    char assetName[128];
    FILE *trace = NULL;

    if (MTK == NULL ||
        (pc != 0x0100D6E2 && pc != 0x0100D872 && pc != 0x0100D942 &&
         pc != 0x0100D97E && pc != 0x0100DA14 && pc != 0x0100DA42 &&
         pc != 0x0100DA4E && pc != 0x01012FE6 && pc != 0x01017E14 &&
         pc != 0x01017E34 && pc != 0x01017E3C && pc != 0x01017E54))
    {
        return;
    }
    /* Retain the allocator's observed capacity even when ordinary tracing is
     * off, so the one mandatory null-result snapshot has a valid table size. */
    if (pc == 0x01017E34)
    {
        (void)uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
        battleArrayCapacity = r4 / 340u;
    }
    else if (pc == 0x01017E54)
    {
        battleArrayCapacity = 0;
    }
    if (pc == 0x0100DA4E)
    {
        (void)uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
        forceNullResult = r1 == 0;
    }
    enabled = getenv("CBE_TRACE_ACTOR_SCENE_CAPACITY");
    if (enabled == NULL || enabled[0] == 0 || strcmp(enabled, "0") == 0 ||
        strcmp(enabled, "off") == 0 || strcmp(enabled, "false") == 0)
    {
        if (!forceNullResult)
            return;
    }
    maxText = getenv("CBE_TRACE_ACTOR_SCENE_CAPACITY_MAX");
    if (maxText != NULL && maxText[0] != 0)
    {
        char *end = NULL;
        unsigned long parsed = strtoul(maxText, &end, 0);
        if (end != maxText && end != NULL && *end == 0 && parsed > 0 &&
            parsed <= 1024u)
        {
            maxRecords = (u32)parsed;
        }
    }
    (void)uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
    (void)uc_reg_read(MTK, UC_ARM_REG_R1, &r1);
    (void)uc_reg_read(MTK, UC_ARM_REG_R2, &r2);
    (void)uc_reg_read(MTK, UC_ARM_REG_R3, &r3);
    (void)uc_reg_read(MTK, UC_ARM_REG_R4, &r4);
    (void)uc_reg_read(MTK, UC_ARM_REG_R5, &r5);
    (void)uc_reg_read(MTK, UC_ARM_REG_R7, &r7);
    (void)uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    (void)uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    if (pc == 0x01012FE6 || pc == 0x01017E14 || pc == 0x01017E34 ||
        pc == 0x01017E3C || pc == 0x01017E54)
    {
        u32 arrayPtr = 0;
        u8 guestCount = 0;
        u32 stackWords[8] = {0};
        char lrCallsite[64];
        char stackCallsites[8][64];

        (void)uc_mem_read(MTK, Global_R9 + 0x5CB4u, &arrayPtr,
                          sizeof(arrayPtr));
        (void)uc_mem_read(MTK, Global_R9 + 0x5C73u, &guestCount,
                          sizeof(guestCount));
        if (sp != 0)
            (void)uc_mem_read(MTK, sp, stackWords, sizeof(stackWords));
        vm_trace_format_guest_callsite(lrCallsite, sizeof(lrCallsite), lr);
        for (u32 index = 0; index < mySizeOf(stackWords); ++index)
        {
            vm_trace_format_guest_callsite(stackCallsites[index],
                                           sizeof(stackCallsites[index]),
                                           stackWords[index]);
        }
        if (pc == 0x01017E34)
            battleArrayCapacity = r4 / 340u;
        if (pc == 0x01012FE6)
            phase = "scene-init-reset-count";
        else if (pc == 0x01017E14)
            phase = "battle-array-alloc-entry";
        else if (pc == 0x01017E34)
            phase = "battle-array-alloc-before-store";
        else if (pc == 0x01017E3C)
            phase = "battle-array-free-entry";
        else
            phase = "battle-array-free-before-null";
        if (traceCount >= maxRecords)
        {
            if (pc == 0x01017E54)
                battleArrayCapacity = 0;
            return;
        }
        trace = fopen("logs/actor-scene-node-capacity.log", "ab");
        if (trace == NULL)
            return;
        ++traceCount;
        fprintf(trace,
                "actor_scene_capacity seq=%u phase=%s pc=%08x lr=%08x "
                "array_ptr=%08x guest_count=%u observed_capacity=%u "
                "request_or_result=%08x bytes=%u lr_callsite=%s "
                "stack_callsites=[%s;%s;%s;%s;%s;%s;%s;%s]\n",
                traceCount, phase, pc, lr, arrayPtr, (unsigned)guestCount,
                battleArrayCapacity, r0, r4, lrCallsite,
                stackCallsites[0], stackCallsites[1], stackCallsites[2],
                stackCallsites[3], stackCallsites[4], stackCallsites[5],
                stackCallsites[6], stackCallsites[7]);
        fclose(trace);
        if (pc == 0x01017E54)
            battleArrayCapacity = 0;
        return;
    }
    /* Login and the source scene may consume the ordinary trace budget.
     * Never lose the one null result which immediately precedes the crash. */
    if (traceCount >= maxRecords && !(pc == 0x0100DA4E && r1 == 0))
        return;
    descriptor = pc == 0x0100D6E2 ? r0 : r5;
    if (sp != 0)
    {
        u32 resourceArgAddress = pc == 0x0100D6E2 ? sp : sp + 0x70u;
        (void)uc_mem_read(MTK, resourceArgAddress, &resourcePtr,
                          sizeof(resourcePtr));
        if (pc >= 0x0100D97E)
            (void)uc_mem_read(MTK, sp + 0x38u, &loopIndex, sizeof(loopIndex));
    }
    if (descriptor != 0)
        (void)uc_mem_read(MTK, descriptor + 0x10u, &descriptorCount,
                          sizeof(descriptorCount));
    if (pc == 0x0100D872)
        pendingCount = (int16_t)(r0 & 0xffffu);
    if ((pc == 0x0100DA14 || pc == 0x0100DA42 || pc == 0x0100DA4E) &&
        r7 != 0)
    {
        (void)uc_mem_read(MTK, r7 + 0x2cu, &childData, sizeof(childData));
    }
    vm_trace_read_guest_string(resourcePtr, resource, sizeof(resource));
    if (pc == 0x0100D942)
        vm_trace_read_guest_string(r7, assetName, sizeof(assetName));
    else
        snprintf(assetName, sizeof(assetName), "-");

    if (pc == 0x0100D6E2)
        phase = "descriptor-entry";
    else if (pc == 0x0100D872)
        phase = "descriptor-count-before-store";
    else if (pc == 0x0100D942)
        phase = "asset-name";
    else if (pc == 0x0100D97E)
        phase = "child-loop-entry";
    else if (pc == 0x0100DA14)
        phase = "alloc-table-b-before-call";
    else if (pc == 0x0100DA42)
        phase = "alloc-table-a-before-call";
    else
        phase = "allocation-result-before-deref";

    trace = fopen("logs/actor-scene-node-capacity.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "actor_scene_capacity seq=%u phase=%s pc=%08x lr=%08x map=%s "
            "resource_ptr=%08x resource=%s descriptor=%08x count=%d "
            "pending_count=%d index=%u child_record=%08x child_data=%08x "
            "r0=%08x r1=%08x r2=%08x r3=%08x r5=%08x r7=%08x sp=%08x "
            "asset=%s\n",
            traceCount, phase, pc, lr, g_lastSceLoadName[0] ? g_lastSceLoadName : "-",
            resourcePtr, resource, descriptor, (int)descriptorCount,
            (int)pendingCount, loopIndex, r7, childData,
            r0, r1, r2, r3, r5, r7, sp, assetName);
    if (pc == 0x0100DA14 || pc == 0x0100DA42 || pc == 0x0100DA4E)
    {
        vm_trace_actor_scene_capacity_table(trace, "scene", Global_R9 + 0x5CB0u,
                                            25u);
        vm_trace_actor_scene_capacity_table(trace, "battle-background",
                                            Global_R9 + 0x5CB4u,
                                            battleArrayCapacity);
    }
    fclose(trace);

    if (pc == 0x0100D6E2 || (pc == 0x0100DA4E && r1 == 0))
    {
        printf("[info][scene] actor_scene_capacity phase=%s resource=%s "
               "descriptor=%08x count=%d index=%u result=%08x lr=%08x\n",
               phase, resource, descriptor, (int)descriptorCount, loopIndex,
               r1, lr);
    }
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
#ifdef CBE_PLATFORM_ANDROID
        g_cbeLastRunStatus = (int)p;
#endif
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
                vm_trace_screen_lifecycle_order("before-init", screenFuncPtr,
                                                screenThisPtr, screenInitEntry,
                                                0);
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
                vm_trace_screen_lifecycle_order("after-init", screenFuncPtr,
                                                screenThisPtr, screenInitEntry,
                                                0);
                vm_automation_note_screen_init(screenFuncPtr, screenInitEntry,
                                               screenLogicEntry, screenRenderEntry);
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
                        vm_shop_return_forensics_log("input-dequeue", evt->event,
                                                      evt->r0, evt->r1,
                                                      screenThisPtr);
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
                                u32 eventArgValue = 0;
                                if (evt->event == VM_EVENT_KEYBOARD)
                                {
                                    u32 keyMask = vm_key_mask_from_code(evt->r0);
                                    eventType = evt->r1 ? 0 : 1;
                                    eventArg = vm_malloc_var();
                                    eventArgValue = keyMask;
                                    vm_set_var(eventArg, keyMask);
                                }
                                else
                                {
                                    eventType = evt->r0 == MR_MOUSE_UP ? 4 : (evt->r0 == MR_MOUSE_MOVE ? 5 : 3);
                                    eventArg = vm_malloc_var();
                                    eventArgValue = evt->r1;
                                    vm_set_var(eventArg, evt->r1);
                                }
                                vm_trace_scene_battle_host_input("screen-logic",
                                                                 "before", evt,
                                                                 screenThisPtr,
                                                                 screenLogicEntry,
                                                                 eventType, eventArg,
                                                                 eventArgValue,
                                                                 UC_ERR_OK);
                                uc_reg_write(MTK, UC_ARM_REG_LR, &thumbExitAddr);
                                scheduler_prepare_screen_call(screenThisPtr);
                                uc_reg_write(MTK, UC_ARM_REG_R0, &screenThisPtr);
                                uc_reg_write(MTK, UC_ARM_REG_R1, &eventType);
                                uc_reg_write(MTK, UC_ARM_REG_R2, &eventArg);
                                vm_shop_return_forensics_log("input-logic-before",
                                                              eventType,
                                                              evt->r0,
                                                              screenLogicEntry,
                                                              screenThisPtr);
                                if (g_shopReturnForensicsActive &&
                                    g_shopReturnForensicsActorQuerySeen &&
                                    g_currentScreenThis != 0 &&
                                    g_currentScreenThis != g_shopReturnForensicsCatalogScreen &&
                                    g_shopReturnForensicsReturnLogicEntry == 0 &&
                                    vm_is_pool_entry(screenLogicEntry))
                                {
                                    g_shopReturnForensicsReturnLogicEntry = screenLogicEntry;
                                    vm_shop_return_forensics_log("returned-logic-arm",
                                                                  eventType,
                                                                  evt->r0,
                                                                  screenLogicEntry,
                                                                  screenThisPtr);
                                }
                                p = vm_emu_start(screenLogicEntry, exitAddr);
                                vm_shop_return_forensics_log("input-logic-after",
                                                              eventType,
                                                              evt->r0,
                                                              screenLogicEntry,
                                                              screenThisPtr);
                                vm_trace_scene_battle_host_input("screen-logic",
                                                                 "after", evt,
                                                                 screenThisPtr,
                                                                 screenLogicEntry,
                                                                 eventType, eventArg,
                                                                 eventArgValue, p);
                                if (evt->event == VM_EVENT_KEYBOARD || evt->event == VM_EVENT_TOUCHSCREEN)
                                    vm_free_var(eventArg);
                                if (p != UC_ERR_OK)
                                {
                                    printf("SCR_Event异常:%s\n", uc_strerror(p));
                                    assert(0);
                                }
                                clearTransientInputBeforeIdle = true;
                                scheduler_note_screen_data_package(screenThisPtr);
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
                    vm_trace_screen_lifecycle_order("before-pause", screenFuncPtr,
                                                    screenThisPtr,
                                                    screenPauseEntry, 0);
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
                    vm_trace_screen_lifecycle_order("before-destroy", screenFuncPtr,
                                                    screenThisPtr,
                                                    screenDestoryEntry, 0);
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
                    vm_trace_screen_lifecycle_order("after-destroy", screenFuncPtr,
                                                    screenThisPtr,
                                                    screenDestoryEntry, 0);
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
#ifdef CBE_PLATFORM_ANDROID
    g_cbeLastRunStatus = (int)p;
#endif
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

    /*
     * Normal execution reaches PROGRAM_EXIT_ADDR through Unicorn's native
     * exit facility, and platform exports are covered by their exact manager
     * hooks below.  The only remaining generic control point is the VM log
     * no-op.  Do not run a host code callback for every CBE ROM instruction:
     * the game contains software rasterizers whose inner pixel loops must
     * remain entirely in the emulator.  Repository automation retains the
     * historical full-range callback only while it is actively running.
     */
#if defined(GDB_SERVER_SUPPORT)
    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_CODE, hookCodeCallBack,
                      0, 0, 0xFFFFFFFF);
#else
    if (g_autotestEnabled || !vm_enable_program_exit_control(MTK))
    {
        err = uc_hook_add(MTK, &hookHandle, UC_HOOK_CODE, hookCodeCallBack,
                          0, 0, 0xFFFFFFFF);
    }
    else
    {
        err = uc_hook_add(MTK, &hookHandle, UC_HOOK_CODE, hookCodeCallBack,
                          0, VM_LOG_NOOP_ADDRESS, VM_LOG_NOOP_ADDRESS);
    }
#endif
    if (err == UC_ERR_OK)
        err = add_manager_code_hooks(MTK);
    //    err = uc_hook_add(MTK, &hookHandle, UC_HOOK_BLOCK, hookBlockCallBack, 0, 0, 0xFFFFFFFF);

    /* hookRamCallBack contains only bounded forensic/automation watches.  It
     * has no normal client-platform behavior, so a player session must not
     * cross it for every map-render memory access.  Repository automation
     * and GDB retain the same read/write coverage. */
#if defined(GDB_SERVER_SUPPORT)
    uc_hook_add(MTK, &hookHandle, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                hookRamCallBack, 0, 0, 0xFFFFFFFF);
#else
    if (g_autotestEnabled)
    {
        uc_hook_add(MTK, &hookHandle, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                    hookRamCallBack, 0, 0, 0xFFFFFFFF);
    }
#endif

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
    g_cbeLastRunStatus = UC_ERR_OK;
    RunArmProgram((void *)(uintptr_t)Program_ROM_Address);
    g_cbeRunning = 0;
    return g_cbeLastRunStatus;
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
        scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
        /* The platform ABI reports that asynchronous registration was
         * accepted with a nonzero result.  Do not write the CBE-owned
         * net-manager state here; its lifecycle is driven by the queued
         * network event and the normal guest callbacks. */
        vm_set_call_result(1);
    }
    else if (idx == 1)
    {
        if (netR4)
            g_netCurrentObject = netR4;
        vm_trace_scene_battle_uplink(tmp3, tmp2, netLr, netSp, netR4,
                                     netR5, netR6, netR7);
        vm_net_mock_on_send(tmp1, tmp3, tmp2);
        vm_set_call_result(tmp2);
    }
    else if (idx == 2)
    {
        scheduler_unregister_net_channel(tmp1);
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
        bool sameActiveRequest = false;
        u32 moduleBase = 0;
        u32 guestLr = 0;
        u32 oldActiveScreen = vmAddedScreen;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        uc_reg_read(MTK, UC_ARM_REG_LR, &guestLr);
        if (idx == 2)
        {
            tmp2 = 0;
            tmp3 = 1;
        }
        sameActiveRequest = tmp1 != 0 && tmp1 == vmAddedScreen &&
                            lastAddress == 0x01018150;
        /* This is a platform screen-manager request made by the CBE.  It
         * intentionally has no packet, scene, or caller-PC policy: the guest
         * owns whether a same-screen lifecycle request is meaningful, and the
         * platform must preserve that request exactly as registered. */
        if (tmp1 != 0)
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
        vm_trace_screen_manager_decision(
            idx, guestLr, tmp1, oldActiveScreen, tmp2, tmp3,
            sameActiveRequest, tmp1 != 0);
        vm_set_call_result(0);
    }
    else if (idx == 4 || idx == 5)
    {
        u32 moduleBase = 0;
        bool replacesActiveScreen = false;
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
        if (tmp1 != 0 && oldActiveScreen != 0)
        {
            int requestedIndex = vm_screen_stack_find_related(tmp1);
            int activeIndex = vm_screen_stack_find_related(oldActiveScreen);
            replacesActiveScreen = vm_screen_add_replaces_active(
                requestedIndex, activeIndex);
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
            g_screenExitMode = vm_screen_add_exit_mode(
                replacesActiveScreen, g_screenStackCount,
                VM_SCREEN_EXIT_DESTROY, VM_SCREEN_EXIT_PAUSE);
            g_screenEnterExistingNoCallback = 0;
        }
        if (tmp1 != 0)
            g_screenRemovedWithoutNext = 0;
        vm_autotest_note("screen_mgr idx=%u type=add caller=%08x screen=%08x param=%08x flags=%u old=%08x this=%08x depth=%u replaces_active=%u exit_mode=%u\n",
                         idx, lastAddress, tmp1, tmp2, tmp3, oldActiveScreen,
                         g_currentScreenThis, g_screenStackCount,
                         replacesActiveScreen ? 1u : 0u, g_screenExitMode);
        vm_trace_screen_lifecycle_order("manager-add", tmp1, tmp2, 0,
                                        replacesActiveScreen ? 1u : 0u);
        vm_set_call_result(0);
    }
    else if (idx == 6)
    {
        u32 guestLr = 0;
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_LR, &guestLr);
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
        printf("[info][screen] screen_mgr remove requested=%08x current=%08x result=%u current_match=%u new_top=%08x module=%08x dp=%08x guest_lr=%08x caller=%08x net_depth=%d net_slot=%d host_dp=%08x guest_dp=%08x stack_depth=%u\n",
               tmp1, vmAddedScreen, tmp4, removingCurrent ? 1u : 0u, tmp3,
               tmp5, newTopDataPackage, guestLr, lastAddress,
               g_netTaskDispatchDepth, g_netTaskDispatchSlot,
               g_currentScreenDataPackage, vm_current_data_package(),
               g_screenStackCount);
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
        vm_dl_trace_loader_call("set-current-context", idx);
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
        vm_dl_trace_loader_call("set-context", idx);
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
        vm_dl_trace_loader_call("unload-current", idx);
        vm_dl_unload_loaded_app(g_vmDlCurrAppId);
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        printf("[call]vlDlUnLoadApp\n");
        vm_dl_trace_loader_call("unload-app", idx);
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_dl_unload_loaded_app((u16)tmp1);
        vm_set_call_result(0);
    }
    else if (idx == 4)
    {
        printf("[call]vmDlParseAndCopy\n");
        vm_dl_trace_loader_call("parse-and-copy", idx);
        vm_set_call_result(0xffffffffu);
    }
    else if (idx == 5)
    {
        printf("[call]vlDlLoadApp\n");
        vm_dl_trace_loader_call("load-app", idx);
        vm_set_call_result(0);
    }
    else if (idx == 6)
    {
        printf("[call]vlDlLoadAppEx\n");
        vm_dl_trace_loader_call("load-app-ex", idx);
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

/* Dream-map number forensics: 0x0104C252 initializes the local countdown
 * object.  Its 0x0104C216 update method selects a 60,000 ms cadence for
 * mode 0 and a 1,000 ms cadence for mode 1.  The probes are opt-in and
 * record only the CBE's live arguments; they never change a register,
 * guest memory, packet, callback, or scheduling decision. */
static bool vm_scene_number_trace_enabled(void)
{
    const char *setting = getenv("CBE_TRACE_SCENE_NUMBERS");

    return setting != NULL && setting[0] != 0 &&
           strcmp(setting, "0") != 0 &&
           strcmp(setting, "off") != 0 &&
           strcmp(setting, "false") != 0;
}

static void vm_scene_number_timer_init_code_callback(uc_engine *uc,
                                                       uint64_t address,
                                                       uint32_t size,
                                                       void *user_data)
{
    static u32 traceCount = 0;
    u32 timer = 0;
    u32 seconds = 0;
    u32 lr = 0;
    FILE *trace = NULL;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 32u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &timer);
    (void)uc_reg_read(uc, UC_ARM_REG_R1, &seconds);
    (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    trace = fopen("logs/scene-number-draw.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_number_timer_init seq=%u module=JianghuOL.CBE "
            "pc=0104c252 timer=%08x seconds=%u caller=%08x\n",
            traceCount, timer, seconds, lr & ~1u);
    fclose(trace);
}

/* Capture the call mode used by the CBE's real timer update method.  This is
 * deliberately bounded and rate-limited: it records the first invocation,
 * each value/mode change, and at most one unchanged state per wall-clock
 * second.  Reading the object is evidence only; countdown progression stays
 * entirely inside the CBE method. */
static void vm_scene_number_timer_process_code_callback(uc_engine *uc,
                                                          uint64_t address,
                                                          uint32_t size,
                                                          void *user_data)
{
    static u32 traceCount = 0;
    static u32 previousTimer = 0;
    static u32 previousMode = 0xffffffffu;
    static u32 previousSeconds = 0xffffffffu;
    static u32 previousAnchor = 0xffffffffu;
    static u32 previousWallMs = 0;
    u32 timer = 0;
    u32 mode = 0;
    u32 seconds = 0;
    u32 anchor = 0;
    u32 lr = 0;
    u32 wallMs = 0;
    u32 cadenceMs = 0;
    FILE *trace = NULL;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 128u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &timer);
    (void)uc_reg_read(uc, UC_ARM_REG_R1, &mode);
    (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    if (timer == 0 ||
        uc_mem_read(uc, timer + 0x0cu, &anchor, sizeof(anchor)) != UC_ERR_OK ||
        uc_mem_read(uc, timer + 0x10u, &seconds, sizeof(seconds)) != UC_ERR_OK)
        return;
    wallMs = SDL_GetTicks();
    if (mode == 0)
        cadenceMs = 60000u;
    else if (mode == 1)
        cadenceMs = 1000u;
    if (traceCount != 0 && timer == previousTimer && mode == previousMode &&
        seconds == previousSeconds && anchor == previousAnchor &&
        (u32)(wallMs - previousWallMs) < 1000u)
        return;
    trace = fopen("logs/scene-number-draw.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_number_timer_process seq=%u module=JianghuOL.CBE "
            "pc=0104c216 timer=%08x mode=%u cadence_ms=%u seconds=%u "
            "anchor_ms=%u wall_ms=%u caller=%08x\n",
            traceCount, timer, mode, cadenceMs, seconds, anchor, wallMs,
            lr & ~1u);
    fclose(trace);
    previousTimer = timer;
    previousMode = mode;
    previousSeconds = seconds;
    previousAnchor = anchor;
    previousWallMs = wallMs;
}

/* The dream-map runtime trace showed that its visible value is committed by
 * the caller at 0x010104EE, rather than by the optional 0x0104C216 object
 * update path.  At this return PC R0 still contains the CBE timer-manager
 * millisecond reading and R1 is the value just written by 0x0104C252.  Log
 * both with the host monotonic clock to measure the actual decrement cadence
 * without influencing CBE execution. */
static void vm_scene_number_timer_commit_code_callback(uc_engine *uc,
                                                         uint64_t address,
                                                         uint32_t size,
                                                         void *user_data)
{
    static u32 traceCount = 0;
    u32 cbeTickMs = 0;
    u32 seconds = 0;
    u32 timer = 0;
    u32 lr = 0;
    FILE *trace = NULL;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 128u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &cbeTickMs);
    (void)uc_reg_read(uc, UC_ARM_REG_R1, &seconds);
    (void)uc_reg_read(uc, UC_ARM_REG_R4, &timer);
    (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    trace = fopen("logs/scene-number-draw.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_number_timer_commit seq=%u module=JianghuOL.CBE "
            "pc=010104ee timer=%08x seconds=%u cbe_tick_ms=%u "
            "wall_ms=%u caller=%08x\n",
            traceCount, timer, seconds, cbeTickMs, SDL_GetTicks(),
            lr & ~1u);
    fclose(trace);
}

/* The initializing caller asks its source record for the field literally
 * named "min" at 0x01010716, then forwards that result through an indirect
 * method at 0x01010734.  Record both instruction-boundary values so a packet
 * field cannot be confused with a value manufactured later in the client.
 * These callbacks are diagnostic only: they read registers and append a
 * bounded record without changing guest execution. */
static void vm_scene_number_min_source_code_callback(uc_engine *uc,
                                                      uint64_t address,
                                                      uint32_t size,
                                                      void *user_data)
{
    static u32 traceCount = 0;
    u32 component = 0;
    u32 value = 0;
    u32 record[4] = {0, 0, 0, 0};
    FILE *trace = NULL;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 32u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R4, &component);
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &value);
    if (component != 0)
        (void)uc_mem_read(uc, component, record, sizeof(record));
    trace = fopen("logs/scene-number-draw.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_number_min_source seq=%u module=JianghuOL.CBE "
            "pc=01010718 component=%08x min=%u "
            "record=%08x,%08x,%08x,%08x\n",
            traceCount, component, value, record[0], record[1], record[2],
            record[3]);
    fclose(trace);
}

static void vm_scene_number_min_getter_code_callback(uc_engine *uc,
                                                      uint64_t address,
                                                      uint32_t size,
                                                      void *user_data)
{
    static u32 traceCount = 0;
    u32 component = 0;
    u32 getter = 0;
    u32 key = 0;
    u32 index = 0;
    u32 sp = 0;
    u32 callerStack[4] = {0, 0, 0, 0};
    FILE *trace = NULL;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 32u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &component);
    (void)uc_reg_read(uc, UC_ARM_REG_R1, &key);
    (void)uc_reg_read(uc, UC_ARM_REG_R2, &getter);
    (void)uc_reg_read(uc, UC_ARM_REG_R6, &index);
    (void)uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    /* 0x01010594 saves LR then allocates 0x444 bytes of locals.  At this
     * point its saved caller is therefore SP+0x454.  Reading it only
     * identifies the dispatch path that supplied the 1/27/4 record. */
    if (sp != 0)
        (void)uc_mem_read(uc, sp + 0x454u, callerStack,
                          sizeof(callerStack));
    trace = fopen("logs/scene-number-draw.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_number_min_getter seq=%u module=JianghuOL.CBE "
            "pc=01010716 component=%08x list_index=%u key=%08x getter=%08x "
            "dispatch_stack=%08x,%08x,%08x,%08x\n",
            traceCount, component, index, key, getter & ~1u,
            callerStack[0] & ~1u, callerStack[1] & ~1u,
            callerStack[2] & ~1u, callerStack[3] & ~1u);
    fclose(trace);
}

/* 0x01033D40 is the shared object-field creator used by the record getter's
 * family.  Watching only a 1/27/4 record whose field bytes spell `min` gives
 * the construction caller without scanning arbitrary heap writes. */
static void vm_scene_number_min_field_create_code_callback(uc_engine *uc,
                                                           uint64_t address,
                                                           uint32_t size,
                                                           void *user_data)
{
    static u32 traceCount = 0;
    u32 record = 0;
    u32 key = 0;
    u32 valueType = 0;
    u32 lr = 0;
    u32 header[3] = {0, 0, 0};
    char name[4] = {0, 0, 0, 0};
    FILE *trace = NULL;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 16u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &record);
    (void)uc_reg_read(uc, UC_ARM_REG_R1, &key);
    (void)uc_reg_read(uc, UC_ARM_REG_R2, &valueType);
    (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    if (record == 0 || key == 0 ||
        uc_mem_read(uc, key, name, sizeof(name)) != UC_ERR_OK ||
        memcmp(name, "min", sizeof(name)) != 0)
    {
        return;
    }
    (void)uc_mem_read(uc, record, header, sizeof(header));
    trace = fopen("logs/scene-number-draw.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_number_min_field_create seq=%u module=JianghuOL.CBE "
            "pc=01033d40 record=%08x header=%08x,%08x,%08x "
            "key=%08x type=%u caller=%08x\n",
            traceCount, record, header[0], header[1], header[2], key,
            valueType, lr & ~1u);
    fclose(trace);
}

static void vm_scene_number_min_dispatch_code_callback(uc_engine *uc,
                                                        uint64_t address,
                                                        uint32_t size,
                                                        void *user_data)
{
    static u32 traceCount = 0;
    u32 receiver = 0;
    u32 value = 0;
    u32 method = 0;
    FILE *trace = NULL;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 32u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &receiver);
    (void)uc_reg_read(uc, UC_ARM_REG_R1, &value);
    (void)uc_reg_read(uc, UC_ARM_REG_R2, &method);
    trace = fopen("logs/scene-number-draw.log", "ab");
    if (trace == NULL)
        return;
    ++traceCount;
    fprintf(trace,
            "scene_number_min_dispatch seq=%u module=JianghuOL.CBE "
            "pc=01010734 receiver=%08x min=%u method=%08x\n",
            traceCount, receiver, value, method & ~1u);
    fclose(trace);
}

/* The caller recovered from 0x01010594 is the type-7 data-event dispatcher
 * at 0x01012E4C.  Its first argument is passed unchanged to the CBE packet
 * parser.  Look only at a syntactically bounded WT 1/27/4 object at that
 * instruction boundary, so the already-proven `min` record can be tied back
 * to an actual delivered CBE event without observing heap writes or changing
 * any guest/host state. */
static bool vm_scene_number_packet_read_min(const u8 *payload, u32 payloadLen,
                                            u32 *valueOut,
                                            const char **encodingOut)
{
    static const u8 name[] = {3u, 'm', 'i', 'n'};

    if (payload == NULL || payloadLen < sizeof(name) + 2u)
        return false;
    for (u32 i = 0; i + sizeof(name) + 2u <= payloadLen; ++i)
    {
        u32 valuePos;

        if (memcmp(payload + i, name, sizeof(name)) != 0)
            continue;
        valuePos = i + sizeof(name);
        if (valuePos < payloadLen && payload[valuePos] == 0)
            ++valuePos;
        if (valuePos + 7u <= payloadLen && payload[valuePos] == 0x06u &&
            payload[valuePos + 1u] == 0 && payload[valuePos + 2u] == 4u)
        {
            if (valueOut != NULL)
            {
                *valueOut = ((u32)payload[valuePos + 3u] << 24) |
                            ((u32)payload[valuePos + 4u] << 16) |
                            ((u32)payload[valuePos + 5u] << 8) |
                            (u32)payload[valuePos + 6u];
            }
            if (encodingOut != NULL)
                *encodingOut = "u32";
            return true;
        }
        if (valuePos + 5u <= payloadLen && payload[valuePos] == 4u &&
            payload[valuePos + 1u] == 0 && payload[valuePos + 2u] == 2u)
        {
            if (valueOut != NULL)
                *valueOut = (u32)(((u16)payload[valuePos + 3u] << 8) |
                                  payload[valuePos + 4u]);
            if (encodingOut != NULL)
                *encodingOut = "u16";
            return true;
        }
        if (valuePos + 5u <= payloadLen && payload[valuePos] == 4u)
        {
            if (valueOut != NULL)
            {
                *valueOut = ((u32)payload[valuePos + 1u] << 24) |
                            ((u32)payload[valuePos + 2u] << 16) |
                            ((u32)payload[valuePos + 3u] << 8) |
                            (u32)payload[valuePos + 4u];
            }
            if (encodingOut != NULL)
                *encodingOut = "u32-compact";
            return true;
        }
        if (valuePos + 4u <= payloadLen && payload[valuePos] == 0x03u &&
            payload[valuePos + 1u] == 0 && payload[valuePos + 2u] == 1u)
        {
            if (valueOut != NULL)
                *valueOut = payload[valuePos + 3u];
            if (encodingOut != NULL)
                *encodingOut = "u8";
            return true;
        }
    }
    return false;
}

static void vm_scene_number_event_27_4_code_callback(uc_engine *uc,
                                                      uint64_t address,
                                                      uint32_t size,
                                                      void *user_data)
{
    enum { VM_SCENE_NUMBER_EVENT_PACKET_MAX = 4096 };
    static u32 traceCount = 0;
    u32 response = 0;
    u32 arg1 = 0;
    u32 arg2 = 0;
    u32 eventType = 0;
    u32 lr = 0;
    u8 header[5];
    u8 packet[VM_SCENE_NUMBER_EVENT_PACKET_MAX];
    u32 packetLen;
    u32 offset;
    u32 objectIndex = 0;

    (void)address;
    (void)size;
    (void)user_data;
    if (!vm_scene_number_trace_enabled() || traceCount >= 16u)
        return;
    (void)uc_reg_read(uc, UC_ARM_REG_R0, &response);
    (void)uc_reg_read(uc, UC_ARM_REG_R1, &arg1);
    (void)uc_reg_read(uc, UC_ARM_REG_R2, &arg2);
    (void)uc_reg_read(uc, UC_ARM_REG_R3, &eventType);
    (void)uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    if (eventType != 7u || response == 0 ||
        uc_mem_read(uc, response, header, sizeof(header)) != UC_ERR_OK ||
        header[0] != 'W' || header[1] != 'T')
    {
        return;
    }
    packetLen = ((u32)header[2] << 8) | header[3];
    if (packetLen < sizeof(header) || packetLen > sizeof(packet) ||
        uc_mem_read(uc, response, packet, packetLen) != UC_ERR_OK)
    {
        return;
    }
    offset = 5u;
    while (objectIndex < packet[4] && offset + 6u <= packetLen)
    {
        u32 objectLen = ((u32)packet[offset + 4u] << 8) |
                        packet[offset + 5u];

        if (objectLen < 6u || objectLen > packetLen - offset)
            return;
        if (packet[offset] == 1u && packet[offset + 1u] == 27u &&
            packet[offset + 2u] == 4u)
        {
            const char *encoding = "missing-or-unsupported";
            u32 value = 0;
            bool haveMin = vm_scene_number_packet_read_min(
                packet + offset + 6u, objectLen - 6u, &value, &encoding);
            FILE *trace = fopen("logs/scene-number-draw.log", "ab");

            if (trace != NULL)
            {
                ++traceCount;
                fprintf(trace,
                        "scene_number_event_27_4 seq=%u module=JianghuOL.CBE "
                        "pc=01012e4c event=%u response=%08x arg1=%08x "
                        "arg2=%08x caller=%08x packet_len=%u object_index=%u "
                        "min=%u encoding=%s present=%u\n",
                        traceCount, eventType, response, arg1, arg2,
                        lr & ~1u, packetLen, objectIndex, value, encoding,
                        haveMin ? 1u : 0u);
                fclose(trace);
            }
            if (traceCount >= 16u)
                return;
        }
        offset += objectLen;
        ++objectIndex;
    }
}

static uc_err add_manager_code_hooks(uc_engine *uc)
{
    uc_hook hook;
    uc_err err;
    /* `hookCodeCallBack` already covers all guest code, including loaded CBM
     * modules.  The former second, pool-wide UC_HOOK_CODE callback only
     * repeated read-only battle/R9 forensics for every dynamic instruction.
     * Current screen/module ownership is recorded at the screen and loader
     * lifecycle boundaries, so no platform dispatch depends on that hot-path
     * observation. */
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
    if (vm_scene_number_trace_enabled())
    {
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_timer_commit_code_callback, NULL,
                          0x010104EEu, 0x010104EEu);
        if (err != UC_ERR_OK)
            return err;
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_timer_process_code_callback, NULL,
                          0x0104C216u, 0x0104C216u);
        if (err != UC_ERR_OK)
            return err;
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_timer_init_code_callback, NULL,
                          0x0104C252u, 0x0104C252u);
        if (err != UC_ERR_OK)
            return err;
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_min_field_create_code_callback,
                          NULL, 0x01033D40u, 0x01033D40u);
        if (err != UC_ERR_OK)
            return err;
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_min_getter_code_callback, NULL,
                          0x01010716u, 0x01010716u);
        if (err != UC_ERR_OK)
            return err;
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_min_source_code_callback, NULL,
                          0x01010718u, 0x01010718u);
        if (err != UC_ERR_OK)
            return err;
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_min_dispatch_code_callback, NULL,
                          0x01010734u, 0x01010734u);
        if (err != UC_ERR_OK)
            return err;
        err = uc_hook_add(uc, &hook, UC_HOOK_CODE,
                          vm_scene_number_event_27_4_code_callback, NULL,
                          0x01012E4Cu, 0x01012E4Cu);
        if (err != UC_ERR_OK)
            return err;
    }
    return UC_ERR_OK;
}

void hookCodeCallBack(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    u32 tmp1, tmp2, tmp3, tmp4, tmp5;
    u32 pc = (u32)address & ~1u;

    /* Normal sessions invoke this only at VM_LOG_NOOP_ADDRESS.  A full guest
     * code range is reserved for active repository automation, where the
     * declared PC observations and R9 compatibility are needed. */
    if (!g_autotestEnabled && vm_is_pool_entry(pc))
        return;

    vm_restore_main_r9_for_rom_code((u32)address);
    if (g_autotestEnabled)
    {
        vm_hangup_transition_capture_pre_restore(pc);
        vm_autotest_arm_equipment_enhance_rules_watch();
        vm_autotest_note_startup_pc(pc);
        vm_autotest_note_scene_actor_parser_pc(pc);
        vm_autotest_note_backpack_parser_pc(pc);
        vm_autotest_note_shop_parser_pc(pc);
        vm_autotest_note_role_attr_page_pc(pc);
        vm_autotest_note_equipment_enhance_rules_pc(pc);
        vm_note_mmgame_transfer_parser_pc(pc);
        vm_note_timer_control_item_pc(pc);
        vm_note_stream_read_i16_pc(pc);
        vm_note_net_wrapper_pc(pc);
        vm_shop_return_forensics_note_pc(pc);
        vm_shop_return_forensics_note_mmgame_input_pc(pc);
        vm_hangup_protocol_parser_trace_note_pc(pc);
        vm_hangup_battle_state_watch_note_pc(pc);
        vm_hangup_delegate_register_trace_note_pc(pc);
        vm_hangup_ui_dispatch_trace_note_pc(pc);
        vm_hangup_rand_range_trace_note_pc(pc);
        vm_hangup_auto_candidate_watch_note_pc(pc);
        vm_hangup_transition_trace_note_pc(pc);
        vm_hangup_battle_module_trace_note_pc(pc);
        vm_hangup_combatinfo_read_trace_note_pc(pc);
        vm_hangup_candidate_fault_trace_note_pc(pc);
        vm_automation_note_battle_native_exit_pc(pc);
        vm_automation_note_dream_npc_entry_pc(pc);
        vm_hangup_battle_render_trace_note_pc(pc);
        vm_hangup_vital_forensics_note_pc(pc);
        vm_battle_insight_forensics_note_pc(pc);
        vm_note_sce_load_entry_pc(pc);
        vm_trace_sce_entity_callback_pc(pc);
        vm_trace_scene_challenge_node_table_pc(pc);
        vm_trace_action13_boundary_pc(pc);
        vm_trace_scene_node_create_pc(pc);
        vm_trace_scene_battle_collision_pc(pc);
        vm_trace_actor_scene_capacity_pc(pc);
        vm_note_castlevania_wpay_pc(pc);
    }

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
