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
#include <windows.h>
#pragma comment(lib, "user32.lib")
#define BUF_SIZE 256

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



void*
map_file(char* name)
{
   HANDLE hMapFile;

   hMapFile = OpenFileMappingA(
                   FILE_MAP_ALL_ACCESS,   // read/write access
                   FALSE,                 // do not inherit the name
                   name);               // name of mapping object

   if (hMapFile == NULL) {
      BOOST_LOG(error) << "Could not open file mapping object " <<  GetLastError();
      return nullptr;
   }

    void* pBuf = MapViewOfFile(hMapFile, // handle to map object
               FILE_MAP_ALL_ACCESS,  // read/write permission
               0,
               0,
               BUF_SIZE);

    if (pBuf == NULL) {
        BOOST_LOG(error) << "Could not map view of file (%d) " << GetLastError();
        CloseHandle(hMapFile);
        return nullptr;
    }

   return pBuf;
}
SharedMemory*
init_shared_memory(char* data){
    SharedMemory* memory = (SharedMemory*)map_file(data);
    return memory;
}