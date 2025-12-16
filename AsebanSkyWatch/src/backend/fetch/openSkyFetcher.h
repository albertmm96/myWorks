#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QDateTime>
#include <QJsonObject>
#include <functional>

class OpenSkyFetcher : public QObject {
    Q_OBJECT

public:
    explicit OpenSkyFetcher(QObject* parent = nullptr);
    bool runPythonFlightFetcher(const QString& icao24, qint64 begin, qint64 end);
    void parseAndInsertFlights(const QString& filePath, QSqlDatabase& db);
    void setCredentialsPath(const QString& path) { credentialsPath_ = path; }
    void fetchStatesBBox(double minLat, double minLon,
        double maxLat, double maxLon,
        std::function<void(const QJsonObject&)> onOk,
        std::function<void(const QString&)> onErr);
    // public setter to configure credentials
    void setBasicAuth(const QString& user, const QString& pass);
	// inserts states from obj["states"] into the given db (or default connection if none given)
    void insertStatesToDb(const QJsonObject& obj);

signals:
    void dataReady(const QJsonDocument&);
    void fetchError(const QString&);

private slots:

private:
    // loads clientId/clientSecret from credentials.json (once)
    bool loadCredentials(QString* err = nullptr);

    // ensures we have a fresh access token; calls onReady() when available.
    void ensureAccessToken(std::function<void()> onReady,
        std::function<void(const QString&)> onErr);

    // actually POSTs to /oauth2/token and updates accessToken_/tokenExpiry_
    void requestAccessToken(std::function<void()> onReady,
        std::function<void(const QString&)> onErr);

    QNetworkAccessManager manager;
    QByteArray basicAuth_;
    // OAuth state
    QString clientId_;
    QString clientSecret_;
    QString accessToken_;
    QDateTime tokenExpiry_;       // UTC
    QString credentialsPath_;     // defaults to appdir/credentials.json
};