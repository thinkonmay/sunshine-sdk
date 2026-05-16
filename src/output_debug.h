#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace output_debug {
class timing_t {
public:
  struct sample_t {
    double elapsed_seconds;
    double fps;
    double avg_interval_ms;
    double min_interval_ms;
    double max_interval_ms;
    double jitter_ms;
    double avg_encode_us;
    double max_encode_us;
    uint64_t packets;
    uint64_t bytes;
    int64_t last_frame;
    size_t last_size;
    bool last_idr;
  };

  explicit timing_t(bool enabled);

  void record(int64_t frame_index, size_t packet_size, bool idr_frame, double encode_duration_us = 0);

private:
  void print_terminal(const sample_t &sample);

  bool enabled;
  std::chrono::steady_clock::time_point start_time;
  std::chrono::steady_clock::time_point window_start;
  std::chrono::steady_clock::time_point last_packet;
  uint64_t packets{};
  uint64_t bytes{};
  double interval_total_ms{};
  double interval_squared_total_ms{};
  double interval_min_ms{};
  double interval_max_ms{};
  double encode_total_us{};
  double encode_max_us{};
  size_t last_render_lines{};
  std::vector<sample_t> history;
};
} // namespace output_debug
