// user/test_edge.c
#include "../kernel/types.h"
#include "../kernel/param.h"
#include "user.h"
#include "../kernel/slab.h"

#define NULL 0

void test_cache_create() {
    printf("--- test_cache_create ---\n");

    // NULL name
    kmem_cache_t *c = kmem_cache_create(NULL, 8, 0, 0);
    printf("NULL name: %s\n", c ? "returned cache (bad)" : "returned NULL (ok)");

    // zero size
    c = kmem_cache_create("zero", 0, 0, 0);
    printf("zero size: %s\n", c ? "returned cache (bad)" : "returned NULL (ok)");

    // valid cache
    c = kmem_cache_create("valid", 8, 0, 0);
    printf("valid cache: %s\n", c ? "ok" : "failed (bad)");
    kmem_cache_destroy(c);
}

void test_alloc_free() {
    printf("--- test_alloc_free ---\n");

    // alloc from NULL
    void *obj = kmem_cache_alloc(NULL);
    printf("alloc NULL cache: %s\n", obj ? "returned obj (bad)" : "returned NULL (ok)");

    // free NULL obj
    kmem_cache_t *c = kmem_cache_create("t", 8, 0, 0);
    kmem_cache_free(c, NULL);
    printf("free NULL obj: survived (ok)\n");

    // double free
    obj = kmem_cache_alloc(c);
    kmem_cache_free(c, obj);
    kmem_cache_free(c, obj);
    printf("double free: survived\n");
    kmem_cache_error(c);

    kmem_cache_destroy(c);
}

void test_kmalloc() {
    printf("--- test_kmalloc ---\n");

    // zero size
    void *p = kmalloc(0);
    printf("kmalloc(0): %s\n", p ? "returned ptr" : "returned NULL");

    // valid
    p = kmalloc(64);
    printf("kmalloc(64): %s\n", p ? "ok" : "failed (bad)");
    kfree(p);

    // NULL free
    kfree(NULL);
    printf("kfree(NULL): survived (ok)\n");
}

void test_destroy() {
    printf("--- test_destroy ---\n");

    kmem_cache_destroy(NULL);
    printf("destroy NULL: survived (ok)\n");

    kmem_cache_t *c = kmem_cache_create("leak", 8, 0, 0);
    void *obj = kmem_cache_alloc(c);
    (void)obj;
    kmem_cache_destroy(c);
    printf("destroy with live objects survived\n");

    kmem_cache_destroy(c);
    printf("double destroy survived\n");
}

void test_shrink() {
    printf("--- test_shrink ---\n");

    kmem_cache_shrink(NULL);
    printf("shrink NULL: survived (ok)\n");

    kmem_cache_t *c = kmem_cache_create("shrink", 8, 0, 0);
    kmem_cache_shrink(c);
    printf("shrink empty cache: survived (ok)\n");
    kmem_cache_destroy(c);
}

void main() {
    int num_of_blocks = 1024;
    void *space = sbrk(num_of_blocks * BLOCK_SIZE);
    kmem_init(space, num_of_blocks);

    test_cache_create();
    test_alloc_free();
    test_kmalloc();
    test_destroy();
    test_shrink();

    printf("all edge case tests done\n");
    exit(0);
}