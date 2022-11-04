//----------------------------------------
// Group 9 Project 4
// 1. Joe Elert - 9081168636
// 2. Derek Calamari - 9081197635
// 3. Michael Feist - 9082159105
//----------------------------------------
#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include "page.h"
#include "buf.h"

/**
* The buffer manager controls the reading
* and writing of pages from the disk to
* the buffer pool.  It is also responsible
* for keeping track of which pages are in
* the buffer pool. 
*
* Global Variables:
*   numBufs - number of buffers in the buffer pool
*   bufPool - the buffer pool which stores 
*             pointers to pages read from disk
*   bufTable - the buffer table which stores
*              information about each frame
*              such as pin count, dirty bit, etc.
*   hashTable - the hash table that stores
*               the mapping between pages and frames
*   clockHand - the clock hand that points at
*               the current frame
*/

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


/**
 * AllocBuf method finds the next frame to be replaced in the buffer pool
 * via the clock algorithm.  The clock algorithm works as follows:
 * 
 * 1. Start at the frame pointed to by the clock hand
 * 2. If the frame is invalid, use it
 * 3. If the frame is valid and the reference bit is set, clear the reference bit
 *   and advance the clock hand
 * 4. If the frame is valid and the reference bit is not set, check the pin count
 *  a. If the pin count is 0, check the dirty bit
 *   i. If the dirty bit is set, write the page to disk
 *  b. Remove the page from the hash table
 *  c. Use the frame
 * 5. Else advance the clock hand and repeat steps 2-4
 * 
 * @author Joe Elert
 * @param  int & frame - the newly allocated frame to be used
 * @return Status - OK if successful, BUFFEREXCEEDED if no frames available, 
 *                  ERROR if any other errors occur in method calls
 * 
 */
const Status BufMgr::allocBuf(int & frame) 
{
    Status status;

    for (int i = 0; i < numBufs * 2; i++) {

        BufDesc* tmpbuf = &(bufTable[clockHand]);
        if (tmpbuf->valid == false) {
            frame = clockHand;
            return OK;
        }

        if(tmpbuf->refbit == true) {
            tmpbuf->refbit = false;
            advanceClock();
            continue;
        }
        

        if (tmpbuf->pinCnt == 0) {
            if (tmpbuf->dirty == true) {
                //flush page
                Status status = tmpbuf->file->writePage(tmpbuf->pageNo, &(bufPool[clockHand]));
                
                // if error writing page return UNIXERR status            
                if(status != OK){
                    return status;
                }
            } 
            //remove valid page from hash table
            hashTable->remove(tmpbuf->file, tmpbuf->pageNo);

            //use page
            frame = clockHand;
            return OK;            
        }
        advanceClock();
    }

    //Only reach here if no available frames
    return BUFFEREXCEEDED;
}



/**
 * Checks whether page is in buffer pool. 
 * 1. If page is not in buffer pool, allocate buffer frame, read page from disk into frame,
 * insert page into hashtable, Set frame, then give pointer to the frame containing the page
 * 2. If page is in buffer pool, set reference bit, increment pin count, then give pointer
 * to the frame containing the page.
 * 
 * @author          Derek Calamari
 * @param file      File of corresponding frame
 * @param PageNo    Page number of corresponding frame
 * @param page      Page of corresponding page number 
 * @return          Status- OK if no errors occurred, UNIXERR if a Unix error occurred, 
 *                  BUFFEREXCEEDED if all buffer frames are pinned, HASHTBLERROR if a hash table error occurred.
*/
const Status BufMgr::readPage(File* file, const int PageNo, Page*& page)
{
    //Invoke hashTable lookup
    Status status;
    int frameNo;
    status = hashTable->lookup(file, PageNo, frameNo);

    //Check status of hashTable lookup
    if(status == HASHNOTFOUND){
        //Page not in buffer pool

        //Allocate a free frame 
        status = allocBuf(frameNo);

        if(status != OK){
            return status;
        }

        //Read page from disk into buffer pool frame with file->readPage()
        status = file->readPage(PageNo, &(bufPool[frameNo]));
        if(status != OK){
            return status;
        }

        //insert page into hashtable
        status = hashTable->insert(file, PageNo, frameNo);
        if(status != OK){
            return status;
        }

        //invoke set() --> pinCnt set to 1
        bufTable[frameNo].Set(file, PageNo);

        //return ptr to frame containing the page via the page param
        page = &(bufPool[frameNo]);

        return OK;

    }else{
        //Page is in buffer pool
        BufDesc* tmpbuf = &(bufTable[frameNo]);

        //set reference bit
        tmpbuf->refbit = true;

        //increment pinCnt 
        tmpbuf->pinCnt++;

        //return ptr to frame containing the page via the page param
        page = &(bufPool[frameNo]);

        return OK;
    }
}

/**
 * Decrements the pinCnt of the frame corresponding
 * to the given file and PageNo. If dirty is true,
 * also sets the dirty bit.
 * 
 * @author          Michael Feist
 * @param file      File of corresponding frame
 * @param PageNo    Page number of corresponding frame
 * @param dirty     True if dirty bit should be set to true, false otherwise
 * @return          OK if successful, hashTable->lookup error status if lookup fails, or PAGENOTPINNED if pinCnt == 0
*/
const Status BufMgr::unPinPage(File* file, const int PageNo, 
			       const bool dirty) 
{
    Status status;
    int frameNo = 0;

    status = hashTable->lookup(file, PageNo, frameNo);
    if(status != OK){
        return status;
    }

    BufDesc* tmpbuf = &(bufTable[frameNo]);

    if(tmpbuf->pinCnt == 0){
        return PAGENOTPINNED;
    }

    tmpbuf->pinCnt--;

    if(dirty) {
        tmpbuf->dirty = true;
    }

    return OK;
}

/**
 * Allocates a new page within the given file and a 
 * corresponding frame within the buffer manager.
 * Adds the entry to the hashtable and intializes
 * its buftable entry. Gives back the corresponding
 * page number, and a pointer to the newly allocated
 * frame within the buffer pool.
 * 
 * @author          Michael Feist
 * @param   file    File to allocate page in
 * @param   pageNo  Page number of page allocated will be written back here
 * @param   page    Pointer to frame in bufferpool will be written back here
 * @return          OK if successful, otherwise returns error status from file->allocatePage, allocBuf, or hashTable->insert
*/
const Status BufMgr::allocPage(File* file, int& pageNo, Page*& page) 
{
    Status status;

    status = file->allocatePage(pageNo);
    if(status != OK) {
        return status;
    }

    int frameNo;
    status = allocBuf(frameNo);
    if(status != OK){
        return status;
    }
    status = hashTable->insert(file, pageNo, frameNo);
    if(status != OK){
        return status;
    }
    bufTable[frameNo].Set(file, pageNo);
    page = &bufPool[frameNo];

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


