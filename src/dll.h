/*
*/

extern "C" {


typedef struct _VideoPipeline  VideoPipeline;


typedef enum _EventType {
    POINTER_VISIBLE,
    CHANGE_BITRATE,
    IDR_FRAME,

    STOP
}EventType;

__declspec(dllexport) VideoPipeline* __cdecl StartQueue( int video_width,
                                                        int video_height,
                                                        int video_bitrate,
                                                        int video_framerate,
                                                        int video_codec,
                                                        char* display_name);

__declspec(dllexport) int  __cdecl PopFromQueue(VideoPipeline* pipeline, 
                                                void* data,
                                                int* duration);

__declspec(dllexport) void __cdecl RaiseEvent(VideoPipeline* pipeline,
                                              EventType event,
                                              int value);

__declspec(dllexport) void  __cdecl WaitEvent(VideoPipeline* pipeline,
                                                  EventType event,
                                                  int* value);


__declspec(dllexport) int  __cdecl PeekEvent(VideoPipeline* pipeline,
                                                  EventType event,
                                                  int* value);

typedef VideoPipeline* (*STARTQUEUE)				  ( int video_width,
                                                        int video_height,
                                                        int video_bitrate,
                                                        int video_framerate,
                                                        int video_codec,
                                                        char* display_name);

typedef int  		   (*POPFROMQUEUE)			(VideoPipeline* pipeline, 
                                                void* data,
                                                int* duration);

typedef void 			(*RAISEEVENT)		 (VideoPipeline* pipeline,
                                              EventType event,
                                              int value);

typedef void  			(*WAITEVENT)			(VideoPipeline* pipeline,
                                                  EventType event,
                                                  int* value);

typedef int             (*PEEKEVENT)            (VideoPipeline* pipeline,
                                                  EventType event,
                                                  int* value);
}