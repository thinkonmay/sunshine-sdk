/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include "smemory.h"
#include <atomic>
#include <chrono>
#include <codecvt>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>

// local includes
#include "audio.h"
#include "config.h"
#include "globals.h"
#include "interprocess.h"
#include "logging.h"
#include "output_debug.h"
#include "platform/common.h"
#include "video.h"

#ifdef _WIN32
#include <Windows.h>
#endif

enum StatusCode { NORMAL_EXIT, NO_ENCODER_AVAILABLE };
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
  Resolution,
  Reset,
  VideoReset,
  AudioReset,
  InvalidateRefFrames,
  EventMax
};

using namespace std::literals;
using namespace std::chrono_literals;
using namespace std::chrono;

std::map<int, std::function<void()>> signal_handlers;
void on_signal_forwarder(int sig) {
  signal_handlers.at(sig)();
}

template <class FN> void on_signal(int sig, FN &&fn) {
  signal_handlers.emplace(sig, std::forward<FN>(fn));

  std::signal(sig, on_signal_forwarder);
}

std::vector<std::string> split(std::string s, char delim) {
  std::vector<std::string> result;
  std::stringstream ss(s);
  std::string item;

  while (getline(ss, item, delim)) {
    result.push_back(item);
  }

  return result;
}

std::vector<uint8_t> replace(const std::string_view &original, const std::string_view &old,
                             const std::string_view &_new) {
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

int main(int argc, char *argv[]) {
#ifdef _WIN32
  timeBeginPeriod(1);
  // Switch default C standard library locale to UTF-8 on Windows 10 1803+
  setlocale(LC_ALL, ".UTF-8");
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  // Use UTF-8 conversion for the default C++ locale (used by boost::log)
  std::locale::global(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
#pragma GCC diagnostic pop

  mail::man = std::make_shared<safe::mail_raw_t>();
  MediaMemory *memory = NULL;
  IVSHMEM *ivshmem = NULL;
  SharedMemory *shm = NULL;
  bool debug_output_timing = false;

  std::string ivshmem_path;
  std::string shm_name;
  std::string capture_display;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--force-sw"sv || arg == "-fsw"sv) {
      config::sunshine.flags.set(config::flag::FORCE_SOFTWARE_ENCODER);
    } else if (arg == "--force-hw"sv || arg == "-fhw"sv) {
      config::sunshine.flags.set(config::flag::FORCE_HARDWARE_ENCODER);
    } else if (arg == "--ivshmem"sv && i + 1 < argc) {
      ivshmem_path = argv[++i];
    } else if (arg == "--shm"sv && i + 1 < argc) {
      shm_name = argv[++i];
    } else if (arg == "--capture-display"sv && i + 1 < argc) {
      capture_display = argv[++i];
    }
  }

  auto log_deinit_guard = logging::init(config::sunshine.min_log_level);
  if (!log_deinit_guard) {
    BOOST_LOG(error) << "Logging failed to initialize"sv;
  }

  if (!ivshmem_path.empty()) {
    ivshmem = new IVSHMEM(ivshmem_path.c_str());
    if (ivshmem->Initialize() && ivshmem->GetSize() >= sizeof(MediaMemory)) {
      BOOST_LOG(info) << "Found ivshmem shared memory"sv;
      memory = (MediaMemory *)ivshmem->GetMemory();
    }
  } else if (!shm_name.empty()) {
    shm = new SharedMemory(shm_name.c_str(), sizeof(MediaMemory));
    if (shm->Initialize()) {
      BOOST_LOG(info) << "Found named shared memory: " << shm_name;
      memory = (MediaMemory *)shm->GetMemory();
    }
  }

  if (memory == NULL) {
    BOOST_LOG(info) << "IPC shared memory not available, using mockup memory block"sv;
    BOOST_LOG(info) << "Output packet timing debug mode enabled"sv;
    debug_output_timing = true;
    memory = (MediaMemory *)calloc(1, sizeof(MediaMemory));
  }

  // Create signal handler after logging has been initialized
  auto process_shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [process_shutdown_event, ivshmem, shm]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;
    logging::log_flush();
    process_shutdown_event->raise(true);
    if (ivshmem)
      ivshmem->DeInitialize();
    if (shm)
      shm->DeInitialize();
  });

  on_signal(SIGTERM, [process_shutdown_event, ivshmem, shm]() {
    BOOST_LOG(info) << "Terminate handler called"sv;
    logging::log_flush();
    process_shutdown_event->raise(true);
    if (ivshmem)
      ivshmem->DeInitialize();
    if (shm)
      shm->DeInitialize();
  });

  // Wait as long as possible to terminate Sunshine.exe during logoff/shutdown
  SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

  auto platf_deinit_guard = platf::init();

  if (!platf_deinit_guard) {
    BOOST_LOG(error) << "Platform failed to initialize"sv;
    return StatusCode::NO_ENCODER_AVAILABLE;
  }

  // Probe for a working encoder. On a freshly-booted GPU passthrough VM the
  // display can usually be captured before the GPU driver is ready, so
  // probe_encoders() may not find an (acceptable) encoder yet. We must NOT exit
  // in that case: the display is fine, we just have to wait for the encoder to
  // come up. Keep re-probing indefinitely until an acceptable encoder is found
  // (or shutdown is requested).
  //
  // The candidate encoder list is decided inside probe_encoders() based on the
  // flags below, so a successful probe (return 0) is always acceptable here:
  //  --force-sw : only the software encoder is probed.
  //  --force-hw : the software encoder is removed from the candidate list, so we
  //               wait specifically for a hardware encoder (NVENC/QuickSync/AMF).
  {
    constexpr auto encoder_probe_retry_interval = 2s;

    int attempt = 0;
    while (!process_shutdown_event->peek()) {
      if (video::probe_encoders() == 0) {
        break;
      }

      // The display may still be capturable; only the encoder is missing (e.g.
      // the GPU driver is still initializing). Keep waiting instead of exiting.
      BOOST_LOG(warning) << "No usable encoder found yet (attempt "sv << ++attempt
                         << "); waiting for an encoder to become available"sv;
      std::this_thread::sleep_for(encoder_probe_retry_interval);
    }

    if (process_shutdown_event->peek()) {
      BOOST_LOG(info) << "Shutdown requested while waiting for an encoder"sv;
      return StatusCode::NORMAL_EXIT;
    }
  }

  auto video_capture = [&](safe::mail_t mail, std::string displayin, int codec) {
    video::config_t config;
    config.display = displayin;
    config.width = 1920;
    config.height = 1080;
    config.framerate = 60;
    config.bitrate = 6000;
    config.slicesPerFrame = 1;
    config.numRefFrames = 0;
    config.encoderCscMode = 1;
    config.videoFormat = codec;
    config.dynamicRange = 0;
    config.chromaSamplingType = 0;
    config.enableIntraRefresh = 0;

    video::capture(mail, config, NULL);
  };

  auto audio_capture = [&](safe::mail_t mail) {
    audio::config_t config;
    config.packetDuration = 10;
    config.channels = 2;
    config.mask = 3;
    config.flags = 0;

    audio::capture(mail, config, NULL);
  };

  auto mail = std::make_shared<safe::mail_raw_t>();

  auto pull = [process_shutdown_event, mail, ivshmem](MediaQueue *queue) {
    auto timer = platf::create_high_precision_timer();
    auto local_shutdown = mail->event<bool>(mail::shutdown);
    auto bitrate = mail->event<int>(mail::bitrate);
    auto framerate = mail->event<int>(mail::framerate);
    auto idr = mail->event<bool>(mail::idr);
    auto resolution = mail->event<std::pair<int, int>>(mail::resolution);
    auto video_reset = mail->event<bool>(mail::video_reset);
    auto audio_reset = mail->event<bool>(mail::audio_reset);
    auto invalidate_ref_frames = mail->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames);

    HANDLE event = NULL;
    bool has_doorbell = false;
    if (ivshmem) {
      event = CreateEvent(NULL, FALSE, FALSE, NULL);
      if (event) {
        if (ivshmem->RegisterEvent(event, 0)) {
          BOOST_LOG(info) << "Successfully registered IVSHMEM doorbell event on vector 0";
          has_doorbell = true;
        } else {
          BOOST_LOG(warning) << "Failed to register IVSHMEM doorbell event, falling back to polling";
          CloseHandle(event);
          event = NULL;
        }
      }
    }

    int cached_bitrate = 6000; // kbps, matches default config.bitrate
    int new_framerate;
    auto expected_index = queue->outindex;
    char buffer[DATA_PACKET_SIZE] = {0};
    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      while (expected_index == queue->outindex) {
        if (has_doorbell) {
          WaitForSingleObject(event, 1000);
          if (process_shutdown_event->peek() || local_shutdown->peek()) {
            break;
          }
        } else {
          timer->sleep_for(1ms);
        }
      }
      if (expected_index == queue->outindex) {
        continue;
      }

      memcpy(buffer, queue->outgoing[expected_index].data, queue->outgoing[expected_index].size);

      expected_index++;
      if (expected_index >= OUT_QUEUE_SIZE)
        expected_index = 0;

      switch (buffer[0]) {
      case EventType::Bitrate: {
        if (buffer[1] == 0)
          break;

        int new_bitrate = buffer[1] * 1000;                   // kbps
        if (std::abs(new_bitrate - cached_bitrate) >= 1000) { // only change if delta >= 1 Mbps
          cached_bitrate = new_bitrate;
          bitrate->raise(new_bitrate);
        }
        break;
      }
      case EventType::Framerate:
        new_framerate = buffer[1] * 2;
        if (new_framerate < 20)
          break;

        framerate->raise(new_framerate);
        break;
      case EventType::Idr:
        idr->raise(true);
        break;
      case EventType::Pointer:
        display_cursor = buffer[1] != 0;
        break;
      case EventType::Resolution: {
        int new_width = (uint8_t)buffer[1] * 20;
        int new_height = (uint8_t)buffer[2] * 20;
        if (new_width > 0 && new_height > 0) {
          BOOST_LOG(info) << "Resolution change requested: " << new_width << "x" << new_height;
          resolution->raise(std::make_pair(new_width, new_height));
          idr->raise(true);
        }
        break;
      }
      case EventType::Reset:
        BOOST_LOG(info) << "Pipeline reset requested";
        process_shutdown_event->raise(true);
        break;
      case EventType::VideoReset:
        BOOST_LOG(info) << "Video pipeline reset requested";
        video_reset->raise(true);
        break;
      case EventType::AudioReset:
        BOOST_LOG(info) << "Audio pipeline reset requested";
        audio_reset->raise(true);
        break;
      case EventType::InvalidateRefFrames: {
        uint64_t first_frame = *(uint64_t*)&buffer[1];
        uint64_t last_frame = *(uint64_t*)&buffer[9];
        invalidate_ref_frames->raise(std::make_pair(first_frame, last_frame));
        break;
      }
      default:
        break;
      }
    }

    if (event) {
      CloseHandle(event);
    }
  };

  auto video_output_watchdog_ms = std::make_shared<std::atomic<int64_t>>(0);

  auto push_video = [process_shutdown_event, debug_output_timing, video_output_watchdog_ms, ivshmem,
                     memory](safe::mail_t mail, MediaQueue *queue, UINT16 doorbell_vector) {
    auto video_packets = mail->queue<video::packet_t>(mail::video_packets);
    auto audio_packets = mail->queue<audio::packet_t>(mail::audio_packets);
    auto local_shutdown = mail->event<bool>(mail::shutdown);

    platf::adjust_thread_priority(platf::thread_priority_e::critical);

    auto now_ms = []() {
      return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    };
    output_debug::timing_t output_timing{debug_output_timing};
    auto check_output_timeout = [&]() {
      auto last_output_packet = video_output_watchdog_ms->load();
      if (last_output_packet == 0 || now_ms() - last_output_packet < 10000) {
        return false;
      }

      BOOST_LOG(error) << "No video output packet written for 10 seconds; shutting down"sv;
      local_shutdown->raise(true);
      return true;
    };

    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      do {
        uint8_t flags = 0;
        auto packet = video_packets->pop(100ms);
        if (!packet) {
          check_output_timeout();
          break;
        }

        auto findex = packet->frame_index();
        std::string_view payload{(char *)packet->data(), packet->data_size()};
        std::vector<uint8_t> payload_with_replacements;
        uint64_t rtp_sample_duration = packet->rtp_sample_duration;

        if (packet->is_idr() && packet->replacements) {
          for (auto &replacement : *packet->replacements) {
            auto frame_old = replacement.old;
            auto frame_new = replacement._new;

            payload_with_replacements = replace(payload, frame_old, frame_new);
            payload = {(char *)payload_with_replacements.data(), payload_with_replacements.size()};
          }
        }

        if (packet->is_idr())
          flags |= (1 << 0);
        if (packet->after_ref_frame_invalidation)
          flags |= (1 << 1);

        constexpr auto header_size = sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint8_t);
        if (payload.size() > MEDIA_PACKET_SIZE - header_size) {
          BOOST_LOG(error) << "Dropping oversized video packet: " << payload.size()
                           << " bytes exceeds shared memory payload capacity "
                           << (MEDIA_PACKET_SIZE - header_size);
          if (check_output_timeout()) {
            break;
          }
          continue;
        }

        auto updated = queue->inindex + 1;
        if (updated >= IN_QUEUE_SIZE)
          updated = 0;

        queue->incoming[queue->inindex].size = 0;
        copy_to_packet(&queue->incoming[queue->inindex], &findex, sizeof(uint64_t));
        copy_to_packet(&queue->incoming[queue->inindex], &rtp_sample_duration, sizeof(uint64_t));
        copy_to_packet(&queue->incoming[queue->inindex], &flags, sizeof(uint8_t));
        copy_to_packet(&queue->incoming[queue->inindex], (void *)payload.data(), payload.size());
        queue->inindex = updated;
        if (ivshmem && memory->doorbell_peer_id > 0) {
          ivshmem->RingDoorbell((UINT16)memory->doorbell_peer_id, doorbell_vector);
        }
        video_output_watchdog_ms->store(now_ms());
        output_timing.record(findex, header_size + payload.size(), packet->is_idr(),
                             packet->encode_duration_us.value_or(0));
      } while (video_packets->peek());
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);
  };

  auto push_audio = [process_shutdown_event, ivshmem, memory](safe::mail_t mail, DataQueue *queue) {
    auto video_packets = mail->queue<video::packet_t>(mail::video_packets);
    auto audio_packets = mail->queue<audio::packet_t>(mail::audio_packets);
    auto local_shutdown = mail->event<bool>(mail::shutdown);

    platf::adjust_thread_priority(platf::thread_priority_e::high);

    char sum = 0;
    uint64_t findex = 0;
    while (!process_shutdown_event->peek() && !local_shutdown->peek()) {
      do {
        auto packet = audio_packets->pop();
        if (!packet)
          break;

        char *ptr = (char *)packet->data.begin();
        size_t size = packet->data.size();
        uint64_t rtp_sample_duration = packet->rtp_sample_duration;

        auto updated = queue->inindex + 1;
        if (updated >= IN_QUEUE_SIZE)
          updated = 0;

        findex++;
        queue->incoming[queue->inindex].size = 0;
        copy_to_dpacket(&queue->incoming[queue->inindex], &findex, sizeof(uint64_t));
        copy_to_dpacket(&queue->incoming[queue->inindex], &rtp_sample_duration, sizeof(uint64_t));
        copy_to_dpacket(&queue->incoming[queue->inindex], &sum, sizeof(uint8_t));
        copy_to_dpacket(&queue->incoming[queue->inindex], ptr, size);
        queue->inindex = updated;
        if (ivshmem && memory->doorbell_peer_id > 0) {
          ivshmem->RingDoorbell((UINT16)memory->doorbell_peer_id, MAX_DISPLAY + 1);
        }
      } while (audio_packets->peek());
    }

    if (!local_shutdown->peek())
      local_shutdown->raise(true);
  };

  {
    std::vector<std::string> displays;
    if (!capture_display.empty()) {
      BOOST_LOG(info) << "Pinned capture display: "sv << capture_display;
      displays.emplace_back(capture_display);
    } else {
      displays = platf::display_names(platf::mem_type_e::dxgi);
    }

    for (int i = 0; i < displays.size(); i++) {
      auto codec = memory->video[i].metadata.codec;
      auto capture = std::thread{video_capture, mail, displays.at(i), codec};
      auto forward = std::thread{push_video, mail, &memory->video[i].internal, (UINT16)(i + 1)};
      auto receive = std::thread{pull, &memory->video[i].internal};
      receive.detach();
      capture.detach();
      forward.detach();
    }
  }

  {
    auto capture = std::thread{audio_capture, mail};
    auto forward = std::thread{push_audio, mail, &memory->audio};
    capture.detach();
    forward.detach();
  }

  auto timer = platf::create_high_precision_timer();
  auto local_shutdown = mail->event<bool>(mail::shutdown);
  while (!process_shutdown_event->peek() && !local_shutdown->peek())
    timer->sleep_for(100ms);

  BOOST_LOG(info) << "Closed";
  timer->sleep_for(1s);

  return StatusCode::NORMAL_EXIT;
}
