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

    // lat/lon: prefer response coord, otherwise anchor
    double lat = anchorLat_;
    double lon = anchorLon_;
    if (cache_.contains("coord") && cache_["coord"].isObject()) {
        const QJsonObject coord = cache_["coord"].toObject();
        if (coord.contains("lat")) lat = coord["lat"].toDouble(lat);
        if (coord.contains("lon")) lon = coord["lon"].toDouble(lon);
    }

    const qint64 fetchedAt = QDateTime::currentSecsSinceEpoch();

    // Parse fields
    double tempC = NAN, feelsC = NAN, windSpeed = NAN;
    int humidity = -1, pressure = -1, windDeg = -1, clouds = -1;
    QString weatherMain, weatherDesc, city;

    if (cache_.contains("main") && cache_["main"].isObject()) {
        const QJsonObject m = cache_["main"].toObject();
        if (m.contains("temp"))       tempC = m.value("temp").toDouble(tempC);
        if (m.contains("feels_like")) feelsC = m.value("feels_like").toDouble(feelsC);
        if (m.contains("humidity"))   humidity = m.value("humidity").toInt(humidity);
        if (m.contains("pressure"))   pressure = m.value("pressure").toInt(pressure);
    }

    if (cache_.contains("wind") && cache_["wind"].isObject()) {
        const QJsonObject w = cache_["wind"].toObject();
        if (w.contains("speed")) windSpeed = w.value("speed").toDouble(windSpeed);
        if (w.contains("deg"))   windDeg = w.value("deg").toInt(windDeg);
    }

    if (cache_.contains("clouds") && cache_["clouds"].isObject()) {
        const QJsonObject c = cache_["clouds"].toObject();
        if (c.contains("all")) clouds = c.value("all").toInt(clouds);
    }

    if (cache_.contains("weather") && cache_["weather"].isArray()) {
        const QJsonArray arr = cache_["weather"].toArray();
        if (!arr.isEmpty() && arr.at(0).isObject()) {
            const QJsonObject w0 = arr.at(0).toObject();
            weatherMain = w0.value("main").toString();
            weatherDesc = w0.value("description").toString();
        }
    }

    city = cache_.value("name").toString();

    QSqlDatabase db = QSqlDatabase::database("pg_weather");
    if (!db.isOpen()) {
        emit serviceError("weather upsert failed: pg_weather not open");
        return;
    }

    // we convert values to SQL literals (NULL where missing)
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
        return;
    }
}