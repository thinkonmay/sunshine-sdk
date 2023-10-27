/**
 * @file src/platform/windows/misc.cpp
 * @brief todo
 */
#include <codecvt>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/process.hpp>

// prevent clang format from "optimizing" the header include order
// clang-format off
#include <dwmapi.h>
#include <iphlpapi.h>
#include <iterator>
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

#include "src/main.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include <iterator>


// UDP_SEND_MSG_SIZE was added in the Windows 10 20H1 SDK
#ifndef UDP_SEND_MSG_SIZE
  #define UDP_SEND_MSG_SIZE 2
#endif

// PROC_THREAD_ATTRIBUTE_JOB_LIST is currently missing from MinGW headers
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
  #define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

#ifndef HAS_QOS_FLOWID
typedef UINT32 QOS_FLOWID;
#endif

#ifndef HAS_PQOS_FLOWID
typedef UINT32 *PQOS_FLOWID;
#endif

#include <qos2.h>


namespace bp = boost::process;

using namespace std::literals;
namespace platf {
  using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

  bool enabled_mouse_keys = false;
  MOUSEKEYS previous_mouse_keys_state;

  HANDLE qos_handle = nullptr;

  decltype(QOSCreateHandle) *fn_QOSCreateHandle = nullptr;
  decltype(QOSAddSocketToFlow) *fn_QOSAddSocketToFlow = nullptr;
  decltype(QOSRemoveSocketFromFlow) *fn_QOSRemoveSocketFromFlow = nullptr;

  HANDLE wlan_handle = nullptr;

  decltype(WlanOpenHandle) *fn_WlanOpenHandle = nullptr;
  decltype(WlanCloseHandle) *fn_WlanCloseHandle = nullptr;
  decltype(WlanFreeMemory) *fn_WlanFreeMemory = nullptr;
  decltype(WlanEnumInterfaces) *fn_WlanEnumInterfaces = nullptr;
  decltype(WlanSetInterface) *fn_WlanSetInterface = nullptr;

  std::filesystem::path
  appdata() {
    WCHAR sunshine_path[MAX_PATH];
    GetModuleFileNameW(NULL, sunshine_path, _countof(sunshine_path));
    return std::filesystem::path { sunshine_path }.remove_filename() / L"config"sv;
  }

