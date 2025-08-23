#pragma once
#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QJsonObject>

class OpenSkyFetcher; // forward

// ---- key for tiles cache (x,y,z) ----
struct TileKey {
    int x;
    int y;
    int z;
    bool operator==(const TileKey& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

// ---- non-member qHash for QHash<TileKey, ...> ----
inline uint qHash(const TileKey& key, uint seed = 0) noexcept {
    // we combine x,y,z in a 64-bit then delegate it to qHash(quint64)
    const quint64 h = (quint64(key.z) << 40) ^ (quint64(key.x) << 20) ^ quint64(key.y);
    return qHash(h, seed);
}

class LiveFlightsService : public QObject {
    Q_OBJECT
public:
    explicit LiveFlightsService(OpenSkyFetcher* fetcher, QObject* parent = nullptr);

    // API is called from bridge (GUI)
    void requestTile(double lat, double lon, int z);

    // tile cache TTL (in seconds) → OpenSky response
    void setTtlSeconds(int s) { ttlSeconds_ = s; }

signals:
    // service → bridge : payload { time: <int>, states: [...] }
    void flightsForTileReady(const QJsonObject& obj);
    void serviceError(const QString& msg);

private:
    struct CacheEntry { QJsonObject payload; QDateTime ts; };
    void fetchTile(const TileKey& key);

private:
    OpenSkyFetcher* fetcher_ = nullptr;  // non-owning
    QHash<TileKey, CacheEntry> cache_;
    int ttlSeconds_ = 15;                // default
};