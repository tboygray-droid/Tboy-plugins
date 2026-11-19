#include "EqDesigner.h"

namespace tg::dsp
{
EqDesigner::EqDesigner (const params::ParamRefs& r, LinearPhaseEngine& e)
    : juce::Thread ("TgsoundsEQ designer"), refs (r), lp (e) {}

EqDesigner::~EqDesigner() { stop(); }

void EqDesigner::start (double sampleRate, bool mono)
{
    stop();
    fs = sampleRate;
    isMono = mono;
    lastSignature.clear();
    lastTaps = 0;
    const int P = LinearPhaseEngine::partitionFor (fs);
    partitionFft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (2 * P)));
    startThread (juce::Thread::Priority::low);
}

void EqDesigner::stop()
{
    stopThread (2000);
}

void EqDesigner::run()
{
    while (! threadShouldExit())
    {
        designPass (false);
        wait (25);
    }
}

bool EqDesigner::designPass (bool force)
{
    const juce::ScopedLock sl (passLock);

    std::array<params::BandSnapshot, params::kNumBands> snaps;
    std::vector<float> sig;
    sig.reserve (params::kNumBands * 8 + 4);
    for (int b = 0; b < params::kNumBands; ++b)
    {
        snaps[(size_t) b] = refs.snapshot (b);
        const auto& s = snaps[(size_t) b];
        sig.insert (sig.end(), { (float) s.isActive(), s.freq, s.gain, s.q, (float) s.shape,
                                 (float) s.slope, (float) s.placement });
    }
    const auto mode = (params::Mode) (int) refs.global.mode->load();
    const auto quality = (params::LpQuality) (int) refs.global.lpQuality->load();
    sig.push_back ((float) mode);
    sig.push_back ((float) quality);
    sig.push_back (refs.global.autoGain->load());

    const int taps = LinearPhaseEngine::tapsFor (quality, fs);
    auto& conv = lp.getConvolver();
    const bool lpMode = mode == params::Mode::LinearPhase;
    const bool irOutOfDate = lpMode && (sig != lastSignature || conv.getActiveTaps() != taps || lastTaps != taps);

    if (! force && sig == lastSignature && ! irOutOfDate)
        return false;

    autoGain.store (refs.global.autoGain->load() > 0.5f ? estimateAutoGainDb (snaps, isMono) : 0.0f);

    if (lpMode)
    {
        auto* slot = conv.beginWrite();
        if (slot == nullptr)
            return false;   // audio thread hasn't consumed the previous IR yet; retry next pass

        std::array<std::vector<float>, 4> fir;
        const bool cross = designLinearPhaseFir (snaps, fs, taps, isMono, fir);

        const int P = conv.getPartitionSize();
        const int bins = conv.getNumBins();
        const int numParts = (taps + P - 1) / P;
        for (int p = 0; p < 4; ++p)
        {
            const bool needed = (p == 0 || p == 3) || cross;
            for (int k = 0; k < numParts; ++k)
            {
                auto* dest = slot->spectra[(size_t) p].data() + (size_t) k * (size_t) bins;
                if (needed)
                    conv.transformPartition (fir[(size_t) p].data() + k * P, std::min (P, taps - k * P),
                                             dest, *partitionFft, scratch);
                else
                    std::fill (dest, dest + bins, PartitionedConvolver::Complex {});
            }
        }
        slot->numPartitions = numParts;
        slot->taps = taps;
        slot->cross = cross;
        conv.endWrite();
        lastTaps = taps;
    }

    lastSignature = std::move (sig);
    return true;
}
} // namespace tg::dsp
