#include "MarsPanel.h"

#include "Icons.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    constexpr float lowestHz = 20.0f, highestHz = 20000.0f;

    void styleLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setMinimumHorizontalScale (0.6f);
        label.setBorderSize ({});
        label.setInterceptsMouseClicks (false, false);
    }

    // Each band's colour on the display, low to high: warm to cool.
    const juce::Colour bandColours[] = { juce::Colour (0xffff7a2e), juce::Colour (0xffffb22e), juce::Colour (0xff3ec7a7), juce::Colour (0xff5aa9ff) };
}

MarsPanel::MarsPanel (juce::AudioProcessorValueTreeState& state, std::function<float (int)> driveOfBand)
    : parameters (state), bandDrive (std::move (driveOfBand))
{
    setUp (on, "ON", "SATURATION on or off");
    on.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, ParamIDs::driveOn, on.button);
    setUp (bands, "BANDS", { "1", "2", "3", "4" }, "How many frequency bands, each saturated its own way");
    bands.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::marsBands, bands.box);

    setUp (bandOn, "ON", "This band on, or passed through clean");
    setUp (style, "STYLE", effects::Mars::styleNames, "The kind of saturation: tube, tape, transformer, amp, or an effect");
    setUp (drive, "DRIVE", "How hard the band is driven. Its level stays about the same, automatically");
    setUp (dynamics, "DYNAMICS", "Left: a gate (quiet sounds faded down). Right: pumping compression");
    setUp (feedback, "FEEDBACK", "Some of the band's output back into it, ringing at FB FREQ");
    setUp (feedbackFreq, "FB FREQ", "Where the feedback rings: like moving a mic closer to its speaker");
    setUp (mix, "MIX", "The band's clean sound against the saturated one");
    setUp (bass, "BASS", "Low shelf on the saturated sound");
    setUp (mid, "MID", "Mids (around 1 kHz) of the saturated sound");
    setUp (treble, "TREBLE", "High shelf on the saturated sound");
    setUp (presence, "PRESENCE", "Upper mids (around 4 kHz): edge and clarity");
    setUp (level, "LEVEL", "The band's level (all the way down: off)");

    setUp (input, "INPUT", "Level into MARS: more drives every band harder");
    setUp (output, "OUTPUT", "Level out of MARS");
    setUp (globalMix, "MIX", "MARS against your voice as it came in");
    setUp (quality, "QUALITY", effects::Mars::qualityNames, "Oversampling: cleaner highs with heavy drive, for more CPU");
    setUp (slope, "SLOPE", effects::Mars::slopeNames, "How steeply the bands are split");
    setUp (cross1, "XOVER 1", "Between bands 1 and 2");
    setUp (cross2, "XOVER 2", "Between bands 2 and 3");
    setUp (cross3, "XOVER 3", "Between bands 3 and 4");
    const std::pair<Knob*, const char*> globals[] = { { &input, ParamIDs::marsInput }, { &output, ParamIDs::marsOutput }, { &globalMix, ParamIDs::marsMix },
                                                      { &cross1, ParamIDs::marsCross1 }, { &cross2, ParamIDs::marsCross2 }, { &cross3, ParamIDs::marsCross3 } };
    for (auto [knob, id] : globals)
    {
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        auto* parameter = parameters.getParameter (id);
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    quality.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::marsQuality, quality.box);
    slope.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::marsSlope, slope.box);

    const char* pageNames[] = { "BANDS", "GLOBAL" };
    for (size_t i = 0; i < pages.size(); ++i)
    {
        auto& button = pages[i];
        button.setButtonText (pageNames[i]);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (5320);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        button.onClick = [this, i] { showPage ((int) i); };
        addAndMakeVisible (button);
    }

    selectBand (0);
    showPage (0);
    startTimerHz (20);
}

MarsPanel::~MarsPanel() = default;

void MarsPanel::setUp (Knob& knob, const juce::String& labelText, const juce::String& tooltip)
{
    styleLabel (knob.label, labelText);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 15);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.label);
    addAndMakeVisible (knob.slider);
}

