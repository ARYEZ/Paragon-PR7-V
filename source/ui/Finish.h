#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The unit's finish: Obsidian (a pearl black unit, the default) or Aurora
// (pearl white). On both the keys and knobs are black, in neon rings (pink to
// red to orange): the footswitches' and slot buttons' bezels, the small keys'
// surrounds, the knobs' rings and the expression pedal's ribs. Only the unit
// itself changes (its chassis, the pedal's plate, the lettering printed on
// it); the screens, the neon and the dark panels round it stay as they are.
// (0.59.0 and before also had Pearl White, with chrome rings: retired.)
//
// It's a preference of the person, not part of a project or a rig: kept in a
// small settings file in the user's application data folder, so every
// PR7-V opened afterwards, in any project, uses the same finish. Every open
// window repaints when it changes.
namespace Finish
{
    // Saved by number: only ever add to the end. retiredPearlWhite (0) is
    // only ever read from an old settings file, and opens as Obsidian.
    enum class Type { retiredPearlWhite, obsidian, aurora };

    Type get();
    // `remember` saves it to the settings file (off for tests and snapshots).
    void set (Type, bool remember = true);
    inline bool isBlack() { return get() == Type::obsidian; }            // the chassis colour

    // Told whenever the finish changes, to repaint.
    juce::ChangeBroadcaster& changes();

    // Colours that follow the finish.
    juce::Colour ink();          // lettering printed on the unit: near black on white, pearl white on black
    juce::Colour inkDim();
    juce::Colour stampEdge();    // the fine edge under stamped lettering: a light catch on white, a shadow on black
    juce::Colour printedRed();   // the red lettering and lines (a touch brighter on black, so it reads as clearly)

    // The satin grain laid over pearl surfaces: a fine speckle with no
    // direction to it (so no brushed lines) and the odd bright mica fleck.
    // On black the flecks show more, as they do in a real black pearl paint.
    juce::Image makeSatinGrain (int width, int height, int seed);

    // The neon of the rings round the keys and knobs and of the pedal's ribs:
    // hot pink at the left through red to orange at the right (the reference swatch's
    // colours, 25% darker), across `left` to `right`. Lighting is drawn over it.
    juce::ColourGradient neonGradient (float left, float right);

    // Picks the colour for the current finish.
    inline juce::Colour pick (juce::uint32 onWhite, juce::uint32 onBlack) { return juce::Colour (isBlack() ? onBlack : onWhite); }
}
