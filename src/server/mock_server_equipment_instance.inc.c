/* Global equipment tracking ids (item_instance_id).
 * Included from mock_server_enhance_config.inc.c after wash/crystal helpers. */

enum
{
    VM_NET_MOCK_EQUIP_INSTANCE_LIVE_CAP = 24576
};

typedef enum
{
    VM_NET_MOCK_EQUIP_INST_LOC_EQUIP = 0,
    VM_NET_MOCK_EQUIP_INST_LOC_BAG = 1,
    VM_NET_MOCK_EQUIP_INST_LOC_WAREHOUSE = 2,
    VM_NET_MOCK_EQUIP_INST_LOC_AUCTION = 3,
    VM_NET_MOCK_EQUIP_INST_LOC_CLAIM = 4
} vm_net_mock_equip_instance_loc;

typedef struct
{
    u8 loc;
    u32 roleId;
    u32 pk;
    u32 itemId;
    u32 oldInstanceId;
    u32 newInstanceId;
    u8 keepPriority;
    u8 hasWashText;
} vm_net_mock_equip_instance_live_row;

typedef struct
{
    vm_net_mock_equip_instance_live_row *rows;
    u32 count;
    u32 cap;
    u8 loc;
} vm_net_mock_equip_instance_scan_ctx;

static bool g_vm_net_mock_equip_instance_seq_ready = false;
static bool g_vm_net_mock_equip_instance_migrate_done = false;

static bool vm_net_mock_equip_instance_u32_row(void *contextValue,
                                               unsigned int columnCount,
                                               const char *const *values,
                                               const size_t *lengths)
{
    u32 *out = (u32 *)contextValue;

    if (out == NULL || columnCount < 1 || values == NULL || values[0] == NULL)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], out))
        *out = 0;
    return true;
}

static bool vm_net_mock_equipment_instance_seq_ensure(void)
{
    char query[256];
    u32 nextId = 1;

    if (g_vm_net_mock_equip_instance_seq_ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_equipment_instance_seq ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "next_id INT UNSIGNED NOT NULL DEFAULT 1,"
            "PRIMARY KEY(id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
        return false;
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO server_equipment_instance_seq(id,next_id) "
            "VALUES(1,1)"))
        return false;

    nextId = 1;
    (void)vm_mysql_query(
        "SELECT GREATEST("
        "IFNULL((SELECT MAX(item_instance_id) FROM account_role_backpack),0),"
        "IFNULL((SELECT MAX(item_instance_id) FROM account_role_equipment),0),"
        "IFNULL((SELECT MAX(item_instance_id) FROM server_auction_listings),0),"
        "IFNULL((SELECT MAX(item_instance_id) FROM server_auction_claims),0),"
        "IFNULL((SELECT MAX(item_instance_id) FROM "
        "account_role_enhance_wash_record),0),"
        "IFNULL((SELECT MAX(item_instance_id) FROM "
        "account_role_enhance_crystal_spent),0)"
        ")",
        vm_net_mock_equip_instance_u32_row, &nextId);
    {
        u32 whMax = 0;
        (void)vm_mysql_query(
            "SELECT IFNULL(MAX(item_instance_id),0) FROM account_role_warehouse",
            vm_net_mock_equip_instance_u32_row, &whMax);
        if (whMax > nextId)
            nextId = whMax;
    }
    if (nextId < 0xffffffffu)
        nextId += 1u;
    snprintf(query, sizeof(query),
             "UPDATE server_equipment_instance_seq SET next_id=%u "
             "WHERE id=1 AND next_id<%u",
             nextId, nextId);
    (void)vm_mysql_exec(query);
    g_vm_net_mock_equip_instance_seq_ready = true;
    printf("[info][mock-service] equipment_instance_seq ready next_id>=%u\n",
           nextId);
    return true;
}

static u32 vm_net_mock_equipment_instance_alloc(void)
{
    u32 nextId = 0;

    if (!vm_net_mock_equipment_instance_seq_ensure())
        return 0;
    if (!vm_mysql_exec(
            "UPDATE server_equipment_instance_seq "
            "SET next_id=LAST_INSERT_ID(next_id+1) WHERE id=1"))
    {
        printf("[error][mock-service] equipment_instance_alloc update error=%s\n",
               vm_mysql_last_error());
        return 0;
    }
    if (!vm_mysql_query("SELECT LAST_INSERT_ID()",
                        vm_net_mock_equip_instance_u32_row, &nextId) ||
        nextId == 0)
    {
        printf("[error][mock-service] equipment_instance_alloc last_id error=%s\n",
               vm_mysql_last_error());
        return 0;
    }
    return nextId;
}

