#include "LiveFlightsService.h"
#include "openSkyFetcher.h"
#include "tile_math.h"

#include <algorithm>
#include <cmath>

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QVariant>
#include <QJsonDocument>
#include <QRegularExpression>

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

    maybeAppendLivePoint();

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





static bool isValidIcao24(const QString& s)
{
    static QRegularExpression re("^[0-9a-fA-F]{6}$");
    return re.match(s).hasMatch();
}

void LiveFlightsService::clearTrackTable()
{
    QSqlDatabase db = QSqlDatabase::database("pg_flights");
    if (!db.isValid() || !db.isOpen()) {
        qWarning() << "[Track] pg_flights not open";
        return;
    }

    QSqlQuery q(db);
    // track is only for the selected flight, so TRUNCATE is simplest and fastest
    if (!q.exec("TRUNCATE TABLE flight_track_live;")) {
        qWarning() << "[Track] TRUNCATE failed:" << q.lastError().text();
    }
}

void LiveFlightsService::insertTrackPointsToDb(const QString& icao24, const QJsonArray& points)
{
    QSqlDatabase db = QSqlDatabase::database("pg_flights");
    if (!db.isValid() || !db.isOpen()) {
        qWarning() << "[Track] pg_flights not open";
        return;
    }

    if (!db.transaction()) {
        qWarning() << "[Track] transaction start failed:" << db.lastError().text();
        return;
    }

    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO flight_track_live(ts, icao24, longitude, latitude, source) "
        "VALUES (to_timestamp(:t), :icao24, :lon, :lat, :src) "
        "ON CONFLICT (icao24, ts) DO NOTHING;"
    );

    for (const auto& v : points) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        if (!o.contains("t") || !o.contains("lon") || !o.contains("lat")) continue;

        q.bindValue(":t", static_cast<qint64>(o.value("t").toDouble()));
        q.bindValue(":icao24", icao24);
        q.bindValue(":lon", o.value("lon").toDouble());
        q.bindValue(":lat", o.value("lat").toDouble());
        q.bindValue(":src", o.value("source").toString("hist"));

        if (!q.exec()) {
            qWarning() << "[Track] insert failed:" << q.lastError().text();
            db.rollback();
            return;
        }
    }

    if (!db.commit()) {
        qWarning() << "[Track] commit failed:" << db.lastError().text();
        db.rollback();
    }
}

QJsonArray LiveFlightsService::readTrackFromDb(const QString& icao24) const
{
    QJsonArray out;

    QSqlDatabase db = QSqlDatabase::database("pg_flights");
    if (!db.isValid() || !db.isOpen()) return out;

    QSqlQuery q(db);
    q.prepare(
        "SELECT EXTRACT(EPOCH FROM ts)::bigint AS t, longitude, latitude "
        "FROM flight_track_live "
        "WHERE icao24 = :icao24 "
        "ORDER BY ts ASC;"
    );
    q.bindValue(":icao24", icao24);

    if (!q.exec()) {
        qWarning() << "[Track] read failed:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        QJsonObject p;
        p.insert("t", static_cast<qint64>(q.value(0).toLongLong()));
        p.insert("lon", q.value(1).toDouble());
        p.insert("lat", q.value(2).toDouble());
        out.append(p);
    }
    return out;
}

