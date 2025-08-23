#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "bridge.h"
#include "openSkyFetcher.h"
#include "LiveFlightsService.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QFile>
#include <QDir>

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

    // we add a vector layer for “live” points (e.g., GPS updates)
    const liveSource = new ol.source.Vector();
    const liveLayer  = new ol.layer.Vector({ source: liveSource });
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
        feats.push(new ol.Feature({
          geometry: new ol.geom.Point(ol.proj.fromLonLat([lon, lat])),
          icao24: s[0], callsign: s[1] || ""
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

}

MainWindow::~MainWindow()
{
    delete ui;
}