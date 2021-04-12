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
        //printf("start: %d\n", i);
        for(uint32_t j = 0; j<i ; j++){
            stack[j] = i-j;
        }
        tail = i;
    }
    void push(uint32_t i){
        //printf("pushing to pos %d", i);
        stack[tail] = i;
        tail++;
    }
    uint32_t pop(){
        tail--;
        //printf("popping from pos %d\n", tail);
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
class CCPUChild;
struct threadInfo{
    void * mem;
    void * processArg;
    void (* mainProcess)(CCPU *, void *);
    bool copyMem;
    CCPUChild * parentCPU;
};
void threadFunc(void * data);
void startPageTable(uint32_t page, uint8_t * mem);
uint32_t calculateAddressShift(uint32_t page, uint32_t pageEntry){
    return (page * CCPU::PAGE_SIZE) + (pageEntry*4);
}

class CCPUChild : public CCPU {
private:
    int numNeededPages(uint32_t pagesInStack, uint32_t pagesDesired){
        uint32_t originalPageNum = pagesDesired;
        if(pagesInStack < pagesDesired){
            return pagesDesired;
        }
        uint32_t tmpNumILSLPT = numInLastSecondLevelPageTable;
        uint32_t pagesNeeded = 0;
        if(tmpNumILSLPT < PAGE_DIR_ENTRIES && numInLastSecondLevelPageTable!=0){
            uint32_t freeEntries = (PAGE_DIR_ENTRIES) -tmpNumILSLPT;
            if(freeEntries >= pagesDesired){
                return pagesDesired;
            }
            pagesDesired-=(PAGE_DIR_ENTRIES - tmpNumILSLPT);
        }
        uint32_t residue = pagesDesired%(PAGE_DIR_ENTRIES);
        uint32_t newSecondLevelPages = (pagesDesired-residue)/(PAGE_DIR_ENTRIES);
        pagesNeeded = originalPageNum + newSecondLevelPages;
        if(residue!=0){
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
        }
        memLimit -=numToRemove;
        numInLastSecondLevelPageTable-= numToRemove;

    }
    void printPageTable(uint8_t * memory){
        printf("%d\n\n",m_PageTableRoot>>12);
        for(uint32_t i = 0; i<PAGE_DIR_ENTRIES;i++){
            uint32_t entry;
            memcpy(&entry, (memory + calculateAddressShift(m_PageTableRoot>>12, i)), 4);
            printf("%d\n", entry>>12);
        }
        printf("\n");
    }
    void printAnotherTable(uint8_t * memory, uint32_t pageNum){
        for(uint32_t i = 0; i<PAGE_DIR_ENTRIES;i++){
            uint32_t entry;
            memcpy(&entry, (memory + calculateAddressShift(pageNum>>12, i)), 4);
            //printf("%d\n", entry>>12);
        }
        printf("\n");
    }
    void addFew(uint32_t pageNum, uint32_t numToAdd, uint32_t numPresent, uint8_t * memory, uint32_t * currFreePages, uint32_t * freePagesIndex){
        //printf("Adding page with num %d\nnum present %d\nnum to add %d\n", pageNum, numPresent, numToAdd);
        for(uint32_t i = numPresent; i<numToAdd+numPresent;i++){
            uint32_t entry = 0x00000007 | (currFreePages[(*freePagesIndex)-1]<<12);
            //printf("copying to %d %d %d\n", pageNum, PAGE_SIZE, (pageNum * PAGE_SIZE) + 4*i);
            memcpy(memory + calculateAddressShift(pageNum, i), &entry, 4);
            (*freePagesIndex)--;
        }
    }

    virtual bool SetMemLimit(uint32_t pages){
        uint8_t * memory = (uint8_t *) m_MemStart;

        if(pages < memLimit){
            uint32_t numToRemove = memLimit - pages;
            uint32_t lastSecondLevelPageNum;
            memcpy(&lastSecondLevelPageNum, memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables-1), 4); //times 4 because one entry is 4 bytes
            lastSecondLevelPageNum = lastSecondLevelPageNum >> 12; //todo mozna se to ma andovat s maskou adresy ale nedava to smysl

            if(numToRemove < numInLastSecondLevelPageTable){
                //remove few, set numILSLPT, set memLimit, push to stack
                removeFew(lastSecondLevelPageNum, numToRemove, numInLastSecondLevelPageTable, memory);
                memLimit = pages;
                return true;
            }
            numToRemove -= numInLastSecondLevelPageTable;
            removeFew(lastSecondLevelPageNum, numInLastSecondLevelPageTable, numInLastSecondLevelPageTable, memory);
            //remove entry in first table, push second page to stack
            memset(memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables-1), 0, 4);
            pthread_mutex_lock(&stackMut);
            freePages.push(lastSecondLevelPageNum);
            pthread_mutex_unlock(&stackMut);
            numInLastSecondLevelPageTable = (PAGE_DIR_ENTRIES);
            numSecondLevelPageTables--;

            uint32_t remainder = numToRemove%(PAGE_DIR_ENTRIES);
            uint32_t numWholePagesToRemove = (numToRemove - remainder)/(PAGE_DIR_ENTRIES);
            uint32_t pageNum;
            while(numWholePagesToRemove > 0){

                memcpy(&pageNum, memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables-1), 4);
                pageNum = pageNum >> 12;
                removeFew(pageNum, PAGE_DIR_ENTRIES, PAGE_DIR_ENTRIES, memory);

                memset(memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables-1), 0, 4);
                pthread_mutex_lock(&stackMut);
                freePages.push(pageNum);
                pthread_mutex_unlock(&stackMut);
                numWholePagesToRemove--;
                numSecondLevelPageTables--;
                numInLastSecondLevelPageTable = (PAGE_DIR_ENTRIES);
            }
            //remove few
            if(remainder!=0){
                memcpy(&pageNum, memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables-1), 4);
                pageNum = pageNum >> 12;
                removeFew(pageNum, remainder, PAGE_DIR_ENTRIES, memory);
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
            //printf("oni chcou %d, ja mam %d, chybi mi %d num needed is %d\n", pages, memLimit, numToAdd, numNeeded);
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
                numNeeded--;
                uint32_t numActuallyAddedPages =
                        numToAdd < (PAGE_DIR_ENTRIES) ? numToAdd : (PAGE_DIR_ENTRIES); //=min(numToAdd, (PAGE_DIR_ENTRIES))
                //add few
                addFew(pageNum, numActuallyAddedPages, 0, memory, currFreePages, &numNeeded);
                numToAdd -= numActuallyAddedPages;
                //add entry to first level table
                uint32_t entry = 0x00000007 | (pageNum << 12);
                memcpy(memory + calculateAddressShift(m_PageTableRoot>>12,0), &entry, 4);
                numSecondLevelPageTables = 1;
                numInLastSecondLevelPageTable = numActuallyAddedPages < (PAGE_DIR_ENTRIES) ? numActuallyAddedPages : PAGE_DIR_ENTRIES;
            }
            if (numInLastSecondLevelPageTable != 0) {
                //printf("we do stuff here");
                uint32_t numToActuallyAdd = numToAdd < (PAGE_DIR_ENTRIES)-numInLastSecondLevelPageTable ? numToAdd :(PAGE_DIR_ENTRIES)-numInLastSecondLevelPageTable;
                uint32_t pageNum;
                memcpy(&pageNum, memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables-1), 4);
                pageNum = pageNum >> 12;
                addFew(pageNum, numToActuallyAdd, numInLastSecondLevelPageTable, memory,
                       currFreePages, &numNeeded);
                numToAdd -= numToActuallyAdd;
                numInLastSecondLevelPageTable += numToActuallyAdd;
            }
            uint32_t remainder = numToAdd % (PAGE_DIR_ENTRIES);
            uint32_t numWholePages = (numToAdd - remainder) / (PAGE_DIR_ENTRIES);
            while (numWholePages > 0) {
                uint32_t pageNum = currFreePages[numNeeded - 1];
                numNeeded--;
                addFew(pageNum, PAGE_DIR_ENTRIES, 0, memory, currFreePages, &numNeeded);
                numWholePages--;
                numToAdd -= PAGE_DIR_ENTRIES;
                //add entry to root table
                uint32_t entry = 0x00000007 | (pageNum << 12);
                memcpy(memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables), &entry, 4);
                numSecondLevelPageTables++;
            }
            if (remainder != 0) {
                uint32_t pageNum = currFreePages[numNeeded - 1];
                numNeeded--;
                addFew(pageNum, remainder, 0, memory, currFreePages, &numNeeded);
                uint32_t entry = 0x00000007 | (pageNum << 12);
                memcpy(memory + calculateAddressShift(m_PageTableRoot>>12, numSecondLevelPageTables), &entry, 4);
                numSecondLevelPageTables++;
                numInLastSecondLevelPageTable = remainder;

            }
            if (numNeeded != 1) {
                //printf("je to rozbity");
            }
            delete[] currFreePages;
            memLimit = pages;
            //debug part
