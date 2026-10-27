#include "ui/MarsPanel.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/DoublerPanel.h"
#include "ui/EffectPanel.h"
#include "ui/Footswitch.h"
#include "ui/Faceplate.h"
#include "ui/SignalChain.h"
#include "ui/GraphicEqPanel.h"
#include "ui/HarmonyPanel.h"
#include "dsp/effects/Delay.h"
#include "ui/EqPresets.h"
#include "ui/Finish.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/PerformanceControls.h"

using Catch::Matchers::WithinAbs;

namespace
{
    template <typename Type>
    void findAll (juce::Component& parent, std::vector<Type*>& found)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                found.push_back (match);
            findAll (*child, found);
        }
    }

    // Every visible component sits inside its parent and has some size.
    void checkLayout (juce::Component& parent, const juce::String& path)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible() || dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                continue;

            const auto name = path + "/" + (child->getName().isNotEmpty() ? child->getName()
                                                                          : juce::String (typeid (*child).name()));
            INFO (name << " at " << child->getBounds().toString() << " in " << parent.getLocalBounds().toString());
            CHECK_FALSE (child->getBounds().isEmpty());
            CHECK (parent.getLocalBounds().expanded (1).contains (child->getBounds()));
            checkLayout (*child, name);
        }
    }

    std::vector<float> playThrough (ParagonX1AudioProcessor& processor, const std::vector<float>& input)
    {
        juce::AudioBuffer<float> buffer (2, (int) input.size());
        buffer.clear();
        std::copy (input.begin(), input.end(), buffer.getWritePointer (0));
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        return { buffer.getReadPointer (1), buffer.getReadPointer (1) + buffer.getNumSamples() };
    }

    void set (ParagonX1AudioProcessor& processor, const char* id, float plainValue)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }
}

TEST_CASE ("The window lays out cleanly at every size", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    for (const auto width : { 960, 1280, 1920 })
    {
        DYNAMIC_SECTION (width << " wide")
        {
            editor->setSize (width, width * 9 / 16);
            REQUIRE (editor->getWidth() == width);

            // The view is scaled, not re-laid-out: its transform matches the window.
            auto* view = editor->getChildComponent (0);
            REQUIRE (view != nullptr);
            CHECK_THAT (view->getTransform().mat00, WithinAbs ((float) width / (float) MainView::width, 0.001));

            checkLayout (*view, "view");
        }
    }
}

TEST_CASE ("The window opens as big as fits above the taskbar, up to 1824 x 1026", "[ui]")
{
    using Editor = ParagonX1AudioProcessorEditor;
    // 1080p with a 40-pixel taskbar: room left for the DAW's title bar and toolbar.
    const auto fullHd = Editor::openingSize ({ 1920, 1040 });
    CHECK (fullHd == juce::Point<int> (1706, 959));
    CHECK (fullHd.y + 80 <= 1040);
    CHECK (Editor::openingSize ({ 2560, 1400 }) == juce::Point<int> (1824, 1026));
    const auto laptop = Editor::openingSize ({ 1366, 728 });
    CHECK (laptop.x <= 1366 - 40);
    CHECK (laptop.y + 80 <= 728);
    CHECK (Editor::openingSize ({ 800, 600 }).x == 960);   // never below the smallest size
}

TEST_CASE ("PEARL BLACK recolours the unit's printing, and every open window follows", "[ui]")
{
    Finish::set (Finish::Type::pearlWhite, false);
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> first (processor.createEditor()), second (processor.createEditor());

    // RIG LEVEL is printed on the unit: ink on white, light on black.
    const auto rigLevelColour = [] (juce::Component& editor)
    {
        std::vector<juce::Label*> labels;
        findAll (editor, labels);
        for (auto* label : labels)
            if (label->getText() == "RIG LEVEL")
                return label->findColour (juce::Label::textColourId);
        return juce::Colour();
    };
    CHECK (rigLevelColour (*first) == ParagonColours::ink);

    // Chosen in one window (here, without saving the preference), it shows in both.
    Finish::set (Finish::Type::pearlBlack, false);
    CHECK (Finish::isBlack());
    CHECK (rigLevelColour (*first) == Finish::ink());
    CHECK (rigLevelColour (*second) == Finish::ink());
    CHECK (Finish::ink().getPerceivedBrightness() > 0.8f);

    // Pearl white with neon keys: the white unit's ink, the black unit's keys.
    Finish::set (Finish::Type::pearlWhiteNeon, false);
    CHECK (! Finish::isBlack());
    CHECK (Finish::neonKeys());
    CHECK (rigLevelColour (*first) == ParagonColours::ink);

    // The key and knob drawing still works in every finish.
    for (const auto type : { Finish::Type::pearlBlack, Finish::Type::pearlWhiteNeon, Finish::Type::pearlWhite })
    {
        Finish::set (type, false);
        first->setSize (1280, 720);
        const auto image = first->createComponentSnapshot (first->getLocalBounds());
        CHECK (image.isValid());
    }
    CHECK (rigLevelColour (*first) == ParagonColours::ink);
}

