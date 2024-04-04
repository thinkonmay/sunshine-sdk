/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "thread_pool.h"
#include "thread_safe.h"

#include <boost/interprocess/sync/interprocess_mutex.hpp>



#define QUEUE_SIZE 16
#define PACKET_SIZE 32 * 1024

enum QueueType {
    Video0,
    Video1,
    Audio,
    Microphone,
    Max
};

typedef struct {
    int is_idr;
    QueueType type;
}Metadata;

typedef struct {
    int size;
    Metadata metadata;
    char data[PACKET_SIZE];
} Packet;

typedef enum _EventType {
    POINTER_VISIBLE,
    CHANGE_BITRATE,
    CHANGE_FRAMERATE,
    IDR_FRAME,

    STOP,
    HDR_CALLBACK,
    EVENT_TYPE_MAX
} EventType;

typedef enum _DataType {
    HDR_INFO,
} DataType;

typedef struct {
    int value_number;
    char value_raw[PACKET_SIZE];
    int data_size;

    DataType type;

    int read;
} Event;


typedef struct _Queue{
    Packet array[QUEUE_SIZE];
    int order[QUEUE_SIZE];
}Queue;

typedef struct {
    Queue queues[QueueType::Max];
    Event events[EVENT_TYPE_MAX];
    boost::interprocess::interprocess_mutex lock;
}SharedMemory;

SharedMemory*
obtain_shared_memory(char* key);

void 
push_packet(SharedMemory* memory, void* data, int size, Metadata metadata);

void 
raise_event(SharedMemory* memory, EventType type, Event event);

int
peek_event(SharedMemory* memory, EventType type);

Event
pop_event(SharedMemory* memory, EventType type);