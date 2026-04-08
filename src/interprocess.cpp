/*
Looking Glass - KVM FrameRelay (KVMFR) Client
Copyright (C) 2017-2019 Geoffrey McRae <geoff@hostfission.com>
https://looking-glass.hostfission.com

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "interprocess.h"
#include <SetupAPI.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <initguid.h>

#include "logging.h"

DEFINE_GUID(GUID_DEVINTERFACE_IVSHMEM, 0xdf576976, 0x569d, 0x4672, 0x95, 0xa0, 0xf5, 0x7e, 0x4e,
            0xa0, 0xb2, 0x10);
// {df576976-569d-4672-95a0-f57e4ea0b210}

typedef UINT16 IVSHMEM_PEERID;
typedef UINT64 IVSHMEM_SIZE;

#define IVSHMEM_CACHE_NONCACHED 0
#define IVSHMEM_CACHE_CACHED 1
#define IVSHMEM_CACHE_WRITECOMBINED 2

/*
    This structure is for use with the IOCTL_IVSHMEM_REQUEST_MMAP IOCTL
*/
typedef struct IVSHMEM_MMAP_CONFIG {
  UINT8 cacheMode; // the caching mode of the mapping, see IVSHMEM_CACHE_* for options
} IVSHMEM_MMAP_CONFIG, *PIVSHMEM_MMAP_CONFIG;

/*
    This structure is for use with the IOCTL_IVSHMEM_REQUEST_MMAP IOCTL
*/
typedef struct IVSHMEM_MMAP {
  IVSHMEM_PEERID peerID; // our peer id
  IVSHMEM_SIZE size;     // the size of the memory region
  PVOID ptr;             // pointer to the memory region
  UINT16 vectors;        // the number of vectors available
} IVSHMEM_MMAP, *PIVSHMEM_MMAP;

/*
    This structure is for use with the IOCTL_IVSHMEM_RING_DOORBELL IOCTL
*/
typedef struct IVSHMEM_RING {
  IVSHMEM_PEERID peerID; // the id of the peer to ring
  UINT16 vector;         // the doorbell to ring
} IVSHMEM_RING, *PIVSHMEM_RING;

/*
   This structure is for use with the IOCTL_IVSHMEM_REGISTER_EVENT IOCTL

   Please Note:
     - The IVSHMEM driver has a hard limit of 32 events.
     - Events that are singleShot are released after they have been set.
     - At this time repeating events are only released when the driver device
       handle is closed, closing the event handle doesn't release it from the
       drivers list. While this won't cause a problem in the driver, it will
       cause you to run out of event slots.
 */
typedef struct IVSHMEM_EVENT {
  UINT16 vector;      // the vector that triggers the event
  HANDLE event;       // the event to trigger
  BOOLEAN singleShot; // set to TRUE if you want the driver to only trigger this event once
} IVSHMEM_EVENT, *PIVSHMEM_EVENT;

#define IOCTL_IVSHMEM_REQUEST_PEERID                                                               \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IVSHMEM_REQUEST_SIZE                                                                 \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IVSHMEM_REQUEST_MMAP                                                                 \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IVSHMEM_RELEASE_MMAP                                                                 \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IVSHMEM_RING_DOORBELL                                                                \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IVSHMEM_REGISTER_EVENT                                                               \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

IVSHMEM *IVSHMEM::m_instance = NULL;

IVSHMEM::IVSHMEM(const char *path)
    : m_initialized(false), m_handle(INVALID_HANDLE_VALUE), m_gotSize(false), m_gotMemory(false) {
  memset(m_devPath, 0, 512);
  memcpy(m_devPath, path, strlen(path));
}

IVSHMEM::~IVSHMEM() {
  DeInitialize();
}

