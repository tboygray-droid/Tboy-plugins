#include "PluginEditor.h"
#include "gui/MatchPanel.h"

using namespace tg;

TgsoundsEQAudioProcessorEditor::TgsoundsEQAudioProcessorEditor (TgsoundsEQAudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), display (p), bandPanel (p), bottomBar (p),
      vblank (this, [this] { display.tick(); })
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);   // popup menus / alert windows
    addAndMakeVisible (display);
    addAndMakeVisible (bottomBar);
    addChildComponent (bandPanel);

    display.onSelectionChanged = [this] (int b)
    {
        bandPanel.setBand (b);
        positionBandPanel();
    };
    display.onDrawModeFinished = [this] { bottomBar.setDrawModeButton (false); };
    bandPanel.onDeleteRequested = [this]
    {
        const int b = bandPanel.getBand();
        if (b >= 0) gui::BandAccess (proc).deleteBand (b);
        display.setSelectedBand (-1);
    };
    bandPanel.onLayoutChanged = [this] { positionBandPanel(); };
    bottomBar.onUiStateChanged = [this] { applyUiState(); };
    bottomBar.onDrawModeToggled = [this] (bool on) { display.setDrawMode (on); };
    bottomBar.onMatchClicked = [this] (juce::Component& c) { showMatchPanel (c); };

    // GPU-accelerated rendering of all components. Can be disabled for troubleshooting.
    if (! juce::SystemStats::getEnvironmentVariable ("TGSOUNDSEQ_NO_OPENGL", {}).isNotEmpty())
    {
        openGL.setComponentPaintingEnabled (true);
        openGL.setContinuousRepainting (false);
        openGL.attachTo (*this);
    }

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (960, 540, 3000, 2000);
    auto ui = proc.getUiState();
    setSize ((int) ui.getProperty ("width", 1240), (int) ui.getProperty ("height", 700));
    applyUiState();
    startTimerHz (30);
}

TgsoundsEQAudioProcessorEditor::~TgsoundsEQAudioProcessorEditor()
{
    stopTimer();
    proc.getAnalyzer().setActive (false);
    proc.setSoloBand (-1);
    openGL.detach();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void TgsoundsEQAudioProcessorEditor::applyUiState()
{
    auto ui = proc.getUiState();
    analyzer::AnalyzerSettings s;
    s.resolution = (int) ui.getProperty ("anaRes", 1);
    s.speed = (int) ui.getProperty ("anaSpeed", 2);
    s.tiltDbPerOct = (float) ui.getProperty ("anaTilt", 4.5f);
    s.peakHold = (bool) ui.getProperty ("anaPeak", false);
    s.mode = (analyzer::DisplayMode) juce::jlimit (0, 4, (int) ui.getProperty ("anaMode", 3));
    auto& hub = proc.getAnalyzer();
    hub.setSettings (s);
    hub.setActive (s.mode != analyzer::DisplayMode::Off);
    display.resized();   // piano strip changes the plot area
    display.repaint();
}

void TgsoundsEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace gui::theme;
    g.fillAll (background);
    auto header = getLocalBounds().removeFromTop (30).toFloat();
    g.setGradientFill (juce::ColourGradient (panel, 0, 0, background, 0, header.getBottom(), false));
    g.fillRect (header);

    // Logo: a small stylised EQ curve + wordmark
    juce::Path mark;
    const float x0 = 12, y0 = 15;
    mark.startNewSubPath (x0, y0 + 4);
    mark.cubicTo (x0 + 5, y0 + 4, x0 + 6, y0 - 7, x0 + 11, y0 - 7);
    mark.cubicTo (x0 + 16, y0 - 7, x0 + 16, y0 + 6, x0 + 22, y0 + 6);
    g.setColour (accent);
    g.strokePath (mark, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (text);
    g.setFont (font (16.0f, true));
    g.drawText ("Tgsounds", 40, 5, 80, 20, juce::Justification::centredLeft);
    g.setColour (accent);
    g.drawText ("EQ", 112, 5, 30, 20, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (font (11.5f));
    g.drawText ("24-band dynamic parametric EQ   |   latency " + juce::String (proc.getLatencySamples()) + " smp",
                header.withTrimmedLeft (150).withTrimmedRight (12).toNearestInt(), juce::Justification::centredRight);
}

void TgsoundsEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (30);
    bottomBar.setBounds (r.removeFromBottom (gui::BottomBar::preferredHeight (getWidth())));
    display.setBounds (r);
    positionBandPanel();

    auto ui = proc.getUiState();
    ui.setProperty ("width", getWidth(), nullptr);
    ui.setProperty ("height", getHeight(), nullptr);
}

void TgsoundsEQAudioProcessorEditor::positionBandPanel()
{
    const int b = bandPanel.getBand();
    if (b < 0) return;
    const auto size = bandPanel.getPreferredSize();
    const auto node = display.getNodePosition (b).toInt() + display.getPosition();
    auto area = display.getBounds().reduced (6);
    int x = node.x - size.x / 2;
    int y = node.y + 22;
    if (y + size.y > area.getBottom())
        y = node.y - 22 - size.y;
    x = juce::jlimit (area.getX(), area.getRight() - size.x, x);
    y = juce::jlimit (area.getY(), area.getBottom() - size.y, y);
    bandPanel.setBounds (x, y, size.x, size.y);
}

void TgsoundsEQAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastMeterTime > 0 ? (float) (now - lastMeterTime) : 1.0f / 30.0f;
    lastMeterTime = now;
    bottomBar.getMeter().push (proc.takeOutputPeak (0), proc.takeOutputPeak (1), dt);

    // Selected band deleted elsewhere (undo, automation)?
    const int sel = display.getSelectedBand();
    if (sel >= 0 && ! proc.getParamRefs().snapshot (sel).used)
        display.setSelectedBand (-1);
    if (bandPanel.isVisible() && ! bandPanel.isMouseButtonDown (true))
        positionBandPanel();
    repaint (0, 0, getWidth(), 30);   // header shows latency
}

bool TgsoundsEQAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    auto& um = proc.getUndoManager();
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { um.undo(); return true; }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) { um.redo(); return true; }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && display.getSelectedBand() >= 0)
    {
        gui::BandAccess (proc).deleteBand (display.getSelectedBand());
        display.setSelectedBand (-1);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (display.isDrawMode()) { display.setDrawMode (false); bottomBar.setDrawModeButton (false); }
        else display.setSelectedBand (-1);
        return true;
    }
    return false;
}

void TgsoundsEQAudioProcessorEditor::showMatchPanel (juce::Component& anchor)
{
    auto panel = std::make_unique<gui::MatchPanel> (proc);
    juce::CallOutBox::launchAsynchronously (std::move (panel), getLocalArea (&anchor, anchor.getLocalBounds()), this);
}
