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
    if (!service_) {
        emit error("LiveFlightsService not set");
        return;
    }

    // on each click, we reset any backend geo filter so DB can repopulate freely
    filterEnabled_ = false;

    if (service_) {
        service_->clearGeoFilter();
    }

    service_->requestTile(lat, lon, z);

    // we sample multiple points across the clicked tile and fetch weather at each point
    if (!weatherService_) {
        emit error("LiveWeatherService not set");
        return;
    }

    const auto [tx, ty] = tilemath::lonLatToTile(lat, lon, z);
    const auto [minLat, minLon, maxLat, maxLon] = tilemath::tileBBox(tx, ty, z);

    // sampling density (tune). 4x4 = 16 calls (+ optional clicked point)
    const int N = 4;

    // keep samples away from exact edges so adjacent tiles don't duplicate too hard
    const double marginFrac = 0.12;

    const double lat0 = minLat + (maxLat - minLat) * marginFrac;
    const double lat1 = maxLat - (maxLat - minLat) * marginFrac;
    const double lon0 = minLon + (maxLon - minLon) * marginFrac;
    const double lon1 = maxLon - (maxLon - minLon) * marginFrac;

    QVector<QPair<double, double>> pts;
    pts.reserve(N * N + 1);

    for (int iy = 0; iy < N; ++iy) {
        const double tY = (N == 1) ? 0.5 : double(iy) / double(N - 1);
        const double sLat = lat0 + (lat1 - lat0) * tY;

        for (int ix = 0; ix < N; ++ix) {
            const double tX = (N == 1) ? 0.5 : double(ix) / double(N - 1);
            const double sLon = lon0 + (lon1 - lon0) * tX;

            pts.push_back({ sLat, sLon });
        }
    }

    weatherService_->requestWeatherSamples(pts);
}

void Bridge::onMasterTick() {
    // we flush flights cache to DB
    if (service_) service_->onTick();

    // we flush weather cache to DB
    if (weatherService_) weatherService_->onTick();

    qInfo() << "[MasterTick]";

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