void MarsPanel::setUp (Choice& choice, const juce::String& labelText, const juce::StringArray& items, const juce::String& tooltip)
{
    styleLabel (choice.label, labelText);
    choice.box.addItemList (items, 1);
    choice.box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    choice.box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    choice.box.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    choice.box.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    choice.box.setTooltip (tooltip);
    addAndMakeVisible (choice.label);
    addAndMakeVisible (choice.box);
}

void MarsPanel::setUp (Switch& control, const juce::String& labelText, const juce::String& tooltip)
{
    styleLabel (control.label, labelText);
    control.button.setTooltip (tooltip);
    addAndMakeVisible (control.label);
    addAndMakeVisible (control.button);
}

float MarsPanel::value (const juce::String& id) const { return parameters.getRawParameterValue (id)->load(); }

void MarsPanel::set (const juce::String& id, float plainValue)
{
    auto* parameter = parameters.getParameter (id);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    parameter->endChangeGesture();
}

int MarsPanel::numBands() const { return juce::roundToInt (value (ParamIDs::marsBands)) + 1; }

std::array<float, effects::Mars::maxBands + 1> MarsPanel::bandEdges() const
{
    // The band edges as MARS uses them: each crossover kept above the last.
    std::array<float, effects::Mars::maxBands + 1> edges {};
    edges.fill (highestHz);
    edges[0] = lowestHz;
    const char* ids[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
    auto lowest = 30.0f;
    for (int i = 0; i < numBands() - 1; ++i)
    {
        edges[(size_t) i + 1] = juce::jlimit (lowest, 19200.0f, value (ids[i]));
        lowest = edges[(size_t) i + 1] * 1.25f;
    }
    edges[(size_t) numBands()] = highestHz;
    return edges;
}

float MarsPanel::frequencyAt (float x) const
{
    const auto t = juce::jlimit (0.0f, 1.0f, (x - display.getX()) / juce::jmax (1.0f, display.getWidth()));
    return lowestHz * std::pow (highestHz / lowestHz, t);
}

float MarsPanel::xFor (float hz) const
{
    return display.getX() + display.getWidth() * std::log (juce::jlimit (lowestHz, highestHz, hz) / lowestHz) / std::log (highestHz / lowestHz);
}

int MarsPanel::bandAt (float x) const
{
    const auto edges = bandEdges();
    const auto hz = frequencyAt (x);
    for (int b = 0; b < numBands(); ++b)
        if (hz < edges[(size_t) b + 1])
            return b;
    return numBands() - 1;
}

int MarsPanel::crossoverAt (float x) const
{
    const auto edges = bandEdges();
    for (int i = 0; i < numBands() - 1; ++i)
        if (std::abs (xFor (edges[(size_t) i + 1]) - x) < 5.0f)
            return i;
    return -1;
}

void MarsPanel::selectBand (int band)
{
    selected = juce::jlimit (0, effects::Mars::maxBands - 1, band);
    bindBandControls();
    repaint();
}

void MarsPanel::bindBandControls()
{
    const auto id = [this] (const char* part) { return ParamIDs::marsBand (selected, part); };
    bandOn.attachment.reset();
    bandOn.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, id ("On"), bandOn.button);
    style.attachment.reset();
    style.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, id ("Style"), style.box);
    const std::pair<Knob*, const char*> knobs[] = { { &drive, "Drive" }, { &dynamics, "Dynamics" }, { &feedback, "Feedback" }, { &feedbackFreq, "FbFreq" },
                                                    { &mix, "Mix" }, { &bass, "Bass" }, { &mid, "Mid" }, { &treble, "Treble" },
                                                    { &presence, "Presence" }, { &level, "Level" } };
    for (auto [knob, part] : knobs)
    {
        knob->attachment.reset();
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id (part), knob->slider);
        auto* parameter = parameters.getParameter (id (part));
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
}

