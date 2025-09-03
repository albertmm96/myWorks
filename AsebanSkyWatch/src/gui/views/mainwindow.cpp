#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "bridge.h"
#include "openSkyFetcher.h"
#include "LiveFlightsService.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace {

    // dev test: pretty-print one OpenSky "state vector" (array-of-fields) on one line.
    QString stateLine(const QJsonArray& a) {
        static const QStringList names = {
            "icao24",                          // hex ICAO address
            "callsign",                        // flight ID
            "origin_country",                  // country
            "time_position (s)",               // Unix time
            "last_contact (s)",                // Unix time
            "longitude (deg)",
            "latitude (deg)",
            "baro_altitude (m)",               // barometric altitude (meters)
            "on_ground (bool)",
            "velocity (m/s)",                  // ground speed
            "true_track (deg)",                // heading
            "vertical_rate (m/s)",
            "sensors (list)",
            "geo_altitude (m)",                // geometric altitude
            "squawk",
            "spi (bool)",
            "position_source (enum)",          // 0=ADS-B, 1=ASTERIX, 2=MLAT
            "category"                         // aircraft category
        };

        QStringList kv;
        const int n = std::min(a.size(), names.size());

        for (int i = 0; i < n; ++i) {
            const QJsonValue v = a.at(i);
            QString val;

            if (v.isNull()) {
                val = "null";
            }
            else if (v.isDouble()) {
                const int prec = (i == 5 || i == 6) ? 6 : 3; // lon/lat finer
                val = QString::number(v.toDouble(), 'f', prec);
            }
            else if (v.isBool()) {
                val = v.toBool() ? "true" : "false";
            }
            else if (v.isString()) {
                val = v.toString().trimmed();
            }
            else if (v.isArray()) {
                val = QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
            }
            else if (v.isObject()) {
                val = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
            }
            else {
                val = v.toVariant().toString();
            }

            kv << (names[i] + "=" + val);
        }

        // print any extra trailing fields (if OpenSky adds more)
        for (int i = names.size(); i < a.size(); ++i) {
            const QJsonValue v = a.at(i);
            QString val;
            if (v.isArray()) {
                val = QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
            }
            else if (v.isObject()) {
                val = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
            }
            else if (v.isNull()) {
                val = "null";
            }
            else if (v.isBool()) {
                val = v.toBool() ? "true" : "false";
            }
            else if (v.isDouble()) {
                val = QString::number(v.toDouble(), 'f', 3);
            }
            else {
                val = v.toVariant().toString();
            }
            kv << QString("extra[%1]=%2").arg(i).arg(val);
        }

        return kv.join(" | ");
    }

} // namespace


static const char* kMapHtml = R"HTML(<!DOCTYPE html>
<html>

<head>
  <meta charset="utf-8">
  <title>Qt + OpenLayers</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link rel="stylesheet"
        href="https://cdn.jsdelivr.net/npm/ol@latest/ol.css">
  <style>
    html, body, #map { height: 100%; width: 100%; margin: 0; padding: 0; }
    .ol-zoom { font-size: 1rem; }
  </style>