TEST_CASE ("The chain and footswitches pick which block's knobs show; the graphic EQ is the default", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    // The knob panels in signal-chain order: nine EffectPanels, with
    // SATURATION's own (MARS) as block 4, the graphic EQ's as 5 and
    // DOUBLER's as 7.
    std::vector<EffectPanel*> effectPanels;
    findAll (*editor, effectPanels);
    REQUIRE (effectPanels.size() == 9);
    std::vector<GraphicEqPanel*> eqPanels;
    findAll (*editor, eqPanels);
    REQUIRE (eqPanels.size() == 1);
    std::vector<juce::Component*> panels (effectPanels.begin(), effectPanels.end());
    std::vector<MarsPanel*> marsPanels;
    findAll (*editor, marsPanels);
    REQUIRE (marsPanels.size() == 1);
    panels.insert (panels.begin() + 4, marsPanels[0]);
    constexpr int eqIndex = 5;
    panels.insert (panels.begin() + eqIndex, eqPanels[0]);
    std::vector<DoublerPanel*> doublerPanels;
    findAll (*editor, doublerPanels);
    REQUIRE (doublerPanels.size() == 1);
    panels.insert (panels.begin() + 7, doublerPanels[0]);
    // PITCH (block 0) opens on its HARMONY view; its OCTAVE / FREEZE view is
    // the first EffectPanel, checked below.
    std::vector<HarmonyPanel*> harmonyPanels;
    findAll (*editor, harmonyPanels);
    REQUIRE (harmonyPanels.size() == 1);
    auto* octaveFreeze = panels[0];
    panels[0] = harmonyPanels[0];

    const auto visiblePanel = [&]
    {
        int count = 0, index = -1;
        for (size_t i = 0; i < panels.size(); ++i)
            if (panels[i]->isVisible())
            {
                ++count;
                index = (int) i;
            }
        REQUIRE (count == 1);
        return index;
    };

    // To start with the dock shows the graphic EQ.
    CHECK (visiblePanel() == eqIndex);

    std::vector<SignalChain*> chains;
    findAll (*editor, chains);
    REQUIRE (chains.size() == 1);
    for (int block = 0; block < (int) panels.size(); ++block)
    {
        chains[0]->onSelect (block);
        CHECK (visiblePanel() == block);
    }

    // The seven stomps, 1 to 7: DYNAMICS, SATURATION, DOUBLER on the top
    // row; PITCH, MODULATION, DELAY, REVERB on the bottom. Each one's name
    // shows its own block (by signal-chain position).
    std::vector<Footswitch*> footswitches;
    findAll (*editor, footswitches);
    REQUIRE (footswitches.size() == 7);
    std::sort (footswitches.begin(), footswitches.end(), [] (auto* a, auto* b)
    {
        return std::make_pair (a->getY(), a->getX()) < std::make_pair (b->getY(), b->getX());   // reading order
    });
    const int expectedBlocks[] = { 1, 4, 7, 0, 8, 9, 10 };
    for (size_t i = 0; i < footswitches.size(); ++i)
    {
        INFO ("stomp " << i + 1);
        footswitches[i]->onSelect();
        CHECK (visiblePanel() == expectedBlocks[i]);
    }
    for (size_t i = 1; i < 3; ++i)
        CHECK (footswitches[i]->getY() == footswitches[0]->getY());   // one top row of three...
    for (size_t i = 4; i < 7; ++i)
        CHECK (footswitches[i]->getY() == footswitches[3]->getY());   // ...one bottom row of four...
    CHECK (footswitches[0]->getBottom() <= footswitches[3]->getY());   // ...above it

    // PITCH's view tabs swap HARMONY for OCTAVE / FREEZE and back.
    std::vector<juce::TextButton*> buttons;
    findAll (*editor, buttons);
    const auto button = [&buttons] (const juce::String& text)
    {
        const auto found = std::find_if (buttons.begin(), buttons.end(), [&text] (auto* b) { return b->getButtonText() == text; });
        REQUIRE (found != buttons.end());
        return *found;
    };
    chains[0]->onSelect (0);
    button ("OCTAVE / FREEZE")->onClick();
    CHECK (octaveFreeze->isVisible());
    CHECK_FALSE (harmonyPanels[0]->isVisible());
    button ("HARMONY")->onClick();
    CHECK (harmonyPanels[0]->isVisible());
    CHECK_FALSE (octaveFreeze->isVisible());

    // The GRAPHIC EQ button on any other block's panel goes back to the EQ.
    const auto back = std::find_if (buttons.begin(), buttons.end(), [] (auto* b) { return b->getButtonText() == "GRAPHIC EQ"; });
    REQUIRE (back != buttons.end());
    CHECK ((*back)->isVisible());
    (*back)->onClick();
    CHECK (visiblePanel() == eqIndex);
    CHECK_FALSE ((*back)->isVisible());
}

TEST_CASE ("BYPASS passes the clean voice, on both channels", "[ui][bypass]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (1, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // Make the processed sound very different from the input...
    set (processor, ParamIDs::driveOn, 1.0f);
    set (processor, ParamIDs::lofiAmount, 10.0f);
    set (processor, ParamIDs::inputGain, 12.0f);
    set (processor, ParamIDs::bypass, 1.0f);

    std::vector<float> input (512);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.3f * std::sin (0.05f * (float) i);

    for (int block = 0; block < 4; ++block)
        playThrough (processor, input);   // let the 20 ms bypass fade finish (one block is ~11 ms)
    const auto output = playThrough (processor, input);
    for (size_t i = 0; i < input.size(); ++i)
        REQUIRE_THAT (output[i], WithinAbs (input[i], 1.0e-6));

    CHECK (processor.getBypassParameter() == processor.getParameters().getParameter (ParamIDs::bypass));
}

TEST_CASE ("No latency, so singing through it live has no delay, and BYPASS stays in time", "[ui][bypass]")
{
    // Nothing in PR7-V's chain looks ahead (the NAM amp, which did, went in
    // 0.3.0), so the voice comes straight through. The bypassed signal must
    // line up with the processed one, or toggling bypass would shift it.
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (1, 2, 44100.0, 512);
    processor.prepareToPlay (44100.0, 512);
    const auto latency = processor.getLatencySamples();
    CHECK (latency == 0);

    set (processor, ParamIDs::bypass, 1.0f);
    for (int block = 0; block < 4; ++block)
        playThrough (processor, std::vector<float> (512, 0.0f));   // let the bypass fade finish

    std::vector<float> click (512, 0.0f);
    click[10] = 0.5f;
    const auto output = playThrough (processor, click);
    CHECK_THAT (output[(size_t) (10 + latency)], WithinAbs (0.5, 1.0e-6));   // the click, exactly where it went in
}

TEST_CASE ("BYPASS is global: rigs don't change it and it doesn't mark them modified", "[ui][bypass]")
{
    ParagonX1AudioProcessor processor;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1UiTests").getChildFile (juce::Uuid().toString());
    processor.useRigFolder (folder);

    processor.loadRig (*processor.getRigLibrary().findFactory ("Studio Vocal"));
    set (processor, ParamIDs::bypass, 1.0f);
    CHECK_FALSE (processor.isRigModified());

    processor.loadRig (*processor.getRigLibrary().findFactory ("Slapback"));
    CHECK (processor.getParameters().getRawParameterValue (ParamIDs::bypass)->load() > 0.5f);
    folder.deleteRecursively();
}

