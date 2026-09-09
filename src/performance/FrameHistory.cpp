#include "performance/FrameHistory.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace Overlay::Performance
{
    void FrameHistory::Clear()
    {
        head_ = count_ = 0;
        totalMs_ = 0;
    }

    void FrameHistory::Push(double milliseconds, double historySeconds)
    {
        if (!std::isfinite(milliseconds) || milliseconds <= 0) {
            Clear();
            return;
        }
        if (count_ == capacity) {
            totalMs_ -= values_[head_];
            --count_;
        }
        values_[head_] = static_cast<float>(milliseconds);
        totalMs_ += values_[head_];
        head_ = (head_ + 1) % capacity;
        ++count_;
        const double limit = std::clamp(historySeconds, 1.0, 60.0) * 1000;
        while (count_ > 1) {
            const auto oldest = (head_ + capacity - count_) % capacity;
            if (totalMs_ - values_[oldest] < limit) {
                break;
            }
            totalMs_ -= values_[oldest];
            --count_;
        }
    }

    Statistics FrameHistory::Summarize() const
    {
        if (!count_ || totalMs_ <= 0) {
            return {};
        }
        std::vector<float> sorted;
        sorted.reserve(count_);
        for (std::size_t i = 0; i < count_; ++i) {
            sorted.push_back(values_[(head_ + capacity - count_ + i) % capacity]);
        }
        std::sort(sorted.begin(), sorted.end(), std::greater<float>());
        const auto slowCount = std::max(std::size_t{1}, (count_ + 99) / 100);
        const double slowTotal = std::accumulate(sorted.begin(), sorted.begin() + slowCount, 0.0);
        return {1000 * static_cast<double>(count_) / totalMs_,
                totalMs_ / static_cast<double>(count_),
                1000 * static_cast<double>(slowCount) / slowTotal,
                sorted.front(), totalMs_ / 1000, count_};
    }

    double FrameHistory::RecentMeanMs() const
    {
        double total = 0;
        std::size_t count = 0;
        while (count < count_ && total < 500) {
            total += values_[(head_ + capacity - 1 - count) % capacity];
            ++count;
        }
        return count ? total / static_cast<double>(count) : 0;
    }

    void FrameHistory::Graph(std::span<float> buckets, double historySeconds) const
    {
        std::fill(buckets.begin(), buckets.end(), -1.0F);
        if (buckets.empty()) {
            return;
        }
        const double spanMs = std::clamp(historySeconds, 1.0, 60.0) * 1000;
        double age = 0;
        for (std::size_t i = 0; i < count_ && age < spanMs; ++i) {
            const float value = values_[(head_ + capacity - 1 - i) % capacity];
            const auto fromRight = std::min(buckets.size() - 1,
                static_cast<std::size_t>(age / spanMs * static_cast<double>(buckets.size())));
            auto& bucket = buckets[buckets.size() - 1 - fromRight];
            bucket = std::max(bucket, value);
            age += value;
        }
    }
}
