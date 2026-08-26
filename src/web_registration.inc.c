/*
 * Registration policy, SMTP delivery and web-email verification.
 *
 * This fragment is included by web_admin_server.c after the shared MySQL,
 * socket and form helpers.  Registration data is intentionally persisted in
 * MySQL: a service restart must not re-enable guest allocation, lose a code,
 * or detach an already verified email address from its account.
 */

enum
{
    VM_MOCK_REGISTRATION_EMAIL_MAX = 255,
    VM_MOCK_REGISTRATION_SMTP_HOST_MAX = 256,
    VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX = 256,
    VM_MOCK_REGISTRATION_EMAIL_SUBJECT_MAX = 256,
    VM_MOCK_REGISTRATION_EMAIL_BODY_MAX = 4097,
    VM_MOCK_REGISTRATION_SMTP_MESSAGE_MAX = 16384,
    VM_MOCK_REGISTRATION_CODE_LEN = 6,
    VM_MOCK_REGISTRATION_CODE_EXPIRE_SECONDS = 10 * 60,
    VM_MOCK_REGISTRATION_CODE_RESEND_SECONDS = 60,
    VM_MOCK_REGISTRATION_CODE_MAX_ATTEMPTS = 5,
    VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN = 32,
    VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN = 5,
    VM_MOCK_REGISTRATION_CAPTCHA_EXPIRE_SECONDS = 5 * 60,
    VM_MOCK_REGISTRATION_CAPTCHA_MAX_ATTEMPTS = 5,
    VM_MOCK_REGISTRATION_SMTP_REPLY_MAX = 2048,
    VM_MOCK_REGISTRATION_SMTP_TIMEOUT_MS = 5000
};

/* These columns are MySQL TIMESTAMP values.  Use the connection session's
 * current time for both storage and comparison; UTC wall-clock literals would
 * otherwise be reinterpreted in a non-UTC session time zone. */
#define VM_MOCK_REGISTRATION_SQL_TIMESTAMP_NOW "CURRENT_TIMESTAMP()"
#define VM_MOCK_REGISTRATION_CODE_PLACEHOLDER "{{code}}"

static const char g_vm_mock_registration_default_email_subject[] =
    "江湖OL 注册验证码";
static const char g_vm_mock_registration_default_email_body[] =
    "您正在注册江湖OL账号。\n\n验证码：{{code}}\n\n验证码 10 分钟内有效，请勿泄露给他人。\n";

typedef struct
{
    bool found;
    bool invalid;
    bool allowGameAutoAccount;
} vm_mock_registration_settings;

typedef struct
{
    bool found;
    bool invalid;
    bool enabled;
    u16 port;
    char host[VM_MOCK_REGISTRATION_SMTP_HOST_MAX];
    char username[VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX];
    char password[VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX];
    char senderEmail[VM_MOCK_REGISTRATION_EMAIL_MAX];
} vm_mock_registration_smtp_config;

typedef struct
{
    bool found;
    bool invalid;
    char subject[VM_MOCK_REGISTRATION_EMAIL_SUBJECT_MAX];
    char body[VM_MOCK_REGISTRATION_EMAIL_BODY_MAX];
} vm_mock_registration_email_template;

typedef struct
{
    bool found;
    bool invalid;
    u32 expiresAt;
    u32 lastSentAt;
    u32 attemptCount;
    char codeDigest[33];
} vm_mock_registration_verification;

typedef struct
{
    bool found;
    bool invalid;
    u32 expiresAt;
    u32 attemptCount;
    char email[VM_MOCK_REGISTRATION_EMAIL_MAX];
    char codeDigest[33];
} vm_mock_registration_captcha;

static bool vm_mock_registration_parse_u32(const char *value, size_t length,
                                           u32 *out)
{
    char text[32];

    if (out != NULL)
        *out = 0;
    if (value == NULL || length == 0 || length >= sizeof(text))
        return false;
    memcpy(text, value, length);
    text[length] = 0;
    return vm_net_mock_parse_u32_strict(text, out);
}

static bool vm_mock_registration_copy_text(char *out, size_t outCap,
                                           const char *value, size_t length)
{
    if (out == NULL || outCap == 0 || value == NULL || length >= outCap)
        return false;
    memcpy(out, value, length);
    out[length] = 0;
    return true;
}

static bool vm_mock_registration_email_template_ensure_default(void)
{
    char subjectHex[VM_MOCK_REGISTRATION_EMAIL_SUBJECT_MAX * 2 + 1];
    char bodyHex[VM_MOCK_REGISTRATION_EMAIL_BODY_MAX * 2 + 1];
    char query[VM_MOCK_REGISTRATION_EMAIL_BODY_MAX * 2 + 256];

    if (vm_mysql_hex_encode(g_vm_mock_registration_default_email_subject,
                            strlen(g_vm_mock_registration_default_email_subject),
                            subjectHex, sizeof(subjectHex)) == 0 ||
        vm_mysql_hex_encode(g_vm_mock_registration_default_email_body,
                            strlen(g_vm_mock_registration_default_email_body),
                            bodyHex, sizeof(bodyHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO server_registration_email_templates"
             "(config_id,subject,body) VALUES(1,X'%s',X'%s')",
             subjectHex, bodyHex);
    return vm_mysql_exec(query);
}

static bool vm_mock_registration_ensure_tables(void)
{
    static bool initialized = false;

    if (initialized)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_registration_config ("
            "config_id TINYINT UNSIGNED NOT NULL,"
            "allow_game_auto_account TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(config_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "INSERT IGNORE INTO server_registration_config(config_id,allow_game_auto_account) "
            "VALUES(1,1)") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_smtp_config ("
            "config_id TINYINT UNSIGNED NOT NULL,"
            "host VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "port SMALLINT UNSIGNED NOT NULL DEFAULT 25,"
            "username VARBINARY(255) NOT NULL,"
            "password_value VARBINARY(255) NOT NULL,"
            "sender_email VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(config_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "INSERT IGNORE INTO server_smtp_config(config_id,host,port,username,password_value,sender_email,enabled) "
            "VALUES(1,'',25,'','','',0)") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_registration_email_templates ("
            "config_id TINYINT UNSIGNED NOT NULL,"
            "subject VARBINARY(255) NOT NULL,"
            "body VARBINARY(4096) NOT NULL,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(config_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_email_bindings ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "email VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "verified_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id),UNIQUE KEY uk_account_email_bindings_email(email),"
            "CONSTRAINT fk_account_email_bindings_account FOREIGN KEY(account_id) "
            "REFERENCES accounts(account_id) ON DELETE CASCADE) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS web_registration_email_verifications ("
            "email VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "code_digest CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "attempt_count TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "expires_at TIMESTAMP NOT NULL,"
            "last_sent_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(email),KEY idx_web_registration_verifications_expires(expires_at)) "
            "ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS web_registration_image_captchas ("
            "captcha_token CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "email VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "code_digest CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "attempt_count TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "expires_at TIMESTAMP NOT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(captcha_token),"
            "KEY idx_web_registration_image_captchas_expires(expires_at)) "
            "ENGINE=InnoDB"))
    {
        return false;
    }
    if (!vm_mock_registration_email_template_ensure_default())
        return false;
    initialized = true;
    return true;
}

static bool vm_mock_registration_settings_row(void *contextValue,
                                              unsigned int columnCount,
                                              const char *const *values,
                                              const size_t *lengths)
{
    vm_mock_registration_settings *settings =
        (vm_mock_registration_settings *)contextValue;
    u32 allowed = 0;

    if (settings == NULL || settings->found || columnCount != 1 ||
        !vm_mock_registration_parse_u32(values[0], lengths[0], &allowed) ||
        allowed > 1)
    {
        if (settings != NULL)
            settings->invalid = true;
        return true;
    }
    settings->allowGameAutoAccount = allowed != 0;
    settings->found = true;
    return true;
}

static bool vm_mock_registration_load_settings(
    vm_mock_registration_settings *settings)
{
    if (settings == NULL)
        return false;
    memset(settings, 0, sizeof(*settings));
    if (!vm_mock_registration_ensure_tables() ||
        !vm_mysql_query(
            "SELECT allow_game_auto_account FROM server_registration_config "
            "WHERE config_id=1",
            vm_mock_registration_settings_row, settings) ||
        settings->invalid || !settings->found)
    {
        return false;
    }
    return true;
}

static bool vm_mock_registration_game_auto_account_allowed(void)
{
    vm_mock_registration_settings settings;

    return vm_mock_registration_load_settings(&settings) &&
           settings.allowGameAutoAccount;
}

static bool vm_mock_registration_smtp_row(void *contextValue,
                                          unsigned int columnCount,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_registration_smtp_config *config =
        (vm_mock_registration_smtp_config *)contextValue;
    u32 port = 0;
    u32 enabled = 0;
    size_t decoded = 0;

    if (config == NULL || config->found || columnCount != 6 ||
        !vm_mock_registration_copy_text(config->host, sizeof(config->host),
                                        values[0], lengths[0]) ||
        !vm_mock_registration_parse_u32(values[1], lengths[1], &port) ||
        port == 0 || port > 65535 || values[2] == NULL || values[3] == NULL ||
        !vm_mysql_hex_decode(values[2], lengths[2], config->username,
                             sizeof(config->username) - 1, &decoded))
    {
        if (config != NULL)
            config->invalid = true;
        return true;
    }
    config->username[decoded] = 0;
    decoded = 0;
    if (!vm_mysql_hex_decode(values[3], lengths[3], config->password,
                             sizeof(config->password) - 1, &decoded) ||
        !vm_mock_registration_copy_text(config->senderEmail,
                                        sizeof(config->senderEmail),
                                        values[4], lengths[4]) ||
        !vm_mock_registration_parse_u32(values[5], lengths[5], &enabled) ||
        enabled > 1)
    {
        config->invalid = true;
        return true;
    }
    config->password[decoded] = 0;
    config->port = (u16)port;
    config->enabled = enabled != 0;
    config->found = true;
    return true;
}

static bool vm_mock_registration_load_smtp_config(
    vm_mock_registration_smtp_config *config)
{
    if (config == NULL)
        return false;
    memset(config, 0, sizeof(*config));
    if (!vm_mock_registration_ensure_tables() ||
        !vm_mysql_query(
            "SELECT host,port,HEX(username),HEX(password_value),sender_email,enabled "
            "FROM server_smtp_config WHERE config_id=1",
            vm_mock_registration_smtp_row, config) ||
        config->invalid || !config->found)
    {
        return false;
    }
    return true;
}

static bool vm_mock_registration_email_template_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_mock_registration_email_template *emailTemplate =
        (vm_mock_registration_email_template *)contextValue;
    size_t decoded = 0;

    if (emailTemplate == NULL || emailTemplate->found || columnCount != 2 ||
        values[0] == NULL || values[1] == NULL ||
        !vm_mysql_hex_decode(values[0], lengths[0], emailTemplate->subject,
                             sizeof(emailTemplate->subject) - 1, &decoded))
    {
        if (emailTemplate != NULL)
            emailTemplate->invalid = true;
        return true;
    }
    emailTemplate->subject[decoded] = 0;
    decoded = 0;
    if (!vm_mysql_hex_decode(values[1], lengths[1], emailTemplate->body,
                             sizeof(emailTemplate->body) - 1, &decoded))
    {
        emailTemplate->invalid = true;
        return true;
    }
    emailTemplate->body[decoded] = 0;
    emailTemplate->found = true;
    return true;
}

