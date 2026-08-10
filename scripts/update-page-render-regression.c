/*
 * Regression for the administrative game-content update page.
 *
 * This exercises the exact GET /?tab=updates renderer in-process.  It opens
 * no socket, does not connect to MySQL and does not write any resource or
 * runtime state.  A page-generation fault must not be discovered only after
 * the administrative listener worker has died.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    char *response = NULL;
    size_t responseLen = 0;
    u8 manifest[VM_NET_MOCK_CONTENT_UPDATE_PAYLOAD_MAX];

    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        fputs("unable to configure test resource directory\n", stderr);
        return 1;
    }
    response = (char *)calloc(1, VM_MOCK_ADMIN_RESPONSE_MAX);
    if (response == NULL)
    {
        fputs("unable to allocate update page response buffer\n", stderr);
        return 1;
    }

    /* The page renderer must remain testable without a listener or a MySQL
     * connection.  Supply an already-loaded release; production loading is
     * covered by the MySQL schema/transaction contract in the server code. */
    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    g_vm_net_mock_content_update_loaded = true;
    g_vm_net_mock_content_update.enabled = true;
    g_vm_net_mock_content_update.id = 1;
    if (!vm_net_mock_content_update_add_name(
            &g_vm_net_mock_content_update, "fixture.sce") ||
        vm_net_mock_content_update_build_payload(
            &g_vm_net_mock_content_update, manifest, sizeof(manifest),
            &g_vm_net_mock_content_update.code) == 0)
    {
        fputs("unable to prepare update page fixture\n", stderr);
        free(response);
        return 1;
    }

    vm_mock_admin_render_page(response, VM_MOCK_ADMIN_RESPONSE_MAX,
                              "tab=updates");
    responseLen = strlen(response);
    if (responseLen == 0 ||
        strstr(response, "\xE6\xB8\xB8\xE6\x88\x8F\xE5\x86\x85\xE5\xAE\xB9\xE6\x9B\xB4\xE6\x96\xB0\xE7\xAE\xA1\xE7\x90\x86") == NULL ||
        strstr(response, "\xE5\x90\xAF\xE5\x8A\xA8\xE6\xA8\xA1\xE5\x9D\x97\xE6\x9B\xB4\xE6\x96\xB0") == NULL ||
        strstr(response, "server_content_update_releases") == NULL ||
        strstr(response, "server_content_update_files") == NULL ||
        strstr(response, "add-content-update-files") == NULL ||
        strstr(response, "multiple") == NULL ||
        strstr(response, "b_bamboo.actor") == NULL ||
        strstr(response, "publish-named-update") != NULL ||
        strstr(response, "update-resource-picker") != NULL ||
        responseLen >= VM_MOCK_ADMIN_RESPONSE_MAX - 1)
    {
        fprintf(stderr, "update page render contract failed: bytes=%zu\n",
                responseLen);
        free(response);
        return 1;
    }

    printf("update page render regression passed: bytes=%zu\n", responseLen);
    free(response);
    return 0;
}
