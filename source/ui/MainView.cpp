#include "MainView.h"

#include "Parameters.h"
#include "ui/Logo.h"
#include "dsp/effects/Modulation.h"

namespace
{
    // The mockup image is 1672 px wide; our canvas is 1600. Every position
    // below is measured off the mockup and scaled by this.
    constexpr float mockScale = (float) MainView::width / 1672.0f;

    juce::Rectangle<float> mockF (float left, float top, float right, float bottom)
    {
        return juce::Rectangle<float>::leftTopRightBottom (left, top, right, bottom) * mockScale;
    }

    juce::Rectangle<int> mock (float left, float top, float right, float bottom)
    {
        return mockF (left, top, right, bottom).toNearestInt();
    }

    juce::Point<float> mockPoint (float x, float y) { return juce::Point<float> (x, y) * mockScale; }

    // Header groups.
    const auto tabsGroup = mockF (336, 10, 795, 55);
    const auto rigGroup = mockF (838, 10, 1135, 55);
    const auto tempoGroup = mockF (1250, 13, 1413, 52);
    constexpr float tempoDividerX = 1338;

    // The pedal unit (component bounds include room for its glow).
    const auto pedalBounds = mock (126, 68, 1552, 564);

    // The 2 x 3 button cluster: column centres and row tops.
    constexpr float clusterX[] = { 918, 975, 1032 };
    constexpr float clusterLabelY[] = { 119, 160 };
    constexpr float clusterButtonY[] = { 136, 177 };

    // Footswitch pads' left edges (each is 154 wide) and each row's top.
    // The bottom row (stomps 4-7) is where the original four were; the top
    // row (1-3) sits in the gaps between them, three over four, as on floor
    // units, so a foot on one top switch can't catch the two below it.
    constexpr float footswitchX[] = { 372.5f, 662, 948.5f,      // 1-3
                                      228, 517, 807, 1090 };    // 4-7
    constexpr float footswitchY[] = { 254, 254, 254, 400, 400, 400, 400 };

    void styleInkLabel (juce::Label& label, float size)
    {
        label.setFont (juce::FontOptions (size, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::ink);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
InkArrowButton::InkArrowButton (const juce::String& name, float direction) : juce::Button (name)
{
    path.addTriangle (0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
    path.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * direction, 0.5f, 0.5f));
}

void InkArrowButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto p = path;
    const auto offset = down ? 1.0f : 0.0f;
    p.applyTransform (path.getTransformToScaleToFit (offset, offset, (float) getWidth() - 3.0f, (float) getHeight() - 3.0f, false));
    juce::DropShadow (juce::Colours::black.withAlpha (0.3f), down ? 2 : 4, {}).drawForPath (g, p);
    g.setColour (Finish::ink());
    g.fillPath (p);
}

//==============================================================================
GainColumn::GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                        const juce::String& parameterId, LevelMeterSource& meterSource, Extra extra)
    : attachment (parameters, parameterId, knob),
      reductionDb (std::move (extra.reductionDb)),
      meter (meterSource)
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::FontOptions (14.5f, juce::Font::bold));

    // Set on the knob itself: the editor's look-and-feel is attached after the
    // knob's value box has already been created, so it wouldn't pick these up.
    knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    knob.setDoubleClickReturnValue (true, 0.0);   // double-click resets to 0 dB

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (knob);
    addAndMakeVisible (meter);

    if (extra.parameterId.isNotEmpty())
    {
        extraLabel.setText (extra.title, juce::dontSendNotification);
        extraLabel.setJustificationType (juce::Justification::centred);
        extraLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        extraLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));

        extraAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, extra.parameterId, extraKnob);
        extraKnob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        extraKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        extraKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        if (auto* parameter = parameters.getParameter (extra.parameterId))   // double-click: back to the default
            extraKnob.setDoubleClickReturnValue (true, parameters.getParameterRange (extra.parameterId).convertFrom0to1 (parameter->getDefaultValue()));

        addAndMakeVisible (extraLabel);
        addAndMakeVisible (extraKnob);
    }

    if (reductionDb)
        startTimerHz (30);
}

juce::Rectangle<float> GainColumn::lightArea() const
{
    // Just right of the title's text.
    const auto textWidth = juce::GlyphArrangement::getStringWidth (extraLabel.getFont(), extraLabel.getText());
    const auto bounds = extraLabel.getBounds().toFloat();
    return juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ bounds.getCentreX() + textWidth * 0.5f + 7.0f, bounds.getCentreY() });
}

void GainColumn::timerCallback()
{
    // Lit from 0.1 dB of limiting, so it shows real work rather than noise.
    const auto shouldBeOn = reductionDb() < -0.1f;
    if (shouldBeOn != lightOn)
    {
        lightOn = shouldBeOn;
        repaint (lightArea().expanded (4.0f).getSmallestIntegerContainer());
    }
}

void GainColumn::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());

    if (reductionDb)
    {
        // The limiter's light: dark when idle, glowing orange while it limits.
        const auto light = lightArea();
        if (lightOn)
        {
            g.setColour (ParagonColours::accent.withAlpha (0.35f));
            g.fillEllipse (light.expanded (2.5f));
        }
        g.setColour (lightOn ? ParagonColours::accent : juce::Colours::white.withAlpha (0.15f));
        g.fillEllipse (light);
    }
}

void GainColumn::resized()
{
    // Proportions from the mockup: title, a compact knob, its value in
    // orange, then a tall slim meter.
    // With an extra knob (LOW CUT, LIMITER), it goes under the gain knob, in
    // signal order, and the meter starts lower.
    auto area = getLocalBounds();
    titleLabel.setBounds (area.withHeight (26).withY (14));
    knob.setBounds (juce::Rectangle<int> (76, 92).withCentre ({ area.getCentreX(), 86 }));
    auto meterTop = 146;
    if (extraAttachment != nullptr)
    {
        extraLabel.setBounds (area.withHeight (14).withY (140));
        extraKnob.setBounds (juce::Rectangle<int> (60, 66).withCentre ({ area.getCentreX(), 189 }));
        meterTop = 232;
    }
    meter.setBounds (juce::Rectangle<int> (46, 0).withX (area.getCentreX() - 26).withY (meterTop).withBottom (area.getBottom() - 22));
}

