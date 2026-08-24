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
     * 2s balances sync freshness vs short-TCP load; server presence timeout ~90s. */
    VM_CLIENT_POLL_MIN_INTERVAL_MS = 2000,
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

            g_vm_scene_allow_one_map_stone_completion_reenter =
                observation->sceneSubtype == 1 && !isDream29;
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
    u32 currentR9 = 0;
    (void)address;
    (void)size;
    (void)user_data;
    uc_reg_read(uc, UC_ARM_REG_R9, &currentR9);
    if (currentR9 >= VM_Memory_Pool_ADDRESS &&
        currentR9 < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        vm_dl_note_sp_bf(currentR9, "pool-exec");
    }
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
 * we cancel that path and ask Java to open ACCOUNT_WEB_URL instead.
 */
static volatile int g_androidAccountWebOpenSerial = 0;
static volatile int g_androidAccountWebOpenPending = 0;
static u32 g_androidAccountWebOpenLastMs = 0;
static volatile int g_androidRechargeBlockedTipSerial = 0;
static volatile int g_androidRechargeBlockedTipPending = 0;
static u32 g_androidRechargeBlockedTipLastMs = 0;

int cbeAndroidConsumeAccountWebOpenRequest(void)
{
    if (!g_androidAccountWebOpenPending)
        return 0;
    g_androidAccountWebOpenPending = 0;
    return g_androidAccountWebOpenSerial;
}

int cbeAndroidConsumeRechargeBlockedTip(void)
{
    if (!g_androidRechargeBlockedTipPending)
        return 0;
    g_androidRechargeBlockedTipPending = 0;
    return g_androidRechargeBlockedTipSerial;
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
    return object != NULL && object->major == 1 && object->kind == 31;
}

static bool vm_client_strip_cbe_unsupported_objects(u8 **packet, u32 *packetLen);

static bool vm_client_cbe_clamp_backpack_packet(u8 **response, u32 *responseLen);

static bool vm_client_cbe_patch_actorinfo_packet(u8 **packet, u32 *packetLen);

