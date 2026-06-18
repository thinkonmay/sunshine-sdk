#include <gtest/gtest.h>

#include "ivshmem_protocol.h"
#include "smemory.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

TEST(IvshmemProtocolConstants, QueueAndPacketSizes) {
  EXPECT_EQ(ivshmem_protocol::kInQueueSize, 8);
  EXPECT_EQ(ivshmem_protocol::kOutQueueSize, 8);
  EXPECT_EQ(ivshmem_protocol::kMaxDisplay, 3);
  EXPECT_EQ(ivshmem_protocol::kMediaPacketSize, 5u * 1024u * 1024u);
  EXPECT_EQ(ivshmem_protocol::kDataPacketSize, 5u * 1024u);
  EXPECT_EQ(ivshmem_protocol::kTagSize, 32u * 1024u);
  EXPECT_EQ(ivshmem_protocol::kVideoPacketHeaderSize, 17u);
}

TEST(IvshmemProtocolConstants, StructLayout) {
  EXPECT_EQ(sizeof(MediaPacket::data), MEDIA_PACKET_SIZE);
  EXPECT_EQ(sizeof(DataPacket::data), DATA_PACKET_SIZE);
  EXPECT_EQ(sizeof(DisplayQueue::metadata), sizeof(QueueMetadata));
  EXPECT_GT(sizeof(MediaMemory), sizeof(DisplayQueue) * MAX_DISPLAY);
}

TEST(IvshmemProtocolQueue, AdvanceIndexWraps) {
  EXPECT_EQ(ivshmem_protocol::advance_index(0, IN_QUEUE_SIZE), 1);
  EXPECT_EQ(ivshmem_protocol::advance_index(IN_QUEUE_SIZE - 1, IN_QUEUE_SIZE), 0);
}

TEST(IvshmemProtocolPacket, AppendMediaPacketSegments) {
  auto packet = std::make_unique<MediaPacket>();
  ivshmem_protocol::reset_packet(packet.get());

  std::uint64_t findex = 42;
  std::uint64_t duration = 3000;
  std::uint8_t flags = 0x3;
  const char payload[] = "encoded-nal-unit";

  ivshmem_protocol::append_to_packet(packet.get(), &findex, sizeof(findex));
  ivshmem_protocol::append_to_packet(packet.get(), &duration, sizeof(duration));
  ivshmem_protocol::append_to_packet(packet.get(), &flags, sizeof(flags));
  ivshmem_protocol::append_to_packet(packet.get(), payload, sizeof(payload) - 1);

  ASSERT_EQ(packet->size, 17 + static_cast<int>(sizeof(payload) - 1));

  std::uint64_t out_findex {};
  std::uint64_t out_duration {};
  std::uint8_t out_flags {};
  std::memcpy(&out_findex, packet->data, sizeof(out_findex));
  std::memcpy(&out_duration, packet->data + sizeof(out_findex), sizeof(out_duration));
  std::memcpy(&out_flags, packet->data + sizeof(out_findex) + sizeof(out_duration), sizeof(out_flags));

  EXPECT_EQ(out_findex, findex);
  EXPECT_EQ(out_duration, duration);
  EXPECT_EQ(out_flags, flags);
  EXPECT_STREQ(packet->data + 17, payload);
}

TEST(IvshmemProtocolPacket, LegacyCopyHelpersMatchAppend) {
  auto media = std::make_unique<MediaPacket>();
  auto data = std::make_unique<DataPacket>();

  const char chunk[] = "ivshmem";

  copy_to_packet(media.get(), (void *)chunk, sizeof(chunk) - 1);
  ivshmem_protocol::append_to_packet(data.get(), chunk, sizeof(chunk) - 1);

  EXPECT_EQ(media->size, static_cast<int>(sizeof(chunk) - 1));
  EXPECT_EQ(data->size, static_cast<int>(sizeof(chunk) - 1));
  EXPECT_EQ(std::memcmp(media->data, chunk, sizeof(chunk) - 1), 0);
  EXPECT_EQ(std::memcmp(data->data, chunk, sizeof(chunk) - 1), 0);
}

TEST(IvshmemProtocolPacket, VideoPayloadCapacityGuard) {
  EXPECT_TRUE(ivshmem_protocol::video_payload_fits(MEDIA_PACKET_SIZE -
                                                   ivshmem_protocol::kVideoPacketHeaderSize));
  EXPECT_FALSE(ivshmem_protocol::video_payload_fits(MEDIA_PACKET_SIZE));
}

TEST(IvshmemProtocolQueue, MediaQueueRingUsesIncomingIndex) {
  // MediaQueue is ~40 MiB; keep it off the stack.
  auto queue = std::make_unique<MediaQueue>();
  queue->inindex = 0;
  queue->outindex = 0;

  for (int i = 0; i < IN_QUEUE_SIZE - 1; ++i) {
    ivshmem_protocol::reset_packet(&queue->incoming[queue->inindex]);
    const std::uint64_t frame = static_cast<std::uint64_t>(i);
    ivshmem_protocol::append_to_packet(&queue->incoming[queue->inindex], &frame, sizeof(frame));
    queue->inindex = ivshmem_protocol::advance_index(queue->inindex, IN_QUEUE_SIZE);
  }

  EXPECT_EQ(queue->inindex, IN_QUEUE_SIZE - 1);

  std::vector<std::uint64_t> seen;
  int read_index = 0;
  for (int i = 0; i < IN_QUEUE_SIZE - 1; ++i) {
    const auto &block = queue->incoming[read_index];
    std::uint64_t frame {};
    std::memcpy(&frame, block.data, sizeof(frame));
    seen.push_back(frame);
    read_index = ivshmem_protocol::advance_index(read_index, IN_QUEUE_SIZE);
  }

  ASSERT_EQ(seen.size(), static_cast<std::size_t>(IN_QUEUE_SIZE - 1));
  for (int i = 0; i < IN_QUEUE_SIZE - 1; ++i) {
    EXPECT_EQ(seen[i], static_cast<std::uint64_t>(i));
  }
}

} // namespace
