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

// this file depends on so many stuff that the easiest way is to include
// pch.h from shared
#include "pch.h"

#include <test_helpers.h>

#include <fstream>
#include <iostream>

#include <inject.h>
#include <stringutils.h>
#include <windows_sane.h>

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <hookcontext.h>
#include <hooks/kernel32.h>
#include <hooks/ntdll.h>
#include <logging.h>
#include <mods_abi.h>
#include <mods_controller.h>
#include <mods_protocol.h>
#include <stringcast.h>
#include <unicodestring.h>
#include <usvfs.h>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

namespace spd = spdlog;

namespace ush = usvfs::shared;

namespace
{
void appendU16(std::vector<uint8_t>& bytes, uint16_t value)
{
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<uint8_t>(value >> shift));
  }
}

void appendU64(std::vector<uint8_t>& bytes, uint64_t value)
{
  for (unsigned int shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<uint8_t>(value >> shift));
  }
}

std::vector<uint8_t> controllerFrame(uint16_t type, const uint8_t* executionId,
                                     uint64_t requestId, const uint8_t* planHash,
                                     uint64_t deltaVersion, uint64_t deadline,
                                     std::vector<uint8_t> payload)
{
  std::vector<uint8_t> frame;
  frame.reserve(MODS_USVFS_FRAME_HEADER_SIZE_V1 + payload.size());
  frame.insert(frame.end(), {'M', 'V', 'F', 'S'});
  appendU16(frame, MODS_USVFS_PROTOCOL_VERSION_V1);
  appendU16(frame, type);
  appendU32(frame, static_cast<uint32_t>(MODS_USVFS_FRAME_HEADER_SIZE_V1 +
                                         payload.size()));
  appendU32(frame, static_cast<uint32_t>(payload.size()));
  frame.insert(frame.end(), executionId, executionId + 16);
  appendU64(frame, requestId);
  frame.insert(frame.end(), planHash, planHash + 32);
  appendU64(frame, deltaVersion);
  appendU64(frame, deadline);
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

class ModsControllerTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    mods_usvfs_execution_config_v1 config{};
    config.struct_size              = sizeof(config);
    config.abi_version              = MODS_USVFS_ABI_VERSION_V1;
    std::fill(std::begin(config.execution_id), std::end(config.execution_id), 0x11);
    static const uint16_t instanceName[] = {'m', 'o', 'd', 's', '-', 'a', 'b', 'i'};
    static const uint16_t directory[]    = {'.'};
    config.instance_name             = instanceName;
    config.instance_name_length      = static_cast<uint32_t>(std::size(instanceName));
    config.controller_directory      = directory;
    config.controller_directory_length = static_cast<uint32_t>(std::size(directory));
    config.channel_message_capacity  = 2;
    config.channel_byte_capacity     = MODS_USVFS_EVENT_MAX_PAYLOAD_SIZE_V1;
    config.initial_delta_version     = 0;

    ASSERT_EQ(MODS_USVFS_RESULT_OK,
              mods_usvfs_create_execution_v1(&config, &execution_));
    std::fill(std::begin(executionId_), std::end(executionId_), 0x11);
    std::fill(std::begin(planHash_), std::end(planHash_), 0x22);
    const std::array<uint8_t, 4> planBytes{1, 2, 3, 4};
    mods_usvfs_provider_plan_v1 plan{};
    plan.struct_size = sizeof(plan);
    plan.abi_version = MODS_USVFS_ABI_VERSION_V1;
    plan.bytes       = planBytes.data();
    plan.byte_length = static_cast<uint32_t>(planBytes.size());
    std::memcpy(plan.sha256, planHash_, sizeof(plan.sha256));
    ASSERT_EQ(MODS_USVFS_RESULT_OK,
              mods_usvfs_load_provider_plan_v1(execution_, &plan));
  }

  void TearDown() override
  {
    mods_usvfs_destroy_execution_v1(execution_);
  }

  mods_usvfs_result accept(const std::vector<uint8_t>& frame,
                           uint64_t now = 1)
  {
    mods_usvfs_frame_input_v1 input{};
    input.struct_size         = sizeof(input);
    input.abi_version         = MODS_USVFS_ABI_VERSION_V1;
    input.frame               = frame.data();
    input.frame_length        = static_cast<uint32_t>(frame.size());
    input.now_monotonic_ticks = now;
    return mods_usvfs_accept_frame_v1(execution_, &input);
  }

  mods_usvfs_execution_v1* execution_{nullptr};
  uint8_t executionId_[16]{};
  uint8_t planHash_[32]{};
};
}  // namespace

TEST(ModsUsvfsControllerAbi, EveryInputAndOutputStructIsVersionPrefixed)
{
#define EXPECT_VERSION_PREFIX(type)                                                \
  static_assert(offsetof(type, struct_size) == 0);                                 \
  static_assert(offsetof(type, abi_version) == sizeof(uint32_t))
  EXPECT_VERSION_PREFIX(mods_usvfs_execution_config_v1);
  EXPECT_VERSION_PREFIX(mods_usvfs_provider_plan_v1);
  EXPECT_VERSION_PREFIX(mods_usvfs_root_process_v1);
  EXPECT_VERSION_PREFIX(mods_usvfs_frame_input_v1);
  EXPECT_VERSION_PREFIX(mods_usvfs_message_v1);
  EXPECT_VERSION_PREFIX(mods_usvfs_mutation_completion_v1);
  EXPECT_VERSION_PREFIX(mods_usvfs_delta_v1);
  EXPECT_VERSION_PREFIX(mods_usvfs_pid_buffer_v1);
#undef EXPECT_VERSION_PREFIX
}

TEST(ModsUsvfsControllerAbi, RejectsUnsupportedVersionsAndUnboundedChannels)
{
  mods_usvfs_execution_v1* execution = nullptr;
  const uint16_t name[] = {'t'};
  mods_usvfs_execution_config_v1 config{};
  config.struct_size             = sizeof(config);
  config.abi_version             = MODS_USVFS_ABI_VERSION_V1 + 1;
  config.execution_id[0]         = 1;
  config.instance_name           = name;
  config.instance_name_length    = 1;
  config.channel_message_capacity = 1;
  config.channel_byte_capacity   = 1;
  EXPECT_EQ(MODS_USVFS_RESULT_UNSUPPORTED_ABI_VERSION,
            mods_usvfs_create_execution_v1(&config, &execution));

  config.abi_version              = MODS_USVFS_ABI_VERSION_V1;
  config.channel_message_capacity = MODS_USVFS_MAX_CHANNEL_MESSAGES_V1 + 1;
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_ARGUMENT,
            mods_usvfs_create_execution_v1(&config, &execution));
  EXPECT_EQ(nullptr, execution);
}

