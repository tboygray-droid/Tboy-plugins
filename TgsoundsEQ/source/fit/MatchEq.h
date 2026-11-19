#pragma once

#include "CurveFitter.h"
#include "analyzer/SpectrumAnalyzer.h"
#include <algorithm>

namespace tg::fit
{
/**
    Match EQ: correction = reference - source (level-normalised, 1/3-octave smoothed,
    limited to +-maxCorrection dB), then fitted with EQ bands.
*/
struct MatchSettings
{
    int maxBands = 8;
    double amount = 1.0;          ///< 0..1 scales the correction
    double maxCorrectionDb = 12.0;
    double smoothingOctaves = 3;  ///< 1/N octave smoothing
};

inline std::vector<double> matchGrid()
{
    std::vector<double> f;
    for (int i = 0; i < 96; ++i)
        f.push_back (25.0 * std::pow (18000.0 / 25.0, i / 95.0));
    return f;
}

inline std::vector<double> matchCorrection (const std::vector<double>& freqs, const std::vector<double>& refDb,
                                            const std::vector<double>& srcDb, const MatchSettings& s)
{
    std::vector<double> diff (freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i)
        diff[i] = refDb[i] - srcDb[i];
    diff = smoothOctaves (freqs, diff, s.smoothingOctaves);

    // Level-normalise: a match EQ should change tone, not loudness. The median difference over
    // 100 Hz .. 10 kHz is used (robust: a local resonance does not shift the whole curve, a mean would).
    std::vector<double> mid;
    for (size_t i = 0; i < freqs.size(); ++i)
        if (freqs[i] >= 100 && freqs[i] <= 10000) mid.push_back (diff[i]);
    double offset = 0.0;
    if (! mid.empty())
    {
        std::nth_element (mid.begin(), mid.begin() + (long) mid.size() / 2, mid.end());
        offset = mid[mid.size() / 2];
    }
    for (auto& d : diff)
        d = std::clamp ((d - offset) * s.amount, -s.maxCorrectionDb, s.maxCorrectionDb);
    return diff;
}

inline FitResult computeMatch (const std::vector<double>& freqs, const std::vector<double>& refDb,
                               const std::vector<double>& srcDb, const MatchSettings& s)
{
    const auto target = matchCorrection (freqs, refDb, srcDb, s);
    FitOptions o;
    o.maxBands = s.maxBands;
    o.toleranceDb = 0.5;
    return fitCurve (freqs, target, {}, o);
}
} // namespace tg::fit
