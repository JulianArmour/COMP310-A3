/*
 * Simple File System
 *
 * Author: Julian Armour
 *
 * disk size: 256KiB (256 blocks)
 * max file size: 268 KiB (limited by disk size of course)
 * max number of files: 256
 * DISK STRUCTURE: [SUPER(1 block)|INODE-TBL(1)|FREE-BITMAP(1)|DATA-BLOCKS(253)]
 * INODE STRUCTURE: [mode|size|pointer1|...|pointer12|ind-pointer]
 */

#include "sfs_api.h"

#include "disk_emu.h"
#include <string.h>

static const int MAX_FNAME_SIZE = 20;//maximum length of a file name (including 'period' and 'file extension'
static const int BLOCK_BYTES = 1024;//size in bytes of a block
static const int BLOCK_COUNT = 256;//number of disk blocks
static const int INODE_BLKS = 1;//number of "inode table" blocks
static const int INODE_BLK = 1;//the Inode Table's block address
static const int FREE_BM_BLKS = 1;//number of "free bitmap" blocks
static const int FREE_BM_BLK = 2;//the free block bitmap's block address
static const int ROOT_DIR_INODE = 0;//the inode id (index in the inode tbl) for the root directory
static const int MAX_FILES = 256;//maximum number of files sfs can create (including root)
static const int MAX_FILE_SIZE = BLOCK_BYTES * 268;//inode can hold 268 data blocks
static const int MODE_DIR = 1;//Directory file mode
static const int MODE_BASIC = 2;//Basic file mode

typedef struct {int inodeID; int read; int write;} FD;//a file descriptor
typedef struct {int mode; int size; int pointers[13];} Inode;

/*In-memory data structures*/
int inodeTbl[MAX_FILES];//Inode Table cache (holds up to 256 inodes)
//Directory cache. (holds up to 256 files)
//an entry in the directory is in format [filename|inodeId]
char dir[MAX_FILES][MAX_FNAME_SIZE + sizeof(int)];
int dir_ptr = 0;

FD oft[MAX_FILES];//Open File Descriptor Table (holds up to 256 open files)

unsigned int freeMap[MAX_FILES / sizeof(int)];//Free Block Bitmap (256 / 32 = 8)

//function declarations
static void inodeTbl_init();

static void openFileTbl_init();

static Inode fetchInode(int inodeId);

static void dirCache_init();

static void freeBitmap_init();

static int allocBlk(int *buf);

/*Not depending on the math lib in case you're using bash file to auto-grade*/
static int min(int x, int y) {
  if (x < y) return x;
  else return y;
}

/*Places the name of the next file in the directory in fname. Returns 0 on success, -1 on failure*/
int sfs_getnextfilename(char *fname) {
  //check, starting at dir_ptr, each entry in the directory table for a valid file name
  for (int entriesChecked = 0; entriesChecked < MAX_FILES; ++entriesChecked) {
    char *entry = dir[dir_ptr];
    dir_ptr = (dir_ptr + 1) % MAX_FILES;
    if (entry[0] != '\0') {
      memcpy(fname, entry, MAX_FNAME_SIZE);
      return 0;
    }
  }
  return -1;
}

/*Searches for a file with the name fname in the directory. If found, return's it's index, else returns -1.*/
int searchFile(const char *fname) {
  //check each entry in the directory
  for (int dirIndex = 0; dirIndex < MAX_FILES; ++dirIndex) {
    if (memcmp(fname, dir[dirIndex], MAX_FNAME_SIZE) == 0)
      return dirIndex;
  }
  return -1;
}

/*returns the inode ID from the entry at dirIndex in the root directory.*/
int inodeID_from_dirIndex(int dirIndex) {
  char *entry = dir[dirIndex];
  int *inodeIdPtr = (int *) (entry + MAX_FNAME_SIZE);
  return *inodeIdPtr;
}

/*Opens a file with the given name, returns a File Descriptor ID >= 0. returns -1 on failure.*/
int sfs_fopen(char *name) {
  //search for file name
  int fileDirIndex = searchFile(name);
  //get it's inode ID
  //find a free slot in the OFT
  //place in free slot
  //return index of slot
}

