/*
 * Remote game-service transport used by client-only builds.
 *
 * This file deliberately contains no request handlers, database access,
 * listener sockets or HTTP administration code.  It only forwards guest WT
 * packets to the configured game server and queues the returned bytes back to
 * the emulated client.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET vm_client_socket;
#define VM_CLIENT_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int vm_client_socket;
#define VM_CLIENT_INVALID_SOCKET (-1)
#endif
#include <stdio.h>
#include <time.h>


enum
{
    VM_CLIENT_FRAME_SIZE = 20,
    VM_CLIENT_REQUEST_FLAG_SCENE_SYNC_POLL = 0x2u,
    VM_CLIENT_REQUEST_FLAG_DISCONNECT = 0x4u,
    VM_CLIENT_RESPONSE_FLAG_CLOSE_AFTER_DATA = 0x1u,
    /* Primary CBMR is followed by a second CBMR body on the same TCP reply. */
    VM_CLIENT_RESPONSE_FLAG_HAS_FOLLOWUP = 0x2u,
    /* Applied to send/recv after the TCP handshake completes. */
    VM_CLIENT_SOCKET_TIMEOUT_MS = 10000,
    /* Brief radio blips / Doze wake: retry with backoff before giving up. */
    VM_CLIENT_DATA_ATTEMPTS = 5,
    /* When transport is already marked unreachable, one attempt avoids pinning
     * the sole data worker behind a multi-second retry ladder. */
    VM_CLIENT_DATA_ATTEMPTS_UNREACHABLE = 1,
    VM_CLIENT_DATA_RETRY_BASE_MS = 250,
    /*
     * Cap blocking connect so a SYN blackhole cannot freeze the data worker
     * for the OS TCP timeout (often 20s+).
     */
    VM_CLIENT_DATA_CONNECT_TIMEOUT_MS = 5000,
    /*
     * Scene-poll only: non-blocking connect budget.  WT data uses the bounded
     * connect above so a completed handshake cannot be abandoned by a select
     * race before CBMS is written (deadline path still waits for SO_ERROR=0).
     */
    VM_CLIENT_POLL_CONNECT_TIMEOUT_MS = 2000,
    /* Minimum wall time between scene-poll attempts (scheduler ticks are ~frame).
     * 2s balances sync freshness vs short-TCP load; server presence timeout ~30s. */
    VM_CLIENT_POLL_MIN_INTERVAL_MS = 2000,
    /*
     * After map-stone / region-teleport confirm, deferred 30/1 rides scene-poll
     * but the server refuses same-tick delivery (~100ms).  Mirror PixiJS: wait
     * 150ms then poll at 150ms until 30/1 lands (or the short window ends).
     * Idle cadence stays at VM_CLIENT_POLL_MIN_INTERVAL_MS.
     */
    VM_CLIENT_POLL_URGENT_INTERVAL_MS = 150,
    /* Server refuses deferred 30/1 on the confirm tick (~100ms). */
    VM_CLIENT_POLL_URGENT_READY_DELAY_MS = 100,
    VM_CLIENT_POLL_URGENT_WINDOW_MS = 2500,
    VM_CLIENT_POLL_URGENT_MAX_POLLS = 12,
    VM_CLIENT_REQUEST_MAX = 512,
    VM_CLIENT_QUEUE_MAX = 64,
    VM_CLIENT_FOLLOWUP_MAX = 65536
};

typedef struct
{
    char scene[64];
    u16 x;
    u16 y;
    u32 exitId;
    u8 mapType;
    bool hasSceEntry;
    bool needsSceneDownload;
} vm_net_mock_scene_change_target;

/*
 * Scene-target guard for server-only / remote-client mode.
 * Embedded builders used to set these globals in-process; a remote client must
 * reconstruct the same facts from WT bytes before the guest callback runs
 * (docs/re/2026-07-19-linan-resource-reenter-crash.md).  Without that,
 * 27/12+posinfo after instance-enter 30/1 re-runs EnterSceneByMapName and can
 * crash ScreenInit (dream kind=3: pc→0x4ad5542 / lr=0x010136b3).
 */
static vm_net_mock_scene_change_target g_vm_net_mock_last_scene_change_target;
static bool g_vm_net_mock_last_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_scene_change_target_serial = 0;
/*
 * Deferred WT30/1 already EnterSceneByMapName.  Map-stone 2/3 still sends
 * 27/12+posinfo to bind the walking sprite; allow that one completion reenter
 * through same-suppress so loading is not left half-open (丹霞山 2026-07-31).
 */
static bool g_vm_scene_allow_one_map_stone_completion_reenter = false;
/*
 * Short post-confirm scene-poll boost (Pixi-style).  Cleared when deferred
 * 30/1 observation arrives or the window / poll budget expires.
 */
static u32 g_vm_scene_poll_urgent_ready_at_ms = 0;
static u32 g_vm_scene_poll_urgent_until_ms = 0;
static u32 g_vm_scene_poll_urgent_remaining = 0;
/*
 * Instance-enter into 29* loads mmGame via DF (not 01018150), so the
 * same-reenter guard is never armed.  2/3 completion still calls
 * EnterSceneByMapName → second ScreenInit → pc=0x4ad5542 on FB+kind3.
 * Block that completion reenter for dream only (2026-07-31).
 * Prefer host entry intercept at 0x01018150 (before busy arm); screen_mgr
 * suppress is fallback only (docs/re/2026-07-31-dream-reenter-loading-stall.md).
 */
static bool g_vm_scene_block_dream_completion_reenter = false;
/*
 * mmShop→mmGame kind-2 same-scene 30/1 on 29*: must run one EnterScene to
 * rebuild SCE type4 battle mapctrl.  First messenger enter still uses
 * block_dream_completion_reenter; already-on-29* 30/1 uses this allow-once
 * (2026-08-05 shop-return encounter soft-fail).
 */
static bool g_vm_scene_allow_one_dream_shop_return_reenter = false;
/*
 * 0=off, 1=next EnterSceneByMapName must run (30/1 primary bind),
 * 2=skip further EnterScene (2/3 completion).  Skipping the first call
 * after DF ScreenInit caused pc=0@0x0104d775 (debug-191169).
 */
static u8 g_vm_dream_enter_scene_gate = 0;
/*
 * Login/cold dream DF shell (init 0x0502xxxx) stays PAUSE'd under outdoor.
 * Outdoor→29* hot DoLoading reuses the same `this` with init 0x0518xxxx and
 * faults ParseMinfo (debug-191169). Prefer ScreenResume of that shell.
 */
static bool g_vm_dream_prefer_resume_paused_shell = false;
/* Extensionless 29* leaf seen via WT18/7 uptodate/download (login has no 30/1). */
static char g_vm_dream_scene_hint[64];
typedef struct
{
    bool valid;
    bool pausedUnderOther;
    char scene[64];
    u32 screen;
    u32 thisPtr;
    u32 initEntry;
    u32 resumeEntry;
    u32 dataPackage;
    u32 moduleBase;
} vm_dream_resumable_shell;
static vm_dream_resumable_shell g_vm_dream_resumable_shell;
static vm_net_mock_scene_change_target g_vm_net_mock_last_completed_scene_change_target;
static bool g_vm_net_mock_last_completed_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_completed_scene_change_tick = 0;
static u32 g_vm_net_mock_remote_completed_scene_target_serial = 0;
static bool g_vm_net_mock_update_completed_reenter_pending = false;
static char g_vm_net_mock_update_completed_name[64];

enum
{
    VM_NET_MOCK_COMPLETED_SCENE_REUSE_TICKS = 120
};

static bool vm_net_mock_scene_names_equal_loose(const char *a, const char *b)
{
    return a != NULL && b != NULL && a[0] != 0 && strcmp(a, b) == 0;
}

static bool vm_net_mock_consume_update_completed_scene_reenter(
    const vm_net_mock_scene_change_target *target)
{
    if (!g_vm_net_mock_update_completed_reenter_pending)
        return false;
    if (target == NULL || target->scene[0] == 0)
    {
        g_vm_net_mock_update_completed_reenter_pending = false;
        return false;
    }
    g_vm_net_mock_update_completed_reenter_pending = false;
    printf("[info][screen] screen_mgr allow-update-reenter scene=%s pos=(%u,%u) "
           "exit=%u file=%s\n",
           target->scene, target->x, target->y, target->exitId,
           g_vm_net_mock_update_completed_name[0]
               ? g_vm_net_mock_update_completed_name
               : "-");
    return true;
}

static void vm_net_mock_note_update_chunk_complete(const char *payloadName)
{
    g_vm_net_mock_update_completed_reenter_pending = true;
    if (payloadName != NULL && payloadName[0] != 0)
    {
        snprintf(g_vm_net_mock_update_completed_name,
                 sizeof(g_vm_net_mock_update_completed_name), "%s", payloadName);
    }
    else
    {
        g_vm_net_mock_update_completed_name[0] = 0;
    }
}

static void vm_net_mock_snapshot_remote_completed_target(u32 serial)
{
    if (!g_vm_net_mock_last_scene_change_target_valid || serial == 0 ||
        serial != g_vm_net_mock_last_scene_change_target_serial)
    {
        return;
    }
    g_vm_net_mock_last_completed_scene_change_target =
        g_vm_net_mock_last_scene_change_target;
    g_vm_net_mock_last_completed_scene_change_target.needsSceneDownload = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = true;
    g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;
    g_vm_net_mock_remote_completed_scene_target_serial = serial;
}

static u32 vm_net_mock_apply_remote_observation(
    const vm_net_remote_observation *observation)
{
    u32 clearAfterCallbackSerial = 0;
    bool restoredCompletedTarget = false;

    if (observation == NULL)
        return 0;

    if (observation->hasSceneTarget && observation->scene[0] != 0)
    {
        vm_net_mock_scene_change_target target;

        memset(&target, 0, sizeof(target));
        snprintf(target.scene, sizeof(target.scene), "%s", observation->scene);
        target.x = observation->sceneX;
        target.y = observation->sceneY;
        target.mapType = 2;
        target.hasSceEntry = true;
        target.needsSceneDownload = false;

        /*
         * Snapshot before clearing completed: shop-return kind-2 30/1 must see
         * the prior dream completion to allow one EnterScene.  Clearing first
         * made alreadyOnSameDream always false → permanent block_dream
         * (2026-08-05: server sent 30/1, client never EnterScene).
         */
        {
            bool isDream29 = target.scene[0] == '2' && target.scene[1] == '9';
            bool priorCompletedSameDream =
                isDream29 &&
                g_vm_net_mock_last_completed_scene_change_target_valid &&
                g_vm_net_mock_last_completed_scene_change_target.scene[0] ==
                    '2' &&
                g_vm_net_mock_last_completed_scene_change_target.scene[1] ==
                    '9' &&
                vm_net_mock_scene_names_equal_loose(
                    g_vm_net_mock_last_completed_scene_change_target.scene,
                    target.scene);
            bool priorLiveSameDream =
                isDream29 &&
                g_vm_net_mock_last_scene_change_target_valid &&
                !g_vm_scene_block_dream_completion_reenter &&
                g_vm_dream_enter_scene_gate == 0 &&
                g_vm_net_mock_last_scene_change_target.scene[0] == '2' &&
                g_vm_net_mock_last_scene_change_target.scene[1] == '9' &&
                vm_net_mock_scene_names_equal_loose(
                    g_vm_net_mock_last_scene_change_target.scene, target.scene);
            bool alreadyOnSameDream =
                priorCompletedSameDream || priorLiveSameDream;

            g_vm_net_mock_update_completed_reenter_pending = false;
            g_vm_net_mock_update_completed_name[0] = 0;
            g_vm_net_mock_last_completed_scene_change_target_valid = false;
            g_vm_net_mock_remote_completed_scene_target_serial = 0;
            g_vm_net_mock_last_scene_change_target = target;
            g_vm_net_mock_last_scene_change_target_valid = true;
            ++g_vm_net_mock_last_scene_change_target_serial;
            if (g_vm_net_mock_last_scene_change_target_serial == 0)
                g_vm_net_mock_last_scene_change_target_serial = 1;

            /*
             * Outdoor shop-return DF shell already owns EnterScene.  Do not arm
             * map-stone 27/12 reenter for a stale kind-2 30/1 in that window.
             */
            g_vm_scene_allow_one_map_stone_completion_reenter =
                observation->sceneSubtype == 1 && !isDream29 &&
                !(g_vm_shop_return_suppress_kind2 &&
                  g_schedulerTick < g_vm_shop_return_suppress_kind2_until_tick);
            if (observation->sceneSubtype == 1)
            {
                /* Deferred enter landed — stop urgent poll burst. */
                g_vm_scene_poll_urgent_ready_at_ms = 0;
                g_vm_scene_poll_urgent_until_ms = 0;
                g_vm_scene_poll_urgent_remaining = 0;
            }
            if (observation->sceneSubtype == 1 && !isDream29 &&
                g_vm_scene_allow_one_map_stone_completion_reenter)
            {
                g_vm_map_stone_dismiss_bar = 1;
                g_vm_map_stone_dismiss_bar_tick = 0;
            }
            if (observation->sceneSubtype == 1 && alreadyOnSameDream)
            {
                g_vm_scene_allow_one_dream_shop_return_reenter = true;
                g_vm_scene_block_dream_completion_reenter = false;
                g_vm_dream_enter_scene_gate = 0;
            }
            else
            {
                g_vm_scene_allow_one_dream_shop_return_reenter = false;
                g_vm_scene_block_dream_completion_reenter =
                    observation->sceneSubtype == 1 && isDream29;
                g_vm_dream_enter_scene_gate =
                    g_vm_scene_block_dream_completion_reenter ? 1u : 0u;
            }
            g_vm_dream_prefer_resume_paused_shell = false;
        }
        printf("[info][screen] remote_scene_target_apply serial=%u subtype=%u "
               "scene=%s pos=(%u,%u) allow_map_stone_reenter=%u "
               "allow_dream_shop_return_reenter=%u block_dream_reenter=%u "
               "evidence=WT30/%u-immediately-before-guest-callback\n",
               g_vm_net_mock_last_scene_change_target_serial,
               observation->sceneSubtype, target.scene, target.x, target.y,
               g_vm_scene_allow_one_map_stone_completion_reenter ? 1u : 0u,
               g_vm_scene_allow_one_dream_shop_return_reenter ? 1u : 0u,
               g_vm_scene_block_dream_completion_reenter ? 1u : 0u,
               observation->sceneSubtype);
        /* #region agent log */
        if (target.scene[0] == '2' && target.scene[1] == '9')
            agent_dbg0b451a_on_dream_sce(target.scene);
        /* #endregion */
    }

    if (observation->sceneCompleteAfterCallback &&
        g_vm_net_mock_last_scene_change_target_valid &&
        (observation->scene[0] == 0 ||
         vm_net_mock_scene_names_equal_loose(
             observation->scene, g_vm_net_mock_last_scene_change_target.scene)))
    {
        clearAfterCallbackSerial = g_vm_net_mock_last_scene_change_target_serial;
        vm_net_mock_snapshot_remote_completed_target(clearAfterCallbackSerial);
        printf("[info][screen] remote_scene_target_complete_pending serial=%u "
               "scene=%s action=clear-after-own-callback evidence=WT30/2\n",
               clearAfterCallbackSerial,
               g_vm_net_mock_last_scene_change_target.scene);
    }

    if (observation->updateComplete && observation->updateName[0] != 0)
    {
        if (!g_vm_net_mock_last_scene_change_target_valid &&
            g_vm_net_mock_last_completed_scene_change_target_valid &&
            g_vm_net_mock_remote_completed_scene_target_serial != 0 &&
            g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick <
                VM_NET_MOCK_COMPLETED_SCENE_REUSE_TICKS)
        {
            g_vm_net_mock_last_scene_change_target =
                g_vm_net_mock_last_completed_scene_change_target;
            g_vm_net_mock_last_scene_change_target_valid = true;
            g_vm_net_mock_last_scene_change_target_serial =
                g_vm_net_mock_remote_completed_scene_target_serial;
            g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;
            restoredCompletedTarget = true;
            printf("[info][screen] remote_scene_target_restore serial=%u "
                   "scene=%s file=%s reason=resource-completion-callback\n",
                   g_vm_net_mock_last_scene_change_target_serial,
                   g_vm_net_mock_last_scene_change_target.scene,
                   observation->updateName);
        }

        if (g_vm_net_mock_last_scene_change_target_valid)
        {
            vm_net_mock_note_update_chunk_complete(observation->updateName);
            if (restoredCompletedTarget)
                clearAfterCallbackSerial =
                    g_vm_net_mock_last_scene_change_target_serial;
            printf("[info][screen] remote_update_complete_apply file=%s "
                   "serial=%u action=arm-one-scene-reenter "
                   "immediately-before-guest-callback\n",
                   observation->updateName,
                   g_vm_net_mock_last_scene_change_target_serial);
        }
        else
        {
            printf("[warn][screen] remote_update_complete_unbound file=%s "
                   "action=no-scene-reenter reason=no-recent-packet-target\n",
                   observation->updateName);
        }
    }

    return clearAfterCallbackSerial;
}

static void vm_net_mock_finish_remote_observation(u32 sceneTargetSerial)
{
    if (sceneTargetSerial == 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        sceneTargetSerial != g_vm_net_mock_last_scene_change_target_serial)
    {
        return;
    }
    printf("[info][screen] remote_scene_target_complete serial=%u scene=%s "
           "action=cleared-after-own-guest-callback\n",
           sceneTargetSerial, g_vm_net_mock_last_scene_change_target.scene);
    {
        bool wasDream29 =
            g_vm_net_mock_last_scene_change_target.scene[0] == '2' &&
            g_vm_net_mock_last_scene_change_target.scene[1] == '9';

        g_vm_net_mock_last_scene_change_target_valid = false;
        /*
         * Full-stack scene rebuild (临安 map-stone) may EnterScene without going
         * through same-reenter-allowed-once.  Drop the one-shot arm so a later
         * unrelated same-suppress cannot consume a stale allow.
         *
         * Dream instance 2/3 may now be 30/2-only (no EnterScene).  Clear the
         * completion block after the guest callback so it cannot stick.
         */
        g_vm_scene_allow_one_map_stone_completion_reenter = false;
        g_vm_scene_allow_one_dream_shop_return_reenter = false;
        if (wasDream29)
        {
            g_vm_scene_block_dream_completion_reenter = false;
            g_vm_dream_enter_scene_gate = 0;
        }
    }
}

static bool vm_net_mock_should_rearm_send_ready(void)
{
    return false;
}

/* These emulator helpers historically lived in mock-server.c because that
 * file was included into main.c.  Client-only builds still need them for
 * resource lookup, billing-module names and executable pool diagnostics. */
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
    u32 len;
    u32 ptr;
    if (text == NULL)
        return 0;
    len = (u32)strlen(text) + 1;
    ptr = vm_malloc(len);
    if (ptr != 0)
        uc_mem_write(MTK, ptr, text, len);
    return ptr;
}

static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address,
                                       uint32_t size, void *user_data)
{
    /* Intentionally unused: whole-pool UC_HOOK_CODE removed (see
     * add_manager_code_hooks).  Kept so the symbol still resolves if an
     * old path references it. */
    (void)uc;
    (void)address;
    (void)size;
    (void)user_data;
}

static u32 vm_net_mock_sync_buffer_to_vm(const u8 *buffer, u32 bufferLen)
{
    u32 responsePtr;
    if (buffer == NULL || bufferLen == 0)
        return 0;
    responsePtr = vm_malloc(bufferLen);
    if (responsePtr == 0)
        return 0;
    if (uc_mem_write(MTK, responsePtr, buffer, bufferLen) != UC_ERR_OK)
        return 0;
    return responsePtr;
}

typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    u16 payloadLen;
} vm_client_wt_object;

static bool vm_client_next_wt_object(const u8 *packet, u32 packetLen,
                                     u32 *offset, vm_client_wt_object *object)
{
    u32 start;
    u16 objectLen;
    if (packet == NULL || offset == NULL || *offset < 4 || *offset + 5 > packetLen)
        return false;
    start = *offset;
    objectLen = (u16)(((u16)packet[start + 3] << 8) | packet[start + 4]);
    if (objectLen < 5 || start + objectLen > packetLen)
        return false;
    if (object != NULL)
    {
        object->major = packet[start];
        object->kind = packet[start + 1];
        object->subtype = packet[start + 2];
        object->payloadLen = (u16)(objectLen - 5);
    }
    *offset = start + objectLen;
    return true;
}

/*
 * Title role-select is WT 1/1/6 with actorId/actorID.  Bind dream HUD visit
 * remain to that role so shop-return poke cannot borrow another character's
 * visit_ms (or reset to full 60 after a garbage SCE clear).
 */
static bool vm_client_packet_be32_field(const u8 *packet, u32 packetLen,
                                        const char *field, u32 *outValue)
{
    u32 fieldLen;
    if (packet == NULL || field == NULL || packetLen < 6 || outValue == NULL)
        return false;
    *outValue = 0;
    fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff)
        return false;
    for (u32 i = 0; i + fieldLen + 5 <= packetLen; ++i)
    {
        u32 p;
        u16 valueLen;
        if (packet[i] != (u8)fieldLen)
            continue;
        if (memcmp(packet + i + 1, field, fieldLen) != 0)
            continue;
        p = i + 1 + fieldLen;
        if (p + 2 > packetLen)
            continue;
        valueLen = (u16)(((u16)packet[p] << 8) | packet[p + 1]);
        p += 2;
        if (valueLen < 4 || p + valueLen > packetLen)
            continue;
        *outValue = ((u32)packet[p] << 24) | ((u32)packet[p + 1] << 16) |
                    ((u32)packet[p + 2] << 8) | (u32)packet[p + 3];
        return *outValue != 0;
    }
    return false;
}

static void vm_client_cbe_bag_defer_clear(void);

static void vm_client_note_title_role_select(const u8 *packet, u32 packetLen)
{
    u32 offset = 4;
    vm_client_wt_object object;
    u32 actorId = 0;
    bool haveSelect6 = false;

    if (packet == NULL || packetLen < 10 || packet[0] != 'W' || packet[1] != 'T')
        return;
    while (offset + 5 <= packetLen &&
           vm_client_next_wt_object(packet, packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 1 && object.subtype == 6)
            haveSelect6 = true;
    }
    if (!haveSelect6)
        return;
    if (!vm_client_packet_be32_field(packet, packetLen, "actorID", &actorId) &&
        !vm_client_packet_be32_field(packet, packetLen, "actorid", &actorId) &&
        !vm_client_packet_be32_field(packet, packetLen, "actorId", &actorId))
        return;
    /* Fresh item-manager lifecycle — drop any pending deferred 30/21. */
    vm_client_cbe_bag_defer_clear();
    agent_dbg0b451a_note_active_role(actorId);
}


typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    const u8 *payload;
    u16 payloadLen;
} vm_client_response_object;

/* Response objects use the six-byte header from vm_net_mock_begin_wt_object. */
static bool vm_client_next_response_object(const u8 *response, u32 responseLen,
                                           u32 *offset,
                                           vm_client_response_object *object)
{
    u32 objectStart;
    u16 objectLen;

    if (response == NULL || offset == NULL || *offset < 5 ||
        *offset + 6 > responseLen)
    {
        return false;
    }
    objectStart = *offset;
    objectLen = (u16)(((u16)response[objectStart + 4] << 8) |
                      response[objectStart + 5]);
    if (objectLen < 6 || objectStart + objectLen > responseLen)
        return false;
    if (object != NULL)
    {
        object->major = response[objectStart];
        object->kind = response[objectStart + 1];
        object->subtype = response[objectStart + 2];
        object->payload = response + objectStart + 6;
        object->payloadLen = (u16)(objectLen - 6);
    }
    *offset = objectStart + objectLen;
    return true;
}

static bool vm_client_response_object_field(const vm_client_response_object *object,
                                            const char *field,
                                            const u8 **encoded,
                                            u16 *encodedLen)
{
    u32 pos = 0;
    u32 fieldLen = field ? (u32)strlen(field) : 0;

    if (encoded != NULL)
        *encoded = NULL;
    if (encodedLen != NULL)
        *encodedLen = 0;
    if (object == NULL || object->payload == NULL || fieldLen == 0 ||
        fieldLen > 0xff)
    {
        return false;
    }
    while (pos < object->payloadLen)
    {
        u32 nameLen = object->payload[pos++];
        u16 valueLen = 0;
        if (nameLen > object->payloadLen - pos ||
            object->payloadLen - pos - nameLen < 2)
        {
            return false;
        }
        if (nameLen == fieldLen &&
            memcmp(object->payload + pos, field, fieldLen) == 0)
        {
            pos += nameLen;
            valueLen = (u16)(((u16)object->payload[pos] << 8) |
                             object->payload[pos + 1]);
            pos += 2;
            if (valueLen > object->payloadLen - pos)
                return false;
            if (encoded != NULL)
                *encoded = object->payload + pos;
            if (encodedLen != NULL)
                *encodedLen = valueLen;
            return true;
        }
        pos += nameLen;
        valueLen = (u16)(((u16)object->payload[pos] << 8) |
                         object->payload[pos + 1]);
        pos += 2;
        if (valueLen > object->payloadLen - pos)
            return false;
        pos += valueLen;
    }
    return false;
}

static bool vm_client_response_object_string(const vm_client_response_object *object,
                                             const char *field, char *value,
                                             size_t valueCap)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;
    u16 textLen = 0;
    size_t copyLen = 0;

    if (value == NULL || valueCap == 0)
        return false;
    value[0] = 0;
    if (!vm_client_response_object_field(object, field, &encoded, &encodedLen) ||
        encodedLen < 2)
    {
        return false;
    }
    textLen = (u16)(((u16)encoded[0] << 8) | encoded[1]);
    if ((u32)textLen + 2u != encodedLen)
        return false;
    copyLen = textLen;
    if (copyLen >= valueCap)
        copyLen = valueCap - 1;
    while (copyLen > 0 && encoded[2 + copyLen - 1] == 0)
        --copyLen;
    if (copyLen > 0)
        memcpy(value, encoded + 2, copyLen);
    value[copyLen] = 0;
    return copyLen > 0;
}

static bool vm_client_response_object_posinfo(const vm_client_response_object *object,
                                              u16 *x, u16 *y)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;

    if (!vm_client_response_object_field(object, "posinfo", &encoded,
                                         &encodedLen) ||
        encodedLen != 8 || encoded[0] != 0 || encoded[1] != 2 ||
        encoded[4] != 0 || encoded[5] != 2)
    {
        return false;
    }
    if (x != NULL)
        *x = (u16)(((u16)encoded[2] << 8) | encoded[3]);
    if (y != NULL)
        *y = (u16)(((u16)encoded[6] << 8) | encoded[7]);
    return true;
}

static void vm_client_capture_remote_observation(const u8 *response,
                                                 u32 responseLen,
                                                 vm_net_remote_observation *observation)
{
    u32 packetLen = 0;
    u32 offset = 5;
    u8 objectCount = 0;
    u8 parsedObjects = 0;
    vm_client_response_object object;
    bool hasEnterWithPosinfo = false;

    if (observation == NULL)
        return;
    memset(observation, 0, sizeof(*observation));
    if (response == NULL || responseLen < 5 || response[0] != 'W' ||
        response[1] != 'T')
    {
        return;
    }
    packetLen = ((u32)response[2] << 8) | response[3];
    if (packetLen < 5 || packetLen > responseLen)
        return;
    objectCount = response[4];
    /*
     * First pass: detect 30/1 {scene,posinfo}.  Same-packet outdoor clear
     * 30/2-no-posinfo must not arm clear-after when instance enter 30/1 is
     * also present (dream re-enter drain+30/1, 2026-07-31).
     */
    while (parsedObjects < objectCount &&
           vm_client_next_response_object(response, packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 30 && object.subtype == 1)
        {
            char scene[64];
            u16 x = 0;
            u16 y = 0;

            if (vm_client_response_object_string(&object, "scene", scene,
                                                 sizeof(scene)) &&
                vm_client_response_object_posinfo(&object, &x, &y))
            {
                hasEnterWithPosinfo = true;
            }
        }
        ++parsedObjects;
    }
    offset = 5;
    parsedObjects = 0;
    while (parsedObjects < objectCount &&
           vm_client_next_response_object(response, packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 30 &&
            (object.subtype == 1 || object.subtype == 2))
        {
            char scene[64];
            u16 x = 0;
            u16 y = 0;
            bool haveScene =
                vm_client_response_object_string(&object, "scene", scene,
                                                 sizeof(scene));
            bool havePos = vm_client_response_object_posinfo(&object, &x, &y);

            if (haveScene && havePos)
            {
                observation->hasSceneTarget = 1;
                observation->sceneSubtype = object.subtype;
                observation->sceneX = x;
                observation->sceneY = y;
                snprintf(observation->scene, sizeof(observation->scene), "%s",
                         scene);
                if (object.subtype == 2)
                    observation->sceneCompleteAfterCallback = 1;
            }
            else if (object.subtype == 2 && !hasEnterWithPosinfo)
            {
                observation->sceneCompleteAfterCallback = 1;
                if (haveScene)
                    snprintf(observation->scene, sizeof(observation->scene),
                             "%s", scene);
            }
        }
        ++parsedObjects;
    }
}

#ifdef CBE_PLATFORM_ANDROID
/*
 * Title no-account "start" sends WT 1/1/12 with empty username/password.  The
 * service historically auto-issued guestXXXX credentials there.  On Android
 * we cancel that path and tip the player to use 「登录游戏」 first.
 */
static volatile int g_androidAccountWebOpenSerial = 0;
static volatile int g_androidAccountWebOpenPending = 0;
static u32 g_androidAccountWebOpenLastMs = 0;
static char g_androidAccountWebOpenReason[48];
static volatile int g_androidRechargeBlockedTipSerial = 0;
static volatile int g_androidRechargeBlockedTipPending = 0;
static u32 g_androidRechargeBlockedTipLastMs = 0;
static volatile int g_androidNeedLoginTipSerial = 0;
static volatile int g_androidNeedLoginTipPending = 0;
static u32 g_androidNeedLoginTipLastMs = 0;

int cbeAndroidConsumeAccountWebOpenRequest(void)
{
    if (!g_androidAccountWebOpenPending)
        return 0;
    g_androidAccountWebOpenPending = 0;
    return g_androidAccountWebOpenSerial;
}

const char *cbeAndroidGetAccountWebOpenReason(void)
{
    return g_androidAccountWebOpenReason;
}

int cbeAndroidConsumeRechargeBlockedTip(void)
{
    if (!g_androidRechargeBlockedTipPending)
        return 0;
    g_androidRechargeBlockedTipPending = 0;
    return g_androidRechargeBlockedTipSerial;
}

int cbeAndroidConsumeNeedLoginTip(void)
{
    if (!g_androidNeedLoginTipPending)
        return 0;
    g_androidNeedLoginTipPending = 0;
    return g_androidNeedLoginTipSerial;
}

static void vm_android_request_account_web_open(const char *reason)
{
    u32 now = SDL_GetTicks();
    if (g_androidAccountWebOpenLastMs != 0 &&
        now - g_androidAccountWebOpenLastMs < 2500u)
        return;
    g_androidAccountWebOpenLastMs = now;
    ++g_androidAccountWebOpenSerial;
    if (g_androidAccountWebOpenSerial == 0)
        g_androidAccountWebOpenSerial = 1;
    g_androidAccountWebOpenPending = 1;
    if (reason != NULL && reason[0] != 0)
        snprintf(g_androidAccountWebOpenReason,
                 sizeof(g_androidAccountWebOpenReason), "%s", reason);
    else
        g_androidAccountWebOpenReason[0] = 0;
    printf("[info][network] redirected to account_web reason=%s serial=%d\n",
           reason && reason[0] ? reason : "-",
           g_androidAccountWebOpenSerial);
}

/* Mall「充值」is blocked in-emulator; only tip the player to use 账号中心. */
static void vm_android_request_recharge_blocked_tip(const char *reason)
{
    u32 now = SDL_GetTicks();
    if (g_androidRechargeBlockedTipLastMs != 0 &&
        now - g_androidRechargeBlockedTipLastMs < 2500u)
        return;
    g_androidRechargeBlockedTipLastMs = now;
    ++g_androidRechargeBlockedTipSerial;
    if (g_androidRechargeBlockedTipSerial == 0)
        g_androidRechargeBlockedTipSerial = 1;
    g_androidRechargeBlockedTipPending = 1;
    printf("[info][wpay] recharge blocked tip reason=%s serial=%d\n",
           reason && reason[0] ? reason : "-",
           g_androidRechargeBlockedTipSerial);
}

static void vm_android_request_need_login_tip(const char *reason)
{
    u32 now = SDL_GetTicks();
    if (g_androidNeedLoginTipLastMs != 0 &&
        now - g_androidNeedLoginTipLastMs < 2500u)
        return;
    g_androidNeedLoginTipLastMs = now;
    ++g_androidNeedLoginTipSerial;
    if (g_androidNeedLoginTipSerial == 0)
        g_androidNeedLoginTipSerial = 1;
    g_androidNeedLoginTipPending = 1;
    printf("[info][network] need_login tip reason=%s serial=%d\n",
           reason && reason[0] ? reason : "-",
           g_androidNeedLoginTipSerial);
}

static bool vm_client_packet_string_field_empty(const u8 *packet, u32 packetLen,
                                               const char *field)
{
    u32 fieldLen;
    if (packet == NULL || field == NULL || packetLen < 6)
        return false;
    fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff)
        return false;
    for (u32 i = 0; i + fieldLen + 5 <= packetLen; ++i)
    {
        u32 p;
        u16 valueLen;
        if (packet[i] != (u8)fieldLen)
            continue;
        if (memcmp(packet + i + 1, field, fieldLen) != 0)
            continue;
        p = i + 1 + fieldLen;
        if (p + 2 > packetLen)
            continue;
        valueLen = (u16)(((u16)packet[p] << 8) | packet[p + 1]);
        p += 2;
        if (valueLen == 0)
            return true;
        if (p + valueLen > packetLen)
            continue;
        if (valueLen >= 2)
        {
            u16 blobLen = (u16)(((u16)packet[p] << 8) | packet[p + 1]);
            if ((u32)blobLen + 2 == valueLen)
            {
                u32 copyLen = blobLen;
                while (copyLen > 0 && packet[p + 2 + copyLen - 1] == 0)
                    --copyLen;
                return copyLen == 0;
            }
        }
        {
            u32 copyLen = valueLen;
            while (copyLen > 0 && packet[p + copyLen - 1] == 0)
                --copyLen;
            return copyLen == 0;
        }
    }
    return false;
}

static bool vm_client_is_android_no_account_login(const u8 *packet, u32 packetLen)
{
    u32 offset = 4;
    vm_client_wt_object object;
    bool haveLogin12 = false;
    bool haveEmptyUser = false;
    bool haveEmptyPass = false;

    if (packet == NULL || packetLen < 10 || packet[0] != 'W' || packet[1] != 'T')
        return false;
    while (offset + 5 <= packetLen &&
           vm_client_next_wt_object(packet, packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 1 && object.subtype == 12)
            haveLogin12 = true;
    }
    if (!haveLogin12)
        return false;
    haveEmptyUser =
        vm_client_packet_string_field_empty(packet, packetLen, "userName") ||
        vm_client_packet_string_field_empty(packet, packetLen, "username");
    haveEmptyPass =
        vm_client_packet_string_field_empty(packet, packetLen, "password");
    return haveEmptyUser && haveEmptyPass;
}
#endif

static void vm_client_finish_wt_packet(u8 *packet, u32 len, u8 objectCount)
{
    packet[0] = 'W';
    packet[1] = 'T';
    packet[2] = (u8)(len >> 8);
    packet[3] = (u8)len;
    packet[4] = objectCount;
}

/*
 * Pixi-only WT objects that CBE cannot dispatch (docs/re/2026-08-17-pixi-cosmetics-1-31.md).
 * Scene-sync poll appends peer 31/5 and may push object count past the CBE cap of 10,
 * which also prevents later objects in the same packet (e.g. 30/21 / 17/1) from applying.
 */
static bool vm_client_wt_object_is_cbe_unsupported(
    const vm_client_response_object *object)
{
    return object != NULL && object->major == 1 &&
           (object->kind == 31 || object->kind == 32);
}

static bool vm_client_strip_cbe_unsupported_objects(u8 **packet, u32 *packetLen);

static bool vm_client_cbe_clamp_backpack_packet(u8 **response, u32 *responseLen);

static bool vm_client_cbe_patch_actorinfo_packet(u8 **packet, u32 *packetLen);

static bool vm_client_decode_object_u8(const vm_client_response_object *object,
                                       const char *field, u8 *valueOut);

static bool vm_client_decode_object_u32(const vm_client_response_object *object,
                                        const char *field, u32 *valueOut);

static void vm_client_cbe_bag_defer_clear(void);
static bool vm_client_cbe_bag_defer_strip_login_grid(u8 **packet, u32 *packetLen);
static bool vm_client_cbe_bag_defer_flush(u32 connectId);
static u32 vm_client_deliver_wt_response(
    u8 *packet,
    u32 packetLen,
    u32 eventType,
    u32 callback,
    u32 context,
    u32 connectId,
    bool attachRemoteObservation,
    bool updateNetMockScratch);

enum
{
    /*
     * CBE backpack ceiling after 2026-10-08 patches:
     * - stream_read_u8 ldrsb→ldrb (0x01033ABC): capacity 128..255 safe
     * - InitTimerControl(255,255); host MF pool 0x14000→0x40000
     * - actorinfo capacity remains tagged u8 (max 255); true 256 needs i16
     */
    VM_CLIENT_CBE_BACKPACK_SLOTS = 255,
    /*
     * Login 30/21 above this row count is stripped from the group-type1 WT and
     * delivered once after scene poll — Unicorn HandleItemGrid on 8KB+ stalls
     * enter-game; Pixi only pays JS parse on openBag.  Never send a second
     * full 30/21 (duplicates stacks).
     */
    VM_CLIENT_CBE_BAG_DEFER_ROW_THRESHOLD = 40
};

static bool vm_client_seq_skip_tagged(const u8 *buf, u32 len, u32 *pos);
static bool vm_client_put_object_field(u8 *out, u32 outCap, u32 *pos,
                                       const char *name, const u8 *value,
                                       u16 valueLen);
static bool vm_client_object_field_pos(const u8 *packet, u32 packetLen,
                                       u32 objectStart, const char *field,
                                       u32 *valuePosOut, u16 *valueLenOut);
static bool vm_client_cbe_item_is_client_grid_item(u32 itemId);
static u32 vm_client_cbe_grid_wire_count(u32 itemId, u32 count);
static bool vm_client_seq_read_tagged_u32(const u8 *buf, u32 len, u32 *pos,
                                          u32 *valueOut);
static bool vm_client_seq_read_tagged_i16(const u8 *buf, u32 len, u32 *pos,
                                          u16 *valueOut);
static bool vm_client_seq_put_tagged_u32(u8 *out, u32 outCap, u32 *pos,
                                         u32 value);
static bool vm_client_seq_put_tagged_i16(u8 *out, u32 outCap, u32 *pos,
                                         u16 value);
static bool vm_client_seq_put_tagged_u32_be32(u8 *out, u32 outCap, u32 *pos,
                                              u32 value);
static bool vm_client_seq_put_compact_extra(u8 *out, u32 outCap, u32 *pos);
static bool vm_client_seq_put_compact_extra_vals(u8 *out, u32 outCap, u32 *pos,
                                                u16 enhanceLevel,
                                                u16 enhanceMax);

static bool vm_client_seq_skip_string(const u8 *buf, u32 len, u32 *pos)
{
    u16 strLen;

    if (buf == NULL || pos == NULL || *pos + 2u > len)
        return false;
    strLen = (u16)(((u16)buf[*pos] << 8) | buf[*pos + 1u]);
    *pos += 2u;
    if (*pos + strLen > len)
        return false;
    *pos += strLen;
    return true;
}

static bool vm_client_seq_skip_u8_compat(const u8 *buf, u32 len, u32 *pos)
{
    if (buf == NULL || pos == NULL)
        return false;
    if (*pos + 3u <= len && buf[*pos] == 0 && buf[*pos + 1u] == 1)
    {
        *pos += 3u;
        return true;
    }
    if (*pos + 1u <= len)
    {
        (*pos)++;
        return true;
    }
    return false;
}

/*
 * Walk parse_actorinfo_playerinfo_blob layout to the backpackCapacity slot.
 * Fresh CBE blob: tagged u8 capacity after money.  Live/Pixi blob omits it.
 */
static bool vm_client_actorinfo_cap_site(const u8 *blob, u32 blobLen,
                                          u32 *capValuePosOut,
                                          bool *hasFreshCapOut)
{
    u32 pos = 0;
    u32 i;

    if (capValuePosOut != NULL)
        *capValuePosOut = 0;
    if (hasFreshCapOut != NULL)
        *hasFreshCapOut = false;
    if (blob == NULL || blobLen < 48u)
        return false;
    if (!vm_client_seq_skip_tagged(blob, blobLen, &pos))
        return false;
    if (!vm_client_seq_skip_u8_compat(blob, blobLen, &pos) ||
        !vm_client_seq_skip_u8_compat(blob, blobLen, &pos))
        return false;
    if (!vm_client_seq_skip_string(blob, blobLen, &pos))
        return false;
    if (!vm_client_seq_skip_tagged(blob, blobLen, &pos))
        return false;
    if (!vm_client_seq_skip_string(blob, blobLen, &pos))
        return false;
    if (!vm_client_seq_skip_tagged(blob, blobLen, &pos))
        return false;
    for (i = 0; i < 4u; ++i)
    {
        if (!vm_client_seq_skip_tagged(blob, blobLen, &pos))
            return false;
    }
    if (!vm_client_seq_skip_tagged(blob, blobLen, &pos))
        return false;
    for (i = 0; i < 6u; ++i)
    {
        if (!vm_client_seq_skip_tagged(blob, blobLen, &pos))
            return false;
    }
    if (!vm_client_seq_skip_tagged(blob, blobLen, &pos) ||
        !vm_client_seq_skip_tagged(blob, blobLen, &pos))
        return false;
    if (pos + 2u > blobLen)
        return false;
    if (blob[pos] == 0 && blob[pos + 1u] == 1)
    {
        if (capValuePosOut != NULL)
            *capValuePosOut = pos + 2u;
        if (hasFreshCapOut != NULL)
            *hasFreshCapOut = true;
        return true;
    }
    if (capValuePosOut != NULL)
        *capValuePosOut = pos;
    if (hasFreshCapOut != NULL)
        *hasFreshCapOut = false;
    return true;
}

static bool vm_client_patch_actorinfo_blob(const u8 *in, u32 inLen,
                                           u8 *out, u32 outCap, u32 *outLen,
                                           u8 *oldCapOut, bool *insertedOut)
{
    u32 capSite = 0;
    bool hasFresh = false;
    u8 newCap = (u8)VM_CLIENT_CBE_BACKPACK_SLOTS;

    if (oldCapOut != NULL)
        *oldCapOut = 0;
    if (insertedOut != NULL)
        *insertedOut = false;
    if (in == NULL || out == NULL || outLen == NULL)
        return false;
    if (!vm_client_actorinfo_cap_site(in, inLen, &capSite, &hasFresh))
        return false;
    if (hasFresh)
    {
        u8 oldCap = in[capSite];

        if (oldCapOut != NULL)
            *oldCapOut = oldCap;
        if (oldCap != 0 && oldCap <= newCap)
            return false;
        if (outCap < inLen)
            return false;
        memcpy(out, in, inLen);
        out[capSite] = newCap;
        *outLen = inLen;
        return true;
    }
    if (outCap < inLen + 3u)
        return false;
    memcpy(out, in, capSite);
    out[capSite] = 0;
    out[capSite + 1u] = 1;
    out[capSite + 2u] = newCap;
    memcpy(out + capSite + 3u, in + capSite, inLen - capSite);
    *outLen = inLen + 3u;
    if (insertedOut != NULL)
        *insertedOut = true;
    return true;
}

static bool vm_client_put_object_entry_raw(u8 *out, u32 outCap, u32 *pos,
                                            const u8 *name, u8 nameLen,
                                            const u8 *value, u16 valueLen)
{
    if (out == NULL || pos == NULL || name == NULL || value == NULL)
        return false;
    if (*pos + 1u + nameLen + 2u + valueLen > outCap)
        return false;
    out[(*pos)++] = nameLen;
    memcpy(out + *pos, name, nameLen);
    *pos += nameLen;
    out[(*pos)++] = (u8)(valueLen >> 8);
    out[(*pos)++] = (u8)valueLen;
    memcpy(out + *pos, value, valueLen);
    *pos += valueLen;
    return true;
}

static bool vm_client_rewrite_object_actorinfo(const vm_client_response_object *object,
                                                const u8 *newActorinfo,
                                                u16 newActorinfoLen,
                                                u8 *outObj, u32 outCap,
                                                u32 *outLen)
{
    u32 pos = 0;
    u32 srcPos = 0;
    u32 objectStart = 0;
    const u8 *payload = object->payload;
    u32 payloadLen = object->payloadLen;

    if (outObj == NULL || outLen == NULL || object == NULL || payload == NULL)
        return false;
    objectStart = pos;
    outObj[pos++] = object->major;
    outObj[pos++] = object->kind;
    outObj[pos++] = object->subtype;
    outObj[pos++] = 0;
    outObj[pos++] = 0;
    outObj[pos++] = 0;
    while (srcPos < payloadLen)
    {
        u8 nameLen;
        u16 valueLen;
        const u8 *value;

        if (srcPos + 1u > payloadLen)
            return false;
        nameLen = payload[srcPos++];
        if (srcPos + nameLen + 2u > payloadLen)
            return false;
        valueLen = (u16)(((u16)payload[srcPos + nameLen] << 8) |
                          payload[srcPos + nameLen + 1u]);
        value = payload + srcPos + nameLen + 2u;
        if (srcPos + nameLen + 2u + valueLen > payloadLen)
            return false;
        if (nameLen == 9 && memcmp(payload + srcPos, "actorinfo", 9) == 0)
        {
            if (!vm_client_put_object_field(outObj, outCap, &pos, "actorinfo",
                                            newActorinfo, newActorinfoLen))
                return false;
        }
        else if (!vm_client_put_object_entry_raw(outObj, outCap, &pos,
                                                  payload + srcPos, nameLen,
                                                  value, valueLen))
        {
            return false;
        }
        srcPos += nameLen + 2u + valueLen;
    }
    outObj[objectStart + 4] = (u8)(pos >> 8);
    outObj[objectStart + 5] = (u8)pos;
    *outLen = pos;
    return true;
}

static bool vm_client_cbe_patch_actorinfo_packet(u8 **packet, u32 *packetLen)
{
    u32 offset = 5;
    u8 *src = NULL;
    bool needRebuild = false;
    bool needInplace = false;
    vm_client_response_object object;

    if (packet == NULL || *packet == NULL || packetLen == NULL ||
        *packetLen < 5 || (*packet)[0] != 'W' || (*packet)[1] != 'T')
        return false;
    src = *packet;

    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(src, *packetLen, &offset, &object))
    {
        const u8 *actorinfo = NULL;
        u16 actorinfoLen = 0;
        u8 patched[2048];
        u32 patchedLen = 0;
        u8 oldCap = 0;
        bool inserted = false;

        if (object.major != 1 || object.kind != 1 ||
            (object.subtype != 6 && object.subtype != 14) ||
            !vm_client_response_object_field(&object, "actorinfo", &actorinfo,
                                             &actorinfoLen) ||
            !vm_client_patch_actorinfo_blob(actorinfo, actorinfoLen, patched,
                                            sizeof(patched), &patchedLen,
                                            &oldCap, &inserted))
        {
            continue;
        }
        if (inserted)
            needRebuild = true;
        else
            needInplace = true;
    }
    if (!needRebuild && !needInplace)
        return false;

    if (!needRebuild)
    {
        offset = 5;
        while (offset + 6u <= *packetLen &&
               vm_client_next_response_object(src, *packetLen, &offset, &object))
        {
            const u8 *actorinfo = NULL;
            u16 actorinfoLen = 0;
            u8 patched[2048];
            u32 patchedLen = 0;
            u8 oldCap = 0;
            bool inserted = false;
            u32 objectStart = offset - ((u32)object.payloadLen + 6u);
            u32 fieldPos = 0;
            u16 fieldLen = 0;
            u32 capSite = 0;
            bool hasFresh = false;

            if (object.major != 1 || object.kind != 1 ||
                (object.subtype != 6 && object.subtype != 14) ||
                !vm_client_response_object_field(&object, "actorinfo", &actorinfo,
                                                 &actorinfoLen) ||
                !vm_client_patch_actorinfo_blob(actorinfo, actorinfoLen, patched,
                                                sizeof(patched), &patchedLen,
                                                &oldCap, &inserted) ||
                inserted ||
                !vm_client_object_field_pos(src, *packetLen, objectStart,
                                            "actorinfo", &fieldPos, &fieldLen) ||
                fieldLen != actorinfoLen ||
                !vm_client_actorinfo_cap_site(actorinfo, actorinfoLen, &capSite,
                                              &hasFresh) ||
                !hasFresh)
            {
                continue;
            }
            (*packet)[fieldPos + capSite] = (u8)VM_CLIENT_CBE_BACKPACK_SLOTS;
            printf("[info][network] cbe_actorinfo_cap object=1/1/%u old=%u "
                   "new=64 mode=inplace-fresh "
                   "evidence=parse_actorinfo-manager+38\n",
                   object.subtype, oldCap);
        }
        return true;
    }

    {
        u32 outPos = 5;
        u8 objectCount = 0;
        u8 *out = NULL;
        u32 outCap = *packetLen + 128u;

        out = (u8 *)malloc(outCap);
        if (out == NULL)
            return false;
        out[0] = 'W';
        out[1] = 'T';
        out[2] = 0;
        out[3] = 0;
        out[4] = 0;
        offset = 5;
        while (offset + 6u <= *packetLen &&
               vm_client_next_response_object(src, *packetLen, &offset, &object))
        {
            const u8 *actorinfo = NULL;
            u16 actorinfoLen = 0;
            u8 patched[2048];
            u32 patchedLen = 0;
            u8 oldCap = 0;
            bool inserted = false;
            const u8 *copySrc = src + offset - ((u32)object.payloadLen + 6u);
            u32 copyLen = (u32)object.payloadLen + 6u;
            u8 *rewritten = NULL;
            u32 rewrittenCap = (u32)object.payloadLen + 96u;
            u32 rewrittenLen = 0;

            if (object.major == 1 && object.kind == 1 &&
                (object.subtype == 6 || object.subtype == 14) &&
                vm_client_response_object_field(&object, "actorinfo", &actorinfo,
                                                &actorinfoLen) &&
                vm_client_patch_actorinfo_blob(actorinfo, actorinfoLen, patched,
                                               sizeof(patched), &patchedLen,
                                               &oldCap, &inserted) &&
                inserted)
            {
                rewritten = (u8 *)malloc(rewrittenCap);
                if (rewritten == NULL ||
                    !vm_client_rewrite_object_actorinfo(&object, patched,
                                                        (u16)patchedLen,
                                                        rewritten, rewrittenCap,
                                                        &rewrittenLen))
                {
                    free(rewritten);
                    free(out);
                    return false;
                }
                copySrc = rewritten;
                copyLen = rewrittenLen;
                printf("[info][network] cbe_actorinfo_cap object=1/1/%u old=%u "
                       "new=64 mode=insert-live "
                       "evidence=parse_actorinfo-manager+38\n",
                       object.subtype, oldCap);
            }
            if (outPos + copyLen > outCap || objectCount == 0xff)
            {
                free(rewritten);
                free(out);
                return false;
            }
            memcpy(out + outPos, copySrc, copyLen);
            outPos += copyLen;
            ++objectCount;
            free(rewritten);
        }
        if (offset != *packetLen)
        {
            free(out);
            return false;
        }
        vm_client_finish_wt_packet(out, outPos, objectCount);
        if (outPos > *packetLen)
        {
            u8 *grown = (u8 *)realloc(*packet, outPos);
            if (grown == NULL)
            {
                free(out);
                return false;
            }
            *packet = grown;
        }
        memcpy(*packet, out, outPos);
        *packetLen = outPos;
        free(out);
        return true;
    }
}

/*
 * CBE InitTimerControl raised to (255,255).  Live/Pixi may publish 256; CBE
 * actorinfo capacity is still u8 so clamp at 255.  Leave server storage
 * untouched (docs/re/2026-10-08-cbe-backpack-255-init-patch.md).
 */

static bool vm_client_seq_skip_tagged(const u8 *buf, u32 len, u32 *pos)
{
    u8 size;

    if (buf == NULL || pos == NULL || *pos + 2u > len || buf[*pos] != 0)
        return false;
    size = buf[*pos + 1u];
    if (size == 0 || *pos + 2u + size > len)
        return false;
    *pos += 2u + size;
    return true;
}

static bool vm_client_seq_skip_common_extra(const u8 *buf, u32 len, u32 *pos)
{
    u8 attrCount;
    u32 i;

    if (!vm_client_seq_skip_tagged(buf, len, pos) ||
        !vm_client_seq_skip_tagged(buf, len, pos))
        return false;
    if (*pos + 3u > len || buf[*pos] != 0 || buf[*pos + 1u] != 1)
        return false;
    attrCount = buf[*pos + 2u];
    *pos += 3u;
    for (i = 0; i < attrCount; ++i)
    {
        if (!vm_client_seq_skip_tagged(buf, len, pos) ||
            !vm_client_seq_skip_tagged(buf, len, pos) ||
            !vm_client_seq_skip_tagged(buf, len, pos) ||
            !vm_client_seq_skip_tagged(buf, len, pos))
            return false;
    }
    /*
     * Pixi 装备行在 attr 后跟 gem trailer：tagged u8 0x47 + reflect/combo/revive.
     * CBE 0x418C 停在 attr_count；不跳过则下一格 itemId 错位，800 等后续道具丢失。
     */
    if (*pos + 3u <= len && buf[*pos] == 0 && buf[*pos + 1u] == 1 &&
        buf[*pos + 2u] == 0x47)
    {
        for (i = 0; i < 4u; ++i)
        {
            if (!vm_client_seq_skip_tagged(buf, len, pos))
                return false;
        }
    }
    return true;
}

static bool vm_client_seq_read_row_count(const u8 *buf, u32 len, u32 *pos,
                                         u32 *countOut, u8 *tagSizeOut)
{
    u8 size;

    if (buf == NULL || pos == NULL || countOut == NULL || *pos + 2u > len ||
        buf[*pos] != 0)
        return false;
    size = buf[*pos + 1u];
    if (size == 1)
    {
        if (*pos + 3u > len)
            return false;
        *countOut = buf[*pos + 2u];
        *pos += 3u;
        if (tagSizeOut != NULL)
            *tagSizeOut = 1;
        return true;
    }
    if (size == 2)
    {
        if (*pos + 4u > len)
            return false;
        *countOut = ((u32)buf[*pos + 2u] << 8) | buf[*pos + 3u];
        *pos += 4u;
        if (tagSizeOut != NULL)
            *tagSizeOut = 2;
        return true;
    }
    return false;
}

static bool vm_client_seq_write_row_count(u8 *out, u32 outCap, u32 *pos,
                                          u32 count, u8 tagSize)
{
    if (out == NULL || pos == NULL)
        return false;
    if (tagSize == 1)
    {
        if (count > 0xffu || *pos + 3u > outCap)
            return false;
        out[(*pos)++] = 0;
        out[(*pos)++] = 1;
        out[(*pos)++] = (u8)count;
        return true;
    }
    if (tagSize == 2)
    {
        if (count > 0xffffu || *pos + 4u > outCap)
            return false;
        out[(*pos)++] = 0;
        out[(*pos)++] = 2;
        out[(*pos)++] = (u8)(count >> 8);
        out[(*pos)++] = (u8)count;
        return true;
    }
    return false;
}

static bool vm_client_decode_tagged_u32(const u8 *encoded, u16 encodedLen,
                                        u32 *valueOut)
{
    if (encoded == NULL || valueOut == NULL || encodedLen < 3 ||
        encoded[0] != 0)
        return false;
    if (encoded[1] == 1 && encodedLen == 3)
    {
        *valueOut = encoded[2];
        return true;
    }
    if (encoded[1] == 2 && encodedLen == 4)
    {
        *valueOut = ((u32)encoded[2] << 8) | encoded[3];
        return true;
    }
    if (encoded[1] == 4 && encodedLen == 6)
    {
        *valueOut = ((u32)encoded[2] << 24) | ((u32)encoded[3] << 16) |
                    ((u32)encoded[4] << 8) | encoded[5];
        return true;
    }
    return false;
}

static bool vm_client_encode_tagged_u16(u8 *out, u16 outCap, u16 *outLen,
                                         u16 value, u8 preferSize)
{
    if (out == NULL || outLen == NULL)
        return false;
    if (preferSize == 1 || (preferSize == 0 && value <= 0xffu))
    {
        if (outCap < 3 || value > 0xffu)
            return false;
        out[0] = 0;
        out[1] = 1;
        out[2] = (u8)value;
        *outLen = 3;
        return true;
    }
    if (outCap < 4)
        return false;
    out[0] = 0;
    out[1] = 2;
    out[2] = (u8)(value >> 8);
    out[3] = (u8)value;
    *outLen = 4;
    return true;
}

static bool vm_client_put_object_field(u8 *out, u32 outCap, u32 *pos,
                                       const char *name, const u8 *value,
                                       u16 valueLen)
{
    u32 nameLen = name ? (u32)strlen(name) : 0;

    if (out == NULL || pos == NULL || nameLen == 0 || nameLen > 0xffu ||
        value == NULL)
        return false;
    if (*pos + 1u + nameLen + 2u + valueLen > outCap)
        return false;
    out[(*pos)++] = (u8)nameLen;
    memcpy(out + *pos, name, nameLen);
    *pos += nameLen;
    out[(*pos)++] = (u8)(valueLen >> 8);
    out[(*pos)++] = (u8)valueLen;
    memcpy(out + *pos, value, valueLen);
    *pos += valueLen;
    return true;
}

static bool vm_client_wt_shrink_at(u8 *packet, u32 *packetLen, u32 shrinkAt,
                                   u32 shrinkBy)
{
    if (packet == NULL || packetLen == NULL || shrinkBy == 0 ||
        shrinkAt + shrinkBy > *packetLen)
        return false;
    memmove(packet + shrinkAt, packet + shrinkAt + shrinkBy,
            *packetLen - shrinkAt - shrinkBy);
    *packetLen -= shrinkBy;
    vm_client_finish_wt_packet(packet, *packetLen, packet[4]);
    return true;
}

static void vm_client_wt_patch_object_len(u8 *packet, u32 objectStart, int delta)
{
    u32 oldLen = ((u32)packet[objectStart + 4u] << 8) |
                 packet[objectStart + 5u];
    u32 newLen = (u32)((int)oldLen + delta);

    packet[objectStart + 4u] = (u8)(newLen >> 8);
    packet[objectStart + 5u] = (u8)newLen;
}

static bool vm_client_object_field_pos(const u8 *packet, u32 packetLen,
                                       u32 objectStart, const char *field,
                                       u32 *valuePosOut, u16 *valueLenOut)
{
    u32 fieldLen = field ? (u32)strlen(field) : 0;
    u32 objectLen;
    u32 pos;
    u32 end;

    if (valuePosOut != NULL)
        *valuePosOut = 0;
    if (valueLenOut != NULL)
        *valueLenOut = 0;
    if (packet == NULL || fieldLen == 0 || fieldLen > 0xffu ||
        objectStart + 6u > packetLen)
        return false;
    objectLen = ((u32)packet[objectStart + 4u] << 8) |
                packet[objectStart + 5u];
    if (objectStart + objectLen > packetLen)
        return false;
    pos = objectStart + 6u;
    end = objectStart + objectLen;
    while (pos + 1u + fieldLen + 2u <= end)
    {
        u8 nameLen = packet[pos++];
        u16 valueLen;
        if (nameLen != fieldLen ||
            memcmp(packet + pos, field, fieldLen) != 0)
        {
            pos += nameLen;
            if (pos + 2u > end)
                return false;
            valueLen = (u16)(((u16)packet[pos] << 8) | packet[pos + 1u]);
            pos += 2u + valueLen;
            continue;
        }
        pos += fieldLen;
        valueLen = (u16)(((u16)packet[pos] << 8) | packet[pos + 1u]);
        pos += 2u;
        if (pos + valueLen > end)
            return false;
        if (valuePosOut != NULL)
            *valuePosOut = pos;
        if (valueLenOut != NULL)
            *valueLenOut = valueLen;
        return true;
    }
    return false;
}

/*
 * Coerce tagged-i16 (00 02 hi lo) to tagged-u8 (00 01 lo) inside a named WT
 * field WITHOUT shrinking packet/object lengths.  Trailing pad byte stays in
 * the 4-byte value region; field walkers skip by declared valueLen so the
 * next named field stays aligned.
 *
 * Shrinking gridnum (i16→u8 + memmove) crashed login at pc=0x01031ef4 /
 * null+0x11a whether compound or solo-split (client_out 2026-08-24).
 */
static bool vm_client_coerce_field_tagged_i16_to_u8_pad(u8 *packet,
                                                       u32 packetLen,
                                                       u32 valuePos,
                                                       u16 valueLen,
                                                       u32 newValue)
{
    if (packet == NULL || valueLen != 4u || valuePos + 4u > packetLen ||
        packet[valuePos] != 0 || packet[valuePos + 1u] != 2 ||
        newValue > 0xffu)
        return false;
    packet[valuePos] = 0;
    packet[valuePos + 1u] = 1;
    packet[valuePos + 2u] = (u8)newValue;
    packet[valuePos + 3u] = 0;
    return true;
}

/*
 * Shrink tagged-i16 (00 02 hi lo) to tagged-u8 (00 01 lo) inside a WT field.
 * Used for 17/1 maxnum when pad-coerce is insufficient, and as fallback.
 * Prefer pad-coerce for 30/21 gridnum (see above).
 */
static bool vm_client_patch_field_tagged_i16_to_u8(u8 *packet, u32 *packetLen,
                                                   u32 objectStart,
                                                   u32 valuePos, u16 valueLen,
                                                   u32 newValue)
{
    if (packet == NULL || packetLen == NULL || valueLen != 4u ||
        valuePos < 3u || valuePos + 4u > *packetLen ||
        packet[valuePos] != 0 || packet[valuePos + 1u] != 2 ||
        newValue > 0xffu)
        return false;
    packet[valuePos] = 0;
    packet[valuePos + 1u] = 1;
    packet[valuePos + 2u] = (u8)newValue;
    packet[valuePos - 2u] = 0;
    packet[valuePos - 1u] = 3;
    if (!vm_client_wt_shrink_at(packet, packetLen, valuePos + 3u, 1u))
        return false;
    vm_client_wt_patch_object_len(packet, objectStart, -1);
    return true;
}

static bool vm_client_patch_blob_count_i16_to_u8(u8 *packet, u32 *packetLen,
                                                 u32 objectStart,
                                                 u32 valuePos, u16 valueLen,
                                                 u32 *countOut)
{
    u32 count;

    if (packet == NULL || packetLen == NULL || valueLen < 4u ||
        valuePos + 4u > *packetLen || packet[valuePos] != 0 ||
        packet[valuePos + 1u] != 2)
        return false;
    count = ((u32)packet[valuePos + 2u] << 8) | packet[valuePos + 3u];
    if (countOut != NULL)
        *countOut = count;
    packet[valuePos] = 0;
    packet[valuePos + 1u] = 1;
    packet[valuePos + 2u] = (u8)count;
    packet[valuePos - 2u] = (u8)((valueLen - 1u) >> 8);
    packet[valuePos - 1u] = (u8)(valueLen - 1u);
    if (!vm_client_wt_shrink_at(packet, packetLen, valuePos + 3u, 1u))
        return false;
    vm_client_wt_patch_object_len(packet, objectStart, -1);
    return true;
}

static bool vm_client_cbe_clamp_backpack17_inplace(u8 *packet, u32 *packetLen,
                                                   u32 objectStart,
                                                   const vm_client_response_object *object,
                                                   bool *changedOut)
{
    u32 maxnumPos = 0;
    u32 iteminfoPos = 0;
    u16 maxnumLen = 0;
    u16 iteminfoLen = 0;
    u32 maxnumValue = 0;
    u32 rowCount = 0;
    bool changed = false;

    if (changedOut != NULL)
        *changedOut = false;
    if (packet == NULL || packetLen == NULL || object == NULL)
        return false;
    if (!vm_client_object_field_pos(packet, *packetLen, objectStart, "maxnum",
                                    &maxnumPos, &maxnumLen) ||
        !vm_client_object_field_pos(packet, *packetLen, objectStart, "iteminfo",
                                    &iteminfoPos, &iteminfoLen))
        return false;
    if (!vm_client_decode_tagged_u32(packet + maxnumPos, maxnumLen,
                                     &maxnumValue))
        return false;
    /*
     * CBE_EMU mock wires maxnum as tagged i16.  Do not shrink to u8 — that
     * left business+546 at 0xFFFF (UI "/-1").  Only clamp oversized values.
     */
    if (maxnumValue > VM_CLIENT_CBE_BACKPACK_SLOTS &&
        maxnumLen == 4u && packet[maxnumPos] == 0 &&
        packet[maxnumPos + 1u] == 2)
    {
        packet[maxnumPos + 2u] = 0;
        packet[maxnumPos + 3u] = (u8)VM_CLIENT_CBE_BACKPACK_SLOTS;
        maxnumValue = VM_CLIENT_CBE_BACKPACK_SLOTS;
        changed = true;
        printf("[info][network] cbe_backpack_clamp object=17/1 maxnum=%u "
               "maxnum_tag=i16 mode=inplace-value "
               "evidence=CBE_EMU:put_object_u16-maxnum\n",
               maxnumValue);
    }
    if (iteminfoLen >= 4u && packet[iteminfoPos] == 0 &&
        packet[iteminfoPos + 1u] == 2)
    {
        rowCount = ((u32)packet[iteminfoPos + 2u] << 8) |
                   packet[iteminfoPos + 3u];
        if (rowCount > VM_CLIENT_CBE_BACKPACK_SLOTS)
            return false;
        if (vm_client_patch_blob_count_i16_to_u8(packet, packetLen, objectStart,
                                                 iteminfoPos, iteminfoLen,
                                                 &rowCount))
        {
            changed = true;
            printf("[info][network] cbe_backpack_clamp object=17/1 maxnum=%u "
                   "rows=%u count_tag=i16->u8 mode=inplace "
                   "evidence=CBE_EMU:seq_put_u8-count\n",
                   maxnumValue, rowCount);
        }
    }
    if (changedOut != NULL)
        *changedOut = changed;
    return true;
}

static bool vm_client_cbe_clamp_backpack30_inplace(u8 *packet, u32 *packetLen,
                                                   u32 objectStart,
                                                   const vm_client_response_object *object,
                                                   bool *changedOut)
{
    u32 gridnumPos = 0;
    u16 gridnumLen = 0;
    u32 gridnumValue = 0;

    if (changedOut != NULL)
        *changedOut = false;
    if (packet == NULL || packetLen == NULL || object == NULL)
        return false;
    if (!vm_client_object_field_pos(packet, *packetLen, objectStart, "gridnum",
                                    &gridnumPos, &gridnumLen))
        return false;
    if (!vm_client_decode_tagged_u32(packet + gridnumPos, gridnumLen,
                                     &gridnumValue))
        return false;
    if (gridnumValue > VM_CLIENT_CBE_BACKPACK_SLOTS)
        gridnumValue = VM_CLIENT_CBE_BACKPACK_SLOTS;
    /*
     * Pad-coerce only.  Shrinking gridnum crashed at 0x01031ef4 (0x11a) on
     * both compound login packs and solo-split deliveries.
     */
    if (gridnumLen == 4u && packet[gridnumPos] == 0 &&
        packet[gridnumPos + 1u] == 2 &&
        vm_client_coerce_field_tagged_i16_to_u8_pad(
            packet, *packetLen, gridnumPos, gridnumLen, gridnumValue))
    {
        printf("[info][network] cbe_backpack_clamp object=30/21 gridnum=%u "
               "gridnum_tag=i16->u8 mode=pad-coerce iteminfo=unchanged "
               "evidence=HandleItemGridResponse-u8-gridnum-no-shrink\n",
               gridnumValue);
        if (changedOut != NULL)
            *changedOut = true;
    }
    return true;
}

static bool vm_client_seq_read_tagged_i16(const u8 *buf, u32 len, u32 *pos,
                                          u16 *valueOut)
{
    if (buf == NULL || pos == NULL || valueOut == NULL || *pos + 4u > len ||
        buf[*pos] != 0 || buf[*pos + 1u] != 2)
        return false;
    *valueOut = (u16)(((u16)buf[*pos + 2u] << 8) | buf[*pos + 3u]);
    *pos += 4u;
    return true;
}

static bool vm_client_truncate_backpack17_iteminfo(const u8 *in, u16 inLen,
                                                   u8 *out, u32 outCap,
                                                   u16 *outLen, u16 maxRows,
                                                   u16 *keptRows,
                                                   bool *truncatedOut,
                                                   bool *countTagToU8Out)
{
    u32 pos = 0;
    u32 count = 0;
    u8 tagSize = 0;
    u32 outPos = 0;
    u32 countValuePos = 0;
    u32 i;
    u16 keep = 0;

    if (keptRows != NULL)
        *keptRows = 0;
    if (truncatedOut != NULL)
        *truncatedOut = false;
    if (countTagToU8Out != NULL)
        *countTagToU8Out = false;
    if (in == NULL || out == NULL || outLen == NULL || maxRows == 0)
        return false;
    if (!vm_client_seq_read_row_count(in, inLen, &pos, &count, &tagSize))
        return false;
    /*
     * mmGame:0x418C 按 bootstrap-compact 解析 17/1：itemId + i16 + i16 + attr0.
     * Live Pixi 行带强化词条/宝石 trailer；原样拷贝会在装备行后错位，
     * 传送石 800 等后续消耗品进不了背包列表。
     */
    if (!vm_client_seq_write_row_count(out, outCap, &outPos, 0, 1))
        return false;
    countValuePos = outPos - 1u;
    if (countTagToU8Out != NULL)
        *countTagToU8Out = (tagSize != 1);
    for (i = 0; i < count; ++i)
    {
        u32 itemId = 0;
        u16 firstWire = 0;
        u16 secondWire = 0;
        u32 extraPos;

        if (!vm_client_seq_read_tagged_u32(in, inLen, &pos, &itemId))
            return false;
        extraPos = pos;
        if (!vm_client_seq_read_tagged_i16(in, inLen, &pos, &firstWire))
        {
            pos = extraPos;
            if (!vm_client_seq_skip_common_extra(in, inLen, &pos))
                return false;
            firstWire = 0;
            secondWire = 0;
        }
        else if (!vm_client_seq_read_tagged_i16(in, inLen, &pos, &secondWire))
        {
            return false;
        }
        else
        {
            u8 attrCount;
            u32 a;

            if (pos + 3u > inLen || in[pos] != 0 || in[pos + 1u] != 1)
                return false;
            attrCount = in[pos + 2u];
            pos += 3u;
            for (a = 0; a < attrCount; ++a)
            {
                if (!vm_client_seq_skip_tagged(in, inLen, &pos) ||
                    !vm_client_seq_skip_tagged(in, inLen, &pos) ||
                    !vm_client_seq_skip_tagged(in, inLen, &pos) ||
                    !vm_client_seq_skip_tagged(in, inLen, &pos))
                    return false;
            }
            if (pos + 3u <= inLen && in[pos] == 0 && in[pos + 1u] == 1 &&
                in[pos + 2u] == 0x47)
            {
                for (a = 0; a < 4u; ++a)
                {
                    if (!vm_client_seq_skip_tagged(in, inLen, &pos))
                        return false;
                }
            }
            /* Compact extra: attr_count is the following u8, not i16 high byte. */
            secondWire = (u16)(secondWire & 0xffu);
        }
        if (!vm_client_cbe_item_is_client_grid_item(itemId))
            continue;
        if (keep >= maxRows)
            break;
        /*
         * Prefer a partial compact list over aborting the whole rewrite.
         * A failed rewrite used to deliver the raw Pixi 17/1 and trip
         * mmGame:0x418C's 1024-byte stream copy (UI「解包错误」, empty bag).
         */
        if (!vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, itemId) ||
            !vm_client_seq_put_compact_extra_vals(out, outCap, &outPos,
                                                  firstWire, secondWire))
        {
            if (truncatedOut != NULL)
                *truncatedOut = true;
            break;
        }
        ++keep;
    }
    out[countValuePos] = (u8)keep;
    *outLen = (u16)outPos;
    if (keptRows != NULL)
        *keptRows = keep;
    if (truncatedOut != NULL)
        *truncatedOut = (count > keep || tagSize != 1 || *truncatedOut);
    return true;
}

static bool vm_client_build_backpack30_grid_compact(const u8 *in, u16 inLen,
                                                    u8 *out, u32 outCap,
                                                    u16 *outLen, u16 maxRows,
                                                    u16 *keptRows,
                                                    u16 *droppedRowsOut)
{
    u32 inPos = 0;
    u32 outPos = 0;
    u16 kept = 0;
    u16 dropped = 0;

    if (keptRows != NULL)
        *keptRows = 0;
    if (droppedRowsOut != NULL)
        *droppedRowsOut = 0;
    if (in == NULL || out == NULL || outLen == NULL || maxRows == 0)
        return false;
    while (inPos < inLen)
    {
        u32 itemId = 0;
        u32 seq = 0;
        u16 seq16 = 0;
        u32 count = 0;
        u32 wireCount = 0;
        u16 enhanceLevel = 0;
        u16 enhanceMax = 0;
        u32 extraPos = 0;

        if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &itemId))
            break;
        extraPos = inPos;
        if (vm_client_seq_read_tagged_i16(in, inLen, &inPos, &seq16))
            seq = seq16;
        else
        {
            inPos = extraPos;
            if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &seq))
                break;
        }
        if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &count))
            break;
        extraPos = inPos;
        if (vm_client_seq_read_tagged_i16(in, inLen, &extraPos, &enhanceLevel))
            (void)vm_client_seq_read_tagged_i16(in, inLen, &extraPos, &enhanceMax);
        if (!vm_client_seq_skip_common_extra(in, inLen, &inPos))
            break;
        if (!vm_client_cbe_item_is_client_grid_item(itemId))
        {
            ++dropped;
            continue;
        }
        wireCount = vm_client_cbe_grid_wire_count(itemId, count);
        if (wireCount == 0)
        {
            ++dropped;
            continue;
        }
        if (kept >= maxRows)
        {
            ++dropped;
            continue;
        }
        if (!vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, itemId) ||
            !vm_client_seq_put_tagged_i16(out, outCap, &outPos, (u16)seq) ||
            !vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, wireCount) ||
            !vm_client_seq_put_compact_extra_vals(out, outCap, &outPos,
                                                  enhanceLevel, enhanceMax))
            return false;
        ++kept;
    }
    *outLen = (u16)outPos;
    if (keptRows != NULL)
        *keptRows = kept;
    if (droppedRowsOut != NULL)
        *droppedRowsOut = dropped;
    return true;
}

