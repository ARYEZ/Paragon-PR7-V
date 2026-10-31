#include "NoteMapEditor.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    using Harmony = effects::Harmony;

    const juce::Colour noteBlue { 0xff4fa3ff }, picked { 0xffffd84a }, assigned { 0xffc58cff }, held { 0xffff6fa8 };
    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    bool isBlack (int note)
    {
        const auto n = ((note % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1d2a3a));
        button.setColour (juce::TextButton::textColourOnId, noteBlue);
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    }

    constexpr float labelWidth = 44.0f;
}

NoteMapEditor::NoteMapEditor (juce::AudioProcessorValueTreeState& state, std::function<bool (int)> isKeyHeld)
    : parameters (state), keyHeld (std::move (isKeyHeld))
{
    for (auto* button : { &spread, &fold, &fromChord, &eraseButton, &done })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    spread.setClickingTogglesState (true);
    fold.setClickingTogglesState (true);
    spread.setRadioGroupId (4310);
    fold.setRadioGroupId (4310);
    spread.setTooltip ("The notes each note plays move with the octave you sing or play in");
    fold.setTooltip ("The notes each note plays stay where they're set, whatever the octave");
    spread.onClick = [this] { set (ParamIDs::harmonyMapFold, 0.0f); };
    fold.onClick = [this] { set (ParamIDs::harmonyMapFold, 1.0f); };
    fromChord.setTooltip ("Start from one of the built-in chord maps (replaces this map)");
    fromChord.onClick = [this]
    {
        juce::PopupMenu menu;
        for (int chord = 0; chord < Harmony::customChord; ++chord)
            menu.addItem (chord + 1, Harmony::chordMaps[(size_t) chord].name);
        juce::Component::SafePointer<NoteMapEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fromChord), [safe] (int chosen)
        {
            if (safe != nullptr && chosen > 0)
                safe->fillFromChord (chosen - 1);
        });
    };
    eraseButton.setTooltip ("Clear the whole map");
    eraseButton.onClick = [this] { erase(); };
    done.onClick = [this] { if (onDone != nullptr) onDone(); };
    startTimerHz (15);
}

NoteMapEditor::~NoteMapEditor() = default;

//==============================================================================
float NoteMapEditor::value (const juce::String& id) const { return parameters.getRawParameterValue (id)->load(); }

void NoteMapEditor::set (const juce::String& id, float plainValue)
{
    auto* parameter = parameters.getParameter (id);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    parameter->endChangeGesture();
}

int NoteMapEditor::key() const { return juce::roundToInt (value (ParamIDs::harmonyKey)); }
Scales::Scale NoteMapEditor::scale() const { return (Scales::Scale) juce::roundToInt (value (ParamIDs::harmonyScale)); }

Scales::Custom NoteMapEditor::customScale() const
{
    Scales::Custom custom {};
    for (int note = 0; note < 12; ++note)
    {
        const auto box = juce::roundToInt (value (ParamIDs::harmonyScaleNote (note)));
        custom[(size_t) note] = box == 1 ? 1 : box == 2 ? -1 : 0;
    }
    return custom;
}

std::uint64_t NoteMapEditor::mapOf (int noteClass) const
{
    std::uint64_t bits = 0;
    for (int part = 0; part < 3; ++part)
        bits |= (std::uint64_t) juce::roundToInt (value (ParamIDs::harmonyMap (noteClass, part))) << (16 * part);
    return bits;
}

void NoteMapEditor::setMap (int noteClass, std::uint64_t bits)
{
    for (int part = 0; part < 3; ++part)
        set (ParamIDs::harmonyMap (noteClass, part), (float) ((bits >> (16 * part)) & 0xffffu));
    repaint();
}

void NoteMapEditor::selectNote (int noteClass)
{
    selected = juce::jlimit (0, 11, noteClass);
    repaint();
}

void NoteMapEditor::toggleKey (int midiNote)
{
    const auto offset = midiNote - (reference + selected);
    if (std::abs (offset) > Harmony::mapSpan)
        return;
    setMap (selected, mapOf (selected) ^ (std::uint64_t { 1 } << (offset + Harmony::mapSpan)));
}

void NoteMapEditor::cycleScaleBox (int noteClass)
{
    if (scale() != Scales::Scale::custom)
        return;
    // The boxes are drawn over the keys; the setting is kept above the KEY.
    const auto id = ParamIDs::harmonyScaleNote (Scales::aboveKey (noteClass, key()));
    set (id, (float) ((juce::roundToInt (value (id)) + 1) % 3));
    repaint();
}

