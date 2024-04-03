/**
 * @file src/config.cpp
 * @brief todo
 */
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "config.h"
#include "file_handler.h"
#include "logging.h"
#include "utility.h"

#include "platform/common.h"

#ifdef _WIN32
  #include <shellapi.h>
#endif

#ifndef __APPLE__
  // For NVENC legacy constants
  #include <ffnvcodec/nvEncodeAPI.h>
#endif

namespace fs = std::filesystem;
using namespace std::literals;

#define CA_DIR "credentials"
#define PRIVATE_KEY_FILE CA_DIR "/cakey.pem"
#define CERTIFICATE_FILE CA_DIR "/cacert.pem"

#define APPS_JSON_PATH platf::appdata().string() + "/apps.json"
namespace config {

  namespace nv {

    nvenc::nvenc_two_pass
    twopass_from_view(const std::string_view &preset) {
      if (preset == "disabled") return nvenc::nvenc_two_pass::disabled;
      if (preset == "quarter_res") return nvenc::nvenc_two_pass::quarter_resolution;
      if (preset == "full_res") return nvenc::nvenc_two_pass::full_resolution;
      BOOST_LOG(warning) << "config: unknown nvenc_twopass value: " << preset;
      return nvenc::nvenc_two_pass::quarter_resolution;
    }

  }  // namespace nv

  namespace amd {
#ifdef __APPLE__
  // values accurate as of 27/12/2022, but aren't strictly necessary for MacOS build
  #define AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED 100
  #define AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY 30
  #define AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED 70
  #define AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED 10
  #define AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY 0
  #define AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED 5
  #define AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED 1
  #define AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY 2
  #define AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED 0
  #define AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP 0
  #define AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR 3
  #define AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR 2
  #define AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR 1
  #define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP 0
  #define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR 3
  #define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR 2
  #define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR 1
  #define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP 0
  #define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR 1
  #define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR 2
  #define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR 3
  #define AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING 0
  #define AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY 1
  #define AMF_VIDEO_ENCODER_AV1_USAGE_ULTRA_LOW_LATENCY 2
  #define AMF_VIDEO_ENCODER_AV1_USAGE_WEBCAM 3
  #define AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY_HIGH_QUALITY 5
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING 0
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY 1
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY 2
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM 3
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY_HIGH_QUALITY 5
  #define AMF_VIDEO_ENCODER_USAGE_TRANSCODING 0
  #define AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY 1
  #define AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY 2
  #define AMF_VIDEO_ENCODER_USAGE_WEBCAM 3
  #define AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY_HIGH_QUALITY 5
  #define AMF_VIDEO_ENCODER_UNDEFINED 0
  #define AMF_VIDEO_ENCODER_CABAC 1
  #define AMF_VIDEO_ENCODER_CALV 2
#else
  #include <AMF/components/VideoEncoderAV1.h>
  #include <AMF/components/VideoEncoderHEVC.h>
  #include <AMF/components/VideoEncoderVCE.h>
#endif

    enum class quality_av1_e : int {
      speed = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED,
      quality = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY,
      balanced = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED
    };

    enum class quality_hevc_e : int {
      speed = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED,
      quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY,
      balanced = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED
    };

    enum class quality_h264_e : int {
      speed = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED,
      quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY,
      balanced = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED
    };

    enum class rc_av1_e : int {
      cqp = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP,
      vbr_latency = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
      vbr_peak = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
      cbr = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR
    };

    enum class rc_hevc_e : int {
      cqp = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP,
      vbr_latency = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
      vbr_peak = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
      cbr = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR
    };

    enum class rc_h264_e : int {
      cqp = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP,
      vbr_latency = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
      vbr_peak = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
      cbr = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR
    };

    enum class usage_av1_e : int {
      transcoding = AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING,
      webcam = AMF_VIDEO_ENCODER_AV1_USAGE_WEBCAM,
      lowlatency_high_quality = AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY_HIGH_QUALITY,
      lowlatency = AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY,
      ultralowlatency = AMF_VIDEO_ENCODER_AV1_USAGE_ULTRA_LOW_LATENCY
    };

    enum class usage_hevc_e : int {
      transcoding = AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING,
      webcam = AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM,
      lowlatency_high_quality = AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY_HIGH_QUALITY,
      lowlatency = AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY,
      ultralowlatency = AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY
    };

