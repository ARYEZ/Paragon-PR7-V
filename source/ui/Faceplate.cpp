#include "Faceplate.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "dsp/effects/CompGate.h"
#include "dsp/effects/Mars.h"
#include "ui/DockPanels.h"

namespace
{
    juce::Font font (float height, bool bold = true) { return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain)); }

    void screw (juce::Graphics& g, juce::Point<float> centre, float radius)
    {
        const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb9b4bb), head.getX(), head.getY(),
                                                 juce::Colour (0xff4a464c), head.getRight(), head.getBottom(), false));
        g.fillEllipse (head);
        g.setColour (juce::Colour (0xff2a272c));
        g.drawLine (centre.x - radius * 0.65f, centre.y + radius * 0.3f, centre.x + radius * 0.65f, centre.y - radius * 0.3f, 1.2f);
    }

    // A 19-inch rack unit: the metal, its ears and their screws. Returns the
    // front panel between the ears.
    juce::Rectangle<float> rackUnit (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour top, juce::Colour bottom)
    {
        g.setGradientFill (juce::ColourGradient (top, 0.0f, area.getY(), bottom, 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle (area, 5.0f);
        // Brushed lines, very faint.
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (auto y = area.getY() + 3.0f; y < area.getBottom() - 3.0f; y += 3.0f)
            g.drawHorizontalLine ((int) y, area.getX() + 4.0f, area.getRight() - 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawLine (area.getX() + 5.0f, area.getY() + 0.8f, area.getRight() - 5.0f, area.getY() + 0.8f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

        auto inner = area;
        for (auto ear : { inner.removeFromLeft (20.0f), inner.removeFromRight (20.0f) })
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRect (ear.reduced (0.0f, 2.0f));
            screw (g, { ear.getCentreX(), ear.getY() + 18.0f }, 4.5f);
            screw (g, { ear.getCentreX(), ear.getBottom() - 18.0f }, 4.5f);
        }
        return inner.reduced (8.0f, 10.0f);
    }

    // A small lamp: lit, a glow in `colour`; off, dark glass.
    void lamp (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, bool lit)
    {
        const auto glass = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        if (lit)
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f), centre.x, centre.y,
                                                     colour.withAlpha (0.0f), centre.x + radius * 3.2f, centre.y, true));
            g.fillEllipse (glass.expanded (radius * 2.2f));
        }
        g.setGradientFill (juce::ColourGradient (lit ? colour.brighter (0.6f) : colour.darker (2.6f), centre.x - radius * 0.3f, centre.y - radius * 0.4f,
                                                 lit ? colour : colour.darker (3.5f), glass.getRight(), glass.getBottom(), true));
        g.fillEllipse (glass);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (glass, 1.0f);
    }

    // A dark readout window with a small label above its value.
    void readout (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label, const juce::String& value, juce::Colour ink)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (font (9.5f));
        g.drawText (label, area.removeFromTop (12.0f), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff070607));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (ink);
        g.setFont (font (13.0f));
        g.drawText (value, area.reduced (6.0f, 0.0f), juce::Justification::centredRight, false);
    }

    // A small knob seen from the front, pointing at `position` (0 to 1).
    void knob (juce::Graphics& g, juce::Point<float> centre, float radius, float position, juce::Colour pointer)
    {
        const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (body.translated (0.0f, 2.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a363c), body.getX(), body.getY(),
                                                 juce::Colour (0xff0d0c0e), body.getRight(), body.getBottom(), false));
        g.fillEllipse (body);
        const auto angle = juce::MathConstants<float>::pi * (-0.75f + 1.5f * juce::jlimit (0.0f, 1.0f, position));
        g.setColour (pointer);
        g.drawLine (juce::Line<float> (centre, centre.getPointOnCircumference (radius * 0.85f, angle)), 2.2f);
    }
}

//==============================================================================
Faceplate::Faceplate (ParagonX1AudioProcessor& processorToShow)
    : processor (processorToShow), parameters (processorToShow.getParameters())
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void Faceplate::setBlock (Unit newUnit, const juce::String& newTitle, Icons::Type newIcon, juce::Colour newColour,
                          const juce::String& newOnIds, const juce::String& newTypeId)
{
    unit = newUnit;
    title = newTitle;
    icon = newIcon;
    colour = newColour;
    onIds = juce::StringArray::fromTokens (newOnIds, ",", {});
    typeId = newTypeId;
    repaint();
}

