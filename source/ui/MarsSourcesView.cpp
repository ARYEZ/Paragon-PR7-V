#include "MarsSourcesView.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"

using Mod = effects::MarsModulation;

MarsSourcesView::MarsSourcesView (juce::AudioProcessorValueTreeState& state, const effects::Mars* marsToShow,
                                  std::function<void (int)> modulateMenu)
    : parameters (state), mars (marsToShow), showModulateMenu (std::move (modulateMenu))
{
    for (int t = 0; t < numTabs; ++t)
    {
        auto& button = tabs[(size_t) t];
        button.setButtonText (tabNames[t]);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (5330);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff26202a));
        button.setColour (juce::TextButton::textColourOnId, MarsModUi::sourceColour (sourceOfTab (t)));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        button.onClick = [this, t] { selectTab (t); };
        addAndMakeVisible (button);
    }
    for (auto* button : { &plusStep, &minusStep })
    {
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button->setColour (juce::TextButton::textColourOffId, ParagonColours::text);
        addAndMakeVisible (*button);
    }
    plusStep.setButtonText ("+");
    plusStep.setTooltip ("Add a step after the picked one (a copy of it)");
    plusStep.onClick = [this] { addStep(); };
    minusStep.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")));
    minusStep.setTooltip ("Remove the picked step");
    minusStep.onClick = [this] { removeStep(); };

    using MarsModUi::setUp;
    setUp (*this, rate, "RATE", "How fast the XLFO goes round (Hz)");
    setUp (*this, offset, "OFFSET", "Synced: faster or slower than the tempo, x0.5 to x2");
    setUp (*this, sync, "SYNC", Mod::syncNames, "Free (RATE in Hz), or one cycle per so many bars of the song's tempo");
    setUp (*this, balance, "BALANCE", "Left: the first half of the wave goes by quickly. Right: the second");
    setUp (*this, glide, "GLIDE", "Added to every step's glide: more slides from step to step, less steps");
    setUp (*this, phase, "PHASE", "Where the cycle starts (the triangle on the display)");
    setUp (*this, midiRestart, "MIDI", Mod::midiRestartNames, "Restart the cycle on a MIDI note: every note, or only the first of held notes (Legato)");
    setUp (*this, stepValue, "VALUE", "The picked step's value");
    setUp (*this, stepGlide, "STEP GLIDE", "How much of the picked step slides into the next");
    setUp (*this, stepCurve, "CURVE", Mod::curveNames, "The shape of the slide into the next step");
    setUp (*this, stepRandom, "RANDOM", "A new random value each time round");

    setUp (*this, egTrigger, "TRIGGER", Mod::triggerNames, "What sets it off: your voice, one band, or MIDI notes");
    setUp (*this, egThreshold, "THRESHOLD", "How loud the voice must be to set it off");
    setUp (*this, egRange, "RANGE", Mod::egRangeNames, "0 to 1, or centred (0 while it sustains, so it swings both ways)");
    setUp (*this, egDelay, "DELAY", "Wait before the attack");
    setUp (*this, egHold, "HOLD", "Stay at the sustain level this long after the voice stops");
    setUp (*this, egAttack, "ATTACK", "Time to rise to the top");
    setUp (*this, egDecay, "DECAY", "Time to fall to the sustain level");
    setUp (*this, egSustain, "SUSTAIN", "The level while the voice goes on");
    setUp (*this, egRelease, "RELEASE", "Time to fall back to 0 after the hold");

    auto efTriggers = Mod::triggerNames;
    efTriggers.removeString ("MIDI Notes");
    setUp (*this, efTrigger, "TRIGGER", efTriggers, "What it follows: your voice, or one band");
    setUp (*this, efMode, "MODE", Mod::efModeNames, "Envelope: how loud. Transient: only the attacks (word starts, consonants)");
    setUp (*this, efGain, "GAIN", "How sensitive: more makes quieter singing read higher");
    setUp (*this, efAttack, "ATTACK", "How quickly it rises");
    setUp (*this, efRelease, "RELEASE", "How quickly it falls back");

    setUp (*this, midiInput, "INPUT", Mod::midiInputNames, "What MIDI moves it: velocity, pitch bend, the mod wheel, aftertouch, or any controller");
    setUp (*this, midiController, "CC", "The controller number (Controller input)");
    setUp (*this, midiResponse, "RESPONSE", Mod::responseNames, "How the MIDI value is curved");

    setUp (*this, xyMode, "MODE", Mod::xyModeNames, "Two directions (X and Y) or one (a slider: Y)");
    setUp (*this, xyRange, "RANGE", Mod::xyRangeNames, "Bipolar: -1 to 1 (the middle moves nothing). Unipolar: 0 to 1");
    setUp (*this, xyX, "X", "Left to right");
    setUp (*this, xyY, "Y", "Bottom to top");

    for (auto* knob : { &rate, &offset, &balance, &glide, &phase, &egThreshold, &egDelay, &egAttack, &egDecay,
                        &egSustain, &egHold, &egRelease, &efAttack, &efRelease })
        knob->slider.onModulateMenu = [this] (int target) { if (showModulateMenu != nullptr) showModulateMenu (target); };

    selectTab (0);
}