TEST_F(ModsControllerTest, RejectsInvalidRootHandlesAndUsesCallerOwnedPidBuffers)
{
  mods_usvfs_root_process_v1 root{};
  root.struct_size = sizeof(root);
  root.abi_version = MODS_USVFS_ABI_VERSION_V1;
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_NATIVE_HANDLE,
            mods_usvfs_inject_root_v1(execution_, &root));
  root.process_handle        = 1;
  root.primary_thread_handle = 1;
  EXPECT_EQ(MODS_USVFS_RESULT_UNSUPPORTED_OPERATION,
            mods_usvfs_inject_root_v1(execution_, &root));

  mods_usvfs_pid_buffer_v1 pids{};
  pids.struct_size = sizeof(pids);
  pids.abi_version = MODS_USVFS_ABI_VERSION_V1;
  EXPECT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_query_attached_pids_v1(execution_, &pids));
  EXPECT_EQ(0U, pids.required_count);
}

TEST(ModsUsvfsControllerAbi, PoisonBeforePlanPreventsLaterPlanLoading)
{
  mods_usvfs_execution_v1* execution = nullptr;
  const uint16_t name[] = {'p', 'o', 'i', 's', 'o', 'n'};
  mods_usvfs_execution_config_v1 config{};
  config.struct_size              = sizeof(config);
  config.abi_version              = MODS_USVFS_ABI_VERSION_V1;
  config.instance_name            = name;
  config.instance_name_length     = static_cast<uint32_t>(std::size(name));
  config.channel_message_capacity = 1;
  config.channel_byte_capacity    = 16;
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_create_execution_v1(&config, &execution));

  std::array<uint8_t, MODS_USVFS_FRAME_HEADER_SIZE_V1> malformed{};
  mods_usvfs_frame_input_v1 input{};
  input.struct_size  = sizeof(input);
  input.abi_version  = MODS_USVFS_ABI_VERSION_V1;
  input.frame        = malformed.data();
  input.frame_length = static_cast<uint32_t>(malformed.size());
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_FRAME,
            mods_usvfs_accept_frame_v1(execution, &input));

  const uint8_t planByte = 1;
  mods_usvfs_provider_plan_v1 plan{};
  plan.struct_size = sizeof(plan);
  plan.abi_version = MODS_USVFS_ABI_VERSION_V1;
  plan.bytes       = &planByte;
  plan.byte_length = 1;
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_STATE,
            mods_usvfs_load_provider_plan_v1(execution, &plan));
  mods_usvfs_destroy_execution_v1(execution);
}

TEST_F(ModsControllerTest, ProviderPlanIsImmutable)
{
  const std::array<uint8_t, 1> replacement{9};
  mods_usvfs_provider_plan_v1 plan{};
  plan.struct_size = sizeof(plan);
  plan.abi_version = MODS_USVFS_ABI_VERSION_V1;
  plan.bytes       = replacement.data();
  plan.byte_length = static_cast<uint32_t>(replacement.size());
  std::memcpy(plan.sha256, planHash_, sizeof(plan.sha256));

  EXPECT_EQ(MODS_USVFS_RESULT_PROVIDER_PLAN_ALREADY_LOADED,
            mods_usvfs_load_provider_plan_v1(execution_, &plan));
}

TEST_F(ModsControllerTest, IdentityViolationPoisonsAnUnDroppableFatalState)
{
  auto frame = controllerFrame(MODS_USVFS_MESSAGE_HEALTH_EVENT, executionId_, 0,
                               planHash_, 0, 0, {});
  frame[16] ^= 0xff;
  EXPECT_EQ(MODS_USVFS_RESULT_EXECUTION_MISMATCH, accept(frame));

  std::array<uint8_t, 4> fatalPayload{};
  mods_usvfs_message_v1 message{};
  message.struct_size      = sizeof(message);
  message.abi_version      = MODS_USVFS_ABI_VERSION_V1;
  message.payload          = fatalPayload.data();
  message.payload_capacity = static_cast<uint32_t>(fatalPayload.size());
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));
  EXPECT_EQ(MODS_USVFS_MESSAGE_KIND_FATAL, message.message_kind);
  EXPECT_EQ(MODS_USVFS_RESULT_EXECUTION_MISMATCH, fatalPayload[0]);

  fatalPayload.fill(0);
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));
  EXPECT_EQ(MODS_USVFS_RESULT_EXECUTION_MISMATCH, fatalPayload[0]);
}

TEST_F(ModsControllerTest, ProviderPlanMismatchPoisonsExecution)
{
  auto frame = controllerFrame(MODS_USVFS_MESSAGE_HEALTH_EVENT, executionId_, 0,
                               planHash_, 0, 0, {});
  frame[40] ^= 0xff;
  EXPECT_EQ(MODS_USVFS_RESULT_PROVIDER_PLAN_MISMATCH, accept(frame));
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_STATE, accept(frame));
}

TEST_F(ModsControllerTest, PollPreservesMessagesWhenTheCallerBufferIsSmall)
{
  const auto frame = controllerFrame(
      MODS_USVFS_MESSAGE_HEALTH_EVENT, executionId_, 0, planHash_, 0, 0,
      std::vector<uint8_t>(4, 7));
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(frame));

  std::array<uint8_t, 3> tooSmall{};
  mods_usvfs_message_v1 message{};
  message.struct_size      = sizeof(message);
  message.abi_version      = MODS_USVFS_ABI_VERSION_V1;
  message.payload          = tooSmall.data();
  message.payload_capacity = static_cast<uint32_t>(tooSmall.size());
  EXPECT_EQ(MODS_USVFS_RESULT_BUFFER_TOO_SMALL,
            mods_usvfs_poll_message_v1(execution_, &message));
  EXPECT_EQ(4U, message.payload_length);

  std::array<uint8_t, 4> payload{};
  message.payload          = payload.data();
  message.payload_capacity = static_cast<uint32_t>(payload.size());
  EXPECT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));
  EXPECT_EQ(MODS_USVFS_MESSAGE_KIND_HEALTH, message.message_kind);
  EXPECT_EQ((std::array<uint8_t, 4>{7, 7, 7, 7}), payload);
}

