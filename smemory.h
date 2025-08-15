#define OUT_QUEUE_SIZE 8
#define IN_QUEUE_SIZE 8

#define TAG_SIZE 8192
#define PACKET_SIZE 5 * 1024 * 1024

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
    char data[PACKET_SIZE];
} Packet;

typedef struct _Queue{
	int inindex;
	int outindex;
    Packet incoming[IN_QUEUE_SIZE];
    Packet outgoing[OUT_QUEUE_SIZE];
}Queue;

typedef struct _DisplayQueue{
	Queue internal;
    QueueMetadata metadata;
}DisplayQueue;

typedef struct _Memory {
	DisplayQueue video;
	Queue audio;
	Queue data;


	char worker_info[TAG_SIZE];
}Memory;