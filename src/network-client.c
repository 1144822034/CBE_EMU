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
#include <netdb.h>
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
    VM_CLIENT_SOCKET_TIMEOUT_MS = 5000,
    VM_CLIENT_REQUEST_MAX = 512,
    VM_CLIENT_QUEUE_MAX = 64,
    VM_CLIENT_FOLLOWUP_MAX = 65536,
    VM_CLIENT_COMPLETED_SCENE_REUSE_TICKS = 120
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

/* Scene-target state is produced by the embedded response builders on the
 * desktop server.  A remote-only client never builds those responses. */
static vm_net_mock_scene_change_target g_vm_net_mock_last_scene_change_target;
static bool g_vm_net_mock_last_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_scene_change_target_serial = 0;
static vm_net_mock_scene_change_target
    g_vm_client_last_completed_scene_change_target;
static bool g_vm_client_last_completed_scene_change_target_valid = false;
static u32 g_vm_client_last_completed_scene_change_tick = 0;
static u32 g_vm_client_completed_scene_target_serial = 0;
static bool g_vm_client_update_completed_reenter_pending = false;
static char g_vm_client_update_completed_name[64];

static bool vm_net_mock_scene_names_equal_exact(const char *a, const char *b)
{
    return a != NULL && b != NULL && a[0] != 0 && strcmp(a, b) == 0;
}

static bool vm_net_mock_consume_update_completed_scene_reenter(
    const vm_net_mock_scene_change_target *target);
static u32 vm_net_mock_apply_remote_observation(
    const vm_net_remote_observation *observation);
static void vm_net_mock_finish_remote_observation(u32 sceneTargetSerial);

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
    (void)size;
    (void)user_data;
    /* The generic code hook is primarily a ROM dispatcher; dynamically
     * loaded CBM code has its own pool hook.  Keep this forensic observation
     * here so the post-callback BattleScene render transition is not missed. */
    vm_hangup_battle_render_trace_note_pc((u32)address & ~1u);
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
    const u8 *payload;
    u16 payloadLen;
} vm_client_wt_object;

static bool vm_client_next_wt_object(const u8 *packet, u32 packetLen,
                                     u32 *offset, vm_client_wt_object *object)
{
    u32 start;
    u16 objectLen;
    if (packet == NULL || offset == NULL || *offset < 5 || *offset + 6 > packetLen)
        return false;
    start = *offset;
    objectLen = (u16)(((u16)packet[start + 4] << 8) | packet[start + 5]);
    if (objectLen < 6 || start + objectLen > packetLen)
        return false;
    if (object != NULL)
    {
        object->major = packet[start];
        object->kind = packet[start + 1];
        object->subtype = packet[start + 2];
        object->payload = packet + start + 6;
        object->payloadLen = (u16)(objectLen - 6);
    }
    *offset = start + objectLen;
    return true;
}

static bool vm_client_wt_object_field(const vm_client_wt_object *object,
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

static bool vm_client_wt_object_u32(const vm_client_wt_object *object,
                                    const char *field, u32 *value)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;
    const u8 *bytes = NULL;

    if (!vm_client_wt_object_field(object, field, &encoded, &encodedLen))
        return false;
    if (encodedLen == 7 && encoded[0] == 6 && encoded[1] == 0 &&
        encoded[2] == 4)
    {
        bytes = encoded + 3;
    }
    else if (encodedLen == 5 && encoded[0] == 4)
    {
        bytes = encoded + 1;
    }
    else
    {
        return false;
    }
    if (value != NULL)
    {
        *value = ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) |
                 ((u32)bytes[2] << 8) | bytes[3];
    }
    return true;
}

static bool vm_client_wt_object_wrapped_bytes(
    const vm_client_wt_object *object, const char *field,
    const u8 **value, u16 *valueLen)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;
    u16 innerLen = 0;

    if (value != NULL)
        *value = NULL;
    if (valueLen != NULL)
        *valueLen = 0;
    if (!vm_client_wt_object_field(object, field, &encoded, &encodedLen) ||
        encodedLen < 2)
    {
        return false;
    }
    innerLen = (u16)(((u16)encoded[0] << 8) | encoded[1]);
    if ((u32)innerLen + 2u != encodedLen)
        return false;
    if (value != NULL)
        *value = encoded + 2;
    if (valueLen != NULL)
        *valueLen = innerLen;
    return true;
}

static bool vm_client_wt_object_string(const vm_client_wt_object *object,
                                       const char *field, char *value,
                                       size_t valueCap)
{
    const u8 *text = NULL;
    u16 textLen = 0;
    size_t copyLen = 0;

    if (value == NULL || valueCap == 0)
        return false;
    value[0] = 0;
    if (!vm_client_wt_object_wrapped_bytes(object, field, &text, &textLen))
        return false;
    copyLen = SDL_min((size_t)textLen, valueCap - 1);
    while (copyLen > 0 && text[copyLen - 1] == 0)
        --copyLen;
    memcpy(value, text, copyLen);
    value[copyLen] = 0;
    return value[0] != 0;
}

