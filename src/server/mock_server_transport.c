static u32 vm_net_mock_sync_response_to_vm(void)
{
    if (g_netMockResponseLen == 0)
        return 0;

    /*
     * Network data events keep the VM response pointer in the queued task.
     * Do not free the previous pointer here: a burst of sends can queue
     * response A, build response B before A is dispatched, and leave A's
     * callback reading freed/reused memory.
     */
    g_netMockResponseVmPtr = vm_malloc(g_netMockResponseLen);
    if (g_netMockResponseVmPtr == 0)
        return 0;
    uc_mem_write(MTK, g_netMockResponseVmPtr, g_netMockResponse, g_netMockResponseLen);
    return g_netMockResponseVmPtr;
}

static bool vm_net_mock_extract_item_use_backpack_followup(u8 *response,
                                                           u32 *responseLen,
                                                           u8 *followOut,
                                                           u32 followCap,
                                                           u32 *followLenOut)
{
    u32 offset = 4;
    u32 primaryPos = 5;
    u32 followPos = 5;
    u8 primaryCount = 0;
    u8 followCount = 0;
    bool haveItemUse = false;
    vm_net_mock_request_object object;

    if (followLenOut)
        *followLenOut = 0;
    if (response == NULL || responseLen == NULL || *responseLen < 10 ||
        response[0] != 'W' || response[1] != 'T' || followOut == NULL || followCap < 5)
    {
        return false;
    }

    while (offset + 5 <= *responseLen && vm_net_mock_next_request_object(response, *responseLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 7 && object.subtype == 1)
            haveItemUse = true;
        if (object.major == 1 && object.kind == 17 && object.subtype == 1)
            ++followCount;
        else
            ++primaryCount;
    }
    if (offset != *responseLen || !haveItemUse || followCount == 0 || primaryCount == 0)
        return false;

    offset = 4;
    primaryPos = 5;
    followPos = 5;
    primaryCount = 0;
    followCount = 0;
    while (offset + 5 <= *responseLen)
    {
        u32 objectStart = offset;
        u32 objectLen = 0;
        if (!vm_net_mock_next_request_object(response, *responseLen, &offset, &object))
            return false;
        objectLen = offset - objectStart;
        if (object.major == 1 && object.kind == 17 && object.subtype == 1)
        {
            if (followPos + objectLen > followCap || followCount == 0xff)
                return false;
            memcpy(followOut + followPos, response + objectStart, objectLen);
            followPos += objectLen;
            ++followCount;
            continue;
        }
        memmove(response + primaryPos, response + objectStart, objectLen);
        primaryPos += objectLen;
        ++primaryCount;
    }

    vm_net_mock_finish_wt_packet(response, primaryPos, primaryCount);
    vm_net_mock_finish_wt_packet(followOut, followPos, followCount);
    *responseLen = primaryPos;
    if (followLenOut)
        *followLenOut = followPos;
    return true;
}

static bool vm_net_mock_is_actor_moveinfo_timeline(const u8 *moveInfo, u16 moveInfoLen)
{
    bool hasDirection = false;

    if (moveInfo == NULL || moveInfoLen == 0 || moveInfoLen > 10)
        return false;
    for (u16 i = 0; i < moveInfoLen; ++i)
    {
        if (moveInfo[i] > 4)
            return false;
        if (moveInfo[i] != 0)
            hasDirection = true;
    }
    return hasDirection;
}

static void vm_net_mock_apply_actor_moveinfo_timeline(u16 *x, u16 *y,
                                                      const u8 *moveInfo, u16 moveInfoLen)
{
    /*
     * Client evidence:
     *   - scene_runtime_tick(0x01014EE0) uploads up to ten raw move bytes in
     *     WT 2/1 field "moveinfo".
     *   - ProcessSceneAutoAction(0x01045428) consumes node+136[node+317] until
     *     node+317 == node+318, advancing exactly one 4-pixel step per byte:
     *       1 = up, 2 = right, 3 = down, 4 = left.
     */
    u16 curX = x ? *x : 0;
    u16 curY = y ? *y : 0;

    if (curX == 0 || curY == 0 || moveInfo == NULL)
        return;
    for (u16 i = 0; i < moveInfoLen; ++i)
    {
        switch (moveInfo[i])
        {
        case 1:
            curY = curY > 4 ? (u16)(curY - 4) : 1;
            break;
        case 2:
            curX = (u16)(curX + 4);
            break;
        case 3:
            curY = (u16)(curY + 4);
            break;
        case 4:
            curX = curX > 4 ? (u16)(curX - 4) : 1;
            break;
        default:
            break;
        }
    }
    if (x)
        *x = curX;
    if (y)
        *y = curY;
}

static void vm_net_mock_format_moveinfo_timeline(const u8 *moveInfo, u16 moveInfoLen,
                                                 char *out, u32 outCap)
{
    u32 pos = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (moveInfo == NULL || moveInfoLen == 0)
        return;
    for (u16 i = 0; i < moveInfoLen && pos + 2 < outCap; ++i)
    {
        out[pos++] = (char)('0' + moveInfo[i]);
        if (i + 1 < moveInfoLen && pos + 1 < outCap)
            out[pos++] = ',';
    }
    out[pos] = 0;
}

static u32 vm_net_mock_sync_buffer_to_vm(const u8 *buffer, u32 bufferLen)
{
    u32 responsePtr = 0;
    if (buffer == NULL || bufferLen == 0)
        return 0;
    responsePtr = vm_malloc(bufferLen);
    if (responsePtr == 0)
        return 0;
    uc_mem_write(MTK, responsePtr, buffer, bufferLen);
    return responsePtr;
}

static void vm_mock_service_write_le32(u8 *dst, u32 value)
{
    dst[0] = (u8)(value & 0xff);
    dst[1] = (u8)((value >> 8) & 0xff);
    dst[2] = (u8)((value >> 16) & 0xff);
    dst[3] = (u8)((value >> 24) & 0xff);
}

static u32 vm_mock_service_read_le32(const u8 *src)
{
    return (u32)src[0] |
           ((u32)src[1] << 8) |
           ((u32)src[2] << 16) |
           ((u32)src[3] << 24);
}

static void vm_mock_service_encode_header(u8 *header, const char magic[4], u32 flags, u32 bodyLen, u32 aux)
{
    memcpy(header, magic, 4);
    vm_mock_service_write_le32(header + 4, 1);
    vm_mock_service_write_le32(header + 8, flags);
    vm_mock_service_write_le32(header + 12, bodyLen);
    vm_mock_service_write_le32(header + 16, aux);
}

static int vm_mock_service_socket_init(void)
{
#ifdef _WIN32
    static int s_ready = -1;
    if (s_ready >= 0)
        return s_ready;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        s_ready = 0;
        return 0;
    }
    s_ready = 1;
#endif
    return 1;
}

static void vm_mock_service_socket_close(vm_mock_service_socket sock)
{
    if (sock == VM_MOCK_SERVICE_INVALID_SOCKET)
        return;
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

static int vm_mock_service_send_all(vm_mock_service_socket sock, const u8 *data, u32 len)
{
    u32 sent = 0;
    while (sent < len)
    {
        int rc = send(sock, (const char *)(data + sent), (int)(len - sent), 0);
        if (rc <= 0)
            return 0;
        sent += (u32)rc;
    }
    return 1;
}

static int vm_mock_service_recv_all(vm_mock_service_socket sock, u8 *data, u32 len)
{
    u32 got = 0;
    while (got < len)
    {
        int rc = recv(sock, (char *)(data + got), (int)(len - got), 0);
        if (rc <= 0)
            return 0;
        got += (u32)rc;
    }
    return 1;
}

static void vm_mock_service_copy_account_id(char *dst, size_t dstCap, const char *src)
{
    if (dst == NULL || dstCap == 0)
        return;
    dst[0] = 0;
    if (src == NULL || src[0] == 0)
        return;
    snprintf(dst, dstCap, "%s", src);
}

typedef struct
{
    u32 clientId;
} vm_mock_service_request_meta;

static bool vm_mock_service_parse_login_request(const u8 *request,
                                                u32 requestLen,
                                                vm_mock_service_login_request *login)
{
    if (login == NULL)
        return false;
    memset(login, 0, sizeof(*login));
    if (!vm_net_mock_is_login_validation_request(request, requestLen, &login->requestSubtype))
        return false;
    login->haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName",
                                                              login->userName, sizeof(login->userName));
    if (!login->haveUserName)
        login->haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username",
                                                                  login->userName, sizeof(login->userName));
    login->havePassword = vm_net_mock_get_object_string_field(request, requestLen, "password",
                                                              login->password, sizeof(login->password));
    return true;
}

static bool vm_mock_service_login_is_no_account(const vm_mock_service_login_request *login)
{
    return login != NULL &&
           login->requestSubtype == 12 &&
           login->haveUserName && login->havePassword &&
           login->userName[0] == 0 && login->password[0] == 0;
}

static bool vm_mock_service_request_is_stateless_prelogin(const u8 *request,
                                                          u32 requestLen)
{
    u8 wtKind = 0;
    u8 wtSubtype = 0;

    if (request == NULL || requestLen == 0)
        return false;
    if (vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &wtKind, &wtSubtype))
    {
        if (wtKind == 18)
            return true;
        if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x63, 1))
            return true;
    }
    return vm_net_mock_request_contains(request, requestLen, "version") != 0;
}

static bool vm_mock_service_login_requires_auth(const vm_mock_service_login_request *login)
{
    if (login == NULL)
        return false;
    if (vm_mock_service_login_is_no_account(login))
        return false;
    return login->haveUserName || login->havePassword ||
           login->userName[0] != 0 || login->password[0] != 0;
}

static bool vm_mock_service_authenticate_login_request(const vm_mock_service_login_request *login,
                                                       const char **errorOut)
{
    if (errorOut)
        *errorOut = "account or password error";
    if (login == NULL || !vm_mock_service_login_requires_auth(login))
        return true;
    if (!login->haveUserName || !login->havePassword ||
        login->userName[0] == 0 || login->password[0] == 0)
    {
        return false;
    }
    if (!vm_mock_service_account_verify_credentials(login->userName, login->password))
        return false;
    return true;
}

static void vm_mock_service_build_request_meta(vm_mock_service_request_meta *meta)
{
    if (meta == NULL)
        return;
    memset(meta, 0, sizeof(*meta));
    meta->clientId = g_mockServiceClientId;
}

static u32 vm_mock_service_encode_request_meta(const vm_mock_service_request_meta *meta,
                                               u8 *out,
                                               u32 outCap)
{
    if (meta == NULL || out == NULL || outCap < 4)
        return 0;
    vm_mock_service_write_le32(out, meta->clientId);
    return 4;
}

static void vm_mock_service_parse_account_metadata(const u8 *meta,
                                                   u32 metaLen,
                                                   vm_mock_service_request_meta *parsed)
{
    if (parsed == NULL)
        return;
    memset(parsed, 0, sizeof(*parsed));
    if (meta == NULL || metaLen == 0)
        return;
    if (metaLen >= 4)
        parsed->clientId = vm_mock_service_read_le32(meta);
}

static int vm_mock_service_resolve_ipv4_host(const char *host,
                                             int allowAny,
                                             struct in_addr *addrOut)
{
    const char *resolvedHost = host && host[0] ? host : "127.0.0.1";
    unsigned long ip = INADDR_NONE;

    if (addrOut == NULL)
        return 0;
    if (allowAny &&
        (strcmp(resolvedHost, "*") == 0 || strcmp(resolvedHost, "0.0.0.0") == 0))
    {
        addrOut->s_addr = htonl(INADDR_ANY);
        return 1;
    }
    if (strcmp(resolvedHost, "localhost") == 0)
        resolvedHost = "127.0.0.1";

    ip = inet_addr(resolvedHost);
    if (ip == INADDR_NONE && strcmp(resolvedHost, "255.255.255.255") != 0)
    {
        struct hostent *hostEntry = gethostbyname(resolvedHost);
        if (hostEntry == NULL || hostEntry->h_addr_list == NULL || hostEntry->h_addr_list[0] == NULL)
            return 0;
        memcpy(addrOut, hostEntry->h_addr_list[0], sizeof(*addrOut));
        return 1;
    }

