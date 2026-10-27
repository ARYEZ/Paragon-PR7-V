#include "Faceplate.h"

#include "Finish.h"
#include "LedDisplay.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "PedalBody.h"
#include "dsp/effects/CompGate.h"
#include "dsp/effects/Mars.h"

// Every part here is one of the PR7's own: the unit's pearl chassis
// (PedalBody::paintUnit), its LCD screens (LedDisplay::paintScreen, the LED
// orange), big knobs, small keys with their light stripes, EQ faders,
// neon LED bars, and lettering stamped in ink or printed in red, all
// following the finish.
namespace
{
    const PedalBody::Shape unitShape { 16.0f, 10.0f, 5.0f, 0.3f };
    constexpr float glowRoom = 7.0f;        // round the unit, for its light
    constexpr float screenSize = 0.45f;     // the small screens' frame and bezel, against the main display's
    const auto orange = LedDisplay::ledOrange;

    juce::FontOptions font (float height, int style = juce::Font::bold) { return juce::FontOptions (height, style); }

    void stamped (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                  juce::Justification justification = juce::Justification::centred)
    {
        g.setFont (font (height));
        ParagonLookAndFeel::drawStampedText (g, text, area, justification, Finish::ink());
    }

    // The model's name, as PARAGON is on the badge, with a red printed line
    // under it. Returns how wide the name is.
    float nameplate (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name, const juce::String& line)
    {
        const auto nameFont = font (area.getHeight() * 0.62f, juce::Font::bold | juce::Font::italic);
        g.setFont (nameFont);
        ParagonLookAndFeel::drawStampedText (g, name, area.removeFromTop (area.getHeight() * 0.66f), juce::Justification::bottomLeft, Finish::ink());
        g.setColour (Finish::printedRed());
        g.setFont (font (area.getHeight() * 0.72f));
        g.drawText (line, area, juce::Justification::centredLeft, false);
        return (float) juce::GlyphArrangement::getStringWidthInt (nameFont, name);
    }

    // A PR7 screen: returns its glass, to draw on before `finish`.
    juce::Rectangle<float> screen (juce::Graphics& g, juce::Rectangle<float> area) { return LedDisplay::paintScreen (g, area, screenSize); }
    void finish (juce::Graphics& g, juce::Rectangle<float> glass) { LedDisplay::paintGlassOverlay (g, glass, screenSize); }

    // A meter on the glass, the tuner's way: a row of LED bars, lit up to `amount` (0 to 1).
    void bars (juce::Graphics& g, juce::Rectangle<float> area, float amount, int count)
    {
        const auto width = area.getWidth() / (float) count;
        const auto lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, amount) * (float) count);
        for (int i = 0; i < count; ++i)
        {
            const auto bar = juce::Rectangle<float> (area.getX() + width * (float) i, area.getY(), width, area.getHeight()).reduced (width * 0.22f, 0.0f);
            if (i < lit)
            {
                g.setColour (orange.withAlpha (0.3f));
                g.fillRoundedRectangle (bar.expanded (2.0f), 2.0f);
                g.setColour (orange);
            }
            else
            {
                g.setColour (orange.withAlpha (0.1f));
            }
            g.fillRoundedRectangle (bar, 1.5f);
        }
    }

    // A small screen with a label stamped beside it and its value in orange.
    void readout (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label, const juce::String& value)
    {
        stamped (g, label, area.removeFromLeft (64.0f), 8.0f, juce::Justification::centredLeft);
        const auto glass = screen (g, area);
        g.setColour (orange);
        g.setFont (font (glass.getHeight() * 0.62f));
        g.drawText (value, glass.reduced (6.0f, 0.0f), juce::Justification::centredRight, false);
        finish (g, glass);
    }

    // The angle a knob points at for `position` (0 to 1), as the unit's knobs turn.
    float knobAngle (float position) { return juce::MathConstants<float>::pi * (1.2f + 1.6f * juce::jlimit (0.0f, 1.0f, position)); }
}