static bool vm_mock_registration_load_email_template(
    vm_mock_registration_email_template *emailTemplate)
{
    if (emailTemplate == NULL)
        return false;
    memset(emailTemplate, 0, sizeof(*emailTemplate));
    if (!vm_mock_registration_ensure_tables() ||
        !vm_mysql_query(
            "SELECT HEX(subject),HEX(body) FROM server_registration_email_templates "
            "WHERE config_id=1",
            vm_mock_registration_email_template_row, emailTemplate) ||
        emailTemplate->invalid || !emailTemplate->found)
    {
        return false;
    }
    return true;
}

static bool vm_mock_registration_normalize_email(const char *input,
                                                 char *out, size_t outCap)
{
    size_t length = input ? strlen(input) : 0;
    size_t at = (size_t)-1;
    bool previousDot = false;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (length < 6 || length >= outCap || length >= VM_MOCK_REGISTRATION_EMAIL_MAX)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)input[i];
        bool allowed = (ch >= 'a' && ch <= 'z') ||
                       (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
                       ch == '-' || ch == '+' || ch == '@';

        if (!allowed)
            return false;
        if (ch == '@')
        {
            if (at != (size_t)-1 || i == 0 || i + 1 == length || previousDot)
                return false;
            at = i;
            previousDot = false;
        }
        else
        {
            if (ch == '.' && (i == 0 || i + 1 == length || previousDot))
                return false;
            previousDot = ch == '.';
        }
        out[i] = (char)tolower(ch);
    }
    out[length] = 0;
    if (at == (size_t)-1 || at < 1 || at + 3 >= length ||
        out[at + 1] == '-' || out[length - 1] == '-' ||
        strchr(out + at + 1, '.') == NULL)
    {
        out[0] = 0;
        return false;
    }
    return true;
}

static bool vm_mock_registration_smtp_host_valid(const char *host)
{
    size_t length = host ? strlen(host) : 0;

    if (length == 0 || length >= VM_MOCK_REGISTRATION_SMTP_HOST_MAX)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)host[i];
        if (!(isalnum(ch) || ch == '.' || ch == '-' || ch == ':'))
            return false;
    }
    return true;
}

static bool vm_mock_registration_smtp_text_valid(const char *value,
                                                 size_t cap)
{
    size_t length = value ? strlen(value) : 0;

    if (length >= cap)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)value[i];
        if (ch < 0x20 || ch == 0x7f || ch == '\r' || ch == '\n')
            return false;
    }
    return true;
}

static bool vm_mock_registration_email_subject_valid(const char *subject)
{
    size_t length = subject ? strlen(subject) : 0;

    if (length == 0 || length >= VM_MOCK_REGISTRATION_EMAIL_SUBJECT_MAX)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)subject[i];

        if (ch < 0x20 || ch == 0x7f)
            return false;
    }
    return true;
}

static bool vm_mock_registration_email_body_valid(const char *body)
{
    size_t length = body ? strlen(body) : 0;

    if (length == 0 || length >= VM_MOCK_REGISTRATION_EMAIL_BODY_MAX ||
        strstr(body, VM_MOCK_REGISTRATION_CODE_PLACEHOLDER) == NULL)
    {
        return false;
    }
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)body[i];

        if ((ch < 0x20 && ch != '\r' && ch != '\n' && ch != '\t') ||
            ch == 0x7f)
        {
            return false;
        }
    }
    return true;
}

static bool vm_mock_registration_email_template_ready(
    const vm_mock_registration_email_template *emailTemplate)
{
    return emailTemplate != NULL &&
           vm_mock_registration_email_subject_valid(emailTemplate->subject) &&
           vm_mock_registration_email_body_valid(emailTemplate->body);
}

static bool vm_mock_registration_smtp_config_ready(
    const vm_mock_registration_smtp_config *config)
{
    char normalizedSender[VM_MOCK_REGISTRATION_EMAIL_MAX];

    return config != NULL && config->enabled && config->port != 0 &&
           vm_mock_registration_smtp_host_valid(config->host) &&
           vm_mock_registration_smtp_text_valid(config->username,
                                                sizeof(config->username)) &&
           vm_mock_registration_smtp_text_valid(config->password,
                                                sizeof(config->password)) &&
           (!config->username[0] || config->password[0]) &&
           vm_mock_registration_normalize_email(config->senderEmail,
                                                normalizedSender,
                                                sizeof(normalizedSender));
}

static bool vm_mock_registration_web_email_available(void)
{
    vm_mock_registration_smtp_config config;
    vm_mock_registration_email_template emailTemplate;

    return vm_mock_registration_load_smtp_config(&config) &&
           vm_mock_registration_load_email_template(&emailTemplate) &&
           vm_mock_registration_smtp_config_ready(&config) &&
           vm_mock_registration_email_template_ready(&emailTemplate);
}

static bool vm_mock_registration_parse_checkbox(const char *body,
                                                 const char *key)
{
    char value[16];

    if (!vm_mock_admin_form_value(body, key, value, sizeof(value)))
        return false;
    return strcmp(value, "1") == 0 || strcmp(value, "on") == 0 ||
           strcmp(value, "true") == 0;
}

