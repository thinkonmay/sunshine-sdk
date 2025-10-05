/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <codecvt>
#include <csignal>
#include <fstream>
#include <iostream>
#include <smemory.h>

// local includes
#include "globals.h"
#include "interprocess.h"
#include "logging.h"
#include "version.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "config.h"
#include "file_handler.h"
#include "platform/common.h"

#ifdef _WIN32
#include <Windows.h>
#endif

enum StatusCode {
  NORMAL_EXIT,
  INVALID_IVSHMEM,
  NO_ENCODER_AVAILABLE
};
enum QueueType {
  Video,
  Audio,
};
enum EventType {
  Pointer,
  Bitrate,
  Framerate,
  Idr,
  Hdr,
  Stop,
  BufferOverflow,
  EventMax
};

using namespace std::literals;

std::map<int, std::function<void()>> signal_handlers;
void
on_signal_forwarder(int sig) {
  signal_handlers.at(sig)();
}

template <class FN>
void
on_signal(int sig, FN &&fn) {
  signal_handlers.emplace(sig, std::forward<FN>(fn));

  std::signal(sig, on_signal_forwarder);
}



std::vector<std::string> 
split (std::string s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss (s);
    std::string item;

    while (getline (ss, item, delim)) {
        result.push_back (item);
    }

    return result;
}


std::vector<uint8_t> replace(const std::string_view &original, const std::string_view &old, const std::string_view &_new) {
  std::vector<uint8_t> replaced;
  replaced.reserve(original.size() + _new.size() - old.size());

  auto begin = std::begin(original);
  auto end = std::end(original);
  auto next = std::search(begin, end, std::begin(old), std::end(old));

  std::copy(begin, next, std::back_inserter(replaced));
  if (next != end) {
    std::copy(std::begin(_new), std::end(_new), std::back_inserter(replaced));
    std::copy(next + old.size(), end, std::back_inserter(replaced));
  }

  return replaced;
}

