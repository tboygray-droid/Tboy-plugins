// Hidden benchmark: TgsoundsEQTests "[bench]"
#include <catch2/catch_test_macros.hpp>
#include "TestHelpers.h"
#include <chrono>
#include <cstdio>

using namespace tg::params;

TEST_CASE ("CPU: 24 active bands, stereo 48 kHz", "[.][bench]")
{
    struct Cfg { const char* name; int mode; int os; int q; bool modulate; };
    for (auto cfg : { Cfg { "Zero Latency (static)", 0, 0, 0, false }, Cfg { "Zero Latency (all bands modulating)", 0, 0, 0, true },
                      Cfg { "Natural Phase (all bands modulating)", 1, 0, 0, true }, Cfg { "Zero Latency 4x OS (static)", 0, 2, 0, false },
                      Cfg { "Linear Phase Medium", 2, 0, 1, false }, Cfg { "Linear Phase Max", 2, 0, 3, false } })
    {
        TgsoundsEQAudioProcessor proc;
        auto& apvts = proc.getAPVTS();
        for (int b = 0; b < kNumBands; ++b)
        {
            tgtest::setParam (apvts, bandID (b, band::used), 1);
            tgtest::setParam (apvts, bandID (b, band::gain), (b % 2 ? 4.0f : -4.0f));
            tgtest::setParam (apvts, bandID (b, band::shape), (float) (b % 3 == 0 ? 0 : (b % 3 == 1 ? 1 : 3)));
        }
        tgtest::setParam (apvts, global::mode, (float) cfg.mode);
        tgtest::setParam (apvts, global::os, (float) cfg.os);
        tgtest::setParam (apvts, global::lpQuality, (float) cfg.q);
        tgtest::prepare (proc, 48000.0, 512);
        proc.runDesignerNow();

        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        juce::Random rng (1);
        const int blocks = 48000 * 5 / 512;   // 5 s of audio
        double secs = 0.0;
        for (int i = 0; i < blocks; ++i)
        {
            if (cfg.modulate)
                for (int b = 0; b < kNumBands; ++b)
                    apvts.getParameter (bandID (b, band::freq))->setValueNotifyingHost (rng.nextFloat());
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 512; ++s)
                    buf.setSample (ch, s, rng.nextFloat() - 0.5f);
            const auto t0 = std::chrono::steady_clock::now();
            proc.processBlock (buf, midi);
            secs += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        }
        std::printf ("%-40s %6.2f %% of one core (5 s audio in %.3f s)\n", cfg.name, 100.0 * secs / 5.0, secs);
    }
}
