#pragma once

#include <array>

// Starting points for the graphic EQ. Choosing one sets the faders and LEVEL;
// from there you tweak as usual. LEVEL makes up for the loudness the curve
// adds or takes away, so switching between presets compares tone, not volume.
//
// VOCAL (0.15.1): the moves engineers most often make on a voice with an
// octave EQ, measured over a voice's range (100 Hz to 8 kHz, its esses
// included). They're our own curves from common practice, not copies of any
// product's presets.
// INSTRUMENT: PR7-G's guitar curves (after an amp and cab, measured over a
// guitar's range, 80 Hz to 5 kHz), kept for anyone running other sounds
// through PR7-V. Some (Radio, Mud Cut) suit a voice too.
namespace EqPresets
{
    struct Preset
    {
        const char* name;
        const char* description;                 // the menu's tooltip
        std::array<float, 9> gainsDb;            // 63, 125, 250, 500, 1k, 2k, 4k, 8k, 16k
        float levelDb;
        bool vocal = false;                      // in the VOCAL group (else INSTRUMENT)
    };

    // The menu's order: the vocal ones first. Only ever add: a rig stores the
    // faders, not the preset, so reordering is safe, but tests name a few.
    inline constexpr std::array<Preset, 25> all { {
        { "Vocal Clarity",      "Rumble out, a little mud cut, presence and air: a clear all-round voice.",
          { -6.0f, -3.0f, -2.0f, 0.0f, 0.0f, 1.0f, 3.0f, 2.0f, 2.0f }, -0.6f, true },
        { "Air & Shine",        "A lift at the very top for a breathy, expensive pop sheen.",
          { -3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 3.0f, 5.0f }, -0.5f, true },
        { "Presence Push",      "Upper mids forward so the voice cuts through a busy band.",
          { -4.0f, -2.0f, -1.0f, 0.0f, 1.0f, 3.0f, 4.0f, 1.0f, 0.0f }, -1.8f, true },
        { "Boxy Cut",           "Takes out the cardboard-box sound of a small room or a close mic (250-500 Hz).",
          { 0.0f, 0.0f, -2.0f, -4.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 1.4f, true },
        { "Nasal Fix",          "Dips around 1 kHz, where a voice can sound honky or nasal.",
          { 0.0f, 0.0f, 0.0f, 0.0f, -4.0f, -1.0f, 0.0f, 0.0f, 0.0f }, 1.0f, true },
        { "Harsh Tamer",        "Softens the hard, piercing 2-8 kHz edge (and some sibilance).",
          { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f, -4.0f, -3.0f, 0.0f }, 1.3f, true },
        { "Warm & Close",       "Chest and warmth up, the top softened: intimate, close to the mic.",
          { 0.0f, 2.0f, 2.0f, 0.0f, -1.0f, -1.0f, 0.0f, -1.0f, -2.0f }, -0.4f, true },
        { "Deep Voice",         "For lower voices: the rumble tightened, the chest kept, clarity added.",
          { -4.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f }, -0.8f, true },
        { "Bright Voice",       "For higher voices: lows thinned, a smooth lift up top.",
          { -6.0f, -3.0f, -1.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f, 3.0f }, -0.6f, true },
        { "Rap Upfront",        "Punchy and forward: low mids cleared, consonants and bite pushed.",
          { -4.0f, 0.0f, -2.0f, -1.0f, 1.0f, 2.0f, 3.0f, 2.0f, 1.0f }, -1.2f, true },
        { "Spoken Word",        "Podcast and voice-over: no rumble or boom, easy to understand, never harsh.",
          { -8.0f, -2.0f, 0.0f, -1.0f, 0.0f, 1.0f, 2.0f, 0.0f, -2.0f }, 0.0f, true },
        { "Backing Vocals",     "Thinner and softer on top, so harmonies sit behind the lead.",
          { -8.0f, -6.0f, -3.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f }, 1.8f, true },
        { "Telephone",          "Only the middle of the voice (about 300 Hz to 3 kHz), like a phone line.",
          { -12.0f, -12.0f, -6.0f, 2.0f, 4.0f, 3.0f, -4.0f, -12.0f, -12.0f }, 0.5f, true },

        { "Metal Scoop",        "The classic V: big lows and highs, mids pulled right down.",
          { 3.0f, 2.0f, 0.0f, -4.0f, -6.0f, -3.0f, 2.0f, 3.0f, 1.0f }, 1.0f },
        { "Tight Low End",      "Cuts the boom under high gain so palm mutes stay tight; adds bite.",
          { -6.0f, -4.0f, -1.0f, 0.0f, 1.0f, 2.0f, 2.0f, 1.0f, 0.0f }, -0.5f },
        { "Mud Cut",            "Clears the boxy, muddy low mids without thinning the tone.",
          { 0.0f, -1.0f, -4.0f, -3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 1.5f },
        { "Fizz Tamer",         "Rolls off the harsh top end that high-gain captures can have.",
          { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f, -6.0f, -9.0f }, 0.5f },
        { "Lead Mid Boost",     "Pushes the mids so solos sit on top of the band.",
          { -2.0f, -1.0f, 0.0f, 3.0f, 5.0f, 3.0f, 0.0f, -1.0f, -2.0f }, -3.0f },
        { "Cut Through the Mix", "Less low end, more upper mids: heard clearly in a dense band.",
          { -4.0f, -2.0f, -2.0f, 0.0f, 2.0f, 4.0f, 3.0f, 0.0f, -3.0f }, -1.5f },
        { "Fat Rhythm",         "Thick, full lows and low mids for big rhythm parts.",
          { 3.0f, 4.0f, 3.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f }, -2.5f },
        { "British Crunch",     "A forward upper-mid bark, like a cranked British stack.",
          { 0.0f, 1.0f, 0.0f, -1.0f, 2.0f, 3.0f, 1.0f, 0.0f, -1.0f }, -1.5f },
        { "Bright Clean",       "Sparkle and air for clean and chorus tones.",
          { -2.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 3.0f, 4.0f, 3.0f }, -1.0f },
        { "Funk Scoop",         "Scooped low mids and a crisp top for clean rhythm.",
          { 0.0f, 1.0f, -1.0f, -3.0f, -2.0f, 0.0f, 3.0f, 4.0f, 2.0f }, 0.0f },
        { "Warm Jazz",          "Round and dark: the highs rolled off, the lows full.",
          { 2.0f, 3.0f, 2.0f, 1.0f, 0.0f, -2.0f, -4.0f, -6.0f, -8.0f }, -1.0f },
        { "Radio",              "A narrow mid band, like an old AM radio or a phone.",
          { -12.0f, -12.0f, -8.0f, 2.0f, 6.0f, 3.0f, -6.0f, -12.0f, -12.0f }, -0.5f },
    } };
}
