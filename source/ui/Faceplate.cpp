#include "Faceplate.h"

#include "Finish.h"
#include "LedDisplay.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "PedalBody.h"
#include "dsp/PitchDetector.h"
#include "dsp/Scales.h"
#include "dsp/effects/CompGate.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Harmony.h"
#include "dsp/effects/Mars.h"
#include "dsp/effects/Reverb.h"

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

    // One of the unit's big knobs with its name stamped under it.
    void bigKnob (juce::Graphics& g, juce::Rectangle<float> cell, const juce::String& name, float position)
    {
        stamped (g, name, cell.removeFromBottom (12.0f), 8.0f);
        const auto size = juce::jmin (cell.getWidth(), cell.getHeight() + 16.0f);
        ParagonLookAndFeel::drawPedalKnob (g, cell.withSizeKeepingCentre (size, size).translated (0.0f, -2.0f), knobAngle (position));
    }

    // A number on a screen in the main display's seven-segment digits (a
    // dot drawn for a decimal point), then a small unit after it.
    void sevenSegment (juce::Graphics& g, juce::Rectangle<float> glass, const juce::String& number, const juce::String& unitText)
    {
        auto area = glass.reduced (glass.getWidth() * 0.06f, glass.getHeight() * 0.2f);
        const auto unitWidth = area.getHeight() * 0.9f;
        g.setColour (orange);
        g.setFont (font (area.getHeight() * 0.38f));
        g.drawText (unitText, area.removeFromRight (unitWidth), juce::Justification::bottomLeft, false);
        const auto digitHeight = area.getHeight();
        const auto digitWidth = digitHeight * 0.55f;
        auto digits = 0;
        for (auto c : number)
            digits += c == '.' ? 0 : 1;
        auto x = area.getRight() - 4.0f - (float) digits * digitWidth * 1.15f;
        for (auto c : number)
        {
            if (c == '.')
            {
                g.setColour (orange);
                g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre ({ x - digitWidth * 0.08f, area.getBottom() - 2.0f }));
                continue;
            }
            LedDisplay::drawDigit (g, c, juce::Rectangle<float> (x, area.getY(), digitWidth, digitHeight), orange);
            x += digitWidth * 1.15f;
        }
    }

    // A note's name, "A3".
    juce::String noteName (float midiNote)
    {
        const auto named = PitchDetector::toNote (Scales::hzFromMidi (midiNote));
        return named.name + juce::String (named.octave);
    }
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
    if (unit == Unit::delay)
        return Look::delay;
    if (unit == Unit::reverb)
        return Look::reverb;
    if (unit == Unit::pitch)
        return Look::harmony;
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
        case Look::delay:    paintDelay (g, header, content); break;
        case Look::reverb:   paintReverb (g, header, content); break;
        case Look::harmony:  paintHarmony (g, header, content); break;
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

