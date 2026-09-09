#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Overlay::Telemetry
{
    struct GpuReading
    {
        std::string name;
        std::optional<double> usage, temperature, watts, clockMHz;
        std::optional<double> gameMemoryGiB, gameBudgetGiB, boardMemoryGiB, boardTotalGiB;
    };

    // Construct, poll and destroy on the telemetry worker, never the render thread.
    class GpuCollector
    {
    public:
        GpuCollector();
        ~GpuCollector();
        std::vector<GpuReading> Poll();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
