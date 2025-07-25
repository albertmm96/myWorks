// AsebanSkyWatch.cpp : définit le point d'entrée de l'application.
//

#include "AsebanSkyWatch.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QPushButton>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QPushButton bouton("Hello Qt 6!");
    bouton.resize(200, 60);
    bouton.show();

    return app.exec();
}