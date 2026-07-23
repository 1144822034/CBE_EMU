static u32 vm_net_mock_load_xse_resource(const char *scriptName,
                                         u8 *out, u32 outCap)
{
    char path[256];
    u8 raw[8192];
    u32 rawLen = 0;
    u32 decodedLen = 0;

    if (scriptName == NULL || scriptName[0] == 0 || out == NULL || outCap < 16 ||
        !vm_net_mock_open_server_data_resource(scriptName, ".xse", NULL,
                                               path, sizeof(path)))
    {
        return 0;
    }
    rawLen = vm_net_mock_load_response_file(path, raw, sizeof(raw));
    if (rawLen == 0)
        return 0;

    if (rawLen > 4)
    {
        u32 declaredLen = vm_net_mock_read_le32_at(raw, 0);
        if (declaredLen != 0 && declaredLen <= rawLen - 4 &&
            (raw[4] == 1 || raw[4] == 2))
        {
            decodedLen = vm_net_mock_decode_lzss_resource_stream(raw + 4,
                                                                 declaredLen,
                                                                 out,
                                                                 outCap);
            if (decodedLen >= 16 && memcmp(out, "XSE0", 4) == 0)
                return decodedLen;
            return 0;
        }
    }
    if (rawLen >= 16 && memcmp(raw, "XSE0", 4) == 0)
    {
        if (rawLen > outCap)
            return 0;
        memcpy(out, raw, rawLen);
        return rawLen;
    }
    decodedLen = vm_net_mock_decode_lzss_resource_stream(raw, rawLen, out, outCap);
    if (decodedLen >= 16 && memcmp(out, "XSE0", 4) == 0)
        return decodedLen;
    return 0;
}

static bool vm_net_mock_read_sce_string_field(const u8 *data, u32 len, u32 *pos,
                                              u16 expectedField, char *out, size_t outCap)
{
    if (data == NULL || pos == NULL || out == NULL || outCap == 0 || *pos + 5 > len)
        return false;
    if (vm_net_mock_read_le16_at(data, *pos) != 3 ||
        vm_net_mock_read_le16_at(data, *pos + 2) != expectedField)
    {
        return false;
    }
    *pos += 4;
    return vm_net_mock_read_sce_len_string(data, len, pos, out, outCap);
}

static bool vm_net_mock_read_sce_scalar_token(const u8 *data, u32 len, u32 *pos,
                                              u16 *fieldOut, u16 *valueOut)
{
    if (data == NULL || pos == NULL || fieldOut == NULL || valueOut == NULL ||
        *pos + 6 > len || vm_net_mock_read_le16_at(data, *pos) != 1)
    {
        return false;
    }
    *fieldOut = vm_net_mock_read_le16_at(data, *pos + 2);
    *valueOut = vm_net_mock_read_le16_at(data, *pos + 4);
    *pos += 6;
    return true;
}

static bool vm_net_mock_parse_sce_interactive_npc_at(const u8 *data, u32 len, u32 off,
                                                     vm_net_mock_scene_npcinfo_seed *seedOut,
                                                     u32 *endOut)
{
    vm_net_mock_scene_npcinfo_seed seed;
    u32 pos = off;
    u16 field = 0;
    u16 value = 0;

    if (data == NULL || seedOut == NULL || off + 8 > len)
        return false;
    memset(&seed, 0, sizeof(seed));
    seed.kind = vm_net_mock_read_le16_at(data, pos);
    if (seed.kind > 32)
        return false;
    pos += 2;
    if (!vm_net_mock_read_sce_string_field(data, len, &pos, 3,
                                           seed.actorResource, sizeof(seed.actorResource)) ||
        !vm_net_mock_str_ends_with(seed.actorResource, ".actor"))
    {
        return false;
    }
    if (!vm_net_mock_read_sce_string_field(data, len, &pos, 4,
                                           seed.scriptName, sizeof(seed.scriptName)) ||
        !vm_net_mock_str_ends_with(seed.scriptName, ".xse"))
    {
        return false;
    }

    /* SCE field 2 is a local state marker (commonly "0:"). It is not part of
     * the server npcinfo row, but it must be consumed before field 1. */
    if (pos + 4 <= len && vm_net_mock_read_le16_at(data, pos) == 3 &&
        vm_net_mock_read_le16_at(data, pos + 2) == 2)
    {
        char stateText[32];
        if (!vm_net_mock_read_sce_string_field(data, len, &pos, 2,
                                               stateText, sizeof(stateText)))
            return false;
    }
    if (!vm_net_mock_read_sce_string_field(data, len, &pos, 1,
                                           seed.displayName, sizeof(seed.displayName)) ||
        seed.displayName[0] == 0)
    {
        return false;
    }

    if (pos + 6 > len)
        return false;
    if (vm_net_mock_read_le16_at(data, pos) == 1)
    {
        if (!vm_net_mock_read_sce_scalar_token(data, len, &pos, &field, &value))
            return false;
        if (field == 0x18 && value <= 8)
        {
            seed.orientation = value;
            if (pos + 6 > len)
                return false;
            if (vm_net_mock_read_le16_at(data, pos) == 1)
            {
                if (!vm_net_mock_read_sce_scalar_token(data, len, &pos, &field, &value))
                    return false;
            }
            else if (vm_net_mock_read_le16_at(data, pos) == 2)
            {
                field = vm_net_mock_read_le16_at(data, pos + 2);
                value = vm_net_mock_read_le16_at(data, pos + 4);
                pos += 6;
            }
            else
            {
                return false;
            }
        }
    }
    else if (vm_net_mock_read_le16_at(data, pos) == 2)
    {
        field = vm_net_mock_read_le16_at(data, pos + 2);
        value = vm_net_mock_read_le16_at(data, pos + 4);
        pos += 6;
    }
    else
    {
        return false;
    }
    seed.x = field;
    seed.y = value;
    if (seed.x == 0 || seed.y == 0)
        return false;

    /* The client copies these strings into fixed 30/30/32-byte row fields. */
    if (strlen(seed.displayName) >= 30 || strlen(seed.actorResource) >= 30 ||
        strlen(seed.scriptName) >= 32)
    {
        return false;
    }

    /* bin/JHOnlineData is a writable client cache and may be empty before the
     * service starts. SCE rows therefore validate against the clean server
     * download source. Keep the proven n_girl.actor compatibility mapping
     * explicit: that legacy visual crashed in the current renderer, while
     * n_woman1.actor is its compatible current equivalent. */
    {
        const char *replacement = NULL;

        if (strcmp(seed.actorResource, "n_girl.actor") == 0)
            replacement = "n_woman1.actor";
        if (replacement != NULL)
        {
            if (!vm_net_mock_open_server_data_resource(replacement, ".actor",
                                                       NULL, NULL, 0))
            {
                printf("[error][network] mock_scene_npc_actor_resource_missing npc=%s actor=%s script=%s action=skip-row\n",
                       seed.displayName, replacement, seed.scriptName);
                return false;
            }
            printf("[info][network] mock_scene_npc_actor_resource_alias npc=%s legacy=%s current=%s script=%s evidence=compatible-server-resource\n",
                   seed.displayName, seed.actorResource, replacement, seed.scriptName);
            snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", replacement);
        }
        else if (!vm_net_mock_open_server_data_resource(seed.actorResource, ".actor",
                                                        NULL, NULL, 0))
        {
            printf("[error][network] mock_scene_npc_actor_resource_missing npc=%s actor=%s script=%s action=skip-row\n",
                   seed.displayName, seed.actorResource, seed.scriptName);
            return false;
        }
    }
    *seedOut = seed;
    if (endOut)
        *endOut = pos;
    return true;
}

typedef struct
{
    u32 actorId;
    u16 x;
    u16 y;
    char displayName[32];
    char actorResource[64];
} vm_net_mock_sce_combat_spawn;

static bool vm_net_mock_parse_sce_combat_spawn_at(
    const u8 *data, u32 len, u32 off,
    vm_net_mock_sce_combat_spawn *spawnOut, u32 *endOut)
{
    vm_net_mock_sce_combat_spawn spawn;
    u32 pos = off;
    u16 metaKind = 0;
    u16 field = 0;
    u16 value = 0;

    if (data == NULL || spawnOut == NULL || off + 14 > len ||
        vm_net_mock_read_le16_at(data, off) != 3)
    {
        return false;
    }

    memset(&spawn, 0, sizeof(spawn));
    pos += 2;
    spawn.x = vm_net_mock_read_le16_at(data, pos);
    spawn.y = vm_net_mock_read_le16_at(data, pos + 2);
    pos += 4;
    if (spawn.x == 0 || spawn.y == 0 || pos + 8 > len)
        return false;

    /* SCE2 combat actor record recovered from the real scene payload:
     *   kind=3, x, y,
     *   meta token 5/6 { field 14 = actor id },
     *   string field 15 = display name,
     *   scalar field 16 = visual/class hint,
     *   string field 17 = actor resource.
     */
    metaKind = vm_net_mock_read_le16_at(data, pos);
    if ((metaKind != 5 && metaKind != 6) ||
        vm_net_mock_read_le16_at(data, pos + 4) != 0x0e)
    {
        return false;
    }
    spawn.actorId = vm_net_mock_read_le16_at(data, pos + 6);
    pos += 8;
    if (spawn.actorId == 0 ||
        !vm_net_mock_read_sce_string_field(data, len, &pos, 0x0f,
                                           spawn.displayName,
                                           sizeof(spawn.displayName)) ||
        spawn.displayName[0] == 0 ||
        !vm_net_mock_read_sce_scalar_token(data, len, &pos, &field, &value) ||
        field != 0x10 ||
        !vm_net_mock_read_sce_string_field(data, len, &pos, 0x11,
                                           spawn.actorResource,
                                           sizeof(spawn.actorResource)) ||
        !vm_net_mock_str_ends_with(spawn.actorResource, ".actor"))
    {
        return false;
    }

    *spawnOut = spawn;
    if (endOut)
        *endOut = pos;
    return true;
}

static bool vm_net_mock_select_sce_combat_spawn(const char *scene, u32 actorId,
                                                 u32 *indexOut,
                                                 u32 *posxOut,
                                                 u32 *posyOut)
{
    u8 data[8192];
    char resourceScene[64];
    char legacyScene[64];

    if (scene == NULL || scene[0] == 0 || actorId == 0)
        return false;

    snprintf(resourceScene, sizeof(resourceScene), "%s", scene);
    legacyScene[0] = 0;
    for (u32 resourcePass = 0; resourcePass < 2; ++resourcePass)
    {
        u32 len = vm_net_mock_load_scene_resource(resourceScene, data, sizeof(data));
        u32 start = vm_net_mock_scene_payload_start(data, len);
        u32 spawnIndex = 0;

        if (len != 0 && start != 0)
        {
            for (u32 off = start; off + 14 <= len; ++off)
            {
                vm_net_mock_sce_combat_spawn spawn;
                u32 end = 0;

                if (!vm_net_mock_parse_sce_combat_spawn_at(data, len, off,
                                                           &spawn, &end))
                {
                    continue;
                }
                /* Scene actor slot zero is reserved by the runtime; recovered
                 * combat spawns occupy subsequent slots in SCE record order.
                 * Autonomous battle creation has no client-selected node, so
                 * it uses the same SCE record catalog that originally creates
                 * the live scene nodes.  Normal collision challenges must not
                 * use this selector: their request already identifies the
                 * exact live node selected by the client. */
                ++spawnIndex;
                if (spawn.actorId == actorId)
                {
                    if (indexOut)
                        *indexOut = spawnIndex;
                    if (posxOut)
                        *posxOut = spawn.x;
                    if (posyOut)
                        *posyOut = spawn.y;
                    printf("[info][network] mock_scene_monster_target scene=%s resource_scene=%s actor=%u index=%u pos=(%u,%u) name=%s actor_resource=%s source=SCE2-combat-spawn\n",
                           scene, resourceScene, actorId, spawnIndex,
                           spawn.x, spawn.y, spawn.displayName,
                           spawn.actorResource);
                    return true;
                }
                if (end > off)
                    off = end - 1;
            }
        }

        if (resourcePass != 0 ||
            !vm_net_mock_scene_resource_legacy_alias(scene, legacyScene,
                                                     sizeof(legacyScene)) ||
            strcmp(resourceScene, legacyScene) == 0)
        {
            break;
        }
        snprintf(resourceScene, sizeof(resourceScene), "%s", legacyScene);
    }
    return false;
}

typedef struct
{
    char displayName[32];
    char firstScene[64];
} vm_net_mock_monster_resource_label;

static vm_net_mock_monster_resource_label
    g_vm_net_mock_monster_resource_labels[
        sizeof(g_vm_net_mock_monster_entries) /
        sizeof(g_vm_net_mock_monster_entries[0])];
static bool g_vm_net_mock_monster_resource_labels_loaded = false;

static void vm_net_mock_monster_resource_labels_load(void)
{
    u32 catalogCount = 0;

    if (g_vm_net_mock_monster_resource_labels_loaded)
        return;
    g_vm_net_mock_monster_resource_labels_loaded = true;
    memset(g_vm_net_mock_monster_resource_labels, 0,
           sizeof(g_vm_net_mock_monster_resource_labels));
    catalogCount = vm_net_mock_load_auto_monster_catalog();

    for (u32 catalogIndex = 0; catalogIndex < catalogCount; ++catalogIndex)
    {
        const vm_net_mock_auto_monster_catalog_item *catalog =
            &g_vm_net_mock_auto_monster_catalog[catalogIndex];
        u8 data[8192];
        char resourceScene[64];
        char legacyScene[64];

        snprintf(resourceScene, sizeof(resourceScene), "%s", catalog->scene);
        legacyScene[0] = 0;
        for (u32 idIndex = 0; idIndex < 3; ++idIndex)
        {
            int monsterIndex = vm_net_mock_monster_catalog_index(
                catalog->monsterIds[idIndex]);
            if (monsterIndex >= 0 &&
                g_vm_net_mock_monster_resource_labels[monsterIndex]
                    .firstScene[0] == 0)
            {
                snprintf(g_vm_net_mock_monster_resource_labels[monsterIndex]
                             .firstScene,
                         sizeof(g_vm_net_mock_monster_resource_labels[monsterIndex]
                                    .firstScene),
                         "%s", catalog->scene);
            }
        }

        for (u32 resourcePass = 0; resourcePass < 2; ++resourcePass)
        {
            u32 len = vm_net_mock_load_scene_resource(resourceScene, data,
                                                       sizeof(data));
            u32 start = vm_net_mock_scene_payload_start(data, len);

            if (len != 0 && start != 0)
            {
                for (u32 off = start; off + 14 <= len; ++off)
                {
                    vm_net_mock_sce_combat_spawn spawn;
                    u32 end = 0;
                    int monsterIndex = -1;

                    if (!vm_net_mock_parse_sce_combat_spawn_at(
                            data, len, off, &spawn, &end))
                    {
                        continue;
                    }
                    monsterIndex = vm_net_mock_monster_catalog_index(spawn.actorId);
                    if (monsterIndex >= 0 &&
                        g_vm_net_mock_monster_resource_labels[monsterIndex]
                            .displayName[0] == 0)
                    {
                        snprintf(g_vm_net_mock_monster_resource_labels[monsterIndex]
                                     .displayName,
                                 sizeof(g_vm_net_mock_monster_resource_labels[monsterIndex]
                                            .displayName),
                                 "%s", spawn.displayName);
                    }
                    if (end > off)
                        off = end - 1;
                }
            }
            if (resourcePass != 0 ||
                !vm_net_mock_scene_resource_legacy_alias(
                    catalog->scene, legacyScene, sizeof(legacyScene)) ||
                strcmp(resourceScene, legacyScene) == 0)
            {
                break;
            }
            snprintf(resourceScene, sizeof(resourceScene), "%s", legacyScene);
        }
    }
}

static u32 vm_net_mock_monster_admin_list(
    vm_net_mock_monster_admin_row *rows, u32 rowCap)
{
    u32 total = (u32)(sizeof(g_vm_net_mock_monster_entries) /
                      sizeof(g_vm_net_mock_monster_entries[0]));
    u32 copied = vm_net_mock_min_u32(total, rowCap);

    if (rows == NULL || rowCap == 0)
        return total;
    (void)vm_net_mock_monster_db_load();
    vm_net_mock_monster_resource_labels_load();
    memset(rows, 0, sizeof(*rows) * copied);

    for (u32 i = 0; i < copied; ++i)
    {
        const vm_net_mock_monster_entry *entry =
            &g_vm_net_mock_monster_entries[i];
        vm_net_mock_monster_stats stats =
            vm_net_mock_monster_stats_for_enemy(entry->enemyId);
        const vm_net_mock_monster_override *override =
            &g_vm_net_mock_monster_overrides[i];

        rows[i].enemyId = stats.enemyId;
        rows[i].level = stats.level;
        rows[i].family = override->used ? override->family : entry->family;
        rows[i].hp = stats.hp;
        rows[i].mp = stats.mp;
        rows[i].attack = stats.attack;
        rows[i].defense = stats.defense;
        rows[i].exp = stats.exp;
        rows[i].gold = stats.gold;
        rows[i].dropItemId = stats.dropItemId;
        rows[i].dropRatePercent = stats.dropRatePercent;
        rows[i].overridden = override->used;
        snprintf(rows[i].displayName, sizeof(rows[i].displayName), "%s",
                 g_vm_net_mock_monster_resource_labels[i].displayName);
        snprintf(rows[i].firstScene, sizeof(rows[i].firstScene), "%s",
                 g_vm_net_mock_monster_resource_labels[i].firstScene);
    }
    return total;
}

static u32 vm_net_mock_scene_npcinfo_hash(const char *scene,
                                          const vm_net_mock_scene_npcinfo_seed *seed)
{
    u32 hash = 2166136261u;
    const char *parts[3];
    u8 coords[4];

    parts[0] = scene ? scene : "";
    parts[1] = seed ? seed->scriptName : "";
    parts[2] = seed ? seed->displayName : "";
    for (u32 part = 0; part < 3; ++part)
    {
        const u8 *p = (const u8 *)parts[part];
        while (*p)
        {
            hash ^= *p++;
            hash *= 16777619u;
        }
        hash ^= 0xffu;
        hash *= 16777619u;
    }
    coords[0] = (u8)(seed->x >> 8);
    coords[1] = (u8)seed->x;
    coords[2] = (u8)(seed->y >> 8);
    coords[3] = (u8)seed->y;
    for (u32 i = 0; i < sizeof(coords); ++i)
    {
        hash ^= coords[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool vm_net_mock_scene_is_linan_south_gate(const char *scene)
{
    return scene != NULL &&
           vm_net_mock_scene_names_equal_loose(
               scene,
               "\x63\x30\x34\xc1\xd9\xb0\xb2\xb8\xae\x5f\x30\x31"); /* c04临安府_01 */
}

enum
{
    VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX = 256
};

typedef struct
{
    bool enabled;
    char scene[64];
    vm_net_mock_scene_npcinfo_seed seed;
} vm_net_mock_dynamic_npc_override;

typedef struct
{
    vm_net_mock_scene_npcinfo_seed seed;
    bool enabled;
    bool builtin;
    bool overridden;
} vm_net_mock_dynamic_npc_admin_row;

typedef struct
{
    u32 loaded;
    u32 skipped;
    u32 quarantined;
} vm_net_mock_dynamic_npc_load_context;

static vm_net_mock_dynamic_npc_override
    g_vm_net_mock_dynamic_npc_overrides[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
static u32 g_vm_net_mock_dynamic_npc_override_count = 0;
static bool g_vm_net_mock_dynamic_npc_db_loaded = false;
static bool g_vm_net_mock_dynamic_npc_db_valid = false;

static bool vm_net_mock_dynamic_npc_decode_hex(const char *value, size_t valueLen,
                                               char *out, size_t outCap)
{
    size_t decodedLen = 0;

    if (value == NULL || out == NULL || outCap < 2 ||
        !vm_mysql_hex_decode(value, valueLen, out, outCap - 1, &decodedLen) ||
        decodedLen >= outCap)
    {
        return false;
    }
    out[decodedLen] = 0;
    return true;
}

static bool vm_net_mock_dynamic_npc_row(void *contextValue,
                                       unsigned int columnCount,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_net_mock_dynamic_npc_load_context *context =
        (vm_net_mock_dynamic_npc_load_context *)contextValue;
    vm_net_mock_dynamic_npc_override row;
    u32 number[11];

    memset(&row, 0, sizeof(row));
    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 16 ||
        g_vm_net_mock_dynamic_npc_override_count >= VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX ||
        !vm_net_mock_dynamic_npc_decode_hex(values[0], lengths[0],
                                            row.scene, sizeof(row.scene)) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &number[0]) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &number[1]) || number[1] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &number[2]) || number[2] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &number[3]) ||
        number[3] > VM_NET_MOCK_NPC_KIND_MAX ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &number[4]) || number[4] > 0xffffu ||
        !vm_net_mock_dynamic_npc_decode_hex(values[6], lengths[6],
                                            row.seed.actorResource, sizeof(row.seed.actorResource)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[7], lengths[7],
                                            row.seed.displayName, sizeof(row.seed.displayName)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[8], lengths[8],
                                            row.seed.scriptName, sizeof(row.seed.scriptName)) ||
        !vm_mock_mysql_parse_u32(values[9], lengths[9], &number[5]) || number[5] > 1u ||
        !vm_mock_mysql_parse_u32(values[10], lengths[10], &number[6]) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[11], lengths[11],
                                            row.seed.instanceScene,
                                            sizeof(row.seed.instanceScene)) ||
        !vm_mock_mysql_parse_u32(values[12], lengths[12], &number[7]) || number[7] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[13], lengths[13], &number[8]) || number[8] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[14], lengths[14], &number[9]) || number[9] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[15], lengths[15], &number[10]) || number[10] > 0xffu)
    {
        if (context != NULL)
            ++context->skipped;
        return true;
    }

    row.seed.actorId = number[0];
    row.seed.x = (u16)number[1];
    row.seed.y = (u16)number[2];
    row.seed.kind = (u16)number[3];
    row.seed.orientation = (u16)number[4];
    row.enabled = number[5] != 0;
    row.seed.taskId = number[6];
    row.seed.instanceX = (u16)number[7];
    row.seed.instanceY = (u16)number[8];
    row.seed.challengeEnemyId = number[9];
    row.seed.instanceMinLevel = (u16)number[10];
    if (row.seed.actorId == 0 || row.seed.x == 0 || row.seed.y == 0 ||
        !vm_net_mock_scene_name_is_safe(row.scene) ||
        row.seed.displayName[0] == 0 ||
        strlen(row.seed.displayName) >= 30 ||
        strlen(row.seed.actorResource) >= 30 ||
        strlen(row.seed.scriptName) >= 32 ||
        !vm_net_mock_str_ends_with(row.seed.actorResource, ".actor") ||
        (row.seed.scriptName[0] != 0 &&
         !vm_net_mock_str_ends_with(row.seed.scriptName, ".xse")) ||
        (row.seed.kind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE &&
         ((row.seed.instanceScene[0] == 0 && row.seed.challengeEnemyId == 0) ||
          (row.seed.instanceScene[0] != 0 &&
           (!vm_net_mock_scene_name_is_safe(row.seed.instanceScene) ||
            row.seed.instanceX == 0 || row.seed.instanceY == 0 ||
            !vm_net_mock_scene_resource_exists(row.seed.instanceScene))) ||
          (row.seed.challengeEnemyId != 0 &&
           !vm_net_mock_monster_enemy_id_known(row.seed.challengeEnemyId)) ||
          row.seed.instanceMinLevel == 0)) ||
        !vm_net_mock_open_server_data_resource(row.seed.actorResource, ".actor",
                                               NULL, NULL, 0) ||
        (row.seed.scriptName[0] != 0 &&
         !vm_net_mock_open_server_data_resource(row.seed.scriptName, ".xse",
                                                NULL, NULL, 0)))
    {
        ++context->skipped;
        return true;
    }

    snprintf(g_vm_net_mock_dynamic_npc_overrides[g_vm_net_mock_dynamic_npc_override_count].scene,
             sizeof(g_vm_net_mock_dynamic_npc_overrides[0].scene), "%s", row.scene);
    g_vm_net_mock_dynamic_npc_overrides[g_vm_net_mock_dynamic_npc_override_count].seed = row.seed;
    g_vm_net_mock_dynamic_npc_overrides[g_vm_net_mock_dynamic_npc_override_count].enabled = row.enabled;
    ++g_vm_net_mock_dynamic_npc_override_count;
    ++context->loaded;
    return true;
}