TEST_CASE ("The expression screen: click a line to assign the pedal, again to unassign; scroll to step", "[ui]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    ExpressionAssign screen (state);
    screen.setBounds (0, 0, 272, 160);   // about its size in the EXPRESSION ASSIGN panel
    const auto k = 160.0f / 126.0f;      // its lettering's scale at that height
    // The glass's content: right of it the scroll buttons, inside it the frame.
    const auto glass = screen.getLocalBounds().toFloat().withTrimmedRight (38.0f).reduced (7.0f).reduced (5.0f * k, 4.0f * k);

    const auto target = [&] { return juce::roundToInt (state.getRawParameterValue (ParamIDs::expressionTarget)->load()); };
    const auto rowCentre = [&] (int line)
    {
        // The heading at the top, the position bar at the bottom, the lines
        // showing evenly between (as the screen lays them out).
        const auto top = glass.getY() + 15.0f * k, height = (glass.getHeight() - 25.0f * k) / (float) ExpressionAssign::visibleRows;
        return juce::Point<float> (glass.getCentreX(), top + height * ((float) line + 0.5f));
    };
    const auto click = [&] (int line)
    {
        const auto position = rowCentre (line);
        const auto now = juce::Time::getCurrentTime();
        screen.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &screen, &screen, now, position, now, 1, false));
    };
    const auto scroll = [&] (float deltaY)
    {
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = deltaY;
        const auto now = juce::Time::getCurrentTime();
        screen.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                 &screen, &screen, now, {}, now, 0, false), wheel);
    };

    REQUIRE (target() == ExpressionTargets::off);

    click (1);
    CHECK (target() == ExpressionTargets::wah);
    click (4);
    CHECK (target() == ExpressionTargets::loFi);
    click (4);
    CHECK (target() == ExpressionTargets::off);

    // WHAMMY-WAH also switches the FILTER / PITCH block on as a Whammy-Wah.
    click (5);
    CHECK (target() == ExpressionTargets::whammyWah);
    CHECK (state.getRawParameterValue (ParamIDs::fpOn)->load() > 0.5f);
    CHECK (juce::roundToInt (state.getRawParameterValue (ParamIDs::fpType)->load()) == (int) effects::FilterPitch::Type::whammyWah);
    click (5);
    CHECK (target() == ExpressionTargets::off);

    // Scrolling down from unassigned starts at the top, then steps down; it stops at the ends.
    scroll (-1.0f);
    CHECK (target() == ExpressionTargets::volume);
    scroll (-1.0f);
    CHECK (target() == ExpressionTargets::wah);
    scroll (1.0f);
    scroll (1.0f);
    CHECK (target() == ExpressionTargets::volume);
    click (0);
    CHECK (target() == ExpressionTargets::off);

    // The list is longer than the screen: the down arrow (the heading's
    // right end) scrolls it, and the last line becomes TALK BOX, which also
    // switches FILTER / PITCH on as a Talk Box.
    const auto downArrow = juce::Point<float> (glass.getRight() - 5.0f * k, glass.getY() + 6.5f * k);
    for (int i = 0; i < 2; ++i)
    {
        const auto now = juce::Time::getCurrentTime();
        screen.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), downArrow, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &screen, &screen, now, downArrow, now, 1, false));
    }
    CHECK (target() == ExpressionTargets::off);   // scrolling doesn't choose anything
    click (ExpressionAssign::visibleRows - 1);
    CHECK (target() == ExpressionTargets::talkBox);
    CHECK (juce::roundToInt (state.getRawParameterValue (ParamIDs::fpType)->load()) == (int) effects::FilterPitch::Type::talkBox);
}

TEST_CASE ("The dock's expression screen scrolls with the buttons beside it", "[ui]")
{
    ParagonX1AudioProcessor processor;
    ExpressionAssign screen (processor.getParameters());
    screen.setBounds (0, 0, 272, 160);   // about its size in the EXPRESSION ASSIGN panel

    std::vector<juce::Button*> buttons;
    findAll (screen, buttons);
    REQUIRE (buttons.size() == 2);
    auto* up = buttons[0];
    auto* down = buttons[1];
    // Both inside the component, to the right of the screen.
    for (auto* button : buttons)
    {
        CHECK (screen.getLocalBounds().contains (button->getBounds()));
        CHECK (button->getX() > screen.getWidth() / 2);
    }

    // At the top: only down works. Two steps reach the end of the eight-line list.
    CHECK (! up->isEnabled());
    CHECK (down->isEnabled());
    for (int i = 0; i < ExpressionAssign::numTargets - ExpressionAssign::visibleRows; ++i)
        down->onClick();
    CHECK (! down->isEnabled());
    CHECK (up->isEnabled());

    // Scrolling never assigns the pedal.
    CHECK (juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::expressionTarget)->load()) == ExpressionTargets::off);
}

TEST_CASE ("EQ PRESETS: choosing one sets the faders and switches the EQ on; moving a fader lets go of the name", "[ui]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    GraphicEqPanel panel (state);
    panel.setBounds (0, 0, 520, 220);

    juce::ComboBox* menu = nullptr;
    for (auto* child : panel.getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*> (child))
            menu = box;
    REQUIRE (menu != nullptr);
    CHECK (menu->getSelectedItemIndex() == -1);   // a flat EQ isn't one of the presets

    const auto value = [&] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };
    const auto& scoop = EqPresets::all[0];
    CHECK (scoop.vocal);   // the vocal presets come first
    CHECK (std::count_if (EqPresets::all.begin(), EqPresets::all.end(), [] (const auto& preset) { return preset.vocal; }) == 13);
    menu->setSelectedItemIndex (0, juce::sendNotificationSync);

    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
        CHECK_THAT (value (ParamIDs::eqBands[band]), WithinAbs (scoop.gainsDb[(size_t) band], 0.05));
    CHECK_THAT (value (ParamIDs::eqLevel), WithinAbs (scoop.levelDb, 0.05));
    CHECK (value (ParamIDs::eqOn) > 0.5f);
    CHECK (menu->getText() == juce::String (scoop.name));

    // Nudge 1 kHz: it's no longer the preset, so the menu says PRESETS again.
    auto* band = state.getParameter (ParamIDs::eqBands[4]);
    band->setValueNotifyingHost (band->convertTo0to1 (scoop.gainsDb[4] + 2.0f));
    CHECK (menu->getSelectedItemIndex() == -1);
}

namespace
{
    juce::MouseEvent mouseAt (juce::Component& component, juce::Point<float> where, juce::ModifierKeys mods = {})
    {
        return { juce::Desktop::getInstance().getMainMouseSource(), where, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &component, &component, juce::Time::getCurrentTime(), where, juce::Time::getCurrentTime(), 1, false };
    }
}

TEST_CASE ("HARMONY's graph: click to add a voice, drag it, scroll its interval, right-click to remove it", "[ui][harmony]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    HarmonyPanel panel (state, [] { return -1.0f; });
    panel.setSize (643, 243);
    const auto value = [&state] (int voice, const char* part)
    {
        return state.getRawParameterValue (ParamIDs::harmonyVoice (voice, part))->load();
    };

    // Voices 1 and 2 start in use (a third and a fifth above); 3 to 8 are free.
    REQUIRE (value (0, "Active") > 0.5f);
    REQUIRE (value (2, "Active") < 0.5f);

    SECTION ("A click on an empty spot adds the first free voice there, at unison")
    {
        const auto spot = panel.pointFor (0.6f, -6.0f);
        panel.mouseDown (mouseAt (panel, spot));
        panel.mouseUp (mouseAt (panel, spot));
        CHECK (value (2, "Active") > 0.5f);
        CHECK (panel.getSelectedVoice() == 2);
        CHECK_THAT (value (2, "Pan"), WithinAbs (0.6, 0.02));
        CHECK_THAT (value (2, "Level"), WithinAbs (-6.0, 0.3));
        CHECK_THAT (value (2, "Pitch"), WithinAbs (0.0, 0.01));
    }

    SECTION ("Dragging a voice moves its pan and level")
    {
        const auto from = panel.pointFor (value (0, "Pan"), value (0, "Level"));
        const auto to = panel.pointFor (0.2f, -12.0f);
        panel.mouseDown (mouseAt (panel, from));
        panel.mouseDrag (mouseAt (panel, to));
        panel.mouseUp (mouseAt (panel, to));
        CHECK_THAT (value (0, "Pan"), WithinAbs (0.2, 0.02));
        CHECK_THAT (value (0, "Level"), WithinAbs (-12.0, 0.3));
    }

    SECTION ("Scrolling over a voice changes its interval a semitone at a time")
    {
        juce::MouseWheelDetails up {};
        up.deltaY = 0.2f;
        panel.mouseWheelMove (mouseAt (panel, panel.pointFor (value (0, "Pan"), value (0, "Level"))), up);
        CHECK_THAT (value (0, "Pitch"), WithinAbs (5.0, 0.01));   // +4 -> +5
    }

    SECTION ("Right-clicking a voice removes it")
    {
        const auto at = panel.pointFor (value (1, "Pan"), value (1, "Level"));
        panel.mouseDown (mouseAt (panel, at, juce::ModifierKeys::rightButtonModifier));
        CHECK (value (1, "Active") < 0.5f);
    }

    SECTION ("The graph's level scale: 0 dB sits above -12 dB, Off at the bottom")
    {
        CHECK (panel.pointFor (0.0f, 0.0f).y < panel.pointFor (0.0f, -12.0f).y);
        CHECK (panel.pointFor (0.0f, -12.0f).y < panel.pointFor (0.0f, effects::Harmony::silentDb).y);
        const auto [pan, level] = panel.panAndLevelAt (panel.pointFor (-0.4f, -3.0f));
        CHECK_THAT (pan, WithinAbs (-0.4, 0.02));
        CHECK_THAT (level, WithinAbs (-3.0, 0.3));
    }
}

