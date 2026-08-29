/*
 * Resource-only regression for the ordinary edge-portal scene contract.
 *
 * It does not start a listener, connect to MySQL, or touch an account.  The
 * test drives the same request builders used by the service and proves the
 * ownership boundary that prevents both a loading stall and same-target scene
 * re-entry:
 *
 *   WT2/3 first request      -> exactly one WT30/2 with scene + posinfo
 *   WT25/5 + 6/* composite   -> task handler (not standalone mmGame handler)
 *                              without another scene-channel entry object
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool begin_request_object(u8 *out, u32 outCap, u32 *pos, u8 kind,
                                 u8 subtype, u32 *objectStart)
{
    if (out == NULL || pos == NULL || *pos + 5 > outCap)
        return false;
    if (objectStart)
        *objectStart = *pos;
    out[(*pos)++] = 1;
    out[(*pos)++] = kind;
    out[(*pos)++] = subtype;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    return true;
}

static void finish_request_object(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;
    out[objectStart + 3] = (u8)(objectLen >> 8);
    out[objectStart + 4] = (u8)objectLen;
}

static void finish_request_packet(u8 *out, u32 length)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(length >> 8);
    out[3] = (u8)length;
}

static bool build_scene_change_request(u8 *out, u32 outCap, const char *scene,
                                       u32 exitId, u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL ||
        !begin_request_object(out, outCap, &pos, 2, 3, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "maptype", 2) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "mapID", scene) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "exitID", exitId))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    finish_request_packet(out, pos);
    *lengthOut = pos;
    return true;
}

static bool append_empty_request_object(u8 *out, u32 outCap, u32 *pos,
                                        u8 kind, u8 subtype, u8 *count)
{
    u32 objectStart = 0;

    if (!begin_request_object(out, outCap, pos, kind, subtype, &objectStart))
    {
        return false;
    }
    finish_request_object(out, objectStart, *pos);
    (void)count;
    return true;
}

static bool build_scene_task_subset_request(u8 *out, u32 outCap, u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL ||
        !append_empty_request_object(out, outCap, &pos, 6, 1, NULL) ||
        !append_empty_request_object(out, outCap, &pos, 6, 13, NULL) ||
        !append_empty_request_object(out, outCap, &pos, 6, 14, NULL) ||
        !begin_request_object(out, outCap, &pos, 2, 10, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "Type", 101))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    if (!append_empty_request_object(out, outCap, &pos, 0x19, 5, NULL))
        return false;
    finish_request_packet(out, pos);
    *lengthOut = pos;
    return true;
}

static bool build_scene_default_event_request(u8 *out, u32 outCap,
                                              u32 *lengthOut)
{
    u32 pos = 4;

    if (out == NULL || lengthOut == NULL ||
        !append_empty_request_object(out, outCap, &pos, 0x19, 5, NULL))
    {
        return false;
    }
    finish_request_packet(out, pos);
    *lengthOut = pos;
    return true;
}

static bool build_type27_followup_request(u8 *out, u32 outCap,
                                          u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL ||
        !append_empty_request_object(out, outCap, &pos, 2, 1, NULL) ||
        !append_empty_request_object(out, outCap, &pos, 0x1b, 11, NULL) ||
        !begin_request_object(out, outCap, &pos, 0x1b, 4, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 1))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    if (!append_empty_request_object(out, outCap, &pos, 7, 42, NULL))
        return false;
    finish_request_packet(out, pos);
    *lengthOut = pos;
    return true;
}

static bool build_scene_change_post_enter_request(u8 *out, u32 outCap,
                                                  const char *scene,
                                                  u32 exitId,
                                                  u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL ||
        !append_empty_request_object(out, outCap, &pos, 0x19, 5, NULL) ||
        !begin_request_object(out, outCap, &pos, 2, 3, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "maptype", 2) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "mapID", scene) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "exitID", exitId))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    if (!append_empty_request_object(out, outCap, &pos, 0x1b, 11, NULL) ||
        !append_empty_request_object(out, outCap, &pos, 7, 42, NULL))
    {
        return false;
    }
    finish_request_packet(out, pos);
    *lengthOut = pos;
    return true;
}

static bool response_has_object(const u8 *packet, u32 length, u8 kind,
                                u8 subtype)
{
    u32 offset = 5;

    if (packet == NULL || length < 5 || packet[0] != 'W' || packet[1] != 'T')
        return false;
    for (u32 i = 0; i < packet[4]; ++i)
    {
        u16 objectLen = 0;

        if (offset + 6 > length)
            return false;
        objectLen = (u16)((packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > length)
            return false;
        if (packet[offset] == 1 && packet[offset + 1] == kind &&
            packet[offset + 2] == subtype)
        {
            return true;
        }
        offset += objectLen;
    }
    return false;
}

static bool response_object_get_u8_field(const u8 *packet, u32 length,
                                         u8 kind, u8 subtype,
                                         const char *fieldName, u8 *valueOut)
{
    u32 offset = 5;

    if (valueOut)
        *valueOut = 0;
    if (packet == NULL || length < 5 || packet[0] != 'W' || packet[1] != 'T' ||
        fieldName == NULL || fieldName[0] == 0)
    {
        return false;
    }
    for (u32 i = 0; i < packet[4]; ++i)
    {
        u16 objectLen = 0;

        if (offset + 6 > length)
            return false;
        objectLen = (u16)((packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > length)
            return false;
        if (packet[offset] != 1 || packet[offset + 1] != kind ||
            packet[offset + 2] != subtype)
        {
            offset += objectLen;
            continue;
        }
        return vm_net_mock_get_object_u8_field(packet + offset + 6,
                                               objectLen - 6, fieldName,
                                               valueOut);
    }
    return false;
}

static bool response_object_get_u16_field(const u8 *packet, u32 length,
                                          u8 kind, u8 subtype,
                                          const char *fieldName, u16 *valueOut)
{
    u32 offset = 5;

    if (valueOut)
        *valueOut = 0;
    if (packet == NULL || length < 5 || packet[0] != 'W' || packet[1] != 'T' ||
        fieldName == NULL || fieldName[0] == 0)
    {
        return false;
    }
    for (u32 i = 0; i < packet[4]; ++i)
    {
        u16 objectLen = 0;

        if (offset + 6 > length)
            return false;
        objectLen = (u16)((packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > length)
            return false;
        if (packet[offset] != 1 || packet[offset + 1] != kind ||
            packet[offset + 2] != subtype)
        {
            offset += objectLen;
            continue;
        }
        return vm_net_mock_get_object_u16_field(packet + offset + 6,
                                                objectLen - 6, fieldName,
                                                valueOut);
    }
    return false;
}

static int count_scene_result_posinfo(const u8 *packet, u32 length,
                                      bool wantPosinfo)
{
    u32 offset = 5;
    int count = 0;

    if (packet == NULL || length < 5 || packet[0] != 'W' || packet[1] != 'T')
        return -1;
    for (u32 i = 0; i < packet[4]; ++i)
    {
        u8 major = 0;
        u8 kind = 0;
        u8 subtype = 0;
        const u8 *payload = NULL;
        u16 payloadLen = 0;
        u16 objectLen = 0;
        bool hasPosinfo = false;
        u32 fieldOffset = 0;

        /* Responses use the six-byte object header written by
         * vm_net_mock_begin_wt_object(): length is at +4 and fields at +6.
         * The five-byte request parser is intentionally not used here. */
        if (offset + 6 > length)
            return -1;
        major = packet[offset];
        kind = packet[offset + 1];
        subtype = packet[offset + 2];
        objectLen = (u16)((packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > length)
            return -1;
        payload = packet + offset + 6;
        payloadLen = (u16)(objectLen - 6);
        offset += objectLen;
        if (major != 1 || kind != 0x1e || subtype != 2)
            continue;
        while (fieldOffset < payloadLen)
        {
            u8 nameLen = 0;
            u16 valueLen = 0;

            if (fieldOffset + 1 > payloadLen)
                return -1;
            nameLen = payload[fieldOffset++];
            if (fieldOffset + nameLen + 2 > payloadLen)
                return -1;
            if (nameLen == 7 &&
                memcmp(payload + fieldOffset, "posinfo", 7) == 0)
            {
                hasPosinfo = true;
            }
            fieldOffset += nameLen;
            valueLen = (u16)((payload[fieldOffset] << 8) |
                             payload[fieldOffset + 1]);
            fieldOffset += 2;
            if (fieldOffset + valueLen > payloadLen)
                return -1;
            fieldOffset += valueLen;
        }
        if (hasPosinfo == wantPosinfo)
            ++count;
    }
    return count;
}

