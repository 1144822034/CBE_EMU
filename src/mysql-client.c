#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

#include "mysql-client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <pthread.h>
#endif

#define VM_MYSQL_CLIENT_LONG_PASSWORD       0x00000001u
#define VM_MYSQL_CLIENT_LONG_FLAG           0x00000004u
#define VM_MYSQL_CLIENT_CONNECT_WITH_DB     0x00000008u
#define VM_MYSQL_CLIENT_PROTOCOL_41         0x00000200u
#define VM_MYSQL_CLIENT_TRANSACTIONS        0x00002000u
#define VM_MYSQL_CLIENT_SECURE_CONNECTION   0x00008000u
#define VM_MYSQL_CLIENT_MULTI_RESULTS       0x00020000u
#define VM_MYSQL_CLIENT_PLUGIN_AUTH         0x00080000u

#define VM_MYSQL_PACKET_CAPACITY (1024u * 1024u)

typedef struct
{
    uint32_t state[5];
    uint64_t byte_count;
    uint8_t block[64];
    size_t block_len;
} vm_mysql_sha1_context;

#if defined(_MSC_VER)
#define VM_MYSQL_THREAD_LOCAL __declspec(thread)
#else
#define VM_MYSQL_THREAD_LOCAL __thread
#endif

/* A MySQL protocol stream cannot be shared by concurrent requests: packet
 * sequence numbers and result rows would interleave.  Each long-lived service
 * worker therefore owns and reuses its own connection and error buffer. */
static VM_MYSQL_THREAD_LOCAL SOCKET g_vm_mysql_socket = INVALID_SOCKET;
static VM_MYSQL_THREAD_LOCAL char g_vm_mysql_error[512];
#ifdef _WIN32
static pthread_once_t g_vm_mysql_wsa_once = PTHREAD_ONCE_INIT;
static int g_vm_mysql_wsa_ready = 0;

static void vm_mysql_wsa_initialize_once(void)
{
    WSADATA wsa_data;
    g_vm_mysql_wsa_ready =
        WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0 ? 1 : -1;
}
#endif

static uint32_t vm_mysql_rotl32(uint32_t value, unsigned int bits)
{
    return (value << bits) | (value >> (32u - bits));
}

