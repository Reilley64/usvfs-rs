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

#include <stdint.h>

#if defined(_WIN32)
#if defined(BUILDING_USVFS_DLL)
#define MODS_USVFS_API __declspec(dllexport)
#else
#define MODS_USVFS_API __declspec(dllimport)
#endif
#define MODS_USVFS_CALL __cdecl
#else
#define MODS_USVFS_API
#define MODS_USVFS_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define MODS_USVFS_ABI_VERSION_V1 UINT32_C(1)
#define MODS_USVFS_PROTOCOL_VERSION_V1 UINT32_C(1)
#define MODS_USVFS_FORK_REVISION_V1 UINT32_C(1)
#define MODS_USVFS_HOOK_MANIFEST_VERSION_V1 UINT32_C(1)
#define MODS_USVFS_MANDATORY_HOOK_COUNT_V1 UINT32_C(50)
#define MODS_USVFS_HEALTH_V1_STRUCT_SIZE UINT32_C(48)
#define MODS_USVFS_HOOK_STATUS_V1_STRUCT_SIZE UINT32_C(20)

  typedef uint32_t mods_usvfs_result;
#define MODS_USVFS_RESULT_OK UINT32_C(0)
#define MODS_USVFS_RESULT_INVALID_ARGUMENT UINT32_C(1)
#define MODS_USVFS_RESULT_STRUCT_TOO_SMALL UINT32_C(2)
#define MODS_USVFS_RESULT_UNSUPPORTED_ABI_VERSION UINT32_C(3)
#define MODS_USVFS_RESULT_NOT_INITIALIZED UINT32_C(4)
#define MODS_USVFS_RESULT_HOOK_ID_OUT_OF_RANGE UINT32_C(5)
#define MODS_USVFS_RESULT_HOOK_HANDSHAKE_INCOMPLETE UINT32_C(6)
#define MODS_USVFS_RESULT_FRAME_TOO_LARGE UINT32_C(7)
#define MODS_USVFS_RESULT_TRUNCATED_FRAME UINT32_C(8)
#define MODS_USVFS_RESULT_INVALID_FRAME UINT32_C(9)
#define MODS_USVFS_RESULT_UNSUPPORTED_PROTOCOL_VERSION UINT32_C(10)
#define MODS_USVFS_RESULT_UNKNOWN_MESSAGE_TYPE UINT32_C(11)
#define MODS_USVFS_RESULT_PAYLOAD_TOO_LARGE UINT32_C(12)

  typedef uint32_t mods_usvfs_architecture;
#define MODS_USVFS_ARCHITECTURE_X86 UINT32_C(1)
#define MODS_USVFS_ARCHITECTURE_X64 UINT32_C(2)

  typedef uint64_t mods_usvfs_capability_flags;
#define MODS_USVFS_CAPABILITY_FAIL_CLOSED_DESCENDANT_HANDSHAKE (UINT64_C(1) << 0)
#define MODS_USVFS_CAPABILITY_NO_BLACKLIST_BYPASS (UINT64_C(1) << 1)
#define MODS_USVFS_CAPABILITY_MOHIDDEN_ORDINARY_FILE (UINT64_C(1) << 2)
#define MODS_USVFS_CAPABILITY_EXACT_MANDATORY_HOOK_MANIFEST (UINT64_C(1) << 3)
/* Defined for negotiation but not advertised until their behavior is implemented. */
#define MODS_USVFS_CAPABILITY_VIRTUAL_OPEN_REGISTRY (UINT64_C(1) << 4)
#define MODS_USVFS_CAPABILITY_STEAM_COPY_ON_WRITE (UINT64_C(1) << 5)
#define MODS_USVFS_CAPABILITY_PROVIDER_OWNED_TOMBSTONES (UINT64_C(1) << 6)
#define MODS_USVFS_CAPABILITY_DURABLE_MUTATION_DELTAS (UINT64_C(1) << 7)
#define MODS_USVFS_CAPABILITY_BOUNDED_EVENT_CHANNEL (UINT64_C(1) << 8)
#define MODS_USVFS_CAPABILITIES_V1                                                     \
  (MODS_USVFS_CAPABILITY_FAIL_CLOSED_DESCENDANT_HANDSHAKE |                            \
   MODS_USVFS_CAPABILITY_NO_BLACKLIST_BYPASS |                                         \
   MODS_USVFS_CAPABILITY_MOHIDDEN_ORDINARY_FILE |                                      \
   MODS_USVFS_CAPABILITY_EXACT_MANDATORY_HOOK_MANIFEST)

  typedef uint32_t mods_usvfs_hook_id;
