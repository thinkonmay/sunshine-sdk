/**
 * @file src/main.h
 * @brief Main header file for the Sunshine application.
 */
#ifndef __MAIN_H__
#define __MAIN_H__
#endif

// macros
#pragma once
#include <boost/log/common.hpp>

// standard includes
#include <assert.h> /* assert */
#include <stdio.h>  /* printf */

#include <bitset>
#include <climits>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string_view>

// local includes
#include "thread_safe.h"

extern boost::log::sources::severity_logger<int> verbose;  // Dominating output
extern boost::log::sources::severity_logger<int>
    debug;  // Follow what is happening
extern boost::log::sources::severity_logger<int>
    info;  // Should be informed about
extern boost::log::sources::severity_logger<int> warning;  // Strange events
extern boost::log::sources::severity_logger<int> error;    // Recoverable errors
extern boost::log::sources::severity_logger<int> fatal;  // Unrecoverable errors

// namespaces
namespace mail {
#define MAIL(x)                           \
    constexpr auto x = std::string_view { \
#x                                \
    }

// queue
MAIL(video_packets);
MAIL(audio_packets);

// event
MAIL(shutdown);
MAIL(switch_display);
MAIL(toggle_cursor);
MAIL(idr);
MAIL(bitrate);
MAIL(framerate);
MAIL(hdr);

#undef MAIL

}  // namespace mail