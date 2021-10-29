// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held? 该锁是否被锁上了

  // For debugging:
  char *name;        // Name of lock. 名字
  struct cpu *cpu;   // The cpu holding the lock. 哪个CPU取了锁
  uint pcs[10];      // The call stack (an array of program counters)
                     // that locked the lock. 调用栈信息
};

