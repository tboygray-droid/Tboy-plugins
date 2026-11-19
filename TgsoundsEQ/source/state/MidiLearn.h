#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace tg::state
{
/**
    MIDI Learn for any parameter.

    Audio thread: processMidi() reads CC messages. Mapped CCs and "learn" candidates are
    pushed into lock-free FIFOs; nothing else happens on the audio thread.
    Message thread: dispatch() (called from a timer) sets parameter values / completes learning.
    Mappings persist in the state tree under <MIDI>.
*/
class MidiLearn
{
public:
    explicit MidiLearn (juce::AudioProcessor& p);

    // ---- message thread ----
    void startLearning (int parameterIndex) noexcept { learning.store (parameterIndex); }
    void cancelLearning() noexcept { learning.store (-1); }
    int getLearningParameter() const noexcept { return learning.load(); }
    void clearMapping (int parameterIndex);
    int getCcFor (int parameterIndex) const noexcept;
    void dispatch();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& t);

    // ---- audio thread ----
    void processMidi (const juce::MidiBuffer& midi) noexcept;

private:
    struct Event { int cc; int value; };

    juce::AudioProcessor& processor;
    std::array<std::atomic<int>, 128> ccToParam;   // -1 = unmapped
    std::atomic<int> learning { -1 };

    juce::AbstractFifo fifo { 1024 };
    std::array<Event, 1024> events {};
};
} // namespace tg::state
