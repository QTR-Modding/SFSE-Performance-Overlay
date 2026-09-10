#pragma once

namespace Overlay::Config
{
    struct Settings
    {
        bool enabled = true;
        bool followFrameworkTheme = true;
        bool showHeadings = true;
        bool fps = true;
        bool frameTime = true;
        bool graph = true;
        bool lowOnePercent = true;
        bool peak = false;
        bool cpuUsage = true;
        bool ram = true;
        bool gpuUsage = true;
        bool gpuTemperature = true;
        bool gpuPower = true;
        bool gpuClock = false;
        bool vram = true;
        bool gameVram = false;
        int adapter = -1;
        int corner = 1;
        float opacity = 0.85F;
        float scale = 0.7F;
        float width = 18.0F;
        float layout = 0.0F;
        float margin = 20.0F;
        float historySeconds = 10.0F;
        float graphCeiling = 50.0F;
        float graphHeight = 1.0F;
        void Load();
        [[nodiscard]] bool Save() const;
    };
}
