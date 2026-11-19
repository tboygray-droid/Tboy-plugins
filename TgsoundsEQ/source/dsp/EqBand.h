#pragma once

#include "Svf.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace tg::dsp
{
/**
    One EQ band running at the processing rate (base rate x oversampling).

    - Parameters are smoothed (freq/Q multiplicatively, gain linearly) and the
      cascade is re-designed at "control rate" every controlInterval samples.
    - Inside each control interval the SVF coefficients are ramped linearly per sample.
    - Enabling/disabling ramps a wet mix so bands fade in/out without clicks.
    - Optional dynamics: a band-limited detector (SVF at the band frequency) drives
      a gain offset (threshold / ratio / range / attack / release, up or down).

    Real-time safe: no allocations, no locks.
*/
class EqBand
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxControlInterval = 128;
    /** Samples between coefficient re-designs: 32 at 44.1/48 kHz, scaled up with the rate. */
    int controlInterval = 32;

    void prepare (double sampleRate);
    void setSampleRate (double sampleRate);   ///< RT-safe; resets state
    void reset() noexcept;

    void setMethod (DesignMethod m) noexcept { method = m; }

    /** Update targets from a parameter snapshot. Call once per block before process().
        @param deltaOnly  if true the static gain is ignored and only the dynamic gain
                          offset is applied (used for dynamics in linear-phase mode). */
    void setTarget (const params::BandSnapshot& s, bool deltaOnly = false) noexcept;

    /** True if processing would change the signal (or is fading). */
    bool isAudible() const noexcept { return mix.getCurrentValue() > 0.0 || mix.getTargetValue() > 0.0; }

    /** Process numChannels channels in place.
        @param detector  detector source channels (same domain as audio), or nullptr to use the audio input
        @param numDetChannels number of detector channels (1 or 2) */
    void process (float* const* channels, int numChannels, int numSamples,
                  const float* const* detector, int numDetChannels) noexcept;

    /** Current dynamic gain offset in dB (for the GUI). */
    float getDynamicGainDb() const noexcept { return dynGainForGui.load (std::memory_order_relaxed); }

    /** Design currently used by the audio thread (static gain + dynamics) for tests/GUI. */
    BandDesign getCurrentDesign() const noexcept { return lastDesign; }

private:
    void controlUpdate (int len, const float* const* det, int numDet, int offset) noexcept;
    void runDetector (const float* const* det, int numDet, int offset, int len) noexcept;
    void designTarget() noexcept;

    double fs = 48000.0;
    DesignMethod method = DesignMethod::Bilinear;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> freq, q;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> gain, mix;

    params::Shape shape = params::Shape::Bell;
    params::Slope slope = params::Slope::dB24;
    bool deltaMode = false;

    // dynamics
    bool dynOn = false;
    float threshold = -30.0f, range = 6.0f, ratio = 2.0f;
    double attCoef = 0.0, relCoef = 0.0;
    float dynSign = -1.0f;
    double envelope = 0.0;
    double dynGainDb = 0.0;
    SvfRuntime detFilter;
    SvfState detState;
    double detFreq = -1.0;
    std::atomic<float> dynGainForGui { 0.0f };

    // coefficient ramping
    std::array<SvfCoeffs, kMaxSections> from {}, to {};
    double scalarFrom = 1.0, scalarTo = 1.0;
    int numSections = 0;
    bool rampActive = false;
    bool needsDesign = true;
    BandDesign lastDesign;

    std::array<std::array<SvfState, kMaxSections>, kMaxChannels> state {};
};
} // namespace tg::dsp
