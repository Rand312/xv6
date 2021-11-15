#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)   //初始化进程表，就是初始化进程表的锁
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {     //返回CPU ID，与数组元素减去数组首地址得到索引一个道理
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)   //获取当前CPU
{
  int apicid, i;
  
  if(readeflags()&FL_IF)   //检查是否处于关中断的状态
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();   //获取当前CPU的APIC ID，也就是CPU ID
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  //APIC ID不能够保证一定是连续的，但在这xv6中根据ioapicenable函数推测还有实际测试，
  //APIC ID，CPU数据的索引(CPU ID) 是一样的
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)  //比对哪个CPU结构体的ID是上述ID，返回其地址
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
//获取当前CPU上运行的进程
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();    //关中断
  c = mycpu();  //获取当前CPU
  p = c->proc;  //运行在当前CPU上面的进程
  popcli();     //popcli
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

    /*从头至尾依次寻找空间任务结构体*/
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) 
      if(p->state == UNUSED)
          goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;   //设置状态为EMBRYO
  p->pid = nextpid++;  //设置进程号

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){  //分配内核栈
      p->state = UNUSED;    //如果分配失败，回收该任务结构体后返回
      return 0;
  }
  sp = p->kstack + KSTACKSIZE;  //栈顶

  // Leave room for trap frame. 为中断栈帧预留空间
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret. 将中断返回程序的地址放进去
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  //这一步模拟内核态上下文的内容，eip(返回地址) 填写为forkret函数地址
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);   //切换级上下文置0
  p->context->eip = (uint)forkret;    //切换级上下文的eip字段，相当于返回地址，上CPU时执行的第一个函数

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();   //分配任务结构体，预留上下文空间
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)     //建立页表的内核部分
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size); //初始化虚拟地址空间，将initcode程序装进去
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;  //用户代码段选择子
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;  //用户数据段选择子
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;   //允许中断
  p->tf->esp = PGSIZE;   //初始化栈顶
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;    //可上CPU运行了

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){   
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)  //调用allocuvm增长进程空间
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0) //调用deallocuvm减少进程空间
      return -1;
  }
  curproc->sz = sz;    //更新当前进程大小
  switchuvm(curproc);  //重新加载当前进程的用户空间
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){  //分配初始化进程结构体和内核栈
    return -1;
  }

  // Copy process state from proc. 从父进程复制各种数据信息
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){ 
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;   //用户部分的大小
  np->parent = curproc;   //子进程的父进程是当前进程
  *np->tf = *curproc->tf; //子进程的栈帧就是父进程的栈帧

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;   //将中断栈帧的eax值修改为0

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));  //复制进程名字

  pid = np->pid;    //进程号

  acquire(&ptable.lock);
  np->state = RUNNABLE;    //子进程可以跑了!!!
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();  //当前进程
  struct proc *p;
  int fd;

  if(curproc == initproc)   //init进程是不能退出的，有特殊用处
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){  //关闭所有文件
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);  //放下当前工作路径的inode
  end_op();
  curproc->cwd = 0;  //当前工作路径设为0表空

  acquire(&ptable.lock);  //取锁

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);   //唤醒父进程

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){     //将被遗弃的孩子过继给init进程
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE) //如果其中有处于僵尸状态的进程，则唤醒init进程让其立即处理
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;  //状态变为僵尸状态
  sched();    //调度，永远不会返回
  panic("zombie exit");   //因为不会返回，正常情况是不可能执行到这的
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){    //"无限循环"
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ //循环寻找子进程
      if(p->parent != curproc)  //如果进程p的父进程不是当前进程
        continue;
      havekids = 1;   //当前进程有子进程
      if(p->state == ZOMBIE){  //如果子进程的状态是ZOMBIE，回收它的资源
        // Found one.
        pid = p->pid;      
        kfree(p->kstack);  //回收内核栈
        p->kstack = 0;
        freevm(p->pgdir);  //回收用户空间以及页表占用的五ii内存
        p->pid = 0;   
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED; //状态变为UNUSED,表该结构体空闲了
        release(&ptable.lock);  //释放锁
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){ //如果当前进程没有子进程或者当前进程被杀死了了,解锁
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    //如果当前进程有子进程，但是子进程还没有退出，父进程休眠等待子进程退出
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();    //允许中断

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);  //取锁
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){  //循环找一个RUNNABLE进程
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;   //此CPU准备运行p
      switchuvm(p);  //切换p进程页表
      p->state = RUNNING;  //设置状态为RUNNING

      swtch(&(c->scheduler), p->context);  //切换进程
      switchkvm();   //回来之后切换成内核页表

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;   //上个进程下CPU，此时CPU上没进程运行
    }
    release(&ptable.lock);   //释放锁

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)   //让出CPU，重新调度
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))   
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)       
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)   //主动让出CPU
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);  //解锁

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);  //初始化inode
    initlog(ROOTDEV); //初始化日志
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
	p->chan = chan;    //休眠在chan上
  p->state = SLEEPING;  //状态更改为SLEEPING

  sched();  //让出CPU调度
  // Tidy up.
  p->chan = 0;  //休眠对象改为0

  // Reacquire original lock.
  if(lk != &ptable.lock){  //如果lk不是ptable.lock
    release(&ptable.lock); //释放ptable.lock
    acquire(lk);   //重新获取lk锁
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)  //寻找休眠状态且休眠在chan上的进程
      p->state = RUNNABLE;   //将其状态更改为RUNNABLE
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);  //取锁
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ //循环寻找pid号进程
    if(p->pid == pid){  //找到了
      p->killed = 1;   //killed置1
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)  //如果该进程在睡瞌睡
        p->state = RUNNABLE;   //唤醒
      release(&ptable.lock);  //放锁
      return 0;   //返回正确
    }
  }
  release(&ptable.lock);  //放锁
  return -1;   //返回错误
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)  //获取每个进程的状态，栈帧情况
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
