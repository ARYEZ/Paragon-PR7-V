#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/FactoryRigs.h"
#include "ui/EqPresets.h"

using Catch::Matchers::WithinAbs;

namespace
{
    // A fresh, empty rigs folder for each test.
    struct TempRigFolder
    {
        TempRigFolder() { folder.deleteRecursively(); }
        ~TempRigFolder() { folder.deleteRecursively(); }
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1RigTests")
                                .getChildFile (juce::Uuid().toString());
    };

    struct RigRig   // a processor with its rigs in a temporary folder
    {
        RigRig()
        {
            processor.setPlayConfigDetails (1, 2, 48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            processor.useRigFolder (temp.folder);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        float get (const char* id) { return processor.getParameters().getRawParameterValue (id)->load(); }

        const RigLibrary::Entry& entryNamed (const juce::String& name)
        {
            for (const auto& entry : processor.getRigLibrary().getEntries())
                if (entry.rig.name == name)
                    return entry;
            FAIL ("No rig called " << name);
            return processor.getRigLibrary().getEntries().front();
        }

        TempRigFolder temp;
        ParagonX1AudioProcessor processor;
    };
}

TEST_CASE ("A rig survives being written to a file and read back", "[rigs]")
{
    Rig rig;
    rig.name = "AC/DC: \"Back in Black\" & <more>";
    rig.bank = 2;
    rig.number = 17;
    rig.favourite = true;
    rig.tags = { "Rock", "High Gain" };
    rig.parameters = { { "driveGain", 6.5f }, { "delayTime", 420.0f } };

    const auto restored = Rig::fromXml (*juce::XmlDocument::parse (rig.toXml()->toString()));
    REQUIRE (restored.has_value());
    CHECK (restored->name == rig.name);
    CHECK (restored->getSlotName() == "C17");
    CHECK (restored->favourite);
    CHECK (restored->tags == rig.tags);
    CHECK (restored->parameters == rig.parameters);
}

TEST_CASE ("Rig files from before 0.3.0 load: the amp and cab are ignored, the Doubler moves to its stomp", "[rigs]")
{
    // As 0.2.0 wrote it: a NAM model and IR, and MODULATION on its Doubler type (10).
    const auto old = Rig::fromXml (*juce::XmlDocument::parse (
        "<ParagonRig version=\"1\" parameterVersion=\"15\" name=\"Old Double\" bank=\"A\" number=\"3\""
        "            ampModel=\"C:\\Models\\Plexi.nam\" cabIr=\"C:\\IRs\\V30.wav\" keepAmpAndCab=\"0\">"
        "  <Param id=\"modOn\" value=\"1\"/><Param id=\"modType\" value=\"10\"/>"
        "  <Param id=\"modRate\" value=\"1.2\"/><Param id=\"modDepth\" value=\"0.25\"/><Param id=\"modMix\" value=\"0.65\"/>"
        "  <Param id=\"reverbOn\" value=\"1\"/>"
        "</ParagonRig>"));
    REQUIRE (old.has_value());
    CHECK (old->name == "Old Double");
    const auto& p = old->parameters;
    CHECK (p.at (ParamIDs::doublerOn) > 0.5f);
    // ...and on to 0.7.0's voices: the old takes, at MIX's level (65%), RATE/4.
    CHECK (p.at (ParamIDs::doublerVoice (0, "On")) > 0.5f);
    CHECK_THAT (p.at (ParamIDs::doublerVoice (0, "Delay")), WithinAbs (19.0, 0.001));
    CHECK_THAT (p.at (ParamIDs::doublerVoice (1, "Delay")), WithinAbs (27.0, 0.001));
    CHECK_THAT (p.at (ParamIDs::doublerVoice (0, "Gain")), WithinAbs (-3.74, 0.05));
    CHECK_THAT (p.at (ParamIDs::doublerVoice (0, "Rate")), WithinAbs (0.3, 0.001));
    CHECK (p.count (ParamIDs::doublerMix) == 0);   // the old knobs themselves are gone
    CHECK (p.at (ParamIDs::modOn) < 0.5f);
    CHECK (p.at (ParamIDs::reverbOn) > 0.5f);   // everything else as it was

    // Saved again, it's a current rig: no amp or cab, and not converted twice.
    const auto resaved = old->toXml();
    CHECK_FALSE (resaved->hasAttribute ("ampModel"));
    CHECK (resaved->getIntAttribute ("parameterVersion") == 30);
}

TEST_CASE ("Files that aren't rigs, or are from a newer version, are refused", "[rigs]")
{
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<SomethingElse/>")).has_value());
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<ParagonRig version=\"99\" name=\"x\"/>")).has_value());
}

