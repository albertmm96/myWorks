#include "LiveWeatherService.h"
#include "OpenWeatherFetcher.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QDateTime>
#include <cmath>

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

void LiveWeatherService::onTick()
{
    if (cache_.isEmpty())
        return;

    auto sqlQuoted = [](QString s) -> QString {
        s.replace('\'', "''");
        return "'" + s + "'";
        };

    QSqlDatabase db = QSqlDatabase::database("pg_weather");
    if (!db.isOpen()) {
        emit serviceError("weather upsert failed: pg_weather not open");
        return;
    }

    // choose lat/lon: we prefer anchor (we set it on click), but if coord exists we use it.
    double lat = anchorLat_;
    double lon = anchorLon_;
    if (cache_.contains("coord") && cache_["coord"].isObject()) {
        const QJsonObject coord = cache_["coord"].toObject();
        if (coord.contains("lat")) lat = coord["lat"].toDouble(lat);
        if (coord.contains("lon")) lon = coord["lon"].toDouble(lon);
    }

    const qint64 fetchedAt = QDateTime::currentSecsSinceEpoch();
    const QString payload = QString::fromUtf8(
        QJsonDocument(cache_).toJson(QJsonDocument::Compact)
    );

    const QString sql = QString(
        "INSERT INTO weather_live (lat, lon, fetched_at, payload) "
        "VALUES (%1, %2, %3, CAST(%4 AS jsonb)) "
        "ON CONFLICT (lat, lon) DO UPDATE SET "
        "fetched_at = EXCLUDED.fetched_at, "
        "payload    = EXCLUDED.payload;"
    )
        .arg(QString::number(lat, 'f', 6))
        .arg(QString::number(lon, 'f', 6))
        .arg(QString::number(fetchedAt))
        .arg(sqlQuoted(payload));

    QSqlQuery q(db);
    if (!q.exec(sql)) {
        emit serviceError(QString("weather upsert failed: %1").arg(q.lastError().text()));
        return;
    }

    // Optional auto-refresh on TTL (only if you want no-click refresh)
    if (!hasAnchor_ || !fetcher_)
        return;

    const qint64 ageSec = cacheTs_.isValid()
        ? cacheTs_.secsTo(QDateTime::currentDateTimeUtc())
        : (ttlSeconds_ + 1);

    if (ageSec > ttlSeconds_) {
        fetcher_->fetchCurrentWeather(anchorLat_, anchorLon_);
    }
}
