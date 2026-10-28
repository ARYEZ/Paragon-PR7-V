#include "Icons.h"

namespace Icons
{
    namespace
    {
        // A pulse line, like a heartbeat: used for compressor and reverb shapes.
        void addPulse (juce::Path& p, std::initializer_list<juce::Point<float>> points)
        {
            bool first = true;
            for (auto point : points)
            {
                if (first)
                    p.startNewSubPath (point);
                else
                    p.lineTo (point);
                first = false;
            }
        }
    }

    juce::Path get (Type type)
    {
        juce::Path p;

        switch (type)
        {
            case Type::amp:
                // Amp head: cabinet outline, grille slots, a row of knobs.
                p.addRoundedRectangle (0.05f, 0.22f, 0.9f, 0.56f, 0.06f);
                p.addRectangle (0.12f, 0.32f, 0.76f, 0.001f);
                for (int i = 0; i < 6; ++i)
                    p.addRectangle (0.16f + (float) i * 0.12f, 0.40f, 0.06f, 0.16f);
                for (int i = 0; i < 5; ++i)
                    p.addEllipse (0.2f + (float) i * 0.14f, 0.62f, 0.05f, 0.05f);
                break;

            case Type::cabinet:
                p.addRoundedRectangle (0.12f, 0.08f, 0.76f, 0.84f, 0.06f);
                p.addEllipse (0.24f, 0.2f, 0.52f, 0.52f);
                p.addEllipse (0.42f, 0.38f, 0.16f, 0.16f);
                p.addEllipse (0.46f, 0.78f, 0.08f, 0.08f);
                break;

            case Type::rig:
                // An isometric cube.
                p.startNewSubPath (0.5f, 0.08f);
                p.lineTo (0.88f, 0.28f);
                p.lineTo (0.88f, 0.72f);
                p.lineTo (0.5f, 0.92f);
                p.lineTo (0.12f, 0.72f);
                p.lineTo (0.12f, 0.28f);
                p.closeSubPath();
                p.startNewSubPath (0.12f, 0.28f);
                p.lineTo (0.5f, 0.48f);
                p.lineTo (0.88f, 0.28f);
                p.startNewSubPath (0.5f, 0.48f);
                p.lineTo (0.5f, 0.92f);
                break;

            case Type::comp:
                addPulse (p, { { 0.0f, 0.5f }, { 0.3f, 0.5f }, { 0.38f, 0.3f }, { 0.46f, 0.82f },
                               { 0.56f, 0.12f }, { 0.64f, 0.66f }, { 0.7f, 0.5f }, { 1.0f, 0.5f } });
                break;

            case Type::drive:
                // A stompbox: body, two knobs, footswitch.
                p.addRoundedRectangle (0.22f, 0.06f, 0.56f, 0.88f, 0.08f);
                p.addEllipse (0.32f, 0.16f, 0.12f, 0.12f);
                p.addEllipse (0.56f, 0.16f, 0.12f, 0.12f);
                p.addEllipse (0.4f, 0.6f, 0.2f, 0.2f);
                break;

            case Type::modulation:
                p.startNewSubPath (0.0f, 0.5f);
                for (int i = 1; i <= 40; ++i)
                {
                    const auto x = (float) i / 40.0f;
                    p.lineTo (x, 0.5f - 0.36f * std::sin (x * juce::MathConstants<float>::twoPi * 2.0f));
                }
                break;

            case Type::delay:
                // Repeats fading round a circle.
                for (int i = 0; i < 9; ++i)
                {
                    const auto angle = (float) i / 9.0f * juce::MathConstants<float>::twoPi * 0.85f;
                    const auto r = 0.05f + 0.06f * (float) (9 - i) / 9.0f;
                    p.addEllipse (0.5f + 0.38f * std::sin (angle) - r, 0.5f - 0.38f * std::cos (angle) - r, r * 2.0f, r * 2.0f);
                }
                break;

            case Type::reverb:
                addPulse (p, { { 0.0f, 0.5f }, { 0.2f, 0.5f }, { 0.26f, 0.05f }, { 0.32f, 0.95f }, { 0.4f, 0.15f },
                               { 0.48f, 0.85f }, { 0.56f, 0.28f }, { 0.64f, 0.72f }, { 0.72f, 0.4f }, { 0.8f, 0.6f },
                               { 0.86f, 0.5f }, { 1.0f, 0.5f } });
                break;

            case Type::guitar:
                // Body and neck of an electric guitar, angled.
                p.addEllipse (0.06f, 0.52f, 0.3f, 0.3f);
                p.addEllipse (0.2f, 0.42f, 0.24f, 0.24f);
                p.startNewSubPath (0.34f, 0.56f);
                p.lineTo (0.86f, 0.1f);
                p.addRectangle (0.8f, 0.04f, 0.14f, 0.1f);
                break;

            case Type::deEsser:
                // An "S" with an arrow pointing down beside it: esses turned down.
                p.startNewSubPath (0.6f, 0.2f);
                p.cubicTo (0.5f, 0.06f, 0.1f, 0.1f, 0.16f, 0.33f);
                p.cubicTo (0.22f, 0.52f, 0.58f, 0.46f, 0.6f, 0.68f);
                p.cubicTo (0.62f, 0.92f, 0.2f, 0.94f, 0.1f, 0.78f);
                p.startNewSubPath (0.86f, 0.22f);
                p.lineTo (0.86f, 0.82f);
                p.startNewSubPath (0.76f, 0.7f);
                p.lineTo (0.86f, 0.82f);
                p.lineTo (0.96f, 0.7f);
                break;

            case Type::microphone:
            {
                // A handheld vocal mic, angled like a singer holds it: the
                // round grille with its mesh, a band, then the tapering handle.
                p.addEllipse (0.52f, 0.06f, 0.4f, 0.4f);
                p.startNewSubPath (0.6f, 0.18f);
                p.lineTo (0.8f, 0.38f);
                p.startNewSubPath (0.56f, 0.3f);
                p.lineTo (0.68f, 0.42f);
                juce::Path handle;
                handle.startNewSubPath (0.55f, 0.37f);
                handle.lineTo (0.61f, 0.43f);
                handle.lineTo (0.14f, 0.94f);
                handle.lineTo (0.06f, 0.86f);
                handle.closeSubPath();
                p.addPath (handle);
                break;
            }

            case Type::speaker:
                p.startNewSubPath (0.08f, 0.38f);
                p.lineTo (0.24f, 0.38f);
                p.lineTo (0.44f, 0.18f);
                p.lineTo (0.44f, 0.82f);
                p.lineTo (0.24f, 0.62f);
                p.lineTo (0.08f, 0.62f);
                p.closeSubPath();
                p.addCentredArc (0.44f, 0.5f, 0.18f, 0.18f, 0.0f, 0.5f, 2.64f, true);
                p.addCentredArc (0.44f, 0.5f, 0.32f, 0.32f, 0.0f, 0.55f, 2.6f, true);
                p.addCentredArc (0.44f, 0.5f, 0.46f, 0.46f, 0.0f, 0.6f, 2.55f, true);
                break;

            case Type::processor:
                // Three effect blocks joined by a cable, as on the mockup's PROCESSOR tab.
                p.addRoundedRectangle (0.04f, 0.1f, 0.3f, 0.3f, 0.06f);
                p.addRoundedRectangle (0.66f, 0.1f, 0.3f, 0.3f, 0.06f);
                p.addRoundedRectangle (0.35f, 0.6f, 0.3f, 0.3f, 0.06f);
                p.startNewSubPath (0.34f, 0.25f);
                p.lineTo (0.66f, 0.25f);
                p.startNewSubPath (0.19f, 0.4f);
                p.quadraticTo (0.19f, 0.75f, 0.35f, 0.75f);
                break;

            case Type::presets:
                // A 2 x 2 grid of rounded tiles: a library.
                for (int i = 0; i < 4; ++i)
                    p.addRoundedRectangle (0.08f + 0.46f * (float) (i % 2), 0.08f + 0.46f * (float) (i / 2), 0.38f, 0.38f, 0.1f);
                break;

            case Type::undo:
            case Type::redo:
            {
                // A curved arrow turning back on itself; redo is its mirror image.
                p.startNewSubPath (0.2f, 0.36f);
                p.lineTo (0.62f, 0.36f);
                p.cubicTo (0.98f, 0.36f, 0.98f, 0.86f, 0.62f, 0.86f);
                p.lineTo (0.36f, 0.86f);
                p.startNewSubPath (0.36f, 0.18f);
                p.lineTo (0.18f, 0.36f);
                p.lineTo (0.36f, 0.54f);
                if (type == Type::redo)
                    p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 0.5f, 0.5f));
                break;
            }

