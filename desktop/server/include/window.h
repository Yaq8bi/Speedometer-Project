#ifndef WINDOW_H
#define WINDOW_H

#include <QLabel>
#include <QDialog>
#include <QAbstractSlider>
#include <QSlider>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QMouseEvent>
#include <QWidget>
#include <QCheckBox>
#include "comservice.h"

class MyCustomSlider : public QAbstractSlider
{

public:
    /**
     * @brief Function that sets the size of the sliders
     * 
     * @param parent 
     */
    explicit MyCustomSlider(QWidget *parent = nullptr);

protected:
    /**
     * @brief Paint Event handler for Qt widgets
     * 
     * @param event 
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief Enables click-to-jump behavior on sliders
     * 
     * @param event 
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief Enables drag support for sliders
     * 
     * @param event 
     */
    void mouseMoveEvent(QMouseEvent *event) override;
};

class Window : public QDialog
{
    QLabel leftLabel{"Light Signals:"};
    QLabel label1{"Left"};
    QLabel label2{"Right"};
    QLabel label3{"Warning"};

    QCheckBox checkbox1;
    QCheckBox checkbox2;
    QCheckBox checkbox3;

public:
    bool server_window_closed{false};
    Window(COMService &com_service);

private:

    QVBoxLayout mainLayout;
    QGridLayout gridLayout;
    QGridLayout CheckLayout;

    MyCustomSlider sliderSpeed;
    QLabel labelSpeed{"0 Kph"};

    MyCustomSlider sliderTemp;
    QLabel labelTemp{"0 °C"};

    MyCustomSlider sliderBattery;
    QLabel labelBattery{"0 %"};

    QLabel labelSpeedTitle{"Speed :"};
    QLabel labelTempTitle{"Temperature:"};
    QLabel labelBatteryTitle{"Battery:"};

protected:
    /**
     * @brief Overrides the closeEvent virtual function from QWidget
     * 
     * @param event 
     */
    void closeEvent(QCloseEvent *event) override;
};

#endif // WINDOW_H
