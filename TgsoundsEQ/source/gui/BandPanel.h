#pragma once

#include "Controls.h"
#include "BandAccess.h"

namespace tg::gui
{
/**
    Floating panel shown under the selected node: Freq / Gain / Q knobs, Shape, Slope and
    Stereo placement, enable / solo / delete, and the per-band dynamics section.
    The same controls are re-bound whenever the selected band changes.
*/
class BandPanel : public juce::Component, private juce::Timer
{
public:
    explicit BandPanel (TgsoundsEQAudioProcessor& p);
    ~BandPanel() override;

    void setBand (int b);
    int getBand() const noexcept { return band; }
    juce::Point<int> getPreferredSize() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onDeleteRequested;
    std::function<void()> onLayoutChanged;

private:
    void timerCallback() override;
    void updateVisibility();

    TgsoundsEQAudioProcessor& proc;
    BandAccess access;
    int band = -1;
    bool showDyn = false;

    ParamToggle enabled { proc, "ON" };
    juce::TextButton solo { "SOLO" }, remove { "X" };
    ParamToggle dyn { proc, "DYN" };
    ParamCombo shape { proc }, slope { proc }, placement { proc }, dynDir { proc }, detSource { proc };
    ParamKnob freq { proc, "Freq" }, gain { proc, "Gain" }, q { proc, "Q" };
    ParamKnob threshold { proc, "Thresh" }, range { proc, "Range" }, ratio { proc, "Ratio" },
              attack { proc, "Attack" }, release { proc, "Release" };
};
} // namespace tg::gui
