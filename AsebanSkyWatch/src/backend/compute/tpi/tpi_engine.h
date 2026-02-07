#pragma once
#include "rolling_window.h"
#include "robust_stats.h"

#include "aircraft_state.h"
#include "weather_grid.h"

#include <unordered_map>
#include <vector>
#include <cstdint>

namespace skywatch::compute::tpi {

    struct TpiConfig final {
        std::int64_t W_sec = 120;
        std::size_t min_samples = 6;

        float S_vs = 1.5f;
        float S_jerk = 0.25f;
        float S_turn = 2.0f;
        float S_acc = 0.20f;

        float S_w = 15.0f;
        float S_g = 10.0f;

        std::size_t n_max = 30;
        float tau_sec = 45.0f;
        float N0 = 50.0f;
    };

    struct TileDiag final {
        float BT = 0.f;
        float WT = 0.f;
        float lambda = 0.f;
        float Neff = 0.f;
        std::size_t aircraft_used = 0;
    };

    class TpiEngine final {
    public:
        explicit TpiEngine(TpiConfig cfg = {}) : cfg_(cfg) {}

        void ingest(const skywatch::model::AircraftState& s);

        float computeTileTpi(
            const std::vector<const skywatch::model::AircraftState*>& aircraftInTile,
            std::int64_t nowSec,
            const skywatch::model::WeatherGrid* gridOrNull,
            double tileCenterLat,
            double tileCenterLon,
            TileDiag* diagOut = nullptr);

        void purgeOlderThan(std::int64_t cutoffSec);

    private:
        static constexpr std::size_t kCap = 512;

        struct Hist final {
            RollingWindow<kCap> w;
            std::int64_t lastSeenSec = 0;
        };

        bool computeBa_(const std::string& icao24, float& Ba, std::size_t& sampleCount,
            float& std_vs, float& std_jerk, float& std_turn, float& std_acc);

        float weatherScore_(const skywatch::model::WeatherGrid* g, double lat, double lon) const noexcept;

        TpiConfig cfg_;
        std::unordered_map<std::string, Hist> hist_;
        std::vector<float> tmp_;
    };

} // namespace skywatch::compute::tpi
