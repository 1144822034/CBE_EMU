/*
 * Local HTTP administration server for the Jianghu OL mock service.
 *
 * This implementation is included by mock-server.c after the shared account,
 * role, MySQL and socket helpers have been defined. Keeping it in a separate
 * source file isolates HTTP parsing and page rendering from game protocol code.
 */

#include <ctype.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

enum
{
    VM_MOCK_ADMIN_REQUEST_MAX = 8192,
    VM_MOCK_ADMIN_RESPONSE_MAX = 524288,
    VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS = 100,
    VM_MOCK_USER_SESSION_MAX = 64
};

#define VM_MOCK_ADMIN_BASE_PATH "/admin-418yz6"
#define VM_MOCK_ADMIN_ROOT_PATH VM_MOCK_ADMIN_BASE_PATH "/"
#define VM_MOCK_ADMIN_LOGIN_PATH VM_MOCK_ADMIN_BASE_PATH "/login"
#define VM_MOCK_ADMIN_LOGOUT_PATH VM_MOCK_ADMIN_BASE_PATH "/logout"
#define VM_MOCK_ADMIN_ACTION_PATH VM_MOCK_ADMIN_BASE_PATH "/action"

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

static bool vm_mock_admin_request_has_allowed_origin(const char *request, size_t headerLen)
{
    char host[128];
    char origin[192];
    char expectedHttpOrigin[192];
    char expectedHttpsOrigin[192];
    const char *cursor = NULL;

    if (!vm_mock_admin_header_value(request, headerLen, "Host", host, sizeof(host)))
        return false;
    for (cursor = host; *cursor != 0; ++cursor)
    {
        unsigned char ch = (unsigned char)*cursor;
        if (ch <= 0x20 || ch >= 0x7f || ch == '/' || ch == '\\' ||
            ch == '?' || ch == '#' || ch == '@')
        {
            return false;
        }
    }
    if (!vm_mock_admin_header_value(request, headerLen, "Origin", origin, sizeof(origin)))
        return true;
    snprintf(expectedHttpOrigin, sizeof(expectedHttpOrigin), "http://%s", host);
    snprintf(expectedHttpsOrigin, sizeof(expectedHttpsOrigin), "https://%s", host);
    return strcmp(origin, expectedHttpOrigin) == 0 || strcmp(origin, expectedHttpsOrigin) == 0;
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
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; img-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
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

static int vm_mock_admin_send_binary_response(vm_mock_service_socket client,
                                              const char *status,
                                              const char *contentType,
                                              const u8 *body,
                                              u32 bodyLen)
{
    char header[1024];
    int headerLen = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; img-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "\r\n",
        status ? status : "200 OK",
        contentType ? contentType : "application/octet-stream",
        bodyLen);

    if (headerLen <= 0 || (size_t)headerLen >= sizeof(header))
        return 0;
    if (!vm_mock_service_send_all(client, (const u8 *)header, (u32)headerLen))
        return 0;
    return bodyLen == 0 ||
           (body != NULL && vm_mock_service_send_all(client, body, bodyLen));
}

enum
{
    VM_MOCK_ADMIN_SCENE_FILE_MAX = 512,
    VM_MOCK_ADMIN_ACTOR_FILE_MAX = 1024,
    VM_MOCK_ADMIN_XSE_FILE_MAX = 512,
    VM_MOCK_ADMIN_UPDATE_FILE_MAX = 1400,
    VM_MOCK_ADMIN_SHOP_PAGE_SIZE = 50
};

typedef struct
{
    char name[64];
    uint64_t size;
} vm_mock_admin_scene_file;

static char g_vm_mock_admin_session_token[40];

typedef struct
{
    bool active;
    char token[40];
    char accountId[64];
    u32 lastUsedTick;
} vm_mock_user_session;

typedef struct
{
    bool found;
    bool invalid;
    char password[65];
    u32 failedAttempts;
    bool locked;
} vm_mock_admin_login_config;

static vm_mock_user_session g_vm_mock_user_sessions[VM_MOCK_USER_SESSION_MAX];
static u32 g_vm_mock_user_session_serial = 0;

static const char g_vm_mock_admin_script[] =
    "(()=>{"
    "const keep=(selector,key)=>{"
    "const box=document.querySelector(selector);if(!box)return;"
    "const restore=()=>{const value=sessionStorage.getItem(key);"
    "if(value!==null){const top=parseInt(value,10);if(Number.isFinite(top))box.scrollTop=top;}};"
    "const save=()=>sessionStorage.setItem(key,String(box.scrollTop));"
    "restore();box.addEventListener('scroll',save,{passive:true});"
    "box.addEventListener('click',save);window.addEventListener('load',restore,{once:true});};"
    "const setupItemFilter=()=>{"
    "const category=document.querySelector('#item-category');"
    "const item=document.querySelector('#item-select');if(!category||!item)return;"
    "const apply=(reset)=>{const wanted=category.value;"
    "for(const option of item.options){if(!option.value)continue;"
    "const show=wanted==='all'||option.dataset.category===wanted;"
    "option.hidden=!show;option.disabled=!show;option.style.display=show?'':'none';}"
    "if(reset)item.value='';};"
    "category.addEventListener('change',()=>apply(true));apply(false);};"
    "const setupUpdateFilter=()=>{"
    "const input=document.querySelector('#update-resource-filter');"
    "const select=document.querySelector('#update-resource-select');if(!input||!select)return;"
    "const apply=()=>{const wanted=input.value.trim().toLowerCase();"
    "for(const option of select.options){if(!option.value)continue;"
    "const show=!wanted||option.textContent.toLowerCase().includes(wanted);"
    "option.hidden=!show;option.disabled=!show;option.style.display=show?'':'none';}};"
    "input.addEventListener('input',apply);apply();};"
    "document.addEventListener('DOMContentLoaded',()=>{"
    "keep('.accounts','cbe-admin-accounts-scroll');"
    "keep('.scene-list','cbe-admin-scenes-scroll');"
    "keep('.shop-list','cbe-admin-shop-scroll');"
    "keep('.update-left','cbe-admin-update-left-scroll');"
    "keep('.update-right','cbe-admin-update-right-scroll');"
    "setupItemFilter();setupUpdateFilter();});"
    "})();";

static void vm_mock_admin_ensure_session_token(void)
{
    u32 value = 0;
    u32 words[4];

    if (g_vm_mock_admin_session_token[0] != 0)
        return;
    value = (u32)time(NULL) ^ (u32)getpid() ^
            (u32)(uintptr_t)&g_vm_mock_admin_session_token ^ scheduler_get_tick_ms();
    if (value == 0)
        value = 0x6a09e667u;
    for (u32 i = 0; i < 4; ++i)
    {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        value += 0x9e3779b9u + i * 0x85ebca6bu;
        words[i] = value;
    }
    snprintf(g_vm_mock_admin_session_token,
             sizeof(g_vm_mock_admin_session_token),
             "%08x%08x%08x%08x",
             words[0], words[1], words[2], words[3]);
}

static bool vm_mock_admin_request_is_authenticated(const char *request,
                                                    size_t headerLen)
{
    char cookie[1024];
    const char key[] = "cbe_admin=";
    const char *cursor = NULL;

    vm_mock_admin_ensure_session_token();
    if (!vm_mock_admin_header_value(request, headerLen, "Cookie",
                                    cookie, sizeof(cookie)))
    {
        return false;
    }
    cursor = cookie;
    while ((cursor = strstr(cursor, key)) != NULL)
    {
        const char *value = cursor + sizeof(key) - 1;
        size_t valueLen = strcspn(value, "; ");
        if (valueLen == strlen(g_vm_mock_admin_session_token) &&
            memcmp(value, g_vm_mock_admin_session_token, valueLen) == 0)
        {
            return true;
        }
        cursor = value + valueLen;
    }
    return false;
}

static bool vm_mock_admin_login_config_row(void *contextValue,
                                           unsigned int columnCount,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_admin_login_config *config =
        (vm_mock_admin_login_config *)contextValue;
    size_t passwordLen = 0;

    if (config == NULL || config->found || columnCount != 3 ||
        values[0] == NULL || values[1] == NULL || values[2] == NULL ||
        !vm_mysql_hex_decode(values[0], lengths[0], config->password,
                             sizeof(config->password) - 1, &passwordLen) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1],
                                 &config->failedAttempts))
    {
        if (config != NULL)
            config->invalid = true;
        return true;
    }
    u32 locked = 0;
    if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &locked) || locked > 1)
    {
        config->invalid = true;
        return true;
    }
    config->password[passwordLen] = 0;
    config->locked = locked != 0;
    config->found = true;
    return true;
}

static bool vm_mock_admin_load_login_config(vm_mock_admin_login_config *config)
{
    if (config == NULL)
        return false;
    memset(config, 0, sizeof(*config));
    if (!vm_mysql_query(
            "SELECT HEX(password_value),failed_attempts,locked "
            "FROM server_admin_config WHERE config_id=1",
            vm_mock_admin_login_config_row, config) ||
        config->invalid || !config->found || config->password[0] == 0)
    {
        return false;
    }
    return true;
}

static bool vm_mock_admin_verify_login_password(const char *password,
                                                const char **messageOut)
{
    vm_mock_admin_login_config config;
    bool matches = false;

    if (messageOut)
        *messageOut = "后台登录配置不可用，请检查 MySQL";
    if (!vm_mock_admin_load_login_config(&config))
    {
        printf("[error][admin] login_config_load_failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (config.locked)
    {
        if (messageOut)
            *messageOut = "后台已锁定，请先在 MySQL 中解锁";
        printf("[warn][admin] login_rejected reason=locked failed_attempts=%u\n",
               config.failedAttempts);
        return false;
    }
    matches = password != NULL && strcmp(config.password, password) == 0;
    memset(config.password, 0, sizeof(config.password));
    if (matches)
    {
        if (!vm_mysql_exec(
                "UPDATE server_admin_config SET failed_attempts=0 "
                "WHERE config_id=1 AND locked=0"))
        {
            if (messageOut)
                *messageOut = "后台登录状态无法保存，请检查 MySQL";
            printf("[error][admin] login_reset_failed error=%s\n",
                   vm_mysql_last_error());
            return false;
        }
        if (messageOut)
            *messageOut = "ok";
        printf("[info][admin] login_success failed_attempts_reset=1\n");
        return true;
    }
    if (!vm_mysql_exec(
            "UPDATE server_admin_config "
            "SET locked=IF(failed_attempts+1>=5,1,locked),"
            "failed_attempts=LEAST(failed_attempts+1,5) "
            "WHERE config_id=1 AND locked=0"))
    {
        if (messageOut)
            *messageOut = "后台登录状态无法保存，请检查 MySQL";
        printf("[error][admin] login_failure_store_failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (!vm_mock_admin_load_login_config(&config))
    {
        if (messageOut)
            *messageOut = "后台登录配置不可用，请检查 MySQL";
        return false;
    }
    if (messageOut)
        *messageOut = config.locked ?
            "密码连续错误 5 次，后台已锁定" : "管理密码错误";
    printf("[warn][admin] login_failure failed_attempts=%u locked=%u\n",
           config.failedAttempts, config.locked ? 1u : 0u);
    memset(config.password, 0, sizeof(config.password));
    return false;
}

static bool vm_mock_web_cookie_value(const char *request, size_t headerLen,
                                     const char *name, char *out,
                                     size_t outCap)
{
    char cookie[1024];
    char key[96];
    const char *cursor = NULL;

    if (out == NULL || outCap == 0 || name == NULL || name[0] == 0)
        return false;
    out[0] = 0;
    snprintf(key, sizeof(key), "%s=", name);
    if (!vm_mock_admin_header_value(request, headerLen, "Cookie",
                                    cookie, sizeof(cookie)))
        return false;
    cursor = cookie;
    while ((cursor = strstr(cursor, key)) != NULL)
    {
        const char *value = cursor + strlen(key);
        size_t valueLen = strcspn(value, "; ");
        if (valueLen > 0 && valueLen < outCap)
        {
            memcpy(out, value, valueLen);
            out[valueLen] = 0;
            return true;
        }
        cursor = value + valueLen;
    }
    return false;
}

static void vm_mock_user_make_session_token(const char *accountId,
                                            char *out, size_t outCap)
{
    u32 value = (u32)time(NULL) ^ (u32)getpid() ^ scheduler_get_tick_ms() ^
                ++g_vm_mock_user_session_serial ^
                (u32)(uintptr_t)&g_vm_mock_user_sessions;
    u32 words[4];

    for (const unsigned char *p = (const unsigned char *)(accountId ? accountId : "");
         *p != 0; ++p)
        value = (value ^ *p) * 16777619u;
    if (value == 0)
        value = 0xbb67ae85u;
    for (u32 i = 0; i < 4; ++i)
    {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        value += 0x9e3779b9u + i * 0x85ebca6bu;
        words[i] = value;
    }
    snprintf(out, outCap, "%08x%08x%08x%08x",
             words[0], words[1], words[2], words[3]);
}

static vm_mock_user_session *vm_mock_user_request_session(const char *request,
                                                          size_t headerLen)
{
    char token[40];

    memset(token, 0, sizeof(token));
    if (!vm_mock_web_cookie_value(request, headerLen, "cbe_user",
                                  token, sizeof(token)))
        return NULL;
    for (u32 i = 0; i < VM_MOCK_USER_SESSION_MAX; ++i)
    {
        vm_mock_user_session *session = &g_vm_mock_user_sessions[i];
        if (session->active && strcmp(session->token, token) == 0)
        {
            if (vm_mock_service_account_find_record(session->accountId) == NULL)
            {
                memset(session, 0, sizeof(*session));
                return NULL;
            }
            session->lastUsedTick = scheduler_get_tick_ms();
            return session;
        }
    }
    return NULL;
}

static vm_mock_user_session *vm_mock_user_issue_session(const char *accountId)
{
    vm_mock_user_session *selected = NULL;

    for (u32 i = 0; i < VM_MOCK_USER_SESSION_MAX; ++i)
    {
        if (!g_vm_mock_user_sessions[i].active)
        {
            selected = &g_vm_mock_user_sessions[i];
            break;
        }
        if (selected == NULL ||
            g_vm_mock_user_sessions[i].lastUsedTick < selected->lastUsedTick)
            selected = &g_vm_mock_user_sessions[i];
    }
    if (selected == NULL)
        return NULL;
    memset(selected, 0, sizeof(*selected));
    selected->active = true;
    snprintf(selected->accountId, sizeof(selected->accountId), "%s", accountId);
    vm_mock_user_make_session_token(accountId, selected->token,
                                    sizeof(selected->token));
    selected->lastUsedTick = scheduler_get_tick_ms();
    return selected;
}

static void vm_mock_user_clear_request_session(const char *request,
                                               size_t headerLen)
{
    vm_mock_user_session *session =
        vm_mock_user_request_session(request, headerLen);
    if (session != NULL)
        memset(session, 0, sizeof(*session));
}

static bool vm_mock_user_valid_registration(const char *account,
                                            const char *password,
                                            const char **messageOut)
{
    size_t accountLen = account ? strlen(account) : 0;
    size_t passwordLen = password ? strlen(password) : 0;

    if (accountLen < 4 || accountLen > 32)
    {
        if (messageOut)
            *messageOut = "账号名长度须为 4 至 32 个字符";
        return false;
    }
    for (size_t i = 0; i < accountLen; ++i)
    {
        unsigned char ch = (unsigned char)account[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_'))
        {
            if (messageOut)
                *messageOut = "账号名只能包含字母、数字和下划线";
            return false;
        }
    }
    if (passwordLen < 6 || passwordLen > 63)
    {
        if (messageOut)
            *messageOut = "密码长度须为 6 至 63 个字符";
        return false;
    }
    return true;
}

static bool vm_mock_admin_prefix_page_routes(char *html, size_t htmlCap)
{
    static const char *needles[] = { "href=\"/", "action=\"/", "src=\"/" };
    static const char *replacements[] = {
        "href=\"" VM_MOCK_ADMIN_ROOT_PATH,
        "action=\"" VM_MOCK_ADMIN_ROOT_PATH,
        "src=\"" VM_MOCK_ADMIN_ROOT_PATH
    };
    char *rewritten = NULL;
    size_t sourceLen = 0;
    size_t sourcePos = 0;
    size_t targetPos = 0;

    if (html == NULL || htmlCap == 0)
        return false;
    sourceLen = strlen(html);
    rewritten = (char *)malloc(htmlCap);
    if (rewritten == NULL)
        return false;
    while (sourcePos < sourceLen)
    {
        bool replaced = false;
        for (u32 i = 0; i < sizeof(needles) / sizeof(needles[0]); ++i)
        {
            size_t needleLen = strlen(needles[i]);
            size_t replacementLen = strlen(replacements[i]);
            if (sourcePos + needleLen <= sourceLen &&
                memcmp(html + sourcePos, needles[i], needleLen) == 0)
            {
                if (targetPos + replacementLen >= htmlCap)
                    goto fail;
                memcpy(rewritten + targetPos, replacements[i], replacementLen);
                targetPos += replacementLen;
                sourcePos += needleLen;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            if (targetPos + 1 >= htmlCap)
                goto fail;
            rewritten[targetPos++] = html[sourcePos++];
        }
    }
    rewritten[targetPos] = 0;
    memcpy(html, rewritten, targetPos + 1);
    free(rewritten);
    return true;

fail:
    free(rewritten);
    return false;
}

static void vm_mock_admin_send_location(vm_mock_service_socket client,
                                        const char *location,
                                        const char *cookieHeader)
{
    char extraHeaders[1024];

    snprintf(extraHeaders, sizeof(extraHeaders), "%sLocation: %s\r\n",
             cookieHeader ? cookieHeader : "",
             location && location[0] ? location : "/");
    (void)vm_mock_admin_send_response(client, "303 See Other",
                                      "text/plain; charset=utf-8",
                                      extraHeaders,
                                      "正在跳转。\n");
}

static void vm_mock_admin_render_login(char *response, size_t responseCap,
                                       const char *error)
{
    vm_mock_admin_text page;

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 后台登录</title><style>"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".card{width:min(380px,calc(100vw - 32px));background:#fff;border:1px solid #e4e7ec;border-radius:12px;padding:26px;box-shadow:0 8px 30px #10182814}"
        "h1{font-size:22px;margin:0 0 6px}.sub{color:#667085;margin:0 0 20px}.error{padding:9px 11px;margin-bottom:12px;border-radius:6px;background:#fef3f2;color:#b42318}"
        "form{display:grid;gap:11px}input{width:100%;border:1px solid #d0d5dd;border-radius:7px;padding:10px 11px;font-size:15px}button{border:0;border-radius:7px;padding:10px 12px;background:#175cd3;color:#fff;cursor:pointer}"
        "</style></head><body><main class=\"card\"><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">请输入管理密码后继续</p>");
    if (error != NULL && error[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"error\">");
        vm_mock_admin_text_append_html(&page, error);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page,
        "<form method=\"post\" action=\"/login\">"
        "<input type=\"password\" name=\"password\" autocomplete=\"current-password\" placeholder=\"管理密码\" autofocus required>"
        "<button type=\"submit\">登录</button></form></main></body></html>");
}

static int vm_mock_admin_scene_file_compare(const void *leftValue,
                                             const void *rightValue)
{
    const vm_mock_admin_scene_file *left =
        (const vm_mock_admin_scene_file *)leftValue;
    const vm_mock_admin_scene_file *right =
        (const vm_mock_admin_scene_file *)rightValue;
    return strcmp(left->name, right->name);
}

static bool vm_mock_admin_text_is_valid_utf8(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (cursor == NULL)
        return false;
    while (*cursor != 0)
    {
        if (*cursor < 0x80)
        {
            ++cursor;
            continue;
        }
        if (*cursor >= 0xc2 && *cursor <= 0xdf &&
            cursor[1] >= 0x80 && cursor[1] <= 0xbf)
        {
            cursor += 2;
            continue;
        }
        if (*cursor == 0xe0 &&
            cursor[1] >= 0xa0 && cursor[1] <= 0xbf &&
            cursor[2] >= 0x80 && cursor[2] <= 0xbf)
        {
            cursor += 3;
            continue;
        }
        if (((*cursor >= 0xe1 && *cursor <= 0xec) ||
             (*cursor >= 0xee && *cursor <= 0xef)) &&
            cursor[1] >= 0x80 && cursor[1] <= 0xbf &&
            cursor[2] >= 0x80 && cursor[2] <= 0xbf)
        {
            cursor += 3;
            continue;
        }
        if (*cursor == 0xed &&
            cursor[1] >= 0x80 && cursor[1] <= 0x9f &&
            cursor[2] >= 0x80 && cursor[2] <= 0xbf)
        {
            cursor += 3;
            continue;
        }
        if (*cursor == 0xf0 &&
            cursor[1] >= 0x90 && cursor[1] <= 0xbf &&
            cursor[2] >= 0x80 && cursor[2] <= 0xbf &&
            cursor[3] >= 0x80 && cursor[3] <= 0xbf)
        {
            cursor += 4;
            continue;
        }
        if (*cursor >= 0xf1 && *cursor <= 0xf3 &&
            cursor[1] >= 0x80 && cursor[1] <= 0xbf &&
            cursor[2] >= 0x80 && cursor[2] <= 0xbf &&
            cursor[3] >= 0x80 && cursor[3] <= 0xbf)
        {
            cursor += 4;
            continue;
        }
        if (*cursor == 0xf4 &&
            cursor[1] >= 0x80 && cursor[1] <= 0x8f &&
            cursor[2] >= 0x80 && cursor[2] <= 0xbf &&
            cursor[3] >= 0x80 && cursor[3] <= 0xbf)
        {
            cursor += 4;
            continue;
        }
        return false;
    }
    return true;
}

/* Resource names inside packets/SCE/XSE and the server state use GBK.  A
 * normal Linux filesystem exposes names as UTF-8, while older deployments may
 * still contain raw GBK filename bytes.  Normalize only at this boundary so
 * the rest of the game server keeps using its established GBK keys. */
static bool vm_mock_admin_host_resource_name_to_game(const char *hostName,
                                                     char *gameName,
                                                     size_t gameNameCap)
{
    if (gameName == NULL || gameNameCap == 0)
        return false;
    gameName[0] = 0;
    if (hostName == NULL || hostName[0] == 0)
        return false;
#ifdef CBE_HOST_UTF8_PATHS
    if (vm_mock_admin_text_is_valid_utf8(hostName))
    {
        char roundTrip[256];
        memset(roundTrip, 0, sizeof(roundTrip));
        utf8_to_gbk((u8 *)hostName, (u8 *)gameName, gameNameCap);
        if (gameName[0] == 0)
            return false;
        gbk_to_utf8((u8 *)gameName, (u8 *)roundTrip, sizeof(roundTrip));
        if (strcmp(roundTrip, hostName) != 0)
        {
            gameName[0] = 0;
            return false;
        }
        return true;
    }
#endif
    if (strlen(hostName) >= gameNameCap)
        return false;
    snprintf(gameName, gameNameCap, "%s", hostName);
    return true;
}

static u32 vm_mock_admin_collect_scene_files(vm_mock_admin_scene_file *files,
                                             u32 fileCap)
{
    u32 count = 0;

    if (files == NULL || fileCap == 0)
        return 0;
    memset(files, 0, sizeof(*files) * fileCap);
#ifdef _WIN32
    {
        static const char *patterns[] = {
            "../web/fs/JHOnlineData/*.sce",
            "web/fs/JHOnlineData/*.sce"
        };
        WIN32_FIND_DATAA found;
        HANDLE search = INVALID_HANDLE_VALUE;

        for (u32 patternIndex = 0;
             patternIndex < sizeof(patterns) / sizeof(patterns[0]);
             ++patternIndex)
        {
            search = FindFirstFileA(patterns[patternIndex], &found);
            if (search == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                size_t nameLen = strlen(found.cFileName);
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                    nameLen == 0 || nameLen >= sizeof(files[0].name) ||
                    !vm_net_mock_str_ends_with(found.cFileName, ".sce"))
                {
                    continue;
                }
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         found.cFileName);
                files[count].size =
                    ((uint64_t)found.nFileSizeHigh << 32) | found.nFileSizeLow;
                ++count;
            } while (count < fileCap && FindNextFileA(search, &found));
            FindClose(search);
            break;
        }
    }
#else
    {
        const char *directories[] = {
            g_vm_net_mock_resource_dir[0] ? g_vm_net_mock_resource_dir : NULL,
            "../web/fs/JHOnlineData",
            "web/fs/JHOnlineData"
        };
        for (u32 directoryIndex = 0;
             directoryIndex < sizeof(directories) / sizeof(directories[0]);
             ++directoryIndex)
        {
            if (directories[directoryIndex] == NULL)
                continue;
            DIR *directory = opendir(directories[directoryIndex]);
            struct dirent *entry = NULL;
            if (directory == NULL)
                continue;
            while (count < fileCap && (entry = readdir(directory)) != NULL)
            {
                char path[1400];
                char gameName[sizeof(files[0].name)];
                struct stat info;
                memset(gameName, 0, sizeof(gameName));
                if (!vm_net_mock_str_ends_with(entry->d_name, ".sce") ||
                    !vm_mock_admin_host_resource_name_to_game(
                        entry->d_name, gameName, sizeof(gameName)))
                    continue;
                snprintf(path, sizeof(path), "%s/%s", directories[directoryIndex],
                         entry->d_name);
                if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
                    continue;
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         gameName);
                files[count].size = (uint64_t)info.st_size;
                ++count;
            }
            closedir(directory);
            break;
        }
    }
#endif
    if (count > 1)
        qsort(files, count, sizeof(files[0]), vm_mock_admin_scene_file_compare);
    return count;
}

static u32 vm_mock_admin_collect_actor_files(vm_mock_admin_scene_file *files,
                                             u32 fileCap)
{
    u32 count = 0;

    if (files == NULL || fileCap == 0)
        return 0;
    memset(files, 0, sizeof(*files) * fileCap);
#ifdef _WIN32
    {
        char configuredPattern[1200];
        const char *patterns[] = {
            NULL,
            "../web/fs/JHOnlineData/*.actor",
            "web/fs/JHOnlineData/*.actor"
        };
        WIN32_FIND_DATAA found;
        HANDLE search = INVALID_HANDLE_VALUE;

        memset(configuredPattern, 0, sizeof(configuredPattern));
        if (g_vm_net_mock_resource_dir[0] != 0)
        {
            size_t dirLen = strlen(g_vm_net_mock_resource_dir);
            if (snprintf(configuredPattern, sizeof(configuredPattern),
                         "%s%s*.actor", g_vm_net_mock_resource_dir,
                         (dirLen != 0 &&
                          (g_vm_net_mock_resource_dir[dirLen - 1] == '/' ||
                           g_vm_net_mock_resource_dir[dirLen - 1] == '\\'))
                             ? ""
                             : "/") < (int)sizeof(configuredPattern))
            {
                patterns[0] = configuredPattern;
            }
        }

        for (u32 patternIndex = 0;
             patternIndex < sizeof(patterns) / sizeof(patterns[0]);
             ++patternIndex)
        {
            if (patterns[patternIndex] == NULL)
                continue;
            search = FindFirstFileA(patterns[patternIndex], &found);
            if (search == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                size_t nameLen = strlen(found.cFileName);
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                    nameLen == 0 || nameLen >= sizeof(files[0].name) ||
                    !vm_net_mock_str_ends_with(found.cFileName, ".actor"))
                {
                    continue;
                }
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         found.cFileName);
                files[count].size =
                    ((uint64_t)found.nFileSizeHigh << 32) | found.nFileSizeLow;
                ++count;
            } while (count < fileCap && FindNextFileA(search, &found));
            FindClose(search);
            break;
        }
    }
#else
    {
        const char *directories[] = {
            g_vm_net_mock_resource_dir[0] ? g_vm_net_mock_resource_dir : NULL,
            "../web/fs/JHOnlineData",
            "web/fs/JHOnlineData"
        };
        for (u32 directoryIndex = 0;
             directoryIndex < sizeof(directories) / sizeof(directories[0]);
             ++directoryIndex)
        {
            if (directories[directoryIndex] == NULL)
                continue;
            DIR *directory = opendir(directories[directoryIndex]);
            struct dirent *entry = NULL;
            if (directory == NULL)
                continue;
            while (count < fileCap && (entry = readdir(directory)) != NULL)
            {
                char path[1400];
                struct stat info;
                size_t nameLen = strlen(entry->d_name);
                if (nameLen == 0 || nameLen >= sizeof(files[0].name) ||
                    !vm_net_mock_str_ends_with(entry->d_name, ".actor"))
                {
                    continue;
                }
                snprintf(path, sizeof(path), "%s/%s",
                         directories[directoryIndex], entry->d_name);
                if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
                    continue;
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         entry->d_name);
                files[count].size = (uint64_t)info.st_size;
                ++count;
            }
            closedir(directory);
            break;
        }
    }
#endif
    if (count > 1)
        qsort(files, count, sizeof(files[0]), vm_mock_admin_scene_file_compare);
    return count;
}

static u32 vm_mock_admin_collect_xse_files(vm_mock_admin_scene_file *files,
                                           u32 fileCap)
{
    u32 count = 0;

    if (files == NULL || fileCap == 0)
        return 0;
    memset(files, 0, sizeof(*files) * fileCap);
#ifdef _WIN32
    {
        static const char *patterns[] = {
            "../web/fs/JHOnlineData/*.xse",
            "web/fs/JHOnlineData/*.xse"
        };
        WIN32_FIND_DATAA found;
        HANDLE search = INVALID_HANDLE_VALUE;

        for (u32 patternIndex = 0;
             patternIndex < sizeof(patterns) / sizeof(patterns[0]);
             ++patternIndex)
        {
            search = FindFirstFileA(patterns[patternIndex], &found);
            if (search == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                size_t nameLen = strlen(found.cFileName);
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                    nameLen == 0 || nameLen >= sizeof(files[0].name) ||
                    !vm_net_mock_str_ends_with(found.cFileName, ".xse"))
                {
                    continue;
                }
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         found.cFileName);
                files[count].size =
                    ((uint64_t)found.nFileSizeHigh << 32) | found.nFileSizeLow;
                ++count;
            } while (count < fileCap && FindNextFileA(search, &found));
            FindClose(search);
            break;
        }
    }