static bool vm_net_mock_dynamic_npc_db_load(void)
{
    vm_net_mock_dynamic_npc_load_context context;

    if (g_vm_net_mock_dynamic_npc_db_loaded)
        return g_vm_net_mock_dynamic_npc_db_valid;
    g_vm_net_mock_dynamic_npc_db_loaded = true;
    g_vm_net_mock_dynamic_npc_db_valid = false;
    g_vm_net_mock_dynamic_npc_override_count = 0;
    memset(g_vm_net_mock_dynamic_npc_overrides, 0,
           sizeof(g_vm_net_mock_dynamic_npc_overrides));
    memset(&context, 0, sizeof(context));

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_dynamic_npcs ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "pos_x SMALLINT UNSIGNED NOT NULL,pos_y SMALLINT UNSIGNED NOT NULL,"
            "npc_kind SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "orientation SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "actor_resource VARBINARY(64) NOT NULL,display_name VARBINARY(32) NOT NULL,"
            "script_name VARBINARY(64) NOT NULL DEFAULT '',enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_dynamic_npc_tasks ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "task_id INT UNSIGNED NOT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id),KEY idx_server_dynamic_npc_tasks_task(task_id),"
            "CONSTRAINT fk_server_dynamic_npc_tasks_npc FOREIGN KEY(scene,actor_id) "
            "REFERENCES server_dynamic_npcs(scene,actor_id) ON DELETE CASCADE) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_dynamic_npc_instances ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "target_scene VARBINARY(64) NOT NULL DEFAULT '',"
            "target_x SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "target_y SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "challenge_enemy_id SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "minimum_level TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id),"
            "CONSTRAINT fk_server_dynamic_npc_instances_npc FOREIGN KEY(scene,actor_id) "
            "REFERENCES server_dynamic_npcs(scene,actor_id) ON DELETE CASCADE) ENGINE=InnoDB") ||
        !vm_mysql_query(
            "SELECT HEX(scene),actor_id,pos_x,pos_y,npc_kind,orientation,"
            "HEX(actor_resource),HEX(display_name),HEX(script_name),enabled,"
            "COALESCE(server_dynamic_npc_tasks.task_id,0),"
            "COALESCE(HEX(server_dynamic_npc_instances.target_scene),''),"
            "COALESCE(server_dynamic_npc_instances.target_x,0),"
            "COALESCE(server_dynamic_npc_instances.target_y,0),"
            "COALESCE(server_dynamic_npc_instances.challenge_enemy_id,0),"
            "COALESCE(server_dynamic_npc_instances.minimum_level,1) "
            "FROM server_dynamic_npcs LEFT JOIN server_dynamic_npc_tasks "
            "USING(scene,actor_id) LEFT JOIN server_dynamic_npc_instances "
            "USING(scene,actor_id) ORDER BY scene,actor_id",
            vm_net_mock_dynamic_npc_row, &context))
    {
        printf("[error][mock-admin] dynamic_npc_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    for (u32 i = 0; i < g_vm_net_mock_dynamic_npc_override_count; ++i)
    {
        vm_net_mock_dynamic_npc_override *row =
            &g_vm_net_mock_dynamic_npc_overrides[i];
        const char *publishError = NULL;
        if (!row->enabled)
            continue;
        if (!vm_net_mock_ensure_actor_resource_published(
                row->seed.actorResource, &publishError))
        {
            row->enabled = false;
            ++context.quarantined;
            printf("[error][mock-admin] dynamic_npc_quarantine scene=%s actor=%u resource=%s reason=%s action=runtime-disable\n",
                   row->scene, row->seed.actorId, row->seed.actorResource,
                   publishError ? publishError : "publish-failed");
        }
    }
    g_vm_net_mock_dynamic_npc_db_valid = true;
    printf("[info][mock-admin] dynamic_npc_db_load rows=%u skipped=%u quarantined=%u\n",
           context.loaded, context.skipped, context.quarantined);
    return true;
}

static int vm_net_mock_dynamic_npc_find_override(const char *scene, u32 actorId)
{
    if (!vm_net_mock_dynamic_npc_db_load() || scene == NULL || actorId == 0)
        return -1;
    for (u32 i = 0; i < g_vm_net_mock_dynamic_npc_override_count; ++i)
    {
        if (g_vm_net_mock_dynamic_npc_overrides[i].seed.actorId == actorId &&
            vm_net_mock_scene_names_equal_loose(
                g_vm_net_mock_dynamic_npc_overrides[i].scene, scene))
        {
            return (int)i;
        }
    }
    return -1;
}

static bool vm_net_mock_dynamic_npc_admin_save(
    const char *scene,
    const vm_net_mock_scene_npcinfo_seed *seed,
    bool enabled,
    const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_dynamic_npc_overrides[0].scene) * 2 + 1];
    char actorHex[sizeof(seed->actorResource) * 2 + 1];
    char nameHex[sizeof(seed->displayName) * 2 + 1];
    char scriptHex[sizeof(seed->scriptName) * 2 + 1];
    char instanceSceneHex[sizeof(seed->instanceScene) * 2 + 1];
    char query[2048];
    int existing = -1;
    vm_net_mock_dynamic_npc_override row;

    if (errorOut)
        *errorOut = "invalid dynamic npc";
    if (!vm_net_mock_dynamic_npc_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (!vm_net_mock_scene_name_is_safe(scene) || seed == NULL ||
        seed->actorId == 0 || seed->x == 0 || seed->y == 0 ||
        seed->kind > VM_NET_MOCK_NPC_KIND_MAX ||
        seed->displayName[0] == 0 || strlen(seed->displayName) >= 30 ||
        seed->actorResource[0] == 0 || strlen(seed->actorResource) >= 30 ||
        strlen(seed->scriptName) >= 32 ||
        !vm_net_mock_str_ends_with(seed->actorResource, ".actor") ||
        (seed->scriptName[0] != 0 &&
         !vm_net_mock_str_ends_with(seed->scriptName, ".xse")) ||
        (seed->kind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE &&
         ((seed->instanceScene[0] == 0 && seed->challengeEnemyId == 0) ||
          (seed->instanceScene[0] != 0 &&
           (!vm_net_mock_scene_name_is_safe(seed->instanceScene) ||
            seed->instanceX == 0 || seed->instanceY == 0 ||
            !vm_net_mock_scene_resource_exists(seed->instanceScene))) ||
          (seed->challengeEnemyId != 0 &&
           !vm_net_mock_monster_enemy_id_known(seed->challengeEnemyId)) ||
          seed->instanceMinLevel == 0 || seed->instanceMinLevel > 0xffu ||
          seed->challengeEnemyId > 0xffffu ||
          seed->actorId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)) ||
        !vm_net_mock_open_server_data_resource(seed->actorResource, ".actor",
                                               NULL, NULL, 0) ||
        (seed->scriptName[0] != 0 &&
         !vm_net_mock_open_server_data_resource(seed->scriptName, ".xse",
                                                NULL, NULL, 0)))
    {
        if (errorOut)
            *errorOut = "NPC fields are invalid or the server Actor/XSE resource is unavailable";
        return false;
    }
    existing = vm_net_mock_dynamic_npc_find_override(scene, seed->actorId);
    if (existing < 0 &&
        g_vm_net_mock_dynamic_npc_override_count >= VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX)
    {
        if (errorOut)
            *errorOut = "dynamic npc catalog is full";
        return false;
    }
    if (vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0 ||
        vm_mysql_hex_encode(seed->actorResource, strlen(seed->actorResource),
                            actorHex, sizeof(actorHex)) == 0 ||
        vm_mysql_hex_encode(seed->displayName, strlen(seed->displayName),
                            nameHex, sizeof(nameHex)) == 0 ||
        (seed->scriptName[0] != 0 &&
         vm_mysql_hex_encode(seed->scriptName, strlen(seed->scriptName),
                             scriptHex, sizeof(scriptHex)) == 0))
    {
        if (errorOut)
            *errorOut = "NPC text encoding failed";
        return false;
    }
    if (seed->scriptName[0] == 0)
        scriptHex[0] = 0;
    instanceSceneHex[0] = 0;
    if (seed->instanceScene[0] != 0 &&
        vm_mysql_hex_encode(seed->instanceScene, strlen(seed->instanceScene),
                            instanceSceneHex, sizeof(instanceSceneHex)) == 0)
    {
        if (errorOut)
            *errorOut = "instance scene encoding failed";
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_dynamic_npcs(scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) "
             "VALUES(X'%s',%u,%u,%u,%u,%u,X'%s',X'%s',X'%s',%u) "
             "ON DUPLICATE KEY UPDATE pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),"
             "npc_kind=VALUES(npc_kind),orientation=VALUES(orientation),"
             "actor_resource=VALUES(actor_resource),display_name=VALUES(display_name),"
             "script_name=VALUES(script_name),enabled=VALUES(enabled)",
             sceneHex, seed->actorId, seed->x, seed->y, seed->kind,
             seed->orientation, actorHex, nameHex, scriptHex, enabled ? 1u : 0u);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (seed->kind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_dynamic_npc_instances(scene,actor_id,target_scene,target_x,target_y,challenge_enemy_id,minimum_level) "
                 "VALUES(X'%s',%u,X'%s',%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
                 "target_scene=VALUES(target_scene),target_x=VALUES(target_x),target_y=VALUES(target_y),"
                 "challenge_enemy_id=VALUES(challenge_enemy_id),minimum_level=VALUES(minimum_level)",
                 sceneHex, seed->actorId, instanceSceneHex, seed->instanceX,
                 seed->instanceY, seed->challengeEnemyId,
                 seed->instanceMinLevel);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM server_dynamic_npc_instances WHERE scene=X'%s' AND actor_id=%u",
                 sceneHex, seed->actorId);
    }
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (seed->taskId != 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_dynamic_npc_tasks(scene,actor_id,task_id) "
                 "VALUES(X'%s',%u,%u) ON DUPLICATE KEY UPDATE task_id=VALUES(task_id)",
                 sceneHex, seed->actorId, seed->taskId);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM server_dynamic_npc_tasks WHERE scene=X'%s' AND actor_id=%u",
                 sceneHex, seed->actorId);
    }
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    memset(&row, 0, sizeof(row));
    snprintf(row.scene, sizeof(row.scene), "%s", scene);
    row.seed = *seed;
    row.enabled = enabled;
    if (existing >= 0)
        g_vm_net_mock_dynamic_npc_overrides[existing] = row;
    else
        g_vm_net_mock_dynamic_npc_overrides[g_vm_net_mock_dynamic_npc_override_count++] = row;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] dynamic_npc_save scene=%s actor=%u enabled=%u kind=%u task=%u pos=(%u,%u) instance=%s@(%u,%u) enemy=%u min_level=%u actor_res=%s script=%s\n",
           scene, seed->actorId, enabled ? 1u : 0u, seed->kind,
           seed->taskId, seed->x, seed->y,
           seed->instanceScene[0] ? seed->instanceScene : "-",
           seed->instanceX, seed->instanceY, seed->challengeEnemyId,
           seed->instanceMinLevel, seed->actorResource,
           seed->scriptName[0] ? seed->scriptName : "-");
    return true;
}

static bool vm_net_mock_dynamic_npc_admin_delete_override(
    const char *scene, u32 actorId, const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_dynamic_npc_overrides[0].scene) * 2 + 1];
    char query[512];
    int existing = -1;

    if (errorOut)
        *errorOut = "dynamic npc override not found";
    if (!vm_net_mock_dynamic_npc_db_load() ||
        !vm_net_mock_scene_name_is_safe(scene) || actorId == 0)
    {
        return false;
    }
    existing = vm_net_mock_dynamic_npc_find_override(scene, actorId);
    if (existing < 0 ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_dynamic_npcs WHERE scene=X'%s' AND actor_id=%u",
             sceneHex, actorId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if ((u32)existing + 1 < g_vm_net_mock_dynamic_npc_override_count)
    {
        memmove(&g_vm_net_mock_dynamic_npc_overrides[existing],
                &g_vm_net_mock_dynamic_npc_overrides[existing + 1],
                (g_vm_net_mock_dynamic_npc_override_count - (u32)existing - 1) *
                    sizeof(g_vm_net_mock_dynamic_npc_overrides[0]));
    }
    --g_vm_net_mock_dynamic_npc_override_count;
    memset(&g_vm_net_mock_dynamic_npc_overrides[g_vm_net_mock_dynamic_npc_override_count],
           0, sizeof(g_vm_net_mock_dynamic_npc_overrides[0]));
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] dynamic_npc_override_delete scene=%s actor=%u\n",
           scene, actorId);
    return true;
}

static u32 vm_net_mock_append_builtin_scene_npcinfo_seeds(
    const char *scene,
    vm_net_mock_scene_npcinfo_seed *seeds,
    u32 seedCap)
{
    vm_net_mock_scene_npcinfo_seed seed;
    u32 count = 0;

    if (seeds == NULL || seedCap == 0)
        return 0;

    if (vm_net_mock_scene_is_penglai02(scene))
    {
        /* 00蓬莱仙岛_02.sce (蓬莱-铸剑谷) contains no actor/xse records.
         * These two actors are supplied by the original service-side scene catalog.
         * The client consumes x/y as scene pixels (0x01037998 -> 0x0100EFC4), and
         * the decoded 512x512 map places the walkable strip immediately to the
         * right of the forge doorway at y=125. Keep the two actors 38 px apart. */
        memset(&seed, 0, sizeof(seed));
        seed.actorId = 20020;
        seed.x = 338;
        seed.y = 125;
        seed.kind = VM_NET_MOCK_NPC_KIND_EQUIPMENT_REPAIR;
        snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", "n_blacksmith.actor");
        snprintf(seed.displayName, sizeof(seed.displayName), "%s", "\xc5\xb7\xd2\xb1\xd7\xd3"); /* 欧冶子 */
        seeds[count++] = seed;

        if (count < seedCap)
        {
            memset(&seed, 0, sizeof(seed));
            seed.actorId = 20021;
            seed.x = 376;
            seed.y = 125;
            snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", "e_monkey.actor");
            snprintf(seed.displayName, sizeof(seed.displayName), "%s", "\xd0\xa1\xba\xef\xd7\xd3"); /* 小猴子 */
            seeds[count++] = seed;
        }
        return count;
    }

    if (!vm_net_mock_scene_is_linan_south_gate(scene))
        return 0;

    /* sMap.dsh row 47 maps 临安-南宣门 to c04临安府_01.sce. Wang Chao,
     * Ma Han and Hu Fei belong to this scene, not c04临安府_09 (北宣门).
     * Keep the two guard rows on the upper plaza. The south-gate map is
     * 304x416, so Hu Fei's former north-gate x=304 boundary coordinate is
     * moved inside the lower-right frontage. The client has four display-name
     * registry slots (RegisterDisplayName, 0x0100EEE0); service rows are kept
     * first so these three corrected actors cannot be truncated by SCE rows. */
    memset(&seed, 0, sizeof(seed));
    seed.actorId = 20090;
    seed.x = 172;
    seed.y = 132;
    snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", "n_solider2.actor");
    snprintf(seed.displayName, sizeof(seed.displayName), "%s", "\xcd\xf5\xb3\xaf"); /* 王朝 */
    snprintf(seed.scriptName, sizeof(seed.scriptName), "%s",
             "\x30\x34\xc1\xd9\xb0\xb2\xcd\xf5\xb3\xaf\x2e\x78\x73\x65"); /* 04临安王朝.xse */
    seeds[count++] = seed;

    if (count < seedCap)
    {
        memset(&seed, 0, sizeof(seed));
        seed.actorId = 20091;
        seed.x = 228;
        seed.y = 132;
        snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", "n_solider1.actor");
        snprintf(seed.displayName, sizeof(seed.displayName), "%s", "\xc2\xed\xba\xba"); /* 马汉 */
        snprintf(seed.scriptName, sizeof(seed.scriptName), "%s",
                 "\x30\x34\xc1\xd9\xb0\xb2\xc2\xed\xba\xba\x2e\x78\x73\x65"); /* 04临安马汉.xse */
        seeds[count++] = seed;
    }

    if (count < seedCap)
    {
        memset(&seed, 0, sizeof(seed));
        seed.actorId = 20092;
        seed.x = 264;
        seed.y = 304;
        snprintf(seed.actorResource, sizeof(seed.actorResource), "%s", "n_man1.actor");
        snprintf(seed.displayName, sizeof(seed.displayName), "%s", "\xba\xfa\xec\xb3"); /* 胡斐 */
        snprintf(seed.scriptName, sizeof(seed.scriptName), "%s",
                 "\x30\x34\xc1\xd9\xb0\xb2\xba\xfa\xec\xb3\x2e\x78\x73\x65"); /* 04临安胡斐.xse */
        seeds[count++] = seed;
    }

    return count;
}

