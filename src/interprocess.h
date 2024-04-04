/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "thread_pool.h"
#include "thread_safe.h"

#include <boost/interprocess/sync/interprocess_mutex.hpp>


using namespace boost::interprocess;

#define QUEUE_SIZE 16
#define PACKET_SIZE 32 * 1024

typedef struct {
    int is_idr;


}VideoMetadata;

typedef struct {
    int size;
    VideoMetadata metadata;
    char data[PACKET_SIZE];
} Packet;

typedef enum _EventType {
    POINTER_VISIBLE,
    CHANGE_BITRATE,
    CHANGE_FRAMERATE,
    CHANGE_DISPLAY,
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

typedef struct {
    Packet audio[QUEUE_SIZE];
    Packet video[QUEUE_SIZE];
    int audio_order[QUEUE_SIZE];
    int video_order[QUEUE_SIZE];


    Event events[EVENT_TYPE_MAX];
    interprocess_mutex lock;
}SharedMemory;

void 
init_shared_memory(SharedMemory* memory);

void 
push_audio_packet(SharedMemory* memory, void* data, int size);

void 
push_video_packet(SharedMemory* memory, void* data, int size, VideoMetadata metadata);

int
peek_video_packet(SharedMemory* memory);

int
peek_audio_packet(SharedMemory* memory);

void 
pop_audio_packet(SharedMemory* memory, void* data, int* size);

VideoMetadata
pop_video_packet(SharedMemory* memory, void* data, int* size);

void 
raise_event(SharedMemory* memory, EventType type, Event event);

int
peek_event(SharedMemory* memory, EventType type);

Event
pop_event(SharedMemory* memory, EventType type);

void
wait_event(SharedMemory* memory, EventType type);