static bool vm_mock_registration_save_admin_config(
    const char *body, const char **messageOut)
{
    vm_mock_registration_smtp_config current;
    vm_mock_registration_smtp_config next;
    vm_mock_registration_email_template currentTemplate;
    vm_mock_registration_email_template nextTemplate;
    char portText[16];
    char settingsQuery[160];
    char smtpQuery[4096];
    char templateQuery[VM_MOCK_REGISTRATION_EMAIL_BODY_MAX * 2 + 512];
    char hostHex[VM_MOCK_REGISTRATION_SMTP_HOST_MAX * 2 + 1];
    char userHex[VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX * 2 + 1];
    char passwordHex[VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX * 2 + 1];
    char senderHex[VM_MOCK_REGISTRATION_EMAIL_MAX * 2 + 1];
    char subjectHex[VM_MOCK_REGISTRATION_EMAIL_SUBJECT_MAX * 2 + 1];
    char bodyHex[VM_MOCK_REGISTRATION_EMAIL_BODY_MAX * 2 + 1];
    char normalizedSender[VM_MOCK_REGISTRATION_EMAIL_MAX];
    u32 port = 0;
    bool allowGameAutoAccount =
        vm_mock_registration_parse_checkbox(body, "allow_game_auto_account");
    bool replacePassword = false;
    bool clearPassword = false;
    bool transaction = false;

    if (messageOut != NULL)
        *messageOut = "注册设置保存失败";
    if (!vm_mock_registration_load_smtp_config(&current) ||
        !vm_mock_registration_load_email_template(&currentTemplate))
    {
        if (messageOut != NULL)
            *messageOut = "无法读取当前 SMTP 或邮件模板配置";
        return false;
    }
    next = current;
    nextTemplate = currentTemplate;
    if (!vm_mock_admin_form_value(body, "smtp_host", next.host,
                                  sizeof(next.host)) ||
        !vm_mock_admin_form_value(body, "smtp_port", portText,
                                  sizeof(portText)) ||
        !vm_net_mock_parse_u32_strict(portText, &port) || port == 0 ||
        port > 65535 ||
        !vm_mock_admin_form_value(body, "smtp_username", next.username,
                                  sizeof(next.username)) ||
        !vm_mock_admin_form_value(body, "smtp_sender_email", next.senderEmail,
                                  sizeof(next.senderEmail)))
    {
        if (messageOut != NULL)
            *messageOut = "SMTP 主机、端口或发件人参数无效";
        return false;
    }
    if (!vm_mock_admin_form_value(body, "registration_email_subject",
                                  nextTemplate.subject,
                                  sizeof(nextTemplate.subject)) ||
        !vm_mock_admin_form_value(body, "registration_email_body",
                                  nextTemplate.body,
                                  sizeof(nextTemplate.body)) ||
        !vm_mock_registration_email_template_ready(&nextTemplate))
    {
        if (messageOut != NULL)
            *messageOut = "邮件主题不能为空；邮件内容必须保留 {{code}} 占位符";
        return false;
    }
    next.port = (u16)port;
    next.enabled = vm_mock_registration_parse_checkbox(body, "smtp_enabled");
    clearPassword = vm_mock_registration_parse_checkbox(body,
                                                        "clear_smtp_password");
    {
        char password[VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX];

        memset(password, 0, sizeof(password));
        if (!vm_mock_admin_form_value(body, "smtp_password", password,
                                      sizeof(password)))
        {
            password[0] = 0;
        }
        if (password[0] != 0)
        {
            snprintf(next.password, sizeof(next.password), "%s", password);
            replacePassword = true;
        }
        else if (clearPassword)
        {
            next.password[0] = 0;
            replacePassword = true;
        }
        memset(password, 0, sizeof(password));
    }
    if (!vm_mock_registration_smtp_text_valid(next.host, sizeof(next.host)) ||
        !vm_mock_registration_smtp_text_valid(next.username,
                                              sizeof(next.username)) ||
        !vm_mock_registration_smtp_text_valid(next.password,
                                              sizeof(next.password)) ||
        (next.senderEmail[0] != 0 &&
         !vm_mock_registration_normalize_email(next.senderEmail,
                                                normalizedSender,
                                                sizeof(normalizedSender))) ||
        (next.enabled && (!vm_mock_registration_normalize_email(
                              next.senderEmail, normalizedSender,
                              sizeof(normalizedSender)) ||
                          !vm_mock_registration_smtp_config_ready(&next))))
    {
        if (messageOut != NULL)
            *messageOut = next.enabled ?
                "启用 SMTP 前须填写有效主机、端口、发件邮箱及完整认证信息" :
                "SMTP 配置参数无效";
        return false;
    }
    if (next.senderEmail[0] != 0)
        snprintf(next.senderEmail, sizeof(next.senderEmail), "%s", normalizedSender);
    if (!replacePassword)
        snprintf(next.password, sizeof(next.password), "%s", current.password);
    if (vm_mysql_hex_encode(next.host, strlen(next.host), hostHex,
                            sizeof(hostHex)) == 0 ||
        vm_mysql_hex_encode(next.username, strlen(next.username), userHex,
                            sizeof(userHex)) == 0 ||
        vm_mysql_hex_encode(next.password, strlen(next.password), passwordHex,
                            sizeof(passwordHex)) == 0 ||
        vm_mysql_hex_encode(next.senderEmail, strlen(next.senderEmail), senderHex,
                            sizeof(senderHex)) == 0 ||
        vm_mysql_hex_encode(nextTemplate.subject, strlen(nextTemplate.subject),
                            subjectHex, sizeof(subjectHex)) == 0 ||
        vm_mysql_hex_encode(nextTemplate.body, strlen(nextTemplate.body),
                            bodyHex, sizeof(bodyHex)) == 0)
    {
        if (messageOut != NULL)
            *messageOut = "SMTP 配置编码失败";
        return false;
    }
    snprintf(settingsQuery, sizeof(settingsQuery),
             "UPDATE server_registration_config SET allow_game_auto_account=%u "
             "WHERE config_id=1",
             allowGameAutoAccount ? 1 : 0);
    snprintf(smtpQuery, sizeof(smtpQuery),
             "UPDATE server_smtp_config SET host=CAST(X'%s' AS CHAR),port=%u,"
             "username=X'%s',password_value=X'%s',sender_email=CAST(X'%s' AS CHAR),"
             "enabled=%u WHERE config_id=1",
             hostHex, next.port, userHex, passwordHex, senderHex,
             next.enabled ? 1 : 0);
    snprintf(templateQuery, sizeof(templateQuery),
             "UPDATE server_registration_email_templates SET subject=X'%s',"
             "body=X'%s' WHERE config_id=1",
             subjectHex, bodyHex);
    if (!vm_mysql_exec("START TRANSACTION"))
        return false;
    transaction = true;
    if (!vm_mysql_exec(settingsQuery) || !vm_mysql_exec(smtpQuery) ||
        !vm_mysql_exec(templateQuery) ||
        !vm_mysql_exec("COMMIT"))
    {
        if (transaction)
            (void)vm_mysql_exec("ROLLBACK");
        if (messageOut != NULL)
            *messageOut = "注册设置写入数据库失败";
        return false;
    }
    if (messageOut != NULL)
        *messageOut = "注册设置、SMTP 与邮件模板已保存";
    return true;
}

static bool vm_mock_registration_verification_row(void *contextValue,
                                                  unsigned int columnCount,
                                                  const char *const *values,
                                                  const size_t *lengths)
{
    vm_mock_registration_verification *verification =
        (vm_mock_registration_verification *)contextValue;

    if (verification == NULL || verification->found || columnCount != 4 ||
        !vm_mock_registration_copy_text(verification->codeDigest,
                                        sizeof(verification->codeDigest),
                                        values[0], lengths[0]) ||
        strlen(verification->codeDigest) != 32 ||
        !vm_mock_registration_parse_u32(values[1], lengths[1],
                                        &verification->expiresAt) ||
        !vm_mock_registration_parse_u32(values[2], lengths[2],
                                        &verification->lastSentAt) ||
        !vm_mock_registration_parse_u32(values[3], lengths[3],
                                        &verification->attemptCount))
    {
        if (verification != NULL)
            verification->invalid = true;
        return true;
    }
    verification->found = true;
    return true;
}

static bool vm_mock_registration_load_verification(const char *email,
                                                    bool forUpdate,
                                                    vm_mock_registration_verification *verification)
{
    char emailHex[VM_MOCK_REGISTRATION_EMAIL_MAX * 2 + 1];
    char query[1024];

    if (verification == NULL || email == NULL ||
        vm_mysql_hex_encode(email, strlen(email), emailHex, sizeof(emailHex)) == 0)
    {
        return false;
    }
    memset(verification, 0, sizeof(*verification));
    snprintf(query, sizeof(query),
             "SELECT code_digest,UNIX_TIMESTAMP(expires_at),UNIX_TIMESTAMP(last_sent_at),attempt_count "
             "FROM web_registration_email_verifications "
             "WHERE email=CAST(X'%s' AS CHAR)%s",
             emailHex, forUpdate ? " FOR UPDATE" : "");
    return vm_mysql_query(query, vm_mock_registration_verification_row,
                          verification) && !verification->invalid;
}

static bool vm_mock_registration_exists_row(void *contextValue,
                                            unsigned int columnCount,
                                            const char *const *values,
                                            const size_t *lengths)
{
    bool *found = (bool *)contextValue;

    (void)values;
    (void)lengths;
    if (found == NULL || *found || columnCount != 1)
        return false;
    *found = true;
    return true;
}

