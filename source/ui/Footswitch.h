#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

#include "PerformanceControls.h"

// One of the four footswitches along the bottom of the pedal: a status LED,
// a big rubber pad, and the block's name printed on the metal. Pressing the
// pad turns the block on or off; clicking the name selects the block so its
// knobs show in the edit panel.
//
// A footswitch can control several on/off parameters at once (DYNAMICS:
// the compressor and the de-esser; PITCH: harmony, the octaver and TUNE
// VOICE). A parameter counts as on above 0.5 (so a choice like TUNE VOICE
// is on at anything but Off). If any of them is on, the LED is lit and
// pressing turns them all off, remembering which were on and how; pressing
// again brings exactly those back. With nothing remembered (the first
// press), it turns on `firstOnIds`, or all of them if that's empty.
class Footswitch final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    Footswitch (int number, const juce::String& name, juce::AudioProcessorValueTreeState& parameters,
                juce::StringArray onParameterIds, juce::StringArray firstOnIds = {});

    // Returns the text under the name, e.g. "Drive 5.0  Tone 5.0".
    std::function<juce::String()> describe;
    std::function<void()> onSelect;
    // Optional: holding the pad down does something else (footswitch 3:
    // Cyclone's SLOW / FAST). Return true if it did, so letting go doesn't
    // also switch the block.
    std::function<bool()> onHold;

    void setSelected (bool shouldBeSelected);

    // Re-reads the parameters now (also happens on a timer).
    void refresh();
    bool isAnyOn() const;
    void toggle();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }

    int number;
    juce::String name;
    juce::AudioProcessorValueTreeState& parameters;
    juce::StringArray onParameterIds, firstOnIds;
    std::map<juce::String, float> wereOn;   // what was on (0 to 1) when it was last switched off
    bool selected = false, lastOn = false;
    juce::String summary;
    juce::Rectangle<int> ledArea, switchArea, nameArea;
    HoldDetector hold { 500 };
    bool pressedPad = false, heldPad = false;
};
