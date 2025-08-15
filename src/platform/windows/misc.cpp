/**
 * @file src/platform/windows/misc.cpp
 * @brief todo
 */
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/program_options/parsers.hpp>

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

// Boost overrides NTDDI_VERSION, so we re-override it here
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN10
#include <Shlwapi.h>

#include "misc.h"

#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include <iterator>

#include "nvprefs/nvprefs_interface.h"

// UDP_SEND_MSG_SIZE was added in the Windows 10 20H1 SDK
#ifndef UDP_SEND_MSG_SIZE
  #define UDP_SEND_MSG_SIZE 2
#endif

// PROC_THREAD_ATTRIBUTE_JOB_LIST is currently missing from MinGW headers
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
  #define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

#include <qos2.h>

#ifndef WLAN_API_MAKE_VERSION
  #define WLAN_API_MAKE_VERSION(_major, _minor) (((DWORD) (_minor)) << 16 | (_major))
#endif


using namespace std::literals;
namespace platf {
  using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

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

  adapteraddrs_t
  get_adapteraddrs() {
    adapteraddrs_t info { nullptr };
    ULONG size = 0;

    while (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, info.get(), &size) == ERROR_BUFFER_OVERFLOW) {
      info.reset((PIP_ADAPTER_ADDRESSES) malloc(size));
    }

