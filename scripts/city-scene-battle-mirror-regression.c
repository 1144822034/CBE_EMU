/* Regression for the server-owned city combat mirror builder.
 *
 * This is pure resource parsing/encoding: it neither opens a listener nor
 * connects to MySQL, and it never writes the generated resources.  It proves
 * that a c-prefixed city SCE with one normal kind-3 record produces the exact
 * non-c scene key plus a b_ background/map pair required by the wilderness
 * collision screen. */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool count_background_reference(const u8 *payload, u32 payloadLen,
                                       const char *wanted)
{
    u32 matches = 0;

    for (u32 off = 0; off + 12u <= payloadLen; ++off)
    {
        vm_net_mock_sce_named_portal portal;
        u32 end = 0;

        if (vm_net_mock_parse_sce_named_portal_at(payload, payloadLen, off,
                                                   &portal, &end) &&
            strcmp(portal.backgroundScene, wanted) == 0)
        {
            ++matches;
        }
    }
    return matches == 1u;
}

int main(void)
{
    static const char cityScene[] =
        "c04\xC1\xD9\xB0\xB2\xB8\xAE_01.sce"; /* c04临安府_01.sce */
    static const char mirrorScene[] =
        "w04\xC1\xD9\xB0\xB2\xB8\xAE_01.sce"; /* w04临安府_01.sce */
    static const char backgroundScene[] =
        "b_04\xC1\xD9\xB0\xB2\xB8\xAE_01.sce";
    static const char backgroundMap[] =
        "b_04\xC1\xD9\xB0\xB2\xB8\xAE_01.map";
    vm_net_mock_scene_battle_monster_admin_row row;
    u8 sourceRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 sourcePayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 mirrorRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 mirrorPayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 backgroundRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 backgroundPayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    u8 backgroundMapRaw[VM_NET_MOCK_SCENE_BATTLE_CITY_MIRROR_MAP_RAW_MAX];
    u8 templateRaw[VM_NET_MOCK_SCENE_BATTLE_MONSTER_RAW_MAX];
    u8 templatePayload[VM_NET_MOCK_SCENE_BATTLE_MONSTER_PAYLOAD_MAX];
    char mirrorName[64];
    char builtBackgroundName[64];
    char builtBackgroundMapName[64];
    char parsedBackgroundMapName[64];
    char sourceMapName[64];
    char resourcePath[1200];
    u32 sourceRawLen = 0;
    u32 sourcePayloadLen = 0;
    u32 mirrorRawLen = 0;
    u32 mirrorPayloadLen = 0;
    u32 backgroundRawLen = 0;
    u32 backgroundPayloadLen = 0;
    u32 backgroundMapRawLen = 0;
    u32 afterMap = 0;
    u32 slot = 0;
    u32 templateRawLen = 0;
    u32 templatePayloadLen = 0;
    const char *stage = "initial";

    memset(&row, 0, sizeof(row));
    memset(mirrorName, 0, sizeof(mirrorName));
    memset(builtBackgroundName, 0, sizeof(builtBackgroundName));
    memset(builtBackgroundMapName, 0, sizeof(builtBackgroundMapName));
    memset(parsedBackgroundMapName, 0, sizeof(parsedBackgroundMapName));
    memset(sourceMapName, 0, sizeof(sourceMapName));
    memset(resourcePath, 0, sizeof(resourcePath));
    row.monsterId = 1000;
    row.x = 120;
    row.y = 120;
    row.quantity = 1;
    row.visualHint = 17;
    snprintf(row.displayName, sizeof(row.displayName), "mirror tiger");
    snprintf(row.actorResource, sizeof(row.actorResource), "e_tiger.actor");
    snprintf(row.effectResource, sizeof(row.effectResource),
             "e_ghostfireR.actor");

#define REQUIRE_CITY(condition, label)                                           \
    do                                                                            \
    {                                                                             \
        stage = (label);                                                          \
        if (!(condition))                                                         \
            goto failed;                                                          \
    } while (0)
    REQUIRE_CITY(vm_net_mock_set_resource_dir("web/fs/JHOnlineData"),
                 "resource-root");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_read_base_raw(
                     cityScene, sourceRaw, sizeof(sourceRaw), &sourceRawLen),
                 "read-base-city");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_decode_raw_sce(
                     sourceRaw, sourceRawLen, sourcePayload,
                     sizeof(sourcePayload), &sourcePayloadLen),
                 "decode-base-city");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_insert_counted_record(
                     sourcePayload, sizeof(sourcePayload), &sourcePayloadLen,
                     &row),
                 "insert-kind3-row");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_payload_map_name(
                     sourcePayload, sourcePayloadLen, sourceMapName,
                     sizeof(sourceMapName), &afterMap),
                 "source-map-name");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_find_empty_background_slot(
                     sourcePayload, sourcePayloadLen, &slot),
                 "source-empty-field18");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_city_mirror_name(
                     cityScene, mirrorName, sizeof(mirrorName)),
                 "city-mirror-name");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_city_background_names(
                     cityScene, sourceMapName, builtBackgroundName,
                     sizeof(builtBackgroundName), builtBackgroundMapName,
                     sizeof(builtBackgroundMapName)),
                 "city-background-names");
    REQUIRE_CITY(vm_net_mock_open_server_data_resource(
                     "b_03\xB5\xA4\xCF\xBC\xC9\xBD.sce", ".sce", NULL,
                     resourcePath, sizeof(resourcePath)),
                 "background-template-path");
    templateRawLen = vm_net_mock_load_response_file(
        resourcePath, templateRaw, sizeof(templateRaw));
    REQUIRE_CITY(templateRawLen != 0, "background-template-read");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_decode_raw_sce(
                     templateRaw, templateRawLen, templatePayload,
                     sizeof(templatePayload), &templatePayloadLen),
                 "background-template-decode");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_payload_map_name(
                     templatePayload, templatePayloadLen, parsedBackgroundMapName,
                     sizeof(parsedBackgroundMapName), &afterMap),
                 "background-template-map-name");
    REQUIRE_CITY(vm_net_mock_open_server_data_resource(
                     sourceMapName, ".map", NULL, resourcePath,
                     sizeof(resourcePath)),
                 "city-map-path");
    backgroundMapRawLen = vm_net_mock_load_response_file(
        resourcePath, backgroundMapRaw, sizeof(backgroundMapRaw));
    REQUIRE_CITY(backgroundMapRawLen >= 5u && backgroundMapRaw[4] == 2u,
                 "city-map-read");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_build_city_mirror(
                     cityScene, sourcePayload, sourcePayloadLen, mirrorName,
                     sizeof(mirrorName), mirrorRaw, sizeof(mirrorRaw),
                     &mirrorRawLen, builtBackgroundName,
                     sizeof(builtBackgroundName), backgroundRaw,
                     sizeof(backgroundRaw), &backgroundRawLen,
                     builtBackgroundMapName, sizeof(builtBackgroundMapName),
                     backgroundMapRaw, sizeof(backgroundMapRaw),
                     &backgroundMapRawLen),
                 "build-city-mirror");
    REQUIRE_CITY(strcmp(mirrorName, mirrorScene) == 0,
                 "mirror-scene-name");
    REQUIRE_CITY(strcmp(builtBackgroundName, backgroundScene) == 0,
                 "background-scene-name");
    REQUIRE_CITY(strcmp(builtBackgroundMapName, backgroundMap) == 0,
                 "background-map-name");
    REQUIRE_CITY(backgroundMapRawLen != 0, "background-map-bytes");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_decode_raw_sce(
                     mirrorRaw, mirrorRawLen, mirrorPayload,
                     sizeof(mirrorPayload), &mirrorPayloadLen),
                 "decode-mirror");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_payload_has_expanded_row(
                     mirrorPayload, mirrorPayloadLen, &row),
                 "mirror-kind3-row");
    REQUIRE_CITY(count_background_reference(mirrorPayload, mirrorPayloadLen,
                                            backgroundScene),
                 "mirror-background-reference");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_decode_raw_sce(
                     backgroundRaw, backgroundRawLen, backgroundPayload,
                     sizeof(backgroundPayload), &backgroundPayloadLen),
                 "decode-background");
    REQUIRE_CITY(vm_net_mock_scene_battle_monster_payload_map_name(
                     backgroundPayload, backgroundPayloadLen,
                     parsedBackgroundMapName, sizeof(parsedBackgroundMapName),
                     &afterMap),
                 "parse-background-map");
    REQUIRE_CITY(afterMap != 0 &&
                     strcmp(parsedBackgroundMapName, backgroundMap) == 0,
                 "background-map-reference");
    REQUIRE_CITY(!vm_net_mock_scene_battle_monster_city_mirror_name(
                     "01\xCC\xD2\xBB\xA8\xB5\xBA_01.sce", mirrorName,
                     sizeof(mirrorName)),
                 "non-city-rejected"); /* 01桃花岛_01.sce */
#undef REQUIRE_CITY

    puts("city scene battle mirror regression passed: c-city -> wilderness mirror + b-background");
    return 0;

failed:
    fprintf(stderr, "city scene battle mirror contract failed at %s\n", stage);
    return 1;
}
