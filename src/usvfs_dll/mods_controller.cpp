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
#include <mods_controller.h>

#include <usvfs.h>
#include <windows_sane.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
template <typename T>
mods_usvfs_result validateStruct(const T* value)
{
  if (value == nullptr) {
    return MODS_USVFS_RESULT_INVALID_ARGUMENT;
  }
  if (value->struct_size < sizeof(T)) {
    return MODS_USVFS_RESULT_STRUCT_TOO_SMALL;
  }
  if (value->abi_version != MODS_USVFS_ABI_VERSION_V1) {
    return MODS_USVFS_RESULT_UNSUPPORTED_ABI_VERSION;
  }
  return MODS_USVFS_RESULT_OK;
}

bool validBytes(const void* bytes, uint32_t length)
{
  return length == 0 || bytes != nullptr;
}

std::string utf8(const uint16_t* value, uint32_t length)
{
  if (length == 0) {
    return {};
  }
  const int required = ::WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS,
      reinterpret_cast<const wchar_t*>(value), static_cast<int>(length), nullptr,
      0, nullptr, nullptr);
  if (required <= 0) {
    throw std::runtime_error("invalid UTF-16 execution instance name");
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  if (::WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS,
          reinterpret_cast<const wchar_t*>(value), static_cast<int>(length),
          result.data(), required, nullptr, nullptr) != required) {
    throw std::runtime_error("failed to encode execution instance name");
  }
  return result;
}

struct QueuedMessage
{
  mods_usvfs_message_kind kind;
  uint64_t requestId;
  uint64_t expectedDeltaVersion;
  uint64_t deadline;
  std::vector<uint8_t> payload;
};

enum class MutationState
{
  Queued,
  Delivered,
};

struct MutationRecord
{
  MutationState state;
  uint64_t deadline;
};

struct CompletedDelta
{
  uint64_t requestId;
  uint64_t version;
  uint64_t deadline;
  std::vector<uint8_t> bytes;
};

std::mutex activeMutex;
mods_usvfs_execution_v1* activeExecution = nullptr;

mods_usvfs_result translateException()
{
  try {
    throw;
  } catch (const std::bad_alloc&) {
    return MODS_USVFS_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    return MODS_USVFS_RESULT_NATIVE_FAILURE;
  }
}
}  // namespace

struct mods_usvfs_execution_v1
{
  std::mutex mutex;
  uint8_t executionId[16]{};
  uint8_t providerPlanHash[32]{};
  std::vector<uint8_t> providerPlan;
  std::u16string controllerDirectory;
  usvfsParameters* parameters{nullptr};
  uint32_t channelMessageCapacity{0};
  uint32_t channelByteCapacity{0};
  uint32_t queuedBytes{0};
  uint64_t currentDeltaVersion{0};
  uint64_t lastRequestId{0};
  bool hasLastRequest{false};
  bool providerPlanLoaded{false};
  bool rootInjected{false};
  bool poisoned{false};
  mods_usvfs_result fatalResult{MODS_USVFS_RESULT_OK};
  uint64_t fatalRequestId{0};
  std::vector<uint8_t> fatalPayload;
  std::deque<QueuedMessage> messages;
  std::unordered_map<uint64_t, MutationRecord> mutations;
  std::optional<uint64_t> lastCompletedRequestId;
  std::optional<CompletedDelta> completedDelta;
};

