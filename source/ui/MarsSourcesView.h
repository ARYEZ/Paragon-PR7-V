#pragma once

#include "MarsModUi.h"

// MARS's MOD page: the modulation sources. A tab for each (XLFO 1-2, EG 1-2,
// EF 1-2, MIDI 1-2, XY 1-2); the picked one's display on the left, its
// controls on the right.
//
//   XLFO   the steps: click one to pick it (its VALUE, GLIDE, CURVE and
//          RANDOM are on the right), drag it up or down; + and - add a step
//          after the picked one (a copy) or remove it. The triangle on top is
//          PHASE: drag it. The line moving across is where the XLFO is now.
//   EG     the envelope: drag the points (delay, attack, decay and sustain,
//          hold, release) and the dots on the curves (their slope).
//   EF     what it's doing over the last two seconds.
//   MIDI   the response curve, and where the input is on it.
//   XY     the pad (or the slider): drag it.
class MarsSourcesView final : public juce::Component
{
public:
    MarsSourcesView (juce::AudioProcessorValueTreeState&, const effects::Mars*, std::function<void (int target)> modulateMenu);
    ~MarsSourcesView() override;

    static constexpr int numTabs = 10;
    static inline const juce::StringArray tabNames { "XLFO 1", "XLFO 2", "EG 1", "EG 2", "EF 1", "EF 2", "MIDI 1", "MIDI 2", "XY 1", "XY 2" };
    void selectTab (int tab);
    int getSelectedTab() const noexcept { return tab; }

    // The picked XLFO step; adding one after it (a copy) or removing it. Public for tests.
    void selectStep (int step);
    int getSelectedStep() const noexcept { return step; }
    void addStep();
    void removeStep();

    void refresh();   // the live parts: call regularly while shown

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Kind { lfo, eg, ef, midi, xy };
    Kind kind() const noexcept;
    int index() const noexcept { return tab % 2; }   // which of the two
    int sourceOfTab (int tabIndex) const noexcept;   // the first of its outputs (XY: X)
    void bind();
    void layOut();
    float value (const juce::String& id) const;
    void set (const juce::String& id, float plainValue);
    void beginGesture (const juce::String& id);
    void setDuringGesture (const juce::String& id, float plainValue);
    void endGesture (const juce::String& id);
    juce::String lfoId (const char* part) const;
    juce::String stepId (int stepIndex, const char* part) const;
    juce::String egId (const char* part) const;
    int numSteps() const;
    int stepAt (float x) const;
    juce::Rectangle<float> stepArea() const;

    // The EG's points and slope dots on the display (in its draw order).
    struct EgGeometry { std::array<juce::Point<float>, 6> points; std::array<juce::Point<float>, 3> slopes; };
    EgGeometry egGeometry() const;
    void paintLfo (juce::Graphics&);
    void paintEg (juce::Graphics&);
    void paintEf (juce::Graphics&);
    void paintMidi (juce::Graphics&);
    void paintXy (juce::Graphics&);

    using Knob = MarsModUi::Knob;
    using Choice = MarsModUi::Choice;
    using Switch = MarsModUi::Switch;

    juce::AudioProcessorValueTreeState& parameters;
    const effects::Mars* mars;
    std::function<void (int)> showModulateMenu;
    int tab = 0, step = 0;

    std::array<juce::TextButton, numTabs> tabs;
    juce::TextButton plusStep, minusStep;
    Knob rate, offset, balance, glide, phase, stepValue, stepGlide;
    Choice sync, midiRestart, stepCurve;
    Switch stepRandom;
    Choice egTrigger, egRange;
    Knob egThreshold, egDelay, egAttack, egDecay, egSustain, egHold, egRelease;
    Choice efTrigger, efMode;
    Knob efGain, efAttack, efRelease;
    Choice midiInput, midiResponse;
    Knob midiController;
    Choice xyMode, xyRange;
    Knob xyX, xyY;

    juce::Rectangle<int> tabRow, controls;
    juce::Rectangle<float> display;

    // Dragging on the display.
    enum class Drag { none, step, phase, egPoint, egSlope, xy };
    Drag dragging = Drag::none;
    int dragIndex = -1;
    float dragStartValue = 0.0f;

    // The live parts.
    std::array<float, 120> efHistory {};
    int efHistoryWrite = 0;
    std::array<float, numTabs> shownSource {};
    float shownPosition = 0.0f, shownEg = 0.0f;
};
