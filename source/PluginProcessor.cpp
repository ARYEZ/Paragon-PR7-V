#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

namespace
{
    constexpr double gainRampSeconds = 0.02;

    // Extra settings kept in the saved state (DAW project or preset). Projects
    // from before 0.3.0 may also hold "ampModelPath" and "cabIrPath", from
    // the NAM amp and cabinet PR7-V no longer has: they're ignored.
    const juce::Identifier rigNameKey { "rigName" };
    const juce::Identifier rigFileKey { "rigFile" };
    const juce::Identifier rigFactoryKey { "rigFactory" };
    const juce::Identifier rigModifiedKey { "rigModified" };
    const juce::Identifier tappedBpmKey { "tappedBpm" };
    const juce::Identifier expressionCcKey { "expressionCc" };
    const juce::Identifier tunerReferenceKey { "tunerReferenceHz" };
    // Which plugin version's parameters a project was saved with, as for
    // rigs (see Rig.cpp). Projects from before 0.3.0 don't say: 0.
    const juce::Identifier parameterVersionKey { "parameterVersion" };
    constexpr int parameterVersion = 37;   // 37: CYCLONE replaced the original rotary (convertOldRotary)
}

ParagonX1AudioProcessor::ParagonX1AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ParagonX1", createParameterLayout())
{
    inputGainDb = parameters.getRawParameterValue (ParamIDs::inputGain);
    outputGainDb = parameters.getRawParameterValue (ParamIDs::outputGain);
    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
        eqGainDb[(size_t) band] = parameters.getRawParameterValue (ParamIDs::eqBands[band]);
    for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
        for (int part = 0; part < ParamIDs::harmonyVoiceParts.size(); ++part)
            harmonyVoiceParameters[(size_t) voice].parts[part] = parameters.getRawParameterValue (ParamIDs::harmonyVoice (voice, ParamIDs::harmonyVoiceParts[part]));
    for (int band = 0; band < effects::Mars::maxBands; ++band)
        for (int part = 0; part < ParamIDs::marsBandParts.size(); ++part)
            marsBandParameters[(size_t) band].parts[part] = parameters.getRawParameterValue (ParamIDs::marsBand (band, ParamIDs::marsBandParts[part]));
    marsModulationParameters.bind (parameters);
    for (int note = 0; note < 12; ++note)
    {
        for (int part = 0; part < 3; ++part)
            harmonyMapParameters[(size_t) note][(size_t) part] = parameters.getRawParameterValue (ParamIDs::harmonyMap (note, part));
        harmonyScaleNoteParameters[(size_t) note] = parameters.getRawParameterValue (ParamIDs::harmonyScaleNote (note));
    }
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
        for (int part = 0; part < ParamIDs::doublerVoiceParts.size(); ++part)
            doublerVoiceParameters[(size_t) voice].parts[part] = parameters.getRawParameterValue (ParamIDs::doublerVoice (voice, ParamIDs::doublerVoiceParts[part]));
    // HARMONY's modulators and the controls they can move.
    harmonyModParameters.bind (parameters);
    for (int o = 0; o < 2; ++o)
        for (int part = 0; part < ParamIDs::vocoderOscParts.size(); ++part)
            vocoderParameters.osc[(size_t) o][(size_t) part] = parameters.getRawParameterValue (ParamIDs::vocoderOsc (o, ParamIDs::vocoderOscParts[part]));
    for (int point = 0; point < effects::Vocoder::curvePoints; ++point)
    {
        vocoderParameters.bandVol[(size_t) point] = parameters.getRawParameterValue (ParamIDs::vocoderBandVol (point));
        vocoderParameters.bandPan[(size_t) point] = parameters.getRawParameterValue (ParamIDs::vocoderBandPan (point));
    }
    for (int i = 0; i < effects::Vocoder::numIntervals; ++i)
        vocoderParameters.intervals[(size_t) i] = parameters.getRawParameterValue (ParamIDs::vocoderInterval (i));
    {
        const auto ids = ParamIDs::harmonyModTargets();
        for (const auto& id : ids)
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameters.getParameter (id));
            jassert (ranged != nullptr);   // a typo in ParamIDs::harmonyModTargets
            harmonyModTargets.push_back ({ parameters.getRawParameterValue (id), ranged->getNormalisableRange(), ParamIDs::isHarmonyPitchTarget (id) });
        }
        harmonyModSlots = std::make_unique<std::array<HarmonyModSlotState, 4>[]> ((size_t) ids.size());
        harmonyModShown = std::make_unique<std::atomic<float>[]> ((size_t) ids.size());
    }

    rigLibrary = std::make_unique<RigLibrary>();

    // Any change to a parameter that rigs store marks the rig as modified,
    // and finishing an edit to one records an undo step.
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.addParameterListener (withId->paramID, this);
                parameter->addListener (this);
            }

    // Housekeeping off the audio thread: show MIDI pedal moves and record
    // undo steps. 30 Hz so a MIDI expression pedal moves smoothly on screen.
    startTimerHz (30);

    // The starting point, so the first edit can be undone.
    history.record (takeSnapshot());
    checkpointWanted = false;
}

ParagonX1AudioProcessor::~ParagonX1AudioProcessor()
{
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            parameters.removeParameterListener (withId->paramID, this);
            parameter->removeListener (this);
        }

    stopTimer();
}

void ParagonX1AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    inputGain.reset (sampleRate, gainRampSeconds);
    outputGain.reset (sampleRate, gainRampSeconds);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load() + param (ParamIDs::rigLevel)));

    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    bypassDelay.prepare ({ sampleRate, (juce::uint32) preparedBlockSize, 1 });
    bypassDry.setSize (1, preparedBlockSize);
    bypassAmount.reset (sampleRate, gainRampSeconds);
    bypassAmount.setCurrentAndTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    compGate.prepare (sampleRate, preparedBlockSize);
    filterPitch.prepare (sampleRate, preparedBlockSize);
    deEsser.prepare (sampleRate, preparedBlockSize);
    mars.prepare (sampleRate, preparedBlockSize);
    presence.prepare (sampleRate, preparedBlockSize);
    lowCut.prepare (sampleRate, preparedBlockSize);
    lowCut.reset (param (ParamIDs::inputLowCut));
    limiter.prepare (sampleRate);
    limiter.reset (param (ParamIDs::outputLimiter));
    graphicEq.prepare (sampleRate, preparedBlockSize);
    doubler.prepare (sampleRate, preparedBlockSize);
    harmony.prepare (sampleRate, preparedBlockSize);
    harmonyModulation.prepare (sampleRate);
    lastGeneratedNote = -1;
    cleanVoice.setSize (1, preparedBlockSize);
    modulation.prepare (sampleRate, preparedBlockSize);
    delay.prepare (sampleRate, preparedBlockSize);
    reverb.prepare (sampleRate, preparedBlockSize);
    loFi.prepare (sampleRate, preparedBlockSize);
    vocoder.prepare (sampleRate, preparedBlockSize);
    vocoderVoice.setSize (1, preparedBlockSize);
    glitch.prepare (sampleRate, preparedBlockSize);
    pitchBlock.prepare (sampleRate, preparedBlockSize);
    talkBox.prepare (sampleRate, preparedBlockSize);
    wah.prepare (sampleRate, preparedBlockSize);
    inputTap.setSampleRate (sampleRate);

    expressionVolume.reset (sampleRate, gainRampSeconds);
    expressionVolume.setCurrentAndTargetValue (1.0f);
    tunerOpen.reset (sampleRate, gainRampSeconds);
    tunerOpen.setCurrentAndTargetValue (isOn (ParamIDs::tunerOn) && isOn (ParamIDs::tunerMute) ? 0.0f : 1.0f);
    // Nothing in the chain looks ahead, except HARMONY's CORRECT and FORMANT while they're on.
    correctLatency = wantedLatency();
    setLatencySamples (correctLatency.load());
}

int ParagonX1AudioProcessor::wantedLatency() const noexcept
{
    // TUNE VOICE and FORMANT both need the voice's slices ahead of time: one shared delay.
    const auto correcting = juce::roundToInt (param (ParamIDs::harmonyCorrect)) != (int) effects::Harmony::Correct::off;
    return correcting || isOn (ParamIDs::formantOn) ? juce::roundToInt (effects::Harmony::correctLatencySeconds * getSampleRate()) : 0;
}

void ParagonX1AudioProcessor::updateLatency()
{
    // The audio thread switches CORRECT's delay only once the DAW has been
    // told, so what it reports and what it does always agree.
    if (const auto wanted = wantedLatency(); wanted != getLatencySamples())
    {
        setLatencySamples (wanted);
        correctLatency = wanted;
    }
}

juce::AudioProcessorParameter* ParagonX1AudioProcessor::getBypassParameter() const
{
    return parameters.getParameter (ParamIDs::bypass);
}

float ParagonX1AudioProcessor::param (const char* id) const
{
    // Looked up by ID each block. APVTS keeps these in a sorted map, so the
    // lookup is a handful of string compares and never allocates.
    return parameters.getRawParameterValue (id)->load();
}

void ParagonX1AudioProcessor::readHostTempo()
{
    // The song position too, while the transport plays, so GLITCH can put its
    // steps on the song's grid. -1: not playing, or the host doesn't say.
    songBeatsAtBlock = -1.0;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;
            if (const auto beats = position->getPpqPosition(); position->getIsPlaying() && beats.hasValue())
                songBeatsAtBlock = *beats;
        }
}

bool ParagonX1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto isMonoOrStereo = [] (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };

    return isMonoOrStereo (layouts.getMainInputChannelSet())
        && isMonoOrStereo (layouts.getMainOutputChannelSet());
}

double ParagonX1AudioProcessor::getTempoBpm() const noexcept
{
    const auto tapped = tappedBpm.load();
    return tapped > 0.0 ? tapped : hostBpm.load();
}

