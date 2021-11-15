#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {   //日志头
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;    //日志区第一块块号
  int size;     //日志区大小
  int outstanding; // 有多少文件系统调用正在执行
  int committing;  // 正在提交
  int dev;     //设备，即主盘还是从盘，文件系统在从盘
  struct logheader lh;   //日志头
};
struct log log;

static void recover_from_log(void);
static void commit();

void initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  struct superblock sb;   //定义局部变量超级块sb
  initlock(&log.lock, "log");   //初始化日志的锁
  readsb(dev, &sb);    //读取超级块
  /*根据超级块的信息设置日志的一些信息*/
  log.start = sb.logstart;   //第一个日志块块号
  log.size = sb.nlog;    //日志块块数
  log.dev = dev;     //日志所在设备
  recover_from_log;   //从日志中恢复
}

// Copy committed blocks from log to their home location
static void install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block  读取日志块
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst 读取数据块
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst  将数据复制到目的地
    bwrite(dbuf);  // write dst to disk 同步缓存块到磁盘
    brelse(lbuf);  //释放 lbuf
    brelse(dbuf);  //释放 dbuf
  }
}

// Read the log header from disk into the in-memory log header
static void read_head(void)   //读取日志头信息
{
  struct buf *buf = bread(log.dev, log.start); //日志头在日志区第一块
  struct logheader *lh = (struct logheader *) (buf->data);  //地址类型转换
  int i;
  log.lh.n = lh->n;   //当前日志块数
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];   //当前日志位置信息
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void write_head(void)  //将日志头写到日志区第一块
{
  struct buf *buf = bread(log.dev, log.start);  //读取日志头
  struct logheader *hb = (struct logheader *) (buf->data);  //类型转换
  int i;
  hb->n = log.lh.n;    //日志记录大小
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];  //位置信息
  }
  bwrite(buf);   //将日志头同步到磁盘
  brelse(buf);
}

static void recover_from_log(void)
{
  read_head();     //读取日志头
  install_trans(); //日志区到数据区
  log.lh.n = 0;    //日志记录清零
  write_head();    //同步日志头信息到磁盘
}

// called at the start of each FS system call.
void begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){   //如果日志正在提交，休眠
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit. 如果此次文件系统调用涉及的块数超过日志块数上限，休眠
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;  //文件系统调用加1
      release(&log.lock);   //释放锁
      break;   //退出循环
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);   //取锁
  log.outstanding -= 1;   //文件系统调用减1
  if(log.committing)   //如果正在提交，panic
    panic("log.committing");
  if(log.outstanding == 0){  //如果正在执行的文件系统调用为0，则可以提交了
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);   //唤醒因日志空间不够而休眠的进程
  }
  release(&log.lock);

  if(do_commit){   //如果可以提交
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit;    //提交
    acquire(&log.lock);  //取锁
    log.committing = 0;  //提交完之后设为没有处于提交状态
    wakeup(&log);        //日志空间已重置，唤醒因正在提交和空间不够而休眠的进程
    release(&log.lock);  //释放锁
  }
}

// Copy modified blocks from cache to log.
static void write_log(void)    //将缓存块写到到日志区
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block日志块
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block缓存块
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log 将缓存块的数据写到日志块
    brelse(from); //释放缓存块
    brelse(to);   //释放日志块
  }
}

static void commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log 缓存块写到日志区
    write_head();    // Write header to disk -- the real commit 内存中的日志头写到日志区
    install_trans(); // Now install writes to home locations 日志区的数据移到数据区(home locations)
    log.lh.n = 0;    //日志清0
    write_head();    // Erase the transaction from the log   //清楚磁盘上的日志头
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache with B_DIRTY.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1) //当前已使用的日志空间不能大于规定的大小
    panic("too big a transaction");
  if (log.outstanding < 1)  //如果当前正执行的系统调用小于1
    panic("log_write outside of trans");

  acquire(&log.lock);   
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion吸收
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n)
    log.lh.n++;      //日志空间使用量加1
  b->flags |= B_DIRTY;  // prevent eviction 设置脏位，避免缓存块直接释放掉了
  release(&log.lock);
}

