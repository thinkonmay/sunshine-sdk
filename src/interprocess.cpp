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

#include <windows.h>
#include <SetupAPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "interprocess.h"

#include "logging.h"

IVSHMEM * IVSHMEM::m_instance = NULL;

IVSHMEM::IVSHMEM(char* path) :
  m_initialized(false),
  m_handle(INVALID_HANDLE_VALUE),
  m_gotSize(false),
  m_gotMemory(false)
{
  memset(m_devPath,0,512);
  memcpy(m_devPath,path,strlen(path));
}

IVSHMEM::~IVSHMEM()
{
  DeInitialize();
}

bool IVSHMEM::Initialize()
{
  if (m_initialized)
    DeInitialize();

  m_handle = CreateFileA(m_devPath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
  if (m_handle == INVALID_HANDLE_VALUE)
  {
    BOOST_LOG(error) << "CreateFile returned INVALID_HANDLE_VALUE";
    return false;
  }

  m_initialized = true;
  return m_initialized;
}

void IVSHMEM::DeInitialize()
{
  if (!m_initialized)
    return;

  if (m_gotMemory)
  {
    if (!DeviceIoControl(m_handle, IOCTL_IVSHMEM_RELEASE_MMAP, NULL, 0, NULL, 0, NULL, NULL))
      BOOST_LOG(error) << "Deintialize DeviceIoControl failed: " <<  (int)GetLastError();
    m_memory = NULL;
  }

  if (m_handle != INVALID_HANDLE_VALUE)
    CloseHandle(m_handle);

  m_initialized = false;
  m_handle      = INVALID_HANDLE_VALUE;
  m_gotSize     = false;
  m_gotMemory   = false;
}

UINT64 IVSHMEM::GetSize()
{
  if (!m_initialized)
    return 0;

  if (m_gotSize)
    return m_size;

  IVSHMEM_SIZE size;
  if (!DeviceIoControl(m_handle, IOCTL_IVSHMEM_REQUEST_SIZE, NULL, 0, &size, sizeof(IVSHMEM_SIZE), NULL, NULL))
  {
    BOOST_LOG(error) << "GetSize DeviceIoControl Failed: " << GetLastError();
    return 0;
  }

  m_gotSize = true;
  m_size    = static_cast<UINT64>(size);
  return m_size;
}




void * IVSHMEM::GetMemory()
{
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
  if (!DeviceIoControl(
    m_handle,
    IOCTL_IVSHMEM_REQUEST_MMAP,
#if defined(IVSHMEM_CACHE_WRITECOMBINED)
    &config, sizeof(IVSHMEM_MMAP_CONFIG),
#else
    NULL   , 0,
#endif
    &map   , sizeof(IVSHMEM_MMAP       ),
    NULL, NULL))
  {
    BOOST_LOG(error) << "GetMemory DeviceIoControl Failed: " << GetLastError();
    return NULL;
  }

  m_gotSize    = true;
  m_gotMemory  = true;
  m_size       = static_cast<UINT64>(map.size   );
  m_memory     = map.ptr;

  return m_memory;
}



HANDLE IVSHMEM::getHandle()
{
    return m_handle;
}

void
copy_to_packet(MediaPacket* packet,void* data, size_t size) {
  memcpy(packet->data+packet->size,data,size);
  packet->size += size;
}
void
copy_to_dpacket(DataPacket* packet,void* data, size_t size) {
  memcpy(packet->data+packet->size,data,size);
  packet->size += size;
}