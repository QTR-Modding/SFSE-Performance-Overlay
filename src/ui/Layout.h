#pragma once

#include <algorithm>

namespace Overlay::UI
{
    struct Layout
    {
        float width;
        int columns;
    };

    inline Layout CalculateLayout(float availableWidth, float sectionWidth, float horizontal, int sections)
    {
        sections = std::clamp(sections, 1, 3);
        sectionWidth = std::max(sectionWidth, 1.0F);
        const float width = std::min(std::max(availableWidth, 1.0F),
            sectionWidth * (1 + std::clamp(horizontal, 0.0F, 1.0F) * static_cast<float>(sections - 1)));
        const int columns = std::clamp(static_cast<int>((width + 0.01F) / sectionWidth), 1, sections);
        return {width, columns};
    }
}
