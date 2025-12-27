#pragma once

#include <QMainWindow>
#include <QWebEngineView>
#include <QWebChannel>
#include <QLabel>
#include <QTimer>
#include <QSlider>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace Ui {
    class MainWindow;
}

class Bridge;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow* ui;
    QWebEngineView* webView;

    Bridge* bridge = nullptr;

    void updateSliderRangesFromDb();

    QLabel* sliderBubbleLabel = nullptr;
    QTimer* sliderBubbleTimer = nullptr;

    void showSliderBubble(QSlider* slider, double degrees);
    void applyGeoFilter();

    double dbMinLat_ = -90.0;
    double dbMaxLat_ = +90.0;
    double dbMinLon_ = -180.0;
    double dbMaxLon_ = +180.0;

private slots:
    void onFilterFlights();
    void onFilterWeather();
    void onAnalyseFlights();
    void onViewTool();
    void onExport();
    void onMarkingTool();

    void onLatitudeSliderValueChanged(int value);
    void onLongitudeSliderValueChanged(int value);
};