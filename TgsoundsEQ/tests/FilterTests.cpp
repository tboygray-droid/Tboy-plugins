#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/FilterDesign.h"
#include "dsp/Svf.h"
#include "dsp/EqBand.h"
#include "dsp/LinearPhaseEngine.h"
#include <map>

using namespace tg::dsp;
using tg::params::Shape;
using tg::params::Slope;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    std::vector<BandDesign> testDesigns()
    {
        std::vector<BandDesign> v;
        for (auto shape : { Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::LowCut, Shape::HighCut,
                            Shape::Notch, Shape::BandPass, Shape::TiltShelf, Shape::FlatTilt })
            for (double f : { 40.0, 1000.0, 8000.0 })
                for (double gain : { -18.0, 12.0 })
                    for (double q : { 0.5, 2.0 })
                        for (auto slope : { Slope::dB6, Slope::dB12, Slope::dB18, Slope::dB48 })
                            v.push_back ({ shape, slope, f, gain, q });
        return v;
    }

    double logFreq (int i, int n, double lo, double hi) { return lo * std::pow (hi / lo, (double) i / (n - 1)); }
}

TEST_CASE ("Bilinear designs match the pre-warped analog response within 0.1 dB", "[filters]")
{
    for (double fs : { 44100.0, 96000.0, 192000.0 })
        for (const auto& d : testDesigns())
        {
            const auto analog = makeAnalog (d);
            const auto digital = designDigital (d, fs, DesignMethod::Bilinear);
            REQUIRE (digital.numSections == analog.numSections);

            for (int i = 0; i < 60; ++i)
            {
                const double hz = logFreq (i, 60, 10.0, 0.45 * fs);
                // Each section was warped at its own corner: evaluate its analog prototype at the warped frequency.
                double analogDb = 20.0 * std::log10 (analog.scalar);
                for (int s = 0; s < analog.numSections; ++s)
                {
                    const auto& sec = analog.sections[(size_t) s];
                    const double f0 = std::min (sec.w0 / (2 * pi), 0.5 * fs * 0.995);
                    const double warped = 2 * pi * f0 * std::tan (pi * hz / fs) / std::tan (pi * f0 / fs);
                    AnalogSection tmp = sec; tmp.w0 = 2 * pi * f0;
                    analogDb += 20.0 * std::log10 (std::max (1e-30, std::abs (tmp.response (warped))));
                }
                const double dig = digital.magnitudeDb (hz, fs);
                if (analogDb > -100.0)   // ignore deep stop-band where both are "nothing"
                    REQUIRE_THAT (dig, WithinAbs (analogDb, 0.1));
            }
        }
}

TEST_CASE ("Matched (Natural Phase) designs follow the analog response", "[filters]")
{
    // Compared with the true (un-warped) analog response up to 0.49 fs:
    //   - within 0.1 dB for bands up to fs/64 (e.g. 690 Hz @ 44.1k, 2.7 kHz with 4x oversampling)
    //   - never worse than the bilinear design, and its worst case around fs/16 .. fs/8
    //     is at least 2.5x smaller than the bilinear worst case
    std::map<double, std::pair<double, double>> worst;   // ratio -> (matched, bilinear)
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (auto shape : { Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::TiltShelf })
            for (double ratio : { 1.0 / 256, 1.0 / 64, 1.0 / 16, 1.0 / 8 })
                for (double gain : { -12.0, 6.0, 18.0 })
                    for (double q : { 0.4, 0.707, 1.5 })
                    {
                        const BandDesign d { shape, Slope::dB12, ratio * fs, gain, q };
                        const auto analog = makeAnalog (d);
                        const auto matched = designDigital (d, fs, DesignMethod::Matched);
                        const auto bilinear = designDigital (d, fs, DesignMethod::Bilinear);
                        double errM = 0.0, errB = 0.0;
                        for (int i = 0; i < 100; ++i)
                        {
                            const double hz = logFreq (i, 100, 10.0, 0.49 * fs);
                            errM = std::max (errM, std::abs (matched.magnitudeDb (hz, fs) - analog.magnitudeDb (hz)));
                            errB = std::max (errB, std::abs (bilinear.magnitudeDb (hz, fs) - analog.magnitudeDb (hz)));
                        }
                        INFO ("shape " << (int) shape << " f0 " << ratio * fs << " gain " << gain << " q " << q << " fs " << fs
                              << " matched " << errM << " bilinear " << errB);
                        if (ratio <= 1.0 / 64)
                            REQUIRE (errM < 0.1);
                        REQUIRE (errM <= errB + 1e-6);
                        auto& w = worst[ratio];
                        w.first = std::max (w.first, errM);
                        w.second = std::max (w.second, errB);
                    }
    for (double ratio : { 1.0 / 16, 1.0 / 8 })
    {
        INFO ("fs/" << 1.0 / ratio << ": matched worst " << worst[ratio].first << ", bilinear worst " << worst[ratio].second);
        CHECK (worst[ratio].first * 2.5 < worst[ratio].second);
    }
}

