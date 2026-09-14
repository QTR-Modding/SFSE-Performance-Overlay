#include "ui/BurnInProtection.h"
#include "config/Settings.h"

#include <SFSEMCP/SFSEMenuFramework.hpp>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>

void Check(bool valid, const char* what);

void TestBurnInProtection()
{
    using namespace Overlay::UI;
    namespace Gui = ImGuiMCP;
    BurnInMotion motion;
    Check(motion.Displacement(64, {100, 100}, false, false).x == 0, "motion starts at selected corner");
    for (int i = 0; i < 100; ++i) motion.Advance(0.01, 64, 2);
    const auto offset = motion.Displacement(64, {100, 100}, false, false);
    Check(std::abs(offset.x - 2) < 0.001F, "horizontal speed in pixels per second");
    Check(offset.y > 1 && offset.y < offset.x, "vertical motion has a different period");
    const auto mirrored = motion.Displacement(64, {100, 100}, true, true);
    Check(mirrored.x == -offset.x && mirrored.y == -offset.y, "right and bottom corners move inward");
    const auto limited = motion.Displacement(64, {10, 0}, false, false);
    Check(limited.x >= 0 && limited.x <= 10 && limited.y == 0, "travel uses available space");
    Check(motion.Displacement(64, {-10, -10}, false, false).x == 0, "oversized overlay has no travel");
    Check(motion.Displacement(0, {100, 100}, false, false).x == 0, "zero range disables movement");
    BurnInMotion otherCadence;
    for (int i = 0; i < 10; ++i) otherCadence.Advance(0.1, 64, 2);
    Check(std::abs(otherCadence.Displacement(64, {100, 100}, false, false).x - offset.x) < 0.001F,
        "movement independent of ordinary frame rate");
    motion.Advance(std::numeric_limits<double>::quiet_NaN(), 64, 2);
    motion.Advance(-1, 64, 2);
    Check(motion.Displacement(64, {100, 100}, false, false).x == offset.x, "invalid frame intervals ignored");
    motion.Advance(60, 64, 2);
    Check(motion.Displacement(64, {100, 100}, false, false).x - offset.x < 0.201F,
        "no catch-up jump after a long gap");
    BurnInMotion bounce;
    for (int i = 0; i < 15; ++i) bounce.Advance(0.1, 1, 1);
    Check(std::abs(bounce.Displacement(1, {100, 100}, false, false).x - 0.5F) < 0.001F,
        "motion reflects at its boundary");

    std::array<Gui::ImDrawVert, 2> vertices{{
        {{10, 20}, {0.25F, 0.75F}, IM_COL32(200, 100, 50, 123)},
        {{30, 40}, {0.5F, 0.5F}, IM_COL32(0, 0, 0, 200)}
    }};
    std::array<Gui::ImDrawCmd, 1> commands{};
    commands[0].ClipRect = {5, 15, 100, 200};
    Gui::ImDrawList draw{};
    draw.VtxBuffer = {2, 2, vertices.data()};
    draw.CmdBuffer = {1, 1, commands.data()};
    ApplyBurnInProtection(draw, {3, -4}, 0.5F);
    Check(vertices[0].pos.x == 13 && vertices[0].pos.y == 16 && vertices[1].pos.x == 33,
        "entire HUD geometry moves together");
    Check(vertices[0].col == IM_COL32(100, 50, 25, 123), "RGB dims without changing opacity");
    Check(vertices[1].col == IM_COL32(0, 0, 0, 200), "black remains black with unchanged alpha");
    Check(vertices[0].uv.x == 0.25F && vertices[0].uv.y == 0.75F, "font texture coordinates unchanged");
    Check(commands[0].ClipRect.x == 8 && commands[0].ClipRect.y == 11 &&
        commands[0].ClipRect.z == 103 && commands[0].ClipRect.w == 196, "clipping follows the HUD");
    ApplyBurnInProtection(draw, {}, 1);
    Check(vertices[0].col == IM_COL32(100, 50, 25, 123), "full brightness leaves colors untouched");
    Gui::ImDrawList empty{};
    ApplyBurnInProtection(empty, {}, 0.5F);
}

void TestBurnInSettings()
{
    // Settings resolve next to this test executable, never the installed game.
    std::array<wchar_t, 32768> exe{};
    Check(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())) != 0, "test executable path");
    const auto path = std::filesystem::path(exe.data()).parent_path() / L"Data/SFSE/Plugins/SFSEPerformanceOverlay.ini";
    Check(!std::filesystem::exists(path), "test must not overwrite an existing settings file");
    std::filesystem::create_directories(path.parent_path());
    Overlay::Config::Settings saved;
    saved.Load();
    Check(!saved.burnInProtection && saved.minimalDecoration, "missing INI preserves old appearance");
    saved.burnInProtection = true;
    saved.minimalDecoration = false;
    saved.overlayBrightness = 0.4F;
    saved.movementRange = 120;
    saved.movementSpeed = 3;
    saved.opacity = 0.7F;
    Check(saved.Save(), "settings save");
    Overlay::Config::Settings loaded;
    loaded.Load();
    Check(loaded.burnInProtection && !loaded.minimalDecoration && loaded.overlayBrightness == 0.4F &&
        loaded.movementRange == 120 && loaded.movementSpeed == 3, "burn-in settings round trip");
    Check(loaded.opacity == 0.7F, "existing settings section remains compatible");
    Check(WritePrivateProfileStringW(L"BurnInProtection", L"Brightness", L"0", path.c_str()) != 0, "write invalid brightness");
    Check(WritePrivateProfileStringW(L"BurnInProtection", L"MovementRange", L"9999", path.c_str()) != 0, "write excessive range");
    Check(WritePrivateProfileStringW(L"BurnInProtection", L"MovementSpeed", L"nan", path.c_str()) != 0, "write nonfinite speed");
    loaded.Load();
    Check(loaded.overlayBrightness == 0.25F && loaded.movementRange == 256 && loaded.movementSpeed == 3,
        "invalid numeric settings are clamped or ignored");
    Check(std::filesystem::remove(path), "remove only the test-owned INI");
}
