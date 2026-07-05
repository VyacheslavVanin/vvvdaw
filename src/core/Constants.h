#pragma once

namespace vvvdaw {

inline constexpr int DefaultSampleRate = 48000;
inline constexpr int DefaultBufferSize = 512;
inline constexpr int MaxTracks = 32;
inline constexpr double MinVolume = 0.0;
inline constexpr double MaxVolume = 1.0;
inline constexpr double DefaultVolume = 0.8;
inline constexpr double MinPan = -1.0;
inline constexpr double MaxPan = 1.0;
inline constexpr double DefaultPan = 0.0;

// Scroll & zoom
inline constexpr int ScrollStepSamples = 48;
inline constexpr double DefaultZoom = 0.001;
inline constexpr double MinZoom = 0.000001;
inline constexpr double MaxZoom = 0.1;
inline constexpr double ZoomFactor = 1.15;

// Thread buffer sizes
inline constexpr int WriterBufferSize = 8192;
inline constexpr int ReaderBufferSize = 16384; // 8192 * 2
inline constexpr int RecordBufferSeconds = 30;
inline constexpr int PlaybackBufferSize = 32768;

// Timeline / Ruler
inline constexpr double DefaultSnapUnitSamples = 48000.0;
inline constexpr int MinLoopGapSamples = 48000;
inline constexpr int TickIntervalSamples = 48000;

// Audio
inline constexpr float MonitoringVolumeFactor = 0.7f;

} // namespace vvvdaw
