// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 

// Read the super block.
void readsb(int dev, struct superblock *sb)  //读超级块
{
  struct buf *bp;

  bp = bread(dev, 1);  //读取超级块数据到缓存块
  memmove(sb, bp->data, sizeof(*sb));  //移动数据
  brelse(bp);   //释放缓存块
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);          //指定设备和块号。然后调用bread来读取
  memset(bp->data, 0, BSIZE);    //置零
  log_write(bp);
  brelse(bp);              //同上，释放锁
}

// Blocks.

// Allocate a zeroed disk block.
static uint balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));       //读取位图信息
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);   
      if((bp->data[bi/8] & m) == 0){  // Is block free?  如果该块空闲
        bp->data[bi/8] |= m;  // Mark block in use.   标记该块使用
        log_write(bp);      
        brelse(bp);           //释放锁
        bzero(dev, b + bi);   //将该块置0
        return b + bi;     //返回块号
      }
    }
    brelse(bp);    //释放锁
  }
  panic("balloc: out of blocks");
}
// Free a disk block.
static void bfree(int dev, uint b) //释放一个数据块，相应位图清零
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));  //读取位图信息
  bi = b % BPB; 
  m = 1 << (bi % 8);  //MASK
  if((bp->data[bi/8] & m) == 0)  //如果该块本来就是空闲的
    panic("freeing free block"); //panic
  bp->data[bi/8] &= ~m;  //置0
  log_write(bp);    //更新到日志区
  brelse(bp);     //释放该块
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;         //磁盘上i结点在内存中的缓存

