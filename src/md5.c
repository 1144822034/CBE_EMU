#include "md5.h"

#include <stdint.h>
#include <string.h>

typedef struct
{
    uint32_t state[4];
    uint64_t byte_count;
    uint8_t block[64];
    size_t block_length;
} vm_md5_context;

static uint32_t vm_md5_rotate_left(uint32_t value, unsigned int shift)
{
    return (value << shift) | (value >> (32u - shift));
}

static uint32_t vm_md5_read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static void vm_md5_transform(vm_md5_context *context, const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
        0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
        0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
        0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
        0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
        0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
        0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
        0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
        0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
    };
    static const uint8_t shifts[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    uint32_t words[16];
    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];

    for (unsigned int i = 0; i < 16; ++i)
        words[i] = vm_md5_read_le32(block + i * 4);

    for (unsigned int i = 0; i < 64; ++i)
    {
        uint32_t function_value = 0;
        unsigned int word_index = 0;
        uint32_t old_d = d;

        if (i < 16)
        {
            function_value = (b & c) | ((~b) & d);
            word_index = i;
        }
        else if (i < 32)
        {
            function_value = (d & b) | ((~d) & c);
            word_index = (5u * i + 1u) & 15u;
        }
        else if (i < 48)
        {
            function_value = b ^ c ^ d;
            word_index = (3u * i + 5u) & 15u;
        }
        else
        {
            function_value = c ^ (b | (~d));
            word_index = (7u * i) & 15u;
        }
        d = c;
        c = b;
        b += vm_md5_rotate_left(a + function_value + constants[i] +
                                    words[word_index],
                                shifts[i]);
        a = old_d;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
}

static void vm_md5_init(vm_md5_context *context)
{
    memset(context, 0, sizeof(*context));
    context->state[0] = 0x67452301u;
    context->state[1] = 0xefcdab89u;
    context->state[2] = 0x98badcfeu;
    context->state[3] = 0x10325476u;
}

static void vm_md5_update(vm_md5_context *context, const void *data_value,
                          size_t length)
{
    const uint8_t *data = (const uint8_t *)data_value;

    context->byte_count += length;
    while (length != 0)
    {
        size_t available = sizeof(context->block) - context->block_length;
        size_t copied = length < available ? length : available;

        memcpy(context->block + context->block_length, data, copied);
        context->block_length += copied;
        data += copied;
        length -= copied;
        if (context->block_length == sizeof(context->block))
        {
            vm_md5_transform(context, context->block);
            context->block_length = 0;
        }
    }
}

static void vm_md5_final(vm_md5_context *context, uint8_t digest[16])
{
    uint64_t bit_count = context->byte_count * 8u;
    uint8_t padding[64] = {0x80};
    uint8_t length_bytes[8];
    size_t padding_length = context->block_length < 56 ?
                                56 - context->block_length :
                                120 - context->block_length;

    for (unsigned int i = 0; i < 8; ++i)
        length_bytes[i] = (uint8_t)(bit_count >> (i * 8));
    vm_md5_update(context, padding, padding_length);
    vm_md5_update(context, length_bytes, sizeof(length_bytes));

    for (unsigned int i = 0; i < 4; ++i)
    {
        digest[i * 4] = (uint8_t)(context->state[i] & 0xffu);
        digest[i * 4 + 1] = (uint8_t)((context->state[i] >> 8) & 0xffu);
        digest[i * 4 + 2] = (uint8_t)((context->state[i] >> 16) & 0xffu);
        digest[i * 4 + 3] = (uint8_t)((context->state[i] >> 24) & 0xffu);
    }
    memset(context, 0, sizeof(*context));
}

void vm_md5_hex(const void *data, size_t length, char output[33])
{
    static const char hexadecimal[] = "0123456789abcdef";
    vm_md5_context context;
    uint8_t digest[16];

    vm_md5_init(&context);
    vm_md5_update(&context, data, length);
    vm_md5_final(&context, digest);
    for (unsigned int i = 0; i < sizeof(digest); ++i)
    {
        output[i * 2] = hexadecimal[digest[i] >> 4];
        output[i * 2 + 1] = hexadecimal[digest[i] & 0x0fu];
    }
    output[32] = 0;
}
