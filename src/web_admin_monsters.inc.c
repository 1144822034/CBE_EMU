/* Monster catalog editor included by web_admin_server.c after the shared
 * mock-server catalogs and HTML helpers are available. */

static const char *vm_mock_admin_monster_family_name(u32 family)
{
    static const char *names[] = {
        "胶质", "野兽", "飞行", "昆虫", "爬行", "亡灵",
        "灵体", "元素", "岩石", "人形", "士兵", "首领"};

    if (family >= sizeof(names) / sizeof(names[0]))
        return "未知";
    return names[family];
}

static void vm_mock_admin_render_monster_family_select(
    vm_mock_admin_text *page, u32 selected)
{
    vm_mock_admin_text_appendf(page, "<select name=\"family\" required>");
    for (u32 family = 0; family <= VM_NET_MOCK_MONSTER_BOSS; ++family)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%u · %s</option>", family,
            selected == family ? " selected" : "", family,
            vm_mock_admin_monster_family_name(family));
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

/* The export is a real Office Open XML workbook.  Keeping the minimal ZIP
 * writer here avoids inventing a CSV/HTML compatibility layer and keeps its
 * data source next to the monster-management contract.  All parts are stored
 * without compression: the bounded boss catalog stays small, while every
 * desktop and mobile Excel reader still receives a normal .xlsx package. */
enum
{
    VM_MOCK_ADMIN_BOSS_EXPORT_MONSTER_MAX = 512,
    VM_MOCK_ADMIN_BOSS_EXPORT_ZIP_MAX = 4 * 1024 * 1024,
    VM_MOCK_ADMIN_BOSS_EXPORT_FILE_COUNT = 6
};

typedef struct
{
    u8 *data;
    u32 length;
    u32 capacity;
    bool failed;
} vm_mock_admin_xlsx_bytes;

typedef struct
{
    const char *name;
    u32 crc32;
    u32 offset;
    u32 length;
} vm_mock_admin_xlsx_zip_entry;

static void vm_mock_admin_xlsx_bytes_free(vm_mock_admin_xlsx_bytes *bytes)
{
    if (bytes == NULL)
        return;
    free(bytes->data);
    memset(bytes, 0, sizeof(*bytes));
}

