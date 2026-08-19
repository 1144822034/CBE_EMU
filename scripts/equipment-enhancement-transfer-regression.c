/*
 * Deterministic server-only regression for the equipment transfer protocol.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections -w
 *       scripts/equipment-enhancement-transfer-regression.c
 *       obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o
 *       obj/server/md5.o -Wl,--gc-sections -o
 *       tmp/equipment-enhancement-transfer-regression.exe -lpthread -liconv
 *       -lm -lkernel32 -lws2_32
 *
 * The fixture does not start a listener or contact MySQL.  It installs a
 * bounded equipment/crystal catalog, invokes the production parser and packet
 * builder, and supplies explicit save callbacks to the transaction helper.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int g_save_calls = 0;

static bool save_success(const char *reason)
{
    ++g_save_calls;
    return reason != NULL && strcmp(reason, "equipment-transfer") == 0;
}

static bool save_failure(const char *reason)
{
    (void)reason;
    ++g_save_calls;
    return false;
}

static bool append_client_number(u8 *payload, u32 payloadCap, u32 *pos,
                                 const char *name, u32 value)
{
    u32 nameLen = name ? (u32)strlen(name) : 0;

    /* SendBattleSeqEvent uses the request object's ordinary int writer:
     * name length/name, NUL, numeric type 4, then one big-endian u32. */
    if (payload == NULL || pos == NULL || nameLen == 0 || nameLen > 0xff ||
        *pos + 1u + nameLen + 6u > payloadCap)
    {
        return false;
    }
    payload[(*pos)++] = (u8)nameLen;
    memcpy(payload + *pos, name, nameLen);
    *pos += nameLen;
    payload[(*pos)++] = 0;
    payload[(*pos)++] = 4;
    payload[(*pos)++] = (u8)(value >> 24);
    payload[(*pos)++] = (u8)(value >> 16);
    payload[(*pos)++] = (u8)(value >> 8);
    payload[(*pos)++] = (u8)value;
    return true;
}

static bool append_client_tagged_u16(u8 *payload, u32 payloadCap, u32 *pos,
                                     const char *name, u16 value)
{
    u32 nameLen = name ? (u32)strlen(name) : 0;

    /* The alternate client writer uses the object-u16 payload {0,2,u16}
     * inside the same outer four-byte value entry. */
    if (payload == NULL || pos == NULL || nameLen == 0 || nameLen > 0xff ||
        *pos + 1u + nameLen + 6u > payloadCap)
    {
        return false;
    }
    payload[(*pos)++] = (u8)nameLen;
    memcpy(payload + *pos, name, nameLen);
    *pos += nameLen;
    payload[(*pos)++] = 0;
    payload[(*pos)++] = 4;
    payload[(*pos)++] = 0;
    payload[(*pos)++] = 2;
    payload[(*pos)++] = (u8)(value >> 8);
    payload[(*pos)++] = (u8)value;
    return true;
}

static u32 build_transfer_request_format(u8 *request, u32 requestCap, u8 subtype,
                                         u16 destinationSeq, u16 sourceSeq,
                                         bool tagged, bool appendExtra)
{
    u8 payload[64];
    u32 payloadLen = 0;
    u32 objectLen = 0;
    u32 requestLen = 0;

    memset(request, 0, requestCap);
    memset(payload, 0, sizeof(payload));
    if (!(tagged
              ? append_client_tagged_u16(payload, sizeof(payload), &payloadLen,
                                         "seqd", destinationSeq)
              : append_client_number(payload, sizeof(payload), &payloadLen,
                                     "seqd", destinationSeq)) ||
        !(tagged
              ? append_client_tagged_u16(payload, sizeof(payload), &payloadLen,
                                         "seqs", sourceSeq)
              : append_client_number(payload, sizeof(payload), &payloadLen,
                                     "seqs", sourceSeq)) ||
        (appendExtra &&
         !append_client_number(payload, sizeof(payload), &payloadLen,
                               "extra", 1)))
    {
        return 0;
    }

    objectLen = 5u + payloadLen;
    requestLen = 4u + objectLen;
    if (requestLen > requestCap)
        return 0;
    request[0] = 'W';
    request[1] = 'T';
    request[2] = (u8)(requestLen >> 8);
    request[3] = (u8)requestLen;
    request[4] = 1;
    request[5] = 29;
    request[6] = subtype;
    request[7] = (u8)(objectLen >> 8);
    request[8] = (u8)objectLen;
    memcpy(request + 9, payload, payloadLen);
    return requestLen;
}

