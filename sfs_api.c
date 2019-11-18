/*
 * Simple File System
 *
 * Assumptions:
 *  - sizeof(int) = 32
 *
 */

#include "sfs_api.h"

#include "disk_emu.h"
#include <string.h>

static const int MAX_FNAME_SIZE = 20;//maximum length of a file name (including 'period' and 'file extension'
static const int BLOCK_SIZE = 1024;//size in bytes of a block
static const int MAX_BLOCK = 256;//number of disk blocks
static const int INODE_BLKS = 1;//number of "inode table" blocks
static const int INODE_BLK = 1;//the Inode Table's block address
static const int FREE_BM_BLKS = 1;//number of "free bitmap" blocks
static const int FREE_BM_BLK = 2;//the free block bitmap's block address
static const int ROOT_DIR_INODE = 0;//the inode id (index in the inode tbl) for the root directory
static const int MAX_FILES = 256;//maximum number of files sfs can create (including root)
static const int MAX_FILE_SIZE = BLOCK_SIZE * 268;//inode can hold 268 data blocks

enum mode {MODE_DIR, MODE_BASIC};
typedef char DirEntry[MAX_FNAME_SIZE + sizeof(int)];//an entry in the root directory [filename|inodeId]
typedef struct {int inode; int read; int write;} FD;//a file descriptor
typedef struct {enum mode mode; int size; int pointers[13];} Inode;

//In-memory data structures
int inodeTbl[MAX_FILES];//Inode Table cache (holds up to 256 inodes)
DirEntry dir[MAX_FILES];//Directory cache. (holds up to 256 files)
FD oft[MAX_FILES];//Open File Descriptor Table (holds up to 256 open files)
unsigned int freeMap[MAX_FILES / sizeof(int)];//Free Block Bitmap (256 / 32 = 8)

//function declarations
static void inodeTbl_init();

static void openFileTbl_init();

static Inode inodeTbl_get(int inodeId);

static void dirCache_init();

void freeBitmap_init();

static int allocBlk(int *buf);

static int min(int x, int y) {
  if (x < y) return x;
  else return y;
}

void mksfs(int fresh) {
  //disk size: 256KiB (256 blocks)
  //max file size: 268 KiB (limited by disk size of course)
  //max number of files: 256
  //DISK STRUCTURE: [SUPER(1 block)|INODE-TBL(1)|FREE-BITMAP(1)|DATA-BLOCKS(253)]
  //INODE STRUCTURE: [mode|size|pointer1|...|pointer12|ind-pointer]
  int blockBuff[BLOCK_SIZE / 4];//temp buffer for writing blocks at FS creation
  if (fresh) {//insert initial filesystem data
    init_fresh_disk("sfs", BLOCK_SIZE, MAX_BLOCK);
    //init super block
    blockBuff[0] = BLOCK_SIZE;// size in bytes of a block
    blockBuff[1] = MAX_BLOCK;//number of filesystem blocks
    blockBuff[2] = INODE_BLKS;//number of "inode table" blocks
    blockBuff[3] = FREE_BM_BLKS;//number of "free bitmap" blocks
    blockBuff[4] = ROOT_DIR_INODE;// root directory inode index
    write_blocks(0, 1, blockBuff);//set super block
    memset(blockBuff, 0, BLOCK_SIZE);//reset blockBuff
    //set bits in free bitmap
    //1 super + 1 inode tbl block + 1 free bitmap block + 1 inode (for root dir)
    //+ 1 (root dir data) = 5 blocks
    //reserve first 5 blocks
    blockBuff[0] = -134217728;//(Decimal representation of 0xF8000000, or equivalently, 0b1111100...0)
    write_blocks(FREE_BM_BLK, 1, blockBuff);//set free bitmap
    memset(blockBuff, 0, BLOCK_SIZE);//reset blockBuff
    //init root directory's inode
    blockBuff[0] = MODE_DIR;
    blockBuff[2] = 4;//initial data block for root dir
    write_blocks(3, 1, blockBuff);//set root dir inode
    memset(blockBuff, 0, BLOCK_SIZE);//reset blockBuff
    //add root directory’s inode in inode table
    blockBuff[0] = 3;//point root dir's inode #0 to block #3
    write_blocks(INODE_BLK, 1, blockBuff);
    memset(blockBuff, 0, BLOCK_SIZE);//reset blockBuff
  } else {
    init_disk("sfs", BLOCK_SIZE, MAX_BLOCK);
  }
  inodeTbl_init();//loads inode table into memory (cache)
  //load an open file descriptor table, with only the root DIR opened at index 0.
  openFileTbl_init();
  dirCache_init();//loads directory into memory (cache)
  freeBitmap_init();//loads the Free Data Block Bitmap into memory (cache)
}

