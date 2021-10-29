// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;         // Size of file system image (blocks) 文件系统大小，也就是一共多少块
  uint nblocks;      // Number of data blocks  数据块数量
  uint ninodes;      // Number of inodes.   //i结点数量
  uint nlog;         // Number of log blocks   //日志块数量  
  uint logstart;     // Block number of first log block  //第一个日志块块号 
  uint inodestart;   // Block number of first inode block  //第一个i结点所在块号
  uint bmapstart;    // Block number of first free map block  //第一个位图块块号
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

// Inodes per block.  每个块能有多少个i结点
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i   i结点在哪个块
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block    每个块能有多少个bit
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b    块b在哪个数据位图块上
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {        //目录项结构
  ushort inum;         //inode编号
  char name[DIRSIZ];   //文件名
};