            case Type::tap:
                // A square with a dot: the mockup's TAP icon.
                p.addRoundedRectangle (0.14f, 0.14f, 0.72f, 0.72f, 0.12f);
                p.addEllipse (0.4f, 0.4f, 0.2f, 0.2f);
                break;

            case Type::tuner:
                // A tuning-meter arc with its needle.
                p.addCentredArc (0.5f, 0.78f, 0.44f, 0.44f, 0.0f, -1.2f, 1.2f, true);
                p.startNewSubPath (0.5f, 0.78f);
                p.lineTo (0.62f, 0.4f);
                p.addEllipse (0.45f, 0.73f, 0.1f, 0.1f);
                break;

            case Type::volume:
                // Rising bars, as on the mockup's VOLUME button.
                for (int i = 0; i < 4; ++i)
                {
                    const auto height = 0.22f + 0.2f * (float) i;
                    p.addRectangle (0.12f + 0.21f * (float) i, 0.88f - height, 0.1f, height);
                }
                break;

            case Type::wah:
                // A resonant peak sweeping across: a filter's response.
                p.startNewSubPath (0.0f, 0.72f);
                p.cubicTo (0.3f, 0.72f, 0.36f, 0.1f, 0.48f, 0.1f);
                p.cubicTo (0.6f, 0.1f, 0.62f, 0.86f, 1.0f, 0.86f);
                p.startNewSubPath (0.26f, 0.94f);
                p.lineTo (0.74f, 0.94f);
                p.startNewSubPath (0.62f, 0.88f);
                p.lineTo (0.74f, 0.94f);
                p.lineTo (0.62f, 1.0f);
                break;

