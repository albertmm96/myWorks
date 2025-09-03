// bridge.h
#pragma once
#include <QObject>

class LiveFlightsService;  // fwd declare

class Bridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void setService(LiveFlightsService* s);

    // returns the last JSON payload rendered on the map
    Q_INVOKABLE QString currentFlightsJson() const { return lastJson_; }

public slots:
    void mouseMoved(double lat, double lon);
    void requestTileAt(double lat, double lon, int z);

signals:
    void flightsForTile(const QString& statesJson);
    void error(const QString& message);

private:
    LiveFlightsService* service_ = nullptr; // not owned

    // cache what we last emitted to JS (the exact flights currently displayed)
    QString lastJson_;
};