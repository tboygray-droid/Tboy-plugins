#include "PartitionedConvolver.h"

namespace tg::dsp
{
void PartitionedConvolver::prepare (int partitionSize, int maxTaps, int numChannels)
{
    P = partitionSize;
    jassert (juce::isPowerOfTwo (P));
    numCh = juce::jlimit (1, 2, numChannels);
    maxPartitions = (maxTaps + P - 1) / P;
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (2 * P)));

    const size_t bins = (size_t) P + 1;
    for (auto& s : slots)
    {
        for (auto& sp : s.spectra)
            sp.assign ((size_t) maxPartitions * bins, Complex {});
        s.numPartitions = 0;
        s.taps = 0;
        s.cross = false;
        s.tag = 0;
    }
    active.store (0);
    spareState.store (0);

    for (int ch = 0; ch < 2; ++ch)
    {
        fdl[(size_t) ch].assign ((size_t) maxPartitions * bins, Complex {});
        inBlock[(size_t) ch].assign ((size_t) P, 0.0f);
        prevIn[(size_t) ch].assign ((size_t) P, 0.0f);
        outBlock[(size_t) ch].assign ((size_t) P, 0.0f);
    }
    fftBuf.assign ((size_t) (4 * P), 0.0f);
    fftBuf2.assign ((size_t) (4 * P), 0.0f);
    acc.assign (bins, Complex {});
    acc2.assign (bins, Complex {});
    reset();
}

void PartitionedConvolver::reset() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (fdl[(size_t) ch].begin(), fdl[(size_t) ch].end(), Complex {});
        std::fill (inBlock[(size_t) ch].begin(), inBlock[(size_t) ch].end(), 0.0f);
        std::fill (prevIn[(size_t) ch].begin(), prevIn[(size_t) ch].end(), 0.0f);
        std::fill (outBlock[(size_t) ch].begin(), outBlock[(size_t) ch].end(), 0.0f);
    }
    head = 0;
    pos = 0;
}

PartitionedConvolver::IrSlot* PartitionedConvolver::beginWrite() noexcept
{
    if (spareState.load (std::memory_order_acquire) != 0)
        return nullptr;
    return &slots[(size_t) (1 - active.load (std::memory_order_acquire))];
}

void PartitionedConvolver::endWrite() noexcept
{
    spareState.store (1, std::memory_order_release);
}

void PartitionedConvolver::transformPartition (const float* ir, int len, Complex* dest,
                                               juce::dsp::FFT& f, std::vector<float>& scratch) const
{
    scratch.assign ((size_t) (4 * P), 0.0f);
    std::copy (ir, ir + len, scratch.begin());
    f.performRealOnlyForwardTransform (scratch.data(), true);
    for (int k = 0; k <= P; ++k)
        dest[k] = { scratch[(size_t) (2 * k)], scratch[(size_t) (2 * k + 1)] };
}

void PartitionedConvolver::accumulate (const IrSlot& s, int outCh, Complex* out) const noexcept
{
    const int bins = P + 1;
    std::fill (out, out + bins, Complex {});

    // Paths contributing to this output: direct (LL / RR) and, if enabled, cross (LR / RL).
    const int direct = outCh == 0 ? LL : RR;
    const int crossP = outCh == 0 ? LR : RL;
    const int directIn = numCh == 1 ? 0 : outCh;
    const int crossIn  = 1 - outCh;

    auto mac = [&] (int path, int inCh)
    {
        const Complex* H = s.spectra[(size_t) path].data();
        const Complex* X = fdl[(size_t) inCh].data();
        for (int k = 0; k < s.numPartitions; ++k)
        {
            int slot = head - k;
            if (slot < 0) slot += maxPartitions;
            const Complex* x = X + (size_t) slot * (size_t) bins;
            const Complex* h = H + (size_t) k * (size_t) bins;
            // Manual complex MAC on interleaved floats (faster than std::complex operator*).
            auto* o = reinterpret_cast<float*> (out);
            auto* xf = reinterpret_cast<const float*> (x);
            auto* hf = reinterpret_cast<const float*> (h);
            for (int b = 0; b < bins; ++b)
            {
                const float xr = xf[2 * b], xi = xf[2 * b + 1];
                const float hr = hf[2 * b], hi = hf[2 * b + 1];
                o[2 * b]     += xr * hr - xi * hi;
                o[2 * b + 1] += xr * hi + xi * hr;
            }
        }
    };

    mac (direct, directIn);
    if (s.cross && numCh == 2)
        mac (crossP, crossIn);
}

