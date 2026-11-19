#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "params/Parameters.h"
#include "dsp/OutputStage.h"
#include "dsp/IirEqEngine.h"
#include "dsp/LinearPhaseEngine.h"
#include "dsp/EqDesigner.h"
#include "dsp/SoloFilter.h"
#include "dsp/DelayLine.h"
#include "analyzer/SpectrumAnalyzer.h"
#include "state/PresetManager.h"
#include "state/MidiLearn.h"

/**
    TgsoundsEQ audio processor.

    Threading model:
      - Audio thread: processBlock() only reads cached atomic parameter values and
        pre-allocated buffers. No locks, allocations or I/O.
      - Designer thread (EqDesigner): linear-phase FIRs and auto-gain, handed over lock-free.
      - Message thread: a timer pushes latency changes to the host.
*/
class TgsoundsEQAudioProcessor : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    TgsoundsEQAudioProcessor();
    ~TgsoundsEQAudioProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    // Host "programs" = the factory presets. setCurrentProgram may arrive on the audio thread
    // (VST3 program-change parameter), so loading is deferred to the message thread when needed.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    // ---- Public API for editor / tests ----
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    /** Flushes pending parameter values into the (undoable) state tree, then starts a new
        undo transaction - so earlier edits can never leak into this transaction. */
    void beginUndoTransaction (const juce::String& name = {});
    const tg::params::ParamRefs& getParamRefs() const noexcept { return refs; }

    /** Latency the DSP needs for the current parameter values. Thread-safe. */
    int computeRequiredLatency() const noexcept;
    /** Pushes computeRequiredLatency() to the host if it changed. Message thread. */
    void updateHostLatency();

    /** Rate the IIR bands currently run at (base rate x oversampling). */
    double getProcessingRate() const noexcept { return processingRate.load(); }
    double getBaseSampleRate() const noexcept { return currentSampleRate; }

    void setSoloBand (int band) noexcept { soloBand.store (band); }
    int getSoloBand() const noexcept { return soloBand.load(); }

    float getDynamicGainDb (int band) const noexcept;
    float getAutoGainDb() const noexcept { return designer.getAutoGainDb(); }

    tg::analyzer::AnalyzerHub& getAnalyzer() noexcept { return analyzer; }
    tg::state::PresetManager& getPresets() noexcept { return presets; }
    tg::state::ABCompare& getAB() noexcept { return abCompare; }
    tg::state::MidiLearn& getMidiLearn() noexcept { return midiLearn; }
    /** Non-parameter UI state (analyzer settings, scale, editor size...). */
    juce::ValueTree getUiState();
    bool hasSidechain() const noexcept { return sideChannels > 0; }

    /** Output peak level per channel since the last call (GUI meters). */
    float takeOutputPeak (int ch) noexcept { return outPeak[(size_t) juce::jlimit (0, 1, ch)].exchange (0.0f); }
    int getNumMainChannels() const noexcept { return mainChannels; }

    /** Forces a synchronous linear-phase / auto-gain design pass (tests, offline). */
    void runDesignerNow() { designer.runOnce(); }

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    int oversamplingLatency (tg::params::OsFactor f) const noexcept;
    void postProcess (juce::AudioBuffer<float>& mainBus, int numCh, int n) noexcept;

    juce::UndoManager undoManager { 30000, 100 };
    juce::AudioProcessorValueTreeState apvts;
    tg::params::ParamRefs refs;

    // DSP
    tg::dsp::IirEqEngine iir;            // Zero Latency / Natural Phase
    tg::dsp::IirEqEngine lpDynamics;     // dynamic "delta" bands in Linear Phase mode
    tg::dsp::LinearPhaseEngine linear;
    tg::dsp::EqDesigner designer { refs, linear };
    tg::dsp::SoloFilter solo;
    tg::dsp::OutputStage outputStage;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> mainOs, sideOs;   // [Off unused, 2x, 4x]
    tg::dsp::DelayLine bypassDelay, lpDetDelay, lpSideDelay;

    tg::analyzer::AnalyzerHub analyzer;
    tg::state::PresetManager presets { apvts };
    tg::state::ABCompare abCompare { apvts };
    tg::state::MidiLearn midiLearn { *this };
    std::array<std::atomic<float>, 2> outPeak {};

    juce::AudioBuffer<float> dryBuffer, detBuffer, sideBuffer;
    std::array<tg::params::BandSnapshot, tg::params::kNumBands> snaps;

    juce::SmoothedValue<float> bypassMix;
    double currentSampleRate = 44100.0;
    int maxBlockSize = 512, mainChannels = 2, sideChannels = 0;
    std::atomic<double> processingRate { 44100.0 };
    int lastMode = -1, lastOs = -1;
    std::atomic<int> soloBand { -1 };
    std::atomic<int> currentProgram { 0 }, pendingProgram { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TgsoundsEQAudioProcessor)
};
