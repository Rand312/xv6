struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;  //文件类型
  int ref; // reference count  //引用数
  char readable;   //可读？
  char writable;   //可写？
  struct pipe *pipe;  //该文件是个管道
  struct inode *ip;   //该文件是个“inode”型文件
  uint off;     //读写文件时记录的偏移量
};


// in-memory copy of an inode
struct inode {
  uint dev;           // Device numbern 设备号(实际表示磁盘的主从)
  uint inum;          // Inode number  inode 编号
  int ref;            // Reference count  引用数
  struct sleeplock lock; // protects everything below here 休眠锁
  int valid;          // inode has been read from disk?  数据有效？

  short type;         // copy of disk inode 文件类型
  short major;        //major number
  short minor;        //minor number
  short nlink;        //硬链接数
  uint size;          //文件大小
  uint addrs[NDIRECT+1];   //数据块索引
};

// table mapping major device number to
// device functions
struct devsw {   //设备读写函数指针
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
