#pragma once

#include "PluginProcessor.h"

namespace tg::gui
{
/** Convenience wrapper for reading / writing band parameters from the GUI (message thread). */
class BandAccess
{
public:
    explicit BandAccess (TgsoundsEQAudioProcessor& p) : proc (p), apvts (p.getAPVTS()) {}

    juce::RangedAudioParameter* param (int band, const char* suffix) const
    {
        return apvts.getParameter (params::bandID (band, suffix));
    }

    float get (int band, const char* suffix) const
    {
        auto* p = param (band, suffix);
        return p->convertFrom0to1 (p->getValue());
    }

    void set (int band, const char* suffix, float plain) const
    {
        auto* p = param (band, suffix);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }

    /** Set with a complete begin/end gesture (single discrete change). */
    void setDiscrete (int band, const char* suffix, float plain) const
    {
        auto* p = param (band, suffix);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }

    params::BandSnapshot snapshot (int band) const { return proc.getParamRefs().snapshot (band); }

    int findFreeBand() const
    {
        for (int b = 0; b < params::kNumBands; ++b)
            if (get (b, params::band::used) < 0.5f)
                return b;
        return -1;
    }

    /** Creates a band (undoable as one step). Returns its index or -1 if all 24 are used. */
    int createBand (float freq, float gain, params::Shape shape, float q = 1.0f) const
    {
        const int b = findFreeBand();
        if (b < 0)
            return -1;
        proc.beginUndoTransaction ("Add band");
        using namespace params::band;
        setDiscrete (b, params::band::shape, (float) shape);
        setDiscrete (b, params::band::freq, freq);
        setDiscrete (b, params::band::gain, params::shapeHasGain (shape) ? gain : 0.0f);
        setDiscrete (b, params::band::q, q);
        setDiscrete (b, slope, (float) params::Slope::dB24);
        setDiscrete (b, placement, 0.0f);
        setDiscrete (b, dynOn, 0.0f);
        setDiscrete (b, enabled, 1.0f);
        setDiscrete (b, used, 1.0f);
        return b;
    }

    void deleteBand (int b) const
    {
        proc.beginUndoTransaction ("Delete band");
        setDiscrete (b, params::band::used, 0.0f);
        setDiscrete (b, params::band::dynOn, 0.0f);
        if (proc.getSoloBand() == b)
            proc.setSoloBand (-1);
    }

    /** Replace all bands by `bands` (draw-a-curve / match EQ). */
    void replaceAllBands (const std::vector<params::BandSnapshot>& bands, const juce::String& undoName) const
    {
        proc.beginUndoTransaction (undoName);
        for (int b = 0; b < params::kNumBands; ++b)
            if (get (b, params::band::used) > 0.5f)
                setDiscrete (b, params::band::used, 0.0f);
        int b = 0;
        for (const auto& s : bands)
        {
            if (b >= params::kNumBands) break;
            using namespace params::band;
            setDiscrete (b, shape, (float) s.shape);
            setDiscrete (b, freq, s.freq);
            setDiscrete (b, gain, s.gain);
            setDiscrete (b, q, s.q);
            setDiscrete (b, slope, (float) s.slope);
            setDiscrete (b, placement, (float) s.placement);
            setDiscrete (b, dynOn, 0.0f);
            setDiscrete (b, enabled, 1.0f);
            setDiscrete (b, used, 1.0f);
            ++b;
        }
    }

private:
    TgsoundsEQAudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
};
} // namespace tg::gui
