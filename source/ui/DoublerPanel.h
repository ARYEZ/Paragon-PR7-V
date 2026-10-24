#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/effects/Doubler.h"

// DOUBLER's knobs, shown when its block is picked: laid out after the lower
// half of Waves Doubler (the owner's reference), to fit the dock.
//
//   left   a row per voice: VOICE (on), GAIN, PAN, DELAY, FDBK, OCT, DETUNE,
//          DEPTH, RATE, DRIFT, as number boxes: drag up or down, double-click to reset
//   right  DIRECT (on, gain, pan) and OUTPUT; the LOW and HIGH shelves on the
//          voices (on, gain, frequency); the feedback's high-pass; MOD RESET
class DoublerPanel final : public juce::Component
{
public:
    DoublerPanel (juce::AudioProcessorValueTreeState&, std::function<void()> resetModulation);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Box
    {
        juce::Slider slider { juce::Slider::LinearBarVertical, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Switch
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    Box& makeBox (const juce::String& id, const juce::String& tooltip = {});
    Switch& makeSwitch (const juce::String& id, const juce::String& tooltip = {});

    juce::AudioProcessorValueTreeState& parameters;
    std::vector<std::unique_ptr<Box>> boxes;
    std::vector<std::unique_ptr<Switch>> switches;

    // The voice table: [voice][column], columns as in the header.
    static constexpr int numColumns = 10;
    std::array<std::array<juce::Component*, numColumns>, effects::Doubler::numVoices> table {};

    juce::Component *directOn = nullptr, *directGain = nullptr, *directPan = nullptr, *output = nullptr;
    juce::Component *lowOn = nullptr, *lowGain = nullptr, *lowFreq = nullptr;
    juce::Component *highOn = nullptr, *highGain = nullptr, *highFreq = nullptr, *feedbackHighPass = nullptr;
    juce::TextButton modReset { "MOD RESET" };

    // Where the labels go, worked out in resized() and drawn in paint().
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
};
