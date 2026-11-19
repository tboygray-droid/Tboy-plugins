#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace tg::analyzer
{
/**
    Single-producer / single-consumer lock-free sample ring (wraps juce::AbstractFifo).
    Producer: the audio thread (mono-sums its channels into the ring).
    Consumer: the analyzer thread. If the consumer falls behind, new samples are dropped
    (the analyzer just skips ahead) - the audio thread never waits.
*/
class AnalyzerFifo
{
public:
    explicit AnalyzerFifo (int capacity = 1 << 16) : fifo (capacity), data ((size_t) capacity, 0.0f) {}

    /** Audio thread. Mono-sums up to 2 channels. Returns the number of samples written. */
    int push (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0)
            return 0;
        const float scale = numChannels > 1 ? 0.5f : 1.0f;
        const auto scope = fifo.write (std::min (numSamples, fifo.getFreeSpace()));
        auto copyBlock = [&] (int start, int size, int srcOffset)
        {
            for (int i = 0; i < size; ++i)
            {
                float v = channels[0][srcOffset + i];
                if (numChannels > 1)
                    v += channels[1][srcOffset + i];
                data[(size_t) (start + i)] = v * scale;
            }
        };
        if (scope.blockSize1 > 0) copyBlock (scope.startIndex1, scope.blockSize1, 0);
        if (scope.blockSize2 > 0) copyBlock (scope.startIndex2, scope.blockSize2, scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

    /** Analyzer thread. Returns number of samples read into dest. */
    int pull (float* dest, int maxSamples) noexcept
    {
        const auto scope = fifo.read (std::min (maxSamples, fifo.getNumReady()));
        if (scope.blockSize1 > 0) std::copy_n (data.data() + scope.startIndex1, scope.blockSize1, dest);
        if (scope.blockSize2 > 0) std::copy_n (data.data() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }
    void clear() noexcept { fifo.reset(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> data;
};
} // namespace tg::analyzer
