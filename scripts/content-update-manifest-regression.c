/*
 * Pure server-side regression for the startup content invalidation protocol.
 *
 * It creates real WT 18/9 and 18/8 objects through the same packet helpers as
 * the mock service, but does not open a listener, connect to MySQL, write a
 * release manifest, or touch a client cache.  The CBE's startup parser expects
 * type=1/id/code in 18/9 and a u8-length filename list in 18/8.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static u32 make_request(u8 *out, u32 outCap, u8 subtype)
{
    u32 pos = 4;
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x12, subtype,
                                     &objectStart))
    {
        return 0;
    }
    if (subtype == 9)
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "version", 0) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "cbm", "") ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "client",
                                           "content-update-regression"))
        {
            return 0;
        }
    }
    else if (subtype == 8)
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "version", 0) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "screen",
                                           "startup") ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "start", 0))
        {
            return 0;
        }
    }
    else
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    return pos;
}

int main(void)
{
    static const char scene[] = "00fixture.sce";
    static const char actor[] = "fixture.actor";
    static const char module[] = "mmGameMstarWqvga.cbm";
    u8 request[512];
    u8 response[2048];
    u8 expected[64];
    const u8 *data = NULL;
    u16 dataLen = 0;
    u8 type = 0;
    u8 kind = 0;
    u8 subtype = 0;
    u32 id = 0;
    u32 code = 0;
    u32 totalSize = 0;
    u32 totalNum = 0;
    u32 version = 0;
    u32 crc = 0;
    u32 requestLen = 0;
    u32 responseLen = 0;
    u32 expectedLen = 0;
    u32 expectedCode = 0;

    memset(g_vm_net_mock_update_slots, 0, sizeof(g_vm_net_mock_update_slots));
    g_vm_net_mock_update_catalog_loaded = true;
    g_vm_net_mock_content_update_loaded = true;
    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    g_vm_net_mock_content_update.enabled = true;
    g_vm_net_mock_content_update.id = 77;
    if (!vm_net_mock_content_update_add_name(&g_vm_net_mock_content_update,
                                             scene) ||
        !vm_net_mock_content_update_add_name(&g_vm_net_mock_content_update,
                                             actor) ||
        vm_net_mock_content_update_add_name(&g_vm_net_mock_content_update,
                                            module))
    {
        fputs("could not initialise manifest fixture\n", stderr);
        return 1;
    }
    expectedLen = vm_net_mock_content_update_build_payload(
        &g_vm_net_mock_content_update, expected, sizeof(expected),
        &expectedCode);
    if (expectedLen != sizeof(scene) + sizeof(actor) ||
        expected[0] != sizeof(scene) - 1 ||
        memcmp(expected + 1, scene, sizeof(scene) - 1) != 0 ||
        expected[sizeof(scene)] != sizeof(actor) - 1 ||
        memcmp(expected + sizeof(scene) + 1, actor,
               sizeof(actor) - 1) != 0)
    {
        fputs("manifest is not a mixed non-CBM u8-length resource list\n",
              stderr);
        return 1;
    }
    g_vm_net_mock_content_update.code = expectedCode;

    requestLen = make_request(request, sizeof(request), 9);
    responseLen = vm_net_mock_build_version_response(request, requestLen,
                                                      response, sizeof(response));
    if (responseLen == 0 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 0x12 || subtype != 5 ||
        !vm_net_mock_get_object_u8_field(response, responseLen, "type", &type) ||
        !vm_net_mock_get_object_u32_field(response, responseLen, "id", &id) ||
        !vm_net_mock_get_object_u32_field(response, responseLen, "code", &code) ||
        type != 1 || id != g_vm_net_mock_content_update.id ||
        code != expectedCode)
    {
        fputs("WT 18/9 content target contract failed\n", stderr);
        return 1;
    }

    requestLen = make_request(request, sizeof(request), 8);
    responseLen = vm_net_mock_build_content_update_chunk_response(
        request, requestLen, response, sizeof(response));
    if (responseLen == 0 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 0x12 || subtype != 8 ||
        !vm_net_mock_get_object_u32_field(response, responseLen, "totalsize",
                                          &totalSize) ||
        !vm_net_mock_get_object_u32_field(response, responseLen, "totalnum",
                                          &totalNum) ||
        !vm_net_mock_get_object_u32_field(response, responseLen, "version",
                                          &version) ||
        !vm_net_mock_get_object_u32_field(response, responseLen, "crc", &crc) ||
        !vm_net_mock_get_object_blob_field(response, responseLen, "data",
                                           &data, &dataLen) ||
        totalSize != expectedLen || totalNum != 2 ||
        version != g_vm_net_mock_content_update.id ||
        crc != expectedCode || dataLen != expectedLen ||
        memcmp(data, expected, expectedLen) != 0)
    {
        fputs("WT 18/8 content manifest chunk contract failed\n", stderr);
        return 1;
    }
    printf("content update manifest regression passed: id=%u bytes=%u code=%u\n",
           id, expectedLen, expectedCode);
    return 0;
}
