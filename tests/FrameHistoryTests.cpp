#include "performance/FrameHistory.h"

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

int main()
{
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
    std::cout << "Frame history tests passed\n";
}
