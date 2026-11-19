#pragma once

#include "Theme.h"
#include "PluginProcessor.h"

namespace tg::gui
{
/** Adds "MIDI Learn / Clear / Reset" to a right-click on any parameter control. */
void showParameterMenu (TgsoundsEQAudioProcessor& proc, juce::RangedAudioParameter& param, juce::Component& target);

/** Rotary knob bound to a parameter, with a name label, value read-out, tooltip,
    double-click-to-default and a right-click menu (MIDI learn). Can be re-bound to another parameter. */
class ParamKnob : public juce::Component, private juce::Slider::Listener
{
public:
    ParamKnob (TgsoundsEQAudioProcessor& p, const juce::String& labelText);
    ~ParamKnob() override;

    void bind (const juce::String& paramID, const juce::String& tooltip = {});
    void unbind();
    void setColour (juce::Colour c);
    void setShowValue (bool shouldShow) { showValue = shouldShow; repaint(); }

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

private:
    struct KnobSlider : juce::Slider
    {
        ParamKnob& owner;
        explicit KnobSlider (ParamKnob& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent& e) override;
    };
    void sliderValueChanged (juce::Slider*) override { repaint(); }

    TgsoundsEQAudioProcessor& proc;
    juce::String label;
    KnobSlider slider { *this };
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool showValue = true;
};

/** Combo box bound to a choice parameter. */
class ParamCombo : public juce::ComboBox
{
public:
    explicit ParamCombo (TgsoundsEQAudioProcessor& p) : proc (p) {}
    void bind (const juce::String& paramID, const juce::String& tooltip = {});
    void unbind() { attachment.reset(); param = nullptr; }
    void mouseDown (const juce::MouseEvent& e) override;

private:
    TgsoundsEQAudioProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Toggle button bound to a bool parameter. */
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (TgsoundsEQAudioProcessor& p, const juce::String& buttonText) : juce::TextButton (buttonText), proc (p)
    {
        setClickingTogglesState (true);
    }
    void bind (const juce::String& paramID, const juce::String& tooltip = {});
    void unbind() { attachment.reset(); param = nullptr; }
    void mouseDown (const juce::MouseEvent& e) override;

private:
    TgsoundsEQAudioProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Small vector-icon button (undo, redo, gear...). */
class IconButton : public juce::Button
{
public:
    enum class Icon { Undo, Redo, Gear, Prev, Next, Piano, Pencil, Phase };
    IconButton (const juce::String& name, Icon i) : juce::Button (name), icon (i) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
};
} // namespace tg::gui
