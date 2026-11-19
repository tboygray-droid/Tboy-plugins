#pragma once

#include "PartitionedConvolver.h"
#include "DelayLine.h"
#include "params/Parameters.h"

namespace tg::dsp
{
/**
    Linear-phase EQ: a symmetric FIR (2x2 matrix for stereo) designed from the exact
    analog magnitude of all bands, run through a partitioned convolver.

    FIR length per quality at 44.1/48 kHz: Low 2048, Medium 4096, High 8192, Max 16384
    (scaled x2 / x4 at 88.2/96 and 176.4/192 kHz so frequency resolution stays constant).

    Latency = partition size + taps / 2, and it is reported exactly to the host.
*/
class LinearPhaseEngine
{
public:
    static int tapsFor (params::LpQuality q, double sampleRate) noexcept;
    static int partitionFor (double sampleRate) noexcept;
    static int latencyFor (params::LpQuality q, double sampleRate) noexcept
    {
        return partitionFor (sampleRate) + tapsFor (q, sampleRate) / 2;
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    /** In-place. If no IR matching `quality` is loaded yet, outputs the input delayed by
        the expected latency, so timing is always correct. */
    void process (float* const* io, int numChannels, int numSamples, params::LpQuality quality) noexcept;

    PartitionedConvolver& getConvolver() noexcept { return conv; }
    double getSampleRate() const noexcept { return fs; }
    int getNumChannels() const noexcept { return numCh; }

private:
    PartitionedConvolver conv;
    DelayLine fallbackDelay;
    juce::AudioBuffer<float> dryCopy;
    double fs = 48000.0;
    int numCh = 2;
};

/** The magnitude matrix of all bands at one frequency (L/R basis). */
struct ResponseMatrix
{
    double ll = 1, lr = 0, rl = 0, rr = 1;
};

/** Combined analog response matrix of the active bands (static gains) at `hz`. */
ResponseMatrix analogMatrixAt (const std::array<params::BandSnapshot, params::kNumBands>& snaps, double hz, bool mono);

/** Designs the linear-phase FIR set. Output vectors have `taps` samples, centred at taps / 2.
    @return true if the cross paths (LR / RL) are needed (any Mid/Side band). */
bool designLinearPhaseFir (const std::array<params::BandSnapshot, params::kNumBands>& snaps,
                           double sampleRate, int taps, bool mono,
                           std::array<std::vector<float>, 4>& firOut);

/** Auto-gain estimate: negative mean dB of the static response, 20 Hz - 20 kHz (log spaced). */
float estimateAutoGainDb (const std::array<params::BandSnapshot, params::kNumBands>& snaps, bool mono);
} // namespace tg::dsp
