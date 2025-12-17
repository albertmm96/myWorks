#include "OpenSkyFetcher.h"

#include <QObject>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonParseError>
#include <QProcess>
#include <QFile>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>
#include <QUrlQuery>
#include <QUrl>
#include <QDateTime>
#include <QSqlDriver>
#include <QtSql/QSqlDatabase>
#include <cmath>

OpenSkyFetcher::OpenSkyFetcher(QObject* parent) : QObject(parent) {
    // we don’t connect finished globally anymore
    QDir dir(QCoreApplication::applicationDirPath());
    // going up 3 levels in the directory
    dir.cdUp();
    dir.cdUp();
    dir.cdUp();
	// we go into /config/credentials.json
    dir.cd("config");
    credentialsPath_ = dir.absolutePath() + "/credentials.json";
}

bool OpenSkyFetcher::runPythonFlightFetcher(const QString& icao24, qint64 begin, qint64 end)
{
    QString basePath = QCoreApplication::applicationDirPath();
    QDir dir(basePath);
    // going up 3 levels in the directory
    dir.cdUp();
    dir.cdUp();
    dir.cdUp();
    // now we go into /src/backend/fetch
    dir.cd("src/backend/fetch");

    QString scriptPath = "opensky_oauth_client.py";

    QString pyPath = "C:/Users/Utilisateur/AppData/Local/Programs/Python/Launcher/py.exe";  // actual py.exe

    QStringList args = {
        scriptPath,
        icao24,
        QString::number(begin),
        QString::number(end)
    };

    QProcess process;
    QString workingDirectory = dir.absolutePath();
    process.setWorkingDirectory(workingDirectory);

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

    QString jsonPath = workingDirectory + "/flights.json";
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

    db.transaction();  //  we wrap insertions in a transaction for performance and consistency

    for (const QJsonValue& val : flights) {
        QJsonObject f = val.toObject();

        //  WE CREATE A FRESH QUERY OBJECT INSIDE LOOP!
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

void OpenSkyFetcher::fetchStatesBBox(double minLat, double minLon, double maxLat, double maxLon, std::function<void(const QJsonObject&)> onOk, std::function<void(const QString&)> onErr)
{
    ensureAccessToken(
        // onReady
        [=]() {
            QUrl url("https://opensky-network.org/api/states/all");
            QUrlQuery q;
            q.addQueryItem("lamin", QString::number(minLat, 'f', 6));
            q.addQueryItem("lomin", QString::number(minLon, 'f', 6));
            q.addQueryItem("lamax", QString::number(maxLat, 'f', 6));
            q.addQueryItem("lomax", QString::number(maxLon, 'f', 6));
            url.setQuery(q);

            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::UserAgentHeader, "AsebanSkyWatch/1.0");
            req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
            req.setRawHeader("Authorization", "Bearer " + accessToken_.toUtf8());

            QNetworkReply* reply = manager.get(req);

            QObject::connect(reply, &QNetworkReply::finished, reply, [reply, onOk, onErr]() {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray ctype = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
                const QByteArray bytes = reply->readAll();
                reply->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    onErr(QString("OpenSky error (%1): %2").arg(status).arg(reply->errorString()));
                    return;
                }
                if (status != 200 || !ctype.startsWith("application/json")) {
                    onErr(QString("HTTP %1 ctype=%2 body(head)=%3")
                        .arg(status)
                        .arg(QString::fromLatin1(ctype))
                        .arg(QString::fromLatin1(bytes.left(200))));
                    return;
                }

                const QJsonDocument doc = QJsonDocument::fromJson(bytes);
                if (!doc.isObject()) {
                    onErr(QString("Invalid JSON head=%1").arg(QString::fromLatin1(bytes.left(200))));
                    return;
                }
                const QJsonObject obj = doc.object();
                const QJsonValue statesVal = obj.value("states");

                if (!statesVal.isArray()) {
                    // we force empty array instead of failing: JS won't crash on null
                    QJsonObject safe = obj;
                    safe.insert("states", QJsonArray());
                    onOk(safe);
                    return;
                }

                onOk(obj);
                });
        },
        // onErr
        onErr
    );
}

