// bridge.cpp
#include "bridge.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include "LiveFlightsService.h"

void Bridge::mouseMoved(double lat, double lon) {
    qInfo() << "[MAP]" << "lat=" << lat << "lon=" << lon;
}

void Bridge::setService(LiveFlightsService* s) {
    service_ = s;
    if (!service_) return;

    // forward merged snapshots to JS, applying filter if enabled
    connect(service_, &LiveFlightsService::flightsMergedReady,
        this, [this](const QJsonObject& obj) {
            // always keep the full snapshot as compact JSON
            lastJson_ = QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact));

            emitFilteredJson(obj);   // <-- IMPORTANT: use the filter
        });

    connect(service_, &LiveFlightsService::serviceError,
        this, &Bridge::error);
}

void Bridge::requestTileAt(double lat, double lon, int z) {
    if (!service_) {
        emit error("LiveFlightsService not set");
        return;
    }

    // Each click: reset filter so all flights for the new tiles are shown
    filterEnabled_ = false;

    service_->requestTile(lat, lon, z);
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

    // re-apply filter on the last snapshot so the map updates immediately
    if (lastJson_.isEmpty())
        return;

    const auto doc = QJsonDocument::fromJson(lastJson_.toUtf8());
    if (!doc.isObject())
        return;

    emitFilteredJson(doc.object());
}

void Bridge::emitFilteredJson(const QJsonObject& obj)
{
    // If filter off, just send the last JSON as-is
    if (!filterEnabled_) {
        if (!lastJson_.isEmpty())
            emit flightsForTile(lastJson_);
        return;
    }

    const QJsonArray inStates = obj.value("states").toArray();
    QJsonArray outStates;          // ✅ no reserve()

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

    lastJson_ = json;              // keep the filtered version too

    qDebug() << "[Bridge] filtering" << inStates.size() << "states with rect lat"
        << minLat_ << "->" << maxLat_
        << "lon" << minLon_ << "->" << maxLon_
        << "=> kept" << outStates.size();

    emit flightsForTile(json);     // JS redraws with filtered flights
}