void ParagonX1AudioProcessor::MarsModulationParameters::bind (juce::AudioProcessorValueTreeState& state)
{
    const auto get = [&state] (const juce::String& id)
    {
        auto* value = state.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    };
    for (int l = 0; l < Mod::numLfos; ++l)
    {
        for (int part = 0; part < ParamIDs::marsLfoParts.size(); ++part)
            lfos[(size_t) l].parts[(size_t) part] = get (ParamIDs::marsLfo (l, ParamIDs::marsLfoParts[part]));
        for (int step = 0; step < Mod::maxSteps; ++step)
            for (int part = 0; part < ParamIDs::marsStepParts.size(); ++part)
                lfos[(size_t) l].steps[(size_t) step][(size_t) part] = get (ParamIDs::marsStep (l, step, ParamIDs::marsStepParts[part]));
    }
    for (int e = 0; e < Mod::numEgs; ++e)
        for (int part = 0; part < ParamIDs::marsEgParts.size(); ++part)
            egs[(size_t) e][(size_t) part] = get (ParamIDs::marsEg (e, ParamIDs::marsEgParts[part]));
    for (int f = 0; f < Mod::numEfs; ++f)
        for (int part = 0; part < ParamIDs::marsEfParts.size(); ++part)
            efs[(size_t) f][(size_t) part] = get (ParamIDs::marsEf (f, ParamIDs::marsEfParts[part]));
    for (int m = 0; m < Mod::numMidis; ++m)
        for (int part = 0; part < ParamIDs::marsMidiParts.size(); ++part)
            midis[(size_t) m][(size_t) part] = get (ParamIDs::marsMidi (m, ParamIDs::marsMidiParts[part]));
    for (int p = 0; p < Mod::numXys; ++p)
        for (int part = 0; part < ParamIDs::marsXyParts.size(); ++part)
            xys[(size_t) p][(size_t) part] = get (ParamIDs::marsXy (p, ParamIDs::marsXyParts[part]));
    for (int slot = 0; slot < Mod::numSlots; ++slot)
        for (int part = 0; part < ParamIDs::marsSlotParts.size(); ++part)
            slots[(size_t) slot][(size_t) part] = get (ParamIDs::marsSlot (slot, ParamIDs::marsSlotParts[part]));
}

void ParagonX1AudioProcessor::MarsModulationParameters::read (Mod::Settings& settings) const noexcept
{
    const auto index = [] (Value value) { return juce::roundToInt (value->load()); };
    for (size_t l = 0; l < lfos.size(); ++l)
    {
        const auto& p = lfos[l].parts;
        auto& lfo = settings.lfos[l];
        lfo.numSteps = index (p[0]) + 1;
        lfo.rateHz = p[1]->load();
        lfo.sync = index (p[2]);
        lfo.offset = p[3]->load();
        lfo.balance = p[4]->load();
        lfo.glide = p[5]->load();
        lfo.phase = p[6]->load();
        lfo.midi = (Mod::MidiRestart) index (p[7]);
        for (size_t step = 0; step < (size_t) Mod::maxSteps; ++step)
        {
            const auto& sp = lfos[l].steps[step];
            lfo.steps[step] = { sp[0]->load(), sp[1]->load(), (Mod::Curve) index (sp[2]), sp[3]->load() > 0.5f };
        }
    }
    for (size_t e = 0; e < egs.size(); ++e)
    {
        const auto& p = egs[e];
        auto& eg = settings.egs[e];
        eg.trigger = (Mod::Trigger) index (p[0]);
        eg.thresholdDb = p[1]->load();
        eg.delayMs = p[2]->load();
        eg.attackMs = p[3]->load();
        eg.decayMs = p[4]->load();
        eg.sustain = p[5]->load();
        eg.holdMs = p[6]->load();
        eg.releaseMs = p[7]->load();
        eg.attackSlope = p[8]->load();
        eg.decaySlope = p[9]->load();
        eg.releaseSlope = p[10]->load();
        eg.centred = index (p[11]) == 1;
    }
    for (size_t f = 0; f < efs.size(); ++f)
    {
        const auto& p = efs[f];
        settings.efs[f] = { (Mod::Trigger) index (p[0]), p[1]->load(), p[2]->load(), index (p[3]) == 1, p[4]->load() };
    }
    for (size_t m = 0; m < midis.size(); ++m)
    {
        const auto& p = midis[m];
        settings.midis[m] = { (Mod::MidiInput) index (p[0]), index (p[1]), (Mod::Response) index (p[2]) };
    }
    for (size_t x = 0; x < xys.size(); ++x)
    {
        const auto& p = xys[x];
        settings.xys[x] = { p[0]->load(), p[1]->load(), index (p[2]) == 1, index (p[3]) == 1 };
    }
    for (size_t s = 0; s < slots.size(); ++s)
    {
        const auto& p = slots[s];
        settings.slots[s] = { index (p[0]), index (p[1]), p[2]->load() };
    }
}

void ParagonX1AudioProcessor::readMidiExpression (const juce::MidiBuffer& midi) noexcept
{
    // CC 11 is "expression" in the MIDI spec, and what most MIDI expression
    // pedals send; SETTINGS can pick another. Only the latest value in the block matters.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isControllerOfType (expressionCc.load()))
            pendingMidiExpression = (float) message.getControllerValue() / 127.0f;
    }
}

float ParagonX1AudioProcessor::getExpressionPosition() const noexcept
{
    // A MIDI pedal move is used straight away, before the timer has copied it
    // into the parameter, so the pedal responds without the timer's delay.
    const auto midi = pendingMidiExpression.load();
    return midi >= 0.0f ? midi : param (ParamIDs::expression);
}

float ParagonX1AudioProcessor::expressionAmount() const noexcept
{
    // MIN and MAX set where the heel and toe land, so the pedal can, say,
    // sweep only the top half of the wah, or never quite reach silence.
    const auto heel = param (ParamIDs::expressionMin), toe = param (ParamIDs::expressionMax);
    return heel + (toe - heel) * getExpressionPosition();
}

void ParagonX1AudioProcessor::syncMidiExpressionToParameter()
{
    auto midi = pendingMidiExpression.load();
    if (midi < 0.0f)
        return;

    if (auto* pedal = parameters.getParameter (ParamIDs::expression))
    {
        pedal->beginChangeGesture();
        pedal->setValueNotifyingHost (pedal->convertTo0to1 (midi));
        pedal->endChangeGesture();
    }

    // Only clear it if no newer MIDI value arrived meanwhile; that one is
    // picked up on the next tick.
    pendingMidiExpression.compare_exchange_strong (midi, -1.0f);
}

void ParagonX1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    readMidiExpression (midi);
    marsMidi.read (midi);
    // HARMONY's modulators restart on a new note (Retrigger, Legato).
    {
        auto held = false;
        for (const auto velocity : harmonyMidi.velocity)
            held = held || velocity > 0.0f;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn())
            {
                harmonyMidiNoteStarted = true;
                harmonyMidiHeldBefore = held;
                break;
            }
    }
    // ...and the MIDI sources: the last key's velocity and place, the wheels, aftertouch.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            harmonyModMidi.velocity = message.getFloatVelocity();
            harmonyModMidi.keyboard = juce::jlimit (-1.0f, 1.0f, ((float) message.getNoteNumber() - 60.0f) / effects::HarmonyModulation::pitchSpan);
        }
        else if (message.isPitchWheel())
            harmonyModMidi.pitchWheel = juce::jlimit (-1.0f, 1.0f, (float) (message.getPitchWheelValue() - 8192) / 8192.0f);
        else if (message.isControllerOfType (1))
            harmonyModMidi.modWheel = (float) message.getControllerValue() / 127.0f;
        else if (message.isChannelPressure())
            harmonyModMidi.aftertouch = (float) message.getChannelPressureValue() / 127.0f;
        else if (message.isAftertouch())
            harmonyModMidi.aftertouch = (float) message.getAfterTouchValue() / 127.0f;
    }
    harmonyMidi.read (midi);
    vocoder.handleMidi (midi);
    for (size_t key = 0; key < shownMidiKeys.size(); ++key)
        shownMidiKeys[key].store (harmonyMidi.velocity[key]);

    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    readHostTempo();

    // Some hosts occasionally send more samples than they promised in
    // prepareToPlay. Every stage's scratch memory is sized for the promise,
    // so work through an oversized block in promised-size pieces.
    for (int start = 0; start < buffer.getNumSamples(); start += preparedBlockSize)
        processChunk (buffer, start, juce::jmin (preparedBlockSize, buffer.getNumSamples() - start));
    marsMidi.blockDone();
}

