#include "mock_server.h"

enum
{
    VM_NET_MOCK_MAILBOX_PAGE_ITEMS = 5,
    VM_NET_MOCK_MAILBOX_LIST_MAX = 64
};

typedef struct
{
    u32 mailId;
    u8 claimState;
    char title[64];
} vm_net_mock_mailbox_list_row;

typedef struct
{
    vm_net_mock_mailbox_list_row rows[VM_NET_MOCK_MAILBOX_LIST_MAX];
    u32 count;
    bool invalid;
} vm_net_mock_mailbox_list;

typedef struct
{
    u32 itemId;
    u32 count;
} vm_net_mock_mail_reward_item;

typedef struct
{
    u32 mailId;
    u8 status;
    u8 claimState;
    char title[64];
    char body[256];
    vm_net_mock_mail_reward_item items[VM_NET_MOCK_MAIL_REWARD_MAX];
    u8 itemCount;
    bool found;
    bool invalid;
} vm_net_mock_mail_detail;

typedef struct
{
    u32 claimState;
    u32 mailStatus;
    bool found;
    bool invalid;
} vm_net_mock_mail_claim_lock;

typedef struct
{
    bool active;
    /* The recipient row is account-scoped.  roleId binds its mail transition
     * to the exact role snapshot committed by the persistence module. */
    u32 roleId;
    u32 mailId;
} vm_net_mock_mail_claim_transaction;

static vm_net_mock_mail_claim_transaction g_vm_net_mock_mail_claim_transaction;
static bool g_vm_net_mock_mailbox_schema_prepared = false;

static bool vm_net_mock_mailbox_legacy_recipient_table_exists(
    bool *existsOut)
{
    vm_mock_mysql_u32_context context;

    if (existsOut == NULL)
        return false;
    *existsOut = false;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.TABLES "
            "WHERE TABLE_SCHEMA=DATABASE() "
            "AND TABLE_NAME='account_role_reward_mails'",
            vm_mock_mysql_single_u32_row, &context) ||
        context.invalid || !context.found || context.value > 1)
    {
        return false;
    }
    *existsOut = context.value == 1;
    return true;
}