Faceplate::Look Faceplate::getLook() const
{
    if (unit == Unit::saturation)
        return Look::mars;
    if (unit == Unit::dynamics)
    {
        switch ((effects::CompGate::Type) juce::roundToInt (raw (ParamIDs::compType)))
        {
            case effects::CompGate::Type::daybreak: return Look::daybreak;
            case effects::CompGate::Type::revival:  return Look::revival;
            case effects::CompGate::Type::classic:  break;
        }
        return Look::classic;
    }
    return Look::generic;
}

float Faceplate::raw (const juce::String& id) const { return parameters.getRawParameterValue (id)->load(); }

float Faceplate::normalised (const juce::String& id) const { return parameters.getParameter (id)->getValue(); }

juce::String Faceplate::text (const juce::String& id) const { return parameters.getParameter (id)->getCurrentValueAsText(); }

bool Faceplate::isOn() const
{
    for (const auto& id : onIds)
        if (raw (id) > 0.5f)
            return true;
    return false;
}

void Faceplate::timerCallback()
{
    if (! isShowing())
        return;
    updateMeters();
    repaint();
}

void Faceplate::updateMeters()
{
    // A needle's ballistics: quick to swing down, slower to come back.
    const auto reduction = juce::jmin (0.0f, processor.getCompGainReductionDb());
    shownReduction += (reduction < shownReduction ? 0.6f : 0.15f) * (reduction - shownReduction);
    for (int band = 0; band < (int) shownDrive.size(); ++band)
    {
        const auto drive = juce::jmax (0.0f, processor.getMarsBandDrive (band));
        auto& shown = shownDrive[(size_t) band];
        shown += (drive > shown ? 0.5f : 0.12f) * (drive - shown);
    }
}

//==============================================================================
void Faceplate::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    auto area = getLocalBounds().reduced (16, 10);
    DockStyle::drawTitle (g, area.removeFromTop (18), title);
    area.removeFromTop (6);
    const auto unitArea = area.toFloat();
    switch (getLook())
    {
        case Look::classic:  paintClassic (g, unitArea); break;
        case Look::daybreak: paintDaybreak (g, unitArea); break;
        case Look::revival:  paintRevival (g, unitArea); break;
        case Look::mars:     paintMars (g, unitArea); break;
        case Look::generic:  paintGeneric (g, unitArea); break;
    }
}

void Faceplate::paintGeneric (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto panel = rackUnit (g, area, juce::Colour (0xff2b282d), juce::Colour (0xff18161a));
    const auto on = isOn();
    // The block's icon and name, its TYPE in a window, and the ON lamp.
    auto left = panel.removeFromLeft (panel.getHeight());
    Icons::draw (g, icon, left.reduced (left.getWidth() * 0.22f), on ? colour : colour.withAlpha (0.35f), 2.4f, true);
    panel.removeFromLeft (12.0f);
    auto right = panel.removeFromRight (60.0f);
    lamp (g, right.getCentre().translated (0.0f, -8.0f), 7.0f, colour, on);
    g.setColour (ParagonColours::textDim);
    g.setFont (font (9.5f));
    g.drawText ("ON", right.withTrimmedTop (right.getHeight() * 0.5f + 6.0f).removeFromTop (14.0f), juce::Justification::centred, false);

    g.setColour (ParagonColours::text);
    g.setFont (font (22.0f));
    g.drawText (title, panel.removeFromTop (panel.getHeight() * 0.45f), juce::Justification::bottomLeft, false);
    panel.removeFromTop (8.0f);
    if (typeId.isNotEmpty())
        readout (g, panel.removeFromTop (42.0f).withWidth (juce::jmin (panel.getWidth(), 220.0f)), "TYPE", text (typeId), colour);
}

