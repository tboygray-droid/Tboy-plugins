#include "ResponseDisplay.h"
#include "fit/CurveFitter.h"
#include "dsp/LinearPhaseEngine.h"

namespace tg::gui
{
using params::Shape;
namespace pb = params::band;

namespace
{
    constexpr double fMin = params::kFreqMin, fMax = params::kFreqMax;
    constexpr float nodeRadius = 7.5f;
    const float scales[] { 3.0f, 6.0f, 12.0f, 30.0f };
    const float analyzerRanges[] { 60.0f, 90.0f, 120.0f };

    juce::String freqText (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (hz, hz < 100 ? 1 : 0) + " Hz";
    }

    /** Bandwidth in octaves of a bell with quality Q. */
    double octavesFromQ (double q) { return 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * q)); }
}

ResponseDisplay::ResponseDisplay (TgsoundsEQAudioProcessor& p) : proc (p), bands (p)
{
    setOpaque (true);
    setRepaintsOnMouseActivity (false);
}

ResponseDisplay::~ResponseDisplay() = default;

// ---- mapping ----------------------------------------------------------------------
float ResponseDisplay::freqToX (double hz) const noexcept
{
    return plot.getX() + plot.getWidth() * (float) (std::log (hz / fMin) / std::log (fMax / fMin));
}

double ResponseDisplay::xToFreq (float x) const noexcept
{
    return fMin * std::exp ((double) (x - plot.getX()) / plot.getWidth() * std::log (fMax / fMin));
}

float ResponseDisplay::scaleDb() const
{
    return scales[juce::jlimit (0, 3, (int) proc.getUiState().getProperty ("scale", 2))];
}

float ResponseDisplay::dbToY (double db) const noexcept
{
    const float span = plot.getHeight() * 0.5f - 10.0f;
    return plot.getCentreY() - (float) db / scaleDb() * span;
}

double ResponseDisplay::yToDb (float y) const noexcept
{
    const float span = plot.getHeight() * 0.5f - 10.0f;
    return (plot.getCentreY() - y) / span * scaleDb();
}

float ResponseDisplay::analyzerDbToY (float db) const noexcept
{
    const float range = analyzerRanges[juce::jlimit (0, 2, (int) proc.getUiState().getProperty ("anaRange", 1))];
    const float top = 6.0f;
    return plot.getY() + (top - db) / (range + top) * plot.getHeight();
}

void ResponseDisplay::resized()
{
    const bool piano = (bool) proc.getUiState().getProperty ("piano", false);
    plot = getLocalBounds().toFloat().withTrimmedLeft (6).withTrimmedRight (36).withTrimmedTop (8)
                           .withTrimmedBottom (piano ? 36.0f : 20.0f);
    numPoints = juce::jlimit (64, 900, (int) plot.getWidth() / 2);
    xFreqs.resize ((size_t) numPoints);
    for (int i = 0; i < numPoints; ++i)
        xFreqs[(size_t) i] = fMin * std::pow (fMax / fMin, (double) i / (numPoints - 1));
    for (auto& c : curves) c.valid = false;
    sumDb.assign ((size_t) numPoints, 0.0f);
    specPre.assign ((size_t) numPoints, -200.0f);
    specPost = specSc = specPeak = specPre;
    sketch.assign ((size_t) getWidth(), std::numeric_limits<float>::quiet_NaN());
}

// ---- data ------------------------------------------------------------------------------
void ResponseDisplay::computeBandCurve (int, const params::BandSnapshot& s, float dynGain, std::vector<float>& out) const
{
    out.resize ((size_t) numPoints);
    const auto mode = (params::Mode) (int) proc.getParamRefs().global.mode->load();
    dsp::BandDesign d { s.shape, s.slope, s.freq, s.gain + dynGain, s.q };

    if (mode == params::Mode::LinearPhase)
    {
        const auto a = dsp::makeAnalog (d);
        for (int i = 0; i < numPoints; ++i)
            out[(size_t) i] = (float) a.magnitudeDb (xFreqs[(size_t) i]);
        return;
    }
    const double rate = proc.getProcessingRate();
    const auto c = dsp::designDigital (d, rate, mode == params::Mode::NaturalPhase ? dsp::DesignMethod::Matched
                                                                                   : dsp::DesignMethod::Bilinear);
    for (int i = 0; i < numPoints; ++i)
        out[(size_t) i] = (float) c.magnitudeDb (std::min (xFreqs[(size_t) i], 0.4999 * rate), rate);
}