static void vm_mysql_sha1_transform(vm_mysql_sha1_context *context, const uint8_t block[64])
{
    uint32_t words[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;

    for (unsigned int i = 0; i < 16; ++i)
    {
        words[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) |
                   (uint32_t)block[i * 4 + 3];
    }
    for (unsigned int i = 16; i < 80; ++i)
        words[i] = vm_mysql_rotl32(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];

    for (unsigned int i = 0; i < 80; ++i)
    {
        uint32_t f;
        uint32_t k;
        if (i < 20)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        uint32_t temp = vm_mysql_rotl32(a, 5) + f + e + k + words[i];
        e = d;
        d = c;
        c = vm_mysql_rotl32(b, 30);
        b = a;
        a = temp;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
}

static void vm_mysql_sha1_init(vm_mysql_sha1_context *context)
{
    memset(context, 0, sizeof(*context));
    context->state[0] = 0x67452301u;
    context->state[1] = 0xefcdab89u;
    context->state[2] = 0x98badcfeu;
    context->state[3] = 0x10325476u;
    context->state[4] = 0xc3d2e1f0u;
}

static void vm_mysql_sha1_update(vm_mysql_sha1_context *context, const void *data_value, size_t data_len)
{
    const uint8_t *data = (const uint8_t *)data_value;
    context->byte_count += data_len;
    while (data_len > 0)
    {
        size_t copy_len = 64 - context->block_len;
        if (copy_len > data_len)
            copy_len = data_len;
        memcpy(context->block + context->block_len, data, copy_len);
        context->block_len += copy_len;
        data += copy_len;
        data_len -= copy_len;
        if (context->block_len == 64)
        {
            vm_mysql_sha1_transform(context, context->block);
            context->block_len = 0;
        }
    }
}

static void vm_mysql_sha1_final(vm_mysql_sha1_context *context, uint8_t digest[20])
{
    uint64_t bit_count = context->byte_count * 8u;
    uint8_t one = 0x80;
    uint8_t zero = 0;
    vm_mysql_sha1_update(context, &one, 1);
    while (context->block_len != 56)
        vm_mysql_sha1_update(context, &zero, 1);
    uint8_t length_bytes[8];
    for (unsigned int i = 0; i < 8; ++i)
        length_bytes[7 - i] = (uint8_t)(bit_count >> (i * 8));
    vm_mysql_sha1_update(context, length_bytes, sizeof(length_bytes));
    for (unsigned int i = 0; i < 5; ++i)
    {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
}

static void vm_mysql_sha1(const void *data, size_t data_len, uint8_t digest[20])
{
    vm_mysql_sha1_context context;
    vm_mysql_sha1_init(&context);
    vm_mysql_sha1_update(&context, data, data_len);
    vm_mysql_sha1_final(&context, digest);
}

static void vm_mysql_set_error(const char *message)
{
    snprintf(g_vm_mysql_error, sizeof(g_vm_mysql_error), "%s", message ? message : "unknown MySQL error");
}

static uint16_t vm_mysql_read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t vm_mysql_read_u24(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
}

static void vm_mysql_write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static const char *vm_mysql_config(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static bool vm_mysql_send_all(const uint8_t *data, size_t data_len)
{
    while (data_len > 0)
    {
        int chunk = data_len > 0x7fffffffU ? 0x7fffffff : (int)data_len;
        int sent = send(g_vm_mysql_socket, (const char *)data, chunk, 0);
        if (sent == SOCKET_ERROR || sent == 0)
        {
            vm_mysql_set_error("MySQL socket send failed");
            return false;
        }
        data += sent;
        data_len -= (size_t)sent;
    }
    return true;
}

static bool vm_mysql_recv_all(uint8_t *data, size_t data_len)
{
    while (data_len > 0)
    {
        int chunk = data_len > 0x7fffffffU ? 0x7fffffff : (int)data_len;
        int received = recv(g_vm_mysql_socket, (char *)data, chunk, 0);
        if (received == SOCKET_ERROR || received == 0)
        {
            vm_mysql_set_error("MySQL socket receive failed");
            return false;
        }
        data += received;
        data_len -= (size_t)received;
    }
    return true;
}

static bool vm_mysql_send_packet(const uint8_t *payload, size_t payload_len, uint8_t sequence)
{
    if (payload_len >= 0x1000000u)
    {
        vm_mysql_set_error("MySQL packet is too large");
        return false;
    }
    uint8_t header[4];
    header[0] = (uint8_t)payload_len;
    header[1] = (uint8_t)(payload_len >> 8);
    header[2] = (uint8_t)(payload_len >> 16);
    header[3] = sequence;
    return vm_mysql_send_all(header, sizeof(header)) && vm_mysql_send_all(payload, payload_len);
}

static bool vm_mysql_recv_packet(uint8_t *payload, size_t payload_capacity, size_t *payload_len, uint8_t *sequence)
{
    uint8_t header[4];
    if (!vm_mysql_recv_all(header, sizeof(header)))
        return false;
    size_t length = vm_mysql_read_u24(header);
    if (length > payload_capacity)
    {
        vm_mysql_set_error("MySQL response packet exceeds local buffer");
        return false;
    }
    if (!vm_mysql_recv_all(payload, length))
        return false;
    *payload_len = length;
    if (sequence != NULL)
        *sequence = header[3];
    return true;
}

static void vm_mysql_scramble_password(const char *password, const uint8_t salt[20], uint8_t output[20])
{
    uint8_t stage1[20];
    uint8_t stage2[20];
    uint8_t combined[40];
    uint8_t stage3[20];
    vm_mysql_sha1(password, strlen(password), stage1);
    vm_mysql_sha1(stage1, sizeof(stage1), stage2);
    memcpy(combined, salt, 20);
    memcpy(combined + 20, stage2, 20);
    vm_mysql_sha1(combined, sizeof(combined), stage3);
    for (unsigned int i = 0; i < 20; ++i)
        output[i] = stage1[i] ^ stage3[i];
}

static bool vm_mysql_is_error(const uint8_t *packet, size_t packet_len)
{
    if (packet_len == 0 || packet[0] != 0xff)
        return false;
    unsigned int error_code = packet_len >= 3 ? vm_mysql_read_u16(packet + 1) : 0;
    size_t message_offset = packet_len >= 9 && packet[3] == '#' ? 9 : 3;
    snprintf(g_vm_mysql_error,
             sizeof(g_vm_mysql_error),
             "MySQL error %u: %.*s",
             error_code,
             (int)(packet_len > message_offset ? packet_len - message_offset : 0),
             packet_len > message_offset ? (const char *)(packet + message_offset) : "");
    return true;
}

static bool vm_mysql_connect(void)
{
    if (g_vm_mysql_socket != INVALID_SOCKET)
        return true;

#ifdef _WIN32
    if (pthread_once(&g_vm_mysql_wsa_once, vm_mysql_wsa_initialize_once) != 0 ||
        g_vm_mysql_wsa_ready != 1)
    {
        vm_mysql_set_error("WSAStartup failed for MySQL connection");
        return false;
    }
#endif

    const char *host = vm_mysql_config("CBE_MYSQL_HOST", "127.0.0.1");
    const char *port = vm_mysql_config("CBE_MYSQL_PORT", "3306");
    const char *user = vm_mysql_config("CBE_MYSQL_USER", "root");
    const char *password = vm_mysql_config("CBE_MYSQL_PASSWORD", "123456");
    const char *database = vm_mysql_config("CBE_MYSQL_DATABASE", "jh_online");

    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(host, port, &hints, &addresses) != 0)
    {
        vm_mysql_set_error("Could not resolve MySQL host");
        return false;
    }
    for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next)
    {
        SOCKET socket_value = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_value == INVALID_SOCKET)
            continue;
        if (connect(socket_value, address->ai_addr, (int)address->ai_addrlen) == 0)
        {
            g_vm_mysql_socket = socket_value;
            break;
        }
        closesocket(socket_value);
    }
    freeaddrinfo(addresses);
    if (g_vm_mysql_socket == INVALID_SOCKET)
    {
        vm_mysql_set_error("Could not connect to MySQL server");
        return false;
    }

    /* Queries are encoded as a four-byte MySQL packet header followed by a
     * small payload. With Nagle enabled, the second send may wait for the
     * header ACK. MySQL already frames every packet, so disabling Nagle avoids
     * adding that latency to the scene bootstrap's small request/response
     * queries. */
    {
        int noDelay = 1;
#ifdef _WIN32
        setsockopt(g_vm_mysql_socket, IPPROTO_TCP, TCP_NODELAY,
                   (const char *)&noDelay, sizeof(noDelay));
#else
        setsockopt(g_vm_mysql_socket, IPPROTO_TCP, TCP_NODELAY,
                   &noDelay, sizeof(noDelay));
#endif
    }

#ifdef _WIN32
    DWORD timeout_ms = 5000;
    setsockopt(g_vm_mysql_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    setsockopt(g_vm_mysql_socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval timeout_value = {5, 0};
    setsockopt(g_vm_mysql_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout_value, sizeof(timeout_value));
    setsockopt(g_vm_mysql_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout_value, sizeof(timeout_value));
#endif

    uint8_t packet[4096];
    size_t packet_len = 0;
    uint8_t sequence = 0;
    if (!vm_mysql_recv_packet(packet, sizeof(packet), &packet_len, &sequence) || packet_len < 34 || vm_mysql_is_error(packet, packet_len))
        goto failed;

    size_t pos = 1;
    while (pos < packet_len && packet[pos] != 0)
        ++pos;
    if (pos + 1 + 4 + 8 + 1 + 2 > packet_len)
    {
        vm_mysql_set_error("Malformed MySQL handshake");
        goto failed;
    }
    ++pos;
    pos += 4;
    uint8_t salt[20];
    memcpy(salt, packet + pos, 8);
    pos += 8 + 1;
    uint32_t server_caps = vm_mysql_read_u16(packet + pos);
    pos += 2;
    if (pos >= packet_len)
    {
        vm_mysql_set_error("Old MySQL handshake is not supported");
        goto failed;
    }
    pos += 1 + 2;
    if (pos + 2 + 1 + 10 > packet_len)
    {
        vm_mysql_set_error("Malformed MySQL capability block");
        goto failed;
    }
    server_caps |= (uint32_t)vm_mysql_read_u16(packet + pos) << 16;
    pos += 2;
    uint8_t auth_data_len = packet[pos++];
    pos += 10;
    memset(salt + 8, 0, 12);
    size_t salt_tail = auth_data_len > 8 ? (size_t)auth_data_len - 8 : 12;
    if (salt_tail > 12)
        salt_tail = 12;
    if (salt_tail > packet_len - pos)
        salt_tail = packet_len - pos;
    memcpy(salt + 8, packet + pos, salt_tail);

    uint32_t requested_caps = VM_MYSQL_CLIENT_LONG_PASSWORD |
                              VM_MYSQL_CLIENT_LONG_FLAG |
                              VM_MYSQL_CLIENT_CONNECT_WITH_DB |
                              VM_MYSQL_CLIENT_PROTOCOL_41 |
                              VM_MYSQL_CLIENT_TRANSACTIONS |
                              VM_MYSQL_CLIENT_SECURE_CONNECTION |
                              VM_MYSQL_CLIENT_MULTI_RESULTS |
                              VM_MYSQL_CLIENT_PLUGIN_AUTH;
    uint32_t client_caps = requested_caps & server_caps;
    if ((client_caps & (VM_MYSQL_CLIENT_PROTOCOL_41 | VM_MYSQL_CLIENT_SECURE_CONNECTION | VM_MYSQL_CLIENT_CONNECT_WITH_DB)) !=
        (VM_MYSQL_CLIENT_PROTOCOL_41 | VM_MYSQL_CLIENT_SECURE_CONNECTION | VM_MYSQL_CLIENT_CONNECT_WITH_DB))
    {
        vm_mysql_set_error("MySQL server lacks required protocol capabilities");
        goto failed;
    }

    uint8_t response[1024];
    pos = 0;
    vm_mysql_write_u32(response + pos, client_caps);
    pos += 4;
    vm_mysql_write_u32(response + pos, VM_MYSQL_PACKET_CAPACITY);
    pos += 4;
    response[pos++] = 33;
    memset(response + pos, 0, 23);
    pos += 23;
    size_t user_len = strlen(user);
    size_t database_len = strlen(database);
    if (pos + user_len + database_len + 64 >= sizeof(response))
    {
        vm_mysql_set_error("MySQL connection settings are too long");
        goto failed;
    }
    memcpy(response + pos, user, user_len + 1);
    pos += user_len + 1;
    if (password[0] == '\0')
        response[pos++] = 0;
    else
    {
        uint8_t scramble[20];
        vm_mysql_scramble_password(password, salt, scramble);
        response[pos++] = 20;
        memcpy(response + pos, scramble, sizeof(scramble));
        pos += sizeof(scramble);
    }
    memcpy(response + pos, database, database_len + 1);
    pos += database_len + 1;
    if (client_caps & VM_MYSQL_CLIENT_PLUGIN_AUTH)
    {
        static const char plugin[] = "mysql_native_password";
        memcpy(response + pos, plugin, sizeof(plugin));
        pos += sizeof(plugin);
    }
    if (!vm_mysql_send_packet(response, pos, 1) ||
        !vm_mysql_recv_packet(packet, sizeof(packet), &packet_len, &sequence))
        goto failed;

    if (packet_len > 1 && packet[0] == 0xfe)
    {
        size_t plugin_end = 1;
        while (plugin_end < packet_len && packet[plugin_end] != 0)
            ++plugin_end;
        if (plugin_end == packet_len || strcmp((const char *)(packet + 1), "mysql_native_password") != 0)
        {
            vm_mysql_set_error("Unsupported MySQL authentication plugin");
            goto failed;
        }
        uint8_t switch_salt[20];
        memset(switch_salt, 0, sizeof(switch_salt));
        size_t switch_len = packet_len - plugin_end - 1;
        if (switch_len > sizeof(switch_salt))
            switch_len = sizeof(switch_salt);
        memcpy(switch_salt, packet + plugin_end + 1, switch_len);
        uint8_t scramble[20];
        vm_mysql_scramble_password(password, switch_salt, scramble);
        if (!vm_mysql_send_packet(scramble, password[0] == '\0' ? 0 : sizeof(scramble), (uint8_t)(sequence + 1)) ||
            !vm_mysql_recv_packet(packet, sizeof(packet), &packet_len, &sequence))
            goto failed;
    }
    if (vm_mysql_is_error(packet, packet_len))
        goto failed;
    if (packet_len == 0 || packet[0] != 0x00)
    {
        vm_mysql_set_error("Unexpected MySQL authentication response");
        goto failed;
    }
    g_vm_mysql_error[0] = '\0';
    return true;

failed:
    closesocket(g_vm_mysql_socket);
    g_vm_mysql_socket = INVALID_SOCKET;
    return false;
}

static bool vm_mysql_read_lenenc(const uint8_t *data,
                                 size_t data_len,
                                 size_t *position,
                                 uint64_t *value,
                                 bool *is_null)
{
    if (*position >= data_len)
        return false;
    uint8_t marker = data[(*position)++];
    *is_null = false;
    if (marker < 0xfb)
    {
        *value = marker;
        return true;
    }
    if (marker == 0xfb)
    {
        *is_null = true;
        *value = 0;
        return true;
    }
    unsigned int width = marker == 0xfc ? 2 : marker == 0xfd ? 3 : marker == 0xfe ? 8 : 0;
    if (width == 0 || *position + width > data_len)
        return false;
    uint64_t result = 0;
    for (unsigned int i = 0; i < width; ++i)
        result |= (uint64_t)data[*position + i] << (i * 8);
    *position += width;
    *value = result;
    return true;
}

static bool vm_mysql_packet_is_eof(const uint8_t *packet, size_t packet_len)
{
    return packet_len > 0 && packet[0] == 0xfe && packet_len < 9;
}

static bool vm_mysql_run_query(const char *sql, vm_mysql_row_callback callback, void *context)
{
    if (sql == NULL || !vm_mysql_connect())
        return false;
    size_t sql_len = strlen(sql);
    uint8_t *request = (uint8_t *)malloc(sql_len + 1);
    uint8_t *packet = (uint8_t *)malloc(VM_MYSQL_PACKET_CAPACITY);
    if (request == NULL || packet == NULL)
    {
        free(request);
        free(packet);
        vm_mysql_set_error("Out of memory while executing MySQL query");
        return false;
    }
    request[0] = 0x03;
    memcpy(request + 1, sql, sql_len);
    if (!vm_mysql_send_packet(request, sql_len + 1, 0))
        goto connection_failed;
    free(request);
    request = NULL;

    size_t packet_len = 0;
    uint8_t sequence = 0;
    if (!vm_mysql_recv_packet(packet, VM_MYSQL_PACKET_CAPACITY, &packet_len, &sequence))
        goto connection_failed;
    if (vm_mysql_is_error(packet, packet_len))
        goto query_failed;
    if (packet_len > 0 && packet[0] == 0x00)
    {
        free(packet);
        g_vm_mysql_error[0] = '\0';
        return true;
    }

    size_t pos = 0;
    uint64_t column_count64 = 0;
    bool is_null = false;
    if (!vm_mysql_read_lenenc(packet, packet_len, &pos, &column_count64, &is_null) || is_null || column_count64 > 256)
    {
        vm_mysql_set_error("Malformed MySQL result-set header");
        goto query_failed;
    }
    unsigned int column_count = (unsigned int)column_count64;
    for (unsigned int i = 0; i < column_count; ++i)
    {
        if (!vm_mysql_recv_packet(packet, VM_MYSQL_PACKET_CAPACITY, &packet_len, &sequence) || vm_mysql_is_error(packet, packet_len))
            goto connection_failed;
    }
    if (!vm_mysql_recv_packet(packet, VM_MYSQL_PACKET_CAPACITY, &packet_len, &sequence) ||
        vm_mysql_is_error(packet, packet_len) || !vm_mysql_packet_is_eof(packet, packet_len))
    {
        vm_mysql_set_error("Malformed MySQL result-set column terminator");
        goto connection_failed;
    }

    while (true)
    {
        if (!vm_mysql_recv_packet(packet, VM_MYSQL_PACKET_CAPACITY, &packet_len, &sequence))
            goto connection_failed;
        if (vm_mysql_is_error(packet, packet_len))
            goto query_failed;
        if (vm_mysql_packet_is_eof(packet, packet_len))
            break;

        const char *values[256];
        size_t lengths[256];
        pos = 0;
        for (unsigned int i = 0; i < column_count; ++i)
        {
            uint64_t value_len = 0;
            if (!vm_mysql_read_lenenc(packet, packet_len, &pos, &value_len, &is_null) ||
                (!is_null && (value_len > SIZE_MAX || pos + (size_t)value_len > packet_len)))
            {
                vm_mysql_set_error("Malformed MySQL result-set row");
                goto connection_failed;
            }
            values[i] = is_null ? NULL : (const char *)(packet + pos);
            lengths[i] = is_null ? 0 : (size_t)value_len;
            if (!is_null)
                pos += (size_t)value_len;
        }
        if (callback != NULL && !callback(context, column_count, values, lengths))
        {
            /* The result must still be drained before this persistent connection
             * can be reused, so continue reading while suppressing callbacks. */
            callback = NULL;
        }
    }
    free(packet);
    g_vm_mysql_error[0] = '\0';
    return true;

connection_failed:
    free(request);
    free(packet);
    vm_mysql_close();
    return false;
query_failed:
    free(request);
    free(packet);
    return false;
}

bool vm_mysql_exec(const char *sql)
{
    return vm_mysql_run_query(sql, NULL, NULL);
}

bool vm_mysql_query(const char *sql, vm_mysql_row_callback callback, void *context)
{
    return vm_mysql_run_query(sql, callback, context);
}

const char *vm_mysql_last_error(void)
{
    return g_vm_mysql_error[0] != '\0' ? g_vm_mysql_error : "no MySQL error detail";
}

void vm_mysql_close(void)
{
    if (g_vm_mysql_socket != INVALID_SOCKET)
    {
        closesocket(g_vm_mysql_socket);
        g_vm_mysql_socket = INVALID_SOCKET;
    }
}

size_t vm_mysql_hex_encode(const void *data_value, size_t data_len, char *output, size_t output_size)
{
    static const char digits[] = "0123456789ABCDEF";
    const uint8_t *data = (const uint8_t *)data_value;
    if (output == NULL || output_size < data_len * 2 + 1)
        return 0;
    for (size_t i = 0; i < data_len; ++i)
    {
        output[i * 2] = digits[data[i] >> 4];
        output[i * 2 + 1] = digits[data[i] & 15];
    }
    output[data_len * 2] = '\0';
    return data_len * 2;
}

static int vm_mysql_hex_digit(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool vm_mysql_hex_decode(const char *text, size_t text_len, void *output_value, size_t output_size, size_t *decoded_len)
{
    uint8_t *output = (uint8_t *)output_value;
    if (text == NULL || (text_len & 1u) != 0 || output == NULL || output_size < text_len / 2)
        return false;
    for (size_t i = 0; i < text_len / 2; ++i)
    {
        int high = vm_mysql_hex_digit(text[i * 2]);
        int low = vm_mysql_hex_digit(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    if (decoded_len != NULL)
        *decoded_len = text_len / 2;
    return true;
}