static bool vm_client_seq_read_tagged_u32(const u8 *buf, u32 len, u32 *pos,
                                          u32 *valueOut)
{
    u32 start;

    if (buf == NULL || pos == NULL || valueOut == NULL || *pos + 2u > len ||
        buf[*pos] != 0)
        return false;
    start = *pos;
    if (!vm_client_seq_skip_tagged(buf, len, pos))
        return false;
    return vm_client_decode_tagged_u32(buf + start, (u16)(*pos - start),
                                       valueOut);
}

static bool vm_client_seq_put_tagged_u32(u8 *out, u32 outCap, u32 *pos,
                                         u32 value)
{
    if (out == NULL || pos == NULL)
        return false;
    if (value <= 0xffu)
    {
        if (*pos + 3u > outCap)
            return false;
        out[(*pos)++] = 0;
        out[(*pos)++] = 1;
        out[(*pos)++] = (u8)value;
        return true;
    }
    if (value <= 0xffffu)
    {
        if (*pos + 4u > outCap)
            return false;
        out[(*pos)++] = 0;
        out[(*pos)++] = 2;
        out[(*pos)++] = (u8)(value >> 8);
        out[(*pos)++] = (u8)value;
        return true;
    }
    if (*pos + 6u > outCap)
        return false;
    out[(*pos)++] = 0;
    out[(*pos)++] = 4;
    out[(*pos)++] = (u8)(value >> 24);
    out[(*pos)++] = (u8)(value >> 16);
    out[(*pos)++] = (u8)(value >> 8);
    out[(*pos)++] = (u8)value;
    return true;
}

/*
 * CBE_EMU vm_net_mock_backpack_item_is_client_grid_item: only 808/817-819 are
 * excluded from 17/1 list and 30/21 grid seed.  834/839/845/846 are normal
 * client-visible rows.
 */
static bool vm_client_cbe_item_is_client_grid_item(u32 itemId)
{
    switch (itemId)
    {
    case 808:
    case 817:
    case 818:
    case 819:
        return false;
    default:
        return itemId != 0;
    }
}

static u32 vm_client_cbe_grid_wire_count(u32 itemId, u32 count)
{
    if (count == 0)
        return 0;
    /* 802/803 reservoir + equipment: visible stack is 1 (count is durability). */
    if (itemId == 802 || itemId == 803)
        return 1;
    if (itemId >= 1000u)
        return 1;
    /*
     * 808/817-819 (stack_limit 0) are already dropped as non-grid.  Remaining
     * consumables (800 传送石, 801, …) use this u32 as item+0xf2 quantity.
     * Forcing 1 hid every stack after 17/1 made the row visible.
     */
    if (count > 255u)
        return 255u;
    return count;
}

static bool vm_client_seq_put_tagged_u8(u8 *out, u32 outCap, u32 *pos, u8 value)
{
    if (out == NULL || pos == NULL || *pos + 3u > outCap)
        return false;
    out[(*pos)++] = 0;
    out[(*pos)++] = 1;
    out[(*pos)++] = value;
    return true;
}

static bool vm_client_seq_put_tagged_u32_be32(u8 *out, u32 outCap, u32 *pos,
                                              u32 value)
{
    if (out == NULL || pos == NULL || *pos + 6u > outCap)
        return false;
  /* CBE_EMU vm_net_mock_seq_put_u32: always tag size 4 for 30/21 rows. */
    out[(*pos)++] = 0;
    out[(*pos)++] = 4;
    out[(*pos)++] = (u8)(value >> 24);
    out[(*pos)++] = (u8)(value >> 16);
    out[(*pos)++] = (u8)(value >> 8);
    out[(*pos)++] = (u8)value;
    return true;
}

static bool vm_client_seq_put_tagged_i16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    if (out == NULL || pos == NULL || *pos + 4u > outCap)
        return false;
    out[(*pos)++] = 0;
    out[(*pos)++] = 2;
    out[(*pos)++] = (u8)(value >> 8);
    out[(*pos)++] = (u8)value;
    return true;
}

/* CBE_EMU vm_net_mock_seq_put_item_compact_extra: enhance/max i16 + attr 0. */
static bool vm_client_seq_put_compact_extra_vals(u8 *out, u32 outCap, u32 *pos,
                                                u16 enhanceLevel,
                                                u16 enhanceMax)
{
    return vm_client_seq_put_tagged_i16(out, outCap, pos, enhanceLevel) &&
           vm_client_seq_put_tagged_i16(out, outCap, pos, enhanceMax) &&
           vm_client_seq_put_tagged_u8(out, outCap, pos, 0);
}

static bool vm_client_seq_put_compact_extra(u8 *out, u32 outCap, u32 *pos)
{
    return vm_client_seq_put_compact_extra_vals(out, outCap, pos, 0, 0);
}

enum
{
    VM_CLIENT_CBE_EQUIP_ATTR_CAP = 6,
    /* 129 live rows with full attrs exceeded 8192 → rewrite aborted. */
    VM_CLIENT_CBE_GRID30_SCRATCH = 32768,
    /* 17/1 compact rewrite scratch; was 2048 and aborted at ~120 rows. */
    VM_CLIENT_CBE_ITEMINFO17_SCRATCH = 8192,
    /*
     * mmGame:0x418C iteminfo stack buffer enlarged 1024→8192
     * (docs/re/2026-10-08-cbe-backpack-255-init-patch.md). Compact row = 17
     * bytes after u8 count tag → (8192-3)/17 = 481; clamp to slot cap 255.
     */
    VM_CLIENT_CBE_ITEMINFO17_STREAM_CAP = 8192,
    VM_CLIENT_CBE_ITEMINFO17_STREAM_ROWS = 255,
    /*
     * Login 30/21 full-attr ~8748B stalls HandleItemGrid before same-packet
     * 7/7 type=2 (+104). Keep full-attr rows under a tighter wire budget so
     * 7/7 still runs; all-compact asserts at InGame.c:913.
     * Mock group-type1 order: 30/21 → 7/7 type=2 → 7/11.
     */
    VM_CLIENT_CBE_GRID30_DELIVER_CAP = 4096,
    /*
     * Stream frame is patched to 8192, but copy callee may still min(len,1024).
     * Keep 17/1 under the classic 1024 until that callee is confirmed enlarged.
     */
    VM_CLIENT_CBE_ITEMINFO17_COPY_SAFE_CAP = 1000
};

/*
 * HandleItemGrid (0x01039952) / InGame.c:913: keep i16+i16+u8 attrs, but
 * drop Pixi 0x47 gem trailer and washed flag bit7. compact extra (attr=0)
 * also asserted.
 */
static bool vm_client_copy_common_extra_cbe(const u8 *in, u32 inLen, u32 *inPos,
                                            u8 *out, u32 outCap, u32 *outPos)
{
    u16 first = 0;
    u16 second = 0;
    u8 attrCount = 0;
    u8 outCount = 0;
    u8 unlocks[VM_CLIENT_CBE_EQUIP_ATTR_CAP];
    u8 types[VM_CLIENT_CBE_EQUIP_ATTR_CAP];
    u8 flags[VM_CLIENT_CBE_EQUIP_ATTR_CAP];
    u16 values[VM_CLIENT_CBE_EQUIP_ATTR_CAP];
    u32 i;

    if (!vm_client_seq_read_tagged_i16(in, inLen, inPos, &first) ||
        !vm_client_seq_read_tagged_i16(in, inLen, inPos, &second))
        return false;
    if (*inPos + 3u > inLen || in[*inPos] != 0 || in[*inPos + 1u] != 1)
        return false;
    attrCount = in[*inPos + 2u];
    *inPos += 3u;
    for (i = 0; i < attrCount; ++i)
    {
        u32 unlock = 0;
        u32 type = 0;
        u32 flag = 0;
        u32 value = 0;

        if (!vm_client_seq_read_tagged_u32(in, inLen, inPos, &unlock) ||
            !vm_client_seq_read_tagged_u32(in, inLen, inPos, &type) ||
            !vm_client_seq_read_tagged_u32(in, inLen, inPos, &flag) ||
            !vm_client_seq_read_tagged_u32(in, inLen, inPos, &value))
            return false;
        if (outCount < VM_CLIENT_CBE_EQUIP_ATTR_CAP)
        {
            unlocks[outCount] = (u8)unlock;
            types[outCount] = (u8)type;
            flags[outCount] = (u8)(flag & 1u);
            values[outCount] = (u16)value;
            outCount += 1;
        }
    }
    if (*inPos + 3u <= inLen && in[*inPos] == 0 && in[*inPos + 1u] == 1 &&
        in[*inPos + 2u] == 0x47)
    {
        for (i = 0; i < 4u; ++i)
        {
            if (!vm_client_seq_skip_tagged(in, inLen, inPos))
                return false;
        }
    }
    if (outCount != attrCount)
        second = (u16)((second & 0xffu) | ((u16)outCount << 8));
    if (!vm_client_seq_put_tagged_i16(out, outCap, outPos, first) ||
        !vm_client_seq_put_tagged_i16(out, outCap, outPos, second) ||
        !vm_client_seq_put_tagged_u8(out, outCap, outPos, outCount))
        return false;
    for (i = 0; i < outCount; ++i)
    {
        if (!vm_client_seq_put_tagged_u8(out, outCap, outPos, unlocks[i]) ||
            !vm_client_seq_put_tagged_u8(out, outCap, outPos, types[i]) ||
            !vm_client_seq_put_tagged_u8(out, outCap, outPos, flags[i]) ||
            !vm_client_seq_put_tagged_i16(out, outCap, outPos, values[i]))
            return false;
    }
    return true;
}

static bool vm_client_build_backpack30_grid_cbe(const u8 *in, u16 inLen, u8 *out,
                                                u32 outCap, u16 *outLen,
                                                u16 maxRows, u16 *keptRows,
                                                u16 *droppedRowsOut)
{
    u32 inPos = 0;
    u32 outPos = 0;
    u16 kept = 0;
    u16 dropped = 0;

    if (keptRows != NULL)
        *keptRows = 0;
    if (droppedRowsOut != NULL)
        *droppedRowsOut = 0;
    if (in == NULL || out == NULL || outLen == NULL || maxRows == 0)
        return false;
    if (inLen == 0)
    {
        *outLen = 0;
        return true;
    }
    while (inPos < inLen)
    {
        u32 itemId = 0;
        u32 seq = 0;
        u16 seq16 = 0;
        u32 count = 0;
        u32 wireCount = 0;
        u32 seqPos;

        u32 extraPos;
        u32 outRowStart;

        if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &itemId))
        {
            /* Trailing garbage after good rows: keep partial list. */
            break;
        }
        /*
         * Live/Pixi 30/21 often wires seq as tagged i16 (same as compact
         * builder).  Requiring u32 here used to fail the whole rewrite.
         */
        seqPos = inPos;
        if (vm_client_seq_read_tagged_i16(in, inLen, &inPos, &seq16))
            seq = seq16;
        else
        {
            inPos = seqPos;
            if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &seq))
                break;
        }
        if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &count))
            break;
        if (!vm_client_cbe_item_is_client_grid_item(itemId))
        {
            if (!vm_client_seq_skip_common_extra(in, inLen, &inPos))
                break;
            ++dropped;
            continue;
        }
        wireCount = vm_client_cbe_grid_wire_count(itemId, count);
        if (kept >= maxRows)
        {
            if (!vm_client_seq_skip_common_extra(in, inLen, &inPos))
                break;
            ++dropped;
            continue;
        }
        outRowStart = outPos;
        extraPos = inPos;
        if (!vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, itemId) ||
            !vm_client_seq_put_tagged_i16(out, outCap, &outPos, (u16)seq) ||
            !vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, wireCount))
        {
            outPos = outRowStart;
            break;
        }
        if (!vm_client_copy_common_extra_cbe(in, inLen, &inPos, out, outCap,
                                             &outPos))
        {
            /*
             * Full-attr copy failed (gem layout / size): skip live extra and
             * emit compact-extra so the row still seeds HandleItemGrid.
             */
            inPos = extraPos;
            outPos = outRowStart;
            if (!vm_client_seq_skip_common_extra(in, inLen, &inPos))
                break;
            if (!vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos,
                                                   itemId) ||
                !vm_client_seq_put_tagged_i16(out, outCap, &outPos, (u16)seq) ||
                !vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos,
                                                   wireCount) ||
                !vm_client_seq_put_compact_extra_vals(out, outCap, &outPos, 0,
                                                      0))
            {
                outPos = outRowStart;
                break;
            }
        }
        ++kept;
    }
    *outLen = (u16)outPos;
    if (keptRows != NULL)
        *keptRows = kept;
    if (droppedRowsOut != NULL)
        *droppedRowsOut = dropped;
    /* Success if we consumed input or produced rows; empty in is OK. */
    return kept > 0 || inLen == 0 || inPos > 0;
}

/* mmGame:sub_D04 7/7 rows: seq + itemId + count + extra; Pixi gem trailer
 * desyncs the next row (InGame.c:913, itemId leftover e.g. 0x5cd). */
static bool vm_client_build_77_iteminfo_cbe(const u8 *in, u16 inLen, u8 *out,
                                            u32 outCap, u16 *outLen,
                                            u16 *keptRows, bool *wornShapedOut)
{
    u32 inPos = 0;
    u32 outPos = 0;
    u32 rowCount = 0;
    u8 tagSize = 0;
    u32 countValuePos = 0;
    u32 i;
    u16 kept = 0;
    bool wornShaped = true;

    if (keptRows != NULL)
        *keptRows = 0;
    if (wornShapedOut != NULL)
        *wornShapedOut = false;
    if (in == NULL || out == NULL || outLen == NULL)
        return false;
    if (inLen == 0)
    {
        *outLen = 0;
        return true;
    }
    if (!vm_client_seq_read_row_count(in, inLen, &inPos, &rowCount, &tagSize) ||
        !vm_client_seq_write_row_count(out, outCap, &outPos, 0, 1))
        return false;
    countValuePos = outPos - 1u;
    for (i = 0; i < rowCount; ++i)
    {
        u32 seq = 0;
        u16 seq16 = 0;
        u32 itemId = 0;
        u32 count = 0;
        u32 seqPos;

        seqPos = inPos;
        if (vm_client_seq_read_tagged_i16(in, inLen, &inPos, &seq16))
            seq = seq16;
        else
        {
            inPos = seqPos;
            if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &seq))
                return false;
        }
        if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &itemId) ||
            !vm_client_seq_read_tagged_u32(in, inLen, &inPos, &count) ||
            !vm_client_seq_put_tagged_i16(out, outCap, &outPos, (u16)seq) ||
            !vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, itemId) ||
            !vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, count) ||
            !vm_client_copy_common_extra_cbe(in, inLen, &inPos, out, outCap,
                                             &outPos))
            return false;
        /* Login wear seq = slotIndex+1 (1..8); bag deltas use backpack seq. */
        if (itemId < 1000u || seq < 1u || seq > 8u)
            wornShaped = false;
        ++kept;
        if (kept <= 8)
            printf("[info][network] cbe_77_row i=%u item=%u seq=%u count=%u\n",
                   kept, itemId, seq, count);
    }
    out[countValuePos] = (u8)kept;
    *outLen = (u16)outPos;
    if (keptRows != NULL)
        *keptRows = kept;
    if (wornShapedOut != NULL)
        *wornShapedOut = (kept > 0 && wornShaped);
    return inPos <= inLen;
}

static bool vm_client_cbe_patch_common_extra_stack1(u8 *extra, u32 extraLen)
{
    if (extra == NULL || extraLen < 3u || extra[0] != 0)
        return false;
    if (extra[1] == 2 && extraLen >= 4u)
    {
        if (extra[2] == 0 && extra[3] == 1)
            return false;
        extra[2] = 0;
        extra[3] = 1;
        return true;
    }
    if (extra[1] == 1)
    {
        if (extra[2] == 1)
            return false;
        extra[2] = 1;
        return true;
    }
    return false;
}

static bool vm_client_sanitize_backpack30_iteminfo(const u8 *in, u16 inLen,
                                                   u8 *out, u32 outCap,
                                                   u16 *outLen, u16 maxRows,
                                                   u16 *keptRows,
                                                   u16 *droppedRows,
                                                   bool *countFixedOut)
{
    u32 pos = 0;
    u32 outPos = 0;
    u16 kept = 0;
    u16 dropped = 0;
    bool countFixed = false;
    char rowLog[192];
    u32 rowLogPos = 0;

    if (keptRows != NULL)
        *keptRows = 0;
    if (droppedRows != NULL)
        *droppedRows = 0;
    if (countFixedOut != NULL)
        *countFixedOut = false;
    if (in == NULL || out == NULL || outLen == NULL || maxRows == 0)
        return false;
    rowLog[0] = '\0';

    while (pos < inLen)
    {
        u32 rowStart = pos;
        u32 itemId = 0;
        u32 seq = 0;
        u32 count = 0;
        u32 idEnd;
        u32 seqEnd;
        u32 extraStart;
        u32 wireCount;
        u32 idLen;
        u32 seqLen;
        u32 extraLen;
        u32 extraOutPos;

        if (!vm_client_seq_read_tagged_u32(in, inLen, &pos, &itemId))
            return false;
        idEnd = pos;
        if (!vm_client_seq_read_tagged_u32(in, inLen, &pos, &seq))
            return false;
        seqEnd = pos;
        if (!vm_client_seq_read_tagged_u32(in, inLen, &pos, &count))
            return false;
        extraStart = pos;
        if (!vm_client_seq_skip_common_extra(in, inLen, &pos))
            return false;

        if (rowLogPos + 24u < sizeof(rowLog))
        {
            int wrote = snprintf(rowLog + rowLogPos, sizeof(rowLog) - rowLogPos,
                                 "%s%u:%u", rowLogPos ? "," : "", itemId, count);
            if (wrote > 0)
                rowLogPos += (u32)wrote;
        }

        if (!vm_client_cbe_item_is_client_grid_item(itemId))
        {
            ++dropped;
            continue;
        }
        wireCount = vm_client_cbe_grid_wire_count(itemId, count);
        if (wireCount != count)
            countFixed = true;
        if (kept >= maxRows)
        {
            ++dropped;
            continue;
        }

        idLen = idEnd - rowStart;
        seqLen = seqEnd - idEnd;
        extraLen = pos - extraStart;
        if (outPos + idLen + seqLen + 6u + extraLen > outCap)
            return false;
        memcpy(out + outPos, in + rowStart, idLen + seqLen);
        outPos += idLen + seqLen;
        if (!vm_client_seq_put_tagged_u32(out, outCap, &outPos, wireCount))
            return false;
        extraOutPos = outPos;
        memcpy(out + outPos, in + extraStart, extraLen);
        outPos += extraLen;
        if (itemId == 802 || itemId == 803 || itemId >= 1000u)
        {
            if (vm_client_cbe_patch_common_extra_stack1(out + extraOutPos,
                                                       extraLen))
                countFixed = true;
        }
        ++kept;
        (void)rowStart;
    }
    if (rowLogPos > 0)
    {
        printf("[info][network] cbe_backpack_30_21_rows n=%u list=%s "
               "evidence=login-grid-wire-forensics\n",
               (u32)(kept + dropped), rowLog);
    }
    *outLen = (u16)outPos;
    if (keptRows != NULL)
        *keptRows = kept;
    if (droppedRows != NULL)
        *droppedRows = dropped;
    if (countFixedOut != NULL)
        *countFixedOut = countFixed;
    return true;
}

static bool vm_client_truncate_backpack30_iteminfo(const u8 *in, u16 inLen,
                                                   u8 *out, u32 outCap,
                                                   u16 *outLen, u16 maxRows,
                                                   u16 *keptRows,
                                                   bool *truncatedOut)
{
    u16 dropped = 0;
    bool countFixed = false;

    if (!vm_client_sanitize_backpack30_iteminfo(in, inLen, out, outCap, outLen,
                                                maxRows, keptRows, &dropped,
                                                &countFixed))
        return false;
    if (truncatedOut != NULL)
        *truncatedOut = (dropped > 0 || countFixed);
    return true;
}

/*
 * CBE string fields (mock put_object_string / put_object_blob): value =
 * BE16(charLen) + GBK bytes. Live/Pixi may send raw GBK; CBE then treats the
 * first two name bytes as length and over-reads into the next field key
 * 「work」, so the HUD shows 「玩家名work」.
 */
static bool vm_client_normalize_cbe_string_blob(const u8 *in, u16 inLen, u8 *out,
                                                u32 outCap, u16 *outLen,
                                                const char **formOut)
{
    const u8 *chars = in;
    u16 charLen = inLen;
    const char *form = "raw";

    if (outLen != NULL)
        *outLen = 0;
    if (formOut != NULL)
        *formOut = "empty";
    if (in == NULL || out == NULL || outLen == NULL || inLen == 0)
        return false;

    if (inLen >= 4u && in[0] == 0 && in[1] == 0x10)
    {
        u16 n = (u16)(((u16)in[2] << 8) | in[3]);
        if ((u32)4u + (u32)n <= inLen)
        {
            chars = in + 4;
            charLen = n;
            form = "tagged-0x10";
        }
    }
    else if (inLen >= 2u)
    {
        u16 n = (u16)(((u16)in[0] << 8) | in[1]);
        if ((u32)2u + (u32)n <= inLen &&
            ((u32)2u + (u32)n == inLen || (u32)2u + (u32)n + 1u == inLen))
        {
            chars = in + 2;
            charLen = n;
            form = "blob-be16";
        }
    }
    while (charLen > 0u && chars[charLen - 1u] == 0)
        charLen = (u16)(charLen - 1u);
    if (charLen == 0 || (u32)2u + (u32)charLen + 1u > outCap)
        return false;
    out[0] = (u8)(charLen >> 8);
    out[1] = (u8)charLen;
    memcpy(out + 2, chars, charLen);
    /* NUL after payload so any C-string HUD path stops before the next key. */
    out[2u + charLen] = 0;
    *outLen = (u16)(2u + charLen + 1u);
    if (formOut != NULL)
        *formOut = form;
    return true;
}

/*
 * 29/4 nearby/friend「查看装备」equipinfo: no leading row count; outer `num`
 * is the row count. Each row = u32 itemId + i16 seq + ParseEquipAttributes.
 * Live/Pixi appends gem trailer 0x47 after attrs; CBE HandleEquipInfoResponse
 * (0x010216D6) stops at attr_count → next row desync → only first piece
 * (often weapon) shows. Strip trailer like 30/21 / 7/7.
 * evidence: docs/re/2026-09-19-nearby-player-equip-list.md
 */
static bool vm_client_build_29_4_equipinfo_cbe(const u8 *in, u16 inLen, u8 num,
                                               u8 *out, u32 outCap,
                                               u16 *outLen, u8 *keptOut)
{
    u32 inPos = 0;
    u32 outPos = 0;
    u8 kept = 0;
    u8 i;

    if (keptOut != NULL)
        *keptOut = 0;
    if (outLen != NULL)
        *outLen = 0;
    if (in == NULL || out == NULL || outLen == NULL || num == 0)
        return false;

    for (i = 0; i < num; ++i)
    {
        u32 itemId = 0;
        u16 seq16 = 0;
        u32 seq = 0;
        u32 seqPos;
        u32 extraPos;
        u32 outRowStart;

        if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &itemId))
            break;
        seqPos = inPos;
        if (vm_client_seq_read_tagged_i16(in, inLen, &inPos, &seq16))
            seq = seq16;
        else
        {
            inPos = seqPos;
            if (!vm_client_seq_read_tagged_u32(in, inLen, &inPos, &seq))
                break;
        }
        outRowStart = outPos;
        extraPos = inPos;
        if (!vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos, itemId) ||
            !vm_client_seq_put_tagged_i16(out, outCap, &outPos, (u16)seq) ||
            !vm_client_copy_common_extra_cbe(in, inLen, &inPos, out, outCap,
                                             &outPos))
        {
            inPos = extraPos;
            outPos = outRowStart;
            if (!vm_client_seq_skip_common_extra(in, inLen, &inPos))
                break;
            if (!vm_client_seq_put_tagged_u32_be32(out, outCap, &outPos,
                                                   itemId) ||
                !vm_client_seq_put_tagged_i16(out, outCap, &outPos, (u16)seq) ||
                !vm_client_seq_put_compact_extra_vals(out, outCap, &outPos, 0,
                                                      0))
                break;
        }
        ++kept;
        if (kept <= 8)
            printf("[info][network] cbe_29_4_row i=%u item=%u seq=%u\n", kept,
                   itemId, seq);
    }
    *outLen = (u16)outPos;
    if (keptOut != NULL)
        *keptOut = kept;
    return kept > 0;
}

static bool vm_client_rewrite_cbe_backpack_object(const vm_client_response_object *object,
                                                  u8 *outObj, u32 outCap,
                                                  u32 *outLen, bool *changedOut)
{
    const u8 *maxnumEnc = NULL;
    const u8 *gridnumEnc = NULL;
    const u8 *iteminfo = NULL;
    u16 maxnumLen = 0;
    u16 gridnumLen = 0;
    u16 iteminfoLen = 0;
    u8 newMaxnum[6];
    u8 newGridnum[6];
    u16 newMaxnumLen = 0;
    u16 newGridnumLen = 0;
    u8 *newIteminfo = NULL;
    u16 newIteminfoLen = 0;
    u16 keptRows = 0;
    bool truncated = false;
    bool changed = false;
    u32 maxnumValue = 0;
    u32 gridnumValue = 0;
    u32 pos = 0;
    u32 objectStart = 0;

    if (changedOut != NULL)
        *changedOut = false;
    if (object == NULL || outObj == NULL || outLen == NULL || outCap < 6)
        return false;

    if (object->major == 1 && object->kind == 17 && object->subtype == 1)
    {
        u32 rowCount = 0;
        u8 tagSize = 0;
        u32 dummyPos = 0;
        u16 droppedRows = 0;
        bool countTagToU8 = false;
        bool maxnumIsI16 = false;
        bool maxnumWasU8 = false;

        if (!vm_client_response_object_field(object, "maxnum", &maxnumEnc,
                                             &maxnumLen) ||
            !vm_client_response_object_field(object, "iteminfo", &iteminfo,
                                             &iteminfoLen))
        {
            return false;
        }
        if (!vm_client_decode_tagged_u32(maxnumEnc, maxnumLen, &maxnumValue))
            return false;
        if (!vm_client_seq_read_row_count(iteminfo, iteminfoLen, &dummyPos,
                                         &rowCount, &tagSize))
            return false;
        maxnumIsI16 = (maxnumLen == 4u && maxnumEnc[0] == 0 &&
                       maxnumEnc[1] == 2);
        maxnumWasU8 = (maxnumLen == 3u && maxnumEnc[0] == 0 &&
                       maxnumEnc[1] == 1);
        if (maxnumValue > VM_CLIENT_CBE_BACKPACK_SLOTS)
            maxnumValue = VM_CLIENT_CBE_BACKPACK_SLOTS;
        if (!vm_client_encode_tagged_u16(newMaxnum, sizeof(newMaxnum),
                                         &newMaxnumLen, (u16)maxnumValue, 2))
            return false;
        {
            u16 listRows = (u16)VM_CLIENT_CBE_BACKPACK_SLOTS;

            if (listRows > (u16)VM_CLIENT_CBE_ITEMINFO17_STREAM_ROWS)
                listRows = (u16)VM_CLIENT_CBE_ITEMINFO17_STREAM_ROWS;
            newIteminfo = (u8 *)malloc(VM_CLIENT_CBE_ITEMINFO17_SCRATCH);
            if (newIteminfo == NULL)
                return false;
            if (!vm_client_truncate_backpack17_iteminfo(
                    iteminfo, iteminfoLen, newIteminfo,
                    VM_CLIENT_CBE_ITEMINFO17_SCRATCH, &newIteminfoLen, listRows,
                    &keptRows, &truncated, &countTagToU8))
            {
                printf("[warn][network] cbe_17_rewrite_failed iteminfo_len=%u "
                       "maxnum=%u list_rows=%u "
                       "evidence=compact-extra+stream-1024\n",
                       iteminfoLen, maxnumValue, listRows);
                free(newIteminfo);
                return false;
            }
            while (newIteminfoLen >
                       (u16)VM_CLIENT_CBE_ITEMINFO17_COPY_SAFE_CAP &&
                   keptRows > 1u)
            {
                u16 nextRows = (u16)(((u32)keptRows *
                                      (u32)VM_CLIENT_CBE_ITEMINFO17_COPY_SAFE_CAP) /
                                     (u32)newIteminfoLen);
                if (nextRows >= keptRows)
                    nextRows = (u16)(keptRows - 1u);
                if (nextRows < 1u)
                    nextRows = 1u;
                printf("[warn][network] cbe_17_copy_safe_cap len=%u cap=%u "
                       "rows=%u trying=%u evidence=copy-callee-min-1024\n",
                       newIteminfoLen,
                       (u32)VM_CLIENT_CBE_ITEMINFO17_COPY_SAFE_CAP, keptRows,
                       nextRows);
                if (!vm_client_truncate_backpack17_iteminfo(
                        iteminfo, iteminfoLen, newIteminfo,
                        VM_CLIENT_CBE_ITEMINFO17_SCRATCH, &newIteminfoLen,
                        nextRows, &keptRows, &truncated, &countTagToU8))
                {
                    free(newIteminfo);
                    return false;
                }
                truncated = true;
            }
            if (newIteminfoLen > (u16)VM_CLIENT_CBE_ITEMINFO17_STREAM_CAP ||
                newIteminfoLen > (u16)VM_CLIENT_CBE_ITEMINFO17_COPY_SAFE_CAP)
            {
                printf("[warn][network] cbe_17_rewrite_oversize iteminfo_len=%u "
                       "kept=%u stream_cap=%u copy_cap=%u "
                       "evidence=mmGame:0x418C-stream+copy\n",
                       newIteminfoLen, keptRows,
                       (u32)VM_CLIENT_CBE_ITEMINFO17_STREAM_CAP,
                       (u32)VM_CLIENT_CBE_ITEMINFO17_COPY_SAFE_CAP);
                free(newIteminfo);
                return false;
            }
        }
        changed = true;
        objectStart = 0;
        outObj[pos++] = object->major;
        outObj[pos++] = object->kind;
        outObj[pos++] = object->subtype;
        outObj[pos++] = 0;
        outObj[pos++] = 0;
        outObj[pos++] = 0;
        if (!vm_client_put_object_field(outObj, outCap, &pos, "maxnum",
                                         newMaxnum, newMaxnumLen) ||
            !vm_client_put_object_field(outObj, outCap, &pos, "iteminfo",
                                         newIteminfo, newIteminfoLen))
        {
            free(newIteminfo);
            return false;
        }
        free(newIteminfo);
        outObj[objectStart + 4] = (u8)(pos >> 8);
        outObj[objectStart + 5] = (u8)pos;
        *outLen = pos;
        if (changedOut != NULL)
            *changedOut = true;
        printf("[info][network] cbe_backpack_clamp object=17/1 maxnum=%u "
               "rows_kept=%u truncated=%u maxnum_tag=%s count_tag=%s "
               "iteminfo_len=%u mode=compact-extra "
               "hex=%02x%02x%02x%02x "
               "evidence=CBE_EMU:17/1-detail+mmGame:0x418C\n",
               maxnumValue, keptRows, truncated ? 1u : 0u,
               maxnumWasU8 ? "u8->i16" : (maxnumIsI16 ? "i16" : "retag-i16"),
               countTagToU8 ? "i16->u8" : "u8",
               newIteminfoLen,
               newMaxnum[0], newMaxnum[1], newMaxnum[2], newMaxnum[3]);
        return true;
    }

    if (object->major == 1 && object->kind == 30 && object->subtype == 21)
    {
        const u8 *resultEnc = NULL;
        u16 resultLen = 0;
        u16 droppedRows = 0;
        u8 emptyIteminfo[1];
        bool rebuilt = false;

        if (!vm_client_response_object_field(object, "result", &resultEnc,
                                             &resultLen) ||
            !vm_client_response_object_field(object, "gridnum", &gridnumEnc,
                                             &gridnumLen) ||
            !vm_client_response_object_field(object, "iteminfo", &iteminfo,
                                             &iteminfoLen))
        {
            return false;
        }
        if (!vm_client_decode_tagged_u32(gridnumEnc, gridnumLen, &gridnumValue))
            return false;
        if (gridnumValue > VM_CLIENT_CBE_BACKPACK_SLOTS)
            gridnumValue = VM_CLIENT_CBE_BACKPACK_SLOTS;
        {
            const char *mode = "cbe-no-gem-trailer";
            bool built = false;

            newIteminfo = (u8 *)malloc(VM_CLIENT_CBE_GRID30_SCRATCH);
            if (newIteminfo == NULL)
                return false;
            built = vm_client_build_backpack30_grid_cbe(
                iteminfo, iteminfoLen, newIteminfo, VM_CLIENT_CBE_GRID30_SCRATCH,
                &newIteminfoLen, (u16)VM_CLIENT_CBE_BACKPACK_SLOTS, &keptRows,
                &droppedRows);
            if (built && keptRows > 0 &&
                newIteminfoLen > (u16)VM_CLIENT_CBE_GRID30_DELIVER_CAP)
            {
                u16 fullLen = newIteminfoLen;
                u16 fullKept = keptRows;
                u16 rowCap;

                /*
                 * All-compact under the size cap still hits InGame.c:913
                 * (compact extra attr=0). Keep full-attr rows, fewer of them.
                 * Scene 17/1 later refreshes names for the full bag.
                 */
                rowCap = (u16)(((u32)VM_CLIENT_CBE_GRID30_DELIVER_CAP *
                                (u32)fullKept) /
                               (u32)fullLen);
                if (rowCap < 1u)
                    rowCap = 1u;
                if (rowCap >= fullKept)
                    rowCap = (u16)(fullKept - 1u);
                printf("[warn][network] cbe_30_oversized_full_attr len=%u "
                       "cap=%u rows=%u trying=full-attr-row-cap "
                       "target_rows=%u evidence=HandleItemGrid-8k-class+"
                       "compact-assert\n",
                       fullLen, (u32)VM_CLIENT_CBE_GRID30_DELIVER_CAP, fullKept,
                       rowCap);
                built = false;
                while (rowCap >= 1u)
                {
                    keptRows = 0;
                    droppedRows = 0;
                    newIteminfoLen = 0;
                    if (vm_client_build_backpack30_grid_cbe(
                            iteminfo, iteminfoLen, newIteminfo,
                            VM_CLIENT_CBE_GRID30_SCRATCH, &newIteminfoLen,
                            rowCap, &keptRows, &droppedRows) &&
                        keptRows > 0 &&
                        newIteminfoLen <=
                            (u16)VM_CLIENT_CBE_GRID30_DELIVER_CAP)
                    {
                        built = true;
                        mode = "full-attr-row-cap";
                        break;
                    }
                    if (rowCap <= 1u)
                        break;
                    rowCap = (u16)(rowCap > 16u ? (rowCap - 16u) : (rowCap / 2u));
                    if (rowCap < 1u)
                        rowCap = 1u;
                }
                if (!built)
                {
                    keptRows = 0;
                    droppedRows = 0;
                    newIteminfoLen = 0;
                    printf("[warn][network] cbe_30_row_cap_failed "
                           "full_len=%u full_rows=%u evidence=no-all-compact\n",
                           fullLen, fullKept);
                }
            }
            if (!built || (keptRows == 0 && iteminfoLen > 0))
            {
                printf("[warn][network] cbe_30_cbe_build_failed gridnum=%u "
                       "iteminfo_len=%u kept=%u trying=compact-extra\n",
                       gridnumValue, iteminfoLen, keptRows);
                keptRows = 0;
                droppedRows = 0;
                newIteminfoLen = 0;
                built = vm_client_build_backpack30_grid_compact(
                    iteminfo, iteminfoLen, newIteminfo,
                    VM_CLIENT_CBE_GRID30_SCRATCH, &newIteminfoLen,
                    (u16)VM_CLIENT_CBE_BACKPACK_SLOTS, &keptRows, &droppedRows);
                if (built && keptRows > 0)
                    mode = "compact-extra-fallback";
            }
            /*
             * Never deliver raw Pixi 30/21 (9960B →「解包错误」).  Empty CBE
             * grid is safer than poison; 17/1 may still refresh names later.
             */
            if (!built || keptRows == 0)
            {
                free(newIteminfo);
                if (!vm_client_encode_tagged_u16(newGridnum, sizeof(newGridnum),
                                                 &newGridnumLen, 0, 1))
                    return false;
                emptyIteminfo[0] = 0;
                newIteminfoLen = 0;
                newIteminfo = emptyIteminfo;
                keptRows = 0;
                mode = "empty-after-rewrite-fail";
                if (iteminfoLen > 0)
                    printf("[warn][network] cbe_30_rewrite_empty gridnum=%u "
                           "iteminfo_len=%u evidence=no-passthrough-pixi\n",
                           gridnumValue, iteminfoLen);
            }
            else
            {
                rebuilt = true;
                if (!vm_client_encode_tagged_u16(newGridnum, sizeof(newGridnum),
                                                 &newGridnumLen, keptRows, 1))
                {
                    free(newIteminfo);
                    return false;
                }
            }
            objectStart = 0;
            outObj[pos++] = object->major;
            outObj[pos++] = object->kind;
            outObj[pos++] = object->subtype;
            outObj[pos++] = 0;
            outObj[pos++] = 0;
            outObj[pos++] = 0;
            if (!vm_client_put_object_field(outObj, outCap, &pos, "result",
                                             resultEnc, resultLen) ||
                !vm_client_put_object_field(outObj, outCap, &pos, "gridnum",
                                             newGridnum, newGridnumLen) ||
                !vm_client_put_object_field(outObj, outCap, &pos, "iteminfo",
                                             newIteminfo, newIteminfoLen))
            {
                if (rebuilt && newIteminfo != emptyIteminfo)
                    free(newIteminfo);
                return false;
            }
            if (rebuilt && newIteminfo != emptyIteminfo)
                free(newIteminfo);
            outObj[objectStart + 4] = (u8)(pos >> 8);
            outObj[objectStart + 5] = (u8)pos;
            *outLen = pos;
            if (changedOut != NULL)
                *changedOut = true;
            printf("[info][network] cbe_backpack_clamp object=30/21 gridnum=%u "
                   "rows_kept=%u dropped=%u iteminfo_len=%u->%u mode=%s "
                   "evidence=InGame.c:913-HandleItemGrid\n",
                   keptRows, keptRows, droppedRows, iteminfoLen, newIteminfoLen,
                   mode);
            return true;
        }
    }

    if (object->major == 1 && object->kind == 7 && object->subtype == 7)
    {
        const u8 *typeEnc = NULL;
        u16 typeLen = 0;
        u8 typeVal = 0;
        u8 typeWear2[3] = {0, 1, 2};
        bool wornShaped = false;
        bool coercedType2 = false;

        if (!vm_client_response_object_field(object, "type", &typeEnc,
                                             &typeLen) ||
            !vm_client_response_object_field(object, "iteminfo", &iteminfo,
                                             &iteminfoLen))
            return false;
        (void)vm_client_decode_object_u8(object, "type", &typeVal);
        newIteminfo = (u8 *)malloc(VM_CLIENT_CBE_GRID30_SCRATCH);
        if (newIteminfo == NULL)
            return false;
        if (!vm_client_build_77_iteminfo_cbe(iteminfo, iteminfoLen, newIteminfo,
                                             VM_CLIENT_CBE_GRID30_SCRATCH,
                                             &newIteminfoLen, &keptRows,
                                             &wornShaped))
        {
            printf("[warn][network] cbe_77_rewrite_failed iteminfo_len=%u "
                   "evidence=keep-original-7/7\n",
                   iteminfoLen);
            free(newIteminfo);
            return false;
        }
        /*
         * Live login may echo request type=1 on the wear bootstrap object.
         * sub_D04 type=1 → +52 bag; type=2 → +104 category 15 (装备栏).
         * Worn-shaped rows (equip id, seq slot 1..8): force type=2.
         */
        if (wornShaped && typeVal != 2u)
        {
            typeEnc = typeWear2;
            typeLen = 3;
            coercedType2 = true;
            typeVal = 2;
        }
        objectStart = 0;
        outObj[pos++] = object->major;
        outObj[pos++] = object->kind;
        outObj[pos++] = object->subtype;
        outObj[pos++] = 0;
        outObj[pos++] = 0;
        outObj[pos++] = 0;
        if (!vm_client_put_object_field(outObj, outCap, &pos, "type", typeEnc,
                                         typeLen) ||
            !vm_client_put_object_field(outObj, outCap, &pos, "iteminfo",
                                         newIteminfo, newIteminfoLen))
        {
            free(newIteminfo);
            return false;
        }
        free(newIteminfo);
        outObj[objectStart + 4] = (u8)(pos >> 8);
        outObj[objectStart + 5] = (u8)pos;
        *outLen = pos;
        if (changedOut != NULL)
            *changedOut = true;
        printf("[info][network] cbe_backpack_clamp object=7/7 type=%u "
               "worn_shaped=%u coerce_type2=%u rows_kept=%u "
               "iteminfo_len=%u->%u mode=cbe-no-gem-trailer "
               "evidence=mmGame:0xD04-InGame.c:913\n",
               typeVal, wornShaped ? 1u : 0u, coercedType2 ? 1u : 0u, keptRows,
               iteminfoLen, newIteminfoLen);
        return true;
    }

    if (object->major == 1 && object->kind == 29 && object->subtype == 4)
    {
        const u8 *resultEnc = NULL;
        const u8 *numEnc = NULL;
        const u8 *levelEnc = NULL;
        const u8 *nameEnc = NULL;
        const u8 *workEnc = NULL;
        const u8 *sexEnc = NULL;
        const u8 *equipinfo = NULL;
        u16 resultLen = 0;
        u16 numLen = 0;
        u16 levelLen = 0;
        u16 nameLen = 0;
        u16 workLen = 0;
        u16 sexLen = 0;
        u16 equipinfoLen = 0;
        u8 numVal = 0;
        u8 keptEquip = 0;
        u8 newNumEnc[3];
        u8 nameBlob[96];
        u16 nameBlobLen = 0;
        const char *nameForm = "raw";

        if (!vm_client_response_object_field(object, "result", &resultEnc,
                                             &resultLen) ||
            !vm_client_response_object_field(object, "num", &numEnc, &numLen) ||
            !vm_client_response_object_field(object, "level", &levelEnc,
                                             &levelLen) ||
            !vm_client_response_object_field(object, "name", &nameEnc,
                                             &nameLen) ||
            !vm_client_response_object_field(object, "work", &workEnc,
                                             &workLen) ||
            !vm_client_response_object_field(object, "sex", &sexEnc, &sexLen) ||
            !vm_client_response_object_field(object, "equipinfo", &equipinfo,
                                             &equipinfoLen))
            return false;
        (void)vm_client_decode_object_u8(object, "num", &numVal);
        if (numVal == 0 || equipinfoLen == 0)
            return false;
        if (!vm_client_normalize_cbe_string_blob(nameEnc, nameLen, nameBlob,
                                                 sizeof(nameBlob), &nameBlobLen,
                                                 &nameForm))
        {
            printf("[warn][network] cbe_29_4_name_normalize_failed len=%u "
                   "evidence=keep-original-name\n",
                   nameLen);
            nameBlobLen = 0;
        }
        newIteminfo = (u8 *)malloc(VM_CLIENT_CBE_GRID30_SCRATCH);
        if (newIteminfo == NULL)
            return false;
        if (!vm_client_build_29_4_equipinfo_cbe(
                equipinfo, equipinfoLen, numVal, newIteminfo,
                VM_CLIENT_CBE_GRID30_SCRATCH, &newIteminfoLen, &keptEquip) ||
            keptEquip == 0)
        {
            printf("[warn][network] cbe_29_4_rewrite_failed num=%u "
                   "equipinfo_len=%u evidence=keep-original-29/4\n",
                   numVal, equipinfoLen);
            free(newIteminfo);
            return false;
        }
        newNumEnc[0] = 0;
        newNumEnc[1] = 1;
        newNumEnc[2] = keptEquip;
        objectStart = 0;
        outObj[pos++] = object->major;
        outObj[pos++] = object->kind;
        outObj[pos++] = object->subtype;
        outObj[pos++] = 0;
        outObj[pos++] = 0;
        outObj[pos++] = 0;
        if (!vm_client_put_object_field(outObj, outCap, &pos, "result",
                                         resultEnc, resultLen) ||
            !vm_client_put_object_field(outObj, outCap, &pos, "num", newNumEnc,
                                         3) ||
            !vm_client_put_object_field(outObj, outCap, &pos, "level", levelEnc,
                                         levelLen) ||
            !vm_client_put_object_field(
                outObj, outCap, &pos, "name",
                nameBlobLen > 0 ? nameBlob : nameEnc,
                nameBlobLen > 0 ? nameBlobLen : nameLen) ||
            !vm_client_put_object_field(outObj, outCap, &pos, "work", workEnc,
                                         workLen) ||
            !vm_client_put_object_field(outObj, outCap, &pos, "sex", sexEnc,
                                         sexLen) ||
            !vm_client_put_object_field(outObj, outCap, &pos, "equipinfo",
                                         newIteminfo, newIteminfoLen))
        {
            free(newIteminfo);
            return false;
        }
        free(newIteminfo);
        outObj[objectStart + 4] = (u8)(pos >> 8);
        outObj[objectStart + 5] = (u8)pos;
        *outLen = pos;
        if (changedOut != NULL)
            *changedOut = true;
        printf("[info][network] cbe_backpack_clamp object=29/4 num=%u->%u "
               "equipinfo_len=%u->%u name_form=%s name_len=%u->%u "
               "mode=cbe-no-gem-trailer "
               "evidence=HandleEquipInfoResponse:0x010216D6\n",
               numVal, keptEquip, equipinfoLen, newIteminfoLen, nameForm,
               nameLen, nameBlobLen > 0 ? nameBlobLen : nameLen);
        return true;
    }

    return false;
}

