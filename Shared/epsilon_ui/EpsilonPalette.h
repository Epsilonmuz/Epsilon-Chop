#pragma once
// epsilon_ui palette — the Epsilon Sounds colours and sizes (design system v1), no font dependency.
// Include this alone to recolour an existing UI; EpsilonUI.h builds the shared controls on top.
#include <juce_gui_basics/juce_gui_basics.h>

namespace epsilon_ui
{
enum class Tier { Muzi, Epsilon };

struct Palette
{
    juce::Colour bg, surface, raised, line, ink, inkMuted, trim, live, liveInk, meterOk, meterHot, meterClip, focus;
};

inline Palette palette(Tier t)
{
    const auto c = [](juce::uint32 rgb) { return juce::Colour(0xff000000u | rgb); };
    Palette p;
    if (t == Tier::Muzi)
    {
        p.bg = c(0x0a0907); p.surface = c(0x15130f); p.raised = c(0x1e1b15); p.line = c(0x332d23);
        p.ink = c(0xefe6d2); p.inkMuted = c(0xa39780); p.trim = c(0xc9a55c); p.live = c(0xe9cb84); p.liveInk = c(0x1a1408);
    }
    else
    {
        p.bg = c(0x16171a); p.surface = c(0x1f2125); p.raised = c(0x292c31); p.line = c(0x3b3f46);
        p.ink = c(0xf2f3f5); p.inkMuted = c(0xa2a7af); p.trim = c(0xb9bec6); p.live = c(0xe4d3a3); p.liveInk = c(0x16171a);
    }
    p.meterOk = c(0x9cc08a); p.meterHot = c(0xe3b45a); p.meterClip = c(0xe0604f); p.focus = c(0xf6e2b0);
    return p;
}

// Spacing / radius / sizes from tokens.json
namespace size
{
    constexpr int space1 = 4, space2 = 8, space3 = 12, space4 = 16, space6 = 24, space8 = 32;
    constexpr float radiusSm = 3.0f, radiusMd = 6.0f, radiusLg = 12.0f;
    constexpr int knobHero = 96, knobMain = 60, knobSmall = 40;
    constexpr int header = 48, footer = 24;
    constexpr float arc = 3.0f;
}

// Dimmer text for disabled / tertiary labels, derived so it stays in the family.
inline juce::Colour faint(const Palette& p) { return p.inkMuted.interpolatedWith(p.surface, 0.45f); }
// Suites (Epsilon Expansion) may tint their hero display with a product hue; chrome stays on the palette.
} // namespace epsilon_ui
