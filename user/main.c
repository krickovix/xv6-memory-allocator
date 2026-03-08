#include "../kernel/types.h"
#include "../kernel/param.h"
#include "user.h"
#include "../kernel/slab.h"


#define RUN_NUM (5)
#define ITERATIONS (1000)

#define shared_size (7)
#define MASK (0xA5)

struct data_s {
    int id;
    kmem_cache_t  *shared;
    int iterations;
};

const char * const CACHE_NAMES[] = {"tc_0",
                                    "tc_1",
                                    "tc_2",
                                    "tc_3",
                                    "tc_4"};

int check(void *data, size_t size) {
    int ret = 1;
    for (int i = 0; i < size; i++) {
        if (((unsigned char *)data)[i] != MASK) {
            ret = 0;
        }
    }

    return ret;
}

void construct(void *data) {
    static int i = 1;
    printf("%d Shared object constructed.\n", i++);
    memset(data, MASK, shared_size);
}

struct objects_s {
    kmem_cache_t *cache;
    void *data;
};

void work(void* pdata) {
    struct data_s data = *(struct data_s*) pdata;
    int size = 0;
    int object_size = data.id + 1;
    kmem_cache_t *cache = kmem_cache_create(CACHE_NAMES[data.id], object_size, 0, 0);

    ////printf("kmalloc size: %d\n", (int)sizeof(struct objects_s)*data.iterations);
    struct objects_s *objs = (struct objects_s*)(kmalloc(sizeof(struct objects_s) * data.iterations));

    for (int i = 0; i < data.iterations; i++) {
        ////printf("i: %d\n", i);
        if (i % 100 == 0) {
            objs[size].data = kmem_cache_alloc(data.shared);
            objs[size].cache = data.shared;
            if (!check(objs[size].data, shared_size)) {
                //printf("Value not correct!");
            }
        }
        else {
            objs[size].data = kmem_cache_alloc(cache);
            objs[size].cache = cache;
            memset(objs[size].data, MASK, object_size);
        }
        //kmem_cache_info(cache);
        size++;
    }

    kmem_cache_info(cache);
    kmem_cache_info(data.shared);

    for (int i = 0; i < size; i++) {
        if (!check(objs[i].data, (cache == objs[i].cache) ? object_size : shared_size)) {
            printf("Value not correct!");
        }
        kmem_cache_free(objs[i].cache, objs[i].data);
    }

    kfree(objs);
    ////printf("before destroy: shared name = %s\n",       data.shared->name);
    //printf("work: destroying cache %s\n", cache->name);
    kmem_cache_destroy(cache);
    ////printf("after destroy: shared name = %s\n",       data.shared->name);
}



void runs(void(*work)(void*), struct data_s* data, int num) {
    for (int i = 0; i < num; i++) {
        struct data_s private_data;
        private_data = *(struct data_s*) data;
        private_data.id = i;
        work(&private_data);
    }
}

void main() {
    int num_of_blocks = 1024;
    void* space = sbrk(num_of_blocks * BLOCK_SIZE);
    kmem_init(space, num_of_blocks);
    kmem_cache_t *shared = kmem_cache_create("shared object", shared_size, construct, 0);
    ////printf("shared cache at: %p\n", shared);
    
    struct data_s data;
    data.shared = shared;
    data.iterations = ITERATIONS;

    runs(work, &data, RUN_NUM);

    ////printf("before destroy\n");
    kmem_cache_destroy(shared);
    ////printf("after destroy\n");
    //free(space);
    exit(0);
}
