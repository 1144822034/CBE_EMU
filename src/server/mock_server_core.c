/*
 * Embedded Jianghu OL mock-server implementation.
 *
 * This file is intentionally included by main.c instead of compiled as a
 * separate translation unit for now. The mock still depends on emulator-local
 * static helpers and runtime globals; this split only moves server behavior out
 * of the main emulator source without changing linkage or guest-visible logic.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include "../mysql-client.h"
#include "../md5.h"

typedef struct
{
    const char *name;
    const char *contains;
    const char *responseFile;
    const u8 *response;
    u32 responseLen;
} vm_net_mock_rule;

/* Timed stat effects are battle-local.  They deliberately never mutate the
 * durable role row: skill.dsh duration is measured in battle rounds. */
typedef struct
{
    u8 remainingRounds;
    int32_t strength;
    int32_t agility;
    int32_t wisdom;
    int32_t attack;
    int32_t defense;
    int32_t crit;
    int32_t hit;
    int32_t dodge;
    int32_t resist;
} vm_net_mock_battle_stat_modifier;

static u8 g_netMockTitleServerListPending = 0;
static u8 g_netMockTitleServerSelectConfirmed = 0;
static u32 g_netMockBackpackGridSeededRoleId = 0;
static u8 g_netMockShop17ListPending = 0;
static u32 g_netMockTitleServerListTick = 0;
static u32 g_netMockTitleServerSelectTick = 0;
static u32 g_netMockTitleSelectedServerId = 0;
static u8 g_netMockBackpackPreferRoleListAfterShopBuy = 0;
static bool g_vm_net_mock_update_completed_reenter_pending = false;
static char g_vm_net_mock_update_completed_name[64];
static u32 g_vm_net_mock_remote_completed_scene_target_serial = 0;
static const char *g_vm_mock_service_active_account_id = NULL;
static u8 g_vmNetMockFollowupResponse[65536];
static u32 g_vmNetMockFollowupResponseLen = 0;
static u32 g_vmNetMockFollowupResponseEventType = 7;
/* Nonzero only while a service-side team battle operation is being built.
 * The values describe the subtype-5 right-side roster in server wire order. */
static u8 g_vm_net_mock_team_battle_party_count_current = 0;
static u8 g_vm_net_mock_team_battle_actor_slot_current = 0;
/* The active team-battle operation snapshots all three party HP rows before a
 * skill is evaluated.  Friendly group skills update this authoritative copy;
 * finish_operation then commits it atomically to the shared team state. */
static u8 g_vm_net_mock_team_battle_member_count_current = 0;
static u32 g_vm_net_mock_team_battle_member_hp_current[3] = {0, 0, 0};
static u32 g_vm_net_mock_team_battle_member_hp_max_current[3] = {0, 0, 0};
static u8 g_vm_net_mock_team_battle_group_hp_changed_mask = 0;
static vm_net_mock_battle_stat_modifier
    g_vm_net_mock_team_battle_member_modifiers_current[3];
static u8 g_vm_net_mock_team_battle_group_modifier_changed_mask = 0;
/* The role currently evaluating a battle action reads this copy.  In a solo
 * battle it is the durable-in-session copy below; team prepare_operation
 * replaces it with the acting member's shared snapshot. */
static vm_net_mock_battle_stat_modifier g_vm_net_mock_battle_active_modifier_current;
static vm_net_mock_battle_stat_modifier g_vm_net_mock_battle_solo_modifier;
/* The ordinary solo builder bundles monster actions with every offensive
 * operation.  During a synchronized party battle this flag is armed only for
 * the last still-alive member that has not acted in the current round. */
static u8 g_vm_net_mock_team_battle_resolve_monsters_current = 0;

#define VM_MOCK_SERVICE_FRAME_SIZE 20
#define VM_MOCK_SERVICE_REQUEST_FLAG_PING 0x1u
#define VM_MOCK_SERVICE_REQUEST_FLAG_SCENE_SYNC_POLL 0x2u
#define VM_MOCK_SERVICE_REQUEST_FLAG_CLIENT_DISCONNECT 0x4u
#define VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS 5000
#define VM_MOCK_SERVICE_RESPONSE_FLAG_CLOSE_AFTER_DATA 0x1u

#ifdef _WIN32
typedef SOCKET vm_mock_service_socket;
#define VM_MOCK_SERVICE_INVALID_SOCKET INVALID_SOCKET
#else
typedef int vm_mock_service_socket;
#define VM_MOCK_SERVICE_INVALID_SOCKET (-1)
#endif

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

static u8 vm_net_mock_battle_enemy_count_current(void)
{
    if (g_mockBattleEnemyCountCurrent < 1)
        return 1;
    if (g_mockBattleEnemyCountCurrent > 3)
        return 3;
    return g_mockBattleEnemyCountCurrent;
}

/* subtype-5 does not leave the action/display table in the same order as the
 * parsed battle-start rows.  sub_6BF0(0x6BF0) copies the right-side party
 * first and the left-side monsters after it.  The wire conversion functions
 * sub_2B26/CalcTargetSideIndex then rotate that display index by the original
 * left/right counts.  Keep this exact permutation for every team action:
 *
 *   display -> wire: display >= monsters ? display-monsters : party+display
 *   wire -> display: wire < party ? monsters+wire : wire-party
 */
static u8 vm_net_mock_team_battle_display_to_wire_slot(u8 displaySlot)
{
    u8 partyCount = g_vm_net_mock_team_battle_party_count_current;
    u8 monsterCount = vm_net_mock_battle_enemy_count_current();

    return displaySlot >= monsterCount
               ? (u8)(displaySlot - monsterCount)
               : (u8)(partyCount + displaySlot);
}

static u8 vm_net_mock_team_battle_wire_to_display_slot(u8 wireSlot)
{
    u8 partyCount = g_vm_net_mock_team_battle_party_count_current;
    u8 monsterCount = vm_net_mock_battle_enemy_count_current();

    return wireSlot < partyCount
               ? (u8)(monsterCount + wireSlot)
               : (u8)(wireSlot - partyCount);
}