int
main(int argc, char *argv[]) {
#ifdef _WIN32
  // Switch default C standard library locale to UTF-8 on Windows 10 1803+
  setlocale(LC_ALL, ".UTF-8");
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale::global(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
#pragma GCC diagnostic pop

  mail::man = std::make_shared<safe::mail_raw_t>();
  auto ivshmem = new IVSHMEM(argv[1]);
  ivshmem->Initialize();
  MediaMemory* memory = NULL;
  if (ivshmem->GetSize() < (UINT64)sizeof(MediaMemory)) {
    BOOST_LOG(error) << "Invalid  ivshmem size: "sv << ivshmem->GetSize();
    BOOST_LOG(error) << "Expected ivshmem size: "sv << sizeof(MediaMemory);
    memory = (MediaMemory*)malloc(sizeof(MediaMemory));
  } else {
    BOOST_LOG(info) << "Found ivshmem shared memory"sv;
    memory = (MediaMemory*)ivshmem->GetMemory();
  }

  auto log_deinit_guard = logging::init(config::sunshine.min_log_level);
  if (!log_deinit_guard) {
    BOOST_LOG(error) << "Logging failed to initialize"sv;
  }

  task_pool.start(1);

  // Create signal handler after logging has been initialized
  auto process_shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [process_shutdown_event,ivshmem]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;
    logging::log_flush();
    process_shutdown_event->raise(true);
    ivshmem->DeInitialize();
  });

  on_signal(SIGTERM, [process_shutdown_event,ivshmem]() {
    BOOST_LOG(info) << "Terminate handler called"sv;
    logging::log_flush();
    process_shutdown_event->raise(true);
    ivshmem->DeInitialize();
  });


  // Modify relevant NVIDIA control panel settings if the system has corresponding gpu
  if (nvprefs_instance.load()) {
    // Restore global settings to the undo file left by improper termination of sunshine.exe
    nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
    // Modify application settings for sunshine.exe
    nvprefs_instance.modify_application_profile();
    // Modify global settings, undo file is produced in the process to restore after improper termination
    nvprefs_instance.modify_global_profile();
    // Unload dynamic library to survive driver re-installation
    nvprefs_instance.unload();
  }

  // Wait as long as possible to terminate Sunshine.exe during logoff/shutdown
  SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

  // We must create a hidden window to receive shutdown notifications since we load gdi32.dll
  std::promise<HWND> session_monitor_hwnd_promise;
  auto session_monitor_hwnd_future = session_monitor_hwnd_promise.get_future();
  std::promise<void> session_monitor_join_thread_promise;
  auto session_monitor_join_thread_future = session_monitor_join_thread_promise.get_future();


  auto platf_deinit_guard = platf::init();



  if (!platf_deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
    return StatusCode::NO_ENCODER_AVAILABLE;
  } else if(video::probe_encoders()) {
    BOOST_LOG(error) << "Video failed to find working encoder"sv;
    return StatusCode::NO_ENCODER_AVAILABLE;
  }
  
  auto video_capture = [&](safe::mail_t mail, std::string displayin,int codec){
    video::capture(mail,video::config_t{
      displayin, 1920, 1080, 60, 6000, 1, 0, 1, codec, 0
    },NULL);
  };

  auto audio_capture = [&](safe::mail_t mail){
    audio::capture(mail,audio::config_t{
      10,2,3,0
    },NULL);
  };
    


  auto mail          = std::make_shared<safe::mail_raw_t>();

  auto pull = [process_shutdown_event,mail](MediaQueue* queue){
    auto timer         = platf::create_high_precision_timer();
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto bitrate       = mail->event<int>(mail::bitrate);
    auto framerate     = mail->event<int>(mail::framerate);
    auto idr           = mail->event<bool>(mail::idr);

    auto expected_index = queue->outindex;
    auto last_bitrate = 6;
    char buffer[DATA_PACKET_SIZE] = {0};
    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      while (expected_index == queue->outindex)
        timer->sleep_for(1ms);

      memcpy(buffer,
        queue->outgoing[expected_index].data,
        queue->outgoing[expected_index].size
      );

      expected_index++;
      if (expected_index >= OUT_QUEUE_SIZE)
        expected_index = 0;
      
      switch (buffer[0]) {
      case EventType::Bitrate:
        if (buffer[1] == 0)
          break;
        
        last_bitrate = buffer[1];
        BOOST_LOG(info) << "bitrate changed to " << (buffer[1] * 1000);
        bitrate->raise(buffer[1] * 1000);
        break;
      case EventType::BufferOverflow:
        if (last_bitrate <= 1) 
          break;

        last_bitrate--;
        BOOST_LOG(info) << "overflow, bitrate changed to " << (last_bitrate * 1000);
        bitrate->raise(last_bitrate * 1000);
        break;
      case EventType::Framerate:
        if (buffer[1] < 20)
          break;

        BOOST_LOG(info) << "framerate changed to " << (int)(buffer[1]);
        framerate->raise(buffer[1]);
        break;
      case EventType::Pointer:
        BOOST_LOG(info) << "pointer changed to " << (bool)(buffer[1] != 0);
        display_cursor = buffer[1] != 0;
        break;
      case EventType::Idr:
        BOOST_LOG(debug) << "IDR";
        idr->raise(true);
        break;
      default:
        BOOST_LOG(info) << "invalid message "<< u_int(buffer[0]) << " " << u_int(buffer[1]);
        break;
      }
    }
  };


  auto push_video = [process_shutdown_event](safe::mail_t mail, MediaQueue* queue){
    auto video_packets = mail->queue<video::packet_t>(mail::video_packets);
    auto audio_packets = mail->queue<audio::packet_t>(mail::audio_packets);
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto touch_port    = mail->event<input::touch_port_t>(mail::touch_port);


#ifdef _WIN32 
    platf::adjust_thread_priority(platf::thread_priority_e::critical);
#endif

    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      do {
        uint8_t flags = 0;
        auto packet = video_packets->pop();
        auto findex = packet->frame_index();
        std::string_view payload {(char *) packet->data(), packet->data_size()};
        std::vector<uint8_t> payload_with_replacements;
        uint64_t utimestamp = packet->frame_timestamp.value().time_since_epoch().count();

        if (packet->is_idr() && packet->replacements) {
          for (auto &replacement : *packet->replacements) {
            auto frame_old = replacement.old;
            auto frame_new = replacement._new;

            payload_with_replacements = replace(payload, frame_old, frame_new);
            payload = {(char *) payload_with_replacements.data(), payload_with_replacements.size()};
          }
        }

        if (packet->is_idr())
          flags |= (1 << 0);
        if (packet->after_ref_frame_invalidation)
          flags |= (1 << 1);

        auto updated = queue->inindex + 1;
        if (updated >= IN_QUEUE_SIZE)
          updated = 0;

        queue->incoming[queue->inindex].size = 0;
        copy_to_packet(&queue->incoming[queue->inindex],&findex,sizeof(uint64_t));
        copy_to_packet(&queue->incoming[queue->inindex],&utimestamp,sizeof(uint64_t));
        copy_to_packet(&queue->incoming[queue->inindex],&flags,sizeof(uint8_t));
        copy_to_packet(&queue->incoming[queue->inindex],(void*)payload.data(),payload.size());
        queue->inindex = updated;
      } while (video_packets->peek());
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);
  };

  auto push_audio = [process_shutdown_event](safe::mail_t mail, DataQueue* queue){
    auto video_packets = mail->queue<video::packet_t>(mail::video_packets);
    auto audio_packets = mail->queue<audio::packet_t>(mail::audio_packets);
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto touch_port    = mail->event<input::touch_port_t>(mail::touch_port);


#ifdef _WIN32 
    platf::adjust_thread_priority(platf::thread_priority_e::critical);
#endif

    char sum = 0;
    uint64_t findex = 0;
    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      do {
        auto packet = audio_packets->pop();
        char* ptr = (char*)packet->second.begin();
        size_t size = packet->second.size();
        uint64_t utimestamp = std::chrono::steady_clock::now().time_since_epoch().count();

        auto updated = queue->inindex + 1;
        if (updated >= IN_QUEUE_SIZE)
          updated = 0;

        findex++;
        queue->incoming[updated].size = 0;
        copy_to_dpacket(&queue->incoming[updated],&findex,sizeof(uint64_t));
        copy_to_dpacket(&queue->incoming[updated],&utimestamp,sizeof(uint64_t));
        copy_to_dpacket(&queue->incoming[updated],&sum,sizeof(uint8_t));
        copy_to_dpacket(&queue->incoming[updated],ptr,size);
        queue->inindex = updated;
      } while (audio_packets->peek());
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);
  };

  auto touch_fun = [mail,process_shutdown_event](QueueMetadata* metadata){
    auto timer         = platf::create_high_precision_timer();
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto touch_port    = mail->event<input::touch_port_t>(mail::touch_port);

    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      if(!touch_port->peek()) {
        timer->sleep_for(100ms);
      } else {
        auto touch = touch_port->pop();
        if (touch.has_value()) {
          auto value = touch.value();
          metadata->client_offsetX = value.client_offsetX;
          metadata->client_offsetY = value.client_offsetY;
          metadata->offsetX = value.offset_x;
          metadata->offsetY = value.offset_y;
          metadata->env_height = value.env_height;
          metadata->env_width = value.env_width;
          metadata->height = value.height;
          metadata->width = value.width;
          metadata->scalar_inv = value.scalar_inv;
        }
      }
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);
  };


  {
    auto displays = platf::display_names(platf::mem_type_e::dxgi);
    for (int i = 0; i < displays.size(); i++)
    {
      auto codec = memory->video[i].metadata.codec;
      auto capture = std::thread{video_capture,mail,displays.at(i),codec};
      auto forward = std::thread{push_video,mail,&memory->video[i].internal};
      auto touch_thread = std::thread{touch_fun,&memory->video[i].metadata};
      auto receive = std::thread{pull,&memory->video[i].internal};
      receive.detach();
      touch_thread.detach();
      capture.detach();
      forward.detach();
    }
  } 

  {
    auto capture = std::thread{audio_capture,mail};
    auto forward = std::thread{push_audio,mail,&memory->audio};
    capture.detach();
    forward.detach();
  }

  auto timer         = platf::create_high_precision_timer();
  auto local_shutdown= mail->event<bool>(mail::shutdown);
  while (!process_shutdown_event->peek() && !local_shutdown->peek())
    timer->sleep_for(100ms);

  BOOST_LOG(info) << "Closed";
  // let other threads to close
  timer->sleep_for(1s);
  task_pool.stop();
  task_pool.join();

#ifdef _WIN32
  // Restore global NVIDIA control panel settings
  if (nvprefs_instance.owning_undo_file() && nvprefs_instance.load()) {
    nvprefs_instance.restore_global_profile();
    nvprefs_instance.unload();
  }
#endif

  return StatusCode::NORMAL_EXIT;
}

