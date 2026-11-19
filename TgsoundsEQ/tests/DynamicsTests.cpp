#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"

using namespace tg::params;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    struct Rig
    {
        TgsoundsEQAudioProcessor proc;
        juce::AudioProcessorValueTreeState& apvts = proc.getAPVTS();

        explicit Rig (bool withSidechain = false, int mode = 0)
        {
            tgtest::setParam (apvts, global::mode, (float) mode);
            tgtest::setParam (apvts, bandID (0, band::used), 1);
            tgtest::setParam (apvts, bandID (0, band::freq), 1000);
            tgtest::setParam (apvts, bandID (0, band::q), 2);
            tgtest::setParam (apvts, bandID (0, band::gain), 0);
            tgtest::setParam (apvts, bandID (0, band::dynOn), 1);
            tgtest::setParam (apvts, bandID (0, band::threshold), -40);
            tgtest::setParam (apvts, bandID (0, band::range), 12);
            tgtest::setParam (apvts, bandID (0, band::ratio), 20);
            tgtest::setParam (apvts, bandID (0, band::attack), 1);
            tgtest::setParam (apvts, bandID (0, band::release), 50);

            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (withSidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            REQUIRE (proc.setBusesLayout (layout));
            proc.setRateAndBufferSizeDetails (48000.0, 256);
            proc.prepareToPlay (48000.0, 256);
            proc.runDesignerNow();
        }

        /** Runs `seconds` of a sine (main) and an optional sine on the sidechain; returns output RMS gain (dB) of the last 0.25 s. */
        double run (double mainHz, float mainAmp, double scHz = 0, float scAmp = 0, double seconds = 1.0)
        {
            juce::AudioBuffer<float> buf (4, 256);
            juce::MidiBuffer midi;
            const int blocks = (int) (seconds * 48000 / 256);
            double in2 = 0, out2 = 0;
            const int lat = proc.getLatencySamples();
            for (int bl = 0; bl < blocks; ++bl)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const int n = bl * 256 + i;
                    const float m = mainAmp * (float) std::sin (2 * pi * mainHz * n / 48000.0);
                    const float s = scAmp * (float) std::sin (2 * pi * scHz * n / 48000.0);
                    buf.setSample (0, i, m); buf.setSample (1, i, m);
                    buf.setSample (2, i, s); buf.setSample (3, i, s);
                }
                proc.processBlock (buf, midi);
                if (bl >= blocks - (48000 / 4) / 256)
                    for (int i = 0; i < 256; ++i)
                    {
                        const int n = bl * 256 + i - lat;
                        const double x = mainAmp * std::sin (2 * pi * mainHz * n / 48000.0);
                        in2 += x * x;
                        out2 += (double) buf.getSample (0, i) * buf.getSample (0, i);
                    }
            }
            return 10 * std::log10 (out2 / in2);
        }
    };
}

TEST_CASE ("Dynamic band: loud in-band signal is cut by up to `range`", "[dynamics]")
{
    Rig rig;
    // -6 dBFS at 1 kHz: far above -40 dB threshold with ratio 20 => full 12 dB range applied.
    const double g = rig.run (1000, 0.5f);
    CHECK_THAT (g, WithinAbs (-12.0, 0.6));
    CHECK_THAT (rig.proc.getDynamicGainDb (0), WithinAbs (-12.0, 0.6));
}

TEST_CASE ("Dynamic band: quiet signal below threshold is untouched", "[dynamics]")
{
    Rig rig;
    const double g = rig.run (1000, 0.001f);   // -60 dBFS < -40 dB threshold
    CHECK_THAT (g, WithinAbs (0.0, 0.1));
}

TEST_CASE ("Dynamic band: gain-up direction boosts instead", "[dynamics]")
{
    Rig rig;
    tgtest::setParam (rig.apvts, bandID (0, band::dynDir), 1);
    CHECK_THAT (rig.run (1000, 0.25f), WithinAbs (12.0, 0.6));
}

TEST_CASE ("Dynamic band: ratio sets the slope below the range limit", "[dynamics]")
{
    Rig rig;
    tgtest::setParam (rig.apvts, bandID (0, band::ratio), 2);
    tgtest::setParam (rig.apvts, bandID (0, band::range), 30);
    // Detector sees the band-pass output (~ the sine itself at f0): peak level -20 dB => 20 dB over,
    // ratio 2:1 => 10 dB of gain reduction.
    rig.run (1000, 0.1f);
    CHECK_THAT (rig.proc.getDynamicGainDb (0), WithinAbs (-10.0, 1.0));
}

TEST_CASE ("Dynamic band: detection is band-limited", "[dynamics]")
{
    Rig rig;
    // A loud signal far from the band (60 Hz) must not trigger the 1 kHz Q=2 band...
    rig.run (60, 0.05f, 0, 0, 0.5);
    CHECK (rig.proc.getDynamicGainDb (0) > -1.0f);
    // ...but the same level at 1 kHz does.
    rig.run (1000, 0.05f, 0, 0, 0.5);
    CHECK (rig.proc.getDynamicGainDb (0) < -8.0f);
}

TEST_CASE ("Dynamic band: external sidechain drives the band", "[dynamics][sidechain]")
{
    Rig rig (true);
    tgtest::setParam (rig.apvts, bandID (0, band::detSource), 1);   // External
    // Quiet main signal, loud 1 kHz key on the sidechain => main is ducked by 12 dB.
    CHECK_THAT (rig.run (1000, 0.001f, 1000, 0.5f), WithinAbs (-12.0, 0.6));
    // Silent sidechain => no reduction even though the main input is loud.
    CHECK_THAT (rig.run (1000, 0.5f, 1000, 0.0f), WithinAbs (0.0, 0.2));
}

TEST_CASE ("Dynamic band works in Linear Phase mode", "[dynamics][linear]")
{
    Rig rig (false, 2);
    CHECK_THAT (rig.run (1000, 0.5f, 0, 0, 1.5), WithinAbs (-12.0, 0.8));
}

TEST_CASE ("Dynamics on a cut shape is ignored (no gain to modulate)", "[dynamics]")
{
    Rig rig;
    tgtest::setParam (rig.apvts, bandID (0, band::shape), (float) Shape::LowCut);
    tgtest::setParam (rig.apvts, bandID (0, band::freq), 20);
    tgtest::setParam (rig.apvts, bandID (0, band::q), 0.7071f);
    CHECK_THAT (rig.run (1000, 0.5f), WithinAbs (0.0, 0.1));
}