static u32 vm_net_mock_append_service_scene_npcinfo_seeds(
    const char *scene,
    vm_net_mock_scene_npcinfo_seed *seeds,
    u32 seedCap)
{
    vm_net_mock_scene_npcinfo_seed builtins[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    u32 builtinCount = 0;
    u32 count = 0;

    if (scene == NULL || seeds == NULL || seedCap == 0)
        return 0;
    memset(builtins, 0, sizeof(builtins));
    builtinCount = vm_net_mock_append_builtin_scene_npcinfo_seeds(
        scene, builtins, VM_NET_MOCK_SCENE_NPCINFO_MAX);
    (void)vm_net_mock_dynamic_npc_db_load();

    for (u32 i = 0; i < builtinCount && count < seedCap; ++i)
    {
        int overrideIndex = vm_net_mock_dynamic_npc_find_override(
            scene, builtins[i].actorId);
        if (overrideIndex < 0)
        {
            seeds[count++] = builtins[i];
        }
        else if (g_vm_net_mock_dynamic_npc_overrides[overrideIndex].enabled)
        {
            seeds[count++] = g_vm_net_mock_dynamic_npc_overrides[overrideIndex].seed;
        }
    }
    for (u32 i = 0;
         i < g_vm_net_mock_dynamic_npc_override_count && count < seedCap;
         ++i)
    {
        bool replacesBuiltin = false;
        const vm_net_mock_dynamic_npc_override *row =
            &g_vm_net_mock_dynamic_npc_overrides[i];

        if (!row->enabled ||
            !vm_net_mock_scene_names_equal_loose(row->scene, scene))
        {
            continue;
        }
        for (u32 builtinIndex = 0; builtinIndex < builtinCount; ++builtinIndex)
        {
            if (builtins[builtinIndex].actorId == row->seed.actorId)
            {
                replacesBuiltin = true;
                break;
            }
        }
        if (!replacesBuiltin)
            seeds[count++] = row->seed;
    }
    return count;
}

static u32 vm_net_mock_dynamic_npc_admin_list(
    const char *scene,
    vm_net_mock_dynamic_npc_admin_row *rows,
    u32 rowCap)
{
    vm_net_mock_scene_npcinfo_seed builtins[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    u32 builtinCount = 0;
    u32 count = 0;

    if (scene == NULL || rows == NULL || rowCap == 0)
        return 0;
    memset(rows, 0, sizeof(*rows) * rowCap);
    memset(builtins, 0, sizeof(builtins));
    builtinCount = vm_net_mock_append_builtin_scene_npcinfo_seeds(
        scene, builtins, VM_NET_MOCK_SCENE_NPCINFO_MAX);
    (void)vm_net_mock_dynamic_npc_db_load();
    for (u32 i = 0; i < builtinCount && count < rowCap; ++i)
    {
        int overrideIndex = vm_net_mock_dynamic_npc_find_override(
            scene, builtins[i].actorId);
        rows[count].builtin = true;
        if (overrideIndex >= 0)
        {
            rows[count].seed =
                g_vm_net_mock_dynamic_npc_overrides[overrideIndex].seed;
            rows[count].enabled =
                g_vm_net_mock_dynamic_npc_overrides[overrideIndex].enabled;
            rows[count].overridden = true;
        }
        else
        {
            rows[count].seed = builtins[i];
            rows[count].enabled = true;
        }
        ++count;
    }
    for (u32 i = 0;
         i < g_vm_net_mock_dynamic_npc_override_count && count < rowCap;
         ++i)
    {
        bool replacesBuiltin = false;
        const vm_net_mock_dynamic_npc_override *row =
            &g_vm_net_mock_dynamic_npc_overrides[i];

        if (!vm_net_mock_scene_names_equal_loose(row->scene, scene))
            continue;
        for (u32 builtinIndex = 0; builtinIndex < builtinCount; ++builtinIndex)
        {
            if (builtins[builtinIndex].actorId == row->seed.actorId)
            {
                replacesBuiltin = true;
                break;
            }
        }
        if (replacesBuiltin)
            continue;
        rows[count].seed = row->seed;
        rows[count].enabled = row->enabled;
        rows[count].overridden = true;
        ++count;
    }
    return count;
}

static u32 vm_net_mock_collect_scene_npcinfo_seeds(const char *scene,
                                                   vm_net_mock_scene_npcinfo_seed *seeds,
                                                   u32 seedCap,
                                                   u32 *totalOut,
                                                   u32 *dynamicOut)
{
    u8 data[8192];
    char resourceScene[64];
    char legacyScene[64];
    u32 len = 0;
    u32 start = 0;
    u32 count = 0;
    u32 total = 0;
    u32 serviceCount = 0;

    if (totalOut)
        *totalOut = 0;
    if (dynamicOut)
        *dynamicOut = 0;
    if (scene == NULL || scene[0] == 0 || seeds == NULL || seedCap == 0)
    {
        return 0;
    }
    memset(seeds, 0, sizeof(*seeds) * seedCap);
    count = vm_net_mock_append_service_scene_npcinfo_seeds(scene, seeds, seedCap);
    total = count;
    serviceCount = count;
    if (dynamicOut)
        *dynamicOut = count;
    /* The _02 resource was audited separately and has no actor/xse records.
     * Avoid decoding and rescanning it on the latency-sensitive first-login
     * request once the confirmed service-side rows have been supplied. */
    if (vm_net_mock_scene_is_penglai02(scene))
    {
        if (totalOut)
            *totalOut = total;
        return count;
    }
    /* c04临安府_01.sce is the old South Gate resource. Its embedded 宋兵乙 /
     * 守门卫兵甲 / 王大胆 / 守门卫兵 rows are a stale scene catalog and must
     * not consume the four client display-name slots alongside the current
     * service-side 王朝 / 马汉 / 胡斐 catalog. */
    if (vm_net_mock_scene_is_linan_south_gate(scene))
    {
        if (totalOut)
            *totalOut = total;
        return count;
    }
    snprintf(resourceScene, sizeof(resourceScene), "%s", scene);
    legacyScene[0] = 0;
    for (u32 resourcePass = 0; resourcePass < 2; ++resourcePass)
    {
        len = vm_net_mock_load_scene_resource(resourceScene, data, sizeof(data));
        start = vm_net_mock_scene_payload_start(data, len);
        if (len != 0 && start != 0)
        {
            for (u32 off = start; off + 8 <= len; ++off)
            {
                vm_net_mock_scene_npcinfo_seed seed;
                u32 end = 0;
                bool duplicate = false;

                if (!vm_net_mock_parse_sce_interactive_npc_at(data, len, off,
                                                              &seed, &end))
                {
                    continue;
                }
                /* The authoritative Tongquetai catalog contains only 大侠郭靖.
                 * The legacy SCE also carries 郭芙蓉/task2.xse, but that row
                 * belongs to old content and must not be restored merely because
                 * the current c00 resource has an empty embedded actor catalog. */
                if (vm_net_mock_scene_is_penglai01(scene) &&
                    (strcmp(seed.scriptName, "task0.xse") != 0 ||
                     strcmp(seed.displayName,
                            "\xb4\xf3\xcf\xc0\xb9\xf9\xbe\xb8") != 0)) /* 大侠郭靖 */
                {
                    printf("[info][network] mock_scene_npc_catalog_skip scene=%s npc=%s script=%s reason=tongquetai-authoritative-guojing-only\n",
                           scene, seed.displayName, seed.scriptName);
                    if (end > off)
                        off = end - 1;
                    continue;
                }
                for (u32 i = 0; i < count; ++i)
                {
                    if (seeds[i].x == seed.x && seeds[i].y == seed.y &&
                        strcmp(seeds[i].scriptName, seed.scriptName) == 0 &&
                        strcmp(seeds[i].displayName, seed.displayName) == 0)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                    continue;
                total += 1;
                if (count < seedCap)
                {
                    u32 candidate = 20000u +
                        (vm_net_mock_scene_npcinfo_hash(scene, &seed) % 40000u);
                    bool collision = true;
                    while (collision)
                    {
                        collision = false;
                        for (u32 i = 0; i < count; ++i)
                        {
                            if (seeds[i].actorId == candidate)
                            {
                                candidate = candidate == 59999u
                                                ? 20000u
                                                : candidate + 1u;
                                collision = true;
                                break;
                            }
                        }
                    }
                    seed.actorId = candidate;
                    seeds[count++] = seed;
                }
                if (end > off)
                    off = end - 1;
            }
        }

        if (total > serviceCount || resourcePass != 0 ||
            !vm_net_mock_scene_resource_legacy_alias(scene, legacyScene,
                                                     sizeof(legacyScene)) ||
            strcmp(resourceScene, legacyScene) == 0)
        {
            break;
        }
        printf("[info][network] mock_scene_npc_resource_alias scene=%s exact=%s legacy=%s reason=exact-catalog-empty\n",
               scene, resourceScene, legacyScene);
        snprintf(resourceScene, sizeof(resourceScene), "%s", legacyScene);
    }
    if (totalOut)
        *totalOut = total;
    return count;
}

static bool vm_net_mock_prepare_scene_enter_resources(vm_net_mock_scene_change_target *target,
                                                      char *missingOut,
                                                      size_t missingOutCap)
{
    if (missingOut != NULL && missingOutCap != 0)
        missingOut[0] = 0;
    if (target == NULL || target->scene[0] == 0)
        return false;
    if (target->needsSceneDownload)
        return false;
    /*
     * A real server does not inspect the client's writable resource cache.
     * It sends the scene-enter contract and, if the client lacks resources,
     * the client drives WT 18/7 chunk requests. Resource completion is therefore
     * observed from the final 18/7 response, not from JHOnlineData contents.
     */
    return true;
}

static bool vm_net_mock_get_scene_center_spawn_from_sce(const char *scene,
                                                         u16 *xOut,
                                                         u16 *yOut);

static void vm_net_mock_defer_scene_enter_completion(const vm_net_mock_scene_change_target *target,
                                                      const char *phase,
                                                      const char *missingResource)
{
    char missingUtf8[128];

    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_scene_change_target = *target;
    /*
     * Same fix as remember_target: if the scene exists locally the download
     * flag is stale and will cause the 25/5 follow-up to defer again.
     */
    if (g_vm_net_mock_last_scene_change_target.needsSceneDownload &&
        vm_net_mock_scene_resource_exists(g_vm_net_mock_last_scene_change_target.scene))
    {
        g_vm_net_mock_last_scene_change_target.needsSceneDownload = false;
        if (g_vm_net_mock_last_scene_change_target.x == 0 &&
            g_vm_net_mock_last_scene_change_target.y == 0)
        {
            u16 cx = 0, cy = 0;
            if (vm_net_mock_get_scene_reasonable_spawn_from_sce(
                    g_vm_net_mock_last_scene_change_target.scene, &cx, &cy, NULL))
            {
                g_vm_net_mock_last_scene_change_target.x = cx;
                g_vm_net_mock_last_scene_change_target.y = cy;
                g_vm_net_mock_last_scene_change_target.hasSceEntry = true;
            }
        }
    }
    g_vm_net_mock_last_scene_change_target_valid = true;
    vm_mock_service_mark_active_session_scene_pending(target, phase ? phase : "scene-enter-defer");
    vm_net_mock_gbk_label_to_utf8((missingResource != NULL && missingResource[0] != 0) ?
                                      missingResource :
                                      "-",
                                  missingUtf8,
                                  sizeof(missingUtf8));
    printf("[info][network] mock_scene_enter_defer phase=%s scene=%s pos=(%u,%u) exit=%u missing=%s keep_pending=1\n",
           phase ? phase : "-",
           target->scene,
           target->x,
           target->y,
           target->exitId,
           missingUtf8);
    vm_autotest_note("mock_scene_enter_defer phase=%s scene=%s pos=(%u,%u) exit=%u missing=%s keep_pending=1\n",
                     phase ? phase : "-",
                     target->scene,
                     target->x,
                     target->y,
                     target->exitId,
                     missingUtf8);
}

static u16 vm_net_mock_u16_add_cap(u16 value, u16 amount);

static bool vm_net_mock_scene_edge_data_available(const char *scene)
{
    u8 data[8192];
    u32 len = 0;

    if (!vm_net_mock_scene_name_is_download_key(scene))
        return false;

    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    return vm_net_mock_scene_payload_start(data, len) != 0;
}

static bool vm_net_mock_find_sce_edge_portal_by_entry(const char *scene, u32 entryId,
                                                      vm_net_mock_sce_edge_portal *portalOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 start = vm_net_mock_scene_payload_start(data, len);

    if (portalOut)
        memset(portalOut, 0, sizeof(*portalOut));
    if (len == 0 || start == 0 || entryId > 0xffff)
        return false;

    for (u32 off = start; off + 18 <= len; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;
        if (!vm_net_mock_parse_sce_edge_portal_at(data, len, off, &portal, &end))
            continue;
        if (portal.entryId != (u16)entryId)
            continue;
        if (portalOut)
            *portalOut = portal;
        return true;
    }
    return false;
}

static bool vm_net_mock_find_sce_edge_portal_by_target_exit(const char *scene,
                                                            const char *targetScene,
                                                            u32 exitId,
                                                            vm_net_mock_sce_edge_portal *portalOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 start = vm_net_mock_scene_payload_start(data, len);

    if (portalOut)
        memset(portalOut, 0, sizeof(*portalOut));
    if (len == 0 || start == 0 || targetScene == NULL || targetScene[0] == 0 || exitId > 0xffff)
        return false;

    for (u32 off = start; off + 18 <= len; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;
        if (!vm_net_mock_parse_sce_edge_portal_at(data, len, off, &portal, &end))
            continue;
        if (portal.targetEntryId != (u16)exitId)
            continue;
        if (!vm_net_mock_scene_names_equal_loose(portal.targetScene, targetScene))
            continue;
        if (portalOut)
            *portalOut = portal;
        return true;
    }
    return false;
}

static bool vm_net_mock_get_scene_dimensions_from_sce(const char *scene, u16 *widthOut, u16 *heightOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 base = 0;
    u16 width = 0;
    u16 height = 0;

    if (widthOut)
        *widthOut = 0;
    if (heightOut)
        *heightOut = 0;
    if (len < 24)
        return false;
    for (base = 0; base + 10 <= len && base < 32; ++base)
    {
        if (memcmp(data + base, "SCE2", 4) == 0)
            break;
    }
    if (base + 10 > len || base >= 32)
        return false;
    width = vm_net_mock_read_le16_at(data, base + 4);
    height = vm_net_mock_read_le16_at(data, base + 6);
    if (width == 0 || height == 0)
        return false;
    if (widthOut)
        *widthOut = width;
    if (heightOut)
        *heightOut = height;
    return true;
}

static bool vm_net_mock_get_scene_center_spawn_from_sce(const char *scene,
                                                        u16 *xOut,
                                                        u16 *yOut)
{
    u16 width = 0;
    u16 height = 0;
    u16 x = 0;
    u16 y = 0;

    if (scene == NULL || scene[0] == 0)
        return false;
    if (!vm_net_mock_get_scene_dimensions_from_sce(scene, &width, &height))
    {
        return false;
    }
    x = (u16)(width / 2);
    y = (u16)(height / 2);
    if (x == 0 || y == 0)
        return false;
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    if (xOut)
        *xOut = x;
    if (yOut)
        *yOut = y;
    return true;
}

static bool vm_net_mock_get_scene_nearest_entry_spawn_from_sce(const char *scene,
                                                               u16 fromX,
                                                               u16 fromY,
                                                               u16 *xOut,
                                                               u16 *yOut,
                                                               u16 *entryIdOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 start = vm_net_mock_scene_payload_start(data, len);
    bool found = false;
    u16 bestX = 0;
    u16 bestY = 0;
    u16 bestEntryId = 0;
    unsigned long long bestDistance = 0xffffffffffffffffull;

    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (entryIdOut)
        *entryIdOut = 0xffff;
    if (scene == NULL || scene[0] == 0 || len == 0 || start == 0)
        return false;

    for (u32 off = start; off + 18 <= len; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;
        int dx = 0;
        int dy = 0;
        unsigned long long distance = 0;
        if (!vm_net_mock_parse_sce_edge_portal_at(data, len, off, &portal, &end))
            continue;
        if (portal.spawnX == 0 && portal.spawnY == 0)
            continue;
        dx = (int)portal.spawnX - (int)fromX;
        dy = (int)portal.spawnY - (int)fromY;
        distance = (unsigned long long)(dx < 0 ? -dx : dx) *
                       (unsigned long long)(dx < 0 ? -dx : dx) +
                   (unsigned long long)(dy < 0 ? -dy : dy) *
                       (unsigned long long)(dy < 0 ? -dy : dy);
        if (found && distance >= bestDistance)
            continue;
        found = true;
        bestDistance = distance;
        bestX = portal.spawnX;
        bestY = portal.spawnY;
        bestEntryId = portal.entryId;
    }

    if (!found)
        return false;
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &bestX, &bestY);
    if (xOut)
        *xOut = bestX;
    if (yOut)
        *yOut = bestY;
    if (entryIdOut)
        *entryIdOut = bestEntryId;
    return true;
}

static bool vm_net_mock_get_scene_reasonable_spawn_from_sce(const char *scene,
                                                            u16 *xOut,
                                                            u16 *yOut,
                                                            u16 *entryIdOut)
{
    u16 width = 0;
    u16 height = 0;
    u16 x = 0;
    u16 y = 0;
    u16 entryId = 0xffff;
    const char *source = "sce-center";

    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (entryIdOut)
        *entryIdOut = 0xffff;
    if (scene == NULL || scene[0] == 0)
        return false;

    /*
     * SCE edge-portal spawn points are scene-space actor coordinates.  Prefer
     * the one nearest the map centre for a generic teleport that has no source
     * entry id, then reuse the existing trigger-rectangle safety adjustment.
     * By contrast, sMap.dsh positionX/positionY place the scene node on the
     * world-map UI and must never be emitted as the actor's scene position.
     */
    if (vm_net_mock_get_scene_dimensions_from_sce(scene, &width, &height) &&
        vm_net_mock_get_scene_nearest_entry_spawn_from_sce(scene,
                                                           (u16)(width / 2),
                                                           (u16)(height / 2),
                                                           &x,
                                                           &y,
                                                           &entryId))
    {
        source = "sce-nearest-entry";
    }
    else if (!vm_net_mock_get_scene_center_spawn_from_sce(scene, &x, &y))
    {
        return false;
    }

    if (xOut)
        *xOut = x;
    if (yOut)
        *yOut = y;
    if (entryIdOut)
        *entryIdOut = entryId;
    vm_autotest_note("mock_scene_reasonable_spawn scene=%s pos=(%u,%u) source=%s entry=%u\n",
                     scene, x, y, source, entryId);
    return true;
}

static bool vm_net_mock_find_sce_edge_portal_at_pos(const char *scene, u16 gridX, u16 gridY,
                                                    u16 margin,
                                                    vm_net_mock_sce_edge_portal *portalOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 start = vm_net_mock_scene_payload_start(data, len);

    if (portalOut)
        memset(portalOut, 0, sizeof(*portalOut));
    if (len == 0 || start == 0)
        return false;

    for (u32 off = start; off + 18 <= len; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;
        u16 left = 0;
        u16 top = 0;
        u16 right = 0;
        u16 bottom = 0;
        if (!vm_net_mock_parse_sce_edge_portal_at(data, len, off, &portal, &end))
            continue;
        left = (portal.left > margin) ? (u16)(portal.left - margin) : 0;
        top = (portal.top > margin) ? (u16)(portal.top - margin) : 0;
        right = (u16)(portal.right + margin);
        bottom = (u16)(portal.bottom + margin);
        if (gridX < left || gridX > right || gridY < top || gridY > bottom)
            continue;
        if (portalOut)
            *portalOut = portal;
        return true;
    }
    return false;
}

static u16 vm_net_mock_u16_sub_floor(u16 value, u16 amount)
{
    return value > amount ? (u16)(value - amount) : 0;
}

static u16 vm_net_mock_u16_add_cap(u16 value, u16 amount)
{
    return value > (u16)(0xffff - amount) ? 0xffff : (u16)(value + amount);
}

static void vm_net_mock_adjust_pos_away_from_sce_portal(const vm_net_mock_sce_edge_portal *portal,
                                                        u16 safeGap, u16 *x, u16 *y)
{
    u16 px = 0;
    u16 py = 0;
    bool xInside = false;
    bool yInside = false;
    u32 dx = 0;
    u32 dy = 0;

    if (portal == NULL || x == NULL || y == NULL || safeGap == 0)
        return;

    px = *x;
    py = *y;
    xInside = px >= portal->left && px <= portal->right;
    yInside = py >= portal->top && py <= portal->bottom;
    dx = xInside ? 0 : (px < portal->left ? (u32)(portal->left - px) : (u32)(px - portal->right));
    dy = yInside ? 0 : (py < portal->top ? (u32)(portal->top - py) : (u32)(py - portal->bottom));

    if (!(xInside && yInside) && dx * dx + dy * dy >= (u32)safeGap * (u32)safeGap)
        return;

    if (xInside && py < portal->top)
    {
        *y = vm_net_mock_u16_sub_floor(portal->top, safeGap);
        return;
    }
    if (xInside && py > portal->bottom)
    {
        *y = vm_net_mock_u16_add_cap(portal->bottom, safeGap);
        return;
    }
    if (yInside && px < portal->left)
    {
        *x = vm_net_mock_u16_sub_floor(portal->left, safeGap);
        return;
    }
    if (yInside && px > portal->right)
    {
        *x = vm_net_mock_u16_add_cap(portal->right, safeGap);
        return;
    }

    if (!xInside || !yInside)
    {
        if (dy > 0 && (dx == 0 || dy <= dx))
            *y = py < portal->top ? vm_net_mock_u16_sub_floor(portal->top, safeGap)
                                  : vm_net_mock_u16_add_cap(portal->bottom, safeGap);
        else if (dx > 0)
            *x = px < portal->left ? vm_net_mock_u16_sub_floor(portal->left, safeGap)
                                   : vm_net_mock_u16_add_cap(portal->right, safeGap);
        return;
    }

    {
        u32 best = 0xffffffffu;
        char dir = 0;
        u32 leftDist = (u32)(px - portal->left);
        u32 rightDist = (u32)(portal->right - px);
        u32 topDist = (u32)(py - portal->top);
        u32 bottomDist = (u32)(portal->bottom - py);

        if (portal->left > 0 && leftDist < best)
        {
            best = leftDist;
            dir = 'l';
        }
        if (portal->top > 0 && topDist < best)
        {
            best = topDist;
            dir = 't';
        }
        if (rightDist < best)
        {
            best = rightDist;
            dir = 'r';
        }
        if (bottomDist < best)
        {
            dir = 'b';
        }

        if (dir == 'l')
            *x = vm_net_mock_u16_sub_floor(portal->left, safeGap);
        else if (dir == 't')
            *y = vm_net_mock_u16_sub_floor(portal->top, safeGap);
        else if (dir == 'r')
            *x = vm_net_mock_u16_add_cap(portal->right, safeGap);
        else if (dir == 'b')
            *y = vm_net_mock_u16_add_cap(portal->bottom, safeGap);
    }
}

static bool vm_net_mock_adjust_safe_player_pos_from_sce(const char *scene, u16 *x, u16 *y)
{
    vm_net_mock_sce_edge_portal portal;
    u16 oldX = 0;
    u16 oldY = 0;

    if (scene == NULL || x == NULL || y == NULL)
        return false;

    oldX = *x;
    oldY = *y;
    if (!vm_net_mock_find_sce_edge_portal_at_pos(scene, oldX, oldY,
                                                VM_NET_MOCK_SCENE_LANDING_SAFE_GAP,
                                                &portal))
    {
        return false;
    }

    vm_net_mock_adjust_pos_away_from_sce_portal(&portal,
                                                VM_NET_MOCK_SCENE_LANDING_SAFE_GAP,
                                                x, y);
    if (oldX != *x || oldY != *y)
    {
        vm_autotest_note("mock_scene_safe_landing scene=%s raw=(%u,%u) safe=(%u,%u) rect=(%u,%u)-(%u,%u) entry=%u targetEntry=%u\n",
                         scene, oldX, oldY, *x, *y,
                         portal.left, portal.top, portal.right, portal.bottom,
                         portal.entryId, portal.targetEntryId);
    }
    return oldX != *x || oldY != *y;
}

static bool vm_net_mock_get_scene_entry_spawn_from_sce(const char *scene, u32 entryId,
                                                       u16 *xOut, u16 *yOut)
{
    vm_net_mock_sce_edge_portal portal;
    u16 x = 0;
    u16 y = 0;

    if (!vm_net_mock_find_sce_edge_portal_by_entry(scene, entryId, &portal))
        return false;
    x = portal.spawnX;
    y = portal.spawnY;
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    if (xOut)
        *xOut = x;
    if (yOut)
        *yOut = y;
    return portal.spawnX != 0 || portal.spawnY != 0;
}

static bool vm_net_mock_resolve_sce_edge_portal_target(const vm_net_mock_sce_edge_portal *portal,
                                                       vm_net_mock_scene_change_target *target)
{
    const char *normalizedTarget = NULL;
    u16 targetX = 0;
    u16 targetY = 0;

    if (portal == NULL || target == NULL || portal->targetScene[0] == 0)
        return false;

    if (!vm_net_mock_get_scene_entry_spawn_from_sce(portal->targetScene, portal->entryId,
                                                    &targetX, &targetY))
        return false;

    memset(target, 0, sizeof(*target));
    normalizedTarget = vm_net_mock_normalize_scene_name_for_enter(portal->targetScene);
    snprintf(target->scene, sizeof(target->scene), "%s", normalizedTarget);
    target->x = targetX;
    target->y = targetY;
    target->exitId = portal->entryId;
    target->mapType = 2;
    target->hasSceEntry = true;
    target->needsSceneDownload = false;
    return true;
}

static bool vm_net_mock_get_scene_portal_target_from_sce(const char *sourceScene,
                                                         u16 gridX, u16 gridY, u16 margin,
                                                         vm_net_mock_scene_change_target *target)
{
    vm_net_mock_sce_edge_portal portal;

    if (target == NULL ||
        !vm_net_mock_find_sce_edge_portal_at_pos(sourceScene, gridX, gridY, margin, &portal))
    {
        return false;
    }
    return vm_net_mock_resolve_sce_edge_portal_target(&portal, target);
}

static void vm_net_mock_remember_moveinfo_source_pos(const char *scene,
                                                     u16 x,
                                                     u16 y,
                                                     const char *reason)
{
    if (!vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    snprintf(g_vm_net_mock_last_moveinfo_source_scene,
             sizeof(g_vm_net_mock_last_moveinfo_source_scene),
             "%s",
             scene);
    g_vm_net_mock_last_moveinfo_source_x = x;
    g_vm_net_mock_last_moveinfo_source_y = y;
    g_vm_net_mock_last_moveinfo_source_tick = g_schedulerTick;
    g_vm_net_mock_last_moveinfo_source_valid = true;
    printf("[info][network] mock_moveinfo_source pos=(%u,%u) reason=%s scene=%s\n",
           x, y, reason ? reason : "moveinfo", scene);
}

static bool vm_net_mock_pending_local_scene_change_matches(const char *requestedTargetScene,
                                                           char *pendingSceneOut,
                                                           size_t pendingSceneCap,
                                                           u8 *pendingOut)
{
#ifdef CBE_SERVER_ONLY
    /* A pending local CBE transition is not observable by the service.  The
     * matching service-session transition is resolved below by its own state. */
    (void)requestedTargetScene;
    if (pendingSceneOut != NULL && pendingSceneCap != 0)
        pendingSceneOut[0] = 0;
    if (pendingOut != NULL)
        *pendingOut = 0;
    return false;
#else
    u32 sceneObj = 0;
    u8 pending = 0;
    char pendingScene[64];

    if (requestedTargetScene == NULL || requestedTargetScene[0] == 0 || Global_R9 == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5C6B, &pending, sizeof(pending)) != UC_ERR_OK ||
        pending == 0)
    {
        return false;
    }
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, sizeof(sceneObj)) != UC_ERR_OK ||
        sceneObj == 0 ||
        !vm_net_read_guest_raw_cstr(sceneObj + 0x475, pendingScene, sizeof(pendingScene)) ||
        !vm_net_mock_scene_name_is_safe(pendingScene) ||
        !vm_net_mock_scene_names_equal_loose(pendingScene, requestedTargetScene))
    {
        return false;
    }
    if (pendingSceneOut != NULL && pendingSceneCap > 0)
        snprintf(pendingSceneOut, pendingSceneCap, "%s", pendingScene);
    if (pendingOut != NULL)
        *pendingOut = pending;
    return true;
#endif
}

static bool vm_net_mock_try_scene_change_source_portal(const char *sourceKind,
                                                       const char *sourceScene,
                                                       const char *requestedTargetScene,
                                                       u32 requestExitId,
                                                       u8 requestMapType,
                                                       bool haveGrid,
                                                       u16 gridX,
                                                       u16 gridY,
                                                       bool allowTargetExitMatch,
                                                       vm_net_mock_scene_change_target *target)
{
    vm_net_mock_sce_edge_portal portal;
    vm_net_mock_scene_change_target portalTarget;
    const char *normalizedTarget = NULL;
    const char *matchMode = NULL;

    if (sourceScene == NULL || requestedTargetScene == NULL || target == NULL ||
        sourceScene[0] == 0 || requestedTargetScene[0] == 0 ||
        !vm_net_mock_scene_name_is_safe(sourceScene))
    {
        return false;
    }

    if (haveGrid &&
        vm_net_mock_find_sce_edge_portal_at_pos(sourceScene, gridX, gridY, 8, &portal) &&
        vm_net_mock_scene_names_equal_loose(portal.targetScene, requestedTargetScene))
    {
        matchMode = "trigger-rect";
    }
    else if (allowTargetExitMatch &&
             vm_net_mock_find_sce_edge_portal_by_target_exit(sourceScene,
                                                             requestedTargetScene,
                                                             requestExitId,
                                                             &portal))
    {
        matchMode = "target-entry";
    }
    else
    {
        return false;
    }

    if (!vm_net_mock_resolve_sce_edge_portal_target(&portal, &portalTarget))
    {
        memset(target, 0, sizeof(*target));
        normalizedTarget = vm_net_mock_scene_name_is_safe(portal.targetScene)
                               ? vm_net_mock_normalize_scene_name_for_enter(portal.targetScene)
                               : portal.targetScene;
        snprintf(target->scene, sizeof(target->scene), "%s", normalizedTarget);
        target->exitId = portal.entryId;
        target->mapType = requestMapType;
        target->hasSceEntry = false;
        target->needsSceneDownload = vm_net_mock_scene_name_is_download_key(portal.targetScene);
        if (target->needsSceneDownload)
        {
            vm_autotest_note("mock_scene_portal_missing_target_entry source=%s target=%s entry=%u request_exit=%u action=download-ack\n",
                             sourceScene, target->scene, portal.entryId, requestExitId);
            printf("[warn][network] mock_scene_portal_missing_target_entry source_kind=%s source=%s target=%s entry=%u targetEntry=%u request_exit=%u match=%s pos=(%u,%u) action=download-ack\n",
                   sourceKind ? sourceKind : "source",
                   sourceScene, target->scene, portal.entryId, portal.targetEntryId,
                   requestExitId, matchMode ? matchMode : "unknown",
                   haveGrid ? gridX : 0, haveGrid ? gridY : 0);
            return true;
        }
        return false;
    }

    if (requestExitId != portal.targetEntryId)
    {
        printf("[warn][network] mock_scene_portal_exit_mismatch source_kind=%s source=%s request=%s request_exit=%u portal_entry=%u targetEntry=%u match=%s pos=(%u,%u)\n",
               sourceKind ? sourceKind : "source",
               sourceScene, requestedTargetScene, requestExitId, portal.entryId,
               portal.targetEntryId, matchMode ? matchMode : "unknown",
               haveGrid ? gridX : 0, haveGrid ? gridY : 0);
    }

    portalTarget.mapType = requestMapType;
    *target = portalTarget;
    printf("[info][network] mock_scene_change_source_portal source_kind=%s source=%s request=%s request_exit=%u portal_entry=%u targetEntry=%u match=%s pos=(%u,%u) target=(%u,%u) evidence=JianghuOL.CBE:0x1018166 SCE:edge_portal\n",
           sourceKind ? sourceKind : "source",
           sourceScene, requestedTargetScene, requestExitId, portal.entryId,
           portal.targetEntryId, matchMode ? matchMode : "unknown",
           haveGrid ? gridX : 0, haveGrid ? gridY : 0,
           target->x, target->y);
    return true;
}

