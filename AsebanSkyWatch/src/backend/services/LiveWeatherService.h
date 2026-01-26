#pragma once
#include <QObject>
#include <QJsonObject>
#include <QDateTime>
#include <QVector>
#include <QPair>
#include <QJsonArray>
#include <QHash>

class OpenWeatherFetcher;

class LiveWeatherService : public QObject {
    Q_OBJECT
public:
    explicit LiveWeatherService(OpenWeatherFetcher* fetcher, QObject* parent = nullptr);

    void requestWeather(double lat, double lon);
    void onTick(); // called by Bridge master timer
    void requestWeatherSamples(const QString& tileKey,
        const QVector<QPair<double, double>>& points);
    void clearCache();
    void setValueFilter(double minTempC, double maxTempC, int minPressure, int maxPressure);
    void applyValueFilterNow();

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

    QString currentTileKey_;

    void fetchNextSample_();
    void upsertWeatherObject_(const QJsonObject& obj, double lat, double lon);
    // active tile sampling registry, so we can refresh periodically
    QHash<QString, QVector<QPair<double, double>>> activeTileSamples_;
    QHash<QString, QDateTime> tileLastFetchUtc_;
    // refresh control
    int tileRefreshSeconds_ = 60;
    // when refreshing, we cycle tiles so we don't spam API
    QStringList refreshTileKeys_;
    int refreshTileIndex_ = 0;

    void refreshOneTileIfStale_();
    bool tileIsStale_(const QString& tileKey) const;
    bool valueFilterEnabled_ = false;
    double minTempC_ = -1e9;
    double maxTempC_ = +1e9;
    int minPressure_ = -1;
    int maxPressure_ = 100000;
    void emitFilteredSamplesForAllTiles_();
    QJsonArray buildFilteredTileArray_(const QString& tileKey);
};