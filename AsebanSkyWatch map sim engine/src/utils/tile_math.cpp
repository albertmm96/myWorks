// utils/tile_math.cpp
#include "tile_math.h"
#include <cmath>
#include <algorithm>
#include <qmath.h>

// implementation in tile_math.cpp
namespace tilemath {

    // we clamp latitude to the Web Mercator limits
    static inline double clampLat(double lat) {
        return std::max(-85.05112878, std::min(85.05112878, lat));
    }

    // converts lon/lat in degrees to tile x,y at zoom z
    std::tuple<int, int> tilemath::lonLatToTile(double lat, double lon, int z)
    {
        lat = clampLat(lat);
        const double n = std::pow(2.0, z);
        const int x = int(std::floor((lon + 180.0) / 360.0 * n));
        const double latRad = lat * M_PI / 180.0;
        const int y = int(std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n));
        return { x,y };
    }

    // returns the bounding box of a tile x,y at zoom z as (minLat, minLon, maxLat, maxLon)
    std::tuple<double, double, double, double> tilemath::tileBBox(int x, int y, int z)
    {
        auto tile2lon = [](int tx, int tz) { return tx / std::pow(2.0, tz) * 360.0 - 180.0; };
        auto tile2lat = [](int ty, int tz) {
            const double n = M_PI - 2.0 * M_PI * ty / std::pow(2.0, tz);
            return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
            };
        double minLon = tile2lon(x, z);
        double maxLon = tile2lon(x + 1, z);
        double minLat = tile2lat(y + 1, z);
        double maxLat = tile2lat(y, z);
        return { minLat, minLon, maxLat, maxLon };
    }

}