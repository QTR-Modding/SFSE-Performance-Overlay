#include "ui/Overlay.h"
#include "ui/Layout.h"
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
            SaveStatus();
        }

        void __stdcall AppearanceSettings()
        {
            Gui::SeparatorText("Appearance");
            Changed(Gui::Checkbox("Show overlay", &settings.enabled));
            Gui::TextDisabled("Hidden: hardware sampling and frame measurements are paused.");
            Changed(Gui::Checkbox("Follow SFSE-MF theme", &settings.followFrameworkTheme));
            Gui::TextWrapped("Uses the framework's colors. Turn off for the original dark overlay palette.");
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
            Changed(Gui::SliderFloat("Layout", &settings.layout, 0, 1, "%.2f"));
            Gui::TextDisabled("Vertical  <---->  Horizontal");
            Gui::TextWrapped("Stacked sections become a compact strip. Readings wrap to fit without shrinking the text.");
            Changed(Gui::Checkbox("Section headings", &settings.showHeadings));
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
            Gui::TextDisabled("%s", label);
            Gui::SameLine();
            const auto width = Gui::CalcTextSize(value.c_str()).x;
            const float offset = std::max(0.0F, Gui::GetContentRegionAvail().x - width);
            Gui::SetCursorPosX(Gui::GetCursorPosX() + offset);
            Gui::TextUnformatted(value.c_str());
        }

        struct Reading
        {
            const char* label{};
            std::string value;
            const char* reserve{};

            float Width() const
            {
                // Reserve stable numeric space so changing digits do not shuffle readings.
                return Gui::CalcTextSize(label).x + Gui::GetFontSize() * 0.3F +
                    std::max(Gui::CalcTextSize(value.c_str()).x, Gui::CalcTextSize(reserve).x);
            }
        };

        struct Section
        {
            const char* title{};
            std::array<Reading, 6> readings{};
            int count{};
            bool graph{};

            void Add(const char* label, std::string value, const char* reserve)
            {
                readings[count++] = {label, std::move(value), reserve};
            }

            void Memory(const char* label, std::optional<double> used, std::optional<double> total)
            {
                Add(label, used && total ? std::format("{:.1f} / {:.1f} GiB", *used, *total) : "--",
                    "99.9 / 99.9 GiB");
            }

            float InlineWidth() const
            {
                const float gap = Gui::GetFontSize();
                float width = graph ? gap * 8 : 0;
                for (int i = 0; i < count; ++i) width += readings[i].Width();
                width += gap * std::max(0, count + static_cast<int>(graph) - 1);
                if (settings.showHeadings) width = std::max(width, Gui::CalcTextSize(title).x);
                return std::max(gap, width);
            }
        };

        void Graph(Gui::ImVec2 size, bool footer)
        {
            const auto start = Gui::GetCursorScreenPos();
            auto* draw = Gui::GetWindowDrawList();
            const auto background = settings.followFrameworkTheme ? Gui::GetColorU32(Gui::ImGuiCol_FrameBg, 0.4F) : IM_COL32(4, 10, 18, 100);
            Draw::AddRectFilled(draw, start, {start.x + size.x, start.y + size.y}, background, 0, 0);
            const float reference = 16.667F;
            if (reference < settings.graphCeiling) {
                const auto y = start.y + size.y * (1 - reference / settings.graphCeiling);
                const auto guide = settings.followFrameworkTheme ? Gui::GetColorU32(Gui::ImGuiCol_Border, 0.5F) : IM_COL32(140, 170, 190, 70);
                Draw::AddLine(draw, {start.x, y}, {start.x + size.x, y}, guide, 1);
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
                const auto color = settings.followFrameworkTheme ? Gui::GetColorU32(
                    values[i] > 16.667F ? Gui::ImGuiCol_PlotLinesHovered : Gui::ImGuiCol_PlotLines) :
                    (values[i] > 33.334F ? IM_COL32(244, 135, 101, 255) :
                        (values[i] > 16.667F ? IM_COL32(234, 201, 125, 255) : IM_COL32(110, 212, 235, 255)));
                if (previousValid) Draw::AddLine(draw, previous, point, color, 1.5F);
                previous = point;
                previousValid = true;
            }
            Gui::Dummy(size);
            if (footer) Row("FRAME TIME", std::format("{:.0f}s  /  {:.0f} ms", settings.historySeconds, settings.graphCeiling));
        }

        void RenderSection(const Section& section, float compact, const Gui::ImVec4& accent)
        {
            if (settings.showHeadings) Gui::TextColored(accent, "%s", section.title);
            const auto start = Gui::GetCursorScreenPos();
            const float available = std::max(1.0F, Gui::GetContentRegionAvail().x);
            const float font = Gui::GetFontSize();
            FlowLayout flow{available, font, Gui::GetStyle()->ItemSpacing.y};
            for (int i = 0; i < section.count; ++i) {
                const auto& reading = section.readings[i];
                const float width = std::min(available, std::lerp(available, reading.Width(), compact));
                const auto position = flow.Place(width);
                Gui::SetCursorScreenPos({start.x + position.x, start.y + position.y});
                Gui::BeginGroup();
                if (reading.Width() <= width + 0.01F) {
                    Gui::TextDisabled("%s", reading.label);
                    Gui::SameLine(0, font * 0.3F);
                    Gui::SetCursorScreenPos({start.x + position.x + width -
                        Gui::CalcTextSize(reading.value.c_str()).x, Gui::GetCursorScreenPos().y});
                    Gui::TextUnformatted(reading.value.c_str());
                } else {
                    Gui::PushTextWrapPos(Gui::GetCursorPosX() + width);
                    Gui::TextWrapped("%s", reading.label);
                    Gui::TextWrapped("%s", reading.value.c_str());
                    Gui::PopTextWrapPos();
                }
                Gui::EndGroup();
                flow.Advance(width, Gui::GetItemRectSize().y);
            }
            if (section.graph) {
                const float width = std::lerp(available, std::min(available, font * 8), compact);
                const auto position = flow.Place(width);
                Gui::SetCursorScreenPos({start.x + position.x, start.y + position.y});
                Gui::BeginGroup();
                Graph({width, font * std::lerp(3.5F, 1.2F, compact)}, compact < 0.5F);
                Gui::EndGroup();
                flow.Advance(width, Gui::GetItemRectSize().y);
            }
            Gui::SetCursorScreenPos(start);
            Gui::Dummy({available, flow.Height()});
        }

        void __stdcall Render()
        {
            Telemetry::SetEnabled(settings.enabled);
            const auto now = GetTickCount64();
            if (dirty && now - changedAt >= 700 && !Gui::IsAnyItemActive()) {
                if (Telemetry::QueueSave(settings)) dirty = false;
            }
            if (!settings.enabled) {
                lastCounter = {};
                history.Clear();
                statistics = {};
                return;
            }
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            if (lastCounter.QuadPart != 0 && frequency.QuadPart > 0) {
                history.Push(static_cast<double>(counter.QuadPart - lastCounter.QuadPart) * 1000 /
                    static_cast<double>(frequency.QuadPart), settings.historySeconds);
            }
            lastCounter = counter;
            if (now - lastSummary >= 500) {
                statistics = history.Summarize();
                statistics.meanMs = history.RecentMeanMs();
                statistics.fps = statistics.meanMs > 0 ? 1000 / statistics.meanMs : 0;
                Telemetry::Read(telemetry);
                lastSummary = now;
            }

            const auto screen = Gui::GetIO()->DisplaySize;
            const bool right = (settings.corner & 1) != 0;
            const bool bottom = (settings.corner & 2) != 0;
            const float fontSize = Gui::GetFontSize() * settings.scale;
            const float margin = std::min(settings.margin, std::min(screen.x, screen.y) * 0.2F);
            std::array<Section, 3> sections{};
            int sectionCount = 1;
            auto& performance = sections[0];
            performance.title = "PERFORMANCE";
            performance.graph = settings.graph;
            if (settings.fps) performance.Add("FPS", statistics.count ? std::format("{:.0f}", statistics.fps) : "--", "999");
            if (settings.frameTime) performance.Add("Frame", statistics.count ? std::format("{:.2f} ms", statistics.meanMs) : "--", "99.99 ms");
            if (settings.lowOnePercent) performance.Add("1% low", statistics.count >= 100 ? std::format("{:.0f} FPS", statistics.lowOnePercent) : "--", "999 FPS");
            if (settings.peak) performance.Add("Peak", statistics.count ? std::format("{:.2f} ms", statistics.peakMs) : "--", "99.99 ms");
            const bool fresh = telemetry.sampledAt && now - telemetry.sampledAt < 3000;
            if (settings.cpuUsage || settings.ram) {
                auto& cpu = sections[sectionCount++];
                cpu.title = "CPU + RAM";
                if (settings.cpuUsage) cpu.Add("CPU", Value(fresh ? telemetry.cpuUsage : std::nullopt, "%"), "100%");
                if (settings.ram) cpu.Memory("RAM", fresh ? telemetry.ramUsedGiB : std::nullopt, telemetry.ramTotalGiB);
            }
            if (settings.gpuUsage || settings.gpuTemperature || settings.gpuPower ||
                settings.gpuClock || settings.vram || settings.gameVram) {
                auto& gpuSection = sections[sectionCount++];
                gpuSection.title = "GPU";
                const Telemetry::GpuReading empty;
                const auto* selected = SelectedGpu();
                const auto& gpu = fresh && selected ? *selected : empty;
                if (settings.gpuUsage) gpuSection.Add("GPU", Value(gpu.usage, "%"), "100%");
                if (settings.gpuTemperature) gpuSection.Add("Temp", Value(gpu.temperature, " C"), "999 C");
                if (settings.gpuPower) gpuSection.Add("Power", Value(gpu.watts, " W"), "999 W");
                if (settings.gpuClock) gpuSection.Add("Clock", Value(gpu.clockMHz, " MHz"), "9999 MHz");
                if (settings.vram) gpuSection.Memory("VRAM", gpu.boardMemoryGiB, gpu.boardTotalGiB);
                if (settings.gameVram) gpuSection.Memory("Game VRAM", gpu.gameMemoryGiB, gpu.gameBudgetGiB);
            }
            // Leave a little slack for per-column pixel rounding.
            float stripWidth = fontSize * (1.2F + 0.8F * static_cast<float>(sectionCount)) + 2 * sectionCount;
            for (int i = 0; i < sectionCount; ++i) stripWidth += sections[i].InlineWidth() * settings.scale;
            const auto layout = CalculateLayout(screen.x - margin * 2, fontSize * settings.width,
                settings.layout, sectionCount, stripWidth);
            Gui::SetNextWindowPos({right ? screen.x - margin : margin, bottom ? screen.y - margin : margin},
                Gui::ImGuiCond_Always, {right ? 1.0F : 0, bottom ? 1.0F : 0});
            Gui::SetNextWindowSize({layout.width, 0});
            Gui::SetNextWindowBgAlpha(settings.opacity);
            const bool ownTheme = !settings.followFrameworkTheme;
            if (ownTheme) {
                Gui::PushStyleColor(Gui::ImGuiCol_WindowBg, {0.025F, 0.045F, 0.075F, 1});
                Gui::PushStyleColor(Gui::ImGuiCol_Text, {0.9F, 0.94F, 0.96F, 1});
                Gui::PushStyleColor(Gui::ImGuiCol_Border, {0.25F, 0.4F, 0.49F, 0.65F});
                Gui::PushStyleColor(Gui::ImGuiCol_TextDisabled, muted);
                Gui::PushStyleColor(Gui::ImGuiCol_CheckMark, cyan);
            }
            const auto accent = *Gui::GetStyleColorVec4(Gui::ImGuiCol_CheckMark);
            Gui::PushStyleVar(Gui::ImGuiStyleVar_WindowPadding, {fontSize * 0.6F, fontSize * 0.5F});
            Gui::PushStyleVar(Gui::ImGuiStyleVar_ItemSpacing, {fontSize * 0.4F, fontSize * 0.14F});
            Gui::PushStyleVar(Gui::ImGuiStyleVar_WindowRounding, fontSize * 0.25F);
            constexpr auto flags = Gui::ImGuiWindowFlags_NoDecoration | Gui::ImGuiWindowFlags_AlwaysAutoResize |
                Gui::ImGuiWindowFlags_NoInputs | Gui::ImGuiWindowFlags_NoSavedSettings |
                Gui::ImGuiWindowFlags_NoFocusOnAppearing;
            if (Gui::Begin("Performance Overlay###SFSEPerformanceOverlay", nullptr, flags)) {
                Gui::SetWindowFontScale(settings.scale);
                Gui::PushStyleVar(Gui::ImGuiStyleVar_CellPadding, {layout.columns > 1 ? fontSize * 0.4F : 0, 0});
                if (Gui::BeginTable("Sections", layout.columns, Gui::ImGuiTableFlags_SizingStretchProp | Gui::ImGuiTableFlags_BordersInnerV)) {
                    for (int i = 0; i < layout.columns; ++i) {
                        const float weight = layout.columns == sectionCount ?
                            std::lerp(fontSize * settings.width, sections[i].InlineWidth(), layout.compact) : 1.0F;
                        Gui::TableSetupColumn(sections[i].title, Gui::ImGuiTableColumnFlags_WidthStretch, weight);
                    }
                    for (int i = 0; i < sectionCount; ++i) {
                        Gui::TableNextColumn();
                        if (i > 0 && layout.columns == 1) Gui::Spacing();
                        RenderSection(sections[i], layout.compact, accent);
                    }
                    Gui::EndTable();
                }
                Gui::PopStyleVar();
            }
            Gui::End();
            Gui::PopStyleVar(3);
            if (ownTheme) Gui::PopStyleColor(5);
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
        if (!Telemetry::Start(settings.enabled)) logger::warn("Telemetry worker unavailable.");
        SFSEMenuFramework::SetSection("Performance Overlay");
        SFSEMenuFramework::AddSectionItem("Appearance", AppearanceSettings);
        SFSEMenuFramework::AddSectionItem("CPU", CpuSettings);
        SFSEMenuFramework::AddSectionItem("GPU", GpuSettings);
        SFSEMenuFramework::AddSectionItem("Performance", PerformanceSettings);
        logger::info("Registered performance HUD and CPU/GPU/Performance settings.");
        // Registration is intentionally process-lifetime, matching the SFSE plugin.
        return true;
    }
}
