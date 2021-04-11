#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <semaphore.h>
#include "common.h"

using namespace std;
#endif /* __PROGTEST__ */


class Stack{
public:
    uint32_t * stack;
    uint32_t tail;
public:
    Stack() = default;
    Stack(uint32_t i){
        stack = new uint32_t [i];
        printf("start: %d\n", i);
        for(uint32_t j = 0; j<i ; j++){
            stack[j] = j;
        }
        tail = i;
    }
    void push(uint32_t i){
        printf("pushing to pos %d", i);
        stack[tail] = i;
        tail++;
    }
    uint32_t pop(){
        tail--;
        printf("popping from pos %d\n", tail);
        return stack[tail];
    }
    uint32_t numFree(){
        return tail;
    }

};
//------------------------------------------------
uint32_t numThreads = 0;
pthread_mutex_t threadMut;
pthread_mutex_t stackMut;
pthread_cond_t cv;
Stack freePages;
//------------------------------------------------
struct threadInfo{
    void * mem;
    void * processArg;
    void (* mainProcess)(CCPU *, void *);
};
void threadFunc(void * data);

uint32_t calculateAddressShift(uint32_t page, uint32_t pageEntry){
    return (page * CCPU::PAGE_SIZE) + (pageEntry*4);
}

class CCPUChild : public CCPU {
private:
    int numNeededPages(uint32_t pagesInStack, uint32_t pagesDesired){
        if(pagesInStack < pagesDesired){
            return pagesDesired;
        }

        //todo maybe bug when exceeding addressing space but probably ok
        uint32_t tmpNumILSLPT = numInLastSecondLevelPageTable;
        uint32_t pagesNeeded = 0;
        if(tmpNumILSLPT < PAGE_SIZE/4 && numSecondLevelPageTables!=0){
            uint32_t freeEntries = (PAGE_SIZE/4) -tmpNumILSLPT;
            if(freeEntries > pagesDesired){
                return pagesDesired;
            }
            pagesDesired-=(PAGE_SIZE/4 - tmpNumILSLPT);
        }
        uint32_t residue = pagesDesired%(PAGE_SIZE/4);
        uint32_t newSecondLevelPages = (pagesDesired-residue)/(PAGE_SIZE/4);
        pagesNeeded = pagesDesired + newSecondLevelPages;
        if(residue==0){
            pagesNeeded++;
        }
        return pagesNeeded;
    }

public:
    static void * mem;
    uint32_t memLimit = 0;
    uint32_t numSecondLevelPageTables = 0;
    uint32_t numInLastSecondLevelPageTable = 0;
    CCPUChild(void *memStart,
              uint32_t pageTableRoot) : CCPU(static_cast<uint8_t*>(memStart),pageTableRoot ){}
    virtual uint32_t GetMemLimit(void) const{
        return memLimit;
    }

    void removeFew(uint32_t pageNum, uint32_t numToRemove, uint32_t numPresent, uint8_t * memory){
        uint32_t numToStay = numPresent-numToRemove;
        for(uint32_t i = 0; i<numToRemove;i++){
            uint32_t deletedPage;
            memcpy(&deletedPage, memory + calculateAddressShift(pageNum, numToStay+i),4);
            deletedPage = deletedPage>>12;
            memset(memory + calculateAddressShift(pageNum, numToStay+i), 0, 4);
            pthread_mutex_lock(&stackMut);
            freePages.push(deletedPage);
            pthread_mutex_unlock(&stackMut);
            memLimit -=numToRemove;
            numInLastSecondLevelPageTable-= numToRemove;
        }

    }
    void addFew(uint32_t pageNum, uint32_t numToAdd, uint32_t numPresent, uint8_t * memory, uint32_t * currFreePages, uint32_t * freePagesIndex){
        printf("Adding page with num %d\nnum present %d\nnum to add %d\n", pageNum, numPresent, numToAdd);
        for(uint32_t i = numPresent; i<numToAdd+numPresent;i++){
            uint32_t entry = 0x00000007 | (currFreePages[(*freePagesIndex)-1]<<12);
            printf("copying to %d %d %d\n", pageNum, PAGE_SIZE, (pageNum * PAGE_SIZE) + 4*i);
            memcpy(memory + calculateAddressShift(pageNum, i), &entry, 4);
            (*freePagesIndex)--;
        }
    }

