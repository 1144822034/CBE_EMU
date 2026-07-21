/* W-coin recharge support for the user-facing account center.
 *
 * The provider secret stays in MySQL (or a process environment override).
 * All provider responses are untrusted until the documented callback MD5 has
 * been verified and the local order amount, payment type and nonce match.
 */

enum
{
    VM_MOCK_PAYMENT_STATUS_CREATING = 0,
    VM_MOCK_PAYMENT_STATUS_WAITING = 1,
    VM_MOCK_PAYMENT_STATUS_PAID_PENDING = 2,
    VM_MOCK_PAYMENT_STATUS_CREDITED = 3,
    VM_MOCK_PAYMENT_STATUS_EXPIRED = 4,
    VM_MOCK_PAYMENT_STATUS_FAILED = 5,
    VM_MOCK_PAYMENT_HTTP_RESPONSE_MAX = 65536,
    VM_MOCK_PAYMENT_RECENT_MAX = 5
};

typedef struct
{
    bool found;
    bool invalid;
    bool enabled;
    char apiBaseUrl[256];
    char secretKey[129];
    char callbackBaseUrl[256];
    u32 wcoinPerYuan;
    u32 minimumYuan;
    u32 maximumYuan;
} vm_mock_payment_config;

typedef struct
{
    bool found;
    bool invalid;
    char payId[64];
    char providerOrderId[64];
    char accountId[64];
    u32 roleId;
    u32 payType;
    u32 priceCents;
    u32 reallyPriceCents;
    u32 wcoinAmount;
    char requestParam[64];
    char payUrl[1025];
    u32 status;
    int providerState;
    u32 timeoutMinutes;
    bool credited;
    u32 lastCheckedAt;
    u32 createdAt;
    u32 creditedAt;
} vm_mock_payment_order;

typedef struct
{
    int code;
    char payId[64];
    char orderId[64];
    char payUrl[1025];
    u32 reallyPriceCents;
    int state;
    u32 timeoutMinutes;
} vm_mock_payment_create_result;

typedef struct
{
    char payId[64];
    char param[64];
    char typeText[16];
    char priceText[32];
    char reallyPriceText[32];
    char sign[65];
    u32 payType;
    u32 priceCents;
    u32 reallyPriceCents;
} vm_mock_payment_callback;

typedef enum
{
    VM_MOCK_PAYMENT_SETTLE_INVALID = 0,
    VM_MOCK_PAYMENT_SETTLE_PENDING = 1,
    VM_MOCK_PAYMENT_SETTLE_CREDITED = 2
} vm_mock_payment_settle_result;

typedef struct
{
    bool found;
    bool invalid;
    u32 value;
} vm_mock_payment_u32_row;

typedef struct
{
    bool found;
    bool invalid;
    uint64_t value;
} vm_mock_payment_u64_row;

typedef struct
{
    vm_mock_payment_order rows[VM_MOCK_PAYMENT_RECENT_MAX];
    u32 count;
    bool invalid;
} vm_mock_payment_recent_rows;

static bool vm_mock_payment_parse_u64(const char *value, size_t valueLen,
                                      uint64_t *out)
{
    uint64_t result = 0;

    if (out)
        *out = 0;
    if (value == NULL || valueLen == 0)
        return false;
    for (size_t i = 0; i < valueLen; ++i)
    {
        unsigned char ch = (unsigned char)value[i];
        if (ch < '0' || ch > '9' ||
            result > (UINT64_MAX - (uint64_t)(ch - '0')) / 10u)
            return false;
        result = result * 10u + (uint64_t)(ch - '0');
    }
    if (out)
        *out = result;
    return true;
}

static bool vm_mock_payment_parse_int(const char *value, size_t valueLen,
                                      int *out)
{
    char text[32];
    char *end = NULL;
    long parsed = 0;

    if (out)
        *out = 0;
    if (value == NULL || valueLen == 0 || valueLen >= sizeof(text))
        return false;
    memcpy(text, value, valueLen);
    text[valueLen] = 0;
    parsed = strtol(text, &end, 10);
    if (end == text || *end != 0 || parsed < -32768 || parsed > 32767)
        return false;
    if (out)
        *out = (int)parsed;
    return true;
}

static bool vm_mock_payment_decimal_to_cents(const char *text, u32 *centsOut)
{
    uint64_t whole = 0;
    u32 fraction = 0;
    u32 fractionDigits = 0;
    bool dot = false;
    bool haveDigit = false;

    if (centsOut)
        *centsOut = 0;
    if (text == NULL || text[0] == 0)
        return false;
    for (const unsigned char *p = (const unsigned char *)text; *p != 0; ++p)
    {
        if (*p == '.')
        {
            if (dot)
                return false;
            dot = true;
            continue;
        }
        if (*p < '0' || *p > '9')
            return false;
        haveDigit = true;
        if (!dot)
        {
            if (whole > (UINT32_MAX / 100u - (uint64_t)(*p - '0')) / 10u)
                return false;
            whole = whole * 10u + (uint64_t)(*p - '0');
        }
        else
        {
            if (fractionDigits >= 2)
                return false;
            fraction = fraction * 10u + (u32)(*p - '0');
            ++fractionDigits;
        }
    }
    if (!haveDigit)
        return false;
    if (fractionDigits == 1)
        fraction *= 10u;
    if (whole > (UINT32_MAX - fraction) / 100u)
        return false;
    if (centsOut)
        *centsOut = (u32)(whole * 100u + fraction);
    return true;
}

static bool vm_mock_payment_constant_sign_equal(const char *left,
                                                const char *right)
{
    unsigned int difference = 0;

    if (left == NULL || right == NULL || strlen(left) != 32 || strlen(right) != 32)
        return false;
    for (u32 i = 0; i < 32; ++i)
        difference |= (unsigned int)(tolower((unsigned char)left[i]) ^
                                     tolower((unsigned char)right[i]));
    return difference == 0;
}

static bool vm_mock_payment_payload_chars_are_safe(const char *value)
{
    if (value == NULL || value[0] == 0)
        return false;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != 0; ++cursor)
    {
        if (*cursor < 0x20u || *cursor == 0x7fu)
            return false;
    }
    return true;
}

static bool vm_mock_payment_url_has_scheme(const char *url,
                                           const char *const *schemes,
                                           size_t schemeCount)
{
    if (!vm_mock_payment_payload_chars_are_safe(url))
        return false;
    for (size_t i = 0; i < schemeCount; ++i)
    {
        size_t length = strlen(schemes[i]);
        if (strlen(url) >= length &&
            vm_mock_admin_ascii_ncasecmp(url, schemes[i], length) == 0)
            return true;
    }
    return false;
}

static bool vm_mock_payment_qr_payload_is_safe(const char *url)
{
    static const char *schemes[] = {
        "http://", "https://", "wxp://", "weixin://", "alipays://"
    };

    return vm_mock_payment_url_has_scheme(
        url, schemes, sizeof(schemes) / sizeof(schemes[0]));
}

static bool vm_mock_payment_click_url_is_safe(const char *url)
{
    static const char *schemes[] = { "http://", "https://" };

    return vm_mock_payment_url_has_scheme(
        url, schemes, sizeof(schemes) / sizeof(schemes[0]));
}

static bool vm_mock_payment_config_row(void *contextValue,
                                       unsigned int columnCount,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_mock_payment_config *config = (vm_mock_payment_config *)contextValue;
    size_t decoded = 0;
    u32 enabled = 0;

    if (config == NULL || config->found || columnCount != 7)
        return true;
    if (values[0] == NULL || values[1] == NULL || values[2] == NULL ||
        values[3] == NULL || values[4] == NULL || values[5] == NULL ||
        values[6] == NULL ||
        !vm_mysql_hex_decode(values[0], lengths[0], config->apiBaseUrl,
                             sizeof(config->apiBaseUrl) - 1, &decoded))
    {
        config->invalid = true;
        return true;
    }
    config->apiBaseUrl[decoded] = 0;
    if (!vm_mysql_hex_decode(values[1], lengths[1], config->secretKey,
                             sizeof(config->secretKey) - 1, &decoded))
    {
        config->invalid = true;
        return true;
    }
    config->secretKey[decoded] = 0;
    if (!vm_mysql_hex_decode(values[2], lengths[2], config->callbackBaseUrl,
                             sizeof(config->callbackBaseUrl) - 1, &decoded) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &config->wcoinPerYuan) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &config->minimumYuan) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &config->maximumYuan) ||
        !vm_mock_mysql_parse_u32(values[6], lengths[6], &enabled) || enabled > 1)
    {
        config->invalid = true;
        return true;
    }
    config->callbackBaseUrl[decoded] = 0;
    config->enabled = enabled != 0;
    config->found = true;
    return true;
}

