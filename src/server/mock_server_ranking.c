/*
 * Player ranking list (WT 23/7).
 *
 * JianghuOL.CBE SendShopPageReq (0x0102ABD4) creates this request with a
 * ranking type and page index. Its historical decompiler name is misleading:
 * HandleRankingList (0x0102B9A8) consumes the reply as a paged ranking table.
 *
 * This module deliberately reads the relational account_roles table instead
 * of the requester's cached role database. A rank is global state, so an
 * offline role must remain visible and a request served by a different worker
 * must observe the same source of truth.
 */

enum
{
    VM_NET_MOCK_RANKING_TYPE_LEVEL = 0,
    VM_NET_MOCK_RANKING_TYPE_EXP = 1,
    VM_NET_MOCK_RANKING_TYPE_MONEY = 2,
    VM_NET_MOCK_RANKING_TYPE_COUNT = 3,
    VM_NET_MOCK_RANKING_PAGE_ROWS = 10,
    /* HandleRankingList allocates 30 bytes for every received cell. */
    VM_NET_MOCK_RANKING_CELL_TEXT_MAX = 30
};

typedef struct
{
    u32 roleId;
    char name[VM_NET_MOCK_RANKING_CELL_TEXT_MAX];
    u32 level;
    u32 exp;
    u32 money;
} vm_net_mock_ranking_row;

typedef struct
{
    vm_net_mock_ranking_row *rows;
    u32 rowCapacity;
    u32 rowCount;
    bool invalid;
} vm_net_mock_ranking_rows_context;

typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_net_mock_ranking_u32_context;

typedef struct
{
    u32 level;
    u32 exp;
    u32 money;
    bool found;
    bool invalid;
} vm_net_mock_ranking_score_context;

static const char *vm_net_mock_ranking_order_by(u8 rankingType)
{
    switch (rankingType)
    {
    case VM_NET_MOCK_RANKING_TYPE_LEVEL:
        return "level DESC,exp DESC,role_id ASC";
    case VM_NET_MOCK_RANKING_TYPE_EXP:
        return "exp DESC,level DESC,role_id ASC";
    case VM_NET_MOCK_RANKING_TYPE_MONEY:
        return "money DESC,level DESC,exp DESC,role_id ASC";
    default:
        return NULL;
    }
}

static const char *vm_net_mock_ranking_type_name(u8 rankingType)
{
    switch (rankingType)
    {
    case VM_NET_MOCK_RANKING_TYPE_LEVEL:
        return "\xb5\xc8\xbc\xb6\xc5\xc5\xd0\xd0"; /* GBK: 等级排行 */
    case VM_NET_MOCK_RANKING_TYPE_EXP:
        return "\xbe\xad\xd1\xe9\xc5\xc5\xd0\xd0"; /* GBK: 经验排行 */
    case VM_NET_MOCK_RANKING_TYPE_MONEY:
        return "\xb2\xc6\xb8\xbb\xc5\xc5\xd0\xd0"; /* GBK: 财富排行 */
    default:
        return "";
    }
}

static const char *vm_net_mock_ranking_value_column_name(u8 rankingType)
{
    switch (rankingType)
    {
    case VM_NET_MOCK_RANKING_TYPE_LEVEL:
        return "\xb5\xc8\xbc\xb6"; /* GBK: 等级 */
    case VM_NET_MOCK_RANKING_TYPE_EXP:
        return "\xbe\xad\xd1\xe9"; /* GBK: 经验 */
    case VM_NET_MOCK_RANKING_TYPE_MONEY:
        return "\xc7\xae\xb1\xd2"; /* GBK: 钱币 */
    default:
        return "";
    }
}

static u32 vm_net_mock_ranking_row_value(const vm_net_mock_ranking_row *row,
                                         u8 rankingType)
{
    if (row == NULL)
        return 0;
    switch (rankingType)
    {
    case VM_NET_MOCK_RANKING_TYPE_LEVEL:
        return row->level;
    case VM_NET_MOCK_RANKING_TYPE_EXP:
        return row->exp;
    case VM_NET_MOCK_RANKING_TYPE_MONEY:
        return row->money;
    default:
        return 0;
    }
}

static bool vm_net_mock_ranking_rows_row(void *contextValue,
                                         unsigned int columnCount,
                                         const char *const *values,
                                         const size_t *lengths)
{
    vm_net_mock_ranking_rows_context *context =
        (vm_net_mock_ranking_rows_context *)contextValue;
    vm_net_mock_ranking_row *row = NULL;
    size_t nameLength = 0;

    if (context == NULL || context->rows == NULL || context->rowCount >= context->rowCapacity ||
        columnCount != 5 || values[1] == NULL ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->rows[context->rowCount].roleId) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &context->rows[context->rowCount].level) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &context->rows[context->rowCount].exp) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &context->rows[context->rowCount].money) ||
        context->rows[context->rowCount].roleId == 0)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }

    row = &context->rows[context->rowCount];
    if (!vm_mysql_hex_decode(values[1], lengths[1], row->name,
                             sizeof(row->name) - 1, &nameLength) ||
        nameLength == 0)
    {
        context->invalid = true;
        memset(row, 0, sizeof(*row));
        return true;
    }
    row->name[nameLength] = 0;
    ++context->rowCount;
    return true;
}

