#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <cmath>
#include <limits>
#include <algorithm>

namespace skywatch::model {

    struct Wind final {
        // according to the meteorological convention of "direction wind is coming FROM"
        // for vector math, we store components (east, north) in m/s
        double uEastMS = 0.0;  // +east
        double vNorthMS = 0.0;  // +north
    };

    struct WeatherSample final {
        std::int64_t unixTimeSec = 0;
        double latDeg = std::numeric_limits<double>::quiet_NaN();
        double lonDeg = std::numeric_limits<double>::quiet_NaN();

        // core analysis fields
        std::optional<double> temperatureK;
        std::optional<double> pressureHPa;

        // wind vector at a point/time of interest
        std::optional<Wind> wind;

        // probable turbulence-related fields we're going to analyze (depending on provider / future derivations)
        // for example: gust, CAPE, TKE proxy, etc.
        std::optional<double> windGustMS;
    };

    struct GridMeta final {
        // regular lat/lon grid definition
        double latMinDeg = 0.0;
        double lonMinDeg = 0.0;
        double dLatDeg = 0.0;  // step in degrees
        double dLonDeg = 0.0;  // step in degrees
        int rows = 0;            // latitude points
        int cols = 0;            // longitude points
    };

    class WeatherGrid final {
    public:
        GridMeta meta{};
        std::int64_t unixTimeSec = 0; // time validity of the grid (single slice)

        // storage: row-major [r*cols + c]
        // we use NaN for "missing" to avoid optionals in hot paths
        std::vector<double> uEastMS;
        std::vector<double> vNorthMS;
        std::vector<double> temperatureK;  // we keep empty if unused
        std::vector<double> pressureHPa;   // we keep empty if unused

        WeatherGrid() = default;

        explicit WeatherGrid(const GridMeta& m, std::int64_t tSec)
            : meta(m), unixTimeSec(tSec)
        {
            const std::size_t n = static_cast<std::size_t>(meta.rows) * static_cast<std::size_t>(meta.cols);
            uEastMS.assign(n, std::numeric_limits<double>::quiet_NaN());
            vNorthMS.assign(n, std::numeric_limits<double>::quiet_NaN());
			// temperatureK / pressureHPa can be allocated if needed to use, but for now we leave them empty
            // temperatureK.assign(n, std::numeric_limits<double>::quiet_NaN());
            // pressureHPa.assign(n, std::numeric_limits<double>::quiet_NaN());
        }

        [[nodiscard]] bool valid() const noexcept {
            return meta.rows > 0 && meta.cols > 0 && meta.dLatDeg > 0.0 && meta.dLonDeg > 0.0
                && uEastMS.size() == static_cast<std::size_t>(meta.rows) * static_cast<std::size_t>(meta.cols)
                && vNorthMS.size() == static_cast<std::size_t>(meta.rows) * static_cast<std::size_t>(meta.cols);
        }

        [[nodiscard]] std::size_t idx(int r, int c) const noexcept {
            return static_cast<std::size_t>(r) * static_cast<std::size_t>(meta.cols) + static_cast<std::size_t>(c);
        }

        /**
         * bilinear interpolation of wind at lat/lon
         * we return nullopt if out of bounds or if any of the 4 corners is missing
         */
        [[nodiscard]] std::optional<Wind> windAt(double latDeg, double lonDeg) const noexcept {
            if (!valid()) return std::nullopt;

            // map to grid coordinates
            const double y = (latDeg - meta.latMinDeg) / meta.dLatDeg;
            const double x = (lonDeg - meta.lonMinDeg) / meta.dLonDeg;

            if (!(x >= 0.0 && y >= 0.0)) return std::nullopt;

            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;

            if (x0 < 0 || y0 < 0 || x1 >= meta.cols || y1 >= meta.rows) return std::nullopt;

            const double fx = x - x0;
            const double fy = y - y0;

            const auto i00 = idx(y0, x0);
            const auto i10 = idx(y0, x1);
            const auto i01 = idx(y1, x0);
            const auto i11 = idx(y1, x1);

            const double u00 = uEastMS[i00], u10 = uEastMS[i10], u01 = uEastMS[i01], u11 = uEastMS[i11];
            const double v00 = vNorthMS[i00], v10 = vNorthMS[i10], v01 = vNorthMS[i01], v11 = vNorthMS[i11];

            if (!std::isfinite(u00) || !std::isfinite(u10) || !std::isfinite(u01) || !std::isfinite(u11)) return std::nullopt;
            if (!std::isfinite(v00) || !std::isfinite(v10) || !std::isfinite(v01) || !std::isfinite(v11)) return std::nullopt;

            const double u0 = u00 * (1.0 - fx) + u10 * fx;
            const double u1 = u01 * (1.0 - fx) + u11 * fx;
            const double v0 = v00 * (1.0 - fx) + v10 * fx;
            const double v1 = v01 * (1.0 - fx) + v11 * fx;

            Wind w{};
            w.uEastMS = u0 * (1.0 - fy) + u1 * fy;
            w.vNorthMS = v0 * (1.0 - fy) + v1 * fy;
            return w;
        }

        /**
         * just an idea: simple turbulence proxy we can start with:
         * magnitude of wind shear (spatial gradient) around the nearest cell
         * may be improved/replaced later with provider turbulence data or vertical profiles
         */
        [[nodiscard]] std::optional<double> horizontalShearProxy(double latDeg, double lonDeg) const noexcept {
            if (!valid()) return std::nullopt;

            const double y = (latDeg - meta.latMinDeg) / meta.dLatDeg;
            const double x = (lonDeg - meta.lonMinDeg) / meta.dLonDeg;

            const int c = static_cast<int>(std::round(x));
            const int r = static_cast<int>(std::round(y));
            if (r <= 0 || c <= 0 || r >= meta.rows - 1 || c >= meta.cols - 1) return std::nullopt;

            const auto ic = idx(r, c);
            const auto irp = idx(r + 1, c);
            const auto irm = idx(r - 1, c);
            const auto icp = idx(r, c + 1);
            const auto icm = idx(r, c - 1);

            const double uC = uEastMS[ic], vC = vNorthMS[ic];
            const double uRp = uEastMS[irp], vRp = vNorthMS[irp];
            const double uRm = uEastMS[irm], vRm = vNorthMS[irm];
            const double uCp = uEastMS[icp], vCp = vNorthMS[icp];
            const double uCm = uEastMS[icm], vCm = vNorthMS[icm];

            if (!std::isfinite(uC) || !std::isfinite(vC) ||
                !std::isfinite(uRp) || !std::isfinite(vRp) ||
                !std::isfinite(uRm) || !std::isfinite(vRm) ||
                !std::isfinite(uCp) || !std::isfinite(vCp) ||
                !std::isfinite(uCm) || !std::isfinite(vCm)) {
                return std::nullopt;
            }

            // finite differences in "grid units"; we can later scale to meters using latitude
            const double du_dy = (uRp - uRm) / (2.0 * meta.dLatDeg);
            const double dv_dy = (vRp - vRm) / (2.0 * meta.dLatDeg);
            const double du_dx = (uCp - uCm) / (2.0 * meta.dLonDeg);
            const double dv_dx = (vCp - vCm) / (2.0 * meta.dLonDeg);

            // shear magnitude proxy
            const double shear = std::sqrt(du_dx * du_dx + dv_dx * dv_dx + du_dy * du_dy + dv_dy * dv_dy);
            return shear;
        }
    };

} // namespace skywatch::model
