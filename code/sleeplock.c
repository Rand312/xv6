// Sleeping locks

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");   //初始化自旋锁
  lk->name = name;   //名字
  lk->locked = 0;    //初始化未上锁
  lk->pid = 0;       //初始化没有进程取该锁
}

void acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);        //优先级:->高于&
    
  while (lk->locked) {    //当锁已被其他进程取走
    sleep(lk, &lk->lk);   //休眠
  }
  lk->locked = 1;        //上锁
  lk->pid = myproc()->pid;  //取锁进程的pid
    
  release(&lk->lk);       
}

void releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);  //取自旋锁
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);        //唤醒
  release(&lk->lk);
}

int
holdingsleep(struct sleeplock *lk)   //是否有进程取得了该锁
{
  int r;
  
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}



