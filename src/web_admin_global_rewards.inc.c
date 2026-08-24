enum
{
    VM_MOCK_ADMIN_GLOBAL_REWARD_ITEM_MAX = 12,
    VM_MOCK_ADMIN_GLOBAL_REWARD_LIST_MAX = 100,
    VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX = 64,
    VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP = 64
};

typedef struct
{
    u32 itemId;
    u32 count;
} vm_mock_admin_global_reward_item;

typedef struct
{
    u32 mailId;
    u32 status;
    u32 recipientCount;
    u32 claimedCount;
    char title[64];
    char body[256];
    char itemSummary[1024];
    bool invalid;
} vm_mock_admin_global_reward_row;

typedef struct
{
    vm_mock_admin_global_reward_row rows[VM_MOCK_ADMIN_GLOBAL_REWARD_LIST_MAX];
    u32 count;
    bool invalid;
} vm_mock_admin_global_reward_list;

static bool vm_mock_admin_global_reward_recipient_account_is_valid(
    const char *accountId)
{
    size_t length = accountId != NULL ? strlen(accountId) : 0;

    if (length < 4 || length > 32)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)accountId[i];

        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_'))
        {
            return false;
        }
    }
    return true;
}

static bool vm_mock_admin_global_reward_recipient_delimiter(unsigned char ch)
{
    return ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\r' ||
           ch == '\n';
}

static bool vm_mock_admin_global_reward_parse_recipients(
    const char *text,
    char recipients[][VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP],
    u32 recipientCap, u32 *recipientCountOut, const char **errorOut)
{
    const char *cursor = text != NULL ? text : "";
    char accountId[VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP];
    size_t accountLength = 0;
    u32 recipientCount = 0;
    bool sawAccountText = false;

    if (recipientCountOut != NULL)
        *recipientCountOut = 0;
    if (errorOut != NULL)
        *errorOut = "指定账号格式无效";
    if (recipients == NULL || recipientCap == 0)
        return false;
    memset(recipients, 0,
           (size_t)recipientCap *
               VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP);
    if (cursor[0] == 0)
    {
        if (errorOut != NULL)
            *errorOut = NULL;
        return true;
    }
    for (;;)
    {
        unsigned char ch = (unsigned char)*cursor;

        if (ch == 0 || vm_mock_admin_global_reward_recipient_delimiter(ch))
        {
            if (accountLength != 0)
            {
                bool duplicate = false;

                accountId[accountLength] = 0;
                sawAccountText = true;
                if (!vm_mock_admin_global_reward_recipient_account_is_valid(
                        accountId))
                {
                    if (errorOut != NULL)
                        *errorOut = "账号只能使用 4-32 位字母、数字或下划线";
                    return false;
                }
                for (u32 i = 0; i < recipientCount; ++i)
                {
                    if (strcmp(recipients[i], accountId) == 0)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                {
                    if (recipientCount >= recipientCap)
                    {
                        if (errorOut != NULL)
                            *errorOut = "一次最多指定 64 个账号";
                        return false;
                    }
                    snprintf(recipients[recipientCount],
                             VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP,
                             "%s", accountId);
                    ++recipientCount;
                }
                accountLength = 0;
            }
            if (ch == 0)
                break;
        }
        else
        {
            sawAccountText = true;
            if (accountLength + 1 >= sizeof(accountId))
            {
                if (errorOut != NULL)
                    *errorOut = "账号只能使用 4-32 位字母、数字或下划线";
                return false;
            }
            accountId[accountLength++] = (char)ch;
        }
        ++cursor;
    }
    if (!sawAccountText || recipientCount == 0)
    {
        if (errorOut != NULL)
            *errorOut = "请至少输入一个指定账号，或留空后发放给全服";
        return false;
    }
    if (recipientCountOut != NULL)
        *recipientCountOut = recipientCount;
    if (errorOut != NULL)
        *errorOut = NULL;
    return true;
}

static bool vm_mock_admin_global_reward_form_has_field(const char *form,
                                                        const char *key)
{
    size_t keyLength = key != NULL ? strlen(key) : 0;
    const char *cursor = form;

    if (form == NULL || keyLength == 0)
        return false;
    if (strncmp(form, "--", 2) == 0)
    {
        char marker[96];

        if (keyLength + sizeof("name=\"\"") > sizeof(marker))
            return false;
        snprintf(marker, sizeof(marker), "name=\"%s\"", key);
        return strstr(form, marker) != NULL;
    }
    while (*cursor != 0)
    {
        const char *pairEnd = strchr(cursor, '&');
        const char *equals = strchr(cursor, '=');
        size_t pairLength = pairEnd ? (size_t)(pairEnd - cursor)
                                   : strlen(cursor);

        if (equals != NULL && (size_t)(equals - cursor) == keyLength &&
            (size_t)(equals - cursor) < pairLength &&
            memcmp(cursor, key, keyLength) == 0)
        {
            return true;
        }
        if (pairEnd == NULL)
            break;
        cursor = pairEnd + 1;
    }
    return false;
}

static bool vm_mock_admin_global_reward_row_callback(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_mock_admin_global_reward_list *list =
        (vm_mock_admin_global_reward_list *)contextValue;
    vm_mock_admin_global_reward_row *row = NULL;
    size_t decodedLen = 0;

    if (list == NULL || list->count >= VM_MOCK_ADMIN_GLOBAL_REWARD_LIST_MAX ||
        columnCount != 7)
    {
        if (list != NULL)
            list->invalid = true;
        return true;
    }
    row = &list->rows[list->count];
    memset(row, 0, sizeof(*row));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->mailId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &row->status) ||
        !vm_mysql_hex_decode(values[2], lengths[2], row->title,
                             sizeof(row->title) - 1u, &decodedLen) ||
        !vm_mysql_hex_decode(values[3], lengths[3], row->body,
                             sizeof(row->body) - 1u, &decodedLen) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &row->recipientCount) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &row->claimedCount) ||
        !vm_mysql_hex_decode(values[6], lengths[6], row->itemSummary,
                             sizeof(row->itemSummary) - 1u, &decodedLen) ||
        row->mailId == 0 || row->status > 2)
    {
        list->invalid = true;
        return true;
    }
    ++list->count;
    return true;
}

