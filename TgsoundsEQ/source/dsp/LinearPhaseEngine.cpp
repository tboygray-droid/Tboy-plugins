#include "LinearPhaseEngine.h"
#include "FilterDesign.h"

namespace tg::dsp
{
using params::Placement;

int LinearPhaseEngine::tapsFor (params::LpQuality q, double sr) noexcept
{
    const int base = 2048 << (int) q;
    const int rateMul = sr > 100000.0 ? 4 : (sr > 50000.0 ? 2 : 1);
    return base * rateMul;
}

int LinearPhaseEngine::partitionFor (double sr) noexcept
{
    return sr > 100000.0 ? 1024 : (sr > 50000.0 ? 512 : 256);
}

void LinearPhaseEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    numCh = juce::jlimit (1, 2, numChannels);
    conv.prepare (partitionFor (fs), tapsFor (params::LpQuality::Max, fs), numCh);
    fallbackDelay.prepare (numCh, latencyFor (params::LpQuality::Max, fs) + 1);
    dryCopy.setSize (numCh, maxBlockSize);
}

void LinearPhaseEngine::reset() noexcept
{
    conv.reset();
    fallbackDelay.reset();
}

void LinearPhaseEngine::process (float* const* io, int numChannels, int n, params::LpQuality quality) noexcept
{
    numChannels = std::min (numChannels, numCh);
    const int wantTaps = tapsFor (quality, fs);

    for (int ch = 0; ch < numChannels; ++ch)
        dryCopy.copyFrom (ch, 0, io[ch], n);

    conv.process (io, numChannels, n);

    // Keep the fallback delay running so switching to it is seamless in time.
    float* outs[2] { dryCopy.getWritePointer (0), dryCopy.getWritePointer (numCh - 1) };
    fallbackDelay.process (outs, outs, numChannels, n, latencyFor (quality, fs));

    if (conv.getActiveTaps() != wantTaps)
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (io[ch], dryCopy.getReadPointer (ch), n);
}

// =====================================================================================
ResponseMatrix analogMatrixAt (const std::array<params::BandSnapshot, params::kNumBands>& snaps, double hz, bool mono)
{
    ResponseMatrix M;
    for (const auto& s : snaps)
    {
        if (! s.isActive())
            continue;
        BandDesign d { s.shape, s.slope, s.freq, s.gain, s.q };
        const double m = std::abs (makeAnalog (d).response (hz));

        if (mono)
        {
            if (s.placement != Placement::Side)
                M.ll *= m;
            continue;
        }

        // Band matrix B in L/R basis, then M = B * M (bands apply in order).
        double b00 = 1, b01 = 0, b10 = 0, b11 = 1;
        switch (s.placement)
        {
            case Placement::Stereo: b00 = m; b11 = m; break;
            case Placement::Left:   b00 = m; break;
            case Placement::Right:  b11 = m; break;
            case Placement::Mid:    b00 = b11 = 0.5 * (m + 1); b01 = b10 = 0.5 * (m - 1); break;
            case Placement::Side:   b00 = b11 = 0.5 * (1 + m); b01 = b10 = 0.5 * (1 - m); break;
        }
        const ResponseMatrix o = M;
        M.ll = b00 * o.ll + b01 * o.rl;
        M.lr = b00 * o.lr + b01 * o.rr;
        M.rl = b10 * o.ll + b11 * o.rl;
        M.rr = b10 * o.lr + b11 * o.rr;
    }
    return M;
}

bool designLinearPhaseFir (const std::array<params::BandSnapshot, params::kNumBands>& snaps,
                           double fs, int taps, bool mono, std::array<std::vector<float>, 4>& fir)
{
    bool cross = false;
    if (! mono)
        for (const auto& s : snaps)
            if (s.isActive() && (s.placement == Placement::Mid || s.placement == Placement::Side))
                cross = true;

    const int N = taps;
    const int order = juce::roundToInt (std::log2 (N));
    juce::dsp::FFT fft (order);
    std::array<std::vector<float>, 4> spec;
    for (auto& s : spec)
        s.assign ((size_t) (2 * N), 0.0f);

    // Zero-phase magnitude samples on the FFT grid.
    for (int k = 0; k <= N / 2; ++k)
    {
        const double hz = std::max (1.0, k * fs / N);
        const auto M = analogMatrixAt (snaps, hz, mono);
        spec[0][(size_t) (2 * k)] = (float) M.ll;
        spec[1][(size_t) (2 * k)] = (float) M.lr;
        spec[2][(size_t) (2 * k)] = (float) M.rl;
        spec[3][(size_t) (2 * k)] = (float) M.rr;
    }

    // Blackman window over the full length keeps truncation ripple low (needed for brickwall cuts).
    std::vector<float> window ((size_t) N);
    for (int n = 0; n < N; ++n)
    {
        const double x = 2.0 * juce::MathConstants<double>::pi * n / N;
        window[(size_t) n] = (float) (0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x));
    }

    for (int p = 0; p < 4; ++p)
    {
        auto& f = fir[(size_t) p];
        f.assign ((size_t) N, 0.0f);
        const bool needed = p == 0 || p == 3 ? ! (mono && p == 3) : cross;
        if (! needed)
            continue;
        fft.performRealOnlyInverseTransform (spec[(size_t) p].data());
        // Circular shift by N/2 turns the zero-phase response into a causal linear-phase one.
        for (int n = 0; n < N; ++n)
            f[(size_t) n] = spec[(size_t) p][(size_t) ((n + N / 2) % N)] * window[(size_t) n];
    }
    return cross;
}

float estimateAutoGainDb (const std::array<params::BandSnapshot, params::kNumBands>& snaps, bool mono)
{
    constexpr int numPoints = 96;
    double sum = 0.0;
    for (int i = 0; i < numPoints; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (i + 0.5) / numPoints);
        const auto M = analogMatrixAt (snaps, hz, mono);
        // Average output power for uncorrelated equal-level L/R input.
        const double p = mono ? M.ll * M.ll
                              : 0.5 * (M.ll * M.ll + M.lr * M.lr + M.rl * M.rl + M.rr * M.rr);
        sum += 10.0 * std::log10 (std::max (1e-12, p));
    }
    return (float) juce::jlimit (-36.0, 36.0, -sum / numPoints);
}
} // namespace tg::dsp
