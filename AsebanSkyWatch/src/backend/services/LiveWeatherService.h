#pragma once
#include <QObject>
#include <QJsonObject>
#include <QDateTime>
#include <QVector>
#include <QPair>
#include <QJsonArray>

class OpenWeatherFetcher;

class LiveWeatherService : public QObject {
    Q_OBJECT
public:
    explicit LiveWeatherService(OpenWeatherFetcher* fetcher, QObject* parent = nullptr);

    void requestWeather(double lat, double lon);
    void onTick(); // called by Bridge master timer
    void requestWeatherSamples(const QVector<QPair<double, double>>& points);

signals:
    void weatherReady(const QJsonObject& obj);
    void serviceError(const QString& msg);
    void weatherSamplesReady(const QJsonArray& samples);

private:
    OpenWeatherFetcher* fetcher_ = nullptr;

    // anchor + cache
    bool hasAnchor_ = false;
    double anchorLat_ = 0.0;
    double anchorLon_ = 0.0;

    QJsonObject cache_;
    QDateTime cacheTs_;
    int ttlSeconds_ = 60; // we keep our previous 60s intent

    // Sampling queue state (tile sampling)
    bool samplingActive_ = false;
    QVector<QPair<double, double>> pendingSamples_;
    QJsonArray completedSamples_;
    double currentSampleLat_ = 0.0;
    double currentSampleLon_ = 0.0;

    void fetchNextSample_();
    void upsertWeatherObject_(const QJsonObject& obj, double lat, double lon);
};