static bool vm_net_mock_equipment_instance_retire(u32 roleId, u32 instanceId,
                                                  u16 bagSeq)
{
    bool ok = true;

    if (roleId == 0)
        return false;
    if (instanceId != 0)
    {
        if (!vm_net_mock_enhance_wash_record_delete(roleId, instanceId, bagSeq))
            ok = false;
        if (!vm_net_mock_enhance_crystal_spent_delete(roleId, instanceId))
            ok = false;
    }
    else if (bagSeq != 0)
    {
        if (!vm_net_mock_enhance_wash_record_delete(roleId, 0, bagSeq))
            ok = false;
    }
    if (ok && instanceId != 0)
    {
        printf("[info][mock-service] equipment_instance_retire role=%u "
               "instance=%u seq=%u evidence=destroy-or-leave\n",
               roleId, instanceId, (u32)bagSeq);
    }
    return ok;
}

/* Move wash/crystal side tables when ownership changes (trade / auction sold). */
static bool vm_net_mock_equipment_instance_transfer_owner(u32 fromRoleId,
                                                          u32 toRoleId,
                                                          u32 instanceId,
                                                          u16 newBagSeq)
{
    char query[384];

    if (fromRoleId == 0 || toRoleId == 0 || instanceId == 0)
        return false;
    (void)vm_net_mock_enhance_wash_record_ensure_table();
    (void)vm_net_mock_enhance_crystal_spent_ensure_table();
    if (fromRoleId == toRoleId)
    {
        if (newBagSeq != 0)
        {
            snprintf(query, sizeof(query),
                     "UPDATE account_role_enhance_wash_record SET bag_seq=%u "
                     "WHERE role_id=%u AND item_instance_id=%u",
                     (u32)newBagSeq, fromRoleId, instanceId);
            (void)vm_mysql_exec(query);
        }
        return true;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_enhance_wash_record "
             "WHERE role_id=%u AND item_instance_id=%u",
             toRoleId, instanceId);
    (void)vm_mysql_exec(query);
    if (newBagSeq != 0)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_enhance_wash_record "
                 "WHERE role_id=%u AND bag_seq=%u AND item_instance_id=0",
                 toRoleId, (u32)newBagSeq);
        (void)vm_mysql_exec(query);
    }
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_enhance_crystal_spent "
             "WHERE role_id=%u AND item_instance_id=%u",
             toRoleId, instanceId);
    (void)vm_mysql_exec(query);
    snprintf(query, sizeof(query),
             "UPDATE account_role_enhance_wash_record "
             "SET role_id=%u, bag_seq=%u "
             "WHERE role_id=%u AND item_instance_id=%u",
             toRoleId, (u32)newBagSeq, fromRoleId, instanceId);
    (void)vm_mysql_exec(query);
    snprintf(query, sizeof(query),
             "UPDATE account_role_enhance_crystal_spent SET role_id=%u "
             "WHERE role_id=%u AND item_instance_id=%u",
             toRoleId, fromRoleId, instanceId);
    (void)vm_mysql_exec(query);
    printf("[info][mock-service] equipment_instance_transfer from=%u to=%u "
           "instance=%u seq=%u evidence=ownership-change\n",
           fromRoleId, toRoleId, instanceId, (u32)newBagSeq);
    return true;
}

static bool vm_net_mock_equipment_instance_retire_role(u32 roleId)
{
    char query[192];

    if (roleId == 0)
        return false;
    (void)vm_net_mock_enhance_wash_record_ensure_table();
    (void)vm_net_mock_enhance_crystal_spent_ensure_table();
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_enhance_wash_record WHERE role_id=%u",
             roleId);
    (void)vm_mysql_exec(query);
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_enhance_crystal_spent WHERE role_id=%u",
             roleId);
    (void)vm_mysql_exec(query);
    printf("[info][mock-service] equipment_instance_retire_role role=%u "
           "evidence=role-delete\n",
           roleId);
    return true;
}

