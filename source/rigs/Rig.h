#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <map>
#include <optional>

// A rig: one complete sound. Every effect setting, and how it's filed
// (name, bank, number, tags).
//
// The INPUT, LOW CUT and OUTPUT knobs at the sides aren't part of a rig: they
// suit the singer's mic and monitoring, not a particular sound, so switching
// rigs leaves them alone (as on hardware processors). Each rig has its own
// RIG LEVEL for balancing rigs against each other instead.
struct Rig
{
    static constexpr int numBanks = 6;          // A to F (factory: everyday, rock, delays, spaces, character, signature styles)
    static constexpr int maxNumber = 99;

    juce::String name { "New Rig" };
    int bank = 0;                               // 0 = A
    int number = 1;                             // 1 to maxNumber
    bool favourite = false;
    juce::StringArray tags;

    // Parameter values in their real units (dB, ms...), by parameter ID.
    // Storing real values rather than 0-1 positions means a rig still
    // loads correctly if a later version changes a knob's range. Loading a
    // rig resets any parameter it doesn't mention to its default, so rigs
    // saved before a parameter existed get that parameter's neutral setting.
    std::map<juce::String, float> parameters;

    // HARMONY's SNAPSHOTS (stage 8, inspired by Waves Harmony's): up to 32
    // stored sets of the harmony voices' settings (ParamIDs::harmonySnapshotIds,
    // real values by ID), recalled with a click or the SNAPSHOT parameter.
    // Saved in the rig (and the project) beside the parameters; older
    // plugins ignore them.
    static constexpr int numHarmonySnapshots = 32;
    using HarmonySnapshot = std::map<juce::String, float>;
    using HarmonySnapshots = std::array<std::optional<HarmonySnapshot>, numHarmonySnapshots>;
    HarmonySnapshots harmonySnapshots;
    static void writeSnapshots (const HarmonySnapshots&, juce::XmlElement& parent);
    static HarmonySnapshots readSnapshots (const juce::XmlElement& parent);

    // HARMONY's modulation slots (stage 9): for each control a modulator
    // moves (ParamIDs::harmonyModTargets), up to four modulators, each at
    // its own depth. Saved beside the parameters (only the controls that
    // have one); older plugins ignore them.
    struct HarmonyModSlot
    {
        int source = 0;      // effects::HarmonyModulation::Source (0: none)
        float depth = 0.0f;  // -1 to 1
        bool operator== (const HarmonyModSlot& other) const { return source == other.source && juce::exactlyEqual (depth, other.depth); }
    };
    using HarmonyModSlots = std::map<juce::String, std::array<HarmonyModSlot, 4>>;
    HarmonyModSlots harmonyModSlots;
    static void writeModSlots (const HarmonyModSlots&, juce::XmlElement& parent);
    static HarmonyModSlots readModSlots (const juce::XmlElement& parent);

    juce::String getSlotName() const;           // e.g. "A01"
    static juce::String bankName (int bank);    // "A".."F"

    std::unique_ptr<juce::XmlElement> toXml() const;
    // Returns nothing if the XML isn't a rig.
    static std::optional<Rig> fromXml (const juce::XmlElement&);

    static constexpr const char* fileExtension = ".pr7rig";
    // From when the plugin was called Paragon X1: still read, never written.
    static constexpr const char* oldFileExtension = ".x1rig";
};