    virtual bool SetMemLimit(uint32_t pages){
        uint8_t * memory = (uint8_t *) m_MemStart;
        if(pages < memLimit){
            uint32_t numToRemove = memLimit - pages;
            uint32_t lastSecondLevelPageNum;
            memcpy(&lastSecondLevelPageNum, memory + (m_PageTableRoot * PAGE_SIZE) + (numSecondLevelPageTables-1)*4, 4); //times 4 because one entry is 4 bytes
            lastSecondLevelPageNum = lastSecondLevelPageNum >> 12;
            if(numToRemove < numInLastSecondLevelPageTable){
                //remove few, set numILSLPT, set memLimit, push to stack
                removeFew(lastSecondLevelPageNum, numToRemove, numInLastSecondLevelPageTable, memory);
                memLimit = pages;
                return true;
            }
            numToRemove -= numInLastSecondLevelPageTable;
            removeFew(lastSecondLevelPageNum, numInLastSecondLevelPageTable, numInLastSecondLevelPageTable, memory);
            //remove entry in first table, push second page to stack
            memset(memory + (m_PageTableRoot * PAGE_SIZE) + (numSecondLevelPageTables-1)*4, 0, 4);
            pthread_mutex_lock(&stackMut);
            freePages.push(lastSecondLevelPageNum);
            pthread_mutex_unlock(&stackMut);

            uint32_t remainder = numToRemove%(PAGE_SIZE/4);
            uint32_t numWholePagesToRemove = (numToRemove - remainder)/(PAGE_SIZE/4);
            uint32_t pageNum;
            while(numWholePagesToRemove > 0){

                memcpy(&pageNum, memory + calculateAddressShift(m_PageTableRoot, numSecondLevelPageTables-1), 4);
                pageNum = pageNum >> 12;
                removeFew(pageNum, PAGE_SIZE/4, PAGE_SIZE/4, memory);

                memset(memory + calculateAddressShift(m_PageTableRoot, numSecondLevelPageTables-1), 0, 4);
                pthread_mutex_lock(&stackMut);
                freePages.push(pageNum);
                pthread_mutex_unlock(&stackMut);
                numWholePagesToRemove--;
                numSecondLevelPageTables--;
            }
            //remove few
            if(remainder!=0){
                memcpy(&pageNum, memory + calculateAddressShift(m_PageTableRoot, numSecondLevelPageTables-1), 4);
                pageNum = pageNum >> 12;
                removeFew(pageNum, remainder, PAGE_SIZE/4, memory);
            }
            memLimit = pages;
            return true;
        }else if(pages == memLimit){
          return true;
        }else {
            uint32_t numToAdd = pages - memLimit;

            uint32_t *currFreePages;
            pthread_mutex_lock(&stackMut);
            uint32_t numNeeded = numNeededPages(freePages.numFree(), numToAdd);
            printf("oni chcou %d, ja mam %d, chybi mi %d num needed is %d\n", pages, memLimit, numToAdd, numNeeded);
            if (numNeeded > freePages.numFree()) {
                pthread_mutex_unlock(&stackMut);
                return false;
            }
            currFreePages = new uint32_t[numNeeded];
            for (uint32_t i = 0; i < numNeeded; i++) {
                currFreePages[i] = freePages.pop();
            }
            pthread_mutex_unlock(&stackMut);

            if (numSecondLevelPageTables == 0) {
                uint32_t pageNum = currFreePages[numNeeded - 1];
                uint32_t numActuallyAddedPages =
                        numToAdd < (PAGE_SIZE / 4) ? numToAdd : (PAGE_SIZE / 4); //=min(numToAdd, (PAGE_SIZE/4))
                //add few
                addFew(pageNum, numActuallyAddedPages, 0, memory, currFreePages, &numNeeded);
                numToAdd -= numActuallyAddedPages;
                //add entry to first level table
                uint32_t entry = 0x00000007 | (pageNum << 12);
                memcpy(memory + calculateAddressShift(m_PageTableRoot,0), &entry, 4);
                numSecondLevelPageTables = 1;
                numInLastSecondLevelPageTable = numActuallyAddedPages < (PAGE_SIZE / 4) ? numActuallyAddedPages : 0;
            }
            if (numInLastSecondLevelPageTable != 0) {
                printf("we do stuff here");
                uint32_t numToActuallyAdd = numToAdd < (PAGE_SIZE-4)-numInLastSecondLevelPageTable ? numToAdd :(PAGE_SIZE-4)-numInLastSecondLevelPageTable;
                uint32_t pageNum;
                memcpy(&pageNum, memory + calculateAddressShift(m_PageTableRoot, numSecondLevelPageTables-1), 4);
                pageNum = pageNum >> 12;
                addFew(pageNum, numToActuallyAdd, numInLastSecondLevelPageTable, memory,
                       currFreePages, &numNeeded);
                numToAdd -= numToActuallyAdd;
                numInLastSecondLevelPageTable += numToActuallyAdd;
            }
            uint32_t remainder = numToAdd % (PAGE_SIZE / 4);
            uint32_t numWholePages = (numToAdd - remainder) / (PAGE_SIZE / 4);
            while (numWholePages > 0) {
                uint32_t pageNum = currFreePages[numNeeded - 1];
                numNeeded--;
                addFew(pageNum, PAGE_SIZE / 4, 0, memory, currFreePages, &numNeeded);
                numWholePages--;
                numToAdd -= PAGE_SIZE / 4;
                //add entry to root table
                uint32_t entry = 0x00000007 | (pageNum << 12);
                memcpy(memory + calculateAddressShift(m_PageTableRoot, numSecondLevelPageTables), &entry, 4);
                numSecondLevelPageTables++;
            }
            if (remainder != 0) {
                uint32_t pageNum = currFreePages[numNeeded - 1];
                addFew(pageNum, remainder, 0, memory, currFreePages, &numNeeded);
                uint32_t entry = 0x00000007 | (pageNum << 12);
                memcpy(memory + calculateAddressShift(m_PageTableRoot, numSecondLevelPageTables), &entry, 4);
                numSecondLevelPageTables++;
                numInLastSecondLevelPageTable += remainder;

            }
            if (numNeeded != 1) {
                printf("je to rozbity");
            }
            delete[] currFreePages;
            memLimit = pages;
            return true;
        }
    }

