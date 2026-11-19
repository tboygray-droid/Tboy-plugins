#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "fit/CurveFitter.h"
#include "fit/MatchEq.h"
#include "state/FactoryPresets.h"
#include "dsp/LinearPhaseEngine.h"

using namespace tg;
using Catch::Matchers::WithinAbs;

namespace
{
    std::vector<double> grid (int n = 100)
    {
        std::vector<double> f;
        for (int i = 0; i < n; ++i) f.push_back (20.0 * std::pow (1000.0, i / (n - 1.0)));
        return f;
    }

    double responseDb (const std::vector<params::BandSnapshot>& bands, double hz)
    {
        std::array<params::BandSnapshot, params::kNumBands> snaps {};
        for (size_t i = 0; i < bands.size(); ++i) snaps[i] = bands[i];
        return 20.0 * std::log10 (dsp::analogMatrixAt (snaps, hz, true).ll);
    }
}

// ---------------------------------------------------------------- curve fitting
TEST_CASE ("Curve fitter recovers a known 3-band EQ", "[fit]")
{
    const std::vector<params::BandSnapshot> truth = [] {
        std::vector<params::BandSnapshot> v (3);
        for (auto& b : v) { b.used = true; b.shape = params::Shape::Bell; }
        v[0].freq = 120;  v[0].gain = 5;  v[0].q = 1.2f;
        v[1].freq = 900;  v[1].gain = -4; v[1].q = 2.0f;
        v[2].freq = 6000; v[2].gain = 3;  v[2].q = 0.8f;
        return v;
    }();
    const auto f = grid();
    std::vector<double> target;
    for (double hz : f) target.push_back (responseDb (truth, hz));

    fit::FitOptions o;
    o.maxBands = 6;
    const auto res = fit::fitCurve (f, target, {}, o);
    INFO ("bands " << res.bands.size() << " max err " << res.maxErrorDb << " rms " << res.rmsErrorDb);
    CHECK (res.maxErrorDb < 0.5);
    CHECK (res.bands.size() <= 6);
    for (double hz : f)
        REQUIRE_THAT (responseDb (res.bands, hz), WithinAbs (responseDb (truth, hz), 0.5));
}

TEST_CASE ("Curve fitter handles a free-hand tilt-like sketch with shelves", "[fit]")
{
    const auto f = grid();
    std::vector<double> target;
    for (double hz : f) target.push_back (hz < 200 ? 4.0 : hz > 5000 ? -3.0 : 4.0 - 7.0 * std::log2 (hz / 200) / std::log2 (25.0));
    fit::FitOptions o;
    o.maxBands = 8;
    const auto res = fit::fitCurve (f, target, {}, o);
    CHECK (res.rmsErrorDb < 0.5);
    CHECK (res.maxErrorDb < 1.2);
}

TEST_CASE ("Match EQ: correction is level-normalised and recovers a tonal difference", "[fit][match]")
{
    // Source = reference + a known EQ tilt/bump + 10 dB level offset. Match must undo the EQ, not the level.
    const auto f = fit::matchGrid();
    std::vector<double> ref, src;
    for (double hz : f)
    {
        const double r = -20.0 - 3.0 * std::log2 (hz / 1000.0);                        // pinkish reference
        const double colour = 6.0 * std::exp (-std::pow (std::log2 (hz / 2500.0) / 0.7, 2.0));   // +6 dB bump at 2.5 kHz
        ref.push_back (r);
        src.push_back (r + colour - 10.0);
    }
    fit::MatchSettings s;
    s.maxBands = 6;
    s.smoothingOctaves = 6;
    const auto res = fit::computeMatch (f, ref, src, s);
    REQUIRE_FALSE (res.bands.empty());
    CHECK_THAT (responseDb (res.bands, 2500.0), WithinAbs (-6.0, 1.0));   // bump removed
    CHECK_THAT (responseDb (res.bands, 200.0), WithinAbs (0.0, 1.0));     // no broadband level change
}

TEST_CASE ("Match EQ end-to-end through the analyzer capture", "[fit][match][analyzer]")
{
    TgsoundsEQAudioProcessor proc;
    tgtest::prepare (proc, 48000.0, 512);
    auto& hub = proc.getAnalyzer();
    hub.setActive (true);

    auto play = [&] (float bumpDb, int blocks)
    {
        // White noise through a +bumpDb bell at 3 kHz (rendered by a second EQ instance).
        TgsoundsEQAudioProcessor colour;
        tgtest::setParam (colour.getAPVTS(), "b01_used", 1);
        tgtest::setParam (colour.getAPVTS(), "b01_freq", 3000);
        tgtest::setParam (colour.getAPVTS(), "b01_gain", bumpDb);
        tgtest::setParam (colour.getAPVTS(), "b01_q", 1.0f);
        tgtest::prepare (colour, 48000.0, 512);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        juce::Random rng (11);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i) { const float x = 0.2f * (rng.nextFloat() * 2 - 1); buf.setSample (0, i, x); buf.setSample (1, i, x); }
            colour.processBlock (buf, midi);
            proc.processBlock (buf, midi);
            hub.processNow (512 / 48000.0);
        }
    };

    hub.startCapture (analyzer::Source::Pre);
    play (0.0f, 400);                      // reference: flat noise
    hub.stopCapture (analyzer::Source::Pre);
    std::vector<double> refDb, srcDb;
    const auto f = fit::matchGrid();
    REQUIRE (hub.getCapture (analyzer::Source::Pre, f, refDb));

    hub.startCapture (analyzer::Source::Pre);
    play (6.0f, 400);                      // source: +6 dB at 3 kHz
    hub.stopCapture (analyzer::Source::Pre);
    REQUIRE (hub.getCapture (analyzer::Source::Pre, f, srcDb));
    hub.setActive (false);

    fit::MatchSettings s;
    const auto res = fit::computeMatch (f, refDb, srcDb, s);
    CHECK_THAT (responseDb (res.bands, 3000.0), WithinAbs (-6.0, 1.5));
    CHECK_THAT (responseDb (res.bands, 300.0), WithinAbs (0.0, 1.0));
}

