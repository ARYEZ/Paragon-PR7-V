#include "SlotWindow.h"

#include "Finish.h"
#include "ParagonLookAndFeel.h"

namespace SlotWindow
{
    namespace
    {
        // A solid triangle printed on the metal; darkens on hover.
        class ArrowButton final : public juce::Button
        {
        public:
            explicit ArrowButton (bool left) : juce::Button (left ? "Previous" : "Next"), pointsLeft (left) {}

            void paintButton (juce::Graphics& g, bool highlighted, bool down) override
            {
                auto area = getLocalBounds().toFloat().withSizeKeepingCentre (10.0f, 16.0f);
                if (down)
                    area = area.translated (0.0f, 1.0f);

                juce::Path triangle;
                if (pointsLeft)
                    triangle.addTriangle (area.getRight(), area.getY(), area.getRight(), area.getBottom(), area.getX(), area.getCentreY());
                else
                    triangle.addTriangle (area.getX(), area.getY(), area.getX(), area.getBottom(), area.getRight(), area.getCentreY());

                // Stamped into the metal, like the lettering.
                if (isEnabled())
                {
                    g.setColour (Finish::stampEdge());
                    g.fillPath (triangle, juce::AffineTransform::translation (0.0f, 1.0f));
                }
                g.setColour (! isEnabled() ? Finish::inkDim().withAlpha (0.4f)
                                           : highlighted ? ParagonColours::glow : Finish::ink());
                g.fillPath (triangle);
            }

        private:
            bool pointsLeft;
        };
    }

    Layout layout (juce::Rectangle<int> bounds)
    {
        Layout l;
        auto area = bounds.reduced (10, 6);
        l.title = area.removeFromTop (22);
        l.corner = l.title.withLeft (l.title.getRight() - 22);
        l.caption = area.removeFromBottom (22);
        area.removeFromBottom (4);

        l.button = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 64, 158), juce::jmin (area.getHeight(), 70));
        l.previous = l.button.withX (l.button.getX() - 28).withWidth (20);
        l.next = l.button.withX (l.button.getRight() + 8).withWidth (20);
        return l;
    }

    void drawTitle (juce::Graphics& g, const Layout& l, const juce::String& title)
    {
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        ParagonLookAndFeel::drawStampedText (g, title, l.title.toFloat(), juce::Justification::centred, Finish::ink());
    }

    void drawRingLed (juce::Graphics& g, juce::Point<float> centre, bool lit)
    {
        const auto ring = juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre);

        // The LED sits in a small chrome-rimmed lens set into the metal.
        const auto lens = ring.expanded (3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a4a52), 0.0f, lens.getY(),
                                                 juce::Colour (0xfff3e9ee), 0.0f, lens.getBottom(), false));
        g.fillEllipse (lens);
        g.setColour (juce::Colour (0xff1a1014));
        g.fillEllipse (lens.reduced (1.2f));
        if (lit)
        {
            g.setColour (ParagonColours::glow.withAlpha (0.3f));
            g.fillEllipse (ring.expanded (4.0f));
            g.setColour (juce::Colour (0xfffff0ec));   // white-hot core, as the mockup's lit LEDs
            g.fillEllipse (ring.reduced (2.2f));
        }
        g.setColour (lit ? ParagonColours::glow : ParagonColours::glow.withAlpha (0.45f));
        g.drawEllipse (ring, 1.6f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));   // the lens's reflection
        g.fillEllipse (lens.reduced (2.5f).withHeight (lens.getHeight() * 0.3f));
    }

    void drawButton (juce::Graphics& g, juce::Rectangle<int> areaInt, bool hover, bool highlight)
    {
        // A plain black piano key in a bezel (chrome, or the neon on pearl
        // black and white + neon), exactly like the footswitches: no icon, the title
        // above and caption below say what it is.
        const auto bezel = areaInt.toFloat();
        ParagonLookAndFeel::drawChromeBezel (g, bezel, 11.0f);
        const auto area = bezel.reduced (7.0f, 6.0f).withTrimmedBottom (1.0f);
        ParagonLookAndFeel::drawPianoKey (g, area, 7.0f, 5.5f, false, hover || highlight);
    }

    void drawCaption (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, bool isError)
    {
        g.setFont (juce::FontOptions (12.5f, isError ? juce::Font::bold : juce::Font::plain));
        g.setColour (Finish::isBlack() ? Finish::stampEdge() : juce::Colours::white.withAlpha (0.6f));   // stamped: a fine edge under the letters
        g.drawFittedText (text, area.translated (0, 1), juce::Justification::centred, 1, 0.7f);
        g.setColour (isError ? (Finish::isBlack() ? ParagonColours::glow : juce::Colour (0xffc8231a)) : Finish::ink());
        g.drawFittedText (text, area, juce::Justification::centred, 1, 0.7f);
    }

    std::unique_ptr<juce::Button> makeArrow (bool pointsLeft)
    {
        return std::make_unique<ArrowButton> (pointsLeft);
    }
}
