#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace skywatch::compute::tpi {

    struct Sample final {
        std::int64_t tSec = 0;
        float vs = NAN;     // vertical_rate (m/s)
        float v = NAN;      // ground speed (m/s)
        float track = NAN;  // degrees [0,360)
    };

    template <std::size_t Capacity>
    class RollingWindow final {
    public:
        void clear() noexcept { head_ = 0; size_ = 0; }
        std::size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }

        // oldest-first indexing
        const Sample& at(std::size_t i) const noexcept {
            const std::size_t idx = (head_ + (Capacity - size_) + i) % Capacity;
            return buf_[idx];
        }

        const Sample& last() const noexcept { return at(size_ - 1); }

        void pushAndPrune(const Sample& s, std::int64_t windowSec) noexcept {
            buf_[head_] = s;
            head_ = (head_ + 1) % Capacity;
            if (size_ < Capacity) ++size_;
            pruneOld_(s.tSec - windowSec);
        }

    private:
        void pruneOld_(std::int64_t minT) noexcept {
            while (size_ > 0) {
                const Sample& oldest = at(0);
                if (oldest.tSec >= minT) break;
                --size_;
            }
        }

        std::array<Sample, Capacity> buf_{};
        std::size_t head_ = 0; // next write index
        std::size_t size_ = 0; // number of valid entries
    };

} // namespace skywatch::compute::tpi