static void vm_net_mock_battle_default_wire_slots(bool playerOnRight, u8 side,
                                                  u8 *playerSlotOut, u8 *enemySlotOut)
{
    u8 playerSlot = playerOnRight ? 1 : 0;
    u8 enemySlot = playerOnRight ? 0 : 1;

    if (side == 1)
    {
        if (g_mockBattleSceneMonsterStartActive != 0 &&
            playerOnRight &&
            g_vm_net_mock_team_battle_party_count_current >= 2)
        {
            u8 firstMonsterDisplay = g_vm_net_mock_team_battle_party_count_current;

            playerSlot = vm_net_mock_team_battle_display_to_wire_slot(
                g_vm_net_mock_team_battle_actor_slot_current);
            enemySlot = vm_net_mock_team_battle_display_to_wire_slot(firstMonsterDisplay);
        }
        /*
         * Battle.cbm CalcTargetSideIndex(0x6CE8) remaps wire slots through the
         * side/group counts loaded by the battle-start packet. For subtype 5
         * with side=1, runtime action evidence shows the player attack record
         * must use actor wire 1 and monster target wire 0. The request-side
         * index is still translated separately below, because Callback_Unknown2()
         * reports the selected unit through sub_2B26().
         */
        else if (g_mockBattleSceneMonsterStartActive != 0)
        {
            playerSlot = 1;
            enemySlot = 0;
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

static u32 vm_net_mock_battle_apply_damage_to_role(u32 damage)
{
    u32 actualDamage = damage;

    if (g_mockBattleRoleHpCurrent == 0)
        return 0;
    if (actualDamage == 0)
        actualDamage = 1;
    if (actualDamage > g_mockBattleRoleHpCurrent)
        actualDamage = g_mockBattleRoleHpCurrent;
    g_mockBattleRoleHpCurrent -= actualDamage;
    return actualDamage;
}

static u32 vm_net_mock_battle_delta_display_value(u32 value)
{
    return (value & 0x80000000u) ? (u32)(0u - value) : value;
}

static void vm_net_mock_battle_sync_enemy_hp_totals(void)
{
    u8 enemyCount = vm_net_mock_battle_enemy_count_current();
    u32 hpTotal = 0;
    u32 hpMaxTotal = 0;

    for (u8 i = 0; i < 3; ++i)
    {
        if (i >= enemyCount)
            continue;
        if (hpTotal > 0xffffffffu - g_mockBattleEnemyHpSlots[i])
            hpTotal = 0xffffffffu;
        else
            hpTotal += g_mockBattleEnemyHpSlots[i];
        if (hpMaxTotal > 0xffffffffu - g_mockBattleEnemyHpMaxSlots[i])
            hpMaxTotal = 0xffffffffu;
        else
            hpMaxTotal += g_mockBattleEnemyHpMaxSlots[i];
    }
    if (hpMaxTotal < hpTotal)
        hpMaxTotal = hpTotal;
    g_mockBattleEnemyHpCurrent = hpTotal;
    g_mockBattleEnemyHpMax = hpMaxTotal;
}

static bool vm_net_mock_battle_enemy_wire_to_index(u8 wireSlot,
                                                   bool playerOnRight,
                                                   u8 side,
                                                   u8 fallbackEnemySlot,
                                                   u8 *enemyIndexOut)
{
    if (g_mockBattleSceneMonsterStartActive != 0 && playerOnRight && side == 1 &&
        g_vm_net_mock_team_battle_party_count_current >= 2)
    {
        u8 partyCount = g_vm_net_mock_team_battle_party_count_current;
        u8 enemyCount = vm_net_mock_battle_enemy_count_current();
        u8 displaySlot = vm_net_mock_team_battle_wire_to_display_slot(wireSlot);

        if (displaySlot >= partyCount &&
            displaySlot < (u8)(partyCount + enemyCount))
        {
            if (enemyIndexOut)
                *enemyIndexOut = (u8)(displaySlot - partyCount);
            return true;
        }
        return false;
    }
    if (g_mockBattleSceneMonsterStartActive != 0 && playerOnRight && side == 1)
    {
        u8 enemyCount = vm_net_mock_battle_enemy_count_current();
        if (enemyCount == 1)
        {
            if (wireSlot == 0)
            {
                if (enemyIndexOut)
                    *enemyIndexOut = 0;
                return true;
            }
            return false;
        }
        if (enemyCount == 2)
        {
            if (wireSlot == 2)
            {
                if (enemyIndexOut)
                    *enemyIndexOut = 0;
                return true;
            }
            if (wireSlot == 0)
            {
                if (enemyIndexOut)
                    *enemyIndexOut = 1;
                return true;
            }
            return false;
        }
        if (wireSlot == 2)
        {
            if (enemyIndexOut)
                *enemyIndexOut = 0;
            return true;
        }
        if (wireSlot == 3)
        {
            if (enemyIndexOut)
                *enemyIndexOut = 1;
            return true;
        }
        if (wireSlot == 0)
        {
            if (enemyIndexOut)
                *enemyIndexOut = 2;
            return true;
        }
        return false;
    }

    if (wireSlot == fallbackEnemySlot)
    {
        if (enemyIndexOut)
            *enemyIndexOut = 0;
        return true;
    }
    return false;
}

static bool vm_net_mock_battle_enemy_wire_is_alive(u8 wireSlot,
                                                   bool playerOnRight,
                                                   u8 side,
                                                   u8 fallbackEnemySlot)
{
    u8 enemyIndex = 0;

    if (!vm_net_mock_battle_enemy_wire_to_index(wireSlot, playerOnRight, side,
                                                fallbackEnemySlot, &enemyIndex))
        return false;
    return enemyIndex < 3 && g_mockBattleEnemyHpSlots[enemyIndex] != 0;
}

static u8 vm_net_mock_battle_enemy_wire_for_index(u8 enemyIndex,
                                                  bool playerOnRight,
                                                  u8 side,
                                                  u8 fallbackEnemySlot);

static u8 vm_net_mock_battle_first_alive_enemy_wire(bool playerOnRight,
                                                    u8 side,
                                                    u8 fallbackEnemySlot)
{
    u8 enemyCount = vm_net_mock_battle_enemy_count_current();

    if (g_mockBattleSceneMonsterStartActive != 0 && playerOnRight && side == 1)
    {
        for (u8 enemyIndex = 0; enemyIndex < enemyCount && enemyIndex < 3; ++enemyIndex)
        {
            if (g_mockBattleEnemyHpSlots[enemyIndex] != 0)
                return vm_net_mock_battle_enemy_wire_for_index(enemyIndex,
                                                               playerOnRight,
                                                               side,
                                                               fallbackEnemySlot);
        }
        return 0;
    }
    return fallbackEnemySlot;
}

static u8 vm_net_mock_battle_enemy_wire_for_index(u8 enemyIndex,
                                                  bool playerOnRight,
                                                  u8 side,
                                                  u8 fallbackEnemySlot)
{
    u8 enemyCount = vm_net_mock_battle_enemy_count_current();

    if (g_mockBattleSceneMonsterStartActive != 0 && playerOnRight && side == 1 &&
        g_vm_net_mock_team_battle_party_count_current >= 2)
    {
        u8 partyCount = g_vm_net_mock_team_battle_party_count_current;

        if (enemyIndex >= enemyCount)
            enemyIndex = (u8)(enemyCount - 1);
        return vm_net_mock_team_battle_display_to_wire_slot(
            (u8)(partyCount + enemyIndex));
    }
    if (g_mockBattleSceneMonsterStartActive != 0 && playerOnRight && side == 1)
    {
        if (enemyIndex >= enemyCount)
            enemyIndex = (u8)(enemyCount - 1);
        if (enemyCount == 1)
            return 0;
        if (enemyCount == 2)
            return enemyIndex == 0 ? 2 : 0;
        if (enemyIndex == 0)
            return 2;
        if (enemyIndex == 1)
            return 3;
        return 0;
    }
    (void)enemyIndex;
    return fallbackEnemySlot;
}

static u8 vm_net_mock_battle_select_live_enemy_wire(u8 requestedWireSlot,
                                                    bool playerOnRight,
                                                    u8 side,
                                                    u8 fallbackEnemySlot)
{
    if (vm_net_mock_battle_enemy_wire_is_alive(requestedWireSlot, playerOnRight,
                                               side, fallbackEnemySlot))
        return requestedWireSlot;
    return vm_net_mock_battle_first_alive_enemy_wire(playerOnRight, side,
                                                    fallbackEnemySlot);
}

static u32 vm_net_mock_battle_enemy_hp_for_wire(u8 wireSlot,
                                                bool playerOnRight,
                                                u8 side,
                                                u8 fallbackEnemySlot)
{
    u8 enemyIndex = 0;

    if (!vm_net_mock_battle_enemy_wire_to_index(wireSlot, playerOnRight, side,
                                                fallbackEnemySlot, &enemyIndex) ||
        enemyIndex >= 3)
        return g_mockBattleEnemyHpCurrent;
    return g_mockBattleEnemyHpSlots[enemyIndex];
}

static void vm_net_mock_battle_damage_enemy_wire(u8 wireSlot,
                                                 bool playerOnRight,
                                                 u8 side,
                                                 u8 fallbackEnemySlot,
                                                 u32 damage)
{
    u8 enemyIndex = 0;

    if (!vm_net_mock_battle_enemy_wire_to_index(wireSlot, playerOnRight, side,
                                                fallbackEnemySlot, &enemyIndex) ||
        enemyIndex >= 3)
    {
        if (g_mockBattleEnemyHpCurrent >= damage)
            g_mockBattleEnemyHpCurrent -= damage;
        else
            g_mockBattleEnemyHpCurrent = 0;
        return;
    }

    if (g_mockBattleEnemyHpSlots[enemyIndex] >= damage)
        g_mockBattleEnemyHpSlots[enemyIndex] -= damage;
    else
        g_mockBattleEnemyHpSlots[enemyIndex] = 0;
    vm_net_mock_battle_sync_enemy_hp_totals();
}

static u8 vm_net_mock_battle_target_wire_slot_from_request(u8 requestIndex,
                                                           bool playerOnRight,
                                                           u8 side,
                                                           u8 fallbackEnemySlot)
{
    if (g_mockBattleSceneMonsterStartActive != 0 && playerOnRight && side == 1 &&
        g_vm_net_mock_team_battle_party_count_current >= 2)
    {
        if (vm_net_mock_battle_enemy_wire_is_alive(requestIndex,
                                                   playerOnRight,
                                                   side,
                                                   fallbackEnemySlot))
            return requestIndex;
        return vm_net_mock_battle_first_alive_enemy_wire(playerOnRight,
                                                         side,
                                                         fallbackEnemySlot);
    }
    if (g_mockBattleSceneMonsterStartActive != 0 && playerOnRight && side == 1)
    {
        u8 enemyCount = vm_net_mock_battle_enemy_count_current();
        if (enemyCount == 1)
            return fallbackEnemySlot;
        if (enemyCount == 2)
        {
            if (requestIndex == 2)
                return 2;
            if (requestIndex == 0 || requestIndex == 1)
                return 0;
            return fallbackEnemySlot;
        }
        if (requestIndex == 2 || requestIndex == 3)
            return requestIndex;
        if (requestIndex == 0)
            return 0;
        if (requestIndex >= 1 && requestIndex <= enemyCount)
            return vm_net_mock_battle_first_alive_enemy_wire(playerOnRight,
                                                             side,
                                                             fallbackEnemySlot);
        return fallbackEnemySlot;
    }
    return requestIndex;
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
#ifdef CBE_SERVER_ONLY
    /* Network callbacks live in the CBE process.  A remote service sends the
     * completed WT frame; the client transport schedules that callback. */
    (void)reason;
    return;
#else
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
#endif
}

static u32 vm_net_mock_copy_response(const u8 *response, u32 responseLen, u8 *out, u32 outCap)
{
    if (response == NULL || responseLen == 0 || out == NULL || outCap == 0)
        return 0;
    u32 copyLen = responseLen < outCap ? responseLen : outCap;
    memcpy(out, response, copyLen);
    return copyLen;
}

static char g_vm_net_mock_resource_dir[1024];
static long vm_net_mock_file_size(const char *path);
static long vm_net_mock_update_file_size(const char *name);
static u32 g_vm_mock_service_active_client_id;
/* The headless service assigns this before dispatching every game packet.  The
 * startup updater uses it to associate the pre-login 18/9 request with the
 * later 18/6 chunk requests without relying on account state. */

static bool vm_net_mock_set_resource_dir(const char *directory)
{
    char taskPath[1200];
    FILE *probe = NULL;
    size_t len = 0;

    if (directory == NULL || directory[0] == 0)
        return false;
    len = strlen(directory);
    snprintf(taskPath, sizeof(taskPath), "%s%stask.dsh", directory,
             (directory[len - 1] == '/' || directory[len - 1] == '\\') ? "" : "/");
    probe = fopen(taskPath, "rb");
    if (probe == NULL)
        return false;
    fclose(probe);
    snprintf(g_vm_net_mock_resource_dir, sizeof(g_vm_net_mock_resource_dir),
             "%s", directory);
    return true;
}

static const char *vm_net_mock_resource_dir(void)
{
    return g_vm_net_mock_resource_dir;
}

static bool vm_net_mock_build_configured_resource_path(const char *name,
                                                       char *out,
                                                       size_t outCap)
{
    size_t dirLen = strlen(g_vm_net_mock_resource_dir);
    if (out == NULL || outCap == 0 || name == NULL || name[0] == 0 || dirLen == 0)
        return false;
    return snprintf(out, outCap, "%s%s%s", g_vm_net_mock_resource_dir,
                    (g_vm_net_mock_resource_dir[dirLen - 1] == '/' ||
                     g_vm_net_mock_resource_dir[dirLen - 1] == '\\') ? "" : "/",
                    name) < (int)outCap;
}

enum
{
    VM_NET_MOCK_UPDATE_SLOT_COUNT = 4,
    VM_NET_MOCK_UPDATE_NAMED_MAX = 128,
    VM_NET_MOCK_UPDATE_DELIVERY_MAX = 256,
    VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX = 64,
    VM_NET_MOCK_UPDATE_PAYLOAD_MAX = 1024 * 1024
};

typedef struct
{
    bool enabled;
    u16 version;
} vm_net_mock_update_slot_config;

typedef struct
{
    bool enabled;
    u16 version;
    char name[128];
} vm_net_mock_update_named_config;

typedef struct
{
    u32 identityHash;
    u16 deliveredVersions[VM_NET_MOCK_UPDATE_SLOT_COUNT];
} vm_net_mock_update_delivery;

typedef struct
{
    u32 clientId;
    u32 identityHash;
} vm_net_mock_update_client_map;

static const char *g_vm_net_mock_update_slot_files[VM_NET_MOCK_UPDATE_SLOT_COUNT] = {
    "mmTitleMstarWqvga.cbm",
    "mmGameMstarWqvga.cbm",
    "mmBattleMstarWqvga.cbm",
    "mmShopMstarWqvga.cbm",
};
static const char *g_vm_net_mock_update_slot_labels[VM_NET_MOCK_UPDATE_SLOT_COUNT] = {
    "Title（登录模块）",
    "Game（场景与玩法模块）",
    "Battle（战斗模块）",
    "Shop（商店模块）",
};
static vm_net_mock_update_slot_config
    g_vm_net_mock_update_slots[VM_NET_MOCK_UPDATE_SLOT_COUNT];
static vm_net_mock_update_named_config
    g_vm_net_mock_update_named[VM_NET_MOCK_UPDATE_NAMED_MAX];
static u32 g_vm_net_mock_update_named_count = 0;
static vm_net_mock_update_delivery
    g_vm_net_mock_update_deliveries[VM_NET_MOCK_UPDATE_DELIVERY_MAX];
static u32 g_vm_net_mock_update_delivery_count = 0;
static vm_net_mock_update_client_map
    g_vm_net_mock_update_client_maps[VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX];
static bool g_vm_net_mock_update_catalog_loaded = false;
static bool g_vm_net_mock_update_delivery_loaded = false;

static bool vm_net_mock_update_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == 0 || strstr(name, "..") != NULL)
        return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
    {
        if (*p == '/' || *p == '\\' || *p == '\r' || *p == '\n' || *p == '\t' ||
            *p < 0x20)
            return false;
    }
    return true;
}

static bool vm_net_mock_update_resource_path(const char *leaf,
                                             char *out,
                                             size_t outCap)
{
    static const char *fallbackRoots[] = {
        "../web/fs/JHOnlineData",
        "web/fs/JHOnlineData",
        "JHOnlineData"
    };
    if (!vm_net_mock_update_name_is_safe(leaf))
        return false;
    if (vm_net_mock_build_configured_resource_path(leaf, out, outCap))
        return true;
    for (u32 i = 0; i < sizeof(fallbackRoots) / sizeof(fallbackRoots[0]); ++i)
    {
        char probePath[1200];
        FILE *probe = NULL;
        snprintf(probePath, sizeof(probePath), "%s/task.dsh", fallbackRoots[i]);
        probe = fopen(probePath, "rb");
        if (probe == NULL)
            continue;
        fclose(probe);
        return snprintf(out, outCap, "%s/%s", fallbackRoots[i], leaf) <
               (int)outCap;
    }
    return false;
}

static bool vm_net_mock_get_object_entry_bytes(const u8 *payload,
                                                u32 payloadLen,
                                                const char *field,
                                                const u8 **valueOut,
                                                u16 *valueLenOut)
{
    u32 pos = 0;
    u32 fieldLen = field ? (u32)strlen(field) : 0;

    if (valueOut)
        *valueOut = NULL;
    if (valueLenOut)
        *valueLenOut = 0;
    if (payload == NULL || fieldLen == 0 || fieldLen > 0xff)
        return false;

    while (pos < payloadLen)
    {
        u8 nameLen = payload[pos++];
        u16 valueLen = 0;
        const u8 *name = NULL;
        const u8 *value = NULL;

        if (nameLen == 0 || pos + nameLen + 2 > payloadLen)
            return false;
        name = payload + pos;
        pos += nameLen;
        valueLen = (u16)(((u16)payload[pos] << 8) | payload[pos + 1]);
        pos += 2;
        if (pos + valueLen > payloadLen)
            return false;
        value = payload + pos;
        if (nameLen == fieldLen && memcmp(name, field, fieldLen) == 0)
        {
            if (valueOut)
                *valueOut = value;
            if (valueLenOut)
                *valueLenOut = valueLen;
            return true;
        }
        pos += valueLen;
    }
    return false;
}

static bool vm_net_mock_update_state_path(const char *leaf,
                                          char *out,
                                          size_t outCap)
{
    return vm_net_mock_update_resource_path(leaf, out, outCap);
}

static bool vm_net_mock_update_parse_u32(const char *text, u32 *valueOut)
{
    char *end = NULL;
    unsigned long value = 0;
    if (text == NULL || text[0] == 0)
        return false;
    value = strtoul(text, &end, 10);
    if (end == text || *end != 0 || value > 0xfffffffful)
        return false;
    if (valueOut)
        *valueOut = (u32)value;
    return true;
}

static void vm_net_mock_update_catalog_defaults(void)
{
    memset(g_vm_net_mock_update_slots, 0, sizeof(g_vm_net_mock_update_slots));
    memset(g_vm_net_mock_update_named, 0, sizeof(g_vm_net_mock_update_named));
    g_vm_net_mock_update_named_count = 0;
    for (u32 i = 0; i < VM_NET_MOCK_UPDATE_SLOT_COUNT; ++i)
        g_vm_net_mock_update_slots[i].version = 1;
}

static void vm_net_mock_update_catalog_load(void)
{
    char path[1200];
    char line[512];
    FILE *fp = NULL;

    if (g_vm_net_mock_update_catalog_loaded)
        return;
    g_vm_net_mock_update_catalog_loaded = true;
    vm_net_mock_update_catalog_defaults();
    if (!vm_net_mock_update_state_path("server_update_catalog.tsv", path,
                                       sizeof(path)))
        return;
    fp = fopen(path, "rb");
    if (fp == NULL)
        return;
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *kind = NULL;
        char *a = NULL;
        char *b = NULL;
        char *c = NULL;
        u32 first = 0;
        u32 enabled = 0;
        u32 version = 0;

        line[strcspn(line, "\r\n")] = 0;
        kind = strtok(line, "\t");
        a = strtok(NULL, "\t");
        b = strtok(NULL, "\t");
        c = strtok(NULL, "\t");
        if (kind == NULL || kind[0] == '#' || a == NULL || b == NULL || c == NULL ||
            !vm_net_mock_update_parse_u32(a, &first) ||
            !vm_net_mock_update_parse_u32(b, &enabled))
            continue;
        if (strcmp(kind, "slot") == 0)
        {
            if (first < 1 || first > VM_NET_MOCK_UPDATE_SLOT_COUNT ||
                !vm_net_mock_update_parse_u32(c, &version) || version == 0 ||
                version > 0xffff)
                continue;
            g_vm_net_mock_update_slots[first - 1].enabled = enabled != 0;
            g_vm_net_mock_update_slots[first - 1].version = (u16)version;
        }
        else if (strcmp(kind, "named") == 0 &&
                 g_vm_net_mock_update_named_count < VM_NET_MOCK_UPDATE_NAMED_MAX)
        {
            /* Named rows are: named<TAB>enabled<TAB>version<TAB>name. */
            version = 0;
            if (!vm_net_mock_update_parse_u32(b, &version) || version == 0 ||
                version > 0xffff || !vm_net_mock_update_name_is_safe(c))
                continue;
            vm_net_mock_update_named_config *row =
                &g_vm_net_mock_update_named[g_vm_net_mock_update_named_count++];
            row->enabled = first != 0;
            row->version = (u16)version;
            snprintf(row->name, sizeof(row->name), "%s", c);
        }
    }
    fclose(fp);
}

