#pragma once

#include <QMainWindow>
#include <QWebEngineView>
#include <QWebChannel>

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
};