static bool vm_client_wt_object_posinfo(const vm_client_wt_object *object,
                                        u16 *x, u16 *y)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;

    if (!vm_client_wt_object_field(object, "posinfo", &encoded,
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

static void vm_client_snapshot_completed_scene_target(u32 serial)
{
    if (!g_vm_net_mock_last_scene_change_target_valid || serial == 0 ||
        serial != g_vm_net_mock_last_scene_change_target_serial)
    {
        return;
    }
    g_vm_client_last_completed_scene_change_target =
        g_vm_net_mock_last_scene_change_target;
    g_vm_client_last_completed_scene_change_target.needsSceneDownload = false;
    g_vm_client_last_completed_scene_change_target_valid = true;
    g_vm_client_last_completed_scene_change_tick = g_schedulerTick;
    g_vm_client_completed_scene_target_serial = serial;
}

static bool vm_net_mock_consume_update_completed_scene_reenter(
    const vm_net_mock_scene_change_target *target)
{
    bool matches = false;

    if (!g_vm_client_update_completed_reenter_pending)
        return false;
    matches = target != NULL && target->scene[0] != 0 &&
              g_vm_client_update_completed_name[0] != 0 &&
              vm_net_mock_scene_names_equal_exact(
                  target->scene, g_vm_client_update_completed_name);
    g_vm_client_update_completed_reenter_pending = false;
    if (!matches)
    {
        printf("[warn][screen] remote_update_reenter_rejected file=%s "
               "scene=%s reason=resource-target-mismatch\n",
               g_vm_client_update_completed_name,
               target ? target->scene : "");
        return false;
    }
    printf("[info][screen] screen_mgr allow-update-reenter scene=%s "
           "pos=(%u,%u) exit=%u file=%s source=remote-WT18/7\n",
           target->scene, target->x, target->y, target->exitId,
           g_vm_client_update_completed_name);
    vm_autotest_note("screen_mgr allow-update-reenter scene=%s pos=(%u,%u) "
                     "exit=%u file=%s source=remote-WT18/7\n",
                     target->scene, target->x, target->y, target->exitId,
                     g_vm_client_update_completed_name);
    return true;
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
        snprintf(target.scene, sizeof(target.scene), "%s",
                 observation->scene);
        target.x = observation->sceneX;
        target.y = observation->sceneY;
        target.mapType = 2;
        target.hasSceEntry = true;
        g_vm_client_update_completed_reenter_pending = false;
        g_vm_client_update_completed_name[0] = 0;
        g_vm_client_last_completed_scene_change_target_valid = false;
        g_vm_client_completed_scene_target_serial = 0;
        g_vm_net_mock_last_scene_change_target = target;
        g_vm_net_mock_last_scene_change_target_valid = true;
        ++g_vm_net_mock_last_scene_change_target_serial;
        if (g_vm_net_mock_last_scene_change_target_serial == 0)
            g_vm_net_mock_last_scene_change_target_serial = 1;
        printf("[info][screen] remote_scene_target_apply serial=%u subtype=%u "
               "scene=%s pos=(%u,%u) evidence=WT30/%u-before-callback\n",
               g_vm_net_mock_last_scene_change_target_serial,
               observation->sceneSubtype, target.scene, target.x, target.y,
               observation->sceneSubtype);
    }
    if (observation->sceneCompleteAfterCallback &&
        g_vm_net_mock_last_scene_change_target_valid &&
        (observation->scene[0] == 0 ||
         vm_net_mock_scene_names_equal_exact(
             observation->scene,
             g_vm_net_mock_last_scene_change_target.scene)))
    {
        clearAfterCallbackSerial =
            g_vm_net_mock_last_scene_change_target_serial;
        vm_client_snapshot_completed_scene_target(clearAfterCallbackSerial);
        printf("[info][screen] remote_scene_target_complete_pending serial=%u "
               "scene=%s action=clear-after-own-callback evidence=WT30/2\n",
               clearAfterCallbackSerial,
               g_vm_net_mock_last_scene_change_target.scene);
    }
    if (observation->updateComplete && observation->updateName[0] != 0)
    {
        if (!g_vm_net_mock_last_scene_change_target_valid &&
            g_vm_client_last_completed_scene_change_target_valid &&
            g_vm_client_completed_scene_target_serial != 0 &&
            g_schedulerTick - g_vm_client_last_completed_scene_change_tick <
                VM_CLIENT_COMPLETED_SCENE_REUSE_TICKS &&
            vm_net_mock_scene_names_equal_exact(
                g_vm_client_last_completed_scene_change_target.scene,
                observation->updateName))
        {
            g_vm_net_mock_last_scene_change_target =
                g_vm_client_last_completed_scene_change_target;
            g_vm_net_mock_last_scene_change_target_valid = true;
            g_vm_net_mock_last_scene_change_target_serial =
                g_vm_client_completed_scene_target_serial;
            g_vm_client_last_completed_scene_change_tick = g_schedulerTick;
            restoredCompletedTarget = true;
            printf("[info][screen] remote_scene_target_restore serial=%u "
                   "scene=%s file=%s reason=resource-completion-callback\n",
                   g_vm_net_mock_last_scene_change_target_serial,
                   g_vm_net_mock_last_scene_change_target.scene,
                   observation->updateName);
        }
        if (g_vm_net_mock_last_scene_change_target_valid &&
            vm_net_mock_scene_names_equal_exact(
                g_vm_net_mock_last_scene_change_target.scene,
                observation->updateName))
        {
            snprintf(g_vm_client_update_completed_name,
                     sizeof(g_vm_client_update_completed_name), "%s",
                     observation->updateName);
            g_vm_client_update_completed_reenter_pending = true;
            if (restoredCompletedTarget)
            {
                clearAfterCallbackSerial =
                    g_vm_net_mock_last_scene_change_target_serial;
            }
            printf("[info][screen] remote_update_complete_apply file=%s "
                   "serial=%u action=arm-one-scene-reenter "
                   "before-callback\n",
                   observation->updateName,
                   g_vm_net_mock_last_scene_change_target_serial);
        }
        else
        {
            printf("[warn][screen] remote_update_complete_unbound file=%s "
                   "action=no-scene-reenter reason=no-matching-recent-target\n",
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
           "action=cleared-after-own-callback\n",
           sceneTargetSerial,
           g_vm_net_mock_last_scene_change_target.scene);
    g_vm_net_mock_last_scene_change_target_valid = false;
}

static void vm_client_finish_wt_packet(u8 *packet, u32 len, u8 objectCount)
{
    packet[0] = 'W';
    packet[1] = 'T';
    packet[2] = (u8)(len >> 8);
    packet[3] = (u8)len;
    packet[4] = objectCount;
}

typedef enum
{
    VM_CLIENT_ITEM_FOLLOWUP_NONE = 0,
    VM_CLIENT_ITEM_FOLLOWUP_USE_LIST
} vm_client_item_followup_kind;

static bool vm_client_extract_item_followup(u8 *response, u32 *responseLen,
                                            u8 *followup, u32 followupCap,
                                            u32 *followupLen,
                                            vm_client_item_followup_kind *kindOut)
{
    u32 offset = 5;
    u32 primaryPos = 5;
    u32 followPos = 5;
    u8 primaryCount = 0;
    u8 followCount = 0;
    u8 seenCount = 0;
    bool haveItemUse = false;
    bool haveSilentCompletion = false;
    u8 itemListCount = 0;
    u8 itemGridCount = 0;
    vm_client_item_followup_kind followupKind =
        VM_CLIENT_ITEM_FOLLOWUP_NONE;
    vm_client_wt_object object;

    if (followupLen != NULL)
        *followupLen = 0;
    if (kindOut != NULL)
        *kindOut = VM_CLIENT_ITEM_FOLLOWUP_NONE;
    if (response == NULL || responseLen == NULL || *responseLen < 10 ||
        response[0] != 'W' || response[1] != 'T' ||
        followup == NULL || followupCap < 5)
        return false;

    while (offset + 5 <= *responseLen &&
           vm_client_next_wt_object(response, *responseLen, &offset, &object))
    {
        /* The legacy acknowledgement is 7/1, while the silent completion
         * contract used by ordinary recovery items is 7/4.  Both must be
         * delivered before the full 17/1 list: the CBE operation handler
         * clears its pending-use state in that acknowledgement branch, and
         * the backpack screen owns the list replacement callback. */
        if (object.major == 1 && object.kind == 7 &&
            (object.subtype == 1 || object.subtype == 4))
        {
            haveItemUse = true;
            if (object.subtype == 4)
                haveSilentCompletion = true;
        }
        if (object.major == 1 && object.kind == 17 && object.subtype == 1)
            ++itemListCount;
        if (object.major == 1 && object.kind == 30 && object.subtype == 21)
            ++itemGridCount;
        ++seenCount;
    }
    if (offset != *responseLen || seenCount != response[4])
        return false;

    /* Existing item-use flow: the business acknowledgement must complete
     * before its full backpack-list follow-up reaches the list owner.  The
     * silent 7/4 completion is a stronger boundary: unlike the legacy 7/1
     * response, every object after it belongs to the same refresh transaction
     * and must retain its wire order (30/21 -> 7/42 -> 7/11 -> 7/37). */
    if (haveItemUse && (itemListCount != 0 || itemGridCount != 0))
    {
        followupKind = VM_CLIENT_ITEM_FOLLOWUP_USE_LIST;
    }
    else
    {
        return false;
    }

    offset = 5;
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
        bool isCompletion = object.major == 1 && object.kind == 7 &&
                            object.subtype == 4;
        bool isFollowup = false;

        if (followupKind == VM_CLIENT_ITEM_FOLLOWUP_USE_LIST)
        {
            if (haveSilentCompletion)
            {
                /* Remote transport delivers the primary frame first.  Keep
                 * only the silent completion in that frame; moving the
                 * selected-row 7/11 ahead of 30/21 was the source of the
                 * stale/single-stack quantity (20 -> 9) seen remotely. */
                isFollowup = !isCompletion;
            }
            else
            {
                isFollowup =
                    (object.major == 1 && object.kind == 17 &&
                     object.subtype == 1) ||
                    (object.major == 1 && object.kind == 30 &&
                     object.subtype == 21);
            }
        }
        if (isFollowup)
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
    if (kindOut != NULL)
        *kindOut = followupKind;
    return true;
}

/* SendNPCInteractReq(action13) uses the task-hall acknowledgement as a
 * complete UI transaction.  DispatchItemEvent clears that transaction for
 * 26/0, but a scene battle must enter through the following normal data event.
 * Keep the server's bytes intact while delivering the two parser-backed
 * transactions through the existing event-7 queue. */
static bool vm_client_extract_action13_battle_followup(
    u8 *response, u32 *responseLen, u8 *followup, u32 followupCap,
    u32 *followupLen)
{
    u32 starts[3];
    u32 lengths[3];
    u32 offset = 5;
    u32 objectCount = 0;
    u32 primaryLen = 5;
    u32 followLen = 5;
    vm_client_wt_object objects[3];

    if (followupLen != NULL)
        *followupLen = 0;
    if (response == NULL || responseLen == NULL || *responseLen < 23 ||
        response[0] != 'W' || response[1] != 'T' || response[4] != 3 ||
        followup == NULL || followupCap < 5)
    {
        return false;
    }
    while (offset < *responseLen && objectCount < 3)
    {
        starts[objectCount] = offset;
        if (!vm_client_next_wt_object(response, *responseLen, &offset,
                                      &objects[objectCount]))
        {
            return false;
        }
        lengths[objectCount] = offset - starts[objectCount];
        ++objectCount;
    }
    if (offset != *responseLen || objectCount != 3 ||
        objects[0].major != 1 || objects[0].kind != 26 ||
        objects[0].subtype != 0 || objects[0].payloadLen != 0 ||
        objects[1].major != 1 || objects[1].kind != 2 ||
        objects[1].subtype != 2 || objects[2].major != 1 ||
        objects[2].kind != 4 || objects[2].subtype != 5)
    {
        return false;
    }

    primaryLen += lengths[0];
    followLen += lengths[1] + lengths[2];
    if (followLen > followupCap)
        return false;
    memmove(response + 5, response + starts[0], lengths[0]);
    memcpy(followup + 5, response + starts[1], lengths[1]);
    memcpy(followup + 5 + lengths[1], response + starts[2], lengths[2]);
    vm_client_finish_wt_packet(response, primaryLen, 1);
    vm_client_finish_wt_packet(followup, followLen, 2);
    *responseLen = primaryLen;
    if (followupLen != NULL)
        *followupLen = followLen;
    printf("[info][network] remote_action13_challenge_split primary=26/0 "
           "followup=2/2+4/5 delivery=event7-then-event7 "
           "evidence=JianghuOL.CBE:0x01039C28+mmBattle:0x66CC\n");
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

static vm_client_socket vm_client_connect(void)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
#ifndef _WIN32
    struct timeval timeout;
#endif
    char port[16];
    vm_client_socket sock = VM_CLIENT_INVALID_SOCKET;

    if (!vm_client_socket_init())
        return VM_CLIENT_INVALID_SOCKET;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
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
        if (connect(sock, address->ai_addr, address->ai_addrlen) == 0)
            break;
        vm_client_close_socket(sock);
        sock = VM_CLIENT_INVALID_SOCKET;
    }
#ifdef _WIN32
    }
#endif
    freeaddrinfo(addresses);
    return sock;
}

static u32 vm_client_encode_meta(u8 *meta, u32 cap)
{
    if (meta == NULL || cap < 4 || g_mockServiceClientId == 0)
        return 0;
    vm_client_write_le32(meta, g_mockServiceClientId);
    return 4;
}

static bool vm_client_read_response(vm_client_socket sock, u8 *response, u32 responseCap,
                                    u32 *responseLen, u32 *eventType,
                                    bool *closeAfterData)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u32 flags;
    u32 len;
    u32 event;
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
    return true;
}

