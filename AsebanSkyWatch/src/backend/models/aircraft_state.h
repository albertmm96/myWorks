#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <cmath>
#include <qmath.h>

namespace skywatch::model {

    /**
     * snapshot of an aircraft at time t
     * A potentially relevant "analysis" primitive for density/altitude/frequency, and for fusing with weather
     *
     * coordinates:
     *  - lat/lon in degrees (WGS84)
     *  - altitude in meters (geometric)
     *  - velocities in m/s
     *  - headings in degrees, clockwise from true north [0,360)
     */
    struct AircraftState final {
        // identity / metadata
        std::string icao24;              // hex string (OpenSky ICAO24)
        std::optional<std::string> callsign;

        // time
        std::int64_t unixTimeSec = 0;    // sample timestamp (UTC, seconds)

        // position
        double latDeg = std::numeric_limits<double>::quiet_NaN();
        double lonDeg = std::numeric_limits<double>::quiet_NaN();
        std::optional<double> geoAltM;   // geometric altitude (m), if available
        std::optional<double> baroAltM;  // barometric altitude (m), if available

        // motion
        std::optional<double> groundSpeedMS;   // horizontal speed (m/s)
        std::optional<double> trackDeg;        // ground track (deg)
        std::optional<double> verticalRateMS;  // climb/descent (m/s)

        // data quality / flags
        bool onGround = false;

        // possibly needed helpers (hot-path safe) 

        [[nodiscard]] bool hasValidPosition() const noexcept {
            return std::isfinite(latDeg) && std::isfinite(lonDeg)
                && latDeg >= -90.0 && latDeg <= 90.0
                && lonDeg >= -180.0 && lonDeg <= 180.0;
        }

        [[nodiscard]] double altitudeMetersPreferGeo() const noexcept {
            if (geoAltM.has_value()) return *geoAltM;
            if (baroAltM.has_value()) return *baroAltM;
            return std::numeric_limits<double>::quiet_NaN();
        }

        /**
         * ground velocity vector (east, north) in m/s
         * returns nullopt if speed or track missing
         */
        [[nodiscard]] std::optional<std::pair<double, double>> groundVelocityEN() const noexcept {
            if (!groundSpeedMS || !trackDeg) return std::nullopt;
            const double spd = *groundSpeedMS;
            const double trk = *trackDeg * M_PI / 180.0;
            // trackDeg is clockwise from north => N=cos, E=sin
            const double vN = spd * std::cos(trk);
            const double vE = spd * std::sin(trk);
            return std::make_pair(vE, vN);
        }
    };

} // namespace skywatch::model
