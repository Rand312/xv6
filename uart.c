// Intel 8250 serial port (UART).

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

#define COM1    0x3f8

static int uart;    // is there a uart?

void uartinit(void)
{
  char *p;
  outb(COM1+2, 0);  //关闭FIFO

  outb(COM1+3, 0x80);    // DLAB=1
  outb(COM1+0, 115200/9600); //设置波特率为9600，低字节
  outb(COM1+1, 0);    //高字节
  outb(COM1+3, 0x03);    // DLAB=0, 1位停止位，无校验位，8位数据位
  outb(COM1+4, 0);       // modem控制寄存器，置零忽略
  outb(COM1+1, 0x01);    // Enable receive interrupts.

  // If status is 0xFF, no serial port.
  if(inb(COM1+5) == 0xFF) //如果读线路状态寄存器读出的值为0xff，则说明UART不存在
    return;
  uart = 1;

  // Acknowledge pre-existing interrupt conditions;
  // enable interrupts.
  inb(COM1+2);
  inb(COM1+0);
  ioapicenable(IRQ_COM1, 0);

  // Announce that we're here.
  for(p="xv6...\n"; *p; p++)
    uartputc(*p);
}

void uartputc(int c){
  int i;
  if(!uart)
    return;
  for(i = 0; i < 128 && !(inb(COM1+5) & 0x20); i++)  //循环等待传输保持寄存器空
    microdelay(10);
  outb(COM1+0, c);   //向传输寄存器写入要发送的字符
}
static int uartgetc(void){
  if(!uart)
    return -1;
  if(!(inb(COM1+5) & 0x01))  //数据好没？
    return -1;
  return inb(COM1+0);  //从传输寄存器获取字符
}

void
uartintr(void)
{
  consoleintr(uartgetc);
}
