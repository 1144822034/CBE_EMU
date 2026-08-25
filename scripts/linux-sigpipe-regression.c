/*
 * Linux-only regression for the service-wide SIGPIPE policy and the actual
 * CBMS/HTTP send helper.  It does not start the service or connect to MySQL.
 *
 * Build after `make` on the Linux target:
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 \
 *     -ffunction-sections -fdata-sections -w scripts/linux-sigpipe-regression.c \
 *     obj/linux-server/gifDecode.o obj/linux-server/mystd.o \
 *     obj/linux-server/mysql-client.o obj/linux-server/md5.o \
 *     -Wl,--gc-sections -o tmp/linux-sigpipe-regression -lpthread -lm
 *
 * Run:
 *   ./tmp/linux-sigpipe-regression
 */

#ifndef __linux__
#include <stdio.h>

int main(void)
{
    puts("linux SIGPIPE regression skipped: requires Linux");
    return 0;
}
#else

#include <stdio.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    int sockets[2] = {-1, -1};
    struct sigaction action;
    u8 byte = 0;

    vm_server_install_crash_reporter();
    if (sigaction(SIGPIPE, NULL, &action) != 0 ||
        action.sa_handler != SIG_IGN)
    {
        fputs("SIGPIPE is not ignored by the service\n", stderr);
        return 1;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
    {
        perror("socketpair");
        return 1;
    }
    close(sockets[1]);
    sockets[1] = -1;
    if (vm_mock_service_send_all(sockets[0], &byte, sizeof(byte)) != 0)
    {
        fputs("send to closed peer unexpectedly succeeded\n", stderr);
        close(sockets[0]);
        return 1;
    }
    close(sockets[0]);
    puts("linux SIGPIPE regression passed: closed peer returned send failure without process termination");
    return 0;
}
#endif
