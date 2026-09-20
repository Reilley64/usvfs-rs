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
#include "hookmanager.h"

#include <mods_abi.h>

static_assert(sizeof(mods_usvfs_health_v1) == MODS_USVFS_HEALTH_V1_STRUCT_SIZE);
static_assert(sizeof(mods_usvfs_hook_status_v1) ==
              MODS_USVFS_HOOK_STATUS_V1_STRUCT_SIZE);

namespace
{
template <typename T>
mods_usvfs_result validateOutput(const T* output)
{
  if (output == nullptr) {
    return MODS_USVFS_RESULT_INVALID_ARGUMENT;
  }
  if (static_cast<std::size_t>(output->struct_size) < sizeof(T)) {
    return MODS_USVFS_RESULT_STRUCT_TOO_SMALL;
  }
  if (output->abi_version != MODS_USVFS_ABI_VERSION_V1) {
    return MODS_USVFS_RESULT_UNSUPPORTED_ABI_VERSION;
  }
  return MODS_USVFS_RESULT_OK;
}

constexpr mods_usvfs_architecture currentArchitecture()
{
#if defined(_M_IX86)
  return MODS_USVFS_ARCHITECTURE_X86;
#elif defined(_M_X64)
  return MODS_USVFS_ARCHITECTURE_X64;
#else
#error "The mods usvfs ABI supports only x86 and x64 builds"
#endif
}
}  // namespace

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_get_health_v1(mods_usvfs_health_v1* health)
{
  const mods_usvfs_result validation = validateOutput(health);
  if (validation != MODS_USVFS_RESULT_OK) {
    return validation;
  }

  health->struct_size           = static_cast<uint32_t>(sizeof(*health));
  health->abi_version           = MODS_USVFS_ABI_VERSION_V1;
  health->protocol_version      = MODS_USVFS_PROTOCOL_VERSION_V1;
  health->fork_revision         = MODS_USVFS_FORK_REVISION_V1;
  health->architecture          = currentArchitecture();
  health->mandatory_hook_count  = MODS_USVFS_MANDATORY_HOOK_COUNT_V1;
  health->hook_manifest_version = MODS_USVFS_HOOK_MANIFEST_VERSION_V1;
  health->capability_flags      = MODS_USVFS_CAPABILITIES_V1;
  health->installed_hook_count  = 0;
  health->passed_probe_count    = 0;

  const usvfs::HookManager* manager = usvfs::HookManager::instanceIfPresent();
  if (manager == nullptr) {
    return MODS_USVFS_RESULT_NOT_INITIALIZED;
  }

  health->installed_hook_count = static_cast<uint32_t>(manager->installedHookCount());
  health->passed_probe_count   = static_cast<uint32_t>(manager->passedProbeCount());
  if (health->installed_hook_count != MODS_USVFS_MANDATORY_HOOK_COUNT_V1 ||
      health->passed_probe_count != MODS_USVFS_MANDATORY_HOOK_COUNT_V1) {
    return MODS_USVFS_RESULT_HOOK_HANDSHAKE_INCOMPLETE;
  }
  return MODS_USVFS_RESULT_OK;
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_get_hook_status_v1(mods_usvfs_hook_id hook_id,
                              mods_usvfs_hook_status_v1* hook_status)
{
  const mods_usvfs_result validation = validateOutput(hook_status);
  if (validation != MODS_USVFS_RESULT_OK) {
    return validation;
  }
  if (hook_id >= MODS_USVFS_MANDATORY_HOOK_COUNT_V1) {
    return MODS_USVFS_RESULT_HOOK_ID_OUT_OF_RANGE;
  }

  hook_status->struct_size    = static_cast<uint32_t>(sizeof(*hook_status));
  hook_status->abi_version    = MODS_USVFS_ABI_VERSION_V1;
  hook_status->hook_id        = hook_id;
  hook_status->install_status = MODS_USVFS_HOOK_INSTALL_NOT_ATTEMPTED;
  hook_status->probe_status   = MODS_USVFS_HOOK_PROBE_NOT_RUN;

  const usvfs::HookManager* manager = usvfs::HookManager::instanceIfPresent();
  if (manager == nullptr) {
    return MODS_USVFS_RESULT_NOT_INITIALIZED;
  }

  if (manager->hookInstallationAttempted(hook_id)) {
    hook_status->install_status = manager->hookInstallationSucceeded(hook_id)
                                      ? MODS_USVFS_HOOK_INSTALL_SUCCEEDED
                                      : MODS_USVFS_HOOK_INSTALL_FAILED;
  }
  if (manager->hookProbeRun(hook_id)) {
    hook_status->probe_status = manager->hookProbePassed(hook_id)
                                    ? MODS_USVFS_HOOK_PROBE_PASSED
                                    : MODS_USVFS_HOOK_PROBE_FAILED;
  }
  return MODS_USVFS_RESULT_OK;
}