static bool vm_client_cbe_clamp_backpack_packet(u8 **response, u32 *responseLen)
{
    /* Downlink WT: objects start after objectCount at index 4. */
    u32 offset = 5;
    u32 outPos = 5;
    u8 objectCount = 0;
    u8 *out = NULL;
    u8 *packet = NULL;
    u32 outCap = 0;
    bool anyChanged = false;
    vm_client_response_object object;

    if (response == NULL || *response == NULL || responseLen == NULL ||
        *responseLen < 5 || (*response)[0] != 'W' || (*response)[1] != 'T')
        return false;
    packet = *response;

    /* 17/1 compact-extra rewrite; 30/21 CBE HandleItemGrid rewrite. */
    offset = 5;
    {
        bool needRebuild = false;

        while (offset + 6u <= *responseLen &&
               vm_client_next_response_object(packet, *responseLen, &offset,
                                               &object))
        {
            if (object.major == 1 && object.kind == 30 && object.subtype == 21)
            {
                needRebuild = true;
                break;
            }
            if (object.major == 1 && object.kind == 7 && object.subtype == 7)
            {
                needRebuild = true;
                break;
            }
            if (object.major == 1 && object.kind == 17 && object.subtype == 1)
            {
                needRebuild = true;
                break;
            }
            if (object.major == 1 && object.kind == 29 && object.subtype == 4)
            {
                needRebuild = true;
                break;
            }
        }
        if (!needRebuild)
            return false;
    }

    offset = 5;
    outCap = *responseLen + 64u;
    out = (u8 *)malloc(outCap);
    if (out == NULL)
        return false;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;

    while (offset + 6u <= *responseLen &&
           vm_client_next_response_object(packet, *responseLen, &offset,
                                           &object))
    {
        u8 *rewritten = NULL;
        u32 rewrittenCap = (u32)object.payloadLen + 64u;
        if (object.kind == 30 && object.subtype == 21)
            rewrittenCap = VM_CLIENT_CBE_GRID30_SCRATCH + 256u;
        if (object.kind == 7 && object.subtype == 7)
            rewrittenCap = VM_CLIENT_CBE_GRID30_SCRATCH + 256u;
        if (object.kind == 17 && object.subtype == 1)
            rewrittenCap = VM_CLIENT_CBE_ITEMINFO17_SCRATCH + 256u;
        if (object.kind == 29 && object.subtype == 4)
            rewrittenCap = VM_CLIENT_CBE_GRID30_SCRATCH + 256u;
        u32 rewrittenLen = 0;
        bool changed = false;
        const u8 *src = packet + (offset - (object.payloadLen + 6u));
        u32 srcLen = (u32)object.payloadLen + 6u;
        bool is17 = (object.major == 1 && object.kind == 17 &&
                     object.subtype == 1);

        if (rewrittenCap < 64u)
            rewrittenCap = 64u;
        rewritten = (u8 *)malloc(rewrittenCap);
        if (rewritten != NULL &&
            vm_client_rewrite_cbe_backpack_object(&object, rewritten,
                                                  rewrittenCap, &rewrittenLen,
                                                  &changed) &&
            changed)
        {
            src = rewritten;
            srcLen = rewrittenLen;
            anyChanged = true;
        }
        else if (is17)
        {
            /*
             * Never deliver raw Pixi 17/1: oversized/gem-trailer blobs hit
             * mmGame:0x418C stream copy →「解包错误」and empty bag UI.
             */
            u8 safe[64];
            u32 safePos = 0;
            u8 maxEnc[6];
            u16 maxEncLen = 0;
            u8 emptyInfo[3] = {0, 1, 0};

            printf("[warn][network] cbe_17_passthrough_blocked payload=%u "
                   "evidence=replace-empty-compact-17/1\n",
                   (u32)object.payloadLen);
            if (vm_client_encode_tagged_u16(
                    maxEnc, sizeof(maxEnc), &maxEncLen,
                    (u16)VM_CLIENT_CBE_BACKPACK_SLOTS, 2))
            {
                safe[safePos++] = 1;
                safe[safePos++] = 17;
                safe[safePos++] = 1;
                safe[safePos++] = 0;
                safe[safePos++] = 0;
                safe[safePos++] = 0;
                if (vm_client_put_object_field(safe, sizeof(safe), &safePos,
                                               "maxnum", maxEnc, maxEncLen) &&
                    vm_client_put_object_field(safe, sizeof(safe), &safePos,
                                               "iteminfo", emptyInfo, 3))
                {
                    safe[4] = (u8)(safePos >> 8);
                    safe[5] = (u8)safePos;
                    if (rewritten != NULL && safePos <= rewrittenCap)
                    {
                        memcpy(rewritten, safe, safePos);
                        src = rewritten;
                        srcLen = safePos;
                        anyChanged = true;
                    }
                }
            }
        }
        if (outPos + srcLen > outCap || objectCount == 0xff)
        {
            free(rewritten);
            free(out);
            return false;
        }
        memcpy(out + outPos, src, srcLen);
        outPos += srcLen;
        ++objectCount;
        free(rewritten);
    }
    if (offset != *responseLen)
    {
        free(out);
        return false;
    }
    if (!anyChanged)
    {
        free(out);
        return false;
    }
    vm_client_finish_wt_packet(out, outPos, objectCount);
    if (outPos > *responseLen)
    {
        u8 *grown = (u8 *)realloc(packet, outPos);
        if (grown == NULL)
        {
            free(out);
            return false;
        }
        packet = grown;
        *response = grown;
    }
    memcpy(packet, out, outPos);
    *responseLen = outPos;
    free(out);
    return true;
}

static bool vm_client_strip_cbe_unsupported_objects(u8 **packet, u32 *packetLen)
{
    /* Downlink WT: objects start after objectCount at index 4. */
    u32 offset = 5;
    u32 outPos = 5;
    u8 objectCount = 0;
    u8 removed = 0;
    u8 *out = NULL;
    u8 *src = NULL;
    u32 outCap = 0;
    vm_client_response_object object;

    if (packet == NULL || *packet == NULL || packetLen == NULL ||
        *packetLen < 5 || (*packet)[0] != 'W' || (*packet)[1] != 'T')
        return false;
    src = *packet;
    outCap = *packetLen;
    out = (u8 *)malloc(outCap);
    if (out == NULL)
        return false;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;

    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(src, *packetLen, &offset, &object))
    {
        u32 objectStart = offset - ((u32)object.payloadLen + 6u);
        u32 objectLen = (u32)object.payloadLen + 6u;

        if (vm_client_wt_object_is_cbe_unsupported(&object))
        {
            ++removed;
            continue;
        }
        if (outPos + objectLen > outCap)
        {
            u8 *grown = (u8 *)realloc(out, outCap + objectLen + 64u);
            if (grown == NULL)
            {
                free(out);
                return false;
            }
            out = grown;
            outCap += objectLen + 64u;
        }
        memcpy(out + outPos, src + objectStart, objectLen);
        outPos += objectLen;
        ++objectCount;
    }
    if (offset != *packetLen || removed == 0)
    {
        free(out);
        return false;
    }
    vm_client_finish_wt_packet(out, outPos, objectCount);
    if (outPos > *packetLen)
    {
        u8 *grown = (u8 *)realloc(*packet, outPos);
        if (grown == NULL)
        {
            free(out);
            return false;
        }
        *packet = grown;
    }
    memcpy(*packet, out, outPos);
    *packetLen = outPos;
    free(out);
    printf("[info][network] cbe_strip_unsupported removed=%u objects=%u resp=%u "
           "evidence=WT1/31-pixi-cosmetics\n",
           removed, objectCount, outPos);
    return true;
}

/*
 * Hangup settlement UI shim (mmBattle:sub_7794 reads 4/7.combatinfo while the
 * native auto-battle flag is set).  Remote/live servers may omit combatinfo;
 * synthesize it on the CBE downlink without touching the game server.
 * evidence: CBE_EMU vm_net_mock_append_scene_hangup_combatinfo,
 * docs/re/2026-08-20-hangup-summary-and-battle-insight.md
 */
enum
{
    /*
     * Always unlimited hangup battles (insight or not).  Pixi / admin treat
     * 0xffff as「不限次」; CBE combatinfo.maximum uses the same sentinel.
     */
    VM_CLIENT_HANGUP_SETTLE_DEFAULT_MAX = 0xffffu,
    VM_CLIENT_HANGUP_SETTLE_INSIGHT_MAX = 0xffffu
};

typedef struct
{
    u8 armed;
    u16 completed;
    u16 maxBattles;
    u32 totalExp;
    u32 totalGold;
    u32 totalHpRecovered;
    u32 totalMpRecovered;
    u32 battleToken;
    u32 lastAccountedToken;
    u32 battlePreExp;
    u32 battlePreGold;
    u32 lastRoleExp;
    u32 lastRoleGold;
} vm_client_hangup_settle_ui_state;

static vm_client_hangup_settle_ui_state g_vmClientHangupSettleUi;
/*
 * Hangup skip-4/8 keeps Battle.cbm on the 4/7 shell while mmGame still ticks
 * empty 25/5.  Completing that wait (empty WT or 25/5 result=4) runs
 * JianghuOL.CBE:0x01012E4C Handle25 → 0x01004CC2 rand_range →
 * __aeabi_idiv DIV0 pc=0x0104E980 (client_out 2026-10-05, resp=5).
 * Defer the ACK until a map-side 4/8 (no 4/5 in the same WT).
 * Swallowing the 25/5 uplink is not enough: host scene-poll still returns
 * empty WT (client_out 2026-10-05: swallow_25_5 then queue_scene_poll resp=5).
 * Drop empty WT only while OnSettleShell (after 4/7「挂机中」).  Do not drop
 * while settle_ui is only armed from 2/10 — that ate the 4/12 ACK and the
 * next poll 4/7 (resp=718) was parsed as the 4/12 wait.
 */
static u8 g_vmClientHangupOweSceneDefaultAck;
static u32 g_vmClientHangupOweSceneDefaultConnect;
static u8 g_vmClientHangupOnSettleShell;
/*
 * Native hangup mode (game+1136) is set only by Handle25_2 after 25/3 confirm.
 * Live/immediate Type=2 skips that → combatinfo injected but sub_7794 ignored.
 * 0=idle 1=local 25/3 shown 2=25/2(type=1) accepted (hangup mode).
 */
static u8 g_vmClientHangupNativeMode;
/* Swallow map Type=2 for a few seconds after settle 停止挂机. */
static u32 g_vmClientHangupStopHoldUntilMs;
/* After host confirm, guest still gets a second DOWN/UP on the soft row
 * (client_out: hangup_panel_confirm then mouse_down y=381 → 28A4). */
static u32 g_vmHangupSkipGuestConfirmUntilMs;
/* Server 25/3 panel must not reach CBE channel (Global R9 → 2908 crash). */
static u8 g_vmClientHangupAutoAckAfterPanel;
static u8 g_vmClientHangupPanelVia8996;
/* Captured 2/10 Type=2 (+25/3) uplink; replayed to live after local 25/2(type=1). */
static u8 g_vmClientHangupPendingStart[VM_CLIENT_REQUEST_MAX];
static u32 g_vmClientHangupPendingStartLen;
static u32 g_vmClientHangupPendingConnectId;
/* Guest R9 at hangup 2/10 send — preferred module base for 8996 deliver. */
static u32 g_vmClientHangupSendR9;

static void vm_client_hangup_log_mode_flag(const char *reason);
static int vm_client_hangup_ack_guest_25_2(u32 connectId);
static void vm_client_hangup_arm_skip_guest_confirm(u32 holdMs);
static void vm_client_hangup_arm_settle_after_mode(const char *reason);
static u32 vm_client_build_hangup_dismiss_wt(u8 *out, u32 outCap);
static bool vm_client_begin_local_wt_object(u8 *out, u32 outCap, u32 *pos,
                                            u8 major, u8 kind, u8 subtype,
                                            u32 *objectStart);
static void vm_client_finish_local_wt_object(u8 *out, u32 objectStart, u32 pos);
static bool vm_client_put_object_u8_field(u8 *out, u32 outCap, u32 *pos,
                                          const char *name, u8 value);
static bool vm_client_response_has_object(const u8 *packet, u32 packetLen,
                                          u8 kind, u8 subtype, u16 minPayload);
static bool vm_client_hangup_deliver_wt_to_8996(u32 connectId, u8 *packet,
                                                u32 packetLen, const char *tag);

/* GBK 挂机中 — live 4/7.fdata banner; native reward text, not combatinfo. */
static const u8 kVmClientGbkHangupZhong[6] = {
    0xB9, 0xD2, 0xBB, 0xFA, 0xD6, 0xD0};

static bool vm_client_mem_has_hangup_zhong(const u8 *buf, u32 len)
{
    u32 i;

    if (buf == NULL || len < sizeof(kVmClientGbkHangupZhong))
        return false;
    for (i = 0; i + sizeof(kVmClientGbkHangupZhong) <= len; ++i)
    {
        if (memcmp(buf + i, kVmClientGbkHangupZhong,
                   sizeof(kVmClientGbkHangupZhong)) == 0)
            return true;
    }
    return false;
}

static u32 vm_client_add_capped_u32(u32 a, u32 b)
{
    u64 sum = (u64)a + (u64)b;
    return (u32)(sum > 0xffffffffu ? 0xffffffffu : sum);
}

static void vm_client_hangup_settle_ui_clear(const char *reason)
{
    memset(&g_vmClientHangupSettleUi, 0, sizeof(g_vmClientHangupSettleUi));
    g_vmClientHangupOnSettleShell = 0;
    g_vmClientHangupNativeMode = 0;
    g_vmClientHangupPanelVia8996 = 0;
    g_hangupNetCallEntry = 0;
    g_hangupNetCallR9 = 0;
    g_vmClientHangupSendR9 = 0;
    g_vmClientHangupPendingStartLen = 0;
    g_vmClientHangupPendingConnectId = 0;
    if (reason != NULL && reason[0] != 0)
    {
        printf("[info][network] cbe_hangup_settle_ui_clear reason=%s\n", reason);
    }
}

static void vm_client_hangup_settle_ui_arm(u16 maxBattles, const char *reason)
{
    g_vmClientHangupSettleUi.armed = 1;
    g_vmClientHangupSettleUi.completed = 0;
    g_vmClientHangupSettleUi.maxBattles = maxBattles;
    g_vmClientHangupSettleUi.totalExp = 0;
    g_vmClientHangupSettleUi.totalGold = 0;
    g_vmClientHangupSettleUi.totalHpRecovered = 0;
    g_vmClientHangupSettleUi.totalMpRecovered = 0;
    g_vmClientHangupSettleUi.lastAccountedToken = 0;
    printf("[info][network] cbe_hangup_settle_ui_arm max=%u reason=%s\n",
           maxBattles, reason != NULL ? reason : "unknown");
}

static bool vm_client_hangup_settle_ui_active(void)
{
    return g_vmClientHangupSettleUi.armed != 0 &&
           g_vmClientHangupSettleUi.maxBattles != 0;
}

static bool vm_client_deliver_local_scene_default25_5(u32 connectId);
static void vm_client_hangup_maybe_flush_scene_default_ack(u32 connectId,
                                                           const u8 *packet,
                                                           u32 packetLen);

static bool vm_client_decode_object_u32(const vm_client_response_object *object,
                                        const char *field, u32 *valueOut)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;

    if (valueOut != NULL)
        *valueOut = 0;
    if (object == NULL || valueOut == NULL ||
        !vm_client_response_object_field(object, field, &encoded, &encodedLen))
    {
        return false;
    }
    return vm_client_decode_tagged_u32(encoded, encodedLen, valueOut);
}

static bool vm_client_decode_object_u8(const vm_client_response_object *object,
                                       const char *field, u8 *valueOut)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;

    if (valueOut != NULL)
        *valueOut = 0;
    if (object == NULL || valueOut == NULL ||
        !vm_client_response_object_field(object, field, &encoded, &encodedLen))
    {
        return false;
    }
    if (encodedLen == 3u && encoded[0] == 0 && encoded[1] == 1)
    {
        *valueOut = encoded[2];
        return true;
    }
    if (encodedLen >= 1u)
    {
        *valueOut = encoded[encodedLen - 1u];
        return true;
    }
    return false;
}

static bool vm_client_object_has_field(const vm_client_response_object *object,
                                       const char *field)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;

    return object != NULL &&
           vm_client_response_object_field(object, field, &encoded, &encodedLen);
}

static bool vm_client_build_hangup_combatinfo_raw(u8 *out, u32 outCap, u16 *outLen,
                                                  u16 completed, u16 maximum,
                                                  u32 totalExp, u32 totalGold,
                                                  u32 totalMp, u32 totalHp)
{
    u32 pos = 0;

    if (outLen != NULL)
        *outLen = 0;
    if (out == NULL || outLen == NULL)
        return false;
    if (!vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, completed) ||
        !vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, maximum) ||
        !vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, totalExp) ||
        !vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, totalGold) ||
        !vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, totalMp) ||
        !vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, totalHp) ||
        !vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, 0) ||
        !vm_client_seq_put_tagged_u32_be32(out, outCap, &pos, 0) ||
        !vm_client_seq_put_tagged_u8(out, outCap, &pos, 0))
    {
        return false;
    }
    *outLen = (u16)pos;
    return true;
}

static bool vm_client_copy_object_field(const u8 *payload, u32 payloadLen,
                                        u32 *payloadPos, u8 *out, u32 outCap,
                                        u32 *outPos)
{
    u32 nameLen;
    u16 valueLen;

    if (payload == NULL || payloadPos == NULL || out == NULL || outPos == NULL ||
        *payloadPos >= payloadLen)
    {
        return false;
    }
    nameLen = payload[(*payloadPos)++];
    if (nameLen > payloadLen - *payloadPos)
        return false;
    if (*outPos + 1u + nameLen + 2u > outCap)
        return false;
    out[(*outPos)++] = (u8)nameLen;
    memcpy(out + *outPos, payload + *payloadPos, nameLen);
    *payloadPos += nameLen;
    *outPos += nameLen;
    if (*payloadPos + 2u > payloadLen)
        return false;
    valueLen = (u16)(((u16)payload[*payloadPos] << 8) | payload[*payloadPos + 1u]);
    *payloadPos += 2u;
    if (*payloadPos + valueLen > payloadLen ||
        *outPos + valueLen > outCap)
    {
        return false;
    }
    out[(*outPos)++] = (u8)(valueLen >> 8);
    out[(*outPos)++] = (u8)valueLen;
    memcpy(out + *outPos, payload + *payloadPos, valueLen);
    *payloadPos += valueLen;
    *outPos += valueLen;
    return true;
}

static bool vm_client_rewrite_battle47_add_combatinfo(
    const vm_client_response_object *object, u8 *outObj, u32 outCap, u32 *outLen,
    const u8 *combatinfo, u16 combatinfoLen)
{
    u32 payloadPos = 0;
    u32 pos = 6;
    bool inserted = false;

    if (outLen != NULL)
        *outLen = 0;
    if (object == NULL || outObj == NULL || outLen == NULL || outCap < 8 ||
        combatinfo == NULL || combatinfoLen == 0)
    {
        return false;
    }
    outObj[0] = object->major;
    outObj[1] = object->kind;
    outObj[2] = object->subtype;
    outObj[3] = 0;
    outObj[4] = 0;
    outObj[5] = 0;
    while (payloadPos < object->payloadLen)
    {
        u32 nameLen = object->payload[payloadPos];
        const char *fieldName = (const char *)(object->payload + payloadPos + 1);

        if ((u32)nameLen + 1u > object->payloadLen - payloadPos)
            return false;
        if (nameLen == 9 && memcmp(fieldName, "combatinfo", 9) == 0)
        {
            u16 skipLen = (u16)(((u16)object->payload[payloadPos + 1u + nameLen] << 8) |
                                object->payload[payloadPos + 2u + nameLen]);
            payloadPos += 1u + nameLen + 2u + skipLen;
            continue;
        }
        if (!inserted &&
            ((nameLen == 5 && memcmp(fieldName, "fdata", 5) == 0) ||
             (nameLen == 10 && memcmp(fieldName, "autorevive", 10) == 0)))
        {
            if (!vm_client_put_object_field(outObj, outCap, &pos, "combatinfo",
                                            combatinfo, combatinfoLen))
            {
                return false;
            }
            inserted = true;
        }
        if (!vm_client_copy_object_field(object->payload, object->payloadLen,
                                         &payloadPos, outObj, outCap, &pos))
        {
            return false;
        }
    }
    if (!inserted &&
        !vm_client_put_object_field(outObj, outCap, &pos, "combatinfo",
                                    combatinfo, combatinfoLen))
    {
        return false;
    }
    outObj[4] = (u8)(pos >> 8);
    outObj[5] = (u8)pos;
    *outLen = pos;
    return true;
}

static bool vm_client_packet_has_battle_subtype(const u8 *packet, u32 packetLen,
                                                 u8 subtype)
{
    u32 offset = 5;
    u8 objectCount = 0;
    u8 parsed = 0;
    vm_client_response_object object;

    if (packet == NULL || packetLen < 5 || packet[0] != 'W' || packet[1] != 'T')
        return false;
    objectCount = packet[4];
    while (parsed < objectCount &&
           offset + 6u <= packetLen &&
           vm_client_next_response_object(packet, packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 4 && object.subtype == subtype)
            return true;
        ++parsed;
    }
    return false;
}

static bool vm_client_packet_has_battle_subtype8(const u8 *packet, u32 packetLen)
{
    return vm_client_packet_has_battle_subtype(packet, packetLen, 8);
}

static bool vm_client_packet_is_empty_wt(const u8 *packet, u32 packetLen)
{
    return packet != NULL && packetLen == 5 &&
           packet[0] == 'W' && packet[1] == 'T' && packet[4] == 0;
}

static void vm_client_hangup_settle_note_downlink(const u8 *packet, u32 packetLen)
{
    u32 offset = 5;
    u8 objectCount = 0;
    u8 parsed = 0;
    vm_client_response_object object;
    bool settleTearDown = false;

    if (packet == NULL || packetLen < 5 || packet[0] != 'W' || packet[1] != 'T')
        return;
    settleTearDown = vm_client_packet_has_battle_subtype8(packet, packetLen);
    objectCount = packet[4];
    while (parsed < objectCount &&
           offset + 6u <= packetLen &&
           vm_client_next_response_object(packet, packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 4 && object.subtype == 7 &&
            vm_client_mem_has_hangup_zhong(object.payload, object.payloadLen))
        {
            if (!vm_client_hangup_settle_ui_active())
            {
                vm_client_hangup_settle_ui_arm(
                    (u16)VM_CLIENT_HANGUP_SETTLE_DEFAULT_MAX,
                    "4/7-fdata-挂机中");
            }
            if (g_vmClientHangupSettleUi.battleToken == 0)
                g_vmClientHangupSettleUi.battleToken = 1;
            printf("[info][network] cbe_hangup_fdata_mark "
                   "evidence=4/7.fdata-挂机中\n");
            g_vmClientHangupOnSettleShell = 1;
        }
        if (object.major == 1 && object.kind == 4 && object.subtype == 8)
            g_vmClientHangupOnSettleShell = 0;
        if (object.major == 1 && object.kind == 4 && object.subtype == 11)
        {
            u8 result = 0;
            u8 battle11Type = 0;
            (void)vm_client_decode_object_u8(&object, "result", &result);
            (void)vm_client_decode_object_u8(&object, "type", &battle11Type);
            if (result == 1 && battle11Type == 0)
            {
                /*
                 * Hangup loop exit used to be 4/8+4/11 type=0+4/9.  type=0 here
                 * is tear-down, not「关自动」; wiping combatinfo reset 挂机累计
                 * every fight.  Explicit auto-off is a 4/11-only packet.
                 */
                if (!settleTearDown)
                    vm_client_hangup_settle_ui_clear("battle-auto-off");
            }
            else if (result == 1 && battle11Type == 1)
            {
                if (!vm_client_hangup_settle_ui_active())
                {
                    vm_client_hangup_settle_ui_arm(
                        (u16)VM_CLIENT_HANGUP_SETTLE_DEFAULT_MAX,
                        "battle-auto-on-4/11");
                }
                ++g_vmClientHangupSettleUi.battleToken;
                g_vmClientHangupSettleUi.battlePreExp =
                    g_vmClientHangupSettleUi.lastRoleExp;
                g_vmClientHangupSettleUi.battlePreGold =
                    g_vmClientHangupSettleUi.lastRoleGold;
            }
        }
        if (object.major == 1 && object.kind == 25 && object.subtype == 6)
        {
            u32 insightMax = 0;
            /*
             * 25/6.maxnum is tagged-u16 200 while insight is active
             * (CBE_EMU item.dsh 828 / mock_scene_hangup_active_battle_limit).
             */
            if (vm_client_decode_object_u32(&object, "maxnum", &insightMax) &&
                insightMax >= 200u)
            {
                g_vmClientHangupSettleUi.maxBattles =
                    (u16)VM_CLIENT_HANGUP_SETTLE_INSIGHT_MAX;
            }
        }
        if (object.major == 1 && object.kind == 1 && object.subtype == 3)
        {
            char text[96];
            if (vm_client_response_object_string(&object, "info", text,
                                                 sizeof(text)) &&
                strstr(text, "\xD2\xD1\xCD\xA3\xD6\xB9\xB9\xD2\xBB\xFA") != NULL)
            {
                vm_client_hangup_settle_ui_clear("chat-stop-hangup");
            }
        }
        if (object.major == 1 && object.kind == 1 &&
            (object.subtype == 6 || object.subtype == 14))
        {
            u32 actorExp = 0;
            u32 actorGold = 0;
            if (vm_client_decode_object_u32(&object, "exp", &actorExp))
                g_vmClientHangupSettleUi.lastRoleExp = actorExp;
            if (vm_client_decode_object_u32(&object, "money", &actorGold))
                g_vmClientHangupSettleUi.lastRoleGold = actorGold;
        }
        ++parsed;
    }
}

