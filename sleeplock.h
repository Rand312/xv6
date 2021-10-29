// Long-term locks for processes
struct sleeplock {
  uint locked;       // Is the lock held? 已锁？
  struct spinlock lk; // spinlock protecting this sleep lock 自旋锁
  
  // For debugging:
  char *name;        // Name of lock. 名字
  int pid;           // Process holding lock 哪个进程持有？
};

