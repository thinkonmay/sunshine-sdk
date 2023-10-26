/*
*/



typedef void (*DECODE_CALLBACK) (void* data,int size);

extern "C" {
__declspec(dllexport) void __cdecl Init(
  int width,
  int height,
  int bitrate,
  int framerate,
  int codec
);
__declspec(dllexport) int  __cdecl StartCallback(DECODE_CALLBACK callback);
__declspec(dllexport) int  __cdecl StartQueue();
__declspec(dllexport) int  __cdecl PopFromQueue(void* data,int* duration);
__declspec(dllexport) void __cdecl Wait();
__declspec(dllexport) void __cdecl RaiseEvent(int event_id,int value);
}