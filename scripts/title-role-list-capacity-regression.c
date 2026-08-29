/*
 * Deterministic server-only regression for the title role-list capacity
 * boundary.  It starts no listener and does not connect to MySQL: the fixture
 * provides five valid in-memory roles with maximum stored-name lengths, then
 * drives the real WT 1/1/4 server-select dispatcher and legacy staged builder.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool begin_request_object(u8 *out, u32 outCap, u32 *pos, u8 kind,
                                 u8 subtype, u32 *objectStart)
{
    if (out == NULL || pos == NULL || objectStart == NULL || *pos + 5 > outCap)
        return false;
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

static bool build_server_select_request(u8 *out, u32 outCap, u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL || outCap < pos ||
        !begin_request_object(out, outCap, &pos, 1, 4, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "serverID", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "moneytype", 0))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    *lengthOut = pos;
    return true;
}

static bool response_actorinfo(const u8 *packet, u32 packetLen,
                               const u8 **actorinfoOut, u16 *actorinfoLenOut)
{
    vm_net_mock_response_object object;
    u32 offset = 5;

    if (actorinfoOut)
        *actorinfoOut = NULL;
    if (actorinfoLenOut)
        *actorinfoLenOut = 0;
    memset(&object, 0, sizeof(object));
    return packet != NULL && packetLen >= 5 && packet[0] == 'W' &&
           packet[1] == 'T' && packet[4] == 1 &&
           vm_net_mock_next_response_object(packet, packetLen, &offset, &object) &&
           offset == packetLen && object.major == 1 && object.kind == 1 &&
           object.subtype == 4 &&
           vm_net_mock_get_object_entry_bytes(object.payload, object.payloadLen,
                                              "actorinfo", actorinfoOut,
                                              actorinfoLenOut);
}

static bool contains_bytes(const u8 *data, u32 dataLen, const char *needle)
{
    size_t needleLen = needle ? strlen(needle) : 0;

    if (data == NULL || needle == NULL || needleLen == 0 || needleLen > dataLen)
        return false;
    for (u32 offset = 0; offset + needleLen <= dataLen; ++offset)
    {
        if (memcmp(data + offset, needle, needleLen) == 0)
            return true;
    }
    return false;
}

static bool staged_response_actorinfo(const u8 *packet, u32 packetLen,
                                      const u8 **actorinfoOut,
                                      u16 *actorinfoLenOut)
{
    vm_net_mock_response_object first;
    vm_net_mock_response_object second;
    u32 offset = 5;

    if (actorinfoOut)
        *actorinfoOut = NULL;
    if (actorinfoLenOut)
        *actorinfoLenOut = 0;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    return packet != NULL && packetLen >= 5 && packet[0] == 'W' &&
           packet[1] == 'T' && packet[4] == 2 &&
           vm_net_mock_next_response_object(packet, packetLen, &offset, &first) &&
           first.major == 1 && first.kind == 1 && first.subtype == 16 &&
           vm_net_mock_next_response_object(packet, packetLen, &offset, &second) &&
           offset == packetLen && second.major == 1 && second.kind == 1 &&
           second.subtype == 4 &&
           vm_net_mock_get_object_entry_bytes(second.payload, second.payloadLen,
                                              "actorinfo", actorinfoOut,
                                              actorinfoLenOut);
}

static bool actorinfo_has_fixture_names(const u8 *actorinfo, u16 actorinfoLen,
                                        u32 roleCount)
{
    if (roleCount == 0 || roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        return false;
    for (u32 index = 0; index < roleCount; ++index)
    {
        char name[sizeof(g_vm_net_mock_role_db.roles[index].name)];

        memset(name, 'A' + (int)index, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        if (!contains_bytes(actorinfo, actorinfoLen, name))
            return false;
    }
    return true;
}

int main(void)
{
    u8 request[96];
    u8 response[4096];
    u8 stagedResponse[4096];
    u8 compactActorinfo[VM_NET_MOCK_TITLE_ROLE_LIST_ACTORINFO_CAP];
    const u8 *actorinfo = NULL;
    u16 actorinfoLen = 0;
    u32 requestLen = 0;
    u32 responseLen = 0;
    u32 stagedResponseLen = 0;
    u32 compactActorinfoLen = 0;

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    g_vm_net_mock_role_db.roleCount = VM_NET_MOCK_ROLE_DB_MAX_ROLES;
    g_vm_net_mock_role_db.activeRoleId = 920001;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    for (u32 index = 0; index < VM_NET_MOCK_ROLE_DB_MAX_ROLES; ++index)
    {
        vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[index];

        role->roleId = 920001u + index;
        role->job = (u8)(index % 3u + 1u);
        role->sex = (u8)(index % 2u);
        role->level = 1u + index;
        memset(role->name, 'A' + (int)index, sizeof(role->name) - 1);
        role->name[sizeof(role->name) - 1] = 0;
    }

    memset(g_vm_net_mock_login_servers, 0, sizeof(g_vm_net_mock_login_servers));
    g_vm_net_mock_login_server_count = 1;
    g_vm_net_mock_login_server_db_loaded = true;
    g_vm_net_mock_login_server_db_valid = true;
    g_vm_net_mock_login_servers[0].serverId = 1;
    g_vm_net_mock_login_servers[0].displayColor = 0x112233u;
    g_vm_net_mock_login_servers[0].enabled = true;
    snprintf(g_vm_net_mock_login_servers[0].displayName,
             sizeof(g_vm_net_mock_login_servers[0].displayName), "fixture");
    snprintf(g_vm_net_mock_login_servers[0].label,
             sizeof(g_vm_net_mock_login_servers[0].label), "ready");

    compactActorinfoLen = vm_net_mock_build_title_role_list_actorinfo(
        compactActorinfo, sizeof(compactActorinfo));
    if (compactActorinfoLen != sizeof(compactActorinfo) ||
        compactActorinfoLen <= 128 ||
        !actorinfo_has_fixture_names(compactActorinfo,
                                     (u16)compactActorinfoLen,
                                     VM_NET_MOCK_ROLE_DB_MAX_ROLES))
    {
        fprintf(stderr, "compact title actorinfo capacity failed len=%u cap=%u\n",
                compactActorinfoLen, (u32)sizeof(compactActorinfo));
        return 1;
    }

    if (!build_server_select_request(request, sizeof(request), &requestLen))
    {
        fputs("could not construct WT 1/1/4 server-select request\n", stderr);
        return 1;
    }
    g_netLastHandledValid = 0;
    g_netLastHandledSource[0] = 0;
    responseLen = vm_net_mock_build_response(request, requestLen,
                                             response, sizeof(response));
    if (responseLen == 0 || !g_netLastHandledValid ||
        strcmp(g_netLastHandledSource, "builtin-title-server-select") != 0 ||
        !response_actorinfo(response, responseLen, &actorinfo, &actorinfoLen) ||
        actorinfoLen != compactActorinfoLen ||
        !actorinfo_has_fixture_names(actorinfo, actorinfoLen,
                                     VM_NET_MOCK_ROLE_DB_MAX_ROLES))
    {
        fputs("WT 1/1/4 did not return the full title role list\n", stderr);
        return 1;
    }

    stagedResponseLen = vm_net_mock_build_title_rolelist_stage_response(
        stagedResponse, sizeof(stagedResponse));
    if (stagedResponseLen == 0 ||
        !staged_response_actorinfo(stagedResponse, stagedResponseLen,
                                  &actorinfo, &actorinfoLen) ||
        actorinfoLen != compactActorinfoLen ||
        !actorinfo_has_fixture_names(actorinfo, actorinfoLen,
                                     VM_NET_MOCK_ROLE_DB_MAX_ROLES))
    {
        fputs("staged title role list did not retain the full actorinfo table\n",
              stderr);
        return 1;
    }

    g_vm_net_mock_role_db.roleCount = 1;
    compactActorinfoLen = vm_net_mock_build_title_role_list_actorinfo(
        compactActorinfo, sizeof(compactActorinfo));
    g_netLastHandledValid = 0;
    g_netLastHandledSource[0] = 0;
    responseLen = vm_net_mock_build_response(request, requestLen,
                                             response, sizeof(response));
    if (compactActorinfoLen == 0 || compactActorinfoLen >= 128 ||
        !actorinfo_has_fixture_names(compactActorinfo, (u16)compactActorinfoLen, 1) ||
        responseLen == 0 || !g_netLastHandledValid ||
        strcmp(g_netLastHandledSource, "builtin-title-server-select") != 0 ||
        !response_actorinfo(response, responseLen, &actorinfo, &actorinfoLen) ||
        actorinfoLen != compactActorinfoLen ||
        !actorinfo_has_fixture_names(actorinfo, actorinfoLen, 1))
    {
        fputs("single-role title server-select regression failed\n", stderr);
        return 1;
    }

    printf("title role-list capacity regression passed max_actorinfo=%u roles=%u "
           "single_actorinfo=%u\n",
           VM_NET_MOCK_TITLE_ROLE_LIST_ACTORINFO_CAP,
           VM_NET_MOCK_ROLE_DB_MAX_ROLES, compactActorinfoLen);
    return 0;
}
