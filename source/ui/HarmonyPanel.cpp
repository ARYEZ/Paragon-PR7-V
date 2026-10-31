#include <cstring>
#include "HarmonyPanel.h"

#include "Icons.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "dsp/PitchDetector.h"
#include "ui/Finish.h"
#include "ui/LedDisplay.h"
#include "dsp/effects/Delay.h"

namespace
{
    // The note voices' colour, as Waves Harmony's note harmonies are blue.
    const juce::Colour noteBlue { 0xff4fa3ff };
    constexpr float dotRadius = 12.0f;
    const auto maxGain = juce::Decibels::decibelsToGain (6.0f);

    // The graph's height is the square root of the gain, so the useful
    // levels (0 to -12 dB) get most of it, as on Waves Harmony's graph.
    float heightForDb (float db)
    {
        const auto gain = db <= effects::Harmony::silentDb ? 0.0f : juce::Decibels::decibelsToGain (db);
        return std::sqrt (gain / maxGain);
    }

    float dbForHeight (float height)
    {
        const auto gain = juce::jlimit (0.0f, 1.0f, height) * juce::jlimit (0.0f, 1.0f, height) * maxGain;
        return juce::jlimit (effects::Harmony::silentDb, 6.0f, juce::Decibels::gainToDecibels (gain, effects::Harmony::silentDb));
    }

    void styleLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setMinimumHorizontalScale (0.6f);
        label.setBorderSize ({});
        label.setInterceptsMouseClicks (false, false);
    }

    juce::String intervalText (float semitones)
    {
        const auto value = juce::roundToInt (semitones);
        return value == 0 ? juce::String ("U") : (value > 0 ? "+" : "") + juce::String (value);
    }
}

HarmonyPanel::HarmonyPanel (juce::AudioProcessorValueTreeState& state, std::function<float()> sungNoteSource,
                            std::function<float()> correctNoteSource, const effects::Harmony* harmonyToShow)
    : parameters (state), sungNote (std::move (sungNoteSource)),
      noteMap (state, [this] (int midiNote) { return midiKeyHeld != nullptr && midiKeyHeld (midiNote); }),
      mods (state)
{
    correctNote = std::move (correctNoteSource);
    harmonyEngine = harmonyToShow;
    setUp (harmonyOn, "HARMONY");
    harmonyOn.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, ParamIDs::harmonyOn, harmonyOn.button);
    setUp (voiceOn, "ON");
    setUp (voiceCorrect, "CORRECT");
    voiceCorrect.button.setTooltip ("Keep this voice in the key and scale");
    setUp (voiceFlatten, "FLATTEN");
    voiceFlatten.button.setTooltip ("Hold this voice on one NOTE, whatever you sing (CORRECT still keeps it in the key)");
    setUp (flattenNote, "NOTE");

    for (auto [label, box, id, text] : { std::tuple { &keyLabel, &key, ParamIDs::harmonyKey, "KEY" },
                                         std::tuple { &scaleLabel, &scale, ParamIDs::harmonyScale, "SCALE" },
                                         std::tuple { &correctLabel, &correct, ParamIDs::harmonyCorrect, "TUNE VOICE" } })
    {
        styleLabel (*label, text);
        addAndMakeVisible (*label);
        box->addItemList (dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (id))->choices, 1);
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
        box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box->setColour (juce::ComboBox::textColourId, ParagonColours::accent);
        box->setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
        addAndMakeVisible (*box);
    }
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyKey, key);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyScale, scale);
    key.setTooltip ("The song's key: harmony voices are kept in it");
    correctAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyCorrect, correct);
    correct.setTooltip ("Tune your own voice to the nearest note in the KEY and SCALE. Natural glides to it; Hard jumps (the robotic sound). "
                        "Works with HARMONY off too. While it's on, the plugin runs 40 ms late (your DAW lines it back up)");

    setUp (pitch, "PITCH");
    setUp (fine, "FINE");
    setUp (formant, "FORMANT");
    setUp (source, "SOURCE");
    setUp (harmony, "HARMONY");
    setUp (spread, "SPREAD");
    setUp (correctFormant, "CORR. FORMANT");
    for (auto [knob, id] : { std::pair { &source, ParamIDs::harmonySource }, std::pair { &harmony, ParamIDs::harmonyLevel },
                             std::pair { &spread, ParamIDs::harmonySpread }, std::pair { &correctFormant, ParamIDs::harmonyCorrectFormant } })
    {
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        auto* parameter = parameters.getParameter (id);
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    source.slider.setTooltip ("Your own voice's level");
    harmony.slider.setTooltip ("All the harmony voices' level");
    spread.slider.setTooltip ("How wide the voices spread: 0% puts them all in the middle");
    correctFormant.slider.setTooltip ("100%: raised or lowered voices keep your vowels, like another singer. 0%: they move with the pitch, like a sped-up tape");

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto& tab = tabs[i];
        tab.setButtonText (juce::String ((int) i + 1));
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (4210);
        tab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        tab.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);   // the picked voice, in orange as the plugin's other tabs
        tab.setTooltip ("Voice " + juce::String ((int) i + 1) + " (click an empty one to add it, in the middle)");
        tab.onClick = [this, voice = (int) i]
        {
            if (! isActive (voice))
                addVoiceAt (voice, 0.0f, 0.0f);
            showGlobal (false);
            selectVoice (voice);
            showNotes (false);
        };
        addAndMakeVisible (tab);
    }

    // NOTES: GENERATE NOTES, the CHORD map and the note voices.
    notesTab.setClickingTogglesState (true);
    notesTab.setRadioGroupId (4210);
    notesTab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    notesTab.setColour (juce::TextButton::textColourOnId, noteBlue);
    notesTab.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    notesTab.setTooltip ("GENERATE NOTES: harmonies from chord maps, made from the notes you sing");
    notesTab.onClick = [this] { showGlobal (false); showNotes (true); };
    addAndMakeVisible (notesTab);
    setUp (generate, "GENERATE");
    generate.button.setTooltip ("Turn your singing into notes, and each note into the CHORD map's harmonies");
    generate.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, ParamIDs::harmonyGenerate, generate.button);
    styleLabel (chordLabel, "CHORD");
    addAndMakeVisible (chordLabel);
    chord.addItemList (effects::Harmony::chordMapNames(), 1);
    chord.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    chord.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    chord.setColour (juce::ComboBox::textColourId, noteBlue);
    chord.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    chord.setTooltip ("What each note becomes: the same intervals on every note, or a chord from it (kept in the KEY and SCALE)");
    addAndMakeVisible (chord);
    chordAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyChord, chord);
    styleLabel (midiLabel, "MIDI");
    addAndMakeVisible (midiLabel);
    midiMap.addItemList (effects::Harmony::midiMapNames, 1);
    midiMap.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    midiMap.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    midiMap.setColour (juce::ComboBox::textColourId, noteBlue);
    midiMap.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    midiMap.setTooltip ("Keys played on a MIDI keyboard (or a MIDI track sent to PR7-V) become note voices: As Played, exactly the keys; "
                        "Through Chord, each key through the CHORD map, in the key");
    addAndMakeVisible (midiMap);
    midiMapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyMidiMap, midiMap);
    const std::tuple<Knob*, const char*, const char*, const char*> noteKnobs[] = {
        { &minNote, "MIN NOTE", ParamIDs::harmonyMinNote, "The lowest note listened for: lower sounds don't make notes" },
        { &tolerance, "TOLERANCE", ParamIDs::harmonyTolerance, "How far (cents) your pitch must move for a new note. Off: only a silence starts one" },
        { &toleranceTime, "TIME", ParamIDs::harmonyToleranceTime, "How long your pitch must stay moved for a new note: longer ignores slides and embellishments" },
        { &noteLevel, "LEVEL", ParamIDs::harmonyNoteLevel, "The note voices' level" },
        { &noteWidth, "WIDTH", ParamIDs::harmonyNoteWidth, "How far apart the note voices are panned, low notes left" },
        { &noteFormant, "FORMANT", ParamIDs::harmonyNoteFormant, "The note voices' character: lower for a bigger singer, higher for a smaller one" },
        { &noteAttack, "ATTACK", ParamIDs::harmonyNoteAttack, "How quickly the note voices fade in" },
        { &noteRelease, "RELEASE", ParamIDs::harmonyNoteRelease, "How quickly they fade out" } };
    for (const auto& [knob, label, id, tip] : noteKnobs)
    {
        setUp (*knob, label);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        auto* parameter = parameters.getParameter (id);
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        knob->slider.setTooltip (tip);
    }

    // FX: DELAY, SYNC, FEEDBACK, FILTER and CUTOFF, per voice or for the note voices.
    fxTab.setClickingTogglesState (true);
    fxTab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    fxTab.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    fxTab.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    fxTab.setTooltip ("The picked voice's DELAY, FEEDBACK and FILTER (on NOTES: the note voices')");
    fxTab.onClick = [this] { showFx (fxTab.getToggleState()); };

    // GLOBAL: how the harmony voices glide, and an offset for all of them.
    globalTab.setClickingTogglesState (true);
    globalTab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    globalTab.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    globalTab.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    globalTab.setTooltip ("GLIDE, PITCH and FORMANT for all the harmony voices, with the mix (* when a global setting is changed)");
    globalTab.onClick = [this] { showGlobal (globalTab.getToggleState()); };
    addAndMakeVisible (globalTab);
    const std::tuple<Knob*, const char*, const char*, const char*> globalKnobs[] = {
        { &glide, "GLIDE", ParamIDs::harmonyGlide, "How long the voices take to move to a new note: 1 ms jumps straight there (greyed while SYNC sets it)" },
        { &globalPitch, "PITCH", ParamIDs::harmonyGlobalPitch, "Moves every harmony voice up or down (your own voice stays put)" },
        { &globalFormant, "FORMANT", ParamIDs::harmonyGlobalFormant, "The character of every harmony voice: lower for bigger singers, higher for smaller" } };
    for (const auto& [knob, label, id, tip] : globalKnobs)
    {
        setUp (*knob, label);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        auto* parameter = parameters.getParameter (id);
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        knob->slider.setTooltip (tip);
    }
    styleLabel (glideSyncLabel, "SYNC");
    addAndMakeVisible (glideSyncLabel);
    glideSync.addItemList (effects::Delay::syncNames, 1);
    glideSync.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    glideSync.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    glideSync.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    glideSync.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    glideSync.setTooltip ("GLIDE as a note length at the song's tempo (Off: in ms)");
    addAndMakeVisible (glideSync);
    glideSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyGlideSync, glideSync);
    addAndMakeVisible (fxTab);
    setUp (echoDelay, "DELAY");
    setUp (echoFeedback, "FEEDBACK");
    setUp (cutoff, "CUTOFF");
    echoDelay.slider.setTooltip ("How late the voice comes in (Off: with you). Greyed while SYNC sets it");
    echoFeedback.slider.setTooltip ("Repeats of the delayed voice: higher, more of them");
    cutoff.slider.setTooltip ("Where the FILTER cuts");
    for (auto [label, box, text, choices] : { std::tuple { &syncLabel, &echoSync, "SYNC", effects::Delay::syncNames },
                                              std::tuple { &filterLabel, &filterType, "FILTER", effects::Harmony::filterNames } })
    {
        styleLabel (*label, text);
        addAndMakeVisible (*label);
        box->addItemList (choices, 1);
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
        box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box->setColour (juce::ComboBox::textColourId, ParagonColours::accent);
        box->setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
        addAndMakeVisible (*box);
    }
    echoSync.setTooltip ("DELAY as a note length at the song's tempo (Off: in ms)");
    filterType.setTooltip ("Low-pass keeps the lows (20 kHz: off), high-pass the highs (20 Hz: off), band-pass a band around CUTOFF");
    filterType.onChange = [this]
    {
        // A switch that would silence the voice (high-pass at the top, or
        // low-pass at the bottom) starts CUTOFF somewhere it can be heard.
        const auto id = fxId ("Cutoff", ParamIDs::harmonyNoteCutoff);
        const auto hz = value (id);
        const auto type = filterType.getSelectedItemIndex();
        auto wanted = hz;
        if (type == 0 && hz <= 21.0f)
            wanted = effects::Harmony::filterHighestHz;
        else if (type == 1 && hz >= 19000.0f)
            wanted = effects::Harmony::filterLowestHz;
        else if (type == 2 && (hz <= 21.0f || hz >= 19000.0f))
            wanted = 1000.0f;
        if (! juce::approximatelyEqual (wanted, hz))
        {
            beginGesture (id);
            set (id, wanted);
            endGesture (id);
        }
    };

    editMap.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    editMap.setColour (juce::TextButton::textColourOffId, noteBlue);
    editMap.setTooltip ("The Note Map Editor: your own map of what each note plays (CHORD Custom), and a Custom scale");
    editMap.onClick = [this] { openNoteMap(); };
    addAndMakeVisible (editMap);
    noteMap.onDone = [this] { closeNoteMap(); };
    addChildComponent (noteMap);   // last: on top of everything when open
    snapshotsButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217).withAlpha (0.85f));
    snapshotsButton.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    snapshotsButton.setTooltip ("SNAPSHOTS: store the voices in up to 32 slots and switch between them (the SNAPSHOT parameter switches them from your DAW)");
    snapshotsButton.onClick = [this] { openSnapshots(); };
    snapshotsButton.setEnabled (false);   // until the processor's snapshots are connected
    addAndMakeVisible (snapshotsButton);
    snapshots.onDone = [this] { closeSnapshots(); };
    addChildComponent (snapshots);
    modsButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    modsButton.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    modsButton.setTooltip ("MODS: the modulators (M1-M4, AM, PT). Right-click a knob to put one on it");
    modsButton.onClick = [this] { openMods(); };
    modsButton.setEnabled (false);   // until the processor's modulators are connected
    addAndMakeVisible (modsButton);
    mods.onDone = [this] { closeMods(); };
    addChildComponent (mods);
    for (auto* knob : allKnobs())
        knob->slider.onModulateMenu = [this, knob] (int target) { showModulationMenu (target - 1, knob->slider); };

    for (auto* parameter : parameters.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            allValues.push_back (parameters.getRawParameterValue (ranged->getParameterID()));

    selectVoice (0);
    showNotes (false);
    refreshTabs();
    shownSung = "--";
    startTimerHz (20);
}

