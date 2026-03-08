#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "slab.h"
#include "defs.h"
#include "buddy.h"

#define MEM_BUFFER_NUM 13 // 2^5 - 2^17
#define  MIN_BUFFER_SIZE (1<<5)
#define  MAX_BUFFER_SIZE (1<<17)

static kmem_cache_t *cache_list = 0;
static struct spinlock cache_list_lock;
static enum FLAG allocator_initialized = FALSE;
static kmem_cache_t *memory_buffer_cache_list[MEM_BUFFER_NUM];
static struct spinlock memory_buffer_cache_list_lock;

struct kmem_slab_s
{
    void* mem;

    size_t obj_size; // [bytes]
    kmem_slab_obj_t* free_list;

    uint obj_num;
    uint block_num;

    kmem_slab_t* prev;
    kmem_slab_t* next;
    kmem_cache_t* cache;
};

struct kmem_slab_obj_s
{
    kmem_slab_obj_t* next;
    kmem_slab_t *slab;

};


void add_slab_to(kmem_slab_t **list_head, kmem_slab_t *slab){
    slab->prev = 0;

    slab->next = *list_head;
    if(*list_head)
        (*list_head)->prev = slab;

    *list_head = slab;
}

void remove_slab_from(kmem_slab_t **list_head, kmem_slab_t *slab){

    if(slab->prev)
        slab->prev->next = slab->next;
    else
        *list_head = slab->next;

    if(slab->next)
        slab->next->prev = slab->prev;

    slab->prev = 0;
    slab->next = 0;
}

static kmem_slab_t *slab_create(kmem_cache_t *cachep)
{
    void *mem = buddy_alloc(cachep->slab_block_num);
    if (!mem)
        return 0;

    kmem_slab_t *slab = (kmem_slab_t *) mem;
    slab->mem = mem;
    slab->obj_size = cachep->obj_size;

    char *obj_start = (char *)mem + sizeof(kmem_slab_t);
    slab->free_list = 0;

    for (int i = cachep->max_objs_per_slab-1; i>=0; i--)
    {
        char *objp = obj_start + i*cachep->aligned_size;
        kmem_slab_obj_t *obj = (kmem_slab_obj_t *) objp;
        obj->next = slab->free_list;
        slab->free_list = obj;
    }

    slab->obj_num = 0;
    slab->block_num = cachep->slab_block_num;

    slab->prev = 0;
    slab->next = 0;
    slab->cache = cachep;

    cachep->slab_num++;
    add_slab_to(&cachep->free_slabs, slab);

    return slab;
}

void* slab_add_obj(kmem_slab_t* slab, kmem_cache_t *cachep)
{
    if (!slab)
    {
        cachep->error_flags = ALLOC_ERROR;
        //panic("slab_add_obj: slab does not exist");
        return 0;
    }
    if(!slab->free_list || slab->obj_num == cachep->max_objs_per_slab){
        cachep->error_flags = ALLOC_ERROR;
        //panic("slab_add_obj: adding obj to full slab");
        return 0;
    }

    kmem_slab_obj_t *obj = slab->free_list;
    slab->free_list = slab->free_list->next;
    slab->obj_num++;

    obj->slab = slab;

    return (char*)obj + sizeof (kmem_slab_obj_t);
}

void slab_remove_obj(kmem_slab_t* slab, kmem_cache_t *cachep, kmem_slab_obj_t *obj)
{
    if(slab->obj_num == 0){
        cachep->error_flags = FREE_ERROR;
        //panic("slab_add_obj: removing obj from free slab");
        return;
    }

    obj->next = slab->free_list;
    slab->free_list = obj;
    slab->obj_num--;
}

void slab_destroy_objs(kmem_slab_t *slab, const kmem_cache_t *cachep){

    if (!slab) return;

    if(cachep->dtor){
        char *obj_start = (char *) slab->mem + sizeof(kmem_slab_t);
        for (uint i = 0; i < slab->obj_num; i++)
        {
            char *objp = obj_start + i*cachep->aligned_size;
            void *obj = objp + sizeof(kmem_slab_obj_t);
            cachep->dtor(obj);
        }
    }

    slab->free_list = 0;
}


