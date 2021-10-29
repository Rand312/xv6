// Mutual exclusion spin locks.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

void initlock(struct spinlock *lk, char *name)  //初始化锁 lk
{
  lk->name = name;   //初始化该锁的名字
  lk->locked = 0;    //初始化该锁空闲
  lk->cpu = 0;       //初始化持有该锁的CPU为空
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
// Holding a lock for a long time may cause
// other CPUs to waste time spinning to acquire it.
void
acquire(struct spinlock *lk)
{
  pushcli(); // disable interrupts to avoid deadlock.
  if(holding(lk))   // 如果已经取了锁
    panic("acquire");

  // The xchg is atomic.
  while(xchg(&lk->locked, 1) != 0)   //原子赋值
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen after the lock is acquired.
   __sync_synchronize();   //发出一个full barrier

  // Record info about lock acquisition for debugging.
  // 调试信息
  lk->cpu = mycpu();     //记录当前取锁的CPU
  getcallerpcs(&lk, lk->pcs);   //获取调用栈信息
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->pcs[0] = 0;  //清除调试信息
  lk->cpu = 0;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other cores before the lock is released.
  // Both the C compiler and the hardware may re-order loads and
  // stores; __sync_synchronize() tells them both not to.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code can't use a C assignment, since it might
  // not be atomic. A real OS would use C atomics here.
  asm volatile("movl $0, %0" : "+m" (lk->locked) : ); //lk->locked=0

  popcli();
}

// Record the current call stack in pcs[] by following the %ebp chain.
void getcallerpcs(void *v, uint pcs[])
{
  uint *ebp;
  int i;

  ebp = (uint*)v - 2;  //getcallerpcs的调用者的调用者的ebp地址
  for(i = 0; i < 10; i++){
    if(ebp == 0 || ebp < (uint*)KERNBASE || ebp == (uint*)0xffffffff) //停止条件
      break;
    pcs[i] = ebp[1];     // saved %eip
    ebp = (uint*)ebp[0]; // saved %ebp
  }
  for(; i < 10; i++)
    pcs[i] = 0;
}

// Check whether this cpu is holding the lock.
int holding(struct spinlock *lock)
{
  int r;
  pushcli();   
  r = lock->locked && lock->cpu == mycpu(); //检验锁lock是否被某CPU锁持有且上锁
  popcli();
  return r;
}


// Pushcli/popcli are like cli/sti except that they are matched:
// it takes two popcli to undo two pushcli.  Also, if interrupts
// are off, then pushcli, popcli leaves them off.

void pushcli(void)
{
  int eflags;
  eflags = readeflags();   //cli之前读取eflags寄存器
    
  cli();    //关中断
  if(mycpu()->ncli == 0)   //第一次pushcli()
    mycpu()->intena = eflags & FL_IF;  //记录cli之前的中断状态
  mycpu()->ncli += 1;      //关中断次数(深度)加1
}

void popcli(void)
{
  if(readeflags()&FL_IF)   //如果eflags寄存器IF位为1
    panic("popcli - interruptible");
  if(--mycpu()->ncli < 0)   //如果计数小于0
    panic("popcli");
  if(mycpu()->ncli == 0 && mycpu()->intena)  //关中断次数为0时即开中断
    sti();
}

