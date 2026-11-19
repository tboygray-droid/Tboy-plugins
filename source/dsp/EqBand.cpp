#include "EqBand.h"

namespace tg::dsp
{
using params::Shape;

void EqBand::prepare (double sampleRate)
{
    setSampleRate (sampleRate);
}

void EqBand::setSampleRate (double sampleRate)
{
    fs = sampleRate;
    controlInterval = juce::jlimit (32, kMaxControlInterval, (int) juce::nextPowerOfTwo ((int) (32.0 * fs / 48000.0 + 0.5)));
    const double rampSeconds = 0.05;
    freq.reset (fs, rampSeconds);
    q.reset (fs, rampSeconds);
    gain.reset (fs, rampSeconds);
    mix.reset (fs, 0.02);
    detFreq = -1.0;
    reset();
}

void EqBand::reset() noexcept
{
    for (auto& ch : state)
        for (auto& s : ch)
            s.reset();
    detState.reset();
    envelope = 0.0;
    dynGainDb = 0.0;
    needsDesign = true;
    rampActive = false;
    freq.setCurrentAndTargetValue (freq.getTargetValue() > 0 ? freq.getTargetValue() : 1000.0);
    q.setCurrentAndTargetValue (q.getTargetValue() > 0 ? q.getTargetValue() : 1.0);
    gain.setCurrentAndTargetValue (gain.getTargetValue());
    mix.setCurrentAndTargetValue (mix.getTargetValue());
}

void EqBand::setTarget (const params::BandSnapshot& s, bool deltaOnly) noexcept
{
    const bool active = s.isActive() && (! deltaOnly || (s.dynOn && params::shapeHasGain (s.shape)));
    mix.setTargetValue (active ? 1.0 : 0.0);

    // When a band is silent we snap its parameters, so it fades in at the right place.
    const bool snap = mix.getCurrentValue() <= 0.0 && ! mix.isSmoothing();

    auto setOrSnap = [snap] (auto& sm, double v)
    {
        if (snap) sm.setCurrentAndTargetValue (v);
        else      sm.setTargetValue (v);
    };

    setOrSnap (freq, std::clamp ((double) s.freq, (double) params::kFreqMin, (double) params::kFreqMax));
    setOrSnap (q,    std::clamp ((double) s.q, (double) params::kQMin, (double) params::kQMax));
    setOrSnap (gain, deltaOnly ? 0.0 : (double) s.gain);

    if (s.shape != shape || s.slope != slope || deltaOnly != deltaMode)
    {
        shape = s.shape;
        slope = s.slope;
        deltaMode = deltaOnly;
        needsDesign = true;
        detFreq = -1.0;
    }

    dynOn     = s.dynOn && params::shapeHasGain (s.shape);
    threshold = s.threshold;
    range     = s.range;
    ratio     = std::max (1.0f, s.ratio);
    dynSign   = s.dynDir == params::DynDir::Up ? 1.0f : -1.0f;
    attCoef   = std::exp (-1.0 / (std::max (0.05, (double) s.attackMs) * 0.001 * fs));
    relCoef   = std::exp (-1.0 / (std::max (0.5, (double) s.releaseMs) * 0.001 * fs));
    if (! dynOn && dynGainDb != 0.0)
        needsDesign = true;   // let the dynamic offset return to zero
}

void EqBand::designTarget() noexcept
{
    BandDesign d;
    d.shape  = shape;
    d.slope  = slope;
    d.freq   = freq.getCurrentValue();
    d.q      = q.getCurrentValue();
    d.gainDb = std::clamp (gain.getCurrentValue() + dynGainDb, -48.0, 48.0);
    lastDesign = d;

    const auto cascade = designDigital (d, fs, method);
    const bool structureChanged = cascade.numSections != numSections;

    from = to;
    scalarFrom = scalarTo;
    for (int i = 0; i < cascade.numSections; ++i)
        to[(size_t) i] = SvfCoeffs::fromBiquad (cascade.sections[(size_t) i]);
    scalarTo = cascade.scalar;

    if (structureChanged)
    {
        // A different number of sections cannot be interpolated; start the new sections clean.
        for (int i = std::min (numSections, cascade.numSections); i < kMaxSections; ++i)
            for (auto& ch : state)
                ch[(size_t) i].reset();
        from = to;
        scalarFrom = scalarTo;
        numSections = cascade.numSections;
    }
    rampActive = true;
}

void EqBand::runDetector (const float* const* det, int numDet, int offset, int len) noexcept
{
    // (Re)design the band-limited detector filter when the frequency moves noticeably.
    const double f = freq.getCurrentValue();
    if (detFreq < 0.0 || std::abs (f / detFreq - 1.0) > 0.01)
    {
        detFreq = f;
        AnalogSection a;
        a.w0 = 2.0 * 3.14159265358979323846 * f;
        const double dq = std::clamp (q.getCurrentValue(), 0.3, 8.0);
        switch (shape)
        {
            case Shape::LowShelf:  a.n2 = 0; a.n1 = 0; a.n0 = 1; a.d2 = 1; a.d1 = 1.4142; a.d0 = 1; break; // low-pass
            case Shape::HighShelf: a.n2 = 1; a.n1 = 0; a.n0 = 0; a.d2 = 1; a.d1 = 1.4142; a.d0 = 1; break; // high-pass
            default:               a.n2 = 0; a.n1 = 1.0 / dq; a.n0 = 0; a.d2 = 1; a.d1 = 1.0 / dq; a.d0 = 1; break; // band-pass
        }
        detFilter = SvfRuntime::from (SvfCoeffs::fromBiquad (discretise (a, fs, DesignMethod::Bilinear)));
    }

    const float scale = numDet > 1 ? 0.5f : 1.0f;
    double env = envelope;
    for (int i = 0; i < len; ++i)
    {
        double x = det[0][offset + i];
        if (numDet > 1)
            x += det[1][offset + i];
        const double y = std::abs (detFilter.tick (x * scale, detState));
        env = y > env ? y + attCoef * (env - y) : y + relCoef * (env - y);
    }
    envelope = env < 1e-12 ? 0.0 : env;
    flushDenormals (detState);
}

void EqBand::controlUpdate (int len, const float* const* det, int numDet, int offset) noexcept
{
    const bool smoothing = freq.isSmoothing() || q.isSmoothing() || gain.isSmoothing();
    freq.skip (len);
    q.skip (len);
    gain.skip (len);

    bool redesign = needsDesign || smoothing;

    if (dynOn && det != nullptr)
    {
        runDetector (det, numDet, offset, len);
        const double levelDb = 20.0 * std::log10 (std::max (1e-9, envelope));
        const double over = levelDb - threshold;
        const double amount = over > 0.0 ? std::min ((double) range, over * (1.0 - 1.0 / ratio)) : 0.0;
        const double newGain = dynSign * amount;
        if (std::abs (newGain - dynGainDb) > 1e-4)
        {
            dynGainDb = newGain;
            redesign = true;
        }
    }
    else if (dynGainDb != 0.0)
    {
        // Glide back to zero smoothly (release-like) when dynamics are switched off.
        dynGainDb *= 0.9;
        if (std::abs (dynGainDb) < 1e-3) dynGainDb = 0.0;
        redesign = true;
    }
    dynGainForGui.store ((float) dynGainDb, std::memory_order_relaxed);

    if (redesign)
    {
        designTarget();
        needsDesign = false;
    }
    else if (rampActive)
    {
        from = to;
        scalarFrom = scalarTo;
        rampActive = false;
    }
}

void EqBand::process (float* const* channels, int numChannels, int numSamples,
                      const float* const* detector, int numDetChannels) noexcept
{
    if (! isAudible())
        return;

    numChannels = std::min (numChannels, kMaxChannels);
    const float* const* det = detector != nullptr ? detector : const_cast<const float* const*> (channels);
    const int numDet = detector != nullptr ? numDetChannels : numChannels;

    for (int pos = 0; pos < numSamples; pos += controlInterval)
    {
        const int len = std::min (controlInterval, numSamples - pos);
        const double mixStart = mix.getCurrentValue();
        controlUpdate (len, det, numDet, pos);
        mix.skip (len);
        const double mixEnd = mix.getCurrentValue();

        if (mixStart <= 0.0 && mixEnd <= 0.0)
        {
            // Fully faded out: keep the filter clean for the next fade-in.
            for (auto& ch : state) for (auto& s : ch) s.reset();
            continue;
        }

        const double invLen = 1.0 / len;
        for (int c = 0; c < numChannels; ++c)
        {
            float* x = channels[c] + pos;
            auto& st = state[(size_t) c];

            // Buffer the dry signal for the wet/dry fade (only needed while mixing).
            const bool fading = mixStart < 1.0 || mixEnd < 1.0;
            float dry[kMaxControlInterval];
            if (fading)
                std::copy (x, x + len, dry);

            for (int s = 0; s < numSections; ++s)
            {
                const auto& c0 = from[(size_t) s];
                const auto& c1 = to[(size_t) s];
                auto& ss = st[(size_t) s];

                if (! rampActive)
                {
                    const auto rt = SvfRuntime::from (c1);
                    for (int i = 0; i < len; ++i)
                        x[i] = (float) rt.tick (x[i], ss);
                }
                else
                {
                    for (int i = 0; i < len; ++i)
                    {
                        const double t = (i + 1) * invLen;
                        SvfCoeffs ci;
                        ci.g  = c0.g  + t * (c1.g  - c0.g);
                        ci.k  = c0.k  + t * (c1.k  - c0.k);
                        ci.m0 = c0.m0 + t * (c1.m0 - c0.m0);
                        ci.m1 = c0.m1 + t * (c1.m1 - c0.m1);
                        ci.m2 = c0.m2 + t * (c1.m2 - c0.m2);
                        x[i] = (float) SvfRuntime::from (ci).tick (x[i], ss);
                    }
                }
                flushDenormals (ss);
            }

            // Overall scalar gain (tilt shelves) and the wet/dry mix.
            for (int i = 0; i < len; ++i)
            {
                const double t = (i + 1) * invLen;
                const double sc = rampActive ? scalarFrom + t * (scalarTo - scalarFrom) : scalarTo;
                double y = x[i] * sc;
                if (fading)
                {
                    const double m = mixStart + t * (mixEnd - mixStart);
                    y = dry[i] + m * (y - dry[i]);
                }
                x[i] = (float) y;
            }
        }
    }
}
} // namespace tg::dsp
