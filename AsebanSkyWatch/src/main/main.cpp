// AsebanSkyWatch.cpp : définit le point d'entrée de l'application.
//

#include "main.h"
#include "openSkyFetcher.h"

#include <QApplication>
#include <QPushButton>
#include <QProcess>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDateTime>
#include <QDir>


// check if qt sql queries execute properly
void runQuery(QSqlQuery& query, const QString& sql) {
    if (!query.exec(sql)) {
        qCritical() << "Query failed:" << query.lastError().text();
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // test if qt display works
    QPushButton bouton("Hello Qt 6!");
    bouton.resize(200, 60);
    bouton.show();

    // test if postgresql works
    QProcess process;
    process.start("psql", QStringList{ "-U", "postgres", "-d", "Utilisateur", "-f", "schema.sql" });
    process.waitForFinished();

    // TESTING IF TIMESCALEDB EXTENSION WORKS
    QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL");
    db.setHostName("localhost");
    db.setDatabaseName("Utilisateur");
    db.setUserName("postgres");
    db.setPassword("Jujux236");       
    // check if database connects
    if (!db.open()) {
        qCritical() << "Database connection failed:" << db.lastError().text();
        return 1;
    }
    
    QSqlQuery query;

    // Create a flights hypertable
    runQuery(query,
        "SELECT create_hypertable('flights', 'time', if_not_exists => TRUE);");

    // Create OpenSky fetcher
    OpenSkyFetcher* fetcher = new OpenSkyFetcher;

    // Optional: connect signals for error and JSON output
    QObject::connect(fetcher, &OpenSkyFetcher::dataReady, [](const QJsonDocument& json) {
        qDebug() << "Received OpenSky data:" << json;
        });
    QObject::connect(fetcher, &OpenSkyFetcher::fetchError, [](const QString& error) {
        qWarning() << "OpenSky error:" << error;
        });

    // Define a time window for historical data
    QString icao24 = "e49c0d";
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 end = now;          
    qint64 begin = end - 21600;       // 6 hours before that = 6 x 3600 s

    // Call Python script and insert into DB if successful
    if (fetcher->runPythonFlightFetcher(icao24, begin, end)) {
        QDir dir(QCoreApplication::applicationDirPath());
        // Go up 3 levels
        dir.cdUp();
        dir.cdUp();
        dir.cdUp();
        // Now go into /src/backend/fetch
        dir.cd("src/backend/fetch");
        QString jsonPath = dir.absolutePath() + "/flights.json";
        fetcher->parseAndInsertFlights(jsonPath, db);
    }
    
    return app.exec();
}