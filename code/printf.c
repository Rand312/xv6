#include "types.h"
#include "stat.h"
#include "user.h"

static void
putc(int fd, char c)
{
  write(fd, &c, 1);
}

static void
printint(int fd, int xx, int base, int sgn)   //将整数xx按照进制为base，符号为sgn打印出来
{
  static char digits[] = "0123456789ABCDEF";
  char buf[16];
  int i, neg;
  uint x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  while(--i >= 0)
    putc(fd, buf[i]);
}

// Print to the given fd. Only understands %d, %x, %p, %s.
void
printf(int fd, const char *fmt, ...)
{
  char *s;
  int c, i, state;
  uint *ap;

  state = 0;     //当前字符状态:普通字符还是'%'
  ap = (uint*)(void*)&fmt + 1;  //第一个可变参数地址
  for(i = 0; fmt[i]; i++){   //循环次数为字符串里面的字符数
    c = fmt[i] & 0xff;   //从字符串里取第i个字符
    if(state == 0){   //如果这个字符前面不是%
      if(c == '%'){   //如果当前字符是%
        state = '%';  //记录状态为%
      } else {        //如果当前字符不是%
        putc(fd, c);  //调用putc向fd指向的文件写字符c
      }
    } else if(state == '%'){    //如果当前字符前面是一个%说明需要格式化处理
      if(c == 'd'){    //如果是%d
        printint(fd, *ap, 10, 1);  //调用printint打印有符号十进制数
        ap++;        //参数指针指向下一个参数
      } else if(c == 'x' || c == 'p'){  //如果是%x,%p
        printint(fd, *ap, 16, 0);  //调用printint打印无符号十六进制数
        ap++;       //参数指针指向下一个参数
      } else if(c == 's'){  //如果是%s
        s = (char*)*ap;  //取这个参数的值，这个参数值就是字符串指针
        ap++;       //参数指针指向下一个参数
        if(s == 0)  //如果字符串指针指向0
          s = "(null)";  //不出错，而是赋值"(null)"
        while(*s != 0){  //while循环打印字符串s
          putc(fd, *s);  
          s++;
        }
      } else if(c == 'c'){ //如果是%c
        putc(fd, *ap);  //打印这个字符
        ap++;           //参数指针指向下一个参数
      } else if(c == '%'){  //如果是%%
        putc(fd, c);    //打印%
      } else {     //其他不明情况比如%m,那么就打印%m不做处理
        // Unknown % sequence.  Print it to draw attention.
        putc(fd, '%');  //打印%
        putc(fd, c);    //打印字符
      }
      state = 0;  //state归于0普通状态
    }
  }
}
