#include "MidiLearn.h"

namespace tg::state
{
MidiLearn::MidiLearn (juce::AudioProcessor& p) : processor (p)
{
    for (auto& c : ccToParam) c.store (-1);
}

void MidiLearn::processMidi (const juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController())
            continue;
        const int cc = m.getControllerNumber();
        if (learning.load (std::memory_order_relaxed) < 0 && ccToParam[(size_t) cc].load (std::memory_order_relaxed) < 0)
            continue;
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)      events[(size_t) scope.startIndex1] = { cc, m.getControllerValue() };
        else if (scope.blockSize2 > 0) events[(size_t) scope.startIndex2] = { cc, m.getControllerValue() };
    }
}

void MidiLearn::dispatch()
{
    const auto& params = processor.getParameters();
    while (fifo.getNumReady() > 0)
    {
        Event e;
        {
            const auto scope = fifo.read (1);
            e = events[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        }

        if (const int target = learning.exchange (-1); target >= 0)
        {
            // A CC can only drive one parameter: steal it from any previous owner.
            for (auto& c : ccToParam)
                if (c.load() == target) c.store (-1);
            ccToParam[(size_t) e.cc].store (target);
        }

        const int idx = ccToParam[(size_t) e.cc].load();
        if (juce::isPositiveAndBelow (idx, params.size()))
            params[idx]->setValueNotifyingHost ((float) e.value / 127.0f);
    }
}

void MidiLearn::clearMapping (int parameterIndex)
{
    for (auto& c : ccToParam)
        if (c.load() == parameterIndex) c.store (-1);
}

int MidiLearn::getCcFor (int parameterIndex) const noexcept
{
    for (size_t cc = 0; cc < ccToParam.size(); ++cc)
        if (ccToParam[cc].load() == parameterIndex)
            return (int) cc;
    return -1;
}

juce::ValueTree MidiLearn::toValueTree() const
{
    juce::ValueTree t ("MIDI");
    const auto& params = processor.getParameters();
    for (size_t cc = 0; cc < ccToParam.size(); ++cc)
    {
        const int idx = ccToParam[cc].load();
        if (! juce::isPositiveAndBelow (idx, params.size()))
            continue;
        juce::ValueTree m ("MAP");
        m.setProperty ("cc", (int) cc, nullptr);
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (params[idx]))
            m.setProperty ("param", p->getParameterID(), nullptr);
        t.appendChild (m, nullptr);
    }
    return t;
}

void MidiLearn::fromValueTree (const juce::ValueTree& t)
{
    for (auto& c : ccToParam) c.store (-1);
    const auto& params = processor.getParameters();
    for (const auto& m : t)
    {
        const int cc = m["cc"];
        const auto id = m["param"].toString();
        if (! juce::isPositiveAndBelow (cc, 128))
            continue;
        for (int i = 0; i < params.size(); ++i)
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (params[i]); p != nullptr && p->getParameterID() == id)
                ccToParam[(size_t) cc].store (i);
    }
}
} // namespace tg::state
