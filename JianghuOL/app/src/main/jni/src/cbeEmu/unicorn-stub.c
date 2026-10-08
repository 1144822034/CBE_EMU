/*
 * Minimal Unicorn engine stub implementation for WebAssembly.
 * All functions are no-ops that return success or zero values.
 */

#include "unicorn-stub.h"
#include <string.h>
#include <stdlib.h>

static uc_engine g_fake_uc;
static uc_hook g_fake_hooks[32];
static int g_hook_count = 0;

uc_err uc_open(uc_arch arch, uc_mode mode, uc_engine **handle) {
    (void)arch;
    (void)mode;
    if (handle) *handle = &g_fake_uc;
    return UC_ERR_OK;
}

void uc_close(uc_engine *uc) {
    (void)uc;
}

uc_err uc_errno(uc_engine *uc) {
    (void)uc;
    return UC_ERR_OK;
}

const char *uc_strerror(uc_err err) {
    switch (err) {
        case UC_ERR_OK: return "OK";
        case UC_ERR_NOMEM: return "OUT_OF_MEMORY";
        case UC_ERR_ARCH: return "INVALID_ARCH";
        case UC_ERR_HANDLE: return "INVALID_HANDLE";
        case UC_ERR_MODE: return "INVALID_MODE";
        case UC_ERR_QUERY: return "INVALID_QUERY";
        case UC_ERR_HOOK: return "INVALID_HOOK";
        case UC_ERR_INSN_INVALID: return "INVALID_INSN";
        case UC_ERR_MAP: return "MEM_MAP";
        case UC_ERR_READ: return "MEM_READ";
        case UC_ERR_WRITE: return "MEM_WRITE";
        case UC_ERR_FETCH: return "MEM_FETCH";
        case UC_ERR_FAULT: return "MEM_FAULT";
        case UC_ERR_BP: return "BP";
        case UC_ERR_IO: return "IO";
        case UC_ERR_VERSION: return "VERSION_MISMATCH";
        case UC_ERR_UNSUPPORTED: return "UNSUPPORTED";
        default: return "UNKNOWN_ERROR";
    }
}

uc_err uc_emu_start(uc_engine *uc, uint64_t begin, uint64_t until, uint64_t timeout, uint64_t count) {
    (void)uc; (void)begin; (void)until; (void)timeout; (void)count;
    return UC_ERR_OK;
}

void uc_emu_stop(uc_engine *uc) {
    (void)uc;
}

uc_err uc_hook_add(uc_engine *uc, uc_hook *hook, int type, void *cb,
                    void *user_data, uint64_t begin, uint64_t end, ...) {
    (void)uc; (void)type; (void)user_data; (void)begin; (void)end;
    if (hook && g_hook_count < 32) {
        g_fake_hooks[g_hook_count].hook_type = (uint16_t)type;
        *hook = &g_fake_hooks[g_hook_count++];
    }
    return UC_ERR_OK;
}

uc_err uc_mem_read(uc_engine *uc, uint64_t address, void *buffer, size_t size) {
    (void)uc; (void)address; (void)buffer; (void)size;
    memset(buffer, 0, size);
    return UC_ERR_OK;
}

uc_err uc_mem_write(uc_engine *uc, uint64_t address, const void *buffer, size_t size) {
    (void)uc; (void)address; (void)buffer; (void)size;
    return UC_ERR_OK;
}

uc_err uc_reg_read(uc_engine *uc, int reg, void *value) {
    (void)uc; (void)reg;
    memset(value, 0, sizeof(uint32_t));
    return UC_ERR_OK;
}

uc_err uc_reg_write(uc_engine *uc, int reg, const void *value) {
    (void)uc; (void)reg; (void)value;
    return UC_ERR_OK;
}

uc_err uc_mem_map(uc_engine *uc, uint64_t address, size_t size, int perms) {
    (void)uc; (void)address; (void)size; (void)perms;
    return UC_ERR_OK;
}

uc_err uc_mem_map_ptr(uc_engine *uc, uint64_t address, size_t size, int perms, void *host_ptr) {
    (void)uc; (void)address; (void)size; (void)perms; (void)host_ptr;
    return UC_ERR_OK;
}
