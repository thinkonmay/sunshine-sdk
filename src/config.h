/**
 * @file src/config.h
 * @brief todo
 */
#pragma once

#include <bitset>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nvenc/nvenc_config.h"

namespace config {
  struct video_t {
    // ffmpeg params
    int qp;  // higher == more compression and less quality

    int hevc_mode;
    int av1_mode;

    int min_threads;  // Minimum number of threads/slices for CPU encoding
    struct {
      std::string sw_preset;
      std::string sw_tune;
      std::optional<int> svtav1_preset;
    } sw;

    nvenc::nvenc_config nv;
    bool nv_realtime_hags;
    bool nv_opengl_vulkan_on_dxgi;
    bool nv_sunshine_high_power_mode;

    struct {
      int preset;
      int multipass;
      int h264_coder;
      int aq;
      int vbv_percentage_increase;
    } nv_legacy;

    struct {
      std::optional<int> qsv_preset;
      std::optional<int> qsv_cavlc;
      bool qsv_slow_hevc;
    } qsv;

    struct {
      std::optional<int> amd_usage_h264;
      std::optional<int> amd_usage_hevc;
      std::optional<int> amd_usage_av1;
      std::optional<int> amd_rc_h264;
      std::optional<int> amd_rc_hevc;
      std::optional<int> amd_rc_av1;
      std::optional<int> amd_enforce_hrd;
      std::optional<int> amd_quality_h264;
      std::optional<int> amd_quality_hevc;
      std::optional<int> amd_quality_av1;
      std::optional<int> amd_preanalysis;
      std::optional<int> amd_vbaq;
      int amd_coder;
    } amd;

    struct {
      int vt_allow_sw;
      int vt_require_sw;
      int vt_realtime;
      int vt_coder;
    } vt;

    std::string capture;
    std::string encoder;
    std::string adapter_name;
    std::string output_name;
  };

  struct audio_t {
    std::string sink;
    std::string virtual_sink;
    bool install_steam_drivers;
  };

  constexpr int ENCRYPTION_MODE_NEVER = 0;  // Never use video encryption, even if the client supports it
  constexpr int ENCRYPTION_MODE_OPPORTUNISTIC = 1;  // Use video encryption if available, but stream without it if not supported
  constexpr int ENCRYPTION_MODE_MANDATORY = 2;  // Always use video encryption and refuse clients that can't encrypt

  namespace flag {
    enum flag_e : std::size_t {
      PIN_STDIN = 0,  // Read PIN from stdin instead of http
      FRESH_STATE,  // Do not load or save state
      FORCE_VIDEO_HEADER_REPLACE,  // force replacing headers inside video data
      UPNP,  // Try Universal Plug 'n Play
      CONST_PIN,  // Use "universal" pin
      FLAG_SIZE
    };
  }

  struct prep_cmd_t {
    prep_cmd_t(std::string &&do_cmd, std::string &&undo_cmd, bool &&elevated):
        do_cmd(std::move(do_cmd)), undo_cmd(std::move(undo_cmd)), elevated(std::move(elevated)) {}
    explicit prep_cmd_t(std::string &&do_cmd, bool &&elevated):
        do_cmd(std::move(do_cmd)), elevated(std::move(elevated)) {}
    std::string do_cmd;
    std::string undo_cmd;
    bool elevated;
  };
  struct sunshine_t {
    int min_log_level;
    std::bitset<flag::FLAG_SIZE> flags;
  };

  extern video_t video;
  extern audio_t audio;
  extern sunshine_t sunshine;
}  // namespace config
