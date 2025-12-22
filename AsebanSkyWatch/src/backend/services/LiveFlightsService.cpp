#include "LiveFlightsService.h"
#include "openSkyFetcher.h"
#include "tile_math.h"

#include <algorithm>
#include <cmath>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QVariant>

LiveFlightsService::LiveFlightsService(OpenSkyFetcher* fetcher, QObject* parent)
    : QObject(parent), fetcher_(fetcher)
{

}

bool LiveFlightsService::passesGeoFilter(const QJsonArray& state) const
{
    if (!filterEnabled_) return true;
    if (state.size() <= 6) return false;

    const QJsonValue lonV = state.at(5);
    const QJsonValue latV = state.at(6);
    if (!lonV.isDouble() || !latV.isDouble()) return false;

    const double lon = lonV.toDouble();
    const double lat = latV.toDouble();

    if (std::isnan(lat) || std::isnan(lon)) return false;
    return (lat >= minLat_ && lat <= maxLat_ && lon >= minLon_ && lon <= maxLon_);
}

void LiveFlightsService::purgeDbOutsideFilter()
{
    if (!filterEnabled_) return;

    QSqlDatabase db = QSqlDatabase::database("pg_flights");
    if (!db.isValid() || !db.isOpen()) {
        qWarning() << "[LiveFlightsService] purgeDbOutsideFilter: pg_flights not open";
        return;
    }

    QSqlQuery q(db);
    q.prepare(
        "DELETE FROM states_live "
        "WHERE latitude IS NULL OR longitude IS NULL "
        "   OR latitude < :minLat OR latitude > :maxLat "
        "   OR longitude < :minLon OR longitude > :maxLon;"
    );
    q.bindValue(":minLat", minLat_);
    q.bindValue(":maxLat", maxLat_);
    q.bindValue(":minLon", minLon_);
    q.bindValue(":maxLon", maxLon_);

    if (!q.exec()) {
        qWarning() << "[LiveFlightsService] purge states_live failed:" << q.lastError().text();
    }
    else {
        qInfo() << "[LiveFlightsService] purged states_live outside filter rect";
    }
}

QJsonObject LiveFlightsService::readStatesLiveFromDb() const
{
    QJsonObject payload;
    payload.insert("time", static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));

    QSqlDatabase db = QSqlDatabase::database("pg_flights");
    if (!db.isValid() || !db.isOpen()) {
        payload.insert("states", QJsonArray{});
        return payload;
    }

    QSqlQuery q(db);

    const QString sql =
        "SELECT icao24, callsign, origin_country, time_position, last_contact, "
        "       longitude, latitude, baro_altitude, on_ground, velocity, "
        "       true_track, vertical_rate, geo_altitude, squawk, spi, "
        "       position_source, category "
        "FROM states_live";

    if (!q.exec(sql)) {
        qWarning() << "[LiveFlightsService] readStatesLiveFromDb failed:" << q.lastError().text();
        payload.insert("states", QJsonArray{});
        return payload;
    }

    auto vOrNull = [](const QVariant& v) -> QJsonValue {
        return v.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue::fromVariant(v);
        };

    QJsonArray states;
    while (q.next()) {
        QJsonArray a;

        // the helper to append value or null
        auto appendOrNull = [&](const QVariant& v) {
            if (v.isNull())
                a.append(QJsonValue(QJsonValue::Null));
            else
                a.append(QJsonValue::fromVariant(v));
            };

        appendOrNull(q.value(0));   // [0]  icao24
        appendOrNull(q.value(1));   // [1]  callsign
        appendOrNull(q.value(2));   // [2]  origin_country
        appendOrNull(q.value(3));   // [3]  time_position
        appendOrNull(q.value(4));   // [4]  last_contact
        appendOrNull(q.value(5));   // [5]  longitude
        appendOrNull(q.value(6));   // [6]  latitude
        appendOrNull(q.value(7));   // [7]  baro_altitude
        appendOrNull(q.value(8));   // [8]  on_ground
        appendOrNull(q.value(9));   // [9]  velocity
        appendOrNull(q.value(10));  // [10] true_track
        appendOrNull(q.value(11));  // [11] vertical_rate

        a.append(QJsonValue(QJsonValue::Null)); // [12] sensors (not stored)

        appendOrNull(q.value(12));  // [13] geo_altitude
        appendOrNull(q.value(13));  // [14] squawk
        appendOrNull(q.value(14));  // [15] spi
        appendOrNull(q.value(15));  // [16] position_source
        appendOrNull(q.value(16));  // [17] category

        states.append(a);
    }

    payload.insert("states", states);
    return payload;
}

