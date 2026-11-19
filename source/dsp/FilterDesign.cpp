#include "FilterDesign.h"
#include <algorithm>
#include <cmath>

namespace tg::dsp
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double sqrt2inv = 0.70710678118654752440;

    double dbToAmp (double db) { return std::pow (10.0, db / 20.0); }

    AnalogSection section (double w0, double n2, double n1, double n0, double d2, double d1, double d0)
    {
        AnalogSection s;
        s.w0 = w0; s.n2 = n2; s.n1 = n1; s.n0 = n0; s.d2 = d2; s.d1 = d1; s.d0 = d0;
        return s;
    }

    // ---- Shelves --------------------------------------------------------------
    // 2nd order shelves are the RBJ analog prototypes with explicit Q.
    // 1st order shelves are exact with the half-gain point at w0.
    void addLowShelf (AnalogCascade& c, double w0, double gainDb, double q, bool firstOrder)
    {
        if (firstOrder)
        {
            const double g = std::sqrt (dbToAmp (gainDb));   // sqrt of linear gain
            c.add (section (w0, 0, 1, g, 0, 1, 1.0 / g));
            return;
        }
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        // A * (p^2 + sqrt(A)/Q p + A) / (A p^2 + sqrt(A)/Q p + 1)
        c.add (section (w0, A, A * sA / q, A * A, A, sA / q, 1));
    }

    void addHighShelf (AnalogCascade& c, double w0, double gainDb, double q, bool firstOrder)
    {
        if (firstOrder)
        {
            const double G = dbToAmp (gainDb), g = std::sqrt (G);
            c.add (section (w0, 0, G, g, 0, 1, g));
            return;
        }
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        // A * (A p^2 + sqrt(A)/Q p + 1) / (p^2 + sqrt(A)/Q p + A)
        c.add (section (w0, A * A, A * sA / q, A, 1, sA / q, A));
    }

    void addCut (AnalogCascade& c, double w0, Slope slope, double q, bool highPass)
    {
        std::array<double, kMaxSections> qs {};
        bool hasFirst = false;
        const int order = params::slopeOrder (slope);
        const int n = butterworthQs (order, qs, hasFirst);

        // The user Q scales the resonance of the highest-Q section (Q = 0.707 => pure Butterworth).
        const double qScale = slope == Slope::Brickwall ? 1.0 : q / sqrt2inv;

        for (int i = 0; i < n; ++i)
        {
            if (hasFirst && i == 0)
            {
                c.add (highPass ? section (w0, 0, 1, 0, 0, 1, 1) : section (w0, 0, 0, 1, 0, 1, 1));
                continue;
            }
            double qi = qs[(size_t) i];
            if (i == n - 1)
                qi *= qScale;
            c.add (highPass ? section (w0, 1, 0, 0, 1, 1.0 / qi, 1)
                            : section (w0, 0, 0, 1, 1, 1.0 / qi, 1));
        }
    }

    // Biquad helpers -------------------------------------------------------------
    // |B(e^jw)|^2 = B0*phi0 + B1*phi1 + B2*phi2 with phi0 = cos^2(w/2), phi1 = sin^2(w/2), phi2 = 4 phi0 phi1
    // (Vicanek, "Matched Second Order Digital Filters", 2016)
    struct Phi { double p0, p1, p2; };
    Phi phis (double w)
    {
        const double s = std::sin (w * 0.5), s2 = s * s;
        return { 1.0 - s2, s2, 4.0 * (1.0 - s2) * s2 };
    }

    double analogMag2 (const AnalogSection& s, double omega) { return std::norm (s.response (omega)); }
} // namespace

Biquad discretiseMatched (const AnalogSection& s, double fs);

// =====================================================================================
cd AnalogSection::response (double omega) const noexcept
{
    const cd p (0.0, omega / w0);
    return (n2 * p * p + n1 * p + n0) / (d2 * p * p + d1 * p + d0);
}

cd AnalogCascade::response (double hz) const noexcept
{
    cd h (scalar, 0.0);
    const double omega = 2.0 * pi * hz;
    for (int i = 0; i < numSections; ++i)
        h *= sections[(size_t) i].response (omega);
    return h;
}

