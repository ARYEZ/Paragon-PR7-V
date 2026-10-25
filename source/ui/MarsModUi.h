#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/effects/Mars.h"

// What MARS's panel pages share: their controls, and showing and setting up
// the modulation (MARS stage 2) on any knob.
namespace MarsModUi
{
    using Mod = effects::MarsModulation;
    using Parameters = juce::AudioProcessorValueTreeState;

    // Each kind of source has its colour, used wherever it modulates
    // something (Saturn 2 does the same). A knob moved by more than one kind: yellow.
    juce::Colour sourceColour (int source);
    inline const juce::Colour mixedColour { 0xffffe14d };
    juce::String sourceShortName (int source);   // "XLFO 1", "EG 2", "XY 1 X", "Pedal"

    // A knob that can be modulated: right-click it to pick a source.
    class ModSlider final : public juce::Slider
    {
    public:
        using juce::Slider::Slider;
        int target = 0;   // what it is as a modulation target (0: not one)
        std::function<void (int target)> onModulateMenu;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        bool menuShown = false;
    };

    struct Knob
    {
        juce::Label label;
        ModSlider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };
    struct Switch
    {
        juce::Label label;
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void styleLabel (juce::Label&, const juce::String& text);
    void setUp (juce::Component& parent, Knob&, const juce::String& label, const juce::String& tooltip);
    void setUp (juce::Component& parent, Choice&, const juce::String& label, const juce::StringArray& items, const juce::String& tooltip);
    void setUp (juce::Component& parent, Switch&, const juce::String& label, const juce::String& tooltip);
    void attach (Parameters&, Knob&, const juce::String& id, int target = 0);
    void attach (Parameters&, Choice&, const juce::String& id);
    void attach (Parameters&, Switch&, const juce::String& id);
    void show (Knob&, bool);
    void show (Choice&, bool);
    void show (Switch&, bool);
    // A label above its control, in a cell.
    void place (juce::Component& label, juce::Component& widget, juce::Rectangle<int> cell, int widgetHeight);

    float value (Parameters&, const juce::String& id);
    void set (Parameters&, const juce::String& id, float plainValue);

    // The slots. A slot is in use when it has a source and a target.
    struct SlotOnTarget { int slot, source; float amount; };
    std::vector<SlotOnTarget> slotsOn (Parameters&, int target);
    int addSlot (Parameters&, int source, int target, float amount);   // the slot used, or -1 if all 16 are busy
    void clearSlot (Parameters&, int slot);
    // The colour of what modulates a target (transparent: nothing does).
    juce::Colour targetColour (Parameters&, int target);

    // The right-click menu on a modulatable knob: modulate it with a source,
    // remove a slot on it, or go to the slots.
    void showModulateMenu (Parameters&, int target, juce::Component& knob, std::function<void()> showSlots);

    // Draws a modulated knob's extra ring: from its setting to where the
    // modulation has it now (`offset`: a fraction of its range), with a dot.
    void paintModulation (juce::Graphics&, juce::Component& parent, const ModSlider&, float offset, juce::Colour);
}