static u32 build_transfer_request(u8 *request, u32 requestCap, u8 subtype,
                                  u16 destinationSeq, u16 sourceSeq,
                                  bool appendExtra)
{
    return build_transfer_request_format(request, requestCap, subtype,
                                         destinationSeq, sourceSeq, false,
                                         appendExtra);
}

static bool response_payload(const u8 *response, u32 responseLen,
                             u8 subtype, const u8 **payloadOut,
                             u32 *payloadLenOut)
{
    u32 objectLen = 0;

    if (payloadOut)
        *payloadOut = NULL;
    if (payloadLenOut)
        *payloadLenOut = 0;
    if (response == NULL || responseLen < 11 ||
        response[0] != 'W' || response[1] != 'T' || response[4] != 1 ||
        response[5] != 1 || response[6] != 29 || response[7] != subtype)
    {
        return false;
    }
    objectLen = ((u32)response[9] << 8) | response[10];
    if (objectLen < 6 || objectLen + 5u != responseLen)
        return false;
    if (payloadOut)
        *payloadOut = response + 11;
    if (payloadLenOut)
        *payloadLenOut = objectLen - 6u;
    return true;
}

static bool expect_field_order(const u8 *payload, u32 payloadLen,
                               const char *const *fields, u32 fieldCount)
{
    u32 offset = 0;

    for (u32 i = 0; i < fieldCount; ++i)
    {
        u8 nameLen = 0;
        u16 valueLen = 0;
        size_t expectedLen = strlen(fields[i]);

        if (offset >= payloadLen)
            return false;
        nameLen = payload[offset++];
        if (nameLen != expectedLen || offset + nameLen + 2u > payloadLen ||
            memcmp(payload + offset, fields[i], nameLen) != 0)
        {
            return false;
        }
        offset += nameLen;
        valueLen = (u16)(((u16)payload[offset] << 8) | payload[offset + 1]);
        offset += 2;
        if (offset + valueLen > payloadLen)
            return false;
        offset += valueLen;
    }
    return offset == payloadLen;
}

static bool expect_number_type(const u8 *payload, u32 payloadLen,
                               const char *field, u32 expected,
                               u8 expectedType)
{
    u32 offset = 0;
    u32 fieldLen = field ? (u32)strlen(field) : 0;

    while (fieldLen != 0 && offset < payloadLen)
    {
        u8 nameLen = payload[offset++];
        const u8 *name = NULL;
        const u8 *valueBytes = NULL;
        u16 valueLen = 0;
        u32 value = 0;
        u8 actualType = 0;

        if (offset + nameLen + 2u > payloadLen)
            return false;
        name = payload + offset;
        offset += nameLen;
        valueLen = (u16)(((u16)payload[offset] << 8) | payload[offset + 1]);
        offset += 2;
        if (offset + valueLen > payloadLen)
            return false;
        valueBytes = payload + offset;
        offset += valueLen;
        if (nameLen != fieldLen || memcmp(name, field, fieldLen) != 0)
            continue;
        if (valueLen == 3 && valueBytes[0] == 0 && valueBytes[1] == 1)
        {
            actualType = 1;
            value = valueBytes[2];
        }
        else if (valueLen == 4 && valueBytes[0] == 0 && valueBytes[1] == 2)
        {
            actualType = 2;
            value = ((u32)valueBytes[2] << 8) | valueBytes[3];
        }
        else if (valueLen == 6 && valueBytes[0] == 0 && valueBytes[1] == 4)
        {
            actualType = 4;
            value = ((u32)valueBytes[2] << 24) |
                    ((u32)valueBytes[3] << 16) |
                    ((u32)valueBytes[4] << 8) | valueBytes[5];
        }
        else
            return false;
        return value == expected &&
               (expectedType == 0 || actualType == expectedType);
    }
    return false;
}

