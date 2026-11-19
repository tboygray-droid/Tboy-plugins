#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** TgsoundsEQ visual identity: deep blue-grey background, teal accent, golden-ratio band colours. */
namespace tg::gui::theme
{
inline const juce::Colour background   { 0xff0f1218 };
inline const juce::Colour panel        { 0xff181d26 };
inline const juce::Colour panelLight   { 0xff222935 };
inline const juce::Colour outline      { 0xff2e3644 };
inline const juce::Colour grid         { 0xff242b37 };
inline const juce::Colour gridStrong   { 0xff323b4b };
inline const juce::Colour text         { 0xffd5dbe5 };
inline const juce::Colour textDim      { 0xff7d8799 };
inline const juce::Colour accent       { 0xff35d0ba };   // teal
inline const juce::Colour accent2      { 0xfff2a541 };   // amber (warnings, solo)
inline const juce::Colour danger       { 0xffef5b5b };
inline const juce::Colour analyzerPre  { 0xff5a6a86 };
inline const juce::Colour analyzerPost { 0xff7fb5ff };
inline const juce::Colour analyzerSc   { 0xffd58cff };

/** Distinct, evenly spread hues for the 24 bands. */
inline juce::Colour bandColour (int band)
{
    const float hue = std::fmod (0.53f + 0.61803398875f * (float) band, 1.0f);
    return juce::Colour::fromHSV (hue, 0.62f, 0.98f, 1.0f);
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace tg::gui::theme