MarsSourcesView::~MarsSourcesView() = default;

//==============================================================================
MarsSourcesView::Kind MarsSourcesView::kind() const noexcept
{
    constexpr Kind kinds[] = { Kind::lfo, Kind::eg, Kind::ef, Kind::midi, Kind::xy };
    return kinds[tab / 2];
}

int MarsSourcesView::sourceOfTab (int tabIndex) const noexcept
{
    const auto which = tabIndex % 2;
    switch (tabIndex / 2)
    {
        case 0:  return Mod::lfo1 + which;
        case 1:  return Mod::eg1 + which;
        case 2:  return Mod::ef1 + which;
        case 3:  return Mod::midi1 + which;
        default: return Mod::xy1x + 2 * which;
    }
}

float MarsSourcesView::value (const juce::String& id) const { return MarsModUi::value (parameters, id); }
void MarsSourcesView::set (const juce::String& id, float plainValue) { MarsModUi::set (parameters, id, plainValue); }
void MarsSourcesView::beginGesture (const juce::String& id) { parameters.getParameter (id)->beginChangeGesture(); }
void MarsSourcesView::endGesture (const juce::String& id) { parameters.getParameter (id)->endChangeGesture(); }

void MarsSourcesView::setDuringGesture (const juce::String& id, float plainValue)
{
    auto* parameter = parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

juce::String MarsSourcesView::lfoId (const char* part) const { return ParamIDs::marsLfo (index(), part); }
juce::String MarsSourcesView::stepId (int stepIndex, const char* part) const { return ParamIDs::marsStep (index(), stepIndex, part); }
juce::String MarsSourcesView::egId (const char* part) const { return ParamIDs::marsEg (index(), part); }
int MarsSourcesView::numSteps() const { return juce::roundToInt (value (lfoId ("Steps"))) + 1; }

void MarsSourcesView::selectTab (int newTab)
{
    tab = juce::jlimit (0, numTabs - 1, newTab);
    tabs[(size_t) tab].setToggleState (true, juce::dontSendNotification);
    step = juce::jmin (step, numSteps() - 1);
    bind();
    layOut();
    repaint();
}

void MarsSourcesView::selectStep (int newStep)
{
    step = juce::jlimit (0, numSteps() - 1, newStep);
    using MarsModUi::attach;
    attach (parameters, stepValue, stepId (step, "Value"));
    attach (parameters, stepGlide, stepId (step, "Glide"));
    attach (parameters, stepCurve, stepId (step, "Curve"));
    attach (parameters, stepRandom, stepId (step, "Random"));
    repaint();
}

void MarsSourcesView::addStep()
{
    const auto count = numSteps();
    if (count >= Mod::maxSteps)
        return;
    // The steps after the picked one move along; the new one copies it.
    for (int s = count - 1; s > step; --s)
        for (const auto& part : ParamIDs::marsStepParts)
            set (stepId (s + 1, part.toRawUTF8()), value (stepId (s, part.toRawUTF8())));
    for (const auto& part : ParamIDs::marsStepParts)
        set (stepId (step + 1, part.toRawUTF8()), value (stepId (step, part.toRawUTF8())));
    set (lfoId ("Steps"), (float) count);   // index count: count + 1 steps
    selectStep (step + 1);
}

void MarsSourcesView::removeStep()
{
    const auto count = numSteps();
    if (count <= 1)
        return;
    for (int s = step; s < count - 1; ++s)
        for (const auto& part : ParamIDs::marsStepParts)
            set (stepId (s, part.toRawUTF8()), value (stepId (s + 1, part.toRawUTF8())));
    set (lfoId ("Steps"), (float) (count - 2));
    selectStep (juce::jmin (step, count - 2));
}

void MarsSourcesView::bind()
{
    using MarsModUi::attach;
    const auto l = index();
    attach (parameters, rate, lfoId ("Rate"), Mod::lfoTarget (l, Mod::lfoRate));
    attach (parameters, offset, lfoId ("Offset"), Mod::lfoTarget (l, Mod::lfoRate));
    attach (parameters, balance, lfoId ("Balance"), Mod::lfoTarget (l, Mod::lfoBalance));
    attach (parameters, glide, lfoId ("Glide"), Mod::lfoTarget (l, Mod::lfoGlide));
    attach (parameters, phase, lfoId ("Phase"), Mod::lfoTarget (l, Mod::lfoPhase));
    attach (parameters, sync, lfoId ("Sync"));
    attach (parameters, midiRestart, lfoId ("Midi"));
    selectStep (step);

    attach (parameters, egTrigger, egId ("Trigger"));
    attach (parameters, egRange, egId ("Range"));
    const std::pair<Knob*, int> egKnobs[] = { { &egThreshold, Mod::egThreshold }, { &egDelay, Mod::egDelay }, { &egAttack, Mod::egAttack },
                                              { &egDecay, Mod::egDecay }, { &egSustain, Mod::egSustain }, { &egHold, Mod::egHold },
                                              { &egRelease, Mod::egRelease } };
    const char* egParts[] = { "Threshold", "Delay", "Attack", "Decay", "Sustain", "Hold", "Release" };
    for (auto [knob, part] : egKnobs)
        attach (parameters, *knob, egId (egParts[part]), Mod::egTarget (l, part));

    attach (parameters, efTrigger, ParamIDs::marsEf (l, "Trigger"));
    attach (parameters, efMode, ParamIDs::marsEf (l, "Mode"));
    attach (parameters, efGain, ParamIDs::marsEf (l, "Gain"));
    attach (parameters, efAttack, ParamIDs::marsEf (l, "Attack"), Mod::efTarget (l, Mod::efAttack));
    attach (parameters, efRelease, ParamIDs::marsEf (l, "Release"), Mod::efTarget (l, Mod::efRelease));

    attach (parameters, midiInput, ParamIDs::marsMidi (l, "Input"));
    attach (parameters, midiController, ParamIDs::marsMidi (l, "Controller"));
    attach (parameters, midiResponse, ParamIDs::marsMidi (l, "Response"));

    attach (parameters, xyMode, ParamIDs::marsXy (l, "Mode"));
    attach (parameters, xyRange, ParamIDs::marsXy (l, "Range"));
    attach (parameters, xyX, ParamIDs::marsXy (l, "X"));
    attach (parameters, xyY, ParamIDs::marsXy (l, "Y"));
}

void MarsSourcesView::layOut()
{
    using MarsModUi::show;
    const auto k = kind();
    const auto synced = juce::roundToInt (value (lfoId ("Sync"))) > 0;
    for (auto* knob : { &balance, &glide, &phase, &stepValue, &stepGlide })
        show (*knob, k == Kind::lfo);
    show (rate, k == Kind::lfo && ! synced);
    show (offset, k == Kind::lfo && synced);
    for (auto* choice : { &sync, &midiRestart, &stepCurve })
        show (*choice, k == Kind::lfo);
    show (stepRandom, k == Kind::lfo);
    plusStep.setVisible (k == Kind::lfo);
    minusStep.setVisible (k == Kind::lfo);
    for (auto* choice : { &egTrigger, &egRange })
        show (*choice, k == Kind::eg);
    for (auto* knob : { &egThreshold, &egDelay, &egAttack, &egDecay, &egSustain, &egHold, &egRelease })
        show (*knob, k == Kind::eg);
    for (auto* choice : { &efTrigger, &efMode })
        show (*choice, k == Kind::ef);
    for (auto* knob : { &efGain, &efAttack, &efRelease })
        show (*knob, k == Kind::ef);
    show (midiInput, k == Kind::midi);
    show (midiResponse, k == Kind::midi);
    show (midiController, k == Kind::midi);
    show (xyMode, k == Kind::xy);
    show (xyRange, k == Kind::xy);
    show (xyX, k == Kind::xy && juce::roundToInt (value (ParamIDs::marsXy (index(), "Mode"))) == 0);
    show (xyY, k == Kind::xy);
    resized();
}

//==============================================================================
void MarsSourcesView::resized()
{
    auto area = getLocalBounds();
    tabRow = area.removeFromTop (18);
    const auto tabWidth = (float) tabRow.getWidth() / numTabs;
    for (int t = 0; t < numTabs; ++t)
        tabs[(size_t) t].setBounds (juce::Rectangle<float> (tabRow.getX() + t * tabWidth, (float) tabRow.getY(), tabWidth, (float) tabRow.getHeight())
                                        .reduced (1.0f, 0.0f).toNearestInt());
    area.removeFromTop (6);
    display = area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.36f)).toFloat();
    area.removeFromLeft (8);
    controls = area;

    const auto synced = juce::roundToInt (value (lfoId ("Sync"))) > 0;
    if (kind() == Kind::lfo)
    {
        const auto buttons = display.toNearestInt().removeFromRight (16).reduced (1, 2);
        plusStep.setBounds (buttons.withHeight (16));
        minusStep.setBounds (buttons.withHeight (16).translated (0, 18));
    }

    // Two rows of cells, dropdowns wider than knobs.
    struct Cell { juce::Component* label; juce::Component* widget; float weight; bool isChoice; };
    const auto knob = [] (Knob& k) { return Cell { &k.label, &k.slider, 1.0f, false }; };
    const auto choice = [] (Choice& c, float weight) { return Cell { &c.label, &c.box, weight, true }; };
    const auto rowHeight = controls.getHeight() / 2;
    const auto layRow = [&] (int row, std::initializer_list<Cell> cells)
    {
        auto line = juce::Rectangle<int> (controls.getX(), controls.getY() + row * rowHeight, controls.getWidth(), rowHeight);
        auto total = 0.0f;
        for (const auto& cell : cells)
            total += cell.weight;
        const auto unit = (float) line.getWidth() / juce::jmax (5.6f, total);
        for (const auto& cell : cells)
        {
            const auto cellArea = line.removeFromLeft (juce::roundToInt (unit * cell.weight)).reduced (2, 0);
            MarsModUi::place (*cell.label, *cell.widget, cellArea, cell.isChoice ? 22 : 0);
        }
    };
    switch (kind())
    {
        case Kind::lfo:
            layRow (0, { knob (synced ? offset : rate), choice (sync, 1.6f), knob (balance), knob (glide), knob (phase) });
            layRow (1, { choice (midiRestart, 1.6f), knob (stepValue), knob (stepGlide), choice (stepCurve, 1.2f), Cell { &stepRandom.label, &stepRandom.button, 0.8f, true } });
            break;
        case Kind::eg:
            layRow (0, { choice (egTrigger, 1.6f), knob (egThreshold), choice (egRange, 1.4f), knob (egDelay), knob (egHold) });
            layRow (1, { knob (egAttack), knob (egDecay), knob (egSustain), knob (egRelease) });
            break;
        case Kind::ef:
            layRow (0, { choice (efTrigger, 1.6f), choice (efMode, 1.8f), knob (efGain) });
            layRow (1, { knob (efAttack), knob (efRelease) });
            break;
        case Kind::midi:
            layRow (0, { choice (midiInput, 2.0f), knob (midiController), choice (midiResponse, 2.0f) });
            break;
        case Kind::xy:
            layRow (0, { choice (xyMode, 1.6f), choice (xyRange, 1.8f) });
            layRow (1, { knob (xyX), knob (xyY) });
            break;
    }
}