static bool vm_net_mock_update_catalog_save(const char **errorOut)
{
    char path[1200];
    char tempPath[1240];
    FILE *fp = NULL;

    if (errorOut)
        *errorOut = NULL;
    vm_net_mock_update_catalog_load();
    if (!vm_net_mock_update_state_path("server_update_catalog.tsv", path,
                                       sizeof(path)))
    {
        if (errorOut)
            *errorOut = "资源根目录尚未配置";
        return false;
    }
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    fp = fopen(tempPath, "wb");
    if (fp == NULL)
    {
        if (errorOut)
            *errorOut = "更新配置文件不可写";
        return false;
    }
    fprintf(fp, "# Jianghu OL server update catalog v1\n");
    for (u32 i = 0; i < VM_NET_MOCK_UPDATE_SLOT_COUNT; ++i)
    {
        fprintf(fp, "slot\t%u\t%u\t%u\n", i + 1,
                g_vm_net_mock_update_slots[i].enabled ? 1 : 0,
                g_vm_net_mock_update_slots[i].version);
    }
    for (u32 i = 0; i < g_vm_net_mock_update_named_count; ++i)
    {
        const vm_net_mock_update_named_config *row =
            &g_vm_net_mock_update_named[i];
        if (!vm_net_mock_update_name_is_safe(row->name))
            continue;
        fprintf(fp, "named\t%u\t%u\t%s\n", row->enabled ? 1 : 0,
                row->version, row->name);
    }
    if (fclose(fp) != 0)
    {
        remove(tempPath);
        if (errorOut)
            *errorOut = "更新配置写入失败";
        return false;
    }
    remove(path);
    if (rename(tempPath, path) != 0)
    {
        remove(tempPath);
        if (errorOut)
            *errorOut = "更新配置替换失败";
        return false;
    }
    return true;
}

static void vm_net_mock_update_delivery_load(void)
{
    char path[1200];
    char line[256];
    FILE *fp = NULL;

    if (g_vm_net_mock_update_delivery_loaded)
        return;
    g_vm_net_mock_update_delivery_loaded = true;
    memset(g_vm_net_mock_update_deliveries, 0,
           sizeof(g_vm_net_mock_update_deliveries));
    g_vm_net_mock_update_delivery_count = 0;
    if (!vm_net_mock_update_state_path("server_update_delivery.tsv", path,
                                       sizeof(path)))
        return;
    fp = fopen(path, "rb");
    if (fp == NULL)
        return;
    while (g_vm_net_mock_update_delivery_count < VM_NET_MOCK_UPDATE_DELIVERY_MAX &&
           fgets(line, sizeof(line), fp) != NULL)
    {
        unsigned int hash = 0;
        unsigned int versions[VM_NET_MOCK_UPDATE_SLOT_COUNT] = {0};
        if (sscanf(line, "%x\t%u\t%u\t%u\t%u", &hash, &versions[0],
                   &versions[1], &versions[2], &versions[3]) != 5 || hash == 0)
            continue;
        vm_net_mock_update_delivery *row =
            &g_vm_net_mock_update_deliveries[g_vm_net_mock_update_delivery_count++];
        row->identityHash = (u32)hash;
        for (u32 i = 0; i < VM_NET_MOCK_UPDATE_SLOT_COUNT; ++i)
            row->deliveredVersions[i] =
                versions[i] <= 0xffff ? (u16)versions[i] : 0;
    }
    fclose(fp);
}

