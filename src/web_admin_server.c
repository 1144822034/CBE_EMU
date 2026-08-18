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
    /* Headers stay deliberately bounded.  Form bodies are allocated after
     * Content-Length has been parsed, so catalog editors are not constrained
     * by this header-buffer size. */
    VM_MOCK_ADMIN_HEADER_MAX = 8192,
    /* Multipart image imports are intentionally bounded.  This is large
     * enough for a source PNG/JPEG while still preventing the local admin
     * listener from becoming an unbounded file sink. */
    VM_MOCK_ADMIN_REQUEST_BODY_MAX = 4 * 1024 * 1024,
    VM_MOCK_ADMIN_GIF_UPLOAD_MAX = 2 * 1024 * 1024,
    VM_MOCK_ADMIN_GIF_UPLOAD_PIXEL_MAX = 768 * 768,
    /* This is allocation workspace rather than a product-facing page-length
     * rule.  Catalog editors intentionally serialize many selectable rows;
     * 2 MiB accommodates their bounded data model without replacing a valid
     * page with a synthetic "too large" error. */
    VM_MOCK_ADMIN_RESPONSE_MAX = 2 * 1024 * 1024,
    VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS = 100,
    VM_MOCK_USER_SESSION_MAX = 64
};

#define VM_MOCK_ADMIN_BASE_PATH "/admin-418yz6"
#define VM_MOCK_ADMIN_ROOT_PATH VM_MOCK_ADMIN_BASE_PATH "/"
#define VM_MOCK_ADMIN_LOGIN_PATH VM_MOCK_ADMIN_BASE_PATH "/login"
#define VM_MOCK_ADMIN_LOGIN_SCRIPT_PATH VM_MOCK_ADMIN_BASE_PATH "/login.js"
#define VM_MOCK_ADMIN_LOGOUT_PATH VM_MOCK_ADMIN_BASE_PATH "/logout"
#define VM_MOCK_ADMIN_ACTION_PATH VM_MOCK_ADMIN_BASE_PATH "/action"
#define VM_MOCK_ADMIN_ACCOUNT_LIST_PATH VM_MOCK_ADMIN_BASE_PATH "/accounts"
#define VM_MOCK_ADMIN_MONSTER_BOSS_EXPORT_PATH \
    VM_MOCK_ADMIN_BASE_PATH "/monster-boss-drops.xlsx"

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

/* Browser fetch(FormData) uses multipart/form-data while native HTML POST
 * uses application/x-www-form-urlencoded.  Administrative actions use both
 * paths, so the form reader owns this encoding boundary rather than making
 * individual editor buttons depend on a particular transport.  The request
 * handler terminates text bodies before dispatch; binary uploads retain their
 * dedicated, length-aware parser below. */
static bool vm_mock_admin_multipart_values(const char *form, const char *key,
                                           char *values, size_t valueCap,
                                           u32 valueCapCount, u32 *countOut)
{
    const char *lineEnd = NULL;
    const char *part = NULL;
    size_t keyLen = key ? strlen(key) : 0;
    size_t boundaryLen = 0;
    u32 count = 0;

    if (countOut != NULL)
        *countOut = 0;
    if (form == NULL || keyLen == 0 || values == NULL || valueCap == 0 ||
        valueCapCount == 0 || strncmp(form, "--", 2) != 0)
    {
        return false;
    }
    lineEnd = strstr(form, "\r\n");
    if (lineEnd == NULL || lineEnd == form)
        return false;
    boundaryLen = (size_t)(lineEnd - form);
    part = form;
    while (part != NULL && strncmp(part, form, boundaryLen) == 0)
    {
        const char *headers = part + boundaryLen;
        const char *headersEnd = NULL;
        const char *name = NULL;
        const char *value = NULL;
        const char *next = NULL;
        bool matches = false;

        if (headers[0] == '-' && headers[1] == '-')
            break;
        if (headers[0] != '\r' || headers[1] != '\n')
            return false;
        headers += 2;
        headersEnd = strstr(headers, "\r\n\r\n");
        if (headersEnd == NULL)
            return false;
        name = strstr(headers, "name=\"");
        if (name != NULL && name < headersEnd)
        {
            const char *nameValue = name + strlen("name=\"");
            const char *nameEnd = strchr(nameValue, '\"');

            if (nameEnd != NULL && nameEnd <= headersEnd &&
                (size_t)(nameEnd - nameValue) == keyLen &&
                memcmp(nameValue, key, keyLen) == 0)
            {
                matches = true;
            }
        }
        value = headersEnd + 4;
        next = value;
        while ((next = strstr(next, "\r\n--")) != NULL)
        {
            if (strncmp(next + 2, form, boundaryLen) == 0)
                break;
            ++next;
        }
        if (next == NULL)
            return false;
        if (matches)
        {
            size_t valueLen = (size_t)(next - value);

            if (count >= valueCapCount || valueLen >= valueCap)
                return false;
            memcpy(values + (size_t)count * valueCap, value, valueLen);
            values[(size_t)count * valueCap + valueLen] = 0;
            ++count;
        }
        part = next + 2;
    }
    if (countOut != NULL)
        *countOut = count;
    return count != 0;
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
    if (strncmp(form, "--", 2) == 0)
        return vm_mock_admin_multipart_values(form, key, out, outCap, 1,
                                              NULL);
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

/* HTML <select multiple> posts one key/value pair for every selected option.
 * Keep those values separate through decoding so update-management never has
 * to invent a delimiter that might be valid in a future resource filename. */
static bool vm_mock_admin_form_values(const char *form, const char *key,
                                      char *values, size_t valueCap,
                                      u32 valueCapCount, u32 *countOut)
{
    size_t keyLen = key ? strlen(key) : 0;
    const char *cursor = form;
    u32 count = 0;

    if (countOut)
        *countOut = 0;
    if (form == NULL || keyLen == 0 || values == NULL || valueCap == 0 ||
        valueCapCount == 0)
    {
        return false;
    }
    if (strncmp(form, "--", 2) == 0)
        return vm_mock_admin_multipart_values(form, key, values, valueCap,
                                              valueCapCount, countOut);
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

            if (count >= valueCapCount ||
                !vm_mock_admin_url_decode(value, valueLen,
                                          values + (size_t)count * valueCap,
                                          valueCap) ||
                values[(size_t)count * valueCap] == 0)
            {
                return false;
            }
            ++count;
        }
        if (pairEnd == NULL)
            break;
        cursor = pairEnd + 1;
    }
    if (countOut)
        *countOut = count;
    return count != 0;
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
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src 'self'; img-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
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

static int vm_mock_admin_send_binary_download(vm_mock_service_socket client,
                                              const char *contentType,
                                              const char *filename,
                                              const u8 *body, u32 bodyLen)
{
    char header[1024];
    int headerLen = 0;

    /* Callers provide fixed ASCII filenames.  Keeping the attachment name in
     * this response helper, rather than in page JavaScript, makes the export
     * usable from a direct authenticated URL as well. */
    if (filename == NULL || filename[0] == 0 || strpbrk(filename, "\r\n") != NULL)
        return 0;
    headerLen = snprintf(
        header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src 'self'; img-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "\r\n",
        contentType ? contentType : "application/octet-stream", filename,
        bodyLen);
    if (headerLen <= 0 || (size_t)headerLen >= sizeof(header))
        return 0;
    if (!vm_mock_service_send_all(client, (const u8 *)header, (u32)headerLen))
        return 0;
    return bodyLen == 0 ||
           (body != NULL && vm_mock_service_send_all(client, body, bodyLen));
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
    sent = vm_mock_admin_send_binary_response(
        client, "200 OK", "application/javascript; charset=utf-8", body, length);

done:
    if (fp != NULL)
        fclose(fp);
    free(body);
    if (!sent)
        sent = vm_mock_admin_send_response(client, "404 Not Found", NULL, NULL,
                                           "二维码脚本不可用。\n");
    return sent;
}

enum
{
    VM_MOCK_ADMIN_SCENE_FILE_MAX = 512,
    VM_MOCK_ADMIN_ACTOR_FILE_MAX = 1024,
    VM_MOCK_ADMIN_XSE_FILE_MAX = 512,
    VM_MOCK_ADMIN_UPDATE_FILE_MAX = 2048,
    VM_MOCK_ADMIN_CONTENT_FILE_MAX = 512,
    VM_MOCK_ADMIN_SHOP_PAGE_SIZE = 50,
    /* A merchant can select each catalog item at most once.  Keep the request
     * parser bounded by the authoritative catalog rather than an arbitrary
     * UI-row limit. */
    VM_MOCK_ADMIN_NPC_STOCK_SELECTION_MAX = VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS,
    /* Keep the account pane responsive even for large account directories.
     * More rows are requested only when its own scroll viewport needs them. */
    VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE = 40,
    /* Risk records are already a server-side subset (only <=3 second battle
     * intervals are inserted), but the trail still grows without bound.  Keep
     * one render at a fixed row budget and page the audit_id DESC order with
     * an explicit offset so every historical record stays reachable. */
    VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE = 50
};

typedef struct
{
    char name[64];
    uint64_t size;
} vm_mock_admin_scene_file;

static char g_vm_mock_admin_session_token[40];

/* This script deliberately has no network I/O.  It only remembers a password
 * in the current browser profile when the administrator explicitly opts in. */
static const char g_vm_mock_admin_login_script[] =
    "(()=>{const key='cbe-admin-login-password-v1',form=document.querySelector('[data-admin-login-form]'),password=document.querySelector('[data-admin-login-password]'),remember=document.querySelector('[data-admin-login-remember]');if(!form||!password||!remember)return;try{const saved=localStorage.getItem(key);if(saved!==null&&saved!==''){password.value=saved;remember.checked=true;}}catch(error){}form.addEventListener('submit',()=>{try{if(remember.checked&&password.value)localStorage.setItem(key,password.value);else localStorage.removeItem(key);}catch(error){}});remember.addEventListener('change',()=>{if(!remember.checked)try{localStorage.removeItem(key);}catch(error){}});})();";

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

/*
 * Declarative partial-navigation contract for list/detail management pages:
 *
 * - the scrollable catalogue has data-admin-list;
 * - each same-tab detail link has data-admin-select;
 * - the replaceable editor pane has data-admin-detail.
 *
 * setupPartialNavigation() owns history, selection and catalogue scroll
 * restoration. New management pages should use these markers instead of
 * adding page-specific full-page navigation scripts.
 */
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
    "const source=document.querySelector('#item-picker-options'),category=document.querySelector('#item-category'),modal=document.querySelector('#item-picker-modal'),close=document.querySelector('#item-picker-close'),clear=document.querySelector('#item-picker-clear'),search=document.querySelector('#item-search'),list=document.querySelector('#item-picker-list'),empty=document.querySelector('#item-picker-empty'),count=document.querySelector('#item-result-count'),error=document.querySelector('#item-picker-error'),quality=document.querySelector('#item-quality'),qualityField=document.querySelector('[data-item-quality-field]'),level=document.querySelector('#item-level'),levelField=document.querySelector('[data-item-level-field]');"
    "const inputs=[...document.querySelectorAll('[data-item-picker-input]')];if(!source||!category||!modal||!close||!clear||!search||!list||!empty||!count||!error||!inputs.length)return;"
    "const inputById=new Map(inputs.map(input=>[input.id,input]));const labels=new Map([...category.options].map(option=>[option.value,option.textContent]));const optionByValue=new Map([...source.options].filter(option=>option.value&&option.value!=='0').map(option=>[option.value,option]));const equipmentFilters=!!(quality&&qualityField&&level&&levelField);let activeId='';const choices=[];"
    "const update=input=>{if(!input)return;const option=optionByValue.get(input.value);const text=option?option.textContent:(input.value&&input.value!=='0'?`未知物品 #${input.value}`:'未选择物品');for(const label of document.querySelectorAll('[data-item-picker-label]'))if(label.dataset.itemPickerLabel===input.id)label.textContent=text;};"
    "for(const input of inputs)update(input);window.addEventListener('cbe-item-picker-sync',event=>update(inputById.get(event.detail&&event.detail.id)));"
    "for(const option of source.options){if(!option.value||option.value==='0')continue;const button=document.createElement('button');button.type='button';button.className='item-choice';button.dataset.itemId=option.value;button.dataset.category=option.dataset.category||'';button.dataset.equip=option.dataset.equip==='1'?'1':'0';button.dataset.quality=option.dataset.quality||'0';button.dataset.level=option.dataset.level||'0';button.dataset.search=option.textContent.toLowerCase();const title=document.createElement('strong');title.textContent=option.textContent;const meta=document.createElement('span'),isEquipment=button.dataset.equip==='1';meta.textContent=`${labels.get(button.dataset.category)||'未分类'}${isEquipment?` · 品质 ${button.dataset.quality} · 需求等级 ${button.dataset.level}`:''}`;button.append(title,meta);button.addEventListener('click',()=>{const input=inputById.get(activeId);if(!input)return;input.value=option.value;input.dispatchEvent(new Event('change',{bubbles:true}));update(input);error.textContent='';hide();});choices.push(button);list.appendChild(button);}"
    "const pickerSupportsEquipment=()=>{const wanted=category.value;return wanted==='all'||wanted.startsWith('e');};"
    "const rebuildLevels=()=>{if(!equipmentFilters)return;const previous=level.value;level.replaceChildren();const all=document.createElement('option');all.value='all';all.textContent='全部等级';level.append(all);if(!pickerSupportsEquipment()){levelField.hidden=true;level.value='all';return;}levelField.hidden=false;const wanted=category.value,wantedQuality=quality.value,levels=[...new Set(choices.filter(choice=>choice.dataset.equip==='1'&&(wanted==='all'||choice.dataset.category===wanted)&&(wantedQuality==='all'||choice.dataset.quality===wantedQuality)).map(choice=>choice.dataset.level))].sort((a,b)=>Number(a)-Number(b));for(const value of levels){const option=document.createElement('option');option.value=value;option.textContent=`需求等级 ${value}`;level.append(option);}level.value=[...level.options].some(option=>option.value===previous)?previous:'all';};"
    "const rebuildEquipmentFilters=()=>{if(!equipmentFilters)return;const supports=pickerSupportsEquipment();qualityField.hidden=!supports;levelField.hidden=!supports;if(!supports){quality.value='all';level.value='all';return;}const previous=quality.value;quality.replaceChildren();const all=document.createElement('option');all.value='all';all.textContent='全部品质';quality.append(all);const wanted=category.value,qualities=[...new Set(choices.filter(choice=>choice.dataset.equip==='1'&&(wanted==='all'||choice.dataset.category===wanted)).map(choice=>choice.dataset.quality))].sort((a,b)=>Number(a)-Number(b));for(const value of qualities){const option=document.createElement('option');option.value=value;option.textContent=`品质 ${value}`;quality.append(option);}quality.value=[...quality.options].some(option=>option.value===previous)?previous:'all';rebuildLevels();};"
    "const apply=()=>{const wanted=category.value,keyword=search.value.trim().toLowerCase(),supports=equipmentFilters&&pickerSupportsEquipment();if(equipmentFilters){qualityField.hidden=!supports;levelField.hidden=!supports;if(!supports){quality.value='all';level.value='all';}}const wantedQuality=equipmentFilters?quality.value:'all',wantedLevel=equipmentFilters?level.value:'all',hasEquipmentFilter=supports&&(wantedQuality!=='all'||wantedLevel!=='all');let shown=0;for(const choice of choices){const equipmentMatches=!hasEquipmentFilter||(choice.dataset.equip==='1'&&(wantedQuality==='all'||choice.dataset.quality===wantedQuality)&&(wantedLevel==='all'||choice.dataset.level===wantedLevel)),visible=(wanted==='all'||choice.dataset.category===wanted)&&equipmentMatches&&(!keyword||choice.dataset.search.includes(keyword));choice.hidden=!visible;if(visible)shown++;}count.textContent=`找到 ${shown} 件物品`;empty.hidden=shown!==0;};"
    "const show=id=>{if(!inputById.has(id))return;activeId=id;modal.hidden=false;document.body.classList.add('modal-open');error.textContent='';apply();search.focus();};"
    "function hide(){modal.hidden=true;document.body.classList.remove('modal-open');const trigger=document.querySelector(`[data-item-picker-open=\"${activeId}\"]`);if(trigger)trigger.focus();}"
    "for(const trigger of document.querySelectorAll('[data-item-picker-open]'))trigger.addEventListener('click',()=>show(trigger.dataset.itemPickerOpen));close.addEventListener('click',hide);clear.addEventListener('click',()=>{const input=inputById.get(activeId);if(!input)return;input.value='0';input.dispatchEvent(new Event('change',{bubbles:true}));update(input);hide();});modal.addEventListener('click',event=>{if(event.target===modal)hide();});document.addEventListener('keydown',event=>{if(event.key==='Escape'&&!modal.hidden)hide();});category.addEventListener('change',()=>{rebuildEquipmentFilters();apply();});if(equipmentFilters){quality.addEventListener('change',()=>{rebuildLevels();apply();});level.addEventListener('change',apply);}search.addEventListener('input',apply);for(const form of document.querySelectorAll('form'))form.addEventListener('submit',event=>{const required=[...form.querySelectorAll('[data-item-picker-required]')].find(input=>!input.value||input.value==='0');if(!required)return;event.preventDefault();error.textContent='请先选择物品';show(required.id);});rebuildEquipmentFilters();apply();};"
    "const setupNpcStock=()=>{"
    "const source=document.querySelector('#npc-stock-picker-options'),modal=document.querySelector('#npc-stock-picker-modal'),close=document.querySelector('#npc-stock-picker-close'),clear=document.querySelector('#npc-stock-picker-clear'),category=document.querySelector('#npc-stock-category'),quality=document.querySelector('#npc-stock-quality'),qualityField=document.querySelector('[data-npc-stock-quality-field]'),search=document.querySelector('#npc-stock-search'),list=document.querySelector('#npc-stock-picker-list'),count=document.querySelector('#npc-stock-result-count'),selectedCount=document.querySelector('#npc-stock-selection-count'),empty=document.querySelector('#npc-stock-picker-empty'),selectCategory=document.querySelector('#npc-stock-select-category'),unselectCategory=document.querySelector('#npc-stock-unselect-category'),confirm=document.querySelector('#npc-stock-picker-confirm'),managers=[...document.querySelectorAll('[data-npc-stock-manager]')];"
    "if(!source||!modal||!close||!clear||!category||!quality||!qualityField||!search||!list||!count||!selectedCount||!empty||!selectCategory||!unselectCategory||!confirm||!managers.length)return;"
    "const state={active:null,selected:new Map(),choices:[]},options=[...source.options].filter(option=>option.value);const keyOf=manager=>manager.dataset.npcStockKey||'',setOf=manager=>{const key=keyOf(manager);if(!state.selected.has(key))state.selected.set(key,new Set());return state.selected.get(key);},supportsQuality=manager=>{const kind=Number(manager.dataset.npcStockService);return kind===1||kind===4;},allowed=(option,manager)=>{const categoryName=option.dataset.category||'',kind=Number(manager.dataset.npcStockService),type=categoryName.charAt(0),number=Number(categoryName.slice(1));return kind===1?type==='e'&&number>=7&&number<=9:kind===4?type==='e'&&number>=0&&number<=6:kind===5?type==='i'&&number===10:false;},existing=manager=>new Set([...manager.querySelectorAll('[data-npc-stock-current-item]')].map(input=>input.value));"
    "for(const option of options){const choice=document.createElement('label'),check=document.createElement('input'),body=document.createElement('span'),title=document.createElement('strong'),meta=document.createElement('small'),isEquipment=(option.dataset.category||'').charAt(0)==='e';choice.className='item-choice npc-stock-choice';choice.dataset.itemId=option.value;choice.dataset.category=option.dataset.category||'';choice.dataset.quality=option.dataset.quality||'0';choice.dataset.level=option.dataset.level||'0';choice.dataset.search=option.textContent.toLowerCase();check.type='checkbox';title.textContent=option.textContent;meta.textContent=`${isEquipment?`品质 ${choice.dataset.quality} · 需求等级 ${choice.dataset.level} · `:''}商城默认价 ${option.dataset.price||'—'} 铜`;body.append(title,meta);choice.append(check,body);check.addEventListener('change',()=>{if(!state.active)return;const set=setOf(state.active);if(check.checked)set.add(option.value);else set.delete(option.value);apply();syncAdd(state.active);});state.choices.push(choice);list.append(choice);}"
    "const rebuildCategories=()=>{if(!state.active)return;const previous=category.value;category.replaceChildren();const all=document.createElement('option');all.value='all';all.textContent='全部可售分类';category.append(all);const categories=[...new Set(state.choices.filter(choice=>allowed({dataset:{category:choice.dataset.category}},state.active)).map(choice=>choice.dataset.category))].sort((a,b)=>a.localeCompare(b));for(const value of categories){const sourceOption=state.active.querySelector(`[data-npc-stock-current-category] option[value=\"${value}\"]`);const option=document.createElement('option');option.value=value;option.textContent=sourceOption?sourceOption.textContent:value;category.append(option);}category.value=[...category.options].some(option=>option.value===previous)?previous:'all';};"
    "const rebuildQualities=()=>{if(!state.active)return;if(!supportsQuality(state.active)){qualityField.hidden=true;quality.value='all';return;}qualityField.hidden=false;const previous=quality.value;quality.replaceChildren();const all=document.createElement('option');all.value='all';all.textContent='全部品质';quality.append(all);const qualities=[...new Set(state.choices.filter(choice=>allowed({dataset:{category:choice.dataset.category}},state.active)&&(category.value==='all'||choice.dataset.category===category.value)).map(choice=>choice.dataset.quality))].sort((a,b)=>Number(a)-Number(b));for(const value of qualities){const option=document.createElement('option');option.value=value;option.textContent=`品质 ${value}`;quality.append(option);}quality.value=[...quality.options].some(option=>option.value===previous)?previous:'all';};"
    "const syncAdd=manager=>{const set=setOf(manager),input=manager.querySelector('[data-npc-stock-item-ids]'),button=manager.querySelector('[data-npc-stock-add]');if(input)input.value=[...set].join(',');if(button){button.disabled=set.size===0;button.textContent=`加入库存（${set.size}）`;}};"
    "const apply=()=>{if(!state.active)return;const wanted=category.value,wantedQuality=quality.value,keyword=search.value.trim().toLowerCase(),set=setOf(state.active),current=existing(state.active);let shown=0;for(const choice of state.choices){const visible=allowed({dataset:{category:choice.dataset.category}},state.active)&&!current.has(choice.dataset.itemId)&&(wanted==='all'||choice.dataset.category===wanted)&&(!supportsQuality(state.active)||wantedQuality==='all'||choice.dataset.quality===wantedQuality)&&(!keyword||choice.dataset.search.includes(keyword));choice.hidden=!visible;const check=choice.querySelector('input');check.checked=set.has(choice.dataset.itemId);choice.classList.toggle('selected',check.checked);if(visible)shown++;}count.textContent=`找到 ${shown} 件可添加商品`;selectedCount.textContent=`已选择 ${set.size} 件`;empty.hidden=shown!==0;};"
    "const hide=()=>{modal.hidden=true;document.body.classList.remove('modal-open');const trigger=state.active&&state.active.querySelector('[data-npc-stock-open]');if(trigger)trigger.focus();};const show=manager=>{state.active=manager;rebuildCategories();rebuildQualities();modal.hidden=false;document.body.classList.add('modal-open');apply();search.focus();};"
    "const syncRemove=manager=>{const chosen=[...manager.querySelectorAll('[data-npc-stock-current-item]:checked')].map(input=>input.value),input=manager.querySelector('[data-npc-stock-remove-ids]'),button=manager.querySelector('[data-npc-stock-remove]'),open=manager.querySelector('[data-npc-stock-current-open]'),total=manager.querySelectorAll('[data-npc-stock-current-item]').length;if(input)input.value=chosen.join(',');if(button){button.disabled=chosen.length===0;button.textContent=`移除已选（${chosen.length}）`;}if(open)open.textContent=`管理已有库存（${total}）`;};"
    "const applyCurrent=manager=>{const wanted=manager.querySelector('[data-npc-stock-current-category]')?.value||'all',wantedQuality=manager.querySelector('[data-npc-stock-current-quality]')?.value||'all';for(const row of manager.querySelectorAll('[data-npc-stock-row]'))row.hidden=(wanted!=='all'&&row.dataset.npcStockCategory!==wanted)||(supportsQuality(manager)&&wantedQuality!=='all'&&row.dataset.npcStockQuality!==wantedQuality);};"
    "const hideCurrent=manager=>{const currentModal=manager.querySelector('[data-npc-stock-current-modal]');if(!currentModal)return;currentModal.hidden=true;document.body.classList.remove('modal-open');manager.querySelector('[data-npc-stock-current-open]')?.focus();},showCurrent=manager=>{const currentModal=manager.querySelector('[data-npc-stock-current-modal]');if(!currentModal)return;applyCurrent(manager);currentModal.hidden=false;document.body.classList.add('modal-open');};"
    "for(const manager of managers){syncAdd(manager);syncRemove(manager);applyCurrent(manager);manager.querySelector('[data-npc-stock-open]')?.addEventListener('click',()=>show(manager));manager.querySelector('[data-npc-stock-current-open]')?.addEventListener('click',()=>showCurrent(manager));manager.querySelector('[data-npc-stock-current-close]')?.addEventListener('click',()=>hideCurrent(manager));manager.querySelector('[data-npc-stock-current-modal]')?.addEventListener('click',event=>{if(event.target===event.currentTarget)hideCurrent(manager);});manager.querySelector('[data-npc-stock-current-category]')?.addEventListener('change',()=>applyCurrent(manager));manager.querySelector('[data-npc-stock-current-quality]')?.addEventListener('change',()=>applyCurrent(manager));manager.querySelector('[data-npc-stock-select-category]')?.addEventListener('click',()=>{for(const row of manager.querySelectorAll('[data-npc-stock-row]'))if(!row.hidden){const check=row.querySelector('[data-npc-stock-current-item]');if(check)check.checked=true;}syncRemove(manager);});for(const check of manager.querySelectorAll('[data-npc-stock-current-item]'))check.addEventListener('change',()=>syncRemove(manager));manager.querySelector('[data-npc-stock-add-form]')?.addEventListener('submit',event=>{syncAdd(manager);if(setOf(manager).size)return;event.preventDefault();show(manager);});manager.querySelector('[data-npc-stock-remove-form]')?.addEventListener('submit',event=>{syncRemove(manager);const selected=[...manager.querySelectorAll('[data-npc-stock-current-item]:checked')];if(!selected.length){event.preventDefault();return;}if(!window.confirm(`确定移除已选的 ${selected.length} 件库存商品吗？`))event.preventDefault();});}"
    "clear.addEventListener('click',()=>{if(!state.active)return;setOf(state.active).clear();syncAdd(state.active);apply();});selectCategory.addEventListener('click',()=>{if(!state.active)return;const set=setOf(state.active);for(const choice of state.choices)if(!choice.hidden)set.add(choice.dataset.itemId);syncAdd(state.active);apply();});unselectCategory.addEventListener('click',()=>{if(!state.active)return;const set=setOf(state.active);for(const choice of state.choices)if(!choice.hidden)set.delete(choice.dataset.itemId);syncAdd(state.active);apply();});confirm.addEventListener('click',hide);close.addEventListener('click',hide);modal.addEventListener('click',event=>{if(event.target===modal)hide();});document.addEventListener('keydown',event=>{if(event.key==='Escape'&&!modal.hidden)hide();});document.addEventListener('keydown',event=>{if(event.key!=='Escape')return;for(const manager of managers){const currentModal=manager.querySelector('[data-npc-stock-current-modal]');if(currentModal&&!currentModal.hidden){hideCurrent(manager);break;}}});category.addEventListener('change',()=>{rebuildQualities();apply();});quality.addEventListener('change',apply);search.addEventListener('input',apply);"
    "};"
    "const setupMonsterDrops=()=>{"
    "const manager=document.querySelector('[data-monster-drop-manager]'),box=document.querySelector('#monster-drop-list'),source=document.querySelector('#item-picker-options'),modal=document.querySelector('#monster-drop-picker-modal'),close=document.querySelector('#monster-drop-picker-close'),clear=document.querySelector('#monster-drop-picker-clear'),category=document.querySelector('#monster-drop-category'),quality=document.querySelector('#monster-drop-quality'),qualityField=document.querySelector('[data-monster-drop-quality-field]'),level=document.querySelector('#monster-drop-level'),levelField=document.querySelector('[data-monster-drop-level-field]'),currentModal=document.querySelector('[data-monster-drop-current-modal]'),currentOpen=document.querySelector('[data-monster-drop-current-open]'),currentClose=document.querySelector('[data-monster-drop-current-close]'),currentQualityField=document.querySelector('[data-monster-drop-current-quality-field]'),search=document.querySelector('#monster-drop-search'),list=document.querySelector('#monster-drop-picker-list'),count=document.querySelector('#monster-drop-result-count'),selectedCount=document.querySelector('#monster-drop-selection-count'),error=document.querySelector('#monster-drop-picker-error'),empty=document.querySelector('#monster-drop-picker-empty'),selectFiltered=document.querySelector('#monster-drop-select-category'),unselectFiltered=document.querySelector('#monster-drop-unselect-category'),confirm=document.querySelector('#monster-drop-picker-confirm');"
    "if(!manager||!box||!source||!modal||!close||!clear||!category||!quality||!qualityField||!level||!levelField||!currentModal||!currentOpen||!currentClose||!currentQualityField||!search||!list||!count||!selectedCount||!error||!empty||!selectFiltered||!unselectFiltered||!confirm)return;"
    "const rows=[...box.querySelectorAll('[data-monster-drop-row]')],options=[...source.options].filter(option=>option.value&&option.value!=='0'),optionById=new Map(options.map(option=>[option.value,option])),state={selected:new Set(),choices:[]};"
    "const inputOf=row=>row.querySelector('[data-item-picker-input]'),rateOf=row=>row.querySelector('[data-drop-rate]'),checkOf=row=>row.querySelector('[data-monster-drop-current-item]'),activeRows=()=>rows.filter(row=>{const input=inputOf(row);return input&&input.value&&input.value!=='0';}),freeRows=()=>rows.filter(row=>{const input=inputOf(row);return input&&(!input.value||input.value==='0');}),usedIds=()=>new Set(activeRows().map(row=>inputOf(row).value));"
    "const syncLabel=row=>{const input=inputOf(row);if(input)window.dispatchEvent(new CustomEvent('cbe-item-picker-sync',{detail:{id:input.id}}));};"
    "const setRowMetadata=(row,option)=>{if(!option){row.dataset.monsterDropCategory='i0';row.dataset.monsterDropQuality='0';row.dataset.monsterDropLevel='0';return;}row.dataset.monsterDropCategory=option.dataset.category||'i0';row.dataset.monsterDropQuality=option.dataset.quality||'0';row.dataset.monsterDropLevel=option.dataset.level||'0';};"
    "const syncCurrent=()=>{const chosen=activeRows().filter(row=>checkOf(row)?.checked);const button=manager.querySelector('[data-monster-drop-remove-current]');if(button){button.disabled=chosen.length===0;button.textContent=`移除已选（${chosen.length}）`;}currentOpen.textContent=`管理已有掉落（${activeRows().length}）`;};"
    "const currentSupportsQuality=()=>{const wanted=manager.querySelector('[data-monster-drop-current-category]')?.value||'all';return wanted==='all'?activeRows().some(row=>(row.dataset.monsterDropCategory||'').startsWith('e')):wanted.startsWith('e');};"
    "const applyCurrent=()=>{const wanted=manager.querySelector('[data-monster-drop-current-category]')?.value||'all',wantedQuality=manager.querySelector('[data-monster-drop-current-quality]')?.value||'all',supports=currentSupportsQuality();currentQualityField.hidden=!supports;if(!supports){const field=manager.querySelector('[data-monster-drop-current-quality]');if(field)field.value='all';}for(const row of rows){const input=inputOf(row);if(!input||!input.value||input.value==='0'){row.hidden=true;continue;}const itemCategory=row.dataset.monsterDropCategory||'',itemQuality=row.dataset.monsterDropQuality||'0';row.hidden=(wanted!=='all'&&itemCategory!==wanted)||(supports&&wantedQuality!=='all'&&itemQuality!==wantedQuality);}syncCurrent();};"
    "const syncAdd=()=>{const button=manager.querySelector('[data-monster-drop-add]'),free=freeRows().length,selected=state.selected.size;if(button){button.disabled=selected===0||selected>free;button.textContent=`加入掉落（${selected}）`;}};"
    "for(const option of options){const choice=document.createElement('label'),check=document.createElement('input'),body=document.createElement('span'),title=document.createElement('strong'),meta=document.createElement('small'),isEquipment=option.dataset.equip==='1';choice.className='item-choice monster-drop-choice';choice.dataset.itemId=option.value;choice.dataset.category=option.dataset.category||'';choice.dataset.quality=option.dataset.quality||'0';choice.dataset.level=option.dataset.level||'0';choice.dataset.search=option.textContent.toLowerCase();check.type='checkbox';title.textContent=option.textContent;meta.textContent=isEquipment?`品质 ${choice.dataset.quality} · 需求等级 ${choice.dataset.level}`:'普通物品';body.append(title,meta);choice.append(check,body);check.addEventListener('change',()=>{if(check.checked)state.selected.add(option.value);else state.selected.delete(option.value);applyPicker();syncAdd();});state.choices.push(choice);list.append(choice);}"
    "const pickerSupportsEquipment=()=>{const wanted=category.value;return wanted==='all'||wanted.startsWith('e');};"
    "const rebuildLevels=()=>{const previous=level.value;level.replaceChildren();const all=document.createElement('option');all.value='all';all.textContent='全部等级';level.append(all);if(!pickerSupportsEquipment()){levelField.hidden=true;level.value='all';return;}levelField.hidden=false;const wanted=category.value,levels=[...new Set(state.choices.filter(choice=>choice.dataset.category.startsWith('e')&&(wanted==='all'||choice.dataset.category===wanted)).map(choice=>choice.dataset.level||'0'))].sort((a,b)=>Number(a)-Number(b));for(const value of levels){const option=document.createElement('option');option.value=value;option.textContent=`需求等级 ${value}`;level.append(option);}level.value=[...level.options].some(option=>option.value===previous)?previous:'all';};"
    "const applyPicker=()=>{const wanted=category.value,supports=pickerSupportsEquipment(),keyword=search.value.trim().toLowerCase(),used=usedIds();qualityField.hidden=!supports;levelField.hidden=!supports;if(!supports){quality.value='all';level.value='all';}const wantedQuality=quality.value,wantedLevel=level.value,hasEquipmentFilter=supports&&(wantedQuality!=='all'||wantedLevel!=='all');let shown=0;for(const choice of state.choices){const isEquipment=choice.dataset.category.startsWith('e'),equipmentMatches=!hasEquipmentFilter||(isEquipment&&(wantedQuality==='all'||choice.dataset.quality===wantedQuality)&&(wantedLevel==='all'||choice.dataset.level===wantedLevel)),visible=!used.has(choice.dataset.itemId)&&(wanted==='all'||choice.dataset.category===wanted)&&equipmentMatches&&(!keyword||choice.dataset.search.includes(keyword));choice.hidden=!visible;const check=choice.querySelector('input');check.checked=state.selected.has(choice.dataset.itemId);choice.classList.toggle('selected',check.checked);if(visible)shown++;}count.textContent=`找到 ${shown} 件可添加物品`;selectedCount.textContent=`已选择 ${state.selected.size} 件，剩余 ${freeRows().length} 个槽位`;empty.hidden=shown!==0;};"
    "const hide=()=>{modal.hidden=true;document.body.classList.remove('modal-open');manager.querySelector('[data-monster-drop-open]')?.focus();};const show=()=>{modal.hidden=false;document.body.classList.add('modal-open');error.textContent='';applyPicker();search.focus();};const hideCurrent=()=>{currentModal.hidden=true;document.body.classList.remove('modal-open');currentOpen.focus();},showCurrent=()=>{applyCurrent();currentModal.hidden=false;document.body.classList.add('modal-open');};"
    "const clearRow=row=>{const input=inputOf(row),rate=rateOf(row),check=checkOf(row);if(input)input.value='0';if(rate)rate.value='0';if(check)check.checked=false;setRowMetadata(row,null);row.hidden=true;syncLabel(row);};"
    "const addSelected=()=>{const free=freeRows(),selected=[...state.selected];if(!selected.length)return;if(selected.length>free.length){error.textContent=`当前只剩 ${free.length} 个空掉落槽位，请减少选择或先移除已有掉落`;show();return;}const rate=Math.max(1,Math.min(100,Number(manager.querySelector('[data-monster-drop-default-rate]')?.value)||100));selected.forEach((itemId,index)=>{const row=free[index],option=optionById.get(itemId),input=inputOf(row),rateInput=rateOf(row),check=checkOf(row);if(!row||!option||!input)return;input.value=itemId;if(rateInput)rateInput.value=String(rate);if(check)check.checked=false;setRowMetadata(row,option);row.hidden=false;syncLabel(row);});state.selected.clear();syncAdd();applyCurrent();applyPicker();hide();};"
    "for(const row of rows){const input=inputOf(row),remove=row.querySelector('[data-drop-remove]'),check=checkOf(row);if(input)input.addEventListener('change',()=>{const option=optionById.get(input.value);if(option){setRowMetadata(row,option);row.hidden=false;}else setRowMetadata(row,null);applyCurrent();applyPicker();syncAdd();});if(check)check.addEventListener('change',syncCurrent);if(remove)remove.addEventListener('click',()=>{clearRow(row);applyCurrent();applyPicker();syncAdd();});}"
    "manager.querySelector('[data-monster-drop-open]')?.addEventListener('click',show);manager.querySelector('[data-monster-drop-add]')?.addEventListener('click',addSelected);currentOpen.addEventListener('click',showCurrent);currentClose.addEventListener('click',hideCurrent);currentModal.addEventListener('click',event=>{if(event.target===currentModal)hideCurrent();});manager.querySelector('[data-monster-drop-current-category]')?.addEventListener('change',applyCurrent);manager.querySelector('[data-monster-drop-current-quality]')?.addEventListener('change',applyCurrent);manager.querySelector('[data-monster-drop-select-current]')?.addEventListener('click',()=>{for(const row of activeRows())if(!row.hidden){const check=checkOf(row);if(check)check.checked=true;}syncCurrent();});manager.querySelector('[data-monster-drop-remove-current]')?.addEventListener('click',()=>{const selected=activeRows().filter(row=>checkOf(row)?.checked);if(!selected.length)return;if(!window.confirm(`确定移除已选的 ${selected.length} 项掉落吗？`))return;selected.forEach(clearRow);applyCurrent();applyPicker();syncAdd();});"
    "clear.addEventListener('click',()=>{state.selected.clear();syncAdd();applyPicker();});selectFiltered.addEventListener('click',()=>{for(const choice of state.choices)if(!choice.hidden)state.selected.add(choice.dataset.itemId);syncAdd();applyPicker();});unselectFiltered.addEventListener('click',()=>{for(const choice of state.choices)if(!choice.hidden)state.selected.delete(choice.dataset.itemId);syncAdd();applyPicker();});confirm.addEventListener('click',hide);close.addEventListener('click',hide);modal.addEventListener('click',event=>{if(event.target===modal)hide();});document.addEventListener('keydown',event=>{if(event.key==='Escape'&&!modal.hidden)hide();});document.addEventListener('keydown',event=>{if(event.key==='Escape'&&!currentModal.hidden)hideCurrent();});category.addEventListener('change',()=>{rebuildLevels();applyPicker();});quality.addEventListener('change',applyPicker);level.addEventListener('change',applyPicker);search.addEventListener('input',applyPicker);syncAdd();applyCurrent();rebuildLevels();applyPicker();};"
    "const setupTaskRewards=()=>{const box=document.querySelector('#task-reward-list'),add=document.querySelector('#task-reward-add');if(!box||!add)return;const rows=[...box.querySelectorAll('[data-task-reward-row]')];const sync=row=>{const input=row.querySelector('[data-item-picker-input]');if(input)window.dispatchEvent(new CustomEvent('cbe-item-picker-sync',{detail:{id:input.id}}));};const showNext=()=>{const next=rows.find(row=>row.hidden);if(next){next.hidden=false;sync(next);}add.disabled=!rows.some(row=>row.hidden);};add.addEventListener('click',showNext);for(const remove of box.querySelectorAll('[data-task-reward-remove]'))remove.addEventListener('click',()=>{const row=remove.closest('[data-task-reward-row]');if(!row)return;const input=row.querySelector('[data-item-picker-input]'),quantity=row.querySelector('[data-task-reward-count]'),type=row.querySelector('[data-task-reward-type]');if(input)input.value='0';if(quantity)quantity.value='0';if(type)type.value='0';row.hidden=true;sync(row);if(!rows.some(current=>!current.hidden))showNext();add.disabled=false;});add.disabled=!rows.some(row=>row.hidden);};"
    "const setupChestRewards=()=>{for(const box of document.querySelectorAll('[data-chest-reward-list]')){const form=box.closest('form'),add=form&&form.querySelector('[data-chest-reward-add]'),rows=[...box.querySelectorAll('[data-chest-reward-row]')];if(!form||!add||!rows.length)continue;const sync=row=>{const input=row.querySelector('[data-item-picker-input]');if(input)window.dispatchEvent(new CustomEvent('cbe-item-picker-sync',{detail:{id:input.id}}));};const shown=()=>rows.filter(row=>!row.hidden);const update=()=>{const active=shown(),total=active.reduce((sum,row)=>sum+Math.max(0,Number(row.querySelector('[data-chest-reward-weight]')?.value)||0),0);active.forEach((row,index)=>{const number=row.querySelector('[data-chest-reward-index]'),probability=row.querySelector('[data-chest-reward-probability]'),weight=Math.max(0,Number(row.querySelector('[data-chest-reward-weight]')?.value)||0);if(number)number.textContent='#'+(index+1);if(probability)probability.textContent=total&&weight?(weight*100/total).toFixed(2)+'%':'—';});add.disabled=!rows.some(row=>row.hidden);add.textContent=`＋ 添加奖励（${active.length}/${rows.length}）`;};const showNext=()=>{const next=rows.find(row=>row.hidden);if(!next)return;next.hidden=false;sync(next);update();};add.addEventListener('click',showNext);for(const row of rows){for(const field of row.querySelectorAll('[data-chest-reward-count],[data-chest-reward-weight]'))field.addEventListener('input',update);const remove=row.querySelector('[data-chest-reward-remove]');if(!remove)continue;remove.addEventListener('click',()=>{const item=row.querySelector('[data-item-picker-input]'),count=row.querySelector('[data-chest-reward-count]'),weight=row.querySelector('[data-chest-reward-weight]'),broadcast=row.querySelector('[data-chest-reward-broadcast]');if(item)item.value='0';if(count)count.value='0';if(weight)weight.value='0';if(broadcast)broadcast.checked=false;row.hidden=true;sync(row);if(!shown().length){const first=rows[0];first.hidden=false;sync(first);}update();});}update();}};"
    "const setupMonsterSearch=()=>{const input=document.querySelector('#monster-search'),list=document.querySelector('#monster-list');if(!input||!list||input.dataset.monsterSearchBound==='1')return;input.dataset.monsterSearchBound='1';const apply=()=>{const keyword=input.value.trim().toLowerCase();for(const row of list.querySelectorAll('.monster')){const key=(row.dataset.key||'').toLowerCase(),host=row.closest('[data-monster-row]')||row;host.hidden=!!keyword&&!key.includes(keyword);}};input.addEventListener('input',apply);apply();};"
    "const setupMonsterBatchReset=()=>{const manager=document.querySelector('[data-monster-batch-manager]'),list=document.querySelector('#monster-list');if(!manager||!list||manager.dataset.monsterBatchBound==='1')return;manager.dataset.monsterBatchBound='1';const ids=[...manager.querySelectorAll('[data-monster-batch-ids]')],combat=manager.querySelector('[data-monster-batch-reset]'),sceneLevel=manager.querySelector('[data-monster-batch-level-reset]'),visible=manager.querySelector('[data-monster-batch-select-visible]'),clear=manager.querySelector('[data-monster-batch-clear]'),items=()=>[...list.querySelectorAll('[data-monster-batch-item]')],sync=()=>{const selected=items().filter(item=>item.checked).map(item=>item.value),value=selected.join(',');for(const input of ids)input.value=value;if(combat){combat.disabled=!selected.length;combat.textContent=`批量重置属性与奖励（${selected.length}）`;}if(sceneLevel){sceneLevel.disabled=!selected.length;sceneLevel.textContent=`按场景等级重置（${selected.length}）`;}};list.addEventListener('change',event=>{if(event.target.matches('[data-monster-batch-item]'))sync();});list.addEventListener('cbe-monster-list-updated',sync);visible?.addEventListener('click',()=>{for(const item of items()){const row=item.closest('[data-monster-row]');if(row&&!row.hidden)item.checked=true;}sync();});clear?.addEventListener('click',()=>{for(const item of items())item.checked=false;sync();});sync();};"
    "const setupMonsterActions=()=>{if(document.documentElement.dataset.monsterActionsBound==='1')return;document.documentElement.dataset.monsterActionsBound='1';document.addEventListener('submit',event=>{const form=event.target;if(!form?.matches('[data-monster-action]')||event.defaultPrevented)return;const confirmation=form.dataset.confirmMessage;if(confirmation&&!window.confirm(confirmation)){event.preventDefault();return;}event.preventDefault();if(form.dataset.submitting==='1')return;const list=document.querySelector('#monster-list'),detail=document.querySelector('[data-admin-detail]'),actionUrl=form.getAttribute('action')||window.location.href;if(!list||!detail)return;form.dataset.submitting='1';detail.setAttribute('aria-busy','true');fetch(actionUrl,{method:'POST',body:new FormData(form),credentials:'same-origin',cache:'no-store',redirect:'follow'}).then(response=>{if(!response.ok)throw new Error(`HTTP ${response.status}`);return response.text().then(html=>({html,url:response.url}));}).then(({html,url})=>{const next=new DOMParser().parseFromString(html,'text/html'),nextList=next.querySelector('#monster-list'),nextDetail=next.querySelector('[data-admin-detail]');if(!nextList||!nextDetail){if(new URL(url,window.location.href).pathname.endsWith('/login')){window.location.assign(url);return;}throw new Error('missing monster fragment');}const top=list.scrollTop;detail.innerHTML=nextDetail.innerHTML;list.innerHTML=nextList.innerHTML;list.scrollTop=top;document.querySelector('#monster-search')?.dispatchEvent(new Event('input'));list.dispatchEvent(new Event('cbe-monster-list-updated'));document.title=next.title||document.title;history.replaceState(null,'',url);setupItemPicker();setupMonsterDrops();setupTaskRewards();}).catch(()=>{const status=detail.querySelector('[data-monster-action-status]');if(status)status.innerHTML='<div class=\"notice error\">怪物操作提交失败，请稍后重试。</div>';}).finally(()=>{detail.removeAttribute('aria-busy');form.dataset.submitting='0';});});};"
    "const setupTaskActions=()=>{if(document.documentElement.dataset.taskActionsBound==='1')return;document.documentElement.dataset.taskActionsBound='1';document.addEventListener('submit',event=>{const form=event.target;if(!form?.matches('[data-task-action]')||event.defaultPrevented)return;event.preventDefault();if(form.dataset.submitting==='1')return;const catalog=document.querySelector('[data-task-catalog]'),detail=document.querySelector('[data-admin-detail]'),list=catalog?.querySelector('[data-admin-list]'),actionUrl=form.getAttribute('action')||window.location.href;if(!catalog||!detail||!list)return;form.dataset.submitting='1';detail.setAttribute('aria-busy','true');fetch(actionUrl,{method:'POST',body:new FormData(form),credentials:'same-origin',cache:'no-store',redirect:'follow'}).then(response=>{if(!response.ok)throw new Error('HTTP '+response.status);return response.text().then(html=>({html,url:response.url}));}).then(({html,url})=>{const next=new DOMParser().parseFromString(html,'text/html'),nextCatalog=next.querySelector('[data-task-catalog]'),nextDetail=next.querySelector('[data-admin-detail]');if(!nextCatalog||!nextDetail)throw new Error('missing task fragment');const top=list.scrollTop;catalog.innerHTML=nextCatalog.innerHTML;detail.innerHTML=nextDetail.innerHTML;catalog.querySelector('[data-admin-list]')?.scrollTo(0,top);document.title=next.title||document.title;history.replaceState(null,'',url);setupItemPicker();setupTaskRewards();}).catch(()=>{const status=detail.querySelector('[data-task-action-status]');if(status)status.innerHTML='<div class=\"notice error\">任务操作提交失败，请稍后重试。</div>';}).finally(()=>{detail.removeAttribute('aria-busy');form.dataset.submitting='0';});});};"
    "const setupActorPicker=()=>{"
    "const state=window.__cbeActorPickerState||(window.__cbeActorPickerState={bound:false});"
    "const source=document.querySelector('#actor-picker-options'),modal=document.querySelector('#actor-picker-modal'),close=document.querySelector('#actor-picker-close'),search=document.querySelector('#actor-picker-search'),list=document.querySelector('#actor-picker-list'),count=document.querySelector('#actor-result-count'),empty=document.querySelector('#actor-picker-empty'),error=document.querySelector('#actor-picker-error'),selects=[...document.querySelectorAll('select.actor-resource-select')];"
    "if(!source||!modal||!close||!search||!list||!count||!empty||!error||!selects.length)return;"
    "state.source=source;state.modal=modal;state.close=close;state.search=search;state.list=list;state.count=count;state.empty=empty;state.error=error;state.selects=selects;state.options=[...source.options].filter(option=>option.value);state.optionByValue=new Map(state.options.map(option=>[option.value,option]));state.choices=[];state.renderedSource=null;"
    "state.update=select=>{if(!select)return;const field=select.closest('.actor-picker-field'),trigger=field&&field.querySelector('[data-actor-picker-open]');if(!trigger)return;const option=state.optionByValue.get(select.value),selected=select.options[select.selectedIndex],label=trigger.querySelector('[data-actor-picker-label]');if(label)label.textContent=option?option.textContent:(selected&&selected.textContent?selected.textContent:(select.value?`不可用资源：${select.value}`:'请选择 Actor 资源'));};"
    "for(const select of selects){if(select.dataset.actorPickerBound!=='1'){select.dataset.actorPickerBound='1';select.addEventListener('change',()=>state.update(select));}state.update(select);}"
    "state.render=()=>{if(state.renderedSource===state.source)return;state.list.replaceChildren();state.choices=[];const previewBase=new URL('actor-preview.svg',window.location.href).href;for(const option of state.options){const button=document.createElement('button'),image=document.createElement('img'),title=document.createElement('strong');button.type='button';button.className='actor-choice';button.dataset.search=option.textContent.toLowerCase();image.loading='lazy';image.alt=option.textContent+' 预览';image.src=previewBase+'?actor='+encodeURIComponent(option.value);image.addEventListener('error',()=>{image.hidden=true;});title.textContent=option.textContent;button.append(image,title);button.addEventListener('click',()=>{if(!state.active)return;state.active.value=option.value;state.active.dispatchEvent(new Event('change',{bubbles:true}));state.error.textContent='';state.hide();});state.choices.push(button);state.list.append(button);}state.renderedSource=state.source;};"
    "state.apply=()=>{state.render();const keyword=state.search.value.trim().toLowerCase();let shown=0;for(const choice of state.choices){const visible=!keyword||choice.dataset.search.includes(keyword);choice.hidden=!visible;if(visible)shown++;}state.count.textContent=`找到 ${shown} 个 Actor`;state.empty.hidden=shown!==0;};"
    "state.show=select=>{if(!select||!select.isConnected)return;state.active=select;state.modal.hidden=false;document.body.classList.add('modal-open');state.error.textContent='';state.apply();state.search.focus();};"
    "state.hide=()=>{if(!state.modal)return;state.modal.hidden=true;document.body.classList.remove('modal-open');const field=state.active&&state.active.closest('.actor-picker-field'),trigger=field&&field.querySelector('[data-actor-picker-open]');if(trigger)trigger.focus();};"
    "if(!state.bound){state.bound=true;document.addEventListener('click',event=>{const trigger=event.target.closest('[data-actor-picker-open]');if(!trigger)return;const field=trigger.closest('.actor-picker-field'),select=field&&field.querySelector('select.actor-resource-select');if(!select||select.disabled)return;event.preventDefault();state.show(select);});document.addEventListener('keydown',event=>{if(event.key==='Escape'&&state.modal&&!state.modal.hidden)state.hide();});document.addEventListener('submit',event=>{const form=event.target;if(!form||!form.closest('.npc'))return;const missing=[...form.querySelectorAll('select.actor-resource-select')].find(select=>!select.value);if(!missing)return;event.preventDefault();if(state.error)state.error.textContent='请先选择一个可用的 Actor 资源';state.show(missing);});}"
    "if(modal.dataset.actorPickerBound!=='1'){modal.dataset.actorPickerBound='1';close.addEventListener('click',()=>state.hide());modal.addEventListener('click',event=>{if(event.target===modal)state.hide();});search.addEventListener('input',()=>state.apply());}"
    "};"
    "const setupNpcKinds=()=>{for(const form of document.querySelectorAll('.npc form')){"
    "const kind=form.querySelector('select[name=kind]');const fields=form.querySelector('.instance-fields');"
    "if(!kind||!fields)continue;const apply=()=>{const show=kind.value==='6';"
    "fields.hidden=!show;for(const input of fields.querySelectorAll('input,select'))input.disabled=!show;};"
    "kind.addEventListener('change',apply);apply();}};"
    "const setupAccountList=()=>{"
    "const list=document.querySelector('[data-account-list]'),form=document.querySelector('[data-account-search-form]'),input=document.querySelector('[data-account-search]'),status=document.querySelector('[data-account-list-status]');"
    "if(!list||!form||!input||!status)return;const initialState=list.querySelector('[data-account-page-state]');let query=input.value.trim(),next=Number(initialState?initialState.dataset.next:0),more=initialState?initialState.dataset.hasMore==='1':false,loading=false,revision=0,timer=0;if(initialState)initialState.remove();"
    "const updateStatus=message=>{status.textContent=message;status.classList.remove('error');};"
    "const fail=message=>{status.textContent=message;status.classList.add('error');};"
    "const count=()=>list.querySelectorAll('.account').length;"
    "const load=async reset=>{if(loading||(!reset&&!more))return;loading=true;const requestedQuery=query,requestedCursor=reset?0:next,requestedRevision=revision;updateStatus(reset?'正在搜索…':'正在加载更多…');"
    "try{const url=new URL('accounts',window.location.href);url.searchParams.set('q',requestedQuery);url.searchParams.set('cursor',String(requestedCursor));const response=await fetch(url,{credentials:'same-origin',cache:'no-store'});if(!response.ok)throw new Error('HTTP '+response.status);const html=await response.text();if(requestedRevision!==revision)return;const template=document.createElement('template');template.innerHTML=html.trim();const state=template.content.querySelector('[data-account-page-state]');if(!state)throw new Error('invalid account page');const parsedNext=Number(state.dataset.next||0);if(!Number.isInteger(parsedNext)||parsedNext<0)throw new Error('invalid cursor');const parsedMore=state.dataset.hasMore==='1';state.remove();if(reset)list.replaceChildren();list.append(template.content);next=parsedNext;more=parsedMore;const visible=count();updateStatus(visible?`已显示 ${visible} 个账号${more?'，向下滚动加载更多':''}`:'未找到匹配账号');if(more&&list.scrollHeight<=list.clientHeight+4)setTimeout(()=>load(false),0);}catch(error){if(requestedRevision===revision)fail('账号列表加载失败，请稍后重试');}finally{loading=false;}};"
    "const reset=()=>{const wanted=input.value.trim();if(wanted===query&&count())return;query=wanted;next=0;more=true;revision++;load(true);};"
    "form.addEventListener('submit',event=>{event.preventDefault();clearTimeout(timer);reset();});input.addEventListener('input',()=>{clearTimeout(timer);timer=setTimeout(reset,220);});list.addEventListener('scroll',()=>{if(list.scrollTop+list.clientHeight>=list.scrollHeight-72)load(false);},{passive:true});"
    "if(!count())load(true);else updateStatus(`已显示 ${count()} 个账号${more?'，向下滚动加载更多':''}`);};"
    "const setupContentUpdatePicker=()=>{const search=document.querySelector('[data-content-update-search]'),select=document.querySelector('[data-content-update-select]'),selectFiltered=document.querySelector('[data-content-update-select-filtered]'),clear=document.querySelector('[data-content-update-clear-selection]');if(!search||!select||search.dataset.bound==='1')return;search.dataset.bound='1';const apply=()=>{const q=search.value.trim().toLowerCase();for(const option of select.options)option.hidden=!!q&&!option.textContent.toLowerCase().includes(q);};search.addEventListener('input',apply);selectFiltered?.addEventListener('click',()=>{for(const option of select.options)if(!option.hidden)option.selected=true;});clear?.addEventListener('click',()=>{for(const option of select.options)option.selected=false;});apply();};"
    "const setupPartialNavigation=()=>{let serial=0;const selector='[data-admin-select]';const sameTab=url=>{const current=new URL(window.location.href);return url.origin===current.origin&&url.searchParams.get('tab')===current.searchParams.get('tab');};const markSelected=(list,nextList,url)=>{const next=nextList.querySelector(`${selector}[aria-current=page],${selector}.on`),selectedHref=next?new URL(next.getAttribute('href'),url).href:url.href;for(const link of list.querySelectorAll(selector)){const match=new URL(link.getAttribute('href'),window.location.href).href===selectedHref;link.classList.toggle('on',match);if(match){link.setAttribute('aria-current','page');if(next&&next.id)link.id=next.id;}else{link.removeAttribute('aria-current');if(link.id&&link.id.startsWith('selected-'))link.removeAttribute('id');}}};const load=async(url,historyMode)=>{const list=document.querySelector('[data-admin-list]'),detail=document.querySelector('[data-admin-detail]');if(!list||!detail)return false;const request=++serial,scrollTop=list.scrollTop;detail.setAttribute('aria-busy','true');try{const response=await fetch(url,{credentials:'same-origin',cache:'no-store'});if(!response.ok)throw new Error(`HTTP ${response.status}`);const html=await response.text();if(request!==serial)return true;const next=new DOMParser().parseFromString(html,'text/html'),nextList=next.querySelector('[data-admin-list]'),nextDetail=next.querySelector('[data-admin-detail]');if(!nextList||!nextDetail)throw new Error('missing admin fragment');detail.innerHTML=nextDetail.innerHTML;markSelected(list,nextList,url);list.scrollTop=scrollTop;document.title=next.title||document.title;if(historyMode==='push')history.pushState(null,'',url);setupItemPicker();setupNpcStock();setupMonsterDrops();setupTaskRewards();setupActorPicker();setupNpcKinds();setupContentUpdatePicker();return true;}catch(error){if(request===serial)window.location.assign(url);return false;}finally{if(request===serial)detail.removeAttribute('aria-busy');}};document.addEventListener('click',event=>{const link=event.target.closest(selector);if(!link||event.defaultPrevented||event.button!==0||event.metaKey||event.ctrlKey||event.shiftKey||event.altKey||link.target&&link.target!=='_self')return;const url=new URL(link.href,window.location.href);if(!sameTab(url))return;event.preventDefault();void load(url,'push');});window.addEventListener('popstate',()=>{const url=new URL(window.location.href);if(sameTab(url))void load(url,'none');});};"
    "const setupAdminContent=()=>{setupAccountList();setupMonsterSearch();setupMonsterBatchReset();keep('.scene-list','cbe-admin-scenes-scroll');keep('.shop-list','cbe-admin-shop-scroll');keep('.update-menu','cbe-admin-update-menu-scroll');setupItemPicker();setupNpcStock();setupMonsterDrops();setupTaskRewards();setupChestRewards();setupActorPicker();setupNpcKinds();setupContentUpdatePicker();};"
    "const setupAdminLayout=()=>{if(document.querySelector('#admin-spa-layout-style'))return;const style=document.createElement('style');style.id='admin-spa-layout-style';style.textContent='#admin-spa-content{display:contents!important}#admin-spa-content[aria-busy=true]>*{opacity:.62;pointer-events:none;transition:opacity .12s ease}';document.head.append(style);};"
    "const setupAdminHeader=()=>{const main=document.querySelector('#admin-spa-shell'),header=main&&[...main.children].find(node=>node.matches&&node.matches('header'));if(!header||header.dataset.adminSpaHeader==='1')return;header.dataset.adminSpaHeader='1';const logout=header.querySelector('form[action$=\"/logout\"]'),intro=document.createElement('div'),style=document.createElement('style');intro.innerHTML='<h1>江湖 OL 后台管理</h1><p class=\"sub\">账号、游戏内容与运营配置统一管理</p>';header.replaceChildren(intro);if(logout)header.append(logout);style.id='admin-spa-header-style';style.textContent='#admin-spa-shell>header[data-admin-spa-header]{display:flex!important;align-items:center;justify-content:space-between;gap:16px;margin:0 0 12px;padding:4px 2px}#admin-spa-shell>header[data-admin-spa-header] h1{margin:0;color:#183d6e;font-size:22px}#admin-spa-shell>header[data-admin-spa-header] .sub{margin:5px 0 0;color:#63738a}';document.head.append(style);};"
    "const setupAdminSpa=()=>{if(document.documentElement.dataset.adminSpaBound==='1')return;const main=document.querySelector('main.wrap'),nav=main&&[...main.children].find(node=>node.matches&&node.matches('nav.tabs'));if(!main||!nav)return;document.documentElement.dataset.adminSpaBound='1';main.id='admin-spa-shell';nav.id='admin-spa-tabs';nav.classList.add('admin-spa-tabs');const tabs=[['accounts','账号管理'],['content','游戏内容管理'],['tasks','任务管理'],['monsters','怪物管理'],['scene-monsters','场景战斗怪'],['actors','Actor 资源'],['shop','商品管理'],['chests','宝箱管理'],['updates','游戏内容更新管理'],['servers','服务器列表'],['risk','风险角色管理']],content=document.createElement('section'),base=new URL('.',window.location.href).pathname;content.id='admin-spa-content';content.dataset.adminSpaContent='1';for(let node=nav.nextSibling;node;){const next=node.nextSibling;content.append(node);node=next;}main.append(content);if(!document.querySelector('#admin-spa-style')){const style=document.createElement('style');style.id='admin-spa-style';style.textContent='#admin-spa-shell{min-height:100vh}#admin-spa-tabs{display:flex!important;align-items:center;gap:8px;flex-wrap:wrap;margin:0 0 16px;padding:10px 12px;border:1px solid #d6dfed;border-radius:12px;background:#f7f9fd;box-shadow:none}#admin-spa-tabs .admin-spa-tab{display:inline-flex!important;align-items:center;justify-content:center;min-height:34px;margin:0!important;padding:0 13px;border:1px solid #d4ddec;border-radius:8px;background:#fff;color:#385170;font-size:14px;font-weight:650;line-height:1;text-decoration:none;box-shadow:none}#admin-spa-tabs .admin-spa-tab:hover{border-color:#4d77bd;color:#174f9d;background:#f3f7ff}#admin-spa-tabs .admin-spa-tab.on,#admin-spa-tabs .admin-spa-tab[aria-current=page]{border-color:#1f62c9;background:#1f62c9;color:#fff}#admin-spa-content{min-width:0}#admin-spa-content[aria-busy=true]{opacity:.62;pointer-events:none;transition:opacity .12s ease}';document.head.append(style);}const currentTab=url=>url.searchParams.get('tab')||'accounts',setTab=url=>{const tab=currentTab(url);nav.innerHTML=tabs.map(([key,label])=>`<a class=\"tab admin-spa-tab${key===tab?' on':''}\" data-admin-tab=\"${key}\"${key===tab?' aria-current=\"page\"':''} href=\"?tab=${encodeURIComponent(key)}\">${label}</a>`).join('');};const own=url=>url.origin===window.location.origin&&url.pathname.startsWith(base),remoteContent=doc=>{const nextMain=doc.querySelector('main.wrap'),nextNav=nextMain&&[...nextMain.children].find(node=>node.matches&&node.matches('nav.tabs'));if(!nextMain||!nextNav)return null;const template=document.createElement('template');for(const style of doc.head.querySelectorAll('style'))template.content.append(style.cloneNode(true));for(let node=nextNav.nextSibling;node;node=node.nextSibling)template.content.append(node.cloneNode(true));return template.innerHTML;};let serial=0;const replace=(doc,url,historyMode,form)=>{const html=remoteContent(doc);if(html===null)return false;const detail=form&&form.closest('[data-admin-detail]'),nextDetail=doc.querySelector('[data-admin-detail]'),list=document.querySelector('[data-admin-list]'),nextList=doc.querySelector('[data-admin-list]');if(detail&&nextDetail){const top=list?list.scrollTop:0;detail.innerHTML=nextDetail.innerHTML;if(list&&nextList){list.innerHTML=nextList.innerHTML;list.scrollTop=top;list.dispatchEvent(new Event('cbe-monster-list-updated'));}}else content.innerHTML=html;setTab(url);document.title=doc.title||document.title;if(historyMode==='push')history.pushState(null,'',url);else if(historyMode==='replace')history.replaceState(null,'',url);setupAdminContent();return true;};const load=async(url,historyMode,form)=>{const request=++serial;content.setAttribute('aria-busy','true');try{const response=await fetch(url,{credentials:'same-origin',cache:'no-store',redirect:'follow'}),html=await response.text(),doc=new DOMParser().parseFromString(html,'text/html'),finalUrl=new URL(response.url||url,window.location.href);if(!response.ok)throw new Error(`HTTP ${response.status}`);if(request!==serial)return true;if(!own(finalUrl)||!replace(doc,finalUrl,historyMode,form)){window.location.assign(finalUrl);return false;}return true;}catch(error){if(request===serial)window.location.assign(url);return false;}finally{if(request===serial)content.removeAttribute('aria-busy');}};setTab(new URL(window.location.href));document.addEventListener('click',event=>{const link=event.target.closest('a[href]');if(!link||event.defaultPrevented||event.button!==0||event.metaKey||event.ctrlKey||event.shiftKey||event.altKey||link.target&&link.target!=='_self'||link.hasAttribute('download'))return;const url=new URL(link.href,window.location.href);if(!own(url))return;if(link.matches('[data-admin-select]')&&currentTab(url)===currentTab(new URL(window.location.href)))return;event.preventDefault();event.stopPropagation();void load(url,'push',null);},true);document.addEventListener('submit',event=>{const form=event.target;if(!form||event.defaultPrevented||form.matches('[data-monster-action],[data-task-action],[data-account-search-form]')||form.closest('header')||form.target&&form.target!=='_self'||!form.checkValidity())return;const missingActor=form.closest('.npc')&&[...form.querySelectorAll('select.actor-resource-select')].some(select=>!select.value);if(missingActor)return;const url=new URL(form.getAttribute('action')||window.location.href,window.location.href);if(!own(url)||url.pathname.endsWith('/logout'))return;event.preventDefault();event.stopPropagation();if(form.dataset.adminSpaSubmitting==='1')return;form.dataset.adminSpaSubmitting='1';const method=(form.getAttribute('method')||'GET').toUpperCase(),data=new FormData(form);if(method==='GET'){for(const [key,value] of data.entries())url.searchParams.append(key,value);void load(url,'push',form).finally(()=>{form.dataset.adminSpaSubmitting='0';});return;}content.setAttribute('aria-busy','true');fetch(url,{method,body:data,credentials:'same-origin',cache:'no-store',redirect:'follow'}).then(response=>{if(!response.ok)throw new Error(`HTTP ${response.status}`);return response.text().then(html=>({html,url:new URL(response.url||url,window.location.href)}));}).then(({html,url})=>{const doc=new DOMParser().parseFromString(html,'text/html');if(!own(url)||!replace(doc,url,'replace',form))window.location.assign(url);}).catch(()=>{const status=form.querySelector('[data-admin-action-status]')||form.closest('[data-admin-detail]')?.querySelector('[data-admin-action-status]');if(status)status.innerHTML='<div class=\"notice error\">操作提交失败，请稍后重试。</div>';else window.location.assign(url);}).finally(()=>{content.removeAttribute('aria-busy');form.dataset.adminSpaSubmitting='0';});},true);window.addEventListener('popstate',()=>{const url=new URL(window.location.href),shown=nav.querySelector('.admin-spa-tab.on')?.dataset.adminTab;if(own(url)&&shown!==currentTab(url))void load(url,'none',null);});};"
    "document.addEventListener('DOMContentLoaded',()=>{setupMonsterActions();setupTaskActions();setupPartialNavigation();setupAdminSpa();setupAdminLayout();setupAdminHeader();setupAdminContent();});"
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
            bool banned = false;

            if (!vm_mock_service_account_exists(session->accountId))
            {
                memset(session, 0, sizeof(*session));
                return NULL;
            }
            if (!vm_mock_service_account_access_ban_check(
                    session->accountId, &banned, NULL, 0) || banned)
            {
                /* This is the user-center equivalent of invalidating a game
                 * transport session: the next HTTP request cannot continue
                 * to act as a newly banned account. */
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

static u32 vm_mock_user_clear_account_sessions(const char *accountId)
{
    u32 cleared = 0;

    if (accountId == NULL || accountId[0] == 0)
        return 0;
    for (u32 i = 0; i < VM_MOCK_USER_SESSION_MAX; ++i)
    {
        vm_mock_user_session *session = &g_vm_mock_user_sessions[i];

        if (session->active && strcmp(session->accountId, accountId) == 0)
        {
            memset(session, 0, sizeof(*session));
            ++cleared;
        }
    }
    return cleared;
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
                static const char adminScript[] = "admin.js\"";
                static const char adminScriptVersion[] =
                    "admin.js?v=20260813-6\"";
                if (targetPos + replacementLen >= htmlCap)
                    goto fail;
                memcpy(rewritten + targetPos, replacements[i], replacementLen);
                targetPos += replacementLen;
                sourcePos += needleLen;
                /* The backend script is emitted by the executable.  It must
                 * never remain cached across a server restart: an old copy
                 * used form.action, which is shadowed by the hidden
                 * name=\"action\" field in administrative forms. */
                if (i == 2 && sourcePos + sizeof(adminScript) - 1 <= sourceLen &&
                    memcmp(html + sourcePos, adminScript,
                           sizeof(adminScript) - 1) == 0)
                {
                    size_t versionLen = sizeof(adminScriptVersion) - 1;

                    if (targetPos + versionLen >= htmlCap)
                        goto fail;
                    memcpy(rewritten + targetPos, adminScriptVersion,
                           versionLen);
                    targetPos += versionLen;
                    sourcePos += sizeof(adminScript) - 1;
                }
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
        "form{display:grid;gap:11px}input{width:100%;border:1px solid #d0d5dd;border-radius:7px;padding:10px 11px;font-size:15px}.remember{display:flex;align-items:center;gap:8px;color:#475467;cursor:pointer}.remember input{width:auto;padding:0}.local-note{margin:0;color:#667085;font-size:12px}button{border:0;border-radius:7px;padding:10px 12px;background:#175cd3;color:#fff;cursor:pointer}"
        "</style><script src=\"/login.js\" defer></script></head><body><main class=\"card\"><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">请输入管理密码后继续</p>");
    if (error != NULL && error[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"error\">");
        vm_mock_admin_text_append_html(&page, error);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page,
        "<form method=\"post\" action=\"/login\" data-admin-login-form>"
        "<input type=\"password\" name=\"password\" autocomplete=\"current-password\" placeholder=\"管理密码\" data-admin-login-password autofocus required>"
        "<label class=\"remember\"><input type=\"checkbox\" data-admin-login-remember>记住密码（仅本浏览器）</label>"
        "<p class=\"local-note\">密码仅保存在此浏览器的本地存储，不会写入服务器、数据库或 Cookie；取消勾选会清除。</p>"
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
        char configuredPattern[1200];
        static const char *patterns[] = {
            NULL,
            "../web/fs/JHOnlineData/*.sce",
            "web/fs/JHOnlineData/*.sce"
        };
        WIN32_FIND_DATAA found;
        HANDLE search = INVALID_HANDLE_VALUE;

        /* The content page must enumerate the same configured resource root
         * that the server later uses to parse an SCE.  Without this branch an
         * isolated (or deployed) --resource-root works for preview/loading,
         * yet POST validation sees an empty scene catalog and rejects its own
         * form as an invalid scene. */
        memset(configuredPattern, 0, sizeof(configuredPattern));
        if (g_vm_net_mock_resource_dir[0] != 0)
        {
            size_t dirLen = strlen(g_vm_net_mock_resource_dir);
            if (snprintf(configuredPattern, sizeof(configuredPattern),
                         "%s%s*.sce", g_vm_net_mock_resource_dir,
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
    if (!vm_net_mock_content_update_name_is_managed_resource(name) ||
        size == 0 ||
        size > VM_NET_MOCK_UPDATE_PAYLOAD_MAX)
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
    u32 selectableCount = 0;

    if (page == NULL)
        return;
    for (u32 i = 0; i < actorCount; ++i)
    {
        if (!vm_net_mock_dynamic_npc_actor_resource_is_supported(
                actorFiles[i].name))
        {
            continue;
        }
        ++selectableCount;
        if (currentActor != NULL && strcmp(actorFiles[i].name, currentActor) == 0)
        {
            currentFound = true;
            break;
        }
    }
    vm_mock_admin_text_appendf(
        page, "<div class=\"actor-picker-field\"><select class=\"actor-resource-select\" name=\"actor_resource\" required hidden>");
    if (currentActor != NULL && currentActor[0] != 0 && !currentFound)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"\" selected disabled>");
        vm_mock_admin_text_append_html(page, currentActor);
        vm_mock_admin_text_appendf(
            page,
            "%s</option>",
            vm_net_mock_dynamic_npc_actor_resource_is_supported(currentActor)
                ? "（资源不存在，请重新选择）"
                : "（不支持动态 NPC，请改选 n_woman1.actor）");
    }
    if ((currentActor == NULL || currentActor[0] == 0) && selectableCount != 0)
        vm_mock_admin_text_appendf(page, "<option value=\"\" selected disabled>请选择 Actor 资源</option>");
    for (u32 i = 0; i < actorCount; ++i)
    {
        if (!vm_net_mock_dynamic_npc_actor_resource_is_supported(
                actorFiles[i].name))
        {
            continue;
        }
        bool selected = currentFound &&
                        strcmp(actorFiles[i].name, currentActor) == 0;
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "\"%s>", selected ? " selected" : "");
        vm_mock_admin_text_append_html(page, actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    if (selectableCount == 0)
        vm_mock_admin_text_appendf(page, "<option value=\"\" disabled>未找到 Actor 资源</option>");
    vm_mock_admin_text_appendf(
        page,
        "</select><button class=\"actor-picker-trigger\" type=\"button\" data-actor-picker-open aria-haspopup=\"dialog\" aria-controls=\"actor-picker-modal\"><span data-actor-picker-label>请选择 Actor 资源</span><small>搜索与预览</small></button></div>");
}

static void vm_mock_admin_render_actor_picker_modal(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *actorFiles,
    u32 actorCount)
{
    u32 selectableCount = 0;

    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(page, "<select id=\"actor-picker-options\" hidden>");
    for (u32 i = 0; i < actorCount; ++i)
    {
        char actorUtf8[128];

        if (!vm_net_mock_dynamic_npc_actor_resource_is_supported(
                actorFiles[i].name))
        {
            continue;
        }
        memset(actorUtf8, 0, sizeof(actorUtf8));
        vm_mock_admin_resource_name_to_utf8(actorFiles[i].name, actorUtf8,
                                             sizeof(actorUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "\">");
        vm_mock_admin_text_append_html(page,
                                       actorUtf8[0] ? actorUtf8 : actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
        ++selectableCount;
    }
    vm_mock_admin_text_appendf(page,
        "</select><div id=\"actor-picker-modal\" class=\"actor-modal\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"actor-picker-title\" hidden><div class=\"actor-picker-panel\"><div class=\"actor-picker-head\"><div><h3 id=\"actor-picker-title\">选择 Actor 资源</h3><p>仅显示可安全用于动态 NPC 的 Actor；缩略图由服务端资源预览生成。</p></div><button id=\"actor-picker-close\" class=\"actor-picker-close\" type=\"button\" aria-label=\"关闭\">×</button></div><div class=\"actor-picker-tools\"><label><span>搜索资源名称</span><input id=\"actor-picker-search\" type=\"search\" placeholder=\"例如 n_man、woman、guard\" autocomplete=\"off\"></label></div><div class=\"actor-result-bar\"><span id=\"actor-result-count\"></span><span id=\"actor-picker-error\" class=\"actor-picker-error\"></span></div><div id=\"actor-picker-list\" class=\"actor-picker-list\"></div><p id=\"actor-picker-empty\" class=\"actor-picker-empty\" hidden>没有符合条件的 Actor 资源。</p></div></div>");
    if (selectableCount == 0)
    {
        printf("[warn][mock-admin] actor_picker_catalog_empty source=server-resource-root\n");
    }
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
        "药品商人",
        "副本向导（传送／挑战）",
        "装备回收商人（出售装备）",
        "比武擂台（开设／挑战）"
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

/* The client parser accepts a list of action=1 service rows beside its
 * independent action=4 task rows.  This editor intentionally only exposes
 * service kinds that already own a type=2 handler. */
static void vm_mock_admin_render_npc_service_option_fields(
    vm_mock_admin_text *page, const char *runtimeScene,
    const vm_net_mock_scene_npcinfo_seed *seed, bool allowInstance)
{
    static const char *labels[VM_NET_MOCK_NPC_KIND_MAX + 1] = {
        "", "武器商店", "装备修理", "技能导师", "防具商店", "药品商店",
        "副本传送／挑战", "装备回收", "比武擂台"
    };
    vm_net_mock_npc_service_option
        options[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
    u32 optionCount = 0;

    if (page == NULL)
        return;
    memset(options, 0, sizeof(options));
    if (seed != NULL && seed->actorId != 0 && runtimeScene != NULL)
    {
        (void)vm_net_mock_npc_service_options_resolve(
            runtimeScene, seed->actorId, seed->kind,
            seed->serviceOptionName, seed->serviceOptionDescription, options,
            VM_NET_MOCK_NPC_SERVICE_OPTION_MAX, &optionCount, NULL);
    }
    vm_mock_admin_text_appendf(
        page,
        "<fieldset class=\"npc-services\"><legend>可同时提供的对话功能</legend>"
        "<p class=\"hint\">可同时勾选多个已实现功能；任务绑定独立生成“接取／提交任务”选项。每项名称与说明留空时使用默认文案；初始对话中任务和服务合计最多 10 项。</p>");
    for (u32 kind = VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT;
         kind <= VM_NET_MOCK_NPC_KIND_MAX; ++kind)
    {
        const vm_net_mock_npc_service_option *option = NULL;
        char nameUtf8[192];
        char descriptionUtf8[288];

        if (!allowInstance && kind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE)
            continue;
        for (u32 i = 0; i < optionCount; ++i)
        {
            if (options[i].kind == kind)
            {
                option = &options[i];
                break;
            }
        }
        memset(nameUtf8, 0, sizeof(nameUtf8));
        memset(descriptionUtf8, 0, sizeof(descriptionUtf8));
        if (option != NULL)
        {
            vm_net_mock_gbk_label_to_utf8(option->optionName, nameUtf8,
                                          sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(option->optionDescription,
                                          descriptionUtf8,
                                          sizeof(descriptionUtf8));
        }
        vm_mock_admin_text_appendf(
            page,
            "<div class=\"npc-service-row\"><label class=\"npc-service-toggle\"><input type=\"checkbox\" name=\"service_enabled_%u\" value=\"1\"%s><span>%u · %s</span></label><label class=\"field\"><span>选项名称（可留空）</span><input name=\"service_option_name_%u\" maxlength=\"30\" placeholder=\"默认名称\" value=\"",
            kind, option != NULL ? " checked" : "", kind, labels[kind], kind);
        vm_mock_admin_text_append_html(page, nameUtf8);
        vm_mock_admin_text_appendf(
            page,
            "\"></label><label class=\"field\"><span>选项说明（可留空）</span><input name=\"service_option_description_%u\" maxlength=\"45\" placeholder=\"默认说明\" value=\"",
            kind);
        vm_mock_admin_text_append_html(page, descriptionUtf8);
        vm_mock_admin_text_appendf(page, "\"></label></div>");
    }
    vm_mock_admin_text_appendf(page, "</fieldset>");
}

static bool vm_mock_admin_scene_file_to_runtime_key(const char *sceneFile,
                                                    char *runtimeScene,
                                                    size_t runtimeSceneCap);

static void vm_mock_admin_render_instance_scene_select(
    vm_mock_admin_text *page,
    const vm_mock_admin_scene_file *sceneFiles,
    u32 sceneCount,
    const char *currentRuntimeScene)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page, "<select name=\"instance_scene\"><option value=\"\">不传送（仅挑战）</option>");
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char runtimeScene[64];
        char sceneUtf8[192];
        bool selected = false;

        memset(runtimeScene, 0, sizeof(runtimeScene));
        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        if (!vm_mock_admin_scene_file_to_runtime_key(
                sceneFiles[i].name, runtimeScene, sizeof(runtimeScene)))
        {
            continue;
        }
        selected = currentRuntimeScene != NULL && currentRuntimeScene[0] != 0 &&
                   vm_net_mock_scene_names_equal_exact(
                       currentRuntimeScene, runtimeScene);
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name, sceneUtf8,
                                      sizeof(sceneUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, sceneUtf8);
        vm_mock_admin_text_appendf(page, "\"%s>", selected ? " selected" : "");
        vm_mock_admin_text_append_html(page, sceneUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

/* Role recovery accepts only an exact server-owned SCE key.  A datalist keeps
 * the account page compact and searchable while still showing the authoritative
 * resource catalog once, rather than multiplying a full select list per role. */
static void vm_mock_admin_render_role_reset_scene_catalog(
    vm_mock_admin_text *page,
    const vm_mock_admin_scene_file *sceneFiles,
    u32 sceneCount)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page, "<datalist id=\"role-reset-scene-catalog\">");
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char runtimeScene[64];
        char sceneUtf8[192];

        memset(runtimeScene, 0, sizeof(runtimeScene));
        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        if (!vm_mock_admin_scene_file_to_runtime_key(
                sceneFiles[i].name, runtimeScene, sizeof(runtimeScene)))
        {
            continue;
        }
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name, sceneUtf8,
                                      sizeof(sceneUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, sceneUtf8);
        vm_mock_admin_text_appendf(page, "\">");
    }
    vm_mock_admin_text_appendf(page, "</datalist>");
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

    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"instance-fields\"><label class=\"field instance-scene\"><span>副本目标场景</span>");
    vm_mock_admin_render_instance_scene_select(
        page, sceneFiles, sceneCount, seed ? seed->instanceScene : NULL);
    vm_mock_admin_text_appendf(
        page,
        "</label><label class=\"field\"><span>落点 X</span><input type=\"number\" name=\"instance_x\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<label class=\"field\"><span>落点 Y</span><input type=\"number\" name=\"instance_y\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<label class=\"field\"><span>挑战怪物 ID</span><input type=\"number\" name=\"challenge_enemy_id\" min=\"0\" max=\"65535\" value=\"%u\"></label>"
        "<label class=\"field\"><span>最低等级</span><input type=\"number\" name=\"instance_min_level\" min=\"1\" max=\"255\" value=\"%u\"></label>"
        "<p class=\"instance-help\">目标场景留空时只提供挑战；怪物 ID 为 0 时只提供传送。落点 X/Y 都填 0 会自动解析目标 SCE 的安全入口。</p></div>",
        x, y, enemyId, minimumLevel);
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
        const vm_net_mock_task_definition *prerequisite = NULL;
        if (!tasks[i].enabled)
            continue;
        memset(nameUtf8, 0, sizeof(nameUtf8));
        vm_net_mock_gbk_label_to_utf8(tasks[i].name,
                                      nameUtf8, sizeof(nameUtf8));
        if (tasks[i].prerequisiteTaskId != 0)
        {
            for (u32 j = 0; j < taskCount; ++j)
            {
                if (tasks[j].taskId == tasks[i].prerequisiteTaskId)
                {
                    prerequisite = &tasks[j];
                    break;
                }
            }
        }
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%u · ", tasks[i].taskId,
            currentTaskId == tasks[i].taskId ? " selected" : "",
            tasks[i].taskId);
        vm_mock_admin_text_append_html(page, nameUtf8);
        vm_mock_admin_text_appendf(page, "（等级 %u", tasks[i].level);
        if (tasks[i].prerequisiteTaskId != 0)
        {
            vm_mock_admin_text_appendf(page, "，前置：%u",
                                       tasks[i].prerequisiteTaskId);
            if (prerequisite != NULL)
            {
                char prerequisiteUtf8[128];
                memset(prerequisiteUtf8, 0, sizeof(prerequisiteUtf8));
                vm_net_mock_gbk_label_to_utf8(
                    prerequisite->name, prerequisiteUtf8,
                    sizeof(prerequisiteUtf8));
                vm_mock_admin_text_appendf(page, " · ");
                vm_mock_admin_text_append_html(page, prerequisiteUtf8);
            }
        }
        vm_mock_admin_text_appendf(page, "）");
        vm_mock_admin_text_appendf(page, "</option>");
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_npc_task_repeat_policy_select(
    vm_mock_admin_text *page, const vm_net_mock_scene_npcinfo_seed *seed)
{
    u32 policy = VM_NET_MOCK_TASK_REPEAT_NEVER;

    if (page == NULL)
        return;
    if (seed != NULL)
    {
        policy = seed->taskRepeatPolicy;
        if (policy == VM_NET_MOCK_TASK_REPEAT_NEVER && seed->taskRepeatable)
            policy = VM_NET_MOCK_TASK_REPEAT_UNLIMITED;
        if (policy > VM_NET_MOCK_TASK_REPEAT_MONTHLY)
            policy = VM_NET_MOCK_TASK_REPEAT_NEVER;
    }
    vm_mock_admin_text_appendf(
        page,
        "<select name=\"task_repeat_policy\">"
        "<option value=\"0\"%s>完成后不可再次接取</option>"
        "<option value=\"1\"%s>不限次数（完成后可立即再次接取）</option>"
        "<option value=\"2\"%s>每日一次</option>"
        "<option value=\"3\"%s>每周一次</option>"
        "<option value=\"4\"%s>每月一次</option></select>",
        policy == VM_NET_MOCK_TASK_REPEAT_NEVER ? " selected" : "",
        policy == VM_NET_MOCK_TASK_REPEAT_UNLIMITED ? " selected" : "",
        policy == VM_NET_MOCK_TASK_REPEAT_DAILY ? " selected" : "",
        policy == VM_NET_MOCK_TASK_REPEAT_WEEKLY ? " selected" : "",
        policy == VM_NET_MOCK_TASK_REPEAT_MONTHLY ? " selected" : "");
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
    if (runtimeScene == NULL || runtimeSceneCap == 0)
        return false;
    runtimeScene[0] = 0;
    /* This value is sent back to the client as a scene-transition resource
     * key.  LoadSceneRes and the subsequent WT18/7 request preserve it, so a
     * selected SCE filename must remain byte-for-byte intact.  In particular,
     * stripping `.sce` from b_* instance scenes makes the client request a
     * nonexistent bare resource and loop in the update screen. */
    if (sceneFile == NULL || !vm_net_mock_str_ends_with(sceneFile, ".sce") ||
        !vm_net_mock_scene_name_is_safe(sceneFile) ||
        strlen(sceneFile) >= runtimeSceneCap)
    {
        return false;
    }
    snprintf(runtimeScene, runtimeSceneCap, "%s", sceneFile);
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
    VM_MOCK_ADMIN_ACTOR_SVG_MAX = 2 * 1024 * 1024,
    /* Actor resources are executable data contracts for the CBE sprite
     * loader.  These editor bounds deliberately stay below the HTTP body and
     * page-response ceilings, while covering every shipped NPC/effect actor
     * that can be represented safely in the structured editor. */
    VM_MOCK_ADMIN_ACTOR_EDIT_ANIMATION_MAX = 1024,
    VM_MOCK_ADMIN_ACTOR_EDIT_PART_MAX = 4096,
    VM_MOCK_ADMIN_ACTOR_EDIT_FRAME_MAX = 16384,
    VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX = 768 * 1024
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
    /* Exact decoded-SCE location of the u8 target-scene string length.  It is
     * captured while parsing the record so the editor can replace only this
     * field and never reconstruct or guess the rest of a portal record. */
    u32 targetLengthOffset;
    u32 recordOffset;
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
    int32_t animationIndex;
    int32_t partId;
    u32 firstFrame;
    u32 frameCount;
} vm_mock_admin_actor_edit_part;

typedef struct
{
    int32_t rectIndex;
    int32_t offsetX;
    int32_t offsetY;
    int32_t value3;
    int32_t value4;
} vm_mock_admin_actor_edit_frame;

typedef struct
{
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    u32 imageCount;
    vm_mock_admin_actor_rect *rects;
    u32 rectCount;
    vm_mock_admin_actor_edit_part *parts;
    u32 partCount;
    u32 animationCount;
    vm_mock_admin_actor_edit_frame *frames;
    u32 frameCount;
} vm_mock_admin_actor_manifest;

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

/* Locate the already-validated target string within its parsed record.  The
 * portal parsers deliberately describe several native record variants; this
 * narrow capture step keeps the editor tied to the same descriptor that the
 * client will later parse. */
static bool vm_mock_admin_capture_portal_target_location(
    const u8 *data, u32 len, u32 recordOffset, u32 recordEnd,
    vm_mock_admin_scene_portal *portal)
{
    u16 targetField = 0;
    size_t targetLen = 0;

    if (data == NULL || portal == NULL || recordOffset >= recordEnd ||
        recordEnd > len)
    {
        return false;
    }
    targetField = portal->kind == VM_MOCK_ADMIN_PORTAL_NAMED ? 0x17u : 6u;
    targetLen = strlen(portal->targetScene);
    if (targetLen == 0 || targetLen > 0xffu)
        return false;
    for (u32 pos = recordOffset; pos + 5u <= recordEnd; ++pos)
    {
        u32 valueOffset = pos + 4u;
        u32 valueLen = data[valueOffset];

        if (vm_net_mock_read_le16_at(data, pos) != 3u ||
            vm_net_mock_read_le16_at(data, pos + 2u) != targetField ||
            valueLen != targetLen || valueOffset + 1u + valueLen > recordEnd ||
            memcmp(data + valueOffset + 1u, portal->targetScene,
                   targetLen) != 0)
        {
            continue;
        }
        portal->recordOffset = recordOffset;
        portal->targetLengthOffset = valueOffset;
        return true;
    }
    return false;
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

            if (!vm_mock_admin_capture_portal_target_location(
                    data, len, off, end, &portal))
            {
                /* A preview without a precise source field is safe to show,
                 * but not safe to expose as an editable destination. */
                if (end > off + 1)
                    off = end - 1;
                continue;
            }
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

/* The binary Actor format is compact, but its structure is stable: image
 * names, source rectangles, then animation/part/frame tables.  Keep the
 * editor at this semantic layer instead of exposing a raw upload box; this
 * lets the server validate every reference and preserves the fields that the
 * client parser consumes. */
static void vm_mock_admin_actor_manifest_free(vm_mock_admin_actor_manifest *manifest)
{
    if (manifest == NULL)
        return;
    free(manifest->rects);
    free(manifest->parts);
    free(manifest->frames);
    memset(manifest, 0, sizeof(*manifest));
}

static bool vm_mock_admin_actor_manifest_push_part(
    vm_mock_admin_actor_manifest *manifest, int32_t animationIndex,
    int32_t partId)
{
    vm_mock_admin_actor_edit_part *next = NULL;

    if (manifest == NULL ||
        manifest->partCount >= VM_MOCK_ADMIN_ACTOR_EDIT_PART_MAX)
    {
        return false;
    }
    next = (vm_mock_admin_actor_edit_part *)realloc(
        manifest->parts,
        sizeof(*manifest->parts) * (size_t)(manifest->partCount + 1));
    if (next == NULL)
        return false;
    manifest->parts = next;
    memset(&manifest->parts[manifest->partCount], 0,
           sizeof(manifest->parts[manifest->partCount]));
    manifest->parts[manifest->partCount].animationIndex = animationIndex;
    manifest->parts[manifest->partCount].partId = partId;
    ++manifest->partCount;
    return true;
}

static bool vm_mock_admin_actor_manifest_push_frame(
    vm_mock_admin_actor_manifest *manifest,
    const vm_mock_admin_actor_edit_frame *frame)
{
    vm_mock_admin_actor_edit_frame *next = NULL;

    if (manifest == NULL || frame == NULL ||
        manifest->frameCount >= VM_MOCK_ADMIN_ACTOR_EDIT_FRAME_MAX)
    {
        return false;
    }
    next = (vm_mock_admin_actor_edit_frame *)realloc(
        manifest->frames,
        sizeof(*manifest->frames) * (size_t)(manifest->frameCount + 1));
    if (next == NULL)
        return false;
    manifest->frames = next;
    manifest->frames[manifest->frameCount++] = *frame;
    return true;
}

static bool vm_mock_admin_actor_manifest_from_payload(
    const u8 *payload, u32 payloadLen, vm_mock_admin_actor_manifest *manifest)
{
    u32 pos = 0;
    int32_t imageCountSigned = 0;
    int32_t rectCountSigned = 0;
    int32_t animationCountSigned = 0;

    if (manifest == NULL || payload == NULL || payloadLen == 0)
        return false;
    memset(manifest, 0, sizeof(*manifest));
    if (!vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                      &imageCountSigned) ||
        imageCountSigned <= 0 ||
        imageCountSigned > VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX)
    {
        goto failed;
    }
    manifest->imageCount = (u32)imageCountSigned;
    for (u32 i = 0; i < manifest->imageCount; ++i)
    {
        if (!vm_mock_admin_actor_read_string(payload, payloadLen, &pos,
                                             manifest->imageNames[i],
                                             sizeof(manifest->imageNames[i])) ||
            vm_net_mock_scene_name_has_path_separator(
                manifest->imageNames[i]) ||
            !vm_net_mock_str_ends_with(manifest->imageNames[i], ".gif"))
        {
            goto failed;
        }
    }
    if (!vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                      &rectCountSigned) ||
        rectCountSigned <= 0 || rectCountSigned > VM_MOCK_ADMIN_ACTOR_RECT_MAX)
    {
        goto failed;
    }
    manifest->rectCount = (u32)rectCountSigned;
    manifest->rects = (vm_mock_admin_actor_rect *)calloc(
        manifest->rectCount, sizeof(*manifest->rects));
    if (manifest->rects == NULL)
        goto failed;
    for (u32 i = 0; i < manifest->rectCount; ++i)
    {
        if (!vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                          &manifest->rects[i].left) ||
            !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                          &manifest->rects[i].top) ||
            !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                          &manifest->rects[i].right) ||
            !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                          &manifest->rects[i].bottom) ||
            !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                          &manifest->rects[i].imageIndex))
        {
            goto failed;
        }
    }
    if (!vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                      &animationCountSigned) ||
        animationCountSigned <= 0 ||
        animationCountSigned > VM_MOCK_ADMIN_ACTOR_EDIT_ANIMATION_MAX)
    {
        goto failed;
    }
    manifest->animationCount = (u32)animationCountSigned;
    for (int32_t animationIndex = 0;
         animationIndex < animationCountSigned; ++animationIndex)
    {
        int32_t partCountSigned = 0;

        if (!vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                          &partCountSigned) ||
            partCountSigned <= 0 ||
            partCountSigned > VM_MOCK_ADMIN_ACTOR_EDIT_PART_MAX)
        {
            goto failed;
        }
        for (int32_t partIndex = 0; partIndex < partCountSigned; ++partIndex)
        {
            int32_t partId = 0;
            int32_t frameCountSigned = 0;
            vm_mock_admin_actor_edit_part *part = NULL;

            if (!vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                              &partId) ||
                !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                              &frameCountSigned) ||
                frameCountSigned < 0 || frameCountSigned > 65535 ||
                (u32)frameCountSigned > (payloadLen - pos) / 20u ||
                !vm_mock_admin_actor_manifest_push_part(
                    manifest, animationIndex, partId))
            {
                goto failed;
            }
            part = &manifest->parts[manifest->partCount - 1];
            part->firstFrame = manifest->frameCount;
            part->frameCount = (u32)frameCountSigned;
            if (manifest->frameCount + (u32)frameCountSigned >
                VM_MOCK_ADMIN_ACTOR_EDIT_FRAME_MAX)
            {
                goto failed;
            }
            for (int32_t frameIndex = 0;
                 frameIndex < frameCountSigned; ++frameIndex)
            {
                vm_mock_admin_actor_edit_frame frame;
                memset(&frame, 0, sizeof(frame));
                if (!vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                                  &frame.rectIndex) ||
                    !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                                  &frame.offsetX) ||
                    !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                                  &frame.offsetY) ||
                    !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                                  &frame.value3) ||
                    !vm_mock_admin_actor_read_s32(payload, payloadLen, &pos,
                                                  &frame.value4) ||
                    frame.rectIndex < 0 ||
                    (u32)frame.rectIndex >= manifest->rectCount ||
                    !vm_mock_admin_actor_manifest_push_frame(manifest, &frame))
                {
                    goto failed;
                }
            }
        }
    }
    if (pos != payloadLen || manifest->frameCount == 0)
        goto failed;
    return true;

failed:
    vm_mock_admin_actor_manifest_free(manifest);
    return false;
}

static bool vm_mock_admin_actor_manifest_from_resource(
    const char *actorResource, vm_mock_admin_actor_manifest *manifest)
{
    u8 *payload = NULL;
    u32 payloadLen = 0;
    u8 type = 0;
    bool ok = false;

    if (manifest == NULL || actorResource == NULL ||
        vm_net_mock_scene_name_has_path_separator(actorResource) ||
        !vm_net_mock_str_ends_with(actorResource, ".actor") ||
        !vm_mock_admin_load_data_payload(actorResource, ".actor", &payload,
                                         &payloadLen, &type) || type != 2)
    {
        free(payload);
        return false;
    }
    ok = vm_mock_admin_actor_manifest_from_payload(payload, payloadLen,
                                                   manifest);
    free(payload);
    return ok;
}

static char *vm_mock_admin_actor_trim(char *text)
{
    char *end = NULL;
    if (text == NULL)
        return NULL;
    while (*text != 0 && isspace((unsigned char)*text))
        ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = 0;
    return text;
}

static bool vm_mock_admin_actor_parse_i32(const char *text, int32_t *valueOut)
{
    char *end = NULL;
    long long value = 0;

    if (text == NULL || text[0] == 0 || valueOut == NULL)
        return false;
    value = strtoll(text, &end, 10);
    if (end == text || *end != 0 || value < INT32_MIN || value > INT32_MAX)
        return false;
    *valueOut = (int32_t)value;
    return true;
}

static bool vm_mock_admin_actor_parse_csv_i32(char *line, u32 valueCount,
                                               int32_t *values)
{
    char *cursor = line;

    if (line == NULL || values == NULL || valueCount == 0)
        return false;
    for (u32 i = 0; i < valueCount; ++i)
    {
        char *comma = strchr(cursor, ',');
        char *value = NULL;
        if ((i + 1 < valueCount && comma == NULL) ||
            (i + 1 == valueCount && comma != NULL))
        {
            return false;
        }
        if (comma != NULL)
            *comma = 0;
        value = vm_mock_admin_actor_trim(cursor);
        if (!vm_mock_admin_actor_parse_i32(value, &values[i]))
            return false;
        cursor = comma ? comma + 1 : cursor + strlen(cursor);
    }
    return true;
}

static char *vm_mock_admin_actor_next_line(char **cursor)
{
    char *line = NULL;
    char *end = NULL;

    if (cursor == NULL || *cursor == NULL || **cursor == 0)
        return NULL;
    line = *cursor;
    end = line;
    while (*end != 0 && *end != '\r' && *end != '\n')
        ++end;
    if (*end != 0)
    {
        char separator = *end;
        *end++ = 0;
        if ((*end == '\r' || *end == '\n') && *end != separator)
            ++end;
        *cursor = end;
    }
    else
    {
        *cursor = end;
    }
    return vm_mock_admin_actor_trim(line);
}

static bool vm_mock_admin_actor_parse_image_lines(
    char *text, vm_mock_admin_actor_manifest *manifest)
{
    char *cursor = text;
    char *line = NULL;

    if (text == NULL || manifest == NULL)
        return false;
    while ((line = vm_mock_admin_actor_next_line(&cursor)) != NULL)
    {
        if (line[0] == 0)
            continue;
        if (manifest->imageCount >= VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX ||
            strlen(line) >= sizeof(manifest->imageNames[0]) ||
            vm_net_mock_scene_name_has_path_separator(line) ||
            !vm_net_mock_str_ends_with(line, ".gif"))
        {
            return false;
        }
        snprintf(manifest->imageNames[manifest->imageCount],
                 sizeof(manifest->imageNames[0]), "%s", line);
        ++manifest->imageCount;
    }
    return manifest->imageCount != 0;
}

static bool vm_mock_admin_actor_parse_rect_lines(
    char *text, vm_mock_admin_actor_manifest *manifest)
{
    char *cursor = text;
    char *line = NULL;

    if (text == NULL || manifest == NULL)
        return false;
    while ((line = vm_mock_admin_actor_next_line(&cursor)) != NULL)
    {
        int32_t values[5];
        vm_mock_admin_actor_rect *next = NULL;
        if (line[0] == 0)
            continue;
        if (manifest->rectCount >= VM_MOCK_ADMIN_ACTOR_RECT_MAX ||
            !vm_mock_admin_actor_parse_csv_i32(line, 5, values))
        {
            return false;
        }
        next = (vm_mock_admin_actor_rect *)realloc(
            manifest->rects,
            sizeof(*manifest->rects) * (size_t)(manifest->rectCount + 1));
        if (next == NULL)
            return false;
        manifest->rects = next;
        manifest->rects[manifest->rectCount].left = values[0];
        manifest->rects[manifest->rectCount].top = values[1];
        manifest->rects[manifest->rectCount].right = values[2];
        manifest->rects[manifest->rectCount].bottom = values[3];
        manifest->rects[manifest->rectCount].imageIndex = values[4];
        ++manifest->rectCount;
    }
    return manifest->rectCount != 0;
}

static bool vm_mock_admin_actor_parse_part_lines(
    char *text, vm_mock_admin_actor_manifest *manifest)
{
    char *cursor = text;
    char *line = NULL;
    int32_t previousAnimation = -1;

    if (text == NULL || manifest == NULL)
        return false;
    while ((line = vm_mock_admin_actor_next_line(&cursor)) != NULL)
    {
        int32_t values[2];
        if (line[0] == 0)
            continue;
        if (!vm_mock_admin_actor_parse_csv_i32(line, 2, values) ||
            values[0] < 0 || values[0] < previousAnimation ||
            values[0] > previousAnimation + 1 ||
            (manifest->partCount == 0 && values[0] != 0))
        {
            return false;
        }
        for (u32 i = 0; i < manifest->partCount; ++i)
        {
            if (manifest->parts[i].animationIndex == values[0] &&
                manifest->parts[i].partId == values[1])
            {
                return false;
            }
        }
        if (!vm_mock_admin_actor_manifest_push_part(manifest, values[0],
                                                    values[1]))
        {
            return false;
        }
        previousAnimation = values[0];
    }
    if (manifest->partCount == 0 || previousAnimation < 0)
        return false;
    manifest->animationCount = (u32)previousAnimation + 1;
    return true;
}

static bool vm_mock_admin_actor_parse_frame_lines(
    char *text, vm_mock_admin_actor_manifest *manifest)
{
    char *cursor = text;
    char *line = NULL;
    u32 partCursor = 0;

    if (text == NULL || manifest == NULL)
        return false;
    while ((line = vm_mock_admin_actor_next_line(&cursor)) != NULL)
    {
        int32_t values[7];
        vm_mock_admin_actor_edit_frame frame;
        u32 matchedPart = manifest->partCount;

        if (line[0] == 0)
            continue;
        if (!vm_mock_admin_actor_parse_csv_i32(line, 7, values))
            return false;
        for (u32 i = partCursor; i < manifest->partCount; ++i)
        {
            if (manifest->parts[i].animationIndex == values[0] &&
                manifest->parts[i].partId == values[1])
            {
                matchedPart = i;
                break;
            }
        }
        if (matchedPart == manifest->partCount || values[2] < 0 ||
            (u32)values[2] >= manifest->rectCount)
        {
            return false;
        }
        if (manifest->parts[matchedPart].frameCount == 0)
            manifest->parts[matchedPart].firstFrame = manifest->frameCount;
        ++manifest->parts[matchedPart].frameCount;
        memset(&frame, 0, sizeof(frame));
        frame.rectIndex = values[2];
        frame.offsetX = values[3];
        frame.offsetY = values[4];
        frame.value3 = values[5];
        frame.value4 = values[6];
        if (!vm_mock_admin_actor_manifest_push_frame(manifest, &frame))
            return false;
        partCursor = matchedPart;
    }
    return manifest->frameCount != 0;
}

static bool vm_mock_admin_actor_manifest_from_form(
    const char *body, vm_mock_admin_actor_manifest *manifest)
{
    char *images = NULL;
    char *rectangles = NULL;
    char *parts = NULL;
    char *frames = NULL;
    bool ok = false;

    if (body == NULL || manifest == NULL)
        return false;
    memset(manifest, 0, sizeof(*manifest));
    images = (char *)calloc(VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1, 1);
    rectangles = (char *)calloc(VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1, 1);
    parts = (char *)calloc(VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1, 1);
    frames = (char *)calloc(VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1, 1);
    if (images == NULL || rectangles == NULL || parts == NULL || frames == NULL ||
        !vm_mock_admin_form_value(body, "images", images,
                                  VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1) ||
        !vm_mock_admin_form_value(body, "rectangles", rectangles,
                                  VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1) ||
        !vm_mock_admin_form_value(body, "parts", parts,
                                  VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1) ||
        !vm_mock_admin_form_value(body, "frames", frames,
                                  VM_MOCK_ADMIN_ACTOR_EDIT_TEXT_MAX + 1))
    {
        goto done;
    }
    ok = vm_mock_admin_actor_parse_image_lines(images, manifest) &&
         vm_mock_admin_actor_parse_rect_lines(rectangles, manifest) &&
         vm_mock_admin_actor_parse_part_lines(parts, manifest) &&
         vm_mock_admin_actor_parse_frame_lines(frames, manifest);
done:
    free(images);
    free(rectangles);
    free(parts);
    free(frames);
    if (!ok)
        vm_mock_admin_actor_manifest_free(manifest);
    return ok;
}

static bool vm_mock_admin_actor_manifest_encode_raw(
    const vm_mock_admin_actor_manifest *manifest, u8 **rawOut, u32 *rawLenOut)
{
    u8 *payload = NULL;
    u8 *encoded = NULL;
    u8 *raw = NULL;
    u32 payloadLen = 0;
    u32 encodedCap = 0;
    u32 encodedLen = 0;
    u32 pos = 0;
    bool ok = false;

    if (rawOut)
        *rawOut = NULL;
    if (rawLenOut)
        *rawLenOut = 0;
    if (manifest == NULL || rawOut == NULL || rawLenOut == NULL ||
        manifest->imageCount == 0 || manifest->rectCount == 0 ||
        manifest->partCount == 0 || manifest->animationCount == 0 ||
        manifest->frameCount == 0)
    {
        return false;
    }
    payloadLen = 4 + 4 + manifest->rectCount * 20u + 4;
    for (u32 i = 0; i < manifest->imageCount; ++i)
    {
        size_t imageLen = strlen(manifest->imageNames[i]);
        if (imageLen == 0 || imageLen > 0xffu ||
            payloadLen > VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX - 1u - imageLen)
        {
            return false;
        }
        payloadLen += 1u + (u32)imageLen;
    }
    for (u32 i = 0; i < manifest->partCount; ++i)
    {
        if (manifest->parts[i].frameCount > 65535 ||
            manifest->parts[i].firstFrame > manifest->frameCount ||
            manifest->parts[i].frameCount >
                manifest->frameCount - manifest->parts[i].firstFrame ||
            payloadLen > VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX - 8u ||
            manifest->parts[i].frameCount >
                (VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX - payloadLen - 8u) / 20u)
        {
            return false;
        }
        payloadLen += 8u + manifest->parts[i].frameCount * 20u;
    }
    if (payloadLen == 0 || payloadLen > VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX ||
        payloadLen > (UINT32_MAX - 9u) / 2u)
    {
        return false;
    }
    payload = (u8 *)calloc(payloadLen, 1);
    encodedCap = payloadLen + (payloadLen + 126u) / 127u + 9u;
    encoded = (u8 *)malloc(encodedCap);
    if (payload == NULL || encoded == NULL)
        goto done;
    vm_mock_admin_preview_write_le32(payload, pos, manifest->imageCount);
    pos += 4;
    for (u32 i = 0; i < manifest->imageCount; ++i)
    {
        u32 imageLen = (u32)strlen(manifest->imageNames[i]);
        payload[pos++] = (u8)imageLen;
        memcpy(payload + pos, manifest->imageNames[i], imageLen);
        pos += imageLen;
    }
    vm_mock_admin_preview_write_le32(payload, pos, manifest->rectCount);
    pos += 4;
    for (u32 i = 0; i < manifest->rectCount; ++i)
    {
        vm_mock_admin_preview_write_le32(payload, pos, (u32)manifest->rects[i].left);
        vm_mock_admin_preview_write_le32(payload, pos + 4, (u32)manifest->rects[i].top);
        vm_mock_admin_preview_write_le32(payload, pos + 8, (u32)manifest->rects[i].right);
        vm_mock_admin_preview_write_le32(payload, pos + 12, (u32)manifest->rects[i].bottom);
        vm_mock_admin_preview_write_le32(payload, pos + 16, (u32)manifest->rects[i].imageIndex);
        pos += 20;
    }
    vm_mock_admin_preview_write_le32(payload, pos, manifest->animationCount);
    pos += 4;
    for (u32 animationIndex = 0; animationIndex < manifest->animationCount;
         ++animationIndex)
    {
        u32 firstPart = manifest->partCount;
        u32 partCount = 0;
        for (u32 i = 0; i < manifest->partCount; ++i)
        {
            if ((u32)manifest->parts[i].animationIndex == animationIndex)
            {
                if (firstPart == manifest->partCount)
                    firstPart = i;
                ++partCount;
            }
        }
        if (partCount == 0 || firstPart == manifest->partCount)
            goto done;
        vm_mock_admin_preview_write_le32(payload, pos, partCount);
        pos += 4;
        for (u32 i = firstPart; i < firstPart + partCount; ++i)
        {
            const vm_mock_admin_actor_edit_part *part = &manifest->parts[i];
            if ((u32)part->animationIndex != animationIndex)
                goto done;
            vm_mock_admin_preview_write_le32(payload, pos, (u32)part->partId);
            vm_mock_admin_preview_write_le32(payload, pos + 4, part->frameCount);
            pos += 8;
            for (u32 frameIndex = 0; frameIndex < part->frameCount;
                 ++frameIndex)
            {
                const vm_mock_admin_actor_edit_frame *frame =
                    &manifest->frames[part->firstFrame + frameIndex];
                if (frame->rectIndex < 0 ||
                    (u32)frame->rectIndex >= manifest->rectCount)
                {
                    goto done;
                }
                vm_mock_admin_preview_write_le32(payload, pos, (u32)frame->rectIndex);
                vm_mock_admin_preview_write_le32(payload, pos + 4, (u32)frame->offsetX);
                vm_mock_admin_preview_write_le32(payload, pos + 8, (u32)frame->offsetY);
                vm_mock_admin_preview_write_le32(payload, pos + 12, (u32)frame->value3);
                vm_mock_admin_preview_write_le32(payload, pos + 16, (u32)frame->value4);
                pos += 20;
            }
        }
    }
    if (pos != payloadLen ||
        !vm_net_mock_scene_battle_monster_lzss_literal_encode(
            payload, payloadLen, encoded, encodedCap, &encodedLen) ||
        encodedLen == 0 || encodedLen > UINT32_MAX - 4u)
    {
        goto done;
    }
    raw = (u8 *)malloc(encodedLen + 4u);
    if (raw == NULL)
        goto done;
    vm_mock_admin_preview_write_le32(raw, 0, encodedLen);
    memcpy(raw + 4, encoded, encodedLen);
    *rawOut = raw;
    *rawLenOut = encodedLen + 4u;
    raw = NULL;
    ok = true;
done:
    free(payload);
    free(encoded);
    free(raw);
    return ok;
}

static bool vm_mock_admin_actor_name_is_writable(const char *name)
{
    size_t len = name ? strlen(name) : 0;
    if (len < 7 || len >= 64 || !vm_net_mock_str_ends_with(name, ".actor") ||
        vm_net_mock_scene_name_has_path_separator(name) || strstr(name, "..") != NULL)
    {
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char ch = (unsigned char)name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.'))
        {
            return false;
        }
    }
    return true;
}

static bool vm_mock_admin_read_raw_resource_file(const char *path,
                                                  u8 **rawOut, u32 *rawLenOut)
{
    FILE *fp = NULL;
    long sizeLong = 0;
    u8 *raw = NULL;

    if (rawOut)
        *rawOut = NULL;
    if (rawLenOut)
        *rawLenOut = 0;
    if (path == NULL || rawOut == NULL || rawLenOut == NULL ||
        (fp = fopen(path, "rb")) == NULL || fseek(fp, 0, SEEK_END) != 0 ||
        (sizeLong = ftell(fp)) <= 0 ||
        sizeLong > VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX || fseek(fp, 0, SEEK_SET) != 0)
    {
        if (fp != NULL)
            fclose(fp);
        return false;
    }
    raw = (u8 *)malloc((size_t)sizeLong);
    if (raw == NULL || fread(raw, 1, (size_t)sizeLong, fp) != (size_t)sizeLong)
    {
        fclose(fp);
        free(raw);
        return false;
    }
    fclose(fp);
    *rawOut = raw;
    *rawLenOut = (u32)sizeLong;
    return true;
}

static bool vm_mock_admin_write_actor_resource_atomic(const char *path,
                                                       const u8 *raw,
                                                       u32 rawLen)
{
    char tempPath[1240];
    FILE *fp = NULL;
    bool writeOk = false;

    if (path == NULL || path[0] == 0 || raw == NULL || rawLen < 5 ||
        snprintf(tempPath, sizeof(tempPath), "%s.actor-edit.tmp", path) >=
            (int)sizeof(tempPath))
    {
        return false;
    }
    fp = fopen(tempPath, "wb");
    if (fp == NULL)
        return false;
    writeOk = fwrite(raw, 1, rawLen, fp) == rawLen;
    if (fflush(fp) != 0)
        writeOk = false;
    if (fclose(fp) != 0)
        writeOk = false;
    fp = NULL;
    if (!writeOk)
    {
        remove(tempPath);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(tempPath, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
    if (rename(tempPath, path) != 0)
#endif
    {
        remove(tempPath);
        return false;
    }
    return true;
}

/* Resource edits always replace the authoritative file atomically.  The
 * actor writer predates the generic content editor; retain it for existing
 * Actor routes and use this neutral sibling for SCE/GIF/DSH edits so neither
 * temporary filename nor logging claims the wrong resource type. */
static bool vm_mock_admin_write_resource_atomic(const char *path,
                                                const u8 *raw,
                                                u32 rawLen)
{
    char tempPath[1240];
    FILE *fp = NULL;
    bool writeOk = false;

    if (path == NULL || path[0] == 0 || raw == NULL || rawLen < 5 ||
        snprintf(tempPath, sizeof(tempPath), "%s.content-edit.tmp", path) >=
            (int)sizeof(tempPath))
    {
        return false;
    }
    fp = fopen(tempPath, "wb");
    if (fp == NULL)
        return false;
    writeOk = fwrite(raw, 1, rawLen, fp) == rawLen;
    if (fflush(fp) != 0)
        writeOk = false;
    if (fclose(fp) != 0)
        writeOk = false;
    if (!writeOk)
    {
        remove(tempPath);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(tempPath, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
    if (rename(tempPath, path) != 0)
#endif
    {
        remove(tempPath);
        return false;
    }
    return true;
}

/* Actor/GIF payloads remain server resources.  Missing client files are
 * requested through the normal WT 18/7 path, so no per-resource publication
 * catalogue or version row is needed.  This boundary validates the complete
 * dependency set before an NPC may reference it. */
static bool vm_net_mock_ensure_actor_resource_available(
    const char *actorResource, const char **errorOut)
{
    char imageNames[VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX][64];
    u32 imageCount = 0;

    if (errorOut)
        *errorOut = NULL;
    memset(imageNames, 0, sizeof(imageNames));
    if (!vm_mock_admin_actor_resource_inspect(actorResource, imageNames,
                                              &imageCount))
    {
        if (errorOut)
            *errorOut = "Actor 资源无效或引用图片不完整";
        return false;
    }
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

static void vm_mock_admin_render_item_picker_field(
    vm_mock_admin_text *page, const char *pickerId, const char *fieldName,
    const char *label, u32 itemId, bool required);
static void vm_mock_admin_render_item_picker_modal(
    vm_mock_admin_text *page, bool includeEquipmentFilters);
static void vm_mock_admin_render_npc_inventory(
    vm_mock_admin_text *page, const char *sceneUtf8, const char *runtimeScene,
    u32 actorId, u16 serviceKind, const char *pickerPrefix);
static void vm_mock_admin_render_npc_inventories(
    vm_mock_admin_text *page, const char *sceneUtf8, const char *runtimeScene,
    const vm_net_mock_scene_npcinfo_seed *seed, const char *pickerPrefix);
static void vm_mock_admin_render_npc_stock_picker_modal(vm_mock_admin_text *page);
static void vm_mock_admin_render_scene_battle_monster_page(
    char *response, size_t responseCap, const char *query);

static u32 vm_mock_admin_collect_content_files_by_suffix(
    vm_mock_admin_scene_file *files, u32 fileCap, const char *suffix)
{
    vm_mock_admin_scene_file *all = NULL;
    u32 allCount = 0;
    u32 count = 0;

    if (files == NULL || fileCap == 0 || suffix == NULL || suffix[0] == 0)
        return 0;
    memset(files, 0, sizeof(*files) * fileCap);
    all = (vm_mock_admin_scene_file *)calloc(
        VM_MOCK_ADMIN_UPDATE_FILE_MAX, sizeof(*all));
    if (all == NULL)
        return 0;
    allCount = vm_mock_admin_collect_update_files(
        all, VM_MOCK_ADMIN_UPDATE_FILE_MAX);
    for (u32 i = 0; i < allCount && count < fileCap; ++i)
    {
        if (!vm_net_mock_str_ends_with(all[i].name, suffix))
            continue;
        files[count++] = all[i];
    }
    free(all);
    if (count > 1)
        qsort(files, count, sizeof(files[0]), vm_mock_admin_scene_file_compare);
    return count;
}

static bool vm_mock_admin_content_resource_from_query(
    const char *query, const char *field, const char *suffix, char *utf8Out,
    size_t utf8OutCap, char *gameOut, size_t gameOutCap)
{
    if (utf8Out == NULL || utf8OutCap == 0 || gameOut == NULL ||
        gameOutCap == 0)
    {
        return false;
    }
    utf8Out[0] = 0;
    gameOut[0] = 0;
    if (!vm_mock_admin_form_value(query, field, utf8Out, utf8OutCap) ||
        utf8Out[0] == 0 ||
        !vm_mock_admin_utf8_to_gbk_text(utf8Out, gameOut, gameOutCap, false) ||
        !vm_net_mock_str_ends_with(gameOut, suffix) ||
        vm_net_mock_scene_name_has_path_separator(gameOut))
    {
        utf8Out[0] = 0;
        gameOut[0] = 0;
        return false;
    }
    return true;
}

static bool vm_mock_admin_build_gif_preview_bmp(const char *resource,
                                                u8 **bmpOut, u32 *bmpLenOut,
                                                u16 *widthOut, u16 *heightOut)
{
    u8 *payload = NULL;
    u32 payloadLen = 0;
    u8 type = 0;
    GifOutput image;
    int mallocSize = 0;
    u8 *bmp = NULL;
    u32 rowBytes = 0;
    u32 pixelBytes = 0;
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
    if (resource == NULL || bmpOut == NULL || bmpLenOut == NULL ||
        vm_net_mock_scene_name_has_path_separator(resource) ||
        !vm_net_mock_str_ends_with(resource, ".gif") ||
        !vm_mock_admin_load_data_payload(resource, ".gif", &payload,
                                         &payloadLen, &type) ||
        type != 1 ||
        !gifDecodeExt(payload, &image, 1, &mallocSize) || image.pixels == NULL ||
        image.width == 0 || image.height == 0)
    {
        goto done;
    }
    rowBytes = ((u32)image.width * 3u + 3u) & ~3u;
    if (rowBytes == 0 || image.height > (UINT32_MAX - 54u) / rowBytes)
        goto done;
    pixelBytes = rowBytes * image.height;
    bmpLen = 54u + pixelBytes;
    bmp = (u8 *)calloc(bmpLen, 1);
    if (bmp == NULL)
        goto done;
    memcpy(bmp, "BM", 2);
    vm_mock_admin_preview_write_le32(bmp, 2, bmpLen);
    vm_mock_admin_preview_write_le32(bmp, 10, 54);
    vm_mock_admin_preview_write_le32(bmp, 14, 40);
    vm_mock_admin_preview_write_le32(bmp, 18, image.width);
    vm_mock_admin_preview_write_le32(bmp, 22, image.height);
    vm_mock_admin_preview_write_le16(bmp, 26, 1);
    vm_mock_admin_preview_write_le16(bmp, 28, 24);
    vm_mock_admin_preview_write_le32(bmp, 34, pixelBytes);
    for (u32 y = 0; y < image.height; ++y)
    {
        u8 *row = bmp + 54u + (u32)(image.height - 1u - y) * rowBytes;
        u32 pitch = (u32)image.width + ((4u - ((u32)image.width & 3u)) & 3u);
        for (u32 x = 0; x < image.width; ++x)
        {
            u16 color = image.pixels[y * pitch + x];
            row[x * 3u] = (u8)((color & 0x1fu) * 255u / 31u);
            row[x * 3u + 1u] = (u8)(((color >> 5) & 0x3fu) * 255u / 63u);
            row[x * 3u + 2u] = (u8)(((color >> 11) & 0x1fu) * 255u / 31u);
        }
    }
    *bmpOut = bmp;
    *bmpLenOut = bmpLen;
    if (widthOut)
        *widthOut = image.width;
    if (heightOut)
        *heightOut = image.height;
    bmp = NULL;
    ok = true;

done:
    free(payload);
    if (image.owned && image.pixels)
        free_mem(image.pixels);
    free(bmp);
    return ok;
}

enum
{
    VM_MOCK_ADMIN_DSH_COLUMN_MAX = 64,
    VM_MOCK_ADMIN_DSH_VALUE_MAX = 255
};

typedef struct
{
    u32 columnCount;
    u32 rowCount;
    u32 dataOffset;
    u32 columnOffsets[VM_MOCK_ADMIN_DSH_COLUMN_MAX];
    u8 columnLengths[VM_MOCK_ADMIN_DSH_COLUMN_MAX];
} vm_mock_admin_dsh_table;

static bool vm_mock_admin_dsh_table_parse(const u8 *raw, u32 rawLen,
                                          vm_mock_admin_dsh_table *table)
{
    u32 declaredLen = 0;
    u32 headerBytes = 0;
    u32 headerEnd = 0;
    u32 pos = 0;

    if (table != NULL)
        memset(table, 0, sizeof(*table));
    if (raw == NULL || table == NULL || rawLen < 20u ||
        (declaredLen = vm_mock_service_read_le32(raw)) != rawLen - 4u ||
        (table->columnCount = vm_mock_service_read_le32(raw + 4u)) == 0 ||
        table->columnCount > VM_MOCK_ADMIN_DSH_COLUMN_MAX ||
        (table->rowCount = vm_mock_service_read_le32(raw + 8u)) > 100000u ||
        (headerBytes = vm_mock_service_read_le32(raw + 12u)) <
            table->columnCount ||
        headerBytes > rawLen - 16u)
    {
        return false;
    }
    headerEnd = 16u + headerBytes;
    pos = 16u;
    for (u32 column = 0; column < table->columnCount; ++column)
    {
        u32 length = 0;
        if (pos >= headerEnd || pos >= rawLen ||
            (length = raw[pos++]) > headerEnd - pos)
        {
            return false;
        }
        table->columnOffsets[column] = pos;
        table->columnLengths[column] = (u8)length;
        pos += length;
    }
    if (pos != headerEnd)
        return false;
    table->dataOffset = headerEnd;
    for (u32 row = 0; row < table->rowCount; ++row)
    {
        u32 rowLen = 0;
        u32 rowEnd = 0;
        if (pos > rawLen - 4u ||
            (rowLen = vm_mock_service_read_le32(raw + pos)) == 0 ||
            rowLen > rawLen - pos - 4u)
        {
            return false;
        }
        pos += 4u;
        rowEnd = pos + rowLen;
        for (u32 column = 0; column < table->columnCount; ++column)
        {
            u32 valueLen = 0;
            if (pos >= rowEnd || (valueLen = raw[pos++]) > rowEnd - pos)
                return false;
            pos += valueLen;
        }
        if (pos != rowEnd)
            return false;
    }
    return pos == rawLen;
}

static bool vm_mock_admin_dsh_row_at(const u8 *raw, u32 rawLen,
                                     const vm_mock_admin_dsh_table *table,
                                     u32 wantedRow, u32 *rowOffsetOut,
                                     u32 *rowLenOut)
{
    u32 pos = 0;

    if (rowOffsetOut)
        *rowOffsetOut = 0;
    if (rowLenOut)
        *rowLenOut = 0;
    if (raw == NULL || table == NULL || wantedRow >= table->rowCount ||
        table->dataOffset > rawLen)
    {
        return false;
    }
    pos = table->dataOffset;
    for (u32 row = 0; row <= wantedRow; ++row)
    {
        u32 rowLen = 0;
        if (pos > rawLen - 4u ||
            (rowLen = vm_mock_service_read_le32(raw + pos)) == 0 ||
            rowLen > rawLen - pos - 4u)
        {
            return false;
        }
        if (row == wantedRow)
        {
            if (rowOffsetOut)
                *rowOffsetOut = pos + 4u;
            if (rowLenOut)
                *rowLenOut = rowLen;
            return true;
        }
        pos += 4u + rowLen;
    }
    return false;
}

static bool vm_mock_admin_dsh_row_value_at(const u8 *raw, u32 rowOffset,
                                           u32 rowLen, u32 columnCount,
                                           u32 wantedColumn,
                                           const u8 **valueOut,
                                           u8 *valueLenOut)
{
    u32 pos = rowOffset;
    u32 rowEnd = rowOffset + rowLen;

    if (valueOut)
        *valueOut = NULL;
    if (valueLenOut)
        *valueLenOut = 0;
    if (raw == NULL || wantedColumn >= columnCount || rowEnd < rowOffset)
        return false;
    for (u32 column = 0; column < columnCount; ++column)
    {
        u32 valueLen = 0;
        if (pos >= rowEnd || (valueLen = raw[pos++]) > rowEnd - pos)
            return false;
        if (column == wantedColumn)
        {
            if (valueOut)
                *valueOut = raw + pos;
            if (valueLenOut)
                *valueLenOut = (u8)valueLen;
            return true;
        }
        pos += valueLen;
    }
    return false;
}

static u32 vm_mock_admin_dsh_row_fingerprint(const u8 *row, u32 rowLen)
{
    u32 hash = 2166136261u;

    if (row == NULL)
        return 0;
    for (u32 i = 0; i < rowLen; ++i)
    {
        hash ^= row[i];
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

static void vm_mock_admin_dsh_bytes_to_utf8(const u8 *bytes, u8 byteLen,
                                             char *utf8Out,
                                             size_t utf8OutCap)
{
    char gbk[VM_MOCK_ADMIN_DSH_VALUE_MAX + 1u];

    if (utf8Out == NULL || utf8OutCap == 0)
        return;
    utf8Out[0] = 0;
    if (bytes == NULL || byteLen == 0)
        return;
    memcpy(gbk, bytes, byteLen);
    gbk[byteLen] = 0;
    vm_net_mock_gbk_label_to_utf8(gbk, utf8Out, utf8OutCap);
}

static bool vm_mock_admin_dsh_raw_is_valid(const u8 *raw, u32 rawLen)
{
    vm_mock_admin_dsh_table table;

    return vm_mock_admin_dsh_table_parse(raw, rawLen, &table);
}

static void vm_mock_admin_render_dsh_resource_editor(
    vm_mock_admin_text *page, const char *resource, const char *resourceUtf8,
    const char *query)
{
    char path[1200];
    char rowText[32];
    char encodedResource[512];
    u8 *raw = NULL;
    u32 rawLen = 0;
    u32 selectedRow = 0;
    u32 rowOffset = 0;
    u32 rowLen = 0;
    vm_mock_admin_dsh_table table;

    memset(path, 0, sizeof(path));
    memset(rowText, 0, sizeof(rowText));
    memset(encodedResource, 0, sizeof(encodedResource));
    memset(&table, 0, sizeof(table));
    if (page == NULL || resource == NULL || resourceUtf8 == NULL ||
        !vm_net_mock_update_resource_path(resource, path, sizeof(path)) ||
        !vm_mock_admin_read_raw_resource_file(path, &raw, &rawLen) ||
        !vm_mock_admin_dsh_table_parse(raw, rawLen, &table))
    {
        vm_mock_admin_text_appendf(
            page,
            "<div class=\"notice error\">DSH 表结构校验失败，已拒绝编辑。请使用已校验的 DSH 文件导入替换。</div>");
        free(raw);
        return;
    }
    if (vm_mock_admin_form_value(query, "dsh_row", rowText, sizeof(rowText)))
        (void)vm_net_mock_parse_u32_strict(rowText, &selectedRow);
    if (selectedRow >= table.rowCount)
        selectedRow = table.rowCount == 0 ? 0 : table.rowCount - 1u;
    vm_mock_admin_url_encode(resourceUtf8, encodedResource,
                             sizeof(encodedResource));
    vm_mock_admin_text_appendf(
        page,
        "<style>.dsh-editor{display:grid;gap:14px}.dsh-summary{display:flex;gap:8px;flex-wrap:wrap}.dsh-summary span{padding:4px 8px;border-radius:999px;background:#eef4ff;color:#175cd3;font-size:12px}.dsh-row-tools{display:flex;align-items:end;gap:8px;flex-wrap:wrap;padding:11px;border:1px solid #dbe5f3;border-radius:8px;background:#f8fbff}.dsh-row-tools label{display:grid;gap:4px;min-width:140px}.dsh-row-tools input{width:100%%;padding:7px 8px;border:1px solid #d0d5dd;border-radius:6px}.dsh-link{padding:7px 10px;border:1px solid #bfd1eb;border-radius:6px;background:#fff;color:#175cd3;text-decoration:none}.dsh-fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.dsh-fields label{display:grid;gap:4px;font-size:12px;color:#475467}.dsh-fields textarea{min-height:64px;resize:vertical;padding:8px;border:1px solid #d0d5dd;border-radius:6px;font:13px/1.4 ui-monospace,SFMono-Regular,Consolas,monospace}.dsh-save{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.dsh-save button{border:0;border-radius:6px;padding:9px 13px;background:#175cd3;color:#fff;cursor:pointer}@media(max-width:900px){.dsh-fields{grid-template-columns:1fr}}</style><div class=\"dsh-editor\"><div class=\"meta\"><div><strong>");
    vm_mock_admin_text_append_html(page, resourceUtf8);
    vm_mock_admin_text_appendf(
        page,
        "</strong><br><span class=\"hint\">原始 DSH 表格。保存仅改动当前行的列值，字段名、行顺序和其他行保持不变。</span></div></div><div class=\"dsh-summary\"><span>列 %u</span><span>行 %u</span><span>原始数据 %u B</span></div>",
        table.columnCount, table.rowCount, rawLen);
    if (table.rowCount == 0 ||
        !vm_mock_admin_dsh_row_at(raw, rawLen, &table, selectedRow,
                                  &rowOffset, &rowLen))
    {
        vm_mock_admin_text_appendf(
            page,
            "<div class=\"notice error\">该 DSH 没有可编辑行，或当前行记录已损坏。</div></div>");
        free(raw);
        return;
    }
    vm_mock_admin_text_appendf(
        page,
        "<form class=\"dsh-row-tools\" method=\"get\" action=\"/\"><input type=\"hidden\" name=\"tab\" value=\"content\"><input type=\"hidden\" name=\"content_kind\" value=\"dsh\"><input type=\"hidden\" name=\"resource\" value=\"");
    vm_mock_admin_text_append_html(page, resourceUtf8);
    vm_mock_admin_text_appendf(
        page,
        "\"><label><span>跳转行号（0–%u）</span><input name=\"dsh_row\" type=\"number\" min=\"0\" max=\"%u\" value=\"%u\"></label><button type=\"submit\">查看</button><a class=\"dsh-link\" data-admin-select href=\"/?tab=content&amp;content_kind=dsh&amp;resource=%s&amp;dsh_row=%u\">上一行</a><a class=\"dsh-link\" data-admin-select href=\"/?tab=content&amp;content_kind=dsh&amp;resource=%s&amp;dsh_row=%u\">下一行</a></form>",
        table.rowCount - 1u, table.rowCount - 1u, selectedRow,
        encodedResource, selectedRow == 0 ? 0 : selectedRow - 1u,
        encodedResource,
        selectedRow + 1u >= table.rowCount ? table.rowCount - 1u : selectedRow + 1u);
    vm_mock_admin_text_appendf(
        page,
        "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-dsh-row\"><input type=\"hidden\" name=\"resource\" value=\"");
    vm_mock_admin_text_append_html(page, resourceUtf8);
    vm_mock_admin_text_appendf(
        page,
        "\"><input type=\"hidden\" name=\"dsh_row\" value=\"%u\"><input type=\"hidden\" name=\"row_fingerprint\" value=\"%u\"><div class=\"dsh-fields\">",
        selectedRow, vm_mock_admin_dsh_row_fingerprint(raw + rowOffset, rowLen));
    for (u32 column = 0; column < table.columnCount; ++column)
    {
        const u8 *value = NULL;
        u8 valueLen = 0;
        char columnUtf8[512];
        char valueUtf8[1024];

        memset(columnUtf8, 0, sizeof(columnUtf8));
        memset(valueUtf8, 0, sizeof(valueUtf8));
        if (!vm_mock_admin_dsh_row_value_at(raw, rowOffset, rowLen,
                                            table.columnCount, column, &value,
                                            &valueLen))
        {
            continue;
        }
        vm_mock_admin_dsh_bytes_to_utf8(raw + table.columnOffsets[column],
                                        table.columnLengths[column],
                                        columnUtf8, sizeof(columnUtf8));
        vm_mock_admin_dsh_bytes_to_utf8(value, valueLen, valueUtf8,
                                        sizeof(valueUtf8));
        vm_mock_admin_text_appendf(page,
            "<label><span>%u · ", column + 1u);
        vm_mock_admin_text_append_html(page,
                                       columnUtf8[0] ? columnUtf8 : "未命名字段");
        vm_mock_admin_text_appendf(page,
            "</span><textarea name=\"dsh_value_%u\" maxlength=\"255\">",
            column);
        vm_mock_admin_text_append_html(page, valueUtf8);
        vm_mock_admin_text_appendf(page, "</textarea></label>");
    }
    vm_mock_admin_text_appendf(
        page,
        "</div><div class=\"dsh-save\"><button type=\"submit\">保存第 %u 行并发布</button><span class=\"hint\">若该行已被其他管理操作修改，保存会被拒绝，避免覆盖新数据。</span></div></form></div>",
        selectedRow);
    free(raw);
}

static void vm_mock_admin_render_content_resource_page(
    char *response, size_t responseCap, const char *query, const char *suffix,
    const char *kindLabel, bool gifMode)
{
    vm_mock_admin_scene_file files[VM_MOCK_ADMIN_CONTENT_FILE_MAX];
    vm_mock_admin_text page;
    char selectedUtf8[192];
    char selectedGame[64];
    char status[16];
    char message[256];
    u32 count = 0;
    u8 *payload = NULL;
    u32 payloadLen = 0;
    u8 type = 0;

    memset(files, 0, sizeof(files));
    memset(selectedUtf8, 0, sizeof(selectedUtf8));
    memset(selectedGame, 0, sizeof(selectedGame));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    count = vm_mock_admin_collect_content_files_by_suffix(
        files, VM_MOCK_ADMIN_CONTENT_FILE_MAX, suffix);
    (void)vm_mock_admin_content_resource_from_query(
        query, "resource", suffix, selectedUtf8, sizeof(selectedUtf8),
        selectedGame, sizeof(selectedGame));
    {
        bool found = false;
        for (u32 i = 0; i < count; ++i)
        {
            if (strcmp(files[i].name, selectedGame) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found && count != 0)
            snprintf(selectedGame, sizeof(selectedGame), "%s", files[0].name);
    }
    vm_net_mock_gbk_label_to_utf8(selectedGame, selectedUtf8,
                                  sizeof(selectedUtf8));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message,
                                   sizeof(message));
    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 游戏内容管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100%%;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1280px;height:100vh;margin:auto;padding:24px 18px;display:flex;flex-direction:column}.head{display:flex;justify-content:space-between;gap:16px;align-items:flex-start}.head h1{font-size:24px;margin:0}.sub{margin:4px 0 16px;color:#667085}.tabs,.format-tabs{display:flex;gap:6px;flex-wrap:wrap;margin:0 0 16px}.tab,.format{padding:8px 13px;border:1px solid #e4e7ec;border-radius:7px;color:#475467;background:#fff;text-decoration:none}.tab.on,.format.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:#fff;color:#475467}.grid{display:grid;grid-template-columns:300px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.catalog{display:flex;flex-direction:column;min-height:0}.list{display:grid;gap:4px;overflow:auto;min-height:0}.file{display:flex;justify-content:space-between;gap:8px;padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none}.file:hover,.file.on{background:#eef4ff;color:#175cd3}.size,.hint{font-size:12px;color:#667085}.detail{overflow:auto;min-width:0}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.preview{display:grid;grid-template-columns:minmax(180px,420px) 1fr;gap:18px;align-items:start}.preview img{display:block;max-width:100%%;max-height:520px;image-rendering:pixelated;background:#101828;border:1px solid #d0d5dd}.meta{display:grid;gap:8px}.meta div{padding:9px 10px;border-radius:7px;background:#f8fafc}.upload{margin-top:18px;padding:14px;border:1px solid #bfd4f4;border-radius:9px;background:#f7faff}.upload h3{margin:0 0 5px}.upload form{display:flex;align-items:end;gap:10px;flex-wrap:wrap}.upload label{display:grid;gap:4px;min-width:260px}.upload input{padding:8px;border:1px solid #d0d5dd;border-radius:6px;background:#fff}.upload button{border:0;border-radius:6px;padding:9px 13px;background:#175cd3;color:#fff;cursor:pointer}.empty{color:#667085;padding:16px 0}@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:18px 10px}.grid,.preview{grid-template-columns:1fr}.catalog{min-height:260px}.detail{overflow:visible}}</style>"
        "</head><body><main class=\"wrap\"><header class=\"head\"><div><h1>江湖OL 后台管理</h1><p class=\"sub\">游戏内容管理 · 按资源文件格式分类</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab on\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a></nav>"
        "<div class=\"grid\"><aside class=\"card catalog\"><h2>文件格式</h2><nav class=\"format-tabs\"><a class=\"format\" data-admin-select href=\"/?tab=content&amp;content_kind=sce\">SCE 场景</a><a class=\"format%s\" data-admin-select href=\"/?tab=content&amp;content_kind=gif\">GIF 图片</a><a class=\"format%s\" data-admin-select href=\"/?tab=content&amp;content_kind=dsh\">DSH 数据</a></nav><h2>%s（%u）</h2><div class=\"list\" data-admin-list>",
        gifMode ? " on" : "", gifMode ? "" : " on", kindLabel, count);
    for (u32 i = 0; i < count; ++i)
    {
        char utf8[192];
        char encoded[512];

        vm_net_mock_gbk_label_to_utf8(files[i].name, utf8, sizeof(utf8));
        vm_mock_admin_url_encode(utf8, encoded, sizeof(encoded));
        vm_mock_admin_text_appendf(&page,
            "<a class=\"file%s\" data-admin-select href=\"/?tab=content&amp;content_kind=%s&amp;resource=%s\"><span>",
            strcmp(files[i].name, selectedGame) == 0 ? " on" : "",
            gifMode ? "gif" : "dsh", encoded);
        vm_mock_admin_text_append_html(&page, utf8);
        vm_mock_admin_text_appendf(&page, "</span><span class=\"size\">%llu B</span></a>",
                                   (unsigned long long)files[i].size);
    }
    if (count == 0)
        vm_mock_admin_text_appendf(&page, "<p class=\"empty\">未找到 %s 文件。</p>", suffix);
    vm_mock_admin_text_appendf(&page,
        "</div></aside><section class=\"card detail\" data-admin-detail><h2>%s 管理</h2>",
        kindLabel);
    if (status[0] && message[0])
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (gifMode && selectedGame[0] &&
        vm_mock_admin_load_data_payload(selectedGame, suffix, &payload,
                                        &payloadLen, &type))
    {
        char encoded[512];

        vm_mock_admin_url_encode(selectedUtf8, encoded, sizeof(encoded));
        vm_mock_admin_text_appendf(&page,
            "<div class=\"preview\"><img src=\"/gif-preview.bmp?gif=%s\" alt=\"GIF 预览\"><div class=\"meta\"><div><strong>",
            encoded);
        vm_mock_admin_text_append_html(&page, selectedUtf8);
        vm_mock_admin_text_appendf(&page,
            "</strong><br><span class=\"hint\">客户端 type-1 GIF 资源，已按游戏调色板/LZW 格式保存。</span></div><div>资源容器：type %u</div><div>解码后图片数据：%u B</div></div></div>",
            type, payloadLen);
    }
    else if (!gifMode && selectedGame[0])
    {
        vm_mock_admin_render_dsh_resource_editor(&page, selectedGame,
                                                 selectedUtf8, query);
    }
    else if (selectedGame[0])
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"notice error\">资源容器无法解码，已禁止作为可编辑资源导入或发布。</div>");
    }
    free(payload);
    vm_mock_admin_text_appendf(&page,
        "<section class=\"upload\"><h3>%s</h3><p class=\"hint\">%s</p><form method=\"post\" action=\"/%s-upload\" enctype=\"multipart/form-data\"><label><span>选择文件</span><input type=\"file\" name=\"resource_file\" %s required></label><button type=\"submit\">上传并发布</button></form></section>",
        gifMode ? "新增或替换 GIF 图片" : "导入或替换 DSH 数据",
        gifMode ? "支持 PNG、JPG、BMP 或标准 GIF，文件名会保留并自动改为 .gif；最大 768×768 像素。上传后编码为客户端可识别的游戏 GIF 并加入内容更新。" : "仅接受原始 DSH 表格文件：声明长度、字段头和每一行的列记录必须完整。文件名决定新增或替换目标，并自动加入内容更新。",
        gifMode ? "gif" : "dsh",
        gifMode ? "accept=\"image/png,image/jpeg,image/bmp,image/gif\"" : "accept=\".dsh\"");
    vm_mock_admin_text_appendf(&page, "</section></div></main></body></html>");
    if (page.truncated)
        snprintf(response, responseCap, "<!doctype html><meta charset=\"utf-8\"><p>游戏内容页面超过大小限制。</p>");
}

static void vm_mock_admin_render_sce_portal_editor(
    vm_mock_admin_text *page, const char *sceneUtf8,
    const vm_mock_admin_scene_portal *portals, u32 portalCount,
    u32 portalTotal, const vm_mock_admin_scene_file *sceneFiles,
    u32 sceneCount)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"portal-editor\"><p class=\"foot\">每一项都直接对应已解析 SCE 记录中的目标场景字段。只允许从当前服务端实际存在的 SCE 文件中选择，保存不会改动入口号、坐标、碰撞区域或交互名称。</p><div class=\"portal-list\">");
    for (u32 i = 0; i < portalCount; ++i)
    {
        const vm_mock_admin_scene_portal *portal = &portals[i];
        char targetUtf8[192];
        char displayUtf8[192];
        const char *kind = "边界传送";

        memset(targetUtf8, 0, sizeof(targetUtf8));
        memset(displayUtf8, 0, sizeof(displayUtf8));
        vm_net_mock_gbk_label_to_utf8(portal->targetScene, targetUtf8,
                                      sizeof(targetUtf8));
        vm_net_mock_gbk_label_to_utf8(portal->displayName, displayUtf8,
                                      sizeof(displayUtf8));
        if (portal->kind == VM_MOCK_ADMIN_PORTAL_META)
            kind = "区域传送";
        else if (portal->kind == VM_MOCK_ADMIN_PORTAL_NAMED)
            kind = "具名传送";
        vm_mock_admin_text_appendf(
            page,
            "<form method=\"post\" action=\"/action\" class=\"portal-row\"><input type=\"hidden\" name=\"action\" value=\"save-sce-portal-target\"><input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(page, sceneUtf8 ? sceneUtf8 : "");
        vm_mock_admin_text_appendf(
            page,
            "\"><input type=\"hidden\" name=\"portal_offset\" value=\"%u\"><div><strong>%s</strong><span class=\"size\">",
            portal->targetLengthOffset, kind);
        if (displayUtf8[0] != 0)
        {
            vm_mock_admin_text_append_html(page, displayUtf8);
            vm_mock_admin_text_appendf(page, " · ");
        }
        if (portal->entryId == 0xffffu)
            vm_mock_admin_text_appendf(page, "入口 -- → %u", portal->targetEntryId);
        else
            vm_mock_admin_text_appendf(page, "入口 %u → %u", portal->entryId,
                                       portal->targetEntryId);
        vm_mock_admin_text_appendf(
            page, " · 区域 (%u,%u)-(%u,%u)</span></div><label class=\"field\"><span>目标 SCE</span><select name=\"target_scene\" required>",
            portal->left, portal->top, portal->right, portal->bottom);
        for (u32 j = 0; j < sceneCount; ++j)
        {
            char candidateUtf8[192];

            memset(candidateUtf8, 0, sizeof(candidateUtf8));
            vm_net_mock_gbk_label_to_utf8(sceneFiles[j].name, candidateUtf8,
                                          sizeof(candidateUtf8));
            vm_mock_admin_text_appendf(page, "<option value=\"");
            vm_mock_admin_text_append_html(page, candidateUtf8);
            vm_mock_admin_text_appendf(page, "\"%s>",
                                       strcmp(sceneFiles[j].name,
                                              portal->targetScene) == 0 ?
                                           " selected" : "");
            vm_mock_admin_text_append_html(page, candidateUtf8);
            vm_mock_admin_text_appendf(page, "</option>");
        }
        vm_mock_admin_text_appendf(
            page,
            "</select></label><div class=\"actions\"><button type=\"submit\">保存目标场景</button></div></form>");
    }
    if (portalCount == 0)
        vm_mock_admin_text_appendf(page,
            "<p class=\"size\">该场景没有可安全编辑的传送点目标。</p>");
    if (portalTotal > portalCount)
        vm_mock_admin_text_appendf(page,
            "<p class=\"foot\">共识别 %u 个传送点；仅显示具有精确原始字段位置的 %u 个记录。</p>",
            portalTotal, portalCount);
    vm_mock_admin_text_appendf(page, "</div></div>");
}

static void vm_mock_admin_render_content_page(char *response,
                                              size_t responseCap,
                                              const char *query)
{
    vm_mock_admin_scene_file sceneFiles[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    vm_mock_admin_scene_file actorFiles[VM_MOCK_ADMIN_ACTOR_FILE_MAX];
    vm_mock_admin_scene_file xseFiles[VM_MOCK_ADMIN_XSE_FILE_MAX];
    vm_net_mock_dynamic_npc_admin_row npcRows[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
    vm_net_mock_native_npc_admin_row nativeNpcRows[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    vm_net_mock_scene_npcinfo_seed previewNpcRows[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    vm_mock_admin_scene_portal previewPortalRows[VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX];
    vm_mock_admin_scene_preview preview;
    vm_mock_admin_text page;
    char selectedSceneUtf8[192];
    char selectedSceneFile[64];
    char runtimeScene[64];
    char contentKind[16];
    char contentSection[16];
    char status[16];
    char message[256];
    u32 sceneCount = 0;
    u32 actorCount = 0;
    u32 xseCount = 0;
    u32 npcCount = 0;
    u32 nativeNpcCount = 0;
    u32 previewNpcCount = 0;
    u32 previewNpcTotal = 0;
    u32 previewDynamicCount = 0;
    u32 previewPortalCount = 0;
    u32 previewPortalTotal = 0;
    bool previewReady = false;
    bool portalSection = false;

    memset(sceneFiles, 0, sizeof(sceneFiles));
    memset(actorFiles, 0, sizeof(actorFiles));
    memset(xseFiles, 0, sizeof(xseFiles));
    memset(npcRows, 0, sizeof(npcRows));
    memset(nativeNpcRows, 0, sizeof(nativeNpcRows));
    memset(previewNpcRows, 0, sizeof(previewNpcRows));
    memset(previewPortalRows, 0, sizeof(previewPortalRows));
    memset(&preview, 0, sizeof(preview));
    memset(selectedSceneUtf8, 0, sizeof(selectedSceneUtf8));
    memset(selectedSceneFile, 0, sizeof(selectedSceneFile));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(contentKind, 0, sizeof(contentKind));
    memset(contentSection, 0, sizeof(contentSection));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "content_kind", contentKind,
                                   sizeof(contentKind));
    if (strcmp(contentKind, "gif") == 0)
    {
        vm_mock_admin_render_content_resource_page(
            response, responseCap, query, ".gif", "GIF 图片", true);
        return;
    }
    if (strcmp(contentKind, "dsh") == 0)
    {
        vm_mock_admin_render_content_resource_page(
            response, responseCap, query, ".dsh", "DSH 数据", false);
        return;
    }
    (void)vm_mock_admin_form_value(query, "content_section", contentSection,
                                   sizeof(contentSection));
    portalSection = strcmp(contentSection, "portals") == 0;
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
        nativeNpcCount = vm_net_mock_native_npc_admin_list(
            runtimeScene, nativeNpcRows, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX);
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
        ".tabs,.format-tabs,.content-sections{display:flex;gap:6px;margin:0 0 16px;flex-wrap:wrap}.tab,.format,.section-tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on,.format.on,.section-tab.on{background:#175cd3;color:#fff;border-color:#175cd3}"
        ".logout{background:none;color:#667085;border:1px solid #d0d5dd}.grid{display:grid;grid-template-columns:300px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.grid>aside{display:flex;flex-direction:column;min-height:0;overflow:hidden}.grid>section{min-width:0;min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}"
        ".scene-list{display:flex;flex:1;min-height:0;flex-direction:column;gap:4px;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.scene{display:flex;justify-content:space-between;gap:8px;padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none;scroll-margin-block:12px}.scene:hover,.scene.on{background:#eef4ff;color:#175cd3}.size{color:#98a2b3;font-size:12px;white-space:nowrap}"
        ".preview{border:1px solid #d0d5dd;border-radius:9px;padding:12px;margin:0 0 16px;background:#f9fafb}.preview-head{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:10px}.map-scroll{overflow:auto;max-height:760px;padding:8px;border-radius:7px;background:#1f2937}.map-stage{position:relative;margin:auto;box-shadow:0 0 0 1px #0008;background:#111;overflow:visible}.map-stage>img{display:block;width:100%%;height:100%%;image-rendering:pixelated}.portal-box{position:absolute;z-index:1;border:2px dashed #fdb022;background:#fec84b26;pointer-events:none}.portal-box.named{border-color:#22d3ee;background:#22d3ee24}.portal-label{position:absolute;left:-2px;bottom:100%%;max-width:220px;padding:1px 4px;border-radius:3px 3px 0 0;background:#7a2e0e;color:#fff;font-size:10px;line-height:15px;white-space:nowrap}.portal-box.named .portal-label{background:#0e7490}.npc-pin{position:absolute;transform:translate(-50%%,-100%%);display:flex;flex-direction:column;align-items:center;z-index:3;filter:drop-shadow(0 1px 1px #0008);pointer-events:none}.pin-name{max-width:140px;padding:1px 4px;border-radius:3px;background:#175cd3;color:#fff;font-size:11px;line-height:16px;white-space:nowrap}.npc-pin.service .pin-name{background:#b54708}.sprite-wrap{position:relative;display:flex;align-items:flex-end;justify-content:center;min-width:18px;min-height:18px}.actor-sprite{display:block;width:auto;height:auto;max-width:72px;max-height:72px;image-rendering:pixelated}.preview-legend,.preview-npcs,.preview-portals{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:9px}.legend-icon{display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;border-radius:4px;background:#175cd3;color:#fff;font-size:11px}.legend-icon.service{background:#b54708}.legend-portal{width:18px;height:12px;border:2px dashed #fdb022;background:#fec84b26}.legend-portal.named{border-color:#22d3ee;background:#22d3ee24}.npc-chip,.portal-chip{font-size:12px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#344054}.npc-chip.service{background:#fff4e8}.portal-chip{background:#fffaeb;color:#7a2e0e}.portal-chip.named{background:#ecfdff;color:#0e7490}.preview-error{padding:12px;border-radius:7px;background:#fef3f2;color:#b42318;margin-bottom:16px}"
        ".notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.npc-list{display:grid;gap:12px}.npc{border:1px solid #e4e7ec;border-radius:8px;padding:13px}.npc.off{opacity:.62;background:#f9fafb}.npc-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}.badge{font-size:12px;background:#eef4ff;color:#175cd3;padding:2px 7px;border-radius:999px}.fields{display:grid;grid-template-columns:110px 1.1fr 1fr 90px 90px 90px 90px;gap:8px}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}.instance-fields{display:grid;grid-template-columns:minmax(220px,2fr) 90px 90px 120px 100px;gap:8px;align-items:end;margin-top:10px;padding:10px;border-radius:7px;background:#fffaeb;border:1px solid #fedf89}.instance-help{grid-column:1/-1;margin:0;color:#7a2e0e;font-size:12px}"
        ".npc-editor{padding:16px;background:linear-gradient(180deg,#fff 0,#fbfdff 100%%);border-color:#cbd5e1}.npc-editor-grid{display:grid;grid-template-columns:110px minmax(160px,1.1fr) minmax(220px,1.35fr) 96px 96px;gap:10px;align-items:end}.npc-editor-options{display:grid;grid-template-columns:minmax(220px,1.2fr) minmax(250px,1.45fr) minmax(190px,.9fr);gap:10px;align-items:end;margin-top:12px;padding:12px;border:1px solid #dbe7fb;border-radius:8px;background:#f7faff}.npc-services{grid-column:1/-1;display:grid;grid-template-columns:minmax(180px,.85fr) minmax(185px,1fr) minmax(225px,1.35fr);gap:10px;margin:0;padding:12px;border:1px solid #bfd4f4;border-radius:8px;background:#fff}.npc-services legend{padding:0 5px;color:#1849a9;font-weight:700}.npc-services .hint{grid-column:1/-1;margin:0 0 2px;color:#475467;font-size:12px;line-height:1.5}.npc-service-row{display:contents}.npc-service-toggle{display:flex;align-items:center;gap:8px;min-height:39px;padding:8px 10px;border:1px solid #d0d5dd;border-radius:6px;background:#f8fafc;color:#344054;font-weight:600}.npc-service-toggle input{width:auto;margin:0}.npc-service-toggle:has(input:checked){border-color:#84adff;background:#eef4ff;color:#175cd3}.npc-editor-options .instance-fields{grid-column:1/-1}.npc-editor-save{padding-top:2px;margin-top:12px;border-top:1px solid #eaecf0}.npc-editor-post-actions{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:8px;margin-top:8px}.npc-editor-action{margin:0}.npc-editor-action button{min-width:108px}"
        "input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.secondary{background:#475467}.danger{background:#b42318}.enable{background:#027a48}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:10px}.new{margin-top:16px}.foot{color:#667085;font-size:12px;margin:12px 0 0}.portal-editor{margin-top:8px}.portal-list{display:grid;gap:10px}.portal-row{display:grid;grid-template-columns:minmax(220px,1fr) minmax(230px,1fr) max-content;gap:12px;align-items:end;padding:13px;border:1px solid #c7d7fe;border-radius:9px;background:#f8fbff}.portal-row>div:first-of-type{display:grid;gap:3px}.portal-row strong{color:#1849a9}.portal-row .actions{margin:0}.native-section{margin:0 0 18px;padding:14px;border:1px solid #c7d7fe;border-radius:9px;background:#f5f8ff}.native-title{display:flex;justify-content:space-between;gap:10px;align-items:baseline}.native-meta{margin:4px 0 10px;color:#475467;font-size:12px}.inventory{display:grid;gap:12px;margin-top:12px;padding:13px;border:1px solid #c7d7fe;border-radius:9px;background:#fbfdff}.inventory-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding-bottom:9px;border-bottom:1px solid #e4e7ec}.inventory h4{margin:0;font-size:14px}.stock-note,.inventory-error{color:#667085;font-size:12px}.inventory-error{margin:0;color:#b42318}.inventory-tools{display:grid;gap:10px}.inventory-add-form,.inventory-remove-form{display:grid;grid-template-columns:minmax(260px,1fr) 118px max-content max-content;gap:9px;align-items:end;margin:0;padding:11px;border:1px solid #dfe7f6;border-left:3px solid #175cd3;border-radius:7px;background:#f8fafc}.inventory-remove-form{grid-template-columns:minmax(180px,1fr) minmax(145px,.8fr) max-content max-content;border-left-color:#b42318}.inventory-form-tag{grid-column:1/-1;font-size:12px;font-weight:700;line-height:1;color:#175cd3}.inventory-form-tag.remove{color:#b42318}.inventory-list{display:grid}.inventory-row{display:grid;grid-template-columns:66px minmax(170px,1fr) 110px 100px 80px 70px;gap:8px;align-items:end;padding:8px 0;border-top:1px solid #eaecf0}.stock-check{display:flex;align-items:center;gap:4px;min-height:39px;color:#475467;font-size:12px}.stock-check input{width:auto}.inventory-row-form{display:contents}.inventory-row .actions{margin:0}.item-field{display:grid;gap:4px}.item-field>span{font-size:12px;color:#667085}.item-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px;white-space:normal}.item-picker-trigger small{color:#667085;font-weight:400}.item-modal{position:fixed;inset:0;z-index:1001;display:grid;place-items:center;padding:20px;background:#10182899}.item-picker-panel{width:min(820px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.item-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.item-picker-head h3{font-size:19px;margin:0}.item-picker-head p{margin:2px 0 0;color:#667085}.item-picker-head-actions{display:flex;gap:8px}.item-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.item-picker-tools{display:grid;grid-template-columns:minmax(200px,.8fr) minmax(260px,1.2fr);gap:10px;padding:14px 20px 10px}.npc-stock-modal .item-picker-tools{grid-template-columns:minmax(150px,.7fr) minmax(130px,.55fr) minmax(220px,1.15fr)}.item-picker-tools label{display:grid;gap:4px}.item-picker-tools label>span{font-size:12px;color:#667085}.item-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.item-picker-error{color:#b42318;font-weight:600}.item-picker-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:140px;overflow:auto;padding:0 20px 20px}.item-choice{display:grid;gap:2px;padding:10px 12px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.item-choice:hover{border-color:#84adff;background:#f5f8ff}.item-choice strong{font-size:14px}.item-choice span{color:#667085;font-size:12px}.npc-stock-picker-actions{display:flex;gap:8px;justify-content:flex-end;padding:0 20px 10px}.npc-stock-choice{grid-template-columns:auto minmax(0,1fr);align-items:start}.npc-stock-choice input{width:auto;margin-top:3px}.npc-stock-choice.selected{border-color:#175cd3;background:#eef4ff}.item-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}[hidden]{display:none!important}"
        ".actor-picker-field{display:grid;gap:4px}.actor-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px;white-space:normal}.actor-picker-trigger small{color:#667085;font-weight:400}.actor-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899}.actor-picker-panel{width:min(920px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.actor-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.actor-picker-head h3{font-size:19px;margin:0}.actor-picker-head p{margin:2px 0 0;color:#667085}.actor-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.actor-picker-tools{padding:14px 20px 10px}.actor-picker-tools label{display:grid;gap:4px}.actor-picker-tools label>span{font-size:12px;color:#667085}.actor-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.actor-picker-error{color:#b42318;font-weight:600}.actor-picker-list{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;min-height:160px;overflow:auto;padding:0 20px 20px}.actor-choice{display:grid;grid-template-rows:92px auto;gap:7px;padding:10px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.actor-choice:hover{border-color:#84adff;background:#f5f8ff}.actor-choice img{display:block;width:100%%;height:88px;object-fit:contain;image-rendering:pixelated;background:#f9fafb;border-radius:5px}.actor-choice strong{font-size:12px;overflow-wrap:anywhere}.actor-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}[hidden]{display:none!important}.modal-open{overflow:hidden}"
        "@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:18px 10px;overflow:visible}.grid{grid-template-columns:1fr;flex:none}.grid>aside,.grid>section{overflow:visible}.scene-list{flex:none;max-height:260px;overflow:auto}.fields,.instance-fields,.npc-editor-grid,.npc-editor-options,.npc-services{grid-template-columns:1fr 1fr}.inventory-add-form,.inventory-remove-form,.inventory-row{grid-template-columns:1fr 1fr}.inventory-form-tag{grid-column:1/-1}.npc-stock-modal .item-picker-tools{grid-template-columns:1fr}.stock-check{grid-column:1/-1}.inventory-row-form{display:grid;grid-column:1/-1;grid-template-columns:1fr 1fr;gap:8px}.inventory-row>.actions{grid-column:1/-1;justify-self:start}.instance-scene,.instance-help{grid-column:1/-1}}@media(max-width:560px){.npc-editor-grid,.npc-editor-options,.npc-services{grid-template-columns:1fr}.npc-editor-post-actions{justify-content:stretch}.npc-editor-action{flex:1}.npc-editor-action button{width:100%%}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">场景资源、原生 NPC 覆盖与服务端动态 NPC</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab on\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a>"
        "<a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "<a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>"
        "<div class=\"grid\"><aside class=\"card\"><h2>文件格式</h2><nav class=\"format-tabs\"><a class=\"format on\" href=\"/?tab=content&amp;content_kind=sce\">SCE 场景</a><a class=\"format\" href=\"/?tab=content&amp;content_kind=gif\">GIF 图片</a><a class=\"format\" href=\"/?tab=content&amp;content_kind=dsh\">DSH 数据</a></nav><h2>SCE 场景（%u）</h2><div class=\"scene-list\" data-admin-list>",
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
                "<a id=\"selected-scene\" class=\"scene on\" data-admin-select aria-current=\"page\" href=\"/?tab=content&amp;content_kind=sce&amp;content_section=%s&amp;scene=%s#selected-scene\"><span>",
                portalSection ? "portals" : "npcs", encoded);
        }
        else
        {
            vm_mock_admin_text_appendf(&page,
                "<a class=\"scene\" data-admin-select href=\"/?tab=content&amp;content_kind=sce&amp;content_section=%s&amp;scene=%s#selected-scene\"><span>",
                portalSection ? "portals" : "npcs", encoded);
        }
        vm_mock_admin_text_append_html(&page, sceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "</span><span class=\"size\">%llu B</span></a>",
            (unsigned long long)sceneFiles[i].size);
    }
    if (sceneCount == 0)
        vm_mock_admin_text_appendf(&page, "<span class=\"size\">未找到 SCE 文件</span>");
    vm_mock_admin_text_appendf(&page,
        "</div></aside><section data-admin-detail><div class=\"card\"><h2>场景 %s：",
        portalSection ? "坐标传送管理" : "NPC 管理");
    vm_mock_admin_text_append_html(&page,
                                   selectedSceneUtf8[0] ? selectedSceneUtf8 : "未选择场景");
    vm_mock_admin_text_appendf(
        &page,
        "</h2><nav class=\"content-sections\"><a class=\"section-tab%s\" href=\"/?tab=content&amp;content_kind=sce&amp;content_section=npcs&amp;scene=",
        portalSection ? "" : " on");
    {
        char selectedEncoded[512];
        vm_mock_admin_url_encode(selectedSceneUtf8, selectedEncoded,
                                 sizeof(selectedEncoded));
        vm_mock_admin_text_append_html(&page, selectedEncoded);
        vm_mock_admin_text_appendf(&page,
            "\">NPC 管理</a><a class=\"section-tab%s\" href=\"/?tab=content&amp;content_kind=sce&amp;content_section=portals&amp;scene=%s\">坐标传送管理</a></nav><p class=\"foot\">%s</p>",
            portalSection ? " on" : "", selectedEncoded,
            portalSection ?
                "传送点只编辑原 SCE 已解析记录的目标场景名称；入口编号、坐标区域和其他字段保持不变。保存会重新封装 SCE 并通过内容更新发布。" :
                "保存 NPC 会校验 Actor 及引用 GIF。客户端加载时若文件缺失，会先通过 WT 18/7 从服务端下载、校验并安装，再继续创建 NPC；后台不会向客户端目录复制文件。");
    }
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
                " · (%u,%u) · ", seed->x, seed->y);
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
            vm_mock_admin_text_appendf(&page,
                " NPC 模型\"></span></div>");
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
                &page, " (%u,%u) · %s</span>",
                seed->x, seed->y, seed->actorResource);
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
    if (portalSection)
    {
        vm_mock_admin_render_sce_portal_editor(
            &page, selectedSceneUtf8, previewPortalRows, previewPortalCount,
            previewPortalTotal, sceneFiles, sceneCount);
    }
    else
    {
    vm_mock_admin_text_appendf(&page,
        "<div class=\"native-section\"><div class=\"native-title\"><h2>原生 NPC 覆盖</h2><span class=\"badge\">SCE 资源只读</span></div>"
        "<p class=\"foot\">精确绑定当前场景和原生 Actor。仅保存服务类型与启用状态，不会改写 SCE/XSE、模型、坐标或名称；武器/防具/药品商人在下方配置专属库存，单价留空时采用当前商品目录默认价。</p>");
    for (u32 i = 0; i < nativeNpcCount; ++i)
    {
        const vm_net_mock_native_npc_admin_row *row = &nativeNpcRows[i];
        char displayUtf8[128];

        memset(displayUtf8, 0, sizeof(displayUtf8));
        vm_net_mock_gbk_label_to_utf8(row->seed.displayName, displayUtf8,
                                      sizeof(displayUtf8));
        vm_mock_admin_text_appendf(&page,
            "<div class=\"npc%s\"><div class=\"npc-head\"><strong>",
            row->enabled ? "" : " off");
        vm_mock_admin_text_append_html(&page, displayUtf8);
        vm_mock_admin_text_appendf(&page,
            "</strong><span><span class=\"badge\">原生 Actor %u</span> <span class=\"badge\">%s</span></span></div>"
            "<div class=\"native-meta\">场景实体类型 %u · 坐标 (%u,%u) · Actor %s · XSE %s</div>"
            "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-native-npc-override\"><input type=\"hidden\" name=\"scene\" value=\"",
            row->seed.actorId, row->enabled ? "已启用" : "已停用",
            row->seed.sceneEntityKind, row->seed.x, row->seed.y,
            row->seed.actorResource,
            row->seed.scriptName[0] ? row->seed.scriptName : "—");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\"><div class=\"fields\">",
            row->seed.actorId);
        vm_mock_admin_text_appendf(&page,
            "<label class=\"field\"><span>启用状态</span><select name=\"enabled\"><option value=\"1\"%s>启用</option><option value=\"0\"%s>停用</option></select></label></div>",
            row->enabled ? " selected" : "", row->enabled ? "" : " selected");
        vm_mock_admin_render_npc_service_option_fields(
            &page, runtimeScene, &row->seed, false);
        vm_mock_admin_text_appendf(&page,
            "<div class=\"actions\"><button type=\"submit\">保存原生覆盖</button></div></form>");
        if (row->overridden)
        {
            vm_mock_admin_text_appendf(&page,
                "<form method=\"post\" action=\"/action\" class=\"actions\"><input type=\"hidden\" name=\"action\" value=\"delete-native-npc-override\"><input type=\"hidden\" name=\"scene\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">恢复 SCE 默认服务</button></form>",
                row->seed.actorId);
        }
        vm_mock_admin_render_npc_inventories(
            &page, selectedSceneUtf8, runtimeScene, &row->seed,
            "native-stock");
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (nativeNpcCount == 0)
        vm_mock_admin_text_appendf(&page, "<p class=\"size\">该场景没有可覆盖的 SCE 原生 NPC。</p>");
    vm_mock_admin_text_appendf(&page, "</div><h2>动态 NPC</h2><div class=\"npc-list\">");
    for (u32 i = 0; i < npcCount; ++i)
    {
        const vm_net_mock_dynamic_npc_admin_row *row = &npcRows[i];
        char displayUtf8[128];
        vm_net_mock_gbk_label_to_utf8(row->seed.displayName,
                                      displayUtf8, sizeof(displayUtf8));
        vm_mock_admin_text_appendf(&page,
            "<div class=\"npc npc-editor%s\"><div class=\"npc-head\"><strong>Actor %u</strong><span>",
            row->enabled ? "" : " off", row->seed.actorId);
        if (row->builtin)
            vm_mock_admin_text_appendf(&page, "<span class=\"badge\">内置%s</span> ",
                                       row->overridden ? "·已覆盖" : "");
        vm_mock_admin_text_appendf(&page, "<span class=\"badge\">%s</span></span></div>",
                                   row->enabled ? "已启用" : "已停用");
        vm_mock_admin_text_appendf(&page,
            "<form method=\"post\" action=\"/action\" class=\"npc-editor-form\"><input type=\"hidden\" name=\"action\" value=\"save-npc\">"
            "<input type=\"hidden\" name=\"scene\" value=\"");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "\"><div class=\"npc-editor-grid\"><label class=\"field\"><span>Actor ID</span><input name=\"actor_id\" value=\"%u\" readonly></label>"
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
            "<label class=\"field\"><span>Y</span><input type=\"number\" name=\"y\" min=\"1\" max=\"65535\" value=\"%u\" required></label>",
            row->seed.x, row->seed.y);
        vm_mock_admin_text_appendf(&page,
            "</div><div class=\"npc-editor-options\">");
        vm_mock_admin_render_npc_service_option_fields(
            &page, runtimeScene, &row->seed, true);
        vm_mock_admin_text_appendf(&page,
            "<label class=\"field\"><span>XSE 脚本（可留空）</span>");
        vm_mock_admin_render_xse_select(&page, xseFiles, xseCount,
                                        row->seed.scriptName);
        vm_mock_admin_text_appendf(&page,
            "</label><label class=\"field\"><span>可接取任务（可留空；会校验等级与前置任务）</span>");
        vm_mock_admin_render_npc_task_select(&page, row->seed.taskId);
        vm_mock_admin_text_appendf(&page,
            "</label><label class=\"field\"><span>任务重复接取规则</span>");
        vm_mock_admin_render_npc_task_repeat_policy_select(&page, &row->seed);
        vm_mock_admin_text_appendf(&page, "</label>");
        vm_mock_admin_render_instance_fields(&page, sceneFiles, sceneCount,
                                             &row->seed);
        vm_mock_admin_text_appendf(&page,
            "</div><div class=\"actions npc-editor-save\"><button type=\"submit\">保存修改</button></div></form>"
            "<div class=\"npc-editor-post-actions\"><form method=\"post\" action=\"/action\" class=\"npc-editor-action\"><input type=\"hidden\" name=\"action\" value=\"toggle-npc\">"
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
                "<form method=\"post\" action=\"/action\" class=\"npc-editor-action\"><input type=\"hidden\" name=\"action\" value=\"delete-npc-override\">"
                "<input type=\"hidden\" name=\"scene\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\">"
                "<button class=\"danger\" type=\"submit\">%s</button></form>",
                row->seed.actorId,
                row->builtin ? "恢复内置默认" : "删除自定义 NPC");
        }
        vm_mock_admin_text_appendf(&page, "</div>");
        vm_mock_admin_render_npc_inventories(
            &page, selectedSceneUtf8, runtimeScene, &row->seed,
            "dynamic-stock");
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (npcCount == 0)
        vm_mock_admin_text_appendf(&page, "<p class=\"size\">该场景没有服务端动态 NPC。</p>");
    vm_mock_admin_text_appendf(&page,
        "</div><div class=\"npc new npc-editor\"><div class=\"npc-head\"><strong>增加动态 NPC</strong><span class=\"badge\">下次进入场景生效</span></div>"
        "<form method=\"post\" action=\"/action\" class=\"npc-editor-form\"><input type=\"hidden\" name=\"action\" value=\"save-npc\">"
        "<input type=\"hidden\" name=\"scene\" value=\"");
    vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
    vm_mock_admin_text_appendf(&page,
        "\"><div class=\"npc-editor-grid\"><label class=\"field\"><span>Actor ID</span><input type=\"number\" name=\"actor_id\" min=\"1\" max=\"4294967295\" value=\"30000\" required></label>"
        "<label class=\"field\"><span>显示名称</span><input name=\"display_name\" maxlength=\"29\" required></label>"
        "<label class=\"field\"><span>Actor 资源</span>");
    vm_mock_admin_render_actor_select(&page, actorFiles, actorCount,
                                      "n_man1.actor");
    vm_mock_admin_text_appendf(&page,
        "</label>"
        "<label class=\"field\"><span>X</span><input type=\"number\" name=\"x\" min=\"1\" max=\"65535\" required></label>"
        "<label class=\"field\"><span>Y</span><input type=\"number\" name=\"y\" min=\"1\" max=\"65535\" required></label>");
    vm_mock_admin_text_appendf(&page,
        "</div><div class=\"npc-editor-options\">");
    vm_mock_admin_render_npc_service_option_fields(
        &page, runtimeScene, NULL, true);
    vm_mock_admin_text_appendf(&page,
        "<label class=\"field\"><span>XSE 脚本（可留空）</span>");
    vm_mock_admin_render_xse_select(&page, xseFiles, xseCount, NULL);
    vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\"><span>可接取任务（可留空；会校验等级与前置任务）</span>");
    vm_mock_admin_render_npc_task_select(&page, 0);
    vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\"><span>任务重复接取规则</span>");
    vm_mock_admin_render_npc_task_repeat_policy_select(&page, NULL);
    vm_mock_admin_text_appendf(&page, "</label>");
    vm_mock_admin_render_instance_fields(&page, sceneFiles, sceneCount, NULL);
    vm_mock_admin_text_appendf(&page,
        "</div><div class=\"actions npc-editor-save\"><button type=\"submit\">增加 NPC</button></div></form></div>"
        "<p class=\"foot\">对话服务功能决定客户端可操作入口；自定义名称和说明只改变该入口的显示文字。武器商人先按剑、匕首、法杖分类；防具商人提供头盔、衣甲、披风、腰带、护腿、鞋靴和戒指；药品商人提供 item.dsh 类别 10 的药品与消耗品。装备回收商人仅列出背包中的装备，并按装备基础价值的 50% 回收为铜钱；已装备在角色身上的物品不会出现在回收列表。副本向导可独立启用场景传送、守关怪挑战或同时启用两者，并按最低等级拦截。比武擂台会打开客户端原生擂台大厅，可开设、查看和加入在线擂台房间；房间随房主离线即时关闭。商品价格和上架状态均来自后台商品目录。装备修理按实际耐久收费；技能导师只列出当前职业、等级可学且尚未学习的技能。SCE 文件中的内置 NPC 不会被改写。客户端同场景最多安全显示 4 个动态名称，超出时仍按任务优先级筛选。</p>"
        "</div>");
    vm_mock_admin_render_actor_picker_modal(&page, actorFiles, actorCount);
    vm_mock_admin_render_item_picker_modal(&page, false);
    vm_mock_admin_render_npc_stock_picker_modal(&page);
    }
    vm_mock_admin_text_appendf(&page, "</section></div></main></body></html>");

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

static const char *vm_mock_admin_role_job_label(u8 job)
{
    switch (job)
    {
    case 1: return "战士";
    case 2: return "刺客";
    case 3: return "法师";
    default: return "未知职业";
    }
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

/* The shop catalog owns an equipment's category and quality, while equip.dsh
 * owns its required level.  Keep the labels sourced from those two catalog
 * records so every admin item picker describes the same immutable resource
 * data and never mistakes an instance enhancement level for its quality. */
static u32 vm_mock_admin_item_required_level(
    const vm_net_mock_shop_catalog_item *item)
{
    const vm_net_mock_equipment_catalog_item *equipment = NULL;

    if (item == NULL || !item->isEquip)
        return 0;
    equipment = vm_net_mock_find_equipment_catalog_item(item->itemId);
    return equipment != NULL ? equipment->levelRequired : 0;
}

static void vm_mock_admin_append_item_picker_label(
    vm_mock_admin_text *page, const vm_net_mock_shop_catalog_item *item,
    const char *itemNameUtf8, bool includeId)
{
    u32 levelRequired = 0;

    if (page == NULL || item == NULL)
        return;
    if (includeId)
        vm_mock_admin_text_appendf(page, "[%u] ", item->itemId);
    vm_mock_admin_text_append_html(
        page, itemNameUtf8 != NULL && itemNameUtf8[0] != 0
                  ? itemNameUtf8 : "未命名物品");
    if (!item->isEquip)
        return;
    levelRequired = vm_mock_admin_item_required_level(item);
    vm_mock_admin_text_appendf(page, " · 品质 %u · 需求等级 %u",
                               item->quality, levelRequired);
}

static void vm_mock_admin_render_item_picker_catalog_option(
    vm_mock_admin_text *page, const vm_net_mock_shop_catalog_item *item,
    bool includePrice)
{
    char itemNameUtf8[128];
    u32 levelRequired = 0;

    if (page == NULL || item == NULL)
        return;
    memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
    vm_net_mock_gbk_label_to_utf8(item->name, itemNameUtf8,
                                  sizeof(itemNameUtf8));
    levelRequired = vm_mock_admin_item_required_level(item);
    vm_mock_admin_text_appendf(
        page,
        "<option value=\"%u\" data-category=\"%c%u\" data-equip=\"%u\" data-quality=\"%u\" data-level=\"%u\"",
        item->itemId, item->isEquip ? 'e' : 'i', item->category,
        item->isEquip ? 1u : 0u, item->isEquip ? item->quality : 0u,
        levelRequired);
    if (includePrice)
    {
        vm_mock_admin_text_appendf(
            page, " data-price=\"%u\"",
            vm_net_mock_shop_effective_unit_price(item->itemId, item->price));
    }
    vm_mock_admin_text_appendf(page, ">");
    vm_mock_admin_append_item_picker_label(page, item, itemNameUtf8, true);
    vm_mock_admin_text_appendf(page, "</option>");
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
    if (item != NULL)
        vm_mock_admin_append_item_picker_label(page, item, itemNameUtf8, true);
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

static void vm_mock_admin_render_npc_stock_category_options(
    vm_mock_admin_text *page, u16 serviceKind)
{
    u32 first = 0;
    u32 last = 0;
    bool equipment = true;

    if (page == NULL)
        return;
    if (serviceKind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT)
    {
        first = 7;
        last = 9;
    }
    else if (serviceKind == VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT)
    {
        first = 0;
        last = 6;
    }
    else if (serviceKind == VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT)
    {
        first = 10;
        last = 10;
        equipment = false;
    }
    else
    {
        return;
    }
    vm_mock_admin_text_appendf(page,
        "<option value=\"all\">全部可售分类</option>");
    for (u32 category = first; category <= last; ++category)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"%c%u\">",
                                   equipment ? 'e' : 'i', category);
        vm_mock_admin_text_append_html(
            page, vm_mock_admin_item_category_name(equipment, (u8)category));
        vm_mock_admin_text_appendf(page, "</option>");
    }
}

/* equip.dsh owns this field (column 6, named \"品质\").  Keep the numeric
 * label rather than assigning colour/tier names that are not defined by the
 * client resource; an equipment's enhancement level is a separate runtime
 * value and must never be used as this filter. */
static bool vm_mock_admin_npc_stock_service_uses_quality(u16 serviceKind)
{
    return serviceKind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT ||
           serviceKind == VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT;
}

static bool vm_mock_admin_npc_stock_catalog_item_matches_service(
    const vm_net_mock_shop_catalog_item *item, u16 serviceKind)
{
    if (item == NULL)
        return false;
    if (serviceKind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT)
        return item->isEquip && item->category >= 7 && item->category <= 9;
    if (serviceKind == VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT)
        return item->isEquip && item->category <= 6;
    if (serviceKind == VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT)
        return !item->isEquip && item->category == 10;
    return false;
}

static void vm_mock_admin_render_npc_stock_quality_options(
    vm_mock_admin_text *page, u16 serviceKind)
{
    bool qualities[256];
    u32 itemCount = 0;

    if (page == NULL || !vm_mock_admin_npc_stock_service_uses_quality(serviceKind))
        return;
    memset(qualities, 0, sizeof(qualities));
    itemCount = vm_net_mock_load_shop_catalog();
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
        if (vm_mock_admin_npc_stock_catalog_item_matches_service(item, serviceKind))
            qualities[item->quality] = true;
    }
    vm_mock_admin_text_appendf(page, "<option value=\"all\">全部品质</option>");
    for (u32 quality = 0; quality < 256; ++quality)
    {
        if (qualities[quality])
            vm_mock_admin_text_appendf(page,
                "<option value=\"%u\">品质 %u</option>", quality, quality);
    }
}

static void vm_mock_admin_render_catalog_category_options(
    vm_mock_admin_text *page, const char *allLabel)
{
    bool equipmentCategories[256];
    bool itemCategories[256];
    u32 itemCount = 0;

    if (page == NULL)
        return;
    memset(equipmentCategories, 0, sizeof(equipmentCategories));
    memset(itemCategories, 0, sizeof(itemCategories));
    itemCount = vm_net_mock_load_shop_catalog();
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        if (item->isEquip)
            equipmentCategories[item->category] = true;
        else
            itemCategories[item->category] = true;
    }
    vm_mock_admin_text_appendf(page, "<option value=\"all\">");
    vm_mock_admin_text_append_html(
        page, allLabel != NULL ? allLabel : "全部物品分类");
    vm_mock_admin_text_appendf(page, "</option>");
    for (u32 category = 0; category < 256; ++category)
    {
        if (!equipmentCategories[category])
            continue;
        vm_mock_admin_text_appendf(page,
            "<option value=\"e%u\">装备 · ", category);
        vm_mock_admin_text_append_html(
            page, vm_mock_admin_item_category_name(true, (u8)category));
        vm_mock_admin_text_appendf(page, "</option>");
    }
    for (u32 category = 0; category < 256; ++category)
    {
        if (!itemCategories[category])
            continue;
        vm_mock_admin_text_appendf(page,
            "<option value=\"i%u\">物品 · ", category);
        vm_mock_admin_text_append_html(
            page, vm_mock_admin_item_category_name(false, (u8)category));
        vm_mock_admin_text_appendf(page, "</option>");
    }
}

static void vm_mock_admin_render_catalog_quality_options(
    vm_mock_admin_text *page, const char *allLabel)
{
    bool qualities[256];
    u32 itemCount = 0;

    if (page == NULL)
        return;
    memset(qualities, 0, sizeof(qualities));
    itemCount = vm_net_mock_load_shop_catalog();
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        if (item->isEquip)
            qualities[item->quality] = true;
    }
    vm_mock_admin_text_appendf(page, "<option value=\"all\">");
    vm_mock_admin_text_append_html(
        page, allLabel != NULL ? allLabel : "全部品质");
    vm_mock_admin_text_appendf(page, "</option>");
    for (u32 quality = 0; quality < 256; ++quality)
    {
        if (qualities[quality])
            vm_mock_admin_text_appendf(
                page, "<option value=\"%u\">品质 %u</option>", quality,
                quality);
    }
}

/* NPC inventories are a set of catalog products with local price/status
 * overrides.  Existing rows remain individually editable; the toolbar stages
 * an atomic multi-select add/remove request. */
static void vm_mock_admin_render_npc_inventory(
    vm_mock_admin_text *page, const char *sceneUtf8, const char *runtimeScene,
    u32 actorId, u16 serviceKind, const char *pickerPrefix)
{
    vm_net_mock_npc_shop_inventory_row *rows = NULL;
    u32 count = 0;
    u32 loaded = 0;
    u32 serviceCount = 0;

    if (page == NULL || sceneUtf8 == NULL || runtimeScene == NULL ||
        actorId == 0 || pickerPrefix == NULL ||
        !vm_net_mock_npc_service_kind_uses_inventory(serviceKind))
    {
        return;
    }
    count = vm_net_mock_npc_shop_inventory_admin_list(runtimeScene, actorId,
                                                       NULL, 0);
    if (count != 0)
    {
        rows = (vm_net_mock_npc_shop_inventory_row *)calloc(
            count, sizeof(*rows));
        if (rows != NULL)
            loaded = vm_net_mock_npc_shop_inventory_admin_list(
                runtimeScene, actorId, rows, count);
    }
    for (u32 i = 0; i < loaded; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_find_shop_catalog_item(rows[i].itemId);
        if (item != NULL &&
            vm_net_mock_npc_shop_inventory_item_matches_service(item,
                                                                 serviceKind))
        {
            ++serviceCount;
        }
    }
    vm_mock_admin_text_appendf(page,
        "<div class=\"inventory npc-stock-manager\" data-npc-stock-manager data-npc-stock-service=\"%u\" data-npc-stock-key=\"%s-%u\"><div class=\"inventory-head\"><h4>NPC 专属库存（%u）</h4><span class=\"stock-note\">仅显示当前服务类型可售的分类</span></div>",
        serviceKind, pickerPrefix, actorId, serviceCount);
    vm_mock_admin_text_appendf(page,
        "<div class=\"inventory-tools\"><form method=\"post\" action=\"/action\" class=\"inventory-add-form\" data-npc-stock-add-form><input type=\"hidden\" name=\"action\" value=\"save-npc-inventory-bulk\"><input type=\"hidden\" name=\"scene\" value=\"");
    vm_mock_admin_text_append_html(page, sceneUtf8);
    vm_mock_admin_text_appendf(page,
        "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\"><input type=\"hidden\" name=\"service_kind\" value=\"%u\"><input type=\"hidden\" name=\"item_ids\" value=\"\" data-npc-stock-item-ids><span class=\"inventory-form-tag add\">添加商品</span><label class=\"field\"><span>批量单价（铜）</span><input type=\"number\" name=\"unit_price\" min=\"1\" max=\"4294967295\" placeholder=\"留空：使用各商品商城默认价\"></label><label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\" selected>上架</option><option value=\"0\">下架</option></select></label><button type=\"button\" class=\"secondary\" data-npc-stock-open>多选添加商品</button><button type=\"submit\" data-npc-stock-add disabled>加入库存（0）</button></form></div><button type=\"button\" class=\"secondary\" data-npc-stock-current-open aria-haspopup=\"dialog\">管理已有库存（%u）</button><div class=\"item-modal inventory-current-modal\" data-npc-stock-current-modal role=\"dialog\" aria-modal=\"true\" aria-label=\"管理 NPC 已有库存\" hidden><section class=\"item-picker-panel inventory-current-panel\" style=\"width:min(980px,100%%)\"><div class=\"item-picker-head\"><div><h3>管理已有库存</h3><p>筛选、批量移除与单项价格、状态编辑均在此完成。</p></div><button class=\"item-picker-close\" type=\"button\" data-npc-stock-current-close aria-label=\"关闭已有库存\">×</button></div><div class=\"inventory-tools\"><form method=\"post\" action=\"/action\" class=\"inventory-remove-form\" data-npc-stock-remove-form><input type=\"hidden\" name=\"action\" value=\"delete-npc-inventory-bulk\"><input type=\"hidden\" name=\"scene\" value=\"",
        actorId, serviceKind, serviceCount);
    vm_mock_admin_text_append_html(page, sceneUtf8);
    vm_mock_admin_text_appendf(page,
        "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\"><input type=\"hidden\" name=\"service_kind\" value=\"%u\"><input type=\"hidden\" name=\"item_ids\" value=\"\" data-npc-stock-remove-ids><span class=\"inventory-form-tag remove\">管理已有库存</span><label class=\"field\"><span>库存分类</span><select data-npc-stock-current-category>",
        actorId, serviceKind);
    vm_mock_admin_render_npc_stock_category_options(page, serviceKind);
    vm_mock_admin_text_appendf(page, "</select></label>");
    if (vm_mock_admin_npc_stock_service_uses_quality(serviceKind))
    {
        vm_mock_admin_text_appendf(page,
            "<label class=\"field\"><span>装备品质</span><select data-npc-stock-current-quality>");
        vm_mock_admin_render_npc_stock_quality_options(page, serviceKind);
        vm_mock_admin_text_appendf(page, "</select></label>");
    }
    vm_mock_admin_text_appendf(page,
        "<button class=\"secondary\" type=\"button\" data-npc-stock-select-category>全选当前筛选</button><button class=\"danger\" type=\"submit\" data-npc-stock-remove disabled>移除已选（0）</button></form></div>");
    if (count != 0 && rows == NULL)
    {
        vm_mock_admin_text_appendf(page,
            "<p class=\"inventory-error\">库存列表暂时无法分配内存，请刷新后重试。</p>");
    }
    vm_mock_admin_text_appendf(page,
        "<div class=\"inventory-list\" data-npc-stock-current-list style=\"flex:1;min-height:0;overflow:auto;padding:0 20px 20px\">");
    for (u32 i = 0; i < loaded; ++i)
    {
        const vm_net_mock_npc_shop_inventory_row *stock = &rows[i];
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_find_shop_catalog_item(stock->itemId);
        char itemNameUtf8[128];

        if (item == NULL ||
            !vm_net_mock_npc_shop_inventory_item_matches_service(item,
                                                                 serviceKind))
        {
            continue;
        }

        memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
        if (item != NULL)
            vm_net_mock_gbk_label_to_utf8(item->name, itemNameUtf8,
                                          sizeof(itemNameUtf8));
        vm_mock_admin_text_appendf(page,
            "<div class=\"inventory-row npc-stock-row\" data-npc-stock-row data-npc-stock-category=\"%c%u\" data-npc-stock-quality=\"%u\"><label class=\"stock-check\"><input type=\"checkbox\" value=\"%u\" data-npc-stock-current-item><span>选择</span></label><form method=\"post\" action=\"/action\" class=\"inventory-row-form\"><input type=\"hidden\" name=\"action\" value=\"save-npc-inventory\"><input type=\"hidden\" name=\"scene\" value=\"",
            item != NULL && item->isEquip ? 'e' : 'i',
            item != NULL ? item->category : 0u,
            item != NULL && item->isEquip ? item->quality : 0u,
            stock->itemId);
        vm_mock_admin_text_append_html(page, sceneUtf8);
        vm_mock_admin_text_appendf(page,
            "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\"><input type=\"hidden\" name=\"service_kind\" value=\"%u\"><input type=\"hidden\" name=\"item_id\" value=\"%u\"><div><strong>[%u] ",
            actorId, serviceKind, stock->itemId, stock->itemId);
        vm_mock_admin_text_append_html(page,
                                       itemNameUtf8[0] ? itemNameUtf8 : "未知物品");
        vm_mock_admin_text_appendf(page, "</strong>");
        if (item != NULL && item->isEquip)
        {
            vm_mock_admin_text_appendf(
                page, "<small>品质 %u · 需求等级 %u</small>", item->quality,
                vm_mock_admin_item_required_level(item));
        }
        vm_mock_admin_text_appendf(page,
            "</div><label class=\"field\"><span>单价（铜）</span><input type=\"number\" name=\"unit_price\" min=\"1\" max=\"4294967295\" value=\"%u\" placeholder=\"留空：商城默认价\"></label><label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\"%s>上架</option><option value=\"0\"%s>下架</option></select></label><div class=\"actions\"><button type=\"submit\">保存</button></div></form><form method=\"post\" action=\"/action\" class=\"actions\"><input type=\"hidden\" name=\"action\" value=\"delete-npc-inventory\"><input type=\"hidden\" name=\"scene\" value=\"",
            stock->unitPrice, stock->enabled ? " selected" : "",
            stock->enabled ? "" : " selected");
        vm_mock_admin_text_append_html(page, sceneUtf8);
        vm_mock_admin_text_appendf(page,
            "\"><input type=\"hidden\" name=\"actor_id\" value=\"%u\"><input type=\"hidden\" name=\"service_kind\" value=\"%u\"><input type=\"hidden\" name=\"item_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">移除</button></form></div>",
            actorId, serviceKind, stock->itemId);
    }
    vm_mock_admin_text_appendf(page, "</div></section></div></div>");
    free(rows);
}

static void vm_mock_admin_render_npc_inventories(
    vm_mock_admin_text *page, const char *sceneUtf8, const char *runtimeScene,
    const vm_net_mock_scene_npcinfo_seed *seed, const char *pickerPrefix)
{
    vm_net_mock_npc_service_option
        options[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
    u32 optionCount = 0;

    if (page == NULL || sceneUtf8 == NULL || runtimeScene == NULL ||
        seed == NULL || seed->actorId == 0 || pickerPrefix == NULL)
    {
        return;
    }
    memset(options, 0, sizeof(options));
    if (!vm_net_mock_npc_service_options_resolve(
            runtimeScene, seed->actorId, seed->kind,
            seed->serviceOptionName, seed->serviceOptionDescription, options,
            VM_NET_MOCK_NPC_SERVICE_OPTION_MAX, &optionCount, NULL))
    {
        return;
    }
    for (u32 i = 0; i < optionCount; ++i)
    {
        char panelPrefix[64];

        if (!vm_net_mock_npc_service_kind_uses_inventory(options[i].kind))
            continue;
        snprintf(panelPrefix, sizeof(panelPrefix), "%s-%u", pickerPrefix,
                 options[i].kind);
        vm_mock_admin_render_npc_inventory(
            page, sceneUtf8, runtimeScene, seed->actorId, options[i].kind,
            panelPrefix);
    }
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

static void vm_mock_admin_render_item_picker_modal(
    vm_mock_admin_text *page, bool includeEquipmentFilters)
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
        vm_mock_admin_render_item_picker_catalog_option(page, item, false);
    }
    vm_mock_admin_text_appendf(
        page,
        "</select><div class=\"item-modal\" id=\"item-picker-modal\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"item-picker-title\" hidden>"
        "<section class=\"item-picker-panel\"><div class=\"item-picker-head\"><div><h3 id=\"item-picker-title\">选择物品</h3>"
        "<p>按分类、名称或物品 ID 快速查找</p></div><div class=\"item-picker-head-actions\"><button id=\"item-picker-clear\" type=\"button\">清空选择</button><button class=\"item-picker-close\" id=\"item-picker-close\" type=\"button\" aria-label=\"关闭物品选择\">×</button></div></div>"
        "<div class=\"item-picker-tools%s\"><label><span>物品分类</span><select id=\"item-category\"><option value=\"all\">全部分类</option>",
        includeEquipmentFilters ? " item-picker-tools-equipment" : "");
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
    vm_mock_admin_text_appendf(page, "</select></label>");
    if (includeEquipmentFilters)
    {
        vm_mock_admin_text_appendf(
            page,
            "<label data-item-quality-field><span>装备品质</span><select id=\"item-quality\"></select></label>"
            "<label data-item-level-field><span>装备等级</span><select id=\"item-level\"></select></label>");
    }
    vm_mock_admin_text_appendf(
        page,
        "<label><span>搜索</span><input id=\"item-search\" type=\"search\" placeholder=\"输入名称或物品 ID\" autocomplete=\"off\"></label></div>"
        "<div class=\"item-result-bar\"><span id=\"item-result-count\"></span><span class=\"item-picker-error\" id=\"item-picker-error\"></span></div>"
        "<div class=\"item-picker-list\" id=\"item-picker-list\"></div><p class=\"item-picker-empty\" id=\"item-picker-empty\" hidden>没有符合条件的物品</p>"
        "</section></div>");
}

/* This picker is intentionally separate from the single-value picker above:
 * it stages an item-id set for one merchant, and the server validates the
 * service/category relation again before writing it. */
static void vm_mock_admin_render_npc_stock_picker_modal(vm_mock_admin_text *page)
{
    u32 itemCount = vm_net_mock_load_shop_catalog();

    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(page,
        "<select id=\"npc-stock-picker-options\" hidden>");
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        vm_mock_admin_render_item_picker_catalog_option(page, item, true);
    }
    vm_mock_admin_text_appendf(page,
        "</select><div class=\"item-modal npc-stock-modal\" id=\"npc-stock-picker-modal\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"npc-stock-picker-title\" hidden><section class=\"item-picker-panel\"><div class=\"item-picker-head\"><div><h3 id=\"npc-stock-picker-title\">批量选择 NPC 商品</h3><p>仅显示该 NPC 服务类型可售的物品。留空单价时按每个商品的商城默认价保存。</p></div><div class=\"item-picker-head-actions\"><button id=\"npc-stock-picker-clear\" type=\"button\">清空本次选择</button><button class=\"item-picker-close\" id=\"npc-stock-picker-close\" type=\"button\" aria-label=\"关闭商品选择\">×</button></div></div>"
        "<div class=\"item-picker-tools\"><label><span>物品分类</span><select id=\"npc-stock-category\"></select></label><label data-npc-stock-quality-field><span>装备品质</span><select id=\"npc-stock-quality\"></select></label><label><span>搜索</span><input id=\"npc-stock-search\" type=\"search\" placeholder=\"输入名称或物品 ID\" autocomplete=\"off\"></label></div>"
        "<div class=\"item-result-bar\"><span id=\"npc-stock-result-count\"></span><span id=\"npc-stock-selection-count\"></span></div><div class=\"npc-stock-picker-actions\"><button class=\"secondary\" type=\"button\" id=\"npc-stock-select-category\">全选当前筛选</button><button class=\"secondary\" type=\"button\" id=\"npc-stock-unselect-category\">移除当前筛选选择</button><button type=\"button\" id=\"npc-stock-picker-confirm\">确认已选商品</button></div>"
        "<div class=\"item-picker-list\" id=\"npc-stock-picker-list\"></div><p class=\"item-picker-empty\" id=\"npc-stock-picker-empty\" hidden>当前分类没有可添加商品</p></section></div>");
}

/* Monster drops keep an independent probability per item, so this is not an
 * NPC inventory POST clone.  The modal only stages a catalog item set; the
 * monster editor assigns that set to currently empty drop slots and the
 * existing save-monster transaction persists the complete ordered list. */
static void vm_mock_admin_render_monster_drop_picker_modal(
    vm_mock_admin_text *page)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"item-modal monster-drop-modal\" id=\"monster-drop-picker-modal\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"monster-drop-picker-title\" hidden><section class=\"item-picker-panel\"><div class=\"item-picker-head\"><div><h3 id=\"monster-drop-picker-title\">批量选择怪物掉落</h3><p>选择后再加入空掉落槽位；每个物品仍按自身的独立概率投掷。</p></div><div class=\"item-picker-head-actions\"><button id=\"monster-drop-picker-clear\" type=\"button\">清空本次选择</button><button class=\"item-picker-close\" id=\"monster-drop-picker-close\" type=\"button\" aria-label=\"关闭掉落选择\">×</button></div></div>"
        "<div class=\"item-picker-tools\"><label><span>物品分类</span><select id=\"monster-drop-category\">");
    vm_mock_admin_render_catalog_category_options(page, "全部掉落分类");
    vm_mock_admin_text_appendf(
        page,
        "</select></label><label data-monster-drop-quality-field><span>装备品质</span><select id=\"monster-drop-quality\">");
    vm_mock_admin_render_catalog_quality_options(page, "全部品质");
    vm_mock_admin_text_appendf(
        page,
        "</select></label><label data-monster-drop-level-field><span>装备等级</span><select id=\"monster-drop-level\"><option value=\"all\">全部等级</option></select></label><label><span>搜索</span><input id=\"monster-drop-search\" type=\"search\" placeholder=\"输入名称或物品 ID\" autocomplete=\"off\"></label></div>"
        "<div class=\"item-result-bar\"><span id=\"monster-drop-result-count\"></span><span id=\"monster-drop-selection-count\"></span><span class=\"item-picker-error\" id=\"monster-drop-picker-error\"></span></div><div class=\"npc-stock-picker-actions\"><button class=\"secondary\" type=\"button\" id=\"monster-drop-select-category\">全选当前筛选</button><button class=\"secondary\" type=\"button\" id=\"monster-drop-unselect-category\">移除当前筛选选择</button><button type=\"button\" id=\"monster-drop-picker-confirm\">确认已选物品</button></div>"
        "<div class=\"item-picker-list\" id=\"monster-drop-picker-list\"></div><p class=\"item-picker-empty\" id=\"monster-drop-picker-empty\" hidden>当前筛选没有可添加物品</p></section></div>");
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
        "<label><span>数量</span><input type=\"number\" name=\"amount\" min=\"1\" max=\"255\" value=\"1\" required></label>"
        "<button type=\"submit\">给予物品</button></form>"
        "<p class=\"muted grant-note\">相同物品会叠加；新物品需要背包存在空位。装备也遵循现有背包存储规则。</p></div>");
    vm_mock_admin_render_item_picker_modal(page, false);
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
    return vm_net_mock_shop_item_is_secret_treasure(item);
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

static void vm_mock_admin_render_shop_section_select(
    vm_mock_admin_text *page, const vm_net_mock_shop_catalog_item *item)
{
    if (page == NULL || item == NULL)
        return;
    if (item->isEquip)
    {
        vm_mock_admin_text_appendf(
            page,
            "<input type=\"hidden\" name=\"shop_section\" value=\"0\">"
            "<span class=\"shop-section-fixed\">装备位固定</span>");
        return;
    }
    vm_mock_admin_text_appendf(
        page,
        "<select aria-label=\"商城分区\" name=\"shop_section\">"
        "<option value=\"0\"%s>自动（原始分类）</option>",
        item->shopSection == VM_NET_MOCK_SHOP_SECTION_AUTO ?
            " selected" : "");
    if (!item->isEquip)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"1\"%s>秘宝道具</option>",
            item->shopSection == VM_NET_MOCK_SHOP_SECTION_SECRET ?
                " selected" : "");
    }
    vm_mock_admin_text_appendf(
        page, "<option value=\"2\"%s>普通商品</option></select>",
        item->shopSection == VM_NET_MOCK_SHOP_SECTION_NORMAL ?
            " selected" : "");
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
        ".card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.shop-card{display:flex;flex-direction:column;min-height:0;flex:1}.summary{display:flex;gap:9px;flex-wrap:wrap;margin-bottom:12px}.badge{padding:3px 8px;border-radius:999px;background:#eef4ff;color:#175cd3}.badge.secret{background:#fff4e5;color:#b54708}.badge.arms{background:#f4f3ff;color:#5925dc}.badge.off{background:#fef3f2;color:#b42318}.filters{display:grid;grid-template-columns:minmax(190px,.7fr) minmax(220px,1.2fr) auto;gap:9px;align-items:end;margin-bottom:12px}.filters label{display:grid;gap:4px}.filters span{font-size:12px;color:#667085}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.shop-list{min-height:0;flex:1;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;border:1px solid #eaecf0;border-radius:8px}table{border-collapse:collapse;width:100%%}th,td{text-align:left;padding:10px 9px;border-bottom:1px solid #eaecf0;vertical-align:middle}th{position:sticky;top:0;background:#f9fafb;color:#667085;z-index:1}.name{min-width:200px}.section{display:inline-block;padding:2px 7px;border-radius:999px;background:#f2f4f7;color:#475467;font-size:12px;white-space:nowrap}.section.secret{background:#fff4e5;color:#b54708}.section.arms{background:#f4f3ff;color:#5925dc}.row-form{display:grid;grid-template-columns:130px 100px 150px auto;gap:7px;align-items:center}.shop-section-fixed{padding:8px 9px;color:#667085;font-size:12px;white-space:nowrap}.state{font-size:12px;font-weight:600}.state.on{color:#027a48}.state.off{color:#b42318}.pages{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:12px}.page-links{display:flex;gap:7px}.page-links a{padding:6px 10px;border:1px solid #d0d5dd;border-radius:6px;color:#344054;text-decoration:none}.notice{padding:10px 12px;border-radius:7px;margin-bottom:12px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.foot{font-size:12px;color:#667085;margin:10px 0 0}"
        "@media(max-width:760px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:16px 9px;overflow:visible}.shop-card{min-height:700px}.filters,.row-form{grid-template-columns:1fr}.shop-list{min-height:520px}.tabs{overflow:auto}.name{min-width:150px}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">商品价格、上下架与商城分区 · 保存后立即生效</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a>"
        "<a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a>"
        "<a class=\"tab on\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "<a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>"
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
            "<select aria-label=\"上下架状态\" name=\"enabled\"><option value=\"1\"%s>上架</option><option value=\"0\"%s>下架</option></select>",
            pageNumber, item->price, item->enabled ? " selected" : "",
            item->enabled ? "" : " selected");
        vm_mock_admin_render_shop_section_select(&page, item);
        vm_mock_admin_text_appendf(
            &page, "<button type=\"submit\">保存</button></form>%s</td></tr>",
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
        "</div></div><p class=\"foot\">价格、上下架状态和商城分区覆盖保存在 MySQL server_shop_items 表。物品 DSH 分类不会被改写：非装备可指定“秘宝道具”或从默认秘宝页移至普通商品；装备始终按穿戴部位进入对应的神兵页。17/1 NPC 商店的显示价格仍由客户端本地 DSH 决定。</p></section></main></body></html>");
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
    u32 managedFileCount = 0;
    char status[16];
    char message[256];
    char section[32];
    char catalogPath[1200];
    u8 contentPayload[VM_NET_MOCK_CONTENT_UPDATE_PAYLOAD_MAX];
    u32 contentPayloadLen = 0;
    u32 contentChecksum = 0;
    bool showContent = true;
    bool showModules = false;
    bool showConfiguration = false;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(section, 0, sizeof(section));
    memset(catalogPath, 0, sizeof(catalogPath));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_admin_form_value(query, "section", section, sizeof(section));
    if (strcmp(section, "modules") == 0)
    {
        showContent = false;
        showModules = true;
    }
    else if (strcmp(section, "configuration") == 0)
    {
        showContent = false;
        showConfiguration = true;
    }
    vm_net_mock_update_catalog_load();
    if (showModules)
        vm_net_mock_update_delivery_load();
    if (showContent)
        vm_net_mock_content_update_load();
    (void)vm_net_mock_update_state_path("server_update_catalog.tsv",
                                        catalogPath, sizeof(catalogPath));
    if (showContent && g_vm_net_mock_content_update.enabled &&
        g_vm_net_mock_content_update.id != 0)
    {
        contentPayloadLen = vm_net_mock_content_update_build_payload(
            &g_vm_net_mock_content_update, contentPayload,
            sizeof(contentPayload), &contentChecksum);
    }
    if (showContent)
    {
        files = (vm_mock_admin_scene_file *)calloc(
            VM_MOCK_ADMIN_UPDATE_FILE_MAX, sizeof(*files));
        if (files != NULL)
            fileCount = vm_mock_admin_collect_update_files(
                files, VM_MOCK_ADMIN_UPDATE_FILE_MAX);
        for (u32 i = 0; files != NULL && i < fileCount; ++i)
        {
            if (vm_net_mock_content_update_name_is_managed_resource(
                    files[i].name))
            {
                ++managedFileCount;
            }
        }
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 游戏内容更新管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1280px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 12px}h3{font-size:15px;margin:0 0 8px}.sub,.muted{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;gap:6px;margin:0 0 14px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}"
        ".update-grid{display:grid;grid-template-columns:230px minmax(0,1fr);gap:16px;flex:1;min-height:0}.pane{min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.update-menu{min-height:0;overflow:auto;overscroll-behavior:contain}.update-menu h2{margin-bottom:10px}.update-menu-list{display:grid;gap:5px}.update-option{display:block;padding:10px 11px;border:1px solid transparent;border-radius:7px;color:#344054;text-decoration:none}.update-option:hover,.update-option.on{background:#eef4ff;border-color:#c7d7fe;color:#175cd3}.update-option strong,.update-option small{display:block}.update-option small{margin-top:2px;color:#667085;font-size:12px}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d;margin-bottom:12px}.module{border:1px solid #eaecf0;border-radius:8px;padding:12px;margin-top:9px}.module-head{display:flex;justify-content:space-between;gap:10px}.badge{font-size:12px;padding:2px 7px;border-radius:999px;background:#f2f4f7;color:#475467}.badge.on{background:#ecfdf3;color:#027a48}.module form{display:grid;grid-template-columns:100px 1fr auto;gap:8px;align-items:end;margin-top:9px}.publish{display:grid;grid-template-columns:minmax(110px,.7fr) minmax(220px,1.6fr) 90px auto;gap:8px;align-items:end;margin-top:9px}.module label,.publish label{display:grid;gap:4px}.module label span,.publish label span{font-size:12px;color:#667085}.check{display:flex!important;align-items:center;gap:7px;height:36px}.check input{width:auto}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}button:hover{background:#1849a9}.danger{background:#b42318}.notice{padding:10px 12px;border-radius:7px;margin-bottom:12px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.callout{background:#eef4ff;color:#3538cd;border-radius:8px;padding:11px 12px}.published{width:100%%;border-collapse:collapse;margin-top:10px}.published th,.published td{text-align:left;padding:9px 7px;border-bottom:1px solid #eaecf0;vertical-align:middle}.published th{font-size:12px;color:#667085}.inline{display:flex;gap:7px;align-items:center}.foot{font-size:12px;color:#667085}.path{word-break:break-all;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px}"
        ".scene-content-form{display:grid;grid-template-columns:minmax(220px,.7fr) minmax(300px,1.3fr) auto;gap:8px;align-items:end;margin-top:12px}.scene-content-form label{display:grid;gap:4px}.scene-content-form label span{font-size:12px;color:#667085}.content-update-select{min-height:278px;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px}.picker-actions{display:flex;gap:7px;margin-top:7px}.picker-actions button{background:#475467;font-size:12px}.content-actions{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:10px}.modal-open{overflow:hidden}[hidden]{display:none!important}@media(max-width:850px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;overflow:visible}.update-grid{grid-template-columns:1fr;flex:none}.update-menu,.pane{overflow:visible}.module form,.scene-content-form{grid-template-columns:1fr}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">启动模块发布与 MySQL 游戏数据内容版本管理</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a>"
        "<a class=\"tab on\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "<a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>");
    vm_mock_admin_text_appendf(&page,
        "<div class=\"update-grid\"><aside class=\"card update-menu\"><h2>更新选项</h2>"
        "<div class=\"update-menu-list\" data-admin-list>"
        "<a class=\"update-option%s\" data-admin-select%s href=\"/?tab=updates&amp;section=content\"><strong>游戏数据内容更新</strong><small>资源失效清单与内容版本</small></a>"
        "<a class=\"update-option%s\" data-admin-select%s href=\"/?tab=updates&amp;section=modules\"><strong>启动模块更新</strong><small>CBM 槽位发布与下发记录</small></a>"
        "<a class=\"update-option%s\" data-admin-select%s href=\"/?tab=updates&amp;section=configuration\"><strong>启动模块配置</strong><small>服务端配置来源与协议边界</small></a>"
        "</div></aside><section class=\"pane update-detail\" data-admin-detail>",
        showContent ? " on" : "", showContent ? " aria-current=\"page\"" : "",
        showModules ? " on" : "", showModules ? " aria-current=\"page\"" : "",
        showConfiguration ? " on" : "", showConfiguration ? " aria-current=\"page\"" : "");
    if (message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (showModules)
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"card\"><h2>启动模块更新</h2>"
            "<div class=\"callout\">客户端启动发送 WT 18/9；服务器按启用槽位返回 result 位图，客户端再以 WT 18/6 分块下载。替换 CBM 后必须修改版本号。</div>");
        for (u32 i = 0; i < VM_NET_MOCK_UPDATE_SLOT_COUNT; ++i)
        {
            const vm_net_mock_update_slot_config *slot =
                &g_vm_net_mock_update_slots[i];
            long size = vm_net_mock_update_file_size(
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
            "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"reset-update-delivery\"><button class=\"danger\" type=\"submit\">清空记录并让已发布模块重新下发</button></form></div>",
            g_vm_net_mock_update_delivery_count);
    }
    if (showContent)
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"card\"><h2>游戏数据内容更新</h2>"
            "<p class=\"muted\">除 CBM 模块外的游戏数据会先由启动期 WT 18/9 → 18/8 声明为失效，再按现有 WT 18/7 下载实际字节。客户端只在发布 ID 或校验和与本地记录不一致时删除清单中的缓存；同一版本重启不会重复删除。部署场景战斗怪会自动创建新内容版本。</p>"
            "<div class=\"summary\"><span class=\"badge %s\">%s</span><span class=\"badge\">发布 ID %u</span><span class=\"badge\">校验和 %u</span><span class=\"badge\">%u 个资源</span></div>"
            "<table class=\"published\"><thead><tr><th>游戏数据资源</th><th>安装方式</th></tr></thead><tbody>",
        contentPayloadLen != 0 && contentChecksum == g_vm_net_mock_content_update.code ? "on" : "",
        contentPayloadLen != 0 && contentChecksum == g_vm_net_mock_content_update.code ? "已发布，重启后安装" : "尚无有效场景内容版本",
        contentPayloadLen != 0 ? g_vm_net_mock_content_update.id : 0,
        contentPayloadLen != 0 ? g_vm_net_mock_content_update.code : 0,
        contentPayloadLen != 0 ? g_vm_net_mock_content_update.nameCount : 0);
    for (u32 i = 0; contentPayloadLen != 0 &&
                    i < g_vm_net_mock_content_update.nameCount; ++i)
    {
        char resourceUtf8[256];

        memset(resourceUtf8, 0, sizeof(resourceUtf8));
        vm_net_mock_gbk_label_to_utf8(g_vm_net_mock_content_update.names[i],
                                      resourceUtf8, sizeof(resourceUtf8));
        vm_mock_admin_text_appendf(&page, "<tr><td class=\"path\">");
        vm_mock_admin_text_append_html(&page, resourceUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "</td><td><form class=\"inline\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"remove-content-update-file\"><input type=\"hidden\" name=\"resource\" value=\"");
        vm_mock_admin_text_append_html(&page, resourceUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"><button class=\"danger\" type=\"submit\">移除</button></form></td></tr>");
    }
    if (contentPayloadLen == 0)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td colspan=\"2\" class=\"muted\">尚未发布游戏数据内容。可在下方多选资源加入，或在部署场景战斗怪后自动生成版本。</td></tr>");
    }
    vm_mock_admin_text_appendf(&page,
        "</tbody></table><form class=\"scene-content-form\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"add-content-update-files\"><label><span>按文件名筛选</span><input type=\"search\" data-content-update-search placeholder=\"输入 actor、.map、场景名称等筛选\"></label><label><span>多选服务器游戏数据（%u，CBM 已排除）</span><select class=\"content-update-select\" name=\"resource\" data-content-update-select multiple size=\"14\" required>",
        managedFileCount);
    for (u32 i = 0; files != NULL && i < fileCount; ++i)
    {
        char resourceUtf8[256];

        if (!vm_net_mock_content_update_name_is_managed_resource(files[i].name))
            continue;
        memset(resourceUtf8, 0, sizeof(resourceUtf8));
        vm_mock_admin_resource_name_to_utf8(files[i].name, resourceUtf8,
                                            sizeof(resourceUtf8));
        vm_mock_admin_text_appendf(&page, "<option value=\"");
        vm_mock_admin_text_append_html(&page, resourceUtf8);
        vm_mock_admin_text_appendf(&page, "\">");
        vm_mock_admin_text_append_html(&page, resourceUtf8);
        vm_mock_admin_text_appendf(&page, "</option>");
    }
    vm_mock_admin_text_appendf(&page,
        "</select><div class=\"picker-actions\"><button type=\"button\" data-content-update-select-filtered>全选当前筛选</button><button type=\"button\" data-content-update-clear-selection>清空选择</button></div></label><button type=\"submit\">加入已选资源</button></form><div class=\"content-actions\"><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"republish-content-update\"><button type=\"submit\">检查当前清单</button></form><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"clear-content-update\"><button class=\"danger\" type=\"submit\">清空内容更新</button></form><span class=\"foot\">仅新增、移除或资源字节变化才会创建内容版本；相同版本的客户端不会下载。</span></div>"
        "<p class=\"foot\">游戏数据版本和文件列表保存在 MySQL 的 <span class=\"path\">server_content_update_releases</span> / <span class=\"path\">server_content_update_files</span>。资源清单排除 CBM 和服务端状态文件；选择过多资源会令客户端首次处理清单更久。</p></div>");
    }
    if (showConfiguration)
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"card\"><h2>启动模块配置</h2>"
            "<p class=\"muted\">启动模块（CBM）与游戏数据内容更新使用不同的原生协议：前者由槽位配置决定 WT 18/5 → 18/6 下发，后者由 MySQL 内容版本决定 WT 18/9 → 18/8 → 18/7 失效与重取。</p>"
            "<div class=\"module\"><h3>模块发布配置</h3><div class=\"path\">");
        vm_mock_admin_text_append_html(&page,
            catalogPath[0] ? catalogPath : "资源根目录未配置");
        vm_mock_admin_text_appendf(&page,
            "</div><p class=\"foot\">每个槽位的启用状态与版本号保存在 <span class=\"path\">server_update_catalog.tsv</span>；客户端下发完成记录保存在 <span class=\"path\">server_update_delivery.tsv</span>。</p></div>"
            "<div class=\"module\"><h3>内容更新配置</h3><p class=\"foot\">游戏数据 release、资源名称及发布时字节校验保存在 MySQL：<span class=\"path\">server_content_update_releases</span> / <span class=\"path\">server_content_update_files</span>。相同字节不会生成新版本。</p></div></div>");
    }
    vm_mock_admin_text_appendf(&page,
        "</section></div></main></body></html>");
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

static void vm_mock_admin_render_task_reward_rows(
    vm_mock_admin_text *page, const vm_net_mock_task_definition *task)
{
    u8 visibleCount = 1;

    if (page == NULL || task == NULL)
        return;
    if (task->rewardItemNum != 0)
        visibleCount = task->rewardItemNum;
    vm_mock_admin_text_appendf(page,
        "<div id=\"task-reward-list\" class=\"task-reward-list\">");
    for (u8 slot = 0; slot < VM_NET_MOCK_TASK_REWARD_ITEM_MAX; ++slot)
    {
        char pickerId[48];
        char fieldName[48];
        char label[64];
        u32 itemId = slot < task->rewardItemNum
                         ? task->rewardItems[slot].itemId
                         : 0;
        u32 itemCount = slot < task->rewardItemNum
                            ? task->rewardItems[slot].count
                            : 0;
        u8 itemType = slot < task->rewardItemNum
                          ? task->rewardItems[slot].itemType
                          : 0;

        snprintf(pickerId, sizeof(pickerId), "task-reward-item-%u", slot);
        snprintf(fieldName, sizeof(fieldName), "reward_item_id_%u", slot);
        snprintf(label, sizeof(label), "奖励物品 %u", (u32)slot + 1);
        vm_mock_admin_text_appendf(
            page, "<div class=\"task-reward-row\" data-task-reward-row%s>",
            slot < visibleCount ? "" : " hidden");
        vm_mock_admin_render_item_picker_field(page, pickerId, fieldName,
                                               label, itemId, false);
        vm_mock_admin_text_appendf(
            page,
            "<label class=\"field\"><span>数量</span><input type=\"number\" name=\"reward_item_count_%u\" min=\"0\" max=\"4294967295\" value=\"%u\" data-task-reward-count></label>"
            "<label class=\"field\"><span>类型</span><input type=\"number\" name=\"reward_item_type_%u\" min=\"0\" max=\"255\" value=\"%u\" data-task-reward-type></label>"
            "<button class=\"secondary task-reward-remove\" type=\"button\" data-task-reward-remove>移除</button></div>",
            slot, itemCount, slot, itemType);
    }
    vm_mock_admin_text_appendf(
        page,
        "</div><div class=\"actions task-reward-actions\"><button id=\"task-reward-add\" class=\"secondary\" type=\"button\">＋ 添加奖励物品</button></div>");
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
        /* A lone dash is the legacy "no NPC dialog override" placeholder.
         * Keep it out of the editable value so an untouched task clearly
         * represents the empty/default-dialog state. */
        if (strcmp(offerUtf8, "-") == 0)
            offerUtf8[0] = 0;
        if (strcmp(activeUtf8, "-") == 0)
            activeUtf8[0] = 0;
        if (strcmp(completedUtf8, "-") == 0)
            completedUtf8[0] = 0;
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>江湖OL 任务管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1280px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column}.head{display:flex;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 12px}.sub,.hint{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff}.logout{background:#fff!important;color:#667085!important;border:1px solid #d0d5dd!important}.grid{display:grid;grid-template-columns:280px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px}.task-catalog{display:flex;flex-direction:column;min-height:0;overflow:hidden}.task-catalog-head{flex:none}.task-catalog-head .button{display:block;margin-bottom:14px}.task-list{display:flex;flex:1;min-height:0;flex-direction:column;gap:4px;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable}.task{padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none}.task:hover,.task.on{background:#eef4ff;color:#175cd3}.task.off{opacity:.55}.editor{overflow:auto}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.fields{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}input,select,textarea{width:100%%;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}textarea{min-height:68px;resize:vertical}.wide{grid-column:1/-1}.group{margin-top:14px;padding:12px;border:1px solid #e4e7ec;border-radius:8px}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}button,.button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;text-decoration:none}.danger{background:#b42318}.secondary{background:#475467}.badge{font-size:12px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#175cd3}"
        ".item-field{display:grid;gap:4px}.item-field>span{font-size:12px;color:#667085}button.item-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px;white-space:normal}.item-picker-trigger small{color:#667085;font-weight:400}.item-picker-trigger.compact{min-height:32px;font-size:12px}.item-picker-head-actions{display:flex;gap:8px;align-items:center}.item-picker-head-actions #item-picker-clear{background:#f2f4f7;color:#475467}.item-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899}.item-picker-panel{width:min(780px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.item-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.item-picker-head h3{font-size:19px;margin:0}.item-picker-head p{margin:2px 0 0;color:#667085}.item-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.item-picker-tools{display:grid;grid-template-columns:minmax(200px,.8fr) minmax(260px,1.2fr);gap:10px;padding:14px 20px 10px}.item-picker-tools label{display:grid;gap:4px}.item-picker-tools label>span{font-size:12px;color:#667085}.item-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.item-picker-error{color:#b42318;font-weight:600}.item-picker-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:140px;overflow:auto;padding:0 20px 20px}.item-choice{display:grid;gap:2px;padding:10px 12px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.item-choice:hover{border-color:#84adff;background:#f5f8ff}.item-choice strong{font-size:14px}.item-choice span{color:#667085;font-size:12px}.item-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}.task-reward-list{display:grid;gap:9px}.task-reward-row{display:grid;grid-template-columns:minmax(220px,1.7fr) 120px 100px auto;gap:10px;align-items:end;padding:10px;border:1px solid #eaecf0;border-radius:7px;background:#f9fafb}.task-reward-actions{justify-content:flex-start;margin-top:10px}[hidden]{display:none!important}.modal-open{overflow:hidden}@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto}.grid{grid-template-columns:1fr}.list{max-height:300px}.fields,.task-reward-row{grid-template-columns:1fr 1fr}.item-picker-tools,.item-picker-list{grid-template-columns:1fr}}</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\">"
        "<div class=\"head\"><div><h1>江湖OL 后台管理</h1><p class=\"sub\">任务定义、奖励与 NPC 对话</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\">退出登录</button></form></div>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab on\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a><a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a><a class=\"tab\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav><style>@media(max-width:900px){.task-catalog{max-height:300px}.task-list{max-height:none}}</style>"
        "<div class=\"grid\"><aside class=\"card task-catalog\" data-task-catalog><div class=\"task-catalog-head\"><a class=\"button%s\" data-admin-select%s href=\"/?tab=tasks&amp;new=1\">＋ 新增任务</a><h2>任务目录（%u）</h2></div><div class=\"task-list\" data-admin-list>",
        createNew ? " on" : "",
        createNew ? " aria-current=\"page\"" : "",
        taskCount);
    for (u32 i = 0; i < taskCount; ++i)
    {
        char listNameUtf8[128];
        memset(listNameUtf8, 0, sizeof(listNameUtf8));
        vm_net_mock_gbk_label_to_utf8(tasks[i].name, listNameUtf8,
                                      sizeof(listNameUtf8));
        vm_mock_admin_text_appendf(
            &page, "<a class=\"task%s%s\" data-admin-select%s href=\"/?tab=tasks&amp;task=%u\"><strong>%u</strong> · ",
            (!createNew && tasks[i].taskId == selectedTaskId) ? " on" : "",
            tasks[i].enabled ? "" : " off",
            (!createNew && tasks[i].taskId == selectedTaskId) ? " aria-current=\"page\"" : "",
            tasks[i].taskId, tasks[i].taskId);
        vm_mock_admin_text_append_html(&page, listNameUtf8);
        vm_mock_admin_text_appendf(&page, "%s</a>",
                                   tasks[i].overridden ? " · 已编辑" : "");
    }
    vm_mock_admin_text_appendf(&page, "</aside><section class=\"card editor\" data-admin-detail><div data-task-action-status>");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page, "</div>");
    if (!found)
    {
        vm_mock_admin_text_appendf(&page, "<p>没有可编辑的任务。</p></section></div></main></body></html>");
        return;
    }
    vm_mock_admin_text_appendf(&page, "<h2>%s <span class=\"badge\">%s</span></h2><form method=\"post\" action=\"/action\" data-task-action><input type=\"hidden\" name=\"action\" value=\"save-task\">%s<div class=\"fields\">",
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
    vm_mock_admin_text_appendf(&page, "<div class=\"group\"><h2>给予物品与奖励</h2><div class=\"fields\">");
    vm_mock_admin_render_item_picker_field(&page, "task-given-item", "given_item_id", "接取给予物品", edit.givenItemId, false);
    vm_mock_admin_text_appendf(&page, "<label class=\"field\"><span>给予数量</span><input type=\"number\" name=\"given_item_count\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励经验</span><input type=\"number\" name=\"reward_exp\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励铜钱</span><input type=\"number\" name=\"reward_money\" min=\"0\" max=\"4294967295\" value=\"%u\"></label><label class=\"field\"><span>奖励说明（最多31字节）</span><input name=\"reward_text\" maxlength=\"31\" value=\"", edit.givenItemCount, edit.rewardExp, edit.rewardMoney);
    vm_mock_admin_text_append_html(&page, rewardUtf8);
    vm_mock_admin_text_appendf(&page, "\"></label></div><h2 style=\"margin-top:16px\">奖励物品</h2><p class=\"hint\">可配置最多 %u 项不同物品；提交任务时会按客户端原生 awardinfo 多行流一次性入包。装备、神仙壶和逍遥壶每项数量必须为 1。</p>", VM_NET_MOCK_TASK_REWARD_ITEM_MAX);
    vm_mock_admin_render_task_reward_rows(&page, &edit);
    vm_mock_admin_text_appendf(&page, "</div>");
    vm_mock_admin_text_appendf(&page, "<div class=\"group\"><h2>NPC 对话</h2><p class=\"hint\">NPC 绑定该任务后按未接、进行中、可提交三种状态显示；留空时使用服务端安全默认文案。</p><div class=\"fields\"><label class=\"field wide\"><span>可接取时</span><textarea name=\"offer_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, offerUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label><label class=\"field wide\"><span>进行中</span><textarea name=\"active_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, activeUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label><label class=\"field wide\"><span>可提交时</span><textarea name=\"completed_dialog\" maxlength=\"255\">"); vm_mock_admin_text_append_html(&page, completedUtf8); vm_mock_admin_text_appendf(&page, "</textarea></label></div></div><p class=\"hint\">条件类型 1 为收集物品、2 为击败怪物；只有“收集物品”条件应使用目录选择器。两项都为 0 时，接取后再次与交付 NPC 对话即可完成。名称长度按客户端 GBK 字节槽校验。</p><div class=\"actions\"><button type=\"submit\">保存任务</button></div></form>");
    vm_mock_admin_render_item_picker_modal(&page, false);
    if (!createNew && edit.overridden)
    {
        vm_mock_admin_text_appendf(&page, "<form class=\"actions\" method=\"post\" action=\"/action\" data-task-action><input type=\"hidden\" name=\"action\" value=\"delete-task-override\"><input type=\"hidden\" name=\"task_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">%s</button></form>", edit.taskId, edit.builtin ? "恢复 task.dsh 默认" : "删除自定义任务");
    }
    vm_mock_admin_text_appendf(&page, "</section></div></main></body></html>");
    if (page.truncated)
        snprintf(response, responseCap, "<!doctype html><meta charset=\"utf-8\"><p>任务管理页面超过大小限制。</p>");
}

#include "web_admin_monsters.inc.c"
static bool vm_mock_admin_form_u32(const char *body, const char *field,
                                   u32 maximum, u32 *valueOut);
static bool vm_mock_admin_scene_from_form(const char *body,
                                          char *sceneUtf8,
                                          size_t sceneUtf8Cap,
                                          char *runtimeScene,
                                          size_t runtimeSceneCap);
#include "web_admin_chests.inc.c"

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
        "</head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1><p class=\"sub\">标题服务器列表 · 保存后影响下一次登录</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a><a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a><a class=\"tab on\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>"
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

/* Single-column COUNT(*) row shared by the account directory and the risk
 * audit pager. */
typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_mock_admin_count;

static bool vm_mock_admin_count_row(void *contextValue,
                                    unsigned int columnCount,
                                    const char *const *values,
                                    const size_t *lengths)
{
    vm_mock_admin_count *count =
        (vm_mock_admin_count *)contextValue;

    if (count == NULL || count->found || columnCount != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &count->value))
    {
        if (count != NULL)
            count->invalid = true;
        return true;
    }
    count->found = true;
    return true;
}

/* The rapid-entry table only contains events whose adjacent battle starts
 * were inside VM_NET_MOCK_RAPID_BATTLE_ENTRY_WINDOW_MS.  This admin view does
 * not infer risk from scene movement or packet count; it exposes that exact
 * persisted audit evidence so an operator can review the source/scene before
 * taking the account-wide access action. */
typedef struct
{
    uint64_t auditId;
    char accountId[64];
    u32 roleId;
    char roleName[32];
    u32 intervalMs;
    char source[49];
    char scene[129];
    u32 enemyId;
    char createdAt[40];
    bool banned;
} vm_mock_admin_risk_audit_row;

typedef struct
{
    vm_mock_admin_risk_audit_row rows[VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE];
    u32 count;
    bool invalid;
} vm_mock_admin_risk_audit_page;

static bool vm_mock_admin_risk_decode_hex(char *destination,
                                          size_t destinationCap,
                                          const char *value,
                                          size_t valueLen)
{
    size_t decodedLen = 0;

    if (destination == NULL || destinationCap == 0 || value == NULL ||
        !vm_mysql_hex_decode(value, valueLen, destination,
                             destinationCap - 1, &decodedLen) ||
        decodedLen >= destinationCap)
    {
        return false;
    }
    destination[decodedLen] = 0;
    return true;
}

static bool vm_mock_admin_risk_audit_row_callback(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_mock_admin_risk_audit_page *page =
        (vm_mock_admin_risk_audit_page *)contextValue;
    vm_mock_admin_risk_audit_row *row = NULL;
    u32 banned = 0;

    if (page == NULL || page->count >= VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE ||
        columnCount != 10 || values == NULL || lengths == NULL)
    {
        if (page != NULL)
            page->invalid = true;
        return true;
    }
    row = &page->rows[page->count];
    if (!vm_mock_mysql_parse_u64(values[0], lengths[0], &row->auditId) ||
        !vm_mock_mysql_copy_text(row->accountId, sizeof(row->accountId),
                                 values[1], lengths[1]) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &row->roleId) ||
        !vm_mock_admin_risk_decode_hex(row->roleName, sizeof(row->roleName),
                                       values[3], lengths[3]) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &row->intervalMs) ||
        !vm_mock_admin_risk_decode_hex(row->source, sizeof(row->source),
                                       values[5], lengths[5]) ||
        !vm_mock_admin_risk_decode_hex(row->scene, sizeof(row->scene),
                                       values[6], lengths[6]) ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &row->enemyId) ||
        !vm_mock_mysql_copy_text(row->createdAt, sizeof(row->createdAt),
                                 values[8], lengths[8]) ||
        !vm_mock_mysql_parse_u32(values[9], lengths[9], &banned) || banned > 1)
    {
        page->invalid = true;
        return true;
    }
    row->banned = banned != 0;
    ++page->count;
    return true;
}

static bool vm_mock_admin_risk_audit_query(u32 offset, u32 limit,
                                           vm_mock_admin_risk_audit_page *page)
{
    char sql[768];

    if (page == NULL || !vm_net_mock_role_prepare_rapid_battle_entry_schema() ||
        !vm_mock_service_account_ban_schema_prepare())
    {
        return false;
    }
    memset(page, 0, sizeof(*page));
    snprintf(sql, sizeof(sql),
             "SELECT a.audit_id,a.account_id,a.role_id,"
             "HEX(COALESCE(r.role_name,X'')),a.interval_ms,HEX(a.source),"
             "HEX(a.scene_name),a.enemy_id,"
             "DATE_FORMAT(a.created_at,'%Y-%m-%d %H:%i:%s.%f'),"
             "IF(b.account_id IS NULL,0,1) "
             "FROM account_role_rapid_battle_entry_audit a "
             "LEFT JOIN account_roles r ON r.account_id=a.account_id "
             "AND r.role_id=a.role_id "
             "LEFT JOIN account_access_bans b ON b.account_id=a.account_id "
             "WHERE a.interval_ms<=3000 "
             "ORDER BY a.audit_id DESC LIMIT %u,%u",
             offset, limit);
    return vm_mysql_query(sql, vm_mock_admin_risk_audit_row_callback, page) &&
           !page->invalid;
}

static bool vm_mock_admin_risk_audit_query_count(u32 *countOut)
{
    static const char sql[] =
        "SELECT COUNT(*) FROM account_role_rapid_battle_entry_audit "
        "WHERE interval_ms<=3000";
    vm_mock_admin_count count;

    if (countOut != NULL)
        *countOut = 0;
    if (!vm_net_mock_role_prepare_rapid_battle_entry_schema())
        return false;
    memset(&count, 0, sizeof(count));
    if (!vm_mysql_query(sql, vm_mock_admin_count_row, &count) ||
        count.invalid || !count.found)
    {
        return false;
    }
    if (countOut != NULL)
        *countOut = count.value;
    return true;
}

static void vm_mock_admin_render_risk_page(char *response,
                                           size_t responseCap,
                                           const char *query)
{
    vm_mock_admin_text page;
    vm_mock_admin_risk_audit_page auditPage;
    char status[16];
    char message[256];
    char pageText[16];
    u32 totalCount = 0;
    u32 pageNumber = 1;
    u32 pageCount = 1;
    u32 offset = 0;
    bool queryOk = false;

    memset(&auditPage, 0, sizeof(auditPage));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(pageText, 0, sizeof(pageText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_admin_form_value(query, "page", pageText, sizeof(pageText));
    if (pageText[0] != 0 &&
        (!vm_net_mock_parse_u32_strict(pageText, &pageNumber) || pageNumber == 0))
    {
        pageNumber = 1;
    }
    queryOk = vm_mock_admin_risk_audit_query_count(&totalCount);
    if (queryOk)
    {
        if (totalCount != 0)
            pageCount = (totalCount + VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE - 1) /
                        VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE;
        if (pageNumber > pageCount)
            pageNumber = pageCount;
        offset = (pageNumber - 1) * VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE;
        queryOk = vm_mock_admin_risk_audit_query(
            offset, VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE, &auditPage);
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 风险角色管理</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1360px;margin:0 auto;padding:24px 18px 42px}header{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 8px}.sub,.muted{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d;margin-bottom:16px}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.rule{margin:0;color:#475467}.badge{display:inline-block;padding:3px 8px;border-radius:999px;background:#fff1f3;color:#c01048;font-size:12px;font-weight:650}.state{font-size:12px;font-weight:650}.state.banned{color:#b42318}.state.open{color:#b54708}.table-wrap{overflow:auto}table{border-collapse:collapse;width:100%;min-width:1050px}th,td{text-align:left;padding:10px 9px;border-bottom:1px solid #eaecf0;vertical-align:top}th{color:#667085;font-weight:600;white-space:nowrap}.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px}.role{font-weight:650}.pages{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:12px}.page-links{display:flex;gap:7px}.page-links a{padding:6px 10px;border:1px solid #d0d5dd;border-radius:6px;color:#344054;text-decoration:none}button{border:0;border-radius:6px;padding:8px 11px;background:#b42318;color:#fff;font:inherit;cursor:pointer;white-space:nowrap}button:disabled{background:#d0d5dd;color:#667085;cursor:not-allowed}@media(max-width:680px){.wrap{padding:16px 10px}header{display:block}.logout{margin-top:9px}}</style>"
        "</head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1><p class=\"sub\">风险角色管理 · 三秒内连续进入战斗审计</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a><a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a><a class=\"tab\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab on\" href=\"/?tab=risk\">风险角色管理</a></nav>"
        "<section class=\"card\"><h2>审计条件 <span class=\"badge\">相邻进入战斗 ≤ 3000 ms</span></h2><p class=\"rule\">记录来自挑战和挂机两条真实战斗入口。封号是账号级永久访问限制：保存后立即注销该账号所有游戏会话与用户中心会话；之后的登录认证将被拒绝。</p></section>");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page,
        "<section class=\"card\"><h2>风险审计列表</h2>");
    if (!queryOk)
    {
        vm_mock_admin_text_appendf(&page,
            "<p class=\"muted\">无法读取风险审计或账号访问表：%s</p></section></main></body></html>",
            vm_mysql_last_error());
    }
    else if (auditPage.count == 0)
    {
        vm_mock_admin_text_appendf(&page,
            "<p class=\"muted\">暂无三秒内连续进入战斗的审计记录。</p></section></main></body></html>");
    }
    else
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"table-wrap\"><table><thead><tr><th>审计时间</th><th>账号</th><th>角色</th><th>间隔</th><th>战斗来源</th><th>场景</th><th>怪物</th><th>状态</th><th>操作</th></tr></thead><tbody>");
        for (u32 i = 0; i < auditPage.count; ++i)
        {
            const vm_mock_admin_risk_audit_row *row = &auditPage.rows[i];
            char roleNameUtf8[96];
            char sourceUtf8[128];
            char sceneUtf8[256];

            memset(roleNameUtf8, 0, sizeof(roleNameUtf8));
            memset(sourceUtf8, 0, sizeof(sourceUtf8));
            memset(sceneUtf8, 0, sizeof(sceneUtf8));
            vm_net_mock_gbk_label_to_utf8(row->roleName, roleNameUtf8,
                                          sizeof(roleNameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->source, sourceUtf8,
                                          sizeof(sourceUtf8));
            vm_net_mock_gbk_label_to_utf8(row->scene, sceneUtf8,
                                          sizeof(sceneUtf8));
            vm_mock_admin_text_appendf(&page,
                "<tr><td class=\"mono\">%s</td><td class=\"mono\">",
                row->createdAt);
            vm_mock_admin_text_append_html(&page, row->accountId);
            vm_mock_admin_text_appendf(&page,
                "</td><td><span class=\"role\">");
            vm_mock_admin_text_append_html(&page,
                                           roleNameUtf8[0] ? roleNameUtf8 : "（角色已删除）");
            vm_mock_admin_text_appendf(&page, "</span><br><span class=\"mono\">ID %u</span></td><td class=\"mono\">%u ms</td><td class=\"mono\">",
                                       row->roleId, row->intervalMs);
            vm_mock_admin_text_append_html(&page, sourceUtf8);
            vm_mock_admin_text_appendf(&page, "</td><td>");
            vm_mock_admin_text_append_html(&page, sceneUtf8);
            vm_mock_admin_text_appendf(&page,
                "</td><td class=\"mono\">%u</td><td><span class=\"state %s\">%s</span></td><td>",
                row->enemyId, row->banned ? "banned" : "open",
                row->banned ? "已封禁" : "待处置");
            if (row->banned)
            {
                vm_mock_admin_text_appendf(&page,
                    "<button type=\"button\" disabled>已封号</button>");
            }
            else
            {
                vm_mock_admin_text_appendf(&page,
                    "<form method=\"post\" action=\"/action\" onsubmit=\"return confirm('确认封禁该账号并立即断开所有连接？此操作会拒绝后续登录。');\"><input type=\"hidden\" name=\"action\" value=\"ban-risk-account\"><input type=\"hidden\" name=\"account\" value=\"");
                vm_mock_admin_text_append_html(&page, row->accountId);
                vm_mock_admin_text_appendf(&page,
                    "\"><input type=\"hidden\" name=\"page\" value=\"%u\"><button type=\"submit\">封号并断开</button></form>",
                    pageNumber);
            }
            vm_mock_admin_text_appendf(&page, "</td></tr>");
        }
        vm_mock_admin_text_appendf(&page,
            "</tbody></table></div><div class=\"pages\"><span>第 %u / %u 页 · 共 %u 条</span><div class=\"page-links\">",
            pageNumber, pageCount, totalCount);
        if (pageNumber > 1)
            vm_mock_admin_text_appendf(
                &page, "<a href=\"/?tab=risk&amp;page=%u\">上一页</a>",
                pageNumber - 1);
        if (pageNumber < pageCount)
            vm_mock_admin_text_appendf(
                &page, "<a href=\"/?tab=risk&amp;page=%u\">下一页</a>",
                pageNumber + 1);
        vm_mock_admin_text_appendf(&page,
            "</div></div></section></main></body></html>");
    }
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>风险角色管理页面超过大小限制。</p>");
    }
}

/* Account listing is an administrative directory view, not a reason to keep
 * every credential in the game-session cache.  The cursor is a database
 * offset over the same ORDER BY contract on each request.  Fetching one extra
 * row makes the existing scroll/load-more UI deterministic without returning
 * the whole account table. */
typedef struct
{
    char accountIds[VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE + 1][64];
    u32 count;
    bool invalid;
} vm_mock_admin_account_page;

static bool vm_mock_admin_account_page_row(void *contextValue,
                                           unsigned int columnCount,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_admin_account_page *page =
        (vm_mock_admin_account_page *)contextValue;

    if (page == NULL || columnCount != 1 ||
        page->count >= VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE + 1 ||
        !vm_mock_mysql_copy_text(page->accountIds[page->count],
                                 sizeof(page->accountIds[page->count]),
                                 values[0], lengths[0]))
    {
        if (page != NULL)
            page->invalid = true;
        return true;
    }
    ++page->count;
    return true;
}

static bool vm_mock_admin_account_query_page(const char *search, u32 cursor,
                                             vm_mock_admin_account_page *page)
{
    char searchHex[128];
    char query[768];
    size_t searchLen = search ? strlen(search) : 0;

    if (page == NULL || cursor > 0xffffffffu - VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE)
        return false;
    memset(page, 0, sizeof(*page));
    if (searchLen == 0)
    {
        snprintf(query, sizeof(query),
                 "SELECT account_id FROM accounts ORDER BY account_id LIMIT %u,%u",
                 cursor, VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE + 1);
    }
    else
    {
        if (searchLen >= 64 ||
            vm_mysql_hex_encode(search, searchLen, searchHex,
                                sizeof(searchHex)) == 0)
        {
            return false;
        }
        snprintf(query, sizeof(query),
                 "SELECT account_id FROM accounts "
                 "WHERE LOCATE(LOWER(CAST(X'%s' AS CHAR)),LOWER(account_id))>0 "
                 "ORDER BY account_id LIMIT %u,%u",
                 searchHex, cursor, VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE + 1);
    }
    if (!vm_mysql_query(query, vm_mock_admin_account_page_row, page) ||
        page->invalid)
    {
        return false;
    }
    return true;
}

static bool vm_mock_admin_account_query_count(const char *search, u32 *countOut)
{
    vm_mock_admin_count count;
    char searchHex[128];
    char query[640];
    size_t searchLen = search ? strlen(search) : 0;

    if (countOut != NULL)
        *countOut = 0;
    memset(&count, 0, sizeof(count));
    if (searchLen == 0)
    {
        snprintf(query, sizeof(query), "SELECT COUNT(*) FROM accounts");
    }
    else
    {
        if (searchLen >= 64 ||
            vm_mysql_hex_encode(search, searchLen, searchHex,
                                sizeof(searchHex)) == 0)
        {
            return false;
        }
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM accounts "
                 "WHERE LOCATE(LOWER(CAST(X'%s' AS CHAR)),LOWER(account_id))>0",
                 searchHex);
    }
    if (!vm_mysql_query(query, vm_mock_admin_count_row, &count) ||
        count.invalid || !count.found)
    {
        return false;
    }
    if (countOut != NULL)
        *countOut = count.value;
    return true;
}

static void vm_mock_admin_render_account_list_fragment(
    vm_mock_admin_text *page, const char *search, u32 cursor,
    const char *selectedAccount)
{
    vm_mock_admin_account_page accounts;
    u32 emitted = 0;
    bool hasMore = false;

    if (page == NULL)
        return;
    if (!vm_mock_admin_account_query_page(search, cursor, &accounts))
    {
        vm_mock_admin_text_appendf(
            page,
            "<div class=\"account-empty muted\">账号目录暂不可用</div>"
            "<span data-account-page-state data-next=\"0\" data-has-more=\"0\" hidden></span>");
        return;
    }
    hasMore = accounts.count > VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE;
    if (accounts.count > VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE)
        accounts.count = VM_MOCK_ADMIN_ACCOUNT_PAGE_SIZE;
    while (emitted < accounts.count)
    {
        const char *accountId = accounts.accountIds[emitted];
        char encodedAccount[192];
        char encodedSearch[192];
        bool online = false;
        bool selected = false;

        online = vm_mock_admin_account_is_online(accountId);
        selected = selectedAccount != NULL &&
                   strcmp(accountId, selectedAccount) == 0;
        vm_mock_admin_url_encode(accountId, encodedAccount,
                                 sizeof(encodedAccount));
        vm_mock_admin_url_encode(search ? search : "", encodedSearch,
                                 sizeof(encodedSearch));
        vm_mock_admin_text_appendf(
            page,
            "<a%s class=\"account%s\" data-admin-select%s href=\"?tab=accounts&amp;account=%s",
            selected ? " id=\"selected-account\"" : "",
            selected ? " on" : "",
            selected ? " aria-current=\"page\"" : "",
            encodedAccount);
        if (encodedSearch[0] != 0)
            vm_mock_admin_text_appendf(page, "&amp;q=%s", encodedSearch);
        vm_mock_admin_text_appendf(page, "#selected-account\"><span>");
        vm_mock_admin_text_append_html(page, accountId);
        vm_mock_admin_text_appendf(page,
                                   "</span><span class=\"%s\">%s</span></a>",
                                   online ? "dot" : "muted",
                                   online ? "在线" : "离线");
        ++emitted;
    }
    if (emitted == 0 && cursor == 0)
        vm_mock_admin_text_appendf(page,
                                   "<div class=\"account-empty muted\">未找到匹配账号</div>");
    vm_mock_admin_text_appendf(
        page,
        "<span data-account-page-state data-next=\"%u\" data-has-more=\"%u\" hidden></span>",
        cursor + emitted, hasMore ? 1 : 0);
}

static void vm_mock_admin_render_scene_battle_monster_actor_select(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *actorFiles,
    u32 actorCount, const char *fieldName, const char *selectedActor)
{
    bool currentFound = false;
    u32 selectableCount = 0;

    if (page == NULL || fieldName == NULL || fieldName[0] == 0)
        return;
    vm_mock_admin_text_appendf(page,
        "<div class=\"actor-picker-field\"><select class=\"actor-resource-select\" name=\"%s\" required hidden>",
        fieldName);
    for (u32 i = 0; actorFiles != NULL && i < actorCount; ++i)
    {
        if (selectedActor != NULL &&
            strcmp(actorFiles[i].name, selectedActor) == 0)
        {
            currentFound = true;
        }
        ++selectableCount;
    }
    if ((selectedActor == NULL || selectedActor[0] == 0) &&
        selectableCount != 0)
    {
        vm_mock_admin_text_appendf(
            page,
            "<option value=\"\" selected disabled>请选择 Actor 资源</option>");
    }
    else if (!currentFound)
    {
        vm_mock_admin_text_appendf(
            page,
            "<option value=\"\" selected disabled>当前 Actor 资源不存在，请重新选择</option>");
    }
    for (u32 i = 0; actorFiles != NULL && i < actorCount; ++i)
    {
        char actorUtf8[192];

        memset(actorUtf8, 0, sizeof(actorUtf8));
        vm_net_mock_gbk_label_to_utf8(actorFiles[i].name, actorUtf8,
                                      sizeof(actorUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, actorUtf8);
        vm_mock_admin_text_appendf(page, "\"%s>",
            selectedActor != NULL &&
                    strcmp(actorFiles[i].name, selectedActor) == 0
                ? " selected" : "");
        vm_mock_admin_text_append_html(page, actorUtf8);
        vm_mock_admin_text_appendf(page, "</option>");
    }
    if (selectableCount == 0)
        vm_mock_admin_text_appendf(
            page, "<option value=\"\" disabled>未找到 Actor 资源</option>");
    vm_mock_admin_text_appendf(
        page,
        "</select><button class=\"actor-picker-trigger\" type=\"button\" data-actor-picker-open aria-haspopup=\"dialog\" aria-controls=\"actor-picker-modal\"><span data-actor-picker-label>请选择 Actor 资源</span><small>搜索与预览</small></button></div>");
}

/* The body actor is selected from every server-visible Actor resource.  The
 * field18 death effect has a separate, much narrower SCE2 contract; do not
 * reuse this catalogue for it. */
static void vm_mock_admin_render_scene_battle_actor_picker_modal(
    vm_mock_admin_text *page, const vm_mock_admin_scene_file *actorFiles,
    u32 actorCount)
{
    u32 selectableCount = 0;

    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(page, "<select id=\"actor-picker-options\" hidden>");
    for (u32 i = 0; actorFiles != NULL && i < actorCount; ++i)
    {
        char actorUtf8[192];

        memset(actorUtf8, 0, sizeof(actorUtf8));
        vm_mock_admin_resource_name_to_utf8(actorFiles[i].name, actorUtf8,
                                             sizeof(actorUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"");
        vm_mock_admin_text_append_html(page, actorUtf8);
        vm_mock_admin_text_appendf(page, "\">");
        vm_mock_admin_text_append_html(
            page, actorUtf8[0] ? actorUtf8 : actorFiles[i].name);
        vm_mock_admin_text_appendf(page, "</option>");
        ++selectableCount;
    }
    vm_mock_admin_text_appendf(
        page,
        "</select><div id=\"actor-picker-modal\" class=\"actor-modal\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"actor-picker-title\" hidden><div class=\"actor-picker-panel\"><div class=\"actor-picker-head\"><div><h3 id=\"actor-picker-title\">选择场景战斗怪本体 Actor</h3><p>显示服务端可见的 Actor；field18 的退场特效必须从其专用原生列表选择。</p></div><button id=\"actor-picker-close\" class=\"actor-picker-close\" type=\"button\" aria-label=\"关闭\">×</button></div><div class=\"actor-picker-tools\"><label><span>搜索资源名称</span><input id=\"actor-picker-search\" type=\"search\" placeholder=\"例如 monster、ghost、effect\" autocomplete=\"off\"></label></div><div class=\"actor-result-bar\"><span id=\"actor-result-count\"></span><span id=\"actor-picker-error\" class=\"actor-picker-error\"></span></div><div id=\"actor-picker-list\" class=\"actor-picker-list\"></div><p id=\"actor-picker-empty\" class=\"actor-picker-empty\" hidden>没有符合条件的 Actor 资源。</p></div></div>");
    if (selectableCount == 0)
    {
        printf("[warn][mock-admin] scene_battle_actor_picker_catalog_empty source=server-resource-root\n");
    }
}

static void vm_mock_admin_render_scene_battle_monster_effect_select(
    vm_mock_admin_text *page, const char *fieldName, const char *selected)
{
    static const char *const effectResources[] = {
        "e_ghostfireR.actor", "e_ghostfireG.actor",
        "e_ghostfireB.actor", "e_ghostfiresG.actor"
    };
    bool known = false;

    if (page == NULL || fieldName == NULL || fieldName[0] == 0)
        return;
    for (u32 i = 0; i < sizeof(effectResources) / sizeof(effectResources[0]);
         ++i)
    {
        if (selected != NULL && strcmp(selected, effectResources[i]) == 0)
        {
            known = true;
            break;
        }
    }
    vm_mock_admin_text_appendf(page,
        "<select name=\"%s\" required>", fieldName);
    if (selected != NULL && selected[0] != 0 && !known)
    {
        vm_mock_admin_text_appendf(page,
            "<option value=\"\" selected disabled>当前资源不符合原生 field18 契约，请重新选择</option>");
    }
    for (u32 i = 0; i < sizeof(effectResources) / sizeof(effectResources[0]);
         ++i)
    {
        vm_mock_admin_text_appendf(page, "<option value=\"%s\"%s>%s</option>",
            effectResources[i], selected != NULL &&
            strcmp(selected, effectResources[i]) == 0 ? " selected" : "",
            effectResources[i]);
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

/* A battle-capable scene entity has a different client contract from an NPC.
 * This page stores only deployment drafts.  The corresponding action rebuilds
 * a real SCE2 kind-3 record and never injects a dynamic-NPC packet. */
static void vm_mock_admin_render_scene_battle_monster_page(
    char *response, size_t responseCap, const char *query)
{
    vm_mock_admin_scene_file sceneFiles[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    vm_mock_admin_scene_file actorFiles[VM_MOCK_ADMIN_ACTOR_FILE_MAX];
    vm_net_mock_scene_battle_monster_admin_row
        rows[VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX];
    vm_mock_admin_text page;
    char selectedSceneUtf8[192];
    char selectedSceneFile[64];
    char runtimeScene[64];
    char status[16];
    char message[256];
    u32 sceneCount = 0;
    u32 actorCount = 0;
    u32 rowCount = 0;
    u32 enabledCount = 0;
    bool deployed = false;

    memset(sceneFiles, 0, sizeof(sceneFiles));
    memset(actorFiles, 0, sizeof(actorFiles));
    memset(rows, 0, sizeof(rows));
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
        if (!found && sceneCount != 0)
            snprintf(selectedSceneFile, sizeof(selectedSceneFile), "%s",
                     sceneFiles[0].name);
    }
    vm_net_mock_gbk_label_to_utf8(selectedSceneFile, selectedSceneUtf8,
                                  sizeof(selectedSceneUtf8));
    if (selectedSceneFile[0] != 0 &&
        vm_mock_admin_scene_file_to_runtime_key(selectedSceneFile,
                                                runtimeScene,
                                                sizeof(runtimeScene)))
    {
        rowCount = vm_net_mock_scene_battle_monster_admin_list(
            runtimeScene, rows, VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX);
        (void)vm_net_mock_scene_battle_monster_admin_is_deployed(
            runtimeScene, rows, rowCount, &deployed);
        for (u32 i = 0; i < rowCount; ++i)
        {
            if (rows[i].enabled)
                ++enabledCount;
        }
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 场景战斗怪管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1280px;height:100vh;margin:0 auto;padding:24px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;flex:none}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 10px}.sub,.hint,.muted{color:#667085}.sub{margin:4px 0 16px}.tabs{display:flex;gap:6px;margin:0 0 14px;overflow:auto;flex:none}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec;white-space:nowrap}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{border:1px solid #d0d5dd;background:#fff;color:#475467;border-radius:6px;padding:8px 12px}.grid{display:grid;grid-template-columns:290px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.scenes{display:flex;min-height:0;flex-direction:column}.scene-list{display:flex;flex:1;min-height:0;overflow:auto;flex-direction:column;gap:4px;padding-right:4px}.scene{display:flex;justify-content:space-between;gap:8px;padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none}.scene:hover,.scene.on{background:#eef4ff;color:#175cd3}.size{font-size:12px;color:#98a2b3;white-space:nowrap}.editor{overflow:auto;padding-right:4px}.notice{padding:10px 12px;border-radius:7px;margin-bottom:13px}.notice.ok{background:#ecfdf3;color:#027a48}.notice.error{background:#fef3f2;color:#b42318}.summary{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px}.badge{padding:3px 8px;border-radius:999px;background:#eef4ff;color:#175cd3;font-size:12px}.badge.warn{background:#fffaeb;color:#b54708}.callout{padding:12px;border:1px solid #b2ddff;border-radius:8px;background:#eff8ff;color:#1849a9;margin-bottom:14px}.deploy{display:flex;justify-content:space-between;gap:12px;align-items:center;margin:0 0 14px;padding:12px;border:1px solid #d0d5dd;border-radius:8px;background:#f8fafc}.fields{display:grid;grid-template-columns:100px minmax(135px,1fr) minmax(180px,1.35fr) 84px 84px 94px 90px auto;gap:9px;align-items:end}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.secondary{background:#475467}.danger{background:#b42318}.monster-list{display:grid;gap:11px}.monster{padding:13px;border:1px solid #e4e7ec;border-radius:9px}.monster.off{opacity:.62;background:#f9fafb}.monster-head{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:10px}.monster-head h3{font-size:15px;margin:0}.actions{display:flex;gap:8px;align-items:end}.new{border-color:#b2ddff;background:#fbfdff}.foot{font-size:12px;margin:13px 0 0;color:#667085}@media(max-width:920px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:16px 10px;overflow:visible}.grid{grid-template-columns:1fr;flex:none}.scenes{max-height:250px}.editor{overflow:visible}.fields{grid-template-columns:1fr 1fr}.actions{align-items:stretch}}@media(max-width:560px){.fields{grid-template-columns:1fr}.deploy{align-items:stretch;flex-direction:column}.actions{display:grid;grid-template-columns:1fr}}</style></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1><p class=\"sub\">场景战斗怪 · 以客户端原生 SCE2 kind-3 战斗节点部署</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab on\" href=\"/?tab=scene-monsters\">场景战斗怪</a><a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a><a class=\"tab\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>"
        "<style>.actor-picker-field{display:grid;gap:4px}.actor-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px;white-space:normal}.actor-picker-trigger small{color:#667085;font-weight:400}.actor-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899}.actor-picker-panel{width:min(920px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.actor-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.actor-picker-head h3{font-size:19px;margin:0}.actor-picker-head p{margin:2px 0 0;color:#667085}.actor-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.actor-picker-tools{padding:14px 20px 10px}.actor-picker-tools label{display:grid;gap:4px}.actor-picker-tools label>span{font-size:12px;color:#667085}.actor-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.actor-picker-error{color:#b42318;font-weight:600}.actor-picker-list{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;min-height:160px;overflow:auto;padding:0 20px 20px}.actor-choice{display:grid;grid-template-rows:92px auto;gap:7px;padding:10px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.actor-choice:hover{border-color:#84adff;background:#f5f8ff}.actor-choice img{display:block;width:100%%;height:88px;object-fit:contain;image-rendering:pixelated;background:#f9fafb;border-radius:5px}.actor-choice strong{font-size:12px;overflow-wrap:anywhere}.actor-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}[hidden]{display:none!important}.modal-open{overflow:hidden!important}</style>"
        "<div class=\"grid\"><aside class=\"card scenes\"><h2>SCE 场景（%u）</h2><div class=\"scene-list\">",
        sceneCount);
    for (u32 i = 0; i < sceneCount; ++i)
    {
        char sceneUtf8[192];
        char encoded[512];

        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        memset(encoded, 0, sizeof(encoded));
        vm_net_mock_gbk_label_to_utf8(sceneFiles[i].name, sceneUtf8,
                                      sizeof(sceneUtf8));
        vm_mock_admin_url_encode(sceneUtf8, encoded, sizeof(encoded));
        vm_mock_admin_text_appendf(&page,
            "<a class=\"scene%s\" href=\"/?tab=scene-monsters&amp;scene=%s\"><span>",
            strcmp(sceneFiles[i].name, selectedSceneFile) == 0 ? " on" : "",
            encoded);
        vm_mock_admin_text_append_html(&page, sceneUtf8);
        vm_mock_admin_text_appendf(&page, "</span><span class=\"size\">%llu</span></a>",
            (unsigned long long)sceneFiles[i].size);
    }
    vm_mock_admin_text_appendf(&page,
        "</div></aside><section class=\"card editor\">"
        "<h2>场景：%s</h2><div class=\"summary\"><span class=\"badge\">草稿 %u</span><span class=\"badge\">启用 %u</span><span class=\"badge %s\">%s</span></div>",
        selectedSceneUtf8[0] ? selectedSceneUtf8 : "未选择", rowCount,
        enabledCount, deployed ? "" : "warn",
        deployed ? "当前草稿已部署" : "存在未部署变更");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
            strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (runtimeScene[0] == 0)
    {
        vm_mock_admin_text_appendf(&page,
            "<p class=\"muted\">未找到可管理的服务端 SCE 资源。</p></section></div></main></body></html>");
        return;
    }
    vm_mock_admin_text_appendf(&page,
        "<div class=\"callout\"><strong>保存仅更新草稿。</strong>部署会从首次捕获的服务端基础 SCE 重建完整的 kind-3 战斗记录（含 field18 效果 Actor），并校验 Actor 依赖、记录解析和客户端最多 24 个非本地场景节点的限制。部署完成会自动加入“游戏内容更新管理”的启动内容版本；客户端需完整退出并重新启动，再进入该场景。</div>"
        "<form class=\"deploy\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"deploy-scene-battle-monsters\"><input type=\"hidden\" name=\"scene\" value=\"");
    vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
    vm_mock_admin_text_appendf(&page,
        "\"><span><strong>部署场景战斗怪</strong><br><span class=\"hint\">将当前启用项编译进 %s</span></span><button type=\"submit\">验证并部署</button></form>"
        "<div class=\"monster-list\"><article class=\"monster new\"><div class=\"monster-head\"><h3>新增场景战斗怪</h3><span class=\"badge\">草稿</span></div><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-scene-battle-monster\"><input type=\"hidden\" name=\"entry_id\" value=\"0\"><input type=\"hidden\" name=\"scene\" value=\"",
        selectedSceneUtf8);
    vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
    vm_mock_admin_text_appendf(&page,
        "\"><div class=\"fields\"><label class=\"field\"><span>怪物 ID</span><input name=\"monster_id\" type=\"number\" min=\"1\" max=\"65535\" required></label><label class=\"field\"><span>显示名称（GBK ≤29字节）</span><input name=\"display_name\" maxlength=\"29\" required></label><label class=\"field\"><span>Actor 资源</span>");
    vm_mock_admin_render_scene_battle_monster_actor_select(
        &page, actorFiles, actorCount, "actor_resource", NULL);
    vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\"><span>退场特效（field18）</span>");
    vm_mock_admin_render_scene_battle_monster_effect_select(
        &page, "effect_resource", "e_ghostfireR.actor");
    vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\"><span>X</span><input name=\"pos_x\" type=\"number\" min=\"1\" max=\"65535\" required></label><label class=\"field\"><span>Y</span><input name=\"pos_y\" type=\"number\" min=\"1\" max=\"65535\" required></label><label class=\"field\"><span>视觉提示</span><select name=\"visual_hint\"><option value=\"5\">5 · 普通</option><option value=\"6\">6 · 强敌</option></select></label><label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\">启用</option><option value=\"0\">停用</option></select></label><div class=\"actions\"><button type=\"submit\">保存草稿</button></div></div></form></article>");
    for (u32 i = 0; i < rowCount; ++i)
    {
        char nameUtf8[128];

        memset(nameUtf8, 0, sizeof(nameUtf8));
        vm_net_mock_gbk_label_to_utf8(rows[i].displayName, nameUtf8,
                                      sizeof(nameUtf8));
        vm_mock_admin_text_appendf(&page,
            "<article class=\"monster%s\"><div class=\"monster-head\"><h3>#%u · ",
            rows[i].enabled ? "" : " off", rows[i].entryId);
        vm_mock_admin_text_append_html(&page, nameUtf8);
        vm_mock_admin_text_appendf(&page,
            "</h3><span class=\"badge\">%s</span></div><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-scene-battle-monster\"><input type=\"hidden\" name=\"entry_id\" value=\"%u\"><input type=\"hidden\" name=\"scene\" value=\"",
            rows[i].enabled ? "启用" : "停用", rows[i].entryId);
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "\"><div class=\"fields\"><label class=\"field\"><span>怪物 ID</span><input name=\"monster_id\" type=\"number\" min=\"1\" max=\"65535\" value=\"%u\" required></label><label class=\"field\"><span>显示名称</span><input name=\"display_name\" maxlength=\"29\" value=\"",
            rows[i].monsterId);
        vm_mock_admin_text_append_html(&page, nameUtf8);
        vm_mock_admin_text_appendf(&page, "\" required></label><label class=\"field\"><span>Actor 资源</span>");
        vm_mock_admin_render_scene_battle_monster_actor_select(
            &page, actorFiles, actorCount, "actor_resource",
            rows[i].actorResource);
        vm_mock_admin_text_appendf(&page,
        "</label><label class=\"field\"><span>退场特效（field18）</span>");
        vm_mock_admin_render_scene_battle_monster_effect_select(
            &page, "effect_resource", rows[i].effectResource);
        vm_mock_admin_text_appendf(&page,
            "</label><label class=\"field\"><span>X</span><input name=\"pos_x\" type=\"number\" min=\"1\" max=\"65535\" value=\"%u\" required></label><label class=\"field\"><span>Y</span><input name=\"pos_y\" type=\"number\" min=\"1\" max=\"65535\" value=\"%u\" required></label><label class=\"field\"><span>视觉提示</span><select name=\"visual_hint\"><option value=\"5\"%s>5 · 普通</option><option value=\"6\"%s>6 · 强敌</option></select></label><label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\"%s>启用</option><option value=\"0\"%s>停用</option></select></label><div class=\"actions\"><button type=\"submit\">保存草稿</button></div></div></form><form class=\"actions\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"delete-scene-battle-monster\"><input type=\"hidden\" name=\"scene\" value=\"",
            rows[i].x, rows[i].y, rows[i].visualHint == 5 ? " selected" : "",
            rows[i].visualHint == 6 ? " selected" : "",
            rows[i].enabled ? " selected" : "",
            rows[i].enabled ? "" : " selected");
        vm_mock_admin_text_append_html(&page, selectedSceneUtf8);
        vm_mock_admin_text_appendf(&page,
            "\"><input type=\"hidden\" name=\"entry_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">删除草稿</button></form></article>",
            rows[i].entryId);
    }
    vm_mock_admin_text_appendf(&page,
        "</div><p class=\"foot\">怪物 ID 决定怪物属性、掉落和任务击败条件；同一 ID 可以在多个场景或坐标生成。普通 NPC（含任务发布者）不属于此配置层，不能作为场景战斗怪替代。</p>");
    vm_mock_admin_render_scene_battle_actor_picker_modal(
        &page, actorFiles, actorCount);
    vm_mock_admin_text_appendf(
        &page,
        "</section></div></main><script src=\"/admin.js\"></script></body></html>");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>场景战斗怪管理页面超过大小限制。</p>");
    }
}

static bool vm_mock_admin_actor_file_is_listed(
    const vm_mock_admin_scene_file *files, u32 fileCount, const char *name)
{
    if (files == NULL || name == NULL || name[0] == 0)
        return false;
    for (u32 i = 0; i < fileCount; ++i)
    {
        if (strcmp(files[i].name, name) == 0)
            return true;
    }
    return false;
}

static void vm_mock_admin_actor_append_manifest_textareas(
    vm_mock_admin_text *page, const vm_mock_admin_actor_manifest *manifest)
{
    if (page == NULL || manifest == NULL)
        return;
    vm_mock_admin_text_appendf(page,
        "<label>引用图片（每行一个服务端 GIF 文件名）"
        "<textarea name=\"images\" rows=\"5\" required>");
    for (u32 i = 0; i < manifest->imageCount; ++i)
    {
        vm_mock_admin_text_append_html(page, manifest->imageNames[i]);
        vm_mock_admin_text_appendf(page, "\n");
    }
    vm_mock_admin_text_appendf(page,
        "</textarea></label><label>源矩形（每行：left,top,right,bottom,imageIndex）"
        "<textarea name=\"rectangles\" rows=\"9\" required>");
    for (u32 i = 0; i < manifest->rectCount; ++i)
    {
        vm_mock_admin_text_appendf(page, "%d,%d,%d,%d,%d\n",
            manifest->rects[i].left, manifest->rects[i].top,
            manifest->rects[i].right, manifest->rects[i].bottom,
            manifest->rects[i].imageIndex);
    }
    vm_mock_admin_text_appendf(page,
        "</textarea></label><label>动画部件（每行：animationIndex,partId；必须按动画顺序）"
        "<textarea name=\"parts\" rows=\"8\" required>");
    for (u32 i = 0; i < manifest->partCount; ++i)
    {
        vm_mock_admin_text_appendf(page, "%d,%d\n",
            manifest->parts[i].animationIndex, manifest->parts[i].partId);
    }
    vm_mock_admin_text_appendf(page,
        "</textarea></label><label>动画帧（每行：animationIndex,partId,rectIndex,offsetX,offsetY,value3,value4；须按部件顺序）"
        "<textarea name=\"frames\" rows=\"16\" required>");
    for (u32 i = 0; i < manifest->partCount; ++i)
    {
        const vm_mock_admin_actor_edit_part *part = &manifest->parts[i];
        for (u32 frameIndex = 0; frameIndex < part->frameCount; ++frameIndex)
        {
            const vm_mock_admin_actor_edit_frame *frame =
                &manifest->frames[part->firstFrame + frameIndex];
            vm_mock_admin_text_appendf(page, "%d,%d,%d,%d,%d,%d,%d\n",
                part->animationIndex, part->partId, frame->rectIndex,
                frame->offsetX, frame->offsetY, frame->value3, frame->value4);
        }
    }
    vm_mock_admin_text_appendf(page, "</textarea></label>");
}

static void vm_mock_admin_render_actor_resource_page(char *response,
                                                      size_t responseCap,
                                                      const char *query)
{
    vm_mock_admin_text page;
    vm_mock_admin_scene_file files[VM_MOCK_ADMIN_ACTOR_FILE_MAX];
    vm_mock_admin_actor_manifest manifest;
    char selected[64];
    char templateName[64];
    char status[16];
    char message[256];
    char newText[8];
    u32 fileCount = 0;
    bool newMode = false;
    bool loaded = false;

    memset(files, 0, sizeof(files));
    memset(&manifest, 0, sizeof(manifest));
    memset(selected, 0, sizeof(selected));
    memset(templateName, 0, sizeof(templateName));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(newText, 0, sizeof(newText));
    (void)vm_mock_admin_form_value(query, "actor", selected, sizeof(selected));
    (void)vm_mock_admin_form_value(query, "template", templateName,
                                   sizeof(templateName));
    (void)vm_mock_admin_form_value(query, "new", newText, sizeof(newText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    newMode = strcmp(newText, "1") == 0;
    fileCount = vm_mock_admin_collect_actor_files(files,
                                                   VM_MOCK_ADMIN_ACTOR_FILE_MAX);
    if (newMode)
    {
        if (!vm_mock_admin_actor_file_is_listed(files, fileCount, templateName))
        {
            for (u32 i = 0; i < fileCount; ++i)
            {
                if (vm_mock_admin_actor_name_is_writable(files[i].name))
                {
                    snprintf(templateName, sizeof(templateName), "%s",
                             files[i].name);
                    break;
                }
            }
        }
        loaded = templateName[0] != 0 &&
                 vm_mock_admin_actor_manifest_from_resource(templateName,
                                                            &manifest);
    }
    else
    {
        if (!vm_mock_admin_actor_file_is_listed(files, fileCount, selected) &&
            fileCount != 0)
        {
            snprintf(selected, sizeof(selected), "%s", files[0].name);
        }
        loaded = selected[0] != 0 &&
                 vm_mock_admin_actor_manifest_from_resource(selected, &manifest);
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Actor 资源管理 · 江湖OL 后台</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1440px;margin:0 auto;padding:28px 18px 46px}header{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:18px;margin:0}h3{font-size:15px;margin:0}.sub,.muted{color:#667085;margin:4px 0 20px}.tabs{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{border:1px solid #d0d5dd;background:#fff;color:#475467}.layout{display:grid;grid-template-columns:285px minmax(0,1fr);gap:16px;align-items:start}.card{background:#fff;border:1px solid #e4e7ec;border-radius:12px;padding:18px;box-shadow:0 1px 2px #1018280d}.actor-list{display:grid;gap:5px;max-height:calc(100vh - 225px);overflow:auto;padding-right:3px}.actor-link{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px 9px;border-radius:7px;color:#344054;text-decoration:none}.actor-link:hover,.actor-link.on{background:#eef4ff;color:#175cd3}.actor-link small{color:#98a2b3}.new-link{display:block;margin:12px 0;padding:9px 10px;border-radius:7px;background:#175cd3;color:#fff;text-align:center;text-decoration:none;font-weight:650}.new-link:hover{background:#1849a9}.notice{padding:10px 12px;border-radius:8px;margin:0 0 14px}.notice.ok{background:#ecfdf3;color:#027a48}.notice.error{background:#fef3f2;color:#b42318}.actor-head{display:flex;align-items:flex-start;justify-content:space-between;gap:18px;margin-bottom:14px}.preview{width:156px;min-width:156px;height:156px;display:grid;place-items:center;border:1px solid #e4e7ec;border-radius:10px;background:linear-gradient(135deg,#f8fafc,#eef4ff);overflow:hidden}.preview img{max-width:148px;max-height:148px;image-rendering:pixelated}.editor{display:grid;gap:13px}.top-fields{display:grid;grid-template-columns:minmax(230px,1fr) auto;gap:10px;align-items:end}.top-fields label,.editor>label{display:grid;gap:5px;color:#344054;font-weight:600}.top-fields label>span,.editor>label>span{font-size:12px;color:#667085;font-weight:500}input,select,textarea{width:100%%;border:1px solid #d0d5dd;border-radius:7px;padding:9px 10px;background:#fff;color:#1f2937;font:13px/1.45 ui-monospace,SFMono-Regular,Consolas,monospace}textarea{resize:vertical;min-height:76px}button{border:0;border-radius:7px;padding:9px 13px;background:#175cd3;color:#fff;font-weight:650;cursor:pointer}button:hover{background:#1849a9}.template{display:flex;gap:8px;align-items:end;padding:12px;border:1px solid #e4e7ec;border-radius:9px;background:#f8fafc}.template label{display:grid;gap:4px;flex:1;color:#475467}.template label span{font-size:12px}.contract{margin:0;padding:10px 12px;border-left:3px solid #175cd3;border-radius:4px;background:#eef4ff;color:#344054}.stats{display:flex;gap:8px;flex-wrap:wrap}.badge{border-radius:999px;background:#eef4ff;color:#175cd3;padding:3px 8px;font-size:12px}.empty{padding:28px;border:1px dashed #d0d5dd;border-radius:9px;color:#667085;text-align:center}@media(max-width:900px){.layout{grid-template-columns:1fr}.actor-list{max-height:250px}.preview{width:116px;min-width:116px;height:116px}.preview img{max-width:108px;max-height:108px}}@media(max-width:580px){.wrap{padding:18px 10px}.top-fields{grid-template-columns:1fr}.actor-head{align-items:flex-start}.preview{display:none}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>Actor 资源管理</h1><p class=\"sub\">编辑服务端权威 .actor 结构；保存后会用原生内容更新让客户端重新下载。</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a><a class=\"tab on\" href=\"/?tab=actors\">Actor 资源</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a><a class=\"tab\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav><div class=\"layout\"><aside class=\"card\"><h2>已有 Actor（%u）</h2><div class=\"actor-list\" data-admin-list><a class=\"new-link%s\" data-admin-select%s href=\"/?tab=actors&amp;new=1\">新建 Actor 资源</a>",
        fileCount, newMode ? " on" : "",
        newMode ? " aria-current=\"page\"" : "");
    for (u32 i = 0; i < fileCount; ++i)
    {
        char encoded[256];
        vm_mock_admin_url_encode(files[i].name, encoded, sizeof(encoded));
        int selectedActor = !newMode && strcmp(files[i].name, selected) == 0;
        vm_mock_admin_text_appendf(&page,
            "<a class=\"actor-link%s\" data-admin-select%s href=\"/?tab=actors&amp;actor=%s\"><span>",
            selectedActor ? " on" : "",
            selectedActor ? " aria-current=\"page\"" : "", encoded);
        vm_mock_admin_text_append_html(&page, files[i].name);
        vm_mock_admin_text_appendf(&page, "</span><small>%llu B</small></a>",
            (unsigned long long)files[i].size);
    }
    if (fileCount == 0)
        vm_mock_admin_text_appendf(&page, "<div class=\"empty\">服务端资源目录中没有 Actor 文件。</div>");
    vm_mock_admin_text_appendf(&page,
                                "</div></aside><section class=\"card\" data-admin-detail>");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
            strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (!loaded)
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"empty\"><h2>资源无法编辑</h2><p>所选 Actor 不存在、不是 type-2 容器，或不符合当前已确认的图片/矩形/动画帧格式。</p></div>");
    }
    else
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"actor-head\"><div><h2>%s</h2><p class=\"muted\">%s</p><div class=\"stats\"><span class=\"badge\">图片 %u</span><span class=\"badge\">矩形 %u</span><span class=\"badge\">动画 %u</span><span class=\"badge\">部件 %u</span><span class=\"badge\">帧 %u</span></div></div>",
            newMode ? "新建 Actor" : "编辑 Actor",
            newMode ? "先以现有资源为模板，再指定新的 ASCII 文件名。" : "保存会原子替换该服务端资源，并发布到内容更新清单。",
            manifest.imageCount, manifest.rectCount, manifest.animationCount,
            manifest.partCount, manifest.frameCount);
        if (!newMode)
        {
            char encoded[256];
            vm_mock_admin_url_encode(selected, encoded, sizeof(encoded));
            vm_mock_admin_text_appendf(&page,
                "<div class=\"preview\"><img src=\"/actor-preview.svg?actor=%s\" alt=\"Actor 预览\"></div>",
                encoded);
        }
        vm_mock_admin_text_appendf(&page, "</div>");
        if (newMode)
        {
            vm_mock_admin_text_appendf(&page,
                "<form class=\"template\" method=\"get\" action=\"/\"><input type=\"hidden\" name=\"tab\" value=\"actors\"><input type=\"hidden\" name=\"new\" value=\"1\"><label><span>新建模板</span><select name=\"template\">");
            for (u32 i = 0; i < fileCount; ++i)
            {
                vm_mock_admin_text_appendf(&page, "<option value=\"");
                vm_mock_admin_text_append_html(&page, files[i].name);
                vm_mock_admin_text_appendf(&page, "\"%s>",
                    strcmp(files[i].name, templateName) == 0 ? " selected" : "");
                vm_mock_admin_text_append_html(&page, files[i].name);
                vm_mock_admin_text_appendf(&page, "</option>");
            }
            vm_mock_admin_text_appendf(&page,
                "</select></label><button type=\"submit\">载入模板</button></form>");
        }
        vm_mock_admin_text_appendf(&page,
            "<p class=\"contract\">保存前会重新编码 type-2 LZSS 容器、回读验证，并核验每个 GIF 引用；发布内容更新失败时会恢复原文件。图片、部件和帧行不得使用路径分隔符，帧必须按部件顺序。</p>"
            "<form class=\"editor\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-actor-resource\"><input type=\"hidden\" name=\"mode\" value=\"%s\"><div class=\"top-fields\"><label><span>Actor 文件名（仅字母、数字、下划线、连字符和点）</span><input name=\"actor_name\" maxlength=\"63\" value=\"",
            newMode ? "new" : "edit");
        vm_mock_admin_text_append_html(&page,
                                       newMode ? "new_actor.actor" : selected);
        vm_mock_admin_text_appendf(&page,
            "\" required></label><button type=\"submit\">%s并发布</button></div>",
            newMode ? "新建" : "保存");
        vm_mock_admin_actor_append_manifest_textareas(&page, &manifest);
        vm_mock_admin_text_appendf(&page, "</form>");
    }
    vm_mock_admin_text_appendf(&page, "</section></div></main></body></html>");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>Actor 资源编辑页超过大小限制；请缩小该资源的帧表后再编辑。</p>");
    }
    vm_mock_admin_actor_manifest_free(&manifest);
}

static void vm_mock_admin_render_page(char *response, size_t responseCap,
                                      const char *query)
{
    vm_mock_admin_text page;
    char tab[16];
    char selectedAccount[64];
    char accountSearch[64];
    char status[16];
    char message[256];
    const char *roleError = NULL;
    vm_mock_service_account_state *accountState = NULL;
    vm_mock_admin_scene_file resetSceneFiles[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    u32 managedRoleIds[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    char managedRoleNames[VM_NET_MOCK_ROLE_DB_MAX_ROLES][128];
    u32 managedRoleCount = 0;
    u32 resetSceneCount = 0;
    u32 accountTotal = 0;
    vm_mock_admin_account_page initialAccounts;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(tab, 0, sizeof(tab));
    memset(selectedAccount, 0, sizeof(selectedAccount));
    memset(accountSearch, 0, sizeof(accountSearch));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(resetSceneFiles, 0, sizeof(resetSceneFiles));
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
    if (strcmp(tab, "monsters") == 0)
    {
        vm_mock_admin_render_monster_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "scene-monsters") == 0)
    {
        vm_mock_admin_render_scene_battle_monster_page(response, responseCap,
                                                       query);
        return;
    }
    if (strcmp(tab, "actors") == 0)
    {
        vm_mock_admin_render_actor_resource_page(response, responseCap, query);
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
    if (strcmp(tab, "chests") == 0)
    {
        vm_mock_admin_render_chest_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "servers") == 0)
    {
        vm_mock_admin_render_servers_page(response, responseCap, query);
        return;
    }
    if (strcmp(tab, "risk") == 0)
    {
        vm_mock_admin_render_risk_page(response, responseCap, query);
        return;
    }
    (void)vm_mock_admin_form_value(query, "account", selectedAccount, sizeof(selectedAccount));
    (void)vm_mock_admin_form_value(query, "q", accountSearch, sizeof(accountSearch));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));

    resetSceneCount = vm_mock_admin_collect_scene_files(
        resetSceneFiles, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    if (!vm_mock_admin_account_query_count(accountSearch, &accountTotal))
        accountTotal = 0;
    if ((selectedAccount[0] == 0 ||
         !vm_mock_service_account_exists(selectedAccount)) &&
        vm_mock_admin_account_query_page(accountSearch, 0, &initialAccounts) &&
        initialAccounts.count != 0)
    {
        snprintf(selectedAccount, sizeof(selectedAccount), "%s",
                 initialAccounts.accountIds[0]);
    }

    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 后台管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100vh;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1120px;height:100vh;margin:0 auto;padding:28px 18px;display:flex;flex-direction:column;overflow:hidden}header{display:flex;flex:none;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:17px;margin:0 0 14px}"
        ".sub{color:#667085;margin:4px 0 20px}.grid{display:grid;grid-template-columns:240px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:18px;box-shadow:0 1px 2px #1018280d}.grid>aside{display:flex;flex-direction:column;min-height:0;overflow:hidden}.grid>section{min-width:0;min-height:0;overflow:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}"
        ".tabs{display:flex;gap:6px;margin:0 0 16px}.tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;background:#fff;border:1px solid #e4e7ec}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:none;color:#667085;border:1px solid #d0d5dd}"
        ".account-search{display:flex;gap:7px;margin:0 0 8px}.account-search input{min-width:0}.account-search button{padding-inline:10px}.account-list-status{min-height:19px;margin:0 0 8px;color:#667085;font-size:12px}.account-list-status.error{color:#b42318}.accounts{display:flex;flex:1;min-height:0;flex-direction:column;gap:6px;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable;padding-right:4px}.account{display:flex;justify-content:space-between;padding:9px 10px;border-radius:7px;color:#344054;text-decoration:none;scroll-margin-block:12px}.account:hover,.account.on{background:#eef4ff;color:#175cd3}.account-empty{padding:10px 2px}"
        ".dot{color:#12b76a}.muted{color:#98a2b3}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        "table{border-collapse:collapse;width:100%%}th,td{text-align:left;padding:10px 8px;border-bottom:1px solid #eaecf0;vertical-align:top}th{color:#667085;font-weight:600}"
        "input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}button:hover{background:#1849a9}"
        ".inline{display:flex;gap:7px;margin:0 0 7px}.inline input{min-width:105px}.role-rename{align-items:center;margin-top:7px}.role-rename input{width:112px;min-width:112px}.role-rename button{padding:6px 9px;font-size:12px}.level-set{align-items:center;margin:7px 0 0}.level-set input{width:76px;min-width:76px}.level-set button{padding:6px 9px;font-size:12px}.level-note{display:block;margin-top:4px;color:#98a2b3;font-size:12px;line-height:1.35}.scene-reset-input{min-width:230px!important}.reset-position{background:#b54708}.reset-position:hover{background:#93370d}.forms{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:16px}.stack{display:grid;gap:9px}.badge{font-size:12px;background:#eef4ff;color:#175cd3;padding:2px 7px;border-radius:999px}.money{white-space:nowrap}.position{min-width:150px}.item-grant{border-top:1px solid #eaecf0;margin-top:18px;padding-top:18px}.grant-form{display:grid;grid-template-columns:minmax(130px,.8fr) minmax(280px,2fr) 90px auto;gap:9px;align-items:end}.grant-form label,.grant-form .item-field{display:grid;gap:4px}.grant-form label>span,.grant-form .item-field>span{font-size:12px;color:#667085}.grant-note{margin:8px 0 0;font-size:12px}"
        "button.item-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px}button.item-picker-trigger:hover{background:#f9fafb;border-color:#84adff}button.item-picker-trigger small{color:#98a2b3;font-weight:400}.item-picker-head-actions{display:flex;align-items:center;gap:8px}.item-picker-head-actions #item-picker-clear{background:#f2f4f7;color:#475467}.item-picker-trigger.compact{min-height:32px;font-size:12px}"
        "[hidden]{display:none!important}.modal-open{overflow:hidden}.item-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899;backdrop-filter:blur(2px)}.item-picker-panel{width:min(780px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.item-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.item-picker-head h3{font-size:19px;margin:0}.item-picker-head p{margin:2px 0 0;color:#667085}.item-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.item-picker-close:hover{background:#e4e7ec;color:#1d2939}.item-picker-tools{display:grid;grid-template-columns:minmax(200px,.8fr) minmax(260px,1.2fr);gap:10px;padding:14px 20px 10px}.item-picker-tools label{display:grid;gap:4px}.item-picker-tools label>span{font-size:12px;color:#667085}.item-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.item-picker-error{color:#b42318;font-weight:600}.item-picker-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:140px;overflow:auto;padding:0 20px 20px;scrollbar-gutter:stable}.item-choice{display:grid;gap:2px;padding:10px 12px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.item-choice:hover{border-color:#84adff;background:#f5f8ff}.item-choice.selected{border-color:#175cd3;background:#eef4ff}.item-choice strong{font-size:14px}.item-choice span{color:#667085;font-size:12px}.item-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}.foot{margin-top:16px;color:#667085;font-size:12px}"
        "@media(max-width:780px){html,body{height:auto;overflow:auto}.wrap{height:auto;min-height:100vh;padding:18px 10px;overflow:visible}.grid,.forms{grid-template-columns:1fr;flex:none}.grid>aside,.grid>section{overflow:visible}.accounts{flex:none;max-height:220px;overflow:auto}.table-wrap{overflow:auto}.grant-form{grid-template-columns:1fr}.grant-form>button[type=submit]{justify-self:start}.item-modal{padding:10px}.item-picker-panel{max-height:calc(100vh - 20px)}.item-picker-tools,.item-picker-list{grid-template-columns:1fr}.item-picker-list{padding-inline:12px}.item-picker-head,.item-picker-tools{padding-inline:14px}}"
        "</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">本机管理端口 · 数据直接保存到 MySQL · 普通钱币以铜为基础单位</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab on\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a>"
        "<a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "<a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav><div class=\"grid\">"
        "<aside class=\"card\"><h2>账号（%u）</h2><form class=\"account-search\" data-account-search-form role=\"search\"><input data-account-search maxlength=\"63\" placeholder=\"搜索账号名\" aria-label=\"搜索账号名\" value=\"",
        accountTotal);
    vm_mock_admin_text_append_html(&page, accountSearch);
    vm_mock_admin_text_appendf(&page,
                               "\"><button type=\"submit\">搜索</button></form>"
                               "<p class=\"account-list-status\" data-account-list-status></p>"
                               "<div class=\"accounts\" data-account-list data-admin-list>");
    vm_mock_admin_render_account_list_fragment(&page, accountSearch, 0,
                                               selectedAccount);
    vm_mock_admin_text_appendf(&page, "</div></aside><section data-admin-detail><div class=\"card\">");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }

    vm_mock_admin_text_appendf(&page, "<h2>账号与角色明细：");
    vm_mock_admin_text_append_html(&page, selectedAccount[0] ? selectedAccount : "未选择");
    vm_mock_admin_text_appendf(&page, "</h2>");

    if (selectedAccount[0] != 0)
        accountState = vm_mock_service_open_account_role_db_for_management(selectedAccount, &roleError);
    if (accountState != NULL)
    {
        u32 accountWcoin = 0;

        if (vm_mock_service_account_wallet_read(selectedAccount, false, &accountWcoin))
        {
            vm_mock_admin_text_appendf(&page,
                "<div class=\"notice ok\"><strong>账号 W 币：%u</strong>"
                "<span class=\"muted\">W 币归账号所有，全部角色共用。</span>"
                "<form class=\"inline\" method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"add-wcoin\">"
                "<input type=\"hidden\" name=\"account\" value=\"",
                accountWcoin);
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"number\" name=\"amount\" min=\"1\" max=\"4294967295\" placeholder=\"增加 W 币\" required>"
                "<button type=\"submit\">加 W 币</button></form></div>");
        }
        else
        {
            vm_mock_admin_text_appendf(&page,
                "<div class=\"notice error\">账号 W 币钱包暂不可读取，未提供修改入口。</div>");
        }
    }
    vm_mock_admin_text_appendf(&page, "<div class=\"table-wrap\"><table><thead><tr>"
                               "<th>角色 / 职业</th><th>等级 / 状态</th><th>当前位置</th><th>普通钱币</th><th>操作</th>"
                               "</tr></thead><tbody>");
    if (accountState != NULL)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
            char roleNameUtf8[128];
            char sceneUtf8[128];
            u32 gold = role->money / 10000u;
            u32 silver = (role->money / 100u) % 100u;
            u32 copper = role->money % 100u;
            bool active = role->roleId == g_vm_net_mock_role_db.activeRoleId;

            vm_net_mock_gbk_label_to_utf8(role->name,
                                          roleNameUtf8, sizeof(roleNameUtf8));
            vm_net_mock_gbk_label_to_utf8(role->scene,
                                          sceneUtf8, sizeof(sceneUtf8));
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
            vm_mock_admin_text_appendf(&page,
                                       "</strong><br><span class=\"muted\">ID %u · %s</span>"
                                       "<form class=\"inline role-rename\" method=\"post\" action=\"/action\" "
                                       "onsubmit=\"return confirm('确定修改这个角色的名称？');\">"
                                       "<input type=\"hidden\" name=\"action\" value=\"set-role-name\">"
                                       "<input type=\"hidden\" name=\"account\" value=\"",
                                       role->roleId,
                                       vm_mock_admin_role_job_label(role->job));
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                                       "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                                       "<input name=\"role_name\" value=\"",
                                       role->roleId);
            vm_mock_admin_text_append_html(&page, roleNameUtf8);
            vm_mock_admin_text_appendf(&page,
                                       "\" minlength=\"2\" maxlength=\"15\" aria-label=\"角色名称\" required>"
                                       "<button type=\"submit\">改名</button></form></td>");
            vm_mock_admin_text_appendf(&page, "<td><div>Lv.%u%s</div>", role->level,
                                       active ? " <span class=\"badge\">当前角色</span>" : "");
            vm_mock_admin_text_appendf(
                &page,
                "<form class=\"inline level-set\" method=\"post\" action=\"/action\" "
                "onsubmit=\"return confirm('将等级设为指定值，并把经验重置到该等级起点？在线账号会先被强制断开。');\">"
                "<input type=\"hidden\" name=\"action\" value=\"set-role-level\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(
                &page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input type=\"number\" name=\"level\" min=\"1\" max=\"%u\" value=\"%u\" aria-label=\"角色等级\" required>"
                "<button type=\"submit\">设定等级</button></form>"
                "<span class=\"level-note\">在线账号会先强制离线；经验将定位到该等级起点，HP/MP 上限按新等级重算。</span></td>",
                role->roleId, VM_NET_MOCK_ROLE_LEVEL_CAP, role->level);
            vm_mock_admin_text_appendf(&page, "<td class=\"position\">");
            vm_mock_admin_text_append_html(&page, sceneUtf8);
            vm_mock_admin_text_appendf(&page,
                                       "<br><span class=\"muted\">(%u, %u)</span></td>",
                                       role->x, role->y);
            vm_mock_admin_text_appendf(&page,
                                       "<td class=\"money\">%u 金 %u 银 %u 铜<br><span class=\"muted\">总计 %u 铜</span></td><td>",
                                       gold, silver, copper, role->money);
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
                "<form class=\"inline\" method=\"post\" action=\"/action\" "
                "onsubmit=\"return confirm('将角色重置到所选场景的服务端安全落点？若该角色在线，会先强制断开其游戏连接。');\">"
                "<input type=\"hidden\" name=\"action\" value=\"reset-role-selected-scene\">"
                "<input type=\"hidden\" name=\"account\" value=\"");
            vm_mock_admin_text_append_html(&page, selectedAccount);
            vm_mock_admin_text_appendf(&page,
                "\"><input type=\"hidden\" name=\"role\" value=\"%u\">"
                "<input class=\"scene-reset-input\" name=\"reset_scene\" list=\"role-reset-scene-catalog\" placeholder=\"选择或输入目标场景\" aria-label=\"目标重置场景\" required>"
                "<button class=\"reset-position\" type=\"submit\">重置到指定场景</button></form></td></tr>",
                role->roleId);
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
    vm_mock_admin_render_role_reset_scene_catalog(
        &page, resetSceneFiles, resetSceneCount);
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
                               "<p class=\"foot\">位置重置会先断开目标角色的在线游戏连接：选择服务端资源目录中的精确 SCE 场景后，服务端从该 SCE 解析安全落点并保存；目标资源或落点无法验证时不会改写位置，也不会回退到出生点。</p>"
                               "</section></div></main></body></html>");

    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><title>响应过大</title><p>后台页面响应超过大小限制。</p>");
    }
}

static int vm_mock_admin_handle_account_list_request(
    vm_mock_service_socket client, const char *query)
{
    char search[64];
    char cursorText[32];
    char *response = NULL;
    u32 cursor = 0;
    u32 accountTotal = 0;
    vm_mock_admin_text page;

    memset(search, 0, sizeof(search));
    memset(cursorText, 0, sizeof(cursorText));
    if (query != NULL)
    {
        (void)vm_mock_admin_form_value(query, "q", search, sizeof(search));
        if (vm_mock_admin_form_value(query, "cursor", cursorText,
                                     sizeof(cursorText)) &&
            !vm_net_mock_parse_u32_strict(cursorText, &cursor))
        {
            vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL,
                                        "账号列表游标无效。\n");
            return 0;
        }
    }
    if (!vm_mock_admin_account_query_count(search, &accountTotal) ||
        cursor > accountTotal)
    {
        vm_mock_admin_send_response(client, "409 Conflict", NULL, NULL,
                                    "账号目录不可用，请刷新后台页面。\n");
        return 0;
    }
    response = (char *)malloc(VM_MOCK_ADMIN_RESPONSE_MAX);
    if (response == NULL)
    {
        vm_mock_admin_send_response(client, "500 Internal Server Error", NULL,
                                    NULL, "内存不足。\n");
        return 0;
    }
    vm_mock_admin_text_init(&page, response, VM_MOCK_ADMIN_RESPONSE_MAX);
    vm_mock_admin_render_account_list_fragment(&page, search, cursor, NULL);
    if (page.truncated)
    {
        vm_mock_admin_send_response(client, "500 Internal Server Error", NULL,
                                    NULL, "账号列表响应超过大小限制。\n");
        free(response);
        return 0;
    }
    vm_mock_admin_send_response(client, "200 OK", "text/html; charset=utf-8",
                                "Cache-Control: no-store\r\n", response);
    free(response);
    return 1;
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

static void vm_mock_admin_redirect_content_section(
    vm_mock_service_socket client, const char *sceneUtf8, const char *section,
    const char *status, const char *message)
{
    char sceneEncoded[512];
    char sectionEncoded[64];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1700];

    vm_mock_admin_url_encode(sceneUtf8 ? sceneUtf8 : "", sceneEncoded,
                             sizeof(sceneEncoded));
    vm_mock_admin_url_encode(section ? section : "npcs", sectionEncoded,
                             sizeof(sectionEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=content&content_kind=sce&content_section=%s&scene=%s&status=%s&message=%s",
             sectionEncoded, sceneEncoded, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_content_resource(
    vm_mock_service_socket client, const char *kind, const char *resourceUtf8,
    const char *status, const char *message)
{
    char kindEncoded[64];
    char resourceEncoded[512];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1700];

    vm_mock_admin_url_encode(kind ? kind : "", kindEncoded,
                             sizeof(kindEncoded));
    vm_mock_admin_url_encode(resourceUtf8 ? resourceUtf8 : "",
                             resourceEncoded, sizeof(resourceEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=content&content_kind=%s&resource=%s&status=%s&message=%s",
             kindEncoded, resourceEncoded, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_dsh_row(
    vm_mock_service_socket client, const char *resourceUtf8, u32 row,
    const char *status, const char *message)
{
    char resourceEncoded[512];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1800];

    vm_mock_admin_url_encode(resourceUtf8 ? resourceUtf8 : "",
                             resourceEncoded, sizeof(resourceEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=content&content_kind=dsh&resource=%s&dsh_row=%u&status=%s&message=%s",
             resourceEncoded, row, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static bool vm_mock_admin_sce_portal_target_replace(
    const char *scene, const vm_mock_admin_scene_portal *portal,
    const char *targetScene, const char **errorOut, bool *contentChangedOut)
{
    char path[1200];
    u8 *previousRaw = NULL;
    u32 previousRawLen = 0;
    u8 *payload = NULL;
    u32 payloadLen = 0;
    u8 type = 0;
    u8 *newPayload = NULL;
    u32 newPayloadLen = 0;
    u8 *encoded = NULL;
    u32 encodedLen = 0;
    u8 *raw = NULL;
    u32 rawLen = 0;
    u8 *verifyPayload = NULL;
    u32 verifyPayloadLen = 0;
    u8 verifyType = 0;
    const char *publishNames[1];
    const char *publishError = NULL;
    bool changed = false;
    bool restored = false;
    size_t oldTargetLen = 0;
    size_t newTargetLen = 0;
    u32 oldFieldLen = 0;
    u16 expectedField = 0;
    bool ok = false;

    if (errorOut)
        *errorOut = NULL;
    if (contentChangedOut)
        *contentChangedOut = false;
    if (scene == NULL || portal == NULL || targetScene == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_name_is_safe(targetScene) ||
        !vm_net_mock_str_ends_with(scene, ".sce") ||
        !vm_net_mock_str_ends_with(targetScene, ".sce") ||
        !vm_net_mock_scene_resource_exists(targetScene) ||
        !vm_net_mock_update_resource_path(scene, path, sizeof(path)) ||
        !vm_mock_admin_read_raw_resource_file(path, &previousRaw,
                                              &previousRawLen) ||
        !vm_mock_admin_load_data_payload(scene, ".sce", &payload,
                                         &payloadLen, &type))
    {
        if (errorOut)
            *errorOut = "无法读取权威 SCE 资源或目标场景不存在";
        goto done;
    }
    oldTargetLen = strlen(portal->targetScene);
    newTargetLen = strlen(targetScene);
    expectedField = portal->kind == VM_MOCK_ADMIN_PORTAL_NAMED ? 0x17u : 6u;
    if (oldTargetLen == 0 || oldTargetLen > 0xffu || newTargetLen == 0 ||
        newTargetLen > 0xffu || portal->targetLengthOffset < 4u ||
        portal->targetLengthOffset >= payloadLen ||
        vm_net_mock_read_le16_at(payload, portal->targetLengthOffset - 4u) != 3u ||
        vm_net_mock_read_le16_at(payload, portal->targetLengthOffset - 2u) !=
            expectedField)
    {
        if (errorOut)
            *errorOut = "传送点字段已变化；请刷新场景后重新选择";
        goto done;
    }
    oldFieldLen = payload[portal->targetLengthOffset];
    if (oldFieldLen != oldTargetLen ||
        portal->targetLengthOffset + 1u + oldFieldLen > payloadLen ||
        memcmp(payload + portal->targetLengthOffset + 1u, portal->targetScene,
               oldTargetLen) != 0)
    {
        if (errorOut)
            *errorOut = "传送点源字段与当前 SCE 不一致；已拒绝写入";
        goto done;
    }
    if (strcmp(portal->targetScene, targetScene) == 0)
    {
        ok = true;
        goto done;
    }
    if (newTargetLen > oldTargetLen &&
        payloadLen > UINT32_MAX - (u32)(newTargetLen - oldTargetLen))
    {
        if (errorOut)
            *errorOut = "目标场景名称长度超出资源容量";
        goto done;
    }
    newPayloadLen = payloadLen - oldFieldLen + (u32)newTargetLen;
    newPayload = (u8 *)malloc(newPayloadLen);
    if (newPayload == NULL)
    {
        if (errorOut)
            *errorOut = "内存不足，未修改 SCE";
        goto done;
    }
    memcpy(newPayload, payload, portal->targetLengthOffset);
    newPayload[portal->targetLengthOffset] = (u8)newTargetLen;
    memcpy(newPayload + portal->targetLengthOffset + 1u, targetScene,
           newTargetLen);
    memcpy(newPayload + portal->targetLengthOffset + 1u + newTargetLen,
           payload + portal->targetLengthOffset + 1u + oldFieldLen,
           payloadLen - (portal->targetLengthOffset + 1u + oldFieldLen));
    if (type == 1u)
    {
        if (newPayloadLen > UINT32_MAX - 5u)
            goto done;
        rawLen = newPayloadLen + 5u;
        raw = (u8 *)malloc(rawLen);
        if (raw == NULL)
            goto done;
        vm_mock_admin_preview_write_le32(raw, 0, newPayloadLen + 1u);
        raw[4] = 1u;
        memcpy(raw + 5, newPayload, newPayloadLen);
    }
    else if (type == 2u)
    {
        u32 encodedCap = 9u + newPayloadLen +
                         (newPayloadLen + 126u) / 127u;

        encoded = (u8 *)malloc(encodedCap);
        if (encoded == NULL ||
            !vm_net_mock_scene_battle_monster_lzss_literal_encode(
                newPayload, newPayloadLen, encoded, encodedCap, &encodedLen) ||
            encodedLen == 0 || encodedLen > UINT32_MAX - 4u)
        {
            if (errorOut)
                *errorOut = "SCE 压缩封装失败";
            goto done;
        }
        rawLen = encodedLen + 4u;
        raw = (u8 *)malloc(rawLen);
        if (raw == NULL)
            goto done;
        vm_mock_admin_preview_write_le32(raw, 0, encodedLen);
        memcpy(raw + 4, encoded, encodedLen);
    }
    else
    {
        if (errorOut)
            *errorOut = "SCE 使用了不支持的资源容器类型";
        goto done;
    }
    if (!vm_mock_admin_write_resource_atomic(path, raw, rawLen) ||
        !vm_mock_admin_load_data_payload(scene, ".sce", &verifyPayload,
                                         &verifyPayloadLen, &verifyType) ||
        verifyType != type || portal->targetLengthOffset >= verifyPayloadLen ||
        verifyPayload[portal->targetLengthOffset] != newTargetLen ||
        portal->targetLengthOffset + 1u + newTargetLen > verifyPayloadLen ||
        memcmp(verifyPayload + portal->targetLengthOffset + 1u, targetScene,
               newTargetLen) != 0)
    {
        restored = vm_mock_admin_write_resource_atomic(path, previousRaw,
                                                        previousRawLen);
        if (errorOut)
            *errorOut = restored ? "SCE 写入后校验失败，已恢复原资源"
                                 : "SCE 写入后校验失败，恢复原资源也失败";
        goto done;
    }
    publishNames[0] = scene;
    if (!vm_net_mock_content_update_publish_files(publishNames, 1,
                                                  &publishError, &changed))
    {
        restored = vm_mock_admin_write_resource_atomic(path, previousRaw,
                                                        previousRawLen);
        if (errorOut)
            *errorOut = restored ? "内容更新发布失败，已恢复原 SCE"
                                 : "内容更新发布失败，恢复原 SCE 也失败";
        goto done;
    }
    if (contentChangedOut)
        *contentChangedOut = changed;
    printf("[info][mock-admin] sce_portal_target_save scene=%s offset=%u target=%s raw=%u publish=WT18/9+18/8->18/7\n",
           scene, portal->targetLengthOffset, targetScene, rawLen);
    ok = true;

done:
    free(previousRaw);
    free(payload);
    free(newPayload);
    free(encoded);
    free(raw);
    free(verifyPayload);
    return ok;
}

static void vm_mock_admin_handle_sce_portal_action(
    vm_mock_service_socket client, const char *body)
{
    vm_mock_admin_scene_portal portals[VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX];
    char sceneUtf8[192];
    char runtimeScene[64];
    char targetUtf8[192];
    char targetScene[64];
    u32 fieldOffset = 0;
    u32 portalCount = 0;
    const vm_mock_admin_scene_portal *selected = NULL;
    const char *error = NULL;
    bool changed = false;

    memset(portals, 0, sizeof(portals));
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(targetUtf8, 0, sizeof(targetUtf8));
    memset(targetScene, 0, sizeof(targetScene));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)) ||
        !vm_mock_admin_form_u32(body, "portal_offset", 0x00ffffffu,
                                &fieldOffset) ||
        !vm_mock_admin_form_value(body, "target_scene", targetUtf8,
                                  sizeof(targetUtf8)) ||
        !vm_mock_admin_utf8_to_gbk_text(targetUtf8, targetScene,
                                        sizeof(targetScene), false) ||
        !vm_net_mock_str_ends_with(targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(targetScene) ||
        !vm_net_mock_scene_resource_exists(targetScene))
    {
        vm_mock_admin_redirect_content_section(
            client, sceneUtf8, "portals", "error",
            "场景、传送点或目标 SCE 参数无效");
        return;
    }
    portalCount = vm_mock_admin_collect_scene_portals(
        runtimeScene, portals, VM_MOCK_ADMIN_PREVIEW_PORTAL_MAX, NULL);
    for (u32 i = 0; i < portalCount; ++i)
    {
        if (portals[i].targetLengthOffset == fieldOffset)
        {
            selected = &portals[i];
            break;
        }
    }
    if (selected == NULL)
    {
        vm_mock_admin_redirect_content_section(
            client, sceneUtf8, "portals", "error",
            "传送点记录已变化或不是可编辑的原始 SCE 字段；请刷新后重试");
        return;
    }
    if (!vm_mock_admin_sce_portal_target_replace(
            runtimeScene, selected, targetScene, &error, &changed))
    {
        vm_mock_admin_redirect_content_section(
            client, sceneUtf8, "portals", "error",
            error ? error : "传送点目标保存失败");
        return;
    }
    vm_mock_admin_redirect_content_section(
        client, sceneUtf8, "portals", "ok",
        changed ? "目标 SCE 已严格更新并发布；客户端下次加载会下载新场景资源"
                : "目标 SCE 未变化，未重复发布内容更新");
}

static void vm_mock_admin_redirect_scene_battle_monsters(
    vm_mock_service_socket client, const char *sceneUtf8, const char *status,
    const char *message)
{
    char sceneEncoded[512];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1600];

    vm_mock_admin_url_encode(sceneUtf8 ? sceneUtf8 : "", sceneEncoded,
                             sizeof(sceneEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=scene-monsters&scene=%s&status=%s&message=%s",
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

static void vm_mock_admin_redirect_chests(vm_mock_service_socket client,
                                          u32 chestItemId,
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
             "?tab=chests&chest=%u&status=%s&message=%s",
             chestItemId, statusEncoded, messageEncoded);
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

static void vm_mock_admin_redirect_risk(vm_mock_service_socket client,
                                        u32 page,
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
             "?tab=risk&page=%u&status=%s&message=%s",
             page ? page : 1, statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_actors(vm_mock_service_socket client,
                                          const char *actor,
                                          const char *status,
                                          const char *message)
{
    char actorEncoded[256];
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1400];

    vm_mock_admin_url_encode(actor ? actor : "", actorEncoded,
                             sizeof(actorEncoded));
    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=actors&actor=%s&status=%s&message=%s",
             actorEncoded, statusEncoded, messageEncoded);
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

/* Empty is meaningful for merchant prices: it asks the service to resolve the
 * product's configured catalog price.  Keep this separate from form_u32 so
 * existing numeric-only administration fields remain strict. */
static bool vm_mock_admin_form_optional_u32(const char *body, const char *field,
                                            u32 maximum, u32 *valueOut)
{
    char textValue[32];
    u32 parsed = 0;

    memset(textValue, 0, sizeof(textValue));
    if (!vm_mock_admin_form_value(body, field, textValue, sizeof(textValue)) ||
        textValue[0] == 0)
    {
        if (valueOut)
            *valueOut = 0;
        return true;
    }
    if (!vm_net_mock_parse_u32_strict(textValue, &parsed) || parsed > maximum)
        return false;
    if (valueOut)
        *valueOut = parsed;
    return true;
}

/* The browser serializes multi-select inventory choices as a comma-separated
 * decimal list.  Decode through the normal form parser first, then enforce
 * one catalog-bounded, duplicate-free list before any database operation. */
static bool vm_mock_admin_form_u32_csv(const char *body, const char *field,
                                       u32 *values, u32 valueCap,
                                       u32 *valueCountOut)
{
    char *text = NULL;
    char *cursor = NULL;
    u32 count = 0;
    bool ok = false;

    if (valueCountOut)
        *valueCountOut = 0;
    if (body == NULL || field == NULL || values == NULL || valueCap == 0)
        return false;
    text = (char *)malloc(VM_MOCK_ADMIN_REQUEST_BODY_MAX + 1u);
    if (text == NULL ||
        !vm_mock_admin_form_value(body, field, text,
                                  VM_MOCK_ADMIN_REQUEST_BODY_MAX + 1u) ||
        text[0] == 0)
    {
        goto done;
    }
    cursor = text;
    while (*cursor != 0)
    {
        char *comma = strchr(cursor, ',');
        char saved = 0;
        u32 value = 0;

        if (comma != NULL)
        {
            saved = *comma;
            *comma = 0;
        }
        if (cursor[0] == 0 || count >= valueCap ||
            !vm_net_mock_parse_u32_strict(cursor, &value) || value == 0)
        {
            if (comma != NULL)
                *comma = saved;
            goto done;
        }
        for (u32 prior = 0; prior < count; ++prior)
        {
            if (values[prior] == value)
            {
                if (comma != NULL)
                    *comma = saved;
                goto done;
            }
        }
        values[count++] = value;
        if (comma == NULL)
            break;
        *comma = saved;
        cursor = comma + 1;
    }
    ok = count != 0;

done:
    free(text);
    if (ok && valueCountOut)
        *valueCountOut = count;
    return ok;
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
#undef VM_TASK_FORM_U8
#undef VM_TASK_FORM_U32
    for (u8 slot = 0; slot < VM_NET_MOCK_TASK_REWARD_ITEM_MAX; ++slot)
    {
        char itemField[48];
        char countField[48];
        char typeField[48];
        u32 itemId = 0;
        u32 itemCount = 0;
        u32 itemType = 0;

        snprintf(itemField, sizeof(itemField), "reward_item_id_%u", slot);
        snprintf(countField, sizeof(countField), "reward_item_count_%u", slot);
        snprintf(typeField, sizeof(typeField), "reward_item_type_%u", slot);
        if (!vm_mock_admin_form_u32(body, itemField, 0xffffffffu, &itemId) ||
            !vm_mock_admin_form_u32(body, countField, 0xffffffffu, &itemCount) ||
            !vm_mock_admin_form_u32(body, typeField, 0xffu, &itemType))
        {
            vm_mock_admin_redirect_tasks(client, taskId, "error",
                                         "任务奖励表单字段不完整或数值越界");
            return;
        }
        if (itemId == 0 && itemCount == 0)
            continue;
        if (itemId == 0 || itemCount == 0 ||
            task.rewardItemNum >= VM_NET_MOCK_TASK_REWARD_ITEM_MAX)
        {
            vm_mock_admin_redirect_tasks(client, taskId, "error",
                                         "每项任务奖励都必须同时选择物品和数量");
            return;
        }
        task.rewardItems[task.rewardItemNum].itemId = itemId;
        task.rewardItems[task.rewardItemNum].count = itemCount;
        task.rewardItems[task.rewardItemNum].itemType = (u8)itemType;
        ++task.rewardItemNum;
    }
    vm_net_mock_task_reward_items_sync_legacy(&task);
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
    for (u8 rewardIndex = 0; rewardIndex < task.rewardItemNum;
         ++rewardIndex)
    {
        if (vm_net_mock_find_shop_catalog_item(
                task.rewardItems[rewardIndex].itemId) == NULL)
        {
            vm_mock_admin_redirect_tasks(
                client, taskId, "error",
                "任务奖励中的物品 ID 不在物品目录中");
            return;
        }
        if ((vm_net_mock_find_equipment_catalog_item(
                 task.rewardItems[rewardIndex].itemId) != NULL ||
             vm_net_mock_backpack_item_id_uses_reservoir_count(
                 task.rewardItems[rewardIndex].itemId)) &&
            task.rewardItems[rewardIndex].count != 1)
        {
            vm_mock_admin_redirect_tasks(
                client, taskId, "error",
                "装备、神仙壶和逍遥壶必须按每项数量 1 配置奖励");
            return;
        }
    }
    if (!vm_net_mock_task_admin_save(&task, &error))
    {
        vm_mock_admin_redirect_tasks(client, taskId, "error",
                                     error ? error : "任务保存失败");
        return;
    }
    vm_mock_admin_redirect_tasks(client, taskId, "ok", "任务定义已保存并立即生效");
}

static void vm_mock_admin_handle_monster_action(vm_mock_service_socket client,
                                                 const char *action,
                                                 const char *body)
{
    vm_net_mock_monster_admin_row row;
    const char *error = NULL;
    u32 family = 0;

    memset(&row, 0, sizeof(row));
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
    if (strcmp(action, "reset-monster-combat-stats") == 0)
    {
        if (!vm_net_mock_monster_admin_reset_combat_stats(row.enemyId,
                                                           &error))
        {
            vm_mock_admin_redirect_monster(
                client, row.enemyId, "error",
                error ? error : "重置怪物战斗属性失败");
            return;
        }
        vm_mock_admin_redirect_monster(
            client, row.enemyId, "ok",
            "已按当前等级和类型重置 HP、MP、攻击、防御、经验和铜钱；物品掉落保持不变");
        return;
    }
    if (strcmp(action, "reset-monster-combat-stats-bulk") == 0)
    {
        u32 enemyIds[VM_NET_MOCK_MONSTER_CATALOG_MAX];
        u32 enemyCount = 0;
        u32 updated = 0;
        u32 alreadyDefault = 0;
        char message[192];

        memset(enemyIds, 0, sizeof(enemyIds));
        memset(message, 0, sizeof(message));
        if (!vm_mock_admin_form_u32_csv(
                body, "monster_ids", enemyIds,
                VM_NET_MOCK_MONSTER_CATALOG_MAX, &enemyCount))
        {
            vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                           "请至少选择一个有效怪物");
            return;
        }
        if (!vm_net_mock_monster_admin_reset_combat_stats_batch(
                enemyIds, enemyCount, &updated, &alreadyDefault, &error))
        {
            vm_mock_admin_redirect_monster(
                client, row.enemyId, "error",
                error ? error : "批量重置怪物战斗属性失败");
            return;
        }
        snprintf(message, sizeof(message),
                 "已批量按各自等级和类型重置 %u 项属性和结算奖励；%u 项原本已使用服务端公式",
                 updated, alreadyDefault);
        vm_mock_admin_redirect_monster(client, row.enemyId, "ok", message);
        return;
    }
    if (strcmp(action, "reset-monster-scene-levels-bulk") == 0)
    {
        u32 enemyIds[VM_NET_MOCK_MONSTER_CATALOG_MAX];
        u32 enemyCount = 0;
        u32 updated = 0;
        u32 skipped = 0;
        char message[224];

        memset(enemyIds, 0, sizeof(enemyIds));
        memset(message, 0, sizeof(message));
        if (!vm_mock_admin_form_u32_csv(
                body, "monster_ids", enemyIds,
                VM_NET_MOCK_MONSTER_CATALOG_MAX, &enemyCount))
        {
            vm_mock_admin_redirect_monster(client, row.enemyId, "error",
                                           "请至少选择一个有效怪物");
            return;
        }
        if (!vm_net_mock_monster_admin_reset_scene_levels_batch(
                enemyIds, enemyCount, &updated, &skipped, &error))
        {
            vm_mock_admin_redirect_monster(
                client, row.enemyId, "error",
                error ? error : "按场景等级批量重置失败");
            return;
        }
        snprintf(message, sizeof(message),
                 "已按 sMap 场景怪物等级覆盖 %u 项，并重置属性和结算奖励；%u 项缺少有效场景等级，已跳过",
                 updated, skipped);
        vm_mock_admin_redirect_monster(client, row.enemyId, "ok", message);
        return;
    }
    if (strcmp(action, "save-monster") != 0 ||
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
    for (u8 slot = 0; slot < VM_NET_MOCK_MONSTER_DROP_MAX; ++slot)
    {
        char itemField[48];
        char rateField[48];
        u32 itemId = 0;
        u32 rate = 0;

        snprintf(itemField, sizeof(itemField), "drop_item_id_%u", (u32)slot);
        snprintf(rateField, sizeof(rateField), "drop_rate_%u", (u32)slot);
        if (!vm_mock_admin_form_u32(body, itemField, 0xffffffffu, &itemId) ||
            !vm_mock_admin_form_u32(body, rateField, 100u, &rate))
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
        row.drops[row.dropCount].ratePercent = (u8)rate;
        ++row.dropCount;
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

static bool vm_mock_admin_optional_scene_from_form(
    const char *body,
    const char *fieldName,
    char *sceneUtf8,
    size_t sceneUtf8Cap,
    char *runtimeScene,
    size_t runtimeSceneCap)
{
    vm_mock_admin_scene_file files[VM_MOCK_ADMIN_SCENE_FILE_MAX];
    char sceneFile[64];
    u32 fileCount = 0;

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
    fileCount = vm_mock_admin_collect_scene_files(
        files, VM_MOCK_ADMIN_SCENE_FILE_MAX);
    for (u32 i = 0; i < fileCount; ++i)
    {
        if (strcmp(files[i].name, sceneFile) == 0)
        {
            return vm_mock_admin_scene_file_to_runtime_key(
                sceneFile, runtimeScene, runtimeSceneCap);
        }
    }
    return false;
}

/* Dynamic and native NPC forms use checkbox rows instead of a free-form
 * action/value editor.  An absent checkbox is the normal HTML representation
 * of false; every enabled row still carries separately bounded UTF-8 text
 * fields which are converted to the GBK protocol/storage representation. */
static bool vm_mock_admin_form_npc_service_options(
    const char *body, bool allowInstance,
    vm_net_mock_npc_service_option *options, u32 optionCap,
    u32 *optionCountOut)
{
    u32 count = 0;

    if (options == NULL || optionCap < VM_NET_MOCK_NPC_SERVICE_OPTION_MAX ||
        optionCountOut == NULL)
    {
        return false;
    }
    memset(options, 0, optionCap * sizeof(options[0]));
    *optionCountOut = 0;
    for (u32 kind = VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT;
         kind <= VM_NET_MOCK_NPC_KIND_MAX; ++kind)
    {
        char enabledField[48];
        char nameField[48];
        char descriptionField[56];
        char enabledText[8];
        char nameUtf8[256];
        char descriptionUtf8[384];
        bool enabled = false;

        if (!allowInstance && kind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE)
            continue;

        snprintf(enabledField, sizeof(enabledField), "service_enabled_%u", kind);
        snprintf(nameField, sizeof(nameField), "service_option_name_%u", kind);
        snprintf(descriptionField, sizeof(descriptionField),
                 "service_option_description_%u", kind);
        memset(enabledText, 0, sizeof(enabledText));
        memset(nameUtf8, 0, sizeof(nameUtf8));
        memset(descriptionUtf8, 0, sizeof(descriptionUtf8));
        if (vm_mock_admin_form_value(body, enabledField, enabledText,
                                     sizeof(enabledText)))
        {
            if (strcmp(enabledText, "1") != 0)
                return false;
            enabled = true;
        }
        if (!vm_mock_admin_form_value(body, nameField, nameUtf8,
                                      sizeof(nameUtf8)) ||
            !vm_mock_admin_form_value(body, descriptionField,
                                      descriptionUtf8,
                                      sizeof(descriptionUtf8)))
        {
            return false;
        }
        if (strcmp(nameUtf8, "-") == 0)
            nameUtf8[0] = 0;
        if (strcmp(descriptionUtf8, "-") == 0)
            descriptionUtf8[0] = 0;
        if (!enabled)
            continue;
        if ((!allowInstance && kind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE) ||
            count >= optionCap ||
            !vm_mock_admin_utf8_to_gbk_text(nameUtf8,
                                            options[count].optionName,
                                            sizeof(options[count].optionName),
                                            true) ||
            !vm_mock_admin_utf8_to_gbk_text(
                descriptionUtf8, options[count].optionDescription,
                sizeof(options[count].optionDescription), true))
        {
            return false;
        }
        options[count].kind = (u16)kind;
        options[count].sortOrder = (u8)count;
        ++count;
    }
    *optionCountOut = count;
    return true;
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
    char enabledText[8];
    vm_net_mock_scene_npcinfo_seed seed;
    vm_net_mock_npc_service_option
        serviceOptions[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
    const char *error = NULL;
    u32 actorId = 0;
    u32 x = 0;
    u32 y = 0;
    u32 kind = 0;
    u32 taskId = 0;
    u32 taskRepeatPolicy = VM_NET_MOCK_TASK_REPEAT_NEVER;
    u32 instanceX = 0;
    u32 instanceY = 0;
    u32 challengeEnemyId = 0;
    u32 instanceMinLevel = 1;
    u32 serviceOptionCount = 0;

    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(displayUtf8, 0, sizeof(displayUtf8));
    memset(scriptUtf8, 0, sizeof(scriptUtf8));
    memset(actorResource, 0, sizeof(actorResource));
    memset(instanceSceneUtf8, 0, sizeof(instanceSceneUtf8));
    memset(instanceRuntimeScene, 0, sizeof(instanceRuntimeScene));
    memset(enabledText, 0, sizeof(enabledText));
    memset(&seed, 0, sizeof(seed));
    memset(serviceOptions, 0, sizeof(serviceOptions));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)) ||
        !vm_mock_admin_form_u32(body, "actor_id", 0xffffffffu, &actorId) ||
        actorId == 0)
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "场景或 Actor ID 无效");
        return;
    }

    if (strcmp(action, "save-npc-inventory") == 0 ||
        strcmp(action, "delete-npc-inventory") == 0 ||
        strcmp(action, "save-npc-inventory-bulk") == 0 ||
        strcmp(action, "delete-npc-inventory-bulk") == 0)
    {
        vm_net_mock_dynamic_npc_admin_row dynamicRows[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
        vm_net_mock_native_npc_admin_row nativeRows[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
        u16 serviceKind = VM_NET_MOCK_NPC_KIND_NORMAL;
        vm_net_mock_npc_service_option
            configuredServices[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
        u32 configuredServiceCount = 0;
        u32 dynamicCount = vm_net_mock_dynamic_npc_admin_list(
            runtimeScene, dynamicRows, VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX);
        u32 nativeCount = vm_net_mock_native_npc_admin_list(
            runtimeScene, nativeRows, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX);
        u32 itemId = 0;
        u32 unitPrice = 0;
        u32 enabled = 0;
        bool found = false;
        bool dynamicFound = false;
        bool dynamicLookupReady = false;

        memset(configuredServices, 0, sizeof(configuredServices));
        if (!vm_mock_admin_form_u32(body, "service_kind",
                                    VM_NET_MOCK_NPC_KIND_MAX, &kind) ||
            kind == VM_NET_MOCK_NPC_KIND_NORMAL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "库存服务类型无效或页面已过期");
            return;
        }

        /* The dynamic-NPC cache is a scene-delivery cache, not the durable
         * parent index for an admin write.  A freshly saved row must be
         * addressable here even if the browser reaches a service instance
         * whose runtime cache predates that save. */
        dynamicLookupReady = vm_net_mock_dynamic_npc_admin_lookup_exact_kind(
            runtimeScene, actorId, &dynamicFound, &serviceKind);
        if (!dynamicLookupReady)
        {
            printf("[error][mock-admin] npc_inventory_parent_lookup_failed action=%s scene=%s actor=%u error=%s\n",
                   action, runtimeScene, actorId, vm_mysql_last_error());
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "无法读取 NPC 父记录，请稍后重试");
            return;
        }
        found = dynamicFound;
        if (!found)
        {
            for (u32 i = 0; i < nativeCount; ++i)
            {
                if (nativeRows[i].seed.actorId == actorId)
                {
                    serviceKind = nativeRows[i].seed.kind;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            /* Inventory posts carry only the parent identity.  Do not infer a
             * service kind from a stale page: the row could have been deleted
             * or moved since the editor was rendered. */
            printf("[warn][mock-admin] npc_inventory_target_missing action=%s scene=%s actor=%u dynamic_rows=%u native_rows=%u dynamic_db_found=%u\n",
                   action, runtimeScene, actorId, dynamicCount, nativeCount,
                   dynamicFound ? 1u : 0u);
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "该 NPC 已不存在或页面已过期，请刷新场景后重新编辑");
            return;
        }
        if (!vm_net_mock_npc_service_options_resolve(
                runtimeScene, actorId, serviceKind, NULL, NULL,
                configuredServices, VM_NET_MOCK_NPC_SERVICE_OPTION_MAX,
                &configuredServiceCount, NULL))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "无法读取 NPC 服务配置，请稍后重试");
            return;
        }
        serviceKind = (u16)kind;
        if (!vm_net_mock_npc_service_kind_uses_inventory(serviceKind) ||
            !vm_net_mock_npc_service_options_has_kind(
                configuredServices, configuredServiceCount, serviceKind))
        {
            printf("[warn][mock-admin] npc_inventory_service_rejected action=%s scene=%s actor=%u service=%u\n",
                   action, runtimeScene, actorId, serviceKind);
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "当前 NPC 服务类型不支持专属库存；仅武器、防具和药品商人可配置");
            return;
        }
        if ((strcmp(action, "save-npc-inventory-bulk") != 0 &&
             strcmp(action, "delete-npc-inventory-bulk") != 0) &&
            (!vm_mock_admin_form_u32(body, "item_id", 0xffffffffu, &itemId) ||
             itemId == 0))
        {
            printf("[warn][mock-admin] npc_inventory_item_missing action=%s scene=%s actor=%u\n",
                   action, runtimeScene, actorId);
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "库存物品参数无效");
            return;
        }
        if (strcmp(action, "save-npc-inventory-bulk") == 0 ||
            strcmp(action, "delete-npc-inventory-bulk") == 0)
        {
            u32 itemIds[VM_MOCK_ADMIN_NPC_STOCK_SELECTION_MAX];
            u32 itemCount = 0;

            memset(itemIds, 0, sizeof(itemIds));
            if (!vm_mock_admin_form_u32_csv(
                    body, "item_ids", itemIds,
                    VM_MOCK_ADMIN_NPC_STOCK_SELECTION_MAX, &itemCount))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               "库存多选列表无效");
                return;
            }
            if (strcmp(action, "delete-npc-inventory-bulk") == 0)
            {
                if (!vm_net_mock_npc_shop_inventory_admin_delete_many(
                        runtimeScene, actorId, itemIds, itemCount, &error))
                {
                    vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                                   error ? error : "库存商品批量删除失败");
                    return;
                }
                vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                               "已批量移除 NPC 专属库存商品");
                return;
            }
            if (!vm_mock_admin_form_optional_u32(body, "unit_price",
                                                 0xffffffffu, &unitPrice) ||
                !vm_mock_admin_form_u32(body, "enabled", 1, &enabled) ||
                !vm_net_mock_npc_shop_inventory_admin_save_many(
                    runtimeScene, actorId, serviceKind, itemIds, itemCount,
                    unitPrice, enabled != 0, &error))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               error ? error : "库存商品批量保存失败");
                return;
            }
            vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                           "NPC 专属库存已批量保存");
            return;
        }
        if (strcmp(action, "delete-npc-inventory") == 0)
        {
            if (!vm_net_mock_npc_shop_inventory_admin_delete(
                    runtimeScene, actorId, itemId, &error))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               error ? error : "库存商品删除失败");
                return;
            }
            vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                           "NPC 专属库存商品已移除");
            return;
        }
        if (!vm_mock_admin_form_optional_u32(body, "unit_price", 0xffffffffu,
                                             &unitPrice) ||
            !vm_mock_admin_form_u32(body, "enabled", 1, &enabled) ||
            !vm_net_mock_npc_shop_inventory_admin_save(
                runtimeScene, actorId, serviceKind, itemId, unitPrice, enabled != 0,
                &error))
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           error ? error : "库存商品保存失败");
            return;
        }
        vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                       "NPC 专属库存已保存");
        return;
    }

    if (strcmp(action, "save-native-npc-override") == 0 ||
        strcmp(action, "delete-native-npc-override") == 0 ||
        strcmp(action, "save-native-npc-inventory") == 0 ||
        strcmp(action, "delete-native-npc-inventory") == 0)
    {
        vm_net_mock_native_npc_admin_row nativeRows[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
        const vm_net_mock_native_npc_admin_row *nativeRow = NULL;
        u32 nativeCount = vm_net_mock_native_npc_admin_list(
            runtimeScene, nativeRows, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX);

        for (u32 i = 0; i < nativeCount; ++i)
        {
            if (nativeRows[i].seed.actorId == actorId)
            {
                nativeRow = &nativeRows[i];
                break;
            }
        }
        if (nativeRow == NULL)
        {
            vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                           "该场景不存在对应的原生 NPC");
            return;
        }
        if (strcmp(action, "delete-native-npc-override") == 0)
        {
            if (!vm_net_mock_native_npc_admin_delete_override(
                    runtimeScene, actorId, &error))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               error ? error : "原生 NPC 覆盖删除失败");
                return;
            }
            vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                           "已恢复 SCE 原生 NPC 默认服务");
            return;
        }
        if (strcmp(action, "save-native-npc-override") == 0)
        {
            u32 enabled = 0;
            vm_net_mock_npc_service_option
                nativeServices[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
            u32 nativeServiceCount = 0;

            memset(nativeServices, 0, sizeof(nativeServices));
            if (!vm_mock_admin_form_u32(body, "enabled", 1, &enabled) ||
                !vm_mock_admin_form_npc_service_options(
                    body, false, nativeServices,
                    VM_NET_MOCK_NPC_SERVICE_OPTION_MAX,
                    &nativeServiceCount))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               "服务配置或启用状态无效");
                return;
            }
            kind = nativeServiceCount != 0
                       ? nativeServices[0].kind
                       : VM_NET_MOCK_NPC_KIND_NORMAL;
            if (!vm_net_mock_native_npc_admin_save_override(
                    runtimeScene, actorId, (u16)kind, enabled != 0,
                    nativeServices, nativeServiceCount, true, &error))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               error ? error : "原生 NPC 覆盖保存失败");
                return;
            }
            vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                           "原生 NPC 服务覆盖已保存");
            return;
        }
        else
        {
            u32 itemId = 0;
            u32 unitPrice = 0;
            u32 enabled = 0;

            if (!vm_net_mock_npc_service_kind_uses_inventory(
                    nativeRow->seed.kind))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               "当前服务类型不使用商品库存");
                return;
            }
            if (!vm_mock_admin_form_u32(body, "item_id", 0xffffffffu,
                                        &itemId) || itemId == 0)
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               "物品参数无效");
                return;
            }
            if (strcmp(action, "delete-native-npc-inventory") == 0)
            {
                if (!vm_net_mock_npc_shop_inventory_admin_delete(
                        runtimeScene, actorId, itemId, &error))
                {
                    vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                                   error ? error : "库存商品删除失败");
                    return;
                }
                vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                               "NPC 专属库存商品已移除");
                return;
            }
            if (!vm_mock_admin_form_optional_u32(body, "unit_price",
                                                 0xffffffffu, &unitPrice) ||
                !vm_mock_admin_form_u32(body, "enabled", 1, &enabled))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               "单价或上架状态无效");
                return;
            }
            if (!vm_net_mock_npc_shop_inventory_admin_save(
                    runtimeScene, actorId, nativeRow->seed.kind, itemId,
                    unitPrice, enabled != 0,
                    &error))
            {
                vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                               error ? error : "库存商品保存失败");
                return;
            }
            vm_mock_admin_redirect_content(client, sceneUtf8, "ok",
                                           "NPC 专属库存已保存");
            return;
        }
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
            !vm_net_mock_dynamic_npc_actor_resource_is_supported(
                seed.actorResource))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                "该 Actor 不支持动态 NPC；请编辑后改选 n_woman1.actor");
            return;
        }
        if (found && enabled &&
            !vm_net_mock_ensure_actor_resource_available(
                seed.actorResource, &error))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                error ? error :
                        "Actor 资源无效或引用图片不完整");
            return;
        }
        if (!found || !vm_net_mock_dynamic_npc_admin_save(
                          runtimeScene, &seed, enabled, NULL, 0, false,
                          &error))
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
        !vm_mock_admin_form_u32(body, "task_id", 0xffffffffu, &taskId) ||
        !vm_mock_admin_form_u32(body, "task_repeat_policy",
                                VM_NET_MOCK_TASK_REPEAT_MONTHLY,
                                &taskRepeatPolicy) ||
        !vm_mock_admin_form_value(body, "display_name", displayUtf8,
                                  sizeof(displayUtf8)) ||
        !vm_mock_admin_form_value(body, "actor_resource", actorResource,
                                  sizeof(actorResource)) ||
        !vm_mock_admin_form_value(body, "script_name", scriptUtf8,
                                  sizeof(scriptUtf8)) ||
        !vm_mock_admin_form_npc_service_options(
            body, true, serviceOptions,
            VM_NET_MOCK_NPC_SERVICE_OPTION_MAX, &serviceOptionCount))
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
    if (taskId == 0 && taskRepeatPolicy != VM_NET_MOCK_TASK_REPEAT_NEVER)
    {
        vm_mock_admin_redirect_content(client, sceneUtf8, "error",
                                       "只有绑定任务的 NPC 才能设置重复接取");
        return;
    }
    kind = serviceOptionCount != 0 ? serviceOptions[0].kind
                                   : VM_NET_MOCK_NPC_KIND_NORMAL;
    if (vm_net_mock_npc_service_options_has_kind(
            serviceOptions, serviceOptionCount,
            VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE))
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
        /* A guide without a destination scene launches a native battle from
         * a kind-3 record in this exact SCE.  Such a target is configured in
         * the scene-battle-monster layer and is not required to have already
         * appeared in the generic monster catalog.  Destination-scene guides
         * retain the normal catalog contract. */
        if (challengeEnemyId != 0 &&
            ((instanceRuntimeScene[0] == 0 &&
              !vm_net_mock_scene_battle_monster_configured_target_exists(
                  runtimeScene, challengeEnemyId)) ||
             (instanceRuntimeScene[0] != 0 &&
              !vm_net_mock_monster_enemy_id_known(challengeEnemyId))))
        {
            vm_mock_admin_redirect_content(
                client, sceneUtf8, "error",
                instanceRuntimeScene[0] == 0
                    ? "挑战怪物必须是当前场景中已启用的场景战斗怪"
                    : "挑战怪物 ID 不在当前服务端怪物目录中");
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
    if (!vm_net_mock_dynamic_npc_actor_resource_is_supported(actorResource))
    {
        vm_mock_admin_redirect_content(
            client, sceneUtf8, "error",
            "n_girl.actor 不支持动态 NPC；请改选 n_woman1.actor");
        return;
    }
    if (!vm_net_mock_ensure_actor_resource_available(actorResource, &error))
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
                                       "NPC 名称或脚本编码失败");
        return;
    }
    if (serviceOptionCount != 0)
    {
        /* Keep one legacy projection for old service instances that have not
         * yet loaded server_npc_services.  New runtime resolution prefers the
         * explicit multi-service rows written below. */
        snprintf(seed.serviceOptionName, sizeof(seed.serviceOptionName), "%s",
                 serviceOptions[0].optionName);
        snprintf(seed.serviceOptionDescription,
                 sizeof(seed.serviceOptionDescription), "%s",
                 serviceOptions[0].optionDescription);
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
    seed.taskRepeatPolicy = (u8)taskRepeatPolicy;
    seed.taskRepeatable = seed.taskRepeatPolicy !=
                          VM_NET_MOCK_TASK_REPEAT_NEVER;
    seed.instanceX = (u16)instanceX;
    seed.instanceY = (u16)instanceY;
    seed.challengeEnemyId = challengeEnemyId;
    seed.instanceMinLevel = (u16)instanceMinLevel;
    snprintf(seed.instanceScene, sizeof(seed.instanceScene), "%s",
             instanceRuntimeScene);
    snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", actorResource);
    if (!vm_net_mock_dynamic_npc_admin_save(
            runtimeScene, &seed, true, serviceOptions, serviceOptionCount,
            true, &error))
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
    bool contentChanged = false;
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
    if (strcmp(action, "clear-content-update") == 0)
    {
        if (!vm_net_mock_content_update_admin_clear(&error))
        {
            vm_mock_admin_redirect_updates(
                client, "error",
                error ? error : "游戏数据内容更新清空失败");
            return;
        }
        vm_mock_admin_redirect_updates(client, "ok",
                                       "游戏数据内容更新已清空");
        return;
    }
    if (strcmp(action, "republish-content-update") == 0)
    {
        if (!vm_net_mock_content_update_admin_republish(&error,
                                                        &contentChanged))
        {
            vm_mock_admin_redirect_updates(
                client, "error",
                error ? error : "游戏数据内容重新发布失败");
            return;
        }
        vm_mock_admin_redirect_updates(
            client, "ok",
            contentChanged ?
                "检测到资源字节变化，已发布新版本；客户端完整重启后会重新安装" :
                "资源字节未变化；保留当前版本，客户端不会重复下载");
        return;
    }
    if (strcmp(action, "add-content-update-files") == 0)
    {
        char *resourceUtf8Values = NULL;
        char *resourceGbkValues = NULL;
        const char **names = NULL;
        u32 nameCount = 0;
        bool valid = false;

        resourceUtf8Values = (char *)calloc(
            VM_NET_MOCK_CONTENT_UPDATE_FILE_MAX, sizeof(resourceUtf8));
        resourceGbkValues = (char *)calloc(
            VM_NET_MOCK_CONTENT_UPDATE_FILE_MAX, sizeof(resourceGbk));
        names = (const char **)calloc(VM_NET_MOCK_CONTENT_UPDATE_FILE_MAX,
                                      sizeof(*names));
        if (resourceUtf8Values == NULL || resourceGbkValues == NULL ||
            names == NULL ||
            !vm_mock_admin_form_values(
                body, "resource", resourceUtf8Values,
                sizeof(resourceUtf8), VM_NET_MOCK_CONTENT_UPDATE_FILE_MAX,
                &nameCount))
        {
            free(resourceUtf8Values);
            free(resourceGbkValues);
            free(names);
            vm_mock_admin_redirect_updates(client, "error",
                                           "请至少选择一个游戏数据资源");
            return;
        }
        valid = true;
        for (u32 i = 0; i < nameCount; ++i)
        {
            char *utf8 = resourceUtf8Values +
                         (size_t)i * sizeof(resourceUtf8);
            char *gbk = resourceGbkValues +
                        (size_t)i * sizeof(resourceGbk);

            if (!vm_mock_admin_utf8_to_gbk_text(utf8, gbk,
                                                sizeof(resourceGbk), false) ||
                !vm_net_mock_content_update_name_is_managed_resource(gbk))
            {
                valid = false;
                break;
            }
            names[i] = gbk;
        }
        if (!valid || !vm_net_mock_content_update_publish_files(
                          names, nameCount, &error, &contentChanged))
        {
            free(resourceUtf8Values);
            free(resourceGbkValues);
            free(names);
            vm_mock_admin_redirect_updates(
                client, "error",
                error ? error : "游戏数据内容发布失败");
            return;
        }
        free(resourceUtf8Values);
        free(resourceGbkValues);
        free(names);
        vm_mock_admin_redirect_updates(
            client, "ok",
            contentChanged ?
                "检测到新增或变更资源，已发布新版本；客户端完整重启后会安装" :
                "选中资源与当前发布版本一致；客户端不会重复下载");
        return;
    }
    if (strcmp(action, "remove-content-update-file") != 0 ||
        !vm_mock_admin_form_value(body, "resource", resourceUtf8,
                                  sizeof(resourceUtf8)) ||
        !vm_mock_admin_utf8_to_gbk_text(resourceUtf8, resourceGbk,
                                        sizeof(resourceGbk), false) ||
        !vm_net_mock_content_update_name_is_managed_resource(resourceGbk))
    {
        vm_mock_admin_redirect_updates(client, "error", "内容更新资源名称无效");
        return;
    }
    if (!vm_net_mock_content_update_admin_remove_file(resourceGbk, &error))
    {
        vm_mock_admin_redirect_updates(
            client, "error", error ? error : "游戏数据内容更新移除失败");
        return;
    }
    vm_mock_admin_redirect_updates(client, "ok", "资源已从内容更新中移除");
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
    u32 shopSection = VM_NET_MOCK_SHOP_SECTION_AUTO;
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
        !vm_mock_admin_form_u32(body, "shop_section",
                                VM_NET_MOCK_SHOP_SECTION_NORMAL,
                                &shopSection) ||
        !vm_mock_admin_form_value(body, "enabled", enabledText,
                                  sizeof(enabledText)) ||
        (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0))
    {
        vm_mock_admin_redirect_shop(client, category, search, page, "error",
                                    "商品、价格、商城分区或上下架状态无效");
        return;
    }
    enabled = strcmp(enabledText, "1") == 0;
    if (!vm_net_mock_shop_admin_save(itemId, price, enabled,
                                     (u8)shopSection, &error))
    {
        vm_mock_admin_redirect_shop(
            client, category, search, page, "error",
            error ? error : "商品保存失败");
        return;
    }
    vm_mock_admin_redirect_shop(
        client, category, search, page, "ok",
        enabled ? "商品配置已保存并上架" : "商品配置已保存并下架");
}

static void vm_mock_admin_handle_chest_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body)
{
    vm_net_mock_chest_admin_row chest;
    const char *error = NULL;
    int chestIndex = -1;

    memset(&chest, 0, sizeof(chest));
    if (!vm_mock_admin_form_u32(body, "chest_item_id", 0xffffffffu,
                                &chest.chestItemId) ||
        (chestIndex = vm_net_mock_chest_kind_index(chest.chestItemId)) < 0)
    {
        vm_mock_admin_redirect_chests(client, 0, "error", "宝箱类型无效");
        return;
    }
    chest.keyItemId = g_vm_net_mock_chest_kinds[chestIndex].keyItemId;
    if (strcmp(action, "reset-chest-rewards") == 0)
    {
        if (!vm_net_mock_chest_admin_reset(chest.chestItemId, &error))
        {
            vm_mock_admin_redirect_chests(
                client, chest.chestItemId, "error",
                error ? error : "清空宝箱奖池失败");
            return;
        }
        vm_mock_admin_redirect_chests(client, chest.chestItemId, "ok",
                                      "宝箱奖池已清空；开启时将不消耗物品");
        return;
    }
    if (strcmp(action, "save-chest-rewards") != 0)
    {
        vm_mock_admin_redirect_chests(client, chest.chestItemId, "error",
                                      "未知宝箱管理操作");
        return;
    }
    for (u8 slot = 0; slot < VM_NET_MOCK_CHEST_REWARD_MAX; ++slot)
    {
        char itemField[64];
        char countField[64];
        char weightField[64];
        char broadcastField[64];
        char broadcastText[8];
        u32 itemId = 0;
        u32 count = 0;
        u32 weight = 0;
        bool worldBroadcast = false;

        memset(broadcastText, 0, sizeof(broadcastText));
        snprintf(itemField, sizeof(itemField), "reward_item_id_%u", (u32)slot);
        snprintf(countField, sizeof(countField), "reward_count_%u", (u32)slot);
        snprintf(weightField, sizeof(weightField), "reward_weight_%u", (u32)slot);
        snprintf(broadcastField, sizeof(broadcastField),
                 "reward_world_broadcast_%u", (u32)slot);
        if (!vm_mock_admin_form_u32(body, itemField, 0xffffffffu, &itemId) ||
            !vm_mock_admin_form_u32(body, countField,
                                    VM_NET_MOCK_CHEST_REWARD_COUNT_MAX,
                                    &count) ||
            !vm_mock_admin_form_u32(body, weightField,
                                    VM_NET_MOCK_CHEST_REWARD_WEIGHT_MAX,
                                    &weight))
        {
            vm_mock_admin_redirect_chests(
                client, chest.chestItemId, "error",
                "宝箱奖池字段不完整或数值越界");
            return;
        }
        if (vm_mock_admin_form_value(body, broadcastField, broadcastText,
                                     sizeof(broadcastText)))
        {
            if (strcmp(broadcastText, "1") != 0)
            {
                vm_mock_admin_redirect_chests(
                    client, chest.chestItemId, "error",
                    "世界播报开关无效");
                return;
            }
            worldBroadcast = true;
        }
        if (itemId == 0 && count == 0 && weight == 0)
            continue;
        if (itemId == 0 || count == 0 || weight == 0 ||
            chest.rewardCount >= VM_NET_MOCK_CHEST_REWARD_MAX)
        {
            vm_mock_admin_redirect_chests(
                client, chest.chestItemId, "error",
                "每条奖池必须同时选择物品、数量和权重");
            return;
        }
        chest.rewards[chest.rewardCount].itemId = itemId;
        chest.rewards[chest.rewardCount].count = count;
        chest.rewards[chest.rewardCount].weight = weight;
        chest.rewards[chest.rewardCount].worldBroadcast =
            worldBroadcast ? 1 : 0;
        ++chest.rewardCount;
    }
    if (chest.rewardCount == 0)
    {
        vm_mock_admin_redirect_chests(
            client, chest.chestItemId, "error",
            "至少配置一项奖励；如需停用请使用清空奖池");
        return;
    }
    if (!vm_net_mock_chest_admin_save(&chest, &error))
    {
        vm_mock_admin_redirect_chests(
            client, chest.chestItemId, "error",
            error ? error : "宝箱奖池保存失败");
        return;
    }
    vm_mock_admin_redirect_chests(client, chest.chestItemId, "ok",
                                  "宝箱奖池已保存并立即生效");
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

static void vm_mock_admin_handle_scene_battle_monster_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    vm_net_mock_scene_battle_monster_admin_row row;
    char sceneUtf8[192];
    char runtimeScene[64];
    char nameUtf8[128];
    char actorUtf8[192];
    char effectUtf8[192];
    char enabledText[8];
    const char *error = NULL;
    u32 monsterId = 0;
    u32 posX = 0;
    u32 posY = 0;
    u32 visualHint = 0;

    memset(&row, 0, sizeof(row));
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(actorUtf8, 0, sizeof(actorUtf8));
    memset(effectUtf8, 0, sizeof(effectUtf8));
    memset(enabledText, 0, sizeof(enabledText));
    if (!vm_mock_admin_scene_from_form(body, sceneUtf8, sizeof(sceneUtf8),
                                       runtimeScene, sizeof(runtimeScene)))
    {
        vm_mock_admin_redirect_scene_battle_monsters(
            client, sceneUtf8, "error", "场景参数无效或服务端资源不存在");
        return;
    }
    if (strcmp(action, "deploy-scene-battle-monsters") == 0)
    {
        if (!vm_net_mock_scene_battle_monster_admin_deploy(runtimeScene,
                                                            &error))
        {
            vm_mock_admin_redirect_scene_battle_monsters(
                client, sceneUtf8, "error",
                error ? error : "场景战斗怪部署失败");
            return;
        }
        vm_mock_admin_redirect_scene_battle_monsters(
            client, sceneUtf8, "ok",
            "场景战斗怪已验证、部署并加入启动内容版本；客户端需完整退出后重新启动，再进入该场景");
        return;
    }
    if (!vm_mock_admin_form_u32(body, "entry_id", 0xffffffffu,
                                &row.entryId))
    {
        vm_mock_admin_redirect_scene_battle_monsters(
            client, sceneUtf8, "error", "场景战斗怪记录 ID 无效");
        return;
    }
    if (strcmp(action, "delete-scene-battle-monster") == 0)
    {
        if (row.entryId == 0 ||
            !vm_net_mock_scene_battle_monster_admin_delete(runtimeScene,
                                                            row.entryId,
                                                            &error))
        {
            vm_mock_admin_redirect_scene_battle_monsters(
                client, sceneUtf8, "error",
                error ? error : "删除场景战斗怪草稿失败");
            return;
        }
        vm_mock_admin_redirect_scene_battle_monsters(
            client, sceneUtf8, "ok", "场景战斗怪草稿已删除；请部署以更新场景资源");
        return;
    }
    if (strcmp(action, "save-scene-battle-monster") != 0 ||
        !vm_mock_admin_form_u32(body, "monster_id", 0xffffu, &monsterId) ||
        monsterId == 0 ||
        !vm_mock_admin_form_u32(body, "pos_x", 0xffffu, &posX) ||
        posX == 0 ||
        !vm_mock_admin_form_u32(body, "pos_y", 0xffffu, &posY) ||
        posY == 0 ||
        !vm_mock_admin_form_u32(body, "visual_hint", 6u, &visualHint) ||
        (visualHint != 5u && visualHint != 6u) ||
        !vm_mock_admin_form_value(body, "enabled", enabledText,
                                  sizeof(enabledText)) ||
        (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0) ||
        !vm_mock_admin_form_value(body, "display_name", nameUtf8,
                                  sizeof(nameUtf8)) ||
        !vm_mock_admin_form_value(body, "actor_resource", actorUtf8,
                                  sizeof(actorUtf8)) ||
        !vm_mock_admin_form_value(body, "effect_resource", effectUtf8,
                                  sizeof(effectUtf8)) ||
        !vm_mock_admin_utf8_to_gbk_text(nameUtf8, row.displayName,
                                        sizeof(row.displayName), false) ||
        !vm_mock_admin_utf8_to_gbk_text(actorUtf8, row.actorResource,
                                        sizeof(row.actorResource), false) ||
        !vm_mock_admin_utf8_to_gbk_text(effectUtf8, row.effectResource,
                                        sizeof(row.effectResource), false))
    {
        vm_mock_admin_redirect_scene_battle_monsters(
            client, sceneUtf8, "error",
            "怪物 ID、名称、Actor、效果 Actor、坐标、视觉提示或状态无效");
        return;
    }
    row.monsterId = (u16)monsterId;
    row.x = (u16)posX;
    row.y = (u16)posY;
    row.visualHint = (u16)visualHint;
    row.enabled = strcmp(enabledText, "1") == 0;
    if (!vm_net_mock_scene_battle_monster_admin_save(runtimeScene, &row,
                                                      &error))
    {
        vm_mock_admin_redirect_scene_battle_monsters(
            client, sceneUtf8, "error",
            error ? error : "保存场景战斗怪草稿失败");
        return;
    }
    vm_mock_admin_redirect_scene_battle_monsters(
        client, sceneUtf8, "ok",
        row.entryId == 0 ? "场景战斗怪草稿已新增；请部署后客户端才会创建战斗节点" :
                           "场景战斗怪草稿已保存；请部署后客户端才会更新战斗节点");
}

static void vm_mock_admin_handle_actor_resource_action(
    vm_mock_service_socket client, const char *body)
{
    vm_mock_admin_actor_manifest manifest;
    char actorName[64];
    char mode[16];
    char path[1200];
    u8 *previousRaw = NULL;
    u32 previousRawLen = 0;
    u8 *raw = NULL;
    u32 rawLen = 0;
    const char *publishNames[1];
    const char *error = NULL;
    bool newMode = false;
    bool existed = false;
    bool pathExists = false;
    bool restored = false;
    bool contentChanged = false;

    memset(&manifest, 0, sizeof(manifest));
    memset(actorName, 0, sizeof(actorName));
    memset(mode, 0, sizeof(mode));
    memset(path, 0, sizeof(path));
    if (!vm_mock_admin_form_value(body, "actor_name", actorName,
                                  sizeof(actorName)) ||
        !vm_mock_admin_form_value(body, "mode", mode, sizeof(mode)) ||
        !vm_mock_admin_actor_name_is_writable(actorName) ||
        (strcmp(mode, "edit") != 0 && strcmp(mode, "new") != 0))
    {
        vm_mock_admin_redirect_actors(client, actorName, "error",
                                      "Actor 文件名或操作模式无效");
        return;
    }
    newMode = strcmp(mode, "new") == 0;
    if (!vm_net_mock_update_resource_path(actorName, path, sizeof(path)))
    {
        vm_mock_admin_redirect_actors(client, actorName, "error",
                                      "服务端权威资源目录不可写或未配置");
        return;
    }
    pathExists = vm_host_file_exists(path);
    existed = vm_mock_admin_read_raw_resource_file(path, &previousRaw,
                                                    &previousRawLen);
    if (pathExists && !existed)
    {
        vm_mock_admin_redirect_actors(
            client, actorName, "error",
            "现有 Actor 无法完整读取；为保护权威资源，已拒绝覆盖");
        return;
    }
    if ((newMode && existed) || (!newMode && !existed))
    {
        free(previousRaw);
        vm_mock_admin_redirect_actors(
            client, actorName, "error",
            newMode ? "同名 Actor 已存在，请更换新文件名或编辑已有资源"
                    : "该 Actor 已不存在或资源目录已切换，请刷新后重试");
        return;
    }
    if (!vm_mock_admin_actor_manifest_from_form(body, &manifest) ||
        !vm_mock_admin_actor_manifest_encode_raw(&manifest, &raw, &rawLen))
    {
        vm_mock_admin_actor_manifest_free(&manifest);
        free(previousRaw);
        free(raw);
        vm_mock_admin_redirect_actors(
            client, actorName, "error",
            "Actor 结构无效：请检查图片、矩形、部件和帧的行格式及顺序");
        return;
    }
    vm_mock_admin_actor_manifest_free(&manifest);
    if (!vm_mock_admin_write_actor_resource_atomic(path, raw, rawLen))
    {
        free(previousRaw);
        free(raw);
        vm_mock_admin_redirect_actors(client, actorName, "error",
                                      "Actor 临时文件写入或原子替换失败");
        return;
    }
    /* This reopens the just-written server resource and validates the exact
     * type-2 payload plus all GIF dependencies.  The previous bytes are kept
     * until both this check and MySQL-backed content publication succeed. */
    if (!vm_net_mock_ensure_actor_resource_available(actorName, &error))
    {
        if (existed)
            restored = vm_mock_admin_write_actor_resource_atomic(
                path, previousRaw, previousRawLen);
        else
            restored = remove(path) == 0;
        free(previousRaw);
        free(raw);
        vm_mock_admin_redirect_actors(
            client, actorName, "error",
            restored ? "Actor 引用图片或客户端格式校验失败，已恢复原资源"
                     : "Actor 校验失败，且恢复原资源失败；请立即检查资源目录");
        return;
    }
    publishNames[0] = actorName;
    if (!vm_net_mock_content_update_publish_files(publishNames, 1, &error,
                                                  &contentChanged))
    {
        if (existed)
            restored = vm_mock_admin_write_actor_resource_atomic(
                path, previousRaw, previousRawLen);
        else
            restored = remove(path) == 0;
        free(previousRaw);
        free(raw);
        vm_mock_admin_redirect_actors(
            client, actorName, "error",
            restored ? "内容更新发布失败，已恢复原 Actor"
                     : "内容更新发布失败，且恢复原 Actor 失败；请立即检查资源目录");
        return;
    }
    printf("[info][mock-admin] actor_resource_save resource=%s mode=%s raw=%u "
           "publish=WT18/9+18/8->18/7\n",
           actorName, newMode ? "new" : "edit", rawLen);
    free(previousRaw);
    free(raw);
    vm_mock_admin_redirect_actors(
        client, actorName, "ok",
        contentChanged ?
            (newMode ? "Actor 已新建、校验并发布；客户端完整重启后会重新下载"
                     : "Actor 已原子保存、校验并发布；客户端完整重启后会重新下载") :
            "Actor 已保存并通过校验；文件字节未变，客户端不会重复下载");
}

static void vm_mock_admin_handle_dsh_row_action(vm_mock_service_socket client,
                                                const char *body)
{
    char resourceUtf8[256];
    char resource[128];
    char path[1200];
    char fieldName[40];
    char valueUtf8[1024];
    char valueGbk[VM_MOCK_ADMIN_DSH_VALUE_MAX + 1u];
    u8 *raw = NULL;
    u8 *newRaw = NULL;
    u8 *verifyRaw = NULL;
    u8 *newRow = NULL;
    u32 rawLen = 0;
    u32 verifyRawLen = 0;
    u32 selectedRow = 0;
    u32 expectedFingerprint = 0;
    u32 rowOffset = 0;
    u32 rowLen = 0;
    u32 newRowLen = 0;
    u32 newRawLen = 0;
    vm_mock_admin_dsh_table table;
    vm_mock_admin_dsh_table verifyTable;
    const char *publishNames[1];
    const char *publishError = NULL;
    const char *failure = NULL;
    bool changed = false;
    bool wrote = false;
    bool restored = false;
    char failureMessage[384];

    memset(resourceUtf8, 0, sizeof(resourceUtf8));
    memset(resource, 0, sizeof(resource));
    memset(path, 0, sizeof(path));
    memset(fieldName, 0, sizeof(fieldName));
    memset(valueUtf8, 0, sizeof(valueUtf8));
    memset(valueGbk, 0, sizeof(valueGbk));
    memset(failureMessage, 0, sizeof(failureMessage));
    memset(&table, 0, sizeof(table));
    memset(&verifyTable, 0, sizeof(verifyTable));
    if (!vm_mock_admin_content_resource_from_query(
            body, "resource", ".dsh", resourceUtf8, sizeof(resourceUtf8),
            resource, sizeof(resource)) ||
        !vm_mock_admin_form_u32(body, "dsh_row", 0xffffffffu,
                                &selectedRow) ||
        !vm_mock_admin_form_u32(body, "row_fingerprint", 0xffffffffu,
                                &expectedFingerprint) ||
        !vm_net_mock_update_resource_path(resource, path, sizeof(path)) ||
        !vm_mock_admin_read_raw_resource_file(path, &raw, &rawLen) ||
        !vm_mock_admin_dsh_table_parse(raw, rawLen, &table) ||
        selectedRow >= table.rowCount ||
        !vm_mock_admin_dsh_row_at(raw, rawLen, &table, selectedRow,
                                  &rowOffset, &rowLen) ||
        rowOffset < 4u)
    {
        vm_mock_admin_redirect_dsh_row(
            client, resourceUtf8, selectedRow, "error",
            "DSH 文件、行号或表结构无效；未写入任何数据");
        free(raw);
        return;
    }
    if (vm_mock_admin_dsh_row_fingerprint(raw + rowOffset, rowLen) !=
        expectedFingerprint)
    {
        vm_mock_admin_redirect_dsh_row(
            client, resourceUtf8, selectedRow, "error",
            "该行已被其他操作修改，请重新打开后再保存");
        free(raw);
        return;
    }
    newRow = (u8 *)malloc((size_t)table.columnCount *
                          (VM_MOCK_ADMIN_DSH_VALUE_MAX + 1u));
    if (newRow == NULL)
    {
        vm_mock_admin_redirect_dsh_row(client, resourceUtf8, selectedRow,
                                       "error", "内存不足，未写入 DSH");
        free(raw);
        return;
    }
    for (u32 column = 0; column < table.columnCount; ++column)
    {
        size_t valueLen = 0;

        snprintf(fieldName, sizeof(fieldName), "dsh_value_%u", column);
        memset(valueUtf8, 0, sizeof(valueUtf8));
        memset(valueGbk, 0, sizeof(valueGbk));
        if (!vm_mock_admin_form_value(body, fieldName, valueUtf8,
                                      sizeof(valueUtf8)) ||
            !vm_mock_admin_utf8_to_gbk_text(valueUtf8, valueGbk,
                                            sizeof(valueGbk), false) ||
            (valueLen = strlen(valueGbk)) > VM_MOCK_ADMIN_DSH_VALUE_MAX)
        {
            failure = "DSH 字段缺失，或内容无法转换为游戏使用的 GBK 文本";
            goto done;
        }
        newRow[newRowLen++] = (u8)valueLen;
        if (valueLen != 0)
        {
            memcpy(newRow + newRowLen, valueGbk, valueLen);
            newRowLen += (u32)valueLen;
        }
    }
    if (newRowLen == 0 || rowLen > rawLen || rawLen - rowLen > 0xffffffffu - newRowLen)
    {
        failure = "DSH 行长度无效，未写入数据";
        goto done;
    }
    newRawLen = rawLen - rowLen + newRowLen;
    newRaw = (u8 *)malloc(newRawLen);
    if (newRaw == NULL)
    {
        failure = "内存不足，未写入 DSH";
        goto done;
    }
    memcpy(newRaw, raw, rowOffset - 4u);
    vm_mock_admin_preview_write_le32(newRaw, rowOffset - 4u, newRowLen);
    memcpy(newRaw + rowOffset, newRow, newRowLen);
    memcpy(newRaw + rowOffset + newRowLen, raw + rowOffset + rowLen,
           rawLen - rowOffset - rowLen);
    vm_mock_admin_preview_write_le32(newRaw, 0, newRawLen - 4u);
    if (!vm_mock_admin_dsh_raw_is_valid(newRaw, newRawLen))
    {
        failure = "DSH 行保存后的完整表校验失败，未写入数据";
        goto done;
    }
    if (newRawLen == rawLen && memcmp(newRaw, raw, rawLen) == 0)
    {
        vm_mock_admin_redirect_dsh_row(
            client, resourceUtf8, selectedRow, "ok",
            "该行没有变化，未重复发布内容更新");
        goto done;
    }
    if (!vm_mock_admin_write_resource_atomic(path, newRaw, newRawLen))
    {
        failure = "DSH 原子写入失败，原文件未替换";
        goto done;
    }
    wrote = true;
    if (!vm_mock_admin_read_raw_resource_file(path, &verifyRaw,
                                              &verifyRawLen) ||
        verifyRawLen != newRawLen ||
        !vm_mock_admin_dsh_table_parse(verifyRaw, verifyRawLen,
                                       &verifyTable) ||
        selectedRow >= verifyTable.rowCount ||
        !vm_mock_admin_dsh_row_at(verifyRaw, verifyRawLen, &verifyTable,
                                  selectedRow, &rowOffset, &rowLen) ||
        vm_mock_admin_dsh_row_fingerprint(verifyRaw + rowOffset, rowLen) !=
            vm_mock_admin_dsh_row_fingerprint(newRow, newRowLen))
    {
        failure = "DSH 写入后的回读校验失败";
        goto done;
    }
    publishNames[0] = resource;
    if (!vm_net_mock_content_update_publish_files(publishNames, 1,
                                                  &publishError, &changed))
    {
        failure = publishError ? publishError : "内容更新发布失败";
        goto done;
    }
    printf("[info][mock-admin] dsh_row_save resource=%s row=%u old=%u new=%u publish=WT18/9+18/8->18/7\n",
           resource, selectedRow, rawLen, newRawLen);
    vm_mock_admin_redirect_dsh_row(
        client, resourceUtf8, selectedRow, "ok",
        changed ? "DSH 行已保存、回读校验并发布；下次资源加载会下载更新" :
                  "DSH 行已保存并校验；内容版本未变化");

done:
    if (failure != NULL)
    {
        if (wrote)
            restored = vm_mock_admin_write_resource_atomic(path, raw, rawLen);
        snprintf(failureMessage, sizeof(failureMessage), "%s%s",
                 restored ? "操作失败，已恢复原始 DSH：" : "操作失败：",
                 failure);
        vm_mock_admin_redirect_dsh_row(
            client, resourceUtf8, selectedRow, "error", failureMessage);
    }
    free(verifyRaw);
    free(newRaw);
    free(newRow);
    free(raw);
}

static void vm_mock_admin_handle_action(vm_mock_service_socket client, const char *body)
{
    char action[32];
    char account[64];
    char password[64];
    char role[64];
    char itemText[32];
    char amountText[32];
    char levelText[32];
    char roleNameUtf8[128];
    char roleNameGbk[32];
    char resetSceneUtf8[192];
    char resetRuntimeScene[64];
    const char *error = NULL;
    u32 itemId = 0;
    u32 amount = 0;
    u32 level = 0;
    u16 itemSeq = 0;
    bool ok = false;

    memset(action, 0, sizeof(action));
    memset(account, 0, sizeof(account));
    memset(password, 0, sizeof(password));
    memset(role, 0, sizeof(role));
    memset(itemText, 0, sizeof(itemText));
    memset(amountText, 0, sizeof(amountText));
    memset(levelText, 0, sizeof(levelText));
    memset(roleNameUtf8, 0, sizeof(roleNameUtf8));
    memset(roleNameGbk, 0, sizeof(roleNameGbk));
    memset(resetSceneUtf8, 0, sizeof(resetSceneUtf8));
    memset(resetRuntimeScene, 0, sizeof(resetRuntimeScene));
    if (!vm_mock_admin_form_value(body, "action", action, sizeof(action)))
    {
        vm_mock_admin_redirect(client, "", "error", "请求参数不完整");
        return;
    }
    if (strcmp(action, "save-dsh-row") == 0)
    {
        vm_mock_admin_handle_dsh_row_action(client, body);
        return;
    }
    if (strcmp(action, "save-sce-portal-target") == 0)
    {
        vm_mock_admin_handle_sce_portal_action(client, body);
        return;
    }
    if (strcmp(action, "save-npc") == 0 ||
        strcmp(action, "toggle-npc") == 0 ||
        strcmp(action, "delete-npc-override") == 0 ||
        strcmp(action, "save-npc-inventory") == 0 ||
        strcmp(action, "delete-npc-inventory") == 0 ||
        strcmp(action, "save-npc-inventory-bulk") == 0 ||
        strcmp(action, "delete-npc-inventory-bulk") == 0 ||
        strcmp(action, "save-native-npc-override") == 0 ||
        strcmp(action, "delete-native-npc-override") == 0 ||
        strcmp(action, "save-native-npc-inventory") == 0 ||
        strcmp(action, "delete-native-npc-inventory") == 0)
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
    if (strcmp(action, "save-monster") == 0 ||
        strcmp(action, "reset-monster") == 0 ||
        strcmp(action, "reset-monster-combat-stats") == 0 ||
        strcmp(action, "reset-monster-combat-stats-bulk") == 0 ||
        strcmp(action, "reset-monster-scene-levels-bulk") == 0)
    {
        vm_mock_admin_handle_monster_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-scene-battle-monster") == 0 ||
        strcmp(action, "delete-scene-battle-monster") == 0 ||
        strcmp(action, "deploy-scene-battle-monsters") == 0)
    {
        vm_mock_admin_handle_scene_battle_monster_action(client, action,
                                                          body);
        return;
    }
    if (strcmp(action, "save-actor-resource") == 0)
    {
        vm_mock_admin_handle_actor_resource_action(client, body);
        return;
    }
    if (strcmp(action, "save-update-slot") == 0 ||
        strcmp(action, "reset-update-delivery") == 0 ||
        strcmp(action, "add-content-update-files") == 0 ||
        strcmp(action, "remove-content-update-file") == 0 ||
        strcmp(action, "republish-content-update") == 0 ||
        strcmp(action, "clear-content-update") == 0)
    {
        vm_mock_admin_handle_update_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-shop-item") == 0)
    {
        vm_mock_admin_handle_shop_action(client, body);
        return;
    }
    if (strcmp(action, "save-chest-rewards") == 0 ||
        strcmp(action, "reset-chest-rewards") == 0)
    {
        vm_mock_admin_handle_chest_action(client, action, body);
        return;
    }
    if (strcmp(action, "save-login-server") == 0 ||
        strcmp(action, "create-login-server") == 0 ||
        strcmp(action, "delete-login-server") == 0)
    {
        vm_mock_admin_handle_login_server_action(client, action, body);
        return;
    }
    if (strcmp(action, "ban-risk-account") == 0)
    {
        u32 disconnected = 0;
        u32 userSessions = 0;
        u32 pageNumber = 1;
        char pageText[16];

        memset(pageText, 0, sizeof(pageText));
        (void)vm_mock_admin_form_value(body, "page", pageText, sizeof(pageText));
        if (pageText[0] != 0 &&
            (!vm_net_mock_parse_u32_strict(pageText, &pageNumber) ||
             pageNumber == 0))
        {
            pageNumber = 1;
        }
        if (!vm_mock_admin_form_value(body, "account", account,
                                      sizeof(account)) || account[0] == 0)
        {
            vm_mock_admin_redirect_risk(client, pageNumber, "error",
                                        "账号参数不完整");
            return;
        }
        ok = vm_mock_service_account_ban_for_rapid_battle(
            account, &disconnected, &error);
        if (ok)
            userSessions = vm_mock_user_clear_account_sessions(account);
        if (ok)
        {
            char message[192];

            snprintf(message, sizeof(message),
                     "账号已封禁：已立即断开 %u 个游戏会话、注销 %u 个用户中心会话；后续登录将被拒绝",
                     disconnected, userSessions);
            vm_mock_admin_redirect_risk(client, pageNumber, "ok", message);
        }
        else
        {
            vm_mock_admin_redirect_risk(
                client, pageNumber, "error", error ? error : "账号封禁失败");
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
    if (strcmp(action, "set-role-name") == 0)
    {
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "role_name", roleNameUtf8,
                                      sizeof(roleNameUtf8)) ||
            !vm_mock_admin_utf8_to_gbk_text(roleNameUtf8, roleNameGbk,
                                            sizeof(roleNameGbk), false))
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "角色名称参数无效");
            return;
        }
        ok = vm_mock_service_account_set_role_name(account, role, roleNameGbk,
                                                   &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? (error ? error : "角色名称已更新")
                                  : (error ? error : "角色名称修改失败"));
        return;
    }
    if (strcmp(action, "add-money") == 0 || strcmp(action, "add-wcoin") == 0)
    {
        if (!vm_mock_admin_form_value(body, "amount", amountText, sizeof(amountText)) ||
            !vm_net_mock_parse_u32_strict(amountText, &amount) || amount == 0)
        {
            vm_mock_admin_redirect(client, account, "error", "金额必须是大于 0 的整数");
            return;
        }
        if (strcmp(action, "add-money") == 0)
        {
            if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)))
            {
                vm_mock_admin_redirect(client, account, "error", "请选择角色");
                return;
            }
            ok = vm_mock_service_account_add_role_money(account, role, amount, NULL, NULL, &error);
        }
        else
            ok = vm_mock_service_account_add_wcoin(account, amount, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? (strcmp(action, "add-money") == 0 ? "普通钱币增加成功" : "W 币增加成功")
                                  : (error ? error : "余额修改失败"));
        return;
    }
    if (strcmp(action, "reset-role-selected-scene") == 0)
    {
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            role[0] == 0 ||
            !vm_mock_admin_optional_scene_from_form(
                body, "reset_scene", resetSceneUtf8, sizeof(resetSceneUtf8),
                resetRuntimeScene, sizeof(resetRuntimeScene)) ||
            resetRuntimeScene[0] == 0)
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "角色或目标场景参数无效");
            return;
        }
        ok = vm_mock_service_account_reset_role_to_scene_spawn(
            account, role, resetRuntimeScene, &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? (error ? error : "已重置到指定场景的安全落点，重新进入游戏后生效")
                                  : (error ? error : "角色位置重置失败"));
        return;
    }
    if (strcmp(action, "set-role-level") == 0)
    {
        if (!vm_mock_admin_form_value(body, "role", role, sizeof(role)) ||
            !vm_mock_admin_form_value(body, "level", levelText,
                                      sizeof(levelText)) ||
            !vm_net_mock_parse_u32_strict(levelText, &level) || level == 0 ||
            level > VM_NET_MOCK_ROLE_LEVEL_CAP)
        {
            vm_mock_admin_redirect(client, account, "error",
                                   "角色或等级参数无效（等级范围为 1 至 70）");
            return;
        }
        ok = vm_mock_service_account_set_role_level(account, role, level,
                                                    &error);
        vm_mock_admin_redirect(client, account, ok ? "ok" : "error",
                               ok ? "角色等级已更新，经验已重置为该等级起点"
                                  : (error ? error : "角色等级设置失败"));
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

static void vm_mock_user_render_backpack_item(vm_mock_admin_text *page,
                                              const vm_net_mock_role_state *role,
                                              const vm_net_mock_backpack_item_state *item)
{
    const vm_net_mock_shop_catalog_item *catalogItem = NULL;
    char itemNameUtf8[128];
    const char *countLabel = "数量";

    if (page == NULL || role == NULL || item == NULL || item->itemId == 0 ||
        item->seq == 0 || item->count == 0)
    {
        return;
    }
    catalogItem = vm_net_mock_find_shop_catalog_item(item->itemId);
    memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
    if (catalogItem != NULL)
        vm_net_mock_gbk_label_to_utf8(catalogItem->name, itemNameUtf8,
                                      sizeof(itemNameUtf8));
    if (item->itemId == 802)
        countLabel = "剩余生命储量";
    else if (item->itemId == 803)
        countLabel = "剩余法力储量";

    vm_mock_admin_text_appendf(page,
        "<li class=\"bag-item\"><div class=\"bag-item-main\"><strong>");
    if (itemNameUtf8[0] != 0)
        vm_mock_admin_text_append_html(page, itemNameUtf8);
    else
        vm_mock_admin_text_appendf(page, "未知物品 #%u", item->itemId);
    vm_mock_admin_text_appendf(page,
        "</strong><span>物品 ID %u · 序列 %u · %s %u",
        item->itemId, item->seq, countLabel, item->count);
    if (item->enhanceLevel != 0)
        vm_mock_admin_text_appendf(page, " · 强化 +%u", item->enhanceLevel);
    vm_mock_admin_text_appendf(page,
        "</span></div><form method=\"post\" action=\"/user/backpack/delete\" "
        "onsubmit=\"return confirm('确定丢弃这件背包物品？此操作无法恢复。');\">"
        "<input type=\"hidden\" name=\"role_id\" value=\"%u\">"
        "<input type=\"hidden\" name=\"item_id\" value=\"%u\">"
        "<input type=\"hidden\" name=\"item_seq\" value=\"%u\">"
        "<button class=\"bag-delete\" type=\"submit\">丢弃</button></form></li>",
        role->roleId, item->itemId, item->seq);
}

enum
{
    VM_MOCK_USER_ROLE_TRANSFER_CODE_LEN = 8,
    VM_MOCK_USER_ROLE_TRANSFER_EXPIRE_SECONDS = 15 * 60
};

typedef struct
{
    bool invalid;
    u8 count;
    u32 roleIds[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    u8 roleIndices[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
} vm_mock_user_role_transfer_roles;

typedef struct
{
    bool found;
    bool invalid;
    u32 activeRoleId;
    u32 roleCount;
} vm_mock_user_role_transfer_state;

typedef struct
{
    bool found;
    bool invalid;
    char sourceAccountId[64];
    u32 roleId;
    u32 expiresUnix;
} vm_mock_user_role_transfer_code_row;

static bool g_vm_mock_user_role_transfer_schema_ready = false;
static u32 g_vm_mock_user_role_transfer_serial = 0;

static bool vm_mock_user_role_transfer_schema_ensure(void)
{
    if (g_vm_mock_user_role_transfer_schema_ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_transfer_codes ("
            "verification_code CHAR(8) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "source_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "expires_unix INT UNSIGNED NOT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(verification_code),"
            "UNIQUE KEY uq_account_role_transfer_source_role(source_account_id,role_id),"
            "KEY idx_account_role_transfer_expiry(expires_unix),"
            "CONSTRAINT fk_account_role_transfer_source_role "
            "FOREIGN KEY(source_account_id,role_id) REFERENCES account_roles(account_id,role_id) "
            "ON DELETE CASCADE"
            ") ENGINE=InnoDB"))
    {
        return false;
    }
    g_vm_mock_user_role_transfer_schema_ready = true;
    return true;
}

static bool vm_mock_user_role_transfer_account_hex(const char *accountId,
                                                    char *out, size_t outCap)
{
    size_t accountLen = accountId ? strlen(accountId) : 0;

    return accountLen != 0 && accountLen < 64 &&
           vm_mysql_hex_encode((const u8 *)accountId, accountLen, out, outCap) != 0;
}

static bool vm_mock_user_role_transfer_code_is_valid(const char *code)
{
    if (code == NULL || strlen(code) != VM_MOCK_USER_ROLE_TRANSFER_CODE_LEN)
        return false;
    for (u32 i = 0; i < VM_MOCK_USER_ROLE_TRANSFER_CODE_LEN; ++i)
    {
        if (code[i] < '0' || code[i] > '9')
            return false;
    }
    return true;
}

static void vm_mock_user_role_transfer_make_code(char out[9])
{
    u32 entropy = (u32)time(NULL) ^ scheduler_get_tick_ms() ^
                  ++g_vm_mock_user_role_transfer_serial ^
                  (u32)(uintptr_t)&g_vm_mock_user_role_transfer_serial;
    u32 value = 10000000u + (entropy % 90000000u);

    snprintf(out, 9, "%08u", value);
}

static bool vm_mock_user_role_transfer_roles_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_mock_user_role_transfer_roles *context =
        (vm_mock_user_role_transfer_roles *)contextValue;
    u32 roleId = 0;
    u32 roleIndex = 0;

    if (context == NULL || columnCount != 2 ||
        context->count >= VM_NET_MOCK_ROLE_DB_MAX_ROLES ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &roleId) || roleId == 0 ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &roleIndex) ||
        roleIndex >= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->roleIds[context->count] = roleId;
    context->roleIndices[context->count] = (u8)roleIndex;
    ++context->count;
    return true;
}

static bool vm_mock_user_role_transfer_state_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_mock_user_role_transfer_state *context =
        (vm_mock_user_role_transfer_state *)contextValue;

    if (context == NULL || context->found || columnCount != 2 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->activeRoleId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &context->roleCount))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_user_role_transfer_code_row_cb(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_mock_user_role_transfer_code_row *context =
        (vm_mock_user_role_transfer_code_row *)contextValue;

    if (context == NULL || context->found || columnCount != 3 || values[0] == NULL ||
        lengths[0] == 0 || lengths[0] >= sizeof(context->sourceAccountId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &context->roleId) ||
        context->roleId == 0 ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &context->expiresUnix))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    memcpy(context->sourceAccountId, values[0], lengths[0]);
    context->sourceAccountId[lengths[0]] = 0;
    context->found = true;
    return true;
}

static bool vm_mock_user_role_transfer_table_exists(const char *tableName,
                                                     bool *existsOut)
{
    vm_mock_mysql_u32_context context;
    char query[256];

    if (existsOut != NULL)
        *existsOut = false;
    if (tableName == NULL || tableName[0] == 0)
        return false;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM information_schema.TABLES "
             "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='%s'", tableName);
    if (!vm_mysql_query(query, vm_mock_mysql_single_u32_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (existsOut != NULL)
        *existsOut = context.value != 0;
    return true;
}

static bool vm_mock_user_role_transfer_load_roles_for_update(
    const char *accountHex, vm_mock_user_role_transfer_roles *roles)
{
    char query[384];

    if (accountHex == NULL || roles == NULL)
        return false;
    memset(roles, 0, sizeof(*roles));
    snprintf(query, sizeof(query),
             "SELECT role_id,role_index FROM account_roles "
             "WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_index FOR UPDATE",
             accountHex);
    return vm_mysql_query(query, vm_mock_user_role_transfer_roles_row, roles) &&
           !roles->invalid;
}

static bool vm_mock_user_role_transfer_load_state_for_update(
    const char *accountHex, vm_mock_user_role_transfer_state *state)
{
    char query[384];

    if (accountHex == NULL || state == NULL)
        return false;
    memset(state, 0, sizeof(*state));
    snprintf(query, sizeof(query),
             "SELECT active_role_id,role_count FROM account_role_state "
             "WHERE account_id=CAST(X'%s' AS CHAR) FOR UPDATE", accountHex);
    return vm_mysql_query(query, vm_mock_user_role_transfer_state_row, state) &&
           !state->invalid && state->found;
}

static bool vm_mock_user_role_transfer_load_code(
    const char *code, bool forUpdate, vm_mock_user_role_transfer_code_row *row)
{
    char codeHex[32];
    char query[512];

    if (!vm_mock_user_role_transfer_code_is_valid(code) || row == NULL ||
        vm_mysql_hex_encode((const u8 *)code, strlen(code), codeHex,
                            sizeof(codeHex)) == 0)
    {
        return false;
    }
    memset(row, 0, sizeof(*row));
    snprintf(query, sizeof(query),
             "SELECT source_account_id,role_id,expires_unix "
             "FROM account_role_transfer_codes "
             "WHERE verification_code=CAST(X'%s' AS CHAR)%s",
             codeHex, forUpdate ? " FOR UPDATE" : "");
    return vm_mysql_query(query, vm_mock_user_role_transfer_code_row_cb, row) &&
           !row->invalid;
}

static bool vm_mock_user_role_transfer_contains_role(
    const vm_mock_user_role_transfer_roles *roles, u32 roleId)
{
    if (roles == NULL || roleId == 0)
        return false;
    for (u32 i = 0; i < roles->count; ++i)
    {
        if (roles->roleIds[i] == roleId)
            return true;
    }
    return false;
}

static bool vm_mock_user_role_transfer_create_code(const char *sourceAccountId,
                                                    u32 roleId, char codeOut[9],
                                                    const char **errorOut)
{
    vm_mock_user_role_transfer_roles sourceRoles;
    char sourceHex[129];
    char code[9];
    char codeHex[32];
    char query[768];
    u32 nowUnix = (u32)time(NULL);
    bool transactionStarted = false;
    bool created = false;

    if (errorOut != NULL)
        *errorOut = "生成角色迁移验证码失败";
    if (codeOut != NULL)
        codeOut[0] = 0;
    if (sourceAccountId == NULL || roleId == 0 ||
        !vm_mock_user_role_transfer_schema_ensure() ||
        !vm_mock_service_account_exists(sourceAccountId) ||
        !vm_mock_user_role_transfer_account_hex(sourceAccountId, sourceHex,
                                                sizeof(sourceHex)))
    {
        if (errorOut != NULL)
            *errorOut = "角色迁出参数无效";
        return false;
    }
    if (!vm_mysql_exec("START TRANSACTION"))
        goto done;
    transactionStarted = true;
    if (!vm_mock_user_role_transfer_load_roles_for_update(sourceHex, &sourceRoles) ||
        !vm_mock_user_role_transfer_contains_role(&sourceRoles, roleId))
    {
        if (errorOut != NULL)
            *errorOut = "该角色不属于当前账号";
        goto done;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_transfer_codes WHERE expires_unix<%u "
             "OR (source_account_id=CAST(X'%s' AS CHAR) AND role_id=%u)",
             nowUnix, sourceHex, roleId);
    if (!vm_mysql_exec(query))
        goto done;
    for (u32 attempt = 0; attempt < 12; ++attempt)
    {
        vm_mock_user_role_transfer_make_code(code);
        if (vm_mysql_hex_encode((const u8 *)code, strlen(code), codeHex,
                                sizeof(codeHex)) == 0)
        {
            goto done;
        }
        snprintf(query, sizeof(query),
                 "INSERT INTO account_role_transfer_codes"
                 "(verification_code,source_account_id,role_id,expires_unix) "
                 "VALUES(CAST(X'%s' AS CHAR),CAST(X'%s' AS CHAR),%u,%u)",
                 codeHex, sourceHex, roleId,
                 nowUnix + VM_MOCK_USER_ROLE_TRANSFER_EXPIRE_SECONDS);
        if (vm_mysql_exec(query))
        {
            created = true;
            break;
        }
    }
    if (!created)
        goto done;
    if (!vm_mysql_exec("COMMIT"))
        goto done;
    transactionStarted = false;
    if (codeOut != NULL)
        memcpy(codeOut, code, sizeof(code));
    if (errorOut != NULL)
        *errorOut = NULL;
    return true;

done:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    return false;
}

static bool vm_mock_user_role_transfer_peek_code(const char *code,
                                                  char *sourceAccountOut,
                                                  size_t sourceAccountOutCap,
                                                  u32 *roleIdOut)
{
    vm_mock_user_role_transfer_code_row row;

    if (sourceAccountOut != NULL && sourceAccountOutCap != 0)
        sourceAccountOut[0] = 0;
    if (roleIdOut != NULL)
        *roleIdOut = 0;
    if (!vm_mock_user_role_transfer_schema_ensure() ||
        !vm_mock_user_role_transfer_load_code(code, false, &row) || !row.found ||
        row.expiresUnix < (u32)time(NULL))
    {
        return false;
    }
    if (sourceAccountOut != NULL && sourceAccountOutCap != 0)
        snprintf(sourceAccountOut, sourceAccountOutCap, "%s", row.sourceAccountId);
    if (roleIdOut != NULL)
        *roleIdOut = row.roleId;
    return true;
}

/* Parent-role identity is immutable because role_id is globally unique.
 * Migration therefore creates a new destination parent first; children can
 * then be reassigned without ever observing a broken foreign key. */
static bool vm_mock_user_role_transfer_reassign_role_table(
    const char *tableName, const char *sourceHex, u32 sourceRoleId,
    const char *targetHex, u32 targetRoleId)
{
    char query[768];
    bool tableExists = false;

    if (tableName == NULL || sourceHex == NULL || targetHex == NULL ||
        !vm_mock_user_role_transfer_table_exists(tableName, &tableExists))
    {
        return false;
    }
    if (!tableExists)
        return true;
    snprintf(query, sizeof(query),
             "UPDATE %s SET account_id=CAST(X'%s' AS CHAR),role_id=%u "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             tableName, targetHex, targetRoleId, sourceHex, sourceRoleId);
    return vm_mysql_exec(query);
}

static bool vm_mock_user_role_transfer_reassign_role_tables(
    const char *sourceHex, u32 sourceRoleId, const char *targetHex,
    u32 targetRoleId)
{
    static const char *const tables[] = {
        "account_role_equipment",
        "account_role_equipment_durability",
        "account_role_skills",
        "account_role_backpack",
        "account_role_training_books",
        "account_role_item_effects",
        "account_role_tasks",
        "account_role_offline_exp",
        "account_role_practise",
        "account_role_vitality",
        "account_role_battle_entry_state",
        "account_role_rapid_battle_entry_audit"
    };

    for (u32 i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i)
    {
        if (!vm_mock_user_role_transfer_reassign_role_table(
                tables[i], sourceHex, sourceRoleId, targetHex, targetRoleId))
        {
            return false;
        }
    }
    return true;
}

static bool vm_mock_user_role_transfer_reassign_social_rows(
    const char *sourceHex, u32 sourceRoleId, const char *targetHex,
    u32 targetRoleId)
{
    static const char *const tableNames[] = {
        "friendships", "world_chat_messages", "guilds", "guild_members",
        "guild_applications"
    };
    static const char *const queries[] = {
        "UPDATE friendships SET owner_account_id=CAST(X'%s' AS CHAR),owner_role_id=%u "
        "WHERE owner_account_id=CAST(X'%s' AS CHAR) AND owner_role_id=%u",
        "UPDATE world_chat_messages SET source_account_id=CAST(X'%s' AS CHAR),source_role_id=%u "
        "WHERE source_account_id=CAST(X'%s' AS CHAR) AND source_role_id=%u",
        "UPDATE guilds SET leader_account_id=CAST(X'%s' AS CHAR),leader_role_id=%u "
        "WHERE leader_account_id=CAST(X'%s' AS CHAR) AND leader_role_id=%u",
        "UPDATE guild_members SET account_id=CAST(X'%s' AS CHAR),role_id=%u "
        "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
        "UPDATE guild_applications SET applicant_account_id=CAST(X'%s' AS CHAR),applicant_role_id=%u "
        "WHERE applicant_account_id=CAST(X'%s' AS CHAR) AND applicant_role_id=%u"
    };
    char query[1024];

    for (u32 i = 0; i < sizeof(tableNames) / sizeof(tableNames[0]); ++i)
    {
        bool tableExists = false;
        if (!vm_mock_user_role_transfer_table_exists(tableNames[i], &tableExists))
            return false;
        if (!tableExists)
            continue;
        snprintf(query, sizeof(query), queries[i], targetHex, targetRoleId,
                 sourceHex, sourceRoleId);
        if (!vm_mysql_exec(query))
            return false;
        /* Friend rows may reference the migrating role from the other side. */
        if (strcmp(tableNames[i], "friendships") == 0)
        {
            snprintf(query, sizeof(query),
                     "UPDATE friendships SET target_account_id=CAST(X'%s' AS CHAR),"
                     "target_role_id=%u WHERE target_account_id=CAST(X'%s' AS CHAR) "
                     "AND target_role_id=%u",
                     targetHex, targetRoleId, sourceHex, sourceRoleId);
            if (!vm_mysql_exec(query))
                return false;
        }
    }
    return true;
}

/* The destination parent is inserted before dependent rows are reassigned.
 * Thus all foreign keys remain valid throughout this one database transaction. */
static bool vm_mock_user_role_transfer_claim_code(const char *targetAccountId,
                                                  const char *code,
                                                  char *sourceAccountOut,
                                                  size_t sourceAccountOutCap,
                                                  u32 *sourceRoleIdOut,
                                                  u32 *targetRoleIdOut,
                                                  const char **errorOut)
{
    vm_mock_user_role_transfer_code_row codeRow;
    vm_mock_user_role_transfer_roles sourceRoles;
    vm_mock_user_role_transfer_roles targetRoles;
    vm_mock_user_role_transfer_state sourceState;
    vm_mock_user_role_transfer_state targetState;
    char sourceHex[129];
    char targetHex[129];
    char codeHex[32];
    char query[2304];
    u32 targetRoleId = 0;
    u32 targetRoleIndex = 0;
    bool transactionStarted = false;
    bool ok = false;

    if (sourceAccountOut != NULL && sourceAccountOutCap != 0)
        sourceAccountOut[0] = 0;
    if (sourceRoleIdOut != NULL)
        *sourceRoleIdOut = 0;
    if (targetRoleIdOut != NULL)
        *targetRoleIdOut = 0;
    if (errorOut != NULL)
        *errorOut = "角色迁入失败";
    if (targetAccountId == NULL || targetAccountId[0] == 0 ||
        !vm_mock_user_role_transfer_code_is_valid(code) ||
        !vm_mock_user_role_transfer_schema_ensure() ||
        !vm_mock_user_role_transfer_account_hex(targetAccountId, targetHex,
                                                sizeof(targetHex)) ||
        vm_mysql_hex_encode((const u8 *)code, strlen(code), codeHex,
                            sizeof(codeHex)) == 0)
    {
        if (errorOut != NULL)
            *errorOut = "角色迁入参数无效";
        return false;
    }
    if (!vm_mysql_exec("START TRANSACTION"))
        goto done;
    transactionStarted = true;
    if (!vm_mock_user_role_transfer_load_code(code, true, &codeRow) ||
        !codeRow.found || codeRow.expiresUnix < (u32)time(NULL))
    {
        if (errorOut != NULL)
            *errorOut = "验证码无效或已过期";
        goto done;
    }
    if (strcmp(codeRow.sourceAccountId, targetAccountId) == 0)
    {
        if (errorOut != NULL)
            *errorOut = "不能迁入当前账号的角色";
        goto done;
    }
    if (!vm_mock_user_role_transfer_account_hex(codeRow.sourceAccountId,
                                                sourceHex, sizeof(sourceHex)) ||
        !vm_mock_user_role_transfer_load_roles_for_update(sourceHex, &sourceRoles) ||
        !vm_mock_user_role_transfer_contains_role(&sourceRoles, codeRow.roleId) ||
        !vm_mock_user_role_transfer_load_roles_for_update(targetHex, &targetRoles))
    {
        if (errorOut != NULL)
            *errorOut = "迁出角色不存在或账号角色数据异常";
        goto done;
    }
    if (targetRoles.count >= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (errorOut != NULL)
            *errorOut = "当前账号角色数量已达上限";
        goto done;
    }
    for (targetRoleIndex = 0; targetRoleIndex < VM_NET_MOCK_ROLE_DB_MAX_ROLES;
         ++targetRoleIndex)
    {
        bool occupied = false;
        for (u32 i = 0; i < targetRoles.count; ++i)
        {
            if (targetRoles.roleIndices[i] == targetRoleIndex)
            {
                occupied = true;
                break;
            }
        }
        if (!occupied)
            break;
    }
    if (targetRoleIndex >= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (errorOut != NULL)
            *errorOut = "当前账号没有可用角色栏位";
        goto done;
    }
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO account_role_state"
             "(account_id,format_version,active_role_id,role_count) "
             "VALUES(CAST(X'%s' AS CHAR),1,0,0)", targetHex);
    if (!vm_mysql_exec(query) ||
        !vm_mock_user_role_transfer_load_state_for_update(sourceHex, &sourceState) ||
        !vm_mock_user_role_transfer_load_state_for_update(targetHex, &targetState))
    {
        if (errorOut != NULL)
            *errorOut = "账号角色状态不可用";
        goto done;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO role_id_sequence(account_id) VALUES(CAST(X'%s' AS CHAR))",
             targetHex);
    if (!vm_mysql_exec(query))
        goto done;
    {
        vm_mock_mysql_u32_context idContext;
        memset(&idContext, 0, sizeof(idContext));
        if (!vm_mysql_query("SELECT LAST_INSERT_ID()",
                            vm_mock_mysql_single_u32_row, &idContext) ||
            idContext.invalid || !idContext.found || idContext.value == 0)
            goto done;
        targetRoleId = idContext.value;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO account_roles"
             "(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,"
             "level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,"
             "backpack_item_count,designation_id,next_backpack_seq) "
             "SELECT CAST(X'%s' AS CHAR),%u,%u,role_name,job,sex,backpack_capacity,"
             "level,exp,hp,hp_max,mp,mp_max,money,0,scene,pos_x,pos_y,"
             "backpack_item_count,designation_id,next_backpack_seq "
             "FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             targetHex, targetRoleId, targetRoleIndex, sourceHex, codeRow.roleId);
    if (!vm_mysql_exec(query) ||
        !vm_mock_user_role_transfer_reassign_role_tables(
            sourceHex, codeRow.roleId, targetHex, targetRoleId) ||
        !vm_mock_user_role_transfer_reassign_social_rows(
            sourceHex, codeRow.roleId, targetHex, targetRoleId))
    {
        goto done;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_transfer_codes "
             "WHERE verification_code=CAST(X'%s' AS CHAR)", codeHex);
    if (!vm_mysql_exec(query))
        goto done;
    snprintf(query, sizeof(query),
             "DELETE FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u", sourceHex, codeRow.roleId);
    if (!vm_mysql_exec(query))
        goto done;
    /* The normal creator expects dense role indices. */
    for (u32 i = 0, compactIndex = 0; i < sourceRoles.count; ++i)
    {
        if (sourceRoles.roleIds[i] == codeRow.roleId)
            continue;
        snprintf(query, sizeof(query),
                 "UPDATE account_roles SET role_index=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 compactIndex++, sourceHex, sourceRoles.roleIds[i]);
        if (!vm_mysql_exec(query))
            goto done;
    }
    {
        u32 sourceRemaining = sourceRoles.count - 1u;
        u32 sourceActive = sourceState.activeRoleId;
        u32 targetActive = targetState.activeRoleId;
        if (sourceActive == codeRow.roleId)
            sourceActive = sourceRemaining == 0 ? 0 :
                (sourceRoles.roleIds[0] == codeRow.roleId ? sourceRoles.roleIds[1] :
                                                           sourceRoles.roleIds[0]);
        if (!vm_mock_user_role_transfer_contains_role(&targetRoles, targetActive))
            targetActive = targetRoleId;
        snprintf(query, sizeof(query),
                 "UPDATE account_role_state SET active_role_id=%u,role_count=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR)", sourceActive,
                 sourceRemaining, sourceHex);
        if (!vm_mysql_exec(query))
            goto done;
        snprintf(query, sizeof(query),
                 "UPDATE account_role_state SET active_role_id=%u,role_count=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR)", targetActive,
                 targetRoles.count + 1u, targetHex);
        if (!vm_mysql_exec(query))
            goto done;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto done;
    transactionStarted = false;
    if (sourceAccountOut != NULL && sourceAccountOutCap != 0)
        snprintf(sourceAccountOut, sourceAccountOutCap, "%s", codeRow.sourceAccountId);
    if (sourceRoleIdOut != NULL)
        *sourceRoleIdOut = codeRow.roleId;
    if (targetRoleIdOut != NULL)
        *targetRoleIdOut = targetRoleId;
    if (errorOut != NULL)
        *errorOut = NULL;
    ok = true;

done:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (!ok && errorOut != NULL && *errorOut != NULL &&
        strcmp(*errorOut, "角色迁入失败") == 0)
        *errorOut = vm_mysql_last_error()[0] ? "角色迁入数据库操作失败" : "角色迁入失败";
    return ok;
}

static void vm_mock_user_render_role_transfer_card(vm_mock_admin_text *page,
                                                   bool hasRoles)
{
    if (page == NULL)
        return;
    vm_mock_admin_text_appendf(page,
        "<section class=\"card transfer\"><div class=\"transfer-heading\">"
        "<div><h2>角色迁移</h2><p class=\"sub\">完整迁移角色资料、背包、装备、技能、任务、好友、帮派与聊天历史。</p></div>"
        "<span class=\"transfer-badge\">一次性验证码 · 15 分钟有效</span></div>"
        "<div class=\"transfer-grid\"><div class=\"transfer-panel\"><h3>角色迁出</h3>"
        "<p>选择角色后生成 8 位验证码。验证码请仅交给目标账号持有人。</p>");
    if (hasRoles)
    {
        vm_mock_admin_text_appendf(page,
            "<form class=\"transfer-form\" method=\"post\" action=\"/user/role-transfer/export\" "
            "onsubmit=\"return confirm('确定为这个角色生成迁移验证码？');\">"
            "<label>迁出角色<select name=\"role_id\" required>");
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
            char roleNameUtf8[128];
            memset(roleNameUtf8, 0, sizeof(roleNameUtf8));
            vm_net_mock_gbk_label_to_utf8(role->name,
                                          roleNameUtf8, sizeof(roleNameUtf8));
            vm_mock_admin_text_appendf(page, "<option value=\"%u\">", role->roleId);
            vm_mock_admin_text_append_html(page, roleNameUtf8);
            vm_mock_admin_text_appendf(page, "（Lv.%u）</option>", role->level);
        }
        vm_mock_admin_text_appendf(page,
            "</select></label><button type=\"submit\">生成迁出验证码</button></form>");
    }
    else
    {
        vm_mock_admin_text_appendf(page,
            "<p class=\"transfer-muted\">当前账号没有可迁出的角色。</p>");
    }
    vm_mock_admin_text_appendf(page,
        "</div><div class=\"transfer-panel\"><h3>角色迁入</h3>"
        "<p>在目标账号输入迁出方提供的验证码。迁入会保留全部角色关系；账号 W 币与充值记录不会迁移。</p>"
        "<form class=\"transfer-form\" method=\"post\" action=\"/user/role-transfer/import\" "
        "onsubmit=\"return confirm('迁入后两边的游戏连接都会断开，确定继续？');\">"
        "<label>8 位验证码<input name=\"code\" inputmode=\"numeric\" pattern=\"[0-9]{8}\" "
        "minlength=\"8\" maxlength=\"8\" autocomplete=\"one-time-code\" placeholder=\"例如 12345678\" required></label>"
        "<button type=\"submit\">确认迁入角色</button></form></div></div>"
        "<p class=\"transfer-note\">迁入账号须保留至少一个角色栏位。验证码使用后立即失效；迁移成功后请重新登录游戏。</p></section>");
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
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:#f4f7f9;color:#17202a;font:14px/1.6 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{width:min(1080px,calc(100%% - 28px));margin:0 auto;padding:30px 0 46px}header{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:18px}.brand{display:flex;align-items:center;gap:12px}.brand-mark{display:grid;place-items:center;width:42px;height:42px;border-radius:12px;background:linear-gradient(145deg,#0f766e,#175cd3);color:#fff;font:700 20px serif}h1{font-size:24px;margin:0}.sub,.muted{color:#667085;margin:2px 0 0}.logout{border:1px solid #d0d5dd;border-radius:8px;padding:8px 13px;background:#fff;color:#475467;cursor:pointer}.hero{position:relative;overflow:hidden;border-radius:16px;padding:24px;background:linear-gradient(125deg,#12372d,#175cd3);color:#fff;box-shadow:0 14px 34px #175cd326;margin-bottom:18px}.hero:after{content:\"\";position:absolute;width:220px;height:220px;border-radius:50%%;right:-70px;top:-125px;background:#ffffff12}.account-line{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.account-name{font-size:25px;font-weight:750}.hero .sub{color:#dbeafe}.badge{font-size:12px;padding:3px 9px;border-radius:999px;background:#ffffff20;color:#fff}.badge.on{background:#d1fadf;color:#05603a}.overview{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:19px}.overview div{padding:11px 13px;border:1px solid #ffffff20;border-radius:10px;background:#ffffff10}.overview strong{display:block;font-size:18px}.section-title{display:flex;align-items:end;justify-content:space-between;margin:23px 0 10px}.section-title h2{font-size:18px;margin:0}.roles{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:14px}.card,.role{background:#fff;border:1px solid #e4e7ec;border-radius:12px;padding:18px;box-shadow:0 2px 7px #1018280a}.role-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.role h3{font-size:19px;margin:0}.active{display:inline-block;color:#175cd3;background:#eef4ff;border-radius:999px;padding:2px 8px;font-size:12px;font-weight:650}.vitals{display:grid;gap:10px;margin:17px 0}.vital-head{display:flex;justify-content:space-between;color:#475467}.bar{height:7px;border-radius:999px;background:#edf1f5;overflow:hidden}.bar i{display:block;height:100%%;border-radius:inherit;background:#12b76a}.bar.mp i{background:#2e90fa}.stats{display:grid;grid-template-columns:1fr 1fr;gap:10px 16px;padding-top:13px;border-top:1px solid #eaecf0}.stats strong{display:block;color:#667085;font-size:12px;font-weight:550}.bag-head{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-top:17px;padding-top:13px;border-top:1px solid #eaecf0}.bag-head h4{margin:0;font-size:15px}.bag-list{list-style:none;padding:0;margin:9px 0 0;border:1px solid #eaecf0;border-radius:9px;max-height:260px;overflow:auto}.bag-item{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:10px 11px;border-bottom:1px solid #f0f2f5}.bag-item:last-child{border-bottom:0}.bag-item-main{min-width:0}.bag-item-main strong,.bag-item-main span{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.bag-item-main span{font-size:12px;color:#667085}.bag-delete{border:1px solid #fecdca;border-radius:7px;padding:6px 10px;background:#fff5f4;color:#b42318;font:inherit;cursor:pointer}.bag-delete:hover{background:#fee4e2}.bag-empty{margin:9px 0 0;padding:12px;border:1px dashed #d0d5dd;border-radius:9px;color:#667085;text-align:center}.empty{color:#667085;text-align:center}.security{display:grid;grid-template-columns:minmax(190px,.65fr) minmax(0,1.35fr);gap:22px;align-items:start;margin-top:18px}.security h2{font-size:18px;margin:0 0 4px}.password-form{display:grid;grid-template-columns:1fr 1fr;gap:10px}.password-form label{display:grid;gap:4px;color:#475467}.password-form .current{grid-column:1/-1}.password-form input{width:100%%;border:1px solid #d0d5dd;border-radius:8px;padding:9px 10px;font:inherit}.password-form button{grid-column:1/-1;justify-self:start;border:0;border-radius:8px;padding:9px 14px;background:#175cd3;color:#fff;font-weight:650;cursor:pointer}.notice{padding:11px 13px;border-radius:9px;margin-bottom:16px}.notice.ok{background:#ecfdf3;color:#027a48}.notice.error{background:#fef3f2;color:#b42318}@media(max-width:720px){.wrap{padding:18px 0}.overview{grid-template-columns:1fr}.roles,.security,.password-form{grid-template-columns:1fr}.password-form .current{grid-column:auto}.password-form button{grid-column:auto}.stats{grid-template-columns:1fr}.bag-item{align-items:flex-start;flex-direction:column}.bag-delete{align-self:flex-end}header{align-items:center}}"
        ".recharge{margin-top:18px}.recharge h2{font-size:18px;margin:0 0 4px}.recharge-form{display:grid;grid-template-columns:1.2fr .8fr .8fr auto;gap:10px;align-items:end;margin-top:15px}.recharge-form label{display:grid;gap:4px;color:#475467}.recharge-form input,.recharge-form select{width:100%%;border:1px solid #d0d5dd;border-radius:8px;padding:9px 10px;font:inherit;background:#fff}.recharge-form button{border:0;border-radius:8px;padding:10px 14px;background:#175cd3;color:#fff;font-weight:700;cursor:pointer}.recharge-unavailable{padding:12px;border-radius:9px;background:#f2f4f7;color:#667085}.recharge-history{margin-top:18px;border-top:1px solid #eaecf0;padding-top:13px}.recharge-history h3{font-size:14px;margin:0 0 6px}.recharge-history a{display:flex;justify-content:space-between;gap:12px;padding:7px 0;color:#344054;text-decoration:none}.recharge-history a strong{color:#175cd3;font-size:12px}.recharge-note{color:#98a2b3;font-size:12px;margin:13px 0 0}@media(max-width:900px){.recharge-form{grid-template-columns:1fr 1fr}.recharge-form button{align-self:stretch}}@media(max-width:720px){.recharge-form{grid-template-columns:1fr}}"
        ".transfer{margin-top:18px}.transfer-heading{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.transfer h2,.transfer h3{margin:0}.transfer h2{font-size:18px}.transfer h3{font-size:16px}.transfer-badge{flex:0 0 auto;padding:3px 8px;border-radius:999px;background:#eef4ff;color:#175cd3;font-size:12px;font-weight:650}.transfer-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:15px}.transfer-panel{padding:15px;border:1px solid #e4e7ec;border-radius:10px;background:#fcfcfd}.transfer-panel p{min-height:44px;color:#667085;margin:7px 0 12px}.transfer-form{display:grid;gap:9px}.transfer-form label{display:grid;gap:4px;color:#475467}.transfer-form input,.transfer-form select{width:100%%;border:1px solid #d0d5dd;border-radius:8px;padding:9px 10px;background:#fff;font:inherit}.transfer-form button{justify-self:start;border:0;border-radius:8px;padding:9px 13px;background:#175cd3;color:#fff;font-weight:700;cursor:pointer}.transfer-muted,.transfer-note{color:#667085}.transfer-muted{margin:15px 0}.transfer-note{font-size:12px;margin:14px 0 0}@media(max-width:720px){.transfer-heading,.transfer-grid{grid-template-columns:1fr}.transfer-heading{display:grid}.transfer-badge{justify-self:start}}"
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
        "</span><span class=\"badge %s\">%s</span></div><p class=\"sub\">账号数据已连接至江湖服务，可管理角色背包</p>"
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
            vm_net_mock_gbk_label_to_utf8(role->name,
                                          roleNameUtf8, sizeof(roleNameUtf8));
            vm_net_mock_gbk_label_to_utf8(role->scene,
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
                "<div><strong>所在场景</strong>",
                role->roleId, active ? "<span class=\"active\">当前角色</span>" : "",
                role->hp, role->hpMax, hpPercent, role->mp, role->mpMax,
                mpPercent, role->level, role->exp,
                vm_mock_user_job_label(role->job), role->sex == 1 ? "女" : "男",
                gold, silver, copper);
            vm_mock_admin_text_append_html(&page, sceneUtf8);
            vm_mock_admin_text_appendf(&page,
                "</div><div><strong>坐标</strong>(%u, %u)</div></div>"
                "<div class=\"bag-head\"><h4>背包管理</h4><span class=\"muted\">%u / %u 格</span></div>",
                role->x, role->y, vm_net_mock_role_backpack_count(role),
                role->backpackCapacity);
            if (vm_net_mock_role_backpack_count(role) == 0)
            {
                vm_mock_admin_text_appendf(&page,
                    "<div class=\"bag-empty\">背包为空</div>");
            }
            else
            {
                u8 backpackCount = vm_net_mock_role_backpack_count(role);
                vm_mock_admin_text_appendf(&page, "<ul class=\"bag-list\">");
                for (u32 itemIndex = 0; itemIndex < backpackCount; ++itemIndex)
                    vm_mock_user_render_backpack_item(&page, role,
                                                       &role->backpackItems[itemIndex]);
                vm_mock_admin_text_appendf(&page, "</ul>");
            }
            vm_mock_admin_text_appendf(&page, "</article>");
        }
        vm_mock_admin_text_appendf(&page, "</section>");
        vm_mock_payment_render_dashboard(&page, accountId);
        vm_mock_user_render_role_transfer_card(&page, true);
        vm_mock_service_close_account_role_db_for_management(accountState, false);
    }
    else
    {
        vm_mock_admin_text_appendf(&page,
            "<div class=\"card empty\">%s</div>",
            roleError ? roleError : "该账号尚未创建角色，请进入游戏创建角色。");
        vm_mock_admin_text_appendf(&page, "</section>");
        if (accountState != NULL)
        {
            vm_mock_payment_render_dashboard(&page, accountId);
            vm_mock_user_render_role_transfer_card(&page, false);
            vm_mock_service_close_account_role_db_for_management(accountState, false);
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

static const u8 *vm_mock_admin_find_bytes(const u8 *data, u32 dataLen,
                                          const u8 *needle, u32 needleLen)
{
    if (data == NULL || needle == NULL || needleLen == 0 || needleLen > dataLen)
        return NULL;
    for (u32 i = 0; i + needleLen <= dataLen; ++i)
    {
        if (memcmp(data + i, needle, needleLen) == 0)
            return data + i;
    }
    return NULL;
}

static bool vm_mock_admin_parse_multipart_upload(
    const char *request, size_t headerLen, const u8 *body, u32 bodyLen,
    const u8 **fileDataOut, u32 *fileLenOut, char *filenameOut,
    size_t filenameCap)
{
    char contentType[512];
    char boundary[192];
    char separator[200];
    const char *boundaryText = NULL;
    size_t boundaryLen = 0;
    u32 separatorLen = 0;
    u32 pos = 0;

    if (fileDataOut)
        *fileDataOut = NULL;
    if (fileLenOut)
        *fileLenOut = 0;
    if (filenameOut && filenameCap != 0)
        filenameOut[0] = 0;
    if (request == NULL || body == NULL || fileDataOut == NULL ||
        fileLenOut == NULL || filenameOut == NULL || filenameCap == 0 ||
        !vm_mock_admin_header_value(request, headerLen, "Content-Type",
                                    contentType, sizeof(contentType)))
    {
        return false;
    }
    boundaryText = strstr(contentType, "boundary=");
    if (boundaryText == NULL)
        return false;
    boundaryText += 9;
    if (*boundaryText == '"')
    {
        ++boundaryText;
        while (boundaryText[boundaryLen] != 0 &&
               boundaryText[boundaryLen] != '"')
            ++boundaryLen;
    }
    else
    {
        while (boundaryText[boundaryLen] != 0 &&
               boundaryText[boundaryLen] != ';' &&
               !isspace((unsigned char)boundaryText[boundaryLen]))
        {
            ++boundaryLen;
        }
    }
    if (boundaryLen == 0 || boundaryLen >= sizeof(boundary) ||
        boundaryLen + 2u >= sizeof(separator))
    {
        return false;
    }
    memcpy(boundary, boundaryText, boundaryLen);
    boundary[boundaryLen] = 0;
    separator[0] = '-';
    separator[1] = '-';
    memcpy(separator + 2, boundary, boundaryLen);
    separatorLen = (u32)boundaryLen + 2u;
    while (pos < bodyLen)
    {
        const u8 *headerEnd = NULL;
        const u8 *next = NULL;
        const u8 *dataStart = NULL;
        u32 headerStart = 0;
        u32 dataLen = 0;
        char partHeaders[2048];
        const char *filename = NULL;
        const char *filenameEnd = NULL;

        if (pos + separatorLen > bodyLen ||
            memcmp(body + pos, separator, separatorLen) != 0)
        {
            return false;
        }
        pos += separatorLen;
        if (pos + 2u <= bodyLen && body[pos] == '-' && body[pos + 1u] == '-')
            break;
        if (pos + 2u > bodyLen || body[pos] != '\r' || body[pos + 1u] != '\n')
            return false;
        pos += 2u;
        headerStart = pos;
        headerEnd = vm_mock_admin_find_bytes(body + pos, bodyLen - pos,
                                             (const u8 *)"\r\n\r\n", 4u);
        if (headerEnd == NULL || (size_t)(headerEnd - (body + headerStart)) >=
                                     sizeof(partHeaders))
        {
            return false;
        }
        memcpy(partHeaders, body + headerStart,
               (size_t)(headerEnd - (body + headerStart)));
        partHeaders[headerEnd - (body + headerStart)] = 0;
        dataStart = headerEnd + 4u;
        next = vm_mock_admin_find_bytes(dataStart,
                                        bodyLen - (u32)(dataStart - body),
                                        (const u8 *)"\r\n--", 4u);
        if (next == NULL || next + 4u + boundaryLen > body + bodyLen ||
            memcmp(next + 4u, boundary, boundaryLen) != 0)
        {
            return false;
        }
        dataLen = (u32)(next - dataStart);
        if (strstr(partHeaders, "name=\"resource_file\"") != NULL &&
            (filename = strstr(partHeaders, "filename=\"")) != NULL)
        {
            filename += 10;
            filenameEnd = strchr(filename, '"');
            if (filenameEnd == NULL || filenameEnd == filename ||
                (size_t)(filenameEnd - filename) >= filenameCap)
            {
                return false;
            }
            memcpy(filenameOut, filename, (size_t)(filenameEnd - filename));
            filenameOut[filenameEnd - filename] = 0;
            *fileDataOut = dataStart;
            *fileLenOut = dataLen;
            return dataLen != 0;
        }
        pos = (u32)(next - body) + 2u;
    }
    return false;
}

static bool vm_mock_admin_upload_resource_name(const char *filename,
                                               const char *suffix,
                                               char *nameOut,
                                               size_t nameCap)
{
    const char *leaf = filename;
    const char *slash = NULL;
    const char *dot = NULL;
    char utf8[256];
    char gbk[128];
    size_t stemLen = 0;

    if (filename == NULL || suffix == NULL || nameOut == NULL || nameCap == 0)
        return false;
    slash = strrchr(filename, '/');
    if (slash != NULL)
        leaf = slash + 1;
    slash = strrchr(leaf, '\\');
    if (slash != NULL)
        leaf = slash + 1;
    if (leaf[0] == 0 || strlen(leaf) >= sizeof(utf8))
        return false;
    snprintf(utf8, sizeof(utf8), "%s", leaf);
    dot = strrchr(utf8, '.');
    if (dot == NULL || dot == utf8)
        return false;
    stemLen = (size_t)(dot - utf8);
    if (stemLen + strlen(suffix) >= sizeof(utf8))
        return false;
    utf8[stemLen] = 0;
    if (!vm_mock_admin_utf8_to_gbk_text(utf8, gbk, sizeof(gbk), false) ||
        gbk[0] == 0 || strlen(gbk) + strlen(suffix) >= nameCap ||
        snprintf(nameOut, nameCap, "%s%s", gbk, suffix) >= (int)nameCap ||
        !vm_net_mock_update_name_is_safe(nameOut))
    {
        return false;
    }
    return true;
}

static bool vm_mock_admin_raw_resource_is_valid(const u8 *raw, u32 rawLen)
{
    u32 declaredLen = 0;
    u32 decodedLen = 0;
    u8 *decoded = NULL;
    bool ok = false;

    if (raw == NULL || rawLen < 5u)
        return false;
    declaredLen = (u32)raw[0] | ((u32)raw[1] << 8) |
                  ((u32)raw[2] << 16) | ((u32)raw[3] << 24);
    if (declaredLen != rawLen - 4u || declaredLen < 1u)
        return false;
    if (raw[4] == 1u)
        return declaredLen > 1u;
    if (raw[4] != 2u || declaredLen < 9u)
        return false;
    decodedLen = vm_net_mock_read_be32_at(raw + 4, 5) & 0x7fffffffu;
    if (decodedLen == 0 || decodedLen > VM_MOCK_ADMIN_PREVIEW_RESOURCE_MAX)
        return false;
    decoded = (u8 *)malloc(decodedLen);
    if (decoded != NULL &&
        vm_net_mock_decode_lzss_resource_stream(raw + 4, declaredLen,
                                                 decoded, decodedLen) ==
            decodedLen)
    {
        ok = true;
    }
    free(decoded);
    return ok;
}

static bool vm_mock_admin_gif_write_bits(u8 *out, u32 outCap, u32 *bitPos,
                                         u16 code)
{
    if (out == NULL || bitPos == NULL || *bitPos > outCap * 8u - 9u)
        return false;
    for (u32 bit = 0; bit < 9u; ++bit)
    {
        if ((code & (1u << bit)) != 0)
            out[(*bitPos + bit) >> 3] |= (u8)(1u << ((*bitPos + bit) & 7u));
    }
    *bitPos += 9u;
    return true;
}

static bool vm_mock_admin_encode_uploaded_image_as_game_gif(
    const u8 *source, u32 sourceLen, u8 **rawOut, u32 *rawLenOut,
    u16 *widthOut, u16 *heightOut)
{
    int width = 0;
    int height = 0;
    int components = 0;
    u8 *rgba = NULL;
    u32 *histogram = NULL;
    u16 *colorIndex = NULL;
    u16 palette[256];
    u8 *indices = NULL;
    u8 *lzw = NULL;
    u8 *payload = NULL;
    u8 *raw = NULL;
    u32 pixelCount = 0;
    u32 paletteCount = 0;
    u32 lzwLen = 0;
    u32 payloadLen = 0;
    u32 rawLen = 0;
    u32 bitPos = 0;
    u32 pos = 0;
    bool ok = false;

    if (rawOut)
        *rawOut = NULL;
    if (rawLenOut)
        *rawLenOut = 0;
    if (widthOut)
        *widthOut = 0;
    if (heightOut)
        *heightOut = 0;
    if (source == NULL || sourceLen == 0 ||
        sourceLen > VM_MOCK_ADMIN_GIF_UPLOAD_MAX || rawOut == NULL ||
        rawLenOut == NULL ||
        !stbi_info_from_memory(source, (int)sourceLen, &width, &height,
                               &components) ||
        width <= 0 || height <= 0 || width > 0xffff || height > 0xffff ||
        (u32)width > VM_MOCK_ADMIN_GIF_UPLOAD_PIXEL_MAX / (u32)height)
    {
        return false;
    }
    pixelCount = (u32)width * (u32)height;
    rgba = stbi_load_from_memory(source, (int)sourceLen, &width, &height,
                                 &components, 4);
    histogram = (u32 *)calloc(65536u, sizeof(*histogram));
    colorIndex = (u16 *)malloc(65536u * sizeof(*colorIndex));
    indices = (u8 *)malloc(pixelCount);
    if (rgba == NULL || histogram == NULL || colorIndex == NULL ||
        indices == NULL)
    {
        goto done;
    }
    memset(colorIndex, 0xff, 65536u * sizeof(*colorIndex));
    memset(palette, 0, sizeof(palette));
    for (u32 i = 0; i < pixelCount; ++i)
    {
        const u8 *px = rgba + i * 4u;
        u16 color = (u16)(((u16)(px[0] & 0xf8u) << 8) |
                          ((u16)(px[1] & 0xfcu) << 3) | (px[2] >> 3));
        if (px[3] >= 128u)
            ++histogram[color];
    }
    for (u32 slot = 1; slot < 256u; ++slot)
    {
        u32 bestColor = 0;
        u32 bestCount = 0;
        for (u32 color = 0; color < 65536u; ++color)
        {
            if (histogram[color] > bestCount)
            {
                bestColor = color;
                bestCount = histogram[color];
            }
        }
        if (bestCount == 0)
            break;
        palette[slot] = (u16)bestColor;
        colorIndex[bestColor] = (u16)slot;
        histogram[bestColor] = 0;
        paletteCount = slot;
    }
    for (u32 i = 0; i < pixelCount; ++i)
    {
        const u8 *px = rgba + i * 4u;
        u16 color = (u16)(((u16)(px[0] & 0xf8u) << 8) |
                          ((u16)(px[1] & 0xfcu) << 3) | (px[2] >> 3));
        u16 chosen = 0;

        if (px[3] >= 128u)
        {
            if (colorIndex[color] == 0xffffu)
            {
                u32 bestDistance = UINT32_MAX;
                for (u32 slot = 1; slot <= paletteCount; ++slot)
                {
                    int dr = (int)((color >> 11) & 0x1fu) -
                             (int)((palette[slot] >> 11) & 0x1fu);
                    int dg = (int)((color >> 5) & 0x3fu) -
                             (int)((palette[slot] >> 5) & 0x3fu);
                    int db = (int)(color & 0x1fu) -
                             (int)(palette[slot] & 0x1fu);
                    u32 distance = (u32)(dr * dr + dg * dg + db * db);
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        chosen = (u16)slot;
                    }
                }
                colorIndex[color] = chosen;
            }
            else
            {
                chosen = colorIndex[color];
            }
        }
        indices[i] = (u8)chosen;
    }
    lzwLen = (u32)(((uint64_t)pixelCount * 18u + 9u + 7u) / 8u);
    lzw = (u8 *)calloc(lzwLen, 1u);
    if (lzw == NULL)
        goto done;
    for (u32 i = 0; i < pixelCount; ++i)
    {
        if (!vm_mock_admin_gif_write_bits(lzw, lzwLen, &bitPos, 256u) ||
            !vm_mock_admin_gif_write_bits(lzw, lzwLen, &bitPos, indices[i]))
        {
            goto done;
        }
    }
    if (!vm_mock_admin_gif_write_bits(lzw, lzwLen, &bitPos, 257u))
        goto done;
    payloadLen = 7u + 512u + 8u + 11u +
                 (lzwLen + 254u) / 255u + lzwLen + 1u;
    rawLen = payloadLen + 5u;
    payload = (u8 *)calloc(payloadLen, 1u);
    raw = (u8 *)malloc(rawLen);
    if (payload == NULL || raw == NULL)
        goto done;
    payload[pos++] = 0;
    payload[pos++] = 0;
    payload[pos++] = 0x10;
    payload[pos++] = 0;
    payload[pos++] = 0x87;
    payload[pos++] = 0;
    payload[pos++] = 0;
    for (u32 i = 0; i < 256u; ++i)
    {
        payload[pos++] = (u8)(palette[i] >> 8);
        payload[pos++] = (u8)(palette[i] & 0xffu);
    }
    payload[pos++] = 0x21;
    payload[pos++] = 0xf9;
    payload[pos++] = 0x04;
    payload[pos++] = 0x01;
    payload[pos++] = 0;
    payload[pos++] = 0;
    payload[pos++] = 0;
    payload[pos++] = 0;
    payload[pos++] = 0x2c;
    payload[pos++] = 0;
    payload[pos++] = 0;
    payload[pos++] = 0;
    payload[pos++] = 0;
    payload[pos++] = (u8)((u32)width & 0xffu);
    payload[pos++] = (u8)((u32)width >> 8);
    payload[pos++] = (u8)((u32)height & 0xffu);
    payload[pos++] = (u8)((u32)height >> 8);
    payload[pos++] = 0;
    payload[pos++] = 8;
    for (u32 dataPos = 0; dataPos < lzwLen;)
    {
        u32 count = lzwLen - dataPos;
        if (count > 255u)
            count = 255u;
        payload[pos++] = (u8)count;
        memcpy(payload + pos, lzw + dataPos, count);
        pos += count;
        dataPos += count;
    }
    payload[pos++] = 0;
    if (pos != payloadLen)
        goto done;
    vm_mock_admin_preview_write_le32(raw, 0, payloadLen + 1u);
    raw[4] = 1u;
    memcpy(raw + 5, payload, payloadLen);
    {
        GifOutput decoded;
        int allocationSize = 0;

        memset(&decoded, 0, sizeof(decoded));
        if (!gifDecodeExt(payload, &decoded, 1, &allocationSize) ||
            decoded.pixels == NULL || decoded.width != (u16)width ||
            decoded.height != (u16)height)
        {
            if (decoded.owned && decoded.pixels)
                free_mem(decoded.pixels);
            goto done;
        }
        if (decoded.owned && decoded.pixels)
            free_mem(decoded.pixels);
    }
    *rawOut = raw;
    *rawLenOut = rawLen;
    if (widthOut)
        *widthOut = (u16)width;
    if (heightOut)
        *heightOut = (u16)height;
    raw = NULL;
    ok = true;

done:
    stbi_image_free(rgba);
    free(histogram);
    free(colorIndex);
    free(indices);
    free(lzw);
    free(payload);
    free(raw);
    return ok;
}

static bool vm_mock_admin_validate_content_resource(const char *name,
                                                     const char *suffix,
                                                     bool gifResource)
{
    u8 *payload = NULL;
    u8 *raw = NULL;
    u32 payloadLen = 0;
    u32 rawLen = 0;
    u8 type = 0;
    char path[1200];
    bool ok = false;

    if (!gifResource)
    {
        memset(path, 0, sizeof(path));
        if (vm_net_mock_update_resource_path(name, path, sizeof(path)) &&
            vm_mock_admin_read_raw_resource_file(path, &raw, &rawLen))
        {
            ok = vm_mock_admin_dsh_raw_is_valid(raw, rawLen);
        }
        free(raw);
        return ok;
    }
    if (!vm_mock_admin_load_data_payload(name, suffix, &payload, &payloadLen,
                                         &type))
    {
        return false;
    }
    if (type == 1u)
    {
        GifOutput image;
        int allocationSize = 0;

        memset(&image, 0, sizeof(image));
        ok = gifDecodeExt(payload, &image, 1, &allocationSize) != 0 &&
             image.pixels != NULL && image.width != 0 && image.height != 0;
        if (image.owned && image.pixels)
            free_mem(image.pixels);
    }
    free(payload);
    return ok;
}

static void vm_mock_admin_handle_content_file_upload(
    vm_mock_service_socket client, bool gifResource, const char *request,
    size_t headerLen, const u8 *body, u32 bodyLen)
{
    const u8 *fileData = NULL;
    u32 fileLen = 0;
    char filename[256];
    char resource[128];
    char resourceUtf8[256];
    char path[1200];
    u8 *previousRaw = NULL;
    u32 previousRawLen = 0;
    u8 *raw = NULL;
    u32 rawLen = 0;
    const char *names[1];
    const char *error = NULL;
    bool existed = false;
    bool pathExists = false;
    bool restored = false;
    bool changed = false;
    const char *kind = gifResource ? "gif" : "dsh";
    const char *suffix = gifResource ? ".gif" : ".dsh";
    const char *message = NULL;

    memset(filename, 0, sizeof(filename));
    memset(resource, 0, sizeof(resource));
    memset(resourceUtf8, 0, sizeof(resourceUtf8));
    memset(path, 0, sizeof(path));
    if (!vm_mock_admin_parse_multipart_upload(request, headerLen, body, bodyLen,
                                              &fileData, &fileLen, filename,
                                              sizeof(filename)) ||
        !vm_mock_admin_upload_resource_name(filename, suffix, resource,
                                            sizeof(resource)) ||
        fileLen == 0 || (!gifResource && fileLen > VM_MOCK_ADMIN_REQUEST_BODY_MAX) ||
        (gifResource && fileLen > VM_MOCK_ADMIN_GIF_UPLOAD_MAX) ||
        !vm_net_mock_update_resource_path(resource, path, sizeof(path)))
    {
        vm_mock_admin_redirect_content_resource(
            client, kind, "", "error",
            "上传文件、文件名或资源目录无效；文件名将决定服务端资源名称");
        return;
    }
    vm_net_mock_gbk_label_to_utf8(resource, resourceUtf8, sizeof(resourceUtf8));
    pathExists = vm_host_file_exists(path);
    existed = vm_mock_admin_read_raw_resource_file(path, &previousRaw,
                                                    &previousRawLen);
    if (pathExists && !existed)
    {
        vm_mock_admin_redirect_content_resource(
            client, kind, resourceUtf8, "error",
            "已有权威资源无法完整读取；为防止覆盖损坏文件已拒绝上传");
        return;
    }
    if (gifResource)
    {
        if (!vm_mock_admin_encode_uploaded_image_as_game_gif(
                fileData, fileLen, &raw, &rawLen, NULL, NULL))
        {
            free(previousRaw);
            vm_mock_admin_redirect_content_resource(
                client, kind, resourceUtf8, "error",
                "图片无法解码或超过 768×768；未写入游戏 GIF");
            return;
        }
    }
    else
    {
        if (!vm_mock_admin_dsh_raw_is_valid(fileData, fileLen))
        {
            free(previousRaw);
            vm_mock_admin_redirect_content_resource(
                client, kind, resourceUtf8, "error",
                "DSH 必须是完整的原始表格文件，未写入任何数据");
            return;
        }
        raw = (u8 *)malloc(fileLen);
        if (raw == NULL)
        {
            free(previousRaw);
            vm_mock_admin_redirect_content_resource(client, kind,
                                                    resourceUtf8, "error",
                                                    "内存不足，未写入 DSH");
            return;
        }
        memcpy(raw, fileData, fileLen);
        rawLen = fileLen;
    }
    if (!vm_mock_admin_write_resource_atomic(path, raw, rawLen) ||
        !vm_mock_admin_validate_content_resource(resource, suffix,
                                                 gifResource))
    {
        if (existed)
            restored = vm_mock_admin_write_resource_atomic(path, previousRaw,
                                                            previousRawLen);
        else
            restored = remove(path) == 0;
        free(previousRaw);
        free(raw);
        vm_mock_admin_redirect_content_resource(
            client, kind, resourceUtf8, "error",
            restored ? "资源写入后客户端格式校验失败，已恢复原文件"
                     : "资源写入后校验失败，且恢复原文件失败；请检查资源目录");
        return;
    }
    names[0] = resource;
    if (!vm_net_mock_content_update_publish_files(names, 1, &error, &changed))
    {
        if (existed)
            restored = vm_mock_admin_write_resource_atomic(path, previousRaw,
                                                            previousRawLen);
        else
            restored = remove(path) == 0;
        free(previousRaw);
        free(raw);
        vm_mock_admin_redirect_content_resource(
            client, kind, resourceUtf8, "error",
            restored ? "内容更新发布失败，已恢复原资源"
                     : "内容更新发布失败，且恢复原资源失败；请立即检查资源目录");
        return;
    }
    printf("[info][mock-admin] content_%s_upload resource=%s raw=%u publish=WT18/9+18/8->18/7\n",
           kind, resource, rawLen);
    message = changed ?
                  (gifResource ?
                       "GIF 已转换为客户端格式、校验并发布；客户端下次加载会下载新资源" :
                       "DSH 已校验并发布；客户端下次加载会下载新资源") :
                  "资源字节未变化，未重复发布内容更新";
    free(previousRaw);
    free(raw);
    vm_mock_admin_redirect_content_resource(client, kind, resourceUtf8, "ok",
                                            message);
}

/* The request dispatcher only sees a complete, NUL-terminated request.  Its
 * caller owns the allocated buffer so all existing response branches can
 * return normally without leaking a large form body. */
static int vm_mock_admin_dispatch_request(vm_mock_service_socket client,
                                          char *request,
                                          size_t headerLen,
                                          u32 contentLength)
{
    char method[12];
    char target[1024];
    char version[16];
    char *query = NULL;
    char *body = NULL;
    char *response = NULL;
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
            vm_mock_admin_send_response(
                client,
                settleResult == VM_MOCK_PAYMENT_SETTLE_INVALID ?
                    "400 Bad Request" : "200 OK",
                "text/plain; charset=utf-8", NULL,
                settleResult == VM_MOCK_PAYMENT_SETTLE_INVALID ?
                    "error_sign" : "success");
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
        char yuanText[32];
        char typeText[16];
        char payId[64];
        char location[192];
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
        memset(yuanText, 0, sizeof(yuanText));
        memset(typeText, 0, sizeof(typeText));
        if (!vm_mock_admin_form_value(body, "yuan", yuanText,
                                      sizeof(yuanText)) ||
            !vm_mock_admin_form_value(body, "pay_type", typeText,
                                      sizeof(typeText)) ||
            !vm_net_mock_parse_u32_strict(yuanText, &yuan) || yuan == 0 ||
            !vm_net_mock_parse_u32_strict(typeText, &payType))
        {
            vm_mock_user_redirect_message(client, "error", "充值参数无效");
            return 0;
        }
        if (!vm_mock_payment_create_order(session->accountId, yuan, payType,
                                          payId, &message))
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
            bool banned = false;

            ok = vm_mock_service_account_verify_credentials(account, password);
            if (ok && !vm_mock_service_account_access_ban_check(
                          account, &banned, NULL, 0))
            {
                ok = false;
                message = "账号访问状态暂不可用，请稍后重试";
            }
            else if (ok && banned)
            {
                ok = false;
                message = "该账号已被封禁，无法登录";
            }
            else if (!ok)
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
    if (strcmp(target, "/user/role-transfer/export") == 0)
    {
        vm_mock_user_session *session = NULL;
        char roleText[32];
        char verificationCode[9];
        char messageText[384];
        const char *error = NULL;
        u32 roleId = 0;
        bool ok = false;

        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n",
                                        "角色迁出只允许 POST。\n");
            return 0;
        }
        session = vm_mock_user_request_session(request, headerLen);
        if (session == NULL)
        {
            vm_mock_admin_send_location(client, "/", NULL);
            return 0;
        }
        memset(roleText, 0, sizeof(roleText));
        memset(verificationCode, 0, sizeof(verificationCode));
        if (!vm_mock_admin_form_value(body, "role_id", roleText,
                                      sizeof(roleText)) ||
            !vm_net_mock_parse_u32_strict(roleText, &roleId) || roleId == 0)
        {
            vm_mock_user_redirect_message(client, "error", "迁出角色参数无效");
            return 0;
        }
        ok = vm_mock_user_role_transfer_create_code(session->accountId, roleId,
                                                    verificationCode, &error);
        if (ok)
        {
            snprintf(messageText, sizeof(messageText),
                     "角色迁出验证码：%s。请在目标账号输入；15 分钟内有效且仅可使用一次。",
                     verificationCode);
            printf("[info][user-web] role_transfer_export account=%s role=%u "
                   "result=success\n", session->accountId, roleId);
            vm_mock_user_redirect_message(client, "ok", messageText);
        }
        else
        {
            printf("[warn][user-web] role_transfer_export account=%s role=%u "
                   "result=rejected\n", session->accountId, roleId);
            vm_mock_user_redirect_message(client, "error",
                                          error ? error : "生成迁出验证码失败");
        }
        return ok ? 1 : 0;
    }
    if (strcmp(target, "/user/role-transfer/import") == 0)
    {
        vm_mock_user_session *session = NULL;
        char verificationCode[16];
        char sourceAccountId[64];
        char migratedSourceAccountId[64];
        char messageText[384];
        const char *error = NULL;
        u32 sourceRoleId = 0;
        u32 migratedSourceRoleId = 0;
        u32 targetRoleId = 0;
        u32 sourceDisconnected = 0;
        u32 targetDisconnected = 0;
        bool ok = false;

        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n",
                                        "角色迁入只允许 POST。\n");
            return 0;
        }
        session = vm_mock_user_request_session(request, headerLen);
        if (session == NULL)
        {
            vm_mock_admin_send_location(client, "/", NULL);
            return 0;
        }
        memset(verificationCode, 0, sizeof(verificationCode));
        memset(sourceAccountId, 0, sizeof(sourceAccountId));
        memset(migratedSourceAccountId, 0, sizeof(migratedSourceAccountId));
        if (!vm_mock_admin_form_value(body, "code", verificationCode,
                                      sizeof(verificationCode)) ||
            !vm_mock_user_role_transfer_code_is_valid(verificationCode))
        {
            vm_mock_user_redirect_message(client, "error", "请输入有效的 8 位迁移验证码");
            return 0;
        }
        if (!vm_mock_user_role_transfer_peek_code(verificationCode,
                                                  sourceAccountId,
                                                  sizeof(sourceAccountId),
                                                  &sourceRoleId))
        {
            vm_mock_user_redirect_message(client, "error", "验证码无效或已过期");
            return 0;
        }
        if (strcmp(sourceAccountId, session->accountId) == 0)
        {
            vm_mock_user_redirect_message(client, "error", "不能迁入当前账号的角色");
            return 0;
        }
        /* Only a committed migration changes live sessions.  Invalid codes,
         * full target accounts, and database failures leave both players online. */
        ok = vm_mock_user_role_transfer_claim_code(
            session->accountId, verificationCode, migratedSourceAccountId,
            sizeof(migratedSourceAccountId), &migratedSourceRoleId,
            &targetRoleId, &error);
        if (ok)
        {
            targetDisconnected = vm_mock_service_account_disconnect_bound_sessions(
                session->accountId, "user-role-transfer-import-target");
            sourceDisconnected = vm_mock_service_account_disconnect_bound_sessions(
                migratedSourceAccountId, "user-role-transfer-import-source");
            snprintf(messageText, sizeof(messageText),
                     "角色已迁入当前账号（新角色 ID：%u）。角色资料、背包、装备、任务、"
                     "好友、帮派与聊天历史均已保留；两边游戏连接已断开，请重新登录游戏。",
                     targetRoleId);
            printf("[info][user-web] role_transfer_import source=%s role=%u "
                   "target=%s new_role=%u result=success disconnected=%u/%u\n",
                   migratedSourceAccountId, migratedSourceRoleId,
                   session->accountId, targetRoleId, sourceDisconnected,
                   targetDisconnected);
            vm_mock_user_redirect_message(client, "ok", messageText);
        }
        else
        {
            printf("[warn][user-web] role_transfer_import source=%s role=%u "
                   "target=%s result=rejected\n", sourceAccountId, sourceRoleId,
                   session->accountId);
            vm_mock_user_redirect_message(client, "error",
                                          error ? error : "角色迁入失败");
        }
        return ok ? 1 : 0;
    }
    if (strcmp(target, "/user/backpack/delete") == 0)
    {
        vm_mock_user_session *session = NULL;
        char roleText[32];
        char itemText[32];
        char seqText[32];
        const char *error = NULL;
        u32 roleId = 0;
        u32 itemId = 0;
        u32 itemSeq = 0;
        bool ok = false;

        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n",
                                        "删除背包物品只允许 POST。\n");
            return 0;
        }
        session = vm_mock_user_request_session(request, headerLen);
        if (session == NULL)
        {
            vm_mock_admin_send_location(client, "/", NULL);
            return 0;
        }
        memset(roleText, 0, sizeof(roleText));
        memset(itemText, 0, sizeof(itemText));
        memset(seqText, 0, sizeof(seqText));
        if (!vm_mock_admin_form_value(body, "role_id", roleText,
                                      sizeof(roleText)) ||
            !vm_mock_admin_form_value(body, "item_id", itemText,
                                      sizeof(itemText)) ||
            !vm_mock_admin_form_value(body, "item_seq", seqText,
                                      sizeof(seqText)) ||
            !vm_net_mock_parse_u32_strict(roleText, &roleId) || roleId == 0 ||
            !vm_net_mock_parse_u32_strict(itemText, &itemId) || itemId == 0 ||
            !vm_net_mock_parse_u32_strict(seqText, &itemSeq) || itemSeq == 0 ||
            itemSeq > 0xffffu)
        {
            vm_mock_user_redirect_message(client, "error", "背包物品参数无效");
            return 0;
        }
        /* accountId comes exclusively from the authenticated cookie session;
         * a posted account id can never select another user's backpack. */
        ok = vm_mock_service_account_remove_role_backpack_item(
            session->accountId, roleId, itemId, (u16)itemSeq, &error);
        vm_mock_user_redirect_message(client, ok ? "ok" : "error",
                                      ok ? "物品已丢弃" :
                                           (error ? error : "丢弃物品失败"));
        return ok ? 1 : 0;
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
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_LOGIN_SCRIPT_PATH) == 0)
    {
        vm_mock_admin_send_response(client, "200 OK",
                                    "application/javascript; charset=utf-8",
                                    NULL, g_vm_mock_admin_login_script);
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
                                    "Cache-Control: no-store, max-age=0\r\n",
                                    g_vm_mock_admin_script);
        return 1;
    }
    if (strcmp(method, "GET") == 0 &&
        strcmp(target, VM_MOCK_ADMIN_MONSTER_BOSS_EXPORT_PATH) == 0)
    {
        return vm_mock_admin_handle_monster_boss_drop_export(client);
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
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/gif-preview.bmp") == 0)
    {
        char resourceUtf8[256];
        char resource[128];
        u8 *bmp = NULL;
        u32 bmpLen = 0;

        memset(resourceUtf8, 0, sizeof(resourceUtf8));
        memset(resource, 0, sizeof(resource));
        if (!vm_mock_admin_form_value(query, "gif", resourceUtf8,
                                      sizeof(resourceUtf8)) ||
            !vm_mock_admin_utf8_to_gbk_text(resourceUtf8, resource,
                                            sizeof(resource), false) ||
            !vm_mock_admin_build_gif_preview_bmp(resource, &bmp, &bmpLen,
                                                 NULL, NULL))
        {
            vm_mock_admin_send_response(client, "422 Unprocessable Content",
                                        NULL, NULL,
                                        "GIF 资源无法生成预览。\n");
            free(bmp);
            return 0;
        }
        vm_mock_admin_send_binary_response(client, "200 OK", "image/bmp",
                                           bmp, bmpLen);
        free(bmp);
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
    if (strcmp(target, VM_MOCK_ADMIN_ACCOUNT_LIST_PATH) == 0)
    {
        if (strcmp(method, "GET") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: GET\r\n",
                                        "账号列表只允许 GET。\n");
            return 0;
        }
        return vm_mock_admin_handle_account_list_request(client, query);
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
    if (strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/gif-upload") == 0 ||
        strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/dsh-upload") == 0)
    {
        if (strcmp(method, "POST") != 0)
        {
            vm_mock_admin_send_response(client, "405 Method Not Allowed", NULL,
                                        "Allow: POST\r\n", "只允许 POST。\n");
            return 0;
        }
        vm_mock_admin_handle_content_file_upload(
            client, strcmp(target, VM_MOCK_ADMIN_BASE_PATH "/gif-upload") == 0,
            request, headerLen, (const u8 *)body, contentLength);
        return 1;
    }
    vm_mock_admin_send_response(client, "404 Not Found", NULL, NULL, "页面不存在。\n");
    return 0;
}

static bool vm_mock_admin_request_total_length(size_t headerLen,
                                               u32 contentLength,
                                               size_t *totalLenOut)
{
    size_t bodyLen = (size_t)contentLength;

    if (totalLenOut == NULL || headerLen > VM_MOCK_ADMIN_HEADER_MAX ||
        bodyLen > VM_MOCK_ADMIN_REQUEST_BODY_MAX)
        return false;
    *totalLenOut = headerLen + bodyLen;
    return true;
}

/* Receive headers into a small fixed buffer, then size the request allocation
 * from Content-Length.  This keeps the HTTP framing limit separate from the
 * form payload size used by large editor pages such as chest rewards. */
static int vm_mock_admin_handle_client(vm_mock_service_socket client)
{
    char headers[VM_MOCK_ADMIN_HEADER_MAX + 1];
    char *headerEnd = NULL;
    char *request = NULL;
    size_t received = 0;
    size_t headerLen = 0;
    size_t totalLen = 0;
    u32 contentLength = 0;
    int timeoutMs = VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS;
    int result = 0;

#ifdef _WIN32
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs,
               sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs,
               sizeof(timeoutMs));
#else
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
#endif
    memset(headers, 0, sizeof(headers));
    while (received < VM_MOCK_ADMIN_HEADER_MAX)
    {
        int rc = recv(client, headers + received,
                      (int)(VM_MOCK_ADMIN_HEADER_MAX - received), 0);

        if (rc <= 0)
            break;
        received += (size_t)rc;
        headers[received] = 0;
        headerEnd = strstr(headers, "\r\n\r\n");
        if (headerEnd != NULL)
            break;
    }
    if (headerEnd == NULL)
    {
        vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL,
                                    "请求头不完整或过长。\n");
        return 0;
    }
    headerLen = (size_t)(headerEnd - headers) + 4u;
    if (!vm_mock_admin_parse_content_length(headers, headerLen, &contentLength) ||
        !vm_mock_admin_request_total_length(headerLen, contentLength, &totalLen))
    {
        vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL,
                                    "请求长度无效。\n");
        return 0;
    }
    request = (char *)malloc(totalLen + 1u);
    if (request == NULL)
    {
        vm_mock_admin_send_response(client, "500 Internal Server Error", NULL,
                                    NULL, "内存不足。\n");
        return 0;
    }
    if (received > totalLen)
        received = totalLen;
    memcpy(request, headers, received);
    while (received < totalLen)
    {
        int rc = recv(client, request + received, (int)(totalLen - received), 0);

        if (rc <= 0)
            break;
        received += (size_t)rc;
    }
    if (received != totalLen)
    {
        vm_mock_admin_send_response(client, "400 Bad Request", NULL, NULL,
                                    "请求不完整。\n");
        goto done;
    }
    request[totalLen] = 0;
    result = vm_mock_admin_dispatch_request(client, request, headerLen,
                                             contentLength);

done:
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