bool vm_net_mock_mailbox_prepare_schema(void)
{
    bool legacyRecipientsExist = false;

    if (g_vm_net_mock_mailbox_schema_prepared)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_global_reward_mails ("
            "mail_id INT UNSIGNED NOT NULL AUTO_INCREMENT,title VARBINARY(63) NOT NULL,"
            "body VARBINARY(255) NOT NULL,status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "recipient_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "sent_at TIMESTAMP NULL DEFAULT NULL,revoked_at TIMESTAMP NULL DEFAULT NULL,"
            "PRIMARY KEY(mail_id),KEY idx_global_reward_mails_status(status,mail_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_global_reward_mail_items ("
            "mail_id INT UNSIGNED NOT NULL,reward_order TINYINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL,item_count INT UNSIGNED NOT NULL,"
            "PRIMARY KEY(mail_id,reward_order),KEY idx_global_reward_mail_items_item(item_id),"
            "CONSTRAINT fk_global_reward_mail_items_mail FOREIGN KEY(mail_id) "
            "REFERENCES server_global_reward_mails(mail_id) ON DELETE CASCADE) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_reward_mails ("
            "mail_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "claim_state TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "claimed_at TIMESTAMP NULL DEFAULT NULL,"
            "PRIMARY KEY(mail_id,account_id),"
            "KEY idx_account_reward_mails_inbox(account_id,claim_state,mail_id),"
            "CONSTRAINT fk_account_reward_mails_mail FOREIGN KEY(mail_id) "
            "REFERENCES server_global_reward_mails(mail_id) ON DELETE CASCADE,"
            "CONSTRAINT fk_account_reward_mails_account FOREIGN KEY(account_id) "
            "REFERENCES accounts(account_id) ON DELETE CASCADE) ENGINE=InnoDB") ||
        !vm_net_mock_mailbox_legacy_recipient_table_exists(
            &legacyRecipientsExist))
    {
        printf("[error][network] mock_mailbox_schema error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (legacyRecipientsExist &&
        (!vm_mysql_exec(
             "INSERT INTO account_reward_mails"
             "(mail_id,account_id,claim_state,created_at,claimed_at) "
             "SELECT arm.mail_id,arm.account_id,"
             "CASE WHEN MAX(arm.claim_state=1)>0 THEN 1 "
             "WHEN MAX(arm.claim_state=2)>0 THEN 2 ELSE 0 END,"
             "MIN(arm.created_at),"
             "MAX(CASE WHEN arm.claim_state=1 THEN arm.claimed_at ELSE NULL END) "
             "FROM account_role_reward_mails arm JOIN accounts a "
             "ON a.account_id=arm.account_id "
             "GROUP BY arm.mail_id,arm.account_id "
             "ON DUPLICATE KEY UPDATE "
             "created_at=LEAST(account_reward_mails.created_at,VALUES(created_at)),"
             "claimed_at=COALESCE(GREATEST(account_reward_mails.claimed_at,"
             "VALUES(claimed_at)),account_reward_mails.claimed_at,VALUES(claimed_at)),"
             "claim_state=CASE WHEN account_reward_mails.claim_state=1 "
             "OR VALUES(claim_state)=1 THEN 1 "
             "WHEN account_reward_mails.claim_state=2 "
             "OR VALUES(claim_state)=2 THEN 2 ELSE 0 END") ||
         !vm_mysql_exec(
             "UPDATE server_global_reward_mails m SET recipient_count="
             "(SELECT COUNT(*) FROM account_reward_mails arm "
             "WHERE arm.mail_id=m.mail_id)")))
    {
        printf("[error][network] mock_mailbox_account_migration error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_mailbox_schema_prepared = true;
    return true;
}

static bool vm_net_mock_mail_claim_lock_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_mail_claim_lock *context =
        (vm_net_mock_mail_claim_lock *)contextValue;

    if (context == NULL || context->found || columnCount != 2 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->claimState) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &context->mailStatus))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

/* Called by vm_net_mock_role_db_save_relational immediately before COMMIT.
 * The account recipient transition and the selected role/backpack snapshot
 * therefore have one database outcome. */
bool vm_net_mock_mail_claim_commit_in_transaction(u32 scopedRoleId)
{
    vm_net_mock_mail_claim_lock lock;
    char accountHex[129];
    char query[1024];

    if (!g_vm_net_mock_mail_claim_transaction.active)
        return true;
    if (scopedRoleId == 0 ||
        scopedRoleId != g_vm_net_mock_mail_claim_transaction.roleId ||
        !vm_net_mock_mysql_account_hex(accountHex))
    {
        return false;
    }
    memset(&lock, 0, sizeof(lock));
    snprintf(query, sizeof(query),
             "SELECT arm.claim_state,m.status FROM account_reward_mails arm "
             "JOIN server_global_reward_mails m ON m.mail_id=arm.mail_id "
             "WHERE arm.mail_id=%u AND arm.account_id=CAST(X'%s' AS CHAR) "
             "FOR UPDATE",
             g_vm_net_mock_mail_claim_transaction.mailId, accountHex);
    if (!vm_mysql_query(query, vm_net_mock_mail_claim_lock_row, &lock) ||
        !lock.found || lock.invalid || lock.claimState != 0 ||
        lock.mailStatus != 1)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE account_reward_mails SET claim_state=1,"
             "claimed_at=CURRENT_TIMESTAMP WHERE mail_id=%u "
             "AND account_id=CAST(X'%s' AS CHAR) AND claim_state=0",
             g_vm_net_mock_mail_claim_transaction.mailId, accountHex);
    return vm_mysql_exec(query);
}

