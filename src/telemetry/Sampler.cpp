#include "telemetry/Sampler.h"

#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cmath>
#include <mutex>
#include <atomic>
#include <new>
#include <algorithm>
#include <utility>

namespace Overlay::Telemetry
{
    namespace
    {
        struct Shared
        {
            std::mutex mutex;
            Snapshot latest;
            HANDLE wake{};
            std::atomic<bool> enabled{false};
            std::optional<Config::Settings> pendingSave;
            std::atomic<SaveState> saveState{SaveState::Idle};
        };
        Shared* shared{};

        DWORD WINAPI Poll(void* argument)
        {
            auto& destination = *static_cast<Shared*>(argument);
            std::unique_ptr<GpuCollector> gpu;
            PDH_HQUERY query{};
            PDH_HCOUNTER cpu{};
            for (;;) {
                std::optional<Config::Settings> save;
                {
                    const std::lock_guard lock(destination.mutex);
                    save = std::exchange(destination.pendingSave, std::nullopt);
                }
                if (save) {
                    const bool success = save->Save();
                    const std::lock_guard lock(destination.mutex);
                    if (!destination.pendingSave) {
                        destination.saveState.store(success ? SaveState::Saved : SaveState::Failed);
                    }
                }
                if (!destination.enabled.load()) {
                    gpu.reset();
                    if (query) PdhCloseQuery(query);
                    query = nullptr;
                    cpu = nullptr;
                    WaitForSingleObject(destination.wake, INFINITE);
                    continue;
                }
                if (!gpu) {
                    gpu = std::make_unique<GpuCollector>();
                    if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
                        if (PdhAddEnglishCounterW(query,
                            L"\\Processor Information(_Total)\\% Processor Time", 0, &cpu) != ERROR_SUCCESS) {
                            cpu = nullptr;
                        }
                        PdhCollectQueryData(query);
                    }
                }
                Snapshot next;
                if (query && cpu && PdhCollectQueryData(query) == ERROR_SUCCESS) {
                    PDH_FMT_COUNTERVALUE value{};
                    if (PdhGetFormattedCounterValue(cpu, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS &&
                        (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA) &&
                        std::isfinite(value.doubleValue)) {
                        next.cpuUsage = std::clamp(value.doubleValue, 0.0, 100.0);
                    }
                }
                MEMORYSTATUSEX memory{sizeof(MEMORYSTATUSEX)};
                if (GlobalMemoryStatusEx(&memory)) {
                    constexpr double gib = 1024.0 * 1024 * 1024;
                    next.ramUsedGiB = static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys) / gib;
                    next.ramTotalGiB = static_cast<double>(memory.ullTotalPhys) / gib;
                }
                next.gpus = gpu->Poll();
                next.sampledAt = GetTickCount64();
                {
                    const std::lock_guard lock(destination.mutex);
                    destination.latest = std::move(next);
                }
                WaitForSingleObject(destination.wake, 500);
            }
        }
    }

    bool Start(bool enabled)
    {
        if (shared) return true;
        auto* candidate = new (std::nothrow) Shared;
        if (!candidate) return false;
        candidate->enabled.store(enabled);
        candidate->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!candidate->wake) {
            delete candidate;
            return false;
        }
        const auto thread = CreateThread(nullptr, 0, Poll, candidate, 0, nullptr);
        if (!thread) {
            CloseHandle(candidate->wake);
            delete candidate;
            return false;
        }
        CloseHandle(thread);
        // SFSE plugins live until process exit. Keep worker state alive too; never
        // join a worker or call driver shutdown from a DLL detach destructor.
        shared = candidate;
        return true;
    }

    void SetEnabled(bool enabled)
    {
        if (shared && shared->enabled.exchange(enabled) != enabled) {
            SetEvent(shared->wake);
        }
    }

    bool QueueSave(const Config::Settings& settings)
    {
        if (!shared) return false;
        const std::unique_lock lock(shared->mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        shared->pendingSave = settings;
        shared->saveState.store(SaveState::Saving);
        SetEvent(shared->wake);
        return true;
    }

    SaveState GetSaveState()
    {
        return shared ? shared->saveState.load() : SaveState::Failed;
    }

    void Read(Snapshot& destination)
    {
        if (!shared) return;
        const std::unique_lock lock(shared->mutex, std::try_to_lock);
        if (lock.owns_lock() && destination.sampledAt != shared->latest.sampledAt) {
            destination = shared->latest;
        }
    }
}
