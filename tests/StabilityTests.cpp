#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "AllocationGuard.h"
#include <cmath>

using namespace tg::params;

namespace
{
    void randomiseBands (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng, bool includeDynamics)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            for (auto* suffix : { band::freq, band::gain, band::q, band::shape, band::slope, band::placement })
                apvts.getParameter (bandID (b, suffix))->setValueNotifyingHost (rng.nextFloat());
            apvts.getParameter (bandID (b, band::used))->setValueNotifyingHost (rng.nextFloat() < 0.6f ? 1.0f : 0.0f);
            apvts.getParameter (bandID (b, band::enabled))->setValueNotifyingHost (rng.nextFloat() < 0.9f ? 1.0f : 0.0f);
            if (includeDynamics)
                apvts.getParameter (bandID (b, band::dynOn))->setValueNotifyingHost (rng.nextFloat() < 0.3f ? 1.0f : 0.0f);
        }
    }

    bool allFinite (const juce::AudioBuffer<float>& b, int numCh, float limit)
    {
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getSample (ch, i);
                if (! std::isfinite (v) || std::abs (v) > limit)
                    return false;
            }
        return true;
    }
}

TEST_CASE ("Stable under fast random modulation in every mode", "[stability]")
{
    for (int mode = 0; mode < 3; ++mode)
        for (int os : { 0, 2 })
        {
            if (mode == 2 && os != 0) continue;
            TgsoundsEQAudioProcessor proc;
            auto& apvts = proc.getAPVTS();
            tgtest::setParam (apvts, global::mode, (float) mode);
            tgtest::setParam (apvts, global::os, (float) os);
            tgtest::prepare (proc, 48000.0, 128);

            juce::Random rng (42 + mode);
            juce::AudioBuffer<float> buf (4, 128);
            juce::MidiBuffer midi;
            for (int block = 0; block < 600; ++block)
            {
                if (block % 3 == 0)   // parameter jumps every ~8 ms
                    randomiseBands (apvts, rng, true);
                if (mode == 2 && block % 50 == 0)
                    proc.runDesignerNow();
                for (int ch = 0; ch < 4; ++ch)
                    for (int i = 0; i < 128; ++i)
                        buf.setSample (ch, i, (rng.nextFloat() * 2 - 1) * 0.5f);
                proc.processBlock (buf, midi);
                INFO ("mode " << mode << " os " << os << " block " << block);
                // +30 dB boosts stacked can legitimately get loud, but never infinite / exploding.
                REQUIRE (allFinite (buf, 2, 1.0e5f));
            }
        }
}

TEST_CASE ("Sweeping a resonant band from 10 Hz to 30 kHz stays bounded", "[stability]")
{
    for (double sr : { 44100.0, 192000.0 })
    {
        TgsoundsEQAudioProcessor proc;
        auto& apvts = proc.getAPVTS();
        tgtest::setParam (apvts, bandID (0, band::used), 1);
        tgtest::setParam (apvts, bandID (0, band::q), 40);
        tgtest::setParam (apvts, bandID (0, band::gain), 30);
        tgtest::prepare (proc, sr, 64);
        juce::AudioBuffer<float> buf (4, 64);
        juce::MidiBuffer midi;
        juce::Random rng (3);
        auto* freq = apvts.getParameter (bandID (0, band::freq));
        for (int block = 0; block < 2000; ++block)
        {
            freq->setValueNotifyingHost (0.5f + 0.5f * std::sin (block * 0.05f));   // fast full-range sweep
            for (int ch = 0; ch < 4; ++ch)
                for (int i = 0; i < 64; ++i)
                    buf.setSample (ch, i, rng.nextFloat() * 0.2f - 0.1f);
            proc.processBlock (buf, midi);
            REQUIRE (allFinite (buf, 2, 1.0e4f));
        }
    }
}

TEST_CASE ("No denormals after the input goes silent", "[stability][denormals]")
{
    TgsoundsEQAudioProcessor proc;
    auto& apvts = proc.getAPVTS();
    for (int b = 0; b < 6; ++b)
    {
        tgtest::setParam (apvts, bandID (b, band::used), 1);
        tgtest::setParam (apvts, bandID (b, band::gain), 12);
        tgtest::setParam (apvts, bandID (b, band::freq), 20.0f * std::pow (2.0f, (float) b));
        tgtest::setParam (apvts, bandID (b, band::q), 8.0f);
    }
    tgtest::prepare (proc, 48000.0, 256);
    juce::AudioBuffer<float> buf (4, 256);
    juce::MidiBuffer midi;
    buf.clear();
    buf.setSample (0, 0, 1.0f);
    buf.setSample (1, 0, 1.0f);
    proc.processBlock (buf, midi);

    int subnormals = 0;
    for (int block = 0; block < 4000; ++block)   // ~21 s of silence
    {
        buf.clear();
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                subnormals += std::fpclassify (buf.getSample (ch, i)) == FP_SUBNORMAL ? 1 : 0;
    }
    CHECK (subnormals == 0);
    // And it eventually decays to true silence.
    CHECK (buf.getMagnitude (0, 256) == 0.0f);
}

TEST_CASE ("processBlock does not allocate in any mode", "[realtime]")
{
    for (int mode = 0; mode < 3; ++mode)
    {
        TgsoundsEQAudioProcessor proc;
        auto& apvts = proc.getAPVTS();
        juce::Random rng (9);
        randomiseBands (apvts, rng, true);
        tgtest::setParam (apvts, global::mode, (float) mode);
        tgtest::setParam (apvts, global::os, mode == 2 ? 0.0f : 2.0f);
        tgtest::setParam (apvts, global::autoGain, 1);
        tgtest::prepare (proc, 48000.0, 512);
        proc.runDesignerNow();
        proc.setSoloBand (2);

        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        buf.clear();
        proc.processBlock (buf, midi);

        std::size_t allocations = 0;
        for (int i = 0; i < 50; ++i)
        {
            if (i == 20) randomiseBands (apvts, rng, true);   // host-side changes (may allocate: not audio thread)
            tgtest::ScopedAllocationCheck check;              // only processBlock is tracked
            proc.processBlock (buf, midi);
            allocations += check.count();
        }
        INFO ("mode " << mode);
        CHECK (allocations == 0);
    }
}
