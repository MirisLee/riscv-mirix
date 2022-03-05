/* fs/buf.c */
#include <asm.h>
#include <kernel.h>
#include <fs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <buf.h>

struct {
	struct spinlock lock;
	struct buf buf[NBUF];	/* whole buffer array */
	struct buf b_head;	/* used buffer list */
} b_cache;

void b_init(void) {
	struct buf *bp;

	initlock(&b_cache.lock, "b_cache");
	/* create list of buffers */
	b_cache.b_head.prev = &b_cache.b_head;
	b_cache.b_head.next = &b_cache.b_head;
	for (bp = b_cache.buf; bp < b_cache.buf + NBUF; ++bp) {
		bp->next = b_cache.b_head.next;
		bp->prev = &b_cache.b_head;
		initsleeplock(&bp->lock, "buffer");
		b_cache.b_head.next->prev = bp;
		b_cache.b_head.next = bp;
	}
}

static struct buf *b_get(unsigned int dev, unsigned int b_no) {
	struct buf *bp;
	acquire(&b_cache.lock);

	/* check if the block is already cached */
	for (bp = b_cache.b_head.next; bp != &b_cache.b_head; bp = bp->next) {
		if (bp->dev == dev && bp->b_no == b_no) {
			bp->refcnt++;
			release(&b_cache.lock);
			acquiresleep(&bp->lock);
			return bp;
		}
	}

	/* recycle the least recently used buffer */
	for (bp = b_cache.b_head.prev; bp != &b_cache.b_head; bp = bp->prev) {
		if (bp->refcnt == 0) {
			bp->dev = dev;
			bp->b_no = b_no;
			bp->valid = 0;
			bp->refcnt = 1;
			release(&b_cache.lock);
			acquiresleep(&bp->lock);
			return bp;
		}
	}

	panic("b_get: no buffer");
}

struct buf *b_read(unsigned int dev, unsigned int b_no) {
	struct buf *bp = b_get(dev, b_no);
	if (!bp->valid) {
		virt_io_disk_rw(bp, 0);
		bp->valid = 1;
	}
	return bp;
}

void b_write(struct buf *bp) {
	if (!holdingsleep(&bp->lock))
		panic("b_write");
	virtio_disk_rw(bp, 1);
}

void b_relse(struct buf *bp) {
	if (!holdingsleep(&bp->lock))
		panic("b_relse");
	releasesleep(&bp->lock);

	acquire(&b_cache.lock);
	bp->refcnt--;
	if (bp->refcnt == 0) {
		bp->next->prev = bp->prev;
		bp->prev->next = bp->next;
		bp->next = b_cache.b_head.next;
		bp->prev = &b_cache.b_head;
		b_cache.b_head.next->prev = bp;
		b_cache.b_head.next = bp;
	}
	release(&b_cache.lock);	
}

void b_pin(struct buf *bp) {
	acquire(&b_cache.lock);
	bp->refcnt++;
	release(&b_cache.lock);
}

void b_unpin(struct buf *bp) {
	acquire(&b_cache.lock);
	bp->refcnt--;
	release(&b_cache.lock);
}