static bool expect_number(const u8 *payload, u32 payloadLen,
                          const char *field, u32 expected)
{
    return expect_number_type(payload, payloadLen, field, expected, 0);
}

static void seed_catalogs(void)
{
    memset(g_vm_net_mock_equipment_catalog, 0,
           sizeof(g_vm_net_mock_equipment_catalog));
    g_vm_net_mock_equipment_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog_count = 1;
    g_vm_net_mock_equipment_catalog[0].itemId = 12013;
    g_vm_net_mock_equipment_catalog[0].slot = 0;
    g_vm_net_mock_equipment_catalog[0].levelRequired = 20;
    g_vm_net_mock_equipment_catalog[0].durabilityMax = 100;

    memset(g_vm_net_mock_shop_catalog, 0,
           sizeof(g_vm_net_mock_shop_catalog));
    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_admin_db_loaded = true;
    g_vm_net_mock_shop_admin_db_valid = true;
    g_vm_net_mock_shop_catalog_count = 2;
    g_vm_net_mock_shop_catalog[0].itemId = 904;
    strcpy(g_vm_net_mock_shop_catalog[0].name, "L4 Crystal");
    g_vm_net_mock_shop_catalog[0].stack = 99;
    g_vm_net_mock_shop_catalog[0].enabled = 1;
    g_vm_net_mock_shop_catalog[1].itemId = 12013;
    strcpy(g_vm_net_mock_shop_catalog[1].name, "Equipment");
    g_vm_net_mock_shop_catalog[1].isEquip = 1;
    g_vm_net_mock_shop_catalog[1].stack = 1;
    g_vm_net_mock_shop_catalog[1].enabled = 1;
}

static vm_net_mock_role_state *seed_role(u8 destinationLevel,
                                         u8 sourceLevel,
                                         u32 crystalCount,
                                         u32 money)
{
    vm_net_mock_role_state *role = NULL;

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 7001;
    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = 7001;
    role->level = 70;
    role->money = money;
    role->backpackCapacity = 64;
    role->nextBackpackSeq = 104;
    role->backpackItemCount = crystalCount != 0 ? 3 : 2;

    role->backpackItems[0].itemId = 12013;
    role->backpackItems[0].seq = 101;
    role->backpackItems[0].count = 1;
    role->backpackItems[0].enhanceLevel = destinationLevel;
    role->backpackItems[0].durability = 100;
    role->backpackItems[0].durabilityMax = 100;

    role->backpackItems[1].itemId = 12013;
    role->backpackItems[1].seq = 102;
    role->backpackItems[1].count = 1;
    role->backpackItems[1].enhanceLevel = sourceLevel;
    role->backpackItems[1].durability = 100;
    role->backpackItems[1].durabilityMax = 100;

    if (crystalCount != 0)
    {
        role->backpackItems[2].itemId = 904;
        role->backpackItems[2].seq = 103;
        role->backpackItems[2].count = crystalCount;
    }
    vm_net_mock_role_normalize_backpack(role);
    return role;
}

