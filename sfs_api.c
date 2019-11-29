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

unsigned int freeMap[BLOCK_COUNT / (sizeof(int)*8)];//Free Block Bitmap (256 / 32 = 8)

//function declarations
static void inodeTbl_init();

static void oft_init();

static Inode fetchInode(int inodeId);

static void flushInode(int inodeID, Inode inode);

static void dir_init();

static void freeBitmap_init();

static int allocBlk();

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
static int dir_find(const char *fname) {
  //check each entry in the directory
  for (int dirIndex = 0; dirIndex < MAX_FILES; ++dirIndex) {
    if (memcmp(fname, dir[dirIndex], MAX_FNAME_SIZE) == 0)
      return dirIndex;
  }
  return -1;
}

/*Searches for a free entry in the dir cache and returns its index. returns -1 on failure.*/
static int dir_findFree() {
  for (int dirEntry = 0; dirEntry < MAX_FILES; ++dirEntry) {
    if (dir[dirEntry][0] == '\0')// '\0' as the first character denotes an unused entry
      return dirEntry;
  }
  return -1;
}

/*returns the inode ID from the entry at dirIndex in the root directory.*/
static int inodeID_from_dirIndex(int dirIndex) {
  char *entry = dir[dirIndex];
  int *inodeIdPtr = (int *) (entry + MAX_FNAME_SIZE);
  return *inodeIdPtr;
}

/*returns the index of a free slot in oft (Open File Table). Returns -1 if all slots are taken.*/
static int oft_findFree() {
  for (int entry = 0; entry < MAX_FILES; ++entry) {
    FD fd = oft[entry];
    if (fd.inodeID == -1)//this fd entry is unused
      return entry;
  }
  return -1;
}

static int oft_find(int inodeID) {
  for (int entry = 0; entry < MAX_FILES; ++entry) {
    if (oft[entry].inodeID == inodeID)
      return entry;
  }
  return -1;
}

/*Finds and returns the ID of a free inode in the inode table. -1 on failure.*/
static int inodeTbl_findFree() {
  for (int inodeID = 0; inodeID < MAX_FILES; ++inodeID) {
    if (inodeTbl[inodeID] <= 0)// (-inf,0] means free
      return inodeID;
  }
  return -1;
}


static void inodeTbl_flush() {
  write_blocks(INODE_BLK, INODE_BLKS, inodeTbl);
}

static void dir_flush() {
  oft[0].write = 0;//set root directory's write ptr to beginning of file
  sfs_fwrite(0, (char *) dir, sizeof(dir));
}

/*Creates a file with the given name and returns it's inode ID, or -1 on failure.*/
static int createFile(char* name) {
  int newInodeID = inodeTbl_findFree();
  int freeDirEntry = dir_findFree();
  if (newInodeID < 0) return -1;//no more free inodes
  if (freeDirEntry < 0) return -1;//no more room in directory
  //allocate a block for the inode
  int inodeBlock;
  if ((inodeBlock = allocBlk()) == -1) return -1;//failed to allocate block
  //reserve the inode
  inodeTbl[newInodeID] = inodeBlock;
  inodeTbl_flush();
  //reserve directory entry
  memcpy(dir[freeDirEntry], name, 20);
  memcpy(&dir[freeDirEntry][20], &newInodeID, sizeof(int));
  dir_flush();
  //set inode metadata
  Inode newInode;
  newInode.mode = MODE_BASIC;
  flushInode(newInodeID, newInode);
  return newInodeID;
}

/*Opens a file with the given name, tries to create a new file if it does not exist. Returns a File Descriptor ID >= 0.
 * returns -1 on failure.*/
int sfs_fopen(char *name) {
  int inodeID;
  //search for file name
  int fileDirIndex = dir_find(name);
  //check if file exists
  if (fileDirIndex == -1) {//file doesn't exist
    inodeID = createFile(name);
    if (inodeID < 0) return -1;//error creating file
  } else {//file exists
    //get it's inode ID
    inodeID = inodeID_from_dirIndex(fileDirIndex);
    if (oft_find(inodeID) != -1) return -1;//file is already open
  }
  //find a free slot in the OFT
  int freeOFTSlot = oft_findFree();
  if (freeOFTSlot == -1) return -1;//OFT is full
  //place data in free slot
  Inode fileInode = fetchInode(inodeID);
  oft[freeOFTSlot].inodeID = inodeID;
  oft[freeOFTSlot].write = fileInode.size;
  oft[freeOFTSlot].read = 0;
  //return index of slot (FD handle)
  return freeOFTSlot;
}

/*closes an opened file. Returns 0 on success, -1 on failure.*/
int sfs_fclose(int fileID) {
  if (fileID < 0 || MAX_FILES <= fileID) return -1;//fileID out of permitted bounds
  if (oft[fileID].inodeID < 0) return -1;//verify that the file is open.
  //file is open, close it.
  oft[fileID].inodeID = -1;// -1 denotes that the file is closed
  return 0;
}