void ParagonX1AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    const auto numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    // A microphone is a mono source, so the chain up to the graphic EQ runs
    // on the first input channel. On a stereo track that is the left channel.
    auto* mono = buffer.getWritePointer (0, start);
    juce::dsp::AudioBlock<float> monoBlock (&mono, 1, (size_t) numSamples);

    // Keep the clean input, delayed to line up with the processed sound, for BYPASS.
    const auto latency = (float) juce::jlimit (0, bypassDelay.getMaximumDelayInSamples() - 1, getLatencySamples());
    auto* dry = bypassDry.getWritePointer (0);
    for (int i = 0; i < numSamples; ++i)
    {
        bypassDelay.pushSample (0, mono[i]);
        dry[i] = bypassDelay.popSample (0, latency);
    }

    // The tuner listens to the voice itself, before anything changes it.
    inputTap.write (mono, numSamples);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    inputGain.applyGain (mono, numSamples);
    inputMeter.pushSamples (mono, numSamples);

    // LOW CUT: rumble and plosive thumps out first, so nothing after it
    // (above all the compressor) reacts to them. The INPUT meter, just
    // before, shows what the mic sends, for setting the gain.
    lowCut.process (monoBlock, param (ParamIDs::inputLowCut));
    cleanVoice.copyFrom (0, 0, mono, numSamples);   // for HARMONY to read the sung note from

    // PITCH: TUNING, OCTAVER and FREEZE, before the effects shape the voice.
    pitchBlock.process (monoBlock, { param (ParamIDs::pitchTune), isOn (ParamIDs::octOn), param (ParamIDs::octSubTwo),
                                     param (ParamIDs::octSubOne), param (ParamIDs::octDry), param (ParamIDs::octUp),
                                     isOn (ParamIDs::freezeOn), param (ParamIDs::freezeLevel),
                                     param (ParamIDs::freezeAttack) / 1000.0f, param (ParamIDs::freezeRelease) / 1000.0f });

    const auto expressionTarget = (int) param (ParamIDs::expressionTarget);
    const auto pedal = expressionAmount();
    wah.process (monoBlock, expressionTarget == ExpressionTargets::wah, pedal);

    effects::CompGate::Settings compSettings { isOn (ParamIDs::compOn), param (ParamIDs::compThreshold), param (ParamIDs::compRatio),
                                               param (ParamIDs::compLevel), param (ParamIDs::gateThreshold),
                                               (effects::CompGate::Type) (int) param (ParamIDs::compType), param (ParamIDs::compAmount),
                                               param (ParamIDs::compDetail), param (ParamIDs::compMix), isOn (ParamIDs::compIntensity),
                                               param (ParamIDs::revivalGate), param (ParamIDs::revivalCompression), param (ParamIDs::revivalGain) };
    compSettings.photonInputDb = param (ParamIDs::photonInput);
    compSettings.photonOutputDb = param (ParamIDs::photonOutput);
    compSettings.photonAttack = param (ParamIDs::photonAttack);
    compSettings.photonRelease = param (ParamIDs::photonRelease);
    compSettings.photonRatio = juce::roundToInt (param (ParamIDs::photonRatio));
    compSettings.abyssPeakReduction = param (ParamIDs::abyssPeakReduction);
    compSettings.abyssGainDb = param (ParamIDs::abyssGain);
    compSettings.abyssMode = juce::roundToInt (param (ParamIDs::abyssMode));
    compSettings.abyssEmphasis = param (ParamIDs::abyssEmphasis);
    compGate.process (monoBlock, compSettings);
    // The de-esser after the compressor, whose make-up gain brings esses up.
    deEsser.process (monoBlock, { isOn (ParamIDs::deessOn), param (ParamIDs::deessFreq), param (ParamIDs::deessAmount),
                                  isOn (ParamIDs::deessListen) });
    // PITCH / WHAMMY: the pedal bends the pitch shifter from no shift at
    // the heel to its INTERVAL at the toe, like a DigiTech Whammy.
    // WHAMMY-WAH: the pedal plays the Whammy-Wah type. Unassigned, it rests at the heel.
    const auto whammy = expressionTarget == ExpressionTargets::pitch ? pedal : 1.0f;
    // The Talk Box type runs after the cab (below); here FILTER / PITCH stands aside for it.
    const auto fpType = (effects::FilterPitch::Type) (int) param (ParamIDs::fpType);
    const auto talkBoxOn = isOn (ParamIDs::fpOn) && fpType == effects::FilterPitch::Type::talkBox;
    filterPitch.process (monoBlock, { isOn (ParamIDs::fpOn) && ! talkBoxOn, fpType,
                                      param (ParamIDs::fpSensitivity), param (ParamIDs::fpResonance),
                                      param (ParamIDs::fpInterval) * whammy, param (ParamIDs::fpMix),
                                      (int) param (ParamIDs::fpXpProgram),
                                      expressionTarget == ExpressionTargets::whammyWah ? pedal : 0.0f });
    // GLITCH: before the amp (PRE) or after the EQ (POST). The pedal, on
    // GLITCH, fades it in: dry at the heel, its MIX at the toe.
    const effects::Glitch::Settings glitchSettings { isOn (ParamIDs::glitchOn), (effects::Glitch::Mode) (int) param (ParamIDs::glitchMode),
                                                     isOn (ParamIDs::glitchSync), param (ParamIDs::glitchTime),
                                                     (int) param (ParamIDs::glitchDivision), param (ParamIDs::glitchDensity),
                                                     param (ParamIDs::glitchPitch), param (ParamIDs::glitchChaos),
                                                     param (ParamIDs::glitchMix) * (expressionTarget == ExpressionTargets::glitch ? pedal : 1.0f) };
    const auto glitchPre = (int) param (ParamIDs::glitchPosition) == (int) effects::Glitch::Position::pre;
    // Where this chunk starts in the song. Only while the DAW's own tempo is
    // in charge: a tapped tempo doesn't match the DAW's grid.
    const auto songBeats = songBeatsAtBlock >= 0.0 && tappedBpm.load() <= 0.0
                               ? songBeatsAtBlock + (double) start / getSampleRate() * hostBpm.load() / 60.0
                               : -1.0;
    if (glitchPre)
        glitch.process (monoBlock, glitchSettings, getTempoBpm(), songBeats);

    // SATURATION: MARS.
    effects::Mars::Settings marsSettings;
    marsSettings.on = isOn (ParamIDs::driveOn);
    marsSettings.numBands = (int) param (ParamIDs::marsBands) + 1;
    marsSettings.crossoverHz = { param (ParamIDs::marsCross1), param (ParamIDs::marsCross2), param (ParamIDs::marsCross3) };
    marsSettings.slope = (effects::Mars::Slope) (int) param (ParamIDs::marsSlope);
    marsSettings.inputDb = param (ParamIDs::marsInput);
    marsSettings.outputDb = param (ParamIDs::marsOutput);
    marsSettings.mix = param (ParamIDs::marsMix);
    marsSettings.quality = (effects::Mars::Quality) (int) param (ParamIDs::marsQuality);
    for (size_t b = 0; b < marsSettings.bands.size(); ++b)
    {
        const auto& parts = marsBandParameters[b].parts;
        auto& band = marsSettings.bands[b];
        band.on = parts[0]->load() > 0.5f;
        band.style = (effects::Mars::Style) (int) parts[1]->load();
        band.drive = parts[2]->load();
        band.dynamics = parts[3]->load();
        band.feedback = parts[4]->load();
        band.feedbackHz = parts[5]->load();
        band.bassDb = parts[6]->load();
        band.midDb = parts[7]->load();
        band.trebleDb = parts[8]->load();
        band.presenceDb = parts[9]->load();
        band.mix = parts[10]->load();
        band.levelDb = parts[11]->load();
    }
    marsModulationParameters.read (marsSettings.modulation);
    const effects::MarsModulation::Context marsContext { getTempoBpm(), songBeats, pedal, &marsMidi };
    mars.process (monoBlock, marsSettings, marsContext);

    presence.process (monoBlock, param (ParamIDs::presence));

    // TALK BOX: the sound shaped into vowels by the pedal (its TALK BOX
    // setting; unassigned, it rests at the heel: "oo").
    talkBox.process (monoBlock, talkBoxOn, expressionTarget == ExpressionTargets::talkBox ? pedal : 0.0f,
                     param (ParamIDs::fpResonance), param (ParamIDs::fpMix));

    effects::GraphicEq::Settings eq;
    eq.on = isOn (ParamIDs::eqOn);
    for (size_t band = 0; band < eq.gainsDb.size(); ++band)
        eq.gainsDb[band] = eqGainDb[band]->load();
    eq.levelDb = param (ParamIDs::eqLevel);
    graphicEq.process (monoBlock, eq);
    if (! glitchPre)
        glitch.process (monoBlock, glitchSettings, getTempoBpm(), songBeats);

    // The voice as it leaves the mono part: what talks through the VOCODER.
    vocoderVoice.copyFrom (0, 0, buffer, 0, start, numSamples);

    // From here on, stereo: the doubler, modulation, delay and reverb spread
    // the sound across both speakers.
    for (int channel = 1; channel < numOutputChannels; ++channel)
        buffer.copyFrom (channel, start, buffer, 0, start, numSamples);

    const auto stereoBlock = juce::dsp::AudioBlock<float> (buffer)
                                 .getSubBlock ((size_t) start, (size_t) numSamples)
                                 .getSubsetChannelBlock (0, (size_t) juce::jmin (numOutputChannels, effects::maxChannels));

    // MORPH FX: the pedal blends the time effects in, from dry at the heel
    // to each effect's own MIX setting at the toe. (The effects smooth their
    // mix, so sweeping the pedal doesn't click.)
    const auto morph = expressionTarget == ExpressionTargets::morph ? pedal : 1.0f;

    // HARMONY: voices made from the processed voice (so they share its
    // tone), in the key read from the clean one.
    effects::Harmony::Settings harmonySettings;
    harmonySettings.on = isOn (ParamIDs::harmonyOn);
    harmonySettings.key = (int) param (ParamIDs::harmonyKey);
    harmonySettings.scale = (Scales::Scale) (int) param (ParamIDs::harmonyScale);
    harmonySettings.sourceDb = param (ParamIDs::harmonySource);
    harmonySettings.harmonyDb = param (ParamIDs::harmonyLevel);
    harmonySettings.correctFormant = param (ParamIDs::harmonyCorrectFormant);
    harmonySettings.spread = param (ParamIDs::harmonySpread);
    harmonySettings.correct = (effects::Harmony::Correct) juce::roundToInt (param (ParamIDs::harmonyCorrect));
    harmonySettings.voiceFormant = isOn (ParamIDs::formantOn) ? param (ParamIDs::formantShift) : 0.0f;
    harmonySettings.latency = correctLatency.load();
    auto& notes = harmonySettings.notes;
    notes.generate = isOn (ParamIDs::harmonyGenerate);
    notes.chord = juce::roundToInt (param (ParamIDs::harmonyChord));
    notes.minNote = param (ParamIDs::harmonyMinNote);
    notes.toleranceCents = param (ParamIDs::harmonyTolerance);
    notes.toleranceMs = param (ParamIDs::harmonyToleranceTime);
    notes.levelDb = param (ParamIDs::harmonyNoteLevel);
    notes.width = param (ParamIDs::harmonyNoteWidth);
    notes.formant = param (ParamIDs::harmonyNoteFormant);
    notes.attackMs = param (ParamIDs::harmonyNoteAttack);
    notes.releaseMs = param (ParamIDs::harmonyNoteRelease);
    notes.midiMap = juce::roundToInt (param (ParamIDs::harmonyMidiMap));
    harmonySettings.midi = &harmonyMidi;
    // The Note Map Editor's Custom map and scale.
    for (int note = 0; note < 12; ++note)
    {
        std::uint64_t bits = 0;
        for (int part = 0; part < 3; ++part)
            bits |= (std::uint64_t) juce::jlimit (0, 65535, juce::roundToInt (harmonyMapParameters[(size_t) note][(size_t) part]->load())) << (16 * part);
        notes.customMap[(size_t) note] = bits;
        const auto box = juce::roundToInt (harmonyScaleNoteParameters[(size_t) note]->load());
        harmonySettings.customScale[(size_t) note] = box == 1 ? 1 : box == 2 ? -1 : 0;
    }
    notes.fold = isOn (ParamIDs::harmonyMapFold);
    // GLOBAL: GLIDE (a synced one as ms at the tempo now), PITCH and FORMANT.
    {
        const auto sync = juce::roundToInt (param (ParamIDs::harmonyGlideSync));
        const auto syncs = (int) std::size (effects::Delay::syncBeats);
        harmonySettings.glideMs = sync > 0 && sync < syncs
                                      ? (float) (effects::Delay::syncBeats[sync] * 60000.0 / juce::jmax (20.0, getTempoBpm()))
                                      : param (ParamIDs::harmonyGlide);
        harmonySettings.pitch = param (ParamIDs::harmonyGlobalPitch);
        harmonySettings.formant = param (ParamIDs::harmonyGlobalFormant);
    }
    notes.fx = harmonyFx (param (ParamIDs::harmonyNoteDelay), juce::roundToInt (param (ParamIDs::harmonyNoteSync)),
                          param (ParamIDs::harmonyNoteFeedback), juce::roundToInt (param (ParamIDs::harmonyNoteFilter)),
                          param (ParamIDs::harmonyNoteCutoff));
    for (size_t v = 0; v < harmonySettings.voices.size(); ++v)
    {
        const auto& parts = harmonyVoiceParameters[v].parts;   // in harmonyVoiceParts' order
        auto& voice = harmonySettings.voices[v];
        voice.active = parts[0]->load() > 0.5f;
        voice.on = parts[1]->load() > 0.5f;
        voice.semitones = parts[2]->load();
        voice.cents = parts[3]->load();
        voice.formant = parts[4]->load();
        voice.correct = parts[5]->load() > 0.5f;
        voice.pan = parts[6]->load();
        voice.levelDb = parts[7]->load();
        voice.flatten = parts[8]->load() > 0.5f;
        voice.note = parts[9]->load();
        voice.fx = harmonyFx (parts[10]->load(), juce::roundToInt (parts[11]->load()), parts[12]->load(),
                              juce::roundToInt (parts[13]->load()), parts[14]->load());
    }
    processHarmony (stereoBlock, cleanVoice.getReadPointer (0), harmonySettings, songBeats);

    // VOCODER: the synth shaped by the voice, over what came in (DRY).
    effects::Vocoder::Settings vocoderSettings;
    readVocoder (vocoderSettings, harmonySettings.customScale);
    vocoderSettings.key = harmonySettings.key;
    vocoderSettings.scaleType = harmonySettings.scale;
    vocoder.process (stereoBlock, vocoderVoice.getReadPointer (0), cleanVoice.getReadPointer (0), vocoderSettings);

    // DOUBLER: MORPH fades its voices in from the heel.
    effects::Doubler::Settings doublerSettings;
    doublerSettings.on = isOn (ParamIDs::doublerOn);
    for (size_t v = 0; v < doublerSettings.voices.size(); ++v)
    {
        const auto& parts = doublerVoiceParameters[v].parts;   // in doublerVoiceParts' order
        auto& voice = doublerSettings.voices[v];
        voice.on = parts[0]->load() > 0.5f;
        voice.gainDb = parts[1]->load();
        voice.pan = parts[2]->load();
        voice.delayMs = parts[3]->load();
        voice.feedback = parts[4]->load();
        voice.octaveDown = parts[5]->load() > 0.5f;
        voice.detuneCents = parts[6]->load();
        voice.depthCents = parts[7]->load();
        voice.rateHz = parts[8]->load();
        voice.drift = parts[9]->load();
    }
    doublerSettings.directOn = isOn (ParamIDs::doublerDirectOn);
    doublerSettings.directGainDb = param (ParamIDs::doublerDirectGain);
    doublerSettings.directPan = param (ParamIDs::doublerDirectPan);
    doublerSettings.feedbackHighPassHz = param (ParamIDs::doublerFbHighPass);
    doublerSettings.lowShelfOn = isOn (ParamIDs::doublerLowOn);
    doublerSettings.lowShelfDb = param (ParamIDs::doublerLowGain);
    doublerSettings.lowShelfHz = param (ParamIDs::doublerLowFreq);
    doublerSettings.highShelfOn = isOn (ParamIDs::doublerHighOn);
    doublerSettings.highShelfDb = param (ParamIDs::doublerHighGain);
    doublerSettings.highShelfHz = param (ParamIDs::doublerHighFreq);
    doublerSettings.outputDb = param (ParamIDs::doublerOutput);
    doublerSettings.wet = morph;
    doubler.process (stereoBlock, doublerSettings);

    // Types with no dry/wet mix (tremolo, vibrato, auto-pan...) morph their depth instead.
    const auto modType = (effects::Modulation::Type) (int) param (ParamIDs::modType);
    const auto morphsDepth = ! effects::Modulation::usesMix (modType);

    // SYNC: one LFO cycle per note length at the tapped (or host) tempo.
    auto modRate = param (ParamIDs::modRate);
    if (const auto sync = (int) param (ParamIDs::modSync); sync > 0 && sync < (int) std::size (effects::Delay::syncBeats))
        modRate = juce::jlimit (0.02f, 20.0f, (float) (getTempoBpm() / 60.0) / effects::Delay::syncBeats[sync]);

    effects::Modulation::Settings modSettings { isOn (ParamIDs::modOn), modType, modRate,
                                                param (ParamIDs::modDepth) * (morphsDepth ? morph : 1.0f),
                                                param (ParamIDs::modMix) * (morphsDepth ? 1.0f : morph),
                                                param (ParamIDs::modManual), param (ParamIDs::modRes) };
    // CYCLONE: its own controls. MORPH FX fades it in over the dry sound;
    // the pedal on CYCLONE SPEED sets the rotors' speed.
    modSettings.cyclone = { param (ParamIDs::modCycFast) > 0.5f, param (ParamIDs::modCycStop) > 0.5f,
                            param (ParamIDs::modCycFastSpeed), param (ParamIDs::modCycSlowSpeed), param (ParamIDs::modCycAccel),
                            param (ParamIDs::modCycBalance), param (ParamIDs::modCycDrive), param (ParamIDs::modCycLow), param (ParamIDs::modCycHigh),
                            (effects::Cyclone::Mode) (int) param (ParamIDs::modCycMode), param (ParamIDs::modCycLevel),
                            expressionTarget == ExpressionTargets::cycloneSpeed ? pedal : -1.0f, morph };
    modulation.process (stereoBlock, modSettings);
    effects::Delay::Settings delaySettings;
    delaySettings.on = isOn (ParamIDs::delayOn);
    delaySettings.timeMs = param (ParamIDs::delayTime);
    delaySettings.syncIndex = (int) param (ParamIDs::delaySync);
    delaySettings.feedback = param (ParamIDs::delayFeedback);
    delaySettings.tone = param (ParamIDs::delayTone);
    delaySettings.mix = param (ParamIDs::delayMix) * morph;
    delaySettings.pingPong = isOn (ParamIDs::delayPingPong);
    delaySettings.type = (effects::Delay::Type) (int) param (ParamIDs::delayType);
    delaySettings.duck = param (ParamIDs::delayDuck);
    delaySettings.dual = isOn (ParamIDs::delayDual);
    delaySettings.timeRightMs = param (ParamIDs::delayTimeRight);
    delaySettings.syncRightIndex = (int) param (ParamIDs::delaySyncRight);
    delaySettings.startRight = (int) param (ParamIDs::delayPingStart) == 1;
    delaySettings.phaseLeft = isOn (ParamIDs::delayPhaseLeft);
    delaySettings.phaseRight = isOn (ParamIDs::delayPhaseRight);
    delaySettings.lowCutHz = param (ParamIDs::delayLowCut);
    delaySettings.highCutHz = param (ParamIDs::delayHighCut);
    delaySettings.modDepth = param (ParamIDs::delayModDepth);
    delaySettings.modRateHz = param (ParamIDs::delayModRate);
    delaySettings.analog = (effects::Delay::Analog) (int) param (ParamIDs::delayAnalog);
    delaySettings.loFi = isOn (ParamIDs::delayLoFi);
    delay.process (stereoBlock, delaySettings, getTempoBpm());
    effects::Reverb::Settings reverbSettings;
    reverbSettings.on = isOn (ParamIDs::reverbOn);
    reverbSettings.type = (effects::Reverb::Type) (int) param (ParamIDs::reverbType);
    reverbSettings.size = param (ParamIDs::reverbSize);
    reverbSettings.tone = param (ParamIDs::reverbTone);
    reverbSettings.mix = param (ParamIDs::reverbMix) * morph;
    reverbSettings.preDelayMs = param (ParamIDs::reverbPreDelay);
    reverbSettings.duck = param (ParamIDs::reverbDuck);
    reverbSettings.decay = param (ParamIDs::reverbDecay);
    reverbSettings.bassRt = param (ParamIDs::reverbBassRt);
    reverbSettings.crossoverHz = param (ParamIDs::reverbCrossover);
    reverbSettings.rolloffHz = param (ParamIDs::reverbRolloff);
    reverbSettings.diffusion = param (ParamIDs::reverbDiffusion);
    reverbSettings.earlyDb = param (ParamIDs::reverbEarly);
    reverbSettings.reverbDb = param (ParamIDs::reverbLevel);
    reverbSettings.spreadMs = param (ParamIDs::reverbSpread);
    reverbSettings.shape = param (ParamIDs::reverbShape);
    reverbSettings.spin = param (ParamIDs::reverbSpin);
    reverbSettings.wander = param (ParamIDs::reverbWander);
    reverb.process (stereoBlock, reverbSettings);

    // LO-FI last, so the whole sound goes through the phone. Assigned to the
    // expression pedal, the pedal fades it in: full band at the heel, lo-fi at the toe.
    const auto loFiPedal = expressionTarget == ExpressionTargets::loFi ? pedal : 1.0f;
    loFi.process (stereoBlock, { isOn (ParamIDs::lofiOn), (effects::LoFi::Type) (int) param (ParamIDs::lofiType),
                                 param (ParamIDs::lofiAmount), param (ParamIDs::lofiNoise),
                                 param (ParamIDs::lofiMix) * loFiPedal, param (ParamIDs::lofiLevel) });

    // The master OUTPUT knob plus the rig's own RIG LEVEL, and the
    // expression pedal when it's a VOLUME pedal. Squaring the position gives
    // the "audio taper" of a real volume pedal: half way down is about
    // -12 dB rather than -6 dB, so the quiet end of the travel is usable.
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load() + param (ParamIDs::rigLevel)));
    expressionVolume.setTargetValue (expressionTarget == ExpressionTargets::volume ? pedal * pedal : 1.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue() * expressionVolume.getNextValue();
        for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
            stereoBlock.getChannelPointer (channel)[i] *= gain;
    }

    // LIMITER: nothing leaves above its ceiling. (BYPASS, below, still gives
    // the untouched input.)
    limiter.process (stereoBlock, param (ParamIDs::outputLimiter));

    // BYPASS crossfades to the clean input. The chain keeps running underneath
    // so delay and reverb tails, and the amp's state, are ready when it's
    // switched back on.
    bypassAmount.setTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    if (bypassAmount.isSmoothing() || bypassAmount.getTargetValue() > 0.5f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto amount = bypassAmount.getNextValue();
            for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
            {
                auto& sample = stereoBlock.getChannelPointer (channel)[i];
                sample += (dry[i] - sample) * amount;
            }
        }
    }

    // The tuner mutes everything (bypassed or not), as a pedal tuner does,
    // so you can tune on stage silently. Faded to avoid a click.
    tunerOpen.setTargetValue (isOn (ParamIDs::tunerOn) && isOn (ParamIDs::tunerMute) ? 0.0f : 1.0f);
    if (tunerOpen.isSmoothing() || tunerOpen.getTargetValue() < 0.5f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto open = tunerOpen.getNextValue();
            for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
                stereoBlock.getChannelPointer (channel)[i] *= open;
        }
    }

    for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
        outputMeter.pushSamples (stereoBlock.getChannelPointer (channel), numSamples);
}

