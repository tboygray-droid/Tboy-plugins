#include "SpectrumAnalyzer.h"

namespace tg::analyzer
{
void SpectrumChannel::configure (int fftOrder, double sampleRate)
{
    fs = sampleRate;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    fftSize = 1 << fftOrder;
    hop = fftSize / 4;   // 75 % overlap
    filled = 0;
    sinceLastFrame = 0;

    window.resize ((size_t) fftSize);
    double sum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        sum += window[(size_t) i];
    }
    // Normalise so a full-scale sine reads 0 dB (coherent gain of the window, one-sided spectrum).
    const float norm = (float) (2.0 / sum);
    for (auto& w : window) w *= norm;

    ring.assign ((size_t) fftSize, 0.0f);
    frame.assign ((size_t) (2 * fftSize), 0.0f);
    const size_t bins = (size_t) fftSize / 2 + 1;
    rawDb.assign (bins, -200.0f);
    smoothDb.assign (bins, -200.0f);
    peakDb.assign (bins, -200.0f);
    scratch.assign ((size_t) fftSize, 0.0f);
    capturePower.assign (bins, 0.0);
    captureFrames = 0;
    lastTilt = -1.0f;
}

void SpectrumChannel::computeFrame (const AnalyzerSettings& s)
{
    // Unroll the circular ring (oldest sample first) and window it.
    for (int i = 0; i < fftSize; ++i)
        frame[(size_t) i] = ring[(size_t) ((filled + i) % fftSize)] * window[(size_t) i];
    std::fill (frame.begin() + fftSize, frame.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (frame.data(), true);

    if (lastTilt != s.tiltDbPerOct)
    {
        lastTilt = s.tiltDbPerOct;
        tiltTable.resize (rawDb.size());
        for (size_t k = 0; k < tiltTable.size(); ++k)
        {
            const double hz = std::max (1.0, (double) k * fs / fftSize);
            tiltTable[k] = (float) (s.tiltDbPerOct * std::log2 (hz / 1000.0));
        }
    }

    for (size_t k = 0; k < rawDb.size(); ++k)
        rawDb[k] = juce::Decibels::gainToDecibels (frame[k], -200.0f) + tiltTable[k];

    if (capturing)
    {
        for (size_t k = 0; k < capturePower.size(); ++k)
            capturePower[k] += (double) frame[k] * frame[k];
        ++captureFrames;
    }
}

bool SpectrumChannel::process (AnalyzerFifo& fifo, const AnalyzerSettings& s, double elapsed)
{
    if (fftSize == 0)
        return false;

    bool newFrame = false;
    int n;
    while ((n = fifo.pull (scratch.data(), std::min (hop - sinceLastFrame, (int) scratch.size()))) > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) filled] = scratch[(size_t) i];
            filled = (filled + 1) % fftSize;
        }
        sinceLastFrame += n;
        if (sinceLastFrame >= hop)
        {
            sinceLastFrame = 0;
            computeFrame (s);
            newFrame = true;
        }
    }

    // Ballistics: instant attack, release in dB/s depending on speed.
    constexpr float releaseRates[] { 6.0f, 15.0f, 35.0f, 80.0f, 200.0f };
    const float fall = releaseRates[juce::jlimit (0, 4, s.speed)] * (float) elapsed;
    for (size_t k = 0; k < smoothDb.size(); ++k)
    {
        const float target = rawDb[k];
        float& v = smoothDb[k];
        v = target > v ? target : std::max (target, v - fall);
        if (s.peakHold)
            peakDb[k] = std::max (peakDb[k], v);
    }
    return newFrame;
}

//==============================================================================
AnalyzerHub::AnalyzerHub() : juce::Thread ("TgsoundsEQ analyzer") {}
AnalyzerHub::~AnalyzerHub() { setActive (false); }

void AnalyzerHub::setActive (bool shouldRun)
{
    if (shouldRun == active.load())
        return;
    if (shouldRun)
    {
        for (auto& f : fifos) f.clear();
        active = true;
        startThread (juce::Thread::Priority::low);
    }
    else
    {
        active = false;
        stopThread (1000);
    }
}

void AnalyzerHub::setSettings (const AnalyzerSettings& s)
{
    const juce::ScopedLock sl (lock);
    if (s.peakHold && ! settings.peakHold)
        resetPeakRequest = true;
    settings = s;
}

AnalyzerSettings AnalyzerHub::getSettings() const
{
    const juce::ScopedLock sl (lock);
    return settings;
}