/*Initializes the Free Bitmap cache by reading the disk's version of it.*/
void freeBitmap_init() {
  read_blocks(FREE_BM_BLK, FREE_BM_BLKS, freeMap);
}

/*TODO*/
int sfs_fread(int fileID, char *buf, int length) {
  FD file = oft[fileID];
  if (file.inode < 0) return 0;//file is not open
  Inode inode = inodeTbl_get(file.inode);
  //if read query exceeds maximum file size
  if (file.read + length > MAX_FILE_SIZE) {
    //then set length = remaining file space
    length = MAX_FILE_SIZE - file.read;
  }
  //read into buf from disk block by block.
  int bufIndex = 0;
  while (bufIndex < length) {
    int blockNum;//address of block being written to
    char blockBuff[BLOCK_SIZE];//buffer for data block
    int inodePointer = file.read / BLOCK_SIZE;
    //get blockNum for inodePointer, it will be <= 0 if it's not allocated
    if (inodePointer < 12) {//non-indirect pointer
      blockNum = inode.pointers[inodePointer];
    } else {//indirect pointer
      if (inode.pointers[12] <= 0) {//if no block allocated
        blockNum = -1;
      } else {
        //read indirect block into memory
        read_blocks(inode.pointers[12], 1, blockBuff);
        int *indirectBlock = (int *) blockBuff;
        int indirectPointer = inodePointer - 12;
        blockNum = indirectBlock[indirectPointer];
      }
    }
    /*now perform read*/
    //where the read pointer is within the block
    int blockReadPointer = file.read % BLOCK_SIZE;
    //read until either end of block or end of buffer
    int numBytes = min(BLOCK_SIZE - blockReadPointer, length - bufIndex);
    if (blockNum <= 0) {
      //no data block, treat as all-zero block
      memset(buf+bufIndex, 0, numBytes);
    } else {
      //there is a data block, read it into memory and transfer to buf
      read_blocks(blockNum, 1, blockBuff);
      memcpy(buf+bufIndex, blockBuff+blockReadPointer, numBytes);
    }
    file.read += numBytes;
    bufIndex += numBytes;
  }
  //update open file descriptor table
  oft[fileID] = file;
  return bufIndex;
}

