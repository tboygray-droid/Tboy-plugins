#include "Controls.h"

namespace tg::gui
{
namespace
{
    int indexOfParameter (TgsoundsEQAudioProcessor& proc, juce::AudioProcessorParameter& p)
    {
        return proc.getParameters().indexOf (&p);
    }

    juce::String withHints (const juce::String& tip)
    {
        return tip + (tip.isNotEmpty() ? "\n" : "") + "Double-click: reset  |  Right-click: MIDI learn";
    }
}

void showParameterMenu (TgsoundsEQAudioProcessor& proc, juce::RangedAudioParameter& param, juce::Component& target)
{
    auto& ml = proc.getMidiLearn();
    const int idx = indexOfParameter (proc, param);
    const int cc = ml.getCcFor (idx);

    juce::PopupMenu m;
    m.addSectionHeader (param.getName (64));
    m.addItem (1, ml.getLearningParameter() == idx ? "Cancel MIDI Learn" : "MIDI Learn");
    m.addItem (2, cc >= 0 ? "Clear MIDI (CC " + juce::String (cc) + ")" : "Clear MIDI", cc >= 0);
    m.addSeparator();
    m.addItem (3, "Reset to default");

    juce::Component::SafePointer<juce::Component> safe (&target);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                     [&proc, &param, idx, safe] (int r)
                     {
                         auto& learn = proc.getMidiLearn();
                         if (r == 1)
                         {
                             if (learn.getLearningParameter() == idx) learn.cancelLearning();
                             else learn.startLearning (idx);
                         }
                         else if (r == 2) learn.clearMapping (idx);
                         else if (r == 3)
                         {
                             param.beginChangeGesture();
                             param.setValueNotifyingHost (param.getDefaultValue());
                             param.endChangeGesture();
                         }
                         if (safe != nullptr) safe->repaint();
                     });
}

//==============================================================================
ParamKnob::ParamKnob (TgsoundsEQAudioProcessor& p, const juce::String& labelText) : proc (p), label (labelText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
    slider.addListener (this);
    addAndMakeVisible (slider);
}

ParamKnob::~ParamKnob() { slider.removeListener (this); }

void ParamKnob::bind (const juce::String& paramID, const juce::String& tooltip)
{
    attachment.reset();
    param = proc.getAPVTS().getParameter (paramID);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getAPVTS(), paramID, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.setTooltip (withHints (tooltip));
    setEnabled (true);
    repaint();
}

void ParamKnob::unbind()
{
    attachment.reset();
    param = nullptr;
}

void ParamKnob::setColour (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (14);
    if (showValue) r.removeFromBottom (14);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void ParamKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const bool learning = param != nullptr && proc.getMidiLearn().getLearningParameter() == proc.getParameters().indexOf (param);
    g.setColour (learning ? theme::accent2 : theme::textDim);
    g.setFont (theme::font (11.0f, true));
    g.drawText (learning ? "LEARN..." : label.toUpperCase(), r.removeFromTop (14), juce::Justification::centred);
    if (showValue && param != nullptr)
    {
        g.setColour (theme::text);
        g.setFont (theme::font (11.5f));
        g.drawText (param->getCurrentValueAsText(), r.removeFromBottom (14), juce::Justification::centred);
    }
}

void ParamKnob::KnobSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && owner.param != nullptr)
    {
        showParameterMenu (owner.proc, *owner.param, *this);
        return;
    }
    juce::Slider::mouseDown (e);
}

//==============================================================================
void ParamCombo::bind (const juce::String& paramID, const juce::String& tooltip)
{
    attachment.reset();
    param = proc.getAPVTS().getParameter (paramID);
    jassert (param != nullptr);
    clear (juce::dontSendNotification);
    addItemList (param->getAllValueStrings(), 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.getAPVTS(), paramID, *this);
    setTooltip (tooltip + "\nRight-click: MIDI learn");
}