void Faceplate::paintClassic (juce::Graphics& g, juce::Rectangle<float> area)
{
    // A studio rack compressor: the name, a gain-reduction needle, its settings.
    auto panel = rackUnit (g, area, juce::Colour (0xff3a383c), juce::Colour (0xff222024));
    const auto on = isOn();

    auto left = panel.removeFromLeft (panel.getWidth() * 0.27f);
    g.setColour (juce::Colour (0xffe9e3d6));
    g.setFont (font (24.0f));
    g.drawText ("CLASSIC", left.removeFromTop (left.getHeight() * 0.45f), juce::Justification::bottomLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (font (9.5f));
    g.drawText ("STUDIO COMPRESSOR", left.removeFromTop (16.0f), juce::Justification::topLeft, false);
    lamp (g, { left.getX() + 9.0f, left.getY() + 26.0f }, 6.0f, juce::Colour (0xffff3b30), on);
    g.drawText ("POWER", juce::Rectangle<float> (left.getX() + 22.0f, left.getY() + 19.0f, 60.0f, 14.0f), juce::Justification::centredLeft, false);

    // The VU-style meter: cream face, gain reduction 0 (right) to 20 dB (left).
    auto right = panel.removeFromRight (panel.getWidth() * 0.34f);
    panel.reduce (10.0f, 0.0f);
    const auto face = panel.withSizeKeepingCentre (panel.getWidth(), juce::jmin (panel.getHeight(), panel.getWidth() * 0.62f));
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (face.expanded (3.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4ecd6), face.getCentreX(), face.getY(),
                                             juce::Colour (0xffd8ccae), face.getCentreX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 3.0f);
    const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.3f);
    const auto radius = face.getHeight() * 1.08f;
    const auto angleFor = [] (float db) { return 0.58f - 1.16f * std::pow (juce::jlimit (0.0f, 1.0f, db / 20.0f), 0.7f); };   // 0 dB right
    g.setColour (juce::Colour (0xff2b2622));
    g.setFont (font (9.0f));
    for (auto db : { 0, 1, 2, 3, 5, 7, 10, 15, 20 })
    {
        const auto angle = angleFor ((float) db);
        g.drawLine (juce::Line<float> (pivot.getPointOnCircumference (radius * 0.93f, angle), pivot.getPointOnCircumference (radius, angle)), 1.2f);
        const auto at = pivot.getPointOnCircumference (radius * 0.81f, angle);
        g.drawText (juce::String (db), juce::Rectangle<float> (20.0f, 10.0f).withCentre (at), juce::Justification::centred, false);
    }
    g.setFont (font (8.5f));
    g.drawText ("GAIN REDUCTION  dB", face.withTrimmedTop (face.getHeight() * 0.62f).removeFromTop (12.0f), juce::Justification::centred, false);
    const auto needle = angleFor (on ? -shownReduction : 0.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (face.toNearestInt());
        g.setColour (juce::Colour (0xff141210));
        g.drawLine (juce::Line<float> (pivot, pivot.getPointOnCircumference (radius * 0.98f, needle)), 1.6f);
    }

    // The settings, in windows.
    right.removeFromLeft (8.0f);
    const auto row = (right.getHeight() - 8.0f) / 3.0f;
    readout (g, right.removeFromTop (row - 4.0f), "THRESHOLD", text (ParamIDs::compThreshold), ParagonColours::accent);
    right.removeFromTop (4.0f);
    readout (g, right.removeFromTop (row - 4.0f), "RATIO", text (ParamIDs::compRatio), ParagonColours::accent);
    right.removeFromTop (4.0f);
    readout (g, right.removeFromTop (row - 4.0f), "LEVEL", text (ParamIDs::compLevel), ParagonColours::accent);
}