static bool vm_net_mock_mailbox_list_row_callback(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_mailbox_list *context =
        (vm_net_mock_mailbox_list *)contextValue;
    vm_net_mock_mailbox_list_row *row = NULL;
    u32 claimState = 0;
    size_t decodedLen = 0;

    if (context == NULL || context->count >= VM_NET_MOCK_MAILBOX_LIST_MAX ||
        columnCount != 3)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    row = &context->rows[context->count];
    memset(row, 0, sizeof(*row));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->mailId) ||
        row->mailId == 0 || row->mailId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK ||
        !vm_mysql_hex_decode(values[1], lengths[1], row->title,
                             sizeof(row->title) - 1u, &decodedLen) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &claimState) ||
        claimState > 1)
    {
        context->invalid = true;
        return true;
    }
    row->claimState = (u8)claimState;
    ++context->count;
    return true;
}

static bool vm_net_mock_mailbox_detail_row_callback(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_mail_detail *detail = (vm_net_mock_mail_detail *)contextValue;
    u32 status = 0;
    u32 claimState = 0;
    size_t decodedLen = 0;

    if (detail == NULL || detail->found || columnCount != 4 ||
        !vm_mysql_hex_decode(values[0], lengths[0], detail->title,
                             sizeof(detail->title) - 1u, &decodedLen) ||
        !vm_mysql_hex_decode(values[1], lengths[1], detail->body,
                             sizeof(detail->body) - 1u, &decodedLen) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &status) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &claimState) ||
        status > 2 || claimState > 2)
    {
        if (detail != NULL)
            detail->invalid = true;
        return true;
    }
    detail->status = (u8)status;
    detail->claimState = (u8)claimState;
    detail->found = true;
    return true;
}

static bool vm_net_mock_mailbox_item_row_callback(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_mail_detail *detail = (vm_net_mock_mail_detail *)contextValue;
    vm_net_mock_mail_reward_item *item = NULL;

    if (detail == NULL || detail->itemCount >= VM_NET_MOCK_MAIL_REWARD_MAX ||
        columnCount != 2)
    {
        if (detail != NULL)
            detail->invalid = true;
        return true;
    }
    item = &detail->items[detail->itemCount];
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &item->itemId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &item->count) ||
        item->itemId == 0 || item->count == 0 ||
        !vm_net_mock_shop_catalog_item_exists(item->itemId))
    {
        detail->invalid = true;
        return true;
    }
    ++detail->itemCount;
    return true;
}

static bool vm_net_mock_mailbox_load_list(vm_net_mock_role_state *role,
                                          vm_net_mock_mailbox_list *list)
{
    char accountHex[129];
    char query[1024];

    if (role == NULL || list == NULL ||
        !vm_net_mock_mysql_account_hex(accountHex) ||
        !vm_net_mock_mailbox_prepare_schema())
    {
        return false;
    }
    memset(list, 0, sizeof(*list));
    snprintf(query, sizeof(query),
             "SELECT m.mail_id,HEX(m.title),arm.claim_state "
             "FROM account_reward_mails arm JOIN server_global_reward_mails m "
             "ON m.mail_id=arm.mail_id WHERE arm.account_id=CAST(X'%s' AS CHAR) "
             "AND arm.claim_state IN(0,1) AND m.status=1 "
             "ORDER BY m.mail_id DESC LIMIT %u",
             accountHex, VM_NET_MOCK_MAILBOX_LIST_MAX);
    return vm_mysql_query(query, vm_net_mock_mailbox_list_row_callback, list) &&
           !list->invalid;
}

static bool vm_net_mock_mailbox_load_detail(vm_net_mock_role_state *role,
                                            u32 mailId,
                                            vm_net_mock_mail_detail *detail)
{
    char accountHex[129];
    char query[1024];

    if (role == NULL || mailId == 0 || detail == NULL ||
        !vm_net_mock_mysql_account_hex(accountHex) ||
        !vm_net_mock_mailbox_prepare_schema())
    {
        return false;
    }
    memset(detail, 0, sizeof(*detail));
    detail->mailId = mailId;
    snprintf(query, sizeof(query),
             "SELECT HEX(m.title),HEX(m.body),m.status,arm.claim_state "
             "FROM account_reward_mails arm JOIN server_global_reward_mails m "
             "ON m.mail_id=arm.mail_id WHERE arm.mail_id=%u "
             "AND arm.account_id=CAST(X'%s' AS CHAR)",
             mailId, accountHex);
    if (!vm_mysql_query(query, vm_net_mock_mailbox_detail_row_callback, detail) ||
        !detail->found || detail->invalid)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "SELECT item_id,item_count FROM server_global_reward_mail_items "
             "WHERE mail_id=%u ORDER BY reward_order", mailId);
    return vm_mysql_query(query, vm_net_mock_mailbox_item_row_callback, detail) &&
           !detail->invalid && detail->itemCount != 0;
}

