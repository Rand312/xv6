// Simple grep.  Only supports ^ . * $ operators.

#include "types.h"
#include "stat.h"
#include "user.h"

char buf[1024];
int match(char*, char*);

void
grep(char *pattern, int fd)
{
  int n, m;
  char *p, *q;

  m = 0;
  while((n = read(fd, buf+m, sizeof(buf)-m-1)) > 0){  //读取文件内容
    m += n;    //记录已读的字节数
    buf[m] = '\0';   //截止处理
    p = buf;   //buf首地址赋给p
    while((q = strchr(p, '\n')) != 0){  //检查刚读取的这段数据是否有换行，有的话
      *q = 0;                           //其位置上的字符置0，因为match函数以0评判是否为结尾
      if(match(pattern, p)){       //匹配这一行的文本
        *q = '\n';                 //匹配成功的话，重新变成换行符
        write(1, p, q+1 - p);      //并且打印这行
      }
      p = q+1;     //p指向下一行首字符地址
    }   //重复上述操作
    if(p == buf)   //如果读取的这段数据一个换行符都没有
      m = 0;       //m置0重复操作
    if(m > 0){     //读取的这段数据中，p之前的已经匹配处理过了，
      m -= p - buf;    //计算已经处理多少文本
      memmove(buf, p, m); //把p之前的文本移出去
    }
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;
  char *pattern;

  if(argc <= 1){   //grep pattern [file] 至少两个参数，1个参数出错了
    printf(2, "usage: grep pattern [file ...]\n");
    exit();
  }
  pattern = argv[1];

  if(argc <= 2){  //grep pattern 没有指定文件，那么就读取控制台文件
    grep(pattern, 0);
    exit();
  }

  for(i = 2; i < argc; i++){  //grep patter file1 file2 ...
    if((fd = open(argv[i], 0)) < 0){  //打开文件
      printf(1, "grep: cannot open %s\n", argv[i]);
      exit();
    }
    grep(pattern, fd);  //匹配模式串和文本
    close(fd);   //关闭文件
  }   //重复上述操作
  exit();   //完成之后退出
}

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9.

int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')    //规则匹配到结尾了，说明匹配成功返回1
    return 1;
  if(re[1] == '*')    //碰到*，调用matchstar处理
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')  //规则到结尾了
    return *text == '\0';            //如果文本也到结尾了，匹配成功
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))  //普通情况，如果规则是.或者单个字符匹配成功
    return matchhere(re+1, text+1);                //规则和文本都向后移，匹配下一个字符
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
  do{  // a * matches zero or more instances
    if(matchhere(re, text))    //第一次执行为*匹配0次，每执行依次*匹配次数加1
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.')); 
  return 0;
}

