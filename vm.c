#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)    //设置内核用户的代码段和数据段
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];  //获取当前CPU
  //建立段描述符，内核态用户态的代码段和数据段 
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));     //加载到GDTR
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];   //va取高12位->页目录项
  if(*pde & PTE_P){        //若一级页表存在
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));   //取一级页表的物理地址，转化成虚拟地址
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)   //否则分配一页出来做页表
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;    //填写页目录项
  }
  return &pgtab[PTX(va)];  //返回页表项地址
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);    //虚拟地址va以4K为单位的下边界
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);   //偏移量，所以减1
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)   //获取地址a的页表项地址
      return -1;
    if(*pte & PTE_P)     //如果该页本来就存在
      panic("remap");
    *pte = pa | perm | PTE_P;   //填写地址a相应的页表项
    if(a == last)   //映射完了退出循环
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.   内核的映射方式,见文档的示意图
static struct kmap {     
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t* setupkvm(void)      //建立内核页表
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)    //分配一页作为页目录表
    return 0; 
  memset(pgdir, 0, PGSIZE);              //页目录表置0
  if (P2V(PHYSTOP) > (void*)DEVSPACE)    //PHYSTOP的地址不能高于DEVSPACE
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)    //映射4项，循环4次
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start, (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void kvmalloc(void)
{
  kpgdir = setupkvm();  //建立页表
  switchkvm();    //切换页表
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void switchkvm(void)
{
  lcr3(V2P(kpgdir));   //加载内核页表到cr3寄存器，cr3存放的是页目录物理地址
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;  //系统段
  mycpu()->ts.ss0 = SEG_KDATA << 3;   //更改SS为新栈的选择子
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;  //在TSS中记录内核栈地址
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;   //用户态禁止使用io指令
  ltr(SEG_TSS << 3);      //加载TSS段的选择子到TR寄存器
  lcr3(V2P(p->pgdir));  // switch to process's address space  换页表就是换地址空间
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();  //分配一页物理内存
  memset(mem, 0, PGSIZE);  //清零
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);  //映射到虚拟地址空间0-4KB
  memmove(mem, init, sz);  //将要运行的初始化程序搬到0-4KB
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)   //地址必须是对齐的
    panic("loaduvm: addr must be page aligned");
    
  for(i = 0; i < sz; i += PGSIZE){  
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)  //addr+i地址所在页的页表项地址
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);  //页表项记录的物理地址

    if(sz - i < PGSIZE)  //确定一次能够搬运的字节数
      n = sz - i;
    else
      n = PGSIZE;
      
    if(readi(ip, P2V(pa), offset+i, n) != n) //调用readi将数据搬运到地址P2V(pa)处
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)   //多分配一/几页物理内存，然后填写页表项
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);    //向上4K对齐

  for(; a < newsz; a += PGSIZE){
    mem = kalloc();     //分配物理内存
    if(mem == 0){       //如果分配失败
        cprintf("allocuvm out of memory\n");
        deallocuvm(pgdir, newsz, oldsz);  //回收newsz到oldsz这部分空间
        return 0;
    }
    memset(mem, 0, PGSIZE);   //清除内存中的数据
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){  //填写页表项建立映射
        cprintf("allocuvm out of memory (2)\n");   //如果建立映射失败
        deallocuvm(pgdir, newsz, oldsz);   //回收newsz到oldsz这部分空间
        kfree(mem);   //回收刚分配的mem这部分空间
        return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)    //释放一/几页物理内存，页表项清0
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);   //向上4K对齐
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);  //地址a所在页的页表项地址
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;  //
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);     //free 用户空间映射的物理内存
  for(i = 0; i < NPDENTRIES; i++){    //free 页表占用的物理内存 
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);   //free 页目录占用的物理内存
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)      //构造页表的内核部分，内核部分都是一样的
    return 0;
  for(i = 0; i < sz; i += PGSIZE){     //循环用户部分sz
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)  //返回这个地址所在页的页表项地址，判断是否存在
      panic("copyuvm: pte should exist");    //为0表示不存在,panic
    if(!(*pte & PTE_P))    //判断页表项的P位
      panic("copyuvm: page not present");  //如果是0表不存在，panic
    pa = PTE_ADDR(*pte);     //获取该页的物理地址
    flags = PTE_FLAGS(*pte);  //获取该页的属性
      
    if((mem = kalloc()) == 0)  //分配一页
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);  //复制该页数据
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {  //映射该物理页到新的虚拟地址
      kfree(mem);   //如果出错释放
      goto bad;
    }
  }
  return d;     //返回页目录虚拟地址
bad:
  freevm(d);    //释放页目录d指示的所有空间
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);   //根据页目录pgdir获取地址uva所在页的目录项地址
  if((*pte & PTE_P) == 0)   //如果不存在
    return 0;
  if((*pte & PTE_U) == 0)  //如果用户不能访问
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));  //返回该页对应的内核地址
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);  //va0在pgdir的映射下的内核地址
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);   //一次搬运的字节数
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);  //从buf搬运n字节
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

