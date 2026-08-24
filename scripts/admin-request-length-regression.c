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
 * This invokes request-length and embedded-script checks plus one loopback
 * socket-pair test for proxy-rewritten Host/Origin headers. It does not
 * connect to MySQL or modify application state.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static bool vm_mock_admin_open_loopback_pair(
    vm_mock_service_socket *serverOut, vm_mock_service_socket *clientOut)
{
    vm_mock_service_socket listener = VM_MOCK_SERVICE_INVALID_SOCKET;
    vm_mock_service_socket client = VM_MOCK_SERVICE_INVALID_SOCKET;
    vm_mock_service_socket server = VM_MOCK_SERVICE_INVALID_SOCKET;
    struct sockaddr_in address;
#ifdef _WIN32
    int addressLen = sizeof(address);
#else
    socklen_t addressLen = sizeof(address);
#endif

    if (serverOut == NULL || clientOut == NULL ||
        !vm_mock_service_socket_init())
    {
        return false;
    }
    *serverOut = VM_MOCK_SERVICE_INVALID_SOCKET;
    *clientOut = VM_MOCK_SERVICE_INVALID_SOCKET;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == VM_MOCK_SERVICE_INVALID_SOCKET)
        goto fail;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &addressLen) != 0 ||
        listen(listener, 1) != 0)
    {
        goto fail;
    }
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == VM_MOCK_SERVICE_INVALID_SOCKET ||
        connect(client, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        goto fail;
    }
    server = accept(listener, NULL, NULL);
    if (server == VM_MOCK_SERVICE_INVALID_SOCKET)
        goto fail;
    vm_mock_service_socket_close(listener);
    *serverOut = server;
    *clientOut = client;
    return true;

fail:
    vm_mock_service_socket_close(listener);
    vm_mock_service_socket_close(server);
    vm_mock_service_socket_close(client);
    return false;
}