bool IVSHMEM::Initialize() {
  if (m_initialized)
    DeInitialize();

  m_handle = CreateFileA(m_devPath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
  if (m_handle == INVALID_HANDLE_VALUE) {
    BOOST_LOG(error) << "CreateFile returned INVALID_HANDLE_VALUE";
    return false;
  }

  m_initialized = true;
  return m_initialized;
}

void IVSHMEM::DeInitialize() {
  if (!m_initialized)
    return;

  if (m_gotMemory) {
    if (!DeviceIoControl(m_handle, IOCTL_IVSHMEM_RELEASE_MMAP, NULL, 0, NULL, 0, NULL, NULL))
      BOOST_LOG(error) << "Deintialize DeviceIoControl failed: " << (int)GetLastError();
    m_memory = NULL;
  }

  if (m_handle != INVALID_HANDLE_VALUE)
    CloseHandle(m_handle);

  m_initialized = false;
  m_handle = INVALID_HANDLE_VALUE;
  m_gotSize = false;
  m_gotMemory = false;
}

UINT64 IVSHMEM::GetSize() {
  if (!m_initialized)
    return 0;

  if (m_gotSize)
    return m_size;

  IVSHMEM_SIZE size;
  if (!DeviceIoControl(m_handle, IOCTL_IVSHMEM_REQUEST_SIZE, NULL, 0, &size, sizeof(IVSHMEM_SIZE),
                       NULL, NULL)) {
    BOOST_LOG(error) << "GetSize DeviceIoControl Failed: " << GetLastError();
    return 0;
  }

  m_gotSize = true;
  m_size = static_cast<UINT64>(size);
  return m_size;
}

void *IVSHMEM::GetMemory() {
  if (!m_initialized)
    return NULL;

  if (m_gotMemory)
    return m_memory;

// this if define can be removed later once everyone is un the latest version
// old versions of the IVSHMEM driver ignore the input argument, as such this
// is completely backwards compatible
#if defined(IVSHMEM_CACHE_WRITECOMBINED)
  IVSHMEM_MMAP_CONFIG config;
  config.cacheMode = IVSHMEM_CACHE_WRITECOMBINED;
#endif

  IVSHMEM_MMAP map;
  ZeroMemory(&map, sizeof(IVSHMEM_MMAP));
  if (!DeviceIoControl(m_handle, IOCTL_IVSHMEM_REQUEST_MMAP,
#if defined(IVSHMEM_CACHE_WRITECOMBINED)
                       &config, sizeof(IVSHMEM_MMAP_CONFIG),
#else
                       NULL, 0,
#endif
                       &map, sizeof(IVSHMEM_MMAP), NULL, NULL)) {
    BOOST_LOG(error) << "GetMemory DeviceIoControl Failed: " << GetLastError();
    return NULL;
  }

  m_gotSize = true;
  m_gotMemory = true;
  m_size = static_cast<UINT64>(map.size);
  m_memory = map.ptr;

  return m_memory;
}

HANDLE IVSHMEM::getHandle() {
  return m_handle;
}

void copy_to_packet(MediaPacket *packet, void *data, size_t size) {
  memcpy(packet->data + packet->size, data, size);
  packet->size += size;
}
void copy_to_dpacket(DataPacket *packet, void *data, size_t size) {
  memcpy(packet->data + packet->size, data, size);
  packet->size += size;
}

SharedMemory::SharedMemory(const char *name, size_t size)
    : m_size(size), m_handle(NULL), m_memory(NULL), m_initialized(false) {
  memset(m_name, 0, 512);
  if (name) {
    strncpy(m_name, name, 511);
  }
}

SharedMemory::~SharedMemory() {
  DeInitialize();
}

bool SharedMemory::Initialize() {
  if (m_initialized)
    DeInitialize();

  m_handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, m_name);
  if (m_handle == NULL) {
    BOOST_LOG(error) << "OpenFileMappingA failed for " << m_name << ": " << GetLastError();
    return false;
  }

  m_memory = MapViewOfFile(m_handle, FILE_MAP_ALL_ACCESS, 0, 0, m_size);
  if (m_memory == NULL) {
    BOOST_LOG(error) << "MapViewOfFile failed: " << GetLastError();
    CloseHandle(m_handle);
    m_handle = NULL;
    return false;
  }

  m_initialized = true;
  return true;
}

void SharedMemory::DeInitialize() {
  if (m_memory) {
    UnmapViewOfFile(m_memory);
    m_memory = NULL;
  }
  if (m_handle) {
    CloseHandle(m_handle);
    m_handle = NULL;
  }
  m_initialized = false;
}

size_t SharedMemory::GetSize() {
  return m_size;
}

void *SharedMemory::GetMemory() {
  return m_memory;
}