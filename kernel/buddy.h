#ifndef XV6_RISCV_RISCV_BUDDY_H
#define XV6_RISCV_RISCV_BUDDY_H

#define BLOCK_SIZE (4096)

void buddy_init(void *space, int block_num);
void* buddy_alloc(int block_num);
void buddy_free(void *space, int block_num);

#endif //XV6_RISCV_RISCV_BUDDY_H