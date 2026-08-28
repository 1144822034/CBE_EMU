/* Deterministic production-field-writer regression.
 *
 * The standalone service links mock_server_packet_fields.c separately.  A
 * zero-length raw WT field therefore has no source pointer by definition;
 * it must advance no bytes but still succeed.  This is the wire form used by
 * `1/7/42 { booknum=0, booksinfo=[] }` during scene entry.
 *
 * The two stubs satisfy helpers that are compiled into the same production
 * translation unit but are not exercised by this focused boundary test. */

#include <stdio.h>
#include <string.h>

#include "../src/server/mock_server.h"

bool vm_net_mock_seq_put_i16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    (void)out;
    (void)outCap;
    (void)pos;
    (void)value;
    return false;
}

bool vm_net_mock_put_object_blob(u8 *out, u32 outCap, u32 *pos,
                                 const char *name, const u8 *data,
                                 u16 dataLen)
{
    (void)out;
    (void)outCap;
    (void)pos;
    (void)name;
    (void)data;
    (void)dataLen;
    return false;
}

#include "../src/server/mock_server_packet_fields.c"

int main(void)
{
    u8 packet[32];
    u32 pos = 5;

    memset(packet, 0xa5, sizeof(packet));
    if (!vm_net_mock_put_bytes(packet, sizeof(packet), &pos, NULL, 0) ||
        pos != 5 || packet[5] != 0xa5)
    {
        fputs("zero-length field source was rejected or modified bytes\n", stderr);
        return 1;
    }
    if (vm_net_mock_put_bytes(packet, sizeof(packet), &pos, NULL, 1) ||
        pos != 5)
    {
        fputs("non-empty field accepted a null source\n", stderr);
        return 1;
    }
    if (!vm_net_mock_put_bytes(packet, sizeof(packet), &pos, "", 0) ||
        pos != 5)
    {
        fputs("zero-length non-null field changed the stream position\n", stderr);
        return 1;
    }

    puts("packet fields zero-length regression passed");
    return 0;
}
