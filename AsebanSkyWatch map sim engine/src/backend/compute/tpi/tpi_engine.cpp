#include "tpi_engine.h"
#include <cmath>

namespace skywatch::compute::tpi {

    void TpiEngine::ingest(const skywatch::model::AircraftState& s) {
        if (s.icao24.empty()) return;
        if (s.unixTimeSec <= 0) return;

        auto& h = hist_[s.icao24];
        h.lastSeenSec = s.unixTimeSec;

        Sample smp{};
        smp.tSec = s.unixTimeSec;
        if (s.verticalRateMS) smp.vs = float(*s.verticalRateMS);
        if (s.groundSpeedMS)  smp.v = float(*s.groundSpeedMS);
        if (s.trackDeg)       smp.track = float(*s.trackDeg);

        h.w.pushAndPrune(smp, cfg_.W_sec);
    }

    bool TpiEngine::computeBa_(const std::string& icao24, float& Ba, std::size_t& sampleCount,
        float& std_vs, float& std_jerk, float& std_turn, float& std_acc)
    {
        Ba = 0.f; sampleCount = 0;
        std_vs = std_jerk = std_turn = std_acc = 0.f;

        auto it = hist_.find(icao24);
        if (it == hist_.end()) return false;

        const auto& w = it->second.w;
        const std::size_t n = w.size();
        sampleCount = n;
        if (n < cfg_.min_samples) return false;

        // std_vs
        tmp_.clear();
        tmp_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const float vs = w.at(i).vs;
            if (std::isfinite(vs)) tmp_.push_back(vs);
        }
        if (tmp_.size() < cfg_.min_samples) return false;
        std_vs = stddevWinsorized(tmp_);

        // std_jerk
        tmp_.clear();
        for (std::size_t i = 1; i < n; ++i) {
            const auto& a = w.at(i - 1);
            const auto& b = w.at(i);
            const float dt = float(b.tSec - a.tSec);
            if (dt <= 0.f) continue;
            if (!std::isfinite(a.vs) || !std::isfinite(b.vs)) continue;
            tmp_.push_back((b.vs - a.vs) / dt);
        }
        if (tmp_.size() < cfg_.min_samples - 1) return false;
        std_jerk = stddevWinsorized(tmp_);

        // std_turn
        tmp_.clear();
        for (std::size_t i = 1; i < n; ++i) {
            const auto& a = w.at(i - 1);
            const auto& b = w.at(i);
            const float dt = float(b.tSec - a.tSec);
            if (dt <= 0.f) continue;
            if (!std::isfinite(a.track) || !std::isfinite(b.track)) continue;
            tmp_.push_back(angleDiffDeg(b.track, a.track) / dt);
        }
        if (tmp_.size() < cfg_.min_samples - 1) return false;
        std_turn = stddevWinsorized(tmp_);

        // std_acc
        tmp_.clear();
        for (std::size_t i = 1; i < n; ++i) {
            const auto& a = w.at(i - 1);
            const auto& b = w.at(i);
            const float dt = float(b.tSec - a.tSec);
            if (dt <= 0.f) continue;
            if (!std::isfinite(a.v) || !std::isfinite(b.v)) continue;
            tmp_.push_back((b.v - a.v) / dt);
        }
        if (tmp_.size() < cfg_.min_samples - 1) return false;
        std_acc = stddevWinsorized(tmp_);

        const float f1 = clamp01(std_vs / cfg_.S_vs);
        const float f2 = clamp01(std_jerk / cfg_.S_jerk);
        const float f3 = clamp01(std_turn / cfg_.S_turn);
        const float f4 = clamp01(std_acc / cfg_.S_acc);

        Ba = clamp01(0.45f * f1 + 0.35f * f2 + 0.15f * f3 + 0.05f * f4);
        return true;
    }

    float TpiEngine::weatherScore_(const skywatch::model::WeatherGrid* g, double lat, double lon) const noexcept {
        if (!g || !g->valid()) return 0.f;

        // wind speed from u/v
        const auto wOpt = g->windAt(lat, lon);
        float windSpeed = 0.f;
        if (wOpt) {
            const double u = wOpt->uEastMS;
            const double v = wOpt->vNorthMS;
            windSpeed = (std::isfinite(u) && std::isfinite(v)) ? float(std::sqrt(u * u + v * v)) : 0.f;
        }

        float gustFactor = 0.f;
        if (!g->windGustMS.empty()) {
            const auto gustOpt = g->windGustAt(lat, lon);
            if (gustOpt && std::isfinite(*gustOpt)) {
                gustFactor = float(*gustOpt) - windSpeed;
                if (gustFactor < 0.f) gustFactor = 0.f;
            }
        }

        float g3 = 0.f;
        if (!g->humidityPct.empty()) {
            const auto hOpt = g->humidityAt(lat, lon);
            if (hOpt && std::isfinite(*hOpt)) {
                const float H = float(*hOpt);
                if (H >= 60.f) g3 = clamp01((H - 60.f) / 40.f);
            }
        }

        const float g1 = clamp01(windSpeed / cfg_.S_w);
        const float g2 = clamp01(gustFactor / cfg_.S_g);

        return clamp01(0.45f * g1 + 0.45f * g2 + 0.10f * g3);
    }

    float TpiEngine::computeTileTpi(
        const std::vector<const skywatch::model::AircraftState*>& aircraftInTile,
        std::int64_t nowSec,
        const skywatch::model::WeatherGrid* gridOrNull,
        double tileCenterLat,
        double tileCenterLon,
        TileDiag* diagOut)
    {
        float wSum = 0.f;
        float bwSum = 0.f;
        float Neff = 0.f;
        std::size_t used = 0;

        for (const auto* s : aircraftInTile) {
            if (!s) continue;

            float Ba = 0.f, std_vs = 0.f, std_jerk = 0.f, std_turn = 0.f, std_acc = 0.f;
            std::size_t n = 0;
            if (!computeBa_(s->icao24, Ba, n, std_vs, std_jerk, std_turn, std_acc)) continue;

            const float nCap = float(std::min(n, cfg_.n_max));
            const auto hit = hist_.find(s->icao24);
            const float age = (hit != hist_.end()) ? float(nowSec - hit->second.lastSeenSec) : 0.f;
            const float rec = (age > 0.f && cfg_.tau_sec > 0.f) ? std::exp(-age / cfg_.tau_sec) : 1.f;

            const float w = nCap * rec;

            Neff += nCap;
            wSum += w;
            bwSum += w * Ba;
            ++used;
        }

        const float BT = (wSum > 0.f) ? (bwSum / wSum) : 0.f;
        const float WT = weatherScore_(gridOrNull, tileCenterLat, tileCenterLon);
        const float lambda = (cfg_.N0 > 0.f) ? (1.f - std::exp(-Neff / cfg_.N0)) : 0.f;

        const float TPI = clamp01(lambda * clamp01(BT) + (1.f - lambda) * WT);

        if (diagOut) {
            diagOut->BT = clamp01(BT);
            diagOut->WT = WT;
            diagOut->lambda = clamp01(lambda);
            diagOut->Neff = Neff;
            diagOut->aircraft_used = used;
        }

        return TPI;
    }

    void TpiEngine::purgeOlderThan(std::int64_t cutoffSec) {
        for (auto it = hist_.begin(); it != hist_.end(); ) {
            if (it->second.lastSeenSec < cutoffSec) it = hist_.erase(it);
            else ++it;
        }
    }

} // namespace skywatch::compute::tpi
