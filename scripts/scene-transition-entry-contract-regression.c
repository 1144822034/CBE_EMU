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
#include "../src/main.c"
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

    puts("scene transition entry contract regression passed");
    return 0;
}