enum
{
    VM_CLIENT_CBE_BACKPACK_SLOTS = 64
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
 * CBE InitTimerControl(64) only owns 64 logical bag slots.  Live server may
 * publish Pixi-side capacity/rows up to 256.  Clamp downlink 17/1 + 30/21 to
 * the first 64 client-grid rows before the guest parser runs; leave server
 * storage untouched (docs/re/2026-08-20-cbe-emu-backpack-64-clamp.md).
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
     * mmGame:0x418C needs tagged u8 row count.  Keep live row bodies (834 etc.
     * are valid list rows); only drop 808/817-819 per CBE_EMU client_grid_item.
     */
    if (!vm_client_seq_write_row_count(out, outCap, &outPos, 0, 1))
        return false;
    countValuePos = outPos - 1u;
    if (countTagToU8Out != NULL)
        *countTagToU8Out = (tagSize != 1);
    for (i = 0; i < count; ++i)
    {
        u32 rowStart = pos;
        u32 rowLen = 0;
        u32 itemId = 0;

        if (!vm_client_seq_read_tagged_u32(in, inLen, &pos, &itemId))
            return false;
        if (!vm_client_seq_skip_common_extra(in, inLen, &pos))
            return false;
        rowLen = pos - rowStart;
        if (!vm_client_cbe_item_is_client_grid_item(itemId))
            continue;
        if (keep >= maxRows)
            break;
        if (outPos + rowLen > outCap)
            return false;
        memcpy(out + outPos, in + rowStart, rowLen);
        outPos += rowLen;
        ++keep;
    }
    out[countValuePos] = (u8)keep;
    *outLen = (u16)outPos;
    if (keptRows != NULL)
        *keptRows = keep;
    if (truncatedOut != NULL)
        *truncatedOut = (count > keep || tagSize != 1);
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
    if (itemId == 802 || itemId == 803)
        return 1;
    if (itemId >= 1000u)
        return 1;
    /* Cap ordinary stacks so stack_limit==0 ids cannot blow the 74-record pool. */
    if (count > 1u)
        return 1;
    (void)itemId;
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
        if (vm_client_cbe_patch_common_extra_stack1(out + extraOutPos, extraLen))
            countFixed = true;
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
        newIteminfo = (u8 *)malloc(2048u);
        if (newIteminfo == NULL)
            return false;
        if (!vm_client_truncate_backpack17_iteminfo(
                iteminfo, iteminfoLen, newIteminfo, 2048u, &newIteminfoLen,
                (u16)VM_CLIENT_CBE_BACKPACK_SLOTS, &keptRows, &truncated,
                &countTagToU8))
        {
            free(newIteminfo);
            return false;
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
               "iteminfo_len=%u mode=live-rows "
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
        bool countFixed = false;
        u8 emptyIteminfo[1];
        bool gridSeeded = false;

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
        newIteminfo = (u8 *)malloc(2048u);
        if (newIteminfo != NULL &&
            vm_client_build_backpack30_grid_compact(
                iteminfo, iteminfoLen, newIteminfo, 2048u, &newIteminfoLen,
                (u16)VM_CLIENT_CBE_BACKPACK_SLOTS, &keptRows, &droppedRows) &&
            keptRows > 0)
        {
            gridSeeded = true;
            if (!vm_client_encode_tagged_u16(newGridnum, sizeof(newGridnum),
                                             &newGridnumLen, keptRows, 1))
            {
                free(newIteminfo);
                return false;
            }
        }
        else
        {
            if (newIteminfo != NULL)
            {
                (void)vm_client_sanitize_backpack30_iteminfo(
                    iteminfo, iteminfoLen, newIteminfo, 2048u,
                    &newIteminfoLen, (u16)VM_CLIENT_CBE_BACKPACK_SLOTS,
                    &keptRows, &droppedRows, &countFixed);
                free(newIteminfo);
                newIteminfo = NULL;
            }
            if (!vm_client_encode_tagged_u16(newGridnum, sizeof(newGridnum),
                                             &newGridnumLen, 0, 1))
                return false;
            emptyIteminfo[0] = 0;
            newIteminfoLen = 0;
            newIteminfo = emptyIteminfo;
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
            if (gridSeeded && newIteminfo != emptyIteminfo)
                free(newIteminfo);
            return false;
        }
        if (gridSeeded && newIteminfo != emptyIteminfo)
            free(newIteminfo);
        outObj[objectStart + 4] = (u8)(pos >> 8);
        outObj[objectStart + 5] = (u8)pos;
        *outLen = pos;
        if (changedOut != NULL)
            *changedOut = true;
        printf("[info][network] cbe_backpack_clamp object=30/21 gridnum=%u "
               "rows_kept=%u dropped=%u iteminfo_len=%u "
               "mode=%s evidence=CBE_EMU:30/21-compact-seed+HandleItemGrid\n",
               gridSeeded ? keptRows : 0u, keptRows, droppedRows,
               newIteminfoLen,
               gridSeeded ? "compact-grid-seed" : "empty-grid");
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

    /* 30/21 compact-grid-seed + 17/1 live-rows via rewrite. */
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
            if (object.major == 1 && object.kind == 17 && object.subtype == 1)
            {
                /* Always rewrite live 17/1 rows for mmGame:0x418C. */
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
        u32 rewrittenLen = 0;
        bool changed = false;
        const u8 *src = packet + (offset - (object.payloadLen + 6u));
        u32 srcLen = (u32)object.payloadLen + 6u;

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

static void vm_client_prepare_cbe_downlink(u8 **packet, u32 *packetLen)
{
    u32 offset = 5;
    vm_client_response_object object;
    u8 bag17 = 0;
    u8 bag30 = 0;
    u8 kinds[12];
    u8 subtypes[12];
    u8 n = 0;

    if (packet == NULL || packetLen == NULL)
        return;
    (void)vm_client_cbe_patch_actorinfo_packet(packet, packetLen);
    (void)vm_client_strip_cbe_unsupported_objects(packet, packetLen);
    (void)vm_client_cbe_clamp_backpack_packet(packet, packetLen);

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
        if (object.major == 1 && object.kind == 17 && object.subtype == 1)
            ++bag17;
        if (object.major == 1 && object.kind == 30 && object.subtype == 21)
            ++bag30;
    }
    if (bag17 != 0 || bag30 != 0)
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
        printf("[info][network] cbe_backpack_packet objects=%u has_17_1=%u "
               "has_30_21=%u list=%s resp=%u\n",
               n, bag17, bag30, list, *packetLen);
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
         * Product path opens ACCOUNT_WEB (:19091) instead of auto-guest.
         * Do not probe game-login codeVersion here — account-center register /
         * login must remain usable even when the APK is behind the game gate.
         * Saved-credential 「开始游戏」 still hits WT login and the server gate.
         */
        vm_android_request_account_web_open("no_account_login");
        return;
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

    if (g_mockServiceClientId == 0 || Global_R9 == 0)
        return;
    nowMs = SDL_GetTicks();
    if (lastPollTick != 0 && g_schedulerTick - lastPollTick < 1)
        return;
    if (lastPollWallMs != 0 &&
        nowMs - lastPollWallMs < VM_CLIENT_POLL_MIN_INTERVAL_MS)
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
