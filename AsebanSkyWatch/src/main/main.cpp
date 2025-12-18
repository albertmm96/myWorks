#include "main.h"
#include "openSkyFetcher.h"
#include "mainwindow.h"

#include <QApplication>
#include <QProcess>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>

// we check if qt sql queries execute properly
static void runQuery(QSqlQuery& query, const QString& sql) {
    if (!query.exec(sql)) {
        qCritical() << "Query failed:" << query.lastError().text();
        qCritical() << "SQL was:" << sql;
        exit(1);
    }
}

// we open a named PostgreSQL connection, to avoid later conflicts
static QSqlDatabase openPgConnection(const QString& connName)
{
    // if already exists, reuse it
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase existing = QSqlDatabase::database(connName);
        if (!existing.isOpen() && !existing.open()) {
            qCritical() << "[DB] reopen failed for" << connName << ":" << existing.lastError().text();
            exit(1);
        }
        return existing;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL", connName);

	// configs, will move later to config file
    db.setHostName("localhost");
    db.setDatabaseName("Utilisateur");
    db.setUserName("postgres");
    db.setPassword("Jujux236");

    if (!db.open()) {
        qCritical() << "Database connection failed for" << connName << ":" << db.lastError().text();
        exit(1);
    }

    qInfo() << "[DB] opened connection:" << connName;
    return db;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // test if postgresql works on local
    {
        QProcess process;
        process.start("psql", QStringList{ "-U", "postgres", "-d", "Utilisateur", "-f", "schema.sql" });
        process.waitForFinished();
    }

	// we allocate one DB session per schema (flights, weather)
    QSqlDatabase dbFlights = openPgConnection("pg_flights");
    QSqlDatabase dbWeather = openPgConnection("pg_weather");
    Q_UNUSED(dbWeather); // schema init is done once; weather upserts will use pg_weather elsewhere

    // now all schema/bootstrap queries should run on ONE chosen connection
    QSqlQuery query(dbFlights);

    // we drop existing tables if any
    runQuery(query, "DROP TABLE IF EXISTS states_live CASCADE;");
    runQuery(query, "DROP TABLE IF EXISTS weather_live CASCADE;");
    runQuery(query, "DROP TABLE IF EXISTS flights CASCADE;");

	// we keep this for now, may become legacy later or just removed
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
        "time TIMESTAMPTZ DEFAULT NOW());"
    );

    // we turn it into a hypertable
    runQuery(query, "SELECT create_hypertable('flights', 'time', if_not_exists => TRUE);");

    // we create OpenSky fetcher
    OpenSkyFetcher* fetcher = new OpenSkyFetcher;

    // connect signals for error and JSON output
    QObject::connect(fetcher, &OpenSkyFetcher::dataReady, [](const QJsonDocument& json) {
        qDebug() << "Received OpenSky data:" << json;
        });
    QObject::connect(fetcher, &OpenSkyFetcher::fetchError, [](const QString& error) {
        qWarning() << "OpenSky error:" << error;
        });

    // we define a time window for historical data
    QString icao24 = "e49c0d";
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 end = now;
    qint64 begin = end - 21600; // 6 hours before that = 6 x 3600 s
    // call Python script and insert into DB if successful
    if (fetcher->runPythonFlightFetcher(icao24, begin, end)) {
        QDir dir(QCoreApplication::applicationDirPath());
        // going up 3 levels
        dir.cdUp();
        dir.cdUp();
        dir.cdUp();
        // Now go into /src/backend/fetch
        dir.cd("src/backend/fetch");
        QString jsonPath = dir.absolutePath() + "/flights.json";

        // insert using the flights connection (named connection)
        fetcher->parseAndInsertFlights(jsonPath, dbFlights);
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

    // live weather (OpenWeather) stored while the app runs
    runQuery(query,
        "CREATE TABLE IF NOT EXISTS weather_live ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  lat DOUBLE PRECISION NOT NULL,"
        "  lon DOUBLE PRECISION NOT NULL,"
        "  fetched_at BIGINT NOT NULL,"
        "  temp_c DOUBLE PRECISION,"
        "  feels_like_c DOUBLE PRECISION,"
        "  humidity_pct INTEGER,"
        "  pressure_hpa INTEGER,"
        "  wind_speed_ms DOUBLE PRECISION,"
        "  wind_deg INTEGER,"
        "  clouds_pct INTEGER,"
        "  weather_main TEXT,"
        "  weather_desc TEXT,"
        "  city_name TEXT,"
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

    runQuery(query,
        "ALTER TABLE weather_live "
        "ADD COLUMN IF NOT EXISTS temp_c DOUBLE PRECISION, "
        "ADD COLUMN IF NOT EXISTS feels_like_c DOUBLE PRECISION, "
        "ADD COLUMN IF NOT EXISTS humidity_pct INTEGER, "
        "ADD COLUMN IF NOT EXISTS pressure_hpa INTEGER, "
        "ADD COLUMN IF NOT EXISTS wind_speed_ms DOUBLE PRECISION, "
        "ADD COLUMN IF NOT EXISTS wind_deg INTEGER, "
        "ADD COLUMN IF NOT EXISTS clouds_pct INTEGER, "
        "ADD COLUMN IF NOT EXISTS weather_main TEXT, "
        "ADD COLUMN IF NOT EXISTS weather_desc TEXT, "
        "ADD COLUMN IF NOT EXISTS city_name TEXT;"
    );


    // GUI
    MainWindow w;
    w.show();

    const int rc = app.exec();

    //closing the named connections explicitly
    dbFlights.close();
    dbWeather.close();
    QSqlDatabase::removeDatabase("pg_flights");
    QSqlDatabase::removeDatabase("pg_weather");

    return rc;
}
