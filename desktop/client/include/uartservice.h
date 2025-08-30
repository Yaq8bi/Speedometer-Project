#ifndef UARTCOM_H
#define UARTCOM_H

#include <QThread>
#include "comservice.h"

class UARTService : public COMService, public QThread
{
    void run(void) override;
    std::atomic<bool> end{false};
    
    public:
    UARTService()
    {
        start();
    }

    ~UARTService()
    {
        end = true;
        wait();
    }
};

#endif