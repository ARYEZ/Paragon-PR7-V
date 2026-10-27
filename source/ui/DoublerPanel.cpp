#include "DoublerPanel.h"

#include "Icons.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "ui/Finish.h"
#include "ui/LedDisplay.h"

namespace
{
    const char* columnNames[] = { "VOICE", "GAIN", "PAN", "DELAY", "FDBK", "OCT", "DETUNE", "DEPTH", "RATE", "DRIFT" };
    const char* columnParts[] = { "On", "Gain", "Pan", "Delay", "Feedback", "Octave", "Detune", "Depth", "Rate", "Drift" };
    const char* columnTips[] = { "Voice on or off", "The voice's level", "Where the voice sits, left to right",
                                 "How late the voice is, in ms (the detuning itself takes about 13 ms)",
                                 "How much of the voice goes back into its own delay, for repeats",
                                 "An octave down", "How far out of tune the voice is, in cents (100 = a semitone); 2-12 sounds like a double",
                                 "How far the voice's tuning wobbles either way, in cents (positive starts upwards)",
                                 "How fast the wobble goes, in Hz",
                                 "How loose the take is: its timing and tuning wander slowly at random, as a real second take does" };
    constexpr int columnWidths[] = { 32, 44, 42, 40, 36, 26, 40, 38, 36, 38 };
}

DoublerPanel::DoublerPanel (juce::AudioProcessorValueTreeState& state, std::function<void()> resetModulation)
    : parameters (state)
{
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
        for (int column = 0; column < numColumns; ++column)
        {
            const auto id = ParamIDs::doublerVoice (voice, columnParts[column]);
            const auto tip = "Voice " + juce::String (voice + 1) + ": " + columnTips[column];
            const auto isSwitch = column == 0 || column == 5;
            table[(size_t) voice][(size_t) column] = isSwitch ? static_cast<juce::Component*> (&makeSwitch (id, tip).button)
                                                              : static_cast<juce::Component*> (&makeBox (id, tip).slider);
        }

    directOn = &makeSwitch (ParamIDs::doublerDirectOn, "Your own voice, through the doubler").button;
    directGain = &makeBox (ParamIDs::doublerDirectGain, "Your own voice's level").slider;
    directPan = &makeBox (ParamIDs::doublerDirectPan, "Where your own voice sits, left to right").slider;
    output = &makeBox (ParamIDs::doublerOutput, "Everything's level: your voice and the doubles").slider;
    lowOn = &makeSwitch (ParamIDs::doublerLowOn, "The low shelf on the doubles (not your own voice)").button;
    lowGain = &makeBox (ParamIDs::doublerLowGain, "Low shelf: boost or cut the doubles' lows").slider;
    lowFreq = &makeBox (ParamIDs::doublerLowFreq, "Low shelf: where the lows start, in Hz").slider;
    highOn = &makeSwitch (ParamIDs::doublerHighOn, "The high shelf on the doubles (not your own voice)").button;
    highGain = &makeBox (ParamIDs::doublerHighGain, "High shelf: boost or cut the doubles' highs").slider;
    highFreq = &makeBox (ParamIDs::doublerHighFreq, "High shelf: where the highs start, in Hz").slider;
    feedbackHighPass = &makeBox (ParamIDs::doublerFbHighPass, "Cuts the lows from the feedback, so repeats stay clear (16 Hz: no cut)").slider;

    modReset.setTooltip ("Start every voice's wobble together, from the top");
    modReset.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    modReset.onClick = std::move (resetModulation);
    addAndMakeVisible (modReset);
}

DoublerPanel::Box& DoublerPanel::makeBox (const juce::String& id, const juce::String& tooltip)
{
    auto box = std::make_unique<Box>();
    auto& slider = box->slider;
    // A number you drag up or down, in a plain box (drawn by the panel), as
    // on Waves Doubler: no bar filling it.
    slider.setSliderSnapsToMousePosition (false);
    slider.setVelocityBasedMode (false);
    slider.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setTooltip (tooltip);
    slider.onDragStart = slider.onDragEnd = [this] { repaint(); };   // the box's edge lights while dragged
    box->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, slider);
    auto* parameter = parameters.getParameter (id);
    slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    addAndMakeVisible (slider);
    boxes.push_back (std::move (box));
    return *boxes.back();
}

DoublerPanel::Switch& DoublerPanel::makeSwitch (const juce::String& id, const juce::String& tooltip)
{
    auto control = std::make_unique<Switch>();
    control->button.setTooltip (tooltip);
    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, id, control->button);
    addAndMakeVisible (control->button);
    switches.push_back (std::move (control));
    return *switches.back();
}

void DoublerPanel::setRack (bool inRack)
{
    if (rack == inRack)
        return;
    rack = inRack;
    for (auto& box : boxes)
    {
        box->slider.setColour (juce::Slider::textBoxTextColourId, rack ? LedDisplay::ledOrange : ParagonColours::accent);
        box->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    }
    for (auto& control : switches)
        control->button.getProperties().set (ParagonLookAndFeel::rackKeyProperty, rack);
    modReset.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, rack);
    resized();
    repaint();
}

