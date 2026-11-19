#include "IirEqEngine.h"

namespace tg::dsp
{
using params::Placement;

void IirEqEngine::prepare (double sampleRate, int maxBlockSize, int)
{
    maxBlock = maxBlockSize;
    detMainMS.setSize (2, maxBlockSize);
    detSideMS.setSize (2, maxBlockSize);
    for (auto& b : bands)
        b.prepare (sampleRate);
}

void IirEqEngine::setSampleRate (double sampleRate)
{
    for (auto& b : bands)
        b.setSampleRate (sampleRate);
}

void IirEqEngine::reset() noexcept
{
    for (auto& b : bands)
        b.reset();
}

void IirEqEngine::setMethod (DesignMethod m) noexcept
{
    for (auto& b : bands)
        b.setMethod (m);
}

void IirEqEngine::setTargets (const std::array<params::BandSnapshot, params::kNumBands>& snaps, bool deltaOnly) noexcept
{
    for (size_t i = 0; i < bands.size(); ++i)
    {
        bands[i].setTarget (snaps[i], deltaOnly);
        placement[i] = snaps[i].placement;
        source[i] = snaps[i].detSource;
    }
}

void IirEqEngine::toMidSide (float* l, float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        const float s = 0.5f * (l[i] - r[i]);
        l[i] = m; r[i] = s;
    }
}

void IirEqEngine::toLeftRight (float* m, float* s, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float l = m[i] + s[i];
        const float r = m[i] - s[i];
        m[i] = l; s[i] = r;
    }
}

void IirEqEngine::process (juce::dsp::AudioBlock<float> main,
                           juce::dsp::AudioBlock<const float> detMain,
                           juce::dsp::AudioBlock<const float> detSide) noexcept
{
    const int n = (int) main.getNumSamples();
    const int numCh = std::min ((int) main.getNumChannels(), 2);
    if (n == 0 || numCh == 0)
        return;
    jassert (n <= maxBlock);

    const bool haveSide = detSide.getNumChannels() > 0 && (int) detSide.getNumSamples() >= n;
    const int sideCh = haveSide ? std::min ((int) detSide.getNumChannels(), 2) : 0;

    float* io[2] { main.getChannelPointer (0), main.getChannelPointer ((size_t) (numCh - 1)) };
    const float* dm[2] { detMain.getChannelPointer (0), detMain.getChannelPointer ((size_t) std::min (1, (int) detMain.getNumChannels() - 1)) };
    const float* ds[2] { nullptr, nullptr };
    if (haveSide)
    {
        ds[0] = detSide.getChannelPointer (0);
        ds[1] = detSide.getChannelPointer ((size_t) (sideCh - 1));
    }

    // ---------------- Mono ----------------
    if (numCh == 1)
    {
        for (size_t b = 0; b < bands.size(); ++b)
        {
            if (! bands[b].isAudible() || placement[b] == Placement::Side)
                continue;
            const bool ext = source[b] == params::DetSource::External && haveSide;
            bands[b].process (io, 1, n, ext ? ds : dm, ext ? sideCh : 1);
        }
        return;
    }

    // ---------------- Stereo ----------------
    bool msPrepared = false;
    bool inMS = false;

    for (size_t b = 0; b < bands.size(); ++b)
    {
        auto& band = bands[b];
        if (! band.isAudible())
            continue;

        const auto p = placement[b];
        const bool wantMS = p == Placement::Mid || p == Placement::Side;

        if (wantMS && ! msPrepared)
        {
            // Detector signals in M/S, computed once per block on demand.
            for (int ch = 0; ch < 2; ++ch)
                detMainMS.copyFrom (ch, 0, dm[ch], n);
            toMidSide (detMainMS.getWritePointer (0), detMainMS.getWritePointer (1), n);
            if (haveSide)
            {
                for (int ch = 0; ch < 2; ++ch)
                    detSideMS.copyFrom (ch, 0, ds[ch], n);
                toMidSide (detSideMS.getWritePointer (0), detSideMS.getWritePointer (1), n);
            }
            msPrepared = true;
        }

        if (wantMS != inMS)
        {
            if (wantMS) toMidSide (io[0], io[1], n);
            else        toLeftRight (io[0], io[1], n);
            inMS = wantMS;
        }

        const bool ext = source[b] == params::DetSource::External && haveSide;
        const float* detLR[2] { ext ? ds[0] : dm[0], ext ? ds[1] : dm[1] };
        const float* detMS[2] { ext ? detSideMS.getReadPointer (0) : detMainMS.getReadPointer (0),
                                ext ? detSideMS.getReadPointer (1) : detMainMS.getReadPointer (1) };

        switch (p)
        {
            case Placement::Stereo: band.process (io, 2, n, detLR, 2); break;
            case Placement::Left:   band.process (&io[0], 1, n, &detLR[0], 1); break;
            case Placement::Right:  band.process (&io[1], 1, n, &detLR[1], 1); break;
            case Placement::Mid:    band.process (&io[0], 1, n, &detMS[0], 1); break;
            case Placement::Side:   band.process (&io[1], 1, n, &detMS[1], 1); break;
        }
    }

    if (inMS)
        toLeftRight (io[0], io[1], n);
}
} // namespace tg::dsp
