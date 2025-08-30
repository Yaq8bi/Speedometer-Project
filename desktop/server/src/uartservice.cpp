#include <QDebug>
#include "setting.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include "uartservice.h"
#include <iostream>
#include <QProcess>

// Find Relevant ID number via lsusb
#define ESP32_PID 0xea60 // Product ID for ESP-C6

// Retrieve the short serial number of a USB device associated with a given TTY port
QString readSerialFromSys(const QString &portName)
{
    QString serialNumber{};

    QProcess process;
    process.start("udevadm", QStringList() << "info" << "-q" << "all" << "-n" << portName);

    if (!process.waitForFinished(1000)) // wait up to 1 second
    {
        ;
    }

    else
    {
        QString output = process.readAllStandardOutput();

        // Extract the ID_SERIAL_SHORT line
        for (const QString &line : output.split('\n'))
        {
            if (line.startsWith("E: ID_SERIAL_SHORT="))
            {
                serialNumber = line.section('=', 1);
                break; // stop as soon as we find it
            }
        }
    }

    return serialNumber;
}

QString findAvailablePort(const QString &espSN)
{
    QString port;

    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        QString sysSN = readSerialFromSys(info.systemLocation());

        if (sysSN == espSN) // SN match is enough
        {
            port = info.systemLocation();
            break;
        }
    }
    return port;
}

void UARTService::run(void)
{
    QSerialPort serial;
    QString server_ESP_serial_number{};

    QString portName{SERVER_PORT};
    serial.setPortName(portName);
    serial.setBaudRate(BAUDRATE);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    uint8_t localBuffer[BUFLEN]; // Used to transmit the buffer values, minimising lock time on real buffer.

    bool SN_messageDisplayed{false};
    bool wasConnected{false}; // Track previous state
    bool portErrorDisplayed{false};

    while (!end)
    {
        if (server_ESP_serial_number.isEmpty())
        {
            if (!SN_messageDisplayed)
            {
                SN_messageDisplayed = true;
            }

            server_ESP_serial_number = readSerialFromSys(SERVER_PORT);
        }

        else
        {
            break;
        }
        msleep(500);
    }

    while (!end)
    {
        // Ensure port is open
        if (!serial.isOpen())
        {
            if (!serial.open(QIODevice::WriteOnly))
            {
                if (wasConnected) // Only log once when losing connection
                {
                    wasConnected = false;
                }
                msleep(500); // Retry every 0.5s

                portName = findAvailablePort(server_ESP_serial_number);
                if (portName.isEmpty())
                {

                    if (!portErrorDisplayed)
                    {
                        portErrorDisplayed = true;
                    }

                    continue;
                }

                else
                {
                    serial.setPortName(portName);
                    portErrorDisplayed = false;
                }
            }

            continue;
        }
        else
        {
            status = true;
            wasConnected = true;
        }

        while (!end)
        {
            {
                std::scoped_lock lock(mtx);
                std::memcpy(localBuffer, buffer, BUFLEN); // Buffer will only need to be locked during this copy operation instead of the entire transmission time
            }

            serial.write(reinterpret_cast<char *>(localBuffer), BUFLEN);

            if (!serial.waitForBytesWritten(100)) // Timeout in ms
            {
                status = false;
                serial.close();
                break;
            }

            msleep(Setting::INTERVAL);
        }
    }
    serial.close();
}