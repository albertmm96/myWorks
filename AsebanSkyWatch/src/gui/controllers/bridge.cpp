// bridge.cpp
#include "bridge.h"
#include "LiveFlightsService.h"
#include "LiveWeatherService.h"
#include "tile_math.h"

#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDateTime>
#include <QRegularExpression>

void Bridge::mouseMoved(double lat, double lon) {
    qInfo() << "[MAP]" << "lat=" << lat << "lon=" << lon;
}

void Bridge::setService(LiveFlightsService* s) {
    service_ = s;
    if (!service_) return;

    // we forward merged snapshots to JS, applying filter if enabled
    connect(service_, &LiveFlightsService::flightsMergedReady,
        this, [this](const QJsonObject& obj) {
            lastJsonFull_ = QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact));
            emit flightsForTile(lastJsonFull_);
        });

    connect(service_, &LiveFlightsService::serviceError,
        this, &Bridge::error);

    connect(service_, &LiveFlightsService::trackLineReady,
        this, [this](const QJsonArray& arr) {
            const QString json = QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact));
            emit trackLineReady(json);
        });

	//  master timer to drive periodic updates
    if (!masterTimer_) {
        masterTimer_ = new QTimer(this);
        connect(masterTimer_, &QTimer::timeout, this, &Bridge::onMasterTick);
        masterTimer_->start(masterTickMs_);
    }

}

void Bridge::setWeatherService(LiveWeatherService* s)
{
    weatherService_ = s;
    if (!weatherService_) return;

    qInfo() << "[Bridge] Weather service attached";

    // Forward weather JSON to JS only for now (later we can filter the DB)
    connect(weatherService_, &LiveWeatherService::weatherReady,
        this, [this](const QJsonObject& obj) {

            const QString json = QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact));

            emit weatherForTile(json);
        });

    connect(weatherService_, &LiveWeatherService::weatherSamplesReady,
        this, [this](const QJsonArray& arr) {

            const QString json = QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact));

            emit weatherSamplesForTile(json);
        });


    connect(weatherService_, &LiveWeatherService::serviceError,
        this, &Bridge::error);

    // Ensure the single global clock exists (master timer).
    if (!masterTimer_) {
        masterTimer_ = new QTimer(this);
        masterTimer_->setInterval(masterTickMs_);
        connect(masterTimer_, &QTimer::timeout, this, &Bridge::onMasterTick);
        masterTimer_->start();

        qInfo() << "[Bridge] Master timer started, interval(ms)="
            << masterTimer_->interval();
    }
}

void Bridge::requestWeatherAt(double lat, double lon)
{
    lastWeatherLat_ = lat;
    lastWeatherLon_ = lon;

    qInfo() << "[Bridge] Request weather at" << lat << lon;

    if (weatherService_) {
        weatherService_->requestWeather(lat, lon);
    }
    else {
        emit error("LiveWeatherService not set");
    }
}