static int test_exact_request_parser(void)
{
    u8 request[96];
    u32 requestLen = 0;
    vm_net_mock_equipment_transfer_request parsed;
    bool parsedOk = false;

    requestLen = build_transfer_request(request, sizeof(request), 5, 101, 102,
                                        false);
    parsedOk = vm_net_mock_parse_equipment_transfer_request(
        request, requestLen, &parsed);
    if (requestLen == 0 || !parsedOk ||
        parsed.subtype != 5 || parsed.destinationSeq != 101 ||
        parsed.sourceSeq != 102)
    {
        u32 offset = 4;
        u32 destination = 0;
        u32 source = 0;
        vm_net_mock_request_object object;
        bool haveObject = vm_net_mock_next_request_object(
            request, requestLen, &offset, &object);
        bool exact = haveObject &&
                     vm_net_mock_equipment_transfer_has_exact_fields(
                         object.payload, object.payloadLen, &destination,
                         &source);
        fprintf(stderr,
                "request debug len=%u parsed=%u parsed_type=%u parsed_d=%u parsed_s=%u object=%u offset=%u major=%u kind=%u subtype=%u payload=%u exact=%u seqd=%u seqs=%u bytes=",
                requestLen, parsedOk ? 1u : 0u, parsed.subtype,
                parsed.destinationSeq, parsed.sourceSeq,
                haveObject ? 1u : 0u, offset,
                haveObject ? object.major : 0u,
                haveObject ? object.kind : 0u,
                haveObject ? object.subtype : 0u,
                haveObject ? object.payloadLen : 0u,
                exact ? 1u : 0u, destination, source);
        for (u32 i = 0; i < requestLen; ++i)
            fprintf(stderr, "%02X", request[i]);
        fputc('\n', stderr);
        fputs("valid 29/5 transfer request was rejected\n", stderr);
        return 1;
    }
    requestLen = build_transfer_request_format(request, sizeof(request), 5,
                                               101, 102, true, false);
    parsedOk = vm_net_mock_parse_equipment_transfer_request(
        request, requestLen, &parsed);
    if (requestLen == 0 || !parsedOk || parsed.subtype != 5 ||
        parsed.destinationSeq != 101 || parsed.sourceSeq != 102)
    {
        fputs("tagged-u16 29/5 transfer request was rejected\n", stderr);
        return 1;
    }
    requestLen = build_transfer_request(request, sizeof(request), 5, 101, 102,
                                        true);
    if (vm_net_mock_parse_equipment_transfer_request(request, requestLen,
                                                      &parsed))
    {
        fputs("29/5 request with an extra field was accepted\n", stderr);
        return 1;
    }
    requestLen = build_transfer_request(request, sizeof(request), 7, 101, 102,
                                        false);
    if (vm_net_mock_parse_equipment_transfer_request(request, requestLen,
                                                      &parsed))
    {
        fputs("unrelated 29/7 request was accepted\n", stderr);
        return 1;
    }
    requestLen = build_transfer_request(request, sizeof(request), 6, 101, 101,
                                        false);
    if (vm_net_mock_parse_equipment_transfer_request(request, requestLen,
                                                      &parsed))
    {
        fputs("same source/destination request was accepted\n", stderr);
        return 1;
    }
    return 0;
}