void OpenSkyFetcher::setBasicAuth(const QString& user, const QString& pass)
{
    basicAuth_ = (user + ":" + pass).toUtf8().toBase64();
}

void OpenSkyFetcher::insertStatesToDb(const QJsonObject& obj)
{
    // helpers local to this function: safe SQL literals without prepared statements, to solve conflict
    auto sqlQuoted = [](QString s) -> QString {
        s.replace('\'', "''");
        return "'" + s + "'";
        };

    auto sqlVal = [&](const QJsonValue& v) -> QString {
        if (v.isUndefined() || v.isNull()) return "NULL";
        if (v.isBool()) return v.toBool() ? "TRUE" : "FALSE";

        if (v.isDouble()) {
            const double d = v.toDouble();
            const double rd = std::round(d);
            // if it is effectively an integer (time_position/last_contact/position_source), emit integer.
            if (std::fabs(d - rd) < 1e-9) return QString::number(static_cast<qint64>(rd));
            return QString::number(d, 'f', 6);
        }

        // strings / everything else
        return sqlQuoted(v.toVariant().toString());
        };

    const QJsonValue statesVal = obj.value("states");
    if (!statesVal.isArray())
        return;

    const QJsonArray states = statesVal.toArray();
    if (states.isEmpty())
        return;

    QSqlDatabase db = QSqlDatabase::database("pg_flights");
    if (!db.isOpen()) {
        qWarning() << "[DB] insertStatesToDb: pg_flights not open";
        return;
    }

    qInfo() << "[DB] flights conn="
        << db.connectionName()
        << "db="
        << db.databaseName();

    if (!db.transaction()) {
        qWarning() << "[DB] transaction start failed:" << db.lastError().text();
        return;
    }

    QSqlQuery q(db);
    const qint64 upsertAt = QDateTime::currentSecsSinceEpoch();

    for (const QJsonValue& rowV : states) {
        if (!rowV.isArray()) continue;
        const QJsonArray a = rowV.toArray();
        if (a.size() < 17) continue;

        const QString icao24 = a.at(0).toString().trimmed();
        if (icao24.isEmpty()) continue;

        const QString callsign = (a.size() > 1) ? a.at(1).toString().trimmed() : QString();

        // Map OpenSky state vector indices to your states_live columns:
        // 0 icao24
        // 1 callsign
        // 2 origin_country
        // 3 time_position
        // 4 last_contact
        // 5 longitude
        // 6 latitude
        // 7 baro_altitude
        // 8 on_ground
        // 9 velocity
        // 10 true_track
        // 11 vertical_rate
        // 13 geo_altitude
        // 14 squawk
        // 15 spi
        // 16 position_source
        // 17 category (optional)

        const QString sql = QString(
            "INSERT INTO states_live ("
            "icao24, callsign, origin_country, time_position, last_contact, "
            "longitude, latitude, baro_altitude, on_ground, velocity, "
            "true_track, vertical_rate, geo_altitude, squawk, spi, "
            "position_source, category, last_upsert_at"
            ") VALUES ("
            "%1, %2, %3, %4, %5, "
            "%6, %7, %8, %9, %10, "
            "%11, %12, %13, %14, %15, "
            "%16, %17, %18"
            ") ON CONFLICT (icao24) DO UPDATE SET "
            "callsign=EXCLUDED.callsign, "
            "origin_country=EXCLUDED.origin_country, "
            "time_position=EXCLUDED.time_position, "
            "last_contact=EXCLUDED.last_contact, "
            "longitude=EXCLUDED.longitude, "
            "latitude=EXCLUDED.latitude, "
            "baro_altitude=EXCLUDED.baro_altitude, "
            "on_ground=EXCLUDED.on_ground, "
            "velocity=EXCLUDED.velocity, "
            "true_track=EXCLUDED.true_track, "
            "vertical_rate=EXCLUDED.vertical_rate, "
            "geo_altitude=EXCLUDED.geo_altitude, "
            "squawk=EXCLUDED.squawk, "
            "spi=EXCLUDED.spi, "
            "position_source=EXCLUDED.position_source, "
            "category=EXCLUDED.category, "
            "last_upsert_at=EXCLUDED.last_upsert_at;"
        )
            .arg(sqlQuoted(icao24))
            .arg(callsign.isEmpty() ? "NULL" : sqlQuoted(callsign))
            .arg(sqlVal(a.at(2)))
            .arg(sqlVal(a.at(3)))
            .arg(sqlVal(a.at(4)))
            .arg(sqlVal(a.at(5)))
            .arg(sqlVal(a.at(6)))
            .arg(sqlVal(a.at(7)))
            .arg(sqlVal(a.at(8)))
            .arg(sqlVal(a.at(9)))
            .arg(sqlVal(a.at(10)))
            .arg(sqlVal(a.at(11)))
            .arg(sqlVal(a.at(13)))
            .arg(sqlVal(a.at(14)))
            .arg(sqlVal(a.at(15)))
            .arg(sqlVal(a.at(16)))
            .arg((a.size() > 17) ? sqlVal(a.at(17)) : QString("NULL"))
            .arg(QString::number(upsertAt));

        if (!q.exec(sql)) {
            qWarning() << "[DB] states_live upsert failed:" << q.lastError().text();
            db.rollback();
            return;
        }
    }

    if (!db.commit()) {
        qWarning() << "[DB] commit failed:" << db.lastError().text();
        db.rollback();
        return;
    }

    QSqlQuery chk(db);
    if (chk.exec("SELECT COUNT(*), MAX(last_upsert_at) FROM states_live;") && chk.next()) {
        qInfo() << "[DB CHECK] states_live rows="
            << chk.value(0).toLongLong()
            << "max(last_upsert_at)="
            << chk.value(1).toLongLong();
    }
    else {
        qWarning() << "[DB CHECK] failed:"
            << chk.lastError().text();
    }
}