static bool vm_net_mock_get_scene_change_target_from_source_portal(const char *requestedTargetScene,
                                                                   u32 requestExitId,
                                                                   u8 requestMapType,
                                                                   vm_net_mock_scene_change_target *target)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    char runtimeScene[64];
    char sourceScene[64];
    char pendingScene[64] = {0};
    u16 gridX = 0;
    u16 gridY = 0;
    u8 pending = 0;
    bool haveGrid = vm_net_mock_read_current_player_grid(NULL, NULL, &gridX, &gridY, NULL, NULL);
    bool allowTargetExitMatch =
        vm_net_mock_pending_local_scene_change_matches(requestedTargetScene,
                                                       pendingScene,
                                                       sizeof(pendingScene),
                                                       &pending);
    bool haveMoveinfoSource =
        g_vm_net_mock_last_moveinfo_source_valid &&
        g_vm_net_mock_last_moveinfo_source_scene[0] != 0 &&
        (g_schedulerTick - g_vm_net_mock_last_moveinfo_source_tick) < 600;

    if (target == NULL || !vm_net_mock_scene_name_is_safe(requestedTargetScene))
        return false;

    if (haveMoveinfoSource &&
        vm_net_mock_try_scene_change_source_portal(allowTargetExitMatch ? "moveinfo-pending" : "moveinfo",
                                                   g_vm_net_mock_last_moveinfo_source_scene,
                                                   requestedTargetScene,
                                                   requestExitId,
                                                   requestMapType,
                                                   true,
                                                   g_vm_net_mock_last_moveinfo_source_x,
                                                   g_vm_net_mock_last_moveinfo_source_y,
                                                   true,
                                                   target))
    {
        return true;
    }

    if (role != NULL &&
        vm_net_mock_scene_name_is_safe(role->scene) &&
        vm_net_mock_try_scene_change_source_portal(allowTargetExitMatch ? "role-pending" : "role",
                                                   role->scene,
                                                   requestedTargetScene,
                                                   requestExitId,
                                                   requestMapType,
                                                   haveGrid,
                                                   gridX,
                                                   gridY,
                                                   allowTargetExitMatch,
                                                   target))
    {
        return true;
    }

    if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
    {
        snprintf(sourceScene, sizeof(sourceScene), "%s",
                 vm_net_mock_normalize_scene_name_for_enter(runtimeScene));
        if ((role == NULL || !vm_net_mock_scene_names_equal_loose(role->scene, sourceScene)) &&
            vm_net_mock_try_scene_change_source_portal(allowTargetExitMatch ? "runtime-pending" : "runtime",
                                                       sourceScene,
                                                       requestedTargetScene,
                                                       requestExitId,
                                                       requestMapType,
                                                       haveGrid,
                                                       gridX,
                                                       gridY,
                                                       allowTargetExitMatch,
                                                       target))
        {
            return true;
        }
    }

    if (allowTargetExitMatch)
    {
        printf("[info][network] mock_scene_change_source_probe_miss request=%s request_exit=%u pending=%u pending_scene=%s role_scene=%s pos=(%u,%u) moveinfo_valid=%u moveinfo_scene=%s moveinfo_pos=(%u,%u) age=%u\n",
               requestedTargetScene, requestExitId, pending, pendingScene,
               (role != NULL && role->scene[0] != 0) ? role->scene : "",
               haveGrid ? gridX : 0, haveGrid ? gridY : 0,
               haveMoveinfoSource ? 1u : 0u,
               g_vm_net_mock_last_moveinfo_source_valid ? g_vm_net_mock_last_moveinfo_source_scene : "",
               g_vm_net_mock_last_moveinfo_source_valid ? g_vm_net_mock_last_moveinfo_source_x : 0,
               g_vm_net_mock_last_moveinfo_source_valid ? g_vm_net_mock_last_moveinfo_source_y : 0,
               g_vm_net_mock_last_moveinfo_source_valid ? (g_schedulerTick - g_vm_net_mock_last_moveinfo_source_tick) : 0);
    }

    return false;
}

static void vm_net_mock_remember_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_scene_change_target = *target;
    /*
     * Clear needsSceneDownload when the scene file already exists locally.
     * Stale download flags from earlier resolution paths (e.g. edge-portal
     * target-entry mismatch) cause prepare_scene_enter_resources to return
     * false, which defers completion and triggers a re-entry loop.
     */
    if (g_vm_net_mock_last_scene_change_target.needsSceneDownload &&
        vm_net_mock_scene_resource_exists(g_vm_net_mock_last_scene_change_target.scene))
    {
        g_vm_net_mock_last_scene_change_target.needsSceneDownload = false;
        if (g_vm_net_mock_last_scene_change_target.x == 0 &&
            g_vm_net_mock_last_scene_change_target.y == 0)
        {
            u16 cx = 0, cy = 0;
            if (vm_net_mock_get_scene_reasonable_spawn_from_sce(
                    g_vm_net_mock_last_scene_change_target.scene, &cx, &cy, NULL))
            {
                g_vm_net_mock_last_scene_change_target.x = cx;
                g_vm_net_mock_last_scene_change_target.y = cy;
                g_vm_net_mock_last_scene_change_target.hasSceEntry = true;
            }
        }
        printf("[info][network] remember_target cleared needsDownload scene=%s pos=(%u,%u)\n",
               g_vm_net_mock_last_scene_change_target.scene,
               g_vm_net_mock_last_scene_change_target.x,
               g_vm_net_mock_last_scene_change_target.y);
    }
    g_vm_net_mock_last_scene_change_target_valid = true;
    vm_mock_service_mark_active_session_scene_pending(target, "scene-target-remember");
    ++g_vm_net_mock_last_scene_change_target_serial;
    if (g_vm_net_mock_last_scene_change_target_serial == 0)
        g_vm_net_mock_last_scene_change_target_serial = 1;
    printf("[info][network] mock_scene_target_remember serial=%u scene=%s pos=(%u,%u) exit=%u\n",
           g_vm_net_mock_last_scene_change_target_serial,
           target->scene, target->x, target->y, target->exitId);
}

static bool vm_net_mock_refresh_downloaded_scene_change_target(vm_net_mock_scene_change_target *target)
{
    char rawScene[64];
    u16 x = 0;
    u16 y = 0;

    if (target == NULL || target->scene[0] == 0 || !target->needsSceneDownload)
        return target != NULL && target->scene[0] != 0;
    if (!vm_net_mock_scene_resource_exists(target->scene))
        return false;

    snprintf(rawScene, sizeof(rawScene), "%s", target->scene);
    x = target->x;
    y = target->y;
    if (x == 0 && y == 0 &&
        vm_net_mock_get_scene_entry_spawn_from_sce(rawScene, target->exitId, &x, &y))
    {
        target->hasSceEntry = true;
    }
    else
    {
        if (x != 0 || y != 0)
        {
            target->hasSceEntry = true;
        }
        else
        {
            printf("[warn][network] mock_scene_downloaded_missing_entry scene=%s exit=%u action=keep-pending\n",
                   rawScene, target->exitId);
            return false;
        }
    }
    snprintf(target->scene, sizeof(target->scene), "%s",
             vm_net_mock_normalize_scene_name_for_enter(rawScene));
    target->x = x;
    target->y = y;
    target->needsSceneDownload = false;
    vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
    printf("[info][network] mock_scene_download_ready scene=%s pos=(%u,%u) exit=%u\n",
           target->scene, target->x, target->y, target->exitId);
    return true;
}

static bool vm_net_mock_scene_change_targets_equal(const vm_net_mock_scene_change_target *a,
                                                   const vm_net_mock_scene_change_target *b)
{
    return a != NULL && b != NULL &&
           a->x == b->x &&
           a->y == b->y &&
           a->exitId == b->exitId &&
           vm_net_mock_scene_names_equal_loose(a->scene, b->scene);
}

static bool vm_net_mock_scene_change_targets_same_arrival(const vm_net_mock_scene_change_target *a,
                                                          const vm_net_mock_scene_change_target *b)
{
    return a != NULL && b != NULL &&
           a->x == b->x &&
           a->y == b->y &&
           vm_net_mock_scene_names_equal_loose(a->scene, b->scene);
}

static bool vm_net_mock_consume_update_completed_scene_reenter(const vm_net_mock_scene_change_target *target)
{
    char updateNameUtf8[128];

    if (!g_vm_net_mock_update_completed_reenter_pending)
        return false;
    if (target == NULL || target->scene[0] == 0)
    {
        g_vm_net_mock_update_completed_reenter_pending = false;
        return false;
    }
    g_vm_net_mock_update_completed_reenter_pending = false;
    vm_net_mock_gbk_label_to_utf8(g_vm_net_mock_update_completed_name,
                                  updateNameUtf8,
                                  sizeof(updateNameUtf8));
    printf("[info][screen] screen_mgr allow-update-reenter scene=%s pos=(%u,%u) exit=%u file=%s\n",
           target->scene,
           target->x,
           target->y,
           target->exitId,
           updateNameUtf8);
    vm_autotest_note("screen_mgr allow-update-reenter scene=%s pos=(%u,%u) exit=%u file=%s\n",
                     target->scene,
                     target->x,
                     target->y,
                     target->exitId,
                     updateNameUtf8);
    return true;
}

static void vm_net_mock_mark_completed_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_completed_scene_change_target = *target;
    g_vm_net_mock_last_completed_scene_change_target.needsSceneDownload = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = true;
    g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;
    vm_mock_service_mark_active_session_scene_ready(target->scene,
                                                    target->x,
                                                    target->y,
                                                    "scene-target-complete");
}

static void vm_net_mock_mark_direct_scene_enter_completed(const vm_net_mock_scene_change_target *target,
                                                          const char *reason)
{
    if (target == NULL || target->scene[0] == 0)
        return;
    /*
     * Direct mmGame responses such as settings/unstuck already carry
     * scene+posinfo and make the client call EnterSceneByMapName(). We still
     * allocate a fresh target serial so the host same-screen guard accepts that
     * client-driven re-entry, but the target must not remain pending; otherwise
     * later WT 2/3 or 25/5 follow-ups send another 30/1/30/2 and reopen loading.
     */
    vm_net_mock_remember_scene_change_target(target);
    vm_net_mock_mark_completed_scene_change_target(target);
    g_vm_net_mock_last_scene_change_target_valid = false;
    printf("[info][network] mock_scene_target_direct_completed scene=%s pos=(%u,%u) exit=%u reason=%s\n",
           target->scene,
           target->x,
           target->y,
           target->exitId,
           reason ? reason : "direct-enter");
}

static void vm_net_mock_complete_startup_scene_followup(const char *currentScene,
                                                        const char *source,
                                                        u8 objectCount,
                                                        u32 responseLen)
{
    vm_net_mock_scene_change_target target;
    u16 targetX = vm_net_mock_scene_spawn_x();
    u16 targetY = vm_net_mock_scene_spawn_y();

    if (currentScene == NULL || currentScene[0] == 0)
    {
        g_vm_net_mock_title_role_scene_followup_pending = false;
        return;
    }

    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", currentScene);
    if (!vm_net_mock_read_current_player_grid(NULL, NULL, &targetX, &targetY, NULL, NULL))
        vm_net_mock_adjust_safe_player_pos_for_scene(currentScene, &targetX, &targetY);
    target.x = targetX;
    target.y = targetY;
    target.mapType = 2;
    target.exitId = 0;
    target.hasSceEntry = true;
    target.needsSceneDownload = false;
    vm_net_mock_mark_direct_scene_enter_completed(&target, "scene-startup-followup-complete");
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y,
                                      "scene-startup-followup-complete");
    g_vm_net_mock_title_role_scene_followup_pending = false;
    printf("[info][network] mock_scene_startup_followup_complete scene=%s pos=(%u,%u) source=%s objects=%u resp=%u action=no-second-scene-enter\n",
           target.scene, target.x, target.y, source ? source : "-", objectCount, responseLen);
    vm_autotest_note("mock_scene_startup_followup_complete scene=%s pos=(%u,%u) source=%s objects=%u response=no-scene-pos-reenter evidence=JianghuOL.CBE:0x010137CA+0x010396D6\n",
                     target.scene, target.x, target.y, source ? source : "-", objectCount);
}

static bool vm_net_mock_is_recent_completed_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (!g_vm_net_mock_last_completed_scene_change_target_valid ||
        !vm_net_mock_scene_change_targets_same_arrival(target, &g_vm_net_mock_last_completed_scene_change_target))
    {
        return false;
    }
    return (g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick) <
           VM_NET_MOCK_COMPLETED_SCENE_REUSE_TICKS;
}

static bool vm_net_mock_is_recent_completed_scene_name(const char *scene, u32 windowTicks)
{
    if (scene == NULL ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        !vm_net_mock_scene_names_equal_loose(scene, g_vm_net_mock_last_completed_scene_change_target.scene))
    {
        return false;
    }
    return (g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick) < windowTicks;
}

static bool vm_net_mock_scene_change_target_is_unresolved_existing_scene(const vm_net_mock_scene_change_target *target)
{
    return target != NULL &&
           target->scene[0] != 0 &&
           target->needsSceneDownload &&
           !target->hasSceEntry &&
           target->x == 0 &&
           target->y == 0 &&
           vm_net_mock_scene_resource_exists(target->scene);
}

static void vm_net_mock_clear_unresolved_scene_change_target(const vm_net_mock_scene_change_target *target)
{
    if (target == NULL ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !vm_net_mock_scene_names_equal_loose(g_vm_net_mock_last_scene_change_target.scene, target->scene))
    {
        return;
    }
    if (vm_net_mock_scene_change_target_is_unresolved_existing_scene(&g_vm_net_mock_last_scene_change_target) ||
        (g_vm_net_mock_last_scene_change_target.x == 0 &&
         g_vm_net_mock_last_scene_change_target.y == 0))
    {
        printf("[warn][network] mock_scene_target_clear_unresolved scene=%s exit=%u action=drop-zero-pending\n",
               g_vm_net_mock_last_scene_change_target.scene,
               g_vm_net_mock_last_scene_change_target.exitId);
        g_vm_net_mock_last_scene_change_target_valid = false;
    }
}

static bool vm_net_mock_is_recent_current_scene_reload(const char *scene, u32 windowTicks)
{
    if (scene == NULL ||
        !g_vm_net_mock_last_current_scene_reload_valid ||
        !vm_net_mock_scene_names_equal_loose(scene, g_vm_net_mock_last_current_scene_reload_scene))
    {
        return false;
    }
    return (g_schedulerTick - g_vm_net_mock_last_current_scene_reload_tick) < windowTicks;
}

static void vm_net_mock_mark_current_scene_reload(const char *scene)
{
    if (scene == NULL || scene[0] == 0)
        return;
    snprintf(g_vm_net_mock_last_current_scene_reload_scene,
             sizeof(g_vm_net_mock_last_current_scene_reload_scene),
             "%s", scene);
    g_vm_net_mock_last_current_scene_reload_valid = true;
    g_vm_net_mock_last_current_scene_reload_tick = g_schedulerTick;
}

/* The 30/1 current-scene reload arms a single subsequent 12/1 resource
 * follow-up.  Once that follow-up has emitted its catalog and no-posinfo ack,
 * leave no reload provenance behind for ordinary scene refresh requests. */
static void vm_net_mock_consume_current_scene_reload(const char *scene)
{
    if (scene == NULL ||
        !g_vm_net_mock_last_current_scene_reload_valid ||
        !vm_net_mock_scene_names_equal_loose(scene,
                                             g_vm_net_mock_last_current_scene_reload_scene))
    {
        return;
    }
    g_vm_net_mock_last_current_scene_reload_valid = false;
    g_vm_net_mock_last_current_scene_reload_scene[0] = 0;
    g_vm_net_mock_last_current_scene_reload_tick = 0;
}

static bool vm_net_mock_scene_runtime_pending_without_target(void)
{
#ifdef CBE_SERVER_ONLY
    /* A standalone service has no guest scene object.  Scene transitions are
     * tracked by the per-client service session instead. */
    return false;
#else
    u32 sceneObj = 0;
    u8 pending = 0;

    if (!Global_R9 || g_vm_net_mock_last_scene_change_target_valid)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, sizeof(sceneObj)) != UC_ERR_OK || sceneObj == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x5C6B, &pending, sizeof(pending)) != UC_ERR_OK)
        return false;
    return pending != 0;
#endif
}

static bool vm_net_mock_should_use_full_scene_bootstrap(const char *currentScene,
                                                        const vm_net_mock_scene_change_target *target)
{
    if (target == NULL || target->scene[0] == 0)
        return false;
    if (target->needsSceneDownload)
        return false;

    if (vm_net_mock_scene_is_penglai02(target->scene))
        return true;

    if (currentScene != NULL &&
        target->exitId == 0 &&
        vm_net_mock_scene_is_penglai_transfer_scene(currentScene) &&
        vm_net_mock_scene_is_penglai_transfer_scene(target->scene) &&
        !vm_net_mock_scene_names_equal_loose(currentScene, target->scene))
    {
        return true;
    }

    /*
     * Local SCE exports show c00蓬莱仙岛_03 uses an east edge portal into
     * 01桃花岛_01, not the older bottom-to-04 route kept in legacy notes.
     * Runtime on 2026-06-24 reproduced:
     *   2/10 len=19 -> 2/3 len=87 -> assert at scene_runtime_tick(0x01014EE0)
     * when this path is answered by the generic ack-only scene-change packet.
     * Feed the same full bootstrap family used by the other live portal enters.
     */
    if (currentScene != NULL &&
        vm_net_mock_scene_is_c00_penglai03(currentScene) &&
        vm_net_mock_scene_is_taohuadao01(target->scene))
    {
        return true;
    }

    return false;
}

static bool vm_net_mock_scene_uses_current_scene_completion(const char *scene)
{
    return vm_net_mock_scene_is_penglai03(scene) ||
           vm_net_mock_scene_is_penglai04(scene) ||
           vm_net_mock_scene_is_taohuadao01(scene);
}

static void vm_net_mock_get_scene_change_target(const u8 *request, u32 requestLen,
                                                vm_net_mock_scene_change_target *target)
{
    char mapId[64];
    u32 exitId = 0;
    u16 sceSpawnX = 0;
    u16 sceSpawnY = 0;
    const char *currentScene = vm_net_mock_current_scene_name();
    memset(target, 0, sizeof(*target));
    snprintf(target->scene, sizeof(target->scene), "%s", vm_net_mock_default_scene_name());
    target->x = vm_net_mock_scene_spawn_x();
    target->y = vm_net_mock_scene_spawn_y();
    target->exitId = 0;
    target->mapType = 2;
    target->hasSceEntry = false;
    target->needsSceneDownload = false;

    if (!vm_net_mock_get_object_string_field(request, requestLen, "mapID", mapId, sizeof(mapId)))
        return;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId);
    (void)vm_net_mock_get_object_u8_field(request, requestLen, "maptype", &target->mapType);
    target->exitId = exitId;

    if (mapId[0] != 0)
        snprintf(target->scene, sizeof(target->scene), "%s", mapId);

    if (g_vm_net_mock_teleport_stone_map_enter_pending &&
        g_vm_net_mock_last_scene_change_target_valid &&
        (g_vm_net_mock_last_scene_change_target.x != 0 ||
         g_vm_net_mock_last_scene_change_target.y != 0) &&
        vm_net_mock_scene_names_equal_loose(mapId, g_vm_net_mock_last_scene_change_target.scene))
    {
        u32 savedExit = g_vm_net_mock_last_scene_change_target.exitId;
        *target = g_vm_net_mock_last_scene_change_target;
        target->mapType = (target->mapType != 0) ? target->mapType : 2;
        /*
         * Keep target resolution side-effect free. The dispatcher probes this
         * helper from several detectors before the actual scene-change builder;
         * consuming the map-transfer pending flag here loses the authoritative
         * 16/4 landing position before the real response is built.
         */
        printf("[info][network] mock_scene_target_inherit_map_transfer scene=%s pos=(%u,%u) request_exit=%u saved_exit=%u\n",
               target->scene, target->x, target->y, exitId, savedExit);
        return;
    }

    if (vm_net_mock_get_scene_change_target_from_source_portal(mapId, exitId,
                                                               target->mapType,
                                                               target))
    {
        return;
    }

    if (exitId == 0 &&
        g_vm_net_mock_last_scene_change_target_valid &&
        (g_vm_net_mock_last_scene_change_target.x != 0 ||
         g_vm_net_mock_last_scene_change_target.y != 0) &&
        vm_net_mock_scene_names_equal_loose(mapId, g_vm_net_mock_last_scene_change_target.scene))
    {
        *target = g_vm_net_mock_last_scene_change_target;
        target->exitId = exitId;
        target->mapType = (target->mapType != 0) ? target->mapType : 2;
        printf("[info][network] mock_scene_target_inherit_pending scene=%s pos=(%u,%u) exit=%u\n",
               target->scene, target->x, target->y, exitId);
        return;
    }

    if (exitId == 0 &&
        g_vm_net_mock_last_completed_scene_change_target_valid &&
        (g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick) <
            VM_NET_MOCK_COMPLETED_SCENE_REUSE_TICKS &&
        (g_vm_net_mock_last_completed_scene_change_target.x != 0 ||
         g_vm_net_mock_last_completed_scene_change_target.y != 0) &&
        vm_net_mock_scene_names_equal_loose(mapId, g_vm_net_mock_last_completed_scene_change_target.scene))
    {
        *target = g_vm_net_mock_last_completed_scene_change_target;
        target->exitId = exitId;
        target->mapType = (target->mapType != 0) ? target->mapType : 2;
        target->needsSceneDownload = false;
        printf("[info][network] mock_scene_target_inherit_completed scene=%s pos=(%u,%u) exit=%u\n",
               target->scene, target->x, target->y, exitId);
        return;
    }

    if (vm_net_mock_get_scene_entry_spawn_from_sce(mapId, exitId, &sceSpawnX, &sceSpawnY))
    {
        snprintf(target->scene, sizeof(target->scene), "%s",
                 vm_net_mock_normalize_scene_name_for_enter(mapId));
        target->x = sceSpawnX;
        target->y = sceSpawnY;
        target->hasSceEntry = true;
        return;
    }

    if (vm_net_mock_scene_resource_exists(mapId))
    {
        u16 centerX = 0;
        u16 centerY = 0;
        snprintf(target->scene, sizeof(target->scene), "%s",
                 vm_net_mock_normalize_scene_name_for_enter(mapId));
        target->x = 0;
        target->y = 0;
        target->hasSceEntry = false;
        /*
         * The scene file already exists on disk — it is not a download-key
         * resource that needs network transfer.  Falling back to
         * needsSceneDownload=true here forces prepare_scene_enter_resources
         * to return false, which defers completion and keeps the scene-change
         * target pending; the subsequent 25/5 follow-up then re-sends 30/2 +
         * 30/1 and the client re-enters the same screen in a loop.
         *
         * Use a real SCE entry spawn when available so the client lands inside
         * the playable scene instead of inventing (0,0) or a map centre.
         */
        if (vm_net_mock_get_scene_reasonable_spawn_from_sce(mapId,
                                                            &centerX,
                                                            &centerY,
                                                            NULL))
        {
            target->x = centerX;
            target->y = centerY;
            target->hasSceEntry = true;
        }
        printf("[info][network] mock_scene_entry_local_fallback scene=%s exit=%u pos=(%u,%u) action=use-sce-safe-spawn\n",
               mapId, exitId, target->x, target->y);
        return;
    }

    if (vm_net_mock_scene_name_is_download_key(mapId))
    {
        snprintf(target->scene, sizeof(target->scene), "%s", mapId);
        target->x = 0;
        target->y = 0;
        target->needsSceneDownload = true;
        vm_autotest_note("mock_scene_missing_sce target=%s exit=%u action=download-ack\n",
                         target->scene, exitId);
        return;
    }

    if (vm_net_mock_scene_is_penglai01(mapId) && exitId == 0)
    {
        snprintf(target->scene, sizeof(target->scene),
                 "%s", "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65");
        target->x = 396;
        target->y = 473;
    }
    else if (strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0)
    {
        if (exitId == 1)
        {
            target->x = 128;
            target->y = 45;
        }
        else
        {
            target->x = 396;
            target->y = 473;
        }
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31") == 0)
    {
        target->x = 223;
        target->y = 382;
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0 ||
             strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
    {
        if (currentScene != NULL && vm_net_mock_scene_is_penglai02(currentScene))
        {
            target->x = 145;
            target->y = 47;
        }
        else if (currentScene != NULL && vm_net_mock_scene_is_penglai04(currentScene))
        {
            target->x = 105;
            target->y = 395;
        }
        else if (currentScene != NULL &&
                 vm_net_mock_scene_names_equal_loose(currentScene, target->scene))
        {
            target->x = vm_net_mock_scene_spawn_x();
            target->y = vm_net_mock_scene_spawn_y();
        }
        else
        {
            target->x = 105;
            target->y = (exitId == 1) ? 58 : 395;
        }
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 225 : 230;
        target->y = (exitId == 1) ? 116 : 425;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 305 : 80;
        target->y = (exitId == 1) ? 310 : 60;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 40 : 200;
        target->y = (exitId == 1) ? 70 : 540;
    }
    else if (strcmp(mapId, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 323 : 42;
        target->y = (exitId == 1) ? 200 : 60;
    }
    else if (strcmp(mapId, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0)
    {
        target->x = (exitId == 1) ? 256 : 136;
        target->y = (exitId == 1) ? 300 : 58;
    }

    vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
}

static bool vm_net_mock_is_teleport_stone_list_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 0x10 &&
           object.subtype == 1 &&
           object.payloadLen == 0;
}

static bool vm_net_mock_is_teleport_stone_transfer_request(const u8 *request, u32 requestLen, u8 *subtypeOut)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    if (kind != 0x10 || (subtype != 2 && subtype != 3))
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "exitID") &&
        !vm_net_mock_request_contains(request, requestLen, "type"))
    {
        return false;
    }
    if (subtypeOut)
        *subtypeOut = subtype;
    return true;
}

