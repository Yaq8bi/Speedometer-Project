#include <QApplication>
#include "window.h"
// #include <QThread>

#if defined(COMM_PROTOCOL_UART)
#include "uartservice.h"

#elif defined(COMM_PROTOCOL_TCP)
#include "tcpservice.h"

#else
#error "One of COMM_PROTOCOL_UART or COMM_PROTOCOL_TCP must be defined"
#endif

int main(int argc, char **argv)
{
#if defined(COMM_PROTOCOL_UART)
    UARTService com_service;

#elif defined(COMM_PROTOCOL_TCP)
    TCPClient com_service;
#endif

    QApplication app(argc, argv);

    Window win(com_service);
    win.show();

    return app.exec();
}