HarmonyPanel::~HarmonyPanel() = default;

void HarmonyPanel::setUp (Knob& knob, const juce::String& labelText)
{
    styleLabel (knob.label, labelText);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (knob.label);
    addAndMakeVisible (knob.slider);
}

void HarmonyPanel::setUp (Switch& control, const juce::String& labelText)
{
    styleLabel (control.label, labelText);
    addAndMakeVisible (control.label);
    addAndMakeVisible (control.button);
}

juce::String HarmonyPanel::voiceId (int voice, const char* part) const { return ParamIDs::harmonyVoice (voice, part); }

float HarmonyPanel::value (const juce::String& id) const { return parameters.getRawParameterValue (id)->load(); }

bool HarmonyPanel::isActive (int voice) const { return value (voiceId (voice, "Active")) > 0.5f; }

void HarmonyPanel::set (const juce::String& id, float plainValue)
{
    auto* parameter = parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

void HarmonyPanel::beginGesture (const juce::String& id) { parameters.getParameter (id)->beginChangeGesture(); }
void HarmonyPanel::endGesture (const juce::String& id) { parameters.getParameter (id)->endChangeGesture(); }

void HarmonyPanel::selectVoice (int voice)
{
    selected = juce::jlimit (0, (int) tabs.size() - 1, voice);
    tabs[(size_t) selected].setToggleState (true, juce::dontSendNotification);
    bindVoiceControls();
    repaint();
}

juce::Rectangle<float> HarmonyPanel::keyboardArea() const
{
    return graph.reduced (8.0f, 0.0f).removeFromBottom (14.0f).translated (0.0f, -4.0f);
}

void HarmonyPanel::setAccent (juce::Colour colour)
{
    accent = colour;
    repaint();
}

void HarmonyPanel::openNoteMap()
{
    const auto customChord = (float) effects::Harmony::customChord;
    const auto chordNow = juce::roundToInt (value (ParamIDs::harmonyChord));
    if (chordNow != effects::Harmony::customChord)
    {
        auto empty = true;
        for (int note = 0; note < 12; ++note)
            empty = empty && noteMap.mapOf (note) == 0;
        if (empty)
            noteMap.fillFromChord (chordNow);   // start from what you were hearing
        beginGesture (ParamIDs::harmonyChord);
        set (ParamIDs::harmonyChord, customChord);
        endGesture (ParamIDs::harmonyChord);
    }
    noteMap.setVisible (true);
    noteMap.toFront (false);
    rackOverlayChanged();
}

void HarmonyPanel::setSnapshotActions (HarmonySnapshotsView::Actions actions)
{
    snapshots.setActions (std::move (actions));
    snapshotsButton.setEnabled (snapshots.hasActions());
}

void HarmonyPanel::openSnapshots()
{
    if (! snapshots.hasActions())
        return;
    noteMap.setVisible (false);
    snapshots.setVisible (true);
    snapshots.toFront (false);
    rackOverlayChanged();
}

std::vector<HarmonyPanel::Knob*> HarmonyPanel::allKnobs()
{
    return { &pitch, &fine, &formant, &source, &harmony, &spread, &correctFormant, &flattenNote,
             &minNote, &tolerance, &toleranceTime, &noteLevel, &noteWidth, &noteFormant, &noteAttack, &noteRelease,
             &echoDelay, &echoFeedback, &cutoff, &glide, &globalPitch, &globalFormant };
}

juce::String HarmonyPanel::targetIdOf (const Knob& knob) const
{
    if (&knob == &pitch) return voiceId (selected, "Pitch");
    if (&knob == &flattenNote) return voiceId (selected, "Note");
    if (&knob == &formant) return voiceId (selected, "Formant");
    if (&knob == &echoDelay) return fxId ("Delay", ParamIDs::harmonyNoteDelay);
    if (&knob == &echoFeedback) return fxId ("Feedback", ParamIDs::harmonyNoteFeedback);
    if (&knob == &cutoff) return fxId ("Cutoff", ParamIDs::harmonyNoteCutoff);
    if (&knob == &noteLevel) return ParamIDs::harmonyNoteLevel;
    if (&knob == &noteWidth) return ParamIDs::harmonyNoteWidth;
    if (&knob == &noteFormant) return ParamIDs::harmonyNoteFormant;
    if (&knob == &source) return ParamIDs::harmonySource;
    if (&knob == &harmony) return ParamIDs::harmonyLevel;
    if (&knob == &spread) return ParamIDs::harmonySpread;
    if (&knob == &correctFormant) return ParamIDs::harmonyCorrectFormant;
    if (&knob == &globalPitch) return ParamIDs::harmonyGlobalPitch;
    if (&knob == &globalFormant) return ParamIDs::harmonyGlobalFormant;
    return {};   // FINE, GLIDE, the note generator's: not modulated (FINE as in the guide)
}

int HarmonyPanel::targetOf (const Knob& knob) const
{
    static const auto ids = ParamIDs::harmonyModTargets();
    const auto id = targetIdOf (knob);
    return id.isEmpty() ? -1 : ids.indexOf (id);
}

int HarmonyPanel::targetOfKnobAt (juce::Point<int> point) const
{
    for (auto* knob : const_cast<HarmonyPanel*> (this)->allKnobs())
        if (knob->slider.isVisible() && knob->slider.getBounds().contains (point))
            return targetOf (*knob);
    return -1;
}

void HarmonyPanel::setModActions (ModActions actions)
{
    modActions = std::move (actions);
    mods.setActions ({ modActions.value, modActions.position, modActions.get, modActions.voiceValue, modActions.voiceNote,
                       modActions.shape, modActions.setShape });
    modsButton.setEnabled (modActions.get != nullptr);
    // Each knob that can be modulated answers a right-click (its target is
    // looked up when it's clicked, as a voice's knobs follow the picked voice).
    for (auto* knob : allKnobs())
        knob->slider.target = modActions.get != nullptr && targetIdOf (*knob).isNotEmpty() ? 1 : 0;
}

void HarmonyPanel::openMods (int tab)
{
    if (modActions.get == nullptr)
        return;
    noteMap.setVisible (false);
    snapshots.setVisible (false);
    mods.showTab (tab >= 0 ? tab : mods.getTab());   // (and reads what each modulator is on now)
    mods.setVisible (true);
    mods.toFront (false);
    rackOverlayChanged();
}

void HarmonyPanel::closeMods()
{
    mods.setVisible (false);
    rackOverlayChanged();
    repaint();
}

void HarmonyPanel::addModulator (int target, int sourceToAdd)
{
    if (modActions.get == nullptr || modActions.set == nullptr || target < 0)
        return;
    // A pitch at 1/24: a semitone either way (a vibrato); anything else a quarter of its range.
    static const auto ids = ParamIDs::harmonyModTargets();
    const auto depth = ParamIDs::isHarmonyPitchTarget (ids[target]) ? 1.0f / effects::HarmonyModulation::pitchSpan : 0.25f;
    for (int slot = 0; slot < effects::HarmonyModulation::slotsPerTarget; ++slot)
        if (modActions.get (target, slot).source == 0)
        {
            modActions.set (target, slot, { sourceToAdd, depth });
            repaint();
            return;
        }
}

namespace
{
    // The modulation menu's items: 1-15 add a source; then per slot (0-3) 100 + 20 x slot + n:
    // n 0 remove, 1-10 a depth from depths[].
    constexpr float menuDepths[] = { 1.0f, 0.5f, 0.25f, 0.1f, 1.0f / 24.0f, -1.0f / 24.0f, -0.1f, -0.25f, -0.5f, -1.0f };
    juce::String depthText (float depth, bool pitch)
    {
        if (pitch)
        {
            // Whole semitones without decimals (juce::String (x, 0) would print every digit).
            const auto st = depth * effects::HarmonyModulation::pitchSpan;
            return (depth >= 0.0f ? "+" : "") + (std::abs (st - std::round (st)) < 0.01f ? juce::String (juce::roundToInt (st)) : juce::String (st, 1)) + " st";
        }
        return (depth >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (depth * 100.0f)) + "%";
    }
}

juce::PopupMenu HarmonyPanel::modulationMenu (int target)
{
    juce::PopupMenu menu;
    if (modActions.get == nullptr || target < 0)
        return menu;
    static const auto ids = ParamIDs::harmonyModTargets();
    const auto pitchTarget = ParamIDs::isHarmonyPitchTarget (ids[target]);
    menu.addSectionHeader ("MODULATE " + HarmonyModsView::targetName (ids[target]));
    using Mod = effects::HarmonyModulation;
    juce::PopupMenu add;
    auto free = false;
    for (int slot = 0; slot < Mod::slotsPerTarget; ++slot)
        free = free || modActions.get (target, slot).source == 0;
    // The modulators, then MIDI's own in a group of their own.
    juce::PopupMenu midi;
    for (int source = 1; source < Mod::numSources; ++source)
        if (Mod::isBuilt (source))
        {
            auto used = false;
            for (int slot = 0; slot < Mod::slotsPerTarget; ++slot)
                used = used || modActions.get (target, slot).source == source;
            (source >= Mod::velocity ? midi : add).addItem (source, Mod::sourceNames[source], free && ! used);
        }
    add.addSubMenu ("MIDI", midi, free);
    menu.addSubMenu ("Add a modulator", add, free);
    for (int slot = 0; slot < Mod::slotsPerTarget; ++slot)
    {
        const auto found = modActions.get (target, slot);
        if (found.source == 0)
            continue;
        juce::PopupMenu slotMenu;
        for (size_t d = 0; d < std::size (menuDepths); ++d)
            slotMenu.addItem (100 + 20 * slot + 1 + (int) d, "Depth " + depthText (menuDepths[d], pitchTarget), true,
                              std::abs (found.depth - menuDepths[d]) < 1.0e-4f);
        slotMenu.addSeparator();
        slotMenu.addItem (100 + 20 * slot, "Remove");
        menu.addSubMenu (Mod::sourceNames[found.source] + "  " + depthText (found.depth, pitchTarget), slotMenu);
    }
    menu.addSeparator();
    menu.addItem (99, "Show the MODS page");
    return menu;
}

void HarmonyPanel::chooseFromModulationMenu (int target, int item)
{
    if (modActions.get == nullptr || modActions.set == nullptr || target < 0 || item <= 0)
        return;
    if (item < effects::HarmonyModulation::numSources)
        addModulator (target, item);
    else if (item == 99)
        openMods();
    else if (item >= 100)
    {
        const auto slot = (item - 100) / 20, choice = (item - 100) % 20;
        auto found = modActions.get (target, slot);
        if (choice == 0)
            found = {};
        else
            found.depth = menuDepths[(size_t) juce::jlimit (0, (int) std::size (menuDepths) - 1, choice - 1)];
        modActions.set (target, slot, found);
    }
    repaint();
}

void HarmonyPanel::showModulationMenu (int, juce::Component& knob)
{
    // Which control: the knob's own now (a voice's follow the picked voice).
    int target = -1;
    for (auto* k : allKnobs())
        if (&k->slider == &knob)
            target = targetOf (*k);
    if (target < 0)
        return;
    juce::Component::SafePointer<HarmonyPanel> safe (this);
    modulationMenu (target).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&knob), [safe, target] (int item)
    {
        if (safe != nullptr)
            safe->chooseFromModulationMenu (target, item);
    });
}

