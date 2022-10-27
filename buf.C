#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include "page.h"
#include "buf.h"

#define ASSERT(c)  { if (!(c)) { \
		       cerr << "At line " << __LINE__ << ":" << endl << "  "; \
                       cerr << "This condition should hold: " #c << endl; \
                       exit(1); \
		     } \
                   }

//----------------------------------------
// Constructor of the class BufMgr
//----------------------------------------

BufMgr::BufMgr(const int bufs)
{
    numBufs = bufs;

    bufTable = new BufDesc[bufs];
    memset(bufTable, 0, bufs * sizeof(BufDesc));
    for (int i = 0; i < bufs; i++) 
    {
        bufTable[i].frameNo = i;
        bufTable[i].valid = false;
    }

    bufPool = new Page[bufs];
    memset(bufPool, 0, bufs * sizeof(Page));

    int htsize = ((((int) (bufs * 1.2))*2)/2)+1;
    hashTable = new BufHashTbl (htsize);  // allocate the buffer hash table

    clockHand = bufs - 1;
}


BufMgr::~BufMgr() {

    // flush out all unwritten pages
    for (int i = 0; i < numBufs; i++) 
    {
        BufDesc* tmpbuf = &bufTable[i];
        if (tmpbuf->valid == true && tmpbuf->dirty == true) {

#ifdef DEBUGBUF
            cout << "flushing page " << tmpbuf->pageNo
                 << " from frame " << i << endl;
#endif

            tmpbuf->file->writePage(tmpbuf->pageNo, &(bufPool[i]));
        }
    }

    delete [] bufTable;
    delete [] bufPool;
}


const Status BufMgr::allocBuf(int & frame) 
{
    //for(int i = 0; i < numPages; i++, incrementclock()) {
        //If refbit == 1
          //set refbit 0 
          //continue

        //Check current clock position if not pinned and refbit 0
        //if pincount == 0 {
          //if dirty {
              //flush page
              //if error writing page return UNIXERR status
          //}
          //if contains valid page {
            //remove valid page from hash table
          //}
          //use page
          //make sure flags are set correctly (pinCount++, not dirty, etc)
          //set frame to the current frameNo
          //return OK status
        //}
    //}

    //Only reach here if no available frames
    //return BUFFEREXCEEDED status
}

	
const Status BufMgr::readPage(File* file, const int PageNo, Page*& page)
{
//1. Check if page is in buffer pool by invoking lookup() FRom BufHashTbl
    
    //int* frameNo = (int*) malloc(sizeof(int));
    //if(hashTable.lookup(file, pageNo, frameNo) == HASHNOTFOUND){
        //A

    //}else{
        //B
   // }


//Status s = functioncall();
//then check status 

//A. lookup() returns HASHNOTFOUND
    //call allocBuf(&frameNo) to allocate a bufferFrame
        //allocBuf(frameNo) Returns BUFFEREXCEEDED or UNIXERR or OK 
    //file->readPage() to read page from disk into buffer pool frame
        //file.readPage(pageNo, page) returns OK or UNIXERR or BADPAGENO or BADPAGEPTR
        //BADPAGENO / BADPAGEPTR considered UNIXERR?
    //insert page into hashtable
        //hashTable.insert(file, pageNo, &frameNo) returns OK or HASHTBLERROR
    //Set() on frame (leaves pinCnt for the page to 1)
        
    //modify ptr to frame containing page, via page param

//B. lookup() returns FrameNO
    //set appropriate reference bit on BufDesc (frame)
    //increment pinCnt on BufDesc (Frame)
    //modify ptr to frame containing the page, via page param

//return OK on no errors
//return UNIXERR if unixerror occurred
//return BUFFEREXCEEDED if all buffer frames are pinnned
//return HASHTBLERROR

}

const Status BufMgr::unPinPage(File* file, const int PageNo, 
			       const bool dirty) 
{
    Status status;

    int frameNo;
    status = hashTable->lookup(file, PageNo, &frameNo);

    if(status == HASHNOTFOUND)
        return HASHNOTFOUND;
    
    BufDesc bufDesc = bufTable[frameNo];

    if(bufDesc.pinCnt == 0)
        return PAGENOTPINNED;

    bufDesc.pinCnt--;

    if(dirty)
        bufDesc.dirty = 1;

    return OK;
}

const Status BufMgr::allocPage(File* file, int& pageNo, Page*& page) 
{
    Status status;

    int pageNum;
    status = file->allocatePage(&pageNum);
    if(status != OK)
        return UNIXERR;
    
    int frameNo;
    status = allocBuf(&frameNo);
    if(status != OK)
        return status;
    
    status = hashTable->insert(file, pageNum, frameNo);
    if(status != OK)
        return status;

    bufTable->Set(file, pageNum);

    *pageNo = pageNum;
    *page = &bufPool[frameNo];

    return OK;
}

const Status BufMgr::disposePage(File* file, const int pageNo) 
{
    // see if it is in the buffer pool
    Status status = OK;
    int frameNo = 0;
    status = hashTable->lookup(file, pageNo, frameNo);
    if (status == OK)
    {
        // clear the page
        bufTable[frameNo].Clear();
    }
    status = hashTable->remove(file, pageNo);

    // deallocate it in the file
    return file->disposePage(pageNo);
}

const Status BufMgr::flushFile(const File* file) 
{
  Status status;

  for (int i = 0; i < numBufs; i++) {
    BufDesc* tmpbuf = &(bufTable[i]);
    if (tmpbuf->valid == true && tmpbuf->file == file) {

      if (tmpbuf->pinCnt > 0)
	  return PAGEPINNED;

      if (tmpbuf->dirty == true) {
#ifdef DEBUGBUF
	cout << "flushing page " << tmpbuf->pageNo
             << " from frame " << i << endl;
#endif
	if ((status = tmpbuf->file->writePage(tmpbuf->pageNo,
					      &(bufPool[i]))) != OK)
	  return status;

	tmpbuf->dirty = false;
      }

      hashTable->remove(file,tmpbuf->pageNo);

      tmpbuf->file = NULL;
      tmpbuf->pageNo = -1;
      tmpbuf->valid = false;
    }

    else if (tmpbuf->valid == false && tmpbuf->file == file)
      return BADBUFFER;
  }
  
  return OK;
}


void BufMgr::printSelf(void) 
{
    BufDesc* tmpbuf;
  
    cout << endl << "Print buffer...\n";
    for (int i=0; i<numBufs; i++) {
        tmpbuf = &(bufTable[i]);
        cout << i << "\t" << (char*)(&bufPool[i]) 
             << "\tpinCnt: " << tmpbuf->pinCnt;
    
        if (tmpbuf->valid == true)
            cout << "\tvalid\n";
        cout << endl;
    };
}


