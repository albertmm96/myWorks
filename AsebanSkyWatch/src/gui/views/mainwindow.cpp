#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QFile>

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

    // Expose a helper your C++ can call to re-center/zoom.
    window.qtCenterOn = function(lon, lat, zoom) {
      const view = map.getView();
      view.animate({
        center: ol.proj.fromLonLat([lon, lat]),
        zoom: (zoom ?? view.getZoom()),
        duration: 400
      });
    };

    // Optional: add a vector layer for “live” points (e.g., GPS updates)
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

    // we load OpenLayers HTML. Base URL helps resolve relative URLs if we add assets.
    webView->setHtml(QString::fromUtf8(kMapHtml), QUrl("https://local.qt/"));

    // we make sure the resolution is not downscaled
    webView->setZoomFactor(1.0);  // 1.0 = 100%

    // what the page thinks the dpr is
    //webView->page()->runJavaScript("window.devicePixelRatio", [](const QVariant& v) {
    //    qDebug() << "DPR in WebEngine =" << v;
    //    });

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