// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define SECTOR_SIZE   512
#define IDE_BSY       0x80    //状态寄存器的第7位表硬盘是否繁忙
#define IDE_DRDY      0x40    //状态寄存器的第6位表硬盘是否就绪，可继续执行命令
#define IDE_DF        0x20    //driver write fault,写错误
#define IDE_ERR       0x01    //第0位，表是否出错

#define IDE_CMD_READ  0x20    //读扇区命令
#define IDE_CMD_WRITE 0x30    //写扇区命令
#define IDE_CMD_RDMUL 0xc4    //read multiple sectors 读多个扇区
#define IDE_CMD_WRMUL 0xc5    //write multiple sectors 写多个扇区

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;
static struct buf *idequeue;

static int havedisk1;
static void idestart(struct buf*);

// Wait for IDE disk to become ready.
static int
idewait(int checkerr)
{
  int r;

  while(((r = inb(0x1f7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)    //从端口0x1f7读出状态，若硬盘忙，空循环等待
    ;
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)    //检查错误
    return -1;
  return 0;
}

void
ideinit(void)   //磁盘初始化
{
  int i;

  initlock(&idelock, "ide");
  ioapicenable(IRQ_IDE, ncpu - 1);     //让这个CPU来处理硬盘中断
  idewait(0);           //等磁盘就绪，不过以0来调用似乎错误检查就不起作用了，猜测是为了更快返回

  // Check if disk 1 is present
  outb(0x1f6, 0xe0 | (1<<4));     //将硬盘device寄存器高4位设置为1111，表示从盘，寻址模式为LBA

  for(i=0; i<1000; i++){      //指定为从盘后，循环读取状态来判断是否有从盘
    if(inb(0x1f7) != 0){
      havedisk1 = 1;
      break;
    }
  }

  // Switch back to disk 0.
  outb(0x1f6, 0xe0 | (0<<4));     //将第4位置0表切换成主盘
}

// Start the request for b.  Caller must hold idelock.
static void idestart(struct buf *b)
{
  if(b == 0)   
    panic("idestart");
  if(b->blockno >= FSSIZE)   //块号超过了文件系统支持的块数
    panic("incorrect blockno");
  int sector_per_block =  BSIZE/SECTOR_SIZE;   //每块的扇区数
  int sector = b->blockno * sector_per_block;   //扇区数
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;  //一个块包含多个扇区的话就用读多个块的命令
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL; //一个块包含多个扇区的话就用写多个块的命令

  if (sector_per_block > 7) panic("idestart");   //每个块不能大于7个扇区

  idewait(0);     //等待磁盘就绪
  outb(0x3f6, 0);  //用来产生磁盘中断，详见前面0x3f6寄存器

  outb(0x1f2, sector_per_block);  // 读取几个扇区
  /*像0x1f3~6写入扇区地址*/
  outb(0x1f3, sector & 0xff);             //LBA地址 低8位
  outb(0x1f4, (sector >> 8) & 0xff);      //LAB地址 中8位
  outb(0x1f5, (sector >> 16) & 0xff);     //LBA地址 高8位
  outb(0x1f6, 0xe0 | ((b->dev&1)<<4) | ((sector>>24)&0x0f));  //LBA地址最高的4位，(b->dev&1)<<4来选择读写主盘还是从盘

  if(b->flags & B_DIRTY){       //表示数据脏了，需要写到磁盘去了
    outb(0x1f7, write_cmd);     //向0x1f7发送写命令
    outsl(0x1f0, b->data, BSIZE/4);   //向磁盘写数据
  } else {
    outb(0x1f7, read_cmd);     //否则发送读命令，但没有读
  }
}

// Interrupt handler.
void ideintr(void)
{
  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);    //取锁

  if((b = idequeue) == 0){   //如果磁盘请求队列为空
    release(&idelock);
    return;
  }
  idequeue = b->qnext;   //磁盘请求队列链首向后移

  // Read data if needed.
  if(!(b->flags & B_DIRTY) && idewait(1) >= 0)  //如果此次请求磁盘的操作是读且磁盘已经就绪
    insl(0x1f0, b->data, BSIZE/4);   //从0x1f0端口读取数据到b->data

  // Wake process waiting for this buf.
  b->flags |= B_VALID;    //此时缓存块数据有效
  b->flags &= ~B_DIRTY;   //此时缓存块数据不脏
  wakeup(b);    //唤醒等待在缓存块b上的进程

  // Start disk on next buf in queue.
  if(idequeue != 0)   //此时队列还不空，则处理下一个
    idestart(idequeue);  

  release(&idelock);   //释放锁
}

//PAGEBREAK!
// Sync buf with disk.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void iderw(struct buf *b)
{
  struct buf **pp;

  if(!holdingsleep(&b->lock))   //要同步该块到磁盘，那前面应该是已经拿到了这个块的锁
    panic("iderw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)  //这个缓存块既不脏数据又有效的话，则无事可做
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)   //这个缓存块缓存的不是任一设备的数据
    panic("iderw: ide disk 1 not present");

  acquire(&idelock);  //DOC:acquire-lock

  // 将这个块放进请求队列
  b->qnext = 0;  
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;

  // 如果请求队列为空，当前块是唯一请求磁盘的块，则可以马上进行磁盘操作
  if(idequeue == b)
    idestart(b);

  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){  //数据无效，进程休眠
    sleep(b, &idelock);
  }

  release(&idelock);  //释放锁
}
