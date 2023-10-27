/**
 * @file src/main.h
 * @brief Main header file for the Sunshine application.
 */

// macros
#pragma once

// standard includes
#include <filesystem>
#include <string_view>

// lib includes
#include <boost/log/common.hpp>

// local includes
#include "thread_safe.h"

#ifdef _WIN32
  // Declare global singleton used for NVIDIA control panel modifications
  #include "platform/windows/nvprefs/nvprefs_interface.h"
extern nvprefs::nvprefs_interface nvprefs_instance;
#endif

extern bool display_cursor;

extern boost::log::sources::severity_logger<int> verbose;
extern boost::log::sources::severity_logger<int> debug;
extern boost::log::sources::severity_logger<int> info;
extern boost::log::sources::severity_logger<int> warning;
extern boost::log::sources::severity_logger<int> error;
extern boost::log::sources::severity_logger<int> fatal;

// functions
int
main(int argc, char *argv[]);
void
log_flush();
std::uint16_t
map_port(int port);

// namespaces
namespace mail {
#define MAIL(x)                         \
  constexpr auto x = std::string_view { \
    #x                                  \
  }

  extern safe::mail_t man;

  // Global mail
  MAIL(shutdown);
  MAIL(broadcast_shutdown);
  MAIL(video_packets);
  MAIL(audio_packets);
  MAIL(switch_display);

  // Local mail
  MAIL(touch_port);
  MAIL(idr);
  MAIL(invalidate_ref_frames);
  MAIL(gamepad_feedback);
  MAIL(hdr);
#undef MAIL

}  // namespace mail