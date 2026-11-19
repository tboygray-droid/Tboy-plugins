#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "state/FactoryPresets.h"

using namespace tg;
using params::Mode;
using params::OsFactor;

TgsoundsEQAudioProcessor::BusesProperties TgsoundsEQAudioProcessor::makeBuses()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

TgsoundsEQAudioProcessor::TgsoundsEQAudioProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, &undoManager, "TgsoundsEQ", params::createLayout())
{
    refs.bind (apvts);
    startTimerHz (30);
}

TgsoundsEQAudioProcessor::~TgsoundsEQAudioProcessor()
{
    stopTimer();
    designer.stop();
}

bool TgsoundsEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

//==============================================================================
void TgsoundsEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    designer.stop();

    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);
    mainChannels = juce::jlimit (1, 2, getMainBusNumInputChannels());
    sideChannels = getBusCount (true) > 1 && getBus (true, 1)->isEnabled()
                       ? juce::jmin (2, getBus (true, 1)->getNumberOfChannels()) : 0;

    // Oversamplers: polyphase IIR half-band filters (minimum phase, integer latency).
    for (int f = 1; f <= 2; ++f)
    {
        mainOs[(size_t) f] = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) mainChannels, (size_t) f, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        mainOs[(size_t) f]->initProcessing ((size_t) maxBlockSize);
        if (sideChannels > 0)
        {
            sideOs[(size_t) f] = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) sideChannels, (size_t) f, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
            sideOs[(size_t) f]->initProcessing ((size_t) maxBlockSize);
        }
        else
        {
            sideOs[(size_t) f].reset();
        }
    }

    const int maxOsBlock = maxBlockSize * 4;
    iir.prepare (sampleRate, maxOsBlock, mainChannels);
    lpDynamics.prepare (sampleRate, maxBlockSize, mainChannels);
    lpDynamics.setMethod (dsp::DesignMethod::Matched);
    linear.prepare (sampleRate, maxBlockSize, mainChannels);
    solo.prepare (sampleRate);
    outputStage.prepare (sampleRate);

    dryBuffer.setSize (2, maxBlockSize);
    detBuffer.setSize (2, maxOsBlock);
    sideBuffer.setSize (2, maxBlockSize);

    const int maxLatency = linear.latencyFor (params::LpQuality::Max, sampleRate) + 64;
    bypassDelay.prepare (2, maxLatency);
    lpDetDelay.prepare (2, maxLatency);
    lpSideDelay.prepare (2, maxLatency);

    bypassMix.reset (sampleRate, 0.02);
    bypassMix.setCurrentAndTargetValue (refs.global.bypass->load() > 0.5f ? 1.0f : 0.0f);

    lastMode = lastOs = -1;   // force engine (re)configuration on the first block
    processingRate = sampleRate;

    analyzer.setSampleRate (sampleRate);
    designer.start (sampleRate, mainChannels == 1);
    setLatencySamples (computeRequiredLatency());
}

void TgsoundsEQAudioProcessor::releaseResources()
{
    designer.stop();
}

//==============================================================================
int TgsoundsEQAudioProcessor::oversamplingLatency (OsFactor f) const noexcept
{
    if (f == OsFactor::Off || mainOs[(size_t) f] == nullptr)
        return 0;
    return juce::roundToInt (mainOs[(size_t) f]->getLatencyInSamples());
}

int TgsoundsEQAudioProcessor::computeRequiredLatency() const noexcept
{
    const auto mode = (Mode) (int) refs.global.mode->load();
    if (mode == Mode::LinearPhase)
        return dsp::LinearPhaseEngine::latencyFor ((params::LpQuality) (int) refs.global.lpQuality->load(), currentSampleRate);
    return oversamplingLatency ((OsFactor) (int) refs.global.os->load());
}

void TgsoundsEQAudioProcessor::updateHostLatency()
{
    const int required = computeRequiredLatency();
    if (required != getLatencySamples())
        setLatencySamples (required);
}

void TgsoundsEQAudioProcessor::timerCallback()
{
    updateHostLatency();
    midiLearn.dispatch();
    if (const int p = pendingProgram.exchange (-1); p >= 0)
        presets.loadFactoryPreset (p);
}

int TgsoundsEQAudioProcessor::getNumPrograms()
{
    return (int) state::getFactoryPresets().size();
}

const juce::String TgsoundsEQAudioProcessor::getProgramName (int index)
{
    const auto& all = state::getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) all.size()) ? juce::String (all[(size_t) index].name) : juce::String();
}

void TgsoundsEQAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentProgram.store (index);
    if (juce::MessageManager::existsAndIsCurrentThread())
        presets.loadFactoryPreset (index);
    else
        pendingProgram.store (index);   // applied by the message-thread timer
}

void TgsoundsEQAudioProcessor::beginUndoTransaction (const juce::String& name)
{
    (void) apvts.copyState();   // copyState() flushes parameter values into the tree (recorded in the current transaction)
    undoManager.beginNewTransaction (name);
}

juce::ValueTree TgsoundsEQAudioProcessor::getUiState()
{
    return apvts.state.getOrCreateChildWithName ("UI", nullptr);
}

float TgsoundsEQAudioProcessor::getDynamicGainDb (int band) const noexcept
{
    return (Mode) (int) refs.global.mode->load() == Mode::LinearPhase ? lpDynamics.getDynamicGainDb (band)
                                                                      : iir.getDynamicGainDb (band);
}

//==============================================================================
void TgsoundsEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int n = buffer.getNumSamples();
    if (n == 0)
        return;
    if (n > maxBlockSize)
    {
        // Hosts must not exceed the prepared size, but stay safe: process in slices.
        for (int start = 0; start < n; start += maxBlockSize)
        {
            const int len = juce::jmin (maxBlockSize, n - start);
            juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
            juce::MidiBuffer none;
            processBlock (slice, none);
        }
        return;
    }

    auto mainBus = getBusBuffer (buffer, false, 0);
    const int numCh = juce::jmin (mainChannels, mainBus.getNumChannels());
    for (int ch = getMainBusNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (numCh == 0)
        return;

    // ---- Parameters ----
    const auto& g = refs.global;
    const auto mode = (Mode) (int) g.mode->load();
    const auto quality = (params::LpQuality) (int) g.lpQuality->load();
    const auto osf = mode == Mode::LinearPhase ? OsFactor::Off : (OsFactor) (int) g.os->load();
    for (int b = 0; b < params::kNumBands; ++b)
        snaps[(size_t) b] = refs.snapshot (b);

    // ---- Mode / oversampling changes (RT-safe re-configuration) ----
    if ((int) mode != lastMode || (int) osf != lastOs)
    {
        const double rate = currentSampleRate * (1 << (int) osf);
        iir.setSampleRate (rate);
        iir.setMethod (mode == Mode::NaturalPhase ? dsp::DesignMethod::Matched : dsp::DesignMethod::Bilinear);
        if (osf != OsFactor::Off)
        {
            mainOs[(size_t) osf]->reset();
            if (sideOs[(size_t) osf]) sideOs[(size_t) osf]->reset();
        }
        if (mode == Mode::LinearPhase && lastMode != (int) Mode::LinearPhase)
        {
            linear.reset();
            lpDynamics.reset();
            lpDetDelay.reset();
            lpSideDelay.reset();
        }
        processingRate = rate;
        lastMode = (int) mode;
        lastOs = (int) osf;
    }

    // Sidechain (optional bus)
    juce::AudioBuffer<float> sideBus;
    if (sideChannels > 0)
        sideBus = getBusBuffer (buffer, true, 1);
    const int scCh = juce::jmin (sideChannels, sideBus.getNumChannels());

    // Dry copy for bypass crossfade
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, mainBus, ch, 0, n);

    // Analyzer taps (no-ops unless an editor is showing the analyzer)
    analyzer.push (analyzer::Source::Pre, dryBuffer.getArrayOfReadPointers(), numCh, n);
    if (scCh > 0)
        analyzer.push (analyzer::Source::Sidechain, sideBus.getArrayOfReadPointers(), scCh, n);

    juce::dsp::AudioBlock<float> mainBlock (mainBus.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
    juce::dsp::AudioBlock<float> scBlock;
    if (scCh > 0)
        scBlock = juce::dsp::AudioBlock<float> (sideBus.getArrayOfWritePointers(), (size_t) scCh, (size_t) n);

    // ---- EQ ----
    if (mode != Mode::LinearPhase)
    {
        iir.setTargets (snaps, false);

        auto up = osf == OsFactor::Off ? mainBlock : mainOs[(size_t) osf]->processSamplesUp (mainBlock);
        const int upN = (int) up.getNumSamples();

        juce::dsp::AudioBlock<float> det (detBuffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) upN);
        det.copyFrom (up);

        juce::dsp::AudioBlock<const float> scUp;
        if (scCh > 0)
            scUp = osf == OsFactor::Off ? juce::dsp::AudioBlock<const float> (scBlock)
                                        : juce::dsp::AudioBlock<const float> (sideOs[(size_t) osf]->processSamplesUp (scBlock));

        iir.process (up, det, scUp);

        if (osf != OsFactor::Off)
            mainOs[(size_t) osf]->processSamplesDown (mainBlock);
    }
    else
    {
        // Static EQ as linear-phase FIR...
        float* io[2] { mainBus.getWritePointer (0), mainBus.getWritePointer (numCh - 1) };
        linear.process (io, numCh, n, quality);

        // ...plus dynamic "delta" bands (min-phase), detecting on input delayed to line up with the FIR.
        const int lat = linear.latencyFor (quality, currentSampleRate);
        float* det[2] { detBuffer.getWritePointer (0), detBuffer.getWritePointer (1) };
        const float* dry[2] { dryBuffer.getReadPointer (0), dryBuffer.getReadPointer (numCh - 1) };
        lpDetDelay.process (dry, det, numCh, n, lat);

        juce::dsp::AudioBlock<const float> scDelayed;
        if (scCh > 0)
        {
            float* sd[2] { sideBuffer.getWritePointer (0), sideBuffer.getWritePointer (1) };
            const float* sIn[2] { sideBus.getReadPointer (0), sideBus.getReadPointer (scCh - 1) };
            lpSideDelay.process (sIn, sd, scCh, n, lat);
            scDelayed = juce::dsp::AudioBlock<const float> (sideBuffer.getArrayOfReadPointers(), (size_t) scCh, (size_t) n);
        }

        lpDynamics.setTargets (snaps, true);
        lpDynamics.process (mainBlock,
                                juce::dsp::AudioBlock<const float> (detBuffer.getArrayOfReadPointers(), (size_t) numCh, (size_t) n),
                                scDelayed);
    }

    // ---- Solo (listen to one band's region) ----
    {
        const int sb = soloBand.load();
        const bool on = sb >= 0 && sb < params::kNumBands && snaps[(size_t) sb].used;
        solo.setTarget (on, on ? snaps[(size_t) sb] : params::BandSnapshot {});
        float* io[2] { mainBus.getWritePointer (0), mainBus.getWritePointer (numCh - 1) };
        solo.process (io, numCh, n);
    }

    // ---- Output stage ----
    outputStage.setTargets (g.outGain->load(), designer.getAutoGainDb(), g.outPan->load(), g.phaseInv->load() > 0.5f);
    outputStage.process (mainBus, numCh, n);

    // ---- Bypass: crossfade with the dry signal delayed by the current latency ----
    {
        float* dry[2] { dryBuffer.getWritePointer (0), dryBuffer.getWritePointer (1) };
        const int lat = mode == Mode::LinearPhase ? linear.latencyFor (quality, currentSampleRate) : oversamplingLatency (osf);
        bypassDelay.process (dry, dry, numCh, n, lat);

        bypassMix.setTargetValue (g.bypass->load() > 0.5f ? 1.0f : 0.0f);
        if (bypassMix.isSmoothing() || bypassMix.getCurrentValue() > 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = bypassMix.getNextValue();
                for (int ch = 0; ch < numCh; ++ch)
                {
                    auto* x = mainBus.getWritePointer (ch);
                    x[i] += m * (dry[ch][i] - x[i]);
                }
            }
        }
    }

    postProcess (mainBus, numCh, n);
}

