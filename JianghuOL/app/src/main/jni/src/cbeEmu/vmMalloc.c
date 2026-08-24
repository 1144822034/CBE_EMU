#include "vmMalloc.h"

VMBlock *vm_head;

static VMBlock *vm_malloc_new_block(u32 addr, u32 size, int used)
{
    VMBlock *block = (VMBlock *)SDL_malloc(sizeof(VMBlock));
    block->addr = addr;
    block->size = size;
    block->req_size = size;
    block->used = used;
    block->next = NULL;
    return block;
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
            cur->used = 1;
            cur->req_size = cur->size;
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
            cur->req_size = cur->size;
            cur->used = 1;
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
            reserved = 1;
            cur = after->next;
        }
    }

    return reserved;
}

void InitVmMalloc()
{
    vm_head = (VMBlock *)SDL_malloc(sizeof(VMBlock));
    vm_head->addr = VM_MALLOC_POOL_ADDRESS;
    vm_head->size = VM_MemoryBlock_SIZE;
    vm_head->req_size = VM_MemoryBlock_SIZE;
    vm_head->used = 0;
    vm_head->next = NULL;

    /*
     * Battle.cbm is loaded into this VM pool window by the dynamic-CBM loader.
     * Keep ordinary vm_malloc callers from later handing the same bytes to CBE code
     * as a scratch block and clearing the module-local R9 data.
     */
    vm_malloc_reserve_range(0x05080000u, 0x00100000u);
}
// ok
u32 vm_malloc(u32 size)
{
    VMBlock *cur = vm_head;
    u32 req_size = size;

    if (size == 0)
        return 0;

    // 8字节对齐（很重要）
    size = (size + 7) & ~7;

    while (cur)
    {
        if (!cur->used && cur->size >= size)
        {
            // 分裂 block
            if (cur->size > size)
            {
                VMBlock *new_block = (VMBlock *)SDL_malloc(sizeof(VMBlock));
                new_block->addr = cur->addr + size;
                new_block->size = cur->size - size;
                new_block->req_size = new_block->size;
                new_block->used = 0;
                new_block->next = cur->next;

                cur->next = new_block;
            }

            cur->size = size;
            cur->req_size = req_size;
            cur->used = 1;

            // printf("[vm_malloc] addr=0x%08X size=%u\n", cur->addr, size);

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

// ok
void vm_free(u32 addr)
{
    VMBlock *cur = vm_head;

    while (cur)
    {
        if (cur->addr == addr)
        {
            cur->used = 0;

            // printf("[vm_free] addr=0x%08X\n", addr);

            // 合并相邻空闲块
            VMBlock *next = cur->next;
            if (next && !next->used)
            {
                cur->size += next->size;
                cur->next = next->next;
                SDL_free(next);
            }

            return;
        }

        cur = cur->next;
    }
    printf("[vm_free] INVALID addr=0x%08X\n", addr);
    assert(0);
}
