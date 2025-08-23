#include "bridge.h"
#include <QDebug>
#include <QJsonObject>     
#include <QJsonDocument>   
#include "LiveFlightsService.h"

void Bridge::mouseMoved(double lat, double lon) {
    qInfo() << "[MAP]" << "lat=" << lat << "lon=" << lon;
}

void Bridge::setService(LiveFlightsService* s) {
    service_ = s;
    if (!service_) return;

    // wire once: service → bridge → JS
    connect(service_, &LiveFlightsService::flightsForTileReady,
        this, [this](const QJsonObject& obj) {
            const QString json =
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            emit flightsForTile(json);
        });

    connect(service_, &LiveFlightsService::serviceError,
        this, &Bridge::error);
}

void Bridge::requestTileAt(double lat, double lon, int z) {
    if (!service_) { emit error("LiveFlightsService not set"); return; }
    service_->requestTile(lat, lon, z);
}