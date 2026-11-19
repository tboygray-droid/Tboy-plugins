#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tg::state
{
/**
    Factory presets (compiled in) + user presets (XML files on disk).
    All loading happens on the message thread through normal parameter changes,
    so hosts see/record them like automation and the audio thread just follows.
*/
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& apvts);

    struct Entry { juce::String name, category; bool isFactory; juce::File file; };

    /** Factory presets first, then user presets (rescanned on each call). */
    juce::Array<Entry> getAllPresets() const;
    int getNumFactoryPresets() const noexcept;

    bool loadPreset (const Entry& e);
    bool loadFactoryPreset (int index);
    bool loadNext (int direction);   ///< +1 / -1, wraps around

    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const Entry& e);

    juce::String getCurrentPresetName() const;
    static juce::File getUserPresetDirectory();

    /** Reset every parameter to its default (used by "Default" preset / before factory presets). */
    void resetAllToDefault();

    static constexpr const char* fileExtension = ".tgpreset";

private:
    void setCurrentName (const juce::String& name);
    void setParam (const juce::String& id, float plainValue);

    juce::AudioProcessorValueTreeState& apvts;
};

/** Two-slot A/B compare. Switching stores the live state into the active slot and recalls the other. */
class ABCompare
{
public:
    explicit ABCompare (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    int getActive() const noexcept { return active; }
    void select (int slot);          ///< 0 = A, 1 = B
    void copyActiveToOther();

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::array<juce::ValueTree, 2> slots;
    int active = 0;
};

/** Loads a full parameter state tree while keeping the current UI / MIDI-learn children. */
void applyParameterState (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& paramState);
} // namespace tg::state
