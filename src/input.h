/**
 * @file src/input.h
 * @brief todo
 */
#pragma once

#include <functional>

#include "platform/common.h"
#include "thread_safe.h"

namespace input {
  struct touch_port_t: public platf::touch_port_t {
    int env_width, env_height;

    // Offset x and y coordinates of the client
    float client_offsetX, client_offsetY;

    float scalar_inv;

    explicit
    operator bool() const {
      return width != 0 && height != 0 && env_width != 0 && env_height != 0;
    }
  };
}  // namespace input