void MarsPanel::showPage (int newPage)
{
    page = juce::jlimit (0, 1, newPage);
    pages[(size_t) page].setToggleState (true, juce::dontSendNotification);
    const auto onBands = page == 0;
    for (auto* control : std::initializer_list<juce::Component*> { &on.label, &on.button, &bands.label, &bands.box,
                                                                   &bandOn.label, &bandOn.button, &style.label, &style.box })
        control->setVisible (onBands);
    for (auto* knob : { &drive, &dynamics, &feedback, &feedbackFreq, &mix, &bass, &mid, &treble, &presence, &level })
    {
        knob->label.setVisible (onBands);
        knob->slider.setVisible (onBands);
    }
    for (auto* knob : { &input, &output, &globalMix, &cross1, &cross2, &cross3 })
    {
        knob->label.setVisible (! onBands);
        knob->slider.setVisible (! onBands);
    }
    for (auto* choice : { &quality, &slope })
    {
        choice->label.setVisible (! onBands);
        choice->box.setVisible (! onBands);
    }
    resized();
    repaint();
}

void MarsPanel::splitBand (int band, float hz)
{
    const auto count = numBands();
    if (count >= effects::Mars::maxBands || band < 0 || band >= count)
        return;
    // Make room: the bands above move up one, and the new band copies the split one.
    for (int b = count - 1; b >= band; --b)
        for (const auto& part : ParamIDs::marsBandParts)
            set (ParamIDs::marsBand (b + 1, part), value (ParamIDs::marsBand (b, part)));
    const char* ids[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
    std::vector<float> crossovers;
    for (int i = 0; i < count - 1; ++i)
        crossovers.push_back (value (ids[i]));
    crossovers.insert (crossovers.begin() + band, hz);
    for (size_t i = 0; i < crossovers.size(); ++i)
        set (ids[i], crossovers[i]);
    set (ParamIDs::marsBands, (float) count);   // index count: count + 1 bands
    selectBand (band + 1);
}

void MarsPanel::removeBand (int band)
{
    const auto count = numBands();
    if (count <= 1 || band < 0 || band >= count)
        return;
    for (int b = band; b < count - 1; ++b)
        for (const auto& part : ParamIDs::marsBandParts)
            set (ParamIDs::marsBand (b, part), value (ParamIDs::marsBand (b + 1, part)));
    const char* ids[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
    std::vector<float> crossovers;
    for (int i = 0; i < count - 1; ++i)
        crossovers.push_back (value (ids[i]));
    crossovers.erase (crossovers.begin() + juce::jmin (band, count - 2));   // the line on its right (the top band: on its left)
    for (size_t i = 0; i < crossovers.size(); ++i)
        set (ids[i], crossovers[i]);
    set (ParamIDs::marsBands, (float) (count - 2));
    selectBand (juce::jmin (band, count - 2));
}

void MarsPanel::mouseDown (const juce::MouseEvent& event)
{
    if (page != 0 || ! display.contains (event.position))
        return;
    if (event.mods.isPopupMenu())
    {
        removeBand (bandAt (event.position.x));
        return;
    }
    draggingCrossover = crossoverAt (event.position.x);
    if (draggingCrossover >= 0)
    {
        const char* ids[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
        parameters.getParameter (ids[draggingCrossover])->beginChangeGesture();
        return;
    }
    selectBand (bandAt (event.position.x));
    draggingDriveOf = selected;
    dragStartDrive = value (ParamIDs::marsBand (selected, "Drive"));
    parameters.getParameter (ParamIDs::marsBand (selected, "Drive"))->beginChangeGesture();
}

void MarsPanel::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingCrossover >= 0)
    {
        const char* ids[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
        auto* parameter = parameters.getParameter (ids[draggingCrossover]);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (frequencyAt (event.position.x)));
        repaint();
    }
    else if (draggingDriveOf >= 0)
    {
        auto* parameter = parameters.getParameter (ParamIDs::marsBand (draggingDriveOf, "Drive"));
        const auto newDrive = juce::jlimit (0.0f, 1.0f, dragStartDrive - (float) event.getDistanceFromDragStartY() / display.getHeight());
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (newDrive));
        repaint();
    }
}

void MarsPanel::mouseUp (const juce::MouseEvent&)
{
    if (draggingCrossover >= 0)
    {
        const char* ids[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
        parameters.getParameter (ids[draggingCrossover])->endChangeGesture();
    }
    if (draggingDriveOf >= 0)
        parameters.getParameter (ParamIDs::marsBand (draggingDriveOf, "Drive"))->endChangeGesture();
    draggingCrossover = draggingDriveOf = -1;
}

void MarsPanel::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (page == 0 && display.contains (event.position) && crossoverAt (event.position.x) < 0)
        splitBand (bandAt (event.position.x), frequencyAt (event.position.x));
}

