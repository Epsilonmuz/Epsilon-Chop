#pragma once

#include <JuceHeader.h>

// Epsilon Chop visual system.
// Monochrome graphite chassis, colour reserved for the sample itself (each chop
// owns one colour from the "cut" palette), and one live accent (volt) that only
// lights things that are actually sounding or running.
namespace theme
{
inline const juce::Colour ink0 { 0xff0a0b0e };   // window
inline const juce::Colour ink1 { 0xff111318 };   // panels
inline const juce::Colour ink2 { 0xff181b22 };   // wells, cells
inline const juce::Colour ink3 { 0xff21252e };   // raised controls
inline const juce::Colour ink4 { 0xff2c313c };   // hover
inline const juce::Colour line { 0xff272b35 };
inline const juce::Colour lineStrong { 0xff3a404d };
inline const juce::Colour text { 0xffeef0f5 };
inline const juce::Colour dim { 0xff9097a7 };
inline const juce::Colour faint { 0xff5d6371 };
inline const juce::Colour live { 0xffdcff4f };   // the only accent: playing / running

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
