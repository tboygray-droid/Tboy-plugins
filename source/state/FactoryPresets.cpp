#include "FactoryPresets.h"

namespace tg::state
{
using params::Shape;
using params::Slope;
using params::Placement;
using params::Mode;

namespace
{
    PresetBand dyn (PresetBand b, float thr, float range, float ratio, float att, float rel,
                    params::DynDir dir = params::DynDir::Down, params::DetSource src = params::DetSource::Internal)
    {
        b.dynamic = true; b.threshold = thr; b.range = range; b.ratio = ratio; b.attack = att; b.release = rel;
        b.dir = dir; b.source = src;
        return b;
    }
    PresetBand placed (PresetBand b, Placement p) { b.placement = p; return b; }
    PresetBand sloped (PresetBand b, Slope s) { b.slope = s; return b; }
}

const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets
    {
        { "Default", "General", {} },

        // ---- Vocals ----
        { "Vocal - Presence & Air", "Vocal", {
            sloped ({ Shape::LowCut, 80, 0, 0.7071f }, Slope::dB24),
            { Shape::Bell, 300, -2.0f, 1.2f },
            { Shape::Bell, 3000, 2.0f, 0.8f },
            { Shape::HighShelf, 12000, 3.0f, 0.7f } } },
        { "Vocal - De-Box", "Vocal", {
            sloped ({ Shape::LowCut, 100, 0, 0.7071f }, Slope::dB18),
            { Shape::Bell, 450, -4.0f, 1.5f },
            { Shape::Bell, 2500, 1.5f, 1.0f } } },
        { "Vocal - Radio Voice", "Vocal", {
            sloped ({ Shape::LowCut, 400, 0, 0.9f }, Slope::dB24),
            sloped ({ Shape::HighCut, 4000, 0, 0.9f }, Slope::dB24),
            { Shape::Bell, 1500, 4.0f, 1.0f } } },
        { "De-Esser", "Vocal", {
            dyn ({ Shape::Bell, 6500, 0.0f, 3.0f }, -32.0f, 8.0f, 4.0f, 1.0f, 60.0f) } },
        { "Dynamic De-Harsh", "Vocal", {
            dyn ({ Shape::Bell, 3200, 0.0f, 2.0f }, -28.0f, 5.0f, 3.0f, 3.0f, 90.0f) } },
        { "Podcast Voice", "Vocal", {
            sloped ({ Shape::LowCut, 80, 0, 0.7071f }, Slope::dB24),
            { Shape::Bell, 250, -2.0f, 1.0f },
            { Shape::Bell, 3000, 3.0f, 0.9f },
            dyn ({ Shape::Bell, 7000, 0.0f, 2.5f }, -30.0f, 6.0f, 4.0f, 1.0f, 70.0f) } },

        // ---- Drums ----
        { "Kick - Punch", "Drums", {
            sloped ({ Shape::LowCut, 30, 0, 0.7071f }, Slope::dB24),
            { Shape::Bell, 60, 4.0f, 1.2f },
            { Shape::Bell, 350, -5.0f, 1.5f },
            { Shape::Bell, 4000, 3.0f, 1.5f } } },
        { "Snare - Crack", "Drums", {
            sloped ({ Shape::LowCut, 80, 0, 0.7071f }, Slope::dB12),
            { Shape::Bell, 200, 3.0f, 1.2f },
            { Shape::Bell, 900, -3.0f, 2.0f },
            { Shape::Bell, 5000, 3.0f, 1.0f } } },
        { "Drum Bus - Glue", "Drums", {
            { Shape::LowShelf, 80, 2.0f, 0.7f },
            { Shape::Bell, 400, -2.0f, 0.8f },
            { Shape::HighShelf, 10000, 2.5f, 0.7f } } },
        { "Overheads - Clean Up", "Drums", {
            sloped ({ Shape::LowCut, 200, 0, 0.7071f }, Slope::dB12),
            { Shape::Bell, 3500, -2.0f, 1.0f },
            { Shape::HighShelf, 10000, 2.0f, 0.7f } } },

        // ---- Instruments ----
        { "Bass - Definition", "Instruments", {
            sloped ({ Shape::LowCut, 30, 0, 0.7071f }, Slope::dB24),
            { Shape::Bell, 100, 2.0f, 1.0f },
            { Shape::Bell, 250, -3.0f, 1.2f },
            { Shape::Bell, 900, 3.0f, 1.5f } } },
        { "Acoustic Guitar - Sparkle", "Instruments", {
            sloped ({ Shape::LowCut, 90, 0, 0.7071f }, Slope::dB18),
            { Shape::Bell, 200, -3.0f, 1.0f },
            { Shape::Bell, 5000, 2.0f, 1.0f },
            { Shape::HighShelf, 12000, 2.0f, 0.7f } } },
        { "Electric Guitar - Tame Fizz", "Instruments", {
            sloped ({ Shape::LowCut, 80, 0, 0.7071f }, Slope::dB12),
            sloped ({ Shape::HighCut, 9000, 0, 0.7071f }, Slope::dB12),
            { Shape::Bell, 2500, -2.5f, 1.8f } } },
        { "Piano - Clarity", "Instruments", {
            sloped ({ Shape::LowCut, 40, 0, 0.7071f }, Slope::dB12),
            { Shape::Bell, 300, -2.0f, 0.9f },
            { Shape::Bell, 2500, 1.5f, 0.8f },
            { Shape::HighShelf, 9000, 1.5f, 0.7f } } },

        // ---- Master / Mix ----
        { "Master - Gentle Smile", "Master", {
            sloped ({ Shape::LowCut, 20, 0, 0.7071f }, Slope::dB12),
            { Shape::LowShelf, 60, 1.0f, 0.7f },
            { Shape::Bell, 400, -1.0f, 0.7f },
            { Shape::HighShelf, 12000, 1.5f, 0.7f } }, Mode::NaturalPhase },
        { "Master - Linear Phase Polish", "Master", {
            sloped ({ Shape::LowCut, 25, 0, 0.7071f }, Slope::dB48),
            { Shape::Bell, 250, -1.0f, 0.8f },
            { Shape::HighShelf, 10000, 1.0f, 0.7f } }, Mode::LinearPhase },
        { "Master - Mid/Side Width", "Master", {
            placed (sloped ({ Shape::LowCut, 150, 0, 0.7071f }, Slope::dB12), Placement::Side),
            placed ({ Shape::HighShelf, 8000, 2.0f, 0.7f }, Placement::Side),
            placed ({ Shape::Bell, 2000, 1.0f, 0.8f }, Placement::Mid) }, Mode::NaturalPhase },
        { "Mix Bus - Tilt Brighten", "Master", {
            { Shape::TiltShelf, 1000, 2.0f, 0.5f } }, Mode::ZeroLatency, 0.0f, true },
        { "Low-End Tightener", "Master", {
            dyn ({ Shape::Bell, 120, 0.0f, 1.2f }, -24.0f, 4.0f, 3.0f, 10.0f, 150.0f) } },

        // ---- Creative / Utility ----
        { "Telephone", "Creative", {
            sloped ({ Shape::LowCut, 500, 0, 1.0f }, Slope::dB48),
            sloped ({ Shape::HighCut, 3000, 0, 1.0f }, Slope::dB48),
            { Shape::Bell, 1200, 6.0f, 1.2f } }, Mode::ZeroLatency, -3.0f },
        { "Sidechain - Duck Bass Under Kick", "Creative", {
            dyn ({ Shape::Bell, 70, 0.0f, 1.0f }, -30.0f, 6.0f, 6.0f, 2.0f, 80.0f,
                 params::DynDir::Down, params::DetSource::External) } },
    };
    return presets;
}
} // namespace tg::state