void NoteMapEditor::fillFromChord (int chord)
{
    // What each note plays through that map, as the engine does it: the note
    // into the key, then each interval (a third stays a third).
    const auto& map = Harmony::chordMaps[(size_t) juce::jlimit (0, Harmony::customChord - 1, chord)];
    const auto custom = customScale();
    for (int noteClass = 0; noteClass < 12; ++noteClass)
    {
        const auto incoming = reference + noteClass;
        const auto root = Scales::nearest (incoming, key(), scale(), &custom);
        std::uint64_t bits = 0;
        for (int i = 0; i < map.count; ++i)
        {
            const auto offset = Scales::harmonyNote (root, map.intervals[(size_t) i], key(), scale(), &custom) - incoming;
            if (std::abs (offset) <= Harmony::mapSpan)
                bits |= std::uint64_t { 1 } << (offset + Harmony::mapSpan);
        }
        setMap (noteClass, bits);
    }
}

void NoteMapEditor::erase()
{
    for (int noteClass = 0; noteClass < 12; ++noteClass)
        setMap (noteClass, 0);
}

//==============================================================================
void NoteMapEditor::visibilityChanged()
{
    if (isVisible())
        timerCallback();   // right away, not a timer tick later
}

void NoteMapEditor::timerCallback()
{
    // The SPREAD/FOLD buttons and the held keys follow the plugin.
    const auto isFold = value (ParamIDs::harmonyMapFold) > 0.5f;
    (isFold ? fold : spread).setToggleState (true, juce::dontSendNotification);
    auto changed = false;
    for (int note = lowest; note <= highest; ++note)
    {
        const auto now = keyHeld != nullptr && keyHeld (note);
        changed = changed || now != shownHeld[(size_t) note];
        shownHeld[(size_t) note] = now;
    }
    if (changed)
        repaint (keyboard.toNearestInt().expanded (2));
}

void NoteMapEditor::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    auto top = area.removeFromTop (22);
    top.removeFromLeft (78);   // the title
    for (auto* button : { &spread, &fold })
    {
        button->setBounds (top.removeFromLeft (58).reduced (1, 0));
        top.removeFromLeft (2);
    }
    top.removeFromLeft (8);
    done.setBounds (top.removeFromRight (54).reduced (1, 0));
    top.removeFromRight (4);
    eraseButton.setBounds (top.removeFromRight (54).reduced (1, 0));
    top.removeFromRight (4);
    fromChord.setBounds (top.removeFromRight (juce::jmin (96, top.getWidth())).reduced (1, 0));
    area.removeFromTop (4);
    rows = area.removeFromTop (38).toFloat();
    area.removeFromTop (3);
    area.removeFromBottom (14);   // the help line
    keyboard = area.toFloat().withTrimmedLeft (labelWidth);
}

juce::Rectangle<float> NoteMapEditor::keyBounds (int midiNote) const
{
    // Three octaves of seven white keys; black keys sit across the gaps.
    constexpr int whites = 21;
    const auto whiteWidth = keyboard.getWidth() / whites;
    auto whiteIndex = 0;
    for (int note = lowest; note < midiNote; ++note)
        whiteIndex += isBlack (note) ? 0 : 1;
    if (! isBlack (midiNote))
        return { keyboard.getX() + whiteIndex * whiteWidth, keyboard.getY(), whiteWidth, keyboard.getHeight() };
    return juce::Rectangle<float> (whiteWidth * 0.62f, keyboard.getHeight() * 0.6f)
               .withPosition (keyboard.getX() + whiteIndex * whiteWidth - whiteWidth * 0.31f, keyboard.getY());
}

juce::Rectangle<float> NoteMapEditor::boxBounds (int row, int noteClass) const
{
    // Two rows of 12 boxes across the keyboard: wide enough for "C#".
    const auto from = keyboard.getX(), to = keyboard.getRight();
    const auto width = (to - from) / 12.0f;
    return { from + noteClass * width, rows.getY() + row * (rows.getHeight() / 2.0f), width, rows.getHeight() / 2.0f };
}

int NoteMapEditor::keyAt (juce::Point<float> point) const
{
    // Black keys first: they sit on top.
    for (int pass = 0; pass < 2; ++pass)
        for (int note = lowest; note <= highest; ++note)
            if (isBlack (note) == (pass == 0) && keyBounds (note).contains (point))
                return note;
    return -1;
}

void NoteMapEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto point = event.position;
    for (int noteClass = 0; noteClass < 12; ++noteClass)
    {
        if (boxBounds (0, noteClass).contains (point))
            return selectNote (noteClass);
        if (boxBounds (1, noteClass).contains (point))
            return cycleScaleBox (noteClass);
    }
    if (const auto note = keyAt (point); note >= 0)
        toggleKey (note);
}

