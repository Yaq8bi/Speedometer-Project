#include "window.h"
#include "setting.h"
#include <QObject>
#include <QTimer>
#include <QBoxLayout>

Window::Window(COMService &com_service) : canvas(this) // Initialize canvas with this window as parent
{
    setWindowTitle("Client");
    setWindowFlags(Qt::WindowStaysOnTopHint);

    // Uncomment for ADAPTIVE WINDOW  = DISABLE "setFixedSize();"!
    // setMinimumSize(1300,900);
    setFixedSize(800, 560);

    setStyleSheet("background-color: rgb(50, 50, 50);");

    // Create a layout and set it to the dialog
    QBoxLayout layout = QBoxLayout(QBoxLayout::TopToBottom, this);
    setLayout(&layout);
    QObject::connect(&update_timer, &QTimer::timeout, this, [this, &com_service]()
                     {
                         // --- TEST WITH FAKE DATA ---
                         // If this worwks com service is not working...
                         static int counter = 0;
                         counter++;
                        
                         canvas.connection_set_status(com_service.getStatus());
                         
                         canvas.battery_set_level(com_service.getBatteryLevel());
                         canvas.thermometer_set_temperature(com_service.getTemperature());
                         canvas.speedometer_set_speed(com_service.getSpeed());
                         canvas.indicator_set_left(com_service.getLeftLight());
                         canvas.indicator_set_right(com_service.getRightLight());

                         canvas.update(); // Request a repaint
                     });

    update_timer.start(Setting::INTERVAL);

    // Create the canvas and add it to the layout
    layout.addWidget(&canvas);
}