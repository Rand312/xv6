// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void
binit(void)        //头插法将N个buf串起来
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf* bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);   //取bcache的锁

  // Is the block already cached? 要获取的磁盘块已缓存
  for(b = bcache.head.next; b != &bcache.head; b = b->next){ //双向循环链表，从前往后扫描
    if(b->dev == dev && b->blockno == blockno){  //如果设备和块号都对上，那么是要找的块
      b->refcnt++;                   //该块的引用加1
      release(&bcache.lock);         //释放bcache的锁
      acquiresleep(&b->lock);        //给该块加锁
      return b;         //返回该块
    }
  }
    
  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){     //该磁盘块没有缓存，从后往前扫描
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {            //找一个引用为0，且脏位也为0的空闲缓存块
      b->dev = dev;     //设备
      b->blockno = blockno;  //块号
      b->flags = 0;    //刚分配的缓存块，数据无效
      b->refcnt = 1;   //引用数为1
      release(&bcache.lock);     //释放bcache的锁
      acquiresleep(&b->lock);    //给该缓存块加锁
      return b;    //返回该缓存块
    }
  }
  panic("bget: no buffers");  //既没缓存该块，也没得空闲缓存块了，panic
}

// Return a locked buf with the contents of the indicated block.
struct buf* bread(uint dev, uint blockno)  //返回一个存在有效数据的缓存块      
{
  struct buf *b;

  b = bget(dev, blockno);    //获取一个缓存块，bget取了这个块的锁
  if((b->flags & B_VALID) == 0) {  //如果该块是临时分配的数据无效
    iderw(b);    //请求磁盘，读取数据
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)   //将缓存块写到相应磁盘块
{
  if(!holdingsleep(&b->lock))  //要写这个块，那说明已经拿到了这个块，所以肯定也拿到锁了
    panic("bwrite");
  b->flags |= B_DIRTY;      //设置脏位
  iderw(b);                 //请求磁盘写数据
}

// Release a locked buffer.
// Move to the head of the MRU list.
void brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);          //释放缓存块的锁
 
  acquire(&bcache.lock);           //获取bcache的锁
  b->refcnt--;                     //该块的引用减1
  if (b->refcnt == 0) {            //没有地方再引用这个块，将块链接到缓存区链头
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
    
  release(&bcache.lock);           //释放bache的锁
}
//PAGEBREAK!
// Blank page.