static bool vm_mock_registration_value_exists(const char *table,
                                              const char *column,
                                              const char *value,
                                              bool forUpdate,
                                              bool *foundOut)
{
    char valueHex[VM_MOCK_REGISTRATION_EMAIL_MAX * 2 + 1];
    char query[1024];
    bool found = false;

    if (foundOut != NULL)
        *foundOut = false;
    if (table == NULL || column == NULL || value == NULL ||
        vm_mysql_hex_encode(value, strlen(value), valueHex, sizeof(valueHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "SELECT 1 FROM %s WHERE %s=CAST(X'%s' AS CHAR) LIMIT 1%s",
             table, column, valueHex, forUpdate ? " FOR UPDATE" : "");
    if (!vm_mysql_query(query, vm_mock_registration_exists_row, &found))
        return false;
    if (foundOut != NULL)
        *foundOut = found;
    return true;
}

static bool vm_mock_registration_code_is_valid(const char *code)
{
    if (code == NULL || strlen(code) != VM_MOCK_REGISTRATION_CODE_LEN)
        return false;
    for (u32 i = 0; i < VM_MOCK_REGISTRATION_CODE_LEN; ++i)
    {
        if (code[i] < '0' || code[i] > '9')
            return false;
    }
    return true;
}

static void vm_mock_registration_code_digest(const char *email, const char *code,
                                             char digestOut[33])
{
    char material[VM_MOCK_REGISTRATION_EMAIL_MAX + 16];

    snprintf(material, sizeof(material), "%s|%s", email ? email : "",
             code ? code : "");
    vm_md5_hex(material, strlen(material), digestOut);
}

static bool vm_mock_registration_random_bytes(void *out, size_t length)
{
    if (out == NULL || length == 0)
        return false;
#ifdef _WIN32
    typedef BOOLEAN(APIENTRY *vm_mock_registration_rtl_gen_random_fn)(PVOID,
                                                                        ULONG);
    HMODULE advapi = LoadLibraryA("advapi32.dll");
    vm_mock_registration_rtl_gen_random_fn generate = NULL;
    bool ok = false;

    if (advapi == NULL)
        return false;
    generate = (vm_mock_registration_rtl_gen_random_fn)GetProcAddress(
        advapi, "SystemFunction036");
    if (generate != NULL && length <= 0xffffffffu)
        ok = generate(out, (ULONG)length) != 0;
    FreeLibrary(advapi);
    return ok;
#else
    FILE *random = fopen("/dev/urandom", "rb");
    bool ok = random != NULL && fread(out, 1, length, random) == length;
    if (random != NULL)
        fclose(random);
    return ok;
#endif
}

static bool vm_mock_registration_generate_code(char out[VM_MOCK_REGISTRATION_CODE_LEN + 1])
{
    u32 value = 0;
    const u32 ceiling = 0xffffffffu - (0xffffffffu % 1000000u);

    if (out == NULL)
        return false;
    for (u32 attempt = 0; attempt < 16; ++attempt)
    {
        if (!vm_mock_registration_random_bytes(&value, sizeof(value)))
            return false;
        if (value < ceiling)
        {
            snprintf(out, VM_MOCK_REGISTRATION_CODE_LEN + 1, "%06u",
                     value % 1000000u);
            return true;
        }
    }
    return false;
}

static bool vm_mock_registration_captcha_token_normalize(
    const char *input, char out[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1])
{
    if (input == NULL || out == NULL ||
        strlen(input) != VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN)
    {
        return false;
    }
    for (u32 i = 0; i < VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN; ++i)
    {
        unsigned char ch = (unsigned char)input[i];

        if (!isxdigit(ch))
            return false;
        out[i] = (char)toupper(ch);
    }
    out[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN] = 0;
    return true;
}

static bool vm_mock_registration_captcha_code_normalize(
    const char *input, char out[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1])
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

    if (input == NULL || out == NULL ||
        strlen(input) != VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN)
    {
        return false;
    }
    for (u32 i = 0; i < VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN; ++i)
    {
        unsigned char ch = (unsigned char)toupper((unsigned char)input[i]);

        if (strchr(alphabet, ch) == NULL)
            return false;
        out[i] = (char)ch;
    }
    out[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN] = 0;
    return true;
}

static bool vm_mock_registration_generate_captcha(
    char tokenOut[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1],
    char codeOut[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1])
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    u8 tokenBytes[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN / 2];
    u8 codeBytes[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN];

    if (tokenOut == NULL || codeOut == NULL || sizeof(alphabet) != 33 ||
        !vm_mock_registration_random_bytes(tokenBytes, sizeof(tokenBytes)) ||
        !vm_mock_registration_random_bytes(codeBytes, sizeof(codeBytes)) ||
        vm_mysql_hex_encode(tokenBytes, sizeof(tokenBytes), tokenOut,
                            VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1) !=
            VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN)
    {
        return false;
    }
    for (u32 i = 0; i < VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN; ++i)
        codeOut[i] = alphabet[codeBytes[i] & 31u];
    codeOut[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN] = 0;
    memset(tokenBytes, 0, sizeof(tokenBytes));
    memset(codeBytes, 0, sizeof(codeBytes));
    return true;
}

static void vm_mock_registration_captcha_digest(
    const char *email, const char *token,
    const char code[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1],
    char digestOut[33])
{
    char material[VM_MOCK_REGISTRATION_EMAIL_MAX +
                  VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN +
                  VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 3];

    snprintf(material, sizeof(material), "%s|%s|%s", email ? email : "",
             token ? token : "", code ? code : "");
    vm_md5_hex(material, strlen(material), digestOut);
    memset(material, 0, sizeof(material));
}

static bool vm_mock_registration_captcha_row(void *contextValue,
                                             unsigned int columnCount,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_mock_registration_captcha *captcha =
        (vm_mock_registration_captcha *)contextValue;

    if (captcha == NULL || captcha->found || columnCount != 4 ||
        !vm_mock_registration_copy_text(captcha->email, sizeof(captcha->email),
                                        values[0], lengths[0]) ||
        !vm_mock_registration_copy_text(captcha->codeDigest,
                                        sizeof(captcha->codeDigest), values[1],
                                        lengths[1]) ||
        strlen(captcha->codeDigest) != 32 ||
        !vm_mock_registration_parse_u32(values[2], lengths[2],
                                        &captcha->expiresAt) ||
        !vm_mock_registration_parse_u32(values[3], lengths[3],
                                        &captcha->attemptCount))
    {
        if (captcha != NULL)
            captcha->invalid = true;
        return true;
    }
    captcha->found = true;
    return true;
}

static bool vm_mock_registration_load_captcha(
    const char *token, bool forUpdate, vm_mock_registration_captcha *captcha)
{
    char normalizedToken[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1];
    char tokenHex[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN * 2 + 1];
    char query[768];

    if (captcha == NULL ||
        !vm_mock_registration_captcha_token_normalize(token, normalizedToken) ||
        !vm_mysql_hex_encode(normalizedToken, strlen(normalizedToken), tokenHex,
                            sizeof(tokenHex)))
    {
        return false;
    }
    memset(captcha, 0, sizeof(*captcha));
    snprintf(query, sizeof(query),
             "SELECT email,code_digest,UNIX_TIMESTAMP(expires_at),attempt_count "
             "FROM web_registration_image_captchas "
             "WHERE captcha_token=CAST(X'%s' AS CHAR)%s",
             tokenHex, forUpdate ? " FOR UPDATE" : "");
    return vm_mysql_query(query, vm_mock_registration_captcha_row, captcha) &&
           !captcha->invalid;
}

static bool vm_mock_registration_create_captcha(
    const char *emailInput,
    char tokenOut[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1],
    char codeOut[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1],
    const char **messageOut)
{
    char email[VM_MOCK_REGISTRATION_EMAIL_MAX];
    char token[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1];
    char code[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1];
    char digest[33];
    char emailHex[VM_MOCK_REGISTRATION_EMAIL_MAX * 2 + 1];
    char tokenHex[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN * 2 + 1];
    char query[1024];

    if (messageOut != NULL)
        *messageOut = "图片验证码暂不可用，请稍后重试";
    if (tokenOut == NULL || codeOut == NULL ||
        !vm_mock_registration_normalize_email(emailInput, email, sizeof(email)))
    {
        if (messageOut != NULL)
            *messageOut = "邮箱地址格式无效";
        return false;
    }
    tokenOut[0] = 0;
    codeOut[0] = 0;
    if (!vm_mock_registration_ensure_tables() ||
        vm_mysql_hex_encode(email, strlen(email), emailHex, sizeof(emailHex)) == 0)
    {
        return false;
    }
    (void)vm_mysql_exec(
        "DELETE FROM web_registration_image_captchas WHERE expires_at<"
        VM_MOCK_REGISTRATION_SQL_TIMESTAMP_NOW);
    for (u32 attempt = 0; attempt < 4; ++attempt)
    {
        if (!vm_mock_registration_generate_captcha(token, code) ||
            vm_mysql_hex_encode(token, strlen(token), tokenHex,
                                sizeof(tokenHex)) == 0)
        {
            break;
        }
        vm_mock_registration_captcha_digest(email, token, code, digest);
        snprintf(query, sizeof(query),
                 "INSERT INTO web_registration_image_captchas"
                 "(captcha_token,email,code_digest,attempt_count,expires_at) "
                 "VALUES(CAST(X'%s' AS CHAR),CAST(X'%s' AS CHAR),'%s',0,"
                 "DATE_ADD(" VM_MOCK_REGISTRATION_SQL_TIMESTAMP_NOW
                 ",INTERVAL %u SECOND))",
                 tokenHex, emailHex, digest,
                 VM_MOCK_REGISTRATION_CAPTCHA_EXPIRE_SECONDS);
        if (vm_mysql_exec(query))
        {
            snprintf(tokenOut, VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1,
                     "%s", token);
            snprintf(codeOut, VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1, "%s",
                     code);
            memset(code, 0, sizeof(code));
            return true;
        }
    }
    memset(code, 0, sizeof(code));
    return false;
}

