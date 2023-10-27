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

// lib includes
#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>

// local includes
#include "config.h"
#include "main.h"
#include "dll.h"
#include "platform/common.h"
#include "video.h"

extern "C" {
#include <libavutil/log.h>
#ifdef _WIN32
  #include <iphlpapi.h>
  #include "platform/windows/nvprefs/nvprefs_interface.h"
#endif
}

safe::mail_t mail::man;

using namespace std::literals;
namespace bl = boost::log;

#ifdef _WIN32
// Define global singleton used for NVIDIA control panel modifications
nvprefs::nvprefs_interface nvprefs_instance;
#endif

bl::sources::severity_logger<int> verbose(0);  // Dominating output
bl::sources::severity_logger<int> debug(1);  // Follow what is happening
bl::sources::severity_logger<int> info(2);  // Should be informed about
bl::sources::severity_logger<int> warning(3);  // Strange events
bl::sources::severity_logger<int> error(4);  // Recoverable errors
bl::sources::severity_logger<int> fatal(5);  // Unrecoverable errors

bool display_cursor = true;

using text_sink = bl::sinks::asynchronous_sink<bl::sinks::text_ostream_backend>;
boost::shared_ptr<text_sink> sink;

struct NoDelete {
  void
  operator()(void *) {}
};

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)


#ifdef _WIN32
namespace restore_nvprefs_undo {
  int
  entry(const char *name, int argc, char *argv[]) {
    // Restore global NVIDIA control panel settings to the undo file
    // left by improper termination of sunshine.exe, if it exists.
    // This entry point is typically called by the uninstaller.
    if (nvprefs_instance.load()) {
      nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
      nvprefs_instance.unload();
    }
    return 0;
  }
}  // namespace restore_nvprefs_undo
#endif



void
log_flush() {
}



DeInit(){
#ifdef WIN32
  // Restore global NVIDIA control panel settings
  if (nvprefs_instance.owning_undo_file() && nvprefs_instance.load()) {
    nvprefs_instance.restore_global_profile();
    nvprefs_instance.unload();
  }
#endif
}

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



#ifdef _WIN32
  // Switch default C standard library locale to UTF-8 on Windows 10 1803+
  setlocale(LC_ALL, ".UTF-8");
#endif

  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale::global(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
  mail::man = std::make_shared<safe::mail_raw_t>();


  if (config::sunshine.min_log_level >= 1) {
    av_log_set_level(AV_LOG_QUIET);
  } else {
    av_log_set_level(AV_LOG_DEBUG);
  }

  av_log_set_callback([](void *ptr, int level, const char *fmt, va_list vl) {
    static int print_prefix = 1;
    char buffer[1024];

    av_log_format_line(ptr, level, fmt, vl, buffer, sizeof(buffer), &print_prefix);
    if (level <= AV_LOG_FATAL) {
      BOOST_LOG(fatal) << buffer;
    }
    else if (level <= AV_LOG_ERROR) {
      BOOST_LOG(error) << buffer;
    }
    else if (level <= AV_LOG_WARNING) {
      BOOST_LOG(warning) << buffer;
    }
    else if (level <= AV_LOG_INFO) {
      BOOST_LOG(info) << buffer;
    }
    else if (level <= AV_LOG_VERBOSE) {
      // AV_LOG_VERBOSE is less verbose than AV_LOG_DEBUG
      BOOST_LOG(debug) << buffer;
    }
    else {
      BOOST_LOG(verbose) << buffer;
    }
  });

  sink = boost::make_shared<text_sink>();

  boost::shared_ptr<std::ostream> stream { &std::cout, NoDelete {} };
  sink->locked_backend()->add_stream(stream);
  sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(config::sunshine.log_file));
  sink->set_filter(severity >= config::sunshine.min_log_level);

  sink->set_formatter([message = "Message"s, severity = "Severity"s](const bl::record_view &view, bl::formatting_ostream &os) {
    constexpr int DATE_BUFFER_SIZE = 21 + 2 + 1;  // Full string plus ": \0"

    auto log_level = view.attribute_values()[severity].extract<int>().get();

    std::string_view log_type;
    switch (log_level) {
      case 0:
        log_type = "Verbose: "sv;
        break;
      case 1:
        log_type = "Debug: "sv;
        break;
      case 2:
        log_type = "Info: "sv;
        break;
      case 3:
        log_type = "Warning: "sv;
        break;
      case 4:
        log_type = "Error: "sv;
        break;
      case 5:
        log_type = "Fatal: "sv;
        break;
    };

    char _date[DATE_BUFFER_SIZE];
    std::time_t t = std::time(nullptr);
    strftime(_date, DATE_BUFFER_SIZE, "[%Y:%m:%d:%H:%M:%S]: ", std::localtime(&t));

    os << _date << log_type << view.attribute_values()[message].extract<std::string>();
  });

  // Flush after each log record to ensure log file contents on disk isn't stale.
  // This is particularly important when running from a Windows service.
  sink->locked_backend()->auto_flush(true);

  bl::core::get()->add_sink(sink);
  auto fg = util::fail_guard([&sink](){ sink->flush(); });

#ifdef WIN32
  // Modify relevant NVIDIA control panel settings if the system has corresponding gpu
  if (nvprefs_instance.load()) {
    // Restore global settings to the undo file left by improper termination of sunshine.exe
    nvprefs_instance.restore_from_and_delete_undo_file_if_exists();
    // Modify application settings for sunshine.exe
    nvprefs_instance.modify_application_profile();
    // Modify global settings, undo file is produced in the process to restore after improper termination
    nvprefs_instance.modify_global_profile();
    // Unload dynamic library to survive driver reinstallation
    nvprefs_instance.unload();
  }
#endif




  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.
  auto deinit_guard = platf::init();
  if (!deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
    return 1;
  } else if (video::probe_encoders()) {
    BOOST_LOG(error) << "Video failed to find working encoder"sv;
    return 1;
  } else {
    BOOST_LOG(error) << "Hello from thinkmay."sv;
    return 0;
  }

  // auto mic = control->microphone(stream->mapping, stream->channelCount, stream->sampleRate, frame_size);

  // Create signal handler after logging has been initialized
  mail::man->event<bool>(mail::shutdown)->view();
  DeInit();
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