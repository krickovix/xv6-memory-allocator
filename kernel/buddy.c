#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "buddy.h"

typedef struct buddy_block_s
{
    struct buddy_block_s *next;
} buddy_block_t;

#define MAX_ALLOC_ORDER 20

struct {
    void *base_addr;
    int total_blocks;
    int max_order;
    buddy_block_t *free_block_lists[MAX_ALLOC_ORDER];
    struct spinlock lock;
} buddy;

int log2(int block_num){
    int order = 0;
    int block_num_cpy = block_num;
    while(block_num_cpy > 0){
        order++;
        block_num_cpy>>=1;
    }

    return order-1;
}

void buddy_init(void *space, int block_num)
{
    if(block_num < 1)
    {
        //panic("buddy_allocator_init: num of blocks 0 or smaller");
        return;
    }

    int max_order = log2(block_num);

    if(max_order > MAX_ALLOC_ORDER)
    {
        //panic("buddy_allocator_init: num of blocks too big");
        return;
    }

    initlock(&buddy.lock, "buddy_lock");
    buddy.base_addr = space;
    buddy.total_blocks = block_num;
    buddy.max_order = max_order;

    for(int i = 0; i < max_order; i++)
        buddy.free_block_lists[i] = 0;

    buddy_block_t *curr = (buddy_block_t *) buddy.base_addr;
    int remaining = block_num;
    int order = max_order;
    while (remaining > 0){
        while(1 << order > remaining)
            order--;

        // add new block to list
        curr->next = buddy.free_block_lists[order];
        buddy.free_block_lists[order] = curr;

        remaining -= (1<<order);
        curr = (buddy_block_t *)((char *)curr + (1<<order)*BLOCK_SIZE);
    }
}

static int calculate_min_order(int block_num)
{
    for(int order = 0; order <= buddy.max_order; order++){
        if((1 << order) >= block_num)
            return order;
    }
    return -1;
}

static void *calculate_buddy_addr(void *block, int block_num){
    uint64 block_index = ((char *) block - (char *) buddy.base_addr) / BLOCK_SIZE;
    uint64 buddy_index = block_index ^ block_num;
    return buddy.base_addr + buddy_index*BLOCK_SIZE;
}

/*static void *calculate_parent_addr(void *block, int order){
    uint64 block_size = (uint64)(1 << order) * BLOCK_SIZE;
    uint64 parent_size = block_size * 2;
    uint64 addr = (uint64)block;
    return (void*)(addr & ~(parent_size - 1));
}*/

void* buddy_alloc(int block_num)
{
    if(block_num < 1)
    {
        //panic("buddy_alloc: block_num smaller than 1");
        return 0;
    }

    acquire(&buddy.lock);
    int min_order = calculate_min_order(block_num);
    if(min_order < 0) {
        release(&buddy.lock);
        //panic("buddy_alloc: block_num too big for allocated mem");
        return 0;
    }

    int order = min_order;
    while(order <= buddy.max_order && !buddy.free_block_lists[order]) order++; // find the lowest free order
    if(order > buddy.max_order) {
        release(&buddy.lock);
        //panic("buddy_alloc: buddy full");
        return 0;
    }

    buddy_block_t *block = buddy.free_block_lists[order];
    buddy.free_block_lists[order] = buddy.free_block_lists[order]->next;

    while(order > min_order){
        order--;

        buddy_block_t *buddy_block = (buddy_block_t *)((char *)block + (1<<order)*BLOCK_SIZE);
        buddy_block->next = buddy.free_block_lists[order];
        buddy.free_block_lists[order] = buddy_block;
    }

    release(&buddy.lock);

    return block;
}

void buddy_free(void *space, int block_num) {
    if(!space || block_num < 1)
        return;

    int order = calculate_min_order(block_num);

    acquire(&buddy.lock);

    buddy_block_t *addr = (buddy_block_t *) space;

    while(order <= buddy.max_order) {
        buddy_block_t *buddy_addr = (buddy_block_t *)calculate_buddy_addr(addr, 1 << order);

        // search for buddy in free list at this order
        buddy_block_t **pp = &buddy.free_block_lists[order];
        while(*pp && *pp != buddy_addr)
            pp = &(*pp)->next;

        // no buddy in list -> end of merging
        if(!*pp)
            break;

        // found buddy -> remove buddy from this list
        *pp = buddy_addr->next;

        // take the lower address as parent
        if(buddy_addr < addr)
            addr = buddy_addr;

        // clear next pointer of merged block
        addr->next = 0;

        order++;
    }

    addr->next = buddy.free_block_lists[order];
    buddy.free_block_lists[order] = addr;

    release(&buddy.lock);
}