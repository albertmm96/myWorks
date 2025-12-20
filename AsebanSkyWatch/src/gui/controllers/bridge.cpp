// bridge.cpp
#include "bridge.h"
#include "LiveFlightsService.h"
#include "LiveWeatherService.h"

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
            // we always keep the full snapshot as compact JSON
            lastJsonFull_ = QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact));

            emitFilteredJson(obj);
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

    // on each click, reset filter so all flights for the new tiles are shown
    filterEnabled_ = false;
    lastJsonFiltered_.clear();

    if (service_) {
        service_->clearGeoFilter();
    }

    service_->requestTile(lat, lon, z);

    // we update weather by requesting at the clicked point (anchor is managed by LiveWeatherService)
    requestWeatherAt(lat, lon);
}

void Bridge::onMasterTick() {
    // we flush flights cache to DB
    if (service_) service_->onTick();

    // we flush weather cache to DB
    if (weatherService_) weatherService_->onTick();

    qInfo() << "[MasterTick]";

}

void Bridge::setGeoFilter(double minLat, double maxLat,
    double minLon, double maxLon)
{
    // normalize in case caller swaps min/max
    if (minLat > maxLat) std::swap(minLat, maxLat);
    if (minLon > maxLon) std::swap(minLon, maxLon);

    minLat_ = minLat;
    maxLat_ = maxLat;
    minLon_ = minLon;
    maxLon_ = maxLon;
    filterEnabled_ = true;

    if (service_) service_->setGeoFilter(minLat_, maxLat_, minLon_, maxLon_);

    // if the range is degenerate (slider at its minimum etc.), treat as "no filter"
    if ((maxLat_ - minLat_) < 1e-6 || (maxLon_ - minLon_) < 1e-6) {
        filterEnabled_ = false;
        lastJsonFiltered_.clear();
        if (!lastJsonFull_.isEmpty())
            emit flightsForTile(lastJsonFull_);
        return;
    }

    // we re-apply filter on the last *full* snapshot so the map updates immediately
    if (lastJsonFull_.isEmpty())
        return;

    const auto doc = QJsonDocument::fromJson(lastJsonFull_.toUtf8());
    if (!doc.isObject())
        return;

    emitFilteredJson(doc.object());
}

void Bridge::emitFilteredJson(const QJsonObject& obj)
{
    // if filter off, just send the full JSON as-is
    if (!filterEnabled_) {
        if (!lastJsonFull_.isEmpty())
            emit flightsForTile(lastJsonFull_);
        return;
    }

    const QJsonArray inStates = obj.value("states").toArray();
    QJsonArray outStates;

    for (const auto& v : inStates) {
        if (!v.isArray()) continue;
        const QJsonArray a = v.toArray();
        if (a.size() <= 6) continue;

        const QJsonValue lonV = a.at(5);
        const QJsonValue latV = a.at(6);
        if (!lonV.isDouble() || !latV.isDouble()) continue;

        const double lon = lonV.toDouble();
        const double lat = latV.toDouble();

        if (lat >= minLat_ && lat <= maxLat_ &&
            lon >= minLon_ && lon <= maxLon_) {
            outStates.append(a);
        }
    }

    QJsonObject outObj = obj;
    outObj.insert("states", outStates);

    const QString json = QString::fromUtf8(
        QJsonDocument(outObj).toJson(QJsonDocument::Compact));

    lastJsonFiltered_ = json;   // DO NOT overwrite lastJsonFull_

    qDebug() << "[Bridge] filtering" << inStates.size() << "states with rect lat"
        << minLat_ << "->" << maxLat_
        << "lon" << minLon_ << "->" << maxLon_
        << "=> kept" << outStates.size();

    emit flightsForTile(json);
}