set_xmakever("3.0.9")
set_policy("package.requires_lock", true)
-- Adapted from SFSE-Menu-Framework-Example at 2f810f7 (GPL-3.0-only).
option("sf1sdk_dir")
    set_default("../SF1SDK")
    set_showmenu(true)
    set_description("Directory containing the SF1SDK checkout")
option_end()

includes(get_config("sf1sdk_dir") or "../SF1SDK")
add_requires("spdlog v1.16.0", { configs = { header_only = false, wchar = true, std_format = true } })

local name = "SFSE Performance Overlay"
local dll = "SFSEPerformanceOverlay"
local version = "1.1.1"
local author = "Quantumyilmaz"
local major, minor, patch = version:match("^(%d+)%.(%d+)%.(%d+)$")
local packed = tonumber(major) * 0x1000000 + tonumber(minor) * 0x10000 + tonumber(patch) * 0x10

set_project(name)
set_version(version)
set_license("GPL-3.0-only")
set_languages("c++23")
set_encodings("utf-8")
set_warnings("allextra")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target(dll, function()
    set_kind("shared")
    add_rules("sf1sdk.plugin")
    set_version(version)
    add_deps("SF1SDK")
    add_packages("spdlog")
    set_pcxxheader("src/PCH.h")
    add_files("src/**.cpp", "src/Version.rc")
    add_headerfiles("src/**.h")
    add_includedirs("src", "lib/sfse-mcp/include", "lib/sfse-mcp/lib/clib-utils-qtr/include")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN",
                "_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
                'OVERLAY_NAME="' .. name .. '"',
                'OVERLAY_AUTHOR="' .. author .. '"',
                'OVERLAY_VERSION_TEXT="' .. version .. '.0"',
                "OVERLAY_VERSION_MAJOR=" .. major,
                "OVERLAY_VERSION_MINOR=" .. minor,
                "OVERLAY_VERSION_PATCH=" .. patch,
                "OVERLAY_PACKED_VERSION=" .. packed)
    add_syslinks("pdh", "gdi32", "dxgi", "crypt32", "shell32", "ole32", "user32")
    if is_mode("release", "releasedbg") then
        add_shflags("/OPT:REF", "/OPT:ICF", {force = true})
    end
    on_config(function(target)
        target:set("installdir", path.join(os.projectdir(), "build", "staging"))
    end)
end)

target("overlay-tests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/FrameHistoryTests.cpp", "tests/BurnInProtectionTests.cpp",
              "src/performance/FrameHistory.cpp", "src/ui/BurnInProtection.cpp", "src/config/Settings.cpp")
    add_includedirs("src", "lib/sfse-mcp/include", "lib/sfse-mcp/lib/clib-utils-qtr/include")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
end)

target("telemetry-probe", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/TelemetryProbe.cpp", "src/telemetry/Gpu.cpp")
    add_includedirs("src")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_syslinks("dxgi", "pdh", "gdi32", "shell32", "ole32")
end)

target("runtime-probe", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/RuntimeProbe.cpp", "src/telemetry/Sampler.cpp",
              "src/telemetry/Gpu.cpp", "src/config/Settings.cpp")
    add_includedirs("src")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_syslinks("dxgi", "pdh", "gdi32", "shell32", "ole32")
end)