//==============================================================================
void ParagonX1AudioProcessor::timerCallback()
{
    syncMidiExpressionToParameter();
    followSnapshotParameter();
    recordCheckpointIfReady();
    updateLatency();
}

//==============================================================================
void ParagonX1AudioProcessor::HarmonyModParameters::bind (juce::AudioProcessorValueTreeState& state)
{
    for (int m = 0; m < effects::HarmonyModulation::numMods; ++m)
    {
        for (int part = 0; part < ParamIDs::harmonyModParts.size(); ++part)
            mods[(size_t) m].parts[part] = state.getRawParameterValue (ParamIDs::harmonyMod (m, ParamIDs::harmonyModParts[part]));
        for (int step = 0; step < effects::HarmonyModulation::maxSteps; ++step)
            mods[(size_t) m].steps[(size_t) step] = state.getRawParameterValue (ParamIDs::harmonyModStep (m, step));
    }
    amAttack = state.getRawParameterValue (ParamIDs::harmonyAmAttack);
    amRelease = state.getRawParameterValue (ParamIDs::harmonyAmRelease);
    amLevel = state.getRawParameterValue (ParamIDs::harmonyAmLevel);
    ptSmooth = state.getRawParameterValue (ParamIDs::harmonyPtSmooth);
    ptOffset = state.getRawParameterValue (ParamIDs::harmonyPtOffset);
    ptLevel = state.getRawParameterValue (ParamIDs::harmonyPtLevel);
    for (int e = 0; e < effects::HarmonyModulation::numEnvelopes; ++e)
        for (int part = 0; part < ParamIDs::harmonyEnvelopeParts.size(); ++part)
            envelopes[(size_t) e][(size_t) part] = state.getRawParameterValue (ParamIDs::harmonyEnvelope (e, ParamIDs::harmonyEnvelopeParts[part]));
    spMode = state.getRawParameterValue (ParamIDs::harmonySpMode);
    spTrigger = state.getRawParameterValue (ParamIDs::harmonySpTrigger);
    spSpread = state.getRawParameterValue (ParamIDs::harmonySpSpread);
    spLevel = state.getRawParameterValue (ParamIDs::harmonySpLevel);
}

