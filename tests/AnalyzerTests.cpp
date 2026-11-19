#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "AllocationGuard.h"
#include "analyzer/SpectrumAnalyzer.h"
#include <thread>

using namespace tg::analyzer;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    void feedSine (AnalyzerHub& hub, Source src, double hz, float amp, int samples, double fs = 48000.0)
    {
        std::vector<float> x (512);
        for (int pos = 0; pos < samples; pos += 512)
        {
            for (int i = 0; i < 512; ++i)
                x[(size_t) i] = amp * (float) std::sin (2 * pi * hz * (pos + i) / fs);
            const float* ch[1] { x.data() };
            hub.push (src, ch, 1, 512);
            hub.processNow (512 / fs);
        }
    }

    /** A sine of level `db` must read between db - 1.45 (worst-case Hann scalloping) and db + 0.1. */
    void checkLevel (float measured, double db)
    {
        INFO ("measured " << measured << " expected " << db);
        CHECK (measured <= db + 0.1);
        CHECK (measured >= db - 1.45);
    }

    float levelAt (const AnalyzerHub& hub, Source src, float hz, bool peak = false)
    {
        // 1 point per 1/24 octave between 10 Hz and 30 kHz; pick the point nearest `hz`.
        const int n = 277;
        std::vector<float> curve ((size_t) n);
        REQUIRE (hub.fillCurve (src, peak, 10.0f, 30000.0f, curve.data(), n));
        const int idx = juce::roundToInt (std::log (hz / 10.0) / std::log (3000.0) * (n - 1));
        // Local max within +-1/12 octave (the visible peak), like reading it off the display.
        float v = -1000.0f;
        for (int i = std::max (0, idx - 2); i <= std::min (n - 1, idx + 2); ++i)
            v = std::max (v, curve[(size_t) i]);
        return v;
    }

}

TEST_CASE ("Analyzer FIFO is lossless SPSC under concurrency", "[analyzer]")
{
    AnalyzerFifo fifo (4096);
    const int total = 2'000'000;
    std::atomic<bool> done { false };
    std::atomic<int> shortWrites { 0 };
    std::thread producer ([&]
    {
        std::vector<float> block (64);
        int next = 0;
        while (next < total)
        {
            // AbstractFifo holds capacity - 1 samples; back off while there isn't room for a block.
            if (fifo.getNumReady() > 4096 - 1 - 64) { std::this_thread::yield(); continue; }
            for (int i = 0; i < 64; ++i) block[(size_t) i] = (float) ((next + i) % 1000);
            const float* ch[1] { block.data() };
            const int w = fifo.push (ch, 1, 64);
            if (w != 64) shortWrites++;
            next += 64;
        }
        done = true;
    });

    std::vector<float> buf (1000);
    int expected = 0;
    bool ordered = true;
    while (! done || fifo.getNumReady() > 0)
    {
        const int n = fifo.pull (buf.data(), (int) buf.size());
        for (int i = 0; i < n; ++i, ++expected)
            ordered &= buf[(size_t) i] == (float) (expected % 1000);
    }
    producer.join();
    CHECK (shortWrites == 0);
    CHECK (ordered);
    CHECK (expected == total);
}

TEST_CASE ("Analyzer reads a full-scale-normalised sine level at its frequency", "[analyzer]")
{
    AnalyzerHub hub;
    hub.setSampleRate (48000.0);
    AnalyzerSettings s; s.tiltDbPerOct = 0; s.speed = 4; s.resolution = 1;
    hub.setSettings (s);
    hub.setActive (true);              // thread running concurrently is fine: processNow is locked
    feedSine (hub, Source::Post, 1000.0, 0.5f, 48000);
    hub.setActive (false);
    hub.processNow();

    checkLevel (levelAt (hub, Source::Post, 1000.0f), -6.02);   // Hann scalloping < 1.5 dB, normalised peak
    CHECK (levelAt (hub, Source::Post, 100.0f) < -70.0f);
    CHECK (levelAt (hub, Source::Post, 10000.0f) < -70.0f);
    CHECK (levelAt (hub, Source::Pre, 1000.0f) < -150.0f);   // other sources untouched
}

TEST_CASE ("Analyzer tilt lifts highs by the set dB/oct around 1 kHz", "[analyzer]")
{
    AnalyzerHub hub;
    hub.setSampleRate (48000.0);
    AnalyzerSettings s; s.tiltDbPerOct = 4.5f; s.speed = 4;
    hub.setSettings (s);
    hub.setActive (true);
    feedSine (hub, Source::Pre, 4000.0, 0.5f, 48000);
    hub.setActive (false);
    hub.processNow();
    checkLevel (levelAt (hub, Source::Pre, 4000.0f), -6.02 + 2 * 4.5);
}

TEST_CASE ("Analyzer peak hold keeps the maximum after the signal stops", "[analyzer]")
{
    AnalyzerHub hub;
    hub.setSampleRate (48000.0);
    AnalyzerSettings s; s.tiltDbPerOct = 0; s.speed = 4; s.peakHold = true;
    hub.setSettings (s);
    hub.setActive (true);
    feedSine (hub, Source::Post, 2000.0, 0.5f, 24000);
    feedSine (hub, Source::Post, 2000.0, 0.0f, 96000);   // 2 s of silence
    hub.setActive (false);
    hub.processNow();
    CHECK (levelAt (hub, Source::Post, 2000.0f) < -60.0f);                       // live curve has fallen
    checkLevel (levelAt (hub, Source::Post, 2000.0f, true), -6.02);   // peak held
}

TEST_CASE ("Processor feeds the analyzer without allocating", "[analyzer][realtime]")
{
    TgsoundsEQAudioProcessor proc;
    tgtest::setParam (proc.getAPVTS(), "b01_used", 1);
    tgtest::setParam (proc.getAPVTS(), "b01_gain", -20);
    tgtest::setParam (proc.getAPVTS(), "b01_freq", 1000);
    tgtest::prepare (proc, 48000.0, 512);
    auto& hub = proc.getAnalyzer();
    AnalyzerSettings s; s.tiltDbPerOct = 0; s.speed = 4;
    hub.setSettings (s);
    hub.setActive (true);

    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    std::size_t allocs = 0;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = 0.5f * (float) std::sin (2 * pi * 1000.0 * (b * 512 + i) / 48000.0);
            buf.setSample (0, i, v); buf.setSample (1, i, v);
        }
        {
            tgtest::ScopedAllocationCheck check;
            proc.processBlock (buf, midi);
            allocs += check.count();
        }
        hub.processNow (512 / 48000.0);   // drain at "real time" pace (the test runs faster than real time)
    }
    hub.setActive (false);
    hub.processNow();
    CHECK (allocs == 0);
    // The post-EQ spectrum shows the -20 dB bell, the pre spectrum does not.
    checkLevel (levelAt (hub, Source::Pre, 1000.0f), -6.02);
    checkLevel (levelAt (hub, Source::Post, 1000.0f), -26.02);
}
