#include "config/Settings.h"

#include <Windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace Overlay::Config
{
    namespace
    {
        std::wstring Path()
        {
            std::array<wchar_t, 32768> exe{};
            const auto size = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
            if (size == 0 || size >= exe.size()) return {};
            return (std::filesystem::path(exe.data()).parent_path() /
                L"Data/SFSE/Plugins/SFSEPerformanceOverlay.ini").wstring();
        }

        struct Field
        {
            const wchar_t* section;
            const wchar_t* key;
            bool Settings::* value;
        };
        constexpr Field booleans[] = {
            {L"Performance", L"Enabled", &Settings::enabled},
            {L"Performance", L"FPS", &Settings::fps},
            {L"Performance", L"FrameTime", &Settings::frameTime},
            {L"Performance", L"Graph", &Settings::graph},
            {L"Performance", L"LowOnePercent", &Settings::lowOnePercent},
            {L"Performance", L"Peak", &Settings::peak},
            {L"CPU", L"Usage", &Settings::cpuUsage},
            {L"CPU", L"RAM", &Settings::ram},
            {L"GPU", L"Usage", &Settings::gpuUsage},
            {L"GPU", L"Temperature", &Settings::gpuTemperature},
            {L"GPU", L"Power", &Settings::gpuPower},
            {L"GPU", L"Clock", &Settings::gpuClock},
            {L"GPU", L"VRAM", &Settings::vram},
            {L"GPU", L"GameVRAM", &Settings::gameVram}
        };
        struct Number
        {
            const wchar_t* key;
            float Settings::* value;
            float minimum, maximum;
        };
        constexpr Number numbers[] = {
            {L"Opacity", &Settings::opacity, 0, 1},
            {L"Scale", &Settings::scale, 0.4F, 2},
            {L"Width", &Settings::width, 12, 32},
            {L"Layout", &Settings::layout, 0, 1},
            {L"Margin", &Settings::margin, 0, 200},
            {L"HistorySeconds", &Settings::historySeconds, 1, 60},
            {L"GraphCeiling", &Settings::graphCeiling, 8, 200}
        };
    }

    void Settings::Load()
    {
        const auto path = Path();
        if (path.empty()) return;
        for (const auto& field : booleans) {
            this->*field.value = GetPrivateProfileIntW(field.section, field.key,
                this->*field.value ? 1 : 0, path.c_str()) != 0;
        }
        for (const auto& field : numbers) {
            wchar_t buffer[64]{};
            GetPrivateProfileStringW(L"Performance", field.key, L"", buffer, 64, path.c_str());
            if (!buffer[0]) continue;
            wchar_t* end{};
            const auto value = std::wcstof(buffer, &end);
            if (end != buffer && *end == 0 && std::isfinite(value)) {
                this->*field.value = std::clamp(value, field.minimum, field.maximum);
            }
        }
        corner = std::clamp(static_cast<int>(GetPrivateProfileIntW(
            L"Performance", L"Corner", corner, path.c_str())), 0, 3);
        adapter = std::clamp(static_cast<int>(GetPrivateProfileIntW(
            L"GPU", L"Adapter", adapter, path.c_str())), -1, 31);
    }

    bool Settings::Save() const
    {
        const auto path = Path();
        if (path.empty()) return false;
        bool success = true;
        for (const auto& field : booleans) {
            success = WritePrivateProfileStringW(field.section, field.key,
                this->*field.value ? L"1" : L"0", path.c_str()) != FALSE && success;
        }
        for (const auto& field : numbers) {
            success = WritePrivateProfileStringW(L"Performance", field.key,
                std::to_wstring(this->*field.value).c_str(), path.c_str()) != FALSE && success;
        }
        success = WritePrivateProfileStringW(L"Performance", L"Corner",
            std::to_wstring(corner).c_str(), path.c_str()) != FALSE && success;
        success = WritePrivateProfileStringW(L"GPU", L"Adapter",
            std::to_wstring(adapter).c_str(), path.c_str()) != FALSE && success;
        return success;
    }
}
