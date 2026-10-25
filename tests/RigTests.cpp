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
    CHECK (resaved->getIntAttribute ("parameterVersion") == 36);
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

    CHECK (library.nextFreeNumber (0) == 9);   // factory rigs fill A01-A08
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
