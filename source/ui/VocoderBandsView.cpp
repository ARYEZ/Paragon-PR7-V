#include "VocoderBandsView.h"

#include "Parameters.h"
#include "ui/LedDisplay.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    const auto orange = LedDisplay::ledOrange;
    constexpr float screenSize = 0.45f;   // as the faceplate's small screens
    constexpr float lowestShownDb = -60.0f;

    juce::String noteName (float midiNote)
    {
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        const auto note = juce::roundToInt (midiNote);
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
    }
}

VocoderBandsView::VocoderBandsView (juce::AudioProcessorValueTreeState& state, const Voc* vocoderToShow)
    : parameters (state), vocoder (vocoderToShow)
{
    for (auto* key : { &volKey, &panKey, &resetKey })
    {
        key->getProperties().set (ParagonLookAndFeel::rackKeyProperty, true);
        addAndMakeVisible (*key);
    }
    for (auto* key : { &volKey, &panKey })
    {
        key->setColour (juce::TextButton::textColourOnId, orange);   // the curve being drawn, lit
        key->setClickingTogglesState (true);
        key->setRadioGroupId (4480);
    }
    volKey.setToggleState (true, juce::dontSendNotification);
    volKey.setTooltip ("Draw each band's volume");
    panKey.setTooltip ("Draw each band's place in the stereo");
    resetKey.setTooltip ("Flatten the curve showing (every band at 0 dB, or in the middle)");
    volKey.onClick = [this] { showPan (false); };
    panKey.onClick = [this] { showPan (true); };
    resetKey.onClick = [this] { resetCurve(); };
    startTimerHz (30);
}

VocoderBandsView::~VocoderBandsView() = default;

void VocoderBandsView::showPan (bool pan)
{
    showingPan = pan;
    (pan ? panKey : volKey).setToggleState (true, juce::dontSendNotification);
    repaint();
}

juce::String VocoderBandsView::pointId (int point) const
{
    return showingPan ? ParamIDs::vocoderBandPan (point) : ParamIDs::vocoderBandVol (point);
}

float VocoderBandsView::yFor (float value) const noexcept
{
    const auto t = showingPan ? (value + 1.0f) * 0.5f : (value - Voc::minBandDb) / (6.0f - Voc::minBandDb);
    return plot.getBottom() - juce::jlimit (0.0f, 1.0f, t) * plot.getHeight();
}

float VocoderBandsView::valueAt (float y) const noexcept
{
    const auto t = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - y) / juce::jmax (1.0f, plot.getHeight()));
    return showingPan ? t * 2.0f - 1.0f : Voc::minBandDb + t * (6.0f - Voc::minBandDb);
}

