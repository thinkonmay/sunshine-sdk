#define QUEUE_SIZE 256
#define PACKET_SIZE 32 * 1024

enum QueueType {
    Video0,
    Video1,
    Audio,
    Microphone,
    QueueMax
};

typedef enum _EventType {
    Pointer,
    Bitrate,
    Framerate,
    Idr,
    Hdr,
    Stop,
    EventMax
} EventType;

typedef struct {
    int is_idr;
}PacketMetadata;

typedef struct {
    int active;
    char display[64];
    int codec;
}QueueMetadata;

typedef struct {
    int size;
    PacketMetadata metadata;
    char data[PACKET_SIZE];
} Packet;

typedef enum _DataType {
    HDR_INFO,
    NUMBER,
    STRING,
} DataType;

typedef struct {
    int read;
    DataType type;
    int data_size;
    int value_number;
    char value_raw[PACKET_SIZE];
} Event;

typedef struct _Queue{
    int index;
    QueueMetadata metadata;
    Event events[EventMax];
    Packet array[QUEUE_SIZE];
}Queue;

typedef struct {
    Queue queues[QueueMax];
}SharedMemory;