static void vm_net_mock_get_teleport_stone_target(const u8 *request, u32 requestLen,
                                                  vm_net_mock_scene_change_target *target)
{
    u32 exitId = VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID;
    const char *sceneOverride = vm_net_mock_env_str("CBE_TELEPORT_STONE_SCENE", "");
    const char *targetScene = vm_net_mock_default_scene_name();

    memset(target, 0, sizeof(*target));
    if (sceneOverride != NULL && sceneOverride[0] != 0 && vm_net_mock_scene_name_is_safe(sceneOverride))
        targetScene = sceneOverride;

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitid", &exitId);

    snprintf(target->scene, sizeof(target->scene), "%s", vm_net_mock_normalize_scene_name_for_enter(targetScene));
    target->x = VM_NET_MOCK_ROLE_INITIAL_X;
    target->y = VM_NET_MOCK_ROLE_INITIAL_Y;
    (void)vm_net_mock_get_scene_reasonable_spawn_from_sce(target->scene,
                                                          &target->x,
                                                          &target->y,
                                                          NULL);
    if (getenv("CBE_TELEPORT_STONE_X") != NULL)
        target->x = (u16)vm_net_mock_env_u32("CBE_TELEPORT_STONE_X", target->x);
    if (getenv("CBE_TELEPORT_STONE_Y") != NULL)
        target->y = (u16)vm_net_mock_env_u32("CBE_TELEPORT_STONE_Y", target->y);
    target->exitId = exitId;
    target->mapType = 2;
    vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
}

static bool vm_net_mock_is_teleport_stone_map_transfer_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 0x10 &&
           subtype == 4 &&
           vm_net_mock_request_contains(request, requestLen, "curid") &&
           vm_net_mock_request_contains(request, requestLen, "objid");
}

static bool vm_net_mock_copy_dsh_string_field(char *out, size_t outCap,
                                              const u8 *value, u32 valueLen)
{
    u32 copyLen = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (value == NULL || valueLen == 0)
        return false;
    copyLen = (u32)SDL_min(valueLen, (u32)outCap - 1);
    while (copyLen > 0 && value[copyLen - 1] == 0)
        --copyLen;
    if (copyLen == 0)
        return false;
    memcpy(out, value, copyLen);
    out[copyLen] = 0;
    return true;
}

static bool vm_net_mock_find_teleport_stone_wmap_row_dsh(const char *path,
                                                         u32 objId,
                                                         u32 curId,
                                                         u32 *targetRowIdOut,
                                                         u32 *baseRowIdOut,
                                                         u32 *sceneCountOut)
{
    static u8 data[8192];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;

    if (targetRowIdOut)
        *targetRowIdOut = 0;
    if (baseRowIdOut)
        *baseRowIdOut = 0;
    if (sceneCountOut)
        *sceneCountOut = 0;
    if (len < 16 || objId == 0)
        return false;
    /*
     * JianghuOL.CBE:SendItemUseReq(0x0103573A) reads both values from the
     * wMap row's +68 teleport-id field: curid is the current world-map id and
     * objid is the selected world-map id.  The selected child sMap row is not
     * curid; it is saved separately by the client and later sent as
     * 16/2.exitID.  Keep 16/4 on the target world's base row until that
     * authoritative confirmation field arrives.
     */
    (void)curId;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount < 15 || columnCount > 64 || rowCount > 10000)
        return false;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return false;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return false;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        u32 rowId = 0;
        u32 teleportId = 0;
        u32 baseRowId = 0;
        u32 sceneCount = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;
        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            if (rowPos + valueLen > rowEnd)
                break;
            if (col == 0)
                rowId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 1)
                teleportId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 12)
                baseRowId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 13)
                sceneCount = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            rowPos += valueLen;
        }

        if (teleportId == objId && baseRowId != 0)
        {
            u32 targetRowId = baseRowId;
            if (targetRowIdOut)
                *targetRowIdOut = targetRowId;
            if (baseRowIdOut)
                *baseRowIdOut = baseRowId;
            if (sceneCountOut)
                *sceneCountOut = sceneCount;
            return true;
        }
        pos = rowEnd;
    }
    return false;
}

static bool vm_net_mock_find_teleport_stone_smap_scene_dsh(const char *path,
                                                           u32 targetRowId,
                                                           char *out,
                                                           size_t outCap,
                                                           u16 *xOut,
                                                           u16 *yOut)
{
    static u8 data[16384];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;

    if (out != NULL && outCap > 0)
        out[0] = 0;
    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (len < 16 || targetRowId == 0 || out == NULL || outCap == 0)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount < 5 || columnCount > 64 || rowCount > 10000)
        return false;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return false;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return false;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        u32 rowId = 0;
        char scene[64];
        u32 x = 0;
        u32 y = 0;

        scene[0] = 0;
        if (rowEnd > len || rowEnd < rowPos)
            break;
        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            if (rowPos + valueLen > rowEnd)
                break;
            if (col == 0)
                rowId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 1)
                (void)vm_net_mock_copy_dsh_string_field(scene, sizeof(scene), value, valueLen);
            else if (col == 3)
                x = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 4)
                y = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            rowPos += valueLen;
        }

        if (rowId == targetRowId &&
            vm_net_mock_str_ends_with(scene, ".sce") &&
            vm_net_mock_scene_name_is_download_key(scene))
        {
            snprintf(out, outCap, "%s", scene);
            if (xOut)
                *xOut = (u16)(x > 0xffff ? 0 : x);
            if (yOut)
                *yOut = (u16)(y > 0xffff ? 0 : y);
            return true;
        }
        pos = rowEnd;
    }
    return false;
}

/* Ordinary death recovery needs a real return point, not the character's
 * bootstrap map.  `n_telestone` is the authored scene actor used by the
 * client resources; sMap supplies local-scene topology and wMap joins the
 * otherwise separate local-map groups.  The sMap X/Y values are world-map UI
 * markers, so they are deliberately never used as actor coordinates here. */
enum
{
    VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX = 192,
    VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX = 64
};

typedef struct
{
    u32 rowId;
    u32 parentWorldId;
    u32 neighbors[4];
    char scene[64];
} vm_net_mock_death_respawn_smap_node;

typedef struct
{
    u32 worldId;
    u32 neighbors[4];
} vm_net_mock_death_respawn_wmap_node;

static int vm_net_mock_death_respawn_find_smap_node(
    const vm_net_mock_death_respawn_smap_node *nodes, u32 count, u32 rowId)
{
    if (nodes == NULL || rowId == 0)
        return -1;
    for (u32 i = 0; i < count; ++i)
    {
        if (nodes[i].rowId == rowId)
            return (int)i;
    }
    return -1;
}

static int vm_net_mock_death_respawn_find_wmap_node(
    const vm_net_mock_death_respawn_wmap_node *nodes, u32 count, u32 worldId)
{
    if (nodes == NULL || worldId == 0)
        return -1;
    for (u32 i = 0; i < count; ++i)
    {
        if (nodes[i].worldId == worldId)
            return (int)i;
    }
    return -1;
}

static bool vm_net_mock_death_respawn_scene_has_teleport_stone(const char *scene)
{
    static const char marker[] = "n_telestone";
    u8 data[16384];
    u32 len = 0;

    if (!vm_net_mock_scene_name_is_download_key(scene))
        return false;
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    if (len < sizeof(marker) - 1)
        return false;
    for (u32 offset = 0; offset + sizeof(marker) - 1 <= len; ++offset)
    {
        if (memcmp(data + offset, marker, sizeof(marker) - 1) == 0)
            return true;
    }
    return false;
}

static bool vm_net_mock_death_respawn_load_smap_topology(
    vm_net_mock_death_respawn_smap_node *nodes, u32 nodeCap, u32 *nodeCountOut)
{
    char path[256];
    u8 data[16384];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;
    u32 pos = 0;
    u32 nodeCount = 0;

    if (nodeCountOut)
        *nodeCountOut = 0;
    if (nodes == NULL || nodeCap == 0 ||
        !vm_net_mock_open_server_data_resource("sMap.dsh", ".dsh", NULL,
                                               path, sizeof(path)))
    {
        return false;
    }
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columnCount < 12 || columnCount > 64 || rowCount > nodeCap ||
        16u + headerBytes > len)
    {
        return false;
    }
    pos = 16u + headerBytes;
    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        vm_net_mock_death_respawn_smap_node node;
        bool valid = true;

        if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
            return false;
        memset(&node, 0, sizeof(node));
        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
            {
                valid = false;
                break;
            }
            if (col == 0)
                node.rowId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 1)
                valid = vm_net_mock_copy_dsh_string_field(node.scene,
                                                           sizeof(node.scene),
                                                           value, valueLen) && valid;
            else if (col >= 7 && col <= 10)
                node.neighbors[col - 7] = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 11)
                node.parentWorldId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            rowPos += valueLen;
        }
        if (valid && node.rowId != 0 && node.parentWorldId != 0 &&
            vm_net_mock_scene_name_is_download_key(node.scene))
        {
            nodes[nodeCount++] = node;
        }
        pos = rowEnd;
    }
    if (nodeCountOut)
        *nodeCountOut = nodeCount;
    return nodeCount != 0;
}

static bool vm_net_mock_death_respawn_load_wmap_topology(
    vm_net_mock_death_respawn_wmap_node *nodes, u32 nodeCap, u32 *nodeCountOut)
{
    char path[256];
    u8 data[4096];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;
    u32 pos = 0;
    u32 nodeCount = 0;

    if (nodeCountOut)
        *nodeCountOut = 0;
    if (nodes == NULL || nodeCap == 0 ||
        !vm_net_mock_open_server_data_resource("wMap.dsh", ".dsh", NULL,
                                               path, sizeof(path)))
    {
        return false;
    }
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columnCount < 12 || columnCount > 64 || rowCount > nodeCap ||
        16u + headerBytes > len)
    {
        return false;
    }
    pos = 16u + headerBytes;
    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        vm_net_mock_death_respawn_wmap_node node;
        bool valid = true;

        if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
            return false;
        memset(&node, 0, sizeof(node));
        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
            {
                valid = false;
                break;
            }
            if (col == 0)
                node.worldId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col >= 8 && col <= 11)
                node.neighbors[col - 8] = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            rowPos += valueLen;
        }
        if (valid && node.worldId != 0)
            nodes[nodeCount++] = node;
        pos = rowEnd;
    }
    if (nodeCountOut)
        *nodeCountOut = nodeCount;
    return nodeCount != 0;
}

static bool vm_net_mock_resolve_nearest_teleport_stone_respawn(
    const char *fromScene, char *sceneOut, size_t sceneOutCap,
    u16 *xOut, u16 *yOut, u32 *sourceSmapRowOut, u32 *targetSmapRowOut,
    u32 *distanceOut, const char **routeOut)
{
    vm_net_mock_death_respawn_smap_node smap[VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX];
    vm_net_mock_death_respawn_wmap_node wmap[VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX];
    bool smapVisited[VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX];
    u32 smapQueue[VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX];
    u32 smapDistance[VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX];
    u32 wmapQueue[VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX];
    u32 wmapDistance[VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX];
    u32 smapCount = 0;
    u32 wmapCount = 0;
    u32 sourceIndex = 0;
    u32 targetIndex = 0;
    u32 sourceWorldId = 0;
    u32 bestDistance = 0xffffffffu;
    u32 queueHead = 0;
    u32 queueTail = 0;
    const char *route = "-";
    u16 landingX = 0;
    u16 landingY = 0;

    if (sceneOut != NULL && sceneOutCap != 0)
        sceneOut[0] = 0;
    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (sourceSmapRowOut)
        *sourceSmapRowOut = 0;
    if (targetSmapRowOut)
        *targetSmapRowOut = 0;
    if (distanceOut)
        *distanceOut = 0;
    if (routeOut)
        *routeOut = "unresolved";
    if (!vm_net_mock_scene_name_is_safe(fromScene) || sceneOut == NULL || sceneOutCap == 0 ||
        !vm_net_mock_death_respawn_load_smap_topology(smap, sizeof(smap) / sizeof(smap[0]),
                                                       &smapCount))
    {
        return false;
    }
    for (; sourceIndex < smapCount; ++sourceIndex)
    {
        if (vm_net_mock_scene_names_equal_loose(fromScene, smap[sourceIndex].scene))
            break;
    }
    if (sourceIndex == smapCount)
        return false;
    sourceWorldId = smap[sourceIndex].parentWorldId;
    if (sourceSmapRowOut)
        *sourceSmapRowOut = smap[sourceIndex].rowId;

    /* Prefer an actually connected local scene.  This preserves the shortest
     * scene-edge route and avoids treating world-map UI coordinates as space. */
    memset(smapVisited, 0, sizeof(smapVisited));
    smapQueue[queueTail++] = sourceIndex;
    smapVisited[sourceIndex] = true;
    smapDistance[sourceIndex] = 0;
    while (queueHead < queueTail)
    {
        u32 index = smapQueue[queueHead++];

        if (vm_net_mock_death_respawn_scene_has_teleport_stone(smap[index].scene))
        {
            targetIndex = index;
            bestDistance = smapDistance[index];
            route = "smap-neighbor";
            goto resolved_target;
        }
        for (u32 edge = 0; edge < 4; ++edge)
        {
            int neighbor = vm_net_mock_death_respawn_find_smap_node(
                smap, smapCount, smap[index].neighbors[edge]);
            if (neighbor >= 0 && !smapVisited[neighbor] && queueTail < smapCount)
            {
                smapVisited[neighbor] = true;
                smapDistance[neighbor] = smapDistance[index] + 1;
                smapQueue[queueTail++] = (u32)neighbor;
            }
        }
    }

    /* Local sMap components do not cross world-map groups.  If none of their
     * scenes contains an authored stone, select the nearest world-map group
     * whose child scene does, using only wMap adjacency. */
    if (sourceWorldId == 0 ||
        !vm_net_mock_death_respawn_load_wmap_topology(wmap, sizeof(wmap) / sizeof(wmap[0]),
                                                       &wmapCount))
    {
        return false;
    }
    {
        int sourceWorldIndex = vm_net_mock_death_respawn_find_wmap_node(
            wmap, wmapCount, sourceWorldId);
        if (sourceWorldIndex < 0)
            return false;
        for (u32 i = 0; i < wmapCount; ++i)
            wmapDistance[i] = 0xffffffffu;
        queueHead = 0;
        queueTail = 0;
        wmapQueue[queueTail++] = (u32)sourceWorldIndex;
        wmapDistance[sourceWorldIndex] = 0;
        while (queueHead < queueTail)
        {
            u32 index = wmapQueue[queueHead++];
            for (u32 edge = 0; edge < 4; ++edge)
            {
                int neighbor = vm_net_mock_death_respawn_find_wmap_node(
                    wmap, wmapCount, wmap[index].neighbors[edge]);
                if (neighbor >= 0 && wmapDistance[neighbor] == 0xffffffffu &&
                    queueTail < wmapCount)
                {
                    wmapDistance[neighbor] = wmapDistance[index] + 1;
                    wmapQueue[queueTail++] = (u32)neighbor;
                }
            }
        }
    }
    for (u32 i = 0; i < smapCount; ++i)
    {
        int worldIndex = vm_net_mock_death_respawn_find_wmap_node(
            wmap, wmapCount, smap[i].parentWorldId);
        u32 worldDistance = worldIndex >= 0 ? wmapDistance[worldIndex] : 0xffffffffu;

        if (worldDistance == 0xffffffffu ||
            !vm_net_mock_death_respawn_scene_has_teleport_stone(smap[i].scene))
        {
            continue;
        }
        if (worldDistance < bestDistance ||
            (worldDistance == bestDistance && smap[i].rowId < smap[targetIndex].rowId))
        {
            targetIndex = i;
            bestDistance = worldDistance;
        }
    }
    if (bestDistance == 0xffffffffu)
        return false;
    route = "wmap-neighbor";

resolved_target:
    if (!vm_net_mock_get_scene_reasonable_spawn_from_sce(smap[targetIndex].scene,
                                                          &landingX, &landingY, NULL))
    {
        return false;
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(smap[targetIndex].scene, &landingX, &landingY);
    snprintf(sceneOut, sceneOutCap, "%s", smap[targetIndex].scene);
    if (xOut)
        *xOut = landingX;
    if (yOut)
        *yOut = landingY;
    if (targetSmapRowOut)
        *targetSmapRowOut = smap[targetIndex].rowId;
    if (distanceOut)
        *distanceOut = bestDistance;
    if (routeOut)
        *routeOut = route;
    printf("[info][network] mock_death_respawn_nearest_telestone source_scene=%s source_smap=%u target_scene=%s target_smap=%u route=%s hops=%u landing=(%u,%u) evidence=sMap.dsh+wMap.dsh+SCE:n_telestone\n",
           fromScene, smap[sourceIndex].rowId, smap[targetIndex].scene,
           smap[targetIndex].rowId, route, bestDistance, landingX, landingY);
    vm_autotest_note("mock_death_respawn_nearest_telestone source_scene=%s source_smap=%u target_scene=%s target_smap=%u route=%s hops=%u landing=(%u,%u) evidence=sMap.dsh/wMap.dsh/SCE:n_telestone\n",
                     fromScene, smap[sourceIndex].rowId, smap[targetIndex].scene,
                     smap[targetIndex].rowId, route, bestDistance, landingX, landingY);
    return true;
}

static bool vm_net_mock_find_teleport_stone_scene_by_dsh(u32 objId,
                                                         u32 curId,
                                                         char *out,
                                                         size_t outCap,
                                                         u16 *xOut,
                                                         u16 *yOut,
                                                         u32 *targetRowIdOut,
                                                         u32 *baseRowIdOut,
                                                         u32 *sceneCountOut,
                                                         const char **rowSourceOut)
{
    const char *wmapPaths[] = {
        "JHOnlineData/wMap.dsh",
        "bin/JHOnlineData/wMap.dsh"
    };
    const char *smapPaths[] = {
        "JHOnlineData/sMap.dsh",
        "bin/JHOnlineData/sMap.dsh"
    };
    u32 targetRowId = 0;
    u32 baseRowId = 0;
    u32 sceneCount = 0;
    const char *rowSource = "wmap-base";

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (rowSourceOut)
        *rowSourceOut = "-";
    for (u32 i = 0; i < sizeof(wmapPaths) / sizeof(wmapPaths[0]); ++i)
    {
        if (vm_net_mock_find_teleport_stone_wmap_row_dsh(wmapPaths[i], objId, curId,
                                                         &targetRowId, &baseRowId,
                                                         &sceneCount))
        {
            break;
        }
    }
    if (targetRowId == 0)
        return false;
    if (sceneCount > 1)
        rowSource = "wmap-base-await-confirm-exitID";

    for (u32 i = 0; i < sizeof(smapPaths) / sizeof(smapPaths[0]); ++i)
    {
        if (vm_net_mock_find_teleport_stone_smap_scene_dsh(smapPaths[i], targetRowId,
                                                           out, outCap, xOut, yOut))
        {
            if (targetRowIdOut)
                *targetRowIdOut = targetRowId;
            if (baseRowIdOut)
                *baseRowIdOut = baseRowId;
            if (sceneCountOut)
                *sceneCountOut = sceneCount;
            if (rowSourceOut)
                *rowSourceOut = rowSource;
            return true;
        }
    }

    return false;
}

static bool vm_net_mock_get_teleport_stone_smap_row_target(
    u32 smapRow,
    vm_net_mock_scene_change_target *target,
    const char **posSourceOut)
{
    const char *smapPaths[] = {
        "JHOnlineData/sMap.dsh",
        "bin/JHOnlineData/sMap.dsh"
    };
    char scene[64];
    u16 dshX = 0;
    u16 dshY = 0;
    u16 sceEntryId = 0xffff;
    bool targetResourceExists = false;
    const char *posSource = "-";

    if (target == NULL || smapRow == 0)
        return false;
    memset(target, 0, sizeof(*target));
    scene[0] = 0;

    for (u32 i = 0; i < sizeof(smapPaths) / sizeof(smapPaths[0]); ++i)
    {
        if (vm_net_mock_find_teleport_stone_smap_scene_dsh(
                smapPaths[i], smapRow, scene, sizeof(scene), &dshX, &dshY))
        {
            break;
        }
    }
    if (!vm_net_mock_scene_name_is_download_key(scene))
        return false;

    snprintf(target->scene, sizeof(target->scene), "%s", scene);
    targetResourceExists = vm_net_mock_scene_resource_exists(scene);
    target->needsSceneDownload = !targetResourceExists;
    if (targetResourceExists &&
        vm_net_mock_get_scene_reasonable_spawn_from_sce(scene,
                                                        &target->x,
                                                        &target->y,
                                                        &sceEntryId))
    {
        posSource = sceEntryId == 0xffff
                        ? "sce-center-fallback"
                        : "sce-nearest-entry";
        target->hasSceEntry = true;
    }
    else if (!targetResourceExists && (dshX != 0 || dshY != 0))
    {
        target->x = dshX;
        target->y = dshY;
        posSource = "smap-ui-marker-fallback-missing-sce";
        vm_net_mock_adjust_safe_player_pos_for_scene(scene, &target->x, &target->y);
    }
    else
    {
        return false;
    }

    target->exitId = smapRow;
    target->mapType = 2;
    if (posSourceOut)
        *posSourceOut = posSource;
    printf("[info][network] mock_teleport_stone_exit_target smap_row=%u scene=%s smap_marker=(%u,%u) landing=(%u,%u) pos_source=%s download=%u evidence=JianghuOL.CBE:0x0103573A+0x01018F66\n",
           smapRow, target->scene, dshX, dshY, target->x, target->y,
           posSource, target->needsSceneDownload ? 1u : 0u);
    return true;
}

/*
 * `16/2.exitID` has two packet-visible meanings.  The map-stone 16/4 flow
 * uses it as the selected child sMap row, while the ordinary scene-stone list
 * repeats the parent list entry's id during the local item-confirmation
 * callback.  The latter is already represented by the saved provisional
 * target and must not be looked up as an sMap row.
 */
static bool vm_net_mock_refine_teleport_stone_confirmed_target(
    const vm_net_mock_scene_change_target *provisional,
    u32 confirmedExitId,
    vm_net_mock_scene_change_target *target,
    const char **posSourceOut)
{
    vm_net_mock_scene_change_target baseTarget;

    if (provisional == NULL || target == NULL || provisional->scene[0] == 0)
        return false;

    baseTarget = *provisional;
    *target = baseTarget;
    if (posSourceOut)
        *posSourceOut = "confirmed-exit-matches-provisional";

    if (confirmedExitId == 0 || confirmedExitId == baseTarget.exitId)
        return true;

    return vm_net_mock_get_teleport_stone_smap_row_target(confirmedExitId,
                                                           target,
                                                           posSourceOut);
}

static bool vm_net_mock_get_teleport_stone_map_target(const u8 *request, u32 requestLen,
                                                      vm_net_mock_scene_change_target *target,
                                                      u32 *curIdOut,
                                                      u32 *objIdOut,
                                                      const char **sourceOut,
                                                      const char **posSourceOut,
                                                      u32 *smapRowOut,
                                                      u32 *sceneCountOut,
                                                      const char **rowSourceOut)
{
    u32 curId = 0;
    u32 objId = 0;
    char mappedScene[64];
    const char *targetScene = NULL;
    const char *source = "-";
    const char *posSource = "-";
    u16 dshX = 0;
    u16 dshY = 0;
    u16 sceEntryId = 0xffff;
    u32 dshTargetRowId = 0;
    u32 dshBaseRowId = 0;
    u32 dshSceneCount = 0;
    const char *dshRowSource = "-";
    bool targetResourceExists = false;

    memset(target, 0, sizeof(*target));
    mappedScene[0] = 0;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "curid", &curId);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "objid", &objId);
    if (curIdOut)
        *curIdOut = curId;
    if (objIdOut)
        *objIdOut = objId;

    if (vm_net_mock_find_teleport_stone_scene_by_dsh(objId, curId,
                                                     mappedScene, sizeof(mappedScene),
                                                     &dshX, &dshY,
                                                     &dshTargetRowId,
                                                     &dshBaseRowId,
                                                     &dshSceneCount,
                                                     &dshRowSource))
    {
        targetScene = mappedScene;
        source = "wmap-smap-dsh";
    }
    else
    {
        if (sourceOut)
            *sourceOut = "unresolved-wmap-smap";
        if (posSourceOut)
            *posSourceOut = "-";
        if (smapRowOut)
            *smapRowOut = 0;
        if (sceneCountOut)
            *sceneCountOut = 0;
        if (rowSourceOut)
            *rowSourceOut = "-";
        return false;
    }

    if (!vm_net_mock_scene_name_is_download_key(targetScene))
    {
        if (sourceOut)
            *sourceOut = "invalid-smap-scene";
        if (posSourceOut)
            *posSourceOut = "-";
        if (smapRowOut)
            *smapRowOut = dshTargetRowId;
        if (sceneCountOut)
            *sceneCountOut = dshSceneCount;
        if (rowSourceOut)
            *rowSourceOut = dshRowSource;
        return false;
    }

    targetResourceExists = vm_net_mock_scene_resource_exists(targetScene);
    if (targetResourceExists)
    {
        /*
         * Keep the authoritative sMap.dsh key byte-for-byte for map-stone
         * entry.  JianghuOL:LoadSceneRes(0x0103130A) later passes the current
         * scene string to LoadMapDataSheet(0x0103581E, mode 4), which performs
         * an exact lookup against sMap.dsh's map-name column before updating
         * the world-map current-world/current-child indices.  Stripping the
         * `.sce` suffix from c-prefixed targets makes that lookup miss and
         * leaves the previous (commonly Penglai) node highlighted.
         *
         * This exception is deliberately limited to DSH-resolved map-stone
         * targets.  Normal login/portal entry keeps the established
         * extensionless normalization, while loose scene comparisons and the
         * resource loader already accept both key forms.
         */
        snprintf(target->scene, sizeof(target->scene), "%s", targetScene);
    }
    else if (vm_net_mock_scene_name_is_download_key(targetScene))
    {
        snprintf(target->scene, sizeof(target->scene), "%s", targetScene);
        target->needsSceneDownload = true;
    }
    if (targetResourceExists &&
        vm_net_mock_get_scene_reasonable_spawn_from_sce(target->scene,
                                                        &target->x,
                                                        &target->y,
                                                        &sceEntryId))
    {
        posSource = sceEntryId == 0xffff ? "sce-center-fallback" : "sce-nearest-entry";
        target->hasSceEntry = true;
        printf("[info][network] mock_scene_landing_resolve scene=%s smap_marker=(%u,%u) landing=(%u,%u) source=%s entry=%u\n",
               target->scene, dshX, dshY, target->x, target->y,
               posSource, sceEntryId);
        vm_autotest_note("mock_scene_landing_resolve scene=%s smap_marker=(%u,%u) landing=(%u,%u) source=%s entry=%u evidence=SCE2-edge-portal\n",
                         target->scene, dshX, dshY, target->x, target->y,
                         posSource, sceEntryId);
    }
    else if (!targetResourceExists && (dshX != 0 || dshY != 0))
    {
        /*
         * The local SCE is unavailable, so the real scene-space entry cannot be
         * resolved yet. Keep the old marker only as a download-path fallback;
         * installed scenes must never use sMap.dsh UI coordinates as actor data.
         */
        target->x = dshX;
        target->y = dshY;
        posSource = "smap-ui-marker-fallback-missing-sce";
    }
    else
    {
        if (sourceOut)
            *sourceOut = source;
        if (posSourceOut)
            *posSourceOut = targetResourceExists ? "missing-sce-landing" : "missing-smap-marker";
        if (smapRowOut)
            *smapRowOut = dshTargetRowId;
        if (sceneCountOut)
            *sceneCountOut = dshSceneCount;
        if (rowSourceOut)
            *rowSourceOut = dshRowSource;
        return false;
    }
    /* Provisional base row only. The confirmed 16/2.exitID replaces this with
     * the exact selected child row before any 30/1 scene entry is armed. */
    target->exitId = dshTargetRowId;
    target->mapType = 2;
    if (!targetResourceExists)
        vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
    if (target->needsSceneDownload)
    {
        vm_autotest_note("mock_teleport_stone_map_missing_sce curid=%u objid=%u scene=%s smap_row=%u base=%u count=%u pos=(%u,%u)\n",
                         curId, objId, target->scene, dshTargetRowId,
                         dshBaseRowId, dshSceneCount, target->x, target->y);
    }

    if (curIdOut)
        *curIdOut = curId;
    if (objIdOut)
        *objIdOut = objId;
    if (sourceOut)
        *sourceOut = source;
    if (posSourceOut)
        *posSourceOut = posSource;
    if (smapRowOut)
        *smapRowOut = dshTargetRowId;
    if (sceneCountOut)
        *sceneCountOut = dshSceneCount;
    if (rowSourceOut)
        *rowSourceOut = dshRowSource;
    return true;
}

