/*
 * Client-side contract regression for the login backpack bootstrap split.
 *
 * It builds neither a listener nor an emulator session.  The fixture models
 * the previously failing large group/type-1 reply: the complete 30/21 grid is
 * retained, but the normal event-7 queue receives group state first and the
 * grid/reservoir/equipment initializer second.  This keeps the four stage
 * rows on the first live equipment instance without recreating the old
 * combined-packet parser-pool failure.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

static bool begin_object(u8 *packet, u32 cap, u32 *pos, u8 kind, u8 subtype,
                         u32 *startOut)
{
    if (packet == NULL || pos == NULL || *pos + 6 > cap)
        return false;
    if (startOut != NULL)
        *startOut = *pos;
    packet[(*pos)++] = 1;
    packet[(*pos)++] = kind;
    packet[(*pos)++] = subtype;
    packet[(*pos)++] = 0;
    packet[(*pos)++] = 0;
    packet[(*pos)++] = 0;
    return true;
}

static bool finish_object(u8 *packet, u32 cap, u32 start, u32 pos)
{
    u32 length = pos - start;

    if (packet == NULL || start + 6 > pos || pos > cap || length > 0xffffu)
        return false;
    packet[start + 4] = (u8)(length >> 8);
    packet[start + 5] = (u8)length;
    return true;
}

static bool append_sized_object(u8 *packet, u32 cap, u32 *pos, u8 kind,
                                u8 subtype, u16 objectLen)
{
    u32 start = 0;
    u16 valueLen = 0;

    /* A one-byte field name and a BE16 value length consume four payload
     * bytes.  The actual CBE parser is not exercised here; valid fields make
     * this synthetic packet suitable for the same transport object reader. */
    if (objectLen < 10 || !begin_object(packet, cap, pos, kind, subtype, &start))
        return false;
    valueLen = (u16)(objectLen - 10u);
    if (*pos + 4u + valueLen > cap)
        return false;
    packet[(*pos)++] = 1;
    packet[(*pos)++] = 'x';
    packet[(*pos)++] = (u8)(valueLen >> 8);
    packet[(*pos)++] = (u8)valueLen;
    memset(packet + *pos, 0, valueLen);
    *pos += valueLen;
    return finish_object(packet, cap, start, *pos);
}

static bool append_typed_object(u8 *packet, u32 cap, u32 *pos, u16 objectLen,
                                u8 type)
{
    u32 start = 0;
    u16 fillerLen = 0;

    /* `type` uses the same tagged-u8 representation as the real 7/7 rows. */
    if (objectLen < 20 || !begin_object(packet, cap, pos, 7, 7, &start))
        return false;
    if (*pos + 10 > cap)
        return false;
    packet[(*pos)++] = 4;
    memcpy(packet + *pos, "type", 4);
    *pos += 4;
    packet[(*pos)++] = 0;
    packet[(*pos)++] = 3;
    packet[(*pos)++] = 0;
    packet[(*pos)++] = 1;
    packet[(*pos)++] = type;

    fillerLen = (u16)(objectLen - 20u);
    if (*pos + 4u + fillerLen > cap)
        return false;
    packet[(*pos)++] = 1;
    packet[(*pos)++] = 'x';
    packet[(*pos)++] = (u8)(fillerLen >> 8);
    packet[(*pos)++] = (u8)fillerLen;
    memset(packet + *pos, 0, fillerLen);
    *pos += fillerLen;
    return finish_object(packet, cap, start, *pos);
}

static void finish_packet(u8 *packet, u32 length, u8 objectCount)
{
    packet[0] = 'W';
    packet[1] = 'T';
    packet[2] = (u8)(length >> 8);
    packet[3] = (u8)length;
    packet[4] = objectCount;
}

static u32 parser_pool_cost(const u8 *packet, u32 length)
{
    if (packet == NULL || length < 5 || packet[4] > 10 ||
        length < 5u + (u32)packet[4] * 6u)
    {
        return 0;
    }
    /* Confirmed by event_packet_init(10,19): object table, per-object field
     * table, then copied payload fields (the two outer length prefixes are
     * not copied). */
    return 10u * 88u + (u32)packet[4] * (19u * 12u) +
           (length - 5u - (u32)packet[4] * 6u) - 2u * 19u;
}

