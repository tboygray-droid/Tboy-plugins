#include "BottomBar.h"
#include "state/FactoryPresets.h"

namespace tg::gui
{
BottomBar::BottomBar (TgsoundsEQAudioProcessor& p) : proc (p)
{
    auto& um = proc.getUndoManager();
    undoBtn.setTooltip ("Undo (Ctrl/Cmd+Z)");
    redoBtn.setTooltip ("Redo (Ctrl/Cmd+Shift+Z)");
    undoBtn.onClick = [&um] { um.undo(); };
    redoBtn.onClick = [&um] { um.redo(); };

    for (auto* b : { &aBtn, &bBtn })
    {
        b->setClickingTogglesState (false);
        b->setTooltip ("A/B compare: switch between two settings.");
    }
    aBtn.onClick = [this] { proc.getAB().select (0); };
    bBtn.onClick = [this] { proc.getAB().select (1); };
    copyBtn.setTooltip ("Copy the active slot to the other one (A->B or B->A).");
    copyBtn.onClick = [this] { proc.getAB().copyActiveToOther(); };

    prevBtn.onClick = [this] { proc.getPresets().loadNext (-1); };
    nextBtn.onClick = [this] { proc.getPresets().loadNext (+1); };
    prevBtn.setTooltip ("Previous preset");
    nextBtn.setTooltip ("Next preset");
    presetBtn.setTooltip ("Preset browser");
    presetBtn.onClick = [this] { showPresetMenu(); };
    saveBtn.setTooltip ("Save the current settings as a user preset.");
    saveBtn.onClick = [this] { savePresetDialog(); };

    modeCombo.bind (params::global::mode, "Zero Latency: classic min-phase (bilinear).\nNatural Phase: min-phase, matched to the analog response (no cramping).\nLinear Phase: FIR, adds latency.");
    qualityCombo.bind (params::global::lpQuality, "Linear-phase FIR length (more = better low-frequency precision, more latency).");
    osCombo.bind (params::global::os, "Oversampling for the minimum-phase modes (accurate HF bells/shelves).");

    analyzerCombo.addItemList ({ "Analyzer Off", "Pre", "Post", "Pre + Post", "Sidechain" }, 1);
    analyzerCombo.setTooltip ("Spectrum analyzer source.");
    analyzerCombo.setSelectedItemIndex ((int) proc.getUiState().getProperty ("anaMode", 3), juce::dontSendNotification);
    analyzerCombo.onChange = [this] { setUi ("anaMode", analyzerCombo.getSelectedItemIndex()); };
    analyzerSettings.setTooltip ("Analyzer range, resolution, speed, tilt and peak hold.");
    analyzerSettings.onClick = [this] { showAnalyzerMenu(); };

    scaleCombo.addItemList ({ "+-3 dB", "+-6 dB", "+-12 dB", "+-30 dB" }, 1);
    scaleCombo.setTooltip ("Display scale of the EQ curve.");
    scaleCombo.setSelectedItemIndex ((int) proc.getUiState().getProperty ("scale", 2), juce::dontSendNotification);
    scaleCombo.onChange = [this] { setUi ("scale", scaleCombo.getSelectedItemIndex()); };

    pianoBtn.setClickingTogglesState (true);
    pianoBtn.setToggleState ((bool) proc.getUiState().getProperty ("piano", false), juce::dontSendNotification);
    pianoBtn.setTooltip ("Show a piano keyboard under the display.");
    pianoBtn.onClick = [this] { setUi ("piano", pianoBtn.getToggleState()); };

    drawBtn.setClickingTogglesState (true);
    drawBtn.setTooltip ("Draw mode: sketch a curve and let TgsoundsEQ fit bands to it.");
    drawBtn.onClick = [this] { if (onDrawModeToggled) onDrawModeToggled (drawBtn.getToggleState()); };

    matchBtn.setTooltip ("Match EQ: capture a reference and a source and generate correction bands.");
    matchBtn.onClick = [this] { if (onMatchClicked) onMatchClicked (matchBtn); };

    autoGainBtn.bind (params::global::autoGain, "Auto gain: compensates the average level change of the EQ curve.");
    phaseBtn.bind (params::global::phaseInv, "Invert output polarity.");
    outGain.bind (params::global::outGain, "Output gain");
    outPan.bind (params::global::outPan, "Output balance");
    outGain.setShowValue (false);
    outPan.setShowValue (false);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &undoBtn, &redoBtn, &aBtn, &bBtn, &copyBtn, &prevBtn, &presetBtn, &nextBtn, &saveBtn,
             &modeCombo, &qualityCombo, &osCombo, &analyzerCombo, &analyzerSettings, &scaleCombo,
             &pianoBtn, &drawBtn, &matchBtn, &autoGainBtn, &phaseBtn, &outGain, &outPan, &meter })
        addAndMakeVisible (c);
    startTimerHz (10);
    timerCallback();
}

