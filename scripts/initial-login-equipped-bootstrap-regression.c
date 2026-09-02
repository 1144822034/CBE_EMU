/* Deterministic selected-role equipped-item bootstrap regression.
 *
 * This invokes the production group/type-1 item bootstrap builder with an
 * in-memory session and role.  It starts no listener, database, or client.
 * The contract is intentionally narrow: a fresh role-selection marker emits
 * one type-2 worn-item stream followed by one empty type-3 completion; after
 * that marker is consumed, a later grid bootstrap cannot replay those rows.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
#include "../src/server/mock-server.c"

static int inspect_equipment_stream(const u8 *packet, u32 packetLen,
                                    u32 *type2CountOut, u32 *type3CountOut,
                                    u32 *type2OrdinalOut, u32 *type3OrdinalOut)
{
    /* Responses add the object-count byte after the four-byte WT header. */
    u32 offset = 5;
    u32 ordinal = 0;
    u32 type2Count = 0;
    u32 type3Count = 0;
    u32 type2Ordinal = 0;
    u32 type3Ordinal = 0;
    while (offset + 6u <= packetLen)
    {
        u16 objectLen = (u16)(((u16)packet[offset + 4] << 8) |
                              packet[offset + 5]);
        u8 type = 0;

        if (objectLen < 6 || offset + objectLen > packetLen)
            return 1;
        ++ordinal;
        if (packet[offset] != 1 || packet[offset + 1] != 7 ||
            packet[offset + 2] != 7 ||
            !vm_net_mock_get_object_u8_field(packet + offset + 6,
                                             objectLen - 6,
                                             "type", &type))
        {
            offset += objectLen;
            continue;
        }
        if (type == 2)
        {
            ++type2Count;
            type2Ordinal = ordinal;
        }
        else if (type == 3)
        {
            ++type3Count;
            type3Ordinal = ordinal;
        }
        offset += objectLen;
    }

    if (offset != packetLen)
        return 1;
    *type2CountOut = type2Count;
    *type3CountOut = type3Count;
    *type2OrdinalOut = type2Ordinal;
    *type3OrdinalOut = type3Ordinal;
    return 0;
}

static int assert_initial_login_equipment_bootstrap(void)
{
    vm_mock_service_client_session session;
    vm_mock_service_client_session *savedSessions =
        g_vm_mock_service_client_sessions;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    vm_net_mock_role_db_file savedRoleDb = g_vm_net_mock_role_db;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    vm_net_mock_equipment_catalog_item savedCatalog =
        g_vm_net_mock_equipment_catalog[0];
    u32 savedCatalogCount = g_vm_net_mock_equipment_catalog_count;
    bool savedCatalogLoaded = g_vm_net_mock_equipment_catalog_loaded;
    u32 savedSeededRoleId = g_netMockBackpackGridSeededRoleId;
    u32 savedReseedRoleId = g_netMockBackpackGridReseedPendingRoleId;
    u8 packet[2048];
    u32 pos = 5;
    u8 objectCount = 0;
    u32 type2Count = 0;
    u32 type3Count = 0;
    u32 type2Ordinal = 0;
    u32 type3Ordinal = 0;
    int failed = 0;
    vm_net_mock_role_state *role = NULL;

    memset(&session, 0, sizeof(session));
    memset(packet, 0, sizeof(packet));
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    session.clientId = 0x1e71u;
    session.next = savedSessions;
    g_vm_mock_service_client_sessions = &session;
    g_vm_mock_service_active_client_id = session.clientId;

    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 10001;
    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = 10001;
    role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    role->equippedItems[0].itemId = 40020;
    role->equippedItems[0].durability = 90;
    role->equippedItems[0].durabilityMax = 100;

    memset(&g_vm_net_mock_equipment_catalog[0], 0,
           sizeof(g_vm_net_mock_equipment_catalog[0]));
    g_vm_net_mock_equipment_catalog[0].itemId = 40020;
    g_vm_net_mock_equipment_catalog[0].slot = 0;
    g_vm_net_mock_equipment_catalog[0].levelRequired = 1;
    g_vm_net_mock_equipment_catalog[0].durabilityMax = 100;
    g_vm_net_mock_equipment_catalog_count = 1;
    g_vm_net_mock_equipment_catalog_loaded = true;
    g_netMockBackpackGridSeededRoleId = 0;
    g_netMockBackpackGridReseedPendingRoleId = 0;

    packet[0] = 'W';
    packet[1] = 'T';
    vm_mock_service_initial_equipment_bootstrap_arm(role->roleId);
    if (!vm_net_mock_append_backpack_role_grid_main_objects(
            packet, sizeof(packet), &pos, &objectCount))
    {
        fputs("initial group bootstrap builder failed\n", stderr);
        failed = 1;
        goto cleanup;
    }
    vm_net_mock_finish_wt_packet(packet, pos, objectCount);
    if (inspect_equipment_stream(packet, pos, &type2Count, &type3Count,
                                 &type2Ordinal, &type3Ordinal) != 0 ||
        type2Count != 1 || type3Count != 1 || type2Ordinal >= type3Ordinal ||
        vm_mock_service_initial_equipment_bootstrap_matches(role->roleId) ||
        g_netMockBackpackGridSeededRoleId != role->roleId)
    {
        fprintf(stderr,
                "initial equipped-item stream contract failed type2=%u type3=%u "
                "ord2=%u ord3=%u marker=%u seeded=%u expected=%u\n",
                type2Count, type3Count, type2Ordinal, type3Ordinal,
                vm_mock_service_initial_equipment_bootstrap_matches(role->roleId) ? 1u : 0u,
                g_netMockBackpackGridSeededRoleId, role->roleId);
        failed = 1;
        goto cleanup;
    }

    memset(packet, 0, sizeof(packet));
    packet[0] = 'W';
    packet[1] = 'T';
    pos = 5;
    objectCount = 0;
    g_netMockBackpackGridSeededRoleId = 0;
    if (!vm_net_mock_append_backpack_role_grid_main_objects(
            packet, sizeof(packet), &pos, &objectCount))
    {
        fputs("post-login grid bootstrap builder failed\n", stderr);
        failed = 1;
        goto cleanup;
    }
    vm_net_mock_finish_wt_packet(packet, pos, objectCount);
    if (inspect_equipment_stream(packet, pos, &type2Count, &type3Count,
                                 &type2Ordinal, &type3Ordinal) != 0 ||
        type2Count != 0 || type3Count != 0)
    {
        fputs("post-login grid bootstrap replayed equipped-item rows\n", stderr);
        failed = 1;
    }

cleanup:
    g_vm_mock_service_client_sessions = savedSessions;
    g_vm_mock_service_active_client_id = savedClientId;
    g_vm_net_mock_role_db = savedRoleDb;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    g_vm_net_mock_equipment_catalog[0] = savedCatalog;
    g_vm_net_mock_equipment_catalog_count = savedCatalogCount;
    g_vm_net_mock_equipment_catalog_loaded = savedCatalogLoaded;
    g_netMockBackpackGridSeededRoleId = savedSeededRoleId;
    g_netMockBackpackGridReseedPendingRoleId = savedReseedRoleId;
    return failed;
}

int main(void)
{
    if (assert_initial_login_equipment_bootstrap() != 0)
        return 1;
    puts("initial login equipped bootstrap regression passed");
    return 0;
}
