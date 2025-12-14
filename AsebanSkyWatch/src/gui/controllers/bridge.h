// bridge.h
#pragma once
#include <QObject>
#include <QTimer>

class LiveFlightsService;
class LiveWeatherService;

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

	// same for the weather service
    void setWeatherService(LiveWeatherService* s);
    Q_INVOKABLE void requestWeatherAt(double lat, double lon);

public slots:
    void mouseMoved(double lat, double lon);
    void requestTileAt(double lat, double lon, int z);

signals:
    void flightsForTile(const QString& statesJson);
    void weatherForTile(const QString& weatherJson);
    void error(const QString& message);

private:
    LiveFlightsService* service_ = nullptr;
    LiveWeatherService* weatherService_ = nullptr;

    // cache what we last emitted to JS (the exact flights currently displayed)
    QString lastJson_;

    //   geo filter rectangle (in degrees)  
    bool   filterEnabled_ = false;
    double minLat_ = -90.0;
    double maxLat_ = +90.0;
    double minLon_ = -180.0;
    double maxLon_ = +180.0;

    double lastWeatherLat_ = 0.0;
    double lastWeatherLon_ = 0.0;
	// weather refresh state 
    QTimer* weatherRefreshTimer_ = nullptr;
    bool hasWeatherAnchor_ = false;
    double weatherAnchorLat_ = 0.0;
    double weatherAnchorLon_ = 0.0;
    int weatherRefreshMs_ = 60 * 1000; // 60s

    void emitFilteredJson(const QJsonObject& obj);
};