void AnalyzerHub::resetPeaks()
{
    const juce::ScopedLock sl (lock);
    resetPeakRequest = true;
}

void AnalyzerHub::startCapture (Source src)
{
    const juce::ScopedLock sl (lock);
    channels[(size_t) src].startCapture();
}

void AnalyzerHub::stopCapture (Source src)
{
    const juce::ScopedLock sl (lock);
    channels[(size_t) src].stopCapture();
}

bool AnalyzerHub::isCapturing (Source src) const
{
    const juce::ScopedLock sl (lock);
    return channels[(size_t) src].isCapturing();
}

int AnalyzerHub::getCaptureFrames (Source src) const
{
    const juce::ScopedLock sl (lock);
    return channels[(size_t) src].getCaptureFrames();
}

bool AnalyzerHub::getCapture (Source src, const std::vector<double>& freqs, std::vector<double>& dbOut) const
{
    const juce::ScopedLock sl (lock);
    const auto& ch = channels[(size_t) src];
    if (ch.getCaptureFrames() == 0)
        return false;
    const auto& pw = ch.getCapturePower();
    const double binHz = ch.getSampleRate() / ch.getFftSize();
    dbOut.resize (freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        // Average power over the bins within +-1/12 octave (stable estimate at high frequencies).
        const double lo = freqs[i] / std::pow (2.0, 1.0 / 24.0), hi = freqs[i] * std::pow (2.0, 1.0 / 24.0);
        int k0 = (int) std::floor (lo / binHz), k1 = (int) std::ceil (hi / binHz);
        k0 = juce::jlimit (1, (int) pw.size() - 1, k0);
        k1 = juce::jlimit (k0, (int) pw.size() - 1, k1);
        double sum = 0;
        for (int k = k0; k <= k1; ++k) sum += pw[(size_t) k];
        const double mean = sum / (k1 - k0 + 1) / ch.getCaptureFrames();
        dbOut[i] = 10.0 * std::log10 (std::max (1e-20, mean));
    }
    return true;
}

void AnalyzerHub::processNow (double elapsed)
{
    const juce::ScopedLock sl (lock);
    const int order = AnalyzerSettings::fftOrderFor (settings.resolution);
    const double sr = sampleRate.load();
    if (order != configuredOrder || sr != configuredRate)
    {
        for (auto& c : channels) c.configure (order, sr);
        configuredOrder = order;
        configuredRate = sr;
    }
    if (resetPeakRequest)
    {
        for (auto& c : channels) c.resetPeak();
        resetPeakRequest = false;
    }
    for (size_t i = 0; i < channels.size(); ++i)
        channels[i].process (fifos[i], settings, elapsed);
}

void AnalyzerHub::run()
{
    auto last = juce::Time::getMillisecondCounterHiRes();
    while (! threadShouldExit())
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        processNow ((now - last) * 0.001);
        last = now;
        wait (16);
    }
}

bool AnalyzerHub::fillCurve (Source src, bool peak, float fMin, float fMax, float* out, int numPoints) const
{
    const juce::ScopedLock sl (lock);
    const auto& ch = channels[(size_t) src];
    if (ch.getFftSize() == 0 || numPoints < 2)
        return false;
    const auto& db = peak ? ch.getPeak() : ch.getDb();
    const double binHz = ch.getSampleRate() / ch.getFftSize();
    const int maxBin = (int) db.size() - 1;

    const double ratio = std::log (fMax / fMin);
    for (int i = 0; i < numPoints; ++i)
    {
        const double f0 = fMin * std::exp (ratio * (i - 0.5) / (numPoints - 1));
        const double f1 = fMin * std::exp (ratio * (i + 0.5) / (numPoints - 1));
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        float v;
        if (b1 - b0 < 1.0)
        {
            // Fewer than one bin per point: interpolate at the centre frequency.
            const double bc = std::clamp (0.5 * (b0 + b1), 0.0, (double) maxBin);
            const int k = std::min ((int) bc, maxBin - 1);
            const float t = (float) (bc - k);
            v = db[(size_t) k] + t * (db[(size_t) k + 1] - db[(size_t) k]);
        }
        else
        {
            v = -200.0f;
            for (int k = std::max (0, (int) std::ceil (b0)); k <= std::min (maxBin, (int) b1); ++k)
                v = std::max (v, db[(size_t) k]);
        }
        out[i] = v;
    }
    return true;
}
} // namespace tg::analyzer