void LiveFlightsService::selectFlight(const QString& icao24)
{
    const QString key = icao24.trimmed().toLower();
    if (!isValidIcao24(key)) {
        qWarning() << "[Track] invalid icao24:" << icao24;
        return;
    }
    if (!fetcher_) {
        qWarning() << "[Track] fetcher not set";
        return;
    }

    selectedIcao24_ = key;
    lastTrackMinuteBucket_ = -1;

    clearTrackTable();

    // find the current flight window (begin/end) using OpenSky flights/aircraft
    // Ww take the most recent flight whose lastSeen is close to "now"
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 begin = now - 12 * 3600; // last 12h

    fetcher_->fetchFlightsAircraft(
        key, begin, now,
        // onOk flights array
        [this, key, now](const QJsonArray& flights) {

            qint64 bestFirstSeen = 0;
            qint64 bestLastSeen = 0;

            for (const auto& v : flights) {
                if (!v.isObject()) continue;
                const QJsonObject o = v.toObject();
                const qint64 firstSeen = o.value("firstSeen").toVariant().toLongLong();
                const qint64 lastSeen = o.value("lastSeen").toVariant().toLongLong();
                if (firstSeen <= 0 || lastSeen <= 0) continue;

                // we want the flight that is "current-ish"
                // if OpenSky marks it ended very recently, it still works for track drawing.
                const qint64 age = std::llabs(now - lastSeen);
                if (bestLastSeen == 0 || age < std::llabs(now - bestLastSeen)) {
                    bestFirstSeen = firstSeen;
                    bestLastSeen = lastSeen;
                }
            }

            // fallback: if we can't find a flight window, we still try tracks/all at "now".
            const qint64 trackTime = (bestFirstSeen > 0) ? bestFirstSeen : now;

            fetcher_->fetchTrackAll(
                key, trackTime,
                // onOk track object
                [this, key](const QJsonObject& trackObj) {

                    // OpenSky returns: { path: [ [t, lat, lon, ...], ... ] } (commonly)
                    const QJsonArray path = trackObj.value("path").toArray();
                    if (path.isEmpty()) {
                        qWarning() << "[Track] empty path";
                        emit trackLineReady(QJsonArray{});
                        return;
                    }

                    // Downsample to 1 point per minute
                    QJsonArray sampled;
                    qint64 lastKeptT = -1;

                    for (const auto& pv : path) {
                        if (!pv.isArray()) continue;
                        const QJsonArray a = pv.toArray();
                        if (a.size() < 3) continue;

                        const qint64 t = static_cast<qint64>(a.at(0).toDouble());
                        const double lat = a.at(1).toDouble();
                        const double lon = a.at(2).toDouble();

                        if (lastKeptT < 0 || (t - lastKeptT) >= 60) {
                            QJsonObject p;
                            p.insert("t", t);
                            p.insert("lon", lon);
                            p.insert("lat", lat);
                            p.insert("source", "hist");
                            sampled.append(p);
                            lastKeptT = t;
                        }
                    }

                    insertTrackPointsToDb(key, sampled);

                    // emit full track for rendering
                    QJsonArray dbTrack = readTrackFromDb(key);

                    // update minute bucket to the last emitted point
                    if (!dbTrack.isEmpty()) {
                        const qint64 tLast = static_cast<qint64>(dbTrack.last().toObject().value("t").toDouble());
                        lastTrackMinuteBucket_ = tLast / 60;
                    }

                    emit trackLineReady(dbTrack);
                },
                // onErr track
                [this](const QString& err) {
                    qWarning() << "[Track] fetchTrackAll error:" << err;
                    emit trackLineReady(QJsonArray{});
                }
            );
        },
        // onErr flights/aircraft
        [this, key, now](const QString& err) {
            qWarning() << "[Track] fetchFlightsAircraft error:" << err;

            // fallback: try tracks/all directly
            fetcher_->fetchTrackAll(
                key, now,
                [this, key](const QJsonObject& trackObj) {
                    const QJsonArray path = trackObj.value("path").toArray();
                    QJsonArray sampled;
                    qint64 lastKeptT = -1;

                    for (const auto& pv : path) {
                        if (!pv.isArray()) continue;
                        const QJsonArray a = pv.toArray();
                        if (a.size() < 3) continue;

                        const qint64 t = static_cast<qint64>(a.at(0).toDouble());
                        const double lat = a.at(1).toDouble();
                        const double lon = a.at(2).toDouble();

                        if (lastKeptT < 0 || (t - lastKeptT) >= 60) {
                            QJsonObject p;
                            p.insert("t", t);
                            p.insert("lon", lon);
                            p.insert("lat", lat);
                            p.insert("source", "hist");
                            sampled.append(p);
                            lastKeptT = t;
                        }
                    }

                    clearTrackTable();
                    insertTrackPointsToDb(key, sampled);
                    QJsonArray dbTrack = readTrackFromDb(key);

                    if (!dbTrack.isEmpty()) {
                        const qint64 tLast = static_cast<qint64>(dbTrack.last().toObject().value("t").toDouble());
                        lastTrackMinuteBucket_ = tLast / 60;
                    }

                    emit trackLineReady(dbTrack);
                },
                [this](const QString& e2) {
                    qWarning() << "[Track] fallback fetchTrackAll error:" << e2;
                    emit trackLineReady(QJsonArray{});
                }
            );
        }
    );
}

void LiveFlightsService::clearCache()
{
    qInfo() << "[LiveFlightsService] clearCache()";

    cache_.clear();
    activeTiles_.clear();
    byIcao_.clear();
    lastMerged_ = QJsonObject();

    // we also clear selection/track state
    selectedIcao24_.clear();
    lastTrackMinuteBucket_ = -1;
}


void LiveFlightsService::maybeAppendLivePoint()
{
    if (selectedIcao24_.isEmpty()) return;

    // we use the freshest state we already maintain in memory
    auto it = byIcao_.find(selectedIcao24_);
    if (it == byIcao_.end()) return;

    const QJsonArray a = it.value();
    if (a.size() <= 6) return;

    const QJsonValue lonV = a.at(5);
    const QJsonValue latV = a.at(6);
    const QJsonValue lcV = a.at(4);

    if (!lonV.isDouble() || !latV.isDouble()) return;

    const double lon = lonV.toDouble();
    const double lat = latV.toDouble();

    // we prefer last_contact for time bucketing; fallback to now
    qint64 t = lcV.isDouble() ? static_cast<qint64>(lcV.toDouble())
        : QDateTime::currentSecsSinceEpoch();

    const qint64 bucket = t / 60;
    if (bucket <= lastTrackMinuteBucket_) return; // only once per minute

    QJsonObject p;
    p.insert("t", t);
    p.insert("lon", lon);
    p.insert("lat", lat);
    p.insert("source", "live");

    QJsonArray one;
    one.append(p);

    insertTrackPointsToDb(selectedIcao24_, one);
    lastTrackMinuteBucket_ = bucket;

    // emit updated full track
    emit trackLineReady(readTrackFromDb(selectedIcao24_));
}