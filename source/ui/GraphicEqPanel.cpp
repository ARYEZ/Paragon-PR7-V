#include "GraphicEqPanel.h"

#include "Parameters.h"
#include "ui/EqPresets.h"
#include "ui/Icons.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr float lowestHz = 40.0f, highestHz = 20000.0f;
    constexpr auto faderPadding = (float) ParagonLookAndFeel::eqFaderThumbRadius;   // the faders' travel is inset by this much

    void styleFader (juce::Slider& fader)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.getProperties().set (ParagonLookAndFeel::eqFaderProperty, true);
        fader.setColour (juce::Slider::trackColourId, ParagonColours::eq);
        fader.setDoubleClickReturnValue (true, 0.0);    // double-click a fader: back to 0 dB
        fader.setPopupDisplayEnabled (true, true, nullptr);
    }
}

void GraphicEqPanel::setAccent (juce::Colour colour)
{
    accent = colour;
    for (auto& fader : faders)
        fader.setColour (juce::Slider::trackColourId, accent);
    repaint();
}

GraphicEqPanel::GraphicEqPanel (juce::AudioProcessorValueTreeState& state)
    : parameters (state),
      onAttachment (state, ParamIDs::eqOn, onButton)
{
    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
    {
        auto& fader = faders[(size_t) band];
        styleFader (fader);
        fader.setTooltip (ParamIDs::eqBandNames[band] + " Hz: drag up to boost, down to cut. Double-click for 0 dB.");
        fader.onValueChange = [this] { repaint(); showMatchingPreset(); };
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, ParamIDs::eqBands[band], fader));
        addAndMakeVisible (fader);
    }

    styleFader (level);
    level.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    level.setTooltip ("The EQ's output level, to make up for boosts or cuts. Double-click for 0 dB.");
    level.onValueChange = [this] { repaint(); showMatchingPreset(); };
    levelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, ParamIDs::eqLevel, level);
    addAndMakeVisible (level);

    onButton.setColour (juce::ToggleButton::textColourId, ParagonColours::text);
    onButton.setTooltip ("Switch the EQ on or off");
    onButton.onStateChange = [this] { repaint(); };
    addAndMakeVisible (onButton);

    // Grouped: the vocal curves, then PR7-G's instrument ones (headings
    // aren't items, so an item's index is still its place in EqPresets::all).
    for (size_t i = 0; i < EqPresets::all.size(); ++i)
    {
        if (i == 0 || EqPresets::all[i].vocal != EqPresets::all[i - 1].vocal)
            presets.addSectionHeading (EqPresets::all[i].vocal ? "VOCAL" : "INSTRUMENT");
        presets.addItem (EqPresets::all[i].name, (int) i + 1);
    }
    presets.setTextWhenNothingSelected ("PRESETS");
    presets.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    presets.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    presets.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    presets.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    presets.onChange = [this]
    {
        if (const auto index = presets.getSelectedItemIndex(); index >= 0)
            applyPreset (index);
    };
    addAndMakeVisible (presets);
    showMatchingPreset();

    flat.setTooltip ("Set every band back to 0 dB");
    flat.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    flat.onClick = [this] { flatten(); };
    addAndMakeVisible (flat);
}

void GraphicEqPanel::flatten()
{
    for (const auto& id : ParamIDs::eqBands)
    {
        auto* band = parameters.getParameter (id);
        band->beginChangeGesture();
        band->setValueNotifyingHost (band->getDefaultValue());
        band->endChangeGesture();
    }
}

void GraphicEqPanel::applyPreset (int index)
{
    const auto& preset = EqPresets::all[(size_t) index];
    const auto set = [this] (const juce::String& id, float plainValue)
    {
        auto* parameter = parameters.getParameter (id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    };

    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
        set (ParamIDs::eqBands[band], preset.gainsDb[(size_t) band]);
    set (ParamIDs::eqLevel, preset.levelDb);
    set (ParamIDs::eqOn, 1.0f);   // so you hear it straight away
}

void GraphicEqPanel::showMatchingPreset()
{
    // The menu names the preset while the faders are still set to it; once
    // you move one, it's your own curve and the menu goes back to PRESETS.
    // Read the faders, not the parameters: when a fader reports a change, its
    // parameter's stored value may not have caught up yet.
    const auto matches = [&] (const EqPresets::Preset& preset)
    {
        for (size_t band = 0; band < faders.size(); ++band)
            if (std::abs ((float) faders[band].getValue() - preset.gainsDb[band]) > 0.05f)
                return false;
        return std::abs ((float) level.getValue() - preset.levelDb) <= 0.05f;
    };

    auto match = -1;
    for (size_t i = 0; i < EqPresets::all.size() && match < 0; ++i)
        if (matches (EqPresets::all[i]))
            match = (int) i;

    presets.setSelectedItemIndex (match, juce::dontSendNotification);
    presets.setTooltip (match >= 0 ? juce::String (EqPresets::all[(size_t) match].description)
                                   : juce::String ("EQ presets: common starting curves. Choose one, then tweak the faders to taste."));
}

float GraphicEqPanel::xForFrequency (float frequency) const
{
    const auto proportion = std::log (frequency / lowestHz) / std::log (highestHz / lowestHz);
    return graph.getX() + graph.getWidth() * proportion;
}

float GraphicEqPanel::yForDb (float db) const
{
    // The same scale as the faders' travel, so the curve runs through their caps.
    const auto proportion = (db + effects::GraphicEq::maxGainDb) / (2.0f * effects::GraphicEq::maxGainDb);
    return faderTrack.getBottom() - faderTrack.getHeight() * proportion;
}

void GraphicEqPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto header = area.removeFromTop (24);
    header.removeFromLeft (34);   // icon
    header.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (14.0f, juce::Font::bold), "GRAPHIC EQ") + 18);
    onButton.setBounds (header.removeFromLeft (60));
    flat.setBounds (header.withX (header.getRight() - 130 - 70).withWidth (62).withSizeKeepingCentre (62, 22));
    presets.setBounds (flat.getBounds().withX (flat.getX() - 10 - 190).withWidth (190));   // just left of FLAT
    area.removeFromTop (10);

    // LEVEL on the right, the graph and band faders across the rest.
    auto levelArea = area.removeFromRight (54);
    area.removeFromRight (12);
    area.removeFromBottom (18);   // frequency labels
    area.removeFromTop (14);      // value labels
    graph = area.toFloat();
    faderTrack = graph.reduced (0.0f, faderPadding);

    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
    {
        const auto x = xForFrequency (effects::GraphicEq::frequencies[(size_t) band]);
        faders[(size_t) band].setBounds (juce::Rectangle<int> (34, area.getHeight()).withCentre ({ juce::roundToInt (x), area.getCentreY() }));
    }
    level.setBounds (levelArea.withTrimmedTop (14).withTrimmedBottom (18).withSizeKeepingCentre (34, area.getHeight()));
}