    addrOut->s_addr = ip;
    return 1;
}

static int vm_mock_service_connect(const char *host, u16 port, vm_mock_service_socket *sockOut)
{
    vm_mock_service_socket sock = VM_MOCK_SERVICE_INVALID_SOCKET;
    struct sockaddr_in addr;
    int timeoutMs = VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS;

    if (sockOut)
        *sockOut = VM_MOCK_SERVICE_INVALID_SOCKET;
    if (!vm_mock_service_socket_init())
        return 0;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == VM_MOCK_SERVICE_INVALID_SOCKET)
        return 0;

#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
#else
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!vm_mock_service_resolve_ipv4_host(host, 0, &addr.sin_addr))
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }

    if (sockOut)
        *sockOut = sock;
    return 1;
}

/* Payment HTTP requests briefly release this legacy-state lock while waiting
 * on the remote provider.  Declare it before the included web implementation
 * so that the payment helper can keep game requests moving during that wait. */
static pthread_mutex_t g_vm_mock_service_protocol_mutex = PTHREAD_MUTEX_INITIALIZER;

/* HTTP management implementation depends on the shared helpers above. */
#include "../web_admin_server.c"

static int vm_net_mock_service_run_forever(const char *bindHost, u16 port);

static int vm_net_mock_service_probe_endpoint(void)
{
    vm_mock_service_socket sock = VM_MOCK_SERVICE_INVALID_SOCKET;
    u8 header[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 responseHeader[VM_MOCK_SERVICE_FRAME_SIZE];

    if (!vm_mock_service_connect(g_mockServiceHost, g_mockServicePort, &sock))
        return 0;

    vm_mock_service_encode_header(header, "CBMS", VM_MOCK_SERVICE_REQUEST_FLAG_PING, 0, 0);
    if (!vm_mock_service_send_all(sock, header, sizeof(header)) ||
        !vm_mock_service_recv_all(sock, responseHeader, sizeof(responseHeader)))
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }
    vm_mock_service_socket_close(sock);
    return memcmp(responseHeader, "CBMR", 4) == 0 &&
           vm_mock_service_read_le32(responseHeader + 4) == 1;
}

static u32 vm_net_mock_process_request_bytes(u32 connectId,
                                             const u8 *request,
                                             u32 requestLen,
                                             u8 *response,
                                             u32 responseCap,
                                             u32 *responseEventTypeOut,
                                             bool *closeAfterDataOut)
{
    u8 requestWtKind = 0;
    u8 requestWtSubtype = 0;
    bool haveWtHeader = false;
    bool closeAfterData = false;
    bool cacheSceneTaskState = false;
    u32 responseLen = 0;
    const char *source = "-";

    g_netLastHandledValid = 0;
    g_netLastHandledResponseLen = 0;
    g_netLastHandledSource[0] = 0;
    g_netLastHandledSummary[0] = 0;

    if (request == NULL || requestLen == 0 || response == NULL || responseCap == 0)
    {
        if (responseEventTypeOut)
            *responseEventTypeOut = 7;
        if (closeAfterDataOut)
            *closeAfterDataOut = false;
        return 0;
    }

    closeAfterData = vm_net_mock_request_contains(request, requestLen, "version") &&
                     !vm_net_mock_request_contains(request, requestLen, "start") &&
                          vm_net_mock_has_installed_update();
    haveWtHeader = vm_net_mock_get_wt_header_kind_subtype(request, requestLen,
                                                          &requestWtKind,
                                                          &requestWtSubtype);
    cacheSceneTaskState = vm_net_mock_is_scene_resource_followup_request(request,
                                                                         requestLen);
    if (cacheSceneTaskState)
    {
        vm_net_mock_task_state_request_cache_begin();
        vm_net_mock_scene_npc_request_cache_begin();
    }
    /*
     * Empty 25/5 scene-default sends are emitted from inside the client's wait
     * callback. Dispatching the response reentrantly lets the callback clear
     * R9+0x5531 before the send wrapper writes it back to 1, leaving later WT
     * requests blocked. Queue it like a normal async network response instead.
     */
    responseLen = vm_net_mock_build_response(request, requestLen, response, responseCap);
    if (cacheSceneTaskState)
    {
        vm_net_mock_task_state_request_cache_end();
        vm_net_mock_scene_npc_request_cache_end();
    }
    /*
     * Teleport-stone 16/3 now returns a main-business 30/1 scene-enter object.
     * Keep it on the normal async queue; flushing it from inside the 16/3 send
     * stack re-enters the scene screen before the caller has unwound, and the
     * new screen then stalls before emitting its post-enter follow-up request.
     */
    source = g_netLastHandledValid ? g_netLastHandledSource : "-";
    if (haveWtHeader)
    {
        vm_autotest_note("net_send connect=%u wt=%u/%u len=%u source=%s resp=%u\n",
                         connectId, requestWtKind, requestWtSubtype, requestLen,
                         source,
                         responseLen);
        if (responseLen ||
            strstr(source, "settings") != NULL ||
            strstr(source, "teleport") != NULL ||
            strstr(source, "scene") != NULL ||
            strstr(source, "mmgame") != NULL)
        {
            printf("[info][network] net_send connect=%u wt=%u/%u len=%u source=%s resp=%u\n",
                   connectId, requestWtKind, requestWtSubtype, requestLen, source, responseLen);
        }
    }
    else
    {
        vm_autotest_note("net_send connect=%u malformed len=%u source=%s resp=%u\n",
                         connectId, requestLen,
                         source,
                         responseLen);
        if (responseLen)
            printf("[info][network] net_send connect=%u malformed len=%u source=%s resp=%u\n",
                   connectId, requestLen, source, responseLen);
    }

    if (responseEventTypeOut)
        *responseEventTypeOut = 7;
    if (closeAfterDataOut)
        *closeAfterDataOut = closeAfterData;
    return responseLen;
}

static int vm_net_mock_remote_request(const u8 *request,
                                      u32 requestLen,
                                      u8 *response,
                                      u32 responseCap,
                                      u32 *responseLenOut,
                                      u32 *responseEventTypeOut,
                                      bool *closeAfterDataOut,
                                      u8 *followupResponse,
                                      u32 followupCap,
                                      u32 *followupLenOut)
{
    vm_mock_service_socket sock = VM_MOCK_SERVICE_INVALID_SOCKET;
    u8 header[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 responseHeader[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 meta[128];
    vm_mock_service_request_meta requestMeta;
    u32 flags = 0;
    u32 metaLen = 0;
    u32 responseLen = 0;
    u32 responseEventType = 7;

    if (responseLenOut)
        *responseLenOut = 0;
    if (responseEventTypeOut)
        *responseEventTypeOut = 7;
    if (closeAfterDataOut)
        *closeAfterDataOut = false;
    if (followupLenOut)
        *followupLenOut = 0;
    if (request == NULL || requestLen == 0 || response == NULL || responseCap == 0)
        return 0;
    if (!vm_mock_service_connect(g_mockServiceHost, g_mockServicePort, &sock))
        return 0;

    memset(meta, 0, sizeof(meta));
    memset(&requestMeta, 0, sizeof(requestMeta));
    vm_mock_service_build_request_meta(&requestMeta);
    metaLen = vm_mock_service_encode_request_meta(&requestMeta, meta, sizeof(meta));
    if (metaLen == 0)
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }

    vm_mock_service_encode_header(header, "CBMS", 0, requestLen + metaLen, metaLen);
    if (!vm_mock_service_send_all(sock, header, sizeof(header)) ||
        (metaLen > 0 && !vm_mock_service_send_all(sock, meta, metaLen)) ||
        !vm_mock_service_send_all(sock, request, requestLen) ||
        !vm_mock_service_recv_all(sock, responseHeader, sizeof(responseHeader)))
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }

    if (memcmp(responseHeader, "CBMR", 4) != 0 ||
        vm_mock_service_read_le32(responseHeader + 4) != 1)
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }

    flags = vm_mock_service_read_le32(responseHeader + 8);
    responseLen = vm_mock_service_read_le32(responseHeader + 12);
    responseEventType = vm_mock_service_read_le32(responseHeader + 16);
    if (responseEventType == 0)
        responseEventType = 7;
    if (responseLen > responseCap)
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }
    if (responseLen > 0 && !vm_mock_service_recv_all(sock, response, responseLen))
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }

    vm_mock_service_socket_close(sock);
    if (responseEventType == 7 &&
        followupResponse != NULL && followupCap != 0 && followupLenOut != NULL)
    {
        (void)vm_net_mock_extract_item_use_backpack_followup(response,
                                                             &responseLen,
                                                             followupResponse,
                                                             followupCap,
                                                             followupLenOut);
    }

    if (responseLenOut)
        *responseLenOut = responseLen;
    if (responseEventTypeOut)
        *responseEventTypeOut = responseEventType;
    if (closeAfterDataOut)
        *closeAfterDataOut = (flags & VM_MOCK_SERVICE_RESPONSE_FLAG_CLOSE_AFTER_DATA) != 0;
    return 1;
}

static int vm_net_mock_remote_scene_sync_poll(u8 *response,
                                              u32 responseCap,
                                              u32 *responseLenOut,
                                              u32 *responseEventTypeOut)
{
    vm_mock_service_socket sock = VM_MOCK_SERVICE_INVALID_SOCKET;
    u8 header[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 responseHeader[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 meta[16];
    vm_mock_service_request_meta requestMeta;
    u32 metaLen = 0;
    u32 responseLen = 0;
    u32 responseEventType = 7;

    if (responseLenOut)
        *responseLenOut = 0;
    if (responseEventTypeOut)
        *responseEventTypeOut = 7;
    if (response == NULL || responseCap == 0 ||
        !vm_mock_service_connect(g_mockServiceHost, g_mockServicePort, &sock))
    {
        return 0;
    }
    memset(&requestMeta, 0, sizeof(requestMeta));
    memset(meta, 0, sizeof(meta));
    vm_mock_service_build_request_meta(&requestMeta);
    metaLen = vm_mock_service_encode_request_meta(&requestMeta, meta, sizeof(meta));
    if (metaLen == 0)
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }
    vm_mock_service_encode_header(header,
                                  "CBMS",
                                  VM_MOCK_SERVICE_REQUEST_FLAG_SCENE_SYNC_POLL,
                                  metaLen,
                                  metaLen);
    if (!vm_mock_service_send_all(sock, header, sizeof(header)) ||
        !vm_mock_service_send_all(sock, meta, metaLen) ||
        !vm_mock_service_recv_all(sock, responseHeader, sizeof(responseHeader)))
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }
    if (memcmp(responseHeader, "CBMR", 4) != 0 ||
        vm_mock_service_read_le32(responseHeader + 4) != 1)
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }
    responseLen = vm_mock_service_read_le32(responseHeader + 12);
    responseEventType = vm_mock_service_read_le32(responseHeader + 16);
    if (responseEventType == 0)
        responseEventType = 7;
    if (responseLen > responseCap ||
        (responseLen > 0 && !vm_mock_service_recv_all(sock, response, responseLen)))
    {
        vm_mock_service_socket_close(sock);
        return 0;
    }
    vm_mock_service_socket_close(sock);
    if (responseLenOut)
        *responseLenOut = responseLen;
    if (responseEventTypeOut)
        *responseEventTypeOut = responseEventType;
    return 1;
}

