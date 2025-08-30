#include <QApplication>
#include "window.h"

#if defined(COMM_PROTOCOL_UART)
#include "uartservice.h"

#elif defined(COMM_PROTOCOL_TCP)
#include "tcpservice.h"

#else
#error "One of COMM_PROTOCOL_UART or COMM_PROTOCOL_TCP must be defined"
#endif

void Window::closeEvent(QCloseEvent *event)
{
    event->accept();
}

int main(int argc, char **argv)
{
#if defined(COMM_PROTOCOL_UART)
    UARTService comms;

#elif defined(COMM_PROTOCOL_TCP)
    TCPService comms;

#endif

    QApplication app(argc, argv);

    Window win(comms);

    win.show();
    return app.exec();
}
