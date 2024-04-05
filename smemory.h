#define QUEUE_SIZE 16
#define PACKET_SIZE 32 * 1024

enum QueueType {
    Video0,
    Video1,
    Audio,
    Microphone,
    Max
};

typedef enum _EventType {
    POINTER_VISIBLE,
    CHANGE_BITRATE,
    CHANGE_FRAMERATE,
    IDR_FRAME,

    STOP,
    HDR_CALLBACK,
    EVENT_TYPE_MAX
} EventType;

typedef struct {
    int is_idr;
    enum QueueType type;
}Metadata;

typedef struct {
    int size;
    Metadata metadata;
    char data[PACKET_SIZE];
} Packet;

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
    Queue queues[Max];
    Event events[EVENT_TYPE_MAX];
}SharedMemory;