//==============================================================================
Faceplate::Faceplate (ParagonX1AudioProcessor& processorToShow)
    : processor (processorToShow), parameters (processorToShow.getParameters())
{
    setInterceptsMouseClicks (false, false);
    Finish::changes().addChangeListener (this);
    startTimerHz (30);
}

Faceplate::~Faceplate()
{
    Finish::changes().removeChangeListener (this);
}

void Faceplate::resized()
{
    // Twice the size, so the grain stays fine on high-resolution screens (as on the unit).
    satin = Finish::makeSatinGrain (juce::jmax (1, getWidth() * 2), juce::jmax (1, getHeight() * 2), 4321);
}

void Faceplate::changeListenerCallback (juce::ChangeBroadcaster*)
{
    resized();   // the grain differs on black
    repaint();
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
    // The unit: the PR7's chassis, smaller, in the current finish.
    const auto chassis = getLocalBounds().toFloat().reduced (glowRoom);
    const auto face = chassis.withTrimmedBottom (unitShape.frontEdge).reduced (9.0f);
    PedalBody::paintUnit (g, chassis, face, [this] (juce::Graphics& gg) { gg.drawImage (satin, getLocalBounds().toFloat()); }, unitShape);

    // Its name top left, its status LED top right (as over the footswitches).
    auto content = face.reduced (12.0f, 8.0f);
    auto header = content.removeFromTop (30.0f);
    ParagonLookAndFeel::drawNeonBar (g, header.removeFromRight (34.0f).withSizeKeepingCentre (30.0f, 6.0f).translated (0.0f, -6.0f), isOn());
    content.removeFromTop (6.0f);
    switch (getLook())
    {
        case Look::classic:  paintClassic (g, header, content); break;
        case Look::daybreak: paintDaybreak (g, header, content); break;
        case Look::revival:  paintRevival (g, header, content); break;
        case Look::mars:     paintMars (g, header, content); break;
        case Look::generic:  paintGeneric (g, header, content); break;
    }
}

