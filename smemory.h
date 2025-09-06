#ifndef __SMEMORY__
#define __SMEMORY__
#define OUT_QUEUE_SIZE 8
#define IN_QUEUE_SIZE 8

#define MAX_DISPLAY 3
#define TAG_SIZE 32 * 1024

#define MEDIA_PACKET_SIZE 5 * 1024 * 1024
#define DATA_PACKET_SIZE 1024

typedef struct {
    int active;
    int codec;

    int env_width, env_height;
    int width, height;
    float client_offsetX, client_offsetY;
    float offsetX, offsetY;

    float scalar_inv;
}QueueMetadata;

typedef struct {
	int size;
    char data[MEDIA_PACKET_SIZE];
} MediaPacket;
typedef struct {
	int size;
    char data[DATA_PACKET_SIZE];
} DataPacket;

typedef struct _MediaQueue{
	int inindex;
	int outindex;
    MediaPacket incoming[IN_QUEUE_SIZE];
    DataPacket outgoing[OUT_QUEUE_SIZE];
}MediaQueue;

typedef struct _DataQueue{
	int inindex;
	int outindex;
    DataPacket incoming[IN_QUEUE_SIZE];
    DataPacket outgoing[OUT_QUEUE_SIZE];
}DataQueue;

typedef struct _DisplayQueue{
	MediaQueue internal;
    QueueMetadata metadata;
}DisplayQueue;

typedef struct _HIDQueue{
	DataQueue internal;
    QueueMetadata metadata[MAX_DISPLAY];
}HIDQueue;

typedef struct _MediaMemory {
	DisplayQueue video[MAX_DISPLAY];
	DataQueue audio;
}MediaMemory;

typedef struct _DataMemory {
	DataQueue audio;
	HIDQueue data;
	char worker_info[TAG_SIZE];
	int worker_info_size;
}DataMemory;

#endif