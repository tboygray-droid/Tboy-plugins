#pragma once

#include "Theme.h"

namespace tg::gui
{
/** Flat dark look: arc knobs, rounded pill buttons, compact combo boxes and menus. */
class TgLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TgLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (12.5f); }
    juce::Font getPopupMenuFont() override { return theme::font (13.5f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (12.0f, true); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};
} // namespace tg::gui
