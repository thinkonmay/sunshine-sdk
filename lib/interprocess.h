/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#if _WIN32
#define EXPORT(x)  extern __declspec(dllexport) x __cdecl
#else
#define EXPORT(x) __attribute__((visibility("default"))) extern x
#endif

extern "C" {
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
}SharedMemory;


EXPORT(SharedMemory*) allocate_shared_memory(char* rand,long long* handle) ;
EXPORT(void) lock_shared_memory(SharedMemory* memory);
EXPORT(void) unlock_shared_memory(SharedMemory* memory);
EXPORT(void) free_shared_memory(SharedMemory* buffer);
EXPORT(void) deinit_shared_memory();
}