static bool vm_client_remote_request(const u8 *request, u32 requestLen,
                                     u8 *response, u32 responseCap,
                                     u32 *responseLen, u32 *eventType,
                                     bool *closeAfterData,
                                     u8 *followup, u32 followupCap,
                                     u32 *followupLen,
                                     bool *followupNextSchedulerTick)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 meta[16];
    u32 metaLen = vm_client_encode_meta(meta, sizeof(meta));
    vm_client_socket sock;
    bool ok;

    if (responseLen != NULL)
        *responseLen = 0;
    if (eventType != NULL)
        *eventType = 7;
    if (closeAfterData != NULL)
        *closeAfterData = false;
    if (followupLen != NULL)
        *followupLen = 0;
    if (followupNextSchedulerTick != NULL)
        *followupNextSchedulerTick = false;
    if (request == NULL || requestLen == 0 || response == NULL || metaLen == 0)
        return false;

    sock = vm_client_connect();
    if (sock == VM_CLIENT_INVALID_SOCKET)
        return false;
    vm_client_encode_header(header, 0, requestLen + metaLen, metaLen);
    ok = vm_client_send_all(sock, header, sizeof(header)) &&
         vm_client_send_all(sock, meta, metaLen) &&
         vm_client_send_all(sock, request, requestLen) &&
         vm_client_read_response(sock, response, responseCap,
                                 responseLen, eventType, closeAfterData);
    vm_client_close_socket(sock);
    if (ok && eventType != NULL && *eventType == 7 && responseLen != NULL &&
        followup != NULL && followupLen != NULL &&
        !vm_client_extract_item_followup(response, responseLen, followup,
                                         followupCap, followupLen, NULL))
    {
        if (vm_client_extract_action13_battle_followup(
                response, responseLen, followup, followupCap, followupLen) &&
            followupNextSchedulerTick != NULL)
        {
            *followupNextSchedulerTick = true;
        }
    }
    return ok;
}