static bool vm_mock_payment_ensure_tables(void)
{
    static bool checked = false;
    static bool available = false;
    char digest[33];

    if (checked)
        return available;
    checked = true;
    vm_md5_hex("1547129707139vone66620.1a7cc8678193ee9c70ae3d75fd04ae6a9",
               strlen("1547129707139vone66620.1a7cc8678193ee9c70ae3d75fd04ae6a9"),
               digest);
    if (strcmp(digest, "2b8b5d58c51203162f14939bdbc46a54") != 0)
    {
        printf("[error][payment] md5_self_test_failed\n");
        return false;
    }
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_payment_config ("
            "config_id TINYINT UNSIGNED NOT NULL,"
            "api_base_url VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "secret_key VARBINARY(128) NOT NULL,"
            "callback_base_url VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "wcoin_per_yuan INT UNSIGNED NOT NULL DEFAULT 1000,"
            "minimum_yuan INT UNSIGNED NOT NULL DEFAULT 1,"
            "maximum_yuan INT UNSIGNED NOT NULL DEFAULT 10000,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(config_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "INSERT IGNORE INTO server_payment_config "
            "(config_id,api_base_url,secret_key,callback_base_url,wcoin_per_yuan,minimum_yuan,maximum_yuan,enabled) "
            "VALUES(1,'http://pay.cbhub.top/','', '',1000,1,10000,1)") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS wcoin_recharge_orders ("
            "pay_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "provider_order_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NULL DEFAULT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,pay_type TINYINT UNSIGNED NOT NULL,"
            "price_cents INT UNSIGNED NOT NULL,really_price_cents INT UNSIGNED NOT NULL DEFAULT 0,"
            "wcoin_amount INT UNSIGNED NOT NULL,"
            "request_param VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "pay_url VARCHAR(1024) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,provider_state SMALLINT NOT NULL DEFAULT 0,"
            "timeout_minutes SMALLINT UNSIGNED NOT NULL DEFAULT 5,credited TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "last_checked_at TIMESTAMP NULL DEFAULT NULL,paid_at TIMESTAMP NULL DEFAULT NULL,"
            "credited_at TIMESTAMP NULL DEFAULT NULL,created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(pay_id),UNIQUE KEY uk_wcoin_recharge_provider_order(provider_order_id),"
            "KEY idx_wcoin_recharge_account(account_id,created_at),"
            "KEY idx_wcoin_recharge_pending(status,created_at),"
            "CONSTRAINT fk_wcoin_recharge_account FOREIGN KEY(account_id) REFERENCES accounts(account_id) ON DELETE RESTRICT) "
            "ENGINE=InnoDB"))
    {
        printf("[error][payment] schema_unavailable error=%s\n", vm_mysql_last_error());
        return false;
    }
    available = true;
    return true;
}

static bool vm_mock_payment_load_config(vm_mock_payment_config *config)
{
    const char *environmentKey = getenv("CBE_PAYMENT_KEY");
    const char *environmentCallback = getenv("CBE_PAYMENT_CALLBACK_BASE_URL");

    if (config == NULL)
        return false;
    memset(config, 0, sizeof(*config));
    if (!vm_mock_payment_ensure_tables() ||
        !vm_mysql_query(
            "SELECT HEX(api_base_url),HEX(secret_key),HEX(callback_base_url),"
            "wcoin_per_yuan,minimum_yuan,maximum_yuan,enabled "
            "FROM server_payment_config WHERE config_id=1",
            vm_mock_payment_config_row, config) ||
        config->invalid || !config->found)
        return false;

    if (environmentKey != NULL && environmentKey[0] != 0 &&
        strlen(environmentKey) < sizeof(config->secretKey))
        snprintf(config->secretKey, sizeof(config->secretKey), "%s", environmentKey);
    if (environmentCallback != NULL && environmentCallback[0] != 0 &&
        strlen(environmentCallback) < sizeof(config->callbackBaseUrl))
        snprintf(config->callbackBaseUrl, sizeof(config->callbackBaseUrl), "%s",
                 environmentCallback);
    if (strncmp(config->apiBaseUrl, "http://", 7) != 0 ||
        config->secretKey[0] == 0 || config->wcoinPerYuan == 0 ||
        config->minimumYuan == 0 || config->maximumYuan < config->minimumYuan)
        return false;
    return config->enabled;
}

typedef struct
{
    char host[256];
    u16 port;
    char prefix[256];
} vm_mock_payment_http_target;

static bool vm_mock_payment_parse_http_base(const char *base,
                                            vm_mock_payment_http_target *target)
{
    const char *authority = NULL;
    const char *slash = NULL;
    const char *colon = NULL;
    size_t hostLength = 0;

    if (target == NULL)
        return false;
    memset(target, 0, sizeof(*target));
    if (base == NULL || strncmp(base, "http://", 7) != 0)
        return false;
    authority = base + 7;
    slash = strchr(authority, '/');
    if (slash == NULL)
        slash = authority + strlen(authority);
    colon = memchr(authority, ':', (size_t)(slash - authority));
    hostLength = (size_t)((colon ? colon : slash) - authority);
    if (hostLength == 0 || hostLength >= sizeof(target->host))
        return false;
    memcpy(target->host, authority, hostLength);
    target->host[hostLength] = 0;
    target->port = 80;
    if (colon != NULL)
    {
        char portText[8];
        u32 port = 0;
        size_t portLength = (size_t)(slash - colon - 1);
        if (portLength == 0 || portLength >= sizeof(portText))
            return false;
        memcpy(portText, colon + 1, portLength);
        portText[portLength] = 0;
        if (!vm_net_mock_parse_u32_strict(portText, &port) || port == 0 || port > 65535)
            return false;
        target->port = (u16)port;
    }
    if (*slash == 0)
        snprintf(target->prefix, sizeof(target->prefix), "/");
    else if (strlen(slash) < sizeof(target->prefix))
        snprintf(target->prefix, sizeof(target->prefix), "%s", slash);
    else
        return false;
    if (strchr(target->host, '@') != NULL || strchr(target->host, '\\') != NULL ||
        strchr(target->prefix, '?') != NULL || strchr(target->prefix, '#') != NULL)
        return false;
    return true;
}

static bool vm_mock_payment_http_dechunk(const char *input, size_t inputLength,
                                         char *output, size_t outputCap,
                                         size_t *outputLength)
{
    size_t inputPosition = 0;
    size_t outputPosition = 0;

    if (outputLength)
        *outputLength = 0;
    while (inputPosition < inputLength)
    {
        const char *lineEnd = NULL;
        char sizeText[24];
        char *end = NULL;
        unsigned long chunkLength = 0;
        size_t sizeLength = 0;

        lineEnd = strstr(input + inputPosition, "\r\n");
        if (lineEnd == NULL || lineEnd >= input + inputLength)
            return false;
        sizeLength = (size_t)(lineEnd - (input + inputPosition));
        if (sizeLength == 0 || sizeLength >= sizeof(sizeText))
            return false;
        memcpy(sizeText, input + inputPosition, sizeLength);
        sizeText[sizeLength] = 0;
        if (strchr(sizeText, ';') != NULL)
            *strchr(sizeText, ';') = 0;
        chunkLength = strtoul(sizeText, &end, 16);
        if (end == sizeText || *end != 0)
            return false;
        inputPosition = (size_t)(lineEnd - input) + 2;
        if (chunkLength == 0)
        {
            if (outputPosition >= outputCap)
                return false;
            output[outputPosition] = 0;
            if (outputLength)
                *outputLength = outputPosition;
            return true;
        }
        if (chunkLength > inputLength - inputPosition ||
            outputPosition + chunkLength >= outputCap)
            return false;
        memcpy(output + outputPosition, input + inputPosition, chunkLength);
        outputPosition += chunkLength;
        inputPosition += chunkLength;
        if (inputPosition + 2 > inputLength || input[inputPosition] != '\r' ||
            input[inputPosition + 1] != '\n')
            return false;
        inputPosition += 2;
    }
    return false;
}

