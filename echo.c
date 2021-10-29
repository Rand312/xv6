#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;

  for(i = 1; i < argc; i++)   //从第一个参数开始循环(0起始)
    printf(1, "%s%s", argv[i], i+1 < argc ? " " : "\n");  //将参数作为字符串打印出来
  exit();   //执行完之后退出
}