static const char *vm_net_mock_teleport_stone_display_label(void)
{
    /* GBK: 蓬莱-铜雀台.  This is the display name of c00蓬莱仙岛_01.sce,
     * which is also the default target selected by the existing builder. */
    static const char defaultLabel[] =
        "\xc5\xee\xc0\xb3-\xcd\xad\xc8\xb8\xcc\xa8";
    return vm_net_mock_env_str("CBE_TELEPORT_STONE_LABEL", defaultLabel);
}

static bool vm_net_mock_build_teleport_stone_exitinfo_blob(u8 *out, u32 outCap, u32 *blobLenOut)
{
    u32 pos = 0;
    const char *label = vm_net_mock_teleport_stone_display_label();
    u32 exitId = vm_net_mock_env_u32("CBE_TELEPORT_STONE_EXIT_ID", VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID);

    if (out == NULL || blobLenOut == NULL)
        return false;
    if (exitId > 0xffff)
        exitId = VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID;

    /*
     * mmGameMstarWqvga.cbm:sub_11CE parses 16/1 exitinfo as:
     *   u8 count, repeated u32 exitId, len16 string label, u32 reserved.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, exitId))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, label))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, 0))
        return false;

    *blobLenOut = pos;
    return true;
}

static u32 vm_net_mock_build_teleport_stone_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 exitInfo[128];
    u32 exitInfoLen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_backpack_item_state *teleportStone =
        role ? vm_net_mock_role_find_backpack_item(
                   role, VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID, 0)
             : NULL;
    u32 teleportStoneCount = teleportStone ? teleportStone->count : 0;
    u32 wcoin = vm_net_mock_role_wcoin_balance(role);

    if (outCap < pos)
        return 0;
    memset(exitInfo, 0, sizeof(exitInfo));
    if (!vm_net_mock_build_teleport_stone_exitinfo_blob(exitInfo, sizeof(exitInfo), &exitInfoLen))
        return 0;
    if (exitInfoLen == 0 || exitInfoLen > 0xffff)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x10, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "exitinfo", exitInfo, (u16)exitInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_vm_net_mock_last_teleport_stone_list_tick = g_schedulerTick;

    printf("[info][network] mock_teleport_stone_list entries=1 exit=%u label=%s role=%u item800=%u wcoin=%u exitinfo_len=%u evidence=mmGame:0x11CE\n",
           vm_net_mock_env_u32("CBE_TELEPORT_STONE_EXIT_ID",
                               VM_NET_MOCK_TELEPORT_STONE_DEFAULT_EXIT_ID),
           vm_net_mock_teleport_stone_display_label(),
           role ? role->roleId : 0,
           teleportStoneCount,
           wcoin,
           exitInfoLen);
    vm_autotest_note("mock_teleport_stone_list entries=1 role=%u item800=%u wcoin=%u exitinfo_len=%u evidence=mmGame:0x11CE\n",
                     role ? role->roleId : 0,
                     teleportStoneCount,
                     wcoin,
                     exitInfoLen);
    return pos;
}

static bool vm_net_mock_put_teleport_stone_scene_fields_with_result(u8 *out, u32 outCap, u32 *pos,
                                                                     u8 resultValue,
                                                                     const vm_net_mock_scene_change_target *target)
{
    u8 posInfo[8];
    u32 posInfoLen = 0;

    if (target == NULL)
        return false;
    /*
     * mmGame sub_BCC reads two tagged signed i16 values unchanged and passes
     * them to the main-business API +0x74 callback. EnterSceneByMapName and
     * scene_runtime_init_and_sync then store/copy the same values unchanged,
     * so this path uses the normal SCE pixel coordinate unit.
     */
    posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), target->x, target->y);
    if (posInfoLen == 0)
        return false;
    /*
     * mmGame:0x11CE reads result through JianghuOL:0x01033C6C, which returns
     * value[2]. The field must therefore use the typed-u8 object encoding
     * 00 01 xx; raw-u32 makes result read back as 0 and stalls 16/3 loading.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", resultValue))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", target->scene))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen))
        return false;
    return vm_net_mock_put_object_u16(out, outCap, pos, "exitid", (u16)target->exitId);
}

static bool vm_net_mock_put_teleport_stone_scene_fields(u8 *out, u32 outCap, u32 *pos,
                                                        u8 subtype,
                                                        const vm_net_mock_scene_change_target *target)
{
    return vm_net_mock_put_teleport_stone_scene_fields_with_result(out, outCap, pos,
                                                                   subtype == 3 ? 2 : 1,
                                                                   target);
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, const char *sceneOverride,
                                                               bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly,
                                                               bool preferSceneNpcOther);
static bool vm_net_mock_append_scene_enter_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneName, u16 spawnX, u16 spawnY);

static bool vm_net_mock_append_mmgame_scene_transfer_object(u8 *out, u32 outCap, u32 *pos,
                                                            u8 subtype,
                                                            const vm_net_mock_scene_change_target *target)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x10, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_teleport_stone_scene_fields(out, outCap, pos, subtype, target))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_mmgame_scene_transfer_object_with_result(u8 *out, u32 outCap, u32 *pos,
                                                                        u8 subtype, u8 result,
                                                                        const vm_net_mock_scene_change_target *target)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x10, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_teleport_stone_scene_fields_with_result(out, outCap, pos, result, target))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_mmgame_scene_transfer_empty_object(u8 *out, u32 outCap, u32 *pos,
                                                                  u8 subtype)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x10, subtype, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_settings_unstuck_ack_object(u8 *out, u32 outCap, u32 *pos,
                                                           u32 id)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0c, 3, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "id", (u16)id))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_mmgame_scene_transfer_start_response(const vm_net_mock_scene_change_target *target,
                                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (target == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_append_mmgame_scene_transfer_object(out, outCap, &pos, 3, target))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_mmgame_scene_transfer_start scene=%s pos=(%u,%u) resp=%u\n",
           target->scene, target->x, target->y, pos);
    vm_autotest_note("mock_mmgame_scene_transfer_start scene=%s pos=(%u,%u) response=16/3 evidence=mmGame:0x11CE,0x0BCC\n",
                     target->scene, target->x, target->y);
    return pos;
}

static u32 vm_net_mock_build_scene_channel_enter_combo_for_target(const vm_net_mock_scene_change_target *target,
                                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;

    if (target == NULL || target->scene[0] == 0 || outCap < pos)
        return 0;
    if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                         target->scene, target->x, target->y))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_net_mock_is_settings_unstuck_request(const u8 *request, u32 requestLen, u32 *idOut)
{
    u32 offset = 4;
    u32 id = 0;
    vm_net_mock_request_object object;
    bool matched = false;

    if (idOut)
        *idOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if ((object.major == 0 || object.major == 1) &&
            object.kind == 0x0c &&
            object.subtype == 3 &&
            (vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &id) ||
             vm_net_mock_request_contains(object.payload, object.payloadLen, "id")))
        {
            matched = true;
            break;
        }
    }
    if (!matched)
        return false;
    if (idOut)
        *idOut = id;
    return true;
}

static void vm_net_mock_get_current_scene_unstuck_target(vm_net_mock_scene_change_target *target)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    const char *scene = NULL;
    u16 fromX = 0;
    u16 fromY = 0;
    u16 entryX = 0;
    u16 entryY = 0;
    u16 entryId = 0xffff;
    const char *targetSource = "current-pos";
    const char *sceneSource = "role-db";
    const char *fromSource = "role-pos";

    memset(target, 0, sizeof(*target));
    /*
     * 12/3 belongs to the authenticated service session.  The role's scene
     * and position are durable request-scoped state; the host CBE runtime grid
     * is only a local-emulator fallback and must never supersede them.
     */
    if (role != NULL && vm_net_mock_scene_name_is_safe(role->scene))
    {
        scene = role->scene;
        fromX = role->x;
        fromY = role->y;
        if (session != NULL &&
            session->sceneVisibleReady && !session->sceneVisiblePending &&
            vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) &&
            vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene, scene) &&
            session->sceneVisibleX != 0 && session->sceneVisibleY != 0)
        {
            fromX = session->sceneVisibleX;
            fromY = session->sceneVisibleY;
            fromSource = "session-visible";
        }
    }
    else
    {
        scene = vm_net_mock_current_scene_name();
        fromX = vm_net_mock_scene_spawn_x();
        fromY = vm_net_mock_scene_spawn_y();
        sceneSource = "runtime-fallback";
        if (vm_net_mock_read_current_player_grid(NULL, NULL, &fromX, &fromY, NULL, NULL))
            fromSource = "runtime-grid";
    }
    if (!vm_net_mock_scene_name_is_safe(scene))
    {
        scene = vm_net_mock_default_scene_name();
        sceneSource = "default-fallback";
    }
    if (fromX == 0)
        fromX = vm_net_mock_scene_spawn_x();
    if (fromY == 0)
        fromY = vm_net_mock_scene_spawn_y();
    snprintf(target->scene, sizeof(target->scene), "%s", scene);

    if (vm_net_mock_get_scene_nearest_entry_spawn_from_sce(target->scene,
                                                           fromX,
                                                           fromY,
                                                           &entryX,
                                                           &entryY,
                                                           &entryId))
    {
        target->x = entryX;
        target->y = entryY;
        targetSource = "sce-nearest-entry";
    }
    else if (vm_net_mock_get_scene_center_spawn_from_sce(target->scene, &entryX, &entryY))
    {
        target->x = entryX;
        target->y = entryY;
        targetSource = "sce-center";
    }
    else
    {
        target->x = fromX;
        target->y = fromY;
        vm_net_mock_adjust_safe_player_pos_for_scene(target->scene, &target->x, &target->y);
    }
    target->exitId = 0;
    target->mapType = 2;
    target->hasSceEntry = strcmp(targetSource, "current-pos") != 0;
    target->needsSceneDownload = false;
    printf("[info][network] mock_unstuck_target scene=%s scene_source=%s from=(%u,%u) from_source=%s pos=(%u,%u) source=%s entry=%u\n",
           target->scene,
           sceneSource,
           fromX,
           fromY,
           fromSource,
           target->x,
           target->y,
           targetSource,
           entryId);
}

static u32 vm_net_mock_build_settings_unstuck_response(const u8 *request, u32 requestLen,
                                                       u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 id = 0;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_settings_unstuck_request(request, requestLen, &id))
        return 0;

    vm_net_mock_get_current_scene_unstuck_target(&target);
    if (!vm_net_mock_append_settings_unstuck_ack_object(out, outCap, &pos, id))
        return 0;
    if (!vm_net_mock_append_mmgame_scene_transfer_object(out, outCap, &pos, 3, &target))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 2);

    vm_net_mock_mark_direct_scene_enter_completed(&target, "settings-unstuck-target");
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "settings-unstuck-target");
    printf("[info][network] mock_settings_unstuck id=%u scene=%s pos=(%u,%u) response=12/3+16/3 resp=%u\n",
           id, target.scene, target.x, target.y, pos);
    vm_autotest_note("mock_settings_unstuck id=%u scene=%s pos=(%u,%u) response=12/3+16/3 evidence=mmGame:0x5BCA,0x6512,0x11CE,0x0BCC\n",
                     id, target.scene, target.x, target.y);
    return pos;
}

static bool vm_net_mock_is_settings_unstuck_16_2_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    if (kind != 0x10 || subtype != 2)
        return false;
    if (g_vm_net_mock_last_teleport_stone_list_tick != 0 &&
        (g_schedulerTick - g_vm_net_mock_last_teleport_stone_list_tick) < 600)
    {
        return false;
    }
    /*
     * mmGame's compact settings/unstuck path can send a 16/2 object with only
     * `type`. Teleport-stone selection carries explicit exit/scene context or
     * happens immediately after 16/1 exitinfo, so keep those on the normal
     * confirmation path.
     */
    return vm_net_mock_request_contains(request, requestLen, "type") &&
           !vm_net_mock_request_contains(request, requestLen, "exitID") &&
           !vm_net_mock_request_contains(request, requestLen, "exitid") &&
           !vm_net_mock_request_contains(request, requestLen, "scene") &&
           !vm_net_mock_request_contains(request, requestLen, "posinfo");
}

static u32 vm_net_mock_build_settings_unstuck_16_2_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 pos = 5;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_settings_unstuck_16_2_request(request, requestLen))
        return 0;

    vm_net_mock_get_current_scene_unstuck_target(&target);
    if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(out, outCap, &pos,
                                                                     2, 1, &target))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_net_mock_mark_direct_scene_enter_completed(&target, "settings-unstuck-16-2-target");
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "settings-unstuck-16-2-target");
    printf("[info][network] mock_settings_unstuck_16_2 scene=%s pos=(%u,%u) response=16/2-direct-enter resp=%u\n",
           target.scene, target.x, target.y, pos);
    vm_autotest_note("mock_settings_unstuck_16_2 scene=%s pos=(%u,%u) response=16/2-result1 evidence=mmGame:0x11CE/0x0BCC\n",
                     target.scene, target.x, target.y);
    return pos;
}

/*
 * A direct mmGame 16/2 scene entry reaches JianghuOL.CBE's scene-runtime
 * initializer with parser state 2 or 3.  That initializer then sends exactly
 * one 16/3 object whose `exitID` is the current X coordinate encoded as an
 * i16 and whose `type` is zero (JianghuOL.CBE:0x0101359C).  It is a runtime
 * synchronization acknowledgement, not a teleport-stone selection: treating
 * its coordinate as an sMap exit id makes the generic 16/3 handler invent and
 * persist the default teleport scene.
 */
static bool vm_net_mock_is_scene_runtime_position_ack_16_3_request(
    const u8 *request, u32 requestLen, u16 *positionXOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 fieldPos = 0;
    u16 positionX = 0;
    u8 type = 0;
    bool havePositionX = false;
    bool haveType = false;

    if (positionXOut)
        *positionXOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 0x10 ||
        object.subtype != 3)
    {
        return false;
    }

    while (fieldPos < object.payloadLen)
    {
        u8 nameLen = 0;
        u16 valueLen = 0;
        const u8 *name = NULL;
        const u8 *value = NULL;

        if (fieldPos + 3 > object.payloadLen)
            return false;
        nameLen = object.payload[fieldPos++];
        if (fieldPos + nameLen + 2 > object.payloadLen)
            return false;
        name = object.payload + fieldPos;
        fieldPos += nameLen;
        valueLen = (u16)(((u16)object.payload[fieldPos] << 8) |
                         object.payload[fieldPos + 1]);
        fieldPos += 2;
        if (fieldPos + valueLen > object.payloadLen)
            return false;
        value = object.payload + fieldPos;
        fieldPos += valueLen;

        if (nameLen == 6 && memcmp(name, "exitID", 6) == 0)
        {
            if (havePositionX)
                return false;
            if (valueLen == 2)
            {
                positionX = (u16)(((u16)value[0] << 8) | value[1]);
            }
            else if (valueLen == 4 && value[0] == 0 && value[1] == 2)
            {
                positionX = (u16)(((u16)value[2] << 8) | value[3]);
            }
            else
            {
                return false;
            }
            havePositionX = true;
        }
        else if (nameLen == 4 && memcmp(name, "type", 4) == 0)
        {
            if (haveType)
                return false;
            if (valueLen == 1)
            {
                type = value[0];
            }
            else if (valueLen == 3 && value[0] == 0 && value[1] == 1)
            {
                type = value[2];
            }
            else
            {
                return false;
            }
            haveType = true;
        }
    }

    if (!havePositionX || !haveType || type != 0)
        return false;
    if (positionXOut)
        *positionXOut = positionX;
    return true;
}

static u32 vm_net_mock_build_scene_runtime_position_ack_16_3_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u16 positionX = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_scene_runtime_position_ack_16_3_request(request, requestLen,
                                                                 &positionX))
    {
        return 0;
    }

    /* This request only closes the scene-runtime send.  In particular do not
     * remember a new scene target or persist `positionX` as a teleport exit. */
    vm_net_mock_finish_wt_packet(out, pos, 0);
    printf("[info][network] mock_scene_runtime_position_ack_16_3 posx=%u response=empty-wt action=no-scene-target-or-position-save evidence=JianghuOL.CBE:0x0101359C(parserState2or3)\n",
           positionX);
    vm_autotest_note("mock_scene_runtime_position_ack_16_3 posx=%u response=empty-wt evidence=JianghuOL.CBE:0x0101359C\n",
                     positionX);
    return pos;
}

