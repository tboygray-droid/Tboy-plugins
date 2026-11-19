#pragma once

#include "params/Parameters.h"
#include <vector>

namespace tg::fit
{
/**
    Fits EQ bands to a target magnitude curve (dB on a frequency grid).
    Used by the draw-a-curve mode and by Match EQ.

    1. Greedy placement: put a band on the largest weighted residual; its width (Q) is taken
       from where the residual falls to half; near the spectrum edges a shelf is tried too.
    2. Levenberg-Marquardt refinement of (log f, gain, log Q) of all bands on the dB error.
*/
struct FitOptions
{
    int maxBands = 8;
    double toleranceDb = 0.4;   ///< stop adding bands when max |error| is below this
    int lmIterations = 25;
};

struct FitResult
{
    std::vector<params::BandSnapshot> bands;
    double maxErrorDb = 0.0;
    double rmsErrorDb = 0.0;
};

FitResult fitCurve (const std::vector<double>& freqs, const std::vector<double>& targetDb,
                    const std::vector<double>& weights, const FitOptions& options = {});

/** Fractional-octave smoothing of a dB curve on a log grid (used for Match EQ). */
std::vector<double> smoothOctaves (const std::vector<double>& freqs, const std::vector<double>& db, double fraction);
} // namespace tg::fit
