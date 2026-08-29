/*
 * Regression for listener-owned incomplete CBMS frames.
 *
 * The test sends a CBMS PING header in two TCP writes. After the first write,
 * ingress must consume those bytes so select() has no stale readability to
 * spin on; after the second write, the normal empty CBMR must still arrive.
 * It opens only an isolated loopback connection: no listener, MySQL, or game
 * account is started or mutated.
 */

#include <stdio.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int ingress_test_send_all(vm_mock_service_socket socket,
                                 const u8 *data, u32 length)
{
    u32 sent = 0;

    while (sent < length)
    {
        int rc = send(socket, (const char *)data + sent, (int)(length - sent), 0);
        if (rc <= 0)
            return 0;
        sent += (u32)rc;
    }
    return 1;
}

static int ingress_test_socket_is_readable(vm_mock_service_socket socket)
{
    fd_set readSet;
    struct timeval timeout;
    int rc;

    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
#ifdef _WIN32
    rc = select(0, &readSet, NULL, NULL, &timeout);
#else
    rc = select(socket + 1, &readSet, NULL, NULL, &timeout);
#endif
    return rc > 0 && FD_ISSET(socket, &readSet);
}

static int ingress_test_open_loopback_pair(vm_mock_service_socket *serverOut,
                                            vm_mock_service_socket *clientOut)
{
    vm_mock_service_socket listener = VM_MOCK_SERVICE_INVALID_SOCKET;
    vm_mock_service_socket server = VM_MOCK_SERVICE_INVALID_SOCKET;
    vm_mock_service_socket client = VM_MOCK_SERVICE_INVALID_SOCKET;
    struct sockaddr_in address;
    int reuse = 1;
#ifdef _WIN32
    int addressLen = sizeof(address);
#else
    socklen_t addressLen = sizeof(address);
#endif

    if (serverOut == NULL || clientOut == NULL)
        return 0;
    *serverOut = VM_MOCK_SERVICE_INVALID_SOCKET;
    *clientOut = VM_MOCK_SERVICE_INVALID_SOCKET;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == VM_MOCK_SERVICE_INVALID_SOCKET ||
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
                   sizeof(reuse)) != 0 ||
        bind(listener, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &addressLen) != 0)
    {
        vm_mock_service_socket_close(listener);
        return 0;
    }
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == VM_MOCK_SERVICE_INVALID_SOCKET ||
        connect(client, (const struct sockaddr *)&address, sizeof(address)) != 0)
    {
        vm_mock_service_socket_close(client);
        vm_mock_service_socket_close(listener);
        return 0;
    }
    server = accept(listener, NULL, NULL);
    vm_mock_service_socket_close(listener);
    if (server == VM_MOCK_SERVICE_INVALID_SOCKET)
    {
        vm_mock_service_socket_close(client);
        return 0;
    }
    *serverOut = server;
    *clientOut = client;
    return 1;
}