static bool vm_client_remote_poll(u8 *response, u32 responseCap,
                                  u32 *responseLen, u32 *eventType)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 meta[16];
    u32 metaLen = vm_client_encode_meta(meta, sizeof(meta));
    vm_client_socket sock;
    bool ok;
    if (responseLen != NULL)
        *responseLen = 0;
    if (eventType != NULL)
        *eventType = 7;
    if (response == NULL || metaLen == 0)
        return false;
    sock = vm_client_connect();
    if (sock == VM_CLIENT_INVALID_SOCKET)
        return false;
    vm_client_encode_header(header, VM_CLIENT_REQUEST_FLAG_SCENE_SYNC_POLL,
                            metaLen, metaLen);
    ok = vm_client_send_all(sock, header, sizeof(header)) &&
         vm_client_send_all(sock, meta, metaLen) &&
         vm_client_read_response(sock, response, responseCap,
                                 responseLen, eventType, NULL);
    vm_client_close_socket(sock);
    return ok;
}

static void vm_net_mock_service_notify_disconnect(const char *reason)
{
    u8 header[VM_CLIENT_FRAME_SIZE];
    u8 responseHeader[VM_CLIENT_FRAME_SIZE];
    u8 meta[16];
    u32 metaLen;
    vm_client_socket sock;
    bool ok;
    if (g_mockServiceClientId == 0)
        return;
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
    bool followupNextSchedulerTick;
    bool requestIsUpdateChunk;
    u32 updateChunkStart;
    char updateChunkName[64];
    u8 *response;
    u8 *followup;
} vm_client_completion;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    bool workerStarted;
    bool stopRequested;
    bool scenePollOutstanding;
    u32 generation;
    u32 nextSequence;
    u32 queuedJobs;
    vm_client_job *jobHead;
    vm_client_job *jobTail;
    vm_client_completion *completionHead;
    vm_client_completion *completionTail;
} vm_client_async_state;

