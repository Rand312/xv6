#include "types.h"
#include "stat.h"
#include "user.h"

char buf[512];

void
wc(int fd, char *name)
{
  int i, n;
  int l, w, c, inword;

  l = w = c = 0;
  inword = 0;
  while((n = read(fd, buf, sizeof(buf))) > 0){  //从fd指向的文件中读取数据
    for(i=0; i<n; i++){    //读取了多少个字符，循环多少次
      c++;     
      if(buf[i] == '\n')   //有换行
        l++;     //行数加1
      if(strchr(" \r\t\n\v", buf[i]))  //有空白字符
        inword = 0;   
      else if(!inword){  
        w++;              //单词数加1
        inword = 1;
      }
    }
  }
  if(n < 0){       //没有读取到字符
    printf(1, "wc: read error\n");   
    exit();
  }
  printf(1, "%d %d %d %s\n", l, w, c, name);  //打印 行数，单词数，字节数，名字
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){   //如果参数≤1
    wc(0, "");     //从键盘获取输入
    exit();        //执行完退出
  }

  for(i = 1; i < argc; i++){   //从第一个参数开始循环(0起始)
    if((fd = open(argv[i], 0)) < 0){    //打开参数指向的文件
      printf(1, "wc: cannot open %s\n", argv[i]);
      exit();
    }
    wc(fd, argv[i]);   //统计这个文件
    close(fd);    //关闭文件描述符
  }
  exit();   //执行完后退出
}