static bool vm_mock_registration_verify_captcha(const char *emailInput,
                                                const char *tokenInput,
                                                const char *codeInput,
                                                const char **messageOut)
{
    char email[VM_MOCK_REGISTRATION_EMAIL_MAX];
    char token[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1];
    char code[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1];
    char tokenHex[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN * 2 + 1];
    char digest[33];
    char query[512];
    vm_mock_registration_captcha captcha;
    bool transaction = false;
    time_t now = time(NULL);

    if (messageOut != NULL)
        *messageOut = "图片验证码无效或已过期，请重新验证";
    if (!vm_mock_registration_normalize_email(emailInput, email, sizeof(email)) ||
        !vm_mock_registration_captcha_token_normalize(tokenInput, token) ||
        !vm_mock_registration_captcha_code_normalize(codeInput, code) ||
        vm_mysql_hex_encode(token, strlen(token), tokenHex, sizeof(tokenHex)) == 0 ||
        !vm_mock_registration_ensure_tables() || !vm_mysql_exec("START TRANSACTION"))
    {
        return false;
    }
    transaction = true;
    if (!vm_mock_registration_load_captcha(token, true, &captcha) ||
        !captcha.found || now <= 0 || captcha.expiresAt <= (u32)now ||
        captcha.attemptCount >= VM_MOCK_REGISTRATION_CAPTCHA_MAX_ATTEMPTS)
    {
        if (messageOut != NULL)
            *messageOut = "图片验证码无效或已过期，请重新验证";
        goto rejected;
    }
    vm_mock_registration_captcha_digest(email, token, code, digest);
    if (strcmp(captcha.email, email) != 0 ||
        strcmp(captcha.codeDigest, digest) != 0)
    {
        snprintf(query, sizeof(query),
                 "UPDATE web_registration_image_captchas "
                 "SET attempt_count=attempt_count+1 WHERE captcha_token=CAST(X'%s' AS CHAR) "
                 "AND attempt_count<%u",
                 tokenHex, VM_MOCK_REGISTRATION_CAPTCHA_MAX_ATTEMPTS);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
            goto failed_no_rollback;
        transaction = false;
        if (messageOut != NULL)
            *messageOut = "图片验证码错误，请重新验证";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM web_registration_image_captchas "
             "WHERE captcha_token=CAST(X'%s' AS CHAR)",
             tokenHex);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed_no_rollback;
    transaction = false;
    memset(code, 0, sizeof(code));
    return true;

rejected:
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    memset(code, 0, sizeof(code));
    return false;

failed_no_rollback:
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    memset(code, 0, sizeof(code));
    if (messageOut != NULL)
        *messageOut = "图片验证码服务暂不可用，请稍后重试";
    return false;
}

static size_t vm_mock_registration_base64_encode(const u8 *input, size_t length,
                                                 char *out, size_t outCap)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t required = ((length + 2) / 3) * 4;
    size_t in = 0;
    size_t pos = 0;

    if (out == NULL || outCap == 0 || (length != 0 && input == NULL) ||
        required + 1 > outCap)
    {
        return 0;
    }
    while (in < length)
    {
        u32 word = (u32)input[in++] << 16;
        bool haveSecond = in < length;
        bool haveThird = haveSecond && in + 1 < length;

        if (haveSecond)
            word |= (u32)input[in++] << 8;
        if (haveThird)
            word |= input[in++];
        out[pos++] = alphabet[(word >> 18) & 63u];
        out[pos++] = alphabet[(word >> 12) & 63u];
        out[pos++] = haveSecond ? alphabet[(word >> 6) & 63u] : '=';
        out[pos++] = haveThird ? alphabet[word & 63u] : '=';
    }
    out[pos] = 0;
    return pos;
}