TEST_F(ModsControllerTest, FullChannelPoisonsInsteadOfDroppingFatalState)
{
  const auto frame = controllerFrame(MODS_USVFS_MESSAGE_HEALTH_EVENT,
                                     executionId_, 0, planHash_, 0, 0, {});
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(frame));
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(frame));
  EXPECT_EQ(MODS_USVFS_RESULT_CHANNEL_FULL, accept(frame));

  std::array<uint8_t, 4> payload{};
  mods_usvfs_message_v1 message{};
  message.struct_size      = sizeof(message);
  message.abi_version      = MODS_USVFS_ABI_VERSION_V1;
  message.payload          = payload.data();
  message.payload_capacity = static_cast<uint32_t>(payload.size());
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));
  EXPECT_EQ(MODS_USVFS_MESSAGE_KIND_FATAL, message.message_kind);
  EXPECT_EQ(MODS_USVFS_RESULT_CHANNEL_FULL, payload[0]);
}

TEST_F(ModsControllerTest, DeliveredMutationSlotsRemainBounded)
{
  std::array<uint8_t, 1> payload{};
  mods_usvfs_message_v1 message{};
  message.struct_size      = sizeof(message);
  message.abi_version      = MODS_USVFS_ABI_VERSION_V1;
  message.payload          = payload.data();
  message.payload_capacity = static_cast<uint32_t>(payload.size());

  for (uint64_t requestId : {UINT64_C(1), UINT64_C(2)}) {
    const auto request = controllerFrame(
        MODS_USVFS_MESSAGE_MUTATION_REQUEST, executionId_, requestId,
        planHash_, 0, 100, std::vector<uint8_t>{1});
    ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(request));
    ASSERT_EQ(MODS_USVFS_RESULT_OK,
              mods_usvfs_poll_message_v1(execution_, &message));
  }

  const auto overflow = controllerFrame(
      MODS_USVFS_MESSAGE_MUTATION_REQUEST, executionId_, 3, planHash_, 0,
      100, std::vector<uint8_t>{1});
  EXPECT_EQ(MODS_USVFS_RESULT_CHANNEL_FULL, accept(overflow));
}

TEST_F(ModsControllerTest, PollPoisonsAQueuedMutationAfterItsDeadline)
{
  const auto request = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                       executionId_, 1, planHash_, 0, 5,
                                       std::vector<uint8_t>{1});
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(request));

  std::array<uint8_t, 4> payload{};
  mods_usvfs_message_v1 message{};
  message.struct_size         = sizeof(message);
  message.abi_version         = MODS_USVFS_ABI_VERSION_V1;
  message.now_monotonic_ticks = 5;
  message.payload             = payload.data();
  message.payload_capacity    = static_cast<uint32_t>(payload.size());
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));
  EXPECT_EQ(MODS_USVFS_MESSAGE_KIND_FATAL, message.message_kind);
  EXPECT_EQ(MODS_USVFS_RESULT_DEADLINE_EXPIRED, payload[0]);
}

TEST_F(ModsControllerTest, ExpiredMutationPoisonsExecution)
{
  const auto request = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                       executionId_, 10, planHash_, 0, 100,
                                       std::vector<uint8_t>{1});
  EXPECT_EQ(MODS_USVFS_RESULT_DEADLINE_EXPIRED, accept(request, 100));
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_STATE, accept(request, 1));
}

TEST_F(ModsControllerTest, DuplicateMutationReplayPoisonsExecution)
{
  const auto request = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                       executionId_, 10, planHash_, 0, 100,
                                       std::vector<uint8_t>{1});
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(request));
  EXPECT_EQ(MODS_USVFS_RESULT_DUPLICATE_REQUEST, accept(request));
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_STATE, accept(request));
}

TEST_F(ModsControllerTest, StaleMutationReplayPoisonsExecution)
{
  const auto request = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                       executionId_, 10, planHash_, 0, 100,
                                       std::vector<uint8_t>{1});
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(request));
  const auto stale = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                     executionId_, 9, planHash_, 0, 100,
                                     std::vector<uint8_t>{1});
  EXPECT_EQ(MODS_USVFS_RESULT_STALE_REQUEST, accept(stale));
}

TEST_F(ModsControllerTest, ExpiredRequestWinsOverWrongDeltaVersion)
{
  const auto request = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                       executionId_, 1, planHash_, 9, 5,
                                       std::vector<uint8_t>{1});
  EXPECT_EQ(MODS_USVFS_RESULT_DEADLINE_EXPIRED, accept(request, 5));
}

TEST_F(ModsControllerTest, CompletingAnotherRequestDetectsAbandonedWork)
{
  std::array<uint8_t, 1> payload{};
  mods_usvfs_message_v1 message{};
  message.struct_size      = sizeof(message);
  message.abi_version      = MODS_USVFS_ABI_VERSION_V1;
  message.payload          = payload.data();
  message.payload_capacity = static_cast<uint32_t>(payload.size());

  const auto first = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                     executionId_, 1, planHash_, 0, 5,
                                     std::vector<uint8_t>{1});
  const auto second = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                      executionId_, 2, planHash_, 0, 100,
                                      std::vector<uint8_t>{1});
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(first));
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(second));
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));

  mods_usvfs_mutation_completion_v1 completion{};
  completion.struct_size = sizeof(completion);
  completion.abi_version = MODS_USVFS_ABI_VERSION_V1;
  std::memcpy(completion.execution_id, executionId_, sizeof(executionId_));
  completion.request_id = 2;
  std::memcpy(completion.provider_plan_hash, planHash_, sizeof(planHash_));
  completion.result              = MODS_USVFS_RESULT_INVALID_ARGUMENT;
  completion.delta_version       = 0;
  completion.now_monotonic_ticks = 10;
  EXPECT_EQ(MODS_USVFS_RESULT_DEADLINE_EXPIRED,
            mods_usvfs_complete_mutation_v1(execution_, &completion));
}

TEST_F(ModsControllerTest, OutOfOrderCompletionPoisonsExecution)
{
  const auto request = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                       executionId_, 1, planHash_, 0, 100,
                                       std::vector<uint8_t>{9});
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(request));
  std::array<uint8_t, 1> payload{};
  mods_usvfs_message_v1 message{};
  message.struct_size      = sizeof(message);
  message.abi_version      = MODS_USVFS_ABI_VERSION_V1;
  message.payload          = payload.data();
  message.payload_capacity = static_cast<uint32_t>(payload.size());
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));

  mods_usvfs_mutation_completion_v1 completion{};
  completion.struct_size = sizeof(completion);
  completion.abi_version = MODS_USVFS_ABI_VERSION_V1;
  std::memcpy(completion.execution_id, executionId_, sizeof(executionId_));
  completion.request_id = 1;
  std::memcpy(completion.provider_plan_hash, planHash_, sizeof(planHash_));
  completion.result        = MODS_USVFS_RESULT_OK;
  completion.delta_version = 2;
  EXPECT_EQ(MODS_USVFS_RESULT_UNEXPECTED_DELTA_VERSION,
            mods_usvfs_complete_mutation_v1(execution_, &completion));
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_STATE,
            mods_usvfs_complete_mutation_v1(execution_, &completion));
}

