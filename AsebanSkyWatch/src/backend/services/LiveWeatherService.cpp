#include "LiveWeatherService.h"
#include "OpenWeatherFetcher.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QDateTime>
#include <QJsonArray>
#include <cmath>

LiveWeatherService::LiveWeatherService(OpenWeatherFetcher* fetcher, QObject* parent)
    : QObject(parent), fetcher_(fetcher) {

    connect(fetcher_, &OpenWeatherFetcher::weatherReady, this, [this](const QJsonObject& obj) {

        // if we're sampling, upsert each sample immediately + accumulate markers
        if (samplingActive_) {
            upsertWeatherObject_(obj, currentSampleLat_, currentSampleLon_);

            QJsonObject pt;
            pt["lat"] = currentSampleLat_;
            pt["lon"] = currentSampleLon_;
            pt["tileKey"] = currentTileKey_;   // we will set this in requestWeatherSamples
            completedSamples_.append(pt);

            fetchNextSample_();
            return;
        }

        // normal single-point behavior (existing)
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
    if (!cache_.isEmpty()) {
        double lat = anchorLat_;
        double lon = anchorLon_;
        if (cache_.contains("coord") && cache_["coord"].isObject()) {
            const QJsonObject coord = cache_["coord"].toObject();
            if (coord.contains("lat")) lat = coord["lat"].toDouble(lat);
            if (coord.contains("lon")) lon = coord["lon"].toDouble(lon);
        }
        upsertWeatherObject_(cache_, lat, lon);
    }

    // refresh tile samples periodically (one tile per tick to avoid API bursts)
    refreshOneTileIfStale_();
}

void LiveWeatherService::requestWeatherSamples(const QString& tileKey,
    const QVector<QPair<double, double>>& points)
{
    activeTileSamples_[tileKey] = points;
    if (!tileLastFetchUtc_.contains(tileKey))
        tileLastFetchUtc_[tileKey] = QDateTime(); // invalid = stale

    // keep a stable list for round-robin refresh
    refreshTileKeys_ = activeTileSamples_.keys();


    if (!fetcher_ || points.isEmpty())
        return;

    currentTileKey_ = tileKey;
    samplingActive_ = true;
    pendingSamples_ = points;
    completedSamples_ = QJsonArray();

    fetchNextSample_();
}

void LiveWeatherService::clearCache()
{
    qInfo() << "[LiveWeatherService] clearCache()";

    // stop any in-progress sampling
    samplingActive_ = false;
    pendingSamples_.clear();
    completedSamples_ = QJsonArray();

    // clear single-point anchor cache
    cache_ = QJsonObject();
    cacheTs_ = QDateTime();

    // clear all active tile sampling state so it can't repopulate after TRUNCATE
    activeTileSamples_.clear();
    tileLastFetchUtc_.clear();
    refreshTileKeys_.clear();
    refreshTileIndex_ = 0;
    currentTileKey_.clear();
}

void LiveWeatherService::setValueFilter(double minTempC, double maxTempC, int minPressure, int maxPressure)
{
    if (minTempC > maxTempC) std::swap(minTempC, maxTempC);
    if (minPressure > maxPressure) std::swap(minPressure, maxPressure);

    minTempC_ = minTempC;
    maxTempC_ = maxTempC;
    minPressure_ = minPressure;
    maxPressure_ = maxPressure;

    valueFilterEnabled_ = true;

    qInfo() << "[LiveWeatherService] Value filter set:"
        << "temp" << minTempC_ << "->" << maxTempC_
        << "pressure" << minPressure_ << "->" << maxPressure_;
}

void LiveWeatherService::applyValueFilterNow()
{
    QSqlDatabase db = QSqlDatabase::database("pg_weather");
    if (!db.isOpen()) {
        emit serviceError("weather filter apply failed: pg_weather not open");
        return;
    }

    if (valueFilterEnabled_) {
        // we  delete rows that do NOT match current filter (DB becomes “reality”)
        const QString sql = QString(
            "DELETE FROM weather_live "
            "WHERE (temp_c IS NULL OR temp_c < %1 OR temp_c > %2) "
            "   OR (pressure_hpa IS NULL OR pressure_hpa < %3 OR pressure_hpa > %4);"
        )
            .arg(QString::number(minTempC_, 'f', 6))
            .arg(QString::number(maxTempC_, 'f', 6))
            .arg(QString::number(minPressure_))
            .arg(QString::number(maxPressure_));

        QSqlQuery q(db);
        if (!q.exec(sql)) {
            emit serviceError(QString("weather filter delete failed: %1").arg(q.lastError().text()));
        }
    }

    // we reemit samples so JS redraws circles according to DB after deletion
    emitFilteredSamplesForAllTiles_();
}


void LiveWeatherService::fetchNextSample_()
{
    if (pendingSamples_.isEmpty()) {
        samplingActive_ = false;
        tileLastFetchUtc_[currentTileKey_] = QDateTime::currentDateTimeUtc();
        emit weatherSamplesReady(completedSamples_);
        return;
    }

    const auto p = pendingSamples_.front();
    pendingSamples_.pop_front();

    currentSampleLat_ = p.first;
    currentSampleLon_ = p.second;

    fetcher_->fetchCurrentWeather(currentSampleLat_, currentSampleLon_);
}

void LiveWeatherService::upsertWeatherObject_(const QJsonObject& obj, double lat, double lon)
{
    auto sqlQuoted = [](QString s) -> QString {
        s.replace('\'', "''");
        return "'" + s + "'";
        };

    const qint64 fetchedAt = QDateTime::currentSecsSinceEpoch();

    // Parse fields
    double tempC = NAN, feelsC = NAN, windSpeed = NAN;
    int humidity = -1, pressure = -1, windDeg = -1, clouds = -1;
    QString weatherMain, weatherDesc, city;

    if (obj.contains("main") && obj["main"].isObject()) {
        const QJsonObject m = obj["main"].toObject();
        if (m.contains("temp"))       tempC = m.value("temp").toDouble(tempC);
        if (m.contains("feels_like")) feelsC = m.value("feels_like").toDouble(feelsC);
        if (m.contains("humidity"))   humidity = m.value("humidity").toInt(humidity);
        if (m.contains("pressure"))   pressure = m.value("pressure").toInt(pressure);
    }

    if (obj.contains("wind") && obj["wind"].isObject()) {
        const QJsonObject w = obj["wind"].toObject();
        if (w.contains("speed")) windSpeed = w.value("speed").toDouble(windSpeed);
        if (w.contains("deg"))   windDeg = w.value("deg").toInt(windDeg);
    }

    if (obj.contains("clouds") && obj["clouds"].isObject()) {
        const QJsonObject c = obj["clouds"].toObject();
        if (c.contains("all")) clouds = c.value("all").toInt(clouds);
    }

    if (obj.contains("weather") && obj["weather"].isArray()) {
        const QJsonArray arr = obj["weather"].toArray();
        if (!arr.isEmpty() && arr.at(0).isObject()) {
            const QJsonObject w0 = arr.at(0).toObject();
            weatherMain = w0.value("main").toString();
            weatherDesc = w0.value("description").toString();
        }
    }

    city = obj.value("name").toString();

    QSqlDatabase db = QSqlDatabase::database("pg_weather");
    if (!db.isOpen()) {
        emit serviceError("weather upsert failed: pg_weather not open");
        return;
    }


	// weather value filtering
    if (valueFilterEnabled_) {
        const bool tempOk =
            !std::isnan(tempC) &&
            tempC >= minTempC_ &&
            tempC <= maxTempC_;

        const bool pressureOk =
            (pressure >= 0) &&
            pressure >= minPressure_ &&
            pressure <= maxPressure_;

        if (!tempOk || !pressureOk) {
            QSqlQuery del(db);
            del.prepare(
                "DELETE FROM weather_live "
                "WHERE abs(lat - :lat) < 1e-6 "
                "  AND abs(lon - :lon) < 1e-6;"
            );
            del.bindValue(":lat", lat);
            del.bindValue(":lon", lon);
            del.exec();
            return; // we skip the insert/update
        }
    }


    auto sqlNumOrNull = [](double v) -> QString {
        return std::isnan(v) ? "NULL" : QString::number(v, 'f', 6);
        };
    auto sqlIntOrNull = [](int v) -> QString {
        return (v < 0) ? "NULL" : QString::number(v);
        };
    auto sqlTextOrNull = [&](const QString& v) -> QString {
        return v.isEmpty() ? "NULL" : sqlQuoted(v);
        };

    const QString sql = QString(
        "INSERT INTO weather_live ("
        "lat, lon, fetched_at, "
        "temp_c, feels_like_c, humidity_pct, pressure_hpa, "
        "wind_speed_ms, wind_deg, clouds_pct, "
        "weather_main, weather_desc, city_name"
        ") VALUES ("
        "%1, %2, %3, "
        "%4, %5, %6, %7, "
        "%8, %9, %10, "
        "%11, %12, %13"
        ") ON CONFLICT (lat, lon) DO UPDATE SET "
        "fetched_at=EXCLUDED.fetched_at, "
        "temp_c=EXCLUDED.temp_c, "
        "feels_like_c=EXCLUDED.feels_like_c, "
        "humidity_pct=EXCLUDED.humidity_pct, "
        "pressure_hpa=EXCLUDED.pressure_hpa, "
        "wind_speed_ms=EXCLUDED.wind_speed_ms, "
        "wind_deg=EXCLUDED.wind_deg, "
        "clouds_pct=EXCLUDED.clouds_pct, "
        "weather_main=EXCLUDED.weather_main, "
        "weather_desc=EXCLUDED.weather_desc, "
        "city_name=EXCLUDED.city_name, "
        "time=NOW();"
    )
        .arg(QString::number(lat, 'f', 6))
        .arg(QString::number(lon, 'f', 6))
        .arg(QString::number(fetchedAt))
        .arg(sqlNumOrNull(tempC))
        .arg(sqlNumOrNull(feelsC))
        .arg(sqlIntOrNull(humidity))
        .arg(sqlIntOrNull(pressure))
        .arg(sqlNumOrNull(windSpeed))
        .arg(sqlIntOrNull(windDeg))
        .arg(sqlIntOrNull(clouds))
        .arg(sqlTextOrNull(weatherMain))
        .arg(sqlTextOrNull(weatherDesc))
        .arg(sqlTextOrNull(city));

    QSqlQuery q(db);
    if (!q.exec(sql)) {
        emit serviceError(QString("weather upsert failed: %1").arg(q.lastError().text()));
    }
}

bool LiveWeatherService::tileIsStale_(const QString& tileKey) const
{
    const QDateTime last = tileLastFetchUtc_.value(tileKey);
    if (!last.isValid())
        return true;

    const int age = last.secsTo(QDateTime::currentDateTimeUtc());
    return age >= tileRefreshSeconds_;
}

void LiveWeatherService::emitFilteredSamplesForAllTiles_()
{
    // we rebuild for each tileKey we have sampled points for
    for (auto it = activeTileSamples_.cbegin(); it != activeTileSamples_.cend(); ++it) {
        const QString tileKey = it.key();
        const QJsonArray arr = buildFilteredTileArray_(tileKey);
        emit weatherSamplesReady(arr); // the bridge forwards to JS are unchanged here
    }
}

QJsonArray LiveWeatherService::buildFilteredTileArray_(const QString& tileKey)
{
    QJsonArray out;

    const auto points = activeTileSamples_.value(tileKey);
    if (points.isEmpty())
        return out;

    QSqlDatabase db = QSqlDatabase::database("pg_weather");
    if (!db.isOpen())
        return out;

    // we rely on DB being already filtered (applyValueFilterNow deletes non-matching),
    // so “exists row” == “should show”.
    QSqlQuery q(db);
    q.prepare(
        "SELECT 1 FROM weather_live "
        "WHERE abs(lat - :lat) < 1e-6 AND abs(lon - :lon) < 1e-6 "
        "LIMIT 1;"
    );

    for (const auto& p : points) {
        const double lat = p.first;
        const double lon = p.second;

        q.bindValue(":lat", lat);
        q.bindValue(":lon", lon);

        if (!q.exec()) {
            // if something goes wrong, fail “closed” (don’t draw)
            continue;
        }

        if (q.next()) {
            QJsonObject pt;
            pt["lat"] = lat;
            pt["lon"] = lon;
            pt["tileKey"] = tileKey;
            out.append(pt);
        }
    }

    return out;
}

void LiveWeatherService::refreshOneTileIfStale_()
{
    // don't refresh while a sampling batch is already running
    if (samplingActive_)
        return;

    if (activeTileSamples_.isEmpty())
        return;

    // if keys list got out of sync, rebuild
    if (refreshTileKeys_.size() != activeTileSamples_.size()) {
        refreshTileKeys_ = activeTileSamples_.keys();
        refreshTileIndex_ = 0;
    }
    if (refreshTileKeys_.isEmpty())
        return;

    // round-robin pick
    if (refreshTileIndex_ >= refreshTileKeys_.size())
        refreshTileIndex_ = 0;

    const QString tileKey = refreshTileKeys_.at(refreshTileIndex_++);
    if (!activeTileSamples_.contains(tileKey))
        return;

    if (!tileIsStale_(tileKey))
        return;

    // trigger a refresh fetch for that tile (same requestWeatherSamples path)
    const auto points = activeTileSamples_.value(tileKey);

    // this will upsert all points and then update tileLastFetchUtc_
    requestWeatherSamples(tileKey, points);
}