void NoteMapEditor::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0d0b0e));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    auto title = getLocalBounds().reduced (6, 4).removeFromTop (22).toFloat();
    g.setColour (noteBlue);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("NOTE MAP", title.removeFromLeft (78.0f), juce::Justification::centredLeft, false);

    // The row labels.
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("PLAYS", juce::Rectangle<float> (rows.getX(), rows.getY(), labelWidth, rows.getHeight() / 2.0f), juce::Justification::centredLeft, false);
    g.drawText ("SCALE", juce::Rectangle<float> (rows.getX(), rows.getCentreY(), labelWidth, rows.getHeight() / 2.0f), juce::Justification::centredLeft, false);

    const auto custom = customScale();
    const auto currentScale = scale();
    const auto currentKey = key();
    const auto editable = currentScale == Scales::Scale::custom;
    for (int noteClass = 0; noteClass < 12; ++noteClass)
    {
        // PLAYS: the picked note yellow; a dot if it plays anything.
        auto box = boxBounds (0, noteClass).reduced (1.0f);
        g.setColour (noteClass == selected ? picked : juce::Colour (0xff1c181f));
        g.fillRoundedRectangle (box, 2.0f);
        g.setColour (noteClass == selected ? juce::Colour (0xff1a1408) : ParagonColours::text);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (noteNames[noteClass], box, juce::Justification::centred, false);
        if (mapOf (noteClass) != 0 && noteClass != selected)
        {
            g.setColour (assigned);
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ box.getCentreX(), box.getBottom() - 2.5f }));
        }

        // SCALE: in, or which way it goes.
        box = boxBounds (1, noteClass).reduced (1.0f);
        const auto in = Scales::isIn (noteClass, currentKey, currentScale, &custom);
        auto mark = juce::String();
        if (! in)
        {
            const auto to = Scales::nearest (noteClass + 60, currentKey, currentScale, &custom) - 60;
            mark = to > noteClass ? ">" : to < noteClass ? "<" : "";
        }
        g.setColour (juce::Colour (0xff1c181f).withAlpha (editable ? 1.0f : 0.5f));
        g.fillRoundedRectangle (box, 2.0f);
        g.setColour ((in ? ParagonColours::textDim : noteBlue).withAlpha (editable ? 1.0f : 0.6f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (in ? juce::String ("-") : mark, box, juce::Justification::centred, false);
    }

    // The keys: what the picked note plays (purple dots) and what sounds (blue rings).
    const auto bits = mapOf (selected);
    std::array<bool, 128> plays {}, sounds {};
    for (int bit = 0; bit < Harmony::mapBits; ++bit)
        if ((bits >> bit) & 1u)
        {
            const auto note = reference + selected + bit - Harmony::mapSpan;
            plays[(size_t) note] = true;
            sounds[(size_t) juce::jlimit (0, 127, Scales::nearest (note, currentKey, currentScale, &custom))] = true;
        }
    for (int pass = 0; pass < 2; ++pass)
        for (int note = lowest; note <= highest; ++note)
        {
            if (isBlack (note) != (pass == 1))
                continue;
            const auto bounds = keyBounds (note).reduced (pass == 0 ? 0.5f : 0.0f, 0.0f);
            auto colour = pass == 0 ? juce::Colour (0xffe6e2e8) : juce::Colour (0xff1a171c);
            if (note == reference + selected)
                colour = picked;
            if (shownHeld[(size_t) note])
                colour = held;
            g.setColour (colour);
            g.fillRect (bounds);
            g.setColour (juce::Colour (0xff0d0b0e));
            g.drawRect (bounds, 0.6f);
            const auto spot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getCentreX(), bounds.getBottom() - 8.0f });
            if (plays[(size_t) note])
            {
                g.setColour (assigned);
                g.fillEllipse (spot);
            }
            if (sounds[(size_t) note])
            {
                g.setColour (noteBlue);
                g.drawEllipse (spot.expanded (2.0f), 1.6f);
            }
            if (note % 12 == 0)
            {
                g.setColour (juce::Colour (0xff6b6569));
                g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
                g.drawText ("C" + juce::String (note / 12 - 1), bounds.withTrimmedTop (bounds.getHeight() - 22.0f), juce::Justification::centredTop, false);
            }
        }

    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (editable ? "Pick a note in PLAYS, then click keys to add what it plays. SCALE boxes: click for In, > or <."
                         : "Pick a note in PLAYS, then click keys to add what it plays. (Set SCALE to Custom to edit the SCALE boxes.)",
                getLocalBounds().reduced (6, 4).removeFromBottom (14).toFloat(), juce::Justification::centredLeft, true);
}
