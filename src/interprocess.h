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

#pragma once

#include "smemory.h"
#include <stdbool.h>
#include <windows.h>

class IVSHMEM {
public:
  IVSHMEM(char *path);
  ~IVSHMEM();

  bool Initialize();
  void DeInitialize();
  UINT64 GetSize();
  void *GetMemory();
  HANDLE getHandle();

protected:
private:
  static IVSHMEM *m_instance;

  char m_devPath[512];
  bool m_initialized;
  HANDLE m_handle;
  UINT64 m_size;
  bool m_gotSize;
  void *m_memory;
  bool m_gotMemory;
};

void copy_to_packet(MediaPacket *packet, void *data, size_t size);

void copy_to_dpacket(DataPacket *packet, void *data, size_t size);