#ifndef SETTING_H
#define SETTING_H

#define BUFLEN 3
#define BAUDRATE 1048576

#define SERVER_PORT "/dev/ttyUSB0"
#define CLIENT_PORT "/dev/ttyUSB1"

#ifdef __cplusplus

#define SIGNAL_LIST {{{0, 8, 0, 240}, "speed"},         \
                     {{8, 7, 0, 100}, "battery"},       \
                     {{15, 7, -60, 60}, "temperature"}, \
                     {{22, 1, 0, 1}, "signal-left"},    \
                     {{23, 1, 0, 1}, "signal-right"}}

#include <map>
#include <tuple>
#include <string>

// To use initialize with: Setting::Signal &signal{Setting::Signal::handle()};
// To access a signal value: signal["speed"].start;

namespace Setting
{

    class Signal
    {
        struct value_t
        {
            int start, length, min, max;
        };
        using key_t = std::string;
        std::map<key_t, value_t> signal;

        Signal()
        {
            const std::tuple<value_t, key_t> list[] = SIGNAL_LIST;
#undef SIGNAL_LIST
            for (const auto &item : list)
            {
                signal.insert({std::get<key_t>(item), std::get<value_t>(item)});
            };
        }

    public:
        const value_t &operator[](const key_t &key)
        {
            return signal[key];
        }
        static Signal &handle(void)
        {
            static Signal instance;
            return instance;
        }
    };
    constexpr int INTERVAL{40};

    namespace TCPIP
    {
        constexpr int PORT{12345};
        const char IP[]{"127.0.0.1"};
    }
}

#endif
#endif