static void vm_mock_registration_smtp_set_timeout(vm_mock_service_socket socketValue)
{
#ifdef _WIN32
    DWORD timeout = VM_MOCK_REGISTRATION_SMTP_TIMEOUT_MS;
    (void)setsockopt(socketValue, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
                     sizeof(timeout));
    (void)setsockopt(socketValue, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout,
                     sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = VM_MOCK_REGISTRATION_SMTP_TIMEOUT_MS / 1000;
    timeout.tv_usec = (VM_MOCK_REGISTRATION_SMTP_TIMEOUT_MS % 1000) * 1000;
    (void)setsockopt(socketValue, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     sizeof(timeout));
    (void)setsockopt(socketValue, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                     sizeof(timeout));
#endif
}

static bool vm_mock_registration_smtp_read_reply(vm_mock_service_socket socketValue,
                                                 int expectedCode,
                                                 int *receivedCodeOut)
{
    char line[VM_MOCK_REGISTRATION_SMTP_REPLY_MAX];
    int firstCode = 0;
    size_t length = 0;

    if (receivedCodeOut != NULL)
        *receivedCodeOut = 0;
    while (length + 1 < sizeof(line))
    {
        char ch = 0;
        int received = recv(socketValue, &ch, 1, 0);

        if (received != 1)
            return false;
        line[length++] = ch;
        if (ch != '\n')
            continue;
        line[length] = 0;
        if (length < 5 || !isdigit((unsigned char)line[0]) ||
            !isdigit((unsigned char)line[1]) || !isdigit((unsigned char)line[2]) ||
            (line[3] != ' ' && line[3] != '-'))
        {
            return false;
        }
        {
            int code = (line[0] - '0') * 100 + (line[1] - '0') * 10 +
                       (line[2] - '0');
            if (firstCode == 0)
                firstCode = code;
            if (receivedCodeOut != NULL)
                *receivedCodeOut = code;
            if (code != firstCode)
                return false;
            if (line[3] == ' ')
                return code == expectedCode;
        }
        length = 0;
    }
    return false;
}

static void vm_mock_registration_smtp_log_failure(
    const vm_mock_registration_smtp_config *config, const char *stage,
    int expectedCode, int receivedCode)
{
    printf("[warn][user-web] smtp_delivery_failed stage=%s host=%s port=%u "
           "expected=%d received=%d\n",
           stage ? stage : "unknown",
           config && config->host[0] ? config->host : "-",
           config ? config->port : 0, expectedCode, receivedCode);
}

static bool vm_mock_registration_smtp_command(
    const vm_mock_registration_smtp_config *config,
    vm_mock_service_socket socketValue, const char *stage, const char *command,
    int expectedCode)
{
    int receivedCode = 0;

    if (command == NULL ||
        !vm_mock_service_send_all(socketValue, (const u8 *)command,
                                  (u32)strlen(command)))
    {
        vm_mock_registration_smtp_log_failure(config, stage, expectedCode, 0);
        return false;
    }
    if (!vm_mock_registration_smtp_read_reply(socketValue, expectedCode,
                                              &receivedCode))
    {
        vm_mock_registration_smtp_log_failure(config, stage, expectedCode,
                                              receivedCode);
        return false;
    }
    return true;
}

static bool vm_mock_registration_render_email_body(
    const vm_mock_registration_email_template *emailTemplate, const char *code,
    char *out, size_t outCap)
{
    const char *cursor = NULL;
    const char *placeholder = NULL;
    size_t outLength = 0;
    size_t codeLength = 0;
    bool replaced = false;

    if (!vm_mock_registration_email_template_ready(emailTemplate) ||
        !vm_mock_registration_code_is_valid(code) || out == NULL || outCap == 0)
    {
        return false;
    }
    cursor = emailTemplate->body;
    codeLength = strlen(code);
    while ((placeholder = strstr(cursor,
                                 VM_MOCK_REGISTRATION_CODE_PLACEHOLDER)) != NULL)
    {
        size_t prefixLength = (size_t)(placeholder - cursor);

        if (prefixLength > outCap - 1 - outLength ||
            codeLength > outCap - 1 - outLength - prefixLength)
        {
            return false;
        }
        memcpy(out + outLength, cursor, prefixLength);
        outLength += prefixLength;
        memcpy(out + outLength, code, codeLength);
        outLength += codeLength;
        cursor = placeholder + strlen(VM_MOCK_REGISTRATION_CODE_PLACEHOLDER);
        replaced = true;
    }
    {
        size_t tailLength = strlen(cursor);

        if (tailLength > outCap - 1 - outLength)
            return false;
        memcpy(out + outLength, cursor, tailLength);
        outLength += tailLength;
    }
    out[outLength] = 0;
    return replaced;
}

/* Normalize text-area line endings and dot-stuff body lines so administrator
 * supplied text remains a single SMTP DATA payload. */
static bool vm_mock_registration_smtp_escape_body(const char *body, char *out,
                                                  size_t outCap)
{
    size_t outLength = 0;
    bool atLineStart = true;

    if (body == NULL || out == NULL || outCap == 0)
        return false;
    for (size_t i = 0; body[i] != 0; ++i)
    {
        unsigned char ch = (unsigned char)body[i];

        if (ch == '\r' || ch == '\n')
        {
            if (ch == '\r' && body[i + 1] == '\n')
                ++i;
            if (outLength + 2 >= outCap)
                return false;
            out[outLength++] = '\r';
            out[outLength++] = '\n';
            atLineStart = true;
            continue;
        }
        if (atLineStart && ch == '.')
        {
            if (outLength + 1 >= outCap)
                return false;
            out[outLength++] = '.';
        }
        if (outLength + 1 >= outCap)
            return false;
        out[outLength++] = (char)ch;
        atLineStart = false;
    }
    if (!atLineStart)
    {
        if (outLength + 2 >= outCap)
            return false;
        out[outLength++] = '\r';
        out[outLength++] = '\n';
    }
    out[outLength] = 0;
    return true;
}

static bool vm_mock_registration_smtp_send_code(
    const vm_mock_registration_smtp_config *config,
    const vm_mock_registration_email_template *emailTemplate,
    const char *recipient, const char *code)
{
    vm_mock_service_socket socketValue = VM_MOCK_SERVICE_INVALID_SOCKET;
    struct sockaddr_in address;
    char command[1024];
    char authRaw[VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX * 2 + 2];
    char authEncoded[VM_MOCK_REGISTRATION_SMTP_CREDENTIAL_MAX * 3 + 8];
    char subjectEncoded[VM_MOCK_REGISTRATION_EMAIL_SUBJECT_MAX * 2];
    char renderedBody[VM_MOCK_REGISTRATION_EMAIL_BODY_MAX];
    char escapedBody[VM_MOCK_REGISTRATION_SMTP_MESSAGE_MAX];
    char message[VM_MOCK_REGISTRATION_SMTP_MESSAGE_MAX];
    bool ok = false;
    size_t userLen = 0;
    size_t passwordLen = 0;
    int receivedCode = 0;

    if (!vm_mock_registration_smtp_config_ready(config) ||
        !vm_mock_registration_email_template_ready(emailTemplate) ||
        recipient == NULL ||
        !vm_mock_registration_code_is_valid(code) ||
        !vm_mock_service_socket_init())
    {
        vm_mock_registration_smtp_log_failure(config, "preflight", 0, 0);
        return false;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(config->port);
    if (!vm_mock_service_resolve_ipv4_host(config->host, 0, &address.sin_addr))
    {
        vm_mock_registration_smtp_log_failure(config, "resolve_ipv4", 0, 0);
        return false;
    }
    socketValue = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketValue == VM_MOCK_SERVICE_INVALID_SOCKET)
    {
        vm_mock_registration_smtp_log_failure(config, "socket", 0, 0);
        return false;
    }
    vm_mock_registration_smtp_set_timeout(socketValue);
    if (connect(socketValue, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        vm_mock_registration_smtp_log_failure(config, "connect", 0, 0);
        goto done;
    }
    if (!vm_mock_registration_smtp_read_reply(socketValue, 220, &receivedCode))
    {
        vm_mock_registration_smtp_log_failure(config, "greeting", 220,
                                              receivedCode);
        goto done;
    }
    if (!vm_mock_registration_smtp_command(config, socketValue, "ehlo",
                                            "EHLO jh-online-server\r\n", 250))
    {
        goto done;
    }
    if (config->username[0] != 0)
    {
        userLen = strlen(config->username);
        passwordLen = strlen(config->password);
        if (userLen + passwordLen + 2 > sizeof(authRaw))
            goto done;
        authRaw[0] = 0;
        memcpy(authRaw + 1, config->username, userLen);
        authRaw[userLen + 1] = 0;
        memcpy(authRaw + userLen + 2, config->password, passwordLen);
        if (vm_mock_registration_base64_encode((const u8 *)authRaw,
                                               userLen + passwordLen + 2,
                                               authEncoded,
                                               sizeof(authEncoded)) == 0 ||
            snprintf(command, sizeof(command), "AUTH PLAIN %s\r\n", authEncoded) >=
                (int)sizeof(command))
        {
            vm_mock_registration_smtp_log_failure(config, "auth_prepare", 235,
                                                  0);
            goto done;
        }
        if (!vm_mock_registration_smtp_command(config, socketValue, "auth_plain",
                                                command, 235))
        {
            goto done;
        }
    }
    if (snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n",
                 config->senderEmail) >= (int)sizeof(command))
    {
        vm_mock_registration_smtp_log_failure(config, "mail_from_prepare", 250,
                                              0);
        goto done;
    }
    if (!vm_mock_registration_smtp_command(config, socketValue, "mail_from",
                                            command, 250))
        goto done;
    if (snprintf(command, sizeof(command), "RCPT TO:<%s>\r\n", recipient) >=
        (int)sizeof(command))
    {
        vm_mock_registration_smtp_log_failure(config, "rcpt_to_prepare", 250,
                                              0);
        goto done;
    }
    if (!vm_mock_registration_smtp_command(config, socketValue, "rcpt_to",
                                            command, 250) ||
        !vm_mock_registration_smtp_command(config, socketValue, "data",
                                            "DATA\r\n", 354))
    {
        goto done;
    }
    if (vm_mock_registration_base64_encode((const u8 *)emailTemplate->subject,
                                           strlen(emailTemplate->subject),
                                           subjectEncoded,
                                           sizeof(subjectEncoded)) == 0 ||
        !vm_mock_registration_render_email_body(emailTemplate, code,
                                                renderedBody,
                                                sizeof(renderedBody)) ||
        !vm_mock_registration_smtp_escape_body(renderedBody, escapedBody,
                                               sizeof(escapedBody)) ||
        snprintf(message, sizeof(message),
                 "From: <%s>\r\nTo: <%s>\r\n"
                 "Subject: =?UTF-8?B?%s?=\r\n"
                 "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
                 "Content-Transfer-Encoding: 8bit\r\n\r\n%s.\r\n",
                 config->senderEmail, recipient, subjectEncoded, escapedBody) >=
            (int)sizeof(message))
    {
        vm_mock_registration_smtp_log_failure(config, "data_prepare", 250, 0);
        goto done;
    }
    if (!vm_mock_service_send_all(socketValue, (const u8 *)message,
                                  (u32)strlen(message)))
    {
        vm_mock_registration_smtp_log_failure(config, "data_body", 250, 0);
        goto done;
    }
    receivedCode = 0;
    if (!vm_mock_registration_smtp_read_reply(socketValue, 250, &receivedCode))
    {
        vm_mock_registration_smtp_log_failure(config, "data_result", 250,
                                              receivedCode);
        goto done;
    }
    ok = true;
    (void)vm_mock_service_send_all(socketValue, (const u8 *)"QUIT\r\n", 6);

done:
    memset(authRaw, 0, sizeof(authRaw));
    memset(authEncoded, 0, sizeof(authEncoded));
    memset(renderedBody, 0, sizeof(renderedBody));
    memset(escapedBody, 0, sizeof(escapedBody));
    memset(message, 0, sizeof(message));
    vm_mock_service_socket_close(socketValue);
    return ok;
}

static bool vm_mock_registration_store_verification(const char *email,
                                                    const char *code)
{
    char emailHex[VM_MOCK_REGISTRATION_EMAIL_MAX * 2 + 1];
    char digest[33];
    char query[1024];

    if (email == NULL || !vm_mock_registration_code_is_valid(code) ||
        vm_mysql_hex_encode(email, strlen(email), emailHex, sizeof(emailHex)) == 0)
    {
        return false;
    }
    vm_mock_registration_code_digest(email, code, digest);
    snprintf(query, sizeof(query),
             "INSERT INTO web_registration_email_verifications"
             "(email,code_digest,attempt_count,expires_at,last_sent_at) "
             "VALUES(CAST(X'%s' AS CHAR),'%s',0,DATE_ADD("
             VM_MOCK_REGISTRATION_SQL_TIMESTAMP_NOW
             ",INTERVAL 10 MINUTE),"
             VM_MOCK_REGISTRATION_SQL_TIMESTAMP_NOW ") "
             "ON DUPLICATE KEY UPDATE code_digest=VALUES(code_digest),attempt_count=0,"
             "expires_at=VALUES(expires_at),last_sent_at=VALUES(last_sent_at)",
             emailHex, digest);
    return vm_mysql_exec(query);
}

static bool vm_mock_registration_send_verification_code(const char *emailInput,
                                                        char *emailOut,
                                                        size_t emailOutCap,
                                                        const char **messageOut)
{
    vm_mock_registration_smtp_config config;
    vm_mock_registration_email_template emailTemplate;
    vm_mock_registration_verification existing;
    char code[VM_MOCK_REGISTRATION_CODE_LEN + 1];
    time_t now = time(NULL);

    if (messageOut != NULL)
        *messageOut = "验证码发送失败，请稍后重试";
    if (!vm_mock_registration_normalize_email(emailInput, emailOut, emailOutCap))
    {
        if (messageOut != NULL)
            *messageOut = "邮箱地址格式无效";
        return false;
    }
    if (!vm_mock_registration_load_smtp_config(&config) ||
        !vm_mock_registration_load_email_template(&emailTemplate) ||
        !vm_mock_registration_smtp_config_ready(&config) ||
        !vm_mock_registration_email_template_ready(&emailTemplate))
    {
        if (messageOut != NULL)
            *messageOut = "注册邮箱服务尚未配置，请联系管理员";
        return false;
    }
    if (!vm_mock_registration_load_verification(emailOut, false, &existing))
    {
        if (messageOut != NULL)
            *messageOut = "验证码服务暂不可用";
        return false;
    }
    if (existing.found && now > 0 && existing.lastSentAt != 0 &&
        (u64)now < (u64)existing.lastSentAt +
                        VM_MOCK_REGISTRATION_CODE_RESEND_SECONDS)
    {
        if (messageOut != NULL)
            *messageOut = "验证码已发送，请在 60 秒后再试";
        return false;
    }
    if (!vm_mock_registration_generate_code(code) ||
        !vm_mock_registration_smtp_send_code(&config, &emailTemplate, emailOut,
                                             code) ||
        !vm_mock_registration_store_verification(emailOut, code))
    {
        if (messageOut != NULL)
            *messageOut = "验证码发送失败，请检查 SMTP 配置和服务日志";
        memset(code, 0, sizeof(code));
        return false;
    }
    printf("[info][user-web] registration_code_sent email=%s smtp=%s:%u\n",
           emailOut, config.host, config.port);
    memset(code, 0, sizeof(code));
    if (messageOut != NULL)
        *messageOut = "验证码已发送，请在 10 分钟内完成注册";
    return true;
}

static bool vm_mock_registration_create_verified_account(
    const char *account, const char *password, const char *emailInput,
    const char *code, const char **messageOut)
{
    char email[VM_MOCK_REGISTRATION_EMAIL_MAX];
    char accountHex[127];
    char passwordHex[127];
    char emailHex[VM_MOCK_REGISTRATION_EMAIL_MAX * 2 + 1];
    char digest[33];
    char query[2048];
    vm_mock_registration_verification verification;
    bool accountExists = false;
    bool emailExists = false;
    bool transaction = false;
    time_t now = time(NULL);

    if (messageOut != NULL)
        *messageOut = "注册失败";
    if (!vm_mock_registration_normalize_email(emailInput, email, sizeof(email)) ||
        !vm_mock_registration_code_is_valid(code) || account == NULL ||
        password == NULL ||
        vm_mysql_hex_encode(account, strlen(account), accountHex,
                            sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(password, strlen(password), passwordHex,
                            sizeof(passwordHex)) == 0 ||
        vm_mysql_hex_encode(email, strlen(email), emailHex, sizeof(emailHex)) == 0 ||
        !vm_mock_registration_ensure_tables() || !vm_mysql_exec("START TRANSACTION"))
    {
        if (messageOut != NULL)
            *messageOut = "注册服务暂不可用";
        return false;
    }
    transaction = true;
    if (!vm_mock_registration_load_verification(email, true, &verification))
        goto failed;
    if (!verification.found || now <= 0 || verification.expiresAt <= (u32)now ||
        verification.attemptCount >= VM_MOCK_REGISTRATION_CODE_MAX_ATTEMPTS)
    {
        if (messageOut != NULL)
            *messageOut = "验证码已过期或尝试次数过多，请重新获取";
        goto rejected;
    }
    vm_mock_registration_code_digest(email, code, digest);
    if (strcmp(verification.codeDigest, digest) != 0)
    {
        snprintf(query, sizeof(query),
                 "UPDATE web_registration_email_verifications SET attempt_count=attempt_count+1 "
                 "WHERE email=CAST(X'%s' AS CHAR) AND attempt_count<%u",
                 emailHex, VM_MOCK_REGISTRATION_CODE_MAX_ATTEMPTS);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
            goto failed_no_rollback;
        transaction = false;
        if (messageOut != NULL)
            *messageOut = "邮箱验证码错误";
        return false;
    }
    if (!vm_mock_registration_value_exists("accounts", "account_id", account,
                                           true, &accountExists) ||
        !vm_mock_registration_value_exists("account_email_bindings", "email", email,
                                           true, &emailExists))
    {
        goto failed;
    }
    if (accountExists)
    {
        if (messageOut != NULL)
            *messageOut = "该账号名已被注册";
        goto rejected;
    }
    if (emailExists)
    {
        if (messageOut != NULL)
            *messageOut = "该邮箱已绑定其他账号";
        goto rejected;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO accounts(account_id,password_value) VALUES(CAST(X'%s' AS CHAR),X'%s')",
             accountHex, passwordHex);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "INSERT INTO account_wallets(account_id,wcoin) VALUES(CAST(X'%s' AS CHAR),0)",
             accountHex);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "INSERT INTO account_email_bindings(account_id,email) "
             "VALUES(CAST(X'%s' AS CHAR),CAST(X'%s' AS CHAR))",
             accountHex, emailHex);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "DELETE FROM web_registration_email_verifications "
             "WHERE email=CAST(X'%s' AS CHAR)", emailHex);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed_no_rollback;
    transaction = false;
    if (messageOut != NULL)
        *messageOut = "ok";
    return true;

