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

    if (!process.waitForFinished(1000)) // Wait up to 1 second
    {
        qDebug() << "udevadm timed out for port" << portName;
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
                break; // Stop as soon as we find it
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
    QString client_ESP_serial_number{};

    QString portName{CLIENT_PORT};
    serial.setPortName(portName);
    serial.setBaudRate(BAUDRATE);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    uint8_t localBuffer[BUFLEN]; // Used to transmit the buffer values, minimising lock time on real buffer.

    bool wasConnected{false}; // Track previous state
    bool portErrorDisplayed{false};
    bool SN_messageDisplayed{false};

    while (!end)
    {
        if (client_ESP_serial_number.isEmpty())
        {
            if (!SN_messageDisplayed)
            {
                //Found ESP32 port
                SN_messageDisplayed = true;
            }

            else // Represent retries as a progress bar
            {
                std::cout << "-" << std::flush;
            }
            client_ESP_serial_number = readSerialFromSys(CLIENT_PORT);
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
        if (portName.isEmpty() || !serial.isOpen())
        {
            if (!serial.open(QIODevice::ReadOnly))
            {
                if (wasConnected) // Only log once when losing connection
                {
                    wasConnected = false;
                }
                msleep(500); // Retry every 0.5s

                portName = findAvailablePort(client_ESP_serial_number);
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
            if (serial.waitForReadyRead(100)) // Wait up to 100ms
            {
                status = true;

                while (serial.bytesAvailable() >= 3)
                {
                    QByteArray data = serial.readAll();

                    // Check packet alignment
                    if (data.size() % 3 != 0)
                    {
                        qWarning() << "UART misaligned: got" << data.size() << "bytes, clearing buffer";
                        serial.clear(QSerialPort::Input);
                        break; // breaks inner while loop, outer loop continues
                    }

                    // Process in 3-byte chunks
                    for (int i = 0; i < data.size(); i += 3)
                    {
                        uint8_t localBuffer[3];
                        memcpy(localBuffer, data.constData() + i, 3);
                        {
                            std::scoped_lock lock(mtx);
                            memcpy(buffer, localBuffer, 3);
                        }
                    }
                }
            }
            else
            {
                status = false;

                // Timeout occurred; check for port errors
                if (serial.error() == QSerialPort::TimeoutError)
                {
                    // Transient timeout, try again
                    continue;
                }
                else
                {
                    serial.close();
                    break; // Exit outer loop to trigger reconnection logic
                }
            }
        }
    }
}
