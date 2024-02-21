/*
 */

extern "C" {

typedef struct _VideoPipeline Pipeline;

typedef enum _EventType {
    POINTER_VISIBLE,
    CHANGE_BITRATE,
    CHANGE_FRAMERATE,
    CHANGE_DISPLAY,
    IDR_FRAME,

    STOP
} EventType;

typedef enum _Codec {
    H264 = 1,
    H265,
    AV1,
    OPUS,
} Codec;

__declspec(dllexport) Pipeline* __cdecl StartQueue(int video_codec);

__declspec(dllexport) int __cdecl PopFromQueue(Pipeline* pipeline,
                                               void* data, int* duration);

__declspec(dllexport) void __cdecl RaiseEvent(Pipeline* pipeline,
                                              EventType event, int value);

__declspec(dllexport) void __cdecl RaiseEventS(Pipeline* pipeline,
                                               EventType event, char* value);

__declspec(dllexport) void __cdecl WaitEvent(Pipeline* pipeline,
                                             EventType event);

__declspec(dllexport) int __cdecl PeekEvent(Pipeline* pipeline,
                                            EventType event);

typedef Pipeline* (*STARTQUEUE)(int video_codec);

typedef int (*POPFROMQUEUE)(Pipeline* pipeline, void* data, int* duration);

typedef void (*RAISEEVENT)(Pipeline* pipeline, EventType event, int value);

typedef void (*RAISEEVENTS)(Pipeline* pipeline, EventType event,
                            char* value);

typedef void (*WAITEVENT)(Pipeline* pipeline, EventType event);

typedef int (*PEEKEVENT)(Pipeline* pipeline, EventType event);
}