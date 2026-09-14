#include "ui/BurnInProtection.h"

#include <SFSEMCP/SFSEMenuFramework.hpp>
#include <algorithm>
#include <cmath>

namespace Overlay::UI
{
    void BurnInMotion::Advance(double seconds, float range, float speed)
    {
        if (range <= 0 || !std::isfinite(seconds) || seconds <= 0) return;
        // No catch-up leap after loading, losing focus, or a stalled frame.
        const double step = std::min(seconds, 0.1) * speed / range;
        phaseX = std::fmod(phaseX + step, 2.0);
        // Different periods avoid retracing the same diagonal on each pass.
        phaseY = std::fmod(phaseY + step * 0.6180339887498948, 2.0);
    }

    Offset BurnInMotion::Displacement(float range, Offset available, bool right, bool bottom) const
    {
        const auto travel = [range](double phase, float room) {
            return static_cast<float>(1.0 - std::abs(phase - 1.0)) *
                std::clamp(range, 0.0F, std::max(0.0F, room));
        };
        return {travel(phaseX, available.x) * (right ? -1 : 1),
                travel(phaseY, available.y) * (bottom ? -1 : 1)};
    }

    void ApplyBurnInProtection(ImGuiMCP::ImDrawList& draw, Offset offset, float brightness)
    {
        brightness = std::isfinite(brightness) ? std::clamp(brightness, 0.0F, 1.0F) : 1.0F;
        for (int i = 0; i < draw.VtxBuffer.Size; ++i) {
            auto& vertex = draw.VtxBuffer.Data[i];
            vertex.pos.x += offset.x;
            vertex.pos.y += offset.y;
            if (brightness < 1) {
                // Relative RGB dimming, not a calibrated luminance or opacity setting.
                auto color = vertex.col & IM_COL32_A_MASK;
                for (const int shift : {IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT}) {
                    const auto channel = (vertex.col >> shift) & 0xFFU;
                    color |= static_cast<ImGuiMCP::ImU32>(channel * brightness + 0.5F) << shift;
                }
                vertex.col = color;
            }
        }
        for (int i = 0; i < draw.CmdBuffer.Size; ++i) {
            auto& clip = draw.CmdBuffer.Data[i].ClipRect;
            clip.x += offset.x;
            clip.z += offset.x;
            clip.y += offset.y;
            clip.w += offset.y;
        }
    }
}
