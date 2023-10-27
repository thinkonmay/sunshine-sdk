/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>


// local includes
#include "config.h"
#include "main.h"
#include "dll.h"
#include "platform/common.h"
#include "video.h"



safe::mail_t mail::man;

using namespace std::literals;

bool display_cursor = true;


static video::config_t monitor   = { 1920, 1080, 60, 6000, 1, 0, 1, 0, 0 };
static std::shared_ptr<safe::mail_raw_t> privatemail;
static std::chrono::steady_clock::time_point start; 
extern int __cdecl
Init(
  int width,
  int height,
  int bitrate,
  int framerate,
  int codec
) {
  start = std::chrono::steady_clock::now(); 
  monitor.width = width;
  monitor.height = height;
  monitor.framerate = framerate;

  switch (codec)
  {
  case 2: // h265
    monitor.videoFormat = 1;
    config::video.hevc_mode = 1;
    config::video.av1_mode = 0;
    break;
  case 3: // av1
    monitor.videoFormat = 2;
    config::video.hevc_mode = 0;
    config::video.av1_mode = 1;
    break;
  default:
    monitor.videoFormat = 0;
    config::video.hevc_mode = 0;
    config::video.av1_mode = 0;
    break;
  }



  mail::man = std::make_shared<safe::mail_raw_t>();






  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.
  auto deinit_guard = platf::init();
  if (!deinit_guard) {
    // BOOST_LOG(error) << "Platform failed to initialize"sv;
    return 1;
  } else if (video::probe_encoders()) {
    // BOOST_LOG(error) << "Video failed to find working encoder"sv;
    return 1;
  } else {
    // BOOST_LOG(error) << "Hello from thinkmay."sv;
    return 0;
  }

  // auto mic = control->microphone(stream->mapping, stream->channelCount, stream->sampleRate, frame_size);

  // Create signal handler after logging has been initialized
  mail::man->event<bool>(mail::shutdown)->view();
}



extern void __cdecl
Wait(){
  mail::man->event<bool>(mail::shutdown)->view();
}





/**
 * @brief Main application entry point.
 * @param argc The number of arguments.
 * @param argv The arguments.
 *
 * EXAMPLES:
 * ```cpp
 * main(1, const char* args[] = {"sunshine", nullptr});
 * ```
 */
extern int __cdecl 
StartQueue() {
  void* null = 0;
  privatemail = std::make_shared<safe::mail_raw_t>();
  auto capture = std::thread {[&](){video::capture(privatemail, monitor, (void*)null);}};

  // Create signal handler after logging has been initialized
  mail::man->event<bool>(mail::shutdown)->view();
  return 0;
}

int __cdecl 
PopFromQueue(void* data,int* duration) 
{
  auto packets = mail::man->queue<video::packet_t>(mail::video_packets);
  auto packet = packets->pop();

  if(packet->frame_timestamp) {
    auto duration_to_latency = [](const std::chrono::steady_clock::duration &duration) {
      const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
      return std::clamp<decltype(duration_ns)>((duration_ns + 50) / 100, 0, std::numeric_limits<int>::max());
    };


    *duration = duration_to_latency(*packet->frame_timestamp - start);
    start = *packet->frame_timestamp;
  }

  // packet->
  memcpy(data,packet->data(),packet->data_size());
  return packet->data_size();
}

void __cdecl 
RaiseEvent(int event_id, int value) 
{
  switch (event_id)
  {
  case 1: // IDR FRAME
    if (privatemail)
      privatemail->event<bool>(mail::idr)->raise(true);
    break;
  default:
    break;
  }
}

void 
DecodeCallbackEx(void* data, int size) {
  printf("received packet with size %d\n",size);
}

/**
 * @brief Main application entry point.
 * @param argc The number of arguments.
 * @param argv The arguments.
 *
 * EXAMPLES:
 * ```cpp
 * main(1, const char* args[] = {"sunshine", nullptr});
 * ```
 */
int
main(int argc, char *argv[]) {
  Init(1920,1080,6000,60,1);
  // StartCallback(DecodeCallbackEx);

  auto video = std::thread {[](){
    auto shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);
    // Video traffic is sent on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    int duration = 0;
    void* data = malloc(100 * 1000 * 1000);
    while (true) {
      int size = PopFromQueue(data,&duration);
      if (shutdown_event->peek()) {
        break;
      }



      DecodeCallbackEx(data,size);
    }

    shutdown_event->raise(true);
  }};

  StartQueue();
  return 0;
}