static bool vm_net_mock_update_delivery_save(const char **errorOut)
{
    char path[1200];
    char tempPath[1240];
    FILE *fp = NULL;

    if (errorOut)
        *errorOut = NULL;
    vm_net_mock_update_delivery_load();
    if (!vm_net_mock_update_state_path("server_update_delivery.tsv", path,
                                       sizeof(path)))
    {
        if (errorOut)
            *errorOut = "资源根目录尚未配置";
        return false;
    }
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    fp = fopen(tempPath, "wb");
    if (fp == NULL)
    {
        if (errorOut)
            *errorOut = "更新下发记录不可写";
        return false;
    }
    fprintf(fp, "# identity_hash title game battle shop\n");
    for (u32 i = 0; i < g_vm_net_mock_update_delivery_count; ++i)
    {
        const vm_net_mock_update_delivery *row =
            &g_vm_net_mock_update_deliveries[i];
        fprintf(fp, "%08x\t%u\t%u\t%u\t%u\n", row->identityHash,
                row->deliveredVersions[0], row->deliveredVersions[1],
                row->deliveredVersions[2], row->deliveredVersions[3]);
    }
    if (fclose(fp) != 0)
    {
        remove(tempPath);
        if (errorOut)
            *errorOut = "更新下发记录写入失败";
        return false;
    }
    remove(path);
    if (rename(tempPath, path) != 0)
    {
        remove(tempPath);
        if (errorOut)
            *errorOut = "更新下发记录替换失败";
        return false;
    }
    return true;
}

static u32 vm_net_mock_update_hash_bytes(u32 hash, const u8 *data, u32 len)
{
    if (hash == 0)
        hash = 2166136261u;
    for (u32 i = 0; data != NULL && i < len; ++i)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static u32 vm_net_mock_update_identity_hash(const u8 *request, u32 requestLen)
{
    static const char *fields[] = {"imsi", "client", "plat", "proj"};
    char value[128];
    u32 hash = 2166136261u;
    bool haveValue = false;

    for (u32 i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        memset(value, 0, sizeof(value));
        if (!vm_net_mock_get_object_string_field(request, requestLen, fields[i],
                                                 value, sizeof(value)) ||
            value[0] == 0)
            continue;
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)fields[i],
                                             (u32)strlen(fields[i]));
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)value,
                                             (u32)strlen(value));
        haveValue = true;
    }
    return haveValue && hash != 0 ? hash : 0;
}

static void vm_net_mock_update_remember_client(u32 clientId, u32 identityHash)
{
    u32 freeIndex = VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX;
    if (clientId == 0 || identityHash == 0)
        return;
    for (u32 i = 0; i < VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX; ++i)
    {
        if (g_vm_net_mock_update_client_maps[i].clientId == clientId)
        {
            g_vm_net_mock_update_client_maps[i].identityHash = identityHash;
            return;
        }
        if (freeIndex == VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX &&
            g_vm_net_mock_update_client_maps[i].clientId == 0)
            freeIndex = i;
    }
    if (freeIndex == VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX)
        freeIndex = clientId % VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX;
    g_vm_net_mock_update_client_maps[freeIndex].clientId = clientId;
    g_vm_net_mock_update_client_maps[freeIndex].identityHash = identityHash;
}

static u32 vm_net_mock_update_identity_for_active_client(const u8 *request,
                                                         u32 requestLen)
{
    for (u32 i = 0; g_vm_mock_service_active_client_id != 0 &&
                    i < VM_NET_MOCK_UPDATE_CLIENT_MAP_MAX; ++i)
    {
        if (g_vm_net_mock_update_client_maps[i].clientId ==
            g_vm_mock_service_active_client_id)
            return g_vm_net_mock_update_client_maps[i].identityHash;
    }
    return vm_net_mock_update_identity_hash(request, requestLen);
}

static vm_net_mock_update_delivery *
vm_net_mock_update_find_delivery(u32 identityHash, bool create)
{
    vm_net_mock_update_delivery *row = NULL;
    if (identityHash == 0)
        return NULL;
    vm_net_mock_update_delivery_load();
    for (u32 i = 0; i < g_vm_net_mock_update_delivery_count; ++i)
    {
        if (g_vm_net_mock_update_deliveries[i].identityHash == identityHash)
            return &g_vm_net_mock_update_deliveries[i];
    }
    if (!create)
        return NULL;
    if (g_vm_net_mock_update_delivery_count < VM_NET_MOCK_UPDATE_DELIVERY_MAX)
        row = &g_vm_net_mock_update_deliveries[g_vm_net_mock_update_delivery_count++];
    else
        row = &g_vm_net_mock_update_deliveries[identityHash %
                                               VM_NET_MOCK_UPDATE_DELIVERY_MAX];
    memset(row, 0, sizeof(*row));
    row->identityHash = identityHash;
    return row;
}

static vm_net_mock_update_named_config *
vm_net_mock_update_find_named(const char *name)
{
    vm_net_mock_update_catalog_load();
    for (u32 i = 0; i < g_vm_net_mock_update_named_count; ++i)
    {
        if (strcmp(g_vm_net_mock_update_named[i].name, name ? name : "") == 0)
            return &g_vm_net_mock_update_named[i];
    }
    return NULL;
}

static bool vm_net_mock_update_admin_save_slot(u8 slot, u16 version,
                                               bool enabled,
                                               const char **errorOut)
{
    long size = -1;
    if (slot < 1 || slot > VM_NET_MOCK_UPDATE_SLOT_COUNT || version == 0)
    {
        if (errorOut)
            *errorOut = "模块槽位或版本号无效";
        return false;
    }
    size = vm_net_mock_update_file_size(
        g_vm_net_mock_update_slot_files[slot - 1]);
    if (enabled && (size < 1024 || size > VM_NET_MOCK_UPDATE_PAYLOAD_MAX))
    {
        if (errorOut)
            *errorOut = "模块文件不存在、过小或超过 1 MiB";
        return false;
    }
    vm_net_mock_update_catalog_load();
    g_vm_net_mock_update_slots[slot - 1].version = version;
    g_vm_net_mock_update_slots[slot - 1].enabled = enabled;
    return vm_net_mock_update_catalog_save(errorOut);
}

static bool vm_net_mock_update_admin_save_named(const char *name, u16 version,
                                                const char **errorOut)
{
    long size = -1;
    vm_net_mock_update_named_config *row = NULL;
    if (!vm_net_mock_update_name_is_safe(name) || version == 0)
    {
        if (errorOut)
            *errorOut = "资源名称或版本号无效";
        return false;
    }
    size = vm_net_mock_update_file_size(name);
    if (size <= 0 || size > VM_NET_MOCK_UPDATE_PAYLOAD_MAX)
    {
        if (errorOut)
            *errorOut = "资源文件不存在或超过 1 MiB";
        return false;
    }
    vm_net_mock_update_catalog_load();
    row = vm_net_mock_update_find_named(name);
    if (row == NULL)
    {
        if (g_vm_net_mock_update_named_count >= VM_NET_MOCK_UPDATE_NAMED_MAX)
        {
            if (errorOut)
                *errorOut = "具名资源发布项已达到上限";
            return false;
        }
        row = &g_vm_net_mock_update_named[g_vm_net_mock_update_named_count++];
        memset(row, 0, sizeof(*row));
        snprintf(row->name, sizeof(row->name), "%s", name);
    }
    row->enabled = true;
    row->version = version;
    return vm_net_mock_update_catalog_save(errorOut);
}

static bool vm_net_mock_update_admin_remove_named(const char *name,
                                                  const char **errorOut)
{
    vm_net_mock_update_catalog_load();
    for (u32 i = 0; i < g_vm_net_mock_update_named_count; ++i)
    {
        if (strcmp(g_vm_net_mock_update_named[i].name, name ? name : "") != 0)
            continue;
        if (i + 1 < g_vm_net_mock_update_named_count)
            memmove(&g_vm_net_mock_update_named[i],
                    &g_vm_net_mock_update_named[i + 1],
                    sizeof(g_vm_net_mock_update_named[0]) *
                        (g_vm_net_mock_update_named_count - i - 1));
        --g_vm_net_mock_update_named_count;
        memset(&g_vm_net_mock_update_named[g_vm_net_mock_update_named_count], 0,
               sizeof(g_vm_net_mock_update_named[0]));
        return vm_net_mock_update_catalog_save(errorOut);
    }
    if (errorOut)
        *errorOut = "该具名资源尚未发布";
    return false;
}

static bool vm_net_mock_get_object_entry_field(const u8 *request,
                                               u32 requestLen,
                                               const char *field,
                                               const u8 **value,
                                               u16 *valueLen);

static bool vm_net_mock_update_admin_reset_delivery(const char **errorOut)
{
    vm_net_mock_update_delivery_load();
    memset(g_vm_net_mock_update_deliveries, 0,
           sizeof(g_vm_net_mock_update_deliveries));
    g_vm_net_mock_update_delivery_count = 0;
    return vm_net_mock_update_delivery_save(errorOut);
}

static FILE *vm_net_mock_fopen_game_path(const char *path, const char *mode)
{
    FILE *fp = NULL;

    if (path == NULL || path[0] == 0 || mode == NULL || mode[0] == 0)
        return NULL;

    /* ASCII and already-UTF-8 paths should keep working unchanged. */
    fp = fopen(path, mode);
    if (fp != NULL)
        return fp;

#ifdef CBE_HOST_UTF8_PATHS
    /* Packet/SCE/XSE strings use GBK bytes. Linux filenames use UTF-8, so a
     * path containing a Chinese resource name must be converted at the final
     * host-filesystem boundary. */
    {
        char utf8Path[1024];
        memset(utf8Path, 0, sizeof(utf8Path));
        gbk_to_utf8((u8 *)path, (u8 *)utf8Path, sizeof(utf8Path));
        if (utf8Path[0] != 0 && strcmp(utf8Path, path) != 0)
            fp = fopen(utf8Path, mode);
    }
#endif
    return fp;
}