            case Type::save:
                // A floppy disk: body with a clipped corner, label and shutter.
                p.startNewSubPath (0.12f, 0.12f);
                p.lineTo (0.72f, 0.12f);
                p.lineTo (0.88f, 0.28f);
                p.lineTo (0.88f, 0.88f);
                p.lineTo (0.12f, 0.88f);
                p.closeSubPath();
                p.addRectangle (0.28f, 0.12f, 0.38f, 0.22f);
                p.addRoundedRectangle (0.26f, 0.54f, 0.48f, 0.34f, 0.04f);
                break;

            case Type::settings:
            {
                // A cog: eight teeth round a ring, with a hole in the middle.
                constexpr int teeth = 8;
                for (int i = 0; i < teeth * 2; ++i)
                {
                    const auto angle = (float) i / (float) (teeth * 2) * juce::MathConstants<float>::twoPi;
                    const auto radius = (i % 2 == 0) ? 0.46f : 0.34f;
                    const auto next = (float) (i + 1) / (float) (teeth * 2) * juce::MathConstants<float>::twoPi;
                    const auto a = juce::Point<float> (0.5f + radius * std::sin (angle), 0.5f - radius * std::cos (angle));
                    const auto b = juce::Point<float> (0.5f + radius * std::sin (next), 0.5f - radius * std::cos (next));
                    if (i == 0)
                        p.startNewSubPath (a);
                    else
                        p.lineTo (a);
                    p.lineTo (b);
                }
                p.closeSubPath();
                p.addEllipse (0.36f, 0.36f, 0.28f, 0.28f);
                break;
            }

            case Type::star:
                p.addStar ({ 0.5f, 0.54f }, 5, 0.2f, 0.46f);
                break;