void Faceplate::paintDaybreak (juce::Graphics& g, juce::Rectangle<float> area)
{
    // A boutique pedal, on its side: a sunrise on the enclosure, its knobs,
    // INTENSITY, an LED ladder for the gain reduction and its footswitch.
    const auto on = isOn();
    const auto box = area.reduced (4.0f, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (box.translated (0.0f, 3.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1f4d), 0.0f, box.getY(),
                                             juce::Colour (0xffe8572e), 0.0f, box.getBottom(), false));
    g.fillRoundedRectangle (box, 12.0f);
    {
        // The sun on the horizon, with its rays.
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path shape;
        shape.addRoundedRectangle (box, 12.0f);
        g.reduceClipRegion (shape);
        const juce::Point<float> sun (box.getX() + box.getWidth() * 0.3f, box.getBottom() - box.getHeight() * 0.12f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd27a), sun.x, sun.y, juce::Colour (0x00ffb04a), sun.x + box.getHeight() * 0.9f, sun.y, true));
        g.fillEllipse (juce::Rectangle<float> (box.getHeight() * 1.8f, box.getHeight() * 1.8f).withCentre (sun));
        g.setColour (juce::Colour (0xffffe0a0));
        g.fillEllipse (juce::Rectangle<float> (box.getHeight() * 0.5f, box.getHeight() * 0.5f).withCentre (sun));
        g.setColour (juce::Colour (0x66ffffff));
        for (int ray = 0; ray < 9; ++ray)
        {
            const auto angle = juce::MathConstants<float>::pi * (-0.5f + (float) ray / 8.0f);
            g.drawLine (juce::Line<float> (sun.getPointOnCircumference (box.getHeight() * 0.32f, angle),
                                           sun.getPointOnCircumference (box.getHeight() * 0.62f, angle)), 1.4f);
        }
        g.setColour (juce::Colour (0xff120f22).withAlpha (0.85f));
        g.fillRect (box.withTop (sun.y));
    }
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRoundedRectangle (box.reduced (0.5f), 12.0f, 1.0f);

    auto inner = box.reduced (18.0f, 12.0f);
    // Name, top left.
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold | juce::Font::italic)));
    g.drawText ("DAYBREAK", inner.withHeight (34.0f), juce::Justification::topLeft, false);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (font (9.5f));
    g.drawText ("COMPRESSOR", inner.withTrimmedTop (34.0f).withHeight (12.0f), juce::Justification::topLeft, false);

    // Knobs, top right: COMP, DETAIL, MIX.
    auto knobs = inner.removeFromTop (inner.getHeight() * 0.5f).removeFromRight (inner.getWidth() * 0.5f);
    const auto knobWidth = knobs.getWidth() / 3.0f;
    for (auto [id, name] : { std::pair { ParamIDs::compAmount, "COMP" }, std::pair { ParamIDs::compDetail, "DETAIL" },
                             std::pair { ParamIDs::compMix, "MIX" } })
    {
        auto cell = knobs.removeFromLeft (knobWidth);
        knob (g, cell.getCentre().translated (0.0f, -6.0f), 17.0f, normalised (id), juce::Colour (0xfff1e6d0));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (font (9.5f));
        g.drawText (name, cell.removeFromBottom (14.0f), juce::Justification::centred, false);
    }

    // Bottom: INTENSITY, the LED ladder, the status LED.
    inner.removeFromTop (8.0f);
    auto bottom = inner;
    const auto intense = raw (ParamIDs::compIntensity) > 0.5f;
    auto toggle = bottom.removeFromLeft (70.0f);
    const auto slot = juce::Rectangle<float> (12.0f, 26.0f).withCentre (toggle.getCentre().translated (0.0f, -6.0f));
    g.setColour (juce::Colour (0xff0b0a0c));
    g.fillRoundedRectangle (slot, 5.0f);
    g.setColour (juce::Colour (0xffd9d5dc));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ slot.getCentreX(), intense ? slot.getY() + 6.0f : slot.getBottom() - 6.0f }));
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (font (9.0f));
    g.drawText ("INTENSITY", toggle.removeFromBottom (14.0f), juce::Justification::centred, false);

    auto status = bottom.removeFromRight (60.0f);
    lamp (g, status.getCentre().translated (0.0f, -6.0f), 7.0f, juce::Colour (0xffff4a3a), on);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawText ("ON", status.removeFromBottom (14.0f), juce::Justification::centred, false);

    bottom.reduce (14.0f, 0.0f);
    auto ladder = bottom.withSizeKeepingCentre (bottom.getWidth(), 18.0f).translated (0.0f, -6.0f);
    const auto leds = 10;
    const auto ledWidth = ladder.getWidth() / (float) leds;
    for (int led = 0; led < leds; ++led)
    {
        const auto threshold = 2.0f * (float) (led + 1);   // 2, 4 ... 20 dB
        const auto lit = on && -shownReduction >= threshold - 1.0f;
        const auto ledColour = led < 5 ? juce::Colour (0xff5dff8a) : led < 8 ? juce::Colour (0xffffc53a) : juce::Colour (0xffff4a3a);
        const auto cell = ladder.removeFromLeft (ledWidth).reduced (2.0f, 0.0f);
        g.setColour (lit ? ledColour : ledColour.darker (3.0f).withAlpha (0.8f));
        g.fillRoundedRectangle (cell, 2.0f);
        if (lit)
        {
            g.setColour (ledColour.withAlpha (0.35f));
            g.fillRoundedRectangle (cell.expanded (2.0f), 3.0f);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawText ("GAIN REDUCTION", bottom.removeFromBottom (14.0f), juce::Justification::centred, false);
}

void Faceplate::paintRevival (juce::Graphics& g, juce::Rectangle<float> area)
{
    // A vocal leveller: its three moves drawn as one curve (input to
    // output), with GATE, COMPRESSION and GAIN as faders and a meter.
    auto panel = rackUnit (g, area, juce::Colour (0xff1f2328), juce::Colour (0xff111316));
    const auto on = isOn();
    g.setColour (colour);
    g.fillRect (panel.getX(), panel.getY() - 6.0f, panel.getWidth(), 2.0f);

    auto header = panel.removeFromTop (22.0f);
    g.setColour (juce::Colour (0xffeef1f4));
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::plain)).withExtraKerningFactor (0.25f));
    g.drawText ("REVIVAL", header, juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (font (9.5f));
    g.drawText ("VOCAL LEVELLER", header, juce::Justification::centredRight, false);
    panel.removeFromTop (6.0f);

    // The curve: input (across) to output (up), -60 to 0 dB.
    const auto gateDb = raw (ParamIDs::revivalGate);
    const auto compressionDb = raw (ParamIDs::revivalCompression);
    const auto gainDb = raw (ParamIDs::revivalGain);
    auto graph = panel.removeFromLeft (panel.getHeight() * 1.25f);
    g.setColour (juce::Colour (0xff08090b));
    g.fillRoundedRectangle (graph, 3.0f);
    graph = graph.reduced (6.0f);
    const auto lowest = -60.0f;
    const auto xFor = [&graph, lowest] (float db) { return graph.getX() + graph.getWidth() * (db - lowest) / -lowest; };
    const auto yFor = [&graph, lowest] (float db) { return graph.getBottom() - graph.getHeight() * (juce::jlimit (lowest, 0.0f, db) - lowest) / -lowest; };
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (auto db = -48.0f; db < 0.0f; db += 12.0f)
    {
        g.drawVerticalLine ((int) xFor (db), graph.getY(), graph.getBottom());
        g.drawHorizontalLine ((int) yFor (db), graph.getX(), graph.getRight());
    }
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawLine (xFor (lowest), yFor (lowest), xFor (0.0f), yFor (0.0f), 1.0f);   // unity
    juce::Path curve;
    for (auto in = lowest; in <= 0.0f; in += 0.5f)
    {
        auto out = in + effects::CompGate::revivalCurveDb (in, compressionDb);
        if (gateDb > effects::CompGate::revivalGateOffDb && in < gateDb)
            out -= (gateDb - in);   // a gentle 2:1 expander below GATE
        out = juce::jmin (out, gainDb);   // nothing passes GAIN
        if (in <= lowest)
            curve.startNewSubPath (xFor (in), yFor (out));
        else
            curve.lineTo (xFor (in), yFor (out));
    }
    g.setColour (on ? colour : ParagonColours::textDim);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
    g.setColour (ParagonColours::textDim.withAlpha (0.7f));
    if (gateDb > effects::CompGate::revivalGateOffDb)
    {
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (xFor (gateDb), graph.getY(), xFor (gateDb), graph.getBottom()), dashes, 2, 1.0f);
    }
    g.setFont (font (8.5f));
    g.drawText ("IN", graph.removeFromBottom (11.0f), juce::Justification::centredRight, false);

    // Faders: GATE, COMPRESSION, GAIN; then the reduction meter.
    panel.removeFromLeft (14.0f);
    auto meter = panel.removeFromBottom (26.0f);
    const auto faderWidth = panel.getWidth() / 3.0f;
    for (auto [id, name] : { std::pair { ParamIDs::revivalGate, "GATE" }, std::pair { ParamIDs::revivalCompression, "COMPRESSION" },
                             std::pair { ParamIDs::revivalGain, "GAIN" } })
    {
        auto cell = panel.removeFromLeft (faderWidth);
        g.setColour (ParagonColours::textDim);
        g.setFont (font (8.5f));
        g.drawText (name, cell.removeFromBottom (12.0f), juce::Justification::centred, false);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawText (text (id), cell.removeFromTop (12.0f), juce::Justification::centred, false);
        const auto track = cell.withSizeKeepingCentre (4.0f, cell.getHeight() - 10.0f);
        g.setColour (juce::Colour (0xff050607));
        g.fillRoundedRectangle (track, 2.0f);
        const auto capY = track.getBottom() - track.getHeight() * normalised (id);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9dde2), 0.0f, capY - 5.0f, juce::Colour (0xff7d8288), 0.0f, capY + 5.0f, false));
        g.fillRoundedRectangle (juce::Rectangle<float> (26.0f, 10.0f).withCentre ({ track.getCentreX(), capY }), 2.0f);
        g.setColour (colour);
        g.drawHorizontalLine ((int) capY, track.getCentreX() - 11.0f, track.getCentreX() + 11.0f);
    }
    meter.removeFromTop (8.0f);
    g.setColour (ParagonColours::textDim);
    g.setFont (font (8.5f));
    g.drawText ("REDUCTION", meter.removeFromLeft (64.0f), juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xff050607));
    g.fillRoundedRectangle (meter, 2.0f);
    const auto amount = on ? juce::jlimit (0.0f, 1.0f, -shownReduction / 24.0f) : 0.0f;
    g.setColour (colour);
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * amount), 2.0f);
}

