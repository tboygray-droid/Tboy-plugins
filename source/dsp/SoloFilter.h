#pragma once

#include "Svf.h"

namespace tg::dsp
{
/**
    "Listen" filter for soloing a band: lets through only the region the band acts on
    (band-pass at f0 / Q for bells etc., low-pass for low shelves / low cuts, high-pass for
    high shelves / high cuts). Fades in/out to avoid clicks.
*/
class SoloFilter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        mix.reset (fs, 0.03);
        mix.setCurrentAndTargetValue (0.0);
        for (auto& s : st) s.reset();
    }

    void setTarget (bool on, const params::BandSnapshot& s) noexcept
    {
        mix.setTargetValue (on ? 1.0 : 0.0);
        if (! on)
            return;
        AnalogSection a;
        a.w0 = 2.0 * 3.14159265358979323846 * s.freq;
        const double q = std::clamp ((double) s.q, 0.2, 20.0);
        using params::Shape;
        switch (s.shape)
        {
            case Shape::LowShelf: case Shape::LowCut:     // hear the low region the band acts on
                a.n2 = 0; a.n1 = 0; a.n0 = 1; a.d2 = 1; a.d1 = 1.41421356; a.d0 = 1; break;
            case Shape::HighShelf: case Shape::HighCut:   // hear the high region
                a.n2 = 1; a.n1 = 0; a.n0 = 0; a.d2 = 1; a.d1 = 1.41421356; a.d0 = 1; break;
            default:                                      // band-pass at f0 with the band's Q
                a.n2 = 0; a.n1 = 1.0 / q; a.n0 = 0; a.d2 = 1; a.d1 = 1.0 / q; a.d0 = 1; break;
        }
        rt = SvfRuntime::from (SvfCoeffs::fromBiquad (discretise (a, fs, DesignMethod::Bilinear)));
    }

    void process (float* const* io, int numCh, int n) noexcept
    {
        if (mix.getCurrentValue() <= 0.0 && mix.getTargetValue() <= 0.0)
            return;
        for (int i = 0; i < n; ++i)
        {
            const double m = mix.getNextValue();
            for (int c = 0; c < std::min (numCh, 2); ++c)
            {
                const double x = io[c][i];
                io[c][i] = (float) (x + m * (rt.tick (x, st[(size_t) c]) - x));
            }
        }
        for (auto& s : st) flushDenormals (s);
    }

private:
    double fs = 48000.0;
    juce::SmoothedValue<double> mix;
    SvfRuntime rt;
    std::array<SvfState, 2> st {};
};
} // namespace tg::dsp
