#pragma once

#include "telemetry/Gpu.h"
#include "config/Settings.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Overlay::Telemetry
{
    struct Snapshot
    {
        std::optional<double> cpuUsage;
        std::optional<double> ramUsedGiB;
        std::optional<double> ramTotalGiB;
        std::vector<GpuReading> gpus;
        std::uint64_t sampledAt{};
    };

    bool Start();
    // Nonblocking: keep the caller's previous snapshot if publishing is in progress.
    void Read(Snapshot& destination);
    enum class SaveState { Idle, Saving, Saved, Failed };
    bool QueueSave(const Config::Settings& settings);
    SaveState GetSaveState();
}