void ResponseDisplay::updateCurves()
{
    const double rate = proc.getProcessingRate();
    const int mode = (int) proc.getParamRefs().global.mode->load();
    std::fill (sumDb.begin(), sumDb.end(), 0.0f);

    for (int b = 0; b < params::kNumBands; ++b)
    {
        auto& c = curves[(size_t) b];
        const auto s = bands.snapshot (b);
        const float dyn = s.dynOn && s.isActive() ? proc.getDynamicGainDb (b) : 0.0f;
        const bool same = c.valid && c.rate == rate && c.mode == mode && c.snap.used == s.used && c.snap.shape == s.shape
                          && c.snap.slope == s.slope && c.snap.freq == s.freq && c.snap.gain == s.gain && c.snap.q == s.q
                          && c.snap.enabled == s.enabled;
        if (! same && s.used)
            computeBandCurve (b, s, 0.0f, c.db);
        if (s.used && (! same || std::abs (dyn - c.dynGain) > 0.05f))
        {
            if (std::abs (dyn) > 0.05f) computeBandCurve (b, s, dyn, c.dynDb);
            else c.dynDb = c.db;
            c.dynGain = dyn;
        }
        c.snap = s;
        c.rate = rate;
        c.mode = mode;
        c.valid = true;

        if (s.isActive())
            for (int i = 0; i < numPoints; ++i)
                sumDb[(size_t) i] += c.dynDb[(size_t) i];
    }
}

void ResponseDisplay::updateAnalyzer()
{
    auto& hub = proc.getAnalyzer();
    specValid = false;
    if (! hub.isActive())
        return;
    const auto s = hub.getSettings();
    using analyzer::Source;
    using analyzer::DisplayMode;
    if (s.mode == DisplayMode::Pre || s.mode == DisplayMode::PrePost)
        hub.fillCurve (Source::Pre, false, (float) fMin, (float) fMax, specPre.data(), numPoints);
    if (s.mode == DisplayMode::Post || s.mode == DisplayMode::PrePost)
        hub.fillCurve (Source::Post, false, (float) fMin, (float) fMax, specPost.data(), numPoints);
    if (s.mode == DisplayMode::Sidechain)
        hub.fillCurve (Source::Sidechain, false, (float) fMin, (float) fMax, specSc.data(), numPoints);
    if (s.peakHold)
        hub.fillCurve (s.mode == DisplayMode::Pre ? Source::Pre : (s.mode == DisplayMode::Sidechain ? Source::Sidechain : Source::Post),
                       true, (float) fMin, (float) fMax, specPeak.data(), numPoints);
    specValid = true;
}

void ResponseDisplay::tick()
{
    if (numPoints == 0)
        return;
    updateAnalyzer();
    updateCurves();
    repaint();
}

std::vector<std::pair<int, int>> ResponseDisplay::findCollisions() const
{
    std::vector<std::pair<int, int>> out;
    for (int a = 0; a < params::kNumBands; ++a)
    {
        const auto sa = bands.snapshot (a);
        if (! sa.isActive() || ! params::shapeHasGain (sa.shape) || std::abs (sa.gain) < 0.5f)
            continue;
        for (int b = a + 1; b < params::kNumBands; ++b)
        {
            const auto sb = bands.snapshot (b);
            if (! sb.isActive() || ! params::shapeHasGain (sb.shape) || std::abs (sb.gain) < 0.5f)
                continue;
            const bool samePlace = sa.placement == sb.placement || sa.placement == params::Placement::Stereo
                                   || sb.placement == params::Placement::Stereo;
            if (! samePlace || sa.shape != sb.shape)
                continue;
            // Overlap: centres closer than a quarter of the narrower bandwidth (or 1/12 octave).
            const double dist = std::abs (std::log2 (sa.freq / sb.freq));
            const double bw = std::min (octavesFromQ (sa.q), octavesFromQ (sb.q));
            if (dist < std::max (1.0 / 12.0, 0.25 * bw))
                out.emplace_back (a, b);
        }
    }
    return out;
}

// ---- painting ------------------------------------------------------------------------
void ResponseDisplay::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    drawGrid (g);
    drawAnalyzer (g);
    drawCurves (g);
    drawNodes (g);
    drawOverlays (g);
    if ((bool) proc.getUiState().getProperty ("piano", false))
        drawPiano (g);
}