void kmem_init(void *space, int block_num)
{
    buddy_init(space, block_num);
    cache_list = 0;
    initlock(&cache_list_lock, "cache_list_lock");

    for(int i = 0; i < MEM_BUFFER_NUM; i++)
        memory_buffer_cache_list[i] = 0;
    initlock(&memory_buffer_cache_list_lock, "mem_buffer_cache_list_lock");

    allocator_initialized = TRUE;
}

uint calculate_objs_per_slab(const uint slab_block_num, const size_t obj_size){
    if(obj_size == 0) return 0;

    uint res = (slab_block_num*BLOCK_SIZE-sizeof(kmem_slab_t) )/ (obj_size + sizeof(kmem_slab_obj_t));
    if(res*(obj_size + sizeof(kmem_slab_obj_t)) < (slab_block_num*BLOCK_SIZE-sizeof(kmem_slab_t)))
        res += 1;
    return res;
}

#define MIN_BLOCKS_PER_SLAB 8
#define MIN_OBJS_PER_SLAB 16

uint calculate_blocks_per_slab(const size_t obj_size){
    if(obj_size == 0) return 0;

    uint block_num = 1, obj_num = 0;
    while(block_num < MIN_BLOCKS_PER_SLAB || obj_num < MIN_OBJS_PER_SLAB){
        block_num <<= 1;
        obj_num = calculate_objs_per_slab(block_num, obj_size);
    }

    return block_num;
}

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void (*ctor)(void *), void (*dtor)(void *))
{
    if(allocator_initialized == FALSE)
        return 0;

    if (size == 0 || !name)
        return 0;

    kmem_cache_t *cachep = buddy_alloc(1);
    if (!cachep)
        return 0;

    //printf("cache created at: %p\n", cachep);

    memset(cachep, 0, sizeof(kmem_cache_t));

    int name_len = strlen(name);
    if(name_len > CACHE_NAME_MAX)
        name_len = CACHE_NAME_MAX-1;

    safestrcpy(cachep->name, name, name_len+1);

    cachep->obj_size = size;
    cachep->aligned_size = size + sizeof(kmem_slab_obj_t);
    cachep->slab_block_num = calculate_blocks_per_slab(size);
    cachep->max_objs_per_slab = calculate_objs_per_slab(cachep->slab_block_num, size);

    cachep->block_num = 1;

    cachep->free_slabs = 0;
    cachep->full_slabs = 0;
    cachep->partial_slabs = 0;
    cachep->slab_num = 0;

    cachep->ctor = ctor;
    cachep->dtor = dtor;

    cachep->added_free_slab = FALSE;
    cachep->valid_cache = TRUE;
    cachep->error_flags = NO_ERROR;

    initlock(&cachep->lock, cachep->name);

    acquire(&cache_list_lock);
    cachep->next = cache_list;
    cache_list = cachep;
    release(&cache_list_lock);

    return cachep;
}

int kmem_cache_shrink(kmem_cache_t *cachep)
{
    if(!cachep || cachep->valid_cache == FALSE)
        return -1;

    acquire(&cachep->lock);

    if(cachep->added_free_slab == TRUE) {
        cachep->added_free_slab = FALSE;
        release(&cachep->lock);
        return 0;
    }

    uint freed_block_num = 0;

    kmem_slab_t* free_slab = cachep->free_slabs;
    while(free_slab){
        kmem_slab_t *next = free_slab->next;

        freed_block_num += free_slab->block_num;
        buddy_free(free_slab, free_slab->block_num);
        cachep->slab_num--;

        free_slab = next;
    }
    cachep->free_slabs = 0;

    cachep->added_free_slab = FALSE;
    release(&cachep->lock);
    return freed_block_num;
}