static u32 vm_net_mock_build_teleport_stone_transfer_response(const u8 *request, u32 requestLen,
                                                              u8 subtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    vm_net_mock_scene_change_target target;
    bool targetAlreadyPending = false;
    bool shouldRearmTarget = false;
    bool targetFromConfirm = false;
    const char *responseKind = "16/3-scene";

    if (outCap < pos || (subtype != 2 && subtype != 3))
        return 0;

    if (subtype == 2)
    {
        /*
         * A world-map 16/4 request identifies only the target wMap group. The
         * client's confirmation callback later emits the selected child row
         * as 16/2.exitID (and repeats it in 16/3). Refine the provisional base
         * target from that packet-visible row before preserving it across the
         * normal confirmation chain.
         */
        if (g_vm_net_mock_teleport_stone_confirm_target_valid)
        {
            u32 confirmedExitId = 0;
            const char *confirmedPosSource = "-";
            target = g_vm_net_mock_teleport_stone_confirm_target;
            targetFromConfirm = true;
            if ((vm_net_mock_get_object_u32_field(request, requestLen,
                                                  "exitID", &confirmedExitId) ||
                 vm_net_mock_get_object_u32_field(request, requestLen,
                                                  "exitid", &confirmedExitId)) &&
                confirmedExitId != 0)
            {
                if (!vm_net_mock_refine_teleport_stone_confirmed_target(
                        &target, confirmedExitId, &target, &confirmedPosSource))
                {
                    printf("[error][network] mock_teleport_stone_confirm_target_unresolved subtype=%u exit=%u provisional_scene=%s action=no-wrong-scene-fallback\n",
                           subtype, confirmedExitId, target.scene);
                    return 0;
                }
                g_vm_net_mock_teleport_stone_confirm_target = target;
                printf("[info][network] mock_teleport_stone_confirm_target_refine subtype=%u exit=%u scene=%s pos=(%u,%u) pos_source=%s\n",
                       subtype, confirmedExitId, target.scene, target.x, target.y,
                       confirmedPosSource);
            }
        }
        else
        {
            vm_net_mock_get_teleport_stone_target(request, requestLen, &target);
        }
        if (targetFromConfirm)
        {
            /*
             * ConsumeInventoryItem() sends 16/2 before the authoritative 16/3
             * scene-exit request. mmGame:0x11CE treats a 16/2 result=2 object
             * as the recharge prompt, while a zero-object WT packet is only a
             * transport acknowledgement. Keep the resolved map target alive
             * for 16/3 and let that packet perform the actual 30/1 entry.
             */
            vm_net_mock_finish_wt_packet(out, pos, 0);
            g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
            g_vm_net_mock_teleport_stone_direct_enter_pending = false;
            g_vm_net_mock_teleport_stone_map_enter_pending = false;
            responseKind = "empty-wt-await-16/3";
            printf("[info][network] mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=0 confirm=1 resp=%u\n",
                   subtype, target.exitId, target.scene, target.x, target.y,
                   responseKind, pos);
            vm_autotest_note("mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=0 confirm=1 evidence=JianghuOL:0x01018F66/mmGame:0x11CE\n",
                             subtype, target.exitId, target.scene, target.x, target.y,
                             responseKind);
            return pos;
        }
        if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(out, outCap, &pos,
                                                                         2, 2, &target))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 1);
        g_vm_net_mock_teleport_stone_confirm_target = target;
        g_vm_net_mock_teleport_stone_confirm_target_valid = true;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
        g_vm_net_mock_teleport_stone_direct_enter_pending = false;
        g_vm_net_mock_teleport_stone_map_enter_pending = false;
        responseKind = "16/2-confirm-target";
        printf("[info][network] mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=0 confirm=%u resp=%u\n",
               subtype, target.exitId, target.scene, target.x, target.y,
               responseKind, targetFromConfirm ? 1u : 0u, pos);
        vm_autotest_note("mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=0 confirm=%u evidence=JianghuOL:0x01018F66/0x0103573A\n",
                         subtype, target.exitId, target.scene, target.x, target.y,
                         responseKind, targetFromConfirm ? 1u : 0u);
        return pos;
    }

    if (g_vm_net_mock_teleport_stone_confirm_target_valid)
    {
        u32 confirmedExitId = 0;
        const char *confirmedPosSource = "-";
        target = g_vm_net_mock_teleport_stone_confirm_target;
        if ((vm_net_mock_get_object_u32_field(request, requestLen,
                                              "exitID", &confirmedExitId) ||
             vm_net_mock_get_object_u32_field(request, requestLen,
                                              "exitid", &confirmedExitId)) &&
            confirmedExitId != 0)
        {
            if (!vm_net_mock_refine_teleport_stone_confirmed_target(
                    &target, confirmedExitId, &target, &confirmedPosSource))
            {
                printf("[error][network] mock_teleport_stone_confirm_target_unresolved subtype=%u exit=%u provisional_scene=%s action=no-wrong-scene-fallback\n",
                       subtype, confirmedExitId, target.scene);
                return 0;
            }
            printf("[info][network] mock_teleport_stone_confirm_target_refine subtype=%u exit=%u scene=%s pos=(%u,%u) pos_source=%s\n",
                   subtype, confirmedExitId, target.scene, target.x, target.y,
                   confirmedPosSource);
        }
        g_vm_net_mock_teleport_stone_confirm_target_valid = false;
        targetFromConfirm = true;
    }
    else if (g_vm_net_mock_last_scene_change_target_valid)
    {
        target = g_vm_net_mock_last_scene_change_target;
        targetAlreadyPending = true;
    }
    else
    {
        vm_net_mock_get_teleport_stone_target(request, requestLen, &target);
    }

    if (targetAlreadyPending)
    {
        if (g_vm_net_mock_teleport_stone_subtype3_ack_sent)
        {
            if (!vm_net_mock_append_mmgame_scene_transfer_empty_object(out, outCap, &pos, subtype))
                return 0;
            vm_net_mock_finish_wt_packet(out, pos, 1);
            responseKind = "16/3-duplicate-noop";
        }
        else
        {
            pos = vm_net_mock_build_scene_channel_enter_combo_for_target(&target, out, outCap);
            if (pos == 0)
                return 0;
            g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
            g_vm_net_mock_teleport_stone_direct_enter_pending = true;
            g_vm_net_mock_teleport_stone_map_enter_pending = false;
            shouldRearmTarget = true;
            responseKind = "scene-channel-enter-saved-target";
        }
    }
    else
    {
        pos = vm_net_mock_build_scene_channel_enter_combo_for_target(&target, out, outCap);
        if (pos == 0)
            return 0;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
        g_vm_net_mock_teleport_stone_direct_enter_pending = true;
        g_vm_net_mock_teleport_stone_map_enter_pending = false;
        responseKind = targetFromConfirm ? "scene-channel-enter-confirm-target" : "scene-channel-enter";
    }

    if (!targetAlreadyPending || shouldRearmTarget)
        vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    if (!targetAlreadyPending)
        vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "teleport-stone-target");
    printf("[info][network] mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=%u confirm=%u resp=%u\n",
           subtype, target.exitId, target.scene, target.x, target.y,
           responseKind,
           targetAlreadyPending ? 1u : 0u,
           targetFromConfirm ? 1u : 0u,
           pos);
    vm_autotest_note("mock_teleport_stone_transfer subtype=%u exit=%u scene=%s pos=(%u,%u) response=%s pending=%u confirm=%u evidence=JianghuOL:0x01012E4D/0x01039B8A/0x010396D6\n",
                     subtype, target.exitId, target.scene, target.x, target.y,
                     responseKind,
                     targetAlreadyPending ? 1u : 0u,
                     targetFromConfirm ? 1u : 0u);
    return pos;
}

static bool vm_net_mock_parse_teleport_stone_confirmed_exit_combo(
    const u8 *request, u32 requestLen,
    u32 *itemObjectStartOut, u32 *itemObjectLenOut,
    u32 *objectCountOut, u32 *exitIdOut, u8 *typeOut)
{
    u32 offset = 4;
    u32 objectCount = 0;
    u32 exitId2 = 0;
    u32 exitId3 = 0;
    u32 type2 = 0;
    u32 type3 = 0;
    u32 itemObjectStart = 0;
    u32 itemObjectLen = 0;
    bool haveSubtype2 = false;
    bool haveSubtype3 = false;
    bool haveItemUse = false;
    vm_net_mock_request_object object;

    if (itemObjectStartOut)
        *itemObjectStartOut = 0;
    if (itemObjectLenOut)
        *itemObjectLenOut = 0;
    if (objectCountOut)
        *objectCountOut = 0;
    if (exitIdOut)
        *exitIdOut = 0;
    if (typeOut)
        *typeOut = 0;
    if (!g_vm_net_mock_teleport_stone_confirm_target_valid ||
        request == NULL || requestLen < 14 ||
        request[0] != 'W' || request[1] != 'T')
    {
        return false;
    }

    while (offset < requestLen)
    {
        u32 objectStart = offset;
        if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
            return false;
        ++objectCount;

        if (object.major == 1 && object.kind == 0x10 && object.subtype == 2)
        {
            if (objectCount != 1 || haveSubtype2 ||
                !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                     "exitID", &exitId2) ||
                !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                     "type", &type2))
            {
                return false;
            }
            haveSubtype2 = true;
            continue;
        }
        if (object.major == 1 && object.kind == 0x10 && object.subtype == 3)
        {
            if (!haveSubtype2 || haveSubtype3 ||
                !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                     "exitID", &exitId3) ||
                !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                     "type", &type3))
            {
                return false;
            }
            haveSubtype3 = true;
            continue;
        }
        if (object.major == 1 && object.kind == 7 && object.subtype == 1)
        {
            if (!haveSubtype2 || !haveSubtype3 || haveItemUse || object.payloadLen == 0)
                return false;
            haveItemUse = true;
            itemObjectStart = objectStart;
            itemObjectLen = offset - objectStart;
            continue;
        }
        return false;
    }

    if (offset != requestLen || !haveSubtype2 || !haveSubtype3 ||
        exitId2 != exitId3 || type2 == 0 || type2 != type3 || type2 > 0xffu)
    {
        return false;
    }
    if (itemObjectStartOut)
        *itemObjectStartOut = itemObjectStart;
    if (itemObjectLenOut)
        *itemObjectLenOut = itemObjectLen;
    if (objectCountOut)
        *objectCountOut = objectCount;
    if (exitIdOut)
        *exitIdOut = exitId2;
    if (typeOut)
        *typeOut = (u8)type2;
    return true;
}

static u32 vm_net_mock_build_teleport_stone_confirmed_exit_combo_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u8 itemRequest[512]; /* Matches the host's bounded async WT request size. */
    u8 itemResponse[1024];
    u32 itemObjectStart = 0;
    u32 itemObjectLen = 0;
    u32 objectCount = 0;
    u32 exitId = 0;
    u8 type = 0;
    u32 itemRequestLen = 0;
    u32 itemResponseLen = 0;
    vm_net_mock_scene_change_target target;
    vm_net_mock_scene_change_target provisionalTarget;
    const char *posSource = "-";

    if (out == NULL || outCap < 5 ||
        !vm_net_mock_parse_teleport_stone_confirmed_exit_combo(
            request, requestLen,
            &itemObjectStart, &itemObjectLen,
            &objectCount, &exitId, &type))
    {
        return 0;
    }

    provisionalTarget = g_vm_net_mock_teleport_stone_confirm_target;
    if (!vm_net_mock_refine_teleport_stone_confirmed_target(
            &provisionalTarget, exitId, &target, &posSource))
    {
        printf("[error][network] mock_teleport_stone_confirmed_exit_unresolved exit=%u provisional_scene=%s provisional_row=%u action=no-wrong-scene-fallback\n",
               exitId, provisionalTarget.scene, provisionalTarget.exitId);
        return 0;
    }
    printf("[info][network] mock_teleport_stone_confirmed_exit_resolve exit=%u provisional_scene=%s provisional_row=%u final_scene=%s pos=(%u,%u) pos_source=%s changed=%u evidence=JianghuOL.CBE:0x0103573A(wMap ids)+0x01018F66(exitID)\n",
           exitId,
           provisionalTarget.scene,
           provisionalTarget.exitId,
           target.scene,
           target.x,
           target.y,
           posSource,
           vm_net_mock_scene_names_equal_loose(provisionalTarget.scene,
                                               target.scene)
               ? 0u
               : 1u);

    if (itemObjectLen != 0)
    {
        itemRequestLen = 4 + itemObjectLen;
        if (itemRequestLen > sizeof(itemRequest) ||
            itemObjectStart + itemObjectLen > requestLen)
        {
            return 0;
        }
        itemRequest[0] = 'W';
        itemRequest[1] = 'T';
        itemRequest[2] = (u8)((itemRequestLen >> 8) & 0xff);
        itemRequest[3] = (u8)(itemRequestLen & 0xff);
        memcpy(itemRequest + 4, request + itemObjectStart, itemObjectLen);
        itemResponseLen = vm_net_mock_build_item_use_response(itemRequest, itemRequestLen,
                                                              itemResponse, sizeof(itemResponse));
        if (itemResponseLen < 5 || itemResponse[0] != 'W' || itemResponse[1] != 'T')
            return 0;
    }

    /*
     * The runtime request is one WT packet containing 16/2 + 16/3 (+ 7/1
     * when a stone is consumed), so the saved target must still be consumed
     * here.  Do not append 30/1 to the item response, though.  Runtime crash
     * evidence at JianghuOL.CBE:0x01018136 -> 0x01046189 -> 0x01005AF4
     * shows that same-callback scene entry removes the current scene while the
     * CBM confirmation window is still underneath it.  Its loading widget then
     * draws through a resource owner whose pixel buffer has already been
     * cleared.  Return only the inventory acknowledgement now and let a later
     * service-poll event deliver the one-shot 30/1 after the confirmation
     * callback has unwound.
     */
    if (itemResponseLen > outCap)
        return 0;

    g_vm_net_mock_teleport_stone_confirm_target_valid = false;
    g_vm_net_mock_teleport_stone_deferred_enter_target = target;
    g_vm_net_mock_teleport_stone_deferred_enter_valid = true;
    g_vm_net_mock_teleport_stone_deferred_enter_tick = g_schedulerTick;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;

    if (itemResponseLen >= 5)
        memcpy(out, itemResponse, itemResponseLen);
    else
        vm_net_mock_finish_wt_packet(out, 5, 0);

    printf("[info][network] mock_teleport_stone_confirmed_exit_combo request_objects=%u exit=%u type=%u item_request=%u item_response=%u response_objects=%u deferred_scene=1 scene=%s pos=(%u,%u) target_source=confirmed-exitID armed_tick=%u resp=%u\n",
           objectCount, exitId, type, itemRequestLen, itemResponseLen,
           itemResponseLen >= 5 ? itemResponse[4] : 0,
           target.scene, target.x, target.y,
           g_vm_net_mock_teleport_stone_deferred_enter_tick,
           itemResponseLen >= 5 ? itemResponseLen : 5);
    vm_autotest_note("mock_teleport_stone_confirmed_exit_combo request_objects=%u exit=%u type=%u item=%u response_objects=%u response=item-ack-only deferred_scene=1 evidence=runtime:wt16/2-len130+crash:0x01018136/0x01046189/0x01005AF4\n",
                     objectCount, exitId, type, itemObjectLen ? 1u : 0u,
                     itemResponseLen >= 5 ? itemResponse[4] : 0);
    return itemResponseLen >= 5 ? itemResponseLen : 5;
}

static u32 vm_net_mock_build_teleport_stone_deferred_enter_response(u8 *out,
                                                                    u32 outCap)
{
    vm_net_mock_scene_change_target target;
    u32 pos = 0;
    u32 armedTick = g_vm_net_mock_teleport_stone_deferred_enter_tick;

    if (!g_vm_net_mock_teleport_stone_deferred_enter_valid ||
        out == NULL || outCap < 5)
    {
        return 0;
    }
    /* A poll queued in the request's own scheduler tick is not a safe phase
     * boundary.  Wait for at least the next 100 ms client frame so the item/CBM
     * callback and its screen removal have completed. */
    if (g_schedulerTick == armedTick)
        return 0;

    target = g_vm_net_mock_teleport_stone_deferred_enter_target;
    pos = vm_net_mock_build_scene_channel_enter_combo_for_target(&target, out, outCap);
    if (pos == 0)
        return 0;

    g_vm_net_mock_teleport_stone_deferred_enter_valid = false;
    g_vm_net_mock_teleport_stone_deferred_enter_tick = 0;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
    g_vm_net_mock_teleport_stone_direct_enter_pending = true;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y,
                                      "teleport-stone-deferred-target");

    printf("[info][network] mock_teleport_stone_deferred_enter scene=%s pos=(%u,%u) armed_tick=%u deliver_tick=%u response=scene-channel-enter-confirm-target resp=%u evidence=separate-network-event\n",
           target.scene, target.x, target.y, armedTick, g_schedulerTick, pos);
    vm_autotest_note("mock_teleport_stone_deferred_enter scene=%s pos=(%u,%u) armed_tick=%u deliver_tick=%u response=30/1 evidence=JianghuOL:0x01012E4D/0x01039B8A/0x010396D6 crash-boundary:0x01018136/0x01046189/0x01005AF4\n",
                     target.scene, target.x, target.y, armedTick, g_schedulerTick);
    return pos;
}

static bool vm_net_mock_append_teleport_stone_map_confirm_object(u8 *out, u32 outCap,
                                                                  u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x10, 4, &objectStart))
        return false;
    /*
     * JianghuOL:0x010357E0 dispatches 16/4 to HandleItemUseConfirm
     * (0x010190A8). result=0 opens the normal confirmation dialog, and `value`
     * is both the displayed teleport-stone cost and the count later passed to
     * ConsumeInventoryItem(0x01018F66). A single map transfer costs one item
     * 800. If the client has no stone, ConsumeInventoryItem follows its normal
     * "not enough, purchase?" branch before emitting 16/2 and 16/3.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 0) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "value",
                                    VM_NET_MOCK_TELEPORT_STONE_COST))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_teleport_stone_map_transfer_response(const u8 *request, u32 requestLen,
                                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 curId = 0;
    u32 objId = 0;
    u32 smapRow = 0;
    u32 sceneCount = 0;
    const char *source = NULL;
    const char *posSource = NULL;
    const char *rowSource = NULL;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_teleport_stone_map_transfer_request(request, requestLen))
        return 0;

    if (!vm_net_mock_get_teleport_stone_map_target(request, requestLen, &target, &curId, &objId,
                                                   &source, &posSource, &smapRow, &sceneCount,
                                                   &rowSource))
    {
        printf("[error][network] mock_teleport_stone_map_unresolved curid=%u objid=%u smap_row=%u scene_count=%u row_source=%s scene_source=%s pos_source=%s action=no-fallback\n",
               curId, objId, smapRow, sceneCount,
               rowSource ? rowSource : "-",
               source ? source : "-",
               posSource ? posSource : "-");
        vm_autotest_note("mock_teleport_stone_map_unresolved curid=%u objid=%u smap_row=%u scene_count=%u row_source=%s scene_source=%s pos_source=%s action=no-fallback\n",
                         curId, objId, smapRow, sceneCount,
                         rowSource ? rowSource : "-",
                         source ? source : "-",
                         posSource ? posSource : "-");
        return 0;
    }
    /*
     * 16/4 is a preparation/confirmation response, not the scene-enter packet.
     * Its curid/objid values identify current/target wMap groups; the exact
     * selected child arrives later as 16/2.exitID.
     * Sending 30/1 here bypasses HandleItemUseConfirm and ConsumeInventoryItem,
     * so the world-map controller keeps its old current-world/current-child
     * indices even though the scene itself changes. The client's confirmation
     * callback emits 16/2 and 16/3; the existing subtype-3 path then returns
     * 30/1, retaining parserState=7 and the verified SCE-pixel landing.
     */
    if (!vm_net_mock_append_teleport_stone_map_confirm_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);

    g_vm_net_mock_teleport_stone_confirm_target = target;
    g_vm_net_mock_teleport_stone_confirm_target_valid = true;
    g_vm_net_mock_teleport_stone_deferred_enter_valid = false;
    g_vm_net_mock_teleport_stone_deferred_enter_tick = 0;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    printf("[info][network] mock_teleport_stone_map_confirm curid=%u objid=%u smap_row=%u scene_count=%u row_source=%s scene=%s scene_key=smap-exact scene_pos=(%u,%u) response=16/4-confirm value=%u scene_source=%s pos_source=%s download=%u resp=%u\n",
           curId, objId, smapRow, sceneCount, rowSource ? rowSource : "-",
           target.scene, target.x, target.y,
           (u32)VM_NET_MOCK_TELEPORT_STONE_COST,
           source ? source : "-", posSource ? posSource : "-",
           target.needsSceneDownload ? 1u : 0u, pos);
    vm_autotest_note("mock_teleport_stone_map_confirm curid=%u objid=%u smap_row=%u scene_count=%u row_source=%s scene=%s scene_key=smap-exact scene_pos=(%u,%u) response=16/4-confirm value=%u scene_source=%s pos_source=%s download=%u evidence=JianghuOL:0x010357E0/0x010190A8/0x01018F66/0x0103130A/0x0103581E negative=value0-wrong-cost+direct-30/1-stale-map-controller+extensionless-smap-miss\n",
                      curId, objId, smapRow, sceneCount, rowSource ? rowSource : "-",
                      target.scene, target.x, target.y,
                      (u32)VM_NET_MOCK_TELEPORT_STONE_COST,
                      source ? source : "-", posSource ? posSource : "-",
                      target.needsSceneDownload ? 1u : 0u);
    return pos;
}

static bool vm_net_mock_is_teleport_stone_post_enter_combo_request(const u8 *request, u32 requestLen)
{
    if (!g_vm_net_mock_last_scene_change_target_valid ||
        request == NULL || requestLen < 19 ||
        request[0] != 'W' || request[1] != 'T')
    {
        return false;
    }
    return vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
}

static u32 vm_net_mock_build_teleport_stone_post_enter_combo_response(const u8 *request, u32 requestLen,
                                                                      u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_teleport_stone_post_enter_combo_request(request, requestLen))
        return 0;

    target = g_vm_net_mock_last_scene_change_target;
    if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos,
                                                         target.scene, target.x, target.y))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                       target.scene,
                                                       "teleport-stone-post-enter"))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     g_vm_net_mock_last_scene_change_fb4_type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "teleport-stone-post-enter");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    printf("[info][network] mock_teleport_stone_post_enter scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           target.scene, target.x, target.y, objectCount, pos);
    vm_autotest_note("mock_teleport_stone_post_enter scene=%s pos=(%u,%u) objects=%u evidence=runtime:contains-27/11+12/1+7/42\n",
                     target.scene, target.x, target.y, objectCount);
    return pos;
}

static bool vm_net_mock_append_group_info_object(u8 *out, u32 outCap, u32 *pos, u32 leadId)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 templateId = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_ID", 105);
    u32 templateHp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_HP", 20);
    u32 templateMaxHp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MAX_HP", templateHp);
    u32 templateMp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MP", 20);
    u32 templateMaxMp = vm_net_mock_env_u32("CBE_GROUPINFO_TEMPLATE_MAX_MP", templateMp);
    u8 templateByte0 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE0", 1);
    u8 templateByte1 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE1", 0);
    u8 templateByte2 = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_BYTE2", 0);
    const char *templateName = vm_net_mock_env_str("CBE_GROUPINFO_TEMPLATE_NAME", "Monster");
    bool seedTemplate = vm_net_mock_env_u8("CBE_GROUPINFO_TEMPLATE_SEED", 0) != 0 &&
                        templateId != 0;
    u8 num = 0;
    u32 objectStart = 0;

    /* vm_net_mock_put_object_blob prepends a len16 value to the bytes returned
     * by the client's blob accessor.  stream_read_i32_be_tagged consumes that
     * len16 as the first row's tag header, so the first id inside the blob is
     * raw BE32.  All later u32 values remain explicitly tagged. */
    if (seedTemplate)
    {
        if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateId))
            return false;
        if (!vm_net_mock_seq_put_string(groupInfo, sizeof(groupInfo), &groupInfoLen, templateName))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte0))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte1))
            return false;
        if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, templateByte2))
            return false;
        if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateHp))
            return false;
        if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxHp))
            return false;
        if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMp))
            return false;
        if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxMp))
            return false;
        num = 1;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "num", num))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "leadid", leadId))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u8 vm_mock_service_team_member_job_code(const vm_mock_service_client_session *member)
{
    if (member == NULL || member->onlineJob < 1 || member->onlineJob > 3)
        return 0;
    return (u8)(member->onlineJob - 1);
}

