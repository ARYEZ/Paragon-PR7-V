#include "HarmonyModsView.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    const juce::Colour screenColour { 0xff0d0b0e };
    const char* tabNames[] = { "M1", "M2", "M3", "M4", "AM", "PT" };

    void styleLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setMinimumHorizontalScale (0.6f);
        label.setBorderSize ({});
        label.setInterceptsMouseClicks (false, false);
    }
}

juce::Colour HarmonyModsView::colourOf (int source)
{
    switch (source)
    {
        case Mod::m1: return juce::Colour (0xff4fc3f7);   // sky blue
        case Mod::m2: return juce::Colour (0xff6fdc8c);   // green
        case Mod::m3: return juce::Colour (0xffc58cff);   // violet
        case Mod::m4: return juce::Colour (0xffff6fa8);   // pink
        case Mod::am: return juce::Colour (0xff4fe3dd);   // teal
        case Mod::pt: return juce::Colour (0xffffd54f);   // gold
        default:      return juce::Colours::white;        // several at once
    }
}

juce::String HarmonyModsView::targetName (const juce::String& id)
{
    if (id.startsWith ("hv"))
    {
        const auto voice = id.substring (2).getIntValue();
        return "VOICE " + juce::String (voice) + " " + id.substring (2 + juce::String (voice).length()).toUpperCase();
    }
    const std::pair<const char*, const char*> names[] = {
        { ParamIDs::harmonyNoteLevel, "NOTES LEVEL" }, { ParamIDs::harmonyNoteWidth, "NOTES WIDTH" },
        { ParamIDs::harmonyNoteFormant, "NOTES FORMANT" }, { ParamIDs::harmonyNoteDelay, "NOTES DELAY" },
        { ParamIDs::harmonyNoteFeedback, "NOTES FEEDBACK" }, { ParamIDs::harmonyNoteCutoff, "NOTES CUTOFF" },
        { ParamIDs::harmonySource, "SOURCE" }, { ParamIDs::harmonyLevel, "HARMONY" }, { ParamIDs::harmonySpread, "SPREAD" },
        { ParamIDs::harmonyCorrectFormant, "CORR. FORMANT" }, { ParamIDs::harmonyGlobalPitch, "GLOBAL PITCH" },
        { ParamIDs::harmonyGlobalFormant, "GLOBAL FORMANT" } };
    for (const auto& [paramId, name] : names)
        if (id == paramId)
            return name;
    return id.toUpperCase();
}

HarmonyModsView::HarmonyModsView (juce::AudioProcessorValueTreeState& state)
    : parameters (state)
{
    for (int i = 0; i < numTabs; ++i)
    {
        auto& button = tabs[(size_t) i];
        button.setButtonText (tabNames[i]);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (4290);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOnId, colourOf (sourceOf (i)));
        button.onClick = [this, i] { showTab (i); };
        addAndMakeVisible (button);
    }
    done.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    done.onClick = [this] { if (onDone != nullptr) onDone(); };
    addAndMakeVisible (done);

    setUp (type, "TYPE", Mod::typeNames);
    setUp (shape, "SHAPE", Mod::shapeNames);
    setUp (sync, "SYNC", Mod::syncNames);
    setUp (trigger, "TRIGGER", Mod::triggerNames);
    setUp (play, "PLAY", Mod::playNames);
    setUp (steps, "STEPS");
    setUp (rate, "RATE");
    setUp (phase, "PHASE");
    setUp (warp, "WARP");
    setUp (smooth, "SMOOTH");
    setUp (level, "LEVEL");
    setUp (amAttack, "ATTACK");
    setUp (amRelease, "RELEASE");
    setUp (amLevel, "LEVEL");
    setUp (ptSmooth, "SMOOTH");
    setUp (ptOffset, "OFFSET");
    setUp (ptLevel, "LEVEL");
    type.box.setTooltip ("LFO: a repeating shape. SEQ: a line of up to 16 steps (drag the bars), each in semitones at 100% on a pitch");
    rate.slider.setTooltip ("LFO: how many times a second it goes round. SEQ: how many steps a second");
    sync.box.setTooltip ("Lock RATE to the song's tempo (a whole cycle, or for SEQ each step)");
    phase.slider.setTooltip ("Where in its cycle it starts");
    warp.slider.setTooltip ("Below 1: starts slowly and speeds up through the cycle; above 1: the other way round. The cycle keeps its length");
    smooth.slider.setTooltip ("Rounds off its corners and jumps");
    level.slider.setTooltip ("How much of it comes out (0: none)");
    trigger.box.setTooltip ("Free: runs on its own. Sync: locked to the song. Retrigger: restarts on each new note. "
                            "Legato: restarts on a note only after a gap (notes come from GENERATE or MIDI)");
    play.box.setTooltip ("Loop: round and round. One Shot: once, then stops. Seesaw: there and back. Hold: stays where PHASE puts it");
    amAttack.slider.setTooltip ("How fast it rises as you sing louder");
    amRelease.slider.setTooltip ("How fast it falls back as you get quieter");
    ptOffset.slider.setTooltip ("Moves the note it reads as its middle (0: middle C)");

    // AM and PT: their own parameters, always.
    for (auto [knob, id] : { std::pair { &amAttack, ParamIDs::harmonyAmAttack }, std::pair { &amRelease, ParamIDs::harmonyAmRelease },
                             std::pair { &amLevel, ParamIDs::harmonyAmLevel }, std::pair { &ptSmooth, ParamIDs::harmonyPtSmooth },
                             std::pair { &ptOffset, ParamIDs::harmonyPtOffset }, std::pair { &ptLevel, ParamIDs::harmonyPtLevel } })
    {
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob->slider);
        auto* parameter = parameters.getParameter (id);
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    showTab (0);
    startTimerHz (30);
}

