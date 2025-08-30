#ifndef COMSERVICE_H
#define COMSERVICE_H

#include <mutex>
#include <atomic>
#include <cstdint>
#include "setting.h"

class COMService
{
private:
    /**
     * @brief Instance of class in setting.h
     * 
     */
    Setting::Signal &signal{Setting::Signal::handle()};

    /**
     * @brief Function to insert data into a buffer
     * 
     * @param start_bit Start-position in the buffer
     * @param length    How many bits the value occupies in the buffer
     * @param value     The value that is to be inserted in the buffer
     */
    void insert_data(const uint32_t start_bit, const uint32_t length, uint32_t value);

protected:
    std::mutex mtx;
    uint8_t buffer[BUFLEN]{};
    std::atomic<bool> status{false};

    /**
     * @brief Pure Virutal function to be implemented in other file
     * 
     */
    virtual void run(void) = 0;

public:
    /**
     * @brief Function to set the value for the battery
     * 
     * @param value The value to be sett
     */
    void setBatteryLevel(uint32_t value);

    /**
     * @brief Function to set the value for the temperature
     *
     * @param value The value to be sett
     */
    void setTemperature(int32_t value);

    /**
     * @brief Function to set the value for the left blinker light
     *
     * @param value The value to be sett
     */
    void setLeftLight(bool value);

    /**
     * @brief Function to set the value for the right blinker light
     *
     * @param value The value to be sett
     */
    void setRightLight(bool value);

    /**
     * @brief Function to set the value for the speed
     *
     * @param value The value to be sett
     */
    void setSpeed(uint32_t value);

    /**
     * @brief Destructor for the COMService object
     * 
     */
    virtual ~COMService() = default;
};

#endif