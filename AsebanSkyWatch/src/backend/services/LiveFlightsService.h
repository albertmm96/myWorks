#pragma once
#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QTimer>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include "LiveFlightsService.h"

class OpenSkyFetcher; // forward

// key for tiles cache (x,y,z)  -
struct TileKey {
    int x;
    int y;
    int z;
    bool operator==(const TileKey& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

// non-member qHash for QHash<TileKey, ...>
inline uint qHash(const TileKey& key, uint seed = 0) noexcept {
    // we combine x,y,z in a 64-bit then delegate it to qHash(quint64)
    const quint64 h = (quint64(key.z) << 40) ^ (quint64(key.x) << 20) ^ quint64(key.y);
    return qHash(h, seed);
}

class LiveFlightsService : public QObject {
    Q_OBJECT
public:
    explicit LiveFlightsService(OpenSkyFetcher* fetcher, QObject* parent = nullptr);

    void requestTile(double lat, double lon, int z);
    void setTtlSeconds(int s) { ttlSeconds_ = s; }

    // geo-rectangle filter (lat/lon). When enabled, states outside the rectangle
    // are excluded from merging and from DB flush. The service will also purge
    // existing DB rows outside the rectangle to keep DB consistent with UI.
    void setGeoFilter(double minLat, double maxLat, double minLon, double maxLon);
    void clearGeoFilter();
    void onTick(); // called by Bridge master timer
    void selectFlight(const QString& icao24);

signals:
    void flightsForTileReady(const QJsonObject& obj);
    void serviceError(const QString& msg);
    void flightsMergedReady(const QJsonObject& obj);
    void trackLineReady(const QJsonArray& points);

private:
    struct CacheEntry { QJsonObject payload; QDateTime ts; };
    void fetchTile(const TileKey& key);

    OpenSkyFetcher* fetcher_ = nullptr;
    QHash<TileKey, CacheEntry> cache_;
    int ttlSeconds_ = 15;
    QSet<TileKey> activeTiles_;

    QHash<QString, QJsonArray> byIcao_;
    int staleSeconds_ = 180;

    QJsonObject lastMerged_; // last union-of-tiles payload

    // Filter state
    bool   filterEnabled_ = false;
    double minLat_ = -90.0;
    double maxLat_ = +90.0;
    double minLon_ = -180.0;
    double maxLon_ = +180.0;

    bool passesGeoFilter(const QJsonArray& state) const;
    void purgeDbOutsideFilter();

    void foldStatesIntoMerged(const QJsonObject& obj);
    void emitMerged();
    void pruneStale();
    QJsonObject readStatesLiveFromDb() const;
    void emitFromDb();

    void clearTrackTable();
    void insertTrackPointsToDb(const QString& icao24, const QJsonArray& points);
    QJsonArray readTrackFromDb(const QString& icao24) const;
    void maybeAppendLivePoint();

    QString selectedIcao24_;
    qint64 lastTrackMinuteBucket_ = -1;
};