/* fs/file.c */
#include <asm.h>
#include <mem.h>
#include <kernel.h>
#include <fs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <proc.h>
#include <stat.h>
#include <file.h>

struct device device[NDEV];

struct {
	struct spinlock lock;
	struct file file[NFILE];
} f_table;

void file_init(void) {
	initlock(&f_table.lock, "f_table");
}

struct file *file_alloc(void) {
	struct file *fp;

	acquire(&f_table.lock);
	for (fp = f_table.file; fp < f_table.file + NFILE; ++fp) {
		if (fp->ref == 0) {
			fp->ref = 1;
			release(&f_table.file);
			return fp;
		}
	}
	release(&f_table.file);
	return 0;
}

struct file *file_dup(struct file *fp) {
	acquire(&f_table.lock);
	if (fp->ref < 1) panic("file_dup");
	fp->ref++;
	release(&f_table.lock);
	return fp;
}

void file_close(struct file *fp) {
	struct file f;

	acquire(&f_table.lock);
	if (fp->ref < 1) panic("file_close");
	if (--fp->ref > 0) {
		release(&f_table.lock);
		return;
	}

	f = *fp;
	fp->ref = 0;
	fp->type = FD_NONE;
	release(&f_table.lock);

	if (f.type == FD_PIPE)
		pipe_close(f.pipe, f.writable);
	else if (f.type == FD_INODE || f.type == FD_DEV) {
		begin_op();
		i_put(f.ip);
		end_op();
	}
}

int file_stat(struct file *fp, unsigned long addr) {
	struct proc *p = myproc();
	struct stat stat;

	if (fp->type == FD_INODE || fp->type == FD_DEV) {
		i_lock(fp->ip);
		stati(fp->ip, &stat);
		i_unlock(fp->ip);
		if (copy_out(p->pagetable, addr, (char *)&stat, sizeof(stat)) < 0) 
			return -1;
		return 0;
	}
	return -1;
}

int file_read(struct file *fp, unsigned long addr, int n) {
	int r = 0;
	
	if (!fp->readable) return -1;
	if (fp->type == FD_PIPE)
		r = pipe_read(fp->pipe, addr, n);
	else if (fp->type == FD_DEV) {
		if (fp->major < 0 || fp->major >= NDEV || !device[fp->major].read)
			return -1;
		r = device[fp->major].read(1, addr, n);
	} else if (fp->type == FD_INODE) {
		i_lock(fp->ip);
		if ((r = readi(fp->ip, 1, addr, fp->off, n)) > 0)
			fp->off += r;
		i_unlock(fp->ip);
	} else 
		panic("file_read");

	return r;
}

int file_write(struct file *fp, unsigned long addr, int n) {
	int tmp, w = 0;

	if (!fp->writable) return -1;
	if (fp->type == FD_PIPE)
		w = pipe_write(fp->pipe, addr, n);
	else if (fp->type == FD_DEV) {
		if (fp->major < 0 || fp->major >= NDEV || !device[fp->major].write)
			return -1;
		w = device[fp->major].write(1, addr, n);
	} else if (fp->type == FD_INODE) {
		/* write a few blocks to avoid exceeding the maximum */
		int max = ((MAXOPBLOCKS - 4) / 2) * BSIZE;
		int i = 0;
		while (i < n) {
			int num = n - i;
			if (num > max) num = max;

			begin_op();
			i_lock(fp->ip);
			if ((tmp = writei(fp->ip, 1, addr + i, fp->off, num)) > 0)
				fp->off += tmp;
			i_unlock(fp->ip);
			end_op();

			if (tmp != num)
				break;	/* error*/
			i += tmp;
		}
		w = (i == n? n: -1);
	} else
		panic("file_write");

	return w;
}