static void vm_net_mock_mailbox_append_attachments(
    char *out, u32 outCap, const vm_net_mock_mail_detail *detail)
{
    u32 used = out != NULL ? (u32)strlen(out) : 0;

    if (out == NULL || used >= outCap || detail == NULL)
        return;
    for (u8 i = 0; i < detail->itemCount && used < outCap; ++i)
    {
        const char *catalogName =
            vm_net_mock_shop_catalog_item_name(detail->items[i].itemId);
        int written = snprintf(out + used, outCap - used, "\n%s x%u",
                               catalogName != NULL ? catalogName : "?",
                               detail->items[i].count);
        if (written < 0 || (u32)written >= outCap - used)
        {
            out[outCap - 1u] = 0;
            return;
        }
        used += (u32)written;
    }
}

static bool vm_net_mock_mailbox_claim(vm_net_mock_role_state *role,
                                      const vm_net_mock_mail_detail *detail,
                                      vm_net_mock_mail_claimed_item *claimedItems,
                                      u8 *claimedItemCountOut)
{
    vm_net_mock_role_state before;
    vm_net_mock_role_state projected;
    vm_net_mock_mail_claimed_item claimed[VM_NET_MOCK_MAIL_REWARD_MAX];
    bool saved = false;

    if (claimedItemCountOut)
        *claimedItemCountOut = 0;
    if (claimedItems)
        memset(claimedItems, 0,
               sizeof(*claimedItems) * VM_NET_MOCK_MAIL_REWARD_MAX);
    if (role == NULL || detail == NULL || detail->status != 1 ||
        detail->claimState != 0 || detail->itemCount == 0 ||
        g_vm_net_mock_mail_claim_transaction.active)
    {
        return false;
    }
    before = *role;
    projected = before;
    memset(claimed, 0, sizeof(claimed));
    for (u8 i = 0; i < detail->itemCount; ++i)
    {
        if (!vm_net_mock_role_add_backpack_item_to_role_in_memory(
                &projected, detail->items[i].itemId,
                detail->items[i].count, &claimed[i].seq) ||
            claimed[i].seq == 0)
        {
            return false;
        }
        claimed[i].itemId = detail->items[i].itemId;
        claimed[i].count = detail->items[i].count;
    }
    /* The claim is irreversible after its durable save.  Preflight the exact
     * native reward object against the projected role first, so a malformed
     * attachment can never consume its mail without a client-visible item
     * manager update. */
    {
        vm_net_mock_reward15_item_row rewardRows[VM_NET_MOCK_REWARD15_MAX_ROWS];
        u8 rewardWire[VM_NET_MOCK_REWARD15_ITEMINFO_MAX_BYTES + 128];
        u32 rewardPos = 5;
        u8 rewardObjectCount = 0;

        if (detail->itemCount > VM_NET_MOCK_REWARD15_MAX_ROWS)
            return false;
        memset(rewardRows, 0, sizeof(rewardRows));
        memset(rewardWire, 0, sizeof(rewardWire));
        for (u8 i = 0; i < detail->itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item =
                vm_net_mock_role_find_backpack_item(&projected,
                                                    claimed[i].itemId,
                                                    claimed[i].seq);

            if (item == NULL)
                return false;
            rewardRows[i].item = item;
            rewardRows[i].acquiredCount = claimed[i].count;
        }
        if (!vm_net_mock_append_backpack_reward15_object(
                rewardWire, sizeof(rewardWire), &rewardPos,
                &rewardObjectCount, rewardRows, detail->itemCount) ||
            rewardObjectCount != 1)
        {
            return false;
        }
    }
    g_vm_net_mock_mail_claim_transaction.active = true;
    g_vm_net_mock_mail_claim_transaction.roleId = role->roleId;
    g_vm_net_mock_mail_claim_transaction.mailId = detail->mailId;
    *role = projected;
    saved = vm_net_mock_role_db_save("mail-reward-claim");
    memset(&g_vm_net_mock_mail_claim_transaction, 0,
           sizeof(g_vm_net_mock_mail_claim_transaction));
    if (!saved)
    {
        *role = before;
        return false;
    }
    if (claimedItems)
        memcpy(claimedItems, claimed, sizeof(claimed));
    if (claimedItemCountOut)
        *claimedItemCountOut = detail->itemCount;
    return true;
}