static bool vm_mock_payment_http_post_locked(const char *apiBase,
                                             const char *endpoint,
                                             const char *form,
                                             char *bodyOut,
                                             size_t bodyOutCap,
                                             int *httpStatusOut)
{
    vm_mock_payment_http_target target;
    vm_mock_service_socket socketValue = VM_MOCK_SERVICE_INVALID_SOCKET;
    char path[512];
    char request[8192];
    char *response = NULL;
    size_t responseLength = 0;
    bool ok = false;
    int httpStatus = 0;
    u32 started = scheduler_get_tick_ms();

    if (bodyOut != NULL && bodyOutCap != 0)
        bodyOut[0] = 0;
    if (httpStatusOut)
        *httpStatusOut = 0;
    if (bodyOut == NULL || bodyOutCap == 0 || endpoint == NULL || form == NULL ||
        !vm_mock_payment_parse_http_base(apiBase, &target))
        return false;
    if (target.prefix[strlen(target.prefix) - 1] == '/')
        snprintf(path, sizeof(path), "%s%s", target.prefix,
                 endpoint[0] == '/' ? endpoint + 1 : endpoint);
    else
        snprintf(path, sizeof(path), "%s/%s", target.prefix,
                 endpoint[0] == '/' ? endpoint + 1 : endpoint);
    if (snprintf(request, sizeof(request),
                 "POST %s HTTP/1.1\r\nHost: %s%s%u\r\n"
                 "Content-Type: application/x-www-form-urlencoded\r\n"
                 "Content-Length: %u\r\nConnection: close\r\n"
                 "Accept: application/json\r\nUser-Agent: JianghuOL-Recharge/1.0\r\n\r\n%s",
                 path, target.host, target.port == 80 ? "" : ":",
                 target.port == 80 ? 0 : target.port,
                 (u32)strlen(form), form) >= (int)sizeof(request))
        return false;
    /* Avoid the cosmetic ':0' produced by the compact format above. */
    if (target.port == 80)
    {
        if (snprintf(request, sizeof(request),
                     "POST %s HTTP/1.1\r\nHost: %s\r\n"
                     "Content-Type: application/x-www-form-urlencoded\r\n"
                     "Content-Length: %u\r\nConnection: close\r\n"
                     "Accept: application/json\r\nUser-Agent: JianghuOL-Recharge/1.0\r\n\r\n%s",
                     path, target.host, (u32)strlen(form), form) >= (int)sizeof(request))
            return false;
    }

    response = (char *)malloc(VM_MOCK_PAYMENT_HTTP_RESPONSE_MAX + 1);
    if (response == NULL)
        return false;

    /* The admin worker owns the protocol mutex when entering this function.
     * DNS and remote socket waits do not touch emulator/account state. */
    pthread_mutex_unlock(&g_vm_mock_service_protocol_mutex);
    if (vm_mock_service_connect(target.host, target.port, &socketValue) &&
        vm_mock_service_send_all(socketValue, (const u8 *)request,
                                 (u32)strlen(request)))
    {
        while (responseLength < VM_MOCK_PAYMENT_HTTP_RESPONSE_MAX)
        {
            int received = recv(socketValue, response + responseLength,
                                (int)(VM_MOCK_PAYMENT_HTTP_RESPONSE_MAX - responseLength), 0);
            if (received <= 0)
                break;
            responseLength += (size_t)received;
        }
    }
    vm_mock_service_socket_close(socketValue);
    pthread_mutex_lock(&g_vm_mock_service_protocol_mutex);

    response[responseLength] = 0;
    if (responseLength != 0)
    {
        char *headerEnd = strstr(response, "\r\n\r\n");
        if (sscanf(response, "HTTP/%*u.%*u %d", &httpStatus) == 1 &&
            headerEnd != NULL)
        {
            const char *body = headerEnd + 4;
            size_t bodyLength = responseLength - (size_t)(body - response);
            bool chunked = false;
            const char *cursor = response;

            while (cursor < headerEnd)
            {
                const char *lineEnd = strstr(cursor, "\r\n");
                if (lineEnd == NULL || lineEnd > headerEnd)
                    break;
                if ((size_t)(lineEnd - cursor) >= 18 &&
                    vm_mock_admin_ascii_ncasecmp(cursor, "Transfer-Encoding:", 18) == 0 &&
                    strstr(cursor + 18, "chunked") != NULL &&
                    strstr(cursor + 18, "chunked") < lineEnd)
                    chunked = true;
                cursor = lineEnd + 2;
            }
            if (chunked)
                ok = vm_mock_payment_http_dechunk(body, bodyLength, bodyOut,
                                                  bodyOutCap, NULL);
            else if (bodyLength < bodyOutCap)
            {
                memcpy(bodyOut, body, bodyLength);
                bodyOut[bodyLength] = 0;
                ok = true;
            }
            ok = ok && httpStatus >= 200 && httpStatus < 300;
        }
    }
    if (httpStatusOut)
        *httpStatusOut = httpStatus;
    printf("[info][payment] provider_http endpoint=%s http=%d ok=%u elapsed_ms=%u\n",
           endpoint, httpStatus, ok ? 1u : 0u,
           scheduler_get_tick_ms() >= started ? scheduler_get_tick_ms() - started : 0);
    free(response);
    return ok;
}

static const char *vm_mock_payment_json_field(const char *json,
                                              const char *field)
{
    char needle[96];
    const char *cursor = NULL;

    if (json == NULL || field == NULL ||
        snprintf(needle, sizeof(needle), "\"%s\"", field) >= (int)sizeof(needle))
        return NULL;
    cursor = json;
    while ((cursor = strstr(cursor, needle)) != NULL)
    {
        const char *value = cursor + strlen(needle);
        while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
            ++value;
        if (*value++ != ':')
        {
            cursor += strlen(needle);
            continue;
        }
        while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
            ++value;
        return value;
    }
    return NULL;
}

static bool vm_mock_payment_json_int(const char *json, const char *field,
                                     int *valueOut)
{
    const char *value = vm_mock_payment_json_field(json, field);
    char *end = NULL;
    long parsed = 0;

    if (valueOut)
        *valueOut = 0;
    if (value == NULL)
        return false;
    parsed = strtol(value, &end, 10);
    if (end == value || parsed < INT32_MIN || parsed > INT32_MAX)
        return false;
    if (valueOut)
        *valueOut = (int)parsed;
    return true;
}

static bool vm_mock_payment_json_string(const char *json, const char *field,
                                        char *out, size_t outCap)
{
    const char *value = vm_mock_payment_json_field(json, field);
    size_t position = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (value == NULL || *value++ != '"')
        return false;
    while (*value != 0 && *value != '"')
    {
        unsigned char ch = (unsigned char)*value++;
        if (ch == '\\')
        {
            ch = (unsigned char)*value++;
            switch (ch)
            {
            case '"': case '\\': case '/': break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            default: return false;
            }
        }
        if (position + 1 >= outCap || ch == 0)
            return false;
        out[position++] = (char)ch;
    }
    if (*value != '"')
        return false;
    out[position] = 0;
    return true;
}

static bool vm_mock_payment_json_decimal(const char *json, const char *field,
                                         u32 *centsOut)
{
    const char *value = vm_mock_payment_json_field(json, field);
    char text[32];
    size_t length = 0;
    char quote = 0;

    if (value == NULL)
        return false;
    if (*value == '"')
        quote = *value++;
    while (value[length] != 0 && length + 1 < sizeof(text) &&
           ((value[length] >= '0' && value[length] <= '9') || value[length] == '.'))
        ++length;
    if (length == 0 || (quote != 0 && value[length] != quote))
        return false;
    memcpy(text, value, length);
    text[length] = 0;
    return vm_mock_payment_decimal_to_cents(text, centsOut);
}

static bool vm_mock_payment_parse_create_result(const char *json,
                                                vm_mock_payment_create_result *result)
{
    int timeout = 0;

    if (result == NULL)
        return false;
    memset(result, 0, sizeof(*result));
    if (!vm_mock_payment_json_int(json, "code", &result->code) ||
        result->code != 1 ||
        !vm_mock_payment_json_string(json, "payId", result->payId,
                                     sizeof(result->payId)) ||
        !vm_mock_payment_json_string(json, "orderId", result->orderId,
                                     sizeof(result->orderId)) ||
        !vm_mock_payment_json_string(json, "payUrl", result->payUrl,
                                     sizeof(result->payUrl)) ||
        !vm_mock_payment_json_decimal(json, "reallyPrice",
                                      &result->reallyPriceCents) ||
        !vm_mock_payment_json_int(json, "state", &result->state))
        return false;
    if (vm_mock_payment_json_int(json, "timeOut", &timeout) && timeout > 0 && timeout < 1440)
        result->timeoutMinutes = (u32)timeout;
    else
        result->timeoutMinutes = 5;
    return result->orderId[0] != 0 &&
           vm_mock_payment_qr_payload_is_safe(result->payUrl);
}