void DoublerPanel::layOutRack()
{
    // The top row: MOD RESET (GRAPHIC EQ, placed by the main view, at the right).
    auto area = getLocalBounds();
    auto keys = area.removeFromTop (24);
    modReset.setBounds (keys.removeFromLeft (110));
    area.removeFromTop (8);

    // Left, about two thirds: the voice table, a row per voice.
    const auto gap = 4;
    auto tableArea = area.removeFromLeft (area.getWidth() * 64 / 100);
    area.removeFromLeft (22);
    int totalWidth = 0;
    for (auto width : columnWidths)
        totalWidth += width;
    const auto scale = (float) (tableArea.getWidth() - gap * (numColumns - 1)) / (float) totalWidth;
    auto headings = tableArea.removeFromTop (14);
    std::array<juce::Range<int>, numColumns> columns;
    for (int column = 0; column < numColumns; ++column)
    {
        const auto cell = headings.removeFromLeft (juce::roundToInt ((float) columnWidths[column] * scale));
        columns[(size_t) column] = { cell.getX(), cell.getRight() };
        labels.push_back ({ columnNames[column], cell.expanded (2, 0) });
        headings.removeFromLeft (gap);
    }
    tableArea.removeFromTop (2);
    const auto rowHeight = tableArea.getHeight() / effects::Doubler::numVoices;
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
    {
        const auto row = tableArea.removeFromTop (rowHeight).reduced (0, 4);
        for (int column = 0; column < numColumns; ++column)
        {
            const auto cell = juce::Rectangle<int> (columns[(size_t) column].getStart(), row.getY(), columns[(size_t) column].getLength(), row.getHeight());
            auto* widget = table[(size_t) voice][(size_t) column];
            if (column == 0)
            {
                labels.push_back ({ juce::String (voice + 1), cell.withWidth (12) });
                widget->setBounds (cell.withTrimmedLeft (12).withSizeKeepingCentre (juce::jmin (36, cell.getWidth() - 12), 22));
            }
            else if (column == 5)
                widget->setBounds (cell.withSizeKeepingCentre (juce::jmin (36, cell.getWidth()), 22));
            else
                widget->setBounds (cell.reduced (2, 2));
        }
    }

    // Right: DIRECT and OUTPUT, the LOW and HIGH shelves on the doubles, FB HIGH-PASS.
    const auto cellWidth = area.getWidth() / 4;
    const auto rowHeight2 = area.getHeight() / 4;
    const auto rowOf = [&] (std::initializer_list<std::pair<const char*, juce::Component*>> cells)
    {
        auto row = area.removeFromTop (rowHeight2);
        for (const auto& [name, widget] : cells)
        {
            auto cell = row.removeFromLeft (cellWidth);
            if (name != nullptr)
                labels.push_back ({ name, cell.removeFromTop (13) });
            else
                cell.removeFromTop (13);
            if (widget == nullptr)
                continue;
            if (dynamic_cast<juce::ToggleButton*> (widget) != nullptr)
                widget->setBounds (cell.withSizeKeepingCentre (juce::jmin (40, cell.getWidth() - 4), 22));
            else
                widget->setBounds (cell.reduced (4, 3));
        }
    };
    rowOf ({ { "DIRECT", directOn }, { "GAIN", directGain }, { "PAN", directPan }, { "OUTPUT", output } });
    rowOf ({ { "LOW", lowOn }, { "GAIN", lowGain }, { "FREQ", lowFreq }, { nullptr, nullptr } });
    rowOf ({ { "HIGH", highOn }, { "GAIN", highGain }, { "FREQ", highFreq }, { nullptr, nullptr } });
    rowOf ({ { nullptr, nullptr }, { "FB HIGH-PASS", feedbackHighPass }, { nullptr, nullptr }, { nullptr, nullptr } });
}