static bool vm_net_mock_ranking_u32_row(void *contextValue,
                                        unsigned int columnCount,
                                        const char *const *values,
                                        const size_t *lengths)
{
    vm_net_mock_ranking_u32_context *context =
        (vm_net_mock_ranking_u32_context *)contextValue;

    if (context == NULL || context->found || columnCount != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->value))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_ranking_score_row(void *contextValue,
                                          unsigned int columnCount,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_net_mock_ranking_score_context *context =
        (vm_net_mock_ranking_score_context *)contextValue;

    if (context == NULL || context->found || columnCount != 3 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->level) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &context->exp) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &context->money))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_ranking_query_total(u32 *totalOut)
{
    vm_net_mock_ranking_u32_context context;

    if (totalOut)
        *totalOut = 0;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query("SELECT COUNT(*) FROM account_roles",
                        vm_net_mock_ranking_u32_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (totalOut)
        *totalOut = context.value;
    return true;
}

static bool vm_net_mock_ranking_query_rows(u8 rankingType,
                                           u32 pageIndex,
                                           vm_net_mock_ranking_row *rows,
                                           u32 rowCapacity,
                                           u32 *rowCountOut)
{
    char query[512];
    const char *orderBy = vm_net_mock_ranking_order_by(rankingType);
    vm_net_mock_ranking_rows_context context;
    u32 offset = pageIndex * VM_NET_MOCK_RANKING_PAGE_ROWS;

    if (rowCountOut)
        *rowCountOut = 0;
    if (orderBy == NULL || rows == NULL || rowCapacity == 0)
        return false;
    memset(&context, 0, sizeof(context));
    context.rows = rows;
    context.rowCapacity = rowCapacity;
    snprintf(query, sizeof(query),
             "SELECT role_id,HEX(role_name),level,exp,money FROM account_roles "
             "ORDER BY %s LIMIT %u OFFSET %u",
             orderBy, (u32)VM_NET_MOCK_RANKING_PAGE_ROWS, offset);
    if (!vm_mysql_query(query, vm_net_mock_ranking_rows_row, &context) || context.invalid)
        return false;
    if (rowCountOut)
        *rowCountOut = context.rowCount;
    return true;
}

static bool vm_net_mock_ranking_query_my_order(u8 rankingType,
                                               u32 roleId,
                                               u32 *rankOut)
{
    char query[768];
    vm_net_mock_ranking_score_context score;
    vm_net_mock_ranking_u32_context count;

    if (rankOut)
        *rankOut = 0;
    if (vm_net_mock_ranking_order_by(rankingType) == NULL || roleId == 0)
        return false;

    memset(&score, 0, sizeof(score));
    snprintf(query, sizeof(query),
             "SELECT level,exp,money FROM account_roles WHERE role_id=%u LIMIT 1",
             roleId);
    if (!vm_mysql_query(query, vm_net_mock_ranking_score_row, &score) || score.invalid)
        return false;
    if (!score.found)
        return true;

    memset(&count, 0, sizeof(count));
    switch (rankingType)
    {
    case VM_NET_MOCK_RANKING_TYPE_LEVEL:
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM account_roles WHERE level>%u OR "
                 "(level=%u AND exp>%u) OR "
                 "(level=%u AND exp=%u AND role_id<%u)",
                 score.level, score.level, score.exp,
                 score.level, score.exp, roleId);
        break;
    case VM_NET_MOCK_RANKING_TYPE_EXP:
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM account_roles WHERE exp>%u OR "
                 "(exp=%u AND level>%u) OR "
                 "(exp=%u AND level=%u AND role_id<%u)",
                 score.exp, score.exp, score.level,
                 score.exp, score.level, roleId);
        break;
    case VM_NET_MOCK_RANKING_TYPE_MONEY:
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM account_roles WHERE money>%u OR "
                 "(money=%u AND level>%u) OR "
                 "(money=%u AND level=%u AND exp>%u) OR "
                 "(money=%u AND level=%u AND exp=%u AND role_id<%u)",
                 score.money, score.money, score.level,
                 score.money, score.level, score.exp,
                 score.money, score.level, score.exp, roleId);
        break;
    default:
        return false;
    }
    if (!vm_mysql_query(query, vm_net_mock_ranking_u32_row, &count) ||
        count.invalid || !count.found || count.value == 0xffffffffu)
    {
        return false;
    }
    if (rankOut)
        *rankOut = count.value + 1u;
    return true;
}