TEST_CASE ("Every factory rig is valid", "[rigs]")
{
    ParagonX1AudioProcessor processor;
    juce::StringArray names, slots;

    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        names.addIfNotAlreadyThere (rig.name);
        slots.addIfNotAlreadyThere (rig.getSlotName());

        for (const auto& [id, value] : rig.parameters)
        {
            INFO (id);
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
            REQUIRE (parameter != nullptr);                          // no typos in parameter IDs
            CHECK_FALSE (ParamIDs::notInRigs.contains (id));        // rigs don't touch global settings
            const auto range = parameter->getNormalisableRange();
            CHECK (value >= range.start);
            CHECK (value <= range.end);
        }
    }

    CHECK (names.size() == (int) makeFactoryRigs().size());   // unique names
    CHECK (slots.size() == (int) makeFactoryRigs().size());   // no two in the same slot
}

TEST_CASE ("The Whammy-Wah factory rigs use the XP-100 programs they're named for", "[rigs]")
{
    const auto rigs = makeFactoryRigs();
    const auto find = [&] (const juce::String& name) -> const Rig&
    {
        const auto found = std::find_if (rigs.begin(), rigs.end(), [&] (const Rig& rig) { return rig.name == name; });
        REQUIRE (found != rigs.end());
        return *found;
    };
    const auto programOf = [] (const Rig& rig)
    {
        return effects::FilterPitch::xpProgramNames()[juce::roundToInt (rig.parameters.at (ParamIDs::fpXpProgram))];
    };

    CHECK (programOf (find ("Nu-Metal Screech")) == "21 Down Oct / Up Oct");
    CHECK (programOf (find ("Whammy Lead")) == "13 Up 1 Octave");
    CHECK (programOf (find ("XP Wah Crunch")) == "02 Original Cry Wah");
    CHECK (programOf (find ("Harmony Bends")) == "24 Up 4th / Up 5th");

    // The screech's EQ is the Tight Low End preset (bands it leaves out are at 0 dB).
    const auto& screech = find ("Nu-Metal Screech");
    CHECK (juce::roundToInt (screech.parameters.at (ParamIDs::expressionTarget)) == ExpressionTargets::whammyWah);
    const auto& tight = *std::find_if (EqPresets::all.begin(), EqPresets::all.end(),
                                       [] (const auto& preset) { return juce::String (preset.name) == "Tight Low End"; });
    for (int band = 0; band < ParamIDs::eqBands.size(); ++band)
    {
        const auto found = screech.parameters.find (ParamIDs::eqBands[band]);
        CHECK_THAT (found != screech.parameters.end() ? found->second : 0.0f, Catch::Matchers::WithinAbs (tight.gainsDb[(size_t) band], 0.01));
    }
    CHECK_THAT (screech.parameters.at (ParamIDs::eqLevel), Catch::Matchers::WithinAbs (tight.levelDb, 0.01));
}

TEST_CASE ("The rig library saves, finds, renames and deletes rigs", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);
    const auto factoryCount = library.getEntries().size();

    Rig rig;
    rig.name = "Cyber Drive";
    const auto first = library.saveAsNew (rig);
    REQUIRE (first.existsAsFile());
    REQUIRE (library.find (first) != nullptr);

    SECTION ("A taken name gets a number")
    {
        const auto second = library.saveAsNew (rig);
        REQUIRE (library.find (second) != nullptr);
        CHECK (library.find (second)->rig.name == "Cyber Drive 2");
        CHECK (second != first);
    }

    SECTION ("Names that can't be file names still save")
    {
        rig.name = "AC/DC: Live?";
        const auto file = library.saveAsNew (rig);
        REQUIRE (file.existsAsFile());
        CHECK (library.find (file)->rig.name == "AC/DC: Live?");
    }

    SECTION ("Overwrite and delete")
    {
        auto changed = library.find (first)->rig;
        changed.tags = { "Lead" };
        REQUIRE (library.overwrite (first, changed));
        CHECK (library.find (first)->rig.tags == juce::StringArray { "Lead" });

        REQUIRE (library.remove (first));
        CHECK (library.find (first) == nullptr);
        CHECK (library.getEntries().size() == factoryCount);
    }

    SECTION ("A corrupt rig file is skipped, not fatal")
    {
        temp.folder.getChildFile (juce::String ("broken") + Rig::fileExtension).replaceWithText ("<ParagonRig name=");
        library.rescan();
        CHECK (library.getEntries().size() == factoryCount + 1);
    }
}