static const char *vm_mock_payment_provider_create_error(const char *json,
                                                         int httpStatus,
                                                         const char **reasonOut)
{
    char message[256];
    int code = 0;

    if (reasonOut)
        *reasonOut = "invalid-response";
    memset(message, 0, sizeof(message));
    if (json == NULL ||
        !vm_mock_payment_json_int(json, "code", &code) ||
        !vm_mock_payment_json_string(json, "msg", message, sizeof(message)))
    {
        if (httpStatus < 200 || httpStatus >= 300)
        {
            if (reasonOut)
                *reasonOut = "http-error";
            return "支付平台连接异常，请稍后重试";
        }
        return "支付平台返回了无法识别的数据，请联系管理员";
    }
    if (strcmp(message, "监控端状态异常，请检查") == 0)
    {
        if (reasonOut)
            *reasonOut = "monitor-offline";
        return "支付平台监控端离线，请先启动并检查 V免签监控端";
    }
    if (strcmp(message, "请您先进入后台配置程序") == 0)
    {
        if (reasonOut)
            *reasonOut = "channel-not-configured";
        return "支付平台尚未配置该支付方式；微信支付需在平台后台单独配置 wxpay 收款码";
    }
    if (strcmp(message, "签名错误") == 0 ||
        strcmp(message, "签名校验不通过") == 0)
    {
        if (reasonOut)
            *reasonOut = "signature-rejected";
        return "支付平台签名校验失败，请检查通讯密钥";
    }
    if (strcmp(message, "订单超出负荷，请稍后重试") == 0)
    {
        if (reasonOut)
            *reasonOut = "provider-overloaded";
        return "支付平台当前订单金额已被占用，请稍后重试";
    }
    if (strcmp(message, "商户订单号已存在") == 0)
    {
        if (reasonOut)
            *reasonOut = "duplicate-pay-id";
        return "支付订单号冲突，请重新提交";
    }
    if (reasonOut)
        *reasonOut = code == 1 ? "invalid-success-payload" : "provider-rejected";
    return "支付平台拒绝创建订单，请检查对应支付方式的收款配置";
}

static bool vm_mock_payment_decode_hex_text(const char *value, size_t valueLen,
                                            char *out, size_t outCap)
{
    size_t decoded = 0;

    if (out == NULL || outCap == 0 || value == NULL ||
        !vm_mysql_hex_decode(value, valueLen, out, outCap - 1, &decoded))
        return false;
    out[decoded] = 0;
    return true;
}

static bool vm_mock_payment_order_row_into(vm_mock_payment_order *order,
                                           unsigned int columnCount,
                                           const char *const *values,
                                           const size_t *lengths)
{
    u32 credited = 0;

    if (order == NULL || columnCount != 17)
        return false;
    for (u32 i = 0; i < columnCount; ++i)
    {
        if (values[i] == NULL)
            return false;
    }
    if (!vm_mock_payment_decode_hex_text(values[0], lengths[0], order->payId,
                                         sizeof(order->payId)) ||
        !vm_mock_payment_decode_hex_text(values[1], lengths[1], order->providerOrderId,
                                         sizeof(order->providerOrderId)) ||
        !vm_mock_payment_decode_hex_text(values[2], lengths[2], order->accountId,
                                         sizeof(order->accountId)) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &order->roleId) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &order->payType) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &order->priceCents) ||
        !vm_mock_mysql_parse_u32(values[6], lengths[6], &order->reallyPriceCents) ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &order->wcoinAmount) ||
        !vm_mock_payment_decode_hex_text(values[8], lengths[8], order->requestParam,
                                         sizeof(order->requestParam)) ||
        !vm_mock_payment_decode_hex_text(values[9], lengths[9], order->payUrl,
                                         sizeof(order->payUrl)) ||
        !vm_mock_mysql_parse_u32(values[10], lengths[10], &order->status) ||
        !vm_mock_payment_parse_int(values[11], lengths[11], &order->providerState) ||
        !vm_mock_mysql_parse_u32(values[12], lengths[12], &order->timeoutMinutes) ||
        !vm_mock_mysql_parse_u32(values[13], lengths[13], &credited) || credited > 1 ||
        !vm_mock_mysql_parse_u32(values[14], lengths[14], &order->lastCheckedAt) ||
        !vm_mock_mysql_parse_u32(values[15], lengths[15], &order->createdAt) ||
        !vm_mock_mysql_parse_u32(values[16], lengths[16], &order->creditedAt))
        return false;
    order->credited = credited != 0;
    order->found = true;
    return true;
}

static bool vm_mock_payment_order_row(void *contextValue,
                                      unsigned int columnCount,
                                      const char *const *values,
                                      const size_t *lengths)
{
    vm_mock_payment_order *order = (vm_mock_payment_order *)contextValue;

    if (order == NULL || order->found)
        return true;
    if (!vm_mock_payment_order_row_into(order, columnCount, values, lengths))
        order->invalid = true;
    return true;
}

static void vm_mock_payment_order_select_sql(char *query, size_t queryCap,
                                             const char *suffix)
{
    snprintf(query, queryCap,
             "SELECT HEX(pay_id),IFNULL(HEX(provider_order_id),''),HEX(account_id),"
             "role_id,pay_type,price_cents,really_price_cents,wcoin_amount,HEX(request_param),"
             "HEX(pay_url),status,provider_state,timeout_minutes,credited,"
             "IFNULL(UNIX_TIMESTAMP(last_checked_at),0),UNIX_TIMESTAMP(created_at),"
             "IFNULL(UNIX_TIMESTAMP(credited_at),0) FROM wcoin_recharge_orders %s",
             suffix ? suffix : "");
}

static bool vm_mock_payment_load_order(const char *payId, bool forUpdate,
                                       vm_mock_payment_order *order)
{
    char payHex[127];
    char suffix[320];
    char query[1024];

    if (order == NULL)
        return false;
    memset(order, 0, sizeof(*order));
    if (payId == NULL || payId[0] == 0 || strlen(payId) >= 64 ||
        vm_mysql_hex_encode(payId, strlen(payId), payHex, sizeof(payHex)) == 0)
        return false;
    snprintf(suffix, sizeof(suffix),
             "WHERE pay_id=CAST(X'%s' AS CHAR)%s", payHex,
             forUpdate ? " FOR UPDATE" : "");
    vm_mock_payment_order_select_sql(query, sizeof(query), suffix);
    return vm_mysql_query(query, vm_mock_payment_order_row, order) &&
           order->found && !order->invalid;
}

static bool vm_mock_payment_u32_row_callback(void *contextValue,
                                             unsigned int columnCount,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_mock_payment_u32_row *row = (vm_mock_payment_u32_row *)contextValue;
    if (row == NULL || row->found)
        return true;
    if (columnCount != 1 || values[0] == NULL ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &row->value))
        row->invalid = true;
    else
        row->found = true;
    return true;
}

static bool vm_mock_payment_u64_row_callback(void *contextValue,
                                             unsigned int columnCount,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_mock_payment_u64_row *row = (vm_mock_payment_u64_row *)contextValue;
    if (row == NULL || row->found)
        return true;
    if (columnCount != 1 || values[0] == NULL ||
        !vm_mock_payment_parse_u64(values[0], lengths[0], &row->value))
        row->invalid = true;
    else
        row->found = true;
    return true;
}

static bool vm_mock_payment_role_balance(const char *accountId, u32 roleId,
                                         bool forUpdate, u32 *balanceOut)
{
    char accountHex[127];
    char query[512];
    vm_mock_payment_u32_row row;

    if (balanceOut)
        *balanceOut = 0;
    memset(&row, 0, sizeof(row));
    if (accountId == NULL || accountId[0] == 0 ||
        vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "SELECT wcoin FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u%s", accountHex, roleId, forUpdate ? " FOR UPDATE" : "");
    if (!vm_mysql_query(query, vm_mock_payment_u32_row_callback, &row) ||
        row.invalid || !row.found)
        return false;
    if (balanceOut)
        *balanceOut = row.value;
    return true;
}

static bool vm_mock_payment_role_reserved(const char *accountId, u32 roleId,
                                          uint64_t *reservedOut)
{
    char accountHex[127];
    char query[640];
    vm_mock_payment_u64_row row;

    if (reservedOut)
        *reservedOut = 0;
    memset(&row, 0, sizeof(row));
    if (accountId == NULL ||
        vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "SELECT COALESCE(SUM(wcoin_amount),0) FROM wcoin_recharge_orders "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND status IN (0,1,2)",
             accountHex, roleId);
    if (!vm_mysql_query(query, vm_mock_payment_u64_row_callback, &row) ||
        row.invalid || !row.found)
        return false;
    if (reservedOut)
        *reservedOut = row.value;
    return true;
}