TEST_CASE ("The PITCH stomp: first press HARMONY; then it brings back whatever was on", "[ui][harmony]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    Footswitch pitch (4, "PITCH", state, { ParamIDs::harmonyOn, ParamIDs::octOn }, { ParamIDs::harmonyOn });
    const auto on = [&state] (const char* id) { return state.getRawParameterValue (id)->load() > 0.5f; };

    pitch.toggle();   // first press: HARMONY only, not the octaver too
    CHECK (on (ParamIDs::harmonyOn));
    CHECK_FALSE (on (ParamIDs::octOn));

    set (processor, ParamIDs::octOn, 1.0f);   // the octaver switched on in the panel as well
    pitch.toggle();
    CHECK_FALSE (on (ParamIDs::harmonyOn));
    CHECK_FALSE (on (ParamIDs::octOn));
    pitch.toggle();   // both back, as they were
    CHECK (on (ParamIDs::harmonyOn));
    CHECK (on (ParamIDs::octOn));

    set (processor, ParamIDs::harmonyOn, 0.0f);   // just the octaver now
    pitch.toggle();
    pitch.toggle();
    CHECK_FALSE (on (ParamIDs::harmonyOn));
    CHECK (on (ParamIDs::octOn));
}

TEST_CASE ("MARS's display: double-click splits a band (the new one copies it), right-click removes one", "[ui][mars]")
{
    ParagonX1AudioProcessor processor;
    auto& parameters = processor.getParameters();
    const auto value = [&] (const juce::String& id) { return parameters.getRawParameterValue (id)->load(); };
    const auto setValue = [&] (const juce::String& id, float plain)
    {
        auto* parameter = parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
    };
    MarsPanel panel (parameters, [] (int) { return 0.0f; });
    panel.setBounds (0, 0, 672, 254);

    setValue (ParamIDs::marsBand (0, "Style"), (float) effects::Mars::Style::britishAmp);
    setValue (ParamIDs::marsBand (0, "Drive"), 0.7f);
    panel.splitBand (0, 1000.0f);
    CHECK (juce::roundToInt (value (ParamIDs::marsBands)) == 1);   // 2 bands
    CHECK_THAT (value (ParamIDs::marsCross1), Catch::Matchers::WithinAbs (1000.0, 1.0));
    CHECK (juce::roundToInt (value (ParamIDs::marsBand (1, "Style"))) == (int) effects::Mars::Style::britishAmp);
    CHECK_THAT (value (ParamIDs::marsBand (1, "Drive")), Catch::Matchers::WithinAbs (0.7, 0.01));
    CHECK (panel.getSelectedBand() == 1);

    // Split the top band again, then remove the middle one: the top one moves down.
    setValue (ParamIDs::marsBand (1, "Style"), (float) effects::Mars::Style::warmTube);
    panel.splitBand (1, 4000.0f);
    CHECK (juce::roundToInt (value (ParamIDs::marsBands)) == 2);   // 3 bands
    CHECK_THAT (value (ParamIDs::marsCross2), Catch::Matchers::WithinAbs (4000.0, 1.0));
    setValue (ParamIDs::marsBand (2, "Style"), (float) effects::Mars::Style::foldback);
    panel.removeBand (1);
    CHECK (juce::roundToInt (value (ParamIDs::marsBands)) == 1);
    CHECK (juce::roundToInt (value (ParamIDs::marsBand (1, "Style"))) == (int) effects::Mars::Style::foldback);
    CHECK_THAT (value (ParamIDs::marsCross1), Catch::Matchers::WithinAbs (1000.0, 1.0));

    // The display's frequency scale runs both ways.
    CHECK_THAT (panel.frequencyAt (panel.xFor (2500.0f)), Catch::Matchers::WithinRel (2500.0f, 0.001f));
}