void Faceplate::paintGeneric (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // The block's name and icon, and its TYPE on a screen.
    nameplate (g, header, title, "VOCAL MULTI-FX PROCESSOR");
    Icons::draw (g, icon, body.removeFromLeft (body.getHeight()).reduced (body.getHeight() * 0.14f), Finish::ink(), 2.2f, true);
    body.removeFromLeft (14.0f);
    const auto glass = screen (g, body.withSizeKeepingCentre (body.getWidth(), juce::jmin (body.getHeight(), 70.0f)));
    auto inside = glass.reduced (glass.getWidth() * 0.05f, glass.getHeight() * 0.16f);
    g.setColour (orange.withAlpha (0.7f));
    g.setFont (font (inside.getHeight() * 0.24f));
    g.drawText (typeId.isNotEmpty() ? "TYPE" : "STATUS", inside.removeFromTop (inside.getHeight() * 0.32f), juce::Justification::topLeft, false);
    g.setColour (orange.withAlpha (isOn() ? 1.0f : 0.45f));
    g.setFont (font (inside.getHeight() * 0.5f));
    g.drawFittedText ((typeId.isNotEmpty() ? text (typeId) : juce::String (isOn() ? "ON" : "OFF")).toUpperCase(),
                      inside.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    finish (g, glass);
}

void Faceplate::paintClassic (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // A studio compressor: a gain-reduction needle on a screen, and its settings.
    nameplate (g, header, "CLASSIC", "STUDIO COMPRESSOR");
    const auto on = isOn();

    auto settings = body.removeFromRight (body.getWidth() * 0.46f);
    body.removeFromRight (14.0f);

    // The meter: the needle and scale drawn in the screen's LED orange.
    const auto glass = screen (g, body);
    const juce::Point<float> pivot (glass.getCentreX(), glass.getBottom() + glass.getHeight() * 0.25f);
    const auto radius = glass.getHeight() * 1.0f;
    const auto angleFor = [] (float db) { return 0.62f - 1.24f * std::pow (juce::jlimit (0.0f, 1.0f, db / 20.0f), 0.7f); };   // 0 dB right
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (glass.toNearestInt());
        g.setFont (font (juce::jmax (7.0f, glass.getHeight() * 0.085f)));
        for (auto db : { 0, 1, 2, 3, 5, 7, 10, 15, 20 })
        {
            const auto angle = angleFor ((float) db);
            const auto numbered = db == 0 || db == 3 || db == 5 || db == 10 || db == 20;
            g.setColour (orange.withAlpha (0.75f));
            g.drawLine (juce::Line<float> (pivot.getPointOnCircumference (radius * (numbered ? 0.88f : 0.92f), angle),
                                           pivot.getPointOnCircumference (radius * 0.98f, angle)), numbered ? 1.4f : 1.0f);
            if (numbered)
                g.drawText (juce::String (db), juce::Rectangle<float> (16.0f, 9.0f).withCentre (pivot.getPointOnCircumference (radius * 0.8f, angle)),
                            juce::Justification::centred, false);
        }
        g.setColour (orange.withAlpha (0.6f));
        g.drawText ("GAIN REDUCTION dB", glass.reduced (8.0f, 5.0f), juce::Justification::bottomLeft, false);
        const auto needle = juce::Line<float> (pivot, pivot.getPointOnCircumference (radius * 0.98f, angleFor (on ? -shownReduction : 0.0f)));
        g.setColour (orange.withAlpha (0.3f));
        g.drawLine (needle, 4.0f);
        g.setColour (orange);
        g.drawLine (needle, 1.6f);
    }
    finish (g, glass);

    // THRESHOLD, RATIO and LEVEL, each on its own small screen.
    const auto row = settings.getHeight() / 3.0f;
    for (auto [id, name] : { std::pair { ParamIDs::compThreshold, "THRESHOLD" }, std::pair { ParamIDs::compRatio, "RATIO" },
                             std::pair { ParamIDs::compLevel, "LEVEL" } })
        readout (g, settings.removeFromTop (row).reduced (0.0f, 3.0f), name, text (id));
}

