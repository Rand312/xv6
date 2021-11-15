#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  struct spinlock lock;  //管道锁
  char data[PIPESIZE];   //管道的内存数据区
  uint nread;     // number of bytes read  读多少字节
  uint nwrite;    // number of bytes written 写多少字节
  int readopen;   // read fd is still open  读端仍然打开
  int writeopen;  // write fd is still open 写端仍然打开
};

int
pipealloc(struct file **f0, struct file **f1)   //创建管道
{
  struct pipe *p;

  p = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)  //分配文件结构体
    goto bad;
  if((p = (struct pipe*)kalloc()) == 0)   //分配管道内存区
    goto bad;
  p->readopen = 1;     //读端打开
  p->writeopen = 1;    //写端打开
  p->nwrite = 0;       //写字节数初始为0
  p->nread = 0;        //读字节数初始为0
  initlock(&p->lock, "pipe");  //初始化锁

  (*f0)->type = FD_PIPE;   //管道文件
  (*f0)->readable = 1;     //可读
  (*f0)->writable = 0;     //不可写
  (*f0)->pipe = p;         //指向的管道文件为上述创建的p
  (*f1)->type = FD_PIPE;   //管道文件
  (*f1)->readable = 0;     //可读
  (*f1)->writable = 1;     //可写
  (*f1)->pipe = p;         //指向的管道文件为上述创建的p
  return 0;            //返回0表创建成功

//PAGEBREAK: 20
 bad:         //如果发生错误
  if(p)  //如果已经分配了内存区
    kfree((char*)p);   //释放
  if(*f0)   //如果已分配了文件结构体1
    fileclose(*f0);    //释放
  if(*f1)   //如果已分配了文件结构体2
    fileclose(*f1);    //释放
  return -1;   //返回-1表创建管道错误
}

void
pipeclose(struct pipe *p, int writable)   //关闭管道
{
  acquire(&p->lock);     //取锁
  if(writable){     //如果是关闭写端
    p->writeopen = 0;    //写端关闭
    wakeup(&p->nread);   //唤醒读端
  } else {          //如果是关闭读端
    p->readopen = 0;     //读端关闭
    wakeup(&p->nwrite);  //唤醒写端
  }
  if(p->readopen == 0 && p->writeopen == 0){   //如果读端和写端都关闭了
    release(&p->lock);   //解锁
    kfree((char*)p);     //释放管道的内存数据区
  } else
    release(&p->lock);   //否则也要解锁再退出
}

//PAGEBREAK: 40
int pipewrite(struct pipe *p, char *addr, int n)
{
  int i;

  acquire(&p->lock);
  for(i = 0; i < n; i++){
    while(p->nwrite == p->nread + PIPESIZE){  //管道已经满了需要读
      if(p->readopen == 0 || myproc()->killed){  //读端被关闭或者进程被killed
        release(&p->lock);   //释放锁
        return -1;           //返回-1表错误
      }
      wakeup(&p->nread);    //唤醒读进程
      sleep(&p->nwrite, &p->lock);  //写进程睡眠
    }
    p->data[p->nwrite++ % PIPESIZE] = addr[i];   //写，即数据放入缓存区
  }
  wakeup(&p->nread);  //写了之后唤醒读进程
  release(&p->lock);  //释放锁
  return n;    //返回写了多少字节
}

int piperead(struct pipe *p, char *addr, int n)
{
  int i;

  acquire(&p->lock);    //取锁
  while(p->nread == p->nwrite && p->writeopen){  //管道空
    if(myproc()->killed){       //如果进程被killed
      release(&p->lock);        //释放锁
      return -1;         //返回-1表错误
    }
    sleep(&p->nread, &p->lock); //读进程休眠
  }
  for(i = 0; i < n; i++){  //循环n次
    if(p->nread == p->nwrite)   //数据已经读完，管道空了
      break;
    addr[i] = p->data[p->nread++ % PIPESIZE];  //读操作，搬运数据的一个过程
  }
  wakeup(&p->nwrite);  //读完之后唤醒写进程
  release(&p->lock);   //释放锁
  return i;    //返回读取的字节数
}