//==============================================================================
MainView::MainView (ParagonX1AudioProcessor& processorToShow)
    : audioProcessor (processorToShow),
      inputColumn ("INPUT", processorToShow.getParameters(), ParamIDs::inputGain, processorToShow.getInputMeter(),
                   { ParamIDs::inputLowCut, "LOW CUT", {} }),
      outputColumn ("OUTPUT", processorToShow.getParameters(), ParamIDs::outputGain, processorToShow.getOutputMeter(),
                    { ParamIDs::outputLimiter, "LIMITER", [&processorToShow] { return processorToShow.getLimiterReductionDb(); } }),
      bypassAttachment (processorToShow.getParameters(), ParamIDs::bypass, bypassButton),
      tunerAttachment (processorToShow.getParameters(), ParamIDs::tunerOn, tunerPedal),
      muteAttachment (processorToShow.getParameters(), ParamIDs::tunerMute, mutePedal),
      rigLevelAttachment (processorToShow.getParameters(), ParamIDs::rigLevel, rigLevelKnob),
      presenceAttachment (processorToShow.getParameters(), ParamIDs::presence, presenceKnob),
      expressionPedal (processorToShow),
      tunerReader (processorToShow),
      faceplate (processorToShow),
      expressionPanel (processorToShow.getParameters()),
      eqPanel (processorToShow.getParameters()),
      harmonyPanel (processorToShow.getParameters(), [&processorToShow] { return processorToShow.getHarmonySungNote(); },
                    [&processorToShow] { return processorToShow.getHarmonyCorrectNote(); }, &processorToShow.getHarmony()),
      doublerPanel (processorToShow.getParameters(), [&processorToShow] { processorToShow.resetDoublerModulation(); }),
      marsPanel (processorToShow.getParameters(), [&processorToShow] (int band) { return processorToShow.getMarsBandDrive (band); },
                 &processorToShow.getMars()),
      // Each block in the red to pink to orange of the PR7 family's chains, by
      // its place in the chain: red first, orange last.
      signalChain (processorToShow.getParameters(), {
          { "PITCH", Icons::Type::pitch, ParagonColours::chainColour (0, numBlocks),
            juce::String (ParamIDs::harmonyOn) + "," + ParamIDs::octOn + "," + ParamIDs::freezeOn + "," + ParamIDs::harmonyCorrect },
          { "DYNAMICS", Icons::Type::comp, ParagonColours::chainColour (1, numBlocks), ParamIDs::compOn },
          { "DE-ESS", Icons::Type::deEsser, ParagonColours::chainColour (2, numBlocks), ParamIDs::deessOn },
          { "FILTER / PITCH", Icons::Type::filterPitch, ParagonColours::chainColour (3, numBlocks), ParamIDs::fpOn },
          { "SATURATION", Icons::Type::drive, ParagonColours::chainColour (4, numBlocks), ParamIDs::driveOn },
          { "EQ", Icons::Type::eq, ParagonColours::chainColour (5, numBlocks), ParamIDs::eqOn },
          { "GLITCH", Icons::Type::glitch, ParagonColours::chainColour (6, numBlocks), ParamIDs::glitchOn },
          { "DOUBLER", Icons::Type::doubler, ParagonColours::chainColour (7, numBlocks), ParamIDs::doublerOn },
          { "MOD", Icons::Type::modulation, ParagonColours::chainColour (8, numBlocks), ParamIDs::modOn },   // MODULATION: short, to fit the strip
          { "DELAY", Icons::Type::delay, ParagonColours::chainColour (9, numBlocks), ParamIDs::delayOn },
          { "REVERB", Icons::Type::reverb, ParagonColours::chainColour (10, numBlocks), ParamIDs::reverbOn },
          { "LO-FI", Icons::Type::loFi, ParagonColours::chainColour (11, numBlocks), ParamIDs::lofiOn } }),
      rigBrowser (processorToShow),
      settingsPage (processorToShow)
{
    auto& state = processorToShow.getParameters();

    //==========================================================================
    // Header
    for (auto* tab : { &processorTab, &presetsTab, &settingsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (100);
        addAndMakeVisible (*tab);
    }
    processorTab.setToggleState (true, juce::dontSendNotification);
    processorTab.setComponentID ("processorTab");
    presetsTab.setComponentID ("presetsTab");
    settingsTab.setComponentID ("settingsTab");
    processorTab.onClick = [this] { showPage (Page::processor); };
    presetsTab.onClick = [this] { showPage (Page::presets); };
    settingsTab.onClick = [this] { showPage (Page::settings); };

    previousRig.onClick = [this] { audioProcessor.stepRig (-1); };
    nextRig.onClick = [this] { audioProcessor.stepRig (1); };
    rigNameButton.getProperties().set (ParagonLookAndFeel::plainTextProperty, 19.0f);
    rigNameButton.setColour (juce::TextButton::textColourOffId, ParagonColours::accent);
    rigNameButton.setTooltip ("The current rig. * means it has changed since it was loaded or saved. Click to browse rigs.");
    rigNameButton.onClick = [this] { showPage (Page::presets); };

    // Tempo: shows the DAW's tempo, or the tapped one (in orange) once TAP
    // has been used. Clicking a tapped tempo goes back to following the DAW.
    tempoButton.getProperties().set (ParagonLookAndFeel::plainTextProperty, 14.0f);
    tempoButton.onClick = [this] { audioProcessor.clearTappedTempo(); refreshPerformance(); };
    tapButton.onClick = [this] { if (! headerTapHold.consumeHold()) tap(); };
    tapButton.addMouseListener (&headerTapHold, false);
    headerTapHold.onHold = [this] { toggleTuner(); };
    undoButton.onClick = [this] { audioProcessor.undo(); };
    redoButton.onClick = [this] { audioProcessor.redo(); };
    saveButton.onClick = [this] { saveRig(); };
    settingsButton.onClick = [this] { showPage (page == Page::settings ? Page::processor : Page::settings); };

    for (auto* component : std::initializer_list<juce::Component*> { &previousRig, &nextRig, &rigNameButton, &tempoButton, &tapButton,
                                                                      &undoButton, &redoButton, &saveButton, &settingsButton,
                                                                      &inputColumn, &outputColumn })
        addAndMakeVisible (component);

    //==========================================================================
    // The pedal unit
    display.onClick = [this] { showPage (Page::presets); };
    rigUp.onClick = [this] { audioProcessor.stepRig (-1); };
    rigDown.onClick = [this] { audioProcessor.stepRig (1); };

    for (auto* button : { &pedalSave, &pedalSaveAs, &bypassButton, &undoPedal, &redoPedal, &tunerPedal, &mutePedal, &tapPedal })
        button->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
    pedalSave.onClick = [this] { saveRig(); };
    pedalSaveAs.onClick = [this] { rigBrowser.promptSaveAs(); };
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass the whole plugin (the same as your DAW's bypass button)");

    undoPedal.setTooltip ("Undo the last change (Ctrl+Z)");
    redoPedal.setTooltip ("Redo (Ctrl+Shift+Z)");
    undoPedal.onClick = [this] { audioProcessor.undo(); };
    redoPedal.onClick = [this] { audioProcessor.redo(); };
    tunerPedal.setClickingTogglesState (true);
    tunerPedal.setTooltip ("Tuner: shows the note you're playing on the screen");
    mutePedal.setClickingTogglesState (true);
    mutePedal.setTooltip ("Mute the output while the tuner is on, so you can tune silently");
    tapPedal.setTooltip ("Tap the tempo for the delay (its light flashes in time). Hold to open the tuner.");
    tapPedal.onClick = [this] { if (! pedalTapHold.consumeHold()) tap(); };
    tapPedal.addMouseListener (&pedalTapHold, false);
    pedalTapHold.onHold = [this] { toggleTuner(); };

    // The cluster's labels are printed in red, as in the mockup.
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel, &tunerLabel, &muteLabel, &tapLabel })
    {
        styleInkLabel (*label, 9.5f);
        label->setColour (juce::Label::textColourId, ParagonColours::glow.darker (0.15f));
    }
    styleInkLabel (holdLabel, 8.0f);
    holdLabel.setColour (juce::Label::textColourId, ParagonColours::glow.darker (0.15f));

    for (auto* knob : { &rigLevelKnob, &presenceKnob })
    {
        knob->getProperties().set (ParagonLookAndFeel::pedalKnobProperty, true);
        knob->setDoubleClickReturnValue (true, 0.0);
        knob->setPopupDisplayEnabled (true, true, this);   // shows the value while dragging
    }
    rigLevelKnob.setTooltip ("This rig's volume, saved with the rig: use it to balance rigs. "
                             "The OUTPUT knob on the right is the overall volume.");
    presenceKnob.setTooltip ("Cut or boost the top end (above about 3.5 kHz): more air and presence on the voice, or less harshness.");
    styleInkLabel (rigLevelLabel, 12.5f);
    styleInkLabel (presenceLabel, 12.5f);

    tunerReader.onReading = [this] (const TunerReader::Reading& reading)
    {
        tunerState.hasNote = reading.hasNote;
        tunerState.note = reading.note.name;
        tunerState.octave = reading.note.octave;
        tunerState.cents = reading.note.cents;
        tunerState.frequency = reading.frequency;
        refreshRig();
    };

    // The seven stomps. Each press turns its block on or off; clicking its
    // name shows the block's knobs. The order is the owner's, for singing:
    // the top row shapes the voice, the bottom row adds to it. (PITCH works
    // first in the sound, where it tracks the clean voice best: see the
    // processor.)
    const auto addStomp = [this, &state] (int number, const char* name, juce::StringArray onIds, int block,
                                          std::function<juce::String()> describe, juce::StringArray firstOn = {})
    {
        auto& footswitch = footswitches[(size_t) number - 1];
        footswitch = std::make_unique<Footswitch> (number, name, state, std::move (onIds), std::move (firstOn));
        footswitch->describe = std::move (describe);
        footswitch->onSelect = [this, block] { selectBlock (block); };
        footswitch->refresh();
    };
    // DYNAMICS switches the compressor and the de-esser together, the usual
    // pair on a voice; each also has its own ON in its panel (the
    // de-esser's is the DE-ESS block in the signal chain, after DYNAMICS).
    addStomp (1, "DYNAMICS", { ParamIDs::compOn, ParamIDs::deessOn }, compBlock, [this]
    {
        juce::String text;
        if (juce::roundToInt (rawValue (ParamIDs::compType)) == (int) effects::CompGate::Type::revival)
        {
            text << "Revival  " << valueText (ParamIDs::revivalCompression);
            if (rawValue (ParamIDs::revivalGate) > effects::CompGate::revivalGateOffDb)
                text << "  + gate";
        }
        else
        {
            text << valueText (ParamIDs::compThreshold) << "  " << valueText (ParamIDs::compRatio);
            if (rawValue (ParamIDs::gateThreshold) > effects::CompGate::gateOffDb)
                text << "  + gate";
        }
        if (rawValue (ParamIDs::deessOn) > 0.5f)
            text << "  + de-ess";
        return text;
    });
    addStomp (2, "SATURATION", { ParamIDs::driveOn }, driveBlock, [this]
    {
        // MARS: its bands' styles, low to high.
        const auto numBands = juce::roundToInt (rawValue (ParamIDs::marsBands)) + 1;
        juce::StringArray styles;
        for (int band = 0; band < numBands; ++band)
            styles.add (effects::Mars::styleNames[juce::roundToInt (rawValue (ParamIDs::marsBand (band, "Style").toRawUTF8()))]);
        return "Mars  " + styles.joinIntoString (" / ");
    });
    addStomp (3, "DOUBLER", { ParamIDs::doublerOn }, doublerBlock, [this]
    {
        juce::StringArray on;
        for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
            if (rawValue (ParamIDs::doublerVoice (voice, "On").toRawUTF8()) > 0.5f)
                on.add (juce::String (voice + 1));
        return on.isEmpty() ? juce::String ("No voices") : (on.size() == 1 ? "Voice " : "Voices ") + on.joinIntoString (", ");
    });
    // HARMONY, the octaver and TUNE VOICE (which works without HARMONY, so
    // the stomp must switch it too, or the voice stays tuned, and 40 ms
    // late, with the stomp's light out); it brings back whichever were on,
    // TUNE VOICE in the mode it had, and the first time, HARMONY. FREEZE
    // has its own switch in the panel (a press turning it on too would hold
    // a note by surprise).
    addStomp (4, "PITCH", { ParamIDs::harmonyOn, ParamIDs::octOn, ParamIDs::harmonyCorrect }, pitchBlock, [this]
    {
        juce::StringArray parts;
        if (rawValue (ParamIDs::harmonyOn) > 0.5f)
            parts.add ("Harmony " + valueText (ParamIDs::harmonyKey) + " " + valueText (ParamIDs::harmonyScale));
        if (rawValue (ParamIDs::harmonyCorrect) > 0.5f)
            parts.add ("Tune Voice " + valueText (ParamIDs::harmonyCorrect));
        if (rawValue (ParamIDs::octOn) > 0.5f)
            parts.add ("Octaver");
        return parts.joinIntoString ("  ");
    }, { ParamIDs::harmonyOn });
    addStomp (5, "MODULATION", { ParamIDs::modOn }, modBlock, [this]
    {
        return valueText (ParamIDs::modType) + "  " + valueText (rawValue (ParamIDs::modSync) > 0.5f ? ParamIDs::modSync : ParamIDs::modRate);
    });
    addStomp (6, "DELAY", { ParamIDs::delayOn }, delayBlock, [this]
    {
        return valueText (ParamIDs::delayType) + "  " + valueText (rawValue (ParamIDs::delaySync) > 0.5f ? ParamIDs::delaySync : ParamIDs::delayTime);
    });
    addStomp (7, "REVERB", { ParamIDs::reverbOn }, reverbBlock, [this]
    {
        // The tail's length in seconds, from TYPE, SIZE and DECAY together (as LXP's display shows it).
        const auto type = (effects::Reverb::Type) juce::roundToInt (rawValue (ParamIDs::reverbType));
        const auto seconds = effects::Reverb::decaySeconds (type, rawValue (ParamIDs::reverbSize), rawValue (ParamIDs::reverbDecay));
        return valueText (ParamIDs::reverbType) + "  " + juce::String (seconds, seconds < 10.0f ? 1 : 0) + " s  Mix " + valueText (ParamIDs::reverbMix);
    });

    addAndMakeVisible (pedal);   // first, so everything on the pedal sits on top of it
    processorPage = { &pedal, &display, &rigUp, &rigDown, &pedalSave, &pedalSaveAs, &bypassButton,
                      &undoPedal, &redoPedal, &tunerPedal, &mutePedal, &tapPedal,
                      &undoLabel, &redoLabel, &bypassLabel, &tunerLabel, &muteLabel, &tapLabel, &holdLabel,
                      &rigLevelKnob, &presenceKnob, &rigLevelLabel, &presenceLabel,
                      &expressionPedal,
                      &faceplate, &expressionPanel };
    for (auto& footswitch : footswitches)
        processorPage.push_back (footswitch.get());
    for (auto* component : processorPage)
        addAndMakeVisible (component);

    //==========================================================================
    // Edit panels, one per block, shown in place of the rig summary. Each
    // starts with the block's on switch.
    using Group = EffectPanel::Group;
    // The compressor's TYPE shows only its own knobs: Classic's THRESHOLD and
    // RATIO, Daybreak's COMP, DETAIL, MIX and INTENSITY, or Revival's
    // COMPRESSION and GAIN (in place of LEVEL) and its own GATE (in place of
    // the noise gate's THRESHOLD).
    panels[pitchBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "TUNING", { { ParamIDs::pitchTune, "TUNING" } } },
        { "OCTAVER", { { ParamIDs::octOn, "ON" }, { ParamIDs::octSubTwo, "2 DOWN" }, { ParamIDs::octSubOne, "1 DOWN" },
                       { ParamIDs::octDry, "DRY" }, { ParamIDs::octUp, "1 UP" } } },
        { "FREEZE", { { ParamIDs::freezeOn, "FREEZE" }, { ParamIDs::freezeLevel, "LEVEL" }, { ParamIDs::freezeAttack, "ATTACK" },
                      { ParamIDs::freezeRelease, "RELEASE" } } } });
    const auto compIs = [this] (effects::CompGate::Type type)
    {
        return [this, type] { return juce::roundToInt (rawValue (ParamIDs::compType)) == (int) type; };
    };
    const auto classic = compIs (effects::CompGate::Type::classic), daybreak = compIs (effects::CompGate::Type::daybreak);
    const auto revival = compIs (effects::CompGate::Type::revival);
    const auto notRevival = [revival] { return ! revival(); };
    panels[compBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "COMPRESSOR", { { ParamIDs::compOn, "ON" }, { ParamIDs::compType, "TYPE" },
                          { ParamIDs::compThreshold, "THRESHOLD", classic, true }, { ParamIDs::compRatio, "RATIO", classic, true },
                          { ParamIDs::compAmount, "COMP", daybreak, true }, { ParamIDs::compDetail, "DETAIL", daybreak, true },
                          { ParamIDs::compMix, "MIX", daybreak, true }, { ParamIDs::compIntensity, "INTENSITY", daybreak, true },
                          { ParamIDs::revivalCompression, "COMPRESSION", revival, true },
                          { ParamIDs::revivalGain, "GAIN", revival, true },
                          { ParamIDs::compLevel, "LEVEL", notRevival, true } } },
        { "NOISE GATE", { { ParamIDs::gateThreshold, "THRESHOLD", notRevival, true },
                          { ParamIDs::revivalGate, "GATE", revival, true } } } });
    panels[compBlock]->setMeter ([this] { return audioProcessor.getCompGainReductionDb(); });
    // The de-esser, after the compressor; the DYNAMICS stomp switches both.
    panels[deEssBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "DE-ESSER", { { ParamIDs::deessOn, "ON" }, { ParamIDs::deessFreq, "FREQUENCY" }, { ParamIDs::deessAmount, "AMOUNT" },
                        { ParamIDs::deessListen, "LISTEN" } } } });
    panels[deEssBlock]->setMeter ([this] { return audioProcessor.getDeEsserReductionDb(); });
    const auto fpIs = [this] (effects::FilterPitch::Type type)
    {
        return [this, type] { return juce::roundToInt (rawValue (ParamIDs::fpType)) == (int) type; };
    };
    panels[filterPitchBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "FILTER / PITCH", { { ParamIDs::fpOn, "ON" }, { ParamIDs::fpType, "TYPE" },
                              { ParamIDs::fpSensitivity, "SENSITIVITY", fpIs (effects::FilterPitch::Type::autoWah) },
                              { ParamIDs::fpResonance, "RESONANCE", [autoWah = fpIs (effects::FilterPitch::Type::autoWah),
                                                                      talkBox = fpIs (effects::FilterPitch::Type::talkBox)] { return autoWah() || talkBox(); } },
                              { ParamIDs::fpInterval, "INTERVAL", fpIs (effects::FilterPitch::Type::pitchShifter) },
                              { ParamIDs::fpXpProgram, "PROGRAM", fpIs (effects::FilterPitch::Type::whammyWah) },
                              { ParamIDs::fpMix, "MIX" } } } });
    // SATURATION has MARS's own panel (panels[driveBlock] stays empty).
    // AEON (the flanger) adds its pedal's MANUAL and RES.
    const auto isAeon = [this] { return juce::roundToInt (rawValue (ParamIDs::modType)) == (int) effects::Modulation::Type::flanger; };
    panels[modBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "MODULATION", { { ParamIDs::modOn, "ON" }, { ParamIDs::modType, "TYPE" }, { ParamIDs::modSync, "SYNC" },
                          { ParamIDs::modManual, "MANUAL", isAeon, true },
                          { ParamIDs::modRate, "RATE", [this] { return rawValue (ParamIDs::modSync) < 0.5f; } },
                          { ParamIDs::modDepth, "DEPTH" },
                          { ParamIDs::modRes, "RES", isAeon, true },
                          { ParamIDs::modMix, "MIX", [this] {
                              return effects::Modulation::usesMix ((effects::Modulation::Type) juce::roundToInt (rawValue (ParamIDs::modType))); } } } } });
    // DELAY's three pages (since 0.10.0, with the controls inspired by
    // H-Delay): MAIN as it always was; STEREO (DUAL's right-hand time, the
    // ping-pong's START, each side's PHASE); COLOUR (the filters, MOD,
    // ANALOG and LO-FI).
    const auto dual = [this] { return rawValue (ParamIDs::delayDual) > 0.5f && rawValue (ParamIDs::delayPingPong) < 0.5f; };
    panels[delayBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "DELAY", { { ParamIDs::delayOn, "ON" }, { ParamIDs::delayType, "TYPE" }, { ParamIDs::delaySync, "SYNC" },
                     { ParamIDs::delayTime, "TIME", [this] { return rawValue (ParamIDs::delaySync) < 0.5f; } },
                     { ParamIDs::delayFeedback, "FEEDBACK" }, { ParamIDs::delayTone, "TONE" }, { ParamIDs::delayMix, "MIX" }, { ParamIDs::delayDuck, "DUCK" },
                     { ParamIDs::delayPingPong, "PING-PONG" } } },
        { "DUAL", { { ParamIDs::delayDual, "DUAL", [this] { return rawValue (ParamIDs::delayPingPong) < 0.5f; } },
                    { ParamIDs::delaySyncRight, "RIGHT SYNC", dual },
                    { ParamIDs::delayTimeRight, "RIGHT TIME", [this, dual] { return dual() && rawValue (ParamIDs::delaySyncRight) < 0.5f; } } }, 1 },
        { "PING-PONG", { { ParamIDs::delayPingStart, "START", [this] { return rawValue (ParamIDs::delayPingPong) > 0.5f; } } }, 1 },
        { "PHASE", { { ParamIDs::delayPhaseLeft, "FLIP LEFT" }, { ParamIDs::delayPhaseRight, "FLIP RIGHT" } }, 1 },
        { "FILTERS", { { ParamIDs::delayLowCut, "LOW CUT" }, { ParamIDs::delayHighCut, "HIGH CUT" } }, 2 },
        { "MODULATION", { { ParamIDs::delayModDepth, "DEPTH" }, { ParamIDs::delayModRate, "RATE" } }, 2 },
        { "CHARACTER", { { ParamIDs::delayAnalog, "ANALOG" }, { ParamIDs::delayLoFi, "LO-FI" } }, 2 } });
    panels[delayBlock]->setPages ({ "MAIN", "STEREO", "COLOUR" });
    // REVERB's four pages (since 0.11.0, with the controls inspired by the
    // Lexicon LXP reverbs, for every type but Classic and Spring, which grey
    // them out): MAIN; DECAY (BASS RT and its CROSSOVER, ROLLOFF); SPACE
    // (DIFFUSION, the EARLY and REVERB levels, SPREAD and SHAPE); MOTION
    // (SPIN and WANDER).
    const auto lxp = [this] { return effects::Reverb::hasNetworkControls ((effects::Reverb::Type) juce::roundToInt (rawValue (ParamIDs::reverbType))); };
    panels[reverbBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "REVERB", { { ParamIDs::reverbOn, "ON" }, { ParamIDs::reverbType, "TYPE" }, { ParamIDs::reverbSize, "SIZE" },
                      { ParamIDs::reverbDecay, "DECAY", lxp },
                      { ParamIDs::reverbPreDelay, "PRE-DELAY" }, { ParamIDs::reverbTone, "TONE" }, { ParamIDs::reverbMix, "MIX" },
                      { ParamIDs::reverbDuck, "DUCK" } } },
        { "BASS", { { ParamIDs::reverbBassRt, "BASS RT", lxp }, { ParamIDs::reverbCrossover, "CROSSOVER", lxp } }, 1 },
        { "OUTPUT", { { ParamIDs::reverbRolloff, "ROLLOFF", lxp } }, 1 },
        { "DIFFUSION", { { ParamIDs::reverbDiffusion, "DIFFUSION", lxp } }, 2 },
        { "LEVELS", { { ParamIDs::reverbEarly, "EARLY", lxp }, { ParamIDs::reverbLevel, "REVERB", lxp } }, 2 },
        { "BUILD", { { ParamIDs::reverbSpread, "SPREAD", lxp }, { ParamIDs::reverbShape, "SHAPE", lxp } }, 2 },
        { "MOTION", { { ParamIDs::reverbSpin, "SPIN", lxp }, { ParamIDs::reverbWander, "WANDER", lxp } }, 3 } });
    panels[reverbBlock]->setPages ({ "MAIN", "DECAY", "SPACE", "MOTION" });
    // GLITCH's LENGTH is a time, or a note value while synced: the one that
    // applies shows.
    const auto glitchSynced = [this] { return rawValue (ParamIDs::glitchSync) > 0.5f; };
    const auto glitchFree = [this] { return rawValue (ParamIDs::glitchSync) <= 0.5f; };
    panels[glitchBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "GLITCH", { { ParamIDs::glitchOn, "ON" }, { ParamIDs::glitchMode, "MODE" }, { ParamIDs::glitchPosition, "POSITION" },
                      { ParamIDs::glitchSync, "SYNC" },
                      { ParamIDs::glitchTime, "LENGTH", glitchFree, true }, { ParamIDs::glitchDivision, "LENGTH", glitchSynced, true },
                      { ParamIDs::glitchDensity, "DENSITY" }, { ParamIDs::glitchPitch, "PITCH" }, { ParamIDs::glitchChaos, "CHAOS" },
                      { ParamIDs::glitchMix, "MIX" } } } });
    panels[loFiBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "LO-FI", { { ParamIDs::lofiOn, "ON" }, { ParamIDs::lofiType, "TYPE" }, { ParamIDs::lofiAmount, "AMOUNT" },
                     { ParamIDs::lofiNoise, "NOISE" }, { ParamIDs::lofiMix, "MIX" }, { ParamIDs::lofiLevel, "LEVEL" } } } });
    // Each panel's header and accents in its block's colour in the chain.
    const Icons::Type headers[] = { Icons::Type::pitch, Icons::Type::comp, Icons::Type::deEsser, Icons::Type::filterPitch,
                                    Icons::Type::drive, Icons::Type::eq, Icons::Type::glitch, Icons::Type::doubler,
                                    Icons::Type::modulation, Icons::Type::delay, Icons::Type::reverb, Icons::Type::loFi };
    const char* titles[] = { "PITCH", "DYNAMICS", "DE-ESSER", "FILTER / PITCH", "SATURATION", "GRAPHIC EQ", "GLITCH", "DOUBLER", "MODULATION", "DELAY", "REVERB", "LO-FI" };
    static_assert (std::size (headers) == numBlocks && std::size (titles) == numBlocks);
    for (size_t i = 0; i < panels.size(); ++i)
    {
        if (panels[i] == nullptr)
            continue;
        panels[i]->setHeader (headers[i], ParagonColours::chainColour ((int) i, numBlocks), titles[i]);
        addChildComponent (*panels[i]);
    }
    eqPanel.setAccent (ParagonColours::chainColour (eqBlock, numBlocks));
    harmonyPanel.setAccent (ParagonColours::chainColour (pitchBlock, numBlocks));
    doublerPanel.setAccent (ParagonColours::chainColour (doublerBlock, numBlocks));
    marsPanel.setAccent (ParagonColours::chainColour (driveBlock, numBlocks));
    addChildComponent (eqPanel);
    addChildComponent (harmonyPanel);
    harmonyPanel.setMidiKeys ([&processorToShow] (int key) { return processorToShow.isMidiKeyHeld (key); });
    harmonyPanel.setSnapshotActions ({ [&processorToShow] (int slot) { return processorToShow.getHarmonySnapshot (slot); },
                                       [&processorToShow] (int slot) { processorToShow.storeHarmonySnapshot (slot); },
                                       [&processorToShow] (int slot) { processorToShow.recallHarmonySnapshot (slot); },
                                       [&processorToShow] (int slot, std::optional<Rig::HarmonySnapshot> snapshot) { processorToShow.setHarmonySnapshot (slot, std::move (snapshot)); },
                                       [&processorToShow] { return juce::roundToInt (processorToShow.getParameters().getRawParameterValue (ParamIDs::harmonySnapshot)->load()); } });
    addChildComponent (doublerPanel);
    addChildComponent (marsPanel);
    for (auto* view : { &harmonyView, &octaveView })
    {
        view->setClickingTogglesState (true);
        view->setRadioGroupId (4211);
        view->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        view->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);   // the view showing, in orange
        addChildComponent (*view);
    }
    harmonyView.setToggleState (true, juce::dontSendNotification);
    harmonyView.onClick = [this] { pitchShowsHarmony = true; selectBlock (selectedBlock); };
    octaveView.onClick = [this] { pitchShowsHarmony = false; selectBlock (selectedBlock); };

    closePanel.setTooltip ("Back to the graphic EQ");
    closePanel.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    closePanel.onClick = [this] { selectBlock (eqBlock); };
    addChildComponent (closePanel);


    signalChain.onSelect = [this] (int block) { selectBlock (block); };
    addAndMakeVisible (signalChain);
    addChildComponent (rigBrowser);
    addChildComponent (settingsPage);

    selectBlock (eqBlock);   // the dock shows the graphic EQ until another block is picked

    audioProcessor.rigBroadcaster.addChangeListener (this);
    changeListenerCallback (nullptr);
    Finish::changes().addChangeListener (this);
    applyFinish();

    // The modified marker, tempo and undo state can change from the audio
    // thread or the host, so poll them. 30 Hz for the TAP light's flash.
    startTimerHz (30);
    timerCallback();

    setWantsKeyboardFocus (true);
}