namespace
{
mods_usvfs_result validateExecutionLocked(mods_usvfs_execution_v1* execution)
{
  if (execution == nullptr || execution != activeExecution) {
    return MODS_USVFS_RESULT_INVALID_ARGUMENT;
  }
  return MODS_USVFS_RESULT_OK;
}

mods_usvfs_result validateIdentity(const mods_usvfs_execution_v1& execution,
                                   const uint8_t* executionId,
                                   const uint8_t* providerPlanHash)
{
  if (std::memcmp(execution.executionId, executionId,
                  sizeof(execution.executionId)) != 0) {
    return MODS_USVFS_RESULT_EXECUTION_MISMATCH;
  }
  if (!execution.providerPlanLoaded) {
    return MODS_USVFS_RESULT_INVALID_STATE;
  }
  if (std::memcmp(execution.providerPlanHash, providerPlanHash,
                  sizeof(execution.providerPlanHash)) != 0) {
    return MODS_USVFS_RESULT_PROVIDER_PLAN_MISMATCH;
  }
  return MODS_USVFS_RESULT_OK;
}

mods_usvfs_result poison(mods_usvfs_execution_v1& execution,
                          mods_usvfs_result result,
                          uint64_t requestId = 0)
{
  if (!execution.poisoned) {
    execution.poisoned       = true;
    execution.fatalResult    = result;
    execution.fatalRequestId = requestId;
  }
  return result;
}

std::optional<uint64_t> expiredRequest(
    const mods_usvfs_execution_v1& execution, uint64_t now)
{
  if (execution.completedDelta.has_value() &&
      execution.completedDelta->deadline <= now) {
    return execution.completedDelta->requestId;
  }
  const auto expired = std::find_if(
      execution.mutations.begin(), execution.mutations.end(),
      [now](const auto& entry) { return entry.second.deadline <= now; });
  if (expired != execution.mutations.end()) {
    return expired->first;
  }
  return std::nullopt;
}

mods_usvfs_message_kind messageKind(uint16_t messageType)
{
  switch (messageType) {
  case MODS_USVFS_MESSAGE_MUTATION_REQUEST:
    return MODS_USVFS_MESSAGE_KIND_MUTATION;
  case MODS_USVFS_MESSAGE_HEALTH_EVENT:
    return MODS_USVFS_MESSAGE_KIND_HEALTH;
  case MODS_USVFS_MESSAGE_FATAL_EVENT:
    return MODS_USVFS_MESSAGE_KIND_FATAL;
  case MODS_USVFS_MESSAGE_DESCENDANT_EVENT:
    return MODS_USVFS_MESSAGE_KIND_DESCENDANT;
  default:
    return 0;
  }
}
}  // namespace

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_create_execution_v1(const mods_usvfs_execution_config_v1* config,
                               mods_usvfs_execution_v1** execution)
{
  try {
    if (execution == nullptr) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }
    *execution = nullptr;
    const mods_usvfs_result validation = validateStruct(config);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if (config->instance_name == nullptr || config->instance_name_length == 0 ||
        config->instance_name_length > MODS_USVFS_MAX_INSTANCE_NAME_UTF16_V1 ||
        (config->controller_directory_length != 0 &&
         config->controller_directory == nullptr) ||
        config->channel_message_capacity == 0 ||
        config->channel_message_capacity > MODS_USVFS_MAX_CHANNEL_MESSAGES_V1 ||
        config->channel_byte_capacity == 0 ||
        config->channel_byte_capacity > MODS_USVFS_MAX_CHANNEL_BYTES_V1 ||
        std::find(config->instance_name,
                  config->instance_name + config->instance_name_length,
                  uint16_t{0}) !=
            config->instance_name + config->instance_name_length) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> activeLock(activeMutex);
    if (activeExecution != nullptr) {
      return MODS_USVFS_RESULT_ALREADY_EXISTS;
    }

    auto created = std::make_unique<mods_usvfs_execution_v1>();
    std::memcpy(created->executionId, config->execution_id,
                sizeof(created->executionId));
    created->channelMessageCapacity = config->channel_message_capacity;
    created->channelByteCapacity    = config->channel_byte_capacity;
    created->currentDeltaVersion    = config->initial_delta_version;
    created->controllerDirectory.reserve(config->controller_directory_length);
    for (uint32_t index = 0; index < config->controller_directory_length;
         ++index) {
      created->controllerDirectory.push_back(
          static_cast<char16_t>(config->controller_directory[index]));
    }

    const auto freeParameters = [](usvfsParameters* parameters) {
      if (parameters != nullptr) {
        usvfsFreeParameters(parameters);
      }
    };
    std::unique_ptr<usvfsParameters, decltype(freeParameters)> parameters(
        usvfsCreateParameters(), freeParameters);
    if (parameters == nullptr) {
      return MODS_USVFS_RESULT_OUT_OF_MEMORY;
    }
    const std::string instanceName =
        utf8(config->instance_name, config->instance_name_length);
    /* usvfs reserves four bytes for the inverse shared-memory prefix. */
    if (instanceName.size() > 59) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }
    usvfsSetInstanceName(parameters.get(), instanceName.c_str());
    if (!usvfsCreateVFS(parameters.get())) {
      return MODS_USVFS_RESULT_NATIVE_FAILURE;
    }
    created->parameters = parameters.release();

    activeExecution = created.release();
    *execution      = activeExecution;
    return MODS_USVFS_RESULT_OK;
  } catch (...) {
    return translateException();
  }
}