//==============================================================================
juce::Rectangle<float> MarsSourcesView::stepArea() const
{
    // Below the PHASE strip, left of the + and - buttons.
    return display.reduced (4.0f, 0.0f).withTrimmedTop (12.0f).withTrimmedBottom (4.0f).withTrimmedRight (16.0f);
}

int MarsSourcesView::stepAt (float x) const
{
    const auto area = stepArea();
    return juce::jlimit (0, numSteps() - 1, (int) ((x - area.getX()) / area.getWidth() * (float) numSteps()));
}

MarsSourcesView::EgGeometry MarsSourcesView::egGeometry() const
{
    // Each time gets up to a fifth of the width, by its knob's position;
    // the sustain a fixed piece.
    const auto area = display.reduced (8.0f, 10.0f).withTrimmedRight (10.0f);
    const auto unit = area.getWidth() / 6.0f;
    const auto l = index();
    const auto width = [&] (int part, const char* id) { return unit * Mod::targetRange (Mod::egTarget (l, part)).convertTo0to1 (value (egId (id))); };
    const auto sustainY = area.getBottom() - area.getHeight() * value (egId ("Sustain"));
    EgGeometry geometry;
    auto& p = geometry.points;
    p[0] = { area.getX(), area.getBottom() };
    p[1] = { p[0].x + width (Mod::egDelay, "Delay"), area.getBottom() };
    p[2] = { p[1].x + width (Mod::egAttack, "Attack"), area.getY() };
    p[3] = { p[2].x + width (Mod::egDecay, "Decay"), sustainY };
    p[4] = { p[3].x + unit + width (Mod::egHold, "Hold"), sustainY };
    p[5] = { p[4].x + width (Mod::egRelease, "Release"), area.getBottom() };
    const auto curveY = [&] (float from, float to, float slope) { return from + (to - from) * (float) Mod::egCurve (0.5, slope); };
    geometry.slopes[0] = { 0.5f * (p[1].x + p[2].x), curveY (p[1].y, p[2].y, value (egId ("AttackSlope"))) };
    geometry.slopes[1] = { 0.5f * (p[2].x + p[3].x), curveY (p[2].y, p[3].y, value (egId ("DecaySlope"))) };
    geometry.slopes[2] = { 0.5f * (p[4].x + p[5].x), curveY (p[4].y, p[5].y, value (egId ("ReleaseSlope"))) };
    return geometry;
}

