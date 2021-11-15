# Initial process execs /init.
# This code runs in user space.

#include "syscall.h"
#include "traps.h"


# exec(init, argv)
.globl start
start:
  pushl $argv    #压入参数argv
  pushl $init    #压入参数路径init
  pushl $0       #无效返回地址
  movl $SYS_exec, %eax  #exec系统调用号
  int $T_SYSCALL  #执行系统调用

# for(;;) exit();
exit:              
  movl $SYS_exit, %eax   #exit系统调用号
  int $T_SYSCALL   #执行exit系统调用
  jmp exit    

# char init[] = "/init\0";  准备exec第一个参数路径
init:
  .string "/init\0"

# char *argv[] = { init, 0 };  准备exec第二个参数字符串数组
.p2align 2
argv:
  .long init
  .long 0
