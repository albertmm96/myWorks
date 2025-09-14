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


OpenSkyFetcher::OpenSkyFetcher(QObject* parent) : QObject(parent) {
    // don’t connect finished globally anymore
    QDir dir(QCoreApplication::applicationDirPath());
    // Go up 3 levels
    dir.cdUp();
    dir.cdUp();
    dir.cdUp();
	// Now go into /config/credentials.json
    dir.cd("config");
    credentialsPath_ = dir.absolutePath() + "/credentials.json";
}

bool OpenSkyFetcher::runPythonFlightFetcher(const QString& icao24, qint64 begin, qint64 end)
{
    QString basePath = QCoreApplication::applicationDirPath();
    QDir dir(basePath);
    // Go up 3 levels
    dir.cdUp();
    dir.cdUp();
    dir.cdUp();
    // Now go into /src/backend/fetch
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
            req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
            req.setRawHeader("Authorization", "Bearer " + accessToken_.toUtf8());

            QNetworkReply* reply = manager.get(req);

            // IMPORTANT: close the lambda and the connect with "});"
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
                    // Force empty array instead of failing → JS won't crash on null
                    QJsonObject safe = obj;
                    safe.insert("states", QJsonArray());
                    onOk(safe);
                    return;
                }

                onOk(obj);
                }); // <-- this was missing
        },
        // onErr
        onErr
    );
}

void OpenSkyFetcher::setBasicAuth(const QString& user, const QString& pass)
{
    basicAuth_ = (user + ":" + pass).toUtf8().toBase64();
}

void OpenSkyFetcher::insertStatesToDb(const QJsonObject& obj, QSqlDatabase db)
{
    if (!db.isValid()) db = QSqlDatabase::database(); // default connection
    if (!db.isOpen()) { qWarning() << "[DB] not open"; return; }

    const QJsonArray states = obj.value("states").toArray();
    if (states.isEmpty()) { qInfo() << "[DB] states empty; nothing to insert"; return; }

    QSqlQuery q(db);
    if (!db.transaction()) qWarning() << "[DB] begin tx failed:" << db.lastError().text();

    q.prepare(R"SQL(
        INSERT INTO states_live (
            icao24, callsign, origin_country, time_position, last_contact,
            longitude, latitude, baro_altitude, on_ground, velocity,
            true_track, vertical_rate, sensors, geo_altitude, squawk,
            spi, position_source, category
        ) VALUES (
            :icao24, :callsign, :origin_country, :time_position, :last_contact,
            :lon, :lat, :baro_alt, :on_ground, :vel,
            :track, :v_rate, :sensors::jsonb, :geo_alt, :squawk,
            :spi, :pos_src, :category
        )
        ON CONFLICT (icao24) DO UPDATE SET
            callsign        = EXCLUDED.callsign,
            origin_country  = EXCLUDED.origin_country,
            time_position   = EXCLUDED.time_position,
            last_contact    = EXCLUDED.last_contact,
            longitude       = EXCLUDED.longitude,
            latitude        = EXCLUDED.latitude,
            baro_altitude   = EXCLUDED.baro_altitude,
            on_ground       = EXCLUDED.on_ground,
            velocity        = EXCLUDED.velocity,
            true_track      = EXCLUDED.true_track,
            vertical_rate   = EXCLUDED.vertical_rate,
            sensors         = EXCLUDED.sensors,
            geo_altitude    = EXCLUDED.geo_altitude,
            squawk          = EXCLUDED.squawk,
            spi             = EXCLUDED.spi,
            position_source = EXCLUDED.position_source,
            category        = EXCLUDED.category
    )SQL");

    for (const QJsonValue& v : states) {
        if (!v.isArray()) continue;
        const QJsonArray a = v.toArray();

        auto bindMaybe = [&](const char* name, int idx) {
            const QJsonValue vv = (idx < a.size() ? a.at(idx) : QJsonValue());
            if (vv.isNull() || vv.isUndefined()) q.bindValue(name, QVariant());
            else q.bindValue(name, vv.toVariant());
            };

        bindMaybe(":icao24", 0);
        // callsign trim (can be empty)
        q.bindValue(":callsign", a.size() > 1 ? a.at(1).toString().trimmed() : QString());
        bindMaybe(":origin_country", 2);
        bindMaybe(":time_position", 3);
        bindMaybe(":last_contact", 4);
        bindMaybe(":lon", 5);
        bindMaybe(":lat", 6);
        bindMaybe(":baro_alt", 7);
        bindMaybe(":on_ground", 8);
        bindMaybe(":vel", 9);
        bindMaybe(":track", 10);
        bindMaybe(":v_rate", 11);

        // sensors -> JSON compact (string), casted in ::jsonb on SQL side
        if (a.size() > 12 && a.at(12).isArray()) {
            const auto json = QJsonDocument(a.at(12).toArray()).toJson(QJsonDocument::Compact);
            q.bindValue(":sensors", QString::fromUtf8(json));
        }
        else {
            q.bindValue(":sensors", QVariant());
        }

        bindMaybe(":geo_alt", 13);
        bindMaybe(":squawk", 14);
        bindMaybe(":spi", 15);
        bindMaybe(":pos_src", 16);
        bindMaybe(":category", 17);

        if (!q.exec()) {
            qWarning() << "[DB] upsert failed:" << q.lastError().text();
        }
    }

    if (!db.commit()) qWarning() << "[DB] commit failed:" << db.lastError().text();
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