static u8 vm_mock_service_team_member_sex_code(const vm_mock_service_client_session *member)
{
    return member != NULL && member->onlineSex <= 1 ? (u8)(member->onlineSex + 1) : 1;
}

/* Group-manager rows are keyed by their id at node+36.  New and migrated
 * roles use globally unique persistent ids.  Keep the synthetic-id fallback
 * for malformed legacy/imported state so a collision cannot corrupt the
 * observer's own portrait row. */
static u32 vm_mock_service_team_member_wire_id(
    const vm_mock_service_client_session *observer,
    const vm_mock_service_client_session *member)
{
    if (member == NULL || member->onlineRoleId == 0)
        return 0;
    if (observer != NULL && observer->clientId != member->clientId &&
        observer->onlineRoleId == member->onlineRoleId)
    {
        return 0x6A000000u | (member->clientId & 0x00FFFFFFu);
    }
    return member->onlineRoleId;
}

/* Full 5/3 and 5/10 roster rows are:
 * {u32 id, len16 string name, tagged-u8 sexGroup(1..2),
 *  tagged-u8 jobIndex(0..2), tagged-u8 online,
 *  tagged-u32 hp, mp, hpmax, mpmax}.  The object blob's own len16 prefix is
 * the first row id's tag header; later row ids carry an explicit 00/04 tag.
 * net_handle_group_info first calls the
 * non-advancing stream_peek_i16_be to retain the upcoming name length, then
 * stream_read_cstr_len16 consumes that same length and name.  There is no
 * reserved byte between id and name.  AddRoleToList passes the two visual
 * bytes to GetMapTileData as (jobIndex, sexGroup).  GetMapTileData and the
 * team HUD select the six zero-based role-resource slots with
 * 2 * jobIndex + sexGroup - 1, matching title/actorinfo/equipment visuals. */
static bool vm_net_mock_append_team_member_full_row(u8 *groupInfo, u32 groupInfoCap,
                                                    u32 *groupInfoLen,
                                                    const vm_mock_service_client_session *observer,
                                                    const vm_mock_service_client_session *member,
                                                    bool firstRowInBlob)
{
    bool encoded = false;
    u32 hp = 1;
    u32 hpMax = 1;
    u32 mp = 0;
    u32 mpMax = 0;
    const char *name = NULL;

    u32 wireId = vm_mock_service_team_member_wire_id(observer, member);

    if (groupInfo == NULL || groupInfoLen == NULL || member == NULL || wireId == 0)
        return false;
    name = member->onlineRoleName[0] ? member->onlineRoleName : "Player";
    hpMax = member->onlineHpMax ? member->onlineHpMax : 1;
    /* Zero is a valid current HP value (dead member), not an unset sentinel. */
    hp = member->onlineHp;
    if (hp > hpMax)
        hp = hpMax;
    mpMax = member->onlineMpMax;
    mp = member->onlineMp;
    if (mp > mpMax)
        mp = mpMax;

    encoded = (firstRowInBlob
                   ? vm_net_mock_put_be32(groupInfo, groupInfoCap, groupInfoLen, wireId)
                   : vm_net_mock_seq_put_u32(groupInfo, groupInfoCap, groupInfoLen, wireId)) &&
              vm_net_mock_seq_put_string(groupInfo, groupInfoCap, groupInfoLen, name) &&
              vm_net_mock_seq_put_u8(groupInfo, groupInfoCap, groupInfoLen,
                                     vm_mock_service_team_member_sex_code(member)) &&
              vm_net_mock_seq_put_u8(groupInfo, groupInfoCap, groupInfoLen,
                                     vm_mock_service_team_member_job_code(member)) &&
              vm_net_mock_seq_put_u8(groupInfo, groupInfoCap, groupInfoLen,
                                     member->roleOnline ? 1 : 0) &&
              vm_net_mock_seq_put_u32(groupInfo, groupInfoCap, groupInfoLen, hp) &&
              vm_net_mock_seq_put_u32(groupInfo, groupInfoCap, groupInfoLen, mp) &&
              vm_net_mock_seq_put_u32(groupInfo, groupInfoCap, groupInfoLen, hpMax) &&
              vm_net_mock_seq_put_u32(groupInfo, groupInfoCap, groupInfoLen, mpMax);
    if (encoded)
    {
        printf("[info][network] mock_team_member_row observer=%08x member=%08x/%u "
               "wire=%u hp=%u/%u mp=%u/%u wire_order=hp-mp-hpmax-mpmax\n",
               observer ? observer->clientId : 0,
               member->clientId,
               member->onlineRoleId,
               wireId,
               hp, hpMax, mp, mpMax);
    }
    return encoded;
}

static bool vm_net_mock_append_team_group_info_object(u8 *out, u32 outCap, u32 *pos,
                                                       const vm_mock_service_team *team,
                                                       const vm_mock_service_client_session *observer,
                                                       u8 subtype)
{
    u8 groupInfo[512];
    u32 groupInfoLen = 0;
    u32 objectStart = 0;
    u8 memberCount = 0;
    u32 leaderRoleId = 0;

    if (out == NULL || pos == NULL || team == NULL || !team->active ||
        (subtype != 3 && subtype != 10))
    {
        return false;
    }
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *session =
            vm_mock_service_find_client_session(team->memberClientIds[member]);
        if (session == NULL || session->onlineRoleId == 0)
            return false;
        if (member == 0)
            leaderRoleId = vm_mock_service_team_member_wire_id(observer, session);
        if (!vm_net_mock_append_team_member_full_row(groupInfo, sizeof(groupInfo),
                                                     &groupInfoLen, observer, session,
                                                     memberCount == 0))
        {
            return false;
        }
        ++memberCount;
    }
    if (memberCount == 0 || leaderRoleId == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, subtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "num", memberCount) ||
        !vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
    {
        return false;
    }
    if (subtype == 10 && !vm_net_mock_put_object_u32(out, outCap, pos, "leadid", leaderRoleId))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_team_groupinfo subtype=%u observer=%08x members=%u "
           "groupinfo_len=%u leader=%u "
           "layout=blob-prefix-raw-first-id-tagged-next-id-name-sexgroup-jobindex-online-hp-mp-hpmax-mpmax\n",
           subtype,
           observer ? observer->clientId : 0,
           memberCount,
           groupInfoLen,
           leaderRoleId);
    return true;
}

/* Subtype 5 is the native incremental member-join/update packet.  Unlike the
 * full 5/3 and 5/10 rows it has no online-state byte:
 * {raw-u32 id, len16 name, tagged-u8 sexGroup(1..2),
 *  tagged-u8 jobIndex(0..2), tagged-u32 hp, mp, hpmax, mpmax}. */
static bool vm_net_mock_append_team_member_join_object(
    u8 *out, u32 outCap, u32 *pos,
    const vm_mock_service_client_session *observer,
    const vm_mock_service_client_session *member)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 objectStart = 0;
    u32 wireId = vm_mock_service_team_member_wire_id(observer, member);
    u32 hpMax = 1;
    u32 hp = 1;
    u32 mpMax = 0;
    u32 mp = 0;
    u8 sex = 0;
    u8 job = 1;
    const char *name = NULL;

    if (out == NULL || pos == NULL || member == NULL || wireId == 0)
        return false;
    name = member->onlineRoleName[0] ? member->onlineRoleName : "Player";
    hpMax = member->onlineHpMax ? member->onlineHpMax : 1;
    hp = member->onlineHp;
    if (hp > hpMax)
        hp = hpMax;
    mpMax = member->onlineMpMax;
    mp = member->onlineMp;
    if (mp > mpMax)
        mp = mpMax;
    sex = vm_mock_service_team_member_sex_code(member);
    job = vm_mock_service_team_member_job_code(member);

    if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, wireId) ||
        !vm_net_mock_seq_put_string(groupInfo, sizeof(groupInfo), &groupInfoLen, name) ||
        !vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, sex) ||
        !vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, job) ||
        !vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, hp) ||
        !vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, mp) ||
        !vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, hpMax) ||
        !vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, mpMax) ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 5, &objectStart) ||
        !vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo,
                                     (u16)groupInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_team_member_join observer=%08x member=%08x/%u "
           "wire=%u sex_group=%u job_index=%u hp=%u/%u mp=%u/%u groupinfo_len=%u "
           "layout=blob-prefix-raw-id-name-sexgroup-jobindex-hp-mp-hpmax-mpmax\n",
           observer ? observer->clientId : 0,
           member->clientId,
           member->onlineRoleId,
           wireId,
           sex,
           job,
           hp,
           hpMax,
           mp,
           mpMax,
           groupInfoLen);
    return true;
}

/* net_handle_group_info subtype 11 consumes hsp as a raw first role id followed
 * by four tagged big-endian u32 values: HP, max HP, MP, max MP.  The object
 * blob len16 supplies the first id's tag header.  Unlike subtype 5, this path updates an
 * existing roster entry in place, which is what keeps party HUD bars current
 * while a member is fighting. */
static bool vm_net_mock_append_team_hsp_object(u8 *out, u32 outCap, u32 *pos,
                                               const vm_mock_service_client_session *observer,
                                               const vm_mock_service_client_session *member)
{
    u8 hsp[32];
    u32 hspLen = 0;
    u32 objectStart = 0;
    u32 hpMax = 1;
    u32 hp = 1;
    u32 mpMax = 0;
    u32 mp = 0;

    u32 wireId = vm_mock_service_team_member_wire_id(observer, member);

    if (out == NULL || pos == NULL || member == NULL || wireId == 0)
        return false;
    hpMax = member->onlineHpMax ? member->onlineHpMax : 1;
    hp = member->onlineHp;
    if (hp > hpMax)
        hp = hpMax;
    mpMax = member->onlineMpMax;
    mp = member->onlineMp;
    if (mp > mpMax)
        mp = mpMax;
    if (!vm_net_mock_put_be32(hsp, sizeof(hsp), &hspLen, wireId) ||
        !vm_net_mock_seq_put_u32(hsp, sizeof(hsp), &hspLen, hp) ||
        !vm_net_mock_seq_put_u32(hsp, sizeof(hsp), &hspLen, hpMax) ||
        !vm_net_mock_seq_put_u32(hsp, sizeof(hsp), &hspLen, mp) ||
        !vm_net_mock_seq_put_u32(hsp, sizeof(hsp), &hspLen, mpMax) ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 11, &objectStart) ||
        !vm_net_mock_put_object_blob(out, outCap, pos, "hsp", hsp, (u16)hspLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_team_hsp observer=%08x member=%08x/%u "
           "wire=%u hsp_len=%u layout=blob-prefix-raw-id-tagged-hsp\n",
           observer ? observer->clientId : 0,
           member->clientId,
           member->onlineRoleId,
           wireId,
           hspLen);
    return true;
}

static bool vm_net_mock_append_battle_template_prefill_object_ex(u8 *out, u32 outCap,
                                                                 u32 *pos, u32 templateId,
                                                                 const char *templateName,
                                                                 u8 rowByte34, u8 rowByte35,
                                                                 u32 templateHp,
                                                                 u32 templateMaxHp,
                                                                 u32 templateMp,
                                                                 u32 templateMaxMp)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 objectStart = 0;

    if (templateId == 0)
        return false;
    if (templateName == NULL)
        templateName = "";

    if (!vm_net_mock_put_be32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateId))
        return false;
    /* Subtype 5 has no online-state byte: id, name, sexGroup, jobIndex, HP/MP. */
    if (!vm_net_mock_seq_put_string(groupInfo, sizeof(groupInfo), &groupInfoLen, templateName))
        return false;
    if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, rowByte34))
        return false;
    if (!vm_net_mock_seq_put_u8(groupInfo, sizeof(groupInfo), &groupInfoLen, rowByte35))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateHp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxHp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMp))
        return false;
    if (!vm_net_mock_seq_put_u32(groupInfo, sizeof(groupInfo), &groupInfoLen, templateMaxMp))
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_enemy_template_prefill_object(u8 *out, u32 outCap,
                                                                    u32 *pos, u32 templateId)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(templateId);
    u32 templateHp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_HP", stats.hp);
    u32 templateMaxHp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MAX_HP", templateHp);
    u32 templateMp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MP", stats.mp);
    u32 templateMaxMp = vm_net_mock_env_u32("CBE_BATTLE_PREFILL_TEMPLATE_MAX_MP", templateMp);
    u8 rowByte34 = vm_net_mock_env_u8("CBE_BATTLE_PREFILL_TEMPLATE_BYTE34", 1);
    u8 rowByte35 = vm_net_mock_env_u8("CBE_BATTLE_PREFILL_TEMPLATE_BYTE35", 0);
    const char *templateName = vm_net_mock_env_str("CBE_BATTLE_PREFILL_TEMPLATE_NAME", "Monster");

    return vm_net_mock_append_battle_template_prefill_object_ex(out, outCap, pos,
                                                                templateId,
                                                                templateName,
                                                                rowByte34,
                                                                rowByte35,
                                                                templateHp,
                                                                templateMaxHp,
                                                                templateMp,
                                                                templateMaxMp);
}

static bool vm_net_mock_append_type1_object(u8 *out, u32 outCap, u32 *pos, u8 npcNum)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x1a, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", npcNum))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name",
                                       vm_net_mock_role_spouse_name(role)))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "money", money))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_is_role_action23_request(const u8 *request, u32 requestLen, u32 *idOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 id = 0;

    if (idOut)
        *idOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x0a && object.subtype == 23)
        {
            (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &id);
            if (idOut)
                *idOut = id;
            return true;
        }
    }
    return false;
}

static u32 vm_net_mock_build_role_action23_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 id = 0;

    if (outCap < pos || !vm_net_mock_is_role_action23_request(request, requestLen, &id))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x0a, 23, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "id", id))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_role_action23 id=%u resp=%u\n", id, pos);
    vm_autotest_note("mock_role_action23 id=%u response=10/23 result=1 evidence=xxjh:0x103C830 field=id\n",
                     id);
    return pos;
}

static bool vm_net_mock_is_role_designation23_request(const u8 *request, u32 requestLen,
                                                      u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 subtype = 0;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x17 ||
        (object.subtype != 1 && object.subtype != 3))
        return false;
    subtype = object.subtype;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (subtypeOut)
        *subtypeOut = subtype;
    return offset == requestLen;
}

static bool vm_net_mock_parse_role_designation23_request_fields(const u8 *request, u32 requestLen,
                                                                u8 *indexOut,
                                                                u8 *typeOut,
                                                                u8 *resultOut,
                                                                u8 *pageOut,
                                                                u32 *idOut,
                                                                char *payloadHex,
                                                                u32 payloadHexCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    static const char hex[] = "0123456789ABCDEF";

    if (indexOut)
        *indexOut = 0xff;
    if (typeOut)
        *typeOut = 0xff;
    if (resultOut)
        *resultOut = 0xff;
    if (pageOut)
        *pageOut = 0xff;
    if (idOut)
        *idOut = 0;
    if (payloadHex && payloadHexCap > 0)
        payloadHex[0] = 0;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x17 ||
        (object.subtype != 1 && object.subtype != 3))
        return false;
    if (payloadHex && payloadHexCap > 0)
    {
        u32 hexPos = 0;
        u32 maxBytes = object.payloadLen < 16 ? object.payloadLen : 16;
        for (u32 i = 0; i < maxBytes && hexPos + 3 < payloadHexCap; ++i)
        {
            payloadHex[hexPos++] = hex[object.payload[i] >> 4];
            payloadHex[hexPos++] = hex[object.payload[i] & 0x0f];
            if (i + 1 < maxBytes && hexPos + 1 < payloadHexCap)
                payloadHex[hexPos++] = ' ';
        }
        payloadHex[hexPos] = 0;
    }
    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "index", indexOut);
    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", typeOut);
    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "result", resultOut);
    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "page", pageOut);
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", idOut);
    return true;
}

static bool vm_net_mock_append_role_designation_list_row(u8 *out, u32 outCap, u32 *pos,
                                                         const vm_net_mock_designation_entry *entry)
{
    if (entry == NULL)
        return false;

    if (!vm_net_mock_seq_put_u8(out, outCap, pos, entry->id))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, pos, entry->fieldB))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, pos, entry->name))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, pos, entry->description))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, pos, entry->overheadResource))
        return false;
    return true;
}

static bool vm_net_mock_build_role_designation_update_blob(u8 *out, u32 outCap, u32 *blobLenOut,
                                                           u32 roleId,
                                                           const vm_net_mock_designation_entry *entry)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || entry == NULL || roleId == 0)
        return false;

    /*
     * net_handle_designationinfo_update(0x01010DB6) consumes each row as:
     *   tagged u32 actorId,
     *   tagged i8 fieldA,
     *   tagged i8 fieldB,
     *   len16 shortTitle,
     *   len16 overheadResource.
     * The resource slot is a named overhead badge/icon, so it must be a real
     * local resource name rather than the human-readable GBK title.
     */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, entry->id))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, entry->fieldB))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, entry->name))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, entry->overheadResource))
        return false;

    if (blobLenOut)
        *blobLenOut = pos;
    return true;
}

static bool vm_net_mock_append_role_designation_update23_object(u8 *out, u32 outCap, u32 *pos,
                                                                u32 roleId,
                                                                const vm_net_mock_designation_entry *entry,
                                                                u32 *designationInfoLenOut)
{
    u32 objectStart = 0;
    u8 designationInfo[64];
    u32 designationInfoLen = 0;

    if (designationInfoLenOut)
        *designationInfoLenOut = 0;
    if (!vm_net_mock_build_role_designation_update_blob(designationInfo,
                                                        sizeof(designationInfo),
                                                        &designationInfoLen,
                                                        roleId,
                                                        entry))
    {
        return false;
    }
    if (designationInfoLen == 0 || designationInfoLen > 0xffffu)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x17, 2, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "count", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "designationinfo",
                                      designationInfo, (u16)designationInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (designationInfoLenOut)
        *designationInfoLenOut = designationInfoLen;
    return true;
}

static u32 vm_net_mock_build_role_designation23_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 designationInfo[768];
    u32 designationInfoLen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    const vm_net_mock_designation_entry *activeDesignation = vm_net_mock_role_designation(role);
    u32 roleMoney = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u8 requestIndex = 0xff;
    u8 requestType = 0xff;
    u8 requestResult = 0xff;
    u8 requestPage = 0xff;
    u8 requestSubtype = 0;
    u8 unlockedCount = 0;
    u32 requestId = 0;
    char titleUtf8[64];
    char selectedTitleUtf8[64];
    char requestPayloadHex[64];

    if (outCap < pos || !vm_net_mock_is_role_designation23_request(request, requestLen, &requestSubtype))
        return 0;
    requestPayloadHex[0] = 0;
    (void)vm_net_mock_parse_role_designation23_request_fields(request,
                                                              requestLen,
                                                              &requestIndex,
                                                              &requestType,
                                                              &requestResult,
                                                              &requestPage,
                                                              &requestId,
                                                              requestPayloadHex,
                                                              sizeof(requestPayloadHex));
    if (role != NULL && role->designationId != activeDesignation->id)
    {
        role->designationId = activeDesignation->id;
        vm_net_mock_role_db_save("role-designation-condition-refresh");
    }
    if (requestSubtype == 3)
    {
        const vm_net_mock_designation_entry *selectedDesignation =
            vm_net_mock_designation_by_id(requestType == 0xff ? activeDesignation->id : requestType);
        u32 updateInfoLen = 0;
        if (!vm_net_mock_designation_is_unlocked(role, selectedDesignation))
        {
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x17, 3, &objectStart))
                return 0;
            if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 0))
                return 0;
            vm_net_mock_finish_wt_object(out, objectStart, pos);
            vm_net_mock_finish_wt_packet(out, pos, 1);
            vm_net_mock_gbk_label_to_utf8(selectedDesignation->name, selectedTitleUtf8, sizeof(selectedTitleUtf8));
            printf("[info][network] mock_role_designation23_select role=%u result=0 locked=1 title=%s designation=%u money=%u min_money=%u req_index=%u req_type=%u req_payload=%s resp=%u\n",
                   roleId,
                   selectedTitleUtf8,
                   selectedDesignation->id,
                   roleMoney,
                   selectedDesignation->minMoney,
                   requestIndex,
                   requestType,
                   requestPayloadHex,
                   pos);
            vm_autotest_note("mock_role_designation23_select role=%u result=0 locked=1 designation=%u money=%u min_money=%u response=23/3 evidence=JianghuOL.CBE:0x0102A93E\n",
                             roleId,
                             selectedDesignation->id,
                             roleMoney,
                             selectedDesignation->minMoney);
            return pos;
        }
        if (role != NULL)
        {
            role->designationId = selectedDesignation->id;
            vm_net_mock_role_db_save("role-designation-select");
            activeDesignation = selectedDesignation;
        }
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x17, 3, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        if (!vm_net_mock_append_role_designation_update23_object(out,
                                                                 outCap,
                                                                 &pos,
                                                                 roleId,
                                                                 selectedDesignation,
                                                                 &updateInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, 2);
        vm_net_mock_gbk_label_to_utf8(selectedDesignation->name, selectedTitleUtf8, sizeof(selectedTitleUtf8));
        printf("[info][network] mock_role_designation23_select role=%u result=1 title=%s designation=%u field_b=%u money=%u min_money=%u overhead=%s update=23/2 designationinfo_len=%u req_index=%u req_type=%u req_payload=%s resp=%u\n",
               roleId,
               selectedTitleUtf8,
               selectedDesignation->id,
               selectedDesignation->fieldB,
               roleMoney,
               selectedDesignation->minMoney,
               selectedDesignation->overheadResource[0] ? selectedDesignation->overheadResource : "-",
               updateInfoLen,
               requestIndex,
               requestType,
               requestPayloadHex,
               pos);
        vm_autotest_note("mock_role_designation23_select role=%u result=1 designation=%u response=23/3+23/2 evidence=JianghuOL.CBE:0x0102A93E select,0x01010DB6 scene-node-update\n",
                         roleId,
                         selectedDesignation->id);
        return pos;
    }

    for (u32 i = 0; i < vm_net_mock_designation_entry_count(); ++i)
    {
        const vm_net_mock_designation_entry *entry = &g_vm_net_mock_designation_entries[i];
        if (!vm_net_mock_designation_is_unlocked(role, entry))
            continue;
        if (!vm_net_mock_append_role_designation_list_row(designationInfo,
                                                          sizeof(designationInfo),
                                                          &designationInfoLen,
                                                          entry))
        {
            return 0;
        }
        ++unlockedCount;
    }
    if (unlockedCount == 0 || designationInfoLen == 0 || designationInfoLen > 0xffffu)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x17, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "equiptype", activeDesignation->id))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "count", unlockedCount))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "designationinfo",
                                      designationInfo, (u16)designationInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_gbk_label_to_utf8(activeDesignation->name, titleUtf8, sizeof(titleUtf8));
    printf("[info][network] mock_role_designation23_list role=%u count=%u catalog=%u active=%u title=%s money=%u overhead=%s req_index=%u req_type=%u req_result=%u req_page=%u req_id=%u req_payload=%s designationinfo_len=%u resp=%u\n",
           roleId,
           unlockedCount,
           vm_net_mock_designation_entry_count(),
           activeDesignation->id,
           titleUtf8,
           roleMoney,
           activeDesignation->overheadResource[0] ? activeDesignation->overheadResource : "-",
           requestIndex,
           requestType,
           requestResult,
           requestPage,
           requestId,
           requestPayloadHex,
           designationInfoLen,
           pos);
    vm_autotest_note("mock_role_designation23_list role=%u count=%u active=%u designationinfo_len=%u response=23/1 evidence=JianghuOL.CBE:0x0102A93E runtime=wt23/1-index\n",
                     roleId,
                     unlockedCount,
                     activeDesignation->id,
                     designationInfoLen);
    return pos;
}