/*given the file name path, returns the size of the file. returns -1 if the file doesn't exist.*/
int sfs_getfilesize(const char* path) {
  //search directory for file name `path`
  int dirEntryIndex = searchFile(path);
  if (dirEntryIndex == -1) return -1;//file does not exist
  int inodeId = inodeID_from_dirIndex(dirEntryIndex);
  Inode fileInode = fetchInode(inodeId);
  return fileInode.size;
}

void mksfs(int fresh) {
  //disk size: 256KiB (256 blocks)
  //max file size: 268 KiB (limited by disk size of course)
  //max number of files: 256
  //DISK STRUCTURE: [SUPER(1 block)|INODE-TBL(1)|FREE-BITMAP(1)|DATA-BLOCKS(253)]
  //INODE STRUCTURE: [mode|size|pointer1|...|pointer12|ind-pointer]
  int blockBuff[BLOCK_BYTES / 4];//temp buffer for writing blocks at FS creation
  if (fresh) {//insert initial filesystem data
    init_fresh_disk("sfs", BLOCK_BYTES, BLOCK_COUNT);
    //init super block
    blockBuff[0] = BLOCK_BYTES;// size in bytes of a block
    blockBuff[1] = BLOCK_COUNT;//number of filesystem blocks
    blockBuff[2] = INODE_BLKS;//number of "inode table" blocks
    blockBuff[3] = FREE_BM_BLKS;//number of "free bitmap" blocks
    blockBuff[4] = ROOT_DIR_INODE;// root directory inode index
    write_blocks(0, 1, blockBuff);//set super block
    memset(blockBuff, 0, BLOCK_BYTES);//reset blockBuff
    //set bits in free bitmap
    //1 super + 1 inode tbl block + 1 free bitmap block + 1 inode (for root dir)
    //+ 1 (root dir data) = 5 blocks
    //reserve first 5 blocks
    blockBuff[0] = -134217728;//(Decimal representation of 0xF8000000, or equivalently, 0b1111100...0)
    write_blocks(FREE_BM_BLK, 1, blockBuff);//set free bitmap
    memset(blockBuff, 0, BLOCK_BYTES);//reset blockBuff
    //init root directory's inode
    blockBuff[0] = MODE_DIR;
    blockBuff[2] = 4;//initial data block for root dir
    write_blocks(3, 1, blockBuff);//set root dir inode
    memset(blockBuff, 0, BLOCK_BYTES);//reset blockBuff
    //add root directory’s inode in inode table
    blockBuff[0] = 3;//point root dir's inode #0 to block #3
    write_blocks(INODE_BLK, 1, blockBuff);
    memset(blockBuff, 0, BLOCK_BYTES);//reset blockBuff
  } else {
    init_disk("sfs", BLOCK_BYTES, BLOCK_COUNT);
  }
  inodeTbl_init();//loads inode table into memory (cache)
  //load an open file descriptor table, with only the root DIR opened at index 0.
  openFileTbl_init();
  dirCache_init();//loads directory into memory (cache)
  freeBitmap_init();//loads the Free Data Block Bitmap into memory (cache)
}

/*Initializes the Free Bitmap cache by reading the disk's version of it.*/
void static freeBitmap_init() {
  read_blocks(FREE_BM_BLK, FREE_BM_BLKS, freeMap);
}

/*Updates the on-disk inode data-structure with in-memory inode.*/
void flushInode(int inodeID, Inode inode) {
  int blk[BLOCK_BYTES / sizeof(int)];
  int inodeBlkAddr = inodeTbl[inodeID];
  read_blocks(inodeBlkAddr, 1, (char *) blk);
  //encode inode to block
  blk[0] = inode.mode;
  blk[1] = inode.size;
  for (int i = 0; i < 13; ++i) {
    blk[i+2] = inode.pointers[i];
  }
  write_blocks(inodeBlkAddr, 1, blk);
}

/*Given an index in the inode table (inodeId), this will return the corresponding inode data-structure,*/
static Inode fetchInode(int inodeId) {
  Inode inode;
  int blk[BLOCK_BYTES / 4];
  read_blocks(inodeTbl[inodeId], 1, blk);
  //parse inode block
  inode.mode = blk[0];
  inode.size = blk[1];
  for (int i = 0; i < 13; ++i) {
    inode.pointers[i] = blk[i+2];
  }
  return inode;
}

