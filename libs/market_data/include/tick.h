#pragma once

#include <cstdint>

// POD market data tick transmitted over UDP.
//
// Previously this struct held std::string symbol, which made it non-trivial.
// Casting a raw UDP buffer into a non-trivial type via reinterpret_cast is
// undefined behaviour: the std::string internal state (heap pointer, size, SSO
// buffer) is not valid in arbitrary memory. Changing to char[8] makes this a
// trivially-copyable POD type, so the UDP subscriber's reinterpret_cast becomes
// well-defined under [basic.types] in the C++ standard.
//
// int64_t timestamp_ns stores nanoseconds since epoch (steady_clock), chosen
// over std::chrono::time_point because it is a fixed-size integer — safe to
// transmit as raw bytes across process/machine boundaries.
//
// Size: symbol[8] + bid(8) + ask(8) + last(8) + timestamp_ns(8) = 40 bytes.
struct Tick {
    char    symbol[8];
    double  bid;
    double  ask;
    double  last;
    int64_t timestamp_ns;
};
