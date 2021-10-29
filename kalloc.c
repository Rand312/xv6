// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;    //指向下一个空闲物理页
};

struct {
  struct spinlock lock;   //自旋锁
  int use_lock;           //现下是否使用锁？
  struct run *freelist;   //空闲链表头
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.

void kinit1(void *vstart, void *vend)    //kinit1(end, P2V(4*1024*1024));
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void kinit2(void *vstart, void *vend)   //kinit2(P2V(4*1024*1024), P2V(PHYSTOP));
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void freerange(void *vstart, void *vend)   //连续释放vstart到vend之间的页
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(char *v)   //释放页v
{
  struct run *r;
  //这个页应该在这些范围内且边界为4K的倍数
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP) 
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);  //将这个页填充无用信息，全置为1

  if(kmem.use_lock)        //如果使用了锁，取锁
    acquire(&kmem.lock);
  r = (struct run*)v;       //头插法将这个页放在链头
  r->next = kmem.freelist;  //当前页指向链头
  kmem.freelist = r;        //链头移到当前页
  if(kmem.use_lock)         //如果使用了锁，解锁
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char* kalloc(void)
{
  struct run *r;       //声明run结构体指针

  if(kmem.use_lock)    //如果使用了锁，取锁
    acquire(&kmem.lock);
  r = kmem.freelist;      //第一个空闲页地址赋给r
  if(r)
    kmem.freelist = r->next;  //链头移动到下一页，相当于把链头给分配出去了
  if(kmem.use_lock)    //如果使用了锁，解锁
    release(&kmem.lock);
  return (char*)r;    //返回第一个空闲页的地址
}

