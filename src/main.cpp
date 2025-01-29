/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <codecvt>
#include <csignal>
#include <fstream>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>

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

using namespace std::literals;
using namespace boost::asio::ip;
using boost::asio::ip::udp;
using boost::asio::ip::address;

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



template<class FN>
class UDPClient {
public:
  UDPClient(FN &&handler) {
    bufhandler = std::forward<FN>(handler);
  }

  void Bind(udp::endpoint _remote_endpoint) {
    if(io_service != nullptr)
      io_service->stop();
    if (socket != nullptr)
      socket->close();

    io_service = new boost::asio::io_context();
    socket = new udp::socket(*io_service);
    socket->open(udp::v4());
    socket->bind(_remote_endpoint);
    remote_endpoint = remote_endpoint;
  }
  void Receiver() {
    wait();
    io_service->run();
  }

  uintptr_t GetHandle() {
    return (uintptr_t)socket->native_handle();
  }

private:
  std::function<void(std::string)> bufhandler;
  boost::asio::io_context* io_service = nullptr;
  udp::socket* socket = nullptr;
  boost::array<char, 16 * 1024> recv_buffer;
  udp::endpoint remote_endpoint;
  void handle_receive(const boost::system::error_code& err, size_t bytes_transferred) {
    if (!err) {
      char* data = recv_buffer.c_array();
      auto buff = std::string(data,data+bytes_transferred);
      bufhandler(buff);
      wait();
    }
  }