static int assert_invalidated_scene_completion_order(const char *targetScene)
{
    const u32 clientId = 0x10203040u;
    vm_net_mock_scene_change_target target;
    u8 defaultEvent[64];
    u8 taskSubset[256];
    u8 response[4096];
    u8 sceneData[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    vm_net_mock_sce_combat_spawn spawn;
    u32 defaultEventLen = 0;
    u32 taskSubsetLen = 0;
    u32 responseLen = 0;
    u32 sceneLen = 0;
    u32 sceneStart = 0;
    bool haveSpawn = false;

    memset(&spawn, 0, sizeof(spawn));
    sceneLen = vm_net_mock_load_scene_resource(targetScene, sceneData,
                                                sizeof(sceneData));
    sceneStart = vm_net_mock_scene_payload_start(sceneData, sceneLen);
    for (u32 off = sceneStart; sceneStart != 0 && off + 14 <= sceneLen; ++off)
    {
        u32 end = 0;

        if (!vm_net_mock_parse_sce_combat_spawn_at(
                sceneData, sceneLen, off, &spawn, &end))
        {
            continue;
        }
        haveSpawn = true;
        break;
    }
    if (!haveSpawn)
    {
        fputs("scene fixture has no structured combat dependency\n", stderr);
        return 1;
    }

    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    g_vm_net_mock_content_update_loaded = true;
    g_vm_net_mock_content_update.enabled = true;
    g_vm_net_mock_content_update.id = 901;
    g_vm_net_mock_content_update.code = 902;
    g_vm_net_mock_content_update.nameCount = 3;
    snprintf(g_vm_net_mock_content_update.names[0],
             sizeof(g_vm_net_mock_content_update.names[0]), "%s",
             targetScene);
    snprintf(g_vm_net_mock_content_update.names[1],
             sizeof(g_vm_net_mock_content_update.names[1]), "%s",
             spawn.actorResource);
    snprintf(g_vm_net_mock_content_update.names[2],
             sizeof(g_vm_net_mock_content_update.names[2]), "%s",
             spawn.effectResource);
    g_vm_mock_service_active_client_id = clientId;
    vm_net_mock_content_client_note_version(clientId, true, false);

    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", targetScene);
    target.x = 120;
    target.y = 120;
    target.mapType = 2;
    target.hasSceEntry = true;
    target.sceneEnterPosinfoSent = true;
    g_vm_net_mock_last_scene_change_target = target;
    g_vm_net_mock_last_scene_change_target_valid = true;

    if (!build_scene_default_event_request(defaultEvent,
                                           sizeof(defaultEvent),
                                           &defaultEventLen) ||
        !build_scene_task_subset_request(taskSubset, sizeof(taskSubset),
                                         &taskSubsetLen))
    {
        fputs("could not construct invalidated-scene ordering requests\n",
              stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_mmgame_scene_transfer_followup_response(
        defaultEvent, defaultEventLen, response, sizeof(response));
    if (responseLen == 0 ||
        !response_has_object(response, responseLen, 0x19, 5) ||
        response_has_object(response, responseLen, 0x1e, 1) ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_scene_change_target.needsSceneDownload ||
        g_vm_net_mock_last_scene_change_target.sceneCompletionSent)
    {
        fputs("stale scene WT25/5 closed the loader before WT18/7\n",
              stderr);
        return 1;
    }
    /* A missing SCE requests WT18/7 before scene_runtime_init_and_sync can
     * emit WT6/1. Complete the body and effect manifests explicitly before
     * that runtime request: field17 is not a synthetic cache hit, and the
     * client may not enter while its Actor download remains pending. This is
     * deliberately not an instance-guide target: an ordinary entered scene
     * must still complete directly with one no-posinfo 30/2. */
    vm_net_mock_note_update_chunk_complete(targetScene);
    vm_net_mock_note_update_chunk_complete(spawn.actorResource);
    vm_net_mock_note_update_chunk_complete(spawn.effectResource);
    responseLen = vm_net_mock_build_scene_resource_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 1 ||
        g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target.sceneCompletionSent ||
        vm_net_mock_content_client_resource_pending(clientId,
                                                    spawn.actorResource) ||
        vm_net_mock_content_client_resource_pending(clientId,
                                                    spawn.effectResource))
    {
        fputs("WT6/1 did not reconcile cached combat dependencies and complete once\n",
              stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_scene_task_subset_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 0)
    {
        fputs("repeated WT6/1 completed the cache-hit scene more than once\n",
              stderr);
        return 1;
    }

    /* The persisted content version only proves that cache invalidation ran.
     * A manifest file can still be lazily missing after reconnect, so a warm
     * version must use the same first-25/5 loader boundary. */
    vm_net_mock_content_client_note_version(clientId, true, true);
    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", targetScene);
    target.x = 121;
    target.y = 120;
    target.mapType = 2;
    target.hasSceEntry = true;
    g_vm_net_mock_last_scene_change_target = target;
    g_vm_net_mock_last_scene_change_target_valid = true;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    responseLen = vm_net_mock_build_mmgame_scene_transfer_followup_response(
        defaultEvent, defaultEventLen, response, sizeof(response));
    if (responseLen == 0 ||
        !response_has_object(response, responseLen, 0x19, 5) ||
        response_has_object(response, responseLen, 0x1e, 1) ||
        count_scene_result_posinfo(response, responseLen, false) != 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_scene_change_target.sceneResourceProbeAcknowledged)
    {
        fputs("current-version manifest scene skipped the loader boundary\n",
              stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_scene_task_subset_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 1 ||
        g_vm_net_mock_last_scene_change_target_valid)
    {
        fputs("warm manifest scene did not complete at WT6/1 boundary\n",
              stderr);
        return 1;
    }

    g_vm_mock_service_active_client_id = 0;
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    return 0;
}

static int assert_instance_direct_enter_followup_order(const char *targetScene)
{
    vm_net_mock_role_db_file savedRoleDb = g_vm_net_mock_role_db;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    vm_net_mock_scene_npcinfo_seed seed;
    u8 type27Request[256];
    u8 taskSubset[256];
    u8 response[4096];
    u32 type27RequestLen = 0;
    u32 taskSubsetLen = 0;
    u32 responseLen = 0;
    u32 targetSerial = 0;
    int result = 1;

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", targetScene);
    g_vm_net_mock_role_db.roles[0].x = 120;
    g_vm_net_mock_role_db.roles[0].y = 120;
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_seeded = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;

    memset(&seed, 0, sizeof(seed));
    seed.actorId = 20092;
    snprintf(seed.instanceScene, sizeof(seed.instanceScene), "%s", targetScene);
    seed.instanceX = 120;
    seed.instanceY = 120;
    responseLen = vm_net_mock_build_instance_enter_response(
        &seed, response, sizeof(response));
    if (responseLen == 0 ||
        !response_has_object(response, responseLen, 0x1e, 1) ||
        response_has_object(response, responseLen, 0x1e, 2) ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_scene_change_target.sceneEnterPosinfoSent ||
        g_vm_net_mock_last_scene_change_target.sceneCompletionSent)
    {
        fputs("instance 30/1 did not preserve its entered pending target\n",
              stderr);
        goto cleanup;
    }
    targetSerial = g_vm_net_mock_last_scene_change_target_serial;

    if (!build_type27_followup_request(type27Request, sizeof(type27Request),
                                       &type27RequestLen))
    {
        fputs("could not construct instance WT2/1 type27 followup\n", stderr);
        goto cleanup;
    }
    responseLen = vm_net_mock_build_type27_followup_combo_response(
        type27Request, type27RequestLen, response, sizeof(response));
    if (responseLen == 0 ||
        response_has_object(response, responseLen, 0x1e, 1) ||
        response_has_object(response, responseLen, 0x1e, 2) ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        g_vm_net_mock_last_scene_change_target_serial != targetSerial)
    {
        fputs("instance WT2/1 type27 followup re-entered or completed target\n",
              stderr);
        goto cleanup;
    }

    if (!build_scene_task_subset_request(taskSubset, sizeof(taskSubset),
                                         &taskSubsetLen) || taskSubsetLen != 39)
    {
        fputs("could not construct instance WT6/1 resource followup\n", stderr);
        goto cleanup;
    }
    responseLen = vm_net_mock_build_scene_resource_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        response_has_object(response, responseLen, 0x1e, 1) ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 1 ||
        g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target.sceneCompletionSent)
    {
        fputs("instance WT6/1 did not complete once with 30/2 no-posinfo\n",
              stderr);
        goto cleanup;
    }

    responseLen = vm_net_mock_build_scene_resource_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        response_has_object(response, responseLen, 0x1e, 1) ||
        response_has_object(response, responseLen, 0x1e, 2))
    {
        fputs("repeated instance WT6/1 emitted another scene object\n", stderr);
        goto cleanup;
    }
    result = 0;

cleanup:
    g_vm_net_mock_role_db = savedRoleDb;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    return result;
}

/* A direct NPC instance enters its destination shell with the first 30/1.
 * When the SCE later completes WT18/7, the first runtime follow-up must seed
 * that existing shell and close it through 30/2 without posinfo. A second
 * 30/1 would reinitialize its live background Actor array. */
static int assert_instance_sce_install_completion_order(const char *targetScene)
{
    const u32 clientId = 0x40506070u;
    vm_net_mock_scene_npcinfo_seed seed;
    u8 taskSubset[256];
    u8 response[4096];
    u8 sceneData[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    vm_net_mock_sce_combat_spawn spawn;
    u32 taskSubsetLen = 0;
    u32 responseLen = 0;
    u32 sceneLen = 0;
    u32 sceneStart = 0;
    bool haveSpawn = false;

    memset(&spawn, 0, sizeof(spawn));
    sceneLen = vm_net_mock_load_scene_resource(targetScene, sceneData,
                                                sizeof(sceneData));
    sceneStart = vm_net_mock_scene_payload_start(sceneData, sceneLen);
    for (u32 off = sceneStart; sceneStart != 0 && off + 14 <= sceneLen; ++off)
    {
        u32 end = 0;

        if (!vm_net_mock_parse_sce_combat_spawn_at(
                sceneData, sceneLen, off, &spawn, &end))
        {
            continue;
        }
        haveSpawn = true;
        break;
    }
    if (!haveSpawn)
    {
        fputs("instance SCE-install fixture has no structured combat dependency\n",
              stderr);
        return 1;
    }

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", targetScene);
    g_vm_net_mock_role_db.roles[0].x = 120;
    g_vm_net_mock_role_db.roles[0].y = 120;
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_seeded = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    g_vm_net_mock_content_update_loaded = true;
    g_vm_net_mock_content_update.enabled = true;
    g_vm_net_mock_content_update.id = 903;
    g_vm_net_mock_content_update.code = 904;
    g_vm_net_mock_content_update.nameCount = 3;
    snprintf(g_vm_net_mock_content_update.names[0],
             sizeof(g_vm_net_mock_content_update.names[0]), "%s", targetScene);
    snprintf(g_vm_net_mock_content_update.names[1],
             sizeof(g_vm_net_mock_content_update.names[1]), "%s",
             spawn.actorResource);
    snprintf(g_vm_net_mock_content_update.names[2],
             sizeof(g_vm_net_mock_content_update.names[2]), "%s",
             spawn.effectResource);
    g_vm_mock_service_active_client_id = clientId;
    vm_net_mock_content_client_note_version(clientId, true, false);

    memset(&seed, 0, sizeof(seed));
    seed.actorId = 20092;
    snprintf(seed.instanceScene, sizeof(seed.instanceScene), "%s", targetScene);
    seed.instanceX = 120;
    seed.instanceY = 120;
    responseLen = vm_net_mock_build_instance_enter_response(
        &seed, response, sizeof(response));
    if (responseLen == 0 ||
        response[4] != 1 ||
        !response_has_object(response, responseLen, 0x1e, 1) ||
        !g_vm_net_mock_last_scene_change_target_valid)
    {
        fputs("instance 30/1 did not arm the pending target contract\n",
              stderr);
        return 1;
    }
    if (!build_scene_task_subset_request(taskSubset, sizeof(taskSubset),
                                         &taskSubsetLen) || taskSubsetLen != 39)
    {
        fputs("could not construct instance SCE-install runtime request\n", stderr);
        return 1;
    }

    vm_net_mock_note_update_chunk_complete(targetScene);
    /* Reconnects can retain the field17 body locally and therefore send no
     * WT18/7 for it after the manifest.  The target SCE and field18 effect
     * have completed; field17 must remain pending rather than be invented as
     * a cache hit or used to hold the already-created shell forever. */
    vm_net_mock_note_update_chunk_complete(spawn.effectResource);
    responseLen = vm_net_mock_build_scene_task_subset_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        response_has_object(response, responseLen, 0x1e, 1) ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 1 ||
        g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target.sceneCompletionSent ||
        !vm_net_mock_content_client_resource_pending(clientId,
                                                     spawn.actorResource) ||
        vm_net_mock_content_client_resource_pending(clientId,
                                                    spawn.effectResource) ||
        response_has_object(response, responseLen, 4, 5))
    {
        fputs("instance reconnect body-cache boundary did not complete with one no-posinfo 30/2\n",
              stderr);
        return 1;
    }

    /* If the renderer subsequently proves the body missing and requests its
     * normal WT18/7, completing that request must not recreate the scene. */
    vm_net_mock_note_update_chunk_complete(spawn.actorResource);
    responseLen = vm_net_mock_build_scene_task_subset_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        response_has_object(response, responseLen, 0x1e, 1) ||
        response_has_object(response, responseLen, 0x1e, 2) ||
        vm_net_mock_content_client_resource_pending(clientId,
                                                    spawn.actorResource))
    {
        fputs("post-body-install instance runtime request emitted another scene object\n",
              stderr);
        return 1;
    }

    g_vm_mock_service_active_client_id = 0;
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    return 0;
}

/* Role-select has already created its first scene shell from actorinfo.  A
 * later final WT18/7 must not turn standalone WT25/5 into either 16/2 or
 * 16/3 scene re-entry: both retain the live background Actor array in this
 * startup context. */
static int assert_startup_sce_install_no_second_scene_enter(const char *targetScene)
{
    const u32 clientId = 0x50607080u;
    vm_net_mock_sce_combat_spawn spawn;
    u8 defaultEvent[64];
    u8 taskSubset[256];
    u8 response[4096];
    u8 sceneData[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u32 taskSubsetLen = 0;
    u32 defaultEventLen = 0;
    u32 responseLen = 0;
    u32 sceneLen = 0;
    u32 sceneStart = 0;
    bool haveSpawn = false;

    memset(&spawn, 0, sizeof(spawn));
    sceneLen = vm_net_mock_load_scene_resource(targetScene, sceneData,
                                                sizeof(sceneData));
    sceneStart = vm_net_mock_scene_payload_start(sceneData, sceneLen);
    for (u32 off = sceneStart; sceneStart != 0 && off + 14 <= sceneLen; ++off)
    {
        u32 end = 0;

        if (!vm_net_mock_parse_sce_combat_spawn_at(
                sceneData, sceneLen, off, &spawn, &end))
        {
            continue;
        }
        haveSpawn = true;
        break;
    }
    if (!haveSpawn)
    {
        fputs("startup SCE-install fixture has no structured combat dependency\n",
              stderr);
        return 1;
    }

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", targetScene);
    g_vm_net_mock_role_db.roles[0].x = 120;
    g_vm_net_mock_role_db.roles[0].y = 120;
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    memset(&g_vm_net_mock_startup_sce_enter_target, 0,
           sizeof(g_vm_net_mock_startup_sce_enter_target));
    g_vm_net_mock_startup_sce_enter_pending = false;
    g_vm_net_mock_startup_sce_enter_install_generation = 0;
    g_vm_net_mock_startup_sce_enter_armed_tick = 0;
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_seeded = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
    g_vm_net_mock_title_role_scene_followup_pending = true;
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    g_vm_net_mock_content_update_loaded = true;
    g_vm_net_mock_content_update.enabled = true;
    g_vm_net_mock_content_update.id = 905;
    g_vm_net_mock_content_update.code = 906;
    g_vm_net_mock_content_update.nameCount = 3;
    snprintf(g_vm_net_mock_content_update.names[0],
             sizeof(g_vm_net_mock_content_update.names[0]), "%s", targetScene);
    snprintf(g_vm_net_mock_content_update.names[1],
             sizeof(g_vm_net_mock_content_update.names[1]), "%s",
             spawn.actorResource);
    snprintf(g_vm_net_mock_content_update.names[2],
             sizeof(g_vm_net_mock_content_update.names[2]), "%s",
             spawn.effectResource);
    g_vm_mock_service_active_client_id = clientId;
    vm_net_mock_content_client_note_version(clientId, true, false);

    if (!build_scene_task_subset_request(taskSubset, sizeof(taskSubset),
                                         &taskSubsetLen) || taskSubsetLen != 39)
    {
        fputs("could not construct startup SCE-install runtime request\n", stderr);
        return 1;
    }
    if (!build_scene_default_event_request(defaultEvent, sizeof(defaultEvent),
                                           &defaultEventLen))
    {
        fputs("could not construct startup native scene-enter WT25/5 request\n",
              stderr);
        return 1;
    }

    vm_net_mock_note_update_chunk_complete(targetScene);
    vm_net_mock_note_update_chunk_complete(spawn.actorResource);
    vm_net_mock_note_update_chunk_complete(spawn.effectResource);
    responseLen = vm_net_mock_build_scene_resource_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    if (responseLen == 0 ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) > 1 ||
        g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        g_vm_net_mock_title_role_scene_followup_pending ||
        g_vm_net_mock_startup_sce_enter_pending ||
        g_vm_net_mock_startup_sce_enter_install_generation != 0 ||
        vm_net_mock_content_client_resource_pending(clientId,
                                                    spawn.actorResource) ||
        vm_net_mock_content_client_resource_pending(clientId,
                                                    spawn.effectResource))
    {
        fputs("startup final SCE install changed the completed-scene contract\n",
              stderr);
        return 1;
    }

    responseLen = vm_net_mock_build_response(defaultEvent, defaultEventLen,
                                             response, sizeof(response));
    if (responseLen == 0 ||
        response_has_object(response, responseLen, 0x10, 2) ||
        response_has_object(response, responseLen, 0x10, 3) ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        g_vm_net_mock_startup_sce_enter_pending ||
        g_vm_net_mock_last_scene_change_target_valid)
    {
        fputs("startup WT25/5 emitted a scene re-entry or incomplete followup\n",
              stderr);
        return 1;
    }

    responseLen = vm_net_mock_build_response(defaultEvent, defaultEventLen,
                                             response, sizeof(response));
    if (responseLen == 0 || response[4] != 1 ||
        !response_has_object(response, responseLen, 0x19, 5) ||
        response_has_object(response, responseLen, 0x10, 2) ||
        response_has_object(response, responseLen, 0x10, 3) ||
        g_vm_net_mock_startup_sce_enter_pending ||
        g_vm_net_mock_last_scene_change_target_valid)
    {
        fputs("repeated startup WT25/5 did not remain a control ACK\n", stderr);
        return 1;
    }

    g_vm_mock_service_active_client_id = 0;
    g_vm_net_mock_title_role_scene_followup_pending = false;
    g_vm_net_mock_startup_sce_enter_pending = false;
    g_vm_net_mock_startup_sce_enter_install_generation = 0;
    g_vm_net_mock_startup_sce_enter_armed_tick = 0;
    memset(&g_vm_net_mock_startup_sce_enter_target, 0,
           sizeof(g_vm_net_mock_startup_sce_enter_target));
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    return 0;
}

static int assert_post_enter_npc_seed_handoff(void)
{
    static const char targetScene[] =
        "\x63\x30\x34\xc1\xd9\xb0\xb2\xb8\xae\x5f\x30\x31\x2e\x73\x63\x65";
    vm_net_mock_role_db_file savedRoleDb = g_vm_net_mock_role_db;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    vm_net_mock_scene_change_target savedTarget =
        g_vm_net_mock_last_scene_change_target;
    bool savedTargetValid = g_vm_net_mock_last_scene_change_target_valid;
    u32 savedTargetSerial = g_vm_net_mock_last_scene_change_target_serial;
    vm_net_mock_scene_change_target savedCompletedTarget =
        g_vm_net_mock_last_completed_scene_change_target;
    bool savedCompletedTargetValid =
        g_vm_net_mock_last_completed_scene_change_target_valid;
    u32 savedCompletedTargetTick = g_vm_net_mock_last_completed_scene_change_tick;
    bool savedNpcPending = g_vm_net_mock_scene_moveinfo_npc_pending;
    bool savedNpcSeeded = g_vm_net_mock_scene_moveinfo_npc_seeded;
    char savedNpcPendingScene[sizeof(g_vm_net_mock_scene_moveinfo_npc_pending_scene)];
    char savedNpcSeededScene[sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene)];
    u32 savedSchedulerTick = g_schedulerTick;
    u8 postEnter[256];
    u8 followup[256];
    u8 response[4096];
    u32 postEnterLen = 0;
    u32 followupLen = 0;
    u32 responseLen = 0;
    u8 npcNum = 0;
    int result = 1;

    memcpy(savedNpcPendingScene, g_vm_net_mock_scene_moveinfo_npc_pending_scene,
           sizeof(savedNpcPendingScene));
    memcpy(savedNpcSeededScene, g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
           sizeof(savedNpcSeededScene));
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", targetScene);
    g_vm_net_mock_role_db.roles[0].x = 201;
    g_vm_net_mock_role_db.roles[0].y = 140;
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_seeded = false;
    g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
    g_schedulerTick = 100;

    if (!build_scene_change_post_enter_request(postEnter, sizeof(postEnter),
                                               targetScene, 0, &postEnterLen) ||
        postEnterLen != 78 ||
        !build_scene_task_subset_request(followup, sizeof(followup),
                                         &followupLen) ||
        followupLen != 39)
    {
        fputs("could not construct post-enter NPC handoff requests\n", stderr);
        goto cleanup;
    }

    responseLen = vm_net_mock_build_scene_change_post_enter_followup_response(
        postEnter, postEnterLen, response, sizeof(response));
    if (responseLen == 0 ||
        !response_has_object(response, responseLen, 0x1b, 11) ||
        response_object_get_u8_field(response, responseLen, 0x1b, 11,
                                     "npcnum", &npcNum) ||
        !g_vm_net_mock_scene_moveinfo_npc_pending ||
        g_vm_net_mock_scene_moveinfo_npc_seeded ||
        !g_vm_net_mock_last_completed_scene_change_target_valid)
    {
        fputs("post-enter response consumed the NPC catalog before WT6/1\n",
              stderr);
        goto cleanup;
    }

    responseLen = vm_net_mock_build_scene_resource_followup_response(
        followup, followupLen, response, sizeof(response));
    if (responseLen == 0 ||
        !response_has_object(response, responseLen, 0x1b, 11) ||
        !response_object_get_u8_field(response, responseLen, 0x1b, 11,
                                      "npcnum", &npcNum) ||
        npcNum == 0 ||
        g_vm_net_mock_scene_moveinfo_npc_pending ||
        !g_vm_net_mock_scene_moveinfo_npc_seeded ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 0)
    {
        fputs("first WT6/1 did not own the one-shot non-empty NPC catalog\n",
              stderr);
        goto cleanup;
    }

    responseLen = vm_net_mock_build_scene_resource_followup_response(
        followup, followupLen, response, sizeof(response));
    if (responseLen == 0 ||
        response_object_get_u8_field(response, responseLen, 0x1b, 11,
                                     "npcnum", &npcNum))
    {
        fputs("repeated WT6/1 duplicated the NPC catalog\n", stderr);
        goto cleanup;
    }
    result = 0;

cleanup:
    g_vm_net_mock_role_db = savedRoleDb;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    g_vm_net_mock_last_scene_change_target = savedTarget;
    g_vm_net_mock_last_scene_change_target_valid = savedTargetValid;
    g_vm_net_mock_last_scene_change_target_serial = savedTargetSerial;
    g_vm_net_mock_last_completed_scene_change_target = savedCompletedTarget;
    g_vm_net_mock_last_completed_scene_change_target_valid =
        savedCompletedTargetValid;
    g_vm_net_mock_last_completed_scene_change_tick = savedCompletedTargetTick;
    g_vm_net_mock_scene_moveinfo_npc_pending = savedNpcPending;
    g_vm_net_mock_scene_moveinfo_npc_seeded = savedNpcSeeded;
    memcpy(g_vm_net_mock_scene_moveinfo_npc_pending_scene, savedNpcPendingScene,
           sizeof(savedNpcPendingScene));
    memcpy(g_vm_net_mock_scene_moveinfo_npc_seeded_scene, savedNpcSeededScene,
           sizeof(savedNpcSeededScene));
    g_schedulerTick = savedSchedulerTick;
    return result;
}

