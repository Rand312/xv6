#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

int kbdgetc(void)
{
  static uint shift;      //shift用bit来记录控制键，比如shift,ctrl
  static uchar *charcode[4] = {
    normalmap, shiftmap, ctlmap, ctlmap
  };       //映射表
  uint st, data, c;

  st = inb(KBSTATP);
  if((st & KBS_DIB) == 0)     //输出缓冲区未满，没法用指令in读取
    return -1;
  data = inb(KBDATAP);      //从输出缓冲区读数据

  if(data == 0xE0){       //通码以e0开头的键
    shift |= E0ESC;       //记录e0
    return 0;
  } else if(data & 0x80){    //断码，表键弹起
    // Key released
    data = (shift & E0ESC ? data : data & 0x7F);     
    shift &= ~(shiftcode[data] | E0ESC);             
    return 0;
  } else if(shift & E0ESC){   //紧接着0xE0后的扫描码
    // Last character was an E0 escape; or with 0x80
    data |= 0x80;
    shift &= ~E0ESC;
  }

  shift |= shiftcode[data];   //记录控制键状态，如Shift,Ctrl,Alt
  shift ^= togglecode[data];  //记录控制键状态，如CapsLock，NumLock，ScrollLock     
  c = charcode[shift & (CTL | SHIFT)][data]; //获取映射表的内容，也就是该键表示的意义
  if(shift & CAPSLOCK){
    if('a' <= c && c <= 'z')
      c += 'A' - 'a';
    else if('A' <= c && c <= 'Z')
      c += 'a' - 'A';
  }
  return c;
}

void
kbdintr(void)    //键盘中断服务程序
{
  consoleintr(kbdgetc);
}