void HarmonyPanel::closeSnapshots()
{
    snapshots.setVisible (false);
    rackOverlayChanged();
    repaint();
}

void HarmonyPanel::closeNoteMap()
{
    noteMap.setVisible (false);
    rackOverlayChanged();
    repaint();
}

void HarmonyPanel::rackOverlayChanged()
{
    // On the rack the Note Map and SNAPSHOTS open on a screen across the
    // display and the controls: what's under it is hidden (its screen's
    // frame would otherwise show the edges of the keys and knobs below),
    // leaving the top row (HARMONY, GLOBAL, TUNE VOICE).
    if (! rack)
        return;
    const auto open = noteMap.isVisible() || snapshots.isVisible() || mods.isVisible();
    const std::initializer_list<juce::Component*> topRow { &noteMap, &snapshots, &mods, &harmonyOn.label, &harmonyOn.button, &globalTab,
                                                           &modsButton, &correct, &correctLabel };
    if (open)
    {
        for (auto* child : getChildren())
            if (std::find (topRow.begin(), topRow.end(), child) == topRow.end())
                child->setVisible (false);
    }
    else
    {
        for (auto* child : std::initializer_list<juce::Component*> { &key, &scale, &keyLabel, &scaleLabel, &snapshotsButton, &notesTab, &fxTab })
            child->setVisible (true);
        for (auto& tab : tabs)
            tab.setVisible (true);
        updateVisibility();
    }
    repaint();
}

void HarmonyPanel::showNotes (bool show)
{
    notesShown = show;
    if (show)
        notesTab.setToggleState (true, juce::dontSendNotification);
    else
        tabs[(size_t) selected].setToggleState (true, juce::dontSendNotification);
    if (! show)
        noteMap.setVisible (false);
    bindFxControls();
    updateVisibility();
}