void TgsoundsEQAudioProcessor::postProcess (juce::AudioBuffer<float>& mainBus, int numCh, int n) noexcept
{
    analyzer.push (analyzer::Source::Post, mainBus.getArrayOfReadPointers(), numCh, n);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float peak = mainBus.getMagnitude (ch, 0, n);
        auto& slot = outPeak[(size_t) ch];
        float prev = slot.load (std::memory_order_relaxed);
        while (peak > prev && ! slot.compare_exchange_weak (prev, peak, std::memory_order_relaxed)) {}
    }
    if (numCh == 1)
        outPeak[1].store (outPeak[0].load (std::memory_order_relaxed), std::memory_order_relaxed);
}

juce::AudioProcessorParameter* TgsoundsEQAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (params::global::bypass);
}

//==============================================================================
void TgsoundsEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto old = state.getChildWithName ("MIDI"); old.isValid())
        state.removeChild (old, nullptr);
    state.appendChild (midiLearn.toValueTree(), nullptr);
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TgsoundsEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (auto midiTree = tree.getChildWithName ("MIDI"); midiTree.isValid())
    {
        midiLearn.fromValueTree (midiTree);
        tree.removeChild (midiTree, nullptr);
    }
    apvts.replaceState (tree);
}

juce::AudioProcessorEditor* TgsoundsEQAudioProcessor::createEditor()
{
    return new TgsoundsEQAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TgsoundsEQAudioProcessor();
}