TEST_CASE ("Flat tilt is a constant slope through the pivot", "[filters]")
{
    for (double fs : { 44100.0, 96000.0 })
        for (double tilt : { -12.0, 9.0 })
        {
            const BandDesign d { Shape::FlatTilt, Slope::dB12, 1000.0, tilt, 1.0 };
            const auto a = makeAnalog (d);
            CHECK_THAT (a.magnitudeDb (1000.0), WithinAbs (0.0, 1e-9));                         // pivot
            CHECK_THAT (a.magnitudeDb (8000.0) - a.magnitudeDb (125.0), WithinAbs (tilt * 6.0 / std::log2 (1000.0), 0.25));
            const auto m = designDigital (d, fs, DesignMethod::Matched);
            for (int i = 0; i < 60; ++i)
            {
                const double hz = logFreq (i, 60, 10.0, 0.45 * fs);
                REQUIRE_THAT (m.magnitudeDb (hz, fs), WithinAbs (a.magnitudeDb (hz), 0.5));
            }
        }
}

TEST_CASE ("Bilinear vs matched: matched removes Nyquist cramping of a high bell", "[filters]")
{
    const double fs = 44100.0;
    const BandDesign d { Shape::Bell, Slope::dB12, 16000.0, 12.0, 0.7 };
    const auto analog = makeAnalog (d);
    const double errBilinear = std::abs (designDigital (d, fs, DesignMethod::Bilinear).magnitudeDb (19000.0, fs) - analog.magnitudeDb (19000.0));
    const double errMatched  = std::abs (designDigital (d, fs, DesignMethod::Matched).magnitudeDb (19000.0, fs) - analog.magnitudeDb (19000.0));
    CHECK (errMatched < 0.5 * errBilinear);
}

TEST_CASE ("Cut slopes have the nominal asymptotic steepness", "[filters]")
{
    const double fs = 192000.0;
    for (auto [slope, dbPerOct] : std::vector<std::pair<Slope, double>> {
             { Slope::dB6, 6 }, { Slope::dB12, 12 }, { Slope::dB18, 18 }, { Slope::dB24, 24 }, { Slope::dB36, 36 },
             { Slope::dB48, 48 }, { Slope::dB72, 72 }, { Slope::dB96, 96 } })
    {
        const BandDesign d { Shape::HighCut, slope, 1000.0, 0.0, 0.70710678 };
        const auto a = makeAnalog (d);
        const double measured = a.magnitudeDb (32000.0) - a.magnitudeDb (64000.0);
        CHECK_THAT (measured, WithinAbs (dbPerOct * 20.0 * std::log10 (2.0) / 6.0, 0.01));   // "6 dB" is really 6.02 dB per order
        CHECK_THAT (a.magnitudeDb (1000.0), WithinAbs (-3.0103, 0.01));   // Butterworth: -3 dB at cutoff
        CHECK_THAT (designDigital (d, fs, DesignMethod::Bilinear).magnitudeDb (10.0, fs), WithinAbs (0.0, 0.01));
    }
}

