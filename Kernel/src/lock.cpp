#include <lock.h>

#include <scheduler.h>
#include <timer.h>
#include <cpu.h>
#include <logging.h>

void Semaphore::Wait(){
    thread_t* thread = GetCPULocal()->currentThread;

    __sync_fetch_and_sub(&value, 1);
    while(value < 0 && thread->state != ThreadStateZombie) {
        assert(CheckInterrupts());
        
        Scheduler::BlockCurrentThread(blocked, blockedLock);
        asm("pause");
    }
}

void Semaphore::WaitTimeout(long timeout){
    __sync_fetch_and_sub(&value, 1);
    if(value < 0){
        thread_t* cThread = GetCPULocal()->currentThread;
        acquireLock(&cThread->stateLock);
        blocked.add_back(cThread);
        if(value >= 0){
            blocked.remove(cThread);
            releaseLock(&cThread->stateLock);
            return;
        }
        releaseLock(&cThread->stateLock);
        Timer::SleepCurrentThread(timeout); // TODO: Find a better way to do this
    }
}