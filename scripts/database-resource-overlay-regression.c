/* Verify that mutable scene publications are isolated by MySQL database while
 * immutable base-resource reads never observe an overlay. */
#include <stdio.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool write_fixture(const char *path, const char *bytes)
{
    FILE *fp = fopen(path, "wb");
    size_t len = bytes ? strlen(bytes) : 0;
    bool ok = fp != NULL && fwrite(bytes, 1, len, fp) == len;
    if (fp != NULL && fclose(fp) != 0)
        ok = false;
    return ok;
}

static bool read_exact(const char *path, const char *expected)
{
    char bytes[32];
    u32 len = 0;
    memset(bytes, 0, sizeof(bytes));
    len = vm_net_mock_load_response_file(path, (u8 *)bytes,
                                         sizeof(bytes) - 1u);
    return len == strlen(expected) && memcmp(bytes, expected, len) == 0;
}

int main(void)
{
    const char *root = "tmp/resource-overlay-regression";
    char path[1400];
    char selected[1400];
    u8 payload[32];
    u32 payloadLen = 0;

    (void)_mkdir("tmp");
    (void)_mkdir(root);
    if (!write_fixture("tmp/resource-overlay-regression/task.dsh", "task") ||
        !write_fixture("tmp/resource-overlay-regression/test.sce", "base") ||
        !vm_net_mock_set_resource_dir(root))
    {
        fputs("base resource fixture setup failed\n", stderr);
        return 1;
    }

    _putenv_s("CBE_MYSQL_DATABASE", "db_a");
    if (!vm_net_mock_build_overlay_resource_path("test.sce", path,
                                                 sizeof(path)) ||
        !write_fixture(path, "overlay-a"))
    {
        fputs("db_a overlay setup failed\n", stderr);
        return 1;
    }

    _putenv_s("CBE_MYSQL_DATABASE", "db_b");
    if (!vm_net_mock_build_overlay_resource_path("test.sce", path,
                                                 sizeof(path)) ||
        !write_fixture(path, "overlay-b"))
    {
        fputs("db_b overlay setup failed\n", stderr);
        return 1;
    }

    _putenv_s("CBE_MYSQL_DATABASE", "db_a");
    memset(payload, 0, sizeof(payload));
    payloadLen = vm_net_mock_load_requested_resource_payload(
        "test.sce", payload, sizeof(payload), selected, sizeof(selected));
    if (payloadLen != strlen("overlay-a") ||
        memcmp(payload, "overlay-a", payloadLen) != 0 ||
        strstr(selected, "/db_a/test.sce") == NULL)
    {
        fputs("db_a download did not use its overlay\n", stderr);
        return 1;
    }

    _putenv_s("CBE_MYSQL_DATABASE", "db_b");
    if (!vm_net_mock_open_server_scene_resource("test.sce", NULL, selected,
                                                sizeof(selected)) ||
        strstr(selected, "/db_b/test.sce") == NULL ||
        !read_exact(selected, "overlay-b"))
    {
        fputs("db_b scene lookup did not use its overlay\n", stderr);
        return 1;
    }

    _putenv_s("CBE_MYSQL_DATABASE", "db_without_overlay");
    if (!vm_net_mock_update_resource_path("test.sce", selected,
                                         sizeof(selected)) ||
        !read_exact(selected, "base"))
    {
        fputs("overlay miss did not fall back to immutable base\n", stderr);
        return 1;
    }
    if (!vm_net_mock_open_server_base_resource("test.sce", NULL, selected,
                                               sizeof(selected)) ||
        !read_exact(selected, "base"))
    {
        fputs("base-resource read observed an overlay\n", stderr);
        return 1;
    }

    _putenv_s("CBE_MYSQL_DATABASE", "../unsafe");
    if (vm_net_mock_build_overlay_resource_path("test.sce", path,
                                                sizeof(path)))
    {
        fputs("unsafe database name was accepted\n", stderr);
        return 1;
    }

    puts("database resource overlay regression passed: publications isolated; base immutable");
    return 0;
}