static vm_client_async_state g_vmClientAsync = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0,
    false, false, false, 1, 1, 0, NULL, NULL, NULL, NULL};

static void vm_client_free_completion(vm_client_completion *completion)
{
    if (completion == NULL)
        return;
    free(completion->response);
    free(completion->followup);
    free(completion);
}

static bool vm_client_capture_update_chunk_request(
    const u8 *request, u32 requestLen, u32 *startOut,
    char *nameOut, size_t nameOutCap)
{
    u32 packetLen = 0;
    u32 offset = 4;

    if (startOut != NULL)
        *startOut = 0;
    if (nameOut != NULL && nameOutCap != 0)
        nameOut[0] = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T')
    {
        return false;
    }
    packetLen = ((u32)request[2] << 8) | request[3];
    if (packetLen < 9 || packetLen > requestLen)
        return false;
    while (offset + 5 <= packetLen)
    {
        u16 objectLen = (u16)(((u16)request[offset + 3] << 8) |
                              request[offset + 4]);
        vm_client_wt_object object;

        if (objectLen < 5 || offset + objectLen > packetLen)
            return false;
        memset(&object, 0, sizeof(object));
        object.major = request[offset];
        object.kind = request[offset + 1];
        object.subtype = request[offset + 2];
        object.payload = request + offset + 5;
        object.payloadLen = (u16)(objectLen - 5);
        if (object.major == 1 && object.kind == 18 && object.subtype == 7)
        {
            (void)vm_client_wt_object_u32(&object, "start", startOut);
            (void)vm_client_wt_object_string(&object, "name", nameOut,
                                             nameOutCap);
            return true;
        }
        offset += objectLen;
    }
    return false;
}

static void *vm_client_worker_main(void *unused)
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
        bool followupNextSchedulerTick = false;
        bool success;

        pthread_mutex_lock(&g_vmClientAsync.mutex);
        while (!g_vmClientAsync.stopRequested && g_vmClientAsync.jobHead == NULL)
            pthread_cond_wait(&g_vmClientAsync.condition, &g_vmClientAsync.mutex);
        if (g_vmClientAsync.stopRequested)
        {
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            break;
        }
        job = g_vmClientAsync.jobHead;
        g_vmClientAsync.jobHead = job->next;
        if (g_vmClientAsync.jobHead == NULL)
            g_vmClientAsync.jobTail = NULL;
        if (g_vmClientAsync.queuedJobs != 0)
            --g_vmClientAsync.queuedJobs;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);

        completion = (vm_client_completion *)calloc(1, sizeof(*completion));
        if (completion == NULL || responseScratch == NULL || followupScratch == NULL)
        {
            bool wasPoll = job->kind == VM_CLIENT_JOB_SCENE_POLL;
            free(job);
            free(completion);
            if (wasPoll)
            {
                pthread_mutex_lock(&g_vmClientAsync.mutex);
                g_vmClientAsync.scenePollOutstanding = false;
                pthread_mutex_unlock(&g_vmClientAsync.mutex);
            }
            continue;
        }
        completion->generation = job->generation;
        completion->sequence = job->sequence;
        completion->enqueueMs = job->enqueueMs;
        completion->workerStartMs = SDL_GetTicks();
        completion->connectId = job->connectId;
        completion->kind = job->kind;
        completion->eventType = 7;
        if (job->kind == VM_CLIENT_JOB_DATA &&
            vm_client_capture_update_chunk_request(
                job->request, job->requestLen,
                &completion->updateChunkStart,
                completion->updateChunkName,
                sizeof(completion->updateChunkName)))
        {
            completion->requestIsUpdateChunk = true;
        }

        if (job->kind == VM_CLIENT_JOB_SCENE_POLL)
        {
            success = vm_client_remote_poll(responseScratch, sizeof(g_netMockResponse),
                                            &responseLen, &eventType);
        }
        else
        {
            success = vm_client_remote_request(job->request, job->requestLen,
                                               responseScratch, sizeof(g_netMockResponse),
                                               &responseLen, &eventType,
                                               &closeAfterData,
                                               followupScratch, VM_CLIENT_FOLLOWUP_MAX,
                                               &followupLen,
                                               &followupNextSchedulerTick);
        }
        completion->workerDoneMs = SDL_GetTicks();
        completion->success = success;
        completion->closeAfterData = closeAfterData;
        completion->eventType = eventType;
        completion->responseLen = responseLen;
        completion->followupLen = followupLen;
        completion->followupNextSchedulerTick = followupNextSchedulerTick;
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

        pthread_mutex_lock(&g_vmClientAsync.mutex);
        if (g_vmClientAsync.stopRequested ||
            completion->generation != g_vmClientAsync.generation)
        {
            pthread_mutex_unlock(&g_vmClientAsync.mutex);
            vm_client_free_completion(completion);
            continue;
        }
        if (g_vmClientAsync.completionTail != NULL)
            g_vmClientAsync.completionTail->next = completion;
        else
            g_vmClientAsync.completionHead = completion;
        g_vmClientAsync.completionTail = completion;
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
    }
    free(responseScratch);
    free(followupScratch);
    return NULL;
}

