#pragma once
#include <cstdint>

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
inline constexpr int ScrollStepSamples = 8;
inline constexpr double DefaultZoom = 0.001;
inline constexpr double MinZoom = 0.000001;
// Deepest zoom, in pixels per sample: enough to discern individual samples.
// Tune this constant to control how close you can get to a single sample.
inline constexpr double SampleViewPixelsPerSample = 4.0;
inline constexpr double MaxZoom = SampleViewPixelsPerSample;
inline constexpr double ZoomFactor = 1.15;

// Waveform rendering thresholds (pixels per sample).
// At/above RawSampleRenderZoom the waveform is drawn sample-by-sample from raw
// audio instead of from the (coarse/fine) peak envelope.
inline constexpr double SampleViewZoom = 1.0;
inline constexpr double RawSampleRenderZoom = 1.0 / 16.0;

// Thread buffer sizes
inline constexpr int WriterBufferSize = 8192;
inline constexpr int ReaderBufferSize = 16384; // 8192 * 2
inline constexpr int RecordBufferSeconds = 30;
inline constexpr int PlaybackBufferSize = 32768;

// Timeline / Ruler
inline constexpr double DefaultSnapUnitSamples = 48000.0;
inline constexpr int MinLoopGapSamples = 48000;
inline constexpr int TickIntervalSamples = 48000;

// Automatic bus color scheme. A folder anchors its group with a muted hue tint
// (AutoFolder*), which the bus strips darken toward the background gray via
// blendColor(#2e2e2e, tint, 0.4) — that resolves to ~AutoStrip*. The bus
// color palette's S/V reset uses the strip values so manual colors blend in.
inline constexpr int AutoFolderSaturation = 90;
inline constexpr int AutoFolderValue = 160;
inline constexpr int AutoStripSaturation = 64;
inline constexpr int AutoStripValue = 92;

// Automatic track color scheme: without a manual color (and without a colored
// output bus) each track gets a bright, saturated stable hue so rows are easy
// to tell apart — brighter than the muted bus tints.
inline constexpr int AutoTrackSaturation = 180;
inline constexpr int AutoTrackValue = 200;

// Track color tinting: the panel and the empty timeline background only take
// a light blend of the track color over the base gray; the exact color is
// used as-is for the event backgrounds.
inline constexpr float TrackRowTintStrength = 0.35f;
inline constexpr float TrackTimelineTintStrength = 0.1f;

// Audio
inline constexpr float MonitoringVolumeFactor = 0.7f;

// Track rows: explicit height control and the bottom resize handle.
// The minimum height is computed at runtime (name row + handle) by the panel;
// these are the default / bounds for the per-track height.
inline constexpr int DefaultTrackHeight = 160;
inline constexpr int MaxTrackHeight = 600;
inline constexpr int TrackResizeHandleHeight = 6;
inline constexpr int DefaultPluginPanelWidth = 200;
// Width of the thin vertical color strip on the left of a track row.
inline constexpr int TrackColorBarWidth = 5;

// Default crossfade length applied to the junction between two adjacent
// audio events, in milliseconds.
inline constexpr int DefaultCrossfadeMs = 5;

enum class TransportState : uint8_t {
    Stopped,
    Playing,
    Paused,
    Recording,
    Precounting
};

} // namespace vvvdaw
