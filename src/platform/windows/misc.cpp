/**
 * @file src/platform/windows/misc.cpp
 * @brief todo
 */
#include <memory>
#include <string>

// prevent clang format from "optimizing" the header include order
// clang-format off
#include <dwmapi.h>
#include <timeapi.h>
#include <userenv.h>
#include <winsock2.h>
#include <windows.h>
#include <winuser.h>
#include <wlanapi.h>
#include <ws2tcpip.h>
#include <wtsapi32.h>
#include <sddl.h>
// clang-format on

// Boost overrides NTDDI_VERSION, so we re-override it here
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN10
#include <Shlwapi.h>

#include "misc.h"

#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

#include <qos2.h>

#ifndef WLAN_API_MAKE_VERSION
#define WLAN_API_MAKE_VERSION(_major, _minor) (((DWORD)(_minor)) << 16 | (_major))
#endif

using namespace std::literals;
namespace platf {

HDESK
syncThreadDesktop() {
  HWINSTA hWinSta0 = OpenWindowStation(TEXT("WinSta0"), FALSE, MAXIMUM_ALLOWED);
  if (NULL == hWinSta0) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to OpenWindowStation [0x"sv << util::hex(err).to_string_view()
                     << ']';
  }

  if (!SetProcessWindowStation(hWinSta0)) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to SetProcessWindowStation [0x"sv << util::hex(err).to_string_view()
                     << ']';
  }

  HDESK hDesk = OpenDesktop(TEXT("default"), DF_ALLOWOTHERACCOUNTHOOK, FALSE, MAXIMUM_ALLOWED);
  if (NULL == hDesk) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to OpenDesktop [0x"sv << util::hex(err).to_string_view() << ']';
  }

  if (!SwitchDesktop(hDesk)) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to SwitchDesktop [0x"sv << util::hex(err).to_string_view() << ']';
  }

  if (!SetThreadDesktop(hDesk)) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to SetThreadDesktop [0x"sv << util::hex(err).to_string_view()
                     << ']';
  }

  if (hDesk != NULL) {
    CloseDesktop(hDesk);
  }
  if (hWinSta0 != NULL) {
    CloseWindowStation(hWinSta0);
  }
  BOOST_LOG(debug) << "Thread sync with default desktop"sv;
  return hDesk;
}

void adjust_thread_priority(thread_priority_e priority) {
  int win32_priority;

  switch (priority) {
  case thread_priority_e::low:
    win32_priority = THREAD_PRIORITY_BELOW_NORMAL;
    break;
  case thread_priority_e::normal:
    win32_priority = THREAD_PRIORITY_NORMAL;
    break;
  case thread_priority_e::high:
    win32_priority = THREAD_PRIORITY_ABOVE_NORMAL;
    break;
  case thread_priority_e::critical:
    win32_priority = THREAD_PRIORITY_HIGHEST;
    break;
  default:
    BOOST_LOG(error) << "Unknown thread priority: "sv << (int)priority;
    return;
  }

  if (!SetThreadPriority(GetCurrentThread(), win32_priority)) {
    auto winerr = GetLastError();
    BOOST_LOG(warning) << "Unable to set thread priority to "sv << win32_priority << ": "sv
                       << winerr;
  }
}

int64_t qpc_counter() {
  LARGE_INTEGER performace_counter;
  if (QueryPerformanceCounter(&performace_counter))
    return performace_counter.QuadPart;
  return 0;
}

std::chrono::nanoseconds qpc_time_difference(int64_t performance_counter1,
                                             int64_t performance_counter2) {
  auto get_frequency = []() {
    LARGE_INTEGER frequency;
    frequency.QuadPart = 0;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
  };
  static const double frequency = get_frequency();
  if (frequency) {
    return std::chrono::nanoseconds(
        (int64_t)((performance_counter1 - performance_counter2) * frequency / std::nano::den));
  }
  return {};
}

/**
 * @brief Converts a UTF-8 string into a UTF-16 wide string.
 * @param string The UTF-8 string.
 * @return The converted UTF-16 wide string.
 */
std::wstring from_utf8(const std::string &string) {
  // No conversion needed if the string is empty
  if (string.empty()) {
    return {};
  }

  // Get the output size required to store the string
  auto output_size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string.data(), string.size(), nullptr, 0);
  if (output_size == 0) {
    auto winerr = GetLastError();
    BOOST_LOG(error) << "Failed to get UTF-16 buffer size: "sv << winerr;
    return {};
  }

  // Perform the conversion
  std::wstring output(output_size, L'\0');
  output_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string.data(), string.size(),
                                    output.data(), output.size());
  if (output_size == 0) {
    auto winerr = GetLastError();
    BOOST_LOG(error) << "Failed to convert string to UTF-16: "sv << winerr;
    return {};
  }

  return output;
}

/**
 * @brief Converts a UTF-16 wide string into a UTF-8 string.
 * @param string The UTF-16 wide string.
 * @return The converted UTF-8 string.
 */
std::string to_utf8(const std::wstring &string) {
  // No conversion needed if the string is empty
  if (string.empty()) {
    return {};
  }

  // Get the output size required to store the string
  auto output_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, string.data(),
                                         string.size(), nullptr, 0, nullptr, nullptr);
  if (output_size == 0) {
    auto winerr = GetLastError();
    BOOST_LOG(error) << "Failed to get UTF-8 buffer size: "sv << winerr;
    return {};
  }

  // Perform the conversion
  std::string output(output_size, '\0');
  output_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, string.data(), string.size(),
                                    output.data(), output.size(), nullptr, nullptr);
  if (output_size == 0) {
    auto winerr = GetLastError();
    BOOST_LOG(error) << "Failed to convert string to UTF-8: "sv << winerr;
    return {};
  }

  return output;
}

class win32_high_precision_timer : public high_precision_timer {
public:
  win32_high_precision_timer() {
    // Use CREATE_WAITABLE_TIMER_HIGH_RESOLUTION if supported (Windows 10 1809+)
    timer = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                  TIMER_ALL_ACCESS);
    if (!timer) {
      timer = CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
      if (!timer) {
        BOOST_LOG(error)
            << "Unable to create high_precision_timer, CreateWaitableTimerEx() failed: "
            << GetLastError();
      }
    }
  }

  ~win32_high_precision_timer() {
    if (timer)
      CloseHandle(timer);
  }

  void sleep_for(const std::chrono::nanoseconds &duration) override {
    if (!timer) {
      BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with uninitialized timer";
      return;
    }
    if (duration < 0s) {
      BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with negative duration";
      return;
    }
    if (duration > 5s) {
      BOOST_LOG(error)
          << "Attempting high_precision_timer::sleep_for() with unexpectedly large duration (>5s)";
      return;
    }

    LARGE_INTEGER due_time;
    due_time.QuadPart = duration.count() / -100;
    SetWaitableTimer(timer, &due_time, 0, nullptr, nullptr, false);
    WaitForSingleObject(timer, INFINITE);
  }

  operator bool() override {
    return timer != NULL;
  }

private:
  HANDLE timer = NULL;
};

std::unique_ptr<high_precision_timer> create_high_precision_timer() {
  return std::make_unique<win32_high_precision_timer>();
}
} // namespace platf