static FILE *vm_net_mock_fopen_response_file(const char *path)
{
    FILE *fp = NULL;

    if (path == NULL || path[0] == 0)
        return NULL;

    fp = vm_net_mock_fopen_game_path(path, "rb");
    if (fp != NULL)
        return fp;

    if (g_vm_net_mock_resource_dir[0] != 0)
    {
        static const char *prefixes[] = {
            "JHOnlineData/", "bin/JHOnlineData/", "../bin/JHOnlineData/",
            "web/fs/JHOnlineData/", "../web/fs/JHOnlineData/"
        };
        const char *leaf = NULL;
        char configuredPath[1200];
        for (u32 i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
        {
            size_t prefixLen = strlen(prefixes[i]);
            if (strncmp(path, prefixes[i], prefixLen) == 0)
            {
                leaf = path + prefixLen;
                break;
            }
        }
        if (leaf != NULL && leaf[0] != 0 &&
            vm_net_mock_build_configured_resource_path(leaf, configuredPath,
                                                       sizeof(configuredPath)))
        {
            fp = vm_net_mock_fopen_game_path(configuredPath, "rb");
            if (fp != NULL)
                return fp;
        }
    }

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
            fp = vm_net_mock_fopen_game_path(tryPath, "rb");
            if (fp != NULL)
                return fp;

            snprintf(tryPath, sizeof(tryPath), "%s\\..\\%s", exePath, path);
            fp = vm_net_mock_fopen_game_path(tryPath, "rb");
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

static long vm_net_mock_update_file_size(const char *name)
{
    char path[1200];
    FILE *fp = NULL;
    long size = -1;
    if (!vm_net_mock_update_resource_path(name, path, sizeof(path)))
        return -1;
    fp = vm_net_mock_fopen_game_path(path, "rb");
    if (fp == NULL)
        return -1;
    if (fseek(fp, 0, SEEK_END) == 0)
        size = ftell(fp);
    fclose(fp);
    return size;
}

static bool vm_net_mock_update_named_content_version(const char *name,
                                                     u16 *versionOut)
{
    char path[1200];
    FILE *fp = NULL;
    u8 buffer[8192];
    u32 hash = 2166136261u;
    bool readAny = false;

    if (versionOut)
        *versionOut = 0;
    if (!vm_net_mock_update_name_is_safe(name) ||
        !vm_net_mock_update_resource_path(name, path, sizeof(path)))
    {
        return false;
    }
    fp = vm_net_mock_fopen_game_path(path, "rb");
    if (fp == NULL)
        return false;
    hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)name,
                                         (u32)strlen(name));
    while (!feof(fp))
    {
        size_t readLen = fread(buffer, 1, sizeof(buffer), fp);
        if (readLen != 0)
        {
            readAny = true;
            hash = vm_net_mock_update_hash_bytes(hash, buffer, (u32)readLen);
        }
        if (ferror(fp))
        {
            fclose(fp);
            return false;
        }
    }
    fclose(fp);
    if (!readAny)
        return false;
    if (versionOut)
        *versionOut = (u16)((hash % 65535u) + 1u);
    return true;
}

/* Publish one Actor dependency set as a single catalog transaction.  The
 * payload remains in the authoritative server tree; clients install it only
 * after their normal WT 18/7 request. */
