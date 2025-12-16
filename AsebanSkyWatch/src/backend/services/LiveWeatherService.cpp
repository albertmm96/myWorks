#include "LiveWeatherService.h"
#include "OpenWeatherFetcher.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

LiveWeatherService::LiveWeatherService(OpenWeatherFetcher* fetcher, QObject* parent)
    : QObject(parent), fetcher_(fetcher) {

    connect(fetcher_, &OpenWeatherFetcher::weatherReady, this, [this](const QJsonObject& obj) {
        cache_ = obj;
        cacheTs_ = QDateTime::currentDateTimeUtc();
        emit weatherReady(obj); // forward to Bridge/UI
        });

    connect(fetcher_, &OpenWeatherFetcher::fetchError, this, [this](const QString& msg) {
        emit serviceError(msg);
        });
}

void LiveWeatherService::requestWeather(double lat, double lon) {
    hasAnchor_ = true;
    anchorLat_ = lat;
    anchorLon_ = lon;

    if (fetcher_) fetcher_->fetchCurrentWeather(lat, lon);
}

void LiveWeatherService::onTick(){
    if (cache_.isEmpty()) return;

    // we prefer lat/lon from the response if present; otherwise fall back to your anchor
    double lat = anchorLat_;
    double lon = anchorLon_;
    if (cache_.contains("coord") && cache_["coord"].isObject()) {
        const QJsonObject coord = cache_["coord"].toObject();
        if (coord.contains("lat")) lat = coord["lat"].toDouble(lat);
        if (coord.contains("lon")) lon = coord["lon"].toDouble(lon);
    }

    const qint64 fetchedAt = QDateTime::currentSecsSinceEpoch();
    const QByteArray payloadJson = QJsonDocument(cache_).toJson(QJsonDocument::Compact);

    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        emit serviceError("weather upsert failed: DB not open");
        return;
    }

    QSqlQuery q(db);
    q.prepare(R"SQL(
        INSERT INTO weather_live (lat, lon, fetched_at, payload)
        VALUES (:lat, :lon, :fetched_at, CAST(:payload AS jsonb))
        ON CONFLICT (lat, lon) DO UPDATE
        SET fetched_at = EXCLUDED.fetched_at,
            payload    = EXCLUDED.payload
    )SQL");

    q.bindValue(":lat", lat);
    q.bindValue(":lon", lon);
    q.bindValue(":fetched_at", fetchedAt);
    q.bindValue(":payload", QString::fromUtf8(payloadJson));

    if (!q.exec()) {
        emit serviceError(QString("weather upsert failed: %1").arg(q.lastError().text()));
    }

    // Auto-refresh: fetch again if TTL expired
    if (!hasAnchor_ || !fetcher_) return;

    const qint64 ageSec = cacheTs_.isValid()
        ? cacheTs_.secsTo(QDateTime::currentDateTimeUtc())
        : (ttlSeconds_ + 1);

    if (ageSec > ttlSeconds_) {
        fetcher_->fetchCurrentWeather(anchorLat_, anchorLon_);
    }

}