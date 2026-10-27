#include "DockPanels.h"

#include "Parameters.h"
#include "ui/Finish.h"
#include "ui/ParagonLookAndFeel.h"

namespace DockStyle
{
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, area.getY(),
                                                 juce::Colour (0xff09070a), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (ParagonColours::text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (title, area, juce::Justification::centredLeft, false);
    }
}

namespace
{
    void styleButton (juce::TextButton& button, bool primary)
    {
        button.setColour (juce::TextButton::buttonColourId, primary ? ParagonColours::accentDim.withAlpha (0.6f) : juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, primary ? ParagonColours::accent : ParagonColours::text);
    }

    void styleCombo (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff09070a));
        box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box.setColour (juce::ComboBox::textColourId, ParagonColours::text);
        box.setColour (juce::ComboBox::arrowColourId, ParagonColours::text);
    }
}

//==============================================================================
SettingsPage::SettingsPage (ParagonX1AudioProcessor& p)
    : processor (p),
      muteAttachment (p.getParameters(), ParamIDs::tunerMute, muteWhileTuning)
{
    reference.setRange (430.0, 450.0, 1.0);
    reference.setTextValueSuffix (" Hz");
    reference.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    reference.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    reference.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    reference.setColour (juce::Slider::thumbColourId, ParagonColours::accent);
    reference.setDoubleClickReturnValue (true, 440.0);
    reference.onValueChange = [this] { processor.setTunerReferenceHz (reference.getValue()); };
    reference.setTooltip ("The pitch the tuner calls A4. 440 Hz is standard; some bands tune to 432 or 442. Double-click for 440.");

    muteWhileTuning.setColour (juce::ToggleButton::textColourId, ParagonColours::text);

    for (int cc = 1; cc <= 127; ++cc)
        ccBox.addItem ("CC " + juce::String (cc) + (cc == 11 ? "  (Expression, standard)" : cc == 7 ? "  (Volume)" : cc == 4 ? "  (Foot controller)" : ""), cc);
    styleCombo (ccBox);
    ccBox.onChange = [this] { processor.setExpressionCc (ccBox.getSelectedId()); };

    styleButton (openFolder, false);
    openFolder.onClick = [this]
    {
        processor.getRigLibrary().getFolder().createDirectory();
        processor.getRigLibrary().getFolder().startAsProcess();
    };

    pearlWhite.setTooltip ("The original pearl white unit with black keys and knobs");
    pearlWhiteNeon.setTooltip ("The pearl white unit with neon pink-to-orange keys and knobs");
    pearlBlack.setTooltip ("A pearl black unit with neon pink-to-orange keys and knobs");
    pearlWhite.onClick = [] { Finish::set (Finish::Type::pearlWhite); };
    pearlWhiteNeon.onClick = [] { Finish::set (Finish::Type::pearlWhiteNeon); };
    pearlBlack.onClick = [] { Finish::set (Finish::Type::pearlBlack); };
    Finish::changes().addChangeListener (this);
    showFinish();

    for (auto* component : std::initializer_list<juce::Component*> { &reference, &muteWhileTuning, &ccBox, &openFolder, &pearlWhite, &pearlWhiteNeon, &pearlBlack })
        addAndMakeVisible (component);
}

SettingsPage::~SettingsPage()
{
    Finish::changes().removeChangeListener (this);
}

void SettingsPage::showFinish()
{
    // The chosen finish is lit like the dock's primary buttons.
    styleButton (pearlWhite, Finish::get() == Finish::Type::pearlWhite);
    styleButton (pearlWhiteNeon, Finish::get() == Finish::Type::pearlWhiteNeon);
    styleButton (pearlBlack, Finish::get() == Finish::Type::pearlBlack);
    repaint();
}

void SettingsPage::visibilityChanged()
{
    // Settings can change from a loaded project, so read them each time the page opens.
    reference.setValue (processor.getTunerReferenceHz(), juce::dontSendNotification);
    ccBox.setSelectedId (processor.getExpressionCc(), juce::dontSendNotification);
}

void SettingsPage::paint (juce::Graphics& g)
{
    const auto section = [&g] (juce::Rectangle<int> area, const juce::String& title, const juce::String& body)
    {
        DockStyle::drawPanel (g, area.toFloat());
        auto inner = area.reduced (24, 18);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title, inner.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (body, inner.removeFromTop (60), juce::Justification::topLeft, 4, 1.0f);
    };

    section (tunerArea, "TUNER", "Hold TAP or press TUNER on the pedal to tune. Reference pitch for A4:");
    section (expressionArea, "EXPRESSION PEDAL",
             "A MIDI expression pedal moves the on-screen pedal. Pick the MIDI controller it sends "
             "(most send CC 11), and route the MIDI track to Paragon PR7-V in your DAW.");
    section (finishArea, "FINISH",
             "The unit's colour: pearl white with black keys, or pearl white or pearl black with neon pink-to-orange keys. "
             "Saved on this computer, so every Paragon PR7-V you open uses it.");
    section (rigsArea, "RIGS", "Your rigs are files in this folder, so you can back them up or share them:\n"
                               + processor.getRigLibrary().getFolder().getFullPathName());
    section (aboutArea, "ABOUT",
             juce::String ("Paragon PR7-V v") + JucePlugin_VersionString
                 + ". Free and open source under the GNU AGPLv3.\n"
                   "Built with JUCE (AGPLv3) and the Inter typeface (SIL OFL).");
}

void SettingsPage::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (area.getHeight() / 2 - 6);
    area.removeFromTop (12);
    const auto third = (top.getWidth() - 24) / 3;
    tunerArea = top.removeFromLeft (third);
    expressionArea = top.withTrimmedLeft (12).removeFromLeft (third);
    finishArea = top.withTrimmedLeft (24 + third);
    rigsArea = area.removeFromLeft (area.getWidth() / 2 - 6);
    aboutArea = area.withTrimmedLeft (12);

    auto tunerControls = tunerArea.reduced (24, 18).withTrimmedTop (90);
    reference.setBounds (tunerControls.removeFromTop (32).withWidth (juce::jmin (420, tunerControls.getWidth())));
    tunerControls.removeFromTop (12);
    muteWhileTuning.setBounds (tunerControls.removeFromTop (26).withWidth (360));

    ccBox.setBounds (expressionArea.reduced (24, 18).withTrimmedTop (100).removeFromTop (30).withWidth (300));
    openFolder.setBounds (rigsArea.reduced (24, 18).withTrimmedTop (100).removeFromTop (32).withWidth (200));
    auto finishButtons = finishArea.reduced (24, 18).withTrimmedTop (100).removeFromTop (32);
    const auto buttonWidth = juce::jmin (150, (finishButtons.getWidth() - 24) / 3);
    pearlWhite.setBounds (finishButtons.removeFromLeft (buttonWidth));
    pearlWhiteNeon.setBounds (finishButtons.withTrimmedLeft (12).removeFromLeft (buttonWidth));
    pearlBlack.setBounds (finishButtons.withTrimmedLeft (24 + buttonWidth).removeFromLeft (buttonWidth));
}