    enum class usage_h264_e : int {
      transcoding = AMF_VIDEO_ENCODER_USAGE_TRANSCODING,
      webcam = AMF_VIDEO_ENCODER_USAGE_WEBCAM,
      lowlatency_high_quality = AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY_HIGH_QUALITY,
      lowlatency = AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY,
      ultralowlatency = AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY
    };

    enum coder_e : int {
      _auto = AMF_VIDEO_ENCODER_UNDEFINED,
      cabac = AMF_VIDEO_ENCODER_CABAC,
      cavlc = AMF_VIDEO_ENCODER_CALV
    };

    template <class T>
    std::optional<int>
    quality_from_view(const std::string_view &quality_type) {
#define _CONVERT_(x) \
  if (quality_type == #x##sv) return (int) T::x
      _CONVERT_(quality);
      _CONVERT_(speed);
      _CONVERT_(balanced);
#undef _CONVERT_
      return std::nullopt;
    }

    template <class T>
    std::optional<int>
    rc_from_view(const std::string_view &rc) {
#define _CONVERT_(x) \
  if (rc == #x##sv) return (int) T::x
      _CONVERT_(cqp);
      _CONVERT_(vbr_latency);
      _CONVERT_(vbr_peak);
      _CONVERT_(cbr);
#undef _CONVERT_
      return std::nullopt;
    }

    template <class T>
    std::optional<int>
    usage_from_view(const std::string_view &usage) {
#define _CONVERT_(x) \
  if (usage == #x##sv) return (int) T::x
      _CONVERT_(transcoding);
      _CONVERT_(webcam);
      _CONVERT_(lowlatency);
      _CONVERT_(lowlatency_high_quality);
      _CONVERT_(ultralowlatency);
#undef _CONVERT_
      return std::nullopt;
    }

    int
    coder_from_view(const std::string_view &coder) {
      if (coder == "auto"sv) return _auto;
      if (coder == "cabac"sv || coder == "ac"sv) return cabac;
      if (coder == "cavlc"sv || coder == "vlc"sv) return cavlc;

      return -1;
    }
  }  // namespace amd

  namespace qsv {
    enum preset_e : int {
      veryslow = 1,
      slower = 2,
      slow = 3,
      medium = 4,
      fast = 5,
      faster = 6,
      veryfast = 7
    };

    enum cavlc_e : int {
      _auto = false,
      enabled = true,
      disabled = false
    };

    std::optional<int>
    preset_from_view(const std::string_view &preset) {
#define _CONVERT_(x) \
  if (preset == #x##sv) return x
      _CONVERT_(veryslow);
      _CONVERT_(slower);
      _CONVERT_(slow);
      _CONVERT_(medium);
      _CONVERT_(fast);
      _CONVERT_(faster);
      _CONVERT_(veryfast);
#undef _CONVERT_
      return std::nullopt;
    }

    std::optional<int>
    coder_from_view(const std::string_view &coder) {
      if (coder == "auto"sv) return _auto;
      if (coder == "cabac"sv || coder == "ac"sv) return disabled;
      if (coder == "cavlc"sv || coder == "vlc"sv) return enabled;
      return std::nullopt;
    }

  }  // namespace qsv

  namespace vt {

    enum coder_e : int {
      _auto = 0,
      cabac,
      cavlc
    };

    int
    coder_from_view(const std::string_view &coder) {
      if (coder == "auto"sv) return _auto;
      if (coder == "cabac"sv || coder == "ac"sv) return cabac;
      if (coder == "cavlc"sv || coder == "vlc"sv) return cavlc;

      return -1;
    }

    int
    allow_software_from_view(const std::string_view &software) {
      if (software == "allowed"sv || software == "forced") return 1;

      return 0;
    }

    int
    force_software_from_view(const std::string_view &software) {
      if (software == "forced") return 1;

      return 0;
    }

    int
    rt_from_view(const std::string_view &rt) {
      if (rt == "disabled" || rt == "off" || rt == "0") return 0;

      return 1;
    }

  }  // namespace vt

  namespace sw {
    int
    svtav1_preset_from_view(const std::string_view &preset) {
#define _CONVERT_(x, y) \
  if (preset == #x##sv) return y
      _CONVERT_(veryslow, 1);
      _CONVERT_(slower, 2);
      _CONVERT_(slow, 4);
      _CONVERT_(medium, 5);
      _CONVERT_(fast, 7);
      _CONVERT_(faster, 9);
      _CONVERT_(veryfast, 10);
      _CONVERT_(superfast, 11);
      _CONVERT_(ultrafast, 12);
#undef _CONVERT_
      return 11;  // Default to superfast
    }
  }  // namespace sw

  video_t video {
    28,  // qp

    0,  // hevc_mode
    0,  // av1_mode

    2,  // min_threads
    {
      "superfast"s,  // preset
      "zerolatency"s,  // tune
      11,  // superfast
    },  // software

    {},  // nv
    true,  // nv_realtime_hags
    true,  // nv_opengl_vulkan_on_dxgi
    true,  // nv_sunshine_high_power_mode
    {},  // nv_legacy

    {
      qsv::medium,  // preset
      qsv::_auto,  // cavlc
      false,  // slow_hevc
    },  // qsv

    {
      (int) amd::quality_h264_e::balanced,  // quality (h264)
      (int) amd::quality_hevc_e::balanced,  // quality (hevc)
      (int) amd::quality_av1_e::balanced,  // quality (av1)
      (int) amd::rc_h264_e::cbr,  // rate control (h264)
      (int) amd::rc_hevc_e::cbr,  // rate control (hevc)
      (int) amd::rc_av1_e::cbr,  // rate control (av1)
      (int) amd::usage_h264_e::ultralowlatency,  // usage (h264)
      (int) amd::usage_hevc_e::ultralowlatency,  // usage (hevc)
      (int) amd::usage_av1_e::ultralowlatency,  // usage (av1)
      0,  // preanalysis
      1,  // vbaq
      1,  // enforce_hrd
      (int) amd::coder_e::_auto,  // coder
    },  // amd

    {
      0,
      0,
      1,
      -1,
    },  // vt

    {},  // capture
    {},  // encoder
    {},  // adapter_name
    {},  // output_name
  };

  audio_t audio {
    {},  // audio_sink
    {},  // virtual_sink
    true,  // install_steam_drivers
  };

  stream_t stream {
    10s,  // ping_timeout

    APPS_JSON_PATH,

    20,  // fecPercentage
    1,  // channels

    ENCRYPTION_MODE_NEVER,  // lan_encryption_mode
    ENCRYPTION_MODE_OPPORTUNISTIC,  // wan_encryption_mode
  };

  nvhttp_t nvhttp {
    "lan",  // origin web manager

    PRIVATE_KEY_FILE,
    CERTIFICATE_FILE,

    boost::asio::ip::host_name(),  // sunshine_name,
    "sunshine_state.json"s,  // file_state
    {},  // external_ip
    {
      "352x240"s,
      "480x360"s,
      "858x480"s,
      "1280x720"s,
      "1920x1080"s,
      "2560x1080"s,
      "2560x1440"s,
      "3440x1440"s,
      "1920x1200"s,
      "3840x2160"s,
      "3840x1600"s,
    },  // supported resolutions

    { 10, 30, 60, 90, 120 },  // supported fps
  };

  input_t input {
    {
      { 0x10, 0xA0 },
      { 0x11, 0xA2 },
      { 0x12, 0xA4 },
    },
    -1ms,  // back_button_timeout
    500ms,  // key_repeat_delay
    std::chrono::duration<double> { 1 / 24.9 },  // key_repeat_period

    {
      platf::supported_gamepads().front().data(),
      platf::supported_gamepads().front().size(),
    },  // Default gamepad
    true,  // back as touchpad click enabled (manual DS4 only)
    true,  // client gamepads with motion events are emulated as DS4
    true,  // client gamepads with touchpads are emulated as DS4

    true,  // keyboard enabled
    true,  // mouse enabled
    true,  // controller enabled
    true,  // always send scancodes
    true,  // high resolution scrolling
    true,  // native pen/touch support
  };

  sunshine_t sunshine {
    "en",  // locale
    2,  // min_log_level
    0,  // flags
    {},  // User file
    {},  // Username
    {},  // Password
    {},  // Password Salt
    platf::appdata().string() + "/sunshine.conf",  // config file
    {},  // cmd args
    47989,  // Base port number
    "ipv4",  // Address family
    platf::appdata().string() + "/sunshine.log",  // log file
    {},  // prep commands
  };
}  // namespace config