#else
    {
        const char *directories[] = {
            g_vm_net_mock_resource_dir[0] ? g_vm_net_mock_resource_dir : NULL,
            "../web/fs/JHOnlineData",
            "web/fs/JHOnlineData"
        };
        for (u32 directoryIndex = 0;
             directoryIndex < sizeof(directories) / sizeof(directories[0]);
             ++directoryIndex)
        {
            if (directories[directoryIndex] == NULL)
                continue;
            DIR *directory = opendir(directories[directoryIndex]);
            struct dirent *entry = NULL;
            if (directory == NULL)
                continue;
            while (count < fileCap && (entry = readdir(directory)) != NULL)
            {
                char path[1400];
                char gameName[sizeof(files[0].name)];
                struct stat info;
                memset(gameName, 0, sizeof(gameName));
                if (!vm_net_mock_str_ends_with(entry->d_name, ".xse") ||
                    !vm_mock_admin_host_resource_name_to_game(
                        entry->d_name, gameName, sizeof(gameName)))
                {
                    continue;
                }
                snprintf(path, sizeof(path), "%s/%s",
                         directories[directoryIndex], entry->d_name);
                if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
                    continue;
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         gameName);
                files[count].size = (uint64_t)info.st_size;
                ++count;
            }
            closedir(directory);
            break;
        }
    }
#endif
    if (count > 1)
        qsort(files, count, sizeof(files[0]), vm_mock_admin_scene_file_compare);
    return count;
}

static bool vm_mock_admin_update_file_is_visible(const char *name,
                                                 uint64_t size)
{
    if (!vm_net_mock_update_name_is_safe(name) || size == 0 ||
        size > VM_NET_MOCK_UPDATE_PAYLOAD_MAX)
        return false;
    if (strcmp(name, "server_update_catalog.tsv") == 0 ||
        strcmp(name, "server_update_delivery.tsv") == 0 ||
        vm_net_mock_str_ends_with(name, ".cbm"))
        return false;
    return true;
}

static u32 vm_mock_admin_collect_update_files(vm_mock_admin_scene_file *files,
                                              u32 fileCap)
{
    u32 count = 0;

    if (files == NULL || fileCap == 0)
        return 0;
    memset(files, 0, sizeof(*files) * fileCap);
#ifdef _WIN32
    {
        const char *directories[] = {
            g_vm_net_mock_resource_dir[0] ? g_vm_net_mock_resource_dir : NULL,
            "../web/fs/JHOnlineData",
            "web/fs/JHOnlineData"
        };
        WIN32_FIND_DATAA found;
        for (u32 directoryIndex = 0;
             directoryIndex < sizeof(directories) / sizeof(directories[0]);
             ++directoryIndex)
        {
            char pattern[1200];
            HANDLE search = INVALID_HANDLE_VALUE;
            if (directories[directoryIndex] == NULL)
                continue;
            snprintf(pattern, sizeof(pattern), "%s\\*", directories[directoryIndex]);
            search = FindFirstFileA(pattern, &found);
            if (search == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                size_t nameLen = strlen(found.cFileName);
                uint64_t size = ((uint64_t)found.nFileSizeHigh << 32) |
                                found.nFileSizeLow;
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                    nameLen == 0 || nameLen >= sizeof(files[0].name) ||
                    !vm_mock_admin_update_file_is_visible(found.cFileName, size))
                    continue;
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         found.cFileName);
                files[count].size = size;
                ++count;
            } while (count < fileCap && FindNextFileA(search, &found));
            FindClose(search);
            break;
        }
    }
#else
    {
        const char *directories[] = {
            g_vm_net_mock_resource_dir[0] ? g_vm_net_mock_resource_dir : NULL,
            "../web/fs/JHOnlineData",
            "web/fs/JHOnlineData"
        };
        for (u32 directoryIndex = 0;
             directoryIndex < sizeof(directories) / sizeof(directories[0]);
             ++directoryIndex)
        {
            DIR *directory = NULL;
            struct dirent *entry = NULL;
            if (directories[directoryIndex] == NULL)
                continue;
            directory = opendir(directories[directoryIndex]);
            if (directory == NULL)
                continue;
            while (count < fileCap && (entry = readdir(directory)) != NULL)
            {
                char path[1400];
                struct stat info;
                size_t nameLen = strlen(entry->d_name);
                if (nameLen == 0 || nameLen >= sizeof(files[0].name))
                    continue;
                snprintf(path, sizeof(path), "%s/%s", directories[directoryIndex],
                         entry->d_name);
                if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
                    !vm_mock_admin_update_file_is_visible(
                        entry->d_name, (uint64_t)info.st_size))
                    continue;
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         entry->d_name);
                files[count].size = (uint64_t)info.st_size;
                ++count;
            }
            closedir(directory);
            break;
        }
    }
#endif
    if (count > 1)
        qsort(files, count, sizeof(files[0]), vm_mock_admin_scene_file_compare);
    return count;
}

static void vm_mock_admin_resource_name_to_utf8(const char *name,
                                                char *out,
                                                size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (name == NULL)
        return;
#ifdef CBE_HOST_UTF8_PATHS
    if (vm_mock_admin_text_is_valid_utf8(name))
        snprintf(out, outCap, "%s", name);
    else
        vm_net_mock_gbk_label_to_utf8(name, out, outCap);
#else
    vm_net_mock_gbk_label_to_utf8(name, out, outCap);
#endif
}

static void vm_mock_admin_render_actor_select(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *actorFiles,
    u32 actorCount, const char *currentActor)
{
    bool currentFound = false;

    if (page == NULL)
        return;
    for (u32 i = 0; i < actorCount; ++i)
    {
        if (currentActor != NULL && strcmp(actorFiles[i].name, currentActor) == 0)
        {
            currentFound = true;
            break;
        }
    }
    vm_mock_admin_text_appendf(
        page, "<select name=\"actor_resource\" required>");
    if (currentActor != NULL && currentActor[0] != 0 && !currentFound)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"\" selected disabled>");
        vm_mock_admin_text_append_html(page, currentActor);
        vm_mock_admin_text_appendf(page, "（资源不存在，请重新选择）</option>");
    }
    if ((currentActor == NULL || currentActor[0] == 0) && actorCount != 0)
        vm_mock_admin_text_appendf(page, "<option value=\"\" selected disabled>请选择 Actor 资源</option>");
    for (u32 i = 0; i < actorCount; ++i)
    {
        bool selected = currentFound &&
                        strcmp(actorFiles[i].name, currentActor) == 0;
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "\"%s>", selected ? " selected" : "");
        vm_mock_admin_text_append_html(page, actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    if (actorCount == 0)
        vm_mock_admin_text_appendf(page, "<option value=\"\" disabled>未找到 Actor 资源</option>");
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_xse_select(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *xseFiles,
    u32 xseCount, const char *currentScript)
{
    bool currentFound = false;
    bool hasCurrent = currentScript != NULL && currentScript[0] != 0;

    if (page == NULL)
        return;
    for (u32 i = 0; i < xseCount; ++i)
    {
        if (hasCurrent && strcmp(xseFiles[i].name, currentScript) == 0)
        {
            currentFound = true;
            break;
        }
    }
    vm_mock_admin_text_appendf(page, "<select name=\"script_name\">");
    if (hasCurrent && !currentFound)
    {
        char currentUtf8[192];
        vm_net_mock_gbk_label_to_utf8(currentScript, currentUtf8,
                                      sizeof(currentUtf8));
        vm_mock_admin_text_appendf(
            page, "<option value=\"\" selected disabled>");
        vm_mock_admin_text_append_html(page, currentUtf8);
        vm_mock_admin_text_appendf(
            page, "（资源不存在，请重新选择）</option>");
    }
    vm_mock_admin_text_appendf(
        page, "<option value=\"\"%s>无脚本</option>",
        hasCurrent ? "" : " selected");
    for (u32 i = 0; i < xseCount; ++i)
    {
        char nameUtf8[192];
        bool selected = currentFound &&
                        strcmp(xseFiles[i].name, currentScript) == 0;

        vm_net_mock_gbk_label_to_utf8(xseFiles[i].name, nameUtf8,
                                      sizeof(nameUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, nameUtf8);
        vm_mock_admin_text_appendf(page, "\"%s>", selected ? " selected" : "");
        vm_mock_admin_text_append_html(page, nameUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    if (xseCount == 0)
        vm_mock_admin_text_appendf(
            page, "<option value=\"\" disabled>未找到 XSE 脚本资源</option>");
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_npc_kind_select(vm_mock_admin_text *page,
                                                 u16 currentKind)
{
    static const char *labels[] = {
        "普通／任务 NPC",
        "武器商人",
        "装备修理",
        "技能导师",
        "防具商人（含腰带）",
        "药品商人"
    };

    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(page,
                               "<select name=\"kind\" required>");
    for (u32 kind = VM_NET_MOCK_NPC_KIND_NORMAL;
         kind <= VM_NET_MOCK_NPC_KIND_MAX; ++kind)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%u · %s</option>",
            kind, currentKind == kind ? " selected" : "", kind,
            labels[kind]);
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_npc_task_select(vm_mock_admin_text *page,
                                                 u32 currentTaskId)
{
    vm_net_mock_task_definition tasks[VM_NET_MOCK_TASK_CATALOG_MAX];
    u32 taskCount = 0;

    if (page == NULL)
        return;
    memset(tasks, 0, sizeof(tasks));
    taskCount = vm_net_mock_task_admin_list(
        tasks, VM_NET_MOCK_TASK_CATALOG_MAX);
    vm_mock_admin_text_appendf(
        page, "<select name=\"task_id\"><option value=\"0\"%s>不绑定任务</option>",
        currentTaskId == 0 ? " selected" : "");
    for (u32 i = 0; i < taskCount; ++i)
    {
        char nameUtf8[128];
        if (!tasks[i].enabled)
            continue;
        memset(nameUtf8, 0, sizeof(nameUtf8));
        vm_net_mock_gbk_label_to_utf8(tasks[i].name,
                                      nameUtf8, sizeof(nameUtf8));
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%u · ", tasks[i].taskId,
            currentTaskId == tasks[i].taskId ? " selected" : "",
            tasks[i].taskId);
        vm_mock_admin_text_append_html(page, nameUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static bool vm_mock_admin_utf8_to_gbk_text(const char *utf8,
                                           char *gbk, size_t gbkCap,
                                           bool allowEmpty)
{
    if (gbk == NULL || gbkCap == 0)
        return false;
    gbk[0] = 0;
    if (utf8 == NULL || utf8[0] == 0)
        return allowEmpty;
    utf8_to_gbk((u8 *)utf8, (u8 *)gbk, gbkCap);
    return gbk[0] != 0;
}

static bool vm_mock_admin_scene_file_to_runtime_key(const char *sceneFile,
                                                    char *runtimeScene,
                                                    size_t runtimeSceneCap)
{
    char stem[64];
    char canonical[64];
    size_t len = 0;

    if (runtimeScene == NULL || runtimeSceneCap == 0)
        return false;
    runtimeScene[0] = 0;
    if (sceneFile == NULL || !vm_net_mock_scene_name_is_safe(sceneFile))
        return false;
    snprintf(stem, sizeof(stem), "%s", sceneFile);
    len = strlen(stem);
    if (len > 4 && strcmp(stem + len - 4, ".sce") == 0)
    {
        stem[len - 4] = 0;
        len -= 4;
    }
    memset(canonical, 0, sizeof(canonical));
    if (stem[0] == 'c')
    {
        if (len < 4 || !isdigit((unsigned char)stem[1]) ||
            !isdigit((unsigned char)stem[2]))
        {
            return false;
        }
        snprintf(canonical, sizeof(canonical), "%s", stem);
    }
    else if (len < 3 || !isdigit((unsigned char)stem[0]) ||
             !isdigit((unsigned char)stem[1]))
    {
        /* Battle/test SCE files use keys such as b_20黑龙潭 and 测试地图.
         * They have already been selected from the server-owned SCE directory,
         * so the extensionless resource key is both safe and authoritative. */
        snprintf(canonical, sizeof(canonical), "%s", stem);
    }
    else if (stem[2] == '_' && len > 5 &&
        isdigit((unsigned char)stem[len - 2]) &&
        isdigit((unsigned char)stem[len - 1]))
    {
        size_t middleLen = len - 5;
        if (1 + 2 + middleLen + 1 + 2 + 1 > sizeof(canonical))
            return false;
        canonical[0] = 'c';
        canonical[1] = stem[0];
        canonical[2] = stem[1];
        memcpy(canonical + 3, stem + 3, middleLen);
        canonical[3 + middleLen] = '_';
        canonical[4 + middleLen] = stem[len - 2];
        canonical[5 + middleLen] = stem[len - 1];
        canonical[6 + middleLen] = 0;
    }
    else
    {
        if (len + 2 > sizeof(canonical))
            return false;
        canonical[0] = 'c';
        memcpy(canonical + 1, stem, len + 1);
    }

    /* Legacy files resolve through a c-prefixed runtime alias, while newer
     * catalogs such as 00蓬莱仙岛_02.sce are authoritative without that prefix.
     * Keep whichever key the server resource resolver can actually open. */
    if (vm_net_mock_scene_name_is_safe(canonical))
        snprintf(runtimeScene, runtimeSceneCap, "%s", canonical);
    else if (vm_net_mock_scene_name_is_safe(stem))
        snprintf(runtimeScene, runtimeSceneCap, "%s", stem);
    else
        return false;
    return true;
}

enum
{
    VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX = 16,
    VM_MOCK_ADMIN_PREVIEW_PIXEL_MAX = 1024 * 1024,
    VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX = 16 * 1024 * 1024,
    VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX = 64,
    VM_MOCK_ADMIN_ACTOR_RECT_MAX = 1024,
    VM_MOCK_ADMIN_ACTOR_FRAME_MAX = 256,
    VM_MOCK_ADMIN_ACTOR_SVG_MAX = 2 * 1024 * 1024
};

typedef enum
{
    VM_MOCK_ADMIN_PORTAL_EDGE = 1,
    VM_MOCK_ADMIN_PORTAL_META = 2,
    VM_MOCK_ADMIN_PORTAL_NAMED = 3
} vm_mock_admin_portal_kind;

typedef struct
{
    vm_mock_admin_portal_kind kind;
    char targetScene[64];
    char displayName[64];
    u16 entryId;
    u16 targetEntryId;
    u32 left;
    u32 top;
    u32 right;
    u32 bottom;
} vm_mock_admin_scene_portal;

typedef struct
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t imageIndex;
} vm_mock_admin_actor_rect;

typedef struct
{
    int32_t rectIndex;
    int32_t offsetX;
    int32_t offsetY;
} vm_mock_admin_actor_frame;

typedef struct
{
    char mapName[64];
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    u32 imageCount;
    u32 width;
    u32 height;
    u32 tileWidth;
    u32 tileHeight;
    u32 cols;
    u32 rows;
    const u8 *cells;
    u32 cellCount;
} vm_mock_admin_scene_preview;

static char g_vm_mock_admin_preview_cache_scene[64];
static u8 *g_vm_mock_admin_preview_cache_bmp = NULL;
static u32 g_vm_mock_admin_preview_cache_bmp_len = 0;

static void vm_mock_admin_preview_write_le16(u8 *out, u32 off, u16 value)
{
    out[off] = (u8)(value & 0xffu);
    out[off + 1] = (u8)((value >> 8) & 0xffu);
}

static void vm_mock_admin_preview_write_le32(u8 *out, u32 off, u32 value)
{
    out[off] = (u8)(value & 0xffu);
    out[off + 1] = (u8)((value >> 8) & 0xffu);
    out[off + 2] = (u8)((value >> 16) & 0xffu);
    out[off + 3] = (u8)((value >> 24) & 0xffu);
}

static bool vm_mock_admin_load_data_payload(const char *name,
                                            const char *requiredSuffix,
                                            u8 **payloadOut,
                                            u32 *payloadLenOut,
                                            u8 *typeOut)
{
    FILE *fp = NULL;
    u8 *raw = NULL;
    u8 *payload = NULL;
    long rawSizeLong = 0;
    u32 rawSize = 0;
    u32 declaredLen = 0;
    u32 decodedLen = 0;
    u8 type = 0;
    bool ok = false;

    if (payloadOut)
        *payloadOut = NULL;
    if (payloadLenOut)
        *payloadLenOut = 0;
    if (typeOut)
        *typeOut = 0;
    if (name == NULL || payloadOut == NULL || payloadLenOut == NULL ||
        !vm_net_mock_open_server_data_resource(name, requiredSuffix,
                                               &fp, NULL, 0))
    {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0 ||
        (rawSizeLong = ftell(fp)) < 5 ||
        rawSizeLong > VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX ||
        fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return false;
    }
    rawSize = (u32)rawSizeLong;
    raw = (u8 *)malloc(rawSize);
    if (raw == NULL || fread(raw, 1, rawSize, fp) != rawSize)
        goto done;
    declaredLen = (u32)raw[0] | ((u32)raw[1] << 8) |
                  ((u32)raw[2] << 16) | ((u32)raw[3] << 24);
    if (declaredLen != rawSize - 4 || declaredLen < 1)
        goto done;
    type = raw[4];
    if (type == 1)
    {
        decodedLen = declaredLen - 1;
        if (decodedLen == 0)
            goto done;
        payload = (u8 *)malloc(decodedLen);
        if (payload == NULL)
            goto done;
        memcpy(payload, raw + 5, decodedLen);
    }
    else if (type == 2)
    {
        if (declaredLen < 9)
            goto done;
        decodedLen = vm_net_mock_read_be32_at(raw + 4, 5) & 0x7fffffffu;
        if (decodedLen == 0 || decodedLen > VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX)
            goto done;
        payload = (u8 *)malloc(decodedLen);
        if (payload == NULL ||
            vm_net_mock_decode_lzss_resource_stream(raw + 4, declaredLen,
                                                    payload, decodedLen) != decodedLen)
        {
            goto done;
        }
    }
    else
    {
        goto done;
    }
    *payloadOut = payload;
    *payloadLenOut = decodedLen;
    if (typeOut)
        *typeOut = type;
    payload = NULL;
    ok = true;

done:
    if (fp)
        fclose(fp);
    free(raw);
    free(payload);
    return ok;
}

static bool vm_mock_admin_scene_sibling_map_name(const char *scene,
                                                 char *mapName,
                                                 size_t mapNameCap)
{
    char stem[64];
    size_t len = 0;

    if (scene == NULL || scene[0] == 0 || mapName == NULL || mapNameCap == 0 ||
        vm_net_mock_scene_name_has_path_separator(scene))
    {
        return false;
    }
    snprintf(stem, sizeof(stem), "%s", scene);
    len = strlen(stem);
    if (len > 4 && strcmp(stem + len - 4, ".sce") == 0)
        stem[len - 4] = 0;
    if (snprintf(mapName, mapNameCap, "%s.map", stem) >= (int)mapNameCap ||
        !vm_net_mock_open_server_data_resource(mapName, ".map", NULL, NULL, 0))
    {
        mapName[0] = 0;
        return false;
    }
    return true;
}

static bool vm_mock_admin_scene_map_name(const char *scene,
                                         char *mapName,
                                         size_t mapNameCap)
{
    u8 data[8192];
    u32 len = 0;
    u32 base = 0;
    u32 nameLen = 0;

    if (mapName == NULL || mapNameCap == 0)
        return false;
    mapName[0] = 0;
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    if (len < 15)
        return vm_mock_admin_scene_sibling_map_name(scene, mapName, mapNameCap);
    for (base = 0; base + 15 <= len && base < 32; ++base)
    {
        if (memcmp(data + base, "SCE2", 4) == 0)
            break;
    }
    if (base + 15 > len || base >= 32)
        return vm_mock_admin_scene_sibling_map_name(scene, mapName, mapNameCap);
    nameLen = data[base + 10];
    if (nameLen == 0 || nameLen >= mapNameCap || base + 11 + nameLen > len)
        return vm_mock_admin_scene_sibling_map_name(scene, mapName, mapNameCap);
    memcpy(mapName, data + base + 11, nameLen);
    mapName[nameLen] = 0;
    if (vm_net_mock_str_ends_with(mapName, ".map") &&
        !vm_net_mock_scene_name_has_path_separator(mapName))
    {
        return true;
    }
    return vm_mock_admin_scene_sibling_map_name(scene, mapName, mapNameCap);
}

static bool vm_mock_admin_parse_map_preview(const u8 *data, u32 len,
                                            const char *mapName,
                                            vm_mock_admin_scene_preview *preview)
{
    u32 pos = 0;
    u32 expectedCells = 0;

    if (data == NULL || preview == NULL || len < 24)
        return false;
    memset(preview, 0, sizeof(*preview));
    if (mapName)
        snprintf(preview->mapName, sizeof(preview->mapName), "%s", mapName);
    preview->imageCount = vm_mock_service_read_le32(data + pos);
    pos += 4;
    if (preview->imageCount == 0 ||
        preview->imageCount > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
    {
        return false;
    }
    for (u32 i = 0; i < preview->imageCount; ++i)
    {
        u32 nameLen = 0;
        if (pos >= len)
            return false;
        nameLen = data[pos++];
        if (nameLen == 0 || nameLen >= sizeof(preview->imageNames[i]) ||
            pos + nameLen > len)
        {
            return false;
        }
        memcpy(preview->imageNames[i], data + pos, nameLen);
        preview->imageNames[i][nameLen] = 0;
        pos += nameLen;
    }
    if (pos + 16 > len)
        return false;
    preview->width = vm_mock_service_read_le32(data + pos);
    preview->height = vm_mock_service_read_le32(data + pos + 4);
    preview->tileWidth = vm_mock_service_read_le32(data + pos + 8);
    preview->tileHeight = vm_mock_service_read_le32(data + pos + 12);
    pos += 16;
    if (preview->width == 0 || preview->height == 0 ||
        preview->tileWidth == 0 || preview->tileHeight == 0 ||
        preview->width > 4096 || preview->height > 4096 ||
        preview->width > VM_MOCK_ADMIN_PREVIEW_PIXEL_MAX / preview->height ||
        ((len - pos) & 3u) != 0)
    {
        return false;
    }
    preview->cols = (preview->width + preview->tileWidth - 1) /
                    preview->tileWidth;
    preview->rows = (preview->height + preview->tileHeight - 1) /
                    preview->tileHeight;
    if (preview->cols == 0 || preview->rows == 0 ||
        preview->cols > 4096 / preview->rows)
    {
        return false;
    }
    expectedCells = preview->cols * preview->rows;
    preview->cellCount = (len - pos) / 4;
    if (preview->cellCount != expectedCells)
        return false;
    preview->cells = data + pos;
    return true;
}

static bool vm_mock_admin_scene_preview_info(const char *scene,
                                             vm_mock_admin_scene_preview *preview)
{
    char mapName[64];
    u8 *mapPayload = NULL;
    u32 mapPayloadLen = 0;
    bool ok = false;

    memset(mapName, 0, sizeof(mapName));
    if (!vm_mock_admin_scene_map_name(scene, mapName, sizeof(mapName)) ||
        !vm_mock_admin_load_data_payload(mapName, ".map", &mapPayload,
                                         &mapPayloadLen, NULL))
    {
        return false;
    }
    ok = vm_mock_admin_parse_map_preview(mapPayload, mapPayloadLen,
                                         mapName, preview);
    free(mapPayload);
    if (ok)
        preview->cells = NULL;
    return ok;
}

static bool vm_mock_admin_read_sce_string_field(const u8 *data, u32 len,
                                                u32 *pos, u16 expectedField,
                                                char *out, size_t outCap)
{
    if (data == NULL || pos == NULL || out == NULL || outCap == 0 ||
        *pos + 5 > len || vm_net_mock_read_le16_at(data, *pos) != 3 ||
        vm_net_mock_read_le16_at(data, *pos + 2) != expectedField)
    {
        return false;
    }
    *pos += 4;
    return vm_net_mock_read_sce_len_string(data, len, pos, out, outCap);
}

static bool vm_mock_admin_parse_sce_meta_portal_at(
    const u8 *data, u32 len, u32 off, vm_mock_admin_scene_portal *portal,
    u32 *endOut)
{
    u32 pos = off;
    u16 kind = 0;

    if (data == NULL || portal == NULL || off + 12 > len)
        return false;
    memset(portal, 0, sizeof(*portal));
    portal->kind = VM_MOCK_ADMIN_PORTAL_META;
    portal->entryId = 0xffff;
    portal->targetEntryId = 0xffff;
    kind = vm_net_mock_read_le16_at(data, pos);
    pos += 2;
    if (kind == 8)
    {
        if (pos + 6 > len)
            return false;
        pos += 6;
    }
    else
    {
        if (pos + 8 > len || vm_net_mock_read_le16_at(data, pos) != 8)
            return false;
        pos += 8;
    }
    if (!vm_mock_admin_read_sce_string_field(data, len, &pos, 6,
                                             portal->targetScene,
                                             sizeof(portal->targetScene)) ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x07,
                                           &portal->entryId))
    {
        return false;
    }
    {
        u16 left = 0;
        u16 top = 0;
        u16 right = 0;
        u16 bottom = 0;
        if (!vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0a, &left) ||
            !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0b, &top) ||
            !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0c, &right) ||
            !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0d, &bottom) ||
            !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x13,
                                               &portal->targetEntryId) ||
            right < left || bottom < top)
        {
            return false;
        }
        portal->left = left;
        portal->top = top;
        portal->right = right;
        portal->bottom = bottom;
    }
    if (endOut)
        *endOut = pos;
    return true;
}

static bool vm_mock_admin_parse_sce_named_portal_at(
    const u8 *data, u32 len, u32 off, vm_mock_admin_scene_portal *portal,
    u32 *endOut)
{
    u32 pos = off;
    u16 kind = 0;
    u16 tileX = 0;
    u16 tileY = 0;
    u16 tileWidth = 0;
    u16 tileHeight = 0;

    if (data == NULL || portal == NULL || off + 12 > len)
        return false;
    memset(portal, 0, sizeof(*portal));
    portal->kind = VM_MOCK_ADMIN_PORTAL_NAMED;
    portal->entryId = 0xffff;
    portal->targetEntryId = 0xffff;
    kind = vm_net_mock_read_le16_at(data, pos);
    pos += 2;
    if (kind == 4)
    {
        if (pos + 8 > len)
            return false;
        tileX = vm_net_mock_read_le16_at(data, pos);
        tileY = vm_net_mock_read_le16_at(data, pos + 2);
        tileWidth = vm_net_mock_read_le16_at(data, pos + 4);
        tileHeight = vm_net_mock_read_le16_at(data, pos + 6);
        pos += 8;
    }
    else
    {
        if (pos + 10 > len || vm_net_mock_read_le16_at(data, pos) != 4)
            return false;
        tileX = vm_net_mock_read_le16_at(data, pos + 2);
        tileY = vm_net_mock_read_le16_at(data, pos + 4);
        tileWidth = vm_net_mock_read_le16_at(data, pos + 6);
        tileHeight = vm_net_mock_read_le16_at(data, pos + 8);
        pos += 10;
    }
    if (tileWidth == 0 || tileHeight == 0)
        return false;

    /* Named portals optionally carry a field-0x12 interaction prompt. */
    if (kind == 4 && pos + 3 <= len &&
        vm_net_mock_read_le16_at(data, pos) == 0x12)
    {
        char ignoredPrompt[128];
        pos += 2;
        if (!vm_net_mock_read_sce_len_string(data, len, &pos,
                                             ignoredPrompt,
                                             sizeof(ignoredPrompt)))
        {
            return false;
        }
    }
    else if (pos + 5 <= len && vm_net_mock_read_le16_at(data, pos) == 3 &&
             vm_net_mock_read_le16_at(data, pos + 2) == 0x12)
    {
        char ignoredPrompt[128];
        if (!vm_mock_admin_read_sce_string_field(data, len, &pos, 0x12,
                                                 ignoredPrompt,
                                                 sizeof(ignoredPrompt)))
        {
            return false;
        }
    }
    if (!vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x15,
                                           &portal->targetEntryId) ||
        !vm_mock_admin_read_sce_string_field(data, len, &pos, 0x16,
                                             portal->displayName,
                                             sizeof(portal->displayName)) ||
        !vm_mock_admin_read_sce_string_field(data, len, &pos, 0x17,
                                             portal->targetScene,
                                             sizeof(portal->targetScene)) ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene))
    {
        return false;
    }
    portal->left = (u32)tileX * 16u;
    portal->top = (u32)tileY * 16u;
    portal->right = portal->left + (u32)tileWidth * 16u;
    portal->bottom = portal->top + (u32)tileHeight * 16u;
    if (endOut)
        *endOut = pos;
    return true;
}

