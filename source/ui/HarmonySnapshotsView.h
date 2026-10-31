#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "rigs/Rig.h"

// HARMONY's SNAPSHOTS page (stage 8), inspired by Waves Harmony's snapshot
// panel: the 32 slots, each showing the voices it holds ("+4 +7"), the one
// in use lit. Opened by SNAPSHOTS on the HARMONY graph, over the graph and
// the controls.
//
//   click a slot     pick it; a stored one is recalled at once
//   STORE            the harmony voices as they are now, into the picked slot
//   COPY / PASTE     a slot's snapshot to another
//   DELETE           empties the picked slot
//   right-click      the same, for that slot
//
// It works through `Actions` (the processor's snapshot functions), so it
// holds no snapshots of its own, only the one copied.
class HarmonySnapshotsView final : public juce::Component,
                                   private juce::Timer
{
public:
    struct Actions
    {
        std::function<std::optional<Rig::HarmonySnapshot> (int)> get;
        std::function<void (int)> store, recall;
        std::function<void (int, std::optional<Rig::HarmonySnapshot>)> set;
        std::function<int()> recalled;   // 0 none, else slot + 1
    };

    HarmonySnapshotsView();
    ~HarmonySnapshotsView() override;

    void setActions (Actions);
    bool hasActions() const noexcept { return actions.get != nullptr; }
    std::function<void()> onDone;

    // Public for tests and the panel.
    void pick (int slot);               // and recall it, if it's stored
    int getPicked() const noexcept { return picked; }
    void storeHere();
    void copyPicked();
    void pastePicked();
    void deletePicked();
    // What a slot shows: its voices' intervals ("+4 +7"; a flattened voice's note).
    static juce::String summary (const Rig::HarmonySnapshot&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> slotBounds (int slot) const;
    int slotAt (juce::Point<float>) const;

    Actions actions;
    int picked = -1;
    std::optional<Rig::HarmonySnapshot> copied;
    juce::TextButton store { "STORE" }, copy { "COPY" }, paste { "PASTE" }, erase { "DELETE" }, done { "DONE" };
    juce::Rectangle<float> grid;
};
