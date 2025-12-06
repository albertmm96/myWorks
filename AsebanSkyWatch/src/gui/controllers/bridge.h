// bridge.h
#pragma once
#include <QObject>

class LiveFlightsService;  // fwd declare

class Bridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void setService(LiveFlightsService* s);

    // returns the last JSON payload rendered on the map
    Q_INVOKABLE QString currentFlightsJson() const { return lastJson_; }

    // sets / updates the active geographic filter (in degrees)
    Q_INVOKABLE void setGeoFilter(double minLat, double maxLat,
        double minLon, double maxLon);

public slots:
    void mouseMoved(double lat, double lon);
    void requestTileAt(double lat, double lon, int z);

signals:
    void flightsForTile(const QString& statesJson);
    void error(const QString& message);

private:
    LiveFlightsService* service_ = nullptr; // not owned

    // cache what we last emitted to JS (the exact flights currently displayed)
    QString lastJson_;

    // --- geo filter rectangle (in degrees) ---
    bool   filterEnabled_ = false;
    double minLat_ = -90.0;
    double maxLat_ = +90.0;
    double minLon_ = -180.0;
    double maxLon_ = +180.0;

    void emitFilteredJson(const QJsonObject& obj);
};