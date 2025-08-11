#include <QObject>
#include "OpenSkyFetcher.h"
#include <QNetworkRequest>
#include <QJsonParseError>
#include <QProcess>
#include <QFile>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>


OpenSkyFetcher::OpenSkyFetcher(QObject* parent) : QObject(parent) {
    connect(&manager, &QNetworkAccessManager::finished,
        this, &OpenSkyFetcher::onReplyFinished);
}

bool OpenSkyFetcher::runPythonFlightFetcher(const QString& icao24, qint64 begin, qint64 end)
{
    QString basePath = QCoreApplication::applicationDirPath();
    QString scriptPath = basePath + "/../../../src/backend/fetch/opensky_oauth_client.py";
    QString jsonPath = basePath + "/../../../src/backend/fetch/flights.json";

    QString pyPath = "C:/Users/Utilisateur/AppData/Local/Programs/Python/Launcher/py.exe";  // actual py.exe

    QStringList args = {
        scriptPath,
        icao24,
        QString::number(begin),
        QString::number(end)
    };

    QProcess process;
    process.setWorkingDirectory(basePath + "/../../../");

    qDebug() << "Running command:" << pyPath << args;

    process.start(pyPath, args);  // No cmd.exe, no manual quoting

    if (!process.waitForStarted()) {
        qWarning() << "Failed to start Python process!";
        qWarning() << "Error string:" << process.errorString();
        return false;
    }

    if (!process.waitForFinished()) {
        qWarning() << "Python script did not finish.";
        return false;
    }

    QByteArray output = process.readAllStandardOutput();
    QByteArray errors = process.readAllStandardError();

    if (!output.isEmpty()) qDebug() << "Python output:" << output;
    if (!errors.isEmpty()) qWarning() << "Python errors:" << errors;

    if (!QFile::exists(jsonPath)) {
        qWarning() << "flights.json not found at:" << jsonPath;
        return false;
    }

    return true;
}


void OpenSkyFetcher::parseAndInsertFlights(const QString& filePath, QSqlDatabase& db)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit fetchError("Failed to open " + filePath);
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        emit fetchError("JSON parse error: " + parseError.errorString());
        return;
    }

    if (!doc.isArray()) {
        emit fetchError("flights.json does not contain a JSON array.");
        return;
    }

    QJsonArray flights = doc.array();

    db.transaction();  //  wrap insertions in a transaction for performance and consistency

    for (const QJsonValue& val : flights) {
        QJsonObject f = val.toObject();

        //  CREATE A FRESH QUERY OBJECT INSIDE LOOP!
        QSqlQuery query(db);

        query.prepare(R"(
            INSERT INTO flights (
                icao24, callsign, estDepartureAirport, estArrivalAirport,
                firstSeen, lastSeen,
                estDepartureAirportHorizDistance, estArrivalAirportHorizDistance,
                estDepartureAirportVertDistance, estArrivalAirportVertDistance,
                departureAirportCandidatesCount, arrivalAirportCandidatesCount
            ) VALUES (
                :icao24, :callsign, :estDepartureAirport, :estArrivalAirport,
                :firstSeen, :lastSeen,
                :estDepartureAirportHorizDistance, :estArrivalAirportHorizDistance,
                :estDepartureAirportVertDistance, :estArrivalAirportVertDistance,
                :departureAirportCandidatesCount, :arrivalAirportCandidatesCount
            )
        )");

        query.bindValue(":icao24", f["icao24"].toString());
        query.bindValue(":callsign", f["callsign"].toString());
        query.bindValue(":estDepartureAirport", f["estDepartureAirport"].toString());
        query.bindValue(":estArrivalAirport", f["estArrivalAirport"].toString());
        query.bindValue(":firstSeen", f["firstSeen"].toInt());
        query.bindValue(":lastSeen", f["lastSeen"].toInt());
        query.bindValue(":estDepartureAirportHorizDistance", f["estDepartureAirportHorizDistance"].toInt());
        query.bindValue(":estArrivalAirportHorizDistance", f["estArrivalAirportHorizDistance"].toInt());
        query.bindValue(":estDepartureAirportVertDistance", f["estDepartureAirportVertDistance"].toInt());
        query.bindValue(":estArrivalAirportVertDistance", f["estArrivalAirportVertDistance"].toInt());
        query.bindValue(":departureAirportCandidatesCount", f["departureAirportCandidatesCount"].toInt());
        query.bindValue(":arrivalAirportCandidatesCount", f["arrivalAirportCandidatesCount"].toInt());

        if (!query.exec()) {
            qWarning() << "Insert failed:" << query.lastError().text();
            qWarning() << "With values: "
                << f["icao24"].toString()
                << f["callsign"].toString()
                << f["firstSeen"].toInt()
                << f["lastSeen"].toInt();
        }
    }

    db.commit();  //  commit the batch
    emit dataReady(doc);
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