TEST_CASE ("MARS's modulation pages: slots from the knobs, the target list, XLFO steps", "[ui][mars][modulation]")
{
    using Mod = effects::MarsModulation;
    ParagonX1AudioProcessor processor;
    auto& parameters = processor.getParameters();
    const auto value = [&] (const juce::String& id) { return parameters.getRawParameterValue (id)->load(); };
    MarsPanel panel (parameters, [] (int) { return 0.0f; }, &processor.getMars());
    panel.setBounds (0, 0, 672, 254);

    SECTION ("Four pages; MOD shows the sources, SLOTS the slots")
    {
        panel.showPage (2);
        CHECK (panel.getSourcesView().isVisible());
        CHECK_FALSE (panel.getSlotsView().isVisible());
        panel.showPage (3);
        CHECK (panel.getSlotsView().isVisible());
        CHECK_FALSE (panel.getSourcesView().isVisible());
        for (int tab = 0; tab < MarsSourcesView::numTabs; ++tab)
            panel.getSourcesView().selectTab (tab);   // every tab lays out
    }

    SECTION ("Modulating a knob takes the first free slot at +50%; a full set says so")
    {
        const auto drive = Mod::bandTarget (0, Mod::drive);
        CHECK (MarsModUi::addSlot (parameters, Mod::lfo1, drive, 0.5f) == 0);
        CHECK (MarsModUi::addSlot (parameters, Mod::ef1, drive, 0.5f) == 1);
        CHECK (juce::roundToInt (value (ParamIDs::marsSlot (1, "Source"))) == Mod::ef1);
        CHECK (juce::roundToInt (value (ParamIDs::marsSlot (1, "Target"))) == drive);
        CHECK_THAT (value (ParamIDs::marsSlot (1, "Amount")), Catch::Matchers::WithinAbs (0.5, 0.01));
        CHECK (MarsModUi::slotsOn (parameters, drive).size() == 2);
        CHECK (MarsModUi::targetColour (parameters, drive) == MarsModUi::mixedColour);   // two kinds of source
        MarsModUi::clearSlot (parameters, 0);
        CHECK (MarsModUi::slotsOn (parameters, drive).size() == 1);
        CHECK (MarsModUi::targetColour (parameters, drive) == MarsModUi::sourceColour (Mod::ef1));
        CHECK (MarsModUi::addSlot (parameters, Mod::xy1x, drive, 0.5f) == 0);   // the freed slot again
        for (int s = 2; s < Mod::numSlots; ++s)
            MarsModUi::addSlot (parameters, Mod::lfo2, Mod::globalTarget (Mod::output), 0.1f);
        CHECK (MarsModUi::addSlot (parameters, Mod::lfo2, drive, 0.5f) == -1);
    }

    SECTION ("The grouped target list keeps the parameter's order")
    {
        juce::ComboBox box;
        MarsSlotsView::fillTargets (box);
        REQUIRE (box.getNumItems() == Mod::numTargets);
        for (int target = 1; target < Mod::numTargets; ++target)
            CHECK (box.getItemText (target) == Mod::targetName (target));
    }

    SECTION ("XLFO steps: + copies the picked step after it, - removes it")
    {
        auto& sources = panel.getSourcesView();
        sources.selectTab (1);   // XLFO 2
        const auto step = [&] (int s, const char* part) { return value (ParamIDs::marsStep (1, s, part)); };
        sources.selectStep (0);
        sources.addStep();   // 1, 1, -1
        CHECK (juce::roundToInt (value (ParamIDs::marsLfo (1, "Steps"))) == 2);
        CHECK_THAT (step (1, "Value"), Catch::Matchers::WithinAbs (1.0, 0.01));
        CHECK_THAT (step (2, "Value"), Catch::Matchers::WithinAbs (-1.0, 0.01));
        CHECK (sources.getSelectedStep() == 1);
        sources.selectStep (0);
        sources.removeStep();   // 1, -1
        CHECK (juce::roundToInt (value (ParamIDs::marsLfo (1, "Steps"))) == 1);
        CHECK_THAT (step (1, "Value"), Catch::Matchers::WithinAbs (-1.0, 0.01));
        CHECK (juce::roundToInt (value (ParamIDs::marsLfo (0, "Steps"))) == 1);   // XLFO 1 untouched
    }
}

TEST_CASE ("HARMONY's NOTES tab: the note controls in place of the voice's", "[ui][harmony][notes]")
{
    ParagonX1AudioProcessor processor;
    HarmonyPanel panel (processor.getParameters(), [] { return -1.0f; }, [] { return -1.0f; }, &processor.getHarmony());
    panel.setBounds (0, 0, 672, 254);
    std::vector<juce::ComboBox*> boxes;
    findAll (panel, boxes);
    const auto chordBox = std::find_if (boxes.begin(), boxes.end(), [] (juce::ComboBox* box)
                                        { return box->getNumItems() == (int) effects::Harmony::chordMaps.size(); });
    REQUIRE (chordBox != boxes.end());
    CHECK_FALSE ((*chordBox)->isVisible());
    panel.showNotes (true);
    CHECK (panel.isShowingNotes());
    CHECK ((*chordBox)->isVisible());
    CHECK ((*chordBox)->getText() == "3rd + 5th Above");   // the default
    panel.selectVoice (1);
    panel.showNotes (false);
    CHECK_FALSE ((*chordBox)->isVisible());
}

TEST_CASE ("HARMONY's Note Map Editor: EDIT, keys, SCALE boxes and DONE", "[ui][harmony][notemap]")
{
    ParagonX1AudioProcessor processor;
    auto& parameters = processor.getParameters();
    HarmonyPanel panel (parameters, [] { return -1.0f; }, [] { return -1.0f; }, &processor.getHarmony());
    panel.setBounds (0, 0, 672, 254);
    panel.showNotes (true);
    const auto plain = [&parameters] (const juce::String& id) { return parameters.getRawParameterValue (id)->load(); };
    auto& editor = panel.getNoteMap();
    const auto mapOfA = [&editor] { return editor.mapOf (9); };
    const auto bit = [] (int offset) { return std::uint64_t { 1 } << (offset + effects::Harmony::mapSpan); };

    // EDIT: CHORD becomes Custom, starting from the map that was playing
    // (3rd + 5th Above in C major: A plays C and E above).
    CHECK_FALSE (panel.isShowingNoteMap());
    panel.openNoteMap();
    CHECK (panel.isShowingNoteMap());
    CHECK (juce::roundToInt (plain (ParamIDs::harmonyChord)) == effects::Harmony::customChord);
    CHECK (mapOfA() == (bit (3) | bit (7)));

    // A key click adds or removes what the picked note plays.
    editor.selectNote (9);
    editor.toggleKey (60 + 9 + 12);   // A4: an octave above
    CHECK (mapOfA() == (bit (3) | bit (7) | bit (12)));
    CHECK (plain (ParamIDs::harmonyMap (9, 1)) > 0.0f);
    editor.toggleKey (60 + 9 + 3);    // C5 off
    CHECK (mapOfA() == (bit (7) | bit (12)));

    // SCALE boxes: only with SCALE Custom; In → Up → Down → In.
    editor.cycleScaleBox (1);
    CHECK (juce::roundToInt (plain (ParamIDs::harmonyScaleNote (1))) == 0);
    auto* scale = parameters.getParameter (ParamIDs::harmonyScale);
    scale->setValueNotifyingHost (scale->convertTo0to1 ((float) Scales::Scale::custom));
    editor.cycleScaleBox (1);
    CHECK (juce::roundToInt (plain (ParamIDs::harmonyScaleNote (1))) == 1);
    editor.cycleScaleBox (1);
    CHECK (juce::roundToInt (plain (ParamIDs::harmonyScaleNote (1))) == 2);

    // A second EDIT keeps your map; ERASE clears it; DONE (or leaving NOTES) closes.
    panel.closeNoteMap();
    panel.openNoteMap();
    CHECK (mapOfA() == (bit (7) | bit (12)));
    editor.erase();
    CHECK (mapOfA() == 0);
    panel.showNotes (false);
    CHECK_FALSE (panel.isShowingNoteMap());
}

TEST_CASE ("The PITCH stomp switches TUNE VOICE too: off means the plain voice, on brings its mode back", "[ui][harmony][correct]")
{
    // 0.21.0 and before: TUNE VOICE works without HARMONY, and the stomp
    // didn't switch it, so with the stomp's light out the voice stayed
    // tuned and 40 ms late: PITCH seemed impossible to turn off.
    ParagonX1AudioProcessor processor;
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;
    processor.setPlayConfigDetails (1, 2, rate, blockSize);
    set (processor, ParamIDs::inputLowCut, 20.0f);
    processor.prepareToPlay (rate, blockSize);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<Footswitch*> footswitches;
    findAll (*editor, footswitches);
    REQUIRE (footswitches.size() == 7);
    std::sort (footswitches.begin(), footswitches.end(), [] (auto* a, auto* b)
               { return std::make_pair (a->getY(), a->getX()) < std::make_pair (b->getY(), b->getX()); });
    auto& pitch = *footswitches[3];
    auto& state = processor.getParameters();
    const auto correct = [&state] { return juce::roundToInt (state.getRawParameterValue (ParamIDs::harmonyCorrect)->load()); };

    // The largest difference between what goes in and what comes out over
    // the last half of a second of a slightly flat A3 (a voice to tune).
    const auto changeToVoice = [&processor]
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        auto worst = 0.0f;
        const auto blocks = (int) rate / blockSize;
        for (int n = 0; n < blocks; ++n)
        {
            std::array<float, blockSize> in {};
            buffer.clear();
            for (int i = 0; i < blockSize; ++i)
            {
                const auto t = (double) (n * blockSize + i) / rate;
                for (int h = 1; h <= 6; ++h)
                    in[(size_t) i] += (float) (0.15 / h * std::sin (juce::MathConstants<double>::twoPi * 214.0 * h * t));
                buffer.setSample (0, i, in[(size_t) i]);
            }
            processor.processBlock (buffer, midi);
            if (n >= blocks / 2)
                for (int i = 0; i < blockSize; ++i)
                    worst = std::max (worst, std::abs (buffer.getSample (0, i) - in[(size_t) i]));
        }
        return worst;
    };

    set (processor, ParamIDs::harmonyCorrect, (float) effects::Harmony::Correct::hard);
    processor.updateLatency();
    CHECK (processor.getLatencySamples() > 0);
    CHECK (changeToVoice() > 0.1f);   // tuned (and late)
    pitch.refresh();
    CHECK (pitch.isAnyOn());          // the stomp's light shows it

    pitch.toggle();                   // off
    processor.updateLatency();
    CHECK (correct() == (int) effects::Harmony::Correct::off);
    CHECK (processor.getLatencySamples() == 0);
    CHECK (changeToVoice() < 1.0e-5f);   // the plain voice, untouched
    CHECK_FALSE (pitch.isAnyOn());

    pitch.toggle();                   // on: Hard again, not another mode, and not HARMONY
    CHECK (correct() == (int) effects::Harmony::Correct::hard);
    CHECK (state.getRawParameterValue (ParamIDs::harmonyOn)->load() < 0.5f);
}

TEST_CASE ("HARMONY's FLATTEN and FX view", "[ui][harmony][voicefx]")
{
    ParagonX1AudioProcessor processor;
    auto& parameters = processor.getParameters();
    HarmonyPanel panel (parameters, [] { return -1.0f; }, [] { return -1.0f; }, &processor.getHarmony());
    panel.setBounds (0, 0, 672, 254);
    std::vector<juce::Slider*> sliders;
    std::vector<juce::ComboBox*> boxes;
    findAll (panel, sliders);
    findAll (panel, boxes);
    // The visible slider (or box) bound to `id` right now, if any.
    const auto sliderFor = [&] (const juce::String& id) -> juce::Slider*
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters.getParameter (id));
        for (auto* slider : sliders)
            if (slider->isVisible() && slider->getRange().getEnd() == parameter->getNormalisableRange().end
                && juce::approximatelyEqual ((float) slider->getValue(), parameter->convertFrom0to1 (parameter->getValue())))
                return slider;
        return nullptr;
    };
    const auto boxWith = [&] (const juce::StringArray& choices) -> juce::ComboBox*
    {
        for (auto* box : boxes)
            if (box->isVisible() && box->getNumItems() == choices.size() && box->getItemText (0) == choices[0])
                return box;
        return nullptr;
    };

    // FLATTEN: PITCH's knob gives way to NOTE (C1 to C5).
    panel.selectVoice (0);
    CHECK (sliderFor (ParamIDs::harmonyVoice (0, "Pitch")) != nullptr);
    CHECK (sliderFor (ParamIDs::harmonyVoice (0, "Note")) == nullptr);
    set (processor, ParamIDs::harmonyVoice (0, "Flatten").toRawUTF8(), 1.0f);
    panel.selectVoice (0);   // (the panel's timer does this in the plugin)
    CHECK (sliderFor (ParamIDs::harmonyVoice (0, "Note")) != nullptr);
    CHECK (sliderFor (ParamIDs::harmonyVoice (0, "Pitch")) == nullptr);

    // FX: the voice's DELAY, SYNC, FEEDBACK, FILTER, CUTOFF instead.
    CHECK (boxWith (effects::Harmony::filterNames) == nullptr);
    panel.showFx (true);
    CHECK (panel.isShowingFx());
    auto* filter = boxWith (effects::Harmony::filterNames);
    REQUIRE (filter != nullptr);
    CHECK (boxWith (effects::Delay::syncNames) != nullptr);
    CHECK (sliderFor (ParamIDs::harmonyVoice (0, "Note")) == nullptr);

    // Switching to HIGH-PASS with CUTOFF at 20 kHz (silence) starts it at 20 Hz.
    const auto cutoffOf = [&parameters] (const juce::String& id) { return parameters.getRawParameterValue (id)->load(); };
    filter->setSelectedItemIndex (1, juce::sendNotificationSync);
    CHECK (juce::roundToInt (cutoffOf (ParamIDs::harmonyVoice (0, "Filter"))) == 1);
    CHECK_THAT (cutoffOf (ParamIDs::harmonyVoice (0, "Cutoff")), Catch::Matchers::WithinAbs (effects::Harmony::filterLowestHz, 0.5));

    // On NOTES, FX is the note voices' (still at their own starting values).
    panel.showNotes (true);
    filter = boxWith (effects::Harmony::filterNames);
    REQUIRE (filter != nullptr);
    CHECK (filter->getSelectedItemIndex() == 0);
    filter->setSelectedItemIndex (2, juce::sendNotificationSync);   // band-pass: from 20 kHz to 1 kHz
    CHECK (juce::roundToInt (cutoffOf (ParamIDs::harmonyNoteFilter)) == 2);
    CHECK_THAT (cutoffOf (ParamIDs::harmonyNoteCutoff), Catch::Matchers::WithinAbs (1000.0, 1.0));
    CHECK_THAT (cutoffOf (ParamIDs::harmonyVoice (0, "Cutoff")), Catch::Matchers::WithinAbs (effects::Harmony::filterLowestHz, 0.5));   // voice 1 untouched
    panel.showFx (false);
    CHECK (boxWith (effects::Harmony::filterNames) == nullptr);
}