#define MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_EX_A UINT32_C(0)
#define MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_A UINT32_C(1)
#define MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_EX_W UINT32_C(2)
#define MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_W UINT32_C(3)
#define MODS_USVFS_HOOK_SET_FILE_ATTRIBUTES_W UINT32_C(4)
#define MODS_USVFS_HOOK_CREATE_DIRECTORY_W UINT32_C(5)
#define MODS_USVFS_HOOK_REMOVE_DIRECTORY_W UINT32_C(6)
#define MODS_USVFS_HOOK_DELETE_FILE_W UINT32_C(7)
#define MODS_USVFS_HOOK_GET_CURRENT_DIRECTORY_A UINT32_C(8)
#define MODS_USVFS_HOOK_GET_CURRENT_DIRECTORY_W UINT32_C(9)
#define MODS_USVFS_HOOK_SET_CURRENT_DIRECTORY_A UINT32_C(10)
#define MODS_USVFS_HOOK_SET_CURRENT_DIRECTORY_W UINT32_C(11)
#define MODS_USVFS_HOOK_EXIT_PROCESS UINT32_C(12)
#define MODS_USVFS_HOOK_CREATE_PROCESS_INTERNAL_W UINT32_C(13)
#define MODS_USVFS_HOOK_MOVE_FILE_A UINT32_C(14)
#define MODS_USVFS_HOOK_MOVE_FILE_W UINT32_C(15)
#define MODS_USVFS_HOOK_MOVE_FILE_EX_A UINT32_C(16)
#define MODS_USVFS_HOOK_MOVE_FILE_EX_W UINT32_C(17)
#define MODS_USVFS_HOOK_MOVE_FILE_WITH_PROGRESS_A UINT32_C(18)
#define MODS_USVFS_HOOK_MOVE_FILE_WITH_PROGRESS_W UINT32_C(19)
#define MODS_USVFS_HOOK_COPY_FILE_EX_W UINT32_C(20)
#define MODS_USVFS_HOOK_COPY_FILE_2 UINT32_C(21)
#define MODS_USVFS_HOOK_GET_PRIVATE_PROFILE_STRING_A UINT32_C(22)
#define MODS_USVFS_HOOK_GET_PRIVATE_PROFILE_STRING_W UINT32_C(23)
#define MODS_USVFS_HOOK_GET_PRIVATE_PROFILE_SECTION_A UINT32_C(24)
#define MODS_USVFS_HOOK_GET_PRIVATE_PROFILE_SECTION_W UINT32_C(25)
#define MODS_USVFS_HOOK_WRITE_PRIVATE_PROFILE_STRING_A UINT32_C(26)
#define MODS_USVFS_HOOK_WRITE_PRIVATE_PROFILE_STRING_W UINT32_C(27)
#define MODS_USVFS_HOOK_GET_FULL_PATH_NAME_A UINT32_C(28)
#define MODS_USVFS_HOOK_GET_FULL_PATH_NAME_W UINT32_C(29)
#define MODS_USVFS_HOOK_FIND_FIRST_FILE_EX_W UINT32_C(30)
#define MODS_USVFS_HOOK_NT_QUERY_FULL_ATTRIBUTES_FILE UINT32_C(31)
#define MODS_USVFS_HOOK_NT_QUERY_ATTRIBUTES_FILE UINT32_C(32)
#define MODS_USVFS_HOOK_NT_QUERY_DIRECTORY_FILE UINT32_C(33)
#define MODS_USVFS_HOOK_NT_QUERY_DIRECTORY_FILE_EX UINT32_C(34)
#define MODS_USVFS_HOOK_NT_QUERY_OBJECT UINT32_C(35)
#define MODS_USVFS_HOOK_NT_QUERY_INFORMATION_FILE UINT32_C(36)
#define MODS_USVFS_HOOK_NT_QUERY_INFORMATION_BY_NAME UINT32_C(37)
#define MODS_USVFS_HOOK_NT_OPEN_FILE UINT32_C(38)
#define MODS_USVFS_HOOK_NT_CREATE_FILE UINT32_C(39)
#define MODS_USVFS_HOOK_NT_CLOSE UINT32_C(40)
#define MODS_USVFS_HOOK_NT_TERMINATE_PROCESS UINT32_C(41)
#define MODS_USVFS_HOOK_LOAD_LIBRARY_EX_A UINT32_C(42)
#define MODS_USVFS_HOOK_LOAD_LIBRARY_EX_W UINT32_C(43)
#define MODS_USVFS_HOOK_GET_MODULE_FILE_NAME_A UINT32_C(44)
#define MODS_USVFS_HOOK_GET_MODULE_FILE_NAME_W UINT32_C(45)
/* These interception seams do not imply the separate mutation capabilities. */
#define MODS_USVFS_HOOK_NT_SET_INFORMATION_FILE UINT32_C(46)
#define MODS_USVFS_HOOK_SET_FILE_INFORMATION_BY_HANDLE UINT32_C(47)
#define MODS_USVFS_HOOK_DUPLICATE_HANDLE UINT32_C(48)
#define MODS_USVFS_HOOK_CREATE_FILE_MAPPING_W UINT32_C(49)

  typedef uint32_t mods_usvfs_hook_install_status;
