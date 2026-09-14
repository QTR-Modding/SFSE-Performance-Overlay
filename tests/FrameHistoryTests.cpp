#include "performance/FrameHistory.h"
#include "ui/Layout.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

void Check(bool valid, const char* what)
{
    if (!valid) {
        std::cerr << "FAIL: " << what << '\n';
        std::exit(1);
    }
}

void TestBurnInProtection();
void TestBurnInSettings();

int main()
{
    TestBurnInProtection();
    TestBurnInSettings();
    using Overlay::UI::CalculateLayout;
    Check(CalculateLayout(1920, 300, 0, 3, 1500).columns == 1, "vertical layout");
    Check(CalculateLayout(1920, 300, 0.25F, 3, 1500).columns == 2, "two-column reflow");
    Check(CalculateLayout(1920, 300, 0.5F, 3, 1500).columns == 3, "three groups at midpoint");
    const auto flat = CalculateLayout(1920, 300, 1, 3, 1500);
    Check(flat.width == 1500 && flat.compact == 1, "compact strip fits its readings");
    Check(CalculateLayout(1920, 300, 0.75F, 3, 1500).width == 1200, "smooth strip expansion");
    Check(CalculateLayout(700, 300, 1, 3, 1500).columns == 2, "screen-limited reflow");
    Check(CalculateLayout(1920, 300, 1, 1, 600).width == 600, "single section can flatten too");
    Check(CalculateLayout(200, 300, 1, 3, 1500).width == 200, "narrow screen clamp");
    using Overlay::UI::CalculateGraphWidth;
    Check(CalculateGraphWidth(300, 20, 0) == 300, "full graph fills section width");
    Check(CalculateGraphWidth(300, 20, 1) == 160, "compact sparkline width");
    Check(CalculateGraphWidth(100, 20, 1) == 100, "graph cannot overflow section");
    using Overlay::UI::CalculateGraphHeight;
    Check(CalculateGraphHeight(20, 0, 1) == 70, "default full graph height");
    Check(std::abs(CalculateGraphHeight(20, 1, 1) - 24) < 0.001F, "default sparkline height");
    Check(CalculateGraphHeight(20, 0, 0.25F) == 17.5F, "shorter full graph");
    Check(CalculateGraphHeight(20, 0, 3) == 210, "taller full graph");
    Check(std::abs(CalculateGraphHeight(20, 1, 3) - 72) < 0.001F, "taller compact graph");
    Overlay::UI::FlowLayout flow{300, 10, 5};
    auto position = flow.Place(100);
    Check(position.x == 0 && position.y == 0, "first reading");
    flow.Advance(100, 20);
    position = flow.Place(190);
    Check(position.x == 110 && position.y == 0, "readings share a row including exact fit");
    flow.Advance(190, 30);
    position = flow.Place(50);
    Check(position.x == 0 && position.y == 35, "wrap below tallest item");
    flow.Advance(50, 20);
    Check(flow.Height() == 55, "wrapped strip height");
    Overlay::Performance::FrameHistory history;
    Check(history.Summarize().count == 0, "empty history");
    for (int i = 0; i < 600; ++i) history.Push(1000.0 / 60, 10);
    auto stats = history.Summarize();
    Check(std::abs(stats.fps - 60) < 0.01, "60 FPS arithmetic");
    Check(std::abs(stats.lowOnePercent - 60) < 0.01, "steady 1% low");
    history.Clear();
    for (int i = 0; i < 99; ++i) history.Push(10, 10);
    history.Push(100, 10);
    stats = history.Summarize();
    Check(stats.lowOnePercent == 10 && stats.peakMs == 100, "slowest 1% mean");
    Check(std::abs(stats.fps - 100000.0 / 1090) < 0.001, "time-weighted FPS");
    std::array<float, 20> graph;
    history.Graph(graph, 10);
    Check(graph.back() == 100 && graph.front() == -1, "graph spikes and blank warmup");
    for (int i = 0; i < 50000; ++i) history.Push(0.1, 60);
    Check(history.Count() == history.capacity, "bounded ring storage");
    history.Push(10, 1);
    Check(history.Summarize().seconds < 1.011, "history shrink");
    history.Push(4000, 10);
    Check(history.Summarize().peakMs == 4000, "long stalls remain visible");
    history.Push(std::numeric_limits<double>::quiet_NaN(), 10);
    Check(history.Count() == 0, "nonfinite input rejected");
    history.Graph({}, 10);
    std::cout << "Overlay tests passed\n";
}
