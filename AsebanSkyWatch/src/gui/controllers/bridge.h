// bridge.h
#include <QObject>

class Bridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
public slots:
    void mouseMoved(double lat, double lon);
};