/* The settings menu can re-enter the exact same scene through a compact
 * mmGame 16/2 result.  Its old scene shell (and NPC nodes) is gone, but the
 * service had already marked that same scene's 27/11 catalog as delivered.
 * Verify the settings-only re-arm leaves the catalog for the first actual
 * WT6/1 runtime request, then consumes it exactly once. */
static int assert_settings_unstuck_npc_reseed_handoff(void)
{
    static const char targetScene[] =
        "\x63\x30\x34\xc1\xd9\xb0\xb2\xb8\xae\x5f\x30\x31\x2e\x73\x63\x65";
    vm_net_mock_role_db_file savedRoleDb = g_vm_net_mock_role_db;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    vm_net_mock_scene_change_target savedTarget =
        g_vm_net_mock_last_scene_change_target;
    bool savedTargetValid = g_vm_net_mock_last_scene_change_target_valid;
    u32 savedTargetSerial = g_vm_net_mock_last_scene_change_target_serial;
    vm_net_mock_scene_change_target savedCompletedTarget =
        g_vm_net_mock_last_completed_scene_change_target;
    bool savedCompletedTargetValid =
        g_vm_net_mock_last_completed_scene_change_target_valid;
    u32 savedCompletedTargetTick = g_vm_net_mock_last_completed_scene_change_tick;
    bool savedNpcPending = g_vm_net_mock_scene_moveinfo_npc_pending;
    bool savedNpcSeeded = g_vm_net_mock_scene_moveinfo_npc_seeded;
    char savedNpcPendingScene[sizeof(g_vm_net_mock_scene_moveinfo_npc_pending_scene)];
    char savedNpcSeededScene[sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene)];
    u32 savedSchedulerTick = g_schedulerTick;
    vm_net_mock_scene_change_target target;
    u8 followup[256];
    u8 response[4096];
    u32 followupLen = 0;
    u32 responseLen = 0;
    u8 npcNum = 0;
    int result = 1;

    memcpy(savedNpcPendingScene, g_vm_net_mock_scene_moveinfo_npc_pending_scene,
           sizeof(savedNpcPendingScene));
    memcpy(savedNpcSeededScene, g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
           sizeof(savedNpcSeededScene));
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", targetScene);
    g_vm_net_mock_role_db.roles[0].x = 200;
    g_vm_net_mock_role_db.roles[0].y = 152;
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_seeded = true;
    snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
             sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene), "%s", targetScene);
    g_schedulerTick = 100;

    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", targetScene);
    target.x = 200;
    target.y = 152;
    target.mapType = 2;
    target.hasSceEntry = true;

    if (!build_scene_task_subset_request(followup, sizeof(followup), &followupLen) ||
        followupLen != 39)
    {
        fputs("could not construct settings-unstuck WT6/1 followup\n", stderr);
        goto cleanup;
    }

    vm_net_mock_mark_direct_scene_enter_completed(
        &target, "regression-settings-unstuck-16-2");
    vm_net_mock_mark_settings_unstuck_npc_reseed_pending(&target, "16/2");
    if (g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        !g_vm_net_mock_scene_moveinfo_npc_pending ||
        g_vm_net_mock_scene_moveinfo_npc_seeded ||
        !vm_net_mock_scene_names_equal_exact(
            g_vm_net_mock_scene_moveinfo_npc_pending_scene, targetScene))
    {
        fputs("settings unstuck did not re-arm the current scene NPC catalog\n",
              stderr);
        goto cleanup;
    }

    responseLen = vm_net_mock_build_scene_resource_followup_response(
        followup, followupLen, response, sizeof(response));
    if (responseLen == 0 ||
        !response_has_object(response, responseLen, 0x1b, 11) ||
        !response_object_get_u8_field(response, responseLen, 0x1b, 11,
                                      "npcnum", &npcNum) ||
        npcNum == 0 || g_vm_net_mock_scene_moveinfo_npc_pending ||
        !g_vm_net_mock_scene_moveinfo_npc_seeded ||
        count_scene_result_posinfo(response, responseLen, true) != 0 ||
        count_scene_result_posinfo(response, responseLen, false) != 0)
    {
        fputs("settings unstuck first WT6/1 did not rebuild NPC nodes once\n",
              stderr);
        goto cleanup;
    }

    responseLen = vm_net_mock_build_scene_resource_followup_response(
        followup, followupLen, response, sizeof(response));
    if (responseLen == 0 ||
        response_object_get_u8_field(response, responseLen, 0x1b, 11,
                                     "npcnum", &npcNum))
    {
        fputs("settings unstuck repeated WT6/1 duplicated the NPC catalog\n",
              stderr);
        goto cleanup;
    }
    result = 0;

