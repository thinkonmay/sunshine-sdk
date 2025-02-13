/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "thread_pool.h"
#include "thread_safe.h"

#include <smemory.h>


SharedMemory*
init_shared_memory(char* data);

void 
push_packet(Queue* memory, void* data, int size, PacketMetadata metadata);

void 
raise_event(Queue* memory, EventType type, Event event);

int
peek_event(Queue* memory, EventType type);

Event
pop_event(Queue* memory, EventType type);