#define MODS_USVFS_HOOK_INSTALL_NOT_ATTEMPTED UINT32_C(0)
#define MODS_USVFS_HOOK_INSTALL_SUCCEEDED UINT32_C(1)
#define MODS_USVFS_HOOK_INSTALL_FAILED UINT32_C(2)

  typedef uint32_t mods_usvfs_hook_probe_status;
#define MODS_USVFS_HOOK_PROBE_NOT_RUN UINT32_C(0)
#define MODS_USVFS_HOOK_PROBE_PASSED UINT32_C(1)
#define MODS_USVFS_HOOK_PROBE_FAILED UINT32_C(2)

  /* Caller sets struct_size and abi_version before each call. */
  typedef struct mods_usvfs_health_v1
  {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t protocol_version;
    uint32_t fork_revision;
    uint32_t architecture;
    uint32_t mandatory_hook_count;
    uint32_t hook_manifest_version;
    uint64_t capability_flags;
    uint32_t installed_hook_count;
    uint32_t passed_probe_count;
  } mods_usvfs_health_v1;

  /* Hook IDs are stable indexes into the ABI v1 mandatory manifest. */
  typedef struct mods_usvfs_hook_status_v1
  {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t hook_id;
    uint32_t install_status;
    uint32_t probe_status;
  } mods_usvfs_hook_status_v1;

  /*
   * Static handshake fields are filled even when the result is NOT_INITIALIZED.
   * A result of OK means all mandatory hooks were installed and probed.
   */
  MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
  mods_usvfs_get_health_v1(mods_usvfs_health_v1* health);

  /* Returns the bounded install and probe states for one mandatory hook. */
  MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL mods_usvfs_get_hook_status_v1(
      mods_usvfs_hook_id hook_id, mods_usvfs_hook_status_v1* hook_status);

#ifdef __cplusplus
}
#endif
