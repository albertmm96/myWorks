// AsebanSkyWatch.cpp : définit le point d'entrée de l'application.
//

#include "AsebanSkyWatch.h"
#include <QApplication>
#include <QPushButton>
#include <QProcess>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDateTime>

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
    // Create a Timescale hypertable
    runQuery(query,
        "CREATE TABLE IF NOT EXISTS sensor_data ("
        "time TIMESTAMPTZ NOT NULL, "
        "sensor_id INT, "
        "temperature DOUBLE PRECISION);");
    // Turn it into a hypertable (idempotent)
    runQuery(query,
        "SELECT create_hypertable('sensor_data', 'time', if_not_exists => TRUE);");
    // Insert sample data
    runQuery(query,
        "INSERT INTO sensor_data (time, sensor_id, temperature) VALUES "
        "(NOW(), 42, 23.1);");
    // Read it back
    runQuery(query, "SELECT * FROM sensor_data ORDER BY time DESC;");
    while (query.next()) {
        qDebug() << "Time:" << query.value("time").toDateTime()
            << "Sensor ID:" << query.value("sensor_id").toInt()
            << "Temp:" << query.value("temperature").toDouble();
    }
    
    return app.exec();
}