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

enum
{
    VM_CLIENT_FRAME_SIZE = 20,
    VM_CLIENT_REQUEST_FLAG_SCENE_SYNC_POLL = 0x2u,
    VM_CLIENT_REQUEST_FLAG_DISCONNECT = 0x4u,
    VM_CLIENT_RESPONSE_FLAG_CLOSE_AFTER_DATA = 0x1u,
    /* Primary CBMR is followed by a second CBMR body on the same TCP reply. */
    VM_CLIENT_RESPONSE_FLAG_HAS_FOLLOWUP = 0x2u,
    /* Applied to send/recv after the TCP handshake completes. */
    VM_CLIENT_SOCKET_TIMEOUT_MS = 12000,
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
     * 3s cuts short-TCP load; server presence timeout is still ~30s. */
    VM_CLIENT_POLL_MIN_INTERVAL_MS = 3000,
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
         * Outdoor map-stone: one 27/12+posinfo reenter after deferred 30/1.
         * Dream 29*: never allow-once; block completion 01018150 instead
         * (instance DF enter leaves same-reenter guard unarmed).
         */
        {
            bool isDream29 = target.scene[0] == '2' && target.scene[1] == '9';

            g_vm_scene_allow_one_map_stone_completion_reenter =
                observation->sceneSubtype == 1 && !isDream29;
            g_vm_scene_block_dream_completion_reenter =
                observation->sceneSubtype == 1 && isDream29;
        }
        printf("[info][screen] remote_scene_target_apply serial=%u subtype=%u "
               "scene=%s pos=(%u,%u) allow_map_stone_reenter=%u "
               "block_dream_reenter=%u "
               "evidence=WT30/%u-immediately-before-guest-callback\n",
               g_vm_net_mock_last_scene_change_target_serial,
               observation->sceneSubtype, target.scene, target.x, target.y,
               g_vm_scene_allow_one_map_stone_completion_reenter ? 1u : 0u,
               g_vm_scene_block_dream_completion_reenter ? 1u : 0u,
               observation->sceneSubtype);
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
        if (wasDream29)
            g_vm_scene_block_dream_completion_reenter = false;
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
        u32 responsePtr;
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
        responsePtr = vm_net_mock_sync_buffer_to_vm(completion->response,
                                                    completion->responseLen);
        if (responsePtr == 0)
        {
            vm_client_free_completion(completion);
            continue;
        }
        if (completion->responseLen <= sizeof(g_netMockResponse))
        {
            memcpy(g_netMockResponse, completion->response, completion->responseLen);
            g_netMockResponseLen = completion->responseLen;
            g_netMockResponseOffset = 0;
        }
        g_netDownLinkData += completion->responseLen;
        scheduler_queue_net_event(completion->eventType, responsePtr,
                                  completion->responseLen, completion->responseLen,
                                  channel->callback, channel->context);
        if (completion->eventType == 7)
        {
            vm_net_remote_observation observation;

            vm_client_capture_remote_observation(completion->response,
                                                 completion->responseLen,
                                                 &observation);
            if ((observation.hasSceneTarget ||
                 observation.sceneCompleteAfterCallback ||
                 observation.updateComplete) &&
                !scheduler_attach_net_remote_observation(
                    completion->eventType, responsePtr, channel->callback,
                    channel->context, &observation))
            {
                printf("[warn][screen] remote_observation_attach_failed "
                       "connect=%u event=%u resp=%u action=no-early-global-mutation\n",
                       completion->connectId, completion->eventType,
                       completion->responseLen);
            }
        }
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
            u32 followupPtr = vm_net_mock_sync_buffer_to_vm(completion->followup,
                                                            completion->followupLen);
            if (followupPtr != 0)
            {
                scheduler_queue_net_event(7, followupPtr, completion->followupLen,
                                          completion->followupLen,
                                          channel->callback, channel->context);
                printf("[info][network] queue_data_followup connect=%u event=7 resp=%u\n",
                       completion->connectId, completion->followupLen);
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