static bool vm_mock_admin_xlsx_bytes_reserve(vm_mock_admin_xlsx_bytes *bytes,
                                             u32 appendLength)
{
    u32 needed = 0;
    u32 capacity = 0;
    u8 *newData = NULL;

    if (bytes == NULL || bytes->failed ||
        appendLength > VM_MOCK_ADMIN_BOSS_EXPORT_ZIP_MAX - bytes->length)
    {
        if (bytes != NULL)
            bytes->failed = true;
        return false;
    }
    needed = bytes->length + appendLength;
    if (needed <= bytes->capacity)
        return true;
    capacity = bytes->capacity == 0 ? 4096u : bytes->capacity;
    while (capacity < needed)
    {
        if (capacity > VM_MOCK_ADMIN_BOSS_EXPORT_ZIP_MAX / 2u)
        {
            capacity = VM_MOCK_ADMIN_BOSS_EXPORT_ZIP_MAX;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < needed)
    {
        bytes->failed = true;
        return false;
    }
    newData = (u8 *)realloc(bytes->data, capacity);
    if (newData == NULL)
    {
        bytes->failed = true;
        return false;
    }
    bytes->data = newData;
    bytes->capacity = capacity;
    return true;
}

static bool vm_mock_admin_xlsx_bytes_append(vm_mock_admin_xlsx_bytes *bytes,
                                            const void *data, u32 length)
{
    if (length == 0)
        return bytes != NULL && !bytes->failed;
    if (data == NULL || !vm_mock_admin_xlsx_bytes_reserve(bytes, length))
        return false;
    memcpy(bytes->data + bytes->length, data, length);
    bytes->length += length;
    return true;
}

static bool vm_mock_admin_xlsx_bytes_append_string(
    vm_mock_admin_xlsx_bytes *bytes, const char *value)
{
    size_t length = value != NULL ? strlen(value) : 0;

    if (length > 0xffffffffu)
    {
        if (bytes != NULL)
            bytes->failed = true;
        return false;
    }
    return vm_mock_admin_xlsx_bytes_append(bytes, value, (u32)length);
}

static bool vm_mock_admin_xlsx_bytes_appendf(vm_mock_admin_xlsx_bytes *bytes,
                                             const char *format, ...)
{
    va_list args;
    va_list copied;
    int length = 0;
    char *text = NULL;
    bool ok = false;

    if (bytes == NULL || bytes->failed || format == NULL)
        return false;
    va_start(args, format);
    va_copy(copied, args);
    length = vsnprintf(NULL, 0, format, copied);
    va_end(copied);
    if (length < 0 || (u32)length > VM_MOCK_ADMIN_BOSS_EXPORT_ZIP_MAX)
        goto done;
    text = (char *)malloc((size_t)length + 1u);
    if (text == NULL)
        goto done;
    if (vsnprintf(text, (size_t)length + 1u, format, args) != length)
        goto done;
    ok = vm_mock_admin_xlsx_bytes_append(bytes, text, (u32)length);

done:
    if (!ok)
        bytes->failed = true;
    free(text);
    va_end(args);
    return ok;
}

static bool vm_mock_admin_xlsx_bytes_append_u16(vm_mock_admin_xlsx_bytes *bytes,
                                                u32 value)
{
    u8 data[2] = {(u8)(value & 0xffu), (u8)((value >> 8) & 0xffu)};

    return vm_mock_admin_xlsx_bytes_append(bytes, data, sizeof(data));
}

static bool vm_mock_admin_xlsx_bytes_append_u32(vm_mock_admin_xlsx_bytes *bytes,
                                                u32 value)
{
    u8 data[4] = {
        (u8)(value & 0xffu), (u8)((value >> 8) & 0xffu),
        (u8)((value >> 16) & 0xffu), (u8)((value >> 24) & 0xffu)};

    return vm_mock_admin_xlsx_bytes_append(bytes, data, sizeof(data));
}

static u32 vm_mock_admin_xlsx_crc32(const u8 *data, u32 length)
{
    u32 crc = 0xffffffffu;

    for (u32 i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (u32 bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

static bool vm_mock_admin_xlsx_append_xml_escaped(
    vm_mock_admin_xlsx_bytes *bytes, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");

    while (*cursor != 0)
    {
        const char *replacement = NULL;

        switch (*cursor)
        {
        case '&': replacement = "&amp;"; break;
        case '<': replacement = "&lt;"; break;
        case '>': replacement = "&gt;"; break;
        case '\"': replacement = "&quot;"; break;
        case '\'': replacement = "&apos;"; break;
        default: break;
        }
        if (replacement != NULL)
        {
            if (!vm_mock_admin_xlsx_bytes_append_string(bytes, replacement))
                return false;
        }
        else if (*cursor >= 0x20u || *cursor == '\t' || *cursor == '\n' ||
                 *cursor == '\r')
        {
            if (!vm_mock_admin_xlsx_bytes_append(bytes, cursor, 1))
                return false;
        }
        ++cursor;
    }
    return true;
}

static bool vm_mock_admin_xlsx_sheet_append_text_cell(
    vm_mock_admin_xlsx_bytes *sheet, char column, u32 row, u32 style,
    const char *value)
{
    return vm_mock_admin_xlsx_bytes_appendf(
               sheet,
               "<c r=\"%c%u\" s=\"%u\" t=\"inlineStr\"><is><t xml:space=\"preserve\">",
               column, row, style) &&
           vm_mock_admin_xlsx_append_xml_escaped(sheet, value) &&
           vm_mock_admin_xlsx_bytes_append_string(sheet, "</t></is></c>");
}

static bool vm_mock_admin_xlsx_sheet_append_probability_cell(
    vm_mock_admin_xlsx_bytes *sheet, u32 row, u32 style, u32 rate)
{
    return vm_mock_admin_xlsx_bytes_appendf(
        sheet, "<c r=\"E%u\" s=\"%u\" t=\"inlineStr\"><is><t>%u%%</t></is></c>",
        row, style, rate);
}

static u32 vm_mock_admin_boss_export_equipment_drop_count(
    const vm_net_mock_monster_admin_row *monster)
{
    u32 count = 0;

    if (monster == NULL)
        return 0;
    for (u32 i = 0; i < monster->dropCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_find_shop_catalog_item(monster->drops[i].itemId);
        if (item != NULL && item->isEquip)
            ++count;
    }
    return count;
}

static int vm_mock_admin_boss_export_monster_compare(const void *leftValue,
                                                      const void *rightValue)
{
    const vm_net_mock_monster_admin_row *const *left =
        (const vm_net_mock_monster_admin_row *const *)leftValue;
    const vm_net_mock_monster_admin_row *const *right =
        (const vm_net_mock_monster_admin_row *const *)rightValue;
    int result = strcmp((*left)->firstScene, (*right)->firstScene);

    if (result != 0)
        return result;
    if ((*left)->enemyId < (*right)->enemyId)
        return -1;
    if ((*left)->enemyId > (*right)->enemyId)
        return 1;
    return 0;
}

static void vm_mock_admin_boss_export_scene_labels(const char *sceneGbk,
                                                   char *regionUtf8,
                                                   size_t regionCap,
                                                   char *locationUtf8,
                                                   size_t locationCap)
{
    char sceneUtf8[192];
    char region[192];
    char *start = NULL;
    char *separator = NULL;
    char *extension = NULL;

    if (regionUtf8 != NULL && regionCap != 0)
        regionUtf8[0] = 0;
    if (locationUtf8 != NULL && locationCap != 0)
        locationUtf8[0] = 0;
    if (sceneGbk == NULL || sceneGbk[0] == 0)
    {
        if (regionUtf8 != NULL && regionCap != 0)
            snprintf(regionUtf8, regionCap, "特殊挑战");
        if (locationUtf8 != NULL && locationCap != 0)
            snprintf(locationUtf8, locationCap, "未标注场景");
        return;
    }
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    vm_net_mock_gbk_label_to_utf8(sceneGbk, sceneUtf8, sizeof(sceneUtf8));
    if (sceneUtf8[0] == 0)
        snprintf(sceneUtf8, sizeof(sceneUtf8), "%s", sceneGbk);
    if (locationUtf8 != NULL && locationCap != 0)
    {
        snprintf(locationUtf8, locationCap, "%s", sceneUtf8);
        extension = strrchr(locationUtf8, '.');
        if (extension != NULL && strcmp(extension, ".sce") == 0)
            *extension = 0;
    }
    snprintf(region, sizeof(region), "%s",
             locationUtf8 != NULL && locationUtf8[0] != 0 ?
                 locationUtf8 : sceneUtf8);
    start = region;
    if (start[0] == 'c' && start[1] >= '0' && start[1] <= '9' &&
        start[2] >= '0' && start[2] <= '9')
    {
        start += 3;
    }
    else
    {
        while (*start >= '0' && *start <= '9')
            ++start;
    }
    separator = strrchr(start, '_');
    if (separator != NULL)
        *separator = 0;
    if (regionUtf8 != NULL && regionCap != 0)
        snprintf(regionUtf8, regionCap, "%s", start[0] != 0 ? start : region);
}

static bool vm_mock_admin_xlsx_append_zip_file(
    vm_mock_admin_xlsx_bytes *zip, vm_mock_admin_xlsx_zip_entry *entry,
    const char *name, const vm_mock_admin_xlsx_bytes *file)
{
    size_t nameLength = name != NULL ? strlen(name) : 0;

    if (zip == NULL || entry == NULL || name == NULL || file == NULL ||
        file->data == NULL || nameLength == 0 || nameLength > 0xffffu)
    {
        return false;
    }
    entry->name = name;
    entry->crc32 = vm_mock_admin_xlsx_crc32(file->data, file->length);
    entry->offset = zip->length;
    entry->length = file->length;
    return vm_mock_admin_xlsx_bytes_append_u32(zip, 0x04034b50u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 20u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) &&
           vm_mock_admin_xlsx_bytes_append_u32(zip, entry->crc32) &&
           vm_mock_admin_xlsx_bytes_append_u32(zip, entry->length) &&
           vm_mock_admin_xlsx_bytes_append_u32(zip, entry->length) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, (u32)nameLength) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) &&
           vm_mock_admin_xlsx_bytes_append(zip, name, (u32)nameLength) &&
           vm_mock_admin_xlsx_bytes_append(zip, file->data, file->length);
}

static bool vm_mock_admin_xlsx_finish_zip(
    vm_mock_admin_xlsx_bytes *zip, const vm_mock_admin_xlsx_zip_entry *entries,
    u32 entryCount)
{
    u32 centralOffset = 0;
    u32 centralLength = 0;

    if (zip == NULL || entries == NULL || entryCount == 0 ||
        entryCount > 0xffffu)
    {
        return false;
    }
    centralOffset = zip->length;
    for (u32 i = 0; i < entryCount; ++i)
    {
        size_t nameLength = strlen(entries[i].name);

        if (nameLength == 0 || nameLength > 0xffffu ||
            !vm_mock_admin_xlsx_bytes_append_u32(zip, 0x02014b50u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 20u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 20u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u32(zip, entries[i].crc32) ||
            !vm_mock_admin_xlsx_bytes_append_u32(zip, entries[i].length) ||
            !vm_mock_admin_xlsx_bytes_append_u32(zip, entries[i].length) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, (u32)nameLength) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u32(zip, 0u) ||
            !vm_mock_admin_xlsx_bytes_append_u32(zip, entries[i].offset) ||
            !vm_mock_admin_xlsx_bytes_append(zip, entries[i].name,
                                             (u32)nameLength))
        {
            return false;
        }
    }
    centralLength = zip->length - centralOffset;
    return vm_mock_admin_xlsx_bytes_append_u32(zip, 0x06054b50u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, entryCount) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, entryCount) &&
           vm_mock_admin_xlsx_bytes_append_u32(zip, centralLength) &&
           vm_mock_admin_xlsx_bytes_append_u32(zip, centralOffset) &&
           vm_mock_admin_xlsx_bytes_append_u16(zip, 0u);
}

static bool vm_mock_admin_build_monster_boss_drop_xlsx(u8 **dataOut,
                                                       u32 *lengthOut)
{
    vm_net_mock_monster_admin_row *monsters = NULL;
    const vm_net_mock_monster_admin_row *bosses[
        VM_MOCK_ADMIN_BOSS_EXPORT_MONSTER_MAX];
    vm_mock_admin_xlsx_bytes zip;
    vm_mock_admin_xlsx_bytes parts[VM_MOCK_ADMIN_BOSS_EXPORT_FILE_COUNT];
    vm_mock_admin_xlsx_zip_entry entries[VM_MOCK_ADMIN_BOSS_EXPORT_FILE_COUNT];
    const char *names[VM_MOCK_ADMIN_BOSS_EXPORT_FILE_COUNT] = {
        "[Content_Types].xml", "_rels/.rels", "xl/workbook.xml",
        "xl/_rels/workbook.xml.rels", "xl/styles.xml", "xl/worksheets/sheet1.xml"};
    u32 total = 0;
    u32 monsterCount = 0;
    u32 bossCount = 0;
    u32 dataRows = 0;
    u32 sheetRow = 2;
    bool ok = false;

    if (dataOut != NULL)
        *dataOut = NULL;
    if (lengthOut != NULL)
        *lengthOut = 0;
    if (dataOut == NULL || lengthOut == NULL)
        return false;
    memset(&zip, 0, sizeof(zip));
    memset(parts, 0, sizeof(parts));
    memset(entries, 0, sizeof(entries));
    memset(bosses, 0, sizeof(bosses));

    total = vm_net_mock_monster_admin_list(NULL, 0);
    if (total > VM_MOCK_ADMIN_BOSS_EXPORT_MONSTER_MAX)
        goto done;
    if (total != 0)
    {
        monsters = (vm_net_mock_monster_admin_row *)calloc(
            total, sizeof(*monsters));
        if (monsters == NULL)
            goto done;
        monsterCount = vm_net_mock_monster_admin_list(monsters, total);
        if (monsterCount > total)
            goto done;
    }
    for (u32 i = 0; i < monsterCount; ++i)
    {
        u32 drops = vm_mock_admin_boss_export_equipment_drop_count(&monsters[i]);

        if (monsters[i].family != VM_NET_MOCK_MONSTER_BOSS || drops == 0)
            continue;
        if (bossCount >= VM_MOCK_ADMIN_BOSS_EXPORT_MONSTER_MAX ||
            dataRows > 1048575u - drops)
        {
            goto done;
        }
        bosses[bossCount++] = &monsters[i];
        dataRows += drops;
    }
    qsort(bosses, bossCount, sizeof(bosses[0]),
          vm_mock_admin_boss_export_monster_compare);

    if (!vm_mock_admin_xlsx_bytes_append_string(
            &parts[0],
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
            "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
            "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
            "</Types>") ||
        !vm_mock_admin_xlsx_bytes_append_string(
            &parts[1],
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
            "</Relationships>") ||
        !vm_mock_admin_xlsx_bytes_append_string(
            &parts[2],
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<sheets><sheet name=\"首领掉落\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>") ||
        !vm_mock_admin_xlsx_bytes_append_string(
            &parts[3],
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
            "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
            "</Relationships>") ||
        !vm_mock_admin_xlsx_bytes_append_string(
            &parts[4],
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Microsoft YaHei\"/></font><font><b/><color rgb=\"FFFFFFFF\"/><sz val=\"11\"/><name val=\"Microsoft YaHei\"/></font></fonts>"
            "<fills count=\"4\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill><fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1F4E78\"/><bgColor indexed=\"64\"/></patternFill></fill><fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFEAF3F8\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
            "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border><border><left style=\"thin\"><color rgb=\"FFD9E2F3\"/></left><right style=\"thin\"><color rgb=\"FFD9E2F3\"/></right><top style=\"thin\"><color rgb=\"FFD9E2F3\"/></top><bottom style=\"thin\"><color rgb=\"FFD9E2F3\"/></bottom><diagonal/></border></borders>"
            "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
            "<cellXfs count=\"4\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/><xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf><xf numFmtId=\"0\" fontId=\"0\" fillId=\"3\" borderId=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf></cellXfs>"
            "</styleSheet>") ||
        !vm_mock_admin_xlsx_bytes_appendf(
            &parts[5],
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><dimension ref=\"A1:E%u\"/><sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"21\"/><cols><col min=\"1\" max=\"1\" width=\"18\" customWidth=\"1\"/><col min=\"2\" max=\"2\" width=\"26\" customWidth=\"1\"/><col min=\"3\" max=\"3\" width=\"20\" customWidth=\"1\"/><col min=\"4\" max=\"4\" width=\"42\" customWidth=\"1\"/><col min=\"5\" max=\"5\" width=\"12\" customWidth=\"1\"/></cols><sheetData>",
            dataRows == 0 ? 2u : dataRows + 1u) ||
        !vm_mock_admin_xlsx_bytes_append_string(&parts[5], "<row r=\"1\" ht=\"25\" customHeight=\"1\">") ||
        !vm_mock_admin_xlsx_sheet_append_text_cell(&parts[5], 'A', 1u, 1u, "区域") ||
        !vm_mock_admin_xlsx_sheet_append_text_cell(&parts[5], 'B', 1u, 1u, "位置") ||
        !vm_mock_admin_xlsx_sheet_append_text_cell(&parts[5], 'C', 1u, 1u, "BOSS") ||
        !vm_mock_admin_xlsx_sheet_append_text_cell(&parts[5], 'D', 1u, 1u, "掉落装备") ||
        !vm_mock_admin_xlsx_sheet_append_text_cell(&parts[5], 'E', 1u, 1u, "概率") ||
        !vm_mock_admin_xlsx_bytes_append_string(&parts[5], "</row>"))
    {
        goto done;
    }

    if (bossCount == 0)
    {
        if (!vm_mock_admin_xlsx_bytes_append_string(&parts[5], "<row r=\"2\">") ||
            !vm_mock_admin_xlsx_sheet_append_text_cell(
                &parts[5], 'A', 2u, 2u, "当前没有已配置装备掉落的首领怪物") ||
            !vm_mock_admin_xlsx_bytes_append_string(&parts[5], "</row><mergeCells count=\"1\"><mergeCell ref=\"A2:E2\"/></mergeCells>"))
        {
            goto done;
        }
    }
    else
    {
        u32 mergeCount = 0;
        u32 regionStart = 0;
        u32 regionEnd = 0;
        char activeRegion[192];
        bool regionActive = false;
        vm_mock_admin_xlsx_bytes merges;

        memset(&merges, 0, sizeof(merges));
        memset(activeRegion, 0, sizeof(activeRegion));
        for (u32 group = 0; group < bossCount; ++group)
        {
            const vm_net_mock_monster_admin_row *monster = bosses[group];
            char regionUtf8[192];
            char locationUtf8[192];
            char bossUtf8[128];
            u32 groupStart = sheetRow;
            u32 groupRows = vm_mock_admin_boss_export_equipment_drop_count(monster);
            u32 style = (group & 1u) == 0 ? 2u : 3u;
            bool sameRegion = false;
            bool first = true;

            memset(regionUtf8, 0, sizeof(regionUtf8));
            memset(locationUtf8, 0, sizeof(locationUtf8));
            memset(bossUtf8, 0, sizeof(bossUtf8));
            vm_mock_admin_boss_export_scene_labels(monster->firstScene,
                                                   regionUtf8, sizeof(regionUtf8),
                                                   locationUtf8, sizeof(locationUtf8));
            sameRegion = regionActive &&
                         strcmp(activeRegion, regionUtf8) == 0;
            if (!sameRegion)
            {
                if (regionActive && regionEnd > regionStart &&
                    !vm_mock_admin_xlsx_bytes_appendf(
                        &merges, "<mergeCell ref=\"A%u:A%u\"/>",
                        regionStart, regionEnd))
                {
                    vm_mock_admin_xlsx_bytes_free(&merges);
                    goto done;
                }
                if (regionActive && regionEnd > regionStart)
                    ++mergeCount;
                snprintf(activeRegion, sizeof(activeRegion), "%s", regionUtf8);
                regionStart = groupStart;
                regionActive = true;
            }
            regionEnd = groupStart + groupRows - 1u;
            vm_net_mock_gbk_label_to_utf8(monster->displayName, bossUtf8,
                                          sizeof(bossUtf8));
            if (bossUtf8[0] == 0)
                snprintf(bossUtf8, sizeof(bossUtf8), "首领 #%u", monster->enemyId);
            for (u32 dropIndex = 0; dropIndex < monster->dropCount; ++dropIndex)
            {
                const vm_net_mock_monster_drop *drop = &monster->drops[dropIndex];
                const vm_net_mock_shop_catalog_item *item =
                    vm_net_mock_find_shop_catalog_item(drop->itemId);
                char itemUtf8[128];
                char itemLabel[224];

                if (item == NULL || !item->isEquip)
                    continue;
                memset(itemUtf8, 0, sizeof(itemUtf8));
                memset(itemLabel, 0, sizeof(itemLabel));
                vm_net_mock_gbk_label_to_utf8(item->name, itemUtf8,
                                              sizeof(itemUtf8));
                snprintf(itemLabel, sizeof(itemLabel), "%s（Lv.%u · 品质%u · ID %u）",
                         itemUtf8[0] != 0 ? itemUtf8 : "未命名装备",
                         vm_mock_admin_item_required_level(item), item->quality,
                         item->itemId);
                if (!vm_mock_admin_xlsx_bytes_appendf(
                        &parts[5], "<row r=\"%u\" ht=\"22\" customHeight=\"1\">",
                        sheetRow) ||
                    (first &&
                     ((!sameRegion &&
                       !vm_mock_admin_xlsx_sheet_append_text_cell(
                           &parts[5], 'A', sheetRow, style, regionUtf8)) ||
                      !vm_mock_admin_xlsx_sheet_append_text_cell(
                          &parts[5], 'B', sheetRow, style, locationUtf8) ||
                      !vm_mock_admin_xlsx_sheet_append_text_cell(
                          &parts[5], 'C', sheetRow, style, bossUtf8))) ||
                    !vm_mock_admin_xlsx_sheet_append_text_cell(
                        &parts[5], 'D', sheetRow, style, itemLabel) ||
                    !vm_mock_admin_xlsx_sheet_append_probability_cell(
                        &parts[5], sheetRow, style, drop->ratePercent) ||
                    !vm_mock_admin_xlsx_bytes_append_string(&parts[5], "</row>"))
                {
                    vm_mock_admin_xlsx_bytes_free(&merges);
                    goto done;
                }
                first = false;
                ++sheetRow;
            }
            if (groupRows > 1u)
            {
                u32 groupEnd = sheetRow - 1u;

                if (!vm_mock_admin_xlsx_bytes_appendf(
                        &merges,
                        "<mergeCell ref=\"B%u:B%u\"/><mergeCell ref=\"C%u:C%u\"/>",
                        groupStart, groupEnd, groupStart,
                        groupEnd))
                {
                    vm_mock_admin_xlsx_bytes_free(&merges);
                    goto done;
                }
                mergeCount += 2u;
            }
        }
        if (regionActive && regionEnd > regionStart)
        {
            if (!vm_mock_admin_xlsx_bytes_appendf(
                    &merges, "<mergeCell ref=\"A%u:A%u\"/>",
                    regionStart, regionEnd))
            {
                vm_mock_admin_xlsx_bytes_free(&merges);
                goto done;
            }
            ++mergeCount;
        }
        if (mergeCount != 0 &&
            (!vm_mock_admin_xlsx_bytes_appendf(&parts[5],
                                                "<mergeCells count=\"%u\">",
                                                mergeCount) ||
             !vm_mock_admin_xlsx_bytes_append(&parts[5], merges.data,
                                              merges.length) ||
             !vm_mock_admin_xlsx_bytes_append_string(&parts[5],
                                                      "</mergeCells>")))
        {
            vm_mock_admin_xlsx_bytes_free(&merges);
            goto done;
        }
        vm_mock_admin_xlsx_bytes_free(&merges);
    }
    if (!vm_mock_admin_xlsx_bytes_append_string(&parts[5],
                                                 "</sheetData></worksheet>"))
    {
        goto done;
    }
    for (u32 i = 0; i < VM_MOCK_ADMIN_BOSS_EXPORT_FILE_COUNT; ++i)
    {
        if (!vm_mock_admin_xlsx_append_zip_file(&zip, &entries[i], names[i],
                                                &parts[i]))
        {
            goto done;
        }
    }
    if (!vm_mock_admin_xlsx_finish_zip(&zip, entries,
                                       VM_MOCK_ADMIN_BOSS_EXPORT_FILE_COUNT))
    {
        goto done;
    }
    *dataOut = zip.data;
    *lengthOut = zip.length;
    zip.data = NULL;
    ok = true;

done:
    for (u32 i = 0; i < VM_MOCK_ADMIN_BOSS_EXPORT_FILE_COUNT; ++i)
        vm_mock_admin_xlsx_bytes_free(&parts[i]);
    vm_mock_admin_xlsx_bytes_free(&zip);
    free(monsters);
    return ok;
}

static int vm_mock_admin_handle_monster_boss_drop_export(
    vm_mock_service_socket client)
{
    u8 *workbook = NULL;
    u32 workbookLength = 0;
    int sent = 0;

    if (!vm_mock_admin_build_monster_boss_drop_xlsx(&workbook,
                                                    &workbookLength))
    {
        vm_mock_admin_send_response(client, "500 Internal Server Error", NULL,
                                    NULL, "首领掉落 Excel 生成失败。\n");
        return 0;
    }
    sent = vm_mock_admin_send_binary_download(
        client,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        "boss-monster-equipment-drops.xlsx", workbook, workbookLength);
    free(workbook);
    return sent;
}

static void vm_mock_admin_render_monster_drop_rows(
    vm_mock_admin_text *page, const vm_net_mock_monster_admin_row *monster)
{
    u8 visibleRows = 1;

    if (page == NULL || monster == NULL)
        return;
    if (monster->dropCount != 0)
        visibleRows = monster->dropCount;
    if (visibleRows > VM_NET_MOCK_MONSTER_DROP_MAX)
        visibleRows = VM_NET_MOCK_MONSTER_DROP_MAX;
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"monster-drop-manager\" data-monster-drop-manager data-monster-drop-cap=\"%u\"><div class=\"drop-tools\"><div class=\"drop-tool-card\"><span class=\"inventory-form-tag add\">批量添加掉落</span><label class=\"field\"><span>新增项默认概率（%%）</span><input type=\"number\" data-monster-drop-default-rate min=\"1\" max=\"100\" value=\"100\"></label><button class=\"secondary\" type=\"button\" data-monster-drop-open>多选掉落物品</button><button type=\"button\" data-monster-drop-add disabled>加入掉落（0）</button></div><div class=\"drop-tool-card\"><span class=\"inventory-form-tag remove\">管理已有掉落</span><span class=\"hint\">已配置 %u 项，可在弹窗内筛选、编辑概率或移除。</span><button class=\"secondary\" type=\"button\" data-monster-drop-current-open aria-haspopup=\"dialog\">管理已有掉落（%u）</button></div></div><div class=\"item-modal monster-current-modal\" data-monster-drop-current-modal role=\"dialog\" aria-modal=\"true\" aria-label=\"管理怪物已有掉落\" hidden><section class=\"item-picker-panel monster-current-panel\" style=\"width:min(960px,100%%)\"><div class=\"item-picker-head\"><div><h3>管理已有掉落</h3><p>修改后的概率与移除结果会随“保存怪物属性”一并提交。</p></div><button class=\"item-picker-close\" type=\"button\" data-monster-drop-current-close aria-label=\"关闭已有掉落\">×</button></div><div class=\"item-picker-tools\"><label><span>掉落分类</span><select data-monster-drop-current-category>",
        VM_NET_MOCK_MONSTER_DROP_MAX, monster->dropCount, monster->dropCount);
    vm_mock_admin_render_catalog_category_options(page, "全部掉落分类");
    vm_mock_admin_text_appendf(
        page,
        "</select></label><label class=\"field\" data-monster-drop-current-quality-field><span>装备品质</span><select data-monster-drop-current-quality>");
    vm_mock_admin_render_catalog_quality_options(page, "全部品质");
    vm_mock_admin_text_appendf(
        page,
        "</select></label></div><div class=\"npc-stock-picker-actions\"><button class=\"secondary\" type=\"button\" data-monster-drop-select-current>全选当前筛选</button><button class=\"danger\" type=\"button\" data-monster-drop-remove-current disabled>移除已选（0）</button></div><div class=\"drop-list\" id=\"monster-drop-list\" style=\"flex:1;min-height:0;overflow:auto;padding:0 20px 20px\">");
    for (u8 slot = 0; slot < VM_NET_MOCK_MONSTER_DROP_MAX; ++slot)
    {
        char pickerId[48];
        char fieldName[48];
        u32 itemId = slot < monster->dropCount ? monster->drops[slot].itemId : 0;
        u32 rate = slot < monster->dropCount ? monster->drops[slot].ratePercent : 0;
        const vm_net_mock_shop_catalog_item *item =
            itemId != 0 ? vm_net_mock_find_shop_catalog_item(itemId) : NULL;
        u32 levelRequired = vm_mock_admin_item_required_level(item);

        snprintf(pickerId, sizeof(pickerId), "monster-drop-item-%u", (u32)slot);
        snprintf(fieldName, sizeof(fieldName), "drop_item_id_%u", (u32)slot);
        vm_mock_admin_text_appendf(
            page,
            "<div class=\"drop-row\" data-drop-row data-monster-drop-row data-monster-drop-category=\"%c%u\" data-monster-drop-quality=\"%u\" data-monster-drop-level=\"%u\"%s><label class=\"stock-check\"><input type=\"checkbox\" value=\"%u\" data-monster-drop-current-item><span>选择</span></label><span class=\"drop-number\">掉落 #%u</span>",
            item != NULL && item->isEquip ? 'e' : 'i',
            item != NULL ? item->category : 0u,
            item != NULL && item->isEquip ? item->quality : 0u,
            levelRequired, slot < visibleRows ? "" : " hidden", itemId,
            (u32)slot + 1u);
        vm_mock_admin_render_item_picker_field(page, pickerId, fieldName,
                                               "物品", itemId, false);
        vm_mock_admin_text_appendf(
            page,
            "<label class=\"field\"><span>概率（%%）</span><input data-drop-rate type=\"number\" name=\"drop_rate_%u\" min=\"0\" max=\"100\" value=\"%u\" required></label>"
            "<button class=\"danger\" type=\"button\" data-drop-remove>移除</button></div>",
            (u32)slot, rate);
    }
    vm_mock_admin_text_appendf(
        page,
        "</div><p class=\"hint\" style=\"margin:0 20px 16px\">每条掉落独立按概率投掷；同一物品不能重复配置。批量选择只会填入空槽位，加入后仍可逐项调整概率。保存怪物属性后才会提交本次掉落修改。</p></section></div></div>");
}