void ParagonX1AudioProcessor::HarmonyModParameters::read (effects::HarmonyModulation::Settings& settings) const noexcept
{
    for (size_t m = 0; m < mods.size(); ++m)
    {
        const auto& parts = mods[m].parts;   // in harmonyModParts' order
        auto& mod = settings.mods[m];
        mod.type = juce::roundToInt (parts[0]->load());
        mod.shape = juce::roundToInt (parts[1]->load());
        mod.rateHz = parts[2]->load();
        mod.sync = juce::roundToInt (parts[3]->load());
        mod.phase = parts[4]->load();
        mod.warp = parts[5]->load();
        mod.smoothMs = parts[6]->load();
        mod.level = parts[7]->load();
        mod.trigger = juce::roundToInt (parts[8]->load());
        mod.play = juce::roundToInt (parts[9]->load());
        mod.steps = juce::roundToInt (parts[10]->load());
        for (size_t step = 0; step < mod.stepValues.size(); ++step)
            mod.stepValues[step] = mods[m].steps[step]->load();
    }
    settings.am = { amAttack->load(), amRelease->load(), amLevel->load() };
    settings.pt = { ptSmooth->load(), ptOffset->load(), ptLevel->load() };
    for (size_t e = 0; e < envelopes.size(); ++e)
    {
        const auto& parts = envelopes[e];
        settings.envelopes[e] = { parts[0]->load(), parts[1]->load(), parts[2]->load(), parts[3]->load(), parts[4]->load(), parts[5]->load(), parts[6]->load() };
    }
    settings.spreader = { juce::roundToInt (spMode->load()), juce::roundToInt (spTrigger->load()), spSpread->load(), spLevel->load() };
}

int ParagonX1AudioProcessor::harmonyModTarget (const juce::String& parameterId)
{
    static const auto ids = ParamIDs::harmonyModTargets();
    return ids.indexOf (parameterId);
}

Rig::HarmonyModSlot ParagonX1AudioProcessor::getHarmonyModSlot (int target, int slot) const
{
    if (! juce::isPositiveAndBelow (target, (int) harmonyModTargets.size()) || ! juce::isPositiveAndBelow (slot, 4))
        return {};
    const auto& state = harmonyModSlots[(size_t) target][(size_t) slot];
    return { state.source.load(), state.depth.load() };
}

void ParagonX1AudioProcessor::setHarmonyModSlot (int target, int slot, Rig::HarmonyModSlot value)
{
    if (! juce::isPositiveAndBelow (target, (int) harmonyModTargets.size()) || ! juce::isPositiveAndBelow (slot, 4))
        return;
    auto& state = harmonyModSlots[(size_t) target][(size_t) slot];
    state.depth = juce::jlimit (-1.0f, 1.0f, value.depth);
    state.source = juce::jlimit (0, (int) effects::HarmonyModulation::numSources - 1, value.source);
    rigModified = true;
    requestCheckpoint();
}

float ParagonX1AudioProcessor::getHarmonyModOffset (int target) const noexcept
{
    return juce::isPositiveAndBelow (target, (int) harmonyModTargets.size()) ? harmonyModShown[(size_t) target].load() : 0.0f;
}

std::array<float, 32> ParagonX1AudioProcessor::getHarmonyModShape (int mod) const
{
    std::array<float, 32> points {};
    if (juce::isPositiveAndBelow (mod, (int) harmonyModShapes.size()))
        for (size_t i = 0; i < points.size(); ++i)
            points[i] = harmonyModShapes[(size_t) mod][i].load();
    return points;
}

void ParagonX1AudioProcessor::setHarmonyModShape (int mod, const std::array<float, 32>& points)
{
    if (! juce::isPositiveAndBelow (mod, (int) harmonyModShapes.size()))
        return;
    // Kept to 1/10000 (finer than any drawing), so a rig or project saves
    // and reloads exactly the same points.
    for (size_t i = 0; i < points.size(); ++i)
        harmonyModShapes[(size_t) mod][i] = (float) (std::round ((double) juce::jlimit (-1.0f, 1.0f, points[i]) * 10000.0) / 10000.0);
    rigModified = true;
    requestCheckpoint();
}

Rig::HarmonyModSlots ParagonX1AudioProcessor::captureHarmonyModSlots() const
{
    Rig::HarmonyModSlots slots;
    const auto ids = ParamIDs::harmonyModTargets();
    for (int target = 0; target < ids.size(); ++target)
        for (int slot = 0; slot < 4; ++slot)
            if (const auto value = getHarmonyModSlot (target, slot); value.source != 0)
                slots[ids[target]][(size_t) slot] = value;
    return slots;
}

void ParagonX1AudioProcessor::restoreHarmonyModSlots (const Rig::HarmonyModSlots& slots)
{
    const auto ids = ParamIDs::harmonyModTargets();
    for (int target = 0; target < ids.size(); ++target)
    {
        const auto found = slots.find (ids[target]);
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto value = found != slots.end() ? found->second[(size_t) slot] : Rig::HarmonyModSlot {};
            auto& state = harmonyModSlots[(size_t) target][(size_t) slot];
            state.depth = value.depth;
            state.source = value.source;
        }
    }
}

bool ParagonX1AudioProcessor::harmonyModInUse() const noexcept
{
    for (size_t target = 0; target < harmonyModTargets.size(); ++target)
        for (const auto& slot : harmonyModSlots[target])
            if (effects::HarmonyModulation::isBuilt (slot.source.load()) && std::abs (slot.depth.load()) > 1.0e-4f)
                return true;
    return false;
}

