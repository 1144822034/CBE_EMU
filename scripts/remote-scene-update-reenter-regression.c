/*
 * Regression for client-only scene resource update observations.
 *
 * This stays entirely in process: it constructs the same WT30/1, WT30/2 and
 * final WT18/7 packets consumed by the remote client transport, then checks
 * the host-side observation lifecycle around the real guest callback boundary.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

static bool put_bytes(u8 *out, u32 cap, u32 *pos, const void *data, u32 len)
{
    if (out == NULL || pos == NULL || data == NULL || *pos > cap ||
        len > cap - *pos)
    {
        return false;
    }
    memcpy(out + *pos, data, len);
    *pos += len;
    return true;
}

static bool put_field(u8 *out, u32 cap, u32 *pos, const char *name,
                      const u8 *encoded, u16 encodedLen)
{
    u32 nameLen = name ? (u32)strlen(name) : 0;
    u8 header[3];

    if (nameLen == 0 || nameLen > 0xff)
        return false;
    header[0] = (u8)nameLen;
    header[1] = (u8)(encodedLen >> 8);
    header[2] = (u8)encodedLen;
    return put_bytes(out, cap, pos, header, 1) &&
           put_bytes(out, cap, pos, name, nameLen) &&
           put_bytes(out, cap, pos, header + 1, 2) &&
           put_bytes(out, cap, pos, encoded, encodedLen);
}

static bool put_string_field(u8 *out, u32 cap, u32 *pos, const char *name,
                             const char *value)
{
    u8 encoded[160];
    u32 valueLen = value ? (u32)strlen(value) : 0;

    if (valueLen > sizeof(encoded) - 2)
        return false;
    encoded[0] = (u8)(valueLen >> 8);
    encoded[1] = (u8)valueLen;
    memcpy(encoded + 2, value, valueLen);
    return put_field(out, cap, pos, name, encoded, (u16)(valueLen + 2));
}

static bool put_u32_field(u8 *out, u32 cap, u32 *pos, const char *name,
                          u32 value)
{
    u8 encoded[7] = {
        6, 0, 4, (u8)(value >> 24), (u8)(value >> 16),
        (u8)(value >> 8), (u8)value};
    return put_field(out, cap, pos, name, encoded, sizeof(encoded));
}

static bool put_posinfo_field(u8 *out, u32 cap, u32 *pos, u16 x, u16 y)
{
    u8 encoded[8] = {
        0, 2, (u8)(x >> 8), (u8)x,
        0, 2, (u8)(y >> 8), (u8)y};
    return put_field(out, cap, pos, "posinfo", encoded, sizeof(encoded));
}

static bool put_blob_field(u8 *out, u32 cap, u32 *pos, const char *name,
                           const u8 *data, u16 len)
{
    u8 encoded[258];

    if (len > sizeof(encoded) - 2)
        return false;
    encoded[0] = (u8)(len >> 8);
    encoded[1] = (u8)len;
    memcpy(encoded + 2, data, len);
    return put_field(out, cap, pos, name, encoded, (u16)(len + 2));
}

static u32 begin_response(u8 *out, u32 cap, u8 kind, u8 subtype,
                          u32 *objectStart)
{
    u32 pos = 5;

    if (out == NULL || objectStart == NULL || cap < 11)
        return 0;
    memset(out, 0, cap);
    *objectStart = pos;
    out[pos++] = 1;
    out[pos++] = kind;
    out[pos++] = subtype;
    out[pos++] = 0;
    out[pos++] = 0;
    out[pos++] = 0;
    return pos;
}

static u32 finish_response(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;

    out[objectStart + 4] = (u8)(objectLen >> 8);
    out[objectStart + 5] = (u8)objectLen;
    vm_client_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 build_scene_response(u8 *out, u32 cap, u8 subtype,
                                const char *scene, bool includePos)
{
    u32 objectStart = 0;
    u32 pos = begin_response(out, cap, 30, subtype, &objectStart);

    if (pos == 0 || !put_string_field(out, cap, &pos, "scene", scene) ||
        (includePos && !put_posinfo_field(out, cap, &pos, 120, 120)))
    {
        return 0;
    }
    return finish_response(out, objectStart, pos);
}

static u32 build_update_response(u8 *out, u32 cap, const char *name)
{
    static const u8 chunk[4] = {1, 2, 3, 4};
    u32 objectStart = 0;
    u32 pos = begin_response(out, cap, 18, 7, &objectStart);

    if (pos == 0 || !put_u32_field(out, cap, &pos, "totalsize", 4) ||
        !put_string_field(out, cap, &pos, "name", name) ||
        !put_blob_field(out, cap, &pos, "data", chunk, sizeof(chunk)))
    {
        return 0;
    }
    return finish_response(out, objectStart, pos);
}

static u32 build_update_request(u8 *out, u32 cap, const char *name,
                                u32 start)
{
    u32 objectStart = 4;
    u32 pos = 9;
    u32 objectLen = 0;

    if (out == NULL || cap < pos)
        return 0;
    memset(out, 0, cap);
    out[objectStart] = 1;
    out[objectStart + 1] = 18;
    out[objectStart + 2] = 7;
    if (!put_u32_field(out, cap, &pos, "start", start) ||
        !put_string_field(out, cap, &pos, "name", name))
    {
        return 0;
    }
    objectLen = pos - objectStart;
    out[objectStart + 3] = (u8)(objectLen >> 8);
    out[objectStart + 4] = (u8)objectLen;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    return pos;
}

static void reset_scene_observation_state(void)
{
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_client_last_completed_scene_change_target, 0,
           sizeof(g_vm_client_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_target_serial = 0;
    g_vm_client_last_completed_scene_change_target_valid = false;
    g_vm_client_last_completed_scene_change_tick = 0;
    g_vm_client_completed_scene_target_serial = 0;
    g_vm_client_update_completed_reenter_pending = false;
    g_vm_client_update_completed_name[0] = 0;
    g_schedulerTick = 100;
}

int main(void)
{
    static const char scene[] =
        "\xB2\xE2\xCA\xD4\xB5\xD8\xCD\xBC.sce"; /* test map, GBK */
    static const char actor[] = "e_batB.actor";
    u8 request[256];
    u8 response[512];
    vm_client_completion completion;
    vm_net_remote_observation observation;
    u32 responseLen = 0;
    u32 clearSerial = 0;
    u32 sceneSerial = 0;
    u32 updateStart = 0xffffffffu;
    char updateName[64];

    reset_scene_observation_state();
    memset(&completion, 0, sizeof(completion));
    memset(&observation, 0, sizeof(observation));
    responseLen = build_scene_response(response, sizeof(response), 1,
                                       scene, true);
    completion.eventType = 7;
    completion.response = response;
    completion.responseLen = responseLen;
    vm_client_capture_remote_scene_observation(&completion, &observation);
    if (responseLen == 0 || !observation.hasSceneTarget ||
        observation.sceneSubtype != 1 || observation.sceneX != 120 ||
        observation.sceneY != 120 ||
        vm_net_mock_apply_remote_observation(&observation) != 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !vm_net_mock_scene_names_equal_exact(
            g_vm_net_mock_last_scene_change_target.scene, scene))
    {
        fputs("WT30/1 did not establish the remote scene target\n", stderr);
        return 1;
    }
    sceneSerial = g_vm_net_mock_last_scene_change_target_serial;

    memset(&observation, 0, sizeof(observation));
    responseLen = build_scene_response(response, sizeof(response), 2,
                                       scene, false);
    completion.responseLen = responseLen;
    vm_client_capture_remote_scene_observation(&completion, &observation);
    clearSerial = vm_net_mock_apply_remote_observation(&observation);
    if (responseLen == 0 || observation.hasSceneTarget ||
        !observation.sceneCompleteAfterCallback || clearSerial != sceneSerial ||
        !g_vm_client_last_completed_scene_change_target_valid)
    {
        fputs("WT30/2 did not snapshot completion at its callback boundary\n",
              stderr);
        return 1;
    }
    vm_net_mock_finish_remote_observation(clearSerial);
    if (g_vm_net_mock_last_scene_change_target_valid)
    {
        fputs("WT30/2 target was not cleared after its own callback\n", stderr);
        return 1;
    }

    memset(&observation, 0, sizeof(observation));
    memset(updateName, 0, sizeof(updateName));
    if (build_update_request(request, sizeof(request), scene, 0) == 0 ||
        !vm_client_capture_update_chunk_request(
            request,
            ((u32)request[2] << 8) | request[3],
            &updateStart, updateName, sizeof(updateName)) ||
        updateStart != 0 ||
        !vm_net_mock_scene_names_equal_exact(updateName, scene))
    {
        fputs("WT18/7 uplink metadata was not captured\n", stderr);
        return 1;
    }
    responseLen = build_update_response(response, sizeof(response), scene);
    completion.responseLen = responseLen;
    completion.requestIsUpdateChunk = true;
    completion.updateChunkStart = updateStart;
    snprintf(completion.updateChunkName, sizeof(completion.updateChunkName),
             "%s", updateName);
    vm_client_capture_remote_scene_observation(&completion, &observation);
    clearSerial = vm_net_mock_apply_remote_observation(&observation);
    if (responseLen == 0 || !observation.updateComplete ||
        clearSerial != sceneSerial ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !vm_net_mock_consume_update_completed_scene_reenter(
            &g_vm_net_mock_last_scene_change_target))
    {
        fputs("final scene WT18/7 did not authorize one native re-entry\n",
              stderr);
        return 1;
    }
    if (vm_net_mock_consume_update_completed_scene_reenter(
            &g_vm_net_mock_last_scene_change_target))
    {
        fputs("scene update authorized more than one re-entry\n", stderr);
        return 1;
    }
    vm_net_mock_finish_remote_observation(clearSerial);

    memset(&observation, 0, sizeof(observation));
    observation.updateComplete = 1;
    snprintf(observation.updateName, sizeof(observation.updateName), "%s",
             actor);
    if (vm_net_mock_apply_remote_observation(&observation) != 0 ||
        g_vm_net_mock_last_scene_change_target_valid ||
        vm_net_mock_consume_update_completed_scene_reenter(NULL))
    {
        fputs("non-scene resource incorrectly authorized scene re-entry\n",
              stderr);
        return 1;
    }

    puts("remote scene update re-entry regression passed");
    return 0;
}