static bool vm_net_mock_equip_instance_scan_row(void *contextValue,
                                                unsigned int columnCount,
                                                const char *const *values,
                                                const size_t *lengths)
{
    vm_net_mock_equip_instance_scan_ctx *ctx =
        (vm_net_mock_equip_instance_scan_ctx *)contextValue;
    vm_net_mock_equip_instance_live_row *row;

    if (ctx == NULL || ctx->rows == NULL || ctx->count >= ctx->cap ||
        columnCount < 4 || values == NULL)
        return false;
    row = &ctx->rows[ctx->count];
    memset(row, 0, sizeof(*row));
    row->loc = ctx->loc;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->roleId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &row->pk) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &row->itemId))
        return false;
    if (values[3] != NULL)
        (void)vm_mock_mysql_parse_u32(values[3], lengths[3], &row->oldInstanceId);
    row->newInstanceId = row->oldInstanceId;
    if (columnCount >= 5 && values[4] != NULL && lengths[4] > 0)
    {
        const char *w = values[4];
        size_t i;
        for (i = 0; i < lengths[4]; ++i)
        {
            if ((i == 0 || w[i - 1] == ';') && w[i] == '1')
            {
                row->hasWashText = 1;
                break;
            }
        }
    }
    /* Catalog may not be warm at authority_prepare — still remap every live
     * gear-like row.  Cultivation books reuse enhanceInstanceId as book_id. */
    if (row->itemId == VM_NET_MOCK_CULTIVATION_BOOK_ITEM_ID)
        return true;
    row->keepPriority = (u8)(ctx->loc * 10u);
    if (row->hasWashText)
        row->keepPriority = (u8)(row->keepPriority > 2 ? row->keepPriority - 2 : 0);
    ctx->count += 1;
    return true;
}

static int vm_net_mock_equip_instance_live_cmp(const void *a, const void *b)
{
    const vm_net_mock_equip_instance_live_row *ra =
        (const vm_net_mock_equip_instance_live_row *)a;
    const vm_net_mock_equip_instance_live_row *rb =
        (const vm_net_mock_equip_instance_live_row *)b;

    if (ra->oldInstanceId != rb->oldInstanceId)
        return ra->oldInstanceId < rb->oldInstanceId ? -1 : 1;
    if (ra->keepPriority != rb->keepPriority)
        return ra->keepPriority < rb->keepPriority ? -1 : 1;
    if (ra->roleId != rb->roleId)
        return ra->roleId < rb->roleId ? -1 : 1;
    if (ra->loc != rb->loc)
        return ra->loc < rb->loc ? -1 : 1;
    if (ra->pk != rb->pk)
        return ra->pk < rb->pk ? -1 : 1;
    return 0;
}

static bool vm_net_mock_equip_instance_update_row(
    const vm_net_mock_equip_instance_live_row *row, u32 newId)
{
    char query[320];

    if (row == NULL || newId == 0)
        return false;
    switch (row->loc)
    {
    case VM_NET_MOCK_EQUIP_INST_LOC_EQUIP:
        snprintf(query, sizeof(query),
                 "UPDATE account_role_equipment SET item_instance_id=%u "
                 "WHERE role_id=%u AND slot_index=%u AND item_id=%u",
                 newId, row->roleId, row->pk, row->itemId);
        break;
    case VM_NET_MOCK_EQUIP_INST_LOC_BAG:
        snprintf(query, sizeof(query),
                 "UPDATE account_role_backpack SET item_instance_id=%u "
                 "WHERE role_id=%u AND item_seq=%u AND item_id=%u",
                 newId, row->roleId, row->pk, row->itemId);
        break;
    case VM_NET_MOCK_EQUIP_INST_LOC_WAREHOUSE:
        snprintf(query, sizeof(query),
                 "UPDATE account_role_warehouse SET item_instance_id=%u "
                 "WHERE role_id=%u AND slot_index=%u AND item_id=%u",
                 newId, row->roleId, row->pk, row->itemId);
        break;
    case VM_NET_MOCK_EQUIP_INST_LOC_AUCTION:
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET item_instance_id=%u "
                 "WHERE listing_id=%u",
                 newId, row->pk);
        break;
    case VM_NET_MOCK_EQUIP_INST_LOC_CLAIM:
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_claims SET item_instance_id=%u "
                 "WHERE claim_id=%u",
                 newId, row->pk);
        break;
    default:
        return false;
    }
    return vm_mysql_exec(query);
}