//            printPageTable(memory);
//            uint32_t debug;
//            memcpy(&debug, memory + calculateAddressShift(m_PageTableRoot >> 12,0), 4);
//            printAnotherTable(memory, debug);
//
//            memcpy(&debug, memory + calculateAddressShift(m_PageTableRoot >> 12,1), 4);
//            printAnotherTable(memory, debug);
//
//            memcpy(&debug, memory + calculateAddressShift(m_PageTableRoot >> 12,2), 4);
//            printAnotherTable(memory, debug);

            return true;
        }
    }

    virtual bool NewProcess(void *processArg, void (*entryPoint)(CCPU *, void *), bool copyMem){
        pthread_mutex_lock(&stackMut);
        if(copyMem && this->numInLastSecondLevelPageTable + this->numSecondLevelPageTables + 1 > freePages.numFree()){
            pthread_mutex_unlock(&stackMut);
            return false;
        }
        pthread_mutex_unlock(&stackMut);

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

        threadInfo * ti = new threadInfo {this->m_MemStart, (processArg), entryPoint, copyMem, this};
        pthread_t tid;

        //create thread
        pthread_create(&tid, &attr, reinterpret_cast<void *(*)(void *)>(threadFunc), (void *)ti);
        return true;
    }

    static void copyNext(CCPUChild * parentCPU, CCPUChild * childCPU, uint32_t firstLevelEntry, uint32_t secondLevelEntry){
        uint8_t * memory = parentCPU->m_MemStart;
        uint32_t secondLevelPageInParent;
        memcpy(&secondLevelPageInParent, memory + calculateAddressShift(parentCPU->m_PageTableRoot>>12,firstLevelEntry), 4);
        uint32_t secondLevelPageInChild;
        memcpy(&secondLevelPageInChild, memory + calculateAddressShift(childCPU->m_PageTableRoot>>12,firstLevelEntry), 4);

        secondLevelPageInParent = secondLevelPageInParent >> 12;
        secondLevelPageInChild = secondLevelPageInChild >> 12;

        uint32_t pageInParent;
        uint32_t pageInChild;
        memcpy(&pageInParent, memory + calculateAddressShift(secondLevelPageInParent, secondLevelEntry),4);
        memcpy(&pageInChild, memory + calculateAddressShift(secondLevelPageInChild, secondLevelEntry),4);

        pageInParent = pageInParent >> 12;
        pageInChild = pageInChild >> 12;

        memcpy(memory + calculateAddressShift(pageInChild, 0), memory + calculateAddressShift(pageInParent, 0), PAGE_SIZE);

    }

    static void copyPages(CCPUChild * parentCPU, CCPUChild * childCPU){
        if(parentCPU->numSecondLevelPageTables == 0) return;
        for(uint32_t i = 0; i<parentCPU->numSecondLevelPageTables-1;i++){
            for(uint32_t j = 0; j<PAGE_DIR_ENTRIES;j++){
                copyNext(parentCPU, childCPU, i, j);
            }
        }
        for(uint32_t j = 0; j<parentCPU->numInLastSecondLevelPageTable;j++){
            copyNext(parentCPU, childCPU, parentCPU->numSecondLevelPageTables-1, j);
        }
    }


    static void threadFunc(void * data){
        threadInfo * ti = (threadInfo *)data;

        //find free page for page table
        pthread_mutex_lock(&stackMut);
        uint32_t pageTableIndex = freePages.pop();
        pthread_mutex_unlock(&stackMut);

        startPageTable(pageTableIndex, (uint8_t *)ti->mem);

        CCPUChild cpu = CCPUChild(ti->mem, pageTableIndex<<12);

        if(ti->copyMem){
            cpu.SetMemLimit(ti->parentCPU->memLimit);
            copyPages(ti->parentCPU, &cpu);
            cpu.numSecondLevelPageTables = ti->parentCPU->numSecondLevelPageTables;
            cpu.numInLastSecondLevelPageTable = ti->parentCPU->numInLastSecondLevelPageTable;
        }


        ti->mainProcess(&cpu, ti->processArg);

        pthread_mutex_lock(&threadMut);
        numThreads--;
        if(numThreads == 0){
            pthread_cond_signal(&cv);
        }
        pthread_mutex_unlock(&threadMut);
        delete ti;
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





void MemMgr(void *mem, uint32_t totalPages, void *processArg, void (*mainProcess)(CCPU *, void *)) {
    memset(mem , 0, CCPU::PAGE_SIZE * totalPages);
    freePages = Stack(totalPages);
    pthread_mutex_init(&threadMut, NULL);
    pthread_mutex_init(&stackMut, NULL);
    pthread_cond_init(&cv, NULL);


    //start process
    pthread_mutex_lock(&stackMut);
    uint32_t pageTableIndex = freePages.pop();
    pthread_mutex_unlock(&stackMut);
    
    //printf("pti %d %d\n", pageTableIndex, pageTableIndex<<12);
    startPageTable(pageTableIndex, (uint8_t *)mem);

    CCPUChild cpu = CCPUChild(mem, pageTableIndex<<12);
    mainProcess(&cpu, processArg);

    pthread_mutex_lock(&threadMut);
    while(numThreads > 0) pthread_cond_wait(&cv, &threadMut);
    pthread_mutex_unlock(&threadMut);

    pthread_mutex_destroy(&threadMut);
    pthread_mutex_destroy(&stackMut);
    delete [] freePages.stack;

}
