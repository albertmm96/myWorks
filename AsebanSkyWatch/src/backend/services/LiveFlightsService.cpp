#include "LiveFlightsService.h"
#include "openSkyFetcher.h"
#include "tile_math.h"
#include <algorithm>

LiveFlightsService::LiveFlightsService(OpenSkyFetcher* fetcher, QObject* parent)
    : QObject(parent), fetcher_(fetcher)
{

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

    // remember this tile as "active"
    activeTiles_.insert(key);

	// check cache TTL
    const auto now = QDateTime::currentDateTimeUtc();
    auto it = cache_.find(key);
    if (it != cache_.end() && it->ts.secsTo(now) < ttlSeconds_) {
        foldStatesIntoMerged(it->payload);
        emitMerged();
        return;
    }

    // otherwise fetch now (onOk will fold & emit)
    fetchTile(key);
}

void LiveFlightsService::onTick()
{
    if (!fetcher_) return;

    const auto now = QDateTime::currentDateTimeUtc();

    // we fold valid cached tiles into the merged map (no network required)
    for (const TileKey& key : std::as_const(activeTiles_)) {
        auto it = cache_.find(key);
        if (it != cache_.end() && it->ts.secsTo(now) < ttlSeconds_) {
            foldStatesIntoMerged(it->payload);
        }
    }

    // the housekeeping and emit merged snapshot (also updates lastMerged_)
    pruneStale();
    emitMerged();

    // deterministic DB flush ONCE per tick using the existing parsing:
    // OpenSkyFetcher::insertStatesToDb reads obj["states"] array and maps indices -> columns.
    if (!lastMerged_.isEmpty()) {
		// confirm on-tick refresh
        qInfo() << "[Tick] activeTiles=" << activeTiles_.size()
            << "lastMergedEmpty=" << lastMerged_.isEmpty();
        
        fetcher_->insertStatesToDb(lastMerged_);
    }

    // we refresh stale/missing tiles (network only; no DB writes here)
    for (const TileKey& key : std::as_const(activeTiles_)) {
        auto it = cache_.find(key);
        const bool needFetch =
            (it == cache_.end()) ||
            (it->ts.secsTo(now) >= ttlSeconds_);

        if (needFetch) {
            fetchTile(key);
        }
    }
}

void LiveFlightsService::fetchTile(const TileKey& key) {
    auto [minLat, minLon, maxLat, maxLon] = tilemath::tileBBox(key.x, key.y, key.z);

    fetcher_->fetchStatesBBox(
        minLat, minLon, maxLat, maxLon,
        // onOk
        [this, key](const QJsonObject& obj) {
            cache_[key] = CacheEntry{ obj, QDateTime::currentDateTimeUtc() };
            foldStatesIntoMerged(obj);   // updates union-of-tiles snapshot
            emitMerged();                // pushes a freshly merged snapshot
        },
        // onErr
        [this](const QString& err) { emit serviceError(err); }
    );
}

// we choose the fresher state for a given icao24 (by index 4 = last_contact)
static qint64 lastContactOf(const QJsonArray& state) {
    return (state.size() > 4 && state.at(4).isDouble()) ? static_cast<qint64>(state.at(4).toDouble()) : 0;
}

void LiveFlightsService::foldStatesIntoMerged(const QJsonObject& obj)
{
    const QJsonArray states = obj.value("states").toArray();
    for (const auto& v : states) {
        if (!v.isArray()) continue;
        const QJsonArray a = v.toArray();
        if (a.isEmpty() || !a.at(0).isString()) continue;       // need icao24 at [0]
        const QString icao = a.at(0).toString();

        auto it = byIcao_.find(icao);
        if (it == byIcao_.end()) {
            byIcao_.insert(icao, a);
        }
        else {
            if (lastContactOf(a) >= lastContactOf(it.value())) {
                it.value() = a; // keep the freshest
            }
        }
    }
}

void LiveFlightsService::emitMerged()
{
    QJsonArray merged;
    for (const auto& a : byIcao_)
        merged.append(a);

    QJsonObject payload;
    payload.insert("time", static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
    payload.insert("states", merged);

    // we keep last merged snapshot for deterministic DB flush on master tick
    lastMerged_ = payload;

    emit flightsMergedReady(payload);
}

void LiveFlightsService::pruneStale()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = byIcao_.begin(); it != byIcao_.end(); ) {
        const qint64 lc = lastContactOf(it.value());
        if (lc > 0 && (now - lc) > staleSeconds_) it = byIcao_.erase(it);
        else ++it;
    }
}
