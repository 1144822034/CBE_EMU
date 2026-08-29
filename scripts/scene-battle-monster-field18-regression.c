/* Regression for the native counted SCE2 entity-list and kind-3 envelope.
 * Pure test: no listener, MySQL connection, or resource write. */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool load_scene_payload(const char *scene, u8 *payload, u32 payloadCap,
                               u32 *payloadLenOut)
{
    u8 raw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u32 rawLen = 0;

    return vm_net_mock_scene_battle_monster_read_base_raw(
               scene, raw, sizeof(raw), &rawLen) &&
           vm_net_mock_scene_battle_monster_decode_raw_sce(
               raw, rawLen, payload, payloadCap, payloadLenOut);
}

static bool same_spawn(const vm_net_mock_sce_combat_spawn *left,
                       const vm_net_mock_sce_combat_spawn *right)
{
    return left->actorId == right->actorId && left->x == right->x &&
           left->y == right->y && left->visualHint == right->visualHint &&
           strcmp(left->displayName, right->displayName) == 0 &&
           strcmp(left->actorResource, right->actorResource) == 0 &&
           strcmp(left->effectResource, right->effectResource) == 0;
}

static bool verify_all_scene_entity_lists(void)
{
    static const char legacyHuashanScene[] =
        "09\xBB\xAA\xC9\xBD_02.sce";
    vm_net_mock_monster_catalog_scene_file
        scenes[VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX];
    u8 payload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u32 sceneCount = vm_net_mock_monster_catalog_collect_scene_files(
        scenes, VM_NET_MOCK_MONSTER_CATALOG_SCENE_FILE_MAX);
    u32 failedCount = 0;
    u32 excludedLegacyCount = 0;

    if (sceneCount == 0)
        return false;
    for (u32 i = 0; i < sceneCount; ++i)
    {
        vm_net_mock_sce_entity_list list;
        u32 payloadLen = 0;

        /* b_*.sce files are battle backdrops loaded by mmBattle, not
         * traversable scene resources using ParseActorFullInfoBlob. */
        if (scenes[i].name[0] == 'b' && scenes[i].name[1] == '_')
            continue;
        /* This shipped legacy resource has a zero client actor count followed
         * by an extra script-placement envelope that is not described by
         * LoadSceneDataFromStream(0x01006204). Production deliberately
         * rejects it instead of byte-scanning for a plausible later count. */
        if (strcmp(scenes[i].name, legacyHuashanScene) == 0)
        {
            ++excludedLegacyCount;
            continue;
        }
        if (!load_scene_payload(scenes[i].name, payload, sizeof(payload),
                                &payloadLen) ||
            !vm_net_mock_scene_battle_monster_parse_entity_list(
                payload, payloadLen, &list) ||
            (list.recordCount == 0u && list.recordsEnd != payloadLen))
        {
            fprintf(stderr, "counted entity-list parse failed for %s\n",
                    scenes[i].name);
            ++failedCount;
        }
    }
    if (failedCount != 0u || excludedLegacyCount != 1u)
    {
        fprintf(stderr, "counted entity-list parse failures: %u/%u\n",
                failedCount, sceneCount);
        return false;
    }
    printf("shipped SCE2 entity lists parsed: scenes=%u legacy_excluded=%u\n",
           sceneCount, excludedLegacyCount);
    return true;
}

