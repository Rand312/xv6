#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

// convert to intel byte order 转化为小端
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

//./mkfs fs.img README $(UPROGS)

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){   //参数小于两个
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);    //退出
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);  //BSIZE是否是dinode大小整数倍
  assert((BSIZE % sizeof(struct dirent)) == 0);  //BSIZE是否是dirent大小整数倍

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666); //打开磁盘文件
  if(fsfd < 0){  //如果打开失败
    perror(argv[1]);
    exit(1);
  }

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap;  //元数据大小
  nblocks = FSSIZE - nmeta;    //数据区大小

  sb.size = xint(FSSIZE);    //文件系统大小
  sb.nblocks = xint(nblocks);   //数据区大小
  sb.ninodes = xint(NINODES);   //inode个数
  sb.nlog = xint(nlog);   //日志区大小
  sb.logstart = xint(2);  //日志区起始位置
  sb.inodestart = xint(2+nlog);  //inode区起始位置
  sb.bmapstart = xint(2+nlog+ninodeblocks);  //位图区起始位置

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     //第一个能够分配的数据块

  for(i = 0; i < FSSIZE; i++)  //磁盘清零
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));   //buf清0
  memmove(buf, &sb, sizeof(sb)); //移动超级块信息到buf
  wsect(1, buf);    //将buf写到第一个扇区(第0个扇区是引导快)

  rootino = ialloc(T_DIR);     //分配一个inode指向根目录文件
  assert(rootino == ROOTINO);  //根目录的inode编号是否为 1

  bzero(&de, sizeof(de));     //目录项de清0
  de.inum = xshort(rootino);  //根目录inode编号
  strcpy(de.name, ".");       //当前目录 . 目录项
  iappend(rootino, &de, sizeof(de));  //向根目录文件末尾写目录项

  bzero(&de, sizeof(de));     //目录项de清0
  de.inum = xshort(rootino);  //根目录inode编号
  strcpy(de.name, "..");      //父目录 .. 目录项
  iappend(rootino, &de, sizeof(de));  //向根目录文件末尾写目录项

  for(i = 2; i < argc; i++){  //从第2个参数可执行文件开始循环(0开始)
    assert(index(argv[i], '/') == 0);  //原本应是想判断该文件是否在根目录下

    if((fd = open(argv[i], 0)) < 0){   //打开该文件
      perror(argv[i]);
      exit(1);
    }

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')   //如果该文件名以_开头，跳过
      ++argv[i];

    inum = ialloc(T_FILE);  //分配一个普通文件类型的inode

    bzero(&de, sizeof(de));  //目录项清 0
    de.inum = xshort(inum);  //inode编号
    strncpy(de.name, argv[i], DIRSIZ);  //copy文件名
    iappend(rootino, &de, sizeof(de));  //将该目录项添加到根目录文件末尾

    while((cc = read(fd, buf, sizeof(buf))) > 0)  //读取参数文件数据到buf
      iappend(inum, buf, cc);   //将buf写进inode指向的数据区

    close(fd);   //关闭该参数文件
  }

  // fix size of root inode dir
  rinode(rootino, &din);  //读取根目录inode
  off = xint(din.size);   //原根目录大小
  off = ((off/BSIZE) + 1) * BSIZE;  //新根目录大小，直接取整??
  din.size = xint(off);   
  winode(rootino, &din);  //写回磁盘

  balloc(freeblock);   //将分配出去的块相应的位图置1

  exit(0);  //退出
}

void
wsect(uint sec, void *buf)   //写“扇区”
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

void winode(uint inum, struct dinode *ip)  //写“inode”
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);  //编号为inum的inode在哪个扇区
  rsect(bn, buf);  //读取这个扇区的数据到buf
  dip = ((struct dinode*)buf) + (inum % IPB); //inode的位置
  *dip = *ip;     //结构体赋值
  wsect(bn, buf); //写回磁盘(文件)
}

void
rinode(uint inum, struct dinode *ip)  //读“inode”
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);  //编号为inum的inode在哪个扇区
  rsect(bn, buf);    //读取这个扇区的数据到buf
  dip = ((struct dinode*)buf) + (inum % IPB); //inode的位置
  *ip = *dip;  //结构体赋值
}

void
rsect(uint sec, void *buf)  //读“扇区”
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)   //分配“inode”
{
  uint inum = freeinode++;   //分配inode编号
  struct dinode din;

  bzero(&din, sizeof(din));  //清0
  din.type = xshort(type);  //文件类型
  din.nlink = xshort(1);    //硬连接数1
  din.size = xint(0);    //文件大小0
  winode(inum, &din);   //写到磁盘
  return inum;   //返回inode编号
}

void
balloc(int used)  //将used前面所有的块的位图置 1 
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);   //清 0
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));  //位图相应位置1表分配出去
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);  //写回磁盘
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)  //将xp指向的数据写到inum指向的文件末尾，写n个字节
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  /***获取 inum 指向的文件最后一个数据块的位置(块号)***/
  rinode(inum, &din);   //读取第inum个inode
  off = xint(din.size);  //文件大小，字节数
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){    
    fbn = off / BSIZE;  //文件大小，块数，向下取整了，所以不是实际的块数
    assert(fbn < MAXFILE);  //不能超过支持的最大文件
    if(fbn < NDIRECT){   //如果在直接索引范围类
      if(xint(din.addrs[fbn]) == 0){  //如果未分配该数据块
        din.addrs[fbn] = xint(freeblock++);  //分配
      }
      x = xint(din.addrs[fbn]);  //记录该块的地址(块号)
    } else {    //如果超出直接索引范围，在间接索引范围内
      if(xint(din.addrs[NDIRECT]) == 0){    //如果间接索引块未分配
        din.addrs[NDIRECT] = xint(freeblock++);  //分配
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);  //读取间接索引块
      if(indirect[fbn - NDIRECT] == 0){   //若间接索引块中的条目指向的数据块未分配
        indirect[fbn - NDIRECT] = xint(freeblock++);   //分配
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);  //同步到磁盘
      }
      x = xint(indirect[fbn-NDIRECT]);   //记录该数据块的地址(块号)
    }
    /**计算要写的字节数***/
    n1 = min(n, (fbn + 1) * BSIZE - off);  //计算一次性最多写的字节数
    rsect(x, buf);    //读取x扇区
    bcopy(p, buf + off - (fbn * BSIZE), n1);  //计算从哪开始写，bcopy(src, dest, size)
    wsect(x, buf);   //写回x扇区
    n -= n1;     //更新还要写的字节数
    off += n1;   //更新dest位置(文件大小)
    p += n1;     //更新src位置
  }
  din.size = xint(off);   //更新inode的文件大小属性
  winode(inum, &din);   //写回inode
}
