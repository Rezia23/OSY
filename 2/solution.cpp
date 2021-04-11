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
        for(uint32_t j = 0; j<i ; j++){
            stack[j] = i;
        }
        tail = i;
    }
    ~Stack(){
        delete [] stack;
    }
    void push(uint32_t i){
        stack[tail] = i;
        tail++;
    }
    uint32_t pop(){
        tail--;
        return stack[tail];
    }

};
//------------------------------------------------
int numThreads = 0;
pthread_mutex_t threadMut;
pthread_mutex_t stackMut;
pthread_cond_t cv;
Stack freePages;
//------------------------------------------------
class CCPUChild : public CCPU {
public:
    CCPUChild(void *memStart,
              uint32_t pageTableRoot) : CCPU(static_cast<uint8_t*>(memStart),pageTableRoot ){}
    virtual uint32_t GetMemLimit(void) const;

    virtual bool SetMemLimit(uint32_t pages);

    virtual bool NewProcess(void *processArg,
                            void           (*entryPoint)(CCPU *, void *),
                            bool copyMem);

protected:
    /*
     if copy-on-write is implemented:

     virtual bool             pageFaultHandler              ( uint32_t          address,
                                                              bool              write );
     */
};

void startPageTable(uint32_t page, void * mem){
    auto * memory = (uint32_t *)(mem);
    for(uint32_t i = 0; i< CCPU::PAGE_SIZE;i++){
        memory[(page * CCPU::PAGE_SIZE) + i] = 0;

    }
}

struct threadInfo{
    void * mem;
    void * processArg;
    void (* mainProcess)(CCPU *, void *);
};

void threadFunc(void * data){
    threadInfo * ti = (threadInfo *)data;
    pthread_mutex_lock(&stackMut);
    uint32_t pageTableIndex = freePages.pop();
    pthread_mutex_unlock(&stackMut);
    startPageTable(pageTableIndex, ti->mem);
    CCPUChild cpu = CCPUChild(ti->mem, pageTableIndex);
    ti->mainProcess(&cpu, ti->processArg);
    pthread_mutex_lock(&threadMut);
    numThreads--;
    if(numThreads == 0){
        pthread_cond_signal(&cv);
    }
    pthread_mutex_unlock(&threadMut);
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

    threadInfo ti = {mem, processArg, mainProcess};
    pthread_t tid;
    //create thread
    pthread_create(&tid, &attr, reinterpret_cast<void *(*)(void *)>(threadFunc), (void *)&ti);


    pthread_cond_wait(&cv, &threadMut);
    pthread_mutex_destroy(&threadMut);
    pthread_mutex_destroy(&stackMut);

}
