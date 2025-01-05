/**
 * @file globals.cpp
 * @brief Implementation for globally accessible variables and functions.
 */
#include "interprocess.h"
#include "logging.h"

#include <thread>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <sstream>

void
init_shared_memory(SharedMemory* memory){
    for (int j = 0; j < QueueType::QueueMax; j++) {
        for (int k = 0; k < EventType::EventMax; k++) 
            memory->queues[j].events[k].read = 1;

        memory->queues[j].index = QUEUE_SIZE - 1;
    }
}

void 
push_packet(Queue* queue, 
                  void* data, 
                  int size, 
                  PacketMetadata metadata){
    // wait while queue is full
    auto new_index = queue->index + 1;

    auto real_index = new_index % QUEUE_SIZE;
    Packet* block = &queue->array[real_index];
    memcpy(block->data,data,size);
    block->size = size;
    block->metadata = metadata;

    //always update index after write data
    queue->index = new_index;
}


void 
raise_event(Queue* queue, EventType type, Event event){
    event.read = false;
    memcpy(&queue->events[type],&event,sizeof(Event));
}

int
peek_event(Queue* memory, EventType type){
    return !memory->events[type].read;
}

Event
pop_event(Queue* queue, EventType type){
    queue->events[type].read = true;
    BOOST_LOG(debug) << "Receive event " << type << ", value: "<< queue->events[type].value_number;
    return queue->events[type];
}