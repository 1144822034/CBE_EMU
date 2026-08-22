/*
 * Lightweight regression for the admin HTTP framing and monster-list/drop
 * editor setup.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections
 *       -fdata-sections scripts/admin-request-length-regression.c
 *       obj/client/gifDecode.o obj/client/cbeParser.o obj/client/mystd.o
 *       obj/client/fontEngine.o obj/client/vmMalloc.o obj/client/fileIoEngine.o
 *       obj/client/lcd.o obj/client/automation_png.o obj/client/md5.o
 *       obj/server/mysql-client.o -Wl,--gc-sections -o
 *       tmp/admin-request-length-regression.exe -lpthread -liconv -lm
 *       -lmingw32 -lkernel32 -lws2_32
 *       Lib/unicorn-2.1.4/unicorn-import.lib -LLib/sdl2-2.0.10/lib
 *       -lSDL2main -lSDL2
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
    char emptyUtf8[8];
    char serviceForm[2048];
    char renderedNpcFields[32768];
    char renderedPortalFields[16384];
    char endpointHost[64];
    u16 endpointPort = 0;
    size_t serviceFormLen = 0;
    char operationFilterSql[768];
    char operationFilterQuery[512];
    u32 operationFilter = 0;
    vm_mock_admin_text renderedPage;
    vm_mock_admin_text renderedPortalPage;
    vm_mock_admin_scene_file sceneFixtures[2];
    vm_mock_admin_scene_portal portalFixture;
    vm_net_mock_npc_service_option
        serviceOptions[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
    u32 serviceOptionCount = 0;

    endpointPort = 19090;
    if (!vm_mock_service_parse_host_port("58.220.46.173:13317", endpointHost,
                                         sizeof(endpointHost), &endpointPort) ||
        strcmp(endpointHost, "58.220.46.173") != 0 || endpointPort != 13317)
    {
        fprintf(stderr, "host:port endpoint parsing failed\n");
        return 1;
    }
    endpointPort = 19090;
    if (!vm_mock_service_parse_host_port("gateway.example", endpointHost,
                                         sizeof(endpointHost), &endpointPort) ||
        strcmp(endpointHost, "gateway.example") != 0 || endpointPort != 19090)
    {
        fprintf(stderr, "host-only endpoint parsing failed\n");
        return 1;
    }
    endpointPort = 19090;
    if (!vm_mock_service_parse_host_port("19444", endpointHost,
                                         sizeof(endpointHost), &endpointPort) ||
        strcmp(endpointHost, "127.0.0.1") != 0 || endpointPort != 19444)
    {
        fprintf(stderr, "legacy port-only endpoint parsing failed\n");
        return 1;
    }
    endpointPort = 19090;
    if (!vm_mock_service_parse_host_port("[::1]:19444", endpointHost,
                                         sizeof(endpointHost), &endpointPort) ||
        strcmp(endpointHost, "::1") != 0 || endpointPort != 19444 ||
        vm_mock_service_parse_host_port("host:0", endpointHost,
                                        sizeof(endpointHost), &endpointPort))
    {
        fprintf(stderr, "IPv6 or invalid endpoint handling failed\n");
        return 1;
    }
    snprintf(g_mockServiceHost, sizeof(g_mockServiceHost),
             "203.0.113.17:19444");
    g_mockServicePort = 19090;
    if (!vm_mock_service_apply_configured_host_port() ||
        strcmp(g_mockServiceHost, "203.0.113.17") != 0 ||
        g_mockServicePort != 19444)
    {
        fprintf(stderr, "compiled g_mockServiceHost endpoint was not applied\n");
        return 1;
    }

    memset(emptyUtf8, 0, sizeof(emptyUtf8));
    vm_net_mock_gbk_label_to_utf8("", emptyUtf8, sizeof(emptyUtf8));
    if (emptyUtf8[0] != 0)
    {
        fprintf(stderr, "empty admin display text still has a placeholder\n");
        return 1;
    }
    memset(serviceForm, 0, sizeof(serviceForm));
    for (u32 kind = VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT;
         kind <= VM_NET_MOCK_NPC_KIND_MAX; ++kind)
    {
        int written = snprintf(
            serviceForm + serviceFormLen,
            sizeof(serviceForm) - serviceFormLen,
            "%sservice_option_name_%u=%s&service_option_description_%u=%s",
            serviceFormLen == 0 ? "service_enabled_1=1&" : "&", kind,
            kind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT ? "-" : "", kind,
            kind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT ? "-" : "");

        if (written <= 0 || (size_t)written >= sizeof(serviceForm) - serviceFormLen)
        {
            fprintf(stderr, "NPC service form fixture overflowed\n");
            return 1;
        }
        serviceFormLen += (size_t)written;
    }
    memset(serviceOptions, 0, sizeof(serviceOptions));
    if (!vm_mock_admin_form_npc_service_options(
            serviceForm, true, serviceOptions,
            VM_NET_MOCK_NPC_SERVICE_OPTION_MAX, &serviceOptionCount) ||
        serviceOptionCount != 1 || serviceOptions[0].kind != 1 ||
        serviceOptions[0].optionName[0] != 0 ||
        serviceOptions[0].optionDescription[0] != 0)
    {
        fprintf(stderr, "NPC service optional dash was not normalized to empty\n");
        return 1;
    }
    vm_mock_admin_text_init(&renderedPage, renderedNpcFields,
                            sizeof(renderedNpcFields));
    vm_mock_admin_render_npc_service_option_fields(
        &renderedPage, NULL, NULL, true);
    vm_mock_admin_render_instance_fields(&renderedPage, NULL, 0, NULL);
    vm_mock_admin_render_scene_picker_modal(&renderedPage);
    vm_mock_admin_render_npc_inventory_setup_pending(
        &renderedPage, true, 0);
    if (renderedPage.truncated ||
        strstr(renderedNpcFields, "data-npc-service-toggle=\"6\"") == NULL ||
        strstr(renderedNpcFields, "data-npc-service-toggle=\"10\"") == NULL ||
        strstr(renderedNpcFields,
               "data-npc-service-config=\"1\" hidden") == NULL ||
        strstr(renderedNpcFields,
               "data-npc-instance-teleport-fields hidden") == NULL ||
        strstr(renderedNpcFields,
               "data-npc-instance-spawn-fields hidden") == NULL ||
        strstr(renderedNpcFields,
               "name=\"instance_spawn_enemy_id\"") == NULL ||
        strstr(renderedNpcFields,
               "data-npc-instance-challenge-fields hidden") == NULL ||
        strstr(renderedNpcFields,
               "scene-resource-select\" name=\"instance_scene\"") == NULL ||
        strstr(renderedNpcFields, "data-scene-picker-open") == NULL ||
        strstr(renderedNpcFields, "id=\"scene-picker-modal\"") == NULL ||
        strstr(renderedNpcFields, "武器商店 专属库存") == NULL)
    {
        fprintf(stderr,
                "NPC service toggle/configuration markup is incomplete\n");
        return 1;
    }
    memset(sceneFixtures, 0, sizeof(sceneFixtures));
    memset(&portalFixture, 0, sizeof(portalFixture));
    snprintf(sceneFixtures[0].name, sizeof(sceneFixtures[0].name),
             "source.sce");
    snprintf(sceneFixtures[1].name, sizeof(sceneFixtures[1].name),
             "target.sce");
    snprintf(portalFixture.targetScene, sizeof(portalFixture.targetScene),
             "target.sce");
    vm_mock_admin_text_init(&renderedPortalPage, renderedPortalFields,
                            sizeof(renderedPortalFields));
    vm_mock_admin_render_sce_portal_editor(
        &renderedPortalPage, "source.sce", &portalFixture, 1, 1,
        sceneFixtures, 2);
    if (renderedPortalPage.truncated ||
        strstr(renderedPortalFields,
               "scene-resource-select\" name=\"target_scene\"") == NULL ||
        strstr(renderedPortalFields, "data-scene-picker-open") == NULL ||
        strstr(renderedPortalFields, "target.sce\" selected") == NULL)
    {
        fprintf(stderr, "SCE portal target picker markup is incomplete\n");
        return 1;
    }

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
    if (strstr(g_vm_mock_admin_script, "const setupNpcServices") == NULL ||
        strstr(g_vm_mock_admin_script, "data-npc-service-toggle") == NULL ||
        strstr(g_vm_mock_admin_script,
               "data-npc-instance-teleport-fields") == NULL ||
        strstr(g_vm_mock_admin_script,
               "data-npc-instance-spawn-fields") == NULL ||
        strstr(g_vm_mock_admin_script,
               "data-npc-instance-challenge-fields") == NULL ||
        strstr(g_vm_mock_admin_script, "setupNpcServices();") == NULL ||
        strstr(g_vm_mock_admin_script, "setupNpcKinds") != NULL)
    {
        fprintf(stderr,
                "NPC service controls do not drive immediate configuration visibility\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "const setupScenePicker") == NULL ||
        strstr(g_vm_mock_admin_script, "data-scene-picker-open") == NULL ||
        strstr(g_vm_mock_admin_script, "scene-resource-select") == NULL ||
        strstr(g_vm_mock_admin_script, "找到 ${shown} 个场景") == NULL)
    {
        fprintf(stderr,
                "searchable SCE picker is missing from the shared admin script\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "const setupContentResourceSearch") == NULL ||
        strstr(g_vm_mock_admin_script, "data-content-resource-search") == NULL ||
        strstr(g_vm_mock_admin_script, "data-content-resource-item") == NULL ||
        strstr(g_vm_mock_admin_script, "setupContentNavigation") == NULL)
    {
        fprintf(stderr,
                "content resource search or Actor navigation grouping is missing\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "const setupAdminToasts") == NULL ||
        strstr(g_vm_mock_admin_script, "#admin-toast-host") == NULL ||
        strstr(g_vm_mock_admin_script, "MutationObserver") == NULL ||
        strstr(g_vm_mock_admin_script, "setTimeout(dismiss,5000)") == NULL)
    {
        fprintf(stderr,
                "queued five-second admin toast notifications are missing\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "操作日志") == NULL ||
        strcmp(vm_mock_admin_operation_log_action_label("set-role-level"),
               "设置角色等级") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("add-money"),
               "增加普通钱币") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("grant-item"),
               "发放物品/装备") != 0)
    {
        fprintf(stderr,
                "admin operation log navigation or action labels are missing\n");
        return 1;
    }
    operationFilter = vm_mock_admin_operation_log_action_filter_from_query(
        "type=add-money&type=spend-wcoin-shop&type=spend-wcoin-instance");
    if (operationFilter == 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("spend-wcoin-shop"),
               "游戏内商城消费 W 币") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("spend-wcoin-instance"),
               "付费副本消费 W 币") != 0 ||
        !vm_mock_admin_operation_log_build_filter("audit-target",
                                                  operationFilter,
                                                  operationFilterSql,
                                                  sizeof(operationFilterSql)) ||
        strstr(operationFilterSql, "action_code='add-money'") == NULL ||
        strstr(operationFilterSql, "action_code='spend-wcoin-shop'") == NULL ||
        strstr(operationFilterSql, "action_code='spend-wcoin-instance'") == NULL)
    {
        fprintf(stderr,
                "multiple operation-log type filters or W-coin labels are missing\n");
        return 1;
    }
    vm_mock_admin_operation_log_append_action_filter_query(
        operationFilter, operationFilterQuery, sizeof(operationFilterQuery));
    if (strstr(operationFilterQuery, "&amp;type=add-money") == NULL ||
        strstr(operationFilterQuery, "&amp;type=spend-wcoin-shop") == NULL ||
        strstr(operationFilterQuery, "&amp;type=spend-wcoin-instance") == NULL)
    {
        fprintf(stderr,
                "operation-log pagination does not retain multiple type filters\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script,
               "grid-template-columns:216px minmax(0,1fr)") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-tabs{display:flex!important;grid-column:1;grid-row:2") == NULL ||
        strstr(g_vm_mock_admin_script, "@media(max-width:820px)") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-shell{width:min(1680px,calc(100vw - 48px))") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-content{display:flex!important;align-self:stretch!important;height:100%!important;flex-direction:column!important;min-height:0!important;overflow:auto!important") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-content>.grid,#admin-spa-content>.update-grid,#admin-spa-content>.shop-card{flex:1 1 auto;min-height:0}") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-content [data-admin-list]{flex:1 1 auto;min-height:0;overflow:auto!important") == NULL ||
        strstr(g_vm_mock_admin_script,
               "#admin-spa-content{display:contents") != NULL)
    {
        fprintf(stderr,
                "admin sidebar navigation layout or its small-screen fallback is missing\n");
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
    puts("admin request-length regression passed: service endpoint parsing + queued admin notifications + multi-type account operation logs + in-game W-coin labels + empty display + NPC service toggle/configuration + searchable SCE/GIF catalog + 24KiB body + monster search + bulk drops");
    return 0;
}