void HarmonyPanel::showFx (bool show)
{
    fxShown = show;
    fxTab.setToggleState (show, juce::dontSendNotification);
    if (show)
    {
        globalShown = false;
        globalTab.setToggleState (false, juce::dontSendNotification);
    }
    updateVisibility();
}

void HarmonyPanel::showGlobal (bool show)
{
    globalShown = show;
    globalTab.setToggleState (show, juce::dontSendNotification);
    if (show)
    {
        fxShown = false;
        fxTab.setToggleState (false, juce::dontSendNotification);
    }
    updateVisibility();
}

void HarmonyPanel::updateVisibility()
{
    // Four views: the picked voice (with the mix), NOTES, FX (either's), or GLOBAL (with the mix).
    const auto fx = fxShown && ! globalShown;
    const auto voiceMain = ! notesShown && ! fx && ! globalShown, notesMain = notesShown && ! fx && ! globalShown;
    shownFlatten = value (voiceId (selected, "Flatten")) > 0.5f;
    const auto show = [] (auto& control, bool visible)
    {
        control.label.setVisible (visible);
        if constexpr (std::is_same_v<std::decay_t<decltype (control)>, Knob>)
            control.slider.setVisible (visible);
        else
            control.button.setVisible (visible);
    };
    for (auto* knob : { &fine, &formant })
        show (*knob, voiceMain);
    for (auto* knob : { &source, &harmony, &spread, &correctFormant })
        show (*knob, voiceMain || globalShown);
    for (auto* knob : { &glide, &globalPitch, &globalFormant })
        show (*knob, globalShown);
    glideSyncLabel.setVisible (globalShown);
    glideSync.setVisible (globalShown);
    show (pitch, voiceMain && ! shownFlatten);
    show (flattenNote, voiceMain && shownFlatten);
    for (auto* control : { &voiceOn, &voiceCorrect, &voiceFlatten })
        show (*control, voiceMain);
    for (auto* knob : { &minNote, &tolerance, &toleranceTime, &noteLevel, &noteWidth, &noteFormant, &noteAttack, &noteRelease })
        show (*knob, notesMain);
    show (generate, notesMain);
    for (auto* component : std::initializer_list<juce::Component*> { &chordLabel, &chord, &midiLabel, &midiMap, &editMap })
        component->setVisible (notesMain);
    for (auto* knob : { &echoDelay, &echoFeedback, &cutoff })
        show (*knob, fx);
    for (auto* component : std::initializer_list<juce::Component*> { &syncLabel, &echoSync, &filterLabel, &filterType })
        component->setVisible (fx);
    resized();
    repaint();
}

juce::String HarmonyPanel::fxId (const char* voicePart, const char* noteId) const
{
    return notesShown ? juce::String (noteId) : voiceId (selected, voicePart);
}

void HarmonyPanel::bindFxControls()
{
    // FX follows the picked voice, or the note voices on NOTES.
    for (auto [knob, part, noteId] : { std::tuple { &echoDelay, "Delay", ParamIDs::harmonyNoteDelay },
                                       std::tuple { &echoFeedback, "Feedback", ParamIDs::harmonyNoteFeedback },
                                       std::tuple { &cutoff, "Cutoff", ParamIDs::harmonyNoteCutoff } })
    {
        knob->attachment.reset();
        const auto id = fxId (part, noteId);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        auto* parameter = parameters.getParameter (id);
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    echoSyncAttachment.reset();
    filterTypeAttachment.reset();
    echoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, fxId ("Sync", ParamIDs::harmonyNoteSync), echoSync);
    filterTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, fxId ("Filter", ParamIDs::harmonyNoteFilter), filterType);
    echoDelay.slider.setEnabled (juce::roundToInt (value (fxId ("Sync", ParamIDs::harmonyNoteSync))) == 0);
}

void HarmonyPanel::bindVoiceControls()
{
    // The voice controls follow whichever voice is picked.
    for (auto [control, part] : { std::pair { &voiceOn, "On" }, std::pair { &voiceCorrect, "Correct" }, std::pair { &voiceFlatten, "Flatten" } })
    {
        control->attachment.reset();
        control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, voiceId (selected, part), control->button);
    }
    for (auto [knob, part] : { std::pair { &pitch, "Pitch" }, std::pair { &fine, "Fine" }, std::pair { &formant, "Formant" } })
    {
        knob->attachment.reset();
        const auto id = voiceId (selected, part);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        knob->slider.setDoubleClickReturnValue (true, 0.0);
    }
    pitch.slider.setTooltip ("Voice " + juce::String (selected + 1) + "'s interval from your note, in semitones (+4 a third up, +7 a fifth); CORRECT keeps it in the key");
    flattenNote.attachment.reset();
    flattenNote.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, voiceId (selected, "Note"), flattenNote.slider);
    flattenNote.slider.setDoubleClickReturnValue (true, effects::Harmony::flattenDefault);
    flattenNote.slider.setTooltip ("Voice " + juce::String (selected + 1) + " stays on this note, whatever you sing");
    bindFxControls();
    updateVisibility();
}

void HarmonyPanel::addVoiceAt (int voice, float pan, float levelDb)
{
    // A new voice starts at unison (U), as on Waves Harmony: set its interval next.
    const std::pair<const char*, float> fresh[] = { { "On", 1.0f }, { "Pitch", 0.0f }, { "Fine", 0.0f }, { "Formant", 0.0f },
                                                    { "Correct", 1.0f }, { "Pan", pan }, { "Level", levelDb },
                                                    { "Flatten", 0.0f }, { "Note", effects::Harmony::flattenDefault }, { "Delay", 0.0f },
                                                    { "Sync", 0.0f }, { "Feedback", 0.0f }, { "Filter", 0.0f },
                                                    { "Cutoff", effects::Harmony::filterHighestHz }, { "Active", 1.0f } };
    for (const auto& [part, plain] : fresh)
    {
        const auto id = voiceId (voice, part);
        beginGesture (id);
        set (id, plain);
        endGesture (id);
    }
}

void HarmonyPanel::removeVoice (int voice)
{
    const auto id = voiceId (voice, "Active");
    beginGesture (id);
    set (id, 0.0f);
    endGesture (id);
}

juce::Point<float> HarmonyPanel::pointFor (float pan, float levelDb) const
{
    const auto inner = graph.reduced (dotRadius + 2.0f);
    return { inner.getX() + (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.5f * inner.getWidth(),
             inner.getBottom() - heightForDb (levelDb) * inner.getHeight() };
}

std::pair<float, float> HarmonyPanel::panAndLevelAt (juce::Point<float> point) const
{
    const auto inner = graph.reduced (dotRadius + 2.0f);
    const auto pan = juce::jlimit (-1.0f, 1.0f, (point.x - inner.getX()) / inner.getWidth() * 2.0f - 1.0f);
    return { std::round (pan * 100.0f) / 100.0f, std::round (dbForHeight ((inner.getBottom() - point.y) / inner.getHeight()) * 10.0f) / 10.0f };
}

int HarmonyPanel::voiceAt (juce::Point<float> point) const
{
    // The picked voice first, as it's drawn on top.
    for (int pass = 0; pass < 2; ++pass)
        for (int voice = effects::Harmony::numVoices - 1; voice >= 0; --voice)
            if (isActive (voice) && (pass == 1 || voice == selected)
                && pointFor (value (voiceId (voice, "Pan")), value (voiceId (voice, "Level"))).getDistanceFrom (point) <= dotRadius + 2.0f)
                return voice;
    return -1;
}

void HarmonyPanel::mouseDown (const juce::MouseEvent& event)
{
    const auto point = event.position;
    if (! graph.contains (point))
        return;

    auto voice = voiceAt (point);
    if (voice >= 0 && event.mods.isPopupMenu())
    {
        removeVoice (voice);
        repaint();
        return;
    }
    if (voice < 0)
    {
        // An empty spot: the first free voice starts there.
        for (int free = 0; free < effects::Harmony::numVoices && voice < 0; ++free)
            if (! isActive (free))
                voice = free;
        if (voice < 0)
            return;   // all eight in use
        const auto [pan, level] = panAndLevelAt (point);
        addVoiceAt (voice, pan, level);
    }

    selectVoice (voice);
    dragging = voice;
    beginGesture (voiceId (voice, "Pan"));
    beginGesture (voiceId (voice, "Level"));
}

void HarmonyPanel::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging < 0)
        return;
    const auto [pan, level] = panAndLevelAt (event.position);
    set (voiceId (dragging, "Pan"), pan);
    set (voiceId (dragging, "Level"), level);
    repaint (graph.expanded (2.0f).getSmallestIntegerContainer());
}

void HarmonyPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    endGesture (voiceId (dragging, "Pan"));
    endGesture (voiceId (dragging, "Level"));
    dragging = -1;
}