TEST_CASE ("Favourites stick, including on factory rigs", "[rigs]")
{
    TempRigFolder temp;
    {
        RigLibrary library (temp.folder);
        library.setFavourite (*library.findFactory ("Slapback"), true);
    }

    RigLibrary reopened (temp.folder);
    CHECK (reopened.findFactory ("Slapback")->rig.favourite);
    CHECK_FALSE (reopened.findFactory ("Studio Clean")->rig.favourite);
}

TEST_CASE ("Rigs are sorted by bank and number, and < > wrap round", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);

    Rig late;
    late.name = "Late";
    late.bank = Rig::numBanks - 1;   // the last bank, after every factory rig
    late.number = 50;
    library.saveAsNew (late);

    const auto& entries = library.getEntries();
    for (size_t i = 1; i < entries.size(); ++i)
        CHECK (entries[i - 1].rig.bank * 100 + entries[i - 1].rig.number <= entries[i].rig.bank * 100 + entries[i].rig.number);

    CHECK (entries.back().rig.name == "Late");
    CHECK (library.step (&entries.back(), 1) == &entries.front());
    CHECK (library.step (&entries.front(), -1) == &entries.back());
    CHECK (library.step (nullptr, 1) == &entries.front());

    CHECK (library.nextFreeNumber (0) == 6);   // factory rigs fill A01-A05
}

TEST_CASE ("Saving and loading a rig restores the sound but not the global knobs", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::driveOn, 1.0f);
    r.set (ParamIDs::driveGain, 7.5f);
    r.set (ParamIDs::delayTime, 640.0f);
    r.set (ParamIDs::rigLevel, -3.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("My Sound"));

    // Change everything, including the global input and output knobs.
    r.set (ParamIDs::driveOn, 0.0f);
    r.set (ParamIDs::driveGain, 1.0f);
    r.set (ParamIDs::delayTime, 100.0f);
    r.set (ParamIDs::rigLevel, 0.0f);
    r.set (ParamIDs::inputGain, 5.0f);
    r.set (ParamIDs::outputGain, -4.0f);

    r.processor.loadRig (r.entryNamed ("My Sound"));

    CHECK (r.get (ParamIDs::driveOn) > 0.5f);
    CHECK_THAT (r.get (ParamIDs::driveGain), WithinAbs (7.5, 0.01));
    CHECK_THAT (r.get (ParamIDs::delayTime), WithinAbs (640.0, 0.5));
    CHECK_THAT (r.get (ParamIDs::rigLevel), WithinAbs (-3.0, 0.01));
    // Global knobs are left alone.
    CHECK_THAT (r.get (ParamIDs::inputGain), WithinAbs (5.0, 0.01));
    CHECK_THAT (r.get (ParamIDs::outputGain), WithinAbs (-4.0, 0.01));
}

TEST_CASE ("A rig that doesn't mention a parameter sets it to its default", "[rigs]")
{
    // E.g. a rig saved by an older version, before that parameter existed.
    RigRig r;
    r.set (ParamIDs::reverbOn, 1.0f);
    r.set (ParamIDs::presence, 6.0f);

    Rig old;
    old.name = "Old Rig";
    old.parameters = { { ParamIDs::driveOn, 1.0f } };
    r.processor.getRigLibrary().saveAsNew (old);
    r.processor.loadRig (r.entryNamed ("Old Rig"));

    CHECK (r.get (ParamIDs::driveOn) > 0.5f);
    CHECK (r.get (ParamIDs::reverbOn) < 0.5f);
    CHECK_THAT (r.get (ParamIDs::presence), WithinAbs (0.0, 0.01));
}

