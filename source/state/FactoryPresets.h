#pragma once

#include "params/ParamIDs.h"
#include <vector>

namespace tg::state
{
/** Compact description of a factory preset. Unlisted parameters stay at their defaults. */
struct PresetBand
{
    params::Shape shape;
    float freq, gain, q;
    params::Slope slope = params::Slope::dB24;
    params::Placement placement = params::Placement::Stereo;
    bool dynamic = false;
    float threshold = -30.0f, range = 6.0f, ratio = 3.0f, attack = 5.0f, release = 80.0f;
    params::DynDir dir = params::DynDir::Down;
    params::DetSource source = params::DetSource::Internal;
};

struct FactoryPreset
{
    const char* name;
    const char* category;
    std::vector<PresetBand> bands;
    params::Mode mode = params::Mode::ZeroLatency;
    float outGain = 0.0f;
    bool autoGain = false;
};

const std::vector<FactoryPreset>& getFactoryPresets();
} // namespace tg::state