static bool vm_mock_admin_portal_equals(const vm_mock_admin_scene_portal *a,
                                        const vm_mock_admin_scene_portal *b)
{
    return a != NULL && b != NULL && a->kind == b->kind &&
           a->entryId == b->entryId &&
           a->targetEntryId == b->targetEntryId &&
           a->left == b->left && a->top == b->top &&
           a->right == b->right && a->bottom == b->bottom &&
           strcmp(a->targetScene, b->targetScene) == 0;
}

static u32 vm_mock_admin_collect_scene_portals(
    const char *scene, vm_mock_admin_scene_portal *portals, u32 portalCap,
    u32 *totalOut)
{
    u8 data[8192];
    u32 len = 0;
    u32 start = 0;
    u32 count = 0;
    u32 total = 0;

    if (totalOut)
        *totalOut = 0;
    if (scene == NULL || scene[0] == 0 || portals == NULL || portalCap == 0)
        return 0;
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    start = vm_net_mock_scene_payload_start(data, len);
    if (len == 0 || start == 0)
        return 0;

    for (u32 off = start; off + 12 <= len; ++off)
    {
        vm_mock_admin_scene_portal portal;
        vm_net_mock_sce_edge_portal edge;
        u32 end = 0;
        bool parsed = false;

        memset(&portal, 0, sizeof(portal));
        memset(&edge, 0, sizeof(edge));
        if (vm_net_mock_parse_sce_edge_portal_at(data, len, off, &edge, &end))
        {
            portal.kind = VM_MOCK_ADMIN_PORTAL_EDGE;
            portal.entryId = edge.entryId;
            portal.targetEntryId = edge.targetEntryId;
            portal.left = edge.left;
            portal.top = edge.top;
            portal.right = edge.right;
            portal.bottom = edge.bottom;
            snprintf(portal.targetScene, sizeof(portal.targetScene), "%s",
                     edge.targetScene);
            parsed = true;
        }
        else if (vm_mock_admin_parse_sce_meta_portal_at(data, len, off,
                                                        &portal, &end) ||
                 vm_mock_admin_parse_sce_named_portal_at(data, len, off,
                                                         &portal, &end))
        {
            parsed = true;
        }
        if (parsed)
        {
            bool duplicate = false;
            for (u32 i = 0; i < count; ++i)
            {
                if (vm_mock_admin_portal_equals(&portals[i], &portal))
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
            {
                ++total;
                if (count < portalCap)
                    portals[count++] = portal;
            }
            if (end > off + 1)
                off = end - 1;
        }
    }
    if (totalOut)
        *totalOut = total;
    return count;
}

static bool vm_mock_admin_actor_read_s32(const u8 *data, u32 len, u32 *pos,
                                         int32_t *valueOut)
{
    if (data == NULL || pos == NULL || valueOut == NULL || *pos + 4 > len)
        return false;
    *valueOut = (int32_t)vm_mock_service_read_le32(data + *pos);
    *pos += 4;
    return true;
}

static bool vm_mock_admin_actor_read_string(const u8 *data, u32 len, u32 *pos,
                                            char *out, size_t outCap)
{
    u32 stringLen = 0;
    if (data == NULL || pos == NULL || out == NULL || outCap == 0 || *pos >= len)
        return false;
    stringLen = data[(*pos)++];
    if (stringLen == 0 || stringLen >= outCap || *pos + stringLen > len)
        return false;
    memcpy(out, data + *pos, stringLen);
    out[stringLen] = 0;
    *pos += stringLen;
    return true;
}

/* An Actor is usable when the authoritative game-data source contains a
 * decodable Actor payload and every image it references is a decodable GIF.
 * Presence in bin/JHOnlineData is a per-client cache state, not a validity
 * rule. */
static bool vm_mock_admin_actor_resource_inspect(
    const char *actorResource,
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64],
    u32 *imageCountOut)
{
    u8 *actorPayload = NULL;
    u32 actorPayloadLen = 0;
    u8 actorType = 0;
    u32 pos = 0;
    int32_t imageCountSigned = 0;
    int32_t rectCountSigned = 0;
    int32_t animationCountSigned = 0;
    u32 imageCount = 0;
    u32 totalFrameCount = 0;
    const char *failureStage = "actor-header";
    bool ok = false;

    if (imageCountOut)
        *imageCountOut = 0;
    if (imageNames)
        memset(imageNames, 0,
               sizeof(imageNames[0]) * VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX);
    if (actorResource == NULL || imageNames == NULL || imageCountOut == NULL ||
        vm_net_mock_scene_name_has_path_separator(actorResource) ||
        !vm_net_mock_str_ends_with(actorResource, ".actor") ||
        !vm_mock_admin_load_data_payload(actorResource, ".actor",
                                         &actorPayload, &actorPayloadLen,
                                         &actorType) || actorType != 2 ||
        !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                      &imageCountSigned) ||
        imageCountSigned <= 0 ||
        imageCountSigned > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
    {
        goto done;
    }
    imageCount = (u32)imageCountSigned;
    failureStage = "image";
    for (u32 i = 0; i < imageCount; ++i)
    {
        u8 *imagePayload = NULL;
        u32 imagePayloadLen = 0;
        u8 imageType = 0;
        GifOutput image;
        int mallocSize = 0;
        bool imageOk = false;

        memset(&image, 0, sizeof(image));
        if (!vm_mock_admin_actor_read_string(actorPayload, actorPayloadLen,
                                             &pos, imageNames[i],
                                             sizeof(imageNames[i])) ||
            vm_net_mock_scene_name_has_path_separator(imageNames[i]) ||
            !vm_net_mock_str_ends_with(imageNames[i], ".gif") ||
            !vm_mock_admin_load_data_payload(imageNames[i], ".gif",
                                             &imagePayload, &imagePayloadLen,
                                             &imageType) || imageType != 1)
        {
            free(imagePayload);
            goto done;
        }
        imageOk = gifDecodeExt(imagePayload, &image, 1, &mallocSize) != 0 &&
                  image.pixels != NULL && image.width != 0 && image.height != 0;
        free(imagePayload);
        if (image.owned && image.pixels)
            free_mem(image.pixels);
        if (!imageOk)
            goto done;
    }
    failureStage = "rect-count";
    if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                      &rectCountSigned) ||
        rectCountSigned <= 0 ||
        rectCountSigned > VM_MOCK_ADMIN_ACTOR_RECT_MAX)
    {
        goto done;
    }
    for (int32_t i = 0; i < rectCountSigned; ++i)
    {
        int32_t left = 0;
        int32_t top = 0;
        int32_t right = 0;
        int32_t bottom = 0;
        int32_t imageIndex = 0;
        failureStage = "rect";
        if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &left) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &top) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &right) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &bottom) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &imageIndex))
        {
            goto done;
        }
        /* Some shipped effect/UI Actors contain unused rectangle rows with a
         * sentinel image index or zero-sized bounds.  The client accepts those
         * files, so only validate the serialized shape here; live frame rows
         * are checked against the rectangle table below. */
        (void)left;
        (void)top;
        (void)right;
        (void)bottom;
        (void)imageIndex;
    }
    failureStage = "animation-count";
    if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                      &animationCountSigned) ||
        animationCountSigned <= 0 || animationCountSigned > 4096)
    {
        goto done;
    }
    for (int32_t animationIndex = 0;
         animationIndex < animationCountSigned; ++animationIndex)
    {
        int32_t partCountSigned = 0;
        failureStage = "part-count";
        if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &partCountSigned) ||
            partCountSigned <= 0 || partCountSigned > 4096)
        {
            goto done;
        }
        for (int32_t partIndex = 0; partIndex < partCountSigned; ++partIndex)
        {
            int32_t partId = 0;
            int32_t frameCountSigned = 0;
            failureStage = "frame-count";
            if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen,
                                              &pos, &partId) ||
                !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen,
                                              &pos, &frameCountSigned) ||
                frameCountSigned < 0 || frameCountSigned > 65535 ||
                (u32)frameCountSigned > (actorPayloadLen - pos) / 20u)
            {
                goto done;
            }
            (void)partId;
            for (int32_t frameIndex = 0;
                 frameIndex < frameCountSigned; ++frameIndex)
            {
                int32_t rectIndex = 0;
                int32_t ignored = 0;
                failureStage = "frame";
                if (!vm_mock_admin_actor_read_s32(actorPayload,
                                                  actorPayloadLen, &pos,
                                                  &rectIndex) ||
                    !vm_mock_admin_actor_read_s32(actorPayload,
                                                  actorPayloadLen, &pos,
                                                  &ignored) ||
                    !vm_mock_admin_actor_read_s32(actorPayload,
                                                  actorPayloadLen, &pos,
                                                  &ignored) ||
                    !vm_mock_admin_actor_read_s32(actorPayload,
                                                  actorPayloadLen, &pos,
                                                  &ignored) ||
                    !vm_mock_admin_actor_read_s32(actorPayload,
                                                  actorPayloadLen, &pos,
                                                  &ignored) ||
                    rectIndex < 0 || rectIndex >= rectCountSigned)
                {
                    goto done;
                }
                ++totalFrameCount;
            }
        }
    }
    failureStage = "payload-tail";
    if (pos != actorPayloadLen || totalFrameCount == 0)
        goto done;
    *imageCountOut = imageCount;
    ok = true;

done:
    if (!ok && actorResource != NULL)
    {
        printf("[warn][mock-admin] actor_resource_validate_reject resource=%s stage=%s pos=%u len=%u images=%d rects=%d animations=%d frames=%u\n",
               actorResource, failureStage, pos, actorPayloadLen,
               imageCountSigned, rectCountSigned, animationCountSigned,
               totalFrameCount);
    }
    free(actorPayload);
    return ok;
}

/* Saving an NPC publishes its complete dependency set.  No file is copied to
 * the emulator cache here. If the scene parser opens a missing Actor/GIF, the
 * emulator client obtains that published name over WT 18/7, verifies every
 * chunk, installs it into its own cache, and retries the original open. */
static bool vm_mock_admin_publish_actor_resource(
    const char *actorResource,
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64],
    u32 imageCount,
    const char **errorOut)
{
    const char *names[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX + 1];

    if (actorResource == NULL || actorResource[0] == 0 ||
        imageNames == NULL || imageCount > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
    {
        if (errorOut)
            *errorOut = "Actor 资源依赖列表无效";
        return false;
    }
    memset(names, 0, sizeof(names));
    names[0] = actorResource;
    for (u32 i = 0; i < imageCount; ++i)
        names[i + 1] = imageNames[i];
    return vm_net_mock_update_admin_publish_named_files(
        names, imageCount + 1, errorOut);
}

/* Records created before named-resource publishing was introduced still need
 * to be safe for a clean client cache. The DB loader publishes enabled rows at
 * startup, and the scene catalog calls this again as an idempotent safety
 * check for service-side companion rows. */
static bool vm_net_mock_ensure_actor_resource_published(
    const char *actorResource, const char **errorOut)
{
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    const char *names[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX + 1];
    u32 imageCount = 0;
    bool current = true;

    if (errorOut)
        *errorOut = NULL;
    memset(imageNames, 0, sizeof(imageNames));
    memset(names, 0, sizeof(names));
    if (!vm_mock_admin_actor_resource_inspect(actorResource, imageNames,
                                              &imageCount))
    {
        if (errorOut)
            *errorOut = "Actor 资源无效或引用图片不完整";
        return false;
    }
    names[0] = actorResource;
    for (u32 i = 0; i < imageCount; ++i)
        names[i + 1] = imageNames[i];
    for (u32 i = 0; i < imageCount + 1; ++i)
    {
        u16 contentVersion = 0;
        vm_net_mock_update_named_config *published =
            vm_net_mock_update_find_named(names[i]);
        if (published == NULL || !published->enabled ||
            !vm_net_mock_update_named_content_version(names[i],
                                                      &contentVersion) ||
            published->version != contentVersion)
        {
            current = false;
            break;
        }
    }
    if (current)
        return true;
    if (!vm_mock_admin_publish_actor_resource(actorResource, imageNames,
                                              imageCount, errorOut))
    {
        return false;
    }
    printf("[info][mock-admin] dynamic_npc_resource_lazy_publish actor=%s dependencies=%u trigger=dynamic-npc-catalog delivery=client-file-miss-WT18/7\n",
           actorResource, imageCount);
    return true;
}

static bool vm_mock_admin_build_actor_preview_svg(const char *actorResource,
                                                  u8 **svgOut,
                                                  u32 *svgLenOut)
{
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    vm_mock_admin_actor_rect *rects = NULL;
    vm_mock_admin_actor_frame frames[VM_MOCK_ADMIN_ACTOR_FRAME_MAX];
    GifOutput images[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX];
    bool imageValid[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX];
    u8 *actorPayload = NULL;
    u32 actorPayloadLen = 0;
    u8 actorType = 0;
    u32 pos = 0;
    int32_t imageCountSigned = 0;
    int32_t rectCountSigned = 0;
    int32_t animationCount = 0;
    int32_t partCount = 0;
    u32 imageCount = 0;
    u32 rectCount = 0;
    u32 frameCount = 0;
    int32_t minX = 0;
    int32_t minY = 0;
    int32_t maxX = 0;
    int32_t maxY = 0;
    u32 width = 0;
    u32 height = 0;
    u16 *canvas = NULL;
    char *svg = NULL;
    vm_mock_admin_text text;
    bool boundsReady = false;
    bool ok = false;

    if (svgOut)
        *svgOut = NULL;
    if (svgLenOut)
        *svgLenOut = 0;
    memset(imageNames, 0, sizeof(imageNames));
    memset(frames, 0, sizeof(frames));
    memset(images, 0, sizeof(images));
    memset(imageValid, 0, sizeof(imageValid));
    if (actorResource == NULL || svgOut == NULL || svgLenOut == NULL ||
        vm_net_mock_scene_name_has_path_separator(actorResource) ||
        !vm_net_mock_str_ends_with(actorResource, ".actor") ||
        !vm_mock_admin_load_data_payload(actorResource, ".actor",
                                         &actorPayload, &actorPayloadLen,
                                         &actorType) || actorType != 2)
    {
        goto done;
    }
    if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                      &imageCountSigned) ||
        imageCountSigned <= 0 ||
        imageCountSigned > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
    {
        goto done;
    }
    imageCount = (u32)imageCountSigned;
    for (u32 i = 0; i < imageCount; ++i)
    {
        if (!vm_mock_admin_actor_read_string(actorPayload, actorPayloadLen,
                                             &pos, imageNames[i],
                                             sizeof(imageNames[i])) ||
            !vm_net_mock_str_ends_with(imageNames[i], ".gif") ||
            vm_net_mock_scene_name_has_path_separator(imageNames[i]))
        {
            goto done;
        }
    }
    if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                      &rectCountSigned) ||
        rectCountSigned <= 0 || rectCountSigned > VM_MOCK_ADMIN_ACTOR_RECT_MAX)
    {
        goto done;
    }
    rectCount = (u32)rectCountSigned;
    rects = (vm_mock_admin_actor_rect *)calloc(rectCount, sizeof(*rects));
    if (rects == NULL)
        goto done;
    for (u32 i = 0; i < rectCount; ++i)
    {
        if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &rects[i].left) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &rects[i].top) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &rects[i].right) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &rects[i].bottom) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &rects[i].imageIndex) ||
            rects[i].imageIndex < 0 ||
            (u32)rects[i].imageIndex >= imageCount)
        {
            goto done;
        }
    }
    if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                      &animationCount) ||
        animationCount <= 0 ||
        !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                      &partCount) ||
        partCount <= 0 || partCount > 4096)
    {
        goto done;
    }
    for (int32_t partIndex = 0; partIndex < partCount; ++partIndex)
    {
        int32_t partId = 0;
        int32_t candidateFrameCount = 0;
        if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &partId) ||
            !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen, &pos,
                                          &candidateFrameCount) ||
            candidateFrameCount < 0 || candidateFrameCount > 65535 ||
            (u32)candidateFrameCount > (actorPayloadLen - pos) / 20u)
        {
            goto done;
        }
        (void)partId;
        if (frameCount == 0 && candidateFrameCount > 0 &&
            candidateFrameCount <= VM_MOCK_ADMIN_ACTOR_FRAME_MAX)
        {
            frameCount = (u32)candidateFrameCount;
            for (u32 i = 0; i < frameCount; ++i)
            {
                int32_t ignored = 0;
                if (!vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen,
                                                  &pos, &frames[i].rectIndex) ||
                    !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen,
                                                  &pos, &frames[i].offsetX) ||
                    !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen,
                                                  &pos, &frames[i].offsetY) ||
                    !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen,
                                                  &pos, &ignored) ||
                    !vm_mock_admin_actor_read_s32(actorPayload, actorPayloadLen,
                                                  &pos, &ignored))
                {
                    goto done;
                }
            }
        }
        else
        {
            pos += (u32)candidateFrameCount * 20u;
        }
    }
    if (frameCount == 0)
        goto done;

    for (u32 i = 0; i < frameCount; ++i)
    {
        vm_mock_admin_actor_rect *rect = NULL;
        int32_t right = 0;
        int32_t bottom = 0;
        if (frames[i].rectIndex < 0 || (u32)frames[i].rectIndex >= rectCount)
            goto done;
        rect = &rects[frames[i].rectIndex];
        if (rect->right <= rect->left || rect->bottom <= rect->top)
            goto done;
        right = frames[i].offsetX + (rect->right - rect->left);
        bottom = frames[i].offsetY + (rect->bottom - rect->top);
        if (!boundsReady)
        {
            minX = frames[i].offsetX;
            minY = frames[i].offsetY;
            maxX = right;
            maxY = bottom;
            boundsReady = true;
        }
        else
        {
            if (frames[i].offsetX < minX)
                minX = frames[i].offsetX;
            if (frames[i].offsetY < minY)
                minY = frames[i].offsetY;
            if (right > maxX)
                maxX = right;
            if (bottom > maxY)
                maxY = bottom;
        }
    }
    if (!boundsReady || maxX <= minX || maxY <= minY ||
        maxX - minX > 512 || maxY - minY > 512)
    {
        goto done;
    }
    width = (u32)(maxX - minX);
    height = (u32)(maxY - minY);
    canvas = (u16 *)calloc((size_t)width * height, sizeof(u16));
    if (canvas == NULL)
        goto done;

    for (u32 i = 0; i < imageCount; ++i)
    {
        u8 *imagePayload = NULL;
        u32 imagePayloadLen = 0;
        u8 imageType = 0;
        int mallocSize = 0;
        if (!vm_mock_admin_load_data_payload(imageNames[i], ".gif",
                                             &imagePayload, &imagePayloadLen,
                                             &imageType) || imageType != 1)
        {
            free(imagePayload);
            continue;
        }
        imageValid[i] = gifDecodeExt(imagePayload, &images[i], 1,
                                     &mallocSize) != 0 &&
                        images[i].pixels != NULL && images[i].width != 0 &&
                        images[i].height != 0;
        free(imagePayload);
    }

    for (u32 i = 0; i < frameCount; ++i)
    {
        const vm_mock_admin_actor_rect *rect = &rects[frames[i].rectIndex];
        GifOutput *image = &images[rect->imageIndex];
        u32 rectWidth = (u32)(rect->right - rect->left);
        u32 rectHeight = (u32)(rect->bottom - rect->top);
        u32 sourcePitch = 0;
        int32_t destX = frames[i].offsetX - minX;
        int32_t destY = frames[i].offsetY - minY;
        if (!imageValid[rect->imageIndex] || rect->left < 0 || rect->top < 0 ||
            (u32)rect->right > image->width ||
            (u32)rect->bottom > image->height)
        {
            continue;
        }
        sourcePitch = image->width + ((4u - (image->width & 3u)) & 3u);
        for (u32 y = 0; y < rectHeight; ++y)
        {
            for (u32 x = 0; x < rectWidth; ++x)
            {
                u16 pixel = image->pixels[((u32)rect->top + y) * sourcePitch +
                                          (u32)rect->left + x];
                if (pixel != 0 && destX + (int32_t)x >= 0 &&
                    destY + (int32_t)y >= 0 &&
                    (u32)(destX + (int32_t)x) < width &&
                    (u32)(destY + (int32_t)y) < height)
                {
                    canvas[(u32)(destY + (int32_t)y) * width +
                           (u32)(destX + (int32_t)x)] = pixel;
                }
            }
        }
    }

    svg = (char *)malloc(VM_MOCK_ADMIN_ACTOR_SVG_MAX);
    if (svg == NULL)
        goto done;
    vm_mock_admin_text_init(&text, svg, VM_MOCK_ADMIN_ACTOR_SVG_MAX);
    vm_mock_admin_text_appendf(
        &text,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%u\" height=\"%u\" viewBox=\"0 0 %u %u\" shape-rendering=\"crispEdges\">",
        width, height, width, height);
    for (u32 y = 0; y < height && !text.truncated; ++y)
    {
        for (u32 x = 0; x < width;)
        {
            u16 pixel = canvas[y * width + x];
            u32 run = 1;
            while (x + run < width && canvas[y * width + x + run] == pixel)
                ++run;
            if (pixel != 0)
            {
                u32 red = ((pixel >> 11) & 0x1fu) * 255u / 31u;
                u32 green = ((pixel >> 5) & 0x3fu) * 255u / 63u;
                u32 blue = (pixel & 0x1fu) * 255u / 31u;
                vm_mock_admin_text_appendf(
                    &text,
                    "<rect x=\"%u\" y=\"%u\" width=\"%u\" height=\"1\" fill=\"#%02x%02x%02x\"/>",
                    x, y, run, red, green, blue);
            }
            x += run;
        }
    }
    vm_mock_admin_text_appendf(&text, "</svg>");
    if (text.truncated || text.length == 0)
        goto done;
    *svgOut = (u8 *)svg;
    *svgLenOut = (u32)text.length;
    svg = NULL;
    ok = true;

done:
    for (u32 i = 0; i < VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX; ++i)
    {
        if (images[i].owned && images[i].pixels)
            free_mem(images[i].pixels);
    }
    free(actorPayload);
    free(rects);
    free(canvas);
    free(svg);
    return ok;
}

