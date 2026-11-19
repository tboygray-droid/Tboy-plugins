#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <complex>
#include <vector>

namespace tg::dsp
{
/**
    Uniformly-partitioned overlap-save convolver (UPOLS) with a 2x2 filter matrix:

        outL = hLL * inL + hLR * inR
        outR = hRL * inL + hRR * inR

    The matrix form lets linear-phase L/R and M/S bands be combined into one FIR set.
    Latency = partition size P (plus whatever delay the impulse responses contain).

    IR updates use two slots and an atomic handshake, with no locks:
      worker: beginWrite() -> fill slot -> endWrite()       (only when the spare is free)
      audio : at a partition boundary, if a new slot is ready, both IRs are run over the
              same input history for one partition and crossfaded, then the slots swap.
*/
class PartitionedConvolver
{
public:
    using Complex = std::complex<float>;
    enum Path { LL = 0, LR = 1, RL = 2, RR = 3 };

    struct IrSlot
    {
        int numPartitions = 0;
        int taps = 0;          ///< IR length the slot was designed for (identifies quality)
        bool cross = false;    ///< use LR / RL paths
        int tag = 0;           ///< free-form id (e.g. sample rate) for validation
        std::array<std::vector<Complex>, 4> spectra;   ///< [path][partition * bins + bin]
    };

    void prepare (int partitionSize, int maxTaps, int numChannels);
    void reset() noexcept;

    int getPartitionSize() const noexcept { return P; }
    int getNumBins() const noexcept { return P + 1; }
    int getMaxPartitions() const noexcept { return maxPartitions; }

    // ---- worker thread ----
    /** Returns the spare slot if the audio thread has consumed the previous update, else nullptr. */
    IrSlot* beginWrite() noexcept;
    void endWrite() noexcept;
    /** FFT one partition of `ir` (length <= P) into `dest` (P + 1 bins). Uses a worker-owned scratch. */
    void transformPartition (const float* ir, int len, Complex* dest, juce::dsp::FFT& fft, std::vector<float>& scratch) const;

    // ---- audio thread ----
    /** Taps of the IR currently in use (0 = none). */
    int getActiveTaps() const noexcept { return slots[(size_t) active.load (std::memory_order_acquire)].taps; }
    int getActiveTag() const noexcept  { return slots[(size_t) active.load (std::memory_order_acquire)].tag; }
    void process (float* const* io, int numChannels, int numSamples) noexcept;

private:
    void processPartition() noexcept;
    void accumulate (const IrSlot& s, int outCh, Complex* acc) const noexcept;

    int P = 256, maxPartitions = 1, numCh = 2, head = 0, pos = 0;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::array<IrSlot, 2> slots;
    std::atomic<int> active { 0 };       // written by the audio thread, read by the designer thread
    std::atomic<int> spareState { 0 };   // 0 = spare free for worker, 1 = spare ready for audio

    std::array<std::vector<Complex>, 2> fdl;          // frequency-domain delay line per input
    std::array<std::vector<float>, 2> inBlock, prevIn, outBlock;
    std::vector<float> fftBuf, fftBuf2;
    std::vector<Complex> acc, acc2;
};
} // namespace tg::dsp