static void vm_mock_admin_render_monster_page(char *response,
                                               size_t responseCap,
                                               const char *query)
{
    enum { VM_MOCK_ADMIN_MONSTER_ROWS_MAX = 128 };
    vm_mock_admin_text page;
    vm_net_mock_monster_admin_row monsters[VM_MOCK_ADMIN_MONSTER_ROWS_MAX];
    vm_net_mock_monster_admin_row *edit = NULL;
    char monsterText[32];
    char status[16];
    char message[256];
    char nameUtf8[128];
    char sceneUtf8[192];
    u32 monsterCount = 0;
    u32 selectedMonsterId = 0;

    memset(monsters, 0, sizeof(monsters));
    memset(monsterText, 0, sizeof(monsterText));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    (void)vm_mock_admin_form_value(query, "monster", monsterText,
                                   sizeof(monsterText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    if (monsterText[0] != 0)
        (void)vm_net_mock_parse_u32_strict(monsterText, &selectedMonsterId);

    monsterCount = vm_net_mock_monster_admin_list(
        monsters, VM_MOCK_ADMIN_MONSTER_ROWS_MAX);
    if (monsterCount > VM_MOCK_ADMIN_MONSTER_ROWS_MAX)
        monsterCount = VM_MOCK_ADMIN_MONSTER_ROWS_MAX;
    if (selectedMonsterId == 0 && monsterCount != 0)
        selectedMonsterId = monsters[0].enemyId;
    for (u32 i = 0; i < monsterCount; ++i)
    {
        if (monsters[i].enemyId == selectedMonsterId)
        {
            edit = &monsters[i];
            break;
        }
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 怪物管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100%%;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1320px;height:100vh;margin:auto;padding:22px 18px;display:flex;flex-direction:column}.head{display:flex;justify-content:space-between;gap:16px;align-items:flex-start}.head h1{font-size:24px;margin:0}.sub{color:#667085;margin:4px 0 14px}.tabs{display:flex;gap:6px;margin-bottom:16px;flex-wrap:wrap}.tab{padding:8px 13px;border:1px solid #e4e7ec;border-radius:7px;background:#fff;color:#475467;text-decoration:none}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:#fff;color:#667085;border:1px solid #d0d5dd}.grid{display:grid;grid-template-columns:340px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:15px;box-shadow:0 1px 2px #1018280d}.catalog{display:flex;flex-direction:column;min-height:0}.search{margin-bottom:10px}.monster-export{display:flex;align-items:center;justify-content:center;margin:10px 0 0;padding:8px 10px;border-radius:6px;background:#175cd3;color:#fff;text-decoration:none;font-weight:650}.monster-export:hover{background:#1849a9}.monster-batch-tools{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin:10px 0 2px}.monster-batch-tools form{display:contents}.monster-batch-tools button{padding:7px 8px;font-size:12px}.monster-batch-tools [data-monster-batch-reset]{grid-column:1/-1}.list{overflow:auto;display:flex;flex-direction:column;gap:4px;margin-top:9px}.monster-row{display:flex;align-items:stretch;gap:3px}.monster-select{display:grid;place-items:center;padding:0 3px}.monster-select input{width:auto;padding:0}.monster{flex:1;padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none;border:1px solid transparent}.monster:hover,.monster.on{background:#eef4ff;color:#175cd3}.monster small{display:block;color:#667085}.monster.override{border-color:#fdb022}.editor{overflow:auto}.badge{font-size:12px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#175cd3}.badge.override{background:#fffaeb;color:#b54708}.notice{padding:10px 12px;border-radius:7px;margin-bottom:13px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.summary{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 16px}.chip{padding:3px 8px;border-radius:999px;background:#f2f4f7;color:#475467}.fields{display:grid;grid-template-columns:repeat(4,minmax(110px,1fr));gap:10px}.field,.item-field{display:grid;gap:4px}.field span,.item-field>span{font-size:12px;color:#667085}.group{padding:13px;border:1px solid #e4e7ec;border-radius:8px;margin-top:13px}.group h2{font-size:16px;margin:0 0 10px}input,select{width:100%%;min-width:0;padding:8px 9px;border:1px solid #d0d5dd;border-radius:6px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer}button:disabled{cursor:not-allowed;opacity:.55}.secondary{background:#475467}.danger{background:#b42318}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:13px}.hint{color:#667085;font-size:12px;margin:8px 0 0}.monster-drop-manager{display:grid;gap:12px}.drop-tools{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.drop-tool-card{display:grid;grid-template-columns:minmax(116px,.8fr) minmax(130px,1fr) auto auto;gap:8px;align-items:end;padding:10px;border:1px solid #d0d5dd;border-radius:8px;background:#f8fafc}.inventory-form-tag{font-weight:700;color:#344054;padding-bottom:8px}.inventory-form-tag.add{color:#175cd3}.inventory-form-tag.remove{color:#b42318}.drop-list{display:grid;gap:9px}.drop-row{display:grid;grid-template-columns:72px 64px minmax(220px,1fr) 135px auto;gap:9px;align-items:end;padding:10px;border:1px solid #e4e7ec;border-radius:8px}.drop-number{font-size:12px;color:#667085;padding-bottom:8px}.stock-check{display:flex;align-items:center;gap:5px;padding-bottom:9px;color:#667085;font-size:12px}.stock-check input{width:auto;padding:0}.item-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px}.item-picker-trigger small{color:#667085;font-weight:400}.item-picker-head-actions,.npc-stock-picker-actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.npc-stock-picker-actions{padding:0 20px 12px}.item-picker-head-actions #item-picker-clear,.item-picker-head-actions #monster-drop-picker-clear{background:#f2f4f7;color:#475467}.item-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899}.item-picker-panel{width:min(780px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.item-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.item-picker-head h3{font-size:19px;margin:0}.item-picker-head p{margin:2px 0 0;color:#667085}.item-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.item-picker-tools{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;padding:14px 20px 10px}.item-picker-tools label{display:grid;gap:4px}.item-picker-tools label>span{font-size:12px;color:#667085}.item-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.item-picker-error{color:#b42318;font-weight:600}.item-picker-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:140px;overflow:auto;padding:0 20px 20px}.item-choice{display:grid;gap:2px;padding:10px 12px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.monster-drop-choice{grid-template-columns:auto minmax(0,1fr);align-items:start}.monster-drop-choice input{width:auto;margin-top:4px}.item-choice:hover,.item-choice.selected{border-color:#84adff;background:#f5f8ff}.item-choice strong{font-size:14px}.item-choice span,.item-choice small{color:#667085;font-size:12px}.item-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}[hidden]{display:none!important}.modal-open{overflow:hidden}@media(max-width:1050px){.drop-tools{grid-template-columns:1fr}.drop-tool-card{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto}.grid{grid-template-columns:1fr}.catalog{max-height:420px}.fields{grid-template-columns:1fr 1fr}.drop-row,.drop-tool-card,.item-picker-tools,.item-picker-list{grid-template-columns:1fr}}</style><script src=\"/admin.js\" defer></script>"
        "</head><body><main class=\"wrap\"><div class=\"head\"><div><h1>江湖OL 后台管理</h1><p class=\"sub\">怪物属性、战斗奖励与掉落覆盖</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\">退出登录</button></form></div>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab on\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a><a class=\"tab\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>"
        "<div class=\"grid\"><aside class=\"card catalog\" data-monster-batch-manager><input class=\"search\" id=\"monster-search\" placeholder=\"按 ID、名称或场景筛选\"><strong>怪物目录（%u）</strong><a class=\"monster-export\" href=\"/monster-boss-drops.xlsx\" download>导出首领掉落 Excel</a><div class=\"monster-batch-tools\"><button class=\"secondary\" type=\"button\" data-monster-batch-select-visible>全选当前筛选</button><button class=\"secondary\" type=\"button\" data-monster-batch-clear>清除选择</button><form method=\"post\" action=\"/action\" data-monster-action data-confirm-message=\"将按各自当前等级与类型重新计算所选怪物的 HP、MP、攻击、防御；经验、铜钱与物品掉落不变。是否继续？\"><input type=\"hidden\" name=\"action\" value=\"reset-monster-combat-stats-bulk\"><input type=\"hidden\" name=\"monster_id\" value=\"%u\"><input type=\"hidden\" name=\"monster_ids\" value=\"\" data-monster-batch-ids><button class=\"danger\" type=\"submit\" data-monster-batch-reset disabled>批量重置四项属性（0）</button></form></div><div class=\"list\" id=\"monster-list\" data-admin-list>",
        monsterCount, selectedMonsterId);

    for (u32 i = 0; i < monsterCount; ++i)
    {
        char rowNameUtf8[128];
        char rowSceneUtf8[192];

        memset(rowNameUtf8, 0, sizeof(rowNameUtf8));
        memset(rowSceneUtf8, 0, sizeof(rowSceneUtf8));
        vm_net_mock_gbk_label_to_utf8(monsters[i].displayName, rowNameUtf8,
                                      sizeof(rowNameUtf8));
        vm_net_mock_gbk_label_to_utf8(monsters[i].firstScene, rowSceneUtf8,
                                      sizeof(rowSceneUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"monster-row\" data-monster-row><label class=\"monster-select\" title=\"选择 #%u 用于批量重置\"><input type=\"checkbox\" value=\"%u\" data-monster-batch-item></label><a class=\"monster%s%s\" data-admin-select%s data-key=\"%u ",
            monsters[i].enemyId, monsters[i].enemyId,
            monsters[i].enemyId == selectedMonsterId ? " on" : "",
            monsters[i].overridden ? " override" : "",
            monsters[i].enemyId == selectedMonsterId ? " aria-current=\"page\"" : "",
            monsters[i].enemyId);
        vm_mock_admin_text_append_html(&page, rowNameUtf8);
        vm_mock_admin_text_appendf(&page, " ");
        vm_mock_admin_text_append_html(&page, rowSceneUtf8);
        vm_mock_admin_text_appendf(
            &page, "\" href=\"/?tab=monsters&amp;monster=%u\"><strong>#%u · ",
            monsters[i].enemyId, monsters[i].enemyId);
        if (rowNameUtf8[0] != 0)
            vm_mock_admin_text_append_html(&page, rowNameUtf8);
        else
            vm_mock_admin_text_appendf(&page, "未命名怪物");
        vm_mock_admin_text_appendf(
            &page, "</strong><small>Lv.%u · %s%s</small></a></div>",
            monsters[i].level,
            vm_mock_admin_monster_family_name(monsters[i].family),
            monsters[i].overridden ? " · 已编辑" : "");
    }
    vm_mock_admin_text_appendf(
        &page,
        "</div><form class=\"actions\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"reset-monster-scene-levels-bulk\"><input type=\"hidden\" name=\"monster_id\" value=\"%u\"><input type=\"hidden\" name=\"monster_ids\" value=\"\" data-monster-batch-ids><button class=\"danger\" type=\"submit\" data-monster-batch-level-reset disabled>按场景等级重置（0）</button></form><p class=\"hint\">等级来源：sMap.dsh 的“怪物等级”。范围取中位数；普通怪优先按 automonster 的首个场景，SCE 专属怪按首个战斗场景。该操作会调整等级、四项属性、经验和铜钱；物品掉落不变。</p></aside><section class=\"card editor\" data-admin-detail>",
        selectedMonsterId);
    vm_mock_admin_text_appendf(&page, "<div data-monster-action-status>");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(&page, "</div>");
    if (edit == NULL)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<p>没有可编辑的怪物。</p></section></div></main></body></html>");
        return;
    }

    vm_net_mock_gbk_label_to_utf8(edit->displayName, nameUtf8,
                                  sizeof(nameUtf8));
    vm_net_mock_gbk_label_to_utf8(edit->firstScene, sceneUtf8,
                                  sizeof(sceneUtf8));
    vm_mock_admin_text_appendf(&page, "<h2>#%u · ", edit->enemyId);
    if (nameUtf8[0] != 0)
        vm_mock_admin_text_append_html(&page, nameUtf8);
    else
        vm_mock_admin_text_appendf(&page, "未命名怪物");
    vm_mock_admin_text_appendf(
        &page, " <span class=\"badge%s\">%s</span></h2><div class=\"summary\"><span class=\"chip\">出现位置／来源：",
        edit->overridden ? " override" : "",
        edit->overridden ? "MySQL 覆盖" : "服务端默认");
    if (sceneUtf8[0] != 0)
        vm_mock_admin_text_append_html(&page, sceneUtf8);
    else
        vm_mock_admin_text_appendf(&page, "任务／特殊挑战目录");
    vm_mock_admin_text_appendf(
        &page,
        "</span><span class=\"chip\">类型：%s</span></div><form method=\"post\" action=\"/action\" data-monster-action><input type=\"hidden\" name=\"action\" value=\"save-monster\"><input type=\"hidden\" name=\"monster_id\" value=\"%u\"><div class=\"group\"><h2>基础配置</h2><div class=\"fields\"><label class=\"field\"><span>怪物 ID（只读）</span><input value=\"%u\" readonly></label><label class=\"field\"><span>等级</span><input type=\"number\" name=\"level\" min=\"1\" max=\"255\" value=\"%u\" required></label><label class=\"field\"><span>怪物类型</span>",
        vm_mock_admin_monster_family_name(edit->family), edit->enemyId,
        edit->enemyId, edit->level);
    vm_mock_admin_render_monster_family_select(&page, edit->family);
    vm_mock_admin_text_appendf(
        &page,
        "</label><label class=\"field\"><span>属性来源</span><input value=\"%s\" readonly></label></div></div>"
        "<div class=\"group\"><h2>战斗属性</h2><div class=\"fields\"><label class=\"field\"><span>HP</span><input type=\"number\" name=\"hp\" min=\"1\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>MP</span><input type=\"number\" name=\"mp\" min=\"1\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>攻击</span><input type=\"number\" name=\"attack\" min=\"1\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>防御</span><input type=\"number\" name=\"defense\" min=\"0\" max=\"2147483647\" value=\"%u\" required></label></div></div>"
        "<div class=\"group\"><h2>结算奖励</h2><div class=\"fields\"><label class=\"field\"><span>经验奖励</span><input type=\"number\" name=\"exp\" min=\"0\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>铜钱奖励</span><input type=\"number\" name=\"gold\" min=\"0\" max=\"2147483647\" value=\"%u\" required></label></div></div>"
        "<div class=\"group\"><h2>物品掉落</h2>",
        edit->overridden ? "MySQL 覆盖" : "服务端公式",
        edit->hp, edit->mp, edit->attack, edit->defense, edit->exp, edit->gold);
    vm_mock_admin_render_monster_drop_rows(&page, edit);
    vm_mock_admin_text_appendf(
        &page,
        "</div><p class=\"hint\">保存后立即影响普通场景战斗、副本挑战、挂机战斗和结算。怪物名称来自真实 SCE，只读；调整等级或类型不会擅自覆盖手工填写的战斗数值。首领为单体高强度战斗，其血量、攻击和防御按同等级品质 0 装备参照计算；任何玩家都可挑战，但同级单人不应能以常规战斗完成击杀。</p><div class=\"actions\"><button type=\"submit\">保存怪物属性</button></div></form>");
    vm_mock_admin_render_item_picker_modal(&page, false);
    vm_mock_admin_render_monster_drop_picker_modal(&page);
    vm_mock_admin_text_appendf(
        &page,
        "<form class=\"actions\" method=\"post\" action=\"/action\" data-monster-action data-confirm-message=\"按当前等级和怪物类型重新计算 HP、MP、攻击、防御；经验、铜钱、物品掉落与其他配置保持不变。是否继续？\"><input type=\"hidden\" name=\"action\" value=\"reset-monster-combat-stats\"><input type=\"hidden\" name=\"monster_id\" value=\"%u\"><button class=\"secondary\" type=\"submit\">重置四项属性</button></form>",
        edit->enemyId);
    if (edit->overridden)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<form class=\"actions\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"reset-monster\"><input type=\"hidden\" name=\"monster_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">恢复服务端默认</button></form>",
            edit->enemyId);
    }
    vm_mock_admin_text_appendf(
        &page,
        "</section></div></main></body></html>");
    if (page.truncated)
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>怪物管理页面超过大小限制。</p>");
}
