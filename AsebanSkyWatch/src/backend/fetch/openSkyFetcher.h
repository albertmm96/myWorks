#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QSqlQuery>

class OpenSkyFetcher : public QObject {
    Q_OBJECT

public:
    explicit OpenSkyFetcher(QObject* parent = nullptr);
    bool runPythonFlightFetcher(const QString& icao24, qint64 begin, qint64 end);
    void parseAndInsertFlights(const QString& filePath, QSqlDatabase& db);

signals:
    void dataReady(const QJsonDocument&);
    void fetchError(const QString&);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager manager;
};