void ResponseDisplay::drawGrid (juce::Graphics& g)
{
    g.setColour (theme::background.brighter (0.03f));
    g.fillRect (plot);

    // Beyond-Nyquist shading (base sample rate).
    const double nyq = proc.getBaseSampleRate() * 0.5;
    if (nyq < fMax)
    {
        const float x = freqToX (nyq);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY(), plot.getRight() - x, plot.getHeight()));
    }

    g.setFont (theme::font (10.5f));
    // Frequency lines
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
        for (int m = 1; m <= 9; ++m)
        {
            const double f = decade * m;
            if (f < fMin || f > fMax) continue;
            const float x = freqToX (f);
            g.setColour (m == 1 ? theme::gridStrong : theme::grid);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f);
        g.setColour (theme::textDim);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 14), juce::Justification::centred);
    }

    // dB lines (EQ scale on the right)
    const float S = scaleDb();
    const float step = S <= 3 ? 1.0f : S <= 6 ? 2.0f : S <= 12 ? 3.0f : 6.0f;
    for (float db = -S; db <= S + 0.01f; db += step)
    {
        const float y = dbToY (db);
        g.setColour (std::abs (db) < 0.01f ? theme::gridStrong.brighter (0.25f) : theme::grid);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (std::abs (db) < 0.01f ? theme::text : theme::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getRight() + 3, y - 7, 30, 14),
                    juce::Justification::centredLeft);
    }

    // Analyzer scale (left, subtle)
    if (proc.getAnalyzer().isActive())
    {
        const float range = analyzerRanges[juce::jlimit (0, 2, (int) proc.getUiState().getProperty ("anaRange", 1))];
        g.setColour (theme::textDim.withAlpha (0.55f));
        g.setFont (theme::font (9.5f));
        for (float db = -12.0f; db >= -range; db -= (range > 90 ? 24.0f : 12.0f))
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 2, analyzerDbToY (db) - 6, 30, 12),
                        juce::Justification::centredLeft);
    }
}