void MarsSourcesView::mouseDown (const juce::MouseEvent& event)
{
    if (! display.contains (event.position))
        return;
    const auto pos = event.position;
    dragging = Drag::none;
    switch (kind())
    {
        case Kind::lfo:
        {
            const auto area = stepArea();
            if (pos.x > area.getRight())
                return;
            if (pos.y < area.getY())
            {
                dragging = Drag::phase;
                beginGesture (lfoId ("Phase"));
                mouseDrag (event);
                return;
            }
            selectStep (stepAt (pos.x));
            dragging = Drag::step;
            dragIndex = step;
            beginGesture (stepId (step, "Value"));
            mouseDrag (event);
            return;
        }
        case Kind::eg:
        {
            const auto geometry = egGeometry();
            for (int i = 1; i < 6; ++i)
                if (geometry.points[(size_t) i].getDistanceFrom (pos) < 8.0f)
                {
                    dragging = Drag::egPoint;
                    dragIndex = i;
                }
            for (int i = 0; i < 3; ++i)
                if (dragging == Drag::none && geometry.slopes[(size_t) i].getDistanceFrom (pos) < 7.0f)
                {
                    const char* ids[] = { "AttackSlope", "DecaySlope", "ReleaseSlope" };
                    dragging = Drag::egSlope;
                    dragIndex = i;
                    dragStartValue = value (egId (ids[i]));
                    beginGesture (egId (ids[i]));
                }
            if (dragging == Drag::egPoint)
            {
                const char* ids[] = { "", "Delay", "Attack", "Decay", "Hold", "Release" };
                beginGesture (egId (ids[dragIndex]));
                if (dragIndex == 3)
                    beginGesture (egId ("Sustain"));
            }
            return;
        }
        case Kind::xy:
            dragging = Drag::xy;
            beginGesture (ParamIDs::marsXy (index(), "X"));
            beginGesture (ParamIDs::marsXy (index(), "Y"));
            mouseDrag (event);
            return;
        case Kind::ef:
        case Kind::midi:
            return;
    }
}

