/**
 * @file src/main.h
 * @brief Main header file for the Sunshine application.
 */

// macros
#pragma once

// standard includes
#include <filesystem>
#include <string_view>
#include <bitset>
#include <climits>
#include <cstring>
#include <iostream>
#include <stdio.h>      /* printf */
#include <assert.h>     /* assert */

// local includes
#include "thread_safe.h"



// namespaces
namespace mail {
#define MAIL(x) constexpr auto x = std::string_view { #x }

  //queue
  MAIL(video_packets);

  //event
  MAIL(shutdown);
  MAIL(switch_display);
  MAIL(toggle_cursor);
  MAIL(idr);
  MAIL(hdr);
  MAIL(invalidate_ref_frames);

#undef MAIL

}  // namespace mail