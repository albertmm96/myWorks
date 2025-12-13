#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class OpenWeatherFetcher : public QObject {
    Q_OBJECT
public:
    explicit OpenWeatherFetcher(QObject* parent = nullptr);

    void setApiKey(const QString& key);
    void fetchCurrentWeather(double lat, double lon);

signals:
    void weatherReady(const QJsonObject& obj);
    void fetchError(const QString& msg);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager nam_;
    QString apiKey_;
};