// Hidden: renders the real editor offscreen to PNGs.  TgsoundsEQTests "[screenshot]"
// Output directory: $TG_SCREENSHOT_DIR (default: current directory).
#include <catch2/catch_test_macros.hpp>
#include "TestHelpers.h"
#include "PluginEditor.h"

namespace
{
    void feedNoise (TgsoundsEQAudioProcessor& proc, int blocks)
    {
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        juce::Random rng (7);
        float b0 = 0, b1 = 0, b2 = 0;
        for (int bl = 0; bl < blocks; ++bl)
        {
            for (int i = 0; i < 512; ++i)
            {
                // Pink-ish noise (Paul Kellet's economy filter) + a 220 Hz tone and a 3.1 kHz resonance.
                const float w = rng.nextFloat() * 2 - 1;
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                const int n = bl * 512 + i;
                const float x = 0.08f * (b0 + b1 + b2 + w * 0.1848f)
                              + 0.05f * std::sin (2.0f * 3.14159265f * 220.0f * (float) n / 48000.0f)
                              + 0.03f * std::sin (2.0f * 3.14159265f * 3100.0f * (float) n / 48000.0f);
                buf.setSample (0, i, x); buf.setSample (1, i, x);
                buf.setSample (2, i, 0); buf.setSample (3, i, 0);
            }
            proc.processBlock (buf, midi);
            proc.getAnalyzer().processNow (512 / 48000.0);
        }
    }

    void snap (juce::Component& c, const juce::String& name)
    {
        const auto dir = juce::File (juce::SystemStats::getEnvironmentVariable ("TG_SCREENSHOT_DIR",
                                                                               juce::File::getCurrentWorkingDirectory().getFullPathName()));
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile (name + ".png");
        f.deleteFile();
        juce::FileOutputStream out (f);
        REQUIRE (out.openedOk());
        juce::PNGImageFormat().writeImageToStream (img, out);
    }
}

TEST_CASE ("Editor screenshots", "[.][screenshot]")
{
    TgsoundsEQAudioProcessor proc;
    tgtest::prepare (proc, 48000.0, 512);
    proc.getPresets().loadFactoryPreset (1);   // Vocal - Presence & Air
    auto& apvts = proc.getAPVTS();
    // Extra: a dynamic de-esser band and a mid-only bell to show more of the UI.
    tgtest::setParam (apvts, "b05_used", 1); tgtest::setParam (apvts, "b05_freq", 6500); tgtest::setParam (apvts, "b05_q", 3);
    tgtest::setParam (apvts, "b05_dyn", 1); tgtest::setParam (apvts, "b05_thr", -50); tgtest::setParam (apvts, "b05_range", 8);
    tgtest::setParam (apvts, "b06_used", 1); tgtest::setParam (apvts, "b06_freq", 3300); tgtest::setParam (apvts, "b06_gain", 2.5f);
    tgtest::setParam (apvts, "b06_q", 0.8f);   // overlaps band 3 -> collision warning
    proc.getUiState().setProperty ("piano", true, nullptr);

    auto editor = std::unique_ptr<TgsoundsEQAudioProcessorEditor> (dynamic_cast<TgsoundsEQAudioProcessorEditor*> (proc.createEditor()));
    REQUIRE (editor != nullptr);
    editor->setSize (1240, 700);
    feedNoise (proc, 200);
    editor->getDisplay().setSelectedBand (1);
    editor->getDisplay().tick();
    snap (*editor, "tgsoundseq_main");

    editor->getDisplay().setSelectedBand (4);   // dynamic band -> panel with dynamics section
    feedNoise (proc, 40);
    editor->getDisplay().tick();
    snap (*editor, "tgsoundseq_dynamic_band");

    // Minimum size + +-30 dB scale + linear phase
    proc.getUiState().setProperty ("scale", 3, nullptr);
    proc.getUiState().setProperty ("piano", false, nullptr);
    tgtest::setParam (apvts, "mode", 2);
    editor->setSize (960, 540);
    editor->getDisplay().setSelectedBand (-1);
    editor->getDisplay().resized();
    editor->getDisplay().tick();
    snap (*editor, "tgsoundseq_small");
    editor.reset();
}
