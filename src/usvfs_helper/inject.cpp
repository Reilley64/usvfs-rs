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
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <boost/filesystem.hpp>

#include "bootstrap.h"
#include "inject.h"
#include <exceptionex.h>
#include <formatters.h>
#include <injectlib.h>
#include <loghelpers.h>
#include <pch.h>
#include <stringcast.h>
#include <stringutils.h>
#include <usvfsparametersprivate.h>
#include <winapi.h>

namespace ush = usvfs::shared;

using namespace winapi;

namespace
{

constexpr DWORD InjectionHandshakeTimeoutMs  = 15000;
constexpr UINT VirtualizationFailureExitCode = 125;

void terminateAndWait(HANDLE processHandle)
{
  ::TerminateProcess(processHandle, VirtualizationFailureExitCode);
  ::WaitForSingleObject(processHandle, INFINITE);
}

void injectSameBitness(HANDLE processHandle, HANDLE threadHandle,
                       const boost::filesystem::path& dllPath,
                       const usvfsParameters& parameters)
{
  if (threadHandle == nullptr || threadHandle == INVALID_HANDLE_VALUE) {
    USVFS_THROW_EXCEPTION(
        usage_error() << ex_msg("injection requires a suspended primary thread"));
  }

  const DWORD observedSuspendCount = ::SuspendThread(threadHandle);
  if (observedSuspendCount == static_cast<DWORD>(-1)) {
    throw ush::windows_error("failed to inspect injection thread suspension");
  }
  const DWORD restoredSuspendCount = ::ResumeThread(threadHandle);
  if (restoredSuspendCount == static_cast<DWORD>(-1)) {
    throw ush::windows_error("failed to restore injection thread suspension");
  }
  if (observedSuspendCount != 1 || restoredSuspendCount != 2) {
    USVFS_THROW_EXCEPTION(usage_error()
                          << ex_msg("injection target is not a singly suspended "
                                    "newly created process"));
  }

  HANDLE readyEvent          = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE continueEvent       = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE targetReadyEvent    = nullptr;
  HANDLE targetContinueEvent = nullptr;

  try {
    if (readyEvent == nullptr || continueEvent == nullptr) {
      throw ush::windows_error("failed to create injection handshake events");
    }

    if (!::DuplicateHandle(::GetCurrentProcess(), readyEvent, processHandle,
                           &targetReadyEvent, EVENT_MODIFY_STATE, FALSE, 0) ||
        !::DuplicateHandle(::GetCurrentProcess(), continueEvent, processHandle,
                           &targetContinueEvent, SYNCHRONIZE, FALSE, 0)) {
      throw ush::windows_error("failed to duplicate injection handshake events");
    }

    usvfs::BootstrapParameters bootstrap{};
    bootstrap.structSize      = sizeof(bootstrap);
    bootstrap.protocolVersion = usvfs::BootstrapProtocolVersion;
    bootstrap.readyEvent =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(targetReadyEvent));
    bootstrap.continueEvent = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(targetContinueEvent));
    bootstrap.parameters = parameters;

    InjectLib::InjectDLL(processHandle, threadHandle, dllPath.c_str(), "InitHooks",
                         &bootstrap, sizeof(bootstrap));

    const DWORD previousSuspendCount = ::ResumeThread(threadHandle);
    if (previousSuspendCount != 1) {
      throw ush::windows_error(
          "injection target is not a newly created suspended process",
          previousSuspendCount == static_cast<DWORD>(-1) ? ::GetLastError()
                                                         : ERROR_INVALID_STATE);
    }

    HANDLE waits[] = {readyEvent, processHandle};
    const DWORD waitResult =
        ::WaitForMultipleObjects(2, waits, FALSE, InjectionHandshakeTimeoutMs);
    if (waitResult != WAIT_OBJECT_0) {
      terminateAndWait(processHandle);
      if (waitResult == WAIT_TIMEOUT) {
        USVFS_THROW_EXCEPTION(timeout_error()
                              << ex_msg("injection handshake didn't complete in time"));
      }
      USVFS_THROW_EXCEPTION(unknown_error() << ex_msg("injection handshake failed")
                                            << ex_win_errcode(::GetLastError()));
    }

    const DWORD handshakeSuspendCount = ::SuspendThread(threadHandle);
    if (handshakeSuspendCount != 0) {
      throw ush::windows_error("failed to suspend process after injection handshake",
                               handshakeSuspendCount == static_cast<DWORD>(-1)
                                   ? ::GetLastError()
                                   : ERROR_INVALID_STATE);
    }
    if (!::SetEvent(continueEvent)) {
      throw ush::windows_error("failed to complete injection handshake");
    }
  } catch (...) {
    if (::WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT) {
      terminateAndWait(processHandle);
    }
    if (readyEvent != nullptr) {
      ::CloseHandle(readyEvent);
    }
    if (continueEvent != nullptr) {
      ::CloseHandle(continueEvent);
    }
    throw;
  }

  ::CloseHandle(readyEvent);
  ::CloseHandle(continueEvent);
}

}  // namespace

void usvfs::injectProcess(const std::wstring& applicationPath,
                          const usvfsParameters& parameters,
                          const PROCESS_INFORMATION& processInfo)
{
  injectProcess(applicationPath, parameters, processInfo.hProcess, processInfo.hThread);
}

