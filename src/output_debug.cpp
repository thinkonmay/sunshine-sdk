#include "output_debug.h"

#include "logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace std::chrono_literals;

namespace output_debug {
namespace {
constexpr size_t graph_width = 60;
constexpr int bar_width = 28;

const char *reset = "\x1b[0m";
const char *dim = "\x1b[2m";
const char *bold = "\x1b[1m";
const char *blue = "\x1b[38;5;39m";
const char *green = "\x1b[38;5;82m";
const char *yellow = "\x1b[38;5;220m";
const char *orange = "\x1b[38;5;208m";
const char *red = "\x1b[38;5;196m";
const char *cyan = "\x1b[38;5;51m";

template <class Value> double max_of(const std::vector<timing_t::sample_t> &history, Value value) {
  double result = 1;
  for (const auto &sample : history) {
    result = std::max(result, value(sample));
  }
  return result;
}

template <class Value>
std::string sparkline(const std::vector<timing_t::sample_t> &history, Value value, double max_value) {
  static constexpr std::array<char, 8> levels{'.', ':', '-', '=', '+', '*', '#', '@'};
  const auto begin = history.size() > graph_width ? history.end() - graph_width : history.begin();
  std::ostringstream out;
  for (auto it = begin; it != history.end(); ++it) {
    auto normalized = std::clamp(value(*it) / max_value, 0.0, 1.0);
    auto index = std::min<size_t>(levels.size() - 1, static_cast<size_t>(normalized * levels.size()));
    out << levels[index];
  }
  return out.str();
}

std::string bar(double value, double max_value, const char *color) {
  const auto filled = static_cast<int>(std::clamp(value / max_value, 0.0, 1.0) * bar_width);
  std::ostringstream out;
  out << color << std::string(filled, '=') << reset << dim << std::string(bar_width - filled, '-')
      << reset;
  return out.str();
}

std::string idr_markers(const std::vector<timing_t::sample_t> &history) {
  const auto begin = history.size() > graph_width ? history.end() - graph_width : history.begin();
  std::ostringstream out;
  for (auto it = begin; it != history.end(); ++it) {
    out << (it->last_idr ? '^' : ' ');
  }
  return out.str();
}

const char *jitter_color(const timing_t::sample_t &sample) {
  if (sample.avg_interval_ms <= 0) {
    return dim;
  }
  const auto ratio = sample.jitter_ms / sample.avg_interval_ms;
  if (sample.jitter_ms < 1.0 || ratio < 0.05) {
    return green;
  }
  if (ratio < 0.15) {
    return yellow;
  }
  if (ratio < 0.35) {
    return orange;
  }
  return red;
}

std::string jitter_insight(const timing_t::sample_t &sample) {
  if (sample.avg_interval_ms <= 0) {
    return "waiting for enough packet intervals to measure jitter";
  }

  const auto ratio = sample.jitter_ms / sample.avg_interval_ms;
  if (sample.jitter_ms < 1.0 || ratio < 0.05) {
    return "stable pacing: jitter is low relative to the average packet interval";
  }
  if (ratio < 0.15) {
    return "minor pacing variance: watch max-interval spikes if stutter appears";
  }
  if (ratio < 0.35) {
    return "noticeable jitter: cadence is uneven enough to correlate with frame pacing issues";
  }
  return "high jitter: output timing is unstable and can explain FPS drops or stutter";
}

std::string quality_label(const timing_t::sample_t &sample) {
  const auto color = jitter_color(sample);
  if (color == green) {
    return std::string{green} + "GOOD" + reset;
  }
  if (color == yellow) {
    return std::string{yellow} + "OK" + reset;
  }
  if (color == orange) {
    return std::string{orange} + "JITTERY" + reset;
  }
  if (color == red) {
    return std::string{red} + "BAD" + reset;
  }
  return std::string{dim} + "WARMING" + reset;
}

std::string clear_previous_render(size_t lines) {
  if (lines == 0) {
    return {};
  }

  std::ostringstream out;
  out << "\x1b[" << lines << "A";
  for (size_t i = 0; i < lines; ++i) {
    out << "\x1b[2K";
    if (i + 1 < lines) {
      out << "\x1b[1B";
    }
  }
  out << "\x1b[" << (lines - 1) << "A" << '\r';
  return out.str();
}

void metric_row(std::ostringstream &out, const char *name, double value, const char *unit,
                double max_value, const char *color, int precision = 2) {
  out << "  " << std::left << std::setw(15) << name << reset << std::right << std::setw(11)
      << std::fixed << std::setprecision(precision) << value << ' ' << std::setw(4) << std::left
      << unit << "  " << bar(value, max_value, color) << reset << '\n';
}
} // namespace

timing_t::timing_t(bool enabled)
    : enabled{enabled}, start_time{std::chrono::steady_clock::now()}, window_start{start_time},
      last_packet{start_time} {
  if (this->enabled) {
    BOOST_LOG(info) << "Output packet timing terminal graph enabled";
  }
}

void timing_t::record(int64_t frame_index, size_t packet_size, bool idr_frame, double encode_duration_us) {
  if (!enabled) {
    return;
  }

  auto now = std::chrono::steady_clock::now();
  auto interval_ms = std::chrono::duration<double, std::milli>(now - last_packet).count();
  last_packet = now;
  packets++;
  bytes += packet_size;
  encode_total_us += encode_duration_us;
  encode_max_us = std::max(encode_max_us, encode_duration_us);

  if (packets > 1) {
    interval_total_ms += interval_ms;
    interval_squared_total_ms += interval_ms * interval_ms;
    if (interval_min_ms == 0 || interval_ms < interval_min_ms) {
      interval_min_ms = interval_ms;
    }
    if (interval_ms > interval_max_ms) {
      interval_max_ms = interval_ms;
    }
  }

  auto elapsed = now - window_start;
  if (elapsed < 1s) {
    return;
  }

  auto elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
  auto interval_count = packets > 1 ? packets - 1 : 0;
  auto avg_interval_ms = interval_count > 0 ? interval_total_ms / interval_count : 0;
  auto jitter_ms = 0.0;
  if (interval_count > 0) {
    auto variance = interval_squared_total_ms / interval_count - avg_interval_ms * avg_interval_ms;
    jitter_ms = std::sqrt(std::max(0.0, variance));
  }
  sample_t sample{
      std::chrono::duration<double>(now - start_time).count(),
      packets * 1000.0 / elapsed_ms,
      avg_interval_ms,
      interval_min_ms,
      interval_max_ms,
      jitter_ms,
      packets > 0 ? encode_total_us / packets : 0,
      encode_max_us,
      packets,
      bytes,
      frame_index,
      packet_size,
      idr_frame,
  };

  history.push_back(sample);
  if (history.size() > 300) {
    history.erase(history.begin(), history.begin() + (history.size() - 300));
  }

  print_terminal(sample);

  window_start = now;
  packets = 0;
  bytes = 0;
  interval_total_ms = 0;
  interval_squared_total_ms = 0;
  interval_min_ms = 0;
  interval_max_ms = 0;
  encode_total_us = 0;
  encode_max_us = 0;
}

void timing_t::print_terminal(const sample_t &sample) {
  const auto fps_max = max_of(history, [](const auto &sample) { return sample.fps; });
  const auto avg_interval_max = max_of(history, [](const auto &sample) { return sample.avg_interval_ms; });
  const auto max_interval_max = max_of(history, [](const auto &sample) { return sample.max_interval_ms; });
  const auto jitter_max = max_of(history, [](const auto &sample) { return sample.jitter_ms; });
  const auto encode_max = max_of(history, [](const auto &sample) { return sample.max_encode_us; });
  const auto interval_scale = std::max({avg_interval_max, max_interval_max, jitter_max, 1.0});
  const auto interval_scale_us = interval_scale * 1000.0;

  std::ostringstream out;
  out << bold << cyan << "Sunshine output packet timing" << reset << dim
      << "  last " << std::min(history.size(), graph_width) << "s" << reset << "\n";
  out << dim << std::string(96, '-') << reset << "\n";
  out << std::fixed << std::setprecision(3);
  out << "  status " << quality_label(sample) << "   time " << sample.elapsed_seconds << "s"
      << "   packets " << sample.packets << "   bytes " << sample.bytes << "   last_frame "
      << sample.last_frame << "   last_size " << sample.last_size << "   IDR "
      << (sample.last_idr ? "yes" : "no") << "\n\n";

  metric_row(out, "FPS", sample.fps, "fps", fps_max, blue, 3);
  metric_row(out, "avg interval", sample.avg_interval_ms * 1000.0, "us", interval_scale_us, green, 1);
  metric_row(out, "max interval", sample.max_interval_ms * 1000.0, "us", interval_scale_us, orange, 1);
  metric_row(out, "jitter", sample.jitter_ms * 1000.0, "us", interval_scale_us, jitter_color(sample), 1);
  metric_row(out, "avg encode", sample.avg_encode_us, "us", encode_max, cyan, 1);
  metric_row(out, "max encode", sample.max_encode_us, "us", encode_max, yellow, 1);

  out << "\n" << bold << "timeline" << reset << dim << "  oldest -> newest" << reset << "\n";
  out << "  " << blue << "fps        " << reset
      << sparkline(history, [](const auto &sample) { return sample.fps; }, fps_max) << "  max "
      << fps_max << "\n";
  out << "  " << green << "avg us     " << reset
      << sparkline(history, [](const auto &sample) { return sample.avg_interval_ms; }, interval_scale)
      << "\n";
  out << "  " << orange << "max us     " << reset
      << sparkline(history, [](const auto &sample) { return sample.max_interval_ms; }, interval_scale)
      << "\n";
  out << "  " << jitter_color(sample) << "jitter us  " << reset
      << sparkline(history, [](const auto &sample) { return sample.jitter_ms; }, interval_scale)
      << "\n";
  out << "  " << cyan << "encode us  " << reset
      << sparkline(history, [](const auto &sample) { return sample.avg_encode_us; }, encode_max)
      << "\n";
  out << "  " << red << "IDR        " << reset << idr_markers(history) << "\n\n";

  out << bold << "jitter insight" << reset << "  " << jitter_color(sample) << jitter_insight(sample)
      << reset << "\n";
  out << dim << std::string(96, '-') << reset << "\n";

  auto render = out.str();
  const auto lines = std::count(render.begin(), render.end(), '\n');
  std::cout << clear_previous_render(last_render_lines) << render << std::flush;
  last_render_lines = lines;
}
} // namespace output_debug