static const char *vm_mock_admin_orientation_arrow(u16 orientation)
{
    switch (orientation)
    {
    case 1:
        return "↑";
    case 2:
        return "→";
    case 3:
        return "↓";
    case 4:
        return "←";
    default:
        return "•";
    }
}

static const char *vm_mock_admin_orientation_name(u16 orientation)
{
    switch (orientation)
    {
    case 0:
        return "默认";
    case 1:
        return "上";
    case 2:
        return "右";
    case 3:
        return "下";
    case 4:
        return "左";
    default:
        return "扩展";
    }
}

static void vm_mock_admin_preview_fill_placeholder(u16 *canvas,
                                                   u32 canvasWidth,
                                                   u32 canvasHeight,
                                                   u32 x0, u32 y0,
                                                   u32 width, u32 height)
{
    for (u32 y = 0; y < height && y0 + y < canvasHeight; ++y)
    {
        for (u32 x = 0; x < width && x0 + x < canvasWidth; ++x)
        {
            canvas[(y0 + y) * canvasWidth + x0 + x] =
                ((((x / 4) ^ (y / 4)) & 1u) == 0) ? 0xf81fu : 0x0000u;
        }
    }
}

static bool vm_mock_admin_build_scene_preview_bmp(const char *scene,
                                                  const u8 **bmpOut,
                                                  u32 *bmpLenOut,
                                                  u32 *widthOut,
                                                  u32 *heightOut)
{
    char mapName[64];
    u8 *mapPayload = NULL;
    u32 mapPayloadLen = 0;
    vm_mock_admin_scene_preview preview;
    GifOutput images[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX];
    bool imageValid[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX];
    u16 *canvas = NULL;
    u8 *bmp = NULL;
    u32 rowBytes = 0;
    u32 imageBytes = 0;
    u32 bmpLen = 0;
    bool ok = false;

    if (bmpOut)
        *bmpOut = NULL;
    if (bmpLenOut)
        *bmpLenOut = 0;
    if (widthOut)
        *widthOut = 0;
    if (heightOut)
        *heightOut = 0;
    if (scene == NULL || bmpOut == NULL || bmpLenOut == NULL)
        return false;
    if (g_vm_mock_admin_preview_cache_bmp != NULL &&
        strcmp(g_vm_mock_admin_preview_cache_scene, scene) == 0)
    {
        *bmpOut = g_vm_mock_admin_preview_cache_bmp;
        *bmpLenOut = g_vm_mock_admin_preview_cache_bmp_len;
        if (widthOut)
            *widthOut = vm_mock_service_read_le32(g_vm_mock_admin_preview_cache_bmp + 18);
        if (heightOut)
            *heightOut = vm_mock_service_read_le32(g_vm_mock_admin_preview_cache_bmp + 22);
        return true;
    }

    memset(mapName, 0, sizeof(mapName));
    memset(&preview, 0, sizeof(preview));
    memset(images, 0, sizeof(images));
    memset(imageValid, 0, sizeof(imageValid));
    if (!vm_mock_admin_scene_map_name(scene, mapName, sizeof(mapName)) ||
        !vm_mock_admin_load_data_payload(mapName, ".map", &mapPayload,
                                         &mapPayloadLen, NULL) ||
        !vm_mock_admin_parse_map_preview(mapPayload, mapPayloadLen,
                                         mapName, &preview))
    {
        goto done;
    }
    canvas = (u16 *)calloc((size_t)preview.width * preview.height, sizeof(u16));
    if (canvas == NULL)
        goto done;

    for (u32 i = 0; i < preview.imageCount; ++i)
    {
        u8 *imagePayload = NULL;
        u32 imagePayloadLen = 0;
        u8 imageType = 0;
        int mallocSize = 0;

        if (!vm_mock_admin_load_data_payload(preview.imageNames[i], ".gif",
                                             &imagePayload, &imagePayloadLen,
                                             &imageType) || imageType != 1)
        {
            free(imagePayload);
            continue;
        }
        imageValid[i] = gifDecodeExt(imagePayload, &images[i], 1,
                                     &mallocSize) != 0 &&
                        images[i].pixels != NULL &&
                        images[i].width != 0 && images[i].height != 0;
        free(imagePayload);
    }

    for (u32 index = 0; index < preview.cellCount; ++index)
    {
        u32 packed = vm_mock_service_read_le32(preview.cells + index * 4);
        u32 imageIndex = (packed >> 24) & 0x0fu;
        u32 tileIndex = packed & 0x00ffffffu;
        u32 gridX = index / preview.rows;
        u32 gridY = index % preview.rows;
        u32 dstX = gridX * preview.tileWidth;
        u32 dstY = gridY * preview.tileHeight;
        u32 copyWidth = preview.tileWidth;
        u32 copyHeight = preview.tileHeight;
        bool copied = false;

        if (dstX + copyWidth > preview.width)
            copyWidth = preview.width - dstX;
        if (dstY + copyHeight > preview.height)
            copyHeight = preview.height - dstY;
        if (imageIndex < preview.imageCount && imageValid[imageIndex])
        {
            GifOutput *image = &images[imageIndex];
            u32 sourcePitch = image->width + ((4u - (image->width & 3u)) & 3u);
            u32 tilesPerRow = image->width / preview.tileWidth;
            u32 tilesPerCol = image->height / preview.tileHeight;
            u32 tileCount = tilesPerRow * tilesPerCol;
            if (tilesPerRow != 0 && tilesPerCol != 0 && tileIndex < tileCount)
            {
                u32 sourceX = (tileIndex % tilesPerRow) * preview.tileWidth;
                u32 sourceY = (tileIndex / tilesPerRow) * preview.tileHeight;
                for (u32 y = 0; y < copyHeight; ++y)
                {
                    memcpy(canvas + (dstY + y) * preview.width + dstX,
                           image->pixels + (sourceY + y) * sourcePitch + sourceX,
                           copyWidth * sizeof(u16));
                }
                copied = true;
            }
        }
        if (!copied)
        {
            vm_mock_admin_preview_fill_placeholder(canvas, preview.width,
                                                   preview.height, dstX, dstY,
                                                   copyWidth, copyHeight);
        }
    }

    rowBytes = (preview.width * 3u + 3u) & ~3u;
    if (preview.height > (0xffffffffu - 54u) / rowBytes)
        goto done;
    imageBytes = rowBytes * preview.height;
    bmpLen = 54u + imageBytes;
    bmp = (u8 *)malloc(bmpLen);
    if (bmp == NULL)
        goto done;
    memset(bmp, 0, bmpLen);
    bmp[0] = 'B';
    bmp[1] = 'M';
    vm_mock_admin_preview_write_le32(bmp, 2, bmpLen);
    vm_mock_admin_preview_write_le32(bmp, 10, 54);
    vm_mock_admin_preview_write_le32(bmp, 14, 40);
    vm_mock_admin_preview_write_le32(bmp, 18, preview.width);
    vm_mock_admin_preview_write_le32(bmp, 22, preview.height);
    vm_mock_admin_preview_write_le16(bmp, 26, 1);
    vm_mock_admin_preview_write_le16(bmp, 28, 24);
    vm_mock_admin_preview_write_le32(bmp, 34, imageBytes);
    vm_mock_admin_preview_write_le32(bmp, 38, 2835);
    vm_mock_admin_preview_write_le32(bmp, 42, 2835);
    for (u32 y = 0; y < preview.height; ++y)
    {
        u8 *dst = bmp + 54u + (preview.height - 1u - y) * rowBytes;
        for (u32 x = 0; x < preview.width; ++x)
        {
            u16 pixel = canvas[y * preview.width + x];
            dst[x * 3] = (u8)((pixel & 0x1fu) * 255u / 31u);
            dst[x * 3 + 1] = (u8)(((pixel >> 5) & 0x3fu) * 255u / 63u);
            dst[x * 3 + 2] = (u8)(((pixel >> 11) & 0x1fu) * 255u / 31u);
        }
    }

    free(g_vm_mock_admin_preview_cache_bmp);
    g_vm_mock_admin_preview_cache_bmp = bmp;
    g_vm_mock_admin_preview_cache_bmp_len = bmpLen;
    snprintf(g_vm_mock_admin_preview_cache_scene,
             sizeof(g_vm_mock_admin_preview_cache_scene), "%s", scene);
    bmp = NULL;
    *bmpOut = g_vm_mock_admin_preview_cache_bmp;
    *bmpLenOut = g_vm_mock_admin_preview_cache_bmp_len;
    if (widthOut)
        *widthOut = preview.width;
    if (heightOut)
        *heightOut = preview.height;
    ok = true;

done:
    for (u32 i = 0; i < VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX; ++i)
    {
        if (images[i].owned && images[i].pixels)
            free_mem(images[i].pixels);
    }
    free(canvas);
    free(mapPayload);
    free(bmp);
    return ok;
}

static void vm_mock_admin_render_content_page(char *response,
                                              size_t responseCap,
                                              const char *query)
{
    vm_mock_admin_scene_file sceneFiles[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    vm_mock_admin_scene_file actorFiles[VM_MOCK_ADMIN_ACTOR_FILE_MAX];
    vm_mock_admin_scene_file xseFiles[VM_MOCK_ADMIN_XSE_FILE_MAX];
    vm_net_mock_dynamic_npc_admin_row npcRows[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
    vm_net_mock_scene_npcinfo_seed previewNpcRows[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    vm_mock_admin_scene_portal previewPortalRows[VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX];
    vm_mock_admin_scene_preview preview;
    vm_mock_admin_text page;
    char selectedSceneUtf8[192];
    char selectedSceneFile[64];
    char runtimeScene[64];
    char status[16];
    char message[256];
    u32 sceneCount = 0;
    u32 actorCount = 0;
    u32 xseCount = 0;
    u32 npcCount = 0;
    u32 previewNpcCount = 0;
    u32 previewNpcTotal = 0;
    u32 previewDynamicCount = 0;
    u32 previewPortalCount = 0;
    u32 previewPortalTotal = 0;
    bool previewReady = false;

    memset(sceneFiles, 0, sizeof(sceneFiles));
    memset(actorFiles, 0, sizeof(actorFiles));
    memset(xseFiles, 0, sizeof(xseFiles));
    memset(npcRows, 0, sizeof(npcRows));
    memset(previewNpcRows, 0, sizeof(previewNpcRows));
    memset(previewPortalRows, 0, sizeof(previewPortalRows));
    memset(&preview, 0, sizeof(preview));
    memset(selectedSceneUtf8, 0, sizeof(selectedSceneUtf8));
    memset(selectedSceneFile, 0, sizeof(selectedSceneFile));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    vm_mock_admin_text_init(&page, response, responseCap);
    sceneCount = vm_mock_admin_collect_scene_files(
        sceneFiles, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    actorCount = vm_mock_admin_collect_actor_files(
        actorFiles, VM_MOCK_ADMIN_ACTOR_FILE_MAX);
    xseCount = vm_mock_admin_collect_xse_files(
        xseFiles, VM_MOCK_ADMIN_XSE_FILE_MAX);
    (void)vm_mock_admin_form_value(query, "scene", selectedSceneUtf8,
                                   sizeof(selectedSceneUtf8));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    if (selectedSceneUtf8[0] != 0)
    {
        (void)vm_mock_admin_utf8_to_gbk_text(selectedSceneUtf8,
                                             selectedSceneFile,
                                             sizeof(selectedSceneFile), false);
    }
    {
        bool found = false;
        for (u32 i = 0; i < sceneCount; ++i)
        {
            if (strcmp(sceneFiles[i].name, selectedSceneFile) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found && sceneCount > 0)
            snprintf(selectedSceneFile, sizeof(selectedSceneFile), "%s",
                     sceneFiles[0].name);
    }
    vm_net_mock_gbk_label_to_utf8(selectedSceneFile,
                                  selectedSceneUtf8,
                                  sizeof(selectedSceneUtf8));
    if (selectedSceneFile[0] != 0 &&
        vm_mock_admin_scene_file_to_runtime_key(selectedSceneFile,
                                                runtimeScene,
                                                sizeof(runtimeScene)))
    {
        npcCount = vm_net_mock_dynamic_npc_admin_list(
            runtimeScene, npcRows, VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX);
        previewReady = vm_mock_admin_scene_preview_info(runtimeScene, &preview);
        previewNpcCount = vm_net_mock_collect_scene_npcinfo_seeds(
            runtimeScene, previewNpcRows, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX,
            &previewNpcTotal, &previewDynamicCount);
        previewPortalCount = vm_mock_admin_collect_scene_portals(
            runtimeScene, previewPortalRows, VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX,
            &previewPortalTotal);
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 游戏内容管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1280px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 12px}.sub{color:#667085;margin:4px 0 16px}"
        ".tabs{display:flex;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}"
        ".logout{background:none;color:#667085;border:1px solid #d0d5dd}.grid{display:grid;grid-template-columns:300px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.grid>aside{display:flex;flex-direction:column;min-height:0;overflow:hidden}.grid>section{min-width:0;min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}"
        ".scene-list{display:flex;flex:1;min-height:0;flex-direction:column;gap:4px;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.scene{display:flex;justify-content:space-between;gap:8px;padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none;scroll-margin-block:12px}.scene:hover,.scene.on{background:#eef4ff;color:#175cd3}.size{color:#98a2b3;font-size:12px;white-space:nowrap}"
        ".preview{border:1px solid #d0d5dd;border-radius:9px;padding:12px;margin:0 0 16px;background:#f9fafb}.preview-head{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:10px}.map-scroll{overflow:auto;max-height:760px;padding:8px;border-radius:7px;background:#1f2937}.map-stage{position:relative;margin:auto;box-shadow:0 0 0 1px #0008;background:#111;overflow:visible}.map-stage>img{display:block;width:100%%;height:100%%;image-rendering:pixelated}.portal-box{position:absolute;z-index:1;border:2px dashed #fdb022;background:#fec84b26;pointer-events:none}.portal-box.named{border-color:#22d3ee;background:#22d3ee24}.portal-label{position:absolute;left:-2px;bottom:100%%;max-width:220px;padding:1px 4px;border-radius:3px 3px 0 0;background:#7a2e0e;color:#fff;font-size:10px;line-height:15px;white-space:nowrap}.portal-box.named .portal-label{background:#0e7490}.npc-pin{position:absolute;transform:translate(-50%%,-100%%);display:flex;flex-direction:column;align-items:center;z-index:3;filter:drop-shadow(0 1px 1px #0008);pointer-events:none}.pin-name{max-width:140px;padding:1px 4px;border-radius:3px;background:#175cd3;color:#fff;font-size:11px;line-height:16px;white-space:nowrap}.npc-pin.service .pin-name{background:#b54708}.sprite-wrap{position:relative;display:flex;align-items:flex-end;justify-content:center;min-width:18px;min-height:18px}.actor-sprite{display:block;width:auto;height:auto;max-width:72px;max-height:72px;image-rendering:pixelated}.facing-badge{position:absolute;right:-13px;bottom:0;min-width:17px;height:17px;padding:0 3px;border:1px solid #fff;border-radius:9px;background:#101828;color:#fff;font-size:11px;font-weight:700;line-height:15px;text-align:center}.preview-legend,.preview-npcs,.preview-portals{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:9px}.legend-icon{display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;border-radius:4px;background:#175cd3;color:#fff;font-size:11px}.legend-icon.service{background:#b54708}.legend-portal{width:18px;height:12px;border:2px dashed #fdb022;background:#fec84b26}.legend-portal.named{border-color:#22d3ee;background:#22d3ee24}.npc-chip,.portal-chip{font-size:12px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#344054}.npc-chip.service{background:#fff4e8}.portal-chip{background:#fffaeb;color:#7a2e0e}.portal-chip.named{background:#ecfdff;color:#0e7490}.preview-error{padding:12px;border-radius:7px;background:#fef3f2;color:#b42318;margin-bottom:16px}"
        ".notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.npc-list{display:grid;gap:12px}.npc{border:1px solid #e4e7ec;border-radius:8px;padding:13px}.npc.off{opacity:.62;background:#f9fafb}.npc-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}.badge{font-size:12px;background:#eef4ff;color:#175cd3;padding:2px 7px;border-radius:999px}.fields{display:grid;grid-template-columns:110px 1.1fr 1fr 90px 90px 90px 90px;gap:8px}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}"
        "input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.danger{background:#b42318}.enable{background:#027a48}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:10px}.new{margin-top:16px}.foot{color:#667085;font-size:12px;margin:12px 0 0}"
        "@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:18px 10px;overflow:visible}.grid{grid-template-columns:1fr;flex:none}.grid>aside,.grid>section{overflow:visible}.scene-list{flex:none;max-height:260px;overflow:auto}.fields{grid-template-columns:1fr 1fr}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">场景资源与服务端动态 NPC</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab on\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a></nav>"
        "<div class=\"grid\"><aside class=\"card\"><h2>SCE 场景（%u）</h2><div class=\"scene-list\">",
        sceneCount);
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char sceneUtf8[192];
        char encoded[512];
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name,
                                      sceneUtf8, sizeof(sceneUtf8));
        vm_mock_admin_url_encode(sceneUtf8, encoded, sizeof(encoded));
        if (strcmp(sceneFiles[i].name, selectedSceneFile) == 0)
        {
            vm_mock_admin_text_appendf(&page,
                "<a id=\"selected-scene\" class=\"scene on\" aria-current=\"page\" href=\"/?tab=content&amp;scene=%s#selected-scene\"><span>",
                encoded);
        }
        else
        {
            vm_mock_admin_text_appendf(&page,
                "<a class=\"scene\" href=\"/?tab=content&amp;scene=%s#selected-scene\"><span>",
                encoded);
        }
        vm_mock_admin_text_append_html(&page, sceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "</span><span class=\"size\">%llu B</span></a>",
            (unsigned long long)sceneFiles[i].size);
    }
    if (sceneCount == 0)
        vm_mock_admin_text_appendf(&page, "<span class=\"size\">未找到 SCE 文件</span>");
    vm_mock_admin_text_appendf(&page,
        "</div></aside><section><div class=\"card\"><h2>动态 NPC：");
    vm_mock_admin_text_append_html(&page,
                                   selectedSceneUtf8[0] ? selectedSceneUtf8 : "未选择场景");
    vm_mock_admin_text_appendf(
        &page,
        "</h2><p class=\"foot\">保存 NPC 会自动发布 Actor 及引用 GIF。客户端加载时若文件缺失，会先通过 WT 18/7 从服务端下载、校验并安装，再继续创建 NPC；后台不会向客户端目录复制文件。</p>");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (previewReady)
    {
        char encodedScene[512];
        char mapNameUtf8[192];

        vm_mock_admin_url_encode(selectedSceneUtf8, encodedScene,
                                 sizeof(encodedScene));
        vm_net_mock_gbk_label_to_utf8(preview.mapName, mapNameUtf8,
                                      sizeof(mapNameUtf8));
        vm_mock_admin_text_appendf(&page,
            "<div class=\"preview\"><div class=\"preview-head\"><strong>场景预览</strong><span class=\"size\">");
        vm_mock_admin_text_append_html(&page, mapNameUtf8);
        vm_mock_admin_text_appendf(&page,
            " · %u×%u · NPC %u%s · 传送点 %u%s</span></div>"
            "<div class=\"map-scroll\"><div class=\"map-stage\" style=\"width:%upx;height:%upx\">"
            "<img src=\"/scene-preview.bmp?scene=%s\" width=\"%u\" height=\"%u\" alt=\"场景完整地图\">",
            preview.width, preview.height, previewNpcCount,
            previewNpcTotal > previewNpcCount ? "+" : "",
            previewPortalCount,
            previewPortalTotal > previewPortalCount ? "+" : "",
            preview.width, preview.height, encodedScene,
            preview.width, preview.height);
        for (u32 i = 0; i < previewPortalCount; ++i)
        {
            const vm_mock_admin_scene_portal *portal = &previewPortalRows[i];
            char targetSceneUtf8[192];
            char displayNameUtf8[192];
            u32 markerLeft = portal->left < preview.width ? portal->left : preview.width - 1;
            u32 markerTop = portal->top < preview.height ? portal->top : preview.height - 1;
            u32 markerRight = portal->right < preview.width ? portal->right : preview.width;
            u32 markerBottom = portal->bottom < preview.height ? portal->bottom : preview.height;
            u32 markerWidth = markerRight > markerLeft ? markerRight - markerLeft : 1;
            u32 markerHeight = markerBottom > markerTop ? markerBottom - markerTop : 1;
            bool named = portal->kind == VM_MOCK_ADMIN_PORTAL_NAMED;

            vm_net_mock_gbk_label_to_utf8(portal->targetScene,
                                          targetSceneUtf8,
                                          sizeof(targetSceneUtf8));
            vm_net_mock_gbk_label_to_utf8(portal->displayName,
                                          displayNameUtf8,
                                          sizeof(displayNameUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<div class=\"portal-box%s\" data-target-scene=\"",
                named ? " named" : "");
            vm_mock_admin_text_append_html(&page, targetSceneUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "\" style=\"left:%upx;top:%upx;width:%upx;height:%upx\" title=\"",
                markerLeft, markerTop, markerWidth, markerHeight);
            if (displayNameUtf8[0] != 0)
            {
                vm_mock_admin_text_append_html(&page, displayNameUtf8);
                vm_mock_admin_text_appendf(&page, " · ");
            }
            vm_mock_admin_text_appendf(&page, "目标场景：");
            vm_mock_admin_text_append_html(&page, targetSceneUtf8);
            if (portal->entryId == 0xffff)
            {
                vm_mock_admin_text_appendf(
                    &page,
                    " · 入口 -- → %u · 区域 (%u,%u)-(%u,%u)\"><span class=\"portal-label\">",
                    portal->targetEntryId, portal->left, portal->top,
                    portal->right, portal->bottom);
            }
            else
            {
                vm_mock_admin_text_appendf(
                    &page,
                    " · 入口 %u → %u · 区域 (%u,%u)-(%u,%u)\"><span class=\"portal-label\">",
                    portal->entryId, portal->targetEntryId,
                    portal->left, portal->top, portal->right, portal->bottom);
            }
            if (displayNameUtf8[0] != 0)
            {
                vm_mock_admin_text_append_html(&page, displayNameUtf8);
                vm_mock_admin_text_appendf(&page, " → ");
            }
            else
            {
                vm_mock_admin_text_appendf(&page, "→ ");
            }
            vm_mock_admin_text_append_html(&page, targetSceneUtf8);
            vm_mock_admin_text_appendf(&page, "</span></div>");
        }
        for (u32 i = 0; i < previewNpcCount; ++i)
        {
            const vm_net_mock_scene_npcinfo_seed *seed = &previewNpcRows[i];
            char npcNameUtf8[128];
            char actorEncoded[256];
            u32 markerX = seed->x < preview.width ? seed->x : preview.width - 1;
            u32 markerY = seed->y < preview.height ? seed->y : preview.height - 1;
            bool serviceNpc = i < previewDynamicCount;
            bool outside = seed->x >= preview.width || seed->y >= preview.height;

            vm_mock_admin_url_encode(seed->actorResource, actorEncoded,
                                     sizeof(actorEncoded));
            vm_net_mock_gbk_label_to_utf8(
                seed->displayName[0] ? seed->displayName : "NPC",
                npcNameUtf8, sizeof(npcNameUtf8));
            vm_mock_admin_text_appendf(&page,
                "<div class=\"npc-pin%s\" style=\"left:%upx;top:%upx\" title=\"",
                serviceNpc ? " service" : "", markerX, markerY);
            vm_mock_admin_text_append_html(&page, npcNameUtf8);
            vm_mock_admin_text_appendf(&page,
                " · (%u,%u) · 朝向 %u %s · ",
                seed->x, seed->y,
                seed->orientation,
                vm_mock_admin_orientation_name(seed->orientation));
            vm_mock_admin_text_append_html(&page, seed->actorResource);
            vm_mock_admin_text_appendf(&page,
                " · %s%s\"><span class=\"pin-name\">",
                serviceNpc ? "服务端动态" : "SCE 内置",
                outside ? " · 坐标越界" : "");
            vm_mock_admin_text_append_html(&page, npcNameUtf8);
            vm_mock_admin_text_appendf(&page,
                "</span><span class=\"sprite-wrap\"><img class=\"actor-sprite\" src=\"/actor-preview.svg?actor=%s\" alt=\"",
                actorEncoded);
            vm_mock_admin_text_append_html(&page, npcNameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                " NPC 模型\"><span class=\"facing-badge\" title=\"朝向 %u %s\">%s%u</span></span></div>",
                seed->orientation,
                vm_mock_admin_orientation_name(seed->orientation),
                vm_mock_admin_orientation_arrow(seed->orientation),
                seed->orientation);
        }
        vm_mock_admin_text_appendf(&page,
            "</div></div><div class=\"preview-legend\"><span class=\"legend-icon service\">人</span><span class=\"size\">服务端动态 NPC</span>"
            "<span class=\"legend-icon\">人</span><span class=\"size\">SCE 内置 NPC</span>"
            "<span class=\"legend-portal\"></span><span class=\"size\">边界/元数据传送点</span>"
            "<span class=\"legend-portal named\"></span><span class=\"size\">具名传送点</span></div>");
        if (previewNpcCount != 0)
            vm_mock_admin_text_appendf(&page, "<div class=\"preview-npcs\">");
        for (u32 i = 0; i < previewNpcCount; ++i)
        {
            const vm_net_mock_scene_npcinfo_seed *seed = &previewNpcRows[i];
            char npcNameUtf8[128];
            bool serviceNpc = i < previewDynamicCount;

            vm_net_mock_gbk_label_to_utf8(
                seed->displayName[0] ? seed->displayName : "NPC",
                npcNameUtf8, sizeof(npcNameUtf8));
            vm_mock_admin_text_appendf(&page,
                "<span class=\"npc-chip%s\">",
                serviceNpc ? " service" : "");
            vm_mock_admin_text_append_html(&page, npcNameUtf8);
            vm_mock_admin_text_appendf(
                &page, " (%u,%u) · 朝向 %u %s · %s</span>",
                seed->x, seed->y, seed->orientation,
                vm_mock_admin_orientation_name(seed->orientation),
                seed->actorResource);
        }
        if (previewNpcCount != 0)
            vm_mock_admin_text_appendf(&page, "</div>");
        if (previewPortalCount != 0)
            vm_mock_admin_text_appendf(&page, "<div class=\"preview-portals\">");
        for (u32 i = 0; i < previewPortalCount; ++i)
        {
            const vm_mock_admin_scene_portal *portal = &previewPortalRows[i];
            char targetSceneUtf8[192];
            char displayNameUtf8[192];
            bool named = portal->kind == VM_MOCK_ADMIN_PORTAL_NAMED;

            vm_net_mock_gbk_label_to_utf8(portal->targetScene,
                                          targetSceneUtf8,
                                          sizeof(targetSceneUtf8));
            vm_net_mock_gbk_label_to_utf8(portal->displayName,
                                          displayNameUtf8,
                                          sizeof(displayNameUtf8));
            vm_mock_admin_text_appendf(&page,
                "<span class=\"portal-chip%s\">",
                named ? " named" : "");
            if (displayNameUtf8[0] != 0)
            {
                vm_mock_admin_text_append_html(&page, displayNameUtf8);
                vm_mock_admin_text_appendf(&page, " → ");
            }
            vm_mock_admin_text_append_html(&page, targetSceneUtf8);
            if (portal->entryId == 0xffff)
            {
                vm_mock_admin_text_appendf(
                    &page, " · 入口 --→%u · (%u,%u)-(%u,%u)</span>",
                    portal->targetEntryId, portal->left, portal->top,
                    portal->right, portal->bottom);
            }
            else
            {
                vm_mock_admin_text_appendf(
                    &page, " · 入口 %u→%u · (%u,%u)-(%u,%u)</span>",
                    portal->entryId, portal->targetEntryId,
                    portal->left, portal->top, portal->right, portal->bottom);
            }
        }
        if (previewPortalCount != 0)
            vm_mock_admin_text_appendf(&page, "</div>");
        if (previewNpcTotal > previewNpcCount)
        {
            vm_mock_admin_text_appendf(&page,
                "<p class=\"foot\">NPC 目录共 %u 项，当前预览显示前 %u 项。</p>",
                previewNpcTotal, previewNpcCount);
        }
        if (previewPortalTotal > previewPortalCount)
        {
            vm_mock_admin_text_appendf(&page,
                "<p class=\"foot\">传送点共 %u 项，当前预览显示前 %u 项。</p>",
                previewPortalTotal, previewPortalCount);
        }
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    else
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"preview-error\">该 SCE 引用的地图资源无法解析，暂时不能生成预览。</div>");
    }
    vm_mock_admin_text_appendf(&page, "<div class=\"npc-list\">");
    for (u32 i = 0; i < npcCount; ++i)
    {
        const vm_net_mock_dynamic_npc_admin_row *row = &npcRows[i];
        char displayUtf8[128];
        vm_net_mock_gbk_label_to_utf8(row->seed.displayName,
                                      displayUtf8, sizeof(displayUtf8));
        vm_mock_admin_text_appendf(&page,
            "<div class=\"npc%s\"><div class=\"npc-head\"><strong>Actor %u</strong><span>",
            row->enabled ? "" : " off", row->seed.actorId);
        if (row->builtin)
            vm_mock_admin_text_appendf(&page, "<span class=\"badge\">内置%s</span> ",
                                       row->overridden ? "·已覆盖" : "");
        vm_mock_admin_text_appendf(&page, "<span class=\"badge\">%s</span></span></div>",
                                   row->enabled ? "已启用" : "已停用");
        vm_mock_admin_text_appendf(&page,
            "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-npc\">"
            "<input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "\"><div class=\"fields\"><label class=\"field\"><span>Actor ID</span><input name=\"actor_id\" value=\"%u\" readonly></label>"
            "<label class=\"field\"><span>显示名称</span><input name=\"display_name\" value=\"",
            row->seed.actorId);
        vm_mock_admin_text_append_html(&page, displayUtf8);
        vm_mock_admin_text_appendf(&page,
            "\" maxlength=\"29\" required></label><label class=\"field\"><span>Actor 资源</span>");
        vm_mock_admin_render_actor_select(&page, actorFiles, actorCount,
                                          row->seed.actorResource);
        vm_mock_admin_text_appendf(&page,
            "</label>"
            "<label class=\"field\"><span>X</span><input type=\"number\" name=\"x\" min=\"1\" max=\"65535\" value=\"%u\" required></label>"
            "<label class=\"field\"><span>Y</span><input type=\"number\" name=\"y\" min=\"1\" max=\"65535\" value=\"%u\" required></label>"
            "<label class=\"field\"><span>服务类型</span>",
            row->seed.x, row->seed.y);
        vm_mock_admin_render_npc_kind_select(&page, row->seed.kind);
        vm_mock_admin_text_appendf(&page,
            "</label><label class=\"field\"><span>朝向</span><input type=\"number\" name=\"orientation\" min=\"0\" max=\"65535\" value=\"%u\"></label></div>"
            "<label class=\"field\" style=\"margin-top:8px\"><span>XSE 脚本（可留空）</span>",
            row->seed.orientation);
        vm_mock_admin_render_xse_select(&page, xseFiles, xseCount,
                                        row->seed.scriptName);
        vm_mock_admin_text_appendf(&page,
            "</label><label class=\"field\" style=\"margin-top:8px\"><span>可接取任务（可留空）</span>");
        vm_mock_admin_render_npc_task_select(&page, row->seed.taskId);
        vm_mock_admin_text_appendf(&page,
            "</label><div class=\"actions\"><button type=\"submit\">保存修改</button></div></form>"
            "<form method=\"post\" action=\"/action\" class=\"actions\"><input type=\"hidden\" name=\"action\" value=\"toggle-npc\">"
            "<input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\"><input type=\"hidden\" name=\"enabled\" value=\"%u\">"
            "<button class=\"%s\" type=\"submit\">%s</button></form>",
            row->seed.actorId, row->enabled ? 0u : 1u,
            row->enabled ? "danger" : "enable",
            row->enabled ? "停用 NPC" : "恢复 NPC");
        if (row->overridden)
        {
            vm_mock_admin_text_appendf(&page,
                "<form method=\"post\" action=\"/action\" class=\"actions\"><input type=\"hidden\" name=\"action\" value=\"delete-npc-override\">"
                "<input type=\"hidden\" name=\"scene\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\">"
                "<button class=\"danger\" type=\"submit\">%s</button></form>",
                row->seed.actorId,
                row->builtin ? "恢复内置默认" : "删除自定义 NPC");
        }
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (npcCount == 0)
        vm_mock_admin_text_appendf(&page, "<p class=\"size\">该场景没有服务端动态 NPC。</p>");
    vm_mock_admin_text_appendf(&page,
        "</div><div class=\"npc new\"><div class=\"npc-head\"><strong>增加动态 NPC</strong><span class=\"badge\">下次进入场景生效</span></div>"
        "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-npc\">"
        "<input type=\"hidden\" name=\"scene\" value=\"");
    vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
    vm_mock_admin_text_appendf(&page,
        "\"><div class=\"fields\"><label class=\"field\"><span>Actor ID</span><input type=\"number\" name=\"actor_id\" min=\"1\" max=\"4294967295\" value=\"30000\" required></label>"
        "<label class=\"field\"><span>显示名称</span><input name=\"display_name\" maxlength=\"29\" required></label>"
        "<label class=\"field\"><span>Actor 资源</span>");
    vm_mock_admin_render_actor_select(&page, actorFiles, actorCount,
                                      "n_man1.actor");
    vm_mock_admin_text_appendf(&page,
        "</label>"
        "<label class=\"field\"><span>X</span><input type=\"number\" name=\"x\" min=\"1\" max=\"65535\" required></label>"
        "<label class=\"field\"><span>Y</span><input type=\"number\" name=\"y\" min=\"1\" max=\"65535\" required></label>"
        "<label class=\"field\"><span>服务类型</span>");
    vm_mock_admin_render_npc_kind_select(&page, VM_NET_MOCK_NPC_KIND_NORMAL);
    vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\"><span>朝向</span><input type=\"number\" name=\"orientation\" min=\"0\" max=\"65535\" value=\"0\"></label></div>"
        "<label class=\"field\" style=\"margin-top:8px\"><span>XSE 脚本（可留空）</span>");
    vm_mock_admin_render_xse_select(&page, xseFiles, xseCount, NULL);
    vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\" style=\"margin-top:8px\"><span>可接取任务（可留空）</span>");
    vm_mock_admin_render_npc_task_select(&page, 0);
    vm_mock_admin_text_appendf(&page,
        "</label><div class=\"actions\"><button type=\"submit\">增加 NPC</button></div></form></div>"
        "<p class=\"foot\">服务类型决定对话中的可操作入口：武器商人先按剑、匕首、法杖分类；防具商人提供头盔、衣甲、披风、腰带、护腿、鞋靴和戒指；药品商人提供 item.dsh 类别 10 的药品与消耗品。商品价格和上架状态均来自后台商品目录。装备修理按实际耐久收费；技能导师只列出当前职业、等级可学且尚未学习的技能。SCE 文件中的内置 NPC 不会被改写。客户端同场景最多安全显示 4 个动态名称，超出时仍按任务优先级筛选。</p>"
        "</div></section></div></main></body></html>");

    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><title>响应过大</title><p>游戏内容页面超过大小限制。</p>");
    }
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

static const char *vm_mock_admin_item_category_name(bool equipment, u8 category)
{
    if (equipment)
    {
        switch (category)
        {
        case 0: return "头盔";
        case 1: return "衣甲";
        case 2: return "披风";
        case 3: return "腰带";
        case 4: return "护腿";
        case 5: return "鞋靴";
        case 6: return "戒指";
        case 7: return "剑";
        case 8: return "匕首";
        case 9: return "法杖";
        default: return "其他装备";
        }
    }
    switch (category)
    {
    case 10: return "药品与消耗品";
    case 11: return "任务物品";
    case 12: return "采集材料";
    case 13: return "普通材料";
    case 14: return "商城道具";
    case 20: return "礼包";
    case 21: return "活动道具";
    case 22: return "徽章";
    case 23: return "玄晶";
    case 24: return "鲜花";
    case 25: return "社交道具";
    case 26: return "婚姻道具";
    case 27: return "帮派资源";
    default: return "其他物品";
    }
}

static void vm_mock_admin_render_item_grant_form(
    vm_mock_admin_text *page, const char *account,
    const u32 *roleIds, char roleNames[][128], u32 roleCount)
{
    bool equipmentCategories[256];
    bool itemCategories[256];
    u32 itemCount = vm_net_mock_load_shop_catalog();

    if (page == NULL || account == NULL || account[0] == 0 ||
        roleIds == NULL || roleNames == NULL || roleCount == 0)
    {
        return;
    }
    memset(equipmentCategories, 0, sizeof(equipmentCategories));
    memset(itemCategories, 0, sizeof(itemCategories));
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
        if (item->isEquip)
            equipmentCategories[item->category] = true;
        else
            itemCategories[item->category] = true;
    }

    vm_mock_admin_text_appendf(
        page,
        "<div class=\"item-grant\"><h2>给予物品</h2>"
        "<form class=\"grant-form\" method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"grant-item\">"
        "<input type=\"hidden\" name=\"account\" value=\"");
    vm_mock_admin_text_append_html(page, account);
    vm_mock_admin_text_appendf(
        page,
        "\"><label><span>角色</span><select name=\"role\" required>");
    for (u32 i = 0; i < roleCount; ++i)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"%u\">", roleIds[i]);
        vm_mock_admin_text_append_html(page, roleNames[i]);
        vm_mock_admin_text_appendf(page, "（ID %u）</option>", roleIds[i]);
    }
    vm_mock_admin_text_appendf(
        page,
        "</select></label><label><span>物品分类</span>"
        "<select id=\"item-category\"><option value=\"all\">全部分类</option>");
    for (u32 category = 0; category < 256; ++category)
    {
        if (!equipmentCategories[category])
            continue;
        vm_mock_admin_text_appendf(page, "<option value=\"e%u\">装备 · ", category);
        vm_mock_admin_text_append_html(
            page, vm_mock_admin_item_category_name(true, (u8)category));
        vm_mock_admin_text_appendf(page, "（%u）</option>", category);
    }
    for (u32 category = 0; category < 256; ++category)
    {
        if (!itemCategories[category])
            continue;
        vm_mock_admin_text_appendf(page, "<option value=\"i%u\">物品 · ", category);
        vm_mock_admin_text_append_html(
            page, vm_mock_admin_item_category_name(false, (u8)category));
        vm_mock_admin_text_appendf(page, "（%u）</option>", category);
    }
    vm_mock_admin_text_appendf(
        page,
        "</select></label><label class=\"item-field\"><span>物品</span>"
        "<select id=\"item-select\" name=\"item\" required>"
        "<option value=\"\" selected disabled>请选择物品</option>");
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
        char itemNameUtf8[128];

        vm_net_mock_gbk_label_to_utf8(item->name, itemNameUtf8,
                                      sizeof(itemNameUtf8));
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\" data-category=\"%c%u\">[%u] ",
            item->itemId, item->isEquip ? 'e' : 'i', item->category,
            item->itemId);
        vm_mock_admin_text_append_html(page, itemNameUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(
        page,
        "</select></label><label><span>数量</span>"
        "<input type=\"number\" name=\"amount\" min=\"1\" max=\"255\" value=\"1\" required>"
        "</label><button type=\"submit\">给予物品</button></form>"
        "<p class=\"muted grant-note\">相同物品会叠加；新物品需要背包存在空位。装备也遵循现有背包存储规则。</p></div>");
}

