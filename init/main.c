/* init/main.c */
#include <asm.h>
#include <kernel.h>

void main() {
    if (r_mhartid() != 0) {
        schedule();
    }
    con_init();
    print_init();
    printk("riscv-mirix kernel booting ...\n\n");
    kalloc_init();
    kpage_init();
    proc_init();
    trap_init();
    plic_init();
    buf_init();
    inode_init();
    file_init();
    virtio_disk_init();
    user_init();
    __sync_synchronize();
    schedule();
}