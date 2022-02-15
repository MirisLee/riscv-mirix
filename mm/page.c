/* mm/page.c */
#include <asm.h>
#include <mem.h>
#include <kernel.h>
#include <elf.h>
#include <fs.h>

pagetable_t kpagetable;

extern char etext[];	/* kernel.ld */
extern char trampoline[];	/* trampoline.s */

pagetable_t kpage_make(void) {
	pagetable_t kpgtbl = (pagetable_t )kalloc();
	memset(kpgtbl, 0, PGSIZE);

	kpage_map(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
	kpage_map(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
	kpage_map(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
	/* kernel text */
	kpage_map(kpgtbl, K_BASE, K_BASE, (long )etext - K_BASE, PTE_R | PTE_X);
	/* kernel data */
	kpage_map(kpgtbl, (long )etext, (long )etext, PHTSTOP - (long )etxt, PTE_R | PTE_W);
	kpage_map(kpgtbl, TRAMPOLINE, (long )trampoline, PGSIZE, PTE_R | PTE_X);
	/* kernel stack */
	proc_mapstacks(kpgtbl);

	return kpgtbl;
}

void kpage_init(void) {
	kpagetable = kpage_make();
	w_satp(MAKE_STAP(kpagetable));
	sfence_vma();
}

pte_t *walk(pagetable_t pagetable, long va, int alloc) {
	if (va >= MAXVA) panic("walk");

	for (int level = 2; level > 0; --level) {
		pte_t *pte = &pagetable[PX(level, va)];
		if (*pte & PTE_V) {
			pagetable = (pagetable_t )PTE2PA(*pte);
		} else {
			/* create required pagetable pages if alloc */
			if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) 
				return 0;
			memset(pagetable, 0, PGSIZE);
			*pte = PA2PTE(pagetable) | PTE_V;
		}
	}
	return &pagetable[PX(0, va)];
}

long walkaddr(pagetable_t pagetable, long va) {
	pte_t *pte;
	long pa;

	if (va >= MAXVA) return 0;
	pte = walk(pagetable, va, 0);
	if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U))
		return 0;
	pa = PTE2PA(*pte);
	return pa;
}

void kpage_map(pagetable_t kpgtbl, long va, long pa, long size, int flags) {
	if (map(kpgtbl, va, pa, size, flags))
		panic("kpage_map");
}

int map(pagetable_t pagetable, long va, long pa, long size, int flags) {
	long addr, end;
	pte_t *pte;

	if (!size) panic("map: size error");
	addr = PGROUNDDOWN(va);
	end = PGROUNDDOWN(va + size - 1);
	while (1) {
		if (!(pte = walk(pagetable, addr, 1))) return -1;
		if (*pte & PTE_V) panic("map: map twice");
		*pte = PA2PTE(pa) | PTE_V | flags;
		if (addr == end) break;
		addr += PGSIZE;
		pa += PGSIZE;
	}
	return 0;
}

void upage_unmap(pagetable_t pagetable, long va, long npages, int free) {
	long addr;
	pte_t *pte;

	if ((va % PGSIZE)) panic("upage_unmap: not aligned");
	for (addr = va; addr < va + npages * PGSIZE; addr += PGSIZE) {
		if (!(pte = walk(pagetable, addr, 0))) panic("upage_unmap: walk");
		if (!(*pte & PTE_V)) panic("upage_unmap: not mapped");
		if (PTE_FLAGS(*pte) == PTE_V) panic("upage_unmap: not a leaf");
		if (free) {
			long pa = PTE2PA(*pte);
			kfree((void *)pa);
		}
		*pte = 0;
	}
}

