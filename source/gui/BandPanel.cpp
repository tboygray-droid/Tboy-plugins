#include "BandPanel.h"

namespace tg::gui
{
namespace pb = params::band;

BandPanel::BandPanel (TgsoundsEQAudioProcessor& p) : proc (p), access (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &enabled, &solo, &remove, &dyn, &shape, &slope, &placement, &dynDir, &detSource,
             &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
        addAndMakeVisible (c);

    solo.setClickingTogglesState (true);
    solo.setColour (juce::TextButton::buttonOnColourId, theme::accent2);
    solo.setTooltip ("Listen to this band in isolation (solo).");
    solo.onClick = [this] { proc.setSoloBand (solo.getToggleState() ? band : -1); };

    remove.setTooltip ("Delete this band (or Alt-click the node / press Delete).");
    remove.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.35f));
    remove.onClick = [this] { if (onDeleteRequested) onDeleteRequested(); };

    dyn.setColour (juce::TextButton::buttonOnColourId, theme::accent2);
    setVisible (false);
    startTimerHz (15);
}

BandPanel::~BandPanel() { stopTimer(); }

void BandPanel::setBand (int b)
{
    band = b;
    if (b < 0)
    {
        for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release }) k->unbind();
        for (auto* c : { &shape, &slope, &placement, &dynDir, &detSource }) c->unbind();
        enabled.unbind();
        dyn.unbind();
        setVisible (false);
        return;
    }

    const auto col = theme::bandColour (b);
    freq.bind (params::bandID (b, pb::freq), "Band frequency (10 Hz - 30 kHz).");
    gain.bind (params::bandID (b, pb::gain), "Band gain (+-30 dB).");
    q.bind (params::bandID (b, pb::q), "Band width / resonance (0.025 - 40). Mouse wheel on the node also changes Q.");
    threshold.bind (params::bandID (b, pb::threshold), "Dynamic threshold: the band starts moving above this level.");
    range.bind (params::bandID (b, pb::range), "Maximum dynamic gain change.");
    ratio.bind (params::bandID (b, pb::ratio), "How fast the gain moves once over the threshold.");
    attack.bind (params::bandID (b, pb::attack), "Dynamic attack time.");
    release.bind (params::bandID (b, pb::release), "Dynamic release time.");
    for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
        k->setColour (col);

    shape.bind (params::bandID (b, pb::shape), "Filter shape.");
    slope.bind (params::bandID (b, pb::slope), "Slope for cuts (6-96 dB/oct or brickwall) and shelves (6 = 1st order).");
    placement.bind (params::bandID (b, pb::placement), "Which channel(s) this band processes.");
    dynDir.bind (params::bandID (b, pb::dynDir), "Gain Down compresses the band, Gain Up expands it.");
    detSource.bind (params::bandID (b, pb::detSource), "Detect from the main input or the external sidechain bus.");
    enabled.bind (params::bandID (b, pb::enabled), "Enable / bypass this band.");
    dyn.bind (params::bandID (b, pb::dynOn), "Turn this band into a dynamic EQ band.");

    solo.setToggleState (proc.getSoloBand() == b, juce::dontSendNotification);
    updateVisibility();
    setVisible (true);
    repaint();
}

juce::Point<int> BandPanel::getPreferredSize() const
{
    return showDyn ? juce::Point<int> (3 * 58 + 5 * 58 + 104 + 24, 116) : juce::Point<int> (500, 116);
}

void BandPanel::updateVisibility()
{
    if (band < 0) return;
    const auto s = access.snapshot (band);
    const bool canDyn = params::shapeHasGain (s.shape);
    const bool wantDyn = s.dynOn && canDyn;
    gain.setEnabled (canDyn);
    slope.setEnabled (params::shapeUsesSlope (s.shape));
    dyn.setEnabled (canDyn);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &threshold, &range, &ratio, &attack, &release, &dynDir, &detSource })
        c->setVisible (wantDyn);
    detSource.setEnabled (proc.hasSidechain());
    if (wantDyn != showDyn)
    {
        showDyn = wantDyn;
        if (onLayoutChanged) onLayoutChanged();
        resized();
    }
}

void BandPanel::timerCallback()
{
    if (band < 0) return;
    updateVisibility();
    solo.setToggleState (proc.getSoloBand() == band, juce::dontSendNotification);
    for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
        k->repaint();   // value read-outs (cheap)
}

void BandPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 3), 8.0f);
    g.setColour (theme::panel.withAlpha (0.96f));
    g.fillRoundedRectangle (r, 8.0f);
    const auto col = band >= 0 ? theme::bandColour (band) : theme::accent;
    g.setColour (col.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 8.0f, 1.2f);

    g.setColour (col);
    g.fillEllipse (12, 11, 12, 12);
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText ("Band " + juce::String (band + 1), 29, 7, 70, 20, juce::Justification::centredLeft);

    if (showDyn)
    {
        g.setColour (theme::outline);
        const int x = 10 + 3 * 58 + 4;
        g.drawVerticalLine (x, 40.0f, (float) getHeight() - 8.0f);
    }
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto header = r.removeFromTop (24);
    header.removeFromLeft (88);
    remove.setBounds (header.removeFromRight (26));
    header.removeFromRight (4);
    enabled.setBounds (header.removeFromLeft (38)); header.removeFromLeft (4);
    solo.setBounds (header.removeFromLeft (46)); header.removeFromLeft (4);
    dyn.setBounds (header.removeFromLeft (42)); header.removeFromLeft (6);
    const int comboW = (header.getWidth() - 8) / 3;
    shape.setBounds (header.removeFromLeft (comboW + 10)); header.removeFromLeft (4);
    slope.setBounds (header.removeFromLeft (comboW - 4)); header.removeFromLeft (4);
    placement.setBounds (header);

    r.removeFromTop (10);
    auto knobs = r;
    knobs.removeFromLeft (2);
    for (auto* k : { &freq, &gain, &q })
        k->setBounds (knobs.removeFromLeft (58));
    if (showDyn)
    {
        knobs.removeFromLeft (12);
        for (auto* k : { &threshold, &range, &ratio, &attack, &release })
            k->setBounds (knobs.removeFromLeft (58));
        knobs.removeFromLeft (6);
        auto col = knobs.removeFromLeft (100).withSizeKeepingCentre (100, 52);
        dynDir.setBounds (col.removeFromTop (24));
        col.removeFromTop (4);
        detSource.setBounds (col.removeFromTop (24));
    }
}
} // namespace tg::gui
