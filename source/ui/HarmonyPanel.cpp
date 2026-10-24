#include "HarmonyPanel.h"

#include "Icons.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "dsp/PitchDetector.h"

namespace
{
    constexpr float dotRadius = 12.0f;
    const auto maxGain = juce::Decibels::decibelsToGain (6.0f);

    // The graph's height is the square root of the gain, so the useful
    // levels (0 to -12 dB) get most of it, as on Waves Harmony's graph.
    float heightForDb (float db)
    {
        const auto gain = db <= effects::Harmony::silentDb ? 0.0f : juce::Decibels::decibelsToGain (db);
        return std::sqrt (gain / maxGain);
    }

    float dbForHeight (float height)
    {
        const auto gain = juce::jlimit (0.0f, 1.0f, height) * juce::jlimit (0.0f, 1.0f, height) * maxGain;
        return juce::jlimit (effects::Harmony::silentDb, 6.0f, juce::Decibels::gainToDecibels (gain, effects::Harmony::silentDb));
    }

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

    juce::String intervalText (float semitones)
    {
        const auto value = juce::roundToInt (semitones);
        return value == 0 ? juce::String ("U") : (value > 0 ? "+" : "") + juce::String (value);
    }
}

HarmonyPanel::HarmonyPanel (juce::AudioProcessorValueTreeState& state, std::function<float()> sungNoteSource)
    : parameters (state), sungNote (std::move (sungNoteSource))
{
    setUp (harmonyOn, "HARMONY");
    harmonyOn.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, ParamIDs::harmonyOn, harmonyOn.button);
    setUp (voiceOn, "ON");
    setUp (voiceCorrect, "CORRECT");
    voiceCorrect.button.setTooltip ("Keep this voice in the key and scale");

    for (auto [label, box, id, text] : { std::tuple { &keyLabel, &key, ParamIDs::harmonyKey, "KEY" },
                                         std::tuple { &scaleLabel, &scale, ParamIDs::harmonyScale, "SCALE" } })
    {
        styleLabel (*label, text);
        addAndMakeVisible (*label);
        box->addItemList (dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (id))->choices, 1);
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
        box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box->setColour (juce::ComboBox::textColourId, ParagonColours::accent);
        box->setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
        addAndMakeVisible (*box);
    }
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyKey, key);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonyScale, scale);
    key.setTooltip ("The song's key: harmony voices are kept in it");

    setUp (pitch, "PITCH");
    setUp (fine, "FINE");
    setUp (formant, "FORMANT");
    setUp (source, "SOURCE");
    setUp (harmony, "HARMONY");
    setUp (spread, "SPREAD");
    setUp (correctFormant, "CORR. FORMANT");
    for (auto [knob, id] : { std::pair { &source, ParamIDs::harmonySource }, std::pair { &harmony, ParamIDs::harmonyLevel },
                             std::pair { &spread, ParamIDs::harmonySpread }, std::pair { &correctFormant, ParamIDs::harmonyCorrectFormant } })
    {
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        auto* parameter = parameters.getParameter (id);
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    source.slider.setTooltip ("Your own voice's level");
    harmony.slider.setTooltip ("All the harmony voices' level");
    spread.slider.setTooltip ("How wide the voices spread: 0% puts them all in the middle");
    correctFormant.slider.setTooltip ("100%: raised or lowered voices keep your vowels, like another singer. 0%: they move with the pitch, like a sped-up tape");

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto& tab = tabs[i];
        tab.setButtonText (juce::String ((int) i + 1));
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (4210);
        tab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        tab.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);   // the picked voice, in orange as the plugin's other tabs
        tab.setTooltip ("Voice " + juce::String ((int) i + 1) + " (click an empty one to add it, in the middle)");
        tab.onClick = [this, voice = (int) i]
        {
            if (! isActive (voice))
                addVoiceAt (voice, 0.0f, 0.0f);
            selectVoice (voice);
        };
        addAndMakeVisible (tab);
    }

    selectVoice (0);
    refreshTabs();
    shownSung = "--";
    startTimerHz (20);
}

HarmonyPanel::~HarmonyPanel() = default;

void HarmonyPanel::setUp (Knob& knob, const juce::String& labelText)
{
    styleLabel (knob.label, labelText);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (knob.label);
    addAndMakeVisible (knob.slider);
}

void HarmonyPanel::setUp (Switch& control, const juce::String& labelText)
{
    styleLabel (control.label, labelText);
    addAndMakeVisible (control.label);
    addAndMakeVisible (control.button);
}