void HarmonyPanel::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Scrolling over a voice changes its interval a semitone at a time.
    const auto voice = voiceAt (event.position);
    if (voice < 0 || std::abs (wheel.deltaY) < 1.0e-4f)
        return;
    const auto id = voiceId (voice, "Pitch");
    beginGesture (id);
    set (id, juce::jlimit (-24.0f, 24.0f, value (id) + (wheel.deltaY > 0.0f ? 1.0f : -1.0f)));
    endGesture (id);
    selectVoice (voice);
}

void HarmonyPanel::refreshTabs()
{
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        tabs[i].setColour (juce::TextButton::textColourOffId, isActive ((int) i) ? ParagonColours::text : ParagonColours::textDim.withAlpha (0.5f));
        // On the rack's keys: a voice not in use is lettered dimmer.
        if ((bool) tabs[i].getProperties()[ParagonLookAndFeel::dimTextProperty] == isActive ((int) i))
        {
            tabs[i].getProperties().set (ParagonLookAndFeel::dimTextProperty, ! isActive ((int) i));
            tabs[i].repaint();
        }
    }
}

void HarmonyPanel::timerCallback()
{
    // A modulated knob's ring (and the graph's ghost dots) move with it.
    if (modActions.offset != nullptr)
    {
        auto moving = false;
        static const auto count = ParamIDs::harmonyModTargets().size();
        for (int target = 0; target < count && ! moving; ++target)
            moving = std::abs (modActions.offset (target)) > 1.0e-5f;
        if (moving || anyModulated)
            repaint();
        anyModulated = moving;
    }
    // On the rack, the names follow the finish's ink.
    const auto ink = rack ? Finish::ink() : ParagonColours::textDim;
    if (ink != inkColour)
    {
        inkColour = ink;
        for (auto* child : getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                label->setColour (juce::Label::textColourId, ink);
    }
    // The voices can move from the DAW (automation, undo, a rig), and the
    // sung note changes all the time: keep both up to date.
    refreshTabs();
    if ((value (voiceId (selected, "Flatten")) > 0.5f) != shownFlatten)
        updateVisibility();   // PITCH becomes NOTE, or back
    // GLOBAL*: a global setting isn't where it starts (as the guide's asterisk).
    const auto globalChanged = value (ParamIDs::harmonyGlide) > effects::Harmony::minGlideMs + 0.5f
                               || juce::roundToInt (value (ParamIDs::harmonyGlideSync)) != 0
                               || std::abs (value (ParamIDs::harmonyGlobalPitch)) > 0.01f
                               || std::abs (value (ParamIDs::harmonyGlobalFormant)) > 0.01f;
    if (const auto text = globalChanged ? "GLOBAL*" : "GLOBAL"; globalTab.getButtonText() != text)
        globalTab.setButtonText (text);
    glide.slider.setEnabled (juce::roundToInt (value (ParamIDs::harmonyGlideSync)) == 0);
    echoDelay.slider.setEnabled (juce::roundToInt (value (fxId ("Sync", ParamIDs::harmonyNoteSync))) == 0);
    const auto note = sungNote ? sungNote() : -1.0f;
    juce::String text ("--");
    if (note >= 0.0f)
    {
        const auto named = PitchDetector::toNote (Scales::hzFromMidi (note));
        text = named.name + juce::String (named.octave);
    }
    // ...and, with CORRECT on, the note it's tuning to.
    juce::String tuned;
    if (const auto target = correctNote ? correctNote() : -1.0f; target >= 0.0f && juce::roundToInt (value (ParamIDs::harmonyCorrect)) > 0)
    {
        const auto named = PitchDetector::toNote (Scales::hzFromMidi (target));
        tuned = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + named.name + juce::String (named.octave);
    }
    if (text != shownSung || tuned != shownTuned)
    {
        shownSung = text;
        shownTuned = tuned;
        repaint (sungArea.getSmallestIntegerContainer());
    }
    // The graph only when something on it moved: redrawing it 20 times a
    // second for nothing took a good share of a core.
    if (const auto signature = graphSignature(); signature != shownGraph)
    {
        shownGraph = signature;
        repaint (graph.expanded (2.0f).getSmallestIntegerContainer());
    }
}

juce::uint64 HarmonyPanel::graphSignature() const
{
    // FNV-1a over what the graph draws: the settings (every parameter, as
    // the voices, chord and readout come from many), the picked voice and
    // view, the note voices sounding, the generated note and the held keys.
    juce::uint64 hash = 14695981039346656037ull;
    const auto add = [&hash] (juce::uint64 value)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    const auto addStep = [&add] (float value, float step) { add ((juce::uint64) (juce::int64) juce::roundToInt (value / step)); };
    for (const auto* value : allValues)
    {
        const auto number = value->load (std::memory_order_relaxed);
        juce::uint32 bits = 0;
        std::memcpy (&bits, &number, sizeof (bits));
        add (bits);
    }
    add ((juce::uint64) selected);
    add (notesShown ? 1u : 0u);
    if (harmonyEngine != nullptr)
    {
        for (int v = 0; v < effects::Harmony::numNoteVoices; ++v)
        {
            const auto note = harmonyEngine->getNoteVoiceNote (v);
            add ((juce::uint64) (juce::int64) note);
            if (note >= 0)
            {
                addStep (harmonyEngine->getNoteVoicePan (v), 0.005f);
                addStep (juce::Decibels::gainToDecibels (harmonyEngine->getNoteVoiceLevel (v), -60.0f), 0.25f);
            }
        }
        if (notesShown)
            add ((juce::uint64) (juce::int64) harmonyEngine->getGeneratedNote());
    }
    if (notesShown && midiKeyHeld != nullptr)
        for (int note = 36; note <= 83; ++note)
            add (midiKeyHeld (note) ? (juce::uint64) note : 0u);
    return hash;
}

void HarmonyPanel::setRack (bool inRack, juce::Rectangle<int> displayArea)
{
    const auto changed = rack != inRack || rackDisplay != displayArea;
    rack = inRack;
    rackDisplay = displayArea;
    if (! changed)
        return;
    styleForRack();
    resized();
    repaint();
}

void HarmonyPanel::styleForRack()
{
    for (auto* knob : { &pitch, &fine, &formant, &source, &harmony, &spread, &correctFormant, &flattenNote,
                        &minNote, &tolerance, &toleranceTime, &noteLevel, &noteWidth, &noteFormant, &noteAttack, &noteRelease,
                        &echoDelay, &echoFeedback, &cutoff, &glide, &globalPitch, &globalFormant })
    {
        knob->slider.getProperties().set (ParagonLookAndFeel::pedalKnobProperty, rack);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, rack ? 18 : 16);
        knob->slider.setColour (juce::Slider::textBoxTextColourId, rack ? LedDisplay::ledOrange : ParagonColours::accent);
        knob->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    }
    for (auto* box : { &key, &scale, &correct, &chord, &midiMap, &echoSync, &filterType, &glideSync })
    {
        box->setColour (juce::ComboBox::backgroundColourId, rack ? juce::Colours::transparentBlack : juce::Colour (0xff151217));
        box->setColour (juce::ComboBox::outlineColourId, rack ? juce::Colours::transparentBlack : ParagonColours::panelEdge);
        if (box != &chord && box != &midiMap)   // the note voices' lists stay blue
            box->setColour (juce::ComboBox::textColourId, rack ? LedDisplay::ledOrange : ParagonColours::accent);
        box->setColour (juce::ComboBox::arrowColourId, rack ? LedDisplay::ledOrange.withAlpha (0.6f) : ParagonColours::textDim);
    }
    for (auto* control : { &harmonyOn, &voiceOn, &voiceCorrect, &voiceFlatten, &generate })
        control->button.getProperties().set (ParagonLookAndFeel::rackKeyProperty, rack);
    for (auto& tab : tabs)
        tab.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, rack);
    for (auto* button : { &notesTab, &fxTab, &globalTab, &modsButton, &editMap, &snapshotsButton })
        button->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, rack);
    inkColour = {};
    timerCallback();
}