void VocoderBandsView::drawAt (juce::Point<float> point, std::optional<juce::Point<float>> from)
{
    const auto indexOf = [this] (float x)
    {
        return juce::jlimit (0, Voc::curvePoints - 1, juce::roundToInt ((x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()) * (float) (Voc::curvePoints - 1)));
    };
    const auto start = from.value_or (point);
    const auto a = indexOf (start.x), b = indexOf (point.x);
    for (int i = juce::jmin (a, b); i <= juce::jmax (a, b); ++i)
    {
        const auto t = a == b ? 1.0f : (float) (i - a) / (float) (b - a);
        auto value = valueAt (start.y + (point.y - start.y) * t);
        if (! showingPan && std::abs (value) < 0.6f)
            value = 0.0f;   // near the middle: exactly 0 dB, as it starts
        auto* parameter = parameters.getParameter (pointId (i));
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
    repaint();
}

void VocoderBandsView::resetCurve()
{
    for (int i = 0; i < Voc::curvePoints; ++i)
    {
        auto* parameter = parameters.getParameter (pointId (i));
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
        parameter->endChangeGesture();
    }
    repaint();
}

void VocoderBandsView::timerCallback()
{
    if (isShowing())
        repaint();
}

void VocoderBandsView::resized()
{
    auto area = getLocalBounds();
    auto keys = area.removeFromTop (24);
    for (auto* key : { &volKey, &panKey })
    {
        key->setBounds (keys.removeFromLeft (50).reduced (2, 1));
        keys.removeFromLeft (2);
    }
    resetKey.setBounds (keys.removeFromRight (60).reduced (2, 1));
    area.removeFromTop (4);
    glass = area.toFloat();   // the screen; its glass is worked out as it's painted
    plot = glass.reduced (glass.getWidth() * 0.05f, glass.getHeight() * 0.1f).withTrimmedTop (14.0f);
}

void VocoderBandsView::mouseDown (const juce::MouseEvent& event)
{
    if (! plot.expanded (6.0f).contains (event.position))
        return;
    if (event.getNumberOfClicks() > 1)
    {
        resetCurve();
        return;
    }
    drawAt (event.position);
    lastDrag = event.position;
}

void VocoderBandsView::mouseDrag (const juce::MouseEvent& event)
{
    if (! lastDrag.has_value())
        return;
    drawAt (event.position, lastDrag);
    lastDrag = event.position;
}

void VocoderBandsView::mouseUp (const juce::MouseEvent&) { lastDrag.reset(); }

void VocoderBandsView::paint (juce::Graphics& g)
{
    const auto inside = LedDisplay::paintScreen (g, glass, screenSize);
    const auto on = parameters.getRawParameterValue (ParamIDs::vocoderOn)->load() > 0.5f;
    const auto mode = juce::jlimit (0, 2, juce::roundToInt (parameters.getRawParameterValue (ParamIDs::vocoderMode)->load()));
    const auto count = Voc::bandCounts[mode];

    // Caption: what's shown, and the notes playing.
    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    g.setColour (orange.withAlpha (0.6f));
    const auto captionArea = inside.reduced (inside.getWidth() * 0.05f, inside.getHeight() * 0.08f).withHeight (12.0f);
    g.drawText (juce::String (count) + " BANDS  " + (showingPan ? "PAN" : "VOL"), captionArea, juce::Justification::centredLeft, false);
    if (vocoder != nullptr)
    {
        juce::StringArray notes;
        for (int v = 0; v < Voc::maxVoices; ++v)
            if (const auto note = vocoder->getVoiceNote (v); note >= 0.0f)
                notes.addIfNotAlreadyThere (noteName (note));
        g.setColour (orange.withAlpha (on ? 0.9f : 0.35f));
        g.drawText (notes.joinIntoString (" "), captionArea, juce::Justification::centredRight, false);
    }

    // The bands: LED bars as tall as the voice is loud in each (-60 dB to 0).
    const auto width = plot.getWidth() / (float) count;
    for (int b = 0; b < count; ++b)
    {
        const auto level = vocoder != nullptr ? vocoder->getBandLevel (b) : 0.0f;
        const auto db = juce::Decibels::gainToDecibels (level * 1.57f, lowestShownDb);   // mean of a sine's size to its peak
        const auto t = juce::jlimit (0.0f, 1.0f, (db - lowestShownDb) / -lowestShownDb);
        const auto column = juce::Rectangle<float> (plot.getX() + width * (float) b, plot.getY(), width, plot.getHeight()).reduced (width * 0.18f, 0.0f);
        g.setColour (orange.withAlpha (0.08f));
        g.fillRoundedRectangle (column, 1.5f);
        if (t > 0.0f && on)
        {
            const auto lit = column.withTrimmedTop (column.getHeight() * (1.0f - t));
            g.setColour (orange.withAlpha (0.25f));
            g.fillRoundedRectangle (lit.expanded (1.5f), 2.0f);
            g.setColour (orange.withAlpha (0.85f));
            g.fillRoundedRectangle (lit, 1.5f);
        }
    }

    // The curve over them, its points as dots, and its middle line.
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), plot.getX(), plot.getRight());
    juce::Path curve;
    for (int p = 0; p < Voc::curvePoints; ++p)
    {
        const auto value = parameters.getRawParameterValue (pointId (p))->load();
        const juce::Point<float> at { plot.getX() + plot.getWidth() * (float) p / (float) (Voc::curvePoints - 1), yFor (value) };
        if (p == 0)
            curve.startNewSubPath (at);
        else
            curve.lineTo (at);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (at));
    }
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.strokePath (curve, juce::PathStrokeType (1.2f));
    g.setColour (orange.withAlpha (0.5f));
    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    const auto scale = plot.withTrimmedLeft (plot.getWidth() - 40.0f);
    g.drawText (showingPan ? "R" : "+6", scale.withHeight (10.0f), juce::Justification::topRight, false);
    g.drawText (showingPan ? "L" : "OFF", scale.withTrimmedTop (scale.getHeight() - 10.0f), juce::Justification::bottomRight, false);

    LedDisplay::paintGlassOverlay (g, inside, screenSize);
}
