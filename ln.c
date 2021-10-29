#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){   //ln old new，至少三个参数
    printf(2, "Usage: ln old new\n");
    exit();
  }
  if(link(argv[1], argv[2]) < 0)  //调用link系统调用建立硬链接
    printf(2, "link %s %s: failed\n", argv[1], argv[2]);
  exit();  //执行完后退出
}
