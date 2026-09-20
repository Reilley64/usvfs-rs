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
#pragma once

#include "mods_abi.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODS_USVFS_FRAME_HEADER_SIZE_V1 UINT32_C(88)
#define MODS_USVFS_FRAME_MAX_SIZE_V1 UINT32_C(65536)
#define MODS_USVFS_EVENT_MAX_PAYLOAD_SIZE_V1 UINT32_C(4096)
#define MODS_USVFS_HANDSHAKE_PAYLOAD_SIZE_V1 UINT32_C(98)
#define MODS_USVFS_FRAME_HEADER_V1_STRUCT_SIZE UINT32_C(96)

#define MODS_USVFS_MESSAGE_HANDSHAKE UINT16_C(1)
#define MODS_USVFS_MESSAGE_MUTATION_REQUEST UINT16_C(2)
#define MODS_USVFS_MESSAGE_MUTATION_COMPLETION UINT16_C(3)
#define MODS_USVFS_MESSAGE_DELTA_PUBLISHED UINT16_C(4)
#define MODS_USVFS_MESSAGE_HEALTH_EVENT UINT16_C(5)
#define MODS_USVFS_MESSAGE_FATAL_EVENT UINT16_C(6)
#define MODS_USVFS_MESSAGE_DESCENDANT_EVENT UINT16_C(7)

typedef struct mods_usvfs_frame_header_v1
{
  uint32_t struct_size;
  uint32_t abi_version;
  uint16_t protocol_version;
  uint16_t message_type;
  uint32_t frame_length;
  uint32_t payload_length;
  uint8_t execution_id[16];
  uint64_t request_id;
  uint8_t provider_plan_hash[32];
  uint64_t expected_delta_version;
  uint64_t deadline;
} mods_usvfs_frame_header_v1;

/**
 * Validates and decodes a bounded IPC frame header. The caller owns both buffers.
 * `frame` is borrowed only for the duration of this call. No pointer is retained.
 */
MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_validate_frame_v1(
    const uint8_t* frame, uint32_t frame_size,
    mods_usvfs_frame_header_v1* header);

#ifdef __cplusplus
}
#endif
