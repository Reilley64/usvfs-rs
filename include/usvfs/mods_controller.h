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

#include "mods_protocol.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODS_USVFS_EXECUTION_CONFIG_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_execution_config_v1))
#define MODS_USVFS_PROVIDER_PLAN_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_provider_plan_v1))
#define MODS_USVFS_ROOT_PROCESS_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_root_process_v1))
#define MODS_USVFS_FRAME_INPUT_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_frame_input_v1))
#define MODS_USVFS_MESSAGE_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_message_v1))
#define MODS_USVFS_MUTATION_COMPLETION_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_mutation_completion_v1))
#define MODS_USVFS_DELTA_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_delta_v1))
#define MODS_USVFS_PID_BUFFER_V1_STRUCT_SIZE ((uint32_t)sizeof(mods_usvfs_pid_buffer_v1))

#define MODS_USVFS_MAX_INSTANCE_NAME_UTF16_V1 UINT32_C(64)
#define MODS_USVFS_MAX_PROVIDER_PLAN_SIZE_V1 (UINT32_C(64) * UINT32_C(1024) * UINT32_C(1024))
#define MODS_USVFS_MAX_CHANNEL_MESSAGES_V1 UINT32_C(1024)
#define MODS_USVFS_MAX_CHANNEL_BYTES_V1 (UINT32_C(64) * UINT32_C(1024) * UINT32_C(1024))

/* Additional controller results. Existing result values remain stable. */
#define MODS_USVFS_RESULT_ALREADY_EXISTS UINT32_C(13)
#define MODS_USVFS_RESULT_OUT_OF_MEMORY UINT32_C(14)
#define MODS_USVFS_RESULT_CHANNEL_EMPTY UINT32_C(15)
#define MODS_USVFS_RESULT_BUFFER_TOO_SMALL UINT32_C(16)
#define MODS_USVFS_RESULT_INVALID_STATE UINT32_C(17)
#define MODS_USVFS_RESULT_EXECUTION_MISMATCH UINT32_C(18)
#define MODS_USVFS_RESULT_PROVIDER_PLAN_MISMATCH UINT32_C(19)
#define MODS_USVFS_RESULT_DUPLICATE_REQUEST UINT32_C(20)
#define MODS_USVFS_RESULT_STALE_REQUEST UINT32_C(21)
#define MODS_USVFS_RESULT_UNEXPECTED_DELTA_VERSION UINT32_C(22)
#define MODS_USVFS_RESULT_DELTA_VERSION_OVERFLOW UINT32_C(23)
#define MODS_USVFS_RESULT_CHANNEL_FULL UINT32_C(24)
#define MODS_USVFS_RESULT_DEADLINE_EXPIRED UINT32_C(25)
#define MODS_USVFS_RESULT_NATIVE_FAILURE UINT32_C(26)
#define MODS_USVFS_RESULT_MUTATION_NOT_PENDING UINT32_C(27)
#define MODS_USVFS_RESULT_MUTATION_ALREADY_COMPLETED UINT32_C(28)
#define MODS_USVFS_RESULT_INVALID_NATIVE_HANDLE UINT32_C(29)
#define MODS_USVFS_RESULT_ROOT_ALREADY_INJECTED UINT32_C(30)
#define MODS_USVFS_RESULT_PROVIDER_PLAN_ALREADY_LOADED UINT32_C(31)
#define MODS_USVFS_RESULT_UNSUPPORTED_OPERATION UINT32_C(32)

/* Opaque. The DLL owns the allocation; destroy it with destroy_execution_v1. */
typedef struct mods_usvfs_execution_v1 mods_usvfs_execution_v1;

/*
 * All pointer fields are borrowed for one call. No pointer is retained.
 * UTF-16 lengths are code-unit counts and exclude a terminator.
 */
typedef struct mods_usvfs_execution_config_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint8_t execution_id[16];
  const uint16_t* instance_name;
  uint32_t instance_name_length;
  const uint16_t* controller_directory;
  uint32_t controller_directory_length;
  uint32_t channel_message_capacity;
  uint32_t channel_byte_capacity;
  uint64_t initial_delta_version;
} mods_usvfs_execution_config_v1;

typedef struct mods_usvfs_provider_plan_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  const uint8_t* bytes;
  uint32_t byte_length;
  uint8_t sha256[32];
} mods_usvfs_provider_plan_v1;

/* process_handle and primary_thread_handle are borrowed and are never closed. */
typedef struct mods_usvfs_root_process_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uintptr_t process_handle;
  uintptr_t primary_thread_handle;
} mods_usvfs_root_process_v1;