TEST_F(ModsControllerTest, PublishesOnlyTheCompletedStrictlyNextDelta)
{
  const auto request = controllerFrame(MODS_USVFS_MESSAGE_MUTATION_REQUEST,
                                       executionId_, 1, planHash_, 0, 100,
                                       std::vector<uint8_t>{9});
  ASSERT_EQ(MODS_USVFS_RESULT_OK, accept(request));

  std::array<uint8_t, 1> requestPayload{};
  mods_usvfs_message_v1 message{};
  message.struct_size      = sizeof(message);
  message.abi_version      = MODS_USVFS_ABI_VERSION_V1;
  message.payload          = requestPayload.data();
  message.payload_capacity = static_cast<uint32_t>(requestPayload.size());
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_poll_message_v1(execution_, &message));

  const std::array<uint8_t, 2> deltaBytes{4, 5};
  mods_usvfs_mutation_completion_v1 completion{};
  completion.struct_size = sizeof(completion);
  completion.abi_version = MODS_USVFS_ABI_VERSION_V1;
  std::memcpy(completion.execution_id, executionId_, sizeof(executionId_));
  completion.request_id = 1;
  std::memcpy(completion.provider_plan_hash, planHash_, sizeof(planHash_));
  completion.result        = MODS_USVFS_RESULT_OK;
  completion.delta_version = 1;
  completion.delta         = deltaBytes.data();
  completion.delta_length  = static_cast<uint32_t>(deltaBytes.size());
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_complete_mutation_v1(execution_, &completion));

  mods_usvfs_delta_v1 delta{};
  delta.struct_size = sizeof(delta);
  delta.abi_version = MODS_USVFS_ABI_VERSION_V1;
  std::memcpy(delta.execution_id, executionId_, sizeof(executionId_));
  delta.request_id = 1;
  std::memcpy(delta.provider_plan_hash, planHash_, sizeof(planHash_));
  delta.delta_version = 1;
  delta.bytes         = deltaBytes.data();
  delta.byte_length   = static_cast<uint32_t>(deltaBytes.size());
  ASSERT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_publish_delta_v1(execution_, &delta));
  EXPECT_EQ(MODS_USVFS_RESULT_UNEXPECTED_DELTA_VERSION,
            mods_usvfs_publish_delta_v1(execution_, &delta));

  const auto currentVersion = controllerFrame(
      MODS_USVFS_MESSAGE_HEALTH_EVENT, executionId_, 0, planHash_, 1, 0, {});
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_STATE, accept(currentVersion));
}

TEST(ModsUsvfsAbi, RejectsInvalidHealthOutput)
{
  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_ARGUMENT, mods_usvfs_get_health_v1(nullptr));

  mods_usvfs_health_v1 tooSmall{};
  tooSmall.struct_size = static_cast<uint32_t>(sizeof(tooSmall) - 1);
  tooSmall.abi_version = MODS_USVFS_ABI_VERSION_V1;
  EXPECT_EQ(MODS_USVFS_RESULT_STRUCT_TOO_SMALL, mods_usvfs_get_health_v1(&tooSmall));

  mods_usvfs_health_v1 unsupported{};
  unsupported.struct_size = static_cast<uint32_t>(sizeof(unsupported));
  unsupported.abi_version = MODS_USVFS_ABI_VERSION_V1 + 1;
  EXPECT_EQ(MODS_USVFS_RESULT_UNSUPPORTED_ABI_VERSION,
            mods_usvfs_get_health_v1(&unsupported));
}

TEST(ModsUsvfsAbi, ReportsStaticHandshakeBeforeHookInitialization)
{
  mods_usvfs_health_v1 health{};
  health.struct_size = static_cast<uint32_t>(sizeof(health));
  health.abi_version = MODS_USVFS_ABI_VERSION_V1;

  EXPECT_EQ(MODS_USVFS_RESULT_NOT_INITIALIZED, mods_usvfs_get_health_v1(&health));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(health)), health.struct_size);
  EXPECT_EQ(MODS_USVFS_ABI_VERSION_V1, health.abi_version);
  EXPECT_EQ(MODS_USVFS_PROTOCOL_VERSION_V1, health.protocol_version);
  EXPECT_EQ(MODS_USVFS_FORK_REVISION_V1, health.fork_revision);
#if defined(_WIN64)
  EXPECT_EQ(MODS_USVFS_ARCHITECTURE_X64, health.architecture);
#else
  EXPECT_EQ(MODS_USVFS_ARCHITECTURE_X86, health.architecture);
#endif
  EXPECT_EQ(MODS_USVFS_MANDATORY_HOOK_COUNT_V1, health.mandatory_hook_count);
  EXPECT_EQ(MODS_USVFS_HOOK_MANIFEST_VERSION_V1, health.hook_manifest_version);
  EXPECT_EQ(MODS_USVFS_CAPABILITIES_V1, health.capability_flags);
  EXPECT_EQ(
      UINT64_C(0),
      health.capability_flags &
          (MODS_USVFS_CAPABILITY_FAIL_CLOSED_DESCENDANT_HANDSHAKE |
           MODS_USVFS_CAPABILITY_VIRTUAL_OPEN_REGISTRY |
           MODS_USVFS_CAPABILITY_STEAM_COPY_ON_WRITE |
           MODS_USVFS_CAPABILITY_PROVIDER_OWNED_TOMBSTONES |
           MODS_USVFS_CAPABILITY_DURABLE_MUTATION_DELTAS |
           MODS_USVFS_CAPABILITY_BOUNDED_EVENT_CHANNEL));
  EXPECT_EQ(0U, health.installed_hook_count);
  EXPECT_EQ(0U, health.passed_probe_count);
}

