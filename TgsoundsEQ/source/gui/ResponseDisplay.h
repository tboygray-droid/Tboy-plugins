#pragma once

#include "Theme.h"
#include "BandAccess.h"
#include "dsp/FilterDesign.h"

namespace tg::gui
{
/**
    The big frequency-response display.

    Draws (back to front): grid + labels, analyzer spectra (filled gradient + glow),
    per-band translucent fills, the summed response, band nodes, collision warnings,
    draw-mode sketch, spectrum-grab highlight, piano overlay.

    Interaction summary (also in the tooltip):
      double-click empty  -> add band        drag node      -> freq / gain (Q for cut shapes)
      shift-drag          -> fine control    wheel on node  -> Q
      alt-click node      -> delete          right-click    -> shape menu
      double-click node   -> enable/disable  hover a spectrum peak + drag -> grab a new band
*/
class ResponseDisplay : public juce::Component,
                        public juce::TooltipClient
{
public:
    explicit ResponseDisplay (TgsoundsEQAudioProcessor& p);
    ~ResponseDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

    int getSelectedBand() const noexcept { return selected; }
    void setSelectedBand (int b);
    /** Node position in this component's coordinates (for placing the band panel). */
    juce::Point<float> getNodePosition (int band) const;

    void setDrawMode (bool on);
    bool isDrawMode() const noexcept { return drawMode; }

    /** Called once per frame by the editor (vblank). */
    void tick();

    std::function<void (int)> onSelectionChanged;
    std::function<void()> onDrawModeFinished;

    // Coordinate mapping (public for tests / editor)
    float freqToX (double hz) const noexcept;
    double xToFreq (float x) const noexcept;
    float dbToY (double db) const noexcept;
    double yToDb (float y) const noexcept;
    float analyzerDbToY (float db) const noexcept;

    /** List of overlapping band pairs (for warnings). */
    std::vector<std::pair<int, int>> findCollisions() const;

private:
    struct BandCurve
    {
        params::BandSnapshot snap;
        float dynGain = 0.0f;
        double rate = 0.0;
        int mode = -1;
        std::vector<float> db;       // static response
        std::vector<float> dynDb;    // response including the current dynamic gain
        bool valid = false;
    };

    void updateCurves();
    void computeBandCurve (int b, const params::BandSnapshot& s, float dynGain, std::vector<float>& out) const;
    void updateAnalyzer();
    int hitTestNode (juce::Point<float> p) const;
    void showBandMenu (int band);
    void beginDrag (int band, const juce::MouseEvent& e);
    void endDrag();
    void findSpectrumGrab (juce::Point<float> p);
    void finishDrawing();

    void drawGrid (juce::Graphics&);
    void drawAnalyzer (juce::Graphics&);
    void drawCurves (juce::Graphics&);
    void drawNodes (juce::Graphics&);
    void drawPiano (juce::Graphics&);
    void drawOverlays (juce::Graphics&);

    float scaleDb() const;

    TgsoundsEQAudioProcessor& proc;
    BandAccess bands;
    juce::Rectangle<float> plot;

    std::array<BandCurve, params::kNumBands> curves;
    std::vector<double> xFreqs;             // frequency of each curve point
    std::vector<float> sumDb;
    int numPoints = 0;

    // analyzer
    std::vector<float> specPre, specPost, specSc, specPeak;
    bool specValid = false;

    // interaction
    int selected = -1, hovered = -1, dragging = -1;
    juce::Point<float> dragStart;
    float dragStartFreq = 0, dragStartGain = 0, dragStartQ = 0;
    bool dragIsQ = false;
    struct Grab { bool active = false; double freq = 0; float y = 0; double q = 4; } grab;
    bool grabDragging = false;

    bool drawMode = false;
    std::vector<float> sketch;   // dB per x pixel, NaN where not drawn
    bool sketching = false;
    int lastSketchX = -1;
    float lastSketchDb = 0;
};
} // namespace tg::gui
