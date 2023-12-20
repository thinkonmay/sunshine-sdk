/*
*/

extern "C" {


typedef struct _VideoPipeline  VideoPipeline;


typedef enum _EventType {
    POINTER_VISIBLE,
    CHANGE_BITRATE,
    CHANGE_DISPLAY,
    IDR_FRAME,

    STOP
}EventType;

typedef enum _Codec {
    H264 = 1,
    H265,
    AV1,
}Codec;

__declspec(dllexport) VideoPipeline* __cdecl StartQueue(int video_codec,
                                                        char* display_name);

__declspec(dllexport) int  __cdecl PopFromQueue(VideoPipeline* pipeline, 
                                                void* data,
                                                int* duration);

__declspec(dllexport) void __cdecl RaiseEvent(VideoPipeline* pipeline,
                                              EventType event,
                                              int value);

__declspec(dllexport) void __cdecl RaiseEventS(VideoPipeline* pipeline,
                                              EventType event,
                                              char* value);

__declspec(dllexport) void  __cdecl WaitEvent(VideoPipeline* pipeline,
                                                  EventType event);


__declspec(dllexport) int  __cdecl PeekEvent(VideoPipeline* pipeline,
                                                  EventType event);

typedef VideoPipeline* (*STARTQUEUE)				  ( int video_codec,
                                                        char* display_name);

typedef int  		   (*POPFROMQUEUE)			(VideoPipeline* pipeline, 
                                                void* data,
                                                int* duration);

typedef void 			(*RAISEEVENT)		 (VideoPipeline* pipeline,
                                              EventType event,
                                              int value);

typedef void 			(*RAISEEVENTS)		 (VideoPipeline* pipeline,
                                              EventType event,
                                              char* value);

typedef void  			(*WAITEVENT)			(VideoPipeline* pipeline,
                                                  EventType event);

typedef int             (*PEEKEVENT)            (VideoPipeline* pipeline,
                                                  EventType event);
}