    return info;
  }

  std::string
  get_mac_address(const std::string_view &address) {
    adapteraddrs_t info = get_adapteraddrs();
    for (auto adapter_pos = info.get(); adapter_pos != nullptr; adapter_pos = adapter_pos->Next) {
      for (auto addr_pos = adapter_pos->FirstUnicastAddress; addr_pos != nullptr; addr_pos = addr_pos->Next) {
        if (adapter_pos->PhysicalAddressLength != 0 && address == from_sockaddr(addr_pos->Address.lpSockaddr)) {
          std::stringstream mac_addr;
          mac_addr << std::hex;
          for (int i = 0; i < adapter_pos->PhysicalAddressLength; i++) {
            if (i > 0) {
              mac_addr << ':';
            }
            mac_addr << std::setw(2) << std::setfill('0') << (int) adapter_pos->PhysicalAddress[i];
          }
          return mac_addr.str();
        }
      }
    }
    BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
    return "00:00:00:00:00:00"s;
  }

  HDESK
  syncThreadDesktop() {
    HWINSTA hWinSta0 = OpenWindowStation(TEXT("WinSta0"), FALSE, MAXIMUM_ALLOWED);
    if (NULL == hWinSta0) { 
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to OpenWindowStation [0x"sv << util::hex(err).to_string_view() << ']';
    }

    if (!SetProcessWindowStation(hWinSta0)) { 
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to SetProcessWindowStation [0x"sv << util::hex(err).to_string_view() << ']';
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
      BOOST_LOG(error) << "Failed to SetThreadDesktop [0x"sv << util::hex(err).to_string_view() << ']';
    }

    if (hDesk != NULL) { CloseDesktop(hDesk); }
    if (hWinSta0 != NULL) { CloseWindowStation(hWinSta0); }
    BOOST_LOG(debug) << "Thread sync with default desktop"sv;
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

    BOOST_LOG(error) << prefix << ": "sv << std::string_view { err_string, bytes };
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
        BOOST_LOG(error) << "Failed to verify token membership for administrative access: " << GetLastError();
      }
      FreeSid(AdministratorsGroup);
    }
    else {
      BOOST_LOG(error) << "Unable to allocate SID to check administrative access: " << GetLastError();
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
      BOOST_LOG(warning) << "There isn't an active user session, therefore it is not possible to execute commands under the users profile.";
      return nullptr;
    }

    // Get the user token for the active console session
    if (!WTSQueryUserToken(consoleSessionId, &userToken)) {
      BOOST_LOG(debug) << "QueryUserToken failed, this would prevent commands from launching under the users profile.";
      return nullptr;
    }

    // We need to know if this is an elevated token or not.
    // Get the elevation type of the user token
    // Elevation - Default: User is not an admin, UAC enabled/disabled does not matter.
    // Elevation - Limited: User is an admin, has UAC enabled.
    // Elevation - Full:    User is an admin, has UAC disabled.
    if (!GetTokenInformation(userToken, TokenElevationType, &elevationType, sizeof(TOKEN_ELEVATION_TYPE), &dwSize)) {
      BOOST_LOG(debug) << "Retrieving token information failed: " << GetLastError();
      CloseHandle(userToken);
      return nullptr;
    }

    // User is currently not an administrator
    // The documentation for this scenario is conflicting, so we'll double check to see if user is actually an admin.
    if (elevated && (elevationType == TokenElevationTypeDefault && !IsUserAdmin(userToken))) {
      // We don't have to strip the token or do anything here, but let's give the user a warning so they're aware what is happening.
      BOOST_LOG(warning) << "This command requires elevation and the current user account logged in does not have administrator rights. "
                         << "For security reasons Sunshine will retain the same access level as the current user and will not elevate it.";
    }

    // User has a limited token, this means they have UAC enabled and is an Administrator
    if (elevated && elevationType == TokenElevationTypeLimited) {
      TOKEN_LINKED_TOKEN linkedToken;
      // Retrieve the administrator token that is linked to the limited token
      if (!GetTokenInformation(userToken, TokenLinkedToken, reinterpret_cast<void *>(&linkedToken), sizeof(TOKEN_LINKED_TOKEN), &dwSize)) {
        // If the retrieval failed, log an error message and return null
        BOOST_LOG(error) << "Retrieving linked token information failed: " << GetLastError();
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


  // Note: This does NOT append a null terminator
  void
  append_string_to_environment_block(wchar_t *env_block, int &offset, const std::wstring &wstr) {
    std::memcpy(&env_block[offset], wstr.data(), wstr.length() * sizeof(wchar_t));
    offset += wstr.length();
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
      BOOST_LOG(error) << "Failed to impersonate user: "sv << winerror;
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
      BOOST_LOG(fatal) << "Failed to revert to self after impersonation: "sv << winerror;
      DebugBreak();
    }

    return ec;
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
        BOOST_LOG(error) << "Unknown thread priority: "sv << (int) priority;
        return;
    }

    if (!SetThreadPriority(GetCurrentThread(), win32_priority)) {
      auto winerr = GetLastError();
      BOOST_LOG(warning) << "Unable to set thread priority to "sv << win32_priority << ": "sv << winerr;
    }
  }



  void
  restart_on_exit() {
    STARTUPINFOEXW startup_info {};
    startup_info.StartupInfo.cb = sizeof(startup_info);

    WCHAR executable[MAX_PATH];
    if (GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable)) == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(fatal) << "Failed to get Sunshine path: "sv << winerr;
      return;
    }

    PROCESS_INFORMATION process_info;
    if (!CreateProcessW(executable,
          GetCommandLineW(),
          nullptr,
          nullptr,
          false,
          CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
          nullptr,
          nullptr,
          (LPSTARTUPINFOW) &startup_info,
          &process_info)) {
      auto winerr = GetLastError();
      BOOST_LOG(fatal) << "Unable to restart Sunshine: "sv << winerr;
      return;
    }

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
  }

  void
  restart() {
  }

  struct enum_wnd_context_t {
    std::set<DWORD> process_ids;
    bool requested_exit;
  };

  static BOOL CALLBACK
  prgrp_enum_windows(HWND hwnd, LPARAM lParam) {
    auto enum_ctx = (enum_wnd_context_t *) lParam;

    // Find the owner PID of this window
    DWORD wnd_process_id;
    if (!GetWindowThreadProcessId(hwnd, &wnd_process_id)) {
      // Continue enumeration
      return TRUE;
    }

    // Check if this window is owned by a process we want to terminate
    if (enum_ctx->process_ids.find(wnd_process_id) != enum_ctx->process_ids.end()) {
      // Send an async WM_CLOSE message to this window
      if (SendNotifyMessageW(hwnd, WM_CLOSE, 0, 0)) {
        BOOST_LOG(debug) << "Sent WM_CLOSE to PID: "sv << wnd_process_id;
        enum_ctx->requested_exit = true;
      }
      else {
        auto error = GetLastError();
        BOOST_LOG(warning) << "Failed to send WM_CLOSE to PID ["sv << wnd_process_id << "]: " << error;
      }
    }

    // Continue enumeration
    return TRUE;
  }

  /**
   * @brief Attempt to gracefully terminate a process group.
   * @param native_handle The job object handle.
   * @return true if termination was successfully requested.
   */
  bool
  request_process_group_exit(std::uintptr_t native_handle) {
    auto job_handle = (HANDLE) native_handle;

    // Get list of all processes in our job object
    bool success;
    DWORD required_length = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST);
    auto process_id_list = (PJOBOBJECT_BASIC_PROCESS_ID_LIST) calloc(1, required_length);
    auto fg = util::fail_guard([&process_id_list]() {
      free(process_id_list);
    });
    while (!(success = QueryInformationJobObject(job_handle, JobObjectBasicProcessIdList,
               process_id_list, required_length, &required_length)) &&
           GetLastError() == ERROR_MORE_DATA) {
      free(process_id_list);
      process_id_list = (PJOBOBJECT_BASIC_PROCESS_ID_LIST) calloc(1, required_length);
      if (!process_id_list) {
        return false;
      }
    }

    if (!success) {
      auto err = GetLastError();
      BOOST_LOG(warning) << "Failed to enumerate processes in group: "sv << err;
      return false;
    }
    else if (process_id_list->NumberOfProcessIdsInList == 0) {
      // If all processes are already dead, treat it as a success
      return true;
    }

    enum_wnd_context_t enum_ctx = {};
    enum_ctx.requested_exit = false;
    for (DWORD i = 0; i < process_id_list->NumberOfProcessIdsInList; i++) {
      enum_ctx.process_ids.emplace(process_id_list->ProcessIdList[i]);
    }

    // Enumerate all windows belonging to processes in the list
    EnumWindows(prgrp_enum_windows, (LPARAM) &enum_ctx);

    // Return success if we told at least one window to close
    return enum_ctx.requested_exit;
  }

  /**
   * @brief Checks if a process group still has running children.
   * @param native_handle The job object handle.
   * @return true if processes are still running.
   */
  bool
  process_group_running(std::uintptr_t native_handle) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting_info;

    if (!QueryInformationJobObject((HANDLE) native_handle, JobObjectBasicAccountingInformation, &accounting_info, sizeof(accounting_info), nullptr)) {
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to get job accounting info: "sv << err;
      return false;
    }

    return accounting_info.ActiveProcesses != 0;
  }

  SOCKADDR_IN
  to_sockaddr(boost::asio::ip::address_v4 address, uint16_t port) {
    SOCKADDR_IN saddr_v4 = {};

    saddr_v4.sin_family = AF_INET;
    saddr_v4.sin_port = htons(port);

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v4.sin_addr, addr_bytes.data(), sizeof(saddr_v4.sin_addr));

    return saddr_v4;
  }

  SOCKADDR_IN6
  to_sockaddr(boost::asio::ip::address_v6 address, uint16_t port) {
    SOCKADDR_IN6 saddr_v6 = {};

    saddr_v6.sin6_family = AF_INET6;
    saddr_v6.sin6_port = htons(port);
    saddr_v6.sin6_scope_id = address.scope_id();

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v6.sin6_addr, addr_bytes.data(), sizeof(saddr_v6.sin6_addr));

    return saddr_v6;
  }

  // Use UDP segmentation offload if it is supported by the OS. If the NIC is capable, this will use
  // hardware acceleration to reduce CPU usage. Support for USO was introduced in Windows 10 20H1.
  bool
  send_batch(batched_send_info_t &send_info) {
    WSAMSG msg;

    // Convert the target address into a SOCKADDR
    SOCKADDR_IN taddr_v4;
    SOCKADDR_IN6 taddr_v6;
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v6;
      msg.namelen = sizeof(taddr_v6);
    }
    else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v4;
      msg.namelen = sizeof(taddr_v4);
    }

    WSABUF buf;
    buf.buf = (char *) send_info.buffer;
    buf.len = send_info.block_size * send_info.block_count;

    msg.lpBuffers = &buf;
    msg.dwBufferCount = 1;
    msg.dwFlags = 0;

    // At most, one DWORD option and one PKTINFO option
    char cmbuf[WSA_CMSG_SPACE(sizeof(DWORD)) +
               std::max(WSA_CMSG_SPACE(sizeof(IN6_PKTINFO)), WSA_CMSG_SPACE(sizeof(IN_PKTINFO)))] = {};
    ULONG cmbuflen = 0;

    msg.Control.buf = cmbuf;
    msg.Control.len = sizeof(cmbuf);

    auto cm = WSA_CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      IN6_PKTINFO pktInfo;

      SOCKADDR_IN6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IPV6;
      cm->cmsg_type = IPV6_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }
    else {
      IN_PKTINFO pktInfo;

      SOCKADDR_IN saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_addr = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IP;
      cm->cmsg_type = IP_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }

    if (send_info.block_count > 1) {
      cmbuflen += WSA_CMSG_SPACE(sizeof(DWORD));

      cm = WSA_CMSG_NXTHDR(&msg, cm);
      cm->cmsg_level = IPPROTO_UDP;
      cm->cmsg_type = UDP_SEND_MSG_SIZE;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(DWORD));
      *((DWORD *) WSA_CMSG_DATA(cm)) = send_info.block_size;
    }

    msg.Control.len = cmbuflen;

    // If USO is not supported, this will fail and the caller will fall back to unbatched sends.
    DWORD bytes_sent;
    return WSASendMsg((SOCKET) send_info.native_socket, &msg, 1, &bytes_sent, nullptr, nullptr) != SOCKET_ERROR;
  }

  bool
  send(send_info_t &send_info) {
    WSAMSG msg;

    // Convert the target address into a SOCKADDR
    SOCKADDR_IN taddr_v4;
    SOCKADDR_IN6 taddr_v6;
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v6;
      msg.namelen = sizeof(taddr_v6);
    }
    else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.name = (PSOCKADDR) &taddr_v4;
      msg.namelen = sizeof(taddr_v4);
    }

    WSABUF buf;
    buf.buf = (char *) send_info.buffer;
    buf.len = send_info.size;

    msg.lpBuffers = &buf;
    msg.dwBufferCount = 1;
    msg.dwFlags = 0;

    char cmbuf[std::max(WSA_CMSG_SPACE(sizeof(IN6_PKTINFO)), WSA_CMSG_SPACE(sizeof(IN_PKTINFO)))] = {};
    ULONG cmbuflen = 0;

    msg.Control.buf = cmbuf;
    msg.Control.len = sizeof(cmbuf);

    auto cm = WSA_CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      IN6_PKTINFO pktInfo;

      SOCKADDR_IN6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IPV6;
      cm->cmsg_type = IPV6_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }
    else {
      IN_PKTINFO pktInfo;

      SOCKADDR_IN saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_addr = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += WSA_CMSG_SPACE(sizeof(pktInfo));

      cm->cmsg_level = IPPROTO_IP;
      cm->cmsg_type = IP_PKTINFO;
      cm->cmsg_len = WSA_CMSG_LEN(sizeof(pktInfo));
      memcpy(WSA_CMSG_DATA(cm), &pktInfo, sizeof(pktInfo));
    }

    msg.Control.len = cmbuflen;

    DWORD bytes_sent;
    if (WSASendMsg((SOCKET) send_info.native_socket, &msg, 1, &bytes_sent, nullptr, nullptr) == SOCKET_ERROR) {
      auto winerr = WSAGetLastError();
      BOOST_LOG(warning) << "WSASendMsg() failed: "sv << winerr;
      return false;
    }

    return true;
  }

  class qos_t: public deinit_t {
  public:
    qos_t(QOS_FLOWID flow_id):
        flow_id(flow_id) {}

    virtual ~qos_t() {
      if (!fn_QOSRemoveSocketFromFlow(qos_handle, (SOCKET) NULL, flow_id, 0)) {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "QOSRemoveSocketFromFlow() failed: "sv << winerr;
      }
    }

  private:
    QOS_FLOWID flow_id;
  };

  /**
   * @brief Enables QoS on the given socket for traffic to the specified destination.
   * @param native_socket The native socket handle.
   * @param address The destination address for traffic sent on this socket.
   * @param port The destination port for traffic sent on this socket.
   * @param data_type The type of traffic sent on this socket.
   * @param dscp_tagging Specifies whether to enable DSCP tagging on outgoing traffic.
   */
  std::unique_ptr<deinit_t>
  enable_socket_qos(uintptr_t native_socket, boost::asio::ip::address &address, uint16_t port, qos_data_type_e data_type, bool dscp_tagging) {
    SOCKADDR_IN saddr_v4;
    SOCKADDR_IN6 saddr_v6;
    PSOCKADDR dest_addr;
    bool using_connect_hack = false;

    // Windows doesn't support any concept of traffic priority without DSCP tagging
    if (!dscp_tagging) {
      return nullptr;
    }

    static std::once_flag load_qwave_once_flag;
    std::call_once(load_qwave_once_flag, []() {
      // qWAVE is not installed by default on Windows Server, so we load it dynamically
      HMODULE qwave = LoadLibraryExA("qwave.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (!qwave) {
        BOOST_LOG(debug) << "qwave.dll is not available on this OS"sv;
        return;
      }

      fn_QOSCreateHandle = (decltype(fn_QOSCreateHandle)) GetProcAddress(qwave, "QOSCreateHandle");
      fn_QOSAddSocketToFlow = (decltype(fn_QOSAddSocketToFlow)) GetProcAddress(qwave, "QOSAddSocketToFlow");
      fn_QOSRemoveSocketFromFlow = (decltype(fn_QOSRemoveSocketFromFlow)) GetProcAddress(qwave, "QOSRemoveSocketFromFlow");

      if (!fn_QOSCreateHandle || !fn_QOSAddSocketToFlow || !fn_QOSRemoveSocketFromFlow) {
        BOOST_LOG(error) << "qwave.dll is missing exports?"sv;

        fn_QOSCreateHandle = nullptr;
        fn_QOSAddSocketToFlow = nullptr;
        fn_QOSRemoveSocketFromFlow = nullptr;

        FreeLibrary(qwave);
        return;
      }

      QOS_VERSION qos_version { 1, 0 };
      if (!fn_QOSCreateHandle(&qos_version, &qos_handle)) {
        auto winerr = GetLastError();
        BOOST_LOG(warning) << "QOSCreateHandle() failed: "sv << winerr;
        return;
      }
    });

    // If qWAVE is unavailable, just return
    if (!fn_QOSAddSocketToFlow || !qos_handle) {
      return nullptr;
    }

    auto disconnect_fg = util::fail_guard([&]() {
      if (using_connect_hack) {
        SOCKADDR_IN6 empty = {};
        empty.sin6_family = AF_INET6;
        if (connect((SOCKET) native_socket, (PSOCKADDR) &empty, sizeof(empty)) < 0) {
          auto wsaerr = WSAGetLastError();
          BOOST_LOG(error) << "qWAVE dual-stack workaround failed: "sv << wsaerr;
        }
      }
    });

    if (address.is_v6()) {
      auto address_v6 = address.to_v6();

      saddr_v6 = to_sockaddr(address_v6, port);
      dest_addr = (PSOCKADDR) &saddr_v6;

      // qWAVE doesn't properly support IPv4-mapped IPv6 addresses, nor does it
      // correctly support IPv4 addresses on a dual-stack socket (despite MSDN's
      // claims to the contrary). To get proper QoS tagging when hosting in dual
      // stack mode, we will temporarily connect() the socket to allow qWAVE to
      // successfully initialize a flow, then disconnect it again so WSASendMsg()
      // works later on.
      if (address_v6.is_v4_mapped()) {
        if (connect((SOCKET) native_socket, (PSOCKADDR) &saddr_v6, sizeof(saddr_v6)) < 0) {
          auto wsaerr = WSAGetLastError();
          BOOST_LOG(error) << "qWAVE dual-stack workaround failed: "sv << wsaerr;
        }
        else {
          BOOST_LOG(debug) << "Using qWAVE connect() workaround for QoS tagging"sv;
          using_connect_hack = true;
          dest_addr = nullptr;
        }
      }
    }
    else {
      saddr_v4 = to_sockaddr(address.to_v4(), port);
      dest_addr = (PSOCKADDR) &saddr_v4;
    }

    QOS_TRAFFIC_TYPE traffic_type;
    switch (data_type) {
      case qos_data_type_e::audio:
        traffic_type = QOSTrafficTypeVoice;
        break;
      case qos_data_type_e::video:
        traffic_type = QOSTrafficTypeAudioVideo;
        break;
      default:
        BOOST_LOG(error) << "Unknown traffic type: "sv << (int) data_type;
        return nullptr;
    }

    QOS_FLOWID flow_id = 0;
    if (!fn_QOSAddSocketToFlow(qos_handle, (SOCKET) native_socket, dest_addr, traffic_type, QOS_NON_ADAPTIVE_FLOW, &flow_id)) {
      auto winerr = GetLastError();
      BOOST_LOG(warning) << "QOSAddSocketToFlow() failed: "sv << winerr;
      return nullptr;
    }

    return std::make_unique<qos_t>(flow_id);
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

  /**
   * @brief Converts a UTF-8 string into a UTF-16 wide string.
   * @param string The UTF-8 string.
   * @return The converted UTF-16 wide string.
   */
  std::wstring
  from_utf8(const std::string &string) {
    // No conversion needed if the string is empty
    if (string.empty()) {
      return {};
    }

    // Get the output size required to store the string
    auto output_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string.data(), string.size(), nullptr, 0);
    if (output_size == 0) {
      auto winerr = GetLastError();
      BOOST_LOG(error) << "Failed to get UTF-16 buffer size: "sv << winerr;
      return {};
    }

    // Perform the conversion
    std::wstring output(output_size, L'\0');
    output_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string.data(), string.size(), output.data(), output.size());
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
  std::string
  to_utf8(const std::wstring &string) {
    // No conversion needed if the string is empty
    if (string.empty()) {
      return {};
    }

    // Get the output size required to store the string
    auto output_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, string.data(), string.size(),
      nullptr, 0, nullptr, nullptr);
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

  class win32_high_precision_timer: public high_precision_timer {
  public:
    win32_high_precision_timer() {
      // Use CREATE_WAITABLE_TIMER_HIGH_RESOLUTION if supported (Windows 10 1809+)
      timer = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
      if (!timer) {
        timer = CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        if (!timer) {
          BOOST_LOG(error) << "Unable to create high_precision_timer, CreateWaitableTimerEx() failed: " << GetLastError();
        }
      }
    }

    ~win32_high_precision_timer() {
      if (timer) CloseHandle(timer);
    }

    void
    sleep_for(const std::chrono::nanoseconds &duration) override {
      if (!timer) {
        BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with uninitialized timer";
        return;
      }
      if (duration < 0s) {
        BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with negative duration";
        return;
      }
      if (duration > 5s) {
        BOOST_LOG(error) << "Attempting high_precision_timer::sleep_for() with unexpectedly large duration (>5s)";
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

  std::unique_ptr<high_precision_timer>
  create_high_precision_timer() {
    return std::make_unique<win32_high_precision_timer>();
  }
}  // namespace platf
