/*
*/



typedef void (*DECODE_CALLBACK) (void* data,int size);

extern "C" {
__declspec(dllexport) void __cdecl Init();
__declspec(dllexport) int  __cdecl StartCallback(DECODE_CALLBACK callback);
__declspec(dllexport) int  __cdecl StartQueue();
__declspec(dllexport) int  __cdecl PopFromQueue(void* data,int* duration);
__declspec(dllexport) void __cdecl Wait();
}