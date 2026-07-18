/*
 * Local HTTP administration server for the Jianghu OL mock service.
 *
 * This implementation is included by mock-server.c after the shared account,
 * role, MySQL and socket helpers have been defined. Keeping it in a separate
 * source file isolates HTTP parsing and page rendering from game protocol code.
 */

#include <ctype.h>

enum
{
    VM_MOCK_ADMIN_REQUEST_MAX = 8192,
    VM_MOCK_ADMIN_RESPONSE_MAX = 131072,
    VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS = 100
};

typedef struct
{
    char *data;
    size_t capacity;
    size_t length;
    bool truncated;
} vm_mock_admin_text;

static void vm_mock_admin_text_init(vm_mock_admin_text *text, char *buffer, size_t capacity)
{
    if (text == NULL)
        return;
    memset(text, 0, sizeof(*text));
    text->data = buffer;
    text->capacity = capacity;
    if (buffer != NULL && capacity > 0)
        buffer[0] = 0;
}

static void vm_mock_admin_text_appendf(vm_mock_admin_text *text, const char *format, ...)
{
    va_list args;
    int written = 0;
    size_t remaining = 0;

    if (text == NULL || text->data == NULL || text->capacity == 0 ||
        text->truncated || format == NULL)
        return;
    if (text->length >= text->capacity - 1)
    {
        text->truncated = true;
        return;
    }
    remaining = text->capacity - text->length;
    va_start(args, format);
    written = vsnprintf(text->data + text->length, remaining, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= remaining)
    {
        text->length = text->capacity - 1;
        text->data[text->length] = 0;
        text->truncated = true;
        return;
    }
    text->length += (size_t)written;
}

static void vm_mock_admin_text_append_html(vm_mock_admin_text *text, const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    while (*p != 0 && text != NULL && !text->truncated)
    {
        switch (*p)
        {
        case '&':
            vm_mock_admin_text_appendf(text, "&amp;");
            break;
        case '<':
            vm_mock_admin_text_appendf(text, "&lt;");
            break;
        case '>':
            vm_mock_admin_text_appendf(text, "&gt;");
            break;
        case '"':
            vm_mock_admin_text_appendf(text, "&quot;");
            break;
        case '\'':
            vm_mock_admin_text_appendf(text, "&#39;");
            break;
        default:
            vm_mock_admin_text_appendf(text, "%c", *p);
            break;
        }
        ++p;
    }
}

static bool vm_mock_admin_url_decode(const char *value, size_t valueLen,
                                     char *out, size_t outCap)
{
    static const char hex[] = "0123456789abcdef";
    size_t outLen = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (value == NULL)
        return false;
    for (size_t i = 0; i < valueLen; ++i)
    {
        unsigned char ch = (unsigned char)value[i];
        if (ch == '+')
        {
            ch = ' ';
        }
        else if (ch == '%')
        {
            const char *hi = NULL;
            const char *lo = NULL;
            if (i + 2 >= valueLen)
                return false;
            hi = strchr(hex, (char)tolower((unsigned char)value[i + 1]));
            lo = strchr(hex, (char)tolower((unsigned char)value[i + 2]));
            if (hi == NULL || lo == NULL)
                return false;
            ch = (unsigned char)(((hi - hex) << 4) | (lo - hex));
            i += 2;
            if (ch == 0)
                return false;
        }
        if (outLen + 1 >= outCap)
            return false;
        out[outLen++] = (char)ch;
    }
    out[outLen] = 0;
    return true;
}

static bool vm_mock_admin_form_value(const char *form, const char *key,
                                     char *out, size_t outCap)
{
    size_t keyLen = key ? strlen(key) : 0;
    const char *cursor = form;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (form == NULL || keyLen == 0)
        return false;
    while (*cursor != 0)
    {
        const char *pairEnd = strchr(cursor, '&');
        const char *equals = strchr(cursor, '=');
        size_t pairLen = pairEnd ? (size_t)(pairEnd - cursor) : strlen(cursor);

        if (equals != NULL && (size_t)(equals - cursor) < pairLen &&
            (size_t)(equals - cursor) == keyLen &&
            memcmp(cursor, key, keyLen) == 0)
        {
            const char *value = equals + 1;
            size_t valueLen = pairLen - (size_t)(value - cursor);
            return vm_mock_admin_url_decode(value, valueLen, out, outCap);
        }
        if (pairEnd == NULL)
            break;
        cursor = pairEnd + 1;
    }
    return false;
}

