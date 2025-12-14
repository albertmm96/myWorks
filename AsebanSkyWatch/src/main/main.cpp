// Description: Main application entry point for the flight tracking application.

#include "main.h"
#include "openSkyFetcher.h"
#include "mainwindow.h"

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

	// Drop existing tables if any
    runQuery(query, "DROP TABLE IF EXISTS states_live CASCADE;");
    runQuery(query, "DROP TABLE IF EXISTS weather_live CASCADE;");
    runQuery(query, "DROP TABLE IF EXISTS flights CASCADE;");

    // Create the flights table
    runQuery(query,
        "CREATE TABLE IF NOT EXISTS flights ("
        "icao24 TEXT, "
        "callsign TEXT, "
        "estDepartureAirport TEXT, "
        "estArrivalAirport TEXT, "
        "firstSeen INTEGER, "
        "lastSeen INTEGER, "
        "estDepartureAirportHorizDistance INTEGER, "
        "estArrivalAirportHorizDistance INTEGER, "
        "estDepartureAirportVertDistance INTEGER, "
        "estArrivalAirportVertDistance INTEGER, "
        "departureAirportCandidatesCount INTEGER, "
        "arrivalAirportCandidatesCount INTEGER, "
        "time TIMESTAMPTZ DEFAULT NOW());");

    // Turn it into a hypertable
    // Create a flights hypertable
    runQuery(query,"SELECT create_hypertable('flights', 'time', if_not_exists => TRUE);");

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
    
    // live states (OpenSky /api/states/all) stored while the app runs
    runQuery(query,
        "CREATE TABLE IF NOT EXISTS states_live ("
        "  icao24 TEXT PRIMARY KEY,"
        "  callsign TEXT,"
        "  origin_country TEXT,"
        "  time_position BIGINT,"
        "  last_contact BIGINT,"
        "  longitude DOUBLE PRECISION,"
        "  latitude DOUBLE PRECISION,"
        "  baro_altitude DOUBLE PRECISION,"
        "  on_ground BOOLEAN,"
        "  velocity DOUBLE PRECISION,"
        "  true_track DOUBLE PRECISION,"
        "  vertical_rate DOUBLE PRECISION,"
        "  geo_altitude DOUBLE PRECISION,"
        "  squawk TEXT,"
        "  spi BOOLEAN,"
        "  position_source INTEGER,"
        "  category INTEGER,"
        "  last_upsert_at BIGINT"
        ");"
    );
	// ensure last_upsert_at exists
    runQuery(query,
        "ALTER TABLE states_live "
        "ADD COLUMN IF NOT EXISTS last_upsert_at BIGINT;"
    );

    //   live weather (OpenWeather) stored while the app runs
    runQuery(query,
        "CREATE TABLE IF NOT EXISTS weather_live ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  lat DOUBLE PRECISION NOT NULL,"
        "  lon DOUBLE PRECISION NOT NULL,"
        "  fetched_at BIGINT NOT NULL,"                 // unix seconds
        "  payload JSONB NOT NULL,"                     // full OpenWeather JSON
        "  time TIMESTAMPTZ DEFAULT NOW(),"
        "  UNIQUE(lat, lon)"
        ");"
    );
    
	// create index on last_contact for faster lookups of most recent states
    runQuery(query,
        "CREATE INDEX IF NOT EXISTS states_live_last_contact_idx "
        "ON states_live (last_contact DESC);"
    );

    // index for quick latest lookups
    runQuery(query,
        "CREATE INDEX IF NOT EXISTS weather_live_fetched_at_idx "
        "ON weather_live (fetched_at DESC);"
    );

    //   GUI test  
    MainWindow w;
    w.show();

    int rc = app.exec();   //   don't return yet
    db.close();
}