void ParagonX1AudioProcessor::applyHarmonyModulation (effects::Harmony::Settings& settings, const effects::HarmonyModulation::Settings& modSettings) noexcept
{
    // Each modulated control: its own setting moved by its slots' sources
    // times their depths (through its range; a pitch control in semitones).
    const auto& ids = harmonyModTargets;
    constexpr int voiceParts = 8;   // Pitch, Note, Formant, Pan, Level, Delay, Feedback, Cutoff (ParamIDs::harmonyModTargets)
    const auto voiceTargets = effects::Harmony::numVoices * voiceParts;
    std::array<bool, effects::Harmony::numVoices + 1> fxMoved {};   // the voices' (and, last, the note voices') DELAY, FEEDBACK or CUTOFF
    std::array<std::array<float, 3>, effects::Harmony::numVoices + 1> fx {};   // their delay, feedback, cutoff as moved
    for (int v = 0; v < effects::Harmony::numVoices; ++v)
        fx[(size_t) v] = { harmonyVoiceParameters[(size_t) v].parts[10]->load(), harmonyVoiceParameters[(size_t) v].parts[12]->load(),
                           harmonyVoiceParameters[(size_t) v].parts[14]->load() };
    fx.back() = { param (ParamIDs::harmonyNoteDelay), param (ParamIDs::harmonyNoteFeedback), param (ParamIDs::harmonyNoteCutoff) };

    for (int t = 0; t < (int) ids.size(); ++t)
    {
        auto amount = 0.0f;
        auto perNote = false;   // a note-by-note source (each note voice its own value, even when the one value is 0)
        for (const auto& slot : harmonyModSlots[(size_t) t])
            if (const auto source = slot.source.load(); effects::HarmonyModulation::isBuilt (source) && std::abs (slot.depth.load()) > 1.0e-6f)
            {
                amount += harmonyModulation.getValue (source) * slot.depth.load();
                perNote = perNote || harmonyModulation.isPerNote (source, modSettings);
            }
        if (std::abs (amount) < 1.0e-6f && ! perNote)
        {
            harmonyModShown[(size_t) t] = 0.0f;
            continue;
        }
        const auto& target = ids[(size_t) t];
        const auto base = target.value->load();
        const auto span = juce::jmax (1.0e-6f, target.range.end - target.range.start);
        const auto from = target.range.convertTo0to1 (juce::jlimit (target.range.start, target.range.end, base));
        const auto moveBy = [&target, base, from] (float by)
        {
            return target.pitch ? base + by * effects::HarmonyModulation::pitchSpan
                                : target.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, from + by));
        };
        const auto moved = moveBy (amount);
        harmonyModShown[(size_t) t] = target.pitch ? amount * effects::HarmonyModulation::pitchSpan / span
                                                   : juce::jlimit (0.0f, 1.0f, from + amount) - from;

        // Stage 10: a note-by-note source on the note voices' LEVEL, WIDTH,
        // FORMANT (or GLOBAL PITCH / FORMANT, for them): each note voice its
        // own offset from `moved`.
        const auto voiceTarget = t - voiceTargets;
        if (voiceTarget == 0 || voiceTarget == 1 || voiceTarget == 2 || voiceTarget == 10 || voiceTarget == 11)
        {
            if (perNote)
                for (int v = 0; v < effects::Harmony::numNoteVoices; ++v)
                {
                    auto own = 0.0f;
                    for (const auto& slot : harmonyModSlots[(size_t) t])
                        if (const auto source = slot.source.load(); effects::HarmonyModulation::isBuilt (source))
                            own += harmonyModulation.getVoiceValue (source, v) * slot.depth.load();
                    const auto offset = moveBy (own) - moved;
                    auto& voiceMod = settings.notes.voiceMods[(size_t) v];
                    switch (voiceTarget)
                    {
                        case 0:  voiceMod.levelDb = offset; break;
                        case 1:  voiceMod.width = offset; break;
                        case 10: voiceMod.pitch = offset; break;
                        default: voiceMod.formant += offset; break;
                    }
                }
        }

        if (t < voiceTargets)
        {
            auto& voice = settings.voices[(size_t) (t / voiceParts)];
            switch (t % voiceParts)
            {
                case 0: voice.semitones = moved; break;
                case 1: voice.note = moved; break;
                case 2: voice.formant = moved; break;
                case 3: voice.pan = moved; break;
                case 4: voice.levelDb = moved; break;
                default: fx[(size_t) (t / voiceParts)][(size_t) (t % voiceParts - 5)] = moved; fxMoved[(size_t) (t / voiceParts)] = true; break;
            }
            continue;
        }
        switch (t - voiceTargets)
        {
            case 0:  settings.notes.levelDb = moved; break;
            case 1:  settings.notes.width = moved; break;
            case 2:  settings.notes.formant = moved; break;
            case 3:  fx.back()[0] = moved; fxMoved.back() = true; break;
            case 4:  fx.back()[1] = moved; fxMoved.back() = true; break;
            case 5:  fx.back()[2] = moved; fxMoved.back() = true; break;
            case 6:  settings.sourceDb = moved; break;
            case 7:  settings.harmonyDb = moved; break;
            case 8:  settings.spread = moved; break;
            case 9:  settings.correctFormant = moved; break;
            case 10: settings.pitch = moved; break;
            case 11: settings.formant = moved; break;
            default: break;
        }
    }

    // DELAY, FEEDBACK and CUTOFF moved: their echo and filter again.
    for (int v = 0; v < effects::Harmony::numVoices; ++v)
        if (fxMoved[(size_t) v])
        {
            const auto& parts = harmonyVoiceParameters[(size_t) v].parts;
            settings.voices[(size_t) v].fx = harmonyFx (fx[(size_t) v][0], juce::roundToInt (parts[11]->load()), fx[(size_t) v][1],
                                                        juce::roundToInt (parts[13]->load()), fx[(size_t) v][2]);
        }
    if (fxMoved.back())
        settings.notes.fx = harmonyFx (fx.back()[0], juce::roundToInt (param (ParamIDs::harmonyNoteSync)), fx.back()[1],
                                       juce::roundToInt (param (ParamIDs::harmonyNoteFilter)), fx.back()[2]);
}

void ParagonX1AudioProcessor::readVocoder (effects::Vocoder::Settings& settings, const Scales::Custom& custom) const noexcept
{
    settings.on = isOn (ParamIDs::vocoderOn);
    settings.mode = (effects::Vocoder::Mode) juce::roundToInt (param (ParamIDs::vocoderMode));
    settings.shift = param (ParamIDs::vocoderShift);
    settings.scale = param (ParamIDs::vocoderScale);
    settings.levelDb = param (ParamIDs::vocoderLevel);
    settings.dryDb = param (ParamIDs::vocoderDry);
    settings.carrier = (effects::Vocoder::Carrier) juce::roundToInt (param (ParamIDs::vocoderCarrier));
    settings.voicing = (effects::Vocoder::Voicing) juce::roundToInt (param (ParamIDs::vocoderVoicing));
    settings.glideMs = param (ParamIDs::vocoderGlide);
    for (size_t i = 0; i < settings.intervals.size(); ++i)
        settings.intervals[i] = juce::roundToInt (vocoderParameters.intervals[i]->load());
    settings.intervalDb = param (ParamIDs::vocoderIntervalLevel);
    settings.custom = &custom;
    for (size_t o = 0; o < 2; ++o)
    {
        const auto& parts = vocoderParameters.osc[o];   // in vocoderOscParts' order
        auto& osc = settings.osc[o];
        osc.on = parts[0]->load() > 0.5f;
        osc.wave = juce::roundToInt (parts[1]->load());
        osc.gainDb = parts[2]->load();
        osc.coarse = parts[3]->load();
        osc.fineCents = parts[4]->load();
        osc.pitchMod = parts[5]->load();
        osc.shape = parts[6]->load();
        osc.shapeMod = parts[7]->load();
    }
    settings.noise.on = isOn (ParamIDs::vocoderNoiseOn);
    settings.noise.gainDb = param (ParamIDs::vocoderNoiseGain);
    settings.noise.crush = param (ParamIDs::vocoderNoiseCrush);
    settings.noise.crushMod = param (ParamIDs::vocoderNoiseCrushMod);
    settings.noise.modSource = juce::roundToInt (param (ParamIDs::vocoderNoiseModSource));
    settings.lfoShape = juce::roundToInt (param (ParamIDs::vocoderLfoShape));
    settings.lfoRateHz = param (ParamIDs::vocoderLfoRate);
    settings.pan = param (ParamIDs::vocoderPan);
    settings.lowCutHz = param (ParamIDs::vocoderLowCut);
    settings.highCutHz = param (ParamIDs::vocoderHighCut);
    for (size_t p = 0; p < settings.bandDb.size(); ++p)
    {
        settings.bandDb[p] = vocoderParameters.bandVol[p]->load();
        settings.bandPan[p] = vocoderParameters.bandPan[p]->load();
    }
}

void ParagonX1AudioProcessor::processHarmony (juce::dsp::AudioBlock<float> block, const float* clean, const effects::Harmony::Settings& base,
                                              double songBeats)
{
    effects::HarmonyModulation::Settings modSettings;
    harmonyModParameters.read (modSettings);
    for (size_t m = 0; m < modSettings.mods.size(); ++m)
        for (size_t point = 0; point < modSettings.mods[m].drawn.size(); ++point)
            modSettings.mods[m].drawn[point] = harmonyModShapes[m][point].load();
    const auto numSamples = (int) block.getNumSamples();
    const auto inUse = harmonyModInUse();

    // While nothing is modulated, the modulators still run (the MODS page
    // shows them moving), once for the whole block, and HARMONY as before.
    // While something is, every 32 samples (under a millisecond): the
    // modulators move on, then HARMONY runs with their offsets.
    const auto chunk = inUse ? 32 : numSamples;
    for (int offset = 0; offset < numSamples; offset += chunk)
    {
        const auto length = juce::jmin (chunk, numSamples - offset);
        effects::HarmonyModulation::Context context;
        context.bpm = getTempoBpm();
        context.songBeats = songBeats >= 0.0 ? songBeats + (double) offset / getSampleRate() * context.bpm / 60.0 : -1.0;
        const auto generated = harmony.getGeneratedNote();
        context.newNote = harmonyMidiNoteStarted || (generated >= 0 && generated != lastGeneratedNote);
        context.heldBefore = harmonyMidiNoteStarted ? harmonyMidiHeldBefore : lastGeneratedNote >= 0;
        harmonyMidiNoteStarted = false;
        lastGeneratedNote = generated;
        context.sungNote = harmony.getSungNote();
        context.midi = harmonyModMidi;
        for (int v = 0; v < effects::Harmony::numNoteVoices; ++v)
            context.noteVoices[(size_t) v] = { harmony.getNoteVoiceTarget (v), harmony.getNoteVoiceVelocity (v) };
        harmonyModulation.advance (modSettings, context, clean + offset, length);

        if (! inUse)
        {
            for (size_t t = 0; t < harmonyModTargets.size(); ++t)
                harmonyModShown[t] = 0.0f;
            harmony.process (block, clean, base);
            return;
        }
        auto settings = base;
        applyHarmonyModulation (settings, modSettings);
        harmony.process (block.getSubBlock ((size_t) offset, (size_t) length), clean + offset, settings);
    }
}