static void vm_mock_admin_url_encode(const char *value, char *out, size_t outCap)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    size_t pos = 0;

    if (out == NULL || outCap == 0)
        return;
    while (*p != 0 && pos + 1 < outCap)
    {
        unsigned char ch = *p++;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
        {
            out[pos++] = (char)ch;
        }
        else
        {
            if (pos + 3 >= outCap)
                break;
            out[pos++] = '%';
            out[pos++] = hex[ch >> 4];
            out[pos++] = hex[ch & 0x0f];
        }
    }
    out[pos] = 0;
}

static int vm_mock_admin_ascii_ncasecmp(const char *left, const char *right, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        unsigned char a = (unsigned char)tolower((unsigned char)left[i]);
        unsigned char b = (unsigned char)tolower((unsigned char)right[i]);
        if (a != b)
            return (int)a - (int)b;
        if (a == 0)
            return 0;
    }
    return 0;
}

static bool vm_mock_admin_parse_content_length(const char *request, size_t headerLen,
                                               u32 *contentLengthOut)
{
    const char *cursor = request;
    const char *end = request + headerLen;
    const char field[] = "Content-Length:";

    if (contentLengthOut)
        *contentLengthOut = 0;
    while (cursor < end)
    {
        const char *lineEnd = strstr(cursor, "\r\n");
        if (lineEnd == NULL || lineEnd > end)
            lineEnd = end;
        if ((size_t)(lineEnd - cursor) >= sizeof(field) - 1 &&
            vm_mock_admin_ascii_ncasecmp(cursor, field, sizeof(field) - 1) == 0)
        {
            char lengthText[32];
            const char *value = cursor + sizeof(field) - 1;
            size_t valueLen = 0;
            u32 parsed = 0;

            while (value < lineEnd && (*value == ' ' || *value == '\t'))
                ++value;
            valueLen = (size_t)(lineEnd - value);
            if (valueLen == 0 || valueLen >= sizeof(lengthText))
                return false;
            memcpy(lengthText, value, valueLen);
            lengthText[valueLen] = 0;
            if (!vm_net_mock_parse_u32_strict(lengthText, &parsed))
                return false;
            if (contentLengthOut)
                *contentLengthOut = parsed;
            return true;
        }
        if (lineEnd == end)
            break;
        cursor = lineEnd + 2;
    }
    return true;
}

static bool vm_mock_admin_header_value(const char *request, size_t headerLen,
                                       const char *name, char *out, size_t outCap)
{
    const char *cursor = request;
    const char *end = request + headerLen;
    size_t nameLen = name ? strlen(name) : 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (request == NULL || nameLen == 0)
        return false;
    while (cursor < end)
    {
        const char *lineEnd = strstr(cursor, "\r\n");
        const char *value = NULL;
        size_t valueLen = 0;

        if (lineEnd == NULL || lineEnd > end)
            lineEnd = end;
        if ((size_t)(lineEnd - cursor) > nameLen && cursor[nameLen] == ':' &&
            vm_mock_admin_ascii_ncasecmp(cursor, name, nameLen) == 0)
        {
            value = cursor + nameLen + 1;
            while (value < lineEnd && (*value == ' ' || *value == '\t'))
                ++value;
            while (lineEnd > value && (lineEnd[-1] == ' ' || lineEnd[-1] == '\t'))
                --lineEnd;
            valueLen = (size_t)(lineEnd - value);
            if (valueLen == 0 || valueLen >= outCap)
                return false;
            memcpy(out, value, valueLen);
            out[valueLen] = 0;
            return true;
        }
        if (lineEnd == end)
            break;
        cursor = lineEnd + 2;
    }
    return false;
}

