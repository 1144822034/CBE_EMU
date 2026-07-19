#include "config.h"

#ifdef CBE_SERVER_ONLY
#include "../Lib/unicorn-2.1.4/unicorn/unicorn.h"
#include "cbeParser.h"

#include <string.h>

/* Shared server builders inspect these fields only as optional emulator
 * hints.  Keep the zero-initialized descriptor locally instead of compiling
 * the client CBE parser into the Linux service. */
cbeInfo g_cbeInfo;

/*
 * A few shared packet builders contain optional probes into the local
 * emulator's guest memory.  The standalone Linux service has no guest at all;
 * report that explicitly so those builders use their authoritative session /
 * database fallbacks without pulling the Unicorn runtime into the server.
 */
uc_err uc_mem_read(uc_engine *uc, uint64_t address, void *bytes, uint64_t size)
{
    (void)uc;
    (void)address;
    if (bytes != NULL && size != 0)
        memset(bytes, 0, (size_t)size);
    return UC_ERR_HANDLE;
}
#endif
