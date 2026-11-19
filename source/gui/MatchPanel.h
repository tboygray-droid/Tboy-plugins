#pragma once

#include "Controls.h"
#include "BandAccess.h"

namespace tg::gui
{
/**
    Match EQ workflow:
      1. Capture the reference (from the sidechain bus, or from the input while the reference plays)
      2. Capture the source (the input)
      3. Apply: the smoothed, level-normalised difference is fitted with EQ bands.
*/
class MatchPanel : public juce::Component, private juce::Timer
{
public:
    explicit MatchPanel (TgsoundsEQAudioProcessor& p);
    ~MatchPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void toggleCapture (bool reference);
    void apply();
    analyzer::Source referenceSource() const;

    TgsoundsEQAudioProcessor& proc;
    BandAccess access;
    juce::ComboBox refSource;
    juce::TextButton captureRef { "Capture Reference" }, captureSrc { "Capture Source" }, applyBtn { "Apply Match" };
    juce::Slider bandsSlider, amountSlider;
    juce::Label status;
    bool hasRef = false, hasSrc = false;
    std::vector<double> refDb, srcDb;
};
} // namespace tg::gui