  void wait() {
    socket->async_receive_from(
      boost::asio::buffer(recv_buffer),
      remote_endpoint,
      boost::bind(
        &UDPClient::handle_receive, 
        this, 
        boost::asio::placeholders::error, 
        boost::asio::placeholders::bytes_transferred
      ));
  }
};

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

  auto log_deinit_guard = logging::init(config::sunshine.min_log_level);
  if (!log_deinit_guard) {
    BOOST_LOG(error) << "Logging failed to initialize"sv;
  }

  // logging can begin at this point
  // if anything is logged prior to this point, it will appear in stdout, but not in the log viewer in the UI
  // the version should be printed to the log before anything else
  BOOST_LOG(info) << PROJECT_NAME << " version: " << PROJECT_VER;


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

  task_pool.start(1);

  // Create signal handler after logging has been initialized
  auto process_shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [&force_shutdown, process_shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      logging::log_flush();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    process_shutdown_event->raise(true);
  });

  on_signal(SIGTERM, [&force_shutdown, process_shutdown_event]() {
    BOOST_LOG(info) << "Terminate handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      logging::log_flush();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    process_shutdown_event->raise(true);
  });

  // If any of the following fail, we log an error and continue event though sunshine will not function correctly.
  // This allows access to the UI to fix configuration problems or view the logs.

  auto platf_deinit_guard = platf::init();
  if (!platf_deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
  }


  int queuetype = QueueType::Video;
  std::stringstream ss0; ss0 << argv[1]; 
  std::string target; ss0 >> target;
  if (target == "audio")
    queuetype = QueueType::Audio;



  if(queuetype == QueueType::Video) {
    if (video::probe_encoders()) {
      BOOST_LOG(error) << "Video failed to find working encoder"sv;
      return StatusCode::NO_ENCODER_AVAILABLE;
    }
  }

  //Get buffer local address from handle
  SharedMemory* memory = 0;
  init_shared_memory(&memory);

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
    


  int port;
  std::string address;
  auto rendpoint = split(std::string(argv[3]),':');
  std::stringstream ss; ss <<   rendpoint[0]; ss >> address;
  std::stringstream ss2; ss2 << rendpoint[1]; ss2 >> port;
  udp::endpoint remote_endpoint = udp::endpoint(make_address(address), port);


  int lport;
  std::string laddress;
  auto lendpoint = split(std::string(argv[2]),':');
  std::stringstream ss3; ss3 << lendpoint[0]; ss3 >> laddress;
  std::stringstream ss4; ss4 << lendpoint[1]; ss4 >> lport;
  udp::endpoint local_endpoint = udp::endpoint(make_address(laddress), lport);

  auto mail          = std::make_shared<safe::mail_raw_t>();
  auto bitrate       = mail->event<int>(mail::bitrate);
  auto framerate     = mail->event<int>(mail::framerate);
  auto idr           = mail->event<bool>(mail::idr);
  auto client = new UDPClient([queuetype,bitrate,framerate,idr](std::string buffer){
    if (buffer.length() != 2) {
      BOOST_LOG(error) << "invalid message "<< buffer.length();
      return;
    } else if (queuetype == QueueType::Audio) {
      BOOST_LOG(error) << "audio buffer does not accept response";
      return;
    }
    
    switch (buffer.at(0)) {
    case EventType::Bitrate:
      BOOST_LOG(info) << "bitrate changed to " << (buffer.at(1) * 1000);
      bitrate->raise(buffer.at(1) * 1000);
      break;
    case EventType::Framerate:
      BOOST_LOG(info) << "framerate changed to " << (buffer.at(1));
      framerate->raise(buffer.at(1));
      break;
    case EventType::Pointer:
      BOOST_LOG(debug) << "pointer changed to " << (buffer.at(1) != 0);
      display_cursor = buffer.at(1) != 0;
      break;
    case EventType::Idr:
      BOOST_LOG(debug) << "IDR";
      idr->raise(true);
      break;
    default:
      BOOST_LOG(error) << "invalid message "<< u_int(buffer.at(0)) << " " << u_int(buffer.at(1));
      break;
    }
  });

  client->Bind(local_endpoint);
  std::thread recv([client,local_endpoint] { while(true) {
    client->Receiver(); 
    client->Bind(local_endpoint);
    std::this_thread::sleep_for(1s);
  }});

  auto push = [client,process_shutdown_event,remote_endpoint,local_endpoint](safe::mail_t mail, Queue* queue, QueueType queue_type){
    auto video_packets = mail->queue<video::packet_t>(mail::video_packets);
    auto audio_packets = mail->queue<audio::packet_t>(mail::audio_packets);
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto touch_port    = mail->event<input::touch_port_t>(mail::touch_port);


#ifdef _WIN32 
    platf::adjust_thread_priority(platf::thread_priority_e::critical);
#endif

    queue->metadata.active = 1;
    auto last_timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    bool first_video_packet = true;

    auto rAddr = remote_endpoint.address();
    auto rPort = remote_endpoint.port();
    auto lAddr = local_endpoint.address();
    auto lPort = local_endpoint.port();

    auto buffer = (char*)malloc(1024 * 1024);
    uint16_t index = 0;
    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      if (queue_type == QueueType::Video) {
        do {
          auto packet = video_packets->pop();
          if (packet->is_idr() != 0 && first_video_packet) {
            BOOST_LOG(info) << "idr frame";
            first_video_packet = false;
          }

          auto timestamp = packet->frame_timestamp.value().time_since_epoch().count();
          const char* ptr = (char*)packet->data();
          size_t size = packet->data_size();
          auto duration = uint32_t(timestamp - last_timestamp);;

          memcpy(buffer,&index,sizeof(uint16_t));
          memcpy(buffer + sizeof(uint16_t),&duration,sizeof(uint32_t));
          memcpy(buffer + sizeof(uint32_t) + sizeof(uint16_t),ptr,size);

          platf::batched_send_info_t send_info {
            buffer, size + sizeof(uint32_t) + sizeof(uint16_t), 1,
            client->GetHandle(), 
            rAddr, rPort, lAddr
          };

          platf::send_batch(send_info);
          last_timestamp = timestamp;
          index++;
        } while (video_packets->peek());
      } else if (queue_type == QueueType::Audio) {
        do {
          auto packet = audio_packets->pop();
          auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
          const char* ptr = (char*)packet->second.begin();
          size_t size = packet->second.size();
          auto duration = uint32_t(timestamp - last_timestamp);;

          memcpy(buffer,&index,sizeof(uint16_t));
          memcpy(buffer + sizeof(uint16_t),&duration,sizeof(uint32_t));
          memcpy(buffer + sizeof(uint32_t) + sizeof(uint16_t),ptr,size);

          platf::batched_send_info_t send_info {
            buffer, size + sizeof(uint32_t) + sizeof(uint16_t), 1,
            client->GetHandle(), 
            rAddr, rPort, lAddr
          };

          platf::send_batch(send_info);
          last_timestamp = timestamp;
          index++;
        } while (audio_packets->peek());
      }
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);

    queue->metadata.active = 0;
  };

  auto touch_fun = [mail,process_shutdown_event](Queue* queue){
    auto local_shutdown= mail->event<bool>(mail::shutdown);
    auto touch_port    = mail->event<input::touch_port_t>(mail::touch_port);

    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      if(touch_port->peek()) {
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
          BOOST_LOG(info) << "touch port event ";
          file_handler::write_file(
            "./metadata.bin",
            std::string_view((char*)&queue->metadata,sizeof(QueueMetadata))
          );
        }
      }
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);

    queue->metadata.active = 0;
  };


  auto queue = &memory->queues[queuetype];
  BOOST_LOG(info) << "Starting capture on channel " << queuetype;
  if (queuetype == QueueType::Video) {
    auto capture = std::thread{video_capture,mail,target,0};
    auto forward = std::thread{push,mail,queue,(QueueType)queuetype};
    auto touch_thread = std::thread{touch_fun,queue};
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
    std::this_thread::sleep_for(1s);

  BOOST_LOG(info) << "Closed" << queuetype;
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