</head>
<body>
  <div id="map"></div>

  <script src="https://cdn.jsdelivr.net/npm/ol@latest/dist/ol.js"></script>
  <script>
    const dpr = window.devicePixelRatio || 1;
    // Basic OSM map
    const map = new ol.Map({
      target: 'map',
      pixelRatio: dpr,
      layers: [
        new ol.layer.Tile({ source: new ol.source.OSM({
          // we request 512px tiles on HiDPI displays
          tilePixelRatio: dpr > 1 ? 2 : 1
        }) })
      ],
      view: new ol.View({
        center: ol.proj.fromLonLat([2.2945, 48.8584]), // Eiffel Tower
        zoom: 12
      })
    });

    // expose a helper the C++ can call to re-center/zoom.
    window.qtCenterOn = function(lon, lat, zoom) {
      const view = map.getView();
      view.animate({
        center: ol.proj.fromLonLat([lon, lat]),
        zoom: (zoom ?? view.getZoom()),
        duration: 400
      });
    };
    
    // airplane SVG pointing UP/NORTH by default
    const planeIconUrl = 'data:image/svg+xml;utf8,' + encodeURIComponent(`
      <svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="-16 -16 32 32">
        <polygon points="0,-7 2.5,7 -2.5,7" 
             fill="red"
             stroke="black"
             stroke-width="0.5"/>
      </svg>
    `);
    
    // cache styles per ~5° to avoid thousands of icon objects
    const styleCache = {};
    function styleForHeading(deg) {
      const bucket = Math.round(deg / 5) * 5;
      if (styleCache[bucket]) return styleCache[bucket];
      const rad = bucket * Math.PI / 180;
      styleCache[bucket] = new ol.style.Style({
        image: new ol.style.Icon({
          src: planeIconUrl,
          rotation: rad,          // radians
          rotateWithView: true,
          anchor: [0.5, 0.5],
          anchorXUnits: 'fraction',
          anchorYUnits: 'fraction'
        })
      });
      return styleCache[bucket];
    }
    
    // fallback dot when heading is missing
    const fallbackDot = new ol.style.Style({
      image: new ol.style.Circle({
        radius: 4,
        fill: new ol.style.Fill({ color: '#1976d2' })
      })
    });
    
    const liveSource = new ol.source.Vector();
    const liveLayer  = new ol.layer.Vector({
      source: liveSource,
      style: function(feature) {
        const hdg = feature.get('headingDeg');
        return (typeof hdg === 'number') ? styleForHeading(hdg) : fallbackDot;
      }
    });
    map.addLayer(liveLayer);

    window.qtAddLivePoint = function(lon, lat) {
      const feature = new ol.Feature({
        geometry: new ol.geom.Point(ol.proj.fromLonLat([lon, lat]))
      });
      liveSource.addFeature(feature);
    };
  </script>

  <script src="qrc:/qtwebchannel/qwebchannel.js"></script>
  <script>
    // draw returned flights
    function renderFlights(states) {
      liveSource.clear();
      const feats = [];
      for (const s of states) {
        const lon = s[5], lat = s[6];
        if (lat == null || lon == null) continue;
    
        const headingDeg = (typeof s[10] === 'number') ? s[10] : null; // true_track in degrees
    
        feats.push(new ol.Feature({
          geometry: new ol.geom.Point(ol.proj.fromLonLat([lon, lat])),
          icao24: s[0],
          callsign: s[1] || "",
          headingDeg: headingDeg
        }));
      }
      liveSource.addFeatures(feats);
    }
  
    new QWebChannel(qt.webChannelTransport, function(channel) {
      window.bridge = channel.objects.bridge;
  
      // we keep mouse log for the moment
      map.on('pointermove', (evt) => {
        const [lon, lat] = ol.proj.toLonLat(evt.coordinate, 'EPSG:3857');
        bridge.mouseMoved(lat, lon);
      });
  
      // CLICK -> ask C++ for flights in the tile
      map.on('singleclick', (evt) => {
        const [lon, lat] = ol.proj.toLonLat(evt.coordinate, 'EPSG:3857');
        const z = Math.round(map.getView().getZoom());
        bridge.requestTileAt(lat, lon, z);
      });
  
      // C++ -> receive and draw
      bridge.flightsForTile.connect(function(statesJson) {
        const payload = (typeof statesJson === "string") ? JSON.parse(statesJson) : statesJson;
        renderFlights(payload.states || payload);
      });
    });
  </script>

</body>
 
