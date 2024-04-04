/**
 * @file globals.cpp
 * @brief Implementation for globally accessible variables and functions.
 */
#include "interprocess.h"

#include <thread>
#include <stdio.h>
#include <iostream>

#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include <thread>
#include <ctime>
#include <unistd.h>

using namespace boost::interprocess;
using namespace std::literals;

#if _WIN32
#define EXPORTS(x)  x __cdecl
#else
#define EXPORTS(x)  x
#endif


typedef struct {
    Packet audio[QUEUE_SIZE];
    Packet video[QUEUE_SIZE];
    int audio_order[QUEUE_SIZE];
    int video_order[QUEUE_SIZE];


    Event events[EVENT_TYPE_MAX];
    interprocess_mutex lock;
}SharedMemoryInternal;


EXPORTS(void) 
lock_shared_memory(SharedMemory* memory){
    SharedMemoryInternal* internal = (SharedMemoryInternal*) memory;
    internal->lock.lock();
}

EXPORTS(void) 
unlock_shared_memory(SharedMemory* memory){
    SharedMemoryInternal* internal = (SharedMemoryInternal*) memory;
    internal->lock.unlock();
}


std::string gen_random(const int len) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::string tmp_s;
    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    
    return tmp_s;
}

std::string random = gen_random(12);
managed_shared_memory segment(create_only, random.c_str(), 2 * sizeof(SharedMemory));



EXPORTS(void) 
init_shared_memory(SharedMemory* memory){
    for (int i = 0; i < QUEUE_SIZE; i++) {
        memory->audio_order[i] = -1;
        memory->video_order[i] = -1;
    }

    for (int i = 0; i < EventType::EVENT_TYPE_MAX; i++) 
        memory->events[i].read = 1;
}



EXPORTS(void) 
deinit_shared_memory() {
    shared_memory_object::remove(random.c_str());
}


EXPORTS(SharedMemory*) 
allocate_shared_memory(long long* handle) {
    //Allocate a portion of the segment (raw memory)
    std::size_t free_memory = segment.get_free_memory();
    SharedMemory* memory = (SharedMemory*)segment.allocate(sizeof(SharedMemory));
    init_shared_memory(memory);

    //Check invariant
    if(free_memory <= segment.get_free_memory())
        return NULL;

    //An handle from the base address can identify any byte of the shared 
    //memory segment even if it is mapped in different base addresses
    managed_shared_memory::handle_t hnd = segment.get_handle_from_address((void*)memory);
    *handle = hnd;

    // thread_test.join();
    return memory;
}


EXPORTS(void) 
free_shared_memory(SharedMemory* buffer) {
    segment.deallocate(buffer);
}