static bool vm_mock_admin_shop_category_filter(const char *filter,
                                                bool *equipmentOut,
                                                u8 *categoryOut)
{
    u32 category = 0;

    if (filter == NULL || (filter[0] != 'e' && filter[0] != 'i') ||
        filter[1] == 0 ||
        !vm_net_mock_parse_u32_strict(filter + 1, &category) || category > 255)
    {
        return false;
    }
    if (equipmentOut)
        *equipmentOut = filter[0] == 'e';
    if (categoryOut)
        *categoryOut = (u8)category;
    return true;
}

static bool vm_mock_admin_shop_is_secret_treasure(
    const vm_net_mock_shop_catalog_item *item)
{
    return item != NULL && !item->isEquip && item->category == 14;
}

static bool vm_mock_admin_shop_is_divine_arms(
    const vm_net_mock_shop_catalog_item *item)
{
    u8 slot;

    if (item == NULL || !item->isEquip)
        return false;
    slot = vm_net_mock_shop_page_equipment_slot(item);
    return slot <= 7;
}

static const char *vm_mock_admin_shop_section_name(
    const vm_net_mock_shop_catalog_item *item)
{
    if (vm_mock_admin_shop_is_secret_treasure(item))
        return "秘宝道具";
    if (vm_mock_admin_shop_is_divine_arms(item))
        return "神兵利器";
    return "普通目录";
}

static bool vm_mock_admin_shop_item_matches(
    const vm_net_mock_shop_catalog_item *item,
    bool filterCategory, bool filterEquipment, u8 category,
    bool filterSecretTreasure, bool filterDivineArms,
    const char *search)
{
    char itemNameUtf8[128];
    char itemIdText[32];

    if (item == NULL)
        return false;
    if (filterCategory &&
        ((item->isEquip != 0) != filterEquipment || item->category != category))
    {
        return false;
    }
    if (filterSecretTreasure &&
        !vm_mock_admin_shop_is_secret_treasure(item))
    {
        return false;
    }
    if (filterDivineArms && !vm_mock_admin_shop_is_divine_arms(item))
        return false;
    if (search == NULL || search[0] == 0)
        return true;
    memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
    vm_net_mock_gbk_label_to_utf8(item->name, itemNameUtf8,
                                  sizeof(itemNameUtf8));
    snprintf(itemIdText, sizeof(itemIdText), "%u", item->itemId);
    return strstr(itemNameUtf8, search) != NULL ||
           strstr(itemIdText, search) != NULL;
}

static void vm_mock_admin_render_shop_page(char *response,
                                           size_t responseCap,
                                           const char *query)
{
    vm_mock_admin_text page;
    bool equipmentCategories[256];
    bool itemCategories[256];
    char categoryFilter[16];
    char search[128];
    char pageText[32];
    char status[16];
    char message[256];
    char categoryEncoded[64];
    char searchEncoded[384];
    bool filterCategory = false;
    bool filterEquipment = false;
    bool filterSecretTreasure = false;
    bool filterDivineArms = false;
    u8 filterCategoryValue = 0;
    u32 itemCount = vm_net_mock_load_shop_catalog();
    u32 matchedCount = 0;
    u32 enabledCount = 0;
    u32 disabledCount = 0;
    u32 secretTreasureCount = 0;
    u32 secretTreasureEnabledCount = 0;
    u32 divineArmsCount = 0;
    u32 divineArmsEnabledCount = 0;
    u32 pageNumber = 1;
    u32 pageCount = 1;
    u32 rowStart = 0;
    u32 rowEnd = 0;
    u32 matchedIndex = 0;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(equipmentCategories, 0, sizeof(equipmentCategories));
    memset(itemCategories, 0, sizeof(itemCategories));
    memset(categoryFilter, 0, sizeof(categoryFilter));
    memset(search, 0, sizeof(search));
    memset(pageText, 0, sizeof(pageText));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "category", categoryFilter,
                                   sizeof(categoryFilter));
    (void)vm_mock_admin_form_value(query, "q", search, sizeof(search));
    (void)vm_mock_admin_form_value(query, "page", pageText, sizeof(pageText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    if (strcmp(categoryFilter, "secret") == 0)
        filterSecretTreasure = true;
    else if (strcmp(categoryFilter, "arsenal") == 0)
        filterDivineArms = true;
    else
        filterCategory = vm_mock_admin_shop_category_filter(
            categoryFilter, &filterEquipment, &filterCategoryValue);
    if (!filterCategory && !filterSecretTreasure && !filterDivineArms)
        snprintf(categoryFilter, sizeof(categoryFilter), "all");
    if (pageText[0] != 0 &&
        (!vm_net_mock_parse_u32_strict(pageText, &pageNumber) || pageNumber == 0))
    {
        pageNumber = 1;
    }

    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        if (item->isEquip)
            equipmentCategories[item->category] = true;
        else
            itemCategories[item->category] = true;
        if (item->enabled)
            ++enabledCount;
        else
            ++disabledCount;
        if (vm_mock_admin_shop_is_secret_treasure(item))
        {
            ++secretTreasureCount;
            if (item->enabled)
                ++secretTreasureEnabledCount;
        }
        if (vm_mock_admin_shop_is_divine_arms(item))
        {
            ++divineArmsCount;
            if (item->enabled)
                ++divineArmsEnabledCount;
        }
        if (vm_mock_admin_shop_item_matches(
                item, filterCategory, filterEquipment, filterCategoryValue,
                filterSecretTreasure, filterDivineArms,
                search))
        {
            ++matchedCount;
        }
    }
    if (matchedCount != 0)
        pageCount = (matchedCount + VM_MOCK_ADMIN_SHOP_PAGE_SIZE - 1) /
                    VM_MOCK_ADMIN_SHOP_PAGE_SIZE;
    if (pageNumber > pageCount)
        pageNumber = pageCount;
    rowStart = (pageNumber - 1) * VM_MOCK_ADMIN_SHOP_PAGE_SIZE;
    rowEnd = rowStart + VM_MOCK_ADMIN_SHOP_PAGE_SIZE;
    if (rowEnd > matchedCount)
        rowEnd = matchedCount;
    vm_mock_admin_url_encode(categoryFilter, categoryEncoded,
                             sizeof(categoryEncoded));
    vm_mock_admin_url_encode(search, searchEncoded, sizeof(searchEncoded));

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 商品管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1240px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}.sub,.muted{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;gap:6px;margin:0 0 14px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}"
        ".card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.shop-card{display:flex;flex-direction:column;min-height:0;flex:1}.summary{display:flex;gap:9px;flex-wrap:wrap;margin-bottom:12px}.badge{padding:3px 8px;border-radius:999px;background:#eef4ff;color:#175cd3}.badge.secret{background:#fff4e5;color:#b54708}.badge.arms{background:#f4f3ff;color:#5925dc}.badge.off{background:#fef3f2;color:#b42318}.filters{display:grid;grid-template-columns:minmax(190px,.7fr) minmax(220px,1.2fr) auto;gap:9px;align-items:end;margin-bottom:12px}.filters label{display:grid;gap:4px}.filters span{font-size:12px;color:#667085}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.shop-list{min-height:0;flex:1;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;border:1px solid #eaecf0;border-radius:8px}table{border-collapse:collapse;width:100%%}th,td{text-align:left;padding:10px 9px;border-bottom:1px solid #eaecf0;vertical-align:middle}th{position:sticky;top:0;background:#f9fafb;color:#667085;z-index:1}.name{min-width:200px}.section{display:inline-block;padding:2px 7px;border-radius:999px;background:#f2f4f7;color:#475467;font-size:12px;white-space:nowrap}.section.secret{background:#fff4e5;color:#b54708}.section.arms{background:#f4f3ff;color:#5925dc}.row-form{display:grid;grid-template-columns:150px 100px auto;gap:7px;align-items:center}.state{font-size:12px;font-weight:600}.state.on{color:#027a48}.state.off{color:#b42318}.pages{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:12px}.page-links{display:flex;gap:7px}.page-links a{padding:6px 10px;border:1px solid #d0d5dd;border-radius:6px;color:#344054;text-decoration:none}.notice{padding:10px 12px;border-radius:7px;margin-bottom:12px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.foot{font-size:12px;color:#667085;margin:10px 0 0}"
        "@media(max-width:760px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:16px 9px;overflow:visible}.shop-card{min-height:700px}.filters,.row-form{grid-template-columns:1fr}.shop-list{min-height:520px}.tabs{overflow:auto}.name{min-width:150px}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">商品价格与上下架状态 · 保存后立即生效</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab on\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a></nav>"
        "<section class=\"card shop-card\">");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(
        &page,
        "<div class=\"summary\"><span class=\"badge\">目录 %u</span><span class=\"badge\">已上架 %u</span><span class=\"badge off\">已下架 %u</span>"
        "<span class=\"badge secret\">秘宝道具 %u（上架 %u）</span><span class=\"badge arms\">神兵利器 %u（上架 %u）</span></div>"
        "<form class=\"filters\" method=\"get\" action=\"/\"><input type=\"hidden\" name=\"tab\" value=\"shop\">"
        "<label><span>商城分区 / 物品分类</span><select name=\"category\"><option value=\"all\"%s>全部商品</option>"
        "<option value=\"secret\"%s>商城 · 秘宝道具</option><option value=\"arsenal\"%s>商城 · 神兵利器</option>",
        itemCount, enabledCount, disabledCount,
        secretTreasureCount, secretTreasureEnabledCount,
        divineArmsCount, divineArmsEnabledCount,
        filterCategory || filterSecretTreasure || filterDivineArms ? "" : " selected",
        filterSecretTreasure ? " selected" : "",
        filterDivineArms ? " selected" : "");
    for (u32 category = 0; category < 256; ++category)
    {
        if (!equipmentCategories[category])
            continue;
        vm_mock_admin_text_appendf(
            &page, "<option value=\"e%u\"%s>装备 · ", category,
            filterCategory && filterEquipment &&
                    filterCategoryValue == category
                ? " selected" : "");
        vm_mock_admin_text_append_html(
            &page, vm_mock_admin_item_category_name(true, (u8)category));
        vm_mock_admin_text_appendf(&page, "（%u）</option>", category);
    }
    for (u32 category = 0; category < 256; ++category)
    {
        if (!itemCategories[category])
            continue;
        vm_mock_admin_text_appendf(
            &page, "<option value=\"i%u\"%s>物品 · ", category,
            filterCategory && !filterEquipment && filterCategoryValue == category
                ? " selected" : "");
        vm_mock_admin_text_append_html(
            &page, vm_mock_admin_item_category_name(false, (u8)category));
        vm_mock_admin_text_appendf(&page, "（%u）</option>", category);
    }
    vm_mock_admin_text_appendf(&page,
        "</select></label><label><span>名称或物品 ID</span><input type=\"search\" name=\"q\" value=\"");
    vm_mock_admin_text_append_html(&page, search);
    vm_mock_admin_text_appendf(&page,
        "\" placeholder=\"输入名称或 ID\"></label><button type=\"submit\">筛选</button></form>"
        "<div class=\"shop-list\"><table><thead><tr><th>ID / 名称</th><th>商城分区</th><th>DSH 分类</th><th>当前状态</th><th>价格与操作</th></tr></thead><tbody>");

    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        char itemNameUtf8[128];

        if (!vm_mock_admin_shop_item_matches(
                item, filterCategory, filterEquipment, filterCategoryValue,
                filterSecretTreasure, filterDivineArms,
                search))
        {
            continue;
        }
        if (matchedIndex < rowStart || matchedIndex >= rowEnd)
        {
            ++matchedIndex;
            continue;
        }
        ++matchedIndex;
        memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
        vm_net_mock_gbk_label_to_utf8(item->name, itemNameUtf8,
                                      sizeof(itemNameUtf8));
        vm_mock_admin_text_appendf(&page, "<tr><td class=\"name\"><strong>[%u] ",
                                   item->itemId);
        vm_mock_admin_text_append_html(&page, itemNameUtf8);
        vm_mock_admin_text_appendf(
            &page, "</strong></td><td><span class=\"section %s\">%s</span></td><td>%s · ",
            vm_mock_admin_shop_is_secret_treasure(item) ? "secret" :
                (vm_mock_admin_shop_is_divine_arms(item) ? "arms" : ""),
            vm_mock_admin_shop_section_name(item),
            item->isEquip ? "装备" : "物品");
        vm_mock_admin_text_append_html(
            &page, vm_mock_admin_item_category_name(item->isEquip != 0,
                                                    item->category));
        vm_mock_admin_text_appendf(
            &page, "（%u）</td><td><span class=\"state %s\">%s</span></td><td>"
            "<form class=\"row-form\" method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-shop-item\">"
            "<input type=\"hidden\" name=\"item\" value=\"%u\">"
            "<input type=\"hidden\" name=\"category\" value=\"",
            item->category, item->enabled ? "on" : "off",
            item->enabled ? "已上架" : "已下架", item->itemId);
        vm_mock_admin_text_append_html(&page, categoryFilter);
        vm_mock_admin_text_appendf(&page, "\"><input type=\"hidden\" name=\"q\" value=\"");
        vm_mock_admin_text_append_html(&page, search);
        vm_mock_admin_text_appendf(
            &page, "\"><input type=\"hidden\" name=\"page\" value=\"%u\">"
            "<input aria-label=\"商品价格\" type=\"number\" name=\"price\" min=\"1\" max=\"4294967295\" value=\"%u\" required>"
            "<select aria-label=\"上下架状态\" name=\"enabled\"><option value=\"1\"%s>上架</option><option value=\"0\"%s>下架</option></select>"
            "<button type=\"submit\">保存</button></form>%s</td></tr>",
            pageNumber, item->price, item->enabled ? " selected" : "",
            item->enabled ? "" : " selected",
            item->itemId == VM_NET_MOCK_BACKPACK_EXPAND_ITEM_ID
                ? "<div class=\"foot\">背包扩容的实际价格由客户端容量档位决定。</div>"
                : "");
    }
    if (matchedCount == 0)
        vm_mock_admin_text_appendf(
            &page, "<tr><td colspan=\"5\" class=\"muted\">没有符合条件的物品</td></tr>");
    vm_mock_admin_text_appendf(&page,
        "</tbody></table></div><div class=\"pages\"><span>第 %u / %u 页 · 共 %u 项</span><div class=\"page-links\">",
        pageNumber, pageCount, matchedCount);
    if (pageNumber > 1)
        vm_mock_admin_text_appendf(
            &page, "<a href=\"/?tab=shop&amp;category=%s&amp;q=%s&amp;page=%u\">上一页</a>",
            categoryEncoded, searchEncoded, pageNumber - 1);
    if (pageNumber < pageCount)
        vm_mock_admin_text_appendf(
            &page, "<a href=\"/?tab=shop&amp;category=%s&amp;q=%s&amp;page=%u\">下一页</a>",
            categoryEncoded, searchEncoded, pageNumber + 1);
    vm_mock_admin_text_appendf(
        &page,
        "</div></div><p class=\"foot\">价格和上下架状态保存在 MySQL server_shop_items 表。价格覆盖用于服务端下发价格的商城页面；17/1 NPC 商店的显示价格仍由客户端本地 DSH 决定。下架不会删除角色已有物品，也不会影响后台赠送物品。</p></section></main></body></html>");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><title>响应过大</title><p>商品管理页面超过大小限制。</p>");
    }
}