static void vm_net_mock_service_notify_disconnect(const char *reason)
{
    vm_mock_service_socket sock = VM_MOCK_SERVICE_INVALID_SOCKET;
    u8 header[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 responseHeader[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 meta[16];
    vm_mock_service_request_meta requestMeta;
    u32 metaLen = 0;

    if (g_mockServiceOnly || g_mockServiceClientId == 0)
        return;
    if (!vm_mock_service_connect(g_mockServiceHost, g_mockServicePort, &sock))
    {
        printf("[warn][mock-service] disconnect_notify_failed client=%08x reason=%s stage=connect\n",
               g_mockServiceClientId,
               reason ? reason : "-");
        return;
    }
    memset(&requestMeta, 0, sizeof(requestMeta));
    memset(meta, 0, sizeof(meta));
    vm_mock_service_build_request_meta(&requestMeta);
    metaLen = vm_mock_service_encode_request_meta(&requestMeta, meta, sizeof(meta));
    if (metaLen == 0)
    {
        vm_mock_service_socket_close(sock);
        return;
    }
    vm_mock_service_encode_header(header,
                                  "CBMS",
                                  VM_MOCK_SERVICE_REQUEST_FLAG_CLIENT_DISCONNECT,
                                  metaLen,
                                  metaLen);
    if (!vm_mock_service_send_all(sock, header, sizeof(header)) ||
        !vm_mock_service_send_all(sock, meta, metaLen) ||
        !vm_mock_service_recv_all(sock, responseHeader, sizeof(responseHeader)) ||
        memcmp(responseHeader, "CBMR", 4) != 0 ||
        vm_mock_service_read_le32(responseHeader + 4) != 1)
    {
        printf("[warn][mock-service] disconnect_notify_failed client=%08x reason=%s stage=exchange\n",
               g_mockServiceClientId,
               reason ? reason : "-");
        vm_mock_service_socket_close(sock);
        return;
    }
    vm_mock_service_socket_close(sock);
    printf("[info][mock-service] disconnect_notify client=%08x reason=%s\n",
           g_mockServiceClientId,
           reason ? reason : "-");
}

/*
 * The service still restores an account snapshot into emulator-era globals
 * while a request is being built.  Those globals, the online/session tables,
 * the social/team state and the single MySQL connection form one protocol
 * state domain and must never be used by two workers at the same time.
 *
 * Socket receive/send deliberately stay outside this mutex.  A slow or
 * half-open client therefore cannot prevent other workers from accepting and
 * reading requests, while the legacy state transition remains atomic until it
 * is migrated to an explicit per-request context.
 */
static int vm_net_mock_service_handle_client(vm_mock_service_socket client,
                                             u8 *requestBuffer,
                                             u32 requestCap,
                                             u8 *responseBuffer,
                                             u32 responseCap,
                                             u32 workerQueueWaitMs)
{
    u8 header[VM_MOCK_SERVICE_FRAME_SIZE];
    char accountId[64];
    char handledSource[sizeof(g_netLastHandledSource)];
    const char *logAccountId = "-";
    vm_mock_service_request_meta requestMeta;
    u32 requestFlags = 0;
    u32 requestLen = 0;
    u32 requestMetaLen = 0;
    u32 payloadLen = 0;
    u32 responseLen = 0;
    u32 responseEventType = 7;
    u32 responseFlags = 0;
    u32 requestReceivedMs = 0;
    u32 requestProcessStartMs = 0;
    u32 requestProcessEndMs = 0;
    u32 protocolWaitStartMs = 0;
    u32 protocolLockMs = 0;
    u32 protocolWaitMs = 0;
    u32 protocolHoldMs = 0;
    bool closeAfterData = false;
    bool haveLoginRequest = false;
    bool allowStatelessRequest = false;
    bool handledValid = false;
    bool protocolLocked = false;
    vm_mock_service_login_request loginRequest;
    vm_mock_service_account_state *accountState = NULL;
    vm_mock_service_client_session *clientSession = NULL;
    const char *authError = NULL;
    const char *sessionAccount = NULL;

    if (!vm_mock_service_recv_all(client, header, sizeof(header)))
        return 0;
    if (memcmp(header, "CBMS", 4) != 0 || vm_mock_service_read_le32(header + 4) != 1)
        return 0;

    requestFlags = vm_mock_service_read_le32(header + 8);
    requestLen = vm_mock_service_read_le32(header + 12);
    requestMetaLen = vm_mock_service_read_le32(header + 16);
    if (requestFlags & VM_MOCK_SERVICE_REQUEST_FLAG_PING)
    {
        vm_mock_service_encode_header(header, "CBMR", 0, 0, 0);
        return vm_mock_service_send_all(client, header, sizeof(header));
    }
    if (requestLen == 0 || requestLen > requestCap || requestMetaLen > requestLen)
        return 0;
    if (!vm_mock_service_recv_all(client, requestBuffer, requestLen))
        return 0;
    requestReceivedMs = scheduler_get_tick_ms();

    memset(accountId, 0, sizeof(accountId));
    memset(handledSource, 0, sizeof(handledSource));
    memset(&requestMeta, 0, sizeof(requestMeta));
    memset(&loginRequest, 0, sizeof(loginRequest));
    vm_mock_service_parse_account_metadata(requestBuffer, requestMetaLen, &requestMeta);
    payloadLen = requestLen - requestMetaLen;

    protocolWaitStartMs = scheduler_get_tick_ms();
    pthread_mutex_lock(&g_vm_mock_service_protocol_mutex);
    protocolLocked = true;
    protocolLockMs = scheduler_get_tick_ms();
    protocolWaitMs = protocolLockMs >= protocolWaitStartMs ?
                         protocolLockMs - protocolWaitStartMs : 0;

#define VM_MOCK_SERVICE_PROTOCOL_UNLOCK()                                      \
    do                                                                         \
    {                                                                          \
        if (protocolLocked)                                                     \
        {                                                                      \
            u32 protocolUnlockMs = scheduler_get_tick_ms();                    \
            protocolHoldMs = protocolUnlockMs >= protocolLockMs ?              \
                                 protocolUnlockMs - protocolLockMs : 0;         \
            protocolLocked = false;                                            \
            pthread_mutex_unlock(&g_vm_mock_service_protocol_mutex);           \
        }                                                                      \
    } while (0)

#define VM_MOCK_SERVICE_PROTOCOL_RETURN(value)                                 \
    do                                                                         \
    {                                                                          \
        VM_MOCK_SERVICE_PROTOCOL_UNLOCK();                                     \
        return (value);                                                        \
    } while (0)

    memset(g_vm_mock_service_login_issue_username, 0, sizeof(g_vm_mock_service_login_issue_username));
    memset(g_vm_mock_service_login_issue_password, 0, sizeof(g_vm_mock_service_login_issue_password));
    g_vm_mock_service_login_issue_result = 0;
    g_vm_mock_service_active_account = NULL;
    g_vm_mock_service_active_account_id = NULL;
    if (requestFlags & VM_MOCK_SERVICE_REQUEST_FLAG_CLIENT_DISCONNECT)
    {
        g_schedulerTick = scheduler_get_tick_ms() / VM_SCHED_FRAME_MS;
        clientSession = vm_mock_service_find_client_session(requestMeta.clientId);
        sessionAccount = vm_mock_service_find_session_account(requestMeta.clientId);
        if (sessionAccount != NULL && sessionAccount[0] != 0)
        {
            vm_mock_service_copy_account_id(accountId, sizeof(accountId), sessionAccount);
            accountState = vm_mock_service_account_find_or_create(accountId);
        }
        if (accountState != NULL)
        {
            vm_mock_service_account_restore(accountState);
            g_vm_mock_service_active_client_id = requestMeta.clientId;
            if (g_vm_net_mock_role_position_dirty)
                vm_net_mock_role_db_save("client-disconnect-position");
            vm_mock_service_account_capture(accountState);
        }
        vm_mock_service_session_mark_offline(clientSession, "explicit-disconnect");
        g_vm_mock_service_active_account = NULL;
        g_vm_mock_service_active_account_id = NULL;
        g_vm_mock_service_active_client_id = 0;
        vm_mock_service_encode_header(header, "CBMR", 0, 0, 0);
        VM_MOCK_SERVICE_PROTOCOL_UNLOCK();
        return vm_mock_service_send_all(client, header, sizeof(header));
    }
    if (requestFlags & VM_MOCK_SERVICE_REQUEST_FLAG_SCENE_SYNC_POLL)
    {
        sessionAccount = vm_mock_service_find_session_account(requestMeta.clientId);
        if (sessionAccount != NULL && sessionAccount[0] != 0)
        {
            vm_mock_service_copy_account_id(accountId, sizeof(accountId), sessionAccount);
            accountState = vm_mock_service_account_find_or_create(accountId);
        }
        if (accountState != NULL)
        {
            vm_mock_service_account_restore(accountState);
            logAccountId = accountState->accountId[0] ? accountState->accountId : "-";
            g_schedulerTick = scheduler_get_tick_ms() / VM_SCHED_FRAME_MS;
            g_vm_mock_service_active_client_id = requestMeta.clientId;
            vm_mock_service_capture_session_presence(requestMeta.clientId);
            vm_mock_service_expire_stale_online_sessions();
            responseLen = vm_net_mock_build_scene_sync_poll_response(responseBuffer, responseCap);
            vm_mock_service_account_capture(accountState);
        }
        g_vm_mock_service_active_account = NULL;
        g_vm_mock_service_active_account_id = NULL;
        g_vm_mock_service_active_client_id = 0;
        vm_mock_service_encode_header(header, "CBMR", 0, responseLen, 7);
        VM_MOCK_SERVICE_PROTOCOL_UNLOCK();
        if (!vm_mock_service_send_all(client, header, sizeof(header)))
            return 0;
        if (responseLen > 0 && !vm_mock_service_send_all(client, responseBuffer, responseLen))
            return 0;
        if (responseLen > 0)
        {
            printf("[info][mock-service] account=%s scene_sync_poll client=%08x response=%u event=7 queue_wait_ms=%u state_wait_ms=%u state_hold_ms=%u\n",
                   logAccountId,
                   requestMeta.clientId,
                   responseLen,
                   workerQueueWaitMs,
                   protocolWaitMs,
                   protocolHoldMs);
        }
        fflush(stdout);
        return 1;
    }
    if (payloadLen == 0)
        VM_MOCK_SERVICE_PROTOCOL_RETURN(0);

    haveLoginRequest = vm_mock_service_parse_login_request(requestBuffer + requestMetaLen,
                                                           payloadLen,
                                                           &loginRequest);
    if (haveLoginRequest && !vm_mock_service_authenticate_login_request(&loginRequest, &authError))
    {
        responseLen = vm_net_mock_build_login_failure_response(loginRequest.requestSubtype,
                                                               authError ? authError : "account or password error",
                                                               responseBuffer,
                                                               responseCap);
        vm_mock_service_encode_header(header, "CBMR", 0, responseLen, 7);
        VM_MOCK_SERVICE_PROTOCOL_UNLOCK();
        if (!vm_mock_service_send_all(client, header, sizeof(header)))
            return 0;
        if (responseLen > 0 && !vm_mock_service_send_all(client, responseBuffer, responseLen))
            return 0;
        printf("[info][mock-service] login_reject client=%08x user=%s reason=%s queue_wait_ms=%u state_wait_ms=%u state_hold_ms=%u\n",
               requestMeta.clientId,
               loginRequest.userName[0] ? loginRequest.userName : "-",
               authError ? authError : "account or password error",
               workerQueueWaitMs,
               protocolWaitMs,
               protocolHoldMs);
        return 1;
    }

    if (haveLoginRequest && vm_mock_service_login_is_no_account(&loginRequest))
    {
        char issuedPassword[64];
        memset(issuedPassword, 0, sizeof(issuedPassword));
        sessionAccount = vm_mock_service_find_session_account(requestMeta.clientId);
        if (sessionAccount && sessionAccount[0] != 0)
        {
            vm_mock_service_copy_account_id(accountId, sizeof(accountId), sessionAccount);
            if (!vm_mock_service_account_copy_password(accountId, issuedPassword, sizeof(issuedPassword)))
            {
                authError = "guest account unavailable";
                responseLen = vm_net_mock_build_login_failure_response(loginRequest.requestSubtype,
                                                                       authError,
                                                                       responseBuffer,
                                                                       responseCap);
                vm_mock_service_encode_header(header, "CBMR", 0, responseLen, 7);
                VM_MOCK_SERVICE_PROTOCOL_UNLOCK();
                if (!vm_mock_service_send_all(client, header, sizeof(header)))
                    return 0;
                if (responseLen > 0 && !vm_mock_service_send_all(client, responseBuffer, responseLen))
                    return 0;
                return 1;
            }
        }
        else
        {
            if (!vm_mock_service_account_issue_guest_credentials(requestMeta.clientId,
                                                                 accountId, sizeof(accountId),
                                                                 issuedPassword, sizeof(issuedPassword),
                                                                 &authError))
            {
                responseLen = vm_net_mock_build_login_failure_response(loginRequest.requestSubtype,
                                                                       authError ? authError : "guest account unavailable",
                                                                       responseBuffer,
                                                                       responseCap);
                vm_mock_service_encode_header(header, "CBMR", 0, responseLen, 7);
                VM_MOCK_SERVICE_PROTOCOL_UNLOCK();
                if (!vm_mock_service_send_all(client, header, sizeof(header)))
                    return 0;
                if (responseLen > 0 && !vm_mock_service_send_all(client, responseBuffer, responseLen))
                    return 0;
                return 1;
            }
        }
        vm_mock_service_bind_session_account(requestMeta.clientId, accountId);
        snprintf(g_vm_mock_service_login_issue_username, sizeof(g_vm_mock_service_login_issue_username), "%s", accountId);
        snprintf(g_vm_mock_service_login_issue_password, sizeof(g_vm_mock_service_login_issue_password), "%s", issuedPassword);
        g_vm_mock_service_login_issue_result = 3;
        printf("[info][mock-service] login_no_account_issue client=%08x user=%s result=%u\n",
               requestMeta.clientId,
               accountId,
               g_vm_mock_service_login_issue_result);
    }
    else if (haveLoginRequest && vm_mock_service_login_requires_auth(&loginRequest))
    {
        vm_mock_service_copy_account_id(accountId, sizeof(accountId), loginRequest.userName);
        vm_mock_service_bind_session_account(requestMeta.clientId, accountId);
    }
    else
    {
        sessionAccount = vm_mock_service_find_session_account(requestMeta.clientId);
        if (sessionAccount && sessionAccount[0] != 0)
            vm_mock_service_copy_account_id(accountId, sizeof(accountId), sessionAccount);
    }
    if (accountId[0] == 0)
    {
        u8 wtKind = 0;
        u8 wtSubtype = 0;
        bool haveWtHeader = false;
        allowStatelessRequest = vm_mock_service_request_is_stateless_prelogin(requestBuffer + requestMetaLen,
                                                                              payloadLen);
        haveWtHeader = vm_net_mock_get_wt_header_kind_subtype(requestBuffer + requestMetaLen,
                                                              payloadLen,
                                                              &wtKind,
                                                              &wtSubtype);
        if (!allowStatelessRequest)
        {
            if (haveWtHeader)
            {
                printf("[warn][mock-service] unbound request client=%08x wt=%u/%u payload=%u source=drop-no-account\n",
                       requestMeta.clientId, wtKind, wtSubtype, payloadLen);
            }
            else
            {
                printf("[warn][mock-service] unbound request client=%08x payload=%u source=drop-no-account\n",
                       requestMeta.clientId, payloadLen);
            }
            memset(g_vm_mock_service_login_issue_username, 0, sizeof(g_vm_mock_service_login_issue_username));
            memset(g_vm_mock_service_login_issue_password, 0, sizeof(g_vm_mock_service_login_issue_password));
            g_vm_mock_service_login_issue_result = 0;
            VM_MOCK_SERVICE_PROTOCOL_RETURN(0);
        }
    }
    else
    {
        accountState = vm_mock_service_account_find_or_create(accountId);
        if (accountState == NULL)
            VM_MOCK_SERVICE_PROTOCOL_RETURN(0);
        vm_mock_service_account_restore(accountState);
        logAccountId = accountState->accountId[0] ? accountState->accountId : "-";
    }

    g_schedulerTick = scheduler_get_tick_ms() / VM_SCHED_FRAME_MS;
    g_vm_mock_service_active_client_id = requestMeta.clientId;
    requestProcessStartMs = scheduler_get_tick_ms();
    responseLen = vm_net_mock_process_request_bytes(0,
                                                    requestBuffer + requestMetaLen,
                                                    payloadLen,
                                                    responseBuffer,
                                                    responseCap,
                                                    &responseEventType,
                                                    &closeAfterData);
    requestProcessEndMs = scheduler_get_tick_ms();
    if (accountState != NULL)
    {
        vm_mock_service_capture_session_presence(requestMeta.clientId);
        vm_mock_service_account_capture(accountState);
    }
    memset(g_vm_mock_service_login_issue_username, 0, sizeof(g_vm_mock_service_login_issue_username));
    memset(g_vm_mock_service_login_issue_password, 0, sizeof(g_vm_mock_service_login_issue_password));
    g_vm_mock_service_login_issue_result = 0;
    g_vm_mock_service_active_account = NULL;
    g_vm_mock_service_active_account_id = NULL;
    g_vm_mock_service_active_client_id = 0;
    handledValid = g_netLastHandledValid != 0;
    if (handledValid)
        snprintf(handledSource, sizeof(handledSource), "%s", g_netLastHandledSource);
    if (closeAfterData)
        responseFlags |= VM_MOCK_SERVICE_RESPONSE_FLAG_CLOSE_AFTER_DATA;

    vm_mock_service_encode_header(header, "CBMR", responseFlags, responseLen, responseEventType);
    VM_MOCK_SERVICE_PROTOCOL_UNLOCK();
    if (!vm_mock_service_send_all(client, header, sizeof(header)))
    {
        printf("[warn][mock-service] response_send_failed stage=header account=%s request=%u response=%u process_ms=%u\n",
               logAccountId, payloadLen, responseLen,
               requestProcessEndMs >= requestProcessStartMs ?
                   requestProcessEndMs - requestProcessStartMs : 0);
        return 0;
    }
    if (responseLen > 0 && !vm_mock_service_send_all(client, responseBuffer, responseLen))
    {
        printf("[warn][mock-service] response_send_failed stage=body account=%s request=%u response=%u process_ms=%u\n",
               logAccountId, payloadLen, responseLen,
               requestProcessEndMs >= requestProcessStartMs ?
                   requestProcessEndMs - requestProcessStartMs : 0);
        return 0;
    }

    if (handledValid &&
        strcmp(handledSource, "builtin-actor-moveinfo-ack") == 0)
    {
        u32 nowMs = scheduler_get_tick_ms();
        printf("[info][mock-service] actor_moveinfo_timing client=%08x process_ms=%u total_ms=%u\n",
               requestMeta.clientId,
               requestProcessEndMs >= requestProcessStartMs ?
                   requestProcessEndMs - requestProcessStartMs : 0,
               nowMs >= requestReceivedMs ? nowMs - requestReceivedMs : 0);
    }

    printf("[info][mock-service] account=%s request=%u response=%u event=%u flags=%u source=%s queue_wait_ms=%u state_wait_ms=%u state_hold_ms=%u process_ms=%u\n",
           logAccountId,
           payloadLen, responseLen, responseEventType, responseFlags,
           handledValid ? handledSource : "-",
           workerQueueWaitMs,
           protocolWaitMs,
           protocolHoldMs,
           requestProcessEndMs >= requestProcessStartMs ?
               requestProcessEndMs - requestProcessStartMs : 0);
    /* Response bytes are already on the client socket and the protocol lock
     * has been released.  Flush the service-only batch here so diagnostics
     * remain current without putting disk latency on the request critical
     * path. */
    fflush(stdout);
#undef VM_MOCK_SERVICE_PROTOCOL_RETURN
#undef VM_MOCK_SERVICE_PROTOCOL_UNLOCK
    return 1;
}

static bool vm_net_mock_service_ensure_resource_root(void)
{
    const char *configuredRoot = getenv("CBE_RESOURCE_ROOT");
    char candidate[1200];

    if (vm_net_mock_resource_dir()[0] != 0)
        return true;

    if (configuredRoot != NULL && configuredRoot[0] != 0)
    {
        static const char *suffixes[] = {
            "", "/web/fs/JHOnlineData", "/JHOnlineData", "/bin/JHOnlineData"
        };
        for (u32 i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
        {
            if (snprintf(candidate, sizeof(candidate), "%s%s",
                         configuredRoot, suffixes[i]) >= (int)sizeof(candidate))
            {
                continue;
            }
            if (vm_net_mock_set_resource_dir(candidate))
            {
                printf("[info][mock-service] resource_root=%s source=environment\n",
                       vm_net_mock_resource_dir());
                return true;
            }
        }
    }

    {
        static const char *relativeCandidates[] = {
            "../web/fs/JHOnlineData", "web/fs/JHOnlineData",
            "JHOnlineData", "bin/JHOnlineData", "../bin/JHOnlineData"
        };
        for (u32 i = 0;
             i < sizeof(relativeCandidates) / sizeof(relativeCandidates[0]); ++i)
        {
            if (vm_net_mock_set_resource_dir(relativeCandidates[i]))
            {
                printf("[info][mock-service] resource_root=%s source=service-auto\n",
                       vm_net_mock_resource_dir());
                return true;
            }
        }
    }
    return false;
}

enum
{
    VM_MOCK_SERVICE_WORKER_DEFAULT = 4,
    VM_MOCK_SERVICE_WORKER_MIN = 2,
    VM_MOCK_SERVICE_WORKER_MAX = 16,
    VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX = 128
};

typedef enum
{
    VM_MOCK_SERVICE_CONNECTION_GAME = 1,
    VM_MOCK_SERVICE_CONNECTION_ADMIN = 2
} vm_mock_service_connection_kind;

typedef struct
{
    vm_mock_service_socket socket;
    vm_mock_service_connection_kind kind;
    u32 acceptedMs;
    u32 sequence;
} vm_mock_service_connection_job;

typedef struct vm_mock_service_worker_pool vm_mock_service_worker_pool;

typedef struct
{
    vm_mock_service_worker_pool *pool;
    pthread_t thread;
    u32 workerId;
    u8 *requestBuffer;
    u8 *responseBuffer;
} vm_mock_service_worker_context;

struct vm_mock_service_worker_pool
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool initialized;
    bool stopRequested;
    u32 workerCount;
    u32 nextSequence;
    u32 queueHead;
    u32 queueTail;
    u32 queuedJobs;
    vm_mock_service_connection_job jobs[VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX];
    vm_mock_service_worker_context workers[VM_MOCK_SERVICE_WORKER_MAX];
};

static vm_mock_service_worker_pool g_vmMockServiceWorkerPool;

static u32 vm_mock_service_worker_count_from_environment(void)
{
    const char *configured = getenv("CBE_MOCK_SERVICE_WORKERS");
    long value = VM_MOCK_SERVICE_WORKER_DEFAULT;
    char *end = NULL;

    if (configured != NULL && configured[0] != 0)
    {
        value = strtol(configured, &end, 10);
        if (end == configured || *end != 0)
            value = VM_MOCK_SERVICE_WORKER_DEFAULT;
    }
    if (value < VM_MOCK_SERVICE_WORKER_MIN)
        value = VM_MOCK_SERVICE_WORKER_MIN;
    if (value > VM_MOCK_SERVICE_WORKER_MAX)
        value = VM_MOCK_SERVICE_WORKER_MAX;
    return (u32)value;
}

static void vm_mock_service_configure_accepted_socket(vm_mock_service_socket client)
{
#ifdef _WIN32
    int timeoutMs = VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
#else
    struct timeval timeout;
    timeout.tv_sec = VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS / 1000;
    timeout.tv_usec = (VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS % 1000) * 1000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

static void *vm_mock_service_connection_worker_main(void *opaque)
{
    vm_mock_service_worker_context *worker = (vm_mock_service_worker_context *)opaque;
    vm_mock_service_worker_pool *pool = worker ? worker->pool : NULL;

    if (worker == NULL || pool == NULL)
        return NULL;

    for (;;)
    {
        vm_mock_service_connection_job job;
        u32 workerStartMs = 0;
        u32 queueWaitMs = 0;
        int ok = 0;

        memset(&job, 0, sizeof(job));
        job.socket = VM_MOCK_SERVICE_INVALID_SOCKET;
        pthread_mutex_lock(&pool->mutex);
        while (!pool->stopRequested && pool->queuedJobs == 0)
            pthread_cond_wait(&pool->condition, &pool->mutex);
        if (pool->stopRequested && pool->queuedJobs == 0)
        {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }
        job = pool->jobs[pool->queueHead];
        pool->queueHead = (pool->queueHead + 1) % VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX;
        pool->queuedJobs--;
        pthread_mutex_unlock(&pool->mutex);

        workerStartMs = scheduler_get_tick_ms();
        queueWaitMs = workerStartMs >= job.acceptedMs ?
                          workerStartMs - job.acceptedMs : 0;
        if (job.kind == VM_MOCK_SERVICE_CONNECTION_GAME)
        {
            ok = vm_net_mock_service_handle_client(job.socket,
                                                   worker->requestBuffer,
                                                   sizeof(g_netMockResponse),
                                                   worker->responseBuffer,
                                                   sizeof(g_netMockResponse),
                                                   queueWaitMs);
            if (!ok)
            {
                printf("[warn][mock-service] dropped malformed request worker=%u sequence=%u queue_wait_ms=%u\n",
                       worker->workerId, job.sequence, queueWaitMs);
            }
        }
        else if (job.kind == VM_MOCK_SERVICE_CONNECTION_ADMIN)
        {
            u32 protocolWaitStartMs = scheduler_get_tick_ms();
            u32 protocolLockMs = 0;
            u32 protocolDoneMs = 0;

            /* The admin surface shares the same MySQL connection and mutates
             * the same account/guild/task caches as game requests. */
            pthread_mutex_lock(&g_vm_mock_service_protocol_mutex);
            protocolLockMs = scheduler_get_tick_ms();
            ok = vm_mock_admin_handle_client(job.socket);
            protocolDoneMs = scheduler_get_tick_ms();
            pthread_mutex_unlock(&g_vm_mock_service_protocol_mutex);
            if (!ok || queueWaitMs > 20 ||
                protocolLockMs - protocolWaitStartMs > 20 ||
                protocolDoneMs - protocolLockMs > 100)
            {
                printf("[info][mock-admin] request worker=%u sequence=%u result=%d queue_wait_ms=%u state_wait_ms=%u process_ms=%u\n",
                       worker->workerId,
                       job.sequence,
                       ok,
                       queueWaitMs,
                       protocolLockMs >= protocolWaitStartMs ?
                           protocolLockMs - protocolWaitStartMs : 0,
                       protocolDoneMs >= protocolLockMs ?
                           protocolDoneMs - protocolLockMs : 0);
            }
        }
        vm_mock_service_socket_close(job.socket);
    }
}

static bool vm_mock_service_worker_pool_start(vm_mock_service_worker_pool *pool)
{
    u32 requestedWorkers = vm_mock_service_worker_count_from_environment();
    u32 createdWorkers = 0;

    if (pool == NULL)
        return false;
    memset(pool, 0, sizeof(*pool));
    if (pthread_mutex_init(&pool->mutex, NULL) != 0)
        return false;
    if (pthread_cond_init(&pool->condition, NULL) != 0)
    {
        pthread_mutex_destroy(&pool->mutex);
        return false;
    }
    pool->nextSequence = 1;
    pool->workerCount = requestedWorkers;
    for (u32 i = 0; i < requestedWorkers; ++i)
    {
        vm_mock_service_worker_context *worker = &pool->workers[i];
        worker->pool = pool;
        worker->workerId = i + 1;
        worker->requestBuffer = (u8 *)malloc(sizeof(g_netMockResponse));
        worker->responseBuffer = (u8 *)malloc(sizeof(g_netMockResponse));
        if (worker->requestBuffer == NULL || worker->responseBuffer == NULL ||
            pthread_create(&worker->thread, NULL,
                           vm_mock_service_connection_worker_main, worker) != 0)
        {
            free(worker->requestBuffer);
            free(worker->responseBuffer);
            worker->requestBuffer = NULL;
            worker->responseBuffer = NULL;
            pthread_mutex_lock(&pool->mutex);
            pool->stopRequested = true;
            pthread_cond_broadcast(&pool->condition);
            pthread_mutex_unlock(&pool->mutex);
            for (u32 j = 0; j < createdWorkers; ++j)
                pthread_join(pool->workers[j].thread, NULL);
            for (u32 j = 0; j < createdWorkers; ++j)
            {
                free(pool->workers[j].requestBuffer);
                free(pool->workers[j].responseBuffer);
            }
            pthread_cond_destroy(&pool->condition);
            pthread_mutex_destroy(&pool->mutex);
            memset(pool, 0, sizeof(*pool));
            return false;
        }
        createdWorkers++;
    }
    pool->initialized = true;
    printf("[info][mock-service] concurrency workers=%u queue=%u state_model=serialized-legacy-context\n",
           pool->workerCount,
           (u32)VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX);
    return true;
}

static bool vm_mock_service_worker_pool_enqueue(vm_mock_service_worker_pool *pool,
                                                vm_mock_service_socket client,
                                                vm_mock_service_connection_kind kind,
                                                u32 *sequenceOut)
{
    vm_mock_service_connection_job *job = NULL;
    bool queued = false;

    if (sequenceOut != NULL)
        *sequenceOut = 0;
    if (pool == NULL || !pool->initialized ||
        client == VM_MOCK_SERVICE_INVALID_SOCKET)
    {
        return false;
    }
    pthread_mutex_lock(&pool->mutex);
    if (!pool->stopRequested &&
        pool->queuedJobs < VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX)
    {
        job = &pool->jobs[pool->queueTail];
        memset(job, 0, sizeof(*job));
        job->socket = client;
        job->kind = kind;
        job->acceptedMs = scheduler_get_tick_ms();
        job->sequence = pool->nextSequence++;
        if (pool->nextSequence == 0)
            pool->nextSequence = 1;
        pool->queueTail = (pool->queueTail + 1) % VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX;
        pool->queuedJobs++;
        if (sequenceOut != NULL)
            *sequenceOut = job->sequence;
        queued = true;
        pthread_cond_signal(&pool->condition);
    }
    pthread_mutex_unlock(&pool->mutex);
    return queued;
}

static int vm_net_mock_service_run_forever(const char *bindHost, u16 port)
{
    vm_mock_service_socket serverSocket = VM_MOCK_SERVICE_INVALID_SOCKET;
    vm_mock_service_socket adminSocket = VM_MOCK_SERVICE_INVALID_SOCKET;
    struct sockaddr_in addr;
    const char *resolvedBindHost = bindHost && bindHost[0] ? bindHost : "127.0.0.1";

    if (!vm_net_mock_service_ensure_resource_root())
    {
        printf("[error][mock-service] resource root unresolved required=task.dsh+item.dsh+equip.dsh\n");
        return -1;
    }

    if (!vm_mock_service_socket_init())
    {
        printf("[error][mock-service] WSAStartup/init failed\n");
        return -1;
    }

    /* Validate the transactional authority before accepting a game client.
     * Legacy files and payload snapshots are allowed only during the first
     * migration; after the seal is recorded, an absent relational row is
     * surfaced as data loss instead of silently restoring an old snapshot. */
    if (!vm_mock_service_mysql_authority_prepare())
    {
        printf("[error][mock-service] mysql persistence unavailable reason=authority-prepare error=%s\n",
               vm_mysql_last_error());
        return -1;
    }
    vm_mock_service_account_db_load();
    vm_mock_service_friend_db_load();
    if (!g_vm_mock_service_account_db_valid || !g_vm_mock_service_friend_db_valid ||
        !vm_mock_service_migrate_account_role_databases() ||
        !vm_mock_service_mysql_authority_seal())
    {
        printf("[error][mock-service] mysql persistence unavailable error=%s\n",
               vm_mysql_last_error());
        return -1;
    }
    if (!vm_mock_payment_ensure_tables())
        printf("[warn][payment] recharge_disabled reason=schema-unavailable\n");
    if (!vm_net_mock_validate_xse_task_resources())
    {
        printf("[error][mock-service] xse/task resource validation failed\n");
        return -1;
    }

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == VM_MOCK_SERVICE_INVALID_SOCKET)
    {
        printf("[error][mock-service] socket create failed\n");
        return -1;
    }

    {
        int reuse = 1;
#ifdef _WIN32
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#else
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!vm_mock_service_resolve_ipv4_host(resolvedBindHost, 1, &addr.sin_addr))
    {
        printf("[error][mock-service] invalid bind host %s\n", resolvedBindHost);
        vm_mock_service_socket_close(serverSocket);
        return -1;
    }
    if (bind(serverSocket, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        printf("[error][mock-service] bind %s:%u failed\n", resolvedBindHost, port);
        vm_mock_service_socket_close(serverSocket);
        return -1;
    }
    if (listen(serverSocket, VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX) != 0)
    {
        printf("[error][mock-service] listen %s:%u failed\n", resolvedBindHost, port);
        vm_mock_service_socket_close(serverSocket);
        return -1;
    }

    printf("[info][mock-service] listening=%s:%u\n", resolvedBindHost, port);
    if (g_mockAdminPort == port)
    {
        printf("[error][mock-admin] port conflicts with game service port=%u\n", g_mockAdminPort);
    }
    else
    {
        adminSocket = vm_mock_admin_open_listener(g_mockAdminBindHost, g_mockAdminPort);
        if (adminSocket == VM_MOCK_SERVICE_INVALID_SOCKET)
            printf("[error][mock-admin] listen %s:%u failed; game service remains available\n",
                   g_mockAdminBindHost, g_mockAdminPort);
        else
            printf("[info][mock-admin] listening=http://%s:%u/\n",
                   g_mockAdminBindHost, g_mockAdminPort);
    }
    if (!vm_mock_service_worker_pool_start(&g_vmMockServiceWorkerPool))
    {
        printf("[error][mock-service] worker pool start failed\n");
        vm_mock_service_socket_close(adminSocket);
        vm_mock_service_socket_close(serverSocket);
        return -1;
    }
    fflush(stdout);
    for (;;)
    {
        vm_mock_service_socket client = VM_MOCK_SERVICE_INVALID_SOCKET;
        vm_mock_service_socket adminClient = VM_MOCK_SERVICE_INVALID_SOCKET;
        struct sockaddr_in clientAddr;
        struct sockaddr_in adminClientAddr;
        int selectRc = 0;
#ifdef _WIN32
        int clientAddrLen = sizeof(clientAddr);
        int adminClientAddrLen = sizeof(adminClientAddr);
#else
        socklen_t clientAddrLen = sizeof(clientAddr);
        socklen_t adminClientAddrLen = sizeof(adminClientAddr);
#endif
        fd_set readSet;
        struct timeval timeout;

        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);
        if (adminSocket != VM_MOCK_SERVICE_INVALID_SOCKET)
            FD_SET(adminSocket, &readSet);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
#ifdef _WIN32
        selectRc = select(0, &readSet, NULL, NULL, &timeout);
#else
        selectRc = select((adminSocket > serverSocket ? adminSocket : serverSocket) + 1,
                          &readSet, NULL, NULL, &timeout);
#endif
        if (selectRc <= 0)
            continue;

        if (FD_ISSET(serverSocket, &readSet))
        {
            client = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
            if (client != VM_MOCK_SERVICE_INVALID_SOCKET)
            {
                u32 sequence = 0;
                vm_mock_service_configure_accepted_socket(client);
                if (!vm_mock_service_worker_pool_enqueue(&g_vmMockServiceWorkerPool,
                                                         client,
                                                         VM_MOCK_SERVICE_CONNECTION_GAME,
                                                         &sequence))
                {
                    printf("[warn][mock-service] connection_rejected kind=game reason=queue-full\n");
                    vm_mock_service_socket_close(client);
                }
            }
        }
        if (adminSocket != VM_MOCK_SERVICE_INVALID_SOCKET && FD_ISSET(adminSocket, &readSet))
        {
            adminClient = accept(adminSocket, (struct sockaddr *)&adminClientAddr,
                                 &adminClientAddrLen);
            if (adminClient != VM_MOCK_SERVICE_INVALID_SOCKET)
            {
                u32 sequence = 0;
                vm_mock_service_configure_accepted_socket(adminClient);
                if (!vm_mock_service_worker_pool_enqueue(&g_vmMockServiceWorkerPool,
                                                         adminClient,
                                                         VM_MOCK_SERVICE_CONNECTION_ADMIN,
                                                         &sequence))
                {
                    printf("[warn][mock-admin] connection_rejected reason=queue-full\n");
                    vm_mock_service_socket_close(adminClient);
                }
            }
        }
    }
}

enum
{
    VM_NET_MOCK_ASYNC_REQUEST_MAX = 512,
    VM_NET_MOCK_ASYNC_QUEUE_MAX = 64
};

typedef enum
{
    VM_NET_MOCK_ASYNC_JOB_DATA = 1,
    VM_NET_MOCK_ASYNC_JOB_SCENE_POLL = 2
} vm_net_mock_async_job_kind;

typedef struct vm_net_mock_async_job
{
    struct vm_net_mock_async_job *next;
    u32 generation;
    u32 sequence;
    u32 enqueueMs;
    u32 connectId;
    u32 requestLen;
    vm_net_mock_async_job_kind kind;
    u8 request[VM_NET_MOCK_ASYNC_REQUEST_MAX];
} vm_net_mock_async_job;

typedef struct vm_net_mock_async_completion
{
    struct vm_net_mock_async_completion *next;
    u32 generation;
    u32 sequence;
    u32 enqueueMs;
    u32 workerStartMs;
    u32 workerDoneMs;
    u32 connectId;
    u32 responseEventType;
    u32 responseLen;
    u32 followupEventType;
    u32 followupLen;
    vm_net_mock_async_job_kind kind;
    bool success;
    bool closeAfterData;
    bool requestIsUpdateChunk;
    u32 updateChunkStart;
    char updateChunkName[64];
    u8 *response;
    u8 *followup;
} vm_net_mock_async_completion;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    bool workerStarted;
    bool stopRequested;
    bool scenePollOutstanding;
    u32 generation;
    u32 nextSequence;
    u32 queuedJobs;
    vm_net_mock_async_job *jobHead;
    vm_net_mock_async_job *jobTail;
    vm_net_mock_async_completion *completionHead;
    vm_net_mock_async_completion *completionTail;
} vm_net_mock_async_state;

static vm_net_mock_async_state g_vmNetMockAsync = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    0,
    false,
    false,
    false,
    1,
    1,
    0,
    NULL,
    NULL,
    NULL,
    NULL};