static bool vm_mock_admin_request_is_local_origin(const char *request, size_t headerLen)
{
    char host[128];
    char origin[192];
    char loopbackWithPort[64];
    char localhostWithPort[64];
    char originLoopback[96];
    char originLocalhost[96];

    snprintf(loopbackWithPort, sizeof(loopbackWithPort), "127.0.0.1:%u", g_mockAdminPort);
    snprintf(localhostWithPort, sizeof(localhostWithPort), "localhost:%u", g_mockAdminPort);
    if (!vm_mock_admin_header_value(request, headerLen, "Host", host, sizeof(host)) ||
        (strcmp(host, loopbackWithPort) != 0 && strcmp(host, localhostWithPort) != 0 &&
         strcmp(host, "127.0.0.1") != 0 && strcmp(host, "localhost") != 0))
    {
        return false;
    }
    if (!vm_mock_admin_header_value(request, headerLen, "Origin", origin, sizeof(origin)))
        return true;
    snprintf(originLoopback, sizeof(originLoopback), "http://%s", loopbackWithPort);
    snprintf(originLocalhost, sizeof(originLocalhost), "http://%s", localhostWithPort);
    return strcmp(origin, originLoopback) == 0 || strcmp(origin, originLocalhost) == 0;
}

static int vm_mock_admin_send_response(vm_mock_service_socket client,
                                       const char *status,
                                       const char *contentType,
                                       const char *extraHeaders,
                                       const char *body)
{
    char header[1024];
    size_t bodyLen = body ? strlen(body) : 0;
    int headerLen = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "%s\r\n",
        status ? status : "200 OK",
        contentType ? contentType : "text/plain; charset=utf-8",
        (u32)bodyLen,
        extraHeaders ? extraHeaders : "");

    if (headerLen <= 0 || (size_t)headerLen >= sizeof(header))
        return 0;
    if (!vm_mock_service_send_all(client, (const u8 *)header, (u32)headerLen))
        return 0;
    return bodyLen == 0 || vm_mock_service_send_all(client, (const u8 *)body, (u32)bodyLen);
}

static bool vm_mock_admin_account_is_online(const char *accountId)
{
    const vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    while (session != NULL)
    {
        if (session->roleOnline && accountId != NULL &&
            strcmp(session->accountId, accountId) == 0)
            return true;
        session = session->next;
    }
    return false;
}

