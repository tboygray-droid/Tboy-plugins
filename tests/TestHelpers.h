#pragma once
#include "PluginProcessor.h"

namespace tgtest
{
    inline void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plainValue)
    {
        auto* p = apvts.getParameter (id);
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    }

    inline void prepare (TgsoundsEQAudioProcessor& p, double sr, int block, int channels = 2)
    {
        juce::AudioProcessor::BusesLayout layout;
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        layout.inputBuses.add (juce::AudioChannelSet::disabled());
        REQUIRE (p.setBusesLayout (layout));
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
    }
}