static bool vm_client_ensure_worker(void)
{
    bool started;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    if (!g_vmClientAsync.workerStarted && !g_vmClientAsync.stopRequested &&
        pthread_create(&g_vmClientAsync.worker, NULL, vm_client_worker_main, NULL) == 0)
    {
        g_vmClientAsync.workerStarted = true;
        printf("[info][network] android client worker started queue_cap=%u\n",
               VM_CLIENT_QUEUE_MAX);
    }
    started = g_vmClientAsync.workerStarted;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    return started;
}

static bool vm_client_enqueue(vm_client_job_kind kind, u32 connectId,
                              const u8 *request, u32 requestLen)
{
    vm_client_job *job;
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
    if (g_vmClientAsync.stopRequested ||
        g_vmClientAsync.queuedJobs >= VM_CLIENT_QUEUE_MAX ||
        (kind == VM_CLIENT_JOB_SCENE_POLL && g_vmClientAsync.scenePollOutstanding))
    {
        pthread_mutex_unlock(&g_vmClientAsync.mutex);
        free(job);
        return false;
    }
    job->generation = g_vmClientAsync.generation;
    job->sequence = g_vmClientAsync.nextSequence++;
    if (g_vmClientAsync.nextSequence == 0)
        g_vmClientAsync.nextSequence = 1;
    if (g_vmClientAsync.jobTail != NULL)
        g_vmClientAsync.jobTail->next = job;
    else
        g_vmClientAsync.jobHead = job;
    g_vmClientAsync.jobTail = job;
    ++g_vmClientAsync.queuedJobs;
    if (kind == VM_CLIENT_JOB_SCENE_POLL)
        g_vmClientAsync.scenePollOutstanding = true;
    pthread_cond_signal(&g_vmClientAsync.condition);
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    return true;
}

/* Capture scene lifecycle facts from the exact downlink packet.  The state is
 * attached to that scheduler event and applied only immediately before its
 * guest callback, preserving response order when multiple TCP jobs finish in
 * one emulator frame. */
static void vm_client_capture_remote_scene_observation(
    const vm_client_completion *completion,
    vm_net_remote_observation *observation)
{
    const u8 *packet = NULL;
    u32 packetLen = 0;
    u32 offset = 5;
    u8 parsedCount = 0;

    if (completion == NULL || observation == NULL ||
        completion->eventType != 7 || completion->response == NULL ||
        completion->responseLen < 5)
    {
        return;
    }
    packet = completion->response;
    packetLen = ((u32)packet[2] << 8) | packet[3];
    if (packet[0] != 'W' || packet[1] != 'T' || packetLen < 5 ||
        packetLen > completion->responseLen)
    {
        return;
    }
    while (parsedCount < packet[4])
    {
        vm_client_wt_object object;

        if (!vm_client_next_wt_object(packet, packetLen, &offset, &object))
            return;
        if (object.major == 1 && object.kind == 30 &&
            (object.subtype == 1 || object.subtype == 2))
        {
            char scene[64];
            u16 x = 0;
            u16 y = 0;
            bool haveScene = vm_client_wt_object_string(
                &object, "scene", scene, sizeof(scene));
            bool havePos = vm_client_wt_object_posinfo(&object, &x, &y);

            if (haveScene && havePos)
            {
                observation->hasSceneTarget = 1;
                observation->sceneSubtype = object.subtype;
                observation->sceneX = x;
                observation->sceneY = y;
                snprintf(observation->scene, sizeof(observation->scene),
                         "%s", scene);
            }
            if (object.subtype == 2)
            {
                observation->sceneCompleteAfterCallback = 1;
                if (haveScene)
                {
                    snprintf(observation->scene,
                             sizeof(observation->scene), "%s", scene);
                }
            }
        }
        if (completion->requestIsUpdateChunk && object.major == 1 &&
            object.kind == 18 && object.subtype == 7)
        {
            const u8 *chunk = NULL;
            u16 chunkLen = 0;
            u32 totalSize = 0;
            char payloadName[64];

            payloadName[0] = 0;
            if (vm_client_wt_object_u32(&object, "totalsize", &totalSize) &&
                vm_client_wt_object_wrapped_bytes(
                    &object, "data", &chunk, &chunkLen) &&
                totalSize != 0 && chunkLen != 0 &&
                completion->updateChunkStart <= totalSize &&
                chunkLen <= totalSize - completion->updateChunkStart &&
                completion->updateChunkStart + chunkLen >= totalSize)
            {
                if (!vm_client_wt_object_string(
                        &object, "name", payloadName,
                        sizeof(payloadName)))
                {
                    snprintf(payloadName, sizeof(payloadName), "%s",
                             completion->updateChunkName);
                }
                if (payloadName[0] != 0)
                {
                    observation->updateComplete = 1;
                    snprintf(observation->updateName,
                             sizeof(observation->updateName), "%s",
                             payloadName);
                }
            }
        }
        ++parsedCount;
    }
}

/*
 * The post-shop hangup investigation needs the guest callback boundary, not
 * merely the TCP completion.  Recognise only the battle-start object prefix
 * emitted by the old direct builder (2/10, 2/2, 4/5, 4/11), the corrected
 * scene-poll start (2/2, 4/5, 4/11), the two-object scene start (2/2, 4/5)
 * used after an action13 confirmation, or a standalone PVP 4/10 start.  The
 * PVP form is included solely to prove whether the existing mmBattle module
 * consumes the packet after an arena/spar confirmation.  This is observation
 * only; transport scheduling and response bytes remain untouched.
 */