static bool vm_net_mock_equip_instance_rekey_side(u32 roleId, u32 oldId, u32 newId)
{
    char query[384];

    if (roleId == 0 || oldId == 0 || newId == 0 || oldId == newId)
        return true;
    snprintf(query, sizeof(query),
             "UPDATE account_role_enhance_wash_record SET item_instance_id=%u "
             "WHERE role_id=%u AND item_instance_id=%u",
             newId, roleId, oldId);
    (void)vm_mysql_exec(query);
    snprintf(query, sizeof(query),
             "UPDATE account_role_enhance_crystal_spent SET item_instance_id=%u "
             "WHERE role_id=%u AND item_instance_id=%u",
             newId, roleId, oldId);
    (void)vm_mysql_exec(query);
    return true;
}

static bool vm_net_mock_equipment_instance_orphan_cleanup(void)
{
    (void)vm_mysql_exec(
        "DELETE r FROM account_role_enhance_wash_record r "
        "WHERE r.item_instance_id<>0 "
        "AND NOT EXISTS (SELECT 1 FROM account_role_backpack b "
        "WHERE b.role_id=r.role_id AND b.item_instance_id=r.item_instance_id) "
        "AND NOT EXISTS (SELECT 1 FROM account_role_equipment e "
        "WHERE e.role_id=r.role_id AND e.item_instance_id=r.item_instance_id) "
        "AND NOT EXISTS (SELECT 1 FROM account_role_warehouse w "
        "WHERE w.role_id=r.role_id AND w.item_instance_id=r.item_instance_id) "
        "AND NOT EXISTS (SELECT 1 FROM server_auction_listings a "
        "WHERE a.seller_role_id=r.role_id AND a.item_instance_id=r.item_instance_id "
        "AND a.status=0) "
        "AND NOT EXISTS (SELECT 1 FROM server_auction_claims c "
        "WHERE c.role_id=r.role_id AND c.item_instance_id=r.item_instance_id "
        "AND c.claimed=0)");
    (void)vm_mysql_exec(
        "DELETE r FROM account_role_enhance_crystal_spent r "
        "WHERE r.item_instance_id<>0 "
        "AND NOT EXISTS (SELECT 1 FROM account_role_backpack b "
        "WHERE b.role_id=r.role_id AND b.item_instance_id=r.item_instance_id) "
        "AND NOT EXISTS (SELECT 1 FROM account_role_equipment e "
        "WHERE e.role_id=r.role_id AND e.item_instance_id=r.item_instance_id) "
        "AND NOT EXISTS (SELECT 1 FROM account_role_warehouse w "
        "WHERE w.role_id=r.role_id AND w.item_instance_id=r.item_instance_id) "
        "AND NOT EXISTS (SELECT 1 FROM server_auction_listings a "
        "WHERE a.seller_role_id=r.role_id AND a.item_instance_id=r.item_instance_id "
        "AND a.status=0) "
        "AND NOT EXISTS (SELECT 1 FROM server_auction_claims c "
        "WHERE c.role_id=r.role_id AND c.item_instance_id=r.item_instance_id "
        "AND c.claimed=0)");
    printf("[info][mock-service] equipment_instance_orphan_cleanup done\n");
    return true;
}