static void vm_mock_admin_render_page(char *response, size_t responseCap,
                                      const char *query)
{
    vm_mock_admin_text page;
    char selectedAccount[64];
    char status[16];
    char message[256];
    const char *roleError = NULL;
    vm_mock_service_account_state *accountState = NULL;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(selectedAccount, 0, sizeof(selectedAccount));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "account", selectedAccount, sizeof(selectedAccount));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));

    vm_mock_service_account_db_load();
    if ((selectedAccount[0] == 0 || vm_mock_service_account_find_record(selectedAccount) == NULL) &&
        g_vm_mock_service_account_db.accountCount > 0)
    {
        snprintf(selectedAccount, sizeof(selectedAccount), "%s",
                 g_vm_mock_service_account_db.accounts[0].username);
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 后台管理</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1120px;margin:28px auto;padding:0 18px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 14px}"
        ".sub{color:#667085;margin:4px 0 20px}.grid{display:grid;grid-template-columns:240px 1fr;gap:16px}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:18px;box-shadow:0 1px 2px #1018280d}"
        ".accounts{display:flex;flex-direction:column;gap:6px}.account{display:flex;justify-content:space-between;padding:9px 10px;border-radius:7px;color:#344054;text-decoration:none}.account:hover,.account.on{background:#eef4ff;color:#175cd3}"
        ".dot{color:#12b76a}.muted{color:#98a2b3}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        "table{border-collapse:collapse;width:100%}th,td{text-align:left;padding:10px 8px;border-bottom:1px solid #eaecf0;vertical-align:top}th{color:#667085;font-weight:600}"
        "input{width:100%;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}button:hover{background:#1849a9}"
        ".inline{display:flex;gap:7px;margin:0 0 7px}.inline input{min-width:105px}.forms{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:16px}.stack{display:grid;gap:9px}.badge{font-size:12px;background:#eef4ff;color:#175cd3;padding:2px 7px;border-radius:999px}.money{white-space:nowrap}.foot{margin-top:16px;color:#667085;font-size:12px}"
        "@media(max-width:780px){.grid,.forms{grid-template-columns:1fr}.accounts{max-height:220px;overflow:auto}.table-wrap{overflow:auto}}"
        "</style></head><body><main class=\"wrap\"><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">本机管理端口 · 数据直接保存到 MySQL · 普通钱币以铜为基础单位</p><div class=\"grid\">"
        "<aside class=\"card\"><h2>账号（%u）</h2><div class=\"accounts\">",
        g_vm_mock_service_account_db.accountCount);

    for (u32 i = 0; i < g_vm_mock_service_account_db.accountCount; ++i)
    {
        const char *accountId = g_vm_mock_service_account_db.accounts[i].username;
        char encoded[192];
        bool online = vm_mock_admin_account_is_online(accountId);
        vm_mock_admin_url_encode(accountId, encoded, sizeof(encoded));
        vm_mock_admin_text_appendf(&page, "<a class=\"account%s\" href=\"/?account=%s\"><span>",
                                   strcmp(accountId, selectedAccount) == 0 ? " on" : "", encoded);
        vm_mock_admin_text_append_html(&page, accountId);
        vm_mock_admin_text_appendf(&page, "</span><span class=\"%s\">%s</span></a>",
                                   online ? "dot" : "muted", online ? "在线" : "离线");
    }
    if (g_vm_mock_service_account_db.accountCount == 0)
        vm_mock_admin_text_appendf(&page, "<span class=\"muted\">暂无账号</span>");
    vm_mock_admin_text_appendf(&page, "</div></aside><section><div class=\"card\">");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }

    vm_mock_admin_text_appendf(&page, "<h2>角色明细：");
    vm_mock_admin_text_append_html(&page, selectedAccount[0] ? selectedAccount : "未选择");
    vm_mock_admin_text_appendf(&page, "</h2><div class=\"table-wrap\"><table><thead><tr>"
                               "<th>角色</th><th>等级 / 状态</th><th>普通钱币</th><th>W 币</th><th>操作</th>"
                               "</tr></thead><tbody>");

    if (selectedAccount[0] != 0)
        accountState = vm_mock_service_open_account_role_db_for_management(selectedAccount, &roleError);
    if (accountState != NULL)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
            char roleNameUtf8[128];
            u32 gold = role->money / 10000u;
            u32 silver = (role->money / 100u) % 100u;
            u32 copper = role->money % 100u;
            bool active = role->roleId == g_vm_net_mock_role_db.activeRoleId;

            vm_net_mock_gbk_label_to_utf8(role->name[0] ? role->name : "-",
                                          roleNameUtf8, sizeof(roleNameUtf8));
            vm_mock_admin_text_appendf(&page, "<tr><td><strong>");
            vm_mock_admin_text_append_html(&page, roleNameUtf8);
            vm_mock_admin_text_appendf(&page, "</strong><br><span class=\"muted\">ID %u</span></td>", role->roleId);
            vm_mock_admin_text_appendf(&page, "<td>Lv.%u%s</td>", role->level,
                                       active ? " <span class=\"badge\">当前角色</span>" : "");
            vm_mock_admin_text_appendf(&page,
                                       "<td class=\"money\">%u 金 %u 银 %u 铜<br><span class=\"muted\">总计 %u 铜</span></td><td>%u</td><td>",
                                       gold, silver, copper, role->money, role->wcoin);
            vm_mock_admin_text_appendf(&page,
                "<form class=\"inline\" method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"add-money\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input type=\"number\" name=\"amount\" min=\"1\" max=\"4294967295\" placeholder=\"增加铜钱\" required>"
                "<button type=\"submit\">加钱</button></form>", role->roleId);
            vm_mock_admin_text_appendf(&page,
                "<form class=\"inline\" method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"add-wcoin\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input type=\"number\" name=\"amount\" min=\"1\" max=\"4294967295\" placeholder=\"增加 W 币\" required>"
                "<button type=\"submit\">加 W 币</button></form></td></tr>", role->roleId);
        }
        if (g_vm_net_mock_role_db.roleCount == 0)
            vm_mock_admin_text_appendf(&page, "<tr><td colspan=\"5\" class=\"muted\">该账号尚未创建角色</td></tr>");
        vm_mock_service_close_account_role_db_for_management(accountState, true);
    }
    else
    {
        vm_mock_admin_text_appendf(&page, "<tr><td colspan=\"5\" class=\"muted\">");
        vm_mock_admin_text_append_html(&page, selectedAccount[0] ?
                                       (roleError ? roleError : "角色数据不可用") : "请选择账号");
        vm_mock_admin_text_appendf(&page, "</td></tr>");
    }
    vm_mock_admin_text_appendf(&page, "</tbody></table></div></div><div class=\"forms\">"
                               "<div class=\"card\"><h2>创建账号</h2><form class=\"stack\" method=\"post\" action=\"/action\">"
                               "<input type=\"hidden\" name=\"action\" value=\"create-account\">"
                               "<input name=\"account\" maxlength=\"63\" placeholder=\"账号名\" required>"
                               "<input type=\"password\" name=\"password\" maxlength=\"63\" placeholder=\"密码\" required>"
                               "<button type=\"submit\">创建账号</button></form></div>"
                               "<div class=\"card\"><h2>修改密码</h2><form class=\"stack\" method=\"post\" action=\"/action\">"
                               "<input type=\"hidden\" name=\"action\" value=\"set-password\">"
                               "<input name=\"account\" maxlength=\"63\" placeholder=\"账号名\" value=\"");
    vm_mock_admin_text_append_html(&page, selectedAccount);
    vm_mock_admin_text_appendf(&page,
                               "\" required><input type=\"password\" name=\"password\" maxlength=\"63\" placeholder=\"新密码\" required>"
                               "<button type=\"submit\">保存新密码</button></form></div></div>"
                               "<p class=\"foot\">安全限制：后台仅绑定 127.0.0.1；所有请求有长度限制，页面不包含外部脚本。</p>"
                               "</section></div></main></body></html>");

    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><title>响应过大</title><p>后台页面响应超过大小限制。</p>");
    }
}

