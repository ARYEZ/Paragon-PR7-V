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
    };

    explicit HarmonyModsView (juce::AudioProcessorValueTreeState&);
    ~HarmonyModsView() override;

    void setActions (Actions);
    std::function<void()> onDone;

    // The tab showing: 0-3 M1-M4, 4 AM, 5 PT.
    void showTab (int tab);
    int getTab() const noexcept { return tab; }
    static constexpr int numTabs = Mod::numMods + 2;
    // The source a tab is about.
    static int sourceOf (int tab) noexcept { return tab < Mod::numMods ? Mod::m1 + tab : tab == Mod::numMods ? Mod::am : Mod::pt; }
    // Each source's colour (its knobs' rings and graph markers too); a control moved by several: white.
    static juce::Colour colourOf (int source);
    // What the tab's modulator is on: "VOICE 1 PITCH +4%  SPREAD -100%".
    juce::String assignmentsText() const;
    // A control's name for people: "VOICE 1 PITCH", "NOTES LEVEL", "SPREAD".
    static juce::String targetName (const juce::String& parameterId);

    // The SEQ steps' bars, and setting a step from a point on them (public for tests).
    juce::Rectangle<float> stepArea() const { return display; }
    void setStepAt (juce::Point<float>);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

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
    void bind();   // the controls to the tab's modulator
    void setUp (Knob&, const juce::String& name);
    void setUp (List&, const juce::String& name, const juce::StringArray& items);
    float value (const juce::String& id) const;
    bool isSequencer() const;

    juce::AudioProcessorValueTreeState& parameters;
    Actions actions;
    int tab = 0;
    std::array<juce::TextButton, numTabs> tabs;
    juce::TextButton done { "DONE" };
    List type, shape, sync, trigger, play;
    Knob steps, rate, phase, warp, smooth, level;
    Knob amAttack, amRelease, amLevel, ptSmooth, ptOffset, ptLevel;
    juce::Rectangle<float> display;      // the shape (or the steps) drawn
    juce::Rectangle<int> header;         // the assignments' line
    juce::String shownAssignments;
};
