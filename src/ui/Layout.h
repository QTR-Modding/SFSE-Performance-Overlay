#pragma once

#include <algorithm>
#include <cmath>

namespace Overlay::UI
{
    struct Layout
    {
        float width;
        int columns;
        float compact;
    };

    inline Layout CalculateLayout(float availableWidth, float sectionWidth, float horizontal,
        int sections, float stripWidth)
    {
        sections = std::clamp(sections, 1, 3);
        sectionWidth = std::max(sectionWidth, 1.0F);
        horizontal = std::clamp(horizontal, 0.0F, 1.0F);
        // First spread the sections, then flatten the readings inside them.
        const float compact = std::max(0.0F, horizontal * 2 - 1);
        const float columnsWidth = sectionWidth * static_cast<float>(sections);
        const float desired = horizontal <= 0.5F ?
            std::lerp(sectionWidth, columnsWidth, horizontal * 2) :
            std::lerp(columnsWidth, std::max(columnsWidth, stripWidth), compact);
        const float width = std::min(std::max(availableWidth, 1.0F), desired);
        const int columns = std::clamp(static_cast<int>((width + 0.01F) / sectionWidth), 1, sections);
        return {width, columns, compact};
    }

    inline float CalculateGraphWidth(float available, float fontSize, float compact, float scale)
    {
        available = std::max(available, 1.0F);
        const float automatic = std::lerp(available, std::min(available, fontSize * 8), compact);
        return std::clamp(automatic * scale, 1.0F, available);
    }

    struct FlowLayout
    {
        struct Position { float x, y; };
        float available, columnGap, rowGap;
        float x{}, y{}, rowHeight{};

        Position Place(float width)
        {
            if (x > 0 && x + width > available + 0.01F) {
                x = 0;
                y += rowHeight + rowGap;
                rowHeight = 0;
            }
            return {x, y};
        }

        void Advance(float width, float height)
        {
            x += width + columnGap;
            rowHeight = std::max(rowHeight, height);
        }

        float Height() const { return y + rowHeight; }
    };
}
