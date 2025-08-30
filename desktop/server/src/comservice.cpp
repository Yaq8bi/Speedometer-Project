#include "comservice.h"
#include <climits>

void COMService::insert_data(const uint32_t start_bit, const uint32_t length, uint32_t value)
{
    uint32_t bytePosition{start_bit / CHAR_BIT};
    uint32_t bitPosition{start_bit % CHAR_BIT};

    int bitValue{0};

    std::scoped_lock lock(mtx);
    for (size_t i = 0; i < length; i++)
    {
        buffer[bytePosition] &= ~(1 << bitPosition); // Clear currently stored bit inside buffer

        bitValue = (value >> i) & 1;                       // Retrieve one bit value at a time from input value
        buffer[bytePosition] |= (bitValue << bitPosition); // Write extracted bit from value into buffer

        bitPosition++;

        if (bitPosition % CHAR_BIT == 0) // Switch byte inside the buffer when bitposition reaches end of current byte
        {
            bytePosition++;  // Increment byte position
            bitPosition = 0; // Reset bit position
        }
    }
}

void COMService::setBatteryLevel(uint32_t value)
{
    insert_data(signal["battery"].start, signal["battery"].length, value);
}

void COMService::setTemperature(int32_t value)
{
    insert_data(signal["temperature"].start, signal["temperature"].length, value);
}

void COMService::setLeftLight(bool value)
{
    insert_data(signal["signal-left"].start, signal["signal-left"].length, value);
}

void COMService::setRightLight(bool value)
{
    insert_data(signal["signal-right"].start, signal["signal-right"].length, value);
}

void COMService::setSpeed(uint32_t value)
{
    insert_data(signal["speed"].start, signal["speed"].length, value);
}