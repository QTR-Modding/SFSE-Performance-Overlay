#include "telemetry/Sampler.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>

int main()
{
    namespace T = Overlay::Telemetry;
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    const auto directory = std::filesystem::path(exe).parent_path() / L"Data/SFSE/Plugins";
    const auto file = directory / L"SFSEPerformanceOverlay.ini";
    if (std::filesystem::exists(file)) {
        std::cerr << "Refusing to overwrite an existing probe settings file\n";
        return 1;
    }
    std::filesystem::create_directories(directory);
    if (!T::Start(false)) return 2;
    T::Snapshot snapshot;
    Sleep(700);
    T::Read(snapshot);
    if (snapshot.sampledAt != 0) return 9;
    T::SetEnabled(true);
    for (int i = 0; i < 30; ++i) {
        Sleep(100);
        T::Read(snapshot);
        if (snapshot.cpuUsage && snapshot.ramUsedGiB) break;
    }
    if (!snapshot.cpuUsage || !snapshot.ramUsedGiB || snapshot.gpus.empty()) return 3;
    std::cout << "CPU " << *snapshot.cpuUsage << "% | RAM " << *snapshot.ramUsedGiB << " GiB\n";
    T::SetEnabled(false);
    Sleep(800); // Allow a query already in progress to finish.
    T::Read(snapshot);
    const auto pausedAt = snapshot.sampledAt;
    Sleep(700);
    T::Read(snapshot);
    if (snapshot.sampledAt != pausedAt) return 6;
    Overlay::Config::Settings requested;
    if (!requested.followFrameworkTheme || !requested.showHeadings) return 10;
    requested.showHeadings = false;
    requested.followFrameworkTheme = false;
    requested.opacity = 0.37F;
    requested.scale = 1.25F;
    requested.layout = 0.75F;
    requested.adapter = -1;
    requested.cpuUsage = false;
    requested.fps = false;
    requested.graph = false;
    requested.corner = 3;
    requested.gpuClock = true;
    for (int i = 0; i < 30 && !T::QueueSave(requested); ++i) Sleep(100);
    for (int i = 0; i < 30 && T::GetSaveState() != T::SaveState::Saved; ++i) Sleep(100);
    if (T::GetSaveState() != T::SaveState::Saved) return 4;
    Overlay::Config::Settings actual;
    actual.Load();
    const bool valid = actual.opacity == requested.opacity && actual.scale == requested.scale &&
        actual.adapter == -1 && !actual.cpuUsage && !actual.fps && !actual.graph &&
        actual.corner == 3 && actual.gpuClock && actual.layout == requested.layout && !actual.followFrameworkTheme && !actual.showHeadings;
    T::Read(snapshot);
    if (snapshot.sampledAt != pausedAt) return 7; // Saving while hidden must not poll.
    T::SetEnabled(true);
    for (int i = 0; i < 30 && snapshot.sampledAt == pausedAt; ++i) {
        Sleep(100);
        T::Read(snapshot);
    }
    if (snapshot.sampledAt == pausedAt) return 8;
    std::filesystem::remove(file);
    if (!valid) return 5;
    std::cout << "Background sampling and asynchronous settings round-trip passed\n";
}
