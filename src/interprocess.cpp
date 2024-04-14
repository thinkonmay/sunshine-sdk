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

#include <boost/interprocess/managed_shared_memory.hpp>

using namespace boost::interprocess;




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
    BOOST_LOG(info) << "Receive event " << type << ", value: "<< queue->events[type].value_number;
    return queue->events[type];
}