static bool vm_net_mock_update_admin_publish_named_files(
    const char *const *names, u32 nameCount, const char **errorOut)
{
    vm_net_mock_update_named_config previous[VM_NET_MOCK_UPDATE_NAMED_MAX];
    u16 versions[VM_NET_MOCK_UPDATE_NAMED_MAX];
    u32 previousCount = 0;
    u32 uniqueCount = 0;
    u32 missingRows = 0;

    if (errorOut)
        *errorOut = NULL;
    if (names == NULL || nameCount == 0 ||
        nameCount > VM_NET_MOCK_UPDATE_NAMED_MAX)
    {
        if (errorOut)
            *errorOut = "具名资源发布列表无效";
        return false;
    }
    memset(versions, 0, sizeof(versions));
    vm_net_mock_update_catalog_load();
    for (u32 i = 0; i < nameCount; ++i)
    {
        bool duplicate = false;
        long size = -1;

        for (u32 j = 0; j < i; ++j)
        {
            if (names[j] != NULL && names[i] != NULL &&
                strcmp(names[j], names[i]) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        if (!vm_net_mock_update_name_is_safe(names[i]) ||
            (size = vm_net_mock_update_file_size(names[i])) <= 0 ||
            size > VM_NET_MOCK_UPDATE_PAYLOAD_MAX ||
            !vm_net_mock_update_named_content_version(names[i], &versions[i]))
        {
            if (errorOut)
                *errorOut = "Actor 或引用图片无法作为具名资源发布";
            return false;
        }
        ++uniqueCount;
        if (vm_net_mock_update_find_named(names[i]) == NULL)
            ++missingRows;
    }
    if (g_vm_net_mock_update_named_count + missingRows >
        VM_NET_MOCK_UPDATE_NAMED_MAX)
    {
        if (errorOut)
            *errorOut = "具名资源发布项已达到上限";
        return false;
    }

    memcpy(previous, g_vm_net_mock_update_named, sizeof(previous));
    previousCount = g_vm_net_mock_update_named_count;
    for (u32 i = 0; i < nameCount; ++i)
    {
        bool duplicate = false;
        vm_net_mock_update_named_config *row = NULL;

        for (u32 j = 0; j < i; ++j)
        {
            if (names[j] != NULL && names[i] != NULL &&
                strcmp(names[j], names[i]) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        row = vm_net_mock_update_find_named(names[i]);
        if (row == NULL)
        {
            row = &g_vm_net_mock_update_named[g_vm_net_mock_update_named_count++];
            memset(row, 0, sizeof(*row));
            snprintf(row->name, sizeof(row->name), "%s", names[i]);
        }
        row->enabled = true;
        row->version = versions[i];
    }
    if (!vm_net_mock_update_catalog_save(errorOut))
    {
        memcpy(g_vm_net_mock_update_named, previous, sizeof(previous));
        g_vm_net_mock_update_named_count = previousCount;
        return false;
    }
    printf("[info][mock-admin] named_resource_batch_publish files=%u catalog_rows=%u delivery=WT18/7 source=server-authoritative\n",
           uniqueCount, g_vm_net_mock_update_named_count);
    return true;
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
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

static u32 vm_alloc_host_string(const char *text)
{
#ifdef CBE_SERVER_ONLY
    /* Server-side packet construction uses byte buffers, never guest strings. */
    (void)text;
    return 0;
#else
    u32 len = (u32)strlen(text) + 1;
    u32 ptr = vm_malloc(len);
    if (ptr)
        uc_mem_write(MTK, ptr, text, len);
    return ptr;
#endif
}

static bool vm_net_mock_has_installed_update(void)
{
    long installedSize = vm_net_mock_file_size("JHOnlineData/MMORPGTempcbm");
    long gameSize = vm_net_mock_file_size("JHOnlineData/mmGameMstarWqvga.cbm");
    if (!vm_net_mock_file_has_min_size("JHOnlineData/mmorpg_updateversioncbm", 40))
        return false;

    /*
     * A clean resource release writes the real mmGame module but does not create
     * the network-update temp name. Treat that as installed; otherwise the
     * version response advertises an update and the client loops on WT 18/6.
     */
    if (gameSize >= 1024)
        return true;
    return installedSize >= 1024;
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
#ifdef CBE_SERVER_ONLY
    return 0;
#else
    u32 value = 0;
    if (Global_R9)
        uc_mem_read(MTK, Global_R9 + 0x9584, &value, sizeof(value));
    return value;
#endif
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

static u32 vm_net_mock_load_update_slot_payload(u8 slot,
                                                u8 *out,
                                                u32 outCap,
                                                char *sourcePath,
                                                size_t sourcePathCap)
{
    char configuredPath[1200];
    char fallbackPath[256];
    FILE *fp = NULL;
    long size = 0;
    u32 len = 0;

    if (sourcePath && sourcePathCap)
        sourcePath[0] = 0;
    if (slot < 1 || slot > VM_NET_MOCK_UPDATE_SLOT_COUNT || out == NULL ||
        outCap == 0)
        return 0;
    if (vm_net_mock_update_resource_path(
            g_vm_net_mock_update_slot_files[slot - 1], configuredPath,
            sizeof(configuredPath)))
    {
        fp = vm_net_mock_fopen_game_path(configuredPath, "rb");
        if (fp != NULL)
        {
            if (fseek(fp, 0, SEEK_END) == 0)
                size = ftell(fp);
            if (size >= 1024 && size <= (long)outCap &&
                fseek(fp, 0, SEEK_SET) == 0)
                len = (u32)fread(out, 1, (size_t)size, fp);
            fclose(fp);
            if (len == (u32)size && vm_net_mock_buffer_has_nonzero(out, len))
            {
                if (sourcePath && sourcePathCap)
                    snprintf(sourcePath, sourcePathCap, "%s", configuredPath);
                return len;
            }
        }
    }
    snprintf(fallbackPath, sizeof(fallbackPath), "JHOnlineData/%s",
             g_vm_net_mock_update_slot_files[slot - 1]);
    len = vm_net_mock_load_response_file(fallbackPath, out, outCap);
    if (len >= 1024 && vm_net_mock_buffer_has_nonzero(out, len))
    {
        if (sourcePath && sourcePathCap)
            snprintf(sourcePath, sourcePathCap, "%s", fallbackPath);
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

static void vm_net_mock_store_be16(u8 *out, u32 offset, u16 value)
{
    out[offset + 0] = (u8)((value >> 8) & 0xff);
    out[offset + 1] = (u8)(value & 0xff);
}

static void vm_net_mock_store_be32(u8 *out, u32 offset, u32 value)
{
    out[offset + 0] = (u8)((value >> 24) & 0xff);
    out[offset + 1] = (u8)((value >> 16) & 0xff);
    out[offset + 2] = (u8)((value >> 8) & 0xff);
    out[offset + 3] = (u8)(value & 0xff);
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

static void vm_net_mock_gbk_label_to_utf8(const char *gbk, char *out, size_t outCap)
{
    bool hasHighByte = false;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (gbk == NULL || gbk[0] == 0)
    {
        snprintf(out, outCap, "-");
        return;
    }

    for (const unsigned char *p = (const unsigned char *)gbk; *p; ++p)
    {
        if (*p >= 0x80)
        {
            hasHighByte = true;
            break;
        }
    }

    if (hasHighByte)
    {
        gbk_to_utf8((u8 *)gbk, (u8 *)out, outCap);
        if (out[0] != 0)
            return;
    }

    snprintf(out, outCap, "%s", gbk);
}

static void vm_net_mock_note_update_chunk_complete(const char *payloadName)
{
    char payloadNameUtf8[128];

    if (payloadName == NULL || payloadName[0] == 0)
        return;
    snprintf(g_vm_net_mock_update_completed_name,
             sizeof(g_vm_net_mock_update_completed_name),
             "%s",
             payloadName);
    g_vm_net_mock_update_completed_reenter_pending = true;
    vm_net_mock_gbk_label_to_utf8(payloadName,
                                  payloadNameUtf8,
                                  sizeof(payloadNameUtf8));
    printf("[info][network] mock_update_chunk_complete client=%08x file=%s action=client-install-callback protocol=WT18/7 evidence=JianghuOL.CBE:0x01037000+0x01036768\n",
           g_vm_mock_service_active_client_id, payloadNameUtf8);
}

static u32 vm_net_mock_load_named_update_payload(const char *payloadName,
                                                 u8 *out,
                                                 u32 outCap,
                                                 char *sourcePath,
                                                 size_t sourcePathCap)
{
    if (payloadName == NULL || payloadName[0] == 0 || out == NULL || outCap == 0)
        return 0;

    char path[1200];
    static const char *pathFormats[] = {
        /*
         * Named update chunks represent server-side resource downloads. Use the
         * clean source tree, not the client's writable cache: a failed run can
         * leave JHOnlineData/<name> polluted with the wrong payload.
         * The emulator is normally started from bin/, so include the parent
         * workspace path before the project-root relative form.
         */
        "../web/fs/JHOnlineData/%s",
        "web/fs/JHOnlineData/%s",
    };

    if (sourcePath && sourcePathCap)
        sourcePath[0] = 0;

    if (!vm_net_mock_update_name_is_safe(payloadName))
        return 0;

    /* The configured server resource root is authoritative on both Windows
     * and Linux. Do not accidentally read a stale client cache under bin/. */
    if (vm_net_mock_update_resource_path(payloadName, path, sizeof(path)))
    {
        FILE *fp = vm_net_mock_fopen_game_path(path, "rb");
        if (fp != NULL)
        {
            long size = 0;
            u32 len = 0;
            if (fseek(fp, 0, SEEK_END) == 0)
                size = ftell(fp);
            if (size > 0 && size <= (long)outCap &&
                fseek(fp, 0, SEEK_SET) == 0)
                len = (u32)fread(out, 1, (size_t)size, fp);
            fclose(fp);
            if (len == (u32)size)
            {
                if (sourcePath && sourcePathCap)
                    snprintf(sourcePath, sourcePathCap, "%s", path);
                return len;
            }
        }
    }

    for (u32 i = 0; i < sizeof(pathFormats) / sizeof(pathFormats[0]); ++i)
    {
        snprintf(path, sizeof(path), pathFormats[i], payloadName);
        u32 len = vm_net_mock_load_response_file(path, out, outCap);
        if (len == 0)
            continue;
        if (sourcePath && sourcePathCap)
            snprintf(sourcePath, sourcePathCap, "%s", path);
        return len;
    }

    return 0;
}

static u32 vm_net_mock_load_resource_update_payload(
    const u8 *request,
    u32 requestLen,
    const char *requestedName,
    u8 *out,
    u32 outCap,
    const char **payloadName,
    char *sourcePath,
    size_t sourcePathCap)
{
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);

    if (payloadName)
        *payloadName = (requestedName && requestedName[0]) ? requestedName : "MMORPGTempcbm";
    if (sourcePath && sourcePathCap)
        sourcePath[0] = 0;

    if (requestedName && requestedName[0])
    {
        if (vm_net_mock_request_contains(request, requestLen, "clientmiss"))
        {
            vm_net_mock_update_named_config *published =
                vm_net_mock_update_find_named(requestedName);
            if (published == NULL || !published->enabled)
            {
                char rejectedNameUtf8[256];
                vm_net_mock_gbk_label_to_utf8(requestedName,
                                              rejectedNameUtf8,
                                              sizeof(rejectedNameUtf8));
                printf("[warn][network] mock_update_client_miss_reject file=%s reason=not-published protocol=WT18/7\n",
                       rejectedNameUtf8);
                return 0;
            }
        }

        if (strcmp(requestedName, "c00PenglaiXiandao_01") == 0)
        {
            if (sourcePath && sourcePathCap)
                snprintf(sourcePath, sourcePathCap, "builtin:minimal-actor-motion");
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);
        }

        if (strcmp(requestedName, "c_mock_missing_motion.actor") == 0 ||
            strcmp(requestedName, "mock_missing_motion.actor") == 0)
        {
            if (sourcePath && sourcePathCap)
                snprintf(sourcePath, sourcePathCap, "builtin:minimal-actor-motion");
            return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);
        }

        if (strcmp(requestedName, "mock_actor_image.gif") == 0)
        {
            if (sourcePath && sourcePathCap)
                snprintf(sourcePath, sourcePathCap, "builtin:minimal-actor-image");
            return vm_net_mock_build_minimal_actor_image_resource(out, outCap);
        }

        u32 namedLen = vm_net_mock_load_named_update_payload(requestedName,
                                                            out,
                                                            outCap,
                                                            sourcePath,
                                                            sourcePathCap);
        if (namedLen > 0)
        {
            if (vm_net_mock_named_payload_looks_like_resource_stream(requestedName,
                                                                     haveRequestType ? requestType : 0,
                                                                     out,
                                                                     namedLen))
                return namedLen;
        }

        return 0;
    }

    if (vm_net_mock_request_contains(request, requestLen, "c_mock_missing_motion.actor") ||
        vm_net_mock_request_contains(request, requestLen, "mock_missing_motion.actor"))
    {
        if (payloadName)
            *payloadName = "c_mock_missing_motion.actor";
        if (sourcePath && sourcePathCap)
            snprintf(sourcePath, sourcePathCap, "builtin:minimal-actor-motion");
        return vm_net_mock_build_minimal_actor_motion_resource(out, outCap);
    }

    if (vm_net_mock_request_contains(request, requestLen, "mock_actor_image.gif"))
    {
        if (payloadName)
            *payloadName = "mock_actor_image.gif";
        if (sourcePath && sourcePathCap)
            snprintf(sourcePath, sourcePathCap, "builtin:minimal-actor-image");
        return vm_net_mock_build_minimal_actor_image_resource(out, outCap);
    }

    {
        u8 updateSlot = 0;
        u32 updateLen = 0;
        if (vm_net_mock_get_object_u8_field(request, requestLen, "id", &updateSlot) &&
            updateSlot >= 1 && updateSlot <= VM_NET_MOCK_UPDATE_SLOT_COUNT)
        {
            if (payloadName)
                *payloadName = g_vm_net_mock_update_slot_files[updateSlot - 1];
            updateLen = vm_net_mock_load_update_slot_payload(
                updateSlot, out, outCap, sourcePath, sourcePathCap);
        }
        else
        {
            updateLen = vm_net_mock_load_update_payload(out, outCap);
            if (updateLen > 0 && sourcePath && sourcePathCap)
                snprintf(sourcePath, sourcePathCap,
                         "JHOnlineData/mmGameMstarWqvga.cbm");
        }
        return updateLen;
    }
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
    u32 currentR9 = 0;
    (void)size;
    (void)user_data;

    uc_reg_read(uc, UC_ARM_REG_R9, &currentR9);
    if (currentR9 >= VM_Memory_Pool_ADDRESS &&
        currentR9 < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
    {
        vm_dl_note_sp_bf(currentR9, "pool-exec");
    }
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

static bool vm_net_mock_update_parse_cbm_versions(const u8 *request,
                                                  u32 requestLen,
                                                  u16 versions[VM_NET_MOCK_UPDATE_SLOT_COUNT])
{
    const u8 *data = NULL;
    u16 dataLen = 0;
    u32 values[VM_NET_MOCK_UPDATE_SLOT_COUNT * 2] = {0};
    u32 valueCount = 0;

    if (versions)
        memset(versions, 0, sizeof(u16) * VM_NET_MOCK_UPDATE_SLOT_COUNT);
    if (!vm_net_mock_get_object_entry_field(request, requestLen, "cbm", &data,
                                            &dataLen) ||
        data == NULL || dataLen == 0)
        return false;
    if (dataLen >= 2 && (((u16)data[0] << 8) | data[1]) == dataLen - 2)
    {
        data += 2;
        dataLen -= 2;
    }
    for (u32 p = 0; p < dataLen &&
                    valueCount < VM_NET_MOCK_UPDATE_SLOT_COUNT * 2;)
    {
        if (p + 3 <= dataLen && data[p] == 0 && data[p + 1] == 1)
        {
            values[valueCount++] = data[p + 2];
            p += 3;
        }
        else if (p + 4 <= dataLen && data[p] == 0 && data[p + 1] == 2)
        {
            values[valueCount++] = ((u32)data[p + 2] << 8) | data[p + 3];
            p += 4;
        }
        else if (p + 4 <= dataLen && data[p] == 3 && data[p + 1] == 0 &&
                 data[p + 2] == 1)
        {
            values[valueCount++] = data[p + 3];
            p += 4;
        }
        else if (p + 5 <= dataLen && data[p] == 4 && data[p + 1] == 0 &&
                 data[p + 2] == 2)
        {
            values[valueCount++] = ((u32)data[p + 3] << 8) | data[p + 4];
            p += 5;
        }
        else
        {
            ++p;
        }
    }
    if (valueCount < VM_NET_MOCK_UPDATE_SLOT_COUNT * 2)
        return false;
    for (u32 i = 0; i < VM_NET_MOCK_UPDATE_SLOT_COUNT; ++i)
    {
        if (values[i * 2] != i + 1 || values[i * 2 + 1] > 0xffff)
            return false;
        if (versions)
            versions[i] = (u16)values[i * 2 + 1];
    }
    return true;
}

static u8 vm_net_mock_update_result_mask(const u8 *request, u32 requestLen,
                                         u32 *identityHashOut,
                                         bool *usedClientVersionsOut)
{
    u16 clientVersions[VM_NET_MOCK_UPDATE_SLOT_COUNT] = {0};
    bool haveClientVersions = vm_net_mock_update_parse_cbm_versions(
        request, requestLen, clientVersions);
    u32 identityHash = vm_net_mock_update_identity_hash(request, requestLen);
    vm_net_mock_update_delivery *delivery = NULL;
    u8 result = 0;

    vm_net_mock_update_catalog_load();
    if (identityHash != 0)
        vm_net_mock_update_remember_client(g_vm_mock_service_active_client_id,
                                           identityHash);
    if (!haveClientVersions)
        delivery = vm_net_mock_update_find_delivery(identityHash, false);
    for (u32 i = 0; i < VM_NET_MOCK_UPDATE_SLOT_COUNT; ++i)
    {
        long size = -1;
        const vm_net_mock_update_slot_config *slot =
            &g_vm_net_mock_update_slots[i];
        if (!slot->enabled)
            continue;
        size = vm_net_mock_update_file_size(
            g_vm_net_mock_update_slot_files[i]);
        if (size < 1024 || size > VM_NET_MOCK_UPDATE_PAYLOAD_MAX)
        {
            printf("[error][network] mock_update_publish_invalid slot=%u file=%s size=%ld action=skip\n",
                   i + 1, g_vm_net_mock_update_slot_files[i], size);
            continue;
        }
        if ((haveClientVersions && clientVersions[i] != slot->version) ||
            (!haveClientVersions &&
             (delivery == NULL ||
              delivery->deliveredVersions[i] != slot->version)))
            result |= (u8)(1u << i);
    }
    if (identityHashOut)
        *identityHashOut = identityHash;
    if (usedClientVersionsOut)
        *usedClientVersionsOut = haveClientVersions;
    return result;
}

static void vm_net_mock_update_mark_delivered(const u8 *request,
                                              u32 requestLen,
                                              u8 slot)
{
    u32 identityHash = 0;
    vm_net_mock_update_delivery *delivery = NULL;
    if (slot < 1 || slot > VM_NET_MOCK_UPDATE_SLOT_COUNT)
        return;
    vm_net_mock_update_catalog_load();
    identityHash = vm_net_mock_update_identity_for_active_client(request,
                                                                 requestLen);
    delivery = vm_net_mock_update_find_delivery(identityHash, true);
    if (delivery == NULL)
        return;
    delivery->deliveredVersions[slot - 1] =
        g_vm_net_mock_update_slots[slot - 1].version;
    if (!vm_net_mock_update_delivery_save(NULL))
        printf("[warn][network] mock_update_delivery_save_failed identity=%08x slot=%u\n",
               identityHash, slot);
}

static u16 vm_net_mock_update_response_version(u8 responseSubtype,
                                               u8 updateSlot,
                                               const char *requestName,
                                               u16 requestVersion)
{
    vm_net_mock_update_named_config *named = NULL;
    vm_net_mock_update_catalog_load();
    if (responseSubtype == 6 && updateSlot >= 1 &&
        updateSlot <= VM_NET_MOCK_UPDATE_SLOT_COUNT &&
        g_vm_net_mock_update_slots[updateSlot - 1].enabled)
        return g_vm_net_mock_update_slots[updateSlot - 1].version;
    if (responseSubtype == 7 && requestName != NULL && requestName[0] != 0)
    {
        named = vm_net_mock_update_find_named(requestName);
        if (named != NULL && named->enabled)
            return named->version;
    }
    return requestVersion != 0 ? requestVersion : 1;
}

static bool vm_net_mock_is_update_version_catalog_request(const u8 *request,
                                                          u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind,
                                                &subtype))
        return false;
    return kind == 0x12 && subtype == 5 &&
           vm_net_mock_request_contains(request, requestLen, "cbm");
}

static u32 vm_net_mock_build_version_response(const u8 *request,
                                              u32 requestLen,
                                              u8 *out,
                                              u32 outCap)
{
    u32 pos = 5;
    u8 requestKind = 0;
    u8 requestSubtype = 0;
    u32 identityHash = 0;
    bool usedClientVersions = false;
    u8 result = vm_net_mock_update_result_mask(request, requestLen,
                                               &identityHash,
                                               &usedClientVersions);
    if (outCap < pos)
        return 0;
    (void)vm_net_mock_get_wt_header_kind_subtype(request, requestLen,
                                                &requestKind,
                                                &requestSubtype);

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, 5, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    u8 objectCount = 1;
    if (result == 0 && requestKind == 0x12 && requestSubtype == 9)
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
    printf("[info][network] mock_update_version request=%u/%u result=0x%02x identity=%08x source=%s configured=%u/%u/%u/%u\n",
           requestKind, requestSubtype, result, identityHash,
           usedClientVersions ? "client-cbm-versions" : "delivery-ledger",
           g_vm_net_mock_update_slots[0].enabled ?
               g_vm_net_mock_update_slots[0].version : 0,
           g_vm_net_mock_update_slots[1].enabled ?
               g_vm_net_mock_update_slots[1].version : 0,
           g_vm_net_mock_update_slots[2].enabled ?
               g_vm_net_mock_update_slots[2].version : 0,
           g_vm_net_mock_update_slots[3].enabled ?
               g_vm_net_mock_update_slots[3].version : 0);
    return pos;
}

static u32 vm_net_mock_build_update_chunk_response_for_subtype(const u8 *request,
                                                               u32 requestLen,
                                                               u8 responseSubtype,
                                                               u8 *out,
                                                               u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    static u8 updateData[VM_NET_MOCK_UPDATE_PAYLOAD_MAX];
    char requestName[256];
    requestName[0] = 0;
    bool haveRequestName = vm_net_mock_get_object_string_field(request, requestLen, "name", requestName, sizeof(requestName)) &&
                            requestName[0] != 0;
    u8 requestType = 0;
    bool haveRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType);
    u8 updateSlot = 0;
    u16 requestVersion = 1;
    u16 responseVersion = 1;
    u8 versionBytes[2];
    const char *payloadName = NULL;
    char payloadNameUtf8[256];
    char sourcePath[384];
    char sourcePathUtf8[512];

    (void)vm_net_mock_get_object_u16_field(request, requestLen, "version", &requestVersion);
    if (requestVersion == 0)
        requestVersion = 1;
    (void)vm_net_mock_get_object_u8_field(request, requestLen, "id", &updateSlot);
    responseVersion = vm_net_mock_update_response_version(
        responseSubtype, updateSlot, haveRequestName ? requestName : NULL,
        requestVersion);

    u32 updateLen = vm_net_mock_load_resource_update_payload(request,
                                                             requestLen,
                                                             haveRequestName ? requestName : NULL,
                                                             updateData,
                                                             sizeof(updateData),
                                                             &payloadName,
                                                             sourcePath,
                                                             sizeof(sourcePath));
    if (updateLen == 0)
    {
        vm_net_mock_gbk_label_to_utf8(haveRequestName ? requestName : "MMORPGTempcbm",
                                      payloadNameUtf8,
                                      sizeof(payloadNameUtf8));
        printf("[error][network] mock_update_chunk_missing subtype=%u file=%s version=%u len=%u\n",
               responseSubtype,
               payloadNameUtf8,
               requestVersion,
               requestLen);
        return 0;
    }

    u32 start = 0;
    vm_net_mock_get_object_u32_field(request, requestLen, "start", &start);
    if (start >= updateLen)
        start = 0;

    u32 chunkLen = updateLen - start;
    if (chunkLen > 0x1000)
        chunkLen = 0x1000;
    u32 crc = vm_net_mock_signed_byte_sum(updateData, start + chunkLen);

    versionBytes[0] = (u8)(responseVersion >> 8);
    versionBytes[1] = (u8)responseVersion;

    if (responseSubtype == 7 &&
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
    {
        return 0;
    }
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", updateLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", versionBytes, sizeof(versionBytes)))
        return 0;
    if (responseSubtype == 7)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", haveRequestType ? requestType : 0))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName ? payloadName : "MMORPGTempcbm"))
            return 0;
    }
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", updateData + start, (u16)chunkLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 1);
    out[5] = 1;
    out[6] = 0x12;
    out[7] = responseSubtype;
    out[8] = 0;
    vm_net_mock_finish_wt_object(out, 5, pos);
    vm_net_mock_gbk_label_to_utf8(payloadName ? payloadName : "MMORPGTempcbm",
                                  payloadNameUtf8,
                                  sizeof(payloadNameUtf8));
    vm_net_mock_gbk_label_to_utf8(sourcePath[0] ? sourcePath : "-",
                                  sourcePathUtf8,
                                  sizeof(sourcePathUtf8));
    printf("[info][network] mock_update_chunk subtype=%u file=%s start=%u chunk=%u total=%u crc=%u version=%u path=%s resp=%u\n",
           responseSubtype,
           payloadNameUtf8,
           start,
           chunkLen,
           updateLen,
           crc,
           responseVersion,
           sourcePathUtf8,
           pos);
    if (responseSubtype == 7 && start + chunkLen >= updateLen)
        vm_net_mock_note_update_chunk_complete(payloadName ? payloadName : haveRequestName ? requestName : "");
    if (responseSubtype == 6 && start + chunkLen >= updateLen)
    {
        vm_net_mock_update_mark_delivered(request, requestLen, updateSlot);
        printf("[info][network] mock_update_module_complete slot=%u file=%s version=%u\n",
               updateSlot,
               payloadNameUtf8,
               responseVersion);
    }
    return pos;
}

static u32 vm_net_mock_build_update_chunk_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    return vm_net_mock_build_update_chunk_response_for_subtype(request, requestLen, 7, out, outCap);
}

static u32 vm_net_mock_build_update_manifest_chunk_response(const u8 *request, u32 requestLen,
                                                            u8 *out, u32 outCap)
{
    return vm_net_mock_build_update_chunk_response_for_subtype(request, requestLen, 6, out, outCap);
}

static bool vm_net_mock_is_update_chunk_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    if (kind != 0x12 || subtype != 7)
        return false;
    return vm_net_mock_request_contains(request, requestLen, "start") &&
           (vm_net_mock_request_contains(request, requestLen, "id") ||
            vm_net_mock_request_contains(request, requestLen, "name"));
}

static bool vm_net_mock_is_update_manifest_chunk_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 0x12 &&
           subtype == 6 &&
           vm_net_mock_request_contains(request, requestLen, "start") &&
           vm_net_mock_request_contains(request, requestLen, "version") &&
           vm_net_mock_request_contains(request, requestLen, "client");
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
static bool vm_net_mock_append_role_skills_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_fb_target_empty11_object(u8 *out, u32 outCap, u32 *pos);
static bool vm_net_mock_append_scene_npcs11_once_or_empty(u8 *out, u32 outCap, u32 *pos,
                                                         const char *scene,
                                                         const char *phase);
static bool vm_net_mock_append_fb_target_result12_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneKey, u16 spawnX, u16 spawnY);
static bool vm_net_mock_append_fb_target_result4_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 typeValue, const char *infoText);
static void vm_net_mock_append_preview_u32(char *out, u32 outCap, u32 *pos, u32 value);