void
iinit(int dev)
{
  int i = 0;
  
  initlock(&icache.lock, "icache");    //初始化i结点缓存的锁
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");   //初始化各个i结点的锁
  }

  readsb(dev, &sb);       //读取超级块然后打印消息
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode* ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){  
    bp = bread(dev, IBLOCK(inum, sb));      //读取第inum个i结点所在的位置
    dip = (struct dinode*)bp->data + inum%IPB;    //该i结点地址
    if(dip->type == 0){  // a free inode   找到空闲i结点
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);   //分配内存inode，以内存中的inode形式返回
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));       //读取磁盘上的inode块
  dip = (struct dinode*)bp->data + ip->inum%IPB;   //找到相应的inode项
  dip->type = ip->type;                            //update
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);   //写到日志区
  brelse(bp);  //释放该块
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode* iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached? 如果该dinode在内存中已缓存
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){    //在缓存中找到该i结点
      ip->ref++;                   //引用加1
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.  记录icache中空闲的inode
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");
  //该dinode在内存中没有缓存，分配一个空闲inode empty
  //根据参数，初始化该空闲inode，还没读入数据，valid设为0
  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode* idup(struct inode *ip) //i结点引用加1
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)    //空指针引用小于1都是错误的
    panic("ilock");

  acquiresleep(&ip->lock);   //上锁

  if(ip->valid == 0){     //有效位为0，从磁盘读入数据
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);     //释放锁
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void iput(struct inode *ip)
{
  acquiresleep(&ip->lock);      //取锁
  if(ip->valid && ip->nlink == 0){   
    //获取该i结点的引用数
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);

    if(r == 1){   //引用数链接数都为0时，删除文件
      // inode has no links and no other references: truncate and free.
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;    //一般情况下引用数减一
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)  //解锁释放
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint bmap(struct inode *ip, uint bn)  //给i结点第bn个索引指向的地方分配块
{
  uint addr, *a;
  struct buf *bp;
  
  //bn为直接索引
  if(bn < NDIRECT){    
    //如果第bn个索引指向的块还未分配，则分配，否则返回块号
    if((addr = ip->addrs[bn]) == 0)     
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }

  bn -= NDIRECT;     //bn为间接索引

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)          //如果间接索引所在的块还未分配，分配
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);           //读取一级索引块
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){             //如果该索引指向的块还未分配，分配
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;         //返回索引bn指向的块的块号
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){         //释放直接索引指向的数据块
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);   
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);     //读取一级索引块
    a = (uint*)bp->data;     
    for(j = 0; j < NINDIRECT; j++){     //释放一级索引指向的块
      if(a[j])
        bfree(ip->dev, a[j]);              
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);    //释放一级索引块
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)   //获取inode信息
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
int readi(struct inode *ip, char *dst, uint off, uint n) //从inode读取数据
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){  //如果该inode指向的是设备文件
    //major number小于0，major number 超过支持的设备数，没有该设备的写函数
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);  //使用设备特有的读取方式
  }

  if(off > ip->size || off + n < off)  //如果开始读取的位置超过文件末尾，如果读取的字节数是负数
    return -1;
  if(off + n > ip->size)   //如果从偏移量开始的n字节超过文件末尾
    n = ip->size - off;    //则只能够再读取这么多字节

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){  //tol:目前总共已读的字节数，n:需要读取的字节数,off:从这开始读,dst:目的地
    bp = bread(ip->dev, bmap(ip, off/BSIZE)); //读取off所在的数据块到缓存块
    m = min(n - tot, BSIZE - off%BSIZE);  //一次性最多读取m字节
    memmove(dst, bp->data + off%BSIZE, m);  //复制数据到dst
    brelse(bp);  //释放缓存块
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.
int
writei(struct inode *ip, char *src, uint off, uint n) //通过inode写数据
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){  //如果是设备文件
    //major number小于0，major number 超过支持的设备数，没有该设备的写函数
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write) 
      return -1;
    return devsw[ip->major].write(ip, src, n); //调用设备特有的写函数
  }

  if(off > ip->size || off + n < off)  //如果开始写的位置超过文件末尾，如果读取的字节数是负数
    return -1;
  if(off + n > MAXFILE*BSIZE)   //文件过大
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){   //tol:目前总共已写的字节数，n:需要写的字节数,off:从这开始写,dst:目的地
    bp = bread(ip->dev, bmap(ip, off/BSIZE));  //读取off所在的数据块到缓存块
    m = min(n - tot, BSIZE - off%BSIZE);       //一次性最多读取m字节
    memmove(bp->data + off%BSIZE, src, m);     //复制数据到dst
    log_write(bp);    //写到日志块
    brelse(bp);       //释放该块
  }

  if(n > 0 && off > ip->size){  //如果写了数据进文件
    ip->size = off;   //更新文件大小
    iupdate(ip);      //更新inode
  }
  return n;   //返回写的字节数
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode* dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)    //如果该文件不是目录文件
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))   //读取dp指向的目录文件，每次读一个目录项
      panic("dirlookup read");
    if(de.inum == 0)  //如果是空闲目录项，continue
      continue;
    if(namecmp(name, de.name) == 0){    //根据名字找文件
      // entry matches path element
      if(poff)              //记录该目录项在目录中的偏移
        *poff = off;
      inum = de.inum;       //name文件的inode编号
      return iget(dp->dev, inum);    //或取该inode
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){   
    iput(ip);       //dirlookup调用iget使引用数加1，所以调用iput使引用数减1
    return -1;      //name目录项已存在，返回-1
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) //如果读取错误
      panic("dirlink read");
    if(de.inum == 0)   //找到一个空闲目录项
      break;
  }

  strncpy(de.name, name, DIRSIZ);     //设置目录项的文件名字
  de.inum = inum;                     //设置目录项的i结点编号
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))   //将该目录项写进dp指向的目录文件中
    panic("dirlink");

  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')   //跳过'/'
    path++;
  if(*path == 0)    //路径空
    return 0;
  s = path;
  while(*path != '/' && *path != 0)   //path继续向后移，剥出最前面的目录名
    path++;
  len = path - s;        //记录该目录名的长度
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);    //将该目录名复制给name
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')   //继续跳过'/'
    path++;
  return path;    //返回剩下的路径
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')   //绝对路径
    ip = iget(ROOTDEV, ROOTINO);    //读取根目录
  else               //相对路径
    ip = idup(myproc()->cwd);       //读取当前工作目录

  while((path = skipelem(path, name)) != 0){
    ilock(ip);   //给inode ip 上锁，使得inode 数据有效
    if(ip->type != T_DIR){   //如果不是目录文件
      iunlockput(ip);   //释放锁，释放inode ip
      return 0;
    }
    if(nameiparent && *path == '\0'){    //如果是要返回父结点，并且剩下的路径已经为空，则当前结点就是父结点直接返回
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){   //查询下一层目录，若无
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;    //当前目录指向下一层，然后while循环，直到解析到最后
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)   //获取当前路径表示的文件inode
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)  //获取当前路径表示的文件的父目录inode
{
  return namex(path, 1, name);
}