void ResponseDisplay::drawAnalyzer (juce::Graphics& g)
{
    if (! specValid)
        return;
    const auto s = proc.getAnalyzer().getSettings();
    using analyzer::DisplayMode;

    auto makePath = [this] (const std::vector<float>& v, bool closed)
    {
        juce::Path p;
        const float bottom = plot.getBottom();
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = freqToX (xFreqs[(size_t) i]);
            const float y = juce::jlimit (plot.getY(), bottom, analyzerDbToY (v[(size_t) i]));
            if (i == 0) { if (closed) { p.startNewSubPath (x, bottom); p.lineTo (x, y); } else p.startNewSubPath (x, y); }
            else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (plot.getRight(), bottom); p.closeSubPath(); }
        return p;
    };

    auto drawSpectrum = [&] (const std::vector<float>& v, juce::Colour c, float fillAlpha, bool glow)
    {
        const auto fill = makePath (v, true);
        g.setGradientFill (juce::ColourGradient (c.withAlpha (fillAlpha), 0, plot.getY() + plot.getHeight() * 0.25f,
                                                 c.withAlpha (0.0f), 0, plot.getBottom(), false));
        g.fillPath (fill);
        const auto line = makePath (v, false);
        if (glow)
        {
            // Soft glow: a few wide, faint strokes under the line.
            for (float w : { 7.0f, 4.0f })
            {
                g.setColour (c.withAlpha (0.06f));
                g.strokePath (line, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        g.setColour (c.withAlpha (0.75f));
        g.strokePath (line, juce::PathStrokeType (1.1f));
    };

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (plot.toNearestInt());
    if (s.mode == DisplayMode::Pre || s.mode == DisplayMode::PrePost)
        drawSpectrum (specPre, theme::analyzerPre, s.mode == DisplayMode::PrePost ? 0.25f : 0.4f, s.mode == DisplayMode::Pre);
    if (s.mode == DisplayMode::Post || s.mode == DisplayMode::PrePost)
        drawSpectrum (specPost, theme::analyzerPost, 0.28f, true);
    if (s.mode == DisplayMode::Sidechain)
        drawSpectrum (specSc, theme::analyzerSc, 0.3f, true);
    if (s.peakHold)
    {
        g.setColour (theme::text.withAlpha (0.45f));
        g.strokePath (makePath (specPeak, false), juce::PathStrokeType (1.0f));
    }
}

void ResponseDisplay::drawCurves (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (plot.toNearestInt());
    const float zeroY = dbToY (0.0);

    auto curvePath = [this] (const std::vector<float>& db)
    {
        juce::Path p;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = freqToX (xFreqs[(size_t) i]);
            const float y = juce::jlimit (plot.getY() - 50.0f, plot.getBottom() + 50.0f, dbToY (db[(size_t) i]));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    for (int b = 0; b < params::kNumBands; ++b)
    {
        const auto& c = curves[(size_t) b];
        if (! c.valid || ! c.snap.used || c.db.size() != (size_t) numPoints)
            continue;
        const auto col = c.snap.enabled ? theme::bandColour (b) : theme::textDim;
        const bool focus = b == selected || b == hovered;

        // Translucent area between the band curve and 0 dB
        auto p = curvePath (c.db);
        juce::Path area (p);
        area.lineTo (plot.getRight(), zeroY);
        area.lineTo (plot.getX(), zeroY);
        area.closeSubPath();
        g.setColour (col.withAlpha (c.snap.enabled ? (focus ? 0.30f : 0.16f) : 0.06f));
        g.fillPath (area);
        g.setColour (col.withAlpha (focus ? 0.9f : 0.35f));
        g.strokePath (p, juce::PathStrokeType (focus ? 1.6f : 1.0f));

        // Dynamic movement: lighter outline at the current dynamic gain
        if (c.snap.dynOn && std::abs (c.dynGain) > 0.05f && c.dynDb.size() == (size_t) numPoints)
        {
            g.setColour (col.withAlpha (0.9f));
            const float dashes[] { 4.0f, 3.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.4f).createDashedStroke (dashed, curvePath (c.dynDb), dashes, 2);
            g.fillPath (dashed);
        }
    }

    // Summed response with glow
    const auto sum = curvePath (sumDb);
    for (float w : { 8.0f, 4.5f })
    {
        g.setColour (theme::accent.withAlpha (0.08f));
        g.strokePath (sum, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (juce::Colour (0xffeaf6f4));
    g.strokePath (sum, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Point<float> ResponseDisplay::getNodePosition (int b) const
{
    const auto s = bands.snapshot (b);
    const float y = params::shapeHasGain (s.shape) ? dbToY (s.gain) : dbToY (0.0);
    return { freqToX (s.freq), juce::jlimit (plot.getY() + 4.0f, plot.getBottom() - 4.0f, y) };
}

void ResponseDisplay::drawNodes (juce::Graphics& g)
{
    const auto collisions = findCollisions();
    auto collides = [&collisions] (int b)
    {
        for (auto& c : collisions) if (c.first == b || c.second == b) return true;
        return false;
    };

    for (int b = 0; b < params::kNumBands; ++b)
    {
        const auto& c = curves[(size_t) b];
        if (! c.snap.used)
            continue;
        const auto pos = getNodePosition (b);
        const auto col = theme::bandColour (b);
        const bool focus = b == selected || b == hovered;
        const float r = nodeRadius + (focus ? 1.5f : 0.0f);

        // Dynamic range indicator
        if (c.snap.dynOn && params::shapeHasGain (c.snap.shape))
        {
            const float sign = c.snap.dynDir == params::DynDir::Up ? 1.0f : -1.0f;
            const float yEnd = dbToY (c.snap.gain + sign * c.snap.range);
            const float yNow = dbToY (c.snap.gain + c.dynGain);
            g.setColour (col.withAlpha (0.35f));
            g.drawLine (pos.x, pos.y, pos.x, yEnd, 3.0f);
            g.setColour (col);
            g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ pos.x, yNow }));
        }

        if (collides (b))
        {
            g.setColour (theme::accent2);
            juce::Path ring;
            ring.addEllipse (juce::Rectangle<float> (r * 2 + 10, r * 2 + 10).withCentre (pos));
            juce::Path dashed;
            const float dashes[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dashes, 2);
            g.fillPath (dashed);
        }

        if (b == proc.getSoloBand())
        {
            g.setColour (theme::accent2.withAlpha (0.5f));
            g.drawEllipse (juce::Rectangle<float> (r * 2 + 16, r * 2 + 16).withCentre (pos), 2.0f);
        }

        // Glow + body
        g.setColour (col.withAlpha (focus ? 0.35f : 0.18f));
        g.fillEllipse (juce::Rectangle<float> (r * 2 + 8, r * 2 + 8).withCentre (pos));
        g.setColour (c.snap.enabled ? col : theme::panelLight);
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (pos));
        g.setColour (b == selected ? juce::Colours::white : col.darker (0.4f));
        g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (pos), b == selected ? 2.0f : 1.0f);
        g.setColour (c.snap.enabled ? theme::background : col);
        g.setFont (theme::font (10.0f, true));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (r * 2, r * 2).withCentre (pos), juce::Justification::centred);
    }
}

void ResponseDisplay::drawOverlays (juce::Graphics& g)
{
    // Spectrum grab highlight
    if (grab.active && dragging < 0)
    {
        const float x = freqToX (grab.freq);
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (26, 26).withCentre ({ x, grab.y }));
        g.setColour (theme::accent);
        g.drawEllipse (juce::Rectangle<float> (12, 12).withCentre ({ x, grab.y }), 1.5f);
        g.setFont (theme::font (11.0f, true));
        g.drawText ("grab " + freqText (grab.freq), juce::Rectangle<float> (x - 60, grab.y - 30, 120, 14), juce::Justification::centred);
    }

    // Draw-mode sketch
    if (drawMode)
    {
        g.setColour (theme::accent2.withAlpha (0.08f));
        g.fillRect (plot);
        g.setColour (theme::accent2);
        g.setFont (theme::font (12.0f, true));
        g.drawText ("DRAW MODE - sketch a curve, release to fit bands (Esc to cancel)",
                    plot.reduced (8).removeFromTop (18), juce::Justification::centredTop);
        juce::Path p;
        bool started = false;
        for (int x = 0; x < (int) sketch.size(); ++x)
        {
            if (std::isnan (sketch[(size_t) x])) { started = false; continue; }
            const float y = dbToY (sketch[(size_t) x]);
            if (! started) { p.startNewSubPath ((float) x, y); started = true; }
            else p.lineTo ((float) x, y);
        }
        g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void ResponseDisplay::drawPiano (juce::Graphics& g)
{
    auto strip = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 18, plot.getWidth(), 15);
    g.setColour (theme::panel);
    g.fillRect (strip);
    for (int note = 12; note <= 135; ++note)
    {
        const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const double fLo = f * std::pow (2.0, -1.0 / 24.0), fHi = f * std::pow (2.0, 1.0 / 24.0);
        if (fHi < fMin || fLo > fMax) continue;
        const int pc = note % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const float x0 = freqToX (std::max (fLo, fMin)), x1 = freqToX (std::min (fHi, fMax));
        auto key = juce::Rectangle<float> (x0, strip.getY(), x1 - x0, strip.getHeight());
        g.setColour (black ? juce::Colour (0xff15181e) : juce::Colour (0xffc9ced8));
        g.fillRect (black ? key.withTrimmedBottom (5) : key.reduced (0.2f, 0));
        if (pc == 0 && x1 - x0 > 2.5f)
        {
            g.setColour (theme::background);
            g.setFont (theme::font (8.5f));
            g.drawText ("C" + juce::String (note / 12 - 1), key.withWidth (18).translated (-1, 2), juce::Justification::centredLeft);
        }
    }
}

// ---- interaction ----------------------------------------------------------------------
int ResponseDisplay::hitTestNode (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 13.0f;
    for (int b = params::kNumBands - 1; b >= 0; --b)
    {
        if (! proc.getParamRefs().snapshot (b).used) continue;   // live state, not the per-frame cache
        const float d = getNodePosition (b).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void ResponseDisplay::setSelectedBand (int b)
{
    if (b == selected) return;
    if (selected >= 0 && proc.getSoloBand() == selected)
        proc.setSoloBand (-1);
    selected = b;
    if (onSelectionChanged) onSelectionChanged (b);
    repaint();
}

void ResponseDisplay::setDrawMode (bool on)
{
    drawMode = on;
    std::fill (sketch.begin(), sketch.end(), std::numeric_limits<float>::quiet_NaN());
    sketching = false;
    setMouseCursor (on ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void ResponseDisplay::findSpectrumGrab (juce::Point<float> p)
{
    grab.active = false;
    if (! specValid || drawMode || ! plot.contains (p))
        return;
    const auto mode = proc.getAnalyzer().getSettings().mode;
    const auto& spec = mode == analyzer::DisplayMode::Pre ? specPre
                     : mode == analyzer::DisplayMode::Sidechain ? specSc : specPost;

    const int centre = juce::roundToInt ((p.x - plot.getX()) / plot.getWidth() * (numPoints - 1));
    const int search = 6;
    int peak = -1;
    float best = -1000.0f;
    for (int i = std::max (0, centre - search); i <= std::min (numPoints - 1, centre + search); ++i)
        if (spec[(size_t) i] > best) { best = spec[(size_t) i]; peak = i; }
    if (peak <= 0 || peak >= numPoints - 1 || best < -100.0f)
        return;

    // Prominence: the peak must stand out from its surroundings (+-1/3 octave).
    const double fPeak = xFreqs[(size_t) peak];
    float surround = 1000.0f;
    int lo = peak, hi = peak;
    while (lo > 0 && xFreqs[(size_t) lo] > fPeak / std::pow (2.0, 1.0 / 3.0)) --lo;
    while (hi < numPoints - 1 && xFreqs[(size_t) hi] < fPeak * std::pow (2.0, 1.0 / 3.0)) ++hi;
    surround = std::min (spec[(size_t) lo], spec[(size_t) hi]);
    const float y = analyzerDbToY (best);
    if (best - surround < 4.0f || std::abs (y - p.y) > 28.0f)
        return;

    // Width at -6 dB below the peak -> Q.
    int l = peak, r = peak;
    while (l > 0 && spec[(size_t) l] > best - 6.0f) --l;
    while (r < numPoints - 1 && spec[(size_t) r] > best - 6.0f) ++r;
    const double oct = std::max (0.1, std::log2 (xFreqs[(size_t) r] / xFreqs[(size_t) l]));
    const double twoN = std::pow (2.0, oct);
    grab = { true, fPeak, y, juce::jlimit (0.5, 12.0, std::sqrt (twoN) / (twoN - 1.0)) };
}

void ResponseDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    const bool hadGrab = grab.active;
    if (h != hovered) { hovered = h; repaint(); }
    if (h < 0) findSpectrumGrab (e.position); else grab.active = false;
    if (hadGrab != grab.active) repaint();
    setMouseCursor (drawMode ? juce::MouseCursor::CrosshairCursor
                    : (h >= 0 || grab.active) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ResponseDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    grab.active = false;
    repaint();
}

void ResponseDisplay::beginDrag (int b, const juce::MouseEvent& e)
{
    dragging = b;
    dragStart = e.position;
    const auto s = bands.snapshot (b);
    dragStartFreq = s.freq;
    dragStartGain = s.gain;
    dragStartQ = s.q;
    dragIsQ = ! params::shapeHasGain (s.shape);
    proc.beginUndoTransaction ("Move band " + juce::String (b + 1));
    bands.param (b, pb::freq)->beginChangeGesture();
    bands.param (b, dragIsQ ? pb::q : pb::gain)->beginChangeGesture();
}

void ResponseDisplay::endDrag()
{
    if (dragging < 0) return;
    bands.param (dragging, pb::freq)->endChangeGesture();
    bands.param (dragging, dragIsQ ? pb::q : pb::gain)->endChangeGesture();
    dragging = -1;
    grabDragging = false;
}

void ResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (drawMode)
    {
        if (e.mods.isLeftButtonDown())
        {
            sketching = true;
            lastSketchX = -1;
            mouseDrag (e);
        }
        return;
    }

    const int n = hitTestNode (e.position);
    if (n >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            setSelectedBand (n);
            showBandMenu (n);
            return;
        }
        if (e.mods.isAltDown())
        {
            bands.deleteBand (n);
            if (selected == n) setSelectedBand (-1);
            hovered = -1;
            return;
        }
        setSelectedBand (n);
        beginDrag (n, e);
        return;
    }

    if (e.mods.isPopupMenu())
    {
        // Add a band of a chosen shape at the clicked position.
        const auto f = (float) xToFreq (e.position.x);
        const auto gain = (float) juce::jlimit (-30.0, 30.0, yToDb (e.position.y));
        juce::PopupMenu m;
        m.addSectionHeader ("Add band at " + freqText (f));
        for (int s = 0; s < params::shapeNames.size(); ++s)
            m.addItem (s + 1, params::shapeNames[s]);
        juce::Component::SafePointer<ResponseDisplay> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                         [safe, f, gain] (int r)
                         {
                             if (safe == nullptr || r <= 0) return;
                             const auto shape = (Shape) (r - 1);
                             const int b = safe->bands.createBand (f, gain, shape, params::shapeHasGain (shape) ? 1.0f : 0.7071f);
                             if (b >= 0) safe->setSelectedBand (b);
                         });
        return;
    }

    if (grab.active)
    {
        const int b = bands.createBand ((float) grab.freq, 0.0f, Shape::Bell, (float) grab.q);
        if (b >= 0)
        {
            setSelectedBand (b);
            beginDrag (b, e);
            dragStartFreq = (float) grab.freq;
            dragStartGain = 0.0f;
            grabDragging = true;
            grab.active = false;
        }
        return;
    }
    setSelectedBand (-1);
}

void ResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drawMode && sketching)
    {
        const int x = juce::jlimit (0, (int) sketch.size() - 1, (int) e.position.x);
        const float db = (float) juce::jlimit (-30.0, 30.0, yToDb (e.position.y));
        if (lastSketchX >= 0 && std::abs (x - lastSketchX) > 1)
        {
            // Fill skipped pixels by linear interpolation.
            const int dir = x > lastSketchX ? 1 : -1;
            for (int i = lastSketchX + dir; i != x; i += dir)
                sketch[(size_t) i] = lastSketchDb + (db - lastSketchDb) * (float) (i - lastSketchX) / (float) (x - lastSketchX);
        }
        sketch[(size_t) x] = db;
        lastSketchX = x;
        lastSketchDb = db;
        repaint();
        return;
    }

    if (dragging < 0)
        return;

    const bool fine = e.mods.isShiftDown();
    const float k = fine ? 0.1f : 1.0f;
    const auto delta = (e.position - dragStart) * k;

    // Frequency: move in log space by the pixel delta (works the same for normal and fine drags).
    const double octPerPixel = std::log2 (fMax / fMin) / plot.getWidth();
    const float newFreq = (float) juce::jlimit (fMin, fMax, dragStartFreq * std::pow (2.0, delta.x * octPerPixel));
    bands.set (dragging, pb::freq, newFreq);

    if (dragIsQ)
    {
        const float q = juce::jlimit (params::kQMin, params::kQMax, dragStartQ * std::pow (2.0f, -delta.y / 60.0f));
        bands.set (dragging, pb::q, q);
    }
    else
    {
        const double dbPerPixel = scaleDb() / (plot.getHeight() * 0.5f - 10.0f);
        const float gain = (float) juce::jlimit (-30.0, 30.0, dragStartGain - delta.y * dbPerPixel);
        bands.set (dragging, pb::gain, gain);
    }
}

void ResponseDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drawMode && sketching)
    {
        sketching = false;
        finishDrawing();
        return;
    }
    endDrag();
}

void ResponseDisplay::finishDrawing()
{
    // Target: the sketch where drawn; elsewhere 0 dB with a small weight.
    std::vector<double> f, target, weight;
    for (int i = 0; i < 120; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, i / 119.0);
        const int x = juce::roundToInt (freqToX (hz));
        const bool drawn = juce::isPositiveAndBelow (x, (int) sketch.size()) && ! std::isnan (sketch[(size_t) x]);
        f.push_back (hz);
        target.push_back (drawn ? sketch[(size_t) x] : 0.0);
        weight.push_back (drawn ? 1.0 : 0.15);
    }
    bool any = false;
    for (auto w : weight) any |= w > 0.5;
    if (any)
    {
        fit::FitOptions o;
        o.maxBands = 10;
        o.toleranceDb = 0.35;
        const auto res = fit::fitCurve (f, target, weight, o);
        bands.replaceAllBands (res.bands, "Draw curve");
    }
    setDrawMode (false);
    if (onDrawModeFinished) onDrawModeFinished();
}

void ResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (drawMode) return;
    const int n = hitTestNode (e.position);
    if (n >= 0)
    {
        proc.beginUndoTransaction ("Toggle band");
        bands.setDiscrete (n, pb::enabled, bands.get (n, pb::enabled) > 0.5f ? 0.0f : 1.0f);
        return;
    }
    if (! plot.contains (e.position)) return;
    const float f = (float) xToFreq (e.position.x);
    const float gain = (float) juce::jlimit (-30.0, 30.0, yToDb (e.position.y));
    // Smart shape: cuts at the extremes, a bell everywhere else.
    const auto shape = f < 25.0f ? Shape::LowCut : f > 18000.0f ? Shape::HighCut : Shape::Bell;
    const int b = bands.createBand (f, gain, shape, shape == Shape::Bell ? 1.0f : 0.7071f);
    if (b >= 0) setSelectedBand (b);
}

void ResponseDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = hitTestNode (e.position);
    if (b < 0) b = selected;
    if (b < 0) return;
    static juce::uint32 lastWheel = 0;
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastWheel > 600)
        proc.beginUndoTransaction ("Change Q");
    lastWheel = now;

    const float factor = std::pow (2.0f, w.deltaY * (e.mods.isShiftDown() ? 0.25f : 1.5f));
    auto* p = bands.param (b, pb::q);
    p->beginChangeGesture();
    bands.set (b, pb::q, juce::jlimit (params::kQMin, params::kQMax, bands.get (b, pb::q) * factor));
    p->endChangeGesture();
}

void ResponseDisplay::showBandMenu (int b)
{
    const auto s = bands.snapshot (b);
    juce::PopupMenu shapes, slopes, places;
    for (int i = 0; i < params::shapeNames.size(); ++i)
        shapes.addItem (100 + i, params::shapeNames[i], true, (int) s.shape == i);
    for (int i = 0; i < params::slopeNames.size(); ++i)
        slopes.addItem (200 + i, params::slopeNames[i], true, (int) s.slope == i);
    for (int i = 0; i < params::placementNames.size(); ++i)
        places.addItem (300 + i, params::placementNames[i], true, (int) s.placement == i);

    juce::PopupMenu m;
    m.addSectionHeader ("Band " + juce::String (b + 1));
    m.addSubMenu ("Shape", shapes);
    m.addSubMenu ("Slope", slopes, params::shapeUsesSlope (s.shape));
    m.addSubMenu ("Stereo Placement", places);
    m.addSeparator();
    m.addItem (1, "Enabled", true, s.enabled);
    m.addItem (2, "Dynamic", params::shapeHasGain (s.shape), s.dynOn);
    m.addItem (3, "Solo (listen)", true, proc.getSoloBand() == b);
    m.addSeparator();
    m.addItem (4, "Delete band");

    juce::Component::SafePointer<ResponseDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ba = safe->bands;
        safe->proc.beginUndoTransaction ("Band edit");
        if (r >= 300)      ba.setDiscrete (b, pb::placement, (float) (r - 300));
        else if (r >= 200) ba.setDiscrete (b, pb::slope, (float) (r - 200));
        else if (r >= 100)
        {
            const auto shape = (Shape) (r - 100);
            ba.setDiscrete (b, pb::shape, (float) shape);
            if (! params::shapeHasGain (shape)) ba.setDiscrete (b, pb::dynOn, 0.0f);
        }
        else if (r == 1) ba.setDiscrete (b, pb::enabled, ba.get (b, pb::enabled) > 0.5f ? 0.0f : 1.0f);
        else if (r == 2) ba.setDiscrete (b, pb::dynOn, ba.get (b, pb::dynOn) > 0.5f ? 0.0f : 1.0f);
        else if (r == 3) safe->proc.setSoloBand (safe->proc.getSoloBand() == b ? -1 : b);
        else if (r == 4)
        {
            ba.deleteBand (b);
            safe->setSelectedBand (-1);
        }
        if (safe->onSelectionChanged) safe->onSelectionChanged (safe->selected);
    });
}