MainView::~MainView()
{
    Finish::changes().removeChangeListener (this);
    audioProcessor.rigBroadcaster.removeChangeListener (this);
}

juce::String MainView::valueText (const char* parameterId) const
{
    return audioProcessor.getParameters().getParameter (parameterId)->getCurrentValueAsText();
}

float MainView::rawValue (const char* parameterId) const
{
    return audioProcessor.getParameters().getRawParameterValue (parameterId)->load();
}

void MainView::selectBlock (int block)
{
    // The selected block's knobs fill the middle of the dock; the graphic EQ is the default.
    selectedBlock = block;
    const auto onProcessorPage = page == Page::processor;
    for (size_t i = 0; i < panels.size(); ++i)
        if (panels[i] != nullptr)
            panels[i]->setVisible ((int) i == block && onProcessorPage && ! (block == pitchBlock && pitchShowsHarmony));
    eqPanel.setVisible (block == eqBlock && onProcessorPage);
    const auto showingPitch = block == pitchBlock && onProcessorPage;
    harmonyPanel.setVisible (showingPitch && pitchShowsHarmony);
    doublerPanel.setVisible (block == doublerBlock && onProcessorPage);
    marsPanel.setVisible (block == driveBlock && onProcessorPage);
    harmonyView.setToggleState (pitchShowsHarmony, juce::dontSendNotification);
    octaveView.setToggleState (! pitchShowsHarmony, juce::dontSendNotification);
    for (auto* view : { &harmonyView, &octaveView })
    {
        view->setVisible (showingPitch);
        if (showingPitch)
            view->toFront (false);
    }
    closePanel.setVisible (block != eqBlock && onProcessorPage);
    if (block != eqBlock)
        closePanel.toFront (false);

    // The FACEPLATE: the block as hardware (its own for DYNAMICS and
    // SATURATION so far; a rack unit with its name and TYPE for the rest).
    struct About { const char* title; Icons::Type icon; juce::String onIds; const char* typeId; };
    const About about[] = {
        { "PITCH", Icons::Type::pitch, juce::String (ParamIDs::harmonyOn) + "," + ParamIDs::octOn + "," + ParamIDs::freezeOn + "," + ParamIDs::harmonyCorrect, "" },
        { "DYNAMICS", Icons::Type::comp, ParamIDs::compOn, ParamIDs::compType },
        { "DE-ESSER", Icons::Type::deEsser, ParamIDs::deessOn, "" },
        { "FILTER / PITCH", Icons::Type::filterPitch, ParamIDs::fpOn, ParamIDs::fpType },
        { "SATURATION", Icons::Type::drive, ParamIDs::driveOn, "" },
        { "GRAPHIC EQ", Icons::Type::eq, ParamIDs::eqOn, "" },
        { "GLITCH", Icons::Type::glitch, ParamIDs::glitchOn, ParamIDs::glitchMode },
        { "DOUBLER", Icons::Type::doubler, ParamIDs::doublerOn, "" },
        { "MODULATION", Icons::Type::modulation, ParamIDs::modOn, ParamIDs::modType },
        { "DELAY", Icons::Type::delay, ParamIDs::delayOn, ParamIDs::delayType },
        { "REVERB", Icons::Type::reverb, ParamIDs::reverbOn, ParamIDs::reverbType },
        { "LO-FI", Icons::Type::loFi, ParamIDs::lofiOn, ParamIDs::lofiType } };
    static_assert (std::size (about) == numBlocks);
    const auto& shown = about[(size_t) juce::jlimit (0, numBlocks - 1, block)];
    const auto unitFor = [] (int which)
    {
        switch (which)
        {
            case compBlock:   return Faceplate::Unit::dynamics;
            case driveBlock:  return Faceplate::Unit::saturation;
            case delayBlock:  return Faceplate::Unit::delay;
            case reverbBlock: return Faceplate::Unit::reverb;
            case pitchBlock:  return Faceplate::Unit::pitch;
            case modBlock:    return Faceplate::Unit::modulation;
            case filterPitchBlock: return Faceplate::Unit::filterPitch;
            case deEssBlock:  return Faceplate::Unit::deEsser;
            case glitchBlock: return Faceplate::Unit::glitch;
            case doublerBlock: return Faceplate::Unit::doubler;
            case eqBlock:     return Faceplate::Unit::eq;
            case loFiBlock:   return Faceplate::Unit::loFi;
            default:          return Faceplate::Unit::other;
        }
    };
    faceplate.setBlock (unitFor (block), shown.title, shown.icon, ParagonColours::chainColour (block, numBlocks), shown.onIds, shown.typeId);

    const int footswitchBlocks[] = { compBlock, driveBlock, doublerBlock, pitchBlock, modBlock, delayBlock, reverbBlock };
    static_assert (std::size (footswitchBlocks) == std::tuple_size_v<decltype (footswitches)>);
    for (size_t i = 0; i < footswitches.size(); ++i)
        footswitches[i]->setSelected (footswitchBlocks[i] == block || (footswitchBlocks[i] == compBlock && block == deEssBlock));

    signalChain.setSelected (block);
    layoutDock();
}