TEST_CASE ("SVF mapping reproduces the biquad exactly", "[filters]")
{
    const double fs = 48000.0;
    for (const auto& d : testDesigns())
        for (auto method : { DesignMethod::Bilinear, DesignMethod::Matched })
        {
            const auto c = designDigital (d, fs, method);
            for (int s = 0; s < c.numSections; ++s)
            {
                const auto& b = c.sections[(size_t) s];
                const auto rt = SvfRuntime::from (SvfCoeffs::fromBiquad (b));
                SvfState st;
                double x1 = 0, x2 = 0, y1 = 0, y2 = 0, maxErr = 0, maxY = 0;
                for (int n = 0; n < 2000; ++n)
                {
                    const double x = n == 0 ? 1.0 : 0.0;
                    const double yDf = b.b0 * x + b.b1 * x1 + b.b2 * x2 - b.a1 * y1 - b.a2 * y2;
                    x2 = x1; x1 = x; y2 = y1; y1 = yDf;
                    const double ySvf = rt.tick (x, st);
                    maxErr = std::max (maxErr, std::abs (ySvf - yDf));
                    maxY = std::max (maxY, std::abs (yDf));
                }
                REQUIRE (maxErr <= 1e-9 * std::max (1.0, maxY));
            }
        }
}

TEST_CASE ("Processed sine gain matches the designed response (EqBand)", "[filters]")
{
    for (auto method : { DesignMethod::Bilinear, DesignMethod::Matched })
        for (auto shape : { Shape::Bell, Shape::LowShelf, Shape::HighCut, Shape::Notch })
        {
            const double fs = 48000.0;
            tg::params::BandSnapshot s;
            s.used = true; s.shape = shape; s.freq = 1000.0f; s.gain = 9.0f; s.q = 1.0f; s.slope = Slope::dB24;
            for (double testHz : { 200.0, 1000.0, 3000.0 })
            {
                if (shape == Shape::Notch && testHz == 1000.0) continue;
                EqBand band;
                band.prepare (fs);
                band.setMethod (method);
                band.setTarget (s);
                const int n = 48000;
                std::vector<float> x ((size_t) n);
                for (int i = 0; i < n; ++i)
                    x[(size_t) i] = (float) std::sin (2 * pi * testHz * i / fs);
                float* ch[1] { x.data() };
                band.process (ch, 1, n, nullptr, 0);
                double sumIn = 0, sumOut = 0;
                for (int i = n / 2; i < n; ++i)
                {
                    const double in = std::sin (2 * pi * testHz * i / fs);
                    sumIn += in * in; sumOut += (double) x[(size_t) i] * x[(size_t) i];
                }
                const double measuredDb = 10 * std::log10 (sumOut / sumIn);
                const double expected = designDigital ({ shape, Slope::dB24, 1000.0, 9.0, 1.0 }, fs, method).magnitudeDb (testHz, fs);
                INFO ("shape " << (int) shape << " at " << testHz);
                REQUIRE_THAT (measuredDb, WithinAbs (expected, 0.1));
            }
        }
}

TEST_CASE ("Linear-phase FIR matches the analog response within 0.1 dB", "[filters][linear]")
{
    std::array<tg::params::BandSnapshot, tg::params::kNumBands> snaps {};
    snaps[0].used = true; snaps[0].shape = Shape::Bell;      snaps[0].freq = 1000;  snaps[0].gain = 8;  snaps[0].q = 1;
    snaps[1].used = true; snaps[1].shape = Shape::HighShelf; snaps[1].freq = 6000;  snaps[1].gain = -6; snaps[1].q = 0.7f;
    snaps[2].used = true; snaps[2].shape = Shape::LowCut;    snaps[2].freq = 60;    snaps[2].slope = Slope::dB12; snaps[2].q = 0.7071f;

    const double fs = 48000.0;
    const int taps = LinearPhaseEngine::tapsFor (tg::params::LpQuality::Max, fs);
    std::array<std::vector<float>, 4> fir;
    designLinearPhaseFir (snaps, fs, taps, true, fir);

    // Exact symmetry around taps/2 => linear phase
    for (int k = 1; k < taps / 2; ++k)
        REQUIRE_THAT (fir[0][(size_t) (taps / 2 + k)], WithinAbs (fir[0][(size_t) (taps / 2 - k)], 1e-6));

    for (double hz : { 150.0, 400.0, 1000.0, 2500.0, 6000.0, 15000.0 })
    {
        std::complex<double> h;
        for (int n = 0; n < taps; ++n)
            h += (double) fir[0][(size_t) n] * std::polar (1.0, -2 * pi * hz * n / fs);
        const double expected = 20 * std::log10 (analogMatrixAt (snaps, hz, true).ll);
        INFO ("hz " << hz);
        REQUIRE_THAT (20 * std::log10 (std::abs (h)), WithinAbs (expected, 0.1));
    }
}
