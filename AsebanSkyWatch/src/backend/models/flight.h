#pragma once
#include <QString>
#include <QJsonArray>
#include <QVector>
#include <QtGlobal>

struct Flight {
    QString icao24;
    QString callsign;
    QString originCountry;
    double lon = qQNaN();
    double lat = qQNaN();
    double trueTrack = qQNaN();
    double velocity = qQNaN();
    double baroAltitude = qQNaN();
    double geoAltitude = qQNaN();
    qint64 timePosition = 0;
    qint64 lastContact = 0;
};

// utility: parse a QJsonArray from OpenSky into Flight
inline Flight fromStateArray(const QJsonArray& a) {
    Flight f;
    f.icao24 = a.at(0).toString();
    f.callsign = a.at(1).toString().trimmed();
    f.originCountry = a.at(2).toString();
    f.timePosition = a.at(3).toVariant().toLongLong();
    f.lastContact = a.at(4).toVariant().toLongLong();
    f.lon = a.at(5).toDouble();
    f.lat = a.at(6).toDouble();
    f.baroAltitude = a.at(7).toDouble();
    f.velocity = a.at(9).toDouble();
    f.trueTrack = a.at(10).toDouble();
    f.geoAltitude = a.at(13).toDouble();
    return f;
}

// utility: parse multiple
inline QVector<Flight> flightsFromStates(const QJsonArray& states) {
    QVector<Flight> out;
    out.reserve(states.size());
    for (const auto& v : states) {
        if (!v.isArray()) continue;
        out.push_back(fromStateArray(v.toArray()));
    }
    return out;
}