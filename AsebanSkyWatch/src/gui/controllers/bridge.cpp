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
            lastJson_ = QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact));

            emitFilteredJson(obj);   //   we use the filter
        });

    connect(service_, &LiveFlightsService::serviceError,
        this, &Bridge::error);
}

void Bridge::setWeatherService(LiveWeatherService* s)
{
    weatherService_ = s;
    if (!weatherService_) return;

    qInfo() << "[Bridge] Weather service attached";

    connect(weatherService_, &LiveWeatherService::weatherReady,
        this, [this](const QJsonObject& obj) {

            //   1) STORE WEATHER IN DB  
            QSqlDatabase db = QSqlDatabase::database();
            if (db.isOpen()) {
                QSqlQuery q(db);
                q.prepare(
                    "INSERT INTO weather_live(lat, lon, fetched_at, payload) "
                    "VALUES (:lat, :lon, :t, CAST(:p AS jsonb)) "
                    "ON CONFLICT(lat, lon) DO UPDATE SET "
                    "fetched_at = EXCLUDED.fetched_at, "
                    "payload    = EXCLUDED.payload"
                );

                q.bindValue(":lat", lastWeatherLat_);
                q.bindValue(":lon", lastWeatherLon_);
                q.bindValue(":t", QDateTime::currentSecsSinceEpoch());

                const QString payloadText =
                    QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
                q.bindValue(":p", payloadText);

                if (!q.exec()) {
                    emit error(QString("weather DB insert failed: %1")
                        .arg(q.lastError().text()));
                }
            }
            else {
                emit error("weather DB insert failed: DB not open");
            }

            //   2) STILL SEND TO JS  
            const QString json = QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact));
            emit weatherForTile(json);
        });

    connect(weatherService_, &LiveWeatherService::serviceError,
        this, &Bridge::error);

    //  periodic weather refresh
    if (!weatherRefreshTimer_) {
        weatherRefreshTimer_ = new QTimer(this);
        weatherRefreshTimer_->setInterval(weatherRefreshMs_);

        connect(weatherRefreshTimer_, &QTimer::timeout, this, [this]() {
            qInfo() << "[Bridge] Weather timer tick. hasAnchor=" << hasWeatherAnchor_
                << "anchorLat=" << weatherAnchorLat_
                << "anchorLon=" << weatherAnchorLon_;

            if (!weatherService_) return;
            if (!hasWeatherAnchor_) return;

            requestWeatherAt(weatherAnchorLat_, weatherAnchorLon_);
            });

        weatherRefreshTimer_->start();
        qInfo() << "[Bridge] Weather timer started, interval(ms)="
            << weatherRefreshTimer_->interval();
    }
}

void Bridge::requestWeatherAt(double lat, double lon)
{
    if (!weatherService_) return;

    // we remember where this weather comes from
    lastWeatherLat_ = lat;
    lastWeatherLon_ = lon;

    weatherService_->requestWeather(lat, lon);
}

void Bridge::requestTileAt(double lat, double lon, int z) {
    if (!service_) {
        emit error("LiveFlightsService not set");
        return;
    }

    // Each click: reset filter so all flights for the new tiles are shown
    filterEnabled_ = false;

    service_->requestTile(lat, lon, z);

    // we update the weather “anchor” location to the clicked point
    hasWeatherAnchor_ = true;
    weatherAnchorLat_ = lat;
    weatherAnchorLon_ = lon;
    qInfo() << "[Bridge] Weather anchor set to" << weatherAnchorLat_ << weatherAnchorLon_;
    requestWeatherAt(lat, lon);
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

    // we re-apply filter on the last snapshot so the map updates immediately
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

    lastJson_ = json;              // we keep the filtered version too

    qDebug() << "[Bridge] filtering" << inStates.size() << "states with rect lat"
        << minLat_ << "->" << maxLat_
        << "lon" << minLon_ << "->" << maxLon_
        << "=> kept" << outStates.size();

    emit flightsForTile(json);     // JS redraws with filtered flights
}