bool OpenSkyFetcher::loadCredentials(QString* err)
{
    if (!clientId_.isEmpty() && !clientSecret_.isEmpty()) return true;

    QFile f(credentialsPath_);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QString("Cannot open %1").arg(credentialsPath_);
        return false;
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto obj = doc.object();
    clientId_ = obj.value("clientId").toString();
    clientSecret_ = obj.value("clientSecret").toString();
    if (clientId_.isEmpty() || clientSecret_.isEmpty()) {
        if (err) *err = "Missing clientId or clientSecret in credentials.json";
        return false;
    }
    return true;
}

void OpenSkyFetcher::ensureAccessToken(std::function<void()> onReady, std::function<void(const QString&)> onErr)
{
    QString lerr;
    if (!loadCredentials(&lerr)) { onErr(lerr); return; }

    // if token exists and not expiring in the next 60s, reuse it
    const auto now = QDateTime::currentDateTimeUtc();
    if (!accessToken_.isEmpty() && now.secsTo(tokenExpiry_) > 60) {
        onReady();
        return;
    }
    // else refresh
    requestAccessToken(std::move(onReady), std::move(onErr));
}

void OpenSkyFetcher::requestAccessToken(std::function<void()> onReady, std::function<void(const QString&)> onErr)
{
    QUrl url("https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setHeader(QNetworkRequest::UserAgentHeader, "AsebanSkyWatch/1.0");

    QUrlQuery form;
    form.addQueryItem("grant_type", "client_credentials");
    form.addQueryItem("client_id", clientId_);
    form.addQueryItem("client_secret", clientSecret_);

    QNetworkReply* reply = manager.post(req, form.query(QUrl::FullyEncoded).toUtf8());
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, onReady, onErr]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray ctype = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            onErr(QString("Token network error (%1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        if (status != 200) {
            onErr(QString("Token HTTP %1: %2").arg(status, 0, 10).arg(QString::fromLatin1(body.left(200))));
            return;
        }
        if (!ctype.startsWith("application/json")) {
            onErr(QString("Token content-type unexpected: %1").arg(QString::fromLatin1(ctype)));
            return;
        }

        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) { onErr("Token invalid JSON"); return; }
        const auto obj = doc.object();

        accessToken_ = obj.value("access_token").toString();
        const int expiresIn = obj.value("expires_in").toInt(3600);
        if (accessToken_.isEmpty()) { onErr("Token missing access_token"); return; }

        tokenExpiry_ = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
        onReady();
        });
}