cleanup:
    g_vm_net_mock_role_db = savedRoleDb;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    g_vm_net_mock_last_scene_change_target = savedTarget;
    g_vm_net_mock_last_scene_change_target_valid = savedTargetValid;
    g_vm_net_mock_last_scene_change_target_serial = savedTargetSerial;
    g_vm_net_mock_last_completed_scene_change_target = savedCompletedTarget;
    g_vm_net_mock_last_completed_scene_change_target_valid =
        savedCompletedTargetValid;
    g_vm_net_mock_last_completed_scene_change_tick = savedCompletedTargetTick;
    g_vm_net_mock_scene_moveinfo_npc_pending = savedNpcPending;
    g_vm_net_mock_scene_moveinfo_npc_seeded = savedNpcSeeded;
    memcpy(g_vm_net_mock_scene_moveinfo_npc_pending_scene, savedNpcPendingScene,
           sizeof(savedNpcPendingScene));
    memcpy(g_vm_net_mock_scene_moveinfo_npc_seeded_scene, savedNpcSeededScene,
           sizeof(savedNpcSeededScene));
    g_schedulerTick = savedSchedulerTick;
    return result;
}

/* The recovery target is selected from an SCE edge entry, then carried in the
 * mmGame 16/2 direct-enter object.  Use an SCE fixture with entry 1 so this
 * exercises the nonzero protocol branch separately from the Linan entry-0
 * NPC-reseed test above. */
