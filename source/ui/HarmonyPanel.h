#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/effects/Harmony.h"

// HARMONY's knobs, shown when the PITCH block is picked: laid out after
// Waves Harmony's (the owner's reference), to fit the dock.
//
//   left    HARMONY on, KEY, SCALE, and the note it hears being sung
//   middle  the graph: each voice a dot labelled with its interval, left to
//           right its pan, bottom to top its level. Click an empty spot to
//           add a voice there, drag one to move it, scroll over one to
//           change its interval, right-click to remove it.
//   right   voice tabs 1-8; the picked voice's ON, CORRECT, PITCH, FINE and
//           FORMANT; then the mix: SOURCE, HARMONY, SPREAD, CORRECT FORMANT.
//           The NOTES tab (stage 3) shows GENERATE NOTES, the CHORD map, MIN
//           NOTE, TOLERANCE and TIME, and the note voices' LEVEL, WIDTH,
//           FORMANT, ATTACK and RELEASE. Note voices show on the graph in
//           blue, labelled with their notes, while they sound.
class HarmonyPanel final : public juce::Component,
                           private juce::Timer
{
public:
    // `sungNote` returns the note being sung (a MIDI number), or -1.
    HarmonyPanel (juce::AudioProcessorValueTreeState&, std::function<float()> sungNote, std::function<float()> correctNote = {},
                  const effects::Harmony* harmony = nullptr);
    ~HarmonyPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void selectVoice (int voice);
    int getSelectedVoice() const noexcept { return selected; }
    void showNotes (bool show);
    // Which MIDI keys are held, for the NOTES graph's keyboard strip.
    void setMidiKeys (std::function<bool (int)> isKeyHeld) { midiKeyHeld = std::move (isKeyHeld); }
    bool isShowingNotes() const noexcept { return notesShown; }

    // Where a voice with this pan and level sits on the graph, and back. Public for tests.
    juce::Point<float> pointFor (float pan, float levelDb) const;
    std::pair<float, float> panAndLevelAt (juce::Point<float>) const;

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Switch
    {
        juce::Label label;
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void timerCallback() override;
    void bindVoiceControls();
    void setUp (Knob&, const juce::String& label);
    void setUp (Switch&, const juce::String& label);
    float value (const juce::String& id) const;
    void set (const juce::String& id, float plainValue);
    void beginGesture (const juce::String& id);
    void endGesture (const juce::String& id);
    juce::String voiceId (int voice, const char* part) const;
    bool isActive (int voice) const;
    int voiceAt (juce::Point<float>) const;
    void addVoiceAt (int voice, float pan, float levelDb);
    void removeVoice (int voice);
    void refreshTabs();

    juce::AudioProcessorValueTreeState& parameters;
    std::function<float()> sungNote;
    int selected = 0, dragging = -1;

    Switch harmonyOn, voiceOn, voiceCorrect;
    juce::Label keyLabel, scaleLabel;
    juce::ComboBox key, scale;
    juce::Label correctLabel;
    juce::ComboBox correct;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> correctAttachment;
    std::function<float()> correctNote;   // the note CORRECT is tuning to, or -1
    const effects::Harmony* harmonyEngine = nullptr;
    bool notesShown = false;
    juce::TextButton notesTab { "NOTES" };
    Switch generate;
    std::function<bool (int)> midiKeyHeld;
    juce::Label midiLabel;
    juce::ComboBox midiMap;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiMapAttachment;
    juce::Rectangle<float> keyboardArea() const;
    juce::Label chordLabel;
    juce::ComboBox chord;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chordAttachment;
    Knob minNote, tolerance, toleranceTime, noteLevel, noteWidth, noteFormant, noteAttack, noteRelease;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;
    Knob pitch, fine, formant, source, harmony, spread, correctFormant;
    std::array<juce::TextButton, effects::Harmony::numVoices> tabs;

    juce::Rectangle<float> graph, sungArea, readoutArea;
    juce::String shownSung, shownTuned;
};