void LiveFlightsService::emitFromDb()
{
    // lastMerged_ now becomes “what DB currently contains”
    const QJsonObject payload = readStatesLiveFromDb();
    lastMerged_ = payload;
    emit flightsMergedReady(payload);
}


void LiveFlightsService::setGeoFilter(double minLat, double maxLat, double minLon, double maxLon)
{
    // normalize
    if (minLat > maxLat) std::swap(minLat, maxLat);
    if (minLon > maxLon) std::swap(minLon, maxLon);

    minLat_ = minLat;
    maxLat_ = maxLat;
    minLon_ = minLon;
    maxLon_ = maxLon;
    filterEnabled_ = true;

    // we remove out-of-rect from in-memory union, so it won't be flushed back
    for (auto it = byIcao_.begin(); it != byIcao_.end(); ) {
        if (!passesGeoFilter(it.value())) it = byIcao_.erase(it);
        else ++it;
    }

    // purge DB outside rect to make DB the truth immediately
    purgeDbOutsideFilter();

    // emit strictly from DB (map must show only what DB contains)
    emitFromDb();
}

void LiveFlightsService::clearGeoFilter()
{
    filterEnabled_ = false;
    emitFromDb();
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

    // housekeeping; build merged snapshot in memory (for DB flush)
    pruneStale();
    emitMerged(); // must update lastMerged_, but must NOT emit to UI anymore

    // flush to DB
    if (!lastMerged_.isEmpty()) {
        qInfo() << "[Tick] activeTiles=" << activeTiles_.size()
            << "lastMergedEmpty=" << lastMerged_.isEmpty();

        fetcher_->insertStatesToDb(lastMerged_);
    }

    // enforce DB == filter, then emit strictly from DB
    if (filterEnabled_) {
        purgeDbOutsideFilter();
    }
    emitFromDb();

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
        if (a.isEmpty() || !a.at(0).isString()) continue; // icao24 at [0]
        const QString icao = a.at(0).toString();

        // if filter enabled: any state outside filter removes the cached entry,
        // so it cannot be flushed later.
        if (filterEnabled_) {
            if (a.size() <= 6) {
                byIcao_.remove(icao);
                continue;
            }
            const QJsonValue lonV = a.at(5);
            const QJsonValue latV = a.at(6);
            if (!lonV.isDouble() || !latV.isDouble()) {
                byIcao_.remove(icao);
                continue;
            }
            const double lon = lonV.toDouble();
            const double lat = latV.toDouble();

            if (std::isnan(lat) || std::isnan(lon) ||
                lat < minLat_ || lat > maxLat_ ||
                lon < minLon_ || lon > maxLon_) {
                byIcao_.remove(icao);
                continue;
            }
        }

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

    for (auto it = byIcao_.cbegin(); it != byIcao_.cend(); ++it) {
        const QJsonArray a = it.value();

        // a defensive filter at emission time too (prevents any slip-through)
        if (filterEnabled_) {
            if (a.size() <= 6) continue;
            const QJsonValue lonV = a.at(5);
            const QJsonValue latV = a.at(6);
            if (!lonV.isDouble() || !latV.isDouble()) continue;

            const double lon = lonV.toDouble();
            const double lat = latV.toDouble();

            if (std::isnan(lat) || std::isnan(lon) ||
                lat < minLat_ || lat > maxLat_ ||
                lon < minLon_ || lon > maxLon_) {
                continue;
            }
        }

        merged.append(a);
    }

    QJsonObject payload;
    payload.insert("time", static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
    payload.insert("states", merged);

    lastMerged_ = payload;
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
