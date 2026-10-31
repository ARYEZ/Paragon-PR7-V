#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Whether the rack (the strip under the floor unit with the selected block's
// display and controls) is shown, or hidden to keep the window small and
// clean. HIDE RACK / SHOW RACK on the SIGNAL CHAIN strip switches it, and
// clicking a block in the chain shows it again.
//
// Like the finish, it's a preference of the person, not part of a project or
// a rig: kept in the settings file, so every PR7-V opened afterwards starts
// the same way, and every open window follows when it changes.
namespace RackView
{
    bool isShown();
    // `remember` saves it to the settings file (off for tests and snapshots).
    void setShown (bool shown, bool remember = true);

    // Told whenever it changes, to re-lay-out and resize the window.
    juce::ChangeBroadcaster& changes();
}