void MarsSourcesView::mouseDrag (const juce::MouseEvent& event)
{
    const auto pos = event.position;
    switch (dragging)
    {
        case Drag::step:
        {
            const auto area = stepArea();
            setDuringGesture (stepId (dragIndex, "Value"), juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * (pos.y - area.getY()) / area.getHeight()));
            break;
        }
        case Drag::phase:
        {
            const auto area = stepArea();
            setDuringGesture (lfoId ("Phase"), juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth()));
            break;
        }
        case Drag::egPoint:
        {
            const auto geometry = egGeometry();
            const auto area = display.reduced (8.0f, 10.0f).withTrimmedRight (10.0f);
            const auto unit = area.getWidth() / 6.0f;
            const auto start = dragIndex == 4 ? geometry.points[3].x + unit : geometry.points[(size_t) dragIndex - 1].x;
            const int parts[] = { 0, Mod::egDelay, Mod::egAttack, Mod::egDecay, Mod::egHold, Mod::egRelease };
            const char* ids[] = { "", "Delay", "Attack", "Decay", "Hold", "Release" };
            const auto range = Mod::targetRange (Mod::egTarget (index(), parts[dragIndex]));
            setDuringGesture (egId (ids[dragIndex]), range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (pos.x - start) / unit)));
            if (dragIndex == 3)
                setDuringGesture (egId ("Sustain"), juce::jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight()));
            break;
        }
        case Drag::egSlope:
        {
            // Up bows the curve up: for the attack that's a negative slope,
            // for the falling decay and release a positive one.
            const char* ids[] = { "AttackSlope", "DecaySlope", "ReleaseSlope" };
            const auto moved = (float) event.getDistanceFromDragStartY() / 40.0f;
            setDuringGesture (egId (ids[dragIndex]), juce::jlimit (-1.0f, 1.0f, dragStartValue + (dragIndex == 0 ? moved : -moved)));
            break;
        }
        case Drag::xy:
        {
            auto pad = display.reduced (8.0f).withTrimmedRight (10.0f);
            const auto slider = juce::roundToInt (value (ParamIDs::marsXy (index(), "Mode"))) == 1;
            pad = slider ? pad.withSizeKeepingCentre (30.0f, pad.getHeight()) : pad.withSizeKeepingCentre (juce::jmin (pad.getWidth(), pad.getHeight()), juce::jmin (pad.getWidth(), pad.getHeight()));
            if (! slider)
                setDuringGesture (ParamIDs::marsXy (index(), "X"), juce::jlimit (0.0f, 1.0f, (pos.x - pad.getX()) / pad.getWidth()));
            setDuringGesture (ParamIDs::marsXy (index(), "Y"), juce::jlimit (0.0f, 1.0f, (pad.getBottom() - pos.y) / pad.getHeight()));
            break;
        }
        case Drag::none:
            return;
    }
    repaint (display.toNearestInt().expanded (2));
}

