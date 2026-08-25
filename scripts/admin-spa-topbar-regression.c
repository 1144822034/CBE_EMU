#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    if (strstr(g_vm_mock_admin_script, "const setupAdminTopbar=()=>") == NULL ||
        strstr(g_vm_mock_admin_script, "main.before(topbar)") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-topbar>header[data-admin-spa-header]") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-shell{height:calc(100vh - 78px)!important") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-tabs,#admin-spa-content{grid-row:1!important}") == NULL)
    {
        fprintf(stderr, "admin topbar is no longer isolated from the two-column layout\n");
        return 1;
    }
    puts("admin spa topbar regression passed");
    return 0;
}