</html>)HTML";

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , webView(new QWebEngineView(this))
{
    ui->setupUi(this);
    auto* central = new QWidget(this);
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(ui->tableView);

    // right: the web map
    splitter->addWidget(webView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
    central->setLayout(layout);
    setCentralWidget(central);

	// we create a web channel to communicate with the map
    auto page = new QWebEnginePage(webView);
    webView->setPage(page);
    auto channel = new QWebChannel(webView);
    auto bridge = new Bridge(webView);
    channel->registerObject(QStringLiteral("bridge"), bridge);
    page->setWebChannel(channel);

    // backend wiring
    auto* fetcher = new OpenSkyFetcher(this);

    // Log where the exe is
    qInfo() << "appDir =" << QCoreApplication::applicationDirPath();

    QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/credentials.json",                          // next to exe
        QDir(QCoreApplication::applicationDirPath()).filePath("../../config/credentials.json"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../config/credentials.json"),
        QDir::current().filePath("config/credentials.json")                                    // current working dir
    };
    QString envPath = qEnvironmentVariable("OPENSKY_CREDENTIALS");
    if (!envPath.isEmpty()) candidates.prepend(envPath);

    QString chosen;
    for (const QString& p : candidates) {
        if (QFile::exists(p)) { chosen = p; break; }
    }
    if (chosen.isEmpty()) {
        qWarning() << "credentials.json not found. Tried:" << candidates;
    }
    else {
        qInfo() << "Using credentials at:" << chosen;
        fetcher->setCredentialsPath(chosen);   // <-- set *after* we found it
    }

    auto* liveSvc = new LiveFlightsService(fetcher, this);
    bridge->setService(liveSvc);
    // log service/bridge errors
    connect(bridge, &Bridge::error, this, [](const QString& m) { qWarning() << "[Bridge]" << m; });
    connect(liveSvc, &LiveFlightsService::serviceError, this, [](const QString& m) { qWarning() << "[Service]" << m; });

    connect(bridge, &Bridge::flightsForTile, this, [bridge](const QString& statesJson) {
        const QJsonDocument doc = QJsonDocument::fromJson(statesJson.toUtf8());
        if (!doc.isObject()) return;
        const QJsonArray states = doc.object().value("states").toArray();

        for (const auto& v : states) {
            if (!v.isArray()) continue;
            qInfo().noquote() << stateLine(v.toArray());
        }
        });

    // we load OpenLayers HTML. Base URL helps resolve relative URLs if we add assets.
    webView->setHtml(QString::fromUtf8(kMapHtml), QUrl("https://local.qt/"));

    // we make sure the resolution is not downscaled
    webView->setZoomFactor(1.0);  // 1.0 = 100%

    // just to try: using fetch button to recenter on Paris
    if (ui->fetchButton) {
        connect(ui->fetchButton, &QPushButton::clicked, this, [this] {
            webView->page()->runJavaScript("qtCenterOn(2.3522, 48.8566, 13);");
            });
    }

    // connect toolbar with buttons
	ui->toolBar->addAction(ui->actionFilter_Flights);
    ui->toolBar->addAction(ui->actionFilter_Weather);
	ui->toolBar->addAction(ui->actionFlight_Analytics);
	ui->toolBar->addAction(ui->actionView_Tool);
	ui->toolBar->addAction(ui->actionExport);
	ui->toolBar->addAction(ui->actionMarking_Tools);

	// connect toolbar buttons' actions
    connect(ui->actionFilter_Flights, &QAction::triggered, this, &MainWindow::onFilterFlights);
    connect(ui->actionFilter_Weather, &QAction::triggered, this, &MainWindow::onFilterWeather);
    connect(ui->actionFlight_Analytics, &QAction::triggered, this, &MainWindow::onAnalyseFlights);
	connect(ui->actionView_Tool, &QAction::triggered, this, &MainWindow::onViewTool);
	connect(ui->actionExport, &QAction::triggered, this, &MainWindow::onExport);
	connect(ui->actionMarking_Tools, &QAction::triggered, this, &MainWindow::onMarkingTool);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onFilterFlights()
{
}

void MainWindow::onFilterWeather()
{
}

void MainWindow::onAnalyseFlights()
{
}

void MainWindow::onViewTool()
{
}

void MainWindow::onExport()
{
}

void MainWindow::onMarkingTool()
{
}