void MainView::layoutDock()
{
    // The rack: the faceplate and the block's controls as one wide unit, the
    // display on the left and the real controls on its face. For now (the
    // owner's example) only DELAY; the rest keep the faceplate beside the panel.
    const auto rack = selectedBlock == delayBlock;
    closePanel.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, rack);   // one of the unit's keys, on the rack
    for (auto& panel : panels)
        if (panel != nullptr)
            panel->setRack (rack && panel == panels[delayBlock]);

    const auto summaryArea = mock (662, 568, 1334, 822);
    if (rack)
    {
        faceplate.setBounds (mock (12, 568, 1334, 822));
        faceplate.setRackDisplayWidth (mockF (0, 0, 400, 0).getWidth());
        const auto controls = faceplate.getControlArea() + faceplate.getPosition();
        panels[delayBlock]->setBounds (controls);
        panels[delayBlock]->toFront (false);
        closePanel.setBounds (controls.getRight() - 108, controls.getY(), 108, 24);
        return;
    }
    faceplate.setRackDisplayWidth (0.0f);
    faceplate.setBounds (mock (12, 568, 652, 822));
    for (auto& panel : panels)
        if (panel != nullptr)
            panel->setBounds (summaryArea);
    closePanel.setBounds (summaryArea.withSizeKeepingCentre (0, 0).withX (summaryArea.getRight() - 130).withY (summaryArea.getY() + 10)
                              .withSize (118, 24));
}

