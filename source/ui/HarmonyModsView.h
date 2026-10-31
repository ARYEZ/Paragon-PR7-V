#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/effects/HarmonyModulation.h"
#include "rigs/Rig.h"

// HARMONY's MODS page (stage 9), inspired by the modulator panels of Waves
// Harmony: one tab per modulator, opened by MODS, over the graph and the
// controls (on the rack: one wide screen).
//
//   M1-M4   TYPE (LFO / SEQ), SHAPE, or STEPS and the steps themselves
//           (drag a bar up or down: -24 to +24), RATE (or SYNC), PHASE,
//           WARP, SMOOTH, LEVEL, TRIGGER and PLAY; the shape drawn with a
//           dot riding it as it runs
//   AM      ATTACK, RELEASE, LEVEL; how loud the voice is now
//   PT      SMOOTH, OFFSET, LEVEL; where the pitch sung puts it now
//
// Stage 10:
//   A1-A2   ATTACK, DECAY, SUSTAIN, RELEASE and their CURVEs; the envelope
//           drawn, with each note's level now beside it
//   SP      MODE, TRIGGER, SPREAD, LEVEL; each note sounding as a dot at its value
//   MIDI    what Velocity, Keyboard, the wheels and Aftertouch read now
//   SHAPE Drawn: draw on M1-M4's display (ERASE flattens it)
//
// Along the top, the controls the tab's modulator is on. Modulators go on a
// control by right-clicking the control's knob (HarmonyPanel).
class HarmonyModsView final : public juce::Component,
                              private juce::Timer
{
public:
    using Mod = effects::HarmonyModulation;

    // What it reads from the processor (the live values, the slots).
    struct Actions
    {
        std::function<float (int source)> value;            // a source's value now
        std::function<float (int mod)> position;            // M1-M4's place in their cycle (0 to 1)
        std::function<Rig::HarmonyModSlot (int target, int slot)> slot;
        std::function<float (int source, int voice)> voiceValue;   // a note-by-note source's value for a note voice
        std::function<int (int voice)> voiceNote;                  // a note voice's note (-1: none)
        std::function<std::array<float, 32> (int mod)> shape;      // M1-M4's drawn shapes
        std::function<void (int mod, const std::array<float, 32>&)> setShape;
    };

    explicit HarmonyModsView (juce::AudioProcessorValueTreeState&);
    ~HarmonyModsView() override;

    void setActions (Actions);
    std::function<void()> onDone;

    // The tab showing: 0-3 M1-M4, 4-5 A1-A2, 6 AM, 7 PT, 8 SP, 9 MIDI.
    void showTab (int tab);
    int getTab() const noexcept { return tab; }
    enum Tab { a1Tab = Mod::numMods, a2Tab, amTab, ptTab, spTab, midiTab, numTabs };
    // The source a tab is about (MIDI: the first of its five).
    static int sourceOf (int tab) noexcept
    {
        constexpr int sources[] = { Mod::m1, Mod::m2, Mod::m3, Mod::m4, Mod::a1, Mod::a2, Mod::am, Mod::pt, Mod::sp, Mod::velocity };
        return sources[juce::jlimit (0, (int) numTabs - 1, tab)];
    }
    // Whether a source is shown on a tab (the MIDI tab shows all five of MIDI's).
    static bool tabShows (int tab, int source) noexcept { return tab == midiTab ? source >= Mod::velocity : source == sourceOf (tab); }
    // Each source's colour (its knobs' rings and graph markers too); a control moved by several: white.
    static juce::Colour colourOf (int source);
    // What the tab's modulator is on: "VOICE 1 PITCH +4%  SPREAD -100%".
    juce::String assignmentsText() const;
    // A control's name for people: "VOICE 1 PITCH", "NOTES LEVEL", "SPREAD".
    static juce::String targetName (const juce::String& parameterId);

    // The SEQ steps' bars, and setting a step from a point on them (public for tests).
    juce::Rectangle<float> stepArea() const { return display; }
    void setStepAt (juce::Point<float>);
    // A drawn shape: sets the point under `point` (and those passed since `from`, so a fast drag leaves no gaps).
    void drawAt (juce::Point<float> point, std::optional<juce::Point<float>> from = {});
    bool isDrawing() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct List
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void timerCallback() override;
    void bind();   // the controls to the tab's modulator (or envelope)
    void attach (Knob&, const juce::String& id);
    void paintVoices (juce::Graphics&, juce::Rectangle<float> area, int source, juce::Colour) const;
    void setUp (Knob&, const juce::String& name);
    void setUp (List&, const juce::String& name, const juce::StringArray& items);
    float value (const juce::String& id) const;
    bool isSequencer() const;

    juce::AudioProcessorValueTreeState& parameters;
    Actions actions;
    int tab = 0;
    std::array<juce::TextButton, numTabs> tabs;
    juce::TextButton done { "DONE" }, erase { "ERASE" };
    List type, shape, sync, trigger, play;
    Knob steps, rate, phase, warp, smooth, level;
    Knob amAttack, amRelease, amLevel, ptSmooth, ptOffset, ptLevel;
    Knob envAttack, envDecay, envSustain, envRelease, envAttackCurve, envDecayCurve, envReleaseCurve;
    List spMode, spTrigger;
    Knob spSpread, spLevel;
    std::optional<juce::Point<float>> lastDraw;
    juce::Rectangle<float> display;      // the shape (or the steps) drawn
    juce::Rectangle<int> header;         // the assignments' line
    juce::String shownAssignments;
    std::vector<int> displaySignature() const;   // what the display draws that can change
    std::vector<int> shownDisplay;
    std::vector<juce::RangedAudioParameter*> modParameters;
};
