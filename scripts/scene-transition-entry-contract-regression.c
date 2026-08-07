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

    puts("scene transition entry contract regression passed");
    return 0;
}
