#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/ParagonLookAndFeel.h"

#include "dsp/effects/Harmony.h"
#include "ui/HarmonyModsView.h"
#include "ui/HarmonySnapshotsView.h"
#include "ui/MarsModUi.h"
#include "ui/NoteMapEditor.h"

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

    // The colour of the header icon, the graph, the fixed voices and the note sung: the PITCH block's colour in the chain. (Note voices stay blue.)
    void setAccent (juce::Colour colour);

    // On the rack unit's face (see Faceplate), at full size. The panel then
    // covers the rack's display too (`displayArea`, in this component): KEY,
    // SCALE and the note sung on the left of it, the graph on a screen beside
    // them, played with the mouse as before. Along the top (after the view
    // keys the main view places), HARMONY's key, GLOBAL and TUNE VOICE; then
    // the voice keys 1-8, NOTES and FX; then the view's controls in one row
    // (the unit's big knobs, lists on screens, switches as keys). The Note
    // Map and SNAPSHOTS open on one wide screen across the display and the controls.
    void setRack (bool inRack, juce::Rectangle<int> displayArea);
    bool isRack() const noexcept { return rack; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
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
    // FX: the picked voice's (or, on NOTES, the note voices') DELAY,
    // FEEDBACK and FILTER, in place of the two rows of controls.
    void showFx (bool show);
    bool isShowingFx() const noexcept { return fxShown; }
    // GLOBAL: GLIDE, its SYNC, PITCH and FORMANT for all the harmony
    // voices, over the mix (SOURCE, HARMONY, SPREAD, CORR. FORMANT).
    void showGlobal (bool show);
    bool isShowingGlobal() const noexcept { return globalShown; }
    // SNAPSHOTS (stage 8): the page over the graph, opened by the button on
    // the graph. Needs the processor's snapshot functions (set by MainView).
    void setSnapshotActions (HarmonySnapshotsView::Actions);
    void openSnapshots();
    void closeSnapshots();
    bool isShowingSnapshots() const noexcept { return snapshots.isVisible(); }
    HarmonySnapshotsView& getSnapshots() noexcept { return snapshots; }
    // MODS (stage 9): the modulators' page, opened by MODS; and putting a
    // modulator on a knob (right-click it). Needs the processor's slots and
    // live values (set by MainView).
    struct ModActions
    {
        std::function<Rig::HarmonyModSlot (int target, int slot)> get;
        std::function<void (int target, int slot, Rig::HarmonyModSlot)> set;
        std::function<float (int target)> offset;   // a control's movement now, a fraction of its range
        std::function<float (int source)> value;
        std::function<float (int mod)> position;
        // Stage 10: a source's value for each note voice, the note voices' notes, the drawn shapes.
        std::function<float (int source, int voice)> voiceValue;
        std::function<int (int voice)> voiceNote;
        std::function<std::array<float, 32> (int mod)> shape;
        std::function<void (int mod, const std::array<float, 32>&)> setShape;
    };
    void setModActions (ModActions);
    void openMods (int tab = -1);
    void closeMods();
    bool isShowingMods() const noexcept { return mods.isVisible(); }
    HarmonyModsView& getMods() noexcept { return mods; }
    // The modulation slots of the control a knob is showing now (a voice's
    // knobs follow the picked voice), as the menu a right-click opens: what
    // it shows, and what choosing an item does. Public for tests.
    int targetOfKnobAt (juce::Point<int>) const;   // -1: no modulatable knob there
    void addModulator (int target, int source);     // into the first free slot, at a starting depth
    juce::PopupMenu modulationMenu (int target);
    void chooseFromModulationMenu (int target, int item);

    // Re-reads the sung note, the voices and GLOBAL's state now (the timer
    // does it; screenshots call it directly).
    void updateReadings() { timerCallback(); }
    // The Note Map Editor (EDIT on the NOTES tab): CHORD becomes Custom; a
    // built-in map is copied in first if the Custom map is empty.
    void openNoteMap();
    void closeNoteMap();
    bool isShowingNoteMap() const noexcept { return noteMap.isVisible(); }
    NoteMapEditor& getNoteMap() noexcept { return noteMap; }

    // Where a voice with this pan and level sits on the graph, and back. Public for tests.
    juce::Point<float> pointFor (float pan, float levelDb) const;
    std::pair<float, float> panAndLevelAt (juce::Point<float>) const;

private:
    juce::Colour accent = ParagonColours::pitch;
    struct Knob
    {
        juce::Label label;
        MarsModUi::ModSlider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };   // right-click: modulate
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
    void bindFxControls();
    void updateVisibility();
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
    juce::TextButton editMap { "EDIT" };
    NoteMapEditor noteMap;
    juce::TextButton snapshotsButton { "SNAPSHOTS" };
    juce::TextButton modsButton { "MODS" };
    HarmonyModsView mods;
    ModActions modActions;
    std::vector<Knob*> allKnobs();
    juce::String targetIdOf (const Knob&) const;   // the control a knob shows now
    int targetOf (const Knob&) const;
    void showModulationMenu (int target, juce::Component& knob);
    bool anyModulated = false;
    // The graph redraws only when what it draws changes (see graphSignature).
    juce::uint64 graphSignature() const;
    std::vector<std::atomic<float>*> allValues;
    juce::uint64 shownGraph = 0;
    HarmonySnapshotsView snapshots;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;
    Knob pitch, fine, formant, source, harmony, spread, correctFormant;
    // Stage 6: FLATTEN (the PITCH knob becomes NOTE while it's on), and the FX view.
    Switch voiceFlatten;
    Knob flattenNote;
    bool shownFlatten = false;
    juce::TextButton fxTab { "FX" };
    bool fxShown = false;
    Knob echoDelay, echoFeedback, cutoff;
    juce::Label syncLabel, filterLabel;
    juce::ComboBox echoSync, filterType;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> echoSyncAttachment, filterTypeAttachment;
    juce::String fxId (const char* voicePart, const char* noteId) const;   // the picked voice's part, or the note voices' parameter
    // Stage 7: the GLOBAL view.
    juce::TextButton globalTab { "GLOBAL" };
    bool globalShown = false;
    Knob glide, globalPitch, globalFormant;
    juce::Label glideSyncLabel;
    juce::ComboBox glideSync;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> glideSyncAttachment;
    std::array<juce::TextButton, effects::Harmony::numVoices> tabs;

    juce::Rectangle<float> graph, sungArea, readoutArea;
    bool rack = false;
    juce::Rectangle<int> rackDisplay;          // on the rack: where the rack's display is
    juce::Rectangle<float> graphFrame, wideScreen, sungFrame;   // on the rack: the screens drawn
    juce::Colour inkColour;
    mutable std::vector<juce::Rectangle<float>> glasses;
    void layOutRack();
    void styleForRack();
    void rackOverlayChanged();
    juce::String shownSung, shownTuned;
};
