#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# Start the first CPU: switch to 32-bit protected mode, jump into C.
# The BIOS loads this code from the first sector of the hard disk into
# memory at physical address 0x7c00 and starts executing in real mode
# with %cs=0 %ip=7c00.

.code16                       # Assemble for 16-bit mode     16位实模式
.globl start
start:
  cli                         # BIOS enabled interrupts; disable   关中断

  # Zero data segment registers DS, ES, and SS.     异或操作将段寄存器清零
  xorw    %ax,%ax             # Set %ax to zero
  movw    %ax,%ds             # -> Data Segment
  movw    %ax,%es             # -> Extra Segment
  movw    %ax,%ss             # -> Stack Segment

  # Physical address line A20 is tied to zero so that the first PCs   打开A20地址线
  # with 2 MB would run software that assumed 1 MB.  Undo that.
seta20.1:
  inb     $0x64,%al               # Wait for not busy 等待i8042缓冲区为空
  testb   $0x2,%al
  jnz     seta20.1

  movb    $0xd1,%al               # 0xd1 -> port 0x64  发送0xd1到端口0x64，表示准备向0x60端口写入命令
  outb    %al,$0x64

seta20.2:
  inb     $0x64,%al               # Wait for not busy  同上
  testb   $0x2,%al
  jnz     seta20.2

  movb    $0xdf,%al               # 0xdf -> port 0x60  向端口0x60写入0xdf，打开A20
  outb    %al,$0x60

  # Switch from real to protected mode.  Use a bootstrap GDT that makes
  # virtual addresses map directly to physical addresses so that the
  # effective memory map doesn't change during the transition.
  lgdt    gdtdesc   #加载gdt

  #将cr0寄存器PE位值1开启保护模式
  movl    %cr0, %eax     
  orl     $CR0_PE, %eax 
  movl    %eax, %cr0

//PAGEBREAK!
  # Complete the transition to 32-bit protected mode by using a long jmp
  # to reload %cs and %eip.  The segment descriptors are set up with no
  # translation, so that the mapping is still the identity mapping.
  ljmp    $(SEG_KCODE<<3), $start32   #跳到 (SEG_KODE<<3):start32，段基址为0，所以就是跳到start32处
                                      #使用长跳刷新流水线，因为目前的流水线里面有16位实模式下的指令，而后面应该用32位保护模式下的指令

.code32  # Tell assembler to generate 32-bit code now.
start32:
  # Set up the protected-mode data segment registers
  movw    $(SEG_KDATA<<3), %ax    # Our data segment selector  设置段寄存器，DS,ES,SS共用一个段选择子
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %ss                # -> SS: Stack Segment
  movw    $0, %ax                 # Zero segments not ready for use  FS,GS设为0，不用
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # Set up the stack pointer and call into C.
  movl    $start, %esp   #将start0x7c00设为栈顶
  call    bootmain       

  # If bootmain returns (it shouldn't), trigger a Bochs
  # breakpoint if running under Bochs, then loop.
  movw    $0x8a00, %ax            # 0x8a00 -> port 0x8a00   处理bootmain发生错误的，具体嗯还没看懂，希望有大佬解释
  movw    %ax, %dx
  outw    %ax, %dx
  movw    $0x8ae0, %ax            # 0x8ae0 -> port 0x8a00
  outw    %ax, %dx
spin:
  jmp     spin



@ #define SEG_NULLASM                                             \
@         .word 0, 0;                                             \
@         .byte 0, 0, 0, 0

@ // The 0xC0 means the limit is in 4096-byte units
@ // and (for executable segments) 32-bit mode.
@ #define SEG_ASM(type,base,lim)                                  \
@         .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);      \
@         .byte (((base) >> 16) & 0xff), (0x90 | (type)),         \
@                 (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)
# Bootstrap GDT
.p2align 2                                # force 4 byte alignment
gdt: 
  SEG_NULLASM                             # null seg    GDT中第一个段描述符不用
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)   # code seg    代码段选择子  执行，读权限
  SEG_ASM(STA_W, 0x0, 0xffffffff)         # data seg    数据段选择子  写权限

gdtdesc:                                  # 构造gdtr用到的6字节数据
  .word   (gdtdesc - gdt - 1)             # sizeof(gdt) - 1   界限=大小-1
  .long   gdt                             # address gdt       gdt起始地址

