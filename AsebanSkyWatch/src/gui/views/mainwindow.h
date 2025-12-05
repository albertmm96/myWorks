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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow* ui;
    QWebEngineView* webView;  // the map

    void updateSliderRangesFromDb();

    // --- slider bubble state ---
    QLabel* sliderBubbleLabel = nullptr;
    QTimer* sliderBubbleTimer = nullptr;

    void showSliderBubble(QSlider* slider, double degrees);

private slots:
    void onFilterFlights();
    void onFilterWeather();
    void onAnalyseFlights();
    void onViewTool();
    void onExport();
    void onMarkingTool();

    // sliders
    void onLatitudeSliderValueChanged(int value);
    void onLongitudeSliderValueChanged(int value);
};