static void vm_mock_admin_redirect(vm_mock_service_socket client,
                                   const char *account, const char *status,
                                   const char *message)
{
    char accountEncoded[256];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1200];
    char extraHeaders[1400];

    vm_mock_admin_url_encode(account ? account : "", accountEncoded, sizeof(accountEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded, sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded, sizeof(messageEncoded));
    snprintf(location, sizeof(location), "/?account=%s&status=%s&message=%s",
             accountEncoded, statusEncoded, messageEncoded);
    snprintf(extraHeaders, sizeof(extraHeaders), "Location: %s\r\n", location);
    (void)vm_mock_admin_send_response(client, "303 See Other", "text/plain; charset=utf-8",
                                      extraHeaders, "正在返回后台页面。\n");
}

static void vm_mock_admin_handle_action(vm_mock_service_socket client, const char *body)
{
    char action[32];
    char account[64];
    char password[64];
    char role[64];
    char amountText[32];
    const char *error = NULL;
    u32 amount = 0;
    bool ok = false;

    memset(action, 0, sizeof(action));
    memset(account, 0, sizeof(account));
    memset(password, 0, sizeof(password));
    memset(role, 0, sizeof(role));
    memset(amountText, 0, sizeof(amountText));
    if (!vm_mock_admin_form_value(body, "action", action, sizeof(action)) ||
        !vm_mock_admin_form_value(body, "account", account, sizeof(account)))
    {
        vm_mock_admin_redirect(client, "", "error", "请求参数不完整");
        return;
    }

    if (strcmp(action, "create-account") == 0)
    {
        if (!vm_mock_admin_form_value(body, "password", password, sizeof(password)))
            error = "密码不能为空";
        else
            ok = vm_mock_service_account_create_record(account, password, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "账号创建成功" : (error ? error : "账号创建失败"));
        return;
    }
    if (strcmp(action, "set-password") == 0)
    {
        if (!vm_mock_admin_form_value(body, "password", password, sizeof(password)))
            error = "密码不能为空";
        else
            ok = vm_mock_service_account_set_password(account, password, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "密码修改成功" : (error ? error : "密码修改失败"));
        return;
    }
    if (strcmp(action, "add-money") == 0 || strcmp(action, "add-wcoin") == 0)
    {
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "amount", amountText, sizeof(amountText)) ||
            !vm_net_mock_parse_u32_strict(amountText, &amount) || amount == 0)
        {
            vm_mock_admin_redirect(client, account, "error", "金额必须是大于 0 的整数");
            return;
        }
        if (strcmp(action, "add-money") == 0)
            ok = vm_mock_service_account_add_role_money(account, role, amount, NULL, NULL, &error);
        else
            ok = vm_mock_service_account_add_role_wcoin(account, role, amount, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? (strcmp(action, "add-money") == 0 ? "普通钱币增加成功" : "W 币增加成功")
                                  : (error ? error : "余额修改失败"));
        return;
    }

    vm_mock_admin_redirect(client, account, "error", "不支持的管理操作");
}