TEST_CASE ("The modified marker tracks changes since loading or saving", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Studio Clean"));
    CHECK_FALSE (r.processor.isRigModified());

    SECTION ("Turning a knob marks it modified")
    {
        r.set (ParamIDs::driveGain, 9.0f);
        CHECK (r.processor.isRigModified());
    }

    SECTION ("The global knobs don't count")
    {
        r.set (ParamIDs::inputGain, 3.0f);
        r.set (ParamIDs::inputLowCut, 150.0f);
        r.set (ParamIDs::outputGain, -3.0f);
        CHECK_FALSE (r.processor.isRigModified());
    }

    SECTION ("Saving clears it")
    {
        r.set (ParamIDs::driveGain, 9.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Saved"));
        CHECK_FALSE (r.processor.isRigModified());
        r.set (ParamIDs::driveGain, 2.0f);
        CHECK (r.processor.isRigModified());
        REQUIRE (r.processor.saveCurrentRig());   // a user rig now, so plain save works
        CHECK_FALSE (r.processor.isRigModified());
    }
}

TEST_CASE ("Factory rigs can't be saved over", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Slapback"));
    r.set (ParamIDs::delayMix, 0.9f);
    CHECK_FALSE (r.processor.saveCurrentRig());   // the UI then asks for a new name
    CHECK_THAT (r.entryNamed ("Slapback").rig.parameters.at (ParamIDs::delayMix), WithinAbs (0.3, 0.001));
}

TEST_CASE ("< and > step through the rigs in order", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Clean Slate"));
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Studio Clean");
    r.processor.stepRig (-1);
    r.processor.stepRig (-1);   // wraps round to the last rig
    CHECK (r.processor.getCurrentRig().name == r.processor.getRigLibrary().getEntries().back().rig.name);
}

TEST_CASE ("Deleting the loaded rig keeps the sound, now unsaved", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::driveGain, 8.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("Temporary"));
    REQUIRE (r.processor.deleteRig (r.entryNamed ("Temporary")));

    CHECK_FALSE (r.processor.getCurrentRig().isUserRig());
    CHECK (r.processor.isRigModified());
    CHECK_THAT (r.get (ParamIDs::driveGain), WithinAbs (8.0, 0.01));
}

TEST_CASE ("The DAW project remembers which rig was loaded", "[rigs][state]")
{
    juce::MemoryBlock saved;
    juce::File rigFile;
    {
        RigRig r;
        r.set (ParamIDs::driveGain, 8.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Project Rig"));
        r.set (ParamIDs::driveGain, 3.0f);   // tweaked after saving
        rigFile = r.processor.getCurrentRig().file;
        r.processor.getStateInformation (saved);
    }

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getCurrentRig().name == "Project Rig");
    CHECK (restored.getCurrentRig().file == rigFile);
    CHECK (restored.isRigModified());   // the tweak is remembered as unsaved
    CHECK_THAT (restored.getParameters().getRawParameterValue (ParamIDs::driveGain)->load(), WithinAbs (3.0, 0.01));
}

TEST_CASE ("Each rig's RIG LEVEL adds to the master output", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::rigLevel, 6.0f);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 20; ++block)
    {
        buffer.clear();
        for (int i = 0; i < 256; ++i)
            buffer.setSample (0, i, 0.1f * std::sin (0.1f * (float) (block * 256 + i)));
        r.processor.processBlock (buffer, midi);
    }
    CHECK_THAT (buffer.getMagnitude (0, 0, 256), WithinAbs (0.1 * 1.9953, 0.005));
}

TEST_CASE ("Rigs live in their own Paragon PR7-V folder, apart from Paragon PR7-G's", "[rigs]")
{
    const auto folder = RigLibrary::defaultFolder();
    CHECK (folder.getFileName() == "Rigs");
    CHECK (folder.getParentDirectory().getFileName() == "Paragon PR7-V");
    CHECK (folder.getParentDirectory().getParentDirectory().getFileName() == "Paragon");

    // The guitar version's rigs folder sits next to it, and is a different folder.
    const auto guitarFolder = folder.getParentDirectory().getSiblingFile ("Paragon PR7-G").getChildFile ("Rigs");
    CHECK (folder != guitarFolder);
    CHECK_FALSE (folder.isAChildOf (guitarFolder.getParentDirectory()));
}

TEST_CASE ("Rig files from Paragon X1 (.x1rig) still load", "[rigs]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ParagonOldRigs-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    REQUIRE (folder.createDirectory());
    Rig old;
    old.name = "From X1";
    REQUIRE (old.toXml()->writeTo (folder.getChildFile ("From X1.x1rig")));

    RigLibrary library (folder);
    CHECK (std::any_of (library.getEntries().begin(), library.getEntries().end(),
                        [] (const auto& entry) { return entry.rig.name == "From X1"; }));
    folder.deleteRecursively();
}

TEST_CASE ("A rig file saved before reverb types loads with the original reverb", "[rigs]")
{
    auto xml = juce::XmlDocument::parse (R"(<ParagonRig version="1" parameterVersion="10" name="Old"><Param id="reverbOn" value="1"/></ParagonRig>)");
    REQUIRE (xml != nullptr);
    const auto rig = Rig::fromXml (*xml);
    REQUIRE (rig.has_value());
    REQUIRE (rig->parameters.count ("reverbType") == 1);
    CHECK (rig->parameters.at ("reverbType") == 0.0f);

    Rig current;   // saved by this version: written with its reverb type, and read back as it is
    current.parameters["reverbType"] = 4.0f;
    CHECK (Rig::fromXml (*current.toXml())->parameters.at ("reverbType") == 4.0f);
}
