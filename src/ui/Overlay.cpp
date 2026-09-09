#include "ui/Overlay.h"
#include "config/Settings.h"
#include "performance/FrameHistory.h"
#include "telemetry/Sampler.h"

#include <SFSEMCP/SFSEMenuFramework.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace Overlay::UI
{
    namespace
    {
        namespace Gui = ImGuiMCP;
        namespace Draw = ImGuiMCP::ImDrawListManager;
        Config::Settings settings;
        Performance::FrameHistory history;
        Performance::Statistics statistics;
        Telemetry::Snapshot telemetry;
        SFSEMenuFramework::Model::HudElement* registration{};
        LARGE_INTEGER frequency{}, lastCounter{};
        ULONGLONG lastSummary{}, changedAt{};
        bool dirty{};
        constexpr Gui::ImVec4 cyan{0.43F, 0.83F, 0.92F, 1};
        constexpr Gui::ImVec4 muted{0.58F, 0.66F, 0.73F, 1};

        std::string Value(std::optional<double> value, const char* unit, int precision = 0)
        {
            return value ? std::format("{:.{}f}{}", *value, precision, unit) : "--";
        }

        void Changed(bool changed)
        {
            if (changed) {
                dirty = true;
                changedAt = GetTickCount64();
            }
        }

        void SaveStatus()
        {
            if (Telemetry::GetSaveState() == Telemetry::SaveState::Failed) {
                Gui::TextColored({1, 0.55F, 0.45F, 1}, "Could not save settings.");
                if (Gui::Button("Retry saving")) Changed(true);
            } else if (dirty || Telemetry::GetSaveState() == Telemetry::SaveState::Saving) {
                Gui::TextDisabled("Changes are live; saving after editing.");
            }
        }

        void __stdcall CpuSettings()
        {
            Gui::SeparatorText("CPU + RAM");
            Changed(Gui::Checkbox("CPU usage", &settings.cpuUsage));
            Changed(Gui::Checkbox("System RAM usage", &settings.ram));
            Gui::Spacing();
            Gui::TextDisabled("CPU temperature and power: no sensor provider connected.");
            Gui::TextWrapped("Usage and RAM cover the whole system, not only Starfield.");
            SaveStatus();
        }

        const Telemetry::GpuReading* SelectedGpu()
        {
            if (telemetry.gpus.empty()) return nullptr;
            if (settings.adapter >= 0) {
                const auto index = static_cast<std::size_t>(settings.adapter);
                return index < telemetry.gpus.size() ? &telemetry.gpus[index] : nullptr;
            }
            return &*std::max_element(telemetry.gpus.begin(), telemetry.gpus.end(),
                [](const auto& left, const auto& right) {
                    return left.gameMemoryGiB.value_or(0) < right.gameMemoryGiB.value_or(0);
                });
        }

        void __stdcall GpuSettings()
        {
            Gui::SeparatorText("GPU");
            const auto* gpu = SelectedGpu();
            const char* preview = settings.adapter < 0 ? "Automatic (most game VRAM)" :
                (gpu ? gpu->name.c_str() : "Selected GPU unavailable");
            if (Gui::BeginCombo("Adapter", preview)) {
                if (Gui::Selectable("Automatic (most game VRAM)", settings.adapter == -1)) {
                    settings.adapter = -1;
                    Changed(true);
                }
                for (std::size_t i = 0; i < telemetry.gpus.size(); ++i) {
                    const auto label = std::format("{}: {}", i + 1, telemetry.gpus[i].name);
                    if (Gui::Selectable(label.c_str(), settings.adapter == static_cast<int>(i))) {
                        settings.adapter = static_cast<int>(i);
                        Changed(true);
                    }
                }
                Gui::EndCombo();
            }
            Changed(Gui::Checkbox("GPU usage", &settings.gpuUsage));
            Changed(Gui::Checkbox("Temperature", &settings.gpuTemperature));
            Changed(Gui::Checkbox("Power draw", &settings.gpuPower));
            Changed(Gui::Checkbox("Clock", &settings.gpuClock));
            Changed(Gui::Checkbox("Total VRAM usage", &settings.vram));
            Changed(Gui::Checkbox("Game VRAM / game budget", &settings.gameVram));
            Gui::Spacing();
            if (gpu) {
                Gui::TextUnformatted(gpu->name.c_str());
                const auto status = std::format("Temperature: {}   Power: {}   Clock: {}",
                    Value(gpu->temperature, " C"), Value(gpu->watts, " W"), Value(gpu->clockMHz, " MHz"));
                Gui::TextWrapped("%s", status.c_str());
            }
            Gui::TextWrapped("-- means unavailable. Sensors depend on your GPU and driver. "
                "Windows counters provide usage and memory; NVIDIA drivers can also provide temperature and power.");
            SaveStatus();
        }

        void __stdcall PerformanceSettings()
        {
            Gui::SeparatorText("Performance");
            Changed(Gui::Checkbox("Show overlay", &settings.enabled));
            Changed(Gui::Checkbox("FPS", &settings.fps));
            Changed(Gui::Checkbox("Frame time", &settings.frameTime));
            Changed(Gui::Checkbox("Frame-time graph", &settings.graph));
            Changed(Gui::Checkbox("1% low FPS", &settings.lowOnePercent));
            Changed(Gui::Checkbox("Peak frame time", &settings.peak));
            Changed(Gui::SliderFloat("History", &settings.historySeconds, 1, 60, "%.0f seconds"));
            Changed(Gui::SliderFloat("Graph ceiling", &settings.graphCeiling, 8, 200, "%.0f ms"));
            Gui::TextWrapped("FPS measures framework render cadence, excluding generated frames. "
                "The 1%% low is the reciprocal of the slowest 1%% average frame time over this history.");
            if (Gui::Button("Reset measurements")) {
                history.Clear();
                statistics = {};
                lastCounter = {};
            }
            Gui::SeparatorText("Appearance");
            constexpr const char* corners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
            if (Gui::BeginCombo("Position", corners[settings.corner])) {
                for (int i = 0; i < 4; ++i) {
                    if (Gui::Selectable(corners[i], settings.corner == i)) {
                        settings.corner = i;
                        Changed(true);
                    }
                }
                Gui::EndCombo();
            }
            Changed(Gui::SliderFloat("Size", &settings.scale, 0.4F, 2, "%.2fx"));
            Changed(Gui::SliderFloat("Width", &settings.width, 12, 32, "%.0f"));
            Changed(Gui::SliderFloat("Background opacity", &settings.opacity, 0, 1, "%.2f"));
            Changed(Gui::SliderFloat("Screen margin", &settings.margin, 0, 200, "%.0f px"));
            if (Gui::Button("Restore defaults")) {
                settings = {};
                Changed(true);
            }
            SaveStatus();
        }

        void Row(const char* label, const std::string& value)
        {
            Gui::TextColored(muted, "%s", label);
            Gui::SameLine();
            const auto width = Gui::CalcTextSize(value.c_str()).x;
            const float offset = std::max(0.0F, Gui::GetContentRegionAvail().x - width);
            Gui::SetCursorPosX(Gui::GetCursorPosX() + offset);
            Gui::TextUnformatted(value.c_str());
        }

        void MemoryRow(const char* label, std::optional<double> used, std::optional<double> total)
        {
            Row(label, used && total ? std::format("{:.1f} / {:.1f} GiB", *used, *total) : "--");
        }

        void Graph()
        {
            const auto start = Gui::GetCursorScreenPos();
            const Gui::ImVec2 size{Gui::GetContentRegionAvail().x, Gui::GetFontSize() * 3.5F};
            auto* draw = Gui::GetWindowDrawList();
            Draw::AddRectFilled(draw, start, {start.x + size.x, start.y + size.y}, IM_COL32(4, 10, 18, 100), 0, 0);
            const float reference = 16.667F;
            if (reference < settings.graphCeiling) {
                const auto y = start.y + size.y * (1 - reference / settings.graphCeiling);
                Draw::AddLine(draw, {start.x, y}, {start.x + size.x, y}, IM_COL32(140, 170, 190, 70), 1);
            }
            std::array<float, 512> values;
            history.Graph(values, settings.historySeconds);
            bool previousValid = false;
            Gui::ImVec2 previous{};
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (values[i] < 0) continue;
                const Gui::ImVec2 point{
                    start.x + static_cast<float>(i) * size.x / static_cast<float>(values.size() - 1),
                    start.y + size.y * (1 - std::min(values[i] / settings.graphCeiling, 1.0F))};
                const auto color = values[i] > 33.334F ? IM_COL32(244, 135, 101, 255) :
                    (values[i] > 16.667F ? IM_COL32(234, 201, 125, 255) : IM_COL32(110, 212, 235, 255));
                if (previousValid) Draw::AddLine(draw, previous, point, color, 1.5F);
                previous = point;
                previousValid = true;
            }
            Gui::Dummy(size);
            Row("FRAME TIME", std::format("{:.0f}s  /  {:.0f} ms", settings.historySeconds, settings.graphCeiling));
        }

        void __stdcall Render()
        {
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            if (lastCounter.QuadPart != 0 && frequency.QuadPart > 0) {
                history.Push(static_cast<double>(counter.QuadPart - lastCounter.QuadPart) * 1000 /
                    static_cast<double>(frequency.QuadPart), settings.historySeconds);
            }
            lastCounter = counter;
            const auto now = GetTickCount64();
            if (now - lastSummary >= 500) {
                statistics = history.Summarize();
                statistics.meanMs = history.RecentMeanMs();
                statistics.fps = statistics.meanMs > 0 ? 1000 / statistics.meanMs : 0;
                Telemetry::Read(telemetry);
                lastSummary = now;
            }
            if (dirty && now - changedAt >= 700 && !Gui::IsAnyItemActive()) {
                if (Telemetry::QueueSave(settings)) dirty = false;
            }
            if (!settings.enabled) return;

            const auto screen = Gui::GetIO()->DisplaySize;
            const bool right = (settings.corner & 1) != 0;
            const bool bottom = (settings.corner & 2) != 0;
            const float fontSize = Gui::GetFontSize() * settings.scale;
            const float margin = std::min(settings.margin, std::min(screen.x, screen.y) * 0.2F);
            Gui::SetNextWindowPos({right ? screen.x - margin : margin, bottom ? screen.y - margin : margin},
                Gui::ImGuiCond_Always, {right ? 1.0F : 0, bottom ? 1.0F : 0});
            Gui::SetNextWindowSize({std::min(fontSize * settings.width, screen.x - margin * 2), 0});
            Gui::SetNextWindowBgAlpha(settings.opacity);
            Gui::PushStyleColor(Gui::ImGuiCol_WindowBg, {0.025F, 0.045F, 0.075F, 1});
            Gui::PushStyleColor(Gui::ImGuiCol_Text, {0.9F, 0.94F, 0.96F, 1});
            Gui::PushStyleColor(Gui::ImGuiCol_Border, {0.25F, 0.4F, 0.49F, 0.65F});
            Gui::PushStyleVar(Gui::ImGuiStyleVar_WindowPadding, {fontSize * 0.6F, fontSize * 0.5F});
            Gui::PushStyleVar(Gui::ImGuiStyleVar_ItemSpacing, {fontSize * 0.4F, fontSize * 0.14F});
            Gui::PushStyleVar(Gui::ImGuiStyleVar_WindowRounding, fontSize * 0.25F);
            constexpr auto flags = Gui::ImGuiWindowFlags_NoDecoration | Gui::ImGuiWindowFlags_AlwaysAutoResize |
                Gui::ImGuiWindowFlags_NoInputs | Gui::ImGuiWindowFlags_NoSavedSettings |
                Gui::ImGuiWindowFlags_NoFocusOnAppearing;
            if (Gui::Begin("Performance Overlay###SFSEPerformanceOverlay", nullptr, flags)) {
                Gui::SetWindowFontScale(settings.scale);
                Gui::TextColored(cyan, "PERFORMANCE");
                if (settings.fps) Row("FPS", statistics.count ? std::format("{:.0f}", statistics.fps) : "--");
                if (settings.frameTime) Row("Frame time", statistics.count ? std::format("{:.2f} ms", statistics.meanMs) : "--");
                if (settings.lowOnePercent) Row("1% low", statistics.count >= 100 ? std::format("{:.0f} FPS", statistics.lowOnePercent) : "warming up");
                if (settings.peak) Row("Peak", statistics.count ? std::format("{:.2f} ms", statistics.peakMs) : "--");
                if (settings.graph) Graph();
                const bool fresh = telemetry.sampledAt && now - telemetry.sampledAt < 3000;
                if (settings.cpuUsage || settings.ram) {
                    Gui::Spacing();
                    Gui::TextColored(cyan, "CPU + RAM");
                    if (settings.cpuUsage) Row("CPU", Value(fresh ? telemetry.cpuUsage : std::nullopt, "%"));
                    if (settings.ram) MemoryRow("RAM", fresh ? telemetry.ramUsedGiB : std::nullopt, telemetry.ramTotalGiB);
                }
                if (settings.gpuUsage || settings.gpuTemperature || settings.gpuPower || settings.gpuClock || settings.vram || settings.gameVram) {
                    Gui::Spacing();
                    Gui::TextColored(cyan, "GPU");
                    const Telemetry::GpuReading empty;
                    const auto* selected = SelectedGpu();
                    const auto& gpu = fresh && selected ? *selected : empty;
                    if (settings.gpuUsage) Row("Usage", Value(gpu.usage, "%"));
                    if (settings.gpuTemperature) Row("Temperature", Value(gpu.temperature, " C"));
                    if (settings.gpuPower) Row("Power", Value(gpu.watts, " W"));
                    if (settings.gpuClock) Row("Clock", Value(gpu.clockMHz, " MHz"));
                    if (settings.vram) MemoryRow("VRAM", gpu.boardMemoryGiB, gpu.boardTotalGiB);
                    if (settings.gameVram) MemoryRow("Game VRAM", gpu.gameMemoryGiB, gpu.gameBudgetGiB);
                }
            }
            Gui::End();
            Gui::PopStyleVar(3);
            Gui::PopStyleColor(3);
        }
    }

    bool Register()
    {
        if (registration) return true;
        if (!SFSEMenuFramework::IsInstalled()) return false;
        settings.Load();
        QueryPerformanceFrequency(&frequency);
        registration = SFSEMenuFramework::AddHudElement(Render);
        if (!registration) return false;
        if (!Telemetry::Start()) logger::warn("Telemetry worker unavailable.");
        SFSEMenuFramework::SetSection("Performance Overlay");
        SFSEMenuFramework::AddSectionItem("CPU", CpuSettings);
        SFSEMenuFramework::AddSectionItem("GPU", GpuSettings);
        SFSEMenuFramework::AddSectionItem("Performance", PerformanceSettings);
        logger::info("Registered performance HUD and CPU/GPU/Performance settings.");
        // Registration is intentionally process-lifetime, matching the SFSE plugin.
        return true;
    }
}