int sfs_fwrite(int fileID, char *buf, int length) {
  FD file = oft[fileID];
  Inode inode = inodeTbl_get(file.inode);
  //if write query exceeds maximum file size
  if (file.write + length > MAX_FILE_SIZE)
    //set length = remaining file space
    length = MAX_FILE_SIZE - file.write;
  //write buf to disk block by block.
  int bufIndex = 0;
  while (bufIndex < length) {
    int blockNum;//address of block being writtern to
    char blockBuff[BLOCK_SIZE];//buffer for Data Block
    int inodePointer = file.write / BLOCK_SIZE;
    //get blockNum for inodePointer, allocate blocks as needed
    if (inodePointer < 12) {//none-indirect pointer
      if (inode.pointers[inodePointer] <= 0)//no block already allocated
        if(allocBlk(&inode.pointers[inodePointer], 1))//allocate 1 block
          return bufIndex;//disk out of memory
      blockNum = inode.pointers[inodePointer];
    } else {//indirect pointer
      if (inode.pointers[12] <= 0)//no block already allocated
        if(allocBlk(&inode.pointers[12], 1))//allocate 1 block
          return bufIndex;//disk out of memory
      //read indirect block into memory
      read_blocks(inode.pointers[12], 1, blockBuff);
      //check if block needs to be allocated
      int *indirectBlock = (int *) blockBuff;
      int indirectPointer = inodePointer - 12;
      if (indirectBlock[indirectPointer] <= 0) {//no block already allocated
        if (allocBlk(&indirectBlock[indirectPointer], 1))
          return bufIndex;//disk out of memory
        write_blocks(indirectBlock[indirectPointer], 1, blockBuff);
      }
      blockNum = indirectBlock[indirectPointer];
    }
    /*now perform write*/

    //where the write pointer is within the block
    int blockWritePointer = file.write % BLOCK_SIZE;
    //number of bytes to write, write until either end of block or end of buffer
    int numBytes = min(BLOCK_SIZE - blockWritePointer, length - bufIndex);
    //only read existing block if we don't overwrite the entire block
    if (numBytes < BLOCK_SIZE)
      read_blocks(blockNum, 1, blockBuff);
    memcpy(blockBuff+blockWritePointer, buf+bufIndex, numBytes);
    write_blocks(blockNum, 1, blockBuff);
    file.write += numBytes;
    bufIndex += numBytes;
  }
  //if data was appended, update file size
  if (file.write > inode.size)
    inode.size = file.write;
  //update open file descriptor table cache
  oft[fileID] = file;
  //update inode table cache
  inodeTbl_update(file.inodeIndex, inode);//updates the inode's Data Block
  return bufIndex;
}

static void freeMap_flush() {
  write_blocks(FREE_BM_BLK, 1, (char *) freeMap);
}

/*allocates a data block and writes its address to buf. Returns 0 on success, -1 on failure.*/
static int allocBlk(int *buf) {
  int addr = 0;
  //iterate over each int buffer in the freeMap cache
  for (unsigned long i = 0; i < (sizeof(freeMap) / sizeof(int)); ++i) {
    //check if one of the bits is 0
    if ((~freeMap[i]) > 0) {//one of the bits is 0
      unsigned int mask = 0x80000000;//0b10000000...0
      for (unsigned long j = 0; j < 8 * sizeof(int); ++j) {
        if (freeMap[i] & mask) {//block at address 'addr' is free
          *buf = addr;
          freeMap[i] |= mask;//reserve block in free bitmap by marking the bit
          freeMap_flush();
          return 0;
        } else {//block at address 'addr' not free, try next block
          mask >>= (unsigned int) 1;
          addr++;
        }
      }
    } else {//all bits are 1, try next chunk of bits
      addr += 8 * sizeof(int);
    }
  }
  *buf = -1;
  return -1;
}

/*Initializes the directory cache by reading the directory contents from the disk.*/
static void dirCache_init() {
  oft[0].read = 0;//set root dir's read pointer to beginning of file
  sfs_fread(0, (char *)dir, sizeof(dir));
}

/*Initializes the Open File Descriptor Table (OFT) in-memory data structure.
 * After initialization, the OFT will only contain 1 open file, the root directory.*/
static void openFileTbl_init() {
  //open the root dir file at initialization
  oft[0].inode = ROOT_DIR_INODE;
  oft[0].read = 0;
  oft[0].write = inodeTbl_get(ROOT_DIR_INODE).size;
  //all other entries are set to closed
  for (int i = 1; i < MAX_FILES; ++i) {
    oft[0].inode = -1;
    oft[0].read = 0;
    oft[0].write = 0;
  }
}

/*Given an index in the inode table (inodeId), this will return the corresponding inode data-structure,*/
static Inode inodeTbl_get(int inodeId) {
  Inode inode;
  int blk[BLOCK_SIZE/4];
  read_blocks(inodeTbl[inodeId], 1, blk);
  inode.mode = blk[0];
  inode.size = blk[1];
  for (int i = 2; i < 15; ++i) {
    inode.pointers[i-2] = blk[i];
  }
  return inode;
}

/*Initializes the inode table cache by reading the inode table from the disk.*/
static void inodeTbl_init() {
  memset(inodeTbl, 0, sizeof(inodeTbl));
  read_blocks(INODE_BLK, 1, inodeTbl);
}
