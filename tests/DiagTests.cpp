// Hidden diagnostics: run with  TgsoundsEQTests "[diag]"
#include <catch2/catch_test_macros.hpp>
#include "dsp/FilterDesign.h"
#include <cstdio>
using namespace tg::dsp;

TEST_CASE ("Print matched vs bilinear error table", "[.][diag]")
{
    const double fs = 44100;
    for (double gain : { -12.0, 12.0 })
    {
        const BandDesign d { tg::params::Shape::Bell, tg::params::Slope::dB12, 1000, gain, 0.4 };
        const auto a = makeAnalog (d);
        for (auto m : { DesignMethod::Matched, DesignMethod::Bilinear })
        {
            const auto dd = designDigital (d, fs, m);
            for (double hz : { 20., 100., 500., 1000., 2000., 4000., 8000., 12000., 16000., 19845. })
                std::printf ("%s g%+.0f %7.0f analog %8.4f err %8.4f\n", m == DesignMethod::Matched ? "M" : "B", gain, hz,
                             a.magnitudeDb (hz), dd.magnitudeDb (hz, fs) - a.magnitudeDb (hz));
        }
    }
}

TEST_CASE ("Worst-case matched error over the test grid", "[.][diag]")
{
    using tg::params::Shape;
    double worstLow = 0, worstHigh = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (auto shape : { Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::TiltShelf, Shape::FlatTilt })
            for (double f : { 50.0, 1000.0, 5000.0, 12000.0 })
                for (double gain : { -12.0, 6.0, 18.0 })
                    for (double q : { 0.4, 0.707, 1.5 })
                    {
                        const BandDesign d { shape, tg::params::Slope::dB12, f, gain, q };
                        const auto a = makeAnalog (d);
                        const auto dd = designDigital (d, fs, DesignMethod::Matched);
                        double e = 0;
                        for (int i = 0; i < 80; ++i)
                        {
                            const double hz = 10.0 * std::pow (0.45 * fs / 10.0, i / 79.0);
                            e = std::max (e, std::abs (dd.magnitudeDb (hz, fs) - a.magnitudeDb (hz)));
                        }
                        (f <= fs / 8 ? worstLow : worstHigh) = std::max (f <= fs / 8 ? worstLow : worstHigh, e);
                        if (e > 0.1) std::printf ("  bad: shape %d f %.0f g %.0f q %.3f fs %.0f err %.3f\n", (int) shape, f, gain, q, fs, e);
                    }
    std::printf ("worst matched error: bands <= fs/8: %.4f dB, higher bands: %.4f dB\n", worstLow, worstHigh);
}

TEST_CASE ("Matched vs bilinear worst error by f0/fs", "[.][diag]")
{
    using tg::params::Shape;
    for (double ratio : { 1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4 })
    {
        double worstM = 0, worstB = 0;
        const double fs = 48000.0;
        for (auto shape : { Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::TiltShelf })
            for (double gain : { -12.0, 6.0, 18.0 })
                for (double q : { 0.4, 0.707, 1.5, 4.0 })
                {
                    const BandDesign d { shape, tg::params::Slope::dB12, ratio * fs, gain, q };
                    const auto a = makeAnalog (d);
                    const auto m = designDigital (d, fs, DesignMethod::Matched);
                    const auto b = designDigital (d, fs, DesignMethod::Bilinear);
                    double eM = 0;
                    for (int i = 0; i < 120; ++i)
                    {
                        const double hzz = 10.0 * std::pow (0.49 * fs / 10.0, i / 119.0);
                        eM = std::max (eM, std::abs (m.magnitudeDb (hzz, fs) - a.magnitudeDb (hzz)));
                        const double hz = 10.0 * std::pow (0.49 * fs / 10.0, i / 119.0);
                        worstM = std::max (worstM, std::abs (m.magnitudeDb (hz, fs) - a.magnitudeDb (hz)));
                        worstB = std::max (worstB, std::abs (b.magnitudeDb (hz, fs) - a.magnitudeDb (hz)));
                    }
                    if (eM > 3.0)
                    {
                        std::printf ("   BAD shape %d gain %.0f q %.2f err %.2f\n", (int) shape, gain, q, eM);
                        for (int i = 0; i < m.numSections; ++i)
                        {
                            const auto& bq = m.sections[(size_t) i];
                            std::printf ("     b %.6f %.6f %.6f a %.6f %.6f\n", bq.b0, bq.b1, bq.b2, bq.a1, bq.a2);
                        }
                        for (double hz : { 100.0, 6000.0, 12000.0, 18000.0, 23000.0 })
                            std::printf ("     %6.0f analog %7.2f matched %7.2f\n", hz, a.magnitudeDb (hz), m.magnitudeDb (hz, fs));
                    }
                }
        std::printf ("f0 = fs/%-4.0f  matched worst %.3f dB   bilinear worst %.3f dB\n", 1.0 / ratio, worstM, worstB);
    }
}

#include "analyzer/AnalyzerFifo.h"
TEST_CASE ("FIFO single-thread diag", "[.][diag]")
{
    tg::analyzer::AnalyzerFifo fifo (4096);
    std::vector<float> block (64), out (1000);
    int next = 0, expected = 0, bad = 0, pushes = 0;
    for (int round = 0; round < 5000; ++round)
    {
        while (fifo.getNumReady() <= 4096 - 1 - 64)
        {
            for (int i = 0; i < 64; ++i) block[(size_t) i] = (float) ((next + i) % 1000);
            const float* ch[1] { block.data() };
            const int before = fifo.getNumReady();
            fifo.push (ch, 1, 64);
            if (fifo.getNumReady() - before != 64 && pushes < 5) { std::printf ("push wrote %d (ready before %d)\n", fifo.getNumReady() - before, before); ++pushes; }
            next += 64;
        }
        const int n = fifo.pull (out.data(), 700);
        for (int i = 0; i < n; ++i, ++expected) if (out[(size_t) i] != (float) (expected % 1000)) { if (bad++ < 3) std::printf ("mismatch at %d: %f\n", expected, out[(size_t) i]); }
    }
    std::printf ("pushed %d, read %d, bad %d\n", next, expected, bad);
}
