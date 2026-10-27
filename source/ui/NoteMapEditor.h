#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/effects/Harmony.h"

// HARMONY's Note Map Editor (stage 5), inspired by Waves Harmony's: CHORD
// Custom's map and SCALE Custom's boxes, on a keyboard of three octaves
// (C3 to B5). Opened by EDIT on the NOTES tab, over the graph.
//
//   PLAYS   a box per note (C to B): click one to edit what that note plays
//           (yellow on the keyboard); a dot under a box means it plays
//           something.
//   SCALE   a box per note: In, > (up to the next note in the scale) or <
//           (down). Editable with SCALE set to Custom; otherwise it shows the
//           SCALE chosen.
//   keys    click to add or remove a note the picked one plays (purple dot);
//           a blue ring shows what sounds once the scale is applied; keys
//           held on a MIDI keyboard are pink.
//   SPREAD / FOLD, FROM CHORD (start from a built-in map), ERASE, DONE.
class NoteMapEditor final : public juce::Component,
                            private juce::Timer
{
public:
    NoteMapEditor (juce::AudioProcessorValueTreeState&, std::function<bool (int)> isKeyHeld);
    ~NoteMapEditor() override;

    std::function<void()> onDone;

    static constexpr int lowest = 48, highest = 83;   // C3 to B5
    static constexpr int reference = effects::Harmony::foldOctave;   // the octave edited: C4 to B4

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Public for tests and the panel.
    void selectNote (int noteClass);
    int getSelectedNote() const noexcept { return selected; }
    void toggleKey (int midiNote);                     // add or remove it from what the picked note plays
    void cycleScaleBox (int noteClass);                // In → Up → Down (SCALE Custom only)
    void fillFromChord (int chord);                    // the 12 notes from a built-in map
    void erase();
    std::uint64_t mapOf (int noteClass) const;
    void setMap (int noteClass, std::uint64_t bits);

private:
    void timerCallback() override;
    float value (const juce::String& id) const;
    void set (const juce::String& id, float plainValue);
    int key() const;
    Scales::Scale scale() const;
    Scales::Custom customScale() const;
    juce::Rectangle<float> keyBounds (int midiNote) const;
    juce::Rectangle<float> boxBounds (int row, int noteClass) const;
    int keyAt (juce::Point<float>) const;

    juce::AudioProcessorValueTreeState& parameters;
    std::function<bool (int)> keyHeld;
    int selected = 0;
    juce::TextButton spread { "SPREAD" }, fold { "FOLD" }, fromChord { "FROM CHORD" }, eraseButton { "ERASE" }, done { "DONE" };
    juce::Rectangle<float> keyboard, rows;
    std::array<bool, 128> shownHeld {};
};