juce::String HarmonyPanel::voiceId (int voice, const char* part) const { return ParamIDs::harmonyVoice (voice, part); }

float HarmonyPanel::value (const juce::String& id) const { return parameters.getRawParameterValue (id)->load(); }

bool HarmonyPanel::isActive (int voice) const { return value (voiceId (voice, "Active")) > 0.5f; }

void HarmonyPanel::set (const juce::String& id, float plainValue)
{
    auto* parameter = parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

void HarmonyPanel::beginGesture (const juce::String& id) { parameters.getParameter (id)->beginChangeGesture(); }
void HarmonyPanel::endGesture (const juce::String& id) { parameters.getParameter (id)->endChangeGesture(); }

void HarmonyPanel::selectVoice (int voice)
{
    selected = juce::jlimit (0, (int) tabs.size() - 1, voice);
    tabs[(size_t) selected].setToggleState (true, juce::dontSendNotification);
    bindVoiceControls();
    repaint();
}

void HarmonyPanel::bindVoiceControls()
{
    // The voice controls follow whichever voice is picked.
    for (auto [control, part] : { std::pair { &voiceOn, "On" }, std::pair { &voiceCorrect, "Correct" } })
    {
        control->attachment.reset();
        control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, voiceId (selected, part), control->button);
    }
    for (auto [knob, part] : { std::pair { &pitch, "Pitch" }, std::pair { &fine, "Fine" }, std::pair { &formant, "Formant" } })
    {
        knob->attachment.reset();
        const auto id = voiceId (selected, part);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        knob->slider.setDoubleClickReturnValue (true, 0.0);
    }
    pitch.slider.setTooltip ("Voice " + juce::String (selected + 1) + "'s interval from your note, in semitones (+4 a third up, +7 a fifth); CORRECT keeps it in the key");
}

void HarmonyPanel::addVoiceAt (int voice, float pan, float levelDb)
{
    // A new voice starts at unison (U), as on Waves Harmony: set its interval next.
    const std::pair<const char*, float> fresh[] = { { "On", 1.0f }, { "Pitch", 0.0f }, { "Fine", 0.0f }, { "Formant", 0.0f },
                                                    { "Correct", 1.0f }, { "Pan", pan }, { "Level", levelDb }, { "Active", 1.0f } };
    for (const auto& [part, plain] : fresh)
    {
        const auto id = voiceId (voice, part);
        beginGesture (id);
        set (id, plain);
        endGesture (id);
    }
}

void HarmonyPanel::removeVoice (int voice)
{
    const auto id = voiceId (voice, "Active");
    beginGesture (id);
    set (id, 0.0f);
    endGesture (id);
}

juce::Point<float> HarmonyPanel::pointFor (float pan, float levelDb) const
{
    const auto inner = graph.reduced (dotRadius + 2.0f);
    return { inner.getX() + (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.5f * inner.getWidth(),
             inner.getBottom() - heightForDb (levelDb) * inner.getHeight() };
}

std::pair<float, float> HarmonyPanel::panAndLevelAt (juce::Point<float> point) const
{
    const auto inner = graph.reduced (dotRadius + 2.0f);
    const auto pan = juce::jlimit (-1.0f, 1.0f, (point.x - inner.getX()) / inner.getWidth() * 2.0f - 1.0f);
    return { std::round (pan * 100.0f) / 100.0f, std::round (dbForHeight ((inner.getBottom() - point.y) / inner.getHeight()) * 10.0f) / 10.0f };
}

int HarmonyPanel::voiceAt (juce::Point<float> point) const
{
    // The picked voice first, as it's drawn on top.
    for (int pass = 0; pass < 2; ++pass)
        for (int voice = effects::Harmony::numVoices - 1; voice >= 0; --voice)
            if (isActive (voice) && (pass == 1 || voice == selected)
                && pointFor (value (voiceId (voice, "Pan")), value (voiceId (voice, "Level"))).getDistanceFrom (point) <= dotRadius + 2.0f)
                return voice;
    return -1;
}

void HarmonyPanel::mouseDown (const juce::MouseEvent& event)
{
    const auto point = event.position;
    if (! graph.contains (point))
        return;

    auto voice = voiceAt (point);
    if (voice >= 0 && event.mods.isPopupMenu())
    {
        removeVoice (voice);
        repaint();
        return;
    }
    if (voice < 0)
    {
        // An empty spot: the first free voice starts there.
        for (int free = 0; free < effects::Harmony::numVoices && voice < 0; ++free)
            if (! isActive (free))
                voice = free;
        if (voice < 0)
            return;   // all eight in use
        const auto [pan, level] = panAndLevelAt (point);
        addVoiceAt (voice, pan, level);
    }

    selectVoice (voice);
    dragging = voice;
    beginGesture (voiceId (voice, "Pan"));
    beginGesture (voiceId (voice, "Level"));
}

void HarmonyPanel::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging < 0)
        return;
    const auto [pan, level] = panAndLevelAt (event.position);
    set (voiceId (dragging, "Pan"), pan);
    set (voiceId (dragging, "Level"), level);
    repaint (graph.expanded (2.0f).getSmallestIntegerContainer());
}

void HarmonyPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    endGesture (voiceId (dragging, "Pan"));
    endGesture (voiceId (dragging, "Level"));
    dragging = -1;
}

void HarmonyPanel::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Scrolling over a voice changes its interval a semitone at a time.
    const auto voice = voiceAt (event.position);
    if (voice < 0 || std::abs (wheel.deltaY) < 1.0e-4f)
        return;
    const auto id = voiceId (voice, "Pitch");
    beginGesture (id);
    set (id, juce::jlimit (-24.0f, 24.0f, value (id) + (wheel.deltaY > 0.0f ? 1.0f : -1.0f)));
    endGesture (id);
    selectVoice (voice);
}

void HarmonyPanel::refreshTabs()
{
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i].setColour (juce::TextButton::textColourOffId, isActive ((int) i) ? ParagonColours::text : ParagonColours::textDim.withAlpha (0.5f));
}

void HarmonyPanel::timerCallback()
{
    // The voices can move from the DAW (automation, undo, a rig), and the
    // sung note changes all the time: keep both up to date.
    refreshTabs();
    const auto note = sungNote ? sungNote() : -1.0f;
    juce::String text ("--");
    if (note >= 0.0f)
    {
        const auto named = PitchDetector::toNote (Scales::hzFromMidi (note));
        text = named.name + juce::String (named.octave);
    }
    if (text != shownSung)
    {
        shownSung = text;
        repaint (sungArea.getSmallestIntegerContainer());
    }
    repaint (graph.expanded (2.0f).getSmallestIntegerContainer());
}

void HarmonyPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    area.removeFromTop (34);   // header: the icon, PITCH and the view tabs (placed by the main view)

    // Left: on, key, scale, the sung note.
    auto left = area.removeFromLeft (112);
    area.removeFromLeft (10);
    harmonyOn.label.setBounds (left.removeFromTop (14));
    harmonyOn.button.setBounds (left.removeFromTop (28).withSizeKeepingCentre (26, 26));
    left.removeFromTop (6);
    keyLabel.setBounds (left.removeFromTop (14));
    key.setBounds (left.removeFromTop (26).reduced (4, 0));
    left.removeFromTop (6);
    scaleLabel.setBounds (left.removeFromTop (14));
    scale.setBounds (left.removeFromTop (26).reduced (4, 0));
    left.removeFromTop (4);
    sungArea = left.toFloat();

    // Middle: the graph, and the picked voice's pan and level under it.
    auto middle = area.removeFromLeft (212);
    area.removeFromLeft (10);
    readoutArea = middle.removeFromBottom (16).toFloat();
    graph = middle.toFloat();

    // Right: voice tabs, the picked voice, then the mix.
    auto right = area;
    auto tabRow = right.removeFromTop (22);
    const auto tabWidth = (tabRow.getWidth() - 3 * ((int) tabs.size() - 1)) / (int) tabs.size();
    for (auto& tab : tabs)
    {
        tab.setBounds (tabRow.removeFromLeft (tabWidth));
        tabRow.removeFromLeft (3);
    }
    right.removeFromTop (6);
    const auto rowHeight = (right.getHeight() - 6) / 2;

    auto voiceRow = right.removeFromTop (rowHeight);
    const auto place = [] (juce::Label& label, juce::Component& widget, juce::Rectangle<int> cell, bool isSwitch)
    {
        label.setBounds (cell.removeFromTop (14));
        if (isSwitch)
            widget.setBounds (cell.withSizeKeepingCentre (26, 26));
        else
            widget.setBounds (cell.reduced (1, 0));
    };
    const auto switchWidth = 44;
    place (voiceOn.label, voiceOn.button, voiceRow.removeFromLeft (switchWidth), true);
    place (voiceCorrect.label, voiceCorrect.button, voiceRow.removeFromLeft (switchWidth + 8), true);
    const auto knobWidth = voiceRow.getWidth() / 3;
    for (auto* knob : { &pitch, &fine, &formant })
        place (knob->label, knob->slider, voiceRow.removeFromLeft (knobWidth), false);

    right.removeFromTop (6);
    auto mixRow = right;
    const auto mixWidth = mixRow.getWidth() / 4;
    for (auto* knob : { &source, &harmony, &spread, &correctFormant })
        place (knob->label, knob->slider, mixRow.removeFromLeft (mixWidth), false);
}

