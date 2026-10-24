#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Line icons from the mockup, drawn as vectors so they stay sharp at any
// window size. Each is designed in a 0-1 square and stroked by drawIcon().
namespace Icons
{
    enum class Type { amp, cabinet, rig, comp, drive, modulation, delay, reverb, guitar, speaker,
                      // Phase 6: header and expression pedal
                      processor, presets, undo, redo, tap, tuner, volume, wah,
                      // Phase 7: header and dock
                      save, settings, star, user, factory, plus, chevronLeft, chevronRight, flame,
                      // Phase 8
                      eq,
                      // Version 0.10
                      filterPitch,
                      // Version 0.12
                      loFi,
                      // Version 0.24
                      glitch,
                      // Version 0.27
                      pitch,
                      // PR7-V 0.3.0
                      doubler, microphone,
                      // PR7-V 0.5.0
                      deEsser };

    juce::Path get (Type type);

    // Draws `type` fitted into `area`, with an optional soft glow behind it.
    void draw (juce::Graphics&, Type type, juce::Rectangle<float> area, juce::Colour colour,
               float strokeWidth = 1.6f, bool glow = false);
}