static void vm_net_mock_async_free_completion(vm_net_mock_async_completion *completion)
{
    if (completion == NULL)
        return;
    free(completion->response);
    free(completion->followup);
    free(completion);
}

typedef struct
{
    u8 major;
    u8 kind;
    u8 subtype;
    const u8 *payload;
    u16 payloadLen;
} vm_net_mock_response_object;

/* Server responses use the six-byte object header produced by
 * vm_net_mock_begin_wt_object().  Requests use a different five-byte object
 * header, so vm_net_mock_next_request_object() cannot safely inspect a remote
 * response. */
static bool vm_net_mock_next_response_object(const u8 *response,
                                             u32 responseLen,
                                             u32 *offset,
                                             vm_net_mock_response_object *object)
{
    u32 objectStart = 0;
    u16 objectLen = 0;

    if (response == NULL || offset == NULL || *offset < 5 ||
        *offset + 6 > responseLen)
    {
        return false;
    }
    objectStart = *offset;
    objectLen = (u16)(((u16)response[objectStart + 4] << 8) |
                      response[objectStart + 5]);
    if (objectLen < 6 || objectStart + objectLen > responseLen)
        return false;
    if (object != NULL)
    {
        object->major = response[objectStart];
        object->kind = response[objectStart + 1];
        object->subtype = response[objectStart + 2];
        object->payload = response + objectStart + 6;
        object->payloadLen = (u16)(objectLen - 6);
    }
    *offset = objectStart + objectLen;
    return true;
}

