// Boot loader.
//
// Part of the boot block, along with bootasm.S, which calls bootmain().
// bootasm.S has put the processor into protected 32-bit mode.
// bootmain() loads an ELF kernel image from the disk starting at
// sector 1 and then jumps to the kernel entry routine.

#include "types.h"
#include "elf.h"
#include "x86.h"
#include "memlayout.h"

#define SECTSIZE  512

void readseg(uchar*, uint, uint);

void bootmain(void){
  struct elfhdr *elf;
  struct proghdr *ph, *eph;
  void (*entry)(void);
  uchar* pa;

  elf = (struct elfhdr*)0x10000;  // scratch space 将内核elf文件加载到这个位置

  // Read 1st page off disk
  readseg((uchar*)elf, 4096, 0);   //从扇区 1 开始读，读4096个字节到0x10000，即8个扇区

  // Is this an ELF executable?
  if(elf->magic != ELF_MAGIC)   //判断是否是elf文件
    return;  // let bootasm.S handle error   //不是就返回

  // Load each program segment (ignores ph flags).
  ph = (struct proghdr*)((uchar*)elf + elf->phoff);    //第一个程序头的位置
  eph = ph + elf->phnum;        //最后一个程序头的位置
  for(; ph < eph; ph++){      //for循环读取程序段
    pa = (uchar*)ph->paddr;   //程序段的位置
    readseg(pa, ph->filesz, ph->off);   //off是该段相对于elf的偏移量，filesz是该段的大小，即从off所在的扇区读取filesz到内存地址为pa的地方
    if(ph->memsz > ph->filesz)     //因为 bss节的存在，elf文件并不需要存在bss的实体，但是内存中需要占位，所以可能大些
      stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz); //调用 stosb 将段的剩余部分置零
  }

  // Call the entry point from the ELF header.
  // Does not return!
  entry = (void(*)(void))(elf->entry);   //entry，内核程序的入口点
  entry();   //调用entry
}

void
waitdisk(void)
{
  // Wait for disk ready.     //等待磁盘空闲
  while((inb(0x1F7) & 0xC0) != 0x40)   //读操作时端口0x1f7是磁盘状态寄存器，从这儿获取磁盘状态忙还是空闲
    ;
}

// Read a single sector at offset into dst.
void
readsect(void *dst, uint offset)    //读扇区offset到内存dst
{
  // Issue command.
  waitdisk();
  outb(0x1F2, 1);   // count = 1      //读取几个扇区
  outb(0x1F3, offset);                //LBA地址 低8位
  outb(0x1F4, offset >> 8);           //LAB地址 中8位
  outb(0x1F5, offset >> 16);          //LBA地址 高8位
  outb(0x1F6, (offset >> 24) | 0xE0);  //LBA地址最高的4位 和 一些其他信息：寻址模式LBA
  outb(0x1F7, 0x20);  // cmd 0x20 - read sectors   //给磁盘发送命令0x20，即读扇区

  // Read data.
  waitdisk();
  insl(0x1F0, dst, SECTSIZE/4);     //从端口0x1f0读128*4=512字节，即一个扇区
}

// Read 'count' bytes at 'offset' from kernel into physical address 'pa'.
// Might copy more than asked.
//从offset所在的扇区读取count个字节到地址pa处
void
readseg(uchar* pa, uint count, uint offset)   
{                                             
  uchar* epa;                                 

  epa = pa + count;

  // Round down to sector boundary.
  pa -= offset % SECTSIZE;    //向下取整(单位扇区)

  // Translate from bytes to sectors; kernel starts at sector 1.
  offset = (offset / SECTSIZE) + 1;   //加1是因为内核文件在扇区1(起始扇区0)

  // If this is too slow, we could read lots of sectors at a time.
  // We'd write more to memory than asked, but it doesn't matter --
  // we load in increasing order.
  for(; pa < epa; pa += SECTSIZE, offset++)  //循环读取扇区到pa处
    readsect(pa, offset);   
}
