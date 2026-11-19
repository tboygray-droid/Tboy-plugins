#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace tg::dsp
{
/** Multi-channel integer delay with a fixed maximum, allocated in prepare(). RT-safe process(). */
class DelayLine
{
public:
    void prepare (int numChannels, int maxDelaySamples)
    {
        size = juce::nextPowerOfTwo (std::max (1, maxDelaySamples) + 1);
        buffer.setSize (std::max (1, numChannels), size);
        reset();
    }

    void reset() noexcept { buffer.clear(); writePos = 0; }

    int getMaxDelay() const noexcept { return size - 1; }

    /** Writes `in` and reads the signal delayed by `delay` samples into `out` (may alias `in`). */
    void process (const float* const* in, float* const* out, int numChannels, int numSamples, int delay) noexcept
    {
        delay = juce::jlimit (0, size - 1, delay);
        const int mask = size - 1;
        numChannels = std::min (numChannels, buffer.getNumChannels());
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            int w = writePos;
            for (int i = 0; i < numSamples; ++i)
            {
                d[w] = in[ch][i];
                out[ch][i] = d[(w - delay) & mask];
                w = (w + 1) & mask;
            }
        }
        writePos = (writePos + numSamples) & mask;
    }

private:
    juce::AudioBuffer<float> buffer;
    int size = 1, writePos = 0;
};
} // namespace tg::dsp