void MainView::showPage (Page newPage)
{
    page = newPage;
    auto& tab = newPage == Page::processor ? processorTab : newPage == Page::presets ? presetsTab : settingsTab;
    tab.setToggleState (true, juce::dontSendNotification);

    for (auto* component : processorPage)
        component->setVisible (newPage == Page::processor);
    rigBrowser.setVisible (newPage == Page::presets);
    settingsPage.setVisible (newPage == Page::settings);
    selectBlock (selectedBlock);
}

void MainView::refreshRig()
{
    const auto rig = audioProcessor.getCurrentRig();
    const auto* entry = audioProcessor.findCurrentRigEntry();
    const auto modified = audioProcessor.isRigModified();
    const auto bypassed = audioProcessor.getParameters().getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;

    const auto headerText = rig.name + (modified ? " *" : "");
    if (rigNameButton.getButtonText() != headerText)
        rigNameButton.setButtonText (headerText);

    display.setContents ({ entry != nullptr ? entry->rig.number : 0, rig.name,
                           entry != nullptr ? Rig::bankName (entry->rig.bank) : juce::String(), modified, bypassed, tunerState });
}

void MainView::timerCallback()
{
    // The rig details change rarely: 5 times a second is plenty for them.
    if (ticks++ % 6 == 0)
        refreshRig();

    refreshPerformance();
}

