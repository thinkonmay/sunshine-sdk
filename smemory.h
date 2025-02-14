#define QUEUE_SIZE 64
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
	void* handle;
    QueueMetadata metadata;

    Packet incoming[QUEUE_SIZE];
    Packet outcoming[QUEUE_SIZE];
}Queue;