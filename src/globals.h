/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "thread_safe.h"

extern bool display_cursor;



namespace mail {
#define MAIL(x)                         \
  constexpr auto x = std::string_view { \
    #x                                  \
  }

  extern safe::mail_t man;

  // Global mail
  MAIL(shutdown);
  MAIL(video_packets);
  MAIL(audio_packets);
  MAIL(bitrate);
  MAIL(framerate);

  // Local mail
  MAIL(touch_port);
  MAIL(idr);
  MAIL(invalidate_ref_frames);
  MAIL(gamepad_feedback);
  MAIL(hdr);
#undef MAIL

}  // namespace mail
