// Multiprocessor support
// Search memory for MP description structures.
// http://developer.intel.com/design/pentium/datashts/24201606.pdf

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mp.h"
#include "x86.h"
#include "mmu.h"
#include "proc.h"

struct cpu cpus[NCPU];
int ncpu;
uchar ioapicid;

static uchar
sum(uchar *addr, int len)
{
  int i, sum;

  sum = 0;
  for(i=0; i<len; i++)
    sum += addr[i];
  return sum;
}

// Look for an MP structure in the len bytes at addr.
static struct mp* mpsearch1(uint a, int len) //在a~a+len这一段寻找floating pointer 结构
{
  uchar *e, *p, *addr;
  addr = P2V(a);   //转换成虚拟地址
  e = addr+len;   //结尾
  for(p = addr; p < e; p += sizeof(struct mp))
    if(memcmp(p, "_MP_", 4) == 0 && sum(p, sizeof(struct mp)) == 0)   //比较签名和校验和，如果符合则存在floating pointer
      return (struct mp*)p;
  return 0;
}

// Search for the MP Floating Pointer Structure, which according to the
// spec is in one of the following three locations:
// 1) in the first KB of the EBDA;
// 2) in the last KB of system base memory;
// 3) in the BIOS ROM between 0xE0000 and 0xFFFFF.
static struct mp* mpsearch(void)     //寻找mp floating pointer 结构
{
  uchar *bda;
  uint p;
  struct mp *mp;

  bda = (uchar *) P2V(0x400);     //BIOS Data Area地址
    
  if((p = ((bda[0x0F]<<8)| bda[0x0E]) << 4)){  //在EBDA中最开始1K中寻找
    if((mp = mpsearch1(p, 1024)))
      return mp;
  } else {                                 //在基本内存的最后1K中查找
    p = ((bda[0x14]<<8)|bda[0x13])*1024;    
    if((mp = mpsearch1(p-1024, 1024)))
      return mp;
  }
  return mpsearch1(0xF0000, 0x10000);   //在0xf0000~0xfffff中查找
}

// Search for an MP configuration table.  For now,
// don't accept the default configurations (physaddr == 0).
// Check for correct signature, calculate the checksum and,
// if correct, check the version.
// To do: check extended table checksum.
static struct mpconf*
mpconfig(struct mp **pmp)
{
  struct mpconf *conf;
  struct mp *mp;

  if((mp = mpsearch()) == 0 || mp->physaddr == 0)
    return 0;
  conf = (struct mpconf*) P2V((uint) mp->physaddr);
  if(memcmp(conf, "PCMP", 4) != 0)
    return 0;
  if(conf->version != 1 && conf->version != 4)
    return 0;
  if(sum((uchar*)conf, conf->length) != 0)
    return 0;
  *pmp = mp;
  return conf;
}

void
mpinit(void)
{
  uchar *p, *e;
  int ismp;
  struct mp *mp;
  struct mpconf *conf;
  struct mpproc *proc;
  struct mpioapic *ioapic;

  if((conf = mpconfig(&mp)) == 0)   //检测MP Table配置
    panic("Expect to run on an SMP");
  ismp = 1;
  lapic = (uint*)conf->lapicaddr;
  for(p=(uchar*)(conf+1), e=(uchar*)conf+conf->length; p<e; ){    //跳过表头，从第一个表项开始for循环
    switch(*p){     //选取当前表项
    case MPPROC:     //如果是处理器
      proc = (struct mpproc*)p;     
      if(ncpu < NCPU) {
        cpus[ncpu].apicid = proc->apicid;  // apic id可以来标识一个CPU
        ncpu++;          //找到一个CPU表项，CPU数量加1
      } 
      p += sizeof(struct mpproc);    //跳过当前CPU表项继续循环
      continue;
    case MPIOAPIC:    //如果是IOAPIC表项
      ioapic = (struct mpioapic*)p;  //强制转换为IOAPIC类型
      ioapicid = ioapic->apicno;  //记录IOAPIC ID
      p += sizeof(struct mpioapic);  //移到下一个表项
      continue;
    case MPBUS:
    case MPIOINTR:
    case MPLINTR:
      p += 8;
      continue;
    default:
      ismp = 0;
      break;
    }
  }
  if(!ismp)
    panic("Didn't find a suitable machine");

  if(mp->imcrp){
    // Bochs doesn't support IMCR, so this doesn't run on Bochs.
    // But it would on real hardware.
    outb(0x22, 0x70);   // Select IMCR
    outb(0x23, inb(0x23) | 1);  // Mask external interrupts.  没看懂这一步，注释给出的意思屏蔽外部中断，
  }                                                         //但是给出的操作是将IMCR寄存器最低位置1,IMCR低7位都没使用，所以？？？？
}
