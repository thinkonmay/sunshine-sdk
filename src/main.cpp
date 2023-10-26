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
#include "platform/common.h"
#include "rtsp.h"
#include "thread_pool.h"
#include "version.h"
#include "video.h"
#include "stream.h"

extern "C" {
#include <libavutil/log.h>
#include <rs.h>

#ifdef _WIN32
  #include <iphlpapi.h>
#endif
}

safe::mail_t mail::man;

namespace asio = boost::asio;
using asio::ip::udp;
using namespace std::literals;
namespace bl = boost::log;

#ifdef _WIN32
// Define global singleton used for NVIDIA control panel modifications
nvprefs::nvprefs_interface nvprefs_instance;
#endif

thread_pool_util::ThreadPool task_pool;
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

/**
 * @brief Print help to stdout.
 * @param name The name of the program.
 *
 * EXAMPLES:
 * ```cpp
 * print_help("sunshine");
 * ```
 */
void
print_help(const char *name) {
  std::cout
    << "Usage: "sv << name << " [options] [/path/to/configuration_file] [--cmd]"sv << std::endl
    << "    Any configurable option can be overwritten with: \"name=value\""sv << std::endl
    << std::endl
    << "    Note: The configuration will be created if it doesn't exist."sv << std::endl
    << std::endl
    << "    --help                    | print help"sv << std::endl
    << "    --creds username password | set user credentials for the Web manager"sv << std::endl
    << "    --version                 | print the version of sunshine"sv << std::endl
    << std::endl
    << "    flags"sv << std::endl
    << "        -0 | Read PIN from stdin"sv << std::endl
    << "        -1 | Do not load previously saved state and do retain any state after shutdown"sv << std::endl
    << "           | Effectively starting as if for the first time without overwriting any pairings with your devices"sv << std::endl
    << "        -2 | Force replacement of headers in video stream"sv << std::endl
    << "        -p | Enable/Disable UPnP"sv << std::endl
    << std::endl;
}

namespace help {
  int
  entry(const char *name, int argc, char *argv[]) {
    print_help(name);
    return 0;
  }
}  // namespace help

namespace version {
  int
  entry(const char *name, int argc, char *argv[]) {
    std::cout << PROJECT_NAME << " version: v" << PROJECT_VER << std::endl;
    return 0;
  }
}  // namespace version

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



/**
 * @brief Flush the log.
 *
 * EXAMPLES:
 * ```cpp
 * log_flush();
 * ```
 */
void
log_flush() {
  sink->flush();
}

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
 * 
*/
void
videoBroadcastThreadmain() {
  auto shutdown_event = mail::man->event<bool>(mail::broadcast_shutdown);
  auto packets = mail::man->queue<video::packet_t>(mail::video_packets);
  auto timebase = boost::posix_time::microsec_clock::universal_time();

  // Video traffic is sent on this thread
  platf::adjust_thread_priority(platf::thread_priority_e::high);


  // IMPORTANT
  while (auto packet = packets->pop()) {
    if (shutdown_event->peek()) {
      break;
    }

    BOOST_LOG(warning) << "Packet size: "sv << packet.get()->data_size();
    BOOST_LOG(warning) << "Packet val: "sv << packet.get()->data();
  }

  shutdown_event->raise(true);
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

  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale::global(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

  mail::man = std::make_shared<safe::mail_raw_t>();

  if (config::parse(argc, argv)) {
    return 0;
  }

  if (config::sunshine.min_log_level >= 1) {
    av_log_set_level(AV_LOG_QUIET);
  }
  else {
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
  auto fg = util::fail_guard(log_flush);

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

  // Wait as long as possible to terminate Sunshine.exe during logoff/shutdown
  SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);
#endif

  BOOST_LOG(info) << PROJECT_NAME << " version: " << PROJECT_VER << std::endl;
  task_pool.start(1);


  // Create signal handler after logging has been initialized
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [&force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      log_flush();
      std::abort();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_event->raise(true);
  });

  on_signal(SIGTERM, [&force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Terminate handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      log_flush();
      std::abort();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_event->raise(true);
  });


  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.

  auto deinit_guard = platf::init();
  if (!deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
  }

  BOOST_LOG(error) << "Hello from thinkmay."sv;
  reed_solomon_init();
  if (video::probe_encoders()) {
    BOOST_LOG(error) << "Video failed to find working encoder"sv;
  }




  auto video = std::thread { videoBroadcastThreadmain };

  stream::config_t config;
  config.monitor = { 1920, 1080, 60, 1000, 1, 0, 1, 0, 0 };
  auto gcm_key = util::from_hex<crypto::aes_t>(std::string("localhost"), true);
  auto iv      = util::from_hex<crypto::aes_t>(std::string("localhost"), true);
  auto session = stream::session::alloc(config, gcm_key, iv);
  auto capture = std::thread { stream::videoThread, &(*session) };



  shutdown_event->view();
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

/**
 * @brief Read a file to string.
 * @param path The path of the file.
 * @return `std::string` : The contents of the file.
 *
 * EXAMPLES:
 * ```cpp
 * std::string contents = read_file("path/to/file");
 * ```
 */
std::string
read_file(const char *path) {
  if (!std::filesystem::exists(path)) {
    BOOST_LOG(debug) << "Missing file: " << path;
    return {};
  }

  std::ifstream in(path);

  std::string input;
  std::string base64_cert;

  while (!in.eof()) {
    std::getline(in, input);
    base64_cert += input + '\n';
  }

  return base64_cert;
}

/**
 * @brief Writes a file.
 * @param path The path of the file.
 * @param contents The contents to write.
 * @return `int` : `0` on success, `-1` on failure.
 *
 * EXAMPLES:
 * ```cpp
 * int write_status = write_file("path/to/file", "file contents");
 * ```
 */
int
write_file(const char *path, const std::string_view &contents) {
  std::ofstream out(path);

  if (!out.is_open()) {
    return -1;
  }

  out << contents;

  return 0;
}

/**
 * @brief Map a specified port based on the base port.
 * @param port The port to map as a difference from the base port.
 * @return `std:uint16_t` : The mapped port number.
 *
 * EXAMPLES:
 * ```cpp
 * std::uint16_t mapped_port = map_port(1);
 * ```
 */
std::uint16_t
map_port(int port) {
  // calculate the port from the config port
  auto mapped_port = (std::uint16_t)((int) config::sunshine.port + port);

  // Ensure port is in the range of 1024-65535
  if (mapped_port < 1024 || mapped_port > 65535) {
    BOOST_LOG(warning) << "Port out of range: "sv << mapped_port;
  }

  // TODO: Ensure port is not already in use by another application

  return mapped_port;
}
