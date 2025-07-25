// AsebanSkyWatch.cpp : définit le point d'entrée de l'application.
//

#include "AsebanSkyWatch.h"
#include <QApplication>
#include <QPushButton>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QPushButton button("Hello from Qt!");
    button.show();
    return app.exec();
}