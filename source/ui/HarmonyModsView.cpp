#include "HarmonyModsView.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    const juce::Colour screenColour { 0xff0d0b0e };
    const char* tabNames[] = { "M1", "M2", "M3", "M4", "A1", "A2", "AM", "PT", "SP", "MIDI" };

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
        case Mod::a1: return juce::Colour (0xffff8a65);   // coral
        case Mod::a2: return juce::Colour (0xffd4e157);   // lime
        case Mod::sp: return juce::Colour (0xff9fa8ff);   // periwinkle
        case Mod::velocity: case Mod::keyboard: case Mod::pitchWheel: case Mod::modWheel: case Mod::aftertouch:
                      return juce::Colour (0xffb0c4d4);   // MIDI's, silver blue
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
    erase.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    erase.setTooltip ("Flatten the drawn shape, to draw it again");
    erase.onClick = [this]
    {
        if (actions.setShape != nullptr && tab < Mod::numMods)
            actions.setShape (tab, {});
        repaint();
    };
    addChildComponent (erase);

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
    setUp (envAttack, "ATTACK");
    setUp (envDecay, "DECAY");
    setUp (envSustain, "SUSTAIN");
    setUp (envRelease, "RELEASE");
    setUp (envAttackCurve, "A CURVE");
    setUp (envDecayCurve, "D CURVE");
    setUp (envReleaseCurve, "R CURVE");
    setUp (spMode, "MODE", Mod::spreadModeNames);
    setUp (spTrigger, "TRIGGER", Mod::spreadTriggerNames);
    setUp (spSpread, "SPREAD");
    setUp (spLevel, "LEVEL");
    type.box.setTooltip ("LFO: a repeating shape. SEQ: a line of up to 16 steps (drag the bars), each in semitones at 100% on a pitch");
    rate.slider.setTooltip ("LFO: how many times a second it goes round. SEQ: how many steps a second");
    sync.box.setTooltip ("Lock RATE to the song's tempo (a whole cycle, or for SEQ each step)");
    phase.slider.setTooltip ("Where in its cycle it starts");
    warp.slider.setTooltip ("Below 1: starts slowly and speeds up through the cycle; above 1: the other way round. The cycle keeps its length");
    smooth.slider.setTooltip ("Rounds off its corners and jumps");
    level.slider.setTooltip ("How much of it comes out (0: none)");
    play.box.setTooltip ("Loop: round and round. One Shot: once, then stops. Seesaw: there and back. Hold: stays where PHASE puts it");
    amAttack.slider.setTooltip ("How fast it rises as you sing louder");
    amRelease.slider.setTooltip ("How fast it falls back as you get quieter");
    ptOffset.slider.setTooltip ("Moves the note it reads as its middle (0: middle C)");
    trigger.box.setTooltip ("Free: runs on its own. Sync: locked to the song. Retrigger: restarts on each new note. "
                            "Legato: restarts on a note only after a gap. Poly: each note its own cycle, from its start "
                            "(notes come from GENERATE or MIDI)");
    envAttack.slider.setTooltip ("How long it takes to rise to the top when a note starts");
    envDecay.slider.setTooltip ("How long it then takes to fall to SUSTAIN");
    envSustain.slider.setTooltip ("Where it stays while the note is held");
    envRelease.slider.setTooltip ("How long it takes to die away when the note ends");
    for (auto* curve : { &envAttackCurve, &envDecayCurve, &envReleaseCurve })
        curve->slider.setTooltip ("1: straight. Below 1: starts slowly, then quickly. Above 1: quickly, then slowly");
    spMode.box.setTooltip ("Outward: the lowest note at -SPREAD, the highest at +SPREAD. Inward: the other way round. "
                           "Random: each note its own random value");
    spTrigger.box.setTooltip ("Retrigger: every note's value worked out again when a note starts. Poly: each note keeps the value it began with");
    spSpread.slider.setTooltip ("How far apart the notes' values go");

    // AM and PT: their own parameters, always.
    for (auto [knob, id] : { std::pair { &amAttack, ParamIDs::harmonyAmAttack }, std::pair { &amRelease, ParamIDs::harmonyAmRelease },
                             std::pair { &amLevel, ParamIDs::harmonyAmLevel }, std::pair { &ptSmooth, ParamIDs::harmonyPtSmooth },
                             std::pair { &ptOffset, ParamIDs::harmonyPtOffset }, std::pair { &ptLevel, ParamIDs::harmonyPtLevel },
                             std::pair { &spSpread, ParamIDs::harmonySpSpread }, std::pair { &spLevel, ParamIDs::harmonySpLevel } })
        attach (*knob, id);
    spMode.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonySpMode, spMode.box);
    spTrigger.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::harmonySpTrigger, spTrigger.box);
    // The modulators' own settings, for knowing when the display changes.
    for (auto* parameter : parameters.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (const auto id = ranged->getParameterID(); id.startsWith ("hm") || id.startsWith ("harmonyA") || id.startsWith ("harmonyPt") || id.startsWith ("harmonySp"))
                modParameters.push_back (ranged);
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

void HarmonyModsView::attach (Knob& knob, const juce::String& id)
{
    knob.attachment.reset();
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob.slider);
    auto* parameter = parameters.getParameter (id);
    knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
}

