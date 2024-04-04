/**
 * @file globals.cpp
 * @brief Implementation for globally accessible variables and functions.
 */
#include "interprocess.h"
#include <thread>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <vector>

#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>


using namespace boost::interprocess;
using namespace std::literals;

#if _WIN32
#define EXPORTS(x)  x __cdecl
#else
#define EXPORTS(x)  x
#endif


typedef struct {
    Queue queues[QueueType::Max];
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
    std::string tmp_s = "thinkmay";
    tmp_s.reserve(len);

    srand(std::chrono::duration_cast<std::chrono::nanoseconds>
              (std::chrono::high_resolution_clock::now().time_since_epoch()).count());
    for (int i = 0; i < len; ++i) {
        int j = rand() % (sizeof(alphanum) - 1);
        tmp_s += alphanum[j];
    }

    return tmp_s;
}

std::string randkey = gen_random(20);
managed_shared_memory segment(create_only, randkey.c_str(), 2 * sizeof(SharedMemory));



void
init_shared_memory(SharedMemory* memory){
    for (int i = 0; i < QUEUE_SIZE; i++) {
        memory->queues[QueueType::Audio].order[i] = -1;
        memory->queues[QueueType::Video].order[i] = -1;
    }

    for (int i = 0; i < EventType::EVENT_TYPE_MAX; i++) 
        memory->events[i].read = 1;
}



EXPORTS(void) 
deinit_shared_memory() {
    shared_memory_object::remove(randkey.c_str());
}

EXPORTS(SharedMemory*) 
obtain_shared_memory(char* rand){
    std::vector<std::string> strings;

    std::istringstream f(rand);
    std::string s;    
    while (getline(f, s, ';')) {
        strings.push_back(s);
    }

    if(strings.size() != 2)
        return NULL;
    
    std::stringstream h;
    std::stringstream k;
    long long handle;  h << strings.at(1); h >> handle;
    std::string key;  k << strings.at(0); k >> key;

    //Open managed segment
    static managed_shared_memory segment(open_only, key.c_str());

    //Get buffer local address from handle
    SharedMemory* memory = (SharedMemory*)segment.get_address_from_handle(handle);

    return memory;
}

EXPORTS(SharedMemory*) 
allocate_shared_memory(char* rand) {
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

    std::stringstream s; s << randkey << ";" << hnd; 
    std::string c; s >> c;
    memcpy(rand,c.c_str(),c.size());
    return memory;
}


EXPORTS(void) 
free_shared_memory(SharedMemory* buffer) {
    segment.deallocate(buffer);
}