void HarmonyPanel::layOutRack()
{
    // The screens' glass, as LedDisplay::paintScreen will draw them.
    const auto glassOf = [] (juce::Rectangle<float> frame, float size)
    {
        juce::Image scratch (juce::Image::ARGB, 1, 1, true);
        juce::Graphics g (scratch);
        return LedDisplay::paintScreen (g, frame, size);
    };

    // The display: KEY, SCALE and the note sung, then the graph on its screen.
    auto left = rackDisplay;
    auto column = left.removeFromLeft (104);
    left.removeFromLeft (10);
    keyLabel.setBounds (column.removeFromTop (13));
    key.setBounds (column.removeFromTop (26).reduced (3, 1));
    column.removeFromTop (5);
    scaleLabel.setBounds (column.removeFromTop (13));
    scale.setBounds (column.removeFromTop (26).reduced (3, 1));
    column.removeFromTop (6);
    sungFrame = column.toFloat();
    sungArea = glassOf (sungFrame, 0.35f).reduced (4.0f, 2.0f);
    graphFrame = left.toFloat();
    auto glass = glassOf (graphFrame, 0.45f).reduced (4.0f, 4.0f);
    readoutArea = glass.removeFromBottom (14.0f);
    graph = glass;
    snapshotsButton.setBounds (graph.toNearestInt().removeFromTop (24).removeFromRight (84).reduced (3, 2));

    // The controls, right of the display.
    auto area = getLocalBounds().withTrimmedLeft (rackDisplay.getRight() + 28);

    // Top row (the main view puts HARMONY / OCTAVE-FREEZE first, and GRAPHIC EQ at the right).
    auto keys = area.removeFromTop (24);
    keys.removeFromLeft (248);
    harmonyOn.label.setBounds (keys.removeFromLeft (62));
    harmonyOn.button.setBounds (keys.removeFromLeft (48).withSizeKeepingCentre (44, 24));
    keys.removeFromLeft (12);
    globalTab.setBounds (keys.removeFromLeft (82));
    keys.removeFromLeft (6);
    modsButton.setBounds (keys.removeFromLeft (64));
    keys.removeFromRight (120);
    correct.setBounds (keys.removeFromRight (96).reduced (0, 1));
    correctLabel.setBounds (keys.removeFromRight (74));
    area.removeFromTop (6);

    // The wide screen the Note Map and SNAPSHOTS open on.
    wideScreen = rackDisplay.withRight (area.getRight()).withTop (area.getY()).toFloat();
    const auto wide = glassOf (wideScreen, 0.35f).reduced (2.0f).toNearestInt();
    noteMap.setBounds (wide);
    snapshots.setBounds (wide);
    mods.setBounds (wide);

    // Second row: the voices' keys, NOTES and FX.
    auto tabRow = area.removeFromTop (24);
    for (auto& tab : tabs)
    {
        tab.setBounds (tabRow.removeFromLeft (38));
        tabRow.removeFromLeft (4);
    }
    tabRow.removeFromLeft (8);
    notesTab.setBounds (tabRow.removeFromLeft (70));
    tabRow.removeFromLeft (4);
    fxTab.setBounds (tabRow.removeFromLeft (48));
    area.removeFromTop (6);

    // Then the view's controls in one row: knobs (with their values on
    // screens under them), lists on screens, switches as keys, all lined up
    // on the knobs' middle.
    enum Kind { knob, list, key };
    struct Item { juce::Label* label; juce::Component* widget; Kind kind; float weight; };
    std::vector<Item> row;
    const auto addKnob = [&row] (Knob& k) { row.push_back ({ &k.label, &k.slider, knob, 1.0f }); };
    if (globalShown)
    {
        addKnob (glide);
        row.push_back ({ &glideSyncLabel, &glideSync, list, 1.7f });
        addKnob (globalPitch);
        addKnob (globalFormant);
        for (auto* k : { &source, &harmony, &spread, &correctFormant })
            addKnob (*k);
    }
    else if (fxShown)
    {
        addKnob (echoDelay);
        row.push_back ({ &syncLabel, &echoSync, list, 1.7f });
        addKnob (echoFeedback);
        row.push_back ({ &filterLabel, &filterType, list, 1.7f });
        addKnob (cutoff);
    }
    else if (notesShown)
    {
        row.push_back ({ &generate.label, &generate.button, key, 0.8f });
        row.push_back ({ &chordLabel, &chord, list, 2.9f });
        row.push_back ({ &midiLabel, &midiMap, list, 1.5f });
        for (auto* k : { &minNote, &tolerance, &toleranceTime, &noteLevel, &noteWidth, &noteFormant, &noteAttack, &noteRelease })
            addKnob (*k);
    }
    else
    {
        for (auto* s : { &voiceOn, &voiceCorrect, &voiceFlatten })
            row.push_back ({ &s->label, &s->button, key, 0.75f });
        addKnob (shownFlatten ? flattenNote : pitch);
        for (auto* k : { &fine, &formant, &source, &harmony, &spread, &correctFormant })
            addKnob (*k);
    }
    auto totalWeight = 0.0f;
    for (const auto& item : row)
        totalWeight += item.weight;
    const auto unit = juce::jmin (112.0f, (float) area.getWidth() / juce::jmax (1.0f, totalWeight));
    auto x = (float) area.getX();
    const auto knobSize = juce::jmin (juce::roundToInt (unit * 1.3f), area.getHeight() - 14 - 18 - 4);
    for (const auto& item : row)
    {
        const auto width = unit * item.weight;
        auto cell = juce::Rectangle<int> (juce::roundToInt (x), area.getY(), juce::roundToInt (width), area.getHeight()).reduced (3, 0);
        x += width;
        item.label->setBounds (cell.removeFromTop (14).expanded (4, 0));
        const auto knobCentreY = cell.getY() + juce::roundToInt ((float) knobSize * 0.42f);
        if (item.kind == knob)
            item.widget->setBounds (cell.withSizeKeepingCentre (knobSize, knobSize + 18 + 4).withY (cell.getY()));
        else if (item.kind == key)
            item.widget->setBounds (juce::Rectangle<int> (juce::jmin (44, cell.getWidth()), 24).withCentre ({ cell.getCentreX(), knobCentreY }));
        else
        {
            auto box = juce::Rectangle<int> (cell.getWidth(), 28).withCentre ({ cell.getCentreX(), knobCentreY });
            if (item.widget == &chord)
            {
                editMap.setBounds (box.removeFromRight (48).reduced (0, 2));
                box.removeFromRight (6);
            }
            item.widget->setBounds (box);
        }
    }
    // PITCH and NOTE share their place.
    if (! globalShown && ! fxShown && ! notesShown)
    {
        const auto& shown = shownFlatten ? flattenNote : pitch;
        auto& other = shownFlatten ? pitch : flattenNote;
        other.label.setBounds (shown.label.getBounds());
        other.slider.setBounds (shown.slider.getBounds());
    }
}

void HarmonyPanel::paintOverChildren (juce::Graphics& g)
{
    for (const auto& glass : glasses)
        LedDisplay::paintGlassOverlay (g, glass, 0.35f);

    // A modulated knob: a ring in its modulator's colour (white for
    // several), with a dot where it has the control now.
    if (modActions.get == nullptr || mods.isVisible() || noteMap.isVisible() || snapshots.isVisible())
        return;
    for (auto* knob : allKnobs())
    {
        if (! knob->slider.isVisible())
            continue;
        const auto target = targetOf (*knob);
        if (target < 0)
            continue;
        auto colour = juce::Colours::transparentBlack;
        for (int slot = 0; slot < effects::HarmonyModulation::slotsPerTarget; ++slot)
            if (const auto found = modActions.get (target, slot); effects::HarmonyModulation::isBuilt (found.source))
                colour = colour.isTransparent() ? HarmonyModsView::colourOf (found.source) : juce::Colours::white;
        if (! colour.isTransparent())
            MarsModUi::paintModulation (g, *this, knob->slider, modActions.offset != nullptr ? modActions.offset (target) : 0.0f, colour);
    }
}