static bool vm_mock_admin_global_reward_load(
    vm_mock_admin_global_reward_list *list)
{
    if (list == NULL || !vm_net_mock_mailbox_prepare_schema())
        return false;
    memset(list, 0, sizeof(*list));
    return vm_mysql_query(
               "SELECT m.mail_id,m.status,HEX(m.title),HEX(m.body),"
               "m.recipient_count,"
               "(SELECT COUNT(*) FROM account_reward_mails arm "
               "WHERE arm.mail_id=m.mail_id AND arm.claim_state=1),"
               "HEX(COALESCE((SELECT GROUP_CONCAT(CONCAT(i.item_id,'x',i.item_count) "
               "ORDER BY i.reward_order SEPARATOR ', ') "
               "FROM server_global_reward_mail_items i WHERE i.mail_id=m.mail_id),'')) "
               "FROM server_global_reward_mails m "
               "ORDER BY m.mail_id DESC LIMIT 100",
               vm_mock_admin_global_reward_row_callback, list) &&
           !list->invalid;
}

static void vm_mock_admin_redirect_global_rewards(
    vm_mock_service_socket client, const char *status, const char *message)
{
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1024];

    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=global-rewards&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_global_reward_render_nav(vm_mock_admin_text *page)
{
    vm_mock_admin_text_appendf(
        page,
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab on\" href=\"/?tab=global-rewards\">奖励邮件管理</a>"
        "<a class=\"tab\" href=\"/?tab=designations\">称号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a>"
        "<a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "<a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>");
}

static void vm_mock_admin_render_global_rewards_page(
    char *response, size_t responseCap, const char *query)
{
    vm_mock_admin_text page;
    vm_mock_admin_global_reward_list list;
    char status[16];
    char message[256];

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(&list, 0, sizeof(list));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_admin_global_reward_load(&list);

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 奖励邮件管理</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1280px;margin:auto;padding:24px 18px}.head{display:flex;justify-content:space-between;gap:16px}.head h1{margin:0}.sub{color:#667085;margin:4px 0 16px}.tabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:14px}.tab{padding:8px 12px;border:1px solid #e4e7ec;border-radius:7px;background:#fff;color:#475467;text-decoration:none}.tab.on{background:#175cd3;color:#fff}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;margin-bottom:16px}.notice{padding:10px 12px;border-radius:7px;margin-bottom:13px}.notice.ok{background:#ecfdf3;color:#027a48}.notice.error{background:#fef3f2;color:#b42318}.fields{display:grid;grid-template-columns:1fr 1fr;gap:12px}.field{display:grid;gap:4px}.wide{grid-column:1/-1}.items{display:grid;gap:8px}.reward-item{display:grid;grid-template-columns:44px minmax(260px,1fr) 140px 76px;gap:10px;align-items:end;padding:10px;border:1px solid #dbe3ef;border-radius:7px;background:#f8fafc}.reward-item[hidden]{display:none}.reward-order{align-self:center;text-align:center;color:#667085;font-weight:600}.reward-item .field,.reward-item .item-field{min-width:0}.reward-remove{background:#fff;color:#b42318;border:1px solid #fda29b}.attachment-actions{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:10px}.add-attachment{background:#fff;color:#175cd3;border:1px solid #84adff}.add-attachment:disabled{color:#98a2b3;border-color:#d0d5dd;cursor:not-allowed}.attachment-count{color:#667085;font-size:12px}input,textarea{width:100%%;border:1px solid #d0d5dd;border-radius:7px;padding:9px}textarea{min-height:100px}.actions{display:flex;justify-content:flex-end;margin-top:14px}button{border:0;border-radius:7px;padding:9px 14px;background:#175cd3;color:#fff;cursor:pointer}.danger{background:#b42318}.mail{border-top:1px solid #eaecf0;padding:14px 0}.mail:first-of-type{border-top:0}.mail-head{display:flex;justify-content:space-between;gap:12px}.badges{display:flex;gap:6px;flex-wrap:wrap}.badge{padding:2px 8px;border-radius:999px;background:#eef4ff;color:#175cd3;font-size:12px}.badge.revoked{background:#f2f4f7;color:#667085}.body{white-space:pre-wrap;color:#475467}.items-text{color:#344054}.hint{color:#667085;font-size:12px}.logout{background:#fff;color:#475467;border:1px solid #d0d5dd}@media(max-width:850px){.fields{grid-template-columns:1fr}.wide{grid-column:auto}.reward-item{grid-template-columns:36px minmax(0,1fr) 110px}.reward-remove{grid-column:2/-1;justify-self:end}.attachment-actions{align-items:flex-start;flex-direction:column}}</style>"
        "<script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header class=\"head\"><div><h1>江湖 OL 后台管理</h1><p class=\"sub\">奖励邮件管理 · 全服或指定账号 · 多物品系统邮件</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_global_reward_render_nav(&page);
    if (message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>发放奖励</h2><p class=\"hint\">指定账号留空时，立即给当前拥有角色的全部账号各创建一封收件；填写账号时仅向指定账号发放。多个账号可用逗号、分号、空格或换行分隔，重复账号会自动去重。指定账号必须已存在且拥有角色。同账号任一角色领取后不可重复领取。至少一个附件。撤回仅关闭未领取收件，已领取物品不会删除。</p>"
        "<form method=\"post\" action=\"/action\" data-global-reward-form><input type=\"hidden\" name=\"action\" value=\"send-global-reward\"><div class=\"fields\"><label class=\"field\"><span>邮件标题</span><input name=\"title\" maxlength=\"30\" required></label><label class=\"field wide\"><span>邮件正文</span><textarea name=\"body_text\" maxlength=\"120\" required></textarea></label><label class=\"field wide\"><span>指定账号（可选）</span><textarea name=\"recipient_accounts\" maxlength=\"4095\" placeholder=\"留空即全服；多个账号用逗号、空格或换行分隔，最多 64 个\"></textarea></label></div><h3>附件</h3><div class=\"items\" id=\"global-reward-items\">");
    for (u32 i = 0; i < VM_MOCK_ADMIN_GLOBAL_REWARD_ITEM_MAX; ++i)
    {
        char pickerId[48];
        char fieldName[48];
        snprintf(pickerId, sizeof(pickerId), "global-reward-item-%u", i);
        snprintf(fieldName, sizeof(fieldName), "reward_item_%u", i);
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"reward-item\" data-global-reward-row=\"%u\"%s>"
            "<span class=\"reward-order\">#%u</span>",
            i, i == 0 ? "" : " hidden", i + 1u);
        vm_mock_admin_render_item_picker_field(&page, pickerId, fieldName,
                                               "选择物品", 0, false);
        vm_mock_admin_text_appendf(
            &page,
            "<label class=\"field\"><span>数量</span><input type=\"number\" name=\"reward_count_%u\" min=\"0\" max=\"4294967295\" value=\"0\" data-global-reward-count></label>"
            "<button class=\"reward-remove\" type=\"button\" data-global-reward-remove>移除</button></div>",
            i);
    }
    vm_mock_admin_text_appendf(
        &page,
        "</div><div class=\"attachment-actions\"><button class=\"add-attachment\" id=\"global-reward-add-item\" data-global-reward-add type=\"button\">添加附件</button>"
        "<span class=\"attachment-count\" id=\"global-reward-item-count\" data-global-reward-item-count>已添加 1 / %u</span></div>"
        "<div class=\"actions\"><button type=\"submit\">立即发放奖励</button></div></form></section>",
        VM_MOCK_ADMIN_GLOBAL_REWARD_ITEM_MAX);
    vm_mock_admin_render_item_picker_modal(&page, true);
    vm_mock_admin_text_appendf(&page, "<section class=\"card\"><h2>历史奖励邮件</h2>");
    if (list.count == 0)
        vm_mock_admin_text_appendf(&page, "<p class=\"hint\">暂无奖励邮件。</p>");
    for (u32 i = 0; i < list.count; ++i)
    {
        vm_mock_admin_global_reward_row *row = &list.rows[i];
        char titleUtf8[192];
        char bodyUtf8[768];
        memset(titleUtf8, 0, sizeof(titleUtf8));
        memset(bodyUtf8, 0, sizeof(bodyUtf8));
        vm_net_mock_gbk_label_to_utf8(row->title, titleUtf8, sizeof(titleUtf8));
        vm_net_mock_gbk_label_to_utf8(row->body, bodyUtf8, sizeof(bodyUtf8));
        vm_mock_admin_text_appendf(&page, "<article class=\"mail\"><div class=\"mail-head\"><div><strong>#%u · ", row->mailId);
        vm_mock_admin_text_append_html(&page, titleUtf8);
        vm_mock_admin_text_appendf(&page, "</strong><div class=\"badges\"><span class=\"badge%s\">%s</span><span class=\"badge\">收件账号 %u</span><span class=\"badge\">已领取账号 %u</span><span class=\"badge\">未领取账号 %u</span></div></div>", row->status == 2 ? " revoked" : "", row->status == 1 ? "已发放" : (row->status == 2 ? "已撤回" : "草稿"), row->recipientCount, row->claimedCount, row->recipientCount >= row->claimedCount ? row->recipientCount - row->claimedCount : 0);
        if (row->status == 1 && row->recipientCount > row->claimedCount)
            vm_mock_admin_text_appendf(&page, "<form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"revoke-global-reward\"><input type=\"hidden\" name=\"mail_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">撤回未领取奖励</button></form>", row->mailId);
        vm_mock_admin_text_appendf(&page, "</div><p class=\"body\">");
        vm_mock_admin_text_append_html(&page, bodyUtf8);
        vm_mock_admin_text_appendf(&page, "</p><p class=\"items-text\">附件：");
        vm_mock_admin_text_append_html(&page, row->itemSummary[0] ? row->itemSummary : "—");
        vm_mock_admin_text_appendf(&page, "</p></article>");
    }
    vm_mock_admin_text_appendf(&page, "</section></main></body></html>");
}

static bool vm_mock_admin_global_reward_send(
    const char *titleGbk, const char *bodyGbk,
    const vm_mock_admin_global_reward_item *items, u32 itemCount,
    const char recipientAccounts[][VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP],
    u32 recipientAccountCount,
    u32 *mailIdOut, u32 *recipientCountOut, const char **errorOut)
{
    char titleHex[128];
    char bodyHex[512];
    char query[2048];
    vm_mock_mysql_u32_context idContext;
    vm_mock_mysql_u32_context countContext;
    bool transactionStarted = false;

    if (mailIdOut != NULL)
        *mailIdOut = 0;
    if (recipientCountOut != NULL)
        *recipientCountOut = 0;
    if (errorOut != NULL)
        *errorOut = "奖励发放失败";
    if (titleGbk == NULL || bodyGbk == NULL || titleGbk[0] == 0 ||
        bodyGbk[0] == 0 || items == NULL || itemCount == 0 ||
        recipientAccountCount > VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX ||
        (recipientAccountCount != 0 && recipientAccounts == NULL) ||
        !vm_net_mock_mailbox_prepare_schema() ||
        vm_mysql_hex_encode(titleGbk, strlen(titleGbk), titleHex,
                            sizeof(titleHex)) == 0 ||
        vm_mysql_hex_encode(bodyGbk, strlen(bodyGbk), bodyHex,
                            sizeof(bodyHex)) == 0 ||
        !vm_mysql_exec("START TRANSACTION"))
    {
        return false;
    }
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "INSERT INTO server_global_reward_mails(title,body,status) "
             "VALUES(X'%s',X'%s',0)", titleHex, bodyHex);
    memset(&idContext, 0, sizeof(idContext));
    if (!vm_mysql_exec(query) ||
        !vm_mysql_query("SELECT LAST_INSERT_ID()",
                        vm_mock_mysql_single_u32_row, &idContext) ||
        idContext.invalid || !idContext.found || idContext.value == 0 ||
        idContext.value > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)
    {
        goto failed;
    }
    for (u32 i = 0; i < itemCount; ++i)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_global_reward_mail_items"
                 "(mail_id,reward_order,item_id,item_count) VALUES(%u,%u,%u,%u)",
                 idContext.value, i, items[i].itemId, items[i].count);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    if (recipientAccountCount == 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO account_reward_mails(mail_id,account_id) "
                 "SELECT DISTINCT %u,ar.account_id FROM account_roles ar JOIN accounts a "
                 "ON a.account_id=ar.account_id",
                 idContext.value);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    else
    {
        for (u32 i = 0; i < recipientAccountCount; ++i)
        {
            char accountHex[
                VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP * 2u + 1u];
            vm_mock_mysql_u32_context roleContext;

            memset(accountHex, 0, sizeof(accountHex));
            memset(&roleContext, 0, sizeof(roleContext));
            if (!vm_mock_admin_global_reward_recipient_account_is_valid(
                    recipientAccounts[i]) ||
                vm_mysql_hex_encode(recipientAccounts[i],
                                    strlen(recipientAccounts[i]), accountHex,
                                    sizeof(accountHex)) == 0)
            {
                if (errorOut != NULL)
                    *errorOut = "指定账号格式无效";
                goto failed;
            }
            snprintf(query, sizeof(query),
                     "SELECT COUNT(*) FROM account_roles "
                     "WHERE account_id=CAST(X'%s' AS CHAR)",
                     accountHex);
            if (!vm_mysql_query(query, vm_mock_mysql_single_u32_row,
                                &roleContext) ||
                roleContext.invalid || !roleContext.found ||
                roleContext.value == 0)
            {
                if (errorOut != NULL)
                    *errorOut = "指定账号不存在或尚未创建角色";
                goto failed;
            }
            snprintf(query, sizeof(query),
                     "INSERT INTO account_reward_mails(mail_id,account_id) "
                     "VALUES(%u,CAST(X'%s' AS CHAR))",
                     idContext.value, accountHex);
            if (!vm_mysql_exec(query))
                goto failed;
        }
    }
    memset(&countContext, 0, sizeof(countContext));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_reward_mails WHERE mail_id=%u",
             idContext.value);
    if (!vm_mysql_query(query, vm_mock_mysql_single_u32_row, &countContext) ||
        countContext.invalid || !countContext.found || countContext.value == 0)
    {
        if (errorOut != NULL)
            *errorOut = "当前没有可接收奖励的账号";
        goto failed;
    }
    snprintf(query, sizeof(query),
             "UPDATE server_global_reward_mails SET status=1,recipient_count=%u,"
             "sent_at=CURRENT_TIMESTAMP WHERE mail_id=%u AND status=0",
             countContext.value, idContext.value);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    if (mailIdOut != NULL)
        *mailIdOut = idContext.value;
    if (recipientCountOut != NULL)
        *recipientCountOut = countContext.value;
    if (errorOut != NULL)
        *errorOut = NULL;
    printf("[info][mock-admin] global_reward_send mail=%u recipient_scope=%s recipient_accounts=%u items=%u action=committed\n",
           idContext.value, recipientAccountCount == 0 ? "all" : "selected",
           countContext.value, itemCount);
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    return false;
}

