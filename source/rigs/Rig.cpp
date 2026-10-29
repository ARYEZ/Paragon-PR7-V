#include "Rig.h"

#include "Parameters.h"

namespace
{
    // Bump when the file layout changes in a way older code can't read.
    constexpr int fileVersion = 1;

    // Which plugin version's parameters the rig was saved with, for the
    // occasional parameter whose default changed (older plugins ignore it).
    // 10: the drive's MODEL, defaulting to the Tube Screamer model.
    // 15: the reverb's TYPE, defaulting to HALL.
    // 26: the DOUBLER stomp, until then MODULATION's Doubler type.
    // 30: the four-voice DOUBLER, replacing its RATE, DEPTH and MIX.
    // 37: CYCLONE, replacing MODULATION's original rotary speaker.
    constexpr int parameterVersion = 37;

    const juce::Identifier rigTag { "ParagonRig" };
    const juce::Identifier parameterTag { "Param" };
    const juce::Identifier snapshotTag { "HarmonySnapshot" };
    const juce::Identifier modSlotTag { "HarmonyMod" };
    const juce::Identifier modShapeTag { "HarmonyShape" };
}

void Rig::writeSnapshots (const HarmonySnapshots& snapshots, juce::XmlElement& parent)
{
    for (int i = 0; i < numHarmonySnapshots; ++i)
    {
        const auto& snapshot = snapshots[(size_t) i];
        if (! snapshot.has_value())
            continue;
        auto* element = parent.createNewChildElement (snapshotTag);
        element->setAttribute ("slot", i + 1);
        for (const auto& [id, value] : *snapshot)
        {
            auto* parameter = element->createNewChildElement (parameterTag);
            parameter->setAttribute ("id", id);
            parameter->setAttribute ("value", value);
        }
    }
}

Rig::HarmonySnapshots Rig::readSnapshots (const juce::XmlElement& parent)
{
    HarmonySnapshots snapshots;
    for (const auto* element : parent.getChildWithTagNameIterator (snapshotTag))
    {
        const auto slot = element->getIntAttribute ("slot", 0) - 1;
        if (slot < 0 || slot >= numHarmonySnapshots)
            continue;
        HarmonySnapshot snapshot;
        for (const auto* parameter : element->getChildWithTagNameIterator (parameterTag))
            if (const auto id = parameter->getStringAttribute ("id"); id.isNotEmpty())
                snapshot[id] = (float) parameter->getDoubleAttribute ("value");
        snapshots[(size_t) slot] = std::move (snapshot);
    }
    return snapshots;
}

void Rig::writeModSlots (const HarmonyModSlots& slots, juce::XmlElement& parent)
{
    for (const auto& [target, four] : slots)
        for (size_t slot = 0; slot < four.size(); ++slot)
            if (four[slot].source != 0)
            {
                auto* element = parent.createNewChildElement (modSlotTag);
                element->setAttribute ("target", target);
                element->setAttribute ("slot", (int) slot + 1);
                element->setAttribute ("source", four[slot].source);
                element->setAttribute ("depth", four[slot].depth);
            }
}

Rig::HarmonyModSlots Rig::readModSlots (const juce::XmlElement& parent)
{
    HarmonyModSlots slots;
    for (const auto* element : parent.getChildWithTagNameIterator (modSlotTag))
    {
        const auto target = element->getStringAttribute ("target");
        const auto slot = element->getIntAttribute ("slot", 0) - 1;
        const auto source = element->getIntAttribute ("source", 0);
        if (target.isEmpty() || slot < 0 || slot >= 4 || source <= 0)
            continue;
        slots[target][(size_t) slot] = { source, juce::jlimit (-1.0f, 1.0f, (float) element->getDoubleAttribute ("depth")) };
    }
    return slots;
}

void Rig::writeModShapes (const HarmonyModShapes& shapes, juce::XmlElement& parent)
{
    for (size_t mod = 0; mod < shapes.size(); ++mod)
    {
        if (std::all_of (shapes[mod].begin(), shapes[mod].end(), [] (float point) { return juce::exactlyEqual (point, 0.0f); }))
            continue;
        juce::StringArray points;
        for (auto point : shapes[mod])
            points.add (juce::String (point, 4));   // the points are kept to 4 decimals (setHarmonyModShape), so this is exact
        auto* element = parent.createNewChildElement (modShapeTag);
        element->setAttribute ("mod", (int) mod + 1);
        element->setAttribute ("points", points.joinIntoString (" "));
    }
}

Rig::HarmonyModShapes Rig::readModShapes (const juce::XmlElement& parent)
{
    HarmonyModShapes shapes {};
    for (const auto* element : parent.getChildWithTagNameIterator (modShapeTag))
    {
        const auto mod = element->getIntAttribute ("mod", 0) - 1;
        if (mod < 0 || mod >= (int) shapes.size())
            continue;
        const auto points = juce::StringArray::fromTokens (element->getStringAttribute ("points"), " ", {});
        for (int i = 0; i < juce::jmin (points.size(), (int) shapes[(size_t) mod].size()); ++i)
            shapes[(size_t) mod][(size_t) i] = juce::jlimit (-1.0f, 1.0f, points[i].getFloatValue());
    }
    return shapes;
}

