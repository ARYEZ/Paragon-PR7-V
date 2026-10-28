#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>

#include "LevelMeterSource.h"
#include "dsp/InputTap.h"
#include "dsp/TapTempo.h"
#include "dsp/effects/CompGate.h"
#include "dsp/effects/DeEsser.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Doubler.h"
#include "dsp/effects/FilterPitch.h"
#include "dsp/effects/HarmonyModulation.h"
#include "dsp/effects/GraphicEq.h"
#include "dsp/effects/Harmony.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Vocoder.h"
#include "dsp/effects/Limiter.h"
#include "dsp/effects/LowCut.h"
#include "dsp/effects/Mars.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/PitchBlock.h"
#include "dsp/effects/TalkBox.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/Presence.h"
#include "dsp/effects/Reverb.h"
#include "dsp/effects/Wah.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"

// Which rig is loaded, for the header display and the Save button.
struct CurrentRig
{
    juce::String name { "Untitled" };
    juce::File file;              // set for the user's own rigs
    juce::String factoryName;     // set for factory rigs
    bool isUserRig() const { return file != juce::File(); }

    bool operator== (const CurrentRig& other) const
    {
        return name == other.name && file == other.file && factoryName == other.factoryName;
    }
};

class ParagonX1AudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AudioProcessorParameter::Listener
{
public:
    ParagonX1AudioProcessor();
    ~ParagonX1AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // MIDI CC 11 moves the expression pedal
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The DAW's own bypass button drives our BYPASS parameter, so the two
    // can't disagree, and bypassing keeps the plugin's latency.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Presets live in the rig browser (Phase 4), so the host's program list is a single entry.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Rigs (message thread only)
    RigLibrary& getRigLibrary() noexcept { return *rigLibrary; }

    // Points the library at another folder. For tests.
    void useRigFolder (const juce::File& folder);

    // The current sound as a rig (not saved anywhere yet).
    Rig captureRig() const;

    // HARMONY's SNAPSHOTS (stage 8): 32 stored sets of the harmony voices'
    // settings, saved with the rig and the project, part of undo. Slots
    // are 0-31. Recalling sets the SNAPSHOT parameter too; the timer
    // recalls when that parameter is changed from the DAW or by MIDI.
    std::optional<Rig::HarmonySnapshot> getHarmonySnapshot (int slot) const;
    void storeHarmonySnapshot (int slot);                                    // the voices as they are now
    void setHarmonySnapshot (int slot, std::optional<Rig::HarmonySnapshot>); // paste one, or empty the slot
    void recallHarmonySnapshot (int slot);
    void followSnapshotParameter();   // the timer: recall if SNAPSHOT was moved (by the DAW or MIDI); public for tests

    // HARMONY's modulation slots (stage 9): `target` is a control's place in
    // ParamIDs::harmonyModTargets() (-1: not one), `slot` 0-3. Saved with
    // the rig and the project, part of undo. Message thread.
    static int harmonyModTarget (const juce::String& parameterId);
    Rig::HarmonyModSlot getHarmonyModSlot (int target, int slot) const;
    void setHarmonyModSlot (int target, int slot, Rig::HarmonyModSlot);
    // How far the modulation has a control moved right now, as a fraction of
    // its range (for its knob's ring; any thread).
    float getHarmonyModOffset (int target) const noexcept;
    // M1-M4's drawn shapes (stage 10): saved and undone like the slots.
    std::array<float, 32> getHarmonyModShape (int mod) const;
    void setHarmonyModShape (int mod, const std::array<float, 32>&);
    const effects::HarmonyModulation& getHarmonyModulation() const noexcept { return harmonyModulation; }

    void loadRig (const RigLibrary::Entry& entry);

    // Saves over the current rig if it's one of the user's own and returns
    // true; returns false if it isn't (a factory rig or never saved), in
    // which case the caller should ask for a name and use saveCurrentRigAs.
    bool saveCurrentRig();
    bool saveCurrentRigAs (const juce::String& name);

    // Loads the next (+1) or previous (-1) rig in the library.
    void stepRig (int direction);

