/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
#include <mods_protocol.h>

#include <cstring>

static_assert(sizeof(mods_usvfs_frame_header_v1) ==
              MODS_USVFS_FRAME_HEADER_V1_STRUCT_SIZE);

namespace
{
uint16_t readU16(const uint8_t* bytes)
{
  return static_cast<uint16_t>(bytes[0]) |
         static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t readU32(const uint8_t* bytes)
{
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t readU64(const uint8_t* bytes)
{
  return static_cast<uint64_t>(readU32(bytes)) |
         (static_cast<uint64_t>(readU32(bytes + 4)) << 32);
}
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_validate_frame_v1(const uint8_t* frame, uint32_t frame_size,
                             mods_usvfs_frame_header_v1* header)
{
  if (frame == nullptr || header == nullptr) {
    return MODS_USVFS_RESULT_INVALID_ARGUMENT;
  }
  if (header->struct_size < sizeof(*header)) {
    return MODS_USVFS_RESULT_STRUCT_TOO_SMALL;
  }
  if (header->abi_version != MODS_USVFS_ABI_VERSION_V1) {
    return MODS_USVFS_RESULT_UNSUPPORTED_ABI_VERSION;
  }
  if (frame_size > MODS_USVFS_FRAME_MAX_SIZE_V1) {
    return MODS_USVFS_RESULT_FRAME_TOO_LARGE;
  }
  if (frame_size < MODS_USVFS_FRAME_HEADER_SIZE_V1) {
    return MODS_USVFS_RESULT_TRUNCATED_FRAME;
  }
  if (std::memcmp(frame, "MVFS", 4) != 0) {
    return MODS_USVFS_RESULT_INVALID_FRAME;
  }

  const uint16_t protocolVersion = readU16(frame + 4);
  if (protocolVersion != MODS_USVFS_PROTOCOL_VERSION_V1) {
    return MODS_USVFS_RESULT_UNSUPPORTED_PROTOCOL_VERSION;
  }
  const uint16_t messageType = readU16(frame + 6);
  if (messageType < MODS_USVFS_MESSAGE_HANDSHAKE ||
      messageType > MODS_USVFS_MESSAGE_DESCENDANT_EVENT) {
    return MODS_USVFS_RESULT_UNKNOWN_MESSAGE_TYPE;
  }

  const uint32_t declaredFrameSize = readU32(frame + 8);
  const uint32_t payloadSize       = readU32(frame + 12);
  if (declaredFrameSize != frame_size ||
      payloadSize != frame_size - MODS_USVFS_FRAME_HEADER_SIZE_V1) {
    return MODS_USVFS_RESULT_INVALID_FRAME;
  }
  if (messageType == MODS_USVFS_MESSAGE_HANDSHAKE &&
      payloadSize != MODS_USVFS_HANDSHAKE_PAYLOAD_SIZE_V1) {
    return MODS_USVFS_RESULT_INVALID_FRAME;
  }
  if (messageType >= MODS_USVFS_MESSAGE_HEALTH_EVENT &&
      payloadSize > MODS_USVFS_EVENT_MAX_PAYLOAD_SIZE_V1) {
    return MODS_USVFS_RESULT_PAYLOAD_TOO_LARGE;
  }

  const uint64_t requestId = readU64(frame + 32);
  const uint64_t deadline  = readU64(frame + 80);
  if (messageType == MODS_USVFS_MESSAGE_MUTATION_REQUEST &&
      (requestId == 0 || deadline == 0)) {
    return MODS_USVFS_RESULT_INVALID_FRAME;
  }

  header->struct_size      = sizeof(*header);
  header->abi_version      = MODS_USVFS_ABI_VERSION_V1;
  header->protocol_version = protocolVersion;
  header->message_type     = messageType;
  header->frame_length     = declaredFrameSize;
  header->payload_length   = payloadSize;
  std::memcpy(header->execution_id, frame + 16, sizeof(header->execution_id));
  header->request_id = requestId;
  std::memcpy(header->provider_plan_hash, frame + 40,
              sizeof(header->provider_plan_hash));
  header->expected_delta_version = readU64(frame + 72);
  header->deadline               = deadline;
  return MODS_USVFS_RESULT_OK;
}