static int test_preview_packets(void)
{
    static const char *levelOneFields[] = {"result", "money", "level"};
    static const char *levelFourFields[] = {
        "result", "flag", "id", "seq", "name", "money", "level"};
    u8 request[96];
    u8 response[512];
    const u8 *payload = NULL;
    u32 payloadLen = 0;
    u32 requestLen = 0;
    u32 responseLen = 0;

    seed_role(0, 1, 0, 10000);
    requestLen = build_transfer_request(request, sizeof(request), 5, 101, 102,
                                        false);
    responseLen = vm_net_mock_build_equipment_transfer_response(
        request, requestLen, response, sizeof(response));
    if (!response_payload(response, responseLen, 5, &payload, &payloadLen) ||
        !expect_field_order(payload, payloadLen, levelOneFields, 3) ||
        !expect_number(payload, payloadLen, "result", 3) ||
        !expect_number(payload, payloadLen, "money", 100) ||
        !expect_number(payload, payloadLen, "level", 1))
    {
        fputs("level-one 29/5 preview contract changed\n", stderr);
        return 1;
    }

    seed_role(0, 4, 2, 10000);
    responseLen = vm_net_mock_build_equipment_transfer_response(
        request, requestLen, response, sizeof(response));
    if (!response_payload(response, responseLen, 5, &payload, &payloadLen) ||
        !expect_field_order(payload, payloadLen, levelFourFields, 7) ||
        !expect_number(payload, payloadLen, "result", 1) ||
        !expect_number(payload, payloadLen, "flag", 1) ||
        !expect_number(payload, payloadLen, "id", 904) ||
        !expect_number(payload, payloadLen, "seq", 103) ||
        !expect_number(payload, payloadLen, "money", 400) ||
        !expect_number(payload, payloadLen, "level", 4))
    {
        fputs("level-four 29/5 preview contract changed\n", stderr);
        return 1;
    }

    seed_role(0, 4, 0, 10000);
    responseLen = vm_net_mock_build_equipment_transfer_response(
        request, requestLen, response, sizeof(response));
    if (!response_payload(response, responseLen, 5, &payload, &payloadLen) ||
        !expect_number(payload, payloadLen, "result", 1) ||
        !expect_number(payload, payloadLen, "flag", 2) ||
        !expect_number(payload, payloadLen, "seq", 0))
    {
        fputs("missing-crystal 29/5 preview contract changed\n", stderr);
        return 1;
    }
    return 0;
}

static int test_commit_rejections(void)
{
    u8 request[96];
    u8 response[4096];
    const u8 *payload = NULL;
    u32 payloadLen = 0;
    u32 requestLen = build_transfer_request(request, sizeof(request), 6,
                                            101, 102, false);
    u32 responseLen = 0;
    vm_net_mock_role_state before;

    seed_role(0, 4, 0, 10000);
    before = g_vm_net_mock_role_db.roles[0];
    responseLen = vm_net_mock_build_equipment_transfer_response(
        request, requestLen, response, sizeof(response));
    if (!response_payload(response, responseLen, 6, &payload, &payloadLen) ||
        !expect_number(payload, payloadLen, "result", 3) ||
        memcmp(&before, &g_vm_net_mock_role_db.roles[0], sizeof(before)) != 0)
    {
        fputs("missing-crystal 29/6 rejection changed role state\n", stderr);
        return 1;
    }

    seed_role(0, 4, 2, 399);
    before = g_vm_net_mock_role_db.roles[0];
    responseLen = vm_net_mock_build_equipment_transfer_response(
        request, requestLen, response, sizeof(response));
    if (!response_payload(response, responseLen, 6, &payload, &payloadLen) ||
        !expect_number(payload, payloadLen, "result", 4) ||
        memcmp(&before, &g_vm_net_mock_role_db.roles[0], sizeof(before)) != 0)
    {
        fputs("money-insufficient 29/6 rejection changed role state\n", stderr);
        return 1;
    }

    seed_role(4, 4, 2, 10000);
    before = g_vm_net_mock_role_db.roles[0];
    responseLen = vm_net_mock_build_equipment_transfer_response(
        request, requestLen, response, sizeof(response));
    if (!response_payload(response, responseLen, 6, &payload, &payloadLen) ||
        !expect_number(payload, payloadLen, "result", 5) ||
        memcmp(&before, &g_vm_net_mock_role_db.roles[0], sizeof(before)) != 0)
    {
        fputs("not-transferable 29/6 rejection changed role state\n", stderr);
        return 1;
    }
    return 0;
}

