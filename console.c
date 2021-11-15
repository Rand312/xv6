// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)  //将整数xx按照进制为base，符号为sgn打印出来
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
//cprintf，过程基本同printf，只是cprintf是内核中使用，printf是用户使用，详见printf
void
cprintf(char *fmt, ...)   
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)    //出现重大错误时 panic
{
  int i;
  uint pcs[10];

  cli();      //关中断
  cons.locking = 0;   //解控制台的锁
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());  //打印CPUID消息
  cprintf(s);     //打印自定义消息
  cprintf("\n");  //换行
  getcallerpcs(&s, pcs);  //获取调用栈帧信息
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);   //打印栈帧信息
  panicked = 1; // freeze other CPU，因为其他CPU会检查全局变量panicked的值，如果为1，(for::);  
  for(;;)    //freeze 当前CPU
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory 文本模式，有32KB

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  /*****获取光标位置*****/
  outb(CRTPORT, 14);           //地址寄存器中选择高光标位置寄存器
  pos = inb(CRTPORT+1) << 8;   //从数据寄存器中获取光标位置高8位
  outb(CRTPORT, 15);           //地址寄存器中选择低光标位置寄存器
  pos |= inb(CRTPORT+1);       //从数据寄存器中获取光标位置低8位

  if(c == '\n')              //换行
    pos += 80 - pos%80;      //光标位置加上本行剩余的位置数就是换行了
  else if(c == BACKSPACE){   //退格
    if(pos > 0) --pos;       
  } else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white  前景色设的7表黑色，背景色位0表白色

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.  一屏最多显示25行，这里整除大于等于24就开始滚动了，说明没有用最后一行，实验证明的确如此
    memmove(crt, crt+80, sizeof(crt[0])*23*80);   //将中23行上移，第一行被覆盖
    pos -= 80;       //直接减去80将光标移动到上一行相应位置
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));   //再将最后一行的东西移动到倒数第二行
  }

  /*移动光标*/
    outb(CRTPORT, 14);
    outb(CRTPORT+1, pos>>8);
    outb(CRTPORT, 15);
    outb(CRTPORT+1, pos);
    crt[pos] = ' ' | 0x0700;   //在光标位置打印空白字符
}

void consputc(int c){
  if(panicked){   //如果panic了，冻住CPU
    cli();    //关中断
    for(;;)   //无限循环来 freeze CPU
      ;
  }

  if(c == BACKSPACE){  //如果是退格键
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);   //打印到串口控制台
  cgaputc(c);   //打印到本地控制台
}

#define INPUT_BUF 128
struct {
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x

void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'):  //清空当前行
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('H'): case '\x7f':  //退格
      if(input.e != input.w){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    default:     //普通字符
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        consputc(c);
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}

int consoleread(struct inode *ip, char *dst, int n){
  uint target;
  int c;
 
  iunlock(ip);    //释放inode ip
  target = n;     //准备要读取的字节数
  acquire(&cons.lock);  //获取控制台的锁
  while(n > 0){
    while(input.r == input.w){  //要读取的数据还没到
      if(myproc()->killed){     //如果该进程已经被killed
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);  //休眠在缓冲区的r位上
    }
	c = input.buf[input.r++ % INPUT_BUF]; //缓存区读取标识索引后移
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;   //搬运字符
    --n;          //还需要读取的字符数减1
    if(c == '\n') //如果是换行符
      break;      //跳出循环准备返回
  }
  release(&cons.lock);  //释放控制台的锁
  ilock(ip);    //重新获取inode的锁

  return target - n;  //返回实际读取的字节数
}

int consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);     //释放控制台文件inode的锁
  acquire(&cons.lock);  //获取控制台的锁
  for(i = 0; i < n; i++)   //循环n次
    consputc(buf[i] & 0xff);  //向控制台写字符
  release(&cons.lock);  //释放控制台的锁
  ilock(ip);    //重新获取控制台文件inode的锁

  return n;
}

void
consoleinit(void)   //控制台初始化
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;  //将写函数指针放进devsw数组
  devsw[CONSOLE].read = consoleread;    //将读函数指针放进devsw数组
  cons.locking = 1;   

  ioapicenable(IRQ_KBD, 0);     //CPU0来处理键盘中断
}

