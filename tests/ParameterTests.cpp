#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "AllocationGuard.h"

using namespace tg::params;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE ("Parameter layout has 24 bands with correct ranges", "[params]")
{
    TgsoundsEQAudioProcessor proc;
    auto& apvts = proc.getAPVTS();

    CHECK (proc.getParameters().size() == kNumBands * (int) band::all.size() + 8);

    for (int b = 0; b < kNumBands; ++b)
        for (auto* suffix : band::all)
            REQUIRE (apvts.getParameter (bandID (b, suffix)) != nullptr);

    auto* freq = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (bandID (0, band::freq)));
    REQUIRE (freq != nullptr);
    CHECK_THAT (freq->range.start, WithinAbs (10.0, 1e-6));
    CHECK_THAT (freq->range.end, WithinAbs (30000.0, 1e-3));
    // Log mapping: normalised 0.5 must sit at the geometric mean.
    CHECK_THAT (freq->convertFrom0to1 (0.5f), WithinRel (std::sqrt (10.0 * 30000.0), 1e-4));

    auto* q = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (bandID (3, band::q)));
    CHECK_THAT (q->range.start, WithinAbs (0.025, 1e-6));
    CHECK_THAT (q->range.end, WithinAbs (40.0, 1e-4));

    auto* gain = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (bandID (23, band::gain)));
    CHECK (gain->range.start == -30.0f);
    CHECK (gain->range.end == 30.0f);

    CHECK (apvts.getParameter (bandID (5, band::freq))->getName (64) == "Band 6 Frequency");
    CHECK (dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (bandID (0, band::shape)))->choices.size() == 9);
    CHECK (dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (bandID (0, band::slope)))->choices.size() == 9);
    CHECK (dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (bandID (0, band::placement)))->choices.size() == 5);
}

TEST_CASE ("Parameter text conversion round-trips", "[params]")
{
    TgsoundsEQAudioProcessor proc;
    auto* freq = proc.getAPVTS().getParameter (bandID (0, band::freq));
    CHECK_THAT (freq->getValueForText ("2.5 kHz"), WithinAbs (freq->convertTo0to1 (2500.0f), 1e-5));
    CHECK (freq->getText (freq->convertTo0to1 (2500.0f), 32) == "2.50 kHz");
}

TEST_CASE ("Supported bus layouts: mono and stereo, matching in/out", "[processor]")
{
    TgsoundsEQAudioProcessor proc;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses  = { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled() };
    l.outputBuses = { juce::AudioChannelSet::stereo() };
    CHECK (proc.checkBusesLayoutSupported (l));
    l.inputBuses  = { juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono() };
    l.outputBuses = { juce::AudioChannelSet::mono() };
    CHECK (proc.checkBusesLayoutSupported (l));
    l.inputBuses  = { juce::AudioChannelSet::mono(), juce::AudioChannelSet::disabled() };
    l.outputBuses = { juce::AudioChannelSet::stereo() };
    CHECK_FALSE (proc.checkBusesLayoutSupported (l));
}

TEST_CASE ("Default state passes audio unchanged at all sample rates", "[processor]")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        TgsoundsEQAudioProcessor proc;
        tgtest::prepare (proc, sr, 256);
        juce::AudioBuffer<float> buf (4, 256);   // 2 main + 2 sidechain
        juce::MidiBuffer midi;
        juce::Random rng (1);
        for (int ch = 0; ch < 4; ++ch)
            for (int i = 0; i < 256; ++i)
                buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        juce::AudioBuffer<float> ref;
        ref.makeCopyOf (buf);

        const int lat = proc.getLatencySamples();
        REQUIRE (lat == 0);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                REQUIRE_THAT (buf.getSample (ch, i), WithinAbs (ref.getSample (ch, i), 1e-6));
    }
}

TEST_CASE ("processBlock does not allocate", "[processor][realtime]")
{
    TgsoundsEQAudioProcessor proc;
    tgtest::prepare (proc, 48000.0, 512);
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    buf.clear();
    tgtest::setParam (proc.getAPVTS(), global::outGain, 6.0f);

    proc.processBlock (buf, midi); // warm-up
    tgtest::ScopedAllocationCheck check;
    for (int i = 0; i < 20; ++i)
        proc.processBlock (buf, midi);
    CHECK (check.count() == 0);
}

TEST_CASE ("Mono bus: output pan is ignored, gain and polarity apply", "[processor]")
{
    TgsoundsEQAudioProcessor proc;
    tgtest::setParam (proc.getAPVTS(), global::outPan, 1.0f);       // hard right
    tgtest::setParam (proc.getAPVTS(), global::outGain, -6.0f);
    tgtest::setParam (proc.getAPVTS(), global::phaseInv, 1.0f);
    tgtest::prepare (proc, 48000.0, 256, 1);
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < 256; ++i) buf.setSample (0, i, 0.5f);
        proc.processBlock (buf, midi);
    }
    CHECK_THAT (buf.getSample (0, 255), WithinAbs (-0.5f * juce::Decibels::decibelsToGain (-6.0f), 1e-4));
}
