#include "types.h"
#include "stat.h"
#include "user.h"

char buf[512];

void
cat(int fd)
{
  int n;

  while((n = read(fd, buf, sizeof(buf))) > 0) {   //读取fd指向的文件的内容
    if (write(1, buf, n) != n) {     //输出到屏幕
      printf(1, "cat: write error\n");
      exit();
    }
  }
  if(n < 0){     //读写的字节数小于0，出错了
    printf(1, "cat: read error\n");
    exit();
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){   //如果没有参数
    cat(0);        //从键盘获取输入
    exit();        //执行完后退出
  }

  for(i = 1; i < argc; i++){    //从第一个参数开始循环(0起始)
    if((fd = open(argv[i], 0)) < 0){    //打开文件
      printf(1, "cat: cannot open %s\n", argv[i]);
      exit();
    }
    cat(fd);     //调用cat读取文件并输出
    close(fd);   //关闭文件
  }
  exit();   //执行完后退出
}
