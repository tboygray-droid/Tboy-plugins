#pragma once

#include "LinearPhaseEngine.h"
#include <juce_core/juce_core.h>

namespace tg::dsp
{
/**
    Background thread that turns parameter changes into "expensive" designs:
      - the auto-gain compensation value
      - linear-phase FIR sets, transformed into convolver partitions
    It polls parameter atomics (no involvement of the audio thread at all), so the
    audio thread never blocks or allocates for this work.
*/
class EqDesigner : private juce::Thread
{
public:
    EqDesigner (const params::ParamRefs& refs, LinearPhaseEngine& engine);
    ~EqDesigner() override;

    /** Must be called while stopped (e.g. from prepareToPlay). */
    void start (double sampleRate, bool mono);
    void stop();

    float getAutoGainDb() const noexcept { return autoGain.load (std::memory_order_relaxed); }

    /** Synchronously run one design pass (used by tests and offline rendering). */
    void runOnce() { designPass (true); }

private:
    void run() override;
    bool designPass (bool force);

    const params::ParamRefs& refs;
    LinearPhaseEngine& lp;
    double fs = 48000.0;
    bool isMono = false;
    std::atomic<float> autoGain { 0.0f };

    std::vector<float> lastSignature;
    int lastTaps = 0;
    juce::CriticalSection passLock;   // serialises runOnce() vs. the thread (never touched by audio)
    std::unique_ptr<juce::dsp::FFT> partitionFft;
    std::vector<float> scratch;
};
} // namespace tg::dsp
