#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();   //开始日志

  if((ip = namei(path)) == 0){    //获取该文件的inode
      end_op();
      cprintf("exec: fail\n");
      return -1;
    }
  ilock(ip);   //对该inode上锁，使得数据有效
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))  //读取elf头
      goto bad;
  if(elf.magic != ELF_MAGIC)  //判断文件类型
      goto bad;

  if((pgdir = setupkvm()) == 0)  //建立内核页表
    goto bad;

  // Load program into memory.
  sz = 0;   //进程大小初始为0
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
      if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))  //读取程序头
        goto bad;
      if(ph.type != ELF_PROG_LOAD)  //如果是可装载段
        continue;
      if(ph.memsz < ph.filesz)  //memsz不应该小于filesz
        goto bad;
      if(ph.vaddr + ph.memsz < ph.vaddr)  //memsz也不应该是负数
        goto bad;
      if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)  //分配sz到ph.vaddr + ph.memsz之间的虚拟内存
        goto bad;
      if(ph.vaddr % PGSIZE != 0)  //地址应该是对齐的
        goto bad;
      if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0) //从磁盘搬运filesz字节的数据到vaddr
        goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);   //size对齐
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)  //分配两页虚拟内存
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));  //清除第一页的页表项USER位，使得用户态下不可访问
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
      if(argc >= MAXARG)
        goto bad;
      sp = (sp - (strlen(argv[argc]) + 1)) & ~3;  //栈指针向下移准备放字符串，保证4字节对齐
      if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0) //搬运字符串
        goto bad;
      ustack[3+argc] = sp;  //记录字符串在栈中的地址
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // 假的返回地址
  ustack[1] = argc;    //参数
  ustack[2] = sp - (argc+1)*4;  // argv pointer 参数

  sp -= (3+argc+1) * 4;   //栈指针向下移
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)  //将字符串地址搬运到新的用户空间栈里面
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)   //解析程序名
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;   //旧页目录
  curproc->pgdir = pgdir;   //换新页目录
  curproc->sz = sz;    //更改程序大小
  curproc->tf->eip = elf.entry;  //设置执行的入口点
  curproc->tf->esp = sp;  //新的用户栈顶
  switchuvm(curproc);   //切换页表
  freevm(oldpgdir);   //释放旧的用户空间
  return 0;

 bad:    //如果出错，释放已分配的资源
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
