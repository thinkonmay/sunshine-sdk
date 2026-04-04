#ifndef WGC_H
#define WGC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*frame_callback_t)(int width, int height, void* frame_data, void* user_data);

typedef struct {
    int width;
    int height;
    int status;
} capture_status_t;

int start_capture(frame_callback_t callback, void* user_data);
void stop_capture();

#ifdef __cplusplus
}
#endif

#endif
