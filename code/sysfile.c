//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)  //获取用户态下传给内核的参数：文件描述符
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)    //取参数文件描述符
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)   //分配文件描述符
{
  int fd;
  struct proc *curproc = myproc();   //当前进程控制块的地址

  for(fd = 0; fd < NOFILE; fd++){  
    if(curproc->ofile[fd] == 0){  //如果该描述符对应的元素为0则空闲可分配
      curproc->ofile[fd] = f;  //填写文件结构体地址
      return fd;   //返回文件描述符
    }
  }
  return -1;
}

int
sys_dup(void)   //系统调用dup，就是分配一个文件描述符使其指向文件结构体f，然后f的引用数加1
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)   //系统调用read
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)  //获取参数
    return -1;
  return fileread(f, p, n);  //调用fileread读取
}

int
sys_write(void)  //系统调用写
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)  //获取参数
    return -1;
  return filewrite(f, p, n); //调用filewrite写
}

int
sys_close(void)  //系统调用close
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)  //获取参数
    return -1;
  myproc()->ofile[fd] = 0;  //将打开文件描述符表的fd项清0
  fileclose(f);   //关闭相应的文件结构体
  return 0;
}

int
sys_fstat(void)    //系统调用 fstat，获取一些inode信息
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0) //获取参数
    return -1;
  return filestat(f, st);  //调用filestat实现
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)  //系统调用link，创建硬链接
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)   //取参数文件名
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){    //获取该文件的inode,如果不存在返回
    end_op();
    return -1;
  }

  ilock(ip);     //锁上该inode，顺便使其数据有效
  if(ip->type == T_DIR){   //如果类型是目录返回
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;    //old文件链接数加1
  iupdate(ip);    //更新inode,写到磁盘(日志区)
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)   //寻找新文件的父目录inode 
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){  //在父目录下填写new的名字，old的inode编号，
    iunlockput(dp);                                           //两者链接在一起了使用同一个inode表同一个文件
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)   //是否是空目录
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){  //跳过. ..
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)    //取得参数路径
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){  //返回最后一个文件的父目录的inode
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)  //unlink的文件是 . 或 ..
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)  //在dp指向的目录下查找name文件
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){  //如果该文件是个目录文件且为空目录，则unlockput
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)  //要创建文件的父目录inode
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){   //如果该文件在目录中存在
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)  //如果类型是普通文件
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)  //如果该文件不存在，分配一个inode
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;        //只有当前这一个文件使用该inode
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries. //如果是创建目录文件，那么必须创建父目录和当前目录
    dp->nlink++;  // for ".."  关于父目录的目录项，父目录的链接加1
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)  //填写目录项
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)  //在父目录下添加当前文件的目录项
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)  //获取参数路径和模式
    return -1;

  begin_op();

  if(omode & O_CREATE){   //如果是创建一个文件
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {   //否则直接打开文件
    if((ip = namei(path)) == 0){  //解析路径，获取文件inode
      end_op();
      return -1;
    }
    ilock(ip);   //上锁，(使得inode数据有效)
    if(ip->type == T_DIR && omode != O_RDONLY){   //如果文件类型是目录且模式为只读
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){   //分配一个文件结构体和文件描述符
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;  
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)  //创建目录
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){  //获取参数路径，调用create创建目录
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)  //创建设备文件
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){   //创建设备文件比如说控制台文件
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)   //更改当前工作路径
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){  //获取参数路径，以及路径中最后一个文件的inode
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){   //如果类型不是目录文件
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);  
  iput(curproc->cwd);  //“放下”进程原路径的inode
  end_op();
  curproc->cwd = ip;  //进程的路径换为当前新路径
  return 0;
}

int
sys_exec(void)  
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){  //获取系统调用的参数，uargv是个字符串数组
    return -1;
  }
  memset(argv, 0, sizeof(argv));  
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)  //获取每个字符串地址所在的地址
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)  //获取每个字符串的地址
      return -1;
  }
  return exec(path, argv);  //执行程序
}

int
sys_pipe(void)   //创建管道
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)  //分配管道(俩文件结构体和一片内存)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){  //分配俩文件描述符
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;  
  fd[1] = fd1;
  return 0;
}
