// bridge.h
#pragma once
#include <QObject>

class LiveFlightsService;  // fwd declare

class Bridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // inject service from MainWindow
    void setService(LiveFlightsService* s);

public slots:
    void mouseMoved(double lat, double lon);
    // called from JS on map click
    void requestTileAt(double lat, double lon, int z);

signals:
    // C++ → JS (your HTML listens to this)
    void flightsForTile(const QString& statesJson);
    // surface errors to JS / logs
    void error(const QString& message);

private:
    LiveFlightsService* service_ = nullptr; // not owned
};