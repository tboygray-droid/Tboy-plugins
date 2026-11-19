// GUI interaction tests: drive the real ResponseDisplay with synthesized mouse events.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "PluginEditor.h"

using namespace tg;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    struct GuiRig
    {
        TgsoundsEQAudioProcessor proc;
        std::unique_ptr<TgsoundsEQAudioProcessorEditor> editor;
        gui::ResponseDisplay* display = nullptr;

        GuiRig()
        {
            tgtest::prepare (proc, 48000.0, 256);
            editor.reset (dynamic_cast<TgsoundsEQAudioProcessorEditor*> (proc.createEditor()));
            editor->setSize (1240, 700);
            display = &editor->getDisplay();
            display->tick();
        }

        juce::MouseEvent ev (juce::Point<float> pos, juce::ModifierKeys mods = {}, int clicks = 1, juce::Point<float> downPos = {}, bool dragged = false)
        {
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (src, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, display, display, now,
                                     downPos.isOrigin() ? pos : downPos, now, clicks, dragged);
        }

        juce::Point<float> at (double hz, double db) const { return { display->freqToX (hz), display->dbToY (db) }; }
        params::BandSnapshot band (int b) const { return proc.getParamRefs().snapshot (b); }
        int usedBands() const { int n = 0; for (int b = 0; b < params::kNumBands; ++b) n += band (b).used; return n; }
    };
}

TEST_CASE ("GUI: double-click adds a band where you click", "[gui]")
{
    GuiRig r;
    r.display->mouseDoubleClick (r.ev (r.at (1000.0, 6.0), {}, 2));
    REQUIRE (r.usedBands() == 1);
    CHECK_THAT (r.band (0).freq, WithinRel (1000.0, 0.02));
    CHECK_THAT (r.band (0).gain, WithinAbs (6.0, 0.3));
    CHECK (r.band (0).shape == params::Shape::Bell);
    CHECK (r.display->getSelectedBand() == 0);

    // At the extremes the smart shape is a cut.
    r.display->mouseDoubleClick (r.ev (r.at (15.0, 0.0), {}, 2));
    CHECK (r.band (1).shape == params::Shape::LowCut);
}

TEST_CASE ("GUI: dragging a node moves frequency and gain; shift-drag is 10x finer", "[gui]")
{
    GuiRig r;
    r.display->mouseDoubleClick (r.ev (r.at (1000.0, 0.0), {}, 2));
    const auto start = r.display->getNodePosition (0);
    const auto target = r.at (2000.0, 6.0);

    r.display->mouseDown (r.ev (start));
    r.display->mouseDrag (r.ev (target, juce::ModifierKeys::leftButtonModifier, 1, start, true));
    r.display->mouseUp (r.ev (target));
    CHECK_THAT (r.band (0).freq, WithinRel (2000.0, 0.02));
    CHECK_THAT (r.band (0).gain, WithinAbs (6.0, 0.3));

    // Fine: the same mouse movement only moves 1/10 as far (0.1 octave, 0.6 dB).
    const auto p0 = r.display->getNodePosition (0);
    const auto p1 = p0 + (r.at (4000.0, 12.0) - r.at (2000.0, 6.0));
    r.display->mouseDown (r.ev (p0));
    r.display->mouseDrag (r.ev (p1, juce::ModifierKeys (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier), 1, p0, true));
    r.display->mouseUp (r.ev (p1));
    CHECK_THAT (r.band (0).freq, WithinRel (2000.0 * std::pow (2.0, 0.1), 0.02));
    CHECK_THAT (r.band (0).gain, WithinAbs (6.6, 0.1));
}

TEST_CASE ("GUI: mouse wheel changes Q, alt-click deletes, undo restores", "[gui]")
{
    GuiRig r;
    r.display->mouseDoubleClick (r.ev (r.at (500.0, 3.0), {}, 2));
    const auto pos = r.display->getNodePosition (0);
    juce::MouseWheelDetails w {};
    w.deltaY = 0.5f;
    r.display->mouseWheelMove (r.ev (pos), w);
    CHECK (r.band (0).q > 1.5f);

    r.proc.getUndoManager().beginNewTransaction();
    r.display->mouseDown (r.ev (pos, juce::ModifierKeys::altModifier));
    CHECK (r.usedBands() == 0);

    (void) r.proc.getAPVTS().copyState();   // flushes values into the tree (APVTS normally does this on a timer)
    REQUIRE (r.proc.getUndoManager().undo());
    CHECK (r.usedBands() == 1);
}

TEST_CASE ("GUI: overlapping bands are reported as collisions", "[gui]")
{
    GuiRig r;
    r.display->mouseDoubleClick (r.ev (r.at (3000.0, 4.0), {}, 2));
    r.display->mouseDoubleClick (r.ev (r.at (3150.0, 3.0), {}, 2));
    r.display->mouseDoubleClick (r.ev (r.at (300.0, 3.0), {}, 2));
    const auto c = r.display->findCollisions();
    REQUIRE (c.size() == 1);
    CHECK (c[0] == std::make_pair (0, 1));
}

TEST_CASE ("GUI: draw-a-curve mode fits bands to the sketch", "[gui]")
{
    GuiRig r;
    r.display->setDrawMode (true);
    // Sketch a +6 dB hump around 1 kHz.
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= 200; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, i / 200.0);
        const double db = 6.0 * std::exp (-std::pow (std::log2 (hz / 1000.0) / 0.8, 2.0));
        pts.push_back (r.at (hz, db));
    }
    r.display->mouseDown (r.ev (pts.front(), juce::ModifierKeys::leftButtonModifier));
    for (auto& p : pts)
        r.display->mouseDrag (r.ev (p, juce::ModifierKeys::leftButtonModifier, 1, pts.front(), true));
    r.display->mouseUp (r.ev (pts.back()));

    CHECK_FALSE (r.display->isDrawMode());
    REQUIRE (r.usedBands() >= 1);
    std::array<params::BandSnapshot, params::kNumBands> snaps {};
    for (int b = 0; b < params::kNumBands; ++b) snaps[(size_t) b] = r.band (b);
    CHECK_THAT (20 * std::log10 (dsp::analogMatrixAt (snaps, 1000.0, true).ll), WithinAbs (6.0, 0.8));
    CHECK_THAT (20 * std::log10 (dsp::analogMatrixAt (snaps, 100.0, true).ll), WithinAbs (0.0, 0.8));
}

TEST_CASE ("GUI: editor survives resizing across the allowed range", "[gui]")
{
    GuiRig r;
    for (auto [w, h] : std::vector<std::pair<int, int>> { { 960, 540 }, { 1179, 600 }, { 1180, 600 }, { 2400, 1400 } })
    {
        r.editor->setSize (w, h);
        r.display->tick();
        auto img = r.editor->createComponentSnapshot (r.editor->getLocalBounds());
        CHECK (img.getWidth() == w);
    }
}