enum
{
    VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY = 20,
    VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT = 200,
    VM_NET_MOCK_BACKPACK_MAX_ITEMS = 200,
    VM_NET_MOCK_BACKPACK_LEGACY_MAX_ITEMS = 40,
    VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID = 800,
    VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_SEQ = 1,
    VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_COUNT = 5,
    VM_NET_MOCK_BACKPACK_EXPAND_ITEM_ID = 806,
    VM_NET_MOCK_SMALL_HORN_ITEM_ID = 807,
    VM_NET_MOCK_BACKPACK_EXPAND_STEP = 5,
    VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE = 1,
    VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK = 99,
    VM_NET_MOCK_SHOP_PAGE_SIZE = 10,
    VM_NET_MOCK_SHOP_SECRET_MAX_ITEMS = 8,
    VM_NET_MOCK_SHOP_EQUIP_CATEGORY_MAX_ITEMS = 80,
    VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS = 10,
    VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS = 2048,
    VM_NET_MOCK_ITEM_EFFECT_CATALOG_MAX_ITEMS = 2048,
    VM_NET_MOCK_SKILL_CATALOG_MAX_ITEMS = 256,
    VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS = 64,
    VM_NET_MOCK_AUTO_MONSTER_CATALOG_MAX_ITEMS = 128,
    VM_NET_MOCK_SHOP_NAME_BYTES = 12,
    VM_NET_MOCK_SKILL_NAME_BYTES = 24,
    VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID = 1,
    VM_NET_MOCK_TELEPORT_STONE_COST = 1,
    VM_NET_MOCK_SCENE_LANDING_SAFE_GAP = 32,
    VM_NET_MOCK_ROLE_DB_MAX_ROLES = 5,
    VM_NET_MOCK_ROLE_DB_LEGACY_VERSION = 1,
    VM_NET_MOCK_ROLE_DB_BACKPACK_VERSION = 2,
    VM_NET_MOCK_ROLE_DB_EQUIP_VERSION = 3,
    VM_NET_MOCK_ROLE_DB_SHOP_WCOIN_VERSION = 4,
    VM_NET_MOCK_ROLE_DB_VERSION = 5,
    VM_NET_MOCK_ROLE_DESIGNATION_COUNT = 10,
    VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL = 16,
    VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST = 901,
    VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_LAST = 916,
    VM_NET_MOCK_ROLE_EXP_PER_LEVEL = 100,
    VM_NET_MOCK_EQUIP_SLOT_COUNT = 8,
    VM_NET_MOCK_EQUIP_CATALOG_MAX_ITEMS = 2048,
    VM_NET_MOCK_BATTLE_POISON_SLIME_ID = 105,
    VM_NET_MOCK_BATTLE_POISON_SLIME_EXP = 5,
    VM_NET_MOCK_BATTLE_POISON_SLIME_GOLD = 5,
    VM_NET_MOCK_BATTLE_CHANGMING_SAN_ITEM_ID = 304,//回春散ID
    VM_NET_MOCK_BATTLE_CHANGMING_SAN_DROP_RATE = 0,//回春散掉落概率
    VM_NET_MOCK_TASK_MATERIAL_DROP_RATE = 100,//人物材料掉落概率
    VM_NET_MOCK_ROLE_DEFAULT_ID = 10001,
    VM_NET_MOCK_ROLE_DEFAULT_HP = 120,
    VM_NET_MOCK_ROLE_DEFAULT_MP = 100,
    VM_NET_MOCK_ROLE_DEFAULT_MONEY = 0,
    VM_NET_MOCK_ROLE_DEATH_MONEY_PENALTY_PERCENT = 5,
    VM_NET_MOCK_ROLE_DEATH_REVIVE_HP_PERCENT = 30,
    VM_NET_MOCK_ROLE_DEATH_REVIVE_MP_PERCENT = 30,
    VM_NET_MOCK_ROLE_INITIAL_X = 224,
    VM_NET_MOCK_ROLE_INITIAL_Y = 132
};

