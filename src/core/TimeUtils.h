#pragma once
#include <QString>
#include <cstdint>
#include <cmath>

namespace TimeUtils {

inline QString formatTime(int64_t samplePos, int sampleRate) {
    if (sampleRate <= 0) return "00:00:00.000";
    int totalMs = static_cast<int>((samplePos * 1000) / sampleRate);
    int hours = totalMs / 3600000;
    int mins = (totalMs % 3600000) / 60000;
    int secs = (totalMs % 60000) / 1000;
    int ms = totalMs % 1000;
    return QString("%1:%2:%3.%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

inline int64_t snapSample(int64_t sample, double unit) {
    if (unit <= 0.0) return sample;
    double rem = std::fmod(static_cast<double>(sample), unit);
    if (rem < unit / 2.0)
        return sample - static_cast<int64_t>(rem);
    else
        return sample + static_cast<int64_t>(unit - rem);
}

} // namespace TimeUtils