static int vm_mock_admin_handle_client(vm_mock_service_socket client)
{
    char request[VM_MOCK_ADMIN_REQUEST_MAX + 1];
    char method[12];
    char target[1024];
    char version[16];
    char *headerEnd = NULL;
    char *query = NULL;
    char *body = NULL;
    char *response = NULL;
    size_t received = 0;
    size_t headerLen = 0;
    u32 contentLength = 0;
    int timeoutMs = VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS;

#ifdef _WIN32
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
#else
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
#endif
    memset(request, 0, sizeof(request));
    while (received < VM_MOCK_ADMIN_REQUEST_MAX)
    {
        int rc = recv(client, request + received,
                      (int)(VM_MOCK_ADMIN_REQUEST_MAX - received), 0);
        if (rc <= 0)
            break;
        received += (size_t)rc;
        request[received] = 0;
        headerEnd = strstr(request, "\r\n\r\n");
        if (headerEnd == NULL)
            continue;
        headerLen = (size_t)(headerEnd - request) + 4;
        if (!vm_mock_admin_parse_content_length(request, headerLen, &contentLength) ||
            contentLength > VM_MOCK_ADMIN_REQUEST_MAX - headerLen)
        {
            vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL, "请求长度无效。\n");
            return 0;
        }
        if (received >= headerLen + contentLength)
            break;
    }
    if (headerEnd == NULL || received < headerLen + contentLength)
    {
        vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL, "请求不完整。\n");
        return 0;
    }
    if (!vm_mock_admin_request_is_local_origin(request, headerLen))
    {
        vm_mock_admin_send_response(client, "403 Forbidden", NULL, NULL,
                                    "只允许从本机后台页面访问。\n");
        return 0;
    }
    body = request + headerLen;
    body[contentLength] = 0;
    memset(method, 0, sizeof(method));
    memset(target, 0, sizeof(target));
    memset(version, 0, sizeof(version));
    if (sscanf(request, "%11s %1023s %15s", method, target, version) != 3 ||
        strncmp(version, "HTTP/", 5) != 0)
    {
        vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL, "HTTP 请求格式无效。\n");
        return 0;
    }
    query = strchr(target, '?');
    if (query != NULL)
        *query++ = 0;
    else
        query = "";

    if (strcmp(method, "GET") == 0 && strcmp(target, "/") == 0)
    {
        response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (response == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error", NULL, NULL, "内存不足。\n");
            return 0;
        }
        vm_mock_admin_render_page(response, VM_MOCK_ADMIN_RESPONSE_MAX, query);
        vm_mock_admin_send_response(client, "200 OK", "text/html; charset=utf-8", NULL, response);
        free(response);
        return 1;
    }
    if (strcmp(method, "GET") == 0 && strcmp(target, "/healthz") == 0)
    {
        vm_mock_admin_send_response(client, "200 OK", "application/json; charset=utf-8", NULL,
                                    "{\"ok\":true,\"service\":\"jianghu-admin\"}\n");
        return 1;
    }
    if (strcmp(target, "/action") == 0)
    {
        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n", "只允许 POST。\n");
            return 0;
        }
        vm_mock_admin_handle_action(client, body);
        return 1;
    }
    vm_mock_admin_send_response(client, "404 Not Found", NULL, NULL, "页面不存在。\n");
    return 0;
}

static vm_mock_service_socket vm_mock_admin_open_listener(u16 port)
{
    vm_mock_service_socket server = VM_MOCK_SERVICE_INVALID_SOCKET;
    struct sockaddr_in address;
    int reuse = 1;

    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == VM_MOCK_SERVICE_INVALID_SOCKET)
        return VM_MOCK_SERVICE_INVALID_SOCKET;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 4) != 0)
    {
        vm_mock_service_socket_close(server);
        return VM_MOCK_SERVICE_INVALID_SOCKET;
    }
    return server;
}
