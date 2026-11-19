#include "MatchPanel.h"
#include "fit/MatchEq.h"

namespace tg::gui
{
MatchPanel::MatchPanel (TgsoundsEQAudioProcessor& p) : proc (p), access (p)
{
    refSource.addItem ("Reference from Sidechain", 1);
    refSource.addItem ("Reference from Input", 2);
    refSource.setSelectedId (p.hasSidechain() ? 1 : 2, juce::dontSendNotification);
    refSource.setItemEnabled (1, p.hasSidechain());
    refSource.setTooltip ("Where the reference spectrum is captured from.");

    for (auto* b : { &captureRef, &captureSrc })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, theme::danger);
    }
    captureRef.setTooltip ("Play the reference material, then click again to stop capturing.");
    captureSrc.setTooltip ("Play the material you want to correct (input), then click again to stop.");
    captureRef.onClick = [this] { toggleCapture (true); };
    captureSrc.onClick = [this] { toggleCapture (false); };
    applyBtn.onClick = [this] { apply(); };
    applyBtn.setTooltip ("Fit EQ bands to the difference (replaces the current bands; undoable).");

    bandsSlider.setRange (1, 16, 1);
    bandsSlider.setValue (8);
    bandsSlider.setTextValueSuffix (" bands");
    amountSlider.setRange (0, 100, 1);
    amountSlider.setValue (100);
    amountSlider.setTextValueSuffix (" %");
    for (auto* s : { &bandsSlider, &amountSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        s->setColour (juce::Slider::trackColourId, theme::accent);
    }
    bandsSlider.setTooltip ("Maximum number of bands used for the match.");
    amountSlider.setTooltip ("How much of the correction to apply.");
    status.setFont (theme::font (12.0f));
    status.setColour (juce::Label::textColourId, theme::textDim);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &refSource, &captureRef, &captureSrc, &applyBtn, &bandsSlider, &amountSlider, &status })
        addAndMakeVisible (c);
    setSize (360, 230);
    startTimerHz (8);
}

MatchPanel::~MatchPanel()
{
    auto& hub = proc.getAnalyzer();
    hub.stopCapture (analyzer::Source::Pre);
    hub.stopCapture (analyzer::Source::Sidechain);
    // Capturing may have switched the analyzer on; restore the user's choice.
    if ((int) proc.getUiState().getProperty ("anaMode", 3) == (int) analyzer::DisplayMode::Off)
        hub.setActive (false);
    stopTimer();
}

analyzer::Source MatchPanel::referenceSource() const
{
    return refSource.getSelectedId() == 1 ? analyzer::Source::Sidechain : analyzer::Source::Pre;
}

void MatchPanel::toggleCapture (bool reference)
{
    auto& hub = proc.getAnalyzer();
    auto& button = reference ? captureRef : captureSrc;
    const auto src = reference ? referenceSource() : analyzer::Source::Pre;
    const auto grid = fit::matchGrid();

    if (button.getToggleState())
    {
        (reference ? captureSrc : captureRef).setEnabled (false);
        hub.setActive (true);
        hub.startCapture (src);
        button.setButtonText ("Stop (capturing...)");
    }
    else
    {
        hub.stopCapture (src);
        const bool ok = hub.getCapture (src, grid, reference ? refDb : srcDb);
        (reference ? hasRef : hasSrc) = ok;
        button.setButtonText (reference ? "Capture Reference" : "Capture Source");
        (reference ? captureSrc : captureRef).setEnabled (true);
    }
}

void MatchPanel::apply()
{
    if (! hasRef || ! hasSrc)
        return;
    fit::MatchSettings s;
    s.maxBands = (int) bandsSlider.getValue();
    s.amount = amountSlider.getValue() / 100.0;
    const auto res = fit::computeMatch (fit::matchGrid(), refDb, srcDb, s);
    access.replaceAllBands (res.bands, "Match EQ");
    status.setText ("Applied " + juce::String ((int) res.bands.size()) + " bands (fit error " + juce::String (res.rmsErrorDb, 2)
                        + " dB RMS).", juce::dontSendNotification);
}

void MatchPanel::timerCallback()
{
    auto& hub = proc.getAnalyzer();
    juce::String s;
    s << "Reference: " << (captureRef.getToggleState() ? juce::String (hub.getCaptureFrames (referenceSource())) + " frames..."
                                                       : (hasRef ? "captured" : "-"));
    s << "    Source: " << (captureSrc.getToggleState() ? juce::String (hub.getCaptureFrames (analyzer::Source::Pre)) + " frames..."
                                                        : (hasSrc ? "captured" : "-"));
    if (! status.getText().startsWith ("Applied") || captureRef.getToggleState() || captureSrc.getToggleState())
        status.setText (s, juce::dontSendNotification);
    applyBtn.setEnabled (hasRef && hasSrc && ! captureRef.getToggleState() && ! captureSrc.getToggleState());
}

void MatchPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText ("Match EQ", getLocalBounds().reduced (12, 8).removeFromTop (20), juce::Justification::centredLeft);
}

void MatchPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (26);
    refSource.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    captureRef.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
    row.removeFromLeft (8);
    captureSrc.setBounds (row);
    r.removeFromTop (8);
    bandsSlider.setBounds (r.removeFromTop (24));
    amountSlider.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);
    applyBtn.setBounds (r.removeFromTop (28));
    status.setBounds (r);
}
} // namespace tg::gui