void MainView::refreshPerformance()
{

    // Tempo: the tapped tempo in orange, or the DAW's.
    const auto tapped = audioProcessor.getTappedBpm() > 0.0;
    const auto tempo = juce::String (audioProcessor.getTempoBpm(), 1) + " BPM";
    if (tempoButton.getButtonText() != tempo)
        tempoButton.setButtonText (tempo);
    tempoButton.setColour (juce::TextButton::textColourOffId, tapped ? ParagonColours::accent : ParagonColours::text);
    tempoButton.setTooltip (tapped ? "Tapped tempo, used by the delay when it's synced. Click to follow your DAW's tempo again."
                                   : "Your DAW's tempo, used by the delay when it's synced. Use TAP to set your own.");

    // TAP's light flashes on each beat, like a hardware unit's.
    const auto beatMs = 60000.0 / audioProcessor.getTempoBpm();
    tapPedal.setToggleState (std::fmod (juce::Time::getMillisecondCounterHiRes(), beatMs) < beatMs * 0.25,
                             juce::dontSendNotification);

    const auto canUndo = audioProcessor.canUndo(), canRedo = audioProcessor.canRedo();
    undoButton.setEnabled (canUndo);
    redoButton.setEnabled (canRedo);
    undoPedal.setToggleState (canUndo, juce::dontSendNotification);   // lit while there's something to undo
    redoPedal.setToggleState (canRedo, juce::dontSendNotification);

    // The tuner runs only while it's switched on.
    auto& parameters = audioProcessor.getParameters();
    const auto tunerOn = parameters.getRawParameterValue (ParamIDs::tunerOn)->load() > 0.5f;
    const auto muted = parameters.getRawParameterValue (ParamIDs::tunerMute)->load() > 0.5f;
    tunerReader.setActive (tunerOn);
    if (tunerState.active != tunerOn || tunerState.muted != muted)
    {
        tunerState.active = tunerOn;
        tunerState.muted = muted;
        if (! tunerOn)
            tunerState.hasNote = false;
        refreshRig();
    }
}

