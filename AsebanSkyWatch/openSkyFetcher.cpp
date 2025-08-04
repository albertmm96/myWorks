#include <QObject>
#include "OpenSkyFetcher.h"
#include <QNetworkRequest>
#include <QJsonParseError>

OpenSkyFetcher::OpenSkyFetcher(QObject* parent) : QObject(parent) {
    connect(&manager, &QNetworkAccessManager::finished,
        this, &OpenSkyFetcher::onReplyFinished);
}

void OpenSkyFetcher::fetchLiveData() {
    QUrl url("https://opensky-network.org/api/states/all");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    manager.get(request);
}

void OpenSkyFetcher::onReplyFinished(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit fetchError(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray response = reply->readAll();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError) {
        emit fetchError("Parse error: " + error.errorString());
    }
    else {
        emit dataReady(doc);
    }
    reply->deleteLater();
}