void MarsPanel::timerCallback()
{
    // The display's drive lights move with the sound; the picked band stays one that exists.
    if (selected >= numBands())
        selectBand (numBands() - 1);
    auto changed = false;
    for (int b = 0; b < effects::Mars::maxBands; ++b)
    {
        const auto now = bandDrive != nullptr ? bandDrive (b) : 0.0f;
        const auto shown = now > shownDrive[(size_t) b] ? now : shownDrive[(size_t) b] * 0.85f;
        changed = changed || std::abs (shown - shownDrive[(size_t) b]) > 0.005f;
        shownDrive[(size_t) b] = shown;
    }
    for (auto [knob, index] : { std::pair { &cross1, 1 }, std::pair { &cross2, 2 }, std::pair { &cross3, 3 } })
    {
        const auto inUse = index < numBands();
        knob->slider.setEnabled (inUse);
        knob->slider.setAlpha (inUse ? 1.0f : 0.35f);
        knob->label.setAlpha (inUse ? 1.0f : 0.35f);
    }
    if (changed && page == 0)
        repaint (display.toNearestInt().expanded (2));
}

void MarsPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto header = area.removeFromTop (24);
    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (14.0f, juce::Font::bold), "SATURATION");
    header.removeFromLeft (26 + 8 + titleWidth + 24);
    for (auto& button : pages)
    {
        button.setBounds (header.removeFromLeft (76).reduced (0, 1));
        header.removeFromLeft (4);
    }
    area.removeFromTop (10);

    const auto place = [] (juce::Component& label, juce::Component& widget, juce::Rectangle<int> cell, int widgetHeight)
    {
        label.setBounds (cell.removeFromTop (14));
        cell.removeFromTop (2);
        widget.setBounds (widgetHeight > 0 ? cell.withSizeKeepingCentre (cell.getWidth(), widgetHeight) : cell);
    };

    if (page == 0)
    {
        // Left: ON, BANDS, then the display.
        auto left = area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.42f));
        auto top = left.removeFromTop (40);
        place (on.label, on.button, top.removeFromLeft (34), 22);
        top.removeFromLeft (10);
        place (bands.label, bands.box, top.removeFromLeft (70), 24);
        left.removeFromTop (6);
        display = left.toFloat();

        // Right: the band's ON and STYLE, then two rows of knobs.
        area.removeFromLeft (14);
        auto bandRow = area.removeFromTop (40);
        bandTitle = bandRow.removeFromLeft (52);
        place (bandOn.label, bandOn.button, bandRow.removeFromLeft (34), 22);
        bandRow.removeFromLeft (8);
        place (style.label, style.box, bandRow, 24);
        area.removeFromTop (4);
        const auto rowHeight = area.getHeight() / 2;
        const std::array<std::array<Knob*, 5>, 2> rows { { { &drive, &dynamics, &feedback, &feedbackFreq, &mix },
                                                           { &bass, &mid, &treble, &presence, &level } } };
        for (const auto& row : rows)
        {
            auto line = area.removeFromTop (rowHeight);
            const auto width = line.getWidth() / 5;
            for (auto* knob : row)
                place (knob->label, knob->slider, line.removeFromLeft (width).reduced (2, 0), 0);
        }
    }
    else
    {
        // GLOBAL: one row.
        std::vector<std::pair<juce::Component*, juce::Component*>> cells {
            { &input.label, &input.slider }, { &output.label, &output.slider }, { &globalMix.label, &globalMix.slider },
            { &quality.label, &quality.box }, { &slope.label, &slope.box },
            { &cross1.label, &cross1.slider }, { &cross2.label, &cross2.slider }, { &cross3.label, &cross3.slider } };
        const auto row = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 110));
        auto line = row;
        const auto weights = std::array<float, 8> { 1, 1, 1, 1.6f, 1.6f, 1, 1, 1 };
        const auto unit = (float) line.getWidth() / 10.2f;
        for (size_t i = 0; i < cells.size(); ++i)
        {
            const auto cell = line.removeFromLeft (juce::roundToInt (unit * weights[i])).reduced (3, 0);
            const auto isChoice = i == 3 || i == 4;
            place (*cells[i].first, *cells[i].second, cell, isChoice ? 26 : 0);
        }
        display = {};
    }
}

