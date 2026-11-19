#pragma once

#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/ResponseDisplay.h"
#include "gui/BandPanel.h"
#include "gui/BottomBar.h"
#include <juce_opengl/juce_opengl.h>

/**
    TgsoundsEQ editor: resizable, OpenGL-accelerated, repainted on every display v-blank (~60 fps).
    Layout: header strip, the response display (with the floating band panel), the bottom bar.
*/
class TgsoundsEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit TgsoundsEQAudioProcessorEditor (TgsoundsEQAudioProcessor&);
    ~TgsoundsEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    tg::gui::ResponseDisplay& getDisplay() noexcept { return display; }

private:
    void timerCallback() override;
    void applyUiState();
    void positionBandPanel();
    void showMatchPanel (juce::Component& anchor);

    TgsoundsEQAudioProcessor& proc;
    tg::gui::TgLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };
    tg::gui::ResponseDisplay display;
    tg::gui::BandPanel bandPanel;
    tg::gui::BottomBar bottomBar;
    juce::OpenGLContext openGL;
    juce::VBlankAttachment vblank;
    double lastMeterTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TgsoundsEQAudioProcessorEditor)
};