bool vm_net_mock_mailbox_build_dialog(
    vm_net_mock_role_state *role,
    const vm_mock_service_npc_context *serviceContext,
    u32 operation, u32 value, vm_net_mock_mailbox_dialog *view)
{
    vm_net_mock_mailbox_list list;
    vm_net_mock_mail_detail detail;
    vm_net_mock_mail_claimed_item claimedItems[VM_NET_MOCK_MAIL_REWARD_MAX];
    u8 claimedItemCount = 0;

    if (view == NULL ||
        (operation != VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX_BASE &&
         operation != VM_NET_MOCK_NPC_SERVICE_OPEN_MAIL_BASE &&
         operation != VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE))
    {
        return false;
    }
    memset(view, 0, sizeof(*view));
    view->action = "mailbox-invalid";
    snprintf(view->dialog, sizeof(view->dialog), "%s",
             "\xd3\xca\xcf\xe4\xb7\xfe\xce\xf1\xb2\xbb\xbf\xc9\xd3\xc3\xa1\xa3"); /* 邮箱服务不可用。 */
    if (role == NULL || !vm_net_mock_npc_service_context_has(
                            serviceContext, VM_NET_MOCK_NPC_KIND_MAILBOX))
    {
        return true;
    }
    if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX_BASE)
    {
        u32 page = value == 0 ? 0 : value - 1u;
        u32 pageCount = 0;
        u32 start = 0;

        view->action = "mailbox-list";
        if (!vm_net_mock_mailbox_load_list(role, &list))
            return true;
        pageCount = list.count == 0 ? 1u :
            (list.count + VM_NET_MOCK_MAILBOX_PAGE_ITEMS - 1u) /
                VM_NET_MOCK_MAILBOX_PAGE_ITEMS;
        if (page >= pageCount)
            page = pageCount - 1u;
        view->page = page;
        snprintf(view->dialog, sizeof(view->dialog),
                 "\xcf\xb5\xcd\xb3\xd3\xca\xcf\xe4 %u/%u", page + 1u, pageCount); /* 系统邮箱 */
        start = page * VM_NET_MOCK_MAILBOX_PAGE_ITEMS;
        for (u32 i = start;
             i < list.count &&
             i < start + VM_NET_MOCK_MAILBOX_PAGE_ITEMS;
             ++i)
        {
            u8 option = view->optionCount++;
            snprintf(view->optionNames[option],
                     sizeof(view->optionNames[option]), "%s%s",
                     list.rows[i].claimState == 0 ? "" : "[\xd2\xd1\xc1\xec]", /* 已领 */
                     list.rows[i].title);
            snprintf(view->optionDescriptions[option],
                     sizeof(view->optionDescriptions[option]), "%s",
                     list.rows[i].claimState == 0
                         ? "\xce\xb4\xc1\xec\xc8\xa1\xa3\xac\xb5\xe3\xbb\xf7\xb2\xe9\xbf\xb4\xb8\xbd\xbc\xfe" /* 未领取，点击查看附件 */
                         : "\xbd\xb1\xc0\xf8\xd2\xd1\xc1\xec\xc8\xa1"); /* 奖励已领取 */
            view->optionValues[option] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_MAIL_BASE | list.rows[i].mailId;
        }
        if (page != 0 &&
            view->optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
        {
            u8 option = view->optionCount++;
            snprintf(view->optionNames[option], sizeof(view->optionNames[option]),
                     "%s", "\xc9\xcf\xd2\xbb\xd2\xb3"); /* 上一页 */
            view->optionValues[option] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX_BASE | page;
        }
        if (page + 1u < pageCount &&
            view->optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
        {
            u8 option = view->optionCount++;
            snprintf(view->optionNames[option], sizeof(view->optionNames[option]),
                     "%s", "\xcf\xc2\xd2\xbb\xd2\xb3"); /* 下一页 */
            view->optionValues[option] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX_BASE | (page + 2u);
        }
        if (list.count == 0)
            snprintf(view->dialog, sizeof(view->dialog), "%s",
                     "\xd4\xdd\xce\xde\xcf\xb5\xcd\xb3\xbd\xb1\xc0\xf8\xd3\xca\xbc\xfe\xa1\xa3"); /* 暂无系统奖励邮件。 */
        return true;
    }

    view->action = operation == VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE
                       ? "mailbox-claim" : "mailbox-detail";
    if (!vm_net_mock_mailbox_load_detail(role, value, &detail))
        return true;
    if (operation == VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE)
    {
        if (vm_net_mock_mailbox_claim(role, &detail, claimedItems,
                                      &claimedItemCount))
        {
            view->result = 1;
            snprintf(view->dialog, sizeof(view->dialog), "%s",
                     "\xbd\xb1\xc0\xf8\xd2\xd1\xc1\xec\xc8\xa1\xa3\xac\xc7\xeb\xd4\xda\xb1\xb3\xb0\xfc\xd6\xd0\xb2\xe9\xbf\xb4\xa1\xa3"); /* 奖励已领取，请在背包中查看。 */
            detail.claimState = 1;
            memcpy(view->claimedItems, claimedItems, sizeof(claimedItems));
            view->claimedItemCount = claimedItemCount;
        }
        else
        {
            snprintf(view->dialog, sizeof(view->dialog), "%s",
                     "\xb1\xb3\xb0\xfc\xbf\xd5\xbc\xe4\xb2\xbb\xd7\xe3\xbb\xf2\xd3\xca\xbc\xfe\xd7\xb4\xcc\xac\xd2\xd1\xb1\xe4\xbb\xaf\xa1\xa3"); /* 背包空间不足或邮件状态已变化。 */
        }
    }
    else
    {
        snprintf(view->dialog, sizeof(view->dialog), "%s\n%s",
                 detail.title, detail.body);
        vm_net_mock_mailbox_append_attachments(view->dialog,
                                                sizeof(view->dialog), &detail);
    }
    if (detail.status == 1 && detail.claimState == 0 &&
        operation != VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE)
    {
        snprintf(view->optionNames[view->optionCount],
                 sizeof(view->optionNames[view->optionCount]), "%s",
                 "\xc1\xec\xc8\xa1\xc8\xab\xb2\xbf\xb8\xbd\xbc\xfe"); /* 领取全部附件 */
        snprintf(view->optionDescriptions[view->optionCount],
                 sizeof(view->optionDescriptions[view->optionCount]), "%s",
                 "\xb1\xb3\xb0\xfc\xbf\xd5\xbc\xe4\xb2\xbb\xd7\xe3\xca\xb1\xb2\xbb\xbb\xe1\xcf\xfb\xba\xc4\xd3\xca\xbc\xfe"); /* 背包空间不足时不会消耗邮件 */
        view->optionValues[view->optionCount++] =
            VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE | detail.mailId;
    }
    snprintf(view->optionNames[view->optionCount],
             sizeof(view->optionNames[view->optionCount]), "%s",
             "\xb7\xb5\xbb\xd8\xd3\xca\xcf\xe4"); /* 返回邮箱 */
    view->optionValues[view->optionCount++] = VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX;
    return true;
}
