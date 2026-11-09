#pragma once

#include <JuceHeader.h>
#include "../Shared/epsilon_ui/EpsilonHeader.h"

// Epsilon Chop visual system.
// Monochrome graphite chassis, colour reserved for the sample itself (each chop
// owns one colour from the "cut" palette), and one live accent (volt) that only
// lights things that are actually sounding or running.
namespace theme
{
// Epsilon Sounds design system v1 (Epsilon tier). Slice colours stay: they are the content.
inline const epsilon_ui::Palette brand = epsilon_ui::palette (epsilon_ui::Tier::Epsilon);
inline const juce::Colour ink0 = brand.bg;   // window
inline const juce::Colour ink1 = brand.surface;   // panels
inline const juce::Colour ink2 = brand.bg.interpolatedWith (brand.surface, 0.5f);   // wells, cells
inline const juce::Colour ink3 = brand.raised;   // raised controls
inline const juce::Colour ink4 = brand.raised.brighter (0.12f);   // hover
inline const juce::Colour line = brand.line;
inline const juce::Colour lineStrong = brand.line.brighter (0.25f);
inline const juce::Colour text = brand.ink;
inline const juce::Colour dim = brand.inkMuted;
inline const juce::Colour faint = epsilon_ui::faint (brand);
inline const juce::Colour live = brand.live;   // the only accent: playing / running

juce::Colour chopColour(int index);

juce::Font sans(float height, bool bold = false);
juce::Font mono(float height);
juce::String noteName(int midiNote);   // FL Studio naming: 60 = C5

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return sans(11.5f, true); }
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return sans(11.5f, true); }
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return sans(13.0f); }
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
};
}
