#pragma once

#include "params/Parameters.h"
#include <array>
#include <complex>

/**
    Filter design for TgsoundsEQ.

    Every band shape is described ONCE as a cascade of analog 1st/2nd-order sections
    (AnalogCascade). From that single description we derive:

      - Bilinear (RBJ-equivalent, frequency pre-warped per section)  -> "Zero Latency"
      - Matched analog (poles by impulse invariance, numerator fitted
        so |H| matches the analog magnitude at DC, Nyquist and f0)    -> "Natural Phase"
      - The exact analog magnitude, used for the linear-phase FIR, the GUI
        and the auto-gain estimator.

    All coefficient maths is done in double precision.
*/
namespace tg::dsp
{
using params::Shape;
using params::Slope;
using cd = std::complex<double>;

inline constexpr int kMaxSections = 16;   // brickwall: 32nd order = 16 biquads

/** H(p) = (n2 p^2 + n1 p + n0) / (d2 p^2 + d1 p + d0), with p = s / w0 */
struct AnalogSection
{
    double n2 = 0, n1 = 0, n0 = 1;
    double d2 = 0, d1 = 0, d0 = 1;
    double w0 = 1;   ///< corner (rad/s)

    bool isFirstOrder() const noexcept { return d2 == 0.0 && n2 == 0.0; }
    cd response (double omega) const noexcept;   ///< omega in rad/s
};

struct AnalogCascade
{
    std::array<AnalogSection, kMaxSections> sections;
    int numSections = 0;
    double scalar = 1.0;   ///< overall linear gain

    void add (const AnalogSection& s) noexcept { if (numSections < kMaxSections) sections[(size_t) numSections++] = s; }
    cd response (double hz) const noexcept;
    double magnitudeDb (double hz) const noexcept;
};

/** Normalised digital biquad, a0 == 1. First-order sections have b2 == a2 == 0. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    cd response (double w) const noexcept;   ///< w in rad/sample
};

struct DigitalCascade
{
    std::array<Biquad, kMaxSections> sections;
    int numSections = 0;
    double scalar = 1.0;

    cd response (double hz, double sampleRate) const noexcept;
    double magnitudeDb (double hz, double sampleRate) const noexcept;
};

enum class DesignMethod { Bilinear, Matched };

/** Parameters needed to design a band (subset of BandSnapshot, possibly smoothed). */
struct BandDesign
{
    Shape shape = Shape::Bell;
    Slope slope = Slope::dB24;
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
};

/** Analog prototype of a band. */
AnalogCascade makeAnalog (const BandDesign& d);

/** Discretise one analog section. */
Biquad discretise (const AnalogSection& s, double sampleRate, DesignMethod method);

/** Analog prototype + discretisation. */
DigitalCascade designDigital (const BandDesign& d, double sampleRate, DesignMethod method);

/** Butterworth section Qs for order n (n odd => first entry is a 1st-order section, Q = 0). */
int butterworthQs (int order, std::array<double, kMaxSections>& qs, bool& hasFirstOrder);
} // namespace tg::dsp
