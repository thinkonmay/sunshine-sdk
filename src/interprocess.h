/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "thread_pool.h"
#include "thread_safe.h"

#include <boost/interprocess/sync/interprocess_mutex.hpp>


#include <smemory.h>

typedef struct {
    Queue queues[QueueType::Max];
    Event events[EVENT_TYPE_MAX];
    boost::interprocess::interprocess_mutex lock;
}SharedMemoryInternal;

SharedMemoryInternal*
obtain_shared_memory(char* key);

void 
push_packet(SharedMemoryInternal* memory, void* data, int size, Metadata metadata);

void 
raise_event(SharedMemoryInternal* memory, EventType type, Event event);

int
peek_event(SharedMemoryInternal* memory, EventType type);

Event
pop_event(SharedMemoryInternal* memory, EventType type);