TEST(ModsUsvfsAbi, ValidatesAndReportsHookStatusBeforeInitialization)
{
  EXPECT_EQ(
      MODS_USVFS_RESULT_INVALID_ARGUMENT,
      mods_usvfs_get_hook_status_v1(MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_EX_A, nullptr));

  mods_usvfs_hook_status_v1 tooSmall{};
  tooSmall.struct_size = static_cast<uint32_t>(sizeof(tooSmall) - 1);
  tooSmall.abi_version = MODS_USVFS_ABI_VERSION_V1;
  EXPECT_EQ(MODS_USVFS_RESULT_STRUCT_TOO_SMALL,
            mods_usvfs_get_hook_status_v1(MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_EX_A,
                                          &tooSmall));

  mods_usvfs_hook_status_v1 unsupported{};
  unsupported.struct_size = static_cast<uint32_t>(sizeof(unsupported));
  unsupported.abi_version = MODS_USVFS_ABI_VERSION_V1 + 1;
  EXPECT_EQ(MODS_USVFS_RESULT_UNSUPPORTED_ABI_VERSION,
            mods_usvfs_get_hook_status_v1(MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_EX_A,
                                          &unsupported));

  mods_usvfs_hook_status_v1 status{};
  status.struct_size = static_cast<uint32_t>(sizeof(status));
  status.abi_version = MODS_USVFS_ABI_VERSION_V1;
  EXPECT_EQ(MODS_USVFS_RESULT_HOOK_ID_OUT_OF_RANGE,
            mods_usvfs_get_hook_status_v1(MODS_USVFS_MANDATORY_HOOK_COUNT_V1, &status));
  EXPECT_EQ(
      MODS_USVFS_RESULT_NOT_INITIALIZED,
      mods_usvfs_get_hook_status_v1(MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_EX_A, &status));
  EXPECT_EQ(MODS_USVFS_HOOK_GET_FILE_ATTRIBUTES_EX_A, status.hook_id);
  EXPECT_EQ(MODS_USVFS_HOOK_INSTALL_NOT_ATTEMPTED, status.install_status);
  EXPECT_EQ(MODS_USVFS_HOOK_PROBE_NOT_RUN, status.probe_status);
}

TEST(ModsUsvfsProtocol, ValidatesAndDecodesBoundedFrameHeaders)
{
  std::array<uint8_t, MODS_USVFS_FRAME_HEADER_SIZE_V1> frame{};
  std::memcpy(frame.data(), "MVFS", 4);
  frame[4] = MODS_USVFS_PROTOCOL_VERSION_V1;
  frame[6] = MODS_USVFS_MESSAGE_HEALTH_EVENT;
  frame[8] = MODS_USVFS_FRAME_HEADER_SIZE_V1;
  frame[32] = 7;
  frame[72] = 3;

  mods_usvfs_frame_header_v1 header{};
  header.struct_size = sizeof(header);
  header.abi_version = MODS_USVFS_ABI_VERSION_V1;
  EXPECT_EQ(MODS_USVFS_RESULT_OK,
            mods_usvfs_validate_frame_v1(frame.data(), frame.size(), &header));
  EXPECT_EQ(MODS_USVFS_MESSAGE_HEALTH_EVENT, header.message_type);
  EXPECT_EQ(7U, header.request_id);
  EXPECT_EQ(3U, header.expected_delta_version);
  EXPECT_EQ(0U, header.payload_length);
}

TEST(ModsUsvfsProtocol, RejectsInvalidAndOversizedFrames)
{
  std::array<uint8_t, MODS_USVFS_FRAME_HEADER_SIZE_V1> frame{};
  mods_usvfs_frame_header_v1 header{};
  header.struct_size = sizeof(header);
  header.abi_version = MODS_USVFS_ABI_VERSION_V1;

  EXPECT_EQ(MODS_USVFS_RESULT_INVALID_FRAME,
            mods_usvfs_validate_frame_v1(frame.data(), frame.size(), &header));
  EXPECT_EQ(MODS_USVFS_RESULT_FRAME_TOO_LARGE,
            mods_usvfs_validate_frame_v1(
                frame.data(), MODS_USVFS_FRAME_MAX_SIZE_V1 + 1, &header));
}

// name of a file to be created in the virtual fs. Shouldn't exist on disc but the
// directory must exist
static LPCSTR VIRTUAL_FILEA  = "C:/np.exe";
static LPCWSTR VIRTUAL_FILEW = L"C:/np.exe";

// a real file on disc that has to exist
static LPCSTR REAL_FILEA  = "C:/windows/notepad.exe";
static LPCWSTR REAL_FILEW = L"C:/windows/notepad.exe";

static LPCSTR REAL_DIRA  = "C:/windows/Logs";
static LPCWSTR REAL_DIRW = L"C:/windows/Logs";

static std::shared_ptr<spdlog::logger> logger()
{
  std::shared_ptr<spdlog::logger> result = spdlog::get("test");
  if (result.get() == nullptr) {
    result = spdlog::stdout_logger_mt("test");
  }
  return result;
}

auto defaultUsvfsParams(const char* instanceName = "usvfs_test")
{
  std::unique_ptr<usvfsParameters, decltype(&usvfsFreeParameters)> parameters{
      usvfsCreateParameters(), &usvfsFreeParameters};

  usvfsSetInstanceName(parameters.get(), instanceName);
  usvfsSetDebugMode(parameters.get(), true);
  usvfsSetLogLevel(parameters.get(), LogLevel::Debug);
  usvfsSetCrashDumpType(parameters.get(), CrashDumpsType::None);
  usvfsSetCrashDumpPath(parameters.get(), "");

  return std::move(parameters);
}

class USVFSTest : public testing::Test
{
public:
  void SetUp()
  {
    SHMLogger::create("usvfs");
    // need to initialize logging in the context of the dll
    usvfsInitLogging();
  }

  void TearDown()
  {
    std::array<char, 1024> buffer;
    while (SHMLogger::instance().tryGet(buffer.data(), buffer.size())) {
      std::cout << buffer.data() << std::endl;
    }
    SHMLogger::free();
  }

private:
};

class USVFSTestWithReroute : public testing::Test
{
public:
  void SetUp()
  {
    SHMLogger::create("usvfs");
    // need to initialize logging in the context of the dll
    usvfsInitLogging();

    auto params = defaultUsvfsParams();
    m_Context.reset(usvfsCreateHookContext(*params, ::GetModuleHandle(nullptr)));
    usvfs::RedirectionTreeContainer& tree = m_Context->redirectionTable();
    tree.addFile(
        ush::string_cast<std::string>(VIRTUAL_FILEW, ush::CodePage::UTF8).c_str(),
        usvfs::RedirectionDataLocal(REAL_FILEA));
  }

  void TearDown()
  {
    std::array<char, 1024> buffer;
    while (SHMLogger::instance().tryGet(buffer.data(), buffer.size())) {
      std::cout << buffer.data() << std::endl;
    }
    m_Context.reset();
    SHMLogger::free();
  }

private:
  std::unique_ptr<usvfs::HookContext> m_Context;
};

class USVFSTestAuto : public testing::Test
{
public:
  void SetUp()
  {
    auto params = defaultUsvfsParams();
    usvfsConnectVFS(params.get());
    SHMLogger::create("usvfs");
  }