    virtual bool NewProcess(void *processArg, void (*entryPoint)(CCPU *, void *), bool copyMem){
        pthread_mutex_lock(&threadMut);
        if(numThreads >= PROCESS_MAX){
            pthread_mutex_unlock(&threadMut);
            return false;
        }
        numThreads++;
        pthread_mutex_unlock(&threadMut);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        threadInfo * ti = new threadInfo {this->m_MemStart, (processArg), entryPoint};
        pthread_t tid;

        //create thread
        pthread_create(&tid, &attr, reinterpret_cast<void *(*)(void *)>(threadFunc), (void *)ti);
        return true;
    }

protected:
    /*
     if copy-on-write is implemented:

     virtual bool             pageFaultHandler              ( uint32_t          address,
                                                              bool              write );
     */
};
void startPageTable(uint32_t page, uint8_t * mem){
   memset(mem+(page*CCPU::PAGE_SIZE), 0, CCPU::PAGE_SIZE);
}



void threadFunc(void * data){
    threadInfo * ti = (threadInfo *)data;

    //find free page for page table
    pthread_mutex_lock(&stackMut);
    uint32_t pageTableIndex = freePages.pop();
    pthread_mutex_unlock(&stackMut);

    startPageTable(pageTableIndex, (uint8_t *)ti->mem);

    CCPUChild cpu = CCPUChild(ti->mem, pageTableIndex);
    ti->mainProcess(&cpu, ti->processArg);

    pthread_mutex_lock(&threadMut);
    numThreads--;
    if(numThreads == 0){
        pthread_cond_signal(&cv);
    }
    pthread_mutex_unlock(&threadMut);
    delete ti;
}


void MemMgr(void *mem, uint32_t totalPages, void *processArg, void (*mainProcess)(CCPU *, void *)) {
    freePages = Stack(totalPages);
    //
    pthread_mutex_init(&threadMut, NULL);
    pthread_mutex_init(&stackMut, NULL);
    pthread_cond_init(&cv, NULL);


    //start process
    pthread_mutex_lock(&stackMut);
    uint32_t pageTableIndex = freePages.pop();
    pthread_mutex_unlock(&stackMut);

    startPageTable(pageTableIndex, (uint8_t *)mem);

    CCPUChild cpu = CCPUChild(mem, pageTableIndex);
    mainProcess(&cpu, processArg);

    pthread_cond_wait(&cv, &threadMut);
    pthread_mutex_destroy(&threadMut);
    pthread_mutex_destroy(&stackMut);
    delete [] freePages.stack;

}
