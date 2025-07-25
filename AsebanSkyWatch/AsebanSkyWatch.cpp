// AsebanSkyWatch.cpp : définit le point d'entrée de l'application.
//

#include "AsebanSkyWatch.h"
#include <QApplication>
#include <QPushButton>
#include <QProcess>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // test if qt display works
    QPushButton bouton("Hello Qt 6!");
    bouton.resize(200, 60);
    bouton.show();

    // test if postgresql works
    QProcess process;
    process.start("psql", QStringList{ "-U", "postgres", "-d", "yourdb", "-f", "schema.sql" });
    process.waitForFinished();

    // test if timescaledb works
    // TBD

    return app.exec();
}