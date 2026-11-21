#include "prx/libc/include/Api.hpp"
#include "prx/libc/include/ChildProcesses.hpp"
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

extern "C" int* APS5_VABI __error_nid_postfix() {
    static thread_local int error = 0;
    return &error;
}

static void Require(bool value) {
    if (!value) throw std::runtime_error("child process wait assertion failed");
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "child") {
        const auto event = reinterpret_cast<HANDLE>(std::stoull(argv[2]));
        return WaitForSingleObject(event, 5000) == WAIT_OBJECT_0 ? 42 : 99;
    }
    try {
        Require(argc == 2);
        const std::string mode(argv[1]);
        if (mode == "invalid") {
            RegisterChildProcess_nid_no_patch(123, 0);
            int status = 1234;
            ChildProcessTimes times{1234, 5678};
            for (unsigned options : {1u, 0u}) {
                bool threw = false;
                try {
                    WaitChildProcess_nid_no_patch(123, options, &status, &times);
                } catch (const std::system_error& error) {
                    threw = error.code() == std::error_code(ERROR_INVALID_HANDLE, std::system_category());
                }
                Require(threw && status == 1234 && times.user == 1234 && times.kernel == 5678);
            }
            return 0;
        }
        Require(mode == "live" || mode == "concurrent");
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        const HANDLE event = CreateEventW(&security, TRUE, FALSE, nullptr);
        Require(event != nullptr);
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        Require(length != 0 && length < executable.size());
        executable.resize(length);
        std::wstring command = L"\"" + executable + L"\" child " + std::to_wstring(reinterpret_cast<std::uintptr_t>(event));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &child) != FALSE);
        Require(CloseHandle(child.hThread) != FALSE);
        RegisterChildProcess_nid_no_patch(static_cast<std::int32_t>(child.dwProcessId), reinterpret_cast<std::int64_t>(child.hProcess));
        int status = 1234;
        ChildProcessTimes times{1234, 5678};
        Require(WaitChildProcess_nid_no_patch(static_cast<std::int32_t>(child.dwProcessId), 1, &status, &times) == 0);
        Require(status == 1234 && times.user == 1234 && times.kernel == 5678);
        if (mode == "live") {
            Require(SetEvent(event) != FALSE);
            Require(WaitChildProcess_nid_no_patch(static_cast<std::int32_t>(child.dwProcessId), 0, &status, &times) == child.dwProcessId);
            Require(status == (42 << 8) && times.user != 1234 && times.kernel != 5678);
            Require(WaitChildProcess_nid_no_patch(static_cast<std::int32_t>(child.dwProcessId), 1, &status) == -2);
        } else {
            std::array<std::int64_t, 2> results{};
            std::array<int, 2> statuses{};
            const auto wait = [&](std::size_t index) {
                results[index] = WaitChildProcess_nid_no_patch(static_cast<std::int32_t>(child.dwProcessId), 0, &statuses[index]);
            };
            std::thread first(wait, 0);
            std::thread second(wait, 1);
            Sleep(25);
            Require(SetEvent(event) != FALSE);
            first.join();
            second.join();
            const bool firstReaped = results[0] == child.dwProcessId && statuses[0] == (42 << 8);
            const bool secondReaped = results[1] == child.dwProcessId && statuses[1] == (42 << 8);
            Require((firstReaped && results[1] == -2) || (secondReaped && results[0] == -2));
        }
        Require(CloseHandle(event) != FALSE);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