static bool vm_client_cbe_inject_hangup_combatinfo(u8 **packet, u32 *packetLen)
{
    u32 offset = 5;
    u8 objectCount = 0;
    u8 parsed = 0;
    u8 newCount = 0;
    u32 srcPos = 5;
    u32 dstPos = 5;
    u8 *out = NULL;
    u32 outCap = 0;
    bool changed = false;
    vm_client_response_object object;

    if (packet == NULL || packetLen == NULL || *packet == NULL ||
        *packetLen < 5 || (*packet)[0] != 'W' || (*packet)[1] != 'T')
    {
        return false;
    }
    objectCount = (*packet)[4];
    outCap = *packetLen + 256u;
    out = (u8 *)malloc(outCap);
    if (out == NULL)
        return false;
    memcpy(out, *packet, 5);

    while (parsed < objectCount &&
           srcPos + 6u <= *packetLen &&
           vm_client_next_response_object(*packet, *packetLen, &srcPos, &object))
    {
        u32 objectStart = srcPos - ((u32)object.payloadLen + 6u);
        u32 objectLen = (u32)object.payloadLen + 6u;
        const u8 *copySrc = *packet + objectStart;
        u32 copyLen = objectLen;

        if (object.major == 1 && object.kind == 4 && object.subtype == 7)
        {
            bool hangupMark = vm_client_mem_has_hangup_zhong(
                object.payload, object.payloadLen);
            bool wantHangupUi =
                hangupMark || g_vmClientHangupNativeMode == 2;
            u8 result = 0;
            u32 wireExp = 0;
            u32 wireGold = 0;
            u32 recoverHp = 0;
            u32 recoverMp = 0;
            u32 expGain = 0;
            u32 goldGain = 0;
            u16 completed = 0;
            u16 maximum = 0;
            u8 combatInfo[64];
            u16 combatInfoLen = 0;
            u32 rewriteCap = 0;
            u8 *rewritten = NULL;
            u32 rewrittenLen = 0;
            bool doRewrite = false;

            if (hangupMark)
            {
                if (!vm_client_hangup_settle_ui_active())
                {
                    vm_client_hangup_settle_ui_arm(
                        (u16)VM_CLIENT_HANGUP_SETTLE_DEFAULT_MAX,
                        "4/7-fdata-挂机中");
                }
                if (g_vmClientHangupSettleUi.battleToken == 0)
                    g_vmClientHangupSettleUi.battleToken = 1;
            }
            if (!wantHangupUi || !vm_client_hangup_settle_ui_active())
            {
                /* Keep original 4/7 object. */
            }
            else
            {
                (void)vm_client_decode_object_u8(&object, "result", &result);
                (void)vm_client_decode_object_u32(&object, "exp", &wireExp);
                (void)vm_client_decode_object_u32(&object, "gold", &wireGold);
                (void)vm_client_decode_object_u32(&object, "hp", &recoverHp);
                (void)vm_client_decode_object_u32(&object, "mp", &recoverMp);
                if (result == 1 &&
                    g_vmClientHangupSettleUi.lastAccountedToken !=
                        g_vmClientHangupSettleUi.battleToken)
                {
                    /*
                     * 4/7.exp/gold are post-reward ABSOLUTE totals. Session
                     * panel needs this-fight gains = wire - baseline.
                     */
                    u32 baseExp = g_vmClientHangupSettleUi.battlePreExp;
                    u32 baseGold = g_vmClientHangupSettleUi.battlePreGold;

                    if (baseExp == 0)
                        baseExp = g_vmClientHangupSettleUi.lastRoleExp;
                    if (baseGold == 0)
                        baseGold = g_vmClientHangupSettleUi.lastRoleGold;
                    if (baseExp != 0 && wireExp >= baseExp)
                        expGain = wireExp - baseExp;
                    if (baseGold != 0 && wireGold >= baseGold)
                        goldGain = wireGold - baseGold;
                    g_vmClientHangupSettleUi.totalExp =
                        vm_client_add_capped_u32(
                            g_vmClientHangupSettleUi.totalExp, expGain);
                    g_vmClientHangupSettleUi.totalGold =
                        vm_client_add_capped_u32(
                            g_vmClientHangupSettleUi.totalGold, goldGain);
                    g_vmClientHangupSettleUi.totalHpRecovered =
                        vm_client_add_capped_u32(
                            g_vmClientHangupSettleUi.totalHpRecovered,
                            recoverHp);
                    g_vmClientHangupSettleUi.totalMpRecovered =
                        vm_client_add_capped_u32(
                            g_vmClientHangupSettleUi.totalMpRecovered,
                            recoverMp);
                    if (wireExp != 0)
                        g_vmClientHangupSettleUi.lastRoleExp = wireExp;
                    if (wireGold != 0)
                        g_vmClientHangupSettleUi.lastRoleGold = wireGold;
                    g_vmClientHangupSettleUi.lastAccountedToken =
                        g_vmClientHangupSettleUi.battleToken;
                    if (g_vmClientHangupSettleUi.completed < 0xffffu)
                        ++g_vmClientHangupSettleUi.completed;
                }
                completed = g_vmClientHangupSettleUi.completed;
                maximum = g_vmClientHangupSettleUi.maxBattles;
                /*
                 * Optional wire combatinfo (native ignores without 1136).
                 * Host overlay always uses the tally below.
                 */
                doRewrite = !vm_client_object_has_field(&object, "combatinfo");
                if (doRewrite)
                {
                    rewriteCap = (u32)object.payloadLen + 6u + 128u;
                    if (rewriteCap < 256u)
                        rewriteCap = 256u;
                    rewritten = (u8 *)malloc(rewriteCap);
                    if (rewritten == NULL)
                    {
                        free(out);
                        return false;
                    }
                    if (!vm_client_build_hangup_combatinfo_raw(
                            combatInfo, sizeof(combatInfo), &combatInfoLen,
                            completed, maximum,
                            g_vmClientHangupSettleUi.totalExp,
                            g_vmClientHangupSettleUi.totalGold,
                            g_vmClientHangupSettleUi.totalMpRecovered,
                            g_vmClientHangupSettleUi.totalHpRecovered) ||
                        !vm_client_rewrite_battle47_add_combatinfo(
                            &object, rewritten, rewriteCap, &rewrittenLen,
                            combatInfo, combatInfoLen))
                    {
                        printf("[warn][network] "
                               "cbe_hangup_combatinfo_rewrite_fail "
                               "payload=%u cap=%u "
                               "evidence=manual-settle-fallback\n",
                               object.payloadLen, rewriteCap);
                        free(rewritten);
                        rewritten = NULL;
                        doRewrite = false;
                    }
                    else
                    {
                        copySrc = rewritten;
                        copyLen = rewrittenLen;
                        changed = true;
                    }
                }
                printf("[info][network] cbe_hangup_combatinfo tally=%u/%u "
                       "exp=%u gold=%u hp=%u mp=%u raw_len=%u rewrite=%u "
                       "evidence=4/7.fdata-挂机中|combatinfo\n",
                       completed, maximum, g_vmClientHangupSettleUi.totalExp,
                       g_vmClientHangupSettleUi.totalGold,
                       g_vmClientHangupSettleUi.totalHpRecovered,
                       g_vmClientHangupSettleUi.totalMpRecovered, combatInfoLen,
                       doRewrite ? 1u : 0u);
                vm_client_hangup_log_mode_flag("at-4/7-combatinfo");
                if (doRewrite && rewritten != NULL)
                {
                    if (dstPos + copyLen > outCap)
                    {
                        u32 grownCap = outCap + copyLen + 128u;
                        u8 *grown = (u8 *)realloc(out, grownCap);
                        if (grown == NULL)
                        {
                            free(rewritten);
                            free(out);
                            return false;
                        }
                        out = grown;
                        outCap = grownCap;
                    }
                    memcpy(out + dstPos, copySrc, copyLen);
                    dstPos += copyLen;
                    ++newCount;
                    ++parsed;
                    free(rewritten);
                    continue;
                }
                if (rewritten != NULL)
                    free(rewritten);
            }
        }
        if (dstPos + copyLen > outCap)
        {
            u32 grownCap = outCap + copyLen + 128u;
            u8 *grown = (u8 *)realloc(out, grownCap);
            if (grown == NULL)
            {
                free(out);
                return false;
            }
            out = grown;
            outCap = grownCap;
        }
        memcpy(out + dstPos, copySrc, copyLen);
        dstPos += copyLen;
        ++newCount;
        ++parsed;
    }
    if (!changed || parsed != objectCount || srcPos != *packetLen)
    {
        free(out);
        return false;
    }
    vm_client_finish_wt_packet(out, dstPos, newCount);
    if (dstPos > *packetLen)
    {
        u8 *grownPacket = (u8 *)realloc(*packet, dstPos);
        if (grownPacket == NULL)
        {
            free(out);
            return false;
        }
        *packet = grownPacket;
    }
    memcpy(*packet, out, dstPos);
    *packetLen = dstPos;
    free(out);
    return true;
}

/*
 * Native hangup needs the server 25/3 panel to reach guest Handle25_3
 * (mmBattle:0x8996) so confirm → sub_28A4 → 25/2 can set game+1136.
 * Do not rewrite the panel to dismiss; that path left flag1136=255 and
 * manual settle despite combatinfo on the wire.
 */
static bool vm_client_hangup_rewrite_panel_to_dismiss(u8 **packet,
                                                      u32 *packetLen)
{
    (void)packet;
    (void)packetLen;
    return false;
}

/*
 * Hangup 4/7 stays on Battle.cbm.  Seed 1/14 is mmGame/mmShop actor-state
 * (0x9DE).  Same-packet 4/6+1/14+4/7 DIV0s at rand_range (stack len=718).
 */
static bool vm_client_hangup_strip_settle_actor14(u8 **packet, u32 *packetLen)
{
    u32 offset = 5;
    u32 outPos = 5;
    u8 objectCount = 0;
    u8 removed = 0;
    u8 hasHangup47 = 0;
    u8 *out = NULL;
    u8 *src = NULL;
    u32 outCap = 0;
    vm_client_response_object object;

    if (packet == NULL || *packet == NULL || packetLen == NULL ||
        *packetLen < 5 || (*packet)[0] != 'W' || (*packet)[1] != 'T')
        return false;
    src = *packet;
    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(src, *packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 4 && object.subtype == 7 &&
            vm_client_mem_has_hangup_zhong(object.payload, object.payloadLen))
            hasHangup47 = 1;
    }
    if (hasHangup47 == 0)
        return false;

    outCap = *packetLen;
    out = (u8 *)malloc(outCap);
    if (out == NULL)
        return false;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
    offset = 5;
    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(src, *packetLen, &offset, &object))
    {
        u32 objectStart = offset - ((u32)object.payloadLen + 6u);
        u32 objectLen = (u32)object.payloadLen + 6u;

        if (object.major == 1 && object.kind == 1 && object.subtype == 14)
        {
            ++removed;
            continue;
        }
        if (outPos + objectLen > outCap)
        {
            u8 *grown = (u8 *)realloc(out, outCap + objectLen + 64u);
            if (grown == NULL)
            {
                free(out);
                return false;
            }
            out = grown;
            outCap += objectLen + 64u;
        }
        memcpy(out + outPos, src + objectStart, objectLen);
        outPos += objectLen;
        ++objectCount;
    }
    if (offset != *packetLen || removed == 0)
    {
        free(out);
        return false;
    }
    vm_client_finish_wt_packet(out, outPos, objectCount);
    if (outPos > *packetLen)
    {
        u8 *grown = (u8 *)realloc(*packet, outPos);
        if (grown == NULL)
        {
            free(out);
            return false;
        }
        *packet = grown;
    }
    memcpy(*packet, out, outPos);
    *packetLen = outPos;
    free(out);
    printf("[info][network] cbe_hangup_strip_actor14 removed=%u objects=%u "
           "resp=%u evidence=4/7-shell-1/14-div0-0x0104e980\n",
           removed, objectCount, outPos);
    return true;
}

static bool vm_client_response_has_object(const u8 *packet, u32 packetLen,
                                          u8 kind, u8 subtype, u16 minPayload)
{
    u32 offset = 5;
    vm_client_response_object object;

    if (packet == NULL || packetLen < 5 || packet[0] != 'W' || packet[1] != 'T')
        return false;
    while (offset + 6u <= packetLen &&
           vm_client_next_response_object(packet, packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == kind &&
            object.subtype == subtype && object.payloadLen >= minPayload)
            return true;
    }
    return false;
}

static u32 g_vmHangupLearnedSkillIds[12];
static u8 g_vmHangupLearnedSkillCount;
static u32 g_vmHangupLastOperate;
static u8 g_vmHangupLastOperateValid;
static bool vm_client_find_u8_field(const u8 *buf, u32 len, const char *field,
                                    u8 *value);

static void vm_client_hangup_note_learnedskill_blob(const u8 *blob, u16 blobLen)
{
    u32 pos = 0;
    u8 n = 0;

    if (blob == NULL || blobLen < 6u)
        return;
    memset(g_vmHangupLearnedSkillIds, 0, sizeof(g_vmHangupLearnedSkillIds));
    while (pos + 6u <= blobLen && n < 12u)
    {
        u32 skillId;

        if (blob[pos] != 0 || blob[pos + 1u] != 4)
            break;
        skillId = ((u32)blob[pos + 2u] << 24) | ((u32)blob[pos + 3u] << 16) |
                  ((u32)blob[pos + 4u] << 8) | (u32)blob[pos + 5u];
        pos += 6u;
        if (skillId == 0)
            continue;
        g_vmHangupLearnedSkillIds[n++] = skillId;
    }
    g_vmHangupLearnedSkillCount = n;
    if (n != 0)
        printf("[info][network] cbe_hangup_learnedskill count=%u "
               "evidence=downlink-learnedskill\n",
               n);
}

static void vm_client_hangup_note_downlink_skills(const u8 *packet, u32 packetLen)
{
    u32 offset = 5;
    vm_client_response_object object;

    if (packet == NULL || packetLen < 5 || packet[0] != 'W' || packet[1] != 'T')
        return;
    while (offset + 6u <= packetLen &&
           vm_client_next_response_object(packet, packetLen, &offset, &object))
    {
        const u8 *blob = NULL;
        u16 blobLen = 0;

        if (vm_client_response_object_field(&object, "learnedskill", &blob,
                                            &blobLen) &&
            blobLen >= 6u)
            vm_client_hangup_note_learnedskill_blob(blob, blobLen);
    }
}

static void vm_client_hangup_note_operate_uplink(const u8 *req, u32 len)
{
    u32 operate = 0;
    u8 operate8 = 0;

    if (req == NULL || len < 9 || req[0] != 'W' || req[1] != 'T')
        return;
    if (req[5] != 4 || req[6] != 2)
        return;
    if (vm_client_find_u8_field(req, len, "Operate", &operate8))
        operate = operate8;
    {
        u32 i;
        u32 fieldLen = 7;

        for (i = 0; i + fieldLen + 8 <= len; ++i)
        {
            if (req[i] != 7 || memcmp(req + i + 1, "Operate", 7) != 0)
                continue;
            if (i + 1 + 7 + 2 + 4 <= len && req[i + 8] == 0 && req[i + 9] == 4)
            {
                operate = ((u32)req[i + 10] << 24) | ((u32)req[i + 11] << 16) |
                          ((u32)req[i + 12] << 8) | (u32)req[i + 13];
                break;
            }
        }
    }
    g_vmHangupLastOperate = operate;
    g_vmHangupLastOperateValid = 1;
}

enum
{
    VM_CLIENT_HANGUP_SKILL_NAME_MAX = 24,
    VM_CLIENT_HANGUP_SKILL_CATALOG_MAX = 256
};

typedef struct
{
    u32 skillId;
    u8 name[VM_CLIENT_HANGUP_SKILL_NAME_MAX];
} vm_client_hangup_skill_name_row;

static vm_client_hangup_skill_name_row
    g_vmHangupSkillNames[VM_CLIENT_HANGUP_SKILL_CATALOG_MAX];
static u32 g_vmHangupSkillNameCount;
static u8 g_vmHangupSkillNamesTried;

static u32 vm_client_hangup_read_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 vm_client_hangup_parse_dsh_u32(const u8 *raw, u32 len)
{
    u32 value = 0;
    u8 haveDigit = 0;
    u32 i;

    if (raw == NULL || len == 0)
        return 0;
    for (i = 0; i < len; ++i)
    {
        if (raw[i] < '0' || raw[i] > '9')
            break;
        haveDigit = 1;
        value = value * 10u + (u32)(raw[i] - '0');
    }
    return haveDigit ? value : 0;
}

static void vm_client_hangup_store_skill_name(u32 skillId, const u8 *name,
                                              u32 nameLen)
{
    vm_client_hangup_skill_name_row *row;
    u32 n;

    if (skillId == 0 || name == NULL || nameLen == 0)
        return;
    if (g_vmHangupSkillNameCount >= VM_CLIENT_HANGUP_SKILL_CATALOG_MAX)
        return;
    while (nameLen > 0 && (name[nameLen - 1u] == 0 || name[nameLen - 1u] == ' '))
        nameLen--;
    if (nameLen == 0)
        return;
    row = &g_vmHangupSkillNames[g_vmHangupSkillNameCount++];
    memset(row, 0, sizeof(*row));
    row->skillId = skillId;
    n = nameLen;
    if (n >= VM_CLIENT_HANGUP_SKILL_NAME_MAX)
        n = VM_CLIENT_HANGUP_SKILL_NAME_MAX - 1u;
    memcpy(row->name, name, n);
}

static void vm_client_hangup_load_skill_names(void)
{
    static const char *paths[] = {
        "JHOnlineData/skill.dsh",
        "bin/JHOnlineData/skill.dsh"
    };
    static u8 data[32768];
    FILE *fp = NULL;
    size_t nread = 0;
    u32 len;
    u32 columnCount;
    u32 rowCount;
    u32 pos;
    u32 row;
    u32 i;

    if (g_vmHangupSkillNamesTried)
        return;
    g_vmHangupSkillNamesTried = 1;
    g_vmHangupSkillNameCount = 0;
    for (i = 0; i < (u32)(sizeof(paths) / sizeof(paths[0])); ++i)
    {
        fp = fopen(paths[i], "rb");
        if (fp != NULL)
            break;
    }
    if (fp == NULL)
        return;
    nread = fread(data, 1, sizeof(data), fp);
    fclose(fp);
    if (nread < 16)
        return;
    len = (u32)nread;
    columnCount = vm_client_hangup_read_le32(data + 4);
    rowCount = vm_client_hangup_read_le32(data + 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 512)
        return;
    pos = 16;
    for (i = 0; i < columnCount; ++i)
    {
        u32 fieldLen;

        if (pos >= len)
            return;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return;
        pos += fieldLen;
    }
    for (row = 0; row < rowCount && pos + 4u <= len; ++row)
    {
        u32 rowLen = vm_client_hangup_read_le32(data + pos);
        u32 rowPos = pos + 4u;
        u32 rowEnd = rowPos + rowLen;
        u32 skillId = 0;
        const u8 *name = NULL;
        u32 nameLen = 0;
        u32 col;

        if (rowEnd > len || rowEnd < rowPos)
            break;
        for (col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
                break;
            if (col == 0)
                skillId = vm_client_hangup_parse_dsh_u32(value, valueLen);
            else if (col == 1)
            {
                name = value;
                nameLen = valueLen;
            }
            rowPos += valueLen;
        }
        vm_client_hangup_store_skill_name(skillId, name, nameLen);
        pos = rowEnd;
    }
    printf("[info][network] cbe_hangup_skill_names count=%u "
           "evidence=JHOnlineData/skill.dsh\n",
           g_vmHangupSkillNameCount);
}

static int vm_client_hangup_copy_skill_name(u32 skillId, u8 *gbk, u32 cap)
{
    u32 i;

    if (skillId == 0 || gbk == NULL || cap == 0)
        return 0;
    vm_client_hangup_load_skill_names();
    for (i = 0; i < g_vmHangupSkillNameCount; ++i)
    {
        if (g_vmHangupSkillNames[i].skillId != skillId)
            continue;
        snprintf((char *)gbk, cap, "%s", (const char *)g_vmHangupSkillNames[i].name);
        return gbk[0] != 0;
    }
    return 0;
}

u8 vm_client_hangup_skill_option_count(void)
{
    u8 n = (u8)(1u + g_vmHangupLearnedSkillCount);

    if (g_vmHangupLastOperateValid && g_vmHangupLastOperate != 0)
    {
        u8 i;
        u8 found = 0;

        for (i = 0; i < g_vmHangupLearnedSkillCount; ++i)
        {
            if (g_vmHangupLearnedSkillIds[i] + 2u == g_vmHangupLastOperate)
            {
                found = 1;
                break;
            }
        }
        if (!found)
            n++;
    }
    return n;
}

u32 vm_client_hangup_skill_option_operate(u8 index)
{
    u8 learned = g_vmHangupLearnedSkillCount;

    if (index == 0)
        return 0;
    if (index <= learned)
        return g_vmHangupLearnedSkillIds[index - 1u] + 2u;
    if (g_vmHangupLastOperateValid)
        return g_vmHangupLastOperate;
    return 0;
}

void vm_client_hangup_skill_option_label(u8 index, u8 *gbk, u32 cap)
{
    static const u8 kNa[] = {0xC6, 0xD5, 0xCD, 0xA8, 0xB9, 0xA5, 0xBB, 0xF7, 0};
    u32 operate;
    u32 skillId;
    int wrote;

    if (gbk == NULL || cap == 0)
        return;
    gbk[0] = 0;
    if (index == 0)
    {
        if (cap >= sizeof(kNa))
            memcpy(gbk, kNa, sizeof(kNa));
        return;
    }
    operate = vm_client_hangup_skill_option_operate(index);
    skillId = (operate >= 2u) ? (operate - 2u) : operate;
    if (vm_client_hangup_copy_skill_name(skillId, gbk, cap))
        return;
    wrote = snprintf((char *)gbk, cap, "\xBC\xBC\xC4\xDC#%u", skillId);
    if (wrote < 0)
        gbk[0] = 0;
}

static bool vm_client_hangup_pending_set_operate(u32 operate)
{
    u8 out[VM_CLIENT_REQUEST_MAX];
    u32 srcPos = 4;
    u32 dstPos = 4;
    u32 len = g_vmClientHangupPendingStartLen;
    const u8 *req = g_vmClientHangupPendingStart;
    u8 operateEnc[6];

    if (req == NULL || len < 9 || req[0] != 'W' || req[1] != 'T')
        return false;
    memcpy(out, req, 4);
    operateEnc[0] = 0;
    operateEnc[1] = 4;
    operateEnc[2] = (u8)(operate >> 24);
    operateEnc[3] = (u8)(operate >> 16);
    operateEnc[4] = (u8)(operate >> 8);
    operateEnc[5] = (u8)operate;
    while (srcPos + 5 <= len)
    {
        u16 objectLen = (u16)(((u16)req[srcPos + 3] << 8) | req[srcPos + 4]);
        u32 objectStart = dstPos;

        if (objectLen < 5 || srcPos + objectLen > len)
            return false;
        if (dstPos + objectLen + 16u > sizeof(out))
            return false;
        memcpy(out + dstPos, req + srcPos, 5);
        dstPos += 5;
        if (srcPos + 5 < srcPos + objectLen)
        {
            memcpy(out + dstPos, req + srcPos + 5, objectLen - 5u);
            dstPos += objectLen - 5u;
        }
        if (req[srcPos] == 1 && req[srcPos + 1] == 2 && req[srcPos + 2] == 10)
        {
            if (!vm_client_put_object_field(out, sizeof(out), &dstPos, "Operate",
                                            operateEnc, 6))
                return false;
            {
                u32 newLen = dstPos - objectStart;

                out[objectStart + 3] = (u8)(newLen >> 8);
                out[objectStart + 4] = (u8)newLen;
            }
        }
        srcPos += objectLen;
    }
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(dstPos >> 8);
    out[3] = (u8)dstPos;
    if (dstPos > sizeof(g_vmClientHangupPendingStart))
        return false;
    memcpy(g_vmClientHangupPendingStart, out, dstPos);
    g_vmClientHangupPendingStartLen = dstPos;
    printf("[info][network] cbe_hangup_pending_operate operate=%u len=%u\n",
           operate, dstPos);
    return true;
}

static bool vm_client_hangup_put_u32_field(u8 *out, u32 outCap, u32 *pos,
                                           const char *name, u32 value)
{
    u8 enc[6];

    enc[0] = 0;
    enc[1] = 4;
    enc[2] = (u8)(value >> 24);
    enc[3] = (u8)(value >> 16);
    enc[4] = (u8)(value >> 8);
    enc[5] = (u8)value;
    return vm_client_put_object_field(out, outCap, pos, name, enc, 6);
}

static bool vm_client_hangup_pick_live_combat(u32 *idOut, u32 *indexOut,
                                              u32 *posxOut, u32 *posyOut)
{
    u32 sceneNodeBase = 0;
    u16 playerX = 0;
    u16 playerY = 0;
    u32 bestDist = 0xffffffffu;
    bool found = false;

    if (idOut)
        *idOut = 0;
    if (indexOut)
        *indexOut = 0;
    if (posxOut)
        *posxOut = 0;
    if (posyOut)
        *posyOut = 0;
    if (Global_R9 == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5CB0, &sceneNodeBase,
                    sizeof(sceneNodeBase)) != UC_ERR_OK ||
        sceneNodeBase == 0)
        return false;
    (void)uc_mem_read(MTK, Global_R9 + 0x5C8E, &playerX, sizeof(playerX));
    (void)uc_mem_read(MTK, Global_R9 + 0x5C90, &playerY, sizeof(playerY));

    for (u32 i = 0; i < 25; ++i)
    {
        u32 node = sceneNodeBase + i * 340u;
        u32 actorId = 0;
        u32 battleX = 0;
        u32 battleY = 0;
        u16 gridX = 0;
        u16 gridY = 0;
        u8 active = 0;
        u8 nodeKind = 0;
        u32 dist;
        u32 dx;
        u32 dy;

        if (uc_mem_read(MTK, node + 319, &active, sizeof(active)) != UC_ERR_OK ||
            active == 0)
            continue;
        if (uc_mem_read(MTK, node + 0x13B, &nodeKind, sizeof(nodeKind)) !=
                UC_ERR_OK ||
            nodeKind != 2)
            continue;
        if (uc_mem_read(MTK, node + 100, &actorId, sizeof(actorId)) != UC_ERR_OK ||
            actorId == 0 || actorId == 1u || actorId == 0x70ffffffu)
            continue;
        (void)uc_mem_read(MTK, node + 240, &battleX, sizeof(battleX));
        (void)uc_mem_read(MTK, node + 244, &battleY, sizeof(battleY));
        if (battleX == 0 || battleY == 0)
            continue;
        (void)uc_mem_read(MTK, node + 0x18, &gridX, sizeof(gridX));
        (void)uc_mem_read(MTK, node + 0x1A, &gridY, sizeof(gridY));
        dx = gridX > playerX ? (u32)(gridX - playerX) : (u32)(playerX - gridX);
        dy = gridY > playerY ? (u32)(gridY - playerY) : (u32)(playerY - gridY);
        dist = dx + dy;
        if (dist >= bestDist)
            continue;
        bestDist = dist;
        found = true;
        if (idOut)
            *idOut = actorId;
        if (indexOut)
            *indexOut = i;
        if (posxOut)
            *posxOut = battleX;
        if (posyOut)
            *posyOut = battleY;
    }
    return found;
}

static u32 vm_client_hangup_bind_live_combat(u8 *req, u32 len, u32 cap)
{
    u8 out[VM_CLIENT_REQUEST_MAX];
    u32 srcPos = 4;
    u32 dstPos = 4;
    u32 liveId = 0;
    u32 liveIndex = 0;
    u32 livePosX = 0;
    u32 livePosY = 0;

    if (req == NULL || len < 9 || len > cap || req[0] != 'W' || req[1] != 'T')
        return len;
    {
        bool hasPosx = false;
        bool hasIndex = false;
        u32 i;

        for (i = 0; i + 4u <= len; ++i)
        {
            if (!hasPosx && memcmp(req + i, "posx", 4) == 0)
                hasPosx = true;
            if (!hasIndex && i + 5u <= len && memcmp(req + i, "index", 5) == 0)
                hasIndex = true;
        }
        if (hasPosx && hasIndex)
            return len;
    }
    if (!vm_client_hangup_pick_live_combat(&liveId, &liveIndex, &livePosX,
                                           &livePosY))
    {
        printf("[info][network] cbe_hangup_live_bind skip "
               "evidence=no-kind2-battle-pos\n");
        return len;
    }
    memcpy(out, req, 4);
    while (srcPos + 5 <= len)
    {
        u16 objectLen = (u16)(((u16)req[srcPos + 3] << 8) | req[srcPos + 4]);
        u32 objectStart = dstPos;

        if (objectLen < 5 || srcPos + objectLen > len)
            return len;
        if (dstPos + objectLen + 64u > sizeof(out) || dstPos + objectLen + 64u > cap)
            return len;
        memcpy(out + dstPos, req + srcPos, 5);
        dstPos += 5;
        if (srcPos + 5 < srcPos + objectLen)
        {
            memcpy(out + dstPos, req + srcPos + 5, objectLen - 5u);
            dstPos += objectLen - 5u;
        }
        if (req[srcPos] == 1 && req[srcPos + 1] == 2 && req[srcPos + 2] == 10)
        {
            if (!vm_client_hangup_put_u32_field(out, sizeof(out), &dstPos, "id",
                                                liveId) ||
                !vm_client_hangup_put_u32_field(out, sizeof(out), &dstPos,
                                                "index", liveIndex) ||
                !vm_client_hangup_put_u32_field(out, sizeof(out), &dstPos,
                                                "posx", livePosX) ||
                !vm_client_hangup_put_u32_field(out, sizeof(out), &dstPos,
                                                "posy", livePosY))
                return len;
            {
                u32 newLen = dstPos - objectStart;

                out[objectStart + 3] = (u8)(newLen >> 8);
                out[objectStart + 4] = (u8)newLen;
            }
        }
        srcPos += objectLen;
    }
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(dstPos >> 8);
    out[3] = (u8)dstPos;
    if (dstPos > cap)
        return len;
    memcpy(req, out, dstPos);
    printf("[info][network] cbe_hangup_live_bind id=%u index=%u pos=(%u,%u) "
           "len=%u evidence=subtype5-request-live-node\n",
           liveId, liveIndex, livePosX, livePosY, dstPos);
    return dstPos;
}

int vm_client_hangup_confirm_skill_and_start(u32 operate)
{
    u32 connectId = g_vmClientHangupPendingConnectId;

    (void)vm_client_hangup_pending_set_operate(operate);
    g_vmHangupLastOperate = operate;
    g_vmHangupLastOperateValid = 1;
    return vm_client_hangup_ack_guest_25_2(connectId);
}

/*
 * Deferred login backpack seed: strip heavy 30/21 (+7/11) from the group-type1
 * WT, then flush once as a follow-up event=7 after that login delivery.
 * Do NOT strip 17/1 — scene startup keeps 7/42+17/1 together; orphan 17/1
 * (bag UI not open) caused「解包错误」and wedged the second login.
 * One 30/21 only (repeat duplicates stacks).
 */
typedef struct
{
    u8 armed;
    u8 delivered;
    u16 gridRows;
    u8 *gridPacket;
    u32 gridPacketLen;
} vm_client_cbe_bag_defer_state;

static vm_client_cbe_bag_defer_state g_vmClientCbeBagDefer;

static void vm_client_cbe_bag_defer_clear(void)
{
    free(g_vmClientCbeBagDefer.gridPacket);
    memset(&g_vmClientCbeBagDefer, 0, sizeof(g_vmClientCbeBagDefer));
}

static bool vm_client_cbe_bag_defer_strip_login_grid(u8 **packet, u32 *packetLen)
{
    u32 offset = 5;
    u32 primaryPos = 5;
    u32 deferPos = 5;
    u8 primaryCount = 0;
    u8 deferCount = 0;
    u32 gridRows = 0;
    bool have3021 = false;
    u8 *deferOut = NULL;
    u32 deferCap = 0;
    vm_client_response_object object;

    if (packet == NULL || *packet == NULL || packetLen == NULL || *packetLen < 10)
        return false;
    if (g_vmClientCbeBagDefer.armed && !g_vmClientCbeBagDefer.delivered)
        return false;

    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(*packet, *packetLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 30 && object.subtype == 21)
        {
            u32 rows = 0;

            if (!vm_client_decode_object_u32(&object, "gridnum", &rows))
                return false;
            gridRows = rows;
            have3021 = true;
        }
    }
    if (!have3021 || gridRows <= (u32)VM_CLIENT_CBE_BAG_DEFER_ROW_THRESHOLD)
        return false;

    deferCap = *packetLen + 64u;
    deferOut = (u8 *)malloc(deferCap);
    if (deferOut == NULL)
        return false;
    deferOut[0] = 'W';
    deferOut[1] = 'T';
    deferOut[2] = 0;
    deferOut[3] = 0;
    deferOut[4] = 0;

    offset = 5;
    while (offset + 6u <= *packetLen)
    {
        u32 start = offset;
        u32 objectLen;
        bool take;

        if (!vm_client_next_response_object(*packet, *packetLen, &offset,
                                             &object))
        {
            free(deferOut);
            return false;
        }
        objectLen = offset - start;
        take = (object.major == 1 && object.kind == 30 &&
                object.subtype == 21) ||
               (object.major == 1 && object.kind == 7 && object.subtype == 11);
        if (take)
        {
            if (deferPos + objectLen > deferCap || deferCount == 0xff)
            {
                free(deferOut);
                return false;
            }
            memcpy(deferOut + deferPos, *packet + start, objectLen);
            deferPos += objectLen;
            ++deferCount;
        }
        else
        {
            memmove(*packet + primaryPos, *packet + start, objectLen);
            primaryPos += objectLen;
            ++primaryCount;
        }
    }
    if (deferCount == 0 || primaryCount == 0)
    {
        free(deferOut);
        return false;
    }
    vm_client_finish_wt_packet(*packet, primaryPos, primaryCount);
    vm_client_finish_wt_packet(deferOut, deferPos, deferCount);
    *packetLen = primaryPos;
    vm_client_cbe_bag_defer_clear();
    g_vmClientCbeBagDefer.armed = 1;
    g_vmClientCbeBagDefer.gridRows = (u16)gridRows;
    g_vmClientCbeBagDefer.gridPacket = deferOut;
    g_vmClientCbeBagDefer.gridPacketLen = deferPos;
    printf("[info][network] cbe_bag_defer strip rows=%u primary=%u defer=%u "
           "evidence=login-HandleItemGrid-off-critical-path\n",
           (unsigned)gridRows, primaryPos, deferPos);
    return true;
}

static bool vm_client_cbe_bag_defer_flush(u32 connectId)
{
    vm_net_channel *channel;
    u32 events;

    if (!g_vmClientCbeBagDefer.armed || g_vmClientCbeBagDefer.delivered ||
        g_vmClientCbeBagDefer.gridPacket == NULL ||
        g_vmClientCbeBagDefer.gridPacketLen < 5)
        return false;
    channel = scheduler_find_net_channel(connectId);
    if (channel == NULL || channel->callback == 0)
    {
        printf("[warn][network] cbe_bag_defer flush_no_channel connect=%u "
               "rows=%u\n",
               connectId, (unsigned)g_vmClientCbeBagDefer.gridRows);
        return false;
    }
    events = vm_client_deliver_wt_response(
        g_vmClientCbeBagDefer.gridPacket, g_vmClientCbeBagDefer.gridPacketLen, 7,
        channel->callback, channel->context, connectId, false, false);
    if (events == 0)
    {
        printf("[warn][network] cbe_bag_defer flush_grid_failed connect=%u "
               "rows=%u len=%u\n",
               connectId, (unsigned)g_vmClientCbeBagDefer.gridRows,
               g_vmClientCbeBagDefer.gridPacketLen);
        return false;
    }
    g_netDownLinkData += g_vmClientCbeBagDefer.gridPacketLen;
    printf("[info][network] cbe_bag_defer deliver rows=%u len=%u connect=%u "
           "evidence=post-login-followup-HandleItemGrid\n",
           (unsigned)g_vmClientCbeBagDefer.gridRows,
           g_vmClientCbeBagDefer.gridPacketLen, connectId);
    g_vmClientCbeBagDefer.delivered = 1;
    vm_client_cbe_bag_defer_clear();
    return true;
}

/*
 * Mock / CBE group-type1 contract: 30/21 seeds the bag, then 7/7 type=2 fills
 * 装备栏 (+104), then 7/11 reservoir. Live often emits 30/21,7/11,7/7 — move
 * 7/7 to immediately after 30/21. Do NOT put 7/7 before 30/21 (manager not ready).
 */
static bool vm_client_cbe_login_bag_order(u8 **packet, u32 *packetLen)
{
    u32 offset = 5;
    u32 outPos = 5;
    u8 objectCount = 0;
    u8 *out = NULL;
    u8 *src = NULL;
    u32 outCap = 0;
    u32 gridStart = 0;
    u32 gridLen = 0;
    u32 equipStart = 0;
    u32 equipLen = 0;
    u8 sawGrid = 0;
    u8 sawEquip = 0;
    u8 lastWasGrid = 0;
    u8 equipRightAfterGrid = 0;
    vm_client_response_object object;

    if (packet == NULL || *packet == NULL || packetLen == NULL ||
        *packetLen < 5 || (*packet)[0] != 'W' || (*packet)[1] != 'T')
        return false;
    src = *packet;
    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(src, *packetLen, &offset, &object))
    {
        u32 start = offset - ((u32)object.payloadLen + 6u);
        u32 len = (u32)object.payloadLen + 6u;

        if (object.major == 1 && object.kind == 30 && object.subtype == 21)
        {
            gridStart = start;
            gridLen = len;
            sawGrid = 1;
            lastWasGrid = 1;
        }
        else if (object.major == 1 && object.kind == 7 && object.subtype == 7)
        {
            equipStart = start;
            equipLen = len;
            sawEquip = 1;
            if (lastWasGrid)
                equipRightAfterGrid = 1;
            lastWasGrid = 0;
        }
        else
            lastWasGrid = 0;
    }
    if (offset != *packetLen || !sawGrid || !sawEquip || equipRightAfterGrid)
        return false;

    outCap = *packetLen;
    out = (u8 *)malloc(outCap);
    if (out == NULL)
        return false;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
    offset = 5;
    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(src, *packetLen, &offset, &object))
    {
        u32 start = offset - ((u32)object.payloadLen + 6u);
        u32 len = (u32)object.payloadLen + 6u;

        if (start == gridStart)
        {
            if (outPos + gridLen + equipLen > outCap || objectCount >= 0xfe)
            {
                free(out);
                return false;
            }
            memcpy(out + outPos, src + gridStart, gridLen);
            outPos += gridLen;
            ++objectCount;
            memcpy(out + outPos, src + equipStart, equipLen);
            outPos += equipLen;
            ++objectCount;
            continue;
        }
        if (start == equipStart)
            continue;
        if (outPos + len > outCap || objectCount == 0xff)
        {
            free(out);
            return false;
        }
        memcpy(out + outPos, src + start, len);
        outPos += len;
        ++objectCount;
    }
    if (offset != *packetLen || objectCount == 0)
    {
        free(out);
        return false;
    }
    vm_client_finish_wt_packet(out, outPos, objectCount);
    memcpy(src, out, outPos);
    *packetLen = outPos;
    free(out);
    printf("[info][network] cbe_login_bag_order objects=%u "
           "evidence=30/21-then-7/7-then-7/11\n",
           objectCount);
    return true;
}

