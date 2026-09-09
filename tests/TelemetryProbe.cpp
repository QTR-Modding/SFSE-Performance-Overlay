#include "telemetry/Gpu.h"
#include <Windows.h>
#include <cmath>
#include <iostream>

int main()
{
    Overlay::Telemetry::GpuCollector collector;
    for (int sample = 0; sample < 3; ++sample) {
        const auto readings = collector.Poll();
        std::cout << "Sample " << sample << ": " << readings.size() << " adapters\n";
        for (const auto& gpu : readings) {
            std::cout << gpu.name;
            const auto print = [](const char* label, std::optional<double> value) {
                std::cout << " | " << label << '=';
                if (value) {
                    if (!std::isfinite(*value) || *value < 0) std::exit(1);
                    std::cout << *value;
                } else std::cout << "unavailable";
            };
            print("usage%", gpu.usage);
            print("C", gpu.temperature);
            print("W", gpu.watts);
            print("MHz", gpu.clockMHz);
            print("boardGiB", gpu.boardMemoryGiB);
            print("totalGiB", gpu.boardTotalGiB);
            print("processGiB", gpu.gameMemoryGiB);
            print("budgetGiB", gpu.gameBudgetGiB);
            std::cout << '\n';
        }
        if (sample < 2) Sleep(600);
    }
}
