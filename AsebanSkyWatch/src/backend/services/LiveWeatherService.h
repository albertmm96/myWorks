#pragma once
#include <QObject>
#include <QJsonObject>

class OpenWeatherFetcher;

class LiveWeatherService : public QObject {
    Q_OBJECT
public:
    explicit LiveWeatherService(OpenWeatherFetcher* fetcher, QObject* parent = nullptr);

    void requestWeather(double lat, double lon);

signals:
    void weatherReady(const QJsonObject& obj);
    void serviceError(const QString& msg);

private:
    OpenWeatherFetcher* fetcher_ = nullptr; // not owned
};