void* kmem_cache_alloc(kmem_cache_t *cachep)
{
    if (!cachep || cachep->valid_cache == FALSE)
        return 0;

    kmem_slab_t* picked_slab = 0;

    acquire(&cachep->lock);
    if (cachep->partial_slabs)
    {
        picked_slab = cachep->partial_slabs;

        if (cachep->partial_slabs->obj_num + 1 == cachep->max_objs_per_slab)
        {
            remove_slab_from(&cachep->partial_slabs, picked_slab);
            add_slab_to(&cachep->full_slabs, picked_slab);

            if(!cachep->partial_slabs && !cachep->free_slabs){
                cachep->free_slabs = slab_create(cachep);
                cachep->added_free_slab = TRUE;
            }
        }
    }
    else {
        picked_slab = cachep->free_slabs? cachep->free_slabs : slab_create(cachep);

        remove_slab_from(&cachep->free_slabs, picked_slab);
        add_slab_to(&cachep->partial_slabs, picked_slab);
    }

    if(!picked_slab){
        cachep->error_flags = ALLOC_ERROR;
        release(&cachep->lock);
        //panic("kmem_cache_alloc: error picking slab");
        return 0;
    }

    void* obj_addr = slab_add_obj(picked_slab, cachep);
    if(!obj_addr){
        cachep->error_flags = ALLOC_ERROR;
        release(&cachep->lock);
        //panic("kmem_cache_alloc: slab full");
        return 0;
    }

    release(&cachep->lock);
    return obj_addr;
}

void kmem_cache_free(kmem_cache_t *cachep, void *objp) {

    if(!cachep || !objp || cachep->valid_cache == FALSE)
        return;

    kmem_slab_obj_t *obj = (kmem_slab_obj_t *) ((char *)objp - sizeof(kmem_slab_obj_t));

    acquire(&cachep->lock);

    kmem_slab_t* slab = obj->slab;

    if (!slab){
        cachep->error_flags = FREE_ERROR;
        release(&cachep->lock);
        //panic("kmem_cache_alloc: obj not belonging to any slab");
        return;
    }

    slab_remove_obj(slab, cachep, obj);

    if(slab->obj_num + 1 == cachep->max_objs_per_slab){
        remove_slab_from(&cachep->full_slabs, slab);
        add_slab_to(&cachep->partial_slabs, slab);
    }

    if(slab->obj_num == 0){
        remove_slab_from(&cachep->partial_slabs, slab);
        add_slab_to(&cachep->free_slabs, slab);
    }

    release(&cachep->lock);
}

void *kmalloc(size_t size){

    if(allocator_initialized == FALSE)
    {
        //panic("kmalloc: allocator not initialized");
        return 0;
    }

    if (size < MIN_BUFFER_SIZE || size > MAX_BUFFER_SIZE)
    {
        //panic("kmalloc: buffer size not between 2^5-2^17");
        return 0;
    }

    size_t rounded_size = MIN_BUFFER_SIZE;
    int index = 0;
    while(rounded_size < size && rounded_size < MAX_BUFFER_SIZE){
        rounded_size <<= 1;
        index++;
    }

    acquire(&memory_buffer_cache_list_lock);
    kmem_cache_t* cachep = memory_buffer_cache_list[index];
    if (!cachep || cachep->valid_cache == FALSE)
    {
        char name[32];
        char *p = name;
        *p++ = 's';
        *p++ = 'i';
        *p++ = 'z';
        *p++ = 'e';
        *p++ = '-';

        size_t temp = rounded_size;
        char digs[20];
        int dig_num = 0;
        do{
            digs[dig_num++] = '0' + temp%10;
            temp /= 10;
        }while(temp > 0);

        for(int i = dig_num-1; i >= 0; i--) {
            *p++ = digs[i];
        }
        *p = '\0';

        cachep = kmem_cache_create(name, rounded_size, 0, 0);
        memory_buffer_cache_list[index] = cachep;
    }

    release(&memory_buffer_cache_list_lock);
    return kmem_cache_alloc(cachep);
}

void kfree(const void *objp){

    if (!objp)
        return;

    acquire(&memory_buffer_cache_list_lock);

    kmem_slab_obj_t *obj = (kmem_slab_obj_t *) ((char *)objp - sizeof(kmem_slab_obj_t));
    kmem_cache_t *cachep = obj->slab->cache;

    release(&memory_buffer_cache_list_lock);
    kmem_cache_free(cachep, (void *) objp);
}