/*
 * The injected-to-controller transport supplies one complete MutationRequest,
 * HealthEvent, FatalEvent, or DescendantEvent frame. Handshake and
 * controller-to-injected frames are directionally invalid here. Frame storage
 * remains caller-owned. Wire payload schemas contain no process-local pointer,
 * callback, or raw HANDLE.
 */
typedef struct mods_usvfs_frame_input_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  const uint8_t* frame;
  uint32_t frame_length;
  uint64_t now_monotonic_ticks;
} mods_usvfs_frame_input_v1;

typedef uint32_t mods_usvfs_message_kind;
#define MODS_USVFS_MESSAGE_KIND_MUTATION UINT32_C(1)
#define MODS_USVFS_MESSAGE_KIND_HEALTH UINT32_C(2)
#define MODS_USVFS_MESSAGE_KIND_FATAL UINT32_C(3)
#define MODS_USVFS_MESSAGE_KIND_DESCENDANT UINT32_C(4)

/*
 * payload is caller-owned. On BUFFER_TOO_SMALL, payload_length is the required
 * size and the message remains queued. On OK, exactly payload_length bytes were
 * copied and the message was removed. A poisoned execution instead returns its
 * terminal Fatal message on every poll, so overflow cannot drop the failure.
 * No callback or allocated output is used.
 */
typedef struct mods_usvfs_message_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t message_kind;
  uint32_t reserved;
  uint8_t execution_id[16];
  uint64_t request_id;
  uint8_t provider_plan_hash[32];
  uint64_t expected_delta_version;
  uint64_t deadline;
  /* Caller supplies the current monotonic time on each poll. */
  uint64_t now_monotonic_ticks;
  uint8_t* payload;
  uint32_t payload_capacity;
  uint32_t payload_length;
} mods_usvfs_message_v1;

/*
 * A successful completion has result == OK, delta_version == current + 1, and
 * may carry a bounded delta. An error completion has no delta and leaves the
 * current version unchanged. Delta bytes are borrowed for this call.
 */
typedef struct mods_usvfs_mutation_completion_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint8_t execution_id[16];
  uint64_t request_id;
  uint8_t provider_plan_hash[32];
  mods_usvfs_result result;
  uint32_t mapped_system_error;
  uint64_t delta_version;
  uint64_t now_monotonic_ticks;
  const uint8_t* delta;
  uint32_t delta_length;
} mods_usvfs_mutation_completion_v1;

/* A published delta must be exactly current + 1 and match a completed request. */
typedef struct mods_usvfs_delta_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint8_t execution_id[16];
  uint64_t request_id;
  uint8_t provider_plan_hash[32];
  uint64_t delta_version;
  uint64_t now_monotonic_ticks;
  const uint8_t* bytes;
  uint32_t byte_length;
} mods_usvfs_delta_v1;

/* pids is caller-owned. required_count is always written after validation. */
typedef struct mods_usvfs_pid_buffer_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t* pids;
  uint32_t capacity;
  uint32_t required_count;
} mods_usvfs_pid_buffer_v1;

MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_create_execution_v1(
    const mods_usvfs_execution_config_v1* config,
    mods_usvfs_execution_v1** execution);

/* Null is accepted. The DLL disconnects the VFS and frees all retained bytes. */
MODS_USVFS_API void MODS_USVFS_CALL
mods_usvfs_destroy_execution_v1(mods_usvfs_execution_v1* execution);

MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_load_provider_plan_v1(
    mods_usvfs_execution_v1* execution,
    const mods_usvfs_provider_plan_v1* plan);

/*
 * This fork revision returns UNSUPPORTED_OPERATION before touching either
 * borrowed handle because Provider Plan attachment and event IPC are not yet
 * connected to injected hooks. This fail-closed result must leave the root
 * suspended. No related capability bit is advertised.
 */
MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_inject_root_v1(
    mods_usvfs_execution_v1* execution,
    const mods_usvfs_root_process_v1* root);

MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_accept_frame_v1(
    mods_usvfs_execution_v1* execution,
    const mods_usvfs_frame_input_v1* input);

MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_poll_message_v1(
    mods_usvfs_execution_v1* execution,
    mods_usvfs_message_v1* message);

MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_complete_mutation_v1(
    mods_usvfs_execution_v1* execution,
    const mods_usvfs_mutation_completion_v1* completion);

MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_publish_delta_v1(
    mods_usvfs_execution_v1* execution,
    const mods_usvfs_delta_v1* delta);

MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_query_attached_pids_v1(
    mods_usvfs_execution_v1* execution,
    mods_usvfs_pid_buffer_v1* buffer);

#ifdef __cplusplus
}
#endif
