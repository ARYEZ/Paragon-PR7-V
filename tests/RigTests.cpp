#include <map>
#include <iostream>
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
    rig.parameters = { { "lofiAmount", 6.5f }, { "delayTime", 420.0f } };

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
    CHECK (resaved->getIntAttribute ("parameterVersion") == 38);
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
        // HARMONY's modulators: real controls and sources, in range.
        const auto targets = ParamIDs::harmonyModTargets();
        for (const auto& [target, slots] : rig.harmonyModSlots)
        {
            INFO (target);
            CHECK (targets.contains (target));
            for (const auto& slot : slots)
            {
                CHECK (juce::isPositiveAndBelow (slot.source, (int) effects::HarmonyModulation::numSources));
                CHECK (std::abs (slot.depth) <= 1.0f);
            }
        }
    }

    CHECK (names.size() == (int) makeFactoryRigs().size());   // unique names
    CHECK (slots.size() == (int) makeFactoryRigs().size());   // no two in the same slot
}

TEST_CASE ("Every factory rig is for the voice, and plays at a sensible level", "[rigs]")
{
    // A sung phrase: a 220 Hz vowel with vibrato, about -20 dB, then a rest.
    constexpr double rate = 48000.0;
    std::vector<float> phrase ((size_t) (2.5 * rate), 0.0f);
    for (size_t i = 0; i < (size_t) (1.6 * rate); ++i)
    {
        const auto t = (double) i / rate;
        const auto f0 = 220.0 * (1.0 + 0.01 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
        {
            const auto shape = 1.0 / (1.0 + std::pow ((harmonic * 220.0 - 700.0) / 400.0, 2.0)) + 0.1;
            phrase[i] += (float) (0.025 * shape * std::sin (juce::MathConstants<double>::twoPi * f0 * harmonic * t));
        }
    }

    const auto rigs = makeFactoryRigs();
    REQUIRE (rigs.size() >= 30);
    std::vector<double> levels;
    for (const auto& factory : rigs)
    {
        INFO (factory.name);
        RigRig r;
        r.processor.loadRig (r.entryNamed (factory.name));
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        std::vector<float> left;
        auto peak = 0.0f;
        auto finite = true;
        for (size_t start = 0; start < phrase.size(); start += 256)
        {
            buffer.clear();
            const auto count = (int) std::min<size_t> (256, phrase.size() - start);
            buffer.setSize (2, count, false, false, true);
            std::copy (phrase.begin() + (long) start, phrase.begin() + (long) start + count, buffer.getWritePointer (0));
            r.processor.processBlock (buffer, midi);
            for (int i = 0; i < count; ++i)
            {
                finite = finite && std::isfinite (buffer.getSample (0, i)) && std::isfinite (buffer.getSample (1, i));
                peak = std::max ({ peak, std::abs (buffer.getSample (0, i)), std::abs (buffer.getSample (1, i)) });
                left.push_back (buffer.getSample (0, i));
            }
        }
        REQUIRE (finite);
        double sum = 0.0;
        for (auto i = (size_t) (0.4 * rate); i < (size_t) (1.6 * rate); ++i)
            sum += (double) left[i] * left[i];
        const auto levelDb = 10.0 * std::log10 (sum / (1.2 * rate) + 1.0e-12);
        levels.push_back (levelDb);
        CHECK (peak <= 1.0f);     // never clipping (the master LIMITER as the last word)
        CHECK (levelDb > -60.0);  // and not silent
    }

    // None stands out much louder or quieter than the rest, so switching
    // rigs doesn't make you jump.
    auto sorted = levels;
    std::sort (sorted.begin(), sorted.end());
    const auto median = sorted[sorted.size() / 2];
    for (size_t i = 0; i < rigs.size(); ++i)
    {
        UNSCOPED_INFO (rigs[i].name << ": " << levels[i] << " dB");
        if (rigs[i].name == "Clean Slate")
            continue;   // everything off: your own voice, before any compressor's make-up
        INFO (rigs[i].name << ": " << levels[i] << " dB, the median " << median << " dB");
        CHECK (std::abs (levels[i] - median) < 6.0);
        if (std::getenv ("PARAGON_RIG_LEVELS") != nullptr)
            std::cout << rigs[i].getSlotName() << " " << rigs[i].name << ": " << levels[i] - median << " dB from the median\n";
    }
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
    CHECK_FALSE (reopened.findFactory ("Studio Vocal")->rig.favourite);
}

TEST_CASE ("A favourite factory rig that was renamed keeps its star under the new name", "[rigs]")
{
    // Favourites are kept by name: one saved before E03 and E22 moved to the
    // DOUBLER (and were renamed) follows them.
    TempRigFolder temp;
    temp.folder.createDirectory();
    temp.folder.getChildFile ("factory-favourites.txt").replaceWithText ("Harmony Stack\nSlapback\nAuto Choir");
    RigLibrary library (temp.folder);
    REQUIRE (library.findFactory ("Vocal Stack") != nullptr);
    CHECK (library.findFactory ("Vocal Stack")->rig.favourite);
    CHECK (library.findFactory ("Voice Wall")->rig.favourite);
    CHECK (library.findFactory ("Slapback")->rig.favourite);
    CHECK (library.findFactory ("Harmony Stack") == nullptr);
    // ...and the file now says so.
    CHECK (temp.folder.getChildFile ("factory-favourites.txt").loadFileAsString().contains ("Vocal Stack"));
}

TEST_CASE ("Rigs moved off HARMONY onto the DOUBLER don't switch the HARMONY engine on", "[rigs]")
{
    // The owner: HARMONY still needs work, the DOUBLER sounds better.
    const juce::StringArray moved { "Vocal Stack", "Voice Wall", "Pop-Punk Chorus", "Haunted Clean", "Prog Tenor Stack", "Soaring Prog Chorus" };
    auto found = 0;
    for (const auto& factory : makeFactoryRigs())
    {
        if (! moved.contains (factory.name))
            continue;
        ++found;
        INFO (factory.name);
        for (const auto* id : { ParamIDs::harmonyOn, ParamIDs::harmonyGenerate, ParamIDs::harmonyCorrect, ParamIDs::formantOn })
        {
            const auto value = factory.parameters.find (id);
            CHECK ((value == factory.parameters.end() || value->second == 0.0f));
        }
        const auto doubler = factory.parameters.find (ParamIDs::doublerOn);
        CHECK ((doubler != factory.parameters.end() && doubler->second == 1.0f));
    }
    CHECK (found == moved.size());
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

    CHECK (library.nextFreeNumber (0) == 10);   // factory rigs fill A01-A09
}

TEST_CASE ("Saving and loading a rig restores the sound but not the global knobs", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::driveOn, 1.0f);
    r.set (ParamIDs::lofiAmount, 7.5f);
    r.set (ParamIDs::delayTime, 640.0f);
    r.set (ParamIDs::rigLevel, -3.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("My Sound"));

    // Change everything, including the global input and output knobs.
    r.set (ParamIDs::driveOn, 0.0f);
    r.set (ParamIDs::lofiAmount, 1.0f);
    r.set (ParamIDs::delayTime, 100.0f);
    r.set (ParamIDs::rigLevel, 0.0f);
    r.set (ParamIDs::inputGain, 5.0f);
    r.set (ParamIDs::outputGain, -4.0f);

    r.processor.loadRig (r.entryNamed ("My Sound"));

    CHECK (r.get (ParamIDs::driveOn) > 0.5f);
    CHECK_THAT (r.get (ParamIDs::lofiAmount), WithinAbs (7.5, 0.01));
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
    r.processor.loadRig (r.entryNamed ("Studio Vocal"));
    CHECK_FALSE (r.processor.isRigModified());

    SECTION ("Turning a knob marks it modified")
    {
        r.set (ParamIDs::lofiAmount, 9.0f);
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
        r.set (ParamIDs::lofiAmount, 9.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Saved"));
        CHECK_FALSE (r.processor.isRigModified());
        r.set (ParamIDs::lofiAmount, 2.0f);
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
    CHECK (r.processor.getCurrentRig().name == "Studio Vocal");
    r.processor.stepRig (-1);
    r.processor.stepRig (-1);   // wraps round to the last rig
    CHECK (r.processor.getCurrentRig().name == r.processor.getRigLibrary().getEntries().back().rig.name);
}

TEST_CASE ("Deleting the loaded rig keeps the sound, now unsaved", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::lofiAmount, 8.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("Temporary"));
    REQUIRE (r.processor.deleteRig (r.entryNamed ("Temporary")));

    CHECK_FALSE (r.processor.getCurrentRig().isUserRig());
    CHECK (r.processor.isRigModified());
    CHECK_THAT (r.get (ParamIDs::lofiAmount), WithinAbs (8.0, 0.01));
}

TEST_CASE ("The DAW project remembers which rig was loaded", "[rigs][state]")
{
    juce::MemoryBlock saved;
    juce::File rigFile;
    {
        RigRig r;
        r.set (ParamIDs::lofiAmount, 8.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Project Rig"));
        r.set (ParamIDs::lofiAmount, 3.0f);   // tweaked after saving
        rigFile = r.processor.getCurrentRig().file;
        r.processor.getStateInformation (saved);
    }

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getCurrentRig().name == "Project Rig");
    CHECK (restored.getCurrentRig().file == rigFile);
    CHECK (restored.isRigModified());   // the tweak is remembered as unsaved
    CHECK_THAT (restored.getParameters().getRawParameterValue (ParamIDs::lofiAmount)->load(), WithinAbs (3.0, 0.01));
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

TEST_CASE ("Rigs saved with 0.7.0's DOUBLER keep their sound: no DRIFT, its old starting settings", "[rigs]")
{
    // As 0.7.0 or 0.8.0 wrote it: voice 1's GAIN saved, voice 2's left at its start.
    const auto old = Rig::fromXml (*juce::XmlDocument::parse (
        "<ParagonRig version=\"1\" parameterVersion=\"30\" name=\"Doubled\" bank=\"A\" number=\"4\">"
        "  <Param id=\"doublerOn\" value=\"1\"/><Param id=\"dv1Gain\" value=\"-9\"/>"
        "</ParagonRig>"));
    REQUIRE (old.has_value());
    const auto& p = old->parameters;
    CHECK_THAT (p.at (ParamIDs::doublerVoice (0, "Gain")), WithinAbs (-9.0, 0.001));    // as saved
    CHECK_THAT (p.at (ParamIDs::doublerVoice (1, "Gain")), WithinAbs (-6.0, 0.001));    // 0.7.0's start, not 0.9.0's -2
    CHECK_THAT (p.at (ParamIDs::doublerVoice (0, "Delay")), WithinAbs (9.4, 0.001));
    CHECK_THAT (p.at (ParamIDs::doublerVoice (1, "Detune")), WithinAbs (4.0, 0.001));
    for (int voice = 0; voice < 4; ++voice)
        CHECK (p.at (ParamIDs::doublerVoice (voice, "Drift")) == 0.0f);

    // A rig saved now isn't touched.
    Rig current;
    current.parameters = { { "doublerOn", 1.0f } };
    const auto reloaded = Rig::fromXml (*juce::XmlDocument::parse (current.toXml()->toString()));
    REQUIRE (reloaded.has_value());
    CHECK (reloaded->parameters.count (ParamIDs::doublerVoice (0, "Drift")) == 0);
}

TEST_CASE ("Rigs saved with a pedal drive model load as MARS, the old knobs gone", "[rigs][mars]")
{
    // As 0.13.0 wrote it: a Face Fuzz with its DRIVE, TONE and LEVEL.
    const auto old = Rig::fromXml (*juce::XmlDocument::parse (
        "<ParagonRig version=\"1\" parameterVersion=\"35\" name=\"Fuzzy\" bank=\"B\" number=\"20\">"
        "  <Param id=\"driveOn\" value=\"1\"/><Param id=\"driveModel\" value=\"3\"/>"
        "  <Param id=\"driveGain\" value=\"10\"/><Param id=\"driveTone\" value=\"2\"/><Param id=\"driveLevel\" value=\"-3\"/>"
        "</ParagonRig>"));
    REQUIRE (old.has_value());
    const auto& p = old->parameters;
    CHECK (juce::roundToInt (p.at (ParamIDs::marsBand (0, "Style"))) == (int) effects::Mars::Style::brokenTube);
    CHECK_THAT (p.at (ParamIDs::marsBand (0, "Drive")), WithinAbs (0.85, 0.01));
    CHECK_THAT (p.at (ParamIDs::marsBand (0, "Treble")), WithinAbs (-6.0, 0.01));
    CHECK_THAT (p.at (ParamIDs::marsBand (0, "Level")), WithinAbs (-3.0, 0.01));
    CHECK (p.at (ParamIDs::driveOn) > 0.5f);
    for (const auto* gone : { "driveModel", "driveGain", "driveTone", "driveLevel" })
        CHECK (p.count (gone) == 0);
}

TEST_CASE ("Bank G's modulators change each rig's sound", "[rigs][harmony][modulators]")
{
    // A sung line that moves (A3, then E4 a fifth up), so the note-by-note
    // sources, PT and the envelopes all have something to follow.
    constexpr double rate = 48000.0;
    std::vector<float> line ((size_t) (3.0 * rate), 0.0f);
    for (size_t i = 0; i < (size_t) (2.6 * rate); ++i)
    {
        const auto t = (double) i / rate;
        const auto f0 = t < 1.3 ? 220.0 : 329.63;
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
        {
            const auto shape = 1.0 / (1.0 + std::pow ((harmonic * f0 - 700.0) / 400.0, 2.0)) + 0.1;
            line[i] += (float) (0.025 * shape * std::sin (juce::MathConstants<double>::twoPi * f0 * harmonic * t));
        }
    }
    const auto play = [&line] (const Rig& factory, bool withModulators)
    {
        RigRig r;
        r.processor.loadRig (r.entryNamed (factory.name));
        if (! withModulators)
            for (int target = 0; target < ParamIDs::harmonyModTargets().size(); ++target)
                for (int slot = 0; slot < effects::HarmonyModulation::slotsPerTarget; ++slot)
                    r.processor.setHarmonyModSlot (target, slot, {});
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 256);
        for (size_t start = 0; start + 256 <= line.size(); start += 256)
        {
            // The keyboard rig: a soft C and a hard G, then the pitch wheel up.
            juce::MidiBuffer midi;
            if (start == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.3f), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 67, 1.0f), 0);
            }
            if (start == (size_t) (1.5 * rate) / 256 * 256)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
            buffer.clear();
            std::copy (line.begin() + (long) start, line.begin() + (long) start + 256, buffer.getWritePointer (0));
            r.processor.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        return out;
    };

    auto count = 0;
    for (const auto& factory : makeFactoryRigs())
    {
        if (factory.bank != 6)
            continue;
        ++count;
        INFO (factory.name);
        REQUIRE_FALSE (factory.harmonyModSlots.empty());
        const auto moving = play (factory, true), still = play (factory, false);
        double difference = 0.0, level = 0.0;
        for (size_t i = 0; i < moving.size(); ++i)
        {
            difference += (double) (moving[i] - still[i]) * (moving[i] - still[i]);
            level += (double) moving[i] * moving[i];
        }
        const auto db = 10.0 * std::log10 (difference / (level + 1.0e-12) + 1.0e-12);
        CAPTURE (db);
        CHECK (db > -20.0);   // the modulators' part is clearly there, not a rounding error
    }
    CHECK (count == 9);
}