static bool vm_net_mock_response_object_field(
    const vm_net_mock_response_object *object,
    const char *field,
    const u8 **encoded,
    u16 *encodedLen)
{
    u32 pos = 0;
    u32 fieldLen = field ? (u32)strlen(field) : 0;

    if (encoded != NULL)
        *encoded = NULL;
    if (encodedLen != NULL)
        *encodedLen = 0;
    if (object == NULL || object->payload == NULL || fieldLen == 0 ||
        fieldLen > 0xff)
    {
        return false;
    }
    while (pos < object->payloadLen)
    {
        u32 nameLen = object->payload[pos++];
        u16 valueLen = 0;
        if (nameLen > object->payloadLen - pos ||
            object->payloadLen - pos - nameLen < 2)
        {
            return false;
        }
        if (nameLen == fieldLen &&
            memcmp(object->payload + pos, field, fieldLen) == 0)
        {
            pos += nameLen;
            valueLen = (u16)(((u16)object->payload[pos] << 8) |
                             object->payload[pos + 1]);
            pos += 2;
            if (valueLen > object->payloadLen - pos)
                return false;
            if (encoded != NULL)
                *encoded = object->payload + pos;
            if (encodedLen != NULL)
                *encodedLen = valueLen;
            return true;
        }
        pos += nameLen;
        valueLen = (u16)(((u16)object->payload[pos] << 8) |
                         object->payload[pos + 1]);
        pos += 2;
        if (valueLen > object->payloadLen - pos)
            return false;
        pos += valueLen;
    }
    return false;
}

