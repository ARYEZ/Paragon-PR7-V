#include "HarmonySnapshotsView.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "dsp/PitchDetector.h"
#include "dsp/Scales.h"
#include "dsp/effects/Harmony.h"

namespace
{
    constexpr int columns = 8, rows = Rig::numHarmonySnapshots / columns;

    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    }
}

HarmonySnapshotsView::HarmonySnapshotsView()
{
    for (auto* button : { &store, &copy, &paste, &erase, &done })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    store.setTooltip ("Store the harmony voices as they are now in the picked slot");
    copy.setTooltip ("Copy the picked slot's snapshot");
    paste.setTooltip ("Paste the copied snapshot into the picked slot");
    erase.setTooltip ("Empty the picked slot");
    store.onClick = [this] { storeHere(); };
    copy.onClick = [this] { copyPicked(); };
    paste.onClick = [this] { pastePicked(); };
    erase.onClick = [this] { deletePicked(); };
    done.onClick = [this] { if (onDone != nullptr) onDone(); };
    startTimerHz (10);   // the one in use can change from the DAW
}

HarmonySnapshotsView::~HarmonySnapshotsView() = default;

void HarmonySnapshotsView::setActions (Actions newActions)
{
    actions = std::move (newActions);
    repaint();
}

juce::String HarmonySnapshotsView::summary (const Rig::HarmonySnapshot& snapshot)
{
    const auto value = [&snapshot] (const juce::String& id, float fallback)
    {
        const auto found = snapshot.find (id);
        return found != snapshot.end() ? found->second : fallback;
    };
    juce::StringArray voices;
    for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
    {
        if (value (ParamIDs::harmonyVoice (voice, "Active"), 0.0f) <= 0.5f)
            continue;
        if (value (ParamIDs::harmonyVoice (voice, "Flatten"), 0.0f) > 0.5f)
        {
            const auto named = PitchDetector::toNote (Scales::hzFromMidi (value (ParamIDs::harmonyVoice (voice, "Note"), effects::Harmony::flattenDefault)));
            voices.add (named.name + juce::String (named.octave));
            continue;
        }
        const auto semitones = juce::roundToInt (value (ParamIDs::harmonyVoice (voice, "Pitch"), 0.0f));
        voices.add (semitones == 0 ? juce::String ("U") : (semitones > 0 ? "+" : "") + juce::String (semitones));
    }
    return voices.isEmpty() ? juce::String ("NO VOICES") : voices.joinIntoString (" ");
}

void HarmonySnapshotsView::pick (int slot)
{
    picked = juce::jlimit (0, Rig::numHarmonySnapshots - 1, slot);
    if (actions.get != nullptr && actions.get (picked).has_value() && actions.recall != nullptr)
        actions.recall (picked);
    repaint();
}

void HarmonySnapshotsView::storeHere()
{
    if (actions.store == nullptr)
        return;
    if (picked < 0)
    {
        // Nothing picked: the first empty slot.
        picked = 0;
        for (int slot = 0; slot < Rig::numHarmonySnapshots; ++slot)
            if (! actions.get (slot).has_value())
            {
                picked = slot;
                break;
            }
    }
    actions.store (picked);
    repaint();
}

void HarmonySnapshotsView::copyPicked()
{
    if (actions.get != nullptr && picked >= 0)
        copied = actions.get (picked);
}

void HarmonySnapshotsView::pastePicked()
{
    if (actions.set != nullptr && picked >= 0 && copied.has_value())
    {
        actions.set (picked, copied);
        repaint();
    }
}

void HarmonySnapshotsView::deletePicked()
{
    if (actions.set != nullptr && picked >= 0)
    {
        actions.set (picked, std::nullopt);
        repaint();
    }
}

