#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace tg::dsp
{
/**
    Final gain stage: output gain, auto-gain compensation, balance pan and polarity.
    All targets are smoothed; process() is allocation free.
*/
class OutputStage
{
public:
    void prepare (double sampleRate)
    {
        for (auto* s : { &gainL, &gainR, &gainMono })
        {
            s->reset (sampleRate, 0.03);
            s->setCurrentAndTargetValue (1.0f);
        }
    }

    /** @param outGainDb  user output gain
        @param autoGainDb compensation from the auto-gain estimator (0 when disabled)
        @param pan        -1 (left) .. +1 (right), balance law
        @param invert     polarity flip */
    void setTargets (float outGainDb, float autoGainDb, float pan, bool invert) noexcept
    {
        const float g = juce::Decibels::decibelsToGain (outGainDb + autoGainDb) * (invert ? -1.0f : 1.0f);
        const float l = pan > 0.0f ? 1.0f - pan : 1.0f;
        const float r = pan < 0.0f ? 1.0f + pan : 1.0f;
        gainL.setTargetValue (g * l);
        gainR.setTargetValue (g * r);
        gainMono.setTargetValue (g);
    }

    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        if (numChannels == 1)
        {
            // Mono: pan is meaningless, use the unpanned gain.
            applyRamp (buffer.getWritePointer (0), numSamples, gainMono);
            gainL.skip (numSamples);
            gainR.skip (numSamples);
            return;
        }
        applyRamp (buffer.getWritePointer (0), numSamples, gainL);
        applyRamp (buffer.getWritePointer (1), numSamples, gainR);
        gainMono.skip (numSamples);
    }

private:
    static void applyRamp (float* x, int n, juce::SmoothedValue<float>& g) noexcept
    {
        if (! g.isSmoothing())
        {
            juce::FloatVectorOperations::multiply (x, g.getTargetValue(), n);
            return;
        }
        for (int i = 0; i < n; ++i)
            x[i] *= g.getNextValue();
    }

    juce::SmoothedValue<float> gainL, gainR, gainMono;
};
} // namespace tg::dsp