HarmonyModsView::~HarmonyModsView() = default;

void HarmonyModsView::setActions (Actions newActions)
{
    actions = std::move (newActions);
    shownAssignments = assignmentsText();
    repaint();
}

void HarmonyModsView::setUp (Knob& knob, const juce::String& name)
{
    styleLabel (knob.label, name);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
    addAndMakeVisible (knob.label);
    addAndMakeVisible (knob.slider);
}

void HarmonyModsView::setUp (List& list, const juce::String& name, const juce::StringArray& items)
{
    styleLabel (list.label, name);
    list.box.addItemList (items, 1);
    list.box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    list.box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    list.box.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    list.box.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    addAndMakeVisible (list.label);
    addAndMakeVisible (list.box);
}

float HarmonyModsView::value (const juce::String& id) const { return parameters.getRawParameterValue (id)->load(); }

bool HarmonyModsView::isSequencer() const
{
    return tab < Mod::numMods && juce::roundToInt (value (ParamIDs::harmonyMod (tab, "Type"))) == 1;
}

void HarmonyModsView::showTab (int newTab)
{
    tab = juce::jlimit (0, numTabs - 1, newTab);
    tabs[(size_t) tab].setToggleState (true, juce::dontSendNotification);
    bind();
    shownAssignments = assignmentsText();
    resized();
    repaint();
}

void HarmonyModsView::bind()
{
    if (tab >= Mod::numMods)
        return;
    const auto id = [this] (const char* part) { return ParamIDs::harmonyMod (tab, part); };
    for (auto [list, part] : { std::pair { &type, "Type" }, std::pair { &shape, "Shape" }, std::pair { &sync, "Sync" },
                               std::pair { &trigger, "Trigger" }, std::pair { &play, "Play" } })
    {
        list->attachment.reset();
        list->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, id (part), list->box);
    }
    for (auto [knob, part] : { std::pair { &steps, "Steps" }, std::pair { &rate, "Rate" }, std::pair { &phase, "Phase" },
                               std::pair { &warp, "Warp" }, std::pair { &smooth, "Smooth" }, std::pair { &level, "Level" } })
    {
        knob->attachment.reset();
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id (part), knob->slider);
        auto* parameter = parameters.getParameter (id (part));
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
}

juce::String HarmonyModsView::assignmentsText() const
{
    if (actions.slot == nullptr)
        return {};
    const auto source = sourceOf (tab);
    const auto ids = ParamIDs::harmonyModTargets();
    juce::StringArray on;
    for (int target = 0; target < ids.size(); ++target)
        for (int slot = 0; slot < Mod::slotsPerTarget; ++slot)
            if (const auto found = actions.slot (target, slot); found.source == source)
            {
                // A pitch's depth in semitones (at full swing), as its knob's menu shows it.
                const auto sign = juce::String (found.depth >= 0.0f ? "+" : "");
                const auto st = found.depth * Mod::pitchSpan;
                on.add (targetName (ids[target]) + " " + (ParamIDs::isHarmonyPitchTarget (ids[target])
                                                              ? sign + juce::String (st, std::abs (st - std::round (st)) < 0.01f ? 0 : 1) + " st"
                                                              : sign + juce::String (juce::roundToInt (found.depth * 100.0f)) + "%"));
            }
    return on.isEmpty() ? juce::String ("NOT ON ANYTHING YET: RIGHT-CLICK A HARMONY KNOB TO PUT " + juce::String (tabNames[tab]) + " ON IT")
                        : "ON  " + on.joinIntoString ("   ");
}