void Faceplate::paintDelay (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // The delay's TYPE as its name; its repeats on a screen; TIME in digits.
    static const char* names[] = { "DIGITAL DELAY", "ANALOG DELAY", "TAPE ECHO", "MOD DELAY", "REVERSE DELAY",
                                   "MULTI-TAP", "SHIMMER DELAY", "DUCKING DELAY" };
    const auto type = juce::jlimit (0, (int) std::size (names) - 1, juce::roundToInt (raw (ParamIDs::delayType)));
    nameplate (g, header, names[type], "ECHO");
    const auto on = isOn();

    // The time each side repeats at (seconds), synced or not.
    const auto bpm = juce::jmax (20.0, processor.getTempoBpm());
    const auto syncs = (int) std::size (effects::Delay::syncBeats);
    const auto secondsFor = [&] (const char* timeId, const char* syncId)
    {
        const auto sync = juce::roundToInt (raw (syncId));
        return sync > 0 && sync < syncs ? (double) effects::Delay::syncBeats[sync] * 60.0 / bpm : raw (timeId) / 1000.0;
    };
    const auto left = juce::jmax (0.001, secondsFor (ParamIDs::delayTime, ParamIDs::delaySync));
    const auto dual = raw (ParamIDs::delayDual) > 0.5f;
    const auto right = dual ? juce::jmax (0.001, secondsFor (ParamIDs::delayTimeRight, ParamIDs::delaySyncRight)) : left;
    const auto feedback = raw (ParamIDs::delayFeedback);

    // The screen: each repeat a bar, as tall as it is loud (FEEDBACK each
    // time), over the next few seconds; a light runs along them at the
    // delay time (in time with it, not with the audio itself).
    auto settings = body.removeFromRight (body.getWidth() * 0.4f);
    body.removeFromRight (14.0f);
    const auto glass = screen (g, body);
    {
        auto plot = glass.reduced (glass.getWidth() * 0.05f, glass.getHeight() * 0.14f);
        g.setColour (orange.withAlpha (0.6f));
        g.setFont (font (8.0f));
        g.drawText (raw (ParamIDs::delayPingPong) > 0.5f ? "REPEATS  PING-PONG" : dual ? "REPEATS  DUAL" : "REPEATS",
                    plot.removeFromTop (10.0f), juce::Justification::topLeft, false);
        const auto span = juce::jmax (left, right) * 6.5;
        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const auto rows = dual ? 2 : 1;
        const auto rowHeight = plot.getHeight() / (float) rows;
        for (int row = 0; row < rows; ++row)
        {
            const auto period = row == 0 ? left : right;
            const auto lane = plot.withTrimmedTop (rowHeight * (float) row).withHeight (rowHeight).reduced (0.0f, 2.0f);
            g.setColour (orange.withAlpha (0.15f));
            g.fillRect (lane.withTop (lane.getBottom() - 1.0f));
            const auto ringing = (int) std::fmod (now / period, 6.0);   // the repeat the light is on
            auto level = 1.0f;
            for (int repeat = 0; repeat < 6 && repeat * period < span; ++repeat)
            {
                const auto x = lane.getX() + lane.getWidth() * (float) (((double) repeat + 1.0) * period / span);
                const auto bar = juce::Rectangle<float> (4.0f, lane.getHeight() * juce::jmax (0.04f, level)).withBottomY (lane.getBottom()).withCentre ({ x, 0.0f })
                                     .withBottomY (lane.getBottom());
                const auto lit = on && repeat == ringing;
                g.setColour (orange.withAlpha (lit ? 0.35f : 0.15f));
                g.fillRoundedRectangle (bar.expanded (2.0f, 1.0f), 2.0f);
                g.setColour (orange.withAlpha (on ? (lit ? 1.0f : 0.6f) : 0.3f));
                g.fillRoundedRectangle (bar, 1.5f);
                level *= juce::jmin (1.0f, feedback);
                if (level < 0.02f)
                    break;
            }
        }
    }
    finish (g, glass);

    // TIME (the note length while synced), then FEEDBACK and MIX knobs.
    auto timeArea = settings.removeFromTop (settings.getHeight() * 0.42f);
    const auto timeGlass = screen (g, timeArea);
    const auto sync = juce::roundToInt (raw (ParamIDs::delaySync));
    if (sync > 0 && sync < syncs)
    {
        g.setColour (orange);
        g.setFont (font (timeGlass.getHeight() * 0.5f));
        g.drawText (effects::Delay::syncNames[sync], timeGlass.reduced (8.0f, 0.0f), juce::Justification::centredRight, false);
    }
    else
    {
        sevenSegment (g, timeGlass, juce::String (juce::roundToInt (left * 1000.0)), "ms");
    }
    finish (g, timeGlass);
    settings.removeFromTop (4.0f);
    const auto half = settings.getWidth() / 2.0f;
    bigKnob (g, settings.removeFromLeft (half), "FEEDBACK", normalised (ParamIDs::delayFeedback));
    bigKnob (g, settings, "MIX", normalised (ParamIDs::delayMix));
}