static void vm_mock_admin_render_update_page(char *response,
                                             size_t responseCap,
                                             const char *query)
{
    vm_mock_admin_text page;
    vm_mock_admin_scene_file *files = NULL;
    u32 fileCount = 0;
    char status[16];
    char message[256];
    char catalogPath[1200];

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(catalogPath, 0, sizeof(catalogPath));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    vm_net_mock_update_catalog_load();
    vm_net_mock_update_delivery_load();
    (void)vm_net_mock_update_state_path("server_update_catalog.tsv",
                                        catalogPath, sizeof(catalogPath));
    files = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_UPDATE_FILE_MAX, sizeof(*files));
    if (files != NULL)
        fileCount = vm_mock_admin_collect_update_files(
            files, VM_MOCK_ADMIN_UPDATE_FILE_MAX);

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 游戏内容更新管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1280px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 12px}h3{font-size:15px;margin:0 0 8px}.sub,.muted{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;gap:6px;margin:0 0 14px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}"
        ".update-grid{display:grid;grid-template-columns:minmax(390px,.95fr) minmax(440px,1.15fr);gap:16px;flex:1;min-height:0}.pane{min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d;margin-bottom:12px}.module{border:1px solid #eaecf0;border-radius:8px;padding:12px;margin-top:9px}.module-head{display:flex;justify-content:space-between;gap:10px}.badge{font-size:12px;padding:2px 7px;border-radius:999px;background:#f2f4f7;color:#475467}.badge.on{background:#ecfdf3;color:#027a48}.module form{display:grid;grid-template-columns:100px 1fr auto;gap:8px;align-items:end;margin-top:9px}.publish{display:grid;grid-template-columns:minmax(110px,.7fr) minmax(220px,1.6fr) 90px auto;gap:8px;align-items:end;margin-top:9px}.module label,.publish label{display:grid;gap:4px}.module label span,.publish label span{font-size:12px;color:#667085}.check{display:flex!important;align-items:center;gap:7px;height:36px}.check input{width:auto}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}button:hover{background:#1849a9}.danger{background:#b42318}.notice{padding:10px 12px;border-radius:7px;margin-bottom:12px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.callout{background:#eef4ff;color:#3538cd;border-radius:8px;padding:11px 12px}.published{width:100%%;border-collapse:collapse;margin-top:10px}.published th,.published td{text-align:left;padding:9px 7px;border-bottom:1px solid #eaecf0;vertical-align:middle}.published th{font-size:12px;color:#667085}.inline{display:flex;gap:7px;align-items:center}.foot{font-size:12px;color:#667085}.path{word-break:break-all;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px}"
        "@media(max-width:850px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;overflow:visible}.update-grid{grid-template-columns:1fr;flex:none}.pane{overflow:visible}.module form,.publish{grid-template-columns:1fr}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">启动 CBM 模块发布与具名资源下发</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab on\" href=\"/?tab=updates\">游戏内容更新管理</a></nav>");
    if (message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page,
        "<div class=\"update-grid\"><section class=\"pane update-left\">"
        "<div class=\"card\"><h2>启动模块更新</h2>"
        "<div class=\"callout\">客户端启动发送 WT 18/9；服务器按启用槽位返回 result 位图，客户端再以 WT 18/6 分块下载。替换 CBM 后必须修改版本号。</div>");
    for (u32 i = 0; i < VM_NET_MOCK_UPDATE_SLOT_COUNT; ++i)
    {
        const vm_net_mock_update_slot_config *slot =
            &g_vm_net_mock_update_slots[i];
        long size = -1;
        size = vm_net_mock_update_file_size(
            g_vm_net_mock_update_slot_files[i]);
        vm_mock_admin_text_appendf(&page,
            "<div class=\"module\"><div class=\"module-head\"><div><h3>槽位 %u · %s</h3><div class=\"foot\"><span class=\"path\">%s</span> · %ld 字节</div></div><span class=\"badge %s\">%s</span></div>"
            "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-update-slot\"><input type=\"hidden\" name=\"slot\" value=\"%u\">"
            "<label><span>发布版本</span><input type=\"number\" name=\"version\" min=\"1\" max=\"65535\" value=\"%u\" required></label>"
            "<label class=\"check\"><input type=\"checkbox\" name=\"enabled\" value=\"1\" %s>启动时下发</label><button type=\"submit\">保存发布设置</button></form></div>",
            i + 1, g_vm_net_mock_update_slot_labels[i],
            g_vm_net_mock_update_slot_files[i], size,
            slot->enabled ? "on" : "", slot->enabled ? "已发布" : "未发布",
            i + 1, slot->version, slot->enabled ? "checked" : "");
    }
    vm_mock_admin_text_appendf(&page,
        "</div><div class=\"card\"><h2>下发记录</h2><p class=\"muted\">当前记录 %u 个客户端标识。只有完整发送最后一块后才记为已下发；发布新版本会自动再次触发。</p>"
        "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"reset-update-delivery\"><button class=\"danger\" type=\"submit\">清空记录并让已发布模块重新下发</button></form></div>"
        "</section><section class=\"pane update-right\"><div class=\"card\"><h2>具名资源发布</h2>"
        "<p class=\"muted\">SCE、XSE、ACTOR、GIF 等资源走 WT 18/7，只会在客户端加载该资源时按名称拉取。动态 NPC 保存时会自动发布 Actor/GIF；客户端文件打开缺失时会在进入 CBE 缺资源分支前下载并安装，后台不会复制客户端文件。其他需要启动即更新的内容仍应打入对应 CBM。</p>"
        "<form class=\"publish\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"publish-named-update\">"
        "<label><span>筛选资源</span><input id=\"update-resource-filter\" type=\"search\" placeholder=\"输入文件名筛选\"></label>"
        "<label><span>服务器资源（%u）</span><select id=\"update-resource-select\" name=\"resource\" required><option value=\"\" selected disabled>请选择要发布的资源</option>",
        g_vm_net_mock_update_delivery_count, fileCount);
    for (u32 i = 0; files != NULL && i < fileCount; ++i)
    {
        char nameUtf8[256];
        vm_mock_admin_resource_name_to_utf8(files[i].name, nameUtf8,
                                            sizeof(nameUtf8));
        vm_mock_admin_text_appendf(&page, "<option value=\"");
        vm_mock_admin_text_append_html(&page, nameUtf8);
        vm_mock_admin_text_appendf(&page, "\">");
        vm_mock_admin_text_append_html(&page, nameUtf8);
        vm_mock_admin_text_appendf(&page, " · %llu 字节</option>",
                                   (unsigned long long)files[i].size);
    }
    vm_mock_admin_text_appendf(&page,
        "</select></label><label><span>响应版本</span><input type=\"number\" name=\"version\" min=\"1\" max=\"65535\" value=\"1\" required></label><button type=\"submit\">发布资源</button></form></div>"
        "<div class=\"card\"><h2>已发布具名资源（%u）</h2><table class=\"published\"><thead><tr><th>资源</th><th>版本</th><th>操作</th></tr></thead><tbody>",
        g_vm_net_mock_update_named_count);
    for (u32 i = 0; i < g_vm_net_mock_update_named_count; ++i)
    {
        const vm_net_mock_update_named_config *row =
            &g_vm_net_mock_update_named[i];
        char nameUtf8[256];
        vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
        vm_mock_admin_text_appendf(&page, "<tr><td class=\"path\">");
        vm_mock_admin_text_append_html(&page, nameUtf8);
        vm_mock_admin_text_appendf(&page, "</td><td>%u</td><td><form class=\"inline\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"remove-named-update\"><input type=\"hidden\" name=\"resource\" value=\"",
                                   row->version);
        vm_mock_admin_text_append_html(&page, nameUtf8);
        vm_mock_admin_text_appendf(&page,
            "\"><button class=\"danger\" type=\"submit\">取消发布</button></form></td></tr>");
    }
    if (g_vm_net_mock_update_named_count == 0)
        vm_mock_admin_text_appendf(&page,
            "<tr><td colspan=\"3\" class=\"muted\">尚未发布具名资源</td></tr>");
    vm_mock_admin_text_appendf(&page,
        "</tbody></table></div><div class=\"card foot\"><strong>发布配置文件</strong><div class=\"path\">");
    vm_mock_admin_text_append_html(&page,
        catalogPath[0] ? catalogPath : "资源根目录未配置");
    vm_mock_admin_text_appendf(&page,
        "</div><p>配置保存在 server_update_catalog.tsv，下发完成记录保存在 server_update_delivery.tsv。</p></div></section></div></main></body></html>");
    free(files);
}

static void vm_mock_admin_render_task_requirement_select(
    vm_mock_admin_text *page, const char *name, u8 current)
{
    static const char *labels[] = {"无", "收集物品", "击败怪物"};
    vm_mock_admin_text_appendf(page, "<select name=\"%s\">", name);
    for (u32 value = 0; value <= 2; ++value)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%u · %s</option>", value,
            current == value ? " selected" : "", value, labels[value]);
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_task_page(char *response,
                                           size_t responseCap,
                                           const char *query)
{
    vm_net_mock_task_definition tasks[VM_NET_MOCK_TASK_CATALOG_MAX];
    vm_net_mock_task_definition edit;
    vm_mock_admin_text page;
    char taskText[32];
    char newText[8];
    char status[16];
    char message[256];
    char nameUtf8[128];
    char giverUtf8[64];
    char receiverUtf8[64];
    char goalUtf8[384];
    char rewardUtf8[128];
    char offerUtf8[768];
    char activeUtf8[768];
    char completedUtf8[768];
    u32 taskCount = 0;
    u32 selectedTaskId = 0;
    bool createNew = false;
    bool found = false;

    memset(tasks, 0, sizeof(tasks));
    memset(&edit, 0, sizeof(edit));
    memset(taskText, 0, sizeof(taskText));
    memset(newText, 0, sizeof(newText));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(giverUtf8, 0, sizeof(giverUtf8));
    memset(receiverUtf8, 0, sizeof(receiverUtf8));
    memset(goalUtf8, 0, sizeof(goalUtf8));
    memset(rewardUtf8, 0, sizeof(rewardUtf8));
    memset(offerUtf8, 0, sizeof(offerUtf8));
    memset(activeUtf8, 0, sizeof(activeUtf8));
    memset(completedUtf8, 0, sizeof(completedUtf8));
    vm_mock_admin_text_init(&page, response, responseCap);
    taskCount = vm_net_mock_task_admin_list(
        tasks, VM_NET_MOCK_TASK_CATALOG_MAX);
    (void)vm_mock_admin_form_value(query, "task", taskText, sizeof(taskText));
    (void)vm_mock_admin_form_value(query, "new", newText, sizeof(newText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    createNew = strcmp(newText, "1") == 0;
    if (!createNew && taskText[0] != 0)
        (void)vm_net_mock_parse_u32_strict(taskText, &selectedTaskId);
    if (!createNew && selectedTaskId == 0 && taskCount != 0)
        selectedTaskId = tasks[0].taskId;
    for (u32 i = 0; !createNew && i < taskCount; ++i)
    {
        if (tasks[i].taskId == selectedTaskId)
        {
            edit = tasks[i];
            found = true;
            break;
        }
    }
    if (createNew)
    {
        edit.taskId = 100000;
        edit.enabled = true;
        edit.level = 1;
        found = true;
    }
    if (found)
    {
        vm_net_mock_gbk_label_to_utf8(edit.name, nameUtf8, sizeof(nameUtf8));
        vm_net_mock_gbk_label_to_utf8(edit.giver, giverUtf8, sizeof(giverUtf8));
        vm_net_mock_gbk_label_to_utf8(edit.receiver, receiverUtf8, sizeof(receiverUtf8));
        vm_net_mock_gbk_label_to_utf8(edit.goal, goalUtf8, sizeof(goalUtf8));
        vm_net_mock_gbk_label_to_utf8(edit.rewardText, rewardUtf8, sizeof(rewardUtf8));
        vm_net_mock_gbk_label_to_utf8(edit.offerDialog, offerUtf8, sizeof(offerUtf8));
        vm_net_mock_gbk_label_to_utf8(edit.activeDialog, activeUtf8, sizeof(activeUtf8));
        vm_net_mock_gbk_label_to_utf8(edit.completedDialog, completedUtf8, sizeof(completedUtf8));
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>江湖OL 任务管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1280px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column}.head{display:flex;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 12px}.sub,.hint{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff}.logout{background:#fff!important;color:#667085!important;border:1px solid #d0d5dd!important}.grid{display:grid;grid-template-columns:280px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px}.list{height:100%%;overflow:auto;display:flex;flex-direction:column;gap:4px}.task{padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none}.task:hover,.task.on{background:#eef4ff;color:#175cd3}.task.off{opacity:.55}.editor{overflow:auto}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.fields{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}input,select,textarea{width:100%%;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}textarea{min-height:68px;resize:vertical}.wide{grid-column:1/-1}.group{margin-top:14px;padding:12px;border:1px solid #e4e7ec;border-radius:8px}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}button,.button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;text-decoration:none}.danger{background:#b42318}.secondary{background:#475467}.badge{font-size:12px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#175cd3}"
        "@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto}.grid{grid-template-columns:1fr}.list{max-height:300px}.fields{grid-template-columns:1fr 1fr}}</style></head><body><main class=\"wrap\">"
        "<div class=\"head\"><div><h1>江湖OL 后台管理</h1><p class=\"sub\">任务定义、奖励与 NPC 对话</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\">退出登录</button></form></div>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab on\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a></nav>"
        "<div class=\"grid\"><aside class=\"card list\"><a class=\"button\" href=\"/?tab=tasks&amp;new=1\">＋ 新增任务</a><h2>任务目录（%u）</h2>",
        taskCount);
    for (u32 i = 0; i < taskCount; ++i)
    {
        char listNameUtf8[128];
        memset(listNameUtf8, 0, sizeof(listNameUtf8));
        vm_net_mock_gbk_label_to_utf8(tasks[i].name, listNameUtf8,
                                      sizeof(listNameUtf8));
        vm_mock_admin_text_appendf(
            &page, "<a class=\"task%s%s\" href=\"/?tab=tasks&amp;task=%u\"><strong>%u</strong> · ",
            (!createNew && tasks[i].taskId == selectedTaskId) ? " on" : "",
            tasks[i].enabled ? "" : " off", tasks[i].taskId, tasks[i].taskId);
        vm_mock_admin_text_append_html(&page, listNameUtf8);
        vm_mock_admin_text_appendf(&page, "%s</a>",
                                   tasks[i].overridden ? " · 已编辑" : "");
    }
    vm_mock_admin_text_appendf(&page, "</aside><section class=\"card editor\">");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (!found)
    {
        vm_mock_admin_text_appendf(&page, "<p>没有可编辑的任务。</p></section></div></main></body></html>");
        return;
    }
    vm_mock_admin_text_appendf(&page, "<h2>%s <span class=\"badge\">%s</span></h2><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-task\">%s<div class=\"fields\">",
                               createNew ? "新增任务" : "编辑任务",
                               edit.builtin ? (edit.overridden ? "task.dsh 覆盖" : "task.dsh 原始") : "自定义",
                               createNew ? "<input type=\"hidden\" name=\"create_new\" value=\"1\">" : "");
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>任务 ID</span><input type=\"number\" name=\"task_id\" min=\"1\" max=\"4294967295\" value=\"%u\" %s required></label>", edit.taskId, createNew ? "" : "readonly");
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\"%s>启用</option><option value=\"0\"%s>停用</option></select></label>", edit.enabled ? " selected" : "", edit.enabled ? "" : " selected");
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>要求等级</span><input type=\"number\" name=\"level\" min=\"0\" max=\"255\" value=\"%u\" required></label><label class=\"field\"><span>前置任务 ID</span><input type=\"number\" name=\"prerequisite_task_id\" min=\"0\" max=\"4294967295\" value=\"%u\"></label>", edit.level, edit.prerequisiteTaskId);
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>难度</span><input type=\"number\" name=\"difficulty\" min=\"0\" max=\"255\" value=\"%u\"></label><label class=\"field\"><span>分类</span><input type=\"number\" name=\"classification\" min=\"0\" max=\"255\" value=\"%u\"></label>", edit.difficulty, edit.classification);
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>任务名称（最多31字节）</span><input name=\"name\" maxlength=\"31\" value=\""); vm_mock_admin_text_append_html(&page, nameUtf8); vm_mock_admin_text_appendf(&page, "\" required></label><label class=\"field\"><span>发布者（最多15字节）</span><input name=\"giver\" maxlength=\"15\" value=\""); vm_mock_admin_text_append_html(&page, giverUtf8); vm_mock_admin_text_appendf(&page, "\" required></label><label class=\"field\"><span>交付者（最多15字节）</span><input name=\"receiver\" maxlength=\"15\" value=\""); vm_mock_admin_text_append_html(&page, receiverUtf8); vm_mock_admin_text_appendf(&page, "\" required></label>");
    vm_mock_admin_text_appendf(&page, "</div><div class=\"group\"><h2>任务目标</h2><div class=\"fields\"><label class=\"field\"><span>条件一类型</span>"); vm_mock_admin_render_task_requirement_select(&page, "requirement_type1", edit.requirementType1); vm_mock_admin_text_appendf(&page, "</label><label class=\"field\"><span>条件一目标 ID</span><input type=\"number\" name=\"requirement_id1\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>条件一数量</span><input type=\"number\" name=\"requirement_count1\" min=\"0\" max=\"255\" value=\"%u\"></label>", edit.requirementId1, edit.requirementCount1);
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>条件二类型</span>"); vm_mock_admin_render_task_requirement_select(&page, "requirement_type2", edit.requirementType2); vm_mock_admin_text_appendf(&page, "</label><label class=\"field\"><span>条件二目标 ID</span><input type=\"number\" name=\"requirement_id2\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>条件二数量</span><input type=\"number\" name=\"requirement_count2\" min=\"0\" max=\"255\" value=\"%u\"></label><label class=\"field wide\"><span>目标说明（最多95字节）</span><textarea name=\"goal\" maxlength=\"95\">", edit.requirementId2, edit.requirementCount2); vm_mock_admin_text_append_html(&page, goalUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label></div></div>");
    vm_mock_admin_text_appendf(&page, "<div class=\"group\"><h2>给予物品与奖励</h2><div class=\"fields\"><label class=\"field\"><span>接取给予物品 ID</span><input type=\"number\" name=\"given_item_id\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>给予数量</span><input type=\"number\" name=\"given_item_count\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励经验</span><input type=\"number\" name=\"reward_exp\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励铜钱</span><input type=\"number\" name=\"reward_money\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励物品 ID</span><input type=\"number\" name=\"reward_item_id\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励物品数量</span><input type=\"number\" name=\"reward_item_count\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励物品类型</span><input type=\"number\" name=\"reward_item_type\" min=\"0\" max=\"255\" value=\"%u\"></label><label class=\"field\"><span>奖励说明（最多31字节）</span><input name=\"reward_text\" maxlength=\"31\" value=\"", edit.givenItemId, edit.givenItemCount, edit.rewardExp, edit.rewardMoney, edit.rewardItemId, edit.rewardItemCount, edit.rewardItemType); vm_mock_admin_text_append_html(&page, rewardUtf8); vm_mock_admin_text_appendf(&page, "\"></label></div></div>");
    vm_mock_admin_text_appendf(&page, "<div class=\"group\"><h2>NPC 对话</h2><p class=\"hint\">NPC 绑定该任务后按未接、进行中、可提交三种状态显示；留空时使用服务端安全默认文案。</p><div class=\"fields\"><label class=\"field wide\"><span>可接取时</span><textarea name=\"offer_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, offerUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label><label class=\"field wide\"><span>进行中</span><textarea name=\"active_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, activeUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label><label class=\"field wide\"><span>可提交时</span><textarea name=\"completed_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, completedUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label></div></div><p class=\"hint\">条件类型 1 为收集物品、2 为击败怪物；两项都为 0 时，接取后再次与交付 NPC 对话即可完成。名称长度按客户端 GBK 字节槽校验。</p><div class=\"actions\"><button type=\"submit\">保存任务</button></div></form>");
    if (!createNew && edit.overridden)
    {
        vm_mock_admin_text_appendf(&page, "<form class=\"actions\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"delete-task-override\"><input type=\"hidden\" name=\"task_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">%s</button></form>", edit.taskId, edit.builtin ? "恢复 task.dsh 默认" : "删除自定义任务");
    }
    vm_mock_admin_text_appendf(&page, "</section></div></main></body></html>");
    if (page.truncated)
        snprintf(response, responseCap, "<!doctype html><meta charset=\"utf-8\"><p>任务管理页面超过大小限制。</p>");
}

static void vm_mock_admin_render_page(char *response, size_t responseCap,
                                      const char *query)
{
    vm_mock_admin_text page;
    char tab[16];
    char selectedAccount[64];
    char status[16];
    char message[256];
    const char *roleError = NULL;
    vm_mock_service_account_state *accountState = NULL;
    u32 managedRoleIds[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    char managedRoleNames[VM_NET_MOCK_ROLE_DB_MAX_ROLES][128];
    u32 managedRoleCount = 0;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(tab, 0, sizeof(tab));
    memset(selectedAccount, 0, sizeof(selectedAccount));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(managedRoleIds, 0, sizeof(managedRoleIds));
    memset(managedRoleNames, 0, sizeof(managedRoleNames));
    (void)vm_mock_admin_form_value(query, "tab", tab, sizeof(tab));
    if (strcmp(tab, "content") == 0)
    {
        vm_mock_admin_render_content_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "tasks") == 0)
    {
        vm_mock_admin_render_task_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "updates") == 0)
    {
        vm_mock_admin_render_update_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "shop") == 0)
    {
        vm_mock_admin_render_shop_page(response, responseCap, query);
        return;
    }
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
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1120px;height:100vh;margin:0 auto;padding:28px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 14px}"
        ".sub{color:#667085;margin:4px 0 20px}.grid{display:grid;grid-template-columns:240px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:18px;box-shadow:0 1px 2px #1018280d}.grid>aside{display:flex;flex-direction:column;min-height:0;overflow:hidden}.grid>section{min-width:0;min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}"
        ".tabs{display:flex;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}"
        ".accounts{display:flex;flex:1;min-height:0;flex-direction:column;gap:6px;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.account{display:flex;justify-content:space-between;padding:9px 10px;border-radius:7px;color:#344054;text-decoration:none;scroll-margin-block:12px}.account:hover,.account.on{background:#eef4ff;color:#175cd3}"
        ".dot{color:#12b76a}.muted{color:#98a2b3}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        "table{border-collapse:collapse;width:100%%}th,td{text-align:left;padding:10px 8px;border-bottom:1px solid #eaecf0;vertical-align:top}th{color:#667085;font-weight:600}"
        "input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}button:hover{background:#1849a9}"
        ".inline{display:flex;gap:7px;margin:0 0 7px}.inline input{min-width:105px}.forms{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:16px}.stack{display:grid;gap:9px}.badge{font-size:12px;background:#eef4ff;color:#175cd3;padding:2px 7px;border-radius:999px}.money{white-space:nowrap}.item-grant{border-top:1px solid #eaecf0;margin-top:18px;padding-top:18px}.grant-form{display:grid;grid-template-columns:minmax(130px,.8fr) minmax(150px,1fr) minmax(260px,1.8fr) 90px auto;gap:9px;align-items:end}.grant-form label{display:grid;gap:4px}.grant-form label>span{font-size:12px;color:#667085}.grant-note{margin:8px 0 0;font-size:12px}.foot{margin-top:16px;color:#667085;font-size:12px}"
        "@media(max-width:780px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:18px 10px;overflow:visible}.grid,.forms{grid-template-columns:1fr;flex:none}.grid>aside,.grid>section{overflow:visible}.accounts{flex:none;max-height:220px;overflow:auto}.table-wrap{overflow:auto}.grant-form{grid-template-columns:1fr}.grant-form button{justify-self:start}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">本机管理端口 · 数据直接保存到 MySQL · 普通钱币以铜为基础单位</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab on\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a></nav><div class=\"grid\">"
        "<aside class=\"card\"><h2>账号（%u）</h2><div class=\"accounts\">",
        g_vm_mock_service_account_db.accountCount);

    for (u32 i = 0; i < g_vm_mock_service_account_db.accountCount; ++i)
    {
        const char *accountId = g_vm_mock_service_account_db.accounts[i].username;
        char encoded[192];
        bool online = vm_mock_admin_account_is_online(accountId);
        vm_mock_admin_url_encode(accountId, encoded, sizeof(encoded));
        if (strcmp(accountId, selectedAccount) == 0)
        {
            vm_mock_admin_text_appendf(&page,
                "<a id=\"selected-account\" class=\"account on\" aria-current=\"page\" href=\"/?tab=accounts&amp;account=%s#selected-account\"><span>",
                encoded);
        }
        else
        {
            vm_mock_admin_text_appendf(&page,
                "<a class=\"account\" href=\"/?tab=accounts&amp;account=%s#selected-account\"><span>",
                encoded);
        }
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
            if (managedRoleCount < VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                managedRoleIds[managedRoleCount] = role->roleId;
                snprintf(managedRoleNames[managedRoleCount],
                         sizeof(managedRoleNames[managedRoleCount]), "%s",
                         roleNameUtf8);
                ++managedRoleCount;
            }
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
    vm_mock_admin_text_appendf(&page, "</tbody></table></div>");
    vm_mock_admin_render_item_grant_form(
        &page, selectedAccount, managedRoleIds, managedRoleNames,
        managedRoleCount);
    vm_mock_admin_text_appendf(&page, "</div><div class=\"forms\">"
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
                               "<p class=\"foot\">安全限制：后台需要密码验证；所有请求有长度限制，页面不包含外部脚本。</p>"
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
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH "?account=%s&status=%s&message=%s",
             accountEncoded, statusEncoded, messageEncoded);
    snprintf(extraHeaders, sizeof(extraHeaders), "Location: %s\r\n", location);
    (void)vm_mock_admin_send_response(client, "303 See Other", "text/plain; charset=utf-8",
                                      extraHeaders, "正在返回后台页面。\n");
}

