// The I/O APIC manages hardware interrupts for an SMP system.
// http://www.intel.com/design/chipsets/datashts/29056601.pdf
// See also picirq.c.

#include "types.h"
#include "defs.h"
#include "traps.h"

#define IOAPIC  0xFEC00000   // Default physical address of IO APIC

#define REG_ID     0x00  // Register index: ID
#define REG_VER    0x01  // Register index: version
#define REG_TABLE  0x10  // Redirection table base  重定向表

// The redirection table starts at REG_TABLE and uses
// two registers to configure each interrupt.
// The first (low) register in a pair contains configuration bits.
// The second (high) register contains a bitmask telling which
// CPUs can serve that interrupt.
#define INT_DISABLED   0x00010000  // Interrupt disabled  中断屏蔽位
#define INT_LEVEL      0x00008000  // Level-triggered (vs edge-)  触发模式，电平还是边沿
#define INT_ACTIVELOW  0x00002000  // Active low (vs high)  管脚极性，置1表示低电平有效，置0高电平有效
#define INT_LOGICAL    0x00000800  // Destination is CPU id (vs APIC ID)  destination mode，1逻辑模式，0物理模式

volatile struct ioapic *ioapic;

// IO APIC MMIO structure: write reg, then read or write data.
struct ioapic {
  uint reg;       //IOREGSEL
  uint pad[3];    //填充12字节
  uint data;      //IOWIN
};

static uint ioapicread(int reg) //读取reg寄存器，reg是个索引值
{
  ioapic->reg = reg;    //选定寄存器reg
  return ioapic->data;  //从窗口寄存器中读出寄存器reg数据
}

static void ioapicwrite(int reg, uint data) //向reg寄存器写data，reg是个索引值
{
  ioapic->reg = reg;    //选定寄存器reg
  ioapic->data = data;  //向窗口寄存器写就相当于向寄存器reg写
}

void ioapicinit(void)
{
  int i, id, maxintr;

  ioapic = (volatile struct ioapic*)IOAPIC;      //IOREGSEL的地址
  maxintr = (ioapicread(REG_VER) >> 16) & 0xFF;  //读取version寄存器16-23位，获取最大的中断数
  id = ioapicread(REG_ID) >> 24;      //读取ID寄存器24-27 获取IOAPIC ID
  if(id != ioapicid)  //检查两者是否相等
    cprintf("ioapicinit: id isn't equal to ioapicid; not a MP\n");

  // Mark all interrupts edge-triggered, active high, disabled,
  // and not routed to any CPUs.  将所有的中断重定向表项设置为边沿，高有效，屏蔽状态
  for(i = 0; i <= maxintr; i++){   
    ioapicwrite(REG_TABLE+2*i, INT_DISABLED | (T_IRQ0 + i));  //设置低32位，每个表项64位，所以2*i，
    ioapicwrite(REG_TABLE+2*i+1, 0);   //设置高32位
  }
}

void ioapicenable(int irq, int cpunum)
{
  // Mark interrupt edge-triggered, active high,
  // enabled, and routed to the given cpunum,
  // which happens to be that cpu's APIC ID.     调用此函数使能相应的中断
  ioapicwrite(REG_TABLE+2*irq, T_IRQ0 + irq);
  ioapicwrite(REG_TABLE+2*irq+1, cpunum << 24);  //左移24位是填写 destination field字段
}