void kmem_cache_destroy(kmem_cache_t *cachep)
{
    if (!cachep || cachep->valid_cache == FALSE)
        return;

    //printf("destroy: acquiring lock\n");
    acquire(&cachep->lock);
    //printf("destroy: freeing free_slabs\n");

    kmem_slab_t *slab;
    while(cachep->free_slabs){
        slab = cachep->free_slabs;
        remove_slab_from(&cachep->free_slabs, slab);
        slab_destroy_objs(slab, cachep);
        buddy_free(slab, slab->block_num);
    }
    //printf("destroy: freeing partial_slabs\n");
    while(cachep->partial_slabs){
        slab = cachep->partial_slabs;
        remove_slab_from(&cachep->partial_slabs, slab);
        slab_destroy_objs(slab, cachep);
        ////printf("buddy_free partial slab: %p, block_num=%d\n", slab, slab->block_num);
        buddy_free(slab, slab->block_num);
        ////printf("ZIV SAM");
    }
    //printf("destroy: freeing full_slabs\n");
    ////printf("ZIV SAM1");
    while(cachep->full_slabs){
        slab = cachep->full_slabs;
        remove_slab_from(&cachep->full_slabs, slab);
        slab_destroy_objs(slab, cachep);
        buddy_free(slab, slab->block_num);
    }
    ////printf("ZIV SAM2");
    //printf("destroy: acquiring cache_list_lock\n");
    acquire(&cache_list_lock);
    //printf("destroy: removing from cache list\n");

    kmem_cache_t **pp = &cache_list;
    while (*pp && *pp != cachep)
        pp = &(*pp)->next;

    if(!(*pp)){
        cachep->error_flags = FREE_ERROR;
        release(&cache_list_lock);
        //panic("kmem_cache_destroy: cache not in cache list");
        return;
    }

    *pp = cachep->next;

    //printf("destroy: releasing cache_list_lock\n");
    release(&cache_list_lock);
    //printf("destroy: setting valid_cache=FALSE\n");
    cachep->valid_cache = FALSE;
    //printf("destroy: releasing lock\n");
    release(&cachep->lock);
    //printf("destroy: buddy_free cachep=%p\n", cachep);
    buddy_free(cachep, 1);
    //printf("destroy: done\n");
}

void kmem_cache_info(kmem_cache_t *cachep)
{
    if(!cachep || cachep->valid_cache == FALSE) {
        printf("ERROR: Invalid cache!");
        return;
    }

    acquire(&cachep->lock);
    const char *name = cachep->name;
    size_t obj_size = cachep->obj_size;
    uint block_num = cachep->slab_num*cachep->slab_block_num;
    uint slab_num = cachep->slab_num;
    uint objs_per_slab = cachep->max_objs_per_slab;

    uint full_slab_num = 0;
    kmem_slab_t *slab = cachep->full_slabs;
    while(slab){
        full_slab_num++;
        slab = slab->next;
    }
    uint utilized = full_slab_num*cachep->max_objs_per_slab;

    slab = cachep->partial_slabs;
    while(slab){
        utilized += slab->obj_num;
        slab = slab->next;
    }
    uint total_objects = slab_num*objs_per_slab;

    uint fullness = total_objects > 0? (utilized*10000) / total_objects : 0;

    printf("\n\n----------------CACHE INFO----------------\n"
           "NAME: %s\n"
           "OBJECT SIZE: %d\n"
           "BLOCK NUM: %d\n"
           "SLAB NUM: %d\n"
           "OBJECTS PER SLAB: %d\n"
           "UTILIZED: %u.%u%% OF CACHE CAPACITY\n",
           //"utilized: %u, total: %u\n\n",
           name,
           (int)obj_size,
           block_num,
           slab_num,
           objs_per_slab,
           fullness/100,
           fullness%100);
           //utilized,
           //total_objects);

    release(&cachep->lock);
}

int kmem_cache_error(kmem_cache_t *cachep)
{
    if(!cachep || cachep->valid_cache == FALSE)
        return -1;

    acquire(&cachep->lock);
    int err = cachep->error_flags;
    release(&cachep->lock);

    return err;
}