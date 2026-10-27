#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"

// The FACEPLATE: the selected block as a small unit of the PR7 line, beside
// its controls on the PROCESSOR page (where the RIG BROWSER and SELECTED
// RIG panels were). Built only from the PR7's own parts, in the current
// finish: the unit's pearl chassis, its LCD screens, big knobs, small keys
// with light stripes, EQ faders, neon LED bars, and lettering stamped in
// ink or printed in red. It follows the block's TYPE and shows what the
// block is doing, live, on its screens:
//
//   DYNAMICS    Classic: a gain-reduction needle, THRESHOLD/RATIO/LEVEL.
//               Daybreak: COMP/DETAIL/MIX knobs, INTENSITY key, a meter.
//               Revival: GATE/COMPRESSION/GAIN as one curve, faders, a meter.
//   SATURATION  MARS: a tube per band, glowing as hard as it's driven.
//   DELAY       its TYPE as the name; the repeats laid out in time, fading
//               by FEEDBACK, a light running along them at the delay time;
//               TIME in seven-segment digits; FEEDBACK and MIX knobs.
//   REVERB      its TYPE as the name; the reverb's shape (pre-delay, early
//               reflections, the tail as long as it decays); DECAY in
//               seconds; SIZE and MIX knobs.
//   PITCH       HARMONY: the note sung (and TUNE VOICE's target), the
//               voices by interval and pan, keys for HARMONY, OCTAVE, FREEZE.
//   MODULATION  its TYPE; the wobble as a wave at RATE and DEPTH, a dot
//               riding it; RATE in digits; DEPTH and MIX knobs.
//   FILTER /    its TYPE; the filter's peak, the interval, the program or
//   PITCH       the talk box's setting; its knobs.
//   DE-ESSER    how much it's taking away (live), FREQUENCY, AMOUNT, LISTEN.
//   GLITCH      its MODE; a 16-step pattern as dense as DENSITY, a light
//               stepping at LENGTH; LENGTH in digits; DENSITY and MIX.
//   DOUBLER     the four voices by pan and delay; a key per voice.
//   GRAPHIC EQ  the EQ's own response curve; LEVEL.
//   LO-FI       its TYPE; a wave showing AMOUNT and NOISE; AMOUNT and MIX.
//   (the generic unit, name, icon and TYPE, is kept for any block added later)
class Faceplate final : public juce::Component,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit Faceplate (ParagonX1AudioProcessor&);
    ~Faceplate() override;

    // Which kind of block: the ones with their own faceplates, or any other.
    enum class Unit { dynamics, saturation, delay, reverb, pitch, modulation, filterPitch, deEsser, glitch, doubler, eq, loFi, other };
    // What's drawn now (for tests).
    enum class Look { generic, classic, daybreak, revival, mars, delay, reverb, harmony,
                      modulation, filterPitch, deEsser, glitch, doubler, eq, loFi };

    // `onIds`: the block's on switches (comma separated); `typeId`: its TYPE
    // (or empty).
    void setBlock (Unit unit, const juce::String& title, Icons::Type icon, juce::Colour colour,
                   const juce::String& onIds, const juce::String& typeId);
    Look getLook() const;

    // The rack: one wide unit holding the block's display on the left (this
    // wide) and its real controls (the block's EffectPanel, in rack mode) on
    // the rest of its face, at getControlArea(). 0: the faceplate alone.
    void setRackDisplayWidth (float width);
    bool isRack() const noexcept { return rackDisplayWidth > 0.0f; }
    juce::Rectangle<int> getControlArea() const;

    // Moves the meters one step towards the plugin's readings (the timer
    // does it 30 times a second; tests and screenshots call it directly).
    void updateMeters();
    float getShownReduction() const noexcept { return shownReduction; }
    float getShownDeEss() const noexcept { return shownDeEss; }
    float getShownDrive (int band) const noexcept { return shownDrive[(size_t) juce::jlimit (0, 3, band)]; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // the finish changed
    float raw (const juce::String& id) const;
    float normalised (const juce::String& id) const;
    juce::String text (const juce::String& id) const;
    bool isOn() const;

    // Each draws its name in `header` and the rest in `body`.
    void paintGeneric (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintClassic (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintDaybreak (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintRevival (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintMars (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintDelay (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintReverb (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintHarmony (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintModulation (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintFilterPitch (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintDeEsser (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintGlitch (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintDoubler (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintEq (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintLoFi (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);

    ParagonX1AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;
    Unit unit = Unit::other;
    juce::String title, typeId;
    juce::StringArray onIds;
    Icons::Type icon = Icons::Type::eq;
    juce::Colour colour;   // the block's colour in the chain (the faders' light)
    juce::Image satin;     // the pearl's grain, as on the unit
    float rackDisplayWidth = 0.0f;

    // Meters, with a little ballistics so they move like real ones.
    float shownReduction = 0.0f;                 // dB, 0 or less
    float shownDeEss = 0.0f;                     // the de-esser's, dB, 0 or less
    std::array<float, 4> shownDrive {};          // MARS bands, 0 to 1+
};
