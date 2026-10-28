#include "RackView.h"


namespace RackView
{
    namespace
    {
        constexpr auto settingKey = "rackShown";

        // The same settings file as the finish (Finish.cpp).
        juce::PropertiesFile::Options settingsOptions()
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "Paragon PR7-V";
            options.folderName = "Paragon";
            options.filenameSuffix = "settings";
            options.osxLibrarySubFolder = "Application Support";
            return options;
        }

        struct State
        {
            State()
            {
                // Read once, the first time a window opens.
                juce::PropertiesFile file (settingsOptions());
                shown = file.getBoolValue (settingKey, true);
            }

            bool shown = true;
            juce::ChangeBroadcaster broadcaster;
        };

        State& state()
        {
            // Shared by every PR7-V window open in the DAW (they're in one
            // process), and only touched on the message thread.
            static State instance;
            return instance;
        }
    }

    bool isShown() { return state().shown; }

    void setShown (bool shown, bool remember)
    {
        if (remember)
        {
            juce::PropertiesFile file (settingsOptions());
            file.setValue (settingKey, shown);
            file.saveIfNeeded();
        }
        if (state().shown != shown)
        {
            state().shown = shown;
            state().broadcaster.sendSynchronousChangeMessage();
        }
    }

    juce::ChangeBroadcaster& changes() { return state().broadcaster; }
}
