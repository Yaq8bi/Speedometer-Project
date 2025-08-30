#include "comservice.h"
#include <cstring>
#include <bitset>
#include <iostream>

// Read buffer as little-endian 24-bit integer
static uint32_t bufferToValue(const uint8_t *buffer)
{
    return buffer[0] | (buffer[1] << 8) | (buffer[2] << 16);
}
// About the comments below for Muetx, if we need them back, we can try the solution i came up with below.
// Problem: Lock guard implementation is blocking the canvas updates.
// Solution i could think of is to run the updates for Comeservice in a seperate thread as to mitigae the blcking issue.

uint32_t COMService::getBatteryLevel()
{

    uint32_t val{0};
    if(status)
    {
        {
            std::scoped_lock lock(mtx);
            val = bufferToValue(buffer);
        }
            auto sig = signal["battery"];
            extract(sig.start, sig.length, val);
    }
    
    return val;
}

int32_t COMService::getTemperature()
{

    int32_t val{0};
    if(status)
    {
        {
            std::scoped_lock lock(mtx);
            val = static_cast<int32_t>(bufferToValue(buffer));
        }
        auto sig = signal["temperature"];
        extract(sig.start, sig.length, val);
    }

    return val;
}

bool COMService::getLeftLight()
{
    uint32_t val{0};
    if(status)
    {
        {
            std::scoped_lock lock(mtx);
            val = bufferToValue(buffer);
        }
        auto sig = signal["signal-left"];
        extract(sig.start, sig.length, val);
    }
    
    return val;
}
bool COMService::getRightLight()
{
    uint32_t val{0};
    if(status)
    {
        {
            std::scoped_lock lock(mtx);
            val = bufferToValue(buffer);
        }
        auto sig = signal["signal-right"];
        extract(sig.start, sig.length, val);
    }
    
    return val;
}
uint32_t COMService::getSpeed()
{
    uint32_t val{0};
    if(status)
    {
        {
            std::scoped_lock lock(mtx);
            val = bufferToValue(buffer);
        }
        auto sig = signal["speed"];
        extract(sig.start, sig.length, val);
    }

    return val;
}

void COMService::extract(uint32_t start, uint32_t length, uint32_t &value)
{
    if (start >= 32 || length == 0 || (start + length > 32))
    {
        value = 0;
        return;
    }

    uint32_t mask = (1u << length) - 1;
    value = (value >> start) & mask;
}
void COMService::extract(uint32_t start, uint32_t length, int32_t &value)
{
    if (start >= 32 || length == 0 || (start + length > 32))
    {
        value = 0;
        return;
    }

    uint32_t mask = (1u << length) - 1;
    uint32_t extracted = (static_cast<uint32_t>(value )>> start) & mask;

    if (extracted & (1u << (length - 1)))
    {
        extracted |= ~mask;
    }

    value = static_cast<int32_t>(extracted); // Overwrite original with signed value as int32_t
}