void ParamCombo::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && param != nullptr)
        showParameterMenu (proc, *param, *this);
    else
        juce::ComboBox::mouseDown (e);
}

void ParamToggle::bind (const juce::String& paramID, const juce::String& tooltip)
{
    attachment.reset();
    param = proc.getAPVTS().getParameter (paramID);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getAPVTS(), paramID, *this);
    setTooltip (tooltip + "\nRight-click: MIDI learn");
}

void ParamToggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && param != nullptr)
        showParameterMenu (proc, *param, *this);
    else
        juce::TextButton::mouseDown (e);
}

//==============================================================================
void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    auto bg = getToggleState() ? theme::accent.withAlpha (0.85f) : theme::panelLight;
    if (down) bg = bg.darker (0.2f); else if (highlighted) bg = bg.brighter (0.12f);
    g.setColour (bg.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.28f;
    juce::Path p;
    juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto ink = (getToggleState() ? theme::background : theme::text).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f);
    g.setColour (ink);

    switch (icon)
    {
        case Icon::Undo:
        case Icon::Redo:
        {
            const float dir = icon == Icon::Undo ? -1.0f : 1.0f;
            p.addCentredArc (c.x, c.y + s * 0.2f, s, s, 0.0f, -dir * 1.4f, dir * 1.2f, true);
            g.strokePath (p, stroke);
            juce::Path head;
            const float hx = c.x - dir * s * 0.98f, hy = c.y + s * 0.2f - s * 0.17f;
            head.addTriangle (hx - s * 0.45f, hy, hx + s * 0.45f, hy, hx, hy - s * 0.55f * 1.0f);
            head.applyTransform (juce::AffineTransform::rotation (dir * 0.4f, hx, hy));
            g.fillPath (head);
            break;
        }
        case Icon::Prev:
        case Icon::Next:
        {
            const float d = icon == Icon::Prev ? -1.0f : 1.0f;
            p.startNewSubPath (c.x - d * s * 0.4f, c.y - s * 0.8f);
            p.lineTo (c.x + d * s * 0.4f, c.y);
            p.lineTo (c.x - d * s * 0.4f, c.y + s * 0.8f);
            g.strokePath (p, stroke);
            break;
        }
        case Icon::Gear:
        {
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                p.addLineSegment ({ c.getPointOnCircumference (s * 0.6f, a), c.getPointOnCircumference (s * 1.15f, a) }, 2.2f);
            }
            g.fillPath (p);
            g.drawEllipse (juce::Rectangle<float> (s * 1.4f, s * 1.4f).withCentre (c), 1.8f);
            break;
        }
        case Icon::Piano:
        {
            auto k = juce::Rectangle<float> (s * 2.6f, s * 1.8f).withCentre (c);
            g.drawRect (k, 1.2f);
            for (int i = 1; i < 4; ++i)
                g.drawVerticalLine ((int) (k.getX() + k.getWidth() * (float) i / 4.0f), k.getY(), k.getBottom());
            for (int i : { 1, 2 })
                g.fillRect (k.getX() + k.getWidth() * (float) i / 4.0f - s * 0.18f, k.getY(), s * 0.36f, k.getHeight() * 0.55f);
            break;
        }
        case Icon::Pencil:
        {
            p.startNewSubPath (c.x - s, c.y + s);
            p.lineTo (c.x - s * 0.7f, c.y + s * 0.2f);
            p.lineTo (c.x + s * 0.6f, c.y - s * 1.1f);
            p.lineTo (c.x + s * 1.1f, c.y - s * 0.6f);
            p.lineTo (c.x - s * 0.2f, c.y + s * 0.7f);
            p.closeSubPath();
            g.strokePath (p, stroke);
            break;
        }
        case Icon::Phase:
        {
            g.drawEllipse (juce::Rectangle<float> (s * 1.6f, s * 1.6f).withCentre (c), 1.6f);
            g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 1.6f);
            break;
        }
    }
}
} // namespace tg::gui