static bool verify_scene_insert(const char *scene, u32 expectedCountOffset,
                                u32 expectedRecordCount,
                                u32 expectedRecordsEnd,
                                u32 expectedCombatCount,
                                const vm_net_mock_scene_battle_monster_admin_row *row)
{
    static u8 original[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    static u8 first[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    static u8 second[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    vm_net_mock_sce_entity_list before;
    vm_net_mock_sce_entity_list after;
    vm_net_mock_sce_combat_spawn oldSpawns[32];
    vm_net_mock_sce_combat_spawn current;
    u8 record[512];
    u32 originalLen = 0;
    u32 firstLen = 0;
    u32 secondLen = 0;
    u32 recordLen = 0;
    u32 nodeCountBefore = 0;
    u32 nodeCountAfter = 0;
    u32 tailLen = 0;

    memset(oldSpawns, 0, sizeof(oldSpawns));
    if (!load_scene_payload(scene, original, sizeof(original), &originalLen) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            original, originalLen, &before) ||
        before.countOffset != expectedCountOffset ||
        before.recordCount != expectedRecordCount ||
        before.recordsEnd != expectedRecordsEnd ||
        before.combatRecordCount != expectedCombatCount ||
        expectedCombatCount > sizeof(oldSpawns) / sizeof(oldSpawns[0]) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            original, originalLen, &nodeCountBefore))
    {
        fprintf(stderr, "native entity-list boundary failed for %s\n", scene);
        return false;
    }
    for (u32 i = 0; i < expectedCombatCount; ++i)
    {
        if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                original, originalLen, i, &oldSpawns[i], NULL))
        {
            fprintf(stderr, "native counted spawn %u failed for %s\n", i, scene);
            return false;
        }
    }

    firstLen = originalLen;
    secondLen = originalLen;
    memcpy(first, original, originalLen);
    memcpy(second, original, originalLen);
    if (!vm_net_mock_scene_battle_monster_append_record(
            record, sizeof(record), &recordLen, row))
        return false;
    tailLen = originalLen - before.recordsEnd;
    if (!vm_net_mock_scene_battle_monster_insert_counted_record(
            first, sizeof(first), &firstLen, row) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            first, firstLen, &after) ||
        after.recordCount != before.recordCount + 1u ||
        after.combatRecordCount != before.combatRecordCount + 1u ||
        after.recordsEnd != before.recordsEnd + recordLen ||
        firstLen != originalLen + recordLen ||
        memcmp(first + after.recordsEnd, original + before.recordsEnd,
               tailLen) != 0 ||
        !vm_net_mock_scene_battle_monster_payload_has_row(first, firstLen, row) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            first, firstLen, &nodeCountAfter) ||
        nodeCountAfter != nodeCountBefore + 1u)
    {
        fprintf(stderr, "counted insertion failed for %s\n", scene);
        return false;
    }
    for (u32 i = 0; i < expectedCombatCount; ++i)
    {
        if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                first, firstLen, i, &current, NULL) ||
            !same_spawn(&oldSpawns[i], &current))
        {
            fprintf(stderr, "native spawn order changed for %s\n", scene);
            return false;
        }
    }
    if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
            first, firstLen, expectedCombatCount, &current, NULL) ||
        current.actorId != row->monsterId || current.x != row->x ||
        current.y != row->y)
    {
        fprintf(stderr, "generated spawn is not final counted row for %s\n", scene);
        return false;
    }

    /* A redeploy rebuilds from the immutable base. The same base and config
     * must therefore produce byte-identical output without duplicating rows. */
    if (!vm_net_mock_scene_battle_monster_insert_counted_record(
            second, sizeof(second), &secondLen, row) ||
        secondLen != firstLen || memcmp(second, first, firstLen) != 0)
    {
        fprintf(stderr, "counted redeploy is not deterministic for %s\n", scene);
        return false;
    }

    printf("scene=%s count=%u->%u combat=%u->%u end=%u->%u tail=%u\n",
           scene, before.recordCount, after.recordCount,
           before.combatRecordCount, after.combatRecordCount,
           before.recordsEnd, after.recordsEnd, tailLen);
    return true;
}

/* Saving two draft rows with one monster ID must result in two independent
 * native kind-3 records.  Quantity expansion alone is not sufficient evidence:
 * these rows deliberately share one ID but use different coordinates, which is
 * the operator contract for placing the same monster at multiple points. */
