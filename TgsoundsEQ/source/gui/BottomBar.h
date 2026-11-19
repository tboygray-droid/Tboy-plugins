#pragma once

#include "Controls.h"
#include "LevelMeter.h"

namespace tg::gui
{
/**
    Bottom bar: Undo / Redo, A/B + copy, preset browser, processing mode (+ LP quality, oversampling),
    analyzer mode + settings, display scale, piano / draw / match, auto gain, polarity, output gain /
    pan and the output meters.
*/
class BottomBar : public juce::Component, private juce::Timer
{
public:
    explicit BottomBar (TgsoundsEQAudioProcessor& p);
    ~BottomBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    LevelMeter& getMeter() noexcept { return meter; }
    /** One row when wide enough, two rows on narrow editors. */
    static int preferredHeight (int width) noexcept { return width >= kSingleRowWidth ? 58 : 94; }
    static constexpr int kSingleRowWidth = 1180;
    void setDrawModeButton (bool on) { drawBtn.setToggleState (on, juce::dontSendNotification); }

    std::function<void()> onUiStateChanged;   ///< scale / piano / analyzer settings changed
    std::function<void (bool)> onDrawModeToggled;
    std::function<void (juce::Component&)> onMatchClicked;

private:
    void timerCallback() override;
    void showPresetMenu();
    void showAnalyzerMenu();
    void savePresetDialog();
    void setUi (const juce::Identifier& id, const juce::var& v);

    TgsoundsEQAudioProcessor& proc;

    IconButton undoBtn { "Undo", IconButton::Icon::Undo }, redoBtn { "Redo", IconButton::Icon::Redo };
    juce::TextButton aBtn { "A" }, bBtn { "B" }, copyBtn { "Copy" };
    IconButton prevBtn { "Previous preset", IconButton::Icon::Prev }, nextBtn { "Next preset", IconButton::Icon::Next };
    juce::TextButton presetBtn, saveBtn { "Save" };
    ParamCombo modeCombo { proc }, qualityCombo { proc }, osCombo { proc };
    juce::ComboBox analyzerCombo, scaleCombo;
    IconButton analyzerSettings { "Analyzer settings", IconButton::Icon::Gear };
    IconButton pianoBtn { "Piano", IconButton::Icon::Piano }, drawBtn { "Draw", IconButton::Icon::Pencil };
    juce::TextButton matchBtn { "Match" };
    ParamToggle autoGainBtn { proc, "AUTO" }, phaseBtn { proc, "INV" };
    ParamKnob outGain { proc, "Out" }, outPan { proc, "Pan" };
    LevelMeter meter;
    std::unique_ptr<juce::AlertWindow> saveDialog;
};
} // namespace tg::gui