//==============================================================================
std::optional<Rig::HarmonySnapshot> ParagonX1AudioProcessor::getHarmonySnapshot (int slot) const
{
    const std::scoped_lock lock (harmonySnapshotsLock);
    return slot >= 0 && slot < Rig::numHarmonySnapshots ? harmonySnapshots[(size_t) slot] : std::nullopt;
}

void ParagonX1AudioProcessor::setHarmonySnapshot (int slot, std::optional<Rig::HarmonySnapshot> snapshot)
{
    if (slot < 0 || slot >= Rig::numHarmonySnapshots)
        return;
    {
        const std::scoped_lock lock (harmonySnapshotsLock);
        harmonySnapshots[(size_t) slot] = std::move (snapshot);
    }
    rigModified = true;
    requestCheckpoint();
}

void ParagonX1AudioProcessor::storeHarmonySnapshot (int slot)
{
    Rig::HarmonySnapshot snapshot;
    for (const auto& id : ParamIDs::harmonySnapshotIds())
        if (auto* parameter = parameters.getParameter (id))
            snapshot[id] = parameter->convertFrom0to1 (parameter->getValue());
    // Stage 10: the modulator slots on the same controls, as the guide's
    // snapshots store "Modulator assignments" (not the modulators' settings).
    snapshot["mod:stored"] = 1.0f;
    const auto ids = ParamIDs::harmonyModTargets();
    const auto stored = ParamIDs::harmonySnapshotIds();
    for (int target = 0; target < ids.size(); ++target)
        if (stored.contains (ids[target]))
            for (int s = 0; s < effects::HarmonyModulation::slotsPerTarget; ++s)
                if (const auto found = getHarmonyModSlot (target, s); found.source != 0)
                {
                    const auto key = "mod:" + ids[target] + ":" + juce::String (s) + ":";
                    snapshot[key + "source"] = (float) found.source;
                    snapshot[key + "depth"] = found.depth;
                }
    setHarmonySnapshot (slot, std::move (snapshot));
    // The one just stored is the one in use (without recalling it).
    appliedSnapshot = slot + 1;
    if (auto* selector = parameters.getParameter (ParamIDs::harmonySnapshot))
    {
        selector->beginChangeGesture();
        selector->setValueNotifyingHost (selector->convertTo0to1 ((float) appliedSnapshot));
        selector->endChangeGesture();
    }
}

void ParagonX1AudioProcessor::applyHarmonySnapshot (const Rig::HarmonySnapshot& snapshot)
{
    // Every stored setting (one the snapshot doesn't have, from an older
    // version, goes back to its default), as one edit.
    for (const auto& id : ParamIDs::harmonySnapshotIds())
    {
        auto* parameter = parameters.getParameter (id);
        if (parameter == nullptr)
            continue;
        const auto found = snapshot.find (id);
        const auto normalised = found != snapshot.end()
                                    ? parameter->convertTo0to1 (parameter->getNormalisableRange().snapToLegalValue (found->second))
                                    : parameter->getDefaultValue();
        if (juce::approximatelyEqual (normalised, parameter->getValue()))
            continue;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (normalised);
        parameter->endChangeGesture();
    }

    // The modulator slots on those controls (a snapshot stored before stage
    // 10 has none: the slots stay as they are).
    if (snapshot.find ("mod:stored") == snapshot.end())
        return;
    const auto ids = ParamIDs::harmonyModTargets();
    const auto stored = ParamIDs::harmonySnapshotIds();
    for (int target = 0; target < ids.size(); ++target)
    {
        if (! stored.contains (ids[target]))
            continue;
        for (int s = 0; s < effects::HarmonyModulation::slotsPerTarget; ++s)
        {
            const auto key = "mod:" + ids[target] + ":" + juce::String (s) + ":";
            const auto source = snapshot.find (key + "source"), depth = snapshot.find (key + "depth");
            const Rig::HarmonyModSlot wanted { source != snapshot.end() ? juce::roundToInt (source->second) : 0,
                                               depth != snapshot.end() ? depth->second : 0.0f };
            if (! (getHarmonyModSlot (target, s) == wanted))
                setHarmonyModSlot (target, s, wanted);
        }
    }
}

void ParagonX1AudioProcessor::recallHarmonySnapshot (int slot)
{
    const auto snapshot = getHarmonySnapshot (slot);
    if (! snapshot.has_value())
        return;
    applyHarmonySnapshot (*snapshot);
    appliedSnapshot = slot + 1;
    if (auto* selector = parameters.getParameter (ParamIDs::harmonySnapshot))
    {
        selector->beginChangeGesture();
        selector->setValueNotifyingHost (selector->convertTo0to1 ((float) appliedSnapshot));
        selector->endChangeGesture();
    }
}

void ParagonX1AudioProcessor::followSnapshotParameter()
{
    // SNAPSHOT moved by the DAW (automation) or a MIDI controller: bring
    // that snapshot back (an empty slot does nothing).
    const auto wanted = juce::roundToInt (parameters.getRawParameterValue (ParamIDs::harmonySnapshot)->load());
    if (wanted == appliedSnapshot)
        return;
    appliedSnapshot = wanted;
    if (wanted > 0)
        if (const auto snapshot = getHarmonySnapshot (wanted - 1))
            applyHarmonySnapshot (*snapshot);
}

//==============================================================================
void ParagonX1AudioProcessor::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (gestureIsStarting)
    {
        ++activeGestures;
    }
    else
    {
        // Never below zero, even if a host sends an unmatched "end".
        auto active = activeGestures.load();
        while (active > 0 && ! activeGestures.compare_exchange_weak (active, active - 1)) {}
        requestCheckpoint();
    }
}

ParagonX1AudioProcessor::Snapshot ParagonX1AudioProcessor::takeSnapshot() const
{
    return { captureRig(), currentRig, rigModified.load() };
}

void ParagonX1AudioProcessor::recordCheckpointIfReady()
{
    // Wait until the edit is really finished: no knob still held.
    if (! checkpointWanted.load() || activeGestures.load() > 0)
        return;

    checkpointWanted = false;
    history.record (takeSnapshot());
}

void ParagonX1AudioProcessor::restoreSnapshot (const Snapshot& snapshot)
{
    applyRig (snapshot.rig);
    currentRig = snapshot.current;
    rigModified = snapshot.modified;
    rigBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::undo()
{
    // Record anything not yet recorded first, so undo takes back the latest
    // edit rather than the one before it.
    recordCheckpointIfReady();
    if (const auto snapshot = history.undo())
        restoreSnapshot (*snapshot);
}

void ParagonX1AudioProcessor::redo()
{
    recordCheckpointIfReady();
    if (const auto snapshot = history.redo())
        restoreSnapshot (*snapshot);
}

//==============================================================================
void ParagonX1AudioProcessor::tapTempo()
{
    if (const auto bpm = tapCounter.tap (juce::Time::getMillisecondCounterHiRes()))
        tappedBpm = std::round (*bpm * 10.0) / 10.0;   // to 0.1 BPM, as displayed
}

void ParagonX1AudioProcessor::clearTappedTempo()
{
    tappedBpm = 0.0;
    tapCounter = {};
}

//==============================================================================
void ParagonX1AudioProcessor::markModified() noexcept
{
    if (! applyingRig.load())
        rigModified = true;
}

void ParagonX1AudioProcessor::parameterChanged (const juce::String&, float)
{
    // Can be called on the audio thread (automation), so only touch the atomic.
    markModified();
}

void ParagonX1AudioProcessor::useRigFolder (const juce::File& folder)
{
    rigLibrary = std::make_unique<RigLibrary> (folder);
    rigBroadcaster.sendChangeMessage();
}

effects::Harmony::Fx ParagonX1AudioProcessor::harmonyFx (float delayMs, int sync, float feedback, int filter, float cutoffHz) const noexcept
{
    effects::Harmony::Fx fx;
    const auto syncs = (int) std::size (effects::Delay::syncBeats);
    if (sync > 0 && sync < syncs)
        delayMs = (float) (effects::Delay::syncBeats[sync] * 60000.0 / juce::jmax (20.0, getTempoBpm()));
    fx.delayMs = juce::jlimit (0.0f, (float) (effects::Harmony::maxEchoSeconds * 1000.0), delayMs);
    fx.feedback = feedback;
    fx.filter = filter;
    fx.cutoffHz = cutoffHz;
    return fx;
}

Rig ParagonX1AudioProcessor::captureRig() const
{
    Rig rig;
    rig.name = currentRig.name;
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! ParamIDs::notInRigs.contains (ranged->paramID))
                rig.parameters[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());
    rig.harmonyModSlots = captureHarmonyModSlots();
    for (int m = 0; m < effects::HarmonyModulation::numMods; ++m)
        rig.harmonyModShapes[(size_t) m] = getHarmonyModShape (m);
    const std::scoped_lock lock (harmonySnapshotsLock);
    rig.harmonySnapshots = harmonySnapshots;
    return rig;
}

