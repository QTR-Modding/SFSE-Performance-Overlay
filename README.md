# SFSE Performance Overlay

A configurable performance HUD for Starfield, using SFSE Menu Framework.

Open SFSE Menu Framework and select **Performance Overlay**. Settings apply live
and save automatically after editing.

- **CPU:** system CPU usage and RAM.
- **GPU:** usage, temperature, power, clock, total VRAM and game VRAM/budget.
- **Performance:** FPS, frame time, 1% lows and a frame-time graph with
  history controls.
- **Appearance:** show/hide, size, opacity, position and vertical-to-horizontal layout.

Hiding the overlay pauses hardware polling and frame measurements. Settings still save.
The overlay follows SFSE Menu Framework's colors by default. Turn off
**Follow SFSE-MF theme** in Appearance to use its original dark palette instead.

Requires SFSE and SFSE Menu Framework 0.15.0. Place the DLL in `Data/SFSE/Plugins`.
Settings are saved to `Data/SFSE/Plugins/SFSEPerformanceOverlay.ini`.

FPS measures framework render cadence, not display scanout or generated frames.
The graph preserves spikes; 1% lows use the slowest 1% average over the selected
history. Loading screens and background gaps can affect measurements; use
**Reset measurements** to start fresh.

Hardware readings use Windows counters and, when available, NVIDIA's installed
NVML driver library. Sensor availability varies; `--` means unavailable. Select
a GPU manually if automatic selection chooses the wrong adapter.

## Build

Clone recursively, then run `xmake f -m releasedbg` and `xmake` with Visual Studio's
C++ tools installed. Generate a Visual Studio solution with
`xmake project -k vsxmake -m "debug,release,releasedbg"`.

Licensed under GPL-3.0-only. SFSE-MCP is MIT licensed. NVIDIA's required notice
is in [notices/NVIDIA.txt](notices/NVIDIA.txt).
