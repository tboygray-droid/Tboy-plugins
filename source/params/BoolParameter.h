#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tg::params
{
/**
    Boolean parameter whose normalised value is always exactly 0 or 1.

    juce::AudioParameterBool stores the raw normalised value it is given (e.g. 0.73), so after
    a host sets such a value, getValue() != the value restored from a saved state (1.0) and
    APVTS sees "no change" on restore. Snapping on set keeps state save / restore exact
    (pluginval "Plugin state restoration", strictness 10).
*/
class BoolParameter final : public juce::RangedAudioParameter
{
public:
    BoolParameter (const juce::ParameterID& id, const juce::String& name, bool defaultValue)
        : juce::RangedAudioParameter (id, name), defaultVal (defaultValue ? 1.0f : 0.0f), value (defaultVal) {}

    bool get() const noexcept { return value.load() >= 0.5f; }

    float getValue() const override { return value.load(); }
    void setValue (float v) override { value.store (v >= 0.5f ? 1.0f : 0.0f); }
    float getDefaultValue() const override { return defaultVal; }
    int getNumSteps() const override { return 2; }
    bool isDiscrete() const override { return true; }
    bool isBoolean() const override { return true; }
    juce::String getText (float v, int) const override { return v >= 0.5f ? "On" : "Off"; }
    float getValueForText (const juce::String& text) const override
    {
        const auto t = text.trim().toLowerCase();
        return (t == "on" || t == "true" || t == "yes" || t.getFloatValue() >= 0.5f) ? 1.0f : 0.0f;
    }
    juce::StringArray getAllValueStrings() const override { return { "Off", "On" }; }
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }

private:
    const float defaultVal;
    std::atomic<float> value;
    const juce::NormalisableRange<float> range { 0.0f, 1.0f, 1.0f };
};
} // namespace tg::params
