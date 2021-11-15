// Per-CPU state
struct cpu {
  uchar apicid;                // LAPIC ID
  struct context *scheduler;   // 调度器的上下文
  struct taskstate ts;         // 任务状态段
  struct segdesc gdt[NSEGS];   // GDT
  volatile uint started;       // CPU是否已经启动
  int ncli;                    // 关中断深度
  int intena;                  // 该CPU在pushcli之前是否允许中断
  struct proc *proc;           // 运行在该CPU上的进程指针
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)进程大小
  pde_t* pgdir;                // Page table 页表
  char *kstack;                // Bottom of kernel stack for this process 内核栈位置
  enum procstate state;        // Process state 程序状态
  int pid;                     // Process ID 进程ID
  struct proc *parent;         // Parent process 父进程指针
  struct trapframe *tf;        // Trap frame for current syscall 中断栈帧指针
  struct context *context;     // swtch() here to run process 上下文指针
  void *chan;                  // If non-zero, sleeping on chan 用来睡眠
  int killed;                  // If non-zero, have been killed 是否被killed
  struct file *ofile[NOFILE];  // Open files 打开文件描述符表
  struct inode *cwd;           // Current directory 当前工作路径
  char name[16];               // Process name (debugging) 进程名字
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