static bool vm_net_mock_is_ranking_page_request(const u8 *request,
                                                u32 requestLen,
                                                u8 *rankingTypeOut,
                                                u8 *pageIndexOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 rankingType = 0;
    u8 pageIndex = 0;

    if (rankingTypeOut)
        *rankingTypeOut = 0;
    if (pageIndexOut)
        *pageIndexOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 23 || object.subtype != 7 ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &rankingType) ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "pageIndex", &pageIndex) ||
        vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || rankingType >= VM_NET_MOCK_RANKING_TYPE_COUNT)
    {
        return false;
    }
    if (rankingTypeOut)
        *rankingTypeOut = rankingType;
    if (pageIndexOut)
        *pageIndexOut = pageIndex;
    return true;
}

static u32 vm_net_mock_build_ranking_page_response(const u8 *request,
                                                    u32 requestLen,
                                                    u8 *out,
                                                    u32 outCap)
{
    static const char *const columnPrefix[] = {
        "\xc5\xc5\xc3\xfb", /* GBK: 排名 */
        "\xbd\xc7\xc9\xab"  /* GBK: 角色 */
    };
    vm_net_mock_ranking_row rows[VM_NET_MOCK_RANKING_PAGE_ROWS];
    u8 orderList[128];
    u8 columnNames[96];
    u8 topPlayerInfo[1024];
    u32 orderListLen = 0;
    u32 columnNamesLen = 0;
    u32 topPlayerInfoLen = 0;
    u32 totalRows = 0;
    u32 rowCount = 0;
    u32 myOrder = 0;
    u32 pageMax = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 rankingType = 0;
    u8 pageIndex = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    memset(rows, 0, sizeof(rows));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_ranking_page_request(request, requestLen,
                                             &rankingType, &pageIndex) ||
        !vm_net_mock_ranking_query_total(&totalRows) ||
        !vm_net_mock_ranking_query_rows(rankingType, pageIndex, rows,
                                        VM_NET_MOCK_RANKING_PAGE_ROWS, &rowCount) ||
        !vm_net_mock_ranking_query_my_order(rankingType,
                                            role ? role->roleId : 0, &myOrder))
    {
        printf("[error][network] mock_ranking_page_query_failed type=%u page=%u "
               "role=%u error=%s\n",
               rankingType, pageIndex, role ? role->roleId : 0, vm_mysql_last_error());
        return 0;
    }

    pageMax = totalRows == 0 ? 0 :
              (totalRows - 1u) / VM_NET_MOCK_RANKING_PAGE_ROWS;
    for (u8 type = 0; type < VM_NET_MOCK_RANKING_TYPE_COUNT; ++type)
    {
        if (!vm_net_mock_seq_put_u8(orderList, sizeof(orderList), &orderListLen, type) ||
            !vm_net_mock_seq_put_string(orderList, sizeof(orderList), &orderListLen,
                                        vm_net_mock_ranking_type_name(type)))
        {
            return 0;
        }
    }
    if (!vm_net_mock_seq_put_string(columnNames, sizeof(columnNames), &columnNamesLen,
                                    columnPrefix[0]) ||
        !vm_net_mock_seq_put_string(columnNames, sizeof(columnNames), &columnNamesLen,
                                    columnPrefix[1]) ||
        !vm_net_mock_seq_put_string(columnNames, sizeof(columnNames), &columnNamesLen,
                                    vm_net_mock_ranking_value_column_name(rankingType)))
    {
        return 0;
    }
    for (u32 i = 0; i < rowCount; ++i)
    {
        char valueText[16];
        u32 globalRank = (u32)pageIndex * VM_NET_MOCK_RANKING_PAGE_ROWS + i + 1u;

        snprintf(valueText, sizeof(valueText), "%u",
                 vm_net_mock_ranking_row_value(&rows[i], rankingType));
        if (!vm_net_mock_seq_put_u32(topPlayerInfo, sizeof(topPlayerInfo),
                                     &topPlayerInfoLen, globalRank) ||
            !vm_net_mock_seq_put_string(topPlayerInfo, sizeof(topPlayerInfo),
                                        &topPlayerInfoLen, rows[i].name) ||
            !vm_net_mock_seq_put_string(topPlayerInfo, sizeof(topPlayerInfo),
                                        &topPlayerInfoLen, valueText))
        {
            return 0;
        }
    }
    if (orderListLen > 0xffffu || columnNamesLen > 0xffffu || topPlayerInfoLen > 0xffffu ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 23, 7, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "ordernum",
                                   VM_NET_MOCK_RANKING_TYPE_COUNT) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "orderlist",
                                    orderList, (u16)orderListLen) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "myorder", myOrder) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "colnum", 3) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "colnames",
                                    columnNames, (u16)columnNamesLen) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pagemax", pageMax) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "count", rowCount) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "topplayerinfo",
                                    topPlayerInfo, (u16)topPlayerInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_ranking_page type=%u page=%u rows=%u total=%u "
           "myorder=%u pagemax=%u source=mysql-account_roles response=23/7 resp=%u "
           "evidence=JianghuOL.CBE:0x0102ABD4+0x0102B9A8\n",
           rankingType, pageIndex, rowCount, totalRows, myOrder, pageMax, pos);
    return pos;
}