TEST_CASE ("Classic left the compressor's menu (0.41.0): new rigs start on Revival, old ones keep Classic", "[rigs]")
{
    ParagonX1AudioProcessor processor;
    CHECK (juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::compType)->load()) == 2);   // Revival

    // A rig saved before 0.41.0 that never mentioned the TYPE relied on Classic being the default.
    juce::XmlElement old ("ParagonRig");
    old.setAttribute ("version", 1);
    old.setAttribute ("parameterVersion", 37);
    old.setAttribute ("name", "Old Squeeze");
    auto* on = old.createNewChildElement ("Param");
    on->setAttribute ("id", ParamIDs::compOn);
    on->setAttribute ("value", 1.0);
    const auto loaded = Rig::fromXml (old);
    REQUIRE (loaded.has_value());
    CHECK (loaded->parameters.at (ParamIDs::compType) == 0.0f);

    // Saved now, a rig without a TYPE is on the new default.
    juce::XmlElement fresh (old);
    fresh.setAttribute ("parameterVersion", 38);
    CHECK (Rig::fromXml (fresh)->parameters.count (ParamIDs::compType) == 0);
}

TEST_CASE ("The album banks (H, I, J, K, L): screams torn up, cleans clean, growls an octave down, the duet two voices wide, no HARMONY", "[rigs]")
{
    // The same sung vowel as the level test: 220 Hz with vibrato, its
    // harmonics up to 2.6 kHz and nothing above. Anything a rig puts above
    // 3.5 kHz, then, it made: distortion (or a pitch shift up).
    constexpr double rate = 48000.0;
    std::vector<float> phrase ((size_t) (2.0 * rate), 0.0f);
    for (size_t i = 0; i < phrase.size(); ++i)
    {
        const auto t = (double) i / rate;
        const auto f0 = 220.0 * (1.0 + 0.01 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
        {
            const auto shape = 1.0 / (1.0 + std::pow ((harmonic * 220.0 - 700.0) / 400.0, 2.0)) + 0.1;
            phrase[i] += (float) (0.025 * shape * std::sin (juce::MathConstants<double>::twoPi * f0 * harmonic * t));
        }
    }

    struct Measures { double highDb, subDb, correlation; };
    const auto measure = [&] (const Rig& factory)
    {
        RigRig r;
        r.processor.loadRig (r.entryNamed (factory.name));
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        std::vector<float> left, right;
        for (size_t start = 0; start + 256 <= phrase.size(); start += 256)
        {
            buffer.clear();
            std::copy (phrase.begin() + (long) start, phrase.begin() + (long) start + 256, buffer.getWritePointer (0));
            r.processor.processBlock (buffer, midi);
            left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
            right.insert (right.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + 256);
        }
        // The spectrum of a steady second (0.8 s in, past every attack), both sides summed.
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);
        const auto from = (size_t) (0.8 * rate);
        for (int i = 0; i < size; ++i)
        {
            const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
            data[(size_t) i] = window * (left[from + (size_t) i] + right[from + (size_t) i]);
        }
        fft.performFrequencyOnlyForwardTransform (data.data());
        double total = 0.0, high = 0.0, sub = 0.0;
        for (int bin = 1; bin < size / 2; ++bin)
        {
            const auto hz = bin * rate / size;
            const auto energy = (double) data[(size_t) bin] * data[(size_t) bin];
            total += energy;
            if (hz > 3500.0)
                high += energy;
            if (hz > 95.0 && hz < 125.0)
                sub += energy;
        }
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t i = from; i < from + (size_t) size; ++i)
        {
            lr += (double) left[i] * right[i];
            ll += (double) left[i] * left[i];
            rr += (double) right[i] * right[i];
        }
        return Measures { 10.0 * std::log10 (high / total + 1.0e-12), 10.0 * std::log10 (sub / total + 1.0e-12), lr / std::sqrt (ll * rr + 1.0e-24) };
    };

    std::map<int, int> perBank;
    for (const auto& factory : makeFactoryRigs())
    {
        if (factory.bank < 7)   // H and later: one album each
            continue;
        ++perBank[factory.bank];
        INFO (factory.getSlotName() << " " << factory.name);
        // The layers come from the DOUBLER: none of these uses the HARMONY
        // engine (voices, generated notes, TUNE VOICE or FORMANT), which the
        // owner wants used as little as possible until it's reworked.
        for (const auto* id : { ParamIDs::harmonyOn, ParamIDs::harmonyGenerate, ParamIDs::harmonyCorrect, ParamIDs::formantOn })
        {
            const auto found = factory.parameters.find (id);
            CHECK ((found == factory.parameters.end() || found->second == 0.0f));
        }
        const auto m = measure (factory);
        CAPTURE (m.highDb, m.subDb, m.correlation);
        if (std::getenv ("PARAGON_RIG_MEASURES") != nullptr)
            std::cout << factory.getSlotName() << " " << factory.name << ": high " << m.highDb << " dB, sub " << m.subDb
                      << " dB, correlation " << m.correlation << "\n";

        // The screams are torn up: clear distortion above the voice's own
        // harmonics (the growl's top is rolled off on purpose, so it's
        // checked by its low octave instead).
        if (factory.tags.contains ("Scream") && ! factory.tags.contains ("Growl"))
            CHECK (m.highDb > -23.0);
        // The cleans stay clean.
        if (factory.tags.contains ("Clean"))
            CHECK (m.highDb < -25.0);
        // A growl: a full octave down under the voice, strong.
        if (factory.tags.contains ("Growl"))
            CHECK (m.subDb > -22.0);
        // The duet: two different throats, one an octave down, spread across.
        if (factory.name == "Twin Throat Duet")
        {
            CHECK (m.subDb > -30.0);
            CHECK (m.correlation < 0.85);
        }
        // Every one of them does something to the voice in stereo or tone:
        // none is a plain mono copy of what came in.
        CHECK (m.correlation < 0.995);
    }
    CHECK (perBank[7] == 15);   // H
    CHECK (perBank[8] == 15);   // I
    CHECK (perBank[9] == 15);   // J
    CHECK (perBank[10] == 15);  // K
    CHECK (perBank[11] == 15);  // L
}

