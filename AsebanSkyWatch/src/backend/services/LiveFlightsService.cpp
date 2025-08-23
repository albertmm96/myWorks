#include "LiveFlightsService.h"
#include "openSkyFetcher.h"
#include "tile_math.h"
#include <algorithm>

LiveFlightsService::LiveFlightsService(OpenSkyFetcher* fetcher, QObject* parent)
    : QObject(parent), fetcher_(fetcher) {
}

// called from bridge when the user moves the map
void LiveFlightsService::requestTile(double lat, double lon, int z) {
    if (!fetcher_) {
        emit serviceError("OpenSkyFetcher not set");
        return;
    }

	// reasonable bbox zoom levels
    z = std::clamp(z, 5, 11);

	// convert to tile x,y with the given zoom
    auto [x, y] = tilemath::lonLatToTile(lat, lon, z);
    TileKey key{ x, y, z };

	// cache lookup: if found and not expired, return it
    const auto now = QDateTime::currentDateTimeUtc();
    auto it = cache_.find(key);
    if (it != cache_.end() && it->ts.secsTo(now) < ttlSeconds_) {
        emit flightsForTileReady(it->payload);
        return;
    }

	// if not, we will search it in OpenSky
    fetchTile(key);
}

void LiveFlightsService::fetchTile(const TileKey& key) {
    // (x,y,z) -> bbox (minLat, minLon, maxLat, maxLon)
    auto [minLat, minLon, maxLat, maxLon] = tilemath::tileBBox(key.x, key.y, key.z);

    fetcher_->fetchStatesBBox(
        minLat, minLon, maxLat, maxLon,
        // onOk
        [this, key](const QJsonObject& obj) {
            cache_[key] = CacheEntry{ obj, QDateTime::currentDateTimeUtc() };
            emit flightsForTileReady(obj);
        },
        // onErr
        [this](const QString& err) {
            emit serviceError(err);
        }
    );
}