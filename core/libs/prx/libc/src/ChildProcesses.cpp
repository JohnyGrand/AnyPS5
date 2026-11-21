#include "prx/libc/include/ChildProcesses.hpp"
#include "prx/libc/include/Api.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
namespace {
struct ChildProcess {
    std::int32_t processId;
    std::int64_t processHandle;
};

struct CloseProcessHandle {
    void operator()(void* handle) const { CloseHandle(handle); }
};

using OwnedProcessHandle = std::unique_ptr<void, CloseProcessHandle>;

std::mutex childProcessMutex;
std::vector<ChildProcess> childProcesses;

int ChildProcessWaitStatus(DWORD code) {
    switch (code) {
    case 0xC0000005u:
    case 0xC00000FDu: return 11;
    case 0xC000001Du:
    case 0xC0000096u: return 4;
    case 0x80000003u: return 5;
    case 0xC0000094u:
    case 0xC0000095u: return 8;
    case 0xC0000409u:
    case 0xC0000374u: return 6;
    default: return static_cast<int>(code & 0xffu) << 8;
    }
}
}

extern "C" void RegisterChildProcess_nid_no_patch(std::int32_t processId, std::int64_t processHandle) {
    std::lock_guard lock(childProcessMutex);
    childProcesses.push_back({processId, processHandle});
}

extern "C" std::int64_t WaitChildProcess_nid_no_patch(std::int32_t processId, unsigned options, int* status, ChildProcessTimes* times) {
    const bool noWait = (options & 1u) != 0;
    const auto matches = [&](const ChildProcess& child) { return processId <= 0 || child.processId == processId; };
    {
        std::lock_guard lock(childProcessMutex);
        if (std::none_of(childProcesses.begin(), childProcesses.end(), matches)) return -2;
    }
    int encoded = 0;
    while (true) {
        std::int32_t reaped = 0;
        {
            std::lock_guard lock(childProcessMutex);
            for (std::size_t index = 0; index < childProcesses.size(); ++index) {
                auto& child = childProcesses[index];
                if (!matches(child)) continue;
                const HANDLE handle = reinterpret_cast<HANDLE>(child.processHandle);
                const DWORD state = WaitForSingleObject(handle, 0);
                if (state == WAIT_FAILED)
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "waitpid: WaitForSingleObject failed");
                if (state != WAIT_OBJECT_0) continue;
                DWORD code = 0;
                if (!GetExitCodeProcess(handle, &code)) {
                    *__error_nid_postfix() = 14;
                    return -1;
                }
                if (times != nullptr) {
                    FILETIME creation{};
                    FILETIME exitTime{};
                    FILETIME kernel{};
                    FILETIME user{};
                    if (!GetProcessTimes(handle, &creation, &exitTime, &kernel, &user))
                        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "wait4: GetProcessTimes failed");
                    times->user = (static_cast<std::uint64_t>(user.dwHighDateTime) << 32) | user.dwLowDateTime;
                    times->kernel = (static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
                }
                encoded = ChildProcessWaitStatus(code);
                CloseHandle(handle);
                reaped = child.processId;
                childProcesses.erase(childProcesses.begin() + static_cast<std::ptrdiff_t>(index));
                break;
            }
        }
        if (reaped != 0) {
            if (status != nullptr) *status = encoded;
            return reaped;
        }
        std::vector<OwnedProcessHandle> ownedHandles;
        {
            const std::lock_guard lock(childProcessMutex);
            for (const auto& child : childProcesses) {
                if (!matches(child)) continue;
                HANDLE copy = nullptr;
                if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(child.processHandle),
                    GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "waitpid: DuplicateHandle failed");
                OwnedProcessHandle duplicate(copy);
                ownedHandles.push_back(std::move(duplicate));
            }
        }
        if (ownedHandles.empty()) return -2;
        if (noWait) return 0;
        std::vector<HANDLE> handles;
        handles.reserve(ownedHandles.size());
        for (const auto& handle : ownedHandles) handles.push_back(handle.get());
        constexpr std::size_t groupSize = MAXIMUM_WAIT_OBJECTS;
        bool timedOut = true;
        for (std::size_t start = 0; start < handles.size() && timedOut; start += groupSize) {
            const std::size_t count = std::min(handles.size() - start, groupSize);
            const DWORD state = WaitForMultipleObjects(static_cast<DWORD>(count), handles.data() + start, FALSE, 250);
            if (state == WAIT_FAILED)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "waitpid: WaitForMultipleObjects failed");
            timedOut = state == WAIT_TIMEOUT;
        }
    }
}
#endif
