#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Icons.h"

// The SIGNAL CHAIN strip along the bottom of the mockup:
//   guitar -> COMP/GATE -> OD/DIST -> NAMP -> CABINET IR -> MODULATION
//          -> DELAY -> REVERB -> output
// Each block glows in its colour (set by its place in the chain) while it's switched on. Clicking a block
// selects it, showing its knobs in the edit panel; double-clicking switches
// it on or off.
class SignalChain final : public juce::Component,
                          private juce::Timer
{
public:
    struct Block
    {
        juce::String name;
        Icons::Type icon;
        juce::Colour colour;
        juce::String onParameterId;   // its on switch; several separated by commas (on if any is)
    };

    SignalChain (juce::AudioProcessorValueTreeState& parameters, std::vector<Block> blocks);

    std::function<void (int blockIndex)> onSelect;
    void setSelected (int blockIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    int blockAt (juce::Point<int> position) const;
    bool isOn (size_t index) const;

    juce::AudioProcessorValueTreeState& parameters;
    std::vector<Block> blocks;
    std::vector<juce::Rectangle<float>> blockAreas;
    std::vector<bool> lastOn;
    juce::Rectangle<float> micArea, outputArea;
    int selected = 0;
};
