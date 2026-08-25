#include "OpenWeatherFetcher.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

OpenWeatherFetcher::OpenWeatherFetcher(QObject* parent) : QObject(parent)
{
    connect(&nam_, &QNetworkAccessManager::finished,
        this, &OpenWeatherFetcher::onReplyFinished);
}

void OpenWeatherFetcher::setApiKey(const QString& key)
{
    apiKey_ = key.trimmed();
    qInfo() << "[Weather] apiKey length =" << apiKey_.size();
}

void OpenWeatherFetcher::fetchCurrentWeather(double lat, double lon)
{
    if (apiKey_.isEmpty()) {
        emit fetchError("OpenWeather API key missing (set OPENWEATHER_API_KEY or call setApiKey())");
        return;
    }

    QUrl url("https://api.openweathermap.org/data/2.5/weather");
    QUrlQuery q;
    q.addQueryItem("lat", QString::number(lat, 'f', 6));
    q.addQueryItem("lon", QString::number(lon, 'f', 6));
    q.addQueryItem("appid", apiKey_);
    q.addQueryItem("units", "metric");
    url.setQuery(q);

    nam_.get(QNetworkRequest(url));
}

void OpenWeatherFetcher::onReplyFinished(QNetworkReply* reply)
{
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();

    if (reply->error() != QNetworkReply::NoError) {
        emit fetchError(QString("HTTP %1 ctype=%2 body(head)=%3")
            .arg(http)
            .arg(ctype)
            .arg(QString::fromUtf8(body.left(200))));
        reply->deleteLater();
        return;
    }

    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        emit fetchError(QString("Bad JSON: %1").arg(pe.errorString()));
        reply->deleteLater();
        return;
    }

    emit weatherReady(doc.object());
    reply->deleteLater();
}