void MarsSourcesView::mouseUp (const juce::MouseEvent&)
{
    switch (dragging)
    {
        case Drag::step:   endGesture (stepId (dragIndex, "Value")); break;
        case Drag::phase:  endGesture (lfoId ("Phase")); break;
        case Drag::egPoint:
        {
            const char* ids[] = { "", "Delay", "Attack", "Decay", "Hold", "Release" };
            endGesture (egId (ids[dragIndex]));
            if (dragIndex == 3)
                endGesture (egId ("Sustain"));
            break;
        }
        case Drag::egSlope:
        {
            const char* ids[] = { "AttackSlope", "DecaySlope", "ReleaseSlope" };
            endGesture (egId (ids[dragIndex]));
            break;
        }
        case Drag::xy:
            endGesture (ParamIDs::marsXy (index(), "X"));
            endGesture (ParamIDs::marsXy (index(), "Y"));
            break;
        case Drag::none:
            break;
    }
    dragging = Drag::none;
}

void MarsSourcesView::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Double-click puts a step (or PHASE, or an EG curve) back to its middle.
    if (! display.contains (event.position))
        return;
    if (kind() == Kind::lfo)
    {
        if (event.position.y < stepArea().getY())
            set (lfoId ("Phase"), 0.0f);
        else if (event.position.x <= stepArea().getRight())
            set (stepId (stepAt (event.position.x), "Value"), 0.0f);
    }
    else if (kind() == Kind::eg)
    {
        const auto geometry = egGeometry();
        const char* ids[] = { "AttackSlope", "DecaySlope", "ReleaseSlope" };
        for (int i = 0; i < 3; ++i)
            if (geometry.slopes[(size_t) i].getDistanceFrom (event.position) < 7.0f)
                set (egId (ids[i]), 0.0f);
    }
    repaint();
}

//==============================================================================
void MarsSourcesView::refresh()
{
    if (! isVisible())
        return;
    auto changed = false;
    for (int t = 0; t < numTabs; ++t)
    {
        const auto now = mars != nullptr ? mars->getSourceValue (sourceOfTab (t) + (t >= 8 ? 1 : 0)) : 0.0f;   // XY: its Y
        changed = changed || std::abs (now - shownSource[(size_t) t]) > 0.004f;
        shownSource[(size_t) t] = now;
    }
    if (mars != nullptr)
    {
        const auto position = kind() == Kind::lfo ? mars->getLfoPosition (index()) : 0.0f;
        const auto eg = kind() == Kind::eg ? mars->getEgLevel (index()) : 0.0f;
        changed = changed || std::abs (position - shownPosition) > 0.001f || std::abs (eg - shownEg) > 0.003f;
        shownPosition = position;
        shownEg = eg;
        efHistory[(size_t) efHistoryWrite] = mars->getSourceValue (Mod::ef1 + index());
        efHistoryWrite = (efHistoryWrite + 1) % (int) efHistory.size();
        changed = changed || kind() == Kind::ef;
    }
    // SYNC changes RATE into OFFSET; SLIDER hides X.
    const auto synced = juce::roundToInt (value (lfoId ("Sync"))) > 0;
    if (kind() == Kind::lfo && rate.slider.isVisible() == synced)
        layOut();
    if (kind() == Kind::xy)
    {
        const auto slider = juce::roundToInt (value (ParamIDs::marsXy (index(), "Mode"))) == 1;
        if (xyX.slider.isVisible() == slider)
            MarsModUi::show (xyX, ! slider);
    }
    if (changed)
        repaint();
}

void MarsSourcesView::paint (juce::Graphics& g)
{
    // A thin line under each tab: what that source is doing now.
    for (int t = 0; t < numTabs; ++t)
    {
        const auto bounds = tabs[(size_t) t].getBounds().toFloat();
        const auto amount = juce::jlimit (-1.0f, 1.0f, shownSource[(size_t) t]);
        const auto middle = bounds.getCentreX();
        const auto end = middle + amount * bounds.getWidth() * 0.45f;
        g.setColour (MarsModUi::sourceColour (sourceOfTab (t)).withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (middle, end), bounds.getBottom() + 1.0f, juce::jmax (middle, end) + 1.0f, bounds.getBottom() + 3.0f));
    }

    g.setColour (juce::Colour (0xff151217));
    g.fillRoundedRectangle (display, 5.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (display, 5.0f, 1.0f);

    switch (kind())
    {
        case Kind::lfo:  paintLfo (g); break;
        case Kind::eg:   paintEg (g); break;
        case Kind::ef:   paintEf (g); break;
        case Kind::midi: paintMidi (g); break;
        case Kind::xy:   paintXy (g); break;
    }
}

void MarsSourcesView::paintOverChildren (juce::Graphics& g)
{
    for (auto* knob : { &rate, &offset, &balance, &glide, &phase, &egThreshold, &egDelay, &egAttack, &egDecay,
                        &egSustain, &egHold, &egRelease, &efAttack, &efRelease })
        if (knob->slider.isVisible() && knob->slider.target > 0)
            MarsModUi::paintModulation (g, *this, knob->slider, mars != nullptr ? mars->getTargetOffset (knob->slider.target) : 0.0f,
                                        MarsModUi::targetColour (parameters, knob->slider.target));
}