juce::String ResponseDisplay::getTooltip()
{
    if (drawMode)
        return "Draw mode: drag to sketch the response you want, release to auto-fit bands.";
    if (hovered >= 0)
    {
        const auto s = bands.snapshot (hovered);
        juce::String t = "Band " + juce::String (hovered + 1) + ": " + params::shapeNames[(int) s.shape] + "  "
                         + freqText (s.freq);
        if (params::shapeHasGain (s.shape)) t << "  " << juce::String (s.gain, 1) << " dB";
        t << "  Q " << juce::String (s.q, 2) << "  [" << params::placementNames[(int) s.placement] << "]";
        if (! s.enabled) t << "  (disabled)";
        for (auto& c : findCollisions())
            if (c.first == hovered || c.second == hovered)
                t << "\nWarning: overlaps band " << (c.first == hovered ? c.second : c.first) + 1
                  << " - consider merging them.";
        t << "\nDrag: freq/gain (Shift = fine)  Wheel: Q  Alt-click: delete\nRight-click: shape menu  Double-click: enable/disable";
        return t;
    }
    if (grab.active)
        return "Spectrum grab: drag to create a band on this " + freqText (grab.freq) + " peak.";
    return "Double-click to add a band. Right-click to add a specific shape.\nHover an analyzer peak and drag to grab it.";
}
} // namespace tg::gui
