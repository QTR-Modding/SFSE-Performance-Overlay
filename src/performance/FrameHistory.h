#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace Overlay::Performance
{
    struct Statistics
    {
        double fps{};
        double meanMs{};
        double lowOnePercent{};
        double peakMs{};
        double seconds{};
        std::size_t count{};
    };

    // Render-thread owned. Storage stays bounded even at uncapped frame rates.
    class FrameHistory
    {
    public:
        static constexpr std::size_t capacity = 32768;
        void Push(double milliseconds, double historySeconds);
        void Clear();
        [[nodiscard]] Statistics Summarize() const;
        [[nodiscard]] double RecentMeanMs() const;
        // Peak per time bucket preserves narrow spikes when history exceeds pixels.
        void Graph(std::span<float> buckets, double historySeconds) const;
        [[nodiscard]] std::size_t Count() const { return count_; }

    private:
        std::array<float, capacity> values_{};
        std::size_t head_{};
        std::size_t count_{};
        double totalMs_{};
    };
}
