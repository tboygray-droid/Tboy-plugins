#include "Parameters.h"
#include "BoolParameter.h"

namespace tg::params
{
using juce::AudioParameterChoice;
using juce::AudioParameterFloat;
using juce::AudioParameterFloatAttributes;
using juce::ParameterID;

juce::NormalisableRange<float> logRange (float min, float max)
{
    const auto ratio = std::log (max / min);
    return { min, max,
             [=] (float, float, float x) { return min * std::exp (ratio * x); },
             [=] (float, float, float v) { return std::log (v / min) / ratio; },
             [=] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); } };
}

namespace
{
    juce::String formatFreq (float hz, int)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                             : juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz";
    }

    float parseFreq (const juce::String& text)
    {
        auto t = text.trim().toLowerCase();
        auto v = t.getFloatValue();
        return t.containsChar ('k') ? v * 1000.0f : v;
    }

    auto freqAttr()  { return AudioParameterFloatAttributes().withStringFromValueFunction (formatFreq)
                                                             .withValueFromStringFunction (parseFreq)
                                                             .withLabel ("Hz"); }
    auto dbAttr()    { return AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " dB"; })
                                                             .withLabel ("dB"); }
    auto msAttr()    { return AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10 ? 2 : 1) + " ms"; })
                                                             .withLabel ("ms"); }
    auto plainAttr (int decimals) { return AudioParameterFloatAttributes().withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); }); }

    ParameterID pid (const juce::String& id) { return { id, kVersionHint }; }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Spread default frequencies of fresh bands log-evenly so new bands don't stack.
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto name = [b] (const char* what) { return "Band " + juce::String (b + 1) + " " + what; };
        const float defaultFreq = 30.0f * std::pow (16000.0f / 30.0f, (float) b / (kNumBands - 1));

        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1),
                                                                           "Band " + juce::String (b + 1), "|");
        group->addChild (
            std::make_unique<BoolParameter>   (pid (bandID (b, band::used)),    name ("Used"), false),
            std::make_unique<BoolParameter>   (pid (bandID (b, band::enabled)), name ("Enabled"), true),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::freq)),    name ("Frequency"),
                                                    logRange (kFreqMin, kFreqMax), defaultFreq, freqAttr()),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::gain)),    name ("Gain"),
                                                    juce::NormalisableRange<float> (kGainMin, kGainMax, 0.01f), 0.0f, dbAttr()),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::q)),       name ("Q"),
                                                    logRange (kQMin, kQMax), 1.0f, plainAttr (3)),
            std::make_unique<AudioParameterChoice> (pid (bandID (b, band::shape)),   name ("Shape"), shapeNames, 0),
            std::make_unique<AudioParameterChoice> (pid (bandID (b, band::slope)),   name ("Slope"), slopeNames, (int) Slope::dB24),
            std::make_unique<AudioParameterChoice> (pid (bandID (b, band::placement)), name ("Stereo Placement"), placementNames, 0),
            std::make_unique<BoolParameter>   (pid (bandID (b, band::dynOn)),   name ("Dynamic"), false),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::threshold)), name ("Threshold"),
                                                    juce::NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -30.0f, dbAttr()),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::range)),   name ("Range"),
                                                    juce::NormalisableRange<float> (0.0f, 30.0f, 0.01f), 6.0f, dbAttr()),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::ratio)),   name ("Ratio"),
                                                    juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f, 0.4f), 2.0f,
                                                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + ":1"; })),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::attack)),  name ("Attack"),
                                                    logRange (0.1f, 500.0f), 10.0f, msAttr()),
            std::make_unique<AudioParameterFloat>  (pid (bandID (b, band::release)), name ("Release"),
                                                    logRange (1.0f, 5000.0f), 120.0f, msAttr()),
            std::make_unique<AudioParameterChoice> (pid (bandID (b, band::dynDir)),  name ("Dynamic Direction"), dynDirNames, 0),
            std::make_unique<AudioParameterChoice> (pid (bandID (b, band::detSource)), name ("Detection Source"), detSourceNames, 0));

        layout.add (std::move (group));
    }

    auto globals = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    globals->addChild (
        std::make_unique<AudioParameterFloat>  (pid (global::outGain), "Output Gain",
                                                juce::NormalisableRange<float> (-36.0f, 36.0f, 0.01f), 0.0f, dbAttr()),
        std::make_unique<AudioParameterFloat>  (pid (global::outPan), "Output Pan",
                                                juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                {
                                                    if (std::abs (v) < 0.005f) return juce::String ("C");
                                                    return juce::String (std::abs (v) * 100.0f, 0) + (v < 0 ? "% L" : "% R");
                                                })),
        std::make_unique<BoolParameter>   (pid (global::autoGain), "Auto Gain", false),
        std::make_unique<BoolParameter>   (pid (global::phaseInv), "Phase Invert", false),
        std::make_unique<AudioParameterChoice> (pid (global::mode), "Processing Mode", modeNames, 0),
        std::make_unique<AudioParameterChoice> (pid (global::lpQuality), "Linear Phase Quality", qualityNames, 1),
        std::make_unique<AudioParameterChoice> (pid (global::os), "Oversampling", osNames, 0),
        std::make_unique<BoolParameter>   (pid (global::bypass), "Bypass", false));
    layout.add (std::move (globals));

    return layout;
}

