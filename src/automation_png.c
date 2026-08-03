#include "automation_png.h"

#include <stdio.h>
#include <stdlib.h>

static u32 automation_png_crc32(const u8 *data, u32 length)
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

static u32 automation_png_adler32(const u8 *data, u32 length)
{
    u32 a = 1;
    u32 b = 0;
    for (u32 i = 0; i < length; ++i)
    {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static int automation_png_put_be32(FILE *stream, u32 value)
{
    u8 bytes[4] = {
        (u8)(value >> 24), (u8)(value >> 16),
        (u8)(value >> 8), (u8)value};
    return fwrite(bytes, 1, sizeof(bytes), stream) == sizeof(bytes);
}

static int automation_png_write_chunk(FILE *stream, const char type[4],
                                      const u8 *data, u32 length)
{
    u8 *crcData;
    u32 crc;
    int ok = 0;

    if (stream == NULL || type == NULL)
        return 0;
    crcData = (u8 *)malloc((size_t)length + 4u);
    if (crcData == NULL)
        return 0;
    memcpy(crcData, type, 4);
    if (length != 0 && data != NULL)
        memcpy(crcData + 4, data, length);
    crc = automation_png_crc32(crcData, length + 4u);
    free(crcData);

    ok = automation_png_put_be32(stream, length) &&
         fwrite(type, 1, 4, stream) == 4 &&
         (length == 0 || fwrite(data, 1, length, stream) == length) &&
         automation_png_put_be32(stream, crc);
    return ok;
}

int automation_png_write_rgb565(const char *path, const u8 *pixels,
                                u32 width, u32 height)
{
    static const u8 signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    u8 ihdr[13] = {0};
    u8 *raw = NULL;
    u8 *zlib = NULL;
    u32 rowBytes;
    u32 rawLen;
    u32 blocks;
    u32 zlibLen;
    u32 rawAt = 0;
    u32 zlibAt = 0;
    FILE *stream = NULL;
    int ok = 0;

    if (path == NULL || pixels == NULL || width == 0 || height == 0 ||
        width > 4096u || height > 4096u)
        return 0;
    if (width > (0xffffffffu - 1u) / 3u)
        return 0;
    rowBytes = width * 3u + 1u;
    if (height > 0xffffffffu / rowBytes)
        return 0;
    rawLen = rowBytes * height;
    blocks = (rawLen + 65534u) / 65535u;
    if (blocks > (0xffffffffu - 6u) / 5u)
        return 0;
    zlibLen = 2u + rawLen + blocks * 5u + 4u;
    raw = (u8 *)malloc(rawLen);
    zlib = (u8 *)malloc(zlibLen);
    if (raw == NULL || zlib == NULL)
        goto done;

    for (u32 y = 0; y < height; ++y)
    {
        raw[rawAt++] = 0; /* PNG filter: None. */
        for (u32 x = 0; x < width; ++x)
        {
            u16 pixel = (u16)pixels[((size_t)y * width + x) * 2u] |
                        ((u16)pixels[((size_t)y * width + x) * 2u + 1u] << 8);
            raw[rawAt++] = (u8)((((pixel >> 11) & 31u) * 255u + 15u) / 31u);
            raw[rawAt++] = (u8)((((pixel >> 5) & 63u) * 255u + 31u) / 63u);
            raw[rawAt++] = (u8)(((pixel & 31u) * 255u + 15u) / 31u);
        }
    }

    zlib[zlibAt++] = 0x78; /* zlib + uncompressed DEFLATE blocks. */
    zlib[zlibAt++] = 0x01;
    rawAt = 0;
    while (rawAt < rawLen)
    {
        u32 blockLen = rawLen - rawAt;
        u16 len16;
        u16 nlen16;
        if (blockLen > 65535u)
            blockLen = 65535u;
        zlib[zlibAt++] = (rawAt + blockLen == rawLen) ? 1u : 0u;
        len16 = (u16)blockLen;
        nlen16 = (u16)~len16;
        zlib[zlibAt++] = (u8)len16;
        zlib[zlibAt++] = (u8)(len16 >> 8);
        zlib[zlibAt++] = (u8)nlen16;
        zlib[zlibAt++] = (u8)(nlen16 >> 8);
        memcpy(zlib + zlibAt, raw + rawAt, blockLen);
        zlibAt += blockLen;
        rawAt += blockLen;
    }
    {
        u32 adler = automation_png_adler32(raw, rawLen);
        zlib[zlibAt++] = (u8)(adler >> 24);
        zlib[zlibAt++] = (u8)(adler >> 16);
        zlib[zlibAt++] = (u8)(adler >> 8);
        zlib[zlibAt++] = (u8)adler;
    }
    if (zlibAt != zlibLen)
        goto done;

    ihdr[0] = (u8)(width >> 24);
    ihdr[1] = (u8)(width >> 16);
    ihdr[2] = (u8)(width >> 8);
    ihdr[3] = (u8)width;
    ihdr[4] = (u8)(height >> 24);
    ihdr[5] = (u8)(height >> 16);
    ihdr[6] = (u8)(height >> 8);
    ihdr[7] = (u8)height;
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* true-colour */
    stream = fopen(path, "wb");
    if (stream == NULL)
        goto done;
    ok = fwrite(signature, 1, sizeof(signature), stream) == sizeof(signature) &&
         automation_png_write_chunk(stream, "IHDR", ihdr, sizeof(ihdr)) &&
         automation_png_write_chunk(stream, "IDAT", zlib, zlibLen) &&
         automation_png_write_chunk(stream, "IEND", NULL, 0);

done:
    if (stream != NULL)
        fclose(stream);
    free(zlib);
    free(raw);
    return ok;
}