void usvfs::injectProcess(const std::wstring& applicationPath,
                          const usvfsParameters& parameters, HANDLE processHandle,
                          HANDLE threadHandle)
{
  bool proc64      = false;
  bool sameBitness = false;
  {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    BOOL wow64;
    IsWow64Process(processHandle, &wow64);
    if (wow64) {
      // process is running under wow64 so it has to be a 32bit process running on 64bit
      // windows
      proc64 = false;
      BOOL temp;
      IsWow64Process(GetCurrentProcess(), &temp);
      sameBitness = temp == TRUE;
    } else {
      BOOL selfWow64;
      IsWow64Process(GetCurrentProcess(), &selfWow64);
      if (selfWow64) {
        // WE are a 32 bit process running on 64bit windows. the other process isn't, so
        // its 64bit
        proc64 = true;
      } else {
        sameBitness = true;
        // we have the same bitness as that other process, but which is it?
#ifdef _WIN64
        proc64 = true;
#else
        proc64 = false;
#endif
      }
    }
  }
  boost::filesystem::path binPath = boost::filesystem::path(applicationPath);
  spdlog::get("usvfs")->info("injecting to process {} with {} bitness",
                             ::GetProcessId(processHandle),
                             sameBitness ? "same" : "different");

  if (sameBitness) {
    static constexpr auto USVFS_DLL =
#ifdef _WIN64
        L"usvfs_x64.dll";
#else
        L"usvfs_x86.dll";
#endif
    const auto& preferedDll         = binPath / USVFS_DLL;
    boost::filesystem::path dllPath = preferedDll;
    bool dllFound                   = boost::filesystem::exists(dllPath);
    // support for runing tests using a usvfs dll in lib folder (and proxy under bin):
    if (!dllFound && binPath.filename() == L"bin") {
      dllPath  = binPath.parent_path() / L"lib" / USVFS_DLL;
      dllFound = boost::filesystem::exists(dllPath);
    }
    if (!dllFound) {
      USVFS_THROW_EXCEPTION(
          file_not_found_error()
          << ex_msg(std::string("dll missing: ") +
                    ush::string_cast<std::string>(preferedDll.wstring()).c_str()));
    }

    spdlog::get("usvfs")->info("dll path: {}", dllPath.wstring());

    injectSameBitness(processHandle, threadHandle, dllPath, parameters);

    spdlog::get("usvfs")->info(
        "injection handshake for same bitness process {} successful",
        ::GetProcessId(processHandle));
  } else {
    // first try platform specific proxy exe:
    static constexpr auto USVFS_PREFERED_EXE =
#ifdef _WIN64
        L"usvfs_proxy_x86.exe";
#else
        L"usvfs_proxy_x64.exe";
#endif
    const auto& preferedExe         = binPath / USVFS_PREFERED_EXE;
    boost::filesystem::path exePath = preferedExe;
    bool exeFound                   = boost::filesystem::exists(exePath);
    // support for runing tests using a usvfs dll in lib folder (and proxy under bin):
    if (!exeFound && binPath.filename() == L"lib") {
      exePath  = binPath.parent_path() / L"bin" / USVFS_PREFERED_EXE;
      exeFound = boost::filesystem::exists(exePath);
    }
    // finally fallback to old proxy naming (but only for 64bit as we don't have a 64bit
    // proxy in this case):
#ifdef _WIN64
    if (!exeFound) {
      exePath  = binPath / L"usvfs_proxy.exe";
      exeFound = boost::filesystem::exists(exePath);
    }
#endif
    if (!exeFound) {
      USVFS_THROW_EXCEPTION(file_not_found_error() << ex_msg(
                                std::string("usvfs proxy not found: ") +
                                ush::string_cast<std::string>(preferedExe.wstring())));
    } else
      spdlog::get("usvfs")->info("using usvfs proxy: {}",
                                 ush::string_cast<std::string>(preferedExe.wstring()));
    // need to use proxy aplication to inject
    auto proxyProcess =
        std::move(wide::createProcess(exePath.wstring())
                      .arg(L"--instance")
                      .arg(ush::string_cast<std::wstring>(parameters.instanceName))
                      .arg(L"--pid")
                      .arg(GetProcessId(processHandle)));

    if (threadHandle != INVALID_HANDLE_VALUE) {
      proxyProcess.arg("--tid").arg(GetThreadId(threadHandle));
    }
    process::Result result = proxyProcess();
    if (!result.valid) {
      USVFS_THROW_EXCEPTION(unknown_error()
                            << ex_msg(std::string("failed to start proxy ") +
                                      ush::string_cast<std::string>(exePath.wstring()))
                            << ex_win_errcode(result.errorCode));
    } else {
      // wait for proxy completion. this shouldn't take long, 15 seconds is very
      // generous
      switch (WaitForSingleObject(result.processInfo.hProcess, 15000)) {
      case WAIT_TIMEOUT: {
        spdlog::get("usvfs")->debug("proxy timeout");
        TerminateProcess(result.processInfo.hProcess, 1);
        USVFS_THROW_EXCEPTION(timeout_error()
                              << ex_msg(std::string("proxy didn't complete in time")));
      } break;
      case WAIT_FAILED: {
        spdlog::get("usvfs")->debug("proxy wait failed");
        TerminateProcess(result.processInfo.hProcess, 1);
        USVFS_THROW_EXCEPTION(unknown_error()
                              << ex_msg(
                                     std::string("failed to wait for proxy completion"))
                              << ex_win_errcode(result.errorCode));
      } break;
      default: {
        DWORD exitCode = 1;
        if (!::GetExitCodeProcess(result.processInfo.hProcess, &exitCode) ||
            exitCode != 0) {
          USVFS_THROW_EXCEPTION(unknown_error()
                                << ex_msg("proxy injection handshake failed"));
        }
        spdlog::get("usvfs")->debug("proxy injection handshake successful");
      } break;
      }
    }
  }
}
