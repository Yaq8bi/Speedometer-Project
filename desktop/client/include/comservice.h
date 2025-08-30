 #ifndef CCOMSERVICE_H
#define CCOMSERVICE_H

#include <mutex>
#include <atomic>
#include <cstdint>
#include <iostream>
#include "setting.h"

    class COMService
    {
        Setting::Signal &signal{Setting::Signal::handle()};

        /**
         * @brief Extracts a value from the buffer based on the start and length.
         * 
         * @param start  Start bit position in the buffer
         * @param length Lenght of the bit field in the buffer
         * @param value Value to extract
         */
        void extract(uint32_t start, uint32_t length, uint32_t &value);

        /**
         * @brief Overloaded extract function for signed integers.
         * 
         * @param start Start bit position in the buffer
         * @param length Lenght of the bit field in the buffer
         * @param value Value to extract as signed integer
         */
        void extract(uint32_t start, uint32_t length, int32_t &value);

    protected:
        std::mutex mtx;
        uint8_t buffer[BUFLEN]{};
        std::atomic<bool> status{false};
        virtual void run(void) = 0;

    public:
        /**
         * @brief Get the connection status.
         * 
         * @return true if connected.  
         * @return false if disconnected.
         */
        bool getStatus(void) { return status; }

        /**
         * @brief Get the battery level.
         * 
         * @return Battery level in percentage, range 0-100
         */
        uint32_t getBatteryLevel();

        /**
         * @brief Get the temperature.
         * 
         * @return Temperature in Celsius, range -60 - 60
         */
        int32_t getTemperature();

        /**
         * @brief Get the left indicator state.
         * 
         * @return true if left indicator is on, false otherwise
         */
        bool getLeftLight();

        /**
         * @brief Get the right indicator state.
         * 
         * @return true if right indicator is on, false otherwise
         */
        bool getRightLight();

        /**
         * @brief Get the speed.
         * 
         * @return Speed in km/h, range 0 - 240
         */
        uint32_t getSpeed();

        virtual ~COMService() = default;
    };
#endif