enum
{
    /* server_dynamic_npcs.npc_kind.  Zero keeps every existing task/dialog
     * NPC compatible; non-zero values add one packet-driven service entry to
     * the normal 26/1 dialog. */
    VM_NET_MOCK_NPC_KIND_NORMAL = 0,
    VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT = 1,
    VM_NET_MOCK_NPC_KIND_EQUIPMENT_REPAIR = 2,
    VM_NET_MOCK_NPC_KIND_SKILL_TRAINER = 3,
    VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT = 4,
    VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT = 5,
    VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE = 6,
    VM_NET_MOCK_NPC_KIND_MAX = VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE,
    VM_NET_MOCK_ROLE_SERVICE_CACHE_MAX = 32,
    VM_NET_MOCK_EQUIPMENT_DURABILITY_MAX = 100,
    VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS = 7
};

/* action=1 in task_hall_activate_selected_entry sends 26/1{type=2,id=value}.
 * Use a private high-byte namespace for nested service menus; catalog ids live
 * in the low 24 bits and are validated again before any state mutation. */
#define VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK       0xff000000u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_WEAPON       0xe1000001u
#define VM_NET_MOCK_NPC_SERVICE_BUY_WEAPON_BASE   0xe2000000u
#define VM_NET_MOCK_NPC_SERVICE_REPAIR_ALL        0xe3000001u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS       0xe4000001u
#define VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE  0xe5000000u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_ARMOR        0xe6000001u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_MEDICINE     0xe7000001u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE 0xe8000000u
#define VM_NET_MOCK_NPC_SERVICE_BUY_ITEM_BASE     0xe9000000u
#define VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE 0xea000000u
#define VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE 0xeb000000u
#define VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE 0xec000000u
#define VM_NET_MOCK_NPC_SERVICE_VALUE_MASK        0x00ffffffu
#define VM_NET_MOCK_NPC_SERVICE_CATEGORY_MASK     0x000000ffu
#define VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_SHIFT 8u
#define VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR 0xfeu
#define VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS 5u

typedef struct
{
    u32 itemId;
    u16 seq;
    u16 enhanceLevel;
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
    u32 wcoin;
    char scene[64];
    u16 x;
    u16 y;
    u8 backpackItemCount;
    u8 designationId;
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
    u8 designationId;
    u16 nextBackpackSeq;
    u32 equippedItemIds[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    vm_net_mock_backpack_item_state backpackItems[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
} vm_net_mock_role_state_v4;

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
    vm_net_mock_backpack_item_state backpackItems[VM_NET_MOCK_BACKPACK_LEGACY_MAX_ITEMS];
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
    u8 backpackItemCount;
    u8 designationId;
    u16 nextBackpackSeq;
    u32 equippedItemIds[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    vm_net_mock_backpack_item_state backpackItems[VM_NET_MOCK_BACKPACK_LEGACY_MAX_ITEMS];
} vm_net_mock_role_state_v3;

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
    vm_net_mock_role_state_v3 roles[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
} vm_net_mock_role_db_file_v3;

typedef struct
{
    char magic[4];
    u32 version;
    u32 activeRoleId;
    u32 roleCount;
    vm_net_mock_role_state_v4 roles[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
} vm_net_mock_role_db_file_v4;

typedef struct
{
    char magic[4];
    u32 version;
    u32 activeRoleId;
    u32 roleCount;
    vm_net_mock_role_state roles[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
} vm_net_mock_role_db_file;

#define VM_MOCK_SERVICE_ACCOUNT_DB_VERSION 1
#define VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS 1000000
#define VM_MOCK_SERVICE_FRIEND_DB_VERSION 1
#define VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS 256

typedef struct
{
    char username[64];
    char password[64];
} vm_mock_service_account_record;

typedef struct
{
    char magic[4];
    u32 version;
    u32 accountCount;
    vm_mock_service_account_record accounts[VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS];
} vm_mock_service_account_db_file;

typedef struct
{
    char ownerAccountId[64];
    u32 ownerRoleId;
    char targetAccountId[64];
    u32 targetRoleId;
    char targetRoleName[32];
    u32 friendDegree;
    u32 targetLevel;
    u8 targetJob;
    u8 targetSex;
    u16 reserved0;
} vm_mock_service_friend_record;

typedef struct
{
    char magic[4];
    u32 version;
    u32 recordCount;
    vm_mock_service_friend_record records[VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS];
} vm_mock_service_friend_db_file;

static vm_net_mock_role_state *vm_net_mock_active_role(void);
static u32 vm_net_mock_role_wcoin_balance(const vm_net_mock_role_state *role);

enum
{
    VM_NET_MOCK_GUILD_PAGE_MAX = 32,
    VM_NET_MOCK_GUILD_NAME_SIZE = 32,
    VM_NET_MOCK_GUILD_ROLE_NAME_SIZE = 32,
    VM_NET_MOCK_GUILD_TEXT_SIZE = 128,
    VM_NET_MOCK_GUILD_NOTICE_MAX_BYTES = 60,
    VM_NET_MOCK_GUILD_POSITION_COUNT = 2
};

typedef struct
{
    u32 guildId;
    u32 guildLevel;
    u32 minimumLevel;
    u32 memberCount;
    u32 memberLimit;
    u32 guildMoney;
    u32 prosperity;
    u32 actionPower;
    u32 researchPower;
    u32 construction;
    char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];
    char leaderName[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
    char currentConstruction[VM_NET_MOCK_GUILD_TEXT_SIZE];
    char notice[VM_NET_MOCK_GUILD_TEXT_SIZE];
} vm_net_mock_guild_record;

typedef struct
{
    u32 roleId;
    u32 level;
    u8 memberRank;
    u8 online;
    char accountId[64];
    char roleName[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
    char memberTitle[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
} vm_net_mock_guild_member_record;

typedef struct
{
    u32 roleId;
    u32 level;
    u8 job;
    u8 sex;
    char accountId[64];
    char roleName[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
} vm_net_mock_guild_application_record;

static bool vm_net_mock_guild_find_role_membership(u32 roleId,
                                                    vm_net_mock_guild_record *guildOut,
                                                    u8 *rankOut);
static bool vm_net_mock_guild_find_membership_for_account(const char *accountId,
                                                           u32 roleId,
                                                           vm_net_mock_guild_record *guildOut,
                                                           u8 *rankOut);

typedef struct
{
    u8 requestSubtype;
    bool haveUserName;
    bool havePassword;
    char userName[64];
    char password[64];
} vm_mock_service_login_request;

static vm_mock_service_account_db_file g_vm_mock_service_account_db;
static bool g_vm_mock_service_account_db_loaded = false;
static bool g_vm_mock_service_account_db_valid = false;
static vm_mock_service_friend_db_file g_vm_mock_service_friend_db;
static bool g_vm_mock_service_friend_db_loaded = false;
static bool g_vm_mock_service_friend_db_valid = false;
static char g_vm_mock_service_login_issue_username[64];
static char g_vm_mock_service_login_issue_password[64];
static u8 g_vm_mock_service_login_issue_result = 0;

static bool vm_mock_service_login_is_no_account(const vm_mock_service_login_request *login);
static bool vm_mock_service_authenticate_login_request(const vm_mock_service_login_request *login,
                                                       const char **errorOut);

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
static bool vm_net_mock_role_db_save(const char *reason);
static bool vm_mock_service_mysql_authority_prepare(void);
static bool vm_mock_service_mysql_authority_seal(void);
static bool vm_mock_service_mysql_authority_is_sealed(void);
static bool vm_net_mock_role_add_backpack_item_to_role(vm_net_mock_role_state *role,
                                                        u32 itemId,
                                                        u32 count,
                                                        u16 *seqOut,
                                                        const char *reason);
static bool vm_net_mock_role_add_backpack_item(u32 itemId, u32 count, u16 *seqOut);
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
static bool vm_net_mock_role_add_exp(vm_net_mock_role_state *role, u32 addExp);
static void vm_net_mock_task_progress_after_battle(u32 enemyId,
                                                   u32 enemyCount,
                                                   u32 dropItemId,
                                                   u32 dropCount);
/* Resolve whether a configured monster drop is a task material and, when it
 * is, how many units the active role can still legitimately receive.  Battle
 * settlement calls this before mutating the backpack; task progress consumes
 * the same authoritative state after a successful grant. */
static bool vm_net_mock_task_material_drop_policy(u32 roleId, u32 itemId,
                                                  bool *isTaskMaterialOut,
                                                  u32 *remainingOut);
static void vm_net_mock_role_build_player_stats(const vm_net_mock_role_state *role,
                                                vm_net_mock_player_stats *stats);
static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap);

