/*
 * Minimal Unicorn engine stub for WebAssembly builds.
 * Only defines symbols actually used by the client code.
 */

#ifndef CBE_UNICORN_STUB_H
#define CBE_UNICORN_STUB_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint32_t uc_err;
typedef void *uc_engine;
typedef size_t uc_hook;

/* Error codes */
enum {
    UC_ERR_OK = 0,
    UC_ERR_NOMEM = 1,
    UC_ERR_ARCH = 2,
    UC_ERR_HANDLE = 3,
    UC_ERR_MODE = 4,
    UC_ERR_QUERY = 5,
    UC_ERR_HOOK = 6,
    UC_ERR_INSN_INVALID = 7,
    UC_ERR_MAP = 8,
    UC_ERR_READ = 9,
    UC_ERR_WRITE = 10,
    UC_ERR_FETCH = 11,
    UC_ERR_FAULT = 12,
    UC_ERR_BP = 13,
    UC_ERR_IO = 14,
    UC_ERR_VERSION = 15,
    UC_ERR_UNSUPPORTED = 999,
    /* Alias for compatibility */
    UC_ERR_READ_UNMAPPED = 9,
    UC_ERR_WRITE_UNMAPPED = 10,
    UC_ERR_FETCH_UNMAPPED = 11
};

/* Architecture & Mode */
enum {
    UC_ARCH_ARM = 1,
    UC_MODE_LITTLE_ENDIAN = 0,
    UC_MODE_BIG_ENDIAN = 1048576,
    UC_MODE_ARM = 16
};

/* Memory types for hooks */
typedef int uc_mem_type;
enum {
    UC_MEM_READ = 1 << 8,
    UC_MEM_WRITE = 1 << 9,
    UC_MEM_FETCH = 1 << 10,
    UC_MEM_READ_UNMAPPED = 1 << 5,
    UC_MEM_WRITE_UNMAPPED = 1 << 6,
    UC_MEM_FETCH_UNMAPPED = 1 << 7,
    UC_MEM_READ_PROT = 1 << 11,
    UC_MEM_WRITE_PROT = 1 << 12,
    UC_MEM_FETCH_PROT = 1 << 13,
    UC_MEM_READ_AFTER = 1 << 14,
    UC_MEM_INVALID = 1 << 15
};

/* Memory protection */
enum {
    UC_PROT_NONE = 1,
    UC_PROT_READ = 2,
    UC_PROT_WRITE = 4,
    UC_PROT_EXEC = 8,
    UC_PROT_ALL = 14
};

/* Hook types */
enum {
    UC_HOOK_CODE = 1,
    UC_HOOK_BLOCK = 2,
    UC_HOOK_INTR = 4,
    UC_HOOK_INSN = 8,
    UC_HOOK_INSN_INVALID = 16,
    UC_HOOK_MEM_READ = 256,
    UC_HOOK_MEM_WRITE = 512,
    UC_HOOK_MEM_FETCH = 1024,
    UC_HOOK_MEM_READ_UNMAPPED = 32,
    UC_HOOK_MEM_WRITE_UNMAPPED = 64,
    UC_HOOK_MEM_FETCH_UNMAPPED = 128
};

/* ARM registers actually used by client code */
typedef int uc_arm_reg;
enum {
    UC_ARM_REG_CPSR = 1,
    UC_ARM_REG_LR = 14,
    UC_ARM_REG_PC = 15,
    UC_ARM_REG_R0 = 0,
    UC_ARM_REG_R1 = 1,
    UC_ARM_REG_R2 = 2,
    UC_ARM_REG_R3 = 3,
    UC_ARM_REG_R4 = 4,
    UC_ARM_REG_R5 = 5,
    UC_ARM_REG_R6 = 6,
    UC_ARM_REG_R7 = 7,
    UC_ARM_REG_R8 = 8,
    UC_ARM_REG_R9 = 9,
    UC_ARM_REG_R10 = 10,
    UC_ARM_REG_R11 = 11,
    UC_ARM_REG_R12 = 12,
    UC_ARM_REG_SP = 13,
    UC_ARM_REG_SPSR = 33,
    UC_ARM_REG_R13 = 13,
    UC_ARM_REG_R14 = 14,
    UC_ARM_REG_R15 = 15
};

/* Function declarations */
uc_err uc_open(int arch, int mode, uc_engine **handle);
void uc_close(uc_engine *uc);
uc_err uc_errno(uc_engine *uc);
const char *uc_strerror(uc_err err);
uc_err uc_emu_start(uc_engine *uc, uint64_t begin, uint64_t until, uint64_t timeout, uint64_t count);
void uc_emu_stop(uc_engine *uc);
uc_err uc_hook_add(uc_engine *uc, uc_hook *hook, int type, void *cb,
                    void *user_data, uint64_t begin, uint64_t end, ...);
uc_err uc_mem_read(uc_engine *uc, uint64_t address, void *buffer, size_t size);
uc_err uc_mem_write(uc_engine *uc, uint64_t address, const void *buffer, size_t size);
uc_err uc_reg_read(uc_engine *uc, int reg, void *value);
uc_err uc_reg_write(uc_engine *uc, int reg, const void *value);
uc_err uc_mem_map(uc_engine *uc, uint64_t address, size_t size, int perms);
uc_err uc_mem_map_ptr(uc_engine *uc, uint64_t address, size_t size, int perms, void *host_ptr);

#endif /* CBE_UNICORN_STUB_H */
