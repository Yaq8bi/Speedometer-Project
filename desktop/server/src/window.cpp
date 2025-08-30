#include <QStyle>
#include <QPainter>
#include <QStyleOptionSlider>
#include "window.h"

MyCustomSlider::MyCustomSlider(QWidget *parent)
    : QAbstractSlider(parent)
{
    setValue(50);
    setFixedSize(600, 30);
    setSingleStep(1);
}

void MyCustomSlider::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QStyleOptionSlider opt;
    opt.initFrom(this);
    opt.orientation = Qt::Horizontal;
    opt.minimum = minimum();
    opt.maximum = maximum();
    opt.sliderPosition = value();
    opt.sliderValue = value();

    style()->drawComplexControl(QStyle::CC_Slider, &opt, &painter, this);
}

void MyCustomSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        int pos = qRound(event->position().x());
        int val = minimum() + (maximum() - minimum()) * pos / width();
        setValue(val);
        event->accept();
    }
}

void MyCustomSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        int pos = qRound(event->position().x());
        int val = minimum() + (maximum() - minimum()) * pos / width();
        setValue(val);
        event->accept();
    }
}

Window::Window(COMService &com_service)
{

    Setting::Signal &signal{Setting::Signal::handle()};

    // Setting the limits for the sliders
    sliderSpeed.setRange(signal["speed"].min, signal["speed"].max);
    sliderSpeed.setValue(0);

    sliderTemp.setRange(signal["temperature"].min, signal["temperature"].max);
    sliderTemp.setValue(0);

    sliderBattery.setRange(signal["battery"].min, signal["battery"].max);
    sliderBattery.setValue(0);

    labelSpeed.setMinimumWidth(70);
    labelTemp.setMinimumWidth(70);
    labelBattery.setMinimumWidth(70);

    // Setting alignemnts for the sliders and labels around them
    labelSpeedTitle.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    labelTempTitle.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    labelBatteryTitle.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    leftLabel.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label1.setAlignment(Qt::AlignVCenter);
    label2.setAlignment(Qt::AlignVCenter);
    label3.setAlignment(Qt::AlignVCenter);

    // Update values for GUI and update the buffer on comservice.
    connect(&sliderSpeed, &QAbstractSlider::valueChanged, this, [this, &com_service](int value)
            { 
                labelSpeed.setText(QString::number(value) + "Kph");
                com_service.setSpeed(value); });

    connect(&sliderTemp, &QAbstractSlider::valueChanged, this, [this, &com_service](int value)
            { 
                labelTemp.setText(QString::number(value) + "°C"); 
                com_service.setTemperature(value); });

    connect(&sliderBattery, &QAbstractSlider::valueChanged, this, [this, &com_service](int value)
            { 
                labelBattery.setText(QString::number(value) + "%"); 
                com_service.setBatteryLevel(value); });

    // Setting positions of sliders and labels in the GUI
    gridLayout.addWidget(&labelSpeedTitle, 0, 0);
    gridLayout.addWidget(&sliderSpeed, 0, 1);
    gridLayout.addWidget(&labelSpeed, 0, 2);

    gridLayout.addWidget(&labelTempTitle, 1, 0);
    gridLayout.addWidget(&sliderTemp, 1, 1);
    gridLayout.addWidget(&labelTemp, 1, 2);

    gridLayout.addWidget(&labelBatteryTitle, 2, 0);
    gridLayout.addWidget(&sliderBattery, 2, 1);
    gridLayout.addWidget(&labelBattery, 2, 2);

    CheckLayout.setColumnMinimumWidth(2, 10);
    CheckLayout.setColumnMinimumWidth(4, 10);
    CheckLayout.setHorizontalSpacing(2);

    gridLayout.addWidget(&leftLabel, 3, 0);
    CheckLayout.addWidget(&checkbox1, 3, 1, Qt::AlignRight);
    CheckLayout.addWidget(&label1, 3, 2);

    CheckLayout.addWidget(&checkbox2, 3, 3, Qt::AlignRight);
    CheckLayout.addWidget(&label2, 3, 4);

    CheckLayout.addWidget(&checkbox3, 3, 5, Qt::AlignRight);
    CheckLayout.addWidget(&label3, 3, 6);

    gridLayout.addLayout(&CheckLayout, 3, 1);

    mainLayout.addLayout(&gridLayout);
    mainLayout.setSizeConstraint(QLayout::SetFixedSize);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::Dialog);
    setModal(true); // Makes the dialog modal, so clicking outside won’t close it

    setLayout(&mainLayout);

    // Left check box.
    connect(&checkbox1, &QCheckBox::stateChanged, [this, &com_service](int state)
            {
    checkbox2.setEnabled(state != Qt::Checked);  // Disable right if left is checked

    // If hazard is not active, allow setting the light
    if (!checkbox3.isChecked()) {
        com_service.setLeftLight(state == Qt::Checked);
    } });

    // Right check box.
    connect(&checkbox2, &QCheckBox::stateChanged, [this, &com_service](int state)
            {
    checkbox1.setEnabled(state != Qt::Checked);  // Disable left if right is checked

    // If hazard is not active, allow setting the light
    if (!checkbox3.isChecked()) {
        com_service.setRightLight(state == Qt::Checked);
    } });

    // Warning (hazard lights) check box.
    connect(&checkbox3, &QCheckBox::stateChanged, [this, &com_service](int state)
            {
    if (state) {
        // Override: turn both lights on
        com_service.setLeftLight(true);
        com_service.setRightLight(true);
    } else {
        // Restore state based on checkbox1 and checkbox2
        com_service.setLeftLight(checkbox1.isChecked());
        com_service.setRightLight(checkbox2.isChecked());
    } });

    setWindowTitle("Server");
}
