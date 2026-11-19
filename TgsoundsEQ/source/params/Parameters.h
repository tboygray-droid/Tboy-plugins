#pragma once

#include "ParamIDs.h"

namespace tg::params
{
/** Builds the complete APVTS parameter layout (24 bands + globals). */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Frequency range with logarithmic mapping (10 Hz .. 30 kHz). */
juce::NormalisableRange<float> logRange (float min, float max);

/** Cached raw atomic pointers for one band. Reading them is lock-free and real-time safe. */
struct BandParamRefs
{
    std::atomic<float>* used {};
    std::atomic<float>* enabled {};
    std::atomic<float>* freq {};
    std::atomic<float>* gain {};
    std::atomic<float>* q {};
    std::atomic<float>* shape {};
    std::atomic<float>* slope {};
    std::atomic<float>* placement {};
    std::atomic<float>* dynOn {};
    std::atomic<float>* threshold {};
    std::atomic<float>* range {};
    std::atomic<float>* ratio {};
    std::atomic<float>* attack {};
    std::atomic<float>* release {};
    std::atomic<float>* dynDir {};
    std::atomic<float>* detSource {};
};

/** A plain value snapshot of one band, convenient for DSP design code. */
struct BandSnapshot
{
    bool used = false, enabled = true;
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    Shape shape = Shape::Bell;
    Slope slope = Slope::dB24;
    Placement placement = Placement::Stereo;
    bool dynOn = false;
    float threshold = -30.0f, range = 6.0f, ratio = 2.0f, attackMs = 10.0f, releaseMs = 120.0f;
    DynDir dynDir = DynDir::Down;
    DetSource detSource = DetSource::Internal;

    bool isActive() const noexcept { return used && enabled; }
};

struct GlobalParamRefs
{
    std::atomic<float>* outGain {};
    std::atomic<float>* outPan {};
    std::atomic<float>* autoGain {};
    std::atomic<float>* phaseInv {};
    std::atomic<float>* mode {};
    std::atomic<float>* lpQuality {};
    std::atomic<float>* os {};
    std::atomic<float>* bypass {};
};

struct ParamRefs
{
    std::array<BandParamRefs, kNumBands> bands;
    GlobalParamRefs global;

    void bind (juce::AudioProcessorValueTreeState& apvts);
    BandSnapshot snapshot (int band) const noexcept;
};
} // namespace tg::params