  std::string
  from_sockaddr(const sockaddr *const socket_address) {
    char data[INET6_ADDRSTRLEN] = {};

    auto family = socket_address->sa_family;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) socket_address)->sin6_addr, data, INET6_ADDRSTRLEN);
    }
    else if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) socket_address)->sin_addr, data, INET_ADDRSTRLEN);
    }

    return std::string { data };
  }

  std::pair<std::uint16_t, std::string>
  from_sockaddr_ex(const sockaddr *const ip_addr) {
    char data[INET6_ADDRSTRLEN] = {};

    auto family = ip_addr->sa_family;
    std::uint16_t port = 0;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
      port = ((sockaddr_in6 *) ip_addr)->sin6_port;
    }
    else if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
      port = ((sockaddr_in *) ip_addr)->sin_port;
    }

    return { port, std::string { data } };
  }

  HDESK
  syncThreadDesktop() {
    auto hDesk = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
    if (!hDesk) {
      auto err = GetLastError();
      // BOOST_LOG(error) << "Failed to Open Input Desktop [0x"sv << util::hex(err).to_string_view() << ']';

      return nullptr;
    }

    if (!SetThreadDesktop(hDesk)) {
      auto err = GetLastError();
      // BOOST_LOG(error) << "Failed to sync desktop to thread [0x"sv << util::hex(err).to_string_view() << ']';
    }

    CloseDesktop(hDesk);

    return hDesk;
  }

  void
  print_status(const std::string_view &prefix, HRESULT status) {
    char err_string[1024];

    DWORD bytes = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      status,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      err_string,
      sizeof(err_string),
      nullptr);

    // BOOST_LOG(error) << prefix << ": "sv << std::string_view { err_string, bytes };
  }

  bool
  IsUserAdmin(HANDLE user_token) {
    WINBOOL ret;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;
    ret = AllocateAndInitializeSid(
      &NtAuthority,
      2,
      SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_ADMINS,
      0, 0, 0, 0, 0, 0,
      &AdministratorsGroup);
    if (ret) {
      if (!CheckTokenMembership(user_token, AdministratorsGroup, &ret)) {
        ret = false;
        // BOOST_LOG(error) << "Failed to verify token membership for administrative access: " << GetLastError();
      }
      FreeSid(AdministratorsGroup);
    }
    else {
      // BOOST_LOG(error) << "Unable to allocate SID to check administrative access: " << GetLastError();
    }

    return ret;
  }

  /**
   * @brief Obtain the current sessions user's primary token with elevated privileges.
   * @return The user's token. If user has admin capability it will be elevated, otherwise it will be a limited token. On error, `nullptr`.
   */
  HANDLE
  retrieve_users_token(bool elevated) {
    DWORD consoleSessionId;
    HANDLE userToken;
    TOKEN_ELEVATION_TYPE elevationType;
    DWORD dwSize;

    // Get the session ID of the active console session
    consoleSessionId = WTSGetActiveConsoleSessionId();
    if (0xFFFFFFFF == consoleSessionId) {
      // If there is no active console session, log a warning and return null
      // BOOST_LOG(warning) << "There isn't an active user session, therefore it is not possible to execute commands under the users profile.";
      return nullptr;
    }

    // Get the user token for the active console session
    if (!WTSQueryUserToken(consoleSessionId, &userToken)) {
      // BOOST_LOG(debug) << "QueryUserToken failed, this would prevent commands from launching under the users profile.";
      return nullptr;
    }

    // We need to know if this is an elevated token or not.
    // Get the elevation type of the user token
    // Elevation - Default: User is not an admin, UAC enabled/disabled does not matter.
    // Elevation - Limited: User is an admin, has UAC enabled.
    // Elevation - Full:    User is an admin, has UAC disabled.
    if (!GetTokenInformation(userToken, TokenElevationType, &elevationType, sizeof(TOKEN_ELEVATION_TYPE), &dwSize)) {
      // BOOST_LOG(debug) << "Retrieving token information failed: " << GetLastError();
      CloseHandle(userToken);
      return nullptr;
    }

    // User is currently not an administrator
    // The documentation for this scenario is conflicting, so we'll double check to see if user is actually an admin.
    if (elevated && (elevationType == TokenElevationTypeDefault && !IsUserAdmin(userToken))) {
      // We don't have to strip the token or do anything here, but let's give the user a warning so they're aware what is happening.
      // BOOST_LOG(warning) << "This command requires elevation and the current user account logged in does not have administrator rights. "
                        //  << "For security reasons Sunshine will retain the same access level as the current user and will not elevate it.";
    }

    // User has a limited token, this means they have UAC enabled and is an Administrator
    if (elevated && elevationType == TokenElevationTypeLimited) {
      TOKEN_LINKED_TOKEN linkedToken;
      // Retrieve the administrator token that is linked to the limited token
      if (!GetTokenInformation(userToken, TokenLinkedToken, reinterpret_cast<void *>(&linkedToken), sizeof(TOKEN_LINKED_TOKEN), &dwSize)) {
        // If the retrieval failed, log an error message and return null
        // BOOST_LOG(error) << "Retrieving linked token information failed: " << GetLastError();
        CloseHandle(userToken);

        // There is no scenario where this should be hit, except for an actual error.
        return nullptr;
      }

      // Since we need the elevated token, we'll replace it with their administrative token.
      CloseHandle(userToken);
      userToken = linkedToken.LinkedToken;
    }

    // We don't need to do anything for TokenElevationTypeFull users here, because they're already elevated.
    return userToken;
  }

  bool
  merge_user_environment_block(bp::environment &env, HANDLE shell_token) {
    // Get the target user's environment block
    PVOID env_block;
    if (!CreateEnvironmentBlock(&env_block, shell_token, FALSE)) {
      return false;
    }

    // Parse the environment block and populate env
    for (auto c = (PWCHAR) env_block; *c != UNICODE_NULL; c += wcslen(c) + 1) {
      // Environment variable entries end with a null-terminator, so std::wstring() will get an entire entry.
      std::string env_tuple = converter.to_bytes(std::wstring { c });
      std::string env_name = env_tuple.substr(0, env_tuple.find('='));
      std::string env_val = env_tuple.substr(env_tuple.find('=') + 1);

      // Perform a case-insensitive search to see if this variable name already exists
      auto itr = std::find_if(env.cbegin(), env.cend(),
        [&](const auto &e) { return boost::iequals(e.get_name(), env_name); });
      if (itr != env.cend()) {
        // Use this existing name if it is already present to ensure we merge properly
        env_name = itr->get_name();
      }

      // For the PATH variable, we will merge the values together
      if (boost::iequals(env_name, "PATH")) {
        env[env_name] = env_val + ";" + env[env_name].to_string();
      }
      else {
        // Other variables will be superseded by those in the user's environment block
        env[env_name] = env_val;
      }
    }

    DestroyEnvironmentBlock(env_block);
    return true;
  }

  /**
   * @brief Check if the current process is running with system-level privileges.
   * @return `true` if the current process has system-level privileges, `false` otherwise.
   */
  bool
  is_running_as_system() {
    BOOL ret;
    PSID SystemSid;
    DWORD dwSize = SECURITY_MAX_SID_SIZE;

    // Allocate memory for the SID structure
    SystemSid = LocalAlloc(LMEM_FIXED, dwSize);
    if (SystemSid == nullptr) {
      // BOOST_LOG(error) << "Failed to allocate memory for the SID structure: " << GetLastError();
      return false;
    }

    // Create a SID for the local system account
    ret = CreateWellKnownSid(WinLocalSystemSid, nullptr, SystemSid, &dwSize);
    if (ret) {
      // Check if the current process token contains this SID
      if (!CheckTokenMembership(nullptr, SystemSid, &ret)) {
        // BOOST_LOG(error) << "Failed to check token membership: " << GetLastError();
        ret = false;
      }
    }
    else {
      // BOOST_LOG(error) << "Failed to create a SID for the local system account. This may happen if the system is out of memory or if the SID buffer is too small: " << GetLastError();
    }

    // Free the memory allocated for the SID structure
    LocalFree(SystemSid);
    return ret;
  }

  // Note: This does NOT append a null terminator
  void
  append_string_to_environment_block(wchar_t *env_block, int &offset, const std::wstring &wstr) {
    std::memcpy(&env_block[offset], wstr.data(), wstr.length() * sizeof(wchar_t));
    offset += wstr.length();
  }

  std::wstring
  create_environment_block(bp::environment &env) {
    int size = 0;
    for (const auto &entry : env) {
      auto name = entry.get_name();
      auto value = entry.to_string();
      size += converter.from_bytes(name).length() + 1 /* L'=' */ + converter.from_bytes(value).length() + 1 /* L'\0' */;
    }

    size += 1 /* L'\0' */;

    wchar_t env_block[size];
    int offset = 0;
    for (const auto &entry : env) {
      auto name = entry.get_name();
      auto value = entry.to_string();

      // Construct the NAME=VAL\0 string
      append_string_to_environment_block(env_block, offset, converter.from_bytes(name));
      env_block[offset++] = L'=';
      append_string_to_environment_block(env_block, offset, converter.from_bytes(value));
      env_block[offset++] = L'\0';
    }

    // Append a final null terminator
    env_block[offset++] = L'\0';

    return std::wstring(env_block, offset);
  }

  LPPROC_THREAD_ATTRIBUTE_LIST
  allocate_proc_thread_attr_list(DWORD attribute_count) {
    SIZE_T size;
    InitializeProcThreadAttributeList(NULL, attribute_count, 0, &size);

    auto list = (LPPROC_THREAD_ATTRIBUTE_LIST) HeapAlloc(GetProcessHeap(), 0, size);
    if (list == NULL) {
      return NULL;
    }

    if (!InitializeProcThreadAttributeList(list, attribute_count, 0, &size)) {
      HeapFree(GetProcessHeap(), 0, list);
      return NULL;
    }

    return list;
  }

  void
  free_proc_thread_attr_list(LPPROC_THREAD_ATTRIBUTE_LIST list) {
    DeleteProcThreadAttributeList(list);
    HeapFree(GetProcessHeap(), 0, list);
  }

  /**
   * @brief Create a `bp::child` object from the results of launching a process.
   * @param process_launched A boolean indicating if the launch was successful.
   * @param cmd The command that was used to launch the process.
   * @param ec A reference to an `std::error_code` object that will store any error that occurred during the launch.
   * @param process_info A reference to a `PROCESS_INFORMATION` structure that contains information about the new process.
   * @return A `bp::child` object representing the new process, or an empty `bp::child` object if the launch failed.
   */
  bp::child
  create_boost_child_from_results(bool process_launched, const std::string &cmd, std::error_code &ec, PROCESS_INFORMATION &process_info) {
    // Use RAII to ensure the process is closed when we're done with it, even if there was an error.
    auto close_process_handles = util::fail_guard([process_launched, process_info]() {
      if (process_launched) {
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
      }
    });

    if (ec) {
      // If there was an error, return an empty bp::child object
      return bp::child();
    }

    if (process_launched) {
      // If the launch was successful, create a new bp::child object representing the new process
      auto child = bp::child((bp::pid_t) process_info.dwProcessId);
      // BOOST_LOG(info) << cmd << " running with PID "sv << child.id();
      return child;
    }
    else {
      auto winerror = GetLastError();
      // BOOST_LOG(error) << "Failed to launch process: "sv << winerror;
      ec = std::make_error_code(std::errc::invalid_argument);
      // We must NOT attach the failed process here, since this case can potentially be induced by ACL
      // manipulation (denying yourself execute permission) to cause an escalation of privilege.
      // So to protect ourselves against that, we'll return an empty child process instead.
      return bp::child();
    }
  }

  /**
   * @brief Impersonate the current user and invoke the callback function.
   * @param user_token A handle to the user's token that was obtained from the shell.
   * @param callback A function that will be executed while impersonating the user.
   * @return An `std::error_code` object that will store any error that occurred during the impersonation
   */
  std::error_code
  impersonate_current_user(HANDLE user_token, std::function<void()> callback) {
    std::error_code ec;
    // Impersonate the user when launching the process. This will ensure that appropriate access
    // checks are done against the user token, not our SYSTEM token. It will also allow network
    // shares and mapped network drives to be used as launch targets, since those credentials
    // are stored per-user.
    if (!ImpersonateLoggedOnUser(user_token)) {
      auto winerror = GetLastError();
      // Log the failure of impersonating the user and its error code
      // BOOST_LOG(error) << "Failed to impersonate user: "sv << winerror;
      ec = std::make_error_code(std::errc::permission_denied);
      return ec;
    }

    // Execute the callback function while impersonating the user
    callback();

    // End impersonation of the logged on user. If this fails (which is extremely unlikely),
    // we will be running with an unknown user token. The only safe thing to do in that case
    // is terminate ourselves.
    if (!RevertToSelf()) {
      auto winerror = GetLastError();
      // Log the failure of reverting to self and its error code
      // BOOST_LOG(fatal) << "Failed to revert to self after impersonation: "sv << winerror;
      std::abort();
    }

    return ec;
  }

  /**
   * @brief A function to create a `STARTUPINFOEXW` structure for launching a process.
   * @param file A pointer to a `FILE` object that will be used as the standard output and error for the new process, or null if not needed.
   * @param job A job object handle to insert the new process into. This pointer must remain valid for the life of this startup info!
   * @param ec A reference to a `std::error_code` object that will store any error that occurred during the creation of the structure.
   * @return A `STARTUPINFOEXW` structure that contains information about how to launch the new process.
   */
  STARTUPINFOEXW
  create_startup_info(FILE *file, HANDLE *job, std::error_code &ec) {
    // Initialize a zeroed-out STARTUPINFOEXW structure and set its size
    STARTUPINFOEXW startup_info = {};
    startup_info.StartupInfo.cb = sizeof(startup_info);

    // Allocate a process attribute list with space for 2 elements
    startup_info.lpAttributeList = allocate_proc_thread_attr_list(2);
    if (startup_info.lpAttributeList == NULL) {
      // If the allocation failed, set ec to an appropriate error code and return the structure
      ec = std::make_error_code(std::errc::not_enough_memory);
      return startup_info;
    }

    if (file) {
      // If a file was provided, get its handle and use it as the standard output and error for the new process
      HANDLE log_file_handle = (HANDLE) _get_osfhandle(_fileno(file));

      // Populate std handles if the caller gave us a log file to use
      startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
      startup_info.StartupInfo.hStdInput = NULL;
      startup_info.StartupInfo.hStdOutput = log_file_handle;
      startup_info.StartupInfo.hStdError = log_file_handle;

      // Allow the log file handle to be inherited by the child process (without inheriting all of
      // our inheritable handles, such as our own log file handle created by SunshineSvc).
      //
      // Note: The value we point to here must be valid for the lifetime of the attribute list,
      // so we need to point into the STARTUPINFO instead of our log_file_variable on the stack.
      UpdateProcThreadAttribute(startup_info.lpAttributeList,
        0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        &startup_info.StartupInfo.hStdOutput,
        sizeof(startup_info.StartupInfo.hStdOutput),
        NULL,
        NULL);
    }

    if (job) {
      // Atomically insert the new process into the specified job.
      //
      // Note: The value we point to here must be valid for the lifetime of the attribute list,
      // so we take a HANDLE* instead of just a HANDLE to use the caller's stack storage.
      UpdateProcThreadAttribute(startup_info.lpAttributeList,
        0,
        PROC_THREAD_ATTRIBUTE_JOB_LIST,
        job,
        sizeof(*job),
        NULL,
        NULL);
    }

    return startup_info;
  }

  void
  adjust_thread_priority(thread_priority_e priority) {
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
        // BOOST_LOG(error) << "Unknown thread priority: "sv << (int) priority;
        return;
    }

    if (!SetThreadPriority(GetCurrentThread(), win32_priority)) {
      auto winerr = GetLastError();
      // BOOST_LOG(warning) << "Unable to set thread priority to "sv << win32_priority << ": "sv << winerr;
    }
  }





  int64_t
  qpc_counter() {
    LARGE_INTEGER performace_counter;
    if (QueryPerformanceCounter(&performace_counter)) return performace_counter.QuadPart;
    return 0;
  }

  std::chrono::nanoseconds
  qpc_time_difference(int64_t performance_counter1, int64_t performance_counter2) {
    auto get_frequency = []() {
      LARGE_INTEGER frequency;
      frequency.QuadPart = 0;
      QueryPerformanceFrequency(&frequency);
      return frequency.QuadPart;
    };
    static const double frequency = get_frequency();
    if (frequency) {
      return std::chrono::nanoseconds((int64_t) ((performance_counter1 - performance_counter2) * frequency / std::nano::den));
    }
    return {};
  }


  // It's not big enough to justify it's own source file :/
  namespace dxgi {
    int
    init();
  }

  std::unique_ptr<deinit_t>
  init() {
    if (dxgi::init()) {
      return nullptr;
    }
    return std::make_unique<deinit_t>();
  }
}  // namespace platf