static bool vm_net_mock_response_object_string(
    const vm_net_mock_response_object *object,
    const char *field,
    char *value,
    size_t valueCap)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;
    u16 textLen = 0;
    size_t copyLen = 0;

    if (value == NULL || valueCap == 0)
        return false;
    value[0] = 0;
    if (!vm_net_mock_response_object_field(object, field, &encoded,
                                           &encodedLen) ||
        encodedLen < 2)
    {
        return false;
    }
    textLen = (u16)(((u16)encoded[0] << 8) | encoded[1]);
    if ((u32)textLen + 2u != encodedLen)
        return false;
    copyLen = SDL_min((size_t)textLen, valueCap - 1);
    while (copyLen > 0 && encoded[2 + copyLen - 1] == 0)
        --copyLen;
    memcpy(value, encoded + 2, copyLen);
    value[copyLen] = 0;
    return value[0] != 0;
}

static bool vm_net_mock_response_object_posinfo(
    const vm_net_mock_response_object *object,
    u16 *x,
    u16 *y)
{
    const u8 *encoded = NULL;
    u16 encodedLen = 0;

    if (!vm_net_mock_response_object_field(object, "posinfo", &encoded,
                                           &encodedLen) ||
        encodedLen != 8 || encoded[0] != 0 || encoded[1] != 2 ||
        encoded[4] != 0 || encoded[5] != 2)
    {
        return false;
    }
    if (x != NULL)
        *x = (u16)(((u16)encoded[2] << 8) | encoded[3]);
    if (y != NULL)
        *y = (u16)(((u16)encoded[6] << 8) | encoded[7]);
    return true;
}

/* Capture packet facts while draining the worker queue, but do not mutate the
 * scene lifecycle yet.  Several responses can be queued in one scheduler tick;
 * applying a later 30/2 before the earlier 30/1 guest callback runs destroys
 * their protocol order and disables the same-scene guard. */
static void vm_net_mock_capture_remote_completion(
    const vm_net_mock_async_completion *completion,
    vm_net_remote_observation *observation)
{
    const u8 *response = NULL;
    u32 responseLen = 0;
    u32 packetLen = 0;
    u32 offset = 5;
    u8 objectCount = 0;
    u8 parsedObjects = 0;
    vm_net_mock_response_object object;

    if (observation == NULL)
        return;
    memset(observation, 0, sizeof(*observation));
    if (completion == NULL || completion->response == NULL ||
        completion->responseLen < 5)
    {
        return;
    }
    response = completion->response;
    responseLen = completion->responseLen;
    if (response[0] != 'W' || response[1] != 'T')
        return;
    packetLen = ((u32)response[2] << 8) | response[3];
    if (packetLen < 5 || packetLen > responseLen)
        return;
    objectCount = response[4];

    while (parsedObjects < objectCount &&
           vm_net_mock_next_response_object(response, packetLen, &offset,
                                            &object))
    {
        if (object.major == 1 && object.kind == 30 &&
            (object.subtype == 1 || object.subtype == 2))
        {
            char scene[64];
            u16 x = 0;
            u16 y = 0;
            bool haveScene = vm_net_mock_response_object_string(
                &object, "scene", scene, sizeof(scene));
            bool havePos = vm_net_mock_response_object_posinfo(&object, &x, &y);

            if (haveScene && havePos)
            {
                observation->hasSceneTarget = 1;
                observation->sceneSubtype = object.subtype;
                observation->sceneX = x;
                observation->sceneY = y;
                snprintf(observation->scene, sizeof(observation->scene),
                         "%s", scene);
                if (object.subtype == 2)
                    observation->sceneCompleteAfterCallback = 1;
            }
            else if (object.subtype == 2)
            {
                observation->sceneCompleteAfterCallback = 1;
                if (haveScene)
                    snprintf(observation->scene,
                             sizeof(observation->scene), "%s", scene);
            }
        }
        ++parsedObjects;
    }

    if (completion->requestIsUpdateChunk)
    {
        const u8 *chunk = NULL;
        u16 chunkLen = 0;
        u32 totalSize = 0;
        char payloadName[64];

        payloadName[0] = 0;
        if (vm_net_mock_get_object_u32_field(response, packetLen, "totalsize",
                                             &totalSize) &&
            vm_net_mock_get_object_blob_field(response, packetLen, "data",
                                              &chunk, &chunkLen) &&
            totalSize != 0 && chunkLen != 0 &&
            completion->updateChunkStart <= totalSize &&
            chunkLen <= totalSize - completion->updateChunkStart &&
            completion->updateChunkStart + chunkLen >= totalSize)
        {
            if (!vm_net_mock_get_object_string_field(response, packetLen,
                                                     "name", payloadName,
                                                     sizeof(payloadName)))
            {
                snprintf(payloadName, sizeof(payloadName), "%s",
                         completion->updateChunkName);
            }
            observation->updateComplete = 1;
            snprintf(observation->updateName,
                     sizeof(observation->updateName), "%s", payloadName);
        }
    }
}

static void vm_net_mock_snapshot_remote_completed_target(u32 serial)
{
    if (!g_vm_net_mock_last_scene_change_target_valid || serial == 0 ||
        serial != g_vm_net_mock_last_scene_change_target_serial)
    {
        return;
    }
    g_vm_net_mock_last_completed_scene_change_target =
        g_vm_net_mock_last_scene_change_target;
    g_vm_net_mock_last_completed_scene_change_target.needsSceneDownload = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = true;
    g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;
    g_vm_net_mock_remote_completed_scene_target_serial = serial;
}

/* Apply observations immediately before their own guest callback.  This keeps
 * TCP worker completion order and guest parser order identical even when the
 * worker produced several responses during one emulator frame. */