            case Type::user:
                p.addEllipse (0.34f, 0.1f, 0.32f, 0.32f);
                p.startNewSubPath (0.14f, 0.9f);
                p.cubicTo (0.14f, 0.5f, 0.86f, 0.5f, 0.86f, 0.9f);
                p.closeSubPath();
                break;

            case Type::factory:
                // A building with a saw-tooth roof.
                p.startNewSubPath (0.1f, 0.9f);
                p.lineTo (0.1f, 0.4f);
                p.lineTo (0.36f, 0.56f);
                p.lineTo (0.36f, 0.4f);
                p.lineTo (0.62f, 0.56f);
                p.lineTo (0.62f, 0.12f);
                p.lineTo (0.88f, 0.12f);
                p.lineTo (0.88f, 0.9f);
                p.closeSubPath();
                p.addRectangle (0.24f, 0.7f, 0.12f, 0.1f);
                p.addRectangle (0.5f, 0.7f, 0.12f, 0.1f);
                break;

            case Type::plus:
                p.startNewSubPath (0.5f, 0.15f);
                p.lineTo (0.5f, 0.85f);
                p.startNewSubPath (0.15f, 0.5f);
                p.lineTo (0.85f, 0.5f);
                break;

            case Type::chevronLeft:
            case Type::chevronRight:
                p.startNewSubPath (0.65f, 0.15f);
                p.lineTo (0.32f, 0.5f);
                p.lineTo (0.65f, 0.85f);
                if (type == Type::chevronRight)
                    p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 0.5f, 0.5f));
                break;

            case Type::eq:
            {
                // Three faders at different heights: a graphic EQ.
                const float xs[] = { 0.2f, 0.5f, 0.8f }, knobs[] = { 0.62f, 0.3f, 0.5f };
                for (int i = 0; i < 3; ++i)
                {
                    p.startNewSubPath (xs[i], 0.08f);
                    p.lineTo (xs[i], 0.92f);
                    p.addRoundedRectangle (xs[i] - 0.1f, knobs[i] - 0.06f, 0.2f, 0.12f, 0.03f);
                }
                break;
            }

            case Type::filterPitch:
                // A resonant filter's peak, and an up / down arrow for pitch.
                p.startNewSubPath (0.04f, 0.72f);
                p.cubicTo (0.25f, 0.72f, 0.3f, 0.18f, 0.42f, 0.18f);
                p.cubicTo (0.52f, 0.18f, 0.55f, 0.85f, 0.66f, 0.92f);
                p.startNewSubPath (0.84f, 0.12f);
                p.lineTo (0.84f, 0.62f);
                p.startNewSubPath (0.74f, 0.24f);
                p.lineTo (0.84f, 0.12f);
                p.lineTo (0.94f, 0.24f);
                p.startNewSubPath (0.74f, 0.5f);
                p.lineTo (0.84f, 0.62f);
                p.lineTo (0.94f, 0.5f);
                break;

            case Type::loFi:
            {
                // An old transistor radio: the case, its round speaker grille and the antenna.
                p.addRoundedRectangle (0.06f, 0.34f, 0.88f, 0.58f, 0.08f);
                p.addEllipse (0.16f, 0.45f, 0.36f, 0.36f);
                p.startNewSubPath (0.62f, 0.52f);
                p.lineTo (0.84f, 0.52f);
                p.startNewSubPath (0.62f, 0.66f);
                p.lineTo (0.84f, 0.66f);
                p.startNewSubPath (0.62f, 0.8f);
                p.lineTo (0.84f, 0.8f);
                p.startNewSubPath (0.3f, 0.34f);
                p.lineTo (0.78f, 0.06f);
                break;
            }

            case Type::vocoder:
            {
                // A voice's bands: a row of bars rising and falling like a
                // spoken vowel, under a small wave (the synth it shapes).
                constexpr float heights[] = { 0.3f, 0.55f, 0.8f, 0.62f, 0.4f, 0.7f, 0.5f, 0.28f };
                for (int i = 0; i < 8; ++i)
                    p.addRoundedRectangle (0.04f + (float) i * 0.12f, 0.96f - heights[i] * 0.7f, 0.07f, heights[i] * 0.7f, 0.03f);
                p.startNewSubPath (0.04f, 0.12f);
                for (int i = 1; i <= 24; ++i)
                {
                    const auto t = (float) i / 24.0f;
                    p.lineTo (0.04f + 0.92f * t, 0.12f + 0.08f * std::sin (t * juce::MathConstants<float>::twoPi * 3.0f));
                }
                break;
            }

            case Type::doubler:
                // Two takes of the same phrase, the second a little late and
                // lower: the same wave twice, offset.
                for (const auto& [dx, dy] : { std::pair { 0.0f, 0.0f }, std::pair { 0.14f, 0.2f } })
                {
                    p.startNewSubPath (dx, 0.4f + dy);
                    for (int i = 1; i <= 30; ++i)
                    {
                        const auto t = (float) i / 30.0f;
                        p.lineTo (dx + 0.86f * t, 0.4f + dy - 0.26f * std::sin (t * juce::MathConstants<float>::twoPi * 1.5f));
                    }
                }
                break;

            case Type::pitch:
            {
                // A tuning fork: two tines on a stem, ringing.
                p.startNewSubPath (0.38f, 0.06f);
                p.lineTo (0.38f, 0.48f);
                p.quadraticTo (0.38f, 0.62f, 0.5f, 0.62f);
                p.quadraticTo (0.62f, 0.62f, 0.62f, 0.48f);
                p.lineTo (0.62f, 0.06f);
                p.startNewSubPath (0.5f, 0.62f);
                p.lineTo (0.5f, 0.94f);
                p.startNewSubPath (0.2f, 0.16f);
                p.quadraticTo (0.12f, 0.27f, 0.2f, 0.38f);
                p.startNewSubPath (0.8f, 0.16f);
                p.quadraticTo (0.88f, 0.27f, 0.8f, 0.38f);
                break;
            }

            case Type::glitch:
            {
                // A waveform chopped up: a smooth start, then square stutter
                // steps that jump about, as GLITCH cuts the sound into pieces.
                p.startNewSubPath (0.04f, 0.5f);
                p.quadraticTo (0.12f, 0.22f, 0.2f, 0.5f);
                p.lineTo (0.28f, 0.5f);
                p.lineTo (0.28f, 0.2f);
                p.lineTo (0.4f, 0.2f);
                p.lineTo (0.4f, 0.74f);
                p.lineTo (0.48f, 0.74f);
                p.lineTo (0.48f, 0.36f);
                p.lineTo (0.58f, 0.36f);
                p.lineTo (0.58f, 0.86f);
                p.lineTo (0.66f, 0.86f);
                p.lineTo (0.66f, 0.12f);
                p.lineTo (0.74f, 0.12f);
                p.lineTo (0.74f, 0.6f);
                p.lineTo (0.84f, 0.6f);
                p.lineTo (0.84f, 0.42f);
                p.lineTo (0.96f, 0.42f);
                break;
            }

            case Type::flame:
                // The mockup's "All Rigs" icon.
                p.startNewSubPath (0.5f, 0.05f);
                p.cubicTo (0.95f, 0.4f, 0.85f, 0.95f, 0.5f, 0.95f);
                p.cubicTo (0.15f, 0.95f, 0.05f, 0.55f, 0.35f, 0.3f);
                p.cubicTo (0.35f, 0.5f, 0.45f, 0.55f, 0.5f, 0.55f);
                p.cubicTo (0.55f, 0.4f, 0.5f, 0.2f, 0.5f, 0.05f);
                p.closeSubPath();
                break;
        }

        return p;
    }

    void draw (juce::Graphics& g, Type type, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth, bool glow)
    {
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        auto path = get (type);
        path.applyTransform (juce::AffineTransform::scale (size).translated (area.getCentreX() - size / 2.0f,
                                                                                  area.getCentreY() - size / 2.0f));

        if (glow)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.strokePath (path, juce::PathStrokeType (strokeWidth * 3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