static int expect_object(const u8 *packet, u32 length, u8 index, u8 kind,
                         u8 subtype, int expectedType)
{
    u32 offset = 5;
    vm_client_wt_object object;

    if (packet == NULL || index >= packet[4])
        return 1;
    for (u8 current = 0; current <= index; ++current)
    {
        if (!vm_client_next_wt_object(packet, length, &offset, &object))
            return 1;
    }
    if (object.major != 1 || object.kind != kind || object.subtype != subtype)
        return 1;
    if (expectedType >= 0)
    {
        u8 type = 0;
        if (!vm_client_wt_object_tagged_u8(&object, "type", &type) ||
            type != (u8)expectedType)
        {
            return 1;
        }
    }
    return 0;
}

static int check_large_login_bootstrap_split(void)
{
    enum { documentedFailureCost = 6283 };
    u8 response[8192] = {0};
    u8 followup[8192] = {0};
    u32 length = 5;
    u32 followupLen = 0;
    u32 originalCost = 0;

    /* Object lengths mirror the archived large bootstrap: 5/10, 10/26,
     * 30/21 (2950), 7/11, 7/7 type-2, 7/20 and 7/32.  The current required
     * type-3 completion is appended as an additional small object. */
    if (!append_sized_object(response, sizeof(response), &length, 5, 10, 104) ||
        !append_sized_object(response, sizeof(response), &length, 10, 26, 65) ||
        !append_sized_object(response, sizeof(response), &length, 30, 21, 2950) ||
        !append_sized_object(response, sizeof(response), &length, 7, 11, 46) ||
        !append_typed_object(response, sizeof(response), &length, 662, 2) ||
        !append_typed_object(response, sizeof(response), &length, 20, 3) ||
        !append_sized_object(response, sizeof(response), &length, 7, 20, 29) ||
        !append_sized_object(response, sizeof(response), &length, 7, 32, 31))
    {
        fputs("could not construct large login bootstrap fixture\n", stderr);
        return 1;
    }
    finish_packet(response, length, 8);
    originalCost = parser_pool_cost(response, length);
    if (originalCost <= documentedFailureCost)
    {
        fprintf(stderr, "fixture no longer exceeds documented parser failure: %u\n",
                originalCost);
        return 1;
    }
    if (!vm_client_extract_login_backpack_bootstrap_followup(
            response, &length, followup, sizeof(followup), &followupLen))
    {
        fputs("large login bootstrap was not split\n", stderr);
        return 1;
    }
    if (response[4] != 4 || followup[4] != 4 ||
        parser_pool_cost(response, length) >= documentedFailureCost ||
        parser_pool_cost(followup, followupLen) >= documentedFailureCost ||
        expect_object(response, length, 0, 5, 10, -1) != 0 ||
        expect_object(response, length, 1, 10, 26, -1) != 0 ||
        expect_object(response, length, 2, 7, 20, -1) != 0 ||
        expect_object(response, length, 3, 7, 32, -1) != 0 ||
        expect_object(followup, followupLen, 0, 30, 21, -1) != 0 ||
        expect_object(followup, followupLen, 1, 7, 11, -1) != 0 ||
        expect_object(followup, followupLen, 2, 7, 7, 2) != 0 ||
        expect_object(followup, followupLen, 3, 7, 7, 3) != 0)
    {
        fprintf(stderr,
                "bootstrap split lost order or remained over parser budget: "
                "primary=%u/%u followup=%u/%u\n",
                length, parser_pool_cost(response, length), followupLen,
                parser_pool_cost(followup, followupLen));
        return 1;
    }
    return 0;
}

static int check_non_bootstrap_is_untouched(void)
{
    u8 response[128] = {0};
    u8 snapshot[128] = {0};
    u8 followup[128] = {0};
    u32 length = 5;
    u32 followupLen = 0;

    if (!append_sized_object(response, sizeof(response), &length, 5, 10, 20) ||
        !append_sized_object(response, sizeof(response), &length, 30, 21, 24))
    {
        return 1;
    }
    finish_packet(response, length, 2);
    memcpy(snapshot, response, length);
    if (vm_client_extract_login_backpack_bootstrap_followup(
            response, &length, followup, sizeof(followup), &followupLen) ||
        followupLen != 0 || length != 49 || memcmp(response, snapshot, length) != 0)
    {
        fputs("non-bootstrap response was incorrectly split\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (check_large_login_bootstrap_split() != 0 ||
        check_non_bootstrap_is_untouched() != 0)
    {
        return 1;
    }
    puts("equipment enhancement bootstrap split regression passed");
    return 0;
}
