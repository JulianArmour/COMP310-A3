void Fmksfs(int fresh) {
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



int Fsfs_fwrite(int fileID, char *buf, int length) {
  openFile_t file = openFDTbl_get(fileID);
  inode_t inode = inodeTbl_get(file.inodeIndex);
  //if write query exceeds maximum file size
  if (file.writePointer + length > MAXFILESIZE)
    //set length = remaining file space
    length = MAXFILESIZE - file.writePointer;
  //write buf to disk block by block.
  int bufIndex = 0;
  while (bufIndex < length) {
    int blockNum;//address of block being writtern to
    char blockBuff[BLOCKSIZE];//buffer for Data Block
    int inodePointer = file.writePointer / BLOCKSIZE;
    //get blockNum for inodePointer, allocate blocks as needed
    if (inodePointer < 12) {//none-indirect pointer
      if (inode.pointers[inodePointer] <= 0)//no block already allocated
        if(freeBitmap_aloc(&inode.pointers[inodePointer], 1))//allocate 1 block
          return bufIndex;//disk out of memory
      blockNum = inode.pointers[inodePointer];
    } else {//indirect pointer
      if (inode.pointers[12] <= 0)//no block already allocated
        if(freeBitmap_aloc(&inode.pointers[12], 1))//allocate 1 block
          return bufIndex;//disk out of memory
      //read indirect block into memory
      read_blocks(inode.pointers[12], 1, blockBuff);
      //check if block needs to be allocated
      int *indirectBlock = (int *) blockBuff;
      int indirectPointer = inodePointer - 12;
      if (indirectBlock[indirectPointer] <= 0) {//no block already allocated
        if (freeBitmap_aloc(&indirectBlock[indirectPointer], 1))
          return bufIndex;//disk out of memory
        write_blocks(indirectBlock[indirectPointer], 1, blockBuff);
      }
      blockNum = indirectBlock[indirectPointer];
    }
    /*now perform write*/

    //where the write pointer is within the block
    int blockWritePointer = file.writePointer % BLOCKSIZE;
    //number of bytes to write, write until either end of block or end of buffer
    int numBytes = min(BLOCKSIZE - blockWritePointer, length - bufIndex);
    //only read existing block if we don't overwrite the entire block
    if (numBytes < BLOCKSIZE)
      read_blocks(blockNum, 1, blockBuff);
    memcpy(blockBuff+blockWritePointer, buf+bufIndex, numBytes);
    write_blocks(blockNum, 1, blockBuff);
    file.writePointer += numBytes;
    bufIndex += numBytes;
  }
  //if data was appended, update file size
  if (file.writePointer > inode.size)
      inode.size = file.writePointer;
  //update open file descriptor table cache
  openFDTbl_update(fileID, file);
  //update inode table cache
  inodeTbl_update(file.inodeIndex, inode);//updates the inode's Data Block
  return bufIndex;
}


int Fsfs_fread(int fileID, char *buf, int length) {
  openFile_t file = openFDTbl_get(fileID);
  inode_t inode = inodeTbl_get(file.inodeIndex);
  //if read query exceeds maximum file size
  if (file.readPointer + length > MAXFILESIZE) {
    //then set length = remaining file space
    length = MAXFILESIZE - file.readPointer;
  }
  //read into buf from disk block by block.
  int bufIndex = 0;
  while (bufIndex < length) {
    int blockNum;//address of block being writtern to
    char blockBuff[BLOCKSIZE];//buffer for data block
    int inodePointer = file.readPointer / BLOCKSIZE;
    //get blockNum for inodePointer, it will be <= 0 if it's not allocated
    if (inodePointer < 12) {//non-indirect pointer
      blockNum = inode.pointers[inodePointer];
    } else {//indirect pointer
      if (inodePointer[12] <= 0) {//if no block allocated
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
    int blockReadPointer = file.readPointer % BLOCKSIZE;
    //read until either end of block or end of buffer
    int numbytes = min(BLOCKSIZE - blockReadPointer, length - bufIndex);
    if (blockNum <= 0) {
      //no data block, treat as all-zero block
      memset(buf+bufIndex, 0, numBytes);
    } else {
      //there is a data block, read it into memory and transfer to buf
      read_blocks(blockNum, 1, blockBuff);
      memcpy(buf+bufIndex, blockBuff+blockReadPointer, numBytes);
    }
    file.readPointer += numBytes;
    bufIndex += numBytes;
  }
  //update open file descriptor table cache
  openFDTbl_update(fileID, file);
  return bufIndex;
}

int closesfs() {
  for (int i = 0; i < MAXOPENFILES; i++) {
    //if file is open, close it.
    if (openFileTbl[i].inodeIndex >= 0)
      sfs_fclose(i);
  }
}


int Fsfs_fopen(char *name) {
  int fileInode;
  if ((fileInode = dirTbl_getInode(name)) <= 0) {//file doesn't exist
    fileInode = inodeTbl_findFree();//find free inode
    if (fileInode == -1) return -1;//no free inode
    //find a free data block and get its index
    int inodeBlock;//inode block buffer
    if (freeBitmap_aloc(&inodeBlock, 1) == -1) return -1;
    inodeTbl_reserve(fileInode, inodeBlock);//updates cache and flushes to disk
    dirTbl_insert(name, fileInode);//updates cache and flushes to disk
  }
  inode_t inode = inodeTbl_get(fileInode);
  int oftIndex = openFileTbl_find(fileInode);
  if (oftIndex < 0)//if file not already open
    oftIndex = openFileTbl_add(fileInode, 0, inode.size);//add new open file
  else //file is already opened, only update read and write pointers
    openFileTbl_put(oftIndex, fileInode, 0, inode.size);
  return oftIndex;
}