void Faceplate::paintReverb (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // The reverb's TYPE as its name; its shape on a screen; DECAY in seconds.
    static const char* names[] = { "CLASSIC", "ROOM", "HALL", "CATHEDRAL", "PLATE", "SPRING", "DIGITAL", "REVERSE", "CHAMBER" };
    const auto type = juce::jlimit (0, (int) std::size (names) - 1, juce::roundToInt (raw (ParamIDs::reverbType)));
    nameplate (g, header, names[type], "REVERB");
    const auto on = isOn();
    const auto decay = effects::Reverb::decaySeconds ((effects::Reverb::Type) type, raw (ParamIDs::reverbSize), raw (ParamIDs::reverbDecay));
    const auto preDelay = raw (ParamIDs::reverbPreDelay) / 1000.0f;
    const auto early = juce::jlimit (0.0f, 1.0f, normalised (ParamIDs::reverbEarly));

    // The screen: level over time. A gap for PRE-DELAY, the early
    // reflections as ticks, then the tail fading over DECAY (rising, for
    // REVERSE; with a spring's ripple, for SPRING).
    auto settings = body.removeFromRight (body.getWidth() * 0.4f);
    body.removeFromRight (14.0f);
    const auto glass = screen (g, body);
    {
        auto plot = glass.reduced (glass.getWidth() * 0.05f, glass.getHeight() * 0.14f);
        g.setColour (orange.withAlpha (0.6f));
        g.setFont (font (8.0f));
        g.drawText ("PRE " + juce::String (juce::roundToInt (preDelay * 1000.0f)) + " ms", plot.removeFromTop (10.0f), juce::Justification::topLeft, false);
        const auto span = preDelay + decay * 1.15f + 0.05f;
        const auto xFor = [&plot, span] (float seconds) { return plot.getX() + plot.getWidth() * seconds / span; };
        const auto start = preDelay;
        const auto reverse = type == (int) effects::Reverb::Type::reverse;
        const auto spring = type == (int) effects::Reverb::Type::spring;
        juce::Path tail;
        tail.startNewSubPath (xFor (start), plot.getBottom());
        for (auto t = 0.0f; t <= decay; t += decay / 120.0f)
        {
            auto level = reverse ? std::pow (t / decay, 2.0f) : std::pow (10.0f, -3.0f * t / decay);   // 60 dB down over DECAY
            if (spring)
                level *= 0.8f + 0.2f * std::cos (t * 70.0f);
            tail.lineTo (xFor (start + t), plot.getBottom() - plot.getHeight() * 0.9f * level);
        }
        tail.lineTo (xFor (start + decay), plot.getBottom());
        tail.closeSubPath();
        g.setGradientFill (juce::ColourGradient (orange.withAlpha (on ? 0.45f : 0.15f), 0.0f, plot.getY(), orange.withAlpha (0.05f), 0.0f, plot.getBottom(), false));
        g.fillPath (tail);
        g.setColour (orange.withAlpha (on ? 1.0f : 0.4f));
        g.strokePath (tail, juce::PathStrokeType (1.4f));
        // Early reflections: a few ticks just after the pre-delay.
        for (int tick = 0; tick < 6; ++tick)
        {
            const auto x = xFor (start + 0.008f + 0.011f * (float) tick);
            const auto height = plot.getHeight() * early * (0.95f - 0.12f * (float) tick);
            g.setColour (orange.withAlpha (on ? 0.9f : 0.35f));
            g.fillRect (juce::Rectangle<float> (2.0f, height).withBottomY (plot.getBottom()).withX (x));
        }
        g.setColour (orange.withAlpha (0.15f));
        g.fillRect (plot.withTop (plot.getBottom() - 1.0f));
    }
    finish (g, glass);

    // DECAY in seconds, then SIZE and MIX knobs.
    auto decayArea = settings.removeFromTop (settings.getHeight() * 0.42f);
    const auto decayGlass = screen (g, decayArea);
    sevenSegment (g, decayGlass, juce::String (decay, decay < 10.0f ? 1 : 0), "s");
    finish (g, decayGlass);
    settings.removeFromTop (4.0f);
    const auto half = settings.getWidth() / 2.0f;
    bigKnob (g, settings.removeFromLeft (half), "SIZE", normalised (ParamIDs::reverbSize));
    bigKnob (g, settings, "MIX", normalised (ParamIDs::reverbMix));
}

