/*
 * Regression for the complete native SCE2 kind-3 battle-monster record.
 *
 * The deployment compiler must emit the native effect-actor tail after field
 * 17: `u16 kind=3, u16 kind=3, u8 length, bytes`.  The production parser
 * must reject both a truncated record and the historical short-tail server
 * envelope,
 * because LoadSceneDataFromStream does not create a live type-2 scene node
 * for that invented grammar.  This test is pure: it opens no listener, uses
 * no MySQL and writes no resources.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_net_mock_scene_battle_monster_admin_row row;
    vm_net_mock_sce_combat_spawn spawn;
    u8 record[256];
    u8 malformed[256];
    u8 encoded[512];
    u8 raw[516];
    u8 roundTrip[256];
    u8 nativeStream[512];
    u8 sharedMapScene[64];
    u8 recoveredPrefix[14];
    const char sharedMapName[] =
        "01\xCC\xD2\xBB\xA8\xB5\xBA_01.map"; /* GBK: 01桃花岛_01.map */
    const u8 terminalFixture[] = {
        0xaa, 0xbb, 0xcc,
        8, 0, 0x46, 0, 0x26, 0, 1, 0, 1, 0, 0x26, 0, 0, 0
    };
    const u8 zeroTerminalFixture[] = { 0xaa, 0xbb, 0x00, 0x00 };
    u32 pos = 0;
    u32 end = 0;
    u32 truncatedLen = 0;
    u32 malformedLen = 0;
    u32 encodedLen = 0;
    u32 roundTripLen = 0;
    u32 insertOffset = 0;
    u32 terminalLen = 0;
    u32 nativeStreamLen = 0;
    u32 sharedMapSceneLen = 0;

    memset(&row, 0, sizeof(row));
    memset(&spawn, 0, sizeof(spawn));
    row.monsterId = 1000;
    row.x = 120;
    row.y = 120;
    row.visualHint = 5;
    snprintf(row.displayName, sizeof(row.displayName), "monkey");
    snprintf(row.actorResource, sizeof(row.actorResource), "e_monkey.actor");
    snprintf(row.effectResource, sizeof(row.effectResource),
             "e_ghostfireR.actor");

    if (!vm_net_mock_scene_battle_monster_append_record(
            record, sizeof(record), &pos, &row) || pos == 0 ||
        !vm_net_mock_parse_sce_combat_spawn_at(record, pos, 0, &spawn, &end) ||
        end != pos || spawn.actorId != row.monsterId || spawn.x != row.x ||
        spawn.y != row.y || strcmp(spawn.displayName, row.displayName) != 0 ||
        strcmp(spawn.actorResource, row.actorResource) != 0 ||
        strcmp(spawn.effectResource, row.effectResource) != 0)
    {
        fputs("complete SCE2 kind-3 field18 contract failed\n", stderr);
        return 1;
    }

    /* 桃花岛's first 毒泥怪 is preceded by this exact native boundary:
     * kind-8 (70,38), scalar field 1/value 38, zero word, then kind-3. */
    {
        const u8 taohuaPrefix[] = {
            8, 0, 70, 0, 38, 0, 1, 0, 1, 0, 38, 0, 0, 0
        };

        memcpy(nativeStream, taohuaPrefix, sizeof(taohuaPrefix));
        nativeStreamLen = sizeof(taohuaPrefix);
        if (!vm_net_mock_scene_battle_monster_append_record(
                nativeStream, sizeof(nativeStream), &nativeStreamLen, &row) ||
            !vm_net_mock_scene_battle_monster_has_spawn_prefix(
                nativeStream, nativeStreamLen, nativeStreamLen))
        {
            fputs("native short-marker/zero/kind-3 stream was not recognized\n",
                  stderr);
            return 1;
        }
        if (vm_net_mock_scene_battle_monster_has_spawn_prefix(
                record, pos, pos))
        {
            fputs("bare kind-3 record was mistaken for a native spawn stream\n",
                  stderr);
            return 1;
        }
    }

    /* A custom scene may reuse 桃花岛's MAP while omitting the marker.  The
     * compiler must recover the authored marker from a shipped same-MAP SCE
     * instead of deriving it from the requested monster coordinates. */
    memset(sharedMapScene, 0, sizeof(sharedMapScene));
    memcpy(sharedMapScene, "SCE2", 4);
    sharedMapScene[4] = 0x40;
    sharedMapScene[5] = 0x01;
    sharedMapScene[6] = 0xd0;
    sharedMapScene[7] = 0x01;
    sharedMapScene[8] = 1;
    sharedMapScene[10] = (u8)strlen(sharedMapName);
    memcpy(sharedMapScene + 11, sharedMapName, strlen(sharedMapName));
    sharedMapSceneLen = 11u + (u32)strlen(sharedMapName) + 4u;
    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData") ||
        !vm_net_mock_scene_battle_monster_find_native_spawn_prefix(
            sharedMapScene, sharedMapSceneLen, recoveredPrefix,
            sizeof(recoveredPrefix)) ||
        vm_net_mock_read_le16_at(recoveredPrefix, 0) != 8u ||
        vm_net_mock_read_le16_at(recoveredPrefix, 2) != 70u ||
        vm_net_mock_read_le16_at(recoveredPrefix, 4) != 38u ||
        vm_net_mock_read_le16_at(recoveredPrefix, 10) != 38u ||
        vm_net_mock_read_le16_at(recoveredPrefix, 12) != 0u)
    {
        fputs("same-MAP native spawn prefix recovery failed\n", stderr);
        return 1;
    }

    if (!vm_net_mock_scene_battle_monster_find_insert_offset(
            terminalFixture, sizeof(terminalFixture), &insertOffset,
            &terminalLen) ||
        insertOffset != 3u || terminalLen != 14u)
    {
        fputs("native SCE2 final kind-8 insertion boundary was not recovered\n",
              stderr);
        return 1;
    }

    if (!vm_net_mock_scene_battle_monster_find_insert_offset(
            zeroTerminalFixture, sizeof(zeroTerminalFixture), &insertOffset,
            &terminalLen) ||
        insertOffset != 2u || terminalLen != 2u)
    {
        fputs("native SCE2 zero-word insertion boundary was not recovered\n",
              stderr);
        return 1;
    }

    /* Verify the output against the field-18 bytes in shipped kind-3
     * records.  This assertion is intentionally independent from the
     * production parser: emitting `3,18` would otherwise round-trip through
     * a matching-but-wrong emitter/parser pair. */
    {
        const u8 nativeEffectEnvelope[] = { 3, 0, 3, 0 };
        u32 envelopeOffset = pos - (u32)(sizeof(nativeEffectEnvelope) + 1u +
                                         strlen(row.effectResource));
        if (memcmp(record + envelopeOffset, nativeEffectEnvelope,
                   sizeof(nativeEffectEnvelope)) != 0)
        {
            fputs("native SCE2 field18 envelope was not emitted\n", stderr);
            return 1;
        }
    }

    /* Remove exactly the final native string field.  A permissive parser
     * would accept this historical bad output and make deployment appear to
     * succeed even though the real client cannot instantiate the node. */
    truncatedLen = pos - (u32)(5u + strlen(row.effectResource));
    if (vm_net_mock_parse_sce_combat_spawn_at(record, truncatedLen, 0,
                                               &spawn, &end))
    {
        fputs("truncated SCE2 kind-3 record was accepted\n", stderr);
        return 1;
    }

    /* Reproduce the malformed short tail: remove the required second
     * `u16 3` before the native one-byte effect length. */
    malformedLen = pos - 2u;
    memcpy(malformed, record, pos);
    {
        u32 envelopeOffset = pos - (u32)(5u + strlen(row.effectResource));
        memmove(malformed + envelopeOffset + 2u,
                malformed + envelopeOffset + 4u,
                1u + strlen(row.effectResource));
    }
    if (vm_net_mock_parse_sce_combat_spawn_at(malformed, malformedLen, 0,
                                               &spawn, &end))
    {
        fputs("historical short-tail SCE2 field18 envelope was accepted\n",
              stderr);
        return 1;
    }

    /* The outer resource is part of the client contract too.  Type 1 is a
     * literal payload, whereas the literal-run encoder above emits type-2
     * LZSS tokens.  A type-1 wrapper would make the CBE feed the compression
     * header to LoadSceneDataFromStream instead of the SCE2 bytes. */
    if (!vm_net_mock_scene_battle_monster_lzss_literal_encode(
            record, pos, encoded, sizeof(encoded), &encodedLen) ||
        encodedLen < 10 || encoded[0] != 2 || encodedLen > sizeof(raw) - 4)
    {
        fputs("SCE2 type-2 resource wrapper was not emitted\n", stderr);
        return 1;
    }
    raw[0] = (u8)encodedLen;
    raw[1] = (u8)(encodedLen >> 8);
    raw[2] = (u8)(encodedLen >> 16);
    raw[3] = (u8)(encodedLen >> 24);
    memcpy(raw + 4, encoded, encodedLen);
    /* This fixture contains one record rather than a full SCE2 payload, so
     * exercise the resource container decoder directly.  Deployment itself
     * additionally uses vm_net_mock_scene_battle_monster_decode_raw_sce() to
     * assert the complete result begins with SCE2. */
    roundTripLen = vm_net_mock_decode_lzss_resource_stream(
        raw + 4, encodedLen, roundTrip, sizeof(roundTrip));
    if (roundTripLen != pos ||
        memcmp(roundTrip, record, pos) != 0)
    {
        fputs("SCE2 resource wrapper did not round-trip to the record\n", stderr);
        return 1;
    }

    printf("scene battle monster field18 regression passed: bytes=%u actor=%s effect=%s\n",
           pos, row.actorResource, row.effectResource);
    return 0;
}
