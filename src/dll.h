/*
*/

extern "C" {
__declspec(dllexport) int __cdecl Init(
  int video_width,
  int video_height,
  int video_bitrate,
  int video_framerate,
  int video_codec
);

__declspec(dllexport) int  __cdecl StartQueue(int media_type);
__declspec(dllexport) int  __cdecl PopFromQueue(int media_type, void* data,int* duration);
__declspec(dllexport) void __cdecl RaiseEvent(int event_id,int value);

__declspec(dllexport) void __cdecl Wait();
__declspec(dllexport) void __cdecl Stop();
}