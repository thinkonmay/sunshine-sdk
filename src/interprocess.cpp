/**
 * @file globals.cpp
 * @brief Implementation for globally accessible variables and functions.
 */
#include "interprocess.h"

#include <thread>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <sstream>

#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

using namespace boost::interprocess;
using namespace std::literals;




SharedMemory* obtain_shared_memory(char* rand) {
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

int queue_size(int* queue) {
    int i = 0;
    while (*queue != -1 && i != QUEUE_SIZE){ // wait while queue is full
        queue++;
        i++;
    } 

    return i;
}

int find_available_slot(int* orders) {
    int available = -1;
    // find available packet slot
    for (int k = 0; k < QUEUE_SIZE; k++) {
        int fnd = 0;

        int* copy = orders;
        int j = 0;
        while ( *copy != -1 && j != QUEUE_SIZE) {
            if (*copy == k) {
                fnd = 1;
                break;
            }

            j++;
            copy++;
        }

        if (!fnd) {
            available = k;
            break;
        }
    }

    return available;
}


void 
push_audio_packet(SharedMemory* memory, void* data, int size){
    // wait while queue is full
    while (queue_size(memory->queues[QueueType::Audio].order) == QUEUE_SIZE) 
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);


    int available = find_available_slot(memory->queues[QueueType::Audio].order);
    memory->queues[QueueType::Audio].order[queue_size(memory->queues[QueueType::Audio].order)] = available;
    Packet* block = &memory->queues[QueueType::Audio].array[available];
    memcpy(block->data,data,size);
    block->size = size;

}

void 
push_video_packet(SharedMemory* memory, 
                  void* data, 
                  int size, 
                  VideoMetadata metadata){
    // wait while queue is full
    while (queue_size(memory->queues[QueueType::Video].order) == QUEUE_SIZE) 
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);

    int available = find_available_slot(memory->queues[QueueType::Video].order);
    memory->queues[QueueType::Video].order[queue_size(memory->queues[QueueType::Video].order)] = available;
    Packet* block = &memory->queues[QueueType::Video].array[available];
    memcpy(block->data,data,size);
    block->size = size;
    block->metadata = metadata;
}

int
peek_video_packet(SharedMemory* memory){
    return memory->queues[QueueType::Video].order[0] != -1;
}

int
peek_audio_packet(SharedMemory* memory){
    return memory->queues[QueueType::Audio].order[0] != -1;
}

void 
pop_audio_packet(SharedMemory* memory, void* data, int* size){
    while (!peek_audio_packet(memory))
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);
    // std::cout << "Audio buffer size : " << queue_size(memory->queues[QueueType::Audio].order) << "\n";

    int pop = memory->queues[QueueType::Audio].order[0];
    Packet *block = &memory->queues[QueueType::Audio].array[pop];
    memcpy(data,block->data,block->size);
    *size = block->size;

    // reorder
    for (int i = 0; i < QUEUE_SIZE - 1; i++)
        memory->queues[QueueType::Audio].order[i] = memory->queues[QueueType::Audio].order[i+1];
    
    memory->queues[QueueType::Audio].order[QUEUE_SIZE - 1] = -1;
    
}

VideoMetadata
pop_video_packet(SharedMemory* memory, void* data, int* size){
    while (!peek_video_packet(memory))
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);
    // std::cout << "Video buffer size : " << queue_size(memory->queues[QueueType::Video].order) << "\n";

    int pop = memory->queues[QueueType::Video].order[0];
    Packet *block = &memory->queues[QueueType::Video].array[pop];
    memcpy(data,block->data,block->size);
    *size = block->size;
    auto copy = block->metadata;

    // reorder
    for (int i = 0; i < QUEUE_SIZE - 1; i++)
        memory->queues[QueueType::Video].order[i] = memory->queues[QueueType::Video].order[i+1];
    
    memory->queues[QueueType::Video].order[QUEUE_SIZE - 1] = -1;

    
    return copy;
}

void 
raise_event(SharedMemory* memory, EventType type, Event event){
    event.read = false;
    memcpy(&memory->events[type],&event,sizeof(Event));
}

int
peek_event(SharedMemory* memory, EventType type){
    return !memory->events[type].read;
}

Event
pop_event(SharedMemory* memory, EventType type){
    memory->events[type].read = true;
    return memory->events[type];
}

void
wait_event(SharedMemory* memory, EventType type){
    while(memory->events[type].read)
        std::this_thread::sleep_for(1ms);
}