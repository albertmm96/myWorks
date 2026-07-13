#include "main.h"
#include "openSkyFetcher.h"
#include "mainwindow.h"

#include <QApplication>
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

// creates app database with default pass
static void ensureDatabaseExists()
{
    const QString targetDatabase = "Utilisateur";
    const QString adminConnectionName = "pg_admin";

    {
        QSqlDatabase adminDb =
            QSqlDatabase::addDatabase("QPSQL", adminConnectionName);

        adminDb.setHostName("localhost");
        adminDb.setDatabaseName("postgres");
        adminDb.setUserName("postgres");
        adminDb.setPassword("Jujux238");

        if (!adminDb.open()) {
            qCritical() << "[DB] Could not connect to PostgreSQL:"
                << adminDb.lastError().text();
            exit(1);
        }

        QSqlQuery query(adminDb);
        query.prepare(
            "SELECT 1 FROM pg_database WHERE datname = :databaseName"
        );
        query.bindValue(":databaseName", targetDatabase);

        if (!query.exec()) {
            qCritical() << "[DB] Database check failed:"
                << query.lastError().text();
            exit(1);
        }

        if (!query.next()) {
            QString escapedName = targetDatabase;
            escapedName.replace('"', "\"\"");

            if (!query.exec(
                QString("CREATE DATABASE \"%1\"").arg(escapedName))) {
                qCritical() << "[DB] Database creation failed:"
                    << query.lastError().text();
                exit(1);
            }

            qInfo() << "[DB] Created database:" << targetDatabase;
        }

        adminDb.close();
    }

    QSqlDatabase::removeDatabase(adminConnectionName);
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
    db.setPassword("Jujux238");

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

    ensureDatabaseExists();

    // we allocate one DB session per schema (flights, weather)
    QSqlDatabase dbFlights = openPgConnection("pg_flights");
    QSqlDatabase dbWeather = openPgConnection("pg_weather");
    Q_UNUSED(dbWeather); // schema init is done once; weather upserts will use pg_weather elsewhere

    // now all schema/bootstrap queries should run on ONE chosen connection
    QSqlQuery query(dbFlights);

    // we drop existing tables if any
    runQuery(query, "DROP TABLE IF EXISTS states_live CASCADE;");
    runQuery(query, "DROP TABLE IF EXISTS weather_live CASCADE;");

    // we create OpenSky fetcher
    OpenSkyFetcher* fetcher = new OpenSkyFetcher;

    // connect signals for error and JSON output
    QObject::connect(fetcher, &OpenSkyFetcher::dataReady, [](const QJsonDocument& json) {
        qDebug() << "Received OpenSky data:" << json;
        });
    QObject::connect(fetcher, &OpenSkyFetcher::fetchError, [](const QString& error) {
        qWarning() << "OpenSky error:" << error;
        });

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

    runQuery(query,
        "CREATE TABLE IF NOT EXISTS flight_track_live ("
        "  ts TIMESTAMPTZ NOT NULL,"
        "  icao24 TEXT NOT NULL,"
        "  longitude DOUBLE PRECISION,"
        "  latitude DOUBLE PRECISION,"
        "  source TEXT,"
        "  PRIMARY KEY (icao24, ts)"
        ");"
    );

    runQuery(query,
        "CREATE EXTENSION IF NOT EXISTS timescaledb;"
    );

    runQuery(query,
        "SELECT create_hypertable("
        "'flight_track_live', "
        "by_range('ts'), "
        "if_not_exists => TRUE"
        ");"
    );

    runQuery(query,
        "SELECT create_hypertable('flight_track_live', 'ts', if_not_exists => TRUE);"
    );

    runQuery(query,
        "CREATE INDEX IF NOT EXISTS flight_track_live_icao_ts_idx "
        "ON flight_track_live (icao24, ts DESC);"
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
