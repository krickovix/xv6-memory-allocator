#ifndef XV6_RISCV_RISCV_SLAB_H
#define XV6_RISCV_RISCV_SLAB_H

#include "types.h"
#include "spinlock.h"

typedef struct kmem_slab_s kmem_slab_t;
typedef struct kmem_slab_obj_s kmem_slab_obj_t;

typedef unsigned long size_t;

#define CACHE_NAME_MAX 20
enum ERROR {NO_ERROR, ALLOC_ERROR, FREE_ERROR};
enum FLAG {FALSE, TRUE};

typedef struct kmem_cache_s
{
    char name[CACHE_NAME_MAX];

    size_t obj_size; 
    size_t aligned_size; // obj_size + size of obj header
    uint slab_block_num;
    uint max_objs_per_slab;

    uint block_num;

    kmem_slab_t *free_slabs;
    kmem_slab_t *full_slabs;
    kmem_slab_t *partial_slabs;
    uint slab_num;

    void (*ctor)(void *);
    void (*dtor)(void *);

    enum FLAG added_free_slab;
    enum FLAG valid_cache;
    enum ERROR error_flags;

    struct spinlock lock;

    struct kmem_cache_s *next;
}kmem_cache_t;

#define BLOCK_SIZE (4096)

void kmem_init(void *space, int block_num);

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void (*ctor)(void *), void (*dtor)(void *)); // Allocate cache

int kmem_cache_shrink(kmem_cache_t *cachep); // Shrink cache

void *kmem_cache_alloc(kmem_cache_t *cachep); // Allocate one object from cache

void kmem_cache_free(kmem_cache_t *cachep, void *objp); // Deallocate one object from cache

void *kmalloc(size_t size); // Alloacate one small memory buffer

void kfree(const void *objp); // Deallocate one small memory buffer

void kmem_cache_destroy(kmem_cache_t *cachep); // Deallocate cache

void kmem_cache_info(kmem_cache_t *cachep); // Print cache info

int kmem_cache_error(kmem_cache_t *cachep); // Print error message

#endif //XV6_RISCV_RISCV_SLAB_H