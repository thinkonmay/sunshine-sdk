/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <csignal>
#include <fstream>
#include <iostream>
#include <boost/interprocess/managed_shared_memory.hpp>

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

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace std::literals;
using namespace boost::interprocess;

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

  task_pool_util::TaskPool::task_id_t force_shutdown = nullptr;

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

  auto log_deinit_guard = logging::init(config::sunshine.min_log_level, config::sunshine.log_file);
  if (!log_deinit_guard) {
    BOOST_LOG(error) << "Logging failed to initialize"sv;
  }

  // logging can begin at this point
  // if anything is logged prior to this point, it will appear in stdout, but not in the log viewer in the UI
  // the version should be printed to the log before anything else
  BOOST_LOG(info) << PROJECT_NAME << " version: " << PROJECT_VER;


#ifdef WIN32
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

  task_pool.start(1);

  // Create signal handler after logging has been initialized
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [&force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      logging::log_flush();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_event->raise(true);
  });

  on_signal(SIGTERM, [&force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Terminate handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      logging::log_flush();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_event->raise(true);
  });

  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.

  auto platf_deinit_guard = platf::init();
  if (!platf_deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
  }

  auto input_deinit_guard = input::init();
  if (video::probe_encoders()) {
    BOOST_LOG(error) << "Video failed to find working encoder"sv;
  }

  //Get buffer local address from handle
  SharedMemory* memory = obtain_shared_memory(argv[1]);

  auto video_capture = std::thread{[&](){
    std::optional<std::string> display = std::string("123");
    if (argc > 2)
      display = std::string(argv[2]);

    video::capture(mail::man,video::config_t{
      display, 1920, 1080, 60, 6000, 1, 0, 1, 0, 0
    },NULL);
  }};
  auto audio_capture = std::thread{[&](){
    audio::capture(mail::man,audio::config_t{
      10,2,3,0
    },NULL);
  }};
    
  auto video_packets = mail::man->queue<video::packet_t>(mail::video_packets);
  auto audio_packets = mail::man->queue<audio::packet_t>(mail::audio_packets);
  auto bitrate       = mail::man->event<int>(mail::bitrate);
  auto framerate     = mail::man->event<int>(mail::framerate);
  auto idr           = mail::man->event<bool>(mail::idr);
  auto input         = input::alloc(mail::man);

  auto push = std::thread{[&](){
    while (!shutdown_event->peek()) {
      while(video_packets->peek()) {
        auto packet = video_packets->pop();
        push_packet(memory,packet->data(),packet->data_size(),Metadata{
          packet->is_idr(),QueueType::Video0
        });
      }
      while(audio_packets->peek()) {
        auto packet = audio_packets->pop();
        push_packet(memory,packet->second.begin(),packet->second.size(),Metadata{
          0,QueueType::Audio
        });
      }

      if(peek_event(memory,EventType::CHANGE_BITRATE))
        bitrate->raise(pop_event(memory,EventType::CHANGE_BITRATE).value_number);
      if(peek_event(memory,EventType::CHANGE_FRAMERATE)) 
        framerate->raise(pop_event(memory,EventType::CHANGE_FRAMERATE).value_number);
      if(peek_event(memory,EventType::POINTER_VISIBLE)) 
        display_cursor = pop_event(memory,EventType::POINTER_VISIBLE).value_number;
      if(peek_event(memory,EventType::IDR_FRAME)) 
        idr->raise(pop_event(memory,EventType::IDR_FRAME).value_number > 0);

      std::this_thread::sleep_for(1ms);
    }
  }};

  while (!shutdown_event->peek())
    std::this_thread::sleep_for(1s);

  push.join();
  audio_capture.join();
  video_capture.join();
  task_pool.stop();
  task_pool.join();

#ifdef WIN32
  // Restore global NVIDIA control panel settings
  if (nvprefs_instance.owning_undo_file() && nvprefs_instance.load()) {
    nvprefs_instance.restore_global_profile();
    nvprefs_instance.unload();
  }
#endif

  return 0;
}
