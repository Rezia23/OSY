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
private:
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
    ~Stack(){
        delete [] stack;
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



class CCPUChild : public CCPU {
private:
    int numNeededPages(uint32_t pagesInStack, uint32_t pagesDesired){
        if(pagesInStack < pagesDesired){
            return pagesDesired;
        }
        //todo maybe bug when exceeding addressing space but probably ok
        uint32_t tmpNumILSLPT = numInLastSecondLevelPageTable;
        uint32_t pagesNeeded = 0;
        if(tmpNumILSLPT < PAGE_SIZE/4){
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



    virtual bool SetMemLimit(uint32_t pages){

        if(pages < memLimit){
            uint32_t numToRemove = memLimit - pages;
            uint32_t startOfRootPageTable = m_PageTableRoot*(PAGE_SIZE/4);
            uint32_t endOfRootPageTable = startOfRootPageTable + (PAGE_SIZE/4)-1;
            for(uint32_t i = endOfRootPageTable;i >=startOfRootPageTable;i--){
                if((((uint32_t *)this->m_MemStart)[i] & 0x0001) == 1){
                    uint32_t secondLevelPage = ((uint32_t *)this->m_MemStart)[i] >> 12;
                    uint32_t startOfPage = secondLevelPage * (PAGE_SIZE/4);
                    uint32_t endOfPage = (secondLevelPage*PAGE_SIZE) + (PAGE_SIZE/4)-1;
                    for(uint32_t j = endOfPage; j >=startOfPage;j--){
                        if((((uint32_t *)this->m_MemStart)[j] & 0x0001) == 1){ //second level entry is present, remove
                            numInLastSecondLevelPageTable--;
                            ((uint32_t *)this->m_MemStart)[j] = 0;
                            if(numToRemove == 0){
                                break;
                            }
                        }
                    }
                    //removed everything from second level pt, delete first level entry
                    if((((uint32_t *)this->m_MemStart)[startOfPage] & 0x0001) != 1){
                        ((uint32_t *)this->m_MemStart)[i] = 0; //set as not present in first level
                        numSecondLevelPageTables--;
                        numInLastSecondLevelPageTable = PAGE_SIZE/4;
                        //push stack
                        pthread_mutex_lock(&stackMut);
                        freePages.push(secondLevelPage);
                        pthread_mutex_unlock(&stackMut);
                    }
                    if(numToRemove == 0){
                        memLimit = pages;
                        return true;
                    }
                }
            }
        }else if(pages == memLimit){
          return true;
        }else{
            uint32_t numToAdd = pages - memLimit;
            uint32_t * currFreePages;
            pthread_mutex_lock(&stackMut);
            uint32_t numNeeded = numNeededPages(freePages.numFree(),numToAdd);
            if(numNeeded > freePages.numFree()){
                pthread_mutex_unlock(&stackMut);
                return false;
            }
            currFreePages = new uint32_t [numNeeded];
            for(uint32_t i = 0; i<numNeeded;i++){
                currFreePages[i] = freePages.pop();
            }
            pthread_mutex_unlock(&stackMut);
            //add some pages
            uint32_t startOfRootPageTable = m_PageTableRoot*(PAGE_SIZE/4);
            uint32_t lastRootEntryAboutSecondLevel = startOfRootPageTable + numSecondLevelPageTables-1;
            while(numNeeded != 0){
                uint32_t lastSLEntry = ((uint32_t *) this->m_MemStart)[lastRootEntryAboutSecondLevel] >> 12;
                while(numInLastSecondLevelPageTable != PAGE_SIZE/4){
                    uint32_t lastEntry = lastSLEntry * (PAGE_SIZE/4) + numInLastSecondLevelPageTable -1;
                    ((uint32_t *)this->m_MemStart)[lastEntry] = (currFreePages[numNeeded-1] << 12) | 0x0007;
                    numNeeded --;
                    numInLastSecondLevelPageTable++;
                }
                numInLastSecondLevelPageTable = 0;
                if(numNeeded == 0){
                    break;
                }
                //add new first level entry
                lastRootEntryAboutSecondLevel++;
                ((uint32_t *)this->m_MemStart)[lastRootEntryAboutSecondLevel] = (currFreePages[numNeeded-1] << 12) | 0x0007;
                numNeeded --;
            }
            memLimit = pages;
            delete [] currFreePages;
            return true;
        }
        return false;
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
void startPageTable(uint32_t page, void * mem){
    auto * memory = (uint32_t *)(mem);
    for(uint32_t i = 0; i< (CCPU::PAGE_SIZE/4);i++){
        memory[(page * CCPU::PAGE_SIZE) + i] = 0;
    }
}



void threadFunc(void * data){
    threadInfo * ti = (threadInfo *)data;

    pthread_mutex_lock(&stackMut);
    uint32_t pageTableIndex = freePages.pop();
    pthread_mutex_unlock(&stackMut);
    printf("Could not read %d\n", pageTableIndex);

    startPageTable(pageTableIndex, ti->mem);
    printf("Do we even get here \n");

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
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    threadInfo * ti = new threadInfo {mem, processArg, mainProcess};
    pthread_t tid;
    //create thread
    pthread_create(&tid, &attr, reinterpret_cast<void *(*)(void *)>(threadFunc), (void *)ti);


    pthread_cond_wait(&cv, &threadMut);
    pthread_mutex_destroy(&threadMut);
    pthread_mutex_destroy(&stackMut);

}
