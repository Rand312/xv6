struct buf {
  int flags;      //表示该块是否脏是否有效
  uint dev;       //该缓存块缓存的是主盘还是从盘
  uint blockno;   //块号
  struct sleeplock lock;   //休眠锁
  uint refcnt;    //引用该块的个数
  struct buf *prev;  //该块的前一个块
  struct buf *next;  //该块的后一个块
  struct buf *qnext; //下一个磁盘队列块
  uchar data[BSIZE];  //缓存的数据
};
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk

