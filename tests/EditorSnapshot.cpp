#include "ui/EffectPanel.h"
#include "ui/Faceplate.h"
#include "ui/HarmonyPanel.h"
#include "ui/MarsPanel.h"
#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Finish.h"

namespace
{
    juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        if (parent.getComponentID() == id)
            return &parent;
        for (auto* child : parent.getChildren())
            if (auto* found = findById (*child, id))
                return found;
        return nullptr;
    }
}

// Hidden test (the "[.]" tag): renders the editor to a PNG for reviewing the
// UI without a DAW. Run with:
//   PARAGON_SNAPSHOT=editor.png ./ParagonX1Tests "[snapshot]"
// Optionally set PARAGON_SNAPSHOT_EFFECTS=1 to switch on the saturation,
// delay and reverb, and PARAGON_SNAPSHOT_LOUD=1 to play a note loud enough
// for the LIMITER to work. PARAGON_SNAPSHOT_PAGE=presets shows
// the rig browser, with a couple of example user rigs, and =settings the SETTINGS page. PARAGON_SNAPSHOT_WIDTH
// sets the window width (default 1280).
template <typename Type>
Type* findType (juce::Component& parent)
{
    if (auto* match = dynamic_cast<Type*> (&parent))
        return match;
    for (auto* child : parent.getChildren())
        if (auto* found = findType<Type> (*child))
            return found;
    return nullptr;
}

TEST_CASE ("Render the editor to a PNG", "[.][snapshot]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "editor.png");

    ParagonX1AudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    const auto showPresets = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "presets";

    // PARAGON_SNAPSHOT_FINISH=black or =neon: the pearl black unit, or pearl
    // white with neon keys (not saved as the preference).
    const auto finish = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_FINISH", {});
    Finish::set (finish == "black" ? Finish::Type::pearlBlack : finish == "neon" ? Finish::Type::pearlWhiteNeon : Finish::Type::pearlWhite, false);
    const auto rigFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotRigs");
    rigFolder.deleteRecursively();
    processor.useRigFolder (rigFolder);

    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EFFECTS", {}) == "1")
        for (const auto* id : { "driveOn", "delayOn", "reverbOn" })
            processor.getParameters().getParameter (id)->setValueNotifyingHost (1.0f);

    // PARAGON_SNAPSHOT_RIG=1: a saved rig loaded on the main page, as in the mockup.
    if (showPresets || juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_RIG", {}) == "1")
    {
        processor.saveCurrentRigAs ("Cyber Drive");
        processor.saveCurrentRigAs ("Sunday Worship Clean");
        processor.loadRig (*processor.getRigLibrary().find (rigFolder.getChildFile (juce::String ("Cyber Drive") + Rig::fileExtension)));
        processor.getParameters().getParameter ("lofiAmount")->setValueNotifyingHost (0.8f);   // shows the * marker
    }

    // PARAGON_SNAPSHOT_TUNER=1: the tuner on, hearing an A string 4 cents
    // sharp, and the expression pedal assigned to the wah.
    const auto showTuner = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_TUNER", {}) == "1";
    if (showTuner)
    {
        auto set = [&] (const char* id, float value)
        {
            auto* p = processor.getParameters().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        set ("tunerOn", 1.0f);
        set ("expressionTarget", 2.0f);
        set ("expression", 0.6f);
        set ("expressionMin", 0.2f);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        const auto frequency = 110.0 * std::pow (2.0, 4.0 / 1200.0);
        for (int b = 0, n = 0; b < 100; ++b)
        {
            buffer.clear();
            for (int i = 0; i < 256; ++i, ++n)
                buffer.setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * n / 48000.0));
            processor.processBlock (buffer, midi);
        }
    }

    // PARAGON_SNAPSHOT_EQ=g1,g2,...,g9 switches the graphic EQ on with these band gains (dB).
    if (const auto eq = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EQ", {}); eq.isNotEmpty())
    {
        const auto gains = juce::StringArray::fromTokens (eq, ",", {});
        processor.getParameters().getParameter ("eqOn")->setValueNotifyingHost (1.0f);
        for (int band = 0; band < juce::jmin (gains.size(), ParamIDs::eqBands.size()); ++band)
        {
            auto* p = processor.getParameters().getParameter (ParamIDs::eqBands[band]);
            p->setValueNotifyingHost (p->convertTo0to1 (gains[band].getFloatValue()));
        }
    }

    // PARAGON_SNAPSHOT_EXPRESSION=0..1 sets the expression pedal's position.
    if (const auto pedal = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EXPRESSION", {}); pedal.isNotEmpty())
        processor.getParameters().getParameter ("expression")->setValueNotifyingHost (pedal.getFloatValue());

    // PARAGON_SNAPSHOT_PARAMS=id=value,id=value sets any parameters (plain values, e.g. modType=8).
    for (const auto& setting : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PARAMS", {}), ",", {}))
        if (auto* p = processor.getParameters().getParameter (setting.upToFirstOccurrenceOf ("=", false, false)))
            p->setValueNotifyingHost (p->convertTo0to1 (setting.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));

    // PARAGON_SNAPSHOT_MODULATE=hv1Pitch:1:0.04,hv1Pan:7:-0.5 puts HARMONY
    // modulators on controls (ID, source number, depth), each in its next slot.
    for (const auto& item : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MODULATE", {}), ",", {}))
    {
        const auto parts = juce::StringArray::fromTokens (item, ":", {});
        const auto target = ParagonX1AudioProcessor::harmonyModTarget (parts[0]);
        for (int slot = 0; slot < 4 && target >= 0; ++slot)
            if (processor.getHarmonyModSlot (target, slot).source == 0)
            {
                processor.setHarmonyModSlot (target, slot, { parts[1].getIntValue(), parts[2].getFloatValue() });
                break;
            }
    }

    // PARAGON_SNAPSHOT_PLAY=seconds sings a note (220 Hz, -12 dB) through
    // the plugin for that long first, so live displays (MARS's modulation) show it.
    if (const auto play = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PLAY", {}).getDoubleValue(); play > 0.0)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int b = 0, n = 0; b < (int) (play * 48000.0 / 256.0); ++b)
        {
            buffer.clear();
            for (int i = 0; i < 256; ++i, ++n)
                buffer.setSample (0, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * n / 48000.0));
            processor.processBlock (buffer, midi);
        }
    }

    // PARAGON_SNAPSHOT_LOUD=1 plays a note 6 dB over full scale, so the
    // LIMITER's light shows it working (and the meters show the level).
    const auto loud = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_LOUD", {}) == "1";
    if (loud)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int b = 0, n = 0; b < 40; ++b)
        {
            buffer.clear();
            for (int i = 0; i < 256; ++i, ++n)
                buffer.setSample (0, i, 2.0f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * n / 48000.0));
            processor.processBlock (buffer, midi);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (loud)
        for (int i = 0; i < 3; ++i)   // let the light's and meters' timers read it
        {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep (40);
        }
    if (showTuner)
        for (int i = 0; i < 12; ++i)   // let the editor's timers run: the tuner reads a few times
        {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep (40);
        }
    if (showPresets)
        if (auto* tab = dynamic_cast<juce::Button*> (findById (*editor, "presetsTab")))
            tab->onClick();
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "settings")
        if (auto* tab = dynamic_cast<juce::Button*> (findById (*editor, "settingsTab")))
            tab->onClick();
    // PARAGON_SNAPSHOT_BLOCK=n opens block n's knobs (0 = pitch, 1 = dynamics, 2 = de-esser ... 7 = vocoder, 8 = doubler ... 12 = lo-fi: the signal chain strip's order) in place of the graphic EQ.
    if (const auto block = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_BLOCK", {}); block.isNotEmpty())
        if (auto* chain = findType<SignalChain> (*editor))
            chain->onSelect (block.getIntValue());
    // PARAGON_SNAPSHOT_PITCHVIEW=octave shows PITCH's OCTAVE / FREEZE / FORMANT view rather than HARMONY.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PITCHVIEW", {}) == "octave")
    {
        std::vector<juce::TextButton*> buttons;
        std::function<void (juce::Component&)> collect = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* button = dynamic_cast<juce::TextButton*> (child))
                    buttons.push_back (button);
                collect (*child);
            }
        };
        collect (*editor);
        for (auto* button : buttons)
            if (button->getButtonText() == "OCTAVE / FREEZE / FORMANT")
                button->onClick();
    }
    // PARAGON_SNAPSHOT_MIDI=60,64,67: hold these MIDI keys (and play a block so HARMONY hears them).
    if (const auto keys = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MIDI", {}); keys.isNotEmpty())
    {
        juce::MidiBuffer midi;
        for (const auto& key : juce::StringArray::fromTokens (keys, ",", {}))
            midi.addEvent (juce::MidiMessage::noteOn (1, key.getIntValue(), 0.9f), 0);
        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        processor.processBlock (buffer, midi);
        // With PLAY, keep singing a little with the keys held, so the note voices sound (and their modulators move).
        if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PLAY", {}).getDoubleValue() > 0.0)
            for (int b = 0, n = 0; b < (int) (0.3 * 48000.0 / 256.0); ++b)
            {
                juce::MidiBuffer none;
                for (int i = 0; i < 256; ++i, ++n)
                    buffer.setSample (0, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * n / 48000.0));
                processor.processBlock (buffer, none);
            }
    }
    // PARAGON_SNAPSHOT_HARMONYNOTES=1: the HARMONY panel's NOTES tab.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_HARMONYNOTES", {}) == "1")
        if (auto* harmonyPanel = findType<HarmonyPanel> (*editor))
        {
            harmonyPanel->showNotes (true);
            // PARAGON_SNAPSHOT_NOTEMAP=1: with its Note Map Editor open.
            if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_NOTEMAP", {}) == "1")
                harmonyPanel->openNoteMap();
        }
    // PARAGON_SNAPSHOT_HARMONYFX=1: the HARMONY panel's FX view (the picked
    // voice's, or with HARMONYNOTES the note voices', DELAY and FILTER).
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_HARMONYFX", {}) == "1")
        if (auto* harmonyPanel = findType<HarmonyPanel> (*editor))
            harmonyPanel->showFx (true);
    // PARAGON_SNAPSHOT_STORE=1,2,5: store the voices in those snapshot slots
    // first (each a step higher, so they differ); PARAGON_SNAPSHOT_SNAPSHOTS=1
    // opens the SNAPSHOTS page.
    if (const auto store = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_STORE", {}); store.isNotEmpty())
    {
        auto step = 0.0f;
        for (const auto& slot : juce::StringArray::fromTokens (store, ",", {}))
        {
            auto* pitch = processor.getParameters().getParameter (ParamIDs::harmonyVoice (0, "Pitch"));
            pitch->setValueNotifyingHost (pitch->convertTo0to1 (4.0f + step));
            processor.storeHarmonySnapshot (slot.getIntValue() - 1);
            step += 1.0f;
        }
    }
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_SNAPSHOTS", {}) == "1")
        if (auto* harmonyPanel = findType<HarmonyPanel> (*editor))
            harmonyPanel->openSnapshots();
    // PARAGON_SNAPSHOT_DRAWN=1: a drawn shape in M1 (a rise with a wobble), for SHAPE Drawn.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_DRAWN", {}) == "1")
    {
        std::array<float, 32> points {};
        for (size_t i = 0; i < points.size(); ++i)
            points[i] = -0.8f + 1.6f * (float) i / 31.0f * 0.8f + 0.3f * std::sin ((float) i * 0.9f);
        processor.setHarmonyModShape (0, points);
    }
    // PARAGON_SNAPSHOT_MODS=n: HARMONY's MODS page on tab n (0-3 M1-M4, 4-5 A1-A2, 6 AM, 7 PT, 8 SP, 9 MIDI).
    if (const auto modsTab = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MODS", {}); modsTab.isNotEmpty())
        if (auto* harmonyPanel = findType<HarmonyPanel> (*editor))
        {
            harmonyPanel->openMods (modsTab.getIntValue());
            harmonyPanel->getMods().resized();
        }
    // PARAGON_SNAPSHOT_HARMONYGLOBAL=1: its GLOBAL view.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_HARMONYGLOBAL", {}) == "1")
        if (auto* harmonyPanel = findType<HarmonyPanel> (*editor))
            harmonyPanel->showGlobal (true);
    // PARAGON_SNAPSHOT_PANELPAGE=n shows page n of the open block's panel, for
    // a panel with pages (DELAY's MAIN, STEREO, COLOUR: 0, 1, 2; MARS's
    // BANDS, GLOBAL, MOD, SLOTS: 0-3).
    if (const auto page = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PANELPAGE", {}); page.isNotEmpty())
    {
        std::function<void (juce::Component&)> turn = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* panel = dynamic_cast<EffectPanel*> (child); panel != nullptr && panel->isVisible())
                    panel->showPage (page.getIntValue());
                if (auto* mars = dynamic_cast<MarsPanel*> (child); mars != nullptr && mars->isVisible())
                {
                    mars->showPage (page.getIntValue());
                    // PARAGON_SNAPSHOT_MODTAB=n: MARS's MOD page on source tab n (0 XLFO 1 ... 9 XY 2).
                    if (const auto modTab = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MODTAB", {}); modTab.isNotEmpty())
                        mars->getSourcesView().selectTab (modTab.getIntValue());
                    mars->getSourcesView().refresh();
                    mars->getSlotsView().refresh();
                }
                turn (*child);
            }
        };
        turn (*editor);
    }
    if (const auto width = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_WIDTH", {}).getIntValue(); width > 0)
        editor->setSize (width, width * 9 / 16);
    editor->resized();
    // HARMONY's readings (the sung note, GLOBAL*) as its timer would show them.
    if (auto* harmonyPanel = findType<HarmonyPanel> (*editor))
        harmonyPanel->updateReadings();
    // The FACEPLATE's meters settle as they would over a second of the timer.
    if (auto* faceplate = findType<Faceplate> (*editor))
        for (int tick = 0; tick < 30; ++tick)
            faceplate->updateMeters();
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
