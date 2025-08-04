#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>

class OpenSkyFetcher : public QObject {
    Q_OBJECT

public:
    explicit OpenSkyFetcher(QObject* parent = nullptr);
    void fetchLiveData();

signals:
    void dataReady(const QJsonDocument&);
    void fetchError(const QString&);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager manager;
};