static bool verify_multiple_draft_rows(
    const char *scene,
    const vm_net_mock_scene_battle_monster_admin_row *firstRow)
{
    static u8 first[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    static u8 second[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    vm_net_mock_scene_battle_monster_admin_row secondRow;
    vm_net_mock_sce_entity_list before;
    vm_net_mock_sce_entity_list after;
    u32 firstLen = 0;
    u32 secondLen = 0;
    u32 beforeNodes = 0;
    u32 afterNodes = 0;

    if (scene == NULL || firstRow == NULL ||
        !load_scene_payload(scene, first, sizeof(first), &firstLen) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            first, firstLen, &before) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            first, firstLen, &beforeNodes))
    {
        fputs("multiple draft fixture base parse failed\n", stderr);
        return false;
    }
    secondRow = *firstRow;
    secondRow.x = 200;
    secondRow.y = 140;
    if (secondRow.monsterId != firstRow->monsterId ||
        (secondRow.x == firstRow->x && secondRow.y == firstRow->y))
    {
        fputs("same-id multi-coordinate fixture was not constructed\n", stderr);
        return false;
    }

    memcpy(second, first, firstLen);
    secondLen = firstLen;
    if (!vm_net_mock_scene_battle_monster_insert_counted_record(
            first, sizeof(first), &firstLen, firstRow) ||
        !vm_net_mock_scene_battle_monster_insert_counted_record(
            first, sizeof(first), &firstLen, &secondRow) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            first, firstLen, &after) ||
        !vm_net_mock_scene_battle_monster_payload_collect_node_count(
            first, firstLen, &afterNodes) ||
        after.recordCount != before.recordCount + 2u ||
        after.combatRecordCount != before.combatRecordCount + 2u ||
        afterNodes != beforeNodes + 2u ||
        !vm_net_mock_scene_battle_monster_payload_has_row(
            first, firstLen, firstRow) ||
        !vm_net_mock_scene_battle_monster_payload_has_row(
            first, firstLen, &secondRow))
    {
        fputs("multiple scene battle monster rows were not both compiled\n",
              stderr);
        return false;
    }

    /* Deployment starts from the captured base on every attempt.  Rebuild the
     * two-row payload to ensure neither draft displaces or duplicates the
     * other during a later deployment. */
    if (!vm_net_mock_scene_battle_monster_insert_counted_record(
            second, sizeof(second), &secondLen, firstRow) ||
        !vm_net_mock_scene_battle_monster_insert_counted_record(
            second, sizeof(second), &secondLen, &secondRow) ||
        secondLen != firstLen || memcmp(second, first, firstLen) != 0)
    {
        fputs("multiple scene battle monster rows are not deterministic\n",
              stderr);
        return false;
    }
    puts("multiple scene battle monster draft rows compiled together");
    return true;
}