double AnalogCascade::magnitudeDb (double hz) const noexcept
{
    return 20.0 * std::log10 (std::max (1e-30, std::abs (response (hz))));
}

cd Biquad::response (double w) const noexcept
{
    const cd z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

cd DigitalCascade::response (double hz, double sampleRate) const noexcept
{
    const double w = 2.0 * pi * hz / sampleRate;
    cd h (scalar, 0.0);
    for (int i = 0; i < numSections; ++i)
        h *= sections[(size_t) i].response (w);
    return h;
}

double DigitalCascade::magnitudeDb (double hz, double sampleRate) const noexcept
{
    return 20.0 * std::log10 (std::max (1e-30, std::abs (response (hz, sampleRate))));
}

// =====================================================================================
int butterworthQs (int order, std::array<double, kMaxSections>& qs, bool& hasFirstOrder)
{
    hasFirstOrder = (order % 2) == 1;
    int n = 0;
    if (hasFirstOrder)
        qs[(size_t) n++] = 0.0;

    // Pole angles from the negative real axis; sorted so the highest Q comes last.
    const int pairs = order / 2;
    for (int k = pairs - 1; k >= 0; --k)
    {
        const double theta = hasFirstOrder ? pi * (k + 1) / order
                                           : pi * (2 * k + 1) / (2.0 * order);
        qs[(size_t) n++] = 1.0 / (2.0 * std::cos (theta));
    }
    // Sort ascending (lowest Q first) -> highest-Q section last.
    std::sort (qs.begin() + (hasFirstOrder ? 1 : 0), qs.begin() + n);
    return n;
}

AnalogCascade makeAnalog (const BandDesign& d)
{
    AnalogCascade c;
    const double w0 = 2.0 * pi * d.freq;
    const double q = std::clamp (d.q, (double) params::kQMin, (double) params::kQMax);
    const bool firstOrderShelf = d.slope == Slope::dB6;

    switch (d.shape)
    {
        case Shape::Bell:
        {
            const double A = std::pow (10.0, d.gainDb / 40.0);
            c.add (section (w0, 1, A / q, 1, 1, 1.0 / (A * q), 1));
            break;
        }
        case Shape::LowShelf:  addLowShelf  (c, w0, d.gainDb, q, firstOrderShelf); break;
        case Shape::HighShelf: addHighShelf (c, w0, d.gainDb, q, firstOrderShelf); break;
        case Shape::LowCut:    addCut (c, w0, d.slope, q, true);  break;
        case Shape::HighCut:   addCut (c, w0, d.slope, q, false); break;
        case Shape::Notch:     c.add (section (w0, 1, 0, 1, 1, 1.0 / q, 1)); break;
        case Shape::BandPass:  c.add (section (w0, 0, 1.0 / q, 0, 1, 1.0 / q, 1)); break;

        case Shape::TiltShelf:
            // A high shelf of +G pivoting around f0: -G/2 below, +G/2 above.
            addHighShelf (c, w0, d.gainDb, q, firstOrderShelf);
            c.scalar = dbToAmp (-0.5 * d.gainDb);
            break;

        case Shape::FlatTilt:
        {
            // Constant-slope tilt across the audio band. Gain = total tilt between 20 Hz and 20 kHz
            // (~10 octaves), built from first-order high shelves spaced one octave apart.
            // The cascade is normalised to 0 dB at f0, so f0 is the pivot.
            const double perOctaveDb = d.gainDb / std::log2 (1000.0);
            for (int i = -1; i <= 10; ++i)
                addHighShelf (c, 2.0 * pi * 20.0 * std::pow (2.0, i + 0.5), perOctaveDb, sqrt2inv, true);
            c.scalar = 1.0 / std::abs (c.response (d.freq));
            break;
        }
    }
    return c;
}

// =====================================================================================
Biquad discretise (const AnalogSection& s, double fs, DesignMethod method)
{
    Biquad b;
    const double nyq = 0.5 * fs;

    if (method == DesignMethod::Bilinear)
    {
        // Pre-warp at the section's own corner so the corner frequency is exact.
        const double f0 = std::min (s.w0 / (2.0 * pi), nyq * 0.995);
        const double K = std::tan (pi * f0 / fs);

        if (s.isFirstOrder())
        {
            const double a0 = s.d1 + s.d0 * K;
            b.b0 = (s.n1 + s.n0 * K) / a0;
            b.b1 = (-s.n1 + s.n0 * K) / a0;
            b.a1 = (-s.d1 + s.d0 * K) / a0;
            return b;
        }
        const double K2 = K * K;
        const double a0 = s.d2 + s.d1 * K + s.d0 * K2;
        b.b0 = (s.n2 + s.n1 * K + s.n0 * K2) / a0;
        b.b1 = (2.0 * (s.n0 * K2 - s.n2)) / a0;
        b.b2 = (s.n2 - s.n1 * K + s.n0 * K2) / a0;
        b.a1 = (2.0 * (s.d0 * K2 - s.d2)) / a0;
        b.a2 = (s.d2 - s.d1 * K + s.d0 * K2) / a0;
        return b;
    }

    // ---------------- Matched design ----------------
    return discretiseMatched (s, fs);
}

// =====================================================================================
// Matched-analog ("Natural Phase") discretisation of a 2nd-order section.
//
// Two candidate denominators are built:
//   (a) impulse-invariant poles z = exp(sT)    - ideal for resonant poles
//   (b) a magnitude fit of |A|^2 to |D(jW)|^2  - better for heavily damped / real poles
// For each candidate the numerator is fitted in the magnitude-squared domain, which is
// linear in (B0, B1, B2): B0 / B1 pin DC / Nyquist exactly and B2 is a relative-error
// least-squares fit over a log grid. The candidate with the smaller max dB error wins.
// Both factorisations pick the larger root as leading coefficient -> minimum phase / stable.
// =====================================================================================
namespace
{
    struct Grid
    {
        static constexpr int n = 25;
        std::array<double, n + 1> w {}, target {};   // target = analog |H|^2
        std::array<double, n + 1> den {};            // analog |D|^2 (for the denominator fit)
        std::array<Phi, n + 1> phi {};               // phi0/1/2 at each grid point
    };

    /** Factor P(w) = C0 phi0 + C1 phi1 + C2 phi2 into a minimum-phase quadratic c0 + c1 z^-1 + c2 z^-2. */
    bool factorPower (double C0, double C1, double C2, double& c0, double& c1, double& c2)
    {
        const double s0 = std::sqrt (std::max (0.0, C0)), s1 = std::sqrt (std::max (0.0, C1));
        const double W = 0.5 * (s0 + s1);
        c1 = 0.5 * (s0 - s1);
        const double rad = W * W + C2;
        const bool exact = rad >= 0.0;
        c0 = 0.5 * (W + std::sqrt (std::max (0.0, rad)));
        c2 = W - c0;
        return exact;
    }

    double fitCoeff2 (const Grid& g, const double* target, double C0, double C1, double wm, double tm, double wmWeight)
    {
        double num = 0.0, den = 0.0;
        auto add = [&] (const Phi& p, double t, double weight)
        {
            const double wt = weight / std::max (1e-30, t * t);
            num += wt * p.p2 * (t - C0 * p.p0 - C1 * p.p1);
            den += wt * p.p2 * p.p2;
        };
        for (int i = 0; i <= Grid::n; ++i)
            add (g.phi[(size_t) i], target[i], 1.0);
        add (phis (wm), tm, wmWeight);
        return den > 0.0 ? num / den : 0.0;
    }

    /** |c0 + c1 z^-1 + c2 z^-2|^2 evaluated through the phi basis (no trig per call). */
    double powerOf (double c0, double c1, double c2, const Phi& p)
    {
        const double s0 = c0 + c1 + c2, s1 = c0 - c1 + c2;
        return s0 * s0 * p.p0 + s1 * s1 * p.p1 - 4.0 * c0 * c2 * p.p2;
    }
}

Biquad discretiseMatched (const AnalogSection& s, double fs)
{
    const double T = 1.0 / fs;
    const double omegaNyq = pi * fs;
    const double T0 = analogMag2 (s, 0.0);
    const double TN = analogMag2 (s, omegaNyq);
    Biquad b;

    if (s.isFirstOrder())
    {
        // Real pole p = -d0/d1 (units of w0), mapped by impulse invariance; numerator pins DC & Nyquist.
        const double pole = std::exp (-(s.d0 / s.d1) * s.w0 * T);
        b.a1 = -pole;
        const double S0 = std::sqrt (T0) * (1.0 + b.a1);
        const double S1 = std::sqrt (TN) * (1.0 - b.a1);
        b.b0 = 0.5 * (S0 + S1);
        b.b1 = 0.5 * (S0 - S1);
        return b;
    }

    // Match point: the section's corner, kept away from DC/Nyquist for conditioning.
    const double wm = std::clamp (s.w0 * T, 1e-4, 0.9 * pi);
    const double TM = analogMag2 (s, wm * fs);

    Grid g;
    const double wLo = std::min (2.0 * pi * 20.0 * T, 0.5 * wm), wHi = 0.97 * pi;
    const double step = std::pow (wHi / wLo, 1.0 / Grid::n);
    double w = wLo;
    for (int i = 0; i <= Grid::n; ++i, w *= step)
    {
        g.w[(size_t) i] = w;
        g.phi[(size_t) i] = phis (w);
        g.target[(size_t) i] = analogMag2 (s, w * fs);
        const cd p (0.0, w * fs / s.w0);
        g.den[(size_t) i] = std::norm (s.d2 * p * p + s.d1 * p + s.d0);
    }

    // ---- candidate denominators ----
    std::array<Biquad, 2> cand {};
    int numCand = 0;
    {
        // (a) impulse invariance; clamp the pole frequency below Nyquist to avoid aliasing.
        const double wp = std::min (s.w0, 2.0 * pi * 0.47 * fs);
        const double disc = s.d1 * s.d1 - 4.0 * s.d2 * s.d0;
        Biquad c;
        if (disc < 0.0)
        {
            const double re = -s.d1 / (2.0 * s.d2) * wp * T;
            const double im = std::sqrt (-disc) / (2.0 * s.d2) * wp * T;
            const double r = std::exp (re);
            c.a1 = -2.0 * r * std::cos (std::min (im, 0.98 * pi));
            c.a2 = r * r;
        }
        else
        {
            const double sq = std::sqrt (disc);
            const double p1 = (-s.d1 + sq) / (2.0 * s.d2) * wp * T;
            const double p2 = (-s.d1 - sq) / (2.0 * s.d2) * wp * T;
            c.a1 = -(std::exp (p1) + std::exp (p2));
            c.a2 = std::exp (p1 + p2);
        }
        cand[(size_t) numCand++] = c;
    }
    {
        // (b) magnitude fit of the denominator
        const auto D = [&] (double omega) { const cd p (0.0, omega / s.w0); return std::norm (s.d2 * p * p + s.d1 * p + s.d0); };
        const double C0 = D (0.0), C1 = D (omegaNyq);
        const cd pm (0.0, wm * fs / s.w0);
        const double C2 = fitCoeff2 (g, g.den.data(), C0, C1, wm, std::norm (s.d2 * pm * pm + s.d1 * pm + s.d0), 8.0);
        double c0, c1, c2;
        if (factorPower (C0, C1, C2, c0, c1, c2) && c0 > 0.0)
        {
            Biquad c;
            c.a1 = c1 / c0;
            c.a2 = std::clamp (c2 / c0, -0.99999, 0.99999);
            if (std::abs (c.a1) < 1.0 + c.a2)   // stability triangle
                cand[(size_t) numCand++] = c;
        }
    }

    // ---- numerators for each candidate denominator; keep the best realisable biquad ----
    double bestErr = 1e300;
    auto consider = [&] (const Biquad& c)
    {
        // Worst power ratio max(r, 1/r) ranks candidates exactly like the max dB error, without logs.
        double err = 1.0;
        for (int i = 0; i <= Grid::n; ++i)
        {
            const auto& p = g.phi[(size_t) i];
            const double t = g.target[(size_t) i];
            if (t <= 1e-12) continue;   // ignore stop-band depth
            const double r = std::max (1e-300, powerOf (c.b0, c.b1, c.b2, p)) / (std::max (1e-300, powerOf (1.0, c.a1, c.a2, p)) * t);
            err = std::max (err, r > 1.0 ? r : 1.0 / r);
        }
        if (err < bestErr) { bestErr = err; b = c; }
    };

    for (int ci = 0; ci < numCand; ++ci)
    {
        Biquad c = cand[(size_t) ci];
        const double A0 = (1.0 + c.a1 + c.a2) * (1.0 + c.a1 + c.a2);
        const double A1 = (1.0 - c.a1 + c.a2) * (1.0 - c.a1 + c.a2);
        const double A2 = -4.0 * c.a2;
        const Phi ph = phis (wm);
        const double denM = A0 * ph.p0 + A1 * ph.p1 + A2 * ph.p2;

        if (s.n0 == 0.0 && s.n1 == 0.0)
        {
            // Pure 2nd-order high-pass numerator (double zero at DC): k (1 - z^-1)^2, matched at w0.
            const double k = std::sqrt (TM * denM) / (4.0 * ph.p1);
            c.b0 = k; c.b1 = -2.0 * k; c.b2 = k;
            consider (c);
            continue;
        }
        if (s.n1 == 0.0 && s.n2 != 0.0 && s.n0 != 0.0 && s.w0 * T < 0.98 * pi)
        {
            // Notch: zeros exactly on the unit circle at w0, scaled to match DC.
            const double cw = std::cos (s.w0 * T * std::sqrt (s.n0 / s.n2));
            const double k = std::sqrt (T0 * A0) / (2.0 - 2.0 * cw);
            c.b0 = k; c.b1 = -2.0 * k * cw; c.b2 = k;
            consider (c);
            continue;
        }

        const double B0 = T0 * A0, B1 = TN * A1;

        // (i) least-squares B2 over the grid
        std::array<double, Grid::n + 1> tgt {};
        for (int i = 0; i <= Grid::n; ++i)
        {
            const Phi& p = g.phi[(size_t) i];
            tgt[(size_t) i] = g.target[(size_t) i] * (A0 * p.p0 + A1 * p.p1 + A2 * p.p2);
        }
        if (factorPower (B0, B1, fitCoeff2 (g, tgt.data(), B0, B1, wm, TM * denM, 8.0), c.b0, c.b1, c.b2))
            consider (c);

        // (ii) classic three-point match (DC, Nyquist, w0)
        if (factorPower (B0, B1, (TM * denM - B0 * ph.p0 - B1 * ph.p1) / ph.p2, c.b0, c.b1, c.b2))
            consider (c);
    }

    // (iii) bilinear as a always-realisable fallback: matched is never worse than bilinear.
    {
        const double f0 = std::min (s.w0 / (2.0 * pi), 0.5 * fs * 0.995);
        const double K = std::tan (pi * f0 / fs), K2 = K * K;
        const double a0 = s.d2 + s.d1 * K + s.d0 * K2;
        Biquad c;
        c.b0 = (s.n2 + s.n1 * K + s.n0 * K2) / a0;
        c.b1 = (2.0 * (s.n0 * K2 - s.n2)) / a0;
        c.b2 = (s.n2 - s.n1 * K + s.n0 * K2) / a0;
        c.a1 = (2.0 * (s.d0 * K2 - s.d2)) / a0;
        c.a2 = (s.d2 - s.d1 * K + s.d0 * K2) / a0;
        consider (c);
    }
    return b;
}

DigitalCascade designDigital (const BandDesign& d, double fs, DesignMethod method)
{
    const auto a = makeAnalog (d);
    DigitalCascade c;
    c.numSections = a.numSections;
    c.scalar = a.scalar;
    for (int i = 0; i < a.numSections; ++i)
        c.sections[(size_t) i] = discretise (a.sections[(size_t) i], fs, method);
    return c;
}
} // namespace tg::dsp