void Bridge::requestTileAt(double lat, double lon, int z)
{
    // =========================
    // 1) FLIGHTS
    // =========================
    if (service_ && flightsEnabled_) {
        service_->requestTile(lat, lon, z);
    }

    // =========================
    // 2) WEATHER – TILE SAMPLING
    // =========================
    
    if (!weatherEnabled_) {
        return;
    }

    if (!weatherService_) {
        emit error("LiveWeatherService not set");
        return;
    }

    // compute tile indices from clicked point
    const auto [tx, ty] = tilemath::lonLatToTile(lat, lon, z);

    // stable tile identity (used for replacement + styling)
    const QString tileKey = QString("%1/%2/%3").arg(z).arg(tx).arg(ty);

    // tile bounding box
    const auto [minLat, minLon, maxLat, maxLon] =
        tilemath::tileBBox(tx, ty, z);

    // sampling resolution
    const int N = 4;               // 4x4 grid
    const double marginFrac = 0.12; // avoid edges

    const double lat0 = minLat + (maxLat - minLat) * marginFrac;
    const double lat1 = maxLat - (maxLat - minLat) * marginFrac;
    const double lon0 = minLon + (maxLon - minLon) * marginFrac;
    const double lon1 = maxLon - (maxLon - minLon) * marginFrac;

    QVector<QPair<double, double>> pts;
    pts.reserve(N * N + 1);

    // include clicked point (anchor sample)
    pts.push_back({ lat, lon });

    // regular grid sampling across the tile
    for (int iy = 0; iy < N; ++iy) {
        const double tY = (N == 1)
            ? 0.5
            : static_cast<double>(iy) / static_cast<double>(N - 1);
        const double sLat = lat0 + (lat1 - lat0) * tY;

        for (int ix = 0; ix < N; ++ix) {
            const double tX = (N == 1)
                ? 0.5
                : static_cast<double>(ix) / static_cast<double>(N - 1);
            const double sLon = lon0 + (lon1 - lon0) * tX;

            pts.push_back({ sLat, sLon });
        }
    }

    // trigger multi-point weather sampling for this tile
    weatherService_->requestWeatherSamples(tileKey, pts);
}

void Bridge::selectFlight(const QString& icao24)
{
    if (!service_) {
        emit error("LiveFlightsService not set");
        return;
    }

    // accept only ICAO24 hex (6 chars)
    static QRegularExpression re("^[0-9a-fA-F]{6}$");
    const QString key = icao24.trimmed();
    if (!re.match(key).hasMatch()) {
        emit error("Invalid icao24");
        return;
    }

    emit trackCleared();
    service_->selectFlight(key);
}

void Bridge::onMasterTick() {
	// we flush flights cache to DB flights show checkbox is enabled
    if (service_ && flightsEnabled_) service_->onTick();

	// we flush weather cache to DB and weather show checkbox is enabled
    if (weatherService_ && weatherEnabled_) weatherService_->onTick();

    qInfo() << "[MasterTick]";

}

void Bridge::setFlightsEnabled(bool enabled)
{
    flightsEnabled_ = enabled;
    qInfo() << "[Bridge] flightsEnabled_ =" << flightsEnabled_;

    if (!enabled) {
        // stop showing immediately
        emit clearFlights();

        // clear DB
        QSqlDatabase db = QSqlDatabase::database("pg_flights");
        if (db.isValid() && db.isOpen()) {
            QSqlQuery q(db);
            if (!q.exec("TRUNCATE TABLE states_live;")) {
                qWarning() << "[Bridge] TRUNCATE states_live failed:" << q.lastError().text();
            }
        }

        // 3) clear service caches so old data cannot “come back”
        if (service_) {
            service_->clearCache();
        }
    }
}

void Bridge::setWeatherEnabled(bool enabled)
{
    weatherEnabled_ = enabled;
    qInfo() << "[Bridge] weatherEnabled_ =" << weatherEnabled_;

    if (!enabled) {
        // 1) stop showing immediately
        emit clearWeather();

        // 2) clear DB
        QSqlDatabase db = QSqlDatabase::database("pg_weather");
        if (db.isValid() && db.isOpen()) {
            QSqlQuery q(db);
            if (!q.exec("TRUNCATE TABLE weather_live;")) {
                qWarning() << "[Bridge] TRUNCATE weather_live failed:" << q.lastError().text();
            }
        }

        // 3) clear weather service caches (active tiles etc.)
        if (weatherService_) {
            weatherService_->clearCache();
        }
    }
}


void Bridge::setGeoFilter(double minLat, double maxLat, double minLon, double maxLon)
{
    if (minLat > maxLat) std::swap(minLat, maxLat);
    if (minLon > maxLon) std::swap(minLon, maxLon);

    minLat_ = minLat;
    maxLat_ = maxLat;
    minLon_ = minLon;
    maxLon_ = maxLon;
    filterEnabled_ = true;

    if (service_) service_->setGeoFilter(minLat_, maxLat_, minLon_, maxLon_);
}