static void vm_client_prepare_cbe_downlink(u8 **packet, u32 *packetLen)
{
    u32 offset = 5;
    vm_client_response_object object;
    u8 bag17 = 0;
    u8 bag30 = 0;
    u8 has47 = 0;
    u8 kinds[12];
    u8 subtypes[12];
    u8 n = 0;

    if (packet == NULL || packetLen == NULL)
        return;
    (void)vm_client_hangup_rewrite_panel_to_dismiss(packet, packetLen);
    vm_client_hangup_settle_note_downlink(*packet, *packetLen);
    (void)vm_client_hangup_strip_settle_actor14(packet, packetLen);
    (void)vm_client_cbe_inject_hangup_combatinfo(packet, packetLen);
    (void)vm_client_cbe_patch_actorinfo_packet(packet, packetLen);
    (void)vm_client_strip_cbe_unsupported_objects(packet, packetLen);
    (void)vm_client_cbe_clamp_backpack_packet(packet, packetLen);
    (void)vm_client_cbe_login_bag_order(packet, packetLen);
    /*
     * Bag-defer disabled: peeling login 30/21 into a lone follow-up caused
     *「解包错误」and a hard stall on the second role login (client_out
     * 2026-10-08 deliver 8831 then freeze).  Keep 30/21 in the group-type1 WT
     * after clamp; equip peel + type=2 coerce remain.
     */
    if (g_vmClientCbeBagDefer.armed)
        vm_client_cbe_bag_defer_clear();
    vm_client_hangup_note_downlink_skills(*packet, *packetLen);

    /* Narrow forensics: confirm whether login 30/21 reached CBE with 17/1. */
    if (*packet == NULL || *packetLen < 5 || (*packet)[0] != 'W' ||
        (*packet)[1] != 'T')
        return;
    offset = 5;
    while (offset + 6u <= *packetLen &&
           vm_client_next_response_object(*packet, *packetLen, &offset,
                                           &object))
    {
        if (n < 12)
        {
            kinds[n] = object.kind;
            subtypes[n] = object.subtype;
            ++n;
        }
        if (object.major == 1 && object.kind == 4 && object.subtype == 7)
            has47 = 1;
        if (object.major == 1 && object.kind == 17 && object.subtype == 1)
            ++bag17;
        if (object.major == 1 && object.kind == 30 && object.subtype == 21)
            ++bag30;
    }
    if (bag17 != 0 || bag30 != 0 || has47 != 0)
    {
        char list[96];
        u32 listPos = 0;
        u8 i;

        list[0] = '\0';
        for (i = 0; i < n && listPos + 12 < sizeof(list); ++i)
        {
            int wrote = snprintf(list + listPos, sizeof(list) - listPos,
                                 "%s%u/%u", i ? "," : "", kinds[i], subtypes[i]);
            if (wrote <= 0)
                break;
            listPos += (u32)wrote;
        }
        if (bag17 != 0 || bag30 != 0)
            printf("[info][network] cbe_backpack_packet objects=%u has_17_1=%u "
                   "has_30_21=%u list=%s resp=%u\n",
                   n, bag17, bag30, list, *packetLen);
        if (has47 != 0)
            printf("[info][network] cbe_hangup_47_packet objects=%u list=%s "
                   "resp=%u evidence=settle-downlink\n",
                   n, list, *packetLen);
    }
}

/* Peel objects that must not share the item-op queue_data event that clears
 * the wait flag:
 *   7/1 use   → 17/1 capacity list, 26/1 warehouse dialog
 *   7/4 discard → 17/1+7/42 list, 7/11 count, 10/26 money refund
 * Delivered as a second event=7. */
static bool vm_client_wt_object_is_item_use_followup(const vm_client_wt_object *object,
                                                      bool haveItemDiscard)
{
    if (object == NULL || object->major != 1)
        return false;
    if (object->kind == 17 && object->subtype == 1)
        return true;
    if (object->kind == 26 && object->subtype == 1)
        return true;
    if (haveItemDiscard)
    {
        if (object->kind == 7 && object->subtype == 42)
            return true;
        if (object->kind == 7 && object->subtype == 11)
            return true;
        if (object->kind == 0x0a && object->subtype == 0x1a)
            return true;
    }
    return false;
}

static bool vm_client_extract_item_followup(u8 *response, u32 *responseLen,
                                            u8 *followup, u32 followupCap,
                                            u32 *followupLen)
{
    u32 offset = 4;
    u32 primaryPos = 5;
    u32 followPos = 5;
    u8 primaryCount = 0;
    u8 followCount = 0;
    bool haveItemUse = false;
    bool haveItemDiscard = false;
    vm_client_wt_object object;

    if (followupLen != NULL)
        *followupLen = 0;
    if (response == NULL || responseLen == NULL || *responseLen < 10 ||
        response[0] != 'W' || response[1] != 'T' ||
        followup == NULL || followupCap < 5)
        return false;

    while (offset + 5 <= *responseLen &&
           vm_client_next_wt_object(response, *responseLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 7 && object.subtype == 1)
            haveItemUse = true;
        if (object.major == 1 && object.kind == 7 && object.subtype == 4)
            haveItemDiscard = true;
        if (vm_client_wt_object_is_item_use_followup(&object, haveItemDiscard))
            ++followCount;
        else
            ++primaryCount;
    }
    if (offset != *responseLen ||
        (!haveItemUse && !haveItemDiscard) ||
        followCount == 0 || primaryCount == 0)
        return false;

    offset = 4;
    primaryPos = 5;
    followPos = 5;
    primaryCount = 0;
    followCount = 0;
    while (offset + 5 <= *responseLen)
    {
        u32 start = offset;
        u32 objectLen;
        if (!vm_client_next_wt_object(response, *responseLen, &offset, &object))
            return false;
        objectLen = offset - start;
        if (vm_client_wt_object_is_item_use_followup(&object, haveItemDiscard))
        {
            if (followPos + objectLen > followupCap || followCount == 0xff)
                return false;
            memcpy(followup + followPos, response + start, objectLen);
            followPos += objectLen;
            ++followCount;
        }
        else
        {
            memmove(response + primaryPos, response + start, objectLen);
            primaryPos += objectLen;
            ++primaryCount;
        }
    }
    vm_client_finish_wt_packet(response, primaryPos, primaryCount);
    vm_client_finish_wt_packet(followup, followPos, followCount);
    *responseLen = primaryPos;
    if (followupLen != NULL)
        *followupLen = followPos;
    return true;
}

/*
 * Peel login 7/7 (wear bootstrap) out of the compound that also carries
 * 30/21.  CBE 装备栏 is filled by sub_D04 type=2 → +104 after the bag grid
 * seeds; delivering wear in a second event=7 avoids depending on parser
 * state immediately after an 8KB+ HandleItemGrid apply.
 * evidence: docs/re/2026-07-21-equipment-login-restore.md
 */
static bool vm_client_extract_login_equip_followup(u8 *response, u32 *responseLen,
                                                   u8 *followup, u32 followupCap,
                                                   u32 *followupLen)
{
    u32 offset = 5;
    u32 primaryPos = 5;
    u32 followPos = 5;
    u8 primaryCount = 0;
    u8 followCount = 0;
    bool have3021 = false;
    bool have77 = false;
    vm_client_response_object object;

    if (followupLen != NULL)
        *followupLen = 0;
    if (response == NULL || responseLen == NULL || *responseLen < 10 ||
        response[0] != 'W' || response[1] != 'T' || followup == NULL ||
        followupCap < 5)
        return false;

    while (offset + 6u <= *responseLen &&
           vm_client_next_response_object(response, *responseLen, &offset,
                                           &object))
    {
        if (object.major == 1 && object.kind == 30 && object.subtype == 21)
            have3021 = true;
        if (object.major == 1 && object.kind == 7 && object.subtype == 7)
        {
            have77 = true;
            ++followCount;
        }
        else
            ++primaryCount;
    }
    if (offset != *responseLen || !have3021 || !have77 || followCount == 0 ||
        primaryCount == 0)
        return false;

    offset = 5;
    primaryPos = 5;
    followPos = 5;
    primaryCount = 0;
    followCount = 0;
    while (offset + 6u <= *responseLen)
    {
        u32 start = offset;
        u32 objectLen;

        if (!vm_client_next_response_object(response, *responseLen, &offset,
                                             &object))
            return false;
        objectLen = offset - start;
        if (object.major == 1 && object.kind == 7 && object.subtype == 7)
        {
            if (followPos + objectLen > followupCap || followCount == 0xff)
                return false;
            memcpy(followup + followPos, response + start, objectLen);
            followPos += objectLen;
            ++followCount;
        }
        else
        {
            memmove(response + primaryPos, response + start, objectLen);
            primaryPos += objectLen;
            ++primaryCount;
        }
    }
    vm_client_finish_wt_packet(response, primaryPos, primaryCount);
    vm_client_finish_wt_packet(followup, followPos, followCount);
    *responseLen = primaryPos;
    if (followupLen != NULL)
        *followupLen = followPos;
    printf("[info][network] cbe_login_equip_peel primary=%u followup=%u "
           "evidence=30/21+7/7-second-event7\n",
           primaryPos, followPos);
    return true;
}

/*
 * CBE event-7 business path: event_packet_init(..., 10, 19) rejects packets
 * with >10 response objects (error 3 -> unpack-error popup).  Upgraded live
 * servers may emit wider compound WT responses; split before the guest parser.
 * evidence: docs/re/2026-07-20-new-role-login-object-cap.md
 */
enum { VM_CLIENT_WT_EVENT7_OBJECT_CAP = 10u };

static u32 vm_client_count_parsed_response_objects(const u8 *packet, u32 packetLen)
{
    /* Downlink WT: objects start after objectCount at index 4. */
    u32 offset = 5;
    u32 count = 0;
    vm_client_response_object object;

    if (packet == NULL || packetLen < 5 || packet[0] != 'W' || packet[1] != 'T')
        return 0;
    while (offset + 6u <= packetLen &&
           vm_client_next_response_object(packet, packetLen, &offset, &object))
    {
        ++count;
    }
    return (offset == packetLen) ? count : 0;
}

static bool vm_client_deliver_wt_single(
    u8 *packet,
    u32 packetLen,
    u32 eventType,
    u32 callback,
    u32 context,
    u32 connectId,
    bool attachRemoteObservation,
    bool updateNetMockScratch)
{
    u32 responsePtr;

    if (packet == NULL || packetLen < 5)
        return false;
    responsePtr = vm_net_mock_sync_buffer_to_vm(packet, packetLen);
    if (responsePtr == 0)
        return false;
    if (updateNetMockScratch && packetLen <= sizeof(g_netMockResponse))
    {
        memcpy(g_netMockResponse, packet, packetLen);
        g_netMockResponseLen = packetLen;
        g_netMockResponseOffset = 0;
        vm_client_note_title_role_select(packet, packetLen);
    }
    scheduler_queue_net_event(eventType, responsePtr, packetLen, packetLen,
                              callback, context);
    if (attachRemoteObservation && eventType == 7)
    {
        vm_net_remote_observation observation;

        vm_client_capture_remote_observation(packet, packetLen, &observation);
        if ((observation.hasSceneTarget ||
             observation.sceneCompleteAfterCallback ||
             observation.updateComplete) &&
            !scheduler_attach_net_remote_observation(
                eventType, responsePtr, callback, context, &observation))
        {
            printf("[warn][screen] remote_observation_attach_failed "
                   "connect=%u event=%u resp=%u action=no-early-global-mutation\n",
                   connectId, eventType, packetLen);
        }
    }
    return true;
}

static bool vm_client_deliver_wt_chunk(
    u8 *chunk,
    u32 *chunkLenInOut,
    u8 chunkCount,
    u32 eventType,
    u32 callback,
    u32 context,
    u32 connectId,
    bool attachRemoteObservation,
    bool updateNetMockScratch)
{
    u32 chunkLen;

    if (chunk == NULL || chunkLenInOut == NULL || *chunkLenInOut < 5 ||
        chunkCount == 0)
        return false;
    chunkLen = *chunkLenInOut;
    vm_client_finish_wt_packet(chunk, chunkLen, chunkCount);
    *chunkLenInOut = chunkLen;
    return vm_client_deliver_wt_single(chunk, chunkLen, eventType, callback,
                                       context, connectId,
                                       attachRemoteObservation,
                                       updateNetMockScratch);
}

static u32 vm_client_deliver_wt_response(
    u8 *packet,
    u32 packetLen,
    u32 eventType,
    u32 callback,
    u32 context,
    u32 connectId,
    bool attachRemoteObservation,
    bool updateNetMockScratch)
{
    u32 objectTotal;
    /* Downlink WT: objects start after objectCount at index 4. */
    u32 offset = 5;
    u32 chunkIndex = 0;
    u32 eventsDelivered = 0;
    u8 chunkCount = 0;
    u32 chunkPos = 5;
    u8 *chunk = NULL;
    vm_client_response_object object;

    if (packet == NULL || packetLen < 5)
        return 0;
    /*
     * After 4/7 hangup shell, mmGame still waits on swallowed 25/5.
     * Empty WT completes that wait → rand_range DIV0 (stack 0x01012E4D).
     * 4/12 empty ACK happens before OnSettleShell.
     */
    if (eventType == 7 && g_vmClientHangupOnSettleShell != 0 &&
        vm_client_packet_is_empty_wt(packet, packetLen))
    {
        printf("[info][network] cbe_hangup_drop_empty_wt_ack connect=%u "
               "len=%u evidence=settle-shell-empty-wt-completes-25/5\n",
               connectId, packetLen);
        return 0;
    }

    objectTotal = vm_client_count_parsed_response_objects(packet, packetLen);
    if (eventType != 7 || objectTotal == 0 ||
        objectTotal <= VM_CLIENT_WT_EVENT7_OBJECT_CAP)
    {
        if (!vm_client_deliver_wt_single(packet, packetLen, eventType, callback,
                                       context, connectId,
                                       attachRemoteObservation,
                                       updateNetMockScratch))
            return 0;
        return 1;
    }

    chunk = (u8 *)malloc(packetLen);
    if (chunk == NULL)
        return 0;
    chunk[0] = 'W';
    chunk[1] = 'T';
    chunk[2] = 0;
    chunk[3] = 0;
    chunk[4] = 0;

    while (offset + 6u <= packetLen &&
           vm_client_next_response_object(packet, packetLen, &offset, &object))
    {
        u32 objectStart = offset - ((u32)object.payloadLen + 6u);
        u32 objectLen = (u32)object.payloadLen + 6u;

        if (chunkCount >= VM_CLIENT_WT_EVENT7_OBJECT_CAP)
        {
            if (!vm_client_deliver_wt_chunk(chunk, &chunkPos, chunkCount,
                                            eventType, callback, context,
                                            connectId,
                                            attachRemoteObservation &&
                                                chunkIndex == 0,
                                            updateNetMockScratch &&
                                                chunkIndex == 0))
            {
                free(chunk);
                return eventsDelivered;
            }
            ++eventsDelivered;
            ++chunkIndex;
            chunkPos = 5;
            chunkCount = 0;
        }
        if (chunkPos + objectLen > packetLen)
        {
            free(chunk);
            return eventsDelivered;
        }
        memcpy(chunk + chunkPos, packet + objectStart, objectLen);
        chunkPos += objectLen;
        ++chunkCount;
    }
    if (offset != packetLen || chunkCount == 0)
    {
        free(chunk);
        if (eventsDelivered == 0)
        {
            if (vm_client_deliver_wt_single(packet, packetLen, eventType,
                                            callback, context, connectId,
                                            attachRemoteObservation,
                                            updateNetMockScratch))
                return 1;
        }
        return eventsDelivered;
    }
    if (vm_client_deliver_wt_chunk(chunk, &chunkPos, chunkCount, eventType,
                                    callback, context, connectId,
                                    attachRemoteObservation &&
                                        chunkIndex == 0,
                                    updateNetMockScratch && chunkIndex == 0))
    {
        ++eventsDelivered;
    }
    free(chunk);
    if (eventsDelivered > 1)
    {
        printf("[info][network] cbe_object_cap_split connect=%u event=%u "
               "objects=%u events=%u resp=%u "
               "evidence=JianghuOL.CBE:0x0103467A\n",
               connectId, eventType, objectTotal, eventsDelivered, packetLen);
    }
    return eventsDelivered;
}

static void vm_client_write_le32(u8 *dst, u32 value)
{
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
    dst[2] = (u8)(value >> 16);
    dst[3] = (u8)(value >> 24);
}

static u32 vm_client_read_le32(const u8 *src)
{
    return (u32)src[0] | ((u32)src[1] << 8) |
           ((u32)src[2] << 16) | ((u32)src[3] << 24);
}

static void vm_client_encode_header(u8 *header, u32 flags, u32 bodyLen, u32 metaLen)
{
    memcpy(header, "CBMS", 4);
    vm_client_write_le32(header + 4, 1);
    vm_client_write_le32(header + 8, flags);
    vm_client_write_le32(header + 12, bodyLen);
    vm_client_write_le32(header + 16, metaLen);
}

static bool vm_client_socket_init(void)
{
#ifdef _WIN32
    static int initialized = -1;
    WSADATA wsaData;

    if (initialized >= 0)
        return initialized != 0;
    initialized = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0 ? 1 : 0;
    return initialized != 0;
#else
    return true;
#endif
}

static void vm_client_close_socket(vm_client_socket sock)
{
    if (sock != VM_CLIENT_INVALID_SOCKET)
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
}

static void vm_client_enable_tcp_nodelay(vm_client_socket sock)
{
    int one = 1;
#ifdef _WIN32
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
#else
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
}

static void vm_client_enable_keepalive(vm_client_socket sock)
{
    int one = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&one, sizeof(one));
#else
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#endif
}

static int vm_client_set_blocking(vm_client_socket sock, int blocking)
{
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0 ? 1 : 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return 0;
    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    return fcntl(sock, F_SETFL, flags) == 0 ? 1 : 0;
#endif
}

static int vm_client_connect_deadline(vm_client_socket sock,
                                      const struct sockaddr *addr,
                                      socklen_t addrLen,
                                      u32 timeoutMs)
{
    int rc;
    fd_set writeSet;
    fd_set errorSet;
    struct timeval tv;
    int soError = 0;
    socklen_t soLen = (socklen_t)sizeof(soError);

    /*
     * WT data uses blocking connect (timeoutMs == 0).  A previous non-blocking
     * path could lose a race on Windows where accept() already completed on the
     * server but select()/SO_ERROR made the client abandon the socket before
     * sending CBMS — server log then shows accept+dispatch with no request.
     */
    if (timeoutMs == 0)
    {
        if (!vm_client_set_blocking(sock, 1))
            return 0;
        return connect(sock, addr, addrLen) == 0;
    }

    if (!vm_client_set_blocking(sock, 0))
        return 0;
    rc = connect(sock, addr, addrLen);
    if (rc == 0)
    {
        (void)vm_client_set_blocking(sock, 1);
        return 1;
    }
#ifdef _WIN32
    {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
        {
            (void)vm_client_set_blocking(sock, 1);
            return 0;
        }
    }
#else
    if (errno != EINPROGRESS && errno != EWOULDBLOCK)
    {
        (void)vm_client_set_blocking(sock, 1);
        return 0;
    }
#endif

    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    FD_SET(sock, &writeSet);
    FD_SET(sock, &errorSet);
    tv.tv_sec = (long)(timeoutMs / 1000u);
    tv.tv_usec = (long)((timeoutMs % 1000u) * 1000u);
#ifdef _WIN32
    rc = select(0, NULL, &writeSet, &errorSet, &tv);
#else
    rc = select((int)sock + 1, NULL, &writeSet, &errorSet, &tv);
#endif
    if (rc == 0)
    {
        (void)vm_client_set_blocking(sock, 1);
        return 0;
    }
    if (rc < 0)
    {
        (void)vm_client_set_blocking(sock, 1);
        return 0;
    }
    if (FD_ISSET(sock, &errorSet))
    {
        (void)getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&soError, &soLen);
        (void)vm_client_set_blocking(sock, 1);
        return 0;
    }
    if (!FD_ISSET(sock, &writeSet))
    {
        (void)vm_client_set_blocking(sock, 1);
        return 0;
    }
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&soError, &soLen) != 0 ||
        soError != 0)
    {
        (void)vm_client_set_blocking(sock, 1);
        return 0;
    }
    (void)vm_client_set_blocking(sock, 1);
    return 1;
}

static bool vm_client_send_all(vm_client_socket sock, const u8 *data, u32 len)
{
    u32 sent = 0;
    while (sent < len)
    {
        int rc = send(sock, (const char *)(data + sent), (int)(len - sent), 0);
        if (rc <= 0)
            return false;
        sent += (u32)rc;
    }
    return true;
}

static bool vm_client_recv_all(vm_client_socket sock, u8 *data, u32 len)
{
    u32 received = 0;
    while (received < len)
    {
        int rc = recv(sock, (char *)(data + received), (int)(len - received), 0);
        if (rc <= 0)
            return false;
        received += (u32)rc;
    }
    return true;
}

static vm_client_socket vm_client_connect_ex(u32 connectTimeoutMs)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
#ifndef _WIN32
    struct timeval timeout;
#endif
    char port[16];
    vm_client_socket sock = VM_CLIENT_INVALID_SOCKET;
    unsigned ipv4A = 0;
    unsigned ipv4B = 0;
    unsigned ipv4C = 0;
    unsigned ipv4D = 0;
    char ipv4Tail = 0;
    int hostIsIpv4 = 0;

    if (!vm_client_socket_init())
        return VM_CLIENT_INVALID_SOCKET;
    memset(&hints, 0, sizeof(hints));
    /* Prefer IPv4 for dotted literals so dual-stack does not try a dead AAAA. */
    hostIsIpv4 = (sscanf(g_mockServiceHost, "%u.%u.%u.%u%c",
                         &ipv4A, &ipv4B, &ipv4C, &ipv4D, &ipv4Tail) == 4 &&
                  ipv4A <= 255u && ipv4B <= 255u && ipv4C <= 255u && ipv4D <= 255u);
    hints.ai_family = hostIsIpv4 ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof(port), "%u", g_mockServicePort);
    if (getaddrinfo(g_mockServiceHost, port, &hints, &addresses) != 0)
        return VM_CLIENT_INVALID_SOCKET;

#ifdef _WIN32
    {
        int timeout = VM_CLIENT_SOCKET_TIMEOUT_MS;
#else
    timeout.tv_sec = VM_CLIENT_SOCKET_TIMEOUT_MS / 1000;
    timeout.tv_usec = (VM_CLIENT_SOCKET_TIMEOUT_MS % 1000) * 1000;
#endif
    for (address = addresses; address != NULL; address = address->ai_next)
    {
        sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (sock == VM_CLIENT_INVALID_SOCKET)
            continue;
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#else
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
        if (vm_client_connect_deadline(sock, address->ai_addr, address->ai_addrlen,
                                       connectTimeoutMs))
        {
            vm_client_enable_tcp_nodelay(sock);
            vm_client_enable_keepalive(sock);
            break;
        }
        vm_client_close_socket(sock);
        sock = VM_CLIENT_INVALID_SOCKET;
    }
#ifdef _WIN32
    }
#endif
    freeaddrinfo(addresses);
    return sock;
}

static vm_client_socket vm_client_connect(void)
{
    /* Bounded connect for WT data / disconnect — avoid OS SYN blackhole stalls. */
    return vm_client_connect_ex(VM_CLIENT_DATA_CONNECT_TIMEOUT_MS);
}

static bool vm_client_read_response(vm_client_socket sock, u8 *response, u32 responseCap,
                                    u32 *responseLen, u32 *eventType,
                                    bool *closeAfterData,
                                    u8 *followup, u32 followupCap, u32 *followupLen);

typedef struct
{
    pthread_mutex_t mutex;
    vm_client_socket sock;
    u32 exchanges;
    u32 reconnects;
    const char *name;
} vm_client_live_session;

static vm_client_live_session g_vmClientDataSession = {
    PTHREAD_MUTEX_INITIALIZER, VM_CLIENT_INVALID_SOCKET, 0, 0, "data"};
static vm_client_live_session g_vmClientPollSession = {
    PTHREAD_MUTEX_INITIALIZER, VM_CLIENT_INVALID_SOCKET, 0, 0, "poll"};

static void vm_client_live_session_close_locked(vm_client_live_session *session)
{
    if (session == NULL)
        return;
    if (session->sock != VM_CLIENT_INVALID_SOCKET)
    {
        vm_client_close_socket(session->sock);
        session->sock = VM_CLIENT_INVALID_SOCKET;
    }
}

static void vm_client_live_session_close(vm_client_live_session *session)
{
    if (session == NULL)
        return;
    pthread_mutex_lock(&session->mutex);
    vm_client_live_session_close_locked(session);
    pthread_mutex_unlock(&session->mutex);
}

static bool vm_client_live_session_exchange(
    vm_client_live_session *session,
    u32 connectTimeoutMs,
    u32 attempts,
    const u8 *header,
    const u8 *meta,
    u32 metaLen,
    const u8 *request,
    u32 requestLen,
    u8 *response,
    u32 responseCap,
    u32 *responseLen,
    u32 *eventType,
    bool *closeAfterData,
    u8 *followup,
    u32 followupCap,
    u32 *followupLen,
    const char **failStageOut)
{
    u32 attempt;
    const char *failStage = "init";
    bool reused = false;

    if (session == NULL || header == NULL || meta == NULL || metaLen == 0 ||
        response == NULL)
        return false;

    pthread_mutex_lock(&session->mutex);
    for (attempt = 1; attempt <= attempts; ++attempt)
    {
        bool ok;

        failStage = "connect";
        reused = session->sock != VM_CLIENT_INVALID_SOCKET;
        if (!reused)
        {
            session->sock = vm_client_connect_ex(connectTimeoutMs);
            if (session->sock == VM_CLIENT_INVALID_SOCKET)
            {
                printf("[warn][network] %s_request attempt=%u/%u stage=%s "
                       "target=%s:%u\n",
                       session->name, attempt, attempts, failStage,
                       g_mockServiceHost, g_mockServicePort);
                if (attempt < attempts)
                    SDL_Delay((u32)VM_CLIENT_DATA_RETRY_BASE_MS * attempt);
                continue;
            }
            if (session->exchanges > 0)
                ++session->reconnects;
        }

        failStage = "send";
        ok = vm_client_send_all(session->sock, header, VM_CLIENT_FRAME_SIZE) &&
             vm_client_send_all(session->sock, meta, metaLen) &&
             (requestLen == 0 ||
              vm_client_send_all(session->sock, request, requestLen));
        if (!ok)
        {
            printf("[warn][network] %s_request attempt=%u/%u stage=%s reused=%u "
                   "target=%s:%u\n",
                   session->name, attempt, attempts, failStage,
                   reused ? 1u : 0u, g_mockServiceHost, g_mockServicePort);
            vm_client_live_session_close_locked(session);
            if (attempt < attempts)
                SDL_Delay((u32)VM_CLIENT_DATA_RETRY_BASE_MS * attempt);
            continue;
        }

        failStage = "recv";
        ok = vm_client_read_response(session->sock, response, responseCap,
                                     responseLen, eventType, closeAfterData,
                                     followup, followupCap, followupLen);
        if (!ok)
        {
            printf("[warn][network] %s_request attempt=%u/%u stage=%s reused=%u "
                   "target=%s:%u\n",
                   session->name, attempt, attempts, failStage,
                   reused ? 1u : 0u, g_mockServiceHost, g_mockServicePort);
            vm_client_live_session_close_locked(session);
            if (attempt < attempts)
                SDL_Delay((u32)VM_CLIENT_DATA_RETRY_BASE_MS * attempt);
            continue;
        }

        ++session->exchanges;
        if (reused && ((session->exchanges & 31u) == 0u))
        {
            printf("[info][network] %s_session reused exchanges=%u reconnects=%u "
                   "target=%s:%u\n",
                   session->name, session->exchanges, session->reconnects,
                   g_mockServiceHost, g_mockServicePort);
        }
        else if (!reused && attempt > 1)
        {
            printf("[info][network] %s_request recovered attempt=%u resp=%u "
                   "target=%s:%u\n",
                   session->name, attempt,
                   responseLen ? *responseLen : 0,
                   g_mockServiceHost, g_mockServicePort);
        }
        if (failStageOut != NULL)
            *failStageOut = NULL;
        /*
         * Server CLOSE_AFTER_DATA (and poll return-2) ends the TCP session.
         * Drop the live socket immediately so the next exchange does not reuse
         * a half-closed fd and block on SO_RCVTIMEO.
         */
        if (closeAfterData != NULL && *closeAfterData)
            vm_client_live_session_close_locked(session);
        pthread_mutex_unlock(&session->mutex);
        return true;
    }

    if (failStageOut != NULL)
        *failStageOut = failStage;
    printf("[warn][network] %s_request exhausted attempts=%u last_stage=%s "
           "target=%s:%u\n",
           session->name, attempts, failStage, g_mockServiceHost,
           g_mockServicePort);
    pthread_mutex_unlock(&session->mutex);
    return false;
}

static u32 vm_client_encode_meta(u8 *meta, u32 cap)
{
    char gbkName[32];
    u32 nameLen = 0;

    if (meta == NULL || cap < 4 || g_mockServiceClientId == 0)
        return 0;
    vm_client_write_le32(meta, g_mockServiceClientId);
    memset(gbkName, 0, sizeof(gbkName));
    if (g_mockPreferredTitleServerUtf8[0] != 0)
    {
        utf8_to_gbk((u8 *)g_mockPreferredTitleServerUtf8, (u8 *)gbkName,
                    sizeof(gbkName));
        nameLen = (u32)strlen(gbkName);
        if (nameLen > 31)
            nameLen = 31;
    }
    if (nameLen == 0 || cap < 5 + nameLen)
        return 4;
    meta[4] = (u8)nameLen;
    memcpy(meta + 5, gbkName, nameLen);
    return 5 + nameLen;
}

static bool vm_client_read_response(vm_client_socket sock, u8 *response, u32 responseCap,
                                    u32 *responseLen, u32 *eventType,
                                    bool *closeAfterData,
                                    u8 *followup, u32 followupCap, u32 *followupLen)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u32 flags;
    u32 len;
    u32 event;
    if (followupLen != NULL)
        *followupLen = 0;
    if (!vm_client_recv_all(sock, header, sizeof(header)) ||
        memcmp(header, "CBMR", 4) != 0 || vm_client_read_le32(header + 4) != 1)
        return false;
    flags = vm_client_read_le32(header + 8);
    len = vm_client_read_le32(header + 12);
    event = vm_client_read_le32(header + 16);
    if (len > responseCap || (len != 0 && !vm_client_recv_all(sock, response, len)))
        return false;
    if (responseLen != NULL)
        *responseLen = len;
    if (eventType != NULL)
        *eventType = event == 0 ? 7 : event;
    if (closeAfterData != NULL)
        *closeAfterData = (flags & VM_CLIENT_RESPONSE_FLAG_CLOSE_AFTER_DATA) != 0;
    if ((flags & VM_CLIENT_RESPONSE_FLAG_HAS_FOLLOWUP) != 0 &&
        followup != NULL && followupCap != 0 && followupLen != NULL)
    {
        u8 followHeader[VM_CLIENT_FRAME_SIZE];
        u32 followBodyLen;
        if (!vm_client_recv_all(sock, followHeader, sizeof(followHeader)) ||
            memcmp(followHeader, "CBMR", 4) != 0 ||
            vm_client_read_le32(followHeader + 4) != 1)
            return false;
        followBodyLen = vm_client_read_le32(followHeader + 12);
        if (followBodyLen > followupCap ||
            (followBodyLen != 0 && !vm_client_recv_all(sock, followup, followBodyLen)))
            return false;
        *followupLen = followBodyLen;
    }
    return true;
}

/*
 * Host-side codeVersion override (does not patch CBE).  Configure via:
 *   1) env CBE_CLIENT_CODE_VERSION
 *   2) ./client_code_version.txt  (Android: /sdcard/JHOnline/client_code_version.txt)
 *   3) ./JHOnlineData/client_code_version.txt
 * Value 0 / missing = leave the CBE-built field unchanged.
 */
static u32 vm_client_configured_code_version(void)
{
    const char *spec = getenv("CBE_CLIENT_CODE_VERSION");
    FILE *fp = NULL;
    unsigned long parsed = 0;
    char *end = NULL;

    if (spec != NULL && spec[0] != 0)
    {
        parsed = strtoul(spec, &end, 0);
        if (end != spec)
            return (u32)parsed;
    }
    fp = fopen("client_code_version.txt", "rb");
    if (fp == NULL)
        fp = fopen("JHOnlineData/client_code_version.txt", "rb");
    if (fp == NULL)
        return 0;
    if (fscanf(fp, "%lu", &parsed) != 1)
        parsed = 0;
    fclose(fp);
    return (u32)parsed;
}

static bool vm_client_request_has_code_version(const u8 *request, u32 requestLen)
{
    static const char name[] = "codeVersion";
    const u8 nameLen = (u8)(sizeof(name) - 1u);

    if (request == NULL || requestLen < (u32)nameLen + 4u)
        return false;
    for (u32 i = 0; i + nameLen + 1u < requestLen; ++i)
    {
        if (request[i] == nameLen &&
            memcmp(request + i + 1, name, nameLen) == 0)
            return true;
    }
    return false;
}

static bool vm_client_patch_code_version_field(u8 *request, u32 requestLen,
                                               u32 version)
{
    static const char name[] = "codeVersion";
    const u8 nameLen = (u8)(sizeof(name) - 1u);
    u16 v16 = (u16)(version > 0xffffu ? 0xffffu : version);

    if (request == NULL || requestLen < (u32)nameLen + 6u)
        return false;
    for (u32 i = 0; i + nameLen + 6u <= requestLen; ++i)
    {
        u32 p;

        if (request[i] != nameLen ||
            memcmp(request + i + 1, name, nameLen) != 0)
            continue;
        p = i + 1u + nameLen;
        /* name + be16(4) + {0x00,0x02,hi,lo} */
        if (p + 6u <= requestLen && request[p] == 0 && request[p + 1] == 4 &&
            request[p + 2] == 0 && request[p + 3] == 2)
        {
            request[p + 4] = (u8)(v16 >> 8);
            request[p + 5] = (u8)v16;
            return true;
        }
        /* name + optional NUL + same / alternate encodings */
        if (p < requestLen && request[p] == 0)
        {
            u32 q = p + 1u;
            if (q + 6u <= requestLen && request[q] == 0 && request[q + 1] == 4 &&
                request[q + 2] == 0 && request[q + 3] == 2)
            {
                request[q + 4] = (u8)(v16 >> 8);
                request[q + 5] = (u8)v16;
                return true;
            }
            if (q + 5u <= requestLen && request[q] == 4 && request[q + 1] == 0 &&
                request[q + 2] == 2)
            {
                request[q + 3] = (u8)(v16 >> 8);
                request[q + 4] = (u8)v16;
                return true;
            }
            if (q + 3u <= requestLen && request[q] == 2)
            {
                request[q + 1] = (u8)(v16 >> 8);
                request[q + 2] = (u8)v16;
                return true;
            }
            if (q + 8u <= requestLen && request[q] == 0 && request[q + 1] == 6 &&
                request[q + 2] == 0 && request[q + 3] == 4)
            {
                request[q + 4] = (u8)(version >> 24);
                request[q + 5] = (u8)(version >> 16);
                request[q + 6] = (u8)(version >> 8);
                request[q + 7] = (u8)version;
                return true;
            }
        }
        if (p + 5u <= requestLen && request[p] == 4 && request[p + 1] == 0 &&
            request[p + 2] == 2)
        {
            request[p + 3] = (u8)(v16 >> 8);
            request[p + 4] = (u8)v16;
            return true;
        }
        if (p + 3u <= requestLen && request[p] == 2)
        {
            request[p + 1] = (u8)(v16 >> 8);
            request[p + 2] = (u8)v16;
            return true;
        }
        if (p + 8u <= requestLen && request[p] == 0 && request[p + 1] == 6 &&
            request[p + 2] == 0 && request[p + 3] == 4)
        {
            request[p + 4] = (u8)(version >> 24);
            request[p + 5] = (u8)(version >> 16);
            request[p + 6] = (u8)(version >> 8);
            request[p + 7] = (u8)version;
            return true;
        }
    }
    return false;
}

static bool vm_client_remote_request(const u8 *request, u32 requestLen,
                                     u8 *response, u32 responseCap,
                                     u32 *responseLen, u32 *eventType,
                                     bool *closeAfterData,
                                     u8 *followup, u32 followupCap,
                                     u32 *followupLen,
                                     u32 attempts)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 meta[48];
    u32 metaLen = vm_client_encode_meta(meta, sizeof(meta));
    const char *failStage = NULL;
    u8 *overrideBuf = NULL;
    const u8 *sendRequest = request;
    u32 overrideVersion = 0;
    bool ok;

    if (responseLen != NULL)
        *responseLen = 0;
    if (eventType != NULL)
        *eventType = 7;
    if (closeAfterData != NULL)
        *closeAfterData = false;
    if (followupLen != NULL)
        *followupLen = 0;
    if (request == NULL || requestLen == 0 || response == NULL || metaLen == 0)
    {
        printf("[warn][network] data_request rejected meta_len=%u req_len=%u "
               "client=%08x\n",
               metaLen, requestLen, g_mockServiceClientId);
        return false;
    }
    if (attempts == 0)
        attempts = 1;

    overrideVersion = vm_client_configured_code_version();
    if (overrideVersion != 0 &&
        vm_client_request_has_code_version(request, requestLen))
    {
        overrideBuf = (u8 *)malloc(requestLen);
        if (overrideBuf != NULL)
        {
            memcpy(overrideBuf, request, requestLen);
            if (vm_client_patch_code_version_field(overrideBuf, requestLen,
                                                   overrideVersion))
            {
                sendRequest = overrideBuf;
                printf("[info][network] client_code_version_override value=%u "
                       "source=host-config evidence=network-client.c\n",
                       overrideVersion);
            }
        }
    }

    vm_client_encode_header(header, 0, requestLen + metaLen, metaLen);
    ok = vm_client_live_session_exchange(
            &g_vmClientDataSession,
            VM_CLIENT_DATA_CONNECT_TIMEOUT_MS,
            attempts,
            header, meta, metaLen, sendRequest, requestLen,
            response, responseCap, responseLen, eventType, closeAfterData,
            followup, followupCap, followupLen, &failStage);
    free(overrideBuf);
    if (!ok)
    {
        (void)failStage;
        return false;
    }
    if ((followupLen == NULL || *followupLen == 0) &&
        eventType != NULL && *eventType == 7 && responseLen != NULL &&
        followup != NULL && followupLen != NULL)
    {
        (void)vm_client_extract_item_followup(response, responseLen,
                                              followup, followupCap,
                                              followupLen);
    }
    /*
     * Login equip peel disabled (2026-10-08): splitting 7/7 type=2 out of the
     * group-type1 WT still left「解包错误」/ second-login freeze after bag-defer
     * was already off (client_out connect=3 stops right after followup 114).
     * Keep 7/7 in the same packet as 30/21; type=2 coerce in rewrite remains.
     */
    return true;
}