void MarsPanel::paint (juce::Graphics& g)
{
    // The same dark panel and header as the other blocks' panels.
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, bounds.getY(),
                                             juce::Colour (0xff09070a), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
    Icons::draw (g, Icons::Type::drive, header.removeFromLeft (26.0f).withSizeKeepingCentre (22.0f, 20.0f), ParagonColours::drive, 1.6f, true);
    header.removeFromLeft (8.0f);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("SATURATION", header, juce::Justification::centredLeft, false);

    if (page != 0 || display.isEmpty())
        return;

    // The band's name beside its controls.
    g.setColour (bandColours[selected]);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("BAND " + juce::String (selected + 1), bandTitle.withTrimmedTop (14), juce::Justification::centredLeft, false);

    // The display: frequencies left to right, each band a column.
    g.setColour (juce::Colour (0xff151217));
    g.fillRoundedRectangle (display, 5.0f);
    g.setFont (juce::FontOptions (9.0f));
    for (const auto hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const auto x = xFor (hz);
        g.setColour (ParagonColours::panelEdge);
        g.drawVerticalLine (juce::roundToInt (x), display.getY() + 2.0f, display.getBottom() - 2.0f);
        g.setColour (ParagonColours::textDim.withAlpha (0.6f));
        g.drawText (hz < 1000.0f ? "100" : hz < 10000.0f ? "1k" : "10k", juce::Rectangle<float> (x + 2.0f, display.getBottom() - 13.0f, 30.0f, 11.0f),
                    juce::Justification::centredLeft, false);
    }

    const auto edges = bandEdges();
    for (int b = 0; b < numBands(); ++b)
    {
        const auto left = xFor (edges[(size_t) b]), right = xFor (edges[(size_t) b + 1]);
        auto column = juce::Rectangle<float> (left, display.getY(), right - left, display.getHeight()).reduced (1.5f, 2.0f);
        const auto colour = bandColours[b];
        const auto isOn = value (ParamIDs::marsBand (b, "On")) > 0.5f;
        g.setColour (colour.withAlpha (b == selected ? 0.16f : 0.06f));
        g.fillRoundedRectangle (column, 4.0f);
        if (b == selected)
        {
            g.setColour (colour.withAlpha (0.8f));
            g.drawRoundedRectangle (column, 4.0f, 1.2f);
        }

        // Its DRIVE as a bar from the bottom, and how hard it's driven right now as a line.
        const auto driveAmount = value (ParamIDs::marsBand (b, "Drive"));
        auto bar = column.reduced (column.getWidth() * 0.3f, 16.0f);
        g.setColour (colour.withAlpha (isOn ? 0.75f : 0.25f));
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * driveAmount), 2.0f);
        const auto live = juce::jlimit (0.0f, 1.0f, shownDrive[(size_t) b] / 4.0f);
        if (live > 0.01f && isOn)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            const auto y = bar.getBottom() - bar.getHeight() * live;
            g.drawLine (bar.getX() - 3.0f, y, bar.getRight() + 3.0f, y, 1.5f);
        }

        // Its number at the top, its style at the bottom.
        g.setColour (isOn ? colour : ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), column.withHeight (14.0f), juce::Justification::centred, false);
        if (column.getWidth() > 30.0f)
        {
            g.setFont (juce::FontOptions (9.0f));
            g.setColour (ParagonColours::textDim);
            const auto styleName = effects::Mars::styleNames[juce::roundToInt (value (ParamIDs::marsBand (b, "Style")))];
            g.drawFittedText (isOn ? styleName : "Off", column.removeFromBottom (24.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
        }
    }

    // The crossover lines, with a handle each.
    for (int i = 0; i < numBands() - 1; ++i)
    {
        const auto x = xFor (edges[(size_t) i + 1]);
        g.setColour (ParagonColours::text.withAlpha (0.7f));
        g.drawVerticalLine (juce::roundToInt (x), display.getY() + 2.0f, display.getBottom() - 2.0f);
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 3.0f, display.getCentreY() - 9.0f, 6.0f, 18.0f), 2.0f);
    }
}