void MarsSourcesView::paintLfo (juce::Graphics& g)
{
    const auto colour = MarsModUi::sourceColour (Mod::lfo1);
    const auto area = stepArea();
    const auto count = numSteps();
    const auto columnWidth = area.getWidth() / (float) count;
    const auto yFor = [&area] (float v) { return area.getCentreY() - v * area.getHeight() * 0.5f; };

    Mod::Lfo lfo;
    lfo.numSteps = count;
    lfo.glide = value (lfoId ("Glide"));
    std::array<float, Mod::maxSteps> values {};
    for (int s = 0; s < count; ++s)
    {
        auto& stepSettings = lfo.steps[(size_t) s];
        stepSettings.value = value (stepId (s, "Value"));
        stepSettings.glide = value (stepId (s, "Glide"));
        stepSettings.curve = (Mod::Curve) juce::roundToInt (value (stepId (s, "Curve")));
        stepSettings.random = value (stepId (s, "Random")) > 0.5f;
        values[(size_t) s] = stepSettings.random ? 0.0f : stepSettings.value;
    }

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    for (int s = 0; s < count; ++s)
    {
        const auto column = juce::Rectangle<float> (area.getX() + s * columnWidth, area.getY(), columnWidth, area.getHeight()).reduced (1.0f, 0.0f);
        if (s == step)
        {
            g.setColour (colour.withAlpha (0.10f));
            g.fillRect (column);
        }
        if (lfo.steps[(size_t) s].random)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("?", column, juce::Justification::centred, false);
            continue;
        }
        const auto y = yFor (lfo.steps[(size_t) s].value);
        g.setColour (colour.withAlpha (s == step ? 0.55f : 0.25f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (column.getX(), juce::jmin (y, area.getCentreY()), column.getRight(), juce::jmax (y, area.getCentreY()) + 0.5f));
        g.setColour (colour.withAlpha (s == step ? 1.0f : 0.6f));
        g.fillRect (column.getX(), y - 1.0f, column.getWidth(), 2.0f);
    }

    // The wave itself.
    juce::Path wave;
    const auto points = juce::jmax (2, (int) area.getWidth());
    for (int i = 0; i <= points; ++i)
    {
        const auto position = (double) i / points * 0.99999;
        const auto point = juce::Point<float> (area.getX() + area.getWidth() * (float) i / points, yFor (Mod::lfoShape (lfo, position, values)));
        if (i == 0)
            wave.startNewSubPath (point);
        else
            wave.lineTo (point);
    }
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.strokePath (wave, juce::PathStrokeType (1.4f));

    // Where it is now, and PHASE's triangle.
    const auto x = area.getX() + area.getWidth() * shownPosition;
    g.setColour (colour);
    g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    const auto phaseX = area.getX() + area.getWidth() * value (lfoId ("Phase"));
    juce::Path triangle;
    triangle.addTriangle (phaseX - 5.0f, display.getY() + 2.0f, phaseX + 5.0f, display.getY() + 2.0f, phaseX, area.getY() - 1.0f);
    g.setColour (ParagonColours::text.withAlpha (0.8f));
    g.fillPath (triangle);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("STEP " + juce::String (step + 1) + "/" + juce::String (count),
                display.withTrimmedLeft (6.0f).withHeight (12.0f).withTrimmedRight (20.0f), juce::Justification::centredRight, false);
}

void MarsSourcesView::paintEg (juce::Graphics& g)
{
    const auto colour = MarsModUi::sourceColour (Mod::eg1);
    const auto geometry = egGeometry();
    const auto& p = geometry.points;
    const auto attackSlope = value (egId ("AttackSlope")), decaySlope = value (egId ("DecaySlope")), releaseSlope = value (egId ("ReleaseSlope"));

    juce::Path curve;
    curve.startNewSubPath (p[0]);
    curve.lineTo (p[1]);
    const auto addCurve = [&curve] (juce::Point<float> from, juce::Point<float> to, float slope)
    {
        for (int i = 1; i <= 24; ++i)
        {
            const auto t = i / 24.0;
            curve.lineTo (from.x + (to.x - from.x) * (float) t, from.y + (to.y - from.y) * (float) Mod::egCurve (t, slope));
        }
    };
    addCurve (p[1], p[2], attackSlope);
    addCurve (p[2], p[3], decaySlope);
    curve.lineTo (p[4]);
    addCurve (p[4], p[5], releaseSlope);

    auto filled = curve;
    filled.lineTo (p[5].x, p[0].y);
    filled.closeSubPath();
    g.setColour (colour.withAlpha (0.15f));
    g.fillPath (filled);
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    for (int i = 1; i < 6; ++i)
    {
        g.setColour (ParagonColours::text);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p[(size_t) i]));
    }
    for (const auto& dot : geometry.slopes)
    {
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot), 1.2f);
    }
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    if (p[4].x - p[3].x > 22.0f)
        g.drawFittedText ("SUSTAIN", juce::Rectangle<float> (p[3].x, p[3].y - 12.0f, p[4].x - p[3].x, 10.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);

    // The level now, on the right.
    const auto meter = display.reduced (3.0f, 10.0f).removeFromRight (4.0f);
    g.setColour (ParagonColours::panelEdge);
    g.fillRect (meter);
    g.setColour (colour);
    g.fillRect (meter.withTop (meter.getBottom() - meter.getHeight() * juce::jlimit (0.0f, 1.0f, shownEg)));
}

