#pragma once

#include <chrono>
#include <cstdint>

// milliseconds like GetTickCount, but precise to the millisecond (steady_clock = QueryPerformanceCounter on
// Windows) instead of the ~15.6 ms steps of GetTickCount; wraps around the same way
inline uint32_t GetPreciseTickCount()
{
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
