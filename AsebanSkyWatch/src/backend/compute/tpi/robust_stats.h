#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace skywatch::compute::tpi {

    inline float clamp01(float x) noexcept {
        if (x < 0.f) return 0.f;
        if (x > 1.f) return 1.f;
        return x;
    }

    // shortest signed difference a-b in [-180,180]
    inline float angleDiffDeg(float a, float b) noexcept {
        float d = a - b;
        while (d > 180.f) d -= 360.f;
        while (d < -180.f) d += 360.f;
        return d;
    }

    inline float stddevWinsorized(std::vector<float>& x, float pLow = 0.05f, float pHigh = 0.95f) {
        const std::size_t n = x.size();
        if (n < 2) return 0.f;

        auto kth = [&](float p) -> float {
            const std::size_t k = static_cast<std::size_t>(std::floor(p * float(n - 1)));
            std::nth_element(x.begin(), x.begin() + k, x.end());
            return x[k];
            };

        const float lo = kth(pLow);
        const float hi = kth(pHigh);

        for (float& v : x) {
            if (v < lo) v = lo;
            else if (v > hi) v = hi;
        }

        double sum = 0.0;
        for (float v : x) sum += v;
        const double mu = sum / double(n);

        double s2 = 0.0;
        for (float v : x) {
            const double d = double(v) - mu;
            s2 += d * d;
        }
        s2 /= double(n - 1);

        return (s2 > 0.0) ? float(std::sqrt(s2)) : 0.f;
    }

} // namespace skywatch::compute::tpi
