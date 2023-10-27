/**
 * @file src/config.cpp
 * @brief todo
 */
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "config.h"
#include "main.h"
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
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING 0
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY 1
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY 2
  #define AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM 3
  #define AMF_VIDEO_ENCODER_USAGE_TRANSCONDING 0
  #define AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY 1
  #define AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY 2
  #define AMF_VIDEO_ENCODER_USAGE_WEBCAM 3
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
      lowlatency = AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY,
      ultralowlatency = AMF_VIDEO_ENCODER_AV1_USAGE_ULTRA_LOW_LATENCY
    };

    enum class usage_hevc_e : int {
      transcoding = AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING,
      webcam = AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM,
      lowlatency = AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY,
      ultralowlatency = AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY
    };

    enum class usage_h264_e : int {
      transcoding = AMF_VIDEO_ENCODER_USAGE_TRANSCONDING,
      webcam = AMF_VIDEO_ENCODER_USAGE_WEBCAM,
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
    usage_from_view(const std::string_view &rc) {
#define _CONVERT_(x) \
  if (rc == #x##sv) return (int) T::x
      _CONVERT_(transcoding);
      _CONVERT_(webcam);
      _CONVERT_(lowlatency);
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

    1,  // min_threads
    {
      "superfast"s,  // preset
      "zerolatency"s,  // tune
      11,  // superfast
    },  // software

    {},  // nv
    true,  // nv_realtime_hags
    {},  // nv_legacy

    {
      qsv::medium,  // preset
      qsv::_auto,  // cavlc
    },  // qsv

    {
      (int) amd::quality_h264_e::balanced,  // quality (h264)
      (int) amd::quality_hevc_e::balanced,  // quality (hevc)
      (int) amd::quality_av1_e::balanced,  // quality (av1)
      (int) amd::rc_h264_e::vbr_latency,  // rate control (h264)
      (int) amd::rc_hevc_e::vbr_latency,  // rate control (hevc)
      (int) amd::rc_av1_e::vbr_latency,  // rate control (av1)
      (int) amd::usage_h264_e::ultralowlatency,  // usage (h264)
      (int) amd::usage_hevc_e::ultralowlatency,  // usage (hevc)
      (int) amd::usage_av1_e::ultralowlatency,  // usage (av1)
      0,  // preanalysis
      1,  // vbaq
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
    1  // channels
  };



  sunshine_t sunshine {
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

  bool
  endline(char ch) {
    return ch == '\r' || ch == '\n';
  }

  bool
  space_tab(char ch) {
    return ch == ' ' || ch == '\t';
  }

  bool
  whitespace(char ch) {
    return space_tab(ch) || endline(ch);
  }

  std::string
  to_string(const char *begin, const char *end) {
    std::string result;

    KITTY_WHILE_LOOP(auto pos = begin, pos != end, {
      auto comment = std::find(pos, end, '#');
      auto endl = std::find_if(comment, end, endline);

      result.append(pos, comment);

      pos = endl;
    })

    return result;
  }

  template <class It>
  It
  skip_list(It skipper, It end) {
    int stack = 1;
    while (skipper != end && stack) {
      if (*skipper == '[') {
        ++stack;
      }
      if (*skipper == ']') {
        --stack;
      }

      ++skipper;
    }

    return skipper;
  }

  std::pair<
    std::string_view::const_iterator,
    std::optional<std::pair<std::string, std::string>>>
  parse_option(std::string_view::const_iterator begin, std::string_view::const_iterator end) {
    begin = std::find_if_not(begin, end, whitespace);
    auto endl = std::find_if(begin, end, endline);
    auto endc = std::find(begin, endl, '#');
    endc = std::find_if(std::make_reverse_iterator(endc), std::make_reverse_iterator(begin), std::not_fn(whitespace)).base();

    auto eq = std::find(begin, endc, '=');
    if (eq == endc || eq == begin) {
      return std::make_pair(endl, std::nullopt);
    }

    auto end_name = std::find_if_not(std::make_reverse_iterator(eq), std::make_reverse_iterator(begin), space_tab).base();
    auto begin_val = std::find_if_not(eq + 1, endc, space_tab);

    if (begin_val == endl) {
      return std::make_pair(endl, std::nullopt);
    }

    // Lists might contain newlines
    if (*begin_val == '[') {
      endl = skip_list(begin_val + 1, end);
      if (endl == end) {
        std::cout << "Warning: Config option ["sv << to_string(begin, end_name) << "] Missing ']'"sv;

        return std::make_pair(endl, std::nullopt);
      }
    }

    return std::make_pair(
      endl,
      std::make_pair(to_string(begin, end_name), to_string(begin_val, endl)));
  }

  void
  string_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input) {
    auto it = vars.find(name);
    if (it == std::end(vars)) {
      return;
    }

    input = std::move(it->second);

    vars.erase(it);
  }

  template <typename T, typename F>
  void
  generic_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, T &input, F &&f) {
    std::string tmp;
    string_f(vars, name, tmp);
    if (!tmp.empty()) {
      input = f(tmp);
    }
  }

  void
  string_restricted_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input, const std::vector<std::string_view> &allowed_vals) {
    std::string temp;
    string_f(vars, name, temp);

    for (auto &allowed_val : allowed_vals) {
      if (temp == allowed_val) {
        input = std::move(temp);
        return;
      }
    }
  }

  void
  path_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, fs::path &input) {
    // appdata needs to be retrieved once only
    static auto appdata = platf::appdata();

    std::string temp;
    string_f(vars, name, temp);

    if (!temp.empty()) {
      input = temp;
    }

    if (input.is_relative()) {
      input = appdata / input;
    }

    auto dir = input;
    dir.remove_filename();

    // Ensure the directories exists
    if (!fs::exists(dir)) {
      fs::create_directories(dir);
    }
  }

  void
  path_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input) {
    fs::path temp = input;

    path_f(vars, name, temp);

    input = temp.string();
  }

  void
  int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input) {
    auto it = vars.find(name);

    if (it == std::end(vars)) {
      return;
    }

    std::string_view val = it->second;

    // If value is something like: "756" instead of 756
    if (val.size() >= 2 && val[0] == '"') {
      val = val.substr(1, val.size() - 2);
    }

    // If that integer is in hexadecimal
    if (val.size() >= 2 && val.substr(0, 2) == "0x"sv) {
      input = util::from_hex<int>(val.substr(2));
    }
    else {
      input = util::from_view(val);
    }

    vars.erase(it);
  }

  void
  int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::optional<int> &input) {
    auto it = vars.find(name);

    if (it == std::end(vars)) {
      return;
    }

    std::string_view val = it->second;

    // If value is something like: "756" instead of 756
    if (val.size() >= 2 && val[0] == '"') {
      val = val.substr(1, val.size() - 2);
    }

    // If that integer is in hexadecimal
    if (val.size() >= 2 && val.substr(0, 2) == "0x"sv) {
      input = util::from_hex<int>(val.substr(2));
    }
    else {
      input = util::from_view(val);
    }

    vars.erase(it);
  }

  template <class F>
  void
  int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input, F &&f) {
    std::string tmp;
    string_f(vars, name, tmp);
    if (!tmp.empty()) {
      input = f(tmp);
    }
  }

  template <class F>
  void
  int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::optional<int> &input, F &&f) {
    std::string tmp;
    string_f(vars, name, tmp);
    if (!tmp.empty()) {
      input = f(tmp);
    }
  }

  void
  int_between_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input, const std::pair<int, int> &range) {
    int temp = input;

    int_f(vars, name, temp);

    TUPLE_2D_REF(lower, upper, range);
    if (temp >= lower && temp <= upper) {
      input = temp;
    }
  }

  bool
  to_bool(std::string &boolean) {
    std::for_each(std::begin(boolean), std::end(boolean), [](char ch) { return (char) std::tolower(ch); });

    return boolean == "true"sv ||
           boolean == "yes"sv ||
           boolean == "enable"sv ||
           boolean == "enabled"sv ||
           boolean == "on"sv ||
           (std::find(std::begin(boolean), std::end(boolean), '1') != std::end(boolean));
  }

  void
  bool_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, bool &input) {
    std::string tmp;
    string_f(vars, name, tmp);

    if (tmp.empty()) {
      return;
    }

    input = to_bool(tmp);
  }

  void
  double_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, double &input) {
    std::string tmp;
    string_f(vars, name, tmp);

    if (tmp.empty()) {
      return;
    }

    char *c_str_p;
    auto val = std::strtod(tmp.c_str(), &c_str_p);

    if (c_str_p == tmp.c_str()) {
      return;
    }

    input = val;
  }

  void
  double_between_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, double &input, const std::pair<double, double> &range) {
    double temp = input;

    double_f(vars, name, temp);

    TUPLE_2D_REF(lower, upper, range);
    if (temp >= lower && temp <= upper) {
      input = temp;
    }
  }

  void
  list_string_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::vector<std::string> &input) {
    std::string string;
    string_f(vars, name, string);

    if (string.empty()) {
      return;
    }

    input.clear();

    auto begin = std::cbegin(string);
    if (*begin == '[') {
      ++begin;
    }

    begin = std::find_if_not(begin, std::cend(string), whitespace);
    if (begin == std::cend(string)) {
      return;
    }

    auto pos = begin;
    while (pos < std::cend(string)) {
      if (*pos == '[') {
        pos = skip_list(pos + 1, std::cend(string)) + 1;
      }
      else if (*pos == ']') {
        break;
      }
      else if (*pos == ',') {
        input.emplace_back(begin, pos);
        pos = begin = std::find_if_not(pos + 1, std::cend(string), whitespace);
      }
      else {
        ++pos;
      }
    }

    if (pos != begin) {
      input.emplace_back(begin, pos);
    }
  }

  void
  list_prep_cmd_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::vector<prep_cmd_t> &input) {
    std::string string;
    string_f(vars, name, string);

    std::stringstream jsonStream;

    // check if string is empty, i.e. when the value doesn't exist in the config file
    if (string.empty()) {
      return;
    }

    // We need to add a wrapping object to make it valid JSON, otherwise ptree cannot parse it.
    jsonStream << "{\"prep_cmd\":" << string << "}";

    boost::property_tree::ptree jsonTree;
    boost::property_tree::read_json(jsonStream, jsonTree);

    for (auto &[_, prep_cmd] : jsonTree.get_child("prep_cmd"s)) {
      auto do_cmd = prep_cmd.get_optional<std::string>("do"s);
      auto undo_cmd = prep_cmd.get_optional<std::string>("undo"s);
      auto elevated = prep_cmd.get_optional<bool>("elevated"s);

      input.emplace_back(do_cmd.value_or(""), undo_cmd.value_or(""), elevated.value_or(false));
    }
  }

  void
  list_int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::vector<int> &input) {
    std::vector<std::string> list;
    list_string_f(vars, name, list);

    for (auto &el : list) {
      std::string_view val = el;

      // If value is something like: "756" instead of 756
      if (val.size() >= 2 && val[0] == '"') {
        val = val.substr(1, val.size() - 2);
      }

      int tmp;

      // If the integer is a hexadecimal
      if (val.size() >= 2 && val.substr(0, 2) == "0x"sv) {
        tmp = util::from_hex<int>(val.substr(2));
      }
      else {
        tmp = util::from_view(val);
      }
      input.emplace_back(tmp);
    }
  }

  void
  map_int_int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::unordered_map<int, int> &input) {
    std::vector<int> list;
    list_int_f(vars, name, list);

    // The list needs to be a multiple of 2
    if (list.size() % 2) {
      std::cout << "Warning: expected "sv << name << " to have a multiple of two elements --> not "sv << list.size() << std::endl;
      return;
    }

    int x = 0;
    while (x < list.size()) {
      auto key = list[x++];
      auto val = list[x++];

      input.emplace(key, val);
    }
  }

  int
  apply_flags(const char *line) {
    int ret = 0;
    while (*line != '\0') {
      switch (*line) {
        case '0':
          config::sunshine.flags[config::flag::PIN_STDIN].flip();
          break;
        case '1':
          config::sunshine.flags[config::flag::FRESH_STATE].flip();
          break;
        case '2':
          config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE].flip();
          break;
        case 'p':
          config::sunshine.flags[config::flag::UPNP].flip();
          break;
        default:
          std::cout << "Warning: Unrecognized flag: ["sv << *line << ']' << std::endl;
          ret = -1;
      }

      ++line;
    }

    return ret;
  }

  void
  apply_config(std::unordered_map<std::string, std::string> &&vars) {
    if (!fs::exists(stream.file_apps.c_str())) {
      fs::copy_file(SUNSHINE_ASSETS_DIR "/apps.json", stream.file_apps);
    }

    for (auto &[name, val] : vars) {
      std::cout << "["sv << name << "] -- ["sv << val << ']' << std::endl;
    }

    int_f(vars, "qp", video.qp);
    int_f(vars, "min_threads", video.min_threads);
    int_between_f(vars, "hevc_mode", video.hevc_mode, { 0, 3 });
    int_between_f(vars, "av1_mode", video.av1_mode, { 0, 3 });
    string_f(vars, "sw_preset", video.sw.sw_preset);
    if (!video.sw.sw_preset.empty()) {
      video.sw.svtav1_preset = sw::svtav1_preset_from_view(video.sw.sw_preset);
    }
    string_f(vars, "sw_tune", video.sw.sw_tune);

    int_between_f(vars, "nvenc_preset", video.nv.quality_preset, { 1, 7 });
    generic_f(vars, "nvenc_twopass", video.nv.two_pass, nv::twopass_from_view);
    bool_f(vars, "nvenc_h264_cavlc", video.nv.h264_cavlc);
    bool_f(vars, "nvenc_realtime_hags", video.nv_realtime_hags);

#ifndef __APPLE__
    video.nv_legacy.preset = video.nv.quality_preset + 11;
    video.nv_legacy.multipass = video.nv.two_pass == nvenc::nvenc_two_pass::quarter_resolution ? NV_ENC_TWO_PASS_QUARTER_RESOLUTION :
                                video.nv.two_pass == nvenc::nvenc_two_pass::full_resolution    ? NV_ENC_TWO_PASS_FULL_RESOLUTION :
                                                                                                 NV_ENC_MULTI_PASS_DISABLED;
    video.nv_legacy.h264_coder = video.nv.h264_cavlc ? NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC : NV_ENC_H264_ENTROPY_CODING_MODE_CABAC;
#endif

    int_f(vars, "qsv_preset", video.qsv.qsv_preset, qsv::preset_from_view);
    int_f(vars, "qsv_coder", video.qsv.qsv_cavlc, qsv::coder_from_view);

    std::string quality;
    string_f(vars, "amd_quality", quality);
    if (!quality.empty()) {
      video.amd.amd_quality_h264 = amd::quality_from_view<amd::quality_h264_e>(quality);
      video.amd.amd_quality_hevc = amd::quality_from_view<amd::quality_hevc_e>(quality);
      video.amd.amd_quality_av1 = amd::quality_from_view<amd::quality_av1_e>(quality);
    }

    std::string rc;
    string_f(vars, "amd_rc", rc);
    int_f(vars, "amd_coder", video.amd.amd_coder, amd::coder_from_view);
    if (!rc.empty()) {
      video.amd.amd_rc_h264 = amd::rc_from_view<amd::rc_h264_e>(rc);
      video.amd.amd_rc_hevc = amd::rc_from_view<amd::rc_hevc_e>(rc);
      video.amd.amd_rc_av1 = amd::rc_from_view<amd::rc_av1_e>(rc);
    }

    std::string usage;
    string_f(vars, "amd_usage", usage);
    if (!usage.empty()) {
      video.amd.amd_usage_h264 = amd::usage_from_view<amd::usage_h264_e>(rc);
      video.amd.amd_usage_hevc = amd::usage_from_view<amd::usage_hevc_e>(rc);
      video.amd.amd_usage_av1 = amd::usage_from_view<amd::usage_av1_e>(rc);
    }

    bool_f(vars, "amd_preanalysis", (bool &) video.amd.amd_preanalysis);
    bool_f(vars, "amd_vbaq", (bool &) video.amd.amd_vbaq);

    int_f(vars, "vt_coder", video.vt.vt_coder, vt::coder_from_view);
    int_f(vars, "vt_software", video.vt.vt_allow_sw, vt::allow_software_from_view);
    int_f(vars, "vt_software", video.vt.vt_require_sw, vt::force_software_from_view);
    int_f(vars, "vt_realtime", video.vt.vt_realtime, vt::rt_from_view);

    string_f(vars, "capture", video.capture);
    string_f(vars, "encoder", video.encoder);
    string_f(vars, "adapter_name", video.adapter_name);
    string_f(vars, "output_name", video.output_name);

    path_f(vars, "log_path", config::sunshine.log_file);

    // Must be run after "file_state"
    path_f(vars, "credentials_file", config::sunshine.credentials_file);

    list_prep_cmd_f(vars, "global_prep_cmd", config::sunshine.prep_cmds);

    string_f(vars, "audio_sink", audio.sink);
    string_f(vars, "virtual_sink", audio.virtual_sink);
    bool_f(vars, "install_steam_audio_drivers", audio.install_steam_drivers);

    int to = -1;
    int_between_f(vars, "ping_timeout", to, { -1, std::numeric_limits<int>::max() });
    if (to != -1) {
      stream.ping_timeout = std::chrono::milliseconds(to);
    }

    int_between_f(vars, "channels", stream.channels, { 1, std::numeric_limits<int>::max() });

    path_f(vars, "file_apps", stream.file_apps);
    int_between_f(vars, "fec_percentage", stream.fec_percentage, { 1, 255 });


    // This config option will only be used by the UI
    // When editing in the config file itself, use "keybindings"
    bool map_rightalt_to_win = false;
    bool_f(vars, "key_rightalt_to_key_win", map_rightalt_to_win);


    int port = sunshine.port;
    sunshine.port = (std::uint16_t) port;

    string_restricted_f(vars, "address_family", sunshine.address_family, { "ipv4"sv, "both"sv });

    bool upnp = false;
    bool_f(vars, "upnp"s, upnp);

    if (upnp) {
      config::sunshine.flags[config::flag::UPNP].flip();
    }

    std::string log_level_string;
    string_f(vars, "min_log_level", log_level_string);

    if (!log_level_string.empty()) {
      if (log_level_string == "verbose"sv) {
        sunshine.min_log_level = 0;
      }
      else if (log_level_string == "debug"sv) {
        sunshine.min_log_level = 1;
      }
      else if (log_level_string == "info"sv) {
        sunshine.min_log_level = 2;
      }
      else if (log_level_string == "warning"sv) {
        sunshine.min_log_level = 3;
      }
      else if (log_level_string == "error"sv) {
        sunshine.min_log_level = 4;
      }
      else if (log_level_string == "fatal"sv) {
        sunshine.min_log_level = 5;
      }
      else if (log_level_string == "none"sv) {
        sunshine.min_log_level = 6;
      }
      else {
        // accept digit directly
        auto val = log_level_string[0];
        if (val >= '0' && val < '7') {
          sunshine.min_log_level = val - '0';
        }
      }
    }

    auto it = vars.find("flags"s);
    if (it != std::end(vars)) {
      apply_flags(it->second.c_str());

      vars.erase(it);
    }

    if (sunshine.min_log_level <= 3) {
      for (auto &[var, _] : vars) {
        std::cout << "Warning: Unrecognized configurable option ["sv << var << ']' << std::endl;
      }
    }
  }
}  // namespace config