void MainView::tap()
{
    audioProcessor.tapTempo();
    refreshPerformance();
}

void MainView::toggleTuner()
{
    if (auto* tuner = audioProcessor.getParameters().getParameter (ParamIDs::tunerOn))
    {
        tuner->beginChangeGesture();
        tuner->setValueNotifyingHost (tuner->getValue() > 0.5f ? 0.0f : 1.0f);
        tuner->endChangeGesture();
    }
    refreshPerformance();
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y (Cmd on a Mac). DAWs often keep the
    // keyboard for themselves, so these work when the plugin window has focus.
    const auto modifiers = key.getModifiers();
    if (! modifiers.isCommandDown())
        return false;

    const auto keyCode = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (keyCode == 'Z')
    {
        if (modifiers.isShiftDown())
            audioProcessor.redo();
        else
            audioProcessor.undo();
        return true;
    }
    if (keyCode == 'Y')
    {
        audioProcessor.redo();
        return true;
    }
    return false;
}

void MainView::saveRig()
{
    // Factory rigs and never-saved sounds need a name first.
    if (! audioProcessor.saveCurrentRig())
        rigBrowser.promptSaveAs();
    refreshRig();
}

void MainView::applyFinish()
{
    for (auto* label : { &rigLevelLabel, &presenceLabel })
        label->setColour (juce::Label::textColourId, Finish::ink());
    // The cluster's names: red over black keys; over neon keys red was
    // hard to read, so they're printed in the unit's ink (white on pearl
    // black, dark on pearl white). HOLD TO TUNER stays red.
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel, &tunerLabel, &muteLabel, &tapLabel })
        label->setColour (juce::Label::textColourId, Finish::neonKeys() ? Finish::ink() : Finish::printedRed());
    holdLabel.setColour (juce::Label::textColourId, Finish::printedRed());
    pedal.refinish();
    expressionPedal.refinish();
    repaint();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &Finish::changes())
    {
        applyFinish();
        return;
    }

    refreshRig();
}

