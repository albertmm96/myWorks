#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "bridge.h"
#include "openSkyFetcher.h"
#include "LiveFlightsService.h"
#include "openWeatherFetcher.h"
#include "LiveWeatherService.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QApplication>
#include <QtMath>      // for qRound
#include <QToolTip>
#include <QCursor>
#include <QDebug>

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

    static QString readApiKeyFromJsonFile(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "[Weather] Cannot open credentials file:" << path;
            return {};
        }

        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[Weather] Bad JSON in" << path << ":" << pe.errorString();
            return {};
        }

        const QString key = doc.object().value("apiKey").toString().trimmed();
        if (key.isEmpty()) {
            qWarning() << "[Weather] Missing/empty \"apiKey\" in" << path;
        }
        return key;
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
    


    // =========================
    // Selected flight track line
    // =========================
    const trackSource = new ol.source.Vector();
    
    const trackLayer = new ol.layer.Vector({
      source: trackSource,
      style: new ol.style.Style({
        stroke: new ol.style.Stroke({
          color: 'rgba(255, 0, 0, 0.90)',
          width: 6
        })
      })
    });
    map.addLayer(trackLayer);
    
    function renderTrackLine(pointsLonLat) {
      trackSource.clear();
      if (!pointsLonLat || pointsLonLat.length < 2) return;
    
      const coords3857 = pointsLonLat.map(p => ol.proj.fromLonLat([p.lon, p.lat]));
      const feat = new ol.Feature({
        geometry: new ol.geom.LineString(coords3857)
      });
      trackSource.addFeature(feat);
    }
    
    function clearTrackLine() {
      trackSource.clear();
    }




    // Weather sample circles (tile sampling)
    const weatherSampleSource = new ol.source.Vector();

    function makeWeatherSampleStyle(radiusPx) {
      return new ol.style.Style({
        image: new ol.style.Circle({
          radius: radiusPx,
          fill: new ol.style.Fill({ color: 'rgba(80, 180, 255, 0.25)' }),
          stroke: new ol.style.Stroke({ color: 'rgba(80, 180, 255, 0.95)', width: 1 })
        })
      });
    }
    
    // cache styles per integer radius to avoid recreating every render
    const weatherStyleCache = new Map();
    
    const weatherSampleLayer = new ol.layer.Vector({
      source: weatherSampleSource,
      style: function(feature, resolution) {
        // Convert resolution to an approximate zoom level
        const zoom = map.getView().getZoom();
        const radius = 3; // constant pixel radius
    
        if (!weatherStyleCache.has(radius)) {
          weatherStyleCache.set(radius, makeWeatherSampleStyle(radius));
        }
        return weatherStyleCache.get(radius);
      }
    });
    map.addLayer(weatherSampleLayer);
    
    map.getView().on('change:resolution', function() {
        weatherSampleLayer.changed();
    });

    map.addLayer(weatherSampleLayer);


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
  



      map.on('singleclick', (evt) => {
        // if user clicked a flight icon, select it (and do NOT request tiles)
        let pickedIcao = null;
      
        map.forEachFeatureAtPixel(evt.pixel, (feature, layer) => {
          const icao = feature.get('icao24');
          if (icao) {
            pickedIcao = icao;
            return true; // stop iteration
          }
          return false;
        });
      
        const [lon, lat] = ol.proj.toLonLat(evt.coordinate, 'EPSG:3857');
      
        if (pickedIcao) {
          clearTrackLine();                 // immediate visual feedback
          bridge.selectFlight(pickedIcao);  // C++ will fetch+emit track
          return;
        }
      
        // OTHERWISE: behave as before, request flights for the clicked tile
        const z = Math.round(map.getView().getZoom());
        bridge.requestTileAt(lat, lon, z);
      });





  
      // C++ -> receive and draw
      bridge.flightsForTile.connect(function(statesJson) {
        const payload = (typeof statesJson === "string") ? JSON.parse(statesJson) : statesJson;
        renderFlights(payload.states || payload);
      });




      bridge.trackLineReady.connect(function(trackJson) {
        const arr = (typeof trackJson === "string") ? JSON.parse(trackJson) : trackJson;
        renderTrackLine(arr);
      });
      
      bridge.trackCleared.connect(function() {
        clearTrackLine();
      });




      // C++ -> receive weather for the clicked location
      bridge.weatherForTile.connect(function(weatherJson) {
        const payload = (typeof weatherJson === "string") ? JSON.parse(weatherJson) : weatherJson;
        console.log("[WEATHER]", payload);
      });
      



      // clear flights layer when flights checkbox is unchecked (= c++ emits signals)
      bridge.clearFlights.connect(function () {
          liveSource.clear();
      });
      

      bridge.clearWeather.connect(function () {
        // clear vector source
        weatherSampleSource.clear();
      
        // clear per-tile cache, otherwise old features can persist/reappear
        weatherTileFeatures.clear();
      });



      // tileKey -> array of features currently displayed for that tile
      const weatherTileFeatures = new Map();
      
      bridge.weatherSamplesForTile.connect(function(samplesJson) {
        const arr = (typeof samplesJson === "string") ? JSON.parse(samplesJson) : samplesJson;
        if (!arr.length) return;
      
        const tileKey = arr[0].tileKey;
        if (!tileKey) return;
      
        // Remove only the old features for THIS tile
        const old = weatherTileFeatures.get(tileKey);
        if (old && old.length) {
          for (const f of old) weatherSampleSource.removeFeature(f);
        }
      
        // Add the new features for THIS tile
        const feats = [];
        for (const p of arr) {
          const lat = p.lat, lon = p.lon;
          if (lat == null || lon == null) continue;
      
          const f = new ol.Feature({
            geometry: new ol.geom.Point(ol.proj.fromLonLat([lon, lat]))
          });
      
          // keep tileKey on feature (optional)
          f.set("tileKey", tileKey);
      
          feats.push(f);
        }
      
        weatherSampleSource.addFeatures(feats);
        weatherTileFeatures.set(tileKey, feats);
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

    auto* leftStack = new QStackedWidget(splitter);
    leftStack->addWidget(ui->tableView);   // index 0
    leftStack->addWidget(ui->scrollArea);  // index 1

    // right: the web map
    splitter->addWidget(leftStack);
    splitter->addWidget(webView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
    central->setLayout(layout);
    setCentralWidget(central);

    connect(ui->ShowFlightsCheckBox, &QCheckBox::toggled,
        this, &MainWindow::onFlightsCheckboxToggled);

    connect(ui->ShowWeatherCheckBox, &QCheckBox::toggled,
        this, &MainWindow::onWeatherCheckboxToggled);

    sliderBubbleLabel = new QLabel(centralWidget());
    sliderBubbleLabel->setObjectName("sliderBubbleLabel");
    sliderBubbleLabel->setStyleSheet(
        "background-color: rgba(0, 0, 0, 180);"
        "color: white;"
        "padding: 2px 6px;"
        "border-radius: 4px;"
        "font-size: 10px;"
    );
    sliderBubbleLabel->setAlignment(Qt::AlignCenter);
    sliderBubbleLabel->hide();

    sliderBubbleTimer = new QTimer(this);
    sliderBubbleTimer->setSingleShot(true);
    connect(sliderBubbleTimer, &QTimer::timeout,
        sliderBubbleLabel, &QWidget::hide);

    // tracking + connections can stay where they are (after this)
    ui->latitudeSlider->setTracking(true);
    ui->longitudeSlider->setTracking(true);

    connect(ui->latitudeSlider, &QSlider::valueChanged,
        this, &MainWindow::onLatitudeSliderValueChanged);
    connect(ui->longitudeSlider, &QSlider::valueChanged,
        this, &MainWindow::onLongitudeSliderValueChanged);

    if (!ui->scrollAreaWidgetContents->layout()) {
        auto* v = new QVBoxLayout(ui->scrollAreaWidgetContents);
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(8);
        v->addWidget(ui->stackedWidget);
        v->addStretch();
    }

    ui->scrollArea->setWidgetResizable(true);
    ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->scrollAreaWidgetContents->setMinimumHeight(1000);

	// we create a web channel to communicate with the map
    auto page = new QWebEnginePage(webView);
    webView->setPage(page);
    auto channel = new QWebChannel(webView);
    bridge = new Bridge(webView);
    channel->registerObject(QStringLiteral("bridge"), bridge);
    page->setWebChannel(channel);

    // backend wiring
    auto* fetcher = new OpenSkyFetcher(this);

    // log where the exe is
    qInfo() << "appDir =" << QCoreApplication::applicationDirPath();

    QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/openSkyCredentials.json",                          // next to exe
        QDir(QCoreApplication::applicationDirPath()).filePath("../../config/openSkyCredentials.json"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../config/openSkyCredentials.json"),
        QDir::current().filePath("config/openSkyCredentials.json")                                    // current working dir
    };
    QString envPath = qEnvironmentVariable("OPENSKY_CREDENTIALS");
    if (!envPath.isEmpty()) candidates.prepend(envPath);

    QString chosen;
    for (const QString& p : candidates) {
        if (QFile::exists(p)) { chosen = p; break; }
    }
    if (chosen.isEmpty()) {
        qWarning() << "openSkyCredentials.json not found. Tried:" << candidates;
    }
    else {
        qInfo() << "Using credentials at:" << chosen;
        fetcher->setCredentialsPath(chosen);   //   set after we found it
    }

    auto* liveSvc = new LiveFlightsService(fetcher, this);
    bridge->setService(liveSvc);


    
    auto* weatherFetcher = new OpenWeatherFetcher(this);

    QStringList weatherCandidates = {
        QCoreApplication::applicationDirPath() + "/openWeatherCredentials.json",                          // next to exe
        QDir(QCoreApplication::applicationDirPath()).filePath("../../config/openWeatherCredentials.json"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../config/openWeatherCredentials.json"),
        QDir::current().filePath("config/openWeatherCredentials.json")                                    // current working dir
    };

    QString weatherEnvPath = qEnvironmentVariable("OPENWEATHER_CREDENTIALS");
    if (!weatherEnvPath.isEmpty()) weatherCandidates.prepend(weatherEnvPath);

    QString weatherChosen;
    for (const QString& p : weatherCandidates) {
        if (QFile::exists(p)) { weatherChosen = p; break; }
    }

    if (weatherChosen.isEmpty()) {
        qWarning() << "[Weather] openWeatherCredentials.json not found. Tried:" << weatherCandidates;
    }
    else {
        qInfo() << "[Weather] Using credentials at:" << weatherChosen;
        const QString apiKey = readApiKeyFromJsonFile(weatherChosen);
        if (!apiKey.isEmpty()) {
            weatherFetcher->setApiKey(apiKey);
        }
    }

    auto* weatherSvc = new LiveWeatherService(weatherFetcher, this);
    bridge->setWeatherService(weatherSvc);




    // log service/bridge errors
    connect(bridge, &Bridge::error, this, [](const QString& m) { qWarning() << "[Bridge]" << m; });
    connect(liveSvc, &LiveFlightsService::serviceError, this, [](const QString& m) { qWarning() << "[Service]" << m; });

    connect(bridge, &Bridge::flightsForTile, this, [](const QString& /*statesJson*/) {
        // intentionally no logging here: printing full state payloads is too heavyB
        });

    // we load OpenLayers HTML. Base URL helps resolve relative URLs if we add assets.
    webView->setHtml(QString::fromUtf8(kMapHtml), QUrl("https://local.qt/"));

    // we make sure the resolution is not downscaled
    webView->setZoomFactor(1.0);  // 1.0 = 100%

    // connect toolbar with buttons
	ui->toolBar->addAction(ui->actionFilter_Flights);
    ui->toolBar->addAction(ui->actionFilter_Weather);
	ui->toolBar->addAction(ui->actionFlight_Analytics);
	ui->toolBar->addAction(ui->actionView_Tool);
	ui->toolBar->addAction(ui->actionExport);
	ui->toolBar->addAction(ui->actionMarking_Tools);

	// connect toolbar buttons' actions
    connect(ui->actionFilter_Flights, &QAction::triggered, this, [=] {
        leftStack->setCurrentWidget(ui->scrollArea);
        ui->stackedWidget->setCurrentWidget(ui->pageFilterFlights);
        updateSliderRangesFromDb();
        });
    connect(ui->actionFilter_Weather, &QAction::triggered, this, [=] {
        leftStack->setCurrentWidget(ui->scrollArea);
        ui->stackedWidget->setCurrentWidget(ui->pageFilterWeather);
        updateSliderRangesFromDb();
        });
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

void MainWindow::updateSliderRangesFromDb()
{
    // full-Earth bounds (degrees)
    dbMinLat_ = -90.0;
    dbMaxLat_ = +90.0;
    dbMinLon_ = -180.0;
    dbMaxLon_ = +180.0;

    auto toSlider = [](double deg) {
        return static_cast<int>(qRound(deg * 100.0)); // 0.01° resolution
        };

    ui->latitudeSlider->setMinimum(toSlider(dbMinLat_));
    ui->latitudeSlider->setMaximum(toSlider(dbMaxLat_));
    ui->longitudeSlider->setMinimum(toSlider(dbMinLon_));
    ui->longitudeSlider->setMaximum(toSlider(dbMaxLon_));

    // optional: default at 0° / 0°
    ui->latitudeSlider->setValue(toSlider(0.0));
    ui->longitudeSlider->setValue(toSlider(0.0));
}

void MainWindow::showSliderBubble(QSlider* slider, double degrees)
{
    sliderBubbleLabel->setText(QString::number(degrees, 'f', 2) + "°");
    sliderBubbleLabel->adjustSize();

    QRect sliderRect = slider->geometry();
    QWidget* parentW = slider->parentWidget();

    QWidget* root = centralWidget();  // same parent as the label

    QPoint topRight = parentW->mapTo(root, sliderRect.topRight());

    int x = topRight.x() + 6;
    int y = topRight.y() + (sliderRect.height() - sliderBubbleLabel->height()) / 2;

    sliderBubbleLabel->move(x, y);
    sliderBubbleLabel->raise();       // make sure it's on top
    sliderBubbleLabel->show();
    sliderBubbleTimer->start(800);
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

void MainWindow::onLatitudeSliderValueChanged(int value)
{
    double degrees = value / 100.0;

    if (QApplication::mouseButtons() & Qt::LeftButton)
        showSliderBubble(ui->latitudeSlider, degrees);

    applyGeoFilter();
}

void MainWindow::onLongitudeSliderValueChanged(int value)
{
    double degrees = value / 100.0;

    if (QApplication::mouseButtons() & Qt::LeftButton)
        showSliderBubble(ui->longitudeSlider, degrees);

    applyGeoFilter();
}

void MainWindow::onFlightsCheckboxToggled(bool checked)
{
    qInfo() << "[UI] Flights checkbox =" << checked;
    bridge->setFlightsEnabled(checked);
}

void MainWindow::onWeatherCheckboxToggled(bool checked)
{
    qInfo() << "[UI] Weather checkbox =" << checked;
    bridge->setWeatherEnabled(checked);
}

void MainWindow::applyGeoFilter()
{
    if (!bridge) return;

    // slider values are stored as degrees * 100
    int latMinInt = std::min(ui->latitudeSlider->minimum(),
        ui->latitudeSlider->value());
    int latMaxInt = std::max(ui->latitudeSlider->minimum(),
        ui->latitudeSlider->value());

    int lonMinInt = std::min(ui->longitudeSlider->minimum(),
        ui->longitudeSlider->value());
    int lonMaxInt = std::max(ui->longitudeSlider->minimum(),
        ui->longitudeSlider->value());

    double minLat = latMinInt / 100.0;
    double maxLat = latMaxInt / 100.0;
    double minLon = lonMinInt / 100.0;
    double maxLon = lonMaxInt / 100.0;

    qDebug() << "[Filter] lat:" << minLat << "->" << maxLat
        << "lon:" << minLon << "->" << maxLon;

    bridge->setGeoFilter(minLat, maxLat, minLon, maxLon);
}