void HarmonyPanel::paint (juce::Graphics& g)
{
    // The same dark panel and header as the other blocks' panels.
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, bounds.getY(),
                                             juce::Colour (0xff09070a), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
    Icons::draw (g, Icons::Type::pitch, header.removeFromLeft (26.0f).withSizeKeepingCentre (22.0f, 20.0f), ParagonColours::pitch, 1.6f, true);
    header.removeFromLeft (8.0f);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("PITCH", header, juce::Justification::centredLeft, false);

    // The sung note, big, under KEY and SCALE.
    if (! sungArea.isEmpty())
    {
        auto area = sungArea;
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("SINGING", area.removeFromTop (14.0f), juce::Justification::centred, false);
        g.setColour (shownSung == "--" ? ParagonColours::textDim.withAlpha (0.6f) : ParagonColours::pitch);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (shownSung, area, juce::Justification::centred, false);
    }

    // The graph: level lines across, the middle line down, L and R.
    g.setColour (juce::Colour (0xff0f0c10));
    g.fillRoundedRectangle (graph, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (graph.reduced (0.5f), 6.0f, 1.0f);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    for (const auto db : { 0, -6, -12, -24 })
    {
        const auto y = pointFor (0.0f, (float) db).y;
        g.setColour (ParagonColours::pitch.withAlpha (db == 0 ? 0.18f : 0.08f));
        g.drawHorizontalLine (juce::roundToInt (y), graph.getX() + 4.0f, graph.getRight() - 4.0f);
        g.setColour (ParagonColours::textDim.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (graph.getX() + 4.0f, y - 11.0f, 24.0f, 10.0f),
                    juce::Justification::centredLeft, false);
    }
    g.setColour (ParagonColours::pitch.withAlpha (0.12f));
    g.drawVerticalLine (juce::roundToInt (graph.getCentreX()), graph.getY() + 4.0f, graph.getBottom() - 4.0f);
    g.setColour (ParagonColours::textDim.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("L", graph.reduced (6.0f, 4.0f), juce::Justification::bottomLeft, false);
    g.drawText ("R", graph.reduced (6.0f, 4.0f), juce::Justification::bottomRight, false);

    // The voices: a dot each, labelled with its interval. Off voices are
    // grey; the picked one has a white ring and is drawn on top.
    const auto harmonyIsOn = value (ParamIDs::harmonyOn) > 0.5f;
    const auto drawVoice = [&] (int voice)
    {
        const auto centre = pointFor (value (voiceId (voice, "Pan")), value (voiceId (voice, "Level")));
        const auto dot = juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (centre);
        const auto sounding = harmonyIsOn && value (voiceId (voice, "On")) > 0.5f;
        const auto colour = value (voiceId (voice, "On")) > 0.5f ? ParagonColours::pitch : ParagonColours::textDim;
        if (sounding)
        {
            g.setColour (colour.withAlpha (0.2f));
            g.fillEllipse (dot.expanded (4.0f));
        }
        g.setColour (colour.withAlpha (sounding ? 1.0f : 0.45f));
        g.fillEllipse (dot);
        if (voice == selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (dot.expanded (2.5f), 1.6f);
        }
        g.setColour (juce::Colour (0xff101418));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (intervalText (value (voiceId (voice, "Pitch"))), dot, juce::Justification::centred, false);
    };
    for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
        if (isActive (voice) && voice != selected)
            drawVoice (voice);
    if (isActive (selected))
        drawVoice (selected);
    else
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawFittedText ("Click to add a voice", graph.reduced (8.0f).toNearestInt(), juce::Justification::centredTop, 1);
    }

    // The picked voice's pan and level, exactly.
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    const auto panText = parameters.getParameter (voiceId (selected, "Pan"))->getCurrentValueAsText();
    const auto levelText = parameters.getParameter (voiceId (selected, "Level"))->getCurrentValueAsText();
    g.drawText ("VOICE " + juce::String (selected + 1) + (isActive (selected) ? "   PAN " + panText + "   LEVEL " + levelText : juce::String ("   (not in use)")),
                readoutArea, juce::Justification::centred, false);
}
