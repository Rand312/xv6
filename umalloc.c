#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void
free(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1;   //要free的块的头部
  //从上一次找到空闲块的地方开始，终止条件:要回收的块位置是否在两个空闲块之间
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)  
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))  //如果要回收的块在链头或链尾
      break;
  if(bp + bp->s.size == p->s.ptr){   //当前要回收的块与下个块相邻
    bp->s.size += p->s.ptr->s.size;  //修改当前块大小
    bp->s.ptr = p->s.ptr->s.ptr;     //合并:当前块指向下一块的下一块
  } else
    bp->s.ptr = p->s.ptr;     //不相邻，直接指向下一块
  if(p + p->s.size == bp){    //当前要回收的块与上个块相邻
    p->s.size += bp->s.size;  //修改上个块的大小
    p->s.ptr = bp->s.ptr;     //合并:上个块指向下个块
  } else
    p->s.ptr = bp;   //上个块指向当前块
  freep = p;   //freep设为刚回收的这个块的上一块
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)  //如果申请小于4096个单元
    nu = 4096;   //申请4096个单元
  p = sbrk(nu * sizeof(Header));  //向内核申请空间
  if(p == (char*)-1)  //分配失败
    return 0;
  hp = (Header*)p;   //申请的空间地址
  hp->s.size = nu;   //初始化空间大小
  free((void*)(hp + 1));  //将申请到的空间加进原本的空闲链表
  return freep;  //返回
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){    //freep若为0，表示第一次malloc，什么都还没有
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){  //从freep开始寻找空闲块
    if(p->s.size >= nunits){     //找到的空闲块足够大
      if(p->s.size == nunits)    //如果大小刚好合适
        prevp->s.ptr = p->s.ptr; //那么只需要改变一下指针
      else {                     //如果比申请的大
        p->s.size -= nunits;     //将该块切割，尾部分配出去
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;           //记录找到空闲块的位置
      return (void*)(p + 1);   //将该块分配给用户空间，返回给用户的部分不包括头部，所以加1
    }
    if(p == freep)   //没有找到合适的块
      if((p = morecore(nunits)) == 0)  //向内核申请nunits大小的空间
        return 0;
  }
}