static void vm_mock_admin_redirect_content(vm_mock_service_socket client,
                                           const char *sceneUtf8,
                                           const char *status,
                                           const char *message)
{
    char sceneEncoded[512];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1600];

    vm_mock_admin_url_encode(sceneUtf8 ? sceneUtf8 : "",
                             sceneEncoded, sizeof(sceneEncoded));
    vm_mock_admin_url_encode(status ? status : "error",
                             statusEncoded, sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败",
                             messageEncoded, sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH "?tab=content&scene=%s&status=%s&message=%s",
             sceneEncoded, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_updates(vm_mock_service_socket client,
                                           const char *status,
                                           const char *message)
{
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1100];

    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH "?tab=updates&status=%s&message=%s", statusEncoded,
             messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_shop(vm_mock_service_socket client,
                                        const char *category,
                                        const char *search,
                                        u32 page,
                                        const char *status,
                                        const char *message)
{
    char categoryEncoded[64];
    char searchEncoded[384];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1500];

    vm_mock_admin_url_encode(category && category[0] ? category : "all",
                             categoryEncoded, sizeof(categoryEncoded));
    vm_mock_admin_url_encode(search ? search : "", searchEncoded,
                             sizeof(searchEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH "?tab=shop&category=%s&q=%s&page=%u&status=%s&message=%s",
             categoryEncoded, searchEncoded, page ? page : 1, statusEncoded,
             messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_tasks(vm_mock_service_socket client,
                                         u32 taskId,
                                         const char *status,
                                         const char *message)
{
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1100];

    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH "?tab=tasks&task=%u&status=%s&message=%s",
             taskId, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static bool vm_mock_admin_form_u32(const char *body, const char *field,
                                   u32 maximum, u32 *valueOut)
{
    char textValue[32];
    u32 parsed = 0;

    memset(textValue, 0, sizeof(textValue));
    if (!vm_mock_admin_form_value(body, field, textValue, sizeof(textValue)) ||
        !vm_net_mock_parse_u32_strict(textValue, &parsed) || parsed > maximum)
    {
        return false;
    }
    if (valueOut)
        *valueOut = parsed;
    return true;
}

static bool vm_mock_admin_utf8_to_gbk_task_text(const char *utf8,
                                                char *gbk,
                                                size_t gbkCap,
                                                bool allowEmpty)
{
    char converted[1024];

    if (gbk == NULL || gbkCap == 0)
        return false;
    gbk[0] = 0;
    if (utf8 == NULL || utf8[0] == 0)
        return allowEmpty;
    memset(converted, 0, sizeof(converted));
    utf8_to_gbk((u8 *)utf8, (u8 *)converted, sizeof(converted));
    if (converted[0] == 0 || strlen(converted) >= gbkCap)
        return false;
    snprintf(gbk, gbkCap, "%s", converted);
    return true;
}

static void vm_mock_admin_handle_task_action(vm_mock_service_socket client,
                                             const char *action,
                                             const char *body)
{
    vm_net_mock_task_definition task;
    const vm_net_mock_task_definition *existing = NULL;
    const char *error = NULL;
    char enabledText[8];
    char createNewText[8];
    char nameUtf8[128];
    char giverUtf8[64];
    char receiverUtf8[64];
    char goalUtf8[384];
    char rewardUtf8[128];
    char offerUtf8[768];
    char activeUtf8[768];
    char completedUtf8[768];
    u32 taskId = 0;
    u32 value = 0;

    memset(&task, 0, sizeof(task));
    memset(enabledText, 0, sizeof(enabledText));
    memset(createNewText, 0, sizeof(createNewText));
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(giverUtf8, 0, sizeof(giverUtf8));
    memset(receiverUtf8, 0, sizeof(receiverUtf8));
    memset(goalUtf8, 0, sizeof(goalUtf8));
    memset(rewardUtf8, 0, sizeof(rewardUtf8));
    memset(offerUtf8, 0, sizeof(offerUtf8));
    memset(activeUtf8, 0, sizeof(activeUtf8));
    memset(completedUtf8, 0, sizeof(completedUtf8));
    if (!vm_mock_admin_form_u32(body, "task_id", 0xffffffffu, &taskId) ||
        taskId == 0 || taskId == VM_NET_MOCK_TEST_TASK_ID)
    {
        vm_mock_admin_redirect_tasks(client, taskId, "error", "任务 ID 无效或使用了保留 ID");
        return;
    }
    if (strcmp(action, "delete-task-override") == 0)
    {
        bool restoreBuiltin = false;
        existing = vm_net_mock_task_admin_find(taskId);
        restoreBuiltin = existing != NULL && existing->builtin;
        if (!vm_net_mock_task_admin_delete_override(taskId, &error))
        {
            vm_mock_admin_redirect_tasks(
                client, taskId, "error",
                error ? error : "任务覆盖记录删除失败");
            return;
        }
        vm_mock_admin_redirect_tasks(
            client, restoreBuiltin ? taskId : 0,
            "ok", restoreBuiltin
                      ? "已恢复 task.dsh 原始任务定义"
                      : "自定义任务已删除");
        return;
    }
    if (strcmp(action, "save-task") != 0 ||
        !vm_mock_admin_form_value(body, "enabled", enabledText, sizeof(enabledText)) ||
        (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0) ||
        !vm_mock_admin_form_value(body, "name", nameUtf8, sizeof(nameUtf8)) ||
        !vm_mock_admin_form_value(body, "giver", giverUtf8, sizeof(giverUtf8)) ||
        !vm_mock_admin_form_value(body, "receiver", receiverUtf8, sizeof(receiverUtf8)) ||
        !vm_mock_admin_form_value(body, "goal", goalUtf8, sizeof(goalUtf8)) ||
        !vm_mock_admin_form_value(body, "reward_text", rewardUtf8, sizeof(rewardUtf8)) ||
        !vm_mock_admin_form_value(body, "offer_dialog", offerUtf8, sizeof(offerUtf8)) ||
        !vm_mock_admin_form_value(body, "active_dialog", activeUtf8, sizeof(activeUtf8)) ||
        !vm_mock_admin_form_value(body, "completed_dialog", completedUtf8, sizeof(completedUtf8)))
    {
        vm_mock_admin_redirect_tasks(client, taskId, "error", "任务表单字段不完整");
        return;
    }
    task.taskId = taskId;
    task.enabled = strcmp(enabledText, "1") == 0;
    existing = vm_net_mock_task_admin_find(taskId);
    (void)vm_mock_admin_form_value(body, "create_new", createNewText,
                                   sizeof(createNewText));
    if (strcmp(createNewText, "1") == 0 && existing != NULL)
    {
        vm_mock_admin_redirect_tasks(client, taskId, "error",
                                     "任务 ID 已存在，请更换后再新增");
        return;
    }
    task.builtin = existing != NULL && existing->builtin;
#define VM_TASK_FORM_U8(fieldName, member)                                      \
    do                                                                          \
    {                                                                           \
        if (!vm_mock_admin_form_u32(body, (fieldName), 0xffu, &value))          \
        {                                                                       \
            vm_mock_admin_redirect_tasks(client, taskId, "error",             \
                                          "任务数值字段无效");                 \
            return;                                                             \
        }                                                                       \
        task.member = (u8)value;                                                 \
    } while (0)
#define VM_TASK_FORM_U32(fieldName, member)                                     \
    do                                                                          \
    {                                                                           \
        if (!vm_mock_admin_form_u32(body, (fieldName), 0xffffffffu, &task.member)) \
        {                                                                       \
            vm_mock_admin_redirect_tasks(client, taskId, "error",             \
                                          "任务数值字段无效");                 \
            return;                                                             \
        }                                                                       \
    } while (0)
    VM_TASK_FORM_U8("level", level);
    VM_TASK_FORM_U8("difficulty", difficulty);
    VM_TASK_FORM_U8("classification", classification);
    VM_TASK_FORM_U8("requirement_type1", requirementType1);
    VM_TASK_FORM_U8("requirement_count1", requirementCount1);
    VM_TASK_FORM_U32("requirement_id1", requirementId1);
    VM_TASK_FORM_U8("requirement_type2", requirementType2);
    VM_TASK_FORM_U8("requirement_count2", requirementCount2);
    VM_TASK_FORM_U32("requirement_id2", requirementId2);
    VM_TASK_FORM_U32("prerequisite_task_id", prerequisiteTaskId);
    VM_TASK_FORM_U32("given_item_id", givenItemId);
    VM_TASK_FORM_U32("given_item_count", givenItemCount);
    VM_TASK_FORM_U32("reward_exp", rewardExp);
    VM_TASK_FORM_U32("reward_money", rewardMoney);
    VM_TASK_FORM_U32("reward_item_id", rewardItemId);
    VM_TASK_FORM_U32("reward_item_count", rewardItemCount);
    VM_TASK_FORM_U8("reward_item_type", rewardItemType);
#undef VM_TASK_FORM_U8
#undef VM_TASK_FORM_U32
    if (task.requirementType1 > 2 || task.requirementType2 > 2 ||
        !vm_mock_admin_utf8_to_gbk_task_text(nameUtf8, task.name,
                                             sizeof(task.name), false) ||
        !vm_mock_admin_utf8_to_gbk_task_text(giverUtf8, task.giver,
                                             sizeof(task.giver), false) ||
        !vm_mock_admin_utf8_to_gbk_task_text(receiverUtf8, task.receiver,
                                             sizeof(task.receiver), false) ||
        !vm_mock_admin_utf8_to_gbk_task_text(goalUtf8, task.goal,
                                             sizeof(task.goal), true) ||
        !vm_mock_admin_utf8_to_gbk_task_text(rewardUtf8, task.rewardText,
                                             sizeof(task.rewardText), true) ||
        !vm_mock_admin_utf8_to_gbk_task_text(offerUtf8, task.offerDialog,
                                             sizeof(task.offerDialog), true) ||
        !vm_mock_admin_utf8_to_gbk_task_text(activeUtf8, task.activeDialog,
                                             sizeof(task.activeDialog), true) ||
        !vm_mock_admin_utf8_to_gbk_task_text(completedUtf8, task.completedDialog,
                                             sizeof(task.completedDialog), true))
    {
        vm_mock_admin_redirect_tasks(
            client, taskId, "error",
            "任务文本无法转换为 GBK，或超过客户端安全字节长度");
        return;
    }
    if (!vm_net_mock_task_admin_save(&task, &error))
    {
        vm_mock_admin_redirect_tasks(client, taskId, "error",
                                     error ? error : "任务保存失败");
        return;
    }
    vm_mock_admin_redirect_tasks(client, taskId, "ok", "任务定义已保存并立即生效");
}

static bool vm_mock_admin_scene_from_form(const char *body,
                                          char *sceneUtf8,
                                          size_t sceneUtf8Cap,
                                          char *runtimeScene,
                                          size_t runtimeSceneCap)
{
    vm_mock_admin_scene_file files[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    char sceneFile[64];
    u32 fileCount = 0;
    bool exists = false;

    if (sceneUtf8 == NULL || sceneUtf8Cap == 0 ||
        runtimeScene == NULL || runtimeSceneCap == 0)
    {
        return false;
    }
    sceneUtf8[0] = 0;
    runtimeScene[0] = 0;
    memset(sceneFile, 0, sizeof(sceneFile));
    if (!vm_mock_admin_form_value(body, "scene", sceneUtf8, sceneUtf8Cap) ||
        !vm_mock_admin_utf8_to_gbk_text(sceneUtf8, sceneFile,
                                        sizeof(sceneFile), false))
    {
        return false;
    }
    fileCount = vm_mock_admin_collect_scene_files(
        files, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    for (u32 i = 0; i < fileCount; ++i)
    {
        if (strcmp(files[i].name, sceneFile) == 0)
        {
            exists = true;
            break;
        }
    }
    return exists && vm_mock_admin_scene_file_to_runtime_key(
                         sceneFile, runtimeScene, runtimeSceneCap);
}

static void vm_mock_admin_handle_npc_action(vm_mock_service_socket client,
                                            const char *action,
                                            const char *body)
{
    char sceneUtf8[192];
    char runtimeScene[64];
    char displayUtf8[128];
    char scriptUtf8[192];
    char actorResource[64];
    char actorImageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    char enabledText[8];
    vm_net_mock_scene_npcinfo_seed seed;
    const char *error = NULL;
    u32 actorId = 0;
    u32 x = 0;
    u32 y = 0;
    u32 kind = 0;
    u32 taskId = 0;
    u32 orientation = 0;
    u32 actorImageCount = 0;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(displayUtf8, 0, sizeof(displayUtf8));
    memset(scriptUtf8, 0, sizeof(scriptUtf8));
    memset(actorResource, 0, sizeof(actorResource));
    memset(actorImageNames, 0, sizeof(actorImageNames));
    memset(enabledText, 0, sizeof(enabledText));
    memset(&seed, 0, sizeof(seed));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)) ||
        !vm_mock_admin_form_u32(body, "actor_id", 0xffffffffu, &actorId) ||
        actorId == 0)
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "场景或 Actor ID 无效");
        return;
    }

    if (strcmp(action, "delete-npc-override") == 0)
    {
        if (!vm_net_mock_dynamic_npc_admin_delete_override(
                runtimeScene, actorId, &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "NPC 覆盖项删除失败");
            return;
        }
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       "NPC 覆盖项已删除");
        return;
    }

    if (strcmp(action, "toggle-npc") == 0)
    {
        vm_net_mock_dynamic_npc_admin_row rows[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
        u32 rowCount = vm_net_mock_dynamic_npc_admin_list(
            runtimeScene, rows, VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX);
        bool found = false;
        bool enabled = false;

        if (!vm_mock_admin_form_value(body, "enabled", enabledText,
                                      sizeof(enabledText)) ||
            (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "NPC 启用状态无效");
            return;
        }
        enabled = strcmp(enabledText, "1") == 0;
        for (u32 i = 0; i < rowCount; ++i)
        {
            if (rows[i].seed.actorId == actorId)
            {
                seed = rows[i].seed;
                found = true;
                break;
            }
        }
        if (found && enabled &&
            (!vm_mock_admin_actor_resource_inspect(
                 seed.actorResource, actorImageNames, &actorImageCount) ||
             !vm_mock_admin_publish_actor_resource(
                 seed.actorResource, actorImageNames, actorImageCount,
                 &error)))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error :
                        "Actor 资源无效、引用图片不完整或无法发布下载");
            return;
        }
        if (!found || !vm_net_mock_dynamic_npc_admin_save(
                          runtimeScene, &seed, enabled, &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "NPC 不存在或状态保存失败");
            return;
        }
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       enabled ? "NPC 已恢复；缺失资源将由客户端在线下载"
                                               : "NPC 已停用");
        return;
    }

    if (strcmp(action, "save-npc") != 0 ||
        !vm_mock_admin_form_u32(body, "x", 0xffffu, &x) || x == 0 ||
        !vm_mock_admin_form_u32(body, "y", 0xffffu, &y) || y == 0 ||
        !vm_mock_admin_form_u32(body, "kind", VM_NET_MOCK_NPC_KIND_MAX, &kind) ||
        !vm_mock_admin_form_u32(body, "task_id", 0xffffffffu, &taskId) ||
        !vm_mock_admin_form_u32(body, "orientation", 0xffffu, &orientation) ||
        !vm_mock_admin_form_value(body, "display_name", displayUtf8,
                                  sizeof(displayUtf8)) ||
        !vm_mock_admin_form_value(body, "actor_resource", actorResource,
                                  sizeof(actorResource)) ||
        !vm_mock_admin_form_value(body, "script_name", scriptUtf8,
                                  sizeof(scriptUtf8)))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "NPC 表单字段不完整或数值越界");
        return;
    }
    if (taskId != 0 && vm_net_mock_task_catalog_find_by_id(taskId) == NULL)
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "绑定任务不存在或已停用");
        return;
    }
    for (const unsigned char *p = (const unsigned char *)actorResource; *p; ++p)
    {
        if (*p >= 0x80)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "Actor 资源名必须使用 ASCII");
            return;
        }
    }
    if (strlen(actorResource) >= sizeof(seed.actorResource) ||
        vm_net_mock_scene_name_has_path_separator(actorResource) ||
        !vm_net_mock_str_ends_with(actorResource, ".actor"))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "Actor 资源名格式无效或名称过长");
        return;
    }
    if (!vm_mock_admin_actor_resource_inspect(
            actorResource, actorImageNames, &actorImageCount))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "所选 Actor 不存在、格式无效或引用图片不完整");
        return;
    }
    if (!vm_mock_admin_utf8_to_gbk_text(displayUtf8, seed.displayName,
                                        sizeof(seed.displayName), false) ||
        !vm_mock_admin_utf8_to_gbk_text(scriptUtf8, seed.scriptName,
                                        sizeof(seed.scriptName), true))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "NPC 名称或脚本名编码失败");
        return;
    }
    if (seed.scriptName[0] != 0 &&
        (vm_net_mock_scene_name_has_path_separator(seed.scriptName) ||
         !vm_net_mock_str_ends_with(seed.scriptName, ".xse") ||
         !vm_net_mock_open_server_data_resource(seed.scriptName, ".xse",
                                                NULL, NULL, 0)))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "所选 XSE 脚本资源不存在");
        return;
    }
    seed.actorId = actorId;
    seed.x = (u16)x;
    seed.y = (u16)y;
    seed.kind = (u16)kind;
    seed.taskId = taskId;
    seed.orientation = (u16)orientation;
    snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", actorResource);
    if (!vm_mock_admin_publish_actor_resource(
            seed.actorResource, actorImageNames, actorImageCount, &error))
    {
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "error",
            error ? error : "Actor 资源有效，但无法发布到 WT 18/7 下载目录");
        return;
    }
    if (!vm_net_mock_dynamic_npc_admin_save(runtimeScene, &seed, true, &error))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       error ? error : "NPC 保存失败");
        return;
    }
    vm_mock_admin_redirect_content(
        client, sceneUtf8, "ok",
        "NPC 保存成功；缺失的 Actor/GIF 将由客户端通过资源更新下载");
}

static void vm_mock_admin_handle_update_action(vm_mock_service_socket client,
                                               const char *action,
                                               const char *body)
{
    const char *error = NULL;
    u32 slot = 0;
    u32 version = 0;
    char enabledText[8];
    char resourceUtf8[256];
    char resourceGbk[128];

    memset(enabledText, 0, sizeof(enabledText));
    memset(resourceUtf8, 0, sizeof(resourceUtf8));
    memset(resourceGbk, 0, sizeof(resourceGbk));
    if (strcmp(action, "save-update-slot") == 0)
    {
        bool enabled = vm_mock_admin_form_value(body, "enabled", enabledText,
                                                sizeof(enabledText)) &&
                       strcmp(enabledText, "1") == 0;
        if (!vm_mock_admin_form_u32(body, "slot",
                                    VM_NET_MOCK_UPDATE_SLOT_COUNT, &slot) ||
            slot == 0 ||
            !vm_mock_admin_form_u32(body, "version", 0xffff, &version) ||
            version == 0)
        {
            vm_mock_admin_redirect_updates(client, "error",
                                           "槽位或版本号无效");
            return;
        }
        if (!vm_net_mock_update_admin_save_slot((u8)slot, (u16)version,
                                                enabled, &error))
        {
            vm_mock_admin_redirect_updates(client, "error",
                                           error ? error : "模块发布设置保存失败");
            return;
        }
        vm_mock_admin_redirect_updates(client, "ok",
                                       enabled ? "启动模块已发布；新客户端将在启动时下载"
                                               : "启动模块发布已停用");
        return;
    }
    if (strcmp(action, "reset-update-delivery") == 0)
    {
        if (!vm_net_mock_update_admin_reset_delivery(&error))
        {
            vm_mock_admin_redirect_updates(client, "error",
                                           error ? error : "下发记录清空失败");
            return;
        }
        vm_mock_admin_redirect_updates(client, "ok",
                                       "下发记录已清空；已发布模块会重新触发");
        return;
    }
    if (!vm_mock_admin_form_value(body, "resource", resourceUtf8,
                                  sizeof(resourceUtf8)) ||
        !vm_mock_admin_utf8_to_gbk_text(resourceUtf8, resourceGbk,
                                        sizeof(resourceGbk), false))
    {
        vm_mock_admin_redirect_updates(client, "error", "资源名称无效");
        return;
    }
    if (strcmp(action, "publish-named-update") == 0)
    {
        if (!vm_mock_admin_form_u32(body, "version", 0xffff, &version) ||
            version == 0 ||
            !vm_net_mock_update_admin_save_named(resourceGbk, (u16)version,
                                                 &error))
        {
            vm_mock_admin_redirect_updates(client, "error",
                                           error ? error : "具名资源发布失败");
            return;
        }
        vm_mock_admin_redirect_updates(client, "ok",
                                       "具名资源已发布，将在客户端下次请求该资源时下发");
        return;
    }
    if (strcmp(action, "remove-named-update") == 0)
    {
        if (!vm_net_mock_update_admin_remove_named(resourceGbk, &error))
        {
            vm_mock_admin_redirect_updates(client, "error",
                                           error ? error : "取消发布失败");
            return;
        }
        vm_mock_admin_redirect_updates(client, "ok", "具名资源已取消发布");
        return;
    }
    vm_mock_admin_redirect_updates(client, "error", "不支持的更新管理操作");
}

static void vm_mock_admin_handle_shop_action(vm_mock_service_socket client,
                                             const char *body)
{
    char category[16];
    char search[128];
    char pageText[32];
    char enabledText[8];
    const char *error = NULL;
    u32 itemId = 0;
    u32 price = 0;
    u32 page = 1;
    bool enabled = false;

    memset(category, 0, sizeof(category));
    memset(search, 0, sizeof(search));
    memset(pageText, 0, sizeof(pageText));
    memset(enabledText, 0, sizeof(enabledText));
    (void)vm_mock_admin_form_value(body, "category", category,
                                   sizeof(category));
    (void)vm_mock_admin_form_value(body, "q", search, sizeof(search));
    (void)vm_mock_admin_form_value(body, "page", pageText, sizeof(pageText));
    if (pageText[0] != 0 &&
        (!vm_net_mock_parse_u32_strict(pageText, &page) || page == 0))
    {
        page = 1;
    }
    if (!vm_mock_admin_form_u32(body, "item", 0xffffffffu, &itemId) ||
        itemId == 0 ||
        !vm_mock_admin_form_u32(body, "price", 0xffffffffu, &price) ||
        price == 0 ||
        !vm_mock_admin_form_value(body, "enabled", enabledText,
                                  sizeof(enabledText)) ||
        (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0))
    {
        vm_mock_admin_redirect_shop(client, category, search, page, "error",
                                    "商品、价格或上下架状态无效");
        return;
    }
    enabled = strcmp(enabledText, "1") == 0;
    if (!vm_net_mock_shop_admin_save(itemId, price, enabled, &error))
    {
        vm_mock_admin_redirect_shop(
            client, category, search, page, "error",
            error ? error : "商品保存失败");
        return;
    }
    vm_mock_admin_redirect_shop(
        client, category, search, page, "ok",
        enabled ? "商品价格已保存并上架" : "商品价格已保存并下架");
}

static void vm_mock_admin_handle_action(vm_mock_service_socket client, const char *body)
{
    char action[32];
    char account[64];
    char password[64];
    char role[64];
    char itemText[32];
    char amountText[32];
    const char *error = NULL;
    u32 itemId = 0;
    u32 amount = 0;
    u16 itemSeq = 0;
    bool ok = false;

    memset(action, 0, sizeof(action));
    memset(account, 0, sizeof(account));
    memset(password, 0, sizeof(password));
    memset(role, 0, sizeof(role));
    memset(itemText, 0, sizeof(itemText));
    memset(amountText, 0, sizeof(amountText));
    if (!vm_mock_admin_form_value(body, "action", action, sizeof(action)))
    {
        vm_mock_admin_redirect(client, "", "error", "请求参数不完整");
        return;
    }
    if (strcmp(action, "save-npc") == 0 ||
        strcmp(action, "toggle-npc") == 0 ||
        strcmp(action, "delete-npc-override") == 0)
    {
        vm_mock_admin_handle_npc_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-task") == 0 ||
        strcmp(action, "delete-task-override") == 0)
    {
        vm_mock_admin_handle_task_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-update-slot") == 0 ||
        strcmp(action, "reset-update-delivery") == 0 ||
        strcmp(action, "publish-named-update") == 0 ||
        strcmp(action, "remove-named-update") == 0)
    {
        vm_mock_admin_handle_update_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-shop-item") == 0)
    {
        vm_mock_admin_handle_shop_action(client, body);
        return;
    }
    if (!vm_mock_admin_form_value(body, "account", account, sizeof(account)))
    {
        vm_mock_admin_redirect(client, "", "error", "账号参数不完整");
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
    if (strcmp(action, "grant-item") == 0)
    {
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "item", itemText,
                                      sizeof(itemText)) ||
            !vm_mock_admin_form_value(body, "amount", amountText,
                                      sizeof(amountText)) ||
            !vm_net_mock_parse_u32_strict(itemText, &itemId) || itemId == 0 ||
            !vm_net_mock_parse_u32_strict(amountText, &amount) || amount == 0 ||
            amount > 255)
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "物品和数量参数无效");
            return;
        }
        ok = vm_mock_service_account_grant_role_item(
            account, role, itemId, amount, &itemSeq, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "物品给予成功" :
                                    (error ? error : "物品给予失败"));
        return;
    }

    vm_mock_admin_redirect(client, account, "error", "不支持的管理操作");
}

