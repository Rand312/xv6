#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){   //rm filepath 至少两个参数
    printf(2, "Usage: rm files...\n");
    exit();
  }

  for(i = 1; i < argc; i++){    
    if(unlink(argv[i]) < 0){   //调用unlink"删除"文件
      printf(2, "rm: %s failed to delete\n", argv[i]);
      break;
    }
  }

  exit();  //执行完后退出
}