BottomBar::~BottomBar() { stopTimer(); }

void BottomBar::setUi (const juce::Identifier& id, const juce::var& v)
{
    proc.getUiState().setProperty (id, v, nullptr);
    if (onUiStateChanged) onUiStateChanged();
}

void BottomBar::timerCallback()
{
    auto& um = proc.getUndoManager();
    undoBtn.setEnabled (um.canUndo());
    redoBtn.setEnabled (um.canRedo());
    const int ab = proc.getAB().getActive();
    aBtn.setToggleState (ab == 0, juce::dontSendNotification);
    bBtn.setToggleState (ab == 1, juce::dontSendNotification);
    copyBtn.setButtonText (ab == 0 ? "A>B" : "B>A");
    presetBtn.setButtonText (proc.getPresets().getCurrentPresetName());
    const bool lp = (int) proc.getParamRefs().global.mode->load() == (int) params::Mode::LinearPhase;
    qualityCombo.setEnabled (lp);
    osCombo.setEnabled (! lp);
}

void BottomBar::showPresetMenu()
{
    auto& pm = proc.getPresets();
    const auto all = pm.getAllPresets();
    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> cats;
    juce::StringArray order;
    for (int i = 0; i < all.size(); ++i)
    {
        const auto& e = all[i];
        if (! order.contains (e.category)) order.add (e.category);
        cats[e.category].addItem (i + 1, e.name, true, e.name == pm.getCurrentPresetName());
    }
    for (auto& c : order)
        menu.addSubMenu (c, cats[c]);
    menu.addSeparator();
    menu.addItem (10001, "Save As...");
    menu.addItem (10002, "Open User Preset Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [this, all] (int r)
    {
        if (r >= 1 && r <= all.size()) proc.getPresets().loadPreset (all[r - 1]);
        else if (r == 10001) savePresetDialog();
        else if (r == 10002)
        {
            auto dir = state::PresetManager::getUserPresetDirectory();
            dir.createDirectory();
            dir.startAsProcess();
        }
    });
}

void BottomBar::savePresetDialog()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save Preset", "Name for the new user preset:", juce::MessageBoxIconType::NoIcon);
    saveDialog->addTextEditor ("name", proc.getPresets().getCurrentPresetName());
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveDialog != nullptr)
            proc.getPresets().saveUserPreset (saveDialog->getTextEditorContents ("name"));
        saveDialog.reset();
    }), false);
}

