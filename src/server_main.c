/* Standalone authoritative service entry.
 *
 * The emulator's CBE/LCD/Unicorn entry remains in main.c.  This translation
 * unit owns only the host protocol service so jh-online-server never links
 * client VM or GUI lifetime code.
 */

#include <stdarg.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "main.h"
#include "gifDecode.h"
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define VM_SCHED_MAX_NET_TASKS 8
#define VM_SCHED_MAX_TIMERS 20
#define VM_SCHED_FRAME_MS 100u

typedef struct
{
    u8 hasSceneTarget;
    u8 sceneSubtype;
    u8 sceneCompleteAfterCallback;
    u8 updateComplete;
    u8 hasHangupBattleStart;
    u8 hangupBattleStartDirect;
    u8 hangupResponseObjectCount;
    u8 hangupResponseParsedCount;
    u8 reserved0;
    u16 sceneX;
    u16 sceneY;
    u32 hangupResponseSequence;
    u32 hangupResponseLength;
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

/* The headless service has no guest test harness.  Its trace helper writes
 * regular host diagnostics without touching emulated state. */
static void vm_autotest_note(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static u8 g_mockServiceOnly = 1;
static u8 g_mockServiceWarnedUnavailable = 0;
static char g_mockServiceHost[64] = "127.0.0.1";
static char g_mockServiceBindHost[64] = "0.0.0.0";
static char g_mockAdminBindHost[64] = "0.0.0.0";
static u32 g_mockServiceClientId = 0;
static u16 g_mockServicePort = 19090;
static u16 g_mockAdminPort = 19091;
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
static u8 g_netBusinessSendReadyDeferred = 0;
static u8 g_netBusinessSendReadyRerun = 0;
static u8 g_netBusinessSendReadyPostVm = 0;
static u8 g_loginTail42AllocPending = 0;
static u8 g_loginTail42FlushPending = 0;
static u32 g_netUpLinkData = 0;
static u32 g_netDownLinkData = 0;
static u32 g_netCurrentObject = 0;
static u8 g_netLastHandledValid = 0;
static u32 g_netLastHandledResponseLen = 0;
static char g_netLastHandledSource[64];
static char g_netLastHandledSummary[512];

static bool g_vm_net_mock_pending_scene_save_valid = false;
static char g_vm_net_mock_pending_scene_save_scene[64];
static char g_vm_net_mock_pending_scene_save_reason[64];
static u16 g_vm_net_mock_pending_scene_save_x = 0;
static u16 g_vm_net_mock_pending_scene_save_y = 0;

/* Authoritative battle session state.  It was formerly declared alongside
 * the emulator only because both programs shared main.c. */
static u32 g_mockBattleOperateSessionSerial = 0;
static u32 g_mockBattleOperateTurnCounter = 0;
u8 g_mockBattleOperateSessionArmed = 0;
static u8 g_vm_net_mock_battle_auto_enabled = 0;
static u8 g_vm_net_mock_battle_auto_last_operation_valid = 0;
static u32 g_vm_net_mock_battle_auto_last_operation_role_id = 0;
static u32 g_vm_net_mock_battle_auto_last_operation_index = 0;
static u32 g_vm_net_mock_battle_auto_last_operation_operate = 0;
static u8 g_vm_net_mock_battle_auto_replay_inflight = 0;
static u8 g_vm_net_mock_battle_action6_emitted_count = 0;
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

static bool vm_net_mock_append_battle_terminal_case11_object(
    u8 *out, u32 outCap, u32 *pos);

static u32 scheduler_get_tick_ms(void)
{
    static uint64_t startMs = 0;
    uint64_t nowMs = 0;
#ifdef _WIN32
    /* Match the original SDL_GetTicks() wraparound contract.  GetTickCount
     * is available in the project's MinGW import library, unlike the newer
     * GetTickCount64 symbol. */
    nowMs = (uint64_t)GetTickCount();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    nowMs = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
    if (startMs == 0)
        startMs = nowMs;
    return (u32)(nowMs - startMs);
}

static bool vm_server_parse_u32(const char *text, u32 *valueOut)
{
    char *end = NULL;
    unsigned long value = 0;
    if (valueOut != NULL)
        *valueOut = 0;
    if (text == NULL || text[0] == 0)
        return false;
    value = strtoul(text, &end, 10);
    if (end == text || *end != 0 || value > 0xfffffffful)
        return false;
    if (valueOut != NULL)
        *valueOut = (u32)value;
    return true;
}

static void vm_server_apply_port(const char *label, const char *text,
                                 u16 *portOut)
{
    u32 value = 0;
    if (portOut == NULL || !vm_server_parse_u32(text, &value) ||
        value == 0 || value > 65535u)
    {
        printf("[warn][mock-service] invalid %s=%s\n", label,
               text ? text : "<empty>");
        return;
    }
    *portOut = (u16)value;
}

/* Server-only command line parsing.  These options retain the public
 * standalone-service contract; remote-client-only options intentionally do
 * not affect a listener process. */
static void vm_mock_service_init_config(int argc, char *args[])
{
    const char *envBind = getenv("CBE_MOCK_SERVICE_BIND");
    const char *envPort = getenv("CBE_MOCK_SERVICE_PORT");
    const char *envAdminBind = getenv("CBE_MOCK_ADMIN_BIND");
    const char *envAdminPort = getenv("CBE_MOCK_ADMIN_PORT");

    if (envBind != NULL && envBind[0] != 0)
        snprintf(g_mockServiceBindHost, sizeof(g_mockServiceBindHost), "%s", envBind);
    if (envAdminBind != NULL && envAdminBind[0] != 0)
        snprintf(g_mockAdminBindHost, sizeof(g_mockAdminBindHost), "%s", envAdminBind);
    if (envPort != NULL)
        vm_server_apply_port("CBE_MOCK_SERVICE_PORT", envPort, &g_mockServicePort);
    if (envAdminPort != NULL)
        vm_server_apply_port("CBE_MOCK_ADMIN_PORT", envAdminPort, &g_mockAdminPort);

    for (int i = 1; i < argc; ++i)
    {
        if (strncmp(args[i], "--mock-service-port=", 20) == 0)
            vm_server_apply_port("mock-service-port", args[i] + 20, &g_mockServicePort);
        else if (strncmp(args[i], "--mock-admin-port=", 18) == 0)
            vm_server_apply_port("mock-admin-port", args[i] + 18, &g_mockAdminPort);
        else if (strncmp(args[i], "--mock-service-bind=", 20) == 0 && args[i][20] != 0)
            snprintf(g_mockServiceBindHost, sizeof(g_mockServiceBindHost), "%s", args[i] + 20);
        else if (strncmp(args[i], "--mock-admin-bind=", 18) == 0 && args[i][18] != 0)
            snprintf(g_mockAdminBindHost, sizeof(g_mockAdminBindHost), "%s", args[i] + 18);
    }

    printf("[info][mock-service] mode=server-only bind=%s:%u admin=%s:%u\n",
           g_mockServiceBindHost, g_mockServicePort,
           g_mockAdminBindHost, g_mockAdminPort);
}

#include "server/mock-server.c"

int main(int argc, char *args[])
{
    const char *resourceRoot = getenv("CBE_RESOURCE_ROOT");
    char originalCwd[1024];
    char resourceCandidate[1200];
    bool resourceReady = false;

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
}