void ParamRefs::bind (juce::AudioProcessorValueTreeState& apvts)
{
    auto get = [&] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& r = bands[(size_t) b];
        r.used      = get (bandID (b, band::used));
        r.enabled   = get (bandID (b, band::enabled));
        r.freq      = get (bandID (b, band::freq));
        r.gain      = get (bandID (b, band::gain));
        r.q         = get (bandID (b, band::q));
        r.shape     = get (bandID (b, band::shape));
        r.slope     = get (bandID (b, band::slope));
        r.placement = get (bandID (b, band::placement));
        r.dynOn     = get (bandID (b, band::dynOn));
        r.threshold = get (bandID (b, band::threshold));
        r.range     = get (bandID (b, band::range));
        r.ratio     = get (bandID (b, band::ratio));
        r.attack    = get (bandID (b, band::attack));
        r.release   = get (bandID (b, band::release));
        r.dynDir    = get (bandID (b, band::dynDir));
        r.detSource = get (bandID (b, band::detSource));
    }

    global.outGain   = get (global::outGain);
    global.outPan    = get (global::outPan);
    global.autoGain  = get (global::autoGain);
    global.phaseInv  = get (global::phaseInv);
    global.mode      = get (global::mode);
    global.lpQuality = get (global::lpQuality);
    global.os        = get (global::os);
    global.bypass    = get (global::bypass);
}

BandSnapshot ParamRefs::snapshot (int b) const noexcept
{
    const auto& r = bands[(size_t) b];
    auto ld = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };

    BandSnapshot s;
    s.used      = ld (r.used) > 0.5f;
    s.enabled   = ld (r.enabled) > 0.5f;
    s.freq      = ld (r.freq);
    s.gain      = ld (r.gain);
    s.q         = ld (r.q);
    s.shape     = (Shape)     juce::jlimit (0, 8, (int) ld (r.shape));
    s.slope     = (Slope)     juce::jlimit (0, 8, (int) ld (r.slope));
    s.placement = (Placement) juce::jlimit (0, 4, (int) ld (r.placement));
    s.dynOn     = ld (r.dynOn) > 0.5f;
    s.threshold = ld (r.threshold);
    s.range     = ld (r.range);
    s.ratio     = ld (r.ratio);
    s.attackMs  = ld (r.attack);
    s.releaseMs = ld (r.release);
    s.dynDir    = (DynDir)    juce::jlimit (0, 1, (int) ld (r.dynDir));
    s.detSource = (DetSource) juce::jlimit (0, 1, (int) ld (r.detSource));
    return s;
}
} // namespace tg::params