static int assert_settings_unstuck_preserves_sce_entry_exitid(void)
{
    static const char targetScene[] =
        "01\xCC\xD2\xBB\xA8\xB5\xBA_01.sce"; /* 01桃花岛_01.sce */
    vm_net_mock_role_db_file savedRoleDb = g_vm_net_mock_role_db;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    vm_net_mock_scene_change_target target;
    u8 response[256];
    u32 responseLen = 5;
    u16 expectedEntryId = 0xffff;
    u16 expectedEntryX = 0;
    u16 expectedEntryY = 0;
    u16 responseExitId = 0;
    int result = 1;

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", targetScene);
    g_vm_net_mock_role_db.roles[0].x = 120;
    g_vm_net_mock_role_db.roles[0].y = 160;

    if (!vm_net_mock_get_scene_nearest_entry_spawn_from_sce(
            targetScene, g_vm_net_mock_role_db.roles[0].x,
            g_vm_net_mock_role_db.roles[0].y, &expectedEntryX,
            &expectedEntryY, &expectedEntryId) ||
        expectedEntryId == 0 || expectedEntryId == 0xffff)
    {
        fputs("settings-unstuck exitid fixture has no nonzero SCE entry\n", stderr);
        goto cleanup;
    }
    vm_net_mock_get_current_scene_unstuck_target(&target);
    if (target.exitId != expectedEntryId)
    {
        fputs("settings-unstuck target discarded its SCE entry contract\n", stderr);
        goto cleanup;
    }
    if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(
            response, sizeof(response), &responseLen, 2, 1, &target))
    {
        fputs("could not build settings-unstuck WT16/2 response\n", stderr);
        goto cleanup;
    }
    vm_net_mock_finish_wt_packet(response, responseLen, 1);
    if (!response_object_get_u16_field(response, responseLen, 0x10, 2,
                                       "exitid", &responseExitId) ||
        responseExitId != expectedEntryId)
    {
        fputs("settings-unstuck WT16/2 omitted its SCE exitid\n", stderr);
        goto cleanup;
    }

    /* An unresolved, but syntactically valid, resource key has no SCE entry.
     * Keep the wire default at zero rather than leaking the internal 0xffff
     * sentinel used by the SCE lookup helper. */
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene),
             "missing-unstuck-entry-fixture.sce");
    g_vm_net_mock_role_db.roles[0].x = 120;
    g_vm_net_mock_role_db.roles[0].y = 160;
    vm_net_mock_get_current_scene_unstuck_target(&target);
    if (target.exitId != 0)
    {
        fputs("settings-unstuck fallback did not keep exitid zero\n", stderr);
        goto cleanup;
    }
    responseLen = 5;
    if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(
            response, sizeof(response), &responseLen, 2, 1, &target))
    {
        fputs("could not build fallback settings-unstuck WT16/2 response\n", stderr);
        goto cleanup;
    }
    vm_net_mock_finish_wt_packet(response, responseLen, 1);
    if (!response_object_get_u16_field(response, responseLen, 0x10, 2,
                                       "exitid", &responseExitId) ||
        responseExitId != 0)
    {
        fputs("settings-unstuck fallback WT16/2 did not keep exitid zero\n",
              stderr);
        goto cleanup;
    }
    result = 0;

