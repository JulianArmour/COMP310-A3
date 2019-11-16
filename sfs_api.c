#include "sfs_api.h"

#include "disk_emu.h"
#include <string.h>

//function declarations
static void inodeTbl_init();

static void openFileTbl_init();

static const int MAXFNAMESIZE = 20;//maximum length of a file name (including 'period' and 'file extension'
static const int BLOCKSIZE = 1024;//size in bytes of a block
static const int MAXBLOCK = 256;//number of disk blocks
static const int INODEBLKS = 1;//number of "inode table" blocks (up to 255 files supported)
static const int INODEBLK = 1;//the Inode Table's block address
static const int FREEBMBLKS = 1;//number of "free bitmap" blocks
static const int ROOTDIRINODE = 0;//the inode id (index in the inode tbl) for the root directory

enum mode {MODE_DIR, MODE_BASIC};
typedef char Dir_Entry[MAXFNAMESIZE + sizeof(int)];//an entry in the root directory [filename|inode]
typedef struct {int inode; int read; int write;} FD;//a file descriptor

//In-memory data structures
int inodeTbl[256];//Inode Table cache (holds up to 256 inodes)
Dir_Entry dir[256];//Directory cache. (holds up to 256 files)
FD oft[256];//Open File Descriptor Table (holds up to 256 open files)
int freeMap[16];//Free Block Bitmap

void mksfs(int fresh) {
  //disk size: 256KiB (256 blocks)
  //max file size: 268 KiB (limited by disk size of course)
  //max number of files: 256
  //DISK STRUCTURE: [SUPER(1 block)|INODE-TBL(1)|FREE-BITMAP(1)|DATA-BLOCKS(253)]
  //INODE STRUCTURE: [mode|size|pointer1|...|pointer12|ind-pointer]
  int blockBuff[BLOCKSIZE/4];//temp buffer for writing blocks at FS creation
  if (fresh) {//insert initial filesystem data
    init_fresh_disk("sfs", BLOCKSIZE, MAXBLOCK);
    //init super block
    blockBuff[0] = BLOCKSIZE;// size in bytes of a block
    blockBuff[1] = MAXBLOCK;//number of filesystem blocks
    blockBuff[2] = INODEBLKS;//number of "inode table" blocks
    blockBuff[3] = FREEBMBLKS;//number of "free bitmap" blocks
    blockBuff[4] = ROOTDIRINODE;// root directory inode index
    write_blocks(0, 1, blockBuff);//set super block
    memset(blockBuff, 0, BLOCKSIZE);//reset blockBuff
    //set bits in free bitmap
    //1 super + 1 inode tbl block + 1 free bitmap block + 1 inode (for root dir)
    //+ 1 (root dir data) = 5 blocks
    //reserve first 5 blocks
    blockBuff[0] = -134217728;//(Decimal representation of 0xF8000000, or equivalently, 0b1111100...0)
    write_blocks(2, 1, blockBuff);//set free bitmap
    memset(blockBuff, 0, BLOCKSIZE);//reset blockBuff
    //init root directory's inode
    blockBuff[0] = MODE_DIR;
    blockBuff[2] = 4;//initial data block for root dir
    write_blocks(3, 1, blockBuff);//set root dir inode
    memset(blockBuff, 0, BLOCKSIZE);//reset blockBuff
    //add root directory’s inode in inode table
    blockBuff[0] = 3;//point root dir's inode #0 to block #3
    write_blocks(INODEBLK, 1, blockBuff);
    memset(blockBuff, 0, BLOCKSIZE);//reset blockBuff
  } else {
    init_disk("sfs", BLOCKSIZE, MAXBLOCK);
  }
  inodeTbl_init();//loads inode table into memory (cache)
  //load an open file descriptor table, with only the root DIR opened at index 0.
  openFileTbl_init();
  dirTblCache_init();//loads directory into memory (cache)
  freeBitmap_init();//loads the Free Data Block Bitmap into memory (cache)
}

static void openFileTbl_init() {

}

static void inodeTbl_init() {
  memset(inodeTbl, 0, sizeof(inodeTbl));
  read_blocks(INODEBLK, 1, inodeTbl);
}