void ParagonX1AudioProcessor::applyRig (const Rig& rig)
{
    applyingRig = true;

    for (auto* parameter : AudioProcessor::getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr || ParamIDs::notInRigs.contains (ranged->paramID))
            continue;

        // Anything the rig doesn't mention goes back to its default.
        const auto found = rig.parameters.find (ranged->paramID);
        const auto normalised = found != rig.parameters.end()
                                  ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                  : ranged->getDefaultValue();
        ranged->setValueNotifyingHost (normalised);
    }

    {
        const std::scoped_lock lock (harmonySnapshotsLock);
        harmonySnapshots = rig.harmonySnapshots;
    }
    restoreHarmonyModSlots (rig.harmonyModSlots);
    for (size_t m = 0; m < harmonyModShapes.size(); ++m)
        for (size_t i = 0; i < harmonyModShapes[m].size(); ++i)
            harmonyModShapes[m][i] = rig.harmonyModShapes[m][i];
    appliedSnapshot = juce::roundToInt (parameters.getRawParameterValue (ParamIDs::harmonySnapshot)->load());   // loading isn't a recall

    applyingRig = false;
    rigModified = false;
}

void ParagonX1AudioProcessor::loadRig (const RigLibrary::Entry& entry)
{
    // Copy first: applying parameters can't invalidate it, but be safe.
    const auto rig = entry.rig;
    currentRig = { rig.name, entry.file, entry.isFactory ? rig.name : juce::String() };
    applyRig (rig);
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

const RigLibrary::Entry* ParagonX1AudioProcessor::findCurrentRigEntry() const
{
    if (currentRig.isUserRig())
        return rigLibrary->find (currentRig.file);
    if (currentRig.factoryName.isNotEmpty())
        return rigLibrary->findFactory (currentRig.factoryName);
    return nullptr;
}

bool ParagonX1AudioProcessor::saveCurrentRig()
{
    const auto* entry = findCurrentRigEntry();
    if (entry == nullptr || entry->isFactory)
        return false;

    // Keep how it's filed; replace the sound.
    auto rig = captureRig();
    rig.name = entry->rig.name;
    rig.bank = entry->rig.bank;
    rig.number = entry->rig.number;
    rig.tags = entry->rig.tags;
    rig.favourite = entry->rig.favourite;

    const auto file = entry->file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    rigModified = false;
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::saveCurrentRigAs (const juce::String& name)
{
    auto rig = captureRig();
    rig.name = name;

    // File it next to the rig it came from, in the first free slot.
    if (const auto* entry = findCurrentRigEntry())
    {
        rig.bank = entry->rig.bank;
        rig.tags = entry->rig.tags;
    }
    rig.number = rigLibrary->nextFreeNumber (rig.bank);

    const auto file = rigLibrary->saveAsNew (rig);
    if (file == juce::File())
        return false;

    const auto* saved = rigLibrary->find (file);
    currentRig = { saved != nullptr ? saved->rig.name : name, file, {} };
    rigModified = false;
    // Saving isn't an edit to undo, but undo shouldn't bring back the old name either.
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

void ParagonX1AudioProcessor::stepRig (int direction)
{
    if (const auto* next = rigLibrary->step (findCurrentRigEntry(), direction))
        loadRig (*next);
}

bool ParagonX1AudioProcessor::updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails)
{
    if (entry.isFactory)
        return false;

    auto rig = entry.rig;
    rig.name = newDetails.name.trim().isEmpty() ? rig.name : newDetails.name.trim();
    rig.bank = newDetails.bank;
    rig.number = newDetails.number;
    rig.tags = newDetails.tags;

    const auto file = entry.file;
    const auto isCurrent = currentRig.file == file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    if (isCurrent)
    {
        currentRig.name = rig.name;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::deleteRig (const RigLibrary::Entry& entry)
{
    if (entry.isFactory)
        return false;

    const auto file = entry.file;
    if (! rigLibrary->remove (file))
        return false;

    // The sound is still loaded; it just isn't saved anywhere any more.
    if (currentRig.file == file)
    {
        currentRig.file = juce::File();
        rigModified = true;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* ParagonX1AudioProcessor::createEditor()
{
    return new ParagonX1AudioProcessorEditor (*this);
}

void ParagonX1AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (parameterVersionKey, parameterVersion, nullptr);
    state.setProperty (rigNameKey, currentRig.name, nullptr);
    state.setProperty (rigFileKey, currentRig.file.getFullPathName(), nullptr);
    state.setProperty (rigFactoryKey, currentRig.factoryName, nullptr);
    state.setProperty (rigModifiedKey, rigModified.load(), nullptr);
    state.setProperty (tappedBpmKey, tappedBpm.load(), nullptr);
    state.setProperty (expressionCcKey, expressionCc.load(), nullptr);
    state.setProperty (tunerReferenceKey, tunerReferenceHz.load(), nullptr);

    {
        // HARMONY's snapshots, beside the parameters.
        juce::XmlElement snapshots ("HarmonySnapshots");
        const std::scoped_lock lock (harmonySnapshotsLock);
        Rig::writeSnapshots (harmonySnapshots, snapshots);
        state.appendChild (juce::ValueTree::fromXml (snapshots), nullptr);
    }
    {
        // HARMONY's modulation slots, likewise.
        juce::XmlElement slots ("HarmonyModulation");
        Rig::writeModSlots (captureHarmonyModSlots(), slots);
        Rig::HarmonyModShapes shapes {};
        for (int m = 0; m < effects::HarmonyModulation::numMods; ++m)
            shapes[(size_t) m] = getHarmonyModShape (m);
        Rig::writeModShapes (shapes, slots);
        state.appendChild (juce::ValueTree::fromXml (slots), nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParagonX1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    // HARMONY's snapshots (none in a project saved before they existed).
    {
        auto saved = state.getChildWithName ("HarmonySnapshots");
        Rig::HarmonySnapshots loaded;
        if (saved.isValid())
        {
            if (auto snapshotsXml = saved.createXml())
                loaded = Rig::readSnapshots (*snapshotsXml);
            state.removeChild (saved, nullptr);
        }
        const std::scoped_lock lock (harmonySnapshotsLock);
        harmonySnapshots = loaded;
    }
    // HARMONY's modulation slots (none in a project saved before they existed).
    {
        auto saved = state.getChildWithName ("HarmonyModulation");
        Rig::HarmonyModSlots loaded;
        Rig::HarmonyModShapes shapes {};
        if (saved.isValid())
        {
            if (auto slotsXml = saved.createXml())
            {
                loaded = Rig::readModSlots (*slotsXml);
                shapes = Rig::readModShapes (*slotsXml);
            }
            state.removeChild (saved, nullptr);
        }
        restoreHarmonyModSlots (loaded);
        for (size_t m = 0; m < harmonyModShapes.size(); ++m)
            for (size_t i = 0; i < harmonyModShapes[m].size(); ++i)
                harmonyModShapes[m][i] = shapes[m][i];
    }

    // Parameters whose default changed: a project saved before they existed
    // gets the setting it had then, so it sounds the same.
    const auto addIfMissing = [&state] (const char* id, int value)
    {
        if (state.getChildWithProperty ("id", id).isValid())
            return;
        juce::ValueTree parameter ("PARAM");
        parameter.setProperty ("id", id, nullptr);
        parameter.setProperty ("value", value, nullptr);
        state.appendChild (parameter, nullptr);
    };
    addIfMissing (ParamIDs::driveModel, 0);   // before the drive had a MODEL: the original (Classic), for convertOldDrive
    addIfMissing (ParamIDs::reverbType, (int) effects::Reverb::Type::classic);    // before the reverb had a TYPE
    addIfMissing (ParamIDs::compType, (int) effects::CompGate::Type::classic);    // before 0.41.0 the compressor started on Classic

    const auto get = [&state] (const juce::String& id) -> std::optional<float>
    {
        const auto saved = state.getChildWithProperty ("id", id);
        return saved.isValid() ? std::optional<float> ((float) saved["value"]) : std::nullopt;
    };
    const auto set = [&state] (const juce::String& id, float value)
    {
        auto saved = state.getChildWithProperty ("id", id);
        if (! saved.isValid())
        {
            saved = juce::ValueTree ("PARAM");
            saved.setProperty ("id", id, nullptr);
            state.appendChild (saved, nullptr);
        }
        saved.setProperty ("value", value, nullptr);
    };
    const auto savedWith = (int) state.getProperty (parameterVersionKey, 0);
    // Saved with MODULATION on its old Doubler type: now the DOUBLER stomp...
    if (savedWith < 26)
        moveOldDoublerSettings ([&get] (const char* id) { return get (id); }, [&set] (const char* id, float value) { set (id, value); });
    // ...and the two-take DOUBLER (RATE, DEPTH, MIX): now its four voices.
    if (savedWith < 30)
        convertOldDoubler (get, set);
    // ...and 0.7.0's DOUBLER, before DRIFT and its stronger start.
    else if (savedWith < 32)
        keepDoublerAsSaved (get, set);
    // Saved with a pedal drive model: now MARS, set up to sound close to it.
    if (savedWith < 36)
        convertOldDrive (get, set);
    // Saved with the original rotary speaker: now CYCLONE, set to match.
    if (savedWith < 37)
        convertOldRotary (get, set);
    parameters.replaceState (state);
    appliedSnapshot = juce::roundToInt (parameters.getRawParameterValue (ParamIDs::harmonySnapshot)->load());   // loading isn't a recall

    // Restoring the project changed parameters, which marked the rig as
    // modified; put back what was saved instead.
    const auto rigFile = state[rigFileKey].toString();
    currentRig = { state.getProperty (rigNameKey, "Untitled").toString(),
                   rigFile.isNotEmpty() ? juce::File (rigFile) : juce::File(),
                   state[rigFactoryKey].toString() };
    rigModified = (bool) state.getProperty (rigModifiedKey, false);
    tappedBpm = (double) state.getProperty (tappedBpmKey, 0.0);
    setExpressionCc ((int) state.getProperty (expressionCcKey, 11));
    setTunerReferenceHz ((double) state.getProperty (tunerReferenceKey, 440.0));

    // A different project or preset: start the undo history afresh from it.
    history.clear();
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

// JUCE calls this to create new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParagonX1AudioProcessor();
}