TEST_CASE ("The rack: every block's display, real controls and EXPRESSION ASSIGN on one unit", "[ui][faceplate][rack]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<Faceplate*> faceplates;
    findAll (*editor, faceplates);
    REQUIRE (faceplates.size() == 1);
    auto& rack = *faceplates[0];
    REQUIRE (rack.isRack());
    std::vector<SignalChain*> chains;
    findAll (*editor, chains);
    REQUIRE (chains.size() == 1);
    std::vector<EffectPanel*> effectPanels;
    findAll (*editor, effectPanels);
    std::vector<juce::Component*> editors;   // HARMONY, the EQ, the DOUBLER, MARS
    {
        std::vector<HarmonyPanel*> harmony; findAll (*editor, harmony);
        std::vector<GraphicEqPanel*> eq; findAll (*editor, eq);
        std::vector<DoublerPanel*> doubler; findAll (*editor, doubler);
        std::vector<MarsPanel*> mars; findAll (*editor, mars);
        REQUIRE ((harmony.size() == 1 && eq.size() == 1 && doubler.size() == 1 && mars.size() == 1));
        editors = { harmony[0], eq[0], doubler[0], mars[0] };
    }
    auto* parent = rack.getParentComponent();
    const auto rackBounds = rack.getBounds();

    // EXPRESSION ASSIGN at the rack's right end, on the same unit.
    std::vector<ExpressionPanel*> expression;
    findAll (*editor, expression);
    REQUIRE (expression.size() == 1);
    CHECK (expression[0]->getBounds() == rack.getSideArea() + rack.getPosition());
    CHECK (expression[0]->getRight() > rack.getControlArea().getRight() + rack.getX());

    for (int block = 0; block < 12; ++block)
    {
        CAPTURE (block);
        chains[0]->onSelect (block);
        // Exactly one set of controls showing, on the rack's face, between
        // its display and EXPRESSION ASSIGN.
        juce::Component* showing = nullptr;
        int count = 0;
        for (auto* panel : effectPanels)
            if (panel->isVisible())
            {
                CHECK (panel->isRack());
                showing = panel;
                ++count;
            }
        for (auto* panel : editors)
            if (panel->isVisible())
            {
                showing = panel;
                ++count;
            }
        REQUIRE (count == 1);
        const auto shown = showing->getBoundsInParent();   // as drawn, scaled for an editor
        CHECK (rackBounds.contains (shown));
        CHECK (shown.getX() >= rack.getX() + rack.getControlArea().getX());
        CHECK (shown.getRight() <= expression[0]->getX());
        // An editor sits in its screen on the face, scaled to fit, and a
        // click on it lands on it (the scaling also moves where it's clicked).
        if (dynamic_cast<EffectPanel*> (showing) == nullptr && dynamic_cast<GraphicEqPanel*> (showing) == nullptr)
        {
            CHECK (shown == rack.getWindowGlass() + rack.getPosition());
            const auto centre = shown.getCentre();
            auto* hit = parent->getComponentAt (centre);
            CHECK ((hit == showing || showing->isParentOf (hit)));
        }
        else
        {
            // On the face at full size, not shrunk into a screen.
            CHECK (rack.getWindowGlass().isEmpty());
            CHECK (showing->getTransform().isIdentity());
            CHECK (shown == rack.getControlArea() + rack.getPosition());
        }
    }

    // The GRAPHIC EQ's faders on the rack set their bands.
    chains[0]->onSelect (5);
    auto* eqPanel = dynamic_cast<GraphicEqPanel*> (editors[1]);
    REQUIRE (eqPanel != nullptr);
    CHECK (eqPanel->isRack());
    std::vector<juce::Slider*> eqFaders;
    findAll (*eqPanel, eqFaders);
    REQUIRE (eqFaders.size() == 10);   // nine bands and LEVEL
    std::sort (eqFaders.begin(), eqFaders.end(), [] (auto* a, auto* b) { return a->getX() < b->getX(); });
    eqFaders[2]->setValue (-5.0, juce::sendNotificationSync);   // 250 Hz
    CHECK_THAT (processor.getParameters().getRawParameterValue ("eq250")->load(), Catch::Matchers::WithinAbs (-5.0, 0.05));
    eqFaders[9]->setValue (3.0, juce::sendNotificationSync);    // LEVEL
    CHECK_THAT (processor.getParameters().getRawParameterValue (ParamIDs::eqLevel)->load(), Catch::Matchers::WithinAbs (3.0, 0.05));

    // On the rack the controls are the real ones: DELAY's FEEDBACK knob
    // (one of the unit's big knobs) sets the delay's feedback.
    chains[0]->onSelect (9);
    EffectPanel* delay = nullptr;
    for (auto* panel : effectPanels)
        if (panel->isVisible())
            delay = panel;
    REQUIRE (delay != nullptr);
    juce::Label* feedbackName = nullptr;
    for (auto* child : delay->getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getText() == "FEEDBACK")
            feedbackName = label;
    REQUIRE (feedbackName != nullptr);
    juce::Slider* feedback = nullptr;
    for (auto* child : delay->getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*> (child); slider != nullptr && slider->getBounds().contains (feedbackName->getBounds().getCentreX(), slider->getY() + 1))
            feedback = slider;
    REQUIRE (feedback != nullptr);
    CHECK ((bool) feedback->getProperties()[ParagonLookAndFeel::pedalKnobProperty]);
    feedback->setValue (0.8, juce::sendNotificationSync);
    CHECK_THAT (processor.getParameters().getRawParameterValue (ParamIDs::delayFeedback)->load(), Catch::Matchers::WithinAbs (0.8, 0.01));
}

