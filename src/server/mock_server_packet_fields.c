#include "mock_server.h"

bool vm_net_mock_put_bytes(u8 *out, u32 outCap, u32 *pos, const void *data, u32 len)
{
    /* A zero-length WT field has no byte source.  The aggregate server helper
     * already accepts that representation; keep the independently linked
     * production helper identical so `7/42 { booksinfo=[] }` remains a valid
     * scene-start object.  Non-empty writes still require real source data. */
    if (out == NULL || pos == NULL || *pos > outCap || len > outCap - *pos ||
        (len != 0 && data == NULL))
    {
        return false;
    }
    if (len != 0)
        memcpy(out + *pos, data, len);
    *pos += len;
    return true;
}

bool vm_net_mock_put_be16(u8 *out, u32 outCap, u32 *pos, u16 value)
{
    u8 bytes[2] = {(u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

bool vm_net_mock_put_be32(u8 *out, u32 outCap, u32 *pos, u32 value)
{
    u8 bytes[4] = {(u8)(value >> 24), (u8)(value >> 16),
                   (u8)(value >> 8), (u8)value};
    return vm_net_mock_put_bytes(out, outCap, pos, bytes, sizeof(bytes));
}

bool vm_net_mock_seq_put_string(u8 *out, u32 outCap, u32 *pos, const char *value)
{
    u16 len = value ? (u16)(strlen(value) + 1) : 1;
    return vm_net_mock_put_be16(out, outCap, pos, len) &&
           vm_net_mock_put_bytes(out, outCap, pos, value ? value : "", len);
}

/* Most WT text fields are length-delimited and must not include a trailing
 * byte.  A few legacy handlers instead pass GetString() straight to libc-like
 * `%s` formatting.  Those fields must retain the protocol's blob wrapper but
 * include a terminal NUL in its inner payload. */
bool vm_net_mock_put_object_cstring(u8 *out, u32 outCap, u32 *pos,
                                    const char *name, const char *value)
{
    size_t valueLen = value ? strlen(value) : 0;
    if (valueLen >= 0xffff)
        return false;
    return vm_net_mock_put_object_blob(out, outCap, pos, name,
                                       (const u8 *)(value ? value : ""),
                                       (u16)(valueLen + 1));
}

bool vm_net_mock_seq_put_string_list(
    u8 *out, u32 outCap, u32 *pos, const char *const *values, u32 count)
{
    u32 i = 0;
    for (i = 0; i < count; ++i)
    {
        if (!vm_net_mock_seq_put_string(out, outCap, pos, values[i]))
            return false;
    }
    return true;
}

u32 vm_net_mock_build_pos_info(u8 *out, u32 outCap, u16 x, u16 y)
{
    u32 pos = 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, x))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, y))
        return 0;
    return pos;
}
