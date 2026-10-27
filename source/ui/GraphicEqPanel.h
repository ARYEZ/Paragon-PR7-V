#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/ParagonLookAndFeel.h"

#include "dsp/effects/GraphicEq.h"

// The graphic EQ's knobs, shown in place of the rig summary when the EQ
// block is selected: nine vertical faders standing at their own
// frequencies on a log scale, over a live drawing of the EQ's response on
// the same scale (so the curve passes through the faders' caps), plus LEVEL,
// an ON light, a PRESETS menu of common curves, and FLAT to reset every band.
class GraphicEqPanel final : public juce::Component
{
public:
    explicit GraphicEqPanel (juce::AudioProcessorValueTreeState&);

    // The colour of the faders, the curve and the header icon: the EQ block's colour in the chain.
    void setAccent (juce::Colour colour);

    // On the rack unit's face (see Faceplate), at full size: no dark screen
    // (the rack's display shows the curve), the faders evenly spaced as on
    // a hardware graphic EQ, each value on a small screen over its fader and
    // its frequency stamped under it; ON and FLAT as the unit's keys, PRESETS
    // on a screen.
    void setRack (bool inRack);
    bool isRack() const noexcept { return rack; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool showTitle = true;   // its icon and name in the header (not on the rack, which shows the name)
    void resized() override;

private:
    juce::Colour accent = ParagonColours::eq;
    float xForFrequency (float frequency) const;
    float yForDb (float db) const;
    void flatten();
    void applyPreset (int index);
    void showMatchingPreset();   // names the preset the faders are set to, if any

    juce::AudioProcessorValueTreeState& parameters;
    std::array<juce::Slider, effects::GraphicEq::numBands> faders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    juce::Slider level { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAttachment;
    juce::ToggleButton onButton { "ON" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment;
    juce::ComboBox presets;
    juce::TextButton flat { "FLAT" };
    juce::Rectangle<float> graph, faderTrack;
    bool rack = false;
    juce::Rectangle<int> onLabel;                        // on the rack: ON's name, beside its key
    mutable std::vector<juce::Rectangle<float>> glasses;   // on the rack: the screens' glass, for their overlay
};
