// Shell.

#include "types.h"
#include "user.h"
#include "fcntl.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit();

  switch(cmd->type){   //选择命令的类型
  default:             //支支持EXEC等几种命令，执行到默认情况就是出错了
    panic("runcmd");

  case EXEC:     //如果是EXEC命令
    ecmd = (struct execcmd*)cmd;  //强制类型转换为execcmd
    if(ecmd->argv[0] == 0)  //如果参数数组argv一个元素都没有
      exit();               //退出
    exec(ecmd->argv[0], ecmd->argv);  //调用exec函数执行这个命令
    printf(2, "exec %s failed\n", ecmd->argv[0]); //正常情况不会返回
    break;

  case REDIR:    //如果是重定向命令
    rcmd = (struct redircmd*)cmd;  //强制类型转换为重定向命令
    close(rcmd->fd);   //关闭标准输入或者标准输出
    if(open(rcmd->file, rcmd->mode) < 0){  //打开文件，描述符为0或1
      printf(2, "open %s failed\n", rcmd->file);
      exit();
    }
    runcmd(rcmd->cmd);  //递归调用runcmd执行命令
    break;

  case LIST:    //如果是LIST命令 
    lcmd = (struct listcmd*)cmd;  //强制类型转换为LIST命令
    if(fork1() == 0)      //fork出一个子进程运行左边的命令
      runcmd(lcmd->left);
    wait();               //等待左边的程序运行结束
    runcmd(lcmd->right);  //运行右边的命令
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){    //fork出一个子进程运行左边的命令
      close(1);          //关闭标准输出
      dup(p[1]);         //标准输出重定向到p[1]
      close(p[0]);       //关闭p[1]
      close(p[1]);       //关闭p[2]
      runcmd(pcmd->left);  //执行左边的命令
    }
    if(fork1() == 0){    //fork出一个子进程运行右边的命令
      close(0);          //关闭标准输入
      dup(p[0]);         //标准输入重定向到p[0]
      close(p[0]);       //关闭p[0]
      close(p[1]);       //关闭p[1]
      runcmd(pcmd->right);   //运行右边的命令
    }
    close(p[0]);    //关闭p[0]
    close(p[1]);    //关闭p[1]
    wait();      //等待子进程退出
    wait();      //等待子进程退出
    break;

  case BACK:    //如果是BACK后台命令
    bcmd = (struct backcmd*)cmd; //强制类型转换为BACK命令
    if(fork1() == 0)      //fork出一个子进程运行命令
      runcmd(bcmd->cmd);  //运行命令
    break;
  }
  exit();
}

int
getcmd(char *buf, int nbuf)  //从键盘(控制台)缓冲区获取输入
{
  printf(2, "$ ");     //打印$符号
  memset(buf, 0, nbuf);   //清空buf
  gets(buf, nbuf);    //从键盘(控制台)缓冲区获取输入，存到buf
  if(buf[0] == 0) // EOF  说明出现了截至符，也就是按下了ctrl+d
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){   //说明存在0，1，2这三个标准输入输出
      close(fd);   //关闭一个console文件,没真正关闭只是引用数减1
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){   //从控制台获取一个命令
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){   //如果是 cd 命令
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n 截断
      if(chdir(buf+3) < 0)     // 更换当前工作目录
        printf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)    //fork出一个子进程运行命令
      runcmd(parsecmd(buf));  //从字符串中解析出命令，然后运行
    wait();  //等待子进程退出
  }
  exit();  //shell退出，一般不会退出
}

void
panic(char *s)
{
  printf(2, "%s\n", s); //打印消息
  exit();   //退出
}

int
fork1(void)     //加了panic的fork
{
  int pid;

  pid = fork();     //fork
  if(pid == -1)     //如果出错panic
    panic("fork");
  return pid;     
}

//PAGEBREAK!
// Constructors

// struct execcmd {
//   int type;
//   char *argv[MAXARGS];
//   char *eargv[MAXARGS];
// };
struct cmd*
execcmd(void)   //
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));   //分配execmd需要的空间
  memset(cmd, 0, sizeof(*cmd)); //置0
  cmd->type = EXEC;             //设置命令类型为exec
  return (struct cmd*)cmd;      //强制类型转换成普通cmd
}


// struct redircmd {
//   int type;
//   struct cmd *cmd;
//   char *file;
//   char *efile;
//   int mode;
//   int fd;
// };
struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));    //分配redircmd命令需要的空间
  memset(cmd, 0, sizeof(*cmd));  //置0
  cmd->type = REDIR;             //命令类型为重定向REDIR
  cmd->cmd = subcmd;             //子命令
  cmd->file = file;              //重定向到哪个文件
  cmd->efile = efile;
  cmd->mode = mode;              //打开文件的格式
  cmd->fd = fd;                  //文件描述符
  return (struct cmd*)cmd;       //强制类型转换成普通cmd
}

// struct pipecmd {
//   int type;
//   struct cmd *left;
//   struct cmd *right;
// };
struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));    //分配pipecmd命令需要的空间
  memset(cmd, 0, sizeof(*cmd));  //置0
  cmd->type = PIPE;              //命令类型为PIPE
  cmd->left = left;              //管道左侧的命令
  cmd->right = right;            //管道右侧的命令
  return (struct cmd*)cmd;       //强制类型转换成普通cmd
}

// struct listcmd {
//   int type;
//   struct cmd *left;
//   struct cmd *right;
// };
struct cmd*
listcmd(struct cmd *left, struct cmd *right) 
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));    //分配listcmd命令需要的空间
  memset(cmd, 0, sizeof(*cmd));  //置0
  cmd->type = LIST;              //命令类型为LIST
  cmd->left = left;              //LIST左侧的命令
  cmd->right = right;            //右侧的命令
  return (struct cmd*)cmd;       //强制类型转换成普通cmd
}

// struct backcmd {
//   int type;
//   struct cmd *cmd;
// };
struct cmd*
backcmd(struct cmd *subcmd)  
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));    //分配listcmd命令需要的空间
  memset(cmd, 0, sizeof(*cmd));  //置0
  cmd->type = BACK;              //命令类型为BACK
  cmd->cmd = subcmd;             //子命令
  return (struct cmd*)cmd;       //强制类型转换成普通cmd
}


//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";   //空白字符
char symbols[] = "<|>&;()";        //特殊符号

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))   //跳过空白字符
    s++;
  if(q)
    *q = s;     //记录s跳过空白字符的第一个有效字符位置
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){    //如果是 >> 追加
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))  //如果不是空白字符也不是特殊符号跳过
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))  //再次跳过空白字符
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)   //想看看这字符串里有无 toks
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))   //跳过空白字符
    s++;
  *ps = s;
  return *s && strchr(toks, *s);    
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);


//runcmd(parsecmd(buf));
struct cmd*
parsecmd(char *s)   
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    printf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