static bool vm_client_remote_poll(u8 *response, u32 responseCap,
                                  u32 *responseLen, u32 *eventType,
                                  u32 attempts)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 meta[48];
    u32 metaLen = vm_client_encode_meta(meta, sizeof(meta));
    bool ok;
    bool closeAfterData = false;

    if (responseLen != NULL)
        *responseLen = 0;
    if (eventType != NULL)
        *eventType = 7;
    if (response == NULL || metaLen == 0)
        return false;
    if (attempts == 0)
        attempts = 1;
    vm_client_encode_header(header, VM_CLIENT_REQUEST_FLAG_SCENE_SYNC_POLL,
                            metaLen, metaLen);
    /* Poll uses fewer attempts — dual worker already isolates it from data. */
    ok = vm_client_live_session_exchange(
        &g_vmClientPollSession,
        VM_CLIENT_POLL_CONNECT_TIMEOUT_MS,
        attempts,
        header, meta, metaLen, NULL, 0,
        response, responseCap, responseLen, eventType, &closeAfterData,
        NULL, 0, NULL, NULL);
    /*
     * Service ends every scene-sync poll TCP with handle_client return 2 (no
     * CLOSE_AFTER_DATA flag on CBMR).  Always drop the poll live session after
     * a finished exchange so the next poll cannot reuse a server-closed fd.
     */
    vm_client_live_session_close(&g_vmClientPollSession);
    (void)closeAfterData;
    return ok;
}

static void vm_net_mock_service_notify_disconnect(const char *reason)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 responseHeader[VM_CLIENT_FRAME_SIZE];
    u8 meta[48];
    u32 metaLen;
    vm_client_socket sock;
    bool ok;
    if (g_mockServiceClientId == 0)
        return;
    /* Drop live sessions before the control-plane disconnect frame. */
    vm_client_live_session_close(&g_vmClientDataSession);
    vm_client_live_session_close(&g_vmClientPollSession);
    metaLen = vm_client_encode_meta(meta, sizeof(meta));
    sock = vm_client_connect();
    if (sock == VM_CLIENT_INVALID_SOCKET)
        return;
    vm_client_encode_header(header, VM_CLIENT_REQUEST_FLAG_DISCONNECT,
                            metaLen, metaLen);
    ok = vm_client_send_all(sock, header, sizeof(header)) &&
         vm_client_send_all(sock, meta, metaLen) &&
         vm_client_recv_all(sock, responseHeader, sizeof(responseHeader)) &&
         memcmp(responseHeader, "CBMR", 4) == 0 &&
         vm_client_read_le32(responseHeader + 4) == 1;
    vm_client_close_socket(sock);
    printf("[info][network] disconnect client=%08x result=%s reason=%s\n",
           g_mockServiceClientId, ok ? "ok" : "failed", reason ? reason : "-");
}

typedef enum
{
    VM_CLIENT_JOB_DATA = 1,
    VM_CLIENT_JOB_SCENE_POLL = 2
} vm_client_job_kind;

typedef struct vm_client_job
{
    struct vm_client_job *next;
    u32 generation;
    u32 sequence;
    u32 enqueueMs;
    u32 connectId;
    u32 requestLen;
    vm_client_job_kind kind;
    u8 request[VM_CLIENT_REQUEST_MAX];
} vm_client_job;

typedef struct vm_client_completion
{
    struct vm_client_completion *next;
    u32 generation;
    u32 sequence;
    u32 enqueueMs;
    u32 workerStartMs;
    u32 workerDoneMs;
    u32 connectId;
    u32 eventType;
    u32 responseLen;
    u32 followupLen;
    vm_client_job_kind kind;
    bool success;
    bool closeAfterData;
    u8 *response;
    u8 *followup;
} vm_client_completion;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t dataCondition;
    pthread_cond_t pollCondition;
    pthread_t dataWorker;
    pthread_t pollWorker;
    bool dataWorkerStarted;
    bool pollWorkerStarted;
    bool stopRequested;
    bool scenePollOutstanding;
    /* Set after the first successful WT data exchange; gates speculative
     * scene-poll so startup version/login are not queued behind a cold connect. */
    bool serviceReachable;
    u32 successfulDataCount;
    u32 consecutiveTransportFailures;
    u32 pollBackoffUntilMs;
    u32 generation;
    u32 nextSequence;
    u32 queuedDataJobs;
    u32 queuedPollJobs;
    vm_client_job *dataJobHead;
    vm_client_job *dataJobTail;
    vm_client_job *pollJobHead;
    vm_client_job *pollJobTail;
    vm_client_completion *completionHead;
    vm_client_completion *completionTail;
} vm_client_async_state;

static vm_client_async_state g_vmClientAsync = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    0, 0,
    false, false, false, false, false,
    0, 0, 0, 1, 1, 0, 0,
    NULL, NULL, NULL, NULL, NULL, NULL};

static void vm_client_free_completion(vm_client_completion *completion)
{
    if (completion == NULL)
        return;
    free(completion->response);
    free(completion->followup);
    free(completion);
}

static void vm_client_push_completion(vm_client_completion *completion)
{
    if (completion == NULL)
        return;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    if (g_vmClientAsync.stopRequested ||
        completion->generation != g_vmClientAsync.generation)
    {
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
        vm_client_free_completion(completion);
        return;
    }
    if (g_vmClientAsync.completionTail != NULL)
        g_vmClientAsync.completionTail->next = completion;
    else
        g_vmClientAsync.completionHead = completion;
    g_vmClientAsync.completionTail = completion;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
}

static void *vm_client_data_worker_main(void *unused)
{
    u8 *responseScratch = (u8 *)malloc(sizeof(g_netMockResponse));
    u8 *followupScratch = (u8 *)malloc(VM_CLIENT_FOLLOWUP_MAX);
    (void)unused;
    for (;;)
    {
        vm_client_job *job;
        vm_client_completion *completion;
        u32 responseLen = 0;
        u32 eventType = 7;
        u32 followupLen = 0;
        bool closeAfterData = false;
        bool success;

        pthread_mutex_lock(&g_vmClientAsync.mutex);
        while (!g_vmClientAsync.stopRequested && g_vmClientAsync.dataJobHead == NULL)
            pthread_cond_wait(&g_vmClientAsync.dataCondition, &g_vmClientAsync.mutex);
        if (g_vmClientAsync.stopRequested)
        {
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            break;
        }
        job = g_vmClientAsync.dataJobHead;
        g_vmClientAsync.dataJobHead = job->next;
        if (g_vmClientAsync.dataJobHead == NULL)
            g_vmClientAsync.dataJobTail = NULL;
        if (g_vmClientAsync.queuedDataJobs != 0)
            --g_vmClientAsync.queuedDataJobs;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);

        completion = (vm_client_completion *)calloc(1, sizeof(*completion));
        if (completion == NULL || responseScratch == NULL || followupScratch == NULL)
        {
            free(job);
            free(completion);
            continue;
        }
        completion->generation = job->generation;
        completion->sequence = job->sequence;
        completion->enqueueMs = job->enqueueMs;
        completion->workerStartMs = SDL_GetTicks();
        completion->connectId = job->connectId;
        completion->kind = VM_CLIENT_JOB_DATA;
        completion->eventType = 7;
        {
            u32 attempts = (u32)VM_CLIENT_DATA_ATTEMPTS;

            pthread_mutex_lock(&g_vmClientAsync.mutex);
            if (!g_vmClientAsync.serviceReachable)
                attempts = (u32)VM_CLIENT_DATA_ATTEMPTS_UNREACHABLE;
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            success = vm_client_remote_request(job->request, job->requestLen,
                                               responseScratch, sizeof(g_netMockResponse),
                                               &responseLen, &eventType,
                                               &closeAfterData,
                                               followupScratch, VM_CLIENT_FOLLOWUP_MAX,
                                               &followupLen,
                                               attempts);
        }
        completion->workerDoneMs = SDL_GetTicks();
        completion->success = success;
        completion->closeAfterData = closeAfterData;
        completion->eventType = eventType;
        completion->responseLen = responseLen;
        completion->followupLen = followupLen;
        if (success && responseLen != 0)
        {
            completion->response = (u8 *)malloc(responseLen);
            if (completion->response != NULL)
                memcpy(completion->response, responseScratch, responseLen);
            else
                completion->success = false;
        }
        if (completion->success && followupLen != 0)
        {
            completion->followup = (u8 *)malloc(followupLen);
            if (completion->followup != NULL)
                memcpy(completion->followup, followupScratch, followupLen);
            else
                completion->success = false;
        }
        free(job);
        vm_client_push_completion(completion);
    }
    free(responseScratch);
    free(followupScratch);
    return NULL;
}

static void *vm_client_poll_worker_main(void *unused)
{
    u8 *responseScratch = (u8 *)malloc(sizeof(g_netMockResponse));
    (void)unused;
    for (;;)
    {
        vm_client_job *job;
        vm_client_completion *completion;
        u32 responseLen = 0;
        u32 eventType = 7;
        bool success;

        pthread_mutex_lock(&g_vmClientAsync.mutex);
        while (!g_vmClientAsync.stopRequested && g_vmClientAsync.pollJobHead == NULL)
            pthread_cond_wait(&g_vmClientAsync.pollCondition, &g_vmClientAsync.mutex);
        if (g_vmClientAsync.stopRequested)
        {
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            break;
        }
        job = g_vmClientAsync.pollJobHead;
        g_vmClientAsync.pollJobHead = job->next;
        if (g_vmClientAsync.pollJobHead == NULL)
            g_vmClientAsync.pollJobTail = NULL;
        if (g_vmClientAsync.queuedPollJobs != 0)
            --g_vmClientAsync.queuedPollJobs;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);

        completion = (vm_client_completion *)calloc(1, sizeof(*completion));
        if (completion == NULL || responseScratch == NULL)
        {
            free(job);
            free(completion);
            pthread_mutex_lock(&g_vmClientAsync.mutex);
            g_vmClientAsync.scenePollOutstanding = false;
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            continue;
        }
        completion->generation = job->generation;
        completion->sequence = job->sequence;
        completion->enqueueMs = job->enqueueMs;
        completion->workerStartMs = SDL_GetTicks();
        completion->connectId = job->connectId;
        completion->kind = VM_CLIENT_JOB_SCENE_POLL;
        completion->eventType = 7;
        {
            u32 attempts = 2;

            pthread_mutex_lock(&g_vmClientAsync.mutex);
            if (!g_vmClientAsync.serviceReachable)
                attempts = 1;
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            success = vm_client_remote_poll(responseScratch, sizeof(g_netMockResponse),
                                            &responseLen, &eventType, attempts);
        }
        completion->workerDoneMs = SDL_GetTicks();
        completion->success = success;
        completion->eventType = eventType;
        completion->responseLen = responseLen;
        if (success && responseLen != 0)
        {
            completion->response = (u8 *)malloc(responseLen);
            if (completion->response != NULL)
                memcpy(completion->response, responseScratch, responseLen);
            else
                completion->success = false;
        }
        free(job);
        vm_client_push_completion(completion);
    }
    free(responseScratch);
    return NULL;
}

static bool vm_client_ensure_worker(void)
{
    bool started;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    if (!g_vmClientAsync.stopRequested)
    {
        if (!g_vmClientAsync.dataWorkerStarted &&
            pthread_create(&g_vmClientAsync.dataWorker, NULL,
                           vm_client_data_worker_main, NULL) == 0)
        {
            g_vmClientAsync.dataWorkerStarted = true;
        }
        if (!g_vmClientAsync.pollWorkerStarted &&
            pthread_create(&g_vmClientAsync.pollWorker, NULL,
                           vm_client_poll_worker_main, NULL) == 0)
        {
            g_vmClientAsync.pollWorkerStarted = true;
        }
        if (g_vmClientAsync.dataWorkerStarted && g_vmClientAsync.pollWorkerStarted)
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                printf("[info][network] dual workers started data+poll queue_cap=%u "
                       "data_connect_ms=%u poll_connect_ms=%u\n",
                       VM_CLIENT_QUEUE_MAX,
                       (u32)VM_CLIENT_DATA_CONNECT_TIMEOUT_MS,
                       (u32)VM_CLIENT_POLL_CONNECT_TIMEOUT_MS);
            }
        }
    }
    started = g_vmClientAsync.dataWorkerStarted && g_vmClientAsync.pollWorkerStarted;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    return started;
}

static bool vm_client_request_contains_ascii(const u8 *request, u32 requestLen,
                                             const char *needle)
{
    size_t needleLen;
    u32 i;

    if (request == NULL || needle == NULL || requestLen == 0)
        return false;
    needleLen = strlen(needle);
    if (needleLen == 0 || requestLen < (u32)needleLen)
        return false;
    for (i = 0; i + (u32)needleLen <= requestLen; ++i)
    {
        if (memcmp(request + i, needle, needleLen) == 0)
            return true;
    }
    return false;
}

/*
 * Map-stone / region-teleport confirm uplinks arm deferred 30/1 on the server.
 * Lightweight wire heuristics only — do not import mock detectors.
 */
static bool vm_client_request_looks_like_teleport_confirm(const u8 *request,
                                                         u32 requestLen)
{
    u8 kind;
    u8 subtype;
    bool hasExit;
    bool hasMapIds;

    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T')
        return false;
    /* Guest uplink: first object major/kind/subtype at [4]/[5]/[6]. */
    kind = request[5];
    subtype = request[6];
    if (kind != 0x10)
        return false;
    if (subtype != 2 && subtype != 3 && subtype != 4)
        return false;
    hasExit = vm_client_request_contains_ascii(request, requestLen, "exitID") ||
              vm_client_request_contains_ascii(request, requestLen, "exitid");
    hasMapIds =
        vm_client_request_contains_ascii(request, requestLen, "curid") &&
        vm_client_request_contains_ascii(request, requestLen, "objid");
    if (subtype == 4)
        return hasMapIds;
    if (subtype == 2)
        return hasExit;
    /*
     * subtype 3 confirm is a short exit packet.  Post-enter follow-ups are
     * longer (16/3+27/11+27/4+7/42, len≈59) and must not re-arm urgent poll.
     */
    return hasExit && requestLen < 40u;
}

static void vm_client_arm_scene_poll_urgent(const char *reason)
{
    u32 nowMs = SDL_GetTicks();

    g_vm_scene_poll_urgent_ready_at_ms =
        nowMs + (u32)VM_CLIENT_POLL_URGENT_READY_DELAY_MS;
    g_vm_scene_poll_urgent_until_ms =
        nowMs + (u32)VM_CLIENT_POLL_URGENT_WINDOW_MS;
    g_vm_scene_poll_urgent_remaining = (u32)VM_CLIENT_POLL_URGENT_MAX_POLLS;
    printf("[info][network] scene_poll_urgent_arm reason=%s ready_ms=%u "
           "window_ms=%u max_polls=%u evidence=pixi-post-confirm-poll\n",
           reason != NULL ? reason : "teleport-confirm",
           (u32)VM_CLIENT_POLL_URGENT_READY_DELAY_MS,
           (u32)VM_CLIENT_POLL_URGENT_WINDOW_MS,
           (u32)VM_CLIENT_POLL_URGENT_MAX_POLLS);
}

static bool vm_client_enqueue(vm_client_job_kind kind, u32 connectId,
                              const u8 *request, u32 requestLen)
{
    vm_client_job *job;
    u32 queuedTotal;
    if (kind == VM_CLIENT_JOB_DATA &&
        (request == NULL || requestLen == 0 || requestLen > VM_CLIENT_REQUEST_MAX))
        return false;
    if (!vm_client_ensure_worker())
        return false;
    if (kind == VM_CLIENT_JOB_DATA &&
        vm_client_request_looks_like_teleport_confirm(request, requestLen))
    {
        u8 subtype = request[6];
        if (subtype == 4)
            vm_client_arm_scene_poll_urgent("map-confirm-16/4");
        else if (subtype == 2)
            vm_client_arm_scene_poll_urgent("teleport-16/2");
        else
            vm_client_arm_scene_poll_urgent("teleport-16/3");
    }
    job = (vm_client_job *)calloc(1, sizeof(*job));
    if (job == NULL)
        return false;
    job->kind = kind;
    job->connectId = connectId;
    job->requestLen = requestLen;
    job->enqueueMs = SDL_GetTicks();
    if (requestLen != 0)
        memcpy(job->request, request, requestLen);

    pthread_mutex_lock(&g_vmClientAsync.mutex);
    if (kind == VM_CLIENT_JOB_SCENE_POLL)
    {
        u32 nowMs = SDL_GetTicks();
        /*
         * Dual workers: poll no longer blocks behind WT data in one FIFO.
         * Still gate early title polls and honor transport backoff.
         */
        if (g_vmClientAsync.scenePollOutstanding ||
            g_vmClientAsync.successfulDataCount < 3 ||
            (g_vmClientAsync.pollBackoffUntilMs != 0 &&
             nowMs < g_vmClientAsync.pollBackoffUntilMs))
        {
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            free(job);
            return false;
        }
        if (!g_vmClientAsync.serviceReachable)
        {
            printf("[info][network] reconnect_probe enqueue target=%s:%u "
                   "failures=%u successful_data=%u\n",
                   g_mockServiceHost, g_mockServicePort,
                   g_vmClientAsync.consecutiveTransportFailures,
                   g_vmClientAsync.successfulDataCount);
        }
    }
    queuedTotal = g_vmClientAsync.queuedDataJobs + g_vmClientAsync.queuedPollJobs;
    if (g_vmClientAsync.stopRequested || queuedTotal >= VM_CLIENT_QUEUE_MAX)
    {
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
        free(job);
        return false;
    }
    job->generation = g_vmClientAsync.generation;
    job->sequence = g_vmClientAsync.nextSequence++;
    if (g_vmClientAsync.nextSequence == 0)
        g_vmClientAsync.nextSequence = 1;

    if (kind == VM_CLIENT_JOB_DATA)
    {
        if (g_vmClientAsync.dataJobTail != NULL)
            g_vmClientAsync.dataJobTail->next = job;
        else
            g_vmClientAsync.dataJobHead = job;
        g_vmClientAsync.dataJobTail = job;
        ++g_vmClientAsync.queuedDataJobs;
        pthread_cond_signal(&g_vmClientAsync.dataCondition);
    }
    else
    {
        if (g_vmClientAsync.pollJobTail != NULL)
            g_vmClientAsync.pollJobTail->next = job;
        else
            g_vmClientAsync.pollJobHead = job;
        g_vmClientAsync.pollJobTail = job;
        ++g_vmClientAsync.queuedPollJobs;
        g_vmClientAsync.scenePollOutstanding = true;
        pthread_cond_signal(&g_vmClientAsync.pollCondition);
    }
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    return true;
}

static void vm_net_mock_async_drain_completions(void)
{
    static u32 failureLogCount = 0;
    for (;;)
    {
        vm_client_completion *completion;
        vm_net_channel *channel;
        u32 generation;
        u32 nowMs;

        pthread_mutex_lock(&g_vmClientAsync.mutex);
        completion = g_vmClientAsync.completionHead;
        if (completion != NULL)
        {
            g_vmClientAsync.completionHead = completion->next;
            if (g_vmClientAsync.completionHead == NULL)
                g_vmClientAsync.completionTail = NULL;
            if (completion->kind == VM_CLIENT_JOB_SCENE_POLL)
                g_vmClientAsync.scenePollOutstanding = false;
        }
        generation = g_vmClientAsync.generation;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
        if (completion == NULL)
            break;
        if (completion->generation != generation)
        {
            vm_client_free_completion(completion);
            continue;
        }
        if (!completion->success)
        {
            u32 failures;
            u32 backoffMs;
            bool markedDown = false;

            pthread_mutex_lock(&g_vmClientAsync.mutex);
            if (g_vmClientAsync.consecutiveTransportFailures < 8)
                ++g_vmClientAsync.consecutiveTransportFailures;
            failures = g_vmClientAsync.consecutiveTransportFailures;
            /* 2s, 4s, 8s… — stop scene-poll from SYN-hammering a remote host. */
            backoffMs = 2000u << (failures > 3 ? 2 : (failures - 1));
            if (backoffMs > 8000u)
                backoffMs = 8000u;
            g_vmClientAsync.pollBackoffUntilMs = SDL_GetTicks() + backoffMs;
            if (failures >= 2 && g_vmClientAsync.serviceReachable)
            {
                g_vmClientAsync.serviceReachable = false;
                markedDown = true;
            }
            else if (failures >= 2)
                g_vmClientAsync.serviceReachable = false;
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            if (markedDown)
            {
                printf("[warn][network] transport_unavailable target=%s:%u kind=%s "
                       "failures=%u poll_backoff_ms=%u connect=%u queue_ms=%u "
                       "network_ms=%u\n",
                       g_mockServiceHost, g_mockServicePort,
                       completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene-poll" : "data",
                       failures, backoffMs, completion->connectId,
                       completion->workerStartMs - completion->enqueueMs,
                       completion->workerDoneMs - completion->workerStartMs);
            }
            else if (failureLogCount < 16)
            {
                ++failureLogCount;
                printf("[warn][network] server request failed target=%s:%u kind=%s "
                       "failures=%u poll_backoff_ms=%u connect=%u queue_ms=%u "
                       "network_ms=%u\n",
                       g_mockServiceHost, g_mockServicePort,
                       completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene-poll" : "data",
                       failures, backoffMs, completion->connectId,
                       completion->workerStartMs - completion->enqueueMs,
                       completion->workerDoneMs - completion->workerStartMs);
            }
            vm_client_free_completion(completion);
            continue;
        }
        failureLogCount = 0;
        pthread_mutex_lock(&g_vmClientAsync.mutex);
        g_vmClientAsync.consecutiveTransportFailures = 0;
        g_vmClientAsync.pollBackoffUntilMs = 0;
        if (completion->kind == VM_CLIENT_JOB_DATA)
        {
            g_vmClientAsync.serviceReachable = true;
            if (g_vmClientAsync.successfulDataCount < 1000000u)
                ++g_vmClientAsync.successfulDataCount;
        }
        else if (completion->kind == VM_CLIENT_JOB_SCENE_POLL &&
                 g_vmClientAsync.successfulDataCount >= 3)
        {
            /* Scene-poll success after a radio blip restores the heartbeat path. */
            if (!g_vmClientAsync.serviceReachable)
            {
                printf("[info][network] reconnect_recovered via scene-poll "
                       "target=%s:%u\n",
                       g_mockServiceHost, g_mockServicePort);
            }
            g_vmClientAsync.serviceReachable = true;
        }
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
        if (completion->responseLen == 0)
        {
            vm_client_free_completion(completion);
            continue;
        }
        channel = scheduler_find_net_channel(completion->connectId);
        if (channel == NULL || channel->callback == 0)
        {
            printf("[warn][network] drop_success_no_channel connect=%u kind=%s "
                   "resp=%u (CBE net channel gone before deliver)\n",
                   completion->connectId,
                   completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene-poll"
                                                                : "data",
                   completion->responseLen);
            vm_client_free_completion(completion);
            continue;
        }
        (void)vm_client_prepare_cbe_downlink(&completion->response,
                                             &completion->responseLen);
        if (completion->followup != NULL && completion->followupLen != 0)
            (void)vm_client_prepare_cbe_downlink(&completion->followup,
                                                  &completion->followupLen);
        if (vm_client_deliver_wt_response(
                completion->response, completion->responseLen,
                completion->eventType, channel->callback, channel->context,
                completion->connectId, true, true) == 0)
        {
            vm_client_free_completion(completion);
            continue;
        }
        vm_client_hangup_maybe_flush_scene_default_ack(
            completion->connectId, completion->response,
            completion->responseLen);
        if (g_vmClientHangupAutoAckAfterPanel)
        {
            u32 hangupConnect = completion->connectId;

            g_vmClientHangupAutoAckAfterPanel = 0;
            printf("[info][network] cbe_hangup_auto_ack_after_panel "
                   "connect=%u evidence=legacy-swallowed-path\n",
                   hangupConnect);
            (void)vm_client_hangup_ack_guest_25_2(hangupConnect);
        }
        g_netDownLinkData += completion->responseLen;
        nowMs = SDL_GetTicks();
        {
            static u32 s_successLogBucketMs = 0;
            static u32 s_successLogSuppressed = 0;
            u32 queueMs = completion->workerStartMs - completion->enqueueMs;
            u32 networkMs = completion->workerDoneMs - completion->workerStartMs;
            u32 deliverMs = nowMs - completion->workerDoneMs;
            bool emitLog = networkMs >= 50u || queueMs >= 50u ||
                           s_successLogBucketMs == 0 ||
                           nowMs < s_successLogBucketMs ||
                           (nowMs - s_successLogBucketMs) >= 1000u;

            if (emitLog)
            {
                printf("[info][network] queue_%s connect=%u event=%u resp=%u "
                       "queue_ms=%u network_ms=%u deliver_ms=%u suppressed=%u\n",
                       completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene_poll"
                                                                    : "data",
                       completion->connectId, completion->eventType,
                       completion->responseLen, queueMs, networkMs, deliverMs,
                       s_successLogSuppressed);
                s_successLogBucketMs = nowMs;
                s_successLogSuppressed = 0;
            }
            else
            {
                s_successLogSuppressed += 1u;
            }
        }
        if (completion->followupLen != 0 && completion->followup != NULL)
        {
            u32 followEvents = vm_client_deliver_wt_response(
                completion->followup, completion->followupLen, 7,
                channel->callback, channel->context, completion->connectId,
                false, false);
            if (followEvents != 0)
            {
                g_netDownLinkData += completion->followupLen;
                if (followEvents == 1)
                {
                    printf("[info][network] queue_data_followup connect=%u "
                           "event=7 resp=%u\n",
                           completion->connectId, completion->followupLen);
                }
            }
        }
        if (completion->closeAfterData)
        {
            /* Exchange already closed the live sock; keep CBE disconnect event. */
            vm_client_live_session_close(&g_vmClientDataSession);
            scheduler_queue_net_event(9, 0, 0, 0, channel->callback, channel->context);
        }
        vm_client_free_completion(completion);
    }
}

static void vm_net_mock_async_reset(void)
{
    vm_client_job *dataJob;
    vm_client_job *pollJob;
    vm_client_completion *completion;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    ++g_vmClientAsync.generation;
    if (g_vmClientAsync.generation == 0)
        g_vmClientAsync.generation = 1;
    dataJob = g_vmClientAsync.dataJobHead;
    pollJob = g_vmClientAsync.pollJobHead;
    completion = g_vmClientAsync.completionHead;
    g_vmClientAsync.dataJobHead = NULL;
    g_vmClientAsync.dataJobTail = NULL;
    g_vmClientAsync.pollJobHead = NULL;
    g_vmClientAsync.pollJobTail = NULL;
    g_vmClientAsync.completionHead = NULL;
    g_vmClientAsync.completionTail = NULL;
    g_vmClientAsync.queuedDataJobs = 0;
    g_vmClientAsync.queuedPollJobs = 0;
    g_vmClientAsync.scenePollOutstanding = false;
    g_vmClientAsync.serviceReachable = false;
    g_vmClientAsync.successfulDataCount = 0;
    g_vmClientAsync.consecutiveTransportFailures = 0;
    g_vmClientAsync.pollBackoffUntilMs = 0;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    while (dataJob != NULL)
    {
        vm_client_job *next = dataJob->next;
        free(dataJob);
        dataJob = next;
    }
    while (pollJob != NULL)
    {
        vm_client_job *next = pollJob->next;
        free(pollJob);
        pollJob = next;
    }
    while (completion != NULL)
    {
        vm_client_completion *next = completion->next;
        vm_client_free_completion(completion);
        completion = next;
    }
    vm_client_live_session_close(&g_vmClientDataSession);
    vm_client_live_session_close(&g_vmClientPollSession);
    vm_client_cbe_bag_defer_clear();
}

static void vm_net_mock_async_shutdown(void)
{
    bool joinData;
    bool joinPoll;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    joinData = g_vmClientAsync.dataWorkerStarted;
    joinPoll = g_vmClientAsync.pollWorkerStarted;
    g_vmClientAsync.stopRequested = true;
    pthread_cond_broadcast(&g_vmClientAsync.dataCondition);
    pthread_cond_broadcast(&g_vmClientAsync.pollCondition);
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    if (joinData)
        pthread_join(g_vmClientAsync.dataWorker, NULL);
    if (joinPoll)
        pthread_join(g_vmClientAsync.pollWorker, NULL);
    vm_net_mock_async_reset();
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    g_vmClientAsync.dataWorkerStarted = false;
    g_vmClientAsync.pollWorkerStarted = false;
    g_vmClientAsync.stopRequested = false;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
}

/* Local empty WT so a truncated uplink still dismisses CBE net-wait. */
static bool vm_client_deliver_local_empty_wt(u32 connectId)
{
    u8 packet[8];
    vm_net_channel *channel = scheduler_find_net_channel(connectId);

    if (channel == NULL || channel->callback == 0)
    {
        printf("[warn][network] empty_wt_ack no channel connect=%u\n",
               connectId);
        return false;
    }
    memset(packet, 0, sizeof(packet));
    vm_client_finish_wt_packet(packet, 5, 0);
    if (vm_client_deliver_wt_response(packet, 5, 7, channel->callback,
                                      channel->context, connectId, false,
                                      false) == 0)
    {
        printf("[warn][network] empty_wt_ack deliver failed connect=%u\n",
               connectId);
        return false;
    }
    printf("[info][network] empty_wt_ack connect=%u event=7 len=5 "
           "objects=0\n",
           connectId);
    return true;
}

static u32 vm_client_build_scene_default25_5_ack(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < 32u)
        return 0;
    memset(out, 0, 5);
    if (!vm_client_begin_local_wt_object(out, outCap, &pos, 1, 25, 5,
                                         &objectStart) ||
        !vm_client_put_object_u8_field(out, outCap, &pos, "result", 4))
    {
        return 0;
    }
    vm_client_finish_local_wt_object(out, objectStart, pos);
    vm_client_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_client_deliver_local_scene_default25_5(u32 connectId)
{
    u8 packet[64];
    u32 packetLen;
    vm_net_channel *channel = scheduler_find_net_channel(connectId);

    if (channel == NULL || channel->callback == 0)
    {
        printf("[warn][network] cbe_hangup_flush_25_5 no channel connect=%u\n",
               connectId);
        return false;
    }
    packetLen = vm_client_build_scene_default25_5_ack(packet, sizeof(packet));
    if (packetLen == 0)
        return false;
    if (vm_client_deliver_wt_response(packet, packetLen, 7, channel->callback,
                                      channel->context, connectId, false,
                                      false) == 0)
    {
        printf("[warn][network] cbe_hangup_flush_25_5 deliver failed "
               "connect=%u\n",
               connectId);
        return false;
    }
    printf("[info][network] cbe_hangup_flush_25_5 connect=%u len=%u "
           "evidence=4/8-map-then-mmGame-25/5-result4\n",
           connectId, packetLen);
    return true;
}

static void vm_client_hangup_maybe_flush_scene_default_ack(u32 connectId,
                                                           const u8 *packet,
                                                           u32 packetLen)
{
    u32 flushConnect;

    if (g_vmClientHangupOweSceneDefaultAck == 0)
        return;
    /*
     * Lone 4/8 is map return → complete swallowed mmGame 25/5.
     * Hangup fight 2 is 4/8+4/10 (CBE poll-synth, no live scene node) or
     * 4/8+4/5 (Pixi live-node).  client_out 2026-10-05: flushing 25/5 on
     * 4/8+4/10 made guest send 4/1; second enter left font obj+0xc=0
     * → UC_ERR_MAP pc=0x01004E36 SCR_Render.
     */
    if (!vm_client_packet_has_battle_subtype8(packet, packetLen))
        return;
    if (vm_client_packet_has_battle_subtype(packet, packetLen, 5) ||
        vm_client_packet_has_battle_subtype(packet, packetLen, 10))
    {
        printf("[info][network] cbe_hangup_keep_25_5 connect=%u "
               "evidence=4/8-plus-4/5-or-4/10-reenter-no-map-flush\n",
               connectId);
        return;
    }
    flushConnect = g_vmClientHangupOweSceneDefaultConnect != 0
                       ? g_vmClientHangupOweSceneDefaultConnect
                       : connectId;
    g_vmClientHangupOweSceneDefaultAck = 0;
    g_vmClientHangupOweSceneDefaultConnect = 0;
    (void)vm_client_deliver_local_scene_default25_5(flushConnect);
}

/*
 * HandleBattleEnterReq sets enter-state=3 (「获取数据」) before 2/10 leaves.
 * Empty WT ack leaves that wait forever.  Same dismiss contract as mock hangup
 * soft-fail prefix: empty 2/10 + 25/12 result=4 (no tip — bag clickthrough
 * must stay silent).
 */
static bool vm_client_begin_local_wt_object(u8 *out, u32 outCap, u32 *pos,
                                            u8 major, u8 kind, u8 subtype,
                                            u32 *objectStart)
{
    if (out == NULL || pos == NULL || *pos + 6u > outCap)
        return false;
    if (objectStart != NULL)
        *objectStart = *pos;
    out[(*pos)++] = major;
    out[(*pos)++] = kind;
    out[(*pos)++] = subtype;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    return true;
}

static void vm_client_finish_local_wt_object(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;

    out[objectStart + 4] = (u8)(objectLen >> 8);
    out[objectStart + 5] = (u8)objectLen;
}

