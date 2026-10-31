#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"

// The panels along the bottom of the PROCESSOR page:
//
//   FACEPLATE (Faceplate.h) | (the selected block's knobs) | EXPRESSION ASSIGN
//
// their shared look, and the SETTINGS page.

// Dark rounded panel with a small grey title, shared by the dock panels.
namespace DockStyle
{
    void drawPanel (juce::Graphics&, juce::Rectangle<float> area);
    void drawTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
}

//==============================================================================
// SETTINGS: tuner reference pitch and mute, the expression pedal's MIDI
// controller, the unit's finish, where rigs are kept, and what the plugin
// is built from.
class SettingsPage final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit SettingsPage (ParagonX1AudioProcessor&);
    ~SettingsPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    ParagonX1AudioProcessor& processor;
    juce::Slider reference { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton muteWhileTuning { "Mute the output while the tuner is on" };
    juce::AudioProcessorValueTreeState::ButtonAttachment muteAttachment;
    juce::ComboBox ccBox;
    juce::TextButton openFolder { "OPEN RIGS FOLDER" };
    juce::TextButton pearlWhite { "PEARL WHITE" }, pearlWhiteNeon { "WHITE + NEON" }, pearlBlack { "PEARL BLACK" };
    juce::Rectangle<int> tunerArea, expressionArea, finishArea, rigsArea, aboutArea;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { showFinish(); }
    void showFinish();
};