TEST_CASE ("The FACEPLATE shows the selected block, by its TYPE, with live meters", "[ui][faceplate]")
{
    ParagonX1AudioProcessor processor;
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;
    processor.setPlayConfigDetails (1, 2, rate, blockSize);
    processor.prepareToPlay (rate, blockSize);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<Faceplate*> faceplates;
    findAll (*editor, faceplates);
    REQUIRE (faceplates.size() == 1);
    auto& faceplate = *faceplates[0];
    std::vector<Footswitch*> footswitches;
    findAll (*editor, footswitches);
    std::sort (footswitches.begin(), footswitches.end(), [] (auto* a, auto* b)
               { return std::make_pair (a->getY(), a->getX()) < std::make_pair (b->getY(), b->getX()); });
    REQUIRE (footswitches.size() == 7);

    // The graphic EQ shows first.
    CHECK (faceplate.getLook() == Faceplate::Look::eq);

    // DYNAMICS (stomp 1's name): the faceplate follows its TYPE.
    footswitches[0]->onSelect();
    for (auto [type, look] : { std::pair { 0, Faceplate::Look::classic }, std::pair { 1, Faceplate::Look::daybreak },
                               std::pair { 2, Faceplate::Look::revival } })
    {
        set (processor, ParamIDs::compType, (float) type);
        CHECK (faceplate.getLook() == look);
    }
    footswitches[1]->onSelect();   // SATURATION
    CHECK (faceplate.getLook() == Faceplate::Look::mars);
    footswitches[3]->onSelect();   // PITCH
    CHECK (faceplate.getLook() == Faceplate::Look::harmony);
    footswitches[5]->onSelect();   // DELAY
    CHECK (faceplate.getLook() == Faceplate::Look::delay);
    footswitches[6]->onSelect();   // REVERB
    CHECK (faceplate.getLook() == Faceplate::Look::reverb);
    // Every block in the signal chain has its own faceplate now.
    std::vector<SignalChain*> chains;
    findAll (*editor, chains);
    REQUIRE (chains.size() == 1);
    const Faceplate::Look expected[] = { Faceplate::Look::harmony, Faceplate::Look::revival /* TYPE set above */, Faceplate::Look::deEsser,
                                         Faceplate::Look::filterPitch, Faceplate::Look::mars, Faceplate::Look::eq,
                                         Faceplate::Look::glitch, Faceplate::Look::doubler, Faceplate::Look::modulation,
                                         Faceplate::Look::delay, Faceplate::Look::reverb, Faceplate::Look::loFi };
    for (int block = 0; block < (int) std::size (expected); ++block)
    {
        chains[0]->onSelect (block);
        CAPTURE (block);
        CHECK (faceplate.getLook() == expected[block]);
    }

    // A loud note: the compressor's needle swings, MARS's driven band glows.
    const auto play = [&processor]
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0, n = 0; b < 100; ++b)
        {
            buffer.clear();
            for (int i = 0; i < blockSize; ++i, ++n)
                buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * n / rate));
            processor.processBlock (buffer, midi);
        }
    };
    footswitches[0]->onSelect();
    set (processor, ParamIDs::compType, 0.0f);
    set (processor, ParamIDs::compOn, 1.0f);
    set (processor, ParamIDs::compThreshold, -40.0f);
    set (processor, ParamIDs::compRatio, 8.0f);
    set (processor, ParamIDs::driveOn, 1.0f);
    set (processor, ParamIDs::marsBand (0, "On").toRawUTF8(), 1.0f);
    set (processor, ParamIDs::marsBand (0, "Drive").toRawUTF8(), 0.9f);
    play();
    for (int tick = 0; tick < 30; ++tick)
        faceplate.updateMeters();
    CAPTURE (processor.getCompGainReductionDb(), faceplate.getShownReduction(), faceplate.getShownDrive (0), faceplate.getShownDrive (1));
    CHECK (faceplate.getShownReduction() < -6.0f);
    CHECK_THAT (faceplate.getShownReduction(), Catch::Matchers::WithinAbs (processor.getCompGainReductionDb(), 1.0));
    CHECK (faceplate.getShownDrive (0) > 0.1f);
    CHECK (faceplate.getShownDrive (1) < 1.0e-3f);   // band 2 isn't in use

    // DYNAMICS off: the needle comes back to rest.
    set (processor, ParamIDs::compOn, 0.0f);
    play();
    for (int tick = 0; tick < 60; ++tick)
        faceplate.updateMeters();
    CHECK (faceplate.getShownReduction() > -0.5f);
}

TEST_CASE ("HARMONY's GLOBAL view: GLIDE, SYNC, PITCH, FORMANT, and its asterisk", "[ui][harmony][global]")
{
    ParagonX1AudioProcessor processor;
    HarmonyPanel panel (processor.getParameters(), [] { return -1.0f; }, [] { return -1.0f; }, &processor.getHarmony());
    panel.setBounds (0, 0, 672, 254);
    std::vector<juce::TextButton*> buttons;
    std::vector<juce::Label*> labels;
    findAll (panel, buttons);
    findAll (panel, labels);
    const auto button = [&buttons] (const juce::String& prefix) -> juce::TextButton*
    {
        for (auto* b : buttons)
            if (b->getButtonText().startsWith (prefix))
                return b;
        return nullptr;
    };
    const auto labelShown = [&labels] (const juce::String& text)
    {
        for (auto* label : labels)
            if (label->isVisible() && label->getText() == text)
                return true;
        return false;
    };
    auto* global = button ("GLOBAL");
    REQUIRE (global != nullptr);
    CHECK (global->isVisible());
    CHECK_FALSE (labelShown ("GLIDE"));

    panel.showGlobal (true);
    CHECK (panel.isShowingGlobal());
    CHECK (labelShown ("GLIDE"));
    CHECK (labelShown ("SOURCE"));       // the mix stays
    CHECK_FALSE (labelShown ("FLATTEN"));

    // The asterisk: a global setting changed.
    panel.updateReadings();
    CHECK (global->getButtonText() == "GLOBAL");
    set (processor, ParamIDs::harmonyGlobalPitch, 7.0f);
    panel.updateReadings();
    CHECK (global->getButtonText() == "GLOBAL*");

    // FX and GLOBAL take turns; a voice tab goes back to the voice.
    panel.showFx (true);
    CHECK_FALSE (panel.isShowingGlobal());
    CHECK_FALSE (labelShown ("GLIDE"));
    panel.showGlobal (true);
    CHECK_FALSE (panel.isShowingFx());
    button ("2")->onClick();   // what clicking tab 2 does
    CHECK_FALSE (panel.isShowingGlobal());
    CHECK (labelShown ("FLATTEN"));
}

TEST_CASE ("HARMONY's SNAPSHOTS page: pick to recall, STORE, COPY / PASTE, DELETE", "[ui][harmony][snapshots]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<HarmonyPanel*> panels;
    findAll (*editor, panels);
    REQUIRE (panels.size() == 1);
    auto& panel = *panels[0];
    panel.openSnapshots();
    CHECK (panel.isShowingSnapshots());
    auto& page = panel.getSnapshots();
    const auto pitch = [&processor] { return processor.getParameters().getRawParameterValue (ParamIDs::harmonyVoice (0, "Pitch"))->load(); };

    // STORE with nothing picked: the first empty slot.
    page.storeHere();
    CHECK (page.getPicked() == 0);
    REQUIRE (processor.getHarmonySnapshot (0).has_value());
    CHECK (HarmonySnapshotsView::summary (*processor.getHarmonySnapshot (0)) == "+4 +7");   // the two starting voices

    // Change the voice, store in slot 3, and pick slot 1 again: it's recalled.
    set (processor, ParamIDs::harmonyVoice (0, "Pitch").toRawUTF8(), 3.0f);
    page.pick (2);
    page.storeHere();
    page.pick (0);
    CHECK (pitch() == 4.0f);
    page.pick (2);
    CHECK (pitch() == 3.0f);

    // COPY slot 3, PASTE into slot 8; DELETE it again.
    page.copyPicked();
    page.pick (7);   // empty: just picked
    CHECK (pitch() == 3.0f);
    page.pastePicked();
    REQUIRE (processor.getHarmonySnapshot (7).has_value());
    CHECK (*processor.getHarmonySnapshot (7) == *processor.getHarmonySnapshot (2));
    page.deletePicked();
    CHECK_FALSE (processor.getHarmonySnapshot (7).has_value());

    panel.closeSnapshots();
    CHECK_FALSE (panel.isShowingSnapshots());
}