cleanup:
    g_vm_net_mock_role_db = savedRoleDb;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    return result;
}

/* A server-side SCE inspection failure is not evidence that the client is
 * downloading a resource.  This shipped legacy scene deliberately has an
 * entity envelope rejected by the battle-resource parser, while its file is
 * still a valid scene key.  The first WT2/3 must therefore retain the normal
 * position-bearing enter instead of creating a pending download with no
 * WT18/7 resource name. */
static int assert_inconclusive_scene_probe_enters_normally(void)
{
    static const char legacyScene[] =
        "09\xBB\xAA\xC9\xBD_02.sce"; /* 09华山_02.sce */
    u8 request[256];
    u8 response[4096];
    u32 requestLen = 0;
    u32 responseLen = 0;

    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_title_role_scene_followup_pending = false;
    g_vm_net_mock_last_moveinfo_source_valid = false;
    g_vm_mock_service_active_client_id = 0x4b495249u;
    g_vm_net_mock_role_db_valid = false;

    if (!build_scene_change_request(request, sizeof(request), legacyScene, 1,
                                    &requestLen))
    {
        fputs("could not construct inconclusive-probe WT2/3 request\n", stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_scene_change_combo_response(
        request, requestLen, response, sizeof(response));
    if (responseLen == 0 ||
        count_scene_result_posinfo(response, responseLen, true) != 1 ||
        count_scene_result_posinfo(response, responseLen, false) != 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        g_vm_net_mock_last_scene_change_target.needsSceneDownload ||
        !g_vm_net_mock_last_scene_change_target.sceneEnterPosinfoSent)
    {
        fputs("inconclusive SCE probe was incorrectly converted to download wait\n",
              stderr);
        return 1;
    }
    puts("inconclusive scene-content probe preserves normal WT30/2 entry");
    return 0;
}

/* player-1 reached the Blackwood Cliff outer portal at (124,387).  The SCE
 * edge resolves that point to Kirin Hall's guarded landing (119,64).  Keep
 * this wire-level fixture separate from an input/UI test: it verifies the
 * only server-owned part of the encounter precondition, namely that the
 * first portal WT2/3 creates the destination scene shell rather than a
 * nameless download wait. */
static int assert_kirin_hall_portal_initial_entry(void)
{
    static const char sourceScene[] =
        "23\xF3\xB4\xC1\xFA\xD5\xAF_11.sce"; /* 23蟠龙寨_11.sce */
    static const char targetScene[] =
        "23\xF3\xB4\xC1\xFA\xD5\xAF_12.sce"; /* 23蟠龙寨_12.sce */
    u8 request[256];
    u8 response[4096];
    u32 requestLen = 0;
    u32 responseLen = 0;

    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_title_role_scene_followup_pending = false;
    g_vm_net_mock_role_db_valid = false;
    g_vm_mock_service_active_client_id = 0x4b495249u;
    snprintf(g_vm_net_mock_last_moveinfo_source_scene,
             sizeof(g_vm_net_mock_last_moveinfo_source_scene), "%s",
             sourceScene);
    g_vm_net_mock_last_moveinfo_source_x = 124;
    g_vm_net_mock_last_moveinfo_source_y = 387;
    g_vm_net_mock_last_moveinfo_source_tick = g_schedulerTick;
    g_vm_net_mock_last_moveinfo_source_valid = true;

    if (!build_scene_change_request(request, sizeof(request), targetScene, 1,
                                    &requestLen))
    {
        fputs("could not construct Kirin Hall portal WT2/3 request\n", stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_scene_change_combo_response(
        request, requestLen, response, sizeof(response));
    if (responseLen == 0 ||
        count_scene_result_posinfo(response, responseLen, true) != 1 ||
        count_scene_result_posinfo(response, responseLen, false) != 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        g_vm_net_mock_last_scene_change_target.needsSceneDownload ||
        !g_vm_net_mock_last_scene_change_target.sceneEnterPosinfoSent ||
        !vm_net_mock_scene_names_equal_exact(
            g_vm_net_mock_last_scene_change_target.scene, targetScene) ||
        g_vm_net_mock_last_scene_change_target.x != 119 ||
        g_vm_net_mock_last_scene_change_target.y != 64)
    {
        fputs("Kirin Hall portal WT2/3 did not create its position-bearing scene shell\n",
              stderr);
        return 1;
    }
    puts("Kirin Hall portal WT2/3 preserves one positioned scene entry");
    return 0;
}

int main(void)
{
    static const char targetScene[] =
        "01\xCC\xD2\xBB\xA8\xB5\xBA_01.sce"; /* 01桃花岛_01.sce */
    u8 sceneChange[256];
    u8 taskSubset[256];
    u8 response[4096];
    u8 repeatResponse[4096];
    u32 sceneChangeLen = 0;
    u32 taskSubsetLen = 0;
    u32 responseLen = 0;
    u32 repeatResponseLen = 0;
    u32 initialTargetSerial = 0;

#ifdef _WIN32
    _putenv_s("CBE_MYSQL_HOST", "127.0.0.1");
    _putenv_s("CBE_MYSQL_PORT", "1");
    _putenv_s("CBE_MYSQL_DATABASE", "cbe_scene_transition_regression");
#else
    setenv("CBE_MYSQL_HOST", "127.0.0.1", 1);
    setenv("CBE_MYSQL_PORT", "1", 1);
    setenv("CBE_MYSQL_DATABASE", "cbe_scene_transition_regression", 1);
#endif

    memset(sceneChange, 0, sizeof(sceneChange));
    memset(taskSubset, 0, sizeof(taskSubset));
    memset(response, 0, sizeof(response));
    memset(repeatResponse, 0, sizeof(repeatResponse));
    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_title_role_scene_followup_pending = false;

    if (assert_inconclusive_scene_probe_enters_normally() != 0)
        return 1;
    if (assert_kirin_hall_portal_initial_entry() != 0)
        return 1;

    if (!build_scene_change_request(sceneChange, sizeof(sceneChange),
                                    targetScene, 1, &sceneChangeLen))
    {
        fputs("could not construct WT2/3 scene-change request\n", stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_scene_change_combo_response(
        sceneChange, sceneChangeLen, response, sizeof(response));
    if (responseLen == 0 ||
        count_scene_result_posinfo(response, responseLen, true) != 1 ||
        count_scene_result_posinfo(response, responseLen, false) != 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_scene_change_target.sceneEnterPosinfoSent)
    {
        fputs("first WT2/3 did not emit exactly one position-bearing WT30/2\n",
              stderr);
        return 1;
    }
    initialTargetSerial = g_vm_net_mock_last_scene_change_target_serial;

    /* A repeated WT2/3 is a completion/retry of the same pending target, not
     * permission to call the scene-entry parser again. */
    repeatResponseLen = vm_net_mock_build_scene_change_combo_response(
        sceneChange, sceneChangeLen, repeatResponse, sizeof(repeatResponse));
    if (repeatResponseLen == 0 ||
        count_scene_result_posinfo(repeatResponse, repeatResponseLen, true) != 0 ||
        count_scene_result_posinfo(repeatResponse, repeatResponseLen, false) != 1 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !g_vm_net_mock_last_scene_change_target.sceneEnterPosinfoSent ||
        g_vm_net_mock_last_scene_change_target_serial != initialTargetSerial)
    {
        fputs("repeated WT2/3 re-entered or reset the pending scene target\n",
              stderr);
        return 1;
    }

    if (!build_scene_task_subset_request(taskSubset, sizeof(taskSubset),
                                         &taskSubsetLen))
    {
        fputs("could not construct WT25/5+6/* scene-task request\n", stderr);
        return 1;
    }
    if (vm_net_mock_is_mmgame_scene_transfer_followup_request(taskSubset,
                                                               taskSubsetLen))
    {
        fputs("composite scene-task request was claimed by mmGame handler\n",
              stderr);
        return 1;
    }

    memset(response, 0, sizeof(response));
    responseLen = vm_net_mock_build_scene_task_subset_followup_response(
        taskSubset, taskSubsetLen, response, sizeof(response));
    {
        int positionedCount = count_scene_result_posinfo(response, responseLen,
                                                         true);
        int ackCount = count_scene_result_posinfo(response, responseLen,
                                                  false);
        if (responseLen == 0 || positionedCount != 0 || ackCount != 0 ||
            g_vm_net_mock_last_scene_change_target_valid ||
            !g_vm_net_mock_last_completed_scene_change_target_valid)
        {
            fprintf(stderr,
                    "scene-task completion did not preserve the one-entry contract "
                    "len=%u positioned=%d ack=%d pending=%u completed=%u\n",
                    responseLen, positionedCount, ackCount,
                    g_vm_net_mock_last_scene_change_target_valid ? 1u : 0u,
                    g_vm_net_mock_last_completed_scene_change_target_valid ? 1u : 0u);
            return 1;
        }
    }

    if (assert_post_enter_npc_seed_handoff() != 0)
        return 1;
    if (assert_settings_unstuck_npc_reseed_handoff() != 0)
        return 1;
    if (assert_settings_unstuck_preserves_sce_entry_exitid() != 0)
        return 1;
    if (assert_invalidated_scene_completion_order(targetScene) != 0)
        return 1;
    if (assert_instance_direct_enter_followup_order(targetScene) != 0)
        return 1;
    if (assert_instance_sce_install_completion_order(targetScene) != 0)
        return 1;
    if (assert_startup_sce_install_no_second_scene_enter(targetScene) != 0)
        return 1;

    puts("scene transition entry contract regression passed");
    return 0;
}
