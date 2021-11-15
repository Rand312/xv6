//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

struct devsw devsw[NDEV];   //设备读写函数指针数组
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;   //文件表，100个槽，即最多打开100个文件

void
fileinit(void)     //初始化文件表的锁
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file* filealloc(void)    //分配一个文件结构体
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){   //如果该文件结构体的引用数为0则说明空闲可分配
      f->ref = 1;   //新分配的，引用数为1
      release(&ftable.lock);
      return f;   //返回文件结构体的指针
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)   //增加文件结构体的引用数
{
  acquire(&ftable.lock);  //取锁
  if(f->ref < 1)    //如果引用数小于1，panic
    panic("filedup");
  f->ref++;        //引用数++
  release(&ftable.lock);   //解锁
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)   //关闭文件
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)  
    panic("fileclose");
  if(--f->ref > 0){     //引用数减1
    release(&ftable.lock);
    return;
  }
  //如果引用数减为0了，回收文件结构体
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);
  
  if(ff.type == FD_PIPE)   //如果该文件是个管道，调用pipeclose来关闭
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){  //如果类型为FD_INODE
    begin_op();
    iput(ff.ip);  //释放该inode，iput里面检查该inode的链接数和引用数是否都为0，如果是则删除文件
    end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)  //获取inode信息，放进stat结构体
{
  if(f->type == FD_INODE){  //如果文件类型为“INODE”文件
    ilock(f->ip);   //取锁
    stati(f->ip, st);  //获取inode信息，放进stat结构体
    iunlock(f->ip); //解锁
    return 0;
  }
  return -1;
}

// Read from file f.
int fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)   //如果该文件不可读
    return -1;
  if(f->type == FD_PIPE)  //如果该文件是管道
    return piperead(f->pipe, addr, n);  //调用管道读的方法
  if(f->type == FD_INODE){  //如果是inode类型的文件
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0) //调用readi方法
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)   //如果是管道文件
    return pipewrite(f->pipe, addr, n);   //调用写管道的方法
  if(f->type == FD_INODE){ //如果是INODE文件
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

