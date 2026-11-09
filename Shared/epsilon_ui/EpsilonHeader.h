#pragma once
// The one product mark every Epsilon Sounds plugin draws top-left (design system v1).
//   Muzi:    [hallmark]  MUZI(serif)  PRODUCT          Epsilon: [hallmark]  EPSILON PRODUCT
//                        BY EPSILON SOUNDS · TAGLINE                         TAGLINE
// Colours come from the tier palette: trim for the marks, inkMuted for the second line.
// makeFont(height, bold) lets each plugin use its own embedded face.
#include "EpsilonPalette.h"
#include "EpsilonBrandArt.h"

namespace epsilon_ui
{
template <typename FontFn>
inline float drawProductMark (juce::Graphics& g, juce::Point<float> topLeft, float s, Tier tier,
                              const juce::String& product, const juce::String& tagline, FontFn&& makeFont)
{
    const auto pal = palette (tier);
    const bool small = 30.0f * s < 32.0f;          // the solid small-size cut below 32 px
    const auto x = topLeft.x, y = topLeft.y;
    brand::fillOutline (g, brand::hallmarkPath (small), { x, y, 30.0f * s, 30.0f * s }, juce::RectanglePlacement::centred, pal.trim);
    const float tx = x + 40.0f * s;
    float right = tx;
    g.setColour (pal.trim);
    if (tier == Tier::Muzi)
    {
        const juce::Rectangle<float> wm (tx, y + 1.0f * s, 62.0f * s, 18.0f * s);
        brand::fillOutline (g, brand::muziWordmarkPath(), wm, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop, pal.trim);
        g.setColour (pal.trim);
        g.setFont (makeFont (15.5f * s, true).withExtraKerningFactor (0.28f));
        const auto p = product.toUpperCase();
        const float pw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), p);
        g.drawText (p, juce::Rectangle<float> (wm.getRight() + 9.0f * s, wm.getY() - 1.0f * s, pw + 6.0f * s, wm.getHeight() + 2.0f * s), juce::Justification::centredLeft, false);
        right = wm.getRight() + 9.0f * s + pw;
        g.setColour (pal.inkMuted);
        g.setFont (makeFont (9.5f * s, true).withExtraKerningFactor (0.16f));
        const auto line2 = juce::String::fromUTF8 ("BY EPSILON SOUNDS") + (tagline.isNotEmpty() ? juce::String::fromUTF8 ("  \xc2\xb7  ") + tagline.toUpperCase() : juce::String());
        g.drawText (line2, juce::Rectangle<float> (tx, y + 21.0f * s, 420.0f * s, 12.0f * s), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setFont (makeFont (17.0f * s, true).withExtraKerningFactor (0.22f));
        const auto p = "EPSILON " + product.toUpperCase();
        const float pw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), p);
        g.drawText (p, juce::Rectangle<float> (tx, y - 1.0f * s, pw + 6.0f * s, 21.0f * s), juce::Justification::centredLeft, false);
        right = tx + pw;
        if (tagline.isNotEmpty())
        {
            g.setColour (pal.inkMuted);
            g.setFont (makeFont (9.5f * s, true).withExtraKerningFactor (0.16f));
            g.drawText (tagline.toUpperCase(), juce::Rectangle<float> (tx, y + 21.0f * s, 420.0f * s, 12.0f * s), juce::Justification::centredLeft, false);
        }
    }
    return right;
}
} // namespace epsilon_ui