void Faceplate::paintDaybreak (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // A pedal compressor: its knobs, INTENSITY on a key, a sunrise printed
    // by the name, and the gain reduction on a screen.
    const auto nameWidth = nameplate (g, header, "DAYBREAK", "PEDAL COMPRESSOR");
    {
        // The sunrise, printed in the unit's red as its lines are.
        const auto art = juce::Rectangle<float> (header.getX() + nameWidth + 14.0f, header.getY() + 2.0f, 34.0f, header.getHeight() * 0.6f);
        const juce::Point<float> sun (art.getCentreX(), art.getBottom());
        g.setColour (Finish::printedRed());
        juce::Path arc;
        arc.addCentredArc (sun.x, sun.y, art.getHeight() * 0.42f, art.getHeight() * 0.42f, 0.0f,
                           -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (arc, juce::PathStrokeType (1.4f));
        for (int ray = 0; ray < 5; ++ray)
        {
            const auto angle = juce::MathConstants<float>::pi * (-0.4f + 0.2f * (float) ray);
            g.drawLine (juce::Line<float> (sun.getPointOnCircumference (art.getHeight() * 0.58f, angle),
                                           sun.getPointOnCircumference (art.getHeight() * 0.92f, angle)), 1.2f);
        }
        g.drawLine (art.getX() - 4.0f, sun.y, art.getRight() + 4.0f, sun.y, 1.2f);
    }

    auto meter = body.removeFromBottom (24.0f);
    body.removeFromBottom (6.0f);

    // INTENSITY: one of the unit's small keys, its stripe lit while it's on.
    auto keyCell = body.removeFromRight (78.0f);
    stamped (g, "INTENSITY", keyCell.removeFromBottom (12.0f), 8.0f);
    const auto key = keyCell.withSizeKeepingCentre (52.0f, 26.0f);
    ParagonLookAndFeel::drawSmallKey (g, key, false, false);
    ParagonLookAndFeel::drawKeyStripe (g, key.reduced (4.0f, 2.0f), raw (ParamIDs::compIntensity) > 0.5f, false);

    // COMP, DETAIL and MIX: the unit's big knobs.
    const auto cellWidth = body.getWidth() / 3.0f;
    for (auto [id, name] : { std::pair { ParamIDs::compAmount, "COMP" }, std::pair { ParamIDs::compDetail, "DETAIL" },
                             std::pair { ParamIDs::compMix, "MIX" } })
    {
        auto cell = body.removeFromLeft (cellWidth);
        stamped (g, name, cell.removeFromBottom (12.0f), 8.0f);
        const auto size = juce::jmin (cell.getWidth(), cell.getHeight() + 16.0f);
        ParagonLookAndFeel::drawPedalKnob (g, cell.withSizeKeepingCentre (size, size).translated (0.0f, -2.0f), knobAngle (normalised (id)));
    }

    // Gain reduction on a screen, in LED bars.
    stamped (g, "GAIN REDUCTION", meter.removeFromLeft (92.0f), 8.0f, juce::Justification::centredLeft);
    const auto glass = screen (g, meter);
    bars (g, glass.reduced (glass.getWidth() * 0.03f, glass.getHeight() * 0.28f), isOn() ? -shownReduction / 20.0f : 0.0f, 24);
    finish (g, glass);
}

void Faceplate::paintRevival (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // A vocal leveller: GATE, COMPRESSION and GAIN drawn as one curve on a
    // screen (input across, output up), the three as faders, and a meter.
    nameplate (g, header, "REVIVAL", "VOCAL LEVELLER");
    const auto on = isOn();

    const auto gateDb = raw (ParamIDs::revivalGate);
    const auto compressionDb = raw (ParamIDs::revivalCompression);
    const auto gainDb = raw (ParamIDs::revivalGain);
    const auto glass = screen (g, body.removeFromLeft (juce::jmin (body.getWidth() * 0.4f, body.getHeight() * 1.5f)));
    {
        const auto graph = glass.reduced (glass.getWidth() * 0.06f, glass.getHeight() * 0.12f);
        const auto lowest = -60.0f;
        const auto xFor = [&graph, lowest] (float db) { return graph.getX() + graph.getWidth() * (db - lowest) / -lowest; };
        const auto yFor = [&graph, lowest] (float db) { return graph.getBottom() - graph.getHeight() * (juce::jlimit (lowest, 0.0f, db) - lowest) / -lowest; };
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
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (glass.toNearestInt());
        g.setColour (orange.withAlpha (0.12f));
        g.drawLine (xFor (lowest), yFor (lowest), xFor (0.0f), yFor (0.0f), 1.0f);   // unity
        g.setColour (orange.withAlpha (on ? 0.3f : 0.12f));
        g.strokePath (curve, juce::PathStrokeType (4.0f));
        g.setColour (orange.withAlpha (on ? 1.0f : 0.45f));
        g.strokePath (curve, juce::PathStrokeType (1.6f));
        if (gateDb > effects::CompGate::revivalGateOffDb)
        {
            const float dashes[] = { 2.0f, 3.0f };
            g.setColour (orange.withAlpha (0.4f));
            g.drawDashedLine (juce::Line<float> (xFor (gateDb), graph.getY(), xFor (gateDb), graph.getBottom()), dashes, 2, 1.0f);
        }
    }
    finish (g, glass);
    body.removeFromLeft (14.0f);

    // The meter along the bottom, the faders above it.
    auto meter = body.removeFromBottom (22.0f);
    body.removeFromBottom (4.0f);
    const auto faderWidth = body.getWidth() / 3.0f;
    for (auto [id, name] : { std::pair { ParamIDs::revivalGate, "GATE" }, std::pair { ParamIDs::revivalCompression, "COMPRESSION" },
                             std::pair { ParamIDs::revivalGain, "GAIN" } })
    {
        auto cell = body.removeFromLeft (faderWidth);
        stamped (g, name, cell.removeFromBottom (11.0f), 7.5f);
        g.setColour (Finish::printedRed());
        g.setFont (font (8.0f));
        g.drawText (text (id), cell.removeFromTop (11.0f), juce::Justification::centred, false);
        const auto top = cell.getY() + 8.0f, bottom = cell.getBottom() - 8.0f;
        const auto capY = bottom - (bottom - top) * normalised (id);
        ParagonLookAndFeel::drawFader (g, cell.getCentreX(), top, bottom, bottom, capY, cell.getWidth(), colour);
    }
    stamped (g, "REDUCTION", meter.removeFromLeft (62.0f), 7.5f, juce::Justification::centredLeft);
    const auto meterGlass = screen (g, meter);
    bars (g, meterGlass.reduced (meterGlass.getWidth() * 0.03f, meterGlass.getHeight() * 0.28f), on ? -shownReduction / 24.0f : 0.0f, 18);
    finish (g, meterGlass);
}

void Faceplate::paintMars (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // A valve unit: behind one long screen, a tube for each band, glowing
    // in the LED orange as hard as the band is driven, its STYLE under it.
    nameplate (g, header, "MARS", "MULTIBAND SATURATION");
    const auto on = isOn();
    const auto glass = screen (g, body);
    auto inside = glass.reduced (glass.getWidth() * 0.03f, glass.getHeight() * 0.08f);
    const auto bands = juce::jlimit (1, effects::Mars::maxBands, juce::roundToInt (raw (ParamIDs::marsBands)) + 1);
    const auto cellWidth = inside.getWidth() / (float) bands;
    for (int band = 0; band < bands; ++band)
    {
        auto cell = inside.removeFromLeft (cellWidth);
        const auto bandOn = on && raw (ParamIDs::marsBand (band, "On")) > 0.5f;
        const auto glow = bandOn ? juce::jlimit (0.0f, 1.0f, 0.15f + 0.85f * shownDrive[(size_t) band]) : 0.0f;
        const auto style = juce::jlimit (0, effects::Mars::styleNames.size() - 1, juce::roundToInt (raw (ParamIDs::marsBand (band, "Style"))));
        g.setColour (orange.withAlpha (bandOn ? 0.9f : 0.35f));
        g.setFont (font (8.0f));
        g.drawFittedText (juce::String (band + 1) + "  " + effects::Mars::styleNames[style].toUpperCase(),
                          cell.removeFromBottom (13.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        // The tube, drawn in light: its outline, its plates, and the filament's glow.
        const auto tube = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() * 0.5f, 34.0f), cell.getHeight() - 4.0f);
        if (glow > 0.0f)
        {
            g.setGradientFill (juce::ColourGradient (orange.withAlpha (0.55f * glow), tube.getCentreX(), tube.getCentreY(),
                                                     orange.withAlpha (0.0f), tube.getCentreX() + tube.getWidth() * 1.3f, tube.getCentreY(), true));
            g.fillEllipse (tube.expanded (tube.getWidth() * 0.6f, 4.0f));
        }
        g.setColour (orange.withAlpha (0.25f + 0.5f * glow));
        g.drawRoundedRectangle (tube, tube.getWidth() * 0.5f, 1.2f);
        const auto plates = tube.withSizeKeepingCentre (tube.getWidth() * 0.5f, tube.getHeight() * 0.45f);
        g.setColour (orange.withAlpha (0.18f));
        g.drawRect (plates, 1.0f);
        g.setColour (orange.withAlpha (glow > 0.0f ? juce::jmin (1.0f, 0.35f + glow) : 0.12f));
        g.fillRect (plates.withSizeKeepingCentre (2.5f, plates.getHeight() * 0.85f));
    }
    finish (g, glass);
}