extern "C" MODS_USVFS_API void MODS_USVFS_CALL
mods_usvfs_destroy_execution_v1(mods_usvfs_execution_v1* execution)
{
  try {
    std::lock_guard<std::mutex> activeLock(activeMutex);
    if (execution == nullptr || execution != activeExecution) {
      return;
    }
    activeExecution = nullptr;
    try {
      usvfsDisconnectVFS();
    } catch (...) {
    }
    if (execution->parameters != nullptr) {
      try {
        usvfsFreeParameters(execution->parameters);
      } catch (...) {
      }
      execution->parameters = nullptr;
    }
    delete execution;
  } catch (...) {
  }
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_load_provider_plan_v1(mods_usvfs_execution_v1* execution,
                                 const mods_usvfs_provider_plan_v1* plan)
{
  try {
    const mods_usvfs_result validation = validateStruct(plan);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if (!validBytes(plan->bytes, plan->byte_length) || plan->byte_length == 0 ||
        plan->byte_length > MODS_USVFS_MAX_PROVIDER_PLAN_SIZE_V1) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> activeLock(activeMutex);
    const mods_usvfs_result executionValidation = validateExecutionLocked(execution);
    if (executionValidation != MODS_USVFS_RESULT_OK) {
      return executionValidation;
    }
    std::lock_guard<std::mutex> lock(execution->mutex);
    if (execution->poisoned) {
      return MODS_USVFS_RESULT_INVALID_STATE;
    }
    if (execution->providerPlanLoaded) {
      return MODS_USVFS_RESULT_PROVIDER_PLAN_ALREADY_LOADED;
    }
    execution->providerPlan.assign(plan->bytes, plan->bytes + plan->byte_length);
    std::memcpy(execution->providerPlanHash, plan->sha256,
                sizeof(execution->providerPlanHash));
    execution->providerPlanLoaded = true;
    return MODS_USVFS_RESULT_OK;
  } catch (...) {
    return translateException();
  }
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_inject_root_v1(mods_usvfs_execution_v1* execution,
                          const mods_usvfs_root_process_v1* root)
{
  try {
    const mods_usvfs_result validation = validateStruct(root);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if (root->process_handle == 0 || root->primary_thread_handle == 0 ||
        root->process_handle == static_cast<uintptr_t>(-1) ||
        root->primary_thread_handle == static_cast<uintptr_t>(-1)) {
      return MODS_USVFS_RESULT_INVALID_NATIVE_HANDLE;
    }

    std::lock_guard<std::mutex> activeLock(activeMutex);
    const mods_usvfs_result executionValidation = validateExecutionLocked(execution);
    if (executionValidation != MODS_USVFS_RESULT_OK) {
      return executionValidation;
    }
    std::lock_guard<std::mutex> lock(execution->mutex);
    if (!execution->providerPlanLoaded) {
      return MODS_USVFS_RESULT_INVALID_STATE;
    }
    if (execution->poisoned) {
      return MODS_USVFS_RESULT_INVALID_STATE;
    }
    if (execution->rootInjected) {
      return MODS_USVFS_RESULT_ROOT_ALREADY_INJECTED;
    }

    /*
     * The legacy bootstrap cannot carry the Provider Plan identity or a bounded
     * event channel. Calling it here would resume an incompletely attached root.
     */
    return MODS_USVFS_RESULT_UNSUPPORTED_OPERATION;
  } catch (...) {
    return translateException();
  }
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_accept_frame_v1(mods_usvfs_execution_v1* execution,
                           const mods_usvfs_frame_input_v1* input)
{
  try {
    const mods_usvfs_result validation = validateStruct(input);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if (!validBytes(input->frame, input->frame_length)) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> activeLock(activeMutex);
    const mods_usvfs_result executionValidation = validateExecutionLocked(execution);
    if (executionValidation != MODS_USVFS_RESULT_OK) {
      return executionValidation;
    }
    std::lock_guard<std::mutex> lock(execution->mutex);
    if (execution->poisoned) {
      return MODS_USVFS_RESULT_INVALID_STATE;
    }
    if (const auto expired =
            expiredRequest(*execution, input->now_monotonic_ticks)) {
      return poison(*execution, MODS_USVFS_RESULT_DEADLINE_EXPIRED,
                    *expired);
    }

    mods_usvfs_frame_header_v1 header{};
    header.struct_size = sizeof(header);
    header.abi_version = MODS_USVFS_ABI_VERSION_V1;
    const mods_usvfs_result frameResult = mods_usvfs_validate_frame_v1(
        input->frame, input->frame_length, &header);
    if (frameResult != MODS_USVFS_RESULT_OK) {
      return poison(*execution, frameResult);
    }
    const mods_usvfs_message_kind kind = messageKind(header.message_type);
    if (kind == 0) {
      return poison(*execution, MODS_USVFS_RESULT_INVALID_FRAME,
                    header.request_id);
    }

    const mods_usvfs_result identity = validateIdentity(
        *execution, header.execution_id, header.provider_plan_hash);
    if (identity != MODS_USVFS_RESULT_OK) {
      return poison(*execution, identity, header.request_id);
    }
    if (kind == MODS_USVFS_MESSAGE_KIND_MUTATION &&
        header.deadline <= input->now_monotonic_ticks) {
      return poison(*execution, MODS_USVFS_RESULT_DEADLINE_EXPIRED,
                    header.request_id);
    }
    if (header.expected_delta_version != execution->currentDeltaVersion) {
      return poison(*execution, MODS_USVFS_RESULT_UNEXPECTED_DELTA_VERSION,
                    header.request_id);
    }
    if (kind == MODS_USVFS_MESSAGE_KIND_FATAL) {
      try {
        const uint8_t* payload =
            input->frame + MODS_USVFS_FRAME_HEADER_SIZE_V1;
        execution->fatalPayload.assign(payload,
                                       payload + header.payload_length);
      } catch (const std::bad_alloc&) {
        return poison(*execution, MODS_USVFS_RESULT_OUT_OF_MEMORY,
                      header.request_id);
      }
      poison(*execution, MODS_USVFS_RESULT_NATIVE_FAILURE,
             header.request_id);
      return MODS_USVFS_RESULT_OK;
    }
    if (kind == MODS_USVFS_MESSAGE_KIND_MUTATION) {
      if (execution->hasLastRequest) {
        if (header.request_id == execution->lastRequestId) {
          return poison(*execution, MODS_USVFS_RESULT_DUPLICATE_REQUEST,
                        header.request_id);
        }
        if (header.request_id < execution->lastRequestId) {
          return poison(*execution, MODS_USVFS_RESULT_STALE_REQUEST,
                        header.request_id);
        }
      }
    }
    if (execution->messages.size() >= execution->channelMessageCapacity ||
        (kind == MODS_USVFS_MESSAGE_KIND_MUTATION &&
         execution->mutations.size() >= execution->channelMessageCapacity) ||
        header.payload_length >
            execution->channelByteCapacity - execution->queuedBytes) {
      return poison(*execution, MODS_USVFS_RESULT_CHANNEL_FULL,
                    header.request_id);
    }

    try {
      const uint8_t* payload = input->frame + MODS_USVFS_FRAME_HEADER_SIZE_V1;
      QueuedMessage queued{
          kind,
          header.request_id,
          header.expected_delta_version,
          header.deadline,
          std::vector<uint8_t>(payload, payload + header.payload_length)};
      if (kind == MODS_USVFS_MESSAGE_KIND_MUTATION) {
        const auto [mutation, inserted] = execution->mutations.try_emplace(
            header.request_id,
            MutationRecord{MutationState::Queued, header.deadline});
        if (!inserted) {
          return poison(*execution, MODS_USVFS_RESULT_DUPLICATE_REQUEST,
                        header.request_id);
        }
        try {
          execution->messages.push_back(std::move(queued));
        } catch (...) {
          execution->mutations.erase(mutation);
          throw;
        }
        execution->lastRequestId  = header.request_id;
        execution->hasLastRequest = true;
      } else {
        execution->messages.push_back(std::move(queued));
      }
      execution->queuedBytes += header.payload_length;
    } catch (const std::bad_alloc&) {
      return poison(*execution, MODS_USVFS_RESULT_OUT_OF_MEMORY,
                    header.request_id);
    } catch (...) {
      return poison(*execution, MODS_USVFS_RESULT_NATIVE_FAILURE,
                    header.request_id);
    }
    return MODS_USVFS_RESULT_OK;
  } catch (...) {
    return translateException();
  }
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_poll_message_v1(mods_usvfs_execution_v1* execution,
                           mods_usvfs_message_v1* message)
{
  try {
    const mods_usvfs_result validation = validateStruct(message);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if (!validBytes(message->payload, message->payload_capacity)) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> activeLock(activeMutex);
    const mods_usvfs_result executionValidation = validateExecutionLocked(execution);
    if (executionValidation != MODS_USVFS_RESULT_OK) {
      return executionValidation;
    }
    std::lock_guard<std::mutex> lock(execution->mutex);
    if (!execution->poisoned) {
      if (const auto expired =
              expiredRequest(*execution, message->now_monotonic_ticks)) {
        poison(*execution, MODS_USVFS_RESULT_DEADLINE_EXPIRED, *expired);
      }
    }

    if (execution->poisoned) {
      message->message_kind = MODS_USVFS_MESSAGE_KIND_FATAL;
      message->reserved     = 0;
      std::memcpy(message->execution_id, execution->executionId,
                  sizeof(message->execution_id));
      message->request_id = execution->fatalRequestId;
      std::memcpy(message->provider_plan_hash, execution->providerPlanHash,
                  sizeof(message->provider_plan_hash));
      message->expected_delta_version = execution->currentDeltaVersion;
      message->deadline               = 0;

      if (!execution->fatalPayload.empty()) {
        message->payload_length =
            static_cast<uint32_t>(execution->fatalPayload.size());
        if (message->payload_capacity < execution->fatalPayload.size()) {
          return MODS_USVFS_RESULT_BUFFER_TOO_SMALL;
        }
        std::memcpy(message->payload, execution->fatalPayload.data(),
                    execution->fatalPayload.size());
      } else {
        message->payload_length = sizeof(uint32_t);
        if (message->payload_capacity < sizeof(uint32_t)) {
          return MODS_USVFS_RESULT_BUFFER_TOO_SMALL;
        }
        const uint32_t result = execution->fatalResult;
        message->payload[0]   = static_cast<uint8_t>(result);
        message->payload[1]   = static_cast<uint8_t>(result >> 8);
        message->payload[2]   = static_cast<uint8_t>(result >> 16);
        message->payload[3]   = static_cast<uint8_t>(result >> 24);
      }
      return MODS_USVFS_RESULT_OK;
    }

    if (execution->messages.empty()) {
      message->payload_length = 0;
      return MODS_USVFS_RESULT_CHANNEL_EMPTY;
    }

    const QueuedMessage& queued = execution->messages.front();
    message->message_kind       = queued.kind;
    message->reserved           = 0;
    std::memcpy(message->execution_id, execution->executionId,
                sizeof(message->execution_id));
    message->request_id = queued.requestId;
    std::memcpy(message->provider_plan_hash, execution->providerPlanHash,
                sizeof(message->provider_plan_hash));
    message->expected_delta_version = queued.expectedDeltaVersion;
    message->deadline               = queued.deadline;
    message->payload_length = static_cast<uint32_t>(queued.payload.size());
    if (message->payload_capacity < queued.payload.size()) {
      return MODS_USVFS_RESULT_BUFFER_TOO_SMALL;
    }
    if (!queued.payload.empty()) {
      std::memcpy(message->payload, queued.payload.data(), queued.payload.size());
    }
    if (queued.kind == MODS_USVFS_MESSAGE_KIND_MUTATION) {
      auto mutation = execution->mutations.find(queued.requestId);
      if (mutation == execution->mutations.end() ||
          mutation->second.state != MutationState::Queued) {
        return poison(*execution, MODS_USVFS_RESULT_INVALID_STATE,
                      queued.requestId);
      }
      mutation->second.state = MutationState::Delivered;
    }
    execution->queuedBytes -= static_cast<uint32_t>(queued.payload.size());
    execution->messages.pop_front();
    return MODS_USVFS_RESULT_OK;
  } catch (...) {
    return translateException();
  }
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_complete_mutation_v1(
    mods_usvfs_execution_v1* execution,
    const mods_usvfs_mutation_completion_v1* completion)
{
  try {
    const mods_usvfs_result validation = validateStruct(completion);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if (!validBytes(completion->delta, completion->delta_length) ||
        completion->delta_length >
            MODS_USVFS_FRAME_MAX_SIZE_V1 - MODS_USVFS_FRAME_HEADER_SIZE_V1) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> activeLock(activeMutex);
    const mods_usvfs_result executionValidation = validateExecutionLocked(execution);
    if (executionValidation != MODS_USVFS_RESULT_OK) {
      return executionValidation;
    }
    std::lock_guard<std::mutex> lock(execution->mutex);
    if (execution->poisoned) {
      return MODS_USVFS_RESULT_INVALID_STATE;
    }
    if (const auto expired =
            expiredRequest(*execution, completion->now_monotonic_ticks)) {
      return poison(*execution, MODS_USVFS_RESULT_DEADLINE_EXPIRED,
                    *expired);
    }
    const mods_usvfs_result identity = validateIdentity(
        *execution, completion->execution_id, completion->provider_plan_hash);
    if (identity != MODS_USVFS_RESULT_OK) {
      return poison(*execution, identity, completion->request_id);
    }
    if (execution->lastCompletedRequestId == completion->request_id) {
      return poison(*execution, MODS_USVFS_RESULT_MUTATION_ALREADY_COMPLETED,
                    completion->request_id);
    }
    const auto mutation = execution->mutations.find(completion->request_id);
    if (mutation == execution->mutations.end() ||
        mutation->second.state != MutationState::Delivered) {
      return poison(*execution, MODS_USVFS_RESULT_MUTATION_NOT_PENDING,
                    completion->request_id);
    }
    if (mutation->second.deadline <= completion->now_monotonic_ticks) {
      return poison(*execution, MODS_USVFS_RESULT_DEADLINE_EXPIRED,
                    completion->request_id);
    }

    std::optional<CompletedDelta> completedDelta;
    if (completion->result == MODS_USVFS_RESULT_OK) {
      if (execution->completedDelta.has_value()) {
        return poison(*execution, MODS_USVFS_RESULT_INVALID_STATE,
                      completion->request_id);
      }
      if (execution->currentDeltaVersion ==
          std::numeric_limits<uint64_t>::max()) {
        return poison(*execution, MODS_USVFS_RESULT_DELTA_VERSION_OVERFLOW,
                      completion->request_id);
      }
      if (completion->delta_version != execution->currentDeltaVersion + 1) {
        return poison(*execution,
                      MODS_USVFS_RESULT_UNEXPECTED_DELTA_VERSION,
                      completion->request_id);
      }
      try {
        completedDelta.emplace(CompletedDelta{
            completion->request_id, completion->delta_version,
            mutation->second.deadline,
            std::vector<uint8_t>(completion->delta,
                                 completion->delta + completion->delta_length)});
      } catch (const std::bad_alloc&) {
        return poison(*execution, MODS_USVFS_RESULT_OUT_OF_MEMORY,
                      completion->request_id);
      }
    } else if (completion->delta_length != 0 ||
               completion->delta_version != execution->currentDeltaVersion) {
      return poison(*execution, MODS_USVFS_RESULT_INVALID_ARGUMENT,
                    completion->request_id);
    }

    if (completedDelta.has_value()) {
      execution->completedDelta.emplace(std::move(*completedDelta));
    }
    execution->mutations.erase(mutation);
    execution->lastCompletedRequestId = completion->request_id;
    return MODS_USVFS_RESULT_OK;
  } catch (...) {
    return translateException();
  }
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_publish_delta_v1(mods_usvfs_execution_v1* execution,
                            const mods_usvfs_delta_v1* delta)
{
  try {
    const mods_usvfs_result validation = validateStruct(delta);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if (!validBytes(delta->bytes, delta->byte_length) ||
        delta->byte_length >
            MODS_USVFS_FRAME_MAX_SIZE_V1 - MODS_USVFS_FRAME_HEADER_SIZE_V1) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> activeLock(activeMutex);
    const mods_usvfs_result executionValidation = validateExecutionLocked(execution);
    if (executionValidation != MODS_USVFS_RESULT_OK) {
      return executionValidation;
    }
    std::lock_guard<std::mutex> lock(execution->mutex);
    if (execution->poisoned) {
      return MODS_USVFS_RESULT_INVALID_STATE;
    }
    if (const auto expired =
            expiredRequest(*execution, delta->now_monotonic_ticks)) {
      return poison(*execution, MODS_USVFS_RESULT_DEADLINE_EXPIRED,
                    *expired);
    }
    const mods_usvfs_result identity = validateIdentity(
        *execution, delta->execution_id, delta->provider_plan_hash);
    if (identity != MODS_USVFS_RESULT_OK) {
      return poison(*execution, identity, delta->request_id);
    }
    if (execution->currentDeltaVersion ==
        std::numeric_limits<uint64_t>::max()) {
      return poison(*execution, MODS_USVFS_RESULT_DELTA_VERSION_OVERFLOW,
                    delta->request_id);
    }
    if (delta->delta_version != execution->currentDeltaVersion + 1) {
      return poison(*execution, MODS_USVFS_RESULT_UNEXPECTED_DELTA_VERSION,
                    delta->request_id);
    }
    if (!execution->completedDelta.has_value()) {
      return poison(*execution, MODS_USVFS_RESULT_MUTATION_NOT_PENDING,
                    delta->request_id);
    }
    const CompletedDelta& completed = *execution->completedDelta;
    if (completed.deadline <= delta->now_monotonic_ticks) {
      return poison(*execution, MODS_USVFS_RESULT_DEADLINE_EXPIRED,
                    delta->request_id);
    }
    if (completed.requestId != delta->request_id ||
        completed.version != delta->delta_version ||
        completed.bytes.size() != delta->byte_length ||
        (!completed.bytes.empty() &&
         std::memcmp(completed.bytes.data(), delta->bytes,
                     completed.bytes.size()) != 0)) {
      return poison(*execution, MODS_USVFS_RESULT_INVALID_ARGUMENT,
                    delta->request_id);
    }

    /*
     * Transport fan-out is deliberately not represented as a capability until
     * every attached hook consumes this state. This ordered commit is the only
     * point at which the controller advances its protocol version.
     */
    execution->currentDeltaVersion = delta->delta_version;
    execution->completedDelta.reset();
    return MODS_USVFS_RESULT_OK;
  } catch (...) {
    return translateException();
  }
}

extern "C" MODS_USVFS_API mods_usvfs_result MODS_USVFS_CALL
mods_usvfs_query_attached_pids_v1(mods_usvfs_execution_v1* execution,
                                  mods_usvfs_pid_buffer_v1* buffer)
{
  try {
    const mods_usvfs_result validation = validateStruct(buffer);
    if (validation != MODS_USVFS_RESULT_OK) {
      return validation;
    }
    if ((buffer->capacity != 0 && buffer->pids == nullptr)) {
      return MODS_USVFS_RESULT_INVALID_ARGUMENT;
    }
    buffer->required_count = 0;

    std::lock_guard<std::mutex> activeLock(activeMutex);
    const mods_usvfs_result executionValidation = validateExecutionLocked(execution);
    if (executionValidation != MODS_USVFS_RESULT_OK) {
      return executionValidation;
    }

    size_t count = 0;
    if (!usvfsGetVFSProcessList(&count, nullptr) ||
        count > std::numeric_limits<uint32_t>::max()) {
      return MODS_USVFS_RESULT_NATIVE_FAILURE;
    }
    std::vector<DWORD> pids(count);
    size_t available = pids.size();
    if (available != 0 &&
        !usvfsGetVFSProcessList(&available, pids.data())) {
      return MODS_USVFS_RESULT_NATIVE_FAILURE;
    }
    if (available > pids.size()) {
      buffer->required_count = static_cast<uint32_t>(available);
      return MODS_USVFS_RESULT_BUFFER_TOO_SMALL;
    }

    buffer->required_count = static_cast<uint32_t>(available);
    if (buffer->capacity < available) {
      return MODS_USVFS_RESULT_BUFFER_TOO_SMALL;
    }
    for (std::size_t index = 0; index < available; ++index) {
      buffer->pids[index] = static_cast<uint32_t>(pids[index]);
    }
    return MODS_USVFS_RESULT_OK;
  } catch (...) {
    return translateException();
  }
}
