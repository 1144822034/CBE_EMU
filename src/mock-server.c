/*
 * Embedded Jianghu OL mock-server implementation.
 *
 * This file is intentionally included by main.c instead of compiled as a
 * separate translation unit for now. The mock still depends on emulator-local
 * static helpers and runtime globals; this split only moves server behavior out
 * of the main emulator source without changing linkage or guest-visible logic.
 */

#ifdef _WIN32
#include <io.h>
#include <stdint.h>
#include <windows.h>
#endif
#include <stdlib.h>

typedef struct
{
    const char *name;
    const char *contains;
    const char *responseFile;
    const u8 *response;
    u32 responseLen;
} vm_net_mock_rule;

static u8 g_netMockTitleServerListPending = 0;
static u8 g_netMockTitleServerSelectConfirmed = 0;
static u32 g_netMockBackpackGridSeededRoleId = 0;
static u8 g_netMockShop17ListPending = 0;
static u32 g_netMockTitleServerListTick = 0;
static u32 g_netMockTitleServerSelectTick = 0;
static u32 g_netMockTitleSelectedServerId = 0;

static bool vm_net_mock_request_contains(const u8 *request, u32 requestLen, const char *needle)
{
    u32 needleLen = needle ? (u32)strlen(needle) : 0;
    if (request == NULL || needleLen == 0 || requestLen < needleLen)
        return false;

    for (u32 i = 0; i + needleLen <= requestLen; ++i)
    {
        if (memcmp(request + i, needle, needleLen) == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_request_has_empty_field(const u8 *request, u32 requestLen, const char *field)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || requestLen < fieldLen + 4)
        return false;

    for (u32 i = 0; i + fieldLen + 4 <= requestLen; ++i)
    {
        if (memcmp(request + i, field, fieldLen) != 0)
            continue;
        u32 p = i + fieldLen;
        if (p + 4 <= requestLen && request[p] == 0 && request[p + 1] == 0x02 && request[p + 2] == 0 && request[p + 3] == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_get_object_u8_field(const u8 *request, u32 requestLen, const char *field, u8 *value)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p < requestLen && request[p] == 0)
            p++;
        if (p + 4 <= requestLen && request[p] == 0x03 && request[p + 1] == 0 && request[p + 2] == 1)
        {
            if (value)
                *value = request[p + 3];
            return true;
        }
        if (p + 2 <= requestLen && request[p] == 1)
        {
            if (value)
                *value = request[p + 1];
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_get_object_u16_field(const u8 *request, u32 requestLen, const char *field, u16 *value)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 6)
        return false;

    for (u32 i = 0; i + fieldLen + 6 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p < requestLen && request[p] == 0)
            p++;
        if (p + 5 <= requestLen && request[p] == 4 && request[p + 1] == 0 && request[p + 2] == 2)
        {
            if (value)
                *value = (u16)(((u16)request[p + 3] << 8) | request[p + 4]);
            return true;
        }
        if (p + 3 <= requestLen && request[p] == 2)
        {
            if (value)
                *value = (u16)(((u16)request[p + 1] << 8) | request[p + 2]);
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_get_object_u32_field(const u8 *request, u32 requestLen, const char *field, u32 *value)
{
    u32 fieldLen = (u32)strlen(field);
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 8)
        return false;

    for (u32 i = 0; i + fieldLen + 8 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        if (p < requestLen && request[p] == 0)
            p++;
        if (p + 7 <= requestLen && request[p] == 0x06 && request[p + 1] == 0 && request[p + 2] == 4)
        {
            if (value)
                *value = ((u32)request[p + 3] << 24) | ((u32)request[p + 4] << 16) |
                         ((u32)request[p + 5] << 8) | (u32)request[p + 6];
            return true;
        }
        if (p + 5 <= requestLen && request[p] == 4)
        {
            if (value)
                *value = ((u32)request[p + 1] << 24) | ((u32)request[p + 2] << 16) |
                         ((u32)request[p + 3] << 8) | (u32)request[p + 4];
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_get_object_blob_field(const u8 *request, u32 requestLen, const char *field,
                                              const u8 **value, u16 *valueLen)
{
    u32 fieldLen = (u32)strlen(field);
    if (value)
        *value = NULL;
    if (valueLen)
        *valueLen = 0;
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        for (u32 variant = 0; variant < 2; ++variant)
        {
            u32 q = p + variant;
            if (variant != 0 && request[p] != 0)
                break;
            if (q + 2 > requestLen)
                continue;
            u16 wrappedLen = (u16)(((u16)request[q] << 8) | request[q + 1]);
            q += 2;
            if (q + wrappedLen > requestLen || wrappedLen < 2)
                continue;
            u16 blobLen = (u16)(((u16)request[q] << 8) | request[q + 1]);
            q += 2;
            if ((u32)blobLen + 2 != wrappedLen || q + blobLen > requestLen)
                continue;
            if (value)
                *value = request + q;
            if (valueLen)
                *valueLen = blobLen;
            return true;
        }
        return false;
    }
    return false;
}

static bool vm_net_mock_get_object_string_field(const u8 *request, u32 requestLen, const char *field, char *value, u32 valueCap)
{
    u32 fieldLen = (u32)strlen(field);
    if (value && valueCap > 0)
        value[0] = 0;
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 5 || value == NULL || valueCap == 0)
        return false;

    for (u32 i = 0; i + fieldLen + 5 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        u32 p = i + 1 + fieldLen;
        for (u32 variant = 0; variant < 2; ++variant)
        {
            u32 q = p + variant;
            if (variant != 0 && request[p] != 0)
                break;
            if (q + 2 > requestLen)
                continue;
            u16 valueLen = (u16)(((u16)request[q] << 8) | request[q + 1]);
            q += 2;
            if (valueLen == 0 || q + valueLen > requestLen)
                continue;
            if (valueLen >= 2)
            {
                u16 blobLen = (u16)(((u16)request[q] << 8) | request[q + 1]);
                if ((u32)blobLen + 2 == valueLen && q + 2 + blobLen <= requestLen)
                {
                    u32 copyLen = SDL_min((u32)blobLen, valueCap - 1);
                    while (copyLen > 0 && request[q + 2 + copyLen - 1] == 0)
                        --copyLen;
                    memcpy(value, request + q + 2, copyLen);
                    value[copyLen] = 0;
                    return true;
                }
            }

            u32 copyLen = SDL_min((u32)valueLen, valueCap - 1);
            while (copyLen > 0 && request[q + copyLen - 1] == 0)
                --copyLen;
            memcpy(value, request + q, copyLen);
            value[copyLen] = 0;
            return true;
        }
        return false;
    }
    return false;
}

typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    const u8 *payload;
    u16 payloadLen;
} vm_net_mock_request_object;

static bool vm_net_mock_next_request_object(const u8 *request, u32 requestLen, u32 *offset, vm_net_mock_request_object *object)
{
    if (request == NULL || offset == NULL || *offset < 4 || *offset + 5 > requestLen)
        return false;

    u32 objectStart = *offset;
    u16 objectLen = (u16)(((u16)request[objectStart + 3] << 8) | request[objectStart + 4]);
    if (objectLen < 5 || objectStart + objectLen > requestLen)
        return false;

    if (object)
    {
        object->major = request[objectStart];
        object->kind = request[objectStart + 1];
        object->subtype = request[objectStart + 2];
        object->payload = request + objectStart + 5;
        object->payloadLen = (u16)(objectLen - 5);
    }
    *offset = objectStart + objectLen;
    return true;
}

static bool vm_net_mock_get_first_object_kind_subtype(const u8 *request, u32 requestLen, u8 *kind, u8 *subtype)
{
    if (request == NULL || requestLen < 11)
        return false;
    if (request[0] != 'W' || request[1] != 'T' || request[4] == 0)
        return false;
    if (kind)
        *kind = request[6];
    if (subtype)
        *subtype = request[7];
    return true;
}

static bool vm_net_mock_get_wt_header_kind_subtype(const u8 *request, u32 requestLen, u8 *kind, u8 *subtype)
{
    if (request == NULL || requestLen < 8)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;
    if (kind)
        *kind = request[5];
    if (subtype)
        *subtype = request[6];
    return true;
}

static bool vm_net_mock_request_contains_object(const u8 *request, u32 requestLen, u8 major, u8 kind, u8 subtype)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == major && object.kind == kind && object.subtype == subtype)
            return true;
    }
    return false;
}

static bool vm_net_mock_is_scene_change_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 2 &&
           subtype == 3 &&
           vm_net_mock_request_contains(request, requestLen, "maptype") &&
           vm_net_mock_request_contains(request, requestLen, "mapID") &&
           vm_net_mock_request_contains(request, requestLen, "exitID");
}

static u32 vm_net_mock_min_u32(u32 a, u32 b)
{
    return (a < b) ? a : b;
}

static u8 vm_net_mock_env_u8(const char *name, u8 fallback);
static bool vm_net_mock_role_db_has_role_id(u32 roleId);
static u8 vm_net_mock_active_role_job_or(u8 fallback);
static u8 vm_net_mock_active_role_sex_or(u8 fallback);

static bool vm_net_mock_battle_player_on_right(void)
{
    return vm_net_mock_env_u8("CBE_BATTLE_PLAYER_ON_RIGHT", 1) != 0;
}

static u8 vm_net_mock_battle_default_side(bool playerOnRight)
{
    return playerOnRight ? 1 : 0;
}

static void vm_net_mock_battle_default_wire_slots(bool playerOnRight, u8 side,
                                                  u8 *playerSlotOut, u8 *enemySlotOut)
{
    u8 playerSlot = playerOnRight ? 1 : 0;
    u8 enemySlot = playerOnRight ? 0 : 1;

    if (side == 1)
    {
        /*
         * Battle.cbm CalcTargetSideIndex(0x6CE8) remaps wire slots through the
         * side/group counts loaded by the battle-start packet. The subtype-5
         * scene-monster path uses the touched scene actor row directly; runtime
         * evidence shows wire 1 is the right-side player and wire 0 is the
         * left-side monster there. The older subtype-10 fallback keeps the
         * opposite one-monster/one-role layout.
         */
        if (g_mockBattleSceneMonsterStartActive != 0)
        {
            playerSlot = playerOnRight ? 1 : 0;
            enemySlot = playerOnRight ? 0 : 1;
        }
        else
        {
            playerSlot = playerOnRight ? 0 : 1;
            enemySlot = playerOnRight ? 1 : 0;
        }
    }
    if (playerSlotOut)
        *playerSlotOut = playerSlot;
    if (enemySlotOut)
        *enemySlotOut = enemySlot;
}

static u32 vm_net_mock_battle_negative_delta_u32(u32 value)
{
    return (u32)(0u - value);
}

static u32 vm_net_mock_battle_delta_display_value(u32 value)
{
    return (value & 0x80000000u) ? (u32)(0u - value) : value;
}

static u8 vm_net_mock_battle_terminal_actor_slot(u8 playerSlot, u8 enemySlot)
{
    return (g_mockBattleEnemyHpCurrent == 0) ? enemySlot : playerSlot;
}

static bool vm_net_mock_is_title_server_select_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen < 4)
        return false;

    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 1, 4))
        return false;

    /*
     * Server/area confirmation carries server-selection state on 1/1/4.
     * Do not answer this with title role-list actorinfo: mmTitle sub_3544()
     * treats actorinfo on subtype 4 as the role-list payload, which belongs
     * behind the later explicit 1/1/16 role-list request.
     */
    return vm_net_mock_request_contains(request, requestLen, "serverID") &&
           vm_net_mock_request_contains(request, requestLen, "moneytype");
}

static bool vm_net_mock_is_title_rolelist_stage_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 1 &&
           subtype == 16 &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 1, 16) &&
           !vm_net_mock_is_title_server_select_request(request, requestLen);
}

static bool vm_net_mock_parse_title_role_select_actor_id_scan(const u8 *payload,
                                                              u32 payloadLen,
                                                              u32 *actorIdOut);

static bool vm_net_mock_parse_title_role_select_request(const u8 *request,
                                                        u32 requestLen,
                                                        u32 *actorIdOut)
{
    u8 kind = 0;
    u8 subtype = 0;
    u32 offset = 4;
    u32 objectCount = 0;
    bool matched = false;
    u32 actorId = 0;
    vm_net_mock_request_object object;

    if (actorIdOut)
        *actorIdOut = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 1 ||
        subtype != 6)
    {
        return false;
    }

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        ++objectCount;
        if (object.major != 1 || object.kind != 1 || object.subtype != 6)
            continue;
        if (matched || object.payloadLen == 0 || object.payloadLen > 64)
            return false;
        matched = vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorID", &actorId) ||
                  vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorid", &actorId) ||
                  vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorId", &actorId) ||
                  vm_net_mock_parse_title_role_select_actor_id_scan(object.payload, object.payloadLen, &actorId);
        if (!matched)
            return false;
    }
    if (!matched || objectCount != 1 || offset != requestLen || actorId == 0)
        return false;
    if (actorIdOut)
        *actorIdOut = actorId;
    return true;
}

static bool vm_net_mock_parse_title_role_select_actor_id_scan(const u8 *payload,
                                                              u32 payloadLen,
                                                              u32 *actorIdOut)
{
    if (actorIdOut)
        *actorIdOut = 0;
    if (payload == NULL || payloadLen < 4)
        return false;

    for (u32 i = 0; i + 4 <= payloadLen; ++i)
    {
        u32 candidate = ((u32)payload[i] << 24) |
                        ((u32)payload[i + 1] << 16) |
                        ((u32)payload[i + 2] << 8) |
                        (u32)payload[i + 3];
        if (candidate != 0 && vm_net_mock_role_db_has_role_id(candidate))
        {
            if (actorIdOut)
                *actorIdOut = candidate;
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_is_title_role_select_request(const u8 *request, u32 requestLen)
{
    u32 actorId = 0;
    return vm_net_mock_parse_title_role_select_request(request, requestLen, &actorId);
}

typedef struct
{
    u8 rawSex;
    u8 rawJob;
    bool rawJobIsIndex;
    bool decoded;
    char name[32];
} vm_net_mock_title_role_create_request;

static void vm_net_mock_copy_request_text(char *out, u32 outCap, const u8 *value, u32 valueLen)
{
    u32 copyLen = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (value == NULL || valueLen == 0)
        return;
    copyLen = SDL_min(valueLen, outCap - 1);
    while (copyLen > 0 && value[copyLen - 1] == 0)
        --copyLen;
    if (copyLen > 0)
        memcpy(out, value, copyLen);
    out[copyLen] = 0;
}

static bool vm_net_mock_parse_request_text_at(const u8 *payload,
                                              u32 payloadLen,
                                              u32 offset,
                                              char *out,
                                              u32 outCap,
                                              u32 *nextOffset)
{
    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (payload == NULL || offset >= payloadLen)
        return false;

    for (u32 variant = 0; variant < 2; ++variant)
    {
        u32 q = offset + variant;
        if (variant != 0 && payload[offset] != 0)
            break;
        if (q + 2 > payloadLen)
            continue;

        u16 valueLen = (u16)(((u16)payload[q] << 8) | payload[q + 1]);
        u32 valueStart = q + 2;
        if (valueLen == 0 || valueStart + valueLen > payloadLen)
            continue;

        if (valueLen >= 2)
        {
            u16 blobLen = (u16)(((u16)payload[valueStart] << 8) | payload[valueStart + 1]);
            if ((u32)blobLen + 2 == valueLen && valueStart + 2 + blobLen <= payloadLen)
            {
                vm_net_mock_copy_request_text(out, outCap, payload + valueStart + 2, blobLen);
                if (out[0] != 0)
                {
                    if (nextOffset)
                        *nextOffset = valueStart + valueLen;
                    return true;
                }
            }
        }

        vm_net_mock_copy_request_text(out, outCap, payload + valueStart, valueLen);
        if (out[0] != 0)
        {
            if (nextOffset)
                *nextOffset = valueStart + valueLen;
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_parse_title_role_create_numeric_at(const u8 *payload,
                                                           u32 payloadLen,
                                                           u32 offset,
                                                           u8 *value,
                                                           u32 *nextOffset)
{
    if (payload == NULL || offset >= payloadLen)
        return false;

    for (u32 variant = 0; variant < 2; ++variant)
    {
        u32 q = offset + variant;
        if (variant != 0 && payload[offset] != 0)
            break;
        if (q + 4 <= payloadLen &&
            payload[q] == 0x03 &&
            payload[q + 1] == 0 &&
            payload[q + 2] == 1)
        {
            if (value)
                *value = payload[q + 3];
            if (nextOffset)
                *nextOffset = q + 4;
            return true;
        }
        if (q + 3 <= payloadLen &&
            payload[q] == 0 &&
            payload[q + 1] == 1)
        {
            if (value)
                *value = payload[q + 2];
            if (nextOffset)
                *nextOffset = q + 3;
            return true;
        }
        if (q + 2 <= payloadLen && payload[q] == 1)
        {
            if (value)
                *value = payload[q + 1];
            if (nextOffset)
                *nextOffset = q + 2;
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_parse_title_role_create_payload_positional(const u8 *payload,
                                                                   u32 payloadLen,
                                                                   vm_net_mock_title_role_create_request *fields)
{
    u8 numericFields[2] = {0};
    u32 numericCount = 0;
    bool sawNameString = false;
    u32 p = 0;

    if (payload == NULL || fields == NULL)
        return false;
    while (p + 1 <= payloadLen)
    {
        u32 fieldStart = p;
        u8 fieldLen = payload[p++];
        if (fieldLen == 0 || fieldLen > 48 || p + fieldLen > payloadLen)
            break;
        p += fieldLen;

        u8 numericValue = 0;
        u32 next = p;
        if (vm_net_mock_parse_title_role_create_numeric_at(payload, payloadLen, p, &numericValue, &next))
        {
            if (numericCount < 2)
                numericFields[numericCount++] = numericValue;
            p = next;
            continue;
        }

        if (vm_net_mock_parse_request_text_at(payload,
                                              payloadLen,
                                              p,
                                              fields->name,
                                              sizeof(fields->name),
                                              &next))
        {
            sawNameString = true;
            p = next;
            continue;
        }

        p = fieldStart;
        break;
    }

    if (numericCount < 2 || !sawNameString || p == 0)
        return false;
    fields->rawJob = numericFields[0];
    fields->rawSex = numericFields[1];
    fields->rawJobIsIndex = true;
    return true;
}

static bool vm_net_mock_parse_title_role_create_payload_raw_stream(const u8 *payload,
                                                                   u32 payloadLen,
                                                                   vm_net_mock_title_role_create_request *fields)
{
    u8 numericFields[2] = {0};
    u32 numericCount = 0;
    u32 p = 0;
    u32 next = 0;

    if (payload == NULL || fields == NULL)
        return false;

    while (numericCount < 2 && p < payloadLen)
    {
        u8 numericValue = 0;
        if (vm_net_mock_parse_title_role_create_numeric_at(payload, payloadLen, p, &numericValue, &next))
        {
            numericFields[numericCount++] = numericValue;
            p = next;
            continue;
        }
        break;
    }

    if (numericCount < 2)
        return false;

    fields->rawJob = numericFields[0];
    fields->rawSex = numericFields[1];
    fields->rawJobIsIndex = true;
    if (!vm_net_mock_parse_request_text_at(payload,
                                           payloadLen,
                                           p,
                                           fields->name,
                                           sizeof(fields->name),
                                           &next))
    {
        return false;
    }
    return true;
}

static bool vm_net_mock_request_text_looks_like_role_name(const char *text)
{
    bool hasNameByte = false;
    size_t len = text ? strlen(text) : 0;
    if (len < 2 || len >= 32)
        return false;
    if (strcmp(text, "sex") == 0 ||
        strcmp(text, "Sex") == 0 ||
        strcmp(text, "job") == 0 ||
        strcmp(text, "Job") == 0 ||
        strcmp(text, "name") == 0 ||
        strcmp(text, "Name") == 0 ||
        strcmp(text, "actorID") == 0 ||
        strcmp(text, "actorid") == 0 ||
        strcmp(text, "actorName") == 0 ||
        strcmp(text, "roleName") == 0)
    {
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        u8 ch = (u8)text[i];
        if (ch < 0x20 || ch == 0x7f)
            return false;
        if (ch >= 0x80 ||
            (ch >= '0' && ch <= '9') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z'))
        {
            hasNameByte = true;
        }
    }
    return hasNameByte;
}

static bool vm_net_mock_parse_title_role_create_payload_scanned(const u8 *payload,
                                                                u32 payloadLen,
                                                                vm_net_mock_title_role_create_request *fields)
{
    u8 numericFields[2] = {0};
    u32 numericCount = 0;
    char bestName[32];
    u32 bestNameLen = 0;

    if (payload == NULL || fields == NULL)
        return false;
    bestName[0] = 0;

    for (u32 p = 0; p < payloadLen && numericCount < 2;)
    {
        u8 numericValue = 0;
        u32 next = p;
        if (vm_net_mock_parse_title_role_create_numeric_at(payload, payloadLen, p, &numericValue, &next) &&
            numericValue <= 3)
        {
            numericFields[numericCount++] = numericValue;
            p = next;
            continue;
        }
        ++p;
    }

    for (u32 p = 0; p + 2 <= payloadLen; ++p)
    {
        char candidate[32];
        u32 next = p;
        candidate[0] = 0;
        if (!vm_net_mock_parse_request_text_at(payload, payloadLen, p, candidate, sizeof(candidate), &next))
            continue;
        if (!vm_net_mock_request_text_looks_like_role_name(candidate))
            continue;
        u32 candidateLen = (u32)strlen(candidate);
        if (candidateLen > bestNameLen)
        {
            bestNameLen = candidateLen;
            snprintf(bestName, sizeof(bestName), "%s", candidate);
        }
    }

    if (numericCount < 2 || bestName[0] == 0)
        return false;
    fields->rawJob = numericFields[0];
    fields->rawSex = numericFields[1];
    fields->rawJobIsIndex = true;
    snprintf(fields->name, sizeof(fields->name), "%s", bestName);
    return true;
}

static bool vm_net_mock_parse_title_role_create_payload(const u8 *payload,
                                                        u32 payloadLen,
                                                        vm_net_mock_title_role_create_request *fields)
{
    bool haveSex = false;
    bool haveJob = false;
    bool haveName = false;

    if (payload == NULL || fields == NULL)
        return false;
    memset(fields, 0, sizeof(*fields));

    haveSex = vm_net_mock_get_object_u8_field(payload, payloadLen, "sex", &fields->rawSex) ||
              vm_net_mock_get_object_u8_field(payload, payloadLen, "Sex", &fields->rawSex) ||
              vm_net_mock_get_object_u8_field(payload, payloadLen, "gender", &fields->rawSex) ||
              vm_net_mock_get_object_u8_field(payload, payloadLen, "genderID", &fields->rawSex);
    haveJob = vm_net_mock_get_object_u8_field(payload, payloadLen, "job", &fields->rawJob) ||
              vm_net_mock_get_object_u8_field(payload, payloadLen, "Job", &fields->rawJob) ||
              vm_net_mock_get_object_u8_field(payload, payloadLen, "jobID", &fields->rawJob) ||
              vm_net_mock_get_object_u8_field(payload, payloadLen, "career", &fields->rawJob);
    haveName = vm_net_mock_get_object_string_field(payload, payloadLen, "name", fields->name, sizeof(fields->name)) ||
               vm_net_mock_get_object_string_field(payload, payloadLen, "Name", fields->name, sizeof(fields->name)) ||
               vm_net_mock_get_object_string_field(payload, payloadLen, "actorname", fields->name, sizeof(fields->name)) ||
               vm_net_mock_get_object_string_field(payload, payloadLen, "actorName", fields->name, sizeof(fields->name)) ||
               vm_net_mock_get_object_string_field(payload, payloadLen, "roleName", fields->name, sizeof(fields->name));

    if (haveSex && haveJob && haveName)
    {
        fields->decoded = true;
        return true;
    }
    if (vm_net_mock_parse_title_role_create_payload_positional(payload, payloadLen, fields))
    {
        fields->decoded = true;
        return true;
    }
    if (vm_net_mock_parse_title_role_create_payload_raw_stream(payload, payloadLen, fields))
    {
        fields->decoded = true;
        return true;
    }
    if (vm_net_mock_parse_title_role_create_payload_scanned(payload, payloadLen, fields))
    {
        fields->decoded = true;
        return true;
    }
    return false;
}

static bool vm_net_mock_parse_title_role_create_request(const u8 *request,
                                                        u32 requestLen,
                                                        vm_net_mock_title_role_create_request *fields)
{
    u8 kind = 0;
    u8 subtype = 0;
    u32 offset = 4;
    u32 objectCount = 0;
    bool matched = false;
    bool parsedFields = false;
    vm_net_mock_request_object object;

    if (fields)
    {
        memset(fields, 0, sizeof(*fields));
        fields->rawSex = 0;
        fields->rawJob = 1;
        fields->rawJobIsIndex = false;
        fields->decoded = false;
    }

    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 1 ||
        subtype != 7)
    {
        return false;
    }

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        ++objectCount;
        if (object.major != 1 || object.kind != 1 || object.subtype != 7)
            continue;
        if (matched || object.payloadLen < 8 || object.payloadLen > 96)
            return false;
        matched = true;
        parsedFields = vm_net_mock_parse_title_role_create_payload(object.payload,
                                                                  object.payloadLen,
                                                                  fields);
    }
    if (fields && !parsedFields)
        fields->decoded = false;
    return matched && objectCount == 1 && offset == requestLen;
}

static bool vm_net_mock_is_title_role_create_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_title_role_create_request fields;
    return vm_net_mock_parse_title_role_create_request(request, requestLen, &fields);
}

static bool vm_net_mock_parse_title_role_delete_request(const u8 *request,
                                                        u32 requestLen,
                                                        u32 *actorIdOut)
{
    u8 kind = 0;
    u8 subtype = 0;
    u32 offset = 4;
    u32 objectCount = 0;
    bool matched = false;
    u32 actorId = 0;
    vm_net_mock_request_object object;

    if (actorIdOut)
        *actorIdOut = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 1 ||
        subtype != 8)
    {
        return false;
    }

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        ++objectCount;
        if (object.major != 1 || object.kind != 1 || object.subtype != 8)
            continue;
        if (matched || object.payloadLen < 8 || object.payloadLen > 48)
            return false;
        matched = vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorID", &actorId) ||
                  vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorid", &actorId);
        if (!matched)
            return false;
    }
    if (!matched || objectCount != 1 || offset != requestLen || actorId == 0)
        return false;
    if (actorIdOut)
        *actorIdOut = actorId;
    return true;
}

static bool vm_net_mock_is_title_role_delete_request(const u8 *request, u32 requestLen)
{
    u32 actorId = 0;
    return vm_net_mock_parse_title_role_delete_request(request, requestLen, &actorId);
}

static bool vm_net_mock_is_login_validation_request(const u8 *request, u32 requestLen, u8 *requestSubtype)
{
    u8 kind = 0;
    u8 subtype = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    if (kind != 1)
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 1, subtype))
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "coreVer") ||
        !vm_net_mock_request_contains(request, requestLen, "appVer") ||
        !(vm_net_mock_request_contains(request, requestLen, "username") ||
          vm_net_mock_request_contains(request, requestLen, "userName")) ||
        !vm_net_mock_request_contains(request, requestLen, "password") ||
        !vm_net_mock_request_contains(request, requestLen, "imsi"))
    {
        return false;
    }
    if (requestSubtype)
        *requestSubtype = subtype;
    return true;
}

static void vm_net_ensure_business_callback(const char *reason)
{
    if (g_netCurrentObject == 0)
        return;

    u32 callback = 0;
    u32 callbackAddr = g_netCurrentObject + 0x14;
    if (uc_mem_read(MTK, callbackAddr, &callback, 4) != UC_ERR_OK)
        return;
    if (callback != 0)
    {
        return;
    }

    callback = VM_GAME_NET_BUSINESS_CALLBACK;
    uc_mem_write(MTK, callbackAddr, &callback, 4);
}

static u32 vm_net_mock_copy_response(const u8 *response, u32 responseLen, u8 *out, u32 outCap)
{
    if (response == NULL || responseLen == 0 || out == NULL || outCap == 0)
        return 0;
    u32 copyLen = responseLen < outCap ? responseLen : outCap;
    memcpy(out, response, copyLen);
    return copyLen;
}

static FILE *vm_net_mock_fopen_response_file(const char *path)
{
    FILE *fp = NULL;

    if (path == NULL || path[0] == 0)
        return NULL;

    fp = fopen(path, "rb");
    if (fp != NULL)
        return fp;

#ifdef __EMSCRIPTEN__
    const char *wasmPath = vm_wasm_lookup_utf8_path_alias(path);
    if (wasmPath != NULL)
    {
        fp = fopen(wasmPath, "rb");
        if (fp != NULL)
            return fp;
    }
#endif

#ifdef _WIN32
    char exePath[MAX_PATH];
    DWORD exeLen = GetModuleFileNameA(NULL, exePath, sizeof(exePath));
    if (exeLen > 0 && exeLen < sizeof(exePath))
    {
        char *slash = strrchr(exePath, '\\');
        char tryPath[MAX_PATH * 2];
        if (slash == NULL)
            slash = strrchr(exePath, '/');
        if (slash != NULL)
        {
            *slash = 0;
            snprintf(tryPath, sizeof(tryPath), "%s\\%s", exePath, path);
            fp = fopen(tryPath, "rb");
            if (fp != NULL)
                return fp;

            snprintf(tryPath, sizeof(tryPath), "%s\\..\\%s", exePath, path);
            fp = fopen(tryPath, "rb");
            if (fp != NULL)
                return fp;
        }
    }
#endif

    return NULL;
}

static u32 vm_net_mock_load_response_file(const char *path, u8 *out, u32 outCap)
{
    if (path == NULL || out == NULL || outCap == 0)
        return 0;

    FILE *fp = vm_net_mock_fopen_response_file(path);
    if (fp == NULL)
        return 0;

    size_t len = fread(out, 1, outCap, fp);
    fclose(fp);
    return (u32)len;
}

static bool vm_net_mock_file_has_data(const char *path)
{
    FILE *fp = vm_net_mock_fopen_response_file(path);
    if (fp == NULL)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size > 40;
}

static bool vm_net_mock_file_has_min_size(const char *path, long minSize)
{
    FILE *fp = vm_net_mock_fopen_response_file(path);
    if (fp == NULL)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size >= minSize;
}

static long vm_net_mock_file_size(const char *path)
{
    FILE *fp = vm_net_mock_fopen_response_file(path);
    if (fp == NULL)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    fclose(fp);
    return size;
}

static u32 vm_net_mock_file_checksum(const char *path)
{
    FILE *fp = vm_net_mock_fopen_response_file(path);
    if (fp == NULL)
        return 0;
    u32 sum = 2166136261u;
    int ch;
    while ((ch = fgetc(fp)) != EOF)
    {
        sum ^= (u8)ch;
        sum *= 16777619u;
    }
    fclose(fp);
    return sum;
}

static bool vm_host_file_exists(const char *path)
{
    FILE *fp = vm_net_mock_fopen_response_file(path);
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

static u32 vm_alloc_host_string(const char *text)
{
    u32 len = (u32)strlen(text) + 1;
    u32 ptr = vm_malloc(len);
    if (ptr)
        uc_mem_write(MTK, ptr, text, len);
    return ptr;
}

static bool vm_net_mock_has_installed_update(void)
{
    long installedSize = vm_net_mock_file_size("JHOnlineData/MMORPGTempcbm");
    long gameSize = vm_net_mock_file_size("JHOnlineData/mmGameMstarWqvga.cbm");
    if (installedSize < 41 || gameSize < 41 ||
        installedSize != gameSize ||
        !vm_net_mock_file_has_min_size("JHOnlineData/mmorpg_updateversioncbm", 40))
    {
        return false;
    }

    u32 installedHash = vm_net_mock_file_checksum("JHOnlineData/MMORPGTempcbm");
    u32 gameHash = vm_net_mock_file_checksum("JHOnlineData/mmGameMstarWqvga.cbm");
    bool ok = installedHash == gameHash;
    return ok;
}

static bool vm_net_mock_buffer_has_nonzero(const u8 *data, u32 len)
{
    for (u32 i = 0; i < len; ++i)
    {
        if (data[i] != 0)
            return true;
    }
    return false;
}

static u32 vm_net_mock_signed_byte_sum(const u8 *data, u32 len)
{
    int sum = 0;
    for (u32 i = 0; i < len; ++i)
        sum += (signed char)data[i];
    return (u32)sum;
}

static u32 vm_net_mock_read_download_checksum(void)
{
    u32 value = 0;
    if (Global_R9)
        uc_mem_read(MTK, Global_R9 + 0x9584, &value, sizeof(value));
    return value;
}

static u32 vm_net_mock_load_update_payload(u8 *out, u32 outCap)
{
    static const char *payloadPaths[] = {
        "JHOnlineData/MMORPGTempcbm.mock",
        "JHOnlineData/mmGameMstarWqvga.cbm",
        "JHOnlineData/mmBattleMstarWqvga.cbm",
        "JHOnlineData/mmTitleMstarWqvga.cbm",
    };
    for (u32 i = 0; i < sizeof(payloadPaths) / sizeof(payloadPaths[0]); ++i)
    {
        u32 len = vm_net_mock_load_response_file(payloadPaths[i], out, outCap);
        if (len == 0)
            continue;
        if (len < 1024)
        {
            continue;
        }
        if (!vm_net_mock_buffer_has_nonzero(out, len))
        {
            continue;
        }
        return len;
    }
    return 0;
}

static u32 vm_net_mock_actor_resource_index(u8 actorJob, u8 actorSex)
{
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    return (u32)(actorJob - 1u) * 2u + (u32)actorSex;
}

static bool vm_net_mock_put_bytes(u8 *out, u32 outCap, u32 *pos, const void *data, u32 len);
static bool vm_net_mock_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value);
static u8 vm_net_mock_env_u8(const char *name, u8 fallback);
static const char *vm_net_mock_env_str(const char *name, const char *fallback);
static const char *vm_net_mock_actor_preview_image_name(u8 actorJob, u8 actorSex);

static bool vm_net_mock_title_login_requires_server_select(void)
{
    const char *spec = getenv("CBE_LOGIN_REQUIRE_SERVER_SELECT");
    if (spec != NULL && spec[0] == '0' && spec[1] == '\0')
        return false;
    return true;
}

static void vm_net_mock_title_login_phase_reset(const char *reason)
{
    if (g_netMockTitleServerListPending || g_netMockTitleServerSelectConfirmed || g_netMockTitleSelectedServerId)
    {
    }
    g_netMockTitleServerListPending = 0;
    g_netMockTitleServerSelectConfirmed = 0;
    g_netMockBackpackGridSeededRoleId = 0;
    g_netMockTitleServerListTick = 0;
    g_netMockTitleServerSelectTick = 0;
    g_netMockTitleSelectedServerId = 0;
}

static void vm_net_mock_title_login_phase_mark_server_list(void)
{
    g_netMockTitleServerListPending = 1;
    g_netMockTitleServerSelectConfirmed = 0;
    g_netMockTitleServerListTick = g_schedulerTick;
    g_netMockTitleServerSelectTick = 0;
    g_netMockTitleSelectedServerId = 0;
}

static void vm_net_mock_title_login_phase_mark_server_select(u32 serverId)
{
    g_netMockTitleServerListPending = 1;
    g_netMockTitleServerSelectConfirmed = 1;
    g_netMockTitleServerSelectTick = g_schedulerTick;
    g_netMockTitleSelectedServerId = serverId;
}

static const char *vm_net_mock_actor_ui_motion_name(u8 actorJob, u8 actorSex)
{
    static const char *uiActorNames[6] = {
        "ui_h_war.actor",
        "ui_hw_war.actor",
        "ui_h_ass.actor",
        "ui_hw_ass.actor",
        "ui_h_mag.actor",
        "ui_hw_mag.actor",
    };
    u32 tableIndex = vm_net_mock_actor_resource_index(actorJob, actorSex);
    if (tableIndex >= sizeof(uiActorNames) / sizeof(uiActorNames[0]))
        tableIndex = 0;
    return uiActorNames[tableIndex];
}

static bool vm_net_mock_put_le16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    if (*pos + 2 > outCap)
        return false;
    out[(*pos)++] = (u8)(value & 0xff);
    out[(*pos)++] = (u8)(value >> 8);
    return true;
}

static void vm_net_mock_store_le32(u8 *out, u32 offset, u32 value)
{
    out[offset + 0] = (u8)(value & 0xff);
    out[offset + 1] = (u8)((value >> 8) & 0xff);
    out[offset + 2] = (u8)((value >> 16) & 0xff);
    out[offset + 3] = (u8)((value >> 24) & 0xff);
}

static u32 vm_net_mock_build_minimal_actor_motion_resource(u8 *out, u32 outCap)
{
    /*
     * File layout expected by sub_100D48A: little-endian resource payload size,
     * followed by a resource stream.  Type 0xf1 is a mock-only raw DF stream.
     * sub_100D6E2 parses this stream as:
     * skipped dword, width/height, image-entry count, image name, frame bounds,
     * then three optional table counts.  Keep one real image entry so scene
     * rendering owns a valid image object while the unknown tables stay empty.
     */
    u8 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", vm_net_mock_active_role_job_or(1));
    u8 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", vm_net_mock_active_role_sex_or(0));
    const char *innerActor = vm_net_mock_env_str("CBE_ACTOR_MOTION_IMAGE_RESOURCE",
                                                 vm_net_mock_actor_ui_motion_name(actorJob, actorSex));
    u32 innerLen = (u32)strlen(innerActor);
    if (innerLen == 0 || innerLen > 0xff)
        return 0;

    u8 descriptor[128];
    u32 descPos = 0;
    memset(descriptor, 0, sizeof(descriptor));
    if (!vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 67) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 293) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 1) ||
        !vm_net_mock_put_u8(descriptor, sizeof(descriptor), &descPos, (u8)innerLen) ||
        !vm_net_mock_put_bytes(descriptor, sizeof(descriptor), &descPos, (const u8 *)innerActor, innerLen) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 67) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 293) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0) ||
        !vm_net_mock_put_le16(descriptor, sizeof(descriptor), &descPos, 0))
        return 0;

    u32 payloadLen = 1 + descPos;
    u32 fileLen = 4 + payloadLen;
    if (outCap < fileLen)
        return 0;
    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    out[4] = 0xf1;
    memcpy(out + 5, descriptor, descPos);
    return fileLen;
}

static u32 vm_net_mock_build_minimal_actor_image_resource(u8 *out, u32 outCap)
{
    /*
     * sub_100D564 parses this stream as an image-sequence descriptor:
     * image resource names, cell width/height, sheet width/height, then a
     * cell-index table.  The actual bitmap remains the original game GIF.
     */
    static const u8 descriptor[] = {
        0x01, 0x00, 0x00, 0x00,                         /* image resource count */
        0x12, 'h', '_', 'w', 'a', 'r', 'r', 'i', 'o',
        'r', 'w', 'a', 'l', 'k', '2', '.', 'g', 'i', 'f',
        0x43, 0x00, 0x00, 0x00,                         /* cell width */
        0x25, 0x01, 0x00, 0x00,                         /* cell height */
        0x43, 0x00, 0x00, 0x00,                         /* sheet width */
        0x25, 0x01, 0x00, 0x00,                         /* sheet height */
        0x00, 0x00, 0x00, 0x00,                         /* single cell index */
    };
    u32 payloadLen = 1 + (u32)sizeof(descriptor);
    u32 fileLen = 4 + payloadLen;
    if (outCap < fileLen)
        return 0;
    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    out[4] = 0xf1;
    memcpy(out + 5, descriptor, sizeof(descriptor));
    return fileLen;
}

static u32 vm_net_mock_build_len_prefixed_resource_blob(
    const u8 *payload, u32 payloadLen, u8 *out, u32 outCap)
{
    if (payload == NULL || payloadLen == 0 || out == NULL)
        return 0;
    if (payloadLen + 4 > outCap)
        return 0;

    out[0] = (u8)payloadLen;
    out[1] = (u8)(payloadLen >> 8);
    out[2] = (u8)(payloadLen >> 16);
    out[3] = (u8)(payloadLen >> 24);
    memcpy(out + 4, payload, payloadLen);
    return payloadLen + 4;
}

static bool vm_net_mock_string_has_non_ascii(const char *text)
{
    if (text == NULL)
        return false;
    while (*text)
    {
        if ((u8)*text >= 0x80)
            return true;
        ++text;
    }
    return false;
}

static bool vm_net_mock_named_payload_looks_like_resource_stream(
    const char *payloadName, u8 requestType, const u8 *payload, u32 payloadLen)
{
    u32 declaredLen = 0;

    if (payload == NULL || payloadLen == 0)
        return false;
    if (requestType != 1)
        return true;
    if (payloadName == NULL || payloadName[0] == 0)
        return true;
    if (strchr(payloadName, '.') != NULL)
        return true;
    if (!vm_net_mock_string_has_non_ascii(payloadName))
        return true;
    if (payloadLen < 4)
        return false;

    declaredLen = (u32)payload[0] |
                  ((u32)payload[1] << 8) |
                  ((u32)payload[2] << 16) |
                  ((u32)payload[3] << 24);
    return declaredLen > 0 && declaredLen <= payloadLen - 4;
}

static u32 vm_net_mock_load_named_update_payload(const char *payloadName, u8 *out, u32 outCap)
{
    if (payloadName == NULL || payloadName[0] == 0 || out == NULL || outCap == 0)
        return 0;

    char path[384];
    snprintf(path, sizeof(path), "JHOnlineData/%s", payloadName);
    return vm_net_mock_load_response_file(path, out, outCap);
}

static u32 vm_net_mock_load_resource_update_payload(
    const u8 *request, u32 requestLen, const char *requestedName, u8 *out, u32 outCap, const char **payloadName)
{
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);

    if (payloadName)
        *payloadName = (requestedName && requestedName[0]) ? requestedName : "MMORPGTempcbm";

    if (requestedName && requestedName[0])
    {
        if (strcmp(requestedName, "c00PenglaiXiandao_01") == 0)
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);

        if (strcmp(requestedName, "c_mock_missing_motion.actor") == 0 ||
            strcmp(requestedName, "mock_missing_motion.actor") == 0)
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);

        if (strcmp(requestedName, "mock_actor_image.gif") == 0)
            return vm_net_mock_build_minimal_actor_image_resource(out, outCap);

        u32 namedLen = vm_net_mock_load_named_update_payload(requestedName, out, outCap);
        if (namedLen > 0)
        {
            if (vm_net_mock_named_payload_looks_like_resource_stream(requestedName,
                                                                     haveRequestType ? requestType : 0,
                                                                     out,
                                                                     namedLen))
                return namedLen;
        }

        if ((!haveRequestType || requestType == 1) && outCap > 4)
        {
            u32 fallbackLen = vm_net_mock_load_update_payload(out + 4, outCap - 4);
            if (fallbackLen > 0)
            {
                u32 wrappedLen = vm_net_mock_build_len_prefixed_resource_blob(out + 4, fallbackLen, out, outCap);
                if (wrappedLen > 0)
                {
                    return wrappedLen;
                }
            }
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "c_mock_missing_motion.actor") ||
        vm_net_mock_request_contains(request, requestLen, "mock_missing_motion.actor"))
    {
        if (payloadName)
            *payloadName = "c_mock_missing_motion.actor";
        return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);
    }

    if (vm_net_mock_request_contains(request, requestLen, "mock_actor_image.gif"))
    {
        if (payloadName)
            *payloadName = "mock_actor_image.gif";
        return vm_net_mock_build_minimal_actor_image_resource(out, outCap);
    }

    return vm_net_mock_load_update_payload(out, outCap);
}

static void vm_net_mock_save_tempdata(const u8 *data, u32 len)
{
    if (data == NULL || len == 0)
        return;
    FILE *fp = fopen("JHOnlineData/mmorpgTempdata", "wb");
    if (fp == NULL)
        return;
    fwrite(data, 1, len, fp);
    u8 payload[65536];
    u32 payloadLen = vm_net_mock_load_update_payload(payload, sizeof(payload));
    if (payloadLen > 0)
    {
        fwrite(payload, 1, payloadLen, fp);
        if (payloadLen < sizeof(payload))
        {
            static const u8 zeros[256] = {0};
            u32 remain = (u32)sizeof(payload) - payloadLen;
            while (remain > 0)
            {
                u32 chunk = remain > sizeof(zeros) ? (u32)sizeof(zeros) : remain;
                fwrite(zeros, 1, chunk, fp);
                remain -= chunk;
            }
        }
    }
    fclose(fp);
}




static void vm_net_log_handled_packet(const char *source, const u8 *request, u32 requestLen, u32 responseLen)
{
    (void)request;
    (void)requestLen;
    snprintf(g_netLastHandledSource, sizeof(g_netLastHandledSource), "%s", source ? source : "unknown");
    g_netLastHandledSummary[0] = 0;
    g_netLastHandledResponseLen = responseLen;
    g_netLastHandledValid = 1;
}

static void vm_net_log_unhandled_packet(const u8 *request, u32 requestLen)
{
    u8 wtKind = 0;
    u8 wtSubtype = 0;
    u8 objectCount = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    char objects[160] = {0};
    u32 used = 0;
    u32 seen = 0;

    if (request && requestLen >= 5)
        objectCount = request[4];
    if (vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &wtKind, &wtSubtype))
    {
        while (seen < 4 && vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        {
            int wrote = snprintf(objects + used, sizeof(objects) - used,
                                 "%s%u/%u/%u:%u",
                                 seen ? "," : "",
                                 object.major, object.kind, object.subtype,
                                 object.payloadLen);
            if (wrote < 0 || (u32)wrote >= sizeof(objects) - used)
                break;
            used += (u32)wrote;
            ++seen;
        }
        printf("[error][network] unhandled wt=%u/%u len=%u objects=%u first=%s last_source=%s last_resp=%u\n",
               wtKind, wtSubtype, requestLen, objectCount,
               objects[0] ? objects : "-",
               g_netLastHandledValid ? g_netLastHandledSource : "-",
               g_netLastHandledResponseLen);
    }
    else
    {
        printf("[error][network] unhandled malformed len=%u last_source=%s last_resp=%u\n",
               requestLen,
               g_netLastHandledValid ? g_netLastHandledSource : "-",
               g_netLastHandledResponseLen);
    }
}

static bool vm_net_try_read_ascii(u32 ptr, u8 *out, u32 outCap)
{
    if (ptr == 0 || out == NULL || outCap == 0)
        return false;

    memset(out, 0, outCap);
    for (u32 i = 0; i + 1 < outCap; ++i)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, ptr + i, &ch, 1) != UC_ERR_OK)
            return false;
        if (ch == 0)
        {
            out[i] = 0;
            return i > 0;
        }
        if (ch < 0x20 || ch > 0x7e)
            return false;
        out[i] = ch;
    }
    out[outCap - 1] = 0;
    return false;
}

static void vm_net_read_guest_ascii_label(u32 ptr, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (ptr == 0)
    {
        snprintf(out, outCap, "<null>");
        return;
    }
    if (vm_net_try_read_ascii(ptr, (u8 *)out, (u32)outCap))
        return;
    snprintf(out, outCap, "<nonascii:%08x>", ptr);
}

static void vm_net_read_guest_gbk_label(u32 ptr, char *out, size_t outCap)
{
    u8 raw[64];
    size_t i = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (ptr == 0)
    {
        snprintf(out, outCap, "<null>");
        return;
    }
    memset(raw, 0, sizeof(raw));
    if (uc_mem_read(MTK, ptr, raw, sizeof(raw) - 1) != UC_ERR_OK)
    {
        snprintf(out, outCap, "<bad:%08x>", ptr);
        return;
    }
    while (i < sizeof(raw) - 1 && raw[i] != 0)
        i++;
    raw[i] = 0;
    if (i == 0)
    {
        snprintf(out, outCap, "<empty>");
        return;
    }
    gbk_to_utf8(raw, out, outCap);
}

static bool vm_net_read_guest_raw_cstr(u32 ptr, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (ptr == 0)
        return false;

    for (size_t i = 0; i + 1 < outCap; ++i)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, ptr + (u32)i, &ch, 1) != UC_ERR_OK)
        {
            out[0] = 0;
            return false;
        }
        out[i] = (char)ch;
        if (ch == 0)
            return i > 0;
    }
    out[outCap - 1] = 0;
    return out[0] != 0;
}

static void vm_net_read_guest_best_effort_label(u32 ptr, char *out, size_t outCap)
{
    char asciiBuf[96];

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;

    vm_net_read_guest_ascii_label(ptr, asciiBuf, sizeof(asciiBuf));
    if (strncmp(asciiBuf, "<nonascii:", 10) == 0)
    {
        vm_net_read_guest_gbk_label(ptr, out, outCap);
        if (out[0] != 0)
            return;
    }
    snprintf(out, outCap, "%s", asciiBuf);
}

static void vm_net_read_active_ui_name(char *out, size_t outCap)
{
    u32 uiObj = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (!Global_R9)
    {
        snprintf(out, outCap, "<no-r9>");
        return;
    }
    if (uc_mem_read(MTK, Global_R9 + 21676, &uiObj, 4) != UC_ERR_OK || uiObj == 0)
    {
        snprintf(out, outCap, "<no-ui>");
        return;
    }
    vm_net_read_guest_ascii_label(uiObj + 1141, out, outCap);
}

















































static void vm_fixup_current_node_visual_res_if_ready(u32 pc, const char *label)
{
    static u32 s_sceneVisualResFixupLimitCount = 0;
    static u32 s_sceneVisualResMissingLimitCount = 0;
    static u32 s_sceneVisualResProbeLimitCount = 0;
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 visualResId = 0;
    u32 tableEntry = 0;
    u32 selectedIndex = 0;
    u32 slotEntries[6] = {0};
    u8 visualGroup = 0;
    u8 visualVariant = 0;

    if (!Global_R9)
    {
        if (s_sceneVisualResProbeLimitCount < 24)
        {
            ++s_sceneVisualResProbeLimitCount;
        }
        return;
    }

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    if (!currentSceneNode)
    {
        if (s_sceneVisualResProbeLimitCount < 24)
        {
            ++s_sceneVisualResProbeLimitCount;
        }
        return;
    }

    uc_mem_read(MTK, currentSceneNode + 0x24, &visualResId, 4);
    uc_mem_read(MTK, currentSceneNode + 0x140, &visualGroup, 1);
    uc_mem_read(MTK, currentSceneNode + 0x141, &visualVariant, 1);
    if (visualGroup == 0 || visualGroup > 2 || visualVariant > 2)
    {
        if (s_sceneVisualResProbeLimitCount < 24)
        {
            ++s_sceneVisualResProbeLimitCount;
        }
        return;
    }

    selectedIndex = (u32)visualGroup + ((u32)visualVariant << 1);
    uc_mem_read(MTK, currentSceneNode + 0x00, &slotEntries[0], sizeof(slotEntries));
    uc_mem_read(MTK, currentSceneNode + 4 * (selectedIndex - 1), &tableEntry, 4);
    if (s_sceneVisualResProbeLimitCount < 24)
    {
        ++s_sceneVisualResProbeLimitCount;
    }

    if (visualResId == 0 && tableEntry != 0)
    {
        uc_mem_write(MTK, currentSceneNode + 0x24, &tableEntry, 4);
        if (s_sceneVisualResFixupLimitCount < 24)
        {
            ++s_sceneVisualResFixupLimitCount;
        }
    }
    else if (visualResId == 0 && tableEntry == 0 && s_sceneVisualResMissingLimitCount < 24)
    {
        ++s_sceneVisualResMissingLimitCount;
    }
}









static bool vm_should_skip_portal_move_entry_append(u32 pc)
{
    static u32 s_portalMoveEntrySkipLimitCount = 0;
    u32 tableBasePtr = 0;
    u32 entryCount = 0;
    u32 entryPtr = 0;
    u32 lr = 0;
    u16 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 boxLeft = 0;
    u16 boxTop = 0;
    u16 boxRight = 0;
    u16 boxBottom = 0;
    u8 kind = 0;

    if (!Global_R9)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5CE4, &tableBasePtr, 4) != UC_ERR_OK || tableBasePtr == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5D40, &entryCount, 4) != UC_ERR_OK)
        return false;

    entryPtr = tableBasePtr + entryCount * 32u;
    uc_mem_read(MTK, entryPtr + 0x00, &actorId, 2);
    uc_mem_read(MTK, entryPtr + 0x02, &gridX, 2);
    uc_mem_read(MTK, entryPtr + 0x04, &gridY, 2);
    uc_mem_read(MTK, entryPtr + 0x06, &boxLeft, 2);
    uc_mem_read(MTK, entryPtr + 0x08, &boxTop, 2);
    uc_mem_read(MTK, entryPtr + 0x0A, &boxRight, 2);
    uc_mem_read(MTK, entryPtr + 0x0C, &boxBottom, 2);
    uc_mem_read(MTK, entryPtr + 0x17, &kind, 1);

    if (actorId != 1 ||
        gridX != 223 ||
        gridY != 382 ||
        boxLeft != 203 ||
        boxTop != 402 ||
        boxRight != 240 ||
        boxBottom != 422 ||
        kind != 2)
    {
        return false;
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (s_portalMoveEntrySkipLimitCount < 16)
    {
        ++s_portalMoveEntrySkipLimitCount;
    }
    return true;
}



















static void vm_scene_seed_status_meter_displaymax_fallback(u32 pc)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 statusMeterNode = 0;
    u32 n2 = 0;
    u32 sourceHead = 0;
    u32 currentPrimaryBaseMax = 0;
    u32 currentSecondaryBaseMax = 0;
    u32 currentPrimaryDisplayMax = 0;
    u32 currentSecondaryDisplayMax = 0;
    u32 meterPrimaryDisplayMax = 0;
    u32 meterSecondaryDisplayMax = 0;

    if (pc != 0x100FF26 || !Global_R9)
        return;

    uc_reg_read(MTK, UC_ARM_REG_R6, &n2);
    if (n2 != 1)
        return;

    hudState = Global_R9 + 0x5C64;
    uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4);
    uc_mem_read(MTK, hudState + 0x48, &statusMeterNode, 4);
    uc_mem_read(MTK, Global_R9 + 0x6048, &sourceHead, 4);

    if (!currentSceneNode || !statusMeterNode || sourceHead != 0)
        return;

    uc_mem_read(MTK, currentSceneNode + 0xBC, &currentPrimaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC0, &currentSecondaryBaseMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC4, &currentPrimaryDisplayMax, 4);
    uc_mem_read(MTK, currentSceneNode + 0xC8, &currentSecondaryDisplayMax, 4);
    uc_mem_read(MTK, statusMeterNode + 0xC4, &meterPrimaryDisplayMax, 4);
    uc_mem_read(MTK, statusMeterNode + 0xC8, &meterSecondaryDisplayMax, 4);

    if (meterPrimaryDisplayMax != 0 || meterSecondaryDisplayMax != 0)
        return;
    if (currentPrimaryBaseMax == 0 && currentSecondaryBaseMax == 0)
        return;

    uc_mem_write(MTK, statusMeterNode + 0xC4, &currentPrimaryBaseMax, 4);
    uc_mem_write(MTK, statusMeterNode + 0xC8, &currentSecondaryBaseMax, 4);
    uc_mem_write(MTK, currentSceneNode + 0xC4, &currentPrimaryBaseMax, 4);
    uc_mem_write(MTK, currentSceneNode + 0xC8, &currentSecondaryBaseMax, 4);

}


static bool vm_is_scene_bootstrap_loading_overlay_caller(u32 lr)
{
    switch (lr & ~1u)
    {
    case 0x100F826:
    case 0x100F89C:
    case 0x101359C:
    case 0x101366A:
    case 0x10136BC:
    case 0x1013776:
    case 0x10137B0:
    case 0x10137BE:
    case 0x10137E4:
    case 0x10137F6:
        return true;
    default:
        return false;
    }
}


































static void hook_vm_pool_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
    (void)size;
    (void)user_data;

    u32 pc = (u32)address & ~1u;
    vm_restore_main_r9_on_code_boundary(pc);
    u32 moduleR9 = g_currentScreenModuleBase;
    u32 inferredCodeBase = 0;
    u32 inferredModuleR9 = 0;
    u32 loaderModuleR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
    if (loaderModuleR9 == 0)
        loaderModuleR9 = g_currentScreenModuleBase;

    if (pc >= 0x05080000 && pc < 0x05094000)
        moduleR9 = loaderModuleR9;
    else if (pc >= 0x05094000 && pc < 0x050A8000)
        moduleR9 = loaderModuleR9;
    else if (pc >= 0x05181F20 && pc < 0x05195464)
        moduleR9 = loaderModuleR9;
    else if (vm_infer_battle_module_from_screen(vmAddedScreen, &inferredCodeBase, &inferredModuleR9) &&
             pc >= inferredCodeBase && pc < inferredModuleR9)
        moduleR9 = loaderModuleR9;

    if (moduleR9 != 0)
    {
        u32 currentR9 = 0;
        uc_reg_read(uc, UC_ARM_REG_R9, &currentR9);
        if (currentR9 != moduleR9)
            uc_reg_write(uc, UC_ARM_REG_R9, &moduleR9);
    }

    vm_note_battle_mp_pc(pc);
}


static u32 vm_net_mock_build_response_from_rules(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    static const vm_net_mock_rule rules[] = {
        /* 服务器版本握手。可用 bin/net_mocks/version-handshake.bin 覆盖响应体。 */
        {"version-handshake", "version", "net_mocks/version-handshake.bin", NULL, 0},
    };

    for (u32 i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i)
    {
        const vm_net_mock_rule *rule = &rules[i];
        if (!vm_net_mock_request_contains(request, requestLen, rule->contains))
            continue;
        u32 fileLen = vm_net_mock_load_response_file(rule->responseFile, out, outCap);
        if (fileLen)
        {
            return fileLen;
        }
        if (rule->response && rule->responseLen)
        {
            return vm_net_mock_copy_response(rule->response, rule->responseLen, out, outCap);
        }
        break;
    }
    return 0;
}

static bool vm_net_mock_put_bytes(u8 *out, u32 outCap, u32 *pos, const void *data, u32 len)
{
    if (*pos + len > outCap)
        return false;
    if (len)
        memcpy(out + *pos, data, len);
    *pos += len;
    return true;
}

static bool vm_net_mock_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value)
{
    return vm_net_mock_put_bytes(out, outCap, pos, &value, 1);
}

static bool vm_net_mock_put_be16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    u8 bytes[2] = {(u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

static bool vm_net_mock_put_be32(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    u8 bytes[4] = {(u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

static bool vm_net_mock_put_name(u8 *out, u32 outCap, u32 *pos, const char *name)
{
    u32 nameLen = (u32)strlen(name) + 1;
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen - 1) &&
           vm_net_mock_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_put_int_field(u8 *out, u32 outCap, u32 *pos, const char *name, u32 value)
{
    return vm_net_mock_put_name(out, outCap, pos, name) &&
           vm_net_mock_put_u8(out, outCap, pos, 0x06) &&
           vm_net_mock_put_be16(out, outCap, pos, 4) &&
           vm_net_mock_put_be32(out, outCap, pos, value);
}

static bool vm_net_mock_put_string_field(u8 *out, u32 outCap, u32 *pos, const char *name, const char *value)
{
    u32 valueLen = (u32)strlen(value);
    return vm_net_mock_put_name(out, outCap, pos, name) &&
           vm_net_mock_put_u8(out, outCap, pos, 0x10) &&
           vm_net_mock_put_be16(out, outCap, pos, (u16)valueLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, value, valueLen);
}

static bool vm_net_mock_put_object_entry(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *value, u16 valueLen)
{
    u32 nameLen = (u32)strlen(name);
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen) &&
           vm_net_mock_put_be16(out, outCap, pos, valueLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, value, valueLen);
}

static bool vm_net_mock_put_object_u8(u8 *out, u32 outCap, u32 *pos, const char *name, u8 value)
{
    u8 encoded[] = {0x00, 0x01, value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_ascii_digit(u8 *out, u32 outCap, u32 *pos, const char *name, u8 digit)
{
    return vm_net_mock_put_object_u8(out, outCap, pos, name, (u8)('0' + digit));
}

static bool vm_net_mock_put_object_u16(u8 *out, u32 outCap, u32 *pos, const char *name, u16 value)
{
    u8 encoded[] = {0x00, 0x02, (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_u32(u8 *out, u32 outCap, u32 *pos, const char *name, u32 value)
{
    u8 encoded[] = {0x00, 0x04, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_object_entry(out, outCap, pos, name, encoded, sizeof(encoded));
}

static bool vm_net_mock_put_object_blob(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *data, u16 dataLen)
{
    u32 nameLen = (u32)strlen(name);
    u16 valueLen = dataLen + 2;
    if (nameLen > 0xff)
        return false;
    return vm_net_mock_put_u8(out, outCap, pos, (u8)nameLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, name, nameLen) &&
           vm_net_mock_put_be16(out, outCap, pos, valueLen) &&
           vm_net_mock_put_be16(out, outCap, pos, dataLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, data, dataLen);
}

static bool vm_net_mock_put_object_raw(u8 *out, u32 outCap, u32 *pos, const char *name, const u8 *data, u16 dataLen)
{
    return vm_net_mock_put_object_entry(out, outCap, pos, name, data, dataLen);
}

static bool vm_net_mock_put_object_string(u8 *out, u32 outCap, u32 *pos, const char *name, const char *value)
{
    return vm_net_mock_put_object_blob(out, outCap, pos, name, (const u8 *)value, (u16)strlen(value));
}

static bool vm_net_mock_begin_wt_object(u8 *out, u32 outCap, u32 *pos, u8 major, u8 kind, u8 subtype, u32 *objectStart)
{
    if (*pos + 6 > outCap)
        return false;
    if (objectStart)
        *objectStart = *pos;
    out[(*pos)++] = major;
    out[(*pos)++] = kind;
    out[(*pos)++] = subtype;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    return true;
}

static void vm_net_mock_finish_wt_object(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;
    out[objectStart + 4] = (u8)(objectLen >> 8);
    out[objectStart + 5] = (u8)objectLen;
}

static void vm_net_mock_finish_wt_packet(u8 *out, u32 pos, u8 objectCount)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = objectCount;
}

static u8 vm_net_mock_env_u8(const char *name, u8 fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(spec, &end, 0);
    if (end == spec)
        return fallback;
    return (u8)parsed;
}

static u32 vm_net_mock_env_u32(const char *name, u32 fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(spec, &end, 0);
    if (end == spec)
        return fallback;
    return (u32)parsed;
}

static u32 vm_net_mock_env_u32_if_set(const char *name, u32 fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    return vm_net_mock_env_u32(name, fallback);
}

static const char *vm_net_mock_env_str(const char *name, const char *fallback)
{
    const char *spec = getenv(name);
    if (spec == NULL || spec[0] == 0)
        return fallback;
    return spec;
}

static u32 vm_net_mock_build_version_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 result = 1;
    if (outCap < pos)
        return 0;

    if (g_netMockUpdateDelivered || vm_net_mock_has_installed_update())
    {
        g_netMockUpdateDelivered = 1;
        result = 0;
    }

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, 5, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    u8 objectCount = 1;
    if (result == 0)
    {
        /* CBE parses subtype 5 in handle_version_update_response and subtype 9
         * in startup_update_net_callback.  Keep both objects: removing subtype 9
         * leaves the startup screen stuck after update_state becomes 2.
         */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, 9, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "id", 0))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "code", 0))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 2;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_update_chunk_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    static u8 updateData[131072];
    char requestName[256];
    requestName[0] = 0;
    bool haveRequestName = vm_net_mock_get_object_string_field(request, requestLen, "name", requestName, sizeof(requestName)) &&
                           requestName[0] != 0;
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);
    const char *payloadName = NULL;
    u32 updateLen = vm_net_mock_load_resource_update_payload(request,
                                                             requestLen,
                                                             haveRequestName ? requestName : NULL,
                                                             updateData,
                                                             sizeof(updateData),
                                                             &payloadName);
    if (updateLen == 0)
        return 0;

    u32 start = 0;
    vm_net_mock_get_object_u32_field(request, requestLen, "start", &start);
    if (start >= updateLen)
        start = 0;

    u32 chunkLen = updateLen - start;
    if (chunkLen > 0x1000)
        chunkLen = 0x1000;
    u32 crc = vm_net_mock_signed_byte_sum(updateData, start + chunkLen);

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", updateLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", (const u8 *)"\x00\x01", 2))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", haveRequestType ? requestType : 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName ? payloadName : "MMORPGTempcbm"))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", updateData + start, (u16)chunkLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 1);
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 7;
    out[8] = 0;
    vm_net_mock_finish_wt_object(out, 5, pos);
    return pos;
}

static bool vm_net_mock_seq_put_u32(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 4) &&
           vm_net_mock_put_be32(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_u24(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 4) &&
           vm_net_mock_put_u8(out, outCap, pos, (u8)(value >> 16)) &&
           vm_net_mock_put_u8(out, outCap, pos, (u8)(value >> 8)) &&
           vm_net_mock_put_u8(out, outCap, pos, (u8)value);
}

static bool vm_net_mock_seq_put_u8(u8 *out, u32 outCap, u32 *pos, u8 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 1) &&
           vm_net_mock_put_u8(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_i16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    return vm_net_mock_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_put_u8(out, outCap, pos, 2) &&
           vm_net_mock_put_be16(out, outCap, pos, value);
}

static bool vm_net_mock_seq_put_string(u8 *out, u32 outCap, u32 *pos, const char *value)
{
    u16 len = value ? (u16)(strlen(value) + 1) : 1;
    return vm_net_mock_put_be16(out, outCap, pos, len) &&
           vm_net_mock_put_bytes(out, outCap, pos, value ? value : "", len);
}

static bool vm_net_mock_seq_put_string_list(
    u8 *out, u32 outCap, u32 *pos, const char *const *values, u32 count)
{
    u32 i = 0;
    for (i = 0; i < count; ++i)
    {
        if (!vm_net_mock_seq_put_string(out, outCap, pos, values[i]))
            return false;
    }
    return true;
}

static u32 vm_net_mock_build_pos_info(u8 *out, u32 outCap, u16 x, u16 y)
{
    u32 pos = 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, x))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, y))
        return 0;
    return pos;
}

static bool vm_net_mock_append_books42_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_backpack_items_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_fb_target_empty11_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_fb_target_result12_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneKey, u16 spawnX, u16 spawnY);
static bool vm_net_mock_append_fb_target_result4_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 typeValue, const char *infoText);
static void vm_net_mock_append_preview_u32(char *out, u32 outCap, u32 *pos, u32 value);

enum
{
    VM_NET_MOCK_BACKPACK_CAPACITY = 40,
    VM_NET_MOCK_BACKPACK_MAX_ITEMS = 40,
    VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID = 800,
    VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_SEQ = 1,
    VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_COUNT = 5,
    VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE = 1,
    VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK = 99,
    VM_NET_MOCK_SHOP_PAGE_SIZE = 10,
    VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS = 10,
    VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS = 2048,
    VM_NET_MOCK_ITEM_EFFECT_CATALOG_MAX_ITEMS = 2048,
    VM_NET_MOCK_SKILL_CATALOG_MAX_ITEMS = 256,
    VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS = 64,
    VM_NET_MOCK_SHOP_NAME_BYTES = 12,
    VM_NET_MOCK_SKILL_NAME_BYTES = 24,
    VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID = 1,
    VM_NET_MOCK_SCENE_LANDING_SAFE_GAP = 32,
    VM_NET_MOCK_ROLE_DB_MAX_ROLES = 5,
    VM_NET_MOCK_ROLE_DB_LEGACY_VERSION = 1,
    VM_NET_MOCK_ROLE_DB_BACKPACK_VERSION = 2,
    VM_NET_MOCK_ROLE_DB_VERSION = 3,
    VM_NET_MOCK_ROLE_EXP_PER_LEVEL = 100,
    VM_NET_MOCK_EQUIP_SLOT_COUNT = 8,
    VM_NET_MOCK_EQUIP_CATALOG_MAX_ITEMS = 2048,
    VM_NET_MOCK_BATTLE_POISON_SLIME_ID = 105,
    VM_NET_MOCK_BATTLE_POISON_SLIME_EXP = 5,
    VM_NET_MOCK_BATTLE_POISON_SLIME_GOLD = 5,
    VM_NET_MOCK_BATTLE_CHANGMING_SAN_ITEM_ID = 304,
    VM_NET_MOCK_BATTLE_CHANGMING_SAN_DROP_RATE = 10,
    VM_NET_MOCK_ROLE_DEFAULT_ID = 10001,
    VM_NET_MOCK_ROLE_DEFAULT_HP = 120,
    VM_NET_MOCK_ROLE_DEFAULT_MP = 100,
    VM_NET_MOCK_ROLE_DEFAULT_MONEY = 1000,
    VM_NET_MOCK_ROLE_INITIAL_X = 223,
    VM_NET_MOCK_ROLE_INITIAL_Y = 382
};

typedef struct
{
    u32 itemId;
    u16 seq;
    u16 reserved0;
    u32 count;
} vm_net_mock_backpack_item_state;

typedef struct
{
    u32 roleId;
    char name[32];
    u8 job;
    u8 sex;
    u8 backpackCapacity;
    u8 reserved0;
    u32 level;
    u32 exp;
    u32 hp;
    u32 hpMax;
    u32 mp;
    u32 mpMax;
    u32 money;
    char scene[64];
    u16 x;
    u16 y;
    u8 backpackItemCount;
    u8 reserved1;
    u16 nextBackpackSeq;
    u32 equippedItemIds[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    vm_net_mock_backpack_item_state backpackItems[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
} vm_net_mock_role_state;

typedef struct
{
    u32 roleId;
    char name[32];
    u8 job;
    u8 sex;
    u8 backpackCapacity;
    u8 reserved0;
    u32 level;
    u32 exp;
    u32 hp;
    u32 hpMax;
    u32 mp;
    u32 mpMax;
    u32 money;
    char scene[64];
    u16 x;
    u16 y;
    u8 backpackItemCount;
    u8 reserved1;
    u16 nextBackpackSeq;
    vm_net_mock_backpack_item_state backpackItems[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
} vm_net_mock_role_state_v2;

typedef struct
{
    u32 roleId;
    char name[32];
    u8 job;
    u8 sex;
    u8 backpackCapacity;
    u8 reserved0;
    u32 level;
    u32 exp;
    u32 hp;
    u32 hpMax;
    u32 mp;
    u32 mpMax;
    u32 money;
    char scene[64];
    u16 x;
    u16 y;
} vm_net_mock_role_state_v1;

typedef struct
{
    char magic[4];
    u32 version;
    u32 activeRoleId;
    u32 roleCount;
    vm_net_mock_role_state_v1 roles[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
} vm_net_mock_role_db_file_v1;

typedef struct
{
    char magic[4];
    u32 version;
    u32 activeRoleId;
    u32 roleCount;
    vm_net_mock_role_state_v2 roles[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
} vm_net_mock_role_db_file_v2;

typedef struct
{
    char magic[4];
    u32 version;
    u32 activeRoleId;
    u32 roleCount;
    vm_net_mock_role_state roles[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
} vm_net_mock_role_db_file;

typedef struct
{
    u32 hp;
    u32 mp;
    u32 attack;
    u32 armor;
    u32 strength;
    u32 agility;
    u32 wisdom;
    u32 crit;
    u32 hit;
    u32 dodge;
    u32 resist;
} vm_net_mock_equipment_bonus;

typedef struct
{
    u32 level;
    u32 job;
    u32 baseStrength;
    u32 baseAgility;
    u32 baseWisdom;
    u32 baseEndurance;
    u32 baseCharm;
    u32 strength;
    u32 agility;
    u32 wisdom;
    u32 endurance;
    u32 charm;
    u32 maxHp;
    u32 maxMp;
    u32 attack;
    u32 defense;
    u32 hit;
    u32 dodge;
    u32 crit;
    u32 resist;
    vm_net_mock_equipment_bonus equipment;
} vm_net_mock_player_stats;

static vm_net_mock_role_state *vm_net_mock_active_role(void);
static u32 vm_net_mock_role_level_from_exp(u32 exp);
static u32 vm_net_mock_role_next_level_start_exp(u32 exp);
static u32 vm_net_mock_role_exp_percent(u32 exp);
static u32 vm_net_mock_role_last_level_exp(u32 exp);
static void vm_net_mock_role_db_save(const char *reason);
static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item(vm_net_mock_role_state *role,
                                                                            u32 itemId,
                                                                            u16 seq);
static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item_by_effect(vm_net_mock_role_state *role,
                                                                                      u32 hp,
                                                                                      u32 mp,
                                                                                      u32 exp);
static bool vm_net_mock_role_consume_backpack_item(vm_net_mock_role_state *role,
                                                   u32 itemId,
                                                   u16 seq,
                                                   u32 count,
                                                   u32 *remainingOut);
static void vm_net_mock_role_sync_derived_vitals(vm_net_mock_role_state *role);
static void vm_net_mock_role_build_player_stats(const vm_net_mock_role_state *role,
                                                vm_net_mock_player_stats *stats);
static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap);

static u8 vm_net_mock_role_backpack_count(const vm_net_mock_role_state *role)
{
    u32 count = 0;
    if (role == NULL)
        return 0;
    count = role->backpackItemCount;
    if (count > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        count = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    if (count > role->backpackCapacity)
        count = role->backpackCapacity;
    return (u8)count;
}

static u8 vm_net_mock_backpack_stack_byte(const vm_net_mock_backpack_item_state *item)
{
    if (item == NULL || item->count == 0)
        return 0;
    return item->count > 255 ? 255 : (u8)item->count;
}

typedef struct
{
    u32 itemId;
    char name[VM_NET_MOCK_SHOP_NAME_BYTES + 1];
    u32 price;
    u32 stock;
    u8 stack;
    u8 visual;
} vm_net_mock_shop_catalog_item;

typedef struct
{
    u32 itemId;
    u8 slot;
    u8 levelRequired;
    u16 reserved0;
    vm_net_mock_equipment_bonus bonus;
} vm_net_mock_equipment_catalog_item;

typedef struct
{
    u32 itemId;
    u8 category;
    u8 levelRequired;
    u8 stack;
    u8 reserved0;
    u32 hp;
    u32 mp;
    u32 exp;
} vm_net_mock_item_effect_catalog_item;

typedef struct
{
    u32 skillId;
    u32 effectIndex;
    u32 mpCost;
    int32_t hpChange;
    u32 strengthCoeff;
    u32 agilityCoeff;
    u32 wisdomCoeff;
    u8 job;
    u8 levelRequired;
    char name[VM_NET_MOCK_SKILL_NAME_BYTES + 1];
} vm_net_mock_skill_catalog_item;

static vm_net_mock_shop_catalog_item g_vm_net_mock_shop_catalog[VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS];
static u32 g_vm_net_mock_shop_catalog_count = 0;
static bool g_vm_net_mock_shop_catalog_loaded = false;
static vm_net_mock_equipment_catalog_item g_vm_net_mock_equipment_catalog[VM_NET_MOCK_EQUIP_CATALOG_MAX_ITEMS];
static u32 g_vm_net_mock_equipment_catalog_count = 0;
static bool g_vm_net_mock_equipment_catalog_loaded = false;
static vm_net_mock_item_effect_catalog_item g_vm_net_mock_item_effect_catalog[VM_NET_MOCK_ITEM_EFFECT_CATALOG_MAX_ITEMS];
static u32 g_vm_net_mock_item_effect_catalog_count = 0;
static bool g_vm_net_mock_item_effect_catalog_loaded = false;
static vm_net_mock_skill_catalog_item g_vm_net_mock_skill_catalog[VM_NET_MOCK_SKILL_CATALOG_MAX_ITEMS];
static u32 g_vm_net_mock_skill_catalog_count = 0;
static bool g_vm_net_mock_skill_catalog_loaded = false;

static u32 vm_net_mock_shop_catalog_group(u32 itemId)
{
    if (itemId >= 1000)
        return 0;
    if (itemId >= 800 && itemId < 1000)
        return 1;
    return 2;
}

static int vm_net_mock_compare_shop_catalog_items(const void *lhs, const void *rhs)
{
    const vm_net_mock_shop_catalog_item *a = (const vm_net_mock_shop_catalog_item *)lhs;
    const vm_net_mock_shop_catalog_item *b = (const vm_net_mock_shop_catalog_item *)rhs;
    u32 groupA = vm_net_mock_shop_catalog_group(a->itemId);
    u32 groupB = vm_net_mock_shop_catalog_group(b->itemId);

    if (groupA != groupB)
        return groupA < groupB ? -1 : 1;
    if (a->itemId != b->itemId)
        return a->itemId < b->itemId ? -1 : 1;
    return 0;
}

static void vm_net_mock_sort_shop_catalog(void)
{
    if (g_vm_net_mock_shop_catalog_count > 1)
    {
        qsort(g_vm_net_mock_shop_catalog,
              g_vm_net_mock_shop_catalog_count,
              sizeof(g_vm_net_mock_shop_catalog[0]),
              vm_net_mock_compare_shop_catalog_items);
    }
}

static u32 vm_net_mock_shop_safe_name_len(const u8 *name, u32 nameLen, u32 cap)
{
    u32 pos = 0;
    if (name == NULL)
        return 0;
    while (pos < nameLen && pos < cap)
    {
        if (name[pos] < 0x80)
        {
            ++pos;
        }
        else if (pos + 1 < nameLen && pos + 2 <= cap)
        {
            pos += 2;
        }
        else
        {
            break;
        }
    }
    return pos;
}

static u32 vm_net_mock_read_le32_at(const u8 *data, u32 off)
{
    return (u32)data[off] |
           ((u32)data[off + 1] << 8) |
           ((u32)data[off + 2] << 16) |
           ((u32)data[off + 3] << 24);
}

static u32 vm_net_mock_parse_dsh_u32(const u8 *raw, u32 len, u32 fallback)
{
    u32 value = 0;
    bool haveDigit = false;

    if (raw == NULL || len == 0)
        return fallback;
    for (u32 i = 0; i < len; ++i)
    {
        if (raw[i] == '-' && !haveDigit)
            return fallback;
        if (raw[i] < '0' || raw[i] > '9')
            break;
        haveDigit = true;
        value = value * 10u + (u32)(raw[i] - '0');
    }
    return haveDigit ? value : fallback;
}

static int32_t vm_net_mock_parse_dsh_s32(const u8 *raw, u32 len, int32_t fallback)
{
    int32_t sign = 1;
    int32_t value = 0;
    bool haveDigit = false;
    u32 pos = 0;

    if (raw == NULL || len == 0)
        return fallback;
    if (raw[pos] == '-')
    {
        sign = -1;
        ++pos;
    }
    else if (raw[pos] == '+')
    {
        ++pos;
    }
    for (; pos < len; ++pos)
    {
        if (raw[pos] < '0' || raw[pos] > '9')
            break;
        haveDigit = true;
        value = value * 10 + (int32_t)(raw[pos] - '0');
    }
    return haveDigit ? value * sign : fallback;
}

static int vm_net_mock_compare_skill_catalog_items(const void *lhs, const void *rhs)
{
    const vm_net_mock_skill_catalog_item *a = (const vm_net_mock_skill_catalog_item *)lhs;
    const vm_net_mock_skill_catalog_item *b = (const vm_net_mock_skill_catalog_item *)rhs;

    if (a->job != b->job)
        return a->job < b->job ? -1 : 1;
    if (a->levelRequired != b->levelRequired)
        return a->levelRequired < b->levelRequired ? -1 : 1;
    if (a->skillId != b->skillId)
        return a->skillId < b->skillId ? -1 : 1;
    return 0;
}

static void vm_net_mock_sort_skill_catalog(void)
{
    if (g_vm_net_mock_skill_catalog_count > 1)
    {
        qsort(g_vm_net_mock_skill_catalog,
              g_vm_net_mock_skill_catalog_count,
              sizeof(g_vm_net_mock_skill_catalog[0]),
              vm_net_mock_compare_skill_catalog_items);
    }
}

static bool vm_net_mock_add_skill_catalog_item(u32 skillId, u32 rawJob,
                                               u32 levelRequired,
                                               u32 effectIndex,
                                               u32 mpCost,
                                               int32_t hpChange,
                                               u32 strengthCoeff,
                                               u32 agilityCoeff,
                                               u32 wisdomCoeff,
                                               const u8 *name,
                                               u32 nameLen)
{
    vm_net_mock_skill_catalog_item *skill = NULL;
    u32 copyLen = 0;

    if (skillId == 0 ||
        rawJob > 2 ||
        g_vm_net_mock_skill_catalog_count >= VM_NET_MOCK_SKILL_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    skill = &g_vm_net_mock_skill_catalog[g_vm_net_mock_skill_catalog_count++];
    memset(skill, 0, sizeof(*skill));
    skill->skillId = skillId;
    skill->effectIndex = effectIndex;
    skill->mpCost = mpCost;
    skill->hpChange = hpChange;
    skill->strengthCoeff = strengthCoeff;
    skill->agilityCoeff = agilityCoeff;
    skill->wisdomCoeff = wisdomCoeff;
    skill->job = (u8)(rawJob + 1);
    skill->levelRequired = (u8)((levelRequired == 0) ? 1 :
                                (levelRequired > 255 ? 255 : levelRequired));
    copyLen = vm_net_mock_shop_safe_name_len(name, nameLen, VM_NET_MOCK_SKILL_NAME_BYTES);
    if (copyLen > 0)
        memcpy(skill->name, name, copyLen);
    skill->name[copyLen] = 0;
    return true;
}

static u32 vm_net_mock_load_skill_catalog_dsh(const char *path)
{
    static u8 data[32768];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
        return 0;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return added;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return added;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        u32 skillId = 0;
        u32 rawJob = 0xff;
        u32 levelRequired = 1;
        u32 effectIndex = 0;
        u32 mpCost = 0;
        int32_t hpChange = 0;
        u32 strengthCoeff = 0;
        u32 agilityCoeff = 0;
        u32 wisdomCoeff = 0;
        const u8 *name = NULL;
        u32 nameLen = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
                break;
            switch (col)
            {
            case 0:
                skillId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 1:
                name = value;
                nameLen = valueLen;
                break;
            case 4:
                levelRequired = vm_net_mock_parse_dsh_u32(value, valueLen, 1);
                break;
            case 5:
                effectIndex = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 6:
                rawJob = vm_net_mock_parse_dsh_u32(value, valueLen, 0xff);
                break;
            case 12:
                mpCost = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 14:
                hpChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 29:
                strengthCoeff = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 30:
                agilityCoeff = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 31:
                wisdomCoeff = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_skill_catalog_item(skillId, rawJob, levelRequired,
                                               effectIndex,
                                               mpCost,
                                               hpChange,
                                               strengthCoeff,
                                               agilityCoeff,
                                               wisdomCoeff,
                                               name, nameLen))
        {
            ++added;
        }
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_skill_catalog(void)
{
    u32 skillCount = 0;

    if (g_vm_net_mock_skill_catalog_loaded)
        return g_vm_net_mock_skill_catalog_count;

    g_vm_net_mock_skill_catalog_loaded = true;
    g_vm_net_mock_skill_catalog_count = 0;
    skillCount = vm_net_mock_load_skill_catalog_dsh("JHOnlineData/skill.dsh");
    if (skillCount == 0)
        skillCount = vm_net_mock_load_skill_catalog_dsh("bin/JHOnlineData/skill.dsh");

    if (skillCount == 0)
    {
        (void)vm_net_mock_add_skill_catalog_item(1, 0, 1, 1, 10,
                                                -130, 50, 0, 0,
                                                (const u8 *)"\xcd\xf2\xbd\xa3\xd6\xef\xcf\xc9\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(101, 1, 1, 7, 20,
                                                -75, 0, 50, 0,
                                                (const u8 *)"\xb7\xe7\xce\xe8\xc8\xd0\xd0\xd0\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(201, 2, 1, 15, 5,
                                                -30, 0, 0, 110,
                                                (const u8 *)"\xe7\xca\xd1\xd7\xbb\xc3\xb7\xa8\x31",
                                                9);
        printf("[warn][network] mock_skill_catalog fallback=skill.dsh-not-found total=%u\n",
               g_vm_net_mock_skill_catalog_count);
    }
    else
    {
        vm_net_mock_sort_skill_catalog();
        printf("[info][network] mock_skill_catalog total=%u source=skill.dsh\n",
               g_vm_net_mock_skill_catalog_count);
    }
    return g_vm_net_mock_skill_catalog_count;
}

static const vm_net_mock_skill_catalog_item *vm_net_mock_find_skill_catalog_item(u32 skillId)
{
    u32 total = vm_net_mock_load_skill_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_skill_catalog[i].skillId == skillId)
            return &g_vm_net_mock_skill_catalog[i];
    }
    return NULL;
}

static const vm_net_mock_skill_catalog_item *vm_net_mock_battle_operate_skill(u32 operate);

static u32 vm_net_mock_role_learned_skill_limit(u32 level)
{
    if (level == 0)
        level = 1;
    return 1 + level / 5;
}

static u32 vm_net_mock_build_role_learned_skill_blob(const vm_net_mock_role_state *role,
                                                     u8 *out, u32 outCap,
                                                     u8 *learnedCountOut,
                                                     char *previewOut,
                                                     u32 previewCap)
{
    u32 total = vm_net_mock_load_skill_catalog();
    u32 pos = 0;
    u32 learned = 0;
    u32 limit = vm_net_mock_role_learned_skill_limit(role ? role->level : 1);
    u8 job = role ? role->job : 1;
    u32 previewPos = 0;

    if (learnedCountOut)
        *learnedCountOut = 0;
    if (previewOut && previewCap > 0)
        previewOut[0] = 0;
    if (out == NULL || outCap == 0)
        return 0;
    if (job == 0 || job > 3)
        job = 1;
    if (limit > VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS)
        limit = VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS;

    for (u32 i = 0; i < total && learned < limit; ++i)
    {
        const vm_net_mock_skill_catalog_item *skill = &g_vm_net_mock_skill_catalog[i];
        if (skill->job != job)
            continue;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, skill->skillId))
            break;
        if (previewOut && previewCap > 0)
            vm_net_mock_append_preview_u32(previewOut, previewCap, &previewPos, skill->skillId);
        ++learned;
    }

    if (learnedCountOut)
        *learnedCountOut = (u8)learned;
    return pos;
}

static bool vm_net_mock_add_shop_catalog_item(u32 itemId, const u8 *name, u32 nameLen,
                                              u32 price, u32 stock, u8 stack, u8 visual)
{
    vm_net_mock_shop_catalog_item *item = NULL;
    u32 copyLen = 0;

    if (itemId == 0 || name == NULL || nameLen == 0 ||
        g_vm_net_mock_shop_catalog_count >= VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS)
    {
        return false;
    }

    item = &g_vm_net_mock_shop_catalog[g_vm_net_mock_shop_catalog_count++];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    copyLen = vm_net_mock_shop_safe_name_len(name, nameLen, VM_NET_MOCK_SHOP_NAME_BYTES);
    memcpy(item->name, name, copyLen);
    item->name[copyLen] = 0;
    item->price = price ? price : VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    item->stock = stock ? stock : VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK;
    item->stack = stack ? stack : 1;
    item->visual = visual ? visual : 1;
    return true;
}

static u32 vm_net_mock_load_shop_catalog_dsh(const char *path, bool equip)
{
    static u8 data[131072];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
        return 0;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return added;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return added;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        u32 itemId = 0;
        u32 price = 0;
        u32 stock = equip ? 1 : VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK;
        u32 visual = 1;
        const u8 *name = NULL;
        u32 nameLen = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            if (rowPos + valueLen > rowEnd)
                break;

            if (col == 0)
                itemId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 1)
            {
                name = value;
                nameLen = valueLen;
            }
            else if (!equip && col == 3)
                visual = vm_net_mock_parse_dsh_u32(value, valueLen, 1);
            else if ((!equip && col == 8) || (equip && col == 5))
                price = vm_net_mock_parse_dsh_u32(value, valueLen, VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE);
            else if (!equip && col == 10)
                stock = vm_net_mock_parse_dsh_u32(value, valueLen, VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK);

            rowPos += valueLen;
        }

        if (vm_net_mock_add_shop_catalog_item(itemId,
                                             name,
                                             nameLen,
                                             price,
                                             stock,
                                             (u8)(stock > 255 ? 255 : stock),
                                             (u8)(visual > 255 ? 1 : visual)))
        {
            ++added;
        }
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_shop_catalog(void)
{
    u32 itemCount = 0;
    u32 equipCount = 0;

    if (g_vm_net_mock_shop_catalog_loaded)
        return g_vm_net_mock_shop_catalog_count;

    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_catalog_count = 0;

    itemCount = vm_net_mock_load_shop_catalog_dsh("JHOnlineData/item.dsh", false);
    if (itemCount == 0)
        itemCount = vm_net_mock_load_shop_catalog_dsh("bin/JHOnlineData/item.dsh", false);
    equipCount = vm_net_mock_load_shop_catalog_dsh("JHOnlineData/equip.dsh", true);
    if (equipCount == 0)
        equipCount = vm_net_mock_load_shop_catalog_dsh("bin/JHOnlineData/equip.dsh", true);

    if (g_vm_net_mock_shop_catalog_count == 0)
    {
        static const char fallbackName[] = "Teleport Stone";
        printf("[warn][network] mock_shop_catalog fallback=item.dsh/equip.dsh-not-found item=%u\n",
               VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID);
        (void)vm_net_mock_add_shop_catalog_item(VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID,
                                                (const u8 *)fallbackName,
                                                (u32)strlen(fallbackName),
                                                VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE,
                                                VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK,
                                                VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_COUNT,
                                                1);
    }
    else
    {
        vm_net_mock_sort_shop_catalog();
        const vm_net_mock_shop_catalog_item *first = &g_vm_net_mock_shop_catalog[0];
        printf("[info][network] mock_shop_catalog total=%u items=%u equips=%u first=%u source=item.dsh/equip.dsh\n",
               g_vm_net_mock_shop_catalog_count, itemCount, equipCount, first->itemId);
    }

    vm_autotest_note("mock_shop_catalog_loaded total=%u items=%u equips=%u source=item.dsh/equip.dsh\n",
                     g_vm_net_mock_shop_catalog_count, itemCount, equipCount);
    return g_vm_net_mock_shop_catalog_count;
}

static const vm_net_mock_shop_catalog_item *vm_net_mock_find_shop_catalog_item(u32 itemId)
{
    u32 total = vm_net_mock_load_shop_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId == itemId)
            return &g_vm_net_mock_shop_catalog[i];
    }
    return NULL;
}

static u8 vm_net_mock_equipment_slot_for_category(u32 category)
{
    switch (category)
    {
    case 7: /* sword */
    case 8: /* dagger */
    case 9: /* staff */
        return 0;
    case 0:
        return 1; /* helmet */
    case 1:
        return 2; /* chest */
    case 2:
        return 3; /* cloak */
    case 3:
        return 4; /* belt */
    case 4:
        return 5; /* leggings */
    case 5:
        return 6; /* boots */
    case 6:
        return 7; /* ring */
    default:
        return 0xff;
    }
}

static bool vm_net_mock_add_equipment_catalog_item(u32 itemId, u32 levelRequired,
                                                   u32 category,
                                                   const vm_net_mock_equipment_bonus *bonus)
{
    vm_net_mock_equipment_catalog_item *item = NULL;
    u8 slot = vm_net_mock_equipment_slot_for_category(category);

    if (itemId == 0 || bonus == NULL || slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
        g_vm_net_mock_equipment_catalog_count >= VM_NET_MOCK_EQUIP_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    item = &g_vm_net_mock_equipment_catalog[g_vm_net_mock_equipment_catalog_count++];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->slot = slot;
    item->levelRequired = (u8)(levelRequired > 255 ? 255 : levelRequired);
    item->bonus = *bonus;
    return true;
}

static u32 vm_net_mock_load_equipment_catalog_dsh(const char *path)
{
    static u8 data[131072];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
        return 0;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return added;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return added;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        u32 itemId = 0;
        u32 levelRequired = 1;
        u32 category = 0xffffffffu;
        vm_net_mock_equipment_bonus bonus;

        memset(&bonus, 0, sizeof(bonus));
        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            u32 parsed = 0;

            if (rowPos + valueLen > rowEnd)
                break;
            parsed = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            switch (col)
            {
            case 0:
                itemId = parsed;
                break;
            case 3:
                levelRequired = parsed ? parsed : 1;
                break;
            case 7:
                category = parsed;
                break;
            case 8:
                bonus.armor = parsed;
                break;
            case 9:
                bonus.attack = parsed;
                break;
            case 10:
                bonus.hp = parsed;
                break;
            case 11:
                bonus.mp = parsed;
                break;
            case 12:
                bonus.strength = parsed;
                break;
            case 13:
                bonus.agility = parsed;
                break;
            case 14:
                bonus.wisdom = parsed;
                break;
            case 15:
                bonus.crit = parsed;
                break;
            case 16:
                bonus.hit = parsed;
                break;
            case 17:
                bonus.dodge = parsed;
                break;
            case 18:
                bonus.resist = parsed;
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_equipment_catalog_item(itemId, levelRequired, category, &bonus))
            ++added;
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_equipment_catalog(void)
{
    u32 equipCount = 0;

    if (g_vm_net_mock_equipment_catalog_loaded)
        return g_vm_net_mock_equipment_catalog_count;

    g_vm_net_mock_equipment_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog_count = 0;
    equipCount = vm_net_mock_load_equipment_catalog_dsh("JHOnlineData/equip.dsh");
    if (equipCount == 0)
        equipCount = vm_net_mock_load_equipment_catalog_dsh("bin/JHOnlineData/equip.dsh");

    if (equipCount == 0)
    {
        printf("[warn][network] mock_equip_catalog fallback=equip.dsh-not-found\n");
    }
    else
    {
        printf("[info][network] mock_equip_catalog total=%u source=equip.dsh\n",
               g_vm_net_mock_equipment_catalog_count);
    }
    return g_vm_net_mock_equipment_catalog_count;
}

static const vm_net_mock_equipment_catalog_item *vm_net_mock_find_equipment_catalog_item(u32 itemId)
{
    u32 total = vm_net_mock_load_equipment_catalog();

    if (itemId == 0)
        return NULL;
    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_equipment_catalog[i].itemId == itemId)
            return &g_vm_net_mock_equipment_catalog[i];
    }
    return NULL;
}

static bool vm_net_mock_add_item_effect_catalog_item(u32 itemId, u32 category,
                                                     u32 levelRequired, u32 stack,
                                                     u32 hp, u32 mp, u32 exp)
{
    vm_net_mock_item_effect_catalog_item *item = NULL;

    if (itemId == 0 ||
        g_vm_net_mock_item_effect_catalog_count >= VM_NET_MOCK_ITEM_EFFECT_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    item = &g_vm_net_mock_item_effect_catalog[g_vm_net_mock_item_effect_catalog_count++];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->category = (u8)(category > 255 ? 255 : category);
    item->levelRequired = (u8)(levelRequired > 255 ? 255 : levelRequired);
    item->stack = (u8)(stack > 255 ? 255 : stack);
    item->hp = hp;
    item->mp = mp;
    item->exp = exp;
    return true;
}

static u32 vm_net_mock_load_item_effect_catalog_dsh(const char *path)
{
    static u8 data[131072];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
        return 0;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return added;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return added;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        u32 itemId = 0;
        u32 category = 0xff;
        u32 levelRequired = 1;
        u32 stack = 1;
        u32 hp = 0;
        u32 mp = 0;
        u32 exp = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            u32 parsed = 0;

            if (rowPos + valueLen > rowEnd)
                break;
            parsed = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            switch (col)
            {
            case 0:
                itemId = parsed;
                break;
            case 5:
                category = parsed;
                break;
            case 6:
                levelRequired = parsed ? parsed : 1;
                break;
            case 10:
                stack = parsed ? parsed : 1;
                break;
            case 15:
                hp = parsed;
                break;
            case 16:
                mp = parsed;
                break;
            case 17:
                exp = parsed;
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_item_effect_catalog_item(itemId, category, levelRequired,
                                                     stack, hp, mp, exp))
        {
            ++added;
        }
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_item_effect_catalog(void)
{
    u32 itemCount = 0;

    if (g_vm_net_mock_item_effect_catalog_loaded)
        return g_vm_net_mock_item_effect_catalog_count;

    g_vm_net_mock_item_effect_catalog_loaded = true;
    g_vm_net_mock_item_effect_catalog_count = 0;
    itemCount = vm_net_mock_load_item_effect_catalog_dsh("JHOnlineData/item.dsh");
    if (itemCount == 0)
        itemCount = vm_net_mock_load_item_effect_catalog_dsh("bin/JHOnlineData/item.dsh");

    if (itemCount == 0)
    {
        (void)vm_net_mock_add_item_effect_catalog_item(301, 10, 1, 20, 100, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(302, 10, 1, 20, 350, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(303, 10, 1, 20, 600, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(304, 10, 1, 20, 850, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(305, 10, 1, 20, 1100, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(321, 10, 1, 20, 0, 100, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(322, 10, 1, 20, 0, 350, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(323, 10, 1, 20, 0, 600, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(324, 10, 1, 20, 0, 850, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(325, 10, 1, 20, 0, 1100, 0);
        printf("[warn][network] mock_item_effect_catalog fallback=item.dsh-not-found total=%u\n",
               g_vm_net_mock_item_effect_catalog_count);
    }
    else
    {
        printf("[info][network] mock_item_effect_catalog total=%u source=item.dsh\n",
               g_vm_net_mock_item_effect_catalog_count);
    }
    return g_vm_net_mock_item_effect_catalog_count;
}

static const vm_net_mock_item_effect_catalog_item *vm_net_mock_find_item_effect_catalog_item(u32 itemId)
{
    u32 total = vm_net_mock_load_item_effect_catalog();

    if (itemId == 0)
        return NULL;
    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_item_effect_catalog[i].itemId == itemId)
            return &g_vm_net_mock_item_effect_catalog[i];
    }
    return NULL;
}

static bool vm_net_mock_item_effect_is_usable(const vm_net_mock_item_effect_catalog_item *item)
{
    if (item == NULL)
        return false;
    return item->category == 10 || item->hp != 0 || item->mp != 0 || item->exp != 0;
}

static bool vm_net_mock_shop17_should_include_item(u32 itemId);
static u32 vm_net_mock_shop17_order_group(u32 itemId);

static void vm_net_mock_append_preview_u32(char *out, u32 outCap, u32 *pos, u32 value)
{
    int written = 0;
    if (out == NULL || outCap == 0 || pos == NULL || *pos >= outCap)
        return;
    written = snprintf(out + *pos, outCap - *pos, "%s%u", *pos ? "," : "", value);
    if (written < 0)
        return;
    if ((u32)written >= outCap - *pos)
        *pos = outCap - 1;
    else
        *pos += (u32)written;
}

static void vm_net_mock_format_shop_page_ids(u32 pageIndex, u32 maxRows,
                                             char *out, u32 outCap)
{
    u32 pos = 0;
    u32 total = vm_net_mock_load_shop_catalog();
    u32 start = pageIndex * VM_NET_MOCK_SHOP_PAGE_SIZE;
    u32 rowCount = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (start >= total)
        return;
    rowCount = total - start;
    if (rowCount > VM_NET_MOCK_SHOP_PAGE_SIZE)
        rowCount = VM_NET_MOCK_SHOP_PAGE_SIZE;
    if (rowCount > maxRows)
        rowCount = maxRows;
    for (u32 i = 0; i < rowCount; ++i)
        vm_net_mock_append_preview_u32(out, outCap, &pos,
                                       g_vm_net_mock_shop_catalog[start + i].itemId);
}

static void vm_net_mock_format_shop17_ids(u32 maxRows, char *out, u32 outCap)
{
    u32 pos = 0;
    u32 total = vm_net_mock_load_shop_catalog();
    u32 filteredCount = 0;
    u32 rowCount = 0;
    u32 emitted = 0;
    bool useFilteredCatalog = false;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    for (u32 i = 0; i < total; ++i)
    {
        if (vm_net_mock_shop17_should_include_item(g_vm_net_mock_shop_catalog[i].itemId))
            ++filteredCount;
    }
    useFilteredCatalog = filteredCount > 0;
    rowCount = useFilteredCatalog ? filteredCount : total;
    if (rowCount > VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS)
        rowCount = VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS;
    if (rowCount > maxRows)
        rowCount = maxRows;

    for (u32 group = 0; group < 3 && emitted < rowCount; ++group)
    {
        for (u32 i = 0; i < total && emitted < rowCount; ++i)
        {
            const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
            if (useFilteredCatalog && !vm_net_mock_shop17_should_include_item(item->itemId))
                continue;
            if (vm_net_mock_shop17_order_group(item->itemId) != group)
                continue;
            vm_net_mock_append_preview_u32(out, outCap, &pos, item->itemId);
            ++emitted;
        }
    }
}

static bool vm_net_mock_seq_put_item_common_extra(u8 *out, u32 outCap, u32 *pos, u8 stackRuntimeByte)
{
    /*
     * JianghuOL.CBE:ParseEquipAttributes (vtable +2452) reads two i16-ish
     * fields, then one u8 attr-count. It only consumes attr slots when that
     * count is nonzero, so zero-attr rows must stop after the count byte.
     */
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, stackRuntimeByte))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, 0))
        return false;
    return vm_net_mock_seq_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_seq_put_shop_page_item_extra(u8 *out, u32 outCap, u32 *pos, u8 stackRuntimeByte)
{
    /*
     * mmShopMstarWqvga.cbm:sub_7BC calls a shop-page item-extra reader after
     * itemId/name/visual/stack/price/stock/flag. The reader is the same
     * ParseEquipAttributes helper as mmGame:0x418C; the six attr arrays are
     * destination capacity, not fields to send when attr-count is zero.
     */
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, stackRuntimeByte))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, 0))
        return false;
    return vm_net_mock_seq_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_build_backpack_iteminfo_blob(u8 *out, u32 outCap,
                                                     const vm_net_mock_role_state *role,
                                                     u32 *blobLenOut, u32 *rowCountOut)
{
    u32 pos = 0;
    u8 itemCount = vm_net_mock_role_backpack_count(role);
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (rowCountOut)
        *rowCountOut = 0;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, itemCount))
        return false;
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
            return false;
        if (!vm_net_mock_seq_put_item_common_extra(out, outCap, &pos,
                                                  vm_net_mock_backpack_stack_byte(item)))
        {
            return false;
        }
    }
    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = itemCount;
    return true;
}

static bool vm_net_mock_shop17_should_include_item(u32 itemId)
{
    /*
     * The 17/1 list is rendered by mmGame:0x418C.  The NPC purchase screen in
     * this path is an equipment shop, so prefer equip.dsh ids (>=1000) and omit
     * low material/task drops.  Keep the packet page-sized: the parser copies
     * iteminfo into a 1024-byte stream buffer.
     */
    return itemId >= 800;
}

static u32 vm_net_mock_shop17_order_group(u32 itemId)
{
    if (itemId >= 1000)
        return 0;
    if (itemId >= 800)
        return 1;
    return 2;
}

static u32 vm_net_mock_shop17_first_item_id(void)
{
    u32 total = vm_net_mock_load_shop_catalog();
    for (u32 group = 0; group < 3; ++group)
    {
        for (u32 i = 0; i < total; ++i)
        {
            u32 itemId = g_vm_net_mock_shop_catalog[i].itemId;
            if (vm_net_mock_shop17_should_include_item(itemId) &&
                vm_net_mock_shop17_order_group(itemId) == group)
            {
                return itemId;
            }
        }
    }
    return total > 0 ? g_vm_net_mock_shop_catalog[0].itemId : 0;
}

static bool vm_net_mock_build_shop17_iteminfo_blob(u8 *out, u32 outCap,
                                                   u32 *blobLenOut, u32 *rowCountOut)
{
    u32 pos = 0;
    u32 total = vm_net_mock_load_shop_catalog();
    u32 filteredCount = 0;
    u32 rowCount = 0;
    bool useFilteredCatalog = false;

    if (out == NULL || blobLenOut == NULL)
        return false;
    if (rowCountOut)
        *rowCountOut = 0;

    for (u32 i = 0; i < total; ++i)
    {
        if (vm_net_mock_shop17_should_include_item(g_vm_net_mock_shop_catalog[i].itemId))
            ++filteredCount;
    }
    useFilteredCatalog = filteredCount > 0;
    rowCount = useFilteredCatalog ? filteredCount : total;
    if (rowCount > VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS)
        rowCount = VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS;
    if (rowCount == 0 && total > 0)
        rowCount = total < VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS ? total : VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)rowCount))
        return false;

    for (u32 group = 0, emitted = 0; group < 3 && emitted < rowCount; ++group)
    {
        for (u32 i = 0; i < total && emitted < rowCount; ++i)
        {
            const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
            if (useFilteredCatalog && !vm_net_mock_shop17_should_include_item(item->itemId))
                continue;
            if (vm_net_mock_shop17_order_group(item->itemId) != group)
                continue;
            if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
                return false;
            if (!vm_net_mock_seq_put_item_common_extra(out, outCap, &pos, item->stack))
                return false;
            ++emitted;
        }
    }

    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
    return true;
}

static bool vm_net_mock_build_backpack_grid_iteminfo_blob(u8 *out, u32 outCap,
                                                         const vm_net_mock_role_state *role,
                                                         u32 *blobLenOut, u32 *gridCountOut)
{
    u32 pos = 0;
    u8 itemCount = vm_net_mock_role_backpack_count(role);
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (gridCountOut)
        *gridCountOut = 0;

    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
            return false;
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, item->seq))
            return false;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->count))
            return false;
        if (!vm_net_mock_seq_put_item_common_extra(out, outCap, &pos, 0))
            return false;
    }
    *blobLenOut = pos;
    if (gridCountOut)
        *gridCountOut = itemCount;
    return true;
}

static bool vm_net_mock_build_shop_iteminfo_page_blob(u8 *out, u32 outCap, u32 *blobLenOut,
                                                      u32 pageIndex, u32 *rowCountOut)
{
    u32 pos = 0;
    u32 total = vm_net_mock_load_shop_catalog();
    u32 start = pageIndex * VM_NET_MOCK_SHOP_PAGE_SIZE;
    u32 rowCount = 0;
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (rowCountOut)
        *rowCountOut = 0;

    /*
     * mmShopMstarWqvga.cbm:sub_7BC reads:
     *   u8 row_count,
     *   u32 itemId, string itemName, u8 visual/status, u8 stackOrLimit,
     *   u32 price, u32 stock, u8 flag, then the common item-extra block.
     */
    if (start < total)
    {
        rowCount = total - start;
        if (rowCount > VM_NET_MOCK_SHOP_PAGE_SIZE)
            rowCount = VM_NET_MOCK_SHOP_PAGE_SIZE;
    }

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)rowCount))
        return false;
    for (u32 i = 0; i < rowCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[start + i];
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
            return false;
        if (!vm_net_mock_seq_put_string(out, outCap, &pos, item->name))
            return false;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, item->visual))
            return false;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, item->stack))
            return false;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->price))
            return false;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->stock))
            return false;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
            return false;
        if (!vm_net_mock_seq_put_shop_page_item_extra(out, outCap, &pos, item->stack))
            return false;
    }

    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
    return true;
}

static bool vm_net_mock_append_shop_catalog_page_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 subtype, u32 pageIndex,
                                                        u32 *totalOut, u32 *rowCountOut,
                                                        u32 *itemInfoLenOut)
{
    u32 objectStart = 0;
    u8 itemInfo[4096];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;
    u32 total = vm_net_mock_load_shop_catalog();

    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_shop_iteminfo_page_blob(itemInfo, sizeof(itemInfo),
                                                  &itemInfoLen, pageIndex, &rowCount))
    {
        return false;
    }
    if (itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "totalnum", total))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (totalOut)
        *totalOut = total;
    if (rowCountOut)
        *rowCountOut = rowCount;
    if (itemInfoLenOut)
        *itemInfoLenOut = itemInfoLen;
    return true;
}

static bool vm_net_mock_append_shop_open_status14_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "shopinfo", "Codex Shop"))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_shop_money4_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "coolmoney", money))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "ticket", 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_shop_actor_state14_object(u8 *out, u32 outCap, u32 *pos,
                                                         u32 *actorInfoLenOut)
{
    u32 objectStart = 0;
    u8 actorInfo[512];
    u32 actorInfoLen = 0;

    if (actorInfoLenOut)
        *actorInfoLenOut = 0;

    memset(actorInfo, 0, sizeof(actorInfo));
    actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
    if (actorInfoLen == 0 || actorInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "revivetype", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "ruffianflag", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 0))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    return true;
}

static bool vm_net_mock_append_shop_empty_page14_object(u8 *out, u32 outCap, u32 *pos, u8 subtype)
{
    u32 objectStart = 0;
    const u8 emptyItemInfo[] = {0};
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "totalnum", 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo",
                                    emptyItemInfo, (u16)sizeof(emptyItemInfo)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_get_shop_page_index_in_request(const u8 *request, u32 requestLen,
                                                      u8 subtype, u32 fallback)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return fallback;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u32 index = 0;
        u8 index8 = 0;
        if (object.major == 1 && object.kind == 14 && object.subtype == subtype)
        {
            if (vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "index", &index))
                return index;
            if (vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "index", &index8))
                return index8;
            return fallback;
        }
    }
    return fallback;
}

static bool vm_net_mock_is_backpack_items_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 17 &&
           object.subtype == 1 &&
           object.payloadLen == 0;
}

static bool vm_net_mock_is_backpack_open_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 7 &&
           object.subtype == 42 &&
           object.payloadLen == 0;
}

static bool vm_net_mock_is_backpack_items_books_combo_request(const u8 *request, u32 requestLen,
                                                              u16 *itemsPayloadLenOut)
{
    u32 offset = 4;
    vm_net_mock_request_object itemsObject;
    vm_net_mock_request_object booksObject;

    if (itemsPayloadLenOut)
        *itemsPayloadLenOut = 0;
    if (request == NULL || requestLen < 14 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &itemsObject))
        return false;
    if (itemsObject.major != 1 ||
        itemsObject.kind != 17 ||
        itemsObject.subtype != 1)
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &booksObject))
        return false;
    if (booksObject.major != 1 ||
        booksObject.kind != 7 ||
        booksObject.subtype != 42 ||
        booksObject.payloadLen != 0)
    {
        return false;
    }
    if (offset != requestLen)
        return false;

    if (itemsPayloadLenOut)
        *itemsPayloadLenOut = itemsObject.payloadLen;
    return true;
}

typedef struct
{
    u32 itemId;
    u16 seq;
    u32 count;
    u32 num;
    u32 hp;
    u32 mp;
    u32 exp;
    u8 type;
    bool haveItemSelector;
    bool haveEffect;
} vm_net_mock_item_use_request;

typedef struct
{
    u32 itemId;
    u16 seq;
    u32 count;
    u8 type;
    bool haveItemSelector;
} vm_net_mock_item_discard_request;

typedef struct
{
    u32 index;
    u16 seq;
} vm_net_mock_battle_item_use_request;

static bool vm_net_mock_get_object_number_field(const u8 *payload, u32 payloadLen,
                                                const char *field, u32 *value)
{
    u32 value32 = 0;
    u16 value16 = 0;
    u8 value8 = 0;

    if (value)
        *value = 0;
    if (vm_net_mock_get_object_u32_field(payload, payloadLen, field, &value32))
    {
        if (value)
            *value = value32;
        return true;
    }
    if (vm_net_mock_get_object_u16_field(payload, payloadLen, field, &value16))
    {
        if (value)
            *value = value16;
        return true;
    }
    if (vm_net_mock_get_object_u8_field(payload, payloadLen, field, &value8))
    {
        if (value)
            *value = value8;
        return true;
    }
    return false;
}

static bool vm_net_mock_item_id_is_active_backpack_row(u32 itemId)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    return vm_net_mock_role_find_backpack_item(role, itemId, 0) != NULL;
}

static bool vm_net_mock_parse_item_use_request(const u8 *request, u32 requestLen,
                                               vm_net_mock_item_use_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_item_use_request parsed;
    u32 candidate = 0;
    u32 value = 0;
    bool haveCandidate = false;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    parsed.count = 1;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 1 || object.payloadLen == 0)
        return false;

    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &parsed.type);
    if (!vm_net_mock_get_object_u16_field(object.payload, object.payloadLen, "seq", &parsed.seq))
    {
        if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq", &value) &&
            value <= 0xffffu)
        {
            parsed.seq = (u16)value;
        }
    }

    haveCandidate = vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemId", &candidate) ||
                    vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemID", &candidate) ||
                    vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemid", &candidate);
    if (haveCandidate)
    {
        parsed.itemId = candidate;
        parsed.haveItemSelector = true;
    }

    if (!parsed.haveItemSelector &&
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &candidate))
    {
        const vm_net_mock_item_effect_catalog_item *effect =
            vm_net_mock_find_item_effect_catalog_item(candidate);
        if (vm_net_mock_item_effect_is_usable(effect) ||
            vm_net_mock_item_id_is_active_backpack_row(candidate))
        {
            parsed.itemId = candidate;
            parsed.haveItemSelector = true;
        }
    }

    if (parsed.seq != 0)
        parsed.haveItemSelector = true;

    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "num", &parsed.num);
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "count", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "usecount", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "useCount", &value))
    {
        parsed.count = value ? value : 1;
    }
    if (parsed.count == 0)
        parsed.count = 1;
    if (parsed.count > 99)
        parsed.count = 99;

    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "hp", &parsed.hp);
    if (parsed.hp == 0)
        (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "HP", &parsed.hp);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "mp", &parsed.mp);
    if (parsed.mp == 0)
        (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "MP", &parsed.mp);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "exp", &parsed.exp);
    if (parsed.exp == 0)
        (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "EXP", &parsed.exp);
    if (parsed.hp == 0 && parsed.mp == 0 && parsed.exp == 0 && parsed.num != 0)
    {
        if (parsed.type == 2)
            parsed.mp = parsed.num;
        else if (parsed.type == 3)
            parsed.exp = parsed.num;
        else
            parsed.hp = parsed.num;
    }
    parsed.haveEffect = parsed.hp != 0 || parsed.mp != 0 || parsed.exp != 0;

    if (!parsed.haveItemSelector && !parsed.haveEffect)
        return false;

    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_parse_item_discard_request(const u8 *request, u32 requestLen,
                                                   vm_net_mock_item_discard_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_item_discard_request parsed;
    u32 value = 0;
    u32 candidate = 0;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 4 || object.payloadLen == 0)
        return false;

    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &parsed.type);
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemseq", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemSeq", &value))
    {
        if (value <= 0xffffu)
        {
            parsed.seq = (u16)value;
            parsed.haveItemSelector = true;
        }
    }

    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemId", &candidate) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemID", &candidate) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemid", &candidate) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &candidate))
    {
        parsed.itemId = candidate;
        parsed.haveItemSelector = true;
    }

    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "count", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "num", &value))
    {
        parsed.count = value;
    }

    if (!parsed.haveItemSelector)
        return false;
    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_build_item_use_iteminfo_blob(u8 *out, u32 outCap,
                                                     u16 seq, u32 itemId,
                                                     u32 count, u32 *blobLenOut)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || itemId == 0)
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seq))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemId))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, count))
        return false;
    if (!vm_net_mock_seq_put_item_common_extra(out, outCap, &pos,
                                              count > 255 ? 255 : (u8)count))
        return false;

    *blobLenOut = pos;
    return true;
}

static bool vm_net_mock_build_item_use_count_info_blob(u8 *out, u32 outCap,
                                                       u16 seq, u32 count,
                                                       u32 *blobLenOut)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || seq == 0)
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seq))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, count))
        return false;

    *blobLenOut = pos;
    return true;
}

static u32 vm_net_mock_add_capped_u32(u32 value, u32 add)
{
    if (0xffffffffu - value < add)
        return 0xffffffffu;
    return value + add;
}

static u32 vm_net_mock_mul_capped_u32(u32 value, u32 count)
{
    uint64_t product = (uint64_t)value * (uint64_t)count;
    return product > 0xffffffffull ? 0xffffffffu : (u32)product;
}

static void vm_net_mock_role_apply_item_effect(vm_net_mock_role_state *role,
                                               u32 hp, u32 mp, u32 exp,
                                               u32 count)
{
    if (role == NULL || count == 0)
        return;

    vm_net_mock_role_sync_derived_vitals(role);
    if (hp != 0)
    {
        uint64_t add = (uint64_t)hp * (uint64_t)count;
        u32 capped = add > 0xffffffffull ? 0xffffffffu : (u32)add;
        role->hp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(role->hp, capped), role->hpMax);
    }
    if (mp != 0)
    {
        uint64_t add = (uint64_t)mp * (uint64_t)count;
        u32 capped = add > 0xffffffffull ? 0xffffffffu : (u32)add;
        role->mp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(role->mp, capped), role->mpMax);
    }
    if (exp != 0)
    {
        uint64_t add = (uint64_t)exp * (uint64_t)count;
        u32 capped = add > 0xffffffffull ? 0xffffffffu : (u32)add;
        role->exp = vm_net_mock_add_capped_u32(role->exp, capped);
        role->level = vm_net_mock_role_level_from_exp(role->exp);
        vm_net_mock_role_sync_derived_vitals(role);
    }
}

static u32 vm_net_mock_build_item_use_hint_response(u8 *out, u32 outCap, const char *hint)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 16, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "hint", hint ? hint : "OK"))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_item_use_response(const u8 *request, u32 requestLen,
                                               u8 *out, u32 outCap)
{
    vm_net_mock_item_use_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    const vm_net_mock_item_effect_catalog_item *effect = NULL;
    u32 itemId = 0;
    u16 seq = 0;
    u32 hp = 0;
    u32 mp = 0;
    u32 exp = 0;
    u32 remaining = 0;
    bool consumed = false;
    bool applied = false;
    u8 itemInfo[64];
    u32 itemInfoLen = 0;
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u8 itemUseType = 1;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_item_use_request(request, requestLen, &parsed))
        return 0;

    role = vm_net_mock_active_role();
    if (role == NULL)
        return vm_net_mock_build_item_use_hint_response(out, outCap, "item unavailable");

    item = vm_net_mock_role_find_backpack_item(role, parsed.itemId, parsed.seq);
    if (item == NULL && !parsed.haveItemSelector && parsed.haveEffect)
        item = vm_net_mock_role_find_backpack_item_by_effect(role, parsed.hp, parsed.mp, parsed.exp);
    if (item != NULL)
    {
        itemId = item->itemId;
        seq = item->seq;
    }
    else
    {
        itemId = parsed.itemId;
        seq = parsed.seq;
    }

    effect = vm_net_mock_find_item_effect_catalog_item(itemId);
    if (vm_net_mock_item_effect_is_usable(effect))
    {
        hp = effect->hp;
        mp = effect->mp;
        exp = effect->exp;
    }
    if (hp == 0)
        hp = parsed.hp;
    if (mp == 0)
        mp = parsed.mp;
    if (exp == 0)
        exp = parsed.exp;

    if (itemId != 0)
        consumed = vm_net_mock_role_consume_backpack_item(role, itemId, seq, parsed.count, &remaining);

    if (consumed && (hp != 0 || mp != 0 || exp != 0))
    {
        vm_net_mock_role_apply_item_effect(role, hp, mp, exp, parsed.count);
        applied = true;
    }

    if (applied || consumed)
        vm_net_mock_role_db_save("item-use");

    if (itemId == 0)
    {
        u32 hintLen = vm_net_mock_build_item_use_hint_response(out, outCap,
                                                               "item unavailable");
        vm_autotest_note("mock_item_use type=%u item=0 seq=0 count=%u hp=%u mp=%u exp=%u applied=%u consumed=0 response=16/2-hint evidence=runtime:wt7/1 mmGame:0x11CE\n",
                         parsed.type, parsed.count, hp, mp, exp, applied ? 1 : 0);
        return hintLen;
    }

    itemUseType = parsed.type ? parsed.type : 1;
    /*
     * JianghuOL.CBE:0x1033544 handles 7/1 as the original item-use success
     * acknowledgement.  When the client has a pending use row, result=1 calls
     * the item-manager operation at +56 with type/id/count=1; this is the path
     * that also updates the occupied-slot counter when the stack reaches zero.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", itemUseType))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "id", (u16)itemId))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;

    /*
     * mmGame:0xD04 reads 7/7 rows as seq, itemId, count, common-extra.  Use
     * type=2 for item-use consumption; type=1 is the add/update path and has
     * already been observed to duplicate stacks when used as a refresh.
     */
    if (!vm_net_mock_build_item_use_iteminfo_blob(itemInfo, sizeof(itemInfo),
                                                  seq, itemId, remaining,
                                                  &itemInfoLen))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 7, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 2))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;

    /*
     * JianghuOL.CBE:0x1033544 handles 7/11 and 7/12 by reading an "info"
     * stream of row_count, seq, and new_count, then writing the backpack row.
     * This path is reached before the mmGame callback that ignores kind 17.
     */
    if (seq != 0)
    {
        if (!vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo),
                                                        seq, remaining, &countInfoLen))
            return 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo, (u16)countInfoLen))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    printf("[info][network] mock_item_use item=%u seq=%u count=%u remaining=%u hp=%u mp=%u exp=%u applied=%u consumed=%u refresh=7/1+7/11 resp=%u\n",
           itemId, seq, parsed.count, remaining, hp, mp, exp,
           applied ? 1 : 0, consumed ? 1 : 0, pos);
    vm_autotest_note("mock_item_use item=%u seq=%u count=%u remaining=%u hp=%u mp=%u exp=%u applied=%u consumed=%u response=7/1-use-ok+7/7-type2+7/11-info evidence=runtime:wt7/1 mmGame:0x11CE->0xD04 JianghuOL.CBE:0x1033544\n",
                     itemId, seq, parsed.count, remaining, hp, mp, exp,
                     applied ? 1 : 0, consumed ? 1 : 0);
    return pos;
}

static u32 vm_net_mock_build_item_discard_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    vm_net_mock_item_discard_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    u32 itemId = 0;
    u16 seq = 0;
    u32 discardCount = 0;
    u32 remaining = 0;
    bool consumed = false;
    u8 result = 2;
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_item_discard_request(request, requestLen, &parsed))
        return 0;

    role = vm_net_mock_active_role();
    if (role != NULL)
    {
        item = vm_net_mock_role_find_backpack_item(role, parsed.itemId, parsed.seq);
        if (item == NULL && parsed.seq != 0)
            item = vm_net_mock_role_find_backpack_item(role, 0, parsed.seq);
        if (item == NULL && parsed.itemId != 0)
            item = vm_net_mock_role_find_backpack_item(role, parsed.itemId, 0);
        if (item != NULL)
        {
            itemId = item->itemId;
            seq = item->seq;
            discardCount = parsed.count ? parsed.count : item->count;
            if (discardCount == 0)
                discardCount = item->count;
            consumed = vm_net_mock_role_consume_backpack_item(role, itemId, seq,
                                                              discardCount, &remaining);
            if (consumed)
            {
                result = 1;
                vm_net_mock_role_db_save("item-discard");
            }
        }
        else
        {
            itemId = parsed.itemId;
            seq = parsed.seq;
        }
    }

    /*
     * JianghuOL.CBE:0x1033544 handles 7/4 as the item-operation completion
     * branch and clears the waiting flag.  The backpack UI callback is the
     * proven mmGame:0x418C path, so a successful discard also sends a full
     * 17/1 list rebuild plus 7/42 book filler.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;

    if (consumed)
    {
        if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (seq != 0)
        {
            if (!vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo),
                                                            seq, remaining, &countInfoLen))
                return 0;
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11, &objectStart))
                return 0;
            if (!vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo, (u16)countInfoLen))
                return 0;
            vm_net_mock_finish_wt_object(out, objectStart, pos);
            objectCount += 1;
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_item_discard item=%u seq=%u count=%u remaining=%u result=%u refresh=%s resp=%u\n",
           itemId, seq, discardCount, remaining, result,
           consumed ? "7/4+17/1+7/42+7/11" : "7/4-fail", pos);
    vm_autotest_note("mock_item_discard item=%u seq=%u count=%u remaining=%u result=%u response=%s evidence=runtime:wt7/4 JianghuOL.CBE:0x1033544 mmGame:0x418C\n",
                     itemId, seq, discardCount, remaining, result,
                     consumed ? "7/4+17/1+7/42+7/11" : "7/4-fail");
    return pos;
}

static bool vm_net_mock_append_backpack_items_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 itemInfo[1024];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u16 capacity = role ? role->backpackCapacity : VM_NET_MOCK_BACKPACK_CAPACITY;

    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_backpack_iteminfo_blob(itemInfo, sizeof(itemInfo), role,
                                                 &itemInfoLen, &rowCount))
        return false;
    if (itemInfoLen == 0 || itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 17, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "maxnum", capacity))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    vm_autotest_note("mock_backpack_items role=%u capacity=%u rows=%u iteminfo_len=%u evidence=mmGame:0x418C\n",
                     role ? role->roleId : 0,
                     capacity,
                     rowCount,
                     itemInfoLen);
    return true;
}

static bool vm_net_mock_append_shop17_items_object(u8 *out, u32 outCap, u32 *pos,
                                                   u32 *rowCountOut, u32 *itemInfoLenOut)
{
    u32 objectStart = 0;
    u8 itemInfo[32768];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;

    if (rowCountOut)
        *rowCountOut = 0;
    if (itemInfoLenOut)
        *itemInfoLenOut = 0;
    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_shop17_iteminfo_blob(itemInfo, sizeof(itemInfo), &itemInfoLen, &rowCount))
        return false;
    if (itemInfoLen == 0 || itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 17, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (rowCountOut)
        *rowCountOut = rowCount;
    if (itemInfoLenOut)
        *itemInfoLenOut = itemInfoLen;
    return true;
}

static bool vm_net_mock_append_backpack_grid_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 itemInfo[1024];
    u32 itemInfoLen = 0;
    u32 gridCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_backpack_grid_iteminfo_blob(itemInfo, sizeof(itemInfo), role,
                                                      &itemInfoLen, &gridCount))
        return false;
    if (gridCount == 0 || itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 21, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "gridnum", (u8)gridCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    vm_autotest_note("mock_backpack_grid role=%u kind=30 subtype=21 gridnum=%u iteminfo_len=%u evidence=JianghuOL:0x1039952\n",
                     role ? role->roleId : 0,
                     gridCount,
                     itemInfoLen);
    return true;
}

static bool vm_net_mock_append_backpack_role_grid_main_objects(u8 *out, u32 outCap, u32 *pos, u8 *objectCount)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (out == NULL || pos == NULL || objectCount == NULL)
        return false;
    if (role == NULL || vm_net_mock_role_backpack_count(role) == 0)
        return true;
    if (g_netMockBackpackGridSeededRoleId != role->roleId)
    {
        if (!vm_net_mock_append_backpack_grid_object(out, outCap, pos))
            return false;
        *objectCount = (u8)(*objectCount + 1);
        g_netMockBackpackGridSeededRoleId = role->roleId;
    }
    return true;
}

static u32 vm_net_mock_build_backpack_items_response(u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);

    return pos;
}

static u32 vm_net_mock_build_backpack_open_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;

    if (outCap < pos)
        return 0;
    /*
     * mmGameMstarWqvga.cbm:sub_2434 opens the backpack component and sends
     * 7/42. Its registered network parser is sub_418C, which handles both
     * 17/1 iteminfo and 7/42 book info while the backpack component is active.
     */
    if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_backpack_items_books_combo_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u16 itemsPayloadLen = 0;
    u32 responseLen = 0;

    if (!vm_net_mock_is_backpack_items_books_combo_request(request, requestLen, &itemsPayloadLen))
        return 0;

    responseLen = vm_net_mock_build_backpack_open_response(out, outCap);
    if (responseLen)
    {
        vm_autotest_note("mock_backpack_items_books_combo len=%u items_payload=%u response=17/1+7/42 evidence=mmGame:0x418C runtime=wt17/1-len25\n",
                         requestLen,
                         itemsPayloadLen);
    }
    return responseLen;
}

static u32 vm_net_mock_build_shop_items_books_combo_response(const u8 *request, u32 requestLen,
                                                            u8 *out, u32 outCap)
{
    u16 itemsPayloadLen = 0;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 rowCount = 0;
    u32 itemInfoLen = 0;
    char ids[160];

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_is_backpack_items_books_combo_request(request, requestLen, &itemsPayloadLen))
        return 0;

    /*
     * NPC dialog buy reaches the mmGame list parser at 0x418C.  That parser's
     * 17/1 branch loads item.dsh/equip.dsh locally and expects iteminfo rows of
     * itemId + common item-extra; returning the normal backpack one-row 17/1
     * keeps the visible shop stuck on 传送石 only.
     */
    if (!vm_net_mock_append_shop17_items_object(out, outCap, &pos, &rowCount, &itemInfoLen))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_netMockShop17ListPending = 0;
    vm_net_mock_format_shop17_ids(8, ids, sizeof(ids));
    printf("[info][network] mock_shop_items_books_combo rows=%u iteminfo_len=%u first=%u ids=%s\n",
           rowCount,
           itemInfoLen,
           vm_net_mock_shop17_first_item_id(),
           ids);
    vm_autotest_note("mock_shop_items_books_combo len=%u items_payload=%u rows=%u iteminfo_len=%u first=%u response=17/1+7/42 evidence=mmGame:0x418C runtime=npc-buy-wt17/1-len25\n",
                     requestLen,
                     itemsPayloadLen,
                     rowCount,
                     itemInfoLen,
                     vm_net_mock_shop17_first_item_id());
    return pos;
}

static u32 vm_net_mock_build_shop_items17_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 rowCount = 0;
    u32 itemInfoLen = 0;
    char ids[160];

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_shop17_items_object(out, outCap, &pos, &rowCount, &itemInfoLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_netMockShop17ListPending = 0;
    vm_net_mock_format_shop17_ids(8, ids, sizeof(ids));
    printf("[info][network] mock_shop_items17 rows=%u iteminfo_len=%u first=%u ids=%s\n",
           rowCount,
           itemInfoLen,
           vm_net_mock_shop17_first_item_id(),
           ids);
    vm_autotest_note("mock_shop_items17 rows=%u iteminfo_len=%u first=%u response=17/1 evidence=mmGame:0x418C runtime=shop-context-empty-17/1\n",
                     rowCount,
                     itemInfoLen,
                     vm_net_mock_shop17_first_item_id());
    return pos;
}

static u32 vm_net_mock_build_shop_items_books_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u32 rowCount = 0;
    u32 itemInfoLen = 0;
    char ids[160];

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_shop17_items_object(out, outCap, &pos, &rowCount, &itemInfoLen))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_netMockShop17ListPending = 0;
    vm_net_mock_format_shop17_ids(8, ids, sizeof(ids));
    printf("[info][network] mock_shop_items_books rows=%u iteminfo_len=%u first=%u ids=%s\n",
           rowCount,
           itemInfoLen,
           vm_net_mock_shop17_first_item_id(),
           ids);
    vm_autotest_note("mock_shop_items_books rows=%u iteminfo_len=%u first=%u response=17/1+7/42 evidence=mmGame:0x418C runtime=shop-context-7/42\n",
                     rowCount,
                     itemInfoLen,
                     vm_net_mock_shop17_first_item_id());
    return pos;
}

static const char *vm_net_mock_default_scene_name(void);
static bool vm_net_mock_scene_is_penglai01(const char *scene);
static bool vm_net_mock_scene_is_penglai02(const char *scene);
static bool vm_net_mock_scene_is_penglai03(const char *scene);
static bool vm_net_mock_scene_is_penglai04(const char *scene);
static bool vm_net_mock_scene_is_penglai_transfer_scene(const char *scene);
static bool vm_net_mock_scene_is_c00_penglai03(const char *scene);
static bool vm_net_mock_scene_is_taohuadao01(const char *scene);

static vm_net_mock_role_db_file g_vm_net_mock_role_db;
static bool g_vm_net_mock_role_db_loaded = false;
static bool g_vm_net_mock_role_db_valid = false;
static u32 g_vm_net_mock_battle_rewarded_serial = 0;
static u32 g_vm_net_mock_battle_rewarded_exp = 0;
static u32 g_vm_net_mock_battle_rewarded_drop_item = 0;
static u16 g_vm_net_mock_battle_rewarded_drop_seq = 0;
static u32 g_vm_net_mock_battle_enemy_id_current = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
static u32 g_vm_net_mock_battle_reward_rng = 0;
static u32 g_vm_net_mock_battle_settlement_sent_serial = 0;
static u32 g_vm_net_mock_battle_recovered_serial = 0;
static char g_vm_net_mock_scene_moveinfo_npc_pending_scene[64];
static bool g_vm_net_mock_scene_moveinfo_npc_pending = false;
static char g_vm_net_mock_scene_moveinfo_npc_seeded_scene[64];
static bool g_vm_net_mock_scene_moveinfo_npc_seeded = false;

static bool vm_net_mock_read_current_player_grid(u32 *nodeOut, u32 *actorIdOut,
                                                 u16 *gridXOut, u16 *gridYOut,
                                                 u16 *targetXOut, u16 *targetYOut);
static bool vm_net_mock_snapshot_current_player_pos(const char *reason);

static void vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(const char *scene)
{
    if (g_vm_net_mock_scene_moveinfo_npc_seeded &&
        (scene == NULL ||
         g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] == 0 ||
         strcmp(g_vm_net_mock_scene_moveinfo_npc_seeded_scene, scene) != 0))
    {
        g_vm_net_mock_scene_moveinfo_npc_seeded = false;
        g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
    }
    if (g_vm_net_mock_scene_moveinfo_npc_pending &&
        (scene == NULL ||
         g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] == 0 ||
         strcmp(g_vm_net_mock_scene_moveinfo_npc_pending_scene, scene) != 0))
    {
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    }
}

static void vm_net_mock_mark_scene_moveinfo_npc_seed_pending(const char *scene)
{
    if (scene == NULL || scene[0] == 0)
        return;
    g_vm_net_mock_scene_moveinfo_npc_pending = true;
    snprintf(g_vm_net_mock_scene_moveinfo_npc_pending_scene,
             sizeof(g_vm_net_mock_scene_moveinfo_npc_pending_scene),
             "%s", scene);
}

static bool vm_net_mock_is_scene_moveinfo_npc_seed_request(const char *scene,
                                                           const u8 *moveInfo,
                                                           u16 moveInfoLen)
{
    if (!g_vm_net_mock_scene_moveinfo_npc_pending)
        return false;
    if (scene == NULL || scene[0] == 0 ||
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] == 0 ||
        strcmp(g_vm_net_mock_scene_moveinfo_npc_pending_scene, scene) != 0)
    {
        return false;
    }
    if (moveInfo == NULL || moveInfoLen != 10)
        return false;
    return true;
}

static bool vm_net_mock_str_ends_with(const char *text, const char *suffix)
{
    size_t textLen = text ? strlen(text) : 0;
    size_t suffixLen = suffix ? strlen(suffix) : 0;
    if (suffixLen == 0 || textLen < suffixLen)
        return false;
    return strcmp(text + textLen - suffixLen, suffix) == 0;
}

static bool vm_net_mock_scene_name_has_path_separator(const char *scene)
{
    if (scene == NULL)
        return true;
    for (const char *p = scene; *p; ++p)
    {
        if (*p == '/' || *p == '\\' || *p == ':' || (u8)*p < 0x20)
            return true;
    }
    return false;
}

static bool vm_net_mock_scene_name_is_download_key(const char *scene)
{
    return scene != NULL &&
           scene[0] != 0 &&
           !vm_net_mock_scene_name_has_path_separator(scene);
}

static bool vm_net_mock_scene_resource_exists(const char *scene)
{
    char path[256];
    FILE *fp = NULL;
    if (scene == NULL || scene[0] == 0 || vm_net_mock_scene_name_has_path_separator(scene))
        return false;

    snprintf(path, sizeof(path), "JHOnlineData/%s", scene);
    fp = fopen(path, "rb");
    if (fp)
    {
        fclose(fp);
        return true;
    }

    if (!vm_net_mock_str_ends_with(scene, ".sce"))
    {
        snprintf(path, sizeof(path), "JHOnlineData/%s.sce", scene);
        fp = fopen(path, "rb");
        if (fp)
        {
            fclose(fp);
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_scene_name_is_safe(const char *scene)
{
    if (scene == NULL || scene[0] == 0)
        return false;
    return vm_net_mock_scene_resource_exists(scene);
}

static bool vm_net_mock_read_runtime_scene_name(char *out, size_t outCap)
{
    u32 sceneObj = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (Global_R9 == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, sizeof(sceneObj)) != UC_ERR_OK ||
        sceneObj == 0)
    {
        return false;
    }
    return vm_net_read_guest_raw_cstr(sceneObj + 0x475, out, outCap) &&
           vm_net_mock_scene_name_is_safe(out);
}

static const char *vm_net_mock_normalize_scene_name_for_enter(const char *scene)
{
    static char normalized[64];
    if (!vm_net_mock_scene_name_is_safe(scene))
        return vm_net_mock_default_scene_name();

    /*
     * Fresh actorinfo/sceneKey historically used extensionless c-prefixed town
     * keys (`c00..._01`), which the local file layer resolves to `.sce`.
     * Replaying `c00..._NN.sce` directly can be mistaken for a downloadable
     * resource key on re-enter, so strip only that c-prefixed suffix form.
     */
    if (scene[0] == 'c' && vm_net_mock_str_ends_with(scene, ".sce"))
    {
        size_t len = strlen(scene) - 4;
        if (len >= sizeof(normalized))
            len = sizeof(normalized) - 1;
        memcpy(normalized, scene, len);
        normalized[len] = 0;
        return normalized;
    }
    return scene;
}

static void vm_net_mock_copy_normalized_scene_name(const char *scene, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (scene == NULL || scene[0] == 0)
        return;
    snprintf(out, outCap, "%s", scene);
    if (out[0] == 'c')
    {
        size_t len = strlen(out);
        if (len > 4 && strcmp(out + len - 4, ".sce") == 0)
            out[len - 4] = 0;
    }
}

static bool vm_net_mock_scene_names_equal_loose(const char *a, const char *b)
{
    char normalizedA[64];
    char normalizedB[64];

    vm_net_mock_copy_normalized_scene_name(a, normalizedA, sizeof(normalizedA));
    vm_net_mock_copy_normalized_scene_name(b, normalizedB, sizeof(normalizedB));
    return normalizedA[0] != 0 &&
           normalizedB[0] != 0 &&
           strcmp(normalizedA, normalizedB) == 0;
}

static bool vm_net_mock_scene_is_c00_penglai03(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_taohuadao01(const char *scene)
{
    return scene != NULL &&
           strcmp(scene, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0;
}

static bool vm_net_mock_adjust_safe_player_pos_from_sce(const char *scene, u16 *x, u16 *y);

static void vm_net_mock_adjust_safe_player_pos_for_scene(const char *scene, u16 *x, u16 *y)
{
    if (scene == NULL || x == NULL || y == NULL)
        return;

    /*
     * SCE edge_portal spawn_point marks the portal glyph/trigger approach, not
     * a good player restore point. Restoring exactly on those coordinates makes
     * the yellow portal marker appear under the player or under the bottom UI.
     */
    if (vm_net_mock_scene_is_penglai04(scene) &&
        *x >= 224 && *x <= 288 &&
        *y >= 256 && *y <= 304)
    {
        *x = 256;
        *y = 245;
        return;
    }

    if (vm_net_mock_scene_is_penglai03(scene) &&
        *x >= 64 && *x <= 144 &&
        *y >= 390)
    {
        *x = 105;
        *y = 360;
        return;
    }

    (void)vm_net_mock_adjust_safe_player_pos_from_sce(scene, x, y);
}

static void vm_net_mock_role_db_path(char *path, size_t pathSize)
{
    snprintf(path, pathSize, "nvram/jhol_mock_roles.bin");
}

static u32 vm_net_mock_role_level_start_exp(u32 level)
{
    unsigned long long a = (unsigned long long)(level - 1);
    unsigned long long b = (unsigned long long)level;
    unsigned long long startExp;

    if (level <= 1)
        return 0;
    if ((a & 1ull) == 0)
        a /= 2;
    else
        b /= 2;
    if (a != 0 && b > 0xffffffffull / a)
        return 0xffffffffu;
    startExp = a * b;
    if (startExp > 0xffffffffull / VM_NET_MOCK_ROLE_EXP_PER_LEVEL)
        return 0xffffffffu;
    startExp *= VM_NET_MOCK_ROLE_EXP_PER_LEVEL;
    if (startExp > 0xffffffffull)
        return 0xffffffffu;
    return (u32)startExp;
}

static u32 vm_net_mock_role_level_from_exp(u32 exp)
{
    u32 level = 1;

    for (;;)
    {
        u32 nextLevel = level + 1;
        u32 nextLevelStart;

        if (nextLevel == 0)
            break;
        nextLevelStart = vm_net_mock_role_level_start_exp(nextLevel);
        if (nextLevelStart == 0xffffffffu || exp < nextLevelStart)
            break;
        level = nextLevel;
    }

    return level;
}

static u32 vm_net_mock_role_last_level_exp(u32 exp)
{
    return vm_net_mock_role_level_start_exp(vm_net_mock_role_level_from_exp(exp));
}

static u32 vm_net_mock_role_next_level_start_exp(u32 exp)
{
    u32 level = vm_net_mock_role_level_from_exp(exp);
    u32 nextLevel = level + 1;

    if (nextLevel == 0)
        return 0xffffffffu;
    return vm_net_mock_role_level_start_exp(nextLevel);
}

static u32 vm_net_mock_role_exp_percent(u32 exp)
{
    u32 levelStart = vm_net_mock_role_last_level_exp(exp);
    u32 nextLevelStart = vm_net_mock_role_next_level_start_exp(exp);
    u32 levelSize = 0;
    u32 current = 0;

    if (exp <= levelStart)
        return 0;
    if (nextLevelStart <= levelStart || nextLevelStart == 0xffffffffu)
        return 100;

    current = exp - levelStart;
    levelSize = nextLevelStart - levelStart;
    return (u32)(((unsigned long long)current * 100ull) / levelSize);
}

static const char *vm_net_mock_default_role_name(void)
{
    return "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
}

static const char *vm_net_mock_role_title(const vm_net_mock_role_state *role)
{
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    if (money >= 1000000)
        return "\xb8\xbb\xbf\xc9\xb5\xd0\xb9\xfa"; /* GBK: fu ke di guo */
    if (money >= 100000)
        return "\xb8\xbb\xbc\xd7\xd2\xbb\xb7\xbd"; /* GBK: fu jia yi fang */
    if (money >= 5000)
        return "\xd0\xa1\xd3\xd0\xbb\xfd\xd0\xee"; /* GBK: xiao you ji xu */
    return "\xd2\xbb\xc6\xb6\xc8\xe7\xcf\xb4";     /* GBK: yi pin ru xi */
}

static const char *vm_net_mock_role_sect_name(const vm_net_mock_role_state *role)
{
    (void)role;
    return vm_net_mock_env_str("CBE_ACTOR_SECT_NAME",
                               "\xc9\xa2\xc8\xcb"); /* GBK: san ren */
}

static const char *vm_net_mock_role_spouse_name(const vm_net_mock_role_state *role)
{
    (void)role;
    return vm_net_mock_env_str("CBE_ACTOR_SPOUSE_NAME",
                               "\xce\xde"); /* GBK: wu */
}

static u16 vm_net_mock_role_derived_attr(u32 level, u32 job, u32 attrIndex)
{
    static const u16 base[3][5] = {
        {12, 8, 7, 11, 3},
        {9, 14, 8, 8, 4},
        {7, 9, 15, 7, 5},
    };
    static const u16 gain[3][5] = {
        {3, 2, 1, 3, 1},
        {2, 3, 2, 2, 1},
        {1, 2, 4, 2, 1},
    };
    u32 jobIndex = (job == 0 || job > 3) ? 0 : job - 1;
    u32 value = 0;

    if (level == 0)
        level = 1;
    if (attrIndex >= 5)
        attrIndex = 0;
    value = base[jobIndex][attrIndex] + (level - 1) * gain[jobIndex][attrIndex];
    if (value > 999)
        value = 999;
    return (u16)value;
}

static u16 vm_net_mock_role_charm(const vm_net_mock_role_state *role, u32 level, u32 job)
{
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 value = vm_net_mock_role_derived_attr(level, job, 4) + money / 100000;
    if (value > 999)
        value = 999;
    return (u16)value;
}

static u32 vm_net_mock_cap_u32(u32 value, u32 cap)
{
    return value > cap ? cap : value;
}

static void vm_net_mock_equipment_bonus_add(vm_net_mock_equipment_bonus *dst,
                                            const vm_net_mock_equipment_bonus *src)
{
    if (dst == NULL || src == NULL)
        return;
    dst->hp += src->hp;
    dst->mp += src->mp;
    dst->attack += src->attack;
    dst->armor += src->armor;
    dst->strength += src->strength;
    dst->agility += src->agility;
    dst->wisdom += src->wisdom;
    dst->crit += src->crit;
    dst->hit += src->hit;
    dst->dodge += src->dodge;
    dst->resist += src->resist;
}

static void vm_net_mock_role_collect_equipment_bonus(const vm_net_mock_role_state *role,
                                                     u32 level,
                                                     vm_net_mock_equipment_bonus *bonus)
{
    if (bonus == NULL)
        return;
    memset(bonus, 0, sizeof(*bonus));
    if (role == NULL)
        return;
    if (level == 0)
        level = 1;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = role->equippedItemIds[slot];
        const vm_net_mock_equipment_catalog_item *item = NULL;

        if (itemId == 0)
            continue;
        item = vm_net_mock_find_equipment_catalog_item(itemId);
        if (item == NULL || item->slot != slot)
            continue;
        if (item->levelRequired > level)
            continue;
        vm_net_mock_equipment_bonus_add(bonus, &item->bonus);
    }
}

static void vm_net_mock_role_build_player_stats(const vm_net_mock_role_state *role,
                                                vm_net_mock_player_stats *stats)
{
    u32 level = role ? role->level : 1;
    u32 job = role ? role->job : 1;
    vm_net_mock_equipment_bonus equipment;

    if (stats == NULL)
        return;
    memset(stats, 0, sizeof(*stats));
    if (level == 0 && role != NULL)
        level = vm_net_mock_role_level_from_exp(role->exp);
    if (level == 0)
        level = 1;
    if (job == 0 || job > 3)
        job = 1;

    vm_net_mock_role_collect_equipment_bonus(role, level, &equipment);
    stats->level = level;
    stats->job = job;
    stats->equipment = equipment;
    stats->baseStrength = vm_net_mock_role_derived_attr(level, job, 0);
    stats->baseAgility = vm_net_mock_role_derived_attr(level, job, 1);
    stats->baseWisdom = vm_net_mock_role_derived_attr(level, job, 2);
    stats->baseEndurance = vm_net_mock_role_derived_attr(level, job, 3);
    stats->baseCharm = vm_net_mock_role_derived_attr(level, job, 4);
    stats->strength = vm_net_mock_cap_u32(stats->baseStrength + equipment.strength, 999);
    stats->agility = vm_net_mock_cap_u32(stats->baseAgility + equipment.agility, 999);
    stats->wisdom = vm_net_mock_cap_u32(stats->baseWisdom + equipment.wisdom, 999);
    stats->endurance = vm_net_mock_cap_u32(stats->baseEndurance, 999);
    stats->charm = vm_net_mock_cap_u32(vm_net_mock_role_charm(role, level, job), 999);

    stats->maxHp = 90 + level * 8 + stats->endurance * 2 + equipment.hp;
    stats->maxMp = 70 + level * 9 + stats->wisdom * 3 + equipment.mp;
    stats->attack = 6 + level * 2 + stats->strength / 2 + equipment.attack / 3;
    stats->defense = 4 + level + stats->endurance / 2 + equipment.armor / 5;
    stats->hit = 75 + level + stats->agility * 2 + equipment.hit;
    stats->dodge = 3 + level / 2 + stats->agility / 2 + equipment.dodge / 2;
    stats->crit = 1 + stats->agility / 3 + stats->wisdom / 5 + equipment.crit / 2;
    stats->resist = stats->wisdom / 2 + stats->endurance / 3 + equipment.resist;

    stats->maxHp = vm_net_mock_cap_u32(stats->maxHp, 9999);
    stats->maxMp = vm_net_mock_cap_u32(stats->maxMp, 9999);
    stats->attack = vm_net_mock_cap_u32(stats->attack, 9999);
    stats->defense = vm_net_mock_cap_u32(stats->defense, 9999);
    stats->hit = vm_net_mock_cap_u32(stats->hit, 9999);
    stats->dodge = vm_net_mock_cap_u32(stats->dodge, 9999);
    stats->crit = vm_net_mock_cap_u32(stats->crit, 9999);
    stats->resist = vm_net_mock_cap_u32(stats->resist, 9999);
}

static void vm_net_mock_role_sync_derived_vitals(vm_net_mock_role_state *role)
{
    vm_net_mock_player_stats stats;
    bool refillHp = false;
    bool refillMp = false;

    if (role == NULL)
        return;
    vm_net_mock_role_build_player_stats(role, &stats);
    refillHp = (role->hpMax == 0);
    refillMp = (role->mpMax == 0);
    role->hpMax = stats.maxHp ? stats.maxHp : VM_NET_MOCK_ROLE_DEFAULT_HP;
    role->mpMax = stats.maxMp ? stats.maxMp : VM_NET_MOCK_ROLE_DEFAULT_MP;
    if (refillHp)
        role->hp = role->hpMax;
    if (refillMp)
        role->mp = role->mpMax;
    if (role->hp > role->hpMax)
        role->hp = role->hpMax;
    if (role->mp > role->mpMax)
        role->mp = role->mpMax;
}

static u32 vm_net_mock_damage_after_defense(u32 attack, u32 defense)
{
    uint64_t scaled = 0;

    if (attack == 0)
        attack = 1;
    scaled = ((uint64_t)attack * 100ull + defense / 2u) / (100u + defense);
    if (scaled == 0)
        scaled = 1;
    if (scaled > 0xffffffffull)
        scaled = 0xffffffffull;
    return (u32)scaled;
}

static void vm_net_mock_role_default_vitals(const vm_net_mock_role_state *role,
                                            u32 *hpOut, u32 *hpMaxOut,
                                            u32 *mpOut, u32 *mpMaxOut)
{
    vm_net_mock_player_stats stats;
    u32 hp = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 mp = VM_NET_MOCK_ROLE_DEFAULT_MP;

    vm_net_mock_role_build_player_stats(role, &stats);
    if (role != NULL)
    {
        hp = role->hp;
        mp = role->mp;
    }
    if (stats.maxHp == 0)
        stats.maxHp = VM_NET_MOCK_ROLE_DEFAULT_HP;
    if (stats.maxMp == 0)
        stats.maxMp = VM_NET_MOCK_ROLE_DEFAULT_MP;
    if (hp > stats.maxHp)
        hp = stats.maxHp;
    if (mp > stats.maxMp)
        mp = stats.maxMp;
    if (hpOut)
        *hpOut = hp;
    if (hpMaxOut)
        *hpMaxOut = stats.maxHp;
    if (mpOut)
        *mpOut = mp;
    if (mpMaxOut)
        *mpMaxOut = stats.maxMp;
}

typedef enum
{
    VM_NET_MOCK_MONSTER_SLIME = 0,
    VM_NET_MOCK_MONSTER_BEAST,
    VM_NET_MOCK_MONSTER_FLYING,
    VM_NET_MOCK_MONSTER_INSECT,
    VM_NET_MOCK_MONSTER_REPTILE,
    VM_NET_MOCK_MONSTER_UNDEAD,
    VM_NET_MOCK_MONSTER_SPIRIT,
    VM_NET_MOCK_MONSTER_ELEMENTAL,
    VM_NET_MOCK_MONSTER_STONE,
    VM_NET_MOCK_MONSTER_HUMANOID,
    VM_NET_MOCK_MONSTER_SOLDIER,
    VM_NET_MOCK_MONSTER_BOSS
} vm_net_mock_monster_family;

typedef struct
{
    u16 enemyId;
    u8 level;
    u8 family;
    u32 dropItemId;
    u8 dropRatePercent;
} vm_net_mock_monster_entry;

typedef struct
{
    u32 enemyId;
    u32 level;
    u32 hp;
    u32 mp;
    u32 attack;
    u32 defense;
    u32 exp;
    u32 gold;
    u32 dropItemId;
    u32 dropRatePercent;
} vm_net_mock_monster_stats;

static const vm_net_mock_monster_entry g_vm_net_mock_monster_entries[] = {
    {  1,  6, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    {  3,  1, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    {  4,  2, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    {  6,  7, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    {  9,  3, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 13, 12, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 18, 38, VM_NET_MOCK_MONSTER_ELEMENTAL, 0, 0},
    { 19, 28, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 22, 12, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 25,  4, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 28,  7, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    { 29,  8, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    { 30,  8, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    { 31,  9, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 32, 10, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 34, 11, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 36, 14, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    { 40, 20, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 41, 20, VM_NET_MOCK_MONSTER_SLIME, 0, 0},
    { 42, 22, VM_NET_MOCK_MONSTER_ELEMENTAL, 0, 0},
    { 45, 22, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 47, 27, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 48, 28, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 49, 31, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 50, 30, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    { 51, 31, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 52, 28, VM_NET_MOCK_MONSTER_REPTILE, 0, 0},
    { 53, 29, VM_NET_MOCK_MONSTER_REPTILE, 0, 0},
    { 54, 29, VM_NET_MOCK_MONSTER_REPTILE, 0, 0},
    { 55, 34, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 56, 34, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 57, 32, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 60, 36, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 64, 37, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 67, 27, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 69, 24, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 70, 24, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 71, 23, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    { 73,  5, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 74,  5, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 75,  6, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 76,  3, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 77,  4, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 78,  9, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 79, 11, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    { 81, 13, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 82, 15, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 83, 15, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 84, 17, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 86, 17, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 87, 17, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 89, 18, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 91, 21, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    { 92, 23, VM_NET_MOCK_MONSTER_SLIME, 0, 0},
    { 94, 26, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    { 97, 36, VM_NET_MOCK_MONSTER_ELEMENTAL, 0, 0},
    { 98, 38, VM_NET_MOCK_MONSTER_SLIME, 0, 0},
    { 99, 39, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    {101, 39, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    {103, 40, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    {104, 41, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    {105,  1, VM_NET_MOCK_MONSTER_SLIME, VM_NET_MOCK_BATTLE_CHANGMING_SAN_ITEM_ID, VM_NET_MOCK_BATTLE_CHANGMING_SAN_DROP_RATE},
    {106,  2, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    {107,  6, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    {110, 46, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    {111, 48, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    {112, 50, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    {120, 50, VM_NET_MOCK_MONSTER_REPTILE, 0, 0},
    {121, 52, VM_NET_MOCK_MONSTER_ELEMENTAL, 0, 0},
    {122, 53, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    {200, 43, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    {201, 44, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    {202, 45, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0}
};

static vm_net_mock_monster_entry vm_net_mock_monster_entry_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_entry fallback;

    if (enemyId == 0)
        enemyId = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
    for (u32 i = 0; i < sizeof(g_vm_net_mock_monster_entries) / sizeof(g_vm_net_mock_monster_entries[0]); ++i)
    {
        if (g_vm_net_mock_monster_entries[i].enemyId == enemyId)
            return g_vm_net_mock_monster_entries[i];
    }

    memset(&fallback, 0, sizeof(fallback));
    fallback.enemyId = (enemyId <= 0xffffu) ? (u16)enemyId : VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
    fallback.family = VM_NET_MOCK_MONSTER_BEAST;
    if (enemyId >= 200)
        fallback.level = 45;
    else if (enemyId >= 120)
        fallback.level = 50;
    else if (enemyId >= 100)
        fallback.level = 30;
    else if (enemyId >= 70)
        fallback.level = 20;
    else if (enemyId >= 30)
        fallback.level = 10;
    else
        fallback.level = 3;
    return fallback;
}

static vm_net_mock_monster_stats vm_net_mock_monster_stats_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_entry entry = vm_net_mock_monster_entry_for_enemy(enemyId);
    vm_net_mock_monster_stats stats;
    u32 level = entry.level ? entry.level : 1;

    memset(&stats, 0, sizeof(stats));
    stats.enemyId = entry.enemyId;
    stats.level = level;
    stats.dropItemId = entry.dropItemId;
    stats.dropRatePercent = entry.dropRatePercent;

    switch ((vm_net_mock_monster_family)entry.family)
    {
    case VM_NET_MOCK_MONSTER_SLIME:
        stats.hp = 16 + level * 4;
        stats.mp = 18 + level * 2;
        stats.attack = 6 + level * 2;
        stats.defense = 2 + level / 4;
        stats.exp = 3 + level * 2;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_BEAST:
        stats.hp = 26 + level * 7;
        stats.mp = 10 + level;
        stats.attack = 7 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_FLYING:
        stats.hp = 18 + level * 5;
        stats.mp = 12 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 1 + level / 4;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_INSECT:
        stats.hp = 16 + level * 5;
        stats.mp = 12 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 1 + level / 5;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_REPTILE:
        stats.hp = 24 + level * 6;
        stats.mp = 14 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 5 + level * 3;
        stats.gold = 4 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_UNDEAD:
        stats.hp = 34 + level * 8;
        stats.mp = 10 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 4 + level / 3;
        stats.exp = 6 + level * 3;
        stats.gold = 4 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_SPIRIT:
        stats.hp = 22 + level * 6;
        stats.mp = 16 + level * 3;
        stats.attack = 10 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 6 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_ELEMENTAL:
        stats.hp = 30 + level * 7;
        stats.mp = 20 + level * 4;
        stats.attack = 11 + level * 2;
        stats.defense = 3 + level / 3;
        stats.exp = 7 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_STONE:
        stats.hp = 42 + level * 9;
        stats.mp = 12 + level * 2;
        stats.attack = 8 + level * 2;
        stats.defense = 6 + level / 2;
        stats.exp = 8 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_HUMANOID:
        stats.hp = 30 + level * 7;
        stats.mp = 12 + level * 2;
        stats.attack = 9 + level * 2;
        stats.defense = 3 + level / 3;
        stats.exp = 6 + level * 3;
        stats.gold = 6 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_SOLDIER:
        stats.hp = 34 + level * 8;
        stats.mp = 10 + level;
        stats.attack = 10 + level * 2;
        stats.defense = 4 + level / 3;
        stats.exp = 7 + level * 3;
        stats.gold = 7 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_BOSS:
        stats.hp = 80 + level * 12;
        stats.mp = 24 + level * 4;
        stats.attack = 14 + level * 3;
        stats.defense = 8 + level / 2;
        stats.exp = 20 + level * 5;
        stats.gold = 25 + level * 4;
        break;
    default:
        stats.hp = 20 + level * 5;
        stats.mp = 10 + level;
        stats.attack = 7 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    }

    if (stats.enemyId == VM_NET_MOCK_BATTLE_POISON_SLIME_ID)
    {
        stats.exp = VM_NET_MOCK_BATTLE_POISON_SLIME_EXP;
        stats.gold = VM_NET_MOCK_BATTLE_POISON_SLIME_GOLD;
    }
    if (stats.hp == 0)
        stats.hp = 1;
    if (stats.mp == 0)
        stats.mp = 1;
    if (stats.attack == 0)
        stats.attack = 1;
    if (stats.exp == 0)
        stats.exp = 1;
    return stats;
}

static u32 vm_net_mock_battle_role_attack_default(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats stats;
    vm_net_mock_role_build_player_stats(role, &stats);
    return stats.attack ? stats.attack : 1;
}

static u32 vm_net_mock_battle_role_defense_default(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats stats;
    vm_net_mock_role_build_player_stats(role, &stats);
    return stats.defense;
}

static u32 vm_net_mock_battle_player_damage_to_enemy(u32 enemyId, u32 enemyHpCurrent)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    u32 attack = vm_net_mock_env_u32_if_set("CBE_BATTLE_PLAYER_ATTACK",
                                            vm_net_mock_battle_role_attack_default());
    u32 defense = vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_DEFENSE", stats.defense);
    u32 damage = vm_net_mock_damage_after_defense(attack, defense);

    if (enemyHpCurrent == 0)
        return 0;
    if (damage == 0)
        damage = 1;
    return vm_net_mock_min_u32(damage, enemyHpCurrent);
}

static u32 vm_net_mock_battle_skill_min_hp_damage(const vm_net_mock_skill_catalog_item *skill)
{
    if (skill == NULL || skill->hpChange >= 0)
        return 0;
    return (u32)(0 - skill->hpChange);
}

static u32 vm_net_mock_battle_player_skill_damage_to_enemy(u32 operate, u32 enemyId,
                                                           u32 enemyHpCurrent)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats playerStats;
    vm_net_mock_monster_stats monsterStats = vm_net_mock_monster_stats_for_enemy(enemyId);
    u32 baseDamage = vm_net_mock_battle_skill_min_hp_damage(skill);
    uint64_t coeffDamage = 0;
    u32 rawDamage = 0;
    u32 defense = 0;
    u32 damage = 0;

    if (enemyHpCurrent == 0)
        return 0;
    if (skill == NULL || baseDamage == 0)
        return vm_net_mock_battle_player_damage_to_enemy(enemyId, enemyHpCurrent);

    vm_net_mock_role_build_player_stats(role, &playerStats);
    coeffDamage += (uint64_t)playerStats.strength * skill->strengthCoeff;
    coeffDamage += (uint64_t)playerStats.agility * skill->agilityCoeff;
    coeffDamage += (uint64_t)playerStats.wisdom * skill->wisdomCoeff;
    coeffDamage = (coeffDamage + 50u) / 100u;
    if (coeffDamage > 0xffffffffull - baseDamage)
        rawDamage = 0xffffffffu;
    else
        rawDamage = baseDamage + (u32)coeffDamage;

    defense = vm_net_mock_env_u32_if_set("CBE_BATTLE_SKILL_ENEMY_DEFENSE",
                                         monsterStats.defense);
    damage = vm_net_mock_damage_after_defense(rawDamage, defense);
    if (damage < baseDamage)
        damage = baseDamage;
    damage = vm_net_mock_env_u32_if_set("CBE_BATTLE_SKILL_DAMAGE", damage);
    if (damage == 0)
        damage = 1;
    return vm_net_mock_min_u32(damage, enemyHpCurrent);
}

static u32 vm_net_mock_battle_enemy_damage_to_role(u32 enemyId, u32 roleHpCurrent)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    u32 attack = vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_ATTACK", stats.attack);
    u32 defense = vm_net_mock_env_u32_if_set("CBE_BATTLE_ROLE_DEFENSE",
                                             vm_net_mock_battle_role_defense_default());
    u32 damage = vm_net_mock_damage_after_defense(attack, defense);

    if (roleHpCurrent == 0)
        return 0;
    if (damage == 0)
        damage = 1;
    return vm_net_mock_min_u32(damage, roleHpCurrent);
}

static const char *vm_net_mock_role_initial_scene_name(void)
{
    return vm_net_mock_default_scene_name();
}

static u32 vm_net_mock_role_default_weapon_for_job(u32 job)
{
    switch (job)
    {
    case 2:
        return 1501; /* starter dagger */
    case 3:
        return 2001; /* starter staff */
    case 1:
    default:
        return 1001; /* starter sword */
    }
}

static void vm_net_mock_role_init_default_equipment(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role->equippedItemIds, 0, sizeof(role->equippedItemIds));
    role->equippedItemIds[0] = vm_net_mock_role_default_weapon_for_job(role->job);
}

static void vm_net_mock_role_init_default_backpack(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role->backpackItems, 0, sizeof(role->backpackItems));
    role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY;
    role->backpackItemCount = 1;
    role->nextBackpackSeq = VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_SEQ + 1;
    role->backpackItems[0].itemId = VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID;
    role->backpackItems[0].seq = VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_SEQ;
    role->backpackItems[0].count = VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_COUNT;
}

static bool vm_net_mock_role_has_default_name(const vm_net_mock_role_state *role)
{
    return role != NULL && strcmp(role->name, vm_net_mock_default_role_name()) == 0;
}

static void vm_net_mock_role_assign_fallback_name(vm_net_mock_role_state *role)
{
    u32 roleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    if (role == NULL)
        return;
    if (role->roleId != 0)
        roleId = role->roleId;
    snprintf(role->name, sizeof(role->name), "Role%u", roleId);
}

static void vm_net_mock_role_init_default(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role, 0, sizeof(*role));
    role->roleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    snprintf(role->name, sizeof(role->name), "%s", vm_net_mock_default_role_name());
    role->job = 1;
    role->sex = 0;
    role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY;
    role->level = 1;
    role->exp = 0;
    role->hp = VM_NET_MOCK_ROLE_DEFAULT_HP;
    role->hpMax = VM_NET_MOCK_ROLE_DEFAULT_HP;
    role->mp = VM_NET_MOCK_ROLE_DEFAULT_MP;
    role->mpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
    role->money = VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    snprintf(role->scene, sizeof(role->scene), "%s", vm_net_mock_role_initial_scene_name());
    role->x = VM_NET_MOCK_ROLE_INITIAL_X;
    role->y = VM_NET_MOCK_ROLE_INITIAL_Y;
    vm_net_mock_role_init_default_equipment(role);
    vm_net_mock_role_init_default_backpack(role);
    vm_net_mock_role_sync_derived_vitals(role);
}

static void vm_net_mock_role_copy_from_v1(vm_net_mock_role_state *dst,
                                          const vm_net_mock_role_state_v1 *src)
{
    if (dst == NULL || src == NULL)
        return;
    memset(dst, 0, sizeof(*dst));
    dst->roleId = src->roleId;
    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->job = src->job;
    dst->sex = src->sex;
    dst->backpackCapacity = src->backpackCapacity;
    dst->level = src->level;
    dst->exp = src->exp;
    dst->hp = src->hp;
    dst->hpMax = src->hpMax;
    dst->mp = src->mp;
    dst->mpMax = src->mpMax;
    dst->money = src->money;
    memcpy(dst->scene, src->scene, sizeof(dst->scene));
    dst->x = src->x;
    dst->y = src->y;
    vm_net_mock_role_init_default_equipment(dst);
    vm_net_mock_role_init_default_backpack(dst);
}

static void vm_net_mock_role_copy_from_v2(vm_net_mock_role_state *dst,
                                          const vm_net_mock_role_state_v2 *src)
{
    if (dst == NULL || src == NULL)
        return;
    memset(dst, 0, sizeof(*dst));
    dst->roleId = src->roleId;
    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->job = src->job;
    dst->sex = src->sex;
    dst->backpackCapacity = src->backpackCapacity;
    dst->level = src->level;
    dst->exp = src->exp;
    dst->hp = src->hp;
    dst->hpMax = src->hpMax;
    dst->mp = src->mp;
    dst->mpMax = src->mpMax;
    dst->money = src->money;
    memcpy(dst->scene, src->scene, sizeof(dst->scene));
    dst->x = src->x;
    dst->y = src->y;
    dst->backpackItemCount = src->backpackItemCount;
    dst->reserved1 = src->reserved1;
    dst->nextBackpackSeq = src->nextBackpackSeq;
    memcpy(dst->backpackItems, src->backpackItems, sizeof(dst->backpackItems));
    vm_net_mock_role_init_default_equipment(dst);
}

static void vm_net_mock_role_normalize_backpack(vm_net_mock_role_state *role)
{
    vm_net_mock_backpack_item_state compact[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
    u32 compactCount = 0;
    u16 maxSeq = 0;

    if (role == NULL)
        return;
    memset(compact, 0, sizeof(compact));
    if (role->backpackCapacity == 0 || role->backpackCapacity > VM_NET_MOCK_BACKPACK_CAPACITY)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY;
    if (role->backpackItemCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        role->backpackItemCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    if (role->backpackItemCount > role->backpackCapacity)
        role->backpackItemCount = role->backpackCapacity;

    for (u32 i = 0; i < role->backpackItemCount; ++i)
    {
        vm_net_mock_backpack_item_state item = role->backpackItems[i];
        if (item.itemId == 0 || item.count == 0)
            continue;
        if (item.seq == 0)
            item.seq = (u16)(maxSeq + 1);
        if (item.seq > maxSeq)
            maxSeq = item.seq;
        compact[compactCount++] = item;
        if (compactCount >= role->backpackCapacity ||
            compactCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        {
            break;
        }
    }
    memset(role->backpackItems, 0, sizeof(role->backpackItems));
    if (compactCount > 0)
        memcpy(role->backpackItems, compact, sizeof(compact[0]) * compactCount);
    role->backpackItemCount = (u8)compactCount;
    if (role->nextBackpackSeq == 0 || role->nextBackpackSeq <= maxSeq)
        role->nextBackpackSeq = (u16)(maxSeq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
}

static void vm_net_mock_role_normalize(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    if (role->roleId == 0)
        role->roleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    role->name[sizeof(role->name) - 1] = 0;
    if (role->name[0] == 0)
        snprintf(role->name, sizeof(role->name), "%s", vm_net_mock_default_role_name());
    if (role->job == 0 || role->job > 3)
        role->job = 1;
    if (role->sex > 1)
        role->sex = 0;
    if (role->backpackCapacity == 0 || role->backpackCapacity > VM_NET_MOCK_BACKPACK_CAPACITY)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY;
    role->level = vm_net_mock_role_level_from_exp(role->exp);
    vm_net_mock_role_sync_derived_vitals(role);
    role->scene[sizeof(role->scene) - 1] = 0;
    if (!vm_net_mock_scene_name_is_safe(role->scene))
        snprintf(role->scene, sizeof(role->scene), "%s", vm_net_mock_role_initial_scene_name());
    if (role->x == 0 || role->y == 0)
    {
        role->x = VM_NET_MOCK_ROLE_INITIAL_X;
        role->y = VM_NET_MOCK_ROLE_INITIAL_Y;
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(role->scene, &role->x, &role->y);
    vm_net_mock_role_normalize_backpack(role);
}

static u32 vm_net_mock_role_db_repair_duplicate_default_names(void)
{
    bool seenDefaultName = false;
    u32 repairCount = 0;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        role->name[sizeof(role->name) - 1] = 0;
        if (!vm_net_mock_role_has_default_name(role))
            continue;
        if (!seenDefaultName)
        {
            seenDefaultName = true;
            continue;
        }
        vm_net_mock_role_assign_fallback_name(role);
        ++repairCount;
    }
    return repairCount;
}

static void vm_net_mock_role_db_save(const char *reason)
{
    char path[128];
    if (!g_vm_net_mock_role_db_valid)
        return;
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    if (g_vm_net_mock_role_db.roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        g_vm_net_mock_role_db.roleCount = VM_NET_MOCK_ROLE_DB_MAX_ROLES;
    if (g_vm_net_mock_role_db.roleCount == 0)
        g_vm_net_mock_role_db.activeRoleId = 0;
#ifdef _WIN32
    _mkdir("nvram");
#else
    mkdir("nvram", 0777);
#endif
    vm_net_mock_role_db_path(path, sizeof(path));
    FILE *fp = fopen(path, "wb");
    if (fp)
    {
        fwrite(&g_vm_net_mock_role_db, 1, sizeof(g_vm_net_mock_role_db), fp);
        fclose(fp);
        vm_autotest_note("mock_role_db_save reason=%s roles=%u active=%u\n",
                         reason ? reason : "state",
                         g_vm_net_mock_role_db.roleCount,
                         g_vm_net_mock_role_db.activeRoleId);
    }
}

static void vm_net_mock_role_db_load(void)
{
    char path[128];
    u8 fileBuf[sizeof(vm_net_mock_role_db_file)];
    vm_net_mock_role_db_file loaded;
    vm_net_mock_role_db_file_v2 backpackFile;
    vm_net_mock_role_db_file_v1 legacy;
    bool loadedFromFile = false;
    bool needsSave = false;

    if (g_vm_net_mock_role_db_loaded)
        return;
    g_vm_net_mock_role_db_loaded = true;
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    g_vm_net_mock_role_db.activeRoleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    g_vm_net_mock_role_db.roleCount = 1;
    vm_net_mock_role_init_default(&g_vm_net_mock_role_db.roles[0]);

    vm_net_mock_role_db_path(path, sizeof(path));
    FILE *fp = fopen(path, "rb");
    if (fp)
    {
        size_t readLen = fread(fileBuf, 1, sizeof(fileBuf), fp);
        fclose(fp);
        if (readLen == sizeof(loaded))
            memcpy(&loaded, fileBuf, sizeof(loaded));
        if (readLen == sizeof(loaded) &&
            memcmp(loaded.magic, "JHR1", 4) == 0 &&
            loaded.version == VM_NET_MOCK_ROLE_DB_VERSION &&
            loaded.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        {
            g_vm_net_mock_role_db = loaded;
            loadedFromFile = true;
        }
        else if (readLen == sizeof(backpackFile))
        {
            memcpy(&backpackFile, fileBuf, sizeof(backpackFile));
            if (memcmp(backpackFile.magic, "JHR1", 4) == 0 &&
                backpackFile.version == VM_NET_MOCK_ROLE_DB_BACKPACK_VERSION &&
                backpackFile.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
                memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
                g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
                g_vm_net_mock_role_db.activeRoleId = backpackFile.activeRoleId;
                g_vm_net_mock_role_db.roleCount = backpackFile.roleCount;
                for (u32 i = 0; i < backpackFile.roleCount; ++i)
                    vm_net_mock_role_copy_from_v2(&g_vm_net_mock_role_db.roles[i],
                                                  &backpackFile.roles[i]);
                loadedFromFile = true;
                needsSave = true;
                vm_autotest_note("mock_role_db_migrate version=2->3 roles=%u active=%u\n",
                                 g_vm_net_mock_role_db.roleCount,
                                 g_vm_net_mock_role_db.activeRoleId);
            }
        }
        else if (readLen == sizeof(legacy))
        {
            memcpy(&legacy, fileBuf, sizeof(legacy));
            if (memcmp(legacy.magic, "JHR1", 4) == 0 &&
                legacy.version == VM_NET_MOCK_ROLE_DB_LEGACY_VERSION &&
                legacy.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
                memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
                g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
                g_vm_net_mock_role_db.activeRoleId = legacy.activeRoleId;
                g_vm_net_mock_role_db.roleCount = legacy.roleCount;
                for (u32 i = 0; i < legacy.roleCount; ++i)
                    vm_net_mock_role_copy_from_v1(&g_vm_net_mock_role_db.roles[i],
                                                  &legacy.roles[i]);
                loadedFromFile = true;
                needsSave = true;
                vm_autotest_note("mock_role_db_migrate version=1->3 roles=%u active=%u\n",
                                 g_vm_net_mock_role_db.roleCount,
                                 g_vm_net_mock_role_db.activeRoleId);
            }
        }
    }

    if (!loadedFromFile)
    {
        needsSave = true;
    }

    if (g_vm_net_mock_role_db.roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        g_vm_net_mock_role_db.roleCount = 1;
        needsSave = true;
    }
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        vm_net_mock_role_normalize(&g_vm_net_mock_role_db.roles[i]);
    u32 repairedDefaultNames = vm_net_mock_role_db_repair_duplicate_default_names();
    if (repairedDefaultNames > 0)
    {
        needsSave = true;
        vm_autotest_note("mock_role_db_repair_duplicate_default_names count=%u roles=%u active=%u\n",
                         repairedDefaultNames,
                         g_vm_net_mock_role_db.roleCount,
                         g_vm_net_mock_role_db.activeRoleId);
    }

    bool activeFound = false;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == g_vm_net_mock_role_db.activeRoleId)
        {
            activeFound = true;
            break;
        }
    }
    if (!activeFound && g_vm_net_mock_role_db.roleCount > 0)
    {
        g_vm_net_mock_role_db.activeRoleId = g_vm_net_mock_role_db.roles[0].roleId;
        needsSave = true;
    }
    else if (g_vm_net_mock_role_db.roleCount == 0 && g_vm_net_mock_role_db.activeRoleId != 0)
    {
        g_vm_net_mock_role_db.activeRoleId = 0;
        needsSave = true;
    }
    g_vm_net_mock_role_db_valid = true;
    if (needsSave)
        vm_net_mock_role_db_save(loadedFromFile ? "normalize" : "init");
}

static vm_net_mock_role_state *vm_net_mock_active_role(void)
{
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid || g_vm_net_mock_role_db.roleCount == 0)
        return NULL;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == g_vm_net_mock_role_db.activeRoleId)
            return &g_vm_net_mock_role_db.roles[i];
    }
    g_vm_net_mock_role_db.activeRoleId = g_vm_net_mock_role_db.roles[0].roleId;
    return &g_vm_net_mock_role_db.roles[0];
}

static bool vm_net_mock_select_active_role(u32 roleId)
{
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid || roleId == 0)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
        {
            if (g_vm_net_mock_role_db.activeRoleId != roleId)
            {
                g_vm_net_mock_role_db.activeRoleId = roleId;
                vm_net_mock_role_db_save("role-select");
            }
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_role_db_has_role_id(u32 roleId)
{
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid || roleId == 0)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
            return true;
    }
    return false;
}

static u8 vm_net_mock_active_role_job_or(u8 fallback)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role == NULL || role->job == 0 || role->job > 3)
        return fallback;
    return role->job;
}

static u8 vm_net_mock_active_role_sex_or(u8 fallback)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role == NULL || role->sex > 1)
        return fallback;
    return role->sex;
}

static u8 vm_net_mock_role_db_sex_from_title_value(u8 rawSex)
{
    if (rawSex >= 1 && rawSex <= 2)
        return (u8)(rawSex - 1);
    if (rawSex == 0)
        return 0;
    return 0;
}

static u8 vm_net_mock_role_db_job_from_title_value(u8 rawJob, bool rawJobIsIndex)
{
    if (rawJobIsIndex)
    {
        if (rawJob <= 2)
            return (u8)(rawJob + 1);
        return 1;
    }
    if (rawJob >= 1 && rawJob <= 3)
        return rawJob;
    if (rawJob == 0)
        return 1;
    return 1;
}

static u32 vm_net_mock_role_db_next_role_id(void)
{
    u32 nextRoleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    vm_net_mock_role_db_load();
    if (g_vm_net_mock_role_db_valid)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            if (g_vm_net_mock_role_db.roles[i].roleId >= nextRoleId)
                nextRoleId = g_vm_net_mock_role_db.roles[i].roleId + 1;
        }
    }
    if (nextRoleId == 0)
        nextRoleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    return nextRoleId;
}

static void vm_net_mock_copy_role_name(char *out, size_t outCap, const char *name)
{
    size_t copyLen = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (name != NULL)
    {
        copyLen = strlen(name);
        if (copyLen >= outCap)
            copyLen = outCap - 1;
        while (copyLen > 0 && name[copyLen - 1] == 0)
            --copyLen;
        if (copyLen > 0)
            memcpy(out, name, copyLen);
        out[copyLen] = 0;
    }
    if (out[0] == 0)
        snprintf(out, outCap, "%s", vm_net_mock_default_role_name());
}

static bool vm_net_mock_role_db_create_from_title(const vm_net_mock_title_role_create_request *request,
                                                  u32 *actorIdOut,
                                                  u8 *resultOut)
{
    vm_net_mock_role_state *role = NULL;
    u32 actorId = 0;
    u8 result = 1;

    if (actorIdOut)
        *actorIdOut = 0;
    if (resultOut)
        *resultOut = result;
    if (request == NULL)
        return false;

    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid ||
        g_vm_net_mock_role_db.roleCount >= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        return true;
    }

    actorId = vm_net_mock_role_db_next_role_id();
    role = &g_vm_net_mock_role_db.roles[g_vm_net_mock_role_db.roleCount];
    vm_net_mock_role_init_default(role);
    role->roleId = actorId;
    if (request->name[0] != 0)
        vm_net_mock_copy_role_name(role->name, sizeof(role->name), request->name);
    else
        vm_net_mock_role_assign_fallback_name(role);
    role->job = vm_net_mock_role_db_job_from_title_value(request->rawJob, request->rawJobIsIndex);
    role->sex = vm_net_mock_role_db_sex_from_title_value(request->rawSex);
    vm_net_mock_role_init_default_equipment(role);
    vm_net_mock_role_normalize(role);
    role->hp = role->hpMax;
    role->mp = role->mpMax;

    g_vm_net_mock_role_db.roleCount += 1;
    g_vm_net_mock_role_db.activeRoleId = actorId;
    vm_net_mock_role_db_save("role-create");

    result = 0;
    if (actorIdOut)
        *actorIdOut = actorId;
    if (resultOut)
        *resultOut = result;
    return true;
}

static bool vm_net_mock_role_db_delete_by_id(u32 actorId, u8 *resultOut, u32 *roleCountOut)
{
    u8 result = 1;
    u32 deleteIndex = VM_NET_MOCK_ROLE_DB_MAX_ROLES;

    if (resultOut)
        *resultOut = result;
    if (roleCountOut)
        *roleCountOut = 0;
    if (actorId == 0)
        return false;

    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid)
        return false;

    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == actorId)
        {
            deleteIndex = i;
            break;
        }
    }

    if (deleteIndex == VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (roleCountOut)
            *roleCountOut = g_vm_net_mock_role_db.roleCount;
        return true;
    }

    for (u32 i = deleteIndex; i + 1 < g_vm_net_mock_role_db.roleCount; ++i)
        g_vm_net_mock_role_db.roles[i] = g_vm_net_mock_role_db.roles[i + 1];
    if (g_vm_net_mock_role_db.roleCount > 0)
    {
        memset(&g_vm_net_mock_role_db.roles[g_vm_net_mock_role_db.roleCount - 1],
               0,
               sizeof(g_vm_net_mock_role_db.roles[g_vm_net_mock_role_db.roleCount - 1]));
        g_vm_net_mock_role_db.roleCount -= 1;
    }

    if (g_vm_net_mock_role_db.roleCount == 0)
    {
        g_vm_net_mock_role_db.activeRoleId = 0;
    }
    else if (g_vm_net_mock_role_db.activeRoleId == actorId)
    {
        g_vm_net_mock_role_db.activeRoleId = g_vm_net_mock_role_db.roles[0].roleId;
    }

    vm_net_mock_role_db_save("role-delete");
    result = 0;
    if (resultOut)
        *resultOut = result;
    if (roleCountOut)
        *roleCountOut = g_vm_net_mock_role_db.roleCount;
    return true;
}

static void vm_net_mock_role_set_position(const char *scene, u16 x, u16 y, const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role == NULL || scene == NULL || scene[0] == 0 || x == 0 || y == 0)
        return;
    snprintf(role->scene, sizeof(role->scene), "%s", scene);
    role->x = x;
    role->y = y;
    vm_net_mock_role_normalize(role);
    vm_net_mock_role_db_save(reason ? reason : "position");
}

static bool vm_net_mock_role_add_backpack_item(u32 itemId, u32 count, u16 *seqOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u8 itemCount = 0;

    if (seqOut)
        *seqOut = 0;
    if (role == NULL || itemId == 0 || count == 0)
        return false;

    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->itemId == itemId)
        {
            if (0xffffffffu - item->count < count)
                item->count = 0xffffffffu;
            else
                item->count += count;
            if (seqOut)
                *seqOut = item->seq;
            vm_net_mock_role_normalize_backpack(role);
            vm_net_mock_role_db_save("backpack-add-stack");
            return true;
        }
    }

    if (itemCount >= role->backpackCapacity || itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        return false;

    vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->seq = role->nextBackpackSeq;
    if (item->seq == 0)
        item->seq = 1;
    item->count = count;
    role->backpackItemCount = (u8)(itemCount + 1);
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
    if (seqOut)
        *seqOut = item->seq;
    vm_net_mock_role_normalize_backpack(role);
    vm_net_mock_role_db_save("backpack-add-item");
    return true;
}

static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item(vm_net_mock_role_state *role,
                                                                            u32 itemId,
                                                                            u16 seq)
{
    u8 itemCount = 0;

    if (role == NULL || (itemId == 0 && seq == 0))
        return NULL;

    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    if (seq != 0)
    {
        for (u32 i = 0; i < itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
            if (item->seq == seq && (itemId == 0 || item->itemId == itemId))
                return item;
        }
    }
    if (itemId != 0)
    {
        for (u32 i = 0; i < itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
            if (item->itemId == itemId)
                return item;
        }
    }
    return NULL;
}

static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item_by_effect(vm_net_mock_role_state *role,
                                                                                      u32 hp,
                                                                                      u32 mp,
                                                                                      u32 exp)
{
    u8 itemCount = 0;

    if (role == NULL || (hp == 0 && mp == 0 && exp == 0))
        return NULL;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        const vm_net_mock_item_effect_catalog_item *effect =
            vm_net_mock_find_item_effect_catalog_item(item->itemId);
        if (!vm_net_mock_item_effect_is_usable(effect))
            continue;
        if ((hp == 0 || effect->hp == hp) &&
            (mp == 0 || effect->mp == mp) &&
            (exp == 0 || effect->exp == exp))
        {
            return item;
        }
    }
    for (u32 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        const vm_net_mock_item_effect_catalog_item *effect =
            vm_net_mock_find_item_effect_catalog_item(item->itemId);
        if (vm_net_mock_item_effect_is_usable(effect))
            return item;
    }
    return NULL;
}

static bool vm_net_mock_role_consume_backpack_item(vm_net_mock_role_state *role,
                                                   u32 itemId,
                                                   u16 seq,
                                                   u32 count,
                                                   u32 *remainingOut)
{
    vm_net_mock_backpack_item_state *item = NULL;

    if (remainingOut)
        *remainingOut = 0;
    if (role == NULL || count == 0)
        return false;

    item = vm_net_mock_role_find_backpack_item(role, itemId, seq);
    if (item == NULL || item->count == 0)
        return false;

    if (item->count <= count)
    {
        item->count = 0;
        if (remainingOut)
            *remainingOut = 0;
    }
    else
    {
        item->count -= count;
        if (remainingOut)
            *remainingOut = item->count;
    }
    vm_net_mock_role_normalize_backpack(role);
    return true;
}

static u32 vm_net_mock_battle_reward_rand(void)
{
    if (g_vm_net_mock_battle_reward_rng == 0)
    {
        g_vm_net_mock_battle_reward_rng =
            0x6d2b79f5u ^
            (g_schedulerTick * 1664525u) ^
            (g_mockBattleOperateSessionSerial * 1013904223u) ^
            (g_vm_net_mock_role_db.activeRoleId << 1);
        if (g_vm_net_mock_battle_reward_rng == 0)
            g_vm_net_mock_battle_reward_rng = 0x9e3779b9u;
    }

    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng << 13;
    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng >> 17;
    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng << 5;
    return g_vm_net_mock_battle_reward_rng;
}

static bool vm_net_mock_battle_roll_percent(u32 percent)
{
    if (percent == 0)
        return false;
    if (percent >= 100)
        return true;
    return (vm_net_mock_battle_reward_rand() % 100u) < percent;
}

static u32 vm_net_mock_battle_reward_exp_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    return stats.exp;
}

static u32 vm_net_mock_battle_reward_gold_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    return stats.gold;
}

static u32 vm_net_mock_battle_grant_reward_once(u32 *dropItemIdOut,
                                                u16 *dropSeqOut,
                                                bool *dropGrantedOut)
{
    u32 rewardExp = 0;
    u32 dropItemId = 0;
    u16 dropSeq = 0;
    bool dropGranted = false;
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(g_vm_net_mock_battle_enemy_id_current);
    u32 dropRate = stats.dropRatePercent;
    u32 dropItemIdDefault = stats.dropItemId;

    if (dropItemIdOut)
        *dropItemIdOut = 0;
    if (dropSeqOut)
        *dropSeqOut = 0;
    if (dropGrantedOut)
        *dropGrantedOut = false;

    if (g_mockBattleOperateSessionSerial == 0)
        return 0;

    if (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial)
    {
        if (dropItemIdOut)
            *dropItemIdOut = g_vm_net_mock_battle_rewarded_drop_item;
        if (dropSeqOut)
            *dropSeqOut = g_vm_net_mock_battle_rewarded_drop_seq;
        if (dropGrantedOut)
            *dropGrantedOut = g_vm_net_mock_battle_rewarded_drop_item != 0;
        return 0;
    }

    rewardExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_EXP",
                                           vm_net_mock_battle_reward_exp_for_enemy(g_vm_net_mock_battle_enemy_id_current));
    if (g_vm_net_mock_battle_enemy_id_current == VM_NET_MOCK_BATTLE_POISON_SLIME_ID)
    {
        dropRate = vm_net_mock_env_u32_if_set("CBE_BATTLE_CHANGMING_SAN_DROP_RATE", dropRate);
        dropItemIdDefault = vm_net_mock_env_u32_if_set("CBE_BATTLE_CHANGMING_SAN_ITEM_ID",
                                                       dropItemIdDefault);
    }
    dropRate = vm_net_mock_env_u32_if_set("CBE_BATTLE_DROP_RATE", dropRate);
    dropItemIdDefault = vm_net_mock_env_u32_if_set("CBE_BATTLE_DROP_ITEM_ID", dropItemIdDefault);
    if (dropItemIdDefault != 0 && vm_net_mock_battle_roll_percent(dropRate))
    {
        dropItemId = dropItemIdDefault;
        dropGranted = vm_net_mock_role_add_backpack_item(dropItemId, 1, &dropSeq);
        if (!dropGranted)
        {
            dropItemId = 0;
            dropSeq = 0;
        }
    }

    g_vm_net_mock_battle_rewarded_serial = g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = rewardExp;
    g_vm_net_mock_battle_rewarded_drop_item = dropItemId;
    g_vm_net_mock_battle_rewarded_drop_seq = dropSeq;

    if (dropItemIdOut)
        *dropItemIdOut = dropItemId;
    if (dropSeqOut)
        *dropSeqOut = dropSeq;
    if (dropGrantedOut)
        *dropGrantedOut = dropGranted;
    return rewardExp;
}

static void vm_net_mock_role_apply_battle_settlement(u32 hp, u32 mp,
                                                     u32 rewardExp, u32 rewardGold,
                                                     u32 *lastExpOut, u32 *curExpOut,
                                                     u32 *percentExpOut, u32 *levelOut,
                                                     u32 *goldOut, u32 *hpOut, u32 *mpOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (hp > role->hpMax)
        hp = role->hpMax;
    if (mp > role->mpMax)
        mp = role->mpMax;
    role->hp = hp;
    role->mp = mp;
    role->exp = (0xffffffffu - role->exp < rewardExp) ? 0xffffffffu : role->exp + rewardExp;
    role->money = (0xffffffffu - role->money < rewardGold) ? 0xffffffffu : role->money + rewardGold;
    vm_net_mock_role_normalize(role);
    vm_net_mock_role_db_save("battle-settle");

    if (lastExpOut)
        *lastExpOut = vm_net_mock_role_last_level_exp(role->exp);
    if (curExpOut)
        *curExpOut = vm_net_mock_role_next_level_start_exp(role->exp);
    if (percentExpOut)
        *percentExpOut = vm_net_mock_role_exp_percent(role->exp);
    if (levelOut)
        *levelOut = role->level;
    if (goldOut)
        *goldOut = role->money;
    if (hpOut)
        *hpOut = role->hp;
    if (mpOut)
        *mpOut = role->mp;
}

static u32 vm_net_mock_battle_recover_mp_value(void)
{
    return vm_net_mock_env_u32_if_set("CBE_BATTLE_RECOVER_MP", 0);
}

static bool vm_net_mock_build_battle_settle_iteminfo_blob(u8 *out, u32 outCap,
                                                          u32 *blobLenOut,
                                                          u32 *rowCountOut,
                                                          u32 ownerRoleId,
                                                          u32 itemId)
{
    u32 pos = 0;
    const vm_net_mock_shop_catalog_item *item = NULL;
    const char *itemName = "Item";

    if (blobLenOut)
        *blobLenOut = 0;
    if (rowCountOut)
        *rowCountOut = 0;
    if (out == NULL || itemId == 0 || ownerRoleId == 0)
        return true;

    item = vm_net_mock_find_shop_catalog_item(itemId);
    if (item != NULL && item->name[0] != 0)
        itemName = item->name;

    /*
     * mmBattle HandleBattleSettleMsg(0x743C) reads itemnum separately, then
     * loops over this raw stream. Each row starts with the owner role id; rows
     * for other ids are skipped. For a normal dropped item, rewardType=2 avoids
     * the equipment branch (1) and the money-add branch (3).
     */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, ownerRoleId))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemId))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, itemName))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 2))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 1))
        return false;

    if (blobLenOut)
        *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = 1;
    return true;
}

static u32 vm_net_mock_battle_apply_mp_recovery_once(vm_net_mock_role_state *role,
                                                     u32 roleMp,
                                                     u32 recoverMp,
                                                     bool *appliedOut)
{
    u32 mpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;

    if (appliedOut)
        *appliedOut = false;
    if (role == NULL || recoverMp == 0 || g_mockBattleOperateSessionSerial == 0)
        return roleMp;
    if (g_vm_net_mock_battle_recovered_serial == g_mockBattleOperateSessionSerial)
        return roleMp;

    vm_net_mock_role_sync_derived_vitals(role);
    mpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
    roleMp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(roleMp, recoverMp), mpMax);
    g_mockBattleRoleMpMax = mpMax;
    g_mockBattleRoleMpCurrent = roleMp;
    g_vm_net_mock_battle_recovered_serial = g_mockBattleOperateSessionSerial;
    if (appliedOut)
        *appliedOut = true;
    return roleMp;
}

static u32 vm_net_mock_role_current_hp_for_battle(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 hp = 0;
    u32 hpMax = 0;
    vm_net_mock_role_default_vitals(role, &hp, &hpMax, NULL, NULL);
    (void)hpMax;
    return hp;
}

static void vm_net_mock_battle_save_terminal_role_state(const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHp = g_mockBattleRoleHpMax != 0 ? g_mockBattleRoleHpCurrent :
                 (role ? role->hp : VM_NET_MOCK_ROLE_DEFAULT_HP);
    u32 roleMp = role ? role->mp : VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 rewardExp = 0;
    u32 rewardGold = 0;
    u32 statusLastExp = 0;
    u32 statusCurExp = 0;
    u32 statusPercentExp = 0;
    u32 statusLevel = 0;
    u32 statusGold = 0;
    u32 dropItemId = 0;
    u16 dropSeq = 0;
    bool dropGranted = false;
    bool victory = g_mockBattleEnemyHpCurrent == 0 && roleHp > 0;
    bool rewardAlreadyGranted = false;
    u32 recoverMp = vm_net_mock_battle_recover_mp_value();
    bool mpRecoveryApplied = false;

    if (role == NULL)
        return;
    if (victory)
    {
        rewardAlreadyGranted = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial);
        rewardExp = vm_net_mock_battle_grant_reward_once(&dropItemId,
                                                         &dropSeq,
                                                         &dropGranted);
        if (!rewardAlreadyGranted)
            rewardGold = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_GOLD",
                                                    vm_net_mock_battle_reward_gold_for_enemy(g_vm_net_mock_battle_enemy_id_current));
    }
    roleMp = vm_net_mock_battle_apply_mp_recovery_once(role, roleMp, recoverMp,
                                                       &mpRecoveryApplied);
    vm_net_mock_role_apply_battle_settlement(roleHp, roleMp, rewardExp, rewardGold,
                                             &statusLastExp, &statusCurExp,
                                             &statusPercentExp, &statusLevel,
                                             &statusGold, &roleHp, &roleMp);
    vm_autotest_note("mock_battle_terminal_save reason=%s enemy=%u victory=%u apply_exp=%u gold=%u total_exp=%u level=%u hp=%u mp=%u recover_mp=%u recovered=%u drop=%u seq=%u\n",
                     reason ? reason : "terminal",
                     g_vm_net_mock_battle_enemy_id_current,
                     victory ? 1 : 0,
                     rewardExp,
                     statusGold,
                     role->exp,
                     statusLevel,
                     roleHp,
                     roleMp,
                     recoverMp,
                     mpRecoveryApplied ? 1 : 0,
                     dropGranted ? dropItemId : 0,
                     dropSeq);
}

static void vm_net_mock_save_player_pos_state(const char *scene, u16 x, u16 y, const char *reason)
{
    char runtimeScene[64];
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (x == 0 || y == 0)
        return;

    if (!vm_net_mock_scene_name_is_safe(scene))
    {
        if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
            scene = runtimeScene;
        else if (role != NULL && vm_net_mock_scene_name_is_safe(role->scene))
            scene = role->scene;
        else
            scene = vm_net_mock_default_scene_name();
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    vm_net_mock_role_set_position(scene, x, y, reason);
}

static void vm_net_mock_mark_pending_scene_pos_save(const char *scene, u16 x, u16 y, const char *reason)
{
    if (!vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    snprintf(g_vm_net_mock_pending_scene_save_scene, sizeof(g_vm_net_mock_pending_scene_save_scene),
             "%s", scene);
    snprintf(g_vm_net_mock_pending_scene_save_reason, sizeof(g_vm_net_mock_pending_scene_save_reason),
             "%s", reason ? reason : "pending-scene-load");
    g_vm_net_mock_pending_scene_save_x = x;
    g_vm_net_mock_pending_scene_save_y = y;
    g_vm_net_mock_pending_scene_save_valid = true;
}

static const char *vm_net_mock_current_scene_name(void)
{
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    static char runtimeScene[64];
    if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
        return vm_net_mock_normalize_scene_name_for_enter(runtimeScene);
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role != NULL && vm_net_mock_scene_name_is_safe(role->scene))
        return vm_net_mock_normalize_scene_name_for_enter(role->scene);
    return vm_net_mock_default_scene_name();
}

static u16 vm_net_mock_scene_spawn_x(void)
{
    if (getenv("CBE_SCENE_POS_X") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_X", VM_NET_MOCK_ROLE_INITIAL_X);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role != NULL && role->x != 0)
        return role->x;
    return VM_NET_MOCK_ROLE_INITIAL_X;
}

static u16 vm_net_mock_scene_spawn_y(void)
{
    if (getenv("CBE_SCENE_POS_Y") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_Y", VM_NET_MOCK_ROLE_INITIAL_Y);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role != NULL && role->y != 0)
        return role->y;
    return VM_NET_MOCK_ROLE_INITIAL_Y;
}

static const char *vm_net_mock_default_scene_name(void)
{
    /*
     * Start on a local SCE that actually carries actor_entity NPC records.
     * Runtime evidence showed saved c00/_02 hub scenes load successfully but
     * produce no n_* actor resource opens because their local SCE records have
     * actor_entity_count=0.
     */
    return "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31\x2e\x73\x63\x65"; /* GBK: 00_PengLaiXianDao01.sce */
}

static const char *vm_net_mock_scene_key_name(void)
{
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    /*
     * This key is copied by parse_actorinfo_response() into R9+0x5E46 and later
     * reused as the mode-10 descriptor name by scene_rebuild_runtime_nodes().
     * The ASCII experiment proved the update path, but also bypassed the local
     * .sce scene descriptor and left the map background black when cached.
     * Keep the default aligned with 30/1.scene and use CBE_SCENE_KEY for
     * non-colliding descriptor experiments.
     */
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    if (role != NULL && vm_net_mock_scene_name_is_safe(role->scene))
        return vm_net_mock_normalize_scene_name_for_enter(role->scene);
    return vm_net_mock_current_scene_name();
}

static const char *vm_net_mock_default_scene_title(void)
{
    return "\xc5\xee\xc0\xb3\x2d\xcd\xad\xc8\xb8\xcc\xa8"; /* GBK: PengLai-TongQueTai */
}

static const char *vm_net_mock_fb_target_info_text(void)
{
    const char *overrideInfo = vm_net_mock_env_str("CBE_FB_TARGET_INFO", "");
    if (overrideInfo != NULL && overrideInfo[0] != 0)
        return overrideInfo;
    return vm_net_mock_default_scene_title();
}

static bool vm_net_mock_put_scene_fields_with(u8 *out, u32 outCap, u32 *pos,
                                               bool includeResult, bool includeType, u8 requestType,
                                               const char *sceneName, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    if (posInfoLen == 0)
        return false;
    if (includeResult && !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (includeType && !vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", sceneName ? sceneName : vm_net_mock_default_scene_name()))
        return false;
    return vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen);
}

static bool vm_net_mock_put_scene_ack_without_posinfo(u8 *out, u32 outCap, u32 *pos,
                                                      u8 requestType, const char *sceneName)
{
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    return vm_net_mock_put_object_string(out, outCap, pos, "scene",
                                         sceneName ? sceneName : vm_net_mock_default_scene_name());
}

static bool vm_net_mock_put_scene_fields(u8 *out, u32 outCap, u32 *pos, bool includeResult, bool includeType, u8 requestType)
{
    return vm_net_mock_put_scene_fields_with(out, outCap, pos,
                                             includeResult, includeType, requestType,
                                             vm_net_mock_scene_key_name(),
                                             vm_net_mock_scene_spawn_x(),
                                             vm_net_mock_scene_spawn_y());
}

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

typedef struct
{
    char targetScene[64];
    u16 entryId;
    u16 targetEntryId;
    u16 left;
    u16 top;
    u16 right;
    u16 bottom;
    u16 spawnX;
    u16 spawnY;
} vm_net_mock_sce_edge_portal;

static vm_net_mock_scene_change_target g_vm_net_mock_last_scene_change_target;
static bool g_vm_net_mock_last_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_scene_change_target_serial = 0;
static bool g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
static bool g_vm_net_mock_teleport_stone_direct_enter_pending = false;
static vm_net_mock_scene_change_target g_vm_net_mock_teleport_stone_confirm_target;
static bool g_vm_net_mock_teleport_stone_confirm_target_valid = false;
static bool g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
static u8 g_vm_net_mock_last_scene_change_fb4_type = 1;
static vm_net_mock_scene_change_target g_vm_net_mock_last_completed_scene_change_target;
static bool g_vm_net_mock_last_completed_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_completed_scene_change_tick = 0;
static char g_vm_net_mock_last_current_scene_reload_scene[64];
static bool g_vm_net_mock_last_current_scene_reload_valid = false;
static u32 g_vm_net_mock_last_current_scene_reload_tick = 0;

static bool vm_net_mock_should_rearm_send_ready(void)
{
    return g_vm_net_mock_last_scene_change_target_valid ||
           g_vm_net_mock_last_completed_scene_change_target_valid;
}

static u16 vm_net_mock_read_le16_at(const u8 *data, u32 off)
{
    return (u16)((u16)data[off] | ((u16)data[off + 1] << 8));
}

static u32 vm_net_mock_scene_payload_start(const u8 *data, u32 len)
{
    u32 base = 0;
    u32 mapNameLen = 0;
    u32 start = 0;

    if (data == NULL || len < 15)
        return 0;

    for (base = 0; base + 15 <= len && base < 32; ++base)
    {
        if (memcmp(data + base, "SCE2", 4) == 0)
            break;
    }
    if (base + 15 > len || base >= 32)
        return 0;

    mapNameLen = data[base + 10];
    start = base + 10 + 1 + mapNameLen + 4;
    if (start >= len)
        return 0;
    return start;
}

static bool vm_net_mock_read_sce_len_string(const u8 *data, u32 len, u32 *pos,
                                            char *out, size_t outCap)
{
    u32 stringLen = 0;

    if (data == NULL || pos == NULL || out == NULL || outCap == 0 || *pos >= len)
        return false;
    stringLen = data[*pos];
    if (*pos + 1 + stringLen > len || stringLen >= outCap)
        return false;
    memcpy(out, data + *pos + 1, stringLen);
    out[stringLen] = 0;
    *pos += 1 + stringLen;
    return true;
}

static bool vm_net_mock_read_sce_scalar_field(const u8 *data, u32 len, u32 *pos,
                                              u16 expectedField, u16 *valueOut)
{
    if (data == NULL || pos == NULL || valueOut == NULL || *pos + 6 > len)
        return false;
    if (vm_net_mock_read_le16_at(data, *pos) != 1 ||
        vm_net_mock_read_le16_at(data, *pos + 2) != expectedField)
    {
        return false;
    }
    *valueOut = vm_net_mock_read_le16_at(data, *pos + 4);
    *pos += 6;
    return true;
}

static bool vm_net_mock_parse_sce_edge_portal_at(const u8 *data, u32 len, u32 off,
                                                 vm_net_mock_sce_edge_portal *portal,
                                                 u32 *endOut)
{
    u32 pos = off;
    u16 kind = 0;
    u16 field = 0;

    if (data == NULL || portal == NULL || off + 18 > len)
        return false;
    memset(portal, 0, sizeof(*portal));

    kind = vm_net_mock_read_le16_at(data, pos);
    pos += 2;
    if (kind == 2)
    {
        if (pos + 4 > len)
            return false;
        portal->spawnX = vm_net_mock_read_le16_at(data, pos);
        portal->spawnY = vm_net_mock_read_le16_at(data, pos + 2);
        pos += 4;
    }
    else
    {
        if (pos + 6 > len || vm_net_mock_read_le16_at(data, pos) != 2)
            return false;
        portal->spawnX = vm_net_mock_read_le16_at(data, pos + 2);
        portal->spawnY = vm_net_mock_read_le16_at(data, pos + 4);
        pos += 6;
    }

    if (pos + 8 > len || vm_net_mock_read_le16_at(data, pos) != 8)
        return false;
    pos += 8;

    if (pos + 5 > len || vm_net_mock_read_le16_at(data, pos) != 3)
        return false;
    field = vm_net_mock_read_le16_at(data, pos + 2);
    pos += 4;
    if (field != 6 ||
        !vm_net_mock_read_sce_len_string(data, len, &pos, portal->targetScene, sizeof(portal->targetScene)) ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene))
    {
        return false;
    }

    if (!vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x07, &portal->entryId) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0a, &portal->left) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0b, &portal->top) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0c, &portal->right) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0d, &portal->bottom) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x13, &portal->targetEntryId))
    {
        return false;
    }

    if (portal->right < portal->left || portal->bottom < portal->top)
        return false;
    if (endOut)
        *endOut = pos;
    return true;
}

static u32 vm_net_mock_load_scene_resource(const char *scene, u8 *out, u32 outCap)
{
    char path[256];
    u32 len = 0;

    if (scene == NULL || scene[0] == 0 || out == NULL || outCap == 0 ||
        vm_net_mock_scene_name_has_path_separator(scene))
    {
        return 0;
    }

    snprintf(path, sizeof(path), "JHOnlineData/%s", scene);
    len = vm_net_mock_load_response_file(path, out, outCap);
    if (len == 0 && !vm_net_mock_str_ends_with(scene, ".sce"))
    {
        snprintf(path, sizeof(path), "JHOnlineData/%s.sce", scene);
        len = vm_net_mock_load_response_file(path, out, outCap);
    }
    return len;
}

static u16 vm_net_mock_u16_add_cap(u16 value, u16 amount);

static u32 vm_net_mock_load_scene_json(const char *scene, char *out, u32 outCap)
{
    char sceneName[80];
    const char *prefixes[] = {
        "../tmp/all_sce_bundle",
        "tmp/all_sce_bundle"
    };

    if (scene == NULL || scene[0] == 0 || out == NULL || outCap < 2 ||
        vm_net_mock_scene_name_has_path_separator(scene))
    {
        return 0;
    }

    snprintf(sceneName, sizeof(sceneName), "%s", scene);
    for (u32 withExt = 0; withExt < 2; ++withExt)
    {
        if (withExt == 1)
        {
            if (vm_net_mock_str_ends_with(sceneName, ".sce"))
                break;
            snprintf(sceneName, sizeof(sceneName), "%s.sce", scene);
        }
        for (u32 i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
        {
            char path[256];
            u32 len = 0;
            snprintf(path, sizeof(path), "%s/%s/scene.json", prefixes[i], sceneName);
            len = vm_net_mock_load_response_file(path, (u8 *)out, outCap - 1);
            if (len > 0)
            {
                out[len] = 0;
                return len;
            }
        }
    }

    out[0] = 0;
    return 0;
}

static bool vm_net_mock_scene_edge_data_available(const char *scene)
{
    char jsonProbe[8];
    u8 data[8192];
    u32 len = 0;

    if (!vm_net_mock_scene_name_is_download_key(scene))
        return false;
    if (vm_net_mock_load_scene_json(scene, jsonProbe, sizeof(jsonProbe)) > 0)
        return true;

    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    return vm_net_mock_scene_payload_start(data, len) != 0;
}

static const char *vm_net_mock_json_skip_string(const char *p, const char *end)
{
    if (p == NULL || end == NULL || p >= end || *p != '"')
        return p;
    ++p;
    while (p < end)
    {
        if (*p == '\\')
        {
            p += (p + 1 < end) ? 2 : 1;
            continue;
        }
        if (*p == '"')
            return p + 1;
        ++p;
    }
    return end;
}

static const char *vm_net_mock_json_find_object_end(const char *start, const char *end)
{
    const char *p = start;
    int depth = 0;

    if (start == NULL || end == NULL || start >= end || *start != '{')
        return NULL;
    while (p < end)
    {
        if (*p == '"')
        {
            p = vm_net_mock_json_skip_string(p, end);
            continue;
        }
        if (*p == '{')
            ++depth;
        else if (*p == '}')
        {
            --depth;
            if (depth == 0)
                return p + 1;
        }
        ++p;
    }
    return NULL;
}

static const char *vm_net_mock_json_find_key(const char *start, const char *end,
                                             const char *key)
{
    const char *p = start;
    size_t keyLen = key ? strlen(key) : 0;

    if (start == NULL || end == NULL || key == NULL || keyLen == 0)
        return NULL;
    while (p != NULL && p < end)
    {
        p = strstr(p, key);
        if (p == NULL || p >= end)
            return NULL;
        if (p + keyLen <= end)
            return p;
        ++p;
    }
    return NULL;
}

static bool vm_net_mock_json_read_u16(const char *start, const char *end,
                                      const char *key, u16 *valueOut)
{
    const char *p = vm_net_mock_json_find_key(start, end, key);
    unsigned long value = 0;

    if (p == NULL || valueOut == NULL)
        return false;
    p = strchr(p, ':');
    if (p == NULL || p >= end)
        return false;
    ++p;
    while (p < end && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t'))
        ++p;
    if (p >= end || *p < '0' || *p > '9')
        return false;
    value = strtoul(p, NULL, 10);
    if (value > 0xffff)
        return false;
    *valueOut = (u16)value;
    return true;
}

static bool vm_net_mock_json_read_gbk_string(const char *start, const char *end,
                                             const char *key, char *out, size_t outCap)
{
    const char *p = vm_net_mock_json_find_key(start, end, key);
    char utf8[96];
    size_t len = 0;

    if (p == NULL || out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    p = strchr(p, ':');
    if (p == NULL || p >= end)
        return false;
    ++p;
    while (p < end && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t'))
        ++p;
    if (p >= end || *p != '"')
        return false;
    ++p;
    while (p + len < end && p[len] != '"' && len + 1 < sizeof(utf8))
        ++len;
    if (p + len >= end || p[len] != '"')
        return false;
    memcpy(utf8, p, len);
    utf8[len] = 0;
    utf8_to_gbk((u8 *)utf8, (u8 *)out, outCap);
    out[outCap - 1] = 0;
    return out[0] != 0;
}

static bool vm_net_mock_json_read_object_range(const char *start, const char *end,
                                               const char *key,
                                               const char **objStartOut,
                                               const char **objEndOut)
{
    const char *p = vm_net_mock_json_find_key(start, end, key);
    const char *objStart = NULL;
    const char *objEnd = NULL;

    if (p == NULL || objStartOut == NULL || objEndOut == NULL)
        return false;
    p = strchr(p, ':');
    if (p == NULL || p >= end)
        return false;
    ++p;
    while (p < end && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t'))
        ++p;
    if (p >= end || *p != '{')
        return false;
    objStart = p;
    objEnd = vm_net_mock_json_find_object_end(objStart, end);
    if (objEnd == NULL)
        return false;
    *objStartOut = objStart;
    *objEndOut = objEnd;
    return true;
}

static bool vm_net_mock_parse_json_edge_portal_record(const char *start, const char *end,
                                                      vm_net_mock_sce_edge_portal *portal)
{
    const char *spawnStart = NULL;
    const char *spawnEnd = NULL;
    const char *rectStart = NULL;
    const char *rectEnd = NULL;

    if (start == NULL || end == NULL || portal == NULL ||
        vm_net_mock_json_find_key(start, end, "\"record_type\": \"edge_portal\"") == NULL)
    {
        return false;
    }

    memset(portal, 0, sizeof(*portal));
    if (!vm_net_mock_json_read_object_range(start, end, "\"spawn_point\"", &spawnStart, &spawnEnd) ||
        !vm_net_mock_json_read_u16(spawnStart, spawnEnd, "\"x\"", &portal->spawnX) ||
        !vm_net_mock_json_read_u16(spawnStart, spawnEnd, "\"y\"", &portal->spawnY) ||
        !vm_net_mock_json_read_gbk_string(start, end, "\"target_scene\"",
                                          portal->targetScene, sizeof(portal->targetScene)) ||
        !vm_net_mock_json_read_u16(start, end, "\"entry_id\"", &portal->entryId) ||
        !vm_net_mock_json_read_object_range(start, end, "\"trigger_rect\"", &rectStart, &rectEnd) ||
        !vm_net_mock_json_read_u16(rectStart, rectEnd, "\"left\"", &portal->left) ||
        !vm_net_mock_json_read_u16(rectStart, rectEnd, "\"top\"", &portal->top) ||
        !vm_net_mock_json_read_u16(rectStart, rectEnd, "\"right\"", &portal->right) ||
        !vm_net_mock_json_read_u16(rectStart, rectEnd, "\"bottom\"", &portal->bottom) ||
        !vm_net_mock_json_read_u16(start, end, "\"target_entry_id\"", &portal->targetEntryId))
    {
        return false;
    }

    return portal->right >= portal->left &&
           portal->bottom >= portal->top &&
           vm_net_mock_scene_name_is_safe(portal->targetScene);
}

static bool vm_net_mock_find_json_edge_portal(const char *scene,
                                              bool byEntry,
                                              u32 entryId,
                                              u16 gridX,
                                              u16 gridY,
                                              u16 margin,
                                              vm_net_mock_sce_edge_portal *portalOut)
{
    static char json[65536];
    u32 len = vm_net_mock_load_scene_json(scene, json, sizeof(json));
    const char *cursor = json;
    const char *end = json + len;

    if (portalOut)
        memset(portalOut, 0, sizeof(*portalOut));
    if (len == 0)
        return false;

    while (cursor < end)
    {
        const char *hit = strstr(cursor, "\"record_type\": \"edge_portal\"");
        const char *objStart = hit;
        const char *objEnd = NULL;
        vm_net_mock_sce_edge_portal portal;

        if (hit == NULL || hit >= end)
            break;
        while (objStart > json && *objStart != '{')
            --objStart;
        if (*objStart != '{')
            break;
        objEnd = vm_net_mock_json_find_object_end(objStart, end);
        if (objEnd == NULL)
            break;
        cursor = objEnd;

        if (!vm_net_mock_parse_json_edge_portal_record(objStart, objEnd, &portal))
            continue;
        if (byEntry)
        {
            if (entryId > 0xffff || portal.entryId != (u16)entryId)
                continue;
        }
        else
        {
            u16 left = (portal.left > margin) ? (u16)(portal.left - margin) : 0;
            u16 top = (portal.top > margin) ? (u16)(portal.top - margin) : 0;
            u16 right = vm_net_mock_u16_add_cap(portal.right, margin);
            u16 bottom = vm_net_mock_u16_add_cap(portal.bottom, margin);
            if (gridX < left || gridX > right || gridY < top || gridY > bottom)
                continue;
        }
        if (portalOut)
            *portalOut = portal;
        return true;
    }
    return false;
}

static bool vm_net_mock_find_sce_edge_portal_by_entry(const char *scene, u32 entryId,
                                                      vm_net_mock_sce_edge_portal *portalOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 start = vm_net_mock_scene_payload_start(data, len);

    if (portalOut)
        memset(portalOut, 0, sizeof(*portalOut));
    if (vm_net_mock_find_json_edge_portal(scene, true, entryId, 0, 0, 0, portalOut))
        return true;
    if (len == 0 || start == 0 || entryId > 0xffff)
        return false;

    for (u32 off = start; off + 18 <= len; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;
        if (!vm_net_mock_parse_sce_edge_portal_at(data, len, off, &portal, &end))
            continue;
        if (portal.entryId != (u16)entryId)
            continue;
        if (portalOut)
            *portalOut = portal;
        return true;
    }
    return false;
}

static bool vm_net_mock_find_sce_edge_portal_at_pos(const char *scene, u16 gridX, u16 gridY,
                                                    u16 margin,
                                                    vm_net_mock_sce_edge_portal *portalOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 start = vm_net_mock_scene_payload_start(data, len);

    if (portalOut)
        memset(portalOut, 0, sizeof(*portalOut));
    if (vm_net_mock_find_json_edge_portal(scene, false, 0, gridX, gridY, margin, portalOut))
        return true;
    if (len == 0 || start == 0)
        return false;

    for (u32 off = start; off + 18 <= len; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;
        u16 left = 0;
        u16 top = 0;
        u16 right = 0;
        u16 bottom = 0;
        if (!vm_net_mock_parse_sce_edge_portal_at(data, len, off, &portal, &end))
            continue;
        left = (portal.left > margin) ? (u16)(portal.left - margin) : 0;
        top = (portal.top > margin) ? (u16)(portal.top - margin) : 0;
        right = (u16)(portal.right + margin);
        bottom = (u16)(portal.bottom + margin);
        if (gridX < left || gridX > right || gridY < top || gridY > bottom)
            continue;
        if (portalOut)
            *portalOut = portal;
        return true;
    }
    return false;
}

static u16 vm_net_mock_u16_sub_floor(u16 value, u16 amount)
{
    return value > amount ? (u16)(value - amount) : 0;
}

static u16 vm_net_mock_u16_add_cap(u16 value, u16 amount)
{
    return value > (u16)(0xffff - amount) ? 0xffff : (u16)(value + amount);
}

static void vm_net_mock_adjust_pos_away_from_sce_portal(const vm_net_mock_sce_edge_portal *portal,
                                                        u16 safeGap, u16 *x, u16 *y)
{
    u16 px = 0;
    u16 py = 0;
    bool xInside = false;
    bool yInside = false;
    u32 dx = 0;
    u32 dy = 0;

    if (portal == NULL || x == NULL || y == NULL || safeGap == 0)
        return;

    px = *x;
    py = *y;
    xInside = px >= portal->left && px <= portal->right;
    yInside = py >= portal->top && py <= portal->bottom;
    dx = xInside ? 0 : (px < portal->left ? (u32)(portal->left - px) : (u32)(px - portal->right));
    dy = yInside ? 0 : (py < portal->top ? (u32)(portal->top - py) : (u32)(py - portal->bottom));

    if (!(xInside && yInside) && dx * dx + dy * dy >= (u32)safeGap * (u32)safeGap)
        return;

    if (xInside && py < portal->top)
    {
        *y = vm_net_mock_u16_sub_floor(portal->top, safeGap);
        return;
    }
    if (xInside && py > portal->bottom)
    {
        *y = vm_net_mock_u16_add_cap(portal->bottom, safeGap);
        return;
    }
    if (yInside && px < portal->left)
    {
        *x = vm_net_mock_u16_sub_floor(portal->left, safeGap);
        return;
    }
    if (yInside && px > portal->right)
    {
        *x = vm_net_mock_u16_add_cap(portal->right, safeGap);
        return;
    }

    if (!xInside || !yInside)
    {
        if (dy > 0 && (dx == 0 || dy <= dx))
            *y = py < portal->top ? vm_net_mock_u16_sub_floor(portal->top, safeGap)
                                  : vm_net_mock_u16_add_cap(portal->bottom, safeGap);
        else if (dx > 0)
            *x = px < portal->left ? vm_net_mock_u16_sub_floor(portal->left, safeGap)
                                   : vm_net_mock_u16_add_cap(portal->right, safeGap);
        return;
    }

    {
        u32 best = 0xffffffffu;
        char dir = 0;
        u32 leftDist = (u32)(px - portal->left);
        u32 rightDist = (u32)(portal->right - px);
        u32 topDist = (u32)(py - portal->top);
        u32 bottomDist = (u32)(portal->bottom - py);

        if (portal->left > 0 && leftDist < best)
        {
            best = leftDist;
            dir = 'l';
        }
        if (portal->top > 0 && topDist < best)
        {
            best = topDist;
            dir = 't';
        }
        if (rightDist < best)
        {
            best = rightDist;
            dir = 'r';
        }
        if (bottomDist < best)
        {
            dir = 'b';
        }

        if (dir == 'l')
            *x = vm_net_mock_u16_sub_floor(portal->left, safeGap);
        else if (dir == 't')
            *y = vm_net_mock_u16_sub_floor(portal->top, safeGap);
        else if (dir == 'r')
            *x = vm_net_mock_u16_add_cap(portal->right, safeGap);
        else if (dir == 'b')
            *y = vm_net_mock_u16_add_cap(portal->bottom, safeGap);
    }
}

static bool vm_net_mock_adjust_safe_player_pos_from_sce(const char *scene, u16 *x, u16 *y)
{
    vm_net_mock_sce_edge_portal portal;
    u16 oldX = 0;
    u16 oldY = 0;

    if (scene == NULL || x == NULL || y == NULL)
        return false;

    oldX = *x;
    oldY = *y;
    if (!vm_net_mock_find_sce_edge_portal_at_pos(scene, oldX, oldY,
                                                VM_NET_MOCK_SCENE_LANDING_SAFE_GAP,
                                                &portal))
    {
        return false;
    }

    vm_net_mock_adjust_pos_away_from_sce_portal(&portal,
                                                VM_NET_MOCK_SCENE_LANDING_SAFE_GAP,
                                                x, y);
    if (oldX != *x || oldY != *y)
    {
        vm_autotest_note("mock_scene_safe_landing scene=%s raw=(%u,%u) safe=(%u,%u) rect=(%u,%u)-(%u,%u) entry=%u targetEntry=%u\n",
                         scene, oldX, oldY, *x, *y,
                         portal.left, portal.top, portal.right, portal.bottom,
                         portal.entryId, portal.targetEntryId);
    }
    return oldX != *x || oldY != *y;
}

static bool vm_net_mock_get_scene_entry_spawn_from_sce(const char *scene, u32 entryId,
                                                       u16 *xOut, u16 *yOut)
{
    vm_net_mock_sce_edge_portal portal;
    u16 x = 0;
    u16 y = 0;

    if (!vm_net_mock_find_sce_edge_portal_by_entry(scene, entryId, &portal))
        return false;
    x = portal.spawnX;
    y = portal.spawnY;
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    if (xOut)
        *xOut = x;
    if (yOut)
        *yOut = y;
    return portal.spawnX != 0 || portal.spawnY != 0;
}

static bool vm_net_mock_get_scene_portal_target_from_sce(const char *sourceScene,
                                                         u16 gridX, u16 gridY, u16 margin,
                                                         vm_net_mock_scene_change_target *target)
{
    vm_net_mock_sce_edge_portal portal;
    const char *normalizedTarget = NULL;
    u16 targetX = 0;
    u16 targetY = 0;

    if (target == NULL ||
        !vm_net_mock_find_sce_edge_portal_at_pos(sourceScene, gridX, gridY, margin, &portal))
    {
        return false;
    }

    if (!vm_net_mock_get_scene_entry_spawn_from_sce(portal.targetScene, portal.entryId,
                                                    &targetX, &targetY) &&
        !vm_net_mock_get_scene_entry_spawn_from_sce(portal.targetScene, portal.targetEntryId,
                                                    &targetX, &targetY))
    {
        return false;
    }

    memset(target, 0, sizeof(*target));
    normalizedTarget = vm_net_mock_normalize_scene_name_for_enter(portal.targetScene);
    snprintf(target->scene, sizeof(target->scene), "%s", normalizedTarget);
    target->x = targetX;
    target->y = targetY;
    target->exitId = portal.entryId;
    target->mapType = 2;
    return true;
}

static void vm_net_mock_remember_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_scene_change_target = *target;
    g_vm_net_mock_last_scene_change_target_valid = true;
    ++g_vm_net_mock_last_scene_change_target_serial;
    if (g_vm_net_mock_last_scene_change_target_serial == 0)
        g_vm_net_mock_last_scene_change_target_serial = 1;
    printf("[info][network] mock_scene_target_remember serial=%u scene=%s pos=(%u,%u) exit=%u\n",
           g_vm_net_mock_last_scene_change_target_serial,
           target->scene, target->x, target->y, target->exitId);
}

static bool vm_net_mock_scene_change_targets_equal(const vm_net_mock_scene_change_target *a,
                                                   const vm_net_mock_scene_change_target *b)
{
    return a != NULL && b != NULL &&
           a->x == b->x &&
           a->y == b->y &&
           a->exitId == b->exitId &&
           vm_net_mock_scene_names_equal_loose(a->scene, b->scene);
}

static void vm_net_mock_mark_completed_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_completed_scene_change_target = *target;
    g_vm_net_mock_last_completed_scene_change_target_valid = true;
    g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;
}

static bool vm_net_mock_is_recent_completed_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (!g_vm_net_mock_last_completed_scene_change_target_valid ||
        !vm_net_mock_scene_change_targets_equal(target, &g_vm_net_mock_last_completed_scene_change_target))
    {
        return false;
    }
    return (g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick) < 90;
}

static bool vm_net_mock_is_recent_completed_scene_name(const char *scene, u32 windowTicks)
{
    if (scene == NULL ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        !vm_net_mock_scene_names_equal_loose(scene, g_vm_net_mock_last_completed_scene_change_target.scene))
    {
        return false;
    }
    return (g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick) < windowTicks;
}

static bool vm_net_mock_is_recent_current_scene_reload(const char *scene, u32 windowTicks)
{
    if (scene == NULL ||
        !g_vm_net_mock_last_current_scene_reload_valid ||
        !vm_net_mock_scene_names_equal_loose(scene, g_vm_net_mock_last_current_scene_reload_scene))
    {
        return false;
    }
    return (g_schedulerTick - g_vm_net_mock_last_current_scene_reload_tick) < windowTicks;
}

static void vm_net_mock_mark_current_scene_reload(const char *scene)
{
    if (scene == NULL || scene[0] == 0)
        return;
    snprintf(g_vm_net_mock_last_current_scene_reload_scene,
             sizeof(g_vm_net_mock_last_current_scene_reload_scene),
             "%s", scene);
    g_vm_net_mock_last_current_scene_reload_valid = true;
    g_vm_net_mock_last_current_scene_reload_tick = g_schedulerTick;
}

static bool vm_net_mock_scene_runtime_pending_without_target(void)
{
    u32 sceneObj = 0;
    u8 pending = 0;

    if (!Global_R9 || g_vm_net_mock_last_scene_change_target_valid)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, sizeof(sceneObj)) != UC_ERR_OK || sceneObj == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5C6B, &pending, sizeof(pending)) != UC_ERR_OK)
        return false;
    return pending != 0;
}

static bool vm_net_mock_should_use_full_scene_bootstrap(const char *currentScene,
                                                        const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return false;
    if (target->needsSceneDownload)
        return false;

    if (vm_net_mock_scene_is_penglai02(target->scene))
        return true;

    if (currentScene != NULL &&
        target->exitId == 0 &&
        vm_net_mock_scene_is_penglai_transfer_scene(currentScene) &&
        vm_net_mock_scene_is_penglai_transfer_scene(target->scene) &&
        !vm_net_mock_scene_names_equal_loose(currentScene, target->scene))
    {
        return true;
    }

    /*
     * Local SCE exports show c00蓬莱仙岛_03 uses an east edge portal into
     * 01桃花岛_01, not the older bottom-to-04 route kept in legacy notes.
     * Runtime on 2026-06-24 reproduced:
     *   2/10 len=19 -> 2/3 len=87 -> assert at scene_runtime_tick(0x01014EE0)
     * when this path is answered by the generic ack-only scene-change packet.
     * Feed the same full bootstrap family used by the other live portal enters.
     */
    if (currentScene != NULL &&
        vm_net_mock_scene_is_c00_penglai03(currentScene) &&
        vm_net_mock_scene_is_taohuadao01(target->scene))
    {
        return true;
    }

    return false;
}

static bool vm_net_mock_scene_uses_current_scene_completion(const char *scene)
{
    return vm_net_mock_scene_is_penglai03(scene) ||
           vm_net_mock_scene_is_penglai04(scene) ||
           vm_net_mock_scene_is_taohuadao01(scene);
}

static void vm_net_mock_get_scene_change_target(const u8 *request, u32 requestLen,
                                                vm_net_mock_scene_change_target *target)
{
    char mapId[64];
    u32 exitId = 0;
    u16 sceSpawnX = 0;
    u16 sceSpawnY = 0;
    const char *currentScene = vm_net_mock_current_scene_name();
    memset(target, 0, sizeof(*target));
    snprintf(target->scene, sizeof(target->scene), "%s", vm_net_mock_default_scene_name());
    target->x = vm_net_mock_scene_spawn_x();
    target->y = vm_net_mock_scene_spawn_y();
    target->exitId = 0;
    target->mapType = 2;
    target->hasSceEntry = false;
    target->needsSceneDownload = false;

    if (!vm_net_mock_get_object_string_field(request, requestLen, "mapID", mapId, sizeof(mapId)))
        return;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId);
    (void)vm_net_mock_get_object_u8_field(request, requestLen, "maptype", &target->mapType);
    target->exitId = exitId;

    if (mapId[0] != 0)
        snprintf(target->scene, sizeof(target->scene), "%s", mapId);

    if (vm_net_mock_get_scene_entry_spawn_from_sce(mapId, exitId, &sceSpawnX, &sceSpawnY))
    {
        snprintf(target->scene, sizeof(target->scene), "%s",
                 vm_net_mock_normalize_scene_name_for_enter(mapId));
        target->x = sceSpawnX;
        target->y = sceSpawnY;
        target->hasSceEntry = true;
        return;
    }

    if (vm_net_mock_scene_name_is_download_key(mapId))
    {
        snprintf(target->scene, sizeof(target->scene), "%s", mapId);
        target->x = 0;
        target->y = 0;
        target->needsSceneDownload = true;
        vm_autotest_note("mock_scene_missing_sce target=%s exit=%u action=download-ack\n",
                         target->scene, exitId);
        return;
    }

    if (vm_net_mock_scene_is_penglai01(mapId) && exitId == 0)
    {
        snprintf(target->scene, sizeof(target->scene),
                 "%s", "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65");
        target->x = 396;
        target->y = 473;
    }
    else if (strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0)
    {
        if (exitId == 1)
        {
            target->x = 128;
            target->y = 45;
        }
        else
        {
            target->x = 396;
            target->y = 473;
        }
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31") == 0)
    {
        target->x = 223;
        target->y = 382;
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
    {
        if (currentScene != NULL && vm_net_mock_scene_is_penglai02(currentScene))
        {
            target->x = 145;
            target->y = 47;
        }
        else if (currentScene != NULL && vm_net_mock_scene_is_penglai04(currentScene))
        {
            target->x = 105;
            target->y = 395;
        }
        else if (currentScene != NULL &&
                 vm_net_mock_scene_names_equal_loose(currentScene, target->scene))
        {
            target->x = vm_net_mock_scene_spawn_x();
            target->y = vm_net_mock_scene_spawn_y();
        }
        else
        {
            target->x = 105;
            target->y = (exitId == 1) ? 58 : 395;
        }
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 225 : 230;
        target->y = (exitId == 1) ? 116 : 425;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 305 : 80;
        target->y = (exitId == 1) ? 310 : 60;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 40 : 200;
        target->y = (exitId == 1) ? 70 : 540;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 323 : 42;
        target->y = (exitId == 1) ? 200 : 60;
    }
    else if (strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 256 : 136;
        target->y = (exitId == 1) ? 300 : 58;
    }

    vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
}

static bool vm_net_mock_is_teleport_stone_list_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 0x10 &&
           object.subtype == 1 &&
           object.payloadLen == 0;
}

static bool vm_net_mock_is_teleport_stone_transfer_request(const u8 *request, u32 requestLen, u8 *subtypeOut)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    if (kind != 0x10 || (subtype != 2 && subtype != 3))
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "exitID") &&
        !vm_net_mock_request_contains(request, requestLen, "type"))
    {
        return false;
    }
    if (subtypeOut)
        *subtypeOut = subtype;
    return true;
}

static void vm_net_mock_get_teleport_stone_target(const u8 *request, u32 requestLen,
                                                  vm_net_mock_scene_change_target *target)
{
    u32 exitId = VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID;
    const char *sceneOverride = vm_net_mock_env_str("CBE_TELEPORT_STONE_SCENE", "");
    const char *targetScene = vm_net_mock_default_scene_name();

    memset(target, 0, sizeof(*target));
    if (sceneOverride != NULL && sceneOverride[0] != 0 && vm_net_mock_scene_name_is_safe(sceneOverride))
        targetScene = sceneOverride;

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitid", &exitId);

    snprintf(target->scene, sizeof(target->scene), "%s", vm_net_mock_normalize_scene_name_for_enter(targetScene));
    target->x = (u16)vm_net_mock_env_u32("CBE_TELEPORT_STONE_X", 223);
    target->y = (u16)vm_net_mock_env_u32("CBE_TELEPORT_STONE_Y", 382);
    target->exitId = exitId;
    target->mapType = 2;
    vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
}

static bool vm_net_mock_is_teleport_stone_map_transfer_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 0x10 &&
           subtype == 4 &&
           vm_net_mock_request_contains(request, requestLen, "curid") &&
           vm_net_mock_request_contains(request, requestLen, "objid");
}

static bool vm_net_mock_teleport_stone_scene_matches_objid(const char *scene, u32 objId)
{
    char idText[3];

    if (scene == NULL || objId > 99 || scene[0] == 0 || scene[0] == 'b')
        return false;
    snprintf(idText, sizeof(idText), "%02u", objId);
    return (scene[0] == idText[0] && scene[1] == idText[1]) ||
           (scene[0] == 'c' && scene[1] == idText[0] && scene[2] == idText[1]);
}

static bool vm_net_mock_teleport_stone_scene_has_index(const char *scene, u32 index)
{
    char suffix[16];

    if (scene == NULL || index == 0 || index > 99)
        return false;
    snprintf(suffix, sizeof(suffix), "_%02u.sce", index);
    return vm_net_mock_str_ends_with(scene, suffix);
}

static u32 vm_net_mock_teleport_stone_scene_score(const char *scene, u32 objId, u32 curId)
{
    u32 sceneIndex = (curId > 0 && curId <= 99) ? curId : 1;
    u32 score = 0;

    if (!vm_net_mock_teleport_stone_scene_matches_objid(scene, objId) ||
        !vm_net_mock_str_ends_with(scene, ".sce"))
    {
        return 0;
    }

    if (vm_net_mock_teleport_stone_scene_has_index(scene, sceneIndex))
        score += 1000;
    else if (vm_net_mock_teleport_stone_scene_has_index(scene, 1))
        score += 500;
    else if (sceneIndex == 1 && vm_net_mock_str_ends_with(scene, "01.sce"))
        score += 400;
    else
        score += 100;

    score += (scene[0] == 'c') ? 10 : 20;
    return score;
}

static bool vm_net_mock_find_teleport_stone_scene_by_objid(u32 objId, u32 curId,
                                                           char *out, size_t outCap)
{
#ifdef _WIN32
    struct _finddata_t data;
    intptr_t handle;
    u32 bestScore = 0;

    if (out == NULL || outCap == 0 || objId > 99)
        return false;
    out[0] = 0;

    handle = _findfirst("JHOnlineData\\*.sce", &data);
    if (handle == (intptr_t)-1)
        return false;

    do
    {
        const char *name = data.name;
        u32 score = 0;
        if ((data.attrib & _A_SUBDIR) != 0 || name == NULL || strlen(name) >= outCap)
            continue;
        score = vm_net_mock_teleport_stone_scene_score(name, objId, curId);
        if (score == 0)
            continue;
        if (score > bestScore || (score == bestScore && out[0] != 0 && strcmp(name, out) < 0))
        {
            snprintf(out, outCap, "%s", name);
            bestScore = score;
        }
    } while (_findnext(handle, &data) == 0);

    _findclose(handle);
    return out[0] != 0 && vm_net_mock_scene_name_is_safe(out);
#else
    (void)objId;
    (void)curId;
    if (out != NULL && outCap > 0)
        out[0] = 0;
    return false;
#endif
}

static void vm_net_mock_get_teleport_stone_map_target(const u8 *request, u32 requestLen,
                                                      vm_net_mock_scene_change_target *target,
                                                      u32 *curIdOut,
                                                      u32 *objIdOut,
                                                      const char **sourceOut)
{
    u32 curId = 0;
    u32 objId = 0;
    char mappedScene[64];
    const char *sceneOverride = vm_net_mock_env_str("CBE_TELEPORT_STONE_SCENE", "");
    const char *targetScene = NULL;
    const char *source = "default";

    memset(target, 0, sizeof(*target));
    mappedScene[0] = 0;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "curid", &curId);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "objid", &objId);

    if (sceneOverride != NULL && sceneOverride[0] != 0 && vm_net_mock_scene_name_is_safe(sceneOverride))
    {
        targetScene = sceneOverride;
        source = "env";
    }
    else if (vm_net_mock_find_teleport_stone_scene_by_objid(objId, curId, mappedScene, sizeof(mappedScene)))
    {
        targetScene = mappedScene;
        source = "resource-scan";
    }
    else
    {
        targetScene = vm_net_mock_default_scene_name();
    }

    snprintf(target->scene, sizeof(target->scene), "%s", vm_net_mock_normalize_scene_name_for_enter(targetScene));
    target->x = (u16)vm_net_mock_env_u32("CBE_TELEPORT_STONE_X", 120);
    target->y = (u16)vm_net_mock_env_u32("CBE_TELEPORT_STONE_Y", 120);
    target->exitId = 1;
    target->mapType = 2;
    vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);

    if (curIdOut)
        *curIdOut = curId;
    if (objIdOut)
        *objIdOut = objId;
    if (sourceOut)
        *sourceOut = source;
}

static bool vm_net_mock_build_teleport_stone_exitinfo_blob(u8 *out, u32 outCap, u32 *blobLenOut)
{
    u32 pos = 0;
    const char *label = vm_net_mock_env_str("CBE_TELEPORT_STONE_LABEL", "Penglai Home");
    u32 exitId = vm_net_mock_env_u32("CBE_TELEPORT_STONE_EXIT_ID", VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID);

    if (out == NULL || blobLenOut == NULL)
        return false;
    if (exitId > 0xffff)
        exitId = VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID;

    /*
     * mmGameMstarWqvga.cbm:sub_11CE parses 16/1 exitinfo as:
     *   u8 count, repeated u32 exitId, len16 string label, u32 reserved.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, exitId))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, label))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 0))
        return false;

    *blobLenOut = pos;
    return true;
}

static u32 vm_net_mock_build_teleport_stone_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 exitInfo[128];
    u32 exitInfoLen = 0;

    if (outCap < pos)
        return 0;
    memset(exitInfo, 0, sizeof(exitInfo));
    if (!vm_net_mock_build_teleport_stone_exitinfo_blob(exitInfo, sizeof(exitInfo), &exitInfoLen))
        return 0;
    if (exitInfoLen == 0 || exitInfoLen > 0xffff)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x10, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "exitinfo", exitInfo, (u16)exitInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_autotest_note("mock_teleport_stone_list entries=1 exitinfo_len=%u evidence=mmGame:0x11CE\n",
                     exitInfoLen);
    return pos;
}

static bool vm_net_mock_put_teleport_stone_scene_fields_with_result(u8 *out, u32 outCap, u32 *pos,
                                                                    u8 resultValue,
                                                                    const vm_net_mock_scene_change_target *target)
{
    u8 posInfo[8];
    u32 posInfoLen = 0;

    if (target == NULL)
        return false;
    posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), target->x, target->y);
    if (posInfoLen == 0)
        return false;
    /*
     * mmGame:0x11CE reads result through JianghuOL:0x01033C6C, which returns
     * value[2]. The field must therefore use the typed-u8 object encoding
     * 00 01 xx; raw-u32 makes result read back as 0 and stalls 16/3 loading.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", resultValue))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", target->scene))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen))
        return false;
    return vm_net_mock_put_object_u16(out, outCap, pos, "exitid", (u16)target->exitId);
}

static bool vm_net_mock_put_teleport_stone_scene_fields(u8 *out, u32 outCap, u32 *pos,
                                                        u8 subtype,
                                                        const vm_net_mock_scene_change_target *target)
{
    return vm_net_mock_put_teleport_stone_scene_fields_with_result(out, outCap, pos,
                                                                   subtype == 3 ? 2 : 1,
                                                                   target);
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly,
                                                               bool preferSceneNpcOther);
static bool vm_net_mock_append_scene_enter_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneName, u16 spawnX, u16 spawnY);

static bool vm_net_mock_append_mmgame_scene_transfer_object(u8 *out, u32 outCap, u32 *pos,
                                                            u8 subtype,
                                                            const vm_net_mock_scene_change_target *target)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x10, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_teleport_stone_scene_fields(out, outCap, pos, subtype, target))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_mmgame_scene_transfer_object_with_result(u8 *out, u32 outCap, u32 *pos,
                                                                        u8 subtype, u8 result,
                                                                        const vm_net_mock_scene_change_target *target)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x10, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_teleport_stone_scene_fields_with_result(out, outCap, pos, result, target))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_mmgame_scene_transfer_empty_object(u8 *out, u32 outCap, u32 *pos,
                                                                  u8 subtype)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x10, subtype, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_settings_unstuck_ack_object(u8 *out, u32 outCap, u32 *pos,
                                                           u32 id)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0c, 3, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "id", (u16)id))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_mmgame_scene_transfer_start_response(const vm_net_mock_scene_change_target *target,
                                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (target == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_append_mmgame_scene_transfer_object(out, outCap, &pos, 3, target))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_mmgame_scene_transfer_start scene=%s pos=(%u,%u) resp=%u\n",
           target->scene, target->x, target->y, pos);
    vm_autotest_note("mock_mmgame_scene_transfer_start scene=%s pos=(%u,%u) response=16/3 evidence=mmGame:0x11CE,0x0BCC\n",
                     target->scene, target->x, target->y);
    return pos;
}

static u32 vm_net_mock_build_scene_channel_enter_combo_for_target(const vm_net_mock_scene_change_target *target,
                                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;

    if (target == NULL || target->scene[0] == 0 || outCap < pos)
        return 0;
    if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                         target->scene, target->x, target->y))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_net_mock_is_settings_unstuck_request(const u8 *request, u32 requestLen, u32 *idOut)
{
    u32 offset = 4;
    u32 id = 0;
    vm_net_mock_request_object object;
    bool matched = false;

    if (idOut)
        *idOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if ((object.major == 0 || object.major == 1) &&
            object.kind == 0x0c &&
            object.subtype == 3 &&
            (vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &id) ||
             vm_net_mock_request_contains(object.payload, object.payloadLen, "id")))
        {
            matched = true;
            break;
        }
    }
    if (!matched)
        return false;
    if (idOut)
        *idOut = id;
    return true;
}

static void vm_net_mock_get_current_scene_unstuck_target(vm_net_mock_scene_change_target *target)
{
    const char *scene = vm_net_mock_current_scene_name();

    memset(target, 0, sizeof(*target));
    if (!vm_net_mock_scene_name_is_safe(scene))
        scene = vm_net_mock_default_scene_name();
    snprintf(target->scene, sizeof(target->scene), "%s", scene);
    target->x = vm_net_mock_scene_spawn_x();
    target->y = vm_net_mock_scene_spawn_y();
    (void)vm_net_mock_read_current_player_grid(NULL, NULL, &target->x, &target->y, NULL, NULL);
    vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
    target->exitId = 0;
    target->mapType = 2;
    target->hasSceEntry = true;
    target->needsSceneDownload = false;
}

static u32 vm_net_mock_build_settings_unstuck_response(const u8 *request, u32 requestLen,
                                                       u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 id = 0;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_settings_unstuck_request(request, requestLen, &id))
        return 0;

    vm_net_mock_get_current_scene_unstuck_target(&target);
    if (!vm_net_mock_append_settings_unstuck_ack_object(out, outCap, &pos, id))
        return 0;
    if (!vm_net_mock_append_mmgame_scene_transfer_object(out, outCap, &pos, 3, &target))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 2);

    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "settings-unstuck-target");
    printf("[info][network] mock_settings_unstuck id=%u scene=%s pos=(%u,%u) response=12/3+16/3 resp=%u\n",
           id, target.scene, target.x, target.y, pos);
    vm_autotest_note("mock_settings_unstuck id=%u scene=%s pos=(%u,%u) response=12/3+16/3 evidence=mmGame:0x5BCA,0x6512,0x11CE,0x0BCC\n",
                     id, target.scene, target.x, target.y);
    return pos;
}

static u32 vm_net_mock_build_teleport_stone_transfer_response(const u8 *request, u32 requestLen,
                                                              u8 subtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    vm_net_mock_scene_change_target target;
    bool targetAlreadyPending = false;
    bool shouldRearmTarget = false;
    bool targetFromConfirm = false;
    const char *responseKind = "16/3-scene";

    if (outCap < pos || (subtype != 2 && subtype != 3))
        return 0;

    if (subtype == 2)
    {
        vm_net_mock_get_teleport_stone_target(request, requestLen, &target);
        if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(out, outCap, &pos,
                                                                         2, 2, &target))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 1);
        g_vm_net_mock_teleport_stone_confirm_target = target;
        g_vm_net_mock_teleport_stone_confirm_target_valid = true;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
        g_vm_net_mock_teleport_stone_direct_enter_pending = false;
        responseKind = "16/2-confirm-target";
        printf("[info][network] mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=0 confirm=1 resp=%u\n",
               subtype, target.exitId, target.scene, target.x, target.y,
               responseKind, pos);
        vm_autotest_note("mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=0 confirm=1 evidence=mmGame:0x11CE/0x24A8\n",
                         subtype, target.exitId, target.scene, target.x, target.y,
                         responseKind);
        return pos;
    }

    if (g_vm_net_mock_teleport_stone_confirm_target_valid)
    {
        target = g_vm_net_mock_teleport_stone_confirm_target;
        g_vm_net_mock_teleport_stone_confirm_target_valid = false;
        targetFromConfirm = true;
    }
    else if (g_vm_net_mock_last_scene_change_target_valid)
    {
        target = g_vm_net_mock_last_scene_change_target;
        targetAlreadyPending = true;
    }
    else
    {
        vm_net_mock_get_teleport_stone_target(request, requestLen, &target);
    }

    if (targetAlreadyPending)
    {
        if (g_vm_net_mock_teleport_stone_subtype3_ack_sent)
        {
            if (!vm_net_mock_append_mmgame_scene_transfer_empty_object(out, outCap, &pos, subtype))
                return 0;
            vm_net_mock_finish_wt_packet(out, pos, 1);
            responseKind = "16/3-duplicate-noop";
        }
        else
        {
            pos = vm_net_mock_build_scene_channel_enter_combo_for_target(&target, out, outCap);
            if (pos == 0)
                return 0;
            g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
            g_vm_net_mock_teleport_stone_direct_enter_pending = true;
            shouldRearmTarget = true;
            responseKind = "scene-channel-enter-saved-target";
        }
    }
    else
    {
        pos = vm_net_mock_build_scene_channel_enter_combo_for_target(&target, out, outCap);
        if (pos == 0)
            return 0;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
        g_vm_net_mock_teleport_stone_direct_enter_pending = true;
        responseKind = targetFromConfirm ? "scene-channel-enter-confirm-target" : "scene-channel-enter";
    }

    if (!targetAlreadyPending || shouldRearmTarget)
        vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    if (!targetAlreadyPending)
        vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "teleport-stone-target");
    printf("[info][network] mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=%u confirm=%u resp=%u\n",
           subtype, target.exitId, target.scene, target.x, target.y,
           responseKind,
           targetAlreadyPending ? 1u : 0u,
           targetFromConfirm ? 1u : 0u,
           pos);
    vm_autotest_note("mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=%u confirm=%u evidence=JianghuOL:0x01012E4D/0x01039B8A/0x010396D6\n",
                     subtype, target.exitId, target.scene, target.x, target.y,
                     responseKind,
                     targetAlreadyPending ? 1u : 0u,
                     targetFromConfirm ? 1u : 0u);
    return pos;
}

static u32 vm_net_mock_build_teleport_stone_map_transfer_response(const u8 *request, u32 requestLen,
                                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 curId = 0;
    u32 objId = 0;
    const char *source = NULL;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_teleport_stone_map_transfer_request(request, requestLen))
        return 0;

    vm_net_mock_get_teleport_stone_map_target(request, requestLen, &target, &curId, &objId, &source);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x10, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_teleport_stone_scene_fields(out, outCap, &pos, 2, &target))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "teleport-stone-map-target");
    vm_autotest_note("mock_teleport_stone_map_transfer curid=%u objid=%u scene=%s pos=(%u,%u) source=%s evidence=xxjh:0x103573A response=16/2\n",
                     curId, objId, target.scene, target.x, target.y, source ? source : "-");
    return pos;
}

static bool vm_net_mock_is_teleport_stone_post_enter_combo_request(const u8 *request, u32 requestLen)
{
    if (!g_vm_net_mock_last_scene_change_target_valid ||
        request == NULL || requestLen < 19 ||
        request[0] != 'W' || request[1] != 'T')
    {
        return false;
    }
    return vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
}

static u32 vm_net_mock_build_teleport_stone_post_enter_combo_response(const u8 *request, u32 requestLen,
                                                                      u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_teleport_stone_post_enter_combo_request(request, requestLen))
        return 0;

    target = g_vm_net_mock_last_scene_change_target;
    if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos,
                                                         target.scene, target.x, target.y))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     g_vm_net_mock_last_scene_change_fb4_type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "teleport-stone-post-enter");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    printf("[info][network] mock_teleport_stone_post_enter scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           target.scene, target.x, target.y, objectCount, pos);
    vm_autotest_note("mock_teleport_stone_post_enter scene=%s pos=(%u,%u) objects=%u evidence=runtime:contains-27/11+12/1+7/42\n",
                     target.scene, target.x, target.y, objectCount);
    return pos;
}

static bool vm_net_mock_append_group_info_object(u8 *out, u32 outCap, u32 *pos, u32 leadId)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 templateId = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_ID", 105);
    u32 templateHp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_HP", 20);
    u32 templateMaxHp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MAX_HP", templateHp);
    u32 templateMp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MP", 20);
    u32 templateMaxMp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MAX_MP", templateMp);
    u8 templateByte0 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE0", 1);
    u8 templateByte1 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE1", 0);
    u8 templateByte2 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE2", 0);
    const char *templateName = vm_net_mock_env_str("CBE_GROUPINFO_TEMPLATE_NAME", "Monster");
    bool seedTemplate = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_SEED", 0) != 0 && templateId != 0;
    u8 num = 0;
    u32 objectStart = 0;

    if (seedTemplate)
    {
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateId))
            return false;
        if (!vm_net_mock_seq_put_string(groupInfo, sizeof(groupInfo), &groupInfoLen, templateName))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte0))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte1))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte2))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateHp))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxHp))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMp))
            return false;
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxMp))
            return false;
        num = 1;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "num", num))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "leadid", leadId))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_template_prefill_object_ex(u8 *out, u32 outCap,
                                                                 u32 *pos, u32 templateId,
                                                                 const char *templateName,
                                                                 u8 rowByte34, u8 rowByte35,
                                                                 u32 templateHp,
                                                                 u32 templateMaxHp,
                                                                 u32 templateMp,
                                                                 u32 templateMaxMp)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 objectStart = 0;

    if (templateId == 0)
        return false;
    if (templateName == NULL)
        templateName = "";

    if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateId))
        return false;
    if (!vm_net_mock_seq_put_string(groupInfo, sizeof(groupInfo), &groupInfoLen, templateName))
        return false;
    if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, rowByte34))
        return false;
    if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, rowByte35))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateHp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxHp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxMp))
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_enemy_template_prefill_object(u8 *out, u32 outCap,
                                                                    u32 *pos, u32 templateId)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(templateId);
    u32 templateHp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_HP", stats.hp);
    u32 templateMaxHp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MAX_HP", templateHp);
    u32 templateMp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MP", stats.mp);
    u32 templateMaxMp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MAX_MP", templateMp);
    u8 rowByte34 = vm_net_mock_env_u8("CBE_BATTLE_PREFILL_TEMPLATE_BYTE34", 1);
    u8 rowByte35 = vm_net_mock_env_u8("CBE_BATTLE_PREFILL_TEMPLATE_BYTE35", 0);
    const char *templateName = vm_net_mock_env_str("CBE_BATTLE_PREFILL_TEMPLATE_NAME", "Monster");

    return vm_net_mock_append_battle_template_prefill_object_ex(out, outCap, pos,
                                                                templateId,
                                                                templateName,
                                                                rowByte34,
                                                                rowByte35,
                                                                templateHp,
                                                                templateMaxHp,
                                                                templateMp,
                                                                templateMaxMp);
}

static bool vm_net_mock_append_type1_object(u8 *out, u32 outCap, u32 *pos, u8 npcNum)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x1a, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", npcNum))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name",
                                       vm_net_mock_role_spouse_name(role)))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "money", money))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_is_role_action23_request(const u8 *request, u32 requestLen, u32 *idOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 id = 0;

    if (idOut)
        *idOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x0a && object.subtype == 23)
        {
            (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &id);
            if (idOut)
                *idOut = id;
            return true;
        }
    }
    return false;
}

static u32 vm_net_mock_build_role_action23_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 id = 0;

    if (outCap < pos || !vm_net_mock_is_role_action23_request(request, requestLen, &id))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x0a, 23, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "id", id))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_role_action23 id=%u resp=%u\n", id, pos);
    vm_autotest_note("mock_role_action23 id=%u response=10/23 result=1 evidence=xxjh:0x103C830 field=id\n",
                     id);
    return pos;
}

static bool vm_net_mock_is_scene_ctrl_page_request(const u8 *request, u32 requestLen, u32 *pageOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 page = 0;

    if (pageOut)
        *pageOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 2 && object.subtype == 7)
        {
            (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "pgIdx", &page);
            if (pageOut)
                *pageOut = page;
            return true;
        }
    }
    return false;
}

static u32 vm_net_mock_build_scene_ctrl_page_response(const u8 *request, u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 page = 0;

    if (outCap < pos || !vm_net_mock_is_scene_ctrl_page_request(request, requestLen, &page))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 7, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "pgIdx", page))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_scene_ctrl_page pgIdx=%u resp=%u\n", page, pos);
    vm_autotest_note("mock_scene_ctrl_page pgIdx=%u response=2/7 result=1 evidence=xxjh:0x103014C\n",
                     page);
    return pos;
}

static bool vm_net_mock_append_misc_player_sync8_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    static const u8 seqOne[] = {0x00, 0x02, 0x00, 0x01};
    /*
     * Focused protocol experiment for the bundled post-login 1/7/7 type=1
     * request. Static analysis shows top-level 7 -> local subtype 8 with
     * type=4/result=1 is one of the few branches that can seed the empty
     * type=15 HUD source head and immediately trigger rebuild(2)->rebuild(1).
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 8, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 4))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "seq", seqOne, sizeof(seqOne)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_misc_player_type_object(u8 *out, u32 outCap, u32 *pos, u8 subtype)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (subtype == 20)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
    }
    else if (subtype == 32)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
            return false;
    }
    else
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneName, u16 spawnX, u16 spawnY)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, pos, false, false, 0,
                                           sceneName, spawnX, spawnY))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_pos_result_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                                 const char *sceneName, u16 spawnX, u16 spawnY)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 2, &objectStart))
        return false;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, pos, true, true, 2,
                                           sceneName, spawnX, spawnY))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_scene_enter_object_for_scene(out, outCap, pos,
                                                          vm_net_mock_scene_key_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_scene_is_penglai01(const char *scene);
static bool vm_net_mock_scene_is_penglai02(const char *scene);
static bool vm_net_mock_scene_is_penglai03(const char *scene);
static bool vm_net_mock_scene_is_penglai04(const char *scene);
static bool vm_net_mock_scene_is_penglai_transfer_scene(const char *scene);
static bool vm_net_mock_scene_supports_actor_other_npc_seed(const char *scene);

static u8 vm_net_mock_scene_room_npc_seed_count(const char *scene)
{
    (void)scene;
    return 0;
}

typedef struct
{
    u32 actorId;
    u32 x;
    u32 y;
    u32 finalActorId;
    const char *actorResource;
    const char *displayName;
    const char *scriptName;
} vm_net_mock_scene_npcinfo_seed;

static bool vm_net_mock_select_scene_npcinfo_seeds(const char *scene,
                                                   const vm_net_mock_scene_npcinfo_seed **seedsOut,
                                                   size_t *seedCountOut)
{
    if (seedsOut)
        *seedsOut = NULL;
    if (seedCountOut)
        *seedCountOut = 0;
    (void)scene;
    return false;
}

static bool vm_net_mock_build_scene_npcinfo_blob(const char *scene,
                                                 u8 *npcInfo, u32 npcInfoCap,
                                                 u8 *npcNumOut, u32 *npcInfoLenOut)
{
    const vm_net_mock_scene_npcinfo_seed *npcSeeds = NULL;
    size_t npcSeedCount = 0;
    u8 npcNum = 0;
    u32 npcInfoLen = 0;

    if (npcNumOut)
        *npcNumOut = 0;
    if (npcInfoLenOut)
        *npcInfoLenOut = 0;
    if (npcInfo == NULL || npcInfoCap == 0)
        return false;

    (void)vm_net_mock_select_scene_npcinfo_seeds(scene, &npcSeeds, &npcSeedCount);
    if (npcSeeds != NULL && npcSeedCount > 0)
    {
        size_t i;
        for (i = 0; i < npcSeedCount; ++i)
        {
            const vm_net_mock_scene_npcinfo_seed *seed = &npcSeeds[i];
            /*
             * scene_parse_npcinfo_and_spawn_npcs(0x01037998) consumes:
             *   u32 rowId, u32 x, u32 y,
             *   str displayName, str actorResource, str extraText,
             *   str registeredName, u32 finalActorId.
             *
             * Runtime evidence: 27/11 is the object that reaches this parser.
             * IDA evidence:
             * - string #1 is copied to node+0x44,
             * - string #2 is passed to the scene visual/resource resolver with
             *   output at node+0x24,
             * - string #4 is passed to RegisterDisplayName(),
             * - the final integer is stored back to node+0x64.
             */
            if (!vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->actorId))
                return false;
            if (!vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->x))
                return false;
            if (!vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->y))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->displayName))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->actorResource))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->scriptName))
                return false;
            if (!vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->displayName))
                return false;
            if (!vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->finalActorId))
                return false;
            npcNum += 1;
        }
    }

    if (npcNumOut)
        *npcNumOut = npcNum;
    if (npcInfoLenOut)
        *npcInfoLenOut = npcInfoLen;
    return true;
}

static bool vm_net_mock_append_scene_room_npc_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 roomList[128];
    u8 npcInfo[256];
    u8 colNames[64];
    u32 roomListLen = 0;
    u32 npcInfoLen = 0;
    u32 colNamesLen = 0;
    u8 npcNum = 0;
    static const char *const roomColumns[] = {"id", "room", "scene", "state"};
    const char *scene = vm_net_mock_current_scene_name();
    bool seedSceneNpcInfo = false;
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roomColumns, 4))
        return false;
    if (!vm_net_mock_seq_put_u32(roomList, sizeof(roomList), &roomListLen, 1001))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "1001"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Penglai"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Scene"))
        return false;
    if (!vm_net_mock_build_scene_npcinfo_blob(scene, npcInfo, sizeof(npcInfo), &npcNum, &npcInfoLen))
        return false;
    seedSceneNpcInfo = npcNum > 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 3, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "curpage", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "pagenum", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "roomnum", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "roomlist", roomList, (u16)roomListLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", npcNum))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "npcinfo", npcInfo, (u16)npcInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_room_roles_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 rolesInfo[128];
    u8 colNames[64];
    char levelText[16];
    u32 rolesInfoLen = 0;
    u32 colNamesLen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();
    const char *jobName = "Warrior";
    u32 level = role ? role->level : 1;
    static const char *const roleColumns[] = {"name", "job", "level", "state"};
    if (role && role->job == 2)
        jobName = "Assassin";
    else if (role && role->job == 3)
        jobName = "Mage";
    snprintf(levelText, sizeof(levelText), "Lv%u", level);
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roleColumns, 4))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, roleName))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, jobName))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, levelText))
        return false;
    if (!vm_net_mock_seq_put_string(rolesInfo, sizeof(rolesInfo), &rolesInfoLen, "Ready"))
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 7, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roomid", 1001))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "rolenum", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "rolesinfo", rolesInfo, (u16)rolesInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly,
                                                               bool preferSceneNpcOther);

static u32 vm_net_mock_build_group_type1_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen < 9 || outCap < 5)
        return 0;

    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 leadId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    bool hasGroup10 = false;
    bool hasType1 = false;
    bool enableMiscSync8 = vm_net_mock_env_u8("CBE_GROUP_TYPE1_MISC_SYNC8", 0) != 0;
    const char *scene = vm_net_mock_current_scene_name();
    /*
     * Runtime evidence now shows the first visible scene enter happens in the
     * later WT 12/1 follow-up, not in this earlier 5/10 group/type packet.
     * Keep 30/3 room+npc data out of 5/10 by default; otherwise NPC seeds are
     * delivered before 30/1 and never materialize on screen.
     *
     * Evidence:
     * - runtime: group/type1 packet arrives before the WT 49 scene/resource
     *   follow-up request and before the later 12/1 response.
     * - runtime: NPC list is absent on map even though mock_scene_room_npc_object
     *   is emitted here.
     * - IDA: scene enter parser is kind 30/subtype 1 (0x010396D6), while NPC
     *   list parser is kind 30/subtype 3 (0x01039222); ordering them as 30/3
     *   before 30/1 is weaker than the later scene-followup path.
     */
    bool includeRoomNpc = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_NPC", 0) != 0;
    bool includeRoomRoles = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_ROLES", 0) != 0;
    bool includeFbTargetClear = false;
    u8 sceneNpcNum = includeRoomNpc ? vm_net_mock_scene_room_npc_seed_count(scene) : 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.kind == 5 && object.subtype == 10)
        {
            hasGroup10 = true;
            vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &leadId);
        }
        else if (object.kind == 7 && object.subtype == 7)
        {
            u8 requestType = 0;
            if (vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &requestType) && requestType == 1)
                hasType1 = true;
        }
    }

    if (!hasGroup10)
        return 0;
    u32 pos = 5;
    u8 objectCount = 0;
    if (!vm_net_mock_append_group_info_object(out, outCap, &pos, leadId))
        return 0;
    objectCount += 1;
    if (hasType1 && !vm_net_mock_append_type1_object(out, outCap, &pos, sceneNpcNum))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_backpack_role_grid_main_objects(out, outCap, &pos, &objectCount))
        return 0;
    if (hasType1 && enableMiscSync8 && !vm_net_mock_append_misc_player_sync8_object(out, outCap, &pos))
        return 0;
    if (hasType1 && enableMiscSync8)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 20))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 32))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && includeRoomNpc && !vm_net_mock_append_scene_room_npc_object(out, outCap, &pos))
        return 0;
    if (hasType1 && includeRoomNpc)
        objectCount += 1;
    if (hasType1 && includeRoomRoles && !vm_net_mock_append_scene_room_roles_object(out, outCap, &pos))
        return 0;
    if (hasType1 && includeRoomRoles)
        objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_scene_change_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_scene_change_target target;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    return pos;
}

static bool vm_net_mock_append_login_tail_skill_objects(u8 *out, u32 outCap, u32 *pos, u8 *addedCount)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u8 learnedSkill[512];
    u32 learnedSkillLen = 0;
    u8 learnedCount = 0;
    char learnedPreview[192];

    if (addedCount == NULL)
        return false;
    *addedCount = 0;
    memset(learnedSkill, 0, sizeof(learnedSkill));
    learnedPreview[0] = 0;
    learnedSkillLen = vm_net_mock_build_role_learned_skill_blob(role,
                                                                learnedSkill,
                                                                sizeof(learnedSkill),
                                                                &learnedCount,
                                                                learnedPreview,
                                                                sizeof(learnedPreview));

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0c, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "learnednum", learnedCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "learnedskill",
                                    learnedSkillLen ? learnedSkill : NULL,
                                    (u16)learnedSkillLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    *addedCount = (u8)(*addedCount + 1);
    vm_autotest_note("mock_role_skills role=%u job=%u level=%u learned=%u ids=%s evidence=JianghuOL.CBE:0x1010594 skill.dsh\n",
                     role ? role->roleId : 0,
                     role ? role->job : 0,
                     role ? role->level : 1,
                     learnedCount,
                     learnedPreview);

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    *addedCount = (u8)(*addedCount + 1);

    if (!vm_net_mock_append_backpack_items_object(out, outCap, pos))
        return false;
    *addedCount = (u8)(*addedCount + 1);
    return true;
}

static bool vm_net_mock_append_books42_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_empty11_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 11, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_scene_npcs11_object(u8 *out, u32 outCap, u32 *pos,
                                                             const char *scene,
                                                             u8 *npcNumOut,
                                                             u32 *npcInfoLenOut)
{
    u32 objectStart = 0;
    u8 npcInfo[256];
    u32 npcInfoLen = 0;
    u8 npcNum = 0;

    if (npcNumOut)
        *npcNumOut = 0;
    if (npcInfoLenOut)
        *npcInfoLenOut = 0;
    if (!vm_net_mock_build_scene_npcinfo_blob(scene, npcInfo, sizeof(npcInfo), &npcNum, &npcInfoLen))
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 11, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", npcNum))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "npcinfo", npcInfo, (u16)npcInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (npcNumOut)
        *npcNumOut = npcNum;
    if (npcInfoLenOut)
        *npcInfoLenOut = npcInfoLen;
    return true;
}

static bool vm_net_mock_append_fb_target_result12_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneKey, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    u32 objectStart = 0;
    if (posInfoLen == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name", sceneKey))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_result12_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_fb_target_result12_for_scene(out, outCap, pos,
                                                          vm_net_mock_scene_key_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_append_fb_target_result4_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 typeValue, const char *infoText)
{
    u32 objectStart = 0;
    if (infoText == NULL)
        infoText = "";
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "min", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", typeValue))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "info", infoText))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u8 vm_net_mock_get_request_fb4_type(const u8 *request, u32 requestLen, u8 fallbackType)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 fb4Type = fallbackType;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 4)
        {
            (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &fb4Type);
            break;
        }
    }

    return fb4Type;
}

static bool vm_net_mock_is_penglai02_repeat_scene_change_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_scene_change_target target;

    if (!vm_net_mock_is_scene_change_request(request, requestLen))
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42) ||
        vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1))
    {
        return false;
    }

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.needsSceneDownload)
        return false;
    return vm_net_mock_scene_is_penglai02(target.scene) &&
           target.exitId == 0 &&
           vm_net_mock_is_recent_completed_scene_change_target(&target);
}

static bool vm_net_mock_is_current_scene_completion_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_scene_change_target target;
    const char *currentScene = NULL;

    if (!vm_net_mock_is_scene_change_request(request, requestLen))
        return false;
    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42))
    {
        return false;
    }

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.scene[0] == 0 || target.needsSceneDownload)
        return false;

    currentScene = vm_net_mock_current_scene_name();
    return currentScene != NULL &&
           vm_net_mock_scene_uses_current_scene_completion(currentScene) &&
           vm_net_mock_scene_names_equal_loose(currentScene, target.scene) &&
           !vm_net_mock_is_recent_completed_scene_change_target(&target);
}

static bool vm_net_mock_is_current_scene_repeat_scene_change_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_scene_change_target target;
    const char *currentScene = NULL;

    if (!vm_net_mock_is_scene_change_request(request, requestLen))
        return false;
    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42))
    {
        return false;
    }

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.scene[0] == 0 || target.needsSceneDownload)
        return false;

    currentScene = vm_net_mock_current_scene_name();
    return currentScene != NULL &&
           vm_net_mock_scene_uses_current_scene_completion(currentScene) &&
           vm_net_mock_scene_names_equal_loose(currentScene, target.scene) &&
           vm_net_mock_is_recent_completed_scene_change_target(&target);
}

static u32 vm_net_mock_build_penglai02_repeat_scene_change_response(const u8 *request, u32 requestLen,
                                                                    u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u8 fb4Type = 1;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_penglai02_repeat_scene_change_request(request, requestLen))
        return 0;

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);

    /*
     * Runtime evidence:
     * - the first _01 -> _02 portal request is WT 2/3 len=74 and needs the
     *   full 30/2(posinfo)+30/1 bootstrap so EnterSceneByMapName() can render;
     * - after visible render, the client re-emits a narrower WT 2/3 contract
     *   for the same _02 exit=0 target together with 27/11, 27/4, and 7/42.
     *
     * Treat that second packet as a completion ack only. Re-sending 30/1 or a
     * 30/2 with posinfo restarts the same-scene enter loop and eventually drops
     * back to loading.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     fb4Type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-penglai02-repeat-ack");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    return pos;
}

static u32 vm_net_mock_build_current_scene_completion_response(const u8 *request, u32 requestLen,
                                                               u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;
    u16 currentX = 0;
    u16 currentY = 0;
    u8 fb4Type = 1;
    bool hasFb4 = false;

    if (outCap < pos || !vm_net_mock_is_current_scene_completion_request(request, requestLen))
        return 0;

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    hasFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, g_vm_net_mock_last_scene_change_fb4_type);
    if (!hasFb4 && vm_net_mock_read_current_player_grid(NULL, NULL, &currentX, &currentY, NULL, NULL))
    {
        target.x = currentX;
        target.y = currentY;
    }

    /*
     * Direct startup into saved `_03` already has the local scene shell on
     * screen before this same-target WT 2/3 completion arrives. The client
     * still wants the 27/12 + 27/11 + 27/4 + 7/42 completion family, but a
     * second 30/2 scene+posinfo commit re-enters the same scene again and
     * shows the repeated loading bar on first visible arrival.
     *
     * Keep this path as a completion ack only. The earlier scene shell remains
     * authoritative for the active screen; later task-subset follow-ups can
     * read the completed target from g_vm_net_mock_last_completed_scene_change_target.
     */
    if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos,
                                                         target.scene, target.x, target.y))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     fb4Type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-current-scene-ack");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    return pos;
}

static u32 vm_net_mock_build_current_scene_repeat_scene_change_response(const u8 *request, u32 requestLen,
                                                                        u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u8 fb4Type = 1;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_current_scene_repeat_scene_change_request(request, requestLen))
        return 0;

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     fb4Type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-current-scene-repeat-ack");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    return pos;
}

static u32 vm_net_mock_build_scene_change_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSkill = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool splitScenePosCommit = true;
    bool currentSceneAlreadyTarget = false;
    u8 fb4Type = 1;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;
    bool recentCompletedTarget = false;
    const char *currentScene = NULL;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    recentCompletedTarget = vm_net_mock_is_recent_completed_scene_change_target(&target);
    currentScene = vm_net_mock_current_scene_name();
    currentSceneAlreadyTarget = !target.needsSceneDownload &&
                                currentScene != NULL &&
                                vm_net_mock_scene_uses_current_scene_completion(currentScene) &&
                                vm_net_mock_scene_names_equal_loose(currentScene, target.scene);
    if (vm_net_mock_should_use_full_scene_bootstrap(currentScene, &target))
    {
        /*
         * Some live portal transitions already entered the client's pending
         * scene-switch path before this request. The 30/2 scene-position
         * result calls EnterSceneByMapName(), which asks the platform to
         * re-enter the destination scene screen; keep the 30/1 scene-enter
         * object in the same dispatch window so the follow-up fresh init has
         * scene data to consume.
         */
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               true, true, true, true, false, false, false))
            return 0;
        if (!vm_net_mock_append_scene_pos_result_object_for_scene(out, outCap, &pos,
                                                                  target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
        /*
         * Runtime `_02 -> _03` already re-enters through the scene-pos result
         * path. Appending an immediate second 30/1 scene-enter object produces
         * one extra same-class screen init and shows the repeated loading bar
         * the user still observes on first arrival.
         */
        if (!(currentScene != NULL &&
              vm_net_mock_scene_is_penglai02(currentScene) &&
              vm_net_mock_scene_is_penglai03(target.scene)))
        {
            if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                                target.scene, target.x, target.y))
                return 0;
            objectCount += 1;
        }
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        /*
         * The full-bootstrap path already includes the 30/2 scene+posinfo
         * object. IDA scene_handle_enter_with_scene_pos(0x010396D6) feeds that
         * object into EnterSceneByMapName(0x01018150), so leaving the same target
         * pending makes the next 25/5 subset repeat the scene enter and shows an
         * extra loading cycle.
         */
        vm_net_mock_mark_completed_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        g_vm_net_mock_last_scene_change_fb4_type = 1;
        vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-transfer-target");
        return pos;
    }
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);
    if (splitScenePosCommit)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    else
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    if (needFb11)
    {
        if (!splitScenePosCommit)
        {
            if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
                return 0;
            objectCount += 1;
        }
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (needFb4)
    {
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
    }
    if (needSkill)
    {
        u8 added = 0;
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos, &added))
            return 0;
        objectCount = (u8)(objectCount + added);
    }
    else if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (!splitScenePosCommit)
    {
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (target.needsSceneDownload)
    {
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
        vm_autotest_note("mock_scene_download_ack scene=%s exit=%u response=scene-ack-without-posinfo evidence=JianghuOL:0x100369C/0x1036C66\n",
                         target.scene, target.exitId);
        return pos;
    }
    if (currentSceneAlreadyTarget)
    {
        /*
         * Some edge portals locally create the destination scene screen before
         * the skill/books-flavoured WT 2/3 request arrives. This request cannot
         * hit the lighter current-scene completion detector because it also asks
         * for 12/1 and 7/42, but keeping it pending makes the following 25/5
         * send another 30/2 scene+posinfo object and re-enter the map.
         */
        vm_net_mock_mark_completed_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_target_valid = false;
    }
    else if (!recentCompletedTarget)
    {
        vm_net_mock_remember_scene_change_target(&target);
    }
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    if (!recentCompletedTarget || currentSceneAlreadyTarget)
        vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-target");
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    return pos;
}

static u32 vm_net_mock_build_type27_followup_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSceneDefault = vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    u8 fb4Type = 1;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 objectStart = 0;

    if (outCap < pos || !needFb4)
        return 0;

    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);

    if (needSceneDefault)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 12, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    if (needFb11)
    {
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, ""))
        return 0;
    objectCount += 1;

    if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_scene_default_event_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    if (outCap < pos)
        return 0;
    if (g_vm_net_mock_last_scene_change_target_valid)
    {
        vm_net_mock_scene_change_target target = g_vm_net_mock_last_scene_change_target;
        if (vm_net_mock_scene_is_penglai_transfer_scene(target.scene))
        {
            if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                                   true, true, true, true, false, false, false))
                return 0;
            if (!vm_net_mock_append_scene_pos_result_object_for_scene(out, outCap, &pos,
                                                                      target.scene, target.x, target.y))
                return 0;
            objectCount += 1;
            vm_net_mock_finish_wt_packet(out, pos, objectCount);
            vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-transfer-completion");
            vm_net_mock_mark_completed_scene_change_target(&target);
            g_vm_net_mock_last_scene_change_target_valid = false;
            g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
            return pos;
        }
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               true, true, true, true, false, false, false))
            return 0;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                            target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_mock_mark_pending_scene_pos_save(target.scene, target.x, target.y, "scene-change-completion");
        vm_net_mock_mark_completed_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        return pos;
    }
    if (g_mockBattleOperateSessionFinished != 0)
    {
        g_mockBattleOperateSessionFinished = 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 5, &objectStart))
        return 0;
    /*
     * scene_runtime_tick() sends the empty 0x19/5 scene-default event request.
     * Repository evidence already showed that a same-subtype `1/25/5 {result=4}`
     * shell is parser-safe on the scene resource-followup path, while the older
     * generic `1/25/12 {result=4}` clear path is consumed globally by
     * net_handle_info_banner_state() and now also correlates with battle exit.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap, u32 *pos);

static bool vm_net_mock_is_scene_change_post_enter_followup_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 30)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x19 || object.subtype != 5 || object.payloadLen != 0)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 3)
        return false;
    if (!vm_net_mock_request_contains(object.payload, object.payloadLen, "maptype") ||
        !vm_net_mock_request_contains(object.payload, object.payloadLen, "mapID") ||
        !vm_net_mock_request_contains(object.payload, object.payloadLen, "exitID"))
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x1b || object.subtype != 11)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_scene_change_post_enter_followup_response(const u8 *request, u32 requestLen,
                                                                       u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_scene_change_post_enter_followup_request(request, requestLen))
        return 0;

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.scene[0] == 0)
        return 0;

    if (target.needsSceneDownload)
    {
        u32 objectStart = 0;

        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        vm_autotest_note("mock_scene_download_followup_ack scene=%s exit=%u response=scene-ack-without-posinfo\n",
                         target.scene, target.exitId);
        return pos;
    }

    if (!g_vm_net_mock_last_scene_change_target_valid ||
        !vm_net_mock_scene_change_targets_equal(&target, &g_vm_net_mock_last_scene_change_target))
    {
        vm_net_mock_remember_scene_change_target(&target);
    }

    /*
     * Evidence:
     * - runtime: once EnterSceneByMapName() gets a real same-screen lifecycle,
     *   Penglai _02 emits `25/5 + 2/3 + 27/11 + 7/42` instead of stalling;
     * - IDA/runtime from earlier scene probes: 27/11 is the empty-NPC-safe path
     *   consumed by scene_parse_npcinfo_and_spawn_npcs(), and deferred scene
     *   completion already wants 27/12 + 27/11 + 27/4 together with the final
     *   30/2 scene-pos result.
     *
     * Keep the map empty of NPCs, but close the deferred scene change with the
     * same packet family the client already understands.
     */
    if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos,
                                                         target.scene, target.x, target.y))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     g_vm_net_mock_last_scene_change_fb4_type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_scene_pos_result_object_for_scene(out, outCap, &pos,
                                                              target.scene, target.x, target.y))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-post-enter-complete");
    vm_net_mock_mark_completed_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    printf("[info][network] mock_scene_change_post_enter_followup scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           target.scene, target.x, target.y, objectCount, pos);
    return pos;
}

static bool vm_net_mock_is_actor_other_scene_default_combo_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 typeValue = 0;

    if (request == NULL || requestLen != 24)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue) || typeValue != 1)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x19 || object.subtype != 5 || object.payloadLen != 0)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_short_scene_default_event_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x19 || object.subtype != 5 || object.payloadLen != 0)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_current_scene_reload_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    const char *scene = NULL;
    u16 x = 0;
    u16 y = 0;
    bool isReloadTrigger = false;

    if (outCap < pos)
        return 0;
    isReloadTrigger = vm_net_mock_is_short_scene_default_event_request(request, requestLen) ||
                      vm_net_mock_is_actor_other_scene_default_combo_request(request, requestLen);
    if (!isReloadTrigger || !vm_net_mock_scene_runtime_pending_without_target())
        return 0;

    scene = vm_net_mock_current_scene_name();
    if (scene == NULL ||
        scene[0] == 0 ||
        vm_net_mock_is_recent_current_scene_reload(scene, 90) ||
        vm_net_mock_is_recent_completed_scene_name(scene, 90))
    {
        return 0;
    }
    x = vm_net_mock_scene_spawn_x();
    y = vm_net_mock_scene_spawn_y();
    (void)vm_net_mock_read_current_player_grid(NULL, NULL, &x, &y, NULL, NULL);
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);

    /*
     * mmGame menu "脱离卡死" re-enters the current scene without a portal
     * target. In that state the client emits the default scene event family,
     * but the old empty 25/5 ack leaves the new scene screen on loading.
     * Reuse the first-scene resource family and finish with 30/1 so
     * scene_handle_enter_with_scene_pos() can load the current map normally.
     */
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           true, true, true, true,
                                                           false, false, false))
        return 0;
    if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos, scene, x, y))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_mark_current_scene_reload(scene);
    vm_net_mock_save_player_pos_state(scene, x, y, "scene-current-reload");
    printf("[info][network] mock_scene_current_reload scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           scene, x, y, objectCount, pos);
    vm_autotest_note("mock_scene_current_reload scene=%s pos=(%u,%u) trigger=%s objects=%u evidence=mmGame:0x24E6 JianghuOL:0x10396D6\n",
                     scene,
                     x,
                     y,
                     vm_net_mock_is_actor_other_scene_default_combo_request(request, requestLen) ? "2/10+25/5" : "25/5",
                     objectCount);
    return pos;
}

static bool vm_net_mock_is_mmgame_scene_transfer_followup_request(const u8 *request, u32 requestLen)
{
    if (!g_vm_net_mock_last_scene_change_target_valid)
        return false;
    if (g_vm_net_mock_teleport_stone_direct_enter_pending)
        return false;
    return vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
}

static u32 vm_net_mock_build_mmgame_scene_transfer_followup_response(const u8 *request, u32 requestLen,
                                                                     u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_mmgame_scene_transfer_followup_request(request, requestLen))
        return 0;
    target = g_vm_net_mock_last_scene_change_target;

    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           true, true, true, true,
                                                           false, false, false))
        return 0;
    if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                         target.scene, target.x, target.y))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "mmgame-scene-transfer-followup");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    printf("[info][network] mock_mmgame_scene_transfer_followup scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           target.scene, target.x, target.y, objectCount, pos);
    vm_autotest_note("mock_mmgame_scene_transfer_followup scene=%s pos=(%u,%u) objects=%u response=resources+30/1 evidence=JianghuOL:0x10396D6 runtime=post-mmGame-16/3\n",
                     target.scene, target.x, target.y, objectCount);
    return pos;
}

static bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen,
                                                   u8 kind, u8 subtype);

static u32 vm_net_mock_build_teleport_stone_direct_enter_default_ack_response(const u8 *request,
                                                                              u32 requestLen,
                                                                              u8 *out,
                                                                              u32 outCap)
{
    u32 len = 0;
    vm_net_mock_scene_change_target target;

    if (!g_vm_net_mock_teleport_stone_direct_enter_pending ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5))
    {
        return 0;
    }

    target = g_vm_net_mock_last_scene_change_target;
    len = vm_net_mock_build_scene_default_event_response(out, outCap);
    if (len == 0)
        return 0;

    printf("[info][network] mock_teleport_stone_direct_enter_ack scene=%s pos=(%u,%u) keep_pending=1 resp=%u\n",
           target.scene, target.x, target.y, len);
    vm_autotest_note("mock_teleport_stone_direct_enter_ack scene=%s pos=(%u,%u) keep_pending=1 response=25/5 evidence=runtime:post-30/1-short-default\n",
                     target.scene, target.x, target.y);
    return len;
}

static bool vm_net_mock_is_scene_resource_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    bool needSkillBooks = false;
    bool needTaskFamily = false;
    if (request == NULL || (requestLen != 49 && requestLen != 39))
        return false;
    needSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (needSkillBooks &&
        (!vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
         !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42)))
        return false;
    needTaskFamily = vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    if (!needTaskFamily)
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_is_current_scene_task_subset_followup_request(const u8 *request, u32 requestLen)
{
    const char *currentScene = NULL;

    if (requestLen != 39 || !vm_net_mock_is_scene_resource_followup_request(request, requestLen))
        return false;
    currentScene = vm_net_mock_current_scene_name();
    if (currentScene == NULL || !vm_net_mock_scene_uses_current_scene_completion(currentScene))
        return false;
    if (g_vm_net_mock_last_completed_scene_change_target_valid &&
        vm_net_mock_scene_names_equal_loose(currentScene, g_vm_net_mock_last_completed_scene_change_target.scene))
    {
        return true;
    }
    return g_vm_net_mock_last_scene_change_target_valid &&
           vm_net_mock_scene_names_equal_loose(currentScene, g_vm_net_mock_last_scene_change_target.scene);
}

static bool vm_net_mock_is_scene_task_subset_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    if (request == NULL || requestLen < 24)
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5))
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_append_taskinfo_empty1_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "taskinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_tasktypes_empty13_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u32 blobPos = 0;
    u8 blob[64];

    memset(blob, 0, sizeof(blob));
    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u8(blob, sizeof(blob), &blobPos, 0))
            return false;
        if (!vm_net_mock_seq_put_string(blob, sizeof(blob), &blobPos, ""))
            return false;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 13, &objectStart))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "tasktypes", blob, (u16)blobPos))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_taskaction_empty14_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "action", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "tasknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "taskinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_actor_other_empty10_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "othernum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "otherinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

typedef struct
{
    u32 actorId;
    u16 x;
    u16 y;
    u8 kind;
    const char *actorResource;
    u8 sceneType;
    const char *displayName;
    u8 scriptType;
    const char *scriptName;
} vm_net_mock_scene_npc_seed;

static bool vm_net_mock_scene_is_penglai02(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_penglai01(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0 ||
            strcmp(scene, "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31") == 0 ||
            strcmp(scene, "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31\x2e\x73\x63\x65") == 0 ||
            strcmp(scene, "c00PenglaiXiandao_01") == 0 ||
            strcmp(scene, "c00PenglaiXiandao_01.sce") == 0);
}

static bool vm_net_mock_scene_is_penglai03(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_penglai04(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_penglai_transfer_scene(const char *scene)
{
    return vm_net_mock_scene_is_penglai01(scene) ||
           vm_net_mock_scene_is_penglai02(scene) ||
           vm_net_mock_scene_is_penglai03(scene) ||
           vm_net_mock_scene_is_penglai04(scene);
}

static bool vm_net_mock_scene_supports_actor_other_npc_seed(const char *scene)
{
    (void)scene;
    return false;
}

static bool vm_net_mock_seq_put_actor_other_scene_npc(u8 *out, u32 outCap, u32 *pos,
                                                      const vm_net_mock_scene_npc_seed *seed)
{
    if (seed == NULL)
        return false;

    /*
     * ParseSceneOtherNodeData(0x01012958) reads a tagged stream per row:
     *   u32 actorId,
     *   u8 visualVariant, u8 visualGroup,
     *   str labelText,
     *   u8 targetPosX, u8 targetPosY,
     *   str shortLabel,
     *   str longLabel,
     *   i16 gridPosX, i16 gridPosY.
     *
     * Evidence:
     * - main:stream_reader_init_from_blob_0x01033B16 installs readers:
     *   +0x20=i32, +0x28=i8, +0x1C=len16 string, +0x24=i16,
     *   +0x2C=peek next len16.
     * - main:ParseSceneOtherNodeData_0x01012958 calls those readers in the
     *   order above, then calls scene_node_find_or_create().
     * - negative runtime: visualGroup=0 underflows
     *   scene_node_refresh_visual(0x0100EDB0), which indexes
     *   node + 8 * visualGroup - 4 + 4 * visualVariant.
     *
     * The live NPC visual table is still not fully identified. Use the first
     * valid visual family to avoid the underflow, keep the actor resource key
     * in labelText for user-visible context, and put the NPC name in shortLabel.
     */
    return vm_net_mock_seq_put_u32(out, outCap, pos, seed->actorId) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 1) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->actorResource) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, (u8)(seed->x / 16u)) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, (u8)(seed->y / 16u)) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->displayName) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->scriptName) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->x) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->y);
}

static bool vm_net_mock_append_actor_other_scene_npcs10_object(u8 *out, u32 outCap, u32 *pos,
                                                              const char *scene,
                                                              bool minimalist,
                                                              u32 *npcCountOut,
                                                              u32 *otherInfoLenOut)
{
    if (npcCountOut)
        *npcCountOut = 0;
    if (otherInfoLenOut)
        *otherInfoLenOut = 0;

    (void)scene;
    (void)minimalist;
    return vm_net_mock_append_actor_other_empty10_object(out, outCap, pos);
}

static bool vm_net_mock_select_scene_actorinfo_npc_seed(const char *scene,
                                                       vm_net_mock_scene_npc_seed *seedOut)
{
    if (seedOut == NULL)
        return false;
    memset(seedOut, 0, sizeof(*seedOut));

    (void)scene;
    return false;
}

static bool vm_net_mock_build_scene_actorinfo_npc_blob(u8 *out, u32 outCap, u32 *blobLen,
                                                       const vm_net_mock_scene_npc_seed *seed)
{
    u32 pos = 0;
    u8 targetX = 0;
    u8 targetY = 0;

    if (out == NULL || blobLen == NULL || seed == NULL || seed->actorResource == NULL)
        return false;

    targetX = (u8)(seed->x / 16u);
    targetY = (u8)(seed->y / 16u);

    /*
     * net_handle_actor_move_info case 5 (0x01012ADC -> 0x01012B7E) reads:
     *   i16 gridX, i16 gridY, i32 actorId,
     *   u8 visualVariant, u8 visualGroup,
     *   str labelText/resource, u8 targetX, u8 targetY,
     *   str shortLabel, str longLabel,
     * then calls scene_node_find_or_create(0x0100EFC4).
     */
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->x))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->y))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, seed->actorId))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, seed->actorResource))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, targetX))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, targetY))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                   seed->displayName ? seed->displayName : "NPC"))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                   seed->scriptName ? seed->scriptName : ""))
        return false;

    *blobLen = pos;
    return true;
}

static bool vm_net_mock_append_scene_actorinfo_npc_object(u8 *out, u32 outCap, u32 *pos,
                                                         const char *scene,
                                                         u32 *actorInfoLenOut,
                                                         u32 *actorIdOut)
{
    vm_net_mock_scene_npc_seed seed;
    u8 actorInfo[256];
    u32 actorInfoLen = 0;
    u32 objectStart = 0;

    if (actorInfoLenOut)
        *actorInfoLenOut = 0;
    if (actorIdOut)
        *actorIdOut = 0;

    if (!vm_net_mock_select_scene_actorinfo_npc_seed(scene, &seed))
        return false;
    memset(actorInfo, 0, sizeof(actorInfo));
    if (!vm_net_mock_build_scene_actorinfo_npc_blob(actorInfo, sizeof(actorInfo), &actorInfoLen, &seed))
        return false;
    if (actorInfoLen == 0 || actorInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    if (actorIdOut)
        *actorIdOut = seed.actorId;
    return true;
}

static bool vm_net_mock_append_battle_status7_object(u8 *out, u32 outCap, u32 *pos);
static u32 vm_net_mock_build_battle_pending_settlement_response(u8 *out, u32 outCap);

static bool vm_net_mock_is_actor_other_only10_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_scene_interaction_followup_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 14, 14) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 14, 4) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 14, 5) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 14, 6))
    {
        return false;
    }

    return true;
}

static u32 vm_net_mock_build_actor_other_only10_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 requestType = 0;
    bool hasType = false;
    bool allowDirectNpcOtherInfo = false;

    if (outCap < pos)
        return 0;
    hasType = vm_net_mock_get_object_u8_field(request, requestLen, "Type", &requestType);
    if (g_vm_net_mock_last_scene_change_target_valid)
    {
        const vm_net_mock_scene_change_target *target = &g_vm_net_mock_last_scene_change_target;
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               false, true, true, true, false, false, false))
            return 0;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                             target->scene, target->x, target->y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_mock_mark_pending_scene_pos_save(target->scene, target->x, target->y, "actor-other-portal-deferred-completion");
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        return pos;
    }
    if (hasType && requestType == 100)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 1);
        return pos;
    }

    allowDirectNpcOtherInfo = hasType &&
                              requestType == 1 &&
                              vm_net_mock_env_u8("CBE_SCENE_NPC_OTHERINFO_DIRECT", 0) != 0;
    if (allowDirectNpcOtherInfo ||
        (hasType &&
         requestType == 1 &&
         vm_net_mock_env_u32("CBE_SCENE_NPC_OTHERINFO", 0) != 0))
    {
        const char *scene = vm_net_mock_current_scene_name();
        u32 npcCount = 0;
        u32 otherInfoLen = 0;

        if (!vm_net_mock_append_actor_other_scene_npcs10_object(out, outCap, &pos,
                                                               scene, false, &npcCount, &otherInfoLen))
            return 0;
        if (npcCount > 0)
        {
            vm_net_mock_finish_wt_packet(out, pos, 1);
            return pos;
        }
        pos = 5;
    }

    if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_net_mock_read_current_player_grid(u32 *nodeOut, u32 *actorIdOut,
                                                 u16 *gridXOut, u16 *gridYOut,
                                                 u16 *targetXOut, u16 *targetYOut)
{
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;

    if (!Global_R9)
        return false;
    hudState = Global_R9 + 0x5C64;
    if (uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4) != UC_ERR_OK || currentSceneNode == 0)
        return false;
    (void)uc_mem_read(MTK, currentSceneNode + 0x64, &actorId, 4);
    (void)uc_mem_read(MTK, currentSceneNode + 0x18, &gridX, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x1A, &gridY, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x11E, &targetX, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x120, &targetY, 2);

    if ((gridX == 0 || gridY == 0) && targetX != 0 && targetY != 0)
    {
        gridX = targetX;
        gridY = targetY;
    }
    if (gridX == 0 || gridY == 0)
        return false;

    if (nodeOut)
        *nodeOut = currentSceneNode;
    if (actorIdOut)
        *actorIdOut = actorId;
    if (gridXOut)
        *gridXOut = gridX;
    if (gridYOut)
        *gridYOut = gridY;
    if (targetXOut)
        *targetXOut = targetX;
    if (targetYOut)
        *targetYOut = targetY;
    return true;
}

typedef struct
{
    const char *sourceScene;
    u16 left;
    u16 top;
    u16 right;
    u16 bottom;
    u8 sourceEntry;
    u8 targetEntry;
    const char *targetScene;
    u16 targetX;
    u16 targetY;
    const char *routeName;
} vm_net_mock_portal_fallback;

static const vm_net_mock_portal_fallback kVmNetMockPortalFallbacks[] = {
        {
            "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31\x2e\x73\x63\x65",
            119, 480, 264, 502,
            0, 0,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            396, 473,
            "penglai-01-bottom-to-02"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            108, 5, 148, 25,
            1, 2,
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            223, 382,
            "penglai-02-north-to-01"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            421, 453, 441, 490,
            0, 3,
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            145, 47,
            "penglai-02-east-to-03"
        },
        {
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33",
            75, 10, 135, 43,
            0, 1,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            128, 45,
            "penglai-03-north-to-02"
        },
        {
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33",
            421, 268, 441, 308,
            1, 3,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "c00-penglai-03-east-to-taohuadao-01"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            75, 10, 135, 43,
            1, 2,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            128, 45,
            "penglai-03-north-to-02"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            64, 400, 144, 416,
            0, 0,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            136, 58,
            "penglai-03-bottom-to-04"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            116, 10, 156, 43,
            0, 2,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            105, 395,
            "penglai-04-north-to-03"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            224, 256, 288, 285,
            1, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "penglai-04-to-taohuadao-01"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            208, 432, 256, 448,
            0, 0,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            80, 60,
            "taohuadao-01-bottom-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            240, 96, 272, 128,
            1, 3,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            256, 300,
            "taohuadao-01-to-penglai-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            320, 272, 352, 336,
            1, 3,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            40, 70,
            "taohuadao-02-east-to-03"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            32, 0, 128, 48,
            0, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "taohuadao-02-north-to-01"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            160, 553, 240, 570,
            0, 0,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            42, 60,
            "taohuadao-03-bottom-to-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            20, 10, 60, 55,
            1, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            305, 310,
            "taohuadao-03-north-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            336, 160, 384, 224,
            1, 0,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            136, 58,
            "taohuadao-04-to-penglai-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            20, 5, 64, 35,
            0, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            200, 540,
            "taohuadao-04-north-to-03"
        },
};

static bool vm_net_mock_find_portal_fallback_with_margin(const char *scene, u16 gridX, u16 gridY,
                                                         u16 margin,
                                                         vm_net_mock_scene_change_target *target,
                                                         const char **routeNameOut)
{
    size_t i;

    if (scene == NULL || target == NULL)
        return false;

    if (vm_net_mock_get_scene_portal_target_from_sce(scene, gridX, gridY, margin, target))
    {
        if (routeNameOut)
            *routeNameOut = "sce-edge-portal";
        return true;
    }

    if (vm_net_mock_scene_edge_data_available(scene))
        return false;

    if (vm_net_mock_scene_name_is_download_key(scene))
    {
        vm_autotest_note("mock_scene_missing_sce source=%s pos=(%u,%u) action=wait-client-download\n",
                         scene, gridX, gridY);
        return false;
    }

    for (i = 0; i < sizeof(kVmNetMockPortalFallbacks) / sizeof(kVmNetMockPortalFallbacks[0]); ++i)
    {
        const vm_net_mock_portal_fallback *fallback = &kVmNetMockPortalFallbacks[i];
        u16 left = (fallback->left > margin) ? (u16)(fallback->left - margin) : 0;
        u16 top = (fallback->top > margin) ? (u16)(fallback->top - margin) : 0;
        u16 right = (u16)(fallback->right + margin);
        u16 bottom = (u16)(fallback->bottom + margin);
        if (strcmp(scene, fallback->sourceScene) != 0)
            continue;
        if (gridX < left || gridX > right || gridY < top || gridY > bottom)
            continue;

        memset(target, 0, sizeof(*target));
        snprintf(target->scene, sizeof(target->scene), "%s", fallback->targetScene);
        target->x = fallback->targetX;
        target->y = fallback->targetY;
        if (routeNameOut)
            *routeNameOut = fallback->routeName;
        return true;
    }

    return false;
}

static bool vm_net_mock_find_portal_fallback(const char *scene, u16 gridX, u16 gridY,
                                             vm_net_mock_scene_change_target *target,
                                             const char **routeNameOut)
{
    return vm_net_mock_find_portal_fallback_with_margin(scene, gridX, gridY, 0,
                                                       target, routeNameOut);
}

static bool vm_net_mock_find_portal_fallback_margin(const char *scene, u16 gridX, u16 gridY,
                                                    u16 margin,
                                                    vm_net_mock_scene_change_target *target,
                                                    const char **routeNameOut)
{
    return vm_net_mock_find_portal_fallback_with_margin(scene, gridX, gridY, margin,
                                                       target, routeNameOut);
}

static u32 vm_net_mock_build_actor_other_portal_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    const char *scene = NULL;
    vm_net_mock_scene_change_target target;
    u16 gridX = 0;
    u16 gridY = 0;

    if (outCap < pos || !vm_net_mock_is_actor_other_only10_request(request, requestLen))
        return 0;

    scene = vm_net_mock_current_scene_name();
    if (vm_net_mock_is_recent_completed_scene_name(scene, 180))
        return 0;

    if (!vm_net_mock_read_current_player_grid(NULL, NULL, &gridX, &gridY, NULL, NULL))
        return 0;
    if (vm_net_mock_scene_is_penglai_transfer_scene(scene))
        return 0;

    if (!vm_net_mock_find_portal_fallback(scene, gridX, gridY, &target, NULL))
    {
        return 0;
    }

    if (!(g_vm_net_mock_pending_scene_save_valid &&
          strcmp(g_vm_net_mock_pending_scene_save_scene, target.scene) == 0))
    {
        u32 objectStart = 0;
        vm_net_mock_remember_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_from_actor_other_portal = true;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        return pos;
    }

    /*
     * IDA/runtime evidence: these same-class edge portals are already present in
     * the local scene table consumed by sub_1018166(). When the player steps
     * inside such a rect, the client emits this 2/10 Type=1 request as part of
     * its local transition countdown. If the target is already queued for a
     * pending save, leave this duplicate as a no-op and let the later completion
     * request close the transition.
     */
    return 0;
}

static bool vm_net_mock_is_actor_moveinfo_upload_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 1)
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "moveinfo"))
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_actor_moveinfo_name_update_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 12)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_actor_moveinfo_name_update_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    const char *name = vm_net_mock_scene_key_name();

    (void)request;
    (void)requestLen;
    if (outCap < pos || !vm_net_mock_is_actor_moveinfo_name_update_request(request, requestLen))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", name ? name : ""))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_net_mock_append_actor_moveinfo_empty_ack_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 1, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_seq_put_len16_blob(u8 *out, u32 outCap, u32 *pos,
                                           const u8 *data, u16 dataLen)
{
    return vm_net_mock_put_be16(out, outCap, pos, dataLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, data, dataLen);
}

static bool vm_net_mock_build_scene_monster_moveinfo_blob(u8 *out, u32 outCap,
                                                          u32 *blobLenOut,
                                                          u32 actorId,
                                                          u32 posx,
                                                          u32 posy)
{
    u32 pos = 0;
    u8 state[64];
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(actorId);
    u32 enemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
    u32 enemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", enemyHp);
    u32 enemyMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp);
    u32 enemyMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_MP", enemyMp);

    if (out == NULL || blobLenOut == NULL || actorId == 0)
        return false;
    if (enemyMaxHp < enemyHp)
        enemyMaxHp = enemyHp;
    if (enemyMaxMp < enemyMp)
        enemyMaxMp = enemyMp;

    memset(state, 0, sizeof(state));
    /*
     * net_handle_actor_move_info case 2 copies this raw blob into
     * ActorSceneNode+0x88. Battle.cbm subtype 5 then reads HP/MP from
     * node+0xB4/+0xB8/+0xBC/+0xC0, i.e. blob+44/+48/+52/+56.
     */
    vm_net_mock_store_le32(state, 44, enemyHp);
    vm_net_mock_store_le32(state, 48, enemyMp);
    vm_net_mock_store_le32(state, 52, enemyMaxHp);
    vm_net_mock_store_le32(state, 56, enemyMaxMp);

    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, (u16)posx))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, (u16)posy))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorId))
        return false;
    if (!vm_net_mock_seq_put_len16_blob(out, outCap, &pos, state, (u16)sizeof(state)))
        return false;

    *blobLenOut = pos;
    return true;
}

static bool vm_net_mock_append_scene_monster_moveinfo2_object(u8 *out, u32 outCap,
                                                              u32 *pos,
                                                              u32 actorId,
                                                              u32 posx,
                                                              u32 posy)
{
    u8 moveInfo[128];
    u32 moveInfoLen = 0;
    u32 objectStart = 0;

    memset(moveInfo, 0, sizeof(moveInfo));
    if (!vm_net_mock_build_scene_monster_moveinfo_blob(moveInfo, sizeof(moveInfo),
                                                       &moveInfoLen,
                                                       actorId, posx, posy))
        return false;
    if (moveInfoLen == 0 || moveInfoLen > 0xffff)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 2, &objectStart))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "moveinfo",
                                      moveInfo, (u16)moveInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(actorId);
        u32 enemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
        u32 enemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", enemyHp);
        u32 enemyMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp);
        u32 enemyMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_MP", enemyMp);
        if (enemyMaxHp < enemyHp)
            enemyMaxHp = enemyHp;
        if (enemyMaxMp < enemyMp)
            enemyMaxMp = enemyMp;
        vm_autotest_note("mock_scene_monster_moveinfo actor=%u level=%u pos=(%u,%u) hp=%u/%u mp=%u/%u len=%u\n",
                         actorId, stats.level, posx, posy,
                         enemyHp, enemyMaxHp, enemyMp, enemyMaxMp,
                         moveInfoLen);
    }
    return true;
}

static bool vm_net_mock_select_scene_actor_moveinfo_target(u32 actorId,
                                                           u32 *indexOut,
                                                           u32 *posxOut,
                                                           u32 *posyOut)
{
    u32 sceneNodeBase = 0;

    if (actorId == 0 || Global_R9 == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5CB0, &sceneNodeBase, sizeof(sceneNodeBase)) != UC_ERR_OK ||
        sceneNodeBase == 0)
        return false;

    for (u32 i = 0; i < 25; ++i)
    {
        u32 node = sceneNodeBase + i * 340u;
        u32 nodeActorId = 0;
        u32 nodePosX = 0;
        u32 nodePosY = 0;
        u8 active = 0;
        if (uc_mem_read(MTK, node + 319, &active, sizeof(active)) != UC_ERR_OK || active == 0)
            continue;
        if (uc_mem_read(MTK, node + 100, &nodeActorId, sizeof(nodeActorId)) != UC_ERR_OK ||
            nodeActorId != actorId)
            continue;
        (void)uc_mem_read(MTK, node + 240, &nodePosX, sizeof(nodePosX));
        (void)uc_mem_read(MTK, node + 244, &nodePosY, sizeof(nodePosY));
        if (indexOut)
            *indexOut = i;
        if (posxOut)
            *posxOut = nodePosX;
        if (posyOut)
            *posyOut = nodePosY;
        return true;
    }
    return false;
}

static bool vm_net_mock_snapshot_current_player_pos(const char *reason)
{
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;

    if (!vm_net_mock_read_current_player_grid(&currentSceneNode, &actorId,
                                              &gridX, &gridY, &targetX, &targetY))
        return false;

    vm_net_mock_save_player_pos_state(NULL, gridX, gridY, reason);
    return true;
}

static u32 vm_net_mock_build_actor_moveinfo_ack_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    const u8 *moveInfo = NULL;
    u16 moveInfoLen = 0;
    bool snappedPos = false;
    const char *scene = NULL;
    const char sourceScene01[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_01.sce */
    const char targetScene01[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_02.sce */
    const char sourceScene03[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_03.sce */
    const char targetScene03[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_04.sce */
    u16 gridX = 0;
    u16 gridY = 0;
    vm_net_mock_scene_change_target portalTarget;

    if (outCap < pos || !vm_net_mock_is_actor_moveinfo_upload_request(request, requestLen))
        return 0;
    (void)vm_net_mock_get_object_blob_field(request, requestLen, "moveinfo", &moveInfo, &moveInfoLen);
    snappedPos = vm_net_mock_snapshot_current_player_pos("moveinfo-upload");
    scene = vm_net_mock_current_scene_name();
    vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(scene);
    gridX = vm_net_mock_scene_spawn_x();
    gridY = vm_net_mock_scene_spawn_y();

    /*
     * 2026-06-09 runtime evidence: the first Taohuadao bottom exit can stop at
     * y=410 while the local SCE trigger rect starts at y=432, so the client
     * never emits the later 2/3 or 4/1 scene-change request. Treat the narrow
     * approach band as the server-side completion for this known portal only.
     */
    if (snappedPos &&
        strcmp(scene, sourceScene01) == 0 &&
        gridX >= 208 && gridX <= 256 &&
        gridY >= 408 && gridY <= 448)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 1, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, false, false, 0, targetScene01, 80, 60))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        vm_net_mock_save_player_pos_state(targetScene01, 80, 60, "moveinfo-taohuadao-bottom-portal");
        return pos;
    }

    if (snappedPos &&
        strcmp(scene, sourceScene03) == 0 &&
        gridX >= 160 && gridX <= 240 &&
        gridY >= 553 && gridY <= 570)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 1, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, false, false, 0, targetScene03, 42, 60))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        vm_net_mock_save_player_pos_state(targetScene03, 42, 60, "moveinfo-taohuadao-bottom-portal");
        return pos;
    }

    if (snappedPos &&
        vm_net_mock_find_portal_fallback_margin(scene, gridX, gridY, 8,
                                                &portalTarget, NULL))
    {
        /*
         * Local-table same-class portals already drive sub_1018166() through a
         * 30-frame countdown. A moveinfo-side split ack in this margin window
         * races that local path, sets a pending scene save early, and prevents
         * the later pendingRawMatch=1 2/10 completion from being exercised.
         */
    }

    /*
     * Fallback only. Runtime evidence showed that post-scene 2/1 moveinfo arrives
     * after 30/1 has closed sceneObj+0x164, so the normal business dispatcher
     * decodes this packet into the shared container but does not enter the
     * kind=2/subtype=10 actor parser. The default scene follow-up now seeds
     * NPC otherinfo while dispatchGate is still open; keep this path gated by
     * pending+not-seeded for rollback experiments and negative evidence.
     */
    if (!g_vm_net_mock_scene_moveinfo_npc_seeded &&
        vm_net_mock_is_scene_moveinfo_npc_seed_request(scene, moveInfo, moveInfoLen) &&
        vm_net_mock_scene_supports_actor_other_npc_seed(scene))
    {
        u8 objectCount = 0;
        u32 npcCount = 0;
        u32 otherInfoLen = 0;

        if (!vm_net_mock_append_actor_other_scene_npcs10_object(out, outCap, &pos,
                                                                scene, false,
                                                                &npcCount, &otherInfoLen))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
        g_vm_net_mock_scene_moveinfo_npc_seeded = true;
        snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                 sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene),
                 "%s", scene ? scene : "");
        return pos;
    }

    /*
     * net_handle_actor_move_info() only reads moveinfo for subtype 2. Subtype 1
     * falls through the default branch at 0x01012B0C, so keep this upload ack
     * empty until the live server semantics are recovered.
     */
    if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_net_mock_is_challenge_interaction_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    bool hasChallenge = false;
    bool hasMoveinfo = false;

    if (request == NULL || requestLen < 9)
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 4 && object.subtype == 1)
        {
            if (hasChallenge)
                return false;
            hasChallenge = true;
        }
        else if (object.major == 1 && object.kind == 2 && object.subtype == 1)
        {
            if (hasMoveinfo)
                return false;
            hasMoveinfo = true;
        }
        else
        {
            return false;
        }
    }
    return offset == requestLen &&
           hasChallenge &&
           vm_net_mock_request_contains(request, requestLen, "id") &&
           vm_net_mock_request_contains(request, requestLen, "index") &&
           vm_net_mock_request_contains(request, requestLen, "posx") &&
           vm_net_mock_request_contains(request, requestLen, "posy");
}

static u32 vm_net_mock_build_battle_start_info_blob(u8 *out, u32 outCap,
                                                    u32 roleId, u32 enemyId,
                                                    bool playerOnRight)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();
    const char *leftName = vm_net_mock_env_str("CBE_BATTLE_LEFT_NAME", "Monster");
    u32 roleIdDefault = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;
    vm_net_mock_monster_stats enemyStats;
    u32 enemyHp = 0;
    u32 enemyMaxHp = 0;
    u32 enemyMp = 0;
    u32 enemyMaxMp = 0;
    u32 leftId = 0;
    u32 leftHp = 0;
    u32 leftMaxHp = 0;
    u32 leftMp = 0;
    u32 leftMaxMp = 0;
    u32 rightId = 0;
    u32 rightHp = 0;
    u32 rightMaxHp = 0;
    u32 rightMp = 0;
    u32 rightMaxMp = 0;
    u8 leftVisual0 = vm_net_mock_env_u8("CBE_BATTLE_LEFT_VISUAL_BYTE0", 0);
    u8 leftVisual1 = vm_net_mock_env_u8("CBE_BATTLE_LEFT_VISUAL_BYTE1", 1);

    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", roleHpDefault);
    roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleMaxHpDefault);
    roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", roleMpDefault);
    roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMaxMpDefault);
    if (roleId == 0)
        roleId = roleIdDefault;
    if (roleMaxHp < roleMaxHpDefault)
        roleMaxHp = roleMaxHpDefault;
    if (roleMaxMp < roleMaxMpDefault)
        roleMaxMp = roleMaxMpDefault;
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;
    if (enemyId == 0)
        enemyId = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
    enemyStats = vm_net_mock_monster_stats_for_enemy(enemyId);
    enemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", enemyStats.hp);
    enemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", enemyHp);
    enemyMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", enemyStats.mp);
    enemyMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_MP", enemyMp);
    if (enemyMaxHp < enemyHp)
        enemyMaxHp = enemyHp;
    if (enemyMaxMp < enemyMp)
        enemyMaxMp = enemyMp;
    leftId = playerOnRight ? enemyId : roleId;
    leftHp = playerOnRight ? enemyHp : roleHp;
    leftMaxHp = playerOnRight ? enemyMaxHp : roleMaxHp;
    leftMp = playerOnRight ? enemyMp : roleMp;
    leftMaxMp = playerOnRight ? enemyMaxMp : roleMaxMp;
    rightId = playerOnRight ? roleId : enemyId;
    rightHp = playerOnRight ? roleHp : enemyHp;
    rightMaxHp = playerOnRight ? roleMaxHp : enemyMaxHp;
    rightMp = playerOnRight ? roleMp : enemyMp;
    rightMaxMp = playerOnRight ? roleMaxMp : enemyMaxMp;

    /*
     * Battle.cbm HandleBattleStartMsg(0x66CC) places the first count at
     * x=40 and the second count at x=200. Keep the default player-on-right
     * layout by sending the monster as the left record and the role id as the
     * right record; sub_6C5E() then finds the current controllable fighter by
     * matching the role id, so input follows the right-side sprite.
     *
     * The first record's visual bytes are passed as sub_23F6(second, first).
     * Defaults 0/1 preserve the existing non-crashing left-side art.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftMaxMp))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, playerOnRight ? leftName : roleName))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, leftVisual0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, leftVisual1))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightMaxMp))
        return 0;
    return pos;
}

static u32 vm_net_mock_build_battle_scene_start_info_blob(u8 *out, u32 outCap,
                                                          u32 sceneMonsterIndex,
                                                          u32 sceneMonsterX,
                                                          u32 sceneMonsterY,
                                                          u32 roleId)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleIdDefault = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;

    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", roleHpDefault);
    roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleMaxHpDefault);
    roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", roleMpDefault);
    roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMaxMpDefault);
    if (roleId == 0)
        roleId = roleIdDefault;
    if (roleMaxHp < roleMaxHpDefault)
        roleMaxHp = roleMaxHpDefault;
    if (roleMaxMp < roleMaxMpDefault)
        roleMaxMp = roleMaxMpDefault;
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;

    /*
     * Battle.cbm HandleBattleStartMsg(0x66CC), subtype 5, is the native
     * scene-monster entry path. After the first count it reads scene index,
     * posx, and posy, then copies the left fighter from the Battle.cbm scene
     * actor table at *(R9+13476). This keeps the monster name/sprite tied to
     * the touched scene node instead of guessing two sub_23F6 visual bytes.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterIndex))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterX))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterY))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxMp))
        return 0;
    return pos;
}

static u32 vm_net_mock_normalize_battle_enemy_id(u32 requestedId)
{
    u32 defaultEnemyId = vm_net_mock_env_u32("CBE_BATTLE_DEFAULT_ENEMY_ID", 105);
    if (defaultEnemyId == 0)
        defaultEnemyId = 105;
    if (requestedId == 0 || requestedId == 10001)
        return defaultEnemyId;
    return requestedId;
}

static u32 vm_net_mock_resolve_battle_enemy_id(u32 requestedId, u32 *tableBaseOut, u32 tableIds[4])
{
    u32 loaderR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
    u32 battleBase = 0;
    u32 enemyTable = 0;
    u32 firstNonzero = 0;

    if (tableBaseOut)
        *tableBaseOut = 0;
    if (tableIds)
        memset(tableIds, 0, sizeof(u32) * 4);
    if (loaderR9 == 0)
        loaderR9 = g_currentScreenModuleBase;
    if (loaderR9 == 0)
        return requestedId;

    battleBase = loaderR9 + 0x3450u;
    if (uc_mem_read(MTK, battleBase + 0x50u, &enemyTable, 4) != UC_ERR_OK || enemyTable == 0)
        return requestedId;
    if (tableBaseOut)
        *tableBaseOut = enemyTable;

    for (u32 i = 0; i < 4; ++i)
    {
        u32 id = 0;
        (void)uc_mem_read(MTK, enemyTable + i * 0x4Cu + 0x24u, &id, 4);
        if (tableIds)
            tableIds[i] = id;
        if (id == requestedId)
            return requestedId;
        if (firstNonzero == 0 && id != 0)
            firstNonzero = id;
    }

    /*
     * A request id that is not already present in Battle.cbm's enemy template
     * table is not render-safe: sub_66CC takes the lookup-failed path and the
     * later fighter draw loop calls a zero callback. Keep the table id as the
     * default crash-safe server contract; force the request id only for focused
     * experiments while recovering the upstream template-population contract.
     */
    if (requestedId != 0 && vm_net_mock_env_u32("CBE_BATTLE_FORCE_REQUEST_ENEMY_ID", 0) != 0)
        return requestedId;
    return firstNonzero ? firstNonzero : requestedId;
}

static bool vm_net_mock_is_battle_operate_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 4 &&
           object.subtype == 2 &&
           vm_net_mock_request_contains(request, requestLen, "index") &&
           vm_net_mock_request_contains(request, requestLen, "Operate");
}

static bool vm_net_mock_parse_battle_item_use_request(const u8 *request, u32 requestLen,
                                                      vm_net_mock_battle_item_use_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_battle_item_use_request parsed;
    u32 index = 0;
    u32 seq = 0;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 4 || object.subtype != 3 ||
        object.payloadLen == 0)
    {
        return false;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "index", &index))
        return false;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq", &seq) &&
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemseq", &seq) &&
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemSeq", &seq))
    {
        return false;
    }
    if (seq == 0 || seq > 0xffffu)
        return false;

    parsed.index = index;
    parsed.seq = (u16)seq;
    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_is_battle_operate_request_relaxed(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return object.major == 1 &&
           object.kind == 4 &&
           object.subtype == 2;
}

static bool vm_net_mock_current_screen_is_battle(void)
{
    u32 inferredCodeBase = 0;
    u32 inferredModuleR9 = 0;

    if (vmAddedScreen != 0 &&
        vm_infer_battle_module_from_screen(vmAddedScreen, &inferredCodeBase, &inferredModuleR9))
        return true;
    if (g_currentScreenThis != 0)
    {
        u32 screen = g_currentScreenThis + 0x18u;
        if (vm_infer_battle_module_from_screen(screen, &inferredCodeBase, &inferredModuleR9))
            return true;
    }
    return false;
}

static bool vm_net_mock_battle_operate_is_skill(u32 operate)
{
    return operate > 2;
}

static bool vm_net_mock_battle_inline_settlement_enabled(void)
{
    /*
     * The result panel is opened by the terminal battle action. If 4/7 arrives
     * only on the next request, the panel has already copied zeroed settlement
     * caches. Keep 4/7 in the terminal response by default and make hp/mp
     * recovery deltas explicit zero unless an env override opts in.
     */
    return vm_net_mock_env_u32("CBE_BATTLE_INLINE_SETTLEMENT", 1) != 0;
}

static u32 vm_net_mock_battle_operate_skill_id(u32 operate)
{
    return operate > 2 ? operate - 2 : 0;
}

static const vm_net_mock_skill_catalog_item *vm_net_mock_battle_operate_skill(u32 operate)
{
    u32 skillId = vm_net_mock_battle_operate_skill_id(operate);

    if (skillId == 0)
        return NULL;
    return vm_net_mock_find_skill_catalog_item(skillId);
}

static u32 vm_net_mock_battle_operate_skill_effect(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    if (skill != NULL && skill->effectIndex != 0)
        return skill->effectIndex;
    return 0;
}

static void vm_net_mock_battle_sync_role_mp_from_role(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    g_mockBattleRoleMpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
    g_mockBattleRoleMpCurrent = vm_net_mock_min_u32(role->mp, g_mockBattleRoleMpMax);
}

static u32 vm_net_mock_battle_role_mp_current(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (g_mockBattleRoleMpMax == 0 && role != NULL)
        vm_net_mock_battle_sync_role_mp_from_role(role);
    return g_mockBattleRoleMpCurrent;
}

static void vm_net_mock_battle_set_role_mp_current(u32 mp)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role != NULL)
    {
        vm_net_mock_role_sync_derived_vitals(role);
        if (g_mockBattleRoleMpMax == 0)
            g_mockBattleRoleMpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
        if (g_mockBattleRoleMpMax == 0)
            g_mockBattleRoleMpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
        g_mockBattleRoleMpCurrent = vm_net_mock_min_u32(mp, g_mockBattleRoleMpMax);
        role->mp = g_mockBattleRoleMpCurrent;
        return;
    }
    g_mockBattleRoleMpCurrent = mp;
}

static bool vm_net_mock_battle_prepare_skill_mp(u32 operate,
                                                u32 *mpBeforeOut,
                                                u32 *mpAfterOut,
                                                u32 *mpCostOut)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 mpBefore = 0;
    u32 mpCost = skill ? skill->mpCost : 0;
    u32 mpAfter = 0;

    if (mpBeforeOut)
        *mpBeforeOut = 0;
    if (mpAfterOut)
        *mpAfterOut = 0;
    if (mpCostOut)
        *mpCostOut = mpCost;
    if (role == NULL || !vm_net_mock_battle_operate_is_skill(operate))
        return false;

    if (g_mockBattleRoleMpMax == 0)
        vm_net_mock_battle_sync_role_mp_from_role(role);
    mpBefore = g_mockBattleRoleMpCurrent;
    mpAfter = (mpBefore > mpCost) ? (mpBefore - mpCost) : 0;
    if (mpBeforeOut)
        *mpBeforeOut = mpBefore;
    if (mpAfterOut)
        *mpAfterOut = mpAfter;
    return true;
}

static void vm_net_mock_battle_commit_skill_mp(u32 mpAfter)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (role->mp != mpAfter)
    {
        vm_net_mock_battle_set_role_mp_current(mpAfter);
        vm_net_mock_role_db_save("battle-skill-use");
    }
    else
    {
        vm_net_mock_battle_set_role_mp_current(mpAfter);
    }
}

static bool vm_net_mock_append_battle_actioninfo_child(u8 *actionInfo, u32 actionInfoCap,
                                                       u32 *actionInfoLen,
                                                       u8 mappedTargetWireSlot,
                                                       u8 childFlag,
                                                       u32 valueA,
                                                       u32 valueBSeed)
{
    return vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen,
                                  mappedTargetWireSlot) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen,
                                  childFlag) &&
           vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen,
                                   valueA) &&
           vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen,
                                   valueBSeed);
}

static bool vm_net_mock_append_battle_actioninfo_record_ex(u8 *actionInfo, u32 actionInfoCap,
                                                           u32 *actionInfoLen, u8 actionType,
                                                           u8 mappedActorWireSlot,
                                                           u8 mappedTargetWireSlot,
                                                           u8 childFlag, u32 valueA,
                                                           u32 valueBSeed,
                                                           bool includeSecondChild,
                                                           u8 secondTargetWireSlot,
                                                           u8 secondChildFlag,
                                                           u32 secondValueA,
                                                           u32 secondValueB,
                                                           u32 effectIndex,
                                                           u8 tail0, u8 tail1, u8 tail2)
{
    char valueText[16];
    const char *blobText = "";
    bool ok = false;
    u8 childCount = includeSecondChild ? 2 : 1;

    if (actionType == 1)
    {
        snprintf(valueText, sizeof(valueText), "%u",
                 vm_net_mock_battle_delta_display_value(valueA));
        blobText = valueText;
    }

    ok = vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, actionType) &&
         vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, mappedActorWireSlot);
    if (!ok)
        return false;

    if (actionType == 3 || actionType == 4)
        return true;

    ok = vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, childCount) &&
         vm_net_mock_append_battle_actioninfo_child(actionInfo, actionInfoCap,
                                                    actionInfoLen,
                                                    mappedTargetWireSlot,
                                                    childFlag, valueA,
                                                    valueBSeed);
    if (ok && includeSecondChild)
    {
        ok = vm_net_mock_append_battle_actioninfo_child(actionInfo, actionInfoCap,
                                                       actionInfoLen,
                                                       secondTargetWireSlot,
                                                       secondChildFlag,
                                                       secondValueA,
                                                       secondValueB);
    }
    if (!ok)
        return false;

    if (actionType != 1 && actionType != 2)
        return true;

    return vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen, effectIndex) &&
           vm_net_mock_seq_put_string(actionInfo, actionInfoCap, actionInfoLen, blobText) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail0) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail1) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail2);
}

static bool vm_net_mock_append_battle_actioninfo_record(u8 *actionInfo, u32 actionInfoCap,
                                                        u32 *actionInfoLen, u8 actionType,
                                                        u8 mappedActorWireSlot,
                                                        u8 mappedTargetWireSlot,
                                                        u8 childFlag, u32 valueA,
                                                        u32 valueBSeed, u32 effectIndex,
                                                        u8 tail0, u8 tail1, u8 tail2)
{
    return vm_net_mock_append_battle_actioninfo_record_ex(actionInfo, actionInfoCap,
                                                         actionInfoLen, actionType,
                                                         mappedActorWireSlot,
                                                         mappedTargetWireSlot,
                                                         childFlag, valueA,
                                                         valueBSeed,
                                                         false, 0, 0, 0, 0,
                                                         effectIndex,
                                                         tail0, tail1, tail2);
}

static bool vm_net_mock_build_battle_teaminfo_blob(u8 *out, u32 outCap,
                                                   u32 *teamInfoLenOut,
                                                   u32 roleId, u32 roleHp,
                                                   u32 roleMp)
{
    u32 pos = 0;

    if (teamInfoLenOut)
        *teamInfoLenOut = 0;
    if (out == NULL || roleId == 0)
        return false;

    /*
     * mmBattle InitActionSlot_B(0x6DBC) calls the tagged-i32 reader three
     * times, but rewinds the stream cursor by two bytes after the first two
     * calls.  The resulting row is an overlapped tagged-i32 sequence:
     *
     *   00 04, id32, hp32, mp32
     *
     * Call starts are row+0, row+4, and row+8, so the returned values are id,
     * hp (ignored by current client code), and mp.  Sending three normal tagged
     * u32 values makes the third read return hp_low16 + next tag header, which
     * was observed as 0x210004 and crashes the battle renderer.
     */
    if (!vm_net_mock_put_u8(out, outCap, &pos, 0))
        return false;
    if (!vm_net_mock_put_u8(out, outCap, &pos, 4))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, &pos, roleId))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, &pos, roleHp))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, &pos, roleMp))
        return false;

    if (teamInfoLenOut)
        *teamInfoLenOut = pos;
    return true;
}

static bool vm_net_mock_put_battle_action_companion_fields(u8 *out, u32 outCap, u32 *pos,
                                                           bool includeTeamInfo,
                                                           u32 teamRoleId,
                                                           u32 teamRoleHp,
                                                           u32 teamRoleMp)
{
    u8 teamInfo[64];
    u32 teamInfoLen = 0;

    if (!includeTeamInfo)
        return true;

    memset(teamInfo, 0, sizeof(teamInfo));
    if (!vm_net_mock_build_battle_teaminfo_blob(teamInfo, sizeof(teamInfo),
                                                &teamInfoLen, teamRoleId,
                                                teamRoleHp, teamRoleMp))
        return false;
    if (teamInfoLen == 0 || teamInfoLen > 0xffff)
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "teaminfo",
                                    teamInfo, (u16)teamInfoLen))
        return false;
    return true;
}

static bool vm_net_mock_append_battle_case11_auto_flag_object(u8 *out, u32 outCap,
                                                              u32 *pos, u8 type)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 11, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", type))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_action6_object_ex(u8 *out, u32 outCap, u32 *pos,
                                                        const u8 *actionInfo,
                                                        u32 actionInfoLen,
                                                        u8 actionCount,
                                                        bool includeTeamInfo,
                                                        u32 teamRoleId,
                                                        u32 teamRoleHp,
                                                        u32 teamRoleMp)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 6, &objectStart))
        return false;
    if (!vm_net_mock_put_battle_action_companion_fields(out, outCap, pos,
                                                       includeTeamInfo,
                                                       teamRoleId,
                                                       teamRoleHp,
                                                       teamRoleMp))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "actionnum", actionCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "actioninfo",
                                    actionInfo, (u16)actionInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_action6_object(u8 *out, u32 outCap, u32 *pos,
                                                     const u8 *actionInfo,
                                                     u32 actionInfoLen,
                                                     u8 actionCount)
{
    return vm_net_mock_append_battle_action6_object_ex(out, outCap, pos,
                                                      actionInfo, actionInfoLen,
                                                      actionCount,
                                                      false, 0, 0, 0);
}

static u32 vm_net_mock_build_battle_single_action_response_ex(u8 *out, u32 outCap,
                                                              u8 actionType, u8 actorWireSlot,
                                                              u8 targetWireSlot, u8 childFlag,
                                                              u32 valueA, u32 valueB,
                                                              bool includeAutoFlag, u8 autoFlagType)
{
    u32 pos = 5;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u32 objectCount = 0;
    u32 effectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos)
        return 0;
    memset(actionInfo, 0, sizeof(actionInfo));
    if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                     &actionInfoLen, actionType,
                                                     actorWireSlot, targetWireSlot,
                                                     childFlag, valueA, valueB,
                                                     (actionType == 1 || actionType == 2) ? effectIndex : 0,
                                                     (actionType == 1 || actionType == 2) ? tail0 : 0,
                                                     (actionType == 1 || actionType == 2) ? tail1 : 0,
                                                     (actionType == 1 || actionType == 2) ? tail2 : 0))
        return 0;
    if (includeAutoFlag)
    {
        if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, autoFlagType))
            return 0;
        ++objectCount;
    }
    if (!vm_net_mock_append_battle_action6_object(out, outCap, &pos,
                                                 actionInfo, actionInfoLen, 1))
        return 0;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, (u8)objectCount);
    return pos;
}

static u32 vm_net_mock_build_battle_single_action_response(u8 *out, u32 outCap,
                                                           u8 actionType, u8 actorWireSlot,
                                                           u8 targetWireSlot, u8 childFlag,
                                                           u32 valueA, u32 valueB)
{
    return vm_net_mock_build_battle_single_action_response_ex(out, outCap,
                                                              actionType,
                                                              actorWireSlot,
                                                              targetWireSlot,
                                                              childFlag,
                                                              valueA,
                                                              valueB,
                                                              false,
                                                              0);
}

static u32 vm_net_mock_build_battle_case11_auto_off_response(u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (outCap < pos ||
        !vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, 0))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_battle_item_use_response(const u8 *request, u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    vm_net_mock_battle_item_use_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    const vm_net_mock_item_effect_catalog_item *effect = NULL;
    u32 itemId = 0;
    u32 remaining = 0;
    u32 hpEffect = 0;
    u32 mpEffect = 0;
    u32 expEffect = 0;
    u32 hpApplied = 0;
    u32 mpApplied = 0;
    u32 expApplied = 0;
    u32 counterDamageValue = 0;
    u32 counterHpDelta = 0;
    bool consumed = false;
    bool applied = false;
    bool includeCounterattack = false;
    bool battleEndsThisRound = false;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", 0);
    u8 counterChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    u32 itemEffectIndex = vm_net_mock_env_u32("CBE_BATTLE_ITEM_EFFECT_INDEX", 0);
    u8 itemTail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TAIL0", 0);
    u8 itemTail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TAIL1", 0);
    u8 itemTail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TAIL2", 0);
    u8 actionInfo[192];
    u32 actionInfoLen = 0;
    u8 actionCount = 0;
    u32 pos = 5;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_battle_item_use_request(request, requestLen, &parsed))
        return 0;

    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);

    if (g_mockBattleAwaitingSettlement != 0)
        return vm_net_mock_build_battle_pending_settlement_response(out, outCap);

    role = vm_net_mock_active_role();
    if (role != NULL)
    {
        vm_net_mock_role_sync_derived_vitals(role);
        item = vm_net_mock_role_find_backpack_item(role, 0, parsed.seq);
    }
    if (item != NULL)
    {
        itemId = item->itemId;
        effect = vm_net_mock_find_item_effect_catalog_item(itemId);
        if (vm_net_mock_item_effect_is_usable(effect))
        {
            hpEffect = effect->hp;
            mpEffect = effect->mp;
            expEffect = effect->exp;
            consumed = vm_net_mock_role_consume_backpack_item(role, itemId, parsed.seq, 1, &remaining);
        }
    }

    if (role != NULL && consumed)
    {
        u32 hpMax = role->hpMax ? role->hpMax : VM_NET_MOCK_ROLE_DEFAULT_HP;
        u32 mpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
        u32 beforeHp = 0;
        u32 beforeMp = vm_net_mock_battle_role_mp_current();
        u32 addHp = vm_net_mock_mul_capped_u32(hpEffect, 1);
        u32 addMp = vm_net_mock_mul_capped_u32(mpEffect, 1);
        u32 addExp = vm_net_mock_mul_capped_u32(expEffect, 1);

        if (g_mockBattleRoleHpCurrent == 0)
            g_mockBattleRoleHpCurrent = role->hp;
        if (g_mockBattleRoleHpMax == 0)
            g_mockBattleRoleHpMax = hpMax;
        if (g_mockBattleRoleHpMax < g_mockBattleRoleHpCurrent)
            g_mockBattleRoleHpMax = g_mockBattleRoleHpCurrent;
        beforeHp = g_mockBattleRoleHpCurrent;

        if (addHp != 0)
        {
            g_mockBattleRoleHpCurrent =
                vm_net_mock_min_u32(vm_net_mock_add_capped_u32(g_mockBattleRoleHpCurrent, addHp),
                                    g_mockBattleRoleHpMax);
            hpApplied = g_mockBattleRoleHpCurrent >= beforeHp
                            ? g_mockBattleRoleHpCurrent - beforeHp
                            : 0;
        }
        if (addMp != 0)
        {
            u32 afterMp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(beforeMp, addMp), mpMax);
            vm_net_mock_battle_set_role_mp_current(afterMp);
            mpApplied = afterMp >= beforeMp ? afterMp - beforeMp : 0;
        }
        if (addExp != 0)
        {
            role->exp = vm_net_mock_add_capped_u32(role->exp, addExp);
            role->level = vm_net_mock_role_level_from_exp(role->exp);
            vm_net_mock_role_sync_derived_vitals(role);
            if (role->hpMax > g_mockBattleRoleHpMax)
                g_mockBattleRoleHpMax = role->hpMax;
            if (g_mockBattleRoleHpCurrent > g_mockBattleRoleHpMax)
                g_mockBattleRoleHpCurrent = g_mockBattleRoleHpMax;
            expApplied = addExp;
        }

        role->hp = vm_net_mock_min_u32(g_mockBattleRoleHpCurrent,
                                       role->hpMax ? role->hpMax : g_mockBattleRoleHpMax);
        applied = hpApplied != 0 || mpApplied != 0 || expApplied != 0;
    }

    memset(actionInfo, 0, sizeof(actionInfo));
    if (consumed)
    {
        u8 itemActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_ACTOR_WIRE_SLOT",
                                                       playerSlot);
        u8 itemTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TARGET_WIRE_SLOT",
                                                        playerSlot);
        u32 itemMpValueB = mpApplied != 0 ? mpApplied : vm_net_mock_battle_role_mp_current();
        if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                         &actionInfoLen, 2,
                                                         itemActorWireSlot,
                                                         itemTargetWireSlot,
                                                         0, hpApplied, itemMpValueB,
                                                         itemEffectIndex,
                                                         itemTail0, itemTail1, itemTail2))
        {
            return 0;
        }
        actionCount = 1;

        includeCounterattack = vm_net_mock_env_u32("CBE_BATTLE_ITEM_USE_COUNTER", 1) != 0 &&
                               g_mockBattleEnemyHpCurrent > 0 &&
                               g_mockBattleRoleHpCurrent > 0;
        if (includeCounterattack)
        {
            u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                             enemySlot);
            u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                              playerSlot);
            u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
            u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
            u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
            u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

            if (counterActionType == 1)
            {
                counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                              counterActorWireSlot);
                counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                               counterTargetWireSlot);
            }
            counterDamageValue = vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                                         g_mockBattleRoleHpCurrent);
            if (counterDamageValue == 0 && g_mockBattleRoleHpCurrent > 0)
                counterDamageValue = 1;
            if (g_mockBattleRoleHpCurrent >= counterDamageValue)
                g_mockBattleRoleHpCurrent -= counterDamageValue;
            else
                g_mockBattleRoleHpCurrent = 0;
            if (role != NULL)
                role->hp = g_mockBattleRoleHpCurrent;
            counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, counterActionType,
                                                             counterActorWireSlot,
                                                             counterTargetWireSlot,
                                                             counterChildFlag,
                                                             counterHpDelta,
                                                             vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0),
                                                             (counterActionType == 1 || counterActionType == 2) ? type1EffectIndex : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail0 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail1 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail2 : 0))
            {
                return 0;
            }
            ++actionCount;
        }

        battleEndsThisRound = g_mockBattleRoleHpCurrent == 0;
        if (battleEndsThisRound)
        {
            u8 terminalActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTION_TYPE", 3);
            u8 terminalActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTOR_WIRE_SLOT",
                                                              playerSlot);
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, terminalActionType,
                                                             terminalActorWireSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
            {
                return 0;
            }
            ++actionCount;
        }
    }
    else
    {
        if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                         &actionInfoLen, 4,
                                                         playerSlot, 0, 0,
                                                         0, 0, 0, 0, 0, 0))
        {
            return 0;
        }
        actionCount = 1;
    }

    if (battleEndsThisRound && vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos))
            return 0;
        ++objectCount;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
    }
    if (!vm_net_mock_append_battle_action6_object(out, outCap, &pos,
                                                 actionInfo, actionInfoLen,
                                                 actionCount))
        return 0;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    if (consumed && !battleEndsThisRound)
        vm_net_mock_role_db_save("battle-item-use");
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = 1;
    }
    else
    {
        g_mockBattlePendingEnemyTurn = 0;
    }

    printf("[info][network] mock_battle_item_use index=%u seq=%u item=%u remaining=%u hp=%u/%u mp=%u/%u exp=%u consumed=%u applied=%u counter=%u resp=%u evidence=mmBattle:0x2B50->4/3,0x7BD0/0x6EB0->4/6\n",
           parsed.index, parsed.seq, itemId, remaining,
           hpApplied, hpEffect, mpApplied, mpEffect, expApplied,
           consumed ? 1 : 0, applied ? 1 : 0, counterDamageValue, pos);
    vm_autotest_note("mock_battle_item_use index=%u seq=%u item=%u remaining=%u hp=%u/%u mp=%u/%u exp=%u consumed=%u applied=%u counter=%u response=4/6-actionType2 evidence=mmBattle:0x2B50,0x6EB0\n",
                     parsed.index, parsed.seq, itemId, remaining,
                     hpApplied, hpEffect, mpApplied, mpEffect, expApplied,
                     consumed ? 1 : 0, applied ? 1 : 0, counterDamageValue);
    return pos;
}

static u32 vm_net_mock_build_battle_operate_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 index = 0;
    u32 operate = 0;
    u8 index8 = 0;
    u8 operate8 = 0;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u32 responseObjectCount = 1;
    u8 actorSlot = 0;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 requestedTargetSlot = enemySlot;
    u8 firstRecordWireActorUsed = 0;
    u8 firstRecordWireTargetUsed = 0;
    u32 attackDamageValue = 12;
    u32 counterDamageValue = 10;
    u32 attackHpDelta = 0;
    u32 counterHpDelta = 0;
    u8 actionCount = 1;
    bool bundleWholeRound = false;
    u8 firstRecordActorWireSlot = 0;
    u8 firstRecordChildFlag = 0;
    u8 counterRecordChildFlag = 0;
    u32 firstRecordMpDelta = 0;
    u32 counterRecordMpDelta = 0;
    bool battleEndsThisRound = false;
    bool allowCounterattack = false;
    bool terminalFollowup = false;
    bool operateIsSkill = false;
    bool operateIsOffensive = false;
    bool skillMpPrepared = false;
    u32 skillMpCost = 0;
    u32 skillMpBefore = 0;
    u32 skillMpAfter = 0;
    u32 skillMpDelta = 0;
    bool skillTeamInfoEnabled = false;
    u32 skillTeamRoleId = 0;
    u32 skillTeamHp = 0;
    u32 skillTeamMp = 0;
    u32 skillCostValueA = 0;
    u32 skillCostValueB = 0;
    bool skillCostActionEnabled = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_ENABLED", 0) != 0;
    u8 skillCostActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_TYPE", 0);
    u32 skillCostEffectIndex = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_EFFECT_INDEX", 0);
    u8 actionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 0);
    u8 firstActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTION_TYPE", actionType);
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", actionType);
    u8 terminalActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTION_TYPE", 3);
    u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos || !vm_net_mock_is_battle_operate_request(request, requestLen))
        return 0;
    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);

    if (!vm_net_mock_get_object_u32_field(request, requestLen, "index", &index) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "index", &index8))
        index = index8;
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "Operate", &operate8))
        operate = operate8;
    operateIsSkill = vm_net_mock_battle_operate_is_skill(operate);
    operateIsOffensive = operate == 0 || operateIsSkill;
    if (operateIsSkill)
    {
        firstActionType = 1;
        type1EffectIndex = vm_net_mock_battle_operate_skill_effect(operate);
        skillMpPrepared = vm_net_mock_battle_prepare_skill_mp(operate,
                                                              &skillMpBefore,
                                                              &skillMpAfter,
                                                              &skillMpCost);
    }

    /*
     * Static/runtime Battle.cbm evidence now converges on one stronger claim:
     * - subtype-6 field "actioninfo" is not consumed as a naked byte array.
     * - the header reads at 0x05188E58/0x05188E64/0x05188E7A use the reader
     *   callback at reader+0x28, and runtime cross-reference shows that slot
     *   resolves to 0x01033AAD -> stream_read_i8_tagged.
     * - similarly, the child/value reads later in sub_6EB0 use the reader
     *   callback table's tagged u32/u16/string helpers, not raw direct bytes.
     *
     * Therefore the next parser-faithful experiment is to keep subtype-6 and
     * the same two-record player/enemy round shell, but encode the inner stream
     * with the repository's existing tagged sequence helpers instead of the old
     * hand-written raw 43-byte layout.
     */
    actorSlot = (u8)(index & 0xFFu);
    requestedTargetSlot = actorSlot;
    if (requestedTargetSlot == playerSlot || requestedTargetSlot > 5)
        requestedTargetSlot = enemySlot;
    if (g_mockBattleOperateSessionFinished != 0)
        g_mockBattleOperateSessionFinished = 0;
    terminalFollowup = false;
    bundleWholeRound = g_mockBattleOperateSessionArmed != 0 &&
                       operateIsOffensive &&
                       vm_net_mock_env_u32("CBE_BATTLE_BUNDLE_ROUND", 1) != 0;
    firstRecordActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_ACTOR_WIRE_SLOT",
                                                       playerSlot);
    firstRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_CHILD_FLAG", 0);
    counterRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    if (g_mockBattleAwaitingSettlement != 0)
    {
        (void)requestedTargetSlot;
        if (g_vm_net_mock_battle_settlement_sent_serial != g_mockBattleOperateSessionSerial)
            return vm_net_mock_build_battle_pending_settlement_response(out, outCap);
        return vm_net_mock_build_battle_case11_auto_off_response(out, outCap);
    }
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent == 0)
    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(g_vm_net_mock_battle_enemy_id_current);
        g_mockBattleEnemyHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
    }
    if (!terminalFollowup && g_mockBattleRoleHpCurrent == 0)
        g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP",
                                                        vm_net_mock_role_current_hp_for_battle());
    if (!terminalFollowup && g_mockBattlePendingEnemyTurn != 0 &&
        g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0)
    {
        u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                         enemySlot);
        u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                          playerSlot);
        if (counterActionType == 1)
        {
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                          counterActorWireSlot);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                           counterTargetWireSlot);
        }
        counterDamageValue = vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                                     g_mockBattleRoleHpCurrent);
        if (counterDamageValue == 0)
            counterDamageValue = 1;
        if (g_mockBattleRoleHpCurrent >= counterDamageValue)
            g_mockBattleRoleHpCurrent -= counterDamageValue;
        else
            g_mockBattleRoleHpCurrent = 0;
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
        counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
        counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
        g_mockBattlePendingEnemyTurn = 0;
        ++g_mockBattleOperateTurnCounter;
        return vm_net_mock_build_battle_single_action_response(out, outCap,
                                                               counterActionType,
                                                               counterActorWireSlot,
                                                               counterTargetWireSlot,
                                                               counterRecordChildFlag,
                                                               counterHpDelta,
                                                               counterRecordMpDelta);
    }
    if (terminalFollowup)
    {
        attackDamageValue = 0;
        counterDamageValue = 0;
        actionCount = 0;
    }
    else
    {
        attackDamageValue = operateIsSkill
                                ? vm_net_mock_battle_player_skill_damage_to_enemy(operate,
                                                                                 g_vm_net_mock_battle_enemy_id_current,
                                                                                 g_mockBattleEnemyHpCurrent)
                                : vm_net_mock_battle_player_damage_to_enemy(g_vm_net_mock_battle_enemy_id_current,
                                                                            g_mockBattleEnemyHpCurrent);
        if (attackDamageValue == 0)
            attackDamageValue = 1;
        if (g_mockBattleEnemyHpCurrent >= attackDamageValue)
            g_mockBattleEnemyHpCurrent -= attackDamageValue;
        else
            g_mockBattleEnemyHpCurrent = 0;
        allowCounterattack = bundleWholeRound && g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0;
        if (allowCounterattack)
        {
            counterDamageValue = vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                                         g_mockBattleRoleHpCurrent);
            if (counterDamageValue == 0)
                counterDamageValue = 1;
            if (g_mockBattleRoleHpCurrent >= counterDamageValue)
                g_mockBattleRoleHpCurrent -= counterDamageValue;
            else
                g_mockBattleRoleHpCurrent = 0;
        }
        battleEndsThisRound = (g_mockBattleEnemyHpCurrent == 0 || g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup)
    {
        attackHpDelta = vm_net_mock_battle_negative_delta_u32(attackDamageValue);
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
    }
    attackHpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_A", attackHpDelta);
    counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
    skillMpDelta = vm_net_mock_env_u32("CBE_BATTLE_SKILL_MP_VALUE_B",
                                       skillMpPrepared ? skillMpAfter :
                                                         vm_net_mock_battle_role_mp_current());
    firstRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        skillTeamRoleId = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_ROLE_ID",
                                              role ? role->roleId :
                                                     VM_NET_MOCK_ROLE_DEFAULT_ID);
        skillTeamHp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_HP",
                                          g_mockBattleRoleHpCurrent);
        skillTeamMp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_MP",
                                          skillMpDelta);
        skillTeamInfoEnabled = skillTeamRoleId != 0;
    }
    skillCostValueA = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_VALUE_A",
                                          g_mockBattleRoleHpCurrent);
    skillCostValueB = skillMpDelta;
    counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
        vm_net_mock_battle_commit_skill_mp(skillMpAfter);
    if (terminalFollowup)
    {
        if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 4);
        g_mockBattleOperateSessionFinished = 0;
        return pos;
    }
    memset(actionInfo, 0, sizeof(actionInfo));
    /*
     * The default wire slots come from the active battle-start flavor. For
     * subtype 5 scene-monster battles the player is wire 1; using wire 0 as the
     * actor makes offensive skills visibly target the player.
     */
    {
        u8 mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTOR_WIRE_SLOT",
                                                         firstRecordActorWireSlot);
        u8 mappedTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_TARGET_WIRE_SLOT",
                                                           requestedTargetSlot);

        if (firstActionType == 1)
        {
            mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT",
                                                         mappedActorWireSlot);
            mappedTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT",
                                                          mappedTargetWireSlot);
        }
        firstRecordWireActorUsed = mappedActorWireSlot;
        firstRecordWireTargetUsed = mappedTargetWireSlot;

        /* record 0 stays on the current live no-crash baseline. */
        if (!terminalFollowup)
        {
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, firstActionType,
                                                             mappedActorWireSlot,
                                                             mappedTargetWireSlot,
                                                             firstRecordChildFlag, attackHpDelta,
                                                             firstRecordMpDelta,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1Tail0 : 0,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1Tail1 : 0,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1Tail2 : 0))
                return 0;
            /*
             * Disabled by default. Runtime negatives showed that a separate
             * MP-cost action is still animated as a normal target update:
             * valueA=0 shows a 0 HP line and valueA=current HP shows a heal.
             * Keep this branch as an explicit experiment only.
             */
            if (skillCostActionEnabled &&
                operateIsSkill && skillMpPrepared && skillMpCost != 0 && firstActionType == 1)
            {
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, skillCostActionType,
                                                                 mappedActorWireSlot,
                                                                 mappedActorWireSlot,
                                                                 0, skillCostValueA,
                                                                 skillCostValueB,
                                                                 (skillCostActionType == 1 || skillCostActionType == 2) ? skillCostEffectIndex : 0,
                                                                 0, 0, 0))
                    return 0;
            }
        }

        /*
         * Plain player-vs-monster rounds stay in subtype 6. Do not arm subtype
         * 11 here: HandleServerBattleCmd(0x7BD0) treats 4/11 type=1 as the
         * auto-battle path and shows the auto battle UI.
         */
        if (allowCounterattack)
        {
            u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                             enemySlot);
            u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                              playerSlot);
            if (counterActionType == 1)
            {
                counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                              counterActorWireSlot);
                counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                               counterTargetWireSlot);
            }
            if (actionCount < 6)
                ++actionCount;
            else
                return 0;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, counterActionType,
                                                             counterActorWireSlot, counterTargetWireSlot,
                                                             counterRecordChildFlag,
                                                             counterHpDelta, counterRecordMpDelta,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1EffectIndex : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail0 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail1 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail2 : 0))
                return 0;
        }
        if (battleEndsThisRound)
        {
            u8 terminalActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTOR_WIRE_SLOT",
                                                              vm_net_mock_battle_terminal_actor_slot(playerSlot,
                                                                                                     enemySlot));
            if (actionCount < 6)
                ++actionCount;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, terminalActionType,
                                                             terminalActorWireSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
                return 0;
        }
    }

    if (battleEndsThisRound && vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos))
            return 0;
        ++responseObjectCount;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
    }
    if (!vm_net_mock_append_battle_action6_object_ex(out, outCap, &pos,
                                                    actionInfo, actionInfoLen,
                                                    actionCount,
                                                    skillTeamInfoEnabled,
                                                    skillTeamRoleId,
                                                    skillTeamHp,
                                                    skillTeamMp))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, (u8)responseObjectCount);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound)
    {
        vm_net_mock_battle_save_terminal_role_state("battle-operate");
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = 1;
    }
    else if (g_mockBattleOperateSessionArmed != 0 && operateIsOffensive && !bundleWholeRound)
    {
        g_mockBattlePendingEnemyTurn = 1;
    }
    printf("[info][network] mock_battle_operate index=%u operate=%u skill=%u action=%u actions=%u effect=%u actor=%u target=%u damage=%u enemyhp=%u rolehp=%u mpcost=%u valueB=%u teaminfo=%u:%u/%u costAction=%u costHp=%u costMp=%u mp=%u/%u resp=%u evidence=mmBattle:0x2B50/0x6EB0/0x6DBC\n",
           index, operate, operateIsSkill ? 1 : 0, firstActionType,
           actionCount,
           (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
           firstRecordWireActorUsed, firstRecordWireTargetUsed,
           attackDamageValue, g_mockBattleEnemyHpCurrent, g_mockBattleRoleHpCurrent,
           skillMpCost, firstRecordMpDelta,
           skillTeamInfoEnabled ? skillTeamRoleId : 0,
           skillTeamInfoEnabled ? skillTeamHp : 0,
           skillTeamInfoEnabled ? skillTeamMp : 0,
           (operateIsSkill && skillCostActionEnabled) ? skillCostActionType : 0,
           (operateIsSkill && skillCostActionEnabled) ? skillCostValueA : 0,
           (operateIsSkill && skillCostActionEnabled) ? skillCostValueB : 0,
           skillMpBefore, skillMpPrepared ? skillMpAfter : skillMpBefore, pos);
    vm_autotest_note("mock_battle_operate index=%u operate=%u skill=%u action=%u actions=%u effect=%u actor=%u target=%u damage=%u enemyhp=%u rolehp=%u mpcost=%u valueB=%u teaminfo=%u:%u/%u costAction=%u costHp=%u costMp=%u mp=%u/%u response=4/6 evidence=mmBattle:0x2B50/0x6EB0/0x6DBC\n",
                     index, operate, operateIsSkill ? 1 : 0, firstActionType,
                     actionCount,
                     (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                     firstRecordWireActorUsed, firstRecordWireTargetUsed,
                     attackDamageValue, g_mockBattleEnemyHpCurrent, g_mockBattleRoleHpCurrent,
                     skillMpCost, firstRecordMpDelta,
                     skillTeamInfoEnabled ? skillTeamRoleId : 0,
                     skillTeamInfoEnabled ? skillTeamHp : 0,
                     skillTeamInfoEnabled ? skillTeamMp : 0,
                     (operateIsSkill && skillCostActionEnabled) ? skillCostActionType : 0,
                     (operateIsSkill && skillCostActionEnabled) ? skillCostValueA : 0,
                     (operateIsSkill && skillCostActionEnabled) ? skillCostValueB : 0,
                     skillMpBefore, skillMpPrepared ? skillMpAfter : skillMpBefore);
    return pos;
}

static u32 vm_net_mock_build_battle_operate_response_fallback(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 index = 0;
    u32 operate = 0;
    u8 index8 = 0;
    u8 operate8 = 0;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u32 responseObjectCount = 1;
    u8 requestKind = 0;
    u8 requestSubtype = 0;
    u8 actorSlot = 0;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 requestedTargetSlot = enemySlot;
    u8 firstRecordWireActorUsed = 0;
    u8 firstRecordWireTargetUsed = 0;
    u32 attackDamageValue = 12;
    u32 counterDamageValue = 10;
    u32 attackHpDelta = 0;
    u32 counterHpDelta = 0;
    u8 actionCount = 1;
    bool bundleWholeRound = false;
    u8 firstRecordActorWireSlot = 0;
    u8 firstRecordChildFlag = 0;
    u8 counterRecordChildFlag = 0;
    u32 firstRecordMpDelta = 0;
    u32 counterRecordMpDelta = 0;
    bool battleEndsThisRound = false;
    bool allowCounterattack = false;
    bool terminalFollowup = false;
    bool operateIsSkill = false;
    bool operateIsOffensive = false;
    bool skillMpPrepared = false;
    u32 skillMpCost = 0;
    u32 skillMpBefore = 0;
    u32 skillMpAfter = 0;
    u32 skillMpDelta = 0;
    bool skillTeamInfoEnabled = false;
    u32 skillTeamRoleId = 0;
    u32 skillTeamHp = 0;
    u32 skillTeamMp = 0;
    u32 skillCostValueA = 0;
    u32 skillCostValueB = 0;
    bool skillCostActionEnabled = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_ENABLED", 0) != 0;
    u8 skillCostActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_TYPE", 0);
    u32 skillCostEffectIndex = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_EFFECT_INDEX", 0);
    u8 actionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 0);
    u8 firstActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTION_TYPE", actionType);
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", actionType);
    u8 terminalActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTION_TYPE", 3);
    u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_is_battle_operate_request_relaxed(request, requestLen))
    {
        if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &requestKind, &requestSubtype) ||
            requestKind != 4 || requestSubtype != 2)
            return 0;
    }
    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);

    if (!vm_net_mock_get_object_u32_field(request, requestLen, "index", &index) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "index", &index8))
        index = index8;
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "Operate", &operate8))
        operate = operate8;
    operateIsSkill = vm_net_mock_battle_operate_is_skill(operate);
    operateIsOffensive = operate == 0 || operateIsSkill;
    if (operateIsSkill)
    {
        firstActionType = 1;
        type1EffectIndex = vm_net_mock_battle_operate_skill_effect(operate);
        skillMpPrepared = vm_net_mock_battle_prepare_skill_mp(operate,
                                                              &skillMpBefore,
                                                              &skillMpAfter,
                                                              &skillMpCost);
    }

    actorSlot = (u8)(index & 0xFFu);
    requestedTargetSlot = actorSlot;
    if (requestedTargetSlot == playerSlot || requestedTargetSlot > 5)
        requestedTargetSlot = enemySlot;
    if (g_mockBattleOperateSessionFinished != 0)
        g_mockBattleOperateSessionFinished = 0;
    terminalFollowup = false;
    bundleWholeRound = g_mockBattleOperateSessionArmed != 0 &&
                       operateIsOffensive &&
                       vm_net_mock_env_u32("CBE_BATTLE_BUNDLE_ROUND", 1) != 0;
    firstRecordActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_ACTOR_WIRE_SLOT",
                                                       playerSlot);
    firstRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_CHILD_FLAG", 0);
    counterRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    if (g_mockBattleAwaitingSettlement != 0)
    {
        (void)requestedTargetSlot;
        if (g_vm_net_mock_battle_settlement_sent_serial != g_mockBattleOperateSessionSerial)
            return vm_net_mock_build_battle_pending_settlement_response(out, outCap);
        return vm_net_mock_build_battle_case11_auto_off_response(out, outCap);
    }
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent == 0)
    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(g_vm_net_mock_battle_enemy_id_current);
        g_mockBattleEnemyHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
    }
    if (!terminalFollowup && g_mockBattleRoleHpCurrent == 0)
        g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP",
                                                        vm_net_mock_role_current_hp_for_battle());
    if (!terminalFollowup && g_mockBattlePendingEnemyTurn != 0 &&
        g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0)
    {
        u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                         enemySlot);
        u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                          playerSlot);
        if (counterActionType == 1)
        {
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                          counterActorWireSlot);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                           counterTargetWireSlot);
        }
        counterDamageValue = vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                                     g_mockBattleRoleHpCurrent);
        if (counterDamageValue == 0)
            counterDamageValue = 1;
        if (g_mockBattleRoleHpCurrent >= counterDamageValue)
            g_mockBattleRoleHpCurrent -= counterDamageValue;
        else
            g_mockBattleRoleHpCurrent = 0;
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
        counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
        counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
        g_mockBattlePendingEnemyTurn = 0;
        ++g_mockBattleOperateTurnCounter;
        return vm_net_mock_build_battle_single_action_response(out, outCap,
                                                               counterActionType,
                                                               counterActorWireSlot,
                                                               counterTargetWireSlot,
                                                               counterRecordChildFlag,
                                                               counterHpDelta,
                                                               counterRecordMpDelta);
    }
    if (terminalFollowup)
    {
        attackDamageValue = 0;
        counterDamageValue = 0;
        actionCount = 0;
    }
    else
    {
        attackDamageValue = operateIsSkill
                                ? vm_net_mock_battle_player_skill_damage_to_enemy(operate,
                                                                                 g_vm_net_mock_battle_enemy_id_current,
                                                                                 g_mockBattleEnemyHpCurrent)
                                : vm_net_mock_battle_player_damage_to_enemy(g_vm_net_mock_battle_enemy_id_current,
                                                                            g_mockBattleEnemyHpCurrent);
        if (attackDamageValue == 0)
            attackDamageValue = 1;
        if (g_mockBattleEnemyHpCurrent >= attackDamageValue)
            g_mockBattleEnemyHpCurrent -= attackDamageValue;
        else
            g_mockBattleEnemyHpCurrent = 0;
        allowCounterattack = bundleWholeRound && g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0;
        if (allowCounterattack)
        {
            counterDamageValue = vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                                         g_mockBattleRoleHpCurrent);
            if (counterDamageValue == 0)
                counterDamageValue = 1;
            if (g_mockBattleRoleHpCurrent >= counterDamageValue)
                g_mockBattleRoleHpCurrent -= counterDamageValue;
            else
                g_mockBattleRoleHpCurrent = 0;
        }
        battleEndsThisRound = (g_mockBattleEnemyHpCurrent == 0 || g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup)
    {
        attackHpDelta = vm_net_mock_battle_negative_delta_u32(attackDamageValue);
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
    }
    attackHpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_A", attackHpDelta);
    counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
    skillMpDelta = vm_net_mock_env_u32("CBE_BATTLE_SKILL_MP_VALUE_B",
                                       skillMpPrepared ? skillMpAfter :
                                                         vm_net_mock_battle_role_mp_current());
    firstRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        skillTeamRoleId = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_ROLE_ID",
                                              role ? role->roleId :
                                                     VM_NET_MOCK_ROLE_DEFAULT_ID);
        skillTeamHp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_HP",
                                          g_mockBattleRoleHpCurrent);
        skillTeamMp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_MP",
                                          skillMpDelta);
        skillTeamInfoEnabled = skillTeamRoleId != 0;
    }
    skillCostValueA = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_VALUE_A",
                                          g_mockBattleRoleHpCurrent);
    skillCostValueB = skillMpDelta;
    counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
        vm_net_mock_battle_commit_skill_mp(skillMpAfter);
    if (terminalFollowup)
    {
        if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
            !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 4);
        g_mockBattleOperateSessionFinished = 0;
        return pos;
    }
    memset(actionInfo, 0, sizeof(actionInfo));
    {
        u8 mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTOR_WIRE_SLOT",
                                                         firstRecordActorWireSlot);
        u8 mappedTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_TARGET_WIRE_SLOT",
                                                          requestedTargetSlot);
        if (firstActionType == 1)
        {
            mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT",
                                                         mappedActorWireSlot);
            mappedTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT",
                                                          mappedTargetWireSlot);
        }
        firstRecordWireActorUsed = mappedActorWireSlot;
        firstRecordWireTargetUsed = mappedTargetWireSlot;
        if (!terminalFollowup)
        {
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, firstActionType,
                                                             mappedActorWireSlot,
                                                             mappedTargetWireSlot,
                                                             firstRecordChildFlag, attackHpDelta,
                                                             firstRecordMpDelta,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1Tail0 : 0,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1Tail1 : 0,
                                                             (firstActionType == 1 || firstActionType == 2) ? type1Tail2 : 0))
            {
                return 0;
            }
            /*
             * Disabled by default. Runtime negatives showed that a separate
             * MP-cost action is still animated as a normal target update:
             * valueA=0 shows a 0 HP line and valueA=current HP shows a heal.
             * Keep this branch as an explicit experiment only.
             */
            if (skillCostActionEnabled &&
                operateIsSkill && skillMpPrepared && skillMpCost != 0 && firstActionType == 1)
            {
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, skillCostActionType,
                                                                 mappedActorWireSlot,
                                                                 mappedActorWireSlot,
                                                                 0, skillCostValueA,
                                                                 skillCostValueB,
                                                                 (skillCostActionType == 1 || skillCostActionType == 2) ? skillCostEffectIndex : 0,
                                                                 0, 0, 0))
                {
                    return 0;
                }
            }
        }
        if (allowCounterattack)
        {
            u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                             enemySlot);
            u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                              playerSlot);
            if (counterActionType == 1)
            {
                counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                              counterActorWireSlot);
                counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                               counterTargetWireSlot);
            }
            if (actionCount < 6)
                ++actionCount;
            else
                return 0;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, counterActionType,
                                                             counterActorWireSlot, counterTargetWireSlot,
                                                             counterRecordChildFlag,
                                                             counterHpDelta, counterRecordMpDelta,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1EffectIndex : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail0 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail1 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail2 : 0))
            {
                return 0;
            }
        }
        if (battleEndsThisRound)
        {
            u8 terminalActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTOR_WIRE_SLOT",
                                                              vm_net_mock_battle_terminal_actor_slot(playerSlot,
                                                                                                     enemySlot));
            if (actionCount < 6)
                ++actionCount;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, terminalActionType,
                                                             terminalActorWireSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
            {
                return 0;
            }
        }
    }

    if (battleEndsThisRound && vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos))
            return 0;
        ++responseObjectCount;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
    }
    if (!vm_net_mock_append_battle_action6_object_ex(out, outCap, &pos,
                                                    actionInfo, actionInfoLen,
                                                    actionCount,
                                                    skillTeamInfoEnabled,
                                                    skillTeamRoleId,
                                                    skillTeamHp,
                                                    skillTeamMp))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, (u8)responseObjectCount);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound)
    {
        vm_net_mock_battle_save_terminal_role_state("battle-operate-fallback");
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = 1;
    }
    else if (g_mockBattleOperateSessionArmed != 0 && operateIsOffensive && !bundleWholeRound)
    {
        g_mockBattlePendingEnemyTurn = 1;
    }
    return pos;
}

static u32 vm_net_mock_build_battle_operate_response_raw82(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 len = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);

    if (len != 0)
    {
        return len;
    }

    return 0;
}

static bool vm_net_mock_append_battle_status7_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHp = g_mockBattleRoleHpMax != 0 ? g_mockBattleRoleHpCurrent :
                 (role ? role->hp : VM_NET_MOCK_ROLE_DEFAULT_HP);
    u32 roleMp = role ? role->mp : VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 statusExp = 0;
    u32 totalExp = role ? role->exp : 0;
    u32 statusCurExp = vm_net_mock_role_next_level_start_exp(totalExp);
    u32 statusLastExp = vm_net_mock_role_last_level_exp(totalExp);
    u32 statusPercentExp = vm_net_mock_role_exp_percent(totalExp);
    u32 statusGold = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 statusLevel = role ? role->level : 1;
    u32 recoverHp = vm_net_mock_env_u32_if_set("CBE_BATTLE_RECOVER_HP", 0);
    u32 recoverMp = vm_net_mock_battle_recover_mp_value();
    u32 dropItemId = 0;
    u16 dropSeq = 0;
    bool dropGranted = false;
    u8 itemInfo[128];
    u32 itemInfoLen = 0;
    u32 itemInfoRows = 0;
    u32 applyRewardExp = 0;
    u32 displayExpGain = 0;
    bool victory = g_mockBattleEnemyHpCurrent == 0 && roleHp > 0;
    bool rewardAlreadyGranted = false;
    bool mpRecoveryApplied = false;

    if (victory)
    {
        rewardAlreadyGranted = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial);
        applyRewardExp = vm_net_mock_battle_grant_reward_once(&dropItemId,
                                                              &dropSeq,
                                                              &dropGranted);
        displayExpGain = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial)
                             ? g_vm_net_mock_battle_rewarded_exp
                             : applyRewardExp;
    }
    if (role != NULL)
    {
        u32 rewardGold = (victory && !rewardAlreadyGranted)
                              ? vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_GOLD",
                                                           vm_net_mock_battle_reward_gold_for_enemy(g_vm_net_mock_battle_enemy_id_current))
                              : 0;
        roleMp = vm_net_mock_battle_apply_mp_recovery_once(role, roleMp, recoverMp,
                                                           &mpRecoveryApplied);
        vm_net_mock_role_apply_battle_settlement(roleHp, roleMp, applyRewardExp, rewardGold,
                                                 &statusLastExp, &statusCurExp,
                                                 &statusPercentExp, &statusLevel,
                                                 &statusGold, &roleHp, &roleMp);
        statusExp = role->exp;
    }
    else
    {
        statusExp = totalExp + applyRewardExp;
    }
    statusLastExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_LAST_EXP", statusLastExp);
    statusCurExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_CUR_EXP", statusCurExp);
    statusPercentExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_PERCENT_EXP",
                                                  statusPercentExp);
    statusLevel = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_LEVEL", statusLevel);
    memset(itemInfo, 0, sizeof(itemInfo));
    if (dropGranted && role != NULL)
    {
        if (!vm_net_mock_build_battle_settle_iteminfo_blob(itemInfo, sizeof(itemInfo),
                                                          &itemInfoLen, &itemInfoRows,
                                                          role->roleId, dropItemId))
        {
            return false;
        }
    }
    printf("[info][network] mock_battle_settle enemy=%u victory=%u exp_gain=%u exp_total=%u gold=%u level=%u recover=%u/%u drop=%u seq=%u item_rows=%u iteminfo_len=%u\n",
           g_vm_net_mock_battle_enemy_id_current,
           victory ? 1 : 0,
           displayExpGain,
           statusExp,
           statusGold,
           statusLevel,
           recoverHp,
           recoverMp,
           dropGranted ? dropItemId : 0,
           dropSeq,
           itemInfoRows,
           itemInfoLen);
    vm_autotest_note("mock_battle_settle enemy=%u victory=%u exp_gain=%u exp_total=%u gold=%u level=%u hp=%u mp=%u recover=%u/%u recovered=%u drop=%u seq=%u item_rows=%u iteminfo_len=%u\n",
                     g_vm_net_mock_battle_enemy_id_current,
                     victory ? 1 : 0,
                     displayExpGain,
                     statusExp,
                     statusGold,
                     statusLevel,
                     roleHp,
                     roleMp,
                     recoverHp,
                     recoverMp,
                     mpRecoveryApplied ? 1 : 0,
                     dropGranted ? dropItemId : 0,
                     dropSeq,
                     itemInfoRows,
                     itemInfoLen);

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 7, &objectStart))
        return false;
    /*
     * Battle.cbm HandleBattleSettleMsg(0x743C) reads exp before lastexp,
     * curexp, and persentexp. Main CBE property rendering uses actor+0xB0
     * as total EXP, lastexp as the level-start threshold, and curexp as the
     * next level-start threshold. persentexp is parsed through the integer
     * getter too, then narrowed into a separate progress cache.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "exp", statusExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", statusLastExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", statusCurExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", statusPercentExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energy", 100))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energymax", 100))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "gold", statusGold))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "level", statusLevel))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "bagstatus", 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "hp", recoverHp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "mp", recoverMp))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "itemnum", itemInfoRows > 255 ? 255 : (u8)itemInfoRows))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo",
                                    itemInfoLen ? itemInfo : NULL,
                                    (u16)itemInfoLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_subtype8_object(u8 *out, u32 outCap, u32 *pos)
{
    static const u8 terminalInfo[12] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
    };
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 8, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "info", terminalInfo, sizeof(terminalInfo)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case4_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case9_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 9, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case11_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, pos, 0);
}

static u32 vm_net_mock_build_battle_pending_settlement_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_battle_status7_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
        return 0;
    ++objectCount;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
    vm_autotest_note("mock_battle_pending_settlement serial=%u objects=%u response=4/7+4/11+4/9 evidence=mmBattle:0x7BD0/0x743C\n",
                     g_mockBattleOperateSessionSerial,
                     objectCount);
    return pos;
}

static u32 vm_net_mock_build_battle_auto12_cancel_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u8 kind = 0;
    u8 subtype = 0;
    u32 pos = 5;
    u32 objectCount = 0;

    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 4 || subtype != 12)
        return 0;
    if (g_mockBattleOperateSessionArmed == 0 && !vm_net_mock_current_screen_is_battle())
        return 0;

    if (outCap < pos ||
        !vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, 0))
        return 0;
    ++objectCount;

    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleOperateSessionFinished = 0;
    if (g_mockBattleEnemyHpCurrent == 0)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleAwaitingSettlement = 1;
    }
    vm_net_mock_finish_wt_packet(out, pos, (u8)objectCount);
    return pos;
}

static u32 vm_net_mock_build_challenge_interaction_response(const u8 *request, u32 requestLen,
                                                            u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    const u8 *moveInfo = NULL;
    u16 moveInfoLen = 0;
    bool hasMoveinfo = false;
    u8 battleInfo[160];
    u32 battleInfoLen = 0;
    u32 id = 0;
    u32 requestedEnemyId = 0;
    u32 enemyWireId = 0;
    u32 enemyTable = 0;
    u32 enemyTableIds[4] = {0};
    u32 index = 0;
    u32 posx = 0;
    u32 posy = 0;
    u32 sceneMonsterIndex = 0;
    u32 sceneMonsterPosX = 0;
    u32 sceneMonsterPosY = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleId = 0;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    bool useSceneMonsterStart = playerOnRight &&
                                vm_net_mock_env_u8("CBE_BATTLE_SCENE_MONSTER_START", 1) != 0;
    u8 battleStartSubtype = useSceneMonsterStart ? 5 : 10;
    bool seedSceneMonsterMoveinfo = useSceneMonsterStart &&
                                    vm_net_mock_env_u8("CBE_BATTLE_SCENE_MONSTER_MOVEINFO", 1) != 0;
    bool prefillEnemyTemplate = false;
    bool prefillPlayerTemplate = false;
    u32 responseObjectCount = 1;
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();

    if (outCap < pos || !vm_net_mock_is_challenge_interaction_request(request, requestLen))
        return 0;
    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    roleId = vm_net_mock_env_u32("CBE_BATTLE_ROLE_ID",
                                 role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID);
    roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", roleHpDefault);
    roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleMaxHpDefault);
    roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", roleMpDefault);
    roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMaxMpDefault);
    if (roleMaxHp < roleMaxHpDefault)
        roleMaxHp = roleMaxHpDefault;
    if (roleMaxMp < roleMaxMpDefault)
        roleMaxMp = roleMaxMpDefault;
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;
    hasMoveinfo = vm_net_mock_get_object_blob_field(request, requestLen, "moveinfo", &moveInfo, &moveInfoLen);
    if (hasMoveinfo)
        (void)vm_net_mock_snapshot_current_player_pos("moveinfo-upload-combo");
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "index", &index);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);

    sceneMonsterIndex = index;
    sceneMonsterPosX = posx;
    sceneMonsterPosY = posy;
    requestedEnemyId = vm_net_mock_normalize_battle_enemy_id(id);
    g_vm_net_mock_battle_enemy_id_current = requestedEnemyId;
    enemyWireId = vm_net_mock_resolve_battle_enemy_id(requestedEnemyId, &enemyTable, enemyTableIds);
    if (seedSceneMonsterMoveinfo)
    {
        /*
         * net_handle_actor_move_info case 2 locates a scene node by actorId
         * only. The bundled SCE has multiple 毒泥怪 rows with actorId 105, so
         * use the same first-active row for both moveinfo and subtype-5 battle
         * start until the unique live monster instance id contract is recovered.
         */
        (void)vm_net_mock_select_scene_actor_moveinfo_target(requestedEnemyId,
                                                            &sceneMonsterIndex,
                                                            &sceneMonsterPosX,
                                                            &sceneMonsterPosY);
    }
    if (playerOnRight && !useSceneMonsterStart)
        enemyWireId = requestedEnemyId;
    prefillEnemyTemplate = !playerOnRight &&
                           vm_net_mock_env_u8("CBE_BATTLE_PREFILL_ENEMY_TEMPLATE", 1) != 0 &&
                           requestedEnemyId != 0 &&
                           (enemyWireId != requestedEnemyId || requestedEnemyId != id);
    if (prefillEnemyTemplate)
        enemyWireId = requestedEnemyId;
    prefillPlayerTemplate = playerOnRight &&
                            vm_net_mock_env_u8("CBE_BATTLE_PREFILL_PLAYER_TEMPLATE", 1) != 0 &&
                            roleId != 0;
    if (useSceneMonsterStart)
    {
        battleInfoLen = vm_net_mock_build_battle_scene_start_info_blob(battleInfo, sizeof(battleInfo),
                                                                       sceneMonsterIndex,
                                                                       sceneMonsterPosX,
                                                                       sceneMonsterPosY,
                                                                       roleId);
    }
    else
    {
        battleInfoLen = vm_net_mock_build_battle_start_info_blob(battleInfo, sizeof(battleInfo),
                                                                 roleId, enemyWireId,
                                                                 playerOnRight);
    }
    if (battleInfoLen == 0 || battleInfoLen > 0xffff)
        return 0;

    if (prefillEnemyTemplate)
    {
        if (!vm_net_mock_append_battle_enemy_template_prefill_object(out, outCap, &pos, requestedEnemyId))
            return 0;
        ++responseObjectCount;
    }
    if (prefillPlayerTemplate)
    {
        u8 playerTemplateByte34 = vm_net_mock_env_u8("CBE_BATTLE_PLAYER_TEMPLATE_BYTE34", 1);
        u8 playerTemplateByte35 = vm_net_mock_env_u8("CBE_BATTLE_PLAYER_TEMPLATE_BYTE35", 0);
        const char *playerTemplateName = vm_net_mock_env_str("CBE_BATTLE_PLAYER_TEMPLATE_NAME", roleName);

        if (!vm_net_mock_append_battle_template_prefill_object_ex(out, outCap, &pos,
                                                                  roleId,
                                                                  playerTemplateName,
                                                                  playerTemplateByte34,
                                                                  playerTemplateByte35,
                                                                  roleHp,
                                                                  roleMaxHp,
                                                                  roleMp,
                                                                  roleMaxMp))
            return 0;
        ++responseObjectCount;
    }
    if (seedSceneMonsterMoveinfo)
    {
        if (!vm_net_mock_append_scene_monster_moveinfo2_object(out, outCap, &pos,
                                                              requestedEnemyId,
                                                              sceneMonsterPosX,
                                                              sceneMonsterPosY))
            return 0;
        ++responseObjectCount;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, battleStartSubtype, &objectStart))
        return 0;
    /*
     * In the default player-on-right layout, side=1 makes wire slot 0 remap to
     * the right-side role record and wire slot 1 remap to the left monster.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "side", battleSide))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo", battleInfo, (u16)battleInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (hasMoveinfo)
    {
        if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(out, outCap, &pos))
            return 0;
        ++responseObjectCount;
    }
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    g_mockBattleOperateSessionArmed = 1;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    g_mockBattleSceneMonsterStartActive = useSceneMonsterStart ? 1 : 0;
    g_mockBattleOperateTurnCounter = 0;
    ++g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = 0;
    g_vm_net_mock_battle_rewarded_drop_item = 0;
    g_vm_net_mock_battle_rewarded_drop_seq = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_mockBattleRoleHpCurrent = roleHp;
    g_mockBattleRoleHpMax = roleMaxHp;
    if (g_mockBattleRoleHpMax < g_mockBattleRoleHpCurrent)
        g_mockBattleRoleHpMax = g_mockBattleRoleHpCurrent;
    g_mockBattleRoleMpCurrent = roleMp;
    g_mockBattleRoleMpMax = roleMaxMp;
    if (g_mockBattleRoleMpMax < g_mockBattleRoleMpCurrent)
        g_mockBattleRoleMpMax = g_mockBattleRoleMpCurrent;
    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(requestedEnemyId);
        g_mockBattleEnemyHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
        g_mockBattleEnemyHpMax = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", g_mockBattleEnemyHpCurrent);
        if (g_mockBattleEnemyHpMax < g_mockBattleEnemyHpCurrent)
            g_mockBattleEnemyHpMax = g_mockBattleEnemyHpCurrent;
        printf("[info][network] mock_challenge_battle_start id=%u requested=%u rolehp=%u/%u rolemp=%u/%u enemyhp=%u/%u enemymp=%u subtype=%u side=%u scene_start=%u index=%u pos=(%u,%u)\n",
               id, requestedEnemyId,
               g_mockBattleRoleHpCurrent,
               g_mockBattleRoleHpMax,
               g_mockBattleRoleMpCurrent,
               g_mockBattleRoleMpMax,
               g_mockBattleEnemyHpCurrent,
               g_mockBattleEnemyHpMax,
               vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp),
               battleStartSubtype, battleSide, useSceneMonsterStart ? 1 : 0,
               sceneMonsterIndex, sceneMonsterPosX, sceneMonsterPosY);
        vm_autotest_note("mock_challenge_battle_start id=%u requested=%u wire=%u level=%u hp=%u/%u rolemp=%u/%u enemymp=%u atk=%u def=%u exp=%u gold=%u index=%u pos=(%u,%u) reqIndex=%u reqPos=(%u,%u) subtype=%u side=%u scene_start=%u table=%08x ids=%u/%u/%u/%u\n",
                         id, requestedEnemyId, enemyWireId,
                         stats.level,
                         g_mockBattleEnemyHpCurrent,
                         g_mockBattleEnemyHpMax,
                         g_mockBattleRoleMpCurrent,
                         g_mockBattleRoleMpMax,
                         vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_ATTACK", stats.attack),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_DEFENSE", stats.defense),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_EXP", stats.exp),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_GOLD", stats.gold),
                         sceneMonsterIndex, sceneMonsterPosX, sceneMonsterPosY,
                         index, posx, posy,
                         battleStartSubtype, battleSide, useSceneMonsterStart ? 1 : 0,
                         enemyTable, enemyTableIds[0], enemyTableIds[1],
                         enemyTableIds[2], enemyTableIds[3]);
    }
    return pos;
}

static u32 vm_net_mock_build_special_scene_interaction_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 id = 0;
    u32 index = 0;
    u32 posx = 0;
    u32 posy = 0;
    const char *scene = vm_net_mock_current_scene_name();
    const char sourceScene01[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_01.sce */
    const char sourceScene03[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_03.sce */
    const char targetScene02[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_02.sce */
    const char targetScene04[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_04.sce */
    const char *targetScene = NULL;
    u16 targetX = 0;
    u16 targetY = 0;
    bool saveImmediately = false;

    if (outCap < pos || !vm_net_mock_is_challenge_interaction_request(request, requestLen))
        return 0;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "index", &index);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);

    /*
     * Runtime evidence on 2026-06-13: id=105 is also used by monsters. A broad
     * id/index/posy gate steals battle challenges, so only answer the known
     * portal rectangles recovered from the same-class scene table.
     */
    if (id == 105 && index == 4 &&
        posx >= 208 && posx <= 256 && posy >= 432 && posy <= 448 &&
        strcmp(scene, sourceScene01) == 0)
    {
        targetScene = targetScene02;
        targetX = 80;
        targetY = 60;
        saveImmediately = true;
    }
    else if (id == 4 && index == 4 &&
             posx >= 160 && posx <= 240 && posy >= 553 && posy <= 570 &&
             strcmp(scene, sourceScene03) == 0)
    {
        targetScene = targetScene04;
        targetX = 42;
        targetY = 60;
    }
    else
    {
        return 0;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, false, false, 0, targetScene, targetX, targetY))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (saveImmediately)
        vm_net_mock_save_player_pos_state(targetScene, targetX, targetY, "special-scene-interaction");
    else
        vm_net_mock_mark_pending_scene_pos_save(targetScene, targetX, targetY, "special-scene-interaction");
    return pos;
}

static u32 vm_net_mock_build_scene_interaction_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u32 page5Index = 0;
    u32 page6Index = 0;
    u32 totalItems = 0;
    u32 pageRows = 0;
    u32 itemInfoLen = 0;
    char page5Ids[160];
    bool needActorOther = false;
    bool needFb11 = false;
    bool needFb4 = false;
    bool needBooks = false;
    u8 fb4Type = 1;

    if (outCap < pos || !vm_net_mock_is_scene_interaction_followup_request(request, requestLen))
        return 0;
    /*
     * The NPC dialog / mmShop init path can batch the 14/14,14/4,14/5,14/6
     * requests into one WT packet.  Answer the whole family in the same response:
     * mmShop:0x9DE increments its local completion counter across these objects
     * and closes loading after all four are seen.
     */
    needActorOther = vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10);
    needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);
    page5Index = vm_net_mock_get_shop_page_index_in_request(request, requestLen, 5, 0);
    page6Index = vm_net_mock_get_shop_page_index_in_request(request, requestLen, 6, 0);

    if (needActorOther)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (needFb11)
    {
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (needFb4)
    {
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, ""))
            return 0;
        ++objectCount;
    }
    if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (!vm_net_mock_append_shop_open_status14_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_shop_money4_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_shop_catalog_page_object(out, outCap, &pos, 5, page5Index,
                                                    &totalItems, &pageRows, &itemInfoLen))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_shop_empty_page14_object(out, outCap, &pos, 6))
        return 0;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_netMockShop17ListPending = 1;
    vm_net_mock_format_shop_page_ids(page5Index, 8, page5Ids, sizeof(page5Ids));
    printf("[info][network] mock_shop_scene_interaction_combo page5=%u page6=%u total=%u rows=%u iteminfo_len=%u ids=%s actorOther=%u fb11=%u fb4=%u books=%u\n",
           page5Index,
           page6Index,
           totalItems,
           pageRows,
           itemInfoLen,
           page5Ids,
           needActorOther ? 1 : 0,
           needFb11 ? 1 : 0,
           needFb4 ? 1 : 0,
           needBooks ? 1 : 0);
    vm_autotest_note("mock_shop_scene_interaction_combo page5=%u page6=%u items_total=%u page_rows=%u iteminfo_len=%u actorOther=%u fb11=%u fb4=%u books=%u evidence=runtime:npc-buy-shop-family mmShop:0x1038/0x618/0x9DE\n",
                     page5Index,
                     page6Index,
                     totalItems,
                     pageRows,
                     itemInfoLen,
                     needActorOther ? 1 : 0,
                     needFb11 ? 1 : 0,
                     needFb4 ? 1 : 0,
                     needBooks ? 1 : 0);
    return pos;
}

static bool vm_net_mock_append_info_banner_clear12_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly,
                                                               bool preferSceneNpcOther)
{
    /*
     * In the first scene-enter dispatch window we have a confirmed practical
     * limit of 10 objects.  Keep the skill/book pair plus empty 6/1 taskinfo
     * in this window; omitting either causes the client to immediately request
     * the large 12/1+7/42+6/*+2/10+25/5 follow-up after 30/1 closes the gate.
     */
    if (includeSkillBooks)
    {
        u8 added = 0;
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, pos, &added))
            return false;
        *objectCount = (u8)(*objectCount + added);
    }

    if (!vm_net_mock_append_taskinfo_empty1_object(out, outCap, pos))
        return false;
    *objectCount += 1;

    /*
     * Case 13 loops over six tasktype slots. Each slot is a tagged i8 type id
     * followed by a len16 C string; use raw field payload so the stream cursor
     * starts directly at the first record.
     */
    if (includeTaskLists)
    {
        if (!vm_net_mock_append_tasktypes_empty13_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_taskaction_empty14_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    if (includeActorOther)
    {
        if (preferSceneNpcOther || vm_net_mock_env_u32("CBE_SCENE_NPC_OTHERINFO", 0) != 0)
        {
            const char *scene = vm_net_mock_current_scene_name();
            u32 npcCount = 0;
            u32 otherInfoLen = 0;
            if (!vm_net_mock_append_actor_other_scene_npcs10_object(out, outCap, pos,
                                                                    scene,
                                                                    preferSceneNpcOther,
                                                                    &npcCount, &otherInfoLen))
                return false;
        }
        else if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, pos))
        {
            return false;
        }
        *objectCount += 1;
    }

    if (includeInfoBanner)
    {
        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    /*
     * Earlier experiments showed the 27/12+27/11 family suppresses the large
     * WT49 follow-up, while 27/11 alone does not set the needed fb state. Pair
     * 27/12 and 27/11 with 27/4 ready/finalize so the gate is restored in the
     * same follow-up scan.
     */
    if (includeFbTargetClear)
    {
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1, ""))
            return false;
        *objectCount += 1;
    }
    else if (includeFbTargetSeedOnly)
    {
        /*
         * Hypothesis under test: after the requested WT49 resource/task/other
         * family is answered, run the complete fb-target trio in one follow-up
         * scan. 27/11 alone leaves sceneGateA at 0, but 27/4 immediately after
         * it restores the gate pair; use a non-empty info string so case 27/4
         * also exercises its scene-object info side effects.
         */
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1,
                                                        vm_net_mock_fb_target_info_text()))
            return false;
        *objectCount += 1;
    }

    return true;
}

static u32 vm_net_mock_build_scene_resource_followup_response(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 actorOtherRequestType = 0;
    bool includeSkillBooks = false;
    bool keepBusinessGate = false;
    bool appendSceneRoomNpcAfterEnter = false;
    bool hasActorOtherRequestType = false;
    bool sceneSupportsActorOtherNpcSeed = false;
    bool actorOtherNpcSeedInFollowup = false;
    bool preferActorOtherNpcRows = false;
    bool probeActorOtherNpcInFollowup = false;
    bool sceneNpcInfo11SeedInFollowup = false;
    bool sceneNpcActorInfoSeedInFollowup = false;
    u8 sceneNpcInfo11Num = 0;
    u32 sceneNpcInfo11Len = 0;
    u32 sceneNpcActorInfoLen = 0;
    u32 sceneNpcActorId = 0;
    const char *currentScene = NULL;
    if (outCap < pos || !vm_net_mock_is_scene_resource_followup_request(request, requestLen))
        return 0;

    currentScene = vm_net_mock_current_scene_name();
    includeSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                        vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    hasActorOtherRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "Type", &actorOtherRequestType);
    sceneSupportsActorOtherNpcSeed = vm_net_mock_scene_supports_actor_other_npc_seed(currentScene);
    /*
     * Runtime evidence from the latest manual runs:
     * - keepBusinessGate=1 emits only trailing 30/2 ack and never reaches
     *   LoadSceneDataFromStream(0x01006204) or ParseMinfoAndSpawnNPCs(0x010159DA).
     * - Without those parsers, scene-owned NPC resources never materialize.
     *
     * Default back to real 30/1 scene enter so the client can load the scene
     * and drive NPC spawning through normal scene-resource callbacks. Keep the
     * env knob for quick rollback while we validate downstream HUD effects.
     */
    keepBusinessGate = includeSkillBooks &&
                       vm_net_mock_env_u8("CBE_FIRST_SCENE_KEEP_BUSINESS_GATE", 0) != 0;
    /*
     * The client's standalone 2/10 Type=1 request arrives after the business
     * scene dispatch gate closes, so it is decoded into the shared container
     * but does not create visible scene nodes.  A first-scene follow-up seed is
     * useful as an explicit probe while the gate is still open, but latest
     * runtime evidence showed the default-on Type101 row can create a scene
     * node with a null draw callback (pc=0, lr=01014597, lastPc=01014594), so
     * keep it env-gated until the actor visual/callback contract is proven.
     * Penglai _02 remains gated off by vm_net_mock_scene_supports_actor_other_npc_seed().
     */
    probeActorOtherNpcInFollowup = vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_NPC_OTHERINFO", 0) != 0;
    actorOtherNpcSeedInFollowup = !keepBusinessGate &&
                                  includeSkillBooks &&
                                  hasActorOtherRequestType &&
                                  actorOtherRequestType == 101 &&
                                  sceneSupportsActorOtherNpcSeed &&
                                  probeActorOtherNpcInFollowup;
    preferActorOtherNpcRows = actorOtherNpcSeedInFollowup;
    sceneNpcInfo11SeedInFollowup = !keepBusinessGate &&
                                   includeSkillBooks &&
                                   vm_net_mock_scene_room_npc_seed_count(currentScene) > 0 &&
                                   vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_NPCINFO11", 0) != 0;
    sceneNpcActorInfoSeedInFollowup = !keepBusinessGate &&
                                      includeSkillBooks &&
                                      sceneSupportsActorOtherNpcSeed &&
                                      vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_NPC_ACTORINFO", 0) != 0;
    /*
     * Narrow first-scene contract:
     * - answer the objects the client actually asked for in WT 49
     *   (12/1, 7/42, 6/1, 6/13, 6/14, 2/10, 25/5),
     * - then append scene enter 30/1.
     *
     * Do not append 30/3 room+npc data by default here. IDA evidence shows
     * scene-channel subtype 3 (0x01039222) consumes curpage/pagenum/roomnum/
     * roomlist and does not read npcnum/npcinfo. 27/11 remains an env-gated
     * probe only: runtime confirms it reaches scene_parse_npcinfo_and_spawn_npcs
     * (0x01037998), but both _02 and first-scene non-empty rows crashed in the
     * actor visual resolver before scene_npcinfo_create_return.
     */
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           includeSkillBooks, true, true, true,
                                                           false, false,
                                                           preferActorOtherNpcRows))
        return 0;
    appendSceneRoomNpcAfterEnter = !keepBusinessGate &&
                                   vm_net_mock_scene_room_npc_seed_count(currentScene) > 0 &&
                                   vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_ROOM_NPC", 0) != 0;
    if (keepBusinessGate)
    {
        u32 objectStart = 0;
        /*
         * Probe the first-scene contract without forcing client globals:
         * 27/12 and full 30/1 both close sceneObj+0x164 before later local
         * HUD responses. A 30/2 result/scene ack without posinfo exercises the
         * scene completion follow-up path but avoids that close branch.
         */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, vm_net_mock_scene_key_name()))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
    }
    else if (!vm_net_mock_append_scene_enter_object(out, outCap, &pos))
    {
        return 0;
    }
    objectCount += 1;

    if (sceneNpcInfo11SeedInFollowup)
    {
        /*
         * Runtime evidence:
         * - SCE-local nodes are created with draw/step callbacks but
         *   visualRes=0 because visual=0,0 makes scene_node_refresh_visual()
         *   read node-4.
         * - 27/11 npcinfo is the server packet path that calls the resource
         *   resolver and writes node+0x24. Emit it after 30/1 so the scene
         *   resource table has been initialized by LoadSceneDataFromStream().
         */
        if (!vm_net_mock_append_fb_target_scene_npcs11_object(out, outCap, &pos,
                                                             currentScene,
                                                             &sceneNpcInfo11Num,
                                                             &sceneNpcInfo11Len))
            return 0;
        objectCount += 1;
    }
    if (sceneNpcActorInfoSeedInFollowup)
    {
        /*
         * Runtime negative: placing 2/5 actorinfo before 30/1 leaves no visible
         * NPC. IDA shows 30/1 enters the scene-channel loader path, so seed
         * actorinfo after scene enter in the same open dispatch window.
         */
        if (!vm_net_mock_append_scene_actorinfo_npc_object(out, outCap, &pos,
                                                          currentScene,
                                                          &sceneNpcActorInfoLen,
                                                          &sceneNpcActorId))
            return 0;
        objectCount += 1;
    }
    if (appendSceneRoomNpcAfterEnter)
    {
        if (!vm_net_mock_append_scene_room_npc_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (actorOtherNpcSeedInFollowup)
    {
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
        g_vm_net_mock_scene_moveinfo_npc_seeded = true;
        snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                 sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene),
                 "%s", currentScene ? currentScene : "");
    }
    else if (!keepBusinessGate &&
        includeSkillBooks &&
        !appendSceneRoomNpcAfterEnter &&
        !preferActorOtherNpcRows &&
        vm_net_mock_env_u8("CBE_POST_SCENE_MOVEINFO_NPC_SEED", 0) != 0)
    {
        const char *pendingScene = currentScene;
        if (vm_net_mock_scene_supports_actor_other_npc_seed(pendingScene))
        {
            vm_net_mock_mark_scene_moveinfo_npc_seed_pending(pendingScene);
        }
    }
    return pos;
}

static u32 vm_net_mock_build_scene_task_subset_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    bool completeDeferredScene = g_vm_net_mock_last_scene_change_target_valid;
    const char *currentScene = NULL;
    bool seedSubsetNpcOther = false;
    bool seedSubsetNpcActorInfo = false;
    bool seedSubsetSceneNpcInfo = false;
    bool includeSkillBooks = false;
    u8 subsetSceneNpcNum = 0;
    u32 subsetSceneNpcInfoLen = 0;
    u32 subsetNpcActorInfoLen = 0;
    u32 subsetNpcActorId = 0;
    if (outCap < pos || !vm_net_mock_is_scene_task_subset_followup_request(request, requestLen))
        return 0;

    /*
     * This post-scene-change request is the WT49 task/other/banner subset
     * without skill/book objects. Runtime shows this is the first scene
     * follow-up after the 30/2 scene-change ack where dispatchGate is still 1,
     * while the later standalone 2/10 Type=1 response arrives with
     * dispatchGate=0 and is only decoded into the shared container.
     *
     * Keep NPC Type101 rows env-gated for the same reason as the first-scene
     * follow-up: the current row shape is parsed, but can leave the scene draw
     * callback at +0x148 unset and crash at scene_draw_actor_pass(0x01014594).
     */
    currentScene = vm_net_mock_current_scene_name();
    includeSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                        vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (completeDeferredScene &&
        currentScene != NULL &&
        vm_net_mock_scene_is_penglai03(currentScene) &&
        vm_net_mock_scene_names_equal_loose(currentScene, g_vm_net_mock_last_scene_change_target.scene))
    {
        /*
         * Runtime _02 -> _03 already consumed the real scene-enter callback from
         * the preceding WT 2/3 response. Repeating a second 30/2 completion
         * here drives scene_handle_change_result_scene_pos() again and produces
         * the visible extra loading-bar loop the user still sees on first entry.
         *
         * Let the later same-target WT 2/3 repeat-ack close the deferred scene
         * transition; this 6/1 subset should stay on the lighter task/banner
         * path only.
         */
        completeDeferredScene = false;
    }
    seedSubsetSceneNpcInfo = vm_net_mock_scene_room_npc_seed_count(currentScene) > 0 &&
                              vm_net_mock_env_u8("CBE_SCENE_TASK_SUBSET_NPCINFO11", 0) != 0;
    seedSubsetNpcOther = vm_net_mock_scene_supports_actor_other_npc_seed(currentScene) &&
                         vm_net_mock_env_u8("CBE_SCENE_TASK_SUBSET_NPC_OTHERINFO", 0) != 0;
    seedSubsetNpcActorInfo = vm_net_mock_scene_supports_actor_other_npc_seed(currentScene) &&
                             vm_net_mock_env_u8("CBE_SCENE_TASK_SUBSET_NPC_ACTORINFO", 0) != 0;
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           includeSkillBooks, true, true, true, false, false,
                                                           seedSubsetNpcOther))
        return 0;

    if (completeDeferredScene)
    {
        const vm_net_mock_scene_change_target *target = &g_vm_net_mock_last_scene_change_target;
        u32 objectStart = 0;
        if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target->scene, target->x, target->y))
            return 0;
        objectCount += 1;
        if (seedSubsetSceneNpcInfo)
        {
            if (!vm_net_mock_append_fb_target_scene_npcs11_object(out, outCap, &pos,
                                                                 currentScene,
                                                                 &subsetSceneNpcNum,
                                                                 &subsetSceneNpcInfoLen))
                return 0;
        }
        else if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
        {
            return 0;
        }
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                        g_vm_net_mock_last_scene_change_fb4_type,
                                                        vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2,
                                               target->scene, target->x, target->y))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        vm_net_mock_mark_completed_scene_change_target(target);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        if (g_vm_net_mock_teleport_stone_direct_enter_pending)
        {
            g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
            g_vm_net_mock_teleport_stone_direct_enter_pending = false;
            printf("[info][network] mock_teleport_stone_direct_enter_followup scene=%s pos=(%u,%u) objects=%u resp=%u\n",
                   target->scene, target->x, target->y, objectCount, pos);
            vm_autotest_note("mock_teleport_stone_direct_enter_followup scene=%s pos=(%u,%u) objects=%u response=scene-task-subset evidence=runtime:post-30/1-task-subset\n",
                             target->scene, target->x, target->y, objectCount);
        }
    }
    if (seedSubsetNpcActorInfo)
    {
        /*
         * Keep the same phase ordering as first scene enter: complete the
         * deferred scene/channel object first, then let actor_move case 5 create
         * NPC nodes after the scene table is initialized.
         */
        if (!vm_net_mock_append_scene_actorinfo_npc_object(out, outCap, &pos,
                                                          currentScene,
                                                          &subsetNpcActorInfoLen,
                                                          &subsetNpcActorId))
            return 0;
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (seedSubsetNpcOther)
    {
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
        g_vm_net_mock_scene_moveinfo_npc_seeded = true;
        snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                 sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene),
                 "%s", currentScene ? currentScene : "");
    }
    return pos;
}

static u32 vm_net_mock_build_short_wt_control_echo_response(const u8 *request, u32 requestLen,
                                                            u8 kind, u8 subtype,
                                                            u8 *out, u32 outCap)
{
    /*
     * 0x63/1 sits on the startup/login bridge. Dropping it leaves the progress
     * spinner running forever; while the exact protocol is still unknown, the
     * client does continue when it receives a short control ack back.
     */
    return vm_net_mock_copy_response(request, requestLen, out, outCap);
}

static u32 vm_net_mock_build_practise_info18_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 18, &objectStart))
        return 0;
    /*
     * sub_102CB46 handles the active practise-info panel. For subtype 18 it
     * reads todaypasthour, todaypastmin, getexp, todaylasthour,
     * todaylastmin, alllasthour, alllastmin, then isgold.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypasthour", 0))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypastmin", 15))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "getexp", 120))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylasthour", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylastmin", 45))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllasthour", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllastmin", 45))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "isgold", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen, u8 kind, u8 subtype);

static u32 vm_net_mock_build_battle_death_prompt_followup_response(const u8 *request, u32 requestLen,
                                                                   u8 *out, u32 outCap)
{
    /*
     * Latest confirmed runtime:
     * - subtype-6 attack now reaches Battle.cbm action parser at tick 216.
     * - the client then shows the prompt "您已经死亡，是否进入商城购买复活石?" via
     *   sub_103838A() result==2 -> sub_10110E6() -> ui_show_message_box().
     * - after the user clicks the prompt, the client emits a short one-object
     *   request `WT len=21 hdr=7/14 objs=1/7/14(result=2)`.
     *
     * The real server-side business contract after this click is still unknown.
     * For now, keep the transport/session alive with a same-packet echo rather
     * than guessing a shop / revive semantic family.
     */
    if (!vm_net_mock_is_short_wt_control_packet(request, requestLen, 7, 14))
        return 0;
    return vm_net_mock_copy_response(request, requestLen, out, outCap);
}

static u32 vm_net_mock_build_shop_actor_query14_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 actorId = 0;
    u32 pos = 5;
    u32 page5Index = 0;
    u32 totalItems = 0;
    u32 pageRows = 0;
    u32 itemInfoLen = 0;
    u32 actorInfoLen = 0;
    char page5Ids[160];
    vm_net_mock_request_object object;

    /*
     * mmShopMstarWqvga.cbm:sub_11F0 sends WT 1/1/14(actorId) when the NPC
     * shop "buy" branch opens. Runtime return-from-shop evidence shows this
     * request can arrive without the sub_1038/sub_618 14/5+14/6 follow-up page
     * requests. sub_162C only marks the shop loading flag and calls sub_11F0,
     * so return the first buy page inline with status/money for that path.
     */
    if (request == NULL || requestLen < 9 || out == NULL || outCap < pos)
        return 0;
    if (request[0] != 'W' || request[1] != 'T')
        return 0;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return 0;
    if (offset != requestLen)
        return 0;
    if (object.major != 1 || object.kind != 1 || object.subtype != 14)
        return 0;
    if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorId", &actorId))
        return 0;

    if (!vm_net_mock_append_shop_open_status14_object(out, outCap, &pos))
        return 0;
    if (!vm_net_mock_append_shop_money4_object(out, outCap, &pos))
        return 0;
    if (!vm_net_mock_append_shop_catalog_page_object(out, outCap, &pos, 5, page5Index,
                                                    &totalItems, &pageRows, &itemInfoLen))
        return 0;
    if (!vm_net_mock_append_shop_empty_page14_object(out, outCap, &pos, 6))
        return 0;
    /*
     * mmShop:0x9DE returns immediately after the 1/1/14 actor-state branch and
     * clears the shop loading flag there, so keep this object last.
     */
    if (!vm_net_mock_append_shop_actor_state14_object(out, outCap, &pos, &actorInfoLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 5);
    g_netMockShop17ListPending = 1;
    vm_net_mock_format_shop_page_ids(page5Index, 8, page5Ids, sizeof(page5Ids));
    printf("[info][network] mock_shop_open14 actorId=%u pages=inline actor_state=last page5=%u page6=0 total=%u rows=%u iteminfo_len=%u actorinfo_len=%u ids=%s\n",
           actorId,
           page5Index,
           totalItems,
           pageRows,
           itemInfoLen,
           actorInfoLen,
           page5Ids);
    vm_autotest_note("mock_shop_open14 actorId=%u pages=inline actor_state=last page5=%u items_total=%u page_rows=%u iteminfo_len=%u actorinfo_len=%u evidence=runtime:no-page-followup-after-1/1/14 mmShop:0x162C/0x11F0/0x9DE/0x7BC\n",
                     actorId,
                     page5Index,
                     totalItems,
                     pageRows,
                     itemInfoLen,
                     actorInfoLen);
    return pos;
}

static bool vm_net_mock_is_shop_info14_request(const u8 *request, u32 requestLen, u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 14)
        return false;
    if (object.subtype != 14 && object.subtype != 4)
        return false;

    if (subtypeOut)
        *subtypeOut = object.subtype;
    return true;
}

static u32 vm_net_mock_build_shop_info14_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 subtype = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_is_shop_info14_request(request, requestLen, &subtype))
        return 0;

    if (subtype == 14)
    {
        if (!vm_net_mock_append_shop_open_status14_object(out, outCap, &pos))
            return 0;
    }
    else
    {
        if (!vm_net_mock_append_shop_money4_object(out, outCap, &pos))
            return 0;
    }

    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_netMockShop17ListPending = 1;
    printf("[info][network] mock_shop_info14 subtype=%u response=14/%u\n", subtype, subtype);
    vm_autotest_note("mock_shop_info14 subtype=%u response=14/%u evidence=mmShop:0x6D6/0x6BC/0x9DE\n",
                     subtype, subtype);
    return pos;
}

static bool vm_net_mock_is_shop_page14_request(const u8 *request, u32 requestLen,
                                               u8 *subtypeOut, u32 *indexOut)
{
    u32 offset = 4;
    u32 index = 0;
    u8 index8 = 0;
    vm_net_mock_request_object object;

    if (subtypeOut)
        *subtypeOut = 0;
    if (indexOut)
        *indexOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 14 || object.subtype < 5 || object.subtype > 13)
        return false;
    if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "index", &index))
    {
        if (!vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "index", &index8))
            return false;
        index = index8;
    }

    if (subtypeOut)
        *subtypeOut = object.subtype;
    if (indexOut)
        *indexOut = index;
    return true;
}

static u32 vm_net_mock_build_shop_page14_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 pageIndex = 0;
    u32 totalItems = 0;
    u32 pageRows = 0;
    u32 itemInfoLen = 0;
    u8 subtype = 0;
    const u8 emptyItemInfo[] = {0};
    char ids[160];

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_is_shop_page14_request(request, requestLen, &subtype, &pageIndex))
        return 0;

    if (subtype == 5)
    {
        if (!vm_net_mock_append_shop_catalog_page_object(out, outCap, &pos, subtype, pageIndex,
                                                        &totalItems, &pageRows, &itemInfoLen))
            return 0;
    }
    else
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 14, subtype, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalnum", 0))
            return 0;
        if (!vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo",
                                        emptyItemInfo, (u16)sizeof(emptyItemInfo)))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        itemInfoLen = (u32)sizeof(emptyItemInfo);
    }

    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_netMockShop17ListPending = 1;
    ids[0] = 0;
    if (subtype == 5)
        vm_net_mock_format_shop_page_ids(pageIndex, 8, ids, sizeof(ids));
    printf("[info][network] mock_shop_page14 subtype=%u index=%u total=%u rows=%u iteminfo_len=%u first=%u ids=%s\n",
           subtype,
           pageIndex,
           totalItems,
           pageRows,
           itemInfoLen,
           g_vm_net_mock_shop_catalog_count > 0 ? g_vm_net_mock_shop_catalog[0].itemId : 0,
           ids);
    vm_autotest_note("mock_shop_page14 subtype=%u index=%u items_total=%u page_rows=%u iteminfo_len=%u evidence=mmShop:0x618/0x7BC\n",
                     subtype, pageIndex, totalItems, pageRows, itemInfoLen);
    return pos;
}

static bool vm_net_mock_is_shop_buy17_request(const u8 *request, u32 requestLen,
                                              u32 *shopIdOut, u8 *countOut)
{
    u32 offset = 4;
    u32 shopId = 0;
    u8 count = 1;
    vm_net_mock_request_object object;

    if (shopIdOut)
        *shopIdOut = 0;
    if (countOut)
        *countOut = 1;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 17 || object.subtype != 2)
        return false;
    if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "shopId", &shopId))
        return false;

    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "num", &count);
    if (count == 0)
        count = 1;
    if (shopIdOut)
        *shopIdOut = shopId;
    if (countOut)
        *countOut = count;
    return true;
}

static u32 vm_net_mock_build_shop_buy17_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 shopId = 0;
    u8 count = 1;
    u16 seq = 0;
    u8 result = 0;

    if (outCap < pos || out == NULL)
        return 0;
    if (!vm_net_mock_is_shop_buy17_request(request, requestLen, &shopId, &count))
        return 0;
    result = vm_net_mock_role_add_backpack_item(shopId, count, &seq) ? 1 : 0;

    /*
     * The buy button sends WT 17/2 with shopId, but mmShopMstarWqvga.cbm:sub_9DE
     * consumes the server result as kind 14 subtype 3.  That branch reads "seq"
     * first, then "result"; on result==1 it inserts the purchased item through
     * the normal item-manager callback.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 14, 3, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "seq", seq))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_autotest_note("mock_shop_buy17 shopId=%u count=%u resp=14/3 seq=%u result=%u evidence=mmShop:0x9DE(seq/result)\n",
                     shopId, count, seq, result);
    return pos;
}

static bool vm_net_mock_is_login_tail_skill_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen != 14)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;

    u32 offset = 4;
    vm_net_mock_request_object object;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x0c || object.subtype != 1 || object.payloadLen != 0)
        return false;

    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42 || object.payloadLen != 0)
        return false;

    return offset == requestLen;
}

static u32 vm_net_mock_build_login_tail_skill_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos, &objectCount))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen, u8 kind, u8 subtype)
{
    if (request == NULL || requestLen != 9)
        return false;
    return request[0] == 'W' &&
           request[1] == 'T' &&
           request[2] == 0 &&
           request[3] == 9 &&
           request[4] == 1 &&
           request[5] == kind &&
           request[6] == subtype &&
           request[7] == 0 &&
           request[8] == 5;
}

static const char *vm_net_mock_actor_resource_name(u8 actorJob, u8 actorSex)
{
    static const char *actorNames[6] = {
        "h_warrior.actor",
        "hW_warrior.actor",
        "h_assassin.actor",
        "hW_assassin.actor",
        "h_mage.actor",
        "hW_mage.actor",
    };
    u32 tableIndex = vm_net_mock_actor_resource_index(actorJob, actorSex);
    if (tableIndex >= sizeof(actorNames) / sizeof(actorNames[0]))
        tableIndex = 0;
    return actorNames[tableIndex];
}

static const char *vm_net_mock_actor_preview_image_name(u8 actorJob, u8 actorSex)
{
    const char *overrideName = vm_net_mock_env_str("CBE_ACTOR_PREVIEW_IMAGE", "");
    (void)actorJob;
    (void)actorSex;
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    return "";
}

static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();
    const char *displayName = vm_net_mock_env_str("CBE_ACTOR_DISPLAY_NAME",
                                                  vm_net_mock_role_sect_name(role));
    u32 roleLevel = vm_net_mock_env_u32("CBE_ROLE_LEVEL", role ? role->level : 1);
    u32 roleExp = role ? role->exp : 0;
    u32 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", role ? role->job : 1);
    u32 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", role ? role->sex : 0);
    vm_net_mock_player_stats playerStats;
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 visualVariant = 0;
    u32 visualGroup = 0;
    u16 actorStrength = 0;
    u16 actorAgility = 0;
    u16 actorWisdom = 0;
    u16 actorEndurance = 0;
    u16 actorCharm = 0;
    u32 actorAttrWords[6];
    u32 primaryCurrent = 0;
    u32 primaryBaseMax = 0;
    u32 secondaryCurrent = 0;
    u32 secondaryBaseMax = 0;
    u32 actorSummaryValue = vm_net_mock_env_u32("CBE_ACTOR_SUMMARY_VALUE", roleExp);
    u32 actorGap09C0 = vm_net_mock_env_u32("CBE_ACTOR_GAP09C0",
                                           role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY);
    u32 actorSummaryStatus = 0;
    u8 actorBackpackCapacity = vm_net_mock_env_u8("CBE_ACTOR_BACKPACK_CAPACITY",
                                                   role ? role->backpackCapacity :
                                                   VM_NET_MOCK_BACKPACK_CAPACITY);
    u8 actorStateByte1 = vm_net_mock_env_u8("CBE_ACTOR_STATE_BYTE1", 0);
    u8 actorTargetX = 12;
    u8 actorTargetY = 10;
    u8 actorField11E = 0;
    u8 actorField120 = 0;
    const char *shortLabel = NULL;
    const char *actorResource = NULL;
    const char *sceneKey = NULL;
    u16 actorResourceArg = 0;
    u16 actorGridX = 0;
    u16 actorGridY = 0;
    u16 motionResourceArg0 = 0;
    u16 motionResourceArg1 = 0;
    u32 primaryDisplayMax = 0;
    u32 secondaryDisplayMax = 0;
    u32 actorGap0CC0 = 0;
    u32 actorGap0CC4 = 0;
    u32 actorGap0CC8 = 0;

    vm_net_mock_role_build_player_stats(role, &playerStats);
    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    primaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_HP_CURRENT",
                                         vm_net_mock_env_u32("CBE_ACTOR_HP", roleHpDefault));
    primaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_HP_MAX", roleMaxHpDefault);
    secondaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_MP_CURRENT",
                                           vm_net_mock_env_u32("CBE_ACTOR_MP", roleMpDefault));
    secondaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_MP_MAX", roleMaxMpDefault);
    primaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_HP_DISPLAY_MAX", primaryBaseMax);
    secondaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_MP_DISPLAY_MAX", secondaryBaseMax);
    if (roleLevel == 0)
        roleLevel = 1;
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    if (primaryBaseMax == 0)
        primaryBaseMax = 120;
    if (secondaryBaseMax == 0)
        secondaryBaseMax = 100;
    if (primaryCurrent > primaryBaseMax)
        primaryCurrent = primaryBaseMax;
    if (secondaryCurrent > secondaryBaseMax)
        secondaryCurrent = secondaryBaseMax;
    if (primaryDisplayMax == 0)
        primaryDisplayMax = primaryBaseMax;
    if (secondaryDisplayMax == 0)
        secondaryDisplayMax = secondaryBaseMax;

    actorStrength = (u16)vm_net_mock_cap_u32(playerStats.strength, 999);
    actorAgility = (u16)vm_net_mock_cap_u32(playerStats.agility, 999);
    actorWisdom = (u16)vm_net_mock_cap_u32(playerStats.wisdom, 999);
    actorEndurance = (u16)vm_net_mock_cap_u32(playerStats.endurance, 999);
    actorCharm = (u16)vm_net_mock_cap_u32(playerStats.charm, 999);
    actorAttrWords[0] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_STRENGTH", actorStrength);
    actorAttrWords[1] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_AGILITY", actorAgility);
    actorAttrWords[2] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_WISDOM", actorWisdom);
    actorAttrWords[3] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_ENDURANCE", actorEndurance);
    actorAttrWords[4] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_CHARM", actorCharm);
    actorAttrWords[5] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_RESERVE",
                                            playerStats.defense);
    actorSummaryStatus = vm_net_mock_env_u32("CBE_ACTOR_STATUS_WORD", roleLevel);
    actorGap0CC0 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC0", playerStats.attack);
    actorGap0CC4 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC4", playerStats.defense);
    actorGap0CC8 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC8", playerStats.resist);
    shortLabel = vm_net_mock_env_str("CBE_ACTOR_SHORT_LABEL", vm_net_mock_role_title(role));

    /*
     * Fresh scene-enter keeps the compact title-facing knobs but the scene-side
     * portrait/widget picker is not fully 0-based. Static `scene_runtime_init_
     * and_sync()` now proves the portrait index is `visualGroup + 2*visualVariant - 1`,
     * so `visualVariant` remains 0-based (`job-1`) while `visualGroup` must stay
     * in the original `1..2` family rather than `0..1`.
     */
    visualVariant = actorJob - 1;
    visualGroup = actorSex + 1;

    actorTargetX = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_X", 12);
    actorTargetY = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_Y", 10);
    actorGridX = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_X", role ? role->x : vm_net_mock_scene_spawn_x());
    actorGridY = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_Y", role ? role->y : vm_net_mock_scene_spawn_y());
    actorField11E = vm_net_mock_env_u8("CBE_ACTOR_BYTE_11E", actorTargetX);
    actorField120 = vm_net_mock_env_u8("CBE_ACTOR_BYTE_120", actorTargetY);
    actorResource = vm_net_mock_actor_resource_name((u8)actorJob, (u8)actorSex);
    sceneKey = vm_net_mock_scene_key_name();
    actorResourceArg = (u16)vm_net_mock_env_u32("CBE_ACTOR_RESOURCE_ARG", roleLevel);
    motionResourceArg0 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG0", actorGridX);
    motionResourceArg1 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG1", actorGridY);

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualVariant))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualGroup))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, displayName))
        return 0;

    /*
     * Fresh scene-enter (`parse_actorinfo_response(a2==0)`) does not match the
     * older guessed "level/current/base/..." order directly. The confirmed
     * stream from the second string onward is:
     *   tagged u32 summaryStatus (stored into a word slot)
     *   u32 primaryCurrent
     *   u32 primaryBaseMax
     *   u32 secondaryCurrent
     *   u32 secondaryBaseMax
     *   u32 charm-like attribute
     *   six truncated u32 -> property words (str/agi/wis/end/charm/reserve)
     *   u32 total EXP (actor+176)
     *   u32 gap09C0
     *   u8 backpackCapacity (stored into main item manager +38)
     *   u8 state1
     *   u32 primaryDisplayMax
     *   u32 secondaryDisplayMax
     *   u8 targetPosX
     *   u8 targetPosY
     *   str shortLabel/title
     *   str previewImage
     *   u32 gap0CC0
     *   u32 gap0CC4
     *   u32 gap0CC8
     *   str actorResource
     *   i16 level word
     *   str sceneKey
     *   i16 motionArg0
     *   i16 motionArg1
     */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryStatus))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos,
                                 vm_net_mock_env_u32("CBE_ACTOR_EXTRA132", actorCharm)))
        return 0;

    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorAttrWords[i]))
            return 0;
    }

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryValue))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap09C0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorBackpackCapacity))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorStateByte1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField11E))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField120))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, shortLabel))
        return 0;
    /*
     * This 20-byte mid-body string lands at actor+0x10A.  The final scene
     * consumer seen so far is `scene_draw_node_overhead_overlay()`, which treats
     * a non-empty value as an optional named overhead badge/icon. Do not seed it
     * with the actor's walk GIF; that makes body art appear as stray fragments.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                    vm_net_mock_actor_preview_image_name((u8)actorJob, (u8)actorSex)))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC0))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC4))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC8))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, actorResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, actorResourceArg))
        return 0;
    /*
     * parse_actorinfo_response copies this second trailing string directly into
     * R9+0x5E46, and fresh scene-enter later reuses that buffer as
     * currentMapIdText / the persistent +1141 scene-text slot. Keep it separate
     * from actorResource and the trailing grid/pose-like word pair.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, sceneKey))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg0))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg1))
        return 0;


    return pos;
}

static u32 vm_net_mock_build_role_list_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();
    u32 roleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 roleLevel = vm_net_mock_env_u32("CBE_ROLE_LEVEL", role ? role->level : 1);

    if (roleLevel == 0)
        roleLevel = 1;

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleLevel))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_title_role_list_actorinfo(u8 *out, u32 outCap)
{
    u32 pos = 0;
    vm_net_mock_role_db_load();
    u32 roleCount = g_vm_net_mock_role_db_valid ? g_vm_net_mock_role_db.roleCount : 1;
    if (roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        roleCount = VM_NET_MOCK_ROLE_DB_MAX_ROLES;

    /*
     * mmTitleMstarWqvga.cbm sub_3544() reads compact role-list bytes as:
     *   id, jobIndex(0..2), sex(1..2), name, level.
     * The create-success path at 0x5324 writes the same layout into the local
     * title row: row+325 = selected job index, row+324 = selected sex + 1.
     *
     * mmTitleMstarWqvga.cbm sub_3544() parses its actorinfo field as:
     *   count, then repeated (tagged u32, tagged u8, tagged u8, len16+cstr, tagged i16).
     * The field name collides with later in-scene actorinfo, but the title-side
     * payload is a compact role-list entry table instead of the large player blob.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)roleCount))
        return 0;
    for (u32 i = 0; i < roleCount; ++i)
    {
        vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        u8 actorJobIndex = role->job > 0 ? (u8)(role->job - 1) : 0;
        u8 actorSex = (u8)(role->sex + 1);
        u16 roleLevel = (u16)role->level;
        if (actorJobIndex > 2)
            actorJobIndex = 0;
        if (actorSex < 1 || actorSex > 2)
            actorSex = 1;
        if (roleLevel == 0)
            roleLevel = 1;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, role->roleId))
            return 0;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorJobIndex))
            return 0;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorSex))
            return 0;
        if (!vm_net_mock_seq_put_string(out, outCap, &pos, role->name))
            return 0;
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, roleLevel))
            return 0;
    }

    return pos;
}

static u32 vm_net_mock_build_login_serverinfo_blob(u8 *out, u32 outCap)
{
    u32 pos = 0;
    const char serverName[] = "\xb2\xe2\xca\xd4\xd2\xbb\xc7\xf8"; /* GBK: ce shi yi qu */
    const char serverLabel[] = "\xcd\xc6\xbc\xf6"; /* GBK: tui jian */

    if (!vm_net_mock_seq_put_string(out, outCap, &pos, serverName))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, serverLabel))
        return 0;
    /* Keep the runtime-stable serverinfo shape: the title parser consumes the
     * first server attribute as a 32-bit field, but the trailing display color
     * is a 24-bit value. Using a visible color here avoids an "empty" looking
     * list when the server row is present but rendered with black-on-black. */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u24(out, outCap, &pos, 0x00FFFFFF))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_title_servconf_blob(u8 *out, u32 outCap)
{
    u32 pos = 0;

    /*
     * mmTitleMstarWqvga.cbm sub_3544() reads servconf through sub_1490(),
     * which consumes a 5-byte tagged blob and copies bytes 2..4 into the
     * server-selection state. Reuse the same compact color-shape encoding here.
     */
    if (!vm_net_mock_seq_put_u24(out, outCap, &pos, 0x00FFFFFF))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_login_color_blob(u8 *out, u32 outCap)
{
    u32 pos = 0;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u24(out, outCap, &pos, 0x00FFFFFF))
        return 0;

    return pos;
}

static u8 vm_net_mock_resolve_login_success_subtype(u8 requestSubtype)
{
    const char *rawSubtype = vm_net_mock_env_str("CBE_LOGIN_TOP_TYPE", "");
    u8 actorSubtype = requestSubtype;

    if (rawSubtype != NULL && rawSubtype[0] != 0)
        actorSubtype = vm_net_mock_env_u8("CBE_LOGIN_TOP_TYPE", requestSubtype);

    if (actorSubtype != 1 && actorSubtype != 2 && actorSubtype != 3 && actorSubtype != 6)
        actorSubtype = 6;

    return actorSubtype;
}

static bool vm_net_mock_append_login_success_object(u8 *out,
                                                    u32 outCap,
                                                    u32 *pos,
                                                    u8 actorSubtype,
                                                    bool includeActorInfo,
                                                    u32 *actorInfoLenOut)
{
    u8 actorInfo[512];
    u32 actorInfoLen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 totalExp = role ? role->exp : 0;
    u32 curExp = vm_net_mock_role_next_level_start_exp(totalExp);
    u32 lastExp = vm_net_mock_role_last_level_exp(totalExp);
    u32 percentExp = vm_net_mock_role_exp_percent(totalExp);

    if (includeActorInfo)
    {
        memset(actorInfo, 0, sizeof(actorInfo));
        actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
        if (actorInfoLen == 0)
            return false;
    }

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, actorSubtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "revivetype", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "ruffianflag", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 0))
        return false;
    if (actorSubtype == 6)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "practiseflag", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expbook", 0))
            return false;
        if (!vm_net_mock_put_object_string(out, outCap, pos, "practiseinfo", ""))
            return false;
    }
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", lastExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", curExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", percentExp))
        return false;
    if (includeActorInfo)
    {
        if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
            return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    return true;
}

static bool vm_net_mock_append_actorinfo_object(u8 *out, u32 outCap, u32 *pos, u32 *actorInfoLenOut)
{
    return vm_net_mock_append_login_success_object(out, outCap, pos, 1, true, actorInfoLenOut);
}

static u32 vm_net_mock_build_login_primary_validation_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[128];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(serverInfo, sizeof(serverInfo));
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    /*
     * IDA: mmTitle net_handle_login_response(0x16DC) reads "servernum"
     * through accessor slot [18], while "result"/"newVer" use [19].
     * Runtime disproved both the original u8 encoding and a later u32 trial:
     * the client still left serverCount==0 while result/newVer parsed. Use a
     * u16 field here as the next narrow contract probe for accessor[18].
     */
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (!g_netMockTitleServerSelectConfirmed)
        vm_net_mock_title_login_phase_mark_server_list();
    return pos;
}

static u32 vm_net_mock_build_login_failure_response(u8 requestSubtype, const char *information, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 responseSubtype = requestSubtype ? requestSubtype : 1;
    const char *message = information ? information : "password error";

    if (outCap < pos)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, responseSubtype, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 2))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "information", message))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_login_primary_wait_server_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[128];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(serverInfo, sizeof(serverInfo));
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    /*
     * The primary login parser only accepts result '1' as the success path
     * that continues on to parse serverinfo/servernum/newVer. Keep this reply
     * narrow: server list metadata only, no actorinfo/role payload.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_list();
    return pos;
}

static bool vm_net_mock_append_role_list_object(u8 *out, u32 outCap, u32 *pos)
{
    u8 roleInfo[128];
    memset(roleInfo, 0, sizeof(roleInfo));
    u32 roleInfoLen = vm_net_mock_build_role_list_info(roleInfo, sizeof(roleInfo));
    if (roleInfoLen == 0)
        return false;

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x20, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roles", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "maxroles", 3))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "allpgs", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "num", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "roleinfo", roleInfo, (u16)roleInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_title_mode15_result_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    /*
     * The active R9+0x29E8 title-local state machine has a narrower confirmed
     * gate: its type=1/subtype=15 success path goes straight to sub_39A4(),
     * which is the same mode<-1 transition we already correlated with the
     * role-selection state.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, 15, &objectStart))
        return false;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_role_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_title_mode15_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_title_server_select_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 serverId = 0;
    u32 moneyType = 0;
    u8 servConf[8];
    u32 servConfLen = 0;
    u8 actorInfo[128];
    u32 actorInfoLen = 0;

    if (outCap < pos)
        return 0;

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "serverID", &serverId);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "moneytype", &moneyType);
    memset(servConf, 0, sizeof(servConf));
    servConfLen = vm_net_mock_build_title_servconf_blob(servConf, sizeof(servConf));
    if (servConfLen == 0)
        return 0;
    actorInfoLen = vm_net_mock_build_title_role_list_actorinfo(actorInfo, sizeof(actorInfo));
    if (actorInfoLen == 0)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "servconf", servConf, (u16)servConfLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_select(serverId);
    return pos;
}

static u32 vm_net_mock_build_title_rolelist_stage_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 actorinfo[128];
    u32 actorinfoLen = 0;

    if (outCap < pos)
        return 0;

    actorinfoLen = vm_net_mock_build_title_role_list_actorinfo(actorinfo, sizeof(actorinfo));
    if (actorinfoLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 16, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorinfo, (u16)actorinfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    vm_net_mock_finish_wt_packet(out, pos, 2);
    return pos;
}

static u32 vm_net_mock_build_title_rolelist_wait_server_select_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (outCap < pos)
        return 0;

    /*
     * role_list_screen_render() can emit the 1/1/16 role-list request as soon
     * as primary login success moves into the title list screen. Until the
     * client has sent the explicit 1/1/4 serverID/moneytype confirmation, keep
     * this as a parser-safe no-op reply. The actual actorinfo object is only
     * valid after server selection, and subtype 16 result '1' is the cached
     * success latch that the title parser expects before it accepts subtype 4
     * actorinfo. Keep this reply minimal so it only advances the state machine
     * and does not mix the later role payload into the same packet.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 16, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_title_role_create_response(const u8 *request,
                                                        u32 requestLen,
                                                        u8 *out,
                                                        u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_title_role_create_request fields;
    u32 actorId = 0;
    u8 result = 1;
    u32 roleCount = 0;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_parse_title_role_create_request(request, requestLen, &fields))
        return 0;

    if (!vm_net_mock_role_db_create_from_title(&fields, &actorId, &result))
        result = 1;
    if (g_vm_net_mock_role_db_valid)
        roleCount = g_vm_net_mock_role_db.roleCount;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 7, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "actorid", actorId))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_autotest_note("mock_title_role_create result=%u actorid=%u decoded=%u raw_sex=%u raw_job=%u job_index=%u roles=%u name_len=%u response=1/1/7 evidence=mmTitle:0x3E66/0x5324 runtime=wt1/7-len34\n",
                     result,
                     actorId,
                     fields.decoded ? 1 : 0,
                     fields.rawSex,
                     fields.rawJob,
                     fields.rawJobIsIndex ? 1 : 0,
                     roleCount,
                     (u32)strlen(fields.name));
    return pos;
}

static u32 vm_net_mock_build_title_role_delete_response(const u8 *request,
                                                        u32 requestLen,
                                                        u8 *out,
                                                        u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 actorId = 0;
    u8 result = 1;
    u32 roleCount = 0;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_parse_title_role_delete_request(request, requestLen, &actorId))
        return 0;

    if (!vm_net_mock_role_db_delete_by_id(actorId, &result, &roleCount))
        result = 1;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 8, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_autotest_note("mock_title_role_delete result=%u actorid=%u roles=%u response=1/1/8 evidence=mmTitle:0x1F90/0x53EC runtime=wt1/8-len25\n",
                     result,
                     actorId,
                     roleCount);
    return pos;
}

static u32 vm_net_mock_build_title_role_select_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    vm_net_mock_role_state *role = NULL;
    u32 actorId = 0;
    u32 activeActorId = 0;
    u32 actorInfoLen = 0;
    bool selected = false;

    if (outCap < pos)
        return 0;

    if (!vm_net_mock_parse_title_role_select_request(request, requestLen, &actorId))
        return 0;
    selected = vm_net_mock_select_active_role(actorId);
    role = vm_net_mock_active_role();
    activeActorId = role ? role->roleId : 0;

    /* A minimal {result, actorID} subtype-6 ack is accepted by
     * role_manage_screen_handle_network(case 6), but as soon as subtype-15
     * pushes the client into the next bootstrap path the main scene-side logic
     * also sees this subtype-6 object. Static scene_runtime_init_and_sync()
     * treats subtype 6 as a full scene-enter family packet and reads fields
     * such as revivetype/type/practiseflag/pcimg/.../actorinfo from it. Reuse
     * the existing full subtype-6 success shell here so the follow-up subtype-15
     * experiment is not blocked by a missing scene-enter payload contract.
     */
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 6, true, &actorInfoLen))
        return 0;

    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_autotest_note("mock_title_role_select requested=%u selected=%u active=%u name_len=%u actorinfo_len=%u response=1/1/6+1/1/15 evidence=mmTitle:0x39FC/0x1B9C/0x53EC\n",
                     actorId,
                     selected ? 1 : 0,
                     activeActorId,
                     role ? (u32)strlen(role->name) : 0,
                     actorInfoLen);
    return pos;
}

static u32 vm_net_mock_build_login_alt12_server_list_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[128];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;
    char userName[64];
    char password[64];
    bool haveUserName = false;
    bool havePassword = false;
    u8 resultCode = vm_net_mock_env_u8("CBE_ALT12_SERVERLIST_RESULT", 4);
    bool echoCredentials = vm_net_mock_env_u8("CBE_ALT12_ECHO_CREDENTIALS", 0) != 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    memset(userName, 0, sizeof(userName));
    memset(password, 0, sizeof(password));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(serverInfo, sizeof(serverInfo));
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username", userName, sizeof(userName));
    if (!haveUserName)
        haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName", userName, sizeof(userName));
    havePassword = vm_net_mock_get_object_string_field(request, requestLen, "password", password, sizeof(password));

    if (resultCode != 3 && resultCode != 4)
        resultCode = 4;

    /*
     * mmTitle net_handle_login_response(0x16DC) treats subtype-12 result '1'
     * as a staged success and login_alt_result_dispatch(0x19C2) immediately
     * enters the stageFlag==4 role-list target. Result '3'/'4' on the subtype
     * 12 path is the parser branch that still consumes serverinfo/servernum.
     * Runtime showed result '3' can loop back through the sub_5916 success
     * callback path; prefer result '4', whose sub_5922 callback checks the
     * title confirmation byte before calling login_stage_success_dispatch().
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", resultCode))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "information", "select server"))
        return 0;
    if (echoCredentials)
    {
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "username", haveUserName ? userName : ""))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "password", havePassword ? password : ""))
            return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_list();
    return pos;
}

static u32 vm_net_mock_build_login_alt12_success_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (outCap < pos)
        return 0;

    /*
     * The live login tap currently sends a top-level 1/1/12 request. Static
     * title RE already has a dedicated wrapper for that alternate success path
     * (`sub_2A50 -> login_alt_result_dispatch -> login_stage_success_dispatch`),
     * so keep this response minimal and do not pre-inject later actorinfo here.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_login_actor_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    g_netMockEnterGameOffset = 0;
    g_netMockEnterGameChecksum = 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    u8 logJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 logSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    return pos;
}

static u32 vm_net_mock_build_login_role_list_only_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;

    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 1, false, NULL))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    return pos;
}

static u32 vm_net_mock_build_login_actor_role_list_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    return pos;
}

static u32 vm_net_mock_build_login_response(const u8 *request, u32 requestLen, u8 requestSubtype, u8 *out, u32 outCap)
{
    const char *mode = vm_net_mock_env_str("CBE_LOGIN_RESPONSE", "");
    char userName[64];
    char password[64];
    bool haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName", userName, sizeof(userName));
    bool havePassword = false;
    memset(password, 0, sizeof(password));
    if (!haveUserName)
        haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username", userName, sizeof(userName));
    havePassword = vm_net_mock_get_object_string_field(request, requestLen, "password", password, sizeof(password));

    if (haveUserName && strcmp(userName, "1234") == 0)
    {
        return vm_net_mock_build_login_failure_response(requestSubtype, "password error", out, outCap);
    }


    if (requestSubtype == 1 &&
        mode != NULL &&
        strcmp(mode, "serverlist-hold") == 0)
    {
        return vm_net_mock_build_login_primary_wait_server_response(out, outCap);
    }

    if (requestSubtype == 1 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0 ||
         strcmp(mode, "primary-validate") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0))
    {
        return vm_net_mock_build_login_primary_validation_response(out, outCap);
    }

    if (requestSubtype == 12 &&
        haveUserName && userName[0] == 0 &&
        havePassword && password[0] == 0 &&
        g_netMockTitleServerListPending &&
        !g_netMockTitleServerSelectConfirmed &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0))
    {
        return vm_net_mock_build_login_alt12_success_response(out, outCap);
    }

    if (requestSubtype == 12 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0))
    {
        return vm_net_mock_build_login_alt12_server_list_response(request, requestLen, out, outCap);
    }

    if (requestSubtype == 12 &&
        (strcmp(mode, "alt12-success") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0))
    {
        return vm_net_mock_build_login_alt12_success_response(out, outCap);
    }

    /* Current static/runtime reading is staged rather than same-packet mixing:
     * login validation returns only the login/server-list contract, then the
     * client sends the next request (1/1/16 for title role list, 1/1/6 for role
     * select). Retain older mixed object modes only for explicit regression
     * comparison through CBE_LOGIN_RESPONSE.
     */
    if (mode == NULL || mode[0] == 0 ||
        strcmp(mode, "staged-rolelist") == 0 ||
        strcmp(mode, "staged") == 0)
    {
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "roles+actor") == 0 ||
        strcmp(mode, "actor+roles") == 0 ||
        strcmp(mode, "roles-first") == 0)
    {
        return vm_net_mock_build_login_actor_role_list_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "actor") == 0)
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);

    return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);
}

static u32 vm_net_mock_build_enter_game_response(u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    u8 payload[65535];
    const char *payloadName = "mmGameMstarWqvga.cbm";
    u32 payloadLen = vm_net_mock_load_response_file("JHOnlineData/mmGameMstarWqvga.cbm", payload, sizeof(payload));
    if (payloadLen == 0)
    {
        payloadName = "MMORPGTempcbm";
        payloadLen = vm_net_mock_load_response_file("JHOnlineData/MMORPGTempcbm", payload, sizeof(payload));
    }
    if (payloadLen == 0)
        return 0;

    if (g_netMockEnterGameOffset >= payloadLen)
    {
        g_netMockEnterGameOffset = 0;
        g_netMockEnterGameChecksum = 0;
    }

    u32 chunkLen = payloadLen - g_netMockEnterGameOffset;
    if (chunkLen > 1024)
        chunkLen = 1024;
    u32 chunkSum = vm_net_mock_signed_byte_sum(payload + g_netMockEnterGameOffset, chunkLen);
    u32 crc = g_netMockEnterGameChecksum + chunkSum;

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", payloadLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", (const u8 *)"\x00\x02\x00\x01", 4))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", payload + g_netMockEnterGameOffset, (u16)chunkLen))
        return 0;

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 7;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    g_netMockEnterGameChecksum = crc;
    g_netMockEnterGameOffset += chunkLen;
    return pos;
}

static bool vm_net_mock_append_game_type_response_object(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap, u32 *pos,
    u8 requestType, u8 responseType, u8 responseSub)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;

    if (request == NULL || requestLen < 8 || out == NULL || pos == NULL)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, request[4], responseType, responseSub, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (requestType == 1)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", 0))
            return false;
        if (!vm_net_mock_put_object_string(out, outCap, pos, "name", "Codex"))
            return false;
        if (!vm_net_mock_put_object_u32(out, outCap, pos, "money", money))
            return false;
    }
    else if (requestType == 2)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
    }
    else if (requestType == 3)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
            return false;
    }

    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_game_type_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap, u8 requestType)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 requestKind = 0;
    u8 requestSubtype = 0;

    if (outCap < pos || request == NULL || requestLen < 8)
        return 0;
    u8 responseType = request[6];
    u8 responseSub = request[7];
    if (requestType == 1)
        responseSub = 0x1a;
    else if (requestType == 2)
        responseSub = 20;
    else if (requestType == 3)
        responseSub = 32;

    if (!vm_net_mock_append_game_type_response_object(request, requestLen, out, outCap, &pos,
                                                      requestType, responseType, responseSub))
        return 0;
    objectCount += 1;

    (void)requestKind;
    (void)requestSubtype;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_net_mock_object_is_split_safe(const vm_net_mock_request_object *object)
{
    if (object == NULL || object->major != 1)
        return false;

    if (object->kind == 2 && (object->subtype == 1 || object->subtype == 10))
        return true;
    if (object->kind == 4 && object->subtype == 1)
        return true;
    if (object->kind == 7 && (object->subtype == 7 || object->subtype == 18))
        return true;
    if (object->kind == 0x19 && object->subtype == 5)
        return true;
    if (object->kind == 0x63 && object->subtype == 1)
        return true;
    return false;
}

static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap);

static u32 vm_net_mock_build_single_object_request(const vm_net_mock_request_object *object, u8 *out, u32 outCap)
{
    u32 len = 0;
    u32 objectLen = 0;
    if (object == NULL || out == NULL)
        return 0;
    objectLen = (u32)object->payloadLen + 5;
    len = (u32)object->payloadLen + 9;
    if (len > outCap || len > 0xffff)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(len >> 8);
    out[3] = (u8)len;
    out[4] = object->major;
    out[5] = object->kind;
    out[6] = object->subtype;
    out[7] = (u8)(objectLen >> 8);
    out[8] = (u8)objectLen;
    if (object->payloadLen != 0)
        memcpy(out + 9, object->payload, object->payloadLen);
    return len;
}

static bool vm_net_mock_append_response_objects(u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
                                                const u8 *response, u32 responseLen)
{
    u32 offset = 5;
    if (out == NULL || pos == NULL || objectCount == NULL ||
        response == NULL || responseLen < 5 || response[0] != 'W' || response[1] != 'T')
    {
        return false;
    }

    while (offset + 6 <= responseLen)
    {
        u32 objectLen = ((u32)response[offset + 4] << 8) | (u32)response[offset + 5];
        if (objectLen < 6 || offset + objectLen > responseLen)
            return false;
        if (*objectCount == 0xff || *pos + objectLen > outCap)
            return false;
        memcpy(out + *pos, response + offset, objectLen);
        *pos += objectLen;
        *objectCount += 1;
        offset += objectLen;
    }
    return offset == responseLen;
}

static u32 vm_net_mock_build_split_safe_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 subCount = 0;
    vm_net_mock_request_object object;
    u8 subRequest[512];
    u8 subResponse[8192];

    if (g_netMockSplitProbe || request == NULL || requestLen < 9 || outCap < pos)
        return 0;
    if (request[0] != 'W' || request[1] != 'T')
        return 0;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u32 subRequestLen = 0;
        u32 subResponseLen = 0;
        if (!vm_net_mock_object_is_split_safe(&object))
            return 0;
        subRequestLen = vm_net_mock_build_single_object_request(&object, subRequest, sizeof(subRequest));
        if (subRequestLen == 0)
            return 0;
        g_netMockSplitProbe = true;
        subResponseLen = vm_net_mock_build_response(subRequest, subRequestLen, subResponse, sizeof(subResponse));
        g_netMockSplitProbe = false;
        if (subResponseLen == 0)
            return 0;
        if (!vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount, subResponse, subResponseLen))
            return 0;
        ++subCount;
    }

    if (offset != requestLen || subCount < 2 || objectCount == 0)
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen == 0 || outCap == 0)
        return 0;

    /* First principle for protocol research: only emulate server/API behavior.
     * Do not advance CBE state here by forcing return values or writing globals.
     */
    u32 hookedLen = 0;

    if (vm_net_mock_request_contains(request, requestLen, "start") &&
        (vm_net_mock_request_contains(request, requestLen, "id") ||
         vm_net_mock_request_contains(request, requestLen, "name")))
    {
        hookedLen = vm_net_mock_build_update_chunk_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-update-chunk", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_scene_interaction_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-interaction-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_actor_query14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-actor-query14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_info14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-info14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_page14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-page14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_buy17_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-buy17", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_items_books_combo_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-items-books-combo", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (g_netMockShop17ListPending && vm_net_mock_is_backpack_open_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_shop_items_books_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-shop-items-books", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (g_netMockShop17ListPending && vm_net_mock_is_backpack_items_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_shop_items17_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-shop-items17", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_settings_unstuck_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-settings-unstuck", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_role_action23_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-role-action23", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_ctrl_page_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-ctrl-page", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_response_from_rules(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("rule", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_penglai02_repeat_scene_change_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-penglai02-repeat", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_current_scene_completion_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-current-scene-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_current_scene_repeat_scene_change_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-current-scene-repeat", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_scene_change_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_scene_change_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-scene-change", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4))
    {
        hookedLen = vm_net_mock_build_type27_followup_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-type27-followup", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_scene_change_post_enter_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-post-enter-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_mmgame_scene_transfer_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-mmgame-scene-transfer-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_teleport_stone_direct_enter_default_ack_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-teleport-stone-direct-enter-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_current_scene_reload_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-current-reload", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-scene-default-event", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_actor_other_scene_default_combo_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-other-scene-default-combo", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x63, 1))
    {
        hookedLen = vm_net_mock_build_short_wt_control_echo_response(request, requestLen, 0x63, 1, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-short-63-1-echo", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 7, 18))
    {
        hookedLen = vm_net_mock_build_practise_info18_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-practise-info18", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_battle_death_prompt_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-battle-death-prompt-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_item_use_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-item-use", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_item_discard_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-item-discard", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_backpack_open_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_backpack_open_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-backpack-open", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_backpack_items_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_backpack_items_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-backpack-items", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_teleport_stone_list_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_teleport_stone_list_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-teleport-stone-list", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 teleportStoneSubtype = 0;
    if (vm_net_mock_is_teleport_stone_transfer_request(request, requestLen, &teleportStoneSubtype))
    {
        hookedLen = vm_net_mock_build_teleport_stone_transfer_response(request, requestLen, teleportStoneSubtype, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-teleport-stone-transfer", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    hookedLen = vm_net_mock_build_teleport_stone_map_transfer_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-teleport-stone-map-transfer", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_teleport_stone_post_enter_combo_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-teleport-stone-post-enter", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_current_scene_task_subset_followup_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_scene_task_subset_followup_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-scene-task-subset-followup-current-scene", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_scene_resource_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-resource-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_task_subset_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-task-subset-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_actor_moveinfo_ack_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-actor-moveinfo-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_actor_moveinfo_name_update_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-actor-moveinfo-name-update", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_special_scene_interaction_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-special-scene-interaction", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_battle_item_use_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-battle-item-use", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_battle_operate_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-battle-operate", request, requestLen, hookedLen);
        return hookedLen;
    }
    hookedLen = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-battle-operate-fallback", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_battle_auto12_cancel_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-battle-auto12-cancel", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_challenge_interaction_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-challenge-interaction", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_actor_other_only10_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_actor_other_portal_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-other-portal", request, requestLen, hookedLen);
            return hookedLen;
        }

        hookedLen = vm_net_mock_build_actor_other_only10_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-other-only10", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_login_tail_skill_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_login_tail_skill_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-login-tail-skill", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_title_server_select_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_title_server_select_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-title-server-select", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 requestKind = 0;
    u8 requestSubtype = 0;
    if (vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &requestKind, &requestSubtype))
    {
        if (vm_net_mock_is_title_role_create_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_role_create_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-role-create", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_title_role_delete_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_role_delete_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-role-delete", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_title_role_select_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_role_select_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-role-select", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_title_rolelist_stage_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_rolelist_wait_server_select_response(out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-rolelist-wait-server-select", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (requestKind == 0x0a && requestSubtype == 0x20)
        {
            hookedLen = vm_net_mock_build_role_list_response(out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-role-list", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
    }

    if (vm_net_mock_is_login_validation_request(request, requestLen, &requestSubtype))
    {
        vm_net_ensure_business_callback("login-request");
        hookedLen = vm_net_mock_load_response_file("net_mocks/login.bin", out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("file-login", request, requestLen, hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_login_response(request, requestLen, requestSubtype, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-login", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 gameRequestType = 0;
    hookedLen = vm_net_mock_build_group_type1_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-group-type1", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_get_object_u8_field(request, requestLen, "type", &gameRequestType) &&
        (gameRequestType == 1 || gameRequestType == 2 || gameRequestType == 3))
    {
        hookedLen = vm_net_mock_build_game_type_response(request, requestLen, out, outCap, gameRequestType);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-game-type", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "version"))
    {
        vm_net_mock_title_login_phase_reset("version-handshake");
        hookedLen = vm_net_mock_build_version_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-version", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    hookedLen = vm_net_mock_build_split_safe_combo_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-split-safe-combo", request, requestLen, hookedLen);
        return hookedLen;
    }
    if (requestKind == 4 && requestSubtype == 2)
    {
        hookedLen = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-battle-operate-lastchance-fallback", request, requestLen, hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_battle_operate_response_raw82(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-battle-operate-raw82", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    vm_net_log_unhandled_packet(request, requestLen);
    assert(0);
    return 0;
}

static u32 vm_net_mock_sync_response_to_vm(void)
{
    if (g_netMockResponseLen == 0)
        return 0;

    g_netMockResponseVmPtr = vm_malloc(g_netMockResponseLen);
    if (g_netMockResponseVmPtr == 0)
        return 0;
    uc_mem_write(MTK, g_netMockResponseVmPtr, g_netMockResponse, g_netMockResponseLen);
    return g_netMockResponseVmPtr;
}

static u32 vm_net_mock_sync_buffer_to_vm(const u8 *buffer, u32 bufferLen)
{
    u32 responsePtr = 0;
    if (buffer == NULL || bufferLen == 0)
        return 0;
    responsePtr = vm_malloc(bufferLen);
    if (responsePtr == 0)
        return 0;
    uc_mem_write(MTK, responsePtr, buffer, bufferLen);
    return responsePtr;
}

static void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
{
    if (dataPtr == 0 || dataLen == 0)
        return;

    g_netLastHandledValid = 0;
    g_netLastHandledResponseLen = 0;
    g_netLastHandledSource[0] = 0;
    g_netLastHandledSummary[0] = 0;

    u8 request[512];
    u32 readLen = dataLen < sizeof(request) ? dataLen : sizeof(request);
    uc_mem_read(MTK, dataPtr, request, readLen);
    bool closeAfterData = vm_net_mock_request_contains(request, readLen, "version") &&
                          !vm_net_mock_request_contains(request, readLen, "start") &&
                          vm_net_mock_has_installed_update();
    u8 requestWtKind = 0;
    u8 requestWtSubtype = 0;
    bool haveWtHeader = vm_net_mock_get_wt_header_kind_subtype(request, readLen,
                                                              &requestWtKind,
                                                              &requestWtSubtype);
    /*
     * Empty 25/5 scene-default sends are emitted from inside the client's wait
     * callback. Dispatching the response reentrantly lets the callback clear
     * R9+0x5531 before the send wrapper writes it back to 1, leaving later WT
     * requests blocked. Queue it like a normal async network response instead.
     */
    bool immediateFlushAfterData = false;
    u32 responseEventType = 7;

    g_netMockResponseLen = vm_net_mock_build_response(request, readLen, g_netMockResponse, sizeof(g_netMockResponse));
    /*
     * Teleport-stone 16/3 now returns a main-business 30/1 scene-enter object.
     * Keep it on the normal async queue; flushing it from inside the 16/3 send
     * stack re-enters the scene screen before the caller has unwound, and the
     * new screen then stalls before emitting its post-enter follow-up request.
     */
    {
        const char *source = g_netLastHandledValid ? g_netLastHandledSource : "-";
        if (haveWtHeader)
        {
            vm_autotest_note("net_send connect=%u wt=%u/%u len=%u source=%s resp=%u\n",
                             connectId, requestWtKind, requestWtSubtype, readLen,
                             source,
                             g_netMockResponseLen);
            if (g_netMockResponseLen ||
                strstr(source, "settings") != NULL ||
                strstr(source, "teleport") != NULL ||
                strstr(source, "scene") != NULL ||
                strstr(source, "mmgame") != NULL)
            {
                printf("[info][network] net_send connect=%u wt=%u/%u len=%u source=%s resp=%u\n",
                       connectId, requestWtKind, requestWtSubtype, readLen, source, g_netMockResponseLen);
            }
        }
        else
        {
            vm_autotest_note("net_send connect=%u malformed len=%u source=%s resp=%u\n",
                             connectId, readLen,
                             source,
                             g_netMockResponseLen);
            if (g_netMockResponseLen)
                printf("[info][network] net_send connect=%u malformed len=%u source=%s resp=%u\n",
                       connectId, readLen, source, g_netMockResponseLen);
        }
    }
    g_netMockResponseOffset = 0;
    g_netUpLinkData += dataLen;
    if (g_netMockResponseLen == 0)
        return;

    u32 responsePtr = vm_net_mock_sync_response_to_vm();
    if (responsePtr == 0)
        return;
    g_netDownLinkData += g_netMockResponseLen;
    vm_net_channel *channel = scheduler_find_net_channel(connectId);
    if (channel && channel->callback)
    {
        scheduler_queue_net_event(responseEventType, responsePtr, g_netMockResponseLen, g_netMockResponseLen, channel->callback, channel->context);
        printf("[info][network] net_queue_data connect=%u event=%u resp=%u cb=%08x ctx=%08x depth=%u\n",
               connectId, responseEventType, g_netMockResponseLen,
               channel->callback, channel->context, g_netTaskDispatchDepth);
        if (immediateFlushAfterData && g_netTaskDispatchDepth == 0)
        {
            g_netDebugReadWindow = 8;
            uc_err flushErr = scheduler_dispatch_net_tasks();
            printf("[info][network] net_flush_data connect=%u wt=%u/%u resp=%u err=%u\n",
                   connectId, requestWtKind, requestWtSubtype, g_netMockResponseLen, flushErr);
            (void)flushErr;
        }
        else if (immediateFlushAfterData)
        {
            printf("[info][network] net_flush_deferred connect=%u wt=%u/%u resp=%u depth=%u\n",
                   connectId, requestWtKind, requestWtSubtype,
                   g_netMockResponseLen, g_netTaskDispatchDepth);
        }
        if (closeAfterData)
        {
            scheduler_queue_net_event(9, 0, 0, 0, channel->callback, channel->context);
        }
    }
}

static u32 vm_net_mock_read_data(u32 dst, u32 dstLen)
{
    static u32 s_netReadObserveCount = 0;
    if (dst == 0 || dstLen == 0 || g_netMockResponseOffset >= g_netMockResponseLen)
    {
        if (g_netDebugReadWindow)
        {
            printf("[info][network] net_read_empty dst=%08x ask=%u offset=%u total=%u window=%u\n",
                   dst, dstLen, g_netMockResponseOffset, g_netMockResponseLen,
                   g_netDebugReadWindow);
            --g_netDebugReadWindow;
        }
        return vm_set_call_result(0);
    }

    u32 remain = g_netMockResponseLen - g_netMockResponseOffset;
    u32 copyLen = dstLen < remain ? dstLen : remain;
    uc_mem_write(MTK, dst, g_netMockResponse + g_netMockResponseOffset, copyLen);
    if (g_netDebugReadWindow)
    {
        u32 off = g_netMockResponseOffset;
        printf("[info][network] net_read_data dst=%08x ask=%u copied=%u offset=%u total=%u head=%02x %02x %02x %02x %02x\n",
               dst, dstLen, copyLen, off, g_netMockResponseLen,
               off + 0 < g_netMockResponseLen ? g_netMockResponse[off + 0] : 0,
               off + 1 < g_netMockResponseLen ? g_netMockResponse[off + 1] : 0,
               off + 2 < g_netMockResponseLen ? g_netMockResponse[off + 2] : 0,
               off + 3 < g_netMockResponseLen ? g_netMockResponse[off + 3] : 0,
               off + 4 < g_netMockResponseLen ? g_netMockResponse[off + 4] : 0);
        --g_netDebugReadWindow;
    }
    g_netMockResponseOffset += copyLen;
    if (s_netReadObserveCount < 20)
    {
        ++s_netReadObserveCount;
        vm_autotest_note("net_read dst=%08x ask=%u copied=%u offset=%u total=%u\n",
                         dst, dstLen, copyLen, g_netMockResponseOffset, g_netMockResponseLen);
    }
    return vm_set_call_result(copyLen);
}
