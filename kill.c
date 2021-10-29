#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char **argv)
{
  int i;

  if(argc < 2){  //kill pid 最少两个参数，少于两个出错
    printf(2, "usage: kill pid...\n");
    exit();
  }
  for(i=1; i<argc; i++)   //从第一个参数开始循环(0起始)
    kill(atoi(argv[i]));  //将字符串转换成整型，调用kill系统调用
  exit();   //执行完后退出
}