/*Given a fileID, reads in length bytes from the file to buf*/
int sfs_fread(int fileID, char *buf, int length) {
  FD file = oft[fileID];
  if (file.inodeID < 0) return 0;//file is not open
  Inode inode = fetchInode(file.inodeID);
  //if read query exceeds maximum file size
  if (file.read + length > MAX_FILE_SIZE) {
    //then set length = remaining file space
    length = MAX_FILE_SIZE - file.read;
  }
  //read into buf from disk block by block.
  int bufIndex = 0;
  while (bufIndex < length) {
    int blockNum;//address of block being written to
    char blockBuff[BLOCK_BYTES];//buffer for data block
    int inodePointer = file.read / BLOCK_BYTES;
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
    int blockReadPointer = file.read % BLOCK_BYTES;
    //read until either end of block or end of buffer
    int numBytes = min(BLOCK_BYTES - blockReadPointer, length - bufIndex);
    if (blockNum <= 0) {
      //no data block, treat as all-zero block
      memset(&buf[bufIndex], 0, numBytes);
    } else {
      //there is a data block, read it into memory and transfer to buf
      read_blocks(blockNum, 1, blockBuff);
      memcpy(&buf[bufIndex], blockBuff+blockReadPointer, numBytes);
    }
    file.read += numBytes;
    bufIndex += numBytes;
  }
  //update open file descriptor table
  oft[fileID] = file;
  return bufIndex;
}

/*Given a fileID, writes length bytes from buf to the file*/
int sfs_fwrite(int fileID, char *buf, int length) {
  FD file = oft[fileID];
  Inode inode = fetchInode(file.inodeID);
  //if write query exceeds maximum file size
  if (file.write + length > MAX_FILE_SIZE)
    //set length = remaining file space
    length = MAX_FILE_SIZE - file.write;
  //write buf to disk block by block.
  int bufIndex = 0;
  while (bufIndex < length) {
    int blockNum;//address of block being written to
    char blockBuff[BLOCK_BYTES];//buffer for Data Block
    int inodePointer = file.write / BLOCK_BYTES;
    //get blockNum for inodePointer, allocate blocks as needed
    if (inodePointer < 12) {//none-indirect pointer
      if (inode.pointers[inodePointer] <= 0)//no block already allocated
        if(allocBlk(&inode.pointers[inodePointer]))//allocate 1 block
          return bufIndex;//disk out of memory
      blockNum = inode.pointers[inodePointer];
    } else {//indirect pointer
      if (inode.pointers[12] <= 0)//no block already allocated
        if(allocBlk(&inode.pointers[12]))//allocate 1 block
          return bufIndex;//disk out of memory
      //read indirect block into memory
      read_blocks(inode.pointers[12], 1, blockBuff);
      //check if block needs to be allocated
      int *indirectBlock = (int *) blockBuff;
      int indirectPointer = inodePointer - 12;
      if (indirectBlock[indirectPointer] <= 0) {//no block already allocated
        if (allocBlk(&indirectBlock[indirectPointer]))
          return bufIndex;//disk out of memory
        write_blocks(indirectBlock[indirectPointer], 1, blockBuff);
      }
      blockNum = indirectBlock[indirectPointer];
    }
    /*now perform write*/

    //where the write pointer is within the block
    int blockWritePointer = file.write % BLOCK_BYTES;
    //number of bytes to write, write until either end of block or end of buffer
    int numBytes = min(BLOCK_BYTES - blockWritePointer, length - bufIndex);
    //only read existing block if we don't overwrite the entire block
    if (numBytes < BLOCK_BYTES)
      read_blocks(blockNum, 1, blockBuff);
    memcpy(&blockBuff[blockWritePointer], &buf[bufIndex], numBytes);
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
  flushInode(file.inodeID, inode);//updates the inode's Data Block
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
  oft[0].inodeID = ROOT_DIR_INODE;
  oft[0].read = 0;
  oft[0].write = fetchInode(ROOT_DIR_INODE).size;
  //all other entries are set to closed
  for (int i = 1; i < MAX_FILES; ++i) {
    oft[0].inodeID = -1;
    oft[0].read = 0;
    oft[0].write = 0;
  }
}

/*Initializes the inode table cache by reading the inode table from the disk.*/
static void inodeTbl_init() {
  memset(inodeTbl, 0, sizeof(inodeTbl));
  read_blocks(INODE_BLK, 1, inodeTbl);
}
