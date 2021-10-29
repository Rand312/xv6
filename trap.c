#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void tvinit(void)   //根据外部的vectors数组构建中断门描述符
{
  int i;

  for(i = 0; i < 256; i++)  //循环256次构造门描述符
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);  
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}


void
idtinit(void)
{
  lidt(idt, sizeof(idt));      //加载IDT地址到IDTR
}

//PAGEBREAK: 41
void trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){    //系统调用
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();     //系统调用处理程序
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:   //时钟中断
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;        //滴答数加1
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();  //写EOI表中断结束
    break;
  case T_IRQ0 + IRQ_IDE:  //磁盘中断
    ideintr();    //磁盘中断处理程序
    lapiceoi();   //写EOI表中断结束
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:    //键盘中断
    kbdintr();      
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:  //串口中断
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:   //伪中断
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER) //如果被killed
     exit();  //退出

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&  //发生了时钟中断
     tf->trapno == T_IRQ0+IRQ_TIMER)
     yield();  //主动让出CPU

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER) //再次检查如果被killed
     exit();  //退出
}