float HarmonyModsView::value (const juce::String& id) const { return parameters.getRawParameterValue (id)->load(); }

bool HarmonyModsView::isSequencer() const
{
    return tab < Mod::numMods && juce::roundToInt (value (ParamIDs::harmonyMod (tab, "Type"))) == 1;
}

bool HarmonyModsView::isDrawing() const
{
    return tab < Mod::numMods && ! isSequencer() && juce::roundToInt (value (ParamIDs::harmonyMod (tab, "Shape"))) == Mod::drawnShape;
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
    if (tab == a1Tab || tab == a2Tab)
    {
        const auto envelope = tab - a1Tab;
        const Knob* knobs[] = { &envAttack, &envDecay, &envSustain, &envRelease, &envAttackCurve, &envDecayCurve, &envReleaseCurve };
        for (int part = 0; part < ParamIDs::harmonyEnvelopeParts.size(); ++part)
            attach (*const_cast<Knob*> (knobs[part]), ParamIDs::harmonyEnvelope (envelope, ParamIDs::harmonyEnvelopeParts[part]));
        return;
    }
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
        attach (*knob, id (part));
}

juce::String HarmonyModsView::assignmentsText() const
{
    if (actions.slot == nullptr)
        return {};
    const auto ids = ParamIDs::harmonyModTargets();
    juce::StringArray on;
    for (int target = 0; target < ids.size(); ++target)
        for (int slot = 0; slot < Mod::slotsPerTarget; ++slot)
            if (const auto found = actions.slot (target, slot); found.source != 0 && tabShows (tab, found.source))
            {
                // A pitch's depth in semitones (at full swing), as its knob's menu shows it.
                const auto sign = juce::String (found.depth >= 0.0f ? "+" : "");
                const auto st = found.depth * Mod::pitchSpan;
                const auto which = tab == midiTab ? Mod::sourceNames[found.source].toUpperCase() + " > " : juce::String();
                on.add (which + targetName (ids[target]) + " " + (ParamIDs::isHarmonyPitchTarget (ids[target])
                                                              ? sign + (std::abs (st - std::round (st)) < 0.01f ? juce::String (juce::roundToInt (st)) : juce::String (st, 1)) + " st"
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
    if (tab < Mod::numMods && (steps.slider.isVisible() != wantSteps || erase.isVisible() != isDrawing()))
        resized();
    if (const auto text = assignmentsText(); text != shownAssignments)
    {
        shownAssignments = text;
        repaint (header);
    }
    // The display only when something on it moved: redrawing it 30 times a
    // second for nothing took a good share of a core.
    if (const auto signature = displaySignature(); signature != shownDisplay)
    {
        shownDisplay = signature;
        repaint (display.expanded (4.0f).getSmallestIntegerContainer());
    }
}

std::vector<int> HarmonyModsView::displaySignature() const
{
    // What the display draws: the tab, its settings (each control in steps
    // finer than a pixel), the sources' values now and the note voices'.
    std::vector<int> state;
    if (! isShowing())
        return state;
    const auto fine = [&state] (float value) { state.push_back (juce::roundToInt (value * 2000.0f)); };
    state.push_back (tab);
    for (auto* parameter : modParameters)
        fine (parameter->getValue());
    // Only the sources this tab draws (a free-running M1 doesn't move the MIDI tab).
    const auto source = sourceOf (tab);
    if (actions.value != nullptr)
    {
        fine (actions.value (source));
        if (tab == midiTab)
            for (const auto midiSource : { Mod::velocity, Mod::keyboard, Mod::pitchWheel, Mod::modWheel, Mod::aftertouch })
                fine (actions.value (midiSource));
    }
    if (tab < Mod::numMods && actions.position != nullptr)
        fine (actions.position (tab));
    if (tab < Mod::numMods && actions.shape != nullptr)
        for (const auto point : actions.shape (tab))
            fine (point);
    for (int voice = 0; voice < Mod::numNoteVoices; ++voice)
    {
        state.push_back (actions.voiceNote != nullptr ? actions.voiceNote (voice) : -1);
        if (actions.voiceValue != nullptr)
            fine (actions.voiceValue (source, voice));
    }
    return state;
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
    top.removeFromRight (6);
    erase.setBounds (top.removeFromRight (60));
    erase.setVisible (isDrawing());
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
    const auto show = [] (std::initializer_list<juce::Component*> parts, bool shown)
    {
        for (auto* part : parts)
            part->setVisible (shown);
    };
    for (auto* knob : { &amAttack, &amRelease, &amLevel })
        show ({ &knob->label, &knob->slider }, tab == amTab);
    for (auto* knob : { &ptSmooth, &ptOffset, &ptLevel })
        show ({ &knob->label, &knob->slider }, tab == ptTab);
    for (auto* knob : { &envAttack, &envDecay, &envSustain, &envRelease, &envAttackCurve, &envDecayCurve, &envReleaseCurve })
        show ({ &knob->label, &knob->slider }, tab == a1Tab || tab == a2Tab);
    show ({ &spMode.label, &spMode.box, &spTrigger.label, &spTrigger.box, &spSpread.label, &spSpread.slider, &spLevel.label, &spLevel.slider },
          tab == spTab);

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
    else if (tab == amTab)
    {
        knob (amAttack);
        knob (amRelease);
        knob (amLevel);
    }
    else if (tab == ptTab)
    {
        knob (ptSmooth);
        knob (ptOffset);
        knob (ptLevel);
    }
    else if (tab == a1Tab || tab == a2Tab)
    {
        for (auto* k : { &envAttack, &envDecay, &envSustain, &envRelease, &envAttackCurve, &envDecayCurve, &envReleaseCurve })
            knob (*k);
    }
    else if (tab == spTab)
    {
        list (spMode, 1.4f);
        list (spTrigger, 1.4f);
        knob (spSpread);
        knob (spLevel);
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

void HarmonyModsView::drawAt (juce::Point<float> point, std::optional<juce::Point<float>> from)
{
    if (! isDrawing() || actions.shape == nullptr || actions.setShape == nullptr)
        return;
    const auto inner = display.reduced (6.0f, 6.0f);
    const auto indexOf = [&inner] (float x) { return juce::jlimit (0, Mod::drawnPoints - 1, (int) ((x - inner.getX()) / inner.getWidth() * (float) Mod::drawnPoints)); };
    const auto valueOf = [&inner] (float y) { return juce::jlimit (-1.0f, 1.0f, (inner.getCentreY() - y) / (inner.getHeight() * 0.5f)); };
    auto points = actions.shape (tab);
    const auto start = from.value_or (point);
    const auto a = indexOf (start.x), b = indexOf (point.x);
    // Every point between where the drag was and where it is, on a straight line.
    for (int i = juce::jmin (a, b); i <= juce::jmax (a, b); ++i)
    {
        const auto t = a == b ? 1.0f : (float) (i - a) / (float) (b - a);
        points[(size_t) i] = valueOf (start.y + (point.y - start.y) * t);
    }
    actions.setShape (tab, points);
    repaint();
}

void HarmonyModsView::paintVoices (juce::Graphics& g, juce::Rectangle<float> area, int source, juce::Colour colour) const
{
    // A thin bar per note voice (its level now, 0 to 1); a voice not sounding: dim.
    const auto width = area.getWidth() / (float) Mod::numNoteVoices;
    for (int v = 0; v < Mod::numNoteVoices; ++v)
    {
        const auto bar = juce::Rectangle<float> (area.getX() + width * (float) v, area.getY(), width, area.getHeight()).reduced (1.5f, 0.0f);
        const auto sounding = actions.voiceNote != nullptr && actions.voiceNote (v) >= 0;
        const auto now = actions.voiceValue != nullptr ? juce::jlimit (0.0f, 1.0f, actions.voiceValue (source, v)) : 0.0f;
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (bar);
        g.setColour (colour.withAlpha (sounding ? 0.9f : 0.35f));
        g.fillRect (bar.withTrimmedTop (bar.getHeight() * (1.0f - now)));
    }
}

void HarmonyModsView::mouseDown (const juce::MouseEvent& event)
{
    if (isDrawing() && display.contains (event.position))
    {
        drawAt (event.position);
        lastDraw = event.position;
        return;
    }
    setStepAt (event.position);
}

void HarmonyModsView::mouseDrag (const juce::MouseEvent& event)
{
    if (lastDraw.has_value())
    {
        drawAt (event.position, lastDraw);
        lastDraw = event.position;
        return;
    }
    setStepAt (event.position);
}

void HarmonyModsView::mouseUp (const juce::MouseEvent&) { lastDraw.reset(); }

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
    if (tab != midiTab && tab != spTab && tab != a1Tab && tab != a2Tab)   // (those draw their own)
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
        if (actions.shape != nullptr)
            mod.drawn = actions.shape (tab);
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
            if (mod.shape == Mod::drawnShape)
            {
                // Its points, to draw on (with a hint while it's flat).
                g.setColour (colour.withAlpha (0.7f));
                for (int i = 0; i < Mod::drawnPoints; ++i)
                    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ inner.getX() + inner.getWidth() * (float) i / (float) Mod::drawnPoints,
                                                                                      yFor (mod.drawn[(size_t) i] * levelNow) }));
                if (std::all_of (mod.drawn.begin(), mod.drawn.end(), [] (float point) { return juce::exactlyEqual (point, 0.0f); }))
                {
                    g.setColour (ParagonColours::textDim);
                    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                    g.drawText ("DRAW THE SHAPE HERE", inner.withTrimmedTop (inner.getHeight() * 0.6f), juce::Justification::centred, false);
                }
            }
        }
    }
    else if (tab == a1Tab || tab == a2Tab)
    {
        // The envelope: ATTACK, DECAY, a stretch held at SUSTAIN, RELEASE.
        // Each stage's width grows with the square root of its time, so a
        // 10 ms attack and a 2 s release both show.
        const auto envelopeIndex = tab - a1Tab;
        const auto id = [envelopeIndex] (const char* part) { return ParamIDs::harmonyEnvelope (envelopeIndex, part); };
        Mod::Envelope envelope { value (id ("Attack")), value (id ("Decay")), value (id ("Sustain")), value (id ("Release")),
                                 value (id ("AttackCurve")), value (id ("DecayCurve")), value (id ("ReleaseCurve")) };
        auto graph = inner;
        const auto voices = graph.removeFromRight (64.0f);
        graph.removeFromRight (8.0f);
        const auto yOf = [&graph] (float v) { return graph.getBottom() - juce::jlimit (0.0f, 1.0f, v) * graph.getHeight(); };
        const auto a = std::sqrt (envelope.attackMs), d = std::sqrt (envelope.decayMs), r = std::sqrt (envelope.releaseMs);
        const auto hold = (a + d + r) * 0.35f;
        const auto unitWidth = graph.getWidth() / juce::jmax (1.0e-3f, a + d + hold + r);
        juce::Path curve;
        curve.startNewSubPath (graph.getX(), yOf (0.0f));
        auto x = graph.getX();
        constexpr int segments = 40;
        for (int i = 1; i <= segments; ++i)   // attack and decay, in time through each
        {
            const auto t = (float) i / (float) segments;
            curve.lineTo (x + a * unitWidth * t, yOf (Mod::envelopeHeld (envelope, (double) (envelope.attackMs * t) * 0.001)));
        }
        x += a * unitWidth;
        for (int i = 1; i <= segments; ++i)
        {
            const auto t = (float) i / (float) segments;
            curve.lineTo (x + d * unitWidth * t, yOf (Mod::envelopeHeld (envelope, (double) (envelope.attackMs + envelope.decayMs * t) * 0.001)));
        }
        x += (d + hold) * unitWidth;
        curve.lineTo (x, yOf (envelope.sustain));
        for (int i = 1; i <= segments; ++i)
        {
            const auto t = (float) i / (float) segments;
            curve.lineTo (x + r * unitWidth * t, yOf (Mod::envelopeReleased (envelope, envelope.sustain, (double) (envelope.releaseMs * t) * 0.001)));
        }
        g.setColour (colour.withAlpha (0.3f));
        g.strokePath (curve, juce::PathStrokeType (4.0f));
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (1.5f));
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        const auto stageX = [&graph, unitWidth] (float from, float width) { return juce::Rectangle<float> (graph.getX() + from * unitWidth, graph.getBottom() - 10.0f, width * unitWidth, 10.0f); };
        g.drawText ("A", stageX (0.0f, a), juce::Justification::centred, false);
        g.drawText ("D", stageX (a, d), juce::Justification::centred, false);
        g.drawText ("S", stageX (a + d, hold), juce::Justification::centred, false);
        g.drawText ("R", stageX (a + d + hold, r), juce::Justification::centred, false);
        // Now: the newest note's level as a line, each note's beside it.
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (yOf (live)), graph.getX(), graph.getRight());
        paintVoices (g, voices, source, colour);
    }
    else if (tab == spTab)
    {
        // Each note sounding: a dot at its value, along the keyboard (low left).
        const auto name = [] (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4); };
        auto any = false;
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        for (int v = 0; v < Mod::numNoteVoices; ++v)
        {
            const auto note = actions.voiceNote != nullptr ? actions.voiceNote (v) : -1;
            if (note < 0)
                continue;
            any = true;
            const auto x = inner.getX() + inner.getWidth() * juce::jlimit (0.0f, 1.0f, (float) (note - 36) / 60.0f);
            const auto y = yFor (actions.voiceValue != nullptr ? actions.voiceValue (Mod::sp, v) : 0.0f);
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x, y }));
            g.setColour (ParagonColours::textDim);
            g.drawText (name (note), juce::Rectangle<float> (40.0f, 10.0f).withCentre ({ x, y > inner.getCentreY() ? y - 11.0f : y + 11.0f }),
                        juce::Justification::centred, false);
        }
        if (! any)
        {
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawFittedText ("EACH NOTE SHOWS HERE AS IT PLAYS (GENERATE OR MIDI)", inner.toNearestInt(), juce::Justification::centred, 2);
        }
    }
    else if (tab == midiTab)
    {
        // What MIDI says now: a bar each (the pitch wheel and keyboard both ways from the middle).
        const int sources[] = { Mod::velocity, Mod::keyboard, Mod::pitchWheel, Mod::modWheel, Mod::aftertouch };
        const char* names[] = { "VELOCITY", "KEYBOARD", "PITCH WHEEL", "MOD WHEEL", "AFTERTOUCH" };
        const auto rowHeight = inner.getHeight() / 5.0f;
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        for (int i = 0; i < 5; ++i)
        {
            auto row = juce::Rectangle<float> (inner.getX(), inner.getY() + rowHeight * (float) i, inner.getWidth(), rowHeight).reduced (0.0f, 2.0f);
            const auto label = row.removeFromLeft (76.0f);
            g.setColour (ParagonColours::textDim);
            g.drawText (names[i], label, juce::Justification::centredLeft, false);
            const auto now = actions.value != nullptr ? actions.value (sources[i]) : 0.0f;
            const auto bipolar = sources[i] == Mod::keyboard || sources[i] == Mod::pitchWheel;
            g.drawRoundedRectangle (row, 2.0f, 1.0f);
            g.setColour (colour.withAlpha (0.85f));
            if (bipolar)
            {
                const auto middle = row.getCentreX(), end = middle + juce::jlimit (-1.0f, 1.0f, now) * row.getWidth() * 0.5f;
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (middle, end), row.getY() + 1.0f, juce::jmax (middle, end) + 1.0f, row.getBottom() - 1.0f));
            }
            else
                g.fillRect (row.reduced (1.0f).withWidth ((row.getWidth() - 2.0f) * juce::jlimit (0.0f, 1.0f, now)));
        }
    }
    else if (tab == amTab)
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