int main(int argc, char **argv)
{
    const char requestHeader[] =
        "POST /admin-418yz6/action HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 24576\r\n\r\n";
    char proxyRequest[] =
        "GET /healthz HTTP/1.1\r\n"
        "Host: 127.0.0.1:19091\r\n"
        "Origin: https://admin.example.test\r\n\r\n";
    char trustedProxyLoginRequest[] =
        "POST /user/login HTTP/1.1\r\n"
        "Host: account.example.test\r\n"
        "X-Real-IP: 203.0.113.7\r\n"
        "X-Forwarded-For: 203.0.113.7\r\n"
        "Content-Length: 0\r\n\r\n";
    char untrustedProxyLoginRequest[] =
        "POST /user/login HTTP/1.1\r\n"
        "Host: account.example.test\r\n"
        "X-Real-IP: 203.0.113.7\r\n"
        "X-Forwarded-For: 203.0.113.7\r\n"
        "Content-Length: 0\r\n\r\n";
    char forwardedForLoginRequest[] =
        "POST /user/login HTTP/1.1\r\n"
        "Host: account.example.test\r\n"
        "X-Forwarded-For: 203.0.113.8\r\n"
        "Content-Length: 0\r\n\r\n";
    char forwardedForChainRequest[] =
        "POST /user/login HTTP/1.1\r\n"
        "Host: account.example.test\r\n"
        "X-Forwarded-For: 203.0.113.8, 198.51.100.10\r\n"
        "Content-Length: 0\r\n\r\n";
    const char paymentCallbackForm[] =
        "payId=JH202608240001&param=P202608240001&type=1&price=1.00&"
        "reallyPrice=1.00&sign=0123456789abcdef0123456789abcdef";
    char proxyResponse[1024];
    vm_mock_service_socket proxyServer = VM_MOCK_SERVICE_INVALID_SOCKET;
    vm_mock_service_socket proxyClient = VM_MOCK_SERVICE_INVALID_SOCKET;
    int proxyResponseLen = 0;
    u32 contentLength = 0;
    size_t totalLength = 0;
    char emptyUtf8[8];
    char serviceForm[2048];
    char renderedNpcFields[32768];
    char renderedPortalFields[16384];
    char renderedMonsterDropBatch[24576];
    char renderedAdminLogin[16384];
    char renderedDesignations[65536];
    char renderedRoleOperations[16384];
    char adminCookieRequest[256];
    char endpointHost[64];
    char resolvedLoginSource[VM_MOCK_SERVICE_LOGIN_IP_CAP];
    const char *callbackPayload = NULL;
    const char *callbackSource = NULL;
    vm_mock_payment_callback paymentCallback;
    u16 endpointPort = 0;
    size_t serviceFormLen = 0;
    char operationFilterSql[768];
    char operationFilterQuery[512];
    u32 operationFilter = 0;
    vm_mock_admin_text renderedPage;
    vm_mock_admin_text renderedPortalPage;
    vm_mock_admin_text renderedMonsterDropBatchPage;
    vm_mock_admin_text renderedRoleOperationsPage;
    vm_mock_admin_operation_log_page operatorLogPage;
    vm_mock_admin_scene_file sceneFixtures[2];
    vm_mock_admin_scene_portal portalFixture;
    char rewardRecipients[VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX]
                         [VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP];
    vm_net_mock_npc_service_option
        serviceOptions[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
    vm_net_mock_monster_admin_row dropFilterFixture;
    vm_net_mock_monster_admin_row dropBatchFixtures[3];
    vm_net_mock_monster_drop_batch_result dropBatchResult;
    bool dropBatchChanged[3];
    const char *dropBatchError = NULL;
    const char *rewardRecipientError = NULL;
    const char *operatorLogValues[] = {
        "7", "operator.alice", "add-wcoin", "audit-target", "0", "0",
        "0", "100", "E5A29EE58AA02057E5B88120313030",
        "2026-08-24 12:34:56.789"
    };
    size_t operatorLogLengths[10];
    vm_mock_admin_session *firstAdminSession = NULL;
    vm_mock_admin_session *secondAdminSession = NULL;
    vm_mock_service_login_ip_block_cache *loginIpBlockCache =
        &g_vmMockServiceLoginIpBlockCache;
    const vm_net_mock_designation_entry *baseDesignation = NULL;
    const vm_net_mock_designation_entry *specialDesignation = NULL;
    vm_net_mock_designation_config *baseDesignationConfig = NULL;
    vm_net_mock_designation_config *specialDesignationConfig = NULL;
    vm_net_mock_designation_admin_row designationDirectoryFixture;
    vm_net_mock_role_state roleOperationFixture;
    u32 dropBatchItemId = 0;
    u32 rewardRecipientCount = 0;
    u32 serviceOptionCount = 0;

    if (!vm_mock_admin_open_loopback_pair(&proxyServer, &proxyClient) ||
        !vm_mock_admin_dispatch_request(proxyServer, proxyRequest,
                                        sizeof(proxyRequest) - 1u, 0))
    {
        fprintf(stderr, "proxy Host/Origin request was rejected\n");
        vm_mock_service_socket_close(proxyServer);
        vm_mock_service_socket_close(proxyClient);
        return 1;
    }

    if (!vm_mock_admin_account_id_is_valid("operator.alice") ||
        !vm_mock_admin_account_id_is_valid("ops-02@example") ||
        vm_mock_admin_account_id_is_valid("") ||
        vm_mock_admin_account_id_is_valid("operator alice") ||
        vm_mock_admin_account_id_is_valid("operator/../../root"))
    {
        fprintf(stderr, "admin operator account validation is incomplete\n");
        return 1;
    }
    memset(&roleOperationFixture, 0, sizeof(roleOperationFixture));
    roleOperationFixture.roleId = 37;
    roleOperationFixture.level = 88;
    roleOperationFixture.equippedItems[0].itemId = 1001;
    roleOperationFixture.equippedItems[0].enhanceLevel = 9;
    roleOperationFixture.equippedItems[0].durability = 48;
    roleOperationFixture.equippedItems[0].durabilityMax = 50;
    memset(renderedRoleOperations, 0, sizeof(renderedRoleOperations));
    vm_mock_admin_text_init(&renderedRoleOperationsPage,
                            renderedRoleOperations,
                            sizeof(renderedRoleOperations));
    vm_mock_admin_render_role_operation_modal(
        &renderedRoleOperationsPage, "role.ops", &roleOperationFixture,
        "操作测试角色");
    if (renderedRoleOperationsPage.truncated ||
        strstr(renderedRoleOperations,
               "data-role-operation-tab=\"profile\"") == NULL ||
        strstr(renderedRoleOperations,
               "data-role-operation-pane=\"items\"") == NULL ||
        strstr(renderedRoleOperations,
               "data-role-operation-pane=\"equipment\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"action\" value=\"set-role-name\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"action\" value=\"set-role-level\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"action\" value=\"add-money\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"action\" value=\"grant-item\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"action\" value=\"reset-role-selected-scene\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"action\" value=\"set-equipped-enhance-level\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"equipment_slot\" value=\"0\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"enhance_level\" min=\"0\" max=\"16\" value=\"9\"") == NULL ||
        strstr(renderedRoleOperations,
               "name=\"role\" value=\"37\"") == NULL)
    {
        fprintf(stderr,
                "role operation modal does not preserve every role action\n");
        return 1;
    }
    if (!vm_mock_service_login_ip_is_valid("203.0.113.7") ||
        !vm_mock_service_login_ip_is_valid("255.255.255.255") ||
        vm_mock_service_login_ip_is_valid("203.0.113.256") ||
        vm_mock_service_login_ip_is_valid("203.0.113") ||
        VM_MOCK_SERVICE_LOGIN_IP_FAILURE_LIMIT != 15)
    {
        fprintf(stderr, "login IP validation or failure limit is invalid\n");
        return 1;
    }
    memset(resolvedLoginSource, 0, sizeof(resolvedLoginSource));
    if (!vm_mock_admin_resolve_login_source_from_trusted_headers(
            "127.0.0.1", trustedProxyLoginRequest,
            sizeof(trustedProxyLoginRequest) - 1u, true, false,
            resolvedLoginSource, sizeof(resolvedLoginSource)) ||
        strcmp(resolvedLoginSource, "203.0.113.7") != 0)
    {
        fprintf(stderr, "trusted proxy real IP was not selected\n");
        return 1;
    }
    memset(resolvedLoginSource, 0, sizeof(resolvedLoginSource));
    if (vm_mock_admin_resolve_login_source_from_trusted_headers(
            "198.51.100.9", untrustedProxyLoginRequest,
            sizeof(untrustedProxyLoginRequest) - 1u, false, false,
            resolvedLoginSource, sizeof(resolvedLoginSource)) ||
        strcmp(resolvedLoginSource, "198.51.100.9") != 0)
    {
        fprintf(stderr, "untrusted peer spoofed the real IP header\n");
        return 1;
    }
    memset(&paymentCallback, 0, sizeof(paymentCallback));
    if (!vm_mock_payment_parse_callback(paymentCallbackForm,
                                        &paymentCallback) ||
        paymentCallback.payType != 1 || paymentCallback.priceCents != 100 ||
        paymentCallback.reallyPriceCents != 100 ||
        !vm_mock_admin_payment_callback_payload_for_request(
            "POST", false, "", paymentCallbackForm, &callbackPayload,
            &callbackSource) ||
        callbackPayload == NULL ||
        strcmp(callbackPayload, paymentCallbackForm) != 0 ||
        callbackSource == NULL || strcmp(callbackSource, "notify-post") != 0 ||
        vm_mock_admin_payment_callback_payload_for_request(
            "POST", true, "", paymentCallbackForm, &callbackPayload,
            &callbackSource))
    {
        fprintf(stderr, "async payment POST callback contract is invalid\n");
        return 1;
    }
    memset(resolvedLoginSource, 0, sizeof(resolvedLoginSource));
    if (!vm_mock_admin_resolve_login_source_from_trusted_headers(
            "10.20.30.40", forwardedForLoginRequest,
            sizeof(forwardedForLoginRequest) - 1u, false, true,
            resolvedLoginSource, sizeof(resolvedLoginSource)) ||
        strcmp(resolvedLoginSource, "203.0.113.8") != 0)
    {
        fprintf(stderr, "trusted proxy forwarded-for IP was not selected\n");
        return 1;
    }
    memset(resolvedLoginSource, 0, sizeof(resolvedLoginSource));
    if (vm_mock_admin_resolve_login_source_from_trusted_headers(
            "10.20.30.40", forwardedForChainRequest,
            sizeof(forwardedForChainRequest) - 1u, false, true,
            resolvedLoginSource, sizeof(resolvedLoginSource)) ||
        strcmp(resolvedLoginSource, "10.20.30.40") != 0)
    {
        fprintf(stderr, "forwarded-for chain was accepted as a source IP\n");
        return 1;
    }
    vm_mock_service_login_ip_set_source("203.0.113.7");
    if (strcmp(vm_mock_service_login_ip_current_source(), "203.0.113.7") != 0)
    {
        fprintf(stderr, "login IP worker source is not retained\n");
        return 1;
    }
    vm_mock_service_login_ip_set_source(NULL);
    if (vm_mock_service_login_ip_current_source()[0] != 0)
    {
        fprintf(stderr, "login IP worker source is not cleared\n");
        return 1;
    }
    pthread_mutex_lock(&loginIpBlockCache->mutex);
    loginIpBlockCache->loaded = true;
    loginIpBlockCache->available = true;
    loginIpBlockCache->overflow = false;
    loginIpBlockCache->count = 0;
    vm_mock_service_login_ip_block_cache_add(loginIpBlockCache, "203.0.113.7");
    vm_mock_service_login_ip_block_cache_add(loginIpBlockCache, "203.0.113.7");
    if (loginIpBlockCache->count != 1 ||
        !vm_mock_service_login_ip_block_cache_contains(loginIpBlockCache,
                                                        "203.0.113.7"))
    {
        pthread_mutex_unlock(&loginIpBlockCache->mutex);
        fprintf(stderr, "login IP block cache does not deduplicate entries\n");
        return 1;
    }
    pthread_mutex_unlock(&loginIpBlockCache->mutex);
    if (!vm_mock_service_login_ip_is_blocked("203.0.113.7") ||
        vm_mock_service_login_ip_is_blocked("203.0.113.8"))
    {
        fprintf(stderr, "login IP block cache does not enforce silent-close gate\n");
        return 1;
    }
    pthread_mutex_lock(&loginIpBlockCache->mutex);
    loginIpBlockCache->loaded = false;
    loginIpBlockCache->available = false;
    loginIpBlockCache->overflow = false;
    loginIpBlockCache->count = 0;
    pthread_mutex_unlock(&loginIpBlockCache->mutex);
    if (!vm_mock_admin_global_reward_parse_recipients(
            "alpha_1, beta_2\ngamma_3 alpha_1", rewardRecipients,
            VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX,
            &rewardRecipientCount, &rewardRecipientError) ||
        rewardRecipientError != NULL || rewardRecipientCount != 3 ||
        strcmp(rewardRecipients[0], "alpha_1") != 0 ||
        strcmp(rewardRecipients[1], "beta_2") != 0 ||
        strcmp(rewardRecipients[2], "gamma_3") != 0 ||
        !vm_mock_admin_global_reward_parse_recipients(
            "", rewardRecipients,
            VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX,
            &rewardRecipientCount, &rewardRecipientError) ||
        rewardRecipientError != NULL || rewardRecipientCount != 0 ||
        vm_mock_admin_global_reward_parse_recipients(
            "bad!account", rewardRecipients,
            VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX,
            &rewardRecipientCount, &rewardRecipientError) ||
        rewardRecipientError == NULL ||
        !vm_mock_admin_global_reward_form_has_field(
            "title=test&recipient_accounts=alpha_1", "recipient_accounts") ||
        vm_mock_admin_global_reward_form_has_field(
            "title=test", "recipient_accounts"))
    {
        fprintf(stderr, "global reward recipient parsing is incomplete\n");
        return 1;
    }
    vm_net_mock_designation_config_reset_to_defaults();
    baseDesignation = vm_net_mock_designation_by_id(0);
    specialDesignation = vm_net_mock_designation_by_id(32);
    baseDesignationConfig = vm_net_mock_designation_config_by_id(0);
    specialDesignationConfig = vm_net_mock_designation_config_by_id(32);
    if (baseDesignation == NULL || specialDesignation == NULL ||
        baseDesignationConfig == NULL || specialDesignationConfig == NULL ||
        strcmp(specialDesignation->name,
               "\xD7\xCA\xC9\xEE\xC0\xCF\xD3\xD1") != 0 ||
        specialDesignation->overheadResource[0] != 0 ||
        !baseDesignationConfig->enabled ||
        baseDesignationConfig->conditionKind !=
            VM_NET_MOCK_DESIGNATION_CONDITION_MONEY ||
        baseDesignationConfig->conditionValue != 0 ||
        specialDesignationConfig->enabled ||
        specialDesignationConfig->conditionKind !=
            VM_NET_MOCK_DESIGNATION_CONDITION_LEVEL ||
        specialDesignationConfig->conditionValue != 1)
    {
        fprintf(stderr, "designation defaults or special-title resource policy failed\n");
        return 1;
    }
    memset(&designationDirectoryFixture, 0,
           sizeof(designationDirectoryFixture));
    designationDirectoryFixture.designationId = 0;
    if (strcmp(vm_mock_admin_designation_directory(
                   &designationDirectoryFixture),
               "money") != 0 ||
        strcmp(vm_mock_admin_designation_directory_label("money"),
               "金钱称号") != 0)
    {
        fprintf(stderr, "wealth designation directory is missing\n");
        return 1;
    }
    designationDirectoryFixture.designationId = 16;
    if (strcmp(vm_mock_admin_designation_directory(
                   &designationDirectoryFixture),
               "level") != 0 ||
        strcmp(vm_mock_admin_designation_directory_label("level"),
               "等级称号") != 0)
    {
        fprintf(stderr, "level designation directory is missing\n");
        return 1;
    }
    designationDirectoryFixture.special = true;
    if (strcmp(vm_mock_admin_designation_directory(
                   &designationDirectoryFixture),
               "special") != 0 ||
        strcmp(vm_mock_admin_designation_directory_label("special"),
               "特殊称号") != 0)
    {
        fprintf(stderr, "special designation directory is missing\n");
        return 1;
    }
    /* The title page's data source is normally MySQL-backed.  Keep the page
     * rendering assertion local and deterministic by using the already reset
     * in-memory defaults as a loaded fixture. */
    g_vm_net_mock_designation_config_db_loaded = true;
    g_vm_net_mock_designation_config_db_valid = true;
    memset(renderedDesignations, 0, sizeof(renderedDesignations));
    vm_mock_admin_render_designations_page(renderedDesignations,
                                           sizeof(renderedDesignations), "");
    if (strstr(renderedDesignations,
               "data-designation-filter=\"money\"") == NULL ||
        strstr(renderedDesignations,
               "data-designation-filter=\"level\"") == NULL ||
        strstr(renderedDesignations,
               "data-designation-filter=\"special\"") == NULL ||
        strstr(renderedDesignations,
               "data-designation-category=\"money\"") == NULL ||
        strstr(renderedDesignations,
               "data-designation-category=\"level\"") == NULL ||
        strstr(renderedDesignations,
               "data-designation-category=\"special\"") == NULL ||
        strstr(renderedDesignations,
               "src=\"/gif-preview.bmp?gif=riches_name0.gif\"") == NULL ||
        strstr(renderedDesignations, "暂无专属徽章预览") == NULL ||
        strstr(renderedDesignations, "客户端徽章资源") != NULL)
    {
        fprintf(stderr, "designation directory or badge preview rendering failed\n");
        return 1;
    }
    vm_mock_admin_operation_audit_begin("operator.alice", "save-npc",
                                        "admin-config");
    if (!g_vm_mock_admin_operation_audit_context.active ||
        strcmp(g_vm_mock_admin_operation_audit_context.operatorAccountId,
               "operator.alice") != 0 ||
        strcmp(g_vm_mock_admin_operation_audit_context.actionCode,
               "save-npc") != 0 ||
        strcmp(g_vm_mock_admin_operation_audit_context.targetAccountId,
               "admin-config") != 0 ||
        !vm_mock_admin_operation_audit_location_is_success(
            "/admin-418yz6/?tab=content&status=ok") ||
        vm_mock_admin_operation_audit_location_is_success(
            "/admin-418yz6/?tab=content&status=error"))
    {
        fprintf(stderr, "admin edit audit context does not preserve operator or outcome\n");
        return 1;
    }
    vm_mock_admin_operation_audit_clear();
    memset(g_vm_mock_admin_sessions, 0, sizeof(g_vm_mock_admin_sessions));
    firstAdminSession = vm_mock_admin_issue_session("operator.alice");
    secondAdminSession = vm_mock_admin_issue_session("operator.bob");
    if (firstAdminSession == NULL || secondAdminSession == NULL ||
        strcmp(firstAdminSession->token, secondAdminSession->token) == 0)
    {
        fprintf(stderr, "admin operator sessions are not independently issued\n");
        return 1;
    }
    snprintf(adminCookieRequest, sizeof(adminCookieRequest),
             "GET /admin-418yz6/ HTTP/1.1\r\nCookie: cbe_admin=%s\r\n\r\n",
             firstAdminSession->token);
    if (vm_mock_admin_request_session(adminCookieRequest,
                                      strlen(adminCookieRequest)) !=
        firstAdminSession)
    {
        fprintf(stderr, "admin session did not retain its operator identity\n");
        return 1;
    }
    vm_mock_admin_clear_request_session(adminCookieRequest,
                                        strlen(adminCookieRequest));
    if (firstAdminSession->active || !secondAdminSession->active)
    {
        fprintf(stderr, "admin logout cleared another operator session\n");
        return 1;
    }
    memset(renderedAdminLogin, 0, sizeof(renderedAdminLogin));
    vm_mock_admin_render_login(renderedAdminLogin, sizeof(renderedAdminLogin),
                               NULL);
    if (strstr(renderedAdminLogin, "name=\"account\"") == NULL ||
        strstr(renderedAdminLogin, "data-admin-login-account") == NULL ||
        strstr(renderedAdminLogin, "记住账号和密码") == NULL ||
        strstr(g_vm_mock_admin_login_script,
               "cbe-admin-login-password-v1") == NULL ||
        strstr(g_vm_mock_admin_login_script,
               "cbe-admin-login-account-v1") == NULL ||
        strstr(g_vm_mock_admin_login_script,
               "localStorage.removeItem(passwordKey)") == NULL)
    {
        fprintf(stderr, "admin login does not preserve opt-in local credentials\n");
        return 1;
    }
    memset(&operatorLogPage, 0, sizeof(operatorLogPage));
    for (u32 i = 0; i < 10; ++i)
        operatorLogLengths[i] = strlen(operatorLogValues[i]);
    if (!vm_mock_admin_operation_log_row_callback(
            &operatorLogPage, 10, operatorLogValues, operatorLogLengths) ||
        operatorLogPage.invalid || operatorLogPage.count != 1 ||
        strcmp(operatorLogPage.rows[0].operatorAccountId,
               "operator.alice") != 0 ||
        strcmp(operatorLogPage.rows[0].accountId, "audit-target") != 0 ||
        strcmp(operatorLogPage.rows[0].detail, "增加 W币 100") != 0)
    {
        fprintf(stderr, "operation-log operator identity row decoding failed\n");
        return 1;
    }
    proxyResponseLen = recv(proxyClient, proxyResponse,
                            (int)sizeof(proxyResponse) - 1, 0);
    vm_mock_service_socket_close(proxyServer);
    vm_mock_service_socket_close(proxyClient);
    if (proxyResponseLen <= 0)
    {
        fprintf(stderr, "proxy Host/Origin request returned no response\n");
        return 1;
    }
    proxyResponse[proxyResponseLen] = 0;
    if (strstr(proxyResponse, "HTTP/1.1 200 OK\r\n") == NULL)
    {
        fprintf(stderr, "proxy Host/Origin request did not reach healthz\n");
        return 1;
    }

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
    if (strstr(g_vm_mock_admin_script,
               "const setupMonsterDropBatchModal") == NULL ||
        strstr(g_vm_mock_admin_script, "data-monster-drop-batch-open") == NULL ||
        strstr(g_vm_mock_admin_script,
               "data-monster-drop-batch-mode") == NULL ||
        strstr(g_vm_mock_admin_script,
               "data-monster-drop-batch-ids") == NULL ||
        strstr(g_vm_mock_admin_script, "setupMonsterDropBatchModal();") == NULL)
    {
        fprintf(stderr,
                "monster drop batch modal script setup is missing\n");
        return 1;
    }
    vm_mock_admin_text_init(&renderedMonsterDropBatchPage,
                            renderedMonsterDropBatch,
                            sizeof(renderedMonsterDropBatch));
    vm_mock_admin_render_monster_drop_batch_modal(
        &renderedMonsterDropBatchPage, 13);
    if (renderedMonsterDropBatchPage.truncated ||
        strstr(renderedMonsterDropBatch,
               "batch-configure-monster-drops") == NULL ||
        strstr(renderedMonsterDropBatch,
               "data-monster-drop-batch-family") == NULL ||
        strstr(renderedMonsterDropBatch,
               "data-monster-drop-batch-mode") == NULL ||
        strstr(renderedMonsterDropBatch,
               "name=\"drop_batch_level_min\"") == NULL ||
        strstr(renderedMonsterDropBatch,
               "name=\"drop_batch_level_max\"") == NULL ||
        strstr(renderedMonsterDropBatch,
               "name=\"drop_batch_item_ids\"") == NULL)
    {
        fprintf(stderr,
                "monster drop batch modal configuration markup is incomplete\n");
        return 1;
    }
    memset(&dropFilterFixture, 0, sizeof(dropFilterFixture));
    dropFilterFixture.level = 40;
    dropFilterFixture.family = VM_NET_MOCK_MONSTER_BOSS;
    if (!vm_net_mock_monster_drop_batch_matches(
            &dropFilterFixture, VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_ALL,
            1, 255) ||
        !vm_net_mock_monster_drop_batch_matches(
            &dropFilterFixture, VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_BOSS,
            40, 40) ||
        vm_net_mock_monster_drop_batch_matches(
            &dropFilterFixture,
            VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_NON_BOSS, 1, 255))
    {
        fprintf(stderr, "boss and level drop-batch filtering failed\n");
        return 1;
    }
    dropFilterFixture.level = 12;
    dropFilterFixture.family = VM_NET_MOCK_MONSTER_BEAST;
    if (!vm_net_mock_monster_drop_batch_matches(
            &dropFilterFixture,
            VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_NON_BOSS, 10, 15) ||
        vm_net_mock_monster_drop_batch_matches(
            &dropFilterFixture, VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_BOSS,
            1, 255) ||
        vm_net_mock_monster_drop_batch_matches(
            &dropFilterFixture,
            VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_NON_BOSS, 13, 20))
    {
        fprintf(stderr, "non-boss or range drop-batch filtering failed\n");
        return 1;
    }
    if (vm_net_mock_load_shop_catalog() == 0 ||
        g_vm_net_mock_shop_catalog[0].itemId == 0)
    {
        fprintf(stderr, "shop catalog fixture is unavailable for drop-batch planning\n");
        return 1;
    }
    dropBatchItemId = g_vm_net_mock_shop_catalog[0].itemId;
    memset(dropBatchFixtures, 0, sizeof(dropBatchFixtures));
    memset(dropBatchChanged, 0, sizeof(dropBatchChanged));
    memset(&dropBatchResult, 0, sizeof(dropBatchResult));
    dropBatchFixtures[0].enemyId = 1;
    dropBatchFixtures[0].level = 40;
    dropBatchFixtures[0].family = VM_NET_MOCK_MONSTER_BOSS;
    dropBatchFixtures[1].enemyId = 2;
    dropBatchFixtures[1].level = 40;
    dropBatchFixtures[1].family = VM_NET_MOCK_MONSTER_BEAST;
    dropBatchFixtures[2].enemyId = 3;
    dropBatchFixtures[2].level = 40;
    dropBatchFixtures[2].family = VM_NET_MOCK_MONSTER_BOSS;
    dropBatchFixtures[2].smartDropExcluded = true;
    if (!vm_net_mock_monster_admin_plan_drop_batch(
            dropBatchFixtures, 3, &dropBatchItemId, 1,
            VM_NET_MOCK_MONSTER_DROP_BATCH_ADD,
            VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_BOSS, 35, 45, 250,
            dropBatchChanged, &dropBatchResult, &dropBatchError) ||
        dropBatchResult.matchedMonsters != 1 ||
        dropBatchResult.changedMonsters != 1 ||
        dropBatchResult.addedDrops != 1 ||
        dropBatchResult.sceneBattleExcluded != 1 ||
        !dropBatchChanged[0] || dropBatchChanged[1] || dropBatchChanged[2] ||
        dropBatchFixtures[0].dropCount != 1 ||
        dropBatchFixtures[0].drops[0].itemId != dropBatchItemId ||
        dropBatchFixtures[0].drops[0].rateBasisPoints != 250)
    {
        fprintf(stderr, "boss-range batch drop addition planning failed: %s\n",
                dropBatchError ? dropBatchError : "unknown");
        return 1;
    }
    memset(dropBatchChanged, 0, sizeof(dropBatchChanged));
    memset(&dropBatchResult, 0, sizeof(dropBatchResult));
    if (!vm_net_mock_monster_admin_plan_drop_batch(
            dropBatchFixtures, 3, &dropBatchItemId, 1,
            VM_NET_MOCK_MONSTER_DROP_BATCH_REMOVE,
            VM_NET_MOCK_MONSTER_DROP_BATCH_FAMILY_BOSS, 35, 45, 0,
            dropBatchChanged, &dropBatchResult, &dropBatchError) ||
        dropBatchResult.changedMonsters != 1 ||
        dropBatchResult.removedDrops != 1 || !dropBatchChanged[0] ||
        dropBatchFixtures[0].dropCount != 0)
    {
        fprintf(stderr, "boss-range batch drop removal planning failed: %s\n",
                dropBatchError ? dropBatchError : "unknown");
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
        strstr(g_vm_mock_admin_script,
               "data-admin-persistent-notice") == NULL ||
        strstr(g_vm_mock_admin_script, "MutationObserver") == NULL ||
        strstr(g_vm_mock_admin_script, "setTimeout(dismiss,5000)") == NULL)
    {
        fprintf(stderr,
                "admin toasts do not preserve persistent page controls\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script,
               "const setupRoleOperationModal") == NULL ||
        strstr(g_vm_mock_admin_script, "data-role-operation-open") == NULL ||
        strstr(g_vm_mock_admin_script, "data-role-operation-tab") == NULL ||
        strstr(g_vm_mock_admin_script, "data-admin-confirm") == NULL ||
        strstr(g_vm_mock_admin_script, "setupRoleOperationModal();") == NULL)
    {
        fprintf(stderr,
                "role operation modal is not owned by the shared admin script\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "const setupGlobalRewards") == NULL ||
        strstr(g_vm_mock_admin_script, "const setupDesignationTab") == NULL ||
        strstr(g_vm_mock_admin_script,
               "const setupDesignationDirectory") == NULL ||
        strstr(g_vm_mock_admin_script,
               "data-designation-filter") == NULL ||
        strstr(g_vm_mock_admin_script,
               "data-designation-category") == NULL ||
        strstr(g_vm_mock_admin_script,
               "setupDesignationDirectory();") == NULL ||
        strstr(g_vm_mock_admin_script, "tab=designations") == NULL ||
        strstr(g_vm_mock_admin_script, "data-global-reward-form") == NULL ||
        strstr(g_vm_mock_admin_script, "data-global-reward-add") == NULL ||
        strstr(g_vm_mock_admin_script, "row.hidden=false") == NULL ||
        strstr(g_vm_mock_admin_script, "setupGlobalRewards();") == NULL)
    {
        fprintf(stderr,
                "global-reward attachment controls are not owned by the shared admin script\n");
        return 1;
    }
    if (strstr(g_vm_mock_admin_script, "操作日志") == NULL ||
        strcmp(vm_mock_admin_operation_log_action_label("admin-edit"),
               "后台配置编辑") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("set-role-level"),
               "设置角色等级") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label(
                   "set-equipped-enhance-level"),
               "设置穿戴装备强化等级") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("add-money"),
               "增加普通钱币") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("grant-item"),
               "发放物品/装备") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("player-trade"),
               "玩家交易") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("discard-equipment"),
               "丢弃装备") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("recycle-equipment"),
               "装备回收") != 0)
    {
        fprintf(stderr,
                "admin operation log navigation or action labels are missing\n");
        return 1;
    }
    operationFilter = vm_mock_admin_operation_log_action_filter_from_query(
        "type=add-money&type=player-trade&type=discard-equipment"
        "&type=recycle-equipment&type=spend-wcoin-shop&type=spend-wcoin-instance");
    if (operationFilter == 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("player-trade"),
               "玩家交易") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("discard-equipment"),
               "丢弃装备") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("recycle-equipment"),
               "装备回收") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("spend-wcoin-shop"),
               "游戏内商城消费 W 币") != 0 ||
        strcmp(vm_mock_admin_operation_log_action_label("spend-wcoin-instance"),
               "付费副本消费 W 币") != 0 ||
        !vm_mock_admin_operation_log_build_filter("audit-target",
                                                  operationFilter,
                                                  operationFilterSql,
                                                  sizeof(operationFilterSql)) ||
        strstr(operationFilterSql, "action_code='add-money'") == NULL ||
        strstr(operationFilterSql, "action_code='player-trade'") == NULL ||
        strstr(operationFilterSql, "action_code='discard-equipment'") == NULL ||
        strstr(operationFilterSql, "action_code='recycle-equipment'") == NULL ||
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
        strstr(operationFilterQuery, "&amp;type=player-trade") == NULL ||
        strstr(operationFilterQuery, "&amp;type=discard-equipment") == NULL ||
        strstr(operationFilterQuery, "&amp;type=recycle-equipment") == NULL ||
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
    puts("admin request-length regression passed: service endpoint parsing + queued admin notifications + multi-type account operation logs + player trade/discard/recovery and W-coin labels + empty display + NPC service toggle/configuration + searchable SCE/GIF catalog + 24KiB body + monster search + bulk drops");
    return 0;
}
