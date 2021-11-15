#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();  
}

int
sys_exit(void)
{
  exit();  
  return 0;  // not reached不会执行到这儿
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)   //去用户栈去参数:进程号
    return -1;
  return kill(pid);   //杀死这个进程
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)  //获取参数:要分配的空间大小
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)  //调用growproc将进程的大小增加n字节
    return -1;
  return addr;   //返回增加的那部分空间的首地址
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)   //取参数:要休眠的滴答数
    return -1;
  acquire(&tickslock);  //取锁
  ticks0 = ticks;      //记录当前滴答数
  while(ticks - ticks0 < n){  //当过去的滴答数小于要休眠的滴答数
    if(myproc()->killed){   //如果该进程被killed
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);  //休眠
  }
  release(&tickslock);  //解锁
  return 0;   //返回
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);   //取滴答数的锁
  xticks = ticks;      //当前滴答数
  release(&tickslock);   //解锁
  return xticks;    //返回当前滴答数
}
