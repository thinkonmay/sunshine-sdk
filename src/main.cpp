/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <codecvt>
#include <csignal>
#include <fstream>
#include <iostream>

// local includes
#include "globals.h"
#include "interprocess.h"
#include "logging.h"
#include "main.h"
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
  NO_ENCODER_AVAILABLE = 77
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


void
copy_to_packet(Packet* packet,void* data, size_t size) {
  memcpy(packet->data+packet->size,data,size);
  packet->size += size;
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

  auto log_deinit_guard = logging::init(config::sunshine.min_log_level);
  if (!log_deinit_guard) {
    BOOST_LOG(error) << "Logging failed to initialize"sv;
  }



  task_pool.start(1);

  // Create signal handler after logging has been initialized
  auto process_shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [process_shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;
    logging::log_flush();
    process_shutdown_event->raise(true);
  });

  on_signal(SIGTERM, [process_shutdown_event]() {
    BOOST_LOG(info) << "Terminate handler called"sv;
    logging::log_flush();
    process_shutdown_event->raise(true);
  });

  int queuetype = QueueType::Video;
  std::stringstream ss0; ss0 << argv[1]; 
  std::string target; ss0 >> target;
  if (target == "audio")
    queuetype = QueueType::Audio;



  if(queuetype == QueueType::Video) {
#ifdef _WIN32
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
#endif
  }

  auto platf_deinit_guard = platf::init();
  auto queue = init_shared_memory(argv[2]);
  memset(queue,0,sizeof(Queue));
  if (!platf_deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
    return StatusCode::NO_ENCODER_AVAILABLE;
  } else if(queuetype == QueueType::Video && video::probe_encoders()) {
    BOOST_LOG(error) << "Video failed to find working encoder"sv;
    return StatusCode::NO_ENCODER_AVAILABLE;
  } else if (queue == nullptr) {
    BOOST_LOG(error) << "Failed to find shared memory"sv;
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

  auto pull = [process_shutdown_event,queue,mail](){
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto bitrate       = mail->event<int>(mail::bitrate);
    auto framerate     = mail->event<int>(mail::framerate);
    auto idr           = mail->event<bool>(mail::idr);

    auto expected_index = 0;
    auto last_bitrate = 6;
    char buffer[512] = {0};
    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      while (expected_index == queue->outindex)
        std::this_thread::sleep_for(100us);

      memcpy(buffer,
        queue->outcoming[expected_index].data,
        queue->outcoming[expected_index].size
      );

      expected_index++;
      if (expected_index >= QUEUE_SIZE)
        expected_index = 0;
      
      switch (buffer[0]) {
      case EventType::Bitrate:
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
        BOOST_LOG(info) << "framerate changed to " << (int)(buffer[1]);
        framerate->raise(buffer[1]);
        break;
      case EventType::Pointer:
        BOOST_LOG(debug) << "pointer changed to " << (bool)(buffer[1] != 0);
        display_cursor = buffer[1] != 0;
        break;
      case EventType::Idr:
        BOOST_LOG(debug) << "IDR";
        idr->raise(true);
        break;
      default:
        BOOST_LOG(error) << "invalid message "<< u_int(buffer[0]) << " " << u_int(buffer[1]);
        break;
      }
    }
  };


  auto push = [process_shutdown_event](safe::mail_t mail, Queue* queue, QueueType queue_type){
    auto video_packets = mail->queue<video::packet_t>(mail::video_packets);
    auto audio_packets = mail->queue<audio::packet_t>(mail::audio_packets);
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto touch_port    = mail->event<input::touch_port_t>(mail::touch_port);


#ifdef _WIN32 
    platf::adjust_thread_priority(platf::thread_priority_e::critical);
#endif

    uint32_t findex = -1;
    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      if (queue_type == QueueType::Video) {
        do {
          auto packet = video_packets->pop();
          char* ptr = (char*)packet->data();
          size_t size = packet->data_size();
          uint64_t utimestamp = packet->frame_timestamp.value().time_since_epoch().count();

          auto updated = queue->inindex + 1;
          if (updated >= QUEUE_SIZE)
            updated = 0;

          findex++;
          queue->incoming[updated].size = 0;
          copy_to_packet(&queue->incoming[updated],&findex,sizeof(uint32_t));
          copy_to_packet(&queue->incoming[updated],&utimestamp,sizeof(uint64_t));
          copy_to_packet(&queue->incoming[updated],ptr,size);
          queue->inindex = updated;
        } while (video_packets->peek());
      } else if (queue_type == QueueType::Audio) {
        do {
          auto packet = audio_packets->pop();
          char* ptr = (char*)packet->second.begin();
          size_t size = packet->second.size();
          uint64_t utimestamp = std::chrono::steady_clock::now().time_since_epoch().count();

          auto updated = queue->inindex + 1;
          if (updated >= QUEUE_SIZE)
            updated = 0;

          findex++;
          queue->incoming[updated].size = 0;
          copy_to_packet(&queue->incoming[updated],&findex,sizeof(uint32_t));
          copy_to_packet(&queue->incoming[updated],&utimestamp,sizeof(uint64_t));
          copy_to_packet(&queue->incoming[updated],ptr,size);
          queue->inindex = updated;
        } while (audio_packets->peek());
      }
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);
  };

  auto touch_fun = [mail,process_shutdown_event](Queue* queue){
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto touch_port    = mail->event<input::touch_port_t>(mail::touch_port);

    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      if(!touch_port->peek()) {
        std::this_thread::sleep_for(100ms);
      } else {
        auto touch = touch_port->pop();
        if (touch.has_value()) {
          auto value = touch.value();
          queue->metadata.client_offsetX = value.client_offsetX;
          queue->metadata.client_offsetY = value.client_offsetY;
          queue->metadata.offsetX= value.offset_x;
          queue->metadata.offsetY = value.offset_y;
          queue->metadata.env_height = value.env_height;
          queue->metadata.env_width = value.env_width;
          queue->metadata.height = value.height;
          queue->metadata.width = value.width;
          queue->metadata.scalar_inv = value.scalar_inv;
        }
      }
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);
  };


  BOOST_LOG(info) << "Starting capture on channel " << queuetype;
  if (queuetype == QueueType::Video) {
    auto capture = std::thread{video_capture,mail,target,0};
    auto forward = std::thread{push,mail,queue,(QueueType)queuetype};
    auto touch_thread = std::thread{touch_fun,queue};
    auto receive = std::thread{pull};
    receive.detach();
    touch_thread.detach();
    capture.detach();
    forward.detach();
  } else if (queuetype == QueueType::Audio) {
    auto capture = std::thread{audio_capture,mail};
    auto forward = std::thread{push,mail,queue,(QueueType)queuetype};
    capture.detach();
    forward.detach();
  }

  auto local_shutdown= mail->event<bool>(mail::shutdown);
  while (!process_shutdown_event->peek() && !local_shutdown->peek())
    std::this_thread::sleep_for(100ms);

  BOOST_LOG(info) << "Closed";
  // let other threads to close
  std::this_thread::sleep_for(1s);
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