static u32 vm_client_build_hangup_dismiss_wt(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    static const u8 kEmpty = 0;
    static const u8 kOthernum0[] = {0x00, 0x04, 0x00, 0x00, 0x00, 0x00};
    static const u8 kResult4[] = {0x00, 0x01, 0x04};

    if (out == NULL || outCap < 64u)
        return 0;
    memset(out, 0, 5);
    if (!vm_client_begin_local_wt_object(out, outCap, &pos, 1, 2, 10,
                                         &objectStart) ||
        !vm_client_put_object_field(out, outCap, &pos, "othernum", kOthernum0,
                                    (u16)sizeof(kOthernum0)) ||
        !vm_client_put_object_field(out, outCap, &pos, "otherinfo", &kEmpty,
                                    0))
    {
        return 0;
    }
    vm_client_finish_local_wt_object(out, objectStart, pos);
    ++objectCount;
    if (!vm_client_begin_local_wt_object(out, outCap, &pos, 1, 0x19, 12,
                                         &objectStart) ||
        !vm_client_put_object_field(out, outCap, &pos, "result", kResult4,
                                    (u16)sizeof(kResult4)))
    {
        return 0;
    }
    vm_client_finish_local_wt_object(out, objectStart, pos);
    ++objectCount;
    vm_client_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_client_deliver_local_hangup_dismiss(u32 connectId)
{
    u8 packet[128];
    u32 packetLen;
    vm_net_channel *channel = scheduler_find_net_channel(connectId);

    if (channel == NULL || channel->callback == 0)
    {
        printf("[warn][network] hangup_dismiss_ack no channel connect=%u\n",
               connectId);
        return false;
    }
    packetLen = vm_client_build_hangup_dismiss_wt(packet, sizeof(packet));
    if (packetLen == 0)
    {
        printf("[warn][network] hangup_dismiss_ack build failed connect=%u\n",
               connectId);
        return vm_client_deliver_local_empty_wt(connectId);
    }
    if (vm_client_deliver_wt_response(packet, packetLen, 7, channel->callback,
                                      channel->context, connectId, false,
                                      false) == 0)
    {
        printf("[warn][network] hangup_dismiss_ack deliver failed connect=%u "
               "len=%u\n",
               connectId, packetLen);
        return false;
    }
    printf("[info][network] hangup_dismiss_ack connect=%u event=7 len=%u "
           "objects=2/10+25/12 evidence=battle-enter-state3-clear\n",
           connectId, packetLen);
    return true;
}

static bool vm_client_put_object_u8_field(u8 *out, u32 outCap, u32 *pos,
                                          const char *name, u8 value)
{
    u8 enc[3];

    enc[0] = 0;
    enc[1] = 1;
    enc[2] = value;
    return vm_client_put_object_field(out, outCap, pos, name, enc, 3);
}

static bool vm_client_put_object_u16_field(u8 *out, u32 outCap, u32 *pos,
                                           const char *name, u16 value)
{
    u8 enc[4];

    enc[0] = 0;
    enc[1] = 2;
    enc[2] = (u8)(value >> 8);
    enc[3] = (u8)value;
    return vm_client_put_object_field(out, outCap, pos, name, enc, 4);
}

static bool vm_client_put_object_blob_field(u8 *out, u32 outCap, u32 *pos,
                                            const char *name, const u8 *data,
                                            u16 dataLen)
{
    u32 nameLen = name ? (u32)strlen(name) : 0;
    u16 valueLen;

    if (out == NULL || pos == NULL || nameLen == 0 || nameLen > 0xffu ||
        (data == NULL && dataLen != 0))
        return false;
    valueLen = (u16)(dataLen + 2u);
    if (*pos + 1u + nameLen + 2u + valueLen > outCap)
        return false;
    out[(*pos)++] = (u8)nameLen;
    memcpy(out + *pos, name, nameLen);
    *pos += nameLen;
    out[(*pos)++] = (u8)(valueLen >> 8);
    out[(*pos)++] = (u8)valueLen;
    out[(*pos)++] = (u8)(dataLen >> 8);
    out[(*pos)++] = (u8)dataLen;
    if (dataLen != 0)
    {
        memcpy(out + *pos, data, dataLen);
        *pos += dataLen;
    }
    return true;
}

/*
 * CBE_EMU native confirm: 2/10 empty + 25/3 {num, info[181], mnum}.
 * info[180]=row count; Handle25_2 then sets game+1136 hangup mode.
 */
static u32 vm_client_build_hangup_panel_wt(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    static const u8 kEmpty = 0;
    static const u8 kOthernum0[] = {0x00, 0x04, 0x00, 0x00, 0x00, 0x00};
    /* GBK: 本次挂机确认 / 挂机场次上限：不限次 / 掉落放入背包 / 是否继续挂机？ */
    static const char panelFormat[] =
        "\xB1\xBE\xB4\xCE\xB9\xD2\xBB\xFA\xC8\xB7\xC8\xCF\n"
        "\xB9\xD2\xBB\xFA\xB3\xA1\xB4\xCE\xC9\xCF\xCF\xDE\xA3\xBA\xB2\xBB\xCF\xDE\xB4\xCE\n"
        "\xB5\xF4\xC2\xE4\xCE\xEF\xC6\xB7\xBD\xAB\xB7\xC5\xC8\xEB\xB1\xB3\xB0\xFC\n"
        "\xCA\xC7\xB7\xF1\xBC\xCC\xD0\xF8\xB9\xD2\xBB\xFA\xA3\xBF";
    u8 info[181];
    int textLen;

    if (out == NULL || outCap < 256u)
        return 0;
    memset(out, 0, 5);
    memset(info, 0, sizeof(info));
    textLen = snprintf((char *)info, 180, "%s", panelFormat);
    if (textLen < 0 || textLen >= 180)
        return 0;
    info[180] = 4;
    if (!vm_client_begin_local_wt_object(out, outCap, &pos, 1, 2, 10,
                                         &objectStart) ||
        !vm_client_put_object_field(out, outCap, &pos, "othernum", kOthernum0,
                                    (u16)sizeof(kOthernum0)) ||
        !vm_client_put_object_field(out, outCap, &pos, "otherinfo", &kEmpty,
                                    0))
    {
        return 0;
    }
    vm_client_finish_local_wt_object(out, objectStart, pos);
    ++objectCount;
    if (!vm_client_begin_local_wt_object(out, outCap, &pos, 1, 25, 3,
                                         &objectStart) ||
        !vm_client_put_object_u16_field(out, outCap, &pos, "num", 0) ||
        !vm_client_put_object_blob_field(out, outCap, &pos, "info", info,
                                         (u16)sizeof(info)) ||
        !vm_client_put_object_u8_field(out, outCap, &pos, "mnum", 0))
    {
        return 0;
    }
    vm_client_finish_local_wt_object(out, objectStart, pos);
    ++objectCount;
    vm_client_finish_wt_packet(out, pos, objectCount);
    return pos;
}

/*
 * Guest uplink wire (not response): WT + u16 len + objects with 5-byte
 * headers (major/kind/subtype/len16).  Response-shaped packets put
 * objectCount at [4], so get_wt_header_kind_subtype reads kind=1/subtype=25
 * and mock falls through to tip「功能暂未实现(1/25)」.
 */
static u32 vm_client_build_hangup_mode25_2_wt(u8 *out, u32 outCap, u8 type)
{
    u32 pos = 4;
    u32 objectStart;
    u8 resultEnc[3];
    u8 typeEnc[3];

    if (out == NULL || outCap < 40u)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 0;
    objectStart = pos;
    if (pos + 5u > outCap)
        return 0;
    out[pos++] = 1;
    out[pos++] = 25;
    out[pos++] = 2;
    out[pos++] = 0;
    out[pos++] = 0;
    resultEnc[0] = 0;
    resultEnc[1] = 1;
    resultEnc[2] = 1;
    typeEnc[0] = 0;
    typeEnc[1] = 1;
    typeEnc[2] = type;
    if (!vm_client_put_object_field(out, outCap, &pos, "result", resultEnc, 3) ||
        !vm_client_put_object_field(out, outCap, &pos, "type", typeEnc, 3))
    {
        return 0;
    }
    {
        u32 objectLen = pos - objectStart;

        out[objectStart + 3] = (u8)(objectLen >> 8);
        out[objectStart + 4] = (u8)objectLen;
    }
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    return pos;
}

static bool vm_client_deliver_local_wt_buf(u32 connectId, const u8 *packet,
                                           u32 packetLen, const char *tag)
{
    vm_net_channel *channel = scheduler_find_net_channel(connectId);

    if (packet == NULL || packetLen < 5 || channel == NULL ||
        channel->callback == 0)
    {
        printf("[warn][network] %s no channel/packet connect=%u\n",
               tag ? tag : "local_wt", connectId);
        return false;
    }
    if (vm_client_deliver_wt_response(packet, packetLen, 7, channel->callback,
                                      channel->context, connectId, false,
                                      false) == 0)
    {
        printf("[warn][network] %s deliver failed connect=%u len=%u\n",
               tag ? tag : "local_wt", connectId, packetLen);
        return false;
    }
    printf("[info][network] %s connect=%u event=7 len=%u\n",
           tag ? tag : "local_wt", connectId, packetLen);
    return true;
}

static bool vm_client_find_u8_field(const u8 *buf, u32 len, const char *field,
                                    u8 *value)
{
    u32 fieldLen;
    u32 i;

    if (buf == NULL || field == NULL || value == NULL)
        return false;
    fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || len < fieldLen + 5)
        return false;
    for (i = 0; i + fieldLen + 5 <= len; ++i)
    {
        u32 p;

        if (buf[i] != (u8)fieldLen)
            continue;
        if (memcmp(buf + i + 1, field, fieldLen) != 0)
            continue;
        p = i + 1 + fieldLen;
        if (p < len && buf[p] == 0)
            p++;
        if (p + 4 <= len && buf[p] == 0x03 && buf[p + 1] == 0 &&
            buf[p + 2] == 1)
        {
            *value = buf[p + 3];
            return true;
        }
        if (p + 2 <= len && buf[p] == 1)
        {
            *value = buf[p + 1];
            return true;
        }
        if (p + 5 <= len && buf[p] == 0 && buf[p + 1] == 3 &&
            buf[p + 2] == 0 && buf[p + 3] == 1)
        {
            *value = buf[p + 4];
            return true;
        }
        if (p + 3 <= len && buf[p] == 0 && buf[p + 1] == 1)
        {
            *value = buf[p + 2];
            return true;
        }
    }
    return false;
}

static bool vm_client_request_is_cbe_hangup_start(const u8 *req, u32 len)
{
    u32 pos = 4;
    bool have210 = false;
    u8 type = 0;
    u32 extraObjects = 0;

    if (req == NULL || len < 9 || req[0] != 'W' || req[1] != 'T')
        return false;
    /*
     * Hangup start is 2/10 Type=2 plus optional 25/3 / 2/1.  mmShop open
     * batches the same 2/10 companion with 14/14+14/4+14/5+14/6; scanning the
     * whole WT for Type==2 then swallowing (stack>=2) acked the mall with an
     * empty WT and left the shop with no items.
     */
    while (pos + 5 <= len)
    {
        u16 objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);
        u8 kind;
        u8 subtype;

        if (objectLen < 5 || pos + objectLen > len)
            break;
        kind = req[pos + 1];
        subtype = req[pos + 2];
        if (req[pos] == 1 && kind == 2 && subtype == 10)
        {
            have210 = true;
            if (!vm_client_find_u8_field(req + pos, objectLen, "Type", &type))
                (void)vm_client_find_u8_field(req + pos, objectLen, "type",
                                              &type);
        }
        else if (!((kind == 25 && subtype == 3) ||
                   (kind == 2 && subtype == 1)))
        {
            ++extraObjects;
        }
        pos += objectLen;
    }
    return have210 && extraObjects == 0 && type == 2;
}

static bool vm_client_request_is_empty_scene_default_25_5(const u8 *req, u32 len)
{
    u32 pos = 4;
    u16 objectLen;
    u32 extra = 0;

    if (req == NULL || len < 9 || req[0] != 'W' || req[1] != 'T')
        return false;
    if (pos + 5 > len)
        return false;
    objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);
    if (objectLen < 5 || pos + objectLen > len ||
        req[pos] != 1 || req[pos + 1] != 25 || req[pos + 2] != 5)
    {
        return false;
    }
    if (objectLen != 5)
        return false;
    pos += objectLen;
    while (pos + 5 <= len)
    {
        objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);
        if (objectLen < 5 || pos + objectLen > len)
            break;
        ++extra;
        pos += objectLen;
    }
    return extra == 0 && pos == len;
}

static bool vm_client_request_is_map_battle_enter_4_1(const u8 *req, u32 len)
{
    u32 pos = 4;
    u16 objectLen;

    if (req == NULL || len < 9 || req[0] != 'W' || req[1] != 'T')
        return false;
    if (pos + 5 > len)
        return false;
    objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);
    if (objectLen < 5 || pos + objectLen > len)
        return false;
    return req[pos] == 1 && req[pos + 1] == 4 && req[pos + 2] == 1;
}

/*
 * Standalone 25/2(type=1|2) or compound stop 2/10(Type=2)+25/2(type=2).
 * evidence: CBE_EMU vm_net_mock_build_scene_hangup_mode_response
 */
static bool vm_client_request_parse_hangup_mode25_2(const u8 *req, u32 len,
                                                    u8 *typeOut,
                                                    bool *compoundStopOut)
{
    u32 pos = 4;
    u8 kind = 0;
    u8 subtype = 0;
    u8 requestedType = 0;
    bool compoundStop = false;

    if (typeOut)
        *typeOut = 0;
    if (compoundStopOut)
        *compoundStopOut = false;
    if (req == NULL || len < 9 || req[0] != 'W' || req[1] != 'T')
        return false;
    /* Request WT: first object begins at offset 4 (major/kind/subtype). */
    kind = req[5];
    subtype = req[6];
    if (kind == 25 && subtype == 2)
    {
        u16 objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);

        if (objectLen < 5 || pos + objectLen != len || req[pos] != 1 ||
            req[pos + 1] != 25 || req[pos + 2] != 2)
            return false;
        if (!vm_client_find_u8_field(req + pos, objectLen, "type",
                                     &requestedType) &&
            !vm_client_find_u8_field(req + pos, objectLen, "Type",
                                     &requestedType))
            return false;
        if (requestedType != 1 && requestedType != 2)
            return false;
    }
    else if (kind == 2 && subtype == 10)
    {
        u16 objectLen;
        u8 actorType = 0;

        if (pos + 5 > len)
            return false;
        objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);
        if (objectLen < 5 || pos + objectLen > len || req[pos] != 1 ||
            req[pos + 1] != 2 || req[pos + 2] != 10)
            return false;
        if (!vm_client_find_u8_field(req + pos, objectLen, "Type", &actorType) &&
            !vm_client_find_u8_field(req + pos, objectLen, "type", &actorType))
            return false;
        if (actorType != 2)
            return false;
        pos += objectLen;
        if (pos + 5 > len)
            return false;
        objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);
        if (objectLen < 5 || pos + objectLen != len || req[pos] != 1 ||
            req[pos + 1] != 25 || req[pos + 2] != 2)
            return false;
        if (!vm_client_find_u8_field(req + pos, objectLen, "type",
                                     &requestedType) &&
            !vm_client_find_u8_field(req + pos, objectLen, "Type",
                                     &requestedType))
            return false;
        if (requestedType != 2)
            return false;
        compoundStop = true;
    }
    else
    {
        return false;
    }
    if (typeOut)
        *typeOut = requestedType;
    if (compoundStopOut)
        *compoundStopOut = compoundStop;
    return true;
}

static void vm_client_hangup_store_pending_start(u32 connectId, const u8 *req,
                                                 u32 len)
{
    if (req == NULL || len == 0 || len > sizeof(g_vmClientHangupPendingStart))
        return;
    memcpy(g_vmClientHangupPendingStart, req, len);
    g_vmClientHangupPendingStartLen = len;
    g_vmClientHangupPendingConnectId = connectId;
}

static bool vm_client_find_mmbattle_8996(u32 *entryOut, u32 *moduleR9Out)
{
    u32 codeBase = 0;
    u32 moduleR9 = 0;
    u32 i;
    int appIndex;

    if (entryOut)
        *entryOut = 0;
    if (moduleR9Out)
        *moduleR9Out = 0;
    /* Prefer the module that issued the hangup uplink (send LR / send R9). */
    appIndex = vm_dl_find_loaded_index_by_pc(g_lastNetSendLr);
    if (appIndex < 0)
        appIndex = vm_dl_find_loaded_index_by_pc(g_lastNetSendLr | 1u);
    if (appIndex < 0 && g_vmClientHangupSendR9 != 0)
        appIndex = vm_dl_find_loaded_index_by_sp_bf(g_vmClientHangupSendR9);
    if (appIndex >= 0)
    {
        codeBase = g_vmDlLoadedApps[appIndex].buffer;
        moduleR9 = g_vmDlLoadedApps[appIndex].spBf;
        if (moduleR9 == 0 && g_vmClientHangupSendR9 != 0)
            moduleR9 = g_vmClientHangupSendR9;
        if (moduleR9 == 0)
            moduleR9 = codeBase + 0x14000u;
        if (codeBase != 0)
        {
            if (entryOut)
                *entryOut = (codeBase + 0x8996u) | 1u;
            if (moduleR9Out)
                *moduleR9Out = moduleR9;
            return true;
        }
    }
    if (g_vmClientHangupSendR9 != 0 &&
        g_vmClientHangupSendR9 >= VM_Memory_Pool_ADDRESS &&
        g_vmClientHangupSendR9 <
            VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        codeBase = g_vmClientHangupSendR9 - 0x14000u;
        if (entryOut)
            *entryOut = (codeBase + 0x8996u) | 1u;
        if (moduleR9Out)
            *moduleR9Out = g_vmClientHangupSendR9;
        return true;
    }
    if (vm_infer_battle_module_from_screen(vmAddedScreen, &codeBase, &moduleR9))
    {
        if (entryOut)
            *entryOut = (codeBase + 0x8996u) | 1u;
        if (moduleR9Out)
            *moduleR9Out = moduleR9;
        return true;
    }
    for (i = g_screenStackCount; i > 0; --i)
    {
        if (!vm_infer_battle_module_from_screen(g_screenStack[i - 1], &codeBase,
                                                &moduleR9))
            continue;
        if (g_screenStackModuleBase[i - 1] != 0)
            moduleR9 = g_screenStackModuleBase[i - 1];
        if (entryOut)
            *entryOut = (codeBase + 0x8996u) | 1u;
        if (moduleR9Out)
            *moduleR9Out = moduleR9;
        return true;
    }
    return false;
}

static bool vm_client_hangup_deliver_wt_to_8996(u32 connectId, u8 *packet,
                                                u32 packetLen, const char *tag)
{
    u32 entry = 0;
    u32 moduleR9 = 0;
    vm_net_channel *channel;
    uc_err fireErr;

    if (!vm_client_find_mmbattle_8996(&entry, &moduleR9))
        return false;
    channel = scheduler_find_net_channel(connectId);
    g_hangupNetCallEntry = entry;
    g_hangupNetCallR9 = moduleR9;
    if (!vm_client_deliver_wt_single(
            packet, packetLen, 7, entry,
            moduleR9 != 0 ? moduleR9 : (channel ? channel->context : 0),
            connectId, false, true))
    {
        g_hangupNetCallEntry = 0;
        g_hangupNetCallR9 = 0;
        return false;
    }
    /*
     * Fire immediately so Handle25_3/Handle25_2 run before Type=2 replay
     * or the next guest tick.  Module R9 is forced in net_fire.
     */
    fireErr = scheduler_dispatch_net_tasks();
    printf("[info][network] %s connect=%u entry=%08x r9=%08x len=%u "
           "parser=mmBattle:0x8996 fire_err=%u\n",
           tag ? tag : "cbe_hangup_8996", connectId, entry, moduleR9,
           packetLen, (unsigned)fireErr);
    vm_client_hangup_log_mode_flag(tag ? tag : "after-8996");
    if (fireErr != UC_ERR_OK)
    {
        g_hangupNetCallEntry = 0;
        g_hangupNetCallR9 = 0;
        printf("[warn][network] %s 8996_fire_err=%u fallback=channel\n",
               tag ? tag : "cbe_hangup_8996", (unsigned)fireErr);
        return false;
    }
    return true;
}

static bool vm_client_hangup_deliver_local_panel(u32 connectId)
{
    u8 packet[320];
    u32 packetLen = vm_client_build_hangup_panel_wt(packet, sizeof(packet));

    if (packetLen == 0)
        return false;
    /*
     * Do NOT cold-call mmBattle:0x8996 here.  client_out 2026-10-05:
     * entry=050276f7 r9=05032d60 fire_err=11 → UC_ERR_MAP / SCR_Event.
     * game at r9+8272 is non-writable (b5f8bd70).  Channel shows the local
     * 25/3 confirm shell; native game+1136 remains unresolved without a
     * real guest 25/2 wait after sub_28A4.
     */
    g_vmClientHangupPanelVia8996 = 0;
    if (!vm_client_deliver_local_wt_buf(connectId, packet, packetLen,
                                        "cbe_hangup_panel_open"))
        return false;
    g_vmClientHangupNativeMode = 1;
    printf("[info][network] cbe_hangup_panel_open connect=%u mode=1 "
           "via=channel evidence=no-cold-8996\n",
           connectId);
    return true;
}

static bool vm_client_hangup_deliver_local_mode25_2(u32 connectId, u8 type,
                                                    bool compoundStop)
{
    u8 packet[128];
    u32 packetLen = 0;

    if (!compoundStop)
    {
        packetLen = vm_client_build_hangup_mode25_2_wt(packet, sizeof(packet),
                                                       type);
    }
    else
    {
        u32 pos = 5;
        u32 objectStart = 0;
        u8 objectCount = 0;
        static const u8 kEmpty = 0;
        static const u8 kOthernum0[] = {0x00, 0x04, 0x00, 0x00, 0x00, 0x00};

        memset(packet, 0, 5);
        if (!vm_client_begin_local_wt_object(packet, sizeof(packet), &pos, 1, 2,
                                             10, &objectStart) ||
            !vm_client_put_object_field(packet, sizeof(packet), &pos, "othernum",
                                        kOthernum0, (u16)sizeof(kOthernum0)) ||
            !vm_client_put_object_field(packet, sizeof(packet), &pos, "otherinfo",
                                        &kEmpty, 0))
            return false;
        vm_client_finish_local_wt_object(packet, objectStart, pos);
        ++objectCount;
        if (!vm_client_begin_local_wt_object(packet, sizeof(packet), &pos, 1, 25,
                                             2, &objectStart) ||
            !vm_client_put_object_u8_field(packet, sizeof(packet), &pos,
                                           "result", 1) ||
            !vm_client_put_object_u8_field(packet, sizeof(packet), &pos, "type",
                                           type))
            return false;
        vm_client_finish_local_wt_object(packet, objectStart, pos);
        ++objectCount;
        vm_client_finish_wt_packet(packet, pos, objectCount);
        packetLen = pos;
    }
    if (packetLen == 0)
        return false;
    /*
     * Channel ack only — does not reach Handle25_2 / game+1136.  Cold 8996
     * for type=1 also UC_ERR_MAP (same session as panel).  Keep host path
     * crash-free; settle shell stays manual until a real 25/2 wait exists.
     */
    if (!vm_client_deliver_local_wt_buf(connectId, packet, packetLen,
                                        "cbe_hangup_mode25_2"))
        return false;
    printf("[info][network] cbe_hangup_mode25_2 connect=%u type=%u "
           "compound_stop=%u via=channel evidence=no-cold-8996\n",
           connectId, type, compoundStop ? 1u : 0u);
    return true;
}

/* mode==1: native 25/3 panel open; guest must receive 确定 for 28A4. */
static int vm_client_hangup_confirm_pending(void)
{
    return g_vmClientHangupNativeMode == 1;
}

static void vm_client_hangup_arm_skip_guest_confirm(u32 holdMs)
{
    u32 now = SDL_GetTicks();

    if (holdMs < 500u)
        holdMs = 500u;
    g_vmHangupSkipGuestConfirmUntilMs = now + holdMs;
}

/* Swallow leftover 确定 after host hangup start (no native 25/3 on map). */
static int vm_client_hangup_suppress_guest_confirm(void)
{
    u32 now;

    if (g_vmClientHangupNativeMode == 1)
        return 1;
    if (g_vmHangupSkipGuestConfirmUntilMs == 0)
        return 0;
    now = SDL_GetTicks();
    if (now < g_vmHangupSkipGuestConfirmUntilMs)
        return 1;
    g_vmHangupSkipGuestConfirmUntilMs = 0;
    return 0;
}

static void vm_client_hangup_log_mode_flag(const char *reason)
{
    u32 codeBase = 0;
    u32 moduleR9 = 0;
    u32 game = 0;
    u8 flag = 0xff;
    u32 si;
    int writable = 0;

    if (g_hangupNetCallR9 != 0)
        moduleR9 = g_hangupNetCallR9;
    else if (g_vmClientHangupSendR9 != 0)
        moduleR9 = g_vmClientHangupSendR9;
    else if (!vm_infer_battle_module_from_screen(vmAddedScreen, &codeBase,
                                                 &moduleR9))
    {
        for (si = g_screenStackCount; si > 0; --si)
        {
            if (!vm_infer_battle_module_from_screen(g_screenStack[si - 1],
                                                    &codeBase, &moduleR9))
                continue;
            if (g_screenStackModuleBase[si - 1] != 0)
                moduleR9 = g_screenStackModuleBase[si - 1];
            break;
        }
    }
    if (moduleR9 != 0 &&
        uc_mem_read(MTK, moduleR9 + 8272u, &game, sizeof(game)) == UC_ERR_OK &&
        game != 0 && vm_is_writable_vm_range(game, 1137u))
    {
        writable = 1;
        (void)uc_mem_read(MTK, game + 1136u, &flag, sizeof(flag));
    }
    printf("[info][network] cbe_hangup_mode_flag reason=%s r9=%08x game=%08x "
           "writable=%d flag1136=%u send_r9=%08x net_r9=%08x "
           "evidence=read-only\n",
           reason ? reason : "-", moduleR9, game, writable,
           writable ? (unsigned)flag : 0xffu, g_vmClientHangupSendR9,
           g_hangupNetCallR9);
}

static void vm_client_hangup_arm_settle_after_mode(const char *reason)
{
    if (vm_client_hangup_settle_ui_active())
        return;
    vm_client_hangup_settle_ui_arm((u16)VM_CLIENT_HANGUP_SETTLE_DEFAULT_MAX,
                                   reason);
    g_vmClientHangupSettleUi.battleToken = 1;
    g_vmClientHangupSettleUi.battlePreExp =
        g_vmClientHangupSettleUi.lastRoleExp;
    g_vmClientHangupSettleUi.battlePreGold =
        g_vmClientHangupSettleUi.lastRoleGold;
}

/*
 * Guest completed A4D4→28A4 and uplinked standalone 25/2(type=1).  Channel-ack
 * that wait (0x87DA→0x8996) so Handle25_2 sets game+1136, then replay Type=2
 * so 19090 still starts fights.  Must not host-call 0x8996 directly.
 */
static int vm_client_hangup_ack_guest_25_2(u32 connectId)
{
    u32 pendingLen = g_vmClientHangupPendingStartLen;
    u8 pending[VM_CLIENT_REQUEST_MAX];

    if (connectId == 0)
        connectId = g_vmClientHangupPendingConnectId;
    if (connectId == 0)
    {
        printf("[warn][network] cbe_hangup_ack_guest_25_2 no connect\n");
        return 0;
    }
    vm_client_hangup_arm_skip_guest_confirm(2500u);
    if (g_vmClientHangupNativeMode == 2)
    {
        printf("[info][network] cbe_hangup_ack_guest_25_2 already-mode2 "
               "connect=%u evidence=idempotent-skip-28A4\n",
               connectId);
        return 1;
    }
    if (g_vmClientHangupNativeMode != 1 && g_vmClientHangupNativeMode != 2)
    {
        /* Allow ack if panel was shown (mode 1) or already armed. */
        if (g_vmClientHangupPendingStartLen == 0 &&
            g_vmClientHangupNativeMode == 0)
        {
            printf("[warn][network] cbe_hangup_ack_guest_25_2 mode=%u "
                   "no-pending\n",
                   g_vmClientHangupNativeMode);
            return 0;
        }
    }
    vm_client_hangup_log_mode_flag("before-guest-25_2-ack");
    /*
     * Do not run guest 28A4 (R0=0 / Global R9 → UC_ERR_MAP) and do not
     * cold-call 8996.  Forward standalone 25/2 so the server arms
     * sceneHangupEnabled, then replay Type=2 for 19090 battles.
     */
    {
        u8 req25[128];
        u32 reqLen = vm_client_build_hangup_mode25_2_wt(req25, sizeof(req25), 1);

        if (reqLen == 0 ||
            !vm_client_enqueue(VM_CLIENT_JOB_DATA, connectId, req25, reqLen))
        {
            printf("[warn][network] cbe_hangup_ack_guest_25_2 enqueue_fail "
                   "connect=%u\n",
                   connectId);
            return 0;
        }
        g_netUpLinkData += reqLen;
        printf("[info][network] cbe_hangup_uplink_25_2 connect=%u len=%u "
               "evidence=host-confirm-skip-28A4\n",
               connectId, reqLen);
    }
    g_vmClientHangupNativeMode = 2;
    vm_client_hangup_arm_settle_after_mode("hangup-guest-25/2-ack");
    vm_client_hangup_log_mode_flag("after-guest-25_2-ack-queued");
    if (pendingLen != 0)
    {
        memcpy(pending, g_vmClientHangupPendingStart, pendingLen);
        pendingLen = vm_client_hangup_bind_live_combat(pending, pendingLen,
                                                       sizeof(pending));
        g_vmClientHangupPendingStartLen = 0;
        g_vmClientHangupPendingConnectId = 0;
        printf("[info][network] cbe_hangup_replay_start connect=%u len=%u "
               "after=guest-25/2-ack\n",
               connectId, pendingLen);
        if (!vm_client_enqueue(VM_CLIENT_JOB_DATA, connectId, pending,
                               pendingLen))
        {
            printf("[warn][network] cbe_hangup_replay_start_fail connect=%u "
                   "len=%u\n",
                   connectId, pendingLen);
        }
        else
        {
            g_netUpLinkData += pendingLen;
        }
    }
    printf("[info][network] cbe_hangup_ack_guest_25_2 connect=%u mode=2 "
           "evidence=8996-25/2+Type2-replay\n",
           connectId);
    vm_hangup_candidate_fault_disarm();
    return 1;
}

static int vm_client_hangup_host_cancel(void)
{
    u32 connectId = g_vmClientHangupPendingConnectId;

    if (g_vmClientHangupNativeMode != 1)
        return 0;
    if (connectId != 0)
        (void)vm_client_deliver_local_hangup_dismiss(connectId);
    g_vmClientHangupPendingStartLen = 0;
    g_vmClientHangupPendingConnectId = 0;
    g_vmClientHangupNativeMode = 0;
    vm_client_hangup_arm_skip_guest_confirm(800u);
    vm_client_hangup_settle_ui_clear("hangup-host-cancel");
    printf("[info][network] cbe_hangup_host_cancel connect=%u\n", connectId);
    vm_hangup_candidate_fault_disarm();
    return 1;
}

static void vm_client_log_uplink_objects(const u8 *req, u32 len)
{
    u32 pos = 4;
    char list[160];
    u32 used = 0;
    u32 n = 0;

    list[0] = 0;
    if (req == NULL || len < 4 || req[0] != 'W' || req[1] != 'T')
        return;
    while (pos + 5 <= len && n < 12)
    {
        u16 objectLen = (u16)(((u16)req[pos + 3] << 8) | req[pos + 4]);
        int wrote;

        if (objectLen < 5 || pos + objectLen > len)
            break;
        wrote = snprintf(list + used, sizeof(list) - used, "%s%u/%u/%u",
                         used ? "," : "", req[pos], req[pos + 1],
                         req[pos + 2]);
        if (wrote < 0 || (u32)wrote >= sizeof(list) - used)
            break;
        used += (u32)wrote;
        pos += objectLen;
        ++n;
    }
    printf("[info][network] cbe_uplink len=%u hdr=%u/%u/%u objects=%s\n",
           len, (unsigned)req[4], (unsigned)req[5], (unsigned)req[6],
           list[0] ? list : "-");
}

static void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
{
    u8 request[VM_CLIENT_REQUEST_MAX];
    u32 readLen;
    if (dataPtr == 0 || dataLen == 0)
        return;
    readLen = dataLen < sizeof(request) ? dataLen : sizeof(request);
    if (uc_mem_read(MTK, dataPtr, request, readLen) != UC_ERR_OK)
        return;
    vm_client_note_title_role_select(request, readLen);
#ifdef CBE_PLATFORM_ANDROID
    if (vm_client_is_android_no_account_login(request, readLen))
    {
        /*
         * No saved login record: 「开始游戏」 builds empty WT 1/1/12.
         * Do not forward. Tip「请先登录游戏」and locally ack with an empty
         * WT so the title progress bar / net wait is dismissed.
         */
        vm_android_request_need_login_tip("no_account_start");
        (void)vm_client_deliver_local_empty_wt(connectId);
        return;
    }
#endif
#if defined(CBE_CLIENT_ONLY)
    vm_client_log_uplink_objects(request, readLen);
    vm_client_hangup_note_operate_uplink(request, readLen);
    if (vm_client_request_is_cbe_hangup_start(request, readLen))
    {
        if (vm_host_should_block_hangup_uplink())
        {
            printf("[info][network] swallow_hangup_uplink len=%u "
                   "evidence=bag-equip-detail-clickthrough "
                   "ack=2/10+25/12\n",
                   readLen);
            (void)vm_client_deliver_local_hangup_dismiss(connectId);
            return;
        }
        /*
         * Already hanging: map/world 二次点击 is NOT stop.  Native stop is
         * hangup-settle 停止挂机 → 2/10+25/2(type=2).
         */
        if (g_vmClientHangupNativeMode == 2)
        {
            printf("[info][network] cbe_hangup_uplink Type=2 len=%u "
                   "forwarded=0 action=map-tap-ignored "
                   "evidence=stop-is-settle-button-25/2-type2\n",
                   readLen);
            (void)vm_client_deliver_local_hangup_dismiss(connectId);
            return;
        }
        if (g_vmClientHangupStopHoldUntilMs != 0 &&
            SDL_GetTicks() < g_vmClientHangupStopHoldUntilMs)
        {
            printf("[info][network] cbe_hangup_uplink Type=2 len=%u "
                   "forwarded=0 action=swallow-after-settle-stop "
                   "evidence=map-type2-after-25/2-type2\n",
                   readLen);
            (void)vm_client_deliver_local_hangup_dismiss(connectId);
            return;
        }
        /*
         * Do not bind mmBattle R9 or forward 25/3 here.  client_out 2026-10-05:
         * hangup_confirm_bind_r9 on the outdoor map rewrote
         * g_currentScreenModuleBase; next mmGame ScreenLogic (pc=502da7a)
         * ran with this=6a8e3180 → UC_ERR_MAP / SCR_Event.  Outdoor module
         * is mmGame, not mmBattle:0x8996.
         * Keep the crash-free start: dismiss wait, host 25/2 to arm
         * sceneHangupEnabled, replay Type=2 for hangup_battle_start.
         */
        printf("[info][network] cbe_hangup_uplink Type=2 len=%u forwarded=0 "
               "evidence=map-send-pick-skill-then-start\n",
               readLen);
        vm_client_hangup_store_pending_start(connectId, request, readLen);
        g_vmClientHangupNativeMode = 1;
        g_vmClientHangupAutoAckAfterPanel = 0;
        g_vmClientHangupPanelVia8996 = 0;
        vm_client_hangup_arm_settle_after_mode("hangup-uplink-2/10");
        (void)vm_client_deliver_local_hangup_dismiss(connectId);
        if (!vm_host_hangup_skill_menu_open())
        {
            printf("[info][network] cbe_hangup_skill_menu_skip "
                   "action=start-na\n");
            (void)vm_client_hangup_ack_guest_25_2(connectId);
        }
        return;
    }
    if (vm_client_request_is_empty_scene_default_25_5(request, readLen) &&
        g_vmClientHangupOnSettleShell != 0)
    {
        /*
         * mmGame scene_runtime_tick still emits empty 25/5 on the 4/7
         * hangup shell.  Empty WT / 25/5 result=4 both DIV0 at 0x0104E980.
         * Do not complete the wait until map-side 4/8 (no 4/5).
         */
        g_vmClientHangupOweSceneDefaultAck = 1;
        g_vmClientHangupOweSceneDefaultConnect = connectId;
        printf("[info][network] cbe_hangup_swallow_25_5 len=%u connect=%u "
               "evidence=div0-0x0104e980-Handle25-on-4/7-shell\n",
               readLen, connectId);
        return;
    }
    /*
     * After hangup 4/8+4/10 reenter, mmGame still emits map 4/1.  Forwarding
     * it stacked a second battle (resp=231) on the 4/10 shell → font +0xc=0
     * UC_ERR_MAP pc=0x01004E36.  CBE hangup next fight is poll 4/10, not 4/1.
     */
    if (g_vmClientHangupNativeMode == 2 &&
        vm_client_request_is_map_battle_enter_4_1(request, readLen))
    {
        printf("[info][network] cbe_hangup_swallow_4_1 len=%u connect=%u "
               "evidence=hangup-owns-encounter-poll-4/10\n",
               readLen, connectId);
        (void)vm_client_deliver_local_empty_wt(connectId);
        return;
    }
    {
        u8 modeType = 0;
        bool compoundStop = false;

        if (vm_client_request_parse_hangup_mode25_2(request, readLen, &modeType,
                                                    &compoundStop) &&
            modeType == 2)
        {
            printf("[info][network] cbe_hangup_uplink stop len=%u "
                   "compound=%u evidence=settle-stop-button-25/2-type2\n",
                   readLen, compoundStop ? 1u : 0u);
            g_vmClientHangupNativeMode = 0;
            g_vmClientHangupPendingStartLen = 0;
            g_vmClientHangupPendingConnectId = 0;
            g_vmClientHangupPanelVia8996 = 0;
            vm_client_hangup_settle_ui_clear("native-settle-stop");
            g_vmClientHangupStopHoldUntilMs = SDL_GetTicks() + 4000u;
        }
    }
#endif
    /*
     * 江湖商城: truncate mmShop 14/* / 1/1/14 uplink (do not forward), then
     * host simulates 右软返回 and poll-delivers coolmall 26/1.
     */
    if (vm_host_coolmall_try_truncate_mall_send(request, readLen))
        return;
#if defined(CBE_CLIENT_ONLY)
    vm_host_dbg_note_open14_uplink(request, readLen);
#endif
    if (!vm_client_enqueue(VM_CLIENT_JOB_DATA, connectId, request, readLen))
    {
        printf("[warn][network] client queue full/reject connect=%u len=%u "
               "data_q=%u poll_q=%u\n",
               connectId, readLen,
               g_vmClientAsync.queuedDataJobs, g_vmClientAsync.queuedPollJobs);
        return;
    }
    g_netUpLinkData += dataLen;
}

static void vm_net_mock_poll_push_if_due(void)
{
    static u32 lastPollTick = 0;
    static u32 lastPollWallMs = 0;
    vm_net_channel *channel = NULL;
    u32 nowMs;
    u32 minIntervalMs = (u32)VM_CLIENT_POLL_MIN_INTERVAL_MS;
    bool urgentActive = false;

    if (g_mockServiceClientId == 0 || Global_R9 == 0)
        return;
    nowMs = SDL_GetTicks();
    if (g_vm_scene_poll_urgent_until_ms != 0)
    {
        if (nowMs >= g_vm_scene_poll_urgent_until_ms ||
            g_vm_scene_poll_urgent_remaining == 0)
        {
            g_vm_scene_poll_urgent_ready_at_ms = 0;
            g_vm_scene_poll_urgent_until_ms = 0;
            g_vm_scene_poll_urgent_remaining = 0;
        }
        else if (nowMs >= g_vm_scene_poll_urgent_ready_at_ms)
        {
            urgentActive = true;
            minIntervalMs = (u32)VM_CLIENT_POLL_URGENT_INTERVAL_MS;
        }
    }
    if (lastPollTick != 0 && g_schedulerTick - lastPollTick < 1)
        return;
    if (lastPollWallMs != 0 && nowMs - lastPollWallMs < minIntervalMs)
        return;
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netChannels[i].active && g_netChannels[i].callback != 0)
        {
            channel = &g_netChannels[i];
            break;
        }
    }
    if (channel == NULL ||
        scheduler_find_pending_net_event(7, channel->callback, channel->context) != NULL)
        return;
    if (vm_client_enqueue(VM_CLIENT_JOB_SCENE_POLL, channel->connectId, NULL, 0))
    {
        lastPollTick = g_schedulerTick;
        lastPollWallMs = nowMs;
        if (urgentActive && g_vm_scene_poll_urgent_remaining > 0)
            --g_vm_scene_poll_urgent_remaining;
    }
}

static u32 vm_net_mock_read_data(u32 dst, u32 dstLen)
{
    u32 remain;
    u32 copyLen;
    if (dst == 0 || dstLen == 0 || g_netMockResponseOffset >= g_netMockResponseLen)
        return vm_set_call_result(0);
    remain = g_netMockResponseLen - g_netMockResponseOffset;
    copyLen = dstLen < remain ? dstLen : remain;
    uc_mem_write(MTK, dst, g_netMockResponse + g_netMockResponseOffset, copyLen);
    g_netMockResponseOffset += copyLen;
    return vm_set_call_result(copyLen);
}
