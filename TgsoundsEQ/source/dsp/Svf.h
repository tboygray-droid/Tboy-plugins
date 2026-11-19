#pragma once

#include "FilterDesign.h"
#include <cmath>

/**
    Trapezoidal (TPT) state-variable filter after Andrew Simper (Cytomic,
    "SvfLinearTrapOptimised2"). Any stable digital biquad can be mapped *exactly*
    onto this structure:

        y = m0 * v0 + m1 * band + m2 * low

    The denominator of the SVF is (1 + gk + g^2) + 2(g^2 - 1) z^-1 + (1 - gk + g^2) z^-2.
    Matching it to 1 + a1 z^-1 + a2 z^-2 gives

        g^2 = (1 + a1 + a2) / (1 - a1 + a2),   k = 2 (1 - a2) / (g (1 - a1 + a2))

    and the mix gains follow from evaluating the numerator at z = 1, z = -1 and
    the z^0 / z^-2 coefficient difference (see fromBiquad()).

    Why: the TPT SVF stays stable for *any* positive g, k, so ramping (g, k, m0..m2)
    per sample keeps the filter stable and click-free under fast modulation, which a
    direct-form biquad cannot guarantee.
*/
namespace tg::dsp
{
struct SvfCoeffs
{
    double g = 0.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;

    static SvfCoeffs identity() noexcept { return {}; }

    static SvfCoeffs fromBiquad (const Biquad& b) noexcept
    {
        SvfCoeffs c;
        const double den = 1.0 - b.a1 + b.a2;      // A(z = -1), > 0 for stable filters
        const double num = 1.0 + b.a1 + b.a2;      // A(z =  1), > 0 for stable filters
        if (den <= 1e-300 || num <= 0.0)
            return identity();

        const double D = 4.0 / den;                // 1 + gk + g^2
        c.g = std::sqrt (num / den);
        if (c.g < 1e-12)
            return identity();
        c.k = 2.0 * (1.0 - b.a2) / (c.g * den);

        c.m0 = D * (b.b0 - b.b1 + b.b2) * 0.25;                          // from z = -1
        c.m2 = D * (b.b0 + b.b1 + b.b2) / (4.0 * c.g * c.g) - c.m0;      // from z = +1
        c.m1 = (D * (b.b0 - b.b2) - c.m0 * D * (1.0 - b.a2)) / (2.0 * c.g);
        return c;
    }

    bool isIdentity() const noexcept { return m0 == 1.0 && m1 == 0.0 && m2 == 0.0; }
};

struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;
    void reset() noexcept { ic1 = ic2 = 0.0; }
};

/** Precomputed per-sample coefficients for the TPT recursion. */
struct SvfRuntime
{
    double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;

    static SvfRuntime from (const SvfCoeffs& c) noexcept
    {
        SvfRuntime r;
        r.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
        r.a2 = c.g * r.a1;
        r.a3 = c.g * r.a2;
        r.m0 = c.m0; r.m1 = c.m1; r.m2 = c.m2;
        return r;
    }

    inline double tick (double v0, SvfState& s) const noexcept
    {
        const double v3 = v0 - s.ic2;
        const double v1 = a1 * s.ic1 + a2 * v3;
        const double v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0 * v1 - s.ic1;
        s.ic2 = 2.0 * v2 - s.ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }
};

/** Flush tiny state values to zero (denormal protection independent of FTZ flags). */
inline void flushDenormals (SvfState& s) noexcept
{
    constexpr double tiny = 1e-20;
    if (std::abs (s.ic1) < tiny) s.ic1 = 0.0;
    if (std::abs (s.ic2) < tiny) s.ic2 = 0.0;
}
} // namespace tg::dsp
