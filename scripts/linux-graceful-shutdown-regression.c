/*
 * Linux-only regression for the service graceful-stop boundary.  It does not
 * start a listener, connect to MySQL, or mutate a game account.  A blocked
 * worker receives one already-admitted CBMS PING frame; SIGTERM is delivered
 * only after the listener mask is restored, then the pool must drain that
 * frame and return its normal CBMR response before joining the worker.
 *
 * Build and run on the Linux service host:
 *   make -j2 linux-graceful-shutdown-regression
 *   ./obj/linux-server/linux-graceful-shutdown-regression
 */

#ifndef __linux__
#include <stdio.h>

int main(void)
{
    puts("linux graceful shutdown regression skipped: requires Linux");
    return 0;
}
#else

#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int recv_all_exact(int socketFd, u8 *buffer, u32 length)
{
    u32 received = 0;

    while (received < length)
    {
        ssize_t rc = recv(socketFd, buffer + received, length - received, 0);
        if (rc <= 0)
            return 0;
        received += (u32)rc;
    }
    return 1;
}

int main(void)
{
    vm_mock_service_worker_pool pool;
    int sockets[2] = {-1, -1};
    u8 requestHeader[VM_MOCK_SERVICE_FRAME_SIZE];
    u8 responseHeader[VM_MOCK_SERVICE_FRAME_SIZE];
    struct sigaction action;
    int result = 1;
    bool poolStarted = false;
    bool workerSignalsBlocked = false;

    memset(&pool, 0, sizeof(pool));
    memset(requestHeader, 0, sizeof(requestHeader));
    memset(responseHeader, 0, sizeof(responseHeader));
    vm_server_install_crash_reporter();
    vm_server_install_shutdown_signal_handlers();
    if (sigaction(SIGINT, NULL, &action) != 0 ||
        action.sa_handler != vm_server_posix_shutdown_signal_handler ||
        sigaction(SIGTERM, NULL, &action) != 0 ||
        action.sa_handler != vm_server_posix_shutdown_signal_handler)
    {
        fputs("service SIGINT/SIGTERM handlers are not graceful-stop handlers\n", stderr);
        goto cleanup;
    }
    if (!vm_server_shutdown_block_for_workers())
    {
        fputs("unable to block shutdown signals before worker creation\n", stderr);
        goto cleanup;
    }
    workerSignalsBlocked = true;
    if (!vm_mock_service_worker_pool_start(&pool, 1, "linux-graceful-test"))
    {
        fputs("unable to start isolated worker pool\n", stderr);
        goto cleanup;
    }
    poolStarted = true;
    vm_server_shutdown_restore_listener_mask();
    workerSignalsBlocked = false;

    /* This would terminate the process if the worker did not inherit the
     * blocked mask.  It remains pending only in that worker until it exits. */
    if (pthread_kill(pool.workers[0].thread, SIGINT) != 0)
    {
        fputs("unable to target worker with SIGINT mask check\n", stderr);
        goto cleanup;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
    {
        perror("socketpair");
        goto cleanup;
    }
    vm_mock_service_encode_header(requestHeader, "CBMS",
                                  VM_MOCK_SERVICE_REQUEST_FLAG_PING, 0, 0);
    if (send(sockets[1], requestHeader, sizeof(requestHeader), 0) !=
        (ssize_t)sizeof(requestHeader) ||
        !vm_mock_service_worker_pool_enqueue(&pool, sockets[0],
                                             VM_MOCK_SERVICE_CONNECTION_GAME,
                                             NULL, 0, 0, 0, NULL))
    {
        fputs("unable to enqueue admitted CBMS PING frame\n", stderr);
        goto cleanup;
    }
    sockets[0] = -1; /* Worker pool owns this end after enqueue. */

    if (raise(SIGTERM) != 0 || !vm_server_shutdown_requested() ||
        vm_server_shutdown_signal_number() != SIGTERM)
    {
        fputs("SIGTERM did not become a listener-owned shutdown request\n", stderr);
        goto cleanup;
    }
    vm_mock_service_worker_pool_drain(&pool);
    poolStarted = false;
    if (!recv_all_exact(sockets[1], responseHeader, sizeof(responseHeader)) ||
        memcmp(responseHeader, "CBMR", 4) != 0 ||
        vm_mock_service_read_le32(responseHeader + 4) != 1 ||
        vm_mock_service_read_le32(responseHeader + 8) != 0 ||
        vm_mock_service_read_le32(responseHeader + 12) != 0 ||
        vm_mock_service_read_le32(responseHeader + 16) != 0)
    {
        fputs("admitted CBMS PING was not drained through its normal CBMR path\n", stderr);
        goto cleanup;
    }
    result = 0;

cleanup:
    if (workerSignalsBlocked)
        vm_server_shutdown_restore_listener_mask();
    if (poolStarted)
        vm_mock_service_worker_pool_drain(&pool);
    if (sockets[0] >= 0)
        close(sockets[0]);
    if (sockets[1] >= 0)
        close(sockets[1]);
    if (result == 0)
        puts("linux graceful shutdown regression passed: SIGTERM stayed in the listener, and the admitted CBMS PING drained to its normal CBMR response");
    return result;
}
#endif