void HarmonyPanel::resized()
{
    if (rack)
    {
        layOutRack();
        return;
    }
    auto area = getLocalBounds().reduced (16, 10);
    area.removeFromTop (34);   // header: the icon, PITCH and the view tabs (placed by the main view)

    // Left: on, key, scale, the sung note.
    auto left = area.removeFromLeft (112);
    area.removeFromLeft (10);
    // HARMONY's switch, and GLOBAL (its settings for every voice) beside it.
    {
        auto labels = left.removeFromTop (14);
        auto row = left.removeFromTop (28);
        // The switch needs only 26 px; its label can run wider, in the row above.
        const auto switchWidth = 38;
        harmonyOn.label.setBounds (labels.removeFromLeft (56));
        harmonyOn.button.setBounds (row.removeFromLeft (switchWidth).withSizeKeepingCentre (26, 26));
        globalTab.setBounds (row.reduced (2, 2));
    }
    left.removeFromTop (6);
    keyLabel.setBounds (left.removeFromTop (14));
    key.setBounds (left.removeFromTop (26).reduced (4, 0));
    left.removeFromTop (6);
    scaleLabel.setBounds (left.removeFromTop (14));
    scale.setBounds (left.removeFromTop (26).reduced (4, 0));
    left.removeFromTop (4);
    sungArea = left.toFloat();
    noteMap.setBounds (area);   // over the graph and the controls
    snapshots.setBounds (area);
    mods.setBounds (area);

    // CORRECT (Waves Harmony puts it by SOURCE; here there's room in the
    // header, left of the GRAPHIC EQ button the main view places).
    // (MainView puts that button 130 px in from the right edge.)
    auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).withRight (getWidth() - 130 - 8);
    correct.setBounds (header.removeFromRight (78).reduced (0, 1));
    correctLabel.setBounds (header.removeFromRight (62));
    header.removeFromRight (8);
    modsButton.setBounds (header.removeFromRight (56).reduced (0, 1));


    // Middle: the graph, and the picked voice's pan and level under it.
    auto middle = area.removeFromLeft (notesShown ? 150 : 196);   // NOTES: a narrower graph, room for its controls
    area.removeFromLeft (10);
    readoutArea = middle.removeFromBottom (16).toFloat();
    graph = middle.toFloat();
    // SNAPSHOTS in the graph's top right corner (above its 0 dB line).
    snapshotsButton.setBounds (middle.withSizeKeepingCentre (middle.getWidth(), middle.getHeight()).removeFromTop (20).removeFromRight (74).reduced (3, 3));

    // Right: voice tabs, the picked voice, then the mix.
    auto right = area;
    auto tabRow = right.removeFromTop (22);
    fxTab.setBounds (tabRow.removeFromRight (34));
    tabRow.removeFromRight (4);
    notesTab.setBounds (tabRow.removeFromRight (56));
    tabRow.removeFromRight (6);
    const auto tabWidth = (tabRow.getWidth() - 3 * ((int) tabs.size() - 1)) / (int) tabs.size();
    for (auto& tab : tabs)
    {
        tab.setBounds (tabRow.removeFromLeft (tabWidth));
        tabRow.removeFromLeft (3);
    }
    right.removeFromTop (6);
    const auto rowHeight = (right.getHeight() - 6) / 2;

    if (globalShown)
    {
        // GLOBAL: GLIDE, SYNC, PITCH, FORMANT; then the mix, as in the voice view.
        const auto knobIn = [] (Knob& knob, juce::Rectangle<int> cell)
        {
            knob.label.setBounds (cell.removeFromTop (14));
            knob.slider.setBounds (cell.reduced (1, 0));
        };
        auto top = right.removeFromTop (rowHeight);
        const auto unit = top.getWidth() / 5;
        knobIn (glide, top.removeFromLeft (unit));
        auto syncCell = top.removeFromLeft (unit * 2).reduced (4, 0);
        glideSyncLabel.setBounds (syncCell.removeFromTop (14));
        glideSync.setBounds (syncCell.withSizeKeepingCentre (syncCell.getWidth(), 24).translated (0, -8));
        knobIn (globalPitch, top.removeFromLeft (unit));
        knobIn (globalFormant, top);
        right.removeFromTop (6);
        auto mixRow = right;
        const auto mixWidth = mixRow.getWidth() / 4;
        for (auto* knob : { &source, &harmony, &spread, &correctFormant })
            knobIn (*knob, mixRow.removeFromLeft (mixWidth));
        return;
    }

    if (fxShown)
    {
        // FX: DELAY, SYNC, FEEDBACK; then FILTER and CUTOFF.
        const auto knobIn = [] (Knob& knob, juce::Rectangle<int> cell)
        {
            knob.label.setBounds (cell.removeFromTop (14));
            knob.slider.setBounds (cell.reduced (1, 0));
        };
        const auto boxIn = [] (juce::Label& label, juce::ComboBox& box, juce::Rectangle<int> cell)
        {
            cell = cell.reduced (4, 0);
            label.setBounds (cell.removeFromTop (14));
            box.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 24).translated (0, -8));
        };
        auto top = right.removeFromTop (rowHeight);
        const auto unit = top.getWidth() / 4;
        knobIn (echoDelay, top.removeFromLeft (unit));
        boxIn (syncLabel, echoSync, top.removeFromLeft (unit * 2));
        knobIn (echoFeedback, top);
        right.removeFromTop (6);
        auto bottom = right;
        boxIn (filterLabel, filterType, bottom.removeFromLeft (unit * 2));
        knobIn (cutoff, bottom.removeFromLeft (unit));
        return;
    }

    if (notesShown)
    {
        // NOTES: what makes the notes, then how the note voices sound.
        auto top = right.removeFromTop (rowHeight);
        const auto unit = (float) top.getWidth() / 6.0f;   // GENERATE 0.62, CHORD 2.48 (with EDIT), the knobs about 1 each
        generate.label.setBounds (top.removeFromLeft (juce::roundToInt (unit * 0.62f)).removeFromTop (14));
        generate.button.setBounds (juce::Rectangle<int> (generate.label.getX(), generate.label.getBottom(), generate.label.getWidth(), top.getHeight() - 14)
                                       .withSizeKeepingCentre (26, 26));
        auto chordCell = top.removeFromLeft (juce::roundToInt (unit * 2.48f)).reduced (2, 0);
        chordLabel.setBounds (chordCell.removeFromTop (14));
        auto chordRow = chordCell.withSizeKeepingCentre (chordCell.getWidth(), 24).translated (0, -8);
        editMap.setBounds (chordRow.removeFromRight (40));
        chordRow.removeFromRight (3);
        chord.setBounds (chordRow);
        const auto knobWidth = top.getWidth() / 3;
        for (auto* knob : { &minNote, &tolerance, &toleranceTime })
        {
            auto cell = top.removeFromLeft (knobWidth);
            knob->label.setBounds (cell.removeFromTop (14));
            knob->slider.setBounds (cell.reduced (1, 0));
        }
        right.removeFromTop (6);
        auto bottom = right;
        const auto width = bottom.getWidth() * 10 / 68;   // MIDI 1.8, the knobs 1 each (of 6.8)
        auto midiCell = bottom.removeFromLeft (bottom.getWidth() - 5 * width).reduced (2, 0);
        midiLabel.setBounds (midiCell.removeFromTop (14));
        midiMap.setBounds (midiCell.withSizeKeepingCentre (midiCell.getWidth(), 24).translated (0, -8));
        for (auto* knob : { &noteLevel, &noteWidth, &noteFormant, &noteAttack, &noteRelease })
        {
            auto cell = bottom.removeFromLeft (width);
            knob->label.setBounds (cell.removeFromTop (14));
            knob->slider.setBounds (cell.reduced (1, 0));
        }
        return;
    }

    auto voiceRow = right.removeFromTop (rowHeight);
    const auto place = [] (juce::Label& label, juce::Component& widget, juce::Rectangle<int> cell, bool isSwitch)
    {
        label.setBounds (cell.removeFromTop (14));
        if (isSwitch)
            widget.setBounds (cell.withSizeKeepingCentre (26, 26));
        else
            widget.setBounds (cell.reduced (1, 0));
    };
    // Three switches: just wide enough for their names, to leave the knobs room.
    place (voiceOn.label, voiceOn.button, voiceRow.removeFromLeft (34), true);
    place (voiceCorrect.label, voiceCorrect.button, voiceRow.removeFromLeft (50), true);
    place (voiceFlatten.label, voiceFlatten.button, voiceRow.removeFromLeft (50), true);
    const auto knobWidth = voiceRow.getWidth() / 3;
    const auto pitchCell = voiceRow.removeFromLeft (knobWidth);
    place (pitch.label, pitch.slider, pitchCell, false);
    place (flattenNote.label, flattenNote.slider, pitchCell, false);   // in PITCH's place while FLATTEN is on
    for (auto* knob : { &fine, &formant })
        place (knob->label, knob->slider, voiceRow.removeFromLeft (knobWidth), false);

    right.removeFromTop (6);
    auto mixRow = right;
    const auto mixWidth = mixRow.getWidth() / 4;
    for (auto* knob : { &source, &harmony, &spread, &correctFormant })
        place (knob->label, knob->slider, mixRow.removeFromLeft (mixWidth), false);
}

