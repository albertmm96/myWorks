#pragma once
#include <QObject>
#include <QJsonObject>
#include <QDateTime>

class OpenWeatherFetcher;

class LiveWeatherService : public QObject {
    Q_OBJECT
public:
    explicit LiveWeatherService(OpenWeatherFetcher* fetcher, QObject* parent = nullptr);

    void requestWeather(double lat, double lon);
    void onTick(); // called by Bridge master timer

signals:
    void weatherReady(const QJsonObject& obj);
    void serviceError(const QString& msg);

private:
    OpenWeatherFetcher* fetcher_ = nullptr;

    // anchor + cache
    bool hasAnchor_ = false;
    double anchorLat_ = 0.0;
    double anchorLon_ = 0.0;

    QJsonObject cache_;
    QDateTime cacheTs_;
    int ttlSeconds_ = 60; // we keep our previous 60s intent
};