void PartitionedConvolver::processPartition() noexcept
{
    const int bins = P + 1;

    // 1. Input spectra: FFT of [previous block | current block] into the FDL head.
    head = (head + 1) % maxPartitions;
    for (int ch = 0; ch < numCh; ++ch)
    {
        std::copy (prevIn[(size_t) ch].begin(), prevIn[(size_t) ch].end(), fftBuf.begin());
        std::copy (inBlock[(size_t) ch].begin(), inBlock[(size_t) ch].end(), fftBuf.begin() + P);
        std::fill (fftBuf.begin() + 2 * P, fftBuf.end(), 0.0f);
        fft->performRealOnlyForwardTransform (fftBuf.data(), true);
        Complex* dst = fdl[(size_t) ch].data() + (size_t) head * (size_t) bins;
        for (int k = 0; k < bins; ++k)
            dst[k] = { fftBuf[(size_t) (2 * k)], fftBuf[(size_t) (2 * k + 1)] };
        std::swap (prevIn[(size_t) ch], inBlock[(size_t) ch]);
    }

    // 2. New IR waiting? Then crossfade old -> new over this partition.
    const bool swapNow = spareState.load (std::memory_order_acquire) == 1;
    const int act = active.load (std::memory_order_relaxed);
    const auto& cur = slots[(size_t) act];
    const auto& nxt = slots[(size_t) (1 - act)];

    auto inverse = [this, bins] (const std::vector<Complex>& spec, std::vector<float>& buf)
    {
        for (int k = 0; k < bins; ++k)
        {
            buf[(size_t) (2 * k)] = spec[(size_t) k].real();
            buf[(size_t) (2 * k + 1)] = spec[(size_t) k].imag();
        }
        std::fill (buf.begin() + 2 * bins, buf.end(), 0.0f);
        fft->performRealOnlyInverseTransform (buf.data());
    };

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& out = outBlock[(size_t) ch];
        if (cur.numPartitions > 0)
        {
            accumulate (cur, ch, acc.data());
            inverse (acc, fftBuf);
            std::copy (fftBuf.begin() + P, fftBuf.begin() + 2 * P, out.begin());   // overlap-save: keep 2nd half
        }
        else
        {
            std::fill (out.begin(), out.end(), 0.0f);
        }

        if (swapNow && nxt.numPartitions > 0)
        {
            accumulate (nxt, ch, acc2.data());
            inverse (acc2, fftBuf2);
            const float inc = 1.0f / (float) P;
            for (int i = 0; i < P; ++i)
            {
                const float t = (float) (i + 1) * inc;
                out[(size_t) i] += t * (fftBuf2[(size_t) (P + i)] - out[(size_t) i]);
            }
        }
    }

    if (swapNow)
    {
        active.store (1 - act, std::memory_order_release);
        spareState.store (0, std::memory_order_release);   // old slot is now the free spare
    }
}

void PartitionedConvolver::process (float* const* io, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, numCh);
    int done = 0;
    while (done < numSamples)
    {
        const int chunk = std::min (numSamples - done, P - pos);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* x = io[ch] + done;
            float* in = inBlock[(size_t) ch].data() + pos;
            const float* out = outBlock[(size_t) ch].data() + pos;
            for (int i = 0; i < chunk; ++i)
            {
                in[i] = x[i];
                x[i] = out[i];
            }
        }
        pos += chunk;
        done += chunk;
        if (pos == P)
        {
            processPartition();
            pos = 0;
        }
    }
}
} // namespace tg::dsp
