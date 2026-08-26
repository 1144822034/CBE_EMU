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
        if (declaredLen != 0 && declaredLen <= rawLen - 4 && raw[4] == 2)
        {
            decodedLen = vm_net_mock_decode_lzss_resource_stream(raw + 4,
                                                                  declaredLen,
                                                                  out,
                                                                  outCap);
            if (decodedLen >= 16 && memcmp(out, "XSE0", 4) == 0)
                return decodedLen;
            return 0;
        }
        if (declaredLen > 1 && declaredLen <= rawLen - 4 && raw[4] == 1)
        {
            decodedLen = declaredLen - 1u;
            if (decodedLen > outCap || decodedLen < 16 ||
                memcmp(raw + 5, "XSE0", 4) != 0)
            {
                return 0;
            }
            memcpy(out, raw + 5, decodedLen);
            return decodedLen;
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

/* The effect Actor tail is not a normal numbered string field.  Direct raw
 * SCE2 samples (01桃花岛_01, 01桃花岛_02 and 06野猪林_01) all contain exactly
 * `u16 3, u16 3, u8 length, bytes` after field17.  Keep this separate from
 * vm_net_mock_read_sce_string_field(): treating the second 3 as a normal
 * field ID, or omitting it, makes the client reject the static scene record. */
static bool vm_net_mock_read_sce_effect_actor_tail(
    const u8 *data, u32 len, u32 *pos, char *out, size_t outCap)
{
    if (data == NULL || pos == NULL || out == NULL || outCap == 0 ||
        *pos + 5 > len ||
        vm_net_mock_read_le16_at(data, *pos) != 3 ||
        vm_net_mock_read_le16_at(data, *pos + 2) != 3)
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

/* This is a dynamic-NPC configuration rule, not an Actor file-format rule.
 * n_girl.actor is structurally valid, but cannot be sent as a dynamic 27/11
 * Actor during the current CBE scene lifecycle.  Existing rows are migrated
 * to n_woman1.actor; an unmigrated row remains visible in admin as disabled
 * instead of reaching the client. */
static bool vm_net_mock_dynamic_npc_actor_resource_is_supported(
    const char *actorResource)
{
    return actorResource != NULL &&
           strcmp(actorResource, "n_girl.actor") != 0;
}

/* SCE resources are source data rather than dynamic-NPC settings.  Validate
 * their Actor against the server authority, but do not rewrite their name. */
static bool vm_net_mock_scene_npc_validate_actor_resource(
    vm_net_mock_scene_npcinfo_seed *seed, const char *source)
{
    if (seed == NULL || seed->actorResource[0] == 0)
        return false;
    if (!vm_net_mock_open_server_data_resource(seed->actorResource, ".actor", NULL,
                                               NULL, 0))
    {
        printf("[error][network] mock_scene_npc_actor_resource_missing source=%s actor=%u npc=%s resource=%s action=skip-row\n",
               source ? source : "-", seed->actorId, seed->displayName,
               seed->actorResource);
        return false;
    }
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
    seed.sceneEntityKind = vm_net_mock_read_le16_at(data, pos);
    /* The first SCE word is the scene entity/resource kind.  It describes
     * how the map instantiates the actor and must never be interpreted as the
     * server-side merchant/service enum. */
    seed.kind = VM_NET_MOCK_NPC_KIND_NORMAL;
    seed.nativeSceneActor = true;
    if (seed.sceneEntityKind > 32)
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

    /* Validate against the clean server download source rather than the
     * writable client cache. */
    if (!vm_net_mock_scene_npc_validate_actor_resource(&seed, "sce-catalog"))
        return false;
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
    u16 visualHint;
    char displayName[32];
    char actorResource[64];
    char effectResource[64];
} vm_net_mock_sce_combat_spawn;

/* Replay the prefix reads performed by
 * LoadSceneDataFromStream(0x01006204). The old scanner started halfway
 * through the first map-template row and happened to see its final two
 * shorts as `1,1`. That only located the callback entity list when the SCE
 * had exactly one map template.
 *
 * SCE2 prefix:
 *   magic, width, height, map_template_count,
 *   map_template_count { string, u16, u16, u16, u16 },
 *   actor_placement_count,
 *   if nonzero {
 *     layer_index_count, layer_index_count { u16 },
 *     actor_template_count, actor_template_count { string },
 *     actor_placement_count { template_index, x, y, flags }
 *   },
 *   u16 scene_tail,
 *   callback payload (the counted entity list).
 *
 * Actor placements become live scene nodes before the callback entities and
 * therefore remain part of the battle scene-node ordinal.
 */
static bool vm_net_mock_parse_sce2_client_prefix(
    const u8 *data, u32 len, u32 *placementCountOut,
    u32 *entityCountOffsetOut)
{
    u32 base = 0;
    u32 pos = 0;
    u16 mapTemplateCount = 0;
    u16 placementCount = 0;
    u16 layerIndexCount = 0;
    u16 actorTemplateCount = 0;

    if (placementCountOut != NULL)
        *placementCountOut = 0;
    if (entityCountOffsetOut != NULL)
        *entityCountOffsetOut = 0;
    if (data == NULL || len < 12u)
        return false;

    for (base = 0; base + 10u <= len && base < 32u; ++base)
    {
        if (memcmp(data + base, "SCE2", 4) == 0)
            break;
    }
    if (base >= 32u || base + 10u > len)
        return false;

    pos = base + 4u;
    if (pos + 6u > len)
        return false;
    /* Width and height are consumed before the map-template count. */
    pos += 4u;
    mapTemplateCount = vm_net_mock_read_le16_at(data, pos);
    pos += 2u;
    if ((u32)mapTemplateCount > (len - pos) / 9u)
        return false;

    for (u16 mapIndex = 0; mapIndex < mapTemplateCount; ++mapIndex)
    {
        u8 nameLen = 0;

        if (pos >= len)
            return false;
        nameLen = data[pos++];
        if (pos + (u32)nameLen + 8u > len)
            return false;
        pos += (u32)nameLen + 8u;
    }

    if (pos + 2u > len)
        return false;
    placementCount = vm_net_mock_read_le16_at(data, pos);
    pos += 2u;

    if (placementCount != 0u)
    {
        if (pos + 2u > len)
            return false;
        layerIndexCount = vm_net_mock_read_le16_at(data, pos);
        pos += 2u;
        if ((u32)layerIndexCount > (len - pos) / 2u)
        {
            return false;
        }
        pos += 2u * (u32)layerIndexCount;

        if (pos + 2u > len)
            return false;
        actorTemplateCount = vm_net_mock_read_le16_at(data, pos);
        pos += 2u;
        if (actorTemplateCount == 0u ||
            (u32)actorTemplateCount > len - pos)
            return false;
        for (u16 templateIndex = 0; templateIndex < actorTemplateCount;
             ++templateIndex)
        {
            u8 nameLen = 0;

            if (pos >= len)
                return false;
            nameLen = data[pos++];
            if (pos + (u32)nameLen > len)
                return false;
            pos += nameLen;
        }

        if ((u32)placementCount > (len - pos) / 8u)
            return false;
        for (u16 placementIndex = 0; placementIndex < placementCount;
             ++placementIndex)
        {
            if (vm_net_mock_read_le16_at(data, pos) >= actorTemplateCount)
                return false;
            pos += 8u;
        }
    }

    /* a1+1556 is read immediately before the callback. */
    if (pos + 2u > len)
        return false;
    pos += 2u;
    if (placementCountOut != NULL)
        *placementCountOut = placementCount;
    if (entityCountOffsetOut != NULL)
        *entityCountOffsetOut = pos;
    return true;
}

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
     *   string field 17 = actor resource,
     *   nested-string field 18 = effect actor resource.
     *
     * Field 18 is not optional in the native kind-3 record, and it is not
     * encoded like fields 15 and 17.  Shipped resources contain the exact
     * envelope `u16 3, u16 3, u8 len, bytes`: the first `3` is the outer
     * entity marker and the second is the effect-tail marker.  The next byte
     * is already the string length, not a u16 field ID.  Earlier generators
     * omitted the second marker or wrote an invented u16 field ID; both byte
     * streams passed our scanner but LoadSceneDataFromStream did not turn
     * them into a live type-2 node.  Do not relax this parser: deployment
     * validation must use the same grammar the client consumes.
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
        !vm_net_mock_str_ends_with(spawn.actorResource, ".actor") ||
        !vm_net_mock_read_sce_effect_actor_tail(
            data, len, &pos, spawn.effectResource,
            sizeof(spawn.effectResource)) ||
        !vm_net_mock_str_ends_with(spawn.effectResource, ".actor"))
    {
        return false;
    }
    spawn.visualHint = value;

    *spawnOut = spawn;
    if (endOut)
        *endOut = pos;
    return true;
}

static bool vm_net_mock_scene_battle_monster_counted_spawn_at(
    const u8 *payload, u32 payloadLen, u32 wantedCombatOrdinal,
    vm_net_mock_sce_combat_spawn *spawnOut, u32 *nodeOrdinalOut);

static bool vm_net_mock_select_sce_combat_spawn(const char *scene, u32 actorId,
                                                 u32 *indexOut,
                                                 u32 *posxOut,
                                                 u32 *posyOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u8 data[8192];
    u32 len = 0;
    u32 propNodeCount = 0;
    u32 sceneNodeIndex = 0;

    if (scene == NULL || scene[0] == 0 || actorId == 0)
        return false;

    /* The static SCE callback completes before the later 27/11 NPC packet.
     * Its counted entity list therefore owns the static node ordinal. */
    if (session == NULL || !session->sceneVisibleReady ||
        session->sceneVisiblePending ||
        !vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene,
                                             scene))
    {
        printf("[error][network] mock_scene_monster_target scene=%s actor=%u "
               "action=reject-unobserved-scene visible_scene=%s "
               "evidence=SCE2-counted-entity-list+mmBattle:0x66CC\n",
               scene, actorId,
               session != NULL ? session->sceneVisibleScene : "-");
        return false;
    }

    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    if (len != 0 &&
        vm_net_mock_parse_sce2_client_prefix(data, len, &propNodeCount,
                                              NULL))
    {
        for (u32 combatOrdinal = 0; combatOrdinal < 256u; ++combatOrdinal)
        {
            vm_net_mock_sce_combat_spawn spawn;
            u32 nodeOrdinal = 0;

            if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                    data, len, combatOrdinal, &spawn, &nodeOrdinal))
                break;
            if (spawn.actorId == actorId)
            {
                sceneNodeIndex = propNodeCount + nodeOrdinal;
                /* The main CBE scene table has rows 0..24.  Sending any
                 * other index to mmBattle 0x66CC makes it copy an invalid
                 * scene node into a battle fighter. */
                if (sceneNodeIndex >= 25)
                {
                    printf("[error][network] mock_scene_monster_target scene=%s actor=%u "
                           "runtime_index=%u action=reject-out-of-range "
                           "evidence=mmBattle:0x66CC scene-node-table[25]\n",
                           scene, actorId, sceneNodeIndex);
                    return false;
                }
                if (indexOut)
                    *indexOut = sceneNodeIndex;
                if (posxOut)
                    *posxOut = spawn.x;
                if (posyOut)
                    *posyOut = spawn.y;
                printf("[info][network] mock_scene_monster_target scene=%s resource_scene=%s actor=%u "
                       "runtime_index=%u prop_nodes=%u entity_node_ordinal=%u combat_ordinal=%u pos=(%u,%u) "
                       "name=%s actor_resource=%s source=SCE2-counted-entity-list "
                       "evidence=ParseActorFullInfoBlob:0x0100F094+mmBattle:0x66CC\n",
                       scene, scene, actorId, sceneNodeIndex, propNodeCount,
                       nodeOrdinal, combatOrdinal, spawn.x, spawn.y,
                       spawn.displayName, spawn.actorResource);
                return true;
            }
        }
    }
    return false;
}

typedef struct
{
    char displayName[32];
    char firstScene[64];
} vm_net_mock_monster_resource_label;

#ifndef _WIN32
#include <dirent.h>
#endif

static vm_net_mock_monster_resource_label
    g_vm_net_mock_monster_resource_labels[VM_NET_MOCK_MONSTER_CATALOG_MAX];

/* These targets are present as structured task.dsh kill requirements but do
 * not occur in automonster.dsh.  If the normal SCE2 scan cannot identify
 * them, retain their task names and provenance instead of showing anonymous
 * editable monsters. */
typedef struct
{
    u16 enemyId;
    const char *displayName;
    const char *source;
} vm_net_mock_monster_task_only_label;

static const vm_net_mock_monster_task_only_label
    g_vm_net_mock_monster_task_only_labels[] = {
        { 15, "\xB1\xCC\xD6\xF1\xBE\xDE\xC9\xDF", "task.dsh#709" }, /* 碧竹巨蛇 */
        { 23, "\xDA\xA4\xBB\xF0\xF7\xE8\xF7\xEB", "task.dsh#5004" }, /* 冥火麒麟 */
        { 38, "\xBB\xF0\xB7\xEF\xBB\xCB", "task.dsh#112" }, /* 火凤凰 */
        { 44, "\xBB\xF0\xF2\xF3\xC9\xDF", "task.dsh#407" }, /* 火蝮蛇 */
        { 63, "\xC7\xE0\xC1\xFA\xCD\xF5", "task.dsh#5007" }, /* 青龙王 */
        { 65, "\xB6\xAB\xB7\xBD\xB2\xBB\xB0\xDC", "task.dsh#5009" }, /* 东方不败 */
        {300, "\xC1\xB6\xD3\xFC\xC4\xA7\xCD\xB7", "task.dsh#5010" }  /* 炼狱魔头 */
    };

enum { VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX = 512 };

typedef struct
{
    char name[64];
} vm_net_mock_monster_catalog_scene_file;

static int vm_net_mock_monster_catalog_scene_file_compare(const void *left,
                                                          const void *right)
{
    const vm_net_mock_monster_catalog_scene_file *a = left;
    const vm_net_mock_monster_catalog_scene_file *b = right;
    return strcmp(a->name, b->name);
}

static u32 vm_net_mock_monster_catalog_add_scene_battle_drafts(void);

/* This enumerates the same server resource root later used by
 * vm_net_mock_load_scene_resource().  The directory is only an index: every
 * selected name is subsequently opened through the normal safe game-resource
 * path, so host paths never become a scene identity or a client payload. */
static u32 vm_net_mock_monster_catalog_collect_scene_files(
    vm_net_mock_monster_catalog_scene_file *files, u32 fileCap)
{
    u32 count = 0;

    if (files == NULL || fileCap == 0)
        return 0;
    memset(files, 0, sizeof(*files) * fileCap);
#ifdef _WIN32
    {
        char pattern[1200];
        const char *directories[] = {
            vm_net_mock_resource_dir()[0] ? vm_net_mock_resource_dir() : NULL,
            "../web/fs/JHOnlineData", "web/fs/JHOnlineData"
        };

        for (u32 directoryIndex = 0;
             directoryIndex < sizeof(directories) / sizeof(directories[0]);
             ++directoryIndex)
        {
            WIN32_FIND_DATAA found;
            HANDLE search = INVALID_HANDLE_VALUE;
            size_t dirLen = 0;

            if (directories[directoryIndex] == NULL)
                continue;
            dirLen = strlen(directories[directoryIndex]);
            if (snprintf(pattern, sizeof(pattern), "%s%s*.sce",
                         directories[directoryIndex],
                         (dirLen != 0 &&
                          (directories[directoryIndex][dirLen - 1] == '/' ||
                           directories[directoryIndex][dirLen - 1] == '\\'))
                             ? "" : "/") >= (int)sizeof(pattern))
            {
                continue;
            }
            search = FindFirstFileA(pattern, &found);
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
                ++count;
            } while (count < fileCap && FindNextFileA(search, &found));
            FindClose(search);
            break;
        }
    }
#else
    {
        const char *directories[] = {
            vm_net_mock_resource_dir()[0] ? vm_net_mock_resource_dir() : NULL,
            "../web/fs/JHOnlineData", "web/fs/JHOnlineData"
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
                char gameName[sizeof(files[0].name)];
                size_t nameLen = strlen(entry->d_name);

                if (nameLen == 0 || !vm_net_mock_str_ends_with(entry->d_name,
                                                                ".sce"))
                {
                    continue;
                }
                memset(gameName, 0, sizeof(gameName));
#ifdef CBE_HOST_UTF8_PATHS
                utf8_to_gbk((u8 *)entry->d_name, (u8 *)gameName,
                            sizeof(gameName));
#else
                if (nameLen >= sizeof(gameName))
                    continue;
                snprintf(gameName, sizeof(gameName), "%s", entry->d_name);
#endif
                if (gameName[0] == 0 ||
                    strlen(gameName) >= sizeof(files[0].name))
                {
                    continue;
                }
                snprintf(files[count].name, sizeof(files[count].name), "%s",
                         gameName);
                ++count;
            }
            closedir(directory);
            break;
        }
    }
#endif
    if (count > 1)
    {
        qsort(files, count, sizeof(files[0]),
              vm_net_mock_monster_catalog_scene_file_compare);
    }
    return count;
}

static void vm_net_mock_monster_catalog_sort(void)
{
    /* The base table is already ID ordered.  SCE-only records are appended
     * while scanning, then inserted here with their parallel label so page
     * order is stable and MySQL overrides keep one canonical index. */
    for (u32 i = 1; i < g_vm_net_mock_monster_catalog_count; ++i)
    {
        vm_net_mock_monster_entry entry =
            g_vm_net_mock_monster_catalog_entries[i];
        vm_net_mock_monster_resource_label label =
            g_vm_net_mock_monster_resource_labels[i];
        bool draftOnly = g_vm_net_mock_monster_catalog_draft_only[i];
        u32 j = i;

        while (j > 0 &&
               g_vm_net_mock_monster_catalog_entries[j - 1].enemyId >
                   entry.enemyId)
        {
            g_vm_net_mock_monster_catalog_entries[j] =
                g_vm_net_mock_monster_catalog_entries[j - 1];
            g_vm_net_mock_monster_resource_labels[j] =
                g_vm_net_mock_monster_resource_labels[j - 1];
            g_vm_net_mock_monster_catalog_draft_only[j] =
                g_vm_net_mock_monster_catalog_draft_only[j - 1];
            --j;
        }
        g_vm_net_mock_monster_catalog_entries[j] = entry;
        g_vm_net_mock_monster_resource_labels[j] = label;
        g_vm_net_mock_monster_catalog_draft_only[j] = draftOnly;
    }
}

static void vm_net_mock_monster_catalog_ensure_loaded(void)
{
    vm_net_mock_monster_catalog_scene_file
        scenes[VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX];
    u32 sceneCount = 0;
    u32 baseCount = 0;
    u32 sceneAdded = 0;
    u32 parsedSpawnCount = 0;
    u32 rejectedSceneEntries = 0;
    u32 draftAdded = 0;

    if (g_vm_net_mock_monster_catalog_loaded ||
        g_vm_net_mock_monster_catalog_loading)
    {
        return;
    }
    g_vm_net_mock_monster_catalog_loading = true;
    vm_net_mock_monster_catalog_seed_base_entries();
    baseCount = g_vm_net_mock_monster_catalog_count;
    memset(g_vm_net_mock_monster_resource_labels, 0,
           sizeof(g_vm_net_mock_monster_resource_labels));
    sceneCount = vm_net_mock_monster_catalog_collect_scene_files(
        scenes, VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX);

    for (u32 sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex)
    {
        u8 data[8192];
        u32 len = vm_net_mock_load_scene_resource(scenes[sceneIndex].name,
                                                   data, sizeof(data));
        u32 start = vm_net_mock_scene_payload_start(data, len);

        if (len == 0 || start == 0)
            continue;
        for (u32 combatOrdinal = 0; combatOrdinal < 256u; ++combatOrdinal)
        {
            vm_net_mock_sce_combat_spawn spawn;
            int monsterIndex = -1;
            u32 beforeCount = g_vm_net_mock_monster_catalog_count;

            if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                    data, len, combatOrdinal, &spawn, NULL))
                break;
            ++parsedSpawnCount;
            monsterIndex = vm_net_mock_monster_catalog_add_scene_entry(
                spawn.actorId);
            if (monsterIndex >= 0)
            {
                if (g_vm_net_mock_monster_catalog_count > beforeCount)
                    ++sceneAdded;
                if (g_vm_net_mock_monster_resource_labels[monsterIndex]
                        .displayName[0] == 0)
                {
                    snprintf(g_vm_net_mock_monster_resource_labels[monsterIndex]
                                 .displayName,
                             sizeof(g_vm_net_mock_monster_resource_labels[
                                        monsterIndex].displayName),
                             "%s", spawn.displayName);
                }
                if (g_vm_net_mock_monster_resource_labels[monsterIndex]
                        .firstScene[0] == 0)
                {
                    snprintf(g_vm_net_mock_monster_resource_labels[monsterIndex]
                                 .firstScene,
                             sizeof(g_vm_net_mock_monster_resource_labels[
                                        monsterIndex].firstScene),
                             "%s", scenes[sceneIndex].name);
                }
            }
            else
            {
                /* Do not turn a capacity/configuration defect into an
                 * anonymous missing boss in the admin UI.  The catalog is
                 * intentionally finite only because all service persistence
                 * arrays share this index; report any rejected native ID. */
                ++rejectedSceneEntries;
            }
        }
    }

    /* Task-only targets retain their exact task name when no native combat
     * placement supplied one.  This is a fallback label only; it never
     * creates an arbitrary battle ID. */
    for (u32 i = 0;
         i < sizeof(g_vm_net_mock_monster_task_only_labels) /
                 sizeof(g_vm_net_mock_monster_task_only_labels[0]);
         ++i)
    {
        const vm_net_mock_monster_task_only_label *label =
            &g_vm_net_mock_monster_task_only_labels[i];
        int monsterIndex = vm_net_mock_monster_catalog_index_loaded(
            label->enemyId);

        if (monsterIndex < 0)
            continue;
        if (g_vm_net_mock_monster_resource_labels[monsterIndex].displayName[0] == 0)
        {
            snprintf(g_vm_net_mock_monster_resource_labels[monsterIndex].displayName,
                     sizeof(g_vm_net_mock_monster_resource_labels[monsterIndex]
                                .displayName),
                     "%s", label->displayName);
        }
        if (g_vm_net_mock_monster_resource_labels[monsterIndex].firstScene[0] == 0)
        {
            snprintf(g_vm_net_mock_monster_resource_labels[monsterIndex].firstScene,
                     sizeof(g_vm_net_mock_monster_resource_labels[monsterIndex]
                                .firstScene),
                     "%s", label->source);
        }
    }

    /* Drafts are visible to the administrator immediately so their combat
     * profile and drops can be configured before publishing.  The parallel
     * draft-only bit keeps this edit-time identity out of live encounter
     * validation until the normal SCE deployment has produced its kind-3
     * record. */
    draftAdded = vm_net_mock_monster_catalog_add_scene_battle_drafts();
    vm_net_mock_monster_catalog_sort();
    g_vm_net_mock_monster_catalog_loaded = true;
    g_vm_net_mock_monster_catalog_loading = false;
    printf("[info][network] mock_monster_catalog base=%u scenes=%u spawns=%u "
           "scene_added=%u drafts_added=%u rejected=%u total=%u "
           "source=SCE2-kind3+task-kill-labels+scene-battle-drafts\n",
           baseCount, sceneCount, parsedSpawnCount, sceneAdded,
           draftAdded, rejectedSceneEntries, g_vm_net_mock_monster_catalog_count);
}

static void vm_net_mock_monster_resource_labels_load(void)
{
    vm_net_mock_monster_catalog_ensure_loaded();
}

static u32 vm_net_mock_monster_admin_list(
    vm_net_mock_monster_admin_row *rows, u32 rowCap)
{
    u32 total = 0;
    u32 copied = 0;

    vm_net_mock_monster_resource_labels_load();
    total = g_vm_net_mock_monster_catalog_count;
    copied = vm_net_mock_min_u32(total, rowCap);
    if (rows == NULL || rowCap == 0)
        return total;
    (void)vm_net_mock_monster_db_load();
    memset(rows, 0, sizeof(*rows) * copied);

    for (u32 i = 0; i < copied; ++i)
    {
        const vm_net_mock_monster_entry *entry =
            &g_vm_net_mock_monster_catalog_entries[i];
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
        rows[i].dropCount = vm_net_mock_monster_drops_for_enemy(
            entry->enemyId, rows[i].drops, VM_NET_MOCK_MONSTER_DROP_MAX);
        if (rows[i].dropCount > VM_NET_MOCK_MONSTER_DROP_MAX)
            rows[i].dropCount = VM_NET_MOCK_MONSTER_DROP_MAX;
        rows[i].overridden = override->used;
        snprintf(rows[i].displayName, sizeof(rows[i].displayName), "%s",
                 g_vm_net_mock_monster_resource_labels[i].displayName);
        snprintf(rows[i].firstScene, sizeof(rows[i].firstScene), "%s",
                 g_vm_net_mock_monster_resource_labels[i].firstScene);
    }
    return total;
}

/* sMap.dsh is the only shipped table that explicitly assigns a monster-level
 * bracket to every ordinary local scene.  automonster.dsh tells us which
 * monsters auto-spawn there, but deliberately has no level column.  A bracket
 * such as "18~25级" has no per-monster split, so its rounded-up midpoint is
 * the only deterministic scene-wide reset value. */
static bool vm_net_mock_parse_smap_monster_level(const u8 *value, u32 valueLen,
                                                  u32 *levelOut)
{
    u32 values[2] = {0, 0};
    u32 count = 0;
    u32 pos = 0;

    if (levelOut)
        *levelOut = 0;
    if (value == NULL || valueLen == 0 || levelOut == NULL)
        return false;
    while (pos < valueLen && count < 2)
    {
        u32 parsed = 0;

        while (pos < valueLen && (value[pos] < '0' || value[pos] > '9'))
            ++pos;
        while (pos < valueLen && value[pos] >= '0' && value[pos] <= '9')
        {
            if (parsed > (VM_NET_MOCK_ROLE_LEVEL_CAP -
                          (u32)(value[pos] - '0')) / 10u)
            {
                return false;
            }
            parsed = parsed * 10u + (u32)(value[pos] - '0');
            ++pos;
        }
        if (parsed != 0)
            values[count++] = parsed;
    }
    if (count == 0 || values[0] > VM_NET_MOCK_ROLE_LEVEL_CAP)
        return false;
    if (count == 1)
    {
        *levelOut = values[0];
        return true;
    }
    if (values[1] < values[0] || values[1] > VM_NET_MOCK_ROLE_LEVEL_CAP)
        return false;
    *levelOut = values[0] + (values[1] - values[0] + 1u) / 2u;
    return true;
}

static bool vm_net_mock_scene_monster_level_from_smap(const char *scene,
                                                        u32 *levelOut)
{
    char path[256];
    u8 data[16384];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    size_t sceneLen = 0;

    if (levelOut)
        *levelOut = 0;
    if (levelOut == NULL || !vm_net_mock_scene_name_is_persistable(scene) ||
        !vm_net_mock_open_server_data_resource("sMap.dsh", ".dsh", NULL,
                                                path, sizeof(path)))
    {
        return false;
    }
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 16)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    /* The shipped sMap layout is stable: column 1 is 场景名称 and column 12
     * is 怪物等级.  Reject a changed layout rather than reading an unrelated
     * value as a combat level. */
    if (columnCount < 13 || columnCount > 32 || rowCount > 512)
        return false;
    for (u32 column = 0; column < columnCount; ++column)
    {
        u32 fieldLen = 0;

        if (pos >= len)
            return false;
        fieldLen = data[pos++];
        if (fieldLen > len - pos)
            return false;
        pos += fieldLen;
    }
    sceneLen = strlen(scene);
    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        const u8 *sceneValue = NULL;
        u32 sceneValueLen = 0;
        const u8 *levelValue = NULL;
        u32 levelValueLen = 0;
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;

        if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
            return false;
        for (u32 column = 0; column < columnCount && rowPos < rowEnd;
             ++column)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (valueLen > rowEnd - rowPos)
                return false;
            if (column == 1)
            {
                sceneValue = value;
                sceneValueLen = valueLen;
            }
            else if (column == 12)
            {
                levelValue = value;
                levelValueLen = valueLen;
            }
            rowPos += valueLen;
        }
        if (sceneValue != NULL && levelValue != NULL &&
            sceneValueLen == sceneLen &&
            memcmp(sceneValue, scene, sceneLen) == 0)
        {
            return vm_net_mock_parse_smap_monster_level(levelValue,
                                                         levelValueLen,
                                                         levelOut);
        }
        pos = rowEnd;
    }
    return false;
}

static bool vm_net_mock_monster_admin_level_source_scene(
    u32 enemyId, char *sceneOut, size_t sceneOutCap, const char **sourceOut)
{
    int index = -1;
    u32 total = 0;

    if (sceneOut != NULL && sceneOutCap != 0)
        sceneOut[0] = 0;
    if (sourceOut != NULL)
        *sourceOut = "-";
    if (sceneOut == NULL || sceneOutCap == 0 || enemyId == 0)
        return false;

    /* Auto-spawn rows are the canonical source for ordinary monsters.  A
     * shared actor ID is intentionally resolved from its first authored
     * auto-spawn row, matching the existing global monster-template model. */
    total = vm_net_mock_load_auto_monster_catalog();
    for (u32 row = 0; row < total; ++row)
    {
        const vm_net_mock_auto_monster_catalog_item *item =
            &g_vm_net_mock_auto_monster_catalog[row];

        for (u32 slot = 0; slot < 3; ++slot)
        {
            if (item->monsterIds[slot] != enemyId)
                continue;
            if (!vm_net_mock_scene_name_is_persistable(item->scene))
                return false;
            snprintf(sceneOut, sceneOutCap, "%s", item->scene);
            if (sourceOut != NULL)
                *sourceOut = "automonster";
            return true;
        }
    }

    /* SCE-only combat records have no automonster row.  Their first parsed
     * combat scene is still an authored map identity and can use sMap's
     * level, unlike the former monster-ID bucket fallback. */
    vm_net_mock_monster_resource_labels_load();
    index = vm_net_mock_monster_catalog_index(enemyId);
    if (index < 0 ||
        !vm_net_mock_scene_name_is_persistable(
            g_vm_net_mock_monster_resource_labels[index].firstScene))
    {
        return false;
    }
    snprintf(sceneOut, sceneOutCap, "%s",
             g_vm_net_mock_monster_resource_labels[index].firstScene);
    if (sourceOut != NULL)
        *sourceOut = "sce-combat";
    return true;
}

/* Persist a scene-derived template for every selected monster as one atomic
 * operation.  The level, combat quartet and settlement rewards follow sMap
 * level plus monster family.  Every configured drop remains untouched.  We
 * duplicate inherited default drops into the first persisted override so a
 * later service restart cannot turn a level reset into an accidental
 * drop-table deletion. */
static bool vm_net_mock_monster_admin_reset_scene_levels_batch(
    const u32 *enemyIds, u32 enemyCount, u32 *updatedOut, u32 *skippedOut,
    const char **errorOut)
{
    typedef struct
    {
        int index;
        u8 family;
        u8 dropCount;
        vm_net_mock_monster_stats stats;
        vm_net_mock_monster_drop drops[VM_NET_MOCK_MONSTER_DROP_MAX];
        char scene[64];
        const char *source;
    } vm_net_mock_monster_scene_level_pending;
    vm_net_mock_monster_scene_level_pending
        pending[VM_NET_MOCK_MONSTER_CATALOG_MAX];
    char query[1024];
    char mysqlError[512];
    u32 pendingCount = 0;
    u32 skipped = 0;
    bool transactionStarted = false;

    if (updatedOut)
        *updatedOut = 0;
    if (skippedOut)
        *skippedOut = 0;
    if (errorOut)
        *errorOut = "怪物目录中不存在该 ID";
    if (enemyIds == NULL || enemyCount == 0 ||
        enemyCount > VM_NET_MOCK_MONSTER_CATALOG_MAX)
    {
        return false;
    }
    vm_net_mock_monster_resource_labels_load();
    if (!g_vm_net_mock_monster_db_valid)
    {
        g_vm_net_mock_monster_db_loaded = false;
        if (!vm_net_mock_monster_db_load())
        {
            if (errorOut)
                *errorOut = vm_mysql_last_error();
            return false;
        }
    }

    for (u32 i = 0; i < enemyCount; ++i)
    {
        vm_net_mock_monster_entry entry;
        vm_net_mock_monster_override *override = NULL;
        u32 sceneLevel = 0;
        int index = vm_net_mock_monster_catalog_index(enemyIds[i]);

        if (enemyIds[i] == 0 || index < 0)
            return false;
        for (u32 previous = 0; previous < i; ++previous)
        {
            if (enemyIds[previous] == enemyIds[i])
            {
                if (errorOut)
                    *errorOut = "批量重置中存在重复怪物 ID";
                return false;
            }
        }
        if (!vm_net_mock_monster_admin_level_source_scene(
                enemyIds[i], pending[pendingCount].scene,
                sizeof(pending[pendingCount].scene),
                &pending[pendingCount].source) ||
            !vm_net_mock_scene_monster_level_from_smap(
                pending[pendingCount].scene, &sceneLevel))
        {
            ++skipped;
            continue;
        }

        override = &g_vm_net_mock_monster_overrides[index];
        entry = vm_net_mock_monster_entry_for_enemy(enemyIds[i]);
        entry.level = sceneLevel;
        if (override->used)
            entry.family = override->family;
        pending[pendingCount].index = index;
        pending[pendingCount].family = entry.family;
        pending[pendingCount].stats =
            vm_net_mock_monster_base_stats_for_entry(&entry);
        pending[pendingCount].dropCount = vm_net_mock_monster_drops_for_enemy(
            enemyIds[i], pending[pendingCount].drops,
            VM_NET_MOCK_MONSTER_DROP_MAX);
        if (pending[pendingCount].dropCount > VM_NET_MOCK_MONSTER_DROP_MAX)
            return false;
        ++pendingCount;
    }

    if (pendingCount != 0)
    {
        if (!vm_mysql_exec("START TRANSACTION"))
            goto mysql_failed;
        transactionStarted = true;
        for (u32 i = 0; i < pendingCount; ++i)
        {
            const vm_net_mock_monster_scene_level_pending *row = &pending[i];

            snprintf(query, sizeof(query),
                     "INSERT INTO server_monsters(monster_id,level,family,hp,mp,attack_value,"
                     "defense_value,reward_exp,reward_money,drop_item_id,drop_rate_percent) "
                     "VALUES(%u,%u,%u,%u,%u,%u,%u,%u,%u,0,0) ON DUPLICATE KEY UPDATE "
                     "level=VALUES(level),family=VALUES(family),hp=VALUES(hp),mp=VALUES(mp),"
                     "attack_value=VALUES(attack_value),defense_value=VALUES(defense_value),"
                     "reward_exp=VALUES(reward_exp),reward_money=VALUES(reward_money),"
                     "drop_item_id=0,drop_rate_percent=0",
                     row->stats.enemyId, row->stats.level, row->family,
                     row->stats.hp, row->stats.mp, row->stats.attack,
                     row->stats.defense, row->stats.exp, row->stats.gold);
            if (!vm_mysql_exec(query))
                goto mysql_failed;
            snprintf(query, sizeof(query),
                     "DELETE FROM server_monster_drops WHERE monster_id=%u",
                     row->stats.enemyId);
            if (!vm_mysql_exec(query))
                goto mysql_failed;
            for (u8 drop = 0; drop < row->dropCount; ++drop)
            {
                char rateText[16];

                memset(rateText, 0, sizeof(rateText));
                vm_net_mock_format_drop_rate_basis_points(
                    row->drops[drop].rateBasisPoints, rateText,
                    sizeof(rateText));
                snprintf(query, sizeof(query),
                         "INSERT INTO server_monster_drops("
                         "monster_id,drop_slot,item_id,drop_rate_percent) "
                         "VALUES(%u,%u,%u,%s)",
                         row->stats.enemyId, (u32)drop + 1u,
                         row->drops[drop].itemId,
                         rateText);
                if (!vm_mysql_exec(query))
                    goto mysql_failed;
            }
        }
        if (!vm_mysql_exec("COMMIT"))
            goto mysql_failed;
        transactionStarted = false;
    }

    for (u32 i = 0; i < pendingCount; ++i)
    {
        vm_net_mock_monster_override *override =
            &g_vm_net_mock_monster_overrides[pending[i].index];

        memset(override, 0, sizeof(*override));
        override->used = true;
        override->family = pending[i].family;
        override->stats = pending[i].stats;
        override->dropCount = pending[i].dropCount;
        if (override->dropCount != 0)
        {
            memcpy(override->drops, pending[i].drops,
                   sizeof(override->drops[0]) * override->dropCount);
        }
        printf("[info][mock-admin] monster_scene_level_reset id=%u scene=%s "
               "source=%s level=%u family=%u hp=%u mp=%u attack=%u defense=%u "
               "exp=%u money=%u preserve=drops\n",
               override->stats.enemyId, pending[i].scene,
               pending[i].source ? pending[i].source : "-",
               override->stats.level, override->family, override->stats.hp,
               override->stats.mp, override->stats.attack,
               override->stats.defense, override->stats.exp,
               override->stats.gold);
    }
    if (updatedOut)
        *updatedOut = pendingCount;
    if (skippedOut)
        *skippedOut = skipped;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_scene_level_batch_reset selected=%u "
           "updated=%u skipped_without_smap_level=%u transaction=%s "
           "source=sMap.dsh\n",
           enemyCount, pendingCount, skipped,
           pendingCount == 0 ? "not-needed" : "committed");
    return true;

mysql_failed:
    snprintf(mysqlError, sizeof(mysqlError), "%s", vm_mysql_last_error());
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] monster_scene_level_batch_reset_failed "
           "selected=%u staged=%u error=%s\n",
           enemyCount, pendingCount, mysqlError);
    if (errorOut)
        *errorOut = "按场景等级重置失败，请检查服务端 MySQL 日志";
    return false;
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
           vm_net_mock_scene_names_equal_exact(
               scene,
               "\x63\x30\x34\xc1\xd9\xb0\xb2\xb8\xae\x5f\x30\x31\x2e\x73\x63\x65"); /* c04临安府_01.sce */
}

/* The original server advertises its arena host at 蜀山南门.  The source SCE
 * already has 苍古 at a verified walkable placement, so give that real native
 * actor the server-side arena service by default rather than inventing a
 * second actor coordinate.  Admin service configuration can still add the
 * same arena service to any explicitly selected dynamic/native NPC. */
static bool vm_net_mock_scene_is_shushan_south_gate(const char *scene)
{
    return scene != NULL &&
           vm_net_mock_scene_names_equal_exact(
               scene,
               "\x63\x31\x34\xca\xf1\xc9\xbd\x5f\x30\x31\x2e\x73\x63\x65"); /* c14蜀山_01.sce */
}

enum
{
    VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX = 256,
    /* Native SCE actors are immutable resource data.  These tables contain
     * only server-owned overlays keyed by the exact runtime scene filename
     * and the deterministic actor id derived from that source row. */
    VM_NET_MOCK_NATIVE_NPC_OVERRIDE_MAX = 512,
    VM_NET_MOCK_NPC_SHOP_INVENTORY_MAX = 4096
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
    vm_net_mock_scene_npcinfo_seed seed;
    bool enabled;
    bool overridden;
} vm_net_mock_native_npc_admin_row;

typedef struct
{
    char scene[64];
    u32 actorId;
    u16 serviceKind;
    bool enabled;
} vm_net_mock_native_npc_override;

typedef struct
{
    char scene[64];
    u32 actorId;
    u32 itemId;
    u32 unitPrice;
    bool enabled;
} vm_net_mock_npc_shop_inventory_row;

typedef struct
{
    char scene[64];
    char previousTargetScene[64];
    char canonicalTargetScene[64];
    u32 actorId;
} vm_net_mock_dynamic_npc_instance_scene_migration;

/* A dynamic NPC itself is keyed by the exact SCE filename.  Earlier admin
 * builds persisted some rows without the .sce suffix, creating a second SQL
 * primary key for the same runtime NPC once an administrator later saved the
 * correct SCE key. */
typedef struct
{
    char legacyScene[64];
    char canonicalScene[64];
    u32 actorId;
} vm_net_mock_dynamic_npc_parent_scene_migration;

typedef struct
{
    u32 loaded;
    u32 skipped;
    u32 quarantined;
    u32 migrated;
    u32 migrationFailures;
    vm_net_mock_dynamic_npc_instance_scene_migration
        migrations[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
    u32 migrationCount;
    vm_net_mock_dynamic_npc_parent_scene_migration
        parentMigrations[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
    u32 parentMigrationCount;
    u32 parentMigrated;
    u32 parentMigrationFailures;
    bool scanningParentSceneMigrations;
} vm_net_mock_dynamic_npc_load_context;

typedef struct
{
    bool found;
    bool invalid;
    u32 count;
} vm_net_mock_dynamic_npc_column_context;

typedef struct
{
    bool found;
    bool invalid;
    u16 serviceKind;
} vm_net_mock_dynamic_npc_exact_kind_context;

static bool vm_net_mock_dynamic_npc_column_count_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths);

static bool vm_net_mock_dynamic_npc_exact_kind_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths);

static vm_net_mock_dynamic_npc_override
    g_vm_net_mock_dynamic_npc_overrides[VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX];
static u32 g_vm_net_mock_dynamic_npc_override_count = 0;
static bool g_vm_net_mock_dynamic_npc_db_loaded = false;
static bool g_vm_net_mock_dynamic_npc_db_valid = false;
static vm_net_mock_native_npc_override
    g_vm_net_mock_native_npc_overrides[VM_NET_MOCK_NATIVE_NPC_OVERRIDE_MAX];
static u32 g_vm_net_mock_native_npc_override_count = 0;
static vm_net_mock_npc_shop_inventory_row
    g_vm_net_mock_npc_shop_inventory[VM_NET_MOCK_NPC_SHOP_INVENTORY_MAX];
static u32 g_vm_net_mock_npc_shop_inventory_count = 0;
static bool g_vm_net_mock_native_npc_db_loaded = false;
static bool g_vm_net_mock_native_npc_db_valid = false;

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

static bool vm_net_mock_native_npc_override_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_native_npc_override row;
    u32 number[3];

    (void)contextValue;
    memset(&row, 0, sizeof(row));
    memset(number, 0, sizeof(number));
    if (columnCount != 4 ||
        g_vm_net_mock_native_npc_override_count >=
            VM_NET_MOCK_NATIVE_NPC_OVERRIDE_MAX ||
        !vm_net_mock_dynamic_npc_decode_hex(values[0], lengths[0], row.scene,
                                            sizeof(row.scene)) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &number[0]) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &number[1]) ||
        number[1] > VM_NET_MOCK_NPC_KIND_MAX ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &number[2]) ||
        number[2] > 1u || row.scene[0] == 0 || number[0] == 0)
    {
        printf("[warn][mock-admin] native_npc_override_row action=skip-invalid\n");
        return true;
    }
    row.actorId = number[0];
    row.serviceKind = (u16)number[1];
    row.enabled = number[2] != 0;
    g_vm_net_mock_native_npc_overrides[
        g_vm_net_mock_native_npc_override_count++] = row;
    return true;
}

static bool vm_net_mock_npc_shop_inventory_db_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_npc_shop_inventory_row row;
    u32 number[4];

    (void)contextValue;
    memset(&row, 0, sizeof(row));
    memset(number, 0, sizeof(number));
    if (columnCount != 5 ||
        g_vm_net_mock_npc_shop_inventory_count >=
            VM_NET_MOCK_NPC_SHOP_INVENTORY_MAX ||
        !vm_net_mock_dynamic_npc_decode_hex(values[0], lengths[0], row.scene,
                                            sizeof(row.scene)) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &number[0]) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &number[1]) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &number[2]) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &number[3]) ||
        number[0] == 0 || number[1] == 0 || number[2] == 0 ||
        number[3] > 1u || row.scene[0] == 0)
    {
        printf("[warn][mock-admin] npc_shop_inventory_row action=skip-invalid\n");
        return true;
    }
    row.actorId = number[0];
    row.itemId = number[1];
    row.unitPrice = number[2];
    row.enabled = number[3] != 0;
    g_vm_net_mock_npc_shop_inventory[
        g_vm_net_mock_npc_shop_inventory_count++] = row;
    return true;
}

static bool vm_net_mock_npc_service_options_table_ensure(void);

static bool vm_net_mock_native_npc_db_load(void)
{
    if (g_vm_net_mock_native_npc_db_loaded)
        return g_vm_net_mock_native_npc_db_valid;
    g_vm_net_mock_native_npc_db_loaded = true;
    g_vm_net_mock_native_npc_db_valid = false;
    g_vm_net_mock_native_npc_override_count = 0;
    g_vm_net_mock_npc_shop_inventory_count = 0;
    memset(g_vm_net_mock_native_npc_overrides, 0,
           sizeof(g_vm_net_mock_native_npc_overrides));
    memset(g_vm_net_mock_npc_shop_inventory, 0,
           sizeof(g_vm_net_mock_npc_shop_inventory));

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_native_npc_overrides ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "service_kind SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_npc_shop_inventory ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL,unit_price INT UNSIGNED NOT NULL,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id,item_id),"
            "KEY idx_server_npc_shop_inventory_npc(scene,actor_id)) ENGINE=InnoDB") ||
        !vm_net_mock_npc_service_options_table_ensure() ||
        !vm_mysql_query(
            "SELECT HEX(scene),actor_id,service_kind,enabled "
            "FROM server_native_npc_overrides ORDER BY scene,actor_id",
            vm_net_mock_native_npc_override_row, NULL) ||
        !vm_mysql_query(
            "SELECT HEX(scene),actor_id,item_id,unit_price,enabled "
            "FROM server_npc_shop_inventory ORDER BY scene,actor_id,item_id",
            vm_net_mock_npc_shop_inventory_db_row, NULL))
    {
        printf("[error][mock-admin] native_npc_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_native_npc_db_valid = true;
    printf("[info][mock-admin] native_npc_db_load overrides=%u inventory=%u\n",
           g_vm_net_mock_native_npc_override_count,
           g_vm_net_mock_npc_shop_inventory_count);
    return true;
}

/* A service row is intentionally independent of the dynamic-NPC and native
 * override tables: both kinds of scene actor use the same 26/1 action=1
 * contract.  Kind zero is a persisted marker for an explicitly empty service
 * set, so an administrator can remove all services without falling back to a
 * legacy single npc_kind value. */
typedef struct
{
    bool configured;
    bool invalid;
    u32 count;
    vm_net_mock_npc_service_option
        options[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
} vm_net_mock_npc_service_options_context;

/* Keep schema installation on the catalog-load/admin paths.  NPC dialogue
 * handling only performs a read from this table; doing CREATE TABLE for each
 * click would add a DDL round-trip to a hot client packet path. */
static bool vm_net_mock_npc_service_options_table_ensure(void)
{
    return vm_mysql_exec(
        "CREATE TABLE IF NOT EXISTS server_npc_services ("
        "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
        "service_kind SMALLINT UNSIGNED NOT NULL,"
        "sort_order TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "option_name VARBINARY(64) NOT NULL DEFAULT '',"
        "option_description VARBINARY(96) NOT NULL DEFAULT '',"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY(scene,actor_id,service_kind),"
        "KEY idx_server_npc_services_dialog(scene,actor_id,sort_order,service_kind)"
        ") ENGINE=InnoDB");
}

static bool vm_net_mock_npc_service_options_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_npc_service_options_context *context =
        (vm_net_mock_npc_service_options_context *)contextValue;
    vm_net_mock_npc_service_option option;
    u32 kind = 0;
    u32 sortOrder = 0;

    memset(&option, 0, sizeof(option));
    if (context == NULL || columnCount != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &kind) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &sortOrder) ||
        kind > VM_NET_MOCK_NPC_KIND_MAX || sortOrder > 0xffu)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->configured = true;
    if (kind == VM_NET_MOCK_NPC_KIND_NORMAL)
    {
        char ignoredName[64];
        char ignoredDescription[96];

        memset(ignoredName, 0, sizeof(ignoredName));
        memset(ignoredDescription, 0, sizeof(ignoredDescription));
        if (!vm_net_mock_dynamic_npc_decode_hex(values[2], lengths[2],
                                                ignoredName,
                                                sizeof(ignoredName)) ||
            !vm_net_mock_dynamic_npc_decode_hex(values[3], lengths[3],
                                                ignoredDescription,
                                                sizeof(ignoredDescription)) ||
            ignoredName[0] != 0 || ignoredDescription[0] != 0)
        {
            context->invalid = true;
        }
        return true;
    }
    if (context->count >= VM_NET_MOCK_NPC_SERVICE_OPTION_MAX ||
        !vm_net_mock_dynamic_npc_decode_hex(values[2], lengths[2],
                                            option.optionName,
                                            sizeof(option.optionName)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[3], lengths[3],
                                            option.optionDescription,
                                            sizeof(option.optionDescription)))
    {
        context->invalid = true;
        return true;
    }
    /* Older admin pages rendered empty optional text as a literal dash. */
    if (strcmp(option.optionName, "-") == 0)
        option.optionName[0] = 0;
    if (strcmp(option.optionDescription, "-") == 0)
        option.optionDescription[0] = 0;
    for (u32 i = 0; i < context->count; ++i)
    {
        if (context->options[i].kind == (u16)kind)
        {
            context->invalid = true;
            return true;
        }
    }
    option.kind = (u16)kind;
    option.sortOrder = (u8)sortOrder;
    context->options[context->count++] = option;
    return true;
}

/* Resolve the effective configuration for a concrete `(scene, actor)`.
 * No relation rows preserves historical one-kind NPCs; a marker row converts
 * that legacy fallback into an explicitly empty set. */
static bool vm_net_mock_npc_service_options_resolve(
    const char *scene, u32 actorId, u16 legacyKind, const char *legacyName,
    const char *legacyDescription, vm_net_mock_npc_service_option *options,
    u32 optionCap, u32 *optionCountOut, bool *configuredOut)
{
    vm_net_mock_npc_service_options_context context;
    char sceneHex[64 * 2 + 1];
    char query[512];

    if (optionCountOut != NULL)
        *optionCountOut = 0;
    if (configuredOut != NULL)
        *configuredOut = false;
    if (scene == NULL || actorId == 0 ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        (options == NULL && optionCap != 0) ||
        optionCap < VM_NET_MOCK_NPC_SERVICE_OPTION_MAX ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT service_kind,sort_order,HEX(option_name),HEX(option_description) "
             "FROM server_npc_services WHERE scene=X'%s' AND actor_id=%u "
             "ORDER BY sort_order,service_kind",
             sceneHex, actorId);
    if (!vm_mysql_query(query, vm_net_mock_npc_service_options_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (!context.configured && legacyKind != VM_NET_MOCK_NPC_KIND_NORMAL)
    {
        vm_net_mock_npc_service_option legacy;

        if (legacyKind > VM_NET_MOCK_NPC_KIND_MAX)
            return false;
        memset(&legacy, 0, sizeof(legacy));
        legacy.kind = legacyKind;
        if (legacyName != NULL && strcmp(legacyName, "-") != 0)
            snprintf(legacy.optionName, sizeof(legacy.optionName), "%s",
                     legacyName);
        if (legacyDescription != NULL && strcmp(legacyDescription, "-") != 0)
            snprintf(legacy.optionDescription,
                     sizeof(legacy.optionDescription), "%s",
                     legacyDescription);
        context.options[0] = legacy;
        context.count = 1;
    }
    if (context.count != 0 && options != NULL)
        memcpy(options, context.options,
               context.count * sizeof(context.options[0]));
    if (optionCountOut != NULL)
        *optionCountOut = context.count;
    if (configuredOut != NULL)
        *configuredOut = context.configured;
    return true;
}

static bool vm_net_mock_npc_service_options_has_kind(
    const vm_net_mock_npc_service_option *options, u32 optionCount,
    u16 serviceKind)
{
    if (options == NULL || serviceKind == VM_NET_MOCK_NPC_KIND_NORMAL ||
        serviceKind > VM_NET_MOCK_NPC_KIND_MAX)
    {
        return false;
    }
    for (u32 i = 0; i < optionCount; ++i)
    {
        if (options[i].kind == serviceKind)
            return true;
    }
    return false;
}

/*
 * Scene battle monsters are deliberately not dynamic-NPC rows.  The CBE
 * scene loader creates battle-capable type-2 nodes only from SCE2 kind-3
 * records, and mmBattle:HandleBattleStartMsg(0x66CC) copies a live type-2
 * node for 4/5 scene battles.  This configuration layer therefore keeps a
 * draft in MySQL and has an explicit deployment step which rebuilds the
 * server-owned SCE source from a captured base resource.  Saving a draft can
 * never cause an NPC packet to masquerade as a monster.
 */
enum
{
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX = 96,
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX = 8192,
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX = 8192,
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX = 24,
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX = 5,
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_SPAWN_SPACING = 16,
    /* Scene-battle drafts own their combat profile.  Keep generated IDs well
     * clear of the shipped catalog, whose stable IDs are also valid picker
     * templates. */
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN = 2000,
    /* One SCE kind-3 record has two Actor roots, each of which can own the
     * validated maximum of 16 GIF leaves.  The node limit bounds enabled
     * records to 24, so this is the largest meaningful per-deployment list. */
    VM_NET_MOCK_SCENE_BATTLE_MONSTER_PUBLISH_NAME_MAX =
        1 + VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX * 2 *
                (1 + VM_NET_MOCK_ACTOR_RESOURCE_IMAGE_MAX)
};

typedef struct
{
    u32 entryId;
    u16 monsterId;
    u16 x;
    u16 y;
    u16 quantity;
    u16 visualHint;
    bool enabled;
    char displayName[32];
    char actorResource[64];
    char effectResource[64];
} vm_net_mock_scene_battle_monster_admin_row;

typedef struct
{
    vm_net_mock_scene_battle_monster_admin_row *rows;
    u32 cap;
    u32 count;
    bool invalid;
} vm_net_mock_scene_battle_monster_list_context;

typedef struct
{
    u8 *raw;
    u32 rawCap;
    u32 rawLen;
    bool found;
    bool invalid;
} vm_net_mock_scene_battle_monster_source_context;

typedef struct
{
    u32 fingerprint;
    u32 configuredCount;
    bool found;
    bool invalid;
} vm_net_mock_scene_battle_monster_deployment_context;

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_net_mock_scene_battle_monster_count_context;

typedef struct
{
    u32 maximum;
    bool found;
    bool invalid;
} vm_net_mock_scene_battle_monster_id_max_context;

typedef struct
{
    u32 rows;
    u32 added;
    u32 labeled;
    u32 rejected;
    bool invalid;
} vm_net_mock_monster_catalog_scene_battle_draft_context;

typedef struct
{
    bool found;
    bool invalid;
} vm_net_mock_scene_battle_monster_column_context;

static bool g_vm_net_mock_scene_battle_monster_effect_column_ready = false;
static bool g_vm_net_mock_scene_battle_monster_effect_contract_ready = false;
static bool g_vm_net_mock_scene_battle_monster_quantity_column_ready = false;

static bool vm_net_mock_scene_battle_monster_column_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths);
static bool vm_net_mock_scene_battle_monster_deployed_source_matches(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCount);

/* field18 is not an arbitrary visual effect slot.  Across the shipped SCE2
 * combat records its value is the monster defeat-effect resource.  In
 * particular, f_blood1.actor is a battle hit effect and was never used in a
 * shipped kind-3 record; accepting it made a syntactically valid deployment
 * that the client did not turn into a live kind-2 scene node. */
static bool vm_net_mock_scene_battle_monster_effect_resource_is_supported(
    const char *resource)
{
    return resource != NULL &&
           (strcmp(resource, "e_ghostfireR.actor") == 0 ||
            strcmp(resource, "e_ghostfireG.actor") == 0 ||
            strcmp(resource, "e_ghostfireB.actor") == 0 ||
            strcmp(resource, "e_ghostfiresG.actor") == 0);
}

/* A field-17 body is not an arbitrary Actor descriptor.  The CBE only loads
 * it as a battle-capable scene node for resources it has already used in a
 * shipped kind-3 record.  Keep that compatibility set separate from the
 * general Actor editor catalogue: an unrelated Actor can be structurally
 * valid yet leave only its field-18 fireball visible at runtime. */
enum { VM_NET_MOCK_SCENE_BATTLE_MONSTER_NATIVE_BODY_MAX = 512 };

typedef struct
{
    char resource[64];
    u16 visualHint;
    bool visualHintAmbiguous;
    char firstScene[64];
} vm_net_mock_scene_battle_monster_native_body;

static vm_net_mock_scene_battle_monster_native_body
    g_vm_net_mock_scene_battle_monster_native_bodies[
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_NATIVE_BODY_MAX];
static u32 g_vm_net_mock_scene_battle_monster_native_body_count = 0;
static bool g_vm_net_mock_scene_battle_monster_native_bodies_loaded = false;
static bool g_vm_net_mock_scene_battle_monster_native_bodies_loading = false;

static bool vm_net_mock_scene_battle_monster_body_resource_is_supported(
    const char *resource);
static bool vm_net_mock_scene_battle_monster_body_visual_hint(
    const char *resource, u16 *hintOut);

/* One draft expands into independent native kind-3 nodes.  The configured
 * coordinate is the center node; the remaining four use a stable cross so
 * every node has a distinct live index and collision position. */
static bool vm_net_mock_scene_battle_monster_expanded_row(
    const vm_net_mock_scene_battle_monster_admin_row *row, u32 ordinal,
    vm_net_mock_scene_battle_monster_admin_row *expandedOut)
{
    static const int offsets[VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX][2] = {
        {0, 0},
        {-VM_NET_MOCK_SCENE_BATTLE_MONSTER_SPAWN_SPACING, 0},
        {VM_NET_MOCK_SCENE_BATTLE_MONSTER_SPAWN_SPACING, 0},
        {0, -VM_NET_MOCK_SCENE_BATTLE_MONSTER_SPAWN_SPACING},
        {0, VM_NET_MOCK_SCENE_BATTLE_MONSTER_SPAWN_SPACING}
    };
    int x = 0;
    int y = 0;

    if (row == NULL || expandedOut == NULL || row->quantity == 0 ||
        row->quantity > VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX ||
        ordinal >= row->quantity)
    {
        return false;
    }
    x = (int)row->x + offsets[ordinal][0];
    y = (int)row->y + offsets[ordinal][1];
    if (x <= 0 || x > 0xffff || y <= 0 || y > 0xffff)
        return false;
    *expandedOut = *row;
    expandedOut->x = (u16)x;
    expandedOut->y = (u16)y;
    expandedOut->quantity = 1;
    return true;
}

static u32 vm_net_mock_scene_battle_monster_enabled_node_count(
    const vm_net_mock_scene_battle_monster_admin_row *rows, u32 rowCount)
{
    u32 count = 0;

    for (u32 i = 0; rows != NULL && i < rowCount; ++i)
    {
        if (!rows[i].enabled || rows[i].quantity == 0 ||
            rows[i].quantity > VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX ||
            count > 0xffffffffu - rows[i].quantity)
        {
            if (rows[i].enabled)
                return 0xffffffffu;
            continue;
        }
        count += rows[i].quantity;
    }
    return count;
}

/* All shipped SCE kind-3 records carry a field-18 effect Actor.  This is a
 * native scene-record dependency, not an optional visual override.  The
 * migration intentionally assigns the same early-scene effect Actor that is
 * present beside e_mucusP.actor in the shipped 01桃花岛_01.sce samples, so
 * existing drafts become explicit, editable complete records rather than
 * remaining ambiguous/truncated rows. */
static bool vm_net_mock_scene_battle_monster_ensure_effect_column(void)
{
    vm_net_mock_scene_battle_monster_column_context context;

    if (g_vm_net_mock_scene_battle_monster_effect_column_ready)
        return true;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() "
            "AND TABLE_NAME='server_scene_battle_monsters' "
            "AND COLUMN_NAME='effect_resource'",
            vm_net_mock_scene_battle_monster_column_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (!context.found)
    {
        if (!vm_mysql_exec(
                "ALTER TABLE server_scene_battle_monsters "
                "ADD COLUMN effect_resource VARBINARY(64) NOT NULL "
                "DEFAULT 'e_ghostfireR.actor' AFTER actor_resource"))
        {
            return false;
        }
        printf("[info][mock-admin] scene_battle_monster_schema "
               "migration=effect-resource-field18 action=applied "
               "default=e_ghostfireR.actor\n");
    }
    /* The former all-Actor picker allowed this known incompatible resource.
     * This narrow one-time data migration repairs only that documented legacy
     * value; other unknown values stay visible and are rejected on deploy so
     * their intended semantics are not silently guessed. */
    if (!g_vm_net_mock_scene_battle_monster_effect_contract_ready)
    {
        if (!vm_mysql_exec(
                "UPDATE server_scene_battle_monsters "
                "SET effect_resource='e_ghostfireR.actor' "
                "WHERE effect_resource='f_blood1.actor'"))
        {
            return false;
        }
        printf("[info][mock-admin] scene_battle_monster_schema "
               "migration=field18-hit-effect-to-native-defeat-effect "
               "from=f_blood1.actor to=e_ghostfireR.actor\n");
        g_vm_net_mock_scene_battle_monster_effect_contract_ready = true;
    }
    g_vm_net_mock_scene_battle_monster_effect_column_ready = true;
    return true;
}

static bool vm_net_mock_scene_battle_monster_ensure_quantity_column(void)
{
    vm_net_mock_scene_battle_monster_column_context context;

    if (g_vm_net_mock_scene_battle_monster_quantity_column_ready)
        return true;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() "
            "AND TABLE_NAME='server_scene_battle_monsters' "
            "AND COLUMN_NAME='quantity'",
            vm_net_mock_scene_battle_monster_column_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (!context.found &&
        !vm_mysql_exec(
            "ALTER TABLE server_scene_battle_monsters "
            "ADD COLUMN quantity TINYINT UNSIGNED NOT NULL DEFAULT 1 "
            "AFTER pos_y"))
    {
        return false;
    }
    printf("[info][mock-admin] scene_battle_monster_schema "
           "migration=quantity action=ready default=1 max=%u\n",
           VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX);
    g_vm_net_mock_scene_battle_monster_quantity_column_ready = true;
    return true;
}

static bool vm_net_mock_scene_battle_monster_column_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_scene_battle_monster_column_context *context =
        (vm_net_mock_scene_battle_monster_column_context *)contextValue;

    if (context == NULL || columnCount != 1 || values == NULL ||
        lengths == NULL || values[0] == NULL || lengths[0] == 0 ||
        context->found)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_scene_battle_monster_count_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_scene_battle_monster_count_context *context =
        (vm_net_mock_scene_battle_monster_count_context *)contextValue;

    if (context == NULL || columnCount != 1 || context->found ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->count))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_scene_battle_monster_admin_count_scene(
    const char *scene, u32 *countOut)
{
    vm_net_mock_scene_battle_monster_count_context context;
    char sceneHex[129];
    char query[320];

    if (countOut)
        *countOut = 0;
    if (scene == NULL || !vm_net_mock_scene_name_is_safe(scene) ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM server_scene_battle_monsters WHERE scene=X'%s'",
             sceneHex);
    if (!vm_mysql_query(query, vm_net_mock_scene_battle_monster_count_row,
                        &context) || context.invalid || !context.found)
    {
        return false;
    }
    if (countOut)
        *countOut = context.count;
    return true;
}

static bool vm_net_mock_scene_battle_monster_schema_ensure(void)
{
    return vm_mysql_exec(
               "CREATE TABLE IF NOT EXISTS server_scene_battle_monsters ("
               "entry_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
               "scene VARBINARY(64) NOT NULL,"
               "monster_id SMALLINT UNSIGNED NOT NULL,"
               "pos_x SMALLINT UNSIGNED NOT NULL,pos_y SMALLINT UNSIGNED NOT NULL,"
               "quantity TINYINT UNSIGNED NOT NULL DEFAULT 1,"
               "display_name VARBINARY(30) NOT NULL,"
               "actor_resource VARBINARY(64) NOT NULL,"
               "effect_resource VARBINARY(64) NOT NULL DEFAULT 'e_ghostfireR.actor',"
               "visual_hint TINYINT UNSIGNED NOT NULL DEFAULT 5,"
               "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
               "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
               "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
               "PRIMARY KEY(entry_id),KEY idx_scene_battle_monster_scene(scene),"
               "UNIQUE KEY uq_scene_battle_monster_pos(scene,monster_id,pos_x,pos_y)"
               ") ENGINE=InnoDB") &&
           vm_mysql_exec(
               "CREATE TABLE IF NOT EXISTS server_scene_battle_monster_sources ("
               "scene VARBINARY(64) NOT NULL,base_resource MEDIUMBLOB NOT NULL,"
               "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
               "PRIMARY KEY(scene)) ENGINE=InnoDB") &&
           vm_mysql_exec(
               "CREATE TABLE IF NOT EXISTS server_scene_battle_monster_deployments ("
               "scene VARBINARY(64) NOT NULL,config_fingerprint INT UNSIGNED NOT NULL,"
               "configured_count SMALLINT UNSIGNED NOT NULL,"
               "deployed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
               "PRIMARY KEY(scene)) ENGINE=InnoDB") &&
           vm_net_mock_scene_battle_monster_ensure_effect_column() &&
           vm_net_mock_scene_battle_monster_ensure_quantity_column();
}

static bool vm_net_mock_scene_battle_monster_decode_hex_text(
    const char *hex, size_t hexLen, char *out, size_t outCap)
{
    size_t decodedLen = 0;

    if (hex == NULL || out == NULL || outCap == 0 ||
        hexLen == 0 || (hexLen & 1u) != 0 ||
        !vm_mysql_hex_decode(hex, hexLen, out, outCap - 1u, &decodedLen) ||
        decodedLen == 0 || decodedLen >= outCap)
    {
        return false;
    }
    out[decodedLen] = 0;
    return true;
}

static bool vm_net_mock_scene_battle_monster_list_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_scene_battle_monster_list_context *context =
        (vm_net_mock_scene_battle_monster_list_context *)contextValue;
    vm_net_mock_scene_battle_monster_admin_row *row = NULL;
    u32 value = 0;

    if (context == NULL || columnCount != 10 || values == NULL ||
        lengths == NULL || context->count >= context->cap)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    row = &context->rows[context->count];
    memset(row, 0, sizeof(*row));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->entryId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &value) ||
        value == 0 || value > 0xffffu)
    {
        context->invalid = true;
        return true;
    }
    row->monsterId = (u16)value;
    if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &value) ||
        value == 0 || value > 0xffffu)
    {
        context->invalid = true;
        return true;
    }
    row->x = (u16)value;
    if (!vm_mock_mysql_parse_u32(values[3], lengths[3], &value) ||
        value == 0 || value > 0xffffu)
    {
        context->invalid = true;
        return true;
    }
    row->y = (u16)value;
    if (!vm_mock_mysql_parse_u32(values[4], lengths[4], &value) ||
        value == 0 ||
        value > VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX)
    {
        context->invalid = true;
        return true;
    }
    row->quantity = (u16)value;
    if (!vm_net_mock_scene_battle_monster_decode_hex_text(
            values[5], lengths[5], row->displayName,
            sizeof(row->displayName)) ||
        !vm_net_mock_scene_battle_monster_decode_hex_text(
            values[6], lengths[6], row->actorResource,
            sizeof(row->actorResource)) ||
        !vm_net_mock_scene_battle_monster_decode_hex_text(
            values[7], lengths[7], row->effectResource,
            sizeof(row->effectResource)) ||
        !vm_mock_mysql_parse_u32(values[8], lengths[8], &value) ||
        value == 0 || value > 0xffffu)
    {
        context->invalid = true;
        return true;
    }
    row->visualHint = (u16)value;
    if (!vm_mock_mysql_parse_u32(values[9], lengths[9], &value) || value > 1)
    {
        context->invalid = true;
        return true;
    }
    row->enabled = value != 0;
    ++context->count;
    return true;
}

/* This is intentionally a catalog-only projection of the draft table.  It
 * does not make the draft a live SCE node: the entry remains draft-only until
 * a normal deployment makes the same ID observable in an SCE2 kind-3 record.
 * Reusing the catalog slot lets Monster Management persist stats and drops
 * before deployment, without maintaining a second incompatible override
 * store. */
static bool vm_net_mock_monster_catalog_scene_battle_draft_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_monster_catalog_scene_battle_draft_context *context =
        (vm_net_mock_monster_catalog_scene_battle_draft_context *)contextValue;
    vm_net_mock_monster_resource_label *label = NULL;
    u32 monsterId = 0;
    int index = -1;
    bool existing = false;

    if (context == NULL || columnCount != 3 || values == NULL ||
        lengths == NULL || values[0] == NULL || values[1] == NULL ||
        values[2] == NULL)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    if (!vm_mock_mysql_parse_u32(values[1], lengths[1], &monsterId) ||
        monsterId == 0 || monsterId > 0xffffu || lengths[0] == 0 ||
        lengths[0] >= sizeof(g_vm_net_mock_monster_resource_labels[0].firstScene) ||
        lengths[2] >= sizeof(g_vm_net_mock_monster_resource_labels[0].displayName))
    {
        ++context->rejected;
        return true;
    }
    existing = vm_net_mock_monster_catalog_index_loaded(monsterId) >= 0;
    index = vm_net_mock_monster_catalog_add_scene_entry(monsterId);
    if (index < 0)
    {
        ++context->rejected;
        return true;
    }

    ++context->rows;
    if (!existing)
    {
        g_vm_net_mock_monster_catalog_draft_only[index] = true;
        ++context->added;
    }
    label = &g_vm_net_mock_monster_resource_labels[index];
    if (label->displayName[0] == 0 && lengths[2] != 0)
    {
        memcpy(label->displayName, values[2], lengths[2]);
        label->displayName[lengths[2]] = 0;
        ++context->labeled;
    }
    if (label->firstScene[0] == 0)
    {
        memcpy(label->firstScene, values[0], lengths[0]);
        label->firstScene[lengths[0]] = 0;
        ++context->labeled;
    }
    return true;
}

static u32 vm_net_mock_monster_catalog_add_scene_battle_drafts(void)
{
    vm_net_mock_monster_catalog_scene_battle_draft_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_net_mock_scene_battle_monster_schema_ensure() ||
        !vm_mysql_query(
            "SELECT scene,monster_id,display_name "
            "FROM server_scene_battle_monsters ORDER BY entry_id",
            vm_net_mock_monster_catalog_scene_battle_draft_row, &context) ||
        context.invalid)
    {
        printf("[warn][mock-admin] monster_catalog_scene_battle_drafts "
               "action=skip error=%s\n", vm_mysql_last_error());
        return 0;
    }
    printf("[info][mock-admin] monster_catalog_scene_battle_drafts "
           "rows=%u added=%u labels=%u rejected=%u\n",
           context.rows, context.added, context.labeled, context.rejected);
    return context.added;
}

static u32 vm_net_mock_scene_battle_monster_admin_list(
    const char *scene, vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCap)
{
    vm_net_mock_scene_battle_monster_list_context context;
    char sceneHex[129];
    char query[640];

    if (rows == NULL || rowCap == 0 || scene == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_battle_monster_schema_ensure() ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return 0;
    }
    memset(rows, 0, sizeof(*rows) * rowCap);
    memset(&context, 0, sizeof(context));
    context.rows = rows;
    context.cap = rowCap;
    snprintf(query, sizeof(query),
             "SELECT entry_id,monster_id,pos_x,pos_y,quantity,HEX(display_name),"
             "HEX(actor_resource),HEX(effect_resource),visual_hint,enabled "
             "FROM server_scene_battle_monsters WHERE scene=X'%s' "
             "ORDER BY entry_id",
             sceneHex);
    if (!vm_mysql_query(query, vm_net_mock_scene_battle_monster_list_row,
                        &context) || context.invalid)
    {
        return 0;
    }
    return context.count;
}

static bool vm_net_mock_scene_battle_monster_id_max_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_scene_battle_monster_id_max_context *context =
        (vm_net_mock_scene_battle_monster_id_max_context *)contextValue;

    if (context == NULL || columnCount != 1 || context->found ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->maximum) ||
        context->maximum > 0xffffu)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_scene_battle_monster_admin_get_entry(
    const char *scene, u32 entryId,
    vm_net_mock_scene_battle_monster_admin_row *rowOut)
{
    vm_net_mock_scene_battle_monster_admin_row
        rows[VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX];
    u32 rowCount = 0;

    if (rowOut != NULL)
        memset(rowOut, 0, sizeof(*rowOut));
    if (scene == NULL || entryId == 0 || rowOut == NULL)
        return false;
    memset(rows, 0, sizeof(rows));
    rowCount = vm_net_mock_scene_battle_monster_admin_list(
        scene, rows, VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX);
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (rows[i].entryId == entryId)
        {
            *rowOut = rows[i];
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_scene_battle_monster_reference_monster_get(
    u32 monsterId, vm_net_mock_monster_admin_row *rowOut)
{
    vm_net_mock_monster_admin_row rows[VM_NET_MOCK_MONSTER_CATALOG_MAX];
    u32 rowCount = 0;

    if (rowOut != NULL)
        memset(rowOut, 0, sizeof(*rowOut));
    if (monsterId == 0 || monsterId > 0xffffu || rowOut == NULL)
        return false;
    memset(rows, 0, sizeof(rows));
    rowCount = vm_net_mock_monster_admin_list(
        rows, VM_NET_MOCK_MONSTER_CATALOG_MAX);
    if (rowCount > VM_NET_MOCK_MONSTER_CATALOG_MAX)
        return false;
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (rows[i].enemyId == monsterId)
        {
            *rowOut = rows[i];
            return true;
        }
    }
    return false;
}

/* A newly configured scene battle monster owns a new stable identity.  The
 * source picker is only a template: persisting its ID directly would make a
 * later Monster Management edit alter the original monster everywhere else. */
static bool vm_net_mock_scene_battle_monster_admin_choose_monster_id(
    u32 maximumStoredId, u32 *monsterIdOut, const char **errorOut)
{
    u32 candidate = VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN;

    if (monsterIdOut != NULL)
        *monsterIdOut = 0;
    if (errorOut != NULL)
        *errorOut = "无法分配新的场景战斗怪 ID";
    if (monsterIdOut == NULL)
        return false;
    if (maximumStoredId >= VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN)
        candidate = maximumStoredId + 1u;
    while (candidate <= 0xffffu)
    {
        if (vm_net_mock_monster_catalog_index(candidate) < 0)
        {
            *monsterIdOut = candidate;
            if (errorOut != NULL)
                *errorOut = "ok";
            return true;
        }
        ++candidate;
    }
    if (errorOut != NULL)
        *errorOut = "可分配的场景战斗怪 ID 已用尽";
    return false;
}

static bool vm_net_mock_scene_battle_monster_admin_allocate_monster_id(
    u32 *monsterIdOut, const char **errorOut)
{
    vm_net_mock_scene_battle_monster_id_max_context context;
    char query[512];

    if (monsterIdOut != NULL)
        *monsterIdOut = 0;
    if (errorOut != NULL)
        *errorOut = "无法分配新的场景战斗怪 ID";
    if (monsterIdOut == NULL || !vm_net_mock_monster_db_load() ||
        !vm_net_mock_scene_battle_monster_schema_ensure())
    {
        if (errorOut != NULL)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(
        query, sizeof(query),
        "SELECT GREATEST("
        "COALESCE((SELECT MAX(monster_id) FROM server_scene_battle_monsters "
        "WHERE monster_id>=%u),%u),"
        "COALESCE((SELECT MAX(monster_id) FROM server_monsters "
        "WHERE monster_id>=%u),%u))",
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN,
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN - 1u,
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN,
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_CUSTOM_ID_MIN - 1u);
    if (!vm_mysql_query(query, vm_net_mock_scene_battle_monster_id_max_row,
                        &context) ||
        context.invalid || !context.found)
    {
        if (errorOut != NULL)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    return vm_net_mock_scene_battle_monster_admin_choose_monster_id(
        context.maximum, monsterIdOut, errorOut);
}

static bool vm_net_mock_scene_battle_monster_admin_prepare_reference_clone(
    const vm_net_mock_monster_admin_row *source, u32 monsterId,
    vm_net_mock_monster_admin_row *cloneOut, const char **errorOut)
{
    if (source == NULL || source->enemyId == 0 || monsterId == 0 ||
        monsterId > 0xffffu || source->enemyId == monsterId || cloneOut == NULL)
    {
        if (errorOut != NULL)
            *errorOut = "参考怪物或新怪物 ID 无效";
        return false;
    }
    *cloneOut = *source;
    cloneOut->enemyId = monsterId;
    cloneOut->overridden = false;
    if (errorOut != NULL)
        *errorOut = "ok";
    return true;
}

static bool vm_net_mock_scene_battle_monster_admin_clone_reference(
    const vm_net_mock_monster_admin_row *source, u32 monsterId,
    const char **errorOut)
{
    vm_net_mock_monster_admin_row clone;

    memset(&clone, 0, sizeof(clone));
    if (!vm_net_mock_scene_battle_monster_admin_prepare_reference_clone(
            source, monsterId, &clone, errorOut))
    {
        return false;
    }
    return vm_net_mock_monster_admin_save(&clone, errorOut);
}

static bool vm_net_mock_scene_battle_monster_deployed_source_matches(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCount);
static bool vm_net_mock_scene_battle_monster_admin_is_deployed(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCount, bool *deployedOut);
static bool vm_net_mock_scene_battle_monster_read_base_raw(
    const char *scene, u8 *raw, u32 rawCap, u32 *rawLenOut);
static bool vm_net_mock_scene_battle_monster_live_capacity_safe(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCount);

/* A direct NPC challenge targets a native SCE kind-3 node in the NPC's
 * current scene.  That identity is deliberately distinct from the general
 * monster catalog: a freshly configured scene battle monster is a valid
 * configuration target before the administrator publishes its SCE update,
 * while it cannot yet be used as an arbitrary 4/10 monster-template fight.
 *
 * Keeping this lookup at the scene-battle configuration layer breaks the old
 * circular dependency where dynamic-NPC loading first asked the catalog to
 * discover a node that only becomes visible after the dynamic NPC itself was
 * accepted.  It does not claim that the client has installed the node; the
 * direct 4/1 handler still verifies the observed live SCE node before it
 * emits the 4/5 scene-battle contract. */
static bool vm_net_mock_scene_battle_monster_configured_target_exists(
    const char *scene, u32 monsterId)
{
    vm_net_mock_scene_battle_monster_admin_row
        rows[VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX];
    u32 rowCount = 0;

    if (scene == NULL || monsterId == 0 || monsterId > 0xffffu ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return false;
    }
    memset(rows, 0, sizeof(rows));
    rowCount = vm_net_mock_scene_battle_monster_admin_list(
        scene, rows, VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX);
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (rows[i].enabled && rows[i].monsterId == monsterId)
            return true;
    }
    return false;
}

/* A teleport target must reference the exact scene resource that the client
 * will load.  Check both the durable enabled draft and the deployed SCE
 * payload so a stale/unpublished row cannot be advertised as a live monster. */
static bool vm_net_mock_scene_battle_monster_target_ready(
    const char *scene, u32 monsterId)
{
    vm_net_mock_scene_battle_monster_admin_row rows[
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX];
    u32 rowCount = 0;
    bool found = false;
    bool deployed = false;

    if (scene == NULL || monsterId == 0 || monsterId > 0xffffu ||
        !vm_net_mock_scene_name_is_safe(scene))
        return false;
    memset(rows, 0, sizeof(rows));
    rowCount = vm_net_mock_scene_battle_monster_admin_list(
        scene, rows, VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX);
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (rows[i].enabled && rows[i].monsterId == monsterId)
        {
            found = true;
            break;
        }
    }
    return found && vm_net_mock_scene_battle_monster_admin_is_deployed(
                        scene, rows, rowCount, &deployed) && deployed &&
           vm_net_mock_scene_battle_monster_live_capacity_safe(
               scene, rows, rowCount);
}

static bool vm_net_mock_npc_instance_challenge_target_is_configured(
    const char *ownerScene, const vm_net_mock_scene_npcinfo_seed *seed)
{
    if (seed == NULL || seed->challengeEnemyId == 0)
        return true;
    if (seed->instanceScene[0] == 0)
    {
        return vm_net_mock_scene_battle_monster_configured_target_exists(
            ownerScene, seed->challengeEnemyId);
    }
    return vm_net_mock_monster_enemy_id_known(seed->challengeEnemyId);
}

static bool vm_net_mock_scene_battle_monster_row_validate(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *row)
{
    vm_net_mock_scene_battle_monster_admin_row expanded;
    u16 nativeVisualHint = 0;

    if (scene == NULL || row == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_resource_exists(scene) || row->monsterId == 0 ||
        row->x == 0 || row->y == 0 || row->displayName[0] == 0 ||
        strlen(row->displayName) >= 30 || row->actorResource[0] == 0 ||
        strlen(row->actorResource) >= 64 || row->effectResource[0] == 0 ||
        strlen(row->effectResource) >= 64 ||
        !vm_net_mock_scene_battle_monster_body_resource_is_supported(
            row->actorResource) ||
        !vm_net_mock_scene_battle_monster_body_visual_hint(
            row->actorResource, &nativeVisualHint) ||
        !vm_net_mock_str_ends_with(row->effectResource, ".actor") ||
        !vm_net_mock_scene_battle_monster_effect_resource_is_supported(
            row->effectResource) ||
        row->quantity == 0 ||
        row->quantity > VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX ||
        !vm_net_mock_open_server_data_resource(row->actorResource, ".actor",
                                                NULL, NULL, 0) ||
        !vm_net_mock_open_server_data_resource(row->effectResource, ".actor",
                                                NULL, NULL, 0))
    {
        return false;
    }
    for (u32 ordinal = 0; ordinal < row->quantity; ++ordinal)
    {
        if (!vm_net_mock_scene_battle_monster_expanded_row(
                row, ordinal, &expanded))
            return false;
    }
    return true;
}

static bool vm_net_mock_scene_battle_monster_admin_entry_exists(
    const char *scene, u32 entryId, bool *foundOut)
{
    vm_net_mock_scene_battle_monster_count_context context;
    char sceneHex[129];
    char query[384];

    if (foundOut)
        *foundOut = false;
    if (scene == NULL || entryId == 0 ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM server_scene_battle_monsters "
             "WHERE scene=X'%s' AND entry_id=%u", sceneHex, entryId);
    if (!vm_mysql_query(query, vm_net_mock_scene_battle_monster_count_row,
                        &context) || context.invalid || !context.found)
    {
        return false;
    }
    if (foundOut)
        *foundOut = context.count == 1;
    return true;
}

static bool vm_net_mock_scene_battle_monster_admin_save(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *row,
    const char **errorOut)
{
    char sceneHex[129];
    char nameHex[sizeof(row->displayName) * 2 + 1];
    char actorHex[sizeof(row->actorResource) * 2 + 1];
    char effectHex[sizeof(row->effectResource) * 2 + 1];
    char query[1280];
    bool existing = false;
    u32 sceneCount = 0;

    if (errorOut)
        *errorOut = "场景战斗怪配置无效";
    if (row != NULL &&
        !vm_net_mock_scene_battle_monster_body_resource_is_supported(
            row->actorResource))
    {
        if (errorOut)
            *errorOut = "本体 Actor 不能使用退场火团特效；请选择原生场景战斗本体 Actor";
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_schema_ensure() ||
        !vm_net_mock_scene_battle_monster_row_validate(scene, row) ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0 ||
        vm_mysql_hex_encode(row->displayName, strlen(row->displayName),
                            nameHex, sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->actorResource, strlen(row->actorResource),
                            actorHex, sizeof(actorHex)) == 0 ||
        vm_mysql_hex_encode(row->effectResource, strlen(row->effectResource),
                            effectHex, sizeof(effectHex)) == 0)
    {
        return false;
    }
    if (row->entryId != 0 &&
        (!vm_net_mock_scene_battle_monster_admin_entry_exists(
             scene, row->entryId, &existing) || !existing))
    {
        if (errorOut)
            *errorOut = "该场景战斗怪已不存在或页面已过期";
        return false;
    }
    if (row->entryId == 0 &&
        (!vm_net_mock_scene_battle_monster_admin_count_scene(scene,
                                                              &sceneCount) ||
         sceneCount >= VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX))
    {
        if (errorOut)
            *errorOut = "该场景的战斗怪草稿数量已达上限";
        return false;
    }
    if (row->entryId == 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_scene_battle_monsters("
                 "scene,monster_id,pos_x,pos_y,quantity,display_name,actor_resource,effect_resource,visual_hint,enabled) "
                 "VALUES(X'%s',%u,%u,%u,%u,X'%s',X'%s',X'%s',%u,%u)",
                 sceneHex, row->monsterId, row->x, row->y, row->quantity,
                 nameHex, actorHex, effectHex,
                 row->visualHint, row->enabled ? 1u : 0u);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_scene_battle_monsters SET monster_id=%u,pos_x=%u,pos_y=%u,quantity=%u,"
                 "display_name=X'%s',actor_resource=X'%s',effect_resource=X'%s',"
                 "visual_hint=%u,enabled=%u "
                 "WHERE scene=X'%s' AND entry_id=%u",
                 row->monsterId, row->x, row->y, row->quantity, nameHex,
                 actorHex, effectHex,
                 row->visualHint, row->enabled ? 1u : 0u, sceneHex,
                 row->entryId);
    }
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
        {
            const char *mysqlError = vm_mysql_last_error();

            /* The multi-row contract deliberately deduplicates only an
             * identical placement.  Surface that specific configuration
             * error instead of making the operator infer it from MySQL's
             * index name; any other database error remains observable. */
            if (row->entryId == 0 && mysqlError != NULL &&
                strstr(mysqlError, "uq_scene_battle_monster_pos") != NULL)
            {
                *errorOut = "同一场景中相同怪物 ID 不能占用相同坐标；请调整坐标或编辑已有草稿";
            }
            else
            {
                *errorOut = mysqlError;
            }
        }
        return false;
    }
    /* The draft table is an edit-time source for Monster Management.  Refresh
     * it immediately so the redirect after save sees a newly assigned ID,
     * rather than waiting for an unrelated SCE deployment or server restart. */
    vm_net_mock_monster_catalog_invalidate();
    if (errorOut)
        *errorOut = "ok";
    return true;
}

static bool vm_net_mock_scene_battle_monster_admin_delete(
    const char *scene, u32 entryId, const char **errorOut)
{
    char sceneHex[129];
    char query[384];
    bool existing = false;

    if (errorOut)
        *errorOut = "场景战斗怪不存在";
    if (!vm_net_mock_scene_battle_monster_schema_ensure() || scene == NULL ||
        entryId == 0 || !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_battle_monster_admin_entry_exists(
            scene, entryId, &existing) || !existing ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_scene_battle_monsters WHERE scene=X'%s' AND entry_id=%u",
             sceneHex, entryId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    vm_net_mock_monster_catalog_invalidate();
    if (errorOut)
        *errorOut = "ok";
    return true;
}

/* New identities are reserved by inserting the draft first so Monster
 * Management can persist its independent profile.  If copying the selected
 * template fails afterwards, remove that exact new ID rather than leaving a
 * half-created scene configuration behind. */
static bool vm_net_mock_scene_battle_monster_admin_delete_generated(
    const char *scene, u32 monsterId)
{
    char sceneHex[129];
    char query[384];

    if (scene == NULL || monsterId == 0 || monsterId > 0xffffu ||
        !vm_net_mock_scene_battle_monster_schema_ensure() ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_scene_battle_monsters "
             "WHERE scene=X'%s' AND monster_id=%u",
             sceneHex, monsterId);
    if (!vm_mysql_exec(query))
        return false;
    vm_net_mock_monster_catalog_invalidate();
    return true;
}

static u32 vm_net_mock_scene_battle_monster_fingerprint(
    const vm_net_mock_scene_battle_monster_admin_row *rows, u32 rowCount)
{
    u32 hash = 2166136261u;

    for (u32 i = 0; rows != NULL && i < rowCount; ++i)
    {
        const vm_net_mock_scene_battle_monster_admin_row *row = &rows[i];
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)&row->entryId,
                                             sizeof(row->entryId));
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)&row->monsterId,
                                             sizeof(row->monsterId));
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)&row->x,
                                             sizeof(row->x));
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)&row->y,
                                             sizeof(row->y));
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)&row->quantity,
                                             sizeof(row->quantity));
        hash = vm_net_mock_update_hash_bytes(hash,
                                             (const u8 *)row->displayName,
                                             (u32)strlen(row->displayName));
        hash = vm_net_mock_update_hash_bytes(hash,
                                             (const u8 *)row->actorResource,
                                             (u32)strlen(row->actorResource));
        hash = vm_net_mock_update_hash_bytes(hash,
                                             (const u8 *)row->effectResource,
                                             (u32)strlen(row->effectResource));
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)&row->visualHint,
                                             sizeof(row->visualHint));
        hash = vm_net_mock_update_hash_bytes(hash, (const u8 *)&row->enabled,
                                             sizeof(row->enabled));
    }
    return hash == 0 ? 1u : hash;
}

static bool vm_net_mock_scene_battle_monster_deployment_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_scene_battle_monster_deployment_context *context =
        (vm_net_mock_scene_battle_monster_deployment_context *)contextValue;

    if (context == NULL || columnCount != 2 || context->found ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0],
                                 &context->fingerprint) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1],
                                 &context->configuredCount) ||
        context->configuredCount >
            VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_scene_battle_monster_admin_is_deployed(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCount, bool *deployedOut)
{
    vm_net_mock_scene_battle_monster_deployment_context context;
    char sceneHex[129];
    char query[384];
    u32 fingerprint = vm_net_mock_scene_battle_monster_fingerprint(rows,
                                                                     rowCount);
    u32 enabledCount = 0;

    if (deployedOut)
        *deployedOut = false;
    if (!vm_net_mock_scene_battle_monster_schema_ensure() || scene == NULL ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    enabledCount = vm_net_mock_scene_battle_monster_enabled_node_count(
        rows, rowCount);
    if (enabledCount == 0xffffffffu)
        return false;
    snprintf(query, sizeof(query),
             "SELECT config_fingerprint,configured_count "
             "FROM server_scene_battle_monster_deployments "
             "WHERE scene=X'%s'", sceneHex);
    if (!vm_mysql_query(query, vm_net_mock_scene_battle_monster_deployment_row,
                        &context) || context.invalid)
    {
        return false;
    }
    if (deployedOut)
    {
        bool fingerprintMatches = context.found &&
                                  context.fingerprint == fingerprint &&
                                  context.configuredCount == enabledCount;
        bool sourceMatches = fingerprintMatches &&
                             vm_net_mock_scene_battle_monster_deployed_source_matches(
                                 scene, rows, rowCount);

        *deployedOut = sourceMatches;
        if (fingerprintMatches && !sourceMatches)
        {
            printf("[warn][mock-admin] scene_battle_monster_deployment_stale "
                   "scene=%s reason=published-sce-does-not-match-current-"
                   "kind3-record-contract action=require-explicit-redeploy\n",
                   scene);
        }
    }
    return true;
}

static bool vm_net_mock_scene_battle_monster_source_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_scene_battle_monster_source_context *context =
        (vm_net_mock_scene_battle_monster_source_context *)contextValue;
    size_t decodedLen = 0;

    if (context == NULL || columnCount != 1 || context->found ||
        values == NULL || lengths == NULL || lengths[0] == 0 ||
        (lengths[0] & 1u) != 0 ||
        !vm_mysql_hex_decode(values[0], lengths[0], context->raw,
                             context->rawCap, &decodedLen) ||
        decodedLen == 0 || decodedLen > context->rawCap)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->rawLen = (u32)decodedLen;
    context->found = true;
    return true;
}

static bool vm_net_mock_scene_battle_monster_read_source_raw(
    const char *scene, u8 *raw, u32 rawCap, u32 *rawLenOut,
    char *pathOut, size_t pathOutCap)
{
    char path[1200];
    u32 rawLen = 0;

    if (rawLenOut)
        *rawLenOut = 0;
    if (pathOut && pathOutCap != 0)
        pathOut[0] = 0;
    if (scene == NULL || raw == NULL || rawCap == 0 ||
        !vm_net_mock_open_server_scene_resource(scene, NULL, path,
                                                 sizeof(path)))
    {
        return false;
    }
    rawLen = vm_net_mock_load_response_file(path, raw, rawCap);
    if (rawLen == 0)
        return false;
    if (pathOut && pathOutCap != 0)
        snprintf(pathOut, pathOutCap, "%s", path);
    if (rawLenOut)
        *rawLenOut = rawLen;
    return true;
}

/* `vm_net_mock_load_scene_resource()` accepts direct SCE2 and the normal
 * four-byte wrapper.  Type 1 is a literal payload; type 2 is the only LZSS
 * stream.  Deployment needs exactly that client resource contract, but starts
 * from the captured raw base rather than the mutable published file. */
static bool vm_net_mock_scene_battle_monster_decode_raw_sce(
    const u8 *raw, u32 rawLen, u8 *decoded, u32 decodedCap,
    u32 *decodedLenOut)
{
    u32 decodedLen = 0;
    u32 declaredLen = 0;

    if (decodedLenOut)
        *decodedLenOut = 0;
    if (raw == NULL || rawLen == 0 || decoded == NULL || decodedCap == 0)
        return false;
    /* A normal resource wrapper contains a compressed literal run.  That run
     * can itself contain the bytes "SCE2" (and the 00 蓬莱仙岛_02 snapshot
     * does), so testing `scene_payload_start(raw)` first misclassifies the
     * wrapper as a direct scene.  Recognize a structurally valid wrapper
     * before considering an unwrapped SCE2 payload. */
    if (rawLen > 4)
    {
        declaredLen = vm_net_mock_read_le16_at(raw, 0) |
                      ((u32)vm_net_mock_read_le16_at(raw, 2) << 16);
        if (declaredLen != 0 && declaredLen <= rawLen - 4 && raw[4] == 2)
        {
            decodedLen = vm_net_mock_decode_lzss_resource_stream(
                raw + 4, declaredLen, decoded, decodedCap);
        }
        else if (declaredLen > 1 && declaredLen <= rawLen - 4 && raw[4] == 1)
        {
            decodedLen = declaredLen - 1u;
            if (decodedLen > decodedCap)
                return false;
            memcpy(decoded, raw + 5, decodedLen);
        }
        else if (vm_net_mock_scene_payload_start(raw, rawLen) != 0)
        {
            if (rawLen > decodedCap)
                return false;
            memcpy(decoded, raw, rawLen);
            decodedLen = rawLen;
        }
        else
        {
            decodedLen = vm_net_mock_decode_lzss_resource_stream(raw, rawLen,
                                                                    decoded,
                                                                    decodedCap);
        }
    }
    if (decodedLen == 0 || vm_net_mock_scene_payload_start(decoded,
                                                            decodedLen) == 0)
    {
        return false;
    }
    if (decodedLenOut)
        *decodedLenOut = decodedLen;
    return true;
}

static bool vm_net_mock_scene_battle_monster_native_body_add(
    const vm_net_mock_sce_combat_spawn *spawn, const char *scene)
{
    vm_net_mock_scene_battle_monster_native_body *entry = NULL;

    if (spawn == NULL || scene == NULL || spawn->visualHint == 0 ||
        !vm_net_mock_str_ends_with(spawn->actorResource, ".actor") ||
        vm_net_mock_scene_battle_monster_effect_resource_is_supported(
            spawn->actorResource))
    {
        return false;
    }
    for (u32 i = 0; i < g_vm_net_mock_scene_battle_monster_native_body_count;
         ++i)
    {
        if (strcmp(g_vm_net_mock_scene_battle_monster_native_bodies[i]
                       .resource,
                   spawn->actorResource) == 0)
        {
            if (g_vm_net_mock_scene_battle_monster_native_bodies[i]
                    .visualHint != spawn->visualHint)
            {
                g_vm_net_mock_scene_battle_monster_native_bodies[i]
                    .visualHintAmbiguous = true;
            }
            return true;
        }
    }
    if (g_vm_net_mock_scene_battle_monster_native_body_count >=
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_NATIVE_BODY_MAX)
    {
        return false;
    }
    entry = &g_vm_net_mock_scene_battle_monster_native_bodies[
        g_vm_net_mock_scene_battle_monster_native_body_count++];
    snprintf(entry->resource, sizeof(entry->resource), "%s",
             spawn->actorResource);
    entry->visualHint = spawn->visualHint;
    snprintf(entry->firstScene, sizeof(entry->firstScene), "%s", scene);
    return true;
}

/* Read only the immutable configured resource root here.  The ordinary scene
 * loader deliberately prefers the per-database deployment overlay, which
 * would otherwise make an invalid generated row look like shipped evidence. */
static void vm_net_mock_scene_battle_monster_native_bodies_ensure_loaded(void)
{
    vm_net_mock_monster_catalog_scene_file
        scenes[VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX];
    u32 sceneCount = 0;
    u32 parsedSpawnCount = 0;
    u32 skippedEffectCount = 0;
    u32 rejectedResourceCount = 0;

    if (g_vm_net_mock_scene_battle_monster_native_bodies_loaded ||
        g_vm_net_mock_scene_battle_monster_native_bodies_loading)
    {
        return;
    }
    g_vm_net_mock_scene_battle_monster_native_bodies_loading = true;
    sceneCount = vm_net_mock_monster_catalog_collect_scene_files(
        scenes, VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX);
    for (u32 sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex)
    {
        u8 raw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
        u8 payload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
        u32 rawLen = 0;
        u32 payloadLen = 0;

        if (!vm_net_mock_scene_battle_monster_read_base_raw(
                scenes[sceneIndex].name, raw, sizeof(raw), &rawLen) ||
            !vm_net_mock_scene_battle_monster_decode_raw_sce(
                raw, rawLen, payload, sizeof(payload), &payloadLen))
        {
            continue;
        }
        for (u32 combatOrdinal = 0; combatOrdinal < 256u; ++combatOrdinal)
        {
            vm_net_mock_sce_combat_spawn spawn;

            if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                    payload, payloadLen, combatOrdinal, &spawn, NULL))
            {
                break;
            }
            ++parsedSpawnCount;
            /* Some shipped kind-3 records are fireball-only visual markers.
             * They prove that the parser accepts the envelope, not that the
             * field-17 descriptor is a collision-capable monster body. */
            if (vm_net_mock_scene_battle_monster_effect_resource_is_supported(
                    spawn.actorResource))
            {
                ++skippedEffectCount;
                continue;
            }
            if (!vm_net_mock_scene_battle_monster_native_body_add(
                    &spawn, scenes[sceneIndex].name))
            {
                ++rejectedResourceCount;
            }
        }
    }
    {
        u32 ambiguousHintCount = 0;

        for (u32 i = 0;
             i < g_vm_net_mock_scene_battle_monster_native_body_count; ++i)
        {
            if (g_vm_net_mock_scene_battle_monster_native_bodies[i]
                    .visualHintAmbiguous)
            {
                ++ambiguousHintCount;
            }
        }
        g_vm_net_mock_scene_battle_monster_native_bodies_loaded =
        g_vm_net_mock_scene_battle_monster_native_body_count != 0 &&
        rejectedResourceCount == 0;
        g_vm_net_mock_scene_battle_monster_native_bodies_loading = false;
        printf("[info][network] mock_scene_battle_native_body_catalog scenes=%u "
               "spawns=%u bodies=%u ambiguous_hint=%u skipped_effect=%u "
               "rejected=%u source=base-SCE2-kind3\n",
               sceneCount, parsedSpawnCount,
               g_vm_net_mock_scene_battle_monster_native_body_count,
               ambiguousHintCount, skippedEffectCount, rejectedResourceCount);
    }
}

static bool vm_net_mock_scene_battle_monster_body_resource_is_supported(
    const char *resource)
{
    if (resource == NULL || !vm_net_mock_str_ends_with(resource, ".actor") ||
        vm_net_mock_scene_battle_monster_effect_resource_is_supported(resource))
    {
        return false;
    }
    vm_net_mock_scene_battle_monster_native_bodies_ensure_loaded();
    if (!g_vm_net_mock_scene_battle_monster_native_bodies_loaded)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_scene_battle_monster_native_body_count;
         ++i)
    {
        if (strcmp(g_vm_net_mock_scene_battle_monster_native_bodies[i]
                       .resource,
                   resource) == 0)
        {
            return true;
        }
    }
    return false;
}

/* Instance entry occurs before the client has loaded the destination SCE, so
 * it cannot use vm_net_mock_select_sce_combat_spawn(): that helper correctly
 * requires a live, current-scene node for a 4/5 battle response.  This
 * resource-only check proves that the exact SCE which the forthcoming 30/1
 * will name contains the configured kind-3 spawn, without claiming that the
 * client has already created its scene-node row. */
static bool vm_net_mock_sce_combat_spawn_resource_has(const char *scene,
                                                       u32 actorId)
{
    u8 data[8192];
    u32 len = 0;
    u32 propNodeCount = 0;

    if (scene == NULL || scene[0] == 0 || actorId == 0 ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return false;
    }
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    if (len == 0 ||
        !vm_net_mock_parse_sce2_client_prefix(data, len, &propNodeCount,
                                              NULL))
    {
        return false;
    }
    for (u32 combatOrdinal = 0; combatOrdinal < 256u; ++combatOrdinal)
    {
        vm_net_mock_sce_combat_spawn spawn;
        u32 nodeOrdinal = 0;
        u32 sceneNodeIndex = 0;

        if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                data, len, combatOrdinal, &spawn, &nodeOrdinal))
        {
            break;
        }
        if (spawn.actorId != actorId)
            continue;
        sceneNodeIndex = propNodeCount + nodeOrdinal;
        if (sceneNodeIndex >= 25)
        {
            printf("[error][network] mock_npc_instance_entry_target scene=%s actor=%u runtime_index=%u action=reject-out-of-range evidence=mmBattle:0x66CC scene-node-table[25]\n",
                   scene, actorId, sceneNodeIndex);
            return false;
        }
        printf("[info][network] mock_npc_instance_entry_target scene=%s actor=%u runtime_index=%u prop_nodes=%u entity_node_ordinal=%u combat_ordinal=%u action=resource-proven-before-scene-enter evidence=SCE2-counted-entity-list+JianghuOL.CBE:0x010396D6\n",
               scene, actorId, sceneNodeIndex, propNodeCount, nodeOrdinal,
               combatOrdinal);
        return true;
    }
    return false;
}

/* field 16 is a native node-class selector, not a player-facing strength
 * toggle.  The current Linan e_tiger deployment used 5 while the shipped
 * e_tiger record uses 17; that produced a visible generic node but never
 * entered TriggerAutoBattle.  Preserve the body Actor's immutable native
 * value, independently of the administrator's monster ID. */
static bool vm_net_mock_scene_battle_monster_body_visual_hint(
    const char *resource, u16 *hintOut)
{
    if (hintOut != NULL)
        *hintOut = 0;
    if (resource == NULL || hintOut == NULL)
        return false;
    vm_net_mock_scene_battle_monster_native_bodies_ensure_loaded();
    if (!g_vm_net_mock_scene_battle_monster_native_bodies_loaded)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_scene_battle_monster_native_body_count;
         ++i)
    {
        const vm_net_mock_scene_battle_monster_native_body *entry =
            &g_vm_net_mock_scene_battle_monster_native_bodies[i];

        if (strcmp(entry->resource, resource) != 0)
            continue;
        if (entry->visualHintAmbiguous || entry->visualHint == 0)
            return false;
        *hintOut = entry->visualHint;
        return true;
    }
    return false;
}


static bool vm_net_mock_scene_battle_monster_load_base_raw(
    const char *scene, u8 *raw, u32 rawCap, u32 *rawLenOut,
    const char **sourceOut)
{
    vm_net_mock_scene_battle_monster_source_context context;
    char sceneHex[129];
    char query[512];
    char *rawHex = NULL;
    u32 currentLen = 0;

    if (rawLenOut)
        *rawLenOut = 0;
    if (sourceOut)
        *sourceOut = "unresolved";
    if (scene == NULL || raw == NULL || rawCap == 0 ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    context.raw = raw;
    context.rawCap = rawCap;
    snprintf(query, sizeof(query),
             "SELECT HEX(base_resource) FROM server_scene_battle_monster_sources "
             "WHERE scene=X'%s'", sceneHex);
    if (!vm_mysql_query(query, vm_net_mock_scene_battle_monster_source_row,
                        &context) || context.invalid)
    {
        return false;
    }
    if (context.found)
    {
        if (rawLenOut)
            *rawLenOut = context.rawLen;
        if (sourceOut)
            *sourceOut = "mysql-captured-base";
        return true;
    }
    if (!vm_net_mock_scene_battle_monster_read_base_raw(scene, raw, rawCap,
                                                         &currentLen) ||
        currentLen == 0 || currentLen > rawCap)
    {
        return false;
    }
    rawHex = calloc((size_t)currentLen * 2u + 1u, 1u);
    if (rawHex == NULL ||
        vm_mysql_hex_encode(raw, currentLen, rawHex,
                            (size_t)currentLen * 2u + 1u) == 0)
    {
        free(rawHex);
        return false;
    }
    {
        size_t queryCap = strlen(rawHex) + strlen(sceneHex) + 160u;
        char *insertQuery = calloc(queryCap, 1u);
        bool inserted = false;
        if (insertQuery != NULL)
        {
            snprintf(insertQuery, queryCap,
                     "INSERT IGNORE INTO server_scene_battle_monster_sources(scene,base_resource) "
                     "VALUES(X'%s',X'%s')", sceneHex, rawHex);
            inserted = vm_mysql_exec(insertQuery);
        }
        free(insertQuery);
        free(rawHex);
        rawHex = NULL;
        if (!inserted)
            return false;
    }
    /* Re-read after INSERT IGNORE so a concurrent deploy always uses one
     * durable base byte stream, not whichever source it happened to see. */
    memset(&context, 0, sizeof(context));
    context.raw = raw;
    context.rawCap = rawCap;
    snprintf(query, sizeof(query),
             "SELECT HEX(base_resource) FROM server_scene_battle_monster_sources "
             "WHERE scene=X'%s'", sceneHex);
    if (!vm_mysql_query(query, vm_net_mock_scene_battle_monster_source_row,
                        &context) || context.invalid || !context.found)
    {
        return false;
    }
    if (rawLenOut)
        *rawLenOut = context.rawLen;
    if (sourceOut)
        *sourceOut = "mysql-captured-base";
    return true;
}

static bool vm_net_mock_scene_battle_monster_append_record(
    u8 *payload, u32 payloadCap, u32 *pos,
    const vm_net_mock_scene_battle_monster_admin_row *row)
{
    size_t nameLen = 0;
    size_t actorLen = 0;
    size_t effectLen = 0;
    u16 nativeVisualHint = 0;

    if (payload == NULL || pos == NULL || row == NULL ||
        row->monsterId == 0 || row->x == 0 || row->y == 0 ||
        !vm_net_mock_scene_battle_monster_body_visual_hint(
            row->actorResource, &nativeVisualHint))
    {
        return false;
    }
    nameLen = strlen(row->displayName);
    actorLen = strlen(row->actorResource);
    effectLen = strlen(row->effectResource);
    if (nameLen == 0 || nameLen >= 30 || actorLen == 0 || actorLen >= 64 ||
        effectLen == 0 || effectLen >= 64 || nameLen > 0xffu ||
        actorLen > 0xffu || effectLen > 0xffu ||
        *pos > payloadCap || payloadCap - *pos <
            35u + (u32)nameLen + (u32)actorLen + (u32)effectLen)
    {
        return false;
    }
    /* Exact record grammar recovered directly from shipped SCE2 kind-3
     * records: kind,x,y, meta(5,1,field14,id), string15(name),
     * scalar16(hint), string17(actor), then the unnumbered child effect
     * Actor `u16 kind=3,u16 kind=3,u8 len`.  Both little-endian kind words
     * are part of the native stream; omitting the second one was the first
     * malformed byte in the release that crashed during SCE installation. */
    if (!vm_net_mock_put_le16(payload, payloadCap, pos, 3) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, row->x) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, row->y) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 5) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 1) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 0x0e) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, row->monsterId) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 3) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 0x0f) ||
        *pos >= payloadCap)
    {
        return false;
    }
    payload[(*pos)++] = (u8)nameLen;
    memcpy(payload + *pos, row->displayName, nameLen);
    *pos += (u32)nameLen;
    if (!vm_net_mock_put_le16(payload, payloadCap, pos, 1) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 0x10) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, nativeVisualHint) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 3) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 0x11) ||
        *pos >= payloadCap)
    {
        return false;
    }
    payload[(*pos)++] = (u8)actorLen;
    memcpy(payload + *pos, row->actorResource, actorLen);
    *pos += (u32)actorLen;
    if (!vm_net_mock_put_le16(payload, payloadCap, pos, 3) ||
        !vm_net_mock_put_le16(payload, payloadCap, pos, 3) ||
        *pos >= payloadCap)
    {
        return false;
    }
    payload[(*pos)++] = (u8)effectLen;
    memcpy(payload + *pos, row->effectResource, effectLen);
    *pos += (u32)effectLen;
    return true;
}

static bool vm_net_mock_scene_battle_monster_lzss_literal_encode(
    const u8 *payload, u32 payloadLen, u8 *encoded, u32 encodedCap,
    u32 *encodedLenOut)
{
    u32 srcPos = 0;
    u32 dstPos = 9;

    if (encodedLenOut)
        *encodedLenOut = 0;
    if (payload == NULL || payloadLen == 0 || encoded == NULL ||
        encodedCap < 10)
    {
        return false;
    }
    /* The shipped decoder accepts literal runs up to 127 bytes.  Literal-only
     * LZSS is intentional here: it is deterministic, requires no speculative
     * compressor heuristics and is validated by the same decoder that serves
     * every existing SCE resource. */
    while (srcPos < payloadLen)
    {
        u32 count = vm_net_mock_min_u32(127u, payloadLen - srcPos);
        if (count == 0 || dstPos > encodedCap ||
            encodedCap - dstPos < count + 1u)
        {
            return false;
        }
        encoded[dstPos++] = (u8)(0x80u | count);
        memcpy(encoded + dstPos, payload + srcPos, count);
        dstPos += count;
        srcPos += count;
    }
    if (dstPos <= 9 || dstPos - 9 > 0xffffffffu ||
        payloadLen > 0x7fffffffu)
    {
        return false;
    }
    /* Type 2 is the client LZSS container.  Type 1 would make the client use
     * the following compression header as literal SCE bytes, so the SCE2
     * parser never sees the appended scene node. */
    encoded[0] = 2;
    encoded[1] = (u8)((dstPos - 9) >> 24);
    encoded[2] = (u8)((dstPos - 9) >> 16);
    encoded[3] = (u8)((dstPos - 9) >> 8);
    encoded[4] = (u8)(dstPos - 9);
    encoded[5] = (u8)(payloadLen >> 24);
    encoded[6] = (u8)(payloadLen >> 16);
    encoded[7] = (u8)(payloadLen >> 8);
    encoded[8] = (u8)payloadLen;
    if (encodedLenOut)
        *encodedLenOut = dstPos;
    return true;
}

static bool vm_net_mock_scene_battle_monster_write_resource(
    const char *path, const u8 *raw, u32 rawLen, const char **errorOut)
{
    char tempPath[1240];
    FILE *fp = NULL;
    bool writeOk = false;

    if (errorOut)
        *errorOut = "场景资源写入失败";
    if (path == NULL || path[0] == 0 || raw == NULL || rawLen == 0 ||
        snprintf(tempPath, sizeof(tempPath), "%s.scene-battle.tmp", path) >=
            (int)sizeof(tempPath))
    {
        return false;
    }
    fp = fopen(tempPath, "wb");
    if (fp == NULL)
    {
        remove(tempPath);
        if (errorOut)
            *errorOut = "场景资源临时文件写入失败";
        return false;
    }
    writeOk = fwrite(raw, 1, rawLen, fp) == rawLen;
    if (fflush(fp) != 0)
        writeOk = false;
    if (fclose(fp) != 0)
        writeOk = false;
    if (!writeOk)
    {
        remove(tempPath);
        if (errorOut)
            *errorOut = "场景资源临时文件写入失败";
        return false;
    }
#ifdef _WIN32
    /* Windows rename() cannot replace an existing file.  MoveFileEx keeps
     * the previous source in place if replacement itself fails; deleting the
     * original first would make a failed deployment destroy the live scene. */
    if (!MoveFileExA(tempPath, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
    if (rename(tempPath, path) != 0)
#endif
    {
        remove(tempPath);
        if (errorOut)
            *errorOut = "场景资源原子替换失败";
        return false;
    }
    if (errorOut)
        *errorOut = "ok";
    return true;
}

typedef struct
{
    u32 countOffset;
    u32 recordsOffset;
    u32 recordsEnd;
    u32 recordCount;
    u32 nodeRecordCount;
    u32 combatRecordCount;
} vm_net_mock_sce_entity_list;

/* Parse one record from ParseActorFullInfoBlob(0x0100F094):
 *   u16 tag, u16 x, u16 y, u16 property_count,
 *   property_count { u16 token_kind, u16 field_id, token_value }.
 * ReadStreamEntry(0x0100EE1E) proves token kinds 0/1/2/3 consume u8, u16,
 * u32 and a one-byte-length string respectively. */
static bool vm_net_mock_parse_sce_entity_record_at(
    const u8 *data, u32 len, u32 off, u16 *tagOut, u32 *endOut)
{
    u32 pos = off;
    u16 tag = 0;
    u16 propertyCount = 0;

    if (tagOut != NULL)
        *tagOut = 0;
    if (endOut != NULL)
        *endOut = 0;
    if (data == NULL || off + 8u > len)
        return false;

    tag = vm_net_mock_read_le16_at(data, pos);
    propertyCount = vm_net_mock_read_le16_at(data, pos + 6u);
    pos += 8u;
    if (tag == 0 || propertyCount > (len - pos) / 5u)
        return false;

    for (u16 propertyIndex = 0; propertyIndex < propertyCount;
         ++propertyIndex)
    {
        u16 tokenKind = 0;
        u32 valueLen = 0;

        if (pos + 4u > len)
            return false;
        tokenKind = vm_net_mock_read_le16_at(data, pos);
        pos += 4u; /* token kind + field id */
        switch (tokenKind)
        {
        case 0:
            valueLen = 1u;
            break;
        case 1:
            valueLen = 2u;
            break;
        case 2:
            valueLen = 4u;
            break;
        case 3:
            if (pos >= len)
                return false;
            valueLen = 1u + data[pos];
            break;
        default:
            return false;
        }
        if (valueLen > len - pos)
            return false;
        pos += valueLen;
    }

    if (tagOut != NULL)
        *tagOut = tag;
    if (endOut != NULL)
        *endOut = pos;
    return true;
}

/* The client classifies a scene key starting with 'c' as a town/special
 * screen.  That screen never calls TriggerAutoBattle, even if its SCE2 data
 * contains a valid kind-3 scene monster.  For an NPC instance target we can
 * keep the original city resource authoritative and publish a sibling
 * wilderness key with the same payload.  The client then selects the normal
 * collision screen from the key itself; no client state or packet is forged.
 *
 * The child background is equally important.  A city SCE can omit field 18
 * on its named portal, but a wilderness shell needs a b_*.sce descriptor to
 * allocate the battle-background Actor table.  The generated background uses
 * the shipped empty Danxia shell and an exact copy of the city map under a
 * b_*.map key. */
enum
{
    VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_NAME_CAP = 64,
    VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_MAP_RAW_MAX = 8192
};

static bool vm_net_mock_scene_battle_monster_read_base_raw(
    const char *scene, u8 *raw, u32 rawCap, u32 *rawLenOut);

static bool vm_net_mock_scene_battle_monster_city_mirror_name(
    const char *scene, char *mirrorOut, size_t mirrorOutCap)
{
    size_t sceneLen = 0;

    if (mirrorOut != NULL && mirrorOutCap != 0)
        mirrorOut[0] = 0;
    if (scene == NULL || scene[0] != 'c' ||
        !vm_net_mock_str_ends_with(scene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(scene) || mirrorOut == NULL ||
        mirrorOutCap == 0)
    {
        return false;
    }
    sceneLen = strlen(scene);
    /* Replace, rather than prefix, the city discriminator.  The exact scene
     * identity remains recognisable and cannot overflow the client's 0x64
     * byte resource-name buffer. */
    if (sceneLen + 1u > mirrorOutCap)
        return false;
    mirrorOut[0] = 'w';
    memcpy(mirrorOut + 1, scene + 1, sceneLen);
    /* This key does not exist until the enclosing deployment writes it.  At
     * generation time validate only the server-safe resource-key grammar;
     * instance entry later requires the actual published SCE through the
     * resource-only target check below. */
    return vm_net_mock_scene_name_is_download_key(mirrorOut) &&
           vm_net_mock_str_ends_with(mirrorOut, ".sce");
}

/* Resolve the exact resource key sent in an NPC instance 30/1.  The durable
 * configuration remains the administrator-selected scene; for city targets
 * with a selected encounter, the generated mirror is the client-visible
 * resource.  Both the source deployment and the mirror's actual kind-3 row
 * are checked before the normal scene-enter packet is allowed. */
static bool vm_net_mock_scene_battle_monster_instance_entry_scene(
    const char *configuredScene, u32 spawnEnemyId, char *sceneOut,
    size_t sceneOutCap)
{
    if (sceneOut != NULL && sceneOutCap != 0)
        sceneOut[0] = 0;
    if (configuredScene == NULL || configuredScene[0] == 0 ||
        sceneOut == NULL || sceneOutCap == 0 ||
        !vm_net_mock_scene_name_is_safe(configuredScene))
    {
        return false;
    }
    if (spawnEnemyId == 0)
    {
        if (strlen(configuredScene) >= sceneOutCap)
            return false;
        snprintf(sceneOut, sceneOutCap, "%s", configuredScene);
        return true;
    }
    if (!vm_net_mock_scene_battle_monster_target_ready(configuredScene,
                                                        spawnEnemyId))
    {
        return false;
    }
    if (configuredScene[0] != 'c')
    {
        if (strlen(configuredScene) >= sceneOutCap)
            return false;
        snprintf(sceneOut, sceneOutCap, "%s", configuredScene);
        return vm_net_mock_sce_combat_spawn_resource_has(sceneOut,
                                                          spawnEnemyId);
    }
    return vm_net_mock_scene_battle_monster_city_mirror_name(
               configuredScene, sceneOut, sceneOutCap) &&
           vm_net_mock_sce_combat_spawn_resource_has(sceneOut,
                                                      spawnEnemyId);
}

static bool vm_net_mock_scene_battle_monster_city_background_names(
    const char *scene, const char *mapName, char *backgroundOut,
    size_t backgroundOutCap, char *backgroundMapOut,
    size_t backgroundMapOutCap)
{
    int written = 0;

    if (backgroundOut != NULL && backgroundOutCap != 0)
        backgroundOut[0] = 0;
    if (backgroundMapOut != NULL && backgroundMapOutCap != 0)
        backgroundMapOut[0] = 0;
    if (scene == NULL || scene[0] != 'c' || mapName == NULL ||
        !vm_net_mock_str_ends_with(scene, ".sce") ||
        !vm_net_mock_str_ends_with(mapName, ".map") ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        vm_net_mock_scene_name_has_path_separator(mapName) ||
        backgroundOut == NULL || backgroundOutCap == 0 ||
        backgroundMapOut == NULL || backgroundMapOutCap == 0)
    {
        return false;
    }
    written = snprintf(backgroundOut, backgroundOutCap, "b_%s", scene + 1);
    if (written < 0 || (size_t)written >= backgroundOutCap ||
        !vm_net_mock_scene_name_is_download_key(backgroundOut) ||
        !vm_net_mock_str_ends_with(backgroundOut, ".sce"))
    {
        return false;
    }
    written = snprintf(backgroundMapOut, backgroundMapOutCap, "b_%s", mapName);
    return written >= 0 && (size_t)written < backgroundMapOutCap &&
           !vm_net_mock_scene_name_has_path_separator(backgroundMapOut) &&
           vm_net_mock_str_ends_with(backgroundMapOut, ".map");
}

static bool vm_net_mock_scene_battle_monster_payload_map_name(
    const u8 *payload, u32 payloadLen, char *mapNameOut, size_t mapNameOutCap,
    u32 *afterMapOut)
{
    u32 afterMap = 0;
    u8 nameLen = 0;

    if (mapNameOut != NULL && mapNameOutCap != 0)
        mapNameOut[0] = 0;
    if (afterMapOut != NULL)
        *afterMapOut = 0;
    if (payload == NULL || payloadLen < 11 ||
        memcmp(payload, "SCE2", 4) != 0 || mapNameOut == NULL ||
        mapNameOutCap == 0)
    {
        return false;
    }
    nameLen = payload[10];
    afterMap = 11u + (u32)nameLen;
    if (nameLen == 0 || afterMap > payloadLen ||
        (size_t)nameLen >= mapNameOutCap)
    {
        return false;
    }
    memcpy(mapNameOut, payload + 11, nameLen);
    mapNameOut[nameLen] = 0;
    if (!vm_net_mock_str_ends_with(mapNameOut, ".map") ||
        vm_net_mock_scene_name_has_path_separator(mapNameOut))
    {
        return false;
    }
    if (afterMapOut != NULL)
        *afterMapOut = afterMap;
    return true;
}

static bool vm_net_mock_scene_battle_monster_find_empty_background_slot(
    const u8 *payload, u32 payloadLen, u32 *slotOut)
{
    u32 matches = 0;
    u32 slot = 0;

    if (slotOut != NULL)
        *slotOut = 0;
    if (payload == NULL || payloadLen < 16 || slotOut == NULL)
        return false;
    for (u32 off = 0; off + 12u <= payloadLen; ++off)
    {
        vm_net_mock_sce_named_portal portal;
        u32 end = 0;

        if (!vm_net_mock_parse_sce_named_portal_at(payload, payloadLen, off,
                                                    &portal, &end) ||
            portal.backgroundScene[0] != 0 || end <= off || end > payloadLen)
        {
            continue;
        }
        for (u32 token = off; token + 5u <= end; ++token)
        {
            if (vm_net_mock_read_le16_at(payload, token) == 3u &&
                vm_net_mock_read_le16_at(payload, token + 2u) == 0x12u &&
                payload[token + 4u] == 0u)
            {
                slot = token;
                ++matches;
            }
        }
    }
    if (matches != 1u)
        return false;
    *slotOut = slot;
    return true;
}

static bool vm_net_mock_scene_battle_monster_wrap_type2(
    const u8 *payload, u32 payloadLen, u8 *raw, u32 rawCap, u32 *rawLenOut)
{
    u32 encodedLen = 0;

    if (rawLenOut != NULL)
        *rawLenOut = 0;
    if (payload == NULL || payloadLen == 0 || raw == NULL || rawCap < 5u ||
        !vm_net_mock_scene_battle_monster_lzss_literal_encode(
            payload, payloadLen, raw + 4, rawCap - 4u, &encodedLen) ||
        encodedLen > rawCap - 4u)
    {
        return false;
    }
    raw[0] = (u8)encodedLen;
    raw[1] = (u8)(encodedLen >> 8);
    raw[2] = (u8)(encodedLen >> 16);
    raw[3] = (u8)(encodedLen >> 24);
    if (rawLenOut != NULL)
        *rawLenOut = encodedLen + 4u;
    return true;
}

static bool vm_net_mock_scene_battle_monster_build_city_mirror(
    const char *scene, const u8 *sourcePayload, u32 sourcePayloadLen,
    char *mirrorNameOut, size_t mirrorNameOutCap, u8 *mirrorRaw,
    u32 mirrorRawCap, u32 *mirrorRawLenOut, char *backgroundNameOut,
    size_t backgroundNameOutCap, u8 *backgroundRaw, u32 backgroundRawCap,
    u32 *backgroundRawLenOut, char *backgroundMapNameOut,
    size_t backgroundMapNameOutCap, u8 *backgroundMapRaw,
    u32 backgroundMapRawCap, u32 *backgroundMapRawLenOut)
{
    static const char backgroundTemplate[] =
        "b_03\xB5\xA4\xCF\xBC\xC9\xBD.sce"; /* b_03丹霞山.sce */
    u8 templateRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 templatePayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 mirrorPayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 backgroundPayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 verifyPayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    char sourceMapName[VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_NAME_CAP];
    char sourceMapPath[1200];
    char templatePath[1200];
    u32 templateRawLen = 0;
    u32 templatePayloadLen = 0;
    u32 templateAfterMap = 0;
    u32 mirrorPayloadLen = 0;
    u32 backgroundPayloadLen = 0;
    u32 verifyPayloadLen = 0;
    u32 slot = 0;
    u32 sourceAfterMap = 0;
    u32 sourceMapRawLen = 0;
    size_t backgroundNameLen = 0;

    if (mirrorRawLenOut != NULL)
        *mirrorRawLenOut = 0;
    if (backgroundRawLenOut != NULL)
        *backgroundRawLenOut = 0;
    if (backgroundMapRawLenOut != NULL)
        *backgroundMapRawLenOut = 0;
    if (!vm_net_mock_scene_battle_monster_city_mirror_name(
            scene, mirrorNameOut, mirrorNameOutCap) ||
        sourcePayload == NULL || sourcePayloadLen == 0 || mirrorRaw == NULL ||
        backgroundRaw == NULL || backgroundMapRaw == NULL ||
        mirrorRawLenOut == NULL || backgroundRawLenOut == NULL ||
        backgroundMapRawLenOut == NULL)
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=arguments scene=%s\n", scene ? scene : "-");
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_payload_map_name(
            sourcePayload, sourcePayloadLen, sourceMapName,
            sizeof(sourceMapName), &sourceAfterMap) ||
        !vm_net_mock_scene_battle_monster_city_background_names(
            scene, sourceMapName, backgroundNameOut, backgroundNameOutCap,
            backgroundMapNameOut, backgroundMapNameOutCap) ||
        !vm_net_mock_scene_battle_monster_find_empty_background_slot(
            sourcePayload, sourcePayloadLen, &slot))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=source-shape scene=%s\n", scene);
        return false;
    }
    if (!vm_net_mock_open_server_data_resource(
            backgroundTemplate, ".sce", NULL, templatePath,
            sizeof(templatePath)))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=open-background-template scene=%s\n", scene);
        return false;
    }

    /* The background shell is a shipped static descriptor, not an
     * administrator-editable SCE; it is deliberately not read from the
     * captured scene-base store used for the city source. */
    templateRawLen = vm_net_mock_load_response_file(
        templatePath, templateRaw, sizeof(templateRaw));
    if (templateRawLen == 0 ||
        !vm_net_mock_scene_battle_monster_decode_raw_sce(
            templateRaw, templateRawLen, templatePayload,
            sizeof(templatePayload), &templatePayloadLen) ||
        !vm_net_mock_scene_battle_monster_payload_map_name(
            templatePayload, templatePayloadLen, sourceMapName,
            sizeof(sourceMapName), &templateAfterMap))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=decode-background-template scene=%s raw=%u\n", scene,
               templateRawLen);
        return false;
    }

    /* Re-read the source map name: the template parse intentionally
     * reused the same buffer only to validate the template's header. */
    if (!vm_net_mock_scene_battle_monster_payload_map_name(
            sourcePayload, sourcePayloadLen, sourceMapName,
            sizeof(sourceMapName), &sourceAfterMap) ||
        !vm_net_mock_open_server_data_resource(sourceMapName, ".map", NULL,
                                                sourceMapPath,
                                                sizeof(sourceMapPath)))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=open-city-map scene=%s map=%s\n", scene,
               sourceMapName);
        return false;
    }
    sourceMapRawLen = vm_net_mock_load_response_file(
        sourceMapPath, backgroundMapRaw, backgroundMapRawCap);
    if (sourceMapRawLen == 0 || sourceMapRawLen < 5u ||
        backgroundMapRaw[4] != 2u)
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=validate-city-map scene=%s raw=%u type=%u\n", scene,
               sourceMapRawLen, sourceMapRawLen > 4u ? backgroundMapRaw[4] : 0u);
        return false;
    }

    backgroundNameLen = strlen(backgroundMapNameOut);
    if (backgroundNameLen > 0xffu || templateAfterMap < 11u ||
        templateAfterMap > templatePayloadLen ||
        sourcePayloadLen + strlen(backgroundNameOut) >
            sizeof(mirrorPayload) ||
        11u + backgroundNameLen + (templatePayloadLen - templateAfterMap) >
            sizeof(backgroundPayload))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=payload-capacity scene=%s source=%u template=%u "
               "template_map_end=%u\n", scene, sourcePayloadLen,
               templatePayloadLen, templateAfterMap);
        return false;
    }
    memcpy(backgroundPayload, templatePayload, 10u);
    memcpy(backgroundPayload + 4u, sourcePayload + 4u, 4u);
    backgroundPayload[10] = (u8)backgroundNameLen;
    memcpy(backgroundPayload + 11u, backgroundMapNameOut, backgroundNameLen);
    memcpy(backgroundPayload + 11u + backgroundNameLen,
           templatePayload + templateAfterMap,
           templatePayloadLen - templateAfterMap);
    backgroundPayloadLen = 11u + (u32)backgroundNameLen +
                           templatePayloadLen - templateAfterMap;

    mirrorPayloadLen = sourcePayloadLen;
    memcpy(mirrorPayload, sourcePayload, sourcePayloadLen);
    backgroundNameLen = strlen(backgroundNameOut);
    if (backgroundNameLen == 0 || backgroundNameLen > 0xffu ||
        slot + 5u > mirrorPayloadLen ||
        mirrorPayloadLen + backgroundNameLen > sizeof(mirrorPayload))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=patch-background-field18 scene=%s slot=%u payload=%u "
               "name_len=%u\n", scene, slot, mirrorPayloadLen,
               (unsigned)backgroundNameLen);
        return false;
    }
    memmove(mirrorPayload + slot + 5u + backgroundNameLen,
            mirrorPayload + slot + 5u, mirrorPayloadLen - (slot + 5u));
    mirrorPayload[slot + 4u] = (u8)backgroundNameLen;
    memcpy(mirrorPayload + slot + 5u, backgroundNameOut, backgroundNameLen);
    mirrorPayloadLen += (u32)backgroundNameLen;

    if (!vm_net_mock_scene_battle_monster_wrap_type2(
            backgroundPayload, backgroundPayloadLen, backgroundRaw,
            backgroundRawCap, backgroundRawLenOut))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=encode-background scene=%s\n", scene);
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_wrap_type2(
            mirrorPayload, mirrorPayloadLen, mirrorRaw, mirrorRawCap,
            mirrorRawLenOut))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=encode-mirror scene=%s\n", scene);
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_decode_raw_sce(
            backgroundRaw, *backgroundRawLenOut, verifyPayload,
            sizeof(verifyPayload), &verifyPayloadLen) ||
        verifyPayloadLen != backgroundPayloadLen ||
        memcmp(verifyPayload, backgroundPayload, backgroundPayloadLen) != 0)
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=verify-background scene=%s raw=%u payload=%u decoded=%u\n",
               scene, *backgroundRawLenOut, backgroundPayloadLen,
               verifyPayloadLen);
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_decode_raw_sce(
            mirrorRaw, *mirrorRawLenOut, verifyPayload, sizeof(verifyPayload),
            &verifyPayloadLen) ||
        verifyPayloadLen != mirrorPayloadLen ||
        memcmp(verifyPayload, mirrorPayload, mirrorPayloadLen) != 0)
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror "
               "stage=verify-mirror scene=%s raw=%u payload=%u decoded=%u\n",
               scene, *mirrorRawLenOut, mirrorPayloadLen, verifyPayloadLen);
        return false;
    }
    *backgroundMapRawLenOut = sourceMapRawLen;
    return true;
}

static void vm_net_mock_scene_battle_monster_restore_overlay_resource(
    const char *name, const u8 *previousRaw, u32 previousRawLen)
{
    char path[1200];
    const char *ignoredError = NULL;

    if (name == NULL || name[0] == 0 ||
        !vm_net_mock_build_overlay_resource_path(name, path, sizeof(path)))
    {
        return;
    }
    if (previousRawLen != 0 && previousRaw != NULL)
    {
        (void)vm_net_mock_scene_battle_monster_write_resource(
            path, previousRaw, previousRawLen, &ignoredError);
    }
    else
    {
        /* The path is an exact, server-derived overlay leaf.  Removing it
         * restores the prior absence after a failed deploy; it never targets
         * a source resource or an administrator-provided host path. */
        (void)remove(path);
    }
}

static bool vm_net_mock_sce_entity_tag_creates_node(u16 tag)
{
    /* ParseActorFullInfoBlob calls scene_node_find_or_create for these tags.
     * Tags 3/14 create kind 2/4 nodes; 5/6/7 create prompt/NPC nodes. */
    return tag == 3u || tag == 5u || tag == 6u || tag == 7u || tag == 14u;
}

static bool vm_net_mock_scene_battle_monster_parse_entity_list_at(
    const u8 *payload, u32 payloadLen, u32 countOffset,
    vm_net_mock_sce_entity_list *listOut)
{
    vm_net_mock_sce_entity_list list;
    u32 pos = 0;

    memset(&list, 0, sizeof(list));
    if (payload == NULL || listOut == NULL || countOffset + 2u > payloadLen)
        return false;
    list.countOffset = countOffset;
    list.recordCount = vm_net_mock_read_le16_at(payload, list.countOffset);
    if (list.recordCount > 256u)
        return false;
    list.recordsOffset = list.countOffset + 2u;
    pos = list.recordsOffset;

    for (u32 recordIndex = 0; recordIndex < list.recordCount; ++recordIndex)
    {
        u16 tag = 0;
        u32 end = 0;

        if (!vm_net_mock_parse_sce_entity_record_at(payload, payloadLen, pos,
                                                     &tag, &end) ||
            end <= pos)
        {
            return false;
        }
        if (vm_net_mock_sce_entity_tag_creates_node(tag))
            ++list.nodeRecordCount;
        if (tag == 3u)
            ++list.combatRecordCount;
        pos = end;
    }
    list.recordsEnd = pos;
    *listOut = list;
    return true;
}

static bool vm_net_mock_scene_battle_monster_parse_entity_list(
    const u8 *payload, u32 payloadLen,
    vm_net_mock_sce_entity_list *listOut)
{
    u32 countOffset = 0;

    if (payload == NULL || listOut == NULL ||
        !vm_net_mock_parse_sce2_client_prefix(payload, payloadLen, NULL,
                                               &countOffset))
        return false;
    return vm_net_mock_scene_battle_monster_parse_entity_list_at(
        payload, payloadLen, countOffset, listOut);
}

static bool vm_net_mock_scene_battle_monster_counted_spawn_at(
    const u8 *payload, u32 payloadLen, u32 wantedCombatOrdinal,
    vm_net_mock_sce_combat_spawn *spawnOut, u32 *nodeOrdinalOut)
{
    vm_net_mock_sce_entity_list list;
    u32 off = 0;
    u32 combatOrdinal = 0;
    u32 nodeOrdinal = 0;

    if (spawnOut == NULL ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &list))
    {
        return false;
    }
    off = list.recordsOffset;
    for (u32 recordIndex = 0; recordIndex < list.recordCount; ++recordIndex)
    {
        u16 tag = 0;
        u32 recordEnd = 0;

        if (!vm_net_mock_parse_sce_entity_record_at(
                payload, list.recordsEnd, off, &tag, &recordEnd) ||
            recordEnd <= off)
        {
            return false;
        }
        if (vm_net_mock_sce_entity_tag_creates_node(tag))
            ++nodeOrdinal;
        if (tag == 3u)
        {
            u32 spawnEnd = 0;

            if (!vm_net_mock_parse_sce_combat_spawn_at(
                    payload, recordEnd, off, spawnOut, &spawnEnd) ||
                spawnEnd != recordEnd)
            {
                return false;
            }
            if (combatOrdinal == wantedCombatOrdinal)
            {
                if (nodeOrdinalOut != NULL)
                    *nodeOrdinalOut = nodeOrdinal;
                return true;
            }
            ++combatOrdinal;
        }
        off = recordEnd;
    }
    return false;
}

static bool vm_net_mock_scene_battle_monster_insert_counted_record(
    u8 *payload, u32 payloadCap, u32 *payloadLenInOut,
    const vm_net_mock_scene_battle_monster_admin_row *row)
{
    vm_net_mock_sce_entity_list list;
    u8 record[512];
    u32 payloadLen = payloadLenInOut != NULL ? *payloadLenInOut : 0;
    u32 recordLen = 0;

    if (payload == NULL || payloadLenInOut == NULL || row == NULL ||
        payloadLen > payloadCap ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &list) ||
        list.recordCount >= 0xffffu ||
        !vm_net_mock_scene_battle_monster_append_record(
            record, sizeof(record), &recordLen, row) ||
        recordLen > payloadCap - payloadLen)
    {
        return false;
    }

    memmove(payload + list.recordsEnd + recordLen,
            payload + list.recordsEnd, payloadLen - list.recordsEnd);
    memcpy(payload + list.recordsEnd, record, recordLen);
    ++list.recordCount;
    payload[list.countOffset] = (u8)list.recordCount;
    payload[list.countOffset + 1u] = (u8)(list.recordCount >> 8);
    *payloadLenInOut = payloadLen + recordLen;
    return true;
}

static bool vm_net_mock_scene_battle_monster_read_base_raw(
    const char *scene, u8 *raw, u32 rawCap, u32 *rawLenOut)
{
    char path[1200];
    u32 rawLen = 0;

    if (rawLenOut)
        *rawLenOut = 0;
    if (scene == NULL || raw == NULL || rawCap == 0 ||
        !vm_net_mock_open_server_base_resource(scene, NULL, path,
                                               sizeof(path)))
        return false;
    rawLen = vm_net_mock_load_response_file(path, raw, rawCap);
    if (rawLen == 0)
        return false;
    if (rawLenOut)
        *rawLenOut = rawLen;
    return true;
}

static bool vm_net_mock_scene_battle_monster_read_overlay_raw(
    const char *scene, u8 *raw, u32 rawCap, u32 *rawLenOut,
    char *pathOut, size_t pathOutCap)
{
    char path[1400];
    u32 rawLen = 0;

    if (rawLenOut)
        *rawLenOut = 0;
    if (pathOut && pathOutCap)
        pathOut[0] = 0;
    if (scene == NULL || raw == NULL || rawCap == 0 ||
        !vm_net_mock_build_overlay_resource_path(scene, path, sizeof(path)))
        return false;
    rawLen = vm_net_mock_load_response_file(path, raw, rawCap);
    if (rawLen == 0)
        return false;
    if (pathOut && pathOutCap)
        snprintf(pathOut, pathOutCap, "%s", path);
    if (rawLenOut)
        *rawLenOut = rawLen;
    return true;
}

static bool vm_net_mock_scene_battle_monster_payload_collect_node_count(
    const u8 *payload, u32 payloadLen, u32 *nodeCountOut)
{
    u32 propCount = 0;
    vm_net_mock_sce_entity_list entityList;

    if (nodeCountOut)
        *nodeCountOut = 0;
    if (!vm_net_mock_parse_sce2_client_prefix(payload, payloadLen,
                                               &propCount, NULL) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &entityList))
    {
        return false;
    }
    if (propCount > VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX ||
        entityList.nodeRecordCount >
            VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX ||
        propCount + entityList.nodeRecordCount >
            VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX)
    {
        return false;
    }
    if (nodeCountOut)
        *nodeCountOut = propCount + entityList.nodeRecordCount;
    return true;
}

static bool vm_net_mock_scene_battle_monster_payload_has_row(
    const u8 *payload, u32 payloadLen,
    const vm_net_mock_scene_battle_monster_admin_row *wanted)
{
    vm_net_mock_sce_entity_list entityList;
    u32 off = 0;

    if (wanted == NULL ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &entityList))
    {
        return false;
    }
    off = entityList.recordsOffset;
    for (u32 recordIndex = 0; recordIndex < entityList.recordCount;
         ++recordIndex)
    {
        u16 tag = 0;
        u32 recordEnd = 0;
        vm_net_mock_sce_combat_spawn spawn;
        u32 end = 0;

        if (!vm_net_mock_parse_sce_entity_record_at(
                payload, entityList.recordsEnd, off, &tag, &recordEnd) ||
            recordEnd <= off)
        {
            return false;
        }
        if (tag == 3u &&
            vm_net_mock_parse_sce_combat_spawn_at(
                payload, recordEnd, off, &spawn, &end) && end == recordEnd &&
            spawn.actorId == wanted->monsterId && spawn.x == wanted->x &&
            spawn.y == wanted->y &&
            strcmp(spawn.displayName, wanted->displayName) == 0 &&
            strcmp(spawn.actorResource, wanted->actorResource) == 0 &&
            strcmp(spawn.effectResource, wanted->effectResource) == 0)
        {
            return true;
        }
        off = recordEnd;
    }
    return false;
}

static bool vm_net_mock_scene_battle_monster_payload_has_expanded_row(
    const u8 *payload, u32 payloadLen,
    const vm_net_mock_scene_battle_monster_admin_row *wanted)
{
    vm_net_mock_scene_battle_monster_admin_row expanded;

    if (wanted == NULL || wanted->quantity == 0 ||
        wanted->quantity > VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX)
    {
        return false;
    }
    for (u32 ordinal = 0; ordinal < wanted->quantity; ++ordinal)
    {
        if (!vm_net_mock_scene_battle_monster_expanded_row(
                wanted, ordinal, &expanded) ||
            !vm_net_mock_scene_battle_monster_payload_has_row(
                payload, payloadLen, &expanded))
        {
            return false;
        }
    }
    return true;
}

/* The deployment table's fingerprint proves that a deploy transaction once
 * completed; it cannot by itself prove that the currently served resource
 * still obeys the current SCE kind-3 grammar.  In particular, the earlier
 * generated form of a battle-monster record ended after field 17 and was
 * accepted by the deployment ledger, but the CBE loader requires field 18
 * (the effect Actor) before it creates a live type-2 scene node.
 *
 * Re-parse the exact server-owned resource with the same grammar used by the
 * battle entry path.  This is intentionally a status check only: a stale
 * publication is repaired through the existing explicit admin deployment,
 * which rebuilds from the captured base and publishes through WT18. */
static bool vm_net_mock_scene_battle_monster_deployed_source_matches(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCount)
{
    u8 raw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 payload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u32 rawLen = 0;
    u32 payloadLen = 0;
    u32 baseNodeCount = 0;
    u32 publishedNodeCount = 0;
    u32 enabledCount = 0;

    if (scene == NULL || rows == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_battle_monster_load_base_raw(
            scene, raw, sizeof(raw), &rawLen, NULL) ||
        !vm_net_mock_scene_battle_monster_decode_raw_sce(
            raw, rawLen, payload, sizeof(payload), &payloadLen) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            payload, payloadLen, &baseNodeCount) ||
        !vm_net_mock_scene_battle_monster_read_overlay_raw(
            scene, raw, sizeof(raw), &rawLen, NULL, 0) ||
        !vm_net_mock_scene_battle_monster_decode_raw_sce(
            raw, rawLen, payload, sizeof(payload), &payloadLen) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            payload, payloadLen, &publishedNodeCount))
    {
        return false;
    }
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (!rows[i].enabled)
            continue;
        if (rows[i].quantity == 0 ||
            enabledCount > 0xffffffffu - rows[i].quantity)
            return false;
        enabledCount += rows[i].quantity;
        if (!vm_net_mock_scene_battle_monster_payload_has_expanded_row(
                payload, payloadLen, &rows[i]))
        {
            return false;
        }
    }
    return baseNodeCount <= VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX &&
           enabledCount <= VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX -
                               baseNodeCount &&
           publishedNodeCount == baseNodeCount + enabledCount;
}

static bool vm_net_mock_scene_battle_monster_deployment_save(
    const char *scene, u32 fingerprint, u32 enabledCount)
{
    char sceneHex[129];
    char query[512];

    if (scene == NULL || enabledCount > 0xffffu ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_scene_battle_monster_deployments("
             "scene,config_fingerprint,configured_count) VALUES(X'%s',%u,%u) "
             "ON DUPLICATE KEY UPDATE config_fingerprint=VALUES(config_fingerprint),"
             "configured_count=VALUES(configured_count)",
             sceneHex, fingerprint, enabledCount);
    return vm_mysql_exec(query);
}

/* 27/11 can append at most four selected NPC rows after the SCE static
 * records.  Deployment must reserve these client-owned rows as well as the
 * static kind-2/kind-3 records it can parse from the base scene; otherwise a
 * syntactically valid SCE can still produce an invalid live node index. */
static u32 vm_net_mock_collect_scene_npcinfo_seeds(
    const char *scene, vm_net_mock_scene_npcinfo_seed *seeds, u32 seedCap,
    u32 *totalOut, u32 *dynamicOut);

static u32 vm_net_mock_scene_battle_monster_npc_node_reserve(
    const char *scene)
{
    vm_net_mock_scene_npcinfo_seed seeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];

    if (scene == NULL || scene[0] == 0)
        return 0;
    memset(seeds, 0, sizeof(seeds));
    return vm_net_mock_collect_scene_npcinfo_seeds(
        scene, seeds, VM_NET_MOCK_SCENE_NPCINFO_MAX, NULL, NULL);
}

/* The on-disk .actor resource is the editor's compressed image/animation
 * manifest.  The motion descriptor parsed at 0x0100D6E2 is a runtime object
 * produced after that manifest is loaded, so it cannot be decoded from the
 * file bytes with the scene-node parser's u16/string grammar.  Keep this
 * reserve conservative until a runtime allocation trace establishes a stable
 * file-to-node mapping; rejecting every valid actor here would be a false
 * configuration error. */
static bool vm_net_mock_actor_scene_node_reserve(
    const char *actorResource, u32 *reserveOut, const char **errorOut)
{
    if (reserveOut)
        *reserveOut = 0;
    if (errorOut)
        *errorOut = NULL;
    if (actorResource == NULL || reserveOut == NULL ||
        vm_net_mock_scene_name_has_path_separator(actorResource) ||
        !vm_net_mock_str_ends_with(actorResource, ".actor"))
    {
        if (errorOut)
            *errorOut = "Actor 资源名格式无效";
        return false;
    }
    return true;
}

static u32 vm_net_mock_scene_battle_monster_collect_publish_names(
    const char *scene,
    const vm_net_mock_scene_battle_monster_admin_row *rows, u32 rowCount,
    const char **names, u32 nameCap, char imageNameStorage[][64],
    u32 imageNameStorageCap)
{
    u32 nameCount = 0;
    u32 imageNameStorageCount = 0;

    if (scene == NULL || rows == NULL || names == NULL || nameCap == 0 ||
        imageNameStorage == NULL || imageNameStorageCap == 0)
        return 0;
    names[nameCount++] = scene;
    for (u32 i = 0; i < rowCount; ++i)
    {
        const char *dependencies[2];

        if (!rows[i].enabled)
            continue;
        dependencies[0] = rows[i].actorResource;
        dependencies[1] = rows[i].effectResource;
        for (u32 dependencyIndex = 0; dependencyIndex < 2; ++dependencyIndex)
        {
            char imageNames[VM_NET_MOCK_ACTOR_RESOURCE_IMAGE_MAX][64];
            u32 imageCount = 0;

            memset(imageNames, 0, sizeof(imageNames));
            if (!vm_net_mock_actor_resource_collect_images(
                    dependencies[dependencyIndex], imageNames, &imageCount))
            {
                return 0;
            }
            for (u32 sourceIndex = 0; sourceIndex <= imageCount; ++sourceIndex)
            {
                const char *sourceName = sourceIndex == 0 ?
                    dependencies[dependencyIndex] : imageNames[sourceIndex - 1u];
                bool duplicate = false;

                for (u32 existing = 0; existing < nameCount; ++existing)
                {
                    if (strcmp(names[existing], sourceName) == 0)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                {
                    if (nameCount >= nameCap)
                        return 0;
                    if (sourceIndex != 0)
                    {
                        if (imageNameStorageCount >= imageNameStorageCap ||
                            snprintf(imageNameStorage[imageNameStorageCount],
                                     sizeof(imageNameStorage[0]), "%s",
                                     sourceName) >=
                                (int)sizeof(imageNameStorage[0]))
                        {
                            return 0;
                        }
                        sourceName = imageNameStorage[imageNameStorageCount++];
                    }
                    names[nameCount++] = sourceName;
                }
            }
        }
    }
    return nameCount;
}

/* Rebuild one exact source SCE from its durable base plus the currently
 * enabled rows.  The runtime does not read the table to synthesize nodes; it
 * reads the resulting SCE through the normal scene-resource path. */
static bool vm_net_mock_scene_battle_monster_admin_deploy(
    const char *scene, const char **errorOut)
{
    vm_net_mock_scene_battle_monster_admin_row
        rows[VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX];
    u8 baseRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 previousRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 payload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 encoded[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 outputRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 roundTripPayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 cityMirrorRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 cityBackgroundRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 cityBackgroundMapRaw[VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_MAP_RAW_MAX];
    u8 previousCityMirrorRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 previousCityBackgroundRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 previousCityBackgroundMapRaw
        [VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_MAP_RAW_MAX];
    vm_net_mock_sce_entity_list baseEntityList;
    vm_net_mock_sce_entity_list finalEntityList;
    const char *baseSource = "unresolved";
    const char *publishError = NULL;
    char resourcePath[1200];
    char cityMirrorPath[1200];
    char cityBackgroundPath[1200];
    char cityBackgroundMapPath[1200];
    char cityMirrorName[VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_NAME_CAP];
    char cityBackgroundName[VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_NAME_CAP];
    char cityBackgroundMapName[VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_NAME_CAP];
    u32 rowCount = 0;
    u32 storedRowCount = 0;
    u32 enabledCount = 0;
    u32 baseRawLen = 0;
    u32 previousRawLen = 0;
    u32 payloadLen = 0;
    u32 encodedLen = 0;
    u32 outputRawLen = 0;
    u32 roundTripPayloadLen = 0;
    u32 cityMirrorRawLen = 0;
    u32 cityBackgroundRawLen = 0;
    u32 cityBackgroundMapRawLen = 0;
    u32 previousCityMirrorRawLen = 0;
    u32 previousCityBackgroundRawLen = 0;
    u32 previousCityBackgroundMapRawLen = 0;
    u32 originalNodeCount = 0;
    u32 finalNodeCount = 0;
    u32 npcNodeReserve = 0;
    u32 actorChildReserve = 0;
    u32 insertOffset = 0;
    u32 ignoredTrailingBytes = 0;
    u32 fingerprint = 0;
    /* A generated SCE kind-3 record is not self-contained: each referenced
     * Actor descriptor in turn names one or more GIF sheets.  Publish the
     * complete, deduplicated closure in the same native content-update
     * release as the SCE.  A bare fireball effect can otherwise render while
     * the absent body Actor prevents creation of a battle-capable node. */
    const char *names[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PUBLISH_NAME_MAX];
    char imageNameStorage[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PUBLISH_NAME_MAX][64];
    u32 nameCount = 0;
    bool contentChanged = false;
    bool cityMirrorRequired = false;
    bool cityBackgroundMapWritten = false;
    bool cityBackgroundWritten = false;
    bool cityMirrorWritten = false;

    if (errorOut)
        *errorOut = "场景战斗怪部署失败";
    if (scene == NULL || !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_resource_exists(scene) ||
        !vm_net_mock_scene_battle_monster_schema_ensure())
    {
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_admin_count_scene(scene,
                                                             &storedRowCount) ||
        storedRowCount > VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX)
    {
        if (errorOut)
            *errorOut = "场景战斗怪草稿数量异常；请先清理超过上限的配置";
        return false;
    }
    memset(rows, 0, sizeof(rows));
    rowCount = vm_net_mock_scene_battle_monster_admin_list(
        scene, rows, VM_NET_MOCK_SCENE_BATTLE_MONSTER_ADMIN_MAX);
    if (rowCount != storedRowCount)
    {
        if (errorOut)
            *errorOut = "场景战斗怪草稿读取不完整；拒绝部署以避免遗漏配置";
        return false;
    }
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (!vm_net_mock_scene_battle_monster_row_validate(scene, &rows[i]))
        {
            if (errorOut)
                *errorOut = "存在无效的场景战斗怪 Actor、名称或坐标";
            return false;
        }
        if (rows[i].enabled)
        {
            u32 rowChildReserve = 0;
            if (!vm_net_mock_ensure_actor_resource_available(
                    rows[i].actorResource, &publishError))
            {
                if (errorOut)
                    *errorOut = publishError ? publishError : "战斗怪 Actor 资源校验失败";
                return false;
            }
            if (!vm_net_mock_ensure_actor_resource_available(
                    rows[i].effectResource, &publishError))
            {
                if (errorOut)
                    *errorOut = publishError ? publishError :
                                             "战斗怪特效 Actor 资源校验失败";
                return false;
            }
            if (!vm_net_mock_actor_scene_node_reserve(
                    rows[i].actorResource, &rowChildReserve, &publishError) ||
                rowChildReserve >
                    (VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX -
                     actorChildReserve) / rows[i].quantity)
            {
                printf("[error][mock-admin] scene_battle_monster_deploy stage=actor-capacity-input scene=%s actor=%u resource=%s\n",
                       scene, rows[i].monsterId, rows[i].actorResource);
                if (errorOut)
                    *errorOut = publishError ? publishError :
                                             "战斗怪 Actor 容量参数校验失败";
                return false;
            }
            actorChildReserve += rowChildReserve * rows[i].quantity;
            enabledCount += rows[i].quantity;
        }
    }
    if (!vm_net_mock_scene_battle_monster_load_base_raw(
            scene, baseRaw, sizeof(baseRaw), &baseRawLen, &baseSource))
    {
        printf("[error][mock-admin] scene_battle_monster_deploy stage=load-base "
               "scene=%s mysql=%s\n", scene, vm_mysql_last_error());
        if (errorOut)
            *errorOut = "无法读取或捕获场景基础资源；请查看服务端 scene_battle_monster_deploy 日志";
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_decode_raw_sce(
            baseRaw, baseRawLen, payload, sizeof(payload), &payloadLen))
    {
        printf("[error][mock-admin] scene_battle_monster_deploy stage=decode-base "
               "scene=%s base=%s raw=%u\n", scene, baseSource, baseRawLen);
        if (errorOut)
            *errorOut = "捕获的基础场景资源不是可解析的 SCE2；请先核对资源来源";
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_payload_collect_node_count(
            payload, payloadLen, &originalNodeCount) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &baseEntityList))
    {
        printf("[error][mock-admin] scene_battle_monster_deploy stage=count-base "
               "scene=%s base=%s raw=%u payload=%u\n", scene, baseSource,
               baseRawLen, payloadLen);
        if (errorOut)
            *errorOut = "基础 SCE2 的静态节点段不符合已确认的场景记录格式";
        return false;
    }
    insertOffset = baseEntityList.recordsEnd;
    ignoredTrailingBytes = payloadLen - insertOffset;
    npcNodeReserve = vm_net_mock_scene_battle_monster_npc_node_reserve(scene);
    if (originalNodeCount + npcNodeReserve + enabledCount + actorChildReserve >
        VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX)
    {
        printf("[error][mock-admin] scene_battle_monster_deploy stage=node-limit "
               "scene=%s base=%s static=%u npc_reserve=%u enabled=%u actor_child_reserve=%u limit=%u\n", scene,
               baseSource, originalNodeCount, npcNodeReserve, enabledCount,
               actorChildReserve,
               VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX);
        if (errorOut)
            *errorOut = "场景静态节点、已下发 NPC、战斗怪及其 Actor 子节点超过客户端 24 个非本地节点上限";
        return false;
    }
    for (u32 i = 0; i < rowCount; ++i)
    {
        vm_net_mock_scene_battle_monster_admin_row expanded;

        if (!rows[i].enabled)
            continue;
        for (u32 ordinal = 0; ordinal < rows[i].quantity; ++ordinal)
        {
            if (!vm_net_mock_scene_battle_monster_expanded_row(
                    &rows[i], ordinal, &expanded) ||
                !vm_net_mock_scene_battle_monster_insert_counted_record(
                    payload, sizeof(payload), &payloadLen, &expanded))
            {
                if (errorOut)
                    *errorOut = "场景资源容量不足，无法写入全部战斗怪";
                return false;
            }
        }
    }
    insertOffset = payloadLen - ignoredTrailingBytes;
    if (!vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &finalEntityList) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            payload, payloadLen, &finalNodeCount) ||
        finalEntityList.recordCount !=
            baseEntityList.recordCount + enabledCount ||
        finalEntityList.recordsEnd != insertOffset ||
        finalNodeCount != originalNodeCount + enabledCount)
    {
        if (errorOut)
            *errorOut = "生成后的场景战斗怪记录未通过 SCE2 解析验证";
        return false;
    }
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (rows[i].enabled &&
            !vm_net_mock_scene_battle_monster_payload_has_expanded_row(
                payload, payloadLen, &rows[i]))
        {
            if (errorOut)
                *errorOut = "生成后的战斗怪记录与配置不一致";
            return false;
        }
    }
    if (!vm_net_mock_scene_battle_monster_lzss_literal_encode(
            payload, payloadLen, encoded, sizeof(encoded), &encodedLen) ||
        encodedLen > sizeof(outputRaw) - 4u)
    {
        if (errorOut)
            *errorOut = "场景资源压缩失败或超过服务端资源上限";
        return false;
    }
    outputRaw[0] = (u8)encodedLen;
    outputRaw[1] = (u8)(encodedLen >> 8);
    outputRaw[2] = (u8)(encodedLen >> 16);
    outputRaw[3] = (u8)(encodedLen >> 24);
    memcpy(outputRaw + 4, encoded, encodedLen);
    outputRawLen = encodedLen + 4u;
    /* Verify the bytes in the exact direction used by the client: wrapper
     * dispatch first, then LZSS decode, then the SCE2 signature.  The old
     * type-1 container passed this server's overly-permissive decoder but
     * delivered a compression header to LoadSceneDataFromStream. */
    if (!vm_net_mock_scene_battle_monster_decode_raw_sce(
            outputRaw, outputRawLen, roundTripPayload,
            sizeof(roundTripPayload), &roundTripPayloadLen) ||
        roundTripPayloadLen != payloadLen ||
        memcmp(roundTripPayload, payload, payloadLen) != 0)
    {
        printf("[error][mock-admin] scene_battle_monster_deploy stage=roundtrip "
               "scene=%s raw=%u payload=%u decoded=%u resource_type=%u\n",
               scene, outputRawLen, payloadLen, roundTripPayloadLen,
               outputRawLen > 4 ? outputRaw[4] : 0);
        if (errorOut)
            *errorOut = "生成的场景资源未能按客户端 SCE2 格式解码";
        return false;
    }
    cityMirrorRequired = scene[0] == 'c' && enabledCount != 0;
    if (cityMirrorRequired &&
        !vm_net_mock_scene_battle_monster_build_city_mirror(
            scene, payload, payloadLen, cityMirrorName,
            sizeof(cityMirrorName), cityMirrorRaw, sizeof(cityMirrorRaw),
            &cityMirrorRawLen, cityBackgroundName,
            sizeof(cityBackgroundName), cityBackgroundRaw,
            sizeof(cityBackgroundRaw), &cityBackgroundRawLen,
            cityBackgroundMapName, sizeof(cityBackgroundMapName),
            cityBackgroundMapRaw, sizeof(cityBackgroundMapRaw),
            &cityBackgroundMapRawLen))
    {
        printf("[error][mock-admin] scene_battle_monster_city_mirror stage=build "
               "scene=%s action=reject-deploy reason=background-template-or-"
               "empty-field18-contract-unavailable\n",
               scene);
        if (errorOut)
            *errorOut = "城市场景战斗镜像生成失败：缺少可验证的背景模板、地图或空 field18 槽";
        return false;
    }
    if (!vm_net_mock_build_overlay_resource_path(scene, resourcePath,
                                                 sizeof(resourcePath)))
    {
        if (errorOut)
            *errorOut = "无法建立当前数据库的场景发布目录";
        return false;
    }
    if (!vm_net_mock_scene_battle_monster_read_overlay_raw(
            scene, previousRaw, sizeof(previousRaw), &previousRawLen,
            NULL, 0))
    {
        previousRawLen = 0;
    }
    if (cityMirrorRequired)
    {
        if (!vm_net_mock_build_overlay_resource_path(
                cityMirrorName, cityMirrorPath, sizeof(cityMirrorPath)) ||
            !vm_net_mock_build_overlay_resource_path(
                cityBackgroundName, cityBackgroundPath,
                sizeof(cityBackgroundPath)) ||
            !vm_net_mock_build_overlay_resource_path(
                cityBackgroundMapName, cityBackgroundMapPath,
                sizeof(cityBackgroundMapPath)))
        {
            if (errorOut)
                *errorOut = "无法建立城市场景战斗镜像发布目录";
            return false;
        }
        if (!vm_net_mock_scene_battle_monster_read_overlay_raw(
                cityMirrorName, previousCityMirrorRaw,
                sizeof(previousCityMirrorRaw), &previousCityMirrorRawLen,
                NULL, 0))
        {
            previousCityMirrorRawLen = 0;
        }
        if (!vm_net_mock_scene_battle_monster_read_overlay_raw(
                cityBackgroundName, previousCityBackgroundRaw,
                sizeof(previousCityBackgroundRaw),
                &previousCityBackgroundRawLen, NULL, 0))
        {
            previousCityBackgroundRawLen = 0;
        }
        if (!vm_net_mock_scene_battle_monster_read_overlay_raw(
                cityBackgroundMapName, previousCityBackgroundMapRaw,
                sizeof(previousCityBackgroundMapRaw),
                &previousCityBackgroundMapRawLen, NULL, 0))
        {
            previousCityBackgroundMapRawLen = 0;
        }
    }
    if (!vm_net_mock_scene_battle_monster_write_resource(resourcePath,
                                                          outputRaw,
                                                          outputRawLen,
                                                          errorOut))
    {
        return false;
    }
    if (cityMirrorRequired)
    {
        if (!vm_net_mock_scene_battle_monster_write_resource(
                cityBackgroundMapPath, cityBackgroundMapRaw,
                cityBackgroundMapRawLen, errorOut))
        {
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                scene, previousRaw, previousRawLen);
            return false;
        }
        cityBackgroundMapWritten = true;
        if (!vm_net_mock_scene_battle_monster_write_resource(
                cityBackgroundPath, cityBackgroundRaw, cityBackgroundRawLen,
                errorOut))
        {
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityBackgroundMapName, previousCityBackgroundMapRaw,
                previousCityBackgroundMapRawLen);
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                scene, previousRaw, previousRawLen);
            return false;
        }
        cityBackgroundWritten = true;
        if (!vm_net_mock_scene_battle_monster_write_resource(
                cityMirrorPath, cityMirrorRaw, cityMirrorRawLen, errorOut))
        {
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityBackgroundName, previousCityBackgroundRaw,
                previousCityBackgroundRawLen);
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityBackgroundMapName, previousCityBackgroundMapRaw,
                previousCityBackgroundMapRawLen);
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                scene, previousRaw, previousRawLen);
            return false;
        }
        cityMirrorWritten = true;
    }
    memset(imageNameStorage, 0, sizeof(imageNameStorage));
    nameCount = vm_net_mock_scene_battle_monster_collect_publish_names(
        scene, rows, rowCount, names, sizeof(names) / sizeof(names[0]),
        imageNameStorage, sizeof(imageNameStorage) / sizeof(imageNameStorage[0]));
    if (nameCount == 0)
    {
        if (errorOut)
            *errorOut = "场景战斗怪依赖资源清单生成失败";
        if (cityMirrorWritten)
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityMirrorName, previousCityMirrorRaw, previousCityMirrorRawLen);
        if (cityBackgroundWritten)
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityBackgroundName, previousCityBackgroundRaw,
                previousCityBackgroundRawLen);
        if (cityBackgroundMapWritten)
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityBackgroundMapName, previousCityBackgroundMapRaw,
                previousCityBackgroundMapRawLen);
        vm_net_mock_scene_battle_monster_restore_overlay_resource(
            scene, previousRaw, previousRawLen);
        return false;
    }
    if (cityMirrorRequired)
    {
        const char *cityNames[] = {cityMirrorName, cityBackgroundName,
                                   cityBackgroundMapName};

        for (u32 cityIndex = 0;
             cityIndex < sizeof(cityNames) / sizeof(cityNames[0]); ++cityIndex)
        {
            bool present = false;

            for (u32 existing = 0; existing < nameCount; ++existing)
            {
                if (strcmp(names[existing], cityNames[cityIndex]) == 0)
                {
                    present = true;
                    break;
                }
            }
            if (!present)
            {
                if (nameCount >= sizeof(names) / sizeof(names[0]))
                {
                    if (errorOut)
                        *errorOut = "城市场景战斗镜像资源清单超过上限";
                    vm_net_mock_scene_battle_monster_restore_overlay_resource(
                        cityMirrorName, previousCityMirrorRaw,
                        previousCityMirrorRawLen);
                    vm_net_mock_scene_battle_monster_restore_overlay_resource(
                        cityBackgroundName, previousCityBackgroundRaw,
                        previousCityBackgroundRawLen);
                    vm_net_mock_scene_battle_monster_restore_overlay_resource(
                        cityBackgroundMapName, previousCityBackgroundMapRaw,
                        previousCityBackgroundMapRawLen);
                    vm_net_mock_scene_battle_monster_restore_overlay_resource(
                        scene, previousRaw, previousRawLen);
                    return false;
                }
                names[nameCount++] = cityNames[cityIndex];
            }
        }
    }
    if (!vm_net_mock_content_update_publish_files(names, nameCount, &publishError,
                                                  &contentChanged))
    {
        if (cityMirrorWritten)
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityMirrorName, previousCityMirrorRaw, previousCityMirrorRawLen);
        if (cityBackgroundWritten)
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityBackgroundName, previousCityBackgroundRaw,
                previousCityBackgroundRawLen);
        if (cityBackgroundMapWritten)
            vm_net_mock_scene_battle_monster_restore_overlay_resource(
                cityBackgroundMapName, previousCityBackgroundMapRaw,
                previousCityBackgroundMapRawLen);
        vm_net_mock_scene_battle_monster_restore_overlay_resource(
            scene, previousRaw, previousRawLen);
        if (errorOut)
            *errorOut = publishError ? publishError : "内容更新发布失败";
        return false;
    }
    fingerprint = vm_net_mock_scene_battle_monster_fingerprint(rows, rowCount);
    if (!vm_net_mock_scene_battle_monster_deployment_save(scene, fingerprint,
                                                           enabledCount))
    {
        printf("[error][mock-admin] scene_battle_monster_deploy_status_failed scene=%s "
               "base=%s nodes=%u->%u error=%s action=content-update-already-published\n",
               scene, baseSource, originalNodeCount, finalNodeCount,
               vm_mysql_last_error());
        if (errorOut)
            *errorOut = "场景资源已部署并加入内容更新，但部署状态写入失败；请重试部署以补齐状态";
        vm_net_mock_monster_catalog_invalidate();
        return false;
    }
    vm_net_mock_monster_catalog_invalidate();
    printf("[info][mock-admin] scene_battle_monster_deploy scene=%s base=%s "
            "drafts=%u enabled=%u nodes=%u->%u raw=%u payload=%u "
            "entity_count=%u->%u entity_end=%u trailing_preserved=%u "
            "manifest_files=%u resource_type=%u content_changed=%u city_mirror=%s publish=WT18/9+18/8->18/7 catalog=invalidated evidence=SCE2-counted-entity-list+kind3+"
            "mmBattle:0x66CC\n",
            scene, baseSource, rowCount, enabledCount, originalNodeCount,
            finalNodeCount, outputRawLen, payloadLen,
            baseEntityList.recordCount, finalEntityList.recordCount,
            finalEntityList.recordsEnd, ignoredTrailingBytes, nameCount, outputRaw[4],
            contentChanged ? 1u : 0u,
            cityMirrorRequired ? cityMirrorName : "-");
    if (errorOut)
        *errorOut = "ok";
    return true;
}

static bool vm_net_mock_npc_service_options_validate(
    const vm_net_mock_npc_service_option *options, u32 optionCount)
{
    if ((options == NULL && optionCount != 0) ||
        optionCount > VM_NET_MOCK_NPC_SERVICE_OPTION_MAX)
    {
        return false;
    }
    for (u32 i = 0; i < optionCount; ++i)
    {
        if (options[i].kind == VM_NET_MOCK_NPC_KIND_NORMAL ||
            options[i].kind > VM_NET_MOCK_NPC_KIND_MAX ||
            strlen(options[i].optionName) >= sizeof(options[i].optionName) ||
            strlen(options[i].optionDescription) >=
                sizeof(options[i].optionDescription))
        {
            return false;
        }
        for (u32 other = 0; other < i; ++other)
        {
            if (options[other].kind == options[i].kind)
                return false;
        }
    }
    return true;
}

/* Caller owns an active transaction.  The marker is written even for an
 * empty set, which is the durable distinction between “nothing configured”
 * and “remove every direct service”. */
static bool vm_net_mock_npc_service_options_replace_in_transaction(
    const char *scene, u32 actorId,
    const vm_net_mock_npc_service_option *options, u32 optionCount,
    const char **errorOut)
{
    char sceneHex[64 * 2 + 1];
    char query[1024];

    if (errorOut != NULL)
        *errorOut = "NPC service options are invalid";
    if (scene == NULL || actorId == 0 || !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_npc_service_options_validate(options, optionCount) ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_npc_services WHERE scene=X'%s' AND actor_id=%u",
             sceneHex, actorId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "INSERT INTO server_npc_services(scene,actor_id,service_kind,sort_order,option_name,option_description) "
             "VALUES(X'%s',%u,0,0,X'',X'')",
             sceneHex, actorId);
    if (!vm_mysql_exec(query))
        goto failed;
    for (u32 i = 0; i < optionCount; ++i)
    {
        char nameHex[sizeof(options[i].optionName) * 2 + 1];
        char descriptionHex[sizeof(options[i].optionDescription) * 2 + 1];

        nameHex[0] = 0;
        descriptionHex[0] = 0;
        if ((options[i].optionName[0] != 0 &&
             vm_mysql_hex_encode(options[i].optionName,
                                 strlen(options[i].optionName), nameHex,
                                 sizeof(nameHex)) == 0) ||
            (options[i].optionDescription[0] != 0 &&
             vm_mysql_hex_encode(options[i].optionDescription,
                                 strlen(options[i].optionDescription),
                                 descriptionHex, sizeof(descriptionHex)) == 0))
        {
            if (errorOut != NULL)
                *errorOut = "NPC service option text encoding failed";
            return false;
        }
        snprintf(query, sizeof(query),
                 "INSERT INTO server_npc_services(scene,actor_id,service_kind,sort_order,option_name,option_description) "
                 "VALUES(X'%s',%u,%u,%u,X'%s',X'%s')",
                 sceneHex, actorId, options[i].kind, options[i].sortOrder,
                 nameHex, descriptionHex);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    if (errorOut != NULL)
        *errorOut = "ok";
    return true;

failed:
    if (errorOut != NULL)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_npc_service_options_admin_replace(
    const char *scene, u32 actorId,
    const vm_net_mock_npc_service_option *options, u32 optionCount,
    const char **errorOut)
{
    bool transactionStarted = false;

    if (errorOut != NULL)
        *errorOut = "NPC service options are invalid";
    if (!vm_net_mock_npc_service_options_table_ensure() ||
        !vm_mysql_exec("START TRANSACTION"))
        goto failed;
    transactionStarted = true;
    if (!vm_net_mock_npc_service_options_replace_in_transaction(
            scene, actorId, options, optionCount, errorOut) ||
        !vm_mysql_exec("COMMIT"))
    {
        goto failed;
    }
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut != NULL && (*errorOut == NULL || strcmp(*errorOut, "ok") == 0))
        *errorOut = vm_mysql_last_error();
    return false;
}

static int vm_net_mock_native_npc_override_find_exact(const char *scene,
                                                       u32 actorId)
{
    if (!vm_net_mock_native_npc_db_load() || scene == NULL || actorId == 0)
        return -1;
    for (u32 i = 0; i < g_vm_net_mock_native_npc_override_count; ++i)
    {
        if (g_vm_net_mock_native_npc_overrides[i].actorId == actorId &&
            strcmp(g_vm_net_mock_native_npc_overrides[i].scene, scene) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

/* The base actor remains wholly owned by its SCE/XSE.  This applies only the
 * server service contract and enabled state stored in the explicit overlay. */
static bool vm_net_mock_native_npc_override_apply(const char *scene,
                                                  vm_net_mock_scene_npcinfo_seed *seed)
{
    int index = -1;

    if (scene == NULL || seed == NULL || !seed->nativeSceneActor ||
        seed->actorId == 0)
    {
        return false;
    }
    index = vm_net_mock_native_npc_override_find_exact(scene, seed->actorId);
    if (index < 0)
        return true;
    if (!g_vm_net_mock_native_npc_overrides[index].enabled)
        return false;
    seed->kind = g_vm_net_mock_native_npc_overrides[index].serviceKind;
    return true;
}

static const vm_net_mock_npc_shop_inventory_row *
vm_net_mock_npc_shop_inventory_find_exact(const char *scene, u32 actorId,
                                          u32 itemId)
{
    if (!vm_net_mock_native_npc_db_load() || scene == NULL || actorId == 0 ||
        itemId == 0)
    {
        return NULL;
    }
    for (u32 i = 0; i < g_vm_net_mock_npc_shop_inventory_count; ++i)
    {
        const vm_net_mock_npc_shop_inventory_row *row =
            &g_vm_net_mock_npc_shop_inventory[i];
        /* Both administration and the live NPC context carry the canonical
         * sMap/SCE resource key. Scene, actor and item are one exact tuple;
         * a bare basename is invalid rather than an alias for `*.sce`. */
        if (row->actorId == actorId && row->itemId == itemId &&
            vm_net_mock_scene_names_equal_exact(row->scene, scene))
        {
            return row;
        }
    }
    return NULL;
}

static u32 vm_net_mock_npc_shop_inventory_admin_list(
    const char *scene, u32 actorId, vm_net_mock_npc_shop_inventory_row *rows,
    u32 rowCap)
{
    u32 count = 0;

    if (!vm_net_mock_native_npc_db_load() || scene == NULL || actorId == 0 ||
        (rows == NULL && rowCap != 0))
    {
        return 0;
    }
    for (u32 i = 0; i < g_vm_net_mock_npc_shop_inventory_count; ++i)
    {
        if (g_vm_net_mock_npc_shop_inventory[i].actorId == actorId &&
            strcmp(g_vm_net_mock_npc_shop_inventory[i].scene, scene) == 0)
        {
            if (rows != NULL && count < rowCap)
                rows[count] = g_vm_net_mock_npc_shop_inventory[i];
            ++count;
        }
    }
    return rows != NULL && count > rowCap ? rowCap : count;
}

static bool vm_net_mock_npc_shop_inventory_service_kind_is_supported(
    u16 serviceKind)
{
    return serviceKind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT ||
           serviceKind == VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT ||
           serviceKind == VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT;
}

/* The client exposes three distinct merchant menus.  Inventory configuration
 * must follow the same category boundary as the later 26/1 menu builder;
 * otherwise an administrator can save an item which can never be displayed
 * (or accidentally route a medicine through an equipment menu). */
static bool vm_net_mock_npc_shop_inventory_item_matches_service(
    const vm_net_mock_shop_catalog_item *item, u16 serviceKind)
{
    if (item == NULL)
        return false;
    if (serviceKind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT)
        return item->isEquip && item->category >= 7u && item->category <= 9u;
    if (serviceKind == VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT)
        return item->isEquip && item->category <= 6u;
    if (serviceKind == VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT)
        return !item->isEquip && item->category == 10u;
    return false;
}

/* A zero value is the explicit administrator-facing spelling of "use the
 * current product-catalog price".  Store the resolved positive price so the
 * NPC shop packet remains self-contained and continues to satisfy the
 * client's u32 price parser. */
static u32 vm_net_mock_npc_shop_inventory_resolve_unit_price(
    const vm_net_mock_shop_catalog_item *item, u32 requestedUnitPrice)
{
    if (item == NULL)
        return 0;
    if (requestedUnitPrice != 0)
        return requestedUnitPrice;
    return vm_net_mock_shop_effective_unit_price(item->itemId, item->price);
}

static bool vm_net_mock_native_npc_admin_save_override(
    const char *scene, u32 actorId, u16 serviceKind, bool enabled,
    const vm_net_mock_npc_service_option *serviceOptions,
    u32 serviceOptionCount, bool replaceServiceOptions,
    const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_native_npc_overrides[0].scene) * 2 + 1];
    char query[768];
    vm_net_mock_native_npc_override row;
    int existing = -1;
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "native NPC override is invalid";
    if (!vm_net_mock_native_npc_db_load() ||
        !vm_net_mock_npc_service_options_table_ensure() ||
        !vm_net_mock_scene_name_is_safe(scene) || actorId == 0 ||
        serviceKind > VM_NET_MOCK_NPC_KIND_MAX ||
        (replaceServiceOptions &&
         !vm_net_mock_npc_service_options_validate(serviceOptions,
                                                    serviceOptionCount)) ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
    {
        return false;
    }
    existing = vm_net_mock_native_npc_override_find_exact(scene, actorId);
    if (existing < 0 && g_vm_net_mock_native_npc_override_count >=
                            VM_NET_MOCK_NATIVE_NPC_OVERRIDE_MAX)
    {
        if (errorOut)
            *errorOut = "native NPC override catalog is full";
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_native_npc_overrides(scene,actor_id,service_kind,enabled) "
             "VALUES(X'%s',%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "service_kind=VALUES(service_kind),enabled=VALUES(enabled)",
             sceneHex, actorId, serviceKind, enabled ? 1u : 0u);
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    transactionStarted = true;
    if (!vm_mysql_exec(query))
    {
        goto failed;
    }
    if (replaceServiceOptions &&
        !vm_net_mock_npc_service_options_replace_in_transaction(
            scene, actorId, serviceOptions, serviceOptionCount, errorOut))
    {
        goto failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    memset(&row, 0, sizeof(row));
    snprintf(row.scene, sizeof(row.scene), "%s", scene);
    row.actorId = actorId;
    row.serviceKind = serviceKind;
    row.enabled = enabled;
    if (existing >= 0)
        g_vm_net_mock_native_npc_overrides[existing] = row;
    else
        g_vm_net_mock_native_npc_overrides[
            g_vm_net_mock_native_npc_override_count++] = row;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] native_npc_override_save scene=%s actor=%u service=%u enabled=%u\n",
           scene, actorId, serviceKind, enabled ? 1u : 0u);
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_native_npc_admin_delete_override(
    const char *scene, u32 actorId, const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_native_npc_overrides[0].scene) * 2 + 1];
    char query[512];
    int existing = -1;
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "native NPC override not found";
    if (!vm_net_mock_native_npc_db_load() ||
        !vm_net_mock_npc_service_options_table_ensure() ||
        !vm_net_mock_scene_name_is_safe(scene) || actorId == 0 ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
    {
        return false;
    }
    existing = vm_net_mock_native_npc_override_find_exact(scene, actorId);
    if (existing < 0)
        return false;
    if (!vm_mysql_exec("START TRANSACTION"))
        goto failed;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "DELETE FROM server_npc_services "
             "WHERE scene=X'%s' AND actor_id=%u", sceneHex, actorId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_native_npc_overrides "
             "WHERE scene=X'%s' AND actor_id=%u", sceneHex, actorId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    if ((u32)existing + 1 < g_vm_net_mock_native_npc_override_count)
    {
        memmove(&g_vm_net_mock_native_npc_overrides[existing],
                &g_vm_net_mock_native_npc_overrides[existing + 1],
                (g_vm_net_mock_native_npc_override_count - (u32)existing - 1) *
                    sizeof(g_vm_net_mock_native_npc_overrides[0]));
    }
    --g_vm_net_mock_native_npc_override_count;
    memset(&g_vm_net_mock_native_npc_overrides[
               g_vm_net_mock_native_npc_override_count],
           0, sizeof(g_vm_net_mock_native_npc_overrides[0]));
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] native_npc_override_delete scene=%s actor=%u\n",
           scene, actorId);
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_npc_shop_inventory_admin_save(
    const char *scene, u32 actorId, u16 serviceKind, u32 itemId,
    u32 unitPrice, bool enabled, const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_npc_shop_inventory[0].scene) * 2 + 1];
    char query[768];
    vm_net_mock_npc_shop_inventory_row row;
    const vm_net_mock_shop_catalog_item *item = NULL;
    int existing = -1;

    if (errorOut)
        *errorOut = "NPC shop inventory is invalid";
    if (!vm_net_mock_native_npc_db_load() ||
        !vm_net_mock_scene_name_is_safe(scene) || actorId == 0 || itemId == 0 ||
        !vm_net_mock_npc_shop_inventory_service_kind_is_supported(serviceKind) ||
        (item = vm_net_mock_find_shop_catalog_item(itemId)) == NULL ||
        !vm_net_mock_npc_shop_inventory_item_matches_service(item, serviceKind) ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
    {
        if (errorOut && itemId != 0 && item == NULL)
            *errorOut = "物品目录中不存在该物品";
        else if (errorOut && item != NULL &&
                 !vm_net_mock_npc_shop_inventory_item_matches_service(
                     item, serviceKind))
            *errorOut = "该物品不属于当前商人可售分类";
        return false;
    }
    unitPrice = vm_net_mock_npc_shop_inventory_resolve_unit_price(item,
                                                                    unitPrice);
    if (unitPrice == 0)
    {
        if (errorOut)
            *errorOut = "物品默认价格无效";
        return false;
    }
    existing = -1;
    for (u32 i = 0; i < g_vm_net_mock_npc_shop_inventory_count; ++i)
    {
        if (g_vm_net_mock_npc_shop_inventory[i].actorId == actorId &&
            g_vm_net_mock_npc_shop_inventory[i].itemId == itemId &&
            strcmp(g_vm_net_mock_npc_shop_inventory[i].scene, scene) == 0)
        {
            existing = (int)i;
            break;
        }
    }
    if (existing < 0 && g_vm_net_mock_npc_shop_inventory_count >=
                            VM_NET_MOCK_NPC_SHOP_INVENTORY_MAX)
    {
        if (errorOut)
            *errorOut = "NPC 专属库存已满";
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_npc_shop_inventory(scene,actor_id,item_id,unit_price,enabled) "
             "VALUES(X'%s',%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "unit_price=VALUES(unit_price),enabled=VALUES(enabled)",
             sceneHex, actorId, itemId, unitPrice, enabled ? 1u : 0u);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    memset(&row, 0, sizeof(row));
    snprintf(row.scene, sizeof(row.scene), "%s", scene);
    row.actorId = actorId;
    row.itemId = itemId;
    row.unitPrice = unitPrice;
    row.enabled = enabled;
    if (existing >= 0)
        g_vm_net_mock_npc_shop_inventory[existing] = row;
    else
        g_vm_net_mock_npc_shop_inventory[
            g_vm_net_mock_npc_shop_inventory_count++] = row;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] npc_shop_inventory_save scene=%s actor=%u item=%u price=%u enabled=%u\n",
           scene, actorId, itemId, unitPrice, enabled ? 1u : 0u);
    return true;
}

/* Bulk edits are committed as one unit.  A category-wide add must never leave
 * half of its selected products configured when a later row is rejected or a
 * MySQL write fails. */
static bool vm_net_mock_npc_shop_inventory_admin_save_many(
    const char *scene, u32 actorId, u16 serviceKind, const u32 *itemIds,
    u32 itemCount, u32 requestedUnitPrice, bool enabled,
    const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_npc_shop_inventory[0].scene) * 2 + 1];
    char query[768];
    u32 newRows = 0;
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "NPC shop inventory is invalid";
    if (!vm_net_mock_native_npc_db_load() ||
        !vm_net_mock_scene_name_is_safe(scene) || actorId == 0 ||
        !vm_net_mock_npc_shop_inventory_service_kind_is_supported(serviceKind) ||
        itemIds == NULL || itemCount == 0 ||
        itemCount > VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
    {
        return false;
    }
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_find_shop_catalog_item(itemIds[i]);
        bool existing = false;

        if (itemIds[i] == 0 || item == NULL ||
            !vm_net_mock_npc_shop_inventory_item_matches_service(
                item, serviceKind) ||
            vm_net_mock_npc_shop_inventory_resolve_unit_price(
                item, requestedUnitPrice) == 0)
        {
            if (errorOut)
                *errorOut = item == NULL ? "物品目录中不存在该物品" :
                            "所选物品不属于当前商人可售分类";
            return false;
        }
        for (u32 prior = 0; prior < i; ++prior)
        {
            if (itemIds[prior] == itemIds[i])
            {
                if (errorOut)
                    *errorOut = "库存选择中包含重复物品";
                return false;
            }
        }
        for (u32 row = 0; row < g_vm_net_mock_npc_shop_inventory_count;
             ++row)
        {
            if (g_vm_net_mock_npc_shop_inventory[row].actorId == actorId &&
                g_vm_net_mock_npc_shop_inventory[row].itemId == itemIds[i] &&
                strcmp(g_vm_net_mock_npc_shop_inventory[row].scene, scene) == 0)
            {
                existing = true;
                break;
            }
        }
        if (!existing)
            ++newRows;
    }
    if (newRows > VM_NET_MOCK_NPC_SHOP_INVENTORY_MAX -
                      g_vm_net_mock_npc_shop_inventory_count)
    {
        if (errorOut)
            *errorOut = "NPC 专属库存已满";
        return false;
    }
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    transactionStarted = true;
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_find_shop_catalog_item(itemIds[i]);
        u32 unitPrice = vm_net_mock_npc_shop_inventory_resolve_unit_price(
            item, requestedUnitPrice);

        snprintf(query, sizeof(query),
                 "INSERT INTO server_npc_shop_inventory(scene,actor_id,item_id,unit_price,enabled) "
                 "VALUES(X'%s',%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
                 "unit_price=VALUES(unit_price),enabled=VALUES(enabled)",
                 sceneHex, actorId, itemIds[i], unitPrice,
                 enabled ? 1u : 0u);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_find_shop_catalog_item(itemIds[i]);
        u32 unitPrice = vm_net_mock_npc_shop_inventory_resolve_unit_price(
            item, requestedUnitPrice);
        u32 row = 0;

        for (; row < g_vm_net_mock_npc_shop_inventory_count; ++row)
        {
            if (g_vm_net_mock_npc_shop_inventory[row].actorId == actorId &&
                g_vm_net_mock_npc_shop_inventory[row].itemId == itemIds[i] &&
                strcmp(g_vm_net_mock_npc_shop_inventory[row].scene, scene) == 0)
                break;
        }
        if (row == g_vm_net_mock_npc_shop_inventory_count)
        {
            memset(&g_vm_net_mock_npc_shop_inventory[row], 0,
                   sizeof(g_vm_net_mock_npc_shop_inventory[row]));
            snprintf(g_vm_net_mock_npc_shop_inventory[row].scene,
                     sizeof(g_vm_net_mock_npc_shop_inventory[row].scene),
                     "%s", scene);
            g_vm_net_mock_npc_shop_inventory[row].actorId = actorId;
            g_vm_net_mock_npc_shop_inventory[row].itemId = itemIds[i];
            ++g_vm_net_mock_npc_shop_inventory_count;
        }
        g_vm_net_mock_npc_shop_inventory[row].unitPrice = unitPrice;
        g_vm_net_mock_npc_shop_inventory[row].enabled = enabled;
    }
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] npc_shop_inventory_save_many scene=%s actor=%u items=%u price=%u enabled=%u\n",
           scene, actorId, itemCount, requestedUnitPrice, enabled ? 1u : 0u);
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_npc_shop_inventory_admin_delete(
    const char *scene, u32 actorId, u32 itemId, const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_npc_shop_inventory[0].scene) * 2 + 1];
    char query[640];
    int existing = -1;

    if (errorOut)
        *errorOut = "NPC inventory item not found";
    if (!vm_net_mock_native_npc_db_load() ||
        !vm_net_mock_scene_name_is_safe(scene) || actorId == 0 || itemId == 0 ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
    {
        return false;
    }
    for (u32 i = 0; i < g_vm_net_mock_npc_shop_inventory_count; ++i)
    {
        if (g_vm_net_mock_npc_shop_inventory[i].actorId == actorId &&
            g_vm_net_mock_npc_shop_inventory[i].itemId == itemId &&
            strcmp(g_vm_net_mock_npc_shop_inventory[i].scene, scene) == 0)
        {
            existing = (int)i;
            break;
        }
    }
    if (existing < 0)
        return false;
    snprintf(query, sizeof(query),
             "DELETE FROM server_npc_shop_inventory "
             "WHERE scene=X'%s' AND actor_id=%u AND item_id=%u",
             sceneHex, actorId, itemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if ((u32)existing + 1 < g_vm_net_mock_npc_shop_inventory_count)
    {
        memmove(&g_vm_net_mock_npc_shop_inventory[existing],
                &g_vm_net_mock_npc_shop_inventory[existing + 1],
                (g_vm_net_mock_npc_shop_inventory_count - (u32)existing - 1) *
                    sizeof(g_vm_net_mock_npc_shop_inventory[0]));
    }
    --g_vm_net_mock_npc_shop_inventory_count;
    memset(&g_vm_net_mock_npc_shop_inventory[
               g_vm_net_mock_npc_shop_inventory_count],
           0, sizeof(g_vm_net_mock_npc_shop_inventory[0]));
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] npc_shop_inventory_delete scene=%s actor=%u item=%u\n",
           scene, actorId, itemId);
    return true;
}

static bool vm_net_mock_npc_shop_inventory_admin_delete_many(
    const char *scene, u32 actorId, const u32 *itemIds, u32 itemCount,
    const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_npc_shop_inventory[0].scene) * 2 + 1];
    char query[640];
    bool transactionStarted = false;
    u32 write = 0;

    if (errorOut)
        *errorOut = "NPC inventory item not found";
    if (!vm_net_mock_native_npc_db_load() ||
        !vm_net_mock_scene_name_is_safe(scene) || actorId == 0 ||
        itemIds == NULL || itemCount == 0 ||
        itemCount > VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
    {
        return false;
    }
    for (u32 i = 0; i < itemCount; ++i)
    {
        bool found = false;

        if (itemIds[i] == 0)
            return false;
        for (u32 prior = 0; prior < i; ++prior)
        {
            if (itemIds[prior] == itemIds[i])
            {
                if (errorOut)
                    *errorOut = "库存选择中包含重复物品";
                return false;
            }
        }
        for (u32 row = 0; row < g_vm_net_mock_npc_shop_inventory_count;
             ++row)
        {
            if (g_vm_net_mock_npc_shop_inventory[row].actorId == actorId &&
                g_vm_net_mock_npc_shop_inventory[row].itemId == itemIds[i] &&
                strcmp(g_vm_net_mock_npc_shop_inventory[row].scene, scene) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (errorOut)
                *errorOut = "所选库存已不存在，请刷新页面";
            return false;
        }
    }
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    transactionStarted = true;
    for (u32 i = 0; i < itemCount; ++i)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM server_npc_shop_inventory "
                 "WHERE scene=X'%s' AND actor_id=%u AND item_id=%u",
                 sceneHex, actorId, itemIds[i]);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;

    for (u32 read = 0; read < g_vm_net_mock_npc_shop_inventory_count; ++read)
    {
        bool removed = false;

        if (g_vm_net_mock_npc_shop_inventory[read].actorId == actorId &&
            strcmp(g_vm_net_mock_npc_shop_inventory[read].scene, scene) == 0)
        {
            for (u32 i = 0; i < itemCount; ++i)
            {
                if (g_vm_net_mock_npc_shop_inventory[read].itemId == itemIds[i])
                {
                    removed = true;
                    break;
                }
            }
        }
        if (!removed)
        {
            if (write != read)
                g_vm_net_mock_npc_shop_inventory[write] =
                    g_vm_net_mock_npc_shop_inventory[read];
            ++write;
        }
    }
    if (write < g_vm_net_mock_npc_shop_inventory_count)
    {
        memset(&g_vm_net_mock_npc_shop_inventory[write], 0,
               (g_vm_net_mock_npc_shop_inventory_count - write) *
                   sizeof(g_vm_net_mock_npc_shop_inventory[0]));
    }
    g_vm_net_mock_npc_shop_inventory_count = write;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] npc_shop_inventory_delete_many scene=%s actor=%u items=%u\n",
           scene, actorId, itemCount);
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_dynamic_npc_column_count_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_dynamic_npc_column_context *context =
        (vm_net_mock_dynamic_npc_column_context *)contextValue;

    if (context == NULL || columnCount != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->count))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_dynamic_npc_exact_kind_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_dynamic_npc_exact_kind_context *context =
        (vm_net_mock_dynamic_npc_exact_kind_context *)contextValue;
    u32 parsedKind = 0;

    if (context == NULL || columnCount != 1 || context->found ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &parsedKind) ||
        parsedKind > VM_NET_MOCK_NPC_KIND_MAX)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->serviceKind = (u16)parsedKind;
    context->found = true;
    return true;
}

/* CREATE TABLE IF NOT EXISTS cannot add a column to an existing production
 * table.  Query INFORMATION_SCHEMA first so this automatic compatibility
 * migration is portable to the older MySQL versions used by existing setups. */
static bool vm_net_mock_dynamic_npc_tasks_ensure_repeatable_column(void)
{
    vm_net_mock_dynamic_npc_column_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_dynamic_npc_tasks' "
            "AND COLUMN_NAME='repeatable'",
            vm_net_mock_dynamic_npc_column_count_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (context.count != 0)
        return true;
    if (!vm_mysql_exec(
            "ALTER TABLE server_dynamic_npc_tasks "
            "ADD COLUMN repeatable TINYINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER task_id"))
    {
        return false;
    }
    printf("[info][mock-admin] dynamic_npc_task_schema migration=repeatable-column action=applied\n");
    return true;
}

/* Existing databases predate customizable NPC service-menu text.  Keep this
 * migration at the configuration owner, and run it before selecting the
 * fields, rather than silently treating a missing column as an empty label. */
static bool vm_net_mock_dynamic_npc_ensure_service_option_columns(void)
{
    vm_net_mock_dynamic_npc_column_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_dynamic_npcs' "
            "AND COLUMN_NAME='service_option_name'",
            vm_net_mock_dynamic_npc_column_count_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (context.count == 0 && !vm_mysql_exec(
            "ALTER TABLE server_dynamic_npcs "
            "ADD COLUMN service_option_name VARBINARY(64) NOT NULL DEFAULT '' "
            "AFTER script_name"))
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_dynamic_npcs' "
            "AND COLUMN_NAME='service_option_description'",
            vm_net_mock_dynamic_npc_column_count_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (context.count == 0 && !vm_mysql_exec(
            "ALTER TABLE server_dynamic_npcs "
            "ADD COLUMN service_option_description VARBINARY(96) NOT NULL DEFAULT '' "
            "AFTER service_option_name"))
    {
        return false;
    }
    printf("[info][mock-admin] dynamic_npc_schema migration=service-option-text action=ready\n");
    return true;
}

static bool vm_net_mock_scene_battle_monster_live_capacity_safe(
    const char *scene, const vm_net_mock_scene_battle_monster_admin_row *rows,
    u32 rowCount)
{
    u8 raw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 payload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u32 rawLen = 0;
    u32 payloadLen = 0;
    u32 staticCount = 0;
    u32 npcReserve = 0;
    u32 configuredNodeCount = 0;
    u32 childReserve = 0;

    if (scene == NULL || rows == NULL ||
        !vm_net_mock_scene_battle_monster_read_overlay_raw(
            scene, raw, sizeof(raw), &rawLen, NULL, 0) ||
        !vm_net_mock_scene_battle_monster_decode_raw_sce(
            raw, rawLen, payload, sizeof(payload), &payloadLen) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            payload, payloadLen, &staticCount))
    {
        return false;
    }
    npcReserve = vm_net_mock_scene_battle_monster_npc_node_reserve(scene);
    for (u32 i = 0; i < rowCount; ++i)
    {
        u32 rowReserve = 0;
        if (!rows[i].enabled)
            continue;
        if (!vm_net_mock_actor_scene_node_reserve(
                rows[i].actorResource, &rowReserve, NULL))
        {
            return false;
        }
        if (rows[i].quantity == 0 ||
            rows[i].quantity > VM_NET_MOCK_SCENE_BATTLE_MONSTER_QUANTITY_MAX ||
            configuredNodeCount >
                VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX -
                    rows[i].quantity ||
            rowReserve >
                (VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX -
                 childReserve) / rows[i].quantity)
            return false;
        configuredNodeCount += rows[i].quantity;
        childReserve += rowReserve * rows[i].quantity;
    }
    if (staticCount > VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX ||
        npcReserve > VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX ||
        configuredNodeCount > VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX ||
        staticCount + npcReserve + childReserve >
            VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX)
    {
        printf("[error][network] mock_scene_battle_monster_capacity_unsafe scene=%s static=%u npc_reserve=%u enabled=%u actor_child_reserve=%u limit=%u action=reject-enter\n",
               scene, staticCount, npcReserve, configuredNodeCount, childReserve,
               VM_NET_MOCK_SCENE_BATTLE_MONSTER_LIVE_NODE_MAX);
        return false;
    }
    return true;
}

static bool vm_net_mock_dynamic_npc_instances_ensure_spawn_enemy_column(void)
{
    vm_net_mock_dynamic_npc_column_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_dynamic_npc_instances' "
            "AND COLUMN_NAME='spawn_enemy_id'",
            vm_net_mock_dynamic_npc_column_count_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (context.count == 0 && !vm_mysql_exec(
            "ALTER TABLE server_dynamic_npc_instances "
            "ADD COLUMN spawn_enemy_id SMALLINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER challenge_enemy_id"))
    {
        return false;
    }
    printf("[info][mock-admin] dynamic_npc_instance_schema migration=spawn-enemy-id action=ready\n");
    return true;
}

/* Every dynamic-NPC scene key is a client resource key.  Both a placement
 * scene and an instance target must name the exact downloadable SCE file;
 * accepting a bare name here makes a distinct SQL key which later collides
 * with the real `*.sce` row.  This is a data migration prerequisite, not a
 * runtime alias: only append the suffix when that exact resource exists. */
static bool vm_net_mock_dynamic_npc_scene_key_canonicalize(
    const char *configuredScene, char *canonicalScene, size_t canonicalCap)
{
    char candidate[64];

    if (canonicalScene == NULL || canonicalCap == 0)
        return false;
    canonicalScene[0] = 0;
    if (configuredScene == NULL || configuredScene[0] == 0 ||
        !vm_net_mock_scene_name_is_download_key(configuredScene))
    {
        return false;
    }
    if (vm_net_mock_str_ends_with(configuredScene, ".sce"))
    {
        if (!vm_net_mock_scene_resource_exists(configuredScene) ||
            strlen(configuredScene) >= canonicalCap)
        {
            return false;
        }
        snprintf(canonicalScene, canonicalCap, "%s", configuredScene);
        return true;
    }
    if (snprintf(candidate, sizeof(candidate), "%s.sce", configuredScene) < 0 ||
        strlen(candidate) >= sizeof(candidate) ||
        !vm_net_mock_scene_resource_exists(candidate) ||
        strlen(candidate) >= canonicalCap)
    {
        return false;
    }
    snprintf(canonicalScene, canonicalCap, "%s", candidate);
    return true;
}

static bool vm_net_mock_dynamic_npc_parent_scene_queue_migration(
    vm_net_mock_dynamic_npc_load_context *context,
    const char *legacyScene, const char *canonicalScene, u32 actorId)
{
    vm_net_mock_dynamic_npc_parent_scene_migration *migration = NULL;

    if (context == NULL || legacyScene == NULL || legacyScene[0] == 0 ||
        canonicalScene == NULL || canonicalScene[0] == 0 || actorId == 0 ||
        context->parentMigrationCount >= VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX)
    {
        return false;
    }
    migration = &context->parentMigrations[context->parentMigrationCount++];
    memset(migration, 0, sizeof(*migration));
    snprintf(migration->legacyScene, sizeof(migration->legacyScene), "%s",
             legacyScene);
    snprintf(migration->canonicalScene, sizeof(migration->canonicalScene), "%s",
             canonicalScene);
    migration->actorId = actorId;
    return true;
}

/* Move all server-owned dependent rows before deleting a legacy parent.  When
 * a correctly keyed parent already exists, it is authoritative for conflicting
 * rows; INSERT IGNORE only carries forward data that has no exact-key owner.
 * The whole move is transactional so a failed migration cannot leave one NPC
 * split between two scene identities. */
static bool vm_net_mock_dynamic_npc_parent_scene_apply_migrations(
    vm_net_mock_dynamic_npc_load_context *context)
{
    if (context == NULL)
        return false;
    for (u32 i = 0; i < context->parentMigrationCount; ++i)
    {
        const vm_net_mock_dynamic_npc_parent_scene_migration *migration =
            &context->parentMigrations[i];
        char legacyHex[sizeof(migration->legacyScene) * 2 + 1];
        char canonicalHex[sizeof(migration->canonicalScene) * 2 + 1];
        char query[2048];
        bool transactionStarted = false;
        vm_net_mock_dynamic_npc_column_context canonicalParent;

        if (vm_mysql_hex_encode(migration->legacyScene,
                                strlen(migration->legacyScene), legacyHex,
                                sizeof(legacyHex)) == 0 ||
            vm_mysql_hex_encode(migration->canonicalScene,
                                strlen(migration->canonicalScene), canonicalHex,
                                sizeof(canonicalHex)) == 0 ||
            !vm_mysql_exec("START TRANSACTION"))
        {
            ++context->parentMigrationFailures;
            printf("[error][mock-admin] dynamic_npc_parent_scene_migration_failed actor=%u from=%s to=%s phase=begin error=%s\n",
                   migration->actorId, migration->legacyScene,
                   migration->canonicalScene, vm_mysql_last_error());
            continue;
        }
        transactionStarted = true;
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO server_dynamic_npcs("
                 "scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,service_option_name,service_option_description,enabled) "
                 "SELECT X'%s',actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,service_option_name,service_option_description,enabled "
                 "FROM server_dynamic_npcs WHERE scene=X'%s' AND actor_id=%u",
                 canonicalHex, legacyHex, migration->actorId);
        if (!vm_mysql_exec(query))
            goto failed;
        /* A legacy key may be deleted only after the exact SCE parent exists
         * in the same transaction.  INSERT IGNORE alone is not evidence of
         * that: it can report success while doing no write, and treating that
         * as a completed migration would orphan the admin target. */
        memset(&canonicalParent, 0, sizeof(canonicalParent));
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM server_dynamic_npcs "
                 "WHERE scene=X'%s' AND actor_id=%u",
                 canonicalHex, migration->actorId);
        if (!vm_mysql_query(query, vm_net_mock_dynamic_npc_column_count_row,
                            &canonicalParent) ||
            canonicalParent.invalid || !canonicalParent.found ||
            canonicalParent.count != 1u)
        {
            printf("[error][mock-admin] dynamic_npc_parent_scene_migration_failed actor=%u from=%s to=%s phase=verify-canonical-parent count=%u error=%s\\n",
                   migration->actorId, migration->legacyScene,
                   migration->canonicalScene, canonicalParent.count,
                   vm_mysql_last_error());
            goto failed;
        }
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO server_dynamic_npc_tasks(scene,actor_id,task_id,repeatable) "
                 "SELECT X'%s',actor_id,task_id,repeatable FROM server_dynamic_npc_tasks "
                 "WHERE scene=X'%s' AND actor_id=%u",
                 canonicalHex, legacyHex, migration->actorId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO server_dynamic_npc_instances("
                "scene,actor_id,target_scene,target_x,target_y,challenge_enemy_id,spawn_enemy_id,minimum_level) "
                 "SELECT X'%s',actor_id,target_scene,target_x,target_y,challenge_enemy_id,spawn_enemy_id,minimum_level "
                 "FROM server_dynamic_npc_instances WHERE scene=X'%s' AND actor_id=%u",
                 canonicalHex, legacyHex, migration->actorId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO server_npc_services("
                 "scene,actor_id,service_kind,sort_order,option_name,option_description) "
                 "SELECT X'%s',actor_id,service_kind,sort_order,option_name,option_description "
                 "FROM server_npc_services WHERE scene=X'%s' AND actor_id=%u",
                 canonicalHex, legacyHex, migration->actorId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO server_npc_shop_inventory(scene,actor_id,item_id,unit_price,enabled) "
                 "SELECT X'%s',actor_id,item_id,unit_price,enabled FROM server_npc_shop_inventory "
                 "WHERE scene=X'%s' AND actor_id=%u",
                 canonicalHex, legacyHex, migration->actorId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(query, sizeof(query),
                 "DELETE FROM server_npc_shop_inventory WHERE scene=X'%s' AND actor_id=%u",
                 legacyHex, migration->actorId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(query, sizeof(query),
                 "DELETE FROM server_npc_services WHERE scene=X'%s' AND actor_id=%u",
                 legacyHex, migration->actorId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(query, sizeof(query),
                 "DELETE FROM server_dynamic_npcs WHERE scene=X'%s' AND actor_id=%u",
                 legacyHex, migration->actorId);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
            goto failed;
        transactionStarted = false;
        ++context->parentMigrated;
        printf("[info][mock-admin] dynamic_npc_parent_scene_migration actor=%u from=%s to=%s action=merged-exact-sce-key\n",
               migration->actorId, migration->legacyScene,
               migration->canonicalScene);
        continue;

failed:
        if (transactionStarted)
            (void)vm_mysql_exec("ROLLBACK");
        ++context->parentMigrationFailures;
        printf("[error][mock-admin] dynamic_npc_parent_scene_migration_failed actor=%u from=%s to=%s phase=transaction error=%s\n",
               migration->actorId, migration->legacyScene,
               migration->canonicalScene, vm_mysql_last_error());
    }
    return context->parentMigrationFailures == 0;
}

static bool vm_net_mock_dynamic_npc_instance_scene_queue_migration(
    vm_net_mock_dynamic_npc_load_context *context,
    const vm_net_mock_dynamic_npc_override *row,
    const char *configuredScene)
{
    vm_net_mock_dynamic_npc_instance_scene_migration *migration = NULL;

    if (context == NULL || row == NULL || configuredScene == NULL ||
        context->migrationCount >= VM_NET_MOCK_DYNAMIC_NPC_OVERRIDE_MAX)
    {
        return false;
    }
    migration = &context->migrations[context->migrationCount++];
    memset(migration, 0, sizeof(*migration));
    snprintf(migration->scene, sizeof(migration->scene), "%s", row->scene);
    snprintf(migration->previousTargetScene,
             sizeof(migration->previousTargetScene), "%s", configuredScene);
    snprintf(migration->canonicalTargetScene,
             sizeof(migration->canonicalTargetScene), "%s",
             row->seed.instanceScene);
    migration->actorId = row->seed.actorId;
    return true;
}

static bool vm_net_mock_dynamic_npc_instance_scene_apply_migrations(
    vm_net_mock_dynamic_npc_load_context *context)
{
    if (context == NULL)
        return false;
    for (u32 i = 0; i < context->migrationCount; ++i)
    {
        const vm_net_mock_dynamic_npc_instance_scene_migration *migration =
            &context->migrations[i];
        char sceneHex[sizeof(migration->scene) * 2 + 1];
        char previousHex[sizeof(migration->previousTargetScene) * 2 + 1];
        char canonicalHex[sizeof(migration->canonicalTargetScene) * 2 + 1];
        char query[768];

        if (vm_mysql_hex_encode(migration->scene, strlen(migration->scene),
                                sceneHex, sizeof(sceneHex)) == 0 ||
            vm_mysql_hex_encode(migration->previousTargetScene,
                                strlen(migration->previousTargetScene),
                                previousHex, sizeof(previousHex)) == 0 ||
            vm_mysql_hex_encode(migration->canonicalTargetScene,
                                strlen(migration->canonicalTargetScene),
                                canonicalHex, sizeof(canonicalHex)) == 0)
        {
            ++context->migrationFailures;
            continue;
        }
        snprintf(query, sizeof(query),
                 "UPDATE server_dynamic_npc_instances SET target_scene=X'%s' "
                 "WHERE scene=X'%s' AND actor_id=%u AND target_scene=X'%s'",
                 canonicalHex, sceneHex, migration->actorId, previousHex);
        if (!vm_mysql_exec(query))
        {
            ++context->migrationFailures;
            printf("[error][mock-admin] dynamic_npc_instance_scene_migration_failed scene=%s actor=%u from=%s to=%s error=%s\n",
                   migration->scene, migration->actorId,
                   migration->previousTargetScene,
                   migration->canonicalTargetScene, vm_mysql_last_error());
            continue;
        }
        ++context->migrated;
        printf("[info][mock-admin] dynamic_npc_instance_scene_migration scene=%s actor=%u from=%s to=%s action=persist-exact-sce-key\n",
               migration->scene, migration->actorId,
               migration->previousTargetScene,
               migration->canonicalTargetScene);
    }
    return context->migrationFailures == 0;
}

static bool vm_net_mock_dynamic_npc_row(void *contextValue,
                                       unsigned int columnCount,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_net_mock_dynamic_npc_load_context *context =
        (vm_net_mock_dynamic_npc_load_context *)contextValue;
    vm_net_mock_dynamic_npc_override row;
    u32 number[14];
    bool hasInstanceConfiguration = false;

    memset(&row, 0, sizeof(row));
    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 21 ||
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
        !vm_net_mock_dynamic_npc_decode_hex(values[9], lengths[9],
                                            row.seed.serviceOptionName,
                                            sizeof(row.seed.serviceOptionName)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[10], lengths[10],
                                            row.seed.serviceOptionDescription,
                                            sizeof(row.seed.serviceOptionDescription)) ||
        !vm_mock_mysql_parse_u32(values[11], lengths[11], &number[5]) || number[5] > 1u ||
        !vm_mock_mysql_parse_u32(values[12], lengths[12], &number[6]) ||
        !vm_mock_mysql_parse_u32(values[13], lengths[13], &number[7]) ||
        number[7] > VM_NET_MOCK_TASK_REPEAT_MONTHLY ||
        !vm_net_mock_dynamic_npc_decode_hex(values[14], lengths[14],
                                            row.seed.instanceScene,
                                            sizeof(row.seed.instanceScene)) ||
        !vm_mock_mysql_parse_u32(values[15], lengths[15], &number[8]) || number[8] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[16], lengths[16], &number[9]) || number[9] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[17], lengths[17], &number[10]) || number[10] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[18], lengths[18], &number[11]) || number[11] > 0xffffu ||
        !vm_mock_mysql_parse_u32(values[19], lengths[19], &number[12]) || number[12] > 0xffu ||
        !vm_mock_mysql_parse_u32(values[20], lengths[20], &number[13]) || number[13] > 1u)
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
    row.seed.taskRepeatPolicy = (u8)number[7];
    row.seed.taskRepeatable = row.seed.taskRepeatPolicy !=
                              VM_NET_MOCK_TASK_REPEAT_NEVER;
    row.seed.instanceX = (u16)number[8];
    row.seed.instanceY = (u16)number[9];
    row.seed.challengeEnemyId = number[10];
    row.seed.instanceSpawnEnemyId = number[11];
    row.seed.instanceMinLevel = (u16)number[12];
    /* The parent `npc_kind` is now only a compatibility projection.  Instance
     * settings belong to the independent child row and may be used by a
     * multi-service NPC whose first selected service is not the instance
     * guide. */
    hasInstanceConfiguration = row.seed.instanceScene[0] != 0 ||
                               row.seed.instanceX != 0 ||
                               row.seed.instanceY != 0 ||
                               row.seed.instanceSpawnEnemyId != 0 ||
                               row.seed.challengeEnemyId != 0;
    if (context->scanningParentSceneMigrations)
    {
        char canonicalScene[sizeof(row.scene)];

        /* Admin placement scenes are exact SCE keys.  Only legacy bare keys
         * are migrated; distinct c00/00 prefixes remain distinct resources. */
        if (!vm_net_mock_str_ends_with(row.scene, ".sce"))
        {
            if (!vm_net_mock_dynamic_npc_scene_key_canonicalize(
                    row.scene, canonicalScene, sizeof(canonicalScene)) ||
                !vm_net_mock_dynamic_npc_parent_scene_queue_migration(
                    context, row.scene, canonicalScene, row.seed.actorId))
            {
                ++context->parentMigrationFailures;
                printf("[error][mock-admin] dynamic_npc_parent_scene_unresolved actor=%u scene=%s action=abort-load reason=exact-sce-resource-not-found-or-migration-queue-full\n",
                       row.seed.actorId, row.scene);
            }
        }
        return true;
    }
    if (hasInstanceConfiguration && row.seed.instanceScene[0] != 0)
    {
        char configuredScene[sizeof(row.seed.instanceScene)];

        snprintf(configuredScene, sizeof(configuredScene), "%s",
                 row.seed.instanceScene);
        if (!vm_net_mock_dynamic_npc_scene_key_canonicalize(
                configuredScene, row.seed.instanceScene,
                sizeof(row.seed.instanceScene)))
        {
            ++context->skipped;
            printf("[error][mock-admin] dynamic_npc_instance_scene_unresolved scene=%s actor=%u target=%s action=skip-row reason=exact-sce-resource-not-found\n",
                   row.scene, row.seed.actorId, configuredScene);
            return true;
        }
        if (strcmp(configuredScene, row.seed.instanceScene) != 0 &&
            !vm_net_mock_dynamic_npc_instance_scene_queue_migration(
                context, &row, configuredScene))
        {
            ++context->skipped;
            printf("[error][mock-admin] dynamic_npc_instance_scene_migration_queue_failed scene=%s actor=%u target=%s action=skip-row\n",
                   row.scene, row.seed.actorId, configuredScene);
            return true;
        }
    }
    if (!vm_net_mock_dynamic_npc_actor_resource_is_supported(
            row.seed.actorResource))
    {
        if (row.enabled)
        {
            row.enabled = false;
            ++context->quarantined;
            printf("[error][mock-admin] dynamic_npc_unsupported_actor scene=%s actor=%u resource=%s action=runtime-disable required_resource=n_woman1.actor\n",
                   row.scene, row.seed.actorId, row.seed.actorResource);
        }
    }
    /* This callback runs while vm_mysql_query is iterating its result set.
     * Never issue a second MySQL query here: the client library correctly
     * rejects it as reentrant and the old fallback then silently exposed the
     * built-in monkey with service rows but without its challenge target.
     * The final SELECT supplies this exact-scene existence bit for direct
     * kind-3 challenges.  Destination-scene guides retain the already local
     * generic catalog predicate. */
    if (hasInstanceConfiguration && row.seed.challengeEnemyId != 0 &&
        ((row.seed.instanceScene[0] == 0 && number[13] == 0) ||
         (row.seed.instanceScene[0] != 0 &&
          !vm_net_mock_monster_enemy_id_known(row.seed.challengeEnemyId))))
    {
        ++context->skipped;
        printf("[error][mock-admin] dynamic_npc_instance_target_unresolved "
               "scene=%s actor=%u target_scene=%s enemy=%u "
               "action=skip-row reason=direct-challenge-requires-enabled-"
               "scene-battle-monster-or-instance-catalog-target "
               "direct_scene_target_exists=%u source=parent-load-query\n",
               row.scene, row.seed.actorId,
               row.seed.instanceScene[0] ? row.seed.instanceScene : "-",
               row.seed.challengeEnemyId, number[13]);
        return true;
    }
    if (row.seed.actorId == 0 || row.seed.x == 0 || row.seed.y == 0 ||
        !vm_net_mock_scene_name_is_safe(row.scene) ||
        row.seed.displayName[0] == 0 ||
        strlen(row.seed.displayName) >= 30 ||
        strlen(row.seed.actorResource) >= 30 ||
        strlen(row.seed.scriptName) >= 32 ||
        strlen(row.seed.serviceOptionName) >= sizeof(row.seed.serviceOptionName) ||
        strlen(row.seed.serviceOptionDescription) >= sizeof(row.seed.serviceOptionDescription) ||
        !vm_net_mock_str_ends_with(row.seed.actorResource, ".actor") ||
        (row.seed.scriptName[0] != 0 &&
         !vm_net_mock_str_ends_with(row.seed.scriptName, ".xse")) ||
        (hasInstanceConfiguration &&
         ((row.seed.instanceScene[0] == 0 && row.seed.challengeEnemyId == 0) ||
          (row.seed.instanceScene[0] != 0 &&
           (!vm_net_mock_str_ends_with(row.seed.instanceScene, ".sce") ||
            !vm_net_mock_scene_name_is_safe(row.seed.instanceScene) ||
            row.seed.instanceX == 0 || row.seed.instanceY == 0 ||
            !vm_net_mock_scene_resource_exists(row.seed.instanceScene))) ||
          row.seed.instanceMinLevel == 0)) ||
        !vm_net_mock_scene_npc_validate_actor_resource(&row.seed,
                                                        "dynamic-npc-db") ||
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

static bool vm_net_mock_dynamic_npc_db_query_rows(
    vm_net_mock_dynamic_npc_load_context *context)
{
    return vm_mysql_query(
        "SELECT HEX(scene),actor_id,pos_x,pos_y,npc_kind,orientation,"
        "HEX(actor_resource),HEX(display_name),HEX(script_name),"
        "HEX(service_option_name),HEX(service_option_description),enabled,"
        "COALESCE(server_dynamic_npc_tasks.task_id,0),"
        "COALESCE(server_dynamic_npc_tasks.repeatable,0),"
        "COALESCE(HEX(server_dynamic_npc_instances.target_scene),''),"
        "COALESCE(server_dynamic_npc_instances.target_x,0),"
        "COALESCE(server_dynamic_npc_instances.target_y,0),"
        "COALESCE(server_dynamic_npc_instances.challenge_enemy_id,0),"
        "COALESCE(server_dynamic_npc_instances.spawn_enemy_id,0),"
        "COALESCE(server_dynamic_npc_instances.minimum_level,1),"
        "EXISTS(SELECT 1 FROM server_scene_battle_monsters AS sbm "
        "WHERE sbm.scene=server_dynamic_npcs.scene "
        "AND sbm.monster_id=COALESCE(server_dynamic_npc_instances.challenge_enemy_id,0) "
        "AND sbm.enabled=1) "
        "FROM server_dynamic_npcs LEFT JOIN server_dynamic_npc_tasks "
        "USING(scene,actor_id) LEFT JOIN server_dynamic_npc_instances "
        "USING(scene,actor_id) ORDER BY scene,actor_id",
        vm_net_mock_dynamic_npc_row, context);
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

    if (!vm_net_mock_native_npc_db_load() ||
        !vm_net_mock_npc_service_options_table_ensure() ||
        !vm_net_mock_scene_battle_monster_schema_ensure() ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_dynamic_npcs ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "pos_x SMALLINT UNSIGNED NOT NULL,pos_y SMALLINT UNSIGNED NOT NULL,"
            "npc_kind SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "orientation SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "actor_resource VARBINARY(64) NOT NULL,display_name VARBINARY(32) NOT NULL,"
            "script_name VARBINARY(64) NOT NULL DEFAULT '',"
            "service_option_name VARBINARY(64) NOT NULL DEFAULT '',"
            "service_option_description VARBINARY(96) NOT NULL DEFAULT '',"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_dynamic_npc_tasks ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "task_id INT UNSIGNED NOT NULL,repeatable TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=不可重复,1=不限次数,2=每日,3=每周,4=每月',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id),KEY idx_server_dynamic_npc_tasks_task(task_id),"
            "CONSTRAINT fk_server_dynamic_npc_tasks_npc FOREIGN KEY(scene,actor_id) "
            "REFERENCES server_dynamic_npcs(scene,actor_id) ON DELETE CASCADE) ENGINE=InnoDB") ||
        !vm_net_mock_dynamic_npc_tasks_ensure_repeatable_column() ||
        !vm_net_mock_dynamic_npc_ensure_service_option_columns() ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_dynamic_npc_instances ("
            "scene VARBINARY(64) NOT NULL,actor_id INT UNSIGNED NOT NULL,"
            "target_scene VARBINARY(64) NOT NULL DEFAULT '',"
            "target_x SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "target_y SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "challenge_enemy_id SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "spawn_enemy_id SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "minimum_level TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene,actor_id),"
            "CONSTRAINT fk_server_dynamic_npc_instances_npc FOREIGN KEY(scene,actor_id) "
            "REFERENCES server_dynamic_npcs(scene,actor_id) ON DELETE CASCADE) ENGINE=InnoDB") ||
        !vm_net_mock_dynamic_npc_instances_ensure_spawn_enemy_column())
    {
        printf("[error][mock-admin] dynamic_npc_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    context.scanningParentSceneMigrations = true;
    if (!vm_net_mock_dynamic_npc_db_query_rows(&context) ||
        !vm_net_mock_dynamic_npc_parent_scene_apply_migrations(&context))
    {
        printf("[error][mock-admin] dynamic_npc_db_load migration=parent-scene-exact-sce-key error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    /* The scan deliberately did not publish rows. Reload only after every
     * legacy key has been atomically merged, so no runtime/admin lookup can
     * observe duplicate scene identities. */
    context.scanningParentSceneMigrations = false;
    context.loaded = 0;
    context.skipped = 0;
    context.quarantined = 0;
    context.migrationCount = 0;
    context.migrationFailures = 0;
    g_vm_net_mock_dynamic_npc_override_count = 0;
    memset(g_vm_net_mock_dynamic_npc_overrides, 0,
           sizeof(g_vm_net_mock_dynamic_npc_overrides));
    if (!vm_net_mock_dynamic_npc_db_query_rows(&context))
    {
        printf("[error][mock-admin] dynamic_npc_db_load failed phase=final-load error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    /* The MySQL row callback must not issue nested queries.  Validate the
     * optional destination-scene kind-3 target only after the complete result
     * set has been materialized, then quarantine invalid rows before runtime
     * lookup can expose them. */
    for (u32 i = 0; i < g_vm_net_mock_dynamic_npc_override_count; ++i)
    {
        vm_net_mock_dynamic_npc_override *row =
            &g_vm_net_mock_dynamic_npc_overrides[i];

        if (row->seed.instanceSpawnEnemyId == 0)
            continue;
        if (row->seed.instanceScene[0] == 0 ||
            !vm_net_mock_scene_battle_monster_target_ready(
                row->seed.instanceScene, row->seed.instanceSpawnEnemyId))
        {
            row->enabled = false;
            ++context.skipped;
            printf("[error][mock-admin] dynamic_npc_instance_spawn_unresolved scene=%s actor=%u target_scene=%s spawn_enemy=%u action=runtime-disable reason=target-kind3-not-enabled-or-deployed\n",
                   row->scene, row->seed.actorId,
                   row->seed.instanceScene[0] ? row->seed.instanceScene : "-",
                   row->seed.instanceSpawnEnemyId);
        }
    }
    if (!vm_net_mock_dynamic_npc_instance_scene_apply_migrations(&context))
    {
        printf("[error][mock-admin] dynamic_npc_db_load migration=instance-scene-sce-suffix error=persist-failed\n");
        return false;
    }
    for (u32 i = 0; i < g_vm_net_mock_dynamic_npc_override_count; ++i)
    {
        vm_net_mock_dynamic_npc_override *row =
            &g_vm_net_mock_dynamic_npc_overrides[i];
        const char *publishError = NULL;
        if (!row->enabled)
            continue;
        if (!vm_net_mock_ensure_actor_resource_available(
                row->seed.actorResource, &publishError))
        {
            row->enabled = false;
            ++context.quarantined;
            printf("[error][mock-admin] dynamic_npc_quarantine scene=%s actor=%u resource=%s reason=%s action=runtime-disable\n",
                   row->scene, row->seed.actorId, row->seed.actorResource,
                   publishError ? publishError : "resource-invalid");
        }
    }
    g_vm_net_mock_dynamic_npc_db_valid = true;
    printf("[info][mock-admin] dynamic_npc_db_load rows=%u skipped=%u quarantined=%u migrated=%u parent_migrated=%u\n",
           context.loaded, context.skipped, context.quarantined,
           context.migrated, context.parentMigrated);
    return true;
}

/* The database is the parent-record authority for admin mutations.  The
 * in-memory override list is intentionally long-lived for scene delivery, so
 * it can lag a save performed through another service instance or a process
 * that was restarted between two browser POSTs.  Resolve inventory ownership
 * by the exact persisted SCE key instead of treating that cache as a durable
 * identity index. */
static bool vm_net_mock_dynamic_npc_admin_lookup_exact_kind(
    const char *scene, u32 actorId, bool *foundOut, u16 *serviceKindOut)
{
    vm_net_mock_dynamic_npc_exact_kind_context context;
    char sceneHex[64 * 2 + 1];
    char query[512];

    if (foundOut != NULL)
        *foundOut = false;
    if (serviceKindOut != NULL)
        *serviceKindOut = VM_NET_MOCK_NPC_KIND_NORMAL;
    if (!vm_net_mock_dynamic_npc_db_load() || scene == NULL || actorId == 0 ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex,
                            sizeof(sceneHex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT npc_kind FROM server_dynamic_npcs "
             "WHERE scene=X'%s' AND actor_id=%u",
             sceneHex, actorId);
    if (!vm_mysql_query(query, vm_net_mock_dynamic_npc_exact_kind_row,
                        &context) || context.invalid)
    {
        return false;
    }
    if (foundOut != NULL)
        *foundOut = context.found;
    if (serviceKindOut != NULL && context.found)
        *serviceKindOut = context.serviceKind;
    return true;
}

static int vm_net_mock_dynamic_npc_find_override(const char *scene, u32 actorId)
{
    if (!vm_net_mock_dynamic_npc_db_load() || scene == NULL || actorId == 0)
        return -1;
    for (u32 i = 0; i < g_vm_net_mock_dynamic_npc_override_count; ++i)
    {
        if (g_vm_net_mock_dynamic_npc_overrides[i].seed.actorId == actorId &&
            vm_net_mock_scene_names_equal_exact(
                g_vm_net_mock_dynamic_npc_overrides[i].scene, scene))
        {
            return (int)i;
        }
    }
    return -1;
}

/* Admin edits are keyed by the exact resource filename printed in the scene
 * picker. A legacy bare key is not an accepted identity for inventory POSTs. */
static int vm_net_mock_dynamic_npc_find_override_exact(const char *scene,
                                                        u32 actorId)
{
    if (!vm_net_mock_dynamic_npc_db_load() || scene == NULL || actorId == 0)
        return -1;
    for (u32 i = 0; i < g_vm_net_mock_dynamic_npc_override_count; ++i)
    {
        if (g_vm_net_mock_dynamic_npc_overrides[i].seed.actorId == actorId &&
            strcmp(g_vm_net_mock_dynamic_npc_overrides[i].scene, scene) == 0)
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
    const vm_net_mock_npc_service_option *serviceOptions,
    u32 serviceOptionCount, bool replaceServiceOptions,
    const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_dynamic_npc_overrides[0].scene) * 2 + 1];
    char actorHex[sizeof(seed->actorResource) * 2 + 1];
    char nameHex[sizeof(seed->displayName) * 2 + 1];
    char scriptHex[sizeof(seed->scriptName) * 2 + 1];
    char serviceOptionNameHex[sizeof(seed->serviceOptionName) * 2 + 1];
    char serviceOptionDescriptionHex[sizeof(seed->serviceOptionDescription) * 2 + 1];
    char instanceSceneHex[sizeof(seed->instanceScene) * 2 + 1];
    char query[2048];
    int existing = -1;
    vm_net_mock_dynamic_npc_override row;
    vm_net_mock_scene_npcinfo_seed normalizedSeed;
    const char *publishError = NULL;
    bool transactionStarted = false;
    bool hasInstanceService = false;
    bool hasInstanceTeleport = false;
    bool hasInstanceChallenge = false;

    if (errorOut)
        *errorOut = "invalid dynamic npc";
    if (seed == NULL)
        return false;
    normalizedSeed = *seed;
    normalizedSeed.taskRepeatPolicy =
        vm_net_mock_task_repeat_policy_from_seed(&normalizedSeed);
    normalizedSeed.taskRepeatable = normalizedSeed.taskRepeatPolicy !=
                                    VM_NET_MOCK_TASK_REPEAT_NEVER;
    seed = &normalizedSeed;
    if (!vm_net_mock_dynamic_npc_db_load() ||
        !vm_net_mock_npc_service_options_table_ensure())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (!vm_net_mock_dynamic_npc_actor_resource_is_supported(
            seed->actorResource))
    {
        if (errorOut)
            *errorOut = "n_girl.actor 不能作为动态 NPC 使用；请改选 n_woman1.actor";
        return false;
    }
    if (replaceServiceOptions &&
        !vm_net_mock_npc_service_options_validate(serviceOptions,
                                                   serviceOptionCount))
    {
        if (errorOut)
            *errorOut = "NPC service options are invalid";
        return false;
    }
    hasInstanceService = replaceServiceOptions
                             ? (vm_net_mock_npc_service_options_has_kind(
                                    serviceOptions, serviceOptionCount,
                                    VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE) ||
                                vm_net_mock_npc_service_options_has_kind(
                                    serviceOptions, serviceOptionCount,
                                    VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE))
                             : vm_net_mock_npc_service_kind_uses_instance_config(
                                   seed->kind);
    hasInstanceTeleport = replaceServiceOptions
                              ? vm_net_mock_npc_service_options_has_kind(
                                    serviceOptions, serviceOptionCount,
                                    VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE)
                              : seed->kind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE &&
                                    seed->instanceScene[0] != 0;
    /* New kind-10 rows always use the source scene's live kind-3 node.  Keep
     * kind-6-without-a-target as a compatibility representation for existing
     * challenge-only NPCs until an administrator next saves the split form. */
    hasInstanceChallenge = hasInstanceService &&
                           seed->challengeEnemyId != 0 &&
                           (replaceServiceOptions
                                ? (vm_net_mock_npc_service_options_has_kind(
                                       serviceOptions, serviceOptionCount,
                                       VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE) ||
                                   seed->instanceScene[0] == 0)
                                : seed->instanceScene[0] == 0);
    if (!vm_net_mock_scene_name_is_safe(scene) ||
        seed->actorId == 0 || seed->x == 0 || seed->y == 0 ||
        seed->kind > VM_NET_MOCK_NPC_KIND_MAX ||
        seed->displayName[0] == 0 || strlen(seed->displayName) >= 30 ||
        seed->actorResource[0] == 0 || strlen(seed->actorResource) >= 30 ||
        strlen(seed->scriptName) >= 32 ||
        strlen(seed->serviceOptionName) >= sizeof(seed->serviceOptionName) ||
        strlen(seed->serviceOptionDescription) >= sizeof(seed->serviceOptionDescription) ||
        !vm_net_mock_str_ends_with(seed->actorResource, ".actor") ||
        (seed->scriptName[0] != 0 &&
         !vm_net_mock_str_ends_with(seed->scriptName, ".xse")) ||
        (hasInstanceService &&
         ((!hasInstanceTeleport && !hasInstanceChallenge) ||
          (hasInstanceTeleport &&
           (!vm_net_mock_str_ends_with(seed->instanceScene, ".sce") ||
            !vm_net_mock_scene_name_is_safe(seed->instanceScene) ||
            seed->instanceX == 0 || seed->instanceY == 0 ||
            !vm_net_mock_scene_resource_exists(seed->instanceScene))) ||
          (hasInstanceChallenge &&
           !vm_net_mock_scene_battle_monster_configured_target_exists(
               scene, seed->challengeEnemyId)) ||
          (seed->instanceSpawnEnemyId != 0 &&
           (!hasInstanceTeleport ||
            !vm_net_mock_scene_battle_monster_target_ready(
                seed->instanceScene, seed->instanceSpawnEnemyId))) ||
          seed->instanceMinLevel == 0 || seed->instanceMinLevel > 0xffu ||
          seed->challengeEnemyId > 0xffffu ||
          seed->actorId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)) ||
        !vm_net_mock_scene_npc_validate_actor_resource(&normalizedSeed,
                                                        "dynamic-npc-admin-save") ||
        (seed->scriptName[0] != 0 &&
         !vm_net_mock_open_server_data_resource(seed->scriptName, ".xse",
                                                NULL, NULL, 0)))
    {
        if (errorOut)
            *errorOut = "NPC fields are invalid or the server Actor/XSE resource is unavailable";
        return false;
    }
    if (!vm_net_mock_ensure_actor_resource_available(seed->actorResource,
                                                      &publishError))
    {
        if (errorOut)
            *errorOut = publishError ? publishError : "Actor resource is invalid";
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
                             scriptHex, sizeof(scriptHex)) == 0) ||
        (seed->serviceOptionName[0] != 0 &&
         vm_mysql_hex_encode(seed->serviceOptionName,
                             strlen(seed->serviceOptionName),
                             serviceOptionNameHex,
                             sizeof(serviceOptionNameHex)) == 0) ||
        (seed->serviceOptionDescription[0] != 0 &&
         vm_mysql_hex_encode(seed->serviceOptionDescription,
                             strlen(seed->serviceOptionDescription),
                             serviceOptionDescriptionHex,
                             sizeof(serviceOptionDescriptionHex)) == 0))
    {
        if (errorOut)
            *errorOut = "NPC text encoding failed";
        return false;
    }
    if (seed->scriptName[0] == 0)
        scriptHex[0] = 0;
    if (seed->serviceOptionName[0] == 0)
        serviceOptionNameHex[0] = 0;
    if (seed->serviceOptionDescription[0] == 0)
        serviceOptionDescriptionHex[0] = 0;
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
             "INSERT INTO server_dynamic_npcs(scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,service_option_name,service_option_description,enabled) "
             "VALUES(X'%s',%u,%u,%u,%u,%u,X'%s',X'%s',X'%s',X'%s',X'%s',%u) "
             "ON DUPLICATE KEY UPDATE pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),"
             "npc_kind=VALUES(npc_kind),orientation=VALUES(orientation),"
             "actor_resource=VALUES(actor_resource),display_name=VALUES(display_name),"
             "script_name=VALUES(script_name),service_option_name=VALUES(service_option_name),"
             "service_option_description=VALUES(service_option_description),enabled=VALUES(enabled)",
             sceneHex, seed->actorId, seed->x, seed->y, seed->kind,
             seed->orientation, actorHex, nameHex, scriptHex,
             serviceOptionNameHex, serviceOptionDescriptionHex, enabled ? 1u : 0u);
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    transactionStarted = true;
    if (!vm_mysql_exec(query))
    {
        goto failed;
    }
    if (hasInstanceService)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_dynamic_npc_instances(scene,actor_id,target_scene,target_x,target_y,challenge_enemy_id,spawn_enemy_id,minimum_level) "
                 "VALUES(X'%s',%u,X'%s',%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
                 "target_scene=VALUES(target_scene),target_x=VALUES(target_x),target_y=VALUES(target_y),"
                 "challenge_enemy_id=VALUES(challenge_enemy_id),spawn_enemy_id=VALUES(spawn_enemy_id),minimum_level=VALUES(minimum_level)",
                 sceneHex, seed->actorId, instanceSceneHex, seed->instanceX,
                 seed->instanceY, seed->challengeEnemyId,
                 seed->instanceSpawnEnemyId, seed->instanceMinLevel);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM server_dynamic_npc_instances WHERE scene=X'%s' AND actor_id=%u",
                 sceneHex, seed->actorId);
    }
    if (!vm_mysql_exec(query))
        goto failed;
    if (seed->taskId != 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_dynamic_npc_tasks(scene,actor_id,task_id,repeatable) "
                 "VALUES(X'%s',%u,%u,%u) ON DUPLICATE KEY UPDATE "
                 "task_id=VALUES(task_id),repeatable=VALUES(repeatable)",
                 sceneHex, seed->actorId, seed->taskId,
                 (u32)seed->taskRepeatPolicy);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM server_dynamic_npc_tasks WHERE scene=X'%s' AND actor_id=%u",
                 sceneHex, seed->actorId);
    }
    if (!vm_mysql_exec(query))
        goto failed;
    if (replaceServiceOptions &&
        !vm_net_mock_npc_service_options_replace_in_transaction(
            scene, seed->actorId, serviceOptions, serviceOptionCount,
            errorOut))
    {
        goto failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
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
    printf("[info][mock-admin] dynamic_npc_save scene=%s actor=%u enabled=%u kind=%u service_option=%s task=%u repeat_policy=%u pos=(%u,%u) instance=%s@(%u,%u) challenge_enemy=%u spawn_enemy=%u spawn_source=SCE2-kind3 min_level=%u actor_res=%s script=%s\n",
           scene, seed->actorId, enabled ? 1u : 0u, seed->kind,
           seed->serviceOptionName[0] ? seed->serviceOptionName : "-",
           seed->taskId, (u32)seed->taskRepeatPolicy, seed->x, seed->y,
           seed->instanceScene[0] ? seed->instanceScene : "-",
           seed->instanceX, seed->instanceY, seed->challengeEnemyId,
           seed->instanceSpawnEnemyId,
           seed->instanceMinLevel, seed->actorResource,
           seed->scriptName[0] ? seed->scriptName : "-");
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_dynamic_npc_admin_delete_override(
    const char *scene, u32 actorId, const char **errorOut)
{
    char sceneHex[sizeof(g_vm_net_mock_dynamic_npc_overrides[0].scene) * 2 + 1];
    char query[512];
    int existing = -1;
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "dynamic npc override not found";
    if (!vm_net_mock_dynamic_npc_db_load() ||
        !vm_net_mock_npc_service_options_table_ensure() ||
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
    if (!vm_mysql_exec("START TRANSACTION"))
        goto failed;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "DELETE FROM server_npc_services WHERE scene=X'%s' AND actor_id=%u",
             sceneHex, actorId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_npc_shop_inventory WHERE scene=X'%s' AND actor_id=%u",
             sceneHex, actorId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_dynamic_npcs WHERE scene=X'%s' AND actor_id=%u",
             sceneHex, actorId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
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

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
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
            !vm_net_mock_scene_names_equal_exact(row->scene, scene))
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
        int overrideIndex = vm_net_mock_dynamic_npc_find_override_exact(
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

        if (strcmp(row->scene, scene) != 0)
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
    u32 len = 0;
    u32 start = 0;
    u32 count = 0;
    u32 total = 0;

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
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
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
            if (vm_net_mock_scene_is_shushan_south_gate(scene) &&
                seed.kind == VM_NET_MOCK_NPC_KIND_NORMAL &&
                strcmp(seed.displayName,
                       "\xb2\xd4\xb9\xc5") == 0) /* 苍古 */
            {
                seed.kind = VM_NET_MOCK_NPC_KIND_ARENA_MASTER;
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
                if (!vm_net_mock_native_npc_override_apply(scene, &seed))
                {
                    printf("[info][network] native_npc_overlay scene=%s actor=%u action=disabled\n",
                           scene, seed.actorId);
                }
                else
                {
                    seeds[count++] = seed;
                    total += 1;
                }
            }
            else
            {
                /* The client can consume only the selected subset.  Keep the
                 * total as a source count when the catalog cap is reached;
                 * an omitted row cannot be interacted with in that packet. */
                total += 1;
            }
            if (end > off)
                off = end - 1;
        }
    }
    if (totalOut)
        *totalOut = total;
    return count;
}

/* Admin discovery reads the SCE source directly so a disabled native actor
 * remains editable.  It intentionally uses the same source exclusions and
 * deterministic id derivation as the runtime catalog. */
static u32 vm_net_mock_native_npc_admin_list(
    const char *scene, vm_net_mock_native_npc_admin_row *rows, u32 rowCap)
{
    vm_net_mock_scene_npcinfo_seed occupied[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    u8 data[8192];
    u32 len = 0;
    u32 start = 0;
    u32 occupiedCount = 0;
    u32 count = 0;

    if (scene == NULL || rows == NULL || rowCap == 0 ||
        vm_net_mock_scene_is_penglai02(scene) ||
        vm_net_mock_scene_is_linan_south_gate(scene))
    {
        return 0;
    }
    memset(rows, 0, sizeof(*rows) * rowCap);
    memset(occupied, 0, sizeof(occupied));
    occupiedCount = vm_net_mock_append_service_scene_npcinfo_seeds(
        scene, occupied, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX);
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    start = vm_net_mock_scene_payload_start(data, len);
    if (len == 0 || start == 0)
        return 0;

    for (u32 off = start; off + 8 <= len && count < rowCap; ++off)
    {
        vm_net_mock_scene_npcinfo_seed seed;
        u32 end = 0;
        bool duplicate = false;
        int overrideIndex = -1;

        if (!vm_net_mock_parse_sce_interactive_npc_at(data, len, off,
                                                      &seed, &end))
        {
            continue;
        }
        if (vm_net_mock_scene_is_penglai01(scene) &&
            (strcmp(seed.scriptName, "task0.xse") != 0 ||
             strcmp(seed.displayName,
                    "\xb4\xf3\xcf\xc0\xb9\xf9\xbe\xb8") != 0))
        {
            if (end > off)
                off = end - 1;
            continue;
        }
        for (u32 i = 0; i < occupiedCount; ++i)
        {
            if (occupied[i].x == seed.x && occupied[i].y == seed.y &&
                strcmp(occupied[i].scriptName, seed.scriptName) == 0 &&
                strcmp(occupied[i].displayName, seed.displayName) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            u32 candidate = 20000u +
                (vm_net_mock_scene_npcinfo_hash(scene, &seed) % 40000u);
            bool collision = true;

            while (collision)
            {
                collision = false;
                for (u32 i = 0; i < occupiedCount; ++i)
                {
                    if (occupied[i].actorId == candidate)
                    {
                        candidate = candidate == 59999u ? 20000u : candidate + 1u;
                        collision = true;
                        break;
                    }
                }
            }
            seed.actorId = candidate;
            overrideIndex = vm_net_mock_native_npc_override_find_exact(
                scene, seed.actorId);
            rows[count].seed = seed;
            rows[count].enabled = overrideIndex < 0 ||
                                  g_vm_net_mock_native_npc_overrides[
                                      overrideIndex].enabled;
            rows[count].overridden = overrideIndex >= 0;
            if (overrideIndex >= 0)
            {
                rows[count].seed.kind =
                    g_vm_net_mock_native_npc_overrides[overrideIndex].serviceKind;
            }
            ++count;
            if (rows[count - 1].enabled &&
                occupiedCount < VM_NET_MOCK_SCENE_NPC_CATALOG_MAX)
            {
                occupied[occupiedCount++] = seed;
                occupied[occupiedCount - 1].kind = rows[count - 1].seed.kind;
            }
        }
        if (end > off)
            off = end - 1;
    }
    return count;
}

static bool vm_net_mock_get_scene_center_spawn_from_sce(const char *scene,
                                                         u16 *xOut,
                                                         u16 *yOut);

static bool vm_net_mock_scene_client_content_ready(
    const char *scene, char *missingOut, size_t missingOutCap)
{
    u8 data[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    vm_net_mock_sce_entity_list entityList;
    u32 len = 0;
    u32 start = 0;

    if (scene == NULL || scene[0] == 0)
        return false;
    if (vm_net_mock_content_client_resource_pending(
            g_vm_mock_service_active_client_id, scene))
    {
        if (missingOut != NULL && missingOutCap != 0)
            snprintf(missingOut, missingOutCap, "%s", scene);
        return false;
    }

    /* Scene-battle deployment publishes the SCE, the field17 body Actor and
     * the field18 exit-effect Actor in one invalidation release.  WT6/1 is
     * only emitted after the target SCE runtime has initialized, and it also
     * supplies the existing evidence boundary for field18.  A field17 body,
     * however, can be looked up later by the battle renderer: it is neither
     * a prerequisite for closing this already-created scene shell nor a
     * cache hit that the server may infer.  Holding 30/2 until an absent
     * field17 WT18/7 arrives deadlocks a reconnect whose client already has
     * the body locally and therefore makes no request.  Leave it pending;
     * the client's normal bare-Actor cache-miss path will request WT18/7 if
     * it truly lacks the resource. */
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    start = vm_net_mock_scene_payload_start(data, len);
    if (len == 0 || start == 0)
        return true;
    if (!vm_net_mock_scene_battle_monster_parse_entity_list(
            data, len, &entityList))
        return false;
    for (u32 combatOrdinal = 0;
         combatOrdinal < entityList.combatRecordCount; ++combatOrdinal)
    {
        vm_net_mock_sce_combat_spawn spawn;
        const char *pendingName = NULL;

        if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                data, len, combatOrdinal, &spawn, NULL))
            return false;
        if (vm_net_mock_content_client_resource_pending(
                     g_vm_mock_service_active_client_id,
                     spawn.effectResource))
        {
            pendingName = spawn.effectResource;
        }
        if (pendingName != NULL)
        {
            if (missingOut != NULL && missingOutCap != 0)
                snprintf(missingOut, missingOutCap, "%s", pendingName);
            return false;
        }
    }
    return true;
}

/* WT6/1 proves that the scene itself reached runtime sync.  It does not prove
 * that a kind-3 body Actor was installed: the CBE can defer that DreamFactory
 * lookup until the battle renderer consumes the copied node.  Marking the body
 * as a cache hit here hid a missing Actor and let 4/5 construct a unit with a
 * null visual context. */
static u32 vm_net_mock_scene_client_note_runtime_ready(const char *scene)
{
    u8 data[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    vm_net_mock_sce_entity_list entityList;
    u32 len = 0;
    u32 start = 0;
    u32 readyCount = 0;

    if (scene == NULL || scene[0] == 0)
        return 0;
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    start = vm_net_mock_scene_payload_start(data, len);
    if (len == 0 || start == 0 ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            data, len, &entityList))
    {
        return 0;
    }
    if (vm_net_mock_content_client_mark_resource_runtime_ready(
            g_vm_mock_service_active_client_id, scene, scene))
    {
        ++readyCount;
    }
    for (u32 combatOrdinal = 0;
         combatOrdinal < entityList.combatRecordCount; ++combatOrdinal)
    {
        vm_net_mock_sce_combat_spawn spawn;

        if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                data, len, combatOrdinal, &spawn, NULL))
        {
            return readyCount;
        }
        if (vm_net_mock_content_client_mark_resource_runtime_ready(
                g_vm_mock_service_active_client_id, spawn.effectResource,
                scene))
        {
            ++readyCount;
        }
    }
    if (readyCount != 0)
    {
        printf("[info][network] mock_scene_runtime_content_ready scene=%s "
               "cache_hits=%u combat_records=%u "
               "evidence=WT6/1-after-scene-runtime-init\n",
               scene, readyCount, entityList.combatRecordCount);
    }
    return readyCount;
}

static bool vm_net_mock_prepare_scene_enter_resources(vm_net_mock_scene_change_target *target,
                                                      char *missingOut,
                                                      size_t missingOutCap)
{
    if (missingOut != NULL && missingOutCap != 0)
        missingOut[0] = 0;
    if (target == NULL || target->scene[0] == 0)
        return false;
    if (target->needsSceneDownload &&
        vm_net_mock_scene_resource_exists(target->scene))
    {
        target->needsSceneDownload = false;
        if (target->x == 0 && target->y == 0)
        {
            u16 cx = 0;
            u16 cy = 0;

            if (vm_net_mock_get_scene_reasonable_spawn_from_sce(
                    target->scene, &cx, &cy, NULL))
            {
                target->x = cx;
                target->y = cy;
                target->hasSceEntry = true;
            }
        }
    }
    if (target->needsSceneDownload)
    {
        if (missingOut != NULL && missingOutCap != 0)
            snprintf(missingOut, missingOutCap, "%s", target->scene);
        return false;
    }
    if (!vm_net_mock_scene_client_content_ready(target->scene, missingOut,
                                                 missingOutCap))
    {
        target->needsSceneDownload = true;
        return false;
    }
    /*
     * A real server does not inspect the client's writable resource cache.
     * It sends the scene-enter contract and, if the client lacks resources,
     * the client drives WT 18/7 chunk requests. Resource completion is therefore
     * observed from the final 18/7 response, not from JHOnlineData contents.
     */
    return true;
}

static void vm_net_mock_defer_scene_enter_completion(const vm_net_mock_scene_change_target *target,
                                                      const char *phase,
                                                      const char *missingResource)
{
    char missingUtf8[128];

    if (target == NULL || target->scene[0] == 0)
        return;
    g_vm_net_mock_last_scene_change_target = *target;
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
        if (!vm_net_mock_scene_names_equal_exact(portal.targetScene, targetScene))
            continue;
        if (portalOut)
            *portalOut = portal;
        return true;
    }
    return false;
}

static bool vm_net_mock_find_sce_named_portal_at_pos(const char *scene, u16 x, u16 y,
                                                      u16 targetEntryId,
                                                      vm_net_mock_sce_named_portal *portalOut)
{
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 start = vm_net_mock_scene_payload_start(data, len);

    if (portalOut != NULL)
        memset(portalOut, 0, sizeof(*portalOut));
    if (len == 0 || start == 0 || targetEntryId == 0)
        return false;

    for (u32 off = start; off + 12 <= len; ++off)
    {
        vm_net_mock_sce_named_portal portal;
        u32 end = 0;

        if (!vm_net_mock_parse_sce_named_portal_at(data, len, off, &portal, &end))
            continue;
        if (portal.targetEntryId != targetEntryId ||
            x < portal.left || x >= portal.right ||
            y < portal.top || y >= portal.bottom)
        {
            continue;
        }
        if (portalOut != NULL)
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

/*
 * The SCE portal spawn is authoritative for an ordinary map transition, but
 * an explicit recovery (death respawn or the settings "unstuck" action) must
 * additionally leave the player somewhere they can take a movement step.
 *
 * The CBE movement path checks the high nibble of the surrounding MAP cells
 * (CheckMoveCollision -> CheckMapMoveCollision / CheckMapMoveCollisionY2).
 * A portal-gap adjustment alone can push a player sideways into an all-edge
 * cell: 11终南山_02's lower-right entry is the concrete case.  Keep this
 * narrowly scoped to recovery landings; normal portal entry continues to use
 * its exact SCE spawn contract.
 */
enum
{
    VM_NET_MOCK_SCENE_RECOVERY_MAP_DATA_MAX = 16384,
    VM_NET_MOCK_SCENE_RECOVERY_MAP_RAW_MAX = 16384,
    VM_NET_MOCK_SCENE_RECOVERY_MAX_EDGE_PORTALS = 32,
    VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES = 1
};

typedef struct
{
    u8 data[VM_NET_MOCK_SCENE_RECOVERY_MAP_DATA_MAX];
    u32 dataLen;
    u32 cellsOffset;
    u32 columns;
    u32 rows;
    u32 tileWidth;
    u32 tileHeight;
    vm_net_mock_sce_edge_portal portals[VM_NET_MOCK_SCENE_RECOVERY_MAX_EDGE_PORTALS];
    u32 portalCount;
} vm_net_mock_scene_recovery_map;

static bool vm_net_mock_scene_recovery_load_map(const char *scene,
                                                vm_net_mock_scene_recovery_map *map)
{
    u8 sceData[8192];
    u8 raw[VM_NET_MOCK_SCENE_RECOVERY_MAP_RAW_MAX];
    char mapName[64];
    char mapPath[256];
    u32 sceLen = 0;
    u32 sceBase = 0;
    u32 scePayloadStart = 0;
    u32 scePos = 0;
    u32 rawLen = 0;
    u32 declaredLen = 0;
    u32 decodedLen = 0;
    u32 nameCount = 0;
    u32 mapPos = 0;
    u32 mapWidth = 0;
    u32 mapHeight = 0;
    u32 cellCount = 0;
    u32 expectedCells = 0;

    if (map == NULL || scene == NULL || scene[0] == 0)
        return false;
    memset(map, 0, sizeof(*map));
    memset(mapName, 0, sizeof(mapName));

    sceLen = vm_net_mock_load_scene_resource(scene, sceData, sizeof(sceData));
    scePayloadStart = vm_net_mock_scene_payload_start(sceData, sceLen);
    if (scePayloadStart == 0)
    {
        return false;
    }
    for (sceBase = 0; sceBase + 11 <= sceLen && sceBase < 32; ++sceBase)
    {
        if (memcmp(sceData + sceBase, "SCE2", 4) == 0)
            break;
    }
    if (sceBase + 11 > sceLen || sceBase >= 32)
        return false;
    scePos = sceBase + 10;
    if (!vm_net_mock_read_sce_len_string(sceData, sceLen, &scePos,
                                         mapName, sizeof(mapName)) ||
        !vm_net_mock_str_ends_with(mapName, ".map") ||
        !vm_net_mock_open_server_data_resource(mapName, ".map", NULL,
                                               mapPath, sizeof(mapPath)))
    {
        return false;
    }

    /* Skip the SCE's reserved dword, then retain the real edge triggers so
     * the recovery candidate cannot immediately re-enter a portal. */
    if (scePos + 4 > sceLen || scePos + 4 != scePayloadStart)
        return false;
    scePos += 4;
    for (u32 off = scePos; off + 18 <= sceLen; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;

        if (!vm_net_mock_parse_sce_edge_portal_at(sceData, sceLen, off,
                                                  &portal, &end))
        {
            continue;
        }
        if (map->portalCount < VM_NET_MOCK_SCENE_RECOVERY_MAX_EDGE_PORTALS)
            map->portals[map->portalCount++] = portal;
    }

    rawLen = vm_net_mock_load_response_file(mapPath, raw, sizeof(raw));
    if (rawLen < 5)
        return false;
    declaredLen = vm_net_mock_read_le16_at(raw, 0) |
                  ((u32)vm_net_mock_read_le16_at(raw, 2) << 16);
    if (declaredLen == 0 || declaredLen > rawLen - 4)
        return false;
    if (raw[4] == 2)
    {
        decodedLen = vm_net_mock_decode_lzss_resource_stream(raw + 4,
                                                              declaredLen,
                                                              map->data,
                                                              sizeof(map->data));
    }
    else if (raw[4] == 1)
    {
        decodedLen = declaredLen - 1u;
        if (decodedLen > sizeof(map->data))
            return false;
        memcpy(map->data, raw + 5, decodedLen);
    }
    else
    {
        return false;
    }
    if (decodedLen < 20)
        return false;
    map->dataLen = decodedLen;

    nameCount = vm_net_mock_read_le32_at(map->data, 0);
    if (nameCount > 128)
        return false;
    mapPos = 4;
    for (u32 i = 0; i < nameCount; ++i)
    {
        u32 nameLen = 0;
        if (mapPos >= map->dataLen)
            return false;
        nameLen = map->data[mapPos++];
        if (nameLen == 0 || mapPos + nameLen > map->dataLen)
            return false;
        mapPos += nameLen;
    }
    if (mapPos + 16 > map->dataLen)
        return false;
    mapWidth = vm_net_mock_read_le32_at(map->data, mapPos);
    mapHeight = vm_net_mock_read_le32_at(map->data, mapPos + 4);
    map->tileWidth = vm_net_mock_read_le32_at(map->data, mapPos + 8);
    map->tileHeight = vm_net_mock_read_le32_at(map->data, mapPos + 12);
    mapPos += 16;
    if (mapWidth == 0 || mapHeight == 0 || map->tileWidth == 0 ||
        map->tileHeight == 0 || map->tileWidth > mapWidth ||
        map->tileHeight > mapHeight)
    {
        return false;
    }
    map->columns = (mapWidth + map->tileWidth - 1u) / map->tileWidth;
    map->rows = (mapHeight + map->tileHeight - 1u) / map->tileHeight;
    if (map->columns == 0 || map->rows == 0 || map->columns > 4096 || map->rows > 4096 ||
        map->columns > UINT32_MAX / map->rows)
    {
        return false;
    }
    expectedCells = map->columns * map->rows;
    cellCount = (map->dataLen - mapPos) / 4u;
    if (cellCount < expectedCells || mapPos + expectedCells * 4u > map->dataLen)
        return false;
    map->cellsOffset = mapPos;
    return true;
}

static bool vm_net_mock_scene_recovery_candidate_is_clear(
    const vm_net_mock_scene_recovery_map *map, u16 x, u16 y)
{
    u32 tileX = 0;
    u32 tileY = 0;

    if (map == NULL || map->tileWidth == 0 || map->tileHeight == 0)
        return false;
    tileX = (u32)x / map->tileWidth;
    tileY = (u32)y / map->tileHeight;
    if (tileX < VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES ||
        tileY < VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES ||
        tileX + VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES >= map->columns ||
        tileY + VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES >= map->rows)
    {
        return false;
    }

    for (int dy = -(int)VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES;
         dy <= (int)VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES; ++dy)
    {
        for (int dx = -(int)VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES;
             dx <= (int)VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES; ++dx)
        {
            u32 cx = (u32)((int)tileX + dx);
            u32 cy = (u32)((int)tileY + dy);
            u32 cellOffset = map->cellsOffset + (cx * map->rows + cy) * 4u;

            if ((vm_net_mock_read_le32_at(map->data, cellOffset) >> 28) != 0)
                return false;
        }
    }

    for (u32 i = 0; i < map->portalCount; ++i)
    {
        const vm_net_mock_sce_edge_portal *portal = &map->portals[i];
        u32 left = portal->left > VM_NET_MOCK_SCENE_LANDING_SAFE_GAP ?
                       portal->left - VM_NET_MOCK_SCENE_LANDING_SAFE_GAP : 0;
        u32 top = portal->top > VM_NET_MOCK_SCENE_LANDING_SAFE_GAP ?
                      portal->top - VM_NET_MOCK_SCENE_LANDING_SAFE_GAP : 0;
        u32 right = (u32)portal->right + VM_NET_MOCK_SCENE_LANDING_SAFE_GAP;
        u32 bottom = (u32)portal->bottom + VM_NET_MOCK_SCENE_LANDING_SAFE_GAP;

        if ((u32)x >= left && (u32)x <= right &&
            (u32)y >= top && (u32)y <= bottom)
        {
            return false;
        }
    }
    return true;
}

static bool vm_net_mock_adjust_recovery_landing_to_map_safe(const char *scene,
                                                             u16 *x, u16 *y)
{
    vm_net_mock_scene_recovery_map map;
    u16 rawX = 0;
    u16 rawY = 0;
    u16 bestX = 0;
    u16 bestY = 0;
    unsigned long long bestDistance = 0;
    bool found = false;

    if (scene == NULL || x == NULL || y == NULL ||
        !vm_net_mock_scene_recovery_load_map(scene, &map))
    {
        return false;
    }
    rawX = *x;
    rawY = *y;
    for (u32 tileX = VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES;
         tileX + VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES < map.columns; ++tileX)
    {
        for (u32 tileY = VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES;
             tileY + VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES < map.rows; ++tileY)
        {
            u32 candidateX = tileX * map.tileWidth + map.tileWidth / 2u;
            u32 candidateY = tileY * map.tileHeight + map.tileHeight / 2u;
            unsigned long long dx = candidateX > rawX ? candidateX - rawX : rawX - candidateX;
            unsigned long long dy = candidateY > rawY ? candidateY - rawY : rawY - candidateY;
            unsigned long long distance = dx * dx + dy * dy;

            if (candidateX > UINT16_MAX || candidateY > UINT16_MAX ||
                !vm_net_mock_scene_recovery_candidate_is_clear(
                    &map, (u16)candidateX, (u16)candidateY) ||
                (found && distance >= bestDistance))
            {
                continue;
            }
            found = true;
            bestDistance = distance;
            bestX = (u16)candidateX;
            bestY = (u16)candidateY;
        }
    }
    if (!found || (bestX == rawX && bestY == rawY))
        return false;

    *x = bestX;
    *y = bestY;
    printf("[info][network] mock_scene_recovery_map_safe_landing scene=%s raw=(%u,%u) safe=(%u,%u) clearance_tiles=%u evidence=MAP-high-nibble+CheckMoveCollision\n",
           scene, rawX, rawY, bestX, bestY,
           VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES);
    vm_autotest_note("mock_scene_recovery_map_safe_landing scene=%s raw=(%u,%u) safe=(%u,%u) clearance_tiles=%u evidence=MAP-high-nibble+CheckMoveCollision\n",
                     scene, rawX, rawY, bestX, bestY,
                     VM_NET_MOCK_SCENE_RECOVERY_CLEARANCE_TILES);
    return true;
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

static bool vm_net_mock_resolve_sce_named_portal_target(
    const vm_net_mock_sce_named_portal *portal,
    vm_net_mock_scene_change_target *target)
{
    u16 targetX = 0;
    u16 targetY = 0;
    const char *normalizedTarget = NULL;

    if (portal == NULL || target == NULL || portal->targetEntryId == 0 ||
        portal->targetScene[0] == 0 ||
        !vm_net_mock_get_scene_entry_spawn_from_sce(portal->targetScene,
                                                    portal->targetEntryId,
                                                    &targetX, &targetY))
    {
        return false;
    }
    normalizedTarget = vm_net_mock_normalize_scene_name_for_enter(portal->targetScene);
    if (!vm_net_mock_scene_name_is_persistable(normalizedTarget))
        return false;
    memset(target, 0, sizeof(*target));
    snprintf(target->scene, sizeof(target->scene), "%s", normalizedTarget);
    target->x = targetX;
    target->y = targetY;
    target->exitId = portal->targetEntryId;
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
        !vm_net_mock_scene_names_equal_exact(pendingScene, requestedTargetScene))
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
        vm_net_mock_scene_names_equal_exact(portal.targetScene, requestedTargetScene))
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
        if ((role == NULL || !vm_net_mock_scene_names_equal_exact(role->scene, sourceScene)) &&
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
           vm_net_mock_scene_names_equal_exact(a->scene, b->scene);
}

static bool vm_net_mock_scene_change_targets_same_arrival(const vm_net_mock_scene_change_target *a,
                                                          const vm_net_mock_scene_change_target *b)
{
    return a != NULL && b != NULL &&
           a->x == b->x &&
           a->y == b->y &&
           vm_net_mock_scene_names_equal_exact(a->scene, b->scene);
}

static bool vm_net_mock_consume_update_completed_scene_reenter(const vm_net_mock_scene_change_target *target)
{
    const vm_net_mock_scene_change_target *effectiveTarget = target;
    bool restoredFromCompleted = false;
    char updateNameUtf8[128];

    if (!g_vm_net_mock_update_completed_reenter_pending)
        return false;
    /*
     * The instance-enter path can legitimately complete its initial WT30/2
     * before the client discovers that the target SCE is stale and requests
     * WT18/7.  In that case the normal active target has already been cleared,
     * but the completed target is still the only authoritative arrival point.
     * Restore it for this one matching resource callback so the client can
     * re-enter through its native scene loader.  Never bind an unrelated
     * resource update to an old scene target.
     */
    if ((effectiveTarget == NULL || effectiveTarget->scene[0] == 0) &&
        g_vm_net_mock_last_completed_scene_change_target_valid &&
        (g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick) <
            VM_NET_MOCK_COMPLETED_SCENE_REUSE_TICKS &&
        g_vm_net_mock_update_completed_name[0] != 0 &&
        vm_net_mock_scene_names_equal_exact(
            g_vm_net_mock_last_completed_scene_change_target.scene,
            g_vm_net_mock_update_completed_name))
    {
        g_vm_net_mock_last_scene_change_target =
            g_vm_net_mock_last_completed_scene_change_target;
        g_vm_net_mock_last_scene_change_target_valid = true;
        effectiveTarget = &g_vm_net_mock_last_scene_change_target;
        restoredFromCompleted = true;
        printf("[info][screen] scene_target_restore_for_update_reenter scene=%s pos=(%u,%u) file=%s reason=completed-target-resource-match\n",
               effectiveTarget->scene, effectiveTarget->x, effectiveTarget->y,
               g_vm_net_mock_update_completed_name);
        vm_autotest_note("scene_target_restore_for_update_reenter scene=%s pos=(%u,%u) file=%s reason=completed-target-resource-match evidence=WT18/7->scene-loader\n",
                         effectiveTarget->scene, effectiveTarget->x,
                         effectiveTarget->y, g_vm_net_mock_update_completed_name);
    }
    if (effectiveTarget == NULL || effectiveTarget->scene[0] == 0 ||
        (restoredFromCompleted &&
         (g_vm_net_mock_update_completed_name[0] == 0 ||
          !vm_net_mock_scene_names_equal_exact(
              effectiveTarget->scene, g_vm_net_mock_update_completed_name))))
    {
        g_vm_net_mock_update_completed_reenter_pending = false;
        return false;
    }
    g_vm_net_mock_update_completed_reenter_pending = false;
    vm_net_mock_gbk_label_to_utf8(g_vm_net_mock_update_completed_name,
                                  updateNameUtf8,
                                  sizeof(updateNameUtf8));
    printf("[info][screen] screen_mgr allow-update-reenter scene=%s pos=(%u,%u) exit=%u file=%s\n",
           effectiveTarget->scene,
           effectiveTarget->x,
           effectiveTarget->y,
           effectiveTarget->exitId,
           updateNameUtf8);
    vm_autotest_note("screen_mgr allow-update-reenter scene=%s pos=(%u,%u) exit=%u file=%s\n",
                     effectiveTarget->scene,
                     effectiveTarget->x,
                     effectiveTarget->y,
                     effectiveTarget->exitId,
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

/* Settings "unstuck" enters the current scene through an mmGame 16/2 or
 * 16/3 result, so it does not pass the normal 30/1 builder that re-arms the
 * one-shot 27/11 directory.  The client has nevertheless discarded the old
 * scene shell.  Re-arm only this settings recovery path and let the first
 * later WT6/1 consume the catalog after scene_runtime_init_and_sync() has
 * rebuilt the runtime tables.  Do not fold this into
 * mark_direct_scene_enter_completed(): that shared helper also closes
 * startup and named-portal paths whose catalog may already have been sent. */
static void vm_net_mock_mark_settings_unstuck_npc_reseed_pending(
    const vm_net_mock_scene_change_target *target, const char *responseKind)
{
    if (target == NULL || target->scene[0] == 0)
        return;

    vm_net_mock_mark_scene_moveinfo_npc_seed_pending(target->scene);
    printf("[info][network] mock_scene_npc_rearm scene=%s trigger=settings-unstuck response=%s immediate=0 next=WT6/1 evidence=JianghuOL.CBE:0x01012FB4+0x01037998\n",
           target->scene, responseKind ? responseKind : "direct-enter");
    vm_autotest_note("mock_scene_npc_rearm scene=%s trigger=settings-unstuck response=%s immediate=0 next=WT6/1 evidence=JianghuOL.CBE:0x01012FB4+0x01037998\n",
                     target->scene, responseKind ? responseKind : "direct-enter");
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

/* This is deliberately test-only while the startup shell's destruction
 * boundary is validated in an isolated client/service run.  Production
 * startup keeps the WT25/5 control acknowledgement. */
static bool vm_net_mock_startup_sce_direct_enter_test_enabled(void)
{
    const char *value = getenv("CBE_TEST_STARTUP_SCE_DIRECT_ENTER");

    return value != NULL && strcmp(value, "1") == 0;
}

/* Role-select has already built the first scene shell from actorinfo.  A
 * final SCE WT18/7 normally must not turn a later standalone WT25/5 into a
 * scene re-entry.  The test gate below permits one packet-correct comparison
 * through the documented mmGame 16/3(result=2) parser branch; it does not
 * write client state or bypass the parser. */
static void vm_net_mock_arm_startup_sce_install_scene_enter(const char *scene)
{
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_scene_change_target target;
    u16 targetX = 0;
    u16 targetY = 0;
    u32 installGeneration = 0;

    if (g_vm_net_mock_startup_sce_enter_pending ||
        !g_vm_net_mock_title_role_scene_followup_pending ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return;
    }
    installGeneration = vm_net_mock_content_client_resource_install_generation(
        g_vm_mock_service_active_client_id, scene);
    role = vm_net_mock_active_role();
    if (installGeneration == 0 || role == NULL ||
        !vm_net_mock_scene_names_equal_exact(role->scene, scene))
    {
        return;
    }
    if (vm_net_mock_startup_sce_direct_enter_test_enabled())
    {
        targetX = role->x;
        targetY = role->y;
        if (!vm_net_mock_read_current_player_grid(NULL, NULL, &targetX,
                                                   &targetY, NULL, NULL))
        {
            vm_net_mock_adjust_safe_player_pos_for_scene(scene, &targetX,
                                                          &targetY);
        }
        if (targetX == 0 || targetY == 0)
            return;

        memset(&target, 0, sizeof(target));
        snprintf(target.scene, sizeof(target.scene), "%s", scene);
        target.x = targetX;
        target.y = targetY;
        target.mapType = 2;
        target.hasSceEntry = true;
        target.needsSceneDownload = false;
        g_vm_net_mock_startup_sce_enter_target = target;
        g_vm_net_mock_startup_sce_enter_install_generation = installGeneration;
        g_vm_net_mock_startup_sce_enter_armed_tick = g_schedulerTick;
        g_vm_net_mock_startup_sce_enter_pending = true;
        printf("[info][network] mock_startup_sce_install_scene_enter_test_armed scene=%s pos=(%u,%u) install_generation=%u request=WT25/5 response=16/3-result2-once evidence=WT18/7+mmGame:0x11CE->0x0BCC\n",
               target.scene, target.x, target.y, installGeneration);
        vm_autotest_note("mock_startup_sce_install_scene_enter_test_armed scene=%s pos=(%u,%u) install_generation=%u request=WT25/5 response=16/3-result2-once test_gate=CBE_TEST_STARTUP_SCE_DIRECT_ENTER\n",
                         target.scene, target.x, target.y, installGeneration);
        return;
    }
    printf("[info][network] mock_startup_sce_install_scene_enter_suppressed scene=%s install_generation=%u action=control-ack-only reason=startup-scene-rebuild-test-disabled evidence=mmGame:0x11CE->0x0BCC+runtime-array-lifetime\n",
           scene, installGeneration);
    vm_autotest_note("mock_startup_sce_install_scene_enter_suppressed scene=%s install_generation=%u action=control-ack-only reason=startup-scene-rebuild-test-disabled evidence=mmGame:0x11CE->0x0BCC+runtime-array-lifetime\n",
                     scene, installGeneration);
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
        !vm_net_mock_scene_names_equal_exact(scene, g_vm_net_mock_last_completed_scene_change_target.scene))
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
        !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_last_scene_change_target.scene, target->scene))
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
        !vm_net_mock_scene_names_equal_exact(scene, g_vm_net_mock_last_current_scene_reload_scene))
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
        !vm_net_mock_scene_names_equal_exact(scene,
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
        !vm_net_mock_scene_names_equal_exact(currentScene, target->scene))
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
    target->mapType = 2;

    if (!vm_net_mock_get_object_string_field(request, requestLen, "mapID", mapId, sizeof(mapId)))
        return;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId);
    (void)vm_net_mock_get_object_u8_field(request, requestLen, "maptype", &target->mapType);
    target->exitId = exitId;
    if (!vm_net_mock_scene_name_is_persistable(mapId))
    {
        /* The map-controller and sMap.dsh contracts use the complete resource
         * key. Do not acknowledge a bare or malformed key by substituting the
         * bootstrap scene: that would make an invalid request move the role to
         * a different map. Leaving target.scene empty makes every builder that
         * consumes this probe reject the request before state mutation. */
        printf("[error][network] mock_scene_target_rejected map=%s exit=%u reason=noncanonical-scene-key contract=exact-sce-key\n",
               mapId[0] ? mapId : "-", exitId);
        return;
    }
    snprintf(target->scene, sizeof(target->scene), "%s", mapId);

    if (g_vm_net_mock_teleport_stone_map_enter_pending &&
        g_vm_net_mock_last_scene_change_target_valid &&
        (g_vm_net_mock_last_scene_change_target.x != 0 ||
         g_vm_net_mock_last_scene_change_target.y != 0) &&
        vm_net_mock_scene_names_equal_exact(mapId, g_vm_net_mock_last_scene_change_target.scene))
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
        vm_net_mock_scene_names_equal_exact(mapId, g_vm_net_mock_last_scene_change_target.scene))
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
        vm_net_mock_scene_names_equal_exact(mapId, g_vm_net_mock_last_completed_scene_change_target.scene))
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

    if (vm_net_mock_scene_name_is_persistable(mapId))
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
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0)
    {
        target->x = 223;
        target->y = 382;
    }
    else if (strcmp(mapId, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0)
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
                 vm_net_mock_scene_names_equal_exact(currentScene, target->scene))
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
    u32 offset = 4;
    vm_net_mock_request_object firstObject;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    if (kind != 0x10 || (subtype != 2 && subtype != 3))
        return false;
    /* `type=0` belongs to the direct scene / named-portal class.  Its actual
     * meaning is resolved from the authoritative source SCE before this
     * detector runs; it can never be a generic teleport-stone confirmation. */
    {
        u8 requestType = 0;
        if (subtype == 3 &&
            vm_net_mock_get_object_u8_field(request, requestLen, "type", &requestType) &&
            requestType == 0)
        {
            return false;
        }
    }
    /* The direct-scene runtime ACK shares 16/3 and an `exitID` field with a
     * teleport selection, but its typed type=0 object carries current X, not
     * a scene exit.  Reject it here even if later stream companions are not
     * implemented yet; it must remain an explicit unresolved runtime stream
     * rather than mutate scene authority through this broad detector. */
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &firstObject) &&
        vm_net_mock_is_scene_runtime_position_ack_16_3_object(&firstObject, NULL))
    {
        return false;
    }
    if (!vm_net_mock_request_contains(request, requestLen, "exitID") &&
        !vm_net_mock_request_contains(request, requestLen, "type"))
    {
        return false;
    }
    if (subtypeOut)
        *subtypeOut = subtype;
    return true;
}

static bool vm_net_mock_get_teleport_stone_catalog_target(
    u32 exitId, vm_net_mock_scene_change_target *target);

static bool vm_net_mock_get_teleport_stone_target(const u8 *request, u32 requestLen,
                                                   vm_net_mock_scene_change_target *target)
{
    u32 exitId = 0;

    if (target == NULL)
        return false;
    memset(target, 0, sizeof(*target));
    if (!(vm_net_mock_get_object_u32_field(request, requestLen, "exitID", &exitId) ||
          vm_net_mock_get_object_u32_field(request, requestLen, "exitid", &exitId)) ||
        exitId == 0 ||
        !vm_net_mock_get_teleport_stone_catalog_target(exitId, target))
    {
        printf("[error][network] mock_teleport_stone_target_unresolved exit=%u "
               "reason=not-an-authored-telestone-scene\n", exitId);
        return false;
    }
    return true;
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

/* Ordinary death recovery first finds a nearby safe sMap sub-scene. If the
 * local map component has none, wMap supplies the nearest town fallback.
 * Both tables mark safety with their authored monster-level value; SCE
 * resources supply the actual player landing point. */
enum
{
    VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX = 192,
    VM_NET_MOCK_DEATH_RESPAWN_WMAP_MAX = 64
};

typedef struct
{
    u32 rowId;
    u32 parentWorldId;
    u32 mapMarkerX;
    u32 mapMarkerY;
    u32 neighbors[4];
    bool isSafe;
    char scene[64];
} vm_net_mock_death_respawn_smap_node;

typedef struct
{
    u32 worldId;
    u32 neighbors[4];
    bool isTown;
} vm_net_mock_death_respawn_wmap_node;

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

static bool vm_net_mock_death_respawn_is_safe_level(const u8 *value,
                                                     u32 valueLen)
{
    /* GBK "无" is the authored monster-level value for safe sub-scenes and
     * for the five town maps.  A local sMap safe sub-scene is a closer
     * recovery target than an adjacent world-map town. */
    return valueLen == 2 && value[0] == 0xce && value[1] == 0xde;
}

static bool vm_net_mock_parse_sce_static_actor_at(const u8 *data, u32 len,
                                                  u32 off, char *actorOut,
                                                  size_t actorOutCap,
                                                  u16 *xOut, u16 *yOut,
                                                  bool *hasPointOut,
                                                  u32 *endOut)
{
    u32 pos = off;

    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (hasPointOut)
        *hasPointOut = false;
    if (endOut)
        *endOut = 0;
    if (actorOut && actorOutCap)
        actorOut[0] = 0;
    if (data == NULL || actorOut == NULL || actorOutCap == 0 || off + 8 > len ||
        vm_net_mock_read_le16_at(data, pos) > 32)
    {
        return false;
    }
    pos += 2;
    if (!vm_net_mock_read_sce_string_field(data, len, &pos, 3,
                                           actorOut, actorOutCap) ||
        !vm_net_mock_str_ends_with(actorOut, ".actor"))
    {
        return false;
    }
    if (pos + 6 <= len && vm_net_mock_read_le16_at(data, pos) == 2)
    {
        if (xOut)
            *xOut = vm_net_mock_read_le16_at(data, pos + 2);
        if (yOut)
            *yOut = vm_net_mock_read_le16_at(data, pos + 4);
        if (hasPointOut)
            *hasPointOut = true;
        pos += 6;
    }
    if (endOut)
        *endOut = pos;
    return true;
}

static bool vm_net_mock_find_sce_static_actor_parent_anchor(
    const u8 *data, u32 actorOffset, u16 *xOut, u16 *yOut)
{
    u32 floor = actorOffset > 64 ? actorOffset - 64 : 0;

    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (data == NULL || actorOffset < 16)
        return false;
    for (u32 candidate = actorOffset; candidate > floor; --candidate)
    {
        u16 x = 0;
        u16 y = 0;

        if (candidate + 16 > actorOffset ||
            vm_net_mock_read_le16_at(data, candidate) != 5 ||
            vm_net_mock_read_le16_at(data, candidate + 6) != 4)
        {
            continue;
        }
        x = vm_net_mock_read_le16_at(data, candidate + 2);
        y = vm_net_mock_read_le16_at(data, candidate + 4);
        if (x == 0 || y == 0)
            continue;
        if (xOut)
            *xOut = x;
        if (yOut)
            *yOut = y;
        return true;
    }
    return false;
}

static bool vm_net_mock_find_scene_teleport_stone_anchor(const char *scene,
                                                          u16 *xOut, u16 *yOut)
{
    u8 data[16384];
    u32 len = 0;
    u32 start = 0;

    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (!vm_net_mock_scene_name_is_download_key(scene))
        return false;
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    start = vm_net_mock_scene_payload_start(data, len);
    if (len == 0 || start == 0)
        return false;
    for (u32 offset = start; offset + 8 <= len; ++offset)
    {
        char actorResource[64];
        u16 x = 0;
        u16 y = 0;
        bool hasPoint = false;
        u32 end = 0;

        if (!vm_net_mock_parse_sce_static_actor_at(
                data, len, offset, actorResource, sizeof(actorResource),
                &x, &y, &hasPoint, &end))
        {
            continue;
        }
        if (strcmp(actorResource, "n_telestone.actor") == 0)
        {
            if (!hasPoint || x == 0 || y == 0)
            {
                if (!vm_net_mock_find_sce_static_actor_parent_anchor(
                        data, offset, &x, &y))
                {
                    return false;
                }
            }
            if (xOut)
                *xOut = x;
            if (yOut)
                *yOut = y;
            return true;
        }
        if (end > offset)
            offset = end - 1;
    }
    return false;
}

static bool vm_net_mock_death_respawn_scene_has_teleport_stone(const char *scene)
{
    return vm_net_mock_find_scene_teleport_stone_anchor(scene, NULL, NULL);
}

/*
 * The client asks the scene teleport stone for an exitinfo list, not a world
 * map page.  Its item-use callback repeats the selected exit id in 16/2 and
 * 16/3, so every list row must carry a stable server-resolvable identity.
 *
 * A destination is eligible only when its actual n_telestone Actor and an
 * exact sMap.dsh scene row both exist.  The actor's SCE coordinates are kept
 * with that row so 16/2 can land by the selected destination stone instead of
 * reusing an unrelated scene entrance or centre.
 */
enum
{
    VM_NET_MOCK_TELEPORT_STONE_DESTINATION_MAX = 64
};

typedef struct
{
    u32 exitId;
    bool hasSmapRow;
    u16 stoneX;
    u16 stoneY;
    char scene[64];
    char label[64];
} vm_net_mock_teleport_stone_destination;

static bool vm_net_mock_find_teleport_stone_smap_destination(
    const char *scene, u32 *rowIdOut, char *labelOut, size_t labelOutCap)
{
    char path[256];
    u8 data[16384];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;
    u32 pos = 0;

    if (rowIdOut)
        *rowIdOut = 0;
    if (labelOut && labelOutCap)
        labelOut[0] = 0;
    if (!vm_net_mock_scene_name_is_download_key(scene) ||
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
    if (columnCount < 3 || columnCount > 64 || rowCount > 10000 ||
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
        u32 rowId = 0;
        char rowScene[64];
        char rowLabel[64];
        bool valid = true;

        if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
            return false;
        memset(rowScene, 0, sizeof(rowScene));
        memset(rowLabel, 0, sizeof(rowLabel));
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
                rowId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 1)
                valid = vm_net_mock_copy_dsh_string_field(
                            rowScene, sizeof(rowScene), value, valueLen) &&
                        valid;
            else if (col == 2)
                (void)vm_net_mock_copy_dsh_string_field(
                    rowLabel, sizeof(rowLabel), value, valueLen);
            rowPos += valueLen;
        }
        if (valid && rowId != 0 &&
            vm_net_mock_scene_names_equal_exact(scene, rowScene))
        {
            if (rowIdOut)
                *rowIdOut = rowId;
            if (labelOut && labelOutCap)
            {
                snprintf(labelOut, labelOutCap, "%s",
                         rowLabel[0] ? rowLabel : rowScene);
            }
            return true;
        }
        pos = rowEnd;
    }
    return false;
}

static u32 vm_net_mock_collect_teleport_stone_destinations(
    vm_net_mock_teleport_stone_destination *destinations, u32 destinationCap)
{
    vm_net_mock_monster_catalog_scene_file files[
        VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX];
    u32 fileCount = 0;
    u32 destinationCount = 0;

    if (destinations == NULL || destinationCap == 0)
        return 0;
    memset(destinations, 0, sizeof(*destinations) * destinationCap);
    fileCount = vm_net_mock_monster_catalog_collect_scene_files(
        files, sizeof(files) / sizeof(files[0]));
    for (u32 fileIndex = 0;
         fileIndex < fileCount && destinationCount < destinationCap;
         ++fileIndex)
    {
        vm_net_mock_teleport_stone_destination *destination =
            &destinations[destinationCount];
        u32 smapRowId = 0;
        u16 stoneX = 0;
        u16 stoneY = 0;

        if (!vm_net_mock_scene_name_is_download_key(files[fileIndex].name) ||
            !vm_net_mock_find_scene_teleport_stone_anchor(
                files[fileIndex].name, &stoneX, &stoneY))
        {
            continue;
        }
        snprintf(destination->scene, sizeof(destination->scene), "%s",
                 files[fileIndex].name);
        if (!vm_net_mock_find_teleport_stone_smap_destination(
                destination->scene, &smapRowId, destination->label,
                sizeof(destination->label)))
        {
            continue;
        }
        destination->exitId = smapRowId;
        destination->hasSmapRow = true;
        destination->stoneX = stoneX;
        destination->stoneY = stoneY;
        if (destination->exitId == 0 || destination->label[0] == 0 ||
            destination->stoneX == 0 || destination->stoneY == 0)
            continue;
        ++destinationCount;
    }
    return destinationCount;
}

static bool vm_net_mock_get_teleport_stone_catalog_target(
    u32 exitId, vm_net_mock_scene_change_target *target)
{
    vm_net_mock_teleport_stone_destination destinations[
        VM_NET_MOCK_TELEPORT_STONE_DESTINATION_MAX];
    u32 destinationCount = 0;

    if (target == NULL || exitId == 0)
        return false;
    destinationCount = vm_net_mock_collect_teleport_stone_destinations(
        destinations, sizeof(destinations) / sizeof(destinations[0]));
    for (u32 i = 0; i < destinationCount; ++i)
    {
        const vm_net_mock_teleport_stone_destination *destination =
            &destinations[i];

        if (destination->exitId != exitId)
            continue;
        memset(target, 0, sizeof(*target));
        snprintf(target->scene, sizeof(target->scene), "%s",
                 destination->scene);
        target->x = destination->stoneX;
        target->y = destination->stoneY;
        vm_net_mock_adjust_safe_player_pos_for_scene(
            target->scene, &target->x, &target->y);
        target->exitId = destination->exitId;
        target->mapType = 2;
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
            else if (col == 3)
                node.mapMarkerX =
                    vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 4)
                node.mapMarkerY =
                    vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col >= 7 && col <= 10)
                node.neighbors[col - 7] = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 11)
                node.parentWorldId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 12)
                node.isSafe = vm_net_mock_death_respawn_is_safe_level(value,
                                                                        valueLen);
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
            else if (col == 16)
            {
                /* GBK "无": the authored wMap monster-level value for the
                 * safe city maps (蓬莱、临安府、雁门关、蜀山、大理). */
                node.isTown = vm_net_mock_death_respawn_is_safe_level(value,
                                                                        valueLen);
            }
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

static bool vm_net_mock_find_town_center_smap_node(
    const vm_net_mock_death_respawn_smap_node *smap, u32 smapCount,
    u32 townWorldId, u32 *targetIndexOut)
{
    unsigned long long totalX = 0;
    unsigned long long totalY = 0;
    u32 memberCount = 0;
    u32 centerX = 0;
    u32 centerY = 0;
    u32 bestIndex = 0;
    unsigned long long bestDistance = 0;
    bool found = false;

    if (targetIndexOut)
        *targetIndexOut = 0;
    if (smap == NULL || smapCount == 0 || townWorldId == 0)
        return false;
    for (u32 i = 0; i < smapCount; ++i)
    {
        if (smap[i].parentWorldId != townWorldId ||
            !vm_net_mock_scene_name_is_download_key(smap[i].scene))
        {
            continue;
        }
        totalX += smap[i].mapMarkerX;
        totalY += smap[i].mapMarkerY;
        ++memberCount;
    }
    if (memberCount == 0)
        return false;
    centerX = (u32)(totalX / memberCount);
    centerY = (u32)(totalY / memberCount);
    for (u32 i = 0; i < smapCount; ++i)
    {
        unsigned long long dx = 0;
        unsigned long long dy = 0;
        unsigned long long distance = 0;

        if (smap[i].parentWorldId != townWorldId ||
            !vm_net_mock_scene_name_is_download_key(smap[i].scene))
        {
            continue;
        }
        dx = smap[i].mapMarkerX > centerX
                 ? smap[i].mapMarkerX - centerX : centerX - smap[i].mapMarkerX;
        dy = smap[i].mapMarkerY > centerY
                 ? smap[i].mapMarkerY - centerY : centerY - smap[i].mapMarkerY;
        distance = dx * dx + dy * dy;
        if (!found || distance < bestDistance ||
            (distance == bestDistance && smap[i].rowId < smap[bestIndex].rowId))
        {
            found = true;
            bestIndex = i;
            bestDistance = distance;
        }
    }
    if (!found)
        return false;
    if (targetIndexOut)
        *targetIndexOut = bestIndex;
    return true;
}

static bool vm_net_mock_resolve_nearest_safe_respawn(
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
    u32 targetWorldId = 0;
    u32 bestDistance = 0xffffffffu;
    u32 queueHead = 0;
    u32 queueTail = 0;
    u16 landingX = 0;
    u16 landingY = 0;
    const char *route = "unresolved";
    bool localSafeTargetFound = false;

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
    if (!vm_net_mock_scene_name_is_safe(fromScene) || sceneOut == NULL ||
        sceneOutCap == 0 ||
        !vm_net_mock_death_respawn_load_smap_topology(
            smap, sizeof(smap) / sizeof(smap[0]), &smapCount))
    {
        return false;
    }
    for (; sourceIndex < smapCount; ++sourceIndex)
    {
        if (vm_net_mock_scene_names_equal_exact(fromScene,
                                                smap[sourceIndex].scene))
        {
            break;
        }
    }
    if (sourceIndex == smapCount)
        return false;
    if (sourceSmapRowOut)
        *sourceSmapRowOut = smap[sourceIndex].rowId;

    /* A source world can contain authored safe rooms even when its parent
     * wMap is not itself a city.  Those sMap links describe the player's
     * immediate map topology, so they must win over a one-hop global map link
     * to Penglai (for example 23蟠龙寨_03 -> 23蟠龙寨_02). */
    memset(smapVisited, 0, sizeof(smapVisited));
    queueHead = 0;
    queueTail = 0;
    smapQueue[queueTail++] = sourceIndex;
    smapVisited[sourceIndex] = true;
    smapDistance[sourceIndex] = 0;
    while (queueHead < queueTail)
    {
        u32 index = smapQueue[queueHead++];

        if (smap[index].isSafe)
        {
            targetIndex = index;
            targetWorldId = smap[index].parentWorldId;
            bestDistance = smapDistance[index];
            route = "smap-nearest-safe-scene";
            localSafeTargetFound = true;
            break;
        }
        for (u32 edge = 0; edge < 4; ++edge)
        {
            int neighbor = vm_net_mock_death_respawn_find_smap_node(
                smap, smapCount, smap[index].neighbors[edge]);
            if (neighbor >= 0 && !smapVisited[neighbor] &&
                queueTail < smapCount)
            {
                smapVisited[neighbor] = true;
                smapDistance[neighbor] = smapDistance[index] + 1;
                smapQueue[queueTail++] = (u32)neighbor;
            }
        }
    }

    /* Not every world has a local safe room.  Only then cross the authored
     * wMap links and choose the nearest town centre. */
    if (!localSafeTargetFound)
    {
        if (!vm_net_mock_death_respawn_load_wmap_topology(
                wmap, sizeof(wmap) / sizeof(wmap[0]), &wmapCount))
        {
            return false;
        }
        int sourceWorldIndex = vm_net_mock_death_respawn_find_wmap_node(
            wmap, wmapCount, smap[sourceIndex].parentWorldId);
        if (sourceWorldIndex < 0)
            return false;
        queueHead = 0;
        queueTail = 0;
        for (u32 i = 0; i < wmapCount; ++i)
            wmapDistance[i] = 0xffffffffu;
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
        for (u32 i = 0; i < wmapCount; ++i)
        {
            if (!wmap[i].isTown || wmapDistance[i] == 0xffffffffu)
                continue;
            if (wmapDistance[i] < bestDistance ||
                (wmapDistance[i] == bestDistance &&
                 (targetWorldId == 0 || wmap[i].worldId < targetWorldId)))
            {
                targetWorldId = wmap[i].worldId;
                bestDistance = wmapDistance[i];
            }
        }
        if (targetWorldId == 0 ||
            !vm_net_mock_find_town_center_smap_node(
                smap, smapCount, targetWorldId, &targetIndex))
        {
            return false;
        }
        route = "wmap-nearest-town-center";
    }
    if (targetWorldId == 0 ||
        !vm_net_mock_get_scene_reasonable_spawn_from_sce(
            smap[targetIndex].scene, &landingX, &landingY, NULL))
    {
        return false;
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(
        smap[targetIndex].scene, &landingX, &landingY);
    (void)vm_net_mock_adjust_recovery_landing_to_map_safe(
        smap[targetIndex].scene, &landingX, &landingY);
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
    printf("[info][network] mock_death_respawn_nearest_safe source_scene=%s "
           "source_smap=%u target_scene=%s target_smap=%u target_world=%u "
           "route=%s hops=%u landing=(%u,%u) evidence=sMap.dsh(monster-level=none)+wMap.dsh+SCE\n",
           fromScene, smap[sourceIndex].rowId,
           smap[targetIndex].scene, smap[targetIndex].rowId,
           targetWorldId, route, bestDistance, landingX, landingY);
    vm_autotest_note("mock_death_respawn_nearest_safe source_scene=%s "
                     "source_smap=%u target_scene=%s target_smap=%u "
                     "target_world=%u route=%s hops=%u landing=(%u,%u) "
                     "evidence=sMap.dsh(monster-level=none)/wMap.dsh/SCE\n",
                     fromScene, smap[sourceIndex].rowId,
                     smap[targetIndex].scene, smap[targetIndex].rowId,
                     targetWorldId, route, bestDistance, landingX, landingY);
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
         * Every other login, portal and map-stone route uses this same exact
         * resource key. No route may strip or append the `.sce` suffix.
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

static bool vm_net_mock_build_teleport_stone_exitinfo_blob(
    u8 *out, u32 outCap, u32 *blobLenOut, u32 *entryCountOut)
{
    vm_net_mock_teleport_stone_destination destinations[
        VM_NET_MOCK_TELEPORT_STONE_DESTINATION_MAX];
    u32 destinationCount = 0;
    u32 pos = 0;

    if (out == NULL || blobLenOut == NULL)
        return false;
    if (entryCountOut)
        *entryCountOut = 0;
    destinationCount = vm_net_mock_collect_teleport_stone_destinations(
        destinations, sizeof(destinations) / sizeof(destinations[0]));
    if (destinationCount == 0 || destinationCount > UINT8_MAX)
        return false;

    /*
     * mmGameMstarWqvga.cbm:sub_11CE parses 16/1 exitinfo as:
     *   u8 count, repeated u32 exitId, len16 string label, u32 reserved.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)destinationCount))
        return false;
    for (u32 i = 0; i < destinationCount; ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos,
                                     destinations[i].exitId) ||
            !vm_net_mock_seq_put_string(out, outCap, &pos,
                                        destinations[i].label) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, 0))
        {
            return false;
        }
    }

    *blobLenOut = pos;
    if (entryCountOut)
        *entryCountOut = destinationCount;
    return true;
}

static u32 vm_net_mock_build_teleport_stone_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 exitInfo[4096];
    u32 exitInfoLen = 0;
    u32 destinationCount = 0;
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
    if (!vm_net_mock_build_teleport_stone_exitinfo_blob(
            exitInfo, sizeof(exitInfo), &exitInfoLen, &destinationCount))
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

    printf("[info][network] mock_teleport_stone_list entries=%u source=SCE:n_telestone-intersect-sMap.dsh "
           "role=%u item800=%u wcoin=%u exitinfo_len=%u evidence=mmGame:0x11CE\n",
           destinationCount,
           role ? role->roleId : 0,
           teleportStoneCount,
           wcoin,
           exitInfoLen);
    vm_autotest_note("mock_teleport_stone_list entries=%u source=SCE:n_telestone-intersect-sMap.dsh role=%u item800=%u wcoin=%u exitinfo_len=%u evidence=mmGame:0x11CE\n",
                     destinationCount, role ? role->roleId : 0,
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

static u32 vm_net_mock_build_startup_sce_install_scene_enter_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_scene_change_target target;
    vm_net_mock_role_state *role = NULL;
    const char *currentScene = NULL;
    u32 installGeneration = 0;
    u32 pos = 5;

    if (!vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5) ||
        !g_vm_net_mock_startup_sce_enter_pending)
    {
        return 0;
    }
    target = g_vm_net_mock_startup_sce_enter_target;
    if (out == NULL || outCap < pos ||
        g_schedulerTick - g_vm_net_mock_startup_sce_enter_armed_tick > 90u ||
        !g_vm_net_mock_last_completed_scene_change_target_valid ||
        !vm_net_mock_scene_change_targets_same_arrival(
            &target, &g_vm_net_mock_last_completed_scene_change_target))
    {
        g_vm_net_mock_startup_sce_enter_pending = false;
        return 0;
    }
    role = vm_net_mock_active_role();
    currentScene = vm_net_mock_current_scene_name();
    installGeneration = vm_net_mock_content_client_resource_install_generation(
        g_vm_mock_service_active_client_id, target.scene);
    if (role == NULL ||
        !vm_net_mock_scene_names_equal_exact(role->scene, target.scene) ||
        !vm_net_mock_scene_names_equal_exact(currentScene, target.scene) ||
        installGeneration == 0 ||
        installGeneration != g_vm_net_mock_startup_sce_enter_install_generation)
    {
        g_vm_net_mock_startup_sce_enter_pending = false;
        return 0;
    }
    /*
     * mmGame:sub_11CE dispatches only 16/3(result=2) to sub_BCC.  The
     * previously probed 16/2(result=1) is a different settings/recovery
     * contract: it caused a client WT2/3 round-trip and did not rebuild the
     * startup action route.  Keep this exact branch test-gated and one-shot.
     */
    if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(
            out, outCap, &pos, 3, 2, &target))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_vm_net_mock_startup_sce_enter_pending = false;
    vm_net_mock_mark_direct_scene_enter_completed(
        &target, "startup-sce-install-25-5");
    vm_net_mock_mark_scene_moveinfo_npc_seed_pending(target.scene);
    printf("[info][network] mock_scene_npc_rearm scene=%s trigger=startup-sce-install response=16/3-result2 immediate=0 next=WT16/3+27/11+7/42 evidence=mmGame:0x11CE->0x0BCC+JianghuOL.CBE:0x01012FB4+0x01037998\n",
           target.scene);
    printf("[info][network] mock_startup_sce_install_scene_enter scene=%s pos=(%u,%u) install_generation=%u response=16/3-result2 next=client-runtime-16/3+27/11+7/42 evidence=mmGame:0x11CE->0x0BCC->main-api+116\n",
           target.scene, target.x, target.y, installGeneration);
    vm_autotest_note("mock_startup_sce_install_scene_enter scene=%s pos=(%u,%u) install_generation=%u response=16/3-result2 next=client-runtime-16/3+27/11+7/42 evidence=mmGame:0x11CE->0x0BCC->main-api+116\n",
                     target.scene, target.x, target.y, installGeneration);
    return pos;
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
     * A settings recovery 16/2 or 16/3 response enters its `scene` through
     * mmGame's direct scene-entry API.  It is not an in-place coordinate
     * repair.  On player-3, a visible transient instance followed by a same
     * scene direct-enter retained its nested background Actor array
     * (050163a8/count=2) across screen destruction and started a second
     * descriptor walk with no FreeBattleActorArray boundary.  The exhausted
     * allocator then faulted at JianghuOL.CBE:0x0100DA4E before the later
     * mmGame fault.  Until an observed native instance exit/resume contract
     * exists, the durable world anchor is the only proven different-scene
     * recovery target; do not turn the session target into a same-shell
     * direct re-entry.
     */
    if (session != NULL && session->transientInstanceActive &&
        session->sceneVisibleReady && !session->sceneVisiblePending &&
        vm_net_mock_scene_name_is_safe(session->transientInstanceScene) &&
        vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene,
                                            session->transientInstanceScene) &&
        role != NULL && vm_net_mock_scene_name_is_persistable(role->scene))
    {
        printf("[warn][network] mock_unstuck_transient_same_scene_unsafe scene=%s visible_pos=(%u,%u) durable_anchor=%s@(%u,%u) action=use-durable-anchor reason=16-2-direct-enter-retains-background-actor-array evidence=player-3:0x0100DA4E\n",
               session->transientInstanceScene,
               session->sceneVisibleX,
               session->sceneVisibleY,
               role->scene,
               role->x,
               role->y);
        vm_autotest_note("mock_unstuck_transient_same_scene_unsafe scene=%s action=use-durable-anchor evidence=player-3:0x0100DA4E\n",
                         session->transientInstanceScene);
    }
    /*
     * 12/3 belongs to the authenticated service session.  The role's scene
     * and position are durable request-scoped state; the host CBE runtime grid
     * is only a local-emulator fallback and must never supersede them.
     */
    if (role != NULL && vm_net_mock_scene_name_is_persistable(role->scene))
    {
        scene = role->scene;
        fromX = role->x;
        fromY = role->y;
        if (session != NULL &&
            session->sceneVisibleReady && !session->sceneVisiblePending &&
            vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) &&
            vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene, scene) &&
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
    if (!vm_net_mock_scene_name_is_persistable(scene))
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
    if (vm_net_mock_adjust_recovery_landing_to_map_safe(target->scene,
                                                         &target->x, &target->y))
    {
        targetSource = "map-collision-safe";
    }
    /*
     * The direct mmGame 16/2 response always serializes exitid.  When the
     * recovery landing came from an SCE edge entry, preserve that entry's
     * authoritative ID just as the ordinary portal target does.  A centre or
     * current-position fallback has no such SCE entry and keeps the protocol
     * default of zero.
     */
    target->exitId = (entryId != 0xffffu) ? entryId : 0;
    target->mapType = 2;
    target->hasSceEntry = strcmp(targetSource, "current-pos") != 0;
    target->needsSceneDownload = false;
    if (!vm_net_mock_scene_resource_exists(target->scene))
    {
        printf("[warn][network] mock_unstuck_target_unresolved scene=%s action=preserve-exact-key reason=server-sce-not-found\n",
               target->scene);
    }
    printf("[info][network] mock_unstuck_target scene=%s scene_source=%s from=(%u,%u) from_source=%s pos=(%u,%u) source=%s entry=%u exit=%u\n",
           target->scene,
           sceneSource,
           fromX,
           fromY,
           fromSource,
           target->x,
           target->y,
           targetSource,
           entryId,
           target->exitId);
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
    vm_net_mock_mark_settings_unstuck_npc_reseed_pending(&target, "12/3+16/3");
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

/* A completed transient instance already owns a live scene shell.  The
 * compact settings request is parsed by mmGame:sub_11CE: result=1 enters the
 * scene again, whereas result=4 reads only `hint`, clears the menu wait state
 * and leaves the current shell in place.  Keep this exact predicate separate
 * from the generic recovery target chooser: there is no in-place coordinate
 * relocation payload in the observed 16/2 contract. */
static bool vm_net_mock_settings_unstuck_hits_live_transient_instance(
    const vm_mock_service_client_session *session)
{
    return session != NULL && session->transientInstanceActive &&
           session->sceneVisibleReady && !session->sceneVisiblePending &&
           session->sceneVisibleX != 0 && session->sceneVisibleY != 0 &&
           vm_net_mock_scene_name_is_safe(session->transientInstanceScene) &&
           vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene,
                                               session->transientInstanceScene);
}

static u32 vm_net_mock_build_settings_unstuck_live_instance_reject_response(
    u8 *out, u32 outCap)
{
    /* GBK: 副本内无法使用脱离卡死 */
    static const char hint[] =
        "\xb8\xb1\xb1\xbe\xc4\xda\xce\xde\xb7\xa8\xca\xb9\xd3\xc3\xcd\xd1\xc0\xeb\xbf\xa8\xcb\xc0";
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x10, 2, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "hint", hint))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_settings_unstuck_16_2_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 pos = 5;
    vm_net_mock_scene_change_target target;
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (outCap < pos || !vm_net_mock_is_settings_unstuck_16_2_request(request, requestLen))
        return 0;

    if (vm_net_mock_settings_unstuck_hits_live_transient_instance(session))
    {
        u32 responseLen = vm_net_mock_build_settings_unstuck_live_instance_reject_response(
            out, outCap);
        if (responseLen == 0)
            return 0;
        printf("[info][network] mock_unstuck_live_instance_preserved scene=%s pos=(%u,%u) response=16/2-result4-hint action=no-direct-reenter reason=16-2-result1-retains-background-actor-array evidence=mmGame:0x11CE+player-3:0x0100DA4E\n",
               session->transientInstanceScene,
               session->sceneVisibleX,
               session->sceneVisibleY);
        vm_autotest_note("mock_unstuck_live_instance_preserved scene=%s pos=(%u,%u) response=16/2-result4-hint action=no-direct-reenter evidence=mmGame:0x11CE(result4)+player-3:0x0100DA4E\n",
                         session->transientInstanceScene,
                         session->sceneVisibleX,
                         session->sceneVisibleY);
        return responseLen;
    }

    vm_net_mock_get_current_scene_unstuck_target(&target);
    if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(out, outCap, &pos,
                                                                     2, 1, &target))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_net_mock_mark_direct_scene_enter_completed(&target, "settings-unstuck-16-2-target");
    vm_net_mock_mark_settings_unstuck_npc_reseed_pending(&target, "16/2");
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

typedef struct
{
    bool found;
    bool invalid;
    u32 wcoinCost;
    u8 enabled;
} vm_net_mock_paid_instance_rule_row;

static bool vm_net_mock_is_scene_runtime_position_ack_16_3_request(
    const u8 *request, u32 requestLen, u16 *positionXOut);

static bool g_vm_net_mock_paid_instance_schema_checked = false;
static bool g_vm_net_mock_paid_instance_schema_valid = false;

static bool vm_net_mock_paid_instance_rule_row_read(void *contextValue,
                                                    unsigned int columnCount,
                                                    const char *const *values,
                                                    const size_t *lengths)
{
    vm_net_mock_paid_instance_rule_row *row =
        (vm_net_mock_paid_instance_rule_row *)contextValue;
    u32 enabled = 0;

    if (row == NULL || row->found || columnCount != 2 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &row->wcoinCost) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &enabled) || enabled > 1)
    {
        if (row != NULL)
            row->invalid = true;
        return true;
    }
    row->enabled = (u8)enabled;
    row->found = true;
    return true;
}

static bool vm_net_mock_paid_instance_schema_prepare(void)
{
    static const char *const seedStatements[] = {
        "INSERT IGNORE INTO paid_instance_access_rules"
        "(background_scene,target_entry_id,wcoin_cost,enabled) "
        "VALUES(X'625F3231D3C4DAA4B9EDB8AE2E736365',2,1,1)",
        "INSERT IGNORE INTO paid_instance_access_rules"
        "(background_scene,target_entry_id,wcoin_cost,enabled) "
        "VALUES(X'625F3233F3B4C1FAD5AF2E736365',2,1,1)",
        "INSERT IGNORE INTO paid_instance_access_rules"
        "(background_scene,target_entry_id,wcoin_cost,enabled) "
        "VALUES(X'625F3235BBAAC9BDB6B4BFDF2E736365',2,1,1)"
    };

    if (g_vm_net_mock_paid_instance_schema_checked)
        return g_vm_net_mock_paid_instance_schema_valid;
    g_vm_net_mock_paid_instance_schema_checked = true;
    g_vm_net_mock_paid_instance_schema_valid = vm_mysql_exec(
        "CREATE TABLE IF NOT EXISTS paid_instance_access_rules ("
        "background_scene VARBINARY(63) NOT NULL,"
        "target_entry_id SMALLINT UNSIGNED NOT NULL,"
        "wcoin_cost INT UNSIGNED NOT NULL,"
        "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY(background_scene,target_entry_id)"
        ") ENGINE=InnoDB");
    if (g_vm_net_mock_paid_instance_schema_valid)
    {
        for (u32 i = 0; i < sizeof(seedStatements) / sizeof(seedStatements[0]); ++i)
        {
            if (!vm_mysql_exec(seedStatements[i]))
            {
                g_vm_net_mock_paid_instance_schema_valid = false;
                break;
            }
        }
    }
    if (!g_vm_net_mock_paid_instance_schema_valid)
    {
        printf("[error][network] mock_paid_instance_rule_schema_failed error=%s\n",
               vm_mysql_last_error());
    }
    return g_vm_net_mock_paid_instance_schema_valid;
}

static bool vm_net_mock_paid_instance_rule_read(const char *backgroundScene,
                                                u16 targetEntryId,
                                                bool *foundOut,
                                                bool *enabledOut,
                                                u32 *wcoinCostOut)
{
    char backgroundHex[129];
    char query[512];
    vm_net_mock_paid_instance_rule_row row;
    size_t backgroundLen = 0;

    if (foundOut != NULL)
        *foundOut = false;
    if (enabledOut != NULL)
        *enabledOut = false;
    if (wcoinCostOut != NULL)
        *wcoinCostOut = 0;
    if (backgroundScene == NULL || backgroundScene[0] == 0 || targetEntryId == 0 ||
        !vm_net_mock_paid_instance_schema_prepare())
    {
        return false;
    }
    backgroundLen = vm_mock_mysql_bounded_strlen(backgroundScene, 64);
    if (backgroundLen == 0 || backgroundLen >= 64 ||
        vm_mysql_hex_encode(backgroundScene, backgroundLen, backgroundHex,
                            sizeof(backgroundHex)) == 0)
    {
        return false;
    }
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT wcoin_cost,enabled FROM paid_instance_access_rules "
             "WHERE background_scene=X'%s' AND target_entry_id=%u",
             backgroundHex, targetEntryId);
    if (!vm_mysql_query(query, vm_net_mock_paid_instance_rule_row_read, &row) ||
        row.invalid)
    {
        return false;
    }
    if (foundOut != NULL)
        *foundOut = row.found;
    if (enabledOut != NULL)
        *enabledOut = row.enabled != 0;
    if (wcoinCostOut != NULL)
        *wcoinCostOut = row.wcoinCost;
    return true;
}

static u32 vm_net_mock_build_paid_instance_failure_response(u8 result,
                                                            const char *hint,
                                                            u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x10, 2, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    if (result == 4 &&
        (hint == NULL || hint[0] == 0 ||
         !vm_net_mock_put_object_string(out, outCap, &pos, "hint", hint)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

/* Named-portal state 2/3 is wire-compatible with the later scene-runtime
 * position acknowledgement.  The only reliable discriminator is the source
 * SCE rectangle plus its exact target-entry id; this is why this handler is
 * deliberately placed before the generic type=0 ACK builder. */
static u32 vm_net_mock_build_named_portal_access_response(const u8 *request,
                                                          u32 requestLen,
                                                          u8 *out, u32 outCap)
{
    static const char unavailableHint[] =
        "\xb8\xb1\xb1\xbe\xc8\xeb\xbf\xda\xb2\xbb\xbf\xc9\xd3\xc3";
    static const char configurationHint[] =
        "\xb8\xb1\xb1\xbe\xc5\xe4\xd6\xc3\xb6\xc1\xc8\xa1\xca\xa7\xb0\xdc";
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_sce_named_portal portal;
    vm_net_mock_scene_change_target target;
    u16 targetEntryId = 0;
    bool ruleFound = false;
    bool ruleEnabled = false;
    u32 wcoinCost = 0;
    u32 wcoinBefore = 0;
    u32 wcoinAfter = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_scene_runtime_position_ack_16_3_request(
            request, requestLen, &targetEntryId) ||
        role == NULL || !vm_net_mock_scene_name_is_safe(role->scene) ||
        !vm_net_mock_find_sce_named_portal_at_pos(role->scene, role->x, role->y,
                                                   targetEntryId, &portal))
    {
        return 0;
    }

    if (!vm_net_mock_resolve_sce_named_portal_target(&portal, &target))
    {
        printf("[error][network] mock_named_portal_access_unresolved scene=%s pos=(%u,%u) entry=%u target=%s action=result4-no-fallback\n",
               role->scene, role->x, role->y, targetEntryId, portal.targetScene);
        return vm_net_mock_build_paid_instance_failure_response(4, unavailableHint,
                                                                 out, outCap);
    }

    if (!vm_net_mock_paid_instance_rule_read(portal.backgroundScene, targetEntryId,
                                             &ruleFound, &ruleEnabled, &wcoinCost))
    {
        printf("[error][network] mock_paid_instance_rule_read_failed source=%s entry=%u action=result4\n",
               portal.backgroundScene[0] ? portal.backgroundScene : "-", targetEntryId);
        return vm_net_mock_build_paid_instance_failure_response(4, configurationHint,
                                                                 out, outCap);
    }
    /* Entry 2 is the shipped paid-instance class.  A missing or disabled rule
     * must fail closed instead of falling through to the normal type=0 ACK or
     * a default teleport target.  Other named entry classes remain free unless
     * explicitly configured in the same table. */
    if ((targetEntryId == 2 && !ruleFound) || (ruleFound && !ruleEnabled) ||
        (ruleFound && wcoinCost == 0))
    {
        printf("[info][network] mock_paid_instance_access_denied source=%s entry=%u rule=%u enabled=%u cost=%u action=result4\n",
               portal.backgroundScene[0] ? portal.backgroundScene : "-", targetEntryId,
               ruleFound ? 1u : 0u, ruleEnabled ? 1u : 0u, wcoinCost);
        return vm_net_mock_build_paid_instance_failure_response(4, unavailableHint,
                                                                 out, outCap);
    }

    if (ruleFound)
    {
        wcoinBefore = vm_net_mock_role_wcoin_balance(role);
        if (wcoinBefore < wcoinCost)
        {
            printf("[info][network] mock_paid_instance_access_insufficient role=%u source=%s entry=%u cost=%u wcoin=%u action=result2-native-recharge\n",
                   role->roleId, portal.backgroundScene, targetEntryId, wcoinCost,
                   wcoinBefore);
            return vm_net_mock_build_paid_instance_failure_response(2, NULL, out, outCap);
        }
        if (!vm_net_mock_account_wallet_debit_exact("paid-instance-access",
                                                    wcoinBefore, wcoinCost,
                                                    &wcoinAfter))
        {
            printf("[error][network] mock_paid_instance_access_debit_failed role=%u source=%s entry=%u cost=%u expected_wcoin=%u action=result4\n",
                   role->roleId, portal.backgroundScene, targetEntryId, wcoinCost,
                   wcoinBefore);
            return vm_net_mock_build_paid_instance_failure_response(4, configurationHint,
                                                                     out, outCap);
        }
        /* The account-wallet debit is committed before scene construction.
         * Keep the operation log append-only and non-blocking: its failure is
         * visible to the service operator but must not falsely reject a paid
         * entry whose authoritative W-coin transaction already succeeded. */
        if (g_vm_mock_service_active_account_id != NULL &&
            g_vm_mock_service_active_account_id[0] != 0)
        {
            char operationDetail[256];

            snprintf(operationDetail, sizeof(operationDetail),
                     "游戏内付费副本消费 W币 %u，入口 %u，余额 %u→%u",
                     wcoinCost, targetEntryId, wcoinBefore, wcoinAfter);
            if (!vm_mock_admin_operation_log_record(
                    "spend-wcoin-instance", g_vm_mock_service_active_account_id,
                    role->roleId, 0, 0, wcoinCost, operationDetail, NULL))
            {
                printf("[error][mock-service] operation_log_game_wcoin_failed source=paid-instance account=%s role=%u entry=%u cost=%u error=%s\n",
                       g_vm_mock_service_active_account_id, role->roleId,
                       targetEntryId, wcoinCost, vm_mysql_last_error());
            }
        }
    }
    else
    {
        wcoinAfter = vm_net_mock_role_wcoin_balance(role);
    }

    if (!vm_net_mock_append_mmgame_scene_transfer_object_with_result(out, outCap, &pos,
                                                                     3, 2, &target))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_mark_direct_scene_enter_completed(&target, "named-portal-access");
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y,
                                      "named-portal-access");
    printf("[info][network] mock_named_portal_access role=%u source_scene=%s source_pos=(%u,%u) background=%s entry=%u target=%s pos=(%u,%u) paid=%u cost=%u wcoin=%u/%u response=16/3-result2\n",
           role->roleId, role->scene, role->x, role->y,
           portal.backgroundScene[0] ? portal.backgroundScene : "-", targetEntryId,
           target.scene, target.x, target.y, ruleFound ? 1u : 0u, wcoinCost,
           wcoinBefore, wcoinAfter);
    vm_autotest_note("mock_named_portal_access scene=%s entry=%u target=%s paid=%u cost=%u response=16/3-result2 evidence=SCE:0x12/0x15/0x17+mmGame:0x11CE\n",
                     role->scene, targetEntryId, target.scene, ruleFound ? 1u : 0u,
                     wcoinCost);
    return pos;
}

/*
 * A direct mmGame 16/2 scene entry reaches JianghuOL.CBE's scene-runtime
 * initializer with parser state 2 or 3.  That initializer then sends a 16/3
 * object whose `exitID` is the current X coordinate (seen as both typed-i16
 * and typed-u32 encodings) and whose `type` is zero
 * (JianghuOL.CBE:0x0101359C).  It is a runtime
 * synchronization acknowledgement, not a teleport-stone selection: treating
 * its coordinate as an sMap exit id makes the generic 16/3 handler invent and
 * persist the default teleport scene.
 */
static bool vm_net_mock_is_scene_runtime_position_ack_16_3_object(
    const vm_net_mock_request_object *object, u16 *positionXOut)
{
    u32 fieldPos = 0;
    u16 positionX = 0;
    u8 type = 0;
    bool havePositionX = false;
    bool haveType = false;

    if (positionXOut != NULL)
        *positionXOut = 0;
    if (object == NULL || object->major != 1 || object->kind != 0x10 ||
        object->subtype != 3)
    {
        return false;
    }

    while (fieldPos < object->payloadLen)
    {
        u8 nameLen = 0;
        u16 valueLen = 0;
        const u8 *name = NULL;
        const u8 *value = NULL;

        if (fieldPos + 3 > object->payloadLen)
            return false;
        nameLen = object->payload[fieldPos++];
        if (fieldPos + nameLen + 2 > object->payloadLen)
            return false;
        name = object->payload + fieldPos;
        fieldPos += nameLen;
        valueLen = (u16)(((u16)object->payload[fieldPos] << 8) |
                         object->payload[fieldPos + 1]);
        fieldPos += 2;
        if (fieldPos + valueLen > object->payloadLen)
            return false;
        value = object->payload + fieldPos;
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
            /* scene_runtime_init_and_sync(0x0101324C) writes exitID through
             * an i16 setter.  The wire layer may preserve it in a typed-u32
             * field as either zero-extended or sign-extended i16.  The value
             * is observational runtime state, never a teleport exit. */
            else if (valueLen == 6 && value[0] == 0 && value[1] == 4 &&
                     ((value[2] == 0 && value[3] == 0) ||
                      (value[2] == 0xff && value[3] == 0xff)))
            {
                positionX = (u16)(((u16)value[4] << 8) | value[5]);
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

static bool vm_net_mock_is_scene_runtime_position_ack_16_3_request(
    const u8 *request, u32 requestLen, u16 *positionXOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (positionXOut != NULL)
        *positionXOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen)
    {
        return false;
    }
    return vm_net_mock_is_scene_runtime_position_ack_16_3_object(&object,
                                                                   positionXOut);
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

/*
 * A direct mmGame 16/2 enter does not necessarily leave the CBE runtime ACK
 * in its own WT frame.  The live client coalesces the ACK and its immediate
 * catalog pulls as:
 *
 *   1/16/3 { exitID: typed-u32(current X), type: typed-u8(0) }
 *   1/27/11 {}
 *   1/7/42  {}
 *
 * The trailing 27/11 and 7/42 are emitted by
 * scene_runtime_init_and_sync (JianghuOL.CBE:0x01012FB4) after the direct
 * enter has created the scene shell.  This is distinct from a teleport-stone
 * selection even though both start with 16/3 and contain exitID.  In
 * particular, the typed-u32 value is the current X coordinate, not an exit
 * ID.  Request frames have no object-count byte and may be batched differently
 * by the transport, so this handler reads a validated object stream instead
 * of matching one total packet length or one fixed trailing-object order.
 */
static u32 vm_net_mock_build_scene_runtime_direct_enter_followup_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    const char *scene = NULL;
    u16 positionX = 0;
    u32 offset = 4;
    u32 validationOffset = 4;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 requestObjectCount = 0;
    u32 independentObjectCount = 0;
    bool haveNpcCatalog = false;
    bool haveBooksCatalog = false;
    vm_net_mock_request_object object;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        (u16)(((u16)request[2] << 8) | request[3]) != requestLen ||
        !vm_net_mock_next_request_object(request, requestLen, &validationOffset, &object) ||
        !vm_net_mock_is_scene_runtime_position_ack_16_3_object(&object, &positionX))
    {
        return 0;
    }

    /* Preflight every object before changing catalog state or dispatching an
     * independently composable companion.  A packet with an unknown or an
     * atomic companion remains unresolved as one whole request. */
    ++requestObjectCount;
    while (vm_net_mock_next_request_object(request, requestLen, &validationOffset, &object))
    {
        ++requestObjectCount;
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 11 &&
            object.payloadLen == 0)
        {
            if (haveNpcCatalog)
                return 0;
            haveNpcCatalog = true;
        }
        else if (object.major == 1 && object.kind == 7 && object.subtype == 42 &&
                 object.payloadLen == 0)
        {
            if (haveBooksCatalog)
                return 0;
            haveBooksCatalog = true;
        }
        else if (vm_net_mock_object_is_independent_combo_candidate(&object))
        {
            ++independentObjectCount;
        }
        else
        {
            return 0;
        }
    }
    if (validationOffset != requestLen ||
        (!haveNpcCatalog && !haveBooksCatalog && independentObjectCount == 0))
    {
        return 0;
    }

    scene = vm_net_mock_current_scene_name();
    if (!vm_net_mock_scene_name_is_safe(scene))
    {
        /* The request is proven to be a scene-runtime follow-up, not a
         * teleport.  Do not let an unavailable role scene fall through to the
         * broad 16/3 teleport detector and create a default destination. */
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[error][network] mock_scene_runtime_direct_enter_followup scene=unresolved posx=%u response=empty-wt action=no-teleport-fallback evidence=JianghuOL.CBE:0x01012FB4\n",
               positionX);
        return pos;
    }

    /* Execute recognized objects in their request order.  The ACK itself has
     * no response object; 27/11 and 7/42 retain their own client parser
     * contracts even if the client reverses the two catalog pulls. */
    if (haveNpcCatalog)
        vm_net_mock_mark_scene_moveinfo_npc_seed_pending(scene);
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (vm_net_mock_is_scene_runtime_position_ack_16_3_object(&object, NULL))
            continue;
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 11 &&
            object.payloadLen == 0)
        {
            if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos, scene,
                                                               "direct-enter-runtime-stream"))
            {
                return 0;
            }
            ++objectCount;
        }
        else if (object.major == 1 && object.kind == 7 && object.subtype == 42 &&
                 object.payloadLen == 0)
        {
            if (!vm_net_mock_append_books42_object(out, outCap, &pos))
                return 0;
            ++objectCount;
        }
        else if (!vm_net_mock_append_independent_single_object_response(
                     &object, out, outCap, &pos, &objectCount))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    /* No scene target, position, or persistent role fields are changed here.
     * `exitID` is observational current-X data from the client runtime. */
    printf("[info][network] mock_scene_runtime_direct_enter_object_stream scene=%s posx=%u request_objects=%u independent=%u response_objects=%u action=no-scene-target-or-position-save evidence=JianghuOL.CBE:0x01012FB4+0x01012E4C+0x01037998\n",
           scene, positionX, requestObjectCount, independentObjectCount, (u32)objectCount);
    vm_autotest_note("mock_scene_runtime_direct_enter_object_stream scene=%s posx=%u request_objects=%u independent=%u response_objects=%u action=no-scene-target-or-position-save evidence=JianghuOL.CBE:0x01012FB4+0x01012E4C+0x01037998\n",
                     scene, positionX, requestObjectCount, independentObjectCount,
                     (u32)objectCount);
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
            if (!vm_net_mock_get_teleport_stone_target(
                    request, requestLen, &target))
            {
                return 0;
            }
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
        if (!vm_net_mock_get_teleport_stone_target(request, requestLen,
                                                    &target))
        {
            return 0;
        }
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
           vm_net_mock_scene_names_equal_exact(provisionalTarget.scene,
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

static bool vm_net_mock_append_team_member_full_row(
    u8 *groupInfo, u32 groupInfoCap, u32 *groupInfoLen,
    const vm_mock_service_client_session *observer,
    const vm_mock_service_client_session *member, bool firstRowInBlob);
static u32 vm_mock_service_team_member_wire_id(
    const vm_mock_service_client_session *observer,
    const vm_mock_service_client_session *member);

/* A 5/10 group-status request is also the client's roster bootstrap.  The
 * solo response must establish the local member, rather than returning an
 * empty synthetic/template list.  The scene HUD deliberately skips the local
 * id, so this node is state for the later 5/4 -> 5/5 invite lifecycle, not an
 * extra self portrait. */
static bool vm_net_mock_append_group_info_object(u8 *out, u32 outCap, u32 *pos,
                                                 const vm_mock_service_client_session *session)
{
    u8 groupInfo[128];
    u32 groupInfoLen = 0;
    u32 leadId = 0;
    u32 objectStart = 0;

    if (session == NULL || session->onlineRoleId == 0 ||
        !vm_net_mock_append_team_member_full_row(groupInfo, sizeof(groupInfo),
                                                 &groupInfoLen, session, session, true))
    {
        return false;
    }
    leadId = vm_mock_service_team_member_wire_id(session, session);
    if (leadId == 0)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "num", 1))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "leadid", leadId))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_team_solo_groupinfo observer=%08x role=%u "
           "groupinfo_len=%u lifecycle=login-self-roster\n",
           session->clientId, leadId, groupInfoLen);
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
               "wire=%u sex_group=%u job_index=%u online=%u hp=%u/%u mp=%u/%u "
               "wire_order=hp-mp-hpmax-mpmax\n",
               observer ? observer->clientId : 0,
               member->clientId,
               member->onlineRoleId,
               wireId,
               vm_mock_service_team_member_sex_code(member),
               vm_mock_service_team_member_job_code(member),
               member->roleOnline ? 1u : 0u,
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

/* The invitee already owns the self row established by login 5/10.  Its
 * successful 5/3 therefore contributes only the inviter/leader row; subtype
 * 3 uses that first row as the leader id.  Sending the full two-member table
 * here would append a second self row because the client has no replace-list
 * operation in the subtype-3/10 parser. */
static bool vm_net_mock_append_team_joiner_leader_roster_object(
    u8 *out, u32 outCap, u32 *pos,
    const vm_mock_service_client_session *joiner,
    const vm_mock_service_client_session *leader)
{
    u8 groupInfo[256];
    u32 groupInfoLen = 0;
    u32 objectStart = 0;
    u32 leaderWireId = 0;

    if (out == NULL || pos == NULL || joiner == NULL || leader == NULL ||
        leader->onlineRoleId == 0 ||
        !vm_net_mock_append_team_member_full_row(groupInfo, sizeof(groupInfo),
                                                 &groupInfoLen, joiner, leader, true))
    {
        return false;
    }
    leaderWireId = vm_mock_service_team_member_wire_id(joiner, leader);
    if (leaderWireId == 0 ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 3, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "num", 1) ||
        !vm_net_mock_put_object_blob(out, outCap, pos, "groupinfo", groupInfo, groupInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_team_joiner_leader_roster joiner=%08x leader=%08x/%u "
           "wire=%u groupinfo_len=%u lifecycle=5/3-leader-delta\n",
           joiner->clientId, leader->clientId, leader->onlineRoleId,
           leaderWireId, groupInfoLen);
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
    u8 designationInfo[2048];
    u32 designationInfoLen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    const vm_net_mock_designation_entry *activeDesignation = vm_net_mock_role_designation(role);
    u32 roleMoney = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 roleLevel = role ? role->level : 1;
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
        if (selectedDesignation == NULL ||
            !vm_net_mock_designation_is_unlocked(role, selectedDesignation))
        {
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x17, 3, &objectStart))
                return 0;
            if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 0))
                return 0;
            vm_net_mock_finish_wt_object(out, objectStart, pos);
            vm_net_mock_finish_wt_packet(out, pos, 1);
            vm_net_mock_gbk_label_to_utf8(selectedDesignation ? selectedDesignation->name : "", selectedTitleUtf8, sizeof(selectedTitleUtf8));
            printf("[info][network] mock_role_designation23_select role=%u result=0 reason=%s title=%s designation=%u money=%u level=%u min_money=%u min_level=%u req_index=%u req_type=%u req_payload=%s resp=%u\n",
                   roleId,
                   selectedDesignation ? "locked" : "unknown-designation",
                   selectedTitleUtf8,
                   selectedDesignation ? selectedDesignation->id : 0xffu,
                   roleMoney,
                   roleLevel,
                   selectedDesignation ? selectedDesignation->minMoney : 0,
                   selectedDesignation ? selectedDesignation->minLevel : 0,
                   requestIndex,
                   requestType,
                   requestPayloadHex,
                   pos);
            vm_autotest_note("mock_role_designation23_select role=%u result=0 designation=%u money=%u level=%u response=23/3 evidence=JianghuOL.CBE:0x0102A93E\n",
                             roleId,
                             selectedDesignation ? selectedDesignation->id : 0xffu,
                             roleMoney,
                             roleLevel);
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
        printf("[info][network] mock_role_designation23_select role=%u result=1 title=%s designation=%u field_b=%u money=%u level=%u min_money=%u min_level=%u overhead=%s update=23/2 designationinfo_len=%u req_index=%u req_type=%u req_payload=%s resp=%u\n",
               roleId,
               selectedTitleUtf8,
               selectedDesignation->id,
               selectedDesignation->fieldB,
               roleMoney,
               roleLevel,
               selectedDesignation->minMoney,
               selectedDesignation->minLevel,
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
    printf("[info][network] mock_role_designation23_list role=%u count=%u catalog=%u active=%u title=%s money=%u level=%u overhead=%s req_index=%u req_type=%u req_result=%u req_page=%u req_id=%u req_payload=%s designationinfo_len=%u resp=%u\n",
           roleId,
           unlockedCount,
           vm_net_mock_designation_entry_count(),
           activeDesignation->id,
           titleUtf8,
           roleMoney,
           roleLevel,
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