// ---------------------------------------------------------------- presets / A-B / MIDI
TEST_CASE ("All factory presets load and produce finite audio", "[presets]")
{
    const auto& presets = state::getFactoryPresets();
    CHECK (presets.size() >= 20);

    TgsoundsEQAudioProcessor proc;
    tgtest::prepare (proc, 48000.0, 256);
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        INFO (presets[(size_t) i].name);
        REQUIRE (proc.getPresets().loadFactoryPreset (i));
        CHECK (proc.getPresets().getCurrentPresetName() == presets[(size_t) i].name);
        int used = 0;
        for (int b = 0; b < params::kNumBands; ++b) used += proc.getParamRefs().snapshot (b).used ? 1 : 0;
        CHECK (used == (int) presets[(size_t) i].bands.size());

        proc.runDesignerNow();
        juce::AudioBuffer<float> buf (4, 256);
        juce::MidiBuffer midi;
        juce::Random rng (i);
        for (int b = 0; b < 20; ++b)
        {
            for (int ch = 0; ch < 4; ++ch) for (int s = 0; s < 256; ++s) buf.setSample (ch, s, rng.nextFloat() - 0.5f);
            proc.processBlock (buf, midi);
        }
        for (int s = 0; s < 256; ++s) REQUIRE (std::isfinite (buf.getSample (0, s)));
    }
}

TEST_CASE ("User presets save, list and reload", "[presets]")
{
    TgsoundsEQAudioProcessor proc;
    auto& pm = proc.getPresets();
    pm.loadFactoryPreset (7);   // Kick - Punch
    tgtest::setParam (proc.getAPVTS(), "b02_gain", 7.5f);
    const juce::String name = "zz_test_preset_" + juce::String (juce::Random::getSystemRandom().nextInt (100000));
    REQUIRE (pm.saveUserPreset (name));

    pm.loadFactoryPreset (0);
    CHECK (proc.getParamRefs().snapshot (1).used == false);

    bool found = false;
    for (auto& e : pm.getAllPresets())
        if (e.name == name)
        {
            found = true;
            REQUIRE (pm.loadPreset (e));
            CHECK_THAT (proc.getParamRefs().snapshot (1).gain, WithinAbs (7.5, 1e-3));
            CHECK (pm.getCurrentPresetName() == name);
            CHECK (pm.deleteUserPreset (e));
        }
    CHECK (found);
}

TEST_CASE ("A/B compare switches and copies settings", "[presets]")
{
    TgsoundsEQAudioProcessor proc;
    auto& ab = proc.getAB();
    auto& apvts = proc.getAPVTS();
    tgtest::setParam (apvts, "b01_used", 1);
    tgtest::setParam (apvts, "b01_gain", 6.0f);
    ab.select (1);                               // B starts as a copy of A
    CHECK_THAT (proc.getParamRefs().snapshot (0).gain, WithinAbs (6.0, 1e-4));
    tgtest::setParam (apvts, "b01_gain", -3.0f); // edit B
    ab.select (0);
    CHECK_THAT (proc.getParamRefs().snapshot (0).gain, WithinAbs (6.0, 1e-4));
    ab.select (1);
    CHECK_THAT (proc.getParamRefs().snapshot (0).gain, WithinAbs (-3.0, 1e-4));
    ab.copyActiveToOther();                      // B -> A
    ab.select (0);
    CHECK_THAT (proc.getParamRefs().snapshot (0).gain, WithinAbs (-3.0, 1e-4));
}

TEST_CASE ("MIDI learn maps a CC, drives the parameter and persists", "[midi]")
{
    TgsoundsEQAudioProcessor proc;
    tgtest::prepare (proc, 48000.0, 256);
    auto* gain = proc.getAPVTS().getParameter ("b03_gain");
    const int idx = proc.getParameters().indexOf (gain);
    auto& ml = proc.getMidiLearn();

    ml.startLearning (idx);
    juce::AudioBuffer<float> buf (4, 256);
    buf.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 127), 0);
    proc.processBlock (buf, midi);   // audio thread only queues
    ml.dispatch();                   // message thread applies
    CHECK (ml.getCcFor (idx) == 21);
    CHECK_THAT (gain->getValue(), WithinAbs (1.0, 1e-6));

    midi.clear();
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 0), 10);
    proc.processBlock (buf, midi);
    ml.dispatch();
    CHECK_THAT (gain->getValue(), WithinAbs (0.0, 1e-6));

    juce::MemoryBlock state;
    proc.getStateInformation (state);
    TgsoundsEQAudioProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.getMidiLearn().getCcFor (idx) == 21);

    ml.clearMapping (idx);
    CHECK (ml.getCcFor (idx) == -1);
}

TEST_CASE ("Automation: every parameter has a unique, readable name and sane range text", "[params]")
{
    TgsoundsEQAudioProcessor proc;
    juce::StringArray names;
    for (auto* p : proc.getParameters())
    {
        const auto n = p->getName (128);
        CHECK (n.isNotEmpty());
        CHECK_FALSE (names.contains (n));
        names.add (n);
        CHECK (p->getText (0.0f, 64).isNotEmpty());
        CHECK (p->getText (1.0f, 64).isNotEmpty());
    }
}