static void vm_net_mock_equipment_instance_migrate_once(void)
{
    static vm_net_mock_equip_instance_live_row
        rows[VM_NET_MOCK_EQUIP_INSTANCE_LIVE_CAP];
    vm_net_mock_equip_instance_scan_ctx ctx;
    u32 i;
    u32 remapped = 0;
    u32 zeroFilled = 0;

    if (g_vm_net_mock_equip_instance_migrate_done)
        return;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_equipment_instance_migrate ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "remap_done TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY(id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4") ||
        !vm_mysql_exec(
            "INSERT IGNORE INTO server_equipment_instance_migrate(id,remap_done) "
            "VALUES(1,0)"))
    {
        printf("[error][mock-service] equipment_instance_migrate table error=%s\n",
               vm_mysql_last_error());
        return;
    }
    {
        u32 done = 0;
        (void)vm_mysql_query(
            "SELECT remap_done FROM server_equipment_instance_migrate WHERE id=1",
            vm_net_mock_equip_instance_u32_row, &done);
        if (done != 0)
        {
            g_vm_net_mock_equip_instance_migrate_done = true;
            (void)vm_net_mock_equipment_instance_seq_ensure();
            return;
        }
    }

    (void)vm_net_mock_enhance_wash_record_ensure_table();
    (void)vm_net_mock_enhance_crystal_spent_ensure_table();
    (void)vm_mysql_exec(
        "ALTER TABLE account_role_warehouse ADD COLUMN item_instance_id "
        "INT UNSIGNED NOT NULL DEFAULT 0 AFTER enhance_level");

    memset(rows, 0, sizeof(rows));
    memset(&ctx, 0, sizeof(ctx));
    ctx.rows = rows;
    ctx.cap = VM_NET_MOCK_EQUIP_INSTANCE_LIVE_CAP;

    ctx.loc = (u8)VM_NET_MOCK_EQUIP_INST_LOC_EQUIP;
    (void)vm_mysql_query(
        "SELECT role_id,slot_index,item_id,item_instance_id,enhance_wash "
        "FROM account_role_equipment WHERE item_id<>0",
        vm_net_mock_equip_instance_scan_row, &ctx);
    ctx.loc = (u8)VM_NET_MOCK_EQUIP_INST_LOC_BAG;
    (void)vm_mysql_query(
        "SELECT role_id,item_seq,item_id,item_instance_id,enhance_wash "
        "FROM account_role_backpack WHERE item_id<>0",
        vm_net_mock_equip_instance_scan_row, &ctx);
    ctx.loc = (u8)VM_NET_MOCK_EQUIP_INST_LOC_WAREHOUSE;
    (void)vm_mysql_query(
        "SELECT role_id,slot_index,item_id,item_instance_id,enhance_wash "
        "FROM account_role_warehouse WHERE item_id<>0",
        vm_net_mock_equip_instance_scan_row, &ctx);
    ctx.loc = (u8)VM_NET_MOCK_EQUIP_INST_LOC_AUCTION;
    (void)vm_mysql_query(
        "SELECT seller_role_id,listing_id,item_id,item_instance_id,enhance_wash "
        "FROM server_auction_listings WHERE status=0 AND item_id<>0",
        vm_net_mock_equip_instance_scan_row, &ctx);
    ctx.loc = (u8)VM_NET_MOCK_EQUIP_INST_LOC_CLAIM;
    (void)vm_mysql_query(
        "SELECT role_id,claim_id,item_id,item_instance_id,enhance_wash "
        "FROM server_auction_claims WHERE claimed=0 AND item_id<>0",
        vm_net_mock_equip_instance_scan_row, &ctx);

    qsort(rows, ctx.count, sizeof(rows[0]), vm_net_mock_equip_instance_live_cmp);

    if (!vm_net_mock_equipment_instance_seq_ensure())
        return;

    for (i = 0; i < ctx.count; ++i)
    {
        vm_net_mock_equip_instance_live_row *row = &rows[i];
        bool collide = false;

        if (row->oldInstanceId == 0)
        {
            u32 nid = vm_net_mock_equipment_instance_alloc();
            if (nid == 0)
                continue;
            if (vm_net_mock_equip_instance_update_row(row, nid))
            {
                row->newInstanceId = nid;
                zeroFilled += 1;
            }
            continue;
        }
        /* First row of an old-id group keeps it; every later same-id row
         * must rekey — do not require prev.newInstanceId==old (that skips
         * the 3rd+ duplicate after the 2nd was already remapped). */
        if (i > 0 && rows[i - 1].oldInstanceId == row->oldInstanceId)
            collide = true;
        if (!collide)
            continue;
        {
            u32 nid = vm_net_mock_equipment_instance_alloc();
            if (nid == 0)
                continue;
            if (!vm_net_mock_equip_instance_update_row(row, nid))
                continue;
            (void)vm_net_mock_equip_instance_rekey_side(row->roleId, row->oldInstanceId,
                                                        nid);
            row->newInstanceId = nid;
            remapped += 1;
        }
    }

    (void)vm_net_mock_equipment_instance_orphan_cleanup();
    if (!vm_mysql_exec(
            "UPDATE server_equipment_instance_migrate SET remap_done=1 WHERE id=1"))
    {
        printf("[error][mock-service] equipment_instance_migrate mark error=%s\n",
               vm_mysql_last_error());
        return;
    }
    g_vm_net_mock_equip_instance_migrate_done = true;
    printf("[info][mock-service] equipment_instance_migrate live=%u zero_fill=%u "
           "rekeyed=%u evidence=full-db-unique-track-id\n",
           ctx.count, zeroFilled, remapped);
}