rejected:
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    return false;

failed:
    if (messageOut != NULL)
        *messageOut = "注册数据写入失败";
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    return false;

failed_no_rollback:
    if (messageOut != NULL)
        *messageOut = "注册事务提交失败";
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    return false;
}

static void vm_mock_admin_render_registration_settings_page(
    char *response, size_t responseCap, const char *query)
{
    vm_mock_registration_settings settings;
    vm_mock_registration_smtp_config smtp;
    vm_mock_registration_email_template emailTemplate;
    vm_mock_admin_text page;
    char status[16];
    char message[256];

    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(&page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 注册设置</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f7f9fc;color:#1d2939;font:14px/1.6 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{width:min(960px,calc(100%% - 28px));margin:0 auto;padding:28px 0 44px}header{display:flex;justify-content:space-between;gap:18px;align-items:center;margin-bottom:16px}h1{margin:0;font-size:24px}.sub,.hint{color:#667085;margin:4px 0 0}.logout{border:1px solid #d0d5dd;border-radius:8px;padding:8px 13px;background:#fff;cursor:pointer}.tabs{display:flex;gap:7px;flex-wrap:wrap;margin:0 0 16px}.tab{padding:8px 11px;border:1px solid #d0d5dd;border-radius:8px;background:#fff;color:#344054;text-decoration:none}.tab.on{background:#175cd3;border-color:#175cd3;color:#fff}.card{margin-top:14px;padding:20px;border:1px solid #e4e7ec;border-radius:12px;background:#fff;box-shadow:0 2px 7px #1018280a}h2{font-size:18px;margin:0}form{display:grid;gap:16px}.switch{display:flex;align-items:flex-start;gap:10px;padding:12px;border:1px solid #d0d5dd;border-radius:9px;background:#fcfcfd}.switch input{margin-top:4px}.switch strong,.switch span{display:block}.switch span{font-size:13px;color:#667085}.fields{display:grid;grid-template-columns:1fr 120px;gap:12px}.fields .wide{grid-column:1/-1}.fields label{display:grid;gap:5px;color:#475467;font-weight:600}.fields input{width:100%%;border:1px solid #d0d5dd;border-radius:8px;padding:9px 10px;font:inherit}.fields input:focus{border-color:#84adff;outline:0;box-shadow:0 0 0 3px #2e90fa18}.save{justify-self:start;border:0;border-radius:8px;padding:10px 15px;background:#175cd3;color:#fff;font-weight:700;cursor:pointer}.notice{padding:11px 13px;border-radius:9px;margin-bottom:14px}.notice.ok{background:#ecfdf3;color:#027a48}.notice.error{background:#fef3f2;color:#b42318}.warning{padding:11px 13px;border-radius:9px;background:#fffaeb;color:#7a2e0e}.credential{font-size:12px;color:#667085;margin:0}@media(max-width:640px){.wrap{padding-top:18px}.fields{grid-template-columns:1fr}header{align-items:flex-start;flex-direction:column}}</style></head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1><p class=\"sub\">账号注册与邮箱验证设置</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab on\" href=\"/?tab=registration\">注册设置</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab\" href=\"/?tab=risk\">风险管理</a></nav>");
    vm_mock_admin_text_appendf(&page,
        "<style>html{scroll-behavior:smooth;scrollbar-gutter:stable}body{min-height:100vh;overflow-y:scroll}form.registration-settings-card{display:flex;flex-direction:column;gap:0;min-height:0;max-height:calc(100vh - 196px);padding:0;overflow:hidden;scroll-margin-top:16px}#admin-spa-content>form.registration-settings-card{flex:1 1 auto;max-height:none;min-height:0;margin:0}.registration-settings-scroll{flex:1 1 auto;min-height:0;overflow-y:auto;overflow-x:hidden;overscroll-behavior:contain;scrollbar-gutter:stable;scrollbar-width:thin;scrollbar-color:#98a2b3 #f2f4f7}.registration-settings-scroll::-webkit-scrollbar{width:11px}.registration-settings-scroll::-webkit-scrollbar-track{background:#f2f4f7}.registration-settings-scroll::-webkit-scrollbar-thumb{background:#98a2b3;border:3px solid #f2f4f7;border-radius:99px}.registration-settings-scroll:focus-visible{outline:3px solid #2e90fa40;outline-offset:-3px}.registration-settings-scroll>section{padding:20px}.registration-settings-scroll>section+section{border-top:1px solid #eaecf0}.registration-settings-actions{display:flex;align-items:center;justify-content:flex-end;gap:12px;flex:none;padding:14px 20px;border-top:1px solid #eaecf0;background:#fff;box-shadow:0 -6px 16px #10182808}.switch{align-items:center;gap:14px;min-height:82px;padding:16px 18px;border-color:#d0d5dd;cursor:pointer;transition:border-color .15s,background .15s}.switch:hover{border-color:#98a2b3;background:#f8faff}.switch input[type=checkbox]{-webkit-appearance:none;appearance:none;position:relative;flex:0 0 auto;width:48px;height:28px;margin:0;border:1px solid #98a2b3;border-radius:999px;background:#d0d5dd;cursor:pointer;transition:background .15s,border-color .15s}.switch input[type=checkbox]::after{content:'';position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%%;background:#fff;box-shadow:0 1px 3px #10182840;transition:transform .15s}.switch input[type=checkbox]:checked{background:#175cd3;border-color:#175cd3}.switch input[type=checkbox]:checked::after{transform:translateX(20px)}.switch input[type=checkbox]:focus-visible{outline:3px solid #2e90fa40;outline-offset:2px}.switch strong{font-size:15px;color:#1d2939;margin-bottom:2px}.fields{grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:14px;margin-top:16px}.fields .wide{grid-column:1/-1}.fields textarea{width:100%%;min-height:188px;max-height:min(52vh,460px);resize:vertical;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable;scrollbar-width:thin;scrollbar-color:#98a2b3 #f2f4f7;border:1px solid #d0d5dd;border-radius:8px;padding:10px;font:14px/1.6 ui-monospace,SFMono-Regular,Consolas,monospace}.fields textarea::-webkit-scrollbar{width:10px}.fields textarea::-webkit-scrollbar-track{background:#f2f4f7;border-radius:8px}.fields textarea::-webkit-scrollbar-thumb{background:#98a2b3;border:2px solid #f2f4f7;border-radius:8px}.fields textarea:focus{border-color:#84adff;outline:0;box-shadow:0 0 0 3px #2e90fa18}.fields .check-line{display:flex;align-items:center;gap:10px;font-weight:500;cursor:pointer}.fields .check-line input[type=checkbox]{width:20px;height:20px;margin:0;accent-color:#175cd3}.mail-template .hint{max-width:720px}.template-token{display:inline-block;padding:1px 6px;border-radius:5px;background:#eff4ff;color:#1849a9;font:600 12px ui-monospace,SFMono-Regular,Consolas,monospace}.save{margin:0}@media(max-width:640px){form.registration-settings-card{max-height:none;overflow:visible}.registration-settings-scroll{overflow:visible}.registration-settings-scroll>section{padding:16px}.registration-settings-actions{padding:14px 16px}.switch{min-height:92px;padding:14px}.fields{grid-template-columns:1fr}.fields textarea{max-height:46vh}}</style>");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (!vm_mock_registration_load_settings(&settings) ||
        !vm_mock_registration_load_smtp_config(&smtp) ||
        !vm_mock_registration_load_email_template(&emailTemplate))
    {
        vm_mock_admin_text_appendf(&page,
            "<section class=\"card\"><div class=\"warning\">注册设置数据暂不可用，请检查 MySQL 连接和迁移。</div></section></main></body></html>");
        return;
    }
    vm_mock_admin_text_appendf(&page,
        "<form class=\"card registration-settings-card\" method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-registration-settings\">"
        "<div class=\"registration-settings-scroll\" tabindex=\"0\" aria-label=\"注册设置内容\">"
        "<section><h2>账号注册</h2><p class=\"hint\">网页注册始终要求邮箱验证码；验证码验证成功后，邮箱会唯一绑定到该游戏账号。</p>"
        "<label class=\"switch\"><input type=\"checkbox\" name=\"allow_game_auto_account\" value=\"1\"%s><span><strong>允许游戏内自动分配账号</strong><span>关闭后，未填写账号和密码的游戏客户端会被拒绝，不会再创建 guest 账号。</span></span></label></section>"
        "<section><h2>SMTP 邮件服务器</h2><p class=\"hint\">启用后，网页注册会把 6 位验证码发送到用户邮箱。账号与密码留空会保持现有值。</p>"
        "<label class=\"switch\"><input type=\"checkbox\" name=\"smtp_enabled\" value=\"1\"%s><span><strong>启用邮箱验证码发送</strong><span>未完成 SMTP 配置时，网页注册会明确拒绝创建账号。</span></span></label>"
        "<div class=\"fields\"><label>SMTP 主机<input name=\"smtp_host\" maxlength=\"255\" value=\"",
        settings.allowGameAutoAccount ? " checked" : "",
        smtp.enabled ? " checked" : "");
    vm_mock_admin_text_append_html(&page, smtp.host);
    vm_mock_admin_text_appendf(&page,
        "\"></label><label>端口<input name=\"smtp_port\" inputmode=\"numeric\" value=\"%u\"></label>"
        "<label class=\"wide\">发件邮箱<input type=\"email\" name=\"smtp_sender_email\" maxlength=\"254\" value=\"",
        smtp.port);
    vm_mock_admin_text_append_html(&page, smtp.senderEmail);
    vm_mock_admin_text_appendf(&page,
        "\"></label><label>SMTP 用户名（可选）<input name=\"smtp_username\" maxlength=\"255\" autocomplete=\"off\" value=\"");
    vm_mock_admin_text_append_html(&page, smtp.username);
    vm_mock_admin_text_appendf(&page,
        "\"></label><label>SMTP 密码<input type=\"password\" name=\"smtp_password\" maxlength=\"255\" autocomplete=\"new-password\" placeholder=\"留空保持不变\"></label>"
        "<label class=\"wide check-line\"><input type=\"checkbox\" name=\"clear_smtp_password\" value=\"1\"><span>清除已保存的 SMTP 密码</span></label></div>"
        "<p class=\"credential\">此服务内置的是明文 SMTP relay 客户端。生产环境请把它连接到本机或内网中已加密转发的 SMTP relay；不要把账号密码直接发送到公网的非加密端口。阿里云 ECS 默认限制 <code>smtpdm.aliyun.com:25</code>，请改用符合云网络策略的安全 relay。</p></section>"
        "<section class=\"mail-template\"><h2>注册验证码邮件</h2><p class=\"hint\">主题和内容会原样发送给注册用户。请在邮件内容中保留 <span class=\"template-token\">{{code}}</span>，发送时会自动替换为 6 位验证码。</p>"
        "<div class=\"fields\"><label class=\"wide\">邮件主题<input name=\"registration_email_subject\" maxlength=\"255\" required value=\"");
    vm_mock_admin_text_append_html(&page, emailTemplate.subject);
    vm_mock_admin_text_appendf(&page,
        "\"></label><label class=\"wide\">邮件内容<textarea name=\"registration_email_body\" maxlength=\"4096\" required spellcheck=\"false\">");
    vm_mock_admin_text_append_html(&page, emailTemplate.body);
    vm_mock_admin_text_appendf(&page,
        "</textarea></label></div></section></div><footer class=\"registration-settings-actions\"><button class=\"save\" type=\"submit\">保存注册设置</button></footer></form></main></body></html>");
    if (page.truncated)
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>注册设置页面超过大小限制。</p>");
}

/* This script is deliberately served as a same-origin resource.  The account
 * centre's CSP forbids inline scripts and inline event handlers. */
static const char g_vm_mock_registration_browser_script[] =
    "(()=>{"
    "const form=document.getElementById('registration-form'),email=document.getElementById('registration-email'),send=document.getElementById('registration-code-send'),dialog=document.getElementById('registration-captcha-dialog'),image=document.getElementById('registration-captcha-image'),code=document.getElementById('registration-captcha-code'),status=document.getElementById('captcha-status'),error=document.getElementById('registration-captcha-error'),cancel=document.querySelector('[data-registration-captcha-cancel]'),confirm=document.querySelector('[data-registration-captcha-confirm]');"
    "if(!form||!email||!send||!dialog||!image||!code||!status||!error||!cancel||!confirm||form.dataset.registrationCaptchaBound==='1')return;form.dataset.registrationCaptchaBound='1';let token='';"
    "const fail=text=>{status.textContent=text||'图片验证码暂不可用，请稍后重试';};"
    "const showSvg=markup=>{const parsed=new DOMParser().parseFromString(markup,'image/svg+xml'),svg=parsed.documentElement;if(!svg||svg.nodeName.toLowerCase()!=='svg')throw new Error('图片验证码加载失败，请重试');image.replaceChildren(document.importNode(svg,true));};"
    "const open=async()=>{if(!email.checkValidity()){email.reportValidity();return;}send.disabled=true;status.textContent='';try{const result=await fetch('/user/register/captcha/new',{method:'POST',credentials:'same-origin',cache:'no-store',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'email='+encodeURIComponent(email.value)}),payload=await result.json();if(!result.ok||!payload.token||!payload.svg)throw new Error(payload.error||'图片验证码暂不可用，请稍后重试');token=payload.token;showSvg(payload.svg);code.value='';error.textContent='';if(typeof dialog.showModal!=='function')throw new Error('当前浏览器不支持图片验证码窗口');dialog.showModal();code.focus();}catch(problem){fail(problem.message);}finally{send.disabled=false;}};"
    "const close=()=>dialog.close();"
    "const submit=()=>{if(!token){error.textContent='请刷新图片验证码后重试';return;}if(!code.checkValidity()){code.reportValidity();return;}form.elements.captcha_token.value=token;form.elements.captcha_code.value=code.value;form.action='/user/register/code';form.submit();};"
    "send.addEventListener('click',open);cancel.addEventListener('click',close);confirm.addEventListener('click',submit);code.addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();submit();}});"
    "})();";
