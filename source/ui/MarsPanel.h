#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/ParagonLookAndFeel.h"

#include "MarsModUi.h"
#include "MarsSlotsView.h"
#include "MarsSourcesView.h"

// SATURATION's panel: MARS, the multi-band saturator inspired by
// FabFilter Saturn 2. Four pages:
//
//   BANDS   left: ON, how many BANDS, and the band display: the
//           frequencies left to right, each band a column. Click a band to
//           pick it; drag a crossover line to move it; drag up or down in a
//           band for its DRIVE; double-click to split a band there (the new
//           one copies it); right-click a band to remove it.
//           right: the picked band's ON, STYLE, DRIVE, DYNAMICS, FEEDBACK,
//           FB FREQ, MIX, and BASS, MID, TREBLE, PRESENCE, LEVEL.
//   GLOBAL  INPUT, OUTPUT, MIX, QUALITY and the crossovers' SLOPE.
//   MOD     the modulation sources (MarsSourcesView).
//   SLOTS   the 16 modulation slots (MarsSlotsView).
//
// Right-click any knob that can be modulated to modulate it with a source
// (Saturn 2 drags a source onto it instead). A modulated knob gets a
// second, thin ring in its source's colour, showing where it is now.
class MarsPanel final : public juce::Component,
                        private juce::Timer
{
public:
    // `bandDrive` returns how hard a band is being driven right now (for the
    // display); `mars` (if given) shows the modulation as it happens.
    MarsPanel (juce::AudioProcessorValueTreeState&, std::function<float (int)> bandDrive, const effects::Mars* mars = nullptr);
    ~MarsPanel() override;

    // The colour of the header icon: the SATURATION block's colour in the chain. (The bands keep their own colours.)
    void setAccent (juce::Colour colour);

    // On the rack unit's face (see Faceplate), at full size. The panel then
    // covers the rack's display too (`displayArea`, in this component): the
    // band display is the rack's display, on one of the unit's screens, and
    // still played with the mouse. Along the top, the page keys, ON and
    // BANDS; on BANDS the picked band's ON, STYLE and its ten knobs in a
    // row (the unit's big knobs); on GLOBAL its row; MOD and SLOTS on one
    // wide screen across the display and the controls.
    void setRack (bool inRack, juce::Rectangle<int> displayArea);
    bool isRack() const noexcept { return rack; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void selectBand (int band);
    int getSelectedBand() const noexcept { return selected; }
    void showPage (int page);
    int getPage() const noexcept { return page; }
    static constexpr int numPages = 4;
    MarsSourcesView& getSourcesView() noexcept { return sourcesView; }
    MarsSlotsView& getSlotsView() noexcept { return slotsView; }

    // The display's frequency at an x position, and back. Public for tests.
    float frequencyAt (float x) const;
    float xFor (float hz) const;

    // Split band `band` at `hz` (the new band copies it), or remove one. Public for tests.
    void splitBand (int band, float hz);
    void removeBand (int band);

private:
    juce::Colour accent = ParagonColours::drive;
    using Knob = MarsModUi::Knob;
    using Choice = MarsModUi::Choice;
    using Switch = MarsModUi::Switch;

    void timerCallback() override;
    void bindBandControls();
    void modulateMenu (int target, juce::Component& knob);
    std::vector<Knob*> modulatableKnobs();
    float liveValue (const juce::String& id, int target) const;   // a knob's value, moved by the modulation
    float value (const juce::String& id) const;
    void set (const juce::String& id, float plainValue);
    int numBands() const;
    std::array<float, effects::Mars::maxBands + 1> bandEdges (bool live = false) const;   // each band's left and right, in Hz (live: as modulated now)
    int bandAt (float x) const;
    int crossoverAt (float x) const;                                    // the crossover line near x, or -1

    juce::AudioProcessorValueTreeState& parameters;
    std::function<float (int)> bandDrive;
    const effects::Mars* mars;
    int selected = 0, page = 0;
    int draggingCrossover = -1, draggingDriveOf = -1;
    float dragStartDrive = 0.0f;

    Switch on, bandOn;
    Choice bands, style, quality, slope;
    Knob drive, dynamics, feedback, feedbackFreq, mix, bass, mid, treble, presence, level;
    Knob input, output, globalMix, cross1, cross2, cross3;
    std::array<juce::TextButton, numPages> pages;
    MarsSourcesView sourcesView;
    MarsSlotsView slotsView;

    juce::Rectangle<float> display;
    bool rack = false;
    juce::Rectangle<int> rackDisplay;                  // on the rack: where the rack's display is
    juce::Rectangle<float> rackScreen;                 // on the rack: the screen drawn there (or across, on MOD / SLOTS)
    juce::Colour inkColour;                            // the stamped names' colour, for the finish showing
    mutable std::vector<juce::Rectangle<float>> glasses;
    void layOutRack();
    void styleForRack();
    std::vector<juce::Label*> allLabels();
    juce::Rectangle<int> bandTitle;
    std::array<float, effects::Mars::maxBands> shownDrive {};
    float shownModulation = 0.0f;   // a sum of the knobs' offsets: when it moves, they're redrawn
};