void GraphicEqPanel::paint (juce::Graphics& g)
{
    // Same dark panel as the rest of the dock.
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, bounds.getY(),
                                             juce::Colour (0xff09070a), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Header: icon and name, as on the other blocks' panels.
    auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
    Icons::draw (g, Icons::Type::eq, header.removeFromLeft (26.0f).withSizeKeepingCentre (20.0f, 20.0f), accent, 1.6f, true);
    header.removeFromLeft (8.0f);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("GRAPHIC EQ", header, juce::Justification::centredLeft, false);

    // The graph: a dark screen with 0 / +-6 / +-12 dB lines.
    const auto on = onButton.getToggleState();
    g.setColour (juce::Colour (0xff050406));
    g.fillRoundedRectangle (graph.expanded (6.0f, 4.0f), 6.0f);
    g.setFont (juce::FontOptions (9.5f));
    for (auto db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const auto y = yForDb (db);
        g.setColour (juce::Colours::white.withAlpha (juce::exactlyEqual (db, 0.0f) ? 0.18f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (y), graph.getX(), graph.getRight());
    }

    // The response curve, filled down to 0 dB, through the faders' caps.
    std::array<float, effects::GraphicEq::numBands> gains {};
    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
        gains[(size_t) band] = (float) faders[(size_t) band].getValue();

    juce::Path curve, fill;
    const auto zeroY = yForDb (0.0f);
    fill.startNewSubPath (graph.getX(), zeroY);
    for (int step = 0; step <= (int) (graph.getWidth() / 2.0f); ++step)
    {
        const auto x = graph.getX() + (float) step * 2.0f;
        const auto frequency = lowestHz * std::pow (highestHz / lowestHz, (x - graph.getX()) / graph.getWidth());
        const auto y = yForDb (juce::jlimit (-15.0f, 15.0f, effects::GraphicEq::responseDb (gains, frequency)));
        if (step == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (graph.getRight(), zeroY);
    fill.closeSubPath();

    const auto colour = on ? accent : ParagonColours::textDim;
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, yForDb (12.0f),
                                             colour.withAlpha (0.04f), 0.0f, zeroY, false));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (on)
    {
        // A white-hot core, like the unit's neon LEDs.
        g.setColour (juce::Colour (0xffffe4dc).withAlpha (0.85f));
        g.strokePath (curve, juce::PathStrokeType (0.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Labels: each band's setting above it and its frequency below.
    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
    {
        const auto x = (float) faders[(size_t) band].getBounds().getCentreX();
        const auto db = gains[(size_t) band];
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (std::abs (db) < 0.05f ? ParagonColours::textDim : colour);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String (db, 1), juce::Rectangle<float> (50.0f, 12.0f).withCentre ({ x, graph.getY() - 9.0f }),
                    juce::Justification::centred, false);
        g.setColour (ParagonColours::text.withAlpha (0.8f));
        g.drawText (ParamIDs::eqBandNames[band], juce::Rectangle<float> (50.0f, 14.0f).withCentre ({ x, graph.getBottom() + 11.0f }),
                    juce::Justification::centred, false);
    }

    const auto levelBox = level.getBounds().toFloat();
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (ParagonColours::accent);
    g.drawText ((level.getValue() > 0.0 ? "+" : "") + juce::String (level.getValue(), 1),
                juce::Rectangle<float> (60.0f, 12.0f).withCentre ({ levelBox.getCentreX(), levelBox.getY() - 7.0f }), juce::Justification::centred, false);
    g.setColour (ParagonColours::text.withAlpha (0.8f));
    g.drawText ("LEVEL", juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ levelBox.getCentreX(), levelBox.getBottom() + 9.0f }),
                juce::Justification::centred, false);
}