void MainView::paint (juce::Graphics& g)
{
    // Background: near black, with a faint red warmth behind the unit.
    g.fillAll (ParagonColours::background);
    const auto centre = pedalBounds.getCentre().toFloat();
    g.setGradientFill (juce::ColourGradient (ParagonColours::glow.withAlpha (0.07f), centre,
                                             ParagonColours::glow.withAlpha (0.0f), centre.translated (760.0f, 0.0f), true));
    g.fillRect (getLocalBounds());

    // Header: emblem and name, as in the mockup's top bar.
    auto emblem = mockF (33, 13, 73, 50);
    juce::Path triangle;
    triangle.addTriangle (emblem.getTopLeft(), emblem.getTopRight(), { emblem.getCentreX(), emblem.getBottom() });
    g.setColour (ParagonColours::glow.withAlpha (0.3f));
    g.strokePath (triangle, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ParagonColours::glow);
    g.strokePath (triangle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    triangle.applyTransform (juce::AffineTransform::scale (0.5f, 0.5f, emblem.getCentreX(), emblem.getCentreY() - 3.0f));
    g.strokePath (triangle, juce::PathStrokeType (1.5f));

    const auto nameArea = mockF (84, 10, 330, 55);
    const auto nameFont = juce::FontOptions (21.0f);
    g.setFont (nameFont);
    g.setColour (ParagonColours::text);
    g.drawText ("PARAGON", nameArea, juce::Justification::centredLeft, false);
    const auto nameWidth = (float) juce::GlyphArrangement::getStringWidthInt (nameFont, "PARAGON ");
    Logo::drawMark (g, nameArea.withTrimmedLeft (nameWidth).withSizeKeepingCentre (nameArea.getWidth() - nameWidth, 17.0f),
                   juce::Justification::centredLeft, { ParagonColours::glow, ParagonColours::glow, {}, {}, 1.0f, true });

    // Framed groups: the tabs, the rig selector and the tempo.
    for (const auto& group : { tabsGroup, rigGroup, tempoGroup })
    {
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (group, 5.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (group.reduced (0.5f), 5.0f, 1.0f);
    }
    g.drawVerticalLine (juce::roundToInt (tempoDividerX * mockScale), tempoGroup.getY() + 6.0f, tempoGroup.getBottom() - 6.0f);
    for (auto x : { 505.0f, 650.0f })
        g.drawVerticalLine (juce::roundToInt (x * mockScale), tabsGroup.getY() + 8.0f, tabsGroup.getBottom() - 8.0f);

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (64.0f * mockScale), 0.0f, (float) getWidth());
}

void MainView::resized()
{
    // Header
    processorTab.setBounds (mock (338, 12, 505, 53));
    presetsTab.setBounds (mock (507, 12, 650, 53));
    settingsTab.setBounds (mock (652, 12, 793, 53));
    previousRig.setBounds (mock (846, 14, 886, 51));
    nextRig.setBounds (mock (1087, 14, 1127, 51));
    rigNameButton.setBounds (mock (888, 14, 1085, 51));
    tempoButton.setBounds (mock (1252, 15, 1336, 50));
    tapButton.setBounds (mock (1340, 15, 1411, 50));
    undoButton.setBounds (mock (1444, 14, 1482, 50));
    redoButton.setBounds (mock (1496, 14, 1534, 50));
    saveButton.setBounds (mock (1551, 14, 1589, 50));
    settingsButton.setBounds (mock (1611, 14, 1649, 50));

    // Side strips
    inputColumn.setBounds (mock (12, 72, 118, 556));
    outputColumn.setBounds (mock (1558, 72, 1662, 556));

    // The pedal unit. Its own drawing is positioned relative to its bounds.
    pedal.setBounds (pedalBounds);
    const auto origin = pedalBounds.getPosition().toFloat();
    const auto local = [origin] (juce::Rectangle<float> r) { return r - origin; };

    PedalBody::Geometry geometry;
    // The face plate sits inside the rim with about as much rim showing
    // below it as above (the rim's top ends level with the pedal bay's bottom, 547).
    geometry.face = local (mockF (164, 94, 1338, 534));
    geometry.pedalBay = local (mockF (1347, 86, 1535, 547));
    geometry.sections = { local (mockF (174, 240, 1325, 388)), local (mockF (174, 396, 1325, 524)) };
    geometry.badge = local (mockF (212, 117, 410, 207));

    // Printed lines: SAVE ... SAVE AS, and the bracket tying TUNER and
    // MUTE to "HOLD TO TUNER", like the mockup's BACK / CTRL bracket.
    geometry.printedInk.startNewSubPath (mockPoint (551, 218) - origin);
    geometry.printedInk.lineTo (mockPoint (637, 218) - origin);
    geometry.printedRed.startNewSubPath (mockPoint (clusterX[0], 199) - origin);
    geometry.printedRed.lineTo (mockPoint (clusterX[0], 210) - origin);
    geometry.printedRed.lineTo (mockPoint (1000, 210) - origin);
    geometry.printedRed.startNewSubPath (mockPoint (clusterX[1], 199) - origin);
    geometry.printedRed.lineTo (mockPoint (clusterX[1], 210) - origin);
    geometry.printedRed.addEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (mockPoint (clusterX[2], 203) - origin));
    pedal.setGeometry (std::move (geometry));

    display.setBounds (mock (425, 108, 800, 202));
    rigUp.setBounds (mock (833, 130, 855, 150));
    rigDown.setBounds (mock (833, 168, 855, 188));
    pedalSave.setBounds (mock (485, 209, 551, 227));
    pedalSaveAs.setBounds (mock (637, 209, 721, 227));

    // The cluster: a red label printed above each small button.
    const auto placeInCluster = [] (juce::Label& label, juce::Button& button, int column, int row)
    {
        const auto x = clusterX[column];
        label.setBounds (mock (x - 30, clusterLabelY[row], x + 30, clusterLabelY[row] + 14));
        button.setBounds (mock (x - 18, clusterButtonY[row], x + 18, clusterButtonY[row] + 19));
    };
    placeInCluster (undoLabel, undoPedal, 0, 0);
    placeInCluster (redoLabel, redoPedal, 1, 0);
    placeInCluster (bypassLabel, bypassButton, 2, 0);
    placeInCluster (tunerLabel, tunerPedal, 0, 1);
    placeInCluster (muteLabel, mutePedal, 1, 1);
    placeInCluster (tapLabel, tapPedal, 2, 1);
    holdLabel.setBounds (mock (996, 207, 1070, 220));

    rigLevelKnob.setBounds (mock (1080, 98, 1194, 212));
    rigLevelLabel.setBounds (mock (1077, 196, 1197, 214));
    presenceKnob.setBounds (mock (1203, 98, 1317, 212));
    presenceLabel.setBounds (mock (1200, 196, 1320, 214));


    for (size_t i = 0; i < footswitches.size(); ++i)
        footswitches[i]->setBounds (mock (footswitchX[i] - 48, footswitchY[i], footswitchX[i] + 164, footswitchY[i] + 120));

    // Exactly where the artwork was cut from the mockup, plus headroom above
    // for the toe to rise into.
    expressionPedal.setBounds (mock (1342, 46, 1553, 562));

    // Dock
    // The FACEPLATE where the RIG BROWSER and SELECTED RIG panels were: both
    // only repeated the PRESETS page and the unit's own display and SAVE buttons.
    const auto summaryArea = mock (662, 568, 1334, 822);
    eqPanel.setBounds (summaryArea);
    harmonyPanel.setBounds (summaryArea);
    doublerPanel.setBounds (summaryArea);
    marsPanel.setBounds (summaryArea);
    // PITCH's view tabs sit in its header, after the icon and name.
    harmonyView.setBounds (summaryArea.getX() + 112, summaryArea.getY() + 10, 84, 24);
    octaveView.setBounds (harmonyView.getRight() + 4, summaryArea.getY() + 10, 128, 24);
    layoutDock();
    expressionPanel.setBounds (mock (1344, 568, 1662, 822));

    signalChain.setBounds (mock (12, 832, 1662, 930));

    // Other pages cover the pedal and the dock.
    const auto pageArea = mock (126, 72, 1548, 822);   // between the INPUT and OUTPUT strips
    rigBrowser.setBounds (pageArea);
    settingsPage.setBounds (pageArea);
}
