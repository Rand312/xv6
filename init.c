// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

int main(void)
{
  int pid, wpid;
 
  if(open("console", O_RDWR) < 0){ //打开控制台
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){  
    printf(1, "init: starting sh\n");
    pid = fork();   //fork一个子进程
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){      //子进程运行shell
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)  //wait()
      printf(1, "zombie!\n");
  }
}