void HarmonyPanel::paint (juce::Graphics& g)
{
    glasses.clear();
    if (rack)
    {
        // On the unit's face: the Note Map / SNAPSHOTS screen when one is
        // open; otherwise the display's screens, and screens behind the
        // values and lists.
        if (noteMap.isVisible() || snapshots.isVisible() || mods.isVisible())
        {
            LedDisplay::paintScreen (g, wideScreen, 0.35f);
            glasses.push_back (LedDisplay::paintScreen (g, correct.getBounds().toFloat().expanded (2.0f, 3.0f), 0.35f));
            return;
        }
        LedDisplay::paintScreen (g, graphFrame, 0.45f);
        glasses.push_back (LedDisplay::paintScreen (g, sungFrame, 0.35f));
        for (auto* child : getChildren())
        {
            if (! child->isVisible())
                continue;
            if (auto* box = dynamic_cast<juce::ComboBox*> (child))
                glasses.push_back (LedDisplay::paintScreen (g, box->getBounds().toFloat().expanded (2.0f, 3.0f), 0.35f));
            else if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                const auto valueBox = slider->getBounds().toFloat().removeFromBottom (18.0f);
                // As wide as the knob's own column (its name's width), so neighbours' screens don't touch.
                auto width = juce::jmin (valueBox.getWidth(), 84.0f);
                for (auto* label : getChildren())
                    if (auto* name = dynamic_cast<juce::Label*> (label); name != nullptr && name->isVisible()
                        && name->getBottom() <= slider->getY() + 2 && std::abs (name->getBounds().getCentreX() - slider->getBounds().getCentreX()) < 3)
                        width = juce::jmin (width, (float) name->getWidth() - 14.0f);
                glasses.push_back (LedDisplay::paintScreen (g, valueBox.withSizeKeepingCentre (width, valueBox.getHeight()).expanded (0.0f, 3.0f), 0.35f));
            }
        }
    }
    else
    {
        // The same dark panel and header as the other blocks' panels.
        const auto bounds = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, bounds.getY(),
                                                 juce::Colour (0xff09070a), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
        Icons::draw (g, Icons::Type::pitch, header.removeFromLeft (26.0f).withSizeKeepingCentre (22.0f, 20.0f), accent, 1.6f, true);
        header.removeFromLeft (8.0f);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("PITCH", header, juce::Justification::centredLeft, false);
    }

    // The sung note, big, under KEY and SCALE.
    if (! sungArea.isEmpty())
    {
        auto area = sungArea;
        g.setColour (rack ? LedDisplay::ledOrange.withAlpha (0.6f) : ParagonColours::textDim);
        g.setFont (juce::FontOptions (rack ? 8.5f : 10.5f, juce::Font::bold));
        g.drawText ("SINGING", area.removeFromTop (14.0f), juce::Justification::centred, false);
        g.setColour (shownSung == "--" ? ParagonColours::textDim.withAlpha (0.6f) : accent);
        if (shownTuned.isNotEmpty() && area.getHeight() < 36.0f)
        {
            // Short of room (GLOBAL sits below): the note and where it's
            // being tuned to on one line.
            g.setFont (juce::FontOptions (juce::jlimit (11.0f, 18.0f, area.getHeight() * 0.85f), juce::Font::bold));
            g.drawFittedText (shownSung + " " + shownTuned, area.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        else
        {
            g.setFont (juce::FontOptions (juce::jmin (22.0f, area.getHeight() * 0.9f), juce::Font::bold));
            const auto tunedLine = area.removeFromBottom (shownTuned.isEmpty() ? 0.0f : 14.0f);
            g.drawText (shownSung, area, juce::Justification::centred, false);
            if (shownTuned.isNotEmpty())
            {
                g.setColour (ParagonColours::accent);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (shownTuned, tunedLine, juce::Justification::centred, false);
            }
        }
    }

    // The graph: level lines across, the middle line down, L and R (on the
    // rack, on its screen's glass).
    if (! rack)
    {
        g.setColour (juce::Colour (0xff0f0c10));
        g.fillRoundedRectangle (graph, 6.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (graph.reduced (0.5f), 6.0f, 1.0f);
    }
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    for (const auto db : { 0, -6, -12, -24 })
    {
        const auto y = pointFor (0.0f, (float) db).y;
        g.setColour (accent.withAlpha (db == 0 ? 0.18f : 0.08f));
        g.drawHorizontalLine (juce::roundToInt (y), graph.getX() + 4.0f, graph.getRight() - 4.0f);
        g.setColour (ParagonColours::textDim.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (graph.getX() + 4.0f, y - 11.0f, 24.0f, 10.0f),
                    juce::Justification::centredLeft, false);
    }
    g.setColour (accent.withAlpha (0.12f));
    g.drawVerticalLine (juce::roundToInt (graph.getCentreX()), graph.getY() + 4.0f, graph.getBottom() - 4.0f);
    g.setColour (ParagonColours::textDim.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("L", graph.reduced (6.0f, 4.0f), juce::Justification::bottomLeft, false);
    g.drawText ("R", graph.reduced (6.0f, 4.0f), juce::Justification::bottomRight, false);

    // The voices: a dot each, labelled with its interval. Off voices are
    // grey; the picked one has a white ring and is drawn on top.
    const auto harmonyIsOn = value (ParamIDs::harmonyOn) > 0.5f;
    const auto drawVoice = [&] (int voice)
    {
        const auto centre = pointFor (value (voiceId (voice, "Pan")), value (voiceId (voice, "Level")));
        const auto dot = juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (centre);
        const auto sounding = harmonyIsOn && value (voiceId (voice, "On")) > 0.5f;
        const auto colour = value (voiceId (voice, "On")) > 0.5f ? accent : ParagonColours::textDim;
        if (sounding)
        {
            g.setColour (colour.withAlpha (0.2f));
            g.fillEllipse (dot.expanded (4.0f));
        }
        g.setColour (colour.withAlpha (sounding ? 1.0f : 0.45f));
        g.fillEllipse (dot);
        if (voice == selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (dot.expanded (2.5f), 1.6f);
        }
        g.setColour (juce::Colour (0xff101418));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        // A flattened voice shows its note (as on Waves Harmony), the others their interval.
        const auto flatNote = juce::roundToInt (value (voiceId (voice, "Note")));
        const auto named = PitchDetector::toNote (Scales::hzFromMidi ((float) flatNote));
        g.drawText (value (voiceId (voice, "Flatten")) > 0.5f ? named.name + juce::String (named.octave)
                                                               : intervalText (value (voiceId (voice, "Pitch"))),
                    dot, juce::Justification::centred, false);
    };
    // Ghost markers (as the guide's): where a modulated PAN or LEVEL has a voice now.
    if (modActions.offset != nullptr)
        for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
        {
            if (! isActive (voice))
                continue;
            static const auto ids = ParamIDs::harmonyModTargets();
            const auto panOffset = modActions.offset (ids.indexOf (voiceId (voice, "Pan")));
            const auto levelOffset = modActions.offset (ids.indexOf (voiceId (voice, "Level")));
            if (std::abs (panOffset) < 1.0e-4f && std::abs (levelOffset) < 1.0e-4f)
                continue;
            auto* levelParameter = parameters.getParameter (voiceId (voice, "Level"));
            const auto levelNow = levelParameter->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, levelParameter->getValue() + levelOffset));
            const auto panNow = juce::jlimit (-1.0f, 1.0f, value (voiceId (voice, "Pan")) + panOffset * 2.0f);
            const auto ghost = juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (pointFor (panNow, levelNow));
            g.setColour (accent.withAlpha (0.3f));
            g.fillEllipse (ghost);
            g.setColour (accent.withAlpha (0.6f));
            g.drawEllipse (ghost, 1.0f);
        }
    for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
        if (isActive (voice) && voice != selected)
            drawVoice (voice);
    if (isActive (selected))
        drawVoice (selected);
    else
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawFittedText ("Click to add a voice", graph.reduced (8.0f).toNearestInt(), juce::Justification::centredTop, 1);
    }

    // The note voices, in blue, while they sound.
    if (harmonyEngine != nullptr)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto noteDb = value (ParamIDs::harmonyNoteLevel);
        for (int v = 0; v < effects::Harmony::numNoteVoices; ++v)
        {
            const auto note = harmonyEngine->getNoteVoiceNote (v);
            if (note < 0)
                continue;
            const auto envelope = harmonyEngine->getNoteVoiceLevel (v);
            const auto centre = pointFor (harmonyEngine->getNoteVoicePan (v), noteDb + juce::Decibels::gainToDecibels (envelope, -60.0f));
            const auto dot = juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (centre);
            g.setColour (noteBlue.withAlpha (0.25f));
            g.fillEllipse (dot.expanded (3.0f));
            g.setColour (noteBlue);
            g.fillEllipse (dot);
            g.setColour (juce::Colour (0xff0b1420));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (names[note % 12]) + juce::String (note / 12 - 1), dot, juce::Justification::centred, false);
        }
    }

    if (notesShown)
    {
        // The keyboard strip, C2 to B5: held MIDI keys pink, the generated note blue.
        const auto keys = keyboardArea();
        constexpr int lowest = 36, highest = 83;
        const auto whiteCount = 28;   // four octaves of seven
        const auto whiteWidth = keys.getWidth() / (float) whiteCount;
        const auto isBlack = [] (int note) { const auto n = note % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; };
        const auto generated = harmonyEngine != nullptr ? harmonyEngine->getGeneratedNote() : -1;
        const auto colourFor = [&] (int note, juce::Colour normal)
        {
            if (midiKeyHeld != nullptr && midiKeyHeld (note))
                return juce::Colour (0xffff6fa8);
            return note == generated ? noteBlue : normal;
        };
        auto white = 0;
        std::array<float, 128> xOf {};
        for (int note = lowest; note <= highest; ++note)
            if (! isBlack (note))
            {
                const auto whiteKey = juce::Rectangle<float> (keys.getX() + white * whiteWidth, keys.getY(), whiteWidth, keys.getHeight()).reduced (0.5f, 0.0f);
                g.setColour (colourFor (note, juce::Colour (0xffd8d4da)));
                g.fillRect (whiteKey);
                xOf[(size_t) note] = whiteKey.getRight();
                ++white;
            }
        for (int note = lowest; note <= highest; ++note)
            if (isBlack (note))
            {
                const auto blackKey = juce::Rectangle<float> (whiteWidth * 0.6f, keys.getHeight() * 0.6f).withCentre ({ xOf[(size_t) note - 1], keys.getY() + keys.getHeight() * 0.3f });
                g.setColour (colourFor (note, juce::Colour (0xff1a171c)));
                g.fillRect (blackKey);
            }

        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        const auto on = value (ParamIDs::harmonyGenerate) > 0.5f;
        g.drawText (on ? "NOTES   " + chord.getText().toUpperCase() : juce::String ("NOTES   GENERATE IS OFF"), readoutArea, juce::Justification::centred, false);
        return;
    }

    // The picked voice's pan and level, exactly.
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    const auto panText = parameters.getParameter (voiceId (selected, "Pan"))->getCurrentValueAsText();
    const auto levelText = parameters.getParameter (voiceId (selected, "Level"))->getCurrentValueAsText();
    g.drawText ("VOICE " + juce::String (selected + 1) + (isActive (selected) ? "   PAN " + panText + "   LEVEL " + levelText : juce::String ("   (not in use)")),
                readoutArea, juce::Justification::centred, false);
}
