/*
 * Regression for instance-guide scene targets.
 *
 * This is deliberately process-local: it reads only the authoritative
 * resource tree and never opens a listener or MySQL connection.  A selected
 * b_* SCE must stay an exact `.sce` resource key through the admin converter
 * and old extensionless rows may be migrated only when that exact SCE exists.
 * The WT18/7 payload loader must continue to reject the old bare name, since
 * accepting it would hide a broken scene-transition contract.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    static const char fullScene[] =
        "b_29\xC3\xCE\xBE\xB3\xBF\xD5\xBC\xE4.sce"; /* b_29梦境空间.sce */
    static const char bareScene[] =
        "b_29\xC3\xCE\xBE\xB3\xBF\xD5\xBC\xE4"; /* b_29梦境空间 */
    char adminKey[64];
    char canonicalKey[64];
    char sourcePath[512];
    u8 payload[1024];
    u32 payloadLen = 0;

    memset(adminKey, 0, sizeof(adminKey));
    memset(canonicalKey, 0, sizeof(canonicalKey));
    memset(sourcePath, 0, sizeof(sourcePath));
    if (!vm_mock_admin_scene_file_to_runtime_key(
            fullScene, adminKey, sizeof(adminKey)) ||
        strcmp(adminKey, fullScene) != 0)
    {
        fputs("admin scene selector rewrote the exact SCE resource key\n", stderr);
        return 1;
    }
    if (!vm_net_mock_dynamic_npc_instance_scene_canonicalize(
            bareScene, canonicalKey, sizeof(canonicalKey)) ||
        strcmp(canonicalKey, fullScene) != 0)
    {
        fputs("legacy instance scene key did not resolve to an exact SCE file\n", stderr);
        return 1;
    }
    payloadLen = vm_net_mock_load_named_update_payload(
        fullScene, payload, sizeof(payload), sourcePath, sizeof(sourcePath));
    if (payloadLen == 0 || strcmp(sourcePath, "") == 0 ||
        vm_net_mock_load_named_update_payload(
            bareScene, payload, sizeof(payload), NULL, 0) != 0)
    {
        fputs("WT18/7 resource lookup did not enforce the exact SCE filename\n",
              stderr);
        return 1;
    }
    printf("instance scene resource-key regression passed: bytes=%u source=%s\n",
           payloadLen, sourcePath);
    return 0;
}
