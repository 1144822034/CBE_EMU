#include "vmMalloc.h"
#include <string.h>

VMBlock *vm_head;

#define VM_USED_HASH_BITS 12
#define VM_USED_HASH_SIZE (1u << VM_USED_HASH_BITS)

static VMBlock *s_used_hash[VM_USED_HASH_SIZE];
static unsigned s_free_ok_total;
static unsigned s_free_ok_logged;

static unsigned vm_used_hash_idx(u32 addr)
{
    return (addr * 2654435761u) >> (32 - VM_USED_HASH_BITS);
}

static void vm_used_hash_add(VMBlock *b)
{
    unsigned i;

    if (!b || !b->used)
        return;
    i = vm_used_hash_idx(b->addr);
    b->hash_next = s_used_hash[i];
    s_used_hash[i] = b;
}

static void vm_used_hash_del(VMBlock *b)
{
    unsigned i;
    VMBlock **pp;

    if (!b)
        return;
    i = vm_used_hash_idx(b->addr);
    pp = &s_used_hash[i];
    while (*pp)
    {
        if (*pp == b)
        {
            *pp = b->hash_next;
            b->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
    b->hash_next = NULL;
}

static VMBlock *vm_used_hash_find(u32 addr)
{
    VMBlock *b;

    for (b = s_used_hash[vm_used_hash_idx(addr)]; b; b = b->hash_next)
    {
        if (b->addr == addr && b->used)
            return b;
    }
    return NULL;
}

static VMBlock *vm_malloc_new_block(u32 addr, u32 size, int used)
{
    VMBlock *block = (VMBlock *)SDL_malloc(sizeof(VMBlock));
    block->addr = addr;
    block->size = size;
    block->req_size = size;
    block->used = used;
    block->next = NULL;
    block->hash_next = NULL;
    return block;
}

static void vm_mark_used(VMBlock *b, u32 req_size)
{
    if (!b)
        return;
    if (b->used)
        vm_used_hash_del(b);
    b->used = 1;
    b->req_size = req_size;
    vm_used_hash_add(b);
}

int vm_malloc_reserve_range(u32 addr, u32 size)
{
    VMBlock *cur = vm_head;
    int reserved = 0;
    u32 reserveStart;
    u32 reserveEnd;

    if (!cur || size == 0)
        return 0;

    reserveStart = addr & ~7u;
    reserveEnd = (addr + size + 7u) & ~7u;
    if (reserveEnd <= reserveStart)
        return 0;

    while (cur)
    {
        u32 blockStart = cur->addr;
        u32 blockEnd = cur->addr + cur->size;
        u32 overlapStart;
        u32 overlapEnd;

        if (cur->used || reserveStart >= blockEnd || reserveEnd <= blockStart)
        {
            cur = cur->next;
            continue;
        }

        overlapStart = reserveStart > blockStart ? reserveStart : blockStart;
        overlapEnd = reserveEnd < blockEnd ? reserveEnd : blockEnd;

        if (overlapStart == blockStart && overlapEnd == blockEnd)
        {
            vm_mark_used(cur, cur->size);
            reserved = 1;
            cur = cur->next;
            continue;
        }

        if (overlapStart == blockStart)
        {
            VMBlock *after = NULL;
            if (overlapEnd < blockEnd)
                after = vm_malloc_new_block(overlapEnd, blockEnd - overlapEnd, 0);
            if (after)
                after->next = cur->next;
            cur->size = overlapEnd - blockStart;
            vm_mark_used(cur, cur->size);
            cur->next = after;
            reserved = 1;
            cur = after;
            continue;
        }

        if (overlapEnd == blockEnd)
        {
            VMBlock *reservedBlock = vm_malloc_new_block(overlapStart, overlapEnd - overlapStart, 1);
            reservedBlock->next = cur->next;
            cur->size = overlapStart - blockStart;
            cur->req_size = cur->size;
            cur->next = reservedBlock;
            vm_used_hash_add(reservedBlock);
            reserved = 1;
            cur = reservedBlock->next;
            continue;
        }

        {
            VMBlock *reservedBlock = vm_malloc_new_block(overlapStart, overlapEnd - overlapStart, 1);
            VMBlock *after = vm_malloc_new_block(overlapEnd, blockEnd - overlapEnd, 0);
            after->next = cur->next;
            reservedBlock->next = after;
            cur->size = overlapStart - blockStart;
            cur->req_size = cur->size;
            cur->next = reservedBlock;
            vm_used_hash_add(reservedBlock);
            reserved = 1;
            cur = after->next;
        }
    }

    return reserved;
}

void InitVmMalloc()
{
    memset(s_used_hash, 0, sizeof(s_used_hash));
    s_free_ok_total = 0;
    s_free_ok_logged = 0;

    vm_head = (VMBlock *)SDL_malloc(sizeof(VMBlock));
    vm_head->addr = VM_MALLOC_POOL_ADDRESS;
    vm_head->size = VM_MemoryBlock_SIZE;
    vm_head->req_size = VM_MemoryBlock_SIZE;
    vm_head->used = 0;
    vm_head->next = NULL;
    vm_head->hash_next = NULL;

    /*
     * Battle.cbm is loaded into this VM pool window by the dynamic-CBM loader.
     * Keep ordinary vm_malloc callers from later handing the same bytes to CBE code
     * as a scratch block and clearing the module-local R9 data.
     */
    vm_malloc_reserve_range(0x05080000u, 0x00100000u);
}

u32 vm_malloc(u32 size)
{
    VMBlock *cur = vm_head;
    u32 req_size = size;

    if (size == 0)
        return 0;

    /* 8字节对齐（很重要） */
    size = (size + 7) & ~7;

    while (cur)
    {
        if (!cur->used && cur->size >= size)
        {
            if (cur->size > size)
            {
                VMBlock *new_block = vm_malloc_new_block(cur->addr + size, cur->size - size, 0);
                new_block->next = cur->next;
                cur->next = new_block;
            }

            cur->size = size;
            vm_mark_used(cur, req_size);
            return cur->addr;
        }

        cur = cur->next;
    }
    printf("[vm_malloc] FAILED size=%u\n", size);
    assert(0);
    return 0;
}

u32 vm_malloc_user_size(u32 addr)
{
    VMBlock *cur = vm_head;

    while (cur)
    {
        if (cur->used && addr >= cur->addr && addr < cur->addr + cur->size)
        {
            u32 offset = addr - cur->addr;
            if (offset < cur->req_size)
                return cur->req_size - offset;
            return 0;
        }

        cur = cur->next;
    }

    return 0;
}

void vm_free(u32 addr)
{
    VMBlock *cur;
    static unsigned s_invalid_total;
    static unsigned s_invalid_printed;
    static unsigned s_invalid_streak;
    static u32 s_invalid_last;

    /* Pool-outside: title teardown often frees garbage (0x77 / 0xFFF3…).
     * Skip O(n) list walk — that walk dominated Android INVALID storms. */
    if (addr < VM_MALLOC_POOL_ADDRESS ||
        addr >= VM_MALLOC_POOL_ADDRESS + VM_MemoryBlock_SIZE)
    {
        goto invalid;
    }

    cur = vm_used_hash_find(addr);
    if (cur)
    {
        vm_used_hash_del(cur);
        cur->used = 0;

        {
            VMBlock *next = cur->next;
            if (next && !next->used)
            {
                cur->size += next->size;
                cur->next = next->next;
                SDL_free(next);
            }
        }

        s_invalid_streak = 0;
        s_free_ok_total++;
        vm_host_note_teardown_free(1);
        if (s_free_ok_logged < 8u &&
            (s_free_ok_total == 1u || (s_free_ok_total % 5000u) == 0u))
        {
            printf("[info][vm_free] ok_progress n=%u addr=0x%08X\n",
                   s_free_ok_total, addr);
            s_free_ok_logged++;
        }
        return;
    }

invalid:
    s_invalid_total++;
    if (s_invalid_streak > 0u && addr == s_invalid_last + 4u)
        s_invalid_streak++;
    else
        s_invalid_streak = 1u;
    s_invalid_last = addr;

    vm_host_note_teardown_free(0);
    if (s_invalid_printed < 3u)
    {
        printf("[vm_free] INVALID addr=0x%08X (n=%u)\n", addr, s_invalid_total);
        vm_host_trace_invalid_free(addr, s_invalid_total);
        s_invalid_printed++;
    }
    else if (s_invalid_total == 4u || (s_invalid_total % 10000u) == 0u)
    {
        printf("[vm_free] INVALID suppressed total=%u last=0x%08X streak=%u\n",
               s_invalid_total, addr, s_invalid_streak);
    }
    /* Do not uc_emu_stop on INVALID storms: stop mid-teardown freezes
     * after ScreenResume (same class of bug as remove-newtop emu_stop). */
    (void)s_invalid_streak;
}