static void vm_mock_user_render_landing(char *response, size_t responseCap,
                                        const char *error,
                                        bool registerActive)
{
    vm_mock_admin_text page;

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 账号中心</title><style>"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 18%% 12%%,#d1fae5 0,transparent 28%%),radial-gradient(circle at 82%% 18%%,#dbeafe 0,transparent 26%%),#f6f8fb;color:#1f2937;font:14px/1.6 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{width:min(480px,calc(100%% - 28px));margin:0 auto;padding:58px 0}.hero{text-align:center;margin-bottom:24px}.mark{display:grid;place-items:center;width:58px;height:58px;margin:0 auto 12px;border-radius:18px;background:linear-gradient(145deg,#0f766e,#175cd3);color:#fff;font:700 26px/1 serif;box-shadow:0 10px 26px #175cd333}.hero h1{font-size:30px;margin:0 0 5px}.hero p{color:#667085;margin:0}.card{background:#fffffff2;border:1px solid #e0e6ed;border-radius:16px;padding:8px 24px 24px;box-shadow:0 18px 50px #10182817;backdrop-filter:blur(8px)}"
        ".tab-toggle{position:absolute;opacity:0;pointer-events:none}.tabs{display:grid;grid-template-columns:1fr 1fr;gap:4px;margin:0 -16px 20px;padding:6px;border-radius:12px;background:#f2f4f7}.tabs label{padding:9px 12px;border-radius:8px;color:#667085;font-weight:650;text-align:center;cursor:pointer}.panels>section{display:none}#login-mode:checked~.tabs label[for=\"login-mode\"],#register-mode:checked~.tabs label[for=\"register-mode\"]{background:#fff;color:#175cd3;box-shadow:0 1px 4px #10182818}#login-mode:checked~.panels .login-panel,#register-mode:checked~.panels .register-panel{display:block}"
        "h2{font-size:20px;margin:0 0 3px}.sub{color:#667085;margin:0 0 18px}.error{margin:0 0 17px;padding:10px 12px;border-radius:8px;background:#fef3f2;color:#b42318}form{display:grid;gap:12px}.field{display:grid;gap:5px;color:#475467;font-weight:550}.field input{width:100%%;border:1px solid #d0d5dd;border-radius:8px;padding:10px 11px;font:inherit;background:#fff;outline:none}.field input:focus{border-color:#84adff;box-shadow:0 0 0 3px #2e90fa18}button{border:0;border-radius:8px;padding:11px 13px;background:#175cd3;color:#fff;font-weight:650;cursor:pointer}.register-panel button{background:#027a48}.note{margin:12px 0 0;color:#98a2b3;font-size:12px}@media(max-width:540px){.wrap{padding:28px 0}.hero h1{font-size:26px}.card{padding-inline:18px}}"
        "</style></head><body><main class=\"wrap\"><header class=\"hero\"><div class=\"mark\">江</div><h1>江湖OL 账号中心</h1><p>管理你的江湖账号与角色资料</p></header>"
        "<section class=\"card\"><input class=\"tab-toggle\" type=\"radio\" id=\"login-mode\" name=\"auth-mode\"%s>"
        "<input class=\"tab-toggle\" type=\"radio\" id=\"register-mode\" name=\"auth-mode\"%s>"
        "<div class=\"tabs\" role=\"tablist\"><label for=\"login-mode\" role=\"tab\">登录账号</label><label for=\"register-mode\" role=\"tab\">注册账号</label></div>",
        registerActive ? "" : " checked",
        registerActive ? " checked" : "");
    if (error != NULL && error[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"error\">");
        vm_mock_admin_text_append_html(&page, error);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page,
        "<div class=\"panels\"><section class=\"login-panel\" role=\"tabpanel\"><h2>欢迎回来</h2><p class=\"sub\">使用游戏账号登录</p>"
        "<form method=\"post\" action=\"/user/login\"><label class=\"field\">账号<input name=\"account\" maxlength=\"63\" autocomplete=\"username\" required></label>"
        "<label class=\"field\">密码<input type=\"password\" name=\"password\" maxlength=\"63\" autocomplete=\"current-password\" required></label>"
        "<button type=\"submit\">登录账号</button></form></section>"
        "<section class=\"register-panel\" role=\"tabpanel\"><h2>创建新账号</h2><p class=\"sub\">注册后将自动登录账号中心</p>"
        "<form method=\"post\" action=\"/user/register\"><label class=\"field\">账号<input name=\"account\" minlength=\"4\" maxlength=\"32\" pattern=\"[A-Za-z0-9_]+\" autocomplete=\"username\" required></label>"
        "<label class=\"field\">密码<input type=\"password\" name=\"password\" minlength=\"6\" maxlength=\"63\" autocomplete=\"new-password\" required></label>"
        "<button type=\"submit\">注册并登录</button></form><p class=\"note\">账号名仅支持字母、数字和下划线，长度 4 至 32 位。</p></section></div></section>"
        "</main></body></html>");
}

static const char *vm_mock_user_job_label(u8 job)
{
    switch (job)
    {
    case 2:
        return "刺客";
    case 3:
        return "法师";
    case 1:
        return "战士";
    default:
        return "未知职业";
    }
}

static void vm_mock_user_render_dashboard(char *response, size_t responseCap,
                                          const char *accountId,
                                          const char *status,
                                          const char *message)
{
    vm_mock_admin_text page;
    vm_mock_service_account_state *accountState = NULL;
    const char *roleError = NULL;
    bool online = vm_mock_admin_account_is_online(accountId);

    accountState = vm_mock_service_open_account_role_db_for_management(accountId,
                                                                        &roleError);
    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>我的江湖账号</title><style>"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:#f4f7f9;color:#17202a;font:14px/1.6 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{width:min(1080px,calc(100%% - 28px));margin:0 auto;padding:30px 0 46px}header{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:18px}.brand{display:flex;align-items:center;gap:12px}.brand-mark{display:grid;place-items:center;width:42px;height:42px;border-radius:12px;background:linear-gradient(145deg,#0f766e,#175cd3);color:#fff;font:700 20px serif}h1{font-size:24px;margin:0}.sub,.muted{color:#667085;margin:2px 0 0}.logout{border:1px solid #d0d5dd;border-radius:8px;padding:8px 13px;background:#fff;color:#475467;cursor:pointer}.hero{position:relative;overflow:hidden;border-radius:16px;padding:24px;background:linear-gradient(125deg,#12372d,#175cd3);color:#fff;box-shadow:0 14px 34px #175cd326;margin-bottom:18px}.hero:after{content:\"\";position:absolute;width:220px;height:220px;border-radius:50%%;right:-70px;top:-125px;background:#ffffff12}.account-line{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.account-name{font-size:25px;font-weight:750}.hero .sub{color:#dbeafe}.badge{font-size:12px;padding:3px 9px;border-radius:999px;background:#ffffff20;color:#fff}.badge.on{background:#d1fadf;color:#05603a}.overview{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:19px}.overview div{padding:11px 13px;border:1px solid #ffffff20;border-radius:10px;background:#ffffff10}.overview strong{display:block;font-size:18px}.section-title{display:flex;align-items:end;justify-content:space-between;margin:23px 0 10px}.section-title h2{font-size:18px;margin:0}.roles{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:14px}.card,.role{background:#fff;border:1px solid #e4e7ec;border-radius:12px;padding:18px;box-shadow:0 2px 7px #1018280a}.role-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.role h3{font-size:19px;margin:0}.active{display:inline-block;color:#175cd3;background:#eef4ff;border-radius:999px;padding:2px 8px;font-size:12px;font-weight:650}.vitals{display:grid;gap:10px;margin:17px 0}.vital-head{display:flex;justify-content:space-between;color:#475467}.bar{height:7px;border-radius:999px;background:#edf1f5;overflow:hidden}.bar i{display:block;height:100%%;border-radius:inherit;background:#12b76a}.bar.mp i{background:#2e90fa}.stats{display:grid;grid-template-columns:1fr 1fr;gap:10px 16px;padding-top:13px;border-top:1px solid #eaecf0}.stats strong{display:block;color:#667085;font-size:12px;font-weight:550}.empty{color:#667085;text-align:center}.security{display:grid;grid-template-columns:minmax(190px,.65fr) minmax(0,1.35fr);gap:22px;align-items:start;margin-top:18px}.security h2{font-size:18px;margin:0 0 4px}.password-form{display:grid;grid-template-columns:1fr 1fr;gap:10px}.password-form label{display:grid;gap:4px;color:#475467}.password-form .current{grid-column:1/-1}.password-form input{width:100%%;border:1px solid #d0d5dd;border-radius:8px;padding:9px 10px;font:inherit}.password-form button{grid-column:1/-1;justify-self:start;border:0;border-radius:8px;padding:9px 14px;background:#175cd3;color:#fff;font-weight:650;cursor:pointer}.notice{padding:11px 13px;border-radius:9px;margin-bottom:16px}.notice.ok{background:#ecfdf3;color:#027a48}.notice.error{background:#fef3f2;color:#b42318}@media(max-width:720px){.wrap{padding:18px 0}.overview{grid-template-columns:1fr}.roles,.security,.password-form{grid-template-columns:1fr}.password-form .current{grid-column:auto}.password-form button{grid-column:auto}.stats{grid-template-columns:1fr}header{align-items:center}}"
        "</style></head><body><main class=\"wrap\"><header><div class=\"brand\"><div class=\"brand-mark\">江</div><div><h1>账号中心</h1><p class=\"sub\">查看角色资料与管理账号安全</p></div></div>"
        "<form method=\"post\" action=\"/user/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    if (status != NULL && status[0] != 0 && message != NULL && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page,
        "<section class=\"hero\"><div class=\"account-line\"><span class=\"account-name\">");
    vm_mock_admin_text_append_html(&page, accountId);
    vm_mock_admin_text_appendf(&page,
        "</span><span class=\"badge %s\">%s</span></div><p class=\"sub\">账号数据已连接至江湖服务</p>"
        "<div class=\"overview\"><div><span>账号状态</span><strong>正常</strong></div><div><span>角色数量</span><strong>%u</strong></div><div><span>当前状态</span><strong>%s</strong></div></div></section>"
        "<div class=\"section-title\"><h2>我的角色</h2><span class=\"muted\">角色数据实时读取</span></div><section class=\"roles\">",
        online ? "on" : "", online ? "游戏在线" : "游戏离线",
        accountState != NULL ? g_vm_net_mock_role_db.roleCount : 0,
        online ? "在线" : "离线");

    if (accountState != NULL && g_vm_net_mock_role_db.roleCount > 0)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
            char roleNameUtf8[128];
            char sceneUtf8[192];
            u32 gold = role->money / 10000u;
            u32 silver = (role->money / 100u) % 100u;
            u32 copper = role->money % 100u;
            bool active = role->roleId == g_vm_net_mock_role_db.activeRoleId;
            u32 hpPercent = role->hpMax ? (u32)(((uint64_t)role->hp * 100u) / role->hpMax) : 0;
            u32 mpPercent = role->mpMax ? (u32)(((uint64_t)role->mp * 100u) / role->mpMax) : 0;

            if (hpPercent > 100)
                hpPercent = 100;
            if (mpPercent > 100)
                mpPercent = 100;

            memset(roleNameUtf8, 0, sizeof(roleNameUtf8));
            memset(sceneUtf8, 0, sizeof(sceneUtf8));
            vm_net_mock_gbk_label_to_utf8(role->name[0] ? role->name : "-",
                                          roleNameUtf8, sizeof(roleNameUtf8));
            vm_net_mock_gbk_label_to_utf8(role->scene[0] ? role->scene : "-",
                                          sceneUtf8, sizeof(sceneUtf8));
            vm_mock_admin_text_appendf(&page, "<article class=\"role\"><div class=\"role-head\"><div><h3>");
            vm_mock_admin_text_append_html(&page, roleNameUtf8);
            vm_mock_admin_text_appendf(&page,
                "</h3><div class=\"muted\">角色 ID %u</div></div>%s</div>"
                "<div class=\"vitals\"><div><div class=\"vital-head\"><span>生命</span><span>%u / %u</span></div><div class=\"bar\"><i style=\"width:%u%%\"></i></div></div>"
                "<div><div class=\"vital-head\"><span>法力</span><span>%u / %u</span></div><div class=\"bar mp\"><i style=\"width:%u%%\"></i></div></div></div><div class=\"stats\">"
                "<div><strong>等级 / 经验</strong>Lv.%u · %u</div>"
                "<div><strong>职业 / 性别</strong>%s · %s</div>"
                "<div><strong>普通钱币</strong>%u 金 %u 银 %u 铜</div>"
                "<div><strong>元宝</strong>%u</div>"
                "<div><strong>所在场景</strong>",
                role->roleId, active ? "<span class=\"active\">当前角色</span>" : "",
                role->hp, role->hpMax, hpPercent, role->mp, role->mpMax,
                mpPercent, role->level, role->exp,
                vm_mock_user_job_label(role->job), role->sex == 1 ? "女" : "男",
                gold, silver, copper, role->wcoin);
            vm_mock_admin_text_append_html(&page, sceneUtf8);
            vm_mock_admin_text_appendf(&page,
                "</div><div><strong>坐标</strong>(%u, %u)</div></div></article>",
                role->x, role->y);
        }
        vm_mock_service_close_account_role_db_for_management(accountState, false);
    }
    else
    {
        if (accountState != NULL)
            vm_mock_service_close_account_role_db_for_management(accountState, false);
        vm_mock_admin_text_appendf(&page,
            "<div class=\"card empty\">%s</div>",
            roleError ? roleError : "该账号尚未创建角色，请进入游戏创建角色。");
    }
    vm_mock_admin_text_appendf(&page,
        "</section><section class=\"card security\"><div><h2>账号安全</h2><p class=\"sub\">修改后请使用新密码登录游戏和账号中心。其他网页登录会话将自动退出。</p></div>"
        "<form class=\"password-form\" method=\"post\" action=\"/user/password\">"
        "<label class=\"current\">当前密码<input type=\"password\" name=\"current_password\" maxlength=\"63\" autocomplete=\"current-password\" required></label>"
        "<label>新密码<input type=\"password\" name=\"new_password\" minlength=\"6\" maxlength=\"63\" autocomplete=\"new-password\" required></label>"
        "<label>确认新密码<input type=\"password\" name=\"confirm_password\" minlength=\"6\" maxlength=\"63\" autocomplete=\"new-password\" required></label>"
        "<button type=\"submit\">保存新密码</button></form></section></main></body></html>");
}

static void vm_mock_user_invalidate_other_sessions(
    const char *accountId, const vm_mock_user_session *keepSession)
{
    for (u32 i = 0; i < VM_MOCK_USER_SESSION_MAX; ++i)
    {
        vm_mock_user_session *session = &g_vm_mock_user_sessions[i];
        if (session->active && session != keepSession &&
            strcmp(session->accountId, accountId) == 0)
            memset(session, 0, sizeof(*session));
    }
}

static void vm_mock_user_redirect_message(vm_mock_service_socket client,
                                          const char *status,
                                          const char *message)
{
    char statusEncoded[64];
    char messageEncoded[768];
    char location[960];

    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location), "/?status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
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
    if (!vm_mock_admin_request_has_allowed_origin(request, headerLen))
    {
        vm_mock_admin_send_response(client, "403 Forbidden", NULL, NULL,
                                    "Host 或 Origin 校验失败。\n");
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

    if (strcmp(method, "GET") == 0 && strcmp(target, "/healthz") == 0)
    {
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/json; charset=utf-8", NULL,
                                    "{\"ok\":true,\"service\":\"jianghu-admin\"}\n");
        return 1;
    }
    if (strcmp(target, "/") == 0)
    {
        vm_mock_user_session *session = NULL;
        char status[16];
        char message[256];

        if (strcmp(method, "GET") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: GET\r\n",
                                        "账号中心首页只允许 GET。\n");
            return 0;
        }
        memset(status, 0, sizeof(status));
        memset(message, 0, sizeof(message));
        (void)vm_mock_admin_form_value(query, "status", status,
                                       sizeof(status));
        (void)vm_mock_admin_form_value(query, "message", message,
                                       sizeof(message));
        response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (response == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        NULL, NULL, "内存不足。\n");
            return 0;
        }
        session = vm_mock_user_request_session(request, headerLen);
        if (session != NULL)
            vm_mock_user_render_dashboard(response, VM_MOCK_ADMIN_RESPONSE_MAX,
                                          session->accountId, status, message);
        else
            vm_mock_user_render_landing(response, VM_MOCK_ADMIN_RESPONSE_MAX,
                                        NULL, false);
        vm_mock_admin_send_response(client, "200 OK",
                                    "text/html; charset=utf-8", NULL, response);
        free(response);
        return 1;
    }
    if (strcmp(target, "/user/login") == 0 ||
        strcmp(target, "/user/register") == 0)
    {
        char account[64];
        char password[64];
        const char *message = NULL;
        bool registering = strcmp(target, "/user/register") == 0;
        bool ok = false;

        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n",
                                        "账号登录和注册只允许 POST。\n");
            return 0;
        }
        memset(account, 0, sizeof(account));
        memset(password, 0, sizeof(password));
        if (!vm_mock_admin_form_value(body, "account", account, sizeof(account)) ||
            !vm_mock_admin_form_value(body, "password", password, sizeof(password)))
        {
            message = "账号或密码参数不完整";
        }
        else if (registering)
        {
            const char *createMessage = NULL;
            if (vm_mock_user_valid_registration(account, password, &message))
            {
                ok = vm_mock_service_account_create_record(account, password,
                                                           &createMessage);
                if (!ok)
                {
                    if (createMessage && strcmp(createMessage,
                                                "account already exists") == 0)
                        message = "该账号名已被注册";
                    else
                        message = "账号注册失败，请稍后重试";
                }
            }
        }
        else
        {
            ok = vm_mock_service_account_verify_credentials(account, password);
            if (!ok)
                message = "账号或密码错误";
        }
        memset(password, 0, sizeof(password));
        if (ok)
        {
            vm_mock_user_session *session = vm_mock_user_issue_session(account);
            char cookieHeader[256];
            if (session == NULL)
            {
                vm_mock_admin_send_response(client, "503 Service Unavailable",
                                            NULL, NULL, "登录会话暂不可用。\n");
                return 0;
            }
            snprintf(cookieHeader, sizeof(cookieHeader),
                     "Set-Cookie: cbe_user=%s; Path=/; HttpOnly; SameSite=Strict\r\n",
                     session->token);
            printf("[info][user-web] %s account=%s result=success\n",
                   registering ? "register" : "login", account);
            vm_mock_admin_send_location(client, "/", cookieHeader);
            return 1;
        }
        printf("[warn][user-web] %s account=%s result=rejected\n",
               registering ? "register" : "login",
               account[0] ? account : "-");
        response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (response == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        NULL, NULL, "内存不足。\n");
            return 0;
        }
        vm_mock_user_render_landing(response, VM_MOCK_ADMIN_RESPONSE_MAX,
                                    message ? message : "操作失败", registering);
        vm_mock_admin_send_response(client,
                                    registering ? "409 Conflict" :
                                                  "401 Unauthorized",
                                    "text/html; charset=utf-8", NULL, response);
        free(response);
        return 1;
    }
    if (strcmp(target, "/user/password") == 0)
    {
        vm_mock_user_session *session = NULL;
        char currentPassword[64];
        char newPassword[64];
        char confirmPassword[64];
        const char *error = NULL;
        bool ok = false;

        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n",
                                        "修改密码只允许 POST。\n");
            return 0;
        }
        session = vm_mock_user_request_session(request, headerLen);
        if (session == NULL)
        {
            vm_mock_admin_send_location(client, "/", NULL);
            return 0;
        }
        memset(currentPassword, 0, sizeof(currentPassword));
        memset(newPassword, 0, sizeof(newPassword));
        memset(confirmPassword, 0, sizeof(confirmPassword));
        if (!vm_mock_admin_form_value(body, "current_password",
                                      currentPassword,
                                      sizeof(currentPassword)) ||
            !vm_mock_admin_form_value(body, "new_password", newPassword,
                                      sizeof(newPassword)) ||
            !vm_mock_admin_form_value(body, "confirm_password",
                                      confirmPassword,
                                      sizeof(confirmPassword)))
        {
            error = "密码参数不完整";
        }
        else if (!vm_mock_service_account_verify_credentials(
                     session->accountId, currentPassword))
        {
            error = "当前密码不正确";
        }
        else if (strcmp(newPassword, confirmPassword) != 0)
        {
            error = "两次输入的新密码不一致";
        }
        else if (strlen(newPassword) < 6 || strlen(newPassword) > 63)
        {
            error = "新密码长度须为 6 至 63 个字符";
        }
        else
        {
            ok = vm_mock_service_account_set_password(
                session->accountId, newPassword, &error);
        }
        memset(currentPassword, 0, sizeof(currentPassword));
        memset(newPassword, 0, sizeof(newPassword));
        memset(confirmPassword, 0, sizeof(confirmPassword));
        if (ok)
        {
            vm_mock_user_invalidate_other_sessions(session->accountId,
                                                   session);
            printf("[info][user-web] password_change account=%s result=success other_sessions=invalidated\n",
                   session->accountId);
            vm_mock_user_redirect_message(client, "ok", "密码修改成功");
        }
        else
        {
            printf("[warn][user-web] password_change account=%s result=rejected\n",
                   session->accountId);
            vm_mock_user_redirect_message(client, "error",
                                          error ? error : "密码修改失败");
        }
        return 1;
    }
    if (strcmp(target, "/user/logout") == 0)
    {
        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n",
                                        "退出登录只允许 POST。\n");
            return 0;
        }
        vm_mock_user_clear_request_session(request, headerLen);
        vm_mock_admin_send_location(
            client, "/",
            "Set-Cookie: cbe_user=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict\r\n");
        return 1;
    }
    if (strcmp(target, VM_MOCK_ADMIN_BASE_PATH) == 0)
    {
        if (strcmp(method, "GET") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: GET\r\n", "只允许 GET。\n");
            return 0;
        }
        vm_mock_admin_send_location(client, VM_MOCK_ADMIN_ROOT_PATH, NULL);
        return 1;
    }
    if (strncmp(target, VM_MOCK_ADMIN_ROOT_PATH,
                strlen(VM_MOCK_ADMIN_ROOT_PATH)) != 0)
    {
        vm_mock_admin_send_response(client, "404 Not Found", NULL, NULL,
                                    "页面不存在。\n");
        return 0;
    }
    if (strcmp(target, VM_MOCK_ADMIN_LOGIN_PATH) == 0)
    {
        char password[65];
        const char *loginMessage = NULL;
        bool loginOk = false;

        memset(password, 0, sizeof(password));
        if (strcmp(method, "POST") == 0 &&
            vm_mock_admin_form_value(body, "password", password,
                                     sizeof(password)))
            loginOk = vm_mock_admin_verify_login_password(password,
                                                          &loginMessage);
        memset(password, 0, sizeof(password));
        if (loginOk)
        {
            char cookieHeader[256];
            vm_mock_admin_ensure_session_token();
            snprintf(cookieHeader, sizeof(cookieHeader),
                     "Set-Cookie: cbe_admin=%s; Path=" VM_MOCK_ADMIN_BASE_PATH "; HttpOnly; SameSite=Strict\r\n",
                     g_vm_mock_admin_session_token);
            vm_mock_admin_send_location(client, VM_MOCK_ADMIN_ROOT_PATH,
                                        cookieHeader);
            return 1;
        }
        if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: GET, POST\r\n",
                                        "登录只允许 GET 或 POST。\n");
            return 0;
        }
        response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (response == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        NULL, NULL, "内存不足。\n");
            return 0;
        }
        vm_mock_admin_render_login(
            response, VM_MOCK_ADMIN_RESPONSE_MAX,
            strcmp(method, "POST") == 0 ?
                (loginMessage ? loginMessage : "管理密码错误") : NULL);
        if (!vm_mock_admin_prefix_page_routes(response,
                                              VM_MOCK_ADMIN_RESPONSE_MAX))
        {
            snprintf(response, VM_MOCK_ADMIN_RESPONSE_MAX,
                     "<!doctype html><meta charset=\"utf-8\"><p>后台登录页面生成失败。</p>");
        }
        vm_mock_admin_send_response(
            client,
            strcmp(method, "POST") == 0 ? "401 Unauthorized" : "200 OK",
            "text/html; charset=utf-8", NULL, response);
        free(response);
        return 1;
    }
    if (strcmp(target, VM_MOCK_ADMIN_LOGOUT_PATH) == 0)
    {
        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n", "退出只允许 POST。\n");
            return 0;
        }
        vm_mock_admin_send_location(
            client, VM_MOCK_ADMIN_LOGIN_PATH,
            "Set-Cookie: cbe_admin=; Path=" VM_MOCK_ADMIN_BASE_PATH "; Max-Age=0; HttpOnly; SameSite=Strict\r\n");
        return 1;
    }
    if (!vm_mock_admin_request_is_authenticated(request, headerLen))
    {
        vm_mock_admin_send_location(client, VM_MOCK_ADMIN_LOGIN_PATH, NULL);
        return 0;
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/admin.js") == 0)
    {
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/javascript; charset=utf-8",
                                    NULL, g_vm_mock_admin_script);
        return 1;
    }
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/actor-preview.svg") == 0)
    {
        char actorResource[128];
        u8 *svg = NULL;
        u32 svgLen = 0;

        memset(actorResource, 0, sizeof(actorResource));
        if (!vm_mock_admin_form_value(query, "actor", actorResource,
                                      sizeof(actorResource)) ||
            !vm_mock_admin_build_actor_preview_svg(actorResource, &svg,
                                                   &svgLen))
        {
            vm_mock_admin_send_response(client, "422 Unprocessable Content",
                                        NULL, NULL,
                                        "NPC 模型资源无法生成预览。\n");
            free(svg);
            return 0;
        }
        vm_mock_admin_send_binary_response(client, "200 OK", "image/svg+xml",
                                           svg, svgLen);
        free(svg);
        return 1;
    }
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/scene-preview.bmp") == 0)
    {
        char sceneUtf8[192];
        char runtimeScene[64];
        const u8 *bmp = NULL;
        u32 bmpLen = 0;

        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        memset(runtimeScene, 0, sizeof(runtimeScene));
        if (!vm_mock_admin_scene_from_form(query, sceneUtf8,
                                           sizeof(sceneUtf8), runtimeScene,
                                           sizeof(runtimeScene)) ||
            !vm_mock_admin_build_scene_preview_bmp(runtimeScene, &bmp,
                                                   &bmpLen, NULL, NULL))
        {
            vm_mock_admin_send_response(client, "422 Unprocessable Content",
                                        NULL, NULL,
                                        "场景地图资源无法生成预览。\n");
            return 0;
        }
        vm_mock_admin_send_binary_response(client, "200 OK", "image/bmp",
                                           bmp, bmpLen);
        return 1;
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_ROOT_PATH) == 0)
    {
        response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (response == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error", NULL, NULL, "内存不足。\n");
            return 0;
        }
        vm_mock_admin_render_page(response, VM_MOCK_ADMIN_RESPONSE_MAX, query);
        if (!vm_mock_admin_prefix_page_routes(response,
                                              VM_MOCK_ADMIN_RESPONSE_MAX))
        {
            snprintf(response, VM_MOCK_ADMIN_RESPONSE_MAX,
                     "<!doctype html><meta charset=\"utf-8\"><p>后台页面生成失败。</p>");
        }
        vm_mock_admin_send_response(client, "200 OK", "text/html; charset=utf-8", NULL, response);
        free(response);
        return 1;
    }
    if (strcmp(target, VM_MOCK_ADMIN_ACTION_PATH) == 0)
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

static vm_mock_service_socket vm_mock_admin_open_listener(const char *bindHost, u16 port)
{
    vm_mock_service_socket server = VM_MOCK_SERVICE_INVALID_SOCKET;
    struct sockaddr_in address;
    const char *resolvedBindHost = bindHost && bindHost[0] ? bindHost : "127.0.0.1";
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
    if (!vm_mock_service_resolve_ipv4_host(resolvedBindHost, 1, &address.sin_addr))
    {
        vm_mock_service_socket_close(server);
        return VM_MOCK_SERVICE_INVALID_SOCKET;
    }
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 4) != 0)
    {
        vm_mock_service_socket_close(server);
        return VM_MOCK_SERVICE_INVALID_SOCKET;
    }
    return server;
}