void HarmonyModsView::timerCallback()
{
    // The SEQ and LFO controls swap when TYPE changes; the assignments can
    // change from the panel; and the shape's dot moves.
    const auto wantSteps = isSequencer();
    if (steps.slider.isVisible() != wantSteps && tab < Mod::numMods)
        resized();
    if (const auto text = assignmentsText(); text != shownAssignments)
    {
        shownAssignments = text;
        repaint (header);
    }
    repaint (display.expanded (4.0f).getSmallestIntegerContainer());
}

void HarmonyModsView::resized()
{
    auto area = getLocalBounds().reduced (8, 4);
    auto top = area.removeFromTop (22);
    for (auto& button : tabs)
    {
        button.setBounds (top.removeFromLeft (44));
        top.removeFromLeft (3);
    }
    done.setBounds (top.removeFromRight (60));
    top.removeFromLeft (12);
    top.removeFromRight (12);
    header = top;
    area.removeFromTop (6);

    display = area.removeFromLeft (juce::jmin (300, area.getWidth() * 3 / 10)).toFloat().reduced (0.0f, 2.0f);
    area.removeFromLeft (14);

    // The controls of the tab showing, in one row.
    const auto isM = tab < Mod::numMods;
    const auto seq = isSequencer();
    for (auto* list : { &type, &shape, &sync, &trigger, &play })
    {
        const auto shown = isM && (list != &shape || ! seq);
        list->label.setVisible (shown);
        list->box.setVisible (shown);
    }
    for (auto* knob : { &steps, &rate, &phase, &warp, &smooth, &level })
    {
        const auto shown = isM && (knob != &steps || seq);
        knob->label.setVisible (shown);
        knob->slider.setVisible (shown);
    }
    for (auto* knob : { &amAttack, &amRelease, &amLevel })
    {
        knob->label.setVisible (tab == Mod::numMods);
        knob->slider.setVisible (tab == Mod::numMods);
    }
    for (auto* knob : { &ptSmooth, &ptOffset, &ptLevel })
    {
        knob->label.setVisible (tab == Mod::numMods + 1);
        knob->slider.setVisible (tab == Mod::numMods + 1);
    }

    struct Cell { juce::Component* label; juce::Component* widget; bool list; float weight; };
    std::vector<Cell> row;
    const auto knob = [&row] (Knob& k) { row.push_back ({ &k.label, &k.slider, false, 1.0f }); };
    const auto list = [&row] (List& l, float weight) { row.push_back ({ &l.label, &l.box, true, weight }); };
    if (isM)
    {
        list (type, 1.1f);
        if (seq)
            knob (steps);
        else
            list (shape, 1.7f);
        knob (rate);
        list (sync, 1.4f);
        knob (phase);
        knob (warp);
        knob (smooth);
        knob (level);
        list (trigger, 1.4f);
        list (play, 1.3f);
    }
    else if (tab == Mod::numMods)
    {
        knob (amAttack);
        knob (amRelease);
        knob (amLevel);
    }
    else
    {
        knob (ptSmooth);
        knob (ptOffset);
        knob (ptLevel);
    }
    auto total = 0.0f;
    for (const auto& cell : row)
        total += cell.weight;
    const auto unit = juce::jmin (84.0f, (float) area.getWidth() / juce::jmax (1.0f, total));
    auto x = (float) area.getX();
    for (const auto& cell : row)
    {
        auto bounds = juce::Rectangle<int> (juce::roundToInt (x), area.getY(), juce::roundToInt (unit * cell.weight), area.getHeight()).reduced (3, 0);
        x += unit * cell.weight;
        cell.label->setBounds (bounds.removeFromTop (14));
        bounds.removeFromTop (2);
        if (cell.list)
            cell.widget->setBounds (bounds.withSizeKeepingCentre (bounds.getWidth(), 24).withY (bounds.getY() + bounds.getHeight() / 2 - 20));
        else
            cell.widget->setBounds (bounds);
    }
}

void HarmonyModsView::setStepAt (juce::Point<float> point)
{
    if (! isSequencer() || ! display.contains (point))
        return;
    const auto count = juce::jlimit (2, Mod::maxSteps, juce::roundToInt (value (ParamIDs::harmonyMod (tab, "Steps"))));
    const auto step = juce::jlimit (0, count - 1, (int) ((point.x - display.getX()) / display.getWidth() * (float) count));
    const auto semitones = juce::jlimit (-Mod::pitchSpan, Mod::pitchSpan,
                                         std::round ((display.getCentreY() - point.y) / (display.getHeight() * 0.5f) * Mod::pitchSpan));
    auto* parameter = parameters.getParameter (ParamIDs::harmonyModStep (tab, step));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (semitones));
    parameter->endChangeGesture();
    repaint();
}

void HarmonyModsView::mouseDown (const juce::MouseEvent& event) { setStepAt (event.position); }
void HarmonyModsView::mouseDrag (const juce::MouseEvent& event) { setStepAt (event.position); }

