#pragma once

namespace ImGuiMCP { struct ImDrawList; }

namespace Overlay::UI
{
    struct Offset
    {
        float x{}, y{};
    };

    class BurnInMotion
    {
    public:
        void Advance(double seconds, float range, float speed);
        [[nodiscard]] Offset Displacement(float range, Offset available, bool right, bool bottom) const;

    private:
        double phaseX{}, phaseY{};
    };

    // Only for this non-interactive HUD's completed, private window draw list.
    // Move clip rectangles with the geometry and leave texture coordinates/alpha intact.
    void ApplyBurnInProtection(ImGuiMCP::ImDrawList& draw, Offset offset, float brightness);
}