    // Rename, re-file or re-tag a user rig from the browser.
    bool updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails);
    bool deleteRig (const RigLibrary::Entry& entry);

    CurrentRig getCurrentRig() const { return currentRig; }
    const RigLibrary::Entry* findCurrentRigEntry() const;

    // True once anything in the rig has changed since it was loaded or saved.
    bool isRigModified() const noexcept { return rigModified.load(); }

    // Tells listeners (the editor) that the rig list or current rig changed.
    juce::ChangeBroadcaster rigBroadcaster;

    //==========================================================================
    // UNDO / REDO (message thread only)
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();

    // Records a finished edit now instead of on the next timer tick. For tests.
    void flushUndoStep() { recordCheckpointIfReady(); }

    //==========================================================================
    // Tempo

    // The DAW's tempo, as last seen by the audio thread (120 if it hasn't said).
    double getHostBpm() const noexcept { return hostBpm.load(); }

    // TAP: each call is one tap (message thread). Once tapped, the tapped
    // tempo is used instead of the DAW's until clearTappedTempo().
    void tapTempo();
    void clearTappedTempo();
    double getTappedBpm() const noexcept { return tappedBpm.load(); }   // 0 when following the DAW
    double getTempoBpm() const noexcept;                                 // whichever is in use

    //==========================================================================
    // Tuner: the voice, for the editor's tuner to measure.
    const InputTap& getInputTap() const noexcept { return inputTap; }

    // The expression pedal's position after MIDI (0 heel to 1 toe), for the UI.
    float getExpressionPosition() const noexcept;

    //==========================================================================
    // SETTINGS page. Saved with the project.

    // Which MIDI controller moves the expression pedal (11 = "expression").
    int getExpressionCc() const noexcept { return expressionCc.load(); }
    void setExpressionCc (int cc) noexcept { expressionCc = juce::jlimit (1, 127, cc); }

    // The tuner's reference pitch for A4 (440 Hz unless the band tunes otherwise).
    double getTunerReferenceHz() const noexcept { return tunerReferenceHz.load(); }
    void setTunerReferenceHz (double hz) noexcept { tunerReferenceHz = juce::jlimit (420.0, 460.0, hz); }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getInputMeter() noexcept { return inputMeter; }
    // How hard the compressor is working (dB, 0 or less), for its meter.
    float getCompGainReductionDb() const noexcept { return compGate.getGainReductionDb(); }
    // The note HARMONY hears being sung (a MIDI number), or -1 between notes. For its panel.
    float getHarmonySungNote() const noexcept { return harmony.getSungNote(); }
    // The note HARMONY's CORRECT is tuning the voice to (a MIDI number), or -1. For its panel.
    float getHarmonyCorrectNote() const noexcept { return harmony.getCorrectNote(); }
    // HARMONY itself, for its panel to show the note voices (its getters are safe from the message thread).
    const effects::Harmony& getHarmony() const noexcept { return harmony; }
    // Whether a MIDI key is held (for HARMONY's keyboard strip).
    bool isMidiKeyHeld (int key) const noexcept { return shownMidiKeys[(size_t) juce::jlimit (0, 127, key)].load() > 0.0f; }

    // CORRECT needs the plugin to run a little late; this tells the DAW
    // (from the message thread: the timer calls it, and tests can).
    void updateLatency();
    // DOUBLER's MOD RESET: start every voice's wobble together.
    void resetDoublerModulation() noexcept { doubler.resetModulation(); }
    // How hard MARS's band is being driven, for its panel's display.
    float getMarsBandDrive (int band) const noexcept { return mars.getBandDrive (band); }
    // MARS itself, for its panel to show the modulation (its getters are safe from the message thread).
    const effects::Mars& getMars() const noexcept { return mars; }
    const effects::Vocoder& getVocoder() const noexcept { return vocoder; }
    // How far the de-esser is turning the esses down (dB, 0 or less), for its meter.
    float getDeEsserReductionDb() const noexcept { return deEsser.getReductionDb(); }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }
    // How far the master LIMITER is turning the sound down (dB, 0 or less), for its light.
    float getLimiterReductionDb() const noexcept { return limiter.getReductionDb(); }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples);
    void readHostTempo();
    float param (const char* id) const;
    bool isOn (const char* id) const { return param (id) > 0.5f; }

    void timerCallback() override;
    int wantedLatency() const noexcept;
    std::atomic<int> correctLatency { 0 };   // what the audio thread runs HARMONY's CORRECT at (0: off)
    void parameterChanged (const juce::String& parameterId, float newValue) override;
    void markModified() noexcept;
    void applyRig (const Rig& rig);
    void applyHarmonySnapshot (const Rig::HarmonySnapshot&);
    Rig::HarmonySnapshots harmonySnapshots;     // message thread (and state saving: hence the lock)
    mutable std::mutex harmonySnapshotsLock;
    int appliedSnapshot = 0;                    // the SNAPSHOT value last acted on

    // Undo snapshots: the sound and which rig it is.
    struct Snapshot
    {
        Rig rig;
        CurrentRig current;
        bool modified = false;   // restored too, but doesn't count as a change

        bool operator== (const Snapshot& other) const
        {
            return rig.parameters == other.rig.parameters && rig.harmonySnapshots == other.rig.harmonySnapshots
                   && rig.harmonyModSlots == other.rig.harmonyModSlots && rig.harmonyModShapes == other.rig.harmonyModShapes
                   && current == other.current;
        }
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    void requestCheckpoint() noexcept { checkpointWanted = true; }
    void recordCheckpointIfReady();

    // Knob drags and button clicks start and end "gestures"; an edit is
    // finished (and worth an undo step) when its gesture ends.
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    void readMidiExpression (const juce::MidiBuffer&) noexcept;
    void syncMidiExpressionToParameter();
    float expressionAmount() const noexcept;   // position mapped through MIN/MAX

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;

    // Smoothing stops zipper noise when a gain knob moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain, outputGain;

    // LOW CUT, straight after the INPUT gain: before anything else.
    effects::LowCut lowCut;
    // LIMITER, after OUTPUT and the volume pedal: last, so it catches everything.
    effects::Limiter limiter;

    // Signal chain, in order: pitch -> wah -> compGate -> filterPitch -> drive
    // -> presence -> talk box -> graphic EQ -> doubler -> modulation -> delay
    // -> reverb -> lo-fi. Up to the EQ it's mono; from the doubler on it's
    // stereo when the track is.
    effects::CompGate compGate;
    effects::DeEsser deEsser;           // DYNAMICS' de-esser, after the compressor
    effects::FilterPitch filterPitch;   // after the compressor, before the drive
    effects::Mars mars;   // SATURATION
    struct MarsBandParameters { std::atomic<float>* parts[12] {}; };   // in marsBandParts' order
    std::array<MarsBandParameters, effects::Mars::maxBands> marsBandParameters {};
    // MARS's modulation settings, in ParamIDs' part orders (marsLfoParts...).
    struct MarsModulationParameters
    {
        using Mod = effects::MarsModulation;
        using Value = std::atomic<float>*;
        struct Lfo { std::array<Value, 8> parts {}; std::array<std::array<Value, 4>, Mod::maxSteps> steps {}; };
        std::array<Lfo, Mod::numLfos> lfos {};
        std::array<std::array<Value, 12>, Mod::numEgs> egs {};
        std::array<std::array<Value, 5>, Mod::numEfs> efs {};
        std::array<std::array<Value, 3>, Mod::numMidis> midis {};
        std::array<std::array<Value, 4>, Mod::numXys> xys {};
        std::array<std::array<Value, 3>, Mod::numSlots> slots {};
        void bind (juce::AudioProcessorValueTreeState&);
        void read (Mod::Settings&) const noexcept;
    } marsModulationParameters;
    // The Note Map Editor's parameters, found once (their IDs are built strings).
    std::array<std::array<std::atomic<float>*, 3>, 12> harmonyMapParameters {};
    std::array<std::atomic<float>*, 12> harmonyScaleNoteParameters {};
    effects::Harmony::MidiKeys harmonyMidi;        // the MIDI keys held, for HARMONY (audio thread only)
    std::array<std::atomic<float>, 128> shownMidiKeys {};   // the same, for the panel
    effects::MarsModulation::MidiState marsMidi;   // what MIDI has said, for MARS's MIDI sources (audio thread only)
    effects::Presence presence;
    effects::Harmony harmony;   // HARMONY: first in the stereo part, before the doubler
    juce::AudioBuffer<float> cleanVoice;   // the voice just after LOW CUT, for HARMONY to read the note from
    // HARMONY's eight voices' settings, looked up once rather than by name every block.
    struct HarmonyVoiceParameters { std::atomic<float>* parts[15] {}; };   // harmonyVoiceParts
    // A HARMONY voice's (or the note voices') DELAY, FEEDBACK and FILTER, a
    // synced DELAY turned into ms at the tempo now.
    effects::Harmony::Fx harmonyFx (float delayMs, int sync, float feedback, int filter, float cutoffHz) const noexcept;
    std::array<HarmonyVoiceParameters, effects::Harmony::numVoices> harmonyVoiceParameters {};
    // Stage 9: the modulators, their parameters (found once) and the slots.
    effects::HarmonyModulation harmonyModulation;
    struct HarmonyModParameters
    {
        struct Mod { std::atomic<float>* parts[11] {}; std::array<std::atomic<float>*, effects::HarmonyModulation::maxSteps> steps {}; };   // harmonyModParts
        std::array<Mod, effects::HarmonyModulation::numMods> mods {};
        std::atomic<float> *amAttack = nullptr, *amRelease = nullptr, *amLevel = nullptr, *ptSmooth = nullptr, *ptOffset = nullptr, *ptLevel = nullptr;
        std::array<std::array<std::atomic<float>*, 7>, effects::HarmonyModulation::numEnvelopes> envelopes {};   // harmonyEnvelopeParts
        std::atomic<float> *spMode = nullptr, *spTrigger = nullptr, *spSpread = nullptr, *spLevel = nullptr;
        void bind (juce::AudioProcessorValueTreeState&);
        void read (effects::HarmonyModulation::Settings&) const noexcept;
    } harmonyModParameters;
    struct HarmonyModSlotState { std::atomic<int> source { 0 }; std::atomic<float> depth { 0.0f }; };
    struct HarmonyModTarget
    {
        std::atomic<float>* value = nullptr;
        juce::NormalisableRange<float> range;
        bool pitch = false;
    };
    std::vector<HarmonyModTarget> harmonyModTargets;                        // ParamIDs::harmonyModTargets(), found once
    std::unique_ptr<std::array<HarmonyModSlotState, 4>[]> harmonyModSlots;  // by target
    std::unique_ptr<std::atomic<float>[]> harmonyModShown;                  // each target's offset now, for the panel
    bool harmonyModInUse() const noexcept;
    void applyHarmonyModulation (effects::Harmony::Settings&, const effects::HarmonyModulation::Settings&) noexcept;
    void processHarmony (juce::dsp::AudioBlock<float>, const float* cleanVoice, const effects::Harmony::Settings&, double songBeats);
    Rig::HarmonyModSlots captureHarmonyModSlots() const;
    void restoreHarmonyModSlots (const Rig::HarmonyModSlots&);
    bool harmonyMidiNoteStarted = false, harmonyMidiHeldBefore = false;   // audio thread: a MIDI note began this block
    effects::HarmonyModulation::Midi harmonyModMidi;                       // audio thread: what MIDI has said, for the MIDI sources
    std::array<std::array<std::atomic<float>, 32>, effects::HarmonyModulation::numMods> harmonyModShapes {};
    int lastGeneratedNote = -1;
    // The VOCODER: after HARMONY, the voice (as it leaves the mono part) doing the talking.
    effects::Vocoder vocoder;
    juce::AudioBuffer<float> vocoderVoice;
    struct VocoderParameters
    {
        std::array<std::array<std::atomic<float>*, 8>, 2> osc {};   // vocoderOscParts
        std::array<std::atomic<float>*, effects::Vocoder::curvePoints> bandVol {}, bandPan {};
        std::array<std::atomic<float>*, effects::Vocoder::numIntervals> intervals {};
    } vocoderParameters;
    void readVocoder (effects::Vocoder::Settings&, const Scales::Custom&) const noexcept;
    effects::Doubler doubler;
    struct DoublerVoiceParameters { std::atomic<float>* parts[10] {}; };
    std::array<DoublerVoiceParameters, effects::Doubler::numVoices> doublerVoiceParameters {};
    effects::Modulation modulation;
    effects::Delay delay;
    effects::Reverb reverb;
    effects::LoFi loFi;   // last: after the reverb
    effects::Glitch glitch;
    effects::PitchBlock pitchBlock;
    effects::TalkBox talkBox;   // FILTER / PITCH's Talk Box type, after the drive and presence
    effects::Wah wah;           // the expression pedal's WAH, after PITCH
    effects::GraphicEq graphicEq;   // after presence, before the doubler
    std::array<std::atomic<float>*, effects::GraphicEq::numBands> eqGainDb {};

    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    // Atomic because the DAW's automation changes parameters on the audio thread.
    std::atomic<bool> rigModified { false }, applyingRig { false };

    // BYPASS: the clean input, delayed by our latency (none at present) so
    // switching bypass never shifts the voice in time, crossfaded in over 20 ms.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypassDelay { 8192 };
    juce::AudioBuffer<float> bypassDry;
    juce::SmoothedValue<float> bypassAmount;

    // The expression pedal's VOLUME, and the tuner's mute.
    juce::SmoothedValue<float> expressionVolume, tunerOpen;

    // The last MIDI expression value not yet copied into the expression parameter (-1 = none).
    std::atomic<float> pendingMidiExpression { -1.0f };
    std::atomic<int> expressionCc { 11 };
    std::atomic<double> tunerReferenceHz { 440.0 };

    InputTap inputTap { 32768 };   // enough for the tuner's window at 192 kHz

    UndoHistory<Snapshot> history;
    std::atomic<int> activeGestures { 0 };
    std::atomic<bool> checkpointWanted { true };

    int preparedBlockSize = 512;
    std::atomic<double> hostBpm { 120.0 }, tappedBpm { 0.0 };
    double songBeatsAtBlock = -1.0;   // the song position at this block's start, in beats; -1 when not playing (audio thread only)
    TapTempo tapCounter;

    LevelMeterSource inputMeter, outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