void Faceplate::paintMars (juce::Graphics& g, juce::Rectangle<float> area)
{
    // A valve unit: a tube for each band, glowing as hard as it's driven,
    // with the band's STYLE under it.
    auto panel = rackUnit (g, area, juce::Colour (0xff3a1914), juce::Colour (0xff1c0b09));
    const auto on = isOn();
    auto header = panel.removeFromTop (22.0f);
    g.setColour (juce::Colour (0xfff3d9c4));
    g.setFont (font (20.0f));
    g.drawText ("MARS", header, juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xffc99b86));
    g.setFont (font (9.5f));
    g.drawText ("MULTIBAND SATURATION", header, juce::Justification::centredRight, false);
    panel.removeFromTop (6.0f);

    const auto bands = juce::jlimit (1, effects::Mars::maxBands, juce::roundToInt (raw (ParamIDs::marsBands)) + 1);
    const auto cellWidth = panel.getWidth() / (float) bands;
    for (int band = 0; band < bands; ++band)
    {
        auto cell = panel.removeFromLeft (cellWidth).reduced (6.0f, 0.0f);
        const auto bandOn = on && raw (ParamIDs::marsBand (band, "On")) > 0.5f;
        g.setColour (juce::Colour (0xffd8b6a4));
        g.setFont (font (9.0f));
        const auto style = juce::jlimit (0, effects::Mars::styleNames.size() - 1, juce::roundToInt (raw (ParamIDs::marsBand (band, "Style"))));
        g.drawFittedText (effects::Mars::styleNames[style].toUpperCase(), cell.removeFromBottom (24.0f).toNearestInt(), juce::Justification::centred, 2);

        // The tube: glass, plates and a glowing filament.
        auto tube = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() * 0.62f, 54.0f), cell.getHeight() - 4.0f);
        const auto glow = bandOn ? juce::jlimit (0.0f, 1.0f, 0.15f + 0.85f * shownDrive[(size_t) band]) : 0.0f;
        auto glass = tube.withTrimmedBottom (10.0f);
        g.setColour (juce::Colour (0xff111111));
        g.fillRoundedRectangle (tube.removeFromBottom (12.0f).reduced (4.0f, 0.0f), 2.0f);   // its base
        if (glow > 0.0f)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff7a2a).withAlpha (0.55f * glow), glass.getCentreX(), glass.getCentreY() + glass.getHeight() * 0.1f,
                                                     juce::Colour (0x00ff7a2a), glass.getCentreX() + glass.getWidth() * 1.2f, glass.getCentreY(), true));
            g.fillEllipse (glass.expanded (glass.getWidth() * 0.5f, 6.0f));
        }
        g.setColour (juce::Colour (0x22c9e6ff));
        g.fillRoundedRectangle (glass, glass.getWidth() * 0.5f);
        g.setColour (juce::Colour (0x44ffffff));
        g.drawRoundedRectangle (glass, glass.getWidth() * 0.5f, 1.0f);
        const auto plates = glass.withSizeKeepingCentre (glass.getWidth() * 0.55f, glass.getHeight() * 0.42f).translated (0.0f, glass.getHeight() * 0.08f);
        g.setColour (juce::Colour (0xff3d3a3a));
        g.fillRect (plates);
        g.setColour (juce::Colour (0xffffb36b).withAlpha (glow > 0.0f ? juce::jmin (1.0f, 0.4f + glow) : 0.1f));
        g.fillRect (plates.withSizeKeepingCentre (3.0f, plates.getHeight() * 0.8f));
        g.setColour (juce::Colour (0x55ffffff));
        g.drawLine (glass.getX() + glass.getWidth() * 0.28f, glass.getY() + glass.getWidth() * 0.4f,
                    glass.getX() + glass.getWidth() * 0.28f, glass.getBottom() - glass.getWidth() * 0.4f, 1.6f);   // a reflection
        g.setColour (ParagonColours::textDim);
        g.setFont (font (8.5f));
        g.drawText (juce::String (band + 1), glass.removeFromTop (14.0f).translated (0.0f, -14.0f), juce::Justification::centred, false);
    }
}