  void TearDown()
  {
    usvfsDisconnectVFS();

    std::array<char, 1024> buffer;
    while (SHMLogger::instance().tryGet(buffer.data(), buffer.size())) {
      std::cout << buffer.data() << std::endl;
    }
    SHMLogger::free();
  }

private:
};

TEST_F(USVFSTestAuto, DisablesBlacklistAndMohiddenSkipping)
{
  usvfsBlacklistExecutable(L"notepad.exe");
  usvfsAddSkipFileSuffix(L".MoHidden");
  usvfsAddSkipFileSuffix(L".skip");

  auto context = usvfs::HookContext::readAccess(__FUNCTION__);
  EXPECT_FALSE(context->executableBlacklisted(L"C:\\Windows\\notepad.exe", nullptr));
  EXPECT_THAT(context->skipFileSuffixes(),
              ::testing::Not(::testing::Contains(".MoHidden")));
  EXPECT_THAT(context->skipFileSuffixes(), ::testing::Contains(".skip"));
}

TEST_F(USVFSTest, CanResizeRedirectiontree)
{
  using usvfs::shared::MissingThrow;
  ASSERT_NO_THROW({
    usvfs::RedirectionTreeContainer container("treetest_shm", 1024);
    for (char i = 'a'; i <= 'z'; ++i) {
      for (char j = 'a'; j <= 'z'; ++j) {
        std::string name = std::string(R"(C:\temp\)") + i + j;
        container.addFile(name, usvfs::RedirectionDataLocal("gaga"), false);
      }
    }

    ASSERT_EQ("gaga", container->node("C:")
                          ->node("temp")
                          ->node("aa", MissingThrow)
                          ->data()
                          .linkTarget);
    ASSERT_EQ("gaga", container->node("C:")
                          ->node("temp")
                          ->node("az", MissingThrow)
                          ->data()
                          .linkTarget);
  });
}

/*
TEST_F(USVFSTest, CreateFileHookReportsCorrectErrorOnMissingFile)
{
  ASSERT_NO_THROW({
    USVFSParameters params;
    USVFSInitParameters(&params, "usvfs_test", true, LogLevel::Debug,
CrashDumpsType::None, ""); std::unique_ptr<usvfs::HookContext>
ctx(CreateHookContext(params, ::GetModuleHandle(nullptr))); HANDLE res =
usvfs::hook_CreateFileW(VIRTUAL_FILEW , GENERIC_READ , FILE_SHARE_READ |
FILE_SHARE_WRITE , nullptr , OPEN_EXISTING , FILE_ATTRIBUTE_NORMAL , nullptr);

    ASSERT_EQ(INVALID_HANDLE_VALUE, res);
    ASSERT_EQ(ERROR_FILE_NOT_FOUND, ::GetLastError());
  });
}
*/

/*
TEST_F(USVFSTestWithReroute, CreateFileHookRedirectsFile)
{
  ASSERT_NE(INVALID_HANDLE_VALUE
            , usvfs::hook_CreateFileW(VIRTUAL_FILEW
                                  , GENERIC_READ
                                  , FILE_SHARE_READ | FILE_SHARE_WRITE
                                  , nullptr
                                  , OPEN_EXISTING
                                  , FILE_ATTRIBUTE_NORMAL
                                  , nullptr));
}
*/

TEST_F(USVFSTest, GetFileAttributesHookReportsCorrectErrorOnMissingFile)
{
  ASSERT_NO_THROW({
    try {
      auto params = defaultUsvfsParams();
      std::unique_ptr<usvfs::HookContext> ctx(
          usvfsCreateHookContext(*params, ::GetModuleHandle(nullptr)));
      DWORD res = usvfs::hook_GetFileAttributesW(VIRTUAL_FILEW);

      ASSERT_EQ(INVALID_FILE_ATTRIBUTES, res);
      ASSERT_EQ(ERROR_FILE_NOT_FOUND, ::GetLastError());
    } catch (const std::exception& e) {
      logger()->error("Exception: {}", e.what());
      throw;
    }
  });
}

TEST_F(USVFSTest, GetFileAttributesHookRedirectsFile)
{
  auto params = defaultUsvfsParams();
  std::unique_ptr<usvfs::HookContext> ctx(
      usvfsCreateHookContext(*params, ::GetModuleHandle(nullptr)));
  usvfs::RedirectionTreeContainer& tree = ctx->redirectionTable();

  tree.addFile(
      ush::string_cast<std::string>(VIRTUAL_FILEW, ush::CodePage::UTF8).c_str(),
      usvfs::RedirectionDataLocal(REAL_FILEA));

  ASSERT_EQ(::GetFileAttributesW(REAL_FILEW),
            usvfs::hook_GetFileAttributesW(VIRTUAL_FILEW));
}
/*
TEST_F(USVFSTest, GetFullPathNameOnRegularCurrentDirectory)
{
  USVFSParameters params;
  USVFSInitParameters(&params, "usvfs_test", true, LogLevel::Debug,
CrashDumpsType::None, ""); std::unique_ptr<usvfs::HookContext>
ctx(CreateHookContext(params, ::GetModuleHandle(nullptr)));

  std::wstring expected = winapi::wide::getCurrentDirectory() + L"\\filename.txt";

  DWORD bufferLength = 32767;
  std::unique_ptr<wchar_t[]> buffer(new wchar_t[bufferLength]);
  LPWSTR filePart = nullptr;

  DWORD res = usvfs::hook_GetFullPathNameW(L"filename.txt", bufferLength, buffer.get(),
&filePart);

  ASSERT_NE(0UL, res);
  ASSERT_EQ(expected, std::wstring(buffer.get()));
}*/

// small wrapper to call usvfs::hook_NtOpenFile with a path
//
// at some point in time, changes were made to USVFS such that calling a hooked
// function from a handle obtained from a non-hooked function would not work anymore,
// meaning that function such as CreateFileW that have no hook equivalent cannot
// be used to test hook functions
//
// this function is useful to simulate a CreateFileW by internally using the hook
// version of NtOpenFile
//
HANDLE hooked_NtOpenFile(LPCWSTR path, ACCESS_MASK accessMask, ULONG shareAccess,
                         ULONG openOptions)
{
  constexpr size_t BUFFER_SIZE = 2048;
  IO_STATUS_BLOCK statusBlock;
  OBJECT_ATTRIBUTES attributes;
  attributes.SecurityDescriptor       = 0;
  attributes.SecurityQualityOfService = 0;
  attributes.RootDirectory            = 0;
  attributes.Attributes               = 0;
  attributes.Length                   = sizeof(OBJECT_ATTRIBUTES);

  WCHAR stringBuffer[BUFFER_SIZE];
  UNICODE_STRING string;
  string.Buffer = stringBuffer;
  lstrcpyW(stringBuffer, L"\\??\\");
  lstrcatW(stringBuffer, path);
  string.Length         = static_cast<USHORT>(lstrlenW(stringBuffer) * 2);
  string.MaximumLength  = BUFFER_SIZE;
  attributes.ObjectName = &string;

  HANDLE ret = INVALID_HANDLE_VALUE;
  if (usvfs::hook_NtOpenFile(&ret, accessMask, &attributes, &statusBlock, shareAccess,
                             openOptions) != STATUS_SUCCESS) {
    return INVALID_HANDLE_VALUE;
  }

  return ret;
}