void BottomBar::showAnalyzerMenu()
{
    auto ui = proc.getUiState();
    juce::PopupMenu range, res, speed, tilt;
    const char* ranges[] { "60 dB", "90 dB", "120 dB" };
    for (int i = 0; i < 3; ++i) range.addItem (100 + i, ranges[i], true, (int) ui.getProperty ("anaRange", 1) == i);
    const char* resNames[] { "Low (4096)", "Medium (8192)", "High (16384)", "Maximum (32768)" };
    for (int i = 0; i < 4; ++i) res.addItem (200 + i, resNames[i], true, (int) ui.getProperty ("anaRes", 1) == i);
    const char* speeds[] { "Very Slow", "Slow", "Medium", "Fast", "Very Fast" };
    for (int i = 0; i < 5; ++i) speed.addItem (300 + i, speeds[i], true, (int) ui.getProperty ("anaSpeed", 2) == i);
    const float tilts[] { 0.0f, 1.5f, 3.0f, 4.5f };
    for (int i = 0; i < 4; ++i)
        tilt.addItem (400 + i, juce::String (tilts[i], 1) + " dB/oct", true, std::abs ((float) ui.getProperty ("anaTilt", 4.5f) - tilts[i]) < 0.01f);

    juce::PopupMenu m;
    m.addSubMenu ("Range", range);
    m.addSubMenu ("Resolution", res);
    m.addSubMenu ("Speed", speed);
    m.addSubMenu ("Tilt", tilt);
    m.addItem (500, "Peak Hold", true, (bool) ui.getProperty ("anaPeak", false));
    m.addItem (501, "Reset Peaks");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&analyzerSettings), [this, tilts] (int r)
    {
        if (r >= 400 && r < 404) setUi ("anaTilt", tilts[r - 400]);
        else if (r >= 300) { if (r == 500) setUi ("anaPeak", ! (bool) proc.getUiState().getProperty ("anaPeak", false));
                             else if (r == 501) proc.getAnalyzer().resetPeaks();
                             else setUi ("anaSpeed", r - 300); }
        else if (r >= 200) setUi ("anaRes", r - 200);
        else if (r >= 100) setUi ("anaRange", r - 100);
    });
}

void BottomBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void BottomBar::resized()
{
    using Item = juce::FlexItem;
    auto make = [] { juce::FlexBox fb; fb.alignItems = juce::FlexBox::AlignItems::center; return fb; };
    auto add = [] (juce::FlexBox& fb, juce::Component& c, float w, float minW, float h = 26.0f, float grow = 0.0f, float marginLeft = 3.0f)
    {
        fb.items.add (Item (c).withWidth (w).withMinWidth (minW).withHeight (h).withFlex (grow, 1.0f).withMargin ({ 0, 0, 0, marginLeft }));
    };

    auto row1 = make(), row2 = make();
    const bool single = getWidth() >= kSingleRowWidth;
    auto& second = single ? row1 : row2;

    add (row1, undoBtn, 28, 24); add (row1, redoBtn, 28, 24);
    add (row1, aBtn, 26, 22, 26, 0, 10); add (row1, bBtn, 26, 22); add (row1, copyBtn, 40, 34);
    add (row1, prevBtn, 22, 18, 26, 0, 10); add (row1, presetBtn, 170, 90, 26, 1.0f); add (row1, nextBtn, 22, 18); add (row1, saveBtn, 40, 34);
    add (row1, modeCombo, 112, 92, 26, 0, 10); add (row1, qualityCombo, 72, 60); add (row1, osCombo, 52, 46);

    add (second, analyzerCombo, 96, 80, 26, 0, single ? 10.0f : 0.0f); add (second, analyzerSettings, 26, 22); add (second, scaleCombo, 68, 60);
    add (second, pianoBtn, 30, 24, 26, 0, 10); add (second, drawBtn, 30, 24); add (second, matchBtn, 52, 44);
    if (! single)
        second.items.add (Item().withFlex (1.0f));   // push the output section to the right
    add (second, autoGainBtn, 44, 38, 26, 0, 10); add (second, phaseBtn, 36, 32);
    add (second, outGain, 40, 36, 48, 0, 6); add (second, outPan, 40, 36, 48);
    add (second, meter, 22, 18, 42, 0, 6);

    // Keep clear of the editor's resize grip in the bottom-right corner.
    auto area = getLocalBounds().reduced (6, 2).withTrimmedRight (14);
    if (single)
    {
        row1.performLayout (area);
    }
    else
    {
        row1.performLayout (area.removeFromTop (38));
        row2.performLayout (area);
    }
}
} // namespace tg::gui
