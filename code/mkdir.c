#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){   //mkdir dirname1 至少两个参数
    printf(2, "Usage: mkdir files...\n");
    exit();
  }

  for(i = 1; i < argc; i++){   //mkdir name1 name2
    if(mkdir(argv[i]) < 0){   //调用mkdir创建目录
      printf(2, "mkdir: %s failed to create\n", argv[i]);
      break;
    }
  }  //重复上述操作

  exit();   //完成后退出
}
