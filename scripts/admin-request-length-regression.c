/*
 * Lightweight regression for the admin HTTP framing and monster-list/drop
 * editor setup.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections
 *       -fdata-sections scripts/admin-request-length-regression.c
 *       obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o
 *       obj/server/md5.o -Wl,--gc-sections -o
 *       tmp/admin-request-length-regression.exe -lpthread -liconv -lm
 *       -lkernel32 -lws2_32
 *
 * This only invokes pure request-length and embedded-script checks; it does
 * not open a listener, connect to MySQL, or modify application state.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(int argc, char **argv)
{
    const char requestHeader[] =
        "POST /admin-418yz6/action HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 24576\r\n\r\n";
    u32 contentLength = 0;
    size_t totalLength = 0;

    if (!vm_mock_admin_parse_content_length(requestHeader,
                                            strlen(requestHeader),
                                            &contentLength) ||
        contentLength != 24576 ||
        !vm_mock_admin_request_total_length(strlen(requestHeader),
                                            contentLength, &totalLength) ||
        totalLength != strlen(requestHeader) + contentLength)
    {
        fprintf(stderr, "large admin form was rejected by request framing\n");
        return 1;
    }
    if (vm_mock_admin_request_total_length(strlen(requestHeader),
                                           VM_MOCK_ADMIN_REQUEST_BODY_MAX + 1u,
                                           &totalLength))
    {
        fprintf(stderr, "resource-protection threshold was not enforced\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "const setupMonsterSearch") == NULL ||
        strstr(g_vm_mock_admin_script, "setupMonsterSearch();") == NULL ||
        strstr(g_vm_mock_admin_script, "monsterSearchBound") == NULL)
    {
        fprintf(stderr, "monster search is not owned by the shared admin script\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "const setupMonsterDrops") == NULL ||
        strstr(g_vm_mock_admin_script, "monster-drop-picker-modal") == NULL ||
        strstr(g_vm_mock_admin_script, "data-monster-drop-add") == NULL ||
        strstr(g_vm_mock_admin_script, "需求等级") == NULL ||
        strstr(g_vm_mock_admin_script, "option.dataset.level") == NULL)
    {
        fprintf(stderr,
                "monster bulk-drop picker or equipment metadata is missing "
                "from the shared admin script\n");
        return 1;
    }
    if (argc == 3 && strcmp(argv[1], "--write-js") == 0)
    {
        FILE *file = fopen(argv[2], "wb");

        if (file == NULL ||
            fwrite(g_vm_mock_admin_script, 1,
                   strlen(g_vm_mock_admin_script), file) !=
                strlen(g_vm_mock_admin_script))
        {
            if (file != NULL)
                fclose(file);
            fprintf(stderr, "failed to export shared admin JavaScript\n");
            return 1;
        }
        fclose(file);
    }
    puts("admin request-length regression passed: 24KiB body + monster search + bulk drops");
    return 0;
}
