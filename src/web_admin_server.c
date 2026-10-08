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
#include <time.h>
#endif

enum
{
    /* Admin POSTs (NPC/task/monster editors) and remote browser access need
     * more headroom than the old 8 KiB / 100 ms pair.  A 100 ms receive
     * timeout is why /admin-418yz6 often answered "请求不完整" after deploy
     * when the body arrived in a later TCP segment.  Keep the receive
     * deadline independent of game protocol-mutex wait time. */
    /* Map editor POSTs cells_b64; 藏宝阁收款码 upload needs ~512 KiB for
     * URL-encoded JPEG base64 (gateway forwards the same body). */
    VM_MOCK_ADMIN_REQUEST_MAX = 524288,
    /* Shared catalogs + 2 MiB headroom; avoid O(npc*catalog) HTML. */
    VM_MOCK_ADMIN_RESPONSE_MAX = 2097152,
    VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS = 15000,
    /* Cap how long an admin dispatch waits for the shared game-state lock
     * so a busy protocol path returns 503 instead of hanging the browser. */
    VM_MOCK_ADMIN_PROTOCOL_LOCK_WAIT_MS = 8000,
    VM_MOCK_USER_SESSION_MAX = 64
};

/* Admin dispatch holds g_vm_mock_service_protocol_mutex.  HTTP send and
 * local filesystem reads do not need that lock; release it for the duration
 * so game workers are not stalled by large page bodies or slow clients. */
#ifdef _WIN32
static __declspec(thread) int g_vm_mock_admin_protocol_lock_depth;
static __declspec(thread) char g_vm_mock_admin_protocol_holder_label[160];
#else
static __thread int g_vm_mock_admin_protocol_lock_depth;
static __thread char g_vm_mock_admin_protocol_holder_label[160];
#endif

static void vm_mock_admin_protocol_holder_note(void)
{
    vm_mock_protocol_lock_note(
        g_vm_mock_admin_protocol_holder_label[0]
            ? g_vm_mock_admin_protocol_holder_label
            : "admin-dispatch");
}

static void vm_mock_admin_protocol_holder_set(const char *method,
                                              const char *target)
{
    snprintf(g_vm_mock_admin_protocol_holder_label,
             sizeof(g_vm_mock_admin_protocol_holder_label),
             "admin-dispatch %s %s",
             method && method[0] ? method : "-",
             target && target[0] ? target : "-");
}

static void vm_mock_admin_protocol_io_pause(void)
{
    if (g_vm_mock_admin_protocol_lock_depth > 0)
    {
        vm_mock_protocol_lock_clear();
        pthread_mutex_unlock(&g_vm_mock_service_protocol_mutex);
    }
}

static void vm_mock_admin_protocol_io_resume(void)
{
    if (g_vm_mock_admin_protocol_lock_depth > 0)
    {
        pthread_mutex_lock(&g_vm_mock_service_protocol_mutex);
        vm_mock_admin_protocol_holder_note();
    }
}

static int vm_mock_admin_protocol_lock_wait(u32 *waitMsOut)
{
    u32 startMs = scheduler_get_tick_ms();

    for (;;)
    {
        if (pthread_mutex_trylock(&g_vm_mock_service_protocol_mutex) == 0)
        {
            if (waitMsOut != NULL)
            {
                u32 nowMs = scheduler_get_tick_ms();
                *waitMsOut = nowMs >= startMs ? nowMs - startMs : 0;
            }
            return 1;
        }
        {
            u32 nowMs = scheduler_get_tick_ms();
            u32 elapsedMs = nowMs >= startMs ? nowMs - startMs : 0;
            if (elapsedMs >= VM_MOCK_ADMIN_PROTOCOL_LOCK_WAIT_MS)
                return 0;
        }
#ifdef _WIN32
        Sleep(20);
#else
        {
            struct timespec pause;
            pause.tv_sec = 0;
            pause.tv_nsec = 20 * 1000 * 1000;
            nanosleep(&pause, NULL);
        }
#endif
    }
}

static void vm_mock_admin_configure_client_socket(vm_mock_service_socket client)
{
#ifdef _WIN32
    int timeoutMs = VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
#else
    struct timeval timeout;
    timeout.tv_sec = VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS / 1000;
    timeout.tv_usec = (VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS % 1000) * 1000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

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


/* Shared top-nav chrome. Uses admin-tabs/admin-tab so page-local .tabs/.tab
 * rules (and the login dual-mode .tabs grid) cannot restyle it per page. */
static void vm_mock_admin_append_tab_styles(vm_mock_admin_text *page)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<style>"
        ".admin-tabs{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 16px;flex:none}"
        ".admin-tab{display:inline-block;box-sizing:border-box;padding:9px 14px;"
        "border-radius:7px;color:#475467;text-decoration:none;background:#fff;"
        "border:1px solid #e4e7ec;white-space:nowrap;font:inherit;line-height:1.25;"
        "margin:0}"
        ".admin-tab.on{background:#175cd3;color:#fff;border-color:#175cd3}"
        "</style>");
}

static void vm_mock_admin_append_tabs(vm_mock_admin_text *page, const char *active)
{
    static const struct
    {
        const char *id;
        const char *label;
    } kTabs[] = {
        {"accounts", "账号管理"},
        {"content", "游戏内容管理"},
        {"tasks", "任务管理"},
        {"monsters", "怪物管理"},
        {"shop", "商品管理"},
        {"catalog", "目录管理"},
        {"chests", "宝箱奖励"},
        {"enhance", "强化配置"},
        {"gem", "宝石配置"},
        {"clientui", "客户端UI"},
        {"combat", "战斗公式"},
        {"arena", "比武"},
        {"worldboss", "世界BOSS"},
        {"ladder", "天梯"},
        {"fruit", "水果机"},
        {"guess", "猜猜看"},
        {"auction", "拍卖行"},
        {"zangbao", "藏宝阁"},
        {"activities", "节日/跑环"},
        {"updates", "游戏内容更新管理"},
        {"servers", "服务器列表"},
        {"gateways", "区服网关"},
        {"maintenance", "服务器维护"},
    };
    size_t i = 0;

    if (page == NULL)
        return;
    if (active == NULL)
        active = "";
    vm_mock_admin_append_tab_styles(page);
    vm_mock_admin_text_appendf(page, "<nav class=\"admin-tabs\">");
    for (i = 0; i < sizeof(kTabs) / sizeof(kTabs[0]); ++i)
    {
        const int on = (strcmp(active, kTabs[i].id) == 0);
        vm_mock_admin_text_appendf(
            page, "<a class=\"admin-tab%s\" href=\"/?tab=%s\">%s</a>",
            on ? " on" : "", kTabs[i].id, kTabs[i].label);
    }
    vm_mock_admin_text_appendf(page, "</nav>");
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
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src 'self'; img-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "%s\r\n",
        status ? status : "200 OK",
        contentType ? contentType : "text/plain; charset=utf-8",
        (u32)bodyLen,
        extraHeaders ? extraHeaders : "");
    int sent = 0;

    if (headerLen <= 0 || (size_t)headerLen >= sizeof(header))
        return 0;
    vm_mock_admin_protocol_io_pause();
    sent = vm_mock_service_send_all(client, (const u8 *)header, (u32)headerLen) &&
           (bodyLen == 0 ||
            vm_mock_service_send_all(client, (const u8 *)body, (u32)bodyLen));
    vm_mock_admin_protocol_io_resume();
    return sent;
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
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src 'self'; img-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "\r\n",
        status ? status : "200 OK",
        contentType ? contentType : "application/octet-stream",
        bodyLen);
    int sent = 0;

    if (headerLen <= 0 || (size_t)headerLen >= sizeof(header))
        return 0;
    vm_mock_admin_protocol_io_pause();
    sent = vm_mock_service_send_all(client, (const u8 *)header, (u32)headerLen) &&
           (bodyLen == 0 ||
            (body != NULL && vm_mock_service_send_all(client, body, bodyLen)));
    vm_mock_admin_protocol_io_resume();
    return sent;
}

static int vm_mock_admin_send_payment_qrcode_script(vm_mock_service_socket client)
{
    static const char *paths[] = {
        "web/payment-qrcode.js",
        "../web/payment-qrcode.js"
    };
    FILE *fp = NULL;
    u8 *body = NULL;
    long lengthLong = 0;
    u32 length = 0;
    int sent = 0;

    /* Local script load is independent of account/protocol state. */
    vm_mock_admin_protocol_io_pause();
    for (u32 i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
    {
        fp = fopen(paths[i], "rb");
        if (fp != NULL)
            break;
    }
    if (fp == NULL || fseek(fp, 0, SEEK_END) != 0 ||
        (lengthLong = ftell(fp)) <= 0 || lengthLong > 128 * 1024 ||
        fseek(fp, 0, SEEK_SET) != 0)
        goto done;
    length = (u32)lengthLong;
    body = (u8 *)malloc(length);
    if (body == NULL || fread(body, 1, length, fp) != length)
        goto done;
done:
    if (fp != NULL)
        fclose(fp);
    vm_mock_admin_protocol_io_resume();
    if (body != NULL)
        sent = vm_mock_admin_send_binary_response(
            client, "200 OK", "application/javascript; charset=utf-8", body, length);
    free(body);
    if (!sent)
        sent = vm_mock_admin_send_response(client, "404 Not Found", NULL, NULL,
                                           "二维码脚本不可用。\n");
    return sent;
}

enum
{
    VM_MOCK_ADMIN_SCENE_FILE_MAX = 2048,
    VM_MOCK_ADMIN_ACTOR_FILE_MAX = 2048,
    VM_MOCK_ADMIN_XSE_FILE_MAX = 2048,
    VM_MOCK_ADMIN_UPDATE_FILE_MAX = 1400,
    VM_MOCK_ADMIN_SHOP_PAGE_SIZE = 50
};

typedef struct
{
    char name[64];
    char alias[32]; /* sMap.dsh column-2 display name, GBK */
    uint64_t size;
} vm_mock_admin_scene_file;

static char g_vm_mock_admin_session_token[40];
/* When POST /action carries X-Requested-With: XMLHttpRequest, redirects become
 * JSON so the page can keep scroll position and form/select state. */
static bool g_vm_mock_admin_ajax_action = false;

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

#include "web_payment.inc.c"

static const char g_vm_mock_admin_script[] =
    "(()=>{"
    "const keep=(selector,key)=>{"
    "const box=document.querySelector(selector);if(!box)return;"
    "const restore=()=>{const value=sessionStorage.getItem(key);"
    "if(value!==null){const top=parseInt(value,10);if(Number.isFinite(top))box.scrollTop=top;}};"
    "const save=()=>sessionStorage.setItem(key,String(box.scrollTop));"
    "restore();box.addEventListener('scroll',save,{passive:true});"
    "box.addEventListener('click',save);window.addEventListener('load',restore,{once:true});};"
    "const setupItemPicker=()=>{"
    "const source=document.querySelector('#item-picker-options'),category=document.querySelector('#item-category'),modal=document.querySelector('#item-picker-modal'),close=document.querySelector('#item-picker-close'),clear=document.querySelector('#item-picker-clear'),search=document.querySelector('#item-search'),list=document.querySelector('#item-picker-list'),empty=document.querySelector('#item-picker-empty'),count=document.querySelector('#item-result-count'),error=document.querySelector('#item-picker-error');"
    "const inputs=[...document.querySelectorAll('[data-item-picker-input]')];if(!source||!category||!modal||!close||!clear||!search||!list||!empty||!count||!error||!inputs.length)return;"
    "if(modal.dataset.itemPickerReady==='1')return;modal.dataset.itemPickerReady='1';"
    "document.body.classList.remove('modal-open');modal.hidden=true;"
    "const inputById=new Map(inputs.map(input=>[input.id,input]));const labels=new Map([...category.options].map(option=>[option.value,option.textContent]));const optionByValue=new Map([...source.options].filter(option=>option.value&&option.value!=='0').map(option=>[option.value,option]));let activeId='';const choices=[];"
    "const update=input=>{if(!input)return;const option=optionByValue.get(input.value);const text=option?option.textContent:(input.value&&input.value!=='0'?`未知物品 #${input.value}`:'未选择物品');for(const label of document.querySelectorAll('[data-item-picker-label]'))if(label.dataset.itemPickerLabel===input.id)label.textContent=text;};"
    "for(const input of inputs)update(input);"
    "list.replaceChildren();"
    "for(const option of source.options){if(!option.value||option.value==='0')continue;const button=document.createElement('button');button.type='button';button.className='item-choice';button.dataset.itemId=option.value;button.dataset.category=option.dataset.category||'';button.dataset.search=option.textContent.toLowerCase();const title=document.createElement('strong');title.textContent=option.textContent;const meta=document.createElement('span');meta.textContent=labels.get(button.dataset.category)||'未分类';button.append(title,meta);button.addEventListener('click',()=>{const input=inputById.get(activeId);if(!input)return;input.value=option.value;update(input);error.textContent='';hide();});choices.push(button);list.appendChild(button);}"
    "const apply=()=>{const wanted=category.value,keyword=search.value.trim().toLowerCase();let shown=0;for(const choice of choices){const visible=(wanted==='all'||choice.dataset.category===wanted)&&(!keyword||choice.dataset.search.includes(keyword));choice.hidden=!visible;if(visible)shown++;}count.textContent=`找到 ${shown} 件物品`;empty.hidden=shown!==0;};"
    "const show=id=>{if(!inputById.has(id))return;activeId=id;modal.hidden=false;document.body.classList.add('modal-open');error.textContent='';apply();search.focus();};"
    "function hide(){modal.hidden=true;document.body.classList.remove('modal-open');const trigger=document.querySelector(`[data-item-picker-open=\"${activeId}\"]`);if(trigger)trigger.focus();}"
    "for(const trigger of document.querySelectorAll('[data-item-picker-open]'))trigger.addEventListener('click',()=>show(trigger.dataset.itemPickerOpen));close.addEventListener('click',hide);clear.addEventListener('click',()=>{const input=inputById.get(activeId);if(!input)return;input.value='0';update(input);hide();});modal.addEventListener('click',event=>{if(event.target===modal)hide();});category.addEventListener('change',apply);search.addEventListener('input',apply);"
    "for(const form of document.querySelectorAll('form')){if(form.dataset.itemPickerSubmitBound==='1')continue;form.dataset.itemPickerSubmitBound='1';form.addEventListener('submit',event=>{const required=[...form.querySelectorAll('[data-item-picker-required]')].find(input=>!input.value||input.value==='0');if(!required)return;event.preventDefault();error.textContent='请先选择物品';show(required.id);});}"
    "if(!window.__cbeItemPickerDocBound){window.__cbeItemPickerDocBound=true;window.addEventListener('cbe-item-picker-sync',event=>{const input=document.getElementById(event.detail&&event.detail.id||'');if(!input)return;const option=[...document.querySelectorAll('#item-picker-options option')].find(o=>o.value===input.value);const text=option?option.textContent:(input.value&&input.value!=='0'?`未知物品 #${input.value}`:'未选择物品');for(const label of document.querySelectorAll('[data-item-picker-label]'))if(label.dataset.itemPickerLabel===input.id)label.textContent=text;});document.addEventListener('keydown',event=>{const open=document.querySelector('#item-picker-modal:not([hidden])');if(event.key==='Escape'&&open){open.hidden=true;document.body.classList.remove('modal-open');}});}apply();};"
    "const setupMonsterDrops=()=>{const box=document.querySelector('#monster-drop-list'),add=document.querySelector('#monster-drop-add');if(!box||!add)return;const rows=[...box.querySelectorAll('[data-drop-row]')];const sync=row=>{const input=row.querySelector('[data-item-picker-input]');if(input)window.dispatchEvent(new CustomEvent('cbe-item-picker-sync',{detail:{id:input.id}}));};const showNext=()=>{const next=rows.find(row=>row.hidden);if(next){next.hidden=false;sync(next);}add.disabled=!rows.some(row=>row.hidden);};add.addEventListener('click',showNext);for(const remove of box.querySelectorAll('[data-drop-remove]'))remove.addEventListener('click',()=>{const row=remove.closest('[data-drop-row]');if(!row)return;const input=row.querySelector('[data-item-picker-input]'),rate=row.querySelector('[data-drop-rate]');if(input)input.value='0';if(rate)rate.value='0';row.hidden=true;sync(row);if(!rows.some(current=>!current.hidden))showNext();add.disabled=false;});add.disabled=!rows.some(row=>row.hidden);};"
    "const setupUpdateFilter=()=>{"
    "const input=document.querySelector('#update-resource-filter');"
    "const select=document.querySelector('#update-resource-select');if(!input||!select)return;"
    "const apply=()=>{const wanted=input.value.trim().toLowerCase();"
    "for(const option of select.options){if(!option.value)continue;"
    "const show=!wanted||option.textContent.toLowerCase().includes(wanted);"
    "option.hidden=!show;option.disabled=!show;option.style.display=show?'':'none';}};"
    "input.addEventListener('input',apply);apply();};"
    "const setupAccountFilter=()=>{"
    "const input=document.querySelector('#account-search');"
    "const btn=document.querySelector('#account-search-btn');"
    "const list=document.querySelector('.accounts');"
    "if(!input||!btn||!list)return;"
    "const apply=()=>{const q=input.value.trim().toLowerCase();"
    "for(const a of list.querySelectorAll('a.account')){"
    "const key=(a.dataset.key||a.textContent||'').toLowerCase();"
    "a.hidden=!!q&&!key.includes(q);}};"
    "btn.addEventListener('click',apply);"
    "input.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();apply();}});};"
    "const setupSceneFilter=()=>{"
    "const input=document.getElementById('scene-search');"
    "const list=document.getElementById('scene-list');"
    "const btn=document.getElementById('scene-search-btn');"
    "if(!input||!list)return;"
    "const rows=[...list.querySelectorAll('a.scene')];"
    "const apply=()=>{const q=(input.value||'').trim().toLowerCase();"
    "for(const row of rows){"
    "const key=((row.getAttribute('data-key')||row.dataset.key||'')+' '+(row.textContent||'')).toLowerCase();"
    "const hide=!!q&&!key.includes(q);"
    "row.hidden=hide;"
    "if(hide){row.setAttribute('hidden','');row.style.display='none';row.classList.add('is-filtered');}"
    "else{row.removeAttribute('hidden');row.style.display='';row.classList.remove('is-filtered');}}};"
    "input.addEventListener('input',apply);"
    "input.addEventListener('keyup',apply);"
    "input.addEventListener('compositionend',apply);"
    "if(btn)btn.addEventListener('click',ev=>{ev.preventDefault();apply();});"
    "apply();};"
    "const setupMonsterFilter=()=>{"
    "const input=document.getElementById('monster-search');"
    "const list=document.getElementById('monster-list');"
    "const btn=document.getElementById('monster-search-btn');"
    "if(!input||!list)return;"
    "const rows=[...list.querySelectorAll('a.monster')];"
    "const apply=()=>{const q=(input.value||'').trim().toLowerCase();"
    "for(const row of rows){"
    "const key=((row.getAttribute('data-key')||row.dataset.key||'')+' '+(row.textContent||'')).toLowerCase();"
    "const hide=!!q&&!key.includes(q);"
    "row.hidden=hide;"
    "if(hide){row.setAttribute('hidden','');row.style.display='none';row.classList.add('is-filtered');}"
    "else{row.removeAttribute('hidden');row.style.display='';row.classList.remove('is-filtered');}}};"
    "input.addEventListener('input',apply);"
    "input.addEventListener('keyup',apply);"
    "input.addEventListener('compositionend',apply);"
    "input.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();apply();}});"
    "if(btn)btn.addEventListener('click',ev=>{ev.preventDefault();apply();});"
    "apply();};"
    "const setupNpcKinds=()=>{for(const form of document.querySelectorAll('.npc form')){"
    "const box6=form.querySelector('input[name=\"service_kind_6\"]');"
    "const fields=form.querySelector('.instance-fields');"
    "if(!fields)continue;const apply=()=>{const show=!!(box6&&box6.checked);"
    "fields.hidden=!show;for(const input of fields.querySelectorAll('input,select'))input.disabled=!show;};"
    "if(box6)box6.addEventListener('change',apply);"
    "for(const box of form.querySelectorAll('input[name^=\"service_kind_\"]'))box.addEventListener('change',apply);"
    "apply();}};"
    "const setupCosmeticGrant=()=>{"
    "const kind=document.getElementById('cosmetic-kind');"
    "const idSel=document.getElementById('cosmetic-id');"
    "const allBox=document.getElementById('cosmetic-all');"
    "if(!kind||!idSel)return;"
    "if(kind.dataset.cosmeticGrantReady==='1')return;kind.dataset.cosmeticGrantReady='1';"
    "const apply=()=>{"
    "const k=kind.value||'chenghao';"
    "const unlockAll=!!(allBox&&allBox.checked);"
    "let first=null;"
    "for(const opt of idSel.options){"
    "if(!opt.value||opt.value==='0'){opt.hidden=false;opt.disabled=false;continue;}"
    "const show=!unlockAll&&opt.dataset.kind===k;"
    "opt.hidden=!show;opt.disabled=!show;"
    "if(show&&!first)first=opt;}"
    "idSel.disabled=unlockAll;"
    "if(unlockAll){idSel.value='0';}"
    "else if(!idSel.selectedOptions.length||idSel.selectedOptions[0].hidden||idSel.selectedOptions[0].disabled){"
    "idSel.value=first?first.value:'0';}};"
    "kind.addEventListener('change',apply);"
    "if(allBox)allBox.addEventListener('change',apply);"
    "apply();};"
    "const setupCatalogSelects=()=>{"
    "for(const select of document.querySelectorAll('select[data-admin-catalog]')){"
    "const source=document.getElementById(select.dataset.adminCatalog);"
    "if(!source)continue;"
    "const current=select.value;"
    "const emptyLabel=select.dataset.adminEmpty;"
    "select.innerHTML='';"
    "if(emptyLabel!==undefined){"
    "const blank=new Option(emptyLabel,'',!current,!current);"
    "select.add(blank);}"
    "else if(select.required&&!current){"
    "const blank=new Option('\u8bf7\u9009\u62e9','',true,true);blank.disabled=true;select.add(blank);}"
    "let found=false;"
    "for(const opt of source.options){"
    "if(!opt.value&&emptyLabel===undefined)continue;"
    "const selected=!!current&&opt.value===current;"
    "if(selected)found=true;"
    "select.add(new Option(opt.textContent||opt.value,opt.value,selected,selected));}"
    "if(current&&!found){"
    "const missing=new Option(current+'\uff08\u8d44\u6e90\u4e0d\u5b58\u5728\uff0c\u8bf7\u91cd\u65b0\u9009\u62e9\uff09','',true,true);"
    "missing.disabled=true;select.add(missing,0);}"
    "}};"
    "const showAdminNotice=(ok,message)=>{"
    "let box=document.querySelector('#admin-ajax-toast');"
    "if(!box){box=document.createElement('div');box.id='admin-ajax-toast';"
    "box.setAttribute('role','status');document.body.appendChild(box);}"
    "const bg=ok?'#ecfdf3':'#fef3f2';const fg=ok?'#027a48':'#b42318';"
    "box.style.cssText='position:fixed;right:20px;bottom:20px;left:auto;top:auto;"
    "transform:none;z-index:2147483647;max-width:min(420px,calc(100vw - 40px));"
    "margin:0;padding:12px 16px;border-radius:10px;box-shadow:0 10px 30px #10182840;"
    "font:14px/1.45 system-ui,-apple-system,Segoe UI,sans-serif;pointer-events:none;"
    "background:'+bg+';color:'+fg+';border:1px solid '+(ok?'#abefc6':'#fecdca');"
    "box.hidden=false;box.textContent=message||(ok?'\u5df2\u4fdd\u5b58':'\u64cd\u4f5c\u5931\u8d25');"
    "clearTimeout(box._hideTimer);box._hideTimer=setTimeout(()=>{box.hidden=true;},2800);};"
    "const captureScroll=(from)=>{const items=[];let el=from;"
    "while(el){items.push({el,top:el.scrollTop,left:el.scrollLeft});el=el.parentElement;}"
    "items.push({el:document.documentElement,top:document.documentElement.scrollTop,left:document.documentElement.scrollLeft});"
    "items.push({el:document.body,top:document.body.scrollTop,left:document.body.scrollLeft});"
    "return {x:window.scrollX,y:window.scrollY,items};};"
    "const restoreScroll=(snap)=>{const apply=()=>{window.scrollTo(snap.x,snap.y);"
    "for(const it of snap.items){it.el.scrollTop=it.top;it.el.scrollLeft=it.left;}};"
    "apply();requestAnimationFrame(()=>{apply();requestAnimationFrame(apply);});};"
    "const sameAdminContext=(nextUrl)=>{try{"
    "const next=new URL(nextUrl,location.href);const cur=new URL(location.href);"
    "const norm=p=>String(p||'/').replace(/\\/+$/,'')||'/';"
    "if(norm(next.pathname)!==norm(cur.pathname))return false;"
    "const tabOf=u=>u.searchParams.get('tab')||'accounts';"
    "if(tabOf(next)!==tabOf(cur))return false;"
    "for(const key of ['chest','monster','scene','account']){"
    "const n=next.searchParams.get(key);const c=cur.searchParams.get(key);"
    "if(n!=null&&c!=null&&n!==c)return false;}"
    "return true;}catch(_){return true;}};"
    "const refreshRoleDetails=async(url)=>{"
    "try{"
    "const page=await fetch(url,{credentials:'same-origin',headers:{'Accept':'text/html'}});"
    "if(!page.ok)return false;"
    "const html=await page.text();"
    "const doc=new DOMParser().parseFromString(html,'text/html');"
    "const next=doc.querySelector('#role-details');"
    "const cur=document.querySelector('#role-details');"
    "if(!next||!cur)return false;"
    "cur.replaceWith(next);"
    "document.body.classList.remove('modal-open');"
    "setupItemPicker();setupCosmeticGrant();"
    "return true;"
    "}catch(_){return false;}};"
    "const setupAjaxForms=()=>{"
    "document.addEventListener('submit',async event=>{"
    "const form=event.target;if(!(form instanceof HTMLFormElement))return;"
    "if(form.dataset.adminAjax==='off')return;"
    "if(event.defaultPrevented)return;"
    "const rawAction=(form.getAttribute('action')||'').split('?')[0];"
    "if(!rawAction.endsWith('/action')&&rawAction!=='/action'&&rawAction!=='action')return;"
    "event.preventDefault();"
    "const postUrl=form.getAttribute('action')||'';"
    "if(!postUrl)return;"
    "const formData=new FormData(form);"
    "const submitter=event.submitter;"
    "if(submitter instanceof HTMLButtonElement||submitter instanceof HTMLInputElement){"
    "if(submitter.name)formData.set(submitter.name,submitter.value);}"
    "const body=new URLSearchParams();"
    "for(const [key,value] of formData.entries())body.append(key,String(value));"
    "const buttons=[...form.querySelectorAll('button,input[type=submit]')];"
    "const snap=captureScroll(form);"
    "if(document.activeElement instanceof HTMLElement)document.activeElement.blur();"
    "for(const button of buttons)button.disabled=true;"
    "try{"
    "const response=await fetch(postUrl,{method:'POST',body,credentials:'same-origin',"
    "headers:{'X-Requested-With':'XMLHttpRequest','Accept':'application/json',"
    "'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'}});"
    "const type=(response.headers.get('content-type')||'').toLowerCase();"
    "if(!type.includes('application/json')){restoreScroll(snap);location.assign(response.url||postUrl);return;}"
    "const data=await response.json();"
    "const target=data&&data.location?data.location:location.href;"
    "const ok=!(data&&data.ok===false);"
    "let message='';"
    "try{message=new URL(target,location.href).searchParams.get('message')||'';}catch(_){}"
    "if(!message)message=ok?'\u5df2\u4fdd\u5b58':'\u64cd\u4f5c\u5931\u8d25';"
    "if(sameAdminContext(target)){showAdminNotice(ok,message);"
    "const action=String(formData.get('action')||'');"
    "if(ok&&(action==='add-wcoin'||action==='add-money'||"
    "action==='rename-role'||action==='set-role-exp'||"
    "action==='set-equip-wash'||action==='grant-item'||"
    "action==='clear-ring-task'))"
    "await refreshRoleDetails(target);"
    "const row=form.closest('tr');"
    "const enabled=form.querySelector('select[name=enabled]');"
    "const npcEnabled=form.querySelector('select[name=npc_enabled]');"
    "if(row&&enabled){const cells=row.querySelectorAll('td');"
    "if(cells.length>=4){"
    "if(npcEnabled){"
    "cells[cells.length-2].innerHTML="
    "'<span class=\"state\">"
    "<span class=\"'+(enabled.value==='1'?'on':'off')+'\">\\u5546\\u57ce'+"
    "(enabled.value==='1'?'\\u5df2\\u4e0a\\u67b6':'\\u672a\\u4e0a\\u67b6')+'</span>"
    "<span class=\"'+(npcEnabled.value==='1'?'on':'off')+'\">NPC'+"
    "(npcEnabled.value==='1'?'\\u5df2\\u4e0a\\u67b6':'\\u672a\\u4e0a\\u67b6')+'</span></span>';"
    "}else{"
    "cells[cells.length-2].textContent=enabled.value==='1'?'\\u542f\\u7528':'\\u7981\\u7528';}}"
    "}"
    "if(action==='chest-delete'&&row)row.remove();"
    "restoreScroll(snap);return;}"
    "restoreScroll(snap);location.assign(target);"
    "}catch(error){showAdminNotice(false,'\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5');restoreScroll(snap);}"
    "finally{for(const button of buttons)button.disabled=false;restoreScroll(snap);}"
    "});};"
    "document.addEventListener('DOMContentLoaded',()=>{"
    "keep('.accounts','cbe-admin-accounts-scroll');"
    "keep('.scene-list','cbe-admin-scenes-scroll');"
    "keep('.shop-list','cbe-admin-shop-scroll');"
    "keep('.update-left','cbe-admin-update-left-scroll');"
    "keep('.update-right','cbe-admin-update-right-scroll');"
    "setupCatalogSelects();setupItemPicker();setupMonsterDrops();setupUpdateFilter();setupNpcKinds();"
    "setupAccountFilter();setupSceneFilter();setupMonsterFilter();setupCosmeticGrant();setupAjaxForms();setupMapEditor();setupSceneNpcDrag();});"
    "const adminRoot=()=>{const p=location.pathname;return p.endsWith('/')?p:p.replace(/[^/]*$/,'');};"
    "const b64ToU32=(b64)=>{const bin=atob(b64);const n=bin.length>>2;const out=new Uint32Array(n);"
    "for(let i=0;i<n;i++){const o=i*4;out[i]=bin.charCodeAt(o)|(bin.charCodeAt(o+1)<<8)|(bin.charCodeAt(o+2)<<16)|(bin.charCodeAt(o+3)<<24);}return out;};"
    "const u32ToB64=(arr)=>{const bytes=new Uint8Array(arr.length*4);for(let i=0;i<arr.length;i++){const v=arr[i]>>>0;const o=i*4;"
    "bytes[o]=v&255;bytes[o+1]=(v>>>8)&255;bytes[o+2]=(v>>>16)&255;bytes[o+3]=(v>>>24)&255;}"
    "let s='';for(let i=0;i<bytes.length;i++)s+=String.fromCharCode(bytes[i]);return btoa(s);};"
    "const setupMapEditor=async()=>{"
    "const root=document.getElementById('map-editor');if(!root)return;"
    "const scene=root.dataset.scene||'';if(!scene)return;"
    "const canvas=document.getElementById('map-canvas');const paletteEl=document.getElementById('map-palette');"
    "const metaEl=document.getElementById('map-cursor-meta');const warnEl=document.getElementById('map-shared-warn');"
    "const showCol=document.getElementById('map-show-collision');const colMode=document.getElementById('map-collision-mode');"
    "const catalogEl=document.getElementById('map-sheet-catalog');const filterEl=document.getElementById('map-sheet-filter');"
    "const previewBox=document.getElementById('map-sheet-preview');const previewImg=document.getElementById('map-sheet-preview-img');"
    "const previewNameEl=document.getElementById('map-sheet-preview-name');const addBtn=document.getElementById('map-sheet-add');"
    "const sheetMeta=document.getElementById('map-sheet-meta');"
    "if(!canvas||!paletteEl||!catalogEl)return;"
    "const ctx=canvas.getContext('2d');ctx.imageSmoothingEnabled=false;"
    "let tool='paint';let brush=0;let sheetIndex=0;let tileIndex=0;let cells=null;let map=null;let sheets=[];"
    "let imageNames=[];let availableSheets=[];let selectedCatalog='';let thumbCache={};let dragging=false;let rectStart=null;"
    "const pack=(si,ti,flags)=>(((flags&15)<<28)|((si&15)<<24)|(ti&0xffffff))>>>0;"
    "const cellAt=(gx,gy)=>cells[gx*map.rows+gy];"
    "const setCell=(gx,gy,v)=>{if(gx<0||gy<0||gx>=map.cols||gy>=map.rows)return;cells[gx*map.rows+gy]=v>>>0;};"
    "const sheetUrl=(name)=>adminRoot()+'tile-sheet.bmp?name='+encodeURIComponent(name);"
    "const loadSheet=(name)=>new Promise((resolve,reject)=>{if(thumbCache[name]&&thumbCache[name].complete){resolve(thumbCache[name]);return;}"
    "const img=new Image();img.onload=()=>{thumbCache[name]=img;resolve(img);};img.onerror=reject;img.src=sheetUrl(name)+'&_='+Date.now();});"
    "const drawMap=()=>{"
    "if(!map||!cells)return;ctx.clearRect(0,0,canvas.width,canvas.height);"
    "for(let gx=0;gx<map.cols;gx++)for(let gy=0;gy<map.rows;gy++){"
    "const packed=cellAt(gx,gy);const si=(packed>>>24)&15;const ti=packed&0xffffff;const sh=sheets[si];"
    "const dx=gx*map.tileWidth,dy=gy*map.tileHeight;"
    "if(sh&&sh.img&&sh.tpr>0){const sx=(ti%sh.tpr)*map.tileWidth,sy=((ti/sh.tpr)|0)*map.tileHeight;"
    "ctx.drawImage(sh.img,sx,sy,map.tileWidth,map.tileHeight,dx,dy,map.tileWidth,map.tileHeight);}"
    "else{ctx.fillStyle='#f0f';ctx.fillRect(dx,dy,map.tileWidth,map.tileHeight);}"
    "if(showCol&&showCol.checked){const fl=(packed>>>28)&15;if(fl){ctx.fillStyle=fl===15?'rgba(180,35,24,.35)':'rgba(253,176,34,.35)';"
    "ctx.fillRect(dx,dy,map.tileWidth,map.tileHeight);ctx.strokeStyle='rgba(255,255,255,.5)';ctx.strokeRect(dx+.5,dy+.5,map.tileWidth-1,map.tileHeight-1);}}"
    "}};"
    "const usableCatalog=()=>{const q=((filterEl&&filterEl.value)||'').trim().toLowerCase();"
    "return availableSheets.filter(n=>!q||n.toLowerCase().includes(q)).sort((a,b)=>a.localeCompare(b,'zh'));};"
    "const updatePreviewUi=()=>{const max=map&&map.imageMax?map.imageMax:16;const full=imageNames.length>=max;"
    "const used=selectedCatalog&&imageNames.includes(selectedCatalog);"
    "if(previewBox)previewBox.classList.toggle('on',!!selectedCatalog);"
    "if(previewNameEl)previewNameEl.textContent=selectedCatalog||'';"
    "if(previewImg){if(selectedCatalog){previewImg.src=sheetUrl(selectedCatalog);previewImg.alt=selectedCatalog;}else{previewImg.removeAttribute('src');}}"
    "if(addBtn){addBtn.disabled=!selectedCatalog||full||used;addBtn.textContent=full?'\\u5df2\\u8fbe\\u4e0a\\u9650':(used?'\\u5df2\\u5728\\u8c03\\u8272\\u677f':'\\u6dfb\\u52a0\\u6b64\\u56fe\\u96c6');}"
    "if(sheetMeta)sheetMeta.textContent='\\u56fe\\u96c6 '+imageNames.length+' / '+max+' \\u00b7 GIF '+usableCatalog().length+' / '+availableSheets.length;};"
    "const selectCatalog=(name)=>{selectedCatalog=name||'';updatePreviewUi();"
    "catalogEl.querySelectorAll('.sheet-card').forEach(c=>c.classList.toggle('on',c.dataset.name===selectedCatalog));};"
    "const refreshCatalog=()=>{const opts=usableCatalog();const max=map&&map.imageMax?map.imageMax:16;const full=imageNames.length>=max;"
    "catalogEl.innerHTML='';"
    "if(!opts.length){const empty=document.createElement('div');empty.className='meta';"
    "empty.textContent=availableSheets.length?'\\u6ca1\\u6709\\u5339\\u914d\\u7684 GIF':'\\u6ca1\\u6709 GIF \\u8d44\\u6e90';catalogEl.appendChild(empty);updatePreviewUi();return;}"
    "if(selectedCatalog&&!opts.includes(selectedCatalog))selectedCatalog='';"
    "opts.forEach(name=>{const used=imageNames.includes(name);const card=document.createElement('div');card.className='sheet-card'+(name===selectedCatalog?' on':'');card.dataset.name=name;"
    "const thumb=document.createElement('img');thumb.className='thumb';thumb.alt=name;thumb.loading='lazy';thumb.src=sheetUrl(name);"
    "thumb.onerror=()=>{const ph=document.createElement('div');ph.className='thumb missing';ph.textContent='?';thumb.replaceWith(ph);};"
    "const label=document.createElement('div');label.className='name';label.textContent=name;"
    "const btn=document.createElement('button');btn.type='button';btn.textContent=used?'\\u5df2\\u6dfb\\u52a0':'\\u6dfb\\u52a0';btn.disabled=full||used;"
    "btn.addEventListener('click',ev=>{ev.stopPropagation();selectCatalog(name);addSheet(name);});"
    "card.addEventListener('click',()=>selectCatalog(name));"
    "card.appendChild(thumb);card.appendChild(label);card.appendChild(btn);catalogEl.appendChild(card);});"
    "updatePreviewUi();};"
    "const rebuildPalette=()=>{"
    "paletteEl.innerHTML='';"
    "imageNames.forEach((name,i)=>{const sh=sheets[i];if(!sh||!sh.img)return;"
    "const block=document.createElement('div');block.className='palette-sheet';"
    "const head=document.createElement('div');head.className='palette-sheet-head';"
    "const title=document.createElement('h3');title.textContent='['+i+'] '+name;head.appendChild(title);"
    "const rm=document.createElement('button');rm.type='button';rm.className='danger';rm.textContent='\\u79fb\\u9664';"
    "rm.disabled=imageNames.length<=1;rm.addEventListener('click',()=>removeSheet(i));head.appendChild(rm);"
    "block.appendChild(head);"
    "const grid=document.createElement('div');grid.className='palette-grid';"
    "grid.style.gridTemplateColumns='repeat('+Math.max(1,sh.tpr)+', '+map.tileWidth+'px)';"
    "for(let ti=0;ti<sh.tpr*sh.tpc;ti++){const c=document.createElement('canvas');c.width=map.tileWidth;c.height=map.tileHeight;"
    "c.dataset.sheet=String(i);c.dataset.tile=String(ti);const cctx=c.getContext('2d');cctx.imageSmoothingEnabled=false;"
    "cctx.drawImage(sh.img,(ti%sh.tpr)*map.tileWidth,((ti/sh.tpr)|0)*map.tileHeight,map.tileWidth,map.tileHeight,0,0,map.tileWidth,map.tileHeight);"
    "c.addEventListener('click',()=>{sheetIndex=i;tileIndex=ti;brush=pack(i,ti,(brush>>>28)&15);"
    "paletteEl.querySelectorAll('canvas').forEach(x=>x.classList.toggle('on',x===c));});"
    "if(i===sheetIndex&&ti===tileIndex)c.classList.add('on');grid.appendChild(c);}"
    "block.appendChild(grid);paletteEl.appendChild(block);});"
    "refreshCatalog();};"
    "const removeSheet=async(index)=>{"
    "if(imageNames.length<=1){showAdminNotice(false,'\\u81f3\\u5c11\\u4fdd\\u7559\\u4e00\\u5f20\\u56fe\\u96c6');return;}"
    "if(!confirm('\\u79fb\\u9664\\u56fe\\u96c6 '+imageNames[index]+' \\uff1f\\u5f15\\u7528\\u8be5\\u56fe\\u96c6\\u7684\\u683c\\u5b50\\u4f1a\\u6539\\u4e3a\\u7b2c0\\u5f20\\u7b2c0\\u74f6\\u3002'))return;"
    "for(let n=0;n<cells.length;n++){const packed=cells[n]>>>0;const si=(packed>>>24)&15;const ti=packed&0xffffff;const fl=(packed>>>28)&15;"
    "if(si===index)cells[n]=pack(0,0,fl);else if(si>index)cells[n]=pack(si-1,ti,fl);}"
    "imageNames.splice(index,1);sheets.splice(index,1);"
    "if(sheetIndex===index){sheetIndex=0;tileIndex=0;}else if(sheetIndex>index)sheetIndex--;"
    "rebuildPalette();drawMap();showAdminNotice(true,'\\u5df2\\u79fb\\u9664\\u56fe\\u96c6');};"
    "const addSheet=async(name)=>{name=name||selectedCatalog;if(!name)return;const max=map.imageMax||16;"
    "if(imageNames.includes(name)){showAdminNotice(false,'\\u56fe\\u96c6\\u5df2\\u5728\\u5217\\u8868\\u4e2d');return;}"
    "if(imageNames.length>=max){showAdminNotice(false,'\\u56fe\\u96c6\\u6570\\u91cf\\u5df2\\u8fbe\\u4e0a\\u9650');return;}"
    "try{const img=await loadSheet(name);const tpr=(img.width/map.tileWidth)|0;const tpc=(img.height/map.tileHeight)|0;"
    "if(tpr<=0||tpc<=0)throw new Error('bad sheet');"
    "imageNames.push(name);sheets.push({img,tpr,tpc,name});sheetIndex=imageNames.length-1;tileIndex=0;selectedCatalog='';"
    "rebuildPalette();drawMap();showAdminNotice(true,'\\u5df2\\u6dfb\\u52a0 '+name);}catch(_){showAdminNotice(false,'\\u52a0\\u8f7d\\u56fe\\u96c6\\u5931\\u8d25');}};"
    "const pickAt=(gx,gy)=>{const packed=cellAt(gx,gy);sheetIndex=(packed>>>24)&15;tileIndex=packed&0xffffff;brush=packed;"
    "paletteEl.querySelectorAll('canvas').forEach(c=>c.classList.toggle('on',(+c.dataset.sheet)===sheetIndex&&(+c.dataset.tile)===tileIndex));};"
    "const paintAt=(gx,gy)=>{if(tool==='eyedrop'){pickAt(gx,gy);return;}const cur=cellAt(gx,gy);"
    "if(tool==='collision'){const flags=parseInt(colMode.value,10)||0;setCell(gx,gy,pack((cur>>>24)&15,cur&0xffffff,flags));return;}"
    "setCell(gx,gy,pack(sheetIndex,tileIndex,(cur>>>28)&15));};"
    "const fillRect=(x0,y0,x1,y1)=>{const xa=Math.min(x0,x1),xb=Math.max(x0,x1),ya=Math.min(y0,y1),yb=Math.max(y0,y1);"
    "for(let gx=xa;gx<=xb;gx++)for(let gy=ya;gy<=yb;gy++)paintAt(gx,gy);};"
    "const gridFromEvent=(ev)=>{const r=canvas.getBoundingClientRect();const scaleX=canvas.width/r.width,scaleY=canvas.height/r.height;"
    "const x=((ev.clientX-r.left)*scaleX)|0,y=((ev.clientY-r.top)*scaleY)|0;"
    "return{gx:Math.min(map.cols-1,Math.max(0,(x/map.tileWidth)|0)),gy:Math.min(map.rows-1,Math.max(0,(y/map.tileHeight)|0))};};"
    "document.querySelectorAll('[data-tool]').forEach(btn=>btn.addEventListener('click',()=>{"
    "tool=btn.getAttribute('data-tool')||'paint';document.querySelectorAll('[data-tool]').forEach(b=>b.classList.toggle('on',b===btn));}));"
    "if(showCol)showCol.addEventListener('change',drawMap);"
    "if(addBtn)addBtn.addEventListener('click',()=>addSheet());"
    "if(filterEl)filterEl.addEventListener('input',()=>refreshCatalog());"
    "canvas.addEventListener('mousedown',ev=>{const g=gridFromEvent(ev);dragging=true;if(tool==='rect'){rectStart=g;return;}"
    "paintAt(g.gx,g.gy);drawMap();});"
    "canvas.addEventListener('mousemove',ev=>{const g=gridFromEvent(ev);const packed=cellAt(g.gx,g.gy);"
    "if(metaEl)metaEl.textContent='('+g.gx+','+g.gy+') sheet='+((packed>>>24)&15)+' tile='+(packed&0xffffff)+' flags=0x'+((packed>>>28)&15).toString(16);"
    "if(!dragging||tool==='rect'||tool==='eyedrop')return;paintAt(g.gx,g.gy);drawMap();});"
    "window.addEventListener('mouseup',ev=>{if(!dragging)return;dragging=false;if(tool==='rect'&&rectStart){const g=gridFromEvent(ev);fillRect(rectStart.gx,rectStart.gy,g.gx,g.gy);drawMap();rectStart=null;}});"
    "const saveBtn=document.getElementById('map-save');"
    "if(saveBtn)saveBtn.addEventListener('click',async()=>{"
    "if(!map||!cells||!imageNames.length)return;saveBtn.disabled=true;try{"
    "const body=new URLSearchParams();body.set('action','save-scene-map');body.set('scene',scene);"
    "body.set('map_name',map.mapName);body.set('cell_count',String(map.cellCount));body.set('cells_b64',u32ToB64(cells));"
    "body.set('image_names',imageNames.join('\\n'));"
    "const response=await fetch(adminRoot()+'action',{method:'POST',body,credentials:'same-origin',"
    "headers:{'X-Requested-With':'XMLHttpRequest','Accept':'application/json','Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'}});"
    "const type=(response.headers.get('content-type')||'').toLowerCase();"
    "if(!type.includes('application/json')){location.assign(response.url||location.href);return;}"
    "const data=await response.json();const target=data&&data.location?data.location:location.href;location.assign(target);"
    "}catch(_){showAdminNotice(false,'\\u5730\\u56fe\\u4fdd\\u5b58\\u5931\\u8d25');}finally{saveBtn.disabled=false;}});"
    "try{"
    "const res=await fetch(adminRoot()+'map-edit.json?scene='+encodeURIComponent(scene),{credentials:'same-origin'});"
    "map=await res.json();if(!map||!map.ok)throw new Error('bad map');"
    "cells=b64ToU32(map.cellsB64);canvas.width=map.width;canvas.height=map.height;"
    "imageNames=Array.isArray(map.imageNames)?map.imageNames.slice():[];"
    "availableSheets=Array.isArray(map.availableSheets)?map.availableSheets.slice():[];"
    "if(warnEl&&map.sharedBy&&map.sharedBy.length){warnEl.hidden=false;warnEl.textContent='\\u8b66\\u544a\\uff1a\\u6b64 .map \\u88ab\\u4ee5\\u4e0b\\u573a\\u666f\\u5171\\u4eab\\uff0c\\u4fdd\\u5b58\\u4f1a\\u4e00\\u5e76\\u5f71\\u54cd\\uff1a '+map.sharedBy.join('\\u3001');}"
    "sheets=[];for(let i=0;i<imageNames.length;i++){const name=imageNames[i];const img=await loadSheet(name);"
    "const tpr=(img.width/map.tileWidth)|0;const tpc=(img.height/map.tileHeight)|0;sheets.push({img,tpr,tpc,name});}"
    "rebuildPalette();drawMap();showAdminNotice(true,'\\u5730\\u56fe\\u5df2\\u52a0\\u8f7d');"
    "}catch(err){showAdminNotice(false,'\\u5730\\u56fe\\u52a0\\u8f7d\\u5931\\u8d25');}"
    "};"
    "const setupSceneNpcDrag=()=>{"
    "const stage=document.getElementById('scene-preview-stage');"
    "if(!stage)return;"
    "const scene=stage.dataset.scene||'';"
    "const mapW=Math.max(1,parseInt(stage.dataset.width,10)||0);"
    "const mapH=Math.max(1,parseInt(stage.dataset.height,10)||0);"
    "if(!scene||!mapW||!mapH)return;"
    "const clamp=(v,lo,hi)=>Math.min(hi,Math.max(lo,v|0));"
    "const mapPointFromEvent=(ev)=>{"
    "const r=stage.getBoundingClientRect();"
    "const scaleX=mapW/Math.max(1,r.width),scaleY=mapH/Math.max(1,r.height);"
    "return{x:clamp(Math.round((ev.clientX-r.left)*scaleX),1,mapW),"
    "y:clamp(Math.round((ev.clientY-r.top)*scaleY),1,mapH)};};"
    "const pinKind=pin=>(pin.dataset.kind||'npc');"
    "const postAction=async(body)=>{"
    "const response=await fetch(adminRoot()+'action',{method:'POST',body,credentials:'same-origin',"
    "headers:{'X-Requested-With':'XMLHttpRequest','Accept':'application/json',"
    "'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'}});"
    "const type=(response.headers.get('content-type')||'').toLowerCase();"
    "if(!type.includes('application/json')){location.assign(response.url||location.href);return null;}"
    "return await response.json();};"
    "const noticeFromData=(data,fallbackOk,fallbackFail)=>{"
    "const ok=!(data&&data.ok===false);"
    "let message='';"
    "try{message=new URL(data&&data.location?data.location:location.href,location.href).searchParams.get('message')||'';}catch(_){}"
    "if(!message)message=ok?fallbackOk:fallbackFail;"
    "return{ok,message,location:(data&&data.location)||location.href};};"
    "const syncForm=(pin,x,y)=>{"
    "if(pinKind(pin)==='combat'){"
    "const idx=pin.dataset.spawnIndex;"
    "if(idx===undefined||idx==='')return;"
    "const xi=document.querySelector('input[name=\"spawn_x_'+idx+'\"]');"
    "const yi=document.querySelector('input[name=\"spawn_y_'+idx+'\"]');"
    "if(xi)xi.value=String(x);if(yi)yi.value=String(y);return;}"
    "const actorId=pin.dataset.actorId;if(!actorId)return;"
    "for(const form of document.querySelectorAll('.npc form')){"
    "const idInput=form.querySelector('input[name=actor_id]');"
    "if(!idInput||String(idInput.value)!==String(actorId))continue;"
    "const xi=form.querySelector('input[name=x]');"
    "const yi=form.querySelector('input[name=y]');"
    "if(xi)xi.value=String(x);if(yi)yi.value=String(y);}};"
    "const syncChip=(pin,x,y)=>{"
    "if(pinKind(pin)!=='npc')return;"
    "const actorId=pin.dataset.actorId;if(!actorId)return;"
    "for(const chip of document.querySelectorAll('.preview-npcs .npc-chip[data-actor-id=\"'+actorId+'\"]')){"
    "const xy=chip.querySelector('.npc-xy');"
    "if(xy)xy.textContent='('+x+','+y+')';}};"
    "const applyPinPos=(pin,x,y)=>{"
    "pin.style.left=x+'px';pin.style.top=y+'px';"
    "pin.dataset.x=String(x);pin.dataset.y=String(y);"
    "const name=pin.querySelector('.pin-name');"
    "const label=name?name.textContent.trim():'NPC';"
    "const title=pin.getAttribute('title')||'';"
    "pin.setAttribute('title',title.replace(/\\(\\d+,\\d+\\)/,'('+x+','+y+')')||"
    "(label+' \\u00b7 ('+x+','+y+')'));};"
    "const savePos=async(pin,x,y)=>{"
    "const kind=pinKind(pin);"
    "const body=new URLSearchParams();"
    "body.set('scene',scene);body.set('x',String(x));body.set('y',String(y));"
    "if(kind==='combat'){"
    "const idx=pin.dataset.spawnIndex;"
    "if(idx===undefined||idx==='')return;"
    "body.set('action','save-combat-spawn-position');body.set('spawn_index',idx);}"
    "else{"
    "const actorId=pin.dataset.actorId;if(!actorId)return;"
    "body.set('action','save-npc-position');body.set('actor_id',actorId);}"
    "try{"
    "const data=await postAction(body);if(!data)return;"
    "const result=noticeFromData(data,'\\u5750\\u6807\\u5df2\\u4fdd\\u5b58','\\u5750\\u6807\\u4fdd\\u5b58\\u5931\\u8d25');"
    "showAdminNotice(result.ok,result.message);"
    "if(result.ok){pin.dataset.savedX=String(x);pin.dataset.savedY=String(y);"
    "syncForm(pin,x,y);syncChip(pin,x,y);}"
    "else{const ox=parseInt(pin.dataset.savedX||pin.dataset.x,10)||x;"
    "const oy=parseInt(pin.dataset.savedY||pin.dataset.y,10)||y;applyPinPos(pin,ox,oy);}"
    "}catch(_){showAdminNotice(false,'\\u5750\\u6807\\u4fdd\\u5b58\\u5931\\u8d25');"
    "const ox=parseInt(pin.dataset.savedX||pin.dataset.x,10)||x;"
    "const oy=parseInt(pin.dataset.savedY||pin.dataset.y,10)||y;applyPinPos(pin,ox,oy);}};"
    "const copyPin=async(pin)=>{"
    "const kind=pinKind(pin);"
    "const body=new URLSearchParams();body.set('scene',scene);"
    "if(kind==='combat'){"
    "const idx=pin.dataset.spawnIndex;"
    "if(idx===undefined||idx==='')return;"
    "body.set('action','copy-combat-spawn');body.set('spawn_index',idx);}"
    "else{"
    "const actorId=pin.dataset.actorId;if(!actorId)return;"
    "body.set('action','copy-npc');body.set('actor_id',actorId);}"
    "try{"
    "const data=await postAction(body);if(!data)return;"
    "const result=noticeFromData(data,'\\u5df2\\u590d\\u5236','\\u590d\\u5236\\u5931\\u8d25');"
    "showAdminNotice(result.ok,result.message);"
    "if(result.ok)location.assign(result.location);"
    "}catch(_){showAdminNotice(false,'\\u590d\\u5236\\u5931\\u8d25');}};"
    "const modal=document.getElementById('scene-pin-modal');"
    "const modalBody=document.getElementById('scene-pin-modal-body');"
    "const modalTitle=document.getElementById('scene-pin-modal-title');"
    "const modalClose=document.getElementById('scene-pin-modal-close');"
    "const panelHome=new WeakMap();"
    "const closePinEdit=()=>{"
    "if(!modal||!modalBody)return;"
    "for(const el of [...modalBody.children]){"
    "if(el.id==='combat-spawn-form')el.querySelectorAll('.combat-card').forEach(c=>{c.hidden=false;});"
    "const home=panelHome.get(el);"
    "if(home)home.appendChild(el);else{"
    "const store=document.getElementById('scene-npc-store')||document.querySelector('.scene-edit-store');"
    "if(store)store.appendChild(el);}}"
    "modal.hidden=true;modal.setAttribute('hidden','');document.body.classList.remove('modal-open');};"
    "const openPinEdit=(el,title,combatIndex)=>{"
    "if(!modal||!modalBody||!el){showAdminNotice(false,'\\u672a\\u627e\\u5230\\u7f16\\u8f91\\u8868\\u5355');return;}"
    "closePinEdit();"
    "if(!panelHome.has(el))panelHome.set(el,el.parentElement);"
    "modalBody.appendChild(el);"
    "if(el.id==='combat-spawn-form'){"
    "el.querySelectorAll('.combat-card').forEach(card=>{"
    "const idx=card.dataset.combatIndex;"
    "const isNew=card.dataset.combatNew==='1';"
    "if(combatIndex==='new')card.hidden=!isNew;"
    "else card.hidden=String(idx)!==String(combatIndex);});}"
    "if(modalTitle)modalTitle.textContent=title||'\\u7f16\\u8f91';"
    "modal.removeAttribute('hidden');modal.hidden=false;document.body.classList.add('modal-open');};"
    "if(modalClose)modalClose.addEventListener('click',ev=>{ev.preventDefault();ev.stopPropagation();closePinEdit();});"
    "if(modal)modal.addEventListener('click',ev=>{if(ev.target===modal)closePinEdit();});"
    "document.addEventListener('keydown',ev=>{if(ev.key==='Escape'&&modal&&!modal.hidden)closePinEdit();});"
    "const btnNewNpc=document.getElementById('btn-open-new-npc');"
    "if(btnNewNpc)btnNewNpc.addEventListener('click',()=>{"
    "openPinEdit(document.getElementById('npc-panel-new'),'\\u65b0\\u589e\\u52a8\\u6001 NPC');});"
    "const btnNewCombat=document.getElementById('btn-open-new-combat');"
    "if(btnNewCombat)btnNewCombat.addEventListener('click',()=>{"
    "openPinEdit(document.getElementById('combat-spawn-form'),'\\u65b0\\u589e\\u5237\\u602a\\u70b9','new');});"
    "for(const pin of stage.querySelectorAll('.npc-pin.copyable,.npc-pin.draggable')){"
    "pin.dataset.savedX=pin.dataset.x||'';pin.dataset.savedY=pin.dataset.y||'';"
    "pin.addEventListener('contextmenu',ev=>{"
    "ev.preventDefault();ev.stopPropagation();"
    "const kind=pinKind(pin);"
    "if(kind==='combat'){"
    "openPinEdit(document.getElementById('combat-spawn-form'),'\\u7f16\\u8f91\\u5237\\u602a\\u70b9',pin.dataset.spawnIndex);"
    "return;}"
    "const panelId=pin.dataset.editPanel||('npc-panel-'+(pin.dataset.actorId||''));"
    "const panel=document.getElementById(panelId);"
    "if(!panel){showAdminNotice(false,'\\u8be5 NPC \\u65e0\\u53ef\\u7f16\\u8f91\\u8868\\u5355');return;}"
    "openPinEdit(panel,'\\u7f16\\u8f91 NPC');});"
    "pin.addEventListener('dblclick',ev=>{"
    "ev.preventDefault();ev.stopPropagation();"
    "copyPin(pin);});"
    "if(!pin.classList.contains('draggable'))continue;"
    "pin.addEventListener('pointerdown',ev=>{"
    "if(ev.button!==0)return;"
    "ev.preventDefault();ev.stopPropagation();"
    "const kind=pinKind(pin);"
    "if(kind==='combat'){if(pin.dataset.spawnIndex===undefined||pin.dataset.spawnIndex==='')return;}"
    "else if(!pin.dataset.actorId)return;"
    "const startX=parseInt(pin.dataset.x,10)||1;const startY=parseInt(pin.dataset.y,10)||1;"
    "const originClientX=ev.clientX,originClientY=ev.clientY;"
    "pin.dataset.savedX=String(startX);pin.dataset.savedY=String(startY);"
    "let curX=startX,curY=startY,moved=false,dragging=false;"
    "const onMove=ev2=>{"
    "const dist=Math.hypot(ev2.clientX-originClientX,ev2.clientY-originClientY);"
    "if(!dragging){"
    "if(dist<5)return;"
    "dragging=true;moved=true;pin.classList.add('dragging');"
    "try{pin.setPointerCapture(ev2.pointerId);}catch(_){}}"
    "const p=mapPointFromEvent(ev2);curX=p.x;curY=p.y;applyPinPos(pin,curX,curY);};"
    "const onUp=async ev2=>{"
    "pin.removeEventListener('pointermove',onMove);"
    "pin.removeEventListener('pointerup',onUp);"
    "pin.removeEventListener('pointercancel',onUp);"
    "pin.classList.remove('dragging');"
    "try{if(dragging)pin.releasePointerCapture(ev2.pointerId);}catch(_){}"
    "if(!moved)return;"
    "applyPinPos(pin,curX,curY);syncForm(pin,curX,curY);syncChip(pin,curX,curY);"
    "await savePos(pin,curX,curY);};"
    "pin.addEventListener('pointermove',onMove);"
    "pin.addEventListener('pointerup',onUp);"
    "pin.addEventListener('pointercancel',onUp);});}"
    "};"
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

/* Cookie names ignore port by RFC; include listen port so one browser can keep
 * sessions for multiple mock-admin instances on the same host. */
static void vm_mock_admin_session_cookie_name(char *out, size_t outCap)
{
    snprintf(out, outCap, "cbe_admin_%u", (unsigned)g_mockAdminPort);
}

static bool vm_mock_admin_request_is_authenticated(const char *request,
                                                    size_t headerLen)
{
    char cookie[1024];
    char key[48];
    size_t keyLen = 0;
    const char *cursor = NULL;

    vm_mock_admin_ensure_session_token();
    vm_mock_admin_session_cookie_name(key, sizeof(key) - 1);
    keyLen = strlen(key);
    if (keyLen + 1 >= sizeof(key))
        return false;
    key[keyLen++] = '=';
    key[keyLen] = 0;
    if (!vm_mock_admin_header_value(request, headerLen, "Cookie",
                                    cookie, sizeof(cookie)))
    {
        return false;
    }
    cursor = cookie;
    while ((cursor = strstr(cursor, key)) != NULL)
    {
        const char *value = cursor + keyLen;
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

static bool vm_mock_admin_json_escape(const char *text, char *out, size_t outCap)
{
    size_t pos = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (text == NULL)
        return true;
    for (; *text != 0; ++text)
    {
        char ch = *text;

        if (ch == '\\' || ch == '"')
        {
            if (pos + 2 >= outCap)
                return false;
            out[pos++] = '\\';
            out[pos++] = ch;
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == '\t')
        {
            if (pos + 2 >= outCap)
                return false;
            out[pos++] = '\\';
            out[pos++] = (ch == '\n') ? 'n' : ((ch == '\r') ? 'r' : 't');
            continue;
        }
        if ((unsigned char)ch < 0x20)
            continue;
        if (pos + 1 >= outCap)
            return false;
        out[pos++] = ch;
    }
    out[pos] = 0;
    return true;
}

static bool vm_mock_admin_location_status_ok(const char *location)
{
    const char *status = NULL;

    if (location == NULL)
        return false;
    status = strstr(location, "status=");
    if (status == NULL)
        return false;
    status += 7;
    return status[0] == 'o' && status[1] == 'k' &&
           (status[2] == 0 || status[2] == '&');
}

static void vm_mock_admin_send_location(vm_mock_service_socket client,
                                        const char *location,
                                        const char *cookieHeader)
{
    char extraHeaders[1024];
    char escapedLocation[1600];
    char json[1800];
    const char *target = location && location[0] ? location : "/";

    if (g_vm_mock_admin_ajax_action)
    {
        if (!vm_mock_admin_json_escape(target, escapedLocation,
                                       sizeof(escapedLocation)))
            snprintf(escapedLocation, sizeof(escapedLocation), "/");
        snprintf(json, sizeof(json),
                 "{\"ok\":%s,\"location\":\"%s\"}\n",
                 vm_mock_admin_location_status_ok(target) ? "true" : "false",
                 escapedLocation);
        snprintf(extraHeaders, sizeof(extraHeaders), "%s",
                 cookieHeader ? cookieHeader : "");
        (void)vm_mock_admin_send_response(client, "200 OK",
                                          "application/json; charset=utf-8",
                                          extraHeaders[0] ? extraHeaders : NULL,
                                          json);
        return;
    }

    snprintf(extraHeaders, sizeof(extraHeaders), "%sLocation: %s\r\n",
             cookieHeader ? cookieHeader : "",
             target);
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

static void vm_mock_admin_fill_scene_aliases(vm_mock_admin_scene_file *files,
                                              u32 fileCount)
{
    char path[256];
    u8 data[16384];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;
    u32 pos = 0;

    if (files == NULL || fileCount == 0)
        return;
    if (!vm_net_mock_open_server_data_resource("sMap.dsh", ".dsh", NULL, path,
                                               sizeof(path)))
    {
        return;
    }
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columnCount < 3 || columnCount > 64 || rowCount > 512 ||
        16u + headerBytes > len)
    {
        return;
    }
    pos = 16u + headerBytes;
    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        char scene[64];
        char alias[32];

        memset(scene, 0, sizeof(scene));
        memset(alias, 0, sizeof(alias));
        if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
            break;
        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
                break;
            if (col == 1)
                (void)vm_net_mock_copy_dsh_string_field(scene, sizeof(scene),
                                                         value, valueLen);
            else if (col == 2)
                (void)vm_net_mock_copy_dsh_string_field(alias, sizeof(alias),
                                                         value, valueLen);
            rowPos += valueLen;
        }
        pos = rowEnd;
        if (scene[0] == 0 || alias[0] == 0)
            continue;
        for (u32 i = 0; i < fileCount; ++i)
        {
            if (files[i].alias[0] != 0)
                continue;
            if (vm_net_mock_scene_names_equal_loose(files[i].name, scene))
            {
                snprintf(files[i].alias, sizeof(files[i].alias), "%s", alias);
                break;
            }
        }
    }
}

static void vm_mock_admin_scene_label_utf8(const vm_mock_admin_scene_file *file,
                                           char *out, size_t outCap)
{
    char nameUtf8[192];
    char aliasUtf8[96];

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(aliasUtf8, 0, sizeof(aliasUtf8));
    if (file == NULL || file->name[0] == 0)
        return;
    vm_net_mock_gbk_label_to_utf8(file->name, nameUtf8, sizeof(nameUtf8));
    if (file->alias[0] != 0)
    {
        vm_net_mock_gbk_label_to_utf8(file->alias, aliasUtf8, sizeof(aliasUtf8));
        snprintf(out, outCap, "%s(%s)", nameUtf8, aliasUtf8);
    }
    else
    {
        snprintf(out, outCap, "%s", nameUtf8);
    }
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
    vm_mock_admin_fill_scene_aliases(files, count);
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

static void vm_mock_admin_fill_xse_task_names(vm_mock_admin_scene_file *files,
                                              u32 fileCount)
{
    if (files == NULL || fileCount == 0)
        return;
    if (!vm_net_mock_load_task_catalog())
        return;
    for (u32 i = 0; i < fileCount; ++i)
    {
        vm_net_mock_xse_summary summary;
        const vm_net_mock_task_definition *task = NULL;

        memset(&summary, 0, sizeof(summary));
        files[i].alias[0] = 0;
        if (!vm_net_mock_load_xse_summary(files[i].name, &summary) ||
            summary.taskRefCount == 0)
        {
            continue;
        }
        task = vm_net_mock_task_admin_find(summary.taskRefs[0].taskId);
        if (task == NULL || task->name[0] == 0)
            continue;
        snprintf(files[i].alias, sizeof(files[i].alias), "%s", task->name);
    }
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
    vm_mock_admin_fill_xse_task_names(files, count);
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

/* Shared option catalogs (once per page). Compact <select data-admin-catalog>
 * fields are filled by setupCatalogSelects() so NPC forms stay small. */
static void vm_mock_admin_render_actor_catalog(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *actorFiles,
    u32 actorCount)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page, "<select id=\"admin-actor-options\" hidden>");
    for (u32 i = 0; i < actorCount; ++i)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "\">");
        vm_mock_admin_text_append_html(page, actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_xse_catalog(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *xseFiles,
    u32 xseCount)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(page, "<select id=\"admin-xse-options\" hidden>");
    for (u32 i = 0; i < xseCount; ++i)
    {
        char nameUtf8[192];
        char labelUtf8[288];

        vm_net_mock_gbk_label_to_utf8(xseFiles[i].name, nameUtf8,
                                      sizeof(nameUtf8));
        vm_mock_admin_scene_label_utf8(&xseFiles[i], labelUtf8,
                                       sizeof(labelUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, nameUtf8);
        vm_mock_admin_text_appendf(page, "\">");
        vm_mock_admin_text_append_html(page, labelUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_scene_catalog(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *sceneFiles,
    u32 sceneCount)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page, "<select id=\"admin-scene-options\" hidden>");
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char sceneUtf8[192];
        char labelUtf8[288];

        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name, sceneUtf8,
                                      sizeof(sceneUtf8));
        vm_mock_admin_scene_label_utf8(&sceneFiles[i], labelUtf8,
                                       sizeof(labelUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, sceneUtf8);
        vm_mock_admin_text_appendf(page, "\">");
        vm_mock_admin_text_append_html(page, labelUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_task_datalist(vm_mock_admin_text *page)
{
    vm_net_mock_task_definition *tasks = NULL;
    u32 taskCount = 0;

    if (page == NULL)
        return;
    tasks = (vm_net_mock_task_definition *)calloc(
        VM_NET_MOCK_TASK_CATALOG_MAX, sizeof(*tasks));
    if (tasks == NULL)
    {
        vm_mock_admin_text_appendf(
            page, "<datalist id=\"admin-task-ids\"></datalist>");
        return;
    }
    taskCount = vm_net_mock_task_admin_list(tasks, VM_NET_MOCK_TASK_CATALOG_MAX);
    vm_mock_admin_text_appendf(page, "<datalist id=\"admin-task-ids\">");
    for (u32 i = 0; i < taskCount; ++i)
    {
        char nameUtf8[128];

        if (!tasks[i].enabled)
            continue;
        memset(nameUtf8, 0, sizeof(nameUtf8));
        vm_net_mock_gbk_label_to_utf8(tasks[i].name, nameUtf8,
                                      sizeof(nameUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"%u\">%u · ",
                                   tasks[i].taskId, tasks[i].taskId);
        vm_mock_admin_text_append_html(page, nameUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(page, "</datalist>");
    free(tasks);
}

static void vm_mock_admin_render_actor_select(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *actorFiles,
    u32 actorCount, const char *currentActor)
{
    bool hasCurrent = currentActor != NULL && currentActor[0] != 0;

    (void)actorFiles;
    (void)actorCount;
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<select name=\"actor_resource\" data-admin-catalog=\"admin-actor-options\" required>");
    if (hasCurrent)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, currentActor);
        vm_mock_admin_text_appendf(page, "\" selected>");
        vm_mock_admin_text_append_html(page, currentActor);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    else
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"\" selected disabled>请选择 Actor 资源</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
    if (actorCount == 0)
        vm_mock_admin_text_appendf(
            page, "<p class=\"size\">未找到 Actor 资源。</p>");
}

static void vm_mock_admin_render_xse_select(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *xseFiles,
    u32 xseCount, const char *currentScript)
{
    bool hasCurrent = currentScript != NULL && currentScript[0] != 0;
    char currentUtf8[192];

    (void)xseFiles;
    (void)xseCount;
    if (page == NULL)
        return;
    memset(currentUtf8, 0, sizeof(currentUtf8));
    if (hasCurrent)
        vm_net_mock_gbk_label_to_utf8(currentScript, currentUtf8,
                                      sizeof(currentUtf8));
    vm_mock_admin_text_appendf(
        page,
        "<select name=\"script_name\" data-admin-catalog=\"admin-xse-options\" "
        "data-admin-empty=\"无脚本\">");
    if (hasCurrent)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, currentUtf8);
        vm_mock_admin_text_appendf(page, "\" selected>");
        vm_mock_admin_text_append_html(page, currentUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    else
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"\" selected>无脚本</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
    if (xseCount == 0)
        vm_mock_admin_text_appendf(
            page, "<p class=\"size\">未找到 XSE 脚本。</p>");
}

static void vm_mock_admin_render_npc_service_checkboxes(vm_mock_admin_text *page,
                                                         u32 serviceMask)
{
    static const char *labels[] = {
        "普通／任务 NPC",
        "武器商人",
        "装备修理",
        "技能导师",
        "防具商人（含腰带）",
        "药品商人",
        "副本向导（传送／挑战分项）"
    };
    u32 effectiveMask = serviceMask & VM_NET_MOCK_NPC_SERVICE_MASK_ALL;

    if (page == NULL)
        return;
    /* Legacy rows may only have npc_kind; callers should already normalize.
     * Empty mask = task-only NPC (no service checkboxes). */
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"service-kinds\"><span class=\"muted\" style=\"display:block;margin-bottom:6px\">"
        "可多选；不选则为普通／任务 NPC。任务入口仍单独绑定。</span>");
    for (u32 kind = 1; kind <= VM_NET_MOCK_NPC_KIND_MAX; ++kind)
    {
        bool checked = (effectiveMask & (1u << kind)) != 0;
        vm_mock_admin_text_appendf(
            page,
            "<label class=\"service-kind\"><input type=\"checkbox\" name=\"service_kind_%u\" value=\"1\"%s> "
            "%u · %s</label>",
            kind, checked ? " checked" : "", kind, labels[kind]);
    }
    vm_mock_admin_text_appendf(page, "</div>");
}

static bool vm_mock_admin_scene_file_to_runtime_key(const char *sceneFile,
                                                    char *runtimeScene,
                                                    size_t runtimeSceneCap);

static void vm_mock_admin_render_npc_task_select_named(
    vm_mock_admin_text *page,
    const char *fieldName,
    u32 currentTaskId,
    const char *emptyLabel);

static void vm_mock_admin_render_instance_scene_select(
    vm_mock_admin_text *page,
    const vm_mock_admin_scene_file *sceneFiles,
    u32 sceneCount,
    const char *currentRuntimeScene)
{
    char currentUtf8[192];
    bool found = false;

    if (page == NULL)
        return;
    memset(currentUtf8, 0, sizeof(currentUtf8));
    for (u32 i = 0; currentRuntimeScene != NULL && currentRuntimeScene[0] != 0 &&
                    sceneFiles != NULL && i < sceneCount;
         ++i)
    {
        char runtimeScene[64];

        memset(runtimeScene, 0, sizeof(runtimeScene));
        if (!vm_mock_admin_scene_file_to_runtime_key(
                sceneFiles[i].name, runtimeScene, sizeof(runtimeScene)))
            continue;
        if (!vm_net_mock_scene_names_equal_loose(currentRuntimeScene,
                                                 runtimeScene))
            continue;
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name, currentUtf8,
                                      sizeof(currentUtf8));
        found = true;
        break;
    }
    vm_mock_admin_text_appendf(
        page,
        "<select name=\"instance_scene\" data-admin-catalog=\"admin-scene-options\" "
        "data-admin-empty=\"不传送（仅挑战）\">");
    if (found)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, currentUtf8);
        vm_mock_admin_text_appendf(page, "\" selected>");
        vm_mock_admin_text_append_html(page, currentUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    else
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"\" selected>不传送（仅挑战）</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_instance_fields(
    vm_mock_admin_text *page,
    const vm_mock_admin_scene_file *sceneFiles,
    u32 sceneCount,
    const vm_net_mock_scene_npcinfo_seed *seed)
{
    u32 x = seed ? seed->instanceX : 0;
    u32 y = seed ? seed->instanceY : 0;
    u32 enemyId = seed ? seed->challengeEnemyId : 0;
    u32 minimumLevel = seed && seed->instanceMinLevel != 0
                           ? seed->instanceMinLevel
                           : 1;
    u32 teleportRequireTaskId = seed ? seed->teleportRequireTaskId : 0;

    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"instance-fields\"%s><label class=\"field instance-scene\"><span>副本目标场景（传送）</span>",
        (seed != NULL &&
         vm_net_mock_npc_has_service(seed, VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE))
            ? ""
            : " hidden");
    vm_mock_admin_render_instance_scene_select(
        page, sceneFiles, sceneCount, seed ? seed->instanceScene : NULL);
    vm_mock_admin_text_appendf(
        page,
        "</label><label class=\"field\"><span>落点 X</span><input type=\"number\" name=\"instance_x\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<label class=\"field\"><span>落点 Y</span><input type=\"number\" name=\"instance_y\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<label class=\"field\"><span>挑战怪物 ID</span><input type=\"number\" name=\"challenge_enemy_id\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<label class=\"field\"><span>最低等级</span><input type=\"number\" name=\"instance_min_level\" min=\"1\" max=\"255\" value=\"%u\"></label>"
        "<label class=\"field instance-teleport-task\"><span>传送前置任务（完成才显示传送）</span>",
        x, y, enemyId, minimumLevel);
    vm_mock_admin_render_npc_task_select_named(
        page, "teleport_require_task_id", teleportRequireTaskId,
        "0 · 无前置（直接显示传送）");
    vm_mock_admin_text_appendf(
        page,
        "</label>"
        "<p class=\"instance-help\">对话中「传送」与「挑战」是两个独立选项：配置了目标场景才出现传送；配置了挑战怪物 ID 才出现挑战。传送前置任务 ID 为 0 时直接显示传送；非 0 时需该任务已完成（state=3）才显示。落点 X/Y 都填 0 会自动解析目标 SCE 的安全入口。挑战怪物 ID 也会在进入目标场景后用于挂机选怪。</p></div>");
}

static void vm_mock_admin_render_npc_task_select_named(
    vm_mock_admin_text *page,
    const char *fieldName,
    u32 currentTaskId,
    const char *emptyLabel)
{
    if (page == NULL || fieldName == NULL || fieldName[0] == 0)
        return;
    if (emptyLabel == NULL || emptyLabel[0] == 0)
        emptyLabel = "不绑定任务";
    /* Options live in the shared #admin-task-ids datalist once per page. */
    vm_mock_admin_text_appendf(
        page,
        "<input type=\"number\" name=\"%s\" list=\"admin-task-ids\" "
        "min=\"0\" max=\"4294967295\" value=\"%u\" "
        "placeholder=\"",
        fieldName, currentTaskId);
    vm_mock_admin_text_append_html(page, emptyLabel);
    vm_mock_admin_text_appendf(page, "\">");
}

static void vm_mock_admin_render_npc_task_select(vm_mock_admin_text *page,
                                                 u32 currentTaskId)
{
    vm_mock_admin_render_npc_task_select_named(
        page, "task_id", currentTaskId, "不绑定任务");
}

/**
 * 渲染动态 NPC 可接任务多绑定表单（最多 VM_NET_MOCK_NPC_BOUND_TASK_MAX 行）。
 * @param page 页面缓冲
 * @param seed 当前 NPC；NULL 表示新增空表单
 */
static void vm_mock_admin_render_npc_bound_tasks(
    vm_mock_admin_text *page,
    const vm_net_mock_scene_npcinfo_seed *seed)
{
    u32 boundCount = seed != NULL ? seed->boundTaskCount : 0;

    if (page == NULL)
        return;
    if (boundCount > VM_NET_MOCK_NPC_BOUND_TASK_MAX)
        boundCount = VM_NET_MOCK_NPC_BOUND_TASK_MAX;
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"bound-tasks\">"
        "<div class=\"bound-tasks-title\">可接取任务（最多 %u 个，可留空）</div>",
        (u32)VM_NET_MOCK_NPC_BOUND_TASK_MAX);
    for (u32 i = 0; i < VM_NET_MOCK_NPC_BOUND_TASK_MAX; ++i)
    {
        char fieldTask[40];
        char fieldRepeatable[48];
        char fieldDaily[56];
        u32 taskId = 0;
        bool repeatable = false;
        u32 dailyLimit = 0;

        if (i < boundCount)
        {
            taskId = seed->boundTasks[i].taskId;
            repeatable = seed->boundTasks[i].repeatable;
            dailyLimit = seed->boundTasks[i].dailyAcceptLimit;
        }
        snprintf(fieldTask, sizeof(fieldTask), "bound_task_id_%u", i);
        snprintf(fieldRepeatable, sizeof(fieldRepeatable),
                 "bound_task_repeatable_%u", i);
        snprintf(fieldDaily, sizeof(fieldDaily),
                 "bound_task_daily_accept_limit_%u", i);
        vm_mock_admin_text_appendf(
            page,
            "<div class=\"bound-task-row\">"
            "<label class=\"field\"><span>任务 %u</span>",
            i + 1u);
        vm_mock_admin_render_npc_task_select_named(
            page, fieldTask, taskId, "不绑定");
        vm_mock_admin_text_appendf(
            page,
            "</label>"
            "<label class=\"field\"><span>重复接取</span>"
            "<select name=\"%s\">"
            "<option value=\"0\"%s>完成后不可再接</option>"
            "<option value=\"1\"%s>完成后允许再接</option>"
            "</select></label>"
            "<label class=\"field\"><span>每天次数</span>"
            "<input type=\"number\" name=\"%s\" min=\"0\" max=\"9999\" value=\"%u\">"
            "</label></div>",
            fieldRepeatable,
            repeatable ? "" : " selected",
            repeatable ? " selected" : "",
            fieldDaily,
            dailyLimit);
    }
    vm_mock_admin_text_appendf(
        page,
        "<p class=\"bound-tasks-help\">"
        "同一 NPC 可绑多个不同任务；空行忽略。每天次数 0=不限制。"
        "对话选项与服务入口合计最多 7 个。"
        "</p></div>");
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
    char battleMapSce[64];
    u16 entryId;
    u16 targetEntryId;
    u16 spawnX;
    u16 spawnY;
    u16 landingX;
    u16 landingY;
    bool landingKnown;
    u32 edgeIndex;
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
    u16 fieldCount = 0;
    char battleOrPrompt[128];

    if (data == NULL || portal == NULL || off + 12 > len)
        return false;
    memset(portal, 0, sizeof(*portal));
    portal->kind = VM_MOCK_ADMIN_PORTAL_NAMED;
    portal->entryId = 0xffff;
    portal->targetEntryId = 0xffff;
    memset(battleOrPrompt, 0, sizeof(battleOrPrompt));
    kind = vm_net_mock_read_le16_at(data, pos);
    pos += 2;
    /*
     * Proven dream/peach F094 battle portal (debug-0b451a):
     *   04 00 <auxX> <auxY> 04 00 03 00 12 00 <b_*.sce> 01 00 15 … 16 … 17 …
     * Legacy stub: 01 00 04 00 0e 00 26 00 04 00 03 00 12 00 …
     */
    if (kind == 4 && off + 12u <= len &&
        vm_net_mock_read_le16_at(data, off + 6u) == 4 &&
        vm_net_mock_read_le16_at(data, off + 8u) == 3 &&
        vm_net_mock_read_le16_at(data, off + 10u) == 0x12)
    {
        tileX = vm_net_mock_read_le16_at(data, off + 2u);
        tileY = vm_net_mock_read_le16_at(data, off + 4u);
        fieldCount = vm_net_mock_read_le16_at(data, off + 6u);
        tileWidth = 4u;
        tileHeight = 4u;
        pos = off + 8u;
        if (fieldCount == 0 ||
            !vm_mock_admin_read_sce_string_field(data, len, &pos, 0x12,
                                                 battleOrPrompt,
                                                 sizeof(battleOrPrompt)))
        {
            return false;
        }
        if (vm_net_mock_str_ends_with(battleOrPrompt, ".sce"))
        {
            snprintf(portal->battleMapSce, sizeof(portal->battleMapSce), "%s",
                     battleOrPrompt);
        }
    }
    else if (kind == 1 && off + 14u <= len &&
             vm_net_mock_read_le16_at(data, off + 2u) == 4 &&
             vm_net_mock_read_le16_at(data, off + 8u) == 4 &&
             vm_net_mock_read_le16_at(data, off + 10u) == 3 &&
             vm_net_mock_read_le16_at(data, off + 12u) == 0x12)
    {
        tileX = vm_net_mock_read_le16_at(data, off + 4u);
        tileY = vm_net_mock_read_le16_at(data, off + 6u);
        tileWidth = 4u;
        tileHeight = 4u;
        pos = off + 10u;
        if (!vm_mock_admin_read_sce_string_field(data, len, &pos, 0x12,
                                                 battleOrPrompt,
                                                 sizeof(battleOrPrompt)))
        {
            return false;
        }
        if (vm_net_mock_str_ends_with(battleOrPrompt, ".sce"))
        {
            snprintf(portal->battleMapSce, sizeof(portal->battleMapSce), "%s",
                     battleOrPrompt);
        }
    }
    else if (kind == 4)
    {
        if (pos + 8 > len)
            return false;
        tileX = vm_net_mock_read_le16_at(data, pos);
        tileY = vm_net_mock_read_le16_at(data, pos + 2);
        tileWidth = vm_net_mock_read_le16_at(data, pos + 4);
        tileHeight = vm_net_mock_read_le16_at(data, pos + 6);
        pos += 8;
        if (tileWidth == 0 || tileHeight == 0)
            return false;
        if (pos + 3 <= len && vm_net_mock_read_le16_at(data, pos) == 0x12)
        {
            pos += 2;
            if (!vm_net_mock_read_sce_len_string(data, len, &pos, battleOrPrompt,
                                                 sizeof(battleOrPrompt)))
            {
                return false;
            }
            if (vm_net_mock_str_ends_with(battleOrPrompt, ".sce"))
            {
                snprintf(portal->battleMapSce, sizeof(portal->battleMapSce),
                         "%s", battleOrPrompt);
            }
        }
        else if (pos + 5 <= len && vm_net_mock_read_le16_at(data, pos) == 3 &&
                 vm_net_mock_read_le16_at(data, pos + 2) == 0x12)
        {
            if (!vm_mock_admin_read_sce_string_field(data, len, &pos, 0x12,
                                                     battleOrPrompt,
                                                     sizeof(battleOrPrompt)))
            {
                return false;
            }
            if (vm_net_mock_str_ends_with(battleOrPrompt, ".sce"))
            {
                snprintf(portal->battleMapSce, sizeof(portal->battleMapSce),
                         "%s", battleOrPrompt);
            }
        }
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
        if (tileWidth == 0 || tileHeight == 0)
            return false;
        if (pos + 5 <= len && vm_net_mock_read_le16_at(data, pos) == 3 &&
            vm_net_mock_read_le16_at(data, pos + 2) == 0x12)
        {
            if (!vm_mock_admin_read_sce_string_field(data, len, &pos, 0x12,
                                                     battleOrPrompt,
                                                     sizeof(battleOrPrompt)))
            {
                return false;
            }
            if (vm_net_mock_str_ends_with(battleOrPrompt, ".sce"))
            {
                snprintf(portal->battleMapSce, sizeof(portal->battleMapSce),
                         "%s", battleOrPrompt);
            }
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
            portal.spawnX = edge.spawnX;
            portal.spawnY = edge.spawnY;
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
                {
                    if (portal.kind == VM_MOCK_ADMIN_PORTAL_EDGE)
                    {
                        vm_net_mock_sce_edge_portal_located targetLocated[16];
                        u32 targetCount = 0;
                        u32 edgeOrdinal = 0;

                        for (u32 i = 0; i < count; ++i)
                        {
                            if (portals[i].kind == VM_MOCK_ADMIN_PORTAL_EDGE)
                                ++edgeOrdinal;
                        }
                        portal.edgeIndex = edgeOrdinal;
                        targetCount = vm_net_mock_sce_edge_portal_locate_all(
                            portal.targetScene, targetLocated,
                            sizeof(targetLocated) / sizeof(targetLocated[0]),
                            NULL);
                        for (u32 i = 0; i < targetCount; ++i)
                        {
                            if (targetLocated[i].portal.entryId ==
                                portal.entryId)
                            {
                                portal.landingX = targetLocated[i].portal.spawnX;
                                portal.landingY = targetLocated[i].portal.spawnY;
                                portal.landingKnown = true;
                                break;
                            }
                        }
                    }
                    portals[count++] = portal;
                }
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
            vm_net_mock_update_find_named_for_request(names[i]);
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

static void vm_mock_admin_clear_scene_preview_cache(void)
{
    free(g_vm_mock_admin_preview_cache_bmp);
    g_vm_mock_admin_preview_cache_bmp = NULL;
    g_vm_mock_admin_preview_cache_bmp_len = 0;
    g_vm_mock_admin_preview_cache_scene[0] = 0;
}

static bool vm_mock_admin_base64_encode(const u8 *src, u32 srcLen,
                                        char *out, size_t outCap,
                                        u32 *outLen)
{
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u32 i = 0;
    u32 o = 0;
    u32 need = ((srcLen + 2u) / 3u) * 4u;

    if (outLen)
        *outLen = 0;
    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (src == NULL && srcLen != 0)
        return false;
    if (need + 1u > outCap)
        return false;
    while (i + 3u <= srcLen)
    {
        u32 n = ((u32)src[i] << 16) | ((u32)src[i + 1] << 8) | (u32)src[i + 2];
        out[o++] = kAlphabet[(n >> 18) & 63u];
        out[o++] = kAlphabet[(n >> 12) & 63u];
        out[o++] = kAlphabet[(n >> 6) & 63u];
        out[o++] = kAlphabet[n & 63u];
        i += 3u;
    }
    if (i < srcLen)
    {
        u32 n = (u32)src[i] << 16;
        out[o++] = kAlphabet[(n >> 18) & 63u];
        if (i + 1u < srcLen)
        {
            n |= (u32)src[i + 1] << 8;
            out[o++] = kAlphabet[(n >> 12) & 63u];
            out[o++] = kAlphabet[(n >> 6) & 63u];
            out[o++] = '=';
        }
        else
        {
            out[o++] = kAlphabet[(n >> 12) & 63u];
            out[o++] = '=';
            out[o++] = '=';
        }
    }
    out[o] = 0;
    if (outLen)
        *outLen = o;
    return true;
}

static int vm_mock_admin_base64_decode_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A';
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9')
        return ch - '0' + 52;
    if (ch == '+')
        return 62;
    if (ch == '/')
        return 63;
    return -1;
}

static bool vm_mock_admin_base64_decode(const char *src, u8 *out, u32 outCap,
                                        u32 *outLen)
{
    u32 i = 0;
    u32 o = 0;
    size_t srcLen = 0;

    if (outLen)
        *outLen = 0;
    if (src == NULL || out == NULL)
        return false;
    srcLen = strlen(src);
    if ((srcLen & 3u) != 0)
        return false;
    while (i < srcLen)
    {
        int a = vm_mock_admin_base64_decode_value(src[i]);
        int b = vm_mock_admin_base64_decode_value(src[i + 1]);
        int c = src[i + 2] == '=' ? -2 : vm_mock_admin_base64_decode_value(src[i + 2]);
        int d = src[i + 3] == '=' ? -2 : vm_mock_admin_base64_decode_value(src[i + 3]);
        u32 n;

        if (a < 0 || b < 0 || c == -1 || d == -1)
            return false;
        if (c == -2 && d != -2)
            return false;
        n = ((u32)a << 18) | ((u32)b << 12) |
            ((u32)(c < 0 ? 0 : c) << 6) | (u32)(d < 0 ? 0 : d);
        if (o >= outCap)
            return false;
        out[o++] = (u8)((n >> 16) & 0xffu);
        if (c >= 0)
        {
            if (o >= outCap)
                return false;
            out[o++] = (u8)((n >> 8) & 0xffu);
        }
        if (d >= 0)
        {
            if (o >= outCap)
                return false;
            out[o++] = (u8)(n & 0xffu);
        }
        i += 4u;
    }
    if (outLen)
        *outLen = o;
    return true;
}

static bool vm_mock_admin_build_map_payload(
    const vm_mock_admin_scene_preview *preview, const u8 *cells, u32 cellBytes,
    u8 **payloadOut, u32 *payloadLenOut)
{
    u32 namesBytes = 0;
    u32 total = 0;
    u32 pos = 0;
    u8 *out = NULL;

    if (payloadOut)
        *payloadOut = NULL;
    if (payloadLenOut)
        *payloadLenOut = 0;
    if (preview == NULL || cells == NULL || cellBytes != preview->cellCount * 4u ||
        preview->imageCount == 0 ||
        preview->imageCount > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
    {
        return false;
    }
    for (u32 i = 0; i < preview->imageCount; ++i)
    {
        size_t nameLen = strlen(preview->imageNames[i]);
        if (nameLen == 0 || nameLen > 255u)
            return false;
        namesBytes += 1u + (u32)nameLen;
    }
    total = 4u + namesBytes + 16u + cellBytes;
    out = (u8 *)malloc(total);
    if (out == NULL)
        return false;
    vm_mock_admin_preview_write_le32(out, 0, preview->imageCount);
    pos = 4;
    for (u32 i = 0; i < preview->imageCount; ++i)
    {
        size_t nameLen = strlen(preview->imageNames[i]);
        out[pos++] = (u8)nameLen;
        memcpy(out + pos, preview->imageNames[i], nameLen);
        pos += (u32)nameLen;
    }
    vm_mock_admin_preview_write_le32(out, pos, preview->width);
    vm_mock_admin_preview_write_le32(out, pos + 4, preview->height);
    vm_mock_admin_preview_write_le32(out, pos + 8, preview->tileWidth);
    vm_mock_admin_preview_write_le32(out, pos + 12, preview->tileHeight);
    pos += 16;
    memcpy(out + pos, cells, cellBytes);
    *payloadOut = out;
    *payloadLenOut = total;
    return true;
}

static bool vm_mock_admin_build_tile_sheet_bmp(const char *imageName,
                                               u8 **bmpOut, u32 *bmpLenOut,
                                               u32 *widthOut, u32 *heightOut)
{
    u8 *imagePayload = NULL;
    u32 imagePayloadLen = 0;
    u8 imageType = 0;
    GifOutput image;
    int mallocSize = 0;
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
    memset(&image, 0, sizeof(image));
    if (imageName == NULL || imageName[0] == 0 ||
        !vm_net_mock_update_name_is_safe(imageName) ||
        !vm_net_mock_str_ends_with(imageName, ".gif") ||
        !vm_mock_admin_load_data_payload(imageName, ".gif", &imagePayload,
                                         &imagePayloadLen, &imageType) ||
        imageType != 1 ||
        gifDecodeExt(imagePayload, &image, 1, &mallocSize) == 0 ||
        image.pixels == NULL || image.width == 0 || image.height == 0 ||
        (u32)image.width * (u32)image.height >
            VM_MOCK_ADMIN_PREVIEW_PIXEL_MAX)
    {
        goto done;
    }
    canvas = (u16 *)malloc((size_t)image.width * (size_t)image.height *
                            sizeof(u16));
    if (canvas == NULL)
        goto done;
    {
        u32 sourcePitch = (u32)image.width +
                          ((4u - ((u32)image.width & 3u)) & 3u);
        for (u32 y = 0; y < (u32)image.height; ++y)
        {
            memcpy(canvas + y * (u32)image.width,
                   image.pixels + y * sourcePitch,
                   (size_t)image.width * sizeof(u16));
        }
    }
    rowBytes = ((u32)image.width * 3u + 3u) & ~3u;
    imageBytes = rowBytes * (u32)image.height;
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
    vm_mock_admin_preview_write_le32(bmp, 18, (u32)image.width);
    vm_mock_admin_preview_write_le32(bmp, 22, (u32)image.height);
    vm_mock_admin_preview_write_le16(bmp, 26, 1);
    vm_mock_admin_preview_write_le16(bmp, 28, 24);
    vm_mock_admin_preview_write_le32(bmp, 34, imageBytes);
    vm_mock_admin_preview_write_le32(bmp, 38, 2835);
    vm_mock_admin_preview_write_le32(bmp, 42, 2835);
    for (u32 y = 0; y < (u32)image.height; ++y)
    {
        u8 *dst = bmp + 54u + ((u32)image.height - 1u - y) * rowBytes;
        for (u32 x = 0; x < (u32)image.width; ++x)
        {
            u16 pixel = canvas[y * (u32)image.width + x];
            dst[x * 3] = (u8)((pixel & 0x1fu) * 255u / 31u);
            dst[x * 3 + 1] = (u8)(((pixel >> 5) & 0x3fu) * 255u / 63u);
            dst[x * 3 + 2] = (u8)(((pixel >> 11) & 0x1fu) * 255u / 31u);
        }
    }
    *bmpOut = bmp;
    *bmpLenOut = bmpLen;
    if (widthOut)
        *widthOut = (u32)image.width;
    if (heightOut)
        *heightOut = (u32)image.height;
    bmp = NULL;
    ok = true;

done:
    if (image.owned && image.pixels)
        free_mem(image.pixels);
    free(imagePayload);
    free(canvas);
    free(bmp);
    return ok;
}

static void vm_mock_admin_render_content_page(char *response,
                                              size_t responseCap,
                                              const char *query)
{
    /* These catalogs are ~200KB+ combined; keep them off the worker stack.
     * Default winpthread stacks are ~1MB and content+HTTP request locals
     * previously overflowed into process crashes on the content tab. */
    vm_mock_admin_scene_file *sceneFiles = NULL;
    vm_mock_admin_scene_file *actorFiles = NULL;
    vm_mock_admin_scene_file *xseFiles = NULL;
    vm_net_mock_dynamic_npc_admin_row *npcRows = NULL;
    vm_net_mock_scene_npcinfo_seed *previewNpcRows = NULL;
    vm_mock_admin_scene_portal *previewPortalRows = NULL;
    vm_net_mock_sce_combat_spawn_located *previewCombatRows = NULL;
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
    u32 previewCombatCount = 0;
    u32 previewCombatTotal = 0;
    u32 sceneMonsterIds[3] = {0, 0, 0};
    bool previewReady = false;
    bool sceneMonstersOverridden = false;
    bool sceneMonstersKnown = false;

    sceneFiles = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_SCENE_FILE_MAX, sizeof(*sceneFiles));
    actorFiles = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_ACTOR_FILE_MAX, sizeof(*actorFiles));
    xseFiles = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_XSE_FILE_MAX, sizeof(*xseFiles));
    npcRows = (vm_net_mock_dynamic_npc_admin_row *)calloc(
        VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX, sizeof(*npcRows));
    previewNpcRows = (vm_net_mock_scene_npcinfo_seed *)calloc(
        VM_NET_MOCK_SCENE_NPC_CATALOG_MAX, sizeof(*previewNpcRows));
    previewPortalRows = (vm_mock_admin_scene_portal *)calloc(
        VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX, sizeof(*previewPortalRows));
    previewCombatRows = (vm_net_mock_sce_combat_spawn_located *)calloc(
        VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX, sizeof(*previewCombatRows));
    if (sceneFiles == NULL || actorFiles == NULL || xseFiles == NULL ||
        npcRows == NULL || previewNpcRows == NULL || previewPortalRows == NULL ||
        previewCombatRows == NULL)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>游戏内容页面内存不足。</p>");
        free(sceneFiles);
        free(actorFiles);
        free(xseFiles);
        free(npcRows);
        free(previewNpcRows);
        free(previewPortalRows);
        free(previewCombatRows);
        return;
    }
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
        previewCombatCount = vm_net_mock_sce_combat_spawn_locate_all(
            runtimeScene, previewCombatRows, VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX,
            &previewCombatTotal);
        sceneMonstersKnown = vm_net_mock_scene_monster_admin_get(
            runtimeScene, sceneMonsterIds, &sceneMonstersOverridden);
        (void)sceneMonstersKnown;
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 游戏内容管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        "[hidden]{display:none!important}"
        ".wrap{max-width:1280px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 12px}.sub{color:#667085;margin:4px 0 16px}"
        ".tabs{display:flex;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}"
        ".logout{background:none;color:#667085;border:1px solid #d0d5dd}.grid{display:grid;grid-template-columns:300px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.grid>aside{display:flex;flex-direction:column;min-height:0;overflow:hidden}.grid>section{min-width:0;min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}"
        ".scene-list{display:flex;flex:1;min-height:0;flex-direction:column;gap:4px;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.scene-search-row{display:flex;gap:8px;margin:0 0 8px;align-items:stretch}.scene-search{flex:1;margin:0;width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}.scene-search-row button{flex:none}.scene{display:flex;justify-content:space-between;gap:8px;padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none;scroll-margin-block:12px}.scene.is-filtered,.scene[hidden]{display:none!important}.scene:hover,.scene.on{background:#eef4ff;color:#175cd3}.scene .scene-name{min-width:0;overflow:hidden;text-overflow:ellipsis}.size{color:#98a2b3;font-size:12px;white-space:nowrap}"
        ".create-scene{flex:none;margin:0 0 12px;padding:10px;border:1px solid #e4e7ec;border-radius:8px;background:#f9fafb}.create-scene .fields{grid-template-columns:1fr;gap:8px;margin-top:8px}.create-scene .actions{margin-top:8px}.monster-slots{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin:10px 0}.monster-slots a{color:#175cd3;text-decoration:none;font-size:12px}"
        ".portal-edit,.combat-edit{display:grid;gap:12px;margin-top:10px}.portal-card,.combat-card{border:1px solid #e4e7ec;border-radius:8px;padding:12px;background:#fff}.portal-card h3,.combat-card h3{margin:0 0 8px;font-size:14px}.portal-fields,.combat-fields{display:grid;grid-template-columns:repeat(4,minmax(90px,1fr));gap:8px}.portal-fields .wide,.combat-fields .wide{grid-column:span 2}.combat-fields .check{display:flex;align-items:flex-end;padding-bottom:8px}"
        ".preview{border:1px solid #d0d5dd;border-radius:9px;padding:12px;margin:0 0 16px;background:#f9fafb}.preview-head{display:flex;justify-content:flex-start;gap:12px;align-items:center;margin-bottom:10px;flex-wrap:wrap}.preview-head .size{margin-left:auto}.map-edit-link{padding:4px 10px;border-radius:6px;background:#175cd3;color:#fff;text-decoration:none;font-size:12px}.map-scroll{overflow:auto;max-height:760px;padding:8px;border-radius:7px;background:#1f2937}.map-stage{position:relative;margin:auto;box-shadow:0 0 0 1px #0008;background:#111;overflow:visible}.map-stage>img{display:block;width:100%%;height:100%%;image-rendering:pixelated}.portal-box{position:absolute;z-index:1;border:2px dashed #fdb022;background:#fec84b26;pointer-events:none}.portal-box.named{border-color:#22d3ee;background:#22d3ee24}.portal-label{position:absolute;left:-2px;bottom:100%%;max-width:220px;padding:1px 4px;border-radius:3px 3px 0 0;background:#7a2e0e;color:#fff;font-size:10px;line-height:15px;white-space:nowrap}.portal-box.named .portal-label{background:#0e7490}.npc-pin{position:absolute;transform:translate(-50%%,-100%%);display:flex;flex-direction:column;align-items:center;z-index:3;filter:drop-shadow(0 1px 1px #0008);pointer-events:none}.npc-pin.draggable,.npc-pin.copyable{pointer-events:auto;touch-action:none;-webkit-user-select:none;user-select:none}.npc-pin.draggable{cursor:grab}.npc-pin.copyable:not(.draggable){cursor:context-menu}.npc-pin.draggable:active,.npc-pin.dragging{cursor:grabbing;z-index:8}.npc-pin.dragging{opacity:.92}.scene-pin-modal{position:fixed;inset:0;z-index:1200;place-items:center;padding:20px;background:#10182899}.scene-pin-modal:not([hidden]){display:grid}.scene-pin-modal[hidden]{display:none!important}.scene-pin-dialog{width:min(920px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.scene-pin-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:14px 16px;border-bottom:1px solid #eaecf0}.scene-pin-head strong{font-size:16px}.scene-pin-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.scene-pin-body{overflow:auto;padding:14px 16px 18px;overscroll-behavior:contain}.scene-pin-body .npc,.scene-pin-body .combat-card{border:0;padding:0;background:transparent}.scene-pin-body .fields{grid-template-columns:repeat(2,minmax(0,1fr))}.scene-pin-body .combat-fields{grid-template-columns:repeat(2,minmax(0,1fr))}.scene-pin-body .instance-fields{grid-template-columns:1fr 1fr}.scene-pin-body .bound-task-row{grid-template-columns:1fr 1fr}.preview-edit-tools{display:flex;flex-wrap:wrap;gap:8px;margin:10px 0 0}.preview-edit-tools button{font-size:13px}.scene-edit-store{display:none!important}.modal-open{overflow:hidden}.pin-name{max-width:140px;padding:1px 4px;border-radius:3px;background:#175cd3;color:#fff;font-size:11px;line-height:16px;white-space:nowrap}.npc-pin.service .pin-name{background:#b54708}.npc-pin.combat .pin-name{background:#b42318}.sprite-wrap{position:relative;display:flex;align-items:flex-end;justify-content:center;min-width:18px;min-height:18px}.actor-sprite{display:block;width:auto;height:auto;max-width:72px;max-height:72px;image-rendering:pixelated}.facing-badge{position:absolute;right:-13px;bottom:0;min-width:17px;height:17px;padding:0 3px;border:1px solid #fff;border-radius:9px;background:#101828;color:#fff;font-size:11px;font-weight:700;line-height:15px;text-align:center}.preview-legend,.preview-npcs,.preview-portals{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:9px}.legend-icon{display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;border-radius:4px;background:#175cd3;color:#fff;font-size:11px}.legend-icon.service{background:#b54708}.legend-icon.combat{background:#b42318}.legend-portal{width:18px;height:12px;border:2px dashed #fdb022;background:#fec84b26}.legend-portal.named{border-color:#22d3ee;background:#22d3ee24}.npc-chip,.portal-chip{font-size:12px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#344054}.npc-chip.service{background:#fff4e8}.npc-chip.combat{background:#fef3f2;color:#b42318}.portal-chip{background:#fffaeb;color:#7a2e0e}.portal-chip.named{background:#ecfdff;color:#0e7490}.preview-error{padding:12px;border-radius:7px;background:#fef3f2;color:#b42318;margin-bottom:16px}"
        ".notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.npc-list{display:grid;gap:12px}.npc{border:1px solid #e4e7ec;border-radius:8px;padding:13px}.npc.off{opacity:.62;background:#f9fafb}.npc-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}.badge{font-size:12px;background:#eef4ff;color:#175cd3;padding:2px 7px;border-radius:999px}.badge.override{background:#fffaeb;color:#b54708}.fields{display:grid;grid-template-columns:110px 1.1fr 1fr 90px 90px 90px 90px;gap:8px}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}.service-kinds{display:flex;flex-wrap:wrap;gap:8px 14px;padding:8px 10px;border:1px solid #e4e7ec;border-radius:7px;background:#f9fafb}.service-kind{display:flex;align-items:center;gap:6px;font-size:13px;color:#344054;white-space:nowrap}.instance-fields{display:grid;grid-template-columns:minmax(220px,2fr) 90px 90px 120px 100px;gap:8px;align-items:end;margin-top:10px;padding:10px;border-radius:7px;background:#fffaeb;border:1px solid #fedf89}.instance-teleport-task,.instance-help{grid-column:1/-1}.instance-help{margin:0;color:#7a2e0e;font-size:12px}.bound-tasks{display:grid;gap:8px;margin-top:10px;padding:10px;border:1px solid #e4e7ec;border-radius:7px;background:#f9fafb}.bound-tasks-title{font-size:12px;color:#667085}.bound-task-row{display:grid;grid-template-columns:minmax(140px,1.4fr) minmax(150px,1fr) minmax(100px,.7fr);gap:8px;align-items:end}.bound-tasks-help{margin:0;font-size:12px;color:#667085}"
        "input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button,.button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap;text-decoration:none;display:inline-block;text-align:center}.danger{background:#b42318}.enable{background:#027a48}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:10px}.new{margin-top:16px}.foot{color:#667085;font-size:12px;margin:12px 0 0}"
        "@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:18px 10px;overflow:visible}.grid{grid-template-columns:1fr;flex:none}.grid>aside,.grid>section{overflow:visible}.scene-list{flex:none;max-height:260px;overflow:auto}.fields,.instance-fields,.monster-slots,.bound-task-row{grid-template-columns:1fr 1fr}.instance-scene,.instance-help{grid-column:1/-1}}"
        "</style><script src=\"/admin.js?v=17\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">场景资源与服务端动态 NPC</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_append_tabs(&page, "content");
    vm_mock_admin_text_appendf(
        &page,
        "<div class=\"grid\"><aside class=\"card\">"
        "<div class=\"create-scene\"><strong>＋ 新增场景</strong>"
        "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"create-scene\">"
        "<div class=\"fields\"><label class=\"field\"><span>新场景名</span>"
        "<input name=\"new_scene_name\" maxlength=\"59\" placeholder=\"例如 my_scene 或 my_scene.sce\" required></label>"
        "<label class=\"field\"><span>克隆自</span><select name=\"clone_from\" required>");
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char sceneUtf8[192];
        char labelUtf8[288];
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name,
                                      sceneUtf8, sizeof(sceneUtf8));
        vm_mock_admin_scene_label_utf8(&sceneFiles[i], labelUtf8,
                                       sizeof(labelUtf8));
        vm_mock_admin_text_appendf(&page, "<option value=\"");
        vm_mock_admin_text_append_html(&page, sceneUtf8);
        vm_mock_admin_text_appendf(
            &page, "\"%s>",
            strcmp(sceneFiles[i].name, selectedSceneFile) == 0 ? " selected" : "");
        vm_mock_admin_text_append_html(&page, labelUtf8);
        vm_mock_admin_text_appendf(&page, "</option>");
    }
    vm_mock_admin_text_appendf(
        &page,
        "</select></label></div><div class=\"actions\"><button type=\"submit\">克隆创建</button></div>"
        "<p class=\"foot\" style=\"margin:8px 0 0\">复制源 .sce，并复制其内嵌 .map 为新场景同名独立地图（不与源场景共用）。</p></form></div>"
        "<h2>SCE 场景（%u）</h2>"
        "<div class=\"scene-search-row\">"
        "<input class=\"scene-search\" id=\"scene-search\" type=\"search\" "
        "placeholder=\"按文件名或地图名筛选，如 铜雀台\" autocomplete=\"off\">"
        "<button type=\"button\" id=\"scene-search-btn\">筛选</button></div>"
        "<div class=\"scene-list\" id=\"scene-list\">",
        sceneCount);
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char sceneUtf8[192];
        char labelUtf8[288];
        char encoded[512];
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name,
                                      sceneUtf8, sizeof(sceneUtf8));
        vm_mock_admin_scene_label_utf8(&sceneFiles[i], labelUtf8,
                                       sizeof(labelUtf8));
        vm_mock_admin_url_encode(sceneUtf8, encoded, sizeof(encoded));
        if (strcmp(sceneFiles[i].name, selectedSceneFile) == 0)
        {
            vm_mock_admin_text_appendf(
                &page,
                "<a id=\"selected-scene\" class=\"scene on\" aria-current=\"page\" data-key=\"");
        }
        else
        {
            vm_mock_admin_text_appendf(&page, "<a class=\"scene\" data-key=\"");
        }
        vm_mock_admin_text_append_html(&page, labelUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\" href=\"/?tab=content&amp;scene=%s#selected-scene\">"
            "<span class=\"scene-name\">",
            encoded);
        vm_mock_admin_text_append_html(&page, labelUtf8);
        vm_mock_admin_text_appendf(
            &page, "</span><span class=\"size\">%llu B</span></a>",
            (unsigned long long)sceneFiles[i].size);
    }
    if (sceneCount == 0)
        vm_mock_admin_text_appendf(&page, "<span class=\"size\">未找到 SCE 文件</span>");
    vm_mock_admin_text_appendf(&page, "</div></aside><section><div class=\"card\"><h2>动态 NPC：");
    {
        char selectedLabelUtf8[288];
        const vm_mock_admin_scene_file *selectedFile = NULL;

        memset(selectedLabelUtf8, 0, sizeof(selectedLabelUtf8));
        for (u32 i = 0; i < sceneCount; ++i)
        {
            if (strcmp(sceneFiles[i].name, selectedSceneFile) == 0)
            {
                selectedFile = &sceneFiles[i];
                break;
            }
        }
        if (selectedFile != NULL)
            vm_mock_admin_scene_label_utf8(selectedFile, selectedLabelUtf8,
                                           sizeof(selectedLabelUtf8));
        else if (selectedSceneUtf8[0] != 0)
            snprintf(selectedLabelUtf8, sizeof(selectedLabelUtf8), "%s",
                     selectedSceneUtf8);
        vm_mock_admin_text_append_html(
            &page, selectedLabelUtf8[0] ? selectedLabelUtf8 : "未选择场景");
    }
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
    vm_mock_admin_render_actor_catalog(&page, actorFiles, actorCount);
    vm_mock_admin_render_xse_catalog(&page, xseFiles, xseCount);
    vm_mock_admin_render_scene_catalog(&page, sceneFiles, sceneCount);
    vm_mock_admin_render_task_datalist(&page);
    if (previewReady)
    {
        char encodedScene[512];
        char mapNameUtf8[192];

        vm_mock_admin_url_encode(selectedSceneUtf8, encodedScene,
                                 sizeof(encodedScene));
        vm_net_mock_gbk_label_to_utf8(preview.mapName, mapNameUtf8,
                                      sizeof(mapNameUtf8));
        vm_mock_admin_text_appendf(&page,
            "<div class=\"preview\"><div class=\"preview-head\"><strong>场景预览</strong>"
            "<a class=\"map-edit-link\" href=\"/?tab=mapedit&amp;scene=%s\">编辑地图</a>"
            "<span class=\"size\">",
            encodedScene);
        vm_mock_admin_text_append_html(&page, mapNameUtf8);
        vm_mock_admin_text_appendf(&page,
            " · %u×%u · NPC %u%s · 传送点 %u%s</span></div>"
            "<div class=\"map-scroll\"><div class=\"map-stage\" id=\"scene-preview-stage\" "
            "data-width=\"%u\" data-height=\"%u\" data-scene=\"",
            preview.width, preview.height, previewNpcCount,
            previewNpcTotal > previewNpcCount ? "+" : "",
            previewPortalCount,
            previewPortalTotal > previewPortalCount ? "+" : "",
            preview.width, preview.height);
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "\" style=\"width:%upx;height:%upx\">"
            "<img src=\"/scene-preview.bmp?scene=%s\" width=\"%u\" height=\"%u\" alt=\"场景完整地图\">",
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
                    " · 入口 -- → %u · 区域 (%u,%u)-(%u,%u)",
                    portal->targetEntryId, portal->left, portal->top,
                    portal->right, portal->bottom);
            }
            else
            {
                vm_mock_admin_text_appendf(
                    &page,
                    " · 入口 %u → %u · 区域 (%u,%u)-(%u,%u)",
                    portal->entryId, portal->targetEntryId,
                    portal->left, portal->top, portal->right, portal->bottom);
            }
            if (portal->battleMapSce[0] != 0)
            {
                char battleUtf8[192];
                vm_net_mock_gbk_label_to_utf8(portal->battleMapSce, battleUtf8,
                                              sizeof(battleUtf8));
                vm_mock_admin_text_appendf(&page, " · 战斗图 ");
                vm_mock_admin_text_append_html(&page, battleUtf8);
            }
            vm_mock_admin_text_appendf(&page, "\"><span class=\"portal-label\">");
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
            vm_mock_admin_text_appendf(
                &page,
                "<div class=\"npc-pin%s draggable copyable\" "
                "data-kind=\"npc\" data-actor-id=\"%u\" data-x=\"%u\" data-y=\"%u\" "
                "data-edit-panel=\"npc-panel-%u\" "
                "style=\"left:%upx;top:%upx\" title=\"",
                serviceNpc ? " service" : "",
                seed->actorId, seed->x, seed->y, seed->actorId,
                markerX, markerY);
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
        for (u32 i = 0; i < previewCombatCount; ++i)
        {
            const vm_net_mock_sce_combat_spawn *spawn = &previewCombatRows[i].spawn;
            char monsterNameUtf8[128];
            char actorEncoded[256];
            u32 markerX = spawn->x < preview.width ? spawn->x : preview.width - 1;
            u32 markerY = spawn->y < preview.height ? spawn->y : preview.height - 1;

            memset(monsterNameUtf8, 0, sizeof(monsterNameUtf8));
            vm_net_mock_gbk_label_to_utf8(
                spawn->displayName[0] ? spawn->displayName : "怪",
                monsterNameUtf8, sizeof(monsterNameUtf8));
            vm_mock_admin_url_encode(spawn->actorResource, actorEncoded,
                                     sizeof(actorEncoded));
            vm_mock_admin_text_appendf(
                &page,
                "<div class=\"npc-pin combat draggable copyable\" data-kind=\"combat\" "
                "data-spawn-index=\"%u\" data-x=\"%u\" data-y=\"%u\" "
                "data-edit-panel=\"combat-spawn-form\" "
                "style=\"left:%upx;top:%upx\" title=\"",
                i, spawn->x, spawn->y, markerX, markerY);
            vm_mock_admin_text_append_html(&page, monsterNameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                " · #%u · (%u,%u) · ",
                spawn->actorId, spawn->x, spawn->y);
            vm_mock_admin_text_append_html(&page, spawn->actorResource);
            vm_mock_admin_text_appendf(
                &page,
                "\"><span class=\"pin-name\">");
            vm_mock_admin_text_append_html(&page, monsterNameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</span><span class=\"sprite-wrap\"><img class=\"actor-sprite\" src=\"/actor-preview.svg?actor=%s\" alt=\"",
                actorEncoded);
            vm_mock_admin_text_append_html(&page, monsterNameUtf8);
            vm_mock_admin_text_appendf(&page, " 地图怪\"></span></div>");
        }
        vm_mock_admin_text_appendf(&page,
            "</div></div><div class=\"preview-legend\"><span class=\"legend-icon service\">人</span><span class=\"size\">服务端动态 NPC（拖动坐标 · 右键编辑 · 双击复制）</span>"
            "<span class=\"legend-icon\">人</span><span class=\"size\">SCE 内置 NPC（拖动坐标 · 右键编辑 · 双击复制；保存为覆盖）</span>"
            "<span class=\"legend-icon combat\">怪</span><span class=\"size\">地图刷怪点（拖动坐标 · 右键编辑 · 双击复制）</span>"
            "<span class=\"legend-portal\"></span><span class=\"size\">边界/元数据传送点</span>"
            "<span class=\"legend-portal named\"></span><span class=\"size\">具名传送点</span></div>"
            "<div class=\"preview-edit-tools\">"
            "<button type=\"button\" id=\"btn-open-new-npc\">＋ 新增动态 NPC</button>"
            "<button type=\"button\" id=\"btn-open-new-combat\">＋ 新增刷怪点</button>"
            "<span class=\"size\">预览图：左键拖动 · 右键编辑 · 双击复制</span></div>");
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
            if (serviceNpc)
            {
                vm_mock_admin_text_appendf(
                    &page,
                    "<span class=\"npc-chip service\" data-actor-id=\"%u\">",
                    seed->actorId);
            }
            else
            {
                vm_mock_admin_text_appendf(&page, "<span class=\"npc-chip\">");
            }
            vm_mock_admin_text_append_html(&page, npcNameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                " <span class=\"npc-xy\">(%u,%u)</span> · 朝向 %u %s · %s</span>",
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
            if (portal->battleMapSce[0] != 0)
            {
                char battleUtf8[192];
                vm_net_mock_gbk_label_to_utf8(portal->battleMapSce, battleUtf8,
                                              sizeof(battleUtf8));
                vm_mock_admin_text_appendf(&page, " · 战斗图 ");
                vm_mock_admin_text_append_html(&page, battleUtf8);
            }
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
        vm_mock_admin_text_appendf(
            &page,
            "<div id=\"scene-pin-modal\" class=\"scene-pin-modal\" hidden>"
            "<div class=\"scene-pin-dialog\" role=\"dialog\" aria-modal=\"true\">"
            "<div class=\"scene-pin-head\"><strong id=\"scene-pin-modal-title\">编辑</strong>"
            "<button type=\"button\" class=\"scene-pin-close\" id=\"scene-pin-modal-close\" "
            "aria-label=\"关闭\">×</button></div>"
            "<div class=\"scene-pin-body\" id=\"scene-pin-modal-body\"></div>"
            "</div></div>");
    }
    else
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"preview-error\">该 SCE 引用的地图资源无法解析，暂时不能生成预览。</div>");
    }
    if (runtimeScene[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"preview\" style=\"margin-bottom:16px\"><div class=\"preview-head\">"
            "<strong>挂机选怪三槽</strong><span class=\"badge%s\">%s</span></div>"
            "<p class=\"foot\" style=\"margin:0 0 8px\">覆盖 automonster.dsh 三槽；只影响挂机/开战选 ID，"
            "<strong>不会</strong>在地图上造怪。地图刷怪点请用下方「地图刷怪点」编辑。</p>"
            "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-scene-monsters\">"
            "<input type=\"hidden\" name=\"scene\" value=\"",
            sceneMonstersOverridden ? " override" : "",
            sceneMonstersOverridden ? "MySQL 覆盖" : "automonster 默认");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"><div class=\"monster-slots\">"
            "<label class=\"field\"><span>怪物槽 1</span>"
            "<input type=\"number\" name=\"monster_id_1\" min=\"0\" max=\"65535\" value=\"%u\">",
            sceneMonsterIds[0]);
        if (sceneMonsterIds[0] != 0)
            vm_mock_admin_text_appendf(
                &page, "<a href=\"/?tab=monsters&amp;monster=%u\">打开怪物 #%u</a>",
                sceneMonsterIds[0], sceneMonsterIds[0]);
        vm_mock_admin_text_appendf(
            &page,
            "</label><label class=\"field\"><span>怪物槽 2</span>"
            "<input type=\"number\" name=\"monster_id_2\" min=\"0\" max=\"65535\" value=\"%u\">",
            sceneMonsterIds[1]);
        if (sceneMonsterIds[1] != 0)
            vm_mock_admin_text_appendf(
                &page, "<a href=\"/?tab=monsters&amp;monster=%u\">打开怪物 #%u</a>",
                sceneMonsterIds[1], sceneMonsterIds[1]);
        vm_mock_admin_text_appendf(
            &page,
            "</label><label class=\"field\"><span>怪物槽 3</span>"
            "<input type=\"number\" name=\"monster_id_3\" min=\"0\" max=\"65535\" value=\"%u\">",
            sceneMonsterIds[2]);
        if (sceneMonsterIds[2] != 0)
            vm_mock_admin_text_appendf(
                &page, "<a href=\"/?tab=monsters&amp;monster=%u\">打开怪物 #%u</a>",
                sceneMonsterIds[2], sceneMonsterIds[2]);
        vm_mock_admin_text_appendf(
            &page,
            "</label></div><div class=\"actions\"><button type=\"submit\">保存刷怪配置</button></div></form>");
        if (sceneMonstersOverridden)
        {
            vm_mock_admin_text_appendf(
                &page,
                "<form method=\"post\" action=\"/action\" class=\"actions\">"
                "<input type=\"hidden\" name=\"action\" value=\"reset-scene-monsters\">"
                "<input type=\"hidden\" name=\"scene\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "\"><button class=\"danger\" type=\"submit\">恢复 automonster 默认</button></form>");
        }
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (runtimeScene[0] != 0)
    {
        char displayNameGbk[32];
        char displayNameUtf8[96];
        vm_net_mock_instance_enter_rule enterRule;

        memset(displayNameGbk, 0, sizeof(displayNameGbk));
        memset(displayNameUtf8, 0, sizeof(displayNameUtf8));
        memset(&enterRule, 0, sizeof(enterRule));
        (void)vm_net_mock_scene_display_name_load(runtimeScene, displayNameGbk,
                                                   sizeof(displayNameGbk));
        if (displayNameGbk[0] == 0)
        {
            for (u32 i = 0; i < sceneCount; ++i)
            {
                if (strcmp(sceneFiles[i].name, selectedSceneFile) == 0 &&
                    sceneFiles[i].alias[0] != 0)
                {
                    snprintf(displayNameGbk, sizeof(displayNameGbk), "%s",
                             sceneFiles[i].alias);
                    break;
                }
            }
        }
        vm_net_mock_gbk_label_to_utf8(displayNameGbk, displayNameUtf8,
                                      sizeof(displayNameUtf8));
        (void)vm_net_mock_instance_enter_rule_load(runtimeScene, &enterRule);
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"preview\" style=\"margin-bottom:16px\">"
            "<div class=\"preview-head\"><strong>地图显示名称</strong></div>"
            "<p class=\"foot\" style=\"margin:0 0 8px\">写入 MySQL "
            "<code>server_scene_display_names</code>，并同步改写本场景 SCE "
            "具名传送点名牌（字段 0x16，如原「梦境空间三层」）后发布资源。"
            "客户端需重新进图或拉到新版本后才会刷新。</p>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-scene-display-name\">"
            "<input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"><div class=\"fields\"><label class=\"field\"><span>显示名称</span>"
            "<input name=\"display_name\" maxlength=\"31\" value=\"");
        vm_mock_admin_text_append_html(&page, displayNameUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\" required></label></div>"
            "<div class=\"actions\"><button type=\"submit\">保存地图显示名称</button></div></form></div>");

        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"preview\" style=\"margin-bottom:16px\">"
            "<div class=\"preview-head\"><strong>每日进入／挑战规则</strong>"
            "<span class=\"badge\">按目标场景</span></div>"
            "<p class=\"foot\" style=\"margin:0 0 8px\">"
            "传送：次数/金币(金)/酷宝 在进入时扣除；挑战：向导「挑战守关怪」开战时扣次。"
            "次数=0 表示该项不限制。金币按 1金=10000铜 从角色 money 扣除。</p>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-instance-enter-rule\">"
            "<input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"><div class=\"fields\">"
            "<label class=\"field\"><span>每日可进次数</span>"
            "<input type=\"number\" name=\"daily_limit\" min=\"0\" max=\"9999\" value=\"%u\"></label>"
            "<label class=\"field\"><span>每日可挑战次数</span>"
            "<input type=\"number\" name=\"challenge_daily_limit\" min=\"0\" max=\"9999\" value=\"%u\"></label>"
            "<label class=\"field\"><span>每次消耗金币</span>"
            "<input type=\"number\" name=\"gold_cost\" min=\"0\" max=\"999999\" value=\"%u\"></label>"
            "<label class=\"field\"><span>每次消耗酷宝</span>"
            "<input type=\"number\" name=\"kubao_cost\" min=\"0\" max=\"999999\" value=\"%u\"></label>"
            "</div><div class=\"actions\"><button type=\"submit\">保存进入／挑战规则</button></div></form></div>",
            enterRule.found ? enterRule.dailyLimit : 0u,
            enterRule.found ? enterRule.challengeDailyLimit : 0u,
            enterRule.found ? enterRule.goldCost : 0u,
            enterRule.found ? enterRule.kubaoCost : 0u);
    }
    if (runtimeScene[0] != 0)
    {
        u32 combatRows = previewCombatCount;
        u32 formRows = combatRows + 1u;

        if (formRows > VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX)
            formRows = VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX;
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"scene-edit-store\">"
            "<form method=\"post\" action=\"/action\" id=\"combat-spawn-form\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-scene-combat-spawns\">"
            "<input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"><input type=\"hidden\" name=\"spawn_count\" value=\"%u\">"
            "<div class=\"combat-edit\">",
            formRows);
        for (u32 i = 0; i < formRows; ++i)
        {
            const vm_net_mock_sce_combat_spawn *spawn =
                i < combatRows ? &previewCombatRows[i].spawn : NULL;
            char nameUtf8[128];
            u32 x = spawn ? spawn->x : 0;
            u32 y = spawn ? spawn->y : 0;
            u32 actorId = spawn ? spawn->actorId : 0;

            memset(nameUtf8, 0, sizeof(nameUtf8));
            if (spawn != NULL && spawn->displayName[0] != 0)
                vm_net_mock_gbk_label_to_utf8(spawn->displayName, nameUtf8,
                                              sizeof(nameUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<div class=\"combat-card\" data-combat-index=\"%u\"%s><h3>%s #%u",
                i,
                i < combatRows ? "" : " data-combat-new=\"1\"",
                i < combatRows ? "刷怪点" : "新增刷怪点", i + 1u);
            if (nameUtf8[0] != 0)
            {
                vm_mock_admin_text_appendf(&page, " · ");
                vm_mock_admin_text_append_html(&page, nameUtf8);
            }
            if (actorId != 0)
                vm_mock_admin_text_appendf(&page, " · ID %u", actorId);
            vm_mock_admin_text_appendf(
                &page,
                "</h3><div class=\"combat-fields\">"
                "<label class=\"field\"><span>坐标 X</span>"
                "<input type=\"number\" name=\"spawn_x_%u\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
                "<label class=\"field\"><span>坐标 Y</span>"
                "<input type=\"number\" name=\"spawn_y_%u\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
                "<label class=\"field\"><span>怪物 ID</span>"
                "<input type=\"number\" name=\"monster_id_%u\" min=\"0\" max=\"65535\" value=\"%u\"></label>",
                i, x, i, y, i, actorId);
            if (actorId != 0)
                vm_mock_admin_text_appendf(
                    &page,
                    "<label class=\"field\"><span>目录</span>"
                    "<a href=\"/?tab=monsters&amp;monster=%u\">打开怪物 #%u</a></label>",
                    actorId, actorId);
            else
                vm_mock_admin_text_appendf(
                    &page,
                    "<label class=\"field\"><span>提示</span>"
                    "<span class=\"size\">X/Y/ID 都填才新增</span></label>");
            if (i < combatRows)
            {
                vm_mock_admin_text_appendf(
                    &page,
                    "<label class=\"field check\"><span>删除</span>"
                    "<input type=\"checkbox\" name=\"delete_%u\" value=\"1\"></label>",
                    i);
            }
            vm_mock_admin_text_appendf(&page, "</div></div>");
        }
        vm_mock_admin_text_appendf(
            &page,
            "</div><div class=\"actions\"><button type=\"submit\">保存地图刷怪点</button></div>"
            "<p class=\"foot\">写入 SCE 战斗点并发布；X/Y/怪物 ID 都填才生效。"
            "勾选删除后保存可移除该点。</p></form></div>");
    }
    if (runtimeScene[0] != 0)
    {
        u32 edgePortalCount = 0;

        for (u32 i = 0; i < previewPortalCount; ++i)
        {
            if (previewPortalRows[i].kind == VM_MOCK_ADMIN_PORTAL_EDGE)
                ++edgePortalCount;
        }
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"preview\" style=\"margin-bottom:16px\"><div class=\"preview-head\">"
            "<strong>传送点编辑</strong><span class=\"badge\">边缘传送 %u</span></div>"
            "<p class=\"foot\" style=\"margin:0 0 8px\">修改会重写 SCE 并发布资源；客户端需重新下载场景。"
            "落脚点写入目标场景中相同 entryId 的 spawn。"
            "梦境/FB 具名传送的 <code>type=4</code> 战斗图字段（0x12=<code>b_*.sce</code>）"
            "与 EOF 空记录由「地图刷怪点」保存时自动规范化，勿改回 type=1。</p>"
            "<div class=\"portal-edit\">",
            edgePortalCount);
        for (u32 i = 0; i < previewPortalCount; ++i)
        {
            const vm_mock_admin_scene_portal *portal = &previewPortalRows[i];
            char targetUtf8[192];
            char nameUtf8[192];
            char battleUtf8[192];

            if (portal->kind != VM_MOCK_ADMIN_PORTAL_NAMED)
                continue;
            memset(targetUtf8, 0, sizeof(targetUtf8));
            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(battleUtf8, 0, sizeof(battleUtf8));
            vm_net_mock_gbk_label_to_utf8(portal->targetScene, targetUtf8,
                                          sizeof(targetUtf8));
            vm_net_mock_gbk_label_to_utf8(portal->displayName, nameUtf8,
                                          sizeof(nameUtf8));
            if (portal->battleMapSce[0] != 0)
                vm_net_mock_gbk_label_to_utf8(portal->battleMapSce, battleUtf8,
                                              sizeof(battleUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<div class=\"portal-card\"><h3>具名/战斗传送（只读）");
            if (nameUtf8[0] != 0)
            {
                vm_mock_admin_text_appendf(&page, " · ");
                vm_mock_admin_text_append_html(&page, nameUtf8);
            }
            vm_mock_admin_text_appendf(&page, " → ");
            vm_mock_admin_text_append_html(&page, targetUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</h3><p class=\"foot\" style=\"margin:0\">exit %u · 区域 (%u,%u)-(%u,%u)",
                portal->targetEntryId, portal->left, portal->top, portal->right,
                portal->bottom);
            if (battleUtf8[0] != 0)
            {
                vm_mock_admin_text_appendf(&page, " · 战斗图 <code>");
                vm_mock_admin_text_append_html(&page, battleUtf8);
                vm_mock_admin_text_appendf(&page, "</code>");
            }
            else
            {
                vm_mock_admin_text_appendf(&page, " · 战斗图（无）");
            }
            vm_mock_admin_text_appendf(&page, "</p></div>");
        }
        for (u32 i = 0; i < previewPortalCount; ++i)
        {
            const vm_mock_admin_scene_portal *portal = &previewPortalRows[i];
            char targetUtf8[192];

            if (portal->kind != VM_MOCK_ADMIN_PORTAL_EDGE)
                continue;
            memset(targetUtf8, 0, sizeof(targetUtf8));
            vm_net_mock_gbk_label_to_utf8(portal->targetScene, targetUtf8,
                                          sizeof(targetUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<div class=\"portal-card\"><h3>边缘传送 #%u · entry %u → ",
                portal->edgeIndex + 1u, portal->entryId);
            vm_mock_admin_text_append_html(&page, targetUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</h3><form method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"save-scene-portal\">"
                "<input type=\"hidden\" name=\"portal_index\" value=\"%u\">"
                "<input type=\"hidden\" name=\"scene\" value=\"",
                portal->edgeIndex);
            vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "\"><div class=\"portal-fields\">"
                "<label class=\"field\"><span>触发 left</span>"
                "<input type=\"number\" name=\"left\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>top</span>"
                "<input type=\"number\" name=\"top\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>right</span>"
                "<input type=\"number\" name=\"right\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>bottom</span>"
                "<input type=\"number\" name=\"bottom\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>接近点 X</span>"
                "<input type=\"number\" name=\"spawn_x\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>接近点 Y</span>"
                "<input type=\"number\" name=\"spawn_y\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>入口 ID</span>"
                "<input type=\"number\" name=\"entry_id\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>exit ID</span>"
                "<input type=\"number\" name=\"target_entry_id\" min=\"0\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field wide\"><span>目标场景</span>"
                "<select name=\"target_scene\" data-admin-catalog=\"admin-scene-options\" required>",
                portal->left, portal->top, portal->right, portal->bottom,
                portal->spawnX, portal->spawnY, portal->entryId,
                portal->targetEntryId);
            vm_mock_admin_text_appendf(&page, "<option value=\"");
            {
                char targetOptUtf8[192];
                memset(targetOptUtf8, 0, sizeof(targetOptUtf8));
                vm_net_mock_gbk_label_to_utf8(portal->targetScene,
                                              targetOptUtf8,
                                              sizeof(targetOptUtf8));
                vm_mock_admin_text_append_html(&page, targetOptUtf8);
                vm_mock_admin_text_appendf(&page, "\" selected>");
                vm_mock_admin_text_append_html(&page, targetOptUtf8);
            }
            vm_mock_admin_text_appendf(
                &page,
                "</option></select></label>"
                "<label class=\"field\"><span>落脚点 X</span>"
                "<input type=\"number\" name=\"landing_x\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
                "<label class=\"field\"><span>落脚点 Y</span>"
                "<input type=\"number\" name=\"landing_y\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
                "</div><p class=\"foot\">%s</p>"
                "<div class=\"actions\"><button type=\"submit\">保存传送点</button></div></form>"
                "<form method=\"post\" action=\"/action\" class=\"actions\">"
                "<input type=\"hidden\" name=\"action\" value=\"delete-scene-portal\">"
                "<input type=\"hidden\" name=\"portal_index\" value=\"%u\">"
                "<input type=\"hidden\" name=\"scene\" value=\"",
                portal->landingKnown ? portal->landingX : portal->spawnX,
                portal->landingKnown ? portal->landingY : portal->spawnY,
                portal->landingKnown ?
                    "落脚点来自目标场景对应 entry 的 spawn；保存时会同步写回目标 SCE。" :
                    "目标场景暂无对应 entry；保存时若填写落脚点将要求目标已有该入口。",
                portal->edgeIndex);
            vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "\"><button class=\"danger\" type=\"submit\">移除传送点</button></form></div>");
        }
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"portal-card\"><h3>新增边缘传送点</h3>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"add-scene-portal\">"
            "<input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"><div class=\"portal-fields\">"
            "<label class=\"field\"><span>触发 left</span>"
            "<input type=\"number\" name=\"left\" min=\"0\" max=\"65535\" value=\"100\" required></label>"
            "<label class=\"field\"><span>top</span>"
            "<input type=\"number\" name=\"top\" min=\"0\" max=\"65535\" value=\"100\" required></label>"
            "<label class=\"field\"><span>right</span>"
            "<input type=\"number\" name=\"right\" min=\"0\" max=\"65535\" value=\"132\" required></label>"
            "<label class=\"field\"><span>bottom</span>"
            "<input type=\"number\" name=\"bottom\" min=\"0\" max=\"65535\" value=\"132\" required></label>"
            "<label class=\"field\"><span>接近点 X</span>"
            "<input type=\"number\" name=\"spawn_x\" min=\"0\" max=\"65535\" value=\"116\" required></label>"
            "<label class=\"field\"><span>接近点 Y</span>"
            "<input type=\"number\" name=\"spawn_y\" min=\"0\" max=\"65535\" value=\"116\" required></label>"
            "<label class=\"field\"><span>入口 ID</span>"
            "<input type=\"number\" name=\"entry_id\" min=\"0\" max=\"65535\" value=\"1\" required></label>"
            "<label class=\"field\"><span>exit ID</span>"
            "<input type=\"number\" name=\"target_entry_id\" min=\"0\" max=\"65535\" value=\"0\" required></label>"
            "<label class=\"field wide\"><span>目标场景</span>"
            "<select name=\"target_scene\" data-admin-catalog=\"admin-scene-options\" required>");
        vm_mock_admin_text_appendf(&page, "<option value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page, "\" selected>");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page, "</option>");
        vm_mock_admin_text_appendf(
            &page,
            "</select></label></div>"
            "<div class=\"actions\"><button type=\"submit\">新增传送点</button></div></form></div>"
            "</div></div>");
    }
    vm_mock_admin_text_appendf(&page, "<div class=\"scene-edit-store\" id=\"scene-npc-store\">");
    vm_mock_admin_text_appendf(&page, "<div class=\"npc-list\">");
    for (u32 i = 0; i < npcCount; ++i)
    {
        const vm_net_mock_dynamic_npc_admin_row *row = &npcRows[i];
        char displayUtf8[128];
        vm_net_mock_gbk_label_to_utf8(row->seed.displayName,
                                      displayUtf8, sizeof(displayUtf8));
        vm_mock_admin_text_appendf(&page,
            "<div class=\"npc%s\" id=\"npc-panel-%u\"><div class=\"npc-head\"><strong>Actor %u</strong><span>",
            row->enabled ? "" : " off", row->seed.actorId, row->seed.actorId);
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
            "<label class=\"field\" style=\"grid-column:1/-1\"><span>服务选项（可多选）</span>",
            row->seed.x, row->seed.y);
        vm_mock_admin_render_npc_service_checkboxes(
            &page, vm_net_mock_npc_effective_service_mask(&row->seed));
        vm_mock_admin_text_appendf(&page,
            "</label><label class=\"field\"><span>朝向</span><input type=\"number\" name=\"orientation\" min=\"0\" max=\"65535\" value=\"%u\"></label></div>"
            "<label class=\"field\" style=\"margin-top:8px\"><span>XSE 脚本（可留空）</span>",
            row->seed.orientation);
        vm_mock_admin_render_xse_select(&page, xseFiles, xseCount,
                                        row->seed.scriptName);
        vm_mock_admin_text_appendf(&page, "</label>");
        vm_mock_admin_render_npc_bound_tasks(&page, &row->seed);
        vm_mock_admin_render_instance_fields(&page, sceneFiles, sceneCount,
                                             &row->seed);
        vm_mock_admin_text_appendf(&page,
            "<div class=\"actions\"><button type=\"submit\">保存修改</button></div></form>"
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
        vm_mock_admin_text_appendf(&page, "<p class=\"size\">该场景没有可编辑 NPC。</p>");
    vm_mock_admin_text_appendf(&page,
        "</div><div class=\"npc new\" id=\"npc-panel-new\"><div class=\"npc-head\"><strong>增加动态 NPC</strong><span class=\"badge\">下次进入场景生效</span></div>"
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
        "<label class=\"field\" style=\"grid-column:1/-1\"><span>服务选项（可多选）</span>");
    vm_mock_admin_render_npc_service_checkboxes(&page, 0);
    vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\"><span>朝向</span><input type=\"number\" name=\"orientation\" min=\"0\" max=\"65535\" value=\"0\"></label></div>"
        "<label class=\"field\" style=\"margin-top:8px\"><span>XSE 脚本（可留空）</span>");
    vm_mock_admin_render_xse_select(&page, xseFiles, xseCount, NULL);
    vm_mock_admin_text_appendf(&page, "</label>");
    vm_mock_admin_render_npc_bound_tasks(&page, NULL);
    vm_mock_admin_render_instance_fields(&page, sceneFiles, sceneCount, NULL);
    vm_mock_admin_text_appendf(&page,
        "<div class=\"actions\"><button type=\"submit\">增加 NPC</button></div></form></div>"
        "<p class=\"foot\">服务选项可多选：对话中每个勾选的服务各增加一个入口。武器商人先按剑、匕首、法杖分类；防具商人提供头盔、衣甲、披风、腰带、护腿、鞋靴和戒指；药品商人出售后台「商品管理」中已「上架NPC」的非装备商品。副本向导在对话中拆成「传送」与「挑战」两项：配置目标场景才显示传送（可设前置任务，ID=0 直接显示）；配置挑战怪物 ID 才显示挑战；并按最低等级拦截。商品价格和上架状态均来自后台商品目录。装备修理按实际耐久收费；技能导师只列出当前职业、等级可学且尚未学习的技能。可接取任务最多 8 个绑定，每行可单独设重复接取与每天次数（0=不限）。任务与服务合计最多 7 个对话选项。SCE 内置 NPC 可编辑：保存写入 MySQL 覆盖，不改写 .sce；「恢复内置默认」可撤销覆盖。客户端同场景最多安全显示 4 个动态名称，超出时仍按任务优先级筛选。</p>"
        "</div></div></section></div></main></body></html>");

    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><title>响应过大</title><p>游戏内容页面超过大小限制。</p>");
    }
    free(sceneFiles);
    free(actorFiles);
    free(xseFiles);
    free(npcRows);
    free(previewNpcRows);
    free(previewPortalRows);
    free(previewCombatRows);
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

static int vm_mock_admin_account_list_compare(const void *leftValue,
                                              const void *rightValue)
{
    const char *left = (const char *)leftValue;
    const char *right = (const char *)rightValue;
    bool leftOnline = vm_mock_admin_account_is_online(left);
    bool rightOnline = vm_mock_admin_account_is_online(right);

    if (leftOnline != rightOnline)
        return leftOnline ? -1 : 1;
    return strcmp(left != NULL ? left : "", right != NULL ? right : "");
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

static void vm_mock_admin_render_item_picker_field(
    vm_mock_admin_text *page, const char *pickerId, const char *fieldName,
    const char *label, u32 itemId, bool required)
{
    const vm_net_mock_shop_catalog_item *item =
        itemId != 0 ? vm_net_mock_find_shop_catalog_item(itemId) : NULL;
    char itemNameUtf8[128];

    if (page == NULL || pickerId == NULL || pickerId[0] == 0 ||
        fieldName == NULL || fieldName[0] == 0 || label == NULL)
    {
        return;
    }
    memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
    if (item != NULL)
        vm_net_mock_gbk_label_to_utf8(item->name, itemNameUtf8,
                                      sizeof(itemNameUtf8));
    vm_mock_admin_text_appendf(
        page, "<div class=\"item-field\"><span>");
    vm_mock_admin_text_append_html(page, label);
    vm_mock_admin_text_appendf(
        page,
        "</span><input id=\"%s\" type=\"hidden\" name=\"%s\" value=\"%u\" data-item-picker-input%s>"
        "<button class=\"item-picker-trigger\" type=\"button\" data-item-picker-open=\"%s\" aria-haspopup=\"dialog\" aria-controls=\"item-picker-modal\">"
        "<span data-item-picker-label=\"%s\">",
        pickerId, fieldName, itemId,
        required ? " data-item-picker-required" : "", pickerId, pickerId);
    if (itemNameUtf8[0] != 0)
    {
        vm_mock_admin_text_appendf(page, "[%u] ", itemId);
        vm_mock_admin_text_append_html(page, itemNameUtf8);
    }
    else if (itemId != 0)
    {
        vm_mock_admin_text_appendf(page, "未知物品 #%u", itemId);
    }
    else
    {
        vm_mock_admin_text_appendf(page, "未选择物品");
    }
    vm_mock_admin_text_appendf(
        page, "</span><small>分类搜索</small></button></div>");
}

/* Requirement IDs are polymorphic (item or monster).  Keep their numeric
 * field editable for monster requirements, but attach the same item chooser
 * when the administrator is configuring a collect-item requirement. */
static void vm_mock_admin_render_item_picker_button(
    vm_mock_admin_text *page, const char *targetId, const char *label)
{
    if (page == NULL || targetId == NULL || targetId[0] == 0 || label == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<button class=\"item-picker-trigger compact\" type=\"button\" data-item-picker-open=\"%s\" aria-haspopup=\"dialog\" aria-controls=\"item-picker-modal\">"
        "<span>", targetId);
    vm_mock_admin_text_append_html(page, label);
    vm_mock_admin_text_appendf(
        page,
        "</span><small data-item-picker-label=\"%s\">从物品目录选择</small></button>",
        targetId);
}

static void vm_mock_admin_render_item_picker_modal(vm_mock_admin_text *page)
{
    bool equipmentCategories[256];
    bool itemCategories[256];
    u32 itemCount = vm_net_mock_load_shop_catalog();

    if (page == NULL)
        return;
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
        "<select id=\"item-picker-options\" hidden>");
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
        char itemNameUtf8[128];

        memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
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
        "</select><div class=\"item-modal\" id=\"item-picker-modal\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"item-picker-title\" hidden>"
        "<section class=\"item-picker-panel\"><div class=\"item-picker-head\"><div><h3 id=\"item-picker-title\">选择物品</h3>"
        "<p>按分类、名称或物品 ID 快速查找</p></div><div class=\"item-picker-head-actions\"><button id=\"item-picker-clear\" type=\"button\">清空选择</button><button class=\"item-picker-close\" id=\"item-picker-close\" type=\"button\" aria-label=\"关闭物品选择\">×</button></div></div>"
        "<div class=\"item-picker-tools\"><label><span>物品分类</span><select id=\"item-category\"><option value=\"all\">全部分类</option>");
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
        "</select></label><label><span>搜索</span><input id=\"item-search\" type=\"search\" placeholder=\"输入名称或物品 ID\" autocomplete=\"off\"></label></div>"
        "<div class=\"item-result-bar\"><span id=\"item-result-count\"></span><span class=\"item-picker-error\" id=\"item-picker-error\"></span></div>"
        "<div class=\"item-picker-list\" id=\"item-picker-list\"></div><p class=\"item-picker-empty\" id=\"item-picker-empty\" hidden>没有符合条件的物品</p>"
        "</section></div>");
}

static void vm_mock_admin_render_cosmetic_grant_form(
    vm_mock_admin_text *page, const char *account,
    const u32 *roleIds, char roleNames[][128], u32 roleCount)
{
    static const struct
    {
        u32 id;
        const char *name;
    } chenghaoRows[] = {
        {86001, "开天辟地"}, {86002, "千古一帝"}, {86003, "神挡杀神"},
        {86004, "捉鬼大师"}, {86005, "仙人之上一换一"}, {86006, "仙人之上一换十"},
        {86007, "仙人之上一换百"}, {86008, "仙人之上我最浪"},
        {86009, "九千九百九十九个界"}, {86010, "战无不胜"}, {86011, "天下第一"},
        {86012, "绝世无双"}, {86013, "技冠群雄"}, {86014, "大哥带你飞"},
        {86015, "单挑小王子"}, {86016, "痞帅街溜子"}, {86017, "专治各种不服"},
        {86018, "泡8喝9玩10尚"}, {86019, "实力不允许低调"}, {86020, "我养你啊"},
        {86021, "是真男人来砍我"}, {86022, "一天三顿小烧烤"}, {86024, "夜闯寡妇村"},
        {86025, "我自横刀向天笑"}, {86026, "独孤求败"}, {86027, "万事皆与钱有缘"},
        {86028, "一刀倒一片"}, {86029, "小楼昨夜又听雨"}, {86031, "唯我独尊"},
        {86032, "凤翔九天"}, {86033, "飞龙在天"}, {86034, "与天同寿"},
        {86036, "烽火戏诸侯"}, {86037, "怒斩百万雄"}, {86038, "无敌最寂寞"},
        {86039, "无敌幸运星"}, {86040, "唯我独尊2"}, {86041, "雄才大略"},
        {86042, "舍我其谁"}, {86043, "天下无双"}, {86044, "纵横四海"},
        {86045, "义薄云天"}, {86046, "威震天下"}, {86047, "拔地倚天"},
        {86048, "十步杀一人"}
    };
    static const struct
    {
        u32 id;
        const char *name;
    } wingRows[] = {
        {88010, "恶魔之翼"}, {88011, "鎏金紫翼"}, {88012, "萤火紫翼"},
        {88014, "黑白羽翼"}, {88015, "桃粉双翼"}, {88016, "暗黑紫翼"},
        {88017, "火龙双翼"}, {88018, "白银流光"}, {88019, "血色双翼"},
        {88020, "紫晶双翼"}, {88021, "烈焰双翼"}, {88022, "青色流光"},
        {88023, "幽蓝羽翼"}, {88024, "红白双翼"}, {88025, "赤电双翼"},
        {88026, "翠绿羽翼"}, {88027, "火黑双翼"}, {88028, "粉青蝶翼"},
        {88029, "橙色羽翼"}, {88030, "暗紫双翼"}, {88031, "深黑羽翼"},
        {88032, "暗金双翼"}, {88033, "白青双翼"}, {88034, "白金双翼"},
        {88035, "三色羽翼"}, {88036, "暗青羽翼"}, {90002, "黑蓝羽翼"},
        {90003, "紫金蝶翼"}, {90006, "双色波浪"}, {90007, "粉紫羽翼"},
        {90010, "流光冰雪"}
    };
    static const struct
    {
        u32 id;
        const char *name;
    } mountRows[] = {
        {11370, "藏青龙"}, {11372, "黑魔龙"}, {11802, "金圣龙"},
        {16171, "鳄青龙"}, {20148, "黄金龙"}, {40183, "蓝云龙"},
        {75535, "云青龙"}, {75537, "汗血马"}, {75549, "腾云龙"}
    };

    if (page == NULL || account == NULL || account[0] == 0 ||
        roleIds == NULL || roleNames == NULL || roleCount == 0)
    {
        return;
    }
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"item-grant\"><h2>解锁外观（称号/翅膀/坐骑）</h2>"
        "<form class=\"grant-form cosmetic-grant\" method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"grant-cosmetic\">"
        "<input type=\"hidden\" name=\"account\" value=\"");
    vm_mock_admin_text_append_html(page, account);
    vm_mock_admin_text_appendf(page,
        "\"><label><span>角色</span><select name=\"role\" required>");
    for (u32 i = 0; i < roleCount; ++i)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"%u\">", roleIds[i]);
        vm_mock_admin_text_append_html(page, roleNames[i]);
        vm_mock_admin_text_appendf(page, "（ID %u）</option>", roleIds[i]);
    }
    vm_mock_admin_text_appendf(
        page,
        "</select></label>"
        "<label><span>类型</span><select name=\"kind\" id=\"cosmetic-kind\" required>"
        "<option value=\"chenghao\">特效称号</option>"
        "<option value=\"wing\">翅膀</option>"
        "<option value=\"mount\">坐骑</option>"
        "</select></label>"
        "<label><span>装扮</span><select name=\"id\" id=\"cosmetic-id\">"
        "<option value=\"0\">请选择装扮</option>");
    for (u32 i = 0; i < sizeof(chenghaoRows) / sizeof(chenghaoRows[0]); ++i)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\" data-kind=\"chenghao\">%u · ",
            chenghaoRows[i].id, chenghaoRows[i].id);
        vm_mock_admin_text_append_html(page, chenghaoRows[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    for (u32 i = 0; i < sizeof(wingRows) / sizeof(wingRows[0]); ++i)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\" data-kind=\"wing\">%u · ",
            wingRows[i].id, wingRows[i].id);
        vm_mock_admin_text_append_html(page, wingRows[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    for (u32 i = 0; i < sizeof(mountRows) / sizeof(mountRows[0]); ++i)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\" data-kind=\"mount\">%u · ",
            mountRows[i].id, mountRows[i].id);
        vm_mock_admin_text_append_html(page, mountRows[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(
        page,
        "</select></label>"
        "<label><span><input type=\"checkbox\" name=\"all\" id=\"cosmetic-all\" value=\"1\"> "
        "解锁该类型全部</span></label>"
        "<button type=\"submit\">解锁外观</button></form>"
        "<p class=\"muted grant-note\">从列表选择装扮；勾选「全部」时忽略所选 ID。"
        "走 Pixi WT 1/31，不改 special_designation_mask。</p></div>");
}

static const char *vm_mock_admin_role_wash_attr_name(u8 type, u8 flag)
{
    switch (type)
    {
    case 1:
        return "力量";
    case 2:
        return "敏捷";
    case 3:
        return "智慧";
    case 4:
        return "物攻";
    case 5:
        return "护甲";
    case 6:
        return "躲闪";
    case 7:
        return "命中";
    case 8:
        return "暴击";
    case 9:
        return flag ? "气血%" : "气血";
    case 10:
        return flag ? "法力%" : "法力";
    default:
        return "未知";
    }
}

static const char *vm_mock_admin_role_equip_slot_name(u8 slot)
{
    static const char *names[] = {
        "武器", "帽子", "上衣", "披风", "束腰", "裤子", "鞋子", "饰品"
    };
    if (slot >= 8)
        return "?";
    return names[slot];
}

static void vm_mock_admin_append_item_name_html(vm_mock_admin_text *page,
                                                u32 itemId)
{
    const vm_net_mock_shop_catalog_item *item =
        itemId != 0 ? vm_net_mock_find_shop_catalog_item(itemId) : NULL;
    char nameUtf8[128];

    memset(nameUtf8, 0, sizeof(nameUtf8));
    if (item != NULL && item->name[0] != 0)
        vm_net_mock_gbk_label_to_utf8(item->name, nameUtf8, sizeof(nameUtf8));
    if (nameUtf8[0] != 0)
        vm_mock_admin_text_append_html(page, nameUtf8);
    else
        vm_mock_admin_text_appendf(page, "物品#%u", itemId);
}

static void vm_mock_admin_render_equip_wash_tier_form(
    vm_mock_admin_text *page, const char *account, u32 roleId,
    const char *where, u32 loc, u8 unlock,
    const vm_net_mock_enhance_wash_slot *slot)
{
    u8 type = 0;
    u8 flag = 0;
    u16 value = 0;
    u16 washCount = 0;
    u8 washed = 0;
    u8 t;

    if (page == NULL || account == NULL || where == NULL || slot == NULL)
        return;
    washed = slot->washed ? 1u : 0u;
    type = slot->type;
    flag = slot->flag;
    value = slot->value;
    washCount = slot->washCount;
    if (type < 1u || type > 10u)
        type = 1u;

    vm_mock_admin_text_appendf(
        page,
        "<tr><td>+%u</td><td>%s</td><td>",
        (u32)unlock,
        washed ? "已洗练" : "未洗练");
    if (washed)
        vm_mock_admin_text_appendf(
            page, "%s %u · 次%u",
            vm_mock_admin_role_wash_attr_name(slot->type, slot->flag),
            (u32)slot->value, (u32)slot->washCount);
    else
        vm_mock_admin_text_appendf(
            page, "<span class=\"muted\">配置默认 · 次%u</span>",
            (u32)slot->washCount);
    vm_mock_admin_text_appendf(
        page,
        "</td><td><form class=\"wash-form\" method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"set-equip-wash\">"
        "<input type=\"hidden\" name=\"account\" value=\"");
    vm_mock_admin_text_append_html(page, account);
    vm_mock_admin_text_appendf(
        page,
        "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
        "<input type=\"hidden\" name=\"where\" value=\"%s\">"
        "<input type=\"hidden\" name=\"loc\" value=\"%u\">"
        "<input type=\"hidden\" name=\"unlock\" value=\"%u\">"
        "<label><span>覆盖</span><select name=\"washed\">"
        "<option value=\"1\"%s>已洗练</option>"
        "<option value=\"0\"%s>未洗练</option></select></label>"
        "<label><span>属性</span><select name=\"type\">",
        roleId, where, loc, (u32)unlock,
        washed ? " selected" : "",
        !washed ? " selected" : "");
    for (t = 1; t <= 10; ++t)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%s</option>",
            (u32)t, type == t ? " selected" : "",
            vm_mock_admin_role_wash_attr_name(t, (t == 9 || t == 10) ? flag : 0));
    }
    vm_mock_admin_text_appendf(
        page,
        "</select></label>"
        "<label><span>%%</span><select name=\"flag\">"
        "<option value=\"0\"%s>数值</option>"
        "<option value=\"1\"%s>百分比</option></select></label>"
        "<label><span>数值</span><input type=\"number\" name=\"value\" "
        "min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<label><span>次数</span><input type=\"number\" name=\"wash_count\" "
        "min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<button type=\"submit\">保存</button></form></td></tr>",
        flag == 0 ? " selected" : "",
        flag != 0 ? " selected" : "",
        (u32)value, (u32)washCount);
}

static void vm_mock_admin_render_equip_wash_piece(
    vm_mock_admin_text *page, const char *account, u32 roleId,
    const char *whereLabel, const char *where, u32 loc,
    u32 itemId, u16 enhanceLevel, u32 instanceId,
    const vm_net_mock_enhance_wash_state *wash)
{
    u8 i;

    if (page == NULL || account == NULL || where == NULL || wash == NULL ||
        itemId == 0)
        return;
    vm_mock_admin_text_appendf(page, "<div class=\"wash-piece\"><h4>");
    vm_mock_admin_text_append_html(page, whereLabel);
    vm_mock_admin_text_appendf(page, " · ");
    vm_mock_admin_append_item_name_html(page, itemId);
    vm_mock_admin_text_appendf(
        page,
        " <span class=\"badge\">+%u</span></h4>"
        "<p class=\"muted\">item %u · instance %u · %s %u</p>"
        "<table class=\"wash-table\"><thead><tr>"
        "<th>档</th><th>状态</th><th>当前</th><th>调整</th>"
        "</tr></thead><tbody>",
        (u32)enhanceLevel, itemId, instanceId,
        strcmp(where, "equip") == 0 ? "slot" : "seq", loc);
    for (i = 0; i < VM_NET_MOCK_ENHANCE_WASH_SLOT_COUNT; ++i)
    {
        vm_mock_admin_render_equip_wash_tier_form(
            page, account, roleId, where, loc,
            vm_net_mock_enhance_unlock_for_index(i), &wash->slots[i]);
    }
    vm_mock_admin_text_appendf(page, "</tbody></table></div>");
}

static void vm_mock_admin_render_role_equip_wash(
    vm_mock_admin_text *page, const char *account,
    const vm_net_mock_role_state *role, const char *roleNameUtf8)
{
    u32 i;
    u32 bagCount = 0;
    u32 shown = 0;
    char whereLabel[64];

    if (page == NULL || account == NULL || role == NULL)
        return;
    vm_mock_admin_text_appendf(page, "<div class=\"wash-role\"><h3>装备洗练 · ");
    vm_mock_admin_text_append_html(
        page, roleNameUtf8 && roleNameUtf8[0] ? roleNameUtf8 : "-");
    vm_mock_admin_text_appendf(page, " <span class=\"muted\">ID %u</span></h3>",
                               role->roleId);

    for (i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
    {
        u32 itemId = role->equippedItemIds[i];
        if (itemId == 0 ||
            vm_net_mock_find_equipment_catalog_item(itemId) == NULL)
            continue;
        snprintf(whereLabel, sizeof(whereLabel), "穿戴·%s",
                 vm_mock_admin_role_equip_slot_name((u8)i));
        vm_mock_admin_render_equip_wash_piece(
            page, account, role->roleId, whereLabel, "equip", i, itemId,
            role->equippedEnhanceLevels[i],
            role->equippedEnhanceInstanceIds[i],
            &role->equippedEnhanceWash[i]);
        ++shown;
    }

    bagCount = vm_net_mock_role_backpack_count(role);
    for (i = 0; i < bagCount && i < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->itemId == 0 || item->count == 0)
            continue;
        if (vm_net_mock_find_equipment_catalog_item(item->itemId) == NULL)
            continue;
        snprintf(whereLabel, sizeof(whereLabel), "背包");
        vm_mock_admin_render_equip_wash_piece(
            page, account, role->roleId, whereLabel, "bag", item->seq,
            item->itemId, item->enhanceLevel, item->enhanceInstanceId,
            &item->enhanceWash);
        ++shown;
    }

    if (shown == 0)
        vm_mock_admin_text_appendf(
            page, "<p class=\"muted\">该角色暂无穿戴或背包装备可调整洗练。</p>");
    vm_mock_admin_text_appendf(
        page,
        "<p class=\"muted grant-note\">覆盖=已洗练时写入 type/flag/value；"
        "未洗练清除覆盖并回退配置默认。次数影响下次洗练金币阶梯。"
        "在线角色重开背包/洗练面板或重登后可见。</p></div>");
}

static void vm_mock_admin_render_item_grant_form(
    vm_mock_admin_text *page, const char *account,
    const u32 *roleIds, char roleNames[][128], u32 roleCount)
{
    if (page == NULL || account == NULL || account[0] == 0 ||
        roleIds == NULL || roleNames == NULL || roleCount == 0)
    {
        return;
    }
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"item-grant\"><h2>给予物品</h2>"
        "<form class=\"grant-form\" method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"grant-item\">"
        "<input type=\"hidden\" name=\"account\" value=\"");
    vm_mock_admin_text_append_html(page, account);
    vm_mock_admin_text_appendf(page,
        "\"><label><span>角色</span><select name=\"role\" required>");
    for (u32 i = 0; i < roleCount; ++i)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"%u\">", roleIds[i]);
        vm_mock_admin_text_append_html(page, roleNames[i]);
        vm_mock_admin_text_appendf(page, "（ID %u）</option>", roleIds[i]);
    }
    vm_mock_admin_text_appendf(page, "</select></label>");
    vm_mock_admin_render_item_picker_field(page, "grant-item", "item",
                                           "物品", 0, true);
    vm_mock_admin_text_appendf(
        page,
        "<label><span>数量</span><input type=\"number\" name=\"amount\" min=\"1\" max=\"1000\" value=\"1\" required></label>"
        "<button type=\"submit\">给予物品</button></form>"
        "<p class=\"muted grant-note\">相同物品会叠加；新物品需要背包存在空位。装备也遵循现有背包存储规则。</p></div>");
    vm_mock_admin_render_item_picker_modal(page);
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
    /* Mall 秘宝 = 类别 14 / 上架类别 10，以及任意 server_shop_items 上架非装备。 */
    return item != NULL && !item->isEquip &&
           (item->category == 14 || item->category == 10 ||
            item->shopDbListed != 0);
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
    int filterMall, int filterNpc,
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
    if (filterMall == 1 && !item->enabled)
        return false;
    if (filterMall == 0 && item->enabled)
        return false;
    if (filterNpc == 1 && !item->npcEnabled)
        return false;
    if (filterNpc == 0 && item->npcEnabled)
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

/* Query mall=/npc=: all|-1 omitted, 1=已上架, 0=未上架. */
static int vm_mock_admin_shop_shelf_filter_value(const char *text)
{
    if (text == NULL || text[0] == 0 || strcmp(text, "all") == 0)
        return -1;
    if (strcmp(text, "1") == 0)
        return 1;
    if (strcmp(text, "0") == 0)
        return 0;
    return -1;
}

static void vm_mock_admin_shop_shelf_filter_text(int value, char *out,
                                                 size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    if (value == 1)
        snprintf(out, outCap, "1");
    else if (value == 0)
        snprintf(out, outCap, "0");
    else
        snprintf(out, outCap, "all");
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
    char mallFilterText[8];
    char npcFilterText[8];
    char pageText[32];
    char status[16];
    char message[256];
    char categoryEncoded[64];
    char searchEncoded[384];
    char mallEncoded[16];
    char npcEncoded[16];
    bool filterCategory = false;
    bool filterEquipment = false;
    bool filterSecretTreasure = false;
    bool filterDivineArms = false;
    int filterMall = -1;
    int filterNpc = -1;
    u8 filterCategoryValue = 0;
    u32 itemCount = vm_net_mock_load_shop_catalog();
    u32 matchedCount = 0;
    u32 enabledCount = 0;
    u32 disabledCount = 0;
    u32 npcEnabledCount = 0;
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
    memset(mallFilterText, 0, sizeof(mallFilterText));
    memset(npcFilterText, 0, sizeof(npcFilterText));
    memset(pageText, 0, sizeof(pageText));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "category", categoryFilter,
                                   sizeof(categoryFilter));
    (void)vm_mock_admin_form_value(query, "q", search, sizeof(search));
    (void)vm_mock_admin_form_value(query, "mall", mallFilterText,
                                   sizeof(mallFilterText));
    (void)vm_mock_admin_form_value(query, "npc", npcFilterText,
                                   sizeof(npcFilterText));
    (void)vm_mock_admin_form_value(query, "page", pageText, sizeof(pageText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    filterMall = vm_mock_admin_shop_shelf_filter_value(mallFilterText);
    filterNpc = vm_mock_admin_shop_shelf_filter_value(npcFilterText);
    vm_mock_admin_shop_shelf_filter_text(filterMall, mallFilterText,
                                         sizeof(mallFilterText));
    vm_mock_admin_shop_shelf_filter_text(filterNpc, npcFilterText,
                                         sizeof(npcFilterText));
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
        if (item->npcEnabled)
            ++npcEnabledCount;
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
                filterSecretTreasure, filterDivineArms, filterMall, filterNpc,
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
    vm_mock_admin_url_encode(mallFilterText, mallEncoded, sizeof(mallEncoded));
    vm_mock_admin_url_encode(npcFilterText, npcEncoded, sizeof(npcEncoded));

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 商品管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1240px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}.sub,.muted{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;gap:6px;margin:0 0 14px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}"
        ".card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.shop-card{display:flex;flex-direction:column;min-height:0;flex:1}.summary{display:flex;gap:9px;flex-wrap:wrap;margin-bottom:12px}.badge{padding:3px 8px;border-radius:999px;background:#eef4ff;color:#175cd3}.badge.secret{background:#fff4e5;color:#b54708}.badge.arms{background:#f4f3ff;color:#5925dc}.badge.off{background:#fef3f2;color:#b42318}.badge.npc{background:#ecfdf3;color:#027a48}.filters{display:grid;grid-template-columns:minmax(160px,.9fr) minmax(120px,.45fr) minmax(120px,.45fr) minmax(180px,1fr) auto;gap:9px;align-items:end;margin-bottom:12px}.filters label{display:grid;gap:4px}.filters span{font-size:12px;color:#667085}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.shop-list{min-height:0;flex:1;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;border:1px solid #eaecf0;border-radius:8px}table{border-collapse:collapse;width:100%%}th,td{text-align:left;padding:10px 9px;border-bottom:1px solid #eaecf0;vertical-align:middle}th{position:sticky;top:0;background:#f9fafb;color:#667085;z-index:1}.name{min-width:200px}.section{display:inline-block;padding:2px 7px;border-radius:999px;background:#f2f4f7;color:#475467;font-size:12px;white-space:nowrap}.section.secret{background:#fff4e5;color:#b54708}.section.arms{background:#f4f3ff;color:#5925dc}.row-form{display:grid;grid-template-columns:90px 90px 100px 100px auto;gap:7px;align-items:center}.state{font-size:12px;font-weight:600;display:flex;flex-direction:column;gap:2px}.state .on{color:#027a48}.state .off{color:#b42318}.pages{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:12px}.page-links{display:flex;gap:7px}.page-links a{padding:6px 10px;border:1px solid #d0d5dd;border-radius:6px;color:#344054;text-decoration:none}.notice{padding:10px 12px;border-radius:7px;margin-bottom:12px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.foot{font-size:12px;color:#667085;margin:10px 0 0}"
        "@media(max-width:760px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:16px 9px;overflow:visible}.shop-card{min-height:700px}.filters,.row-form{grid-template-columns:1fr}.shop-list{min-height:520px}.tabs{overflow:auto}.name{min-width:150px}}"
        "</style><script src=\"/admin.js?v=8\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">商品价格与商城/NPC 上架 · 保存后立即生效</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_append_tabs(&page, "shop");
    vm_mock_admin_text_appendf(
        &page,
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
        "<form method=\"post\" action=\"/action\" style=\"display:flex;flex-wrap:wrap;gap:10px;align-items:end;margin:0 0 12px\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-equip-sell-ratio\">"
        "<input type=\"hidden\" name=\"category\" value=\"%s\">"
        "<input type=\"hidden\" name=\"mall\" value=\"%s\">"
        "<input type=\"hidden\" name=\"npc\" value=\"%s\">"
        "<input type=\"hidden\" name=\"q\" value=\"",
        categoryFilter, mallFilterText, npcFilterText);
    vm_mock_admin_text_append_html(&page, search);
    vm_mock_admin_text_appendf(
        &page,
        "\"><input type=\"hidden\" name=\"page\" value=\"%u\">"
        "<label style=\"display:grid;gap:4px;min-width:220px\"><span style=\"font-size:12px;color:#667085\">"
        "装备出售/丢弃价格比例 %%（相对目录「价值」）</span>"
        "<input type=\"number\" name=\"equip_sell_percent\" min=\"0\" max=\"100\" value=\"%u\" required></label>"
        "<button type=\"submit\">保存出售比例</button>"
        "<span class=\"muted\" style=\"align-self:center\">当前回收 = floor(价值 × 比例 / 100)；默认 10%%。"
        "表 <code>server_equip_sell_config</code>。</span></form>",
        pageNumber, vm_net_mock_equip_sell_percent_get());
    vm_mock_admin_text_appendf(
        &page,
        "<div class=\"summary\"><span class=\"badge\">目录 %u</span>"
        "<span class=\"badge\">商城上架 %u</span>"
        "<span class=\"badge off\">商城下架 %u</span>"
        "<span class=\"badge npc\">NPC上架 %u</span>"
        "<span class=\"badge secret\">秘宝道具 %u（商城 %u）</span>"
        "<span class=\"badge arms\">神兵利器 %u（商城 %u）</span></div>"
        "<form class=\"filters\" method=\"get\" action=\"/\"><input type=\"hidden\" name=\"tab\" value=\"shop\">"
        "<label><span>商城分区 / 物品分类</span><select name=\"category\"><option value=\"all\"%s>全部商品</option>"
        "<option value=\"secret\"%s>商城 · 秘宝道具</option><option value=\"arsenal\"%s>商城 · 神兵利器</option>",
        itemCount, enabledCount, disabledCount, npcEnabledCount,
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
        "</select></label>"
        "<label><span>上架商城</span><select name=\"mall\">"
        "<option value=\"all\"%s>全部</option>"
        "<option value=\"1\"%s>已上架商城</option>"
        "<option value=\"0\"%s>未上架商城</option></select></label>"
        "<label><span>上架 NPC</span><select name=\"npc\">"
        "<option value=\"all\"%s>全部</option>"
        "<option value=\"1\"%s>已上架 NPC</option>"
        "<option value=\"0\"%s>未上架 NPC</option></select></label>"
        "<label><span>名称或物品 ID</span><input type=\"search\" name=\"q\" value=\"",
        filterMall < 0 ? " selected" : "",
        filterMall == 1 ? " selected" : "",
        filterMall == 0 ? " selected" : "",
        filterNpc < 0 ? " selected" : "",
        filterNpc == 1 ? " selected" : "",
        filterNpc == 0 ? " selected" : "");
    vm_mock_admin_text_append_html(&page, search);
    vm_mock_admin_text_appendf(&page,
        "\" placeholder=\"输入名称或 ID\"></label><button type=\"submit\">筛选</button></form>"
        "<div class=\"shop-list\"><table><thead><tr><th>ID / 名称</th><th>商城分区</th><th>DSH 分类</th><th>当前状态</th><th>商城价 / NPC价 / 上架</th></tr></thead><tbody>");

    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        char itemNameUtf8[128];

        if (!vm_mock_admin_shop_item_matches(
                item, filterCategory, filterEquipment, filterCategoryValue,
                filterSecretTreasure, filterDivineArms, filterMall, filterNpc,
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
            &page, "（%u）</td><td><span class=\"state\">"
            "<span class=\"%s\">商城%s</span>"
            "<span class=\"%s\">NPC%s</span></span></td><td>"
            "<form class=\"row-form\" method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-shop-item\">"
            "<input type=\"hidden\" name=\"item\" value=\"%u\">"
            "<input type=\"hidden\" name=\"category\" value=\"",
            item->category,
            item->enabled ? "on" : "off",
            item->enabled ? "已上架" : "未上架",
            item->npcEnabled ? "on" : "off",
            item->npcEnabled ? "已上架" : "未上架",
            item->itemId);
        vm_mock_admin_text_append_html(&page, categoryFilter);
        vm_mock_admin_text_appendf(&page, "\"><input type=\"hidden\" name=\"q\" value=\"");
        vm_mock_admin_text_append_html(&page, search);
        vm_mock_admin_text_appendf(
            &page, "\"><input type=\"hidden\" name=\"mall\" value=\"%s\">"
            "<input type=\"hidden\" name=\"npc\" value=\"%s\">"
            "<input type=\"hidden\" name=\"page\" value=\"%u\">"
            "<input aria-label=\"商城价格(酷宝)\" title=\"商城酷宝价\" type=\"number\" name=\"price\" min=\"1\" max=\"4294967295\" value=\"%u\" required>"
            "<input aria-label=\"NPC价格(铜币)\" title=\"NPC铜币价\" type=\"number\" name=\"npc_price\" min=\"1\" max=\"4294967295\" value=\"%u\" required>"
            "<select aria-label=\"上架到商城\" name=\"enabled\">"
            "<option value=\"1\"%s>上架商城</option>"
            "<option value=\"0\"%s>不上架商城</option></select>"
            "<select aria-label=\"上架到NPC\" name=\"npc_enabled\">"
            "<option value=\"1\"%s>上架NPC</option>"
            "<option value=\"0\"%s>不上架NPC</option></select>"
            "<button type=\"submit\">保存</button></form>%s</td></tr>",
            mallFilterText, npcFilterText, pageNumber, item->price,
            item->npcPrice ? item->npcPrice : item->price,
            item->enabled ? " selected" : "",
            item->enabled ? "" : " selected",
            item->npcEnabled ? " selected" : "",
            item->npcEnabled ? "" : " selected",
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
            &page,
            "<a href=\"/?tab=shop&amp;category=%s&amp;mall=%s&amp;npc=%s&amp;q=%s&amp;page=%u\">上一页</a>",
            categoryEncoded, mallEncoded, npcEncoded, searchEncoded,
            pageNumber - 1);
    if (pageNumber < pageCount)
        vm_mock_admin_text_appendf(
            &page,
            "<a href=\"/?tab=shop&amp;category=%s&amp;mall=%s&amp;npc=%s&amp;q=%s&amp;page=%u\">下一页</a>",
            categoryEncoded, mallEncoded, npcEncoded, searchEncoded,
            pageNumber + 1);
    vm_mock_admin_text_appendf(
        &page,
        "</div></div><p class=\"foot\">价格与货架保存在 MySQL <code>server_shop_items</code>。"
        "<strong>商城价</strong>（price / 酷宝）与<strong>NPC价</strong>（npc_price / 铜币）相互独立。"
        "<strong>上架商城</strong>（enabled）的非装备出现在商城「秘宝道具」并可酷宝购买；"
        "<strong>上架NPC</strong>（npc_enabled）的非装备出现在药品商人（铜币）。"
        "两开关相互独立，可同时开、只开其一或全关。"
        "武器/防具 NPC 仍按白装目录出售，不受本页 NPC 开关影响（NPC 价仍可用本表覆盖）。"
        "装备出售、丢弃、战斗心得自动卖出共用上方「出售价格比例」。"
        "下架不会删除角色已有物品，也不影响后台赠送。</p></section></main></body></html>");
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
        "</style><script src=\"/admin.js?v=7\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">启动 CBM 模块发布与具名资源下发</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_append_tabs(&page, "updates");
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
    /* Full task catalog is ~256KB; keep it off the worker stack. */
    vm_net_mock_task_definition *tasks = NULL;
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

    tasks = (vm_net_mock_task_definition *)calloc(
        VM_NET_MOCK_TASK_CATALOG_MAX, sizeof(*tasks));
    if (tasks == NULL)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>任务管理页面内存不足。</p>");
        return;
    }
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
        ".item-field{display:grid;gap:4px}.item-field>span{font-size:12px;color:#667085}button.item-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px;white-space:normal}.item-picker-trigger small{color:#667085;font-weight:400}.item-picker-trigger.compact{min-height:32px;font-size:12px}.item-picker-head-actions{display:flex;gap:8px;align-items:center}.item-picker-head-actions #item-picker-clear{background:#f2f4f7;color:#475467}.item-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899}.item-picker-panel{width:min(780px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.item-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.item-picker-head h3{font-size:19px;margin:0}.item-picker-head p{margin:2px 0 0;color:#667085}.item-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.item-picker-tools{display:grid;grid-template-columns:minmax(200px,.8fr) minmax(260px,1.2fr);gap:10px;padding:14px 20px 10px}.item-picker-tools label{display:grid;gap:4px}.item-picker-tools label>span{font-size:12px;color:#667085}.item-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.item-picker-error{color:#b42318;font-weight:600}.item-picker-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:140px;overflow:auto;padding:0 20px 20px}.item-choice{display:grid;gap:2px;padding:10px 12px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.item-choice:hover{border-color:#84adff;background:#f5f8ff}.item-choice strong{font-size:14px}.item-choice span{color:#667085;font-size:12px}.item-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}[hidden]{display:none!important}.modal-open{overflow:hidden}@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto}.grid{grid-template-columns:1fr}.list{max-height:300px}.fields{grid-template-columns:1fr 1fr}.item-picker-tools,.item-picker-list{grid-template-columns:1fr}}</style><script src=\"/admin.js?v=7\" defer></script></head><body><main class=\"wrap\">"
        "<div class=\"head\"><div><h1>江湖OL 后台管理</h1><p class=\"sub\">任务定义、奖励与 NPC 对话</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\">退出登录</button></form></div>");
    vm_mock_admin_append_tabs(&page, "tasks");
    vm_mock_admin_text_appendf(
        &page,
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
    vm_mock_admin_text_appendf(&page, "</div><div class=\"group\"><h2>任务目标</h2><div class=\"fields\"><label class=\"field\"><span>条件一类型</span>"); vm_mock_admin_render_task_requirement_select(&page, "requirement_type1", edit.requirementType1); vm_mock_admin_text_appendf(&page, "</label><div class=\"field\"><span>条件一目标 ID</span><input id=\"task-requirement-1\" type=\"number\" name=\"requirement_id1\" min=\"0\" max=\"4294967295\" value=\"%u\" data-item-picker-input>", edit.requirementId1); vm_mock_admin_render_item_picker_button(&page, "task-requirement-1", "收集物品时点击选择"); vm_mock_admin_text_appendf(&page, "</div><label class=\"field\"><span>条件一数量</span><input type=\"number\" name=\"requirement_count1\" min=\"0\" max=\"255\" value=\"%u\"></label>", edit.requirementCount1);
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>条件二类型</span>"); vm_mock_admin_render_task_requirement_select(&page, "requirement_type2", edit.requirementType2); vm_mock_admin_text_appendf(&page, "</label><div class=\"field\"><span>条件二目标 ID</span><input id=\"task-requirement-2\" type=\"number\" name=\"requirement_id2\" min=\"0\" max=\"4294967295\" value=\"%u\" data-item-picker-input>", edit.requirementId2); vm_mock_admin_render_item_picker_button(&page, "task-requirement-2", "收集物品时点击选择"); vm_mock_admin_text_appendf(&page, "</div><label class=\"field\"><span>条件二数量</span><input type=\"number\" name=\"requirement_count2\" min=\"0\" max=\"255\" value=\"%u\"></label><label class=\"field wide\"><span>目标说明（最多95字节）</span><textarea name=\"goal\" maxlength=\"95\">", edit.requirementCount2); vm_mock_admin_text_append_html(&page, goalUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label></div></div>");
    vm_mock_admin_text_appendf(&page, "<div class=\"group\"><h2>给予物品与奖励</h2><div class=\"fields\">"); vm_mock_admin_render_item_picker_field(&page, "task-given-item", "given_item_id", "接取给予物品", edit.givenItemId, false); vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>给予数量</span><input type=\"number\" name=\"given_item_count\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励经验</span><input type=\"number\" name=\"reward_exp\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励铜钱</span><input type=\"number\" name=\"reward_money\" min=\"0\" max=\"4294967295\" value=\"%u\"></label>", edit.givenItemCount, edit.rewardExp, edit.rewardMoney); vm_mock_admin_render_item_picker_field(&page, "task-reward-item", "reward_item_id", "奖励物品", edit.rewardItemId, false); vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>奖励物品数量</span><input type=\"number\" name=\"reward_item_count\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励物品类型</span><input type=\"number\" name=\"reward_item_type\" min=\"0\" max=\"255\" value=\"%u\"></label><label class=\"field\"><span>奖励说明（最多31字节）</span><input name=\"reward_text\" maxlength=\"31\" value=\"", edit.rewardItemCount, edit.rewardItemType); vm_mock_admin_text_append_html(&page, rewardUtf8); vm_mock_admin_text_appendf(&page, "\"></label></div></div>");
    vm_mock_admin_text_appendf(&page, "<div class=\"group\"><h2>NPC 对话</h2><p class=\"hint\">NPC 绑定该任务后按未接、进行中、可提交三种状态显示；留空时使用服务端安全默认文案。</p><div class=\"fields\"><label class=\"field wide\"><span>可接取时</span><textarea name=\"offer_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, offerUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label><label class=\"field wide\"><span>进行中</span><textarea name=\"active_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, activeUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label><label class=\"field wide\"><span>可提交时</span><textarea name=\"completed_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, completedUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label></div></div><p class=\"hint\">条件类型 1 为收集物品、2 为击败怪物；只有“收集物品”条件应使用目录选择器。两项都为 0 时，接取后再次与交付 NPC 对话即可完成。名称长度按客户端 GBK 字节槽校验。</p><div class=\"actions\"><button type=\"submit\">保存任务</button></div></form>");
    vm_mock_admin_render_item_picker_modal(&page);
    if (!createNew && edit.overridden)
    {
        vm_mock_admin_text_appendf(&page, "<form class=\"actions\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"delete-task-override\"><input type=\"hidden\" name=\"task_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">%s</button></form>", edit.taskId, edit.builtin ? "恢复 task.dsh 默认" : "删除自定义任务");
    }
    vm_mock_admin_text_appendf(&page, "</section></div></main></body></html>");
    if (page.truncated)
        snprintf(response, responseCap, "<!doctype html><meta charset=\"utf-8\"><p>任务管理页面超过大小限制。</p>");
    free(tasks);
}

#include "web_admin_monsters.inc.c"

static void vm_mock_admin_render_servers_page(char *response,
                                              size_t responseCap,
                                              const char *query)
{
    vm_mock_admin_text page;
    vm_net_mock_login_server rows[VM_NET_MOCK_LOGIN_SERVER_MAX];
    char status[16];
    char message[256];
    u32 rowCount = 0;
    u32 enabledCount = 0;

    memset(rows, 0, sizeof(rows));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    rowCount = vm_net_mock_login_server_admin_list(
        rows, VM_NET_MOCK_LOGIN_SERVER_MAX);
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (rows[i].enabled)
            ++enabledCount;
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 服务器列表管理</title><style>"
        "*{box-sizing:border-box}html,body{min-height:100%%}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1240px;margin:0 auto;padding:24px 18px 42px}header{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 12px}.sub,.muted{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}.summary{display:flex;gap:9px;flex-wrap:wrap;margin-bottom:14px}.badge{padding:3px 8px;border-radius:999px;background:#eef4ff;color:#175cd3}.badge.off{background:#fef3f2;color:#b42318}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d;margin-bottom:16px}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.server-list{display:grid;gap:12px}.server{border:1px solid #e4e7ec;border-radius:9px;padding:13px}.server-head{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:10px}.server-head h3{font-size:16px;margin:0}.state{font-size:12px;font-weight:650}.state.on{color:#027a48}.state.off{color:#b42318}.fields{display:grid;grid-template-columns:110px minmax(160px,1.3fr) minmax(130px,1fr) 130px 110px 110px auto;gap:9px;align-items:end}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.danger{background:#b42318}.actions{display:flex;gap:8px;align-items:end}.create-fields{display:grid;grid-template-columns:120px minmax(170px,1.3fr) minmax(130px,1fr) 140px 110px 110px auto;gap:10px;align-items:end}.hint{margin:11px 0 0;color:#667085;font-size:12px;line-height:1.65}@media(max-width:940px){.fields,.create-fields{grid-template-columns:1fr 1fr}.actions{align-items:stretch}}@media(max-width:620px){.wrap{padding:16px 10px}.fields,.create-fields{grid-template-columns:1fr}.actions{display:grid;grid-template-columns:1fr 1fr}}</style>"
        "</head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1><p class=\"sub\">标题服务器列表 · 保存后影响下一次登录</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_append_tabs(&page, "servers");
    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><div class=\"summary\"><span class=\"badge\">已配置 %u / %u</span><span class=\"badge\">已启用 %u</span><span class=\"badge off\">已停用 %u</span></div>",
        rowCount, VM_NET_MOCK_LOGIN_SERVER_MAX, enabledCount,
        rowCount >= enabledCount ? rowCount - enabledCount : 0);
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (rowCount == 0)
    {
        vm_mock_admin_text_appendf(&page,
            "<p class=\"muted\">服务器目录无法从 MySQL 读取；登录响应不会使用本地硬编码替代。</p>");
    }
    vm_mock_admin_text_appendf(&page, "<div class=\"server-list\">");
    for (u32 i = 0; i < rowCount; ++i)
    {
        char nameUtf8[128];
        char labelUtf8[128];

        memset(nameUtf8, 0, sizeof(nameUtf8));
        memset(labelUtf8, 0, sizeof(labelUtf8));
        vm_net_mock_gbk_label_to_utf8(rows[i].displayName, nameUtf8,
                                      sizeof(nameUtf8));
        vm_net_mock_gbk_label_to_utf8(rows[i].label, labelUtf8,
                                      sizeof(labelUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<article class=\"server\"><div class=\"server-head\"><h3>服务器 #%u</h3><span class=\"state %s\">%s</span></div>"
            "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-login-server\"><div class=\"fields\">"
            "<label class=\"field\"><span>服务器 ID</span><input name=\"server_id\" type=\"number\" value=\"%u\" readonly></label>"
            "<label class=\"field\"><span>显示名称（GBK ≤31字节）</span><input name=\"display_name\" maxlength=\"31\" value=\"",
            rows[i].serverId, rows[i].enabled ? "on" : "off",
            rows[i].enabled ? "已启用" : "已停用", rows[i].serverId);
        vm_mock_admin_text_append_html(&page, nameUtf8);
        vm_mock_admin_text_appendf(&page,
            "\" required></label><label class=\"field\"><span>状态标签（GBK ≤31字节）</span><input name=\"status_label\" maxlength=\"31\" value=\"");
        vm_mock_admin_text_append_html(&page, labelUtf8);
        vm_mock_admin_text_appendf(&page,
            "\" required></label><label class=\"field\"><span>颜色（0-16777215）</span><input name=\"display_color\" type=\"number\" min=\"0\" max=\"16777215\" value=\"%u\" required></label>"
            "<label class=\"field\"><span>排序</span><input name=\"sort_order\" type=\"number\" min=\"0\" max=\"4294967295\" value=\"%u\" required></label>"
            "<label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\"%s>启用</option><option value=\"0\"%s>停用</option></select></label>"
            "<div class=\"actions\"><button type=\"submit\">保存</button></div></div></form>"
            "<form class=\"actions\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"delete-login-server\"><input type=\"hidden\" name=\"server_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">删除</button></form></article>",
            rows[i].displayColor, rows[i].sortOrder,
            rows[i].enabled ? " selected" : "",
            rows[i].enabled ? "" : " selected", rows[i].serverId);
    }
    vm_mock_admin_text_appendf(&page,
        "</div></section><section class=\"card\"><h2>新增服务器</h2><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"create-login-server\"><div class=\"create-fields\">"
        "<label class=\"field\"><span>服务器 ID</span><input name=\"server_id\" type=\"number\" min=\"1\" max=\"4294967295\" required></label>"
        "<label class=\"field\"><span>显示名称</span><input name=\"display_name\" maxlength=\"31\" required></label>"
        "<label class=\"field\"><span>状态标签</span><input name=\"status_label\" maxlength=\"31\" value=\"推荐\" required></label>"
        "<label class=\"field\"><span>颜色（白色 16777215）</span><input name=\"display_color\" type=\"number\" min=\"0\" max=\"16777215\" value=\"16777215\" required></label>"
        "<label class=\"field\"><span>排序</span><input name=\"sort_order\" type=\"number\" min=\"0\" max=\"4294967295\" value=\"0\" required></label>"
        "<label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\">启用</option><option value=\"0\">停用</option></select></label><div class=\"actions\"><button type=\"submit\">新增服务器</button></div></div></form>"
        "<p class=\"hint\">客户端确认的字段只有名称、状态标签、服务器 ID 和 24 位显示颜色；选择服务器后仍使用当前已连接的游戏服务，不会根据此页面配置切换 CBMS 主机或端口。最多配置 8 项，至少保留一个启用项。</p></section></main></body></html>");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>服务器列表管理页面超过大小限制。</p>");
    }
}

static void vm_mock_admin_render_gateways_page(char *response,
                                               size_t responseCap,
                                               const char *query);
static void vm_mock_admin_handle_gateway_zone_action(
    vm_mock_service_socket client, const char *action, const char *body);

static void vm_mock_admin_render_chest_page(char *response, size_t responseCap,
                                             const char *query);
static void vm_mock_admin_render_catalog_page(char *response, size_t responseCap,
                                               const char *query);
static void vm_mock_admin_handle_chest_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body);
static void vm_mock_admin_render_enhance_page(char *response, size_t responseCap,
                                              const char *query);
static void vm_mock_admin_handle_enhance_action(vm_mock_service_socket client,
                                                const char *action,
                                                const char *body);
static void vm_mock_admin_render_gem_page(char *response, size_t responseCap,
                                          const char *query);
static void vm_mock_admin_handle_gem_action(vm_mock_service_socket client,
                                            const char *action,
                                            const char *body);
static void vm_mock_admin_render_client_ui_page(char *response, size_t responseCap,
                                                const char *query);
static void vm_mock_admin_handle_client_ui_action(vm_mock_service_socket client,
                                                  const char *action,
                                                  const char *body);
static void vm_mock_admin_render_combat_page(char *response, size_t responseCap,
                                             const char *query);
static void vm_mock_admin_handle_combat_action(vm_mock_service_socket client,
                                               const char *action,
                                               const char *body);
static void vm_mock_admin_render_arena_page(char *response, size_t responseCap,
                                            const char *query);
static void vm_mock_admin_handle_arena_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body);
static void vm_mock_admin_render_world_boss_page(char *response, size_t responseCap,
                                                 const char *query);
static void vm_mock_admin_handle_world_boss_action(vm_mock_service_socket client,
                                                   const char *action,
                                                   const char *body);
static void vm_mock_admin_render_maintenance_page(char *response, size_t responseCap,
                                                  const char *query);
static void vm_mock_admin_handle_maintenance_action(vm_mock_service_socket client,
                                                    const char *action,
                                                    const char *body);
static void vm_mock_admin_render_ladder_page(char *response, size_t responseCap,
                                             const char *query);
static void vm_mock_admin_handle_ladder_action(vm_mock_service_socket client,
                                               const char *action,
                                               const char *body);
static void vm_mock_admin_render_activities_page(char *response, size_t responseCap,
                                                 const char *query);
static void vm_mock_admin_handle_activities_action(vm_mock_service_socket client,
                                                   const char *action,
                                                   const char *body);
static void vm_mock_admin_render_fruit_page(char *response, size_t responseCap,
                                            const char *query);
static void vm_mock_admin_handle_fruit_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body);
static void vm_mock_admin_render_guess_page(char *response, size_t responseCap,
                                            const char *query);
static void vm_mock_admin_handle_guess_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body);
static void vm_mock_admin_render_auction_page(char *response, size_t responseCap,
                                              const char *query);
static void vm_mock_admin_handle_auction_action(vm_mock_service_socket client,
                                                const char *action,
                                                const char *body);
static void vm_mock_admin_render_zangbao_page(char *response, size_t responseCap,
                                              const char *query);
static void vm_mock_admin_handle_zangbao_action(vm_mock_service_socket client,
                                                const char *action,
                                                const char *body);
static int vm_mock_zangbao_handle_http(vm_mock_service_socket client,
                                       const char *method, const char *target,
                                       const char *query, const char *body);
static int vm_mock_admin_serve_kcoin_qr(vm_mock_service_socket client,
                                        const char *query);

static void vm_mock_admin_render_mapedit_page(char *response,
                                              size_t responseCap,
                                              const char *query);

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
    u32 accountCount = 0;

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
    if (strcmp(tab, "mapedit") == 0)
    {
        vm_mock_admin_render_mapedit_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "tasks") == 0)
    {
        vm_mock_admin_render_task_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "monsters") == 0)
    {
        vm_mock_admin_render_monster_page(response, responseCap, query);
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
    if (strcmp(tab, "catalog") == 0)
    {
        vm_mock_admin_render_catalog_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "chests") == 0)
    {
        vm_mock_admin_render_chest_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "enhance") == 0)
    {
        vm_mock_admin_render_enhance_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "gem") == 0)
    {
        vm_mock_admin_render_gem_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "clientui") == 0)
    {
        vm_mock_admin_render_client_ui_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "combat") == 0)
    {
        vm_mock_admin_render_combat_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "arena") == 0)
    {
        vm_mock_admin_render_arena_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "worldboss") == 0)
    {
        vm_mock_admin_render_world_boss_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "ladder") == 0)
    {
        vm_mock_admin_render_ladder_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "fruit") == 0)
    {
        vm_mock_admin_render_fruit_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "guess") == 0)
    {
        vm_mock_admin_render_guess_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "auction") == 0)
    {
        vm_mock_admin_render_auction_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "zangbao") == 0)
    {
        vm_mock_admin_render_zangbao_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "activities") == 0)
    {
        vm_mock_admin_render_activities_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "servers") == 0)
    {
        vm_mock_admin_render_servers_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "gateways") == 0)
    {
        vm_mock_admin_render_gateways_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "maintenance") == 0)
    {
        vm_mock_admin_render_maintenance_page(response, responseCap, query);
        return;
    }
    (void)vm_mock_admin_form_value(query, "account", selectedAccount, sizeof(selectedAccount));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));

    vm_mock_service_account_db_load();
    if (!vm_mock_service_account_count(&accountCount))
        accountCount = 0;
    if ((selectedAccount[0] == 0 || vm_mock_service_account_find_record(selectedAccount) == NULL) &&
        accountCount > 0)
    {
        if (!vm_mock_service_account_first_id(selectedAccount, sizeof(selectedAccount)))
            selectedAccount[0] = 0;
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 后台管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1120px;height:100vh;margin:0 auto;padding:28px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 14px}"
        ".sub{color:#667085;margin:4px 0 20px}.grid{display:grid;grid-template-columns:240px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:18px;box-shadow:0 1px 2px #1018280d}.grid>aside{display:flex;flex-direction:column;min-height:0;overflow:hidden}.grid>section{min-width:0;min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}"
        ".tabs{display:flex;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}"
        ".accounts{display:flex;flex:1;min-height:0;flex-direction:column;gap:6px;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.account{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:9px 10px;border-radius:7px;color:#344054;text-decoration:none;scroll-margin-block:12px}.account:hover,.account.on{background:#eef4ff;color:#175cd3}.account-main{display:flex;align-items:center;gap:8px;min-width:0}.account-index{flex:0 0 auto;min-width:1.6em;color:#98a2b3;font-variant-numeric:tabular-nums;font-size:12px}"
        ".account-filter{display:flex;gap:6px;margin:0 0 10px;flex:none}.account-filter input{flex:1;min-width:0}.account-filter button{flex:none;padding:8px 10px}"
        ".dot{color:#12b76a}.muted{color:#98a2b3}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        "table{border-collapse:collapse;width:100%%}th,td{text-align:left;padding:10px 8px;border-bottom:1px solid #eaecf0;vertical-align:top}th{color:#667085;font-weight:600}"
        "input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}button:hover{background:#1849a9}"
        ".inline{display:flex;gap:7px;margin:0 0 7px}.inline input{min-width:105px}.forms{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:16px}.stack{display:grid;gap:9px}.badge{font-size:12px;background:#eef4ff;color:#175cd3;padding:2px 7px;border-radius:999px}.money{white-space:nowrap}.item-grant{border-top:1px solid #eaecf0;margin-top:18px;padding-top:18px}.grant-form{display:grid;grid-template-columns:minmax(130px,.8fr) minmax(280px,2fr) 90px auto;gap:9px;align-items:end}.grant-form.cosmetic-grant{grid-template-columns:minmax(140px,.9fr) minmax(110px,.6fr) minmax(220px,1.4fr) auto auto;}.grant-form.cosmetic-grant select#cosmetic-id{min-width:200px}.grant-form label,.grant-form .item-field{display:grid;gap:4px}.grant-form label>span,.grant-form .item-field>span{font-size:12px;color:#667085}.grant-note{margin:8px 0 0;font-size:12px}"
        ".wash-role{border-top:1px solid #eaecf0;margin-top:16px;padding-top:14px}.wash-role h3{font-size:15px;margin:0 0 10px}.wash-piece{border:1px solid #eaecf0;border-radius:8px;padding:10px 12px;margin:0 0 12px;background:#fafbfc}.wash-piece h4{margin:0 0 4px;font-size:14px}.wash-table{margin-top:8px}.wash-form{display:flex;flex-wrap:wrap;gap:6px;align-items:end}.wash-form label{display:grid;gap:2px;min-width:72px}.wash-form label>span{font-size:11px;color:#667085}.wash-form input,.wash-form select{padding:6px 7px;font-size:12px}.wash-form input[type=number]{width:78px}"
        "button.item-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px}button.item-picker-trigger:hover{background:#f9fafb;border-color:#84adff}button.item-picker-trigger small{color:#98a2b3;font-weight:400}.item-picker-head-actions{display:flex;align-items:center;gap:8px}.item-picker-head-actions #item-picker-clear{background:#f2f4f7;color:#475467}.item-picker-trigger.compact{min-height:32px;font-size:12px}"
        "[hidden]{display:none!important}.modal-open{overflow:hidden}.item-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899;backdrop-filter:blur(2px)}.item-picker-panel{width:min(780px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.item-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.item-picker-head h3{font-size:19px;margin:0}.item-picker-head p{margin:2px 0 0;color:#667085}.item-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.item-picker-close:hover{background:#e4e7ec;color:#1d2939}.item-picker-tools{display:grid;grid-template-columns:minmax(200px,.8fr) minmax(260px,1.2fr);gap:10px;padding:14px 20px 10px}.item-picker-tools label{display:grid;gap:4px}.item-picker-tools label>span{font-size:12px;color:#667085}.item-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.item-picker-error{color:#b42318;font-weight:600}.item-picker-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:140px;overflow:auto;padding:0 20px 20px;scrollbar-gutter:stable}.item-choice{display:grid;gap:2px;padding:10px 12px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.item-choice:hover{border-color:#84adff;background:#f5f8ff}.item-choice.selected{border-color:#175cd3;background:#eef4ff}.item-choice strong{font-size:14px}.item-choice span{color:#667085;font-size:12px}.item-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}.foot{margin-top:16px;color:#667085;font-size:12px}"
        "@media(max-width:780px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:18px 10px;overflow:visible}.grid,.forms{grid-template-columns:1fr;flex:none}.grid>aside,.grid>section{overflow:visible}.accounts{flex:none;max-height:220px;overflow:auto}.table-wrap{overflow:auto}.grant-form{grid-template-columns:1fr}.grant-form>button[type=submit]{justify-self:start}.item-modal{padding:10px}.item-picker-panel{max-height:calc(100vh - 20px)}.item-picker-tools,.item-picker-list{grid-template-columns:1fr}.item-picker-list{padding-inline:12px}.item-picker-head,.item-picker-tools{padding-inline:14px}}"
        "</style><script src=\"/admin.js?v=18\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">本机管理端口 · 数据直接保存到 MySQL · 普通钱币以铜为基础单位</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_append_tabs(&page, "accounts");
    {
        u32 regLimit = vm_mock_service_register_daily_limit();
        u32 regUsed = vm_mock_service_register_daily_used_today();
        u32 practiseMult = vm_net_mock_practise_exp_level_minute_mult();

        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\" style=\"margin-bottom:16px;flex:none\">"
            "<h2>每日注册上限</h2>"
            "<p class=\"sub\" style=\"margin-top:0\">全服玩家自助注册（网页/API）每日限额；"
            "后台「创建账号」不受此限制。今日已用 <strong>%u</strong> / %u。"
            "设为 0 表示禁止新注册。</p>"
            "<form method=\"post\" action=\"/action\" "
            "style=\"display:flex;flex-wrap:wrap;gap:10px;align-items:end;max-width:420px\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-registration-config\">"
            "<label style=\"display:grid;gap:4px;flex:1;min-width:140px\">"
            "<span style=\"font-size:12px;color:#667085\">每日名额</span>"
            "<input type=\"number\" name=\"daily_limit\" min=\"0\" max=\"9999\" "
            "value=\"%u\" required>"
            "</label>"
            "<button type=\"submit\">保存</button></form></section>",
            regUsed, regLimit, regLimit);
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\" style=\"margin-bottom:16px;flex:none\">"
            "<h2>修炼丹经验系数</h2>"
            "<p class=\"sub\" style=\"margin-top:0\">上线结算："
            "<code>经验 = 结算分钟 × 角色等级² × 系数</code>。"
            "结算分钟取 min(离线分钟, 银行剩余, 当日剩余额度)。"
            "默认 8；设为 0 则结算仍扣时间但不给经验。</p>"
            "<form method=\"post\" action=\"/action\" "
            "style=\"display:flex;flex-wrap:wrap;gap:10px;align-items:end;max-width:420px\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-practise-exp-mult\">"
            "<label style=\"display:grid;gap:4px;flex:1;min-width:140px\">"
            "<span style=\"font-size:12px;color:#667085\">等级²×分钟系数</span>"
            "<input type=\"number\" name=\"exp_level_minute_mult\" min=\"0\" max=\"1000\" "
            "value=\"%u\" required>"
            "</label>"
            "<button type=\"submit\">保存</button></form></section>",
            practiseMult);
    }
    vm_mock_admin_text_appendf(
        &page,
        "<div class=\"grid\">"
        "<aside class=\"card\"><h2>账号（%u）</h2>"
        "<div class=\"account-filter\">"
        "<input type=\"search\" id=\"account-search\" placeholder=\"按账号名筛选\" autocomplete=\"off\">"
        "<button type=\"button\" id=\"account-search-btn\">筛选</button></div>"
        "<div class=\"accounts\">",
        accountCount);

    if (accountCount == 0)
    {
        vm_mock_admin_text_appendf(&page, "<span class=\"muted\">暂无账号</span>");
    }
    else
    {
        vm_mock_service_account_id_list accounts;
        if (vm_mock_service_account_collect_ids(&accounts))
        {
            if (accounts.count > 1u)
            {
                qsort(accounts.ids, accounts.count, sizeof(accounts.ids[0]),
                      vm_mock_admin_account_list_compare);
            }
            for (u32 i = 0; i < accounts.count; ++i)
            {
                const char *accountId = accounts.ids[i];
                char encoded[192];
                bool online = vm_mock_admin_account_is_online(accountId);
                vm_mock_admin_url_encode(accountId, encoded, sizeof(encoded));
                if (strcmp(accountId, selectedAccount) == 0)
                {
                    vm_mock_admin_text_appendf(&page,
                        "<a id=\"selected-account\" class=\"account on\" aria-current=\"page\" data-key=\"");
                    vm_mock_admin_text_append_html(&page, accountId);
                    vm_mock_admin_text_appendf(&page,
                        "\" href=\"/?tab=accounts&amp;account=%s#selected-account\">"
                        "<span class=\"account-main\"><span class=\"account-index\">%u</span><span>",
                        encoded, i + 1u);
                }
                else
                {
                    vm_mock_admin_text_appendf(&page,
                        "<a class=\"account\" data-key=\"");
                    vm_mock_admin_text_append_html(&page, accountId);
                    vm_mock_admin_text_appendf(&page,
                        "\" href=\"/?tab=accounts&amp;account=%s#selected-account\">"
                        "<span class=\"account-main\"><span class=\"account-index\">%u</span><span>",
                        encoded, i + 1u);
                }
                vm_mock_admin_text_append_html(&page, accountId);
                vm_mock_admin_text_appendf(&page, "</span></span><span class=\"%s\">%s</span></a>",
                                           online ? "dot" : "muted", online ? "在线" : "离线");
            }
            vm_mock_service_account_id_list_free(&accounts);
        }
    }
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
    vm_mock_admin_text_appendf(&page,
                               "</h2><div id=\"role-details\" class=\"table-wrap\"><table><thead><tr>"
                               "<th>角色</th><th>等级 / 总经验</th><th>普通钱币</th><th>W 币</th><th>操作</th>"
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
            vm_mock_admin_text_appendf(&page,
                                       "<td>Lv.%u%s<br><span class=\"muted\">总经验 %u</span></td>",
                                       role->level,
                                       active ? " <span class=\"badge\">当前角色</span>" : "",
                                       role->exp);
            vm_mock_admin_text_appendf(&page,
                                       "<td class=\"money\">%u 金 %u 银 %u 铜<br><span class=\"muted\">总计 %u 铜</span></td><td>%u</td><td>",
                                       gold, silver, copper, role->money, role->wcoin);
            vm_mock_admin_text_appendf(&page,
                "<form class=\"inline\" method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"rename-role\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input type=\"text\" name=\"name\" maxlength=\"31\" value=\"",
                role->roleId);
            vm_mock_admin_text_append_html(&page, roleNameUtf8);
            vm_mock_admin_text_appendf(&page,
                "\" placeholder=\"新角色名\" required>"
                "<button type=\"submit\">改名</button></form>");
            vm_mock_admin_text_appendf(&page,
                "<form class=\"inline\" method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"set-role-exp\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input type=\"number\" name=\"exp\" min=\"0\" max=\"4294967295\" "
                "value=\"%u\" placeholder=\"总经验\" required>"
                "<button type=\"submit\">调整总经验</button></form>",
                role->roleId, role->exp);
            vm_mock_admin_text_appendf(&page,
                "<form class=\"inline\" method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"add-money\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input type=\"number\" name=\"amount\" step=\"1\" "
                "min=\"-2147483648\" max=\"2147483647\" "
                "placeholder=\"正数增加，负数扣除\" required>"
                "<button type=\"submit\">调整铜钱</button></form>",
                role->roleId);
            vm_mock_admin_text_appendf(&page,
                "<form class=\"inline\" method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"add-wcoin\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input type=\"number\" name=\"amount\" step=\"1\" "
                "min=\"-2147483648\" max=\"2147483647\" "
                "placeholder=\"正数增加，负数扣除\" required>"
                "<button type=\"submit\">调整 W 币</button></form>",
                role->roleId);
            {
                u8 ringActive = 0;
                u16 ringCount = 0;

                (void)vm_mock_ring_admin_peek_active(selectedAccount, role->roleId,
                                                     &ringActive, &ringCount);
                vm_mock_admin_text_appendf(
                    &page,
                    "<div class=\"muted\" style=\"margin:4px 0 2px;font-size:12px\">"
                    "跑环：今日 %u 环 · %s</div>",
                    (u32)ringCount, ringActive ? "有进行中任务" : "无进行中任务");
                if (ringActive)
                {
                    vm_mock_admin_text_appendf(&page,
                        "<form class=\"inline\" method=\"post\" action=\"/action\" "
                        "onsubmit=\"return confirm('\\u786e\\u8ba4\\u79fb\\u9664\\u8be5\\u89d2\\u8272\\u5f53\\u524d\\u5df2\\u63a5\\u8dd1\\u73af\\u4efb\\u52a1\\uff1f\\u4eca\\u65e5\\u73af\\u6570\\u4fdd\\u7559\\u3002');\">"
                        "<input type=\"hidden\" name=\"action\" value=\"clear-ring-task\">"
                        "<input type=\"hidden\" name=\"account\" value=\"");
                    vm_mock_admin_text_append_html(&page, selectedAccount);
                    vm_mock_admin_text_appendf(&page,
                        "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                        "<button type=\"submit\">移除跑环任务</button></form>",
                        role->roleId);
                }
            }
            vm_mock_admin_text_appendf(&page, "</td></tr>");
        }
        if (g_vm_net_mock_role_db.roleCount == 0)
            vm_mock_admin_text_appendf(&page, "<tr><td colspan=\"5\" class=\"muted\">该账号尚未创建角色</td></tr>");
        vm_mock_admin_text_appendf(&page, "</tbody></table>");
        vm_mock_admin_render_item_grant_form(
            &page, selectedAccount, managedRoleIds, managedRoleNames,
            managedRoleCount);
        vm_mock_admin_render_cosmetic_grant_form(
            &page, selectedAccount, managedRoleIds, managedRoleNames,
            managedRoleCount);
    }
    else
    {
        vm_mock_admin_text_appendf(&page, "<tr><td colspan=\"5\" class=\"muted\">");
        vm_mock_admin_text_append_html(&page, selectedAccount[0] ?
                                       (roleError ? roleError : "角色数据不可用") : "请选择账号");
        vm_mock_admin_text_appendf(&page, "</td></tr></tbody></table>");
    }
    vm_mock_admin_text_appendf(&page, "<div class=\"forms\">"
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
                               "<button type=\"submit\">保存新密码</button></form></div></div>");
    if (accountState != NULL)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
            char roleNameUtf8[128];
            vm_net_mock_gbk_label_to_utf8(role->name[0] ? role->name : "-",
                                          roleNameUtf8, sizeof(roleNameUtf8));
            vm_mock_admin_render_role_equip_wash(
                &page, selectedAccount, role, roleNameUtf8);
        }
        vm_mock_service_close_account_role_db_for_management(accountState, true);
    }
    vm_mock_admin_text_appendf(&page, "</div></div>"
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

    vm_mock_admin_url_encode(account ? account : "", accountEncoded, sizeof(accountEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded, sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded, sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH "?account=%s&status=%s&message=%s",
             accountEncoded, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
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

static void vm_mock_admin_redirect_mapedit(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=mapedit&scene=%s&status=%s&message=%s",
             sceneEncoded, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

enum
{
    /* JHOnlineData has ~370 GIFs; keep headroom for future assets. */
    VM_MOCK_ADMIN_MAP_SHEET_CATALOG_MAX = 1024
};

static bool vm_mock_admin_map_sheet_name_known(
    const char names[][64], u32 count, const char *name)
{
    if (name == NULL || name[0] == 0)
        return false;
    for (u32 i = 0; i < count; ++i)
    {
        if (strcmp(names[i], name) == 0)
            return true;
    }
    return false;
}

static int vm_mock_admin_sheet_name_compare(const void *lhs, const void *rhs)
{
    return strcmp((const char *)lhs, (const char *)rhs);
}

/* All safe .gif leaves under the authoritative JHOnlineData resource dir. */
static u32 vm_mock_admin_collect_map_tile_sheets(char names[][64], u32 namesCap)
{
    u32 sheetCount = 0;

    if (names == NULL || namesCap == 0)
        return 0;
    memset(names, 0, (size_t)namesCap * 64u);
#ifdef _WIN32
    {
        char configuredPattern[1200];
        const char *patterns[] = {
            NULL,
            "../web/fs/JHOnlineData/*.gif",
            "web/fs/JHOnlineData/*.gif"
        };
        WIN32_FIND_DATAA found;
        HANDLE search = INVALID_HANDLE_VALUE;

        memset(configuredPattern, 0, sizeof(configuredPattern));
        if (g_vm_net_mock_resource_dir[0] != 0)
        {
            size_t dirLen = strlen(g_vm_net_mock_resource_dir);
            if (snprintf(configuredPattern, sizeof(configuredPattern),
                         "%s%s*.gif", g_vm_net_mock_resource_dir,
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
                char gameName[64];
                size_t nameLen = strlen(found.cFileName);

                memset(gameName, 0, sizeof(gameName));
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                    nameLen == 0 || nameLen >= sizeof(gameName) ||
                    !vm_net_mock_str_ends_with(found.cFileName, ".gif") ||
                    !vm_mock_admin_host_resource_name_to_game(
                        found.cFileName, gameName, sizeof(gameName)) ||
                    !vm_net_mock_update_name_is_safe(gameName) ||
                    vm_mock_admin_map_sheet_name_known(names, sheetCount,
                                                        gameName))
                {
                    continue;
                }
                snprintf(names[sheetCount], 64, "%s", gameName);
                sheetCount++;
            } while (sheetCount < namesCap && FindNextFileA(search, &found));
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
            while (sheetCount < namesCap &&
                   (entry = readdir(directory)) != NULL)
            {
                char path[1400];
                char gameName[64];
                struct stat info;

                memset(gameName, 0, sizeof(gameName));
                if (!vm_net_mock_str_ends_with(entry->d_name, ".gif") ||
                    !vm_mock_admin_host_resource_name_to_game(
                        entry->d_name, gameName, sizeof(gameName)) ||
                    !vm_net_mock_update_name_is_safe(gameName) ||
                    vm_mock_admin_map_sheet_name_known(names, sheetCount,
                                                        gameName))
                {
                    continue;
                }
                snprintf(path, sizeof(path), "%s/%s", directories[directoryIndex],
                         entry->d_name);
                if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
                    continue;
                snprintf(names[sheetCount], 64, "%s", gameName);
                sheetCount++;
            }
            closedir(directory);
            break;
        }
    }
#endif
    if (sheetCount > 1)
        qsort(names, sheetCount, 64, vm_mock_admin_sheet_name_compare);
    return sheetCount;
}

static bool vm_mock_admin_build_map_edit_json(const char *runtimeScene,
                                              const char *sceneUtf8,
                                              char *out, size_t outCap)
{
    char mapName[64];
    char mapNameUtf8[192];
    char escapedScene[384];
    char escapedMap[384];
    u8 *mapPayload = NULL;
    u32 mapPayloadLen = 0;
    vm_mock_admin_scene_preview preview;
    char *cellsB64 = NULL;
    u32 cellsB64Len = 0;
    vm_mock_admin_scene_file *sceneFiles = NULL;
    u32 sceneCount = 0;
    char (*availableSheets)[64] = NULL;
    u32 availableCount = 0;
    vm_mock_admin_text json;
    bool ok = false;
    bool firstShared = true;

    if (out == NULL || outCap == 0 || runtimeScene == NULL || runtimeScene[0] == 0)
        return false;
    out[0] = 0;
    memset(mapName, 0, sizeof(mapName));
    memset(&preview, 0, sizeof(preview));
    if (!vm_mock_admin_scene_map_name(runtimeScene, mapName, sizeof(mapName)) ||
        !vm_mock_admin_load_data_payload(mapName, ".map", &mapPayload,
                                         &mapPayloadLen, NULL) ||
        !vm_mock_admin_parse_map_preview(mapPayload, mapPayloadLen, mapName,
                                         &preview) ||
        preview.cells == NULL || preview.cellCount == 0)
    {
        goto done;
    }
    cellsB64 = (char *)malloc((size_t)preview.cellCount * 4u * 2u + 8u);
    if (cellsB64 == NULL ||
        !vm_mock_admin_base64_encode(preview.cells, preview.cellCount * 4u,
                                     cellsB64,
                                     (size_t)preview.cellCount * 4u * 2u + 8u,
                                     &cellsB64Len))
    {
        goto done;
    }
    vm_net_mock_gbk_label_to_utf8(mapName, mapNameUtf8, sizeof(mapNameUtf8));
    if (!vm_mock_admin_json_escape(sceneUtf8 ? sceneUtf8 : "", escapedScene,
                                   sizeof(escapedScene)) ||
        !vm_mock_admin_json_escape(mapNameUtf8, escapedMap, sizeof(escapedMap)))
    {
        goto done;
    }
    sceneFiles = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_SCENE_FILE_MAX, sizeof(*sceneFiles));
    if (sceneFiles != NULL)
        sceneCount = vm_mock_admin_collect_scene_files(
            sceneFiles, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    availableSheets = (char (*)[64])calloc(VM_MOCK_ADMIN_MAP_SHEET_CATALOG_MAX,
                                            64);
    if (availableSheets != NULL)
        availableCount = vm_mock_admin_collect_map_tile_sheets(
            availableSheets, VM_MOCK_ADMIN_MAP_SHEET_CATALOG_MAX);

    vm_mock_admin_text_init(&json, out, outCap);
    vm_mock_admin_text_appendf(
        &json,
        "{\"ok\":true,\"scene\":\"%s\",\"mapName\":\"%s\",\"width\":%u,"
        "\"height\":%u,\"tileWidth\":%u,\"tileHeight\":%u,\"cols\":%u,"
        "\"rows\":%u,\"cellCount\":%u,\"imageMax\":%u,\"imageNames\":[",
        escapedScene, escapedMap, preview.width, preview.height,
        preview.tileWidth, preview.tileHeight, preview.cols, preview.rows,
        preview.cellCount, (u32)VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX);
    for (u32 i = 0; i < preview.imageCount; ++i)
    {
        char nameUtf8[192];
        char escapedName[384];

        vm_net_mock_gbk_label_to_utf8(preview.imageNames[i], nameUtf8,
                                      sizeof(nameUtf8));
        if (!vm_mock_admin_json_escape(nameUtf8, escapedName,
                                       sizeof(escapedName)))
            goto done;
        vm_mock_admin_text_appendf(&json, "%s\"%s\"", i ? "," : "",
                                   escapedName);
    }
    vm_mock_admin_text_appendf(&json, "],\"availableSheets\":[");
    for (u32 i = 0; i < availableCount; ++i)
    {
        char nameUtf8[192];
        char escapedName[384];

        vm_net_mock_gbk_label_to_utf8(availableSheets[i], nameUtf8,
                                      sizeof(nameUtf8));
        if (!vm_mock_admin_json_escape(nameUtf8, escapedName,
                                       sizeof(escapedName)))
            goto done;
        vm_mock_admin_text_appendf(&json, "%s\"%s\"", i ? "," : "",
                                   escapedName);
    }
    vm_mock_admin_text_appendf(&json, "],\"sharedBy\":[");
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char otherRuntime[64];
        char otherMap[64];
        char otherUtf8[192];
        char escapedOther[384];

        memset(otherRuntime, 0, sizeof(otherRuntime));
        memset(otherMap, 0, sizeof(otherMap));
        if (!vm_mock_admin_scene_file_to_runtime_key(
                sceneFiles[i].name, otherRuntime, sizeof(otherRuntime)) ||
            vm_net_mock_scene_names_equal_loose(otherRuntime, runtimeScene) ||
            !vm_mock_admin_scene_map_name(otherRuntime, otherMap,
                                          sizeof(otherMap)) ||
            strcmp(otherMap, mapName) != 0)
        {
            continue;
        }
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name, otherUtf8,
                                      sizeof(otherUtf8));
        if (!vm_mock_admin_json_escape(otherUtf8, escapedOther,
                                       sizeof(escapedOther)))
            goto done;
        vm_mock_admin_text_appendf(&json, "%s\"%s\"", firstShared ? "" : ",",
                                   escapedOther);
        firstShared = false;
    }
    vm_mock_admin_text_appendf(&json, "],\"cellsB64\":\"%s\"}\n", cellsB64);
    ok = !json.truncated;

done:
    free(mapPayload);
    free(cellsB64);
    free(sceneFiles);
    free(availableSheets);
    return ok;
}

static void vm_mock_admin_render_mapedit_page(char *response,
                                              size_t responseCap,
                                              const char *query)
{
    vm_mock_admin_text page;
    char selectedSceneUtf8[192];
    char selectedSceneFile[64];
    char runtimeScene[64];
    char status[16];
    char message[256];
    char encodedScene[512];
    char mapName[64];
    char mapNameUtf8[192];
    vm_mock_admin_scene_preview preview;
    bool previewReady = false;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(selectedSceneUtf8, 0, sizeof(selectedSceneUtf8));
    memset(selectedSceneFile, 0, sizeof(selectedSceneFile));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(encodedScene, 0, sizeof(encodedScene));
    memset(mapName, 0, sizeof(mapName));
    memset(mapNameUtf8, 0, sizeof(mapNameUtf8));
    memset(&preview, 0, sizeof(preview));
    (void)vm_mock_admin_form_value(query, "scene", selectedSceneUtf8,
                                   sizeof(selectedSceneUtf8));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    if (selectedSceneUtf8[0] != 0 &&
        vm_mock_admin_utf8_to_gbk_text(selectedSceneUtf8, selectedSceneFile,
                                       sizeof(selectedSceneFile), false) &&
        vm_mock_admin_scene_file_to_runtime_key(selectedSceneFile, runtimeScene,
                                                sizeof(runtimeScene)))
    {
        previewReady = vm_mock_admin_scene_map_name(runtimeScene, mapName,
                                                    sizeof(mapName)) &&
                       vm_mock_admin_scene_preview_info(runtimeScene, &preview);
        vm_net_mock_gbk_label_to_utf8(mapName, mapNameUtf8,
                                      sizeof(mapNameUtf8));
    }
    vm_mock_admin_url_encode(selectedSceneUtf8, encodedScene,
                             sizeof(encodedScene));

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 地图编辑</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}"
        "body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{height:100vh;margin:0 auto;padding:16px 18px;display:flex;flex-direction:column;gap:10px;max-width:1600px}"
        "header{display:flex;justify-content:space-between;gap:12px;align-items:flex-start}"
        "h1{font-size:22px;margin:0}.sub{color:#667085;margin:4px 0 0}"
        ".tabs{display:flex;gap:6px;flex-wrap:wrap}.tab{padding:8px 12px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}"
        ".notice{padding:10px 12px;border-radius:7px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.warn{background:#fffaeb;color:#7a2e0e}"
        ".toolbar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:10px}"
        ".toolbar label{display:flex;align-items:center;gap:4px;font-size:13px}"
        ".toolbar button,.toolbar .button{border:0;border-radius:6px;padding:7px 11px;background:#175cd3;color:#fff;cursor:pointer}"
        ".toolbar button.secondary{background:#f2f4f7;color:#344054}.toolbar button.on{outline:2px solid #98a2b3}"
        ".toolbar button.danger{background:#b42318}.meta{color:#667085;font-size:12px}"
        ".layout{display:grid;grid-template-columns:340px minmax(0,1fr);gap:12px;flex:1;min-height:0}"
        ".panel{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:10px;min-height:0;overflow:auto}"
        ".palette-sheet{margin-bottom:12px}.palette-sheet h3{margin:0;font-size:13px;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis}"
        ".palette-sheet-head{display:flex;align-items:center;gap:6px;margin:0 0 6px}"
        ".palette-sheet-head button{border:0;border-radius:4px;padding:2px 7px;background:#f2f4f7;color:#344054;cursor:pointer;font-size:12px}"
        ".palette-sheet-head button.danger{background:#fef3f2;color:#b42318}"
        ".sheet-tools{display:grid;gap:8px;margin:0 0 12px;padding-bottom:10px;border-bottom:1px solid #eaecf0}"
        ".sheet-tools .title{font-size:13px;font-weight:600;color:#344054}"
        ".sheet-tools input[type=search]{width:100%%;border:1px solid #d0d5dd;border-radius:6px;padding:7px 9px}"
        ".sheet-preview{display:none;border:1px solid #e4e7ec;border-radius:8px;padding:8px;background:#f9fafb}"
        ".sheet-preview.on{display:grid;gap:8px}"
        ".sheet-preview .preview-head{display:flex;justify-content:space-between;gap:8px;align-items:center}"
        ".sheet-preview .preview-name{font-size:12px;color:#344054;word-break:break-all}"
        ".sheet-preview img{max-width:100%%;max-height:220px;image-rendering:pixelated;background:#111;border-radius:4px}"
        ".sheet-preview button,.sheet-card button{border:0;border-radius:6px;padding:6px 10px;background:#175cd3;color:#fff;cursor:pointer;font-size:12px}"
        ".sheet-preview button:disabled,.sheet-card button:disabled{opacity:.45;cursor:not-allowed}"
        ".sheet-catalog{display:grid;gap:6px;max-height:420px;overflow:auto;padding-right:2px}"
        ".sheet-card{display:grid;grid-template-columns:72px 1fr auto;gap:8px;align-items:center;border:1px solid #e4e7ec;border-radius:8px;padding:6px;background:#fff;cursor:pointer}"
        ".sheet-card:hover{border-color:#98a2b3}.sheet-card.on{border-color:#175cd3;background:#eef4ff}"
        ".sheet-card .thumb{width:72px;height:48px;object-fit:contain;image-rendering:pixelated;background:#111;border-radius:4px}"
        ".sheet-card .thumb.missing{display:grid;place-items:center;color:#98a2b3;font-size:11px}"
        ".sheet-card .name{font-size:12px;color:#344054;word-break:break-all}"
        ".palette-grid{display:grid;gap:2px;width:max-content}"
        ".palette-grid canvas{image-rendering:pixelated;cursor:pointer;border:1px solid #d0d5dd}"
        ".palette-grid canvas.on{outline:2px solid #175cd3}"
        ".map-wrap{overflow:auto;background:#111;border-radius:8px;padding:8px;min-height:0}"
        "#map-canvas{image-rendering:pixelated;cursor:crosshair;display:block;background:#222}"
        ".logout{background:none;color:#667085;border:1px solid #d0d5dd;border-radius:6px;padding:8px 12px;cursor:pointer}"
        "@media(max-width:900px){.layout{grid-template-columns:1fr}}"
        "</style><script src=\"/admin.js?v=11\" defer></script></head><body>"
        "<main class=\"wrap\" id=\"map-editor\" data-scene=\"");
    vm_mock_admin_text_append_html(
        &page, selectedSceneUtf8[0] ? selectedSceneUtf8 : "");
    vm_mock_admin_text_appendf(
        &page,
        "\"><header><div><h1>地图编辑器</h1><p class=\"sub\">");
    vm_mock_admin_text_append_html(&page, selectedSceneUtf8[0] ? selectedSceneUtf8 : "未选择场景");
    vm_mock_admin_text_appendf(&page, " · ");
    vm_mock_admin_text_append_html(&page, mapNameUtf8[0] ? mapNameUtf8 : "-");
    if (previewReady)
    {
        vm_mock_admin_text_appendf(
            &page, " · %u×%u · tile %u×%u · %u sheets</p></div>",
            preview.width, preview.height, preview.tileWidth, preview.tileHeight,
            preview.imageCount);
    }
    else
    {
        vm_mock_admin_text_appendf(&page, "</p></div>");
    }
    vm_mock_admin_text_appendf(
        &page,
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_append_tab_styles(&page);
    vm_mock_admin_text_appendf(
        &page,
        "<nav class=\"admin-tabs\"><a class=\"admin-tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"admin-tab\" href=\"/?tab=content&amp;scene=%s\">游戏内容管理</a>"
        "<a class=\"admin-tab on\" href=\"/?tab=mapedit&amp;scene=%s\">地图编辑</a></nav>",
        encodedScene, encodedScene);
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (!previewReady)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"notice error\">无法加载场景地图。请从「游戏内容管理」选择有效场景后再打开编辑器。</div>"
            "</main></body></html>");
        return;
    }
    vm_mock_admin_text_appendf(
        &page,
        "<div id=\"map-shared-warn\" class=\"notice warn\" hidden></div>"
        "<div class=\"toolbar\">"
        "<button type=\"button\" data-tool=\"paint\" class=\"on\">画砖</button>"
        "<button type=\"button\" data-tool=\"rect\" class=\"secondary\">矩形填</button>"
        "<button type=\"button\" data-tool=\"eyedrop\" class=\"secondary\">吸管</button>"
        "<button type=\"button\" data-tool=\"collision\" class=\"secondary\">碰撞</button>"
        "<label>碰撞值 <select id=\"map-collision-mode\">"
        "<option value=\"0\">开放 0x0</option>"
        "<option value=\"15\">实体 0xF</option>"
        "<option value=\"1\">北 0x1</option>"
        "<option value=\"2\">西 0x2</option>"
        "<option value=\"4\">南 0x4</option>"
        "<option value=\"8\">东 0x8</option>"
        "</select></label>"
        "<label><input type=\"checkbox\" id=\"map-show-collision\" checked> 显示碰撞</label>"
        "<button type=\"button\" id=\"map-save\" class=\"danger\">保存并发布</button>"
        "<a class=\"button secondary\" href=\"/?tab=content&amp;scene=%s\">返回内容页</a>"
        "<span class=\"meta\" id=\"map-cursor-meta\">-</span></div>"
        "<div class=\"layout\"><aside class=\"panel\">"
        "<div class=\"sheet-tools\">"
        "<div class=\"title\">浏览全部 GIF 资源</div>"
        "<input type=\"search\" id=\"map-sheet-filter\" placeholder=\"筛选文件名，如 level_name / riches_name / m_castle…\" autocomplete=\"off\">"
        "<div class=\"sheet-preview\" id=\"map-sheet-preview\">"
        "<div class=\"preview-head\"><span class=\"preview-name\" id=\"map-sheet-preview-name\"></span>"
        "<button type=\"button\" id=\"map-sheet-add\">添加此图集</button></div>"
        "<img id=\"map-sheet-preview-img\" alt=\"GIF 预览\"></div>"
        "<div class=\"sheet-catalog\" id=\"map-sheet-catalog\"></div>"
        "<div class=\"meta\" id=\"map-sheet-meta\">图集 0 / 16</div></div>"
        "<div id=\"map-palette\"></div></aside>"
        "<section class=\"panel\"><div class=\"map-wrap\"><canvas id=\"map-canvas\" width=\"%u\" height=\"%u\"></canvas></div></section></div>"
        "</main></body></html>",
        encodedScene, preview.width, preview.height);
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
                                        const char *mall,
                                        const char *npc,
                                        u32 page,
                                        const char *status,
                                        const char *message)
{
    char categoryEncoded[64];
    char searchEncoded[384];
    char mallEncoded[16];
    char npcEncoded[16];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1600];

    vm_mock_admin_url_encode(category && category[0] ? category : "all",
                             categoryEncoded, sizeof(categoryEncoded));
    vm_mock_admin_url_encode(search ? search : "", searchEncoded,
                             sizeof(searchEncoded));
    vm_mock_admin_url_encode(mall && mall[0] ? mall : "all", mallEncoded,
                             sizeof(mallEncoded));
    vm_mock_admin_url_encode(npc && npc[0] ? npc : "all", npcEncoded,
                             sizeof(npcEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=shop&category=%s&mall=%s&npc=%s&q=%s&page=%u&status=%s&message=%s",
             categoryEncoded, mallEncoded, npcEncoded, searchEncoded,
             page ? page : 1, statusEncoded, messageEncoded);
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

static void vm_mock_admin_redirect_monster(vm_mock_service_socket client,
                                           u32 monsterId,
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
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=monsters&monster=%u&status=%s&message=%s",
             monsterId, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_servers(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=servers&status=%s&message=%s",
             statusEncoded, messageEncoded);
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

/*
 * Parse a drop-rate percent field (e.g. "0.1", "1", "1.5", "100") into
 * percent×10 storage (1=0.1%, 1000=100%).  At most one decimal digit.
 */
static bool vm_mock_admin_parse_drop_rate_percent_x10(const char *text,
                                                      u32 *x10Out)
{
    const char *cursor = text;
    unsigned long whole = 0;
    unsigned long tenths = 0;
    bool hasDot = false;

    if (x10Out)
        *x10Out = 0;
    if (text == NULL || text[0] == 0)
        return false;
    while (*cursor >= '0' && *cursor <= '9')
    {
        whole = whole * 10ul + (unsigned long)(*cursor - '0');
        if (whole > 100ul)
            return false;
        ++cursor;
    }
    if (*cursor == '.')
    {
        hasDot = true;
        ++cursor;
        if (*cursor < '0' || *cursor > '9')
            return false;
        tenths = (unsigned long)(*cursor - '0');
        ++cursor;
        if (*cursor >= '0' && *cursor <= '9')
            return false;
    }
    if (*cursor != 0)
        return false;
    if (whole > 100ul || (whole == 100ul && (hasDot && tenths != 0)))
        return false;
    if (x10Out)
        *x10Out = (u32)(whole * 10ul + tenths);
    return true;
}

static bool vm_mock_admin_form_drop_rate_x10(const char *body, const char *field,
                                             u32 *x10Out)
{
    char textValue[32];
    u32 parsed = 0;

    memset(textValue, 0, sizeof(textValue));
    if (!vm_mock_admin_form_value(body, field, textValue, sizeof(textValue)) ||
        !vm_mock_admin_parse_drop_rate_percent_x10(textValue, &parsed) ||
        parsed > VM_NET_MOCK_MONSTER_DROP_RATE_X10_MAX)
    {
        return false;
    }
    if (x10Out)
        *x10Out = parsed;
    return true;
}

/**
 * 解析可选 u32 表单字段；缺省或空串时使用 defaultValue。
 * @return 解析成功或采用默认值时为 true；字段存在但非法时为 false
 */
static bool vm_mock_admin_form_u32_or_default(const char *body, const char *field,
                                              u32 maximum, u32 defaultValue,
                                              u32 *valueOut)
{
    char textValue[32];
    u32 parsed = 0;

    memset(textValue, 0, sizeof(textValue));
    if (!vm_mock_admin_form_value(body, field, textValue, sizeof(textValue)) ||
        textValue[0] == 0)
    {
        if (valueOut)
            *valueOut = defaultValue;
        return true;
    }
    if (!vm_net_mock_parse_u32_strict(textValue, &parsed) || parsed > maximum)
        return false;
    if (valueOut)
        *valueOut = parsed;
    return true;
}

#include "web_admin_chests.inc.c"
#include "web_admin_catalog.inc.c"
#include "web_admin_enhance.inc.c"
#include "web_admin_gem.inc.c"
#include "web_admin_client_ui.inc.c"
#include "web_admin_combat.inc.c"
#include "web_admin_arena.inc.c"
#include "web_admin_world_boss.inc.c"
#include "web_admin_maintenance.inc.c"
#include "web_admin_ladder.inc.c"
#include "web_admin_fruit.inc.c"
#include "web_admin_guess.inc.c"
#include "web_admin_auction.inc.c"
#include "web_zangbao.inc.c"
#include "web_admin_zangbao.inc.c"
#include "web_admin_activities.inc.c"
#include "web_admin_gateways.inc.c"

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
    if ((task.givenItemId != 0 &&
         vm_net_mock_find_shop_catalog_item(task.givenItemId) == NULL) ||
        (task.rewardItemId != 0 &&
         vm_net_mock_find_shop_catalog_item(task.rewardItemId) == NULL) ||
        (task.requirementType1 == 1 && task.requirementId1 != 0 &&
         vm_net_mock_find_shop_catalog_item(task.requirementId1) == NULL) ||
        (task.requirementType2 == 1 && task.requirementId2 != 0 &&
         vm_net_mock_find_shop_catalog_item(task.requirementId2) == NULL))
    {
        vm_mock_admin_redirect_tasks(
            client, taskId, "error",
            "任务中的物品 ID 不在物品目录中");
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

static void vm_mock_admin_handle_monster_global_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    vm_net_mock_monster_global_config cfg;

    memset(&cfg, 0, sizeof(cfg));
    if (strcmp(action, "save-monster-global-config") != 0)
    {
        vm_mock_admin_redirect_monster_global(client, "error", "未知操作");
        return;
    }
    if (!vm_mock_admin_form_u32(body, "hp_percent", 1000u, &cfg.hpPercent) ||
        cfg.hpPercent == 0 ||
        !vm_mock_admin_form_u32(body, "attack_percent", 1000u,
                                &cfg.attackPercent) ||
        cfg.attackPercent == 0 ||
        !vm_mock_admin_form_u32(body, "defense_percent", 1000u,
                                &cfg.defensePercent) ||
        !vm_mock_admin_form_u32(body, "drop_rate_percent", 1000u,
                                &cfg.dropRatePercent) ||
        !vm_mock_admin_form_u32(body, "reward_exp_percent", 1000u,
                                &cfg.rewardExpPercent) ||
        !vm_mock_admin_form_u32(body, "reward_money_percent", 1000u,
                                &cfg.rewardMoneyPercent))
    {
        vm_mock_admin_redirect_monster_global(
            client, "error",
            "百分比须为整数：血量/攻击 1–1000，其余 0–1000");
        return;
    }
    {
        char extraX3[8];

        memset(extraX3, 0, sizeof(extraX3));
        cfg.hpExtraX3WhenMultGe6 =
            vm_mock_admin_form_value(body, "hp_extra_x3_when_mult_ge6", extraX3,
                                     sizeof(extraX3)) &&
                    strcmp(extraX3, "1") == 0
                ? 1u
                : 0u;
    }
    if (vm_net_mock_monster_global_config_admin_save(&cfg))
        vm_mock_admin_redirect_monster_global(client, "ok",
                                              "全服怪物百分比已保存并立即生效");
    else
        vm_mock_admin_redirect_monster_global(client, "error",
                                              "保存全服怪物百分比失败");
}

static void vm_mock_admin_handle_monster_action(vm_mock_service_socket client,
                                                 const char *action,
                                                 const char *body)
{
    vm_net_mock_monster_admin_row row;
    char displayUtf8[128];
    char sourceUtf8[192];
    const char *error = NULL;
    u32 family = 0;
    bool creating = strcmp(action, "create-monster") == 0;

    memset(&row, 0, sizeof(row));
    memset(displayUtf8, 0, sizeof(displayUtf8));
    memset(sourceUtf8, 0, sizeof(sourceUtf8));
    if (!vm_mock_admin_form_u32(body, "monster_id", 0xffffu,
                                &row.enemyId) || row.enemyId == 0)
    {
        vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                       "怪物 ID 无效");
        return;
    }
    if (strcmp(action, "reset-monster") == 0)
    {
        if (!vm_net_mock_monster_admin_reset(row.enemyId, &error))
        {
            vm_mock_admin_redirect_monster(
                client, row.enemyId, "error",
                error ? error : "恢复怪物默认属性失败");
            return;
        }
        vm_mock_admin_redirect_monster(client, row.enemyId, "ok",
                                       "已恢复服务端默认怪物属性");
        return;
    }
    if (strcmp(action, "delete-monster") == 0)
    {
        if (!vm_net_mock_monster_admin_delete_custom(row.enemyId, &error))
        {
            vm_mock_admin_redirect_monster(
                client, row.enemyId, "error",
                error ? error : "删除自定义怪物失败");
            return;
        }
        vm_mock_admin_redirect_monster(client, 0, "ok",
                                       "自定义怪物已删除");
        return;
    }
    if ((!creating && strcmp(action, "save-monster") != 0) ||
        !vm_mock_admin_form_u32(body, "level", 0xffu, &row.level) ||
        !vm_mock_admin_form_u32(body, "family", VM_NET_MOCK_MONSTER_BOSS,
                                &family) ||
        !vm_mock_admin_form_u32(body, "hp", VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX,
                                &row.hp) ||
        !vm_mock_admin_form_u32(body, "mp", VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX,
                                &row.mp) ||
        !vm_mock_admin_form_u32(body, "attack", VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX,
                                &row.attack) ||
        !vm_mock_admin_form_u32(body, "defense", VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX,
                                &row.defense) ||
        !vm_mock_admin_form_u32(body, "exp", VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX,
                                &row.exp) ||
        !vm_mock_admin_form_u32(body, "gold", VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX,
                                &row.gold))
    {
        vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                       "怪物表单字段不完整或数值越界");
        return;
    }
    row.family = (u8)family;
    {
        char castSkillText[8];

        memset(castSkillText, 0, sizeof(castSkillText));
        row.castSkill =
            vm_mock_admin_form_value(body, "cast_skill", castSkillText,
                                     sizeof(castSkillText)) &&
                    strcmp(castSkillText, "1") == 0
                ? 1u
                : 0u;
    }
    if (!vm_mock_admin_form_value(body, "actor_resource", row.actorResource,
                                  sizeof(row.actorResource)) ||
        row.actorResource[0] == 0 ||
        !vm_net_mock_str_ends_with(row.actorResource, ".actor"))
    {
        vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                       "请选择有效的战斗 Actor 资源");
        return;
    }
    if (creating)
    {
        if (!vm_mock_admin_form_value(body, "display_name", displayUtf8,
                                      sizeof(displayUtf8)) ||
            displayUtf8[0] == 0 ||
            !vm_mock_admin_utf8_to_gbk_text(displayUtf8, row.displayName,
                                            sizeof(row.displayName), false))
        {
            vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                           "显示名称无效或编码失败");
            return;
        }
        (void)vm_mock_admin_form_value(body, "source_label", sourceUtf8,
                                       sizeof(sourceUtf8));
        if (sourceUtf8[0] == 0)
            snprintf(sourceUtf8, sizeof(sourceUtf8), "%s", "自定义怪物");
        if (!vm_mock_admin_utf8_to_gbk_text(sourceUtf8, row.firstScene,
                                            sizeof(row.firstScene), false))
        {
            vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                           "来源标签无效或编码失败");
            return;
        }
    }
    else if (!vm_mock_admin_form_value(body, "display_name", displayUtf8,
                                       sizeof(displayUtf8)) ||
             displayUtf8[0] == 0 ||
             !vm_mock_admin_utf8_to_gbk_text(displayUtf8, row.displayName,
                                             sizeof(row.displayName), false))
    {
        vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                       "显示名称无效或编码失败");
        return;
    }
    for (u8 slot = 0; slot < VM_NET_MOCK_MONSTER_DROP_MAX; ++slot)
    {
        char itemField[48];
        char rateField[48];
        u32 itemId = 0;
        u32 rate = 0;

        snprintf(itemField, sizeof(itemField), "drop_item_id_%u", (u32)slot);
        snprintf(rateField, sizeof(rateField), "drop_rate_%u", (u32)slot);
        if (!vm_mock_admin_form_u32(body, itemField, 0xffffffffu, &itemId) ||
            !vm_mock_admin_form_drop_rate_x10(body, rateField, &rate))
        {
            vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                           "掉落表单字段不完整或数值越界");
            return;
        }
        if (itemId == 0 && rate == 0)
            continue;
        if (itemId == 0 || rate == 0 ||
            row.dropCount >= VM_NET_MOCK_MONSTER_DROP_MAX)
        {
            vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                           "每条掉落都必须同时选择物品和概率");
            return;
        }
        row.drops[row.dropCount].itemId = itemId;
        row.drops[row.dropCount].ratePercent = (u16)rate;
        ++row.dropCount;
    }
    if (creating)
    {
        if (!vm_net_mock_monster_admin_create(&row, &error))
        {
            vm_mock_admin_redirect_monster(
                client, row.enemyId, "error",
                error ? error : "新增怪物失败");
            return;
        }
        vm_mock_admin_redirect_monster(client, row.enemyId, "ok",
                                       "自定义怪物已创建并立即生效");
        return;
    }
    if (!vm_net_mock_monster_admin_save(&row, &error))
    {
        vm_mock_admin_redirect_monster(
            client, row.enemyId, "error",
            error ? error : "怪物属性保存失败");
        return;
    }
    vm_mock_admin_redirect_monster(client, row.enemyId, "ok",
                                   "怪物属性已保存并立即生效");
}

static bool vm_mock_admin_scene_from_form(const char *body,
                                          char *sceneUtf8,
                                          size_t sceneUtf8Cap,
                                          char *runtimeScene,
                                          size_t runtimeSceneCap)
{
    vm_mock_admin_scene_file *files = NULL;
    char sceneFile[64];
    u32 fileCount = 0;
    bool exists = false;
    bool ok = false;

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
    files = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_SCENE_FILE_MAX, sizeof(*files));
    if (files == NULL)
        return false;
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
    ok = exists && vm_mock_admin_scene_file_to_runtime_key(
                       sceneFile, runtimeScene, runtimeSceneCap);
    free(files);
    return ok;
}

static bool vm_mock_admin_optional_scene_from_form(
    const char *body,
    const char *fieldName,
    char *sceneUtf8,
    size_t sceneUtf8Cap,
    char *runtimeScene,
    size_t runtimeSceneCap)
{
    vm_mock_admin_scene_file *files = NULL;
    char sceneFile[64];
    u32 fileCount = 0;
    bool ok = false;

    if (fieldName == NULL || sceneUtf8 == NULL || sceneUtf8Cap == 0 ||
        runtimeScene == NULL || runtimeSceneCap == 0)
    {
        return false;
    }
    sceneUtf8[0] = 0;
    runtimeScene[0] = 0;
    memset(sceneFile, 0, sizeof(sceneFile));
    if (!vm_mock_admin_form_value(body, fieldName, sceneUtf8, sceneUtf8Cap))
        return false;
    if (sceneUtf8[0] == 0)
        return true;
    if (!vm_mock_admin_utf8_to_gbk_text(sceneUtf8, sceneFile,
                                        sizeof(sceneFile), false))
    {
        return false;
    }
    files = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_SCENE_FILE_MAX, sizeof(*files));
    if (files == NULL)
        return false;
    fileCount = vm_mock_admin_collect_scene_files(
        files, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    for (u32 i = 0; i < fileCount; ++i)
    {
        if (strcmp(files[i].name, sceneFile) == 0)
        {
            ok = vm_mock_admin_scene_file_to_runtime_key(
                sceneFile, runtimeScene, runtimeSceneCap);
            break;
        }
    }
    free(files);
    return ok;
}

static bool vm_mock_admin_path_replace_leaf(const char *fullPath,
                                            const char *newLeaf,
                                            char *out,
                                            size_t outCap)
{
    const char *slash = NULL;
    const char *bslash = NULL;
    const char *sep = NULL;
    size_t dirLen = 0;

    if (fullPath == NULL || newLeaf == NULL || out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    slash = strrchr(fullPath, '/');
    bslash = strrchr(fullPath, '\\');
    sep = slash;
    if (bslash != NULL && (sep == NULL || bslash > sep))
        sep = bslash;
    if (sep == NULL)
        return snprintf(out, outCap, "%s", newLeaf) < (int)outCap;
    dirLen = (size_t)(sep - fullPath + 1);
    if (dirLen + strlen(newLeaf) + 1 > outCap)
        return false;
    memcpy(out, fullPath, dirLen);
    out[dirLen] = 0;
    return snprintf(out + dirLen, outCap - dirLen, "%s", newLeaf) <
           (int)(outCap - dirLen);
}

static bool vm_mock_admin_replace_extension(const char *name,
                                            const char *newExt,
                                            char *out,
                                            size_t outCap)
{
    char stem[128];
    size_t len = 0;

    if (name == NULL || newExt == NULL || out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    len = strlen(name);
    if (len == 0 || len >= sizeof(stem))
        return false;
    snprintf(stem, sizeof(stem), "%s", name);
    if (len > 4 && stem[len - 4] == '.')
        stem[len - 4] = 0;
    return snprintf(out, outCap, "%s%s", stem, newExt) < (int)outCap;
}

static bool vm_mock_admin_copy_file_binary(const char *srcPath,
                                           const char *dstPath,
                                           const char **errorOut)
{
    FILE *in = NULL;
    FILE *out = NULL;
    u8 buffer[8192];
    size_t n = 0;
    bool ok = false;

    if (errorOut)
        *errorOut = "文件复制失败";
    if (srcPath == NULL || dstPath == NULL || srcPath[0] == 0 || dstPath[0] == 0)
        return false;
    in = vm_net_mock_fopen_game_path(srcPath, "rb");
    if (in == NULL)
    {
        if (errorOut)
            *errorOut = "源场景文件不存在";
        return false;
    }
    out = vm_net_mock_fopen_game_path(dstPath, "wb");
    if (out == NULL)
    {
        fclose(in);
        if (errorOut)
            *errorOut = "无法创建目标场景文件";
        return false;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if (fwrite(buffer, 1, n, out) != n)
        {
            if (errorOut)
                *errorOut = "写入目标场景文件失败";
            goto done;
        }
    }
    if (ferror(in))
    {
        if (errorOut)
            *errorOut = "读取源场景文件失败";
        goto done;
    }
    ok = true;
done:
    fclose(in);
    fclose(out);
    if (!ok)
        (void)remove(dstPath);
    return ok;
}

static void vm_mock_admin_handle_scene_monster_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    char sceneUtf8[192];
    char runtimeScene[64];
    const char *error = NULL;
    u32 monsterId1 = 0;
    u32 monsterId2 = 0;
    u32 monsterId3 = 0;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "场景无效");
        return;
    }
    if (strcmp(action, "reset-scene-monsters") == 0)
    {
        if (!vm_net_mock_scene_monster_admin_reset(runtimeScene, &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "恢复场景默认刷怪失败");
            return;
        }
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       "已恢复 automonster.dsh 默认刷怪");
        return;
    }
    if (strcmp(action, "save-scene-monsters") != 0 ||
        !vm_mock_admin_form_u32(body, "monster_id_1", 0xffffu, &monsterId1) ||
        !vm_mock_admin_form_u32(body, "monster_id_2", 0xffffu, &monsterId2) ||
        !vm_mock_admin_form_u32(body, "monster_id_3", 0xffffu, &monsterId3))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "场景刷怪表单字段无效");
        return;
    }
    if (!vm_net_mock_scene_monster_admin_save(runtimeScene, monsterId1,
                                              monsterId2, monsterId3, &error))
    {
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "error",
            error ? error : "场景刷怪配置保存失败");
        return;
    }
    vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                   "场景刷怪配置已保存并立即生效");
}

static void vm_mock_admin_handle_scene_display_name_action(
    vm_mock_service_socket client, const char *body)
{
    char sceneUtf8[192];
    char runtimeScene[64];
    char displayUtf8[96];
    char displayGbk[32];
    const char *error = NULL;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(displayUtf8, 0, sizeof(displayUtf8));
    memset(displayGbk, 0, sizeof(displayGbk));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error", "场景无效");
        return;
    }
    if (!vm_mock_admin_form_value(body, "display_name", displayUtf8,
                                  sizeof(displayUtf8)) ||
        displayUtf8[0] == 0 ||
        !vm_mock_admin_utf8_to_gbk_text(displayUtf8, displayGbk,
                                        sizeof(displayGbk), false))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "显示名称无效或编码失败");
        return;
    }
    if (!vm_net_mock_scene_display_name_admin_save(runtimeScene, displayGbk,
                                                    &error))
    {
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "error",
            error ? error : "地图显示名称保存失败");
        return;
    }
    vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                   "地图显示名称已保存");
}

static void vm_mock_admin_handle_instance_enter_rule_action(
    vm_mock_service_socket client, const char *body)
{
    char sceneUtf8[192];
    char runtimeScene[64];
    u32 dailyLimit = 0;
    u32 challengeDailyLimit = 0;
    u32 goldCost = 0;
    u32 kubaoCost = 0;
    const char *error = NULL;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error", "场景无效");
        return;
    }
    if (!vm_mock_admin_form_u32(body, "daily_limit", 9999u, &dailyLimit) ||
        !vm_mock_admin_form_u32(body, "challenge_daily_limit", 9999u,
                                &challengeDailyLimit) ||
        !vm_mock_admin_form_u32(body, "gold_cost", 999999u, &goldCost) ||
        !vm_mock_admin_form_u32(body, "kubao_cost", 999999u, &kubaoCost))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "进入规则表单字段无效");
        return;
    }
    if (!vm_net_mock_instance_enter_rule_admin_save(
            runtimeScene, dailyLimit, challengeDailyLimit, goldCost, kubaoCost,
            &error))
    {
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "error",
            error ? error : "进入规则保存失败");
        return;
    }
    vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                   "每日进入／挑战规则已保存");
}

static void vm_mock_admin_offset_map_xy(u16 *xInOut, u16 *yInOut, u32 mapW,
                                         u32 mapH)
{
    u32 x;
    u32 y;
    u32 origX;
    u32 origY;

    if (xInOut == NULL || yInOut == NULL)
        return;
    origX = *xInOut;
    origY = *yInOut;
    if (mapW == 0)
        mapW = 65535u;
    if (mapH == 0)
        mapH = 65535u;
    x = origX + 32u;
    y = origY + 32u;
    if (x < 1u)
        x = 1u;
    if (y < 1u)
        y = 1u;
    if (x > mapW)
        x = mapW;
    if (y > mapH)
        y = mapH;
    if (x == origX && y == origY)
    {
        x = origX > 32u ? origX - 32u : 1u;
        y = origY > 32u ? origY - 32u : 1u;
    }
    *xInOut = (u16)x;
    *yInOut = (u16)y;
}

static bool vm_mock_admin_npc_actor_id_taken(const char *runtimeScene,
                                             u32 actorId)
{
    vm_net_mock_scene_npcinfo_seed *seeds = NULL;
    u32 seedCount = 0;
    u32 total = 0;
    u32 dynamicCount = 0;
    bool taken = false;

    if (actorId == 0)
        return true;
    (void)vm_net_mock_dynamic_npc_db_load();
    for (u32 i = 0; i < g_vm_net_mock_dynamic_npc_override_count; ++i)
    {
        if (g_vm_net_mock_dynamic_npc_overrides[i].seed.actorId == actorId)
            return true;
    }
    if (runtimeScene == NULL || runtimeScene[0] == 0)
        return false;
    seeds = (vm_net_mock_scene_npcinfo_seed *)calloc(
        VM_NET_MOCK_SCENE_NPC_CATALOG_MAX, sizeof(*seeds));
    if (seeds == NULL)
        return true;
    seedCount = vm_net_mock_collect_scene_npcinfo_seeds(
        runtimeScene, seeds, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX, &total,
        &dynamicCount);
    for (u32 i = 0; i < seedCount; ++i)
    {
        if (seeds[i].actorId == actorId)
        {
            taken = true;
            break;
        }
    }
    free(seeds);
    return taken;
}

static u32 vm_mock_admin_alloc_free_npc_actor_id(const char *runtimeScene)
{
    u32 candidate = 30000u;

    while (candidate <= VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)
    {
        if (!vm_mock_admin_npc_actor_id_taken(runtimeScene, candidate))
            return candidate;
        ++candidate;
    }
    return 0;
}

static bool vm_mock_admin_find_npc_seed_for_copy(
    const char *runtimeScene, u32 actorId,
    vm_net_mock_scene_npcinfo_seed *seedOut)
{
    vm_net_mock_dynamic_npc_admin_row *rows = NULL;
    vm_net_mock_scene_npcinfo_seed *seeds = NULL;
    u32 rowCount = 0;
    u32 seedCount = 0;
    u32 total = 0;
    u32 dynamicCount = 0;
    bool found = false;

    if (runtimeScene == NULL || actorId == 0 || seedOut == NULL)
        return false;
    memset(seedOut, 0, sizeof(*seedOut));
    rows = (vm_net_mock_dynamic_npc_admin_row *)calloc(
        VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX, sizeof(*rows));
    if (rows != NULL)
    {
        rowCount = vm_net_mock_dynamic_npc_admin_list(
            runtimeScene, rows, VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX);
        for (u32 i = 0; i < rowCount; ++i)
        {
            if (rows[i].seed.actorId == actorId)
            {
                *seedOut = rows[i].seed;
                found = true;
                break;
            }
        }
        free(rows);
    }
    if (found)
        return true;
    seeds = (vm_net_mock_scene_npcinfo_seed *)calloc(
        VM_NET_MOCK_SCENE_NPC_CATALOG_MAX, sizeof(*seeds));
    if (seeds == NULL)
        return false;
    seedCount = vm_net_mock_collect_scene_npcinfo_seeds(
        runtimeScene, seeds, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX, &total,
        &dynamicCount);
    for (u32 i = 0; i < seedCount; ++i)
    {
        if (seeds[i].actorId == actorId)
        {
            *seedOut = seeds[i];
            found = true;
            break;
        }
    }
    free(seeds);
    return found;
}

static void vm_mock_admin_handle_scene_combat_spawn_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    char sceneUtf8[192];
    char runtimeScene[64];
    char key[32];
    vm_net_mock_sce_combat_spawn spawns[VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX];
    const char *error = NULL;
    u32 spawnCount = 0;
    u32 formCount = 0;
    u32 saved = 0;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(spawns, 0, sizeof(spawns));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error", "场景无效");
        return;
    }

    if (strcmp(action, "save-combat-spawn-position") == 0)
    {
        vm_net_mock_sce_combat_spawn_located *located = NULL;
        u32 index = 0;
        u32 x = 0;
        u32 y = 0;
        u32 total = 0;
        u32 count = 0;

        if (!vm_mock_admin_form_u32(body, "spawn_index",
                                    VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX - 1u,
                                    &index) ||
            !vm_mock_admin_form_u32(body, "x", 0xffffu, &x) || x == 0 ||
            !vm_mock_admin_form_u32(body, "y", 0xffffu, &y) || y == 0)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "刷怪点坐标无效");
            return;
        }
        located = (vm_net_mock_sce_combat_spawn_located *)calloc(
            VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX, sizeof(*located));
        if (located == NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "内存不足，无法保存刷怪点坐标");
            return;
        }
        count = vm_net_mock_sce_combat_spawn_locate_all(
            runtimeScene, located, VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX, &total);
        if (index >= count)
        {
            free(located);
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "刷怪点不存在，无法调整坐标");
            return;
        }
        for (u32 i = 0; i < count; ++i)
            spawns[i] = located[i].spawn;
        /*
         * locate_all remaps SCE keep-id wire (#250) → catalog real for admin.
         * If remap missed, actorId may still be #250 which is often absent from
         * the monster catalog — fill_from_catalog then fails and the pin snaps
         * back (看起来像「不能拖动」).  Refuse with a clear tip instead.
         */
        for (u32 i = 0; i < count; ++i)
        {
            if (spawns[i].actorId == 0 ||
                !vm_net_mock_monster_enemy_id_known(spawns[i].actorId))
            {
                free(located);
                vm_mock_admin_redirect_content(
                    client, sceneUtf8, "error",
                    "刷怪点真实怪物 ID 无法解析，请先在表单中重新保存怪物 ID");
                return;
            }
        }
        spawns[index].x = (u16)x;
        spawns[index].y = (u16)y;
        free(located);
        if (!vm_net_mock_sce_combat_spawn_admin_save_all(
                runtimeScene, spawns, count, 0, 0, false, &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "刷怪点坐标保存失败");
            return;
        }
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "ok",
            "刷怪点坐标已保存并发布；客户端需重新下载场景");
        return;
    }

    if (strcmp(action, "copy-combat-spawn") == 0)
    {
        vm_net_mock_sce_combat_spawn_located *located = NULL;
        vm_mock_admin_scene_preview preview;
        u32 index = 0;
        u32 total = 0;
        u32 count = 0;
        u16 copyX = 0;
        u16 copyY = 0;
        char okMsg[128];

        memset(&preview, 0, sizeof(preview));
        if (!vm_mock_admin_form_u32(body, "spawn_index",
                                    VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX - 1u,
                                    &index))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "刷怪点索引无效");
            return;
        }
        located = (vm_net_mock_sce_combat_spawn_located *)calloc(
            VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX, sizeof(*located));
        if (located == NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "内存不足，无法复制刷怪点");
            return;
        }
        count = vm_net_mock_sce_combat_spawn_locate_all(
            runtimeScene, located, VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX, &total);
        if (index >= count)
        {
            free(located);
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "刷怪点不存在，无法复制");
            return;
        }
        if (count >= VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX)
        {
            free(located);
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                "刷怪点数量已达上限，无法继续复制");
            return;
        }
        (void)vm_mock_admin_scene_preview_info(runtimeScene, &preview);
        for (u32 i = 0; i < count; ++i)
            spawns[i] = located[i].spawn;
        spawns[count] = located[index].spawn;
        copyX = spawns[count].x;
        copyY = spawns[count].y;
        vm_mock_admin_offset_map_xy(&copyX, &copyY, preview.width,
                                    preview.height);
        spawns[count].x = copyX;
        spawns[count].y = copyY;
        free(located);
        if (!vm_net_mock_sce_combat_spawn_admin_save_all(
                runtimeScene, spawns, count + 1u, 0, 0, false, &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "刷怪点复制失败");
            return;
        }
        snprintf(okMsg, sizeof(okMsg),
                 "已复制刷怪点 #%u → (%u,%u) 并发布；客户端需重新下载场景",
                 index + 1u, (u32)copyX, (u32)copyY);
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok", okMsg);
        return;
    }

    if (!vm_mock_admin_form_u32(body, "spawn_count",
                                VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX, &formCount))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "地图刷怪点表单无效");
        return;
    }
    for (u32 i = 0; i < formCount && spawnCount < VM_NET_MOCK_SCE_COMBAT_ADMIN_MAX;
         ++i)
    {
        u32 x = 0;
        u32 y = 0;
        u32 monsterId = 0;
        char deleteFlag[8];

        memset(deleteFlag, 0, sizeof(deleteFlag));
        snprintf(key, sizeof(key), "delete_%u", i);
        if (vm_mock_admin_form_value(body, key, deleteFlag, sizeof(deleteFlag)) &&
            deleteFlag[0] != 0 && strcmp(deleteFlag, "0") != 0)
        {
            continue;
        }
        snprintf(key, sizeof(key), "spawn_x_%u", i);
        if (!vm_mock_admin_form_u32(body, key, 0xffffu, &x))
            x = 0;
        snprintf(key, sizeof(key), "spawn_y_%u", i);
        if (!vm_mock_admin_form_u32(body, key, 0xffffu, &y))
            y = 0;
        snprintf(key, sizeof(key), "monster_id_%u", i);
        if (!vm_mock_admin_form_u32(body, key, 0xffffu, &monsterId))
            monsterId = 0;
        if (x == 0 || y == 0 || monsterId == 0)
            continue;
        spawns[spawnCount].x = (u16)x;
        spawns[spawnCount].y = (u16)y;
        spawns[spawnCount].actorId = monsterId;
        ++spawnCount;
    }
    if (!vm_net_mock_sce_combat_spawn_admin_save_all(runtimeScene, spawns,
                                                     spawnCount, 0, 0, false,
                                                     &error))
    {
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "error",
            error ? error : "地图刷怪点保存失败");
        return;
    }
    saved = spawnCount;
    {
        char okMsg[96];

        snprintf(okMsg, sizeof(okMsg), "已保存 %u 个地图刷怪点并发布", saved);
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok", okMsg);
    }
}

static bool vm_mock_admin_parse_portal_fields(
    const char *body, vm_net_mock_sce_edge_portal *portal, u16 *landingXOut,
    u16 *landingYOut, bool requireLanding)
{
    char targetUtf8[192];
    u32 left = 0;
    u32 top = 0;
    u32 right = 0;
    u32 bottom = 0;
    u32 spawnX = 0;
    u32 spawnY = 0;
    u32 entryId = 0;
    u32 targetEntryId = 0;
    u32 landingX = 0;
    u32 landingY = 0;

    if (portal == NULL)
        return false;
    memset(portal, 0, sizeof(*portal));
    memset(targetUtf8, 0, sizeof(targetUtf8));
    if (!vm_mock_admin_form_u32(body, "left", 0xffffu, &left) ||
        !vm_mock_admin_form_u32(body, "top", 0xffffu, &top) ||
        !vm_mock_admin_form_u32(body, "right", 0xffffu, &right) ||
        !vm_mock_admin_form_u32(body, "bottom", 0xffffu, &bottom) ||
        !vm_mock_admin_form_u32(body, "spawn_x", 0xffffu, &spawnX) ||
        !vm_mock_admin_form_u32(body, "spawn_y", 0xffffu, &spawnY) ||
        !vm_mock_admin_form_u32(body, "entry_id", 0xffffu, &entryId) ||
        !vm_mock_admin_form_u32(body, "target_entry_id", 0xffffu,
                                &targetEntryId) ||
        !vm_mock_admin_form_value(body, "target_scene", targetUtf8,
                                  sizeof(targetUtf8)) ||
        targetUtf8[0] == 0 ||
        !vm_mock_admin_utf8_to_gbk_text(targetUtf8, portal->targetScene,
                                        sizeof(portal->targetScene), false) ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene) ||
        right < left || bottom < top)
    {
        return false;
    }
    portal->left = (u16)left;
    portal->top = (u16)top;
    portal->right = (u16)right;
    portal->bottom = (u16)bottom;
    portal->spawnX = (u16)spawnX;
    portal->spawnY = (u16)spawnY;
    portal->entryId = (u16)entryId;
    portal->targetEntryId = (u16)targetEntryId;
    if (landingXOut)
        *landingXOut = (u16)spawnX;
    if (landingYOut)
        *landingYOut = (u16)spawnY;
    if (vm_mock_admin_form_u32(body, "landing_x", 0xffffu, &landingX) &&
        vm_mock_admin_form_u32(body, "landing_y", 0xffffu, &landingY))
    {
        if (landingXOut)
            *landingXOut = (u16)landingX;
        if (landingYOut)
            *landingYOut = (u16)landingY;
        return true;
    }
    return !requireLanding;
}

static void vm_mock_admin_handle_scene_portal_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    char sceneUtf8[192];
    char runtimeScene[64];
    vm_net_mock_sce_edge_portal portal;
    const char *error = NULL;
    u32 portalIndex = 0;
    u16 landingX = 0;
    u16 landingY = 0;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(&portal, 0, sizeof(portal));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)))
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "场景无效");
        return;
    }
    if (strcmp(action, "delete-scene-portal") == 0)
    {
        if (!vm_mock_admin_form_u32(body, "portal_index", 0xffffu,
                                    &portalIndex) ||
            !vm_net_mock_sce_edge_portal_admin_delete(runtimeScene, portalIndex,
                                                      &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "删除传送点失败");
            return;
        }
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       "传送点已删除并发布场景资源");
        return;
    }
    if (strcmp(action, "add-scene-portal") == 0)
    {
        if (!vm_mock_admin_parse_portal_fields(body, &portal, &landingX,
                                               &landingY, false) ||
            !vm_net_mock_sce_edge_portal_admin_add(runtimeScene, &portal,
                                                   &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "新增传送点失败");
            return;
        }
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       "传送点已新增并发布场景资源");
        return;
    }
    if (strcmp(action, "save-scene-portal") != 0 ||
        !vm_mock_admin_form_u32(body, "portal_index", 0xffffu, &portalIndex) ||
        !vm_mock_admin_parse_portal_fields(body, &portal, &landingX, &landingY,
                                           false) ||
        !vm_net_mock_sce_edge_portal_admin_save(runtimeScene, portalIndex,
                                                &portal, landingX, landingY,
                                                true, &error))
    {
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "error",
            error ? error : "保存传送点失败");
        return;
    }
    vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                   "传送点已保存并发布场景资源");
}

static void vm_mock_admin_handle_create_scene(vm_mock_service_socket client,
                                              const char *body)
{
    vm_mock_admin_scene_file *files = NULL;
    char newNameUtf8[192];
    char cloneUtf8[192];
    char newSceneGbk[64];
    char cloneSceneGbk[64];
    char newMapGbk[64];
    char cloneMapGbk[64];
    char sourceScePath[1200];
    char destScePath[1200];
    char sourceMapPath[1200];
    char destMapPath[1200];
    char newSceneUtf8[192];
    const char *publishNames[2];
    const char *error = NULL;
    u32 fileCount = 0;
    u32 publishCount = 0;
    size_t nameLen = 0;
    bool mapCopied = false;

    memset(newNameUtf8, 0, sizeof(newNameUtf8));
    memset(cloneUtf8, 0, sizeof(cloneUtf8));
    memset(newSceneGbk, 0, sizeof(newSceneGbk));
    memset(cloneSceneGbk, 0, sizeof(cloneSceneGbk));
    memset(newMapGbk, 0, sizeof(newMapGbk));
    memset(cloneMapGbk, 0, sizeof(cloneMapGbk));
    memset(sourceScePath, 0, sizeof(sourceScePath));
    memset(destScePath, 0, sizeof(destScePath));
    memset(sourceMapPath, 0, sizeof(sourceMapPath));
    memset(destMapPath, 0, sizeof(destMapPath));
    memset(newSceneUtf8, 0, sizeof(newSceneUtf8));
    memset(publishNames, 0, sizeof(publishNames));

    if (!vm_mock_admin_form_value(body, "new_scene_name", newNameUtf8,
                                  sizeof(newNameUtf8)) ||
        newNameUtf8[0] == 0 ||
        !vm_mock_admin_utf8_to_gbk_text(newNameUtf8, newSceneGbk,
                                        sizeof(newSceneGbk), false))
    {
        vm_mock_admin_redirect_content(client, "", "error",
                                       "新场景名称无效或编码失败");
        return;
    }
    nameLen = strlen(newSceneGbk);
    if (nameLen < 4 ||
        (newSceneGbk[nameLen - 4] != '.' ||
         tolower((unsigned char)newSceneGbk[nameLen - 3]) != 's' ||
         tolower((unsigned char)newSceneGbk[nameLen - 2]) != 'c' ||
         tolower((unsigned char)newSceneGbk[nameLen - 1]) != 'e'))
    {
        if (nameLen + 4 >= sizeof(newSceneGbk))
        {
            vm_mock_admin_redirect_content(client, "", "error",
                                           "新场景名称过长");
            return;
        }
        snprintf(newSceneGbk + nameLen, sizeof(newSceneGbk) - nameLen, ".sce");
    }
    if (!vm_net_mock_update_name_is_safe(newSceneGbk) ||
        !vm_net_mock_scene_name_is_download_key(newSceneGbk))
    {
        vm_mock_admin_redirect_content(client, "", "error",
                                       "场景名称不安全（禁止路径分隔符）");
        return;
    }
    if (!vm_mock_admin_form_value(body, "clone_from", cloneUtf8,
                                  sizeof(cloneUtf8)) ||
        cloneUtf8[0] == 0 ||
        !vm_mock_admin_utf8_to_gbk_text(cloneUtf8, cloneSceneGbk,
                                        sizeof(cloneSceneGbk), false) ||
        !vm_net_mock_update_name_is_safe(cloneSceneGbk))
    {
        vm_mock_admin_redirect_content(client, "", "error",
                                       "请选择要克隆的源场景");
        return;
    }

    files = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_SCENE_FILE_MAX, sizeof(*files));
    if (files == NULL)
    {
        vm_mock_admin_redirect_content(client, "", "error",
                                       "场景列表内存不足");
        return;
    }
    fileCount = vm_mock_admin_collect_scene_files(
        files, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    for (u32 i = 0; i < fileCount; ++i)
    {
        if (strcmp(files[i].name, newSceneGbk) == 0)
        {
            free(files);
            vm_net_mock_gbk_label_to_utf8(newSceneGbk, newSceneUtf8,
                                          sizeof(newSceneUtf8));
            vm_mock_admin_redirect_content(client, newSceneUtf8, "error",
                                           "目标场景已存在");
            return;
        }
    }
    {
        bool cloneExists = false;
        for (u32 i = 0; i < fileCount; ++i)
        {
            if (strcmp(files[i].name, cloneSceneGbk) == 0)
            {
                cloneExists = true;
                break;
            }
        }
        free(files);
        files = NULL;
        if (!cloneExists)
        {
            vm_mock_admin_redirect_content(client, "", "error",
                                           "源场景不存在");
            return;
        }
    }

    if (!vm_net_mock_update_resource_path(cloneSceneGbk, sourceScePath,
                                          sizeof(sourceScePath)) ||
        !vm_mock_admin_path_replace_leaf(sourceScePath, newSceneGbk, destScePath,
                                         sizeof(destScePath)))
    {
        vm_mock_admin_redirect_content(client, "", "error",
                                       "无法解析 JHOnlineData 资源目录");
        return;
    }
    if (!vm_mock_admin_copy_file_binary(sourceScePath, destScePath, &error))
    {
        vm_mock_admin_redirect_content(client, "", "error",
                                       error ? error : "复制场景文件失败");
        return;
    }
    publishNames[publishCount++] = newSceneGbk;

    /*
     * Always clone the walkable .map onto a private leaf named after the new
     * scene, then retarget SCE2's embedded map reference.  Sibling-only copy
     * left clones sharing the source .map (map editor sharedBy warning).
     */
    {
        char runtimeClone[64];
        FILE *mapProbe = NULL;
        FILE *destProbe = NULL;
        const char *retargetError = NULL;

        memset(runtimeClone, 0, sizeof(runtimeClone));
        if (!vm_mock_admin_scene_file_to_runtime_key(
                cloneSceneGbk, runtimeClone, sizeof(runtimeClone)) ||
            !vm_mock_admin_scene_map_name(runtimeClone, cloneMapGbk,
                                          sizeof(cloneMapGbk)) ||
            !vm_mock_admin_replace_extension(newSceneGbk, ".map", newMapGbk,
                                             sizeof(newMapGbk)) ||
            !vm_net_mock_update_name_is_safe(newMapGbk) ||
            !vm_net_mock_str_ends_with(newMapGbk, ".map"))
        {
            (void)remove(destScePath);
            vm_mock_admin_redirect_content(
                client, "", "error",
                "无法解析源场景地图，克隆已回滚");
            return;
        }
        if (strcmp(cloneMapGbk, newMapGbk) == 0)
        {
            (void)remove(destScePath);
            vm_mock_admin_redirect_content(
                client, "", "error",
                "新场景地图名与源地图相同，请换名后再克隆");
            return;
        }
        if (!vm_net_mock_update_resource_path(cloneMapGbk, sourceMapPath,
                                              sizeof(sourceMapPath)) ||
            !vm_mock_admin_path_replace_leaf(sourceMapPath, newMapGbk,
                                             destMapPath, sizeof(destMapPath)))
        {
            (void)remove(destScePath);
            vm_mock_admin_redirect_content(
                client, "", "error",
                "无法解析地图资源路径，克隆已回滚");
            return;
        }
        mapProbe = vm_net_mock_fopen_game_path(sourceMapPath, "rb");
        if (mapProbe == NULL)
        {
            (void)remove(destScePath);
            vm_mock_admin_redirect_content(
                client, "", "error",
                "源场景 .map 不存在，克隆已回滚");
            return;
        }
        fclose(mapProbe);
        destProbe = vm_net_mock_fopen_game_path(destMapPath, "rb");
        if (destProbe != NULL)
        {
            fclose(destProbe);
            (void)remove(destScePath);
            vm_mock_admin_redirect_content(
                client, "", "error",
                "目标 .map 已存在，克隆已回滚");
            return;
        }
        if (!vm_mock_admin_copy_file_binary(sourceMapPath, destMapPath,
                                            &error))
        {
            (void)remove(destScePath);
            vm_mock_admin_redirect_content(
                client, "", "error",
                error ? error : "复制 .map 失败，克隆已回滚");
            return;
        }
        mapCopied = true;
        if (!vm_net_mock_sce_retarget_embedded_map_leaf(newSceneGbk, newMapGbk,
                                                         &retargetError))
        {
            (void)remove(destScePath);
            (void)remove(destMapPath);
            vm_mock_admin_redirect_content(
                client, "", "error",
                retargetError ? retargetError :
                                "改写场景地图引用失败，克隆已回滚");
            return;
        }
        publishNames[publishCount++] = newMapGbk;
    }

    if (!vm_net_mock_update_admin_publish_named_files(publishNames, publishCount,
                                                      &error))
    {
        (void)remove(destScePath);
        if (mapCopied)
            (void)remove(destMapPath);
        vm_mock_admin_redirect_content(
            client, "", "error",
            error ? error : "场景文件已复制，但发布失败");
        return;
    }

    vm_net_mock_gbk_label_to_utf8(newSceneGbk, newSceneUtf8,
                                  sizeof(newSceneUtf8));
    vm_mock_admin_redirect_content(
        client, newSceneUtf8, "ok",
        "场景已克隆并发布（含独立 .map）");
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
    char instanceSceneUtf8[192];
    char instanceRuntimeScene[64];
    char actorImageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    char enabledText[8];
    vm_net_mock_scene_npcinfo_seed seed;
    const char *error = NULL;
    u32 actorId = 0;
    u32 x = 0;
    u32 y = 0;
    u32 kind = 0;
    u32 serviceMask = 0;
    u32 taskId = 0;
    u32 taskRepeatable = 0;
    u32 taskDailyAcceptLimit = 0;
    u32 orientation = 0;
    u32 actorImageCount = 0;
    u32 instanceX = 0;
    u32 instanceY = 0;
    u32 challengeEnemyId = 0;
    u32 instanceMinLevel = 1;
    u32 teleportRequireTaskId = 0;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(displayUtf8, 0, sizeof(displayUtf8));
    memset(scriptUtf8, 0, sizeof(scriptUtf8));
    memset(actorResource, 0, sizeof(actorResource));
    memset(instanceSceneUtf8, 0, sizeof(instanceSceneUtf8));
    memset(instanceRuntimeScene, 0, sizeof(instanceRuntimeScene));
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
        vm_net_mock_dynamic_npc_admin_row *rows = NULL;
        u32 rowCount = 0;
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
        rows = (vm_net_mock_dynamic_npc_admin_row *)calloc(
            VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX, sizeof(*rows));
        if (rows == NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "内存不足，无法切换 NPC 状态");
            return;
        }
        rowCount = vm_net_mock_dynamic_npc_admin_list(
            runtimeScene, rows, VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX);
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
            free(rows);
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error :
                        "Actor 资源无效、引用图片不完整或无法发布下载");
            return;
        }
        if (!found || !vm_net_mock_dynamic_npc_admin_save(
                          runtimeScene, &seed, enabled, &error))
        {
            free(rows);
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "NPC 不存在或状态保存失败");
            return;
        }
        free(rows);
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       enabled ? "NPC 已恢复；缺失资源将由客户端在线下载"
                                               : "NPC 已停用");
        return;
    }

    if (strcmp(action, "save-npc-position") == 0)
    {
        vm_net_mock_dynamic_npc_admin_row *rows = NULL;
        u32 rowCount = 0;
        bool found = false;
        bool enabled = true;
        bool overridden = false;

        if (!vm_mock_admin_form_u32(body, "x", 0xffffu, &x) || x == 0 ||
            !vm_mock_admin_form_u32(body, "y", 0xffffu, &y) || y == 0)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "NPC 坐标无效");
            return;
        }
        rows = (vm_net_mock_dynamic_npc_admin_row *)calloc(
            VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX, sizeof(*rows));
        if (rows == NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "内存不足，无法保存 NPC 坐标");
            return;
        }
        rowCount = vm_net_mock_dynamic_npc_admin_list(
            runtimeScene, rows, VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX);
        for (u32 i = 0; i < rowCount; ++i)
        {
            if (rows[i].seed.actorId == actorId)
            {
                seed = rows[i].seed;
                enabled = rows[i].enabled;
                overridden = rows[i].overridden || !rows[i].builtin;
                found = true;
                break;
            }
        }
        if (!found)
        {
            free(rows);
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "NPC 不存在，无法调整坐标");
            return;
        }
        seed.x = (u16)x;
        seed.y = (u16)y;
        /*
         * Already-persisted NPCs: pos-only UPDATE.  Avoids DELETE/reinsert of
         * server_dynamic_npc_tasks (binding wipe / FK noise on map drag).
         * First move of a pure SCE builtin still needs a full save to create
         * the server_dynamic_npcs row.
         */
        if (overridden)
        {
            if (!vm_net_mock_dynamic_npc_admin_save_position(
                    runtimeScene, actorId, (u16)x, (u16)y, &error))
            {
                free(rows);
                vm_mock_admin_redirect_content(
                    client, sceneUtf8, "error",
                    error ? error : "NPC 坐标保存失败");
                return;
            }
        }
        else if (!vm_net_mock_dynamic_npc_admin_save(runtimeScene, &seed, enabled,
                                                      &error))
        {
            free(rows);
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "NPC 坐标保存失败");
            return;
        }
        free(rows);
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       "NPC 坐标已保存；下次进入场景生效");
        return;
    }

    if (strcmp(action, "copy-npc") == 0)
    {
        vm_mock_admin_scene_preview preview;
        u32 newActorId = 0;
        u16 copyX = 0;
        u16 copyY = 0;
        char okMsg[160];
        char actorImageNamesLocal[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
        u32 actorImageCountLocal = 0;

        memset(&preview, 0, sizeof(preview));
        memset(actorImageNamesLocal, 0, sizeof(actorImageNamesLocal));
        if (!vm_mock_admin_find_npc_seed_for_copy(runtimeScene, actorId, &seed))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "NPC 不存在，无法复制");
            return;
        }
        newActorId = vm_mock_admin_alloc_free_npc_actor_id(runtimeScene);
        if (newActorId == 0)
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                "没有可用的 Actor ID，无法复制 NPC");
            return;
        }
        (void)vm_mock_admin_scene_preview_info(runtimeScene, &preview);
        copyX = seed.x;
        copyY = seed.y;
        vm_mock_admin_offset_map_xy(&copyX, &copyY, preview.width,
                                    preview.height);
        seed.actorId = newActorId;
        seed.x = copyX;
        seed.y = copyY;
        if (!vm_mock_admin_actor_resource_inspect(
                seed.actorResource, actorImageNamesLocal,
                &actorImageCountLocal) ||
            !vm_mock_admin_publish_actor_resource(
                seed.actorResource, actorImageNamesLocal,
                actorImageCountLocal, &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error :
                        "Actor 资源无效、引用图片不完整或无法发布下载");
            return;
        }
        if (!vm_net_mock_dynamic_npc_admin_save(runtimeScene, &seed, true,
                                                 &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error : "NPC 复制失败");
            return;
        }
        snprintf(okMsg, sizeof(okMsg),
                 "已复制 NPC → Actor %u · (%u,%u)；下次进入场景生效",
                 newActorId, (u32)copyX, (u32)copyY);
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok", okMsg);
        return;
    }

    if (strcmp(action, "save-npc") != 0 ||
        !vm_mock_admin_form_u32(body, "x", 0xffffu, &x) || x == 0 ||
        !vm_mock_admin_form_u32(body, "y", 0xffffu, &y) || y == 0 ||
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
    for (u32 serviceKind = 1; serviceKind <= VM_NET_MOCK_NPC_KIND_MAX; ++serviceKind)
    {
        char fieldName[32];
        char checked[8];

        snprintf(fieldName, sizeof(fieldName), "service_kind_%u", serviceKind);
        memset(checked, 0, sizeof(checked));
        if (vm_mock_admin_form_value(body, fieldName, checked, sizeof(checked)) &&
            checked[0] != 0 && strcmp(checked, "0") != 0)
        {
            serviceMask |= (1u << serviceKind);
        }
    }
    serviceMask &= VM_NET_MOCK_NPC_SERVICE_MASK_ALL;
    kind = vm_net_mock_npc_primary_kind_from_mask(serviceMask);
    seed.boundTaskCount = 0;
    memset(seed.boundTasks, 0, sizeof(seed.boundTasks));
    seed.taskId = 0;
    seed.taskRepeatable = false;
    seed.taskDailyAcceptLimit = 0;
    for (u32 bindIndex = 0; bindIndex < VM_NET_MOCK_NPC_BOUND_TASK_MAX; ++bindIndex)
    {
        char fieldTask[40];
        char fieldRepeatable[48];
        char fieldDaily[56];
        u32 bindTaskId = 0;
        u32 bindRepeatable = 0;
        u32 bindDaily = 0;

        snprintf(fieldTask, sizeof(fieldTask), "bound_task_id_%u", bindIndex);
        snprintf(fieldRepeatable, sizeof(fieldRepeatable),
                 "bound_task_repeatable_%u", bindIndex);
        snprintf(fieldDaily, sizeof(fieldDaily),
                 "bound_task_daily_accept_limit_%u", bindIndex);
        if (!vm_mock_admin_form_u32_or_default(body, fieldTask, 0xffffffffu, 0,
                                                &bindTaskId) ||
            !vm_mock_admin_form_u32_or_default(body, fieldRepeatable, 1u, 0,
                                                &bindRepeatable) ||
            !vm_mock_admin_form_u32_or_default(body, fieldDaily, 9999u, 0,
                                                &bindDaily))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "可接取任务绑定字段无效");
            return;
        }
        if (bindTaskId == 0)
            continue;
        if (vm_net_mock_task_catalog_find_by_id(bindTaskId) == NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "绑定任务不存在或已停用");
            return;
        }
        if (vm_net_mock_seed_find_bound_task(&seed, bindTaskId) != NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "同一 NPC 不能重复绑定相同任务");
            return;
        }
        if (!vm_net_mock_seed_add_bound_task(&seed, bindTaskId,
                                              bindRepeatable != 0,
                                              (u16)bindDaily))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "可接取任务绑定数量超过上限");
            return;
        }
    }
    /* Legacy single-field fallback when multi-row form is absent. */
    if (seed.boundTaskCount == 0 &&
        vm_mock_admin_form_u32_or_default(body, "task_id", 0xffffffffu, 0,
                                           &taskId) &&
        taskId != 0)
    {
        if (!vm_mock_admin_form_u32_or_default(body, "task_repeatable", 1u, 0,
                                                &taskRepeatable) ||
            !vm_mock_admin_form_u32_or_default(body, "task_daily_accept_limit",
                                                9999u, 0,
                                                &taskDailyAcceptLimit))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "可接取任务绑定字段无效");
            return;
        }
        if (vm_net_mock_task_catalog_find_by_id(taskId) == NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "绑定任务不存在或已停用");
            return;
        }
        if (!vm_net_mock_seed_add_bound_task(&seed, taskId, taskRepeatable != 0,
                                              (u16)taskDailyAcceptLimit))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "可接取任务绑定失败");
            return;
        }
    }
    if ((serviceMask & (1u << VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE)) != 0)
    {
        vm_mock_admin_scene_preview targetPreview;

        memset(&targetPreview, 0, sizeof(targetPreview));
        if (!vm_mock_admin_optional_scene_from_form(
                body, "instance_scene", instanceSceneUtf8,
                sizeof(instanceSceneUtf8), instanceRuntimeScene,
                sizeof(instanceRuntimeScene)) ||
            !vm_mock_admin_form_u32(body, "instance_x", 0xffffu,
                                    &instanceX) ||
            !vm_mock_admin_form_u32(body, "instance_y", 0xffffu,
                                    &instanceY) ||
            !vm_mock_admin_form_u32(body, "challenge_enemy_id", 0xffffu,
                                    &challengeEnemyId) ||
            !vm_mock_admin_form_u32(body, "instance_min_level", 0xffu,
                                    &instanceMinLevel) ||
            !vm_mock_admin_form_u32(body, "teleport_require_task_id",
                                    0xffffffffu, &teleportRequireTaskId) ||
            instanceMinLevel == 0)
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error", "副本配置字段不完整或数值越界");
            return;
        }
        if (instanceRuntimeScene[0] == 0 && challengeEnemyId == 0)
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error", "副本传送场景和挑战怪物至少配置一项");
            return;
        }
        if (teleportRequireTaskId != 0 && instanceRuntimeScene[0] == 0)
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                "设置传送前置任务时必须同时配置副本目标场景");
            return;
        }
        if (teleportRequireTaskId != 0 &&
            vm_net_mock_task_catalog_find_by_id(teleportRequireTaskId) == NULL)
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error", "传送前置任务不存在或已停用");
            return;
        }
        if (instanceRuntimeScene[0] != 0 &&
            instanceX == 0 && instanceY == 0)
        {
            u16 resolvedX = 0;
            u16 resolvedY = 0;
            u16 entryId = 0xffff;

            if (!vm_net_mock_get_scene_reasonable_spawn_from_sce(
                    instanceRuntimeScene, &resolvedX, &resolvedY, &entryId))
            {
                vm_mock_admin_redirect_content(
                    client, sceneUtf8, "error", "无法从目标 SCE 解析安全副本落点");
                return;
            }
            instanceX = resolvedX;
            instanceY = resolvedY;
        }
        if (instanceRuntimeScene[0] != 0 &&
            ((instanceX == 0) != (instanceY == 0) ||
             !vm_mock_admin_scene_preview_info(instanceRuntimeScene,
                                               &targetPreview) ||
             instanceX >= targetPreview.width ||
             instanceY >= targetPreview.height))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error", "副本落点不在目标场景的有效像素范围内");
            return;
        }
        if (challengeEnemyId != 0 &&
            !vm_net_mock_monster_enemy_id_known(challengeEnemyId))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error", "挑战怪物 ID 不在当前服务端怪物目录中");
            return;
        }
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
    seed.serviceMask = serviceMask;
    vm_net_mock_seed_sync_primary_bound_task(&seed);
    seed.orientation = (u16)orientation;
    seed.instanceX = (u16)instanceX;
    seed.instanceY = (u16)instanceY;
    seed.challengeEnemyId = challengeEnemyId;
    seed.instanceMinLevel = (u16)instanceMinLevel;
    seed.teleportRequireTaskId = teleportRequireTaskId;
    snprintf(seed.instanceScene, sizeof(seed.instanceScene), "%s",
             instanceRuntimeScene);
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
    char mallFilter[8];
    char npcFilter[8];
    char pageText[32];
    char enabledText[8];
    char npcEnabledText[8];
    const char *error = NULL;
    u32 itemId = 0;
    u32 price = 0;
    u32 npcPrice = 0;
    u32 page = 1;
    bool enabled = false;
    bool npcEnabled = false;
    char okMessage[96];

    memset(category, 0, sizeof(category));
    memset(search, 0, sizeof(search));
    memset(mallFilter, 0, sizeof(mallFilter));
    memset(npcFilter, 0, sizeof(npcFilter));
    memset(pageText, 0, sizeof(pageText));
    memset(enabledText, 0, sizeof(enabledText));
    memset(npcEnabledText, 0, sizeof(npcEnabledText));
    memset(okMessage, 0, sizeof(okMessage));
    (void)vm_mock_admin_form_value(body, "category", category,
                                   sizeof(category));
    (void)vm_mock_admin_form_value(body, "q", search, sizeof(search));
    (void)vm_mock_admin_form_value(body, "mall", mallFilter,
                                   sizeof(mallFilter));
    (void)vm_mock_admin_form_value(body, "npc", npcFilter, sizeof(npcFilter));
    (void)vm_mock_admin_form_value(body, "page", pageText, sizeof(pageText));
    vm_mock_admin_shop_shelf_filter_text(
        vm_mock_admin_shop_shelf_filter_value(mallFilter), mallFilter,
        sizeof(mallFilter));
    vm_mock_admin_shop_shelf_filter_text(
        vm_mock_admin_shop_shelf_filter_value(npcFilter), npcFilter,
        sizeof(npcFilter));
    if (pageText[0] != 0 &&
        (!vm_net_mock_parse_u32_strict(pageText, &page) || page == 0))
    {
        page = 1;
    }
    if (!vm_mock_admin_form_u32(body, "item", 0xffffffffu, &itemId) ||
        itemId == 0 ||
        !vm_mock_admin_form_u32(body, "price", 0xffffffffu, &price) ||
        price == 0 ||
        !vm_mock_admin_form_u32(body, "npc_price", 0xffffffffu, &npcPrice) ||
        npcPrice == 0 ||
        !vm_mock_admin_form_value(body, "enabled", enabledText,
                                  sizeof(enabledText)) ||
        (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0) ||
        !vm_mock_admin_form_value(body, "npc_enabled", npcEnabledText,
                                  sizeof(npcEnabledText)) ||
        (strcmp(npcEnabledText, "0") != 0 &&
         strcmp(npcEnabledText, "1") != 0))
    {
        vm_mock_admin_redirect_shop(client, category, search, mallFilter,
                                    npcFilter, page, "error",
                                    "商品、商城价、NPC价或上架状态无效");
        return;
    }
    enabled = strcmp(enabledText, "1") == 0;
    npcEnabled = strcmp(npcEnabledText, "1") == 0;
    if (!vm_net_mock_shop_admin_save(itemId, price, npcPrice, enabled,
                                     npcEnabled, &error))
    {
        vm_mock_admin_redirect_shop(
            client, category, search, mallFilter, npcFilter, page, "error",
            error ? error : "商品保存失败");
        return;
    }
    snprintf(okMessage, sizeof(okMessage),
             "已保存 · 商城%s · NPC%s",
             enabled ? "上架" : "未上架",
             npcEnabled ? "上架" : "未上架");
    vm_mock_admin_redirect_shop(client, category, search, mallFilter,
                                npcFilter, page, "ok", okMessage);
}

static void vm_mock_admin_handle_equip_sell_ratio_action(
    vm_mock_service_socket client, const char *body)
{
    char category[16];
    char search[128];
    char mallFilter[8];
    char npcFilter[8];
    char pageText[32];
    u32 page = 1;
    u32 percent = 10;

    memset(category, 0, sizeof(category));
    memset(search, 0, sizeof(search));
    memset(mallFilter, 0, sizeof(mallFilter));
    memset(npcFilter, 0, sizeof(npcFilter));
    memset(pageText, 0, sizeof(pageText));
    (void)vm_mock_admin_form_value(body, "category", category, sizeof(category));
    (void)vm_mock_admin_form_value(body, "q", search, sizeof(search));
    (void)vm_mock_admin_form_value(body, "mall", mallFilter, sizeof(mallFilter));
    (void)vm_mock_admin_form_value(body, "npc", npcFilter, sizeof(npcFilter));
    (void)vm_mock_admin_form_value(body, "page", pageText, sizeof(pageText));
    vm_mock_admin_shop_shelf_filter_text(
        vm_mock_admin_shop_shelf_filter_value(mallFilter), mallFilter,
        sizeof(mallFilter));
    vm_mock_admin_shop_shelf_filter_text(
        vm_mock_admin_shop_shelf_filter_value(npcFilter), npcFilter,
        sizeof(npcFilter));
    if (pageText[0] != 0 &&
        (!vm_net_mock_parse_u32_strict(pageText, &page) || page == 0))
        page = 1;
    if (!vm_mock_admin_form_u32(body, "equip_sell_percent", 100u, &percent))
    {
        vm_mock_admin_redirect_shop(client, category, search, mallFilter,
                                    npcFilter, page, "error",
                                    "出售比例无效（0–100）");
        return;
    }
    if (!vm_net_mock_equip_sell_percent_admin_save(percent))
    {
        vm_mock_admin_redirect_shop(client, category, search, mallFilter,
                                    npcFilter, page, "error",
                                    "保存出售比例失败");
        return;
    }
    vm_mock_admin_redirect_shop(client, category, search, mallFilter, npcFilter,
                                page, "ok", "装备出售价格比例已保存");
}

static void vm_mock_admin_handle_login_server_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    vm_net_mock_login_server row;
    char displayUtf8[128];
    char labelUtf8[128];
    char enabledText[8];
    const char *error = NULL;
    u32 value = 0;

    memset(&row, 0, sizeof(row));
    memset(displayUtf8, 0, sizeof(displayUtf8));
    memset(labelUtf8, 0, sizeof(labelUtf8));
    memset(enabledText, 0, sizeof(enabledText));
    if (!vm_mock_admin_form_u32(body, "server_id", 0xffffffffu,
                                &row.serverId) || row.serverId == 0)
    {
        vm_mock_admin_redirect_servers(client, "error", "服务器 ID 无效");
        return;
    }
    if (strcmp(action, "delete-login-server") == 0)
    {
        if (!vm_net_mock_login_server_admin_delete(row.serverId, &error))
        {
            vm_mock_admin_redirect_servers(
                client, "error", error ? error : "服务器删除失败");
            return;
        }
        vm_mock_admin_redirect_servers(client, "ok", "服务器已删除");
        return;
    }
    if ((strcmp(action, "save-login-server") != 0 &&
         strcmp(action, "create-login-server") != 0) ||
        !vm_mock_admin_form_value(body, "display_name", displayUtf8,
                                  sizeof(displayUtf8)) ||
        !vm_mock_admin_form_value(body, "status_label", labelUtf8,
                                  sizeof(labelUtf8)) ||
        !vm_mock_admin_utf8_to_gbk_text(displayUtf8, row.displayName,
                                        sizeof(row.displayName), false) ||
        !vm_mock_admin_utf8_to_gbk_text(labelUtf8, row.label,
                                        sizeof(row.label), false) ||
        strlen(row.displayName) >= sizeof(row.displayName) ||
        strlen(row.label) >= sizeof(row.label) ||
        !vm_mock_admin_form_u32(body, "display_color", 0x00ffffffu,
                                &row.displayColor) ||
        !vm_mock_admin_form_u32(body, "sort_order", 0xffffffffu,
                                &row.sortOrder) ||
        !vm_mock_admin_form_value(body, "enabled", enabledText,
                                  sizeof(enabledText)) ||
        (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0))
    {
        vm_mock_admin_redirect_servers(
            client, "error", "服务器名称、标签、颜色、排序或状态无效");
        return;
    }
    value = strcmp(enabledText, "1") == 0 ? 1u : 0u;
    row.enabled = value != 0;
    if (!vm_net_mock_login_server_admin_save(&row, &error))
    {
        vm_mock_admin_redirect_servers(
            client, "error", error ? error : "服务器保存失败");
        return;
    }
    vm_mock_admin_redirect_servers(
        client, "ok",
        strcmp(action, "create-login-server") == 0 ?
            "服务器已新增；下次登录会收到该列表" :
            "服务器配置已保存；下次登录会收到最新列表");
}

static bool vm_mock_admin_parse_map_image_names_form(
    const char *body, char namesOut[][64], u32 namesCap, u32 *countOut,
    const char **errorOut)
{
    char *raw = NULL;
    char (*catalog)[64] = NULL;
    u32 catalogCount = 0;
    u32 count = 0;
    const char *cursor = NULL;

    if (countOut)
        *countOut = 0;
    if (errorOut)
        *errorOut = "图集列表无效";
    if (body == NULL || namesOut == NULL || namesCap == 0 || countOut == NULL)
        return false;
    raw = (char *)malloc(4096);
    if (raw == NULL)
    {
        if (errorOut)
            *errorOut = "图集列表内存不足";
        return false;
    }
    if (!vm_mock_admin_form_value(body, "image_names", raw, 4096) || raw[0] == 0)
    {
        free(raw);
        if (errorOut)
            *errorOut = "缺少图集列表";
        return false;
    }
    catalog = (char (*)[64])calloc(VM_MOCK_ADMIN_MAP_SHEET_CATALOG_MAX, 64);
    if (catalog == NULL)
    {
        free(raw);
        if (errorOut)
            *errorOut = "图集目录内存不足";
        return false;
    }
    catalogCount = vm_mock_admin_collect_map_tile_sheets(
        catalog, VM_MOCK_ADMIN_MAP_SHEET_CATALOG_MAX);
    cursor = raw;
    while (*cursor != 0 && count < namesCap)
    {
        char tokenUtf8[192];
        char tokenGbk[64];
        size_t tokenLen = 0;
        const char *end = cursor;

        while (*end != 0 && *end != '\n' && *end != '\r')
            end++;
        tokenLen = (size_t)(end - cursor);
        while (tokenLen > 0 &&
               (cursor[tokenLen - 1] == ' ' || cursor[tokenLen - 1] == '\t'))
            tokenLen--;
        while (tokenLen > 0 && (*cursor == ' ' || *cursor == '\t'))
        {
            cursor++;
            tokenLen--;
        }
        if (tokenLen > 0)
        {
            if (tokenLen >= sizeof(tokenUtf8))
            {
                free(raw);
                free(catalog);
                if (errorOut)
                    *errorOut = "图集名过长";
                return false;
            }
            memcpy(tokenUtf8, cursor, tokenLen);
            tokenUtf8[tokenLen] = 0;
            memset(tokenGbk, 0, sizeof(tokenGbk));
            if (!vm_mock_admin_utf8_to_gbk_text(tokenUtf8, tokenGbk,
                                                sizeof(tokenGbk), false) ||
                !vm_net_mock_update_name_is_safe(tokenGbk) ||
                !vm_net_mock_str_ends_with(tokenGbk, ".gif"))
            {
                free(raw);
                free(catalog);
                if (errorOut)
                    *errorOut = "图集名非法";
                return false;
            }
            if (vm_mock_admin_map_sheet_name_known(namesOut, count, tokenGbk))
            {
                free(raw);
                free(catalog);
                if (errorOut)
                    *errorOut = "图集列表含重复项";
                return false;
            }
            {
                FILE *fp = NULL;
                bool inCatalog = vm_mock_admin_map_sheet_name_known(
                    catalog, catalogCount, tokenGbk);
                bool exists = vm_net_mock_open_server_data_resource(
                    tokenGbk, ".gif", &fp, NULL, 0);
                if (fp)
                    fclose(fp);
                if (!inCatalog || !exists)
                {
                    free(raw);
                    free(catalog);
                    if (errorOut)
                        *errorOut = "图集不在资源目录或不存在";
                    return false;
                }
            }
            snprintf(namesOut[count], 64, "%s", tokenGbk);
            count++;
        }
        cursor = end;
        while (*cursor == '\n' || *cursor == '\r')
            cursor++;
    }
    free(raw);
    free(catalog);
    if (count == 0 || count > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
    {
        if (errorOut)
            *errorOut = count == 0 ? "至少需要一张图集" : "图集数量超过上限";
        return false;
    }
    *countOut = count;
    if (errorOut)
        *errorOut = "ok";
    return true;
}

static bool vm_mock_admin_map_cells_sheet_indices_valid(const u8 *cells,
                                                        u32 cellCount,
                                                        u32 imageCount)
{
    if (cells == NULL || cellCount == 0 || imageCount == 0 ||
        imageCount > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
        return false;
    for (u32 i = 0; i < cellCount; ++i)
    {
        u32 packed = vm_mock_service_read_le32(cells + i * 4u);
        u32 sheet = (packed >> 24) & 0x0fu;
        if (sheet >= imageCount)
            return false;
    }
    return true;
}

static void vm_mock_admin_handle_save_scene_map(vm_mock_service_socket client,
                                                const char *body)
{
    char sceneUtf8[192];
    char runtimeScene[64];
    char mapNameFormUtf8[192];
    char mapNameFormGbk[64];
    char expectedMap[64];
    char cellCountText[32];
    char *cellsB64 = NULL;
    u8 *mapPayload = NULL;
    u32 mapPayloadLen = 0;
    u8 *newCells = NULL;
    u32 newCellsLen = 0;
    u8 *built = NULL;
    u32 builtLen = 0;
    vm_mock_admin_scene_preview preview;
    const char *error = NULL;
    u32 cellCount = 0;
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    u32 imageCount = 0;
    const char *publishNames[1 + VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX];
    u32 publishCount = 0;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(mapNameFormUtf8, 0, sizeof(mapNameFormUtf8));
    memset(mapNameFormGbk, 0, sizeof(mapNameFormGbk));
    memset(expectedMap, 0, sizeof(expectedMap));
    memset(cellCountText, 0, sizeof(cellCountText));
    memset(imageNames, 0, sizeof(imageNames));
    memset(&preview, 0, sizeof(preview));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)))
    {
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error", "场景无效");
        return;
    }
    if (!vm_mock_admin_form_value(body, "map_name", mapNameFormUtf8,
                                  sizeof(mapNameFormUtf8)) ||
        !vm_mock_admin_utf8_to_gbk_text(mapNameFormUtf8, mapNameFormGbk,
                                        sizeof(mapNameFormGbk), false) ||
        !vm_mock_admin_form_value(body, "cell_count", cellCountText,
                                  sizeof(cellCountText)) ||
        !vm_net_mock_parse_u32_strict(cellCountText, &cellCount) ||
        cellCount == 0)
    {
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error",
                                       "地图保存参数不完整");
        return;
    }
    if (!vm_mock_admin_scene_map_name(runtimeScene, expectedMap,
                                      sizeof(expectedMap)) ||
        strcmp(expectedMap, mapNameFormGbk) != 0 ||
        !vm_net_mock_update_name_is_safe(expectedMap))
    {
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error",
                                       "地图名与场景不匹配");
        return;
    }
    if (!vm_mock_admin_parse_map_image_names_form(body, imageNames,
                                                  VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX,
                                                  &imageCount, &error))
    {
        vm_mock_admin_redirect_mapedit(
            client, sceneUtf8, "error",
            error && error[0] ? error : "图集列表无效");
        return;
    }
    cellsB64 = (char *)malloc(VM_MOCK_ADMIN_REQUEST_MAX + 1);
    if (cellsB64 == NULL)
    {
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error",
                                       "地图保存内存不足");
        return;
    }
    if (!vm_mock_admin_form_value(body, "cells_b64", cellsB64,
                                  VM_MOCK_ADMIN_REQUEST_MAX + 1) ||
        cellsB64[0] == 0)
    {
        free(cellsB64);
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error",
                                       "缺少地图格子数据");
        return;
    }
    newCells = (u8 *)malloc((size_t)cellCount * 4u);
    if (newCells == NULL ||
        !vm_mock_admin_base64_decode(cellsB64, newCells, cellCount * 4u,
                                     &newCellsLen) ||
        newCellsLen != cellCount * 4u)
    {
        free(cellsB64);
        free(newCells);
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error",
                                       "地图格子数据解码失败");
        return;
    }
    free(cellsB64);
    cellsB64 = NULL;
    if (!vm_mock_admin_load_data_payload(expectedMap, ".map", &mapPayload,
                                         &mapPayloadLen, NULL) ||
        !vm_mock_admin_parse_map_preview(mapPayload, mapPayloadLen, expectedMap,
                                         &preview) ||
        preview.cellCount != cellCount)
    {
        free(mapPayload);
        free(newCells);
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error",
                                       "当前地图尺寸与提交不一致");
        return;
    }
    if (!vm_mock_admin_map_cells_sheet_indices_valid(newCells, cellCount,
                                                    imageCount))
    {
        free(mapPayload);
        free(newCells);
        vm_mock_admin_redirect_mapedit(client, sceneUtf8, "error",
                                       "格子引用了不存在的图集下标");
        return;
    }
    preview.imageCount = imageCount;
    memset(preview.imageNames, 0, sizeof(preview.imageNames));
    for (u32 i = 0; i < imageCount; ++i)
        snprintf(preview.imageNames[i], sizeof(preview.imageNames[i]), "%s",
                 imageNames[i]);
    if (!vm_mock_admin_build_map_payload(&preview, newCells, newCellsLen, &built,
                                         &builtLen) ||
        !vm_net_mock_write_map_resource_bytes(expectedMap, built, builtLen,
                                              &error))
    {
        free(mapPayload);
        free(newCells);
        free(built);
        vm_mock_admin_redirect_mapedit(
            client, sceneUtf8, "error",
            error && error[0] ? error : "写入地图失败");
        return;
    }
    publishNames[publishCount++] = expectedMap;
    for (u32 i = 0; i < imageCount; ++i)
        publishNames[publishCount++] = imageNames[i];
    if (!vm_net_mock_publish_scene_resource(runtimeScene, &error))
    {
        if (!vm_net_mock_update_admin_publish_named_files(publishNames,
                                                          publishCount, &error))
        {
            free(mapPayload);
            free(newCells);
            free(built);
            vm_mock_admin_redirect_mapedit(
                client, sceneUtf8, "error",
                error ? error : "地图已写入但发布失败");
            return;
        }
    }
    else
    {
        (void)vm_net_mock_update_admin_publish_named_files(publishNames,
                                                           publishCount, NULL);
    }
    vm_mock_admin_clear_scene_preview_cache();
    free(mapPayload);
    free(newCells);
    free(built);
    vm_mock_admin_redirect_mapedit(client, sceneUtf8, "ok",
                                   "地图已保存并发布；客户端需重进场景拉取更新");
}

static void vm_mock_admin_handle_action(vm_mock_service_socket client, const char *body)
{
    char action[32];
    char account[64];
    char password[64];
    char role[64];
    char itemText[32];
    char amountText[32];
    char nameUtf8[96];
    char nameGbk[96];
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
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(nameGbk, 0, sizeof(nameGbk));
    if (!vm_mock_admin_form_value(body, "action", action, sizeof(action)))
    {
        vm_mock_admin_redirect(client, "", "error", "请求参数不完整");
        return;
    }
    if (strcmp(action, "save-scene-map") == 0)
    {
        vm_mock_admin_handle_save_scene_map(client, body);
        return;
    }
    if (strcmp(action, "save-npc") == 0 ||
        strcmp(action, "save-npc-position") == 0 ||
        strcmp(action, "copy-npc") == 0 ||
        strcmp(action, "toggle-npc") == 0 ||
        strcmp(action, "delete-npc-override") == 0)
    {
        vm_mock_admin_handle_npc_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-scene-monsters") == 0 ||
        strcmp(action, "reset-scene-monsters") == 0)
    {
        vm_mock_admin_handle_scene_monster_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-scene-display-name") == 0)
    {
        vm_mock_admin_handle_scene_display_name_action(client, body);
        return;
    }
    if (strcmp(action, "save-instance-enter-rule") == 0)
    {
        vm_mock_admin_handle_instance_enter_rule_action(client, body);
        return;
    }
    if (strcmp(action, "save-scene-combat-spawns") == 0 ||
        strcmp(action, "save-combat-spawn-position") == 0 ||
        strcmp(action, "copy-combat-spawn") == 0)
    {
        vm_mock_admin_handle_scene_combat_spawn_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-scene-portal") == 0 ||
        strcmp(action, "add-scene-portal") == 0 ||
        strcmp(action, "delete-scene-portal") == 0)
    {
        vm_mock_admin_handle_scene_portal_action(client, action, body);
        return;
    }
    if (strcmp(action, "create-scene") == 0)
    {
        vm_mock_admin_handle_create_scene(client, body);
        return;
    }
    if (strcmp(action, "save-task") == 0 ||
        strcmp(action, "delete-task-override") == 0)
    {
        vm_mock_admin_handle_task_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-monster-global-config") == 0)
    {
        vm_mock_admin_handle_monster_global_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-monster") == 0 ||
        strcmp(action, "reset-monster") == 0 ||
        strcmp(action, "create-monster") == 0 ||
        strcmp(action, "delete-monster") == 0)
    {
        vm_mock_admin_handle_monster_action(client, action, body);
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
    if (strcmp(action, "save-equip-sell-ratio") == 0)
    {
        vm_mock_admin_handle_equip_sell_ratio_action(client, body);
        return;
    }
    if (strcmp(action, "create-catalog-item") == 0 ||
        strcmp(action, "save-catalog-item") == 0 ||
        strcmp(action, "delete-catalog-item") == 0 ||
        strcmp(action, "create-catalog-equip") == 0 ||
        strcmp(action, "save-catalog-equip") == 0 ||
        strcmp(action, "delete-catalog-equip") == 0 ||
        strcmp(action, "create-catalog-skill") == 0 ||
        strcmp(action, "save-catalog-skill") == 0 ||
        strcmp(action, "delete-catalog-skill") == 0)
    {
        vm_mock_admin_handle_catalog_action(client, action, body);
        return;
    }
    if (strcmp(action, "chest-upsert") == 0 ||
        strcmp(action, "chest-delete") == 0 ||
        strcmp(action, "chest-clear") == 0 ||
        strcmp(action, "chest-import-legacy") == 0 ||
        strcmp(action, "chest-import-crystal") == 0 ||
        strcmp(action, "chest-pity-save") == 0 ||
        strcmp(action, "chest-batch-category") == 0 ||
        strcmp(action, "chest-batch-quality") == 0)
    {
        vm_mock_admin_handle_chest_action(client, action, body);
        return;
    }
    if (strcmp(action, "enhance-milestone-upsert") == 0 ||
        strcmp(action, "enhance-milestone-save-all") == 0 ||
        strcmp(action, "enhance-pool-upsert") == 0 ||
        strcmp(action, "enhance-pool-save-all") == 0 ||
        strcmp(action, "enhance-wash-settings-save") == 0 ||
        strcmp(action, "enhance-wash-quality-scale-save") == 0 ||
        strcmp(action, "enhance-gold-cost-save") == 0 ||
        strcmp(action, "enhance-base-scale-save") == 0)
    {
        vm_mock_admin_handle_enhance_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-gem-config") == 0)
    {
        vm_mock_admin_handle_gem_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-client-ui-config") == 0)
    {
        vm_mock_admin_handle_client_ui_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-combat-formula") == 0)
    {
        vm_mock_admin_handle_combat_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-arena-config") == 0 ||
        strcmp(action, "save-arena-reward") == 0 ||
        strcmp(action, "save-arena-wager") == 0 ||
        strcmp(action, "save-tianxia-statue") == 0)
    {
        vm_mock_admin_handle_arena_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-world-boss-config") == 0 ||
        strcmp(action, "world-boss-force-preview") == 0 ||
        strcmp(action, "world-boss-force-spawn") == 0 ||
        strcmp(action, "world-boss-force-despawn") == 0)
    {
        vm_mock_admin_handle_world_boss_action(client, action, body);
        return;
    }
    if (strcmp(action, "maintenance-start") == 0 ||
        strcmp(action, "maintenance-stop") == 0 ||
        strcmp(action, "wipe-all-player-data") == 0)
    {
        vm_mock_admin_handle_maintenance_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-ladder-config") == 0 ||
        strcmp(action, "save-ladder-reward") == 0)
    {
        vm_mock_admin_handle_ladder_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-fruit-config") == 0 ||
        strcmp(action, "save-fruit-wheel") == 0 ||
        strcmp(action, "reset-fruit-wheel") == 0)
    {
        vm_mock_admin_handle_fruit_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-guess-config") == 0)
    {
        vm_mock_admin_handle_guess_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-auction-config") == 0)
    {
        vm_mock_admin_handle_auction_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-zangbao-config") == 0 ||
        strcmp(action, "zangbao-withdraw-approve") == 0 ||
        strcmp(action, "zangbao-withdraw-reject") == 0)
    {
        vm_mock_admin_handle_zangbao_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-festival-config") == 0 ||
        strcmp(action, "save-festival-exchange") == 0 ||
        strcmp(action, "save-yanmen-config") == 0 ||
        strcmp(action, "save-yanmen-exchange") == 0 ||
        strcmp(action, "save-ring-config") == 0 ||
        strcmp(action, "save-vitality-online") == 0)
    {
        vm_mock_admin_handle_activities_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-login-server") == 0 ||
        strcmp(action, "create-login-server") == 0 ||
        strcmp(action, "delete-login-server") == 0)
    {
        vm_mock_admin_handle_login_server_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-gateway-zone") == 0 ||
        strcmp(action, "create-gateway-zone") == 0 ||
        strcmp(action, "delete-gateway-zone") == 0)
    {
        vm_mock_admin_handle_gateway_zone_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-registration-config") == 0)
    {
        u32 dailyLimit = 0;
        char keepAccount[64];

        memset(keepAccount, 0, sizeof(keepAccount));
        (void)vm_mock_admin_form_value(body, "account", keepAccount,
                                       sizeof(keepAccount));
        if (!vm_mock_admin_form_u32(body, "daily_limit", 9999u, &dailyLimit))
        {
            vm_mock_admin_redirect(client, keepAccount, "error",
                                   "每日注册上限无效（0-9999）");
            return;
        }
        if (!vm_mock_service_register_daily_limit_admin_save(dailyLimit))
        {
            const char *mysqlErr = vm_mysql_last_error();
            char errMsg[384];

            memset(errMsg, 0, sizeof(errMsg));
            if (mysqlErr != NULL && mysqlErr[0] != 0 &&
                strcmp(mysqlErr, "no MySQL error detail") != 0)
                snprintf(errMsg, sizeof(errMsg), "保存失败：%s", mysqlErr);
            else
                snprintf(errMsg, sizeof(errMsg), "%s", "保存失败");
            vm_mock_admin_redirect(client, keepAccount, "error", errMsg);
            return;
        }
        {
            char okMsg[96];
            snprintf(okMsg, sizeof(okMsg), "每日注册上限已设为 %u", dailyLimit);
            vm_mock_admin_redirect(client, keepAccount, "ok", okMsg);
        }
        return;
    }
    if (strcmp(action, "save-practise-exp-mult") == 0)
    {
        u32 mult = 0;
        char keepAccount[64];

        memset(keepAccount, 0, sizeof(keepAccount));
        (void)vm_mock_admin_form_value(body, "account", keepAccount,
                                       sizeof(keepAccount));
        if (!vm_mock_admin_form_u32(body, "exp_level_minute_mult", 1000u, &mult))
        {
            vm_mock_admin_redirect(client, keepAccount, "error",
                                   "修炼丹经验系数无效（0-1000）");
            return;
        }
        if (!vm_net_mock_practise_exp_mult_admin_save(mult))
        {
            const char *mysqlErr = vm_mysql_last_error();
            char errMsg[384];

            memset(errMsg, 0, sizeof(errMsg));
            if (mysqlErr != NULL && mysqlErr[0] != 0 &&
                strcmp(mysqlErr, "no MySQL error detail") != 0)
                snprintf(errMsg, sizeof(errMsg), "保存失败：%s", mysqlErr);
            else
                snprintf(errMsg, sizeof(errMsg), "%s", "保存失败");
            vm_mock_admin_redirect(client, keepAccount, "error", errMsg);
            return;
        }
        {
            char okMsg[96];
            snprintf(okMsg, sizeof(okMsg), "修炼丹经验系数已设为 %u", mult);
            vm_mock_admin_redirect(client, keepAccount, "ok", okMsg);
        }
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
    if (strcmp(action, "rename-role") == 0)
    {
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "name", nameUtf8, sizeof(nameUtf8)))
        {
            vm_mock_admin_redirect(client, account, "error", "角色名参数不完整");
            return;
        }
        if (!vm_mock_admin_utf8_to_gbk_text(nameUtf8, nameGbk, sizeof(nameGbk), false) ||
            strlen(nameGbk) >= 32u)
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "角色名无效或过长（GBK 最多 31 字节）");
            return;
        }
        ok = vm_mock_service_account_set_role_name(account, role, nameGbk, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "角色改名成功"
                                  : (error ? error : "角色改名失败"));
        return;
    }
    if (strcmp(action, "set-role-exp") == 0)
    {
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "exp", amountText, sizeof(amountText)) ||
            !vm_net_mock_parse_u32_strict(amountText, &amount))
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "总经验必须是 0 或正整数");
            return;
        }
        ok = vm_mock_service_account_set_role_exp(account, role, amount, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "总经验已更新"
                                  : (error ? error : "总经验修改失败"));
        return;
    }
    if (strcmp(action, "clear-ring-task") == 0)
    {
        u32 roleId = 0;

        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_net_mock_parse_u32_strict(role, &roleId) || roleId == 0)
        {
            vm_mock_admin_redirect(client, account, "error", "角色参数无效");
            return;
        }
        ok = vm_mock_ring_admin_clear_active_task(account, roleId, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "已移除当前跑环任务"
                                  : (error ? error : "移除跑环任务失败"));
        return;
    }
    if (strcmp(action, "add-money") == 0)
    {
        int32_t delta = 0;

        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "amount", amountText, sizeof(amountText)) ||
            !vm_net_mock_parse_i32_strict(amountText, &delta) || delta == 0)
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "金额必须是非零整数（正数增加，负数扣除）");
            return;
        }
        ok = vm_mock_service_account_add_role_money(account, role, delta, NULL, NULL, &error);
        vm_mock_admin_redirect(
            client, account, ok ? "ok" : "error",
            ok ? (delta > 0 ? "普通钱币增加成功" : "普通钱币扣除成功")
               : (error ? error : "余额修改失败"));
        return;
    }
    if (strcmp(action, "add-wcoin") == 0)
    {
        int32_t delta = 0;

        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "amount", amountText, sizeof(amountText)) ||
            !vm_net_mock_parse_i32_strict(amountText, &delta) || delta == 0)
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "数量必须是非零整数（正数增加，负数扣除）");
            return;
        }
        ok = vm_mock_service_account_add_role_wcoin(account, role, delta, &error);
        vm_mock_admin_redirect(
            client, account, ok ? "ok" : "error",
            ok ? (delta > 0 ? "W 币增加成功" : "W 币扣除成功")
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
            amount > 1000)
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
    if (strcmp(action, "set-equip-wash") == 0)
    {
        char where[16];
        char locText[32];
        char unlockText[16];
        char washedText[16];
        char typeText[16];
        char flagText[16];
        char valueText[32];
        char countText[32];
        u32 loc = 0;
        u32 unlock = 0;
        u32 washed = 0;
        u32 type = 0;
        u32 flag = 0;
        u32 value = 0;
        u32 washCount = 0;

        memset(where, 0, sizeof(where));
        memset(locText, 0, sizeof(locText));
        memset(unlockText, 0, sizeof(unlockText));
        memset(washedText, 0, sizeof(washedText));
        memset(typeText, 0, sizeof(typeText));
        memset(flagText, 0, sizeof(flagText));
        memset(valueText, 0, sizeof(valueText));
        memset(countText, 0, sizeof(countText));
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "where", where, sizeof(where)) ||
            !vm_mock_admin_form_value(body, "loc", locText, sizeof(locText)) ||
            !vm_mock_admin_form_value(body, "unlock", unlockText,
                                      sizeof(unlockText)) ||
            !vm_mock_admin_form_value(body, "washed", washedText,
                                      sizeof(washedText)) ||
            !vm_mock_admin_form_value(body, "type", typeText,
                                      sizeof(typeText)) ||
            !vm_mock_admin_form_value(body, "flag", flagText,
                                      sizeof(flagText)) ||
            !vm_mock_admin_form_value(body, "value", valueText,
                                      sizeof(valueText)) ||
            !vm_mock_admin_form_value(body, "wash_count", countText,
                                      sizeof(countText)) ||
            !vm_net_mock_parse_u32_strict(locText, &loc) ||
            !vm_net_mock_parse_u32_strict(unlockText, &unlock) ||
            !vm_net_mock_parse_u32_strict(washedText, &washed) ||
            !vm_net_mock_parse_u32_strict(typeText, &type) ||
            !vm_net_mock_parse_u32_strict(flagText, &flag) ||
            !vm_net_mock_parse_u32_strict(valueText, &value) ||
            !vm_net_mock_parse_u32_strict(countText, &washCount) ||
            unlock > 255u || washed > 1u || type > 255u || flag > 1u ||
            value > 65535u || washCount > 65535u)
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "洗练参数无效");
            return;
        }
        ok = vm_mock_service_account_set_role_equip_wash(
            account, role, where, loc, (u8)unlock, (u8)washed, (u8)type,
            (u8)flag, (u16)value, (u16)washCount, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "洗练属性已更新"
                                  : (error ? error : "洗练属性调整失败"));
        return;
    }
    if (strcmp(action, "grant-cosmetic") == 0)
    {
        char kind[32];
        char idText[32];
        char allText[8];
        u32 cosmeticId = 0;
        u32 granted = 0;
        bool unlockAll = false;
        kind[0] = 0;
        idText[0] = 0;
        allText[0] = 0;
        (void)vm_mock_admin_form_value(body, "kind", kind, sizeof(kind));
        (void)vm_mock_admin_form_value(body, "id", idText, sizeof(idText));
        (void)vm_mock_admin_form_value(body, "all", allText, sizeof(allText));
        unlockAll = allText[0] == '1';
        if (!unlockAll && idText[0] != 0)
            (void)vm_net_mock_parse_u32_strict(idText, &cosmeticId);
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            kind[0] == 0 || (!unlockAll && cosmeticId == 0))
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "外观参数无效（需类型，且 ID 或勾选全部）");
            return;
        }
        ok = vm_mock_service_account_grant_role_cosmetic(
            account, role, kind, cosmeticId, unlockAll, &granted, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "外观解锁成功" :
                                    (error ? error : "外观解锁失败"));
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
        ".recharge{margin-top:18px}.recharge h2{font-size:18px;margin:0 0 4px}.recharge-form{display:grid;grid-template-columns:1.2fr .8fr .8fr auto;gap:10px;align-items:end;margin-top:15px}.recharge-form label{display:grid;gap:4px;color:#475467}.recharge-form input,.recharge-form select{width:100%%;border:1px solid #d0d5dd;border-radius:8px;padding:9px 10px;font:inherit;background:#fff}.recharge-form button{border:0;border-radius:8px;padding:10px 14px;background:#175cd3;color:#fff;font-weight:700;cursor:pointer}.recharge-unavailable{padding:12px;border-radius:9px;background:#f2f4f7;color:#667085}.recharge-history{margin-top:18px;border-top:1px solid #eaecf0;padding-top:13px}.recharge-history h3{font-size:14px;margin:0 0 6px}.recharge-history a{display:flex;justify-content:space-between;gap:12px;padding:7px 0;color:#344054;text-decoration:none}.recharge-history a strong{color:#175cd3;font-size:12px}.recharge-note{color:#98a2b3;font-size:12px;margin:13px 0 0}@media(max-width:900px){.recharge-form{grid-template-columns:1fr 1fr}.recharge-form button{align-self:stretch}}@media(max-width:720px){.recharge-form{grid-template-columns:1fr}}"
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
        vm_mock_payment_render_dashboard(&page, accountId);
        vm_mock_service_close_account_role_db_for_management(accountState, false);
    }
    else
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"card empty\">%s</div>",
            roleError ? roleError : "该账号尚未创建角色，请进入游戏创建角色。");
        if (accountState != NULL)
        {
            vm_mock_payment_render_dashboard(&page, accountId);
            vm_mock_service_close_account_role_db_for_management(accountState, false);
        }
        else
        {
            vm_mock_admin_text_appendf(&page, "</section>");
        }
    }
    vm_mock_admin_text_appendf(&page,
        "<section class=\"card security\"><div><h2>账号安全</h2><p class=\"sub\">修改后请使用新密码登录游戏和账号中心。其他网页登录会话将自动退出。</p></div>"
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

/* Dispatch assumes the HTTP request is already fully buffered.  Callers must
 * hold g_vm_mock_service_protocol_mutex around this function. */
static int vm_mock_admin_dispatch_client(vm_mock_service_socket client,
                                          const char *request,
                                          size_t headerLen,
                                          char *method,
                                          char *target,
                                          char *query,
                                          char *body)
{
    char *response = NULL;

    if (request == NULL || method == NULL || target == NULL || query == NULL)
        return 0;

    if (strcmp(method, "GET") == 0 && strcmp(target, "/healthz") == 0)
    {
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/json; charset=utf-8", NULL,
                                    "{\"ok\":true,\"service\":\"jianghu-admin\"}\n");
        return 1;
    }
    /* Public gateway zone catalog for Pixi pre-login zone picker. */
    if (strcmp(method, "GET") == 0 && strcmp(target, "/api/zones") == 0)
    {
        vm_net_mock_gateway_zone rows[VM_NET_MOCK_GATEWAY_ZONE_MAX];
        char *json = NULL;
        size_t jsonCap = 0;
        size_t jsonLen = 0;
        u32 rowCount = 0;
        u32 defaultZoneId = 0;
        u32 written = 0;
        int ok = 1;
        const char *cors = "Access-Control-Allow-Origin: *\r\n";

        memset(rows, 0, sizeof(rows));
        rowCount = vm_net_mock_gateway_zone_public_list(
            rows, VM_NET_MOCK_GATEWAY_ZONE_MAX, &defaultZoneId);
        jsonCap = 128u + (size_t)rowCount * 384u;
        if (jsonCap < 256u)
            jsonCap = 256u;
        json = (char *)malloc(jsonCap);
        if (json == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8", cors,
                                        "{\"ok\":false,\"error\":\"oom\"}\n");
            return 0;
        }
        jsonLen = (size_t)snprintf(json, jsonCap,
                                   "{\"ok\":true,\"defaultZoneId\":%u,\"zones\":[",
                                   defaultZoneId);
        if (jsonLen >= jsonCap)
            ok = 0;
        for (u32 i = 0; ok && i < rowCount; ++i)
        {
            char nameEsc[256];
            char labelEsc[256];
            char wsEsc[512];
            char webEsc[512];
            char piece[1400];
            int pieceLen;

            memset(nameEsc, 0, sizeof(nameEsc));
            memset(labelEsc, 0, sizeof(labelEsc));
            memset(wsEsc, 0, sizeof(wsEsc));
            memset(webEsc, 0, sizeof(webEsc));
            if (!vm_mock_admin_json_escape(rows[i].displayName, nameEsc,
                                           sizeof(nameEsc)) ||
                !vm_mock_admin_json_escape(rows[i].label, labelEsc,
                                           sizeof(labelEsc)) ||
                !vm_mock_admin_json_escape(rows[i].wsUrl, wsEsc,
                                           sizeof(wsEsc)) ||
                !vm_mock_admin_json_escape(rows[i].accountWebUrl, webEsc,
                                           sizeof(webEsc)))
            {
                ok = 0;
                break;
            }
            pieceLen = snprintf(
                piece, sizeof(piece),
                "%s{\"zoneId\":%u,\"name\":\"%s\",\"label\":\"%s\","
                "\"wsUrl\":\"%s\",\"accountWebUrl\":\"%s\"}",
                written ? "," : "", rows[i].zoneId, nameEsc, labelEsc, wsEsc,
                webEsc);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 4u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++written;
        }
        if (ok && jsonLen + 4u < jsonCap)
        {
            memcpy(json + jsonLen, "]}\n", 3);
            jsonLen += 3;
            json[jsonLen] = 0;
            vm_mock_admin_send_response(client, "200 OK",
                                        "application/json; charset=utf-8",
                                        cors, json);
            free(json);
            return 1;
        }
        free(json);
        vm_mock_admin_send_response(client, "500 Internal Server Error",
                                    "application/json; charset=utf-8", cors,
                                    "{\"ok\":false,\"error\":\"encode\"}\n");
        return 0;
    }
    /* Public custom catalog for Pixi (admin-created item/equip/skill extras). */
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, "/api/custom-catalog") == 0)
    {
        char *json = NULL;
        size_t jsonCap = 0;
        size_t jsonLen = 0;
        int ok = 1;
        const char *cors = "Access-Control-Allow-Origin: *\r\n";
        u32 itemWritten = 0;
        u32 equipWritten = 0;
        u32 skillWritten = 0;

        (void)vm_net_mock_catalog_extra_db_force_reload();
        jsonCap = 512u +
                  (size_t)g_vm_net_mock_item_catalog_extra_count * 320u +
                  (size_t)g_vm_net_mock_equip_catalog_extra_count * 360u +
                  (size_t)g_vm_net_mock_skill_catalog_extra_count * 280u;
        if (jsonCap < 1024u)
            jsonCap = 1024u;
        json = (char *)malloc(jsonCap);
        if (json == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8", cors,
                                        "{\"ok\":false,\"error\":\"oom\"}\n");
            return 0;
        }
        jsonLen = 0;
        {
            char head[48];
            int headLen = snprintf(head, sizeof(head),
                                   "{\"rev\":%u,\"items\":[",
                                   g_vm_net_mock_catalog_extra_rev);
            if (headLen <= 0 || (size_t)headLen >= sizeof(head) ||
                (size_t)headLen >= jsonCap)
            {
                free(json);
                vm_mock_admin_send_response(
                    client, "500 Internal Server Error",
                    "application/json; charset=utf-8", cors,
                    "{\"ok\":false,\"error\":\"encode\"}\n");
                return 0;
            }
            memcpy(json, head, (size_t)headLen);
            jsonLen = (size_t)headLen;
        }
        for (u32 i = 0; ok && i < g_vm_net_mock_item_catalog_extra_count; ++i)
        {
            const vm_net_mock_item_catalog_extra_row *row =
                &g_vm_net_mock_item_catalog_extra[i];
            char nameUtf8[128];
            char descUtf8[384];
            char nameEsc[256];
            char descEsc[768];
            char piece[1200];
            int pieceLen;

            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(descUtf8, 0, sizeof(descUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->description, descUtf8,
                                          sizeof(descUtf8));
            if (!vm_mock_admin_json_escape(nameUtf8, nameEsc, sizeof(nameEsc)) ||
                !vm_mock_admin_json_escape(descUtf8, descEsc, sizeof(descEsc)))
            {
                ok = 0;
                break;
            }
            pieceLen = snprintf(
                piece, sizeof(piece),
                "%s{\"id\":%u,\"name\":\"%s\",\"desc\":\"%s\",\"category\":%u,"
                "\"level\":%u,\"stack\":%u,\"consumeMode\":%u,"
                "\"durationMinutes\":%u,\"hp\":%u,\"mp\":%u,\"exp\":%u,"
                "\"questLink\":%u,\"bindAttr\":%u,\"visual\":%u,\"price\":%u,"
                "\"coolPrice\":%u,\"shopEnabled\":%u}",
                itemWritten ? "," : "", row->itemId, nameEsc, descEsc,
                row->category, row->levelRequired, row->stack, row->consumeMode,
                row->durationMinutes, row->hp, row->mp, row->exp, row->questLink,
                row->bindAttr, row->visual, row->price, row->coolPrice,
                row->shopEnabled ? 1u : 0u);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 64u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++itemWritten;
        }
        if (ok && jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "],\"equips\":[", 12);
            jsonLen += 12;
        }
        else
            ok = 0;
        for (u32 i = 0; ok && i < g_vm_net_mock_equip_catalog_extra_count; ++i)
        {
            const vm_net_mock_equip_catalog_extra_row *row =
                &g_vm_net_mock_equip_catalog_extra[i];
            char nameUtf8[128];
            char descUtf8[384];
            char nameEsc[256];
            char descEsc[768];
            char piece[1400];
            int pieceLen;

            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(descUtf8, 0, sizeof(descUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->description, descUtf8,
                                          sizeof(descUtf8));
            if (!vm_mock_admin_json_escape(nameUtf8, nameEsc, sizeof(nameEsc)) ||
                !vm_mock_admin_json_escape(descUtf8, descEsc, sizeof(descEsc)))
            {
                ok = 0;
                break;
            }
            pieceLen = snprintf(
                piece, sizeof(piece),
                "%s{\"id\":%u,\"name\":\"%s\",\"desc\":\"%s\",\"category\":%u,"
                "\"level\":%u,\"quality\":%u,\"durabilityMax\":%u,\"attack\":%u,"
                "\"armor\":%u,\"hp\":%u,\"mp\":%u,\"strength\":%u,\"agility\":%u,"
                "\"wisdom\":%u,\"dodge\":%u,\"hit\":%u,\"crit\":%u,\"resist\":%u,"
                "\"price\":%u,\"shopEnabled\":%u}",
                equipWritten ? "," : "", row->itemId, nameEsc, descEsc,
                row->category, row->levelRequired, row->quality,
                row->durabilityMax, row->bonus.attack, row->bonus.armor,
                row->bonus.hp, row->bonus.mp, row->bonus.strength,
                row->bonus.agility, row->bonus.wisdom, row->bonus.dodge,
                row->bonus.hit, row->bonus.crit, row->bonus.resist, row->price,
                row->shopEnabled ? 1u : 0u);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 64u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++equipWritten;
        }
        if (ok && jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "],\"skills\":[", 12);
            jsonLen += 12;
        }
        else
            ok = 0;
        for (u32 i = 0; ok && i < g_vm_net_mock_skill_catalog_extra_count; ++i)
        {
            const vm_net_mock_skill_catalog_extra_row *row =
                &g_vm_net_mock_skill_catalog_extra[i];
            char nameUtf8[128];
            char descUtf8[384];
            char nameEsc[256];
            char descEsc[768];
            char piece[1200];
            int pieceLen;

            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(descUtf8, 0, sizeof(descUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->description, descUtf8,
                                          sizeof(descUtf8));
            if (!vm_mock_admin_json_escape(nameUtf8, nameEsc, sizeof(nameEsc)) ||
                !vm_mock_admin_json_escape(descUtf8, descEsc, sizeof(descEsc)))
            {
                ok = 0;
                break;
            }
            pieceLen = snprintf(
                piece, sizeof(piece),
                "%s{\"id\":%u,\"name\":\"%s\",\"desc\":\"%s\",\"effectIndex\":%u,"
                "\"level\":%u,\"rawJob\":%u,\"learnPrice\":%u,\"useLimit\":%u,"
                "\"targetKind\":%u,\"mpCost\":%u,\"hpChange\":%d}",
                skillWritten ? "," : "", row->skillId, nameEsc, descEsc,
                row->effectIndex, row->levelRequired, row->rawJob,
                row->learnPrice, row->useLimit, row->targetDirection,
                row->mpCost, row->hpChange);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 64u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++skillWritten;
        }
        if (ok && jsonLen + 4u < jsonCap)
        {
            memcpy(json + jsonLen, "]}\n", 3);
            jsonLen += 3;
            json[jsonLen] = 0;
            vm_mock_admin_send_response(client, "200 OK",
                                        "application/json; charset=utf-8",
                                        cors, json);
            free(json);
            return 1;
        }
        free(json);
        vm_mock_admin_send_response(client, "500 Internal Server Error",
                                    "application/json; charset=utf-8", cors,
                                    "{\"ok\":false,\"error\":\"encode\"}\n");
        return 0;
    }
    /* Public catalog for Pixi map nameplates / battle visuals (gateway proxy):
     * overrides=levels, actors, names (UTF-8), reals=wire→real spawn bindings,
     * scenes=enemyId→firstScene, items=itemId→drop firstScene.
     * Nameplates must key by REAL id, never SCE keep-id wire (#250). */
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, "/api/monster-levels") == 0)
    {
        char *json = NULL;
        size_t jsonCap = 0;
        size_t jsonLen = 0;
        u32 levelWritten = 0;
        u32 actorWritten = 0;
        u32 nameWritten = 0;
        u32 realWritten = 0;
        int ok = 0;
        vm_net_mock_monster_admin_row *rows = NULL;
        u32 rowTotal = 0;
        u32 rowCap = 0;

        (void)vm_net_mock_monster_db_load();
        vm_net_mock_sce_combat_spawn_bindings_ensure_loaded();
        rowTotal = vm_net_mock_monster_admin_list(NULL, 0);
        rowCap = rowTotal ? rowTotal : 1u;
        rows = (vm_net_mock_monster_admin_row *)calloc(
            rowCap, sizeof(vm_net_mock_monster_admin_row));
        if (rows == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"oom\"}\n");
            return 0;
        }
        rowTotal = vm_net_mock_monster_admin_list(rows, rowCap);
        jsonCap = 256u + (size_t)rowTotal * 280u +
                  (size_t)g_vm_net_mock_sce_combat_spawn_binding_count * 96u +
                  16384u;
        if (jsonCap < 512u)
            jsonCap = 512u;
        json = (char *)malloc(jsonCap);
        if (json == NULL)
        {
            free(rows);
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"oom\"}\n");
            return 0;
        }
        jsonLen = 0;
        ok = 1;
        if (jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "{\"overrides\":{", 14);
            jsonLen += 14;
        }
        else
            ok = 0;
        for (u32 i = 0; ok && i < rowTotal; ++i)
        {
            char piece[40];
            int pieceLen;

            if (rows[i].enemyId == 0 || rows[i].level == 0)
                continue;
            pieceLen = snprintf(piece, sizeof(piece), "%s\"%u\":%u",
                                levelWritten ? "," : "", rows[i].enemyId,
                                rows[i].level);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 4u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++levelWritten;
        }
        if (ok && jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "},\"actors\":{", 12);
            jsonLen += 12;
        }
        else
            ok = 0;
        for (u32 i = 0; ok && i < rowTotal; ++i)
        {
            const char *actor = rows[i].actorResource;
            char piece[96];
            int pieceLen;
            size_t alen;
            int safe;

            if (rows[i].enemyId == 0 || actor[0] == 0)
                continue;
            alen = strlen(actor);
            safe = alen > 0 && alen < 64;
            for (size_t a = 0; safe && a < alen; ++a)
            {
                unsigned char c = (unsigned char)actor[a];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                      c == '-' || c == '/'))
                    safe = 0;
            }
            if (!safe)
                continue;
            pieceLen = snprintf(piece, sizeof(piece), "%s\"%u\":\"%s\"",
                                actorWritten ? "," : "", rows[i].enemyId,
                                actor);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 4u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++actorWritten;
        }
        if (ok && jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "},\"names\":{", 11);
            jsonLen += 11;
        }
        else
            ok = 0;
        for (u32 i = 0; ok && i < rowTotal; ++i)
        {
            char nameUtf8[128];
            char nameEsc[256];
            char piece[320];
            int pieceLen;

            if (rows[i].enemyId == 0 || rows[i].displayName[0] == 0)
                continue;
            memset(nameUtf8, 0, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(rows[i].displayName, nameUtf8,
                                          sizeof(nameUtf8));
            if (nameUtf8[0] == 0)
                continue;
            if (!vm_mock_admin_json_escape(nameUtf8, nameEsc, sizeof(nameEsc)))
            {
                ok = 0;
                break;
            }
            pieceLen = snprintf(piece, sizeof(piece), "%s\"%u\":\"%s\"",
                                nameWritten ? "," : "", rows[i].enemyId,
                                nameEsc);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 4u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++nameWritten;
        }
        if (ok && jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "},\"reals\":[", 11);
            jsonLen += 11;
        }
        else
            ok = 0;
        for (u32 i = 0; ok && i < g_vm_net_mock_sce_combat_spawn_binding_count;
             ++i)
        {
            const vm_net_mock_sce_combat_spawn_binding *bind =
                &g_vm_net_mock_sce_combat_spawn_bindings[i];
            char sceneUtf8[192];
            char sceneEsc[384];
            char piece[512];
            int pieceLen;

            if (bind->scene[0] == 0 || bind->x == 0 || bind->y == 0 ||
                bind->wireActorId == 0 || bind->realActorId == 0)
                continue;
            memset(sceneUtf8, 0, sizeof(sceneUtf8));
            vm_net_mock_gbk_label_to_utf8(bind->scene, sceneUtf8,
                                          sizeof(sceneUtf8));
            if (sceneUtf8[0] == 0)
                continue;
            if (!vm_mock_admin_json_escape(sceneUtf8, sceneEsc, sizeof(sceneEsc)))
            {
                ok = 0;
                break;
            }
            pieceLen = snprintf(
                piece, sizeof(piece),
                "%s{\"s\":\"%s\",\"x\":%u,\"y\":%u,\"w\":%u,\"r\":%u}",
                realWritten ? "," : "", sceneEsc, (u32)bind->x, (u32)bind->y,
                (u32)bind->wireActorId, (u32)bind->realActorId);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 4u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++realWritten;
        }
        if (ok && jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "],\"scenes\":{", 12);
            jsonLen += 12;
        }
        else
            ok = 0;
        {
            u32 sceneWritten = 0;

            for (u32 i = 0; ok && i < rowTotal; ++i)
            {
                char sceneUtf8[192];
                char sceneEsc[384];
                char piece[512];
                int pieceLen;

                if (rows[i].enemyId == 0)
                    continue;
                memset(sceneUtf8, 0, sizeof(sceneUtf8));
                if (rows[i].firstScene[0] == 0)
                    continue;
                if (strstr(rows[i].firstScene, ".dsh") != NULL ||
                    strstr(rows[i].firstScene, ".DSH") != NULL)
                    continue;
                vm_net_mock_gbk_label_to_utf8(rows[i].firstScene, sceneUtf8,
                                              sizeof(sceneUtf8));
                if (sceneUtf8[0] == 0 || strstr(sceneUtf8, ".dsh") != NULL ||
                    strstr(sceneUtf8, ".DSH") != NULL)
                    continue;
                if (!vm_mock_admin_json_escape(sceneUtf8, sceneEsc,
                                               sizeof(sceneEsc)))
                    continue;
                pieceLen = snprintf(piece, sizeof(piece), "%s\"%u\":\"%s\"",
                                    sceneWritten ? "," : "", rows[i].enemyId,
                                    sceneEsc);
                if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                    jsonLen + (size_t)pieceLen + 32u >= jsonCap)
                    break;
                memcpy(json + jsonLen, piece, (size_t)pieceLen);
                jsonLen += (size_t)pieceLen;
                ++sceneWritten;
            }
        }
        if (ok && jsonLen + 16u < jsonCap)
        {
            memcpy(json + jsonLen, "},\"items\":{", 11);
            jsonLen += 11;
        }
        else
            ok = 0;
        {
            u32 itemSceneIds[256];
            u32 itemSceneCount = 0;
            u32 itemWritten = 0;

            memset(itemSceneIds, 0, sizeof(itemSceneIds));
            for (u32 i = 0; ok && i < rowTotal; ++i)
            {
                char sceneUtf8[192];
                char sceneEsc[384];
                u32 d;

                if (rows[i].dropCount == 0)
                    continue;
                memset(sceneUtf8, 0, sizeof(sceneUtf8));
                if (rows[i].firstScene[0] == 0)
                    continue;
                if (strstr(rows[i].firstScene, ".dsh") != NULL ||
                    strstr(rows[i].firstScene, ".DSH") != NULL)
                    continue;
                vm_net_mock_gbk_label_to_utf8(rows[i].firstScene, sceneUtf8,
                                              sizeof(sceneUtf8));
                if (sceneUtf8[0] == 0 || strstr(sceneUtf8, ".dsh") != NULL ||
                    strstr(sceneUtf8, ".DSH") != NULL)
                    continue;
                if (!vm_mock_admin_json_escape(sceneUtf8, sceneEsc,
                                               sizeof(sceneEsc)))
                    continue;
                for (d = 0; d < rows[i].dropCount && itemSceneCount < 256; ++d)
                {
                    char piece[512];
                    int pieceLen;
                    u32 itemId = rows[i].drops[d].itemId;
                    u32 seen = 0;

                    if (itemId == 0)
                        continue;
                    for (u32 s = 0; s < itemSceneCount; ++s)
                    {
                        if (itemSceneIds[s] == itemId)
                        {
                            seen = 1;
                            break;
                        }
                    }
                    if (seen)
                        continue;
                    pieceLen = snprintf(piece, sizeof(piece), "%s\"%u\":\"%s\"",
                                        itemWritten ? "," : "", itemId,
                                        sceneEsc);
                    if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                        jsonLen + (size_t)pieceLen + 32u >= jsonCap)
                        break;
                    memcpy(json + jsonLen, piece, (size_t)pieceLen);
                    jsonLen += (size_t)pieceLen;
                    itemSceneIds[itemSceneCount++] = itemId;
                    ++itemWritten;
                }
            }
        }
        if (ok && jsonLen + 4u < jsonCap)
        {
            memcpy(json + jsonLen, "}}\n", 3);
            jsonLen += 3;
            json[jsonLen] = 0;
            vm_mock_admin_send_response(client, "200 OK",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        json);
        }
        else
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"build\"}\n");
        }
        free(json);
        free(rows);
        (void)levelWritten;
        (void)actorWritten;
        (void)nameWritten;
        (void)realWritten;
        return 1;
    }
    /* Pixi world-boss map overlay — DB/runtime only (no .sce inject). */
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, "/api/world-boss-active") == 0)
    {
        char scene[64];
        char sceneUtf8[128];
        char sceneEsc[256];
        char nameUtf8[128];
        char nameEsc[256];
        char actorEsc[192];
        char nameGbk[32];
        const char *actor = NULL;
        char json[768];
        u8 status = 0;
        u16 x = 0;
        u16 y = 0;
        u32 wire = 0;
        u32 monster = 0;
        u8 visual0 = 0;
        u8 visual1 = 0;
        const vm_mock_world_boss_config *cfg;

        memset(scene, 0, sizeof(scene));
        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        memset(nameUtf8, 0, sizeof(nameUtf8));
        memset(nameGbk, 0, sizeof(nameGbk));
        cfg = vm_net_mock_world_boss_config_get();
        vm_mock_world_boss_admin_runtime_get(&status, scene, sizeof(scene), &x,
                                             &y);
        if (status == 2 &&
            vm_mock_world_boss_active_anchor(scene, sizeof(scene), &x, &y, &wire,
                                            &monster))
        {
            if (scene[0] != 0)
                vm_net_mock_gbk_label_to_utf8(scene, sceneUtf8,
                                              sizeof(sceneUtf8));
            if (monster != 0 &&
                vm_net_mock_monster_battle_left_appearance(
                    monster, nameGbk, sizeof(nameGbk), &visual0, &visual1,
                    &actor) &&
                nameGbk[0] != 0)
            {
                vm_net_mock_gbk_label_to_utf8(nameGbk, nameUtf8,
                                              sizeof(nameUtf8));
            }
            if (!vm_mock_admin_json_escape(
                    sceneUtf8[0] ? sceneUtf8 : scene, sceneEsc,
                    sizeof(sceneEsc)) ||
                !vm_mock_admin_json_escape(nameUtf8[0] ? nameUtf8 : "世界BOSS",
                                           nameEsc, sizeof(nameEsc)) ||
                !vm_mock_admin_json_escape(actor ? actor : "", actorEsc,
                                           sizeof(actorEsc)))
            {
                vm_mock_admin_send_response(
                    client, "500 Internal Server Error",
                    "application/json; charset=utf-8",
                    "Access-Control-Allow-Origin: *\r\n",
                    "{\"ok\":false,\"error\":\"escape\"}\n");
                return 0;
            }
            snprintf(json, sizeof(json),
                     "{\"ok\":true,\"status\":%u,\"scene\":\"%s\",\"x\":%u,"
                     "\"y\":%u,\"monsterId\":%u,\"wireId\":%u,\"name\":\"%s\","
                     "\"actor\":\"%s\"}\n",
                     (u32)status, sceneEsc, (u32)x, (u32)y, monster, wire,
                     nameEsc, actorEsc);
        }
        else
        {
            snprintf(json, sizeof(json),
                     "{\"ok\":true,\"status\":%u,\"scene\":\"\",\"x\":0,\"y\":0,"
                     "\"monsterId\":%u,\"wireId\":0,\"name\":\"\",\"actor\":\"\"}"
                     "\n",
                     (u32)status, cfg ? cfg->monsterId : 0u);
        }
        (void)visual0;
        (void)visual1;
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/json; charset=utf-8",
                                    "Access-Control-Allow-Origin: *\r\n", json);
        return 1;
    }
    /* Pixi quest tracker: display_name → scene from server_dynamic_npcs. */
    if (strcmp(method, "GET") == 0 && strcmp(target, "/api/npc-scenes") == 0)
    {
        char *json = NULL;
        size_t jsonCap = 0;
        size_t jsonLen = 0;
        u32 written = 0;
        int ok = 0;

        (void)vm_net_mock_dynamic_npc_db_load();
        jsonCap = 64u + (size_t)g_vm_net_mock_dynamic_npc_override_count * 96u;
        if (jsonCap < 256u)
            jsonCap = 256u;
        json = (char *)malloc(jsonCap);
        if (json == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"oom\"}\n");
            return 0;
        }
        jsonLen = 0;
        ok = 1;
        if (jsonLen + 24u < jsonCap)
        {
            memcpy(json + jsonLen, "{\"ok\":true,\"scenes\":{", 21);
            jsonLen += 21;
        }
        else
            ok = 0;
        for (u32 i = 0; ok && i < g_vm_net_mock_dynamic_npc_override_count; ++i)
        {
            const vm_net_mock_dynamic_npc_override *row =
                &g_vm_net_mock_dynamic_npc_overrides[i];
            char nameUtf8[128];
            char sceneUtf8[192];
            char nameEsc[256];
            char sceneEsc[384];
            char piece[640];
            int pieceLen;

            if (!row->enabled || row->seed.displayName[0] == 0 ||
                row->scene[0] == 0)
            {
                continue;
            }
            vm_net_mock_gbk_label_to_utf8(row->seed.displayName, nameUtf8,
                                          sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->scene, sceneUtf8,
                                          sizeof(sceneUtf8));
            if (nameUtf8[0] == 0 || sceneUtf8[0] == 0)
                continue;
            if (!vm_mock_admin_json_escape(nameUtf8, nameEsc, sizeof(nameEsc)) ||
                !vm_mock_admin_json_escape(sceneUtf8, sceneEsc, sizeof(sceneEsc)))
            {
                ok = 0;
                break;
            }
            pieceLen = snprintf(piece, sizeof(piece), "%s\"%s\":\"%s\"",
                                written ? "," : "", nameEsc, sceneEsc);
            if (pieceLen <= 0 || (size_t)pieceLen >= sizeof(piece) ||
                jsonLen + (size_t)pieceLen + 4u >= jsonCap)
            {
                ok = 0;
                break;
            }
            memcpy(json + jsonLen, piece, (size_t)pieceLen);
            jsonLen += (size_t)pieceLen;
            ++written;
        }
        if (ok && jsonLen + 4u < jsonCap)
        {
            memcpy(json + jsonLen, "}}\n", 3);
            jsonLen += 3;
            json[jsonLen] = 0;
            vm_mock_admin_send_response(client, "200 OK",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        json);
        }
        else
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"build\"}\n");
        }
        free(json);
        (void)written;
        return 1;
    }
    /* Pixi mall recharge: authenticated create-order (reuse payment core). */
    if (strncmp(target, "/api/zangbao/", 13) == 0 ||
        strncmp(target, "/api/kcoin/", 11) == 0)
    {
        int handled = vm_mock_zangbao_handle_http(client, method, target,
                                                   query, body);
        if (handled)
            return handled;
    }
    if (strcmp(target, "/api/recharge/create") == 0)
    {
        char account[64];
        char password[64];
        char accountGbk[64];
        char passwordGbk[64];
        char accountCanon[64];
        char roleText[32];
        char yuanText[32];
        char typeText[16];
        char payId[64];
        char payUrlEscaped[2048];
        char errorEscaped[256];
        char json[4096];
        u32 roleId = 0;
        u32 yuan = 0;
        u32 payType = 0;
        u32 scratchBalance = 0;
        const char *message = NULL;
        const char *authPath = "none";
        bool authed = false;
        vm_mock_payment_order order;
        const char *cors =
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Access-Control-Max-Age: 86400\r\n";

        if (strcmp(method, "OPTIONS") == 0)
        {
            vm_mock_admin_send_response(client, "204 No Content",
                                        "text/plain; charset=utf-8", cors, "");
            return 1;
        }
        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "Allow: POST, OPTIONS\r\n"
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"method\"}\n");
            return 0;
        }
        memset(account, 0, sizeof(account));
        memset(password, 0, sizeof(password));
        memset(accountGbk, 0, sizeof(accountGbk));
        memset(passwordGbk, 0, sizeof(passwordGbk));
        memset(accountCanon, 0, sizeof(accountCanon));
        memset(roleText, 0, sizeof(roleText));
        memset(yuanText, 0, sizeof(yuanText));
        memset(typeText, 0, sizeof(typeText));
        memset(payId, 0, sizeof(payId));
        memset(payUrlEscaped, 0, sizeof(payUrlEscaped));
        memset(errorEscaped, 0, sizeof(errorEscaped));
        memset(json, 0, sizeof(json));
        memset(&order, 0, sizeof(order));
        if (!vm_mock_admin_form_value(body, "account", account,
                                      sizeof(account)) ||
            !vm_mock_admin_form_value(body, "password", password,
                                      sizeof(password)) ||
            !vm_mock_admin_form_value(body, "role_id", roleText,
                                      sizeof(roleText)) ||
            !vm_mock_admin_form_value(body, "yuan", yuanText,
                                      sizeof(yuanText)) ||
            !vm_mock_admin_form_value(body, "pay_type", typeText,
                                      sizeof(typeText)) ||
            !vm_net_mock_parse_u32_strict(roleText, &roleId) || roleId == 0 ||
            !vm_net_mock_parse_u32_strict(yuanText, &yuan) || yuan == 0 ||
            !vm_net_mock_parse_u32_strict(typeText, &payType) ||
            account[0] == 0)
        {
            memset(password, 0, sizeof(password));
            vm_mock_admin_send_response(client, "400 Bad Request",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"充值参数无效\"}\n");
            return 0;
        }
        (void)vm_mock_admin_utf8_to_gbk_text(account, accountGbk,
                                              sizeof(accountGbk), false);
        (void)vm_mock_admin_utf8_to_gbk_text(password, passwordGbk,
                                              sizeof(passwordGbk), false);
        /* Game WT stores account/password as GBK bytes; HTTP body is UTF-8. */
        if (password[0] != 0 &&
            vm_mock_service_account_verify_credentials(account, password))
        {
            snprintf(accountCanon, sizeof(accountCanon), "%s", account);
            authed = true;
            authPath = "password-utf8";
        }
        else if (password[0] != 0 && accountGbk[0] != 0 && passwordGbk[0] != 0 &&
                 vm_mock_service_account_verify_credentials(accountGbk,
                                                            passwordGbk))
        {
            snprintf(accountCanon, sizeof(accountCanon), "%s", accountGbk);
            authed = true;
            authPath = "password-gbk";
        }
        else if (password[0] != 0 && accountGbk[0] != 0 &&
                 vm_mock_service_account_verify_credentials(accountGbk,
                                                            password))
        {
            snprintf(accountCanon, sizeof(accountCanon), "%s", accountGbk);
            authed = true;
            authPath = "password-account-gbk";
        }
        else if (password[0] != 0 && passwordGbk[0] != 0 &&
                 vm_mock_service_account_verify_credentials(account,
                                                            passwordGbk))
        {
            snprintf(accountCanon, sizeof(accountCanon), "%s", account);
            authed = true;
            authPath = "password-pass-gbk";
        }
        /*
         * Pixi already passed WT login. If password re-check mismatches
         * encoding, accept when role_id belongs to this account (UTF-8/GBK).
         * Payer still pays real money to that role — not a free grant path.
         */
        else if (vm_mock_payment_role_balance(account, roleId, false,
                                              &scratchBalance))
        {
            snprintf(accountCanon, sizeof(accountCanon), "%s", account);
            authed = true;
            authPath = "role-bind-utf8";
        }
        else if (accountGbk[0] != 0 &&
                 vm_mock_payment_role_balance(accountGbk, roleId, false,
                                              &scratchBalance))
        {
            snprintf(accountCanon, sizeof(accountCanon), "%s", accountGbk);
            authed = true;
            authPath = "role-bind-gbk";
        }
        memset(password, 0, sizeof(password));
        memset(passwordGbk, 0, sizeof(passwordGbk));
        if (!authed || accountCanon[0] == 0)
        {
            printf("[warn][payment] api_recharge_auth_failed account_len=%u "
                   "role=%u\n",
                   (u32)strlen(account), roleId);
            vm_mock_admin_send_response(client, "401 Unauthorized",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"账号或密码错误\"}\n");
            return 0;
        }
        printf("[info][payment] api_recharge_auth path=%s role=%u\n",
               authPath, roleId);
        if (!vm_mock_payment_create_order(accountCanon, roleId, yuan, payType,
                                          payId, &message))
        {
            const char *err = message && message[0] ? message : "订单创建失败";
            if (!vm_mock_admin_json_escape(err, errorEscaped,
                                           sizeof(errorEscaped)))
                snprintf(errorEscaped, sizeof(errorEscaped), "订单创建失败");
            snprintf(json, sizeof(json),
                     "{\"ok\":false,\"error\":\"%s\"}\n", errorEscaped);
            vm_mock_admin_send_response(client, "400 Bad Request",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        json);
            return 0;
        }
        if (!vm_mock_payment_load_order(payId, false, &order) ||
            !order.found ||
            !vm_mock_payment_qr_payload_is_safe(order.payUrl) ||
            !vm_mock_admin_json_escape(order.payUrl, payUrlEscaped,
                                       sizeof(payUrlEscaped)) ||
            !vm_mock_admin_json_escape(order.payId, errorEscaped,
                                       sizeof(errorEscaped)))
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"订单已创建但支付链接不可用\"}\n");
            return 0;
        }
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"payId\":\"%s\",\"payType\":%u,"
                 "\"priceCents\":%u,\"reallyPriceCents\":%u,\"wcoinAmount\":%u,"
                 "\"payUrl\":\"%s\",\"status\":%u,\"timeoutMinutes\":%u}\n",
                 errorEscaped, order.payType, order.priceCents,
                 order.reallyPriceCents ? order.reallyPriceCents
                                        : order.priceCents,
                 order.wcoinAmount, payUrlEscaped, order.status,
                 order.timeoutMinutes ? order.timeoutMinutes : 5u);
        printf("[info][payment] api_recharge_create auth=%s role=%u "
               "type=%u yuan=%u pay_id=%s\n",
               authPath, roleId, payType, yuan, order.payId);
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/json; charset=utf-8",
                                    "Access-Control-Allow-Origin: *\r\n", json);
        return 1;
    }
    /* Pixi title-screen register: same create path as /user/register. */
    if (strcmp(target, "/api/register") == 0)
    {
        char account[64];
        char password[64];
        char accountEscaped[128];
        char errorEscaped[256];
        char json[512];
        const char *message = NULL;
        const char *createMessage = NULL;
        const char *cors =
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Access-Control-Max-Age: 86400\r\n";

        if (strcmp(method, "OPTIONS") == 0)
        {
            vm_mock_admin_send_response(client, "204 No Content",
                                        "text/plain; charset=utf-8", cors, "");
            return 1;
        }
        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "Allow: POST, OPTIONS\r\n"
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"method\"}\n");
            return 0;
        }
        memset(account, 0, sizeof(account));
        memset(password, 0, sizeof(password));
        memset(accountEscaped, 0, sizeof(accountEscaped));
        memset(errorEscaped, 0, sizeof(errorEscaped));
        memset(json, 0, sizeof(json));
        if (!vm_mock_admin_form_value(body, "account", account,
                                      sizeof(account)) ||
            !vm_mock_admin_form_value(body, "password", password,
                                      sizeof(password)) ||
            account[0] == 0 || password[0] == 0)
        {
            memset(password, 0, sizeof(password));
            vm_mock_admin_send_response(client, "400 Bad Request",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"账号或密码参数不完整\"}\n");
            return 0;
        }
        if (!vm_mock_user_valid_registration(account, password, &message))
        {
            const char *err =
                message && message[0] ? message : "注册参数无效";
            memset(password, 0, sizeof(password));
            if (!vm_mock_admin_json_escape(err, errorEscaped,
                                           sizeof(errorEscaped)))
                snprintf(errorEscaped, sizeof(errorEscaped), "注册参数无效");
            snprintf(json, sizeof(json),
                     "{\"ok\":false,\"error\":\"%s\"}\n", errorEscaped);
            vm_mock_admin_send_response(client, "400 Bad Request",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        json);
            return 0;
        }
        if (!vm_mock_service_account_register_user(account, password,
                                                 &createMessage))
        {
            const char *err = "账号注册失败，请稍后重试";
            int statusConflict = 0;
            if (createMessage &&
                strcmp(createMessage, "account already exists") == 0)
            {
                err = "该账号名已被注册";
                statusConflict = 1;
            }
            else if (createMessage &&
                     strcmp(createMessage, "daily registration limit reached") == 0)
            {
                err = "今日全服注册名额已满,gmQQ:2350321870";
            }
            else if (createMessage &&
                     strcmp(createMessage, "account db unavailable") == 0)
            {
                err = "账号库暂不可用";
            }
            memset(password, 0, sizeof(password));
            if (!vm_mock_admin_json_escape(err, errorEscaped,
                                           sizeof(errorEscaped)))
                snprintf(errorEscaped, sizeof(errorEscaped),
                         "账号注册失败，请稍后重试");
            snprintf(json, sizeof(json),
                     "{\"ok\":false,\"error\":\"%s\"}\n", errorEscaped);
            printf("[warn][user-web] api_register account=%s result=rejected "
                   "detail=%s\n",
                   account[0] ? account : "-",
                   createMessage ? createMessage : "-");
            vm_mock_admin_send_response(
                client,
                statusConflict ? "409 Conflict" : "400 Bad Request",
                "application/json; charset=utf-8",
                "Access-Control-Allow-Origin: *\r\n", json);
            return 0;
        }
        memset(password, 0, sizeof(password));
        if (!vm_mock_admin_json_escape(account, accountEscaped,
                                       sizeof(accountEscaped)))
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "Access-Control-Allow-Origin: *\r\n",
                                        "{\"ok\":false,\"error\":\"注册响应无效\"}\n");
            return 0;
        }
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"account\":\"%s\"}\n", accountEscaped);
        printf("[info][user-web] api_register account=%s result=success\n",
               account);
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/json; charset=utf-8",
                                    "Access-Control-Allow-Origin: *\r\n", json);
        return 1;
    }
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, "/payment/qrcode.js") == 0)
        return vm_mock_admin_send_payment_qrcode_script(client);
    if (strcmp(target, "/payment/cbhub/notify") == 0 ||
        strcmp(target, "/payment/cbhub/return") == 0)
    {
        bool synchronous = strcmp(target, "/payment/cbhub/return") == 0;
        char payId[64];
        vm_mock_payment_settle_result settleResult;

        if (strcmp(method, "GET") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: GET\r\n", "fail");
            return 0;
        }
        settleResult = vm_mock_payment_process_callback_query(
            query, synchronous ? "return" : "notify", payId);
        if (!synchronous)
        {
            const char *body = "success";
            if (settleResult == VM_MOCK_PAYMENT_SETTLE_INVALID)
            {
                /* process_callback stashes a distinct token in payId on failure. */
                if (strcmp(payId, "error_parse") == 0)
                    body = "error_parse";
                else if (strcmp(payId, "error_settle") == 0)
                    body = "error_settle";
                else
                    body = "error_sign";
            }
            vm_mock_admin_send_response(
                client,
                settleResult == VM_MOCK_PAYMENT_SETTLE_INVALID ?
                    "400 Bad Request" : "200 OK",
                "text/plain; charset=utf-8", NULL, body);
            return settleResult != VM_MOCK_PAYMENT_SETTLE_INVALID;
        }
        response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (response == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        NULL, NULL, "内存不足。\n");
            return 0;
        }
        if (settleResult == VM_MOCK_PAYMENT_SETTLE_INVALID)
        {
            vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL,
                                        "支付回调校验失败。\n");
            free(response);
            return 0;
        }
        else
        {
            vm_mock_payment_order order;
            if (vm_mock_payment_load_order(payId, false, &order))
                vm_mock_payment_render_order_page(response,
                                                  VM_MOCK_ADMIN_RESPONSE_MAX,
                                                  &order);
            else
                vm_mock_payment_render_order_page(response,
                                                  VM_MOCK_ADMIN_RESPONSE_MAX,
                                                  NULL);
            vm_mock_admin_send_response(client, "200 OK",
                                        "text/html; charset=utf-8", NULL,
                                        response);
            free(response);
            return 1;
        }
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
    if (strcmp(target, "/user/recharge/create") == 0)
    {
        vm_mock_user_session *session = NULL;
        char roleText[32];
        char yuanText[32];
        char typeText[16];
        char payId[64];
        char location[192];
        u32 roleId = 0;
        u32 yuan = 0;
        u32 payType = 0;
        const char *message = NULL;

        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n",
                                        "创建充值订单只允许 POST。\n");
            return 0;
        }
        session = vm_mock_user_request_session(request, headerLen);
        if (session == NULL)
        {
            vm_mock_admin_send_location(client, "/", NULL);
            return 0;
        }
        memset(roleText, 0, sizeof(roleText));
        memset(yuanText, 0, sizeof(yuanText));
        memset(typeText, 0, sizeof(typeText));
        if (!vm_mock_admin_form_value(body, "role_id", roleText,
                                      sizeof(roleText)) ||
            !vm_mock_admin_form_value(body, "yuan", yuanText,
                                      sizeof(yuanText)) ||
            !vm_mock_admin_form_value(body, "pay_type", typeText,
                                      sizeof(typeText)) ||
            !vm_net_mock_parse_u32_strict(roleText, &roleId) || roleId == 0 ||
            !vm_net_mock_parse_u32_strict(yuanText, &yuan) || yuan == 0 ||
            !vm_net_mock_parse_u32_strict(typeText, &payType))
        {
            vm_mock_user_redirect_message(client, "error", "充值参数无效");
            return 0;
        }
        if (!vm_mock_payment_create_order(session->accountId, roleId, yuan,
                                          payType, payId, &message))
        {
            vm_mock_user_redirect_message(
                client, "error", message ? message : "订单创建失败");
            return 0;
        }
        snprintf(location, sizeof(location), "/user/recharge/order?id=%s",
                 payId);
        vm_mock_admin_send_location(client, location, NULL);
        return 1;
    }
    if (strcmp(target, "/user/recharge/order") == 0)
    {
        vm_mock_user_session *session = NULL;
        vm_mock_payment_order order;
        char payId[64];
        bool visible = false;

        if (strcmp(method, "GET") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: GET\r\n",
                                        "充值订单页面只允许 GET。\n");
            return 0;
        }
        session = vm_mock_user_request_session(request, headerLen);
        if (session == NULL)
        {
            vm_mock_admin_send_location(client, "/", NULL);
            return 0;
        }
        memset(payId, 0, sizeof(payId));
        memset(&order, 0, sizeof(order));
        if (vm_mock_admin_form_value(query, "id", payId, sizeof(payId)) &&
            vm_mock_payment_load_order(payId, false, &order) &&
            strcmp(order.accountId, session->accountId) == 0)
        {
            visible = true;
            (void)vm_mock_payment_refresh_order(session->accountId, payId);
            (void)vm_mock_payment_load_order(payId, false, &order);
        }
        response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (response == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        NULL, NULL, "内存不足。\n");
            return 0;
        }
        vm_mock_payment_render_order_page(response, VM_MOCK_ADMIN_RESPONSE_MAX,
                                          visible ? &order : NULL);
        vm_mock_admin_send_response(client, visible ? "200 OK" : "404 Not Found",
                                    "text/html; charset=utf-8", NULL, response);
        free(response);
        return visible ? 1 : 0;
    }
    if (strcmp(target, "/user/login") == 0 ||
        strcmp(target, "/user/register") == 0)
    {
        char account[64];
        char password[64];
        char limitMessage[128];
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
        memset(limitMessage, 0, sizeof(limitMessage));
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
                ok = vm_mock_service_account_register_user(account, password,
                                                           &createMessage);
                if (!ok)
                {
                    if (createMessage && strcmp(createMessage,
                                                "account already exists") == 0)
                        message = "该账号名已被注册";
                    else if (createMessage &&
                             strcmp(createMessage,
                                    "daily registration limit reached") == 0)
                    {
                        snprintf(limitMessage, sizeof(limitMessage),
                                 "今日全服注册名额已满（每天最多 %u 个），请明天再试",
                                 vm_mock_service_register_daily_limit());
                        message = limitMessage;
                    }
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
        char account[64];
        char currentPassword[64];
        char newPassword[64];
        char confirmPassword[64];
        char accountEscaped[128];
        char errorEscaped[256];
        char json[512];
        const char *error = NULL;
        const char *accountId = NULL;
        bool ok = false;
        bool apiMode = false;
        const char *cors =
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Access-Control-Max-Age: 86400\r\n";

        if (strcmp(method, "OPTIONS") == 0)
        {
            vm_mock_admin_send_response(client, "204 No Content",
                                        "text/plain; charset=utf-8", cors, "");
            return 1;
        }
        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST, OPTIONS\r\n",
                                        "修改密码只允许 POST。\n");
            return 0;
        }
        memset(account, 0, sizeof(account));
        memset(currentPassword, 0, sizeof(currentPassword));
        memset(newPassword, 0, sizeof(newPassword));
        memset(confirmPassword, 0, sizeof(confirmPassword));
        memset(accountEscaped, 0, sizeof(accountEscaped));
        memset(errorEscaped, 0, sizeof(errorEscaped));
        memset(json, 0, sizeof(json));
        /* Pixi / gateway: form includes account → JSON (no web session). */
        apiMode = vm_mock_admin_form_value(body, "account", account,
                                           sizeof(account)) &&
                  account[0] != 0;
        session = vm_mock_user_request_session(request, headerLen);
        if (apiMode)
            accountId = account;
        else if (session != NULL)
            accountId = session->accountId;
        else
        {
            vm_mock_admin_send_location(client, "/", NULL);
            return 0;
        }
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
        else if (!vm_mock_service_account_verify_credentials(accountId,
                                                            currentPassword))
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
            ok = vm_mock_service_account_set_password(accountId, newPassword,
                                                     &error);
        }
        memset(currentPassword, 0, sizeof(currentPassword));
        memset(newPassword, 0, sizeof(newPassword));
        memset(confirmPassword, 0, sizeof(confirmPassword));
        if (ok)
        {
            vm_mock_user_invalidate_other_sessions(accountId, session);
            printf("[info][user-web] password_change account=%s result=success "
                   "other_sessions=invalidated api=%d\n",
                   accountId, apiMode ? 1 : 0);
            if (apiMode)
            {
                if (!vm_mock_admin_json_escape(accountId, accountEscaped,
                                               sizeof(accountEscaped)))
                {
                    vm_mock_admin_send_response(
                        client, "500 Internal Server Error",
                        "application/json; charset=utf-8", cors,
                        "{\"ok\":false,\"error\":\"修改密码响应无效\"}\n");
                    return 0;
                }
                snprintf(json, sizeof(json),
                         "{\"ok\":true,\"account\":\"%s\"}\n", accountEscaped);
                vm_mock_admin_send_response(client, "200 OK",
                                            "application/json; charset=utf-8",
                                            cors, json);
            }
            else
                vm_mock_user_redirect_message(client, "ok", "密码修改成功");
        }
        else
        {
            printf("[warn][user-web] password_change account=%s result=rejected "
                   "api=%d\n",
                   accountId ? accountId : "-", apiMode ? 1 : 0);
            if (apiMode)
            {
                const char *err = error ? error : "密码修改失败";
                if (!vm_mock_admin_json_escape(err, errorEscaped,
                                               sizeof(errorEscaped)))
                    snprintf(errorEscaped, sizeof(errorEscaped),
                             "密码修改失败");
                snprintf(json, sizeof(json),
                         "{\"ok\":false,\"error\":\"%s\"}\n", errorEscaped);
                vm_mock_admin_send_response(client, "400 Bad Request",
                                            "application/json; charset=utf-8",
                                            cors, json);
            }
            else
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
            char cookieName[40];
            vm_mock_admin_ensure_session_token();
            vm_mock_admin_session_cookie_name(cookieName, sizeof(cookieName));
            snprintf(cookieHeader, sizeof(cookieHeader),
                     "Set-Cookie: %s=%s; Path=" VM_MOCK_ADMIN_BASE_PATH "; HttpOnly; SameSite=Strict\r\n",
                     cookieName, g_vm_mock_admin_session_token);
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
        {
            char cookieHeader[256];
            char cookieName[40];
            vm_mock_admin_session_cookie_name(cookieName, sizeof(cookieName));
            snprintf(cookieHeader, sizeof(cookieHeader),
                     "Set-Cookie: %s=; Path=" VM_MOCK_ADMIN_BASE_PATH "; Max-Age=0; HttpOnly; SameSite=Strict\r\n"
                     "Set-Cookie: cbe_admin=; Path=" VM_MOCK_ADMIN_BASE_PATH "; Max-Age=0; HttpOnly; SameSite=Strict\r\n",
                     cookieName);
            vm_mock_admin_send_location(client, VM_MOCK_ADMIN_LOGIN_PATH,
                                        cookieHeader);
        }
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
    /* 提现收款码：页面 rewrite 后为 /admin-…/kcoin-qr?id=（须登录）。 */
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/kcoin-qr") == 0)
    {
        if (vm_mock_admin_serve_kcoin_qr(client, query ? query : ""))
            return 1;
        vm_mock_admin_send_response(client, "404 Not Found", NULL, NULL,
                                    "收款码不存在或无法读取。\n");
        return 0;
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
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/map-edit.json") == 0)
    {
        char sceneUtf8[192];
        char runtimeScene[64];
        char *json = NULL;

        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        memset(runtimeScene, 0, sizeof(runtimeScene));
        json = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
        if (json == NULL)
        {
            vm_mock_admin_send_response(client, "500 Internal Server Error",
                                        NULL, NULL, "内存不足。\n");
            return 0;
        }
        if (!vm_mock_admin_scene_from_form(query, sceneUtf8, sizeof(sceneUtf8),
                                           runtimeScene, sizeof(runtimeScene)) ||
            !vm_mock_admin_build_map_edit_json(runtimeScene, sceneUtf8, json,
                                               VM_MOCK_ADMIN_RESPONSE_MAX))
        {
            free(json);
            vm_mock_admin_send_response(client, "422 Unprocessable Content",
                                        "application/json; charset=utf-8", NULL,
                                        "{\"ok\":false,\"error\":\"无法加载地图\"}\n");
            return 0;
        }
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/json; charset=utf-8", NULL,
                                    json);
        free(json);
        return 1;
    }
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/tile-sheet.bmp") == 0)
    {
        char nameUtf8[192];
        char nameGbk[64];
        u8 *bmp = NULL;
        u32 bmpLen = 0;

        memset(nameUtf8, 0, sizeof(nameUtf8));
        memset(nameGbk, 0, sizeof(nameGbk));
        if (!vm_mock_admin_form_value(query, "name", nameUtf8,
                                      sizeof(nameUtf8)) ||
            !vm_mock_admin_utf8_to_gbk_text(nameUtf8, nameGbk, sizeof(nameGbk),
                                            false) ||
            !vm_mock_admin_build_tile_sheet_bmp(nameGbk, &bmp, &bmpLen, NULL,
                                                NULL))
        {
            free(bmp);
            vm_mock_admin_send_response(client, "422 Unprocessable Content",
                                        NULL, NULL,
                                        "地砖图集无法生成预览。\n");
            return 0;
        }
        vm_mock_admin_send_binary_response(client, "200 OK", "image/bmp", bmp,
                                           bmpLen);
        free(bmp);
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
        char xhr[64];

        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n", "只允许 POST。\n");
            return 0;
        }
        memset(xhr, 0, sizeof(xhr));
        g_vm_mock_admin_ajax_action =
            vm_mock_admin_header_value(request, headerLen, "X-Requested-With",
                                       xhr, sizeof(xhr)) &&
            strcmp(xhr, "XMLHttpRequest") == 0;
        vm_mock_admin_handle_action(client, body);
        g_vm_mock_admin_ajax_action = false;
        return 1;
    }
    vm_mock_admin_send_response(client, "404 Not Found", NULL, NULL, "页面不存在。\n");
    return 0;
}

static int vm_mock_admin_handle_client(vm_mock_service_socket client)
{
    /* 64KiB request buffer: heap-allocate so worker + render locals stay under
     * the default ~1MB winpthread stack. */
    char *request = NULL;
    char method[12];
    char target[1024];
    char version[16];
    char *headerEnd = NULL;
    char *query = NULL;
    char *body = NULL;
    size_t received = 0;
    size_t headerLen = 0;
    u32 contentLength = 0;
    u32 recvStartMs = 0;
    u32 recvDoneMs = 0;
    int result = 0;

    /* Finish reading the HTTP request before taking the game protocol lock.
     * Holding that lock across recv made browsers abort while workers waited
     * on busy game/MySQL work, which surfaced as "请求不完整". */
    vm_mock_admin_configure_client_socket(client);
    request = (char *)calloc(1, VM_MOCK_ADMIN_REQUEST_MAX + 1);
    if (request == NULL)
    {
        vm_mock_admin_send_response(client, "500 Internal Server Error", NULL,
                                    NULL, "内存不足。\n");
        return 0;
    }
    recvStartMs = scheduler_get_tick_ms();
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
            free(request);
            return 0;
        }
        if (received >= headerLen + contentLength)
            break;
    }
    recvDoneMs = scheduler_get_tick_ms();
    if (headerEnd == NULL || received < headerLen + contentLength)
    {
        printf("[warn][mock-admin] incomplete_request received=%u header=%u content_length=%u recv_ms=%u\n",
               (u32)received,
               (u32)headerLen,
               contentLength,
               recvDoneMs >= recvStartMs ? recvDoneMs - recvStartMs : 0);
        vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL, "请求不完整。\n");
        free(request);
        return 0;
    }
    if (!vm_mock_admin_request_has_allowed_origin(request, headerLen))
    {
        vm_mock_admin_send_response(client, "403 Forbidden", NULL, NULL,
                                    "Host 或 Origin 校验失败。\n");
        free(request);
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
        free(request);
        return 0;
    }
    query = strchr(target, '?');
    if (query != NULL)
        *query++ = 0;
    else
        query = "";

    /*
     * Pixi polls these public JSON maps frequently.  They only need the
     * in-memory monster/NPC catalogs (MySQL load is serialized inside the
     * loaders).  Taking the shared game protocol mutex here used to stall
     * every game worker for minutes when a cold/reconnect MySQL path blocked
     * under admin-dispatch — see docs/re/2026-08-30-admin-dispatch-protocol-lock-mysql.md.
     */
    if (strcmp(method, "GET") == 0 &&
        (strcmp(target, "/api/monster-levels") == 0 ||
         strcmp(target, "/api/world-boss-active") == 0 ||
         strcmp(target, "/api/npc-scenes") == 0 ||
         strcmp(target, "/api/zones") == 0 ||
         strcmp(target, "/api/custom-catalog") == 0))
    {
        g_vm_mock_admin_protocol_lock_depth = 0;
        g_vm_mock_admin_protocol_holder_label[0] = 0;
        result = vm_mock_admin_dispatch_client(client, request, headerLen,
                                              method, target, query, body);
        free(request);
        return result;
    }

    {
        u32 lockWaitMs = 0;

        if (!vm_mock_admin_protocol_lock_wait(&lockWaitMs))
        {
            printf("[warn][mock-admin] protocol_lock_timeout method=%s target=%s wait_ms=%u\n",
                   method, target, (u32)VM_MOCK_ADMIN_PROTOCOL_LOCK_WAIT_MS);
            vm_mock_protocol_lock_dump_holder("admin-timeout");
            vm_mock_admin_send_response(
                client, "503 Service Unavailable", NULL, NULL,
                "游戏服务繁忙，管理操作请稍后重试。\n");
            free(request);
            return 0;
        }
        if (lockWaitMs > 100)
        {
            printf("[info][mock-admin] protocol_lock_wait method=%s target=%s wait_ms=%u\n",
                   method, target, lockWaitMs);
        }
        g_vm_mock_admin_protocol_lock_depth = 1;
        vm_mock_admin_protocol_holder_set(method, target);
        vm_mock_admin_protocol_holder_note();
        result = vm_mock_admin_dispatch_client(client, request, headerLen,
                                              method, target, query, body);
        g_vm_mock_admin_protocol_lock_depth = 0;
        g_vm_mock_admin_protocol_holder_label[0] = 0;
        vm_mock_protocol_lock_clear();
        pthread_mutex_unlock(&g_vm_mock_service_protocol_mutex);
    }
    free(request);
    return result;
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