static u32 vm_net_mock_apply_remote_observation(
    const vm_net_remote_observation *observation)
{
    u32 clearAfterCallbackSerial = 0;
    bool restoredCompletedTarget = false;

    if (observation == NULL)
        return 0;

    if (observation->hasSceneTarget && observation->scene[0] != 0)
    {
        vm_net_mock_scene_change_target target;

        memset(&target, 0, sizeof(target));
        snprintf(target.scene, sizeof(target.scene), "%s",
                 observation->scene);
        target.x = observation->sceneX;
        target.y = observation->sceneY;
        target.mapType = 2;
        target.hasSceEntry = true;
        target.needsSceneDownload = false;

        /* A position-bearing WT30 object starts a new packet-visible target.
         * Do not let a resource completion from the previous target authorize
         * this lifecycle's re-entry. */
        g_vm_net_mock_update_completed_reenter_pending = false;
        g_vm_net_mock_update_completed_name[0] = 0;
        g_vm_net_mock_last_completed_scene_change_target_valid = false;
        g_vm_net_mock_remote_completed_scene_target_serial = 0;
        g_vm_net_mock_last_scene_change_target = target;
        g_vm_net_mock_last_scene_change_target_valid = true;
        ++g_vm_net_mock_last_scene_change_target_serial;
        if (g_vm_net_mock_last_scene_change_target_serial == 0)
            g_vm_net_mock_last_scene_change_target_serial = 1;
        printf("[info][screen] remote_scene_target_apply serial=%u subtype=%u scene=%s pos=(%u,%u) evidence=WT30/%u-immediately-before-guest-callback\n",
               g_vm_net_mock_last_scene_change_target_serial,
               observation->sceneSubtype,
               target.scene,
               target.x,
               target.y,
               observation->sceneSubtype);
    }

    if (observation->sceneCompleteAfterCallback &&
        g_vm_net_mock_last_scene_change_target_valid &&
        (observation->scene[0] == 0 ||
         vm_net_mock_scene_names_equal_loose(
             observation->scene,
             g_vm_net_mock_last_scene_change_target.scene)))
    {
        clearAfterCallbackSerial =
            g_vm_net_mock_last_scene_change_target_serial;
        vm_net_mock_snapshot_remote_completed_target(
            clearAfterCallbackSerial);
        printf("[info][screen] remote_scene_target_complete_pending serial=%u scene=%s action=clear-after-own-callback evidence=WT30/2\n",
               clearAfterCallbackSerial,
               g_vm_net_mock_last_scene_change_target.scene);
    }

    if (observation->updateComplete && observation->updateName[0] != 0)
    {
        if (!g_vm_net_mock_last_scene_change_target_valid &&
            g_vm_net_mock_last_completed_scene_change_target_valid &&
            g_vm_net_mock_remote_completed_scene_target_serial != 0 &&
            g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick <
                VM_NET_MOCK_COMPLETED_SCENE_REUSE_TICKS)
        {
            g_vm_net_mock_last_scene_change_target =
                g_vm_net_mock_last_completed_scene_change_target;
            g_vm_net_mock_last_scene_change_target_valid = true;
            g_vm_net_mock_last_scene_change_target_serial =
                g_vm_net_mock_remote_completed_scene_target_serial;
            g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;
            restoredCompletedTarget = true;
            printf("[info][screen] remote_scene_target_restore serial=%u scene=%s file=%s reason=resource-completion-callback\n",
                   g_vm_net_mock_last_scene_change_target_serial,
                   g_vm_net_mock_last_scene_change_target.scene,
                   observation->updateName);
        }

        if (g_vm_net_mock_last_scene_change_target_valid)
        {
            vm_net_mock_note_update_chunk_complete(observation->updateName);
            if (restoredCompletedTarget)
                clearAfterCallbackSerial =
                    g_vm_net_mock_last_scene_change_target_serial;
            printf("[info][screen] remote_update_complete_apply file=%s serial=%u action=arm-one-scene-reenter immediately-before-guest-callback\n",
                   observation->updateName,
                   g_vm_net_mock_last_scene_change_target_serial);
        }
        else
        {
            printf("[warn][screen] remote_update_complete_unbound file=%s action=no-scene-reenter reason=no-recent-packet-target\n",
                   observation->updateName);
        }
    }

    return clearAfterCallbackSerial;
}

static void vm_net_mock_finish_remote_observation(u32 sceneTargetSerial)
{
    if (sceneTargetSerial == 0 ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        sceneTargetSerial != g_vm_net_mock_last_scene_change_target_serial)
    {
        return;
    }
    printf("[info][screen] remote_scene_target_complete serial=%u scene=%s action=cleared-after-own-guest-callback\n",
           sceneTargetSerial,
           g_vm_net_mock_last_scene_change_target.scene);
    g_vm_net_mock_last_scene_change_target_valid = false;
}

static void *vm_net_mock_async_worker_main(void *unused)
{
    u8 *responseScratch = (u8 *)malloc(sizeof(g_netMockResponse));
    u8 *followupScratch = (u8 *)malloc(sizeof(g_vmNetMockFollowupResponse));
    (void)unused;

    for (;;)
    {
        vm_net_mock_async_job *job = NULL;
        vm_net_mock_async_completion *completion = NULL;
        u32 responseLen = 0;
        u32 responseEventType = 7;
        u32 followupLen = 0;
        bool closeAfterData = false;
        int success = 0;

        pthread_mutex_lock(&g_vmNetMockAsync.mutex);
        while (!g_vmNetMockAsync.stopRequested && g_vmNetMockAsync.jobHead == NULL)
            pthread_cond_wait(&g_vmNetMockAsync.condition, &g_vmNetMockAsync.mutex);
        if (g_vmNetMockAsync.stopRequested)
        {
            pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
            break;
        }
        job = g_vmNetMockAsync.jobHead;
        g_vmNetMockAsync.jobHead = job->next;
        if (g_vmNetMockAsync.jobHead == NULL)
            g_vmNetMockAsync.jobTail = NULL;
        if (g_vmNetMockAsync.queuedJobs != 0)
            --g_vmNetMockAsync.queuedJobs;
        pthread_mutex_unlock(&g_vmNetMockAsync.mutex);

        completion = (vm_net_mock_async_completion *)calloc(1, sizeof(*completion));
        if (completion == NULL || responseScratch == NULL || followupScratch == NULL)
        {
            bool clearScenePoll = job->kind == VM_NET_MOCK_ASYNC_JOB_SCENE_POLL;
            u32 failedGeneration = job->generation;
            free(job);
            vm_net_mock_async_free_completion(completion);
            if (clearScenePoll)
            {
                pthread_mutex_lock(&g_vmNetMockAsync.mutex);
                if (failedGeneration == g_vmNetMockAsync.generation)
                    g_vmNetMockAsync.scenePollOutstanding = false;
                pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
            }
            continue;
        }
        completion->generation = job->generation;
        completion->sequence = job->sequence;
        completion->enqueueMs = job->enqueueMs;
        completion->workerStartMs = SDL_GetTicks();
        completion->connectId = job->connectId;
        completion->kind = job->kind;
        completion->responseEventType = 7;
        completion->followupEventType = 7;
        if (job->kind == VM_NET_MOCK_ASYNC_JOB_DATA &&
            vm_net_mock_is_update_chunk_request(job->request,
                                                job->requestLen))
        {
            completion->requestIsUpdateChunk = true;
            (void)vm_net_mock_get_object_u32_field(job->request,
                                                  job->requestLen,
                                                  "start",
                                                  &completion->updateChunkStart);
            (void)vm_net_mock_get_object_string_field(
                job->request,
                job->requestLen,
                "name",
                completion->updateChunkName,
                sizeof(completion->updateChunkName));
        }

        if (job->kind == VM_NET_MOCK_ASYNC_JOB_SCENE_POLL)
        {
            success = vm_net_mock_remote_scene_sync_poll(responseScratch,
                                                         sizeof(g_netMockResponse),
                                                         &responseLen,
                                                         &responseEventType);
        }
        else
        {
            success = vm_net_mock_remote_request(job->request,
                                                 job->requestLen,
                                                 responseScratch,
                                                 sizeof(g_netMockResponse),
                                                 &responseLen,
                                                 &responseEventType,
                                                 &closeAfterData,
                                                 followupScratch,
                                                 sizeof(g_vmNetMockFollowupResponse),
                                                 &followupLen);
        }
        completion->workerDoneMs = SDL_GetTicks();
        completion->success = success != 0;
        completion->closeAfterData = closeAfterData;
        completion->responseEventType = responseEventType;
        completion->responseLen = responseLen;
        completion->followupLen = followupLen;

        if (completion->success && responseLen != 0)
        {
            completion->response = (u8 *)malloc(responseLen);
            if (completion->response != NULL)
                memcpy(completion->response, responseScratch, responseLen);
            else
                completion->success = false;
        }
        if (completion->success && followupLen != 0)
        {
            completion->followup = (u8 *)malloc(followupLen);
            if (completion->followup != NULL)
                memcpy(completion->followup, followupScratch, followupLen);
            else
                completion->success = false;
        }
        if (!completion->success)
        {
            free(completion->response);
            free(completion->followup);
            completion->response = NULL;
            completion->followup = NULL;
            completion->responseLen = 0;
            completion->followupLen = 0;
        }
        free(job);

        pthread_mutex_lock(&g_vmNetMockAsync.mutex);
        if (g_vmNetMockAsync.stopRequested ||
            completion->generation != g_vmNetMockAsync.generation)
        {
            pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
            vm_net_mock_async_free_completion(completion);
            continue;
        }
        if (g_vmNetMockAsync.completionTail != NULL)
            g_vmNetMockAsync.completionTail->next = completion;
        else
            g_vmNetMockAsync.completionHead = completion;
        g_vmNetMockAsync.completionTail = completion;
        pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
    }

    free(responseScratch);
    free(followupScratch);
    return NULL;
}

static bool vm_net_mock_async_ensure_worker(void)
{
    bool started = false;

    pthread_mutex_lock(&g_vmNetMockAsync.mutex);
    if (!g_vmNetMockAsync.workerStarted && !g_vmNetMockAsync.stopRequested)
    {
        if (pthread_create(&g_vmNetMockAsync.worker, NULL,
                           vm_net_mock_async_worker_main, NULL) == 0)
        {
            g_vmNetMockAsync.workerStarted = true;
            printf("[info][network] net_async_worker started queue_cap=%u transport=per-request-tcp\n",
                   VM_NET_MOCK_ASYNC_QUEUE_MAX);
        }
    }
    started = g_vmNetMockAsync.workerStarted;
    pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
    return started;
}

static bool vm_net_mock_async_enqueue(vm_net_mock_async_job_kind kind,
                                      u32 connectId,
                                      const u8 *request,
                                      u32 requestLen)
{
    vm_net_mock_async_job *job = NULL;

    if (kind == VM_NET_MOCK_ASYNC_JOB_DATA &&
        (request == NULL || requestLen == 0 || requestLen > VM_NET_MOCK_ASYNC_REQUEST_MAX))
    {
        return false;
    }
    if (!vm_net_mock_async_ensure_worker())
        return false;
    job = (vm_net_mock_async_job *)calloc(1, sizeof(*job));
    if (job == NULL)
        return false;

    job->kind = kind;
    job->connectId = connectId;
    job->requestLen = requestLen;
    job->enqueueMs = SDL_GetTicks();
    if (requestLen != 0)
        memcpy(job->request, request, requestLen);

    pthread_mutex_lock(&g_vmNetMockAsync.mutex);
    if (g_vmNetMockAsync.stopRequested ||
        g_vmNetMockAsync.queuedJobs >= VM_NET_MOCK_ASYNC_QUEUE_MAX ||
        (kind == VM_NET_MOCK_ASYNC_JOB_SCENE_POLL &&
         g_vmNetMockAsync.scenePollOutstanding))
    {
        pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
        free(job);
        return false;
    }
    job->generation = g_vmNetMockAsync.generation;
    job->sequence = g_vmNetMockAsync.nextSequence++;
    if (g_vmNetMockAsync.nextSequence == 0)
        g_vmNetMockAsync.nextSequence = 1;
    if (g_vmNetMockAsync.jobTail != NULL)
        g_vmNetMockAsync.jobTail->next = job;
    else
        g_vmNetMockAsync.jobHead = job;
    g_vmNetMockAsync.jobTail = job;
    ++g_vmNetMockAsync.queuedJobs;
    if (kind == VM_NET_MOCK_ASYNC_JOB_SCENE_POLL)
        g_vmNetMockAsync.scenePollOutstanding = true;
    pthread_cond_signal(&g_vmNetMockAsync.condition);
    pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
    return true;
}