static int test_commit_and_rollback(void)
{
    static const char *successFields[] = {
        "result", "seq", "num", "seqd", "curleveld", "seqs", "curlevels"};
    vm_net_mock_equipment_transfer_request parsed = {6, 101, 102};
    vm_net_mock_equipment_transfer_result state;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state before;
    vm_net_mock_backpack_item_state *destination = NULL;
    vm_net_mock_backpack_item_state *source = NULL;
    vm_net_mock_backpack_item_state *crystal = NULL;
    u8 response[4096];
    const u8 *payload = NULL;
    u32 payloadLen = 0;
    u32 responseLen = 0;

    role = seed_role(0, 4, 2, 10000);
    g_save_calls = 0;
    if (vm_net_mock_equipment_transfer_commit(
            role, &parsed, &state, save_success) != 1 ||
        g_save_calls != 1)
    {
        fputs("successful transfer did not commit once\n", stderr);
        return 1;
    }
    destination = vm_net_mock_role_find_backpack_item(role, 0, 101);
    source = vm_net_mock_role_find_backpack_item(role, 0, 102);
    crystal = vm_net_mock_role_find_backpack_item(role, 904, 103);
    if (destination == NULL || destination->enhanceLevel != 4 ||
        source == NULL || source->enhanceLevel != 0 ||
        crystal == NULL || crystal->count != 1 || role->money != 9600 ||
        state.crystalSeq != 103 || state.crystalConsumed != 1)
    {
        fputs("successful transfer mutation is incomplete\n", stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_equipment_transfer_packet(
        &parsed, &state, response, sizeof(response));
    if (!response_payload(response, responseLen, 6, &payload, &payloadLen) ||
        !expect_field_order(payload, payloadLen, successFields, 7) ||
        !expect_number_type(payload, payloadLen, "result", 1, 1) ||
        !expect_number_type(payload, payloadLen, "seq", 103, 2) ||
        !expect_number_type(payload, payloadLen, "num", 1, 1) ||
        !expect_number_type(payload, payloadLen, "seqd", 101, 2) ||
        !expect_number_type(payload, payloadLen, "curleveld", 4, 1) ||
        !expect_number_type(payload, payloadLen, "seqs", 102, 2) ||
        !expect_number_type(payload, payloadLen, "curlevels", 0, 1))
    {
        fputs("successful 29/6 response contract changed\n", stderr);
        return 1;
    }

    role = seed_role(0, 4, 2, 10000);
    before = *role;
    g_save_calls = 0;
    if (vm_net_mock_equipment_transfer_commit(
            role, &parsed, &state, save_failure) != 6 ||
        g_save_calls != 1 || memcmp(&before, role, sizeof(before)) != 0)
    {
        fputs("failed persistence did not roll back the whole role\n", stderr);
        return 1;
    }

    role = seed_role(0, 1, 0, 10000);
    g_save_calls = 0;
    if (vm_net_mock_equipment_transfer_commit(
            role, &parsed, &state, save_success) != 1 ||
        state.crystalSeq != 0 || state.crystalConsumed != 0 ||
        role->money != 9900)
    {
        fputs("level-one no-crystal transfer changed\n", stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_equipment_transfer_packet(
        &parsed, &state, response, sizeof(response));
    if (!response_payload(response, responseLen, 6, &payload, &payloadLen) ||
        !expect_field_order(payload, payloadLen, successFields, 7) ||
        !expect_number_type(payload, payloadLen, "seq", 0, 2) ||
        !expect_number_type(payload, payloadLen, "num", 0, 1) ||
        !expect_number_type(payload, payloadLen, "curleveld", 1, 1) ||
        !expect_number_type(payload, payloadLen, "seqd", 101, 2) ||
        !expect_number_type(payload, payloadLen, "seqs", 102, 2) ||
        !expect_number_type(payload, payloadLen, "curlevels", 0, 1))
    {
        fputs("level-one 29/6 zero-crystal response changed\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    seed_catalogs();
    if (test_exact_request_parser() != 0 ||
        test_preview_packets() != 0 ||
        test_commit_rejections() != 0 ||
        test_commit_and_rollback() != 0)
    {
        return 1;
    }
    puts("equipment enhancement transfer regression passed: exact 29/5+29/6 parser, preview, rejection, commit, tagged-u16 instance updates, and persistence rollback");
    return 0;
}