pagetable_t upage_create(void) {
	pagetable_t pagetable = (pagetable_t )kalloc();
	if (!pagetable) return 0;
	memset(pagetable, 0, PGSIZE);
	return pagetable;
}
void upage_init(pagetable_t pagetable, unsigned char *src, unsigned int size) {
	char *mem;
	if (size >= PGSIZE)
		panic("upage_init: size error");
	mem = kalloc();	
	memset(mem, 0, PGSIZE);
	map(pagetable, 0, (long )mem, PGSIZE, PTE_W | PTE_R | PTE_X | PTE_U);
	mem_move(mem, src, size);


long upage_alloc(pagetable_t pagetable, long original, long new) {
	char *mem;
	long addr;

	if (new < original) return original;
	original = PGROUNDUP(original);
	for (addr = original; addr < new; addr += PGSIZE) {
		if (!(mem = kalloc())) {
			upage_dealloc(pagetable, addr, original);
			return 0;
		}
		memset(mem, 0, PGSIZE);
		if (map(pagetable, addr, (long )mem, PGSIZE, PTE_W | PTE_R | PTE_X | PTE_U)) {
			kfree(mem);
			upage_dealloc(pagetable, addr, original);
			return 0;
		}
	}
	return new;
}

long upage_dealloc(pagetable_t pagetable, long original, long new) {
	if (new >= original) return original;
	if (PGROUNDUP(new) < PGROUNDUP(original)) {
		int npages = (PGROUNDUP(original) - PGROUNDUP(new)) / PGSIZE;
		upage_unmap(pagetable, PGROUNDUP(new), npages, 1);
	}
	return new;
}

void freewalk(pagetable_t pagetable) {
	for (int i = 0; i < 512; ++i) {	/* 512 PTEs in a pagetable */
		pte_t pte = pagetable[i];
		if ((pte & PTE_V) && !(pte & PTE_W | PTE_R | PTE_X)) {	/* point to a lower pagetable */
			long child = PTE2PA(pte);
			freewalk((pagetable_t )child);
			pagetable[i] = 0;
		} else if (pte & PTE_V) {
			panic("freewalk: leaf");
		}
	}
	kfree((void *)pagetable);
}

void upage_free(pagetable_t pagetable, long size) {
	if (size > 0)
		upage_unmap(pagetable, 0, PGROUNDUP(size) / PGSIZE, 1);
	freewalk(pagetable);
}

int upage_copy(pagetable_t src, pagetable_t dest, long size) {
	pte_t *pte;
	long pa, i;
	unsigned int flags;
	char *mem;

	for (i = 0; i < size; i += PGSIZE) {
		if (!(pte = walk(src, i, 0)))
			panic("upage_copy: pte not exist");
		if (!(*pte & PTE_V))
			panic("upage_copy: page not valid");
		pa = PTE2PA(*pte);
		flags = PTE_FLAGS(*pte);
		if (!(mem = kaaloc())) goto err;
		mem_move(mem, (char *)pa, PGSIZE);
		if (map(dest, i, (long )mem, PGSIZE, flags)) {
			kfree(mem);
			goto err;
		}
	}
	return 0;
err:
	upage_unmap(dest, 0, i / PGSIZE, 1);
	return -1;
}

void upage_clear(pagetable_t pagetable, long va) {
	pte_t *pte = walk(pagetable, va, 0);
	if (!pte) panic("upage_clear");
	*pte &= ~PTE_U;
}

int copy_out(pagetable_t pagetable, long dest, char *src, long len) {
	long size, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(dest);
		if (!(pa0 = walkaddr(pagetable, va0)))
			return -1;
		size = PGSIZE - (dest - va0);
		if (size > len) size = len;
		mem_move((void *)(pa0 + (dest - va0)), src, size);

		len -= size;
		src += size;
		dest = va0 + PGSIZE;
	}
	return 0;
}

int copy_in(pagetable_t pagetable, char *dest, long src, long len) {
	long size, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(src);
		if (!(pa0 = walkaddr(pagetable, va0)))
			return -1;
		size = PGSIZE - (src - va0);
		if (size > len) size = len;
		mem_move(dest, (void *)(pa0 + (src - va0)), size);

		len -= size;
		dest += size;
		src = va0 + PGSIZE;
	}
	return 0;
}

int copy_in_str(pagetable_t pagetable, char *dest, long src, long max) {
	long size, va0, pa0;
	int null_flag = 0;

	while (!null_flag && max > 0) {
		va0 = PGROUNDDOWN(src);
		if (!(pa0 = walkaddr(pagetable, va0)))
			return -1;
		size = PGSIZE - (src - va0);
		if (size > max) size = max;

		char *p = (char *)(pa0 + (src - va0));
		while (size > 0) {
			if (*p == '\0') {
				*dest = '\0';
				null_flag = 1;
			} else {
				*dest = *p;
			}
			size--;
			max--;
			p++;
			dest++;
		}
		src = va0 + PGSIZE;
	}
	if (null_flag)
		return 0;
	else
		return -1;
}