int main(void)
{
    static const char testScene[] =
        "\xB2\xE2\xCA\xD4\xB5\xD8\xCD\xBC.sce";
    static const char penglaiScene[] =
        "00\xC5\xEE\xC0\xB3\xCF\xC9\xB5\xBA_02.sce";
    static const char taohuaScene[] =
        "01\xCC\xD2\xBB\xA8\xB5\xBA_01.sce";
    static const char huanglingScene[] =
        "05\xC9\xCF\xB9\xC5\xBB\xCA\xC1\xEA_02.sce";
    static const char multiMapPenglaiScene[] =
        "c00\xC5\xEE\xC0\xB3\xCF\xC9\xB5\xBA_03.sce";
    vm_net_mock_scene_battle_monster_admin_row row;
    vm_net_mock_sce_combat_spawn spawn;
    vm_net_mock_sce_entity_list list;
    u8 payload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 record[512];
    u8 malformed[512];
    u8 quantityPayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 encoded[1024];
    u8 raw[1028];
    u8 roundTrip[512];
    u32 payloadLen = 0;
    u32 pos = 0;
    u32 end = 0;
    u32 encodedLen = 0;
    u32 roundTripLen = 0;

    memset(&row, 0, sizeof(row));
    row.monsterId = 1000;
    row.x = 120;
    row.y = 120;
    row.quantity = 1;
    row.visualHint = 5;
    snprintf(row.displayName, sizeof(row.displayName), "monkey");
    snprintf(row.actorResource, sizeof(row.actorResource), "e_monkey.actor");
    snprintf(row.effectResource, sizeof(row.effectResource),
             "e_ghostfireR.actor");

    {
        u16 monkeyHint = 0;
        u16 tigerHint = 0;

        if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData") ||
            !vm_net_mock_scene_battle_monster_body_resource_is_supported(
                row.actorResource) ||
            !vm_net_mock_scene_battle_monster_body_resource_is_supported(
                "e_tiger.actor") ||
            !vm_net_mock_scene_battle_monster_body_visual_hint(
                row.actorResource, &monkeyHint) ||
            monkeyHint != 5 ||
            !vm_net_mock_scene_battle_monster_body_visual_hint(
                "e_tiger.actor", &tigerHint) ||
            tigerHint != 17 ||
            vm_net_mock_scene_battle_monster_body_resource_is_supported(
                "e_huayao.actor") ||
            vm_net_mock_scene_battle_monster_body_resource_is_supported(
                row.effectResource) ||
            !vm_net_mock_scene_battle_monster_append_record(
                record, sizeof(record), &pos, &row) ||
            !vm_net_mock_parse_sce_combat_spawn_at(record, pos, 0, &spawn,
                                                    &end) ||
            end != pos || spawn.actorId != row.monsterId ||
            strcmp(spawn.effectResource, row.effectResource) != 0)
        {
            fputs("complete SCE2 kind-3 envelope failed\n", stderr);
            return 1;
        }
    }

    {
        vm_net_mock_scene_battle_monster_admin_row tigerRow = row;
        u32 tigerRecordLen = 0;

        snprintf(tigerRow.actorResource, sizeof(tigerRow.actorResource),
                 "e_tiger.actor");
        tigerRow.visualHint = 5; /* stale UI value must not alter field 16 */
        if (!vm_net_mock_scene_battle_monster_append_record(
                record, sizeof(record), &tigerRecordLen, &tigerRow) ||
            !vm_net_mock_parse_sce_combat_spawn_at(
                record, tigerRecordLen, 0, &spawn, &end) ||
            end != tigerRecordLen || spawn.visualHint != 17)
        {
            fputs("native field16 actor profile was not preserved\n", stderr);
            return 1;
        }
    }

    {
        static const u16 expectedX[5] = {120, 104, 136, 120, 120};
        static const u16 expectedY[5] = {120, 120, 120, 104, 136};
        vm_net_mock_scene_battle_monster_admin_row quantityRow = row;
        vm_net_mock_scene_battle_monster_admin_row expanded;
        vm_net_mock_sce_entity_list before;
        vm_net_mock_sce_entity_list after;
        u32 quantityPayloadLen = 0;
        u32 secondPayloadLen = 0;
        u32 beforeNodeCount = 0;
        u32 afterNodeCount = 0;

        quantityRow.quantity = 5;
        if (!load_scene_payload(testScene, payload, sizeof(payload),
                                &quantityPayloadLen) ||
            !vm_net_mock_scene_battle_monster_parse_entity_list(
                payload, quantityPayloadLen, &before) ||
            !vm_net_mock_scene_battle_monster_payload_collect_node_count(
                payload, quantityPayloadLen, &beforeNodeCount))
        {
            fputs("quantity fixture base parse failed\n", stderr);
            return 1;
        }
        for (u32 ordinal = 0; ordinal < quantityRow.quantity; ++ordinal)
        {
            if (!vm_net_mock_scene_battle_monster_expanded_row(
                    &quantityRow, ordinal, &expanded) ||
                expanded.x != expectedX[ordinal] ||
                expanded.y != expectedY[ordinal] ||
                !vm_net_mock_scene_battle_monster_insert_counted_record(
                    payload, sizeof(payload), &quantityPayloadLen, &expanded))
            {
                fputs("quantity expansion failed\n", stderr);
                return 1;
            }
        }
        if (!vm_net_mock_scene_battle_monster_parse_entity_list(
                payload, quantityPayloadLen, &after) ||
            !vm_net_mock_scene_battle_monster_payload_collect_node_count(
                payload, quantityPayloadLen, &afterNodeCount) ||
            after.recordCount != before.recordCount + 5u ||
            after.combatRecordCount != before.combatRecordCount + 5u ||
            afterNodeCount != beforeNodeCount + 5u ||
            !vm_net_mock_scene_battle_monster_payload_has_expanded_row(
                payload, quantityPayloadLen, &quantityRow))
        {
            fputs("quantity expanded entity-list contract failed\n", stderr);
            return 1;
        }
        memcpy(quantityPayload, payload, quantityPayloadLen);
        if (!load_scene_payload(testScene, payload, sizeof(payload),
                                &secondPayloadLen))
        {
            fputs("quantity deterministic rebuild base failed\n", stderr);
            return 1;
        }
        for (u32 ordinal = 0; ordinal < quantityRow.quantity; ++ordinal)
        {
            if (!vm_net_mock_scene_battle_monster_expanded_row(
                    &quantityRow, ordinal, &expanded) ||
                !vm_net_mock_scene_battle_monster_insert_counted_record(
                    payload, sizeof(payload), &secondPayloadLen, &expanded))
            {
                fputs("quantity deterministic rebuild failed\n", stderr);
                return 1;
            }
        }
        if (secondPayloadLen != quantityPayloadLen ||
            memcmp(payload, quantityPayload, quantityPayloadLen) != 0)
        {
            fputs("quantity redeploy was not byte-identical\n", stderr);
            return 1;
        }
    }

    if (vm_net_mock_parse_sce_combat_spawn_at(
            record, pos - (u32)(5u + strlen(row.effectResource)), 0,
            &spawn, &end))
    {
        fputs("truncated kind-3 effect tail was accepted\n", stderr);
        return 1;
    }
    memcpy(malformed, record, pos);
    {
        u32 effectOffset = pos - (u32)(5u + strlen(row.effectResource));
        memmove(malformed + effectOffset + 2u, malformed + effectOffset + 4u,
                1u + strlen(row.effectResource));
    }
    if (vm_net_mock_parse_sce_combat_spawn_at(
            malformed, pos - 2u, 0, &spawn, &end))
    {
        fputs("short kind-3 effect tail was accepted\n", stderr);
        return 1;
    }

    if (!verify_all_scene_entity_lists() ||
        !verify_multiple_draft_rows(testScene, &row) ||
        !verify_scene_insert(testScene, 38u, 6u, 428u, 0u, &row) ||
        !verify_scene_insert(penglaiScene, 100u, 4u, 395u, 1u, &row) ||
        !verify_scene_insert(taohuaScene, 102u, 8u, 619u, 4u, &row) ||
        !verify_scene_insert(huanglingScene, 40u, 7u, 578u, 4u, &row) ||
        !verify_scene_insert(multiMapPenglaiScene, 111u, 4u, 365u, 0u,
                             &row))
    {
        return 1;
    }

    /* The historical test-map row begins exactly where the client's six-row
     * loop ends. It must not be reported as visible before insertion. */
    if (!load_scene_payload(testScene, payload, sizeof(payload), &payloadLen) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &list) ||
        vm_net_mock_scene_battle_monster_payload_has_row(
            payload, payloadLen, &row) ||
        !vm_net_mock_parse_sce_combat_spawn_at(
            payload, payloadLen, list.recordsEnd, &spawn, &end) ||
        spawn.actorId != 1001u || end != payloadLen)
    {
        fputs("test-map out-of-count historical row boundary failed\n", stderr);
        return 1;
    }

    /* Penglai's native monkey is the final counted entity. The later kind-3,
     * kind-8 and zero word are trailing bytes and must remain untouched. */
    if (!load_scene_payload(penglaiScene, payload, sizeof(payload), &payloadLen) ||
        !vm_net_mock_scene_battle_monster_parse_entity_list(
            payload, payloadLen, &list) ||
        !vm_net_mock_scene_battle_monster_counted_spawn_at(
            payload, payloadLen, 0, &spawn, NULL) ||
        spawn.actorId != 1000u || spawn.x != 120u || spawn.y != 120u ||
        vm_net_mock_read_le16_at(payload, 469u) != 8u ||
        vm_net_mock_read_le16_at(payload, payloadLen - 2u) != 0u)
    {
        fputs("Penglai native monkey counted-row contract failed\n", stderr);
        return 1;
    }

    if (!vm_net_mock_scene_battle_monster_lzss_literal_encode(
            record, pos, encoded, sizeof(encoded), &encodedLen) ||
        encodedLen > sizeof(raw) - 4u || encoded[0] != 2u)
    {
        fputs("SCE2 type-2 wrapper emission failed\n", stderr);
        return 1;
    }
    raw[0] = (u8)encodedLen;
    raw[1] = (u8)(encodedLen >> 8);
    raw[2] = (u8)(encodedLen >> 16);
    raw[3] = (u8)(encodedLen >> 24);
    memcpy(raw + 4, encoded, encodedLen);
    roundTripLen = vm_net_mock_decode_lzss_resource_stream(
        raw + 4, encodedLen, roundTrip, sizeof(roundTrip));
    if (roundTripLen != pos || memcmp(roundTrip, record, pos) != 0)
    {
        fputs("SCE2 type-2 wrapper round trip failed\n", stderr);
        return 1;
    }

    printf("scene battle monster counted-entity regression passed: record=%u\n",
           pos);
    return 0;
}