void Faceplate::paintHarmony (juce::Graphics& g, juce::Rectangle<float> header, juce::Rectangle<float> body)
{
    // The voice processor: the note sung on one screen, the voices on
    // another, and keys for HARMONY, OCTAVE and FREEZE.
    nameplate (g, header, "HARMONY", "VOICE PROCESSOR");

    // Keys down the right, each with its light stripe and name.
    auto keys = body.removeFromRight (98.0f);
    const auto keyRow = keys.getHeight() / 3.0f;
    for (auto [id, name] : { std::pair { ParamIDs::harmonyOn, "HARMONY" }, std::pair { ParamIDs::octOn, "OCTAVE" },
                             std::pair { ParamIDs::freezeOn, "FREEZE" } })
    {
        auto row = keys.removeFromTop (keyRow);
        const auto key = row.removeFromLeft (40.0f).withSizeKeepingCentre (38.0f, juce::jmin (22.0f, row.getHeight() - 4.0f));
        ParagonLookAndFeel::drawSmallKey (g, key, false, false);
        ParagonLookAndFeel::drawKeyStripe (g, key.reduced (4.0f, 2.0f), raw (id) > 0.5f, false);
        stamped (g, name, row.withTrimmedLeft (6.0f), 8.0f, juce::Justification::centredLeft);
    }
    body.removeFromRight (12.0f);

    // The note: SINGING, and with TUNE VOICE on, the note it's tuned to.
    const auto noteGlass = screen (g, body.removeFromLeft (body.getWidth() * 0.42f));
    {
        auto inside = noteGlass.reduced (noteGlass.getWidth() * 0.07f, noteGlass.getHeight() * 0.12f);
        g.setColour (orange.withAlpha (0.6f));
        g.setFont (font (8.0f));
        g.drawText ("KEY " + text (ParamIDs::harmonyKey).toUpperCase() + " " + text (ParamIDs::harmonyScale).toUpperCase(),
                    inside.removeFromTop (10.0f), juce::Justification::topLeft, false);
        const auto sung = processor.getHarmonySungNote();
        const auto correcting = juce::roundToInt (raw (ParamIDs::harmonyCorrect)) > 0;
        const auto target = processor.getHarmonyCorrectNote();
        g.setColour (orange);
        g.setFont (font (inside.getHeight() * 0.62f));
        g.drawText (sung >= 0.0f ? noteName (sung) : juce::String ("--"), inside.removeFromLeft (inside.getWidth() * 0.55f), juce::Justification::centredLeft, false);
        if (correcting)
        {
            g.setColour (orange.withAlpha (target >= 0.0f ? 0.9f : 0.4f));
            g.setFont (font (inside.getHeight() * 0.3f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + (target >= 0.0f ? noteName (target) : juce::String ("--")),
                        inside, juce::Justification::centredLeft, false);
        }
    }
    finish (g, noteGlass);
    body.removeFromLeft (10.0f);

    // The voices, as on the HARMONY graph: across by pan, up by interval;
    // a flattened voice at its note's place; note voices as rings.
    const auto voicesGlass = screen (g, body);
    {
        auto plot = voicesGlass.reduced (voicesGlass.getWidth() * 0.08f, voicesGlass.getHeight() * 0.16f);
        const auto harmonyOn = raw (ParamIDs::harmonyOn) > 0.5f;
        const auto sung = processor.getHarmonySungNote();
        const auto yFor = [&plot] (float semitones) { return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, semitones / 12.0f); };
        g.setColour (orange.withAlpha (0.15f));
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
        g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());
        for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
        {
            const auto part = [voice] (const char* name) { return ParamIDs::harmonyVoice (voice, name); };
            if (raw (part ("Active")) <= 0.5f)
                continue;
            auto interval = raw (part ("Pitch"));
            if (raw (part ("Flatten")) > 0.5f)
                interval = sung >= 0.0f ? raw (part ("Note")) - sung : 0.0f;
            const auto x = plot.getCentreX() + plot.getWidth() * 0.5f * raw (part ("Pan"));
            const auto sounding = harmonyOn && raw (part ("On")) > 0.5f;
            const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, yFor (interval) });
            g.setColour (orange.withAlpha (sounding ? 0.35f : 0.1f));
            g.fillEllipse (dot.expanded (2.5f));
            g.setColour (orange.withAlpha (sounding ? 1.0f : 0.35f));
            g.fillEllipse (dot);
        }
        const auto& engine = processor.getHarmony();
        for (int voice = 0; voice < effects::Harmony::numNoteVoices; ++voice)
        {
            const auto note = engine.getNoteVoiceNote (voice);
            if (note < 0 || sung < 0.0f)
                continue;
            const auto x = plot.getCentreX() + plot.getWidth() * 0.5f * engine.getNoteVoicePan (voice);
            g.setColour (orange.withAlpha (juce::jlimit (0.3f, 1.0f, engine.getNoteVoiceLevel (voice))));
            g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, yFor ((float) note - sung) }), 1.4f);
        }
        g.setColour (orange.withAlpha (0.6f));
        g.setFont (font (8.0f));
        g.drawText ("VOICES", voicesGlass.reduced (8.0f, 4.0f), juce::Justification::topLeft, false);
    }
    finish (g, voicesGlass);
}
