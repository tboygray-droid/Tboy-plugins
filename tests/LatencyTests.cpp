#include <catch2/catch_test_macros.hpp>
#include "TestHelpers.h"

using namespace tg::params;

namespace
{
    /** Feeds an impulse and returns the index of the strongest output sample. */
    int measureDelay (TgsoundsEQAudioProcessor& proc, int block, int totalLength)
    {
        juce::AudioBuffer<float> buf (4, block);
        juce::MidiBuffer midi;
        int bestIdx = -1;
        float best = 0.0f;
        for (int pos = 0; pos < totalLength; pos += block)
        {
            buf.clear();
            if (pos == 0)
            {
                buf.setSample (0, 0, 1.0f);
                buf.setSample (1, 0, 1.0f);
            }
            proc.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
                if (std::abs (buf.getSample (0, i)) > best)
                {
                    best = std::abs (buf.getSample (0, i));
                    bestIdx = pos + i;
                }
        }
        return bestIdx;
    }
}

TEST_CASE ("Reported latency matches the measured delay for every mode", "[latency]")
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
        for (int mode = 0; mode < 3; ++mode)
            for (int sub = 0; sub < (mode == 2 ? 4 : 3); ++sub)   // LP quality or oversampling factor
            {
                TgsoundsEQAudioProcessor proc;
                auto& apvts = proc.getAPVTS();
                tgtest::setParam (apvts, global::mode, (float) mode);
                if (mode == 2) tgtest::setParam (apvts, global::lpQuality, (float) sub);
                else           tgtest::setParam (apvts, global::os, (float) sub);
                // A gentle band so the path is really exercised.
                tgtest::setParam (apvts, bandID (0, band::used), 1);
                tgtest::setParam (apvts, bandID (0, band::freq), 2000);
                tgtest::setParam (apvts, bandID (0, band::gain), 3);
                tgtest::setParam (apvts, bandID (0, band::q), 0.3f);

                tgtest::prepare (proc, sr, 512);
                proc.runDesignerNow();

                const int reported = proc.getLatencySamples();
                CHECK (reported == proc.computeRequiredLatency());
                const int measured = measureDelay (proc, 512, reported + 8192);
                INFO ("sr " << sr << " mode " << mode << " sub " << sub << " reported " << reported << " measured " << measured);
                // IIR modes: min-phase filters smear the peak by a couple of samples at most.
                CHECK (std::abs (measured - reported) <= (mode == 2 ? 0 : 3));
            }
}

TEST_CASE ("Latency follows mode changes at run time", "[latency]")
{
    TgsoundsEQAudioProcessor proc;
    auto& apvts = proc.getAPVTS();
    tgtest::prepare (proc, 48000.0, 256);
    CHECK (proc.getLatencySamples() == 0);

    tgtest::setParam (apvts, global::mode, 2.0f);
    tgtest::setParam (apvts, global::lpQuality, 2.0f);
    proc.updateHostLatency();   // normally driven by the message-thread timer
    CHECK (proc.getLatencySamples() == tg::dsp::LinearPhaseEngine::latencyFor (LpQuality::High, 48000.0));

    proc.runDesignerNow();
    CHECK (measureDelay (proc, 256, 20000) == proc.getLatencySamples());

    tgtest::setParam (apvts, global::mode, 0.0f);
    tgtest::setParam (apvts, global::os, 1.0f);
    proc.updateHostLatency();
    CHECK (proc.getLatencySamples() == proc.computeRequiredLatency());
    CHECK (proc.getLatencySamples() > 0);
}

TEST_CASE ("Bypass stays latency-compensated", "[latency]")
{
    TgsoundsEQAudioProcessor proc;
    auto& apvts = proc.getAPVTS();
    tgtest::setParam (apvts, global::mode, 2.0f);
    tgtest::setParam (apvts, global::bypass, 1.0f);
    tgtest::prepare (proc, 44100.0, 256);
    CHECK (measureDelay (proc, 256, 10000) == proc.getLatencySamples());
}