int main(void)
{
    vm_mock_service_ingress_table ingress;
    vm_mock_service_ingress_drop_log_table dropLogs;
    vm_mock_service_ingress_connection *entry = NULL;
    vm_mock_service_socket server = VM_MOCK_SERVICE_INVALID_SOCKET;
    vm_mock_service_socket client = VM_MOCK_SERVICE_INVALID_SOCKET;
    u8 requestHeader[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 requestBody[4] = {0, 0, 0, 0};
    u8 responseHeader[VM_MOCK_SERVICE_FRAME_SIZE];
    int result = 1;

    memset(&ingress, 0, sizeof(ingress));
    memset(&dropLogs, 0, sizeof(dropLogs));
    memset(requestHeader, 0, sizeof(requestHeader));
    memset(responseHeader, 0, sizeof(responseHeader));
    if (!vm_mock_service_socket_init() ||
        !ingress_test_open_loopback_pair(&server, &client))
    {
        fputs("unable to create isolated loopback connection\n", stderr);
        goto cleanup;
    }
    vm_mock_service_ingress_init(&ingress);
    if (!vm_mock_service_ingress_add(&ingress, server, "127.0.0.1", 0))
    {
        fputs("unable to add isolated ingress connection\n", stderr);
        goto cleanup;
    }
    server = VM_MOCK_SERVICE_INVALID_SOCKET;
    entry = &ingress.entries[0];
    vm_mock_service_encode_header(requestHeader, "CBMS",
                                  VM_MOCK_SERVICE_REQUEST_FLAG_PING, 0, 0);
    if (!ingress_test_send_all(client, requestHeader, 8) ||
        !vm_mock_service_ingress_try_dispatch(&ingress, entry, NULL, &dropLogs,
                                              1, true) ||
        entry->frameLen != 8 ||
        ingress_test_socket_is_readable(entry->socket))
    {
        fputs("partial header was not consumed before listener wait\n", stderr);
        goto cleanup;
    }
    if (!ingress_test_send_all(client, requestHeader + 8,
                               sizeof(requestHeader) - 8) ||
        vm_mock_service_ingress_try_dispatch(&ingress, entry, NULL, &dropLogs,
                                             2, true) ||
        entry->socket != VM_MOCK_SERVICE_INVALID_SOCKET ||
        !vm_mock_service_recv_all(client, responseHeader, sizeof(responseHeader)) ||
        memcmp(responseHeader, "CBMR", 4) != 0 ||
        vm_mock_service_read_le32(responseHeader + 4) != 1 ||
        vm_mock_service_read_le32(responseHeader + 8) != 0 ||
        vm_mock_service_read_le32(responseHeader + 12) != 0 ||
        vm_mock_service_read_le32(responseHeader + 16) != 0)
    {
        fputs("completed fragmented PING did not preserve its CBMR contract\n", stderr);
        goto cleanup;
    }
    vm_mock_service_socket_close(client);
    client = VM_MOCK_SERVICE_INVALID_SOCKET;

    if (!ingress_test_open_loopback_pair(&server, &client) ||
        !vm_mock_service_ingress_add(&ingress, server, "127.0.0.1", 0))
    {
        fputs("unable to create isolated partial-body ingress connection\n", stderr);
        goto cleanup;
    }
    server = VM_MOCK_SERVICE_INVALID_SOCKET;
    entry = &ingress.entries[0];
    vm_mock_service_encode_header(requestHeader, "CBMS", 0, sizeof(requestBody), 0);
    if (!ingress_test_send_all(client, requestHeader, sizeof(requestHeader)) ||
        !ingress_test_send_all(client, requestBody, 2) ||
        !vm_mock_service_ingress_try_dispatch(&ingress, entry, NULL, &dropLogs,
                                              3, true) ||
        !ingress_test_socket_is_readable(entry->socket) ||
        !vm_mock_service_ingress_try_dispatch(&ingress, entry, NULL, &dropLogs,
                                              4, true) ||
        entry->frameLen != VM_MOCK_SERVICE_FRAME_SIZE + 2 ||
        ingress_test_socket_is_readable(entry->socket))
    {
        fputs("partial body was not consumed before listener wait\n", stderr);
        goto cleanup;
    }
    vm_mock_service_ingress_clear(entry);
    vm_mock_service_socket_close(client);
    client = VM_MOCK_SERVICE_INVALID_SOCKET;

    vm_mock_service_encode_header(requestHeader, "CBMS",
                                  VM_MOCK_SERVICE_REQUEST_FLAG_PING, 0, 0);
    if (!ingress_test_open_loopback_pair(&server, &client) ||
        !vm_net_mock_service_handle_client(server, requestHeader,
                                           sizeof(requestHeader), NULL, 0,
                                           NULL, 0, 0, 0) ||
        !vm_mock_service_recv_all(client, responseHeader, sizeof(responseHeader)) ||
        memcmp(responseHeader, "CBMR", 4) != 0)
    {
        fputs("worker prebuffer did not preserve the CBMS PING response\n", stderr);
        goto cleanup;
    }
    vm_mock_service_socket_close(server);
    server = VM_MOCK_SERVICE_INVALID_SOCKET;
    result = 0;

cleanup:
    for (u32 i = 0; i < VM_MOCK_SERVICE_INGRESS_MAX; ++i)
        vm_mock_service_ingress_clear(&ingress.entries[i]);
    vm_mock_service_socket_close(server);
    vm_mock_service_socket_close(client);
    if (result == 0)
        puts("ingress partial-frame regression passed: partial header/body bytes were drained before select and worker prebuffering retained the CBMR response");
    return result;
}