void DoublerPanel::resized()
{
    labels.clear();
    if (rack)
    {
        layOutRack();
        return;
    }
    auto area = getLocalBounds().reduced (16, 10);
    area.removeFromTop (34);   // header: icon and name (and GRAPHIC EQ, placed by the main view)

    // Left: the voice table.
    auto tableArea = area.removeFromLeft (399);
    auto headings = tableArea.removeFromTop (14);
    std::array<juce::Range<int>, numColumns> columns;
    for (int column = 0; column < numColumns; ++column)
    {
        const auto cell = headings.removeFromLeft (columnWidths[column]);
        columns[(size_t) column] = { cell.getX(), cell.getRight() };
        labels.push_back ({ columnNames[column], cell });
        headings.removeFromLeft (3);
    }
    tableArea.removeFromTop (4);
    const auto rowHeight = tableArea.getHeight() / effects::Doubler::numVoices;
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
    {
        const auto row = tableArea.removeFromTop (rowHeight).reduced (0, 3);
        for (int column = 0; column < numColumns; ++column)
        {
            const auto cell = juce::Rectangle<int> (columns[(size_t) column].getStart(), row.getY(), columns[(size_t) column].getLength(), row.getHeight());
            auto* widget = table[(size_t) voice][(size_t) column];
            if (column == 0)
            {
                // The voice's number, with its on light beside it.
                labels.push_back ({ juce::String (voice + 1), cell.withWidth (12) });
                widget->setBounds (cell.withTrimmedLeft (12).withSizeKeepingCentre (22, 22));
            }
            else if (column == 5)
                widget->setBounds (cell.withSizeKeepingCentre (22, 22));
            else
                widget->setBounds (cell);
        }
    }

    // Right: direct, output, the shelves, the feedback's high-pass, MOD RESET.
    area.removeFromLeft (14);
    const auto cellWidth = area.getWidth() / 4;
    const auto rowOf = [&] (std::initializer_list<std::pair<const char*, juce::Component*>> cells)
    {
        auto row = area.removeFromTop (44);
        area.removeFromTop (3);
        for (const auto& [name, widget] : cells)
        {
            auto cell = row.removeFromLeft (cellWidth);
            if (name != nullptr)
                labels.push_back ({ name, cell.removeFromTop (14) });
            else
                cell.removeFromTop (14);
            if (widget == nullptr)
                continue;
            const auto box = cell.reduced (2, 1);
            if (dynamic_cast<juce::ToggleButton*> (widget) != nullptr)
                widget->setBounds (box.withSizeKeepingCentre (22, 22));
            else
                widget->setBounds (box);
        }
    };
    rowOf ({ { "DIRECT", directOn }, { "GAIN", directGain }, { "PAN", directPan }, { "OUTPUT", output } });
    rowOf ({ { "LOW", lowOn }, { "GAIN", lowGain }, { "FREQ", lowFreq }, { nullptr, nullptr } });
    rowOf ({ { "HIGH", highOn }, { "GAIN", highGain }, { "FREQ", highFreq }, { nullptr, nullptr } });
    auto last = area.removeFromTop (44);
    auto feedbackCell = last.removeFromLeft (cellWidth * 2);
    labels.push_back ({ "FB HIGH-PASS", feedbackCell.removeFromTop (14) });
    feedbackHighPass->setBounds (feedbackCell.reduced (2, 1).withWidth (cellWidth - 4));
    last.removeFromTop (14);
    modReset.setBounds (last.reduced (2, 1));
}

void DoublerPanel::setAccent (juce::Colour colour)
{
    accent = colour;
    repaint();
}

void DoublerPanel::paintOverChildren (juce::Graphics& g)
{
    for (const auto& glass : glasses)
        LedDisplay::paintGlassOverlay (g, glass, 0.3f);
}

void DoublerPanel::paint (juce::Graphics& g)
{
    if (rack)
    {
        // On the unit's face: each number on a small screen, names stamped
        // in the finish's ink, the voice numbers printed in its red.
        glasses.clear();
        for (const auto& box : boxes)
            glasses.push_back (LedDisplay::paintScreen (g, box->slider.getBounds().toFloat().expanded (2.0f, 2.0f), 0.3f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        for (const auto& [text, where] : labels)
        {
            const auto isVoiceNumber = text.length() == 1 && juce::CharacterFunctions::isDigit (text[0]);
            if (isVoiceNumber)
            {
                g.setColour (Finish::printedRed());
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (text, where, juce::Justification::centredLeft, false);
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                continue;
            }
            ParagonLookAndFeel::drawStampedText (g, text, where.toFloat(), juce::Justification::centred, Finish::ink());
        }
        return;
    }

    // The same dark panel and header as the other blocks' panels.
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, bounds.getY(),
                                             juce::Colour (0xff09070a), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
    if (showTitle)
    {
        Icons::draw (g, Icons::Type::doubler, header.removeFromLeft (26.0f).withSizeKeepingCentre (22.0f, 20.0f), accent, 1.6f, true);
        header.removeFromLeft (8.0f);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("DOUBLER", header, juce::Justification::centredLeft, false);
    }

    // The number boxes.
    for (const auto& box : boxes)
    {
        const auto area = box->slider.getBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff151217));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (box->slider.isMouseOverOrDragging() ? accent.withAlpha (0.6f) : ParagonColours::panelEdge);
        g.drawRoundedRectangle (area, 4.0f, 1.0f);
    }

    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (const auto& [text, where] : labels)
    {
        const auto isVoiceNumber = text.length() == 1 && juce::CharacterFunctions::isDigit (text[0]);
        g.setColour (isVoiceNumber ? accent : ParagonColours::textDim);
        g.drawFittedText (text, where, isVoiceNumber ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.6f);
    }
}