juce::String Rig::bankName (int bankIndex)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, numBanks - 1, bankIndex)));
}

juce::String Rig::getSlotName() const
{
    return bankName (bank) + juce::String (number).paddedLeft ('0', 2);
}

std::unique_ptr<juce::XmlElement> Rig::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (rigTag);
    xml->setAttribute ("version", fileVersion);
    xml->setAttribute ("parameterVersion", parameterVersion);
    xml->setAttribute ("name", name);
    xml->setAttribute ("bank", bankName (bank));
    xml->setAttribute ("number", number);
    xml->setAttribute ("favourite", favourite);
    xml->setAttribute ("tags", tags.joinIntoString (","));

    for (const auto& [id, value] : parameters)
    {
        auto* parameter = xml->createNewChildElement (parameterTag);
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }
    writeSnapshots (harmonySnapshots, *xml);
    writeModSlots (harmonyModSlots, *xml);
    writeModShapes (harmonyModShapes, *xml);
    return xml;
}

std::optional<Rig> Rig::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (rigTag) || xml.getIntAttribute ("version", 0) > fileVersion)
        return std::nullopt;

    Rig rig;
    rig.name = xml.getStringAttribute ("name", "Untitled").trim();
    if (rig.name.isEmpty())
        rig.name = "Untitled";

    const auto bankLetter = xml.getStringAttribute ("bank", "A").toUpperCase();
    rig.bank = bankLetter.isEmpty() ? 0 : juce::jlimit (0, numBanks - 1, (int) (bankLetter[0] - 'A'));
    rig.number = juce::jlimit (1, maxNumber, xml.getIntAttribute ("number", 1));
    rig.favourite = xml.getBoolAttribute ("favourite");
    rig.tags = juce::StringArray::fromTokens (xml.getStringAttribute ("tags"), ",", {});
    rig.tags.trim();
    rig.tags.removeEmptyStrings();
    // Rigs from before 0.3.0 may name a NAM model and IR ("ampModel",
    // "cabIr"): PR7-V has no amp or cabinet, so they're ignored.

    for (const auto* parameter : xml.getChildWithTagNameIterator (parameterTag))
        if (const auto id = parameter->getStringAttribute ("id"); id.isNotEmpty())
            rig.parameters[id] = (float) parameter->getDoubleAttribute ("value");
    rig.harmonySnapshots = readSnapshots (xml);
    rig.harmonyModSlots = readModSlots (xml);
    rig.harmonyModShapes = readModShapes (xml);

    // Saved before the drive had a MODEL: it was the original overdrive,
    // model 0. Without this it would load as the newer default.
    const auto savedWith = xml.getIntAttribute ("parameterVersion", 0);
    if (savedWith < 10 && rig.parameters.count ("driveModel") == 0)
        rig.parameters["driveModel"] = 0.0f;
    // Saved before the reverb had a TYPE: it was the original, now CLASSIC (0).
    if (savedWith < 15 && rig.parameters.count ("reverbType") == 0)
        rig.parameters["reverbType"] = 0.0f;
    const auto get = [&rig] (const juce::String& id) -> std::optional<float>
    {
        const auto found = rig.parameters.find (id);
        return found != rig.parameters.end() ? std::optional<float> (found->second) : std::nullopt;
    };
    const auto set = [&rig] (const juce::String& id, float value) { rig.parameters[id] = value; };
    // Saved with MODULATION on its old Doubler type: now the DOUBLER stomp...
    if (savedWith < 26)
        moveOldDoublerSettings ([&get] (const char* id) { return get (id); }, [&set] (const char* id, float value) { set (id, value); });
    // ...and the two-take DOUBLER: now its four voices.
    if (savedWith < 30)
    {
        convertOldDoubler (get, set);
        for (const auto* gone : { ParamIDs::doublerRate, ParamIDs::doublerDepth, ParamIDs::doublerMix })
            rig.parameters.erase (gone);
    }
    // ...and 0.7.0's DOUBLER, before DRIFT and its stronger start.
    else if (savedWith < 32)
        keepDoublerAsSaved (get, set);
    // Saved with a pedal drive model: now MARS, set up to sound close to it.
    if (savedWith < 36)
    {
        convertOldDrive (get, set);
        for (const auto* gone : { ParamIDs::driveModel, ParamIDs::driveGain, ParamIDs::driveTone, ParamIDs::driveLevel })
            rig.parameters.erase (gone);
    }
    // Saved with the original rotary speaker: now CYCLONE, set to match.
    if (savedWith < 37)
        convertOldRotary (get, set);

    return rig;
}