static bool vm_mock_admin_global_reward_revoke(u32 mailId,
                                                const char **errorOut)
{
    char query[1024];
    vm_mock_mysql_u32_context stateContext;
    bool transactionStarted = false;

    if (errorOut != NULL)
        *errorOut = "奖励撤回失败";
    if (mailId == 0 || !vm_net_mock_mailbox_prepare_schema() ||
        !vm_mysql_exec("START TRANSACTION"))
    {
        return false;
    }
    transactionStarted = true;
    memset(&stateContext, 0, sizeof(stateContext));
    snprintf(query, sizeof(query),
             "SELECT status FROM server_global_reward_mails WHERE mail_id=%u FOR UPDATE",
             mailId);
    if (!vm_mysql_query(query, vm_mock_mysql_single_u32_row, &stateContext) ||
        stateContext.invalid || !stateContext.found || stateContext.value != 1)
    {
        if (errorOut != NULL)
            *errorOut = "邮件不存在或已经撤回";
        goto failed;
    }
    snprintf(query, sizeof(query),
             "UPDATE account_reward_mails SET claim_state=2 "
             "WHERE mail_id=%u AND claim_state=0", mailId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "UPDATE server_global_reward_mails SET status=2,"
             "revoked_at=CURRENT_TIMESTAMP WHERE mail_id=%u AND status=1",
             mailId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    if (errorOut != NULL)
        *errorOut = NULL;
    printf("[info][mock-admin] global_reward_revoke mail=%u action=unclaimed-only\n",
           mailId);
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    return false;
}

static void vm_mock_admin_handle_global_reward_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    const char *error = NULL;

    if (strcmp(action, "revoke-global-reward") == 0)
    {
        u32 mailId = 0;
        if (!vm_mock_admin_form_u32(body, "mail_id",
                                    VM_NET_MOCK_NPC_SERVICE_VALUE_MASK,
                                    &mailId) || mailId == 0 ||
            !vm_mock_admin_global_reward_revoke(mailId, &error))
        {
            vm_mock_admin_redirect_global_rewards(
                client, "error", error ? error : "奖励撤回失败");
            return;
        }
        vm_mock_admin_redirect_global_rewards(
            client, "ok", "未领取奖励已经撤回；已领取物品保持不变");
        return;
    }
    if (strcmp(action, "send-global-reward") == 0)
    {
        char titleUtf8[256];
        char bodyUtf8[768];
        char titleGbk[64];
        char bodyGbk[256];
        char recipientText[4096];
        char recipientAccounts[VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX]
                              [VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_ACCOUNT_CAP];
        vm_mock_admin_global_reward_item items[VM_MOCK_ADMIN_GLOBAL_REWARD_ITEM_MAX];
        u32 itemCount = 0;
        u32 recipientAccountCount = 0;
        u32 mailId = 0;
        u32 recipientCount = 0;
        char message[256];

        memset(titleUtf8, 0, sizeof(titleUtf8));
        memset(bodyUtf8, 0, sizeof(bodyUtf8));
        memset(titleGbk, 0, sizeof(titleGbk));
        memset(bodyGbk, 0, sizeof(bodyGbk));
        memset(recipientText, 0, sizeof(recipientText));
        memset(recipientAccounts, 0, sizeof(recipientAccounts));
        memset(items, 0, sizeof(items));
        if (!vm_mock_admin_form_value(body, "title", titleUtf8,
                                      sizeof(titleUtf8)) ||
            !vm_mock_admin_form_value(body, "body_text", bodyUtf8,
                                      sizeof(bodyUtf8)) ||
            !vm_mock_admin_utf8_to_gbk_text(titleUtf8, titleGbk,
                                            sizeof(titleGbk), false) ||
            !vm_mock_admin_utf8_to_gbk_text(bodyUtf8, bodyGbk,
                                            sizeof(bodyGbk), false) ||
            titleGbk[0] == 0 || bodyGbk[0] == 0)
        {
            vm_mock_admin_redirect_global_rewards(
                client, "error", "邮件标题或正文无效，或超过客户端 GBK 长度上限");
            return;
        }
        if (vm_mock_admin_global_reward_form_has_field(
                body, "recipient_accounts") &&
            !vm_mock_admin_form_value(body, "recipient_accounts",
                                      recipientText, sizeof(recipientText)))
        {
            vm_mock_admin_redirect_global_rewards(
                client, "error", "指定账号字段无效或长度超限");
            return;
        }
        if (!vm_mock_admin_global_reward_parse_recipients(
                recipientText, recipientAccounts,
                VM_MOCK_ADMIN_GLOBAL_REWARD_RECIPIENT_MAX,
                &recipientAccountCount, &error))
        {
            vm_mock_admin_redirect_global_rewards(
                client, "error", error ? error : "指定账号格式无效");
            return;
        }
        for (u32 slot = 0; slot < VM_MOCK_ADMIN_GLOBAL_REWARD_ITEM_MAX; ++slot)
        {
            char itemField[48];
            char countField[48];
            u32 itemId = 0;
            u32 count = 0;
            snprintf(itemField, sizeof(itemField), "reward_item_%u", slot);
            snprintf(countField, sizeof(countField), "reward_count_%u", slot);
            if (!vm_mock_admin_form_u32(body, itemField, 0xffffffffu, &itemId) ||
                !vm_mock_admin_form_u32(body, countField, 0xffffffffu, &count))
            {
                vm_mock_admin_redirect_global_rewards(
                    client, "error", "奖励附件字段不完整或数量越界");
                return;
            }
            if (itemId == 0 && count == 0)
                continue;
            if (itemId == 0 || count == 0 ||
                vm_net_mock_find_shop_catalog_item(itemId) == NULL)
            {
                vm_mock_admin_redirect_global_rewards(
                    client, "error", "每条附件必须同时选择有效物品并填写数量");
                return;
            }
            if ((vm_net_mock_find_equipment_catalog_item(itemId) != NULL ||
                 vm_net_mock_backpack_item_id_uses_reservoir_count(itemId)) &&
                count > 255u)
            {
                vm_mock_admin_redirect_global_rewards(
                    client, "error", "非堆叠实例单封邮件最多发放 255 个");
                return;
            }
            items[itemCount].itemId = itemId;
            items[itemCount].count = count;
            ++itemCount;
        }
        if (itemCount == 0 ||
            !vm_mock_admin_global_reward_send(titleGbk, bodyGbk, items,
                                              itemCount, recipientAccounts,
                                              recipientAccountCount, &mailId,
                                              &recipientCount, &error))
        {
            vm_mock_admin_redirect_global_rewards(
                client, "error", error ? error : "奖励发放失败");
            return;
        }
        if (recipientAccountCount == 0)
        {
            snprintf(message, sizeof(message),
                     "奖励邮件 #%u 已向全服 %u 个账号发放，共 %u 种附件",
                     mailId, recipientCount, itemCount);
        }
        else
        {
            snprintf(message, sizeof(message),
                     "奖励邮件 #%u 已向 %u 个指定账号发放，共 %u 种附件",
                     mailId, recipientCount, itemCount);
        }
        vm_mock_admin_redirect_global_rewards(client, "ok", message);
        return;
    }
    vm_mock_admin_redirect_global_rewards(client, "error", "未知奖励管理操作");
}
