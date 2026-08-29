/*
 * Resource-only regression for SCE copy/name-edit administration.
 *
 * It reads the checked-in SCE fixture, starts neither a listener nor a
 * database connection, and never writes a resource.  The test validates the
 * exact field-0x16 replacement primitive and container re-encoding used by
 * the title action; the copy path is deliberately byte-preserving and its
 * filename contract is checked independently.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool verify_encoded_payload(u8 type, const u8 *raw, u32 rawLen,
                                   const u8 *expected, u32 expectedLen)
{
    u32 declaredLen = 0;
    u8 *decoded = NULL;
    bool ok = false;

    if (raw == NULL || expected == NULL || rawLen < 5u)
        return false;
    declaredLen = (u32)raw[0] | ((u32)raw[1] << 8) |
                  ((u32)raw[2] << 16) | ((u32)raw[3] << 24);
    if (declaredLen != rawLen - 4u || raw[4] != type)
        return false;
    if (type == 1u)
        return expectedLen == declaredLen - 1u &&
               memcmp(raw + 5, expected, expectedLen) == 0;
    if (type != 2u || declaredLen < 9u ||
        (vm_net_mock_read_be32_at(raw + 4, 5) & 0x7fffffffu) != expectedLen)
    {
        return false;
    }
    decoded = (u8 *)malloc(expectedLen);
    if (decoded == NULL ||
        vm_net_mock_decode_lzss_resource_stream(raw + 4, declaredLen, decoded,
                                                expectedLen) != expectedLen)
    {
        goto done;
    }
    ok = memcmp(decoded, expected, expectedLen) == 0;

done:
    free(decoded);
    return ok;
}

int main(void)
{
    vm_mock_admin_scene_file files[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    vm_mock_admin_scene_portal portals[VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX];
    char savedResourceDir[sizeof(g_vm_net_mock_resource_dir)];
    char scene[64];
    char sceneUtf8[192];
    char rendered[8192];
    vm_mock_admin_text page;
    u8 *payload = NULL;
    u32 payloadLen = 0;
    u8 type = 0;
    u8 *changedPayload = NULL;
    u32 changedPayloadLen = 0;
    bool fieldChanged = false;
    u8 *encodedRaw = NULL;
    u32 encodedRawLen = 0;
    u32 fileCount = 0;
    u32 portalCount = 0;
    const vm_mock_admin_scene_portal *titlePortal = NULL;
    static const char replacementTitle[] = "SCE title regression";
    int result = 1;

    memset(files, 0, sizeof(files));
    memset(portals, 0, sizeof(portals));
    memset(savedResourceDir, 0, sizeof(savedResourceDir));
    memset(scene, 0, sizeof(scene));
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(rendered, 0, sizeof(rendered));
    snprintf(savedResourceDir, sizeof(savedResourceDir), "%s",
             g_vm_net_mock_resource_dir);
    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        fputs("SCE resource management fixture is unavailable\n", stderr);
        goto done;
    }
    fileCount = vm_mock_admin_collect_scene_files(
        files, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    for (u32 i = 0; i < fileCount; ++i)
    {
        portalCount = vm_mock_admin_collect_scene_portals(
            files[i].name, portals, VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX, NULL);
        for (u32 j = 0; j < portalCount; ++j)
        {
            if (portals[j].kind == VM_MOCK_ADMIN_PORTAL_NAMED &&
                portals[j].displayName[0] != 0 &&
                portals[j].displayNameLengthOffset != 0)
            {
                snprintf(scene, sizeof(scene), "%s", files[i].name);
                titlePortal = &portals[j];
                break;
            }
        }
        if (titlePortal != NULL)
            break;
    }
    if (scene[0] == 0 || titlePortal == NULL ||
        !vm_mock_admin_load_data_payload(scene, ".sce", &payload, &payloadLen,
                                         &type) ||
        !vm_mock_admin_sce_payload_has_header(payload, payloadLen) ||
        !vm_mock_admin_sce_string_field_equals(
            payload, payloadLen, titlePortal->displayNameLengthOffset, 0x16u,
            titlePortal->displayName) ||
        !vm_mock_admin_sce_replace_string_field_payload(
            payload, payloadLen, titlePortal->displayNameLengthOffset, 0x16u,
            titlePortal->displayName, replacementTitle, &changedPayload,
            &changedPayloadLen, &fieldChanged) ||
        !fieldChanged || changedPayload == NULL ||
        memcmp(payload, changedPayload, titlePortal->displayNameLengthOffset) !=
            0 ||
        !vm_mock_admin_sce_string_field_equals(
            changedPayload, changedPayloadLen,
            titlePortal->displayNameLengthOffset, 0x16u, replacementTitle) ||
        !vm_mock_admin_sce_encode_payload(type, changedPayload,
                                          changedPayloadLen, &encodedRaw,
                                          &encodedRawLen) ||
        !verify_encoded_payload(type, encodedRaw, encodedRawLen, changedPayload,
                                changedPayloadLen))
    {
        fputs("SCE title replacement or re-encoding contract regressed\n",
              stderr);
        goto done;
    }
    if (!vm_mock_admin_sce_copy_target_is_valid(
            scene, "sce-resource-copy-regression.sce") ||
        vm_mock_admin_sce_copy_target_is_valid(scene, scene) ||
        vm_mock_admin_sce_copy_target_is_valid(scene, "../invalid.sce"))
    {
        fputs("SCE copy filename contract regressed\n", stderr);
        goto done;
    }
    vm_net_mock_gbk_label_to_utf8(scene, sceneUtf8, sizeof(sceneUtf8));
    vm_mock_admin_text_init(&page, rendered, sizeof(rendered));
    vm_mock_admin_render_sce_resource_editor(&page, sceneUtf8, scene);
    if (page.truncated ||
        strstr(rendered, "value=\"copy-sce-resource\"") == NULL ||
        strstr(rendered, "name=\"new_scene_file\"") == NULL ||
        strstr(rendered, "value=\"save-sce-location-title\"") == NULL ||
        strstr(rendered, "name=\"scene_title\"") == NULL ||
        strstr(rendered, "field 0x16") == NULL ||
        strstr(rendered, "SCE2 的 MAP 引用") == NULL)
    {
        fputs("SCE resource editor form contract is missing\n", stderr);
        goto done;
    }
    printf("admin SCE resource management regression passed: %s\n", scene);
    result = 0;

done:
    free(payload);
    free(changedPayload);
    free(encodedRaw);
    snprintf(g_vm_net_mock_resource_dir, sizeof(g_vm_net_mock_resource_dir),
             "%s", savedResourceDir);
    return result;
}
