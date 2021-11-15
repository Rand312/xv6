#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void ls(char *path)  //显示这个路径指示的文件信息  
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){   //打开文件
    printf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){   //获取这个文件信息
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){    //如果是普通文件，直接输出
  case T_FILE:
    printf(1, "%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
    break;

  case T_DIR:    //如果是目录文件，输出其下的文件信息
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){  //判断路径是否对头,buf 512字节,正常情况不会超过
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);   //将参数复制一份到buf
    p = buf+strlen(buf); //p现在应指向参数路径的末尾
    *p++ = '/';   //末尾赋为'/'
    while(read(fd, &de, sizeof(de)) == sizeof(de)){  //读取目录项
      if(de.inum == 0)  //inode编号为0 continue(inode编号从1开始)
        continue;
      memmove(p, de.name, DIRSIZ);   //复制一份文件名字到p，形成新路径，这个路径指向目录下的一个文件
      p[DIRSIZ] = 0;    //截止处理
      if(stat(buf, &st) < 0){    //获取这个文件的状态信息
        printf(1, "ls: cannot stat %s\n", buf);  //打印错误消息
        continue;
      }
      printf(1, "%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);  //打印文件信息
    }
    break;
  }
  close(fd);  //关闭文件
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){   //如果参数个数小于2，也就是没有参数，只有一个程序名
    ls(".");      //那就显示当前目录下的文件信息
    exit();
  }
  for(i=1; i<argc; i++)   //对每个参数(文件)调用ls
    ls(argv[i]);
  exit();    //执行完后退出
}
