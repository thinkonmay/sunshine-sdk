/**
 * @file globals.cpp
 * @brief Implementation for globally accessible variables and functions.
 */
#include "interprocess.h"

#include <thread>
#include <boost/interprocess/sync/scoped_lock.hpp>

using namespace boost::interprocess;
using namespace std::literals;




void 
init_shared_memory(SharedMemory* memory){
    for (int i = 0; i < QUEUE_SIZE; i++) {
        memory->audio_order[i] = -1;
        memory->video_order[i] = -1;
    }

    for (int i = 0; i < EventType::EVENT_TYPE_MAX; i++) 
        memory->events[i].read = 1;
}


int queue_size(int* queue) {
    int i = 0;
    while (*queue != -1 && i != QUEUE_SIZE){ // wait while queue is full
        queue++;
        i++;
    } 

    return i;
}


void 
push_audio_packet(SharedMemory* memory, void* data, int size){
    // wait while queue is full
    while (queue_size(memory->audio_order) == QUEUE_SIZE) 
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);

    int available = -1;
    // find available packet slot
    for (int k = 0; k < QUEUE_SIZE; k++) {
        int fnd = 0;

        int j = 0;
        while ( memory->audio_order[j] != -1 && j != QUEUE_SIZE) {
            if (memory->audio_order[j] == k) {
                fnd = 1;
                break;
            }

            j++;
        }

        if (fnd)
            continue;
        
        available = k;
    }
    
    
    memory->audio_order[queue_size(memory->audio_order)] = available;
    Packet* block = &memory->audio[available];
    memcpy(block->data,data,size);
    block->size = size;

}

void 
push_video_packet(SharedMemory* memory, 
                  void* data, 
                  int size, 
                  VideoMetadata metadata){
    // wait while queue is full
    while (queue_size(memory->audio_order) == QUEUE_SIZE) 
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);

    int available = -1;
    for (int k = 0; k < QUEUE_SIZE; k++) {
        int fnd = 0;

        int j = 0;
        while ( memory->video_order[j] != -1 && j != QUEUE_SIZE) {
            if (memory->video_order[j] == k) {
                fnd = 1;
                break;
            }

            j++;
        }

        if (fnd)
            continue;
        
        available = k;
    }
    
    
    memory->video_order[queue_size(memory->video_order)] = available;
    Packet* block = &memory->video[available];
    memcpy(block->data,data,size);
    block->size = size;
    block->metadata = metadata;
}

int
peek_video_packet(SharedMemory* memory){
    return memory->video_order[0] != -1;
}

int
peek_audio_packet(SharedMemory* memory){
    return memory->audio_order[0] != -1;
}

void 
pop_audio_packet(SharedMemory* memory, void* data, int* size){
    while (!peek_audio_packet(memory))
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);

    int pop = memory->audio_order[0];
    Packet *block = &memory->audio[pop];
    memcpy(data,block->data,block->size);
    *size = block->size;

    // reorder
    for (int i = 0; i < QUEUE_SIZE - 1; i++)
        memory->audio_order[i] = memory->audio_order[i+1];
    
    memory->audio_order[QUEUE_SIZE - 1] = -1;
    
}

VideoMetadata
pop_video_packet(SharedMemory* memory, void* data, int* size){
    while (!peek_video_packet(memory))
        std::this_thread::sleep_for(1ms);

    scoped_lock<interprocess_mutex> lock(memory->lock);

    int pop = memory->video_order[0];
    Packet *block = &memory->video[pop];
    memcpy(data,block->data,block->size);
    *size = block->size;
    auto copy = block->metadata;

    // reorder
    for (int i = 0; i < QUEUE_SIZE - 1; i++)
        memory->video_order[i] = memory->video_order[i+1];
    
    memory->video_order[QUEUE_SIZE - 1] = -1;

    
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