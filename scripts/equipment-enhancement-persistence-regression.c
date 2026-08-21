/*
 * Deterministic server-only regression for the equipment-enhancement commit
 * boundary.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections -w
 *       scripts/equipment-enhancement-persistence-regression.c
 *       obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o
 *       obj/server/md5.o -Wl,--gc-sections -o
 *       tmp/equipment-enhancement-persistence-regression.exe -lpthread
 *       -liconv -lm -lkernel32 -lws2_32
 *
 * The fixture does not start a listener or contact MySQL.  It exercises the
 * production enhancement save/rollback helper with a complete role snapshot:
 * level, generated affixes, consumed crystal row, copper, and unrelated role
 * state must all move together or all be restored.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static u32 g_save_calls;
static const char *g_save_reason;
static bool g_save_result;

static bool save_fixture(const char *reason)
{
    ++g_save_calls;
    g_save_reason = reason;
    return g_save_result;
}

static void seed_role(vm_net_mock_role_state *role)
{
    vm_net_mock_backpack_item_state *equipment;
    vm_net_mock_backpack_item_state *crystal;

    memset(role, 0, sizeof(*role));
    role->roleId = 810001;
    role->level = 12;
    role->money = 9876;
    role->backpackItemCount = 2;
    role->nextBackpackSeq = 19;
    equipment = &role->backpackItems[0];
    equipment->itemId = 1001;
    equipment->seq = 17;
    equipment->count = 1;
    equipment->enhanceLevel = 3;
    equipment->enhanceAffixes.type[0] = 1;
    equipment->enhanceAffixes.value[0] = 11;
    equipment->enhanceAffixes.type[1] = 2;
    equipment->enhanceAffixes.value[1] = 22;
    crystal = &role->backpackItems[1];
    crystal->itemId = 901;
    crystal->seq = 18;
    crystal->count = 5;
}

static u32 build_enhance_request(u8 *request, u32 requestCap, u16 equipSeq)
{
    static const u8 occultInfo[9] = {0, 4, 0, 0, 3, 0x85, 0, 1, 1};
    const char *equipField = "equipseq";
    const char *occultField = "occultinfo";
    u32 payloadLen = 0;
    u32 objectLen = 0;
    u32 requestLen = 0;

    if (request == NULL || requestCap < 64)
        return 0;
    memset(request, 0, requestCap);
    request[0] = 'W';
    request[1] = 'T';
    request[4] = 1;
    request[5] = 29;
    request[6] = 3;
    request[9 + payloadLen++] = (u8)strlen(equipField);
    memcpy(request + 9 + payloadLen, equipField, strlen(equipField));
    payloadLen += (u32)strlen(equipField);
    request[9 + payloadLen++] = 0;
    request[9 + payloadLen++] = 4;
    request[9 + payloadLen++] = 0;
    request[9 + payloadLen++] = 2;
    request[9 + payloadLen++] = (u8)(equipSeq >> 8);
    request[9 + payloadLen++] = (u8)equipSeq;
    request[9 + payloadLen++] = (u8)strlen(occultField);
    memcpy(request + 9 + payloadLen, occultField, strlen(occultField));
    payloadLen += (u32)strlen(occultField);
    request[9 + payloadLen++] = 0;
    request[9 + payloadLen++] = sizeof(occultInfo);
    memcpy(request + 9 + payloadLen, occultInfo, sizeof(occultInfo));
    payloadLen += sizeof(occultInfo);
    objectLen = 5u + payloadLen;
    requestLen = 4u + objectLen;
    request[2] = (u8)(requestLen >> 8);
    request[3] = (u8)requestLen;
    request[7] = (u8)(objectLen >> 8);
    request[8] = (u8)objectLen;
    return requestLen;
}

static bool response_result_is(const u8 *response, u32 responseLen, u8 expected)
{
    u16 objectLen = 0;
    u32 payloadOffset = 11;
    u8 nameLen = 0;
    u16 valueLen = 0;

    if (response == NULL || responseLen < 16 || response[0] != 'W' ||
        response[1] != 'T' || response[4] != 1 || response[5] != 1 ||
        response[6] != 29 || response[7] != 3)
        return false;
    objectLen = (u16)(((u16)response[9] << 8) | response[10]);
    if (objectLen + 5u != responseLen || payloadOffset >= responseLen)
        return false;
    nameLen = response[payloadOffset++];
    if (nameLen != 6 || payloadOffset + nameLen + 2u > responseLen ||
        memcmp(response + payloadOffset, "result", 6) != 0)
        return false;
    payloadOffset += nameLen;
    valueLen = (u16)(((u16)response[payloadOffset] << 8) |
                     response[payloadOffset + 1]);
    payloadOffset += 2;
    return valueLen == 3 && payloadOffset + valueLen == responseLen &&
           response[payloadOffset] == 0 && response[payloadOffset + 1] == 1 &&
           response[payloadOffset + 2] == expected;
}

static void seed_builder_catalog_and_role(void)
{
    vm_net_mock_role_state *role;

    memset(g_vm_net_mock_equipment_catalog, 0,
           sizeof(g_vm_net_mock_equipment_catalog));
    g_vm_net_mock_equipment_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog_count = 1;
    g_vm_net_mock_equipment_catalog[0].itemId = 1001;
    g_vm_net_mock_equipment_catalog[0].slot = 0;
    g_vm_net_mock_equipment_catalog[0].levelRequired = 1;
    g_vm_net_mock_equipment_catalog[0].durabilityMax = 100;
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 810001;
    role = &g_vm_net_mock_role_db.roles[0];
    seed_role(role);
}

static int expect_success_keeps_commit(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state before;
    vm_net_mock_backpack_item_state *equipment;

    seed_role(&role);
    before = role;
    equipment = &role.backpackItems[0];
    equipment->enhanceLevel = 4;
    equipment->enhanceAffixes.type[2] = 3;
    equipment->enhanceAffixes.value[2] = 33;
    role.backpackItems[1].count = 4;
    role.money -= 400;
    g_save_calls = 0;
    g_save_reason = NULL;
    g_save_result = true;
    if (!vm_net_mock_equipment_enhance_persist_or_rollback(
            &role, &before, "equipment-enhance-success", save_fixture) ||
        g_save_calls != 1 || g_save_reason == NULL ||
        strcmp(g_save_reason, "equipment-enhance-success") != 0 ||
        role.backpackItems[0].enhanceLevel != 4 ||
        role.backpackItems[0].enhanceAffixes.type[2] != 3 ||
        role.backpackItems[0].enhanceAffixes.value[2] != 33 ||
        role.backpackItems[1].count != 4 || role.money != 9476)
    {
        fputs("enhancement persistence success path did not keep the full commit\n",
              stderr);
        return 1;
    }
    return 0;
}

static int expect_failure_restores_full_snapshot(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state before;
    vm_net_mock_backpack_item_state *equipment;

    seed_role(&role);
    before = role;
    equipment = &role.backpackItems[0];
    equipment->enhanceLevel = 4;
    equipment->enhanceAffixes.type[2] = 3;
    equipment->enhanceAffixes.value[2] = 33;
    role.backpackItems[1].count = 4;
    role.money -= 400;
    g_save_calls = 0;
    g_save_reason = NULL;
    g_save_result = false;
    if (vm_net_mock_equipment_enhance_persist_or_rollback(
            &role, &before, "equipment-enhance-success", save_fixture) ||
        g_save_calls != 1 || g_save_reason == NULL ||
        strcmp(g_save_reason, "equipment-enhance-success") != 0 ||
        memcmp(&role, &before, sizeof(role)) != 0)
    {
        fputs("enhancement persistence failure did not restore the full snapshot\n",
              stderr);
        return 1;
    }
    if (role.backpackItems[0].enhanceLevel != 3 ||
        role.backpackItems[0].enhanceAffixes.type[2] != 0 ||
        role.backpackItems[0].enhanceAffixes.value[2] != 0 ||
        role.backpackItems[1].count != 5 || role.money != 9876)
    {
        fputs("enhancement persistence failure lost level, affixes, materials, or money\n",
              stderr);
        return 1;
    }
    return 0;
}

static int expect_builder_failure_is_cancelled_and_atomic(void)
{
    u8 request[128];
    u8 response[4096];
    u32 requestLen;
    u32 responseLen;
    vm_net_mock_role_state before;
    vm_net_mock_role_state *role;

    seed_builder_catalog_and_role();
    role = &g_vm_net_mock_role_db.roles[0];
    vm_net_mock_role_normalize_backpack(role);
    before = *role;
    g_save_calls = 0;
    g_save_reason = NULL;
    g_save_result = false;
    requestLen = build_enhance_request(request, sizeof(request), 17);
    responseLen = vm_net_mock_build_equipment_enhance_response_with_save_callback(
        request, requestLen, response, sizeof(response), save_fixture);
    if (requestLen == 0 || responseLen == 0 ||
        !response_result_is(response, responseLen, 0) || g_save_calls != 1 ||
        g_save_reason == NULL ||
        strcmp(g_save_reason, "equipment-enhance-failed") != 0 ||
        memcmp(role, &before, sizeof(before)) != 0)
    {
        fputs("29/3 persistence failure did not cancel and restore atomically\n",
              stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (expect_success_keeps_commit() != 0 ||
        expect_failure_restores_full_snapshot() != 0 ||
        expect_builder_failure_is_cancelled_and_atomic() != 0)
        return 1;
    puts("equipment enhancement persistence regression passed: success commits level/affixes/materials/money and save failure restores the complete role snapshot");
    return 0;
}