static void vm_net_mock_async_drain_completions(void)
{
    static u32 failureLogCount = 0;

    for (;;)
    {
        vm_net_mock_async_completion *completion = NULL;
        vm_net_channel *channel = NULL;
        u32 generation = 0;
        u32 responsePtr = 0;
        u32 nowMs = 0;
        vm_net_remote_observation remoteObservation;

        memset(&remoteObservation, 0, sizeof(remoteObservation));

        pthread_mutex_lock(&g_vmNetMockAsync.mutex);
        completion = g_vmNetMockAsync.completionHead;
        if (completion != NULL)
        {
            g_vmNetMockAsync.completionHead = completion->next;
            if (g_vmNetMockAsync.completionHead == NULL)
                g_vmNetMockAsync.completionTail = NULL;
            if (completion->kind == VM_NET_MOCK_ASYNC_JOB_SCENE_POLL)
                g_vmNetMockAsync.scenePollOutstanding = false;
        }
        generation = g_vmNetMockAsync.generation;
        pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
        if (completion == NULL)
            break;
        if (completion->generation != generation)
        {
            vm_net_mock_async_free_completion(completion);
            continue;
        }
        if (!completion->success)
        {
            if (!g_mockServiceWarnedUnavailable || failureLogCount < 8)
            {
                ++failureLogCount;
                g_mockServiceWarnedUnavailable = 1;
                printf("[warn][mock-service] async request failed target=%s:%u kind=%s pending=game-timeout timeout_ms=%u\n",
                       g_mockServiceHost,
                       g_mockServicePort,
                       completion->kind == VM_NET_MOCK_ASYNC_JOB_SCENE_POLL ? "scene-poll" : "data",
                       (u32)VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS);
            }
            vm_net_mock_async_free_completion(completion);
            continue;
        }
        g_mockServiceWarnedUnavailable = 0;
        if (completion->responseLen == 0)
        {
            vm_net_mock_async_free_completion(completion);
            continue;
        }

        channel = scheduler_find_net_channel(completion->connectId);
        if (channel == NULL || channel->callback == 0)
        {
            vm_net_mock_async_free_completion(completion);
            continue;
        }
        vm_net_mock_capture_remote_completion(completion,
                                              &remoteObservation);
        responsePtr = vm_net_mock_sync_buffer_to_vm(completion->response,
                                                    completion->responseLen);
        if (responsePtr == 0)
        {
            vm_net_mock_async_free_completion(completion);
            continue;
        }
        if (completion->responseLen <= sizeof(g_netMockResponse))
        {
            memcpy(g_netMockResponse, completion->response, completion->responseLen);
            g_netMockResponseLen = completion->responseLen;
            g_netMockResponseOffset = 0;
        }
        g_netDownLinkData += completion->responseLen;
        scheduler_queue_net_event(completion->responseEventType,
                                  responsePtr,
                                  completion->responseLen,
                                  completion->responseLen,
                                  channel->callback,
                                  channel->context);
        if (!scheduler_attach_net_remote_observation(
                completion->responseEventType,
                responsePtr,
                channel->callback,
                channel->context,
                &remoteObservation))
        {
            printf("[warn][screen] remote_observation_attach_failed sequence=%u event=%u resp=%u action=no-early-global-mutation\n",
                   completion->sequence,
                   completion->responseEventType,
                   completion->responseLen);
        }
        nowMs = SDL_GetTicks();
        if (completion->kind == VM_NET_MOCK_ASYNC_JOB_SCENE_POLL)
        {
            printf("[info][network] net_queue_scene_sync_poll connect=%u event=%u resp=%u cb=%08x ctx=%08x async_queue_ms=%u network_ms=%u deliver_ms=%u evidence=service-poll\n",
                   channel->connectId,
                   completion->responseEventType,
                   completion->responseLen,
                   channel->callback,
                   channel->context,
                   completion->workerStartMs - completion->enqueueMs,
                   completion->workerDoneMs - completion->workerStartMs,
                   nowMs - completion->workerDoneMs);
        }
        else
        {
            snprintf(g_netLastHandledSource, sizeof(g_netLastHandledSource),
                     "remote:%s:%u", g_mockServiceHost, g_mockServicePort);
            g_netLastHandledSummary[0] = 0;
            g_netLastHandledResponseLen = completion->responseLen;
            g_netLastHandledValid = 1;
            printf("[info][network] net_queue_data connect=%u event=%u resp=%u source=%s cb=%08x ctx=%08x depth=%u async_queue_ms=%u network_ms=%u deliver_ms=%u\n",
                   completion->connectId,
                   completion->responseEventType,
                   completion->responseLen,
                   g_netLastHandledSource,
                   channel->callback,
                   channel->context,
                   g_netTaskDispatchDepth,
                   completion->workerStartMs - completion->enqueueMs,
                   completion->workerDoneMs - completion->workerStartMs,
                   nowMs - completion->workerDoneMs);
        }
        if (completion->followupLen != 0 && completion->followup != NULL)
        {
            u32 followupPtr = vm_net_mock_sync_buffer_to_vm(completion->followup,
                                                            completion->followupLen);
            if (followupPtr != 0)
            {
                scheduler_queue_net_event(completion->followupEventType,
                                          followupPtr,
                                          completion->followupLen,
                                          completion->followupLen,
                                          channel->callback,
                                          channel->context);
                printf("[info][network] net_queue_followup connect=%u event=%u resp=%u source=%s cb=%08x ctx=%08x depth=%u async=1\n",
                       completion->connectId,
                       completion->followupEventType,
                       completion->followupLen,
                       g_netLastHandledSource,
                       channel->callback,
                       channel->context,
                       g_netTaskDispatchDepth);
            }
        }
        if (completion->closeAfterData)
            scheduler_queue_net_event(9, 0, 0, 0, channel->callback, channel->context);
        vm_net_mock_async_free_completion(completion);
    }
}

static void vm_net_mock_async_reset(void)
{
    vm_net_mock_async_job *job = NULL;
    vm_net_mock_async_completion *completion = NULL;

    pthread_mutex_lock(&g_vmNetMockAsync.mutex);
    ++g_vmNetMockAsync.generation;
    if (g_vmNetMockAsync.generation == 0)
        g_vmNetMockAsync.generation = 1;
    job = g_vmNetMockAsync.jobHead;
    g_vmNetMockAsync.jobHead = NULL;
    g_vmNetMockAsync.jobTail = NULL;
    g_vmNetMockAsync.queuedJobs = 0;
    completion = g_vmNetMockAsync.completionHead;
    g_vmNetMockAsync.completionHead = NULL;
    g_vmNetMockAsync.completionTail = NULL;
    g_vmNetMockAsync.scenePollOutstanding = false;
    pthread_mutex_unlock(&g_vmNetMockAsync.mutex);

    while (job != NULL)
    {
        vm_net_mock_async_job *next = job->next;
        free(job);
        job = next;
    }
    while (completion != NULL)
    {
        vm_net_mock_async_completion *next = completion->next;
        vm_net_mock_async_free_completion(completion);
        completion = next;
    }
}

static void vm_net_mock_async_shutdown(void)
{
    bool joinWorker = false;

    pthread_mutex_lock(&g_vmNetMockAsync.mutex);
    joinWorker = g_vmNetMockAsync.workerStarted;
    g_vmNetMockAsync.stopRequested = true;
    pthread_cond_broadcast(&g_vmNetMockAsync.condition);
    pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
    if (joinWorker)
        pthread_join(g_vmNetMockAsync.worker, NULL);
    vm_net_mock_async_reset();
    pthread_mutex_lock(&g_vmNetMockAsync.mutex);
    g_vmNetMockAsync.workerStarted = false;
    pthread_mutex_unlock(&g_vmNetMockAsync.mutex);
}

static void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen)
{
    u8 request[VM_NET_MOCK_ASYNC_REQUEST_MAX];
    u32 readLen = 0;
    static u32 queueFullLogCount = 0;

    if (dataPtr == 0 || dataLen == 0)
        return;
    readLen = dataLen < sizeof(request) ? dataLen : sizeof(request);
    if (uc_mem_read(MTK, dataPtr, request, readLen) != UC_ERR_OK)
        return;
    if (!vm_net_mock_async_enqueue(VM_NET_MOCK_ASYNC_JOB_DATA,
                                   connectId,
                                   request,
                                   readLen))
    {
        if (queueFullLogCount < 8)
        {
            ++queueFullLogCount;
            printf("[warn][network] net_async_enqueue_failed connect=%u len=%u queue_cap=%u\n",
                   connectId, readLen, VM_NET_MOCK_ASYNC_QUEUE_MAX);
        }
        return;
    }
    g_netUpLinkData += dataLen;
}

static void vm_net_mock_poll_push_if_due(void)
{
    static u32 lastPollTick = 0;
    static u32 pollIntervalTicks = 0;
    vm_net_channel *channel = NULL;

    if (g_mockServiceOnly || g_mockServiceClientId == 0 || Global_R9 == 0)
        return;
    if (pollIntervalTicks == 0)
    {
        pollIntervalTicks = vm_net_mock_env_u8("CBE_SCENE_SYNC_POLL_TICKS", 1);
        if (pollIntervalTicks == 0)
            pollIntervalTicks = 1;
        printf("[info][network] scene_sync_poll cadence ticks=%u interval_ms=%u\n",
               pollIntervalTicks,
               pollIntervalTicks * VM_SCHED_FRAME_MS);
    }
    if (lastPollTick != 0 &&
        g_schedulerTick - lastPollTick < pollIntervalTicks)
    {
        return;
    }
    for (u32 i = 0; i < VM_SCHED_MAX_NET_TASKS; ++i)
    {
        if (g_netChannels[i].active && g_netChannels[i].callback)
        {
            channel = &g_netChannels[i];
            break;
        }
    }
    if (channel == NULL ||
        scheduler_find_pending_net_event(7, channel->callback, channel->context) != NULL)
    {
        return;
    }
    if (vm_net_mock_async_enqueue(VM_NET_MOCK_ASYNC_JOB_SCENE_POLL,
                                  channel->connectId,
                                  NULL,
                                  0))
    {
        lastPollTick = g_schedulerTick;
    }
}

static u32 vm_net_mock_read_data(u32 dst, u32 dstLen)
{
    static u32 s_netReadObserveCount = 0;
    if (dst == 0 || dstLen == 0 || g_netMockResponseOffset >= g_netMockResponseLen)
    {
        if (g_netDebugReadWindow)
        {
            printf("[info][network] net_read_empty dst=%08x ask=%u offset=%u total=%u window=%u\n",
                   dst, dstLen, g_netMockResponseOffset, g_netMockResponseLen,
                   g_netDebugReadWindow);
            --g_netDebugReadWindow;
        }
        return vm_set_call_result(0);
    }

    u32 remain = g_netMockResponseLen - g_netMockResponseOffset;
    u32 copyLen = dstLen < remain ? dstLen : remain;
    uc_mem_write(MTK, dst, g_netMockResponse + g_netMockResponseOffset, copyLen);
    if (g_netDebugReadWindow)
    {
        u32 off = g_netMockResponseOffset;
        printf("[info][network] net_read_data dst=%08x ask=%u copied=%u offset=%u total=%u head=%02x %02x %02x %02x %02x\n",
               dst, dstLen, copyLen, off, g_netMockResponseLen,
               off + 0 < g_netMockResponseLen ? g_netMockResponse[off + 0] : 0,
               off + 1 < g_netMockResponseLen ? g_netMockResponse[off + 1] : 0,
               off + 2 < g_netMockResponseLen ? g_netMockResponse[off + 2] : 0,
               off + 3 < g_netMockResponseLen ? g_netMockResponse[off + 3] : 0,
               off + 4 < g_netMockResponseLen ? g_netMockResponse[off + 4] : 0);
        --g_netDebugReadWindow;
    }
    g_netMockResponseOffset += copyLen;
    if (s_netReadObserveCount < 20)
    {
        ++s_netReadObserveCount;
        vm_autotest_note("net_read dst=%08x ask=%u copied=%u offset=%u total=%u\n",
                         dst, dstLen, copyLen, g_netMockResponseOffset, g_netMockResponseLen);
    }
    return vm_set_call_result(copyLen);
}