static void vm_client_capture_hangup_battle_start_response(
    const vm_client_completion *completion,
    vm_net_remote_observation *observation)
{
    const u8 *packet;
    u32 packetLen;
    u32 offset;
    u8 objectCount;
    u8 parsedCount = 0;
    vm_client_wt_object object;
    const u8 *expectedKinds = NULL;
    const u8 *expectedSubtypes = NULL;
    u8 expectedCount = 0;
    static const u8 directKinds[4] = {2, 2, 4, 4};
    static const u8 directSubtypes[4] = {10, 2, 5, 11};
    static const u8 pollKinds[3] = {2, 4, 4};
    static const u8 pollSubtypes[3] = {2, 5, 11};
    static const u8 sceneKinds[2] = {2, 4};
    static const u8 sceneSubtypes[2] = {2, 5};
    static const u8 pvpKinds[1] = {4};
    static const u8 pvpSubtypes[1] = {10};

    if (observation == NULL)
        return;
    if (completion == NULL || completion->eventType != 7 ||
        completion->response == NULL || completion->responseLen < 5)
    {
        return;
    }
    packet = completion->response;
    packetLen = ((u32)packet[2] << 8) | packet[3];
    if (packet[0] != 'W' || packet[1] != 'T' ||
        packetLen != completion->responseLen)
    {
        return;
    }
    /* Downlink WT keeps an outer object count at byte 4.  Its objects use a
     * six-byte header: major/kind/subtype/reserved/len-hi/len-lo.  Do not use
     * the five-byte request-object helper here. */
    objectCount = packet[4];
    if (objectCount == 0 || packetLen < 11)
        return;
    offset = 5;
    if (packet[offset] == 1 && packet[offset + 1] == 2 &&
        packet[offset + 2] == 10)
    {
        expectedKinds = directKinds;
        expectedSubtypes = directSubtypes;
        expectedCount = 4;
        observation->hangupBattleStartDirect = 1;
    }
    else if (packet[offset] == 1 && packet[offset + 1] == 2 &&
             packet[offset + 2] == 2)
    {
        if (objectCount == 2)
        {
            expectedKinds = sceneKinds;
            expectedSubtypes = sceneSubtypes;
            expectedCount = 2;
        }
        else
        {
            expectedKinds = pollKinds;
            expectedSubtypes = pollSubtypes;
            expectedCount = 3;
        }
    }
    else if (packet[offset] == 1 && packet[offset + 1] == 4 &&
             packet[offset + 2] == 10)
    {
        expectedKinds = pvpKinds;
        expectedSubtypes = pvpSubtypes;
        expectedCount = 1;
        observation->hangupBattleStartDirect = 1;
    }
    else
    {
        return;
    }
    if (objectCount < expectedCount)
        return;
    while (parsedCount < objectCount)
    {
        u16 objectLen;
        if (offset + 6 > packetLen)
            return;
        objectLen = (u16)(((u16)packet[offset + 4] << 8) |
                          packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return;
        object.major = packet[offset];
        object.kind = packet[offset + 1];
        object.subtype = packet[offset + 2];
        object.payloadLen = (u16)(objectLen - 6);
        if (parsedCount < expectedCount &&
            (object.major != 1 || object.kind != expectedKinds[parsedCount] ||
             object.subtype != expectedSubtypes[parsedCount]))
        {
            return;
        }
        ++parsedCount;
        offset += objectLen;
    }
    if (parsedCount != objectCount || offset != packetLen)
        return;

    observation->hasHangupBattleStart = 1;
    observation->hangupResponseObjectCount = objectCount;
    observation->hangupResponseParsedCount = parsedCount;
    observation->hangupResponseSequence = completion->sequence;
    observation->hangupResponseLength = completion->responseLen;
}

static void vm_net_mock_async_drain_completions(void)
{
    static u32 failureLogCount = 0;
    for (;;)
    {
        vm_client_completion *completion;
        vm_net_channel *channel;
        vm_net_remote_observation remoteObservation;
        u32 generation;
        u32 responsePtr;
        u32 nowMs;

        memset(&remoteObservation, 0, sizeof(remoteObservation));
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
            if (failureLogCount < 8)
            {
                ++failureLogCount;
                printf("[warn][network] server request failed target=%s:%u kind=%s\n",
                       g_mockServiceHost, g_mockServicePort,
                       completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene-poll" : "data");
            }
            vm_client_free_completion(completion);
            continue;
        }
        failureLogCount = 0;
        if (completion->responseLen == 0)
        {
            vm_client_free_completion(completion);
            continue;
        }
        channel = scheduler_find_net_channel(completion->connectId);
        if (channel == NULL || channel->callback == 0)
        {
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
        vm_client_capture_remote_scene_observation(completion,
                                                   &remoteObservation);
        vm_client_capture_hangup_battle_start_response(completion,
                                                       &remoteObservation);
        vm_hangup_vital_forensics_capture_response(
            completion->response, completion->responseLen,
            completion->eventType, completion->sequence,
            responsePtr, channel->callback);
        /* Scenario automation observes the exact downlink packet before it is
         * copied to guest RAM.  It never changes bytes, queues, callbacks or
         * scheduler ordering. */
        vm_automation_note_network_response(completion->response,
                                            completion->responseLen,
                                            completion->eventType,
                                            completion->sequence);
        vm_shop_return_forensics_note_downlink(completion->response,
                                               completion->responseLen,
                                               completion->eventType,
                                               completion->sequence,
                                               completion->connectId);
        g_netDownLinkData += completion->responseLen;
        scheduler_queue_net_event(completion->eventType, responsePtr,
                                  completion->responseLen, completion->responseLen,
                                  channel->callback, channel->context);
        if (remoteObservation.hasSceneTarget ||
            remoteObservation.sceneCompleteAfterCallback ||
            remoteObservation.updateComplete ||
            remoteObservation.hasHangupBattleStart)
        {
            (void)scheduler_attach_net_remote_observation(
                completion->eventType, responsePtr, channel->callback,
                channel->context, &remoteObservation);
        }
        if (remoteObservation.hasHangupBattleStart)
        {
            printf("[info][network] mock_hangup_response_queue seq=%u "
                   "event=%u response=%u objects=%u parsed=%u connect=%u "
                   "delivery=normal source=remote-hangup-start\n",
                   remoteObservation.hangupResponseSequence,
                   completion->eventType,
                   remoteObservation.hangupResponseLength,
                   remoteObservation.hangupResponseObjectCount,
                   remoteObservation.hangupResponseParsedCount,
                   completion->connectId);
            vm_net_append_hangup_protocol_trace(
                "queue", &remoteObservation, completion->eventType,
                responsePtr, channel->callback, 0xffff, 0, UC_ERR_OK);
        }
        nowMs = SDL_GetTicks();
        printf("[info][network] queue_%s connect=%u event=%u resp=%u queue_ms=%u network_ms=%u deliver_ms=%u\n",
               completion->kind == VM_CLIENT_JOB_SCENE_POLL ? "scene_poll" : "data",
               completion->connectId, completion->eventType, completion->responseLen,
               completion->workerStartMs - completion->enqueueMs,
               completion->workerDoneMs - completion->workerStartMs,
               nowMs - completion->workerDoneMs);
        if (completion->followupLen != 0 && completion->followup != NULL)
        {
            u32 followupPtr = vm_net_mock_sync_buffer_to_vm(completion->followup,
                                                            completion->followupLen);
            if (followupPtr != 0)
            {
                scheduler_queue_net_event(7, followupPtr, completion->followupLen,
                                          completion->followupLen,
                                          channel->callback, channel->context);
                if (completion->followupNextSchedulerTick)
                {
                    bool deferred = scheduler_defer_net_event_to_next_tick(
                        7, followupPtr, channel->callback, channel->context);
                    printf("[info][network] action13_battle_followup_queue "
                           "event=7 resp=%u queue_tick=%u eligible_tick=%u "
                           "deferred=%u cb=%08x ctx=%08x\n",
                           completion->followupLen, g_schedulerTick,
                           g_schedulerTick + 1u, deferred ? 1u : 0u,
                           channel->callback, channel->context);
                }
            }
            else if (completion->followupNextSchedulerTick)
            {
                printf("[info][network] action13_battle_followup_queue "
                       "event=7 resp=%u queue_tick=%u deferred=0 reason=vm-buffer\n",
                       completion->followupLen, g_schedulerTick);
            }
        }
        if (completion->closeAfterData)
            scheduler_queue_net_event(9, 0, 0, 0, channel->callback, channel->context);
        vm_client_free_completion(completion);
    }
}

static void vm_net_mock_async_reset(void)
{
    vm_client_job *job;
    vm_client_completion *completion;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    ++g_vmClientAsync.generation;
    if (g_vmClientAsync.generation == 0)
        g_vmClientAsync.generation = 1;
    job = g_vmClientAsync.jobHead;
    completion = g_vmClientAsync.completionHead;
    g_vmClientAsync.jobHead = NULL;
    g_vmClientAsync.jobTail = NULL;
    g_vmClientAsync.completionHead = NULL;
    g_vmClientAsync.completionTail = NULL;
    g_vmClientAsync.queuedJobs = 0;
    g_vmClientAsync.scenePollOutstanding = false;
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    while (job != NULL)
    {
        vm_client_job *next = job->next;
        free(job);
        job = next;
    }
    while (completion != NULL)
    {
        vm_client_completion *next = completion->next;
        vm_client_free_completion(completion);
        completion = next;
    }
}

static void vm_net_mock_async_shutdown(void)
{
    bool joinWorker;
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    joinWorker = g_vmClientAsync.workerStarted;
    g_vmClientAsync.stopRequested = true;
    pthread_cond_broadcast(&g_vmClientAsync.condition);
    pthread_mutex_unlock(&g_vmClientAsync.mutex);
    if (joinWorker)
        pthread_join(g_vmClientAsync.worker, NULL);
    vm_net_mock_async_reset();
    pthread_mutex_lock(&g_vmClientAsync.mutex);
    g_vmClientAsync.workerStarted = false;
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
    vm_shop_return_forensics_note_uplink(request, readLen, connectId);
    /* Read-only observation for the opt-in scene-hangup reward confirmer.
     * It recognises the client-owned 25/5 emitted after a real input event;
     * request bytes and transport queue remain unchanged. */
    vm_hangup_auto_confirm_note_uplink(request, readLen);
    if (!vm_client_enqueue(VM_CLIENT_JOB_DATA, connectId, request, readLen))
    {
        printf("[warn][network] client queue full connect=%u len=%u\n",
               connectId, readLen);
        return;
    }
    g_netUpLinkData += dataLen;
}

static void vm_net_mock_poll_push_if_due(void)
{
    static u32 lastPollTick = 0;
    vm_net_channel *channel = NULL;
    if (g_mockServiceClientId == 0 || Global_R9 == 0)
        return;
    if (lastPollTick != 0 && g_schedulerTick - lastPollTick < 1)
        return;
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netChannels[i].active && g_netChannels[i].callback != 0)
        {
            channel = &g_netChannels[i];
            break;
        }
    }
    if (channel == NULL)
        return;
    if (scheduler_find_pending_net_event(7, channel->callback,
                                         channel->context) != NULL)
        return;
    if (vm_client_enqueue(VM_CLIENT_JOB_SCENE_POLL, channel->connectId, NULL, 0))
        lastPollTick = g_schedulerTick;
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