//==============================================================================
void HarmonySnapshotsView::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    auto title = area.removeFromTop (22);
    title.removeFromLeft (90);
    done.setBounds (title.removeFromRight (52));
    for (auto* button : { &erase, &paste, &copy, &store })
    {
        title.removeFromRight (4);
        button->setBounds (title.removeFromRight (button == &erase ? 56 : 50));
    }
    area.removeFromTop (6);
    area.removeFromBottom (14);   // the help line
    grid = area.toFloat();
}

juce::Rectangle<float> HarmonySnapshotsView::slotBounds (int slot) const
{
    const auto width = grid.getWidth() / (float) columns, height = grid.getHeight() / (float) rows;
    return juce::Rectangle<float> (grid.getX() + width * (float) (slot % columns), grid.getY() + height * (float) (slot / columns), width, height).reduced (2.0f);
}

int HarmonySnapshotsView::slotAt (juce::Point<float> point) const
{
    for (int slot = 0; slot < Rig::numHarmonySnapshots; ++slot)
        if (slotBounds (slot).contains (point))
            return slot;
    return -1;
}

void HarmonySnapshotsView::mouseDown (const juce::MouseEvent& event)
{
    const auto slot = slotAt (event.position);
    if (slot < 0 || actions.get == nullptr)
        return;
    if (! event.mods.isPopupMenu())
    {
        pick (slot);
        return;
    }
    // Right-click: the same, for this slot.
    picked = slot;
    repaint();
    const auto stored = actions.get (slot).has_value();
    juce::PopupMenu menu;
    menu.addItem (1, "Store the voices here");
    menu.addItem (2, "Recall", stored);
    menu.addItem (3, "Copy", stored);
    menu.addItem (4, "Paste", copied.has_value());
    menu.addItem (5, "Delete", stored);
    juce::Component::SafePointer<HarmonySnapshotsView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotBounds (slot).toNearestInt())), [safe] (int chosen)
    {
        if (safe == nullptr)
            return;
        switch (chosen)
        {
            case 1: safe->storeHere(); break;
            case 2: safe->pick (safe->picked); break;
            case 3: safe->copyPicked(); break;
            case 4: safe->pastePicked(); break;
            case 5: safe->deletePicked(); break;
            default: break;
        }
    });
}

void HarmonySnapshotsView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0d0b0e));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    auto title = getLocalBounds().reduced (6, 4).removeFromTop (22).toFloat();
    g.setColour (ParagonColours::accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("SNAPSHOTS", title.removeFromLeft (90.0f), juce::Justification::centredLeft, false);

    const auto recalled = actions.recalled != nullptr ? actions.recalled() - 1 : -1;
    for (int slot = 0; slot < Rig::numHarmonySnapshots; ++slot)
    {
        const auto box = slotBounds (slot);
        const auto snapshot = actions.get != nullptr ? actions.get (slot) : std::nullopt;
        const auto inUse = slot == recalled && snapshot.has_value();
        g.setColour (inUse ? ParagonColours::accent.withAlpha (0.22f) : snapshot.has_value() ? juce::Colour (0xff1c181e) : juce::Colour (0xff110f12));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (slot == picked ? ParagonColours::accent : inUse ? ParagonColours::accent.withAlpha (0.6f) : ParagonColours::panelEdge);
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, slot == picked ? 1.6f : 1.0f);

        auto inside = box.reduced (4.0f, 2.0f);
        g.setColour (snapshot.has_value() ? ParagonColours::text : ParagonColours::textDim.withAlpha (0.6f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (juce::String (slot + 1), inside.removeFromTop (11.0f), juce::Justification::topLeft, false);
        if (snapshot.has_value())
        {
            g.setColour (inUse ? ParagonColours::accent : ParagonColours::text);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawFittedText (summary (*snapshot), inside.toNearestInt(), juce::Justification::centred, 2, 0.75f);
        }
    }

    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText ("Click a slot to recall it. STORE saves the voices into the picked slot. Right-click for more.",
                getLocalBounds().reduced (8, 4).removeFromBottom (12), juce::Justification::centredLeft, false);
}