TEST_CASE ("Bank L: the rap's glitch edits really chop the words, and leave most of them standing", "[rigs]")
{
    // A rap-like phrase: 120 ms syllables, each on its own pitch, with short
    // gaps, at 120 BPM (the tempo GLITCH follows with no host). A stutter
    // repeats a syllable where the next should be, so the output differs
    // from the same rig with GLITCH off; a steady tone would hide that.
    constexpr double rate = 48000.0;
    std::vector<float> phrase ((size_t) (3.0 * rate), 0.0f);
    const double pitches[] = { 196.0, 220.0, 174.6, 246.9, 207.7, 185.0, 233.1, 164.8 };
    for (size_t i = 0; i < phrase.size(); ++i)
    {
        const auto t = (double) i / rate;
        const auto syllable = (int) (t / 0.15);
        const auto within = t - syllable * 0.15;
        if (within > 0.12)
            continue;
        const auto envelope = std::sin (juce::MathConstants<double>::pi * within / 0.12);
        const auto f0 = pitches[syllable % 8];
        for (int harmonic = 1; harmonic <= 10; ++harmonic)
            phrase[i] += (float) (0.03 * envelope / harmonic * std::sin (juce::MathConstants<double>::twoPi * f0 * harmonic * t));
    }
    const auto render = [&] (const juce::String& name, bool glitch)
    {
        RigRig r;
        r.processor.loadRig (r.entryNamed (name));
        if (! glitch)
            r.set (ParamIDs::glitchOn, 0.0f);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (size_t start = 0; start + 256 <= phrase.size(); start += 256)
        {
            buffer.clear();
            std::copy (phrase.begin() + (long) start, phrase.begin() + (long) start + 256, buffer.getWritePointer (0));
            r.processor.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        return out;
    };
    for (const auto* name : { "Stutter Edit Verse", "Broken Signal Rap", "Chopped Grid Rap" })
    {
        const auto edited = render (name, true), plain = render (name, false);
        // In 50 ms windows after the first half second: how many are changed
        // by the edit (the difference within 10 dB of the plain rap)?
        int changed = 0, windows = 0;
        const auto window = (size_t) (0.05 * rate);
        for (size_t start = (size_t) (0.5 * rate); start + window <= plain.size(); start += window)
        {
            double difference = 0.0, level = 0.0;
            for (size_t i = start; i < start + window; ++i)
            {
                difference += (double) (edited[i] - plain[i]) * (edited[i] - plain[i]);
                level += (double) plain[i] * plain[i];
            }
            if (level < 1.0e-9)
                continue;
            ++windows;
            if (difference > 0.1 * level)
                ++changed;
        }
        const auto share = (double) changed / (double) windows;
        CAPTURE (name, changed, windows, share);
        if (std::getenv ("PARAGON_RIG_MEASURES") != nullptr)
            std::cout << name << ": " << changed << " of " << windows << " windows edited\n";
        // Edited often enough to hear in every bar...
        CHECK (share > 0.15);
        // ...but most of the words get through untouched.
        CHECK (share < 0.75);
    }
}

TEST_CASE ("Bank K: the choruses carry a strong, tight double and nothing is overloaded", "[rigs]")
{
    // The owner records a singer with this album's tone and asked for two
    // things: the strong double the choruses are known for, and effects that
    // never swamp the voice. Both are measured on the same sung vowel as the
    // album-bank test (220 Hz, harmonics to 2.6 kHz, nothing above).
    constexpr double rate = 48000.0;
    std::vector<float> phrase ((size_t) (2.0 * rate), 0.0f);
    for (size_t i = 0; i < phrase.size(); ++i)
    {
        const auto t = (double) i / rate;
        const auto f0 = 220.0 * (1.0 + 0.01 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
        {
            const auto shape = 1.0 / (1.0 + std::pow ((harmonic * 220.0 - 700.0) / 400.0, 2.0)) + 0.1;
            phrase[i] += (float) (0.025 * shape * std::sin (juce::MathConstants<double>::twoPi * f0 * harmonic * t));
        }
    }
    // The level (dB) of a steady second, both sides, the rig as saved or
    // with one of its effects switched off after loading.
    const auto levelDb = [&] (const juce::String& name, const char* offId)
    {
        RigRig r;
        r.processor.loadRig (r.entryNamed (name));
        if (offId != nullptr)
            r.set (offId, 0.0f);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        double sum = 0.0;
        size_t count = 0;
        for (size_t start = 0; start + 256 <= phrase.size(); start += 256)
        {
            buffer.clear();
            std::copy (phrase.begin() + (long) start, phrase.begin() + (long) start + 256, buffer.getWritePointer (0));
            r.processor.processBlock (buffer, midi);
            if (start < (size_t) (0.8 * rate))
                continue;
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < 256; ++i)
                    sum += (double) buffer.getSample (channel, i) * buffer.getSample (channel, i);
            count += 512;
        }
        return 10.0 * std::log10 (sum / (double) count + 1.0e-20);
    };

    // The strong double, measured as the takes against the lead: the rig
    // with its echo and space off, once with only the DOUBLER's takes
    // (DIRECT off) and once with only the lead (the DOUBLER off). A breathy,
    // noise-like voice band is used here: copies of a perfectly steady tone
    // line up with it and partly cancel, which a real voice doesn't do.
    std::vector<float> breath ((size_t) (2.0 * rate), 0.0f);
    {
        juce::Random random (7);
        float low = 0.0f, lower = 0.0f;
        for (auto& sample : breath)
        {
            low += 0.25f * (random.nextFloat() * 2.0f - 1.0f - low);     // under about 2.5 kHz
            lower += 0.03f * (low - lower);                               // less below about 230 Hz
            sample = 0.4f * (low - lower);
        }
    }
    const auto partDb = [&] (const juce::String& name, const char* offId)
    {
        RigRig r;
        r.processor.loadRig (r.entryNamed (name));
        r.set (ParamIDs::delayOn, 0.0f);
        r.set (ParamIDs::reverbOn, 0.0f);
        r.set (offId, 0.0f);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        double sum = 0.0;
        size_t count = 0;
        for (size_t start = 0; start + 256 <= breath.size(); start += 256)
        {
            buffer.clear();
            std::copy (breath.begin() + (long) start, breath.begin() + (long) start + 256, buffer.getWritePointer (0));
            r.processor.processBlock (buffer, midi);
            if (start < (size_t) (0.8 * rate))
                continue;
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < 256; ++i)
                    sum += (double) buffer.getSample (channel, i) * buffer.getSample (channel, i);
            count += 512;
        }
        return 10.0 * std::log10 (sum / (double) count + 1.0e-20);
    };
    const auto takesAgainstLead = [&] (const char* name)
    {
        const auto db = partDb (name, ParamIDs::doublerDirectOn) - partDb (name, ParamIDs::doublerOn);
        CAPTURE (name, db);
        if (std::getenv ("PARAGON_RIG_MEASURES") != nullptr)
            std::cout << name << ": the takes are " << db << " dB against the lead\n";
        return db;
    };
    // In each chorus the takes together are about as loud as the lead,
    // K08 (the owner's example) included. Two takes 2 dB under measure
    // about -0.5 to -1 dB (the shifter that detunes them costs about 1.5 dB);
    // a polite double, two takes 7 dB under, would sit several dB lower...
    for (const auto* name : { "Storm Belt Chorus", "Twin Heart Chorus", "Bittersweet Anthem", "Lonely Street Echo", "Rising Hall Lift" })
        CHECK (takesAgainstLead (name) > -2.0);
    // ...while a verse keeps its one take faint.
    for (const auto* name : { "Restless Verse", "Hushed Diary Verse", "Piano Lament" })
        CHECK (takesAgainstLead (name) < -10.0);

    // Nothing overloaded: the echoes and the space stay under the voice.
    // Switching DELAY and REVERB off takes away less than 1.5 dB.
    for (const auto& factory : makeFactoryRigs())
    {
        if (factory.bank != 10)
            continue;
        const auto full = levelDb (factory.name, nullptr);
        const auto noDelay = levelDb (factory.name, ParamIDs::delayOn);
        const auto noReverb = levelDb (factory.name, ParamIDs::reverbOn);
        CAPTURE (factory.name, full - noDelay, full - noReverb);
        if (std::getenv ("PARAGON_RIG_MEASURES") != nullptr)
            std::cout << factory.name << ": delay adds " << full - noDelay << " dB, reverb " << full - noReverb << " dB\n";
        CHECK (full - noDelay < 1.5);
        CHECK (full - noReverb < 1.5);
    }
}
