set_xmakever("3.0.9")
set_policy("package.requires_lock", true)
includes("lib/commonlibsf")

-- Adapted from SFSE-Menu-Framework-Example at 2f810f7 (GPL-3.0-only).
-- CommonLib's automatic deployment must never write into a live game setup.
rule("commonlib.plugin", function()
    after_build(function() end)
end)

local name = "SFSE Performance Overlay"
local dll = "SFSEPerformanceOverlay"
local version = "1.1.0"
local author = "Quantumyilmaz"

set_project(name)
set_version(version)
set_license("GPL-3.0-only")
set_languages("c++23")
set_encodings("utf-8")
set_warnings("allextra")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target(dll, function()
    add_rules("commonlibsf.plugin", {
        name = name, author = author, description = name,
        options = { sig_scanning = false, address_library = false,
                    no_struct_use = true, layout_dependent = false }
    })
    set_version(version)
    set_pcxxheader("src/PCH.h")
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src", "lib/sfse-mcp/include", "lib/sfse-mcp/lib/clib-utils-qtr/include")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN",
                "_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING")
    add_syslinks("pdh", "gdi32", "dxgi", "crypt32", "shell32", "ole32")
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
