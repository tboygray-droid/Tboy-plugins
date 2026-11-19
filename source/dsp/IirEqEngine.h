#pragma once

#include "EqBand.h"
#include <juce_dsp/juce_dsp.h>

namespace tg::dsp
{
/**
    The 24-band minimum-phase EQ (Zero Latency / Natural Phase modes).

    Bands run in series in index order. Each band works in its own stereo domain
    (Stereo / Left / Right in L-R, Mid / Side in M-S); the engine converts the
    buffer between L/R and M/S only when the next band needs the other domain.
*/
class IirEqEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void setSampleRate (double sampleRate);   ///< RT-safe (no allocation)
    void reset() noexcept;
    void setMethod (DesignMethod m) noexcept;

    void setTargets (const std::array<params::BandSnapshot, params::kNumBands>& snaps, bool deltaOnly) noexcept;

    /** @param main      audio, processed in place
        @param detMain   copy of the unprocessed main input (same rate/length) for internal detection
        @param detSide   external sidechain (same rate/length) or an empty block */
    void process (juce::dsp::AudioBlock<float> main,
                  juce::dsp::AudioBlock<const float> detMain,
                  juce::dsp::AudioBlock<const float> detSide) noexcept;

    float getDynamicGainDb (int band) const noexcept { return bands[(size_t) band].getDynamicGainDb(); }
    const EqBand& getBand (int b) const noexcept { return bands[(size_t) b]; }

private:
    static void toMidSide (float* l, float* r, int n) noexcept;
    static void toLeftRight (float* m, float* s, int n) noexcept;

    std::array<EqBand, params::kNumBands> bands;
    std::array<params::Placement, params::kNumBands> placement {};
    std::array<params::DetSource, params::kNumBands> source {};
    juce::AudioBuffer<float> detMainMS, detSideMS;
    int maxBlock = 0;
};
} // namespace tg::dsp
