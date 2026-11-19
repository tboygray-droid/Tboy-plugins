#pragma once

#include "Theme.h"

namespace tg::gui
{
/** Stereo output peak meter with decay, peak-hold line and clip indicator. Fed by the editor timer. */
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeter() { setTooltip ("Output level (peak). Click to reset the clip indicator."); }

    void push (float peakL, float peakR, float dtSeconds)
    {
        const float in[2] { peakL, peakR };
        for (int c = 0; c < 2; ++c)
        {
            const float db = juce::Decibels::gainToDecibels (in[c], -70.0f);
            level[c] = db > level[c] ? db : std::max (db, level[c] - 24.0f * dtSeconds);
            if (db >= hold[c]) { hold[c] = db; holdTime[c] = 1.5f; }
            else if ((holdTime[c] -= dtSeconds) < 0) hold[c] = std::max (level[c], hold[c] - 12.0f * dtSeconds);
            clipped[c] |= in[c] >= 1.0f;
        }
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { clipped[0] = clipped[1] = false; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::panelLight);
        g.fillRoundedRectangle (r, 3.0f);
        const float w = (r.getWidth() - 3.0f) / 2.0f;
        for (int c = 0; c < 2; ++c)
        {
            auto bar = juce::Rectangle<float> (r.getX() + 1.0f + c * (w + 1.0f), r.getY() + 5.0f, w, r.getHeight() - 6.0f);
            auto toY = [&bar] (float db) { return bar.getBottom() - bar.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f); };
            const float y = toY (level[c]);
            g.setGradientFill (juce::ColourGradient (theme::danger, 0, toY (3.0f), theme::accent, 0, toY (-18.0f), false));
            g.fillRect (bar.withTop (y));
            g.setColour (theme::text.withAlpha (0.8f));
            g.drawHorizontalLine ((int) toY (hold[c]), bar.getX(), bar.getRight());
            g.setColour (clipped[c] ? theme::danger : theme::outline);
            g.fillRect (bar.getX(), r.getY(), w, 3.0f);
        }
    }

private:
    float level[2] { -70.0f, -70.0f }, hold[2] { -70.0f, -70.0f }, holdTime[2] {};
    bool clipped[2] {};
};
} // namespace tg::gui
