#include "PresetManager.h"
#include "FactoryPresets.h"
#include "params/ParamIDs.h"

namespace tg::state
{
namespace
{
    const juce::Identifier presetNameProp ("presetName");
    const juce::Identifier uiId ("UI"), midiId ("MIDI");
}

void applyParameterState (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& paramState)
{
    // Set each parameter through the normal path (host is notified, audio follows smoothly).
    for (int i = 0; i < paramState.getNumChildren(); ++i)
    {
        const auto child = paramState.getChild (i);
        if (! child.hasType ("PARAM"))
            continue;
        if (auto* p = apvts.getParameter (child["id"].toString()))
        {
            const float plain = (float) child["value"];
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

juce::File PresetManager::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Tgsounds").getChildFile ("TgsoundsEQ").getChildFile ("Presets");
}

int PresetManager::getNumFactoryPresets() const noexcept { return (int) getFactoryPresets().size(); }

juce::Array<PresetManager::Entry> PresetManager::getAllPresets() const
{
    juce::Array<Entry> list;
    for (const auto& f : getFactoryPresets())
        list.add ({ f.name, f.category, true, {} });

    const auto dir = getUserPresetDirectory();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
        files.sort();
        for (const auto& f : files)
            list.add ({ f.getFileNameWithoutExtension(), "User", false, f });
    }
    return list;
}

void PresetManager::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void PresetManager::resetAllToDefault()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != params::global::bypass)
                rp->setValueNotifyingHost (rp->getDefaultValue());
}

bool PresetManager::loadFactoryPreset (int index)
{
    const auto& all = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) all.size()))
        return false;
    const auto& pr = all[(size_t) index];

    resetAllToDefault();
    for (size_t i = 0; i < pr.bands.size() && (int) i < params::kNumBands; ++i)
    {
        const auto& b = pr.bands[i];
        const int bi = (int) i;
        using namespace params::band;
        setParam (params::bandID (bi, used), 1.0f);
        setParam (params::bandID (bi, enabled), 1.0f);
        setParam (params::bandID (bi, shape), (float) b.shape);
        setParam (params::bandID (bi, freq), b.freq);
        setParam (params::bandID (bi, gain), b.gain);
        setParam (params::bandID (bi, q), b.q);
        setParam (params::bandID (bi, slope), (float) b.slope);
        setParam (params::bandID (bi, placement), (float) b.placement);
        setParam (params::bandID (bi, dynOn), b.dynamic ? 1.0f : 0.0f);
        setParam (params::bandID (bi, threshold), b.threshold);
        setParam (params::bandID (bi, range), b.range);
        setParam (params::bandID (bi, ratio), b.ratio);
        setParam (params::bandID (bi, attack), b.attack);
        setParam (params::bandID (bi, release), b.release);
        setParam (params::bandID (bi, dynDir), (float) b.dir);
        setParam (params::bandID (bi, detSource), (float) b.source);
    }
    setParam (params::global::mode, (float) pr.mode);
    setParam (params::global::outGain, pr.outGain);
    setParam (params::global::autoGain, pr.autoGain ? 1.0f : 0.0f);
    setCurrentName (pr.name);
    return true;
}

bool PresetManager::loadPreset (const Entry& e)
{
    if (e.isFactory)
    {
        const auto& all = getFactoryPresets();
        for (size_t i = 0; i < all.size(); ++i)
            if (e.name == all[i].name)
                return loadFactoryPreset ((int) i);
        return false;
    }

    auto xml = juce::XmlDocument::parse (e.file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    resetAllToDefault();
    applyParameterState (apvts, juce::ValueTree::fromXml (*xml));
    setCurrentName (e.name);
    return true;
}

bool PresetManager::loadNext (int direction)
{
    const auto all = getAllPresets();
    if (all.isEmpty())
        return false;
    const auto current = getCurrentPresetName();
    int idx = -1;
    for (int i = 0; i < all.size(); ++i)
        if (all[i].name == current) { idx = i; break; }
    idx = (idx + direction + all.size()) % all.size();
    return loadPreset (all[idx]);
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    const auto dir = getUserPresetDirectory();
    if (! dir.createDirectory())
        return false;

    auto state = apvts.copyState();
    for (auto id : { uiId, midiId })
        if (auto c = state.getChildWithName (id); c.isValid())
            state.removeChild (c, nullptr);
    state.removeProperty (presetNameProp, nullptr);

    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (clean + fileExtension)))
        return false;
    setCurrentName (clean);
    return true;
}

bool PresetManager::deleteUserPreset (const Entry& e)
{
    return ! e.isFactory && e.file.deleteFile();
}

juce::String PresetManager::getCurrentPresetName() const
{
    return apvts.state.getProperty (presetNameProp, "Default").toString();
}

void PresetManager::setCurrentName (const juce::String& name)
{
    apvts.state.setProperty (presetNameProp, name, nullptr);
}

//==============================================================================
void ABCompare::select (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == active)
        return;
    slots[(size_t) active] = apvts.copyState();
    if (slots[(size_t) slot].isValid())
        applyParameterState (apvts, slots[(size_t) slot]);
    // An empty slot starts as a copy of the current settings.
    active = slot;
}

void ABCompare::copyActiveToOther()
{
    slots[(size_t) (1 - active)] = apvts.copyState();
}
} // namespace tg::state