void MarsSourcesView::paintEf (juce::Graphics& g)
{
    const auto colour = MarsModUi::sourceColour (Mod::ef1);
    const auto area = display.reduced (6.0f, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    for (const auto level : { 0.25f, 0.5f, 0.75f })
        g.drawHorizontalLine (juce::roundToInt (area.getBottom() - area.getHeight() * level), area.getX(), area.getRight());

    juce::Path trace;
    const auto count = (int) efHistory.size();
    for (int i = 0; i < count; ++i)
    {
        const auto v = efHistory[(size_t) ((efHistoryWrite + i) % count)];
        const auto point = juce::Point<float> (area.getX() + area.getWidth() * (float) i / (float) (count - 1),
                                               area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, v));
        if (i == 0)
            trace.startNewSubPath (point);
        else
            trace.lineTo (point);
    }
    auto filled = trace;
    filled.lineTo (area.getRight(), area.getBottom());
    filled.lineTo (area.getX(), area.getBottom());
    filled.closeSubPath();
    g.setColour (colour.withAlpha (0.18f));
    g.fillPath (filled);
    g.setColour (colour);
    g.strokePath (trace, juce::PathStrokeType (1.5f));
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (juce::roundToInt (value (ParamIDs::marsEf (index(), "Mode"))) == 1 ? "TRANSIENTS" : "LEVEL",
                area.withHeight (12.0f), juce::Justification::topLeft, false);
}

void MarsSourcesView::paintMidi (juce::Graphics& g)
{
    const auto colour = MarsModUi::sourceColour (Mod::midi1);
    const auto area = display.reduced (10.0f, 10.0f);
    const auto response = (Mod::Response) juce::roundToInt (value (ParamIDs::marsMidi (index(), "Response")));
    g.setColour (ParagonColours::panelEdge);
    g.drawRect (area, 1.0f);
    juce::Path curve;
    for (int i = 0; i <= 64; ++i)
    {
        const auto x = (float) i / 64.0f;
        const auto point = juce::Point<float> (area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * Mod::respond (response, x));
        if (i == 0)
            curve.startNewSubPath (point);
        else
            curve.lineTo (point);
    }
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // Where the input is: the curve's point at the output now.
    const auto out = std::abs (shownSource[(size_t) (6 + index())]);
    auto low = 0.0f, high = 1.0f;
    for (int i = 0; i < 20; ++i)
        (Mod::respond (response, 0.5f * (low + high)) < out ? low : high) = 0.5f * (low + high);
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ area.getX() + area.getWidth() * low, area.getBottom() - area.getHeight() * out }));
}

void MarsSourcesView::paintXy (juce::Graphics& g)
{
    const auto colour = MarsModUi::sourceColour (Mod::xy1x);
    const auto slider = juce::roundToInt (value (ParamIDs::marsXy (index(), "Mode"))) == 1;
    auto pad = display.reduced (8.0f).withTrimmedRight (10.0f);
    const auto side = juce::jmin (pad.getWidth(), pad.getHeight());
    pad = slider ? pad.withSizeKeepingCentre (30.0f, pad.getHeight()) : pad.withSizeKeepingCentre (side, side);
    g.setColour (juce::Colour (0xff0d0b0e));
    g.fillRoundedRectangle (pad, 4.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (pad, 4.0f, 1.0f);
    g.drawHorizontalLine (juce::roundToInt (pad.getCentreY()), pad.getX(), pad.getRight());
    if (! slider)
        g.drawVerticalLine (juce::roundToInt (pad.getCentreX()), pad.getY(), pad.getBottom());

    const auto x = slider ? 0.5f : value (ParamIDs::marsXy (index(), "X"));
    const auto y = value (ParamIDs::marsXy (index(), "Y"));
    const auto puck = juce::Point<float> (pad.getX() + pad.getWidth() * x, pad.getBottom() - pad.getHeight() * y);
    g.setColour (colour.withAlpha (0.3f));
    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (puck));
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (puck));
}
