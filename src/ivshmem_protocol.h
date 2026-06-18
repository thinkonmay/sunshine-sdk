#pragma once

/**
 * @file src/ivshmem_protocol.h
 * @brief Pure IVSHMEM queue/packet helpers shared by guest encode and host consumers.
 *
 * Layout must stay in sync with worker/proxy/util/memory and worker/daemon/utils/memory
 * CGO definitions and docs/engineering/ci_cd/sunshine_testing_feasibility.md.
 */

#include "smemory.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ivshmem_protocol {

constexpr int kInQueueSize = IN_QUEUE_SIZE;
constexpr int kOutQueueSize = OUT_QUEUE_SIZE;
constexpr int kMaxDisplay = MAX_DISPLAY;
constexpr std::size_t kMediaPacketSize = MEDIA_PACKET_SIZE;
constexpr std::size_t kDataPacketSize = DATA_PACKET_SIZE;
constexpr std::size_t kTagSize = TAG_SIZE;

/** Video packet header written by shmsunshine before encoded payload. */
constexpr std::size_t kVideoPacketHeaderSize =
    sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint8_t);

inline int advance_index(int index, int capacity) {
  int updated = index + 1;
  if (updated >= capacity) {
    return 0;
  }
  return updated;
}

inline void reset_packet(MediaPacket *packet) {
  packet->size = 0;
}

inline void reset_packet(DataPacket *packet) {
  packet->size = 0;
}

inline void append_to_packet(MediaPacket *packet, const void *data, std::size_t size) {
  std::memcpy(packet->data + packet->size, data, size);
  packet->size += static_cast<int>(size);
}

inline void append_to_packet(DataPacket *packet, const void *data, std::size_t size) {
  std::memcpy(packet->data + packet->size, data, size);
  packet->size += static_cast<int>(size);
}

inline bool video_payload_fits(std::size_t payload_size) {
  return payload_size <= (kMediaPacketSize - kVideoPacketHeaderSize);
}

} // namespace ivshmem_protocol

// Legacy C linkage used by interprocess.h and main.cpp.
inline void copy_to_packet(MediaPacket *packet, void *data, size_t size) {
  ivshmem_protocol::append_to_packet(packet, data, size);
}

inline void copy_to_dpacket(DataPacket *packet, void *data, size_t size) {
  ivshmem_protocol::append_to_packet(packet, data, size);
}