static void vm_mock_payment_generate_token(const char *prefix,
                                           char *out, size_t outCap)
{
    static u32 serial = 0;
    u32 value = (u32)time(NULL) ^ scheduler_get_tick_ms() ^ ++serial ^
                (u32)(uintptr_t)&serial;
    u32 words[3];

    for (u32 i = 0; i < 3; ++i)
    {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        value += 0x9e3779b9u + i * 0x85ebca6bu;
        words[i] = value;
    }
    snprintf(out, outCap, "%s%llu%08x%08x%08x",
             prefix ? prefix : "", (unsigned long long)time(NULL),
             words[0], words[1], words[2]);
}

static bool vm_mock_payment_insert_local_order(const vm_mock_payment_order *order)
{
    char payHex[127];
    char accountHex[127];
    char paramHex[127];
    char query[1400];

    if (order == NULL ||
        vm_mysql_hex_encode(order->payId, strlen(order->payId), payHex,
                            sizeof(payHex)) == 0 ||
        vm_mysql_hex_encode(order->accountId, strlen(order->accountId), accountHex,
                            sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(order->requestParam, strlen(order->requestParam), paramHex,
                            sizeof(paramHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO wcoin_recharge_orders "
             "(pay_id,account_id,role_id,pay_type,price_cents,wcoin_amount,request_param,status) "
             "VALUES(CAST(X'%s' AS CHAR),CAST(X'%s' AS CHAR),%u,%u,%u,%u,CAST(X'%s' AS CHAR),0)",
             payHex, accountHex, order->roleId, order->payType,
             order->priceCents, order->wcoinAmount, paramHex);
    return vm_mysql_exec(query);
}

static void vm_mock_payment_mark_create_failed(const char *payId)
{
    char payHex[127];
    char query[320];
    if (payId == NULL ||
        vm_mysql_hex_encode(payId, strlen(payId), payHex, sizeof(payHex)) == 0)
        return;
    snprintf(query, sizeof(query),
             "UPDATE wcoin_recharge_orders SET status=5 WHERE pay_id=CAST(X'%s' AS CHAR) "
             "AND credited=0", payHex);
    (void)vm_mysql_exec(query);
}

static bool vm_mock_payment_store_created_order(
    const char *payId, const vm_mock_payment_create_result *provider)
{
    char payHex[127];
    char orderHex[127];
    char urlHex[2051];
    char query[3200];

    if (payId == NULL || provider == NULL ||
        vm_mysql_hex_encode(payId, strlen(payId), payHex, sizeof(payHex)) == 0 ||
        vm_mysql_hex_encode(provider->orderId, strlen(provider->orderId), orderHex,
                            sizeof(orderHex)) == 0 ||
        vm_mysql_hex_encode(provider->payUrl, strlen(provider->payUrl), urlHex,
                            sizeof(urlHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "UPDATE wcoin_recharge_orders SET provider_order_id=CAST(X'%s' AS CHAR),"
             "pay_url=CAST(X'%s' AS CHAR),really_price_cents=%u,provider_state=%d,"
             "timeout_minutes=%u,status=IF(credited=1,status,1) "
             "WHERE pay_id=CAST(X'%s' AS CHAR)",
             orderHex, urlHex, provider->reallyPriceCents, provider->state,
             provider->timeoutMinutes, payHex);
    return vm_mysql_exec(query);
}

static bool vm_mock_payment_build_callback_url(const char *base,
                                               const char *path,
                                               char *out, size_t outCap)
{
    size_t baseLength = base ? strlen(base) : 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (baseLength == 0)
        return false;
    if (strncmp(base, "http://", 7) != 0 && strncmp(base, "https://", 8) != 0)
        return false;
    snprintf(out, outCap, "%s%s%s", base,
             base[baseLength - 1] == '/' ? "" : "/",
             path && path[0] == '/' ? path + 1 : path);
    return strlen(out) < outCap - 1;
}

static bool vm_mock_payment_create_order(const char *accountId, u32 roleId,
                                         u32 yuan, u32 payType,
                                         char payIdOut[64],
                                         const char **messageOut)
{
    vm_mock_payment_config config;
    vm_mock_payment_order order;
    vm_mock_payment_create_result provider;
    uint64_t reserved = 0;
    uint64_t wcoin64 = 0;
    u32 balance = 0;
    char priceText[32];
    char signInput[512];
    char sign[33];
    char form[4096];
    char notifyUrl[512];
    char returnUrl[512];
    char notifyEncoded[1536];
    char returnEncoded[1536];
    char response[VM_MOCK_PAYMENT_HTTP_RESPONSE_MAX + 1];
    const char *failureReason = "local-validation";
    const char *providerMessage = NULL;
    bool httpOk = false;
    bool providerOk = false;
    int httpStatus = 0;

    if (payIdOut)
        payIdOut[0] = 0;
    if (messageOut)
        *messageOut = "充值服务暂不可用";
    memset(&config, 0, sizeof(config));
    memset(&order, 0, sizeof(order));
    memset(&provider, 0, sizeof(provider));
    memset(notifyUrl, 0, sizeof(notifyUrl));
    memset(returnUrl, 0, sizeof(returnUrl));
    memset(response, 0, sizeof(response));
    printf("[info][payment] order_create_request account=%s role=%u type=%u yuan=%u\n",
           accountId ? accountId : "-", roleId, payType, yuan);
    if (!vm_mock_payment_load_config(&config))
        goto failed;
    if (payType != 1 && payType != 2)
    {
        if (messageOut)
            *messageOut = "请选择微信或支付宝";
        goto failed;
    }
    if (yuan < config.minimumYuan || yuan > config.maximumYuan)
    {
        if (messageOut)
            *messageOut = "充值金额超出允许范围";
        goto failed;
    }
    wcoin64 = (uint64_t)yuan * config.wcoinPerYuan;
    if (wcoin64 == 0 || wcoin64 > UINT32_MAX || yuan > UINT32_MAX / 100u ||
        !vm_mock_payment_role_balance(accountId, roleId, false, &balance) ||
        !vm_mock_payment_role_reserved(accountId, roleId, &reserved) ||
        (uint64_t)balance + reserved + wcoin64 > UINT32_MAX)
    {
        if (messageOut)
            *messageOut = "角色不存在或 W 币余额空间不足";
        goto failed;
    }
    snprintf(order.accountId, sizeof(order.accountId), "%s", accountId);
    order.roleId = roleId;
    order.payType = payType;
    order.priceCents = yuan * 100u;
    order.wcoinAmount = (u32)wcoin64;
    vm_mock_payment_generate_token("JH", order.payId, sizeof(order.payId));
    vm_mock_payment_generate_token("P", order.requestParam, sizeof(order.requestParam));
    if (!vm_mock_payment_insert_local_order(&order))
    {
        if (messageOut)
            *messageOut = "订单保存失败，请稍后重试";
        goto failed;
    }
    snprintf(priceText, sizeof(priceText), "%u.%02u",
             order.priceCents / 100u, order.priceCents % 100u);
    snprintf(signInput, sizeof(signInput), "%s%s%u%s%s",
             order.payId, order.requestParam, payType, priceText,
             config.secretKey);
    vm_md5_hex(signInput, strlen(signInput), sign);
    snprintf(form, sizeof(form),
             "payId=%s&type=%u&price=%s&sign=%s&param=%s&isHtml=0",
             order.payId, payType, priceText, sign, order.requestParam);
    if (vm_mock_payment_build_callback_url(config.callbackBaseUrl,
                                           "/payment/cbhub/notify",
                                           notifyUrl, sizeof(notifyUrl)) &&
        vm_mock_payment_build_callback_url(config.callbackBaseUrl,
                                           "/payment/cbhub/return",
                                           returnUrl, sizeof(returnUrl)))
    {
        vm_mock_admin_url_encode(notifyUrl, notifyEncoded, sizeof(notifyEncoded));
        vm_mock_admin_url_encode(returnUrl, returnEncoded, sizeof(returnEncoded));
        snprintf(form + strlen(form), sizeof(form) - strlen(form),
                 "&notifyUrl=%s&returnUrl=%s", notifyEncoded, returnEncoded);
    }
    httpOk = vm_mock_payment_http_post_locked(config.apiBaseUrl, "/createOrder",
                                              form, response, sizeof(response),
                                              &httpStatus);
    providerOk = httpOk &&
                 vm_mock_payment_parse_create_result(response, &provider) &&
                 strcmp(provider.payId, order.payId) == 0;
    if (!providerOk || !vm_mock_payment_store_created_order(order.payId, &provider))
    {
        if (!providerOk)
            providerMessage = vm_mock_payment_provider_create_error(
                response, httpStatus, &failureReason);
        else
        {
            failureReason = "store-provider-order";
            providerMessage = "支付订单保存失败，请稍后重试";
        }
        vm_mock_payment_mark_create_failed(order.payId);
        if (messageOut)
            *messageOut = providerMessage;
        printf("[error][payment] order_create_failed pay_id=%s account=%s role=%u type=%u http=%d reason=%s\n",
               order.payId, accountId, roleId, payType, httpStatus,
               failureReason);
        goto failed;
    }
    if (payIdOut)
        snprintf(payIdOut, 64, "%s", order.payId);
    if (messageOut)
        *messageOut = "ok";
    printf("[info][payment] order_created pay_id=%s account=%s role=%u type=%u cents=%u wcoin=%u\n",
           order.payId, accountId, roleId, payType, order.priceCents,
           order.wcoinAmount);
    memset(config.secretKey, 0, sizeof(config.secretKey));
    memset(signInput, 0, sizeof(signInput));
    return true;

failed:
    memset(config.secretKey, 0, sizeof(config.secretKey));
    memset(signInput, 0, sizeof(signInput));
    return false;
}

static bool vm_mock_payment_parse_callback(const char *query,
                                           vm_mock_payment_callback *callback)
{
    if (callback == NULL)
        return false;
    memset(callback, 0, sizeof(*callback));
    if (!vm_mock_admin_form_value(query, "payId", callback->payId,
                                  sizeof(callback->payId)) ||
        !vm_mock_admin_form_value(query, "param", callback->param,
                                  sizeof(callback->param)) ||
        !vm_mock_admin_form_value(query, "type", callback->typeText,
                                  sizeof(callback->typeText)) ||
        !vm_mock_admin_form_value(query, "price", callback->priceText,
                                  sizeof(callback->priceText)) ||
        !vm_mock_admin_form_value(query, "reallyPrice", callback->reallyPriceText,
                                  sizeof(callback->reallyPriceText)) ||
        !vm_mock_admin_form_value(query, "sign", callback->sign,
                                  sizeof(callback->sign)) ||
        !vm_net_mock_parse_u32_strict(callback->typeText, &callback->payType) ||
        !vm_mock_payment_decimal_to_cents(callback->priceText,
                                          &callback->priceCents) ||
        !vm_mock_payment_decimal_to_cents(callback->reallyPriceText,
                                          &callback->reallyPriceCents) ||
        callback->reallyPriceCents == 0)
        return false;
    return callback->payType == 1 || callback->payType == 2;
}

static void vm_mock_payment_sync_cached_wcoin(const char *accountId,
                                              u32 roleId, u32 balance)
{
    vm_mock_service_account_state *state = g_vm_mock_service_accounts;

    while (state != NULL)
    {
        if (strcmp(state->accountId, accountId) == 0)
        {
            if (state->roleDbValid)
            {
                for (u32 i = 0; i < state->roleDb.roleCount; ++i)
                {
                    if (state->roleDb.roles[i].roleId == roleId)
                        state->roleDb.roles[i].wcoin = balance;
                }
            }
            break;
        }
        state = state->next;
    }
    if (g_vm_mock_service_active_account_id != NULL &&
        strcmp(g_vm_mock_service_active_account_id, accountId) == 0)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
                g_vm_net_mock_role_db.roles[i].wcoin = balance;
        }
    }
}

static vm_mock_payment_settle_result vm_mock_payment_settle_verified(
    const vm_mock_payment_callback *callback)
{
    vm_mock_payment_order order;
    u32 balance = 0;
    u32 after = 0;
    char payHex[127];
    char query[768];
    bool transaction = false;

    if (callback == NULL ||
        vm_mysql_hex_encode(callback->payId, strlen(callback->payId), payHex,
                            sizeof(payHex)) == 0 ||
        !vm_mysql_exec("START TRANSACTION"))
        return VM_MOCK_PAYMENT_SETTLE_INVALID;
    transaction = true;
    if (!vm_mock_payment_load_order(callback->payId, true, &order) ||
        strcmp(order.requestParam, callback->param) != 0 ||
        order.payType != callback->payType ||
        order.priceCents != callback->priceCents)
        goto invalid;
    if (order.credited && order.status == VM_MOCK_PAYMENT_STATUS_CREDITED)
    {
        if (!vm_mysql_exec("COMMIT"))
            goto invalid_no_rollback;
        return VM_MOCK_PAYMENT_SETTLE_CREDITED;
    }
    snprintf(query, sizeof(query),
             "UPDATE wcoin_recharge_orders SET status=2,really_price_cents=%u,"
             "paid_at=COALESCE(paid_at,CURRENT_TIMESTAMP) "
             "WHERE pay_id=CAST(X'%s' AS CHAR) AND credited=0",
             callback->reallyPriceCents, payHex);
    if (!vm_mysql_exec(query))
        goto invalid;
    if (!vm_mock_payment_role_balance(order.accountId, order.roleId, true, &balance) ||
        UINT32_MAX - balance < order.wcoinAmount)
    {
        if (!vm_mysql_exec("COMMIT"))
            goto invalid_no_rollback;
        printf("[error][payment] credit_pending pay_id=%s account=%s role=%u reason=balance-cap-or-role\n",
               order.payId, order.accountId, order.roleId);
        return VM_MOCK_PAYMENT_SETTLE_PENDING;
    }
    after = balance + order.wcoinAmount;
    {
        char accountHex[127];
        if (vm_mysql_hex_encode(order.accountId, strlen(order.accountId), accountHex,
                                sizeof(accountHex)) == 0)
            goto invalid;
        snprintf(query, sizeof(query),
                 "UPDATE account_roles SET wcoin=%u WHERE account_id=CAST(X'%s' AS CHAR) "
                 "AND role_id=%u", after, accountHex, order.roleId);
        if (!vm_mysql_exec(query))
            goto invalid;
    }
    snprintf(query, sizeof(query),
             "UPDATE wcoin_recharge_orders SET status=3,credited=1,"
             "credited_at=CURRENT_TIMESTAMP WHERE pay_id=CAST(X'%s' AS CHAR) AND credited=0",
             payHex);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto invalid_no_rollback;
    transaction = false;
    vm_mock_payment_sync_cached_wcoin(order.accountId, order.roleId, after);
    printf("[info][payment] credited pay_id=%s account=%s role=%u add=%u before=%u after=%u\n",
           order.payId, order.accountId, order.roleId, order.wcoinAmount,
           balance, after);
    return VM_MOCK_PAYMENT_SETTLE_CREDITED;

invalid:
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    return VM_MOCK_PAYMENT_SETTLE_INVALID;
invalid_no_rollback:
    (void)vm_mysql_exec("ROLLBACK");
    return VM_MOCK_PAYMENT_SETTLE_INVALID;
}

static vm_mock_payment_settle_result vm_mock_payment_process_callback_query(
    const char *query, const char *source, char payIdOut[64])
{
    vm_mock_payment_config config;
    vm_mock_payment_callback callback;
    char signInput[512];
    char expected[33];
    vm_mock_payment_settle_result result = VM_MOCK_PAYMENT_SETTLE_INVALID;

    if (payIdOut)
        payIdOut[0] = 0;
    memset(&config, 0, sizeof(config));
    memset(&callback, 0, sizeof(callback));
    memset(signInput, 0, sizeof(signInput));
    if (!vm_mock_payment_load_config(&config) ||
        !vm_mock_payment_parse_callback(query, &callback))
        goto done;
    snprintf(signInput, sizeof(signInput), "%s%s%s%s%s%s",
             callback.payId, callback.param, callback.typeText,
             callback.priceText, callback.reallyPriceText, config.secretKey);
    vm_md5_hex(signInput, strlen(signInput), expected);
    if (!vm_mock_payment_constant_sign_equal(expected, callback.sign))
        goto done;
    result = vm_mock_payment_settle_verified(&callback);
    if (result != VM_MOCK_PAYMENT_SETTLE_INVALID && payIdOut)
        snprintf(payIdOut, 64, "%s", callback.payId);

done:
    printf("[%s][payment] callback source=%s pay_id=%s result=%u\n",
           result == VM_MOCK_PAYMENT_SETTLE_INVALID ? "warn" : "info",
           source ? source : "-", callback.payId[0] ? callback.payId : "-",
           (u32)result);
    memset(config.secretKey, 0, sizeof(config.secretKey));
    memset(signInput, 0, sizeof(signInput));
    return result;
}

static bool vm_mock_payment_refresh_order(const char *accountId,
                                          const char *payId)
{
    vm_mock_payment_config config;
    vm_mock_payment_order order;
    char orderEncoded[256];
    char form[320];
    char response[VM_MOCK_PAYMENT_HTTP_RESPONSE_MAX + 1];
    char callbackUrl[4096];
    char callbackQuery[4096];
    char *question = NULL;
    int code = 0;
    int httpStatus = 0;
    u32 now = (u32)time(NULL);
    char payHex[127];
    char update[384];

    memset(&config, 0, sizeof(config));
    if (!vm_mock_payment_load_order(payId, false, &order) ||
        strcmp(order.accountId, accountId) != 0 ||
        order.status == VM_MOCK_PAYMENT_STATUS_CREDITED ||
        order.status == VM_MOCK_PAYMENT_STATUS_FAILED ||
        order.status == VM_MOCK_PAYMENT_STATUS_EXPIRED ||
        order.providerOrderId[0] == 0)
        return false;
    if (order.lastCheckedAt != 0 && now >= order.lastCheckedAt &&
        now - order.lastCheckedAt < 2)
        return true;
    if (order.createdAt != 0 && order.timeoutMinutes != 0 &&
        now >= order.createdAt + order.timeoutMinutes * 60u)
    {
        if (vm_mysql_hex_encode(payId, strlen(payId), payHex, sizeof(payHex)) != 0)
        {
            snprintf(update, sizeof(update),
                     "UPDATE wcoin_recharge_orders SET status=4,last_checked_at=CURRENT_TIMESTAMP "
                     "WHERE pay_id=CAST(X'%s' AS CHAR) AND status IN (0,1)", payHex);
            (void)vm_mysql_exec(update);
        }
        return true;
    }
    if (!vm_mock_payment_load_config(&config))
        return false;
    vm_mock_admin_url_encode(order.providerOrderId, orderEncoded,
                             sizeof(orderEncoded));
    snprintf(form, sizeof(form), "orderId=%s", orderEncoded);
    if (vm_mysql_hex_encode(payId, strlen(payId), payHex, sizeof(payHex)) != 0)
    {
        snprintf(update, sizeof(update),
                 "UPDATE wcoin_recharge_orders SET last_checked_at=CURRENT_TIMESTAMP "
                 "WHERE pay_id=CAST(X'%s' AS CHAR)", payHex);
        (void)vm_mysql_exec(update);
    }
    if (!vm_mock_payment_http_post_locked(config.apiBaseUrl, "/checkOrder", form,
                                          response, sizeof(response), &httpStatus) ||
        !vm_mock_payment_json_int(response, "code", &code) || code != 1 ||
        !vm_mock_payment_json_string(response, "data", callbackUrl,
                                     sizeof(callbackUrl)))
    {
        memset(config.secretKey, 0, sizeof(config.secretKey));
        return true;
    }
    question = strchr(callbackUrl, '?');
    if (question != NULL && strlen(question + 1) < sizeof(callbackQuery))
    {
        snprintf(callbackQuery, sizeof(callbackQuery), "%s", question + 1);
        if (strchr(callbackQuery, '#') != NULL)
            *strchr(callbackQuery, '#') = 0;
        (void)vm_mock_payment_process_callback_query(callbackQuery,
                                                     "check-order-signed-data",
                                                     NULL);
    }
    memset(config.secretKey, 0, sizeof(config.secretKey));
    return true;
}

static const char *vm_mock_payment_status_label(const vm_mock_payment_order *order)
{
    if (order == NULL)
        return "未知";
    switch (order->status)
    {
    case VM_MOCK_PAYMENT_STATUS_CREATING: return "创建中";
    case VM_MOCK_PAYMENT_STATUS_WAITING: return "等待支付";
    case VM_MOCK_PAYMENT_STATUS_PAID_PENDING: return "已支付，等待入账";
    case VM_MOCK_PAYMENT_STATUS_CREDITED: return "充值成功";
    case VM_MOCK_PAYMENT_STATUS_EXPIRED: return "订单已过期";
    case VM_MOCK_PAYMENT_STATUS_FAILED: return "创建失败";
    default: return "状态异常";
    }
}

static void vm_mock_payment_render_order_page(char *response, size_t responseCap,
                                              const vm_mock_payment_order *order)
{
    vm_mock_admin_text page;
    char refreshUrl[256];
    u32 actualPriceCents = order && order->reallyPriceCents != 0 ?
        order->reallyPriceCents : (order ? order->priceCents : 0);
    u32 remainingSeconds = 0;
    u32 now = (u32)time(NULL);

    if (order != NULL && order->createdAt != 0 && order->timeoutMinutes != 0)
    {
        uint64_t expiresAt = (uint64_t)order->createdAt +
                             (uint64_t)order->timeoutMinutes * 60u;
        if (expiresAt > now)
        {
            uint64_t remaining = expiresAt - now;
            remainingSeconds = remaining > UINT32_MAX ? UINT32_MAX : (u32)remaining;
        }
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    snprintf(refreshUrl, sizeof(refreshUrl), "/user/recharge/order?id=%s",
             order ? order->payId : "");
    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "%s<title>W币充值订单</title><script defer src=\"/payment/qrcode.js\"></script><style>"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:radial-gradient(circle at 20%% 10%%,#dbeafe,transparent 32%%),#f5f7fa;color:#17202a;font:14px/1.65 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{width:min(700px,calc(100%% - 28px));padding:24px 0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:18px;padding:28px;box-shadow:0 20px 55px #10182818}.icon{display:grid;place-items:center;width:54px;height:54px;border-radius:16px;background:%s;color:#fff;font-size:25px;font-weight:800;margin-bottom:15px}h1{font-size:25px;margin:0 0 5px}.sub{color:#667085;margin:0 0 20px}.amount{display:flex;align-items:end;justify-content:space-between;gap:16px;padding:17px;border-radius:12px;background:#f8fafc;margin:16px 0}.amount strong{display:block;font-size:27px}.amount span{color:#667085}.details{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:17px 0}.details div{border:1px solid #eaecf0;border-radius:10px;padding:11px}.details span{display:block;color:#667085;font-size:12px}.payment-box{display:grid;grid-template-columns:280px 1fr;gap:22px;align-items:center;margin-top:18px;padding:20px;border:2px solid #f79009;border-radius:14px;background:#fffcf5}.qr-frame{display:grid;justify-items:center;gap:7px;padding:15px;border:1px solid #fedf89;border-radius:12px;background:#fff}.qr-frame small{color:#667085}.payment-copy .label{color:#7a2e0e;font-weight:700}.actual-price{color:#d92d20;font-size:38px;line-height:1.2;font-weight:850;letter-spacing:.3px;margin:3px 0 10px}.countdown{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 0 13px;padding:9px 11px;border-radius:9px;background:#fff7ed;border:1px solid #fed7aa;color:#9a3412}.countdown span{font-size:13px}.countdown strong{font:800 23px/1 ui-monospace,SFMono-Regular,Consolas,monospace;letter-spacing:1px;color:#c2410c}.countdown strong.expired{color:#b42318}.pay-warning{padding:13px 14px;border-radius:10px;background:#fee4e2;color:#912018;border:1px solid #fda29b}.pay-warning strong{display:block;font-size:17px;margin-bottom:3px}.pay-warning p{margin:0}.qr-error{width:248px;min-height:120px;display:grid;place-items:center;text-align:center;color:#b42318}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.button{display:inline-flex;align-items:center;justify-content:center;text-decoration:none;border-radius:9px;padding:10px 15px;font-weight:700;background:#175cd3;color:#fff}.button.secondary{background:#fff;color:#475467;border:1px solid #d0d5dd}.pending{color:#b54708}.success{color:#027a48}.failed{color:#b42318}.hint{padding:11px 13px;border-radius:9px;background:#fffaeb;color:#7a2e0e;margin-top:14px}@media(max-width:650px){.card{padding:21px}.details{grid-template-columns:1fr}.amount{align-items:start;flex-direction:column}.payment-box{grid-template-columns:1fr}.qr-frame{width:100%%}.payment-copy{text-align:center}.pay-warning{text-align:left}}"
        "</style></head><body><main class=\"wrap\"><section class=\"card\"><div class=\"icon\">%s</div><h1 class=\"%s\">%s</h1>",
        order && order->status == VM_MOCK_PAYMENT_STATUS_CREDITED ?
            "<meta http-equiv=\"refresh\" content=\"3;url=/\">" :
        order && (order->status == VM_MOCK_PAYMENT_STATUS_WAITING ||
                  order->status == VM_MOCK_PAYMENT_STATUS_CREATING ||
                  order->status == VM_MOCK_PAYMENT_STATUS_PAID_PENDING) ?
            "<meta http-equiv=\"refresh\" content=\"3\">" : "",
        order && order->status == VM_MOCK_PAYMENT_STATUS_CREDITED ? "#12b76a" :
        order && order->status >= VM_MOCK_PAYMENT_STATUS_EXPIRED ? "#d92d20" : "#f79009",
        order && order->status == VM_MOCK_PAYMENT_STATUS_CREDITED ? "✓" : "W",
        order && order->status == VM_MOCK_PAYMENT_STATUS_CREDITED ? "success" :
        order && order->status >= VM_MOCK_PAYMENT_STATUS_EXPIRED ? "failed" : "pending",
        vm_mock_payment_status_label(order));
    if (order == NULL)
    {
        vm_mock_admin_text_appendf(&page,
            "<p class=\"sub\">订单不存在或无权查看。</p><div class=\"actions\"><a class=\"button secondary\" href=\"/\">返回用户中心</a></div>");
    }
    else
    {
        vm_mock_admin_text_appendf(&page,
            "<p class=\"sub\">%s</p><div class=\"amount\"><div><span>平台实际应付金额</span><strong>￥%u.%02u</strong></div><div><span>到账 W 币</span><strong>%u</strong></div></div>"
            "<div class=\"details\"><div><span>角色 ID</span>%u</div><div><span>支付方式</span>%s</div><div><span>商户订单号</span>",
            order->status == VM_MOCK_PAYMENT_STATUS_CREDITED ?
                "W 币已经安全入账，3 秒后返回用户中心。" :
            order->status == VM_MOCK_PAYMENT_STATUS_PAID_PENDING ?
                "支付已确认，正在等待 W 币入账；页面会自动检查。" :
            order->status == VM_MOCK_PAYMENT_STATUS_WAITING ?
                "完成支付后请保留本页，系统每 3 秒检查一次。" :
                "该订单已停止检查，你可以返回用户中心重新下单。",
            actualPriceCents / 100u, actualPriceCents % 100u,
            order->wcoinAmount, order->roleId,
            order->payType == 1 ? "微信支付" : "支付宝支付");
        vm_mock_admin_text_append_html(&page, order->payId);
        vm_mock_admin_text_appendf(&page, "</div><div><span>订单状态</span>%s</div></div>",
                                   vm_mock_payment_status_label(order));
        if (order->status == VM_MOCK_PAYMENT_STATUS_WAITING &&
            vm_mock_payment_qr_payload_is_safe(order->payUrl))
        {
            vm_mock_admin_text_appendf(&page,
                "<div class=\"payment-box\"><div class=\"qr-frame\"><div id=\"payment-qr\" data-payment-url=\"");
            vm_mock_admin_text_append_html(&page, order->payUrl);
            vm_mock_admin_text_appendf(&page,
                "\"></div><small>请使用对应支付应用扫码</small></div>"
                "<div class=\"payment-copy\"><div class=\"label\">本单唯一有效支付金额</div>"
                "<div class=\"actual-price\">￥%u.%02u</div>"
                "<div class=\"countdown\"><span>剩余支付时间（有效期 %u 分钟）</span>"
                "<strong id=\"payment-countdown\" data-remaining-seconds=\"%u\">%02u:%02u</strong></div>"
                "<div class=\"pay-warning\"><strong>付款金额必须与上方金额完全一致</strong>"
                "<p>请在支付时准确输入 ￥%u.%02u。多付或少付均无法自动匹配订单，W币将不会自动到账。</p></div></div></div>",
                actualPriceCents / 100u, actualPriceCents % 100u,
                order->timeoutMinutes, remainingSeconds,
                remainingSeconds / 60u, remainingSeconds % 60u,
                actualPriceCents / 100u, actualPriceCents % 100u);
            vm_mock_admin_text_appendf(&page, "<div class=\"actions\">");
            if (vm_mock_payment_click_url_is_safe(order->payUrl))
            {
                vm_mock_admin_text_appendf(&page,
                    "<a class=\"button\" target=\"_blank\" rel=\"noopener noreferrer\" href=\"");
                vm_mock_admin_text_append_html(&page, order->payUrl);
                vm_mock_admin_text_appendf(&page, "\">打开支付链接</a>");
            }
            vm_mock_admin_text_appendf(&page,
                "<a class=\"button secondary\" href=\"/\">返回用户中心</a></div>");
        }
        else
        {
            vm_mock_admin_text_appendf(&page,
                "<div class=\"actions\"><a class=\"button secondary\" href=\"/\">返回用户中心</a></div>");
        }
    }
    vm_mock_admin_text_appendf(&page, "</section></main></body></html>");
}

static bool vm_mock_payment_recent_row(void *contextValue,
                                       unsigned int columnCount,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_mock_payment_recent_rows *rows = (vm_mock_payment_recent_rows *)contextValue;
    if (rows == NULL || rows->count >= VM_MOCK_PAYMENT_RECENT_MAX)
        return true;
    if (!vm_mock_payment_order_row_into(&rows->rows[rows->count], columnCount,
                                        values, lengths))
        rows->invalid = true;
    else
        ++rows->count;
    return true;
}

static void vm_mock_payment_render_dashboard(vm_mock_admin_text *page,
                                             const char *accountId)
{
    vm_mock_payment_config config;
    vm_mock_payment_recent_rows recent;
    char accountHex[127];
    char suffix[384];
    char query[1200];
    bool available = false;

    memset(&config, 0, sizeof(config));
    memset(&recent, 0, sizeof(recent));
    available = vm_mock_payment_load_config(&config);
    vm_mock_admin_text_appendf(page,
        "</section><section class=\"card recharge\"><div><h2>W币充值</h2><p class=\"sub\">充值比例：1 元 = %u W币。订单支付确认后自动入账。</p></div>",
        config.wcoinPerYuan ? config.wcoinPerYuan : 1000u);
    if (!available)
    {
        vm_mock_admin_text_appendf(page,
            "<div class=\"recharge-unavailable\">充值服务尚未配置，请联系管理员。</div></section>");
        memset(config.secretKey, 0, sizeof(config.secretKey));
        return;
    }
    if (g_vm_net_mock_role_db.roleCount == 0)
    {
        vm_mock_admin_text_appendf(page,
            "<div class=\"recharge-unavailable\">请先在游戏中创建角色。</div></section>");
        memset(config.secretKey, 0, sizeof(config.secretKey));
        return;
    }
    vm_mock_admin_text_appendf(page,
        "<form class=\"recharge-form\" method=\"post\" action=\"/user/recharge/create\"><label>充值角色<select name=\"role_id\" required>");
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        char roleNameUtf8[128];
        memset(roleNameUtf8, 0, sizeof(roleNameUtf8));
        vm_net_mock_gbk_label_to_utf8(role->name[0] ? role->name : "-",
                                      roleNameUtf8, sizeof(roleNameUtf8));
        vm_mock_admin_text_appendf(page, "<option value=\"%u\">", role->roleId);
        vm_mock_admin_text_append_html(page, roleNameUtf8);
        vm_mock_admin_text_appendf(page, "（ID %u，余额 %u W币）</option>",
                                   role->roleId, role->wcoin);
    }
    vm_mock_admin_text_appendf(page,
        "</select></label><label>充值金额（元）<input type=\"number\" name=\"yuan\" min=\"%u\" max=\"%u\" step=\"1\" value=\"10\" required></label>"
        "<label>支付方式<select name=\"pay_type\"><option value=\"2\">支付宝</option><option value=\"1\">微信支付</option></select></label>"
        "<button type=\"submit\">生成支付订单</button></form>",
        config.minimumYuan, config.maximumYuan);

    if (accountId != NULL &&
        vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) != 0)
    {
        snprintf(suffix, sizeof(suffix),
                 "WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY created_at DESC LIMIT %u",
                 accountHex, (u32)VM_MOCK_PAYMENT_RECENT_MAX);
        vm_mock_payment_order_select_sql(query, sizeof(query), suffix);
        if (vm_mysql_query(query, vm_mock_payment_recent_row, &recent) &&
            !recent.invalid && recent.count != 0)
        {
            vm_mock_admin_text_appendf(page,
                "<div class=\"recharge-history\"><h3>最近订单</h3>");
            for (u32 i = 0; i < recent.count; ++i)
            {
                const vm_mock_payment_order *order = &recent.rows[i];
                vm_mock_admin_text_appendf(page,
                    "<a href=\"/user/recharge/order?id=%s\"><span>￥%u.%02u · %u W币</span><strong>%s</strong></a>",
                    order->payId, order->priceCents / 100u,
                    order->priceCents % 100u, order->wcoinAmount,
                    vm_mock_payment_status_label(order));
            }
            vm_mock_admin_text_appendf(page, "</div>");
        }
    }
    vm_mock_admin_text_appendf(page,
        "<p class=\"recharge-note\"></p></section>");
    memset(config.secretKey, 0, sizeof(config.secretKey));
}