/*Moves the open file's read pointer to the location loc*/
int sfs_frseek(int fileID, int loc) {
  if (fileID < 0 || MAX_FILES <= fileID) return -1;//fileID out of permitted bounds
  oft[fileID].read = loc;
  return 0;
}

/*Moves the open file's write pointer to the location loc*/
int sfs_fwseek(int fileID, int loc) {
  if (fileID < 0 || MAX_FILES <= fileID) return -1;//fileID out of permitted bounds
  oft[fileID].write = loc;
  return 0;
}

/*given the file name path, returns the size of the file. returns -1 if the file doesn't exist.*/
int sfs_getfilesize(const char* path) {
  //search directory for file name `path`
  int dirEntryIndex = dir_find(path);
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
  oft_init();
  dir_init();//loads directory into memory (cache)
  freeBitmap_init();//loads the Free Data Block Bitmap into memory (cache)
}

/*Initializes the Free Bitmap cache by reading the disk's version of it.*/
void static freeBitmap_init() {
  read_blocks(FREE_BM_BLK, FREE_BM_BLKS, freeMap);
}

/*Updates the on-disk inode data-structure with in-memory inode.*/
static void flushInode(int inodeID, Inode inode) {
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
  if (fileID < 0 || MAX_FILES <= fileID) return 0;//fileID out of permitted bounds
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
  if (fileID < 0 || MAX_FILES <= fileID) return 0;//fileID out of permitted bounds
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
        if((inode.pointers[inodePointer] = allocBlk()) < 0)//allocate 1 block
          return bufIndex;//disk out of memory
      blockNum = inode.pointers[inodePointer];
    } else {//indirect pointer
      if (inode.pointers[12] <= 0)//no block already allocated
        if((inode.pointers[12] = allocBlk()) < 0)//allocate 1 block
          return bufIndex;//disk out of memory
      //read indirect block into memory
      read_blocks(inode.pointers[12], 1, blockBuff);
      //check if block needs to be allocated
      int *indirectBlock = (int *) blockBuff;
      int indirectPointer = inodePointer - 12;
      if (indirectBlock[indirectPointer] <= 0) {//no block already allocated
        if ((indirectBlock[indirectPointer] = allocBlk()) < 0)
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

/*allocates a data block and writes its address to buf. Returns the block number on success, -1 on failure.*/
static int allocBlk() {
  int addr = 0;
  //iterate over each int buffer in the freeMap cache
  for (unsigned long i = 0; i < (sizeof(freeMap) / sizeof(int)); ++i) {
    //check if one of the bits is 0
    if ((~freeMap[i]) > 0) {//one of the bits is 0
      unsigned int mask = 0x80000000;//0b10000000...0
      for (unsigned long j = 0; j < 8 * sizeof(int); ++j) {
        if (freeMap[i] & mask) {//block at address 'addr' is free
          freeMap[i] |= mask;//reserve block in free bitmap by marking the bit
          freeMap_flush();
          return addr;
        } else {//block at address 'addr' not free, try next block
          mask >>= (unsigned int) 1;
          addr++;
        }
      }
    } else {//all bits are 1, try next chunk of bits
      addr += 8 * sizeof(int);
    }
  }
  return -1;
}

static void freeBlk(int blockNum) {
  //used to clear data in the block being freed
  static char blank[BLOCK_BYTES];//static declaration ensures all entries are initialized to 0
  //clear the block's data
  write_blocks(blockNum, 1, blank);
  //get the index (chunk) in the freeMap cache
  int chunk = blockNum / (sizeof(int) * 8);
  //get the bit in the chunk that represents to block
  unsigned int chunkOffset = blockNum % (sizeof(int) * 8);
  //bit-mask used to flip bit representing blockNum to 0
  unsigned int mask = ~((unsigned int)1<<chunkOffset);//111..0..111
  freeMap[chunk] &= mask;//flip the bit from 1 to 0
  freeMap_flush();
}

int sfs_remove(char *file) {
  int dirEntry = dir_find(file);
  if (dirEntry < 0) return -1;
  int inodeID = inodeID_from_dirIndex(dirEntry);
  Inode inode = fetchInode(inodeID);
  //free direct pointer blocks
  for (int pointer = 0; pointer < 12; ++pointer) {
    freeBlk(inode.pointers[pointer]);
  }
}

/*Initializes the directory cache by reading the directory contents from the disk.*/
static void dir_init() {
  oft[0].read = 0;//set root dir's read pointer to beginning of file
  sfs_fread(0, (char *)dir, sizeof(dir));
}

/*Initializes the Open File Descriptor Table (OFT) in-memory data structure.
 * After initialization, the OFT will only contain 1 open file, the root directory.*/
static void oft_init() {
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
