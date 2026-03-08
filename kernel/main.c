#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "slab.h"

volatile static int started = 0;

#define SLAB_BLOCK_NUM 1024  // TODO: IF CRASHES ON BOOT, CHANGE TO 512
static char slab_pool[SLAB_BLOCK_NUM * BLOCK_SIZE];

static kmem_cache_t *spinlock_cache;
struct spinlock *spinlock_alloc()
{
  struct spinlock *lock = kmem_cache_alloc(spinlock_cache);
  return lock;
}

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    kinit();         // physical page allocator
    kmem_init(slab_pool, SLAB_BLOCK_NUM); // initialize slab allocator
    spinlock_cache = kmem_cache_create("spinlock", sizeof(struct spinlock), 0, 0);
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
