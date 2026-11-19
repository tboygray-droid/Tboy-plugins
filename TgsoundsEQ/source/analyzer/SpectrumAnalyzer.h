#pragma once

#include "AnalyzerFifo.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace tg::analyzer
{
enum class Source { Pre = 0, Post = 1, Sidechain = 2 };
enum class DisplayMode { Off, Pre, Post, PrePost, Sidechain };

struct AnalyzerSettings
{
    int resolution = 1;          ///< 0..3 => FFT 4096, 8192, 16384, 32768
    int speed = 2;               ///< 0..4 => very slow .. very fast (release rate)
    float tiltDbPerOct = 4.5f;   ///< 0 .. 4.5 dB/oct around 1 kHz
    bool peakHold = false;
    DisplayMode mode = DisplayMode::PrePost;

    static int fftOrderFor (int resolution) noexcept { return 12 + juce::jlimit (0, 3, resolution); }
};

/**
    Spectrum analysis for one source: sliding window, Hann, 75 % overlap, dB magnitude,
    slope tilt, attack/release ballistics and peak hold. Runs on the analyzer thread.
*/
class SpectrumChannel
{
public:
    void configure (int fftOrder, double sampleRate);
    /** Consume new samples; performs as many hops as available. Returns true if the spectrum changed. */
    bool process (AnalyzerFifo& fifo, const AnalyzerSettings& s, double elapsedSeconds);

    /** GUI access (call with the hub lock held). */
    const std::vector<float>& getDb() const noexcept   { return smoothDb; }
    const std::vector<float>& getPeak() const noexcept { return peakDb; }
    int getFftSize() const noexcept { return fftSize; }
    double getSampleRate() const noexcept { return fs; }
    void resetPeak() { std::fill (peakDb.begin(), peakDb.end(), -200.0f); }

    // Long-term average (Match EQ). Power average of untilted frames.
    void startCapture() { std::fill (capturePower.begin(), capturePower.end(), 0.0); captureFrames = 0; capturing = true; }
    void stopCapture() { capturing = false; }
    bool isCapturing() const noexcept { return capturing; }
    int getCaptureFrames() const noexcept { return captureFrames; }
    const std::vector<double>& getCapturePower() const noexcept { return capturePower; }

private:
    void computeFrame (const AnalyzerSettings& s);

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 0, hop = 0, filled = 0, sinceLastFrame = 0;
    double fs = 48000.0;
    std::vector<float> window, ring, frame, rawDb, smoothDb, peakDb, scratch;
    float lastTilt = -1.0f;
    std::vector<float> tiltTable;
    std::vector<double> capturePower;
    int captureFrames = 0;
    bool capturing = false;
};

/**
    Owns the three sample FIFOs (pre / post / sidechain) and a background thread that analyses
    them at up to ~60 Hz while an editor is open.
*/
class AnalyzerHub : private juce::Thread
{
public:
    AnalyzerHub();
    ~AnalyzerHub() override;

    // ---- audio thread ----
    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }
    void push (Source src, const float* const* ch, int numCh, int n) noexcept
    {
        if (isActive()) fifos[(size_t) src].push (ch, numCh, n);
    }

    // ---- message thread ----
    void setSampleRate (double sr) { sampleRate.store (sr); }
    void setActive (bool shouldRun);
    void setSettings (const AnalyzerSettings& s);
    AnalyzerSettings getSettings() const;
    void resetPeaks();

    /** Fill `numPoints` log-spaced values (fMin..fMax) for the given source. Thread-safe vs the analyzer.
        High frequencies take the max of the covered bins, low frequencies interpolate. */
    bool fillCurve (Source src, bool peak, float fMin, float fMax, float* out, int numPoints) const;

    // ---- Match EQ capture (message thread) ----
    void startCapture (Source src);
    void stopCapture (Source src);
    bool isCapturing (Source src) const;
    /** Average captured spectrum (dB, untilted) at the given frequencies. Returns false if nothing captured. */
    bool getCapture (Source src, const std::vector<double>& freqs, std::vector<double>& dbOut) const;
    int getCaptureFrames (Source src) const;

    /** Synchronous processing for tests. */
    void processNow (double elapsedSeconds = 1.0 / 60.0);

private:
    void run() override;

    std::array<AnalyzerFifo, 3> fifos;
    std::array<SpectrumChannel, 3> channels;
    std::atomic<bool> active { false };
    std::atomic<double> sampleRate { 48000.0 };
    AnalyzerSettings settings;
    int configuredOrder = 0;
    double configuredRate = 0;
    bool resetPeakRequest = false;
    mutable juce::CriticalSection lock;   // analyzer thread <-> GUI only (never the audio thread)
};
} // namespace tg::analyzer
