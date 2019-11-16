#include "sfs_api.h"

#include "disk_emu.h"
#include <string.h>

//function declarations
void inodeTbl_init();

enum mode {MODE_DIR, MODE_BASIC};

const int BLOCKSIZE = 1024;// size in bytes of a block
const int MAXBLOCK = 256;//number of disk blocks
const int INODEBLKS = 1;//number of "inode table" blocks
const int FREEBMBLKS = 1;//number of "free bitmap" blocks
const int ROOTDIRINODE = 0;//the inode id (index in the inode tbl) for the root directory

//In-memory data structures
int inodetbl[256];// Inode Table Cache


void mksfs(int fresh) {
  //disk size: 256KiB (256 blocks)
  //max file size: 268 KiB (limited by disk size)
  //max number of files: 256
  //DISK STRUCTURE: [SUPER(1 block)|I-NODES(1)|FREE-BITMAP(1)|DATA-BLOCKS(253)]
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
    //init free bitmap
    //1 super + 1 inode tbl block + 1 free bitmap block + 1 inode (for root dir)
    //+ 1 (root dir) = 5 blocks
    blockBuff[0] = 0xF8000000;//reserve first 5 blocks
    write_blocks(2, 1, blockBuff);//set free bitmap
    memset(blockBuff, 0, BLOCKSIZE);//reset blockBuff
    //init root directory inode
    blockBuff[0] = MODE_DIR;
    blockBuff[2] = 4;
    write_blocks(3, 1, blockBuff);//set root dir inode
    memset(blockBuff, 0, BLOCKSIZE);//reset blockBuff
    //set root directory’ inode in inode table
    blockBuff[0] = 3;
    write_blocks(1, 1, blockBuff);
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

void inodeTbl_init() {

}
