#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

/**
    Central definition of every parameter ID, enum and range used by TgsoundsEQ.
    Keep IDs stable: they are persisted in sessions and presets.
*/
namespace tg::params
{
inline constexpr int kNumBands = 24;
inline constexpr int kVersionHint = 1;

// ---- Enumerations (the order is persisted, only ever append) ----------------
enum class Shape     { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, FlatTilt };
enum class Slope     { dB6, dB12, dB18, dB24, dB36, dB48, dB72, dB96, Brickwall };
enum class Placement { Stereo, Left, Right, Mid, Side };
enum class Mode      { ZeroLatency, NaturalPhase, LinearPhase };
enum class LpQuality { Low, Medium, High, Max };
enum class OsFactor  { Off, x2, x4 };
enum class DynDir    { Down, Up };
enum class DetSource { Internal, External };

inline const juce::StringArray shapeNames     { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                                                "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" };
inline const juce::StringArray slopeNames     { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct",
                                                "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
inline const juce::StringArray placementNames { "Stereo", "Left", "Right", "Mid", "Side" };
inline const juce::StringArray modeNames      { "Zero Latency", "Natural Phase", "Linear Phase" };
inline const juce::StringArray qualityNames   { "Low", "Medium", "High", "Max" };
inline const juce::StringArray osNames        { "Off", "2x", "4x" };
inline const juce::StringArray dynDirNames    { "Gain Down", "Gain Up" };
inline const juce::StringArray detSourceNames { "Internal", "External SC" };

/** Order (in Butterworth sense) of a cut slope. Brickwall is a very steep 32nd order cascade. */
constexpr int slopeOrder (Slope s) noexcept
{
    constexpr int orders[] { 1, 2, 3, 4, 6, 8, 12, 16, 32 };
    return orders[(int) s];
}

constexpr bool shapeHasGain (Shape s) noexcept
{
    return s == Shape::Bell || s == Shape::LowShelf || s == Shape::HighShelf
        || s == Shape::TiltShelf || s == Shape::FlatTilt;
}

constexpr bool shapeUsesSlope (Shape s) noexcept
{
    return s == Shape::LowCut || s == Shape::HighCut || s == Shape::LowShelf
        || s == Shape::HighShelf || s == Shape::TiltShelf;
}

// ---- Ranges ------------------------------------------------------------------
inline constexpr float kFreqMin = 10.0f,   kFreqMax = 30000.0f;
inline constexpr float kGainMin = -30.0f,  kGainMax = 30.0f;
inline constexpr float kQMin    = 0.025f,  kQMax    = 40.0f;

// ---- Per-band parameter suffixes ---------------------------------------------
namespace band
{
    inline constexpr const char* used      = "used";
    inline constexpr const char* enabled   = "on";
    inline constexpr const char* freq      = "freq";
    inline constexpr const char* gain      = "gain";
    inline constexpr const char* q         = "q";
    inline constexpr const char* shape     = "shape";
    inline constexpr const char* slope     = "slope";
    inline constexpr const char* placement = "place";
    inline constexpr const char* dynOn     = "dyn";
    inline constexpr const char* threshold = "thr";
    inline constexpr const char* range     = "range";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* attack    = "att";
    inline constexpr const char* release   = "rel";
    inline constexpr const char* dynDir    = "dir";
    inline constexpr const char* detSource = "sc";

    inline constexpr std::array<const char*, 16> all { used, enabled, freq, gain, q, shape, slope, placement,
                                                       dynOn, threshold, range, ratio, attack, release,
                                                       dynDir, detSource };
}

/** e.g. bandID (0, "freq") -> "b01_freq" */
inline juce::String bandID (int bandIndex, const char* suffix)
{
    return "b" + juce::String (bandIndex + 1).paddedLeft ('0', 2) + "_" + suffix;
}

// ---- Global parameters ---------------------------------------------------------
namespace global
{
    inline constexpr const char* outGain   = "out_gain";
    inline constexpr const char* outPan    = "out_pan";
    inline constexpr const char* autoGain  = "auto_gain";
    inline constexpr const char* phaseInv  = "phase_inv";
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* lpQuality = "lp_quality";
    inline constexpr const char* os        = "oversampling";
    inline constexpr const char* bypass    = "bypass";
}
} // namespace tg::params