void HarmonyModsView::paint (juce::Graphics& g)
{
    g.setColour (screenColour);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    const auto source = sourceOf (tab);
    const auto colour = colourOf (source);

    // What it's on.
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (shownAssignments.startsWith ("ON") ? colour : ParagonColours::textDim);
    g.drawFittedText (shownAssignments, header, juce::Justification::centredLeft, 1, 0.7f);

    // The display: the shape (or steps) with its dot, or AM's level, or PT's place.
    g.setColour (juce::Colour (0xff151217));
    g.fillRoundedRectangle (display, 4.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (display, 4.0f, 1.0f);
    const auto inner = display.reduced (6.0f, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (juce::roundToInt (inner.getCentreY()), inner.getX(), inner.getRight());
    const auto live = actions.value != nullptr ? actions.value (source) : 0.0f;
    const auto yFor = [&inner] (float v) { return inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * inner.getHeight() * 0.5f; };

    if (tab < Mod::numMods)
    {
        Mod::Mod mod;
        mod.type = isSequencer() ? 1 : 0;
        mod.shape = juce::roundToInt (value (ParamIDs::harmonyMod (tab, "Shape")));
        mod.warp = value (ParamIDs::harmonyMod (tab, "Warp"));
        mod.steps = juce::roundToInt (value (ParamIDs::harmonyMod (tab, "Steps")));
        for (int step = 0; step < Mod::maxSteps; ++step)
            mod.stepValues[(size_t) step] = value (ParamIDs::harmonyModStep (tab, step));
        const auto levelNow = value (ParamIDs::harmonyMod (tab, "Level"));
        const auto position = actions.position != nullptr ? actions.position (tab) : 0.0f;
        if (mod.type == 1)
        {
            // The steps as bars from the middle line; the one playing lit.
            const auto count = juce::jlimit (2, Mod::maxSteps, mod.steps);
            const auto width = inner.getWidth() / (float) count;
            const auto playing = juce::jlimit (0, count - 1, (int) (position * (float) count));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            for (int step = 0; step < count; ++step)
            {
                const auto v = mod.stepValues[(size_t) step] / Mod::pitchSpan;
                const auto bar = juce::Rectangle<float>::leftTopRightBottom (inner.getX() + width * (float) step + 1.5f, juce::jmin (yFor (v), inner.getCentreY()),
                                                                             inner.getX() + width * (float) (step + 1) - 1.5f, juce::jmax (yFor (v), inner.getCentreY()))
                                     .withHeight (juce::jmax (2.0f, std::abs (yFor (v) - inner.getCentreY())));
                g.setColour (colour.withAlpha (step == playing ? 1.0f : 0.45f));
                g.fillRect (bar);
                g.setColour (ParagonColours::textDim);
                g.drawText (juce::String (juce::roundToInt (mod.stepValues[(size_t) step])),
                            juce::Rectangle<float> (inner.getX() + width * (float) step, inner.getBottom() - 10.0f, width, 10.0f), juce::Justification::centred, false);
            }
        }
        else
        {
            // One cycle of the shape (at LEVEL), and a dot riding it.
            juce::Path curve;
            for (int i = 0; i <= 120; ++i)
            {
                const auto p = (float) i / 120.0f;
                const auto point = juce::Point<float> (inner.getX() + inner.getWidth() * p, yFor (Mod::modValue (mod, p, 0, tab + 1) * levelNow));
                if (i == 0)
                    curve.startNewSubPath (point);
                else
                    curve.lineTo (point);
            }
            g.setColour (colour.withAlpha (0.3f));
            g.strokePath (curve, juce::PathStrokeType (4.0f));
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (1.5f));
            const auto dot = juce::Point<float> (inner.getX() + inner.getWidth() * position, yFor (live));
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
        }
    }
    else if (tab == Mod::numMods)
    {
        // AM: how loud the voice is now, as a bar (LEVEL can take it to 2).
        g.setColour (colour.withAlpha (0.85f));
        const auto bar = inner.withTrimmedTop (inner.getHeight() * 0.35f).withTrimmedBottom (inner.getHeight() * 0.35f);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, live / 2.0f)), 3.0f);
        g.setColour (ParagonColours::textDim);
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("VOICE LEVEL  " + juce::String (live, 2), inner.withHeight (14.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        // PT: where the pitch sung puts it now, -1 to 1 (0: middle C plus OFFSET).
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ inner.getCentreX() + live * inner.getWidth() * 0.5f, inner.getCentreY() }));
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("LOWER", inner, juce::Justification::bottomLeft, false);
        g.drawText ("HIGHER", inner, juce::Justification::bottomRight, false);
        g.drawText ("PITCH  " + juce::String (live, 2), inner.withHeight (14.0f), juce::Justification::centredLeft, false);
    }
}