TEST_F(USVFSTest, NtQueryDirectoryFileRegularFile)
{
  auto params = defaultUsvfsParams();
  std::unique_ptr<usvfs::HookContext> ctx(
      usvfsCreateHookContext(*params, ::GetModuleHandle(nullptr)));

  HANDLE hdl =
      hooked_NtOpenFile(L"C:\\", FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
  ASSERT_NE(INVALID_HANDLE_VALUE, hdl);

  IO_STATUS_BLOCK status;
  char buffer[1024];

  usvfs::hook_NtQueryDirectoryFile(hdl, nullptr, nullptr, nullptr, &status, buffer,
                                   1024, FileDirectoryInformation, TRUE, nullptr, TRUE);

  ASSERT_EQ(STATUS_SUCCESS, status.Status);

  usvfs::hook_NtClose(hdl);
}

TEST_F(USVFSTest, NtQueryDirectoryFileFindsVirtualFile)
{
  auto params = defaultUsvfsParams();
  std::unique_ptr<usvfs::HookContext> ctx(
      usvfsCreateHookContext(*params, ::GetModuleHandle(nullptr)));
  usvfs::RedirectionTreeContainer& tree = ctx->redirectionTable();

  tree.addFile(L"C:\\np.exe", usvfs::RedirectionDataLocal(REAL_FILEA));

  HANDLE hdl =
      hooked_NtOpenFile(L"C:\\", FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
  ASSERT_NE(INVALID_HANDLE_VALUE, hdl);

  IO_STATUS_BLOCK status;
  char buffer[1024];

  usvfs::UnicodeString fileName(L"np.exe");

  usvfs::hook_NtQueryDirectoryFile(hdl, nullptr, nullptr, nullptr, &status, buffer,
                                   1024, FileDirectoryInformation, TRUE,
                                   static_cast<PUNICODE_STRING>(fileName), TRUE);

  FILE_DIRECTORY_INFORMATION* info =
      reinterpret_cast<FILE_DIRECTORY_INFORMATION*>(buffer);
  ASSERT_EQ(STATUS_SUCCESS, status.Status);
  ASSERT_EQ(0, wcscmp(info->FileName, L"np.exe"));

  usvfs::hook_NtClose(hdl);
}

TEST_F(USVFSTest, NtQueryDirectoryFileExVirtualFile)
{
  auto params = defaultUsvfsParams();
  std::unique_ptr<usvfs::HookContext> ctx(
      usvfsCreateHookContext(*params, ::GetModuleHandle(nullptr)));
  usvfs::RedirectionTreeContainer& tree = ctx->redirectionTable();

  tree.addFile(L"C:\\0123456789.txt", usvfs::RedirectionDataLocal(REAL_FILEA));
  tree.addFile(L"C:\\123456", usvfs::RedirectionDataLocal(REAL_FILEA));
  tree.addFile(L"C:\\abcdef", usvfs::RedirectionDataLocal(REAL_FILEA));
  tree.addFile(L"C:\\abcdefghijklmnopqrstuvwxyz.txt",
               usvfs::RedirectionDataLocal(REAL_FILEA));

  HANDLE hdl =
      hooked_NtOpenFile(L"C:\\", FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
  ASSERT_NE(INVALID_HANDLE_VALUE, hdl);

  IO_STATUS_BLOCK status;

  constexpr size_t BUFFER_SIZE = 2048;
  char buffer[BUFFER_SIZE];

  std::vector<std::wstring> foundFiles;
  while (usvfs::hook_NtQueryDirectoryFileEx(
             hdl, nullptr, nullptr, nullptr, &status, buffer, BUFFER_SIZE,
             FileFullDirectoryInformation, 0, nullptr) == STATUS_SUCCESS) {
    std::size_t offset = 0;
    while (offset < BUFFER_SIZE) {
      const auto* info = reinterpret_cast<FILE_FULL_DIR_INFORMATION*>(buffer + offset);
      foundFiles.emplace_back(info->FileName, info->FileNameLength / sizeof(wchar_t));

      if (info->NextEntryOffset == 0) {
        break;  // no more entries
      }

      offset += info->NextEntryOffset;
    }
  }

  ASSERT_THAT(foundFiles,
              ::testing::IsSupersetOf({L"0123456789.txt", L"123456", L"abcdef",
                                       L"abcdefghijklmnopqrstuvwxyz.txt"}));

  usvfs::hook_NtClose(hdl);
}

TEST_F(USVFSTest, NtQueryObjectVirtualFile)
{
  std::wstring c_drive_device;
  {
    // find the device path for C:
    HANDLE hdl = ::CreateFileW(
        L"C:\\", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE, hdl);

    char buffer[2048];
    ASSERT_EQ(STATUS_SUCCESS, ::NtQueryObject(hdl, ObjectNameInformation, buffer,
                                              sizeof(buffer), nullptr));

    OBJECT_NAME_INFORMATION* information =
        reinterpret_cast<OBJECT_NAME_INFORMATION*>(buffer);

    c_drive_device =
        std::wstring(information->Name.Buffer, information->Name.Length / 2);

    ::CloseHandle(hdl);
  }

  auto params = defaultUsvfsParams();
  std::unique_ptr<usvfs::HookContext> ctx(
      usvfsCreateHookContext(*params, ::GetModuleHandle(nullptr)));
  usvfs::RedirectionTreeContainer& tree = ctx->redirectionTable();

  tree.addFile(L"C:\\np.exe", usvfs::RedirectionDataLocal(REAL_FILEA));

  HANDLE hdl = hooked_NtOpenFile(L"C:\\np.exe", FILE_GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_NON_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT);
  ASSERT_NE(INVALID_HANDLE_VALUE, hdl) << "last error=" << ::GetLastError();

  {
    char buffer[1024];
    IO_STATUS_BLOCK status;
    const auto res = usvfs::hook_NtQueryInformationFile(
        hdl, &status, buffer, sizeof(buffer), FileNameInformation);
    ASSERT_EQ(STATUS_SUCCESS, res);
    ASSERT_EQ(STATUS_SUCCESS, status.Status);

    FILE_NAME_INFORMATION* fileNameInfo =
        reinterpret_cast<FILE_NAME_INFORMATION*>(buffer);
    ASSERT_EQ(L"\\np.exe",
              std::wstring(fileNameInfo->FileName, fileNameInfo->FileNameLength / 2));
  }

  {
    char buffer[1024];
    IO_STATUS_BLOCK status;
    const auto res = usvfs::hook_NtQueryInformationFile(
        hdl, &status, buffer, sizeof(buffer), FileNormalizedNameInformation);
    ASSERT_EQ(STATUS_SUCCESS, res);
    ASSERT_EQ(STATUS_SUCCESS, status.Status);
    ASSERT_EQ(sizeof(ULONG) + 7 * 2, status.Information);

    FILE_NAME_INFORMATION* fileNameInfo =
        reinterpret_cast<FILE_NAME_INFORMATION*>(buffer);
    ASSERT_EQ(L"\\np.exe",
              std::wstring(fileNameInfo->FileName, fileNameInfo->FileNameLength / 2));
  }

  // buffer of size should be too small for the original path (\Windows\notepad.exe)
  // but not for \np.exe
  {
    // the required size should be sizeof(ULONG) + 7 * 2 but apparently that is
    // not enough for the CI so using 16 * 2 which should be large enough for
    // the hooked version, but still too short for the non-hooked one
    char buffer[sizeof(ULONG) + 7 * 2];
    IO_STATUS_BLOCK status;
    NTSTATUS res;

    res = ::NtQueryInformationFile(hdl, &status, buffer, sizeof(buffer),
                                   FileNameInformation);
    ASSERT_EQ(STATUS_BUFFER_OVERFLOW, res);
    ASSERT_EQ(STATUS_BUFFER_OVERFLOW, status.Status);

    res = usvfs::hook_NtQueryInformationFile(hdl, &status, buffer, sizeof(buffer),
                                             FileNameInformation);
    ASSERT_EQ(STATUS_SUCCESS, res);
    ASSERT_EQ(STATUS_SUCCESS, status.Status);
    ASSERT_EQ(sizeof(ULONG) + 7 * 2, status.Information);

    FILE_NAME_INFORMATION* fileNameInfo =
        reinterpret_cast<FILE_NAME_INFORMATION*>(buffer);
    ASSERT_EQ(L"\\np.exe",
              std::wstring(fileNameInfo->FileName, fileNameInfo->FileNameLength / 2));
  }

  {
    char buffer[2048];
    const auto res = usvfs::hook_NtQueryObject(hdl, ObjectNameInformation, buffer,
                                               sizeof(buffer), nullptr);
    ASSERT_EQ(STATUS_SUCCESS, res);

    OBJECT_NAME_INFORMATION* information =
        reinterpret_cast<OBJECT_NAME_INFORMATION*>(buffer);
    ASSERT_EQ(c_drive_device + L"np.exe",
              std::wstring(information->Name.Buffer,
                           information->Name.Length / sizeof(wchar_t)));
  }

  {
    // expected length is sizeof struct + size of path (in bytes), including the
    // null-character
    const auto expectedLength =
        sizeof(OBJECT_NAME_INFORMATION) + c_drive_device.size() * 2 + 12 + 2;
    ULONG requiredLength;
    NTSTATUS res;
    char buffer[2048];

    res =
        usvfs::hook_NtQueryObject(hdl, ObjectNameInformation, buffer,
                                  sizeof(OBJECT_NAME_INFORMATION) - 1, &requiredLength);
    ASSERT_EQ(STATUS_INFO_LENGTH_MISMATCH, res);
    ASSERT_EQ(expectedLength, requiredLength);

    res = usvfs::hook_NtQueryObject(hdl, ObjectNameInformation, buffer,
                                    sizeof(OBJECT_NAME_INFORMATION), &requiredLength);
    ASSERT_EQ(STATUS_BUFFER_OVERFLOW, res);
    ASSERT_EQ(expectedLength, requiredLength);
  }

  usvfs::hook_NtClose(hdl);
}

TEST_F(USVFSTestAuto, CannotCreateLinkToFileInNonexistantDirectory)
{
  ASSERT_EQ(FALSE, usvfsVirtualLinkFile(
                       REAL_FILEW, L"c:/this_directory_shouldnt_exist/np.exe", FALSE));
}

TEST_F(USVFSTestAuto, CanCreateMultipleLinks)
{
  static LPCWSTR outFile            = LR"(C:\np.exe)";
  static LPCWSTR outDir             = LR"(C:\logs)";
  static LPCWSTR outDirCanonizeTest = LR"(C:\.\not/../logs\.\a\.\b\.\c\..\.\..\.\..\)";
  ASSERT_EQ(TRUE, usvfsVirtualLinkFile(REAL_FILEW, outFile, 0));
  ASSERT_EQ(TRUE, usvfsVirtualLinkDirectoryStatic(REAL_DIRW, outDir, 0));

  // both file and dir exist and have the correct type
  ASSERT_NE(INVALID_FILE_ATTRIBUTES, usvfs::hook_GetFileAttributesW(outFile));
  ASSERT_NE(INVALID_FILE_ATTRIBUTES, usvfs::hook_GetFileAttributesW(outDir));
  ASSERT_EQ(0UL, usvfs::hook_GetFileAttributesW(outFile) & FILE_ATTRIBUTE_DIRECTORY);
  ASSERT_NE(0UL, usvfs::hook_GetFileAttributesW(outDir) & FILE_ATTRIBUTE_DIRECTORY);
  ASSERT_NE(0UL, usvfs::hook_GetFileAttributesW(outDirCanonizeTest) &
                     FILE_ATTRIBUTE_DIRECTORY);
}

int main(int argc, char** argv)
{
  using namespace test;

  auto dllPath = path_of_usvfs_lib(platform_dependant_executable("usvfs", "dll"));
  ScopedLoadLibrary loadDll(dllPath.c_str());
  if (!loadDll) {
    std::wcerr << L"failed to load usvfs dll: " << dllPath.c_str() << L", "
               << GetLastError() << std::endl;
    return 1;
  }

  // note: this makes the logger available only to functions statically linked to the
  // test binary, not those called in the dll
  auto logger = spdlog::stdout_logger_mt("usvfs");
  logger->set_level(spdlog::level::warn);
  testing::InitGoogleTest(&argc, argv);
  int res = RUN_ALL_TESTS();

  return res;
}
