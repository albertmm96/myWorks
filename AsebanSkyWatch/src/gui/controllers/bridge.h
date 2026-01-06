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
    Q_INVOKABLE QString currentFlightsJson() const
    {
        return lastJsonFull_;
    }
    Q_INVOKABLE void setGeoFilter(double minLat, double maxLat, double minLon, double maxLon);

    void setWeatherService(LiveWeatherService* s);
    Q_INVOKABLE void requestWeatherAt(double lat, double lon);

    bool flightsEnabled_ = true;
    bool weatherEnabled_ = true;

public slots:
    void mouseMoved(double lat, double lon);
    void requestTileAt(double lat, double lon, int z);
    void selectFlight(const QString& icao24);
    void setFlightsEnabled(bool enabled);
    void setWeatherEnabled(bool enabled);

private slots:
    void onMasterTick();

signals:
    void flightsForTile(const QString& statesJson);
    void weatherForTile(const QString& weatherJson);
    void weatherSamplesForTile(const QString& samplesJson);
    void error(const QString& message);
    void trackLineReady(const QString& json);
    void trackCleared();
    void clearFlights();
    void clearWeather();

private:
    LiveFlightsService* service_ = nullptr;
    LiveWeatherService* weatherService_ = nullptr;

    QString lastJsonFull_;

    bool   filterEnabled_ = false;
    double minLat_ = -90.0;
    double maxLat_ = +90.0;
    double minLon_ = -180.0;
    double maxLon_ = +180.0;

    double lastWeatherLat_ = 0.0;
    double lastWeatherLon_ = 0.0;

	// single, unique global clock to manage weather/flights cadence
    QTimer* masterTimer_ = nullptr;
    int masterTickMs_ = 3000; // match flights cadence initially
};
