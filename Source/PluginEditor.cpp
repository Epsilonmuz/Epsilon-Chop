#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
using namespace theme;
using Peak = chop::Peak;

bool supportedFile(const juce::File& file)
{
    return file.hasFileExtension("wav;wave;aif;aiff;flac;mp3;ogg");
}

juce::String twoDigits(int value)
{
    return juce::String(value).paddedLeft('0', 2);
}

// Panel rectangles at design size
const juce::Rectangle<float> heroPanel { 20.0f, 76.0f, 1080.0f, 300.0f };
const juce::Rectangle<float> ribbonPanel { 20.0f, 388.0f, 1080.0f, 152.0f };
const juce::Rectangle<float> transportPanel { 20.0f, 552.0f, 360.0f, 160.0f };
const juce::Rectangle<float> variationPanel { 392.0f, 552.0f, 360.0f, 160.0f };
const juce::Rectangle<float> stepPanel { 764.0f, 552.0f, 336.0f, 160.0f };
const juce::Rectangle<float> soundPanel { 20.0f, 724.0f, 1080.0f, 92.0f };

// Computer keyboard: the number row, then the row under it.
constexpr std::array<char, 20> chopKeys { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
                                          'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P' };

void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {})
{
    juce::ColourGradient body(ink1.brighter(0.03f), r.getX(), r.getY(), ink1, r.getX(), r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, 14.0f);
    g.setColour(line);
    g.drawRoundedRectangle(r.reduced(0.5f), 14.0f, 1.0f);
    // A hairline highlight along the top edge gives the chassis some depth.
    g.setColour(juce::Colours::white.withAlpha(0.035f));
    g.drawHorizontalLine(juce::roundToInt(r.getY() + 1.0f), r.getX() + 14.0f, r.getRight() - 14.0f);
    if (title.isNotEmpty())
    {
        g.setColour(faint);
        g.setFont(sans(10.5f, true));
        g.drawText(title, juce::Rectangle<float>(r.getX() + 16.0f, r.getY() + 8.0f, 200.0f, 14.0f), juce::Justification::centredLeft);
    }
}

void caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour(dim);
    g.setFont(sans(10.5f, true));
    g.drawText(text, r, j);
}

Peak peakBetween(const chop::Sample& sample, int firstFrame, int lastFrame)
{
    const auto frames = std::max(1, sample.length());
    auto first = static_cast<int>(static_cast<std::int64_t>(firstFrame) * chop::waveformBins / frames);
    auto last = static_cast<int>(static_cast<std::int64_t>(lastFrame) * chop::waveformBins / frames);
    first = juce::jlimit(0, chop::waveformBins - 1, first);
    last = juce::jlimit(first + 1, chop::waveformBins, last);
    chop::Peak result { 1.0f, -1.0f };
    for (int bin = first; bin < last; ++bin)
    {
        result.low = std::min(result.low, sample.waveform[static_cast<std::size_t>(bin)].low);
        result.high = std::max(result.high, sample.waveform[static_cast<std::size_t>(bin)].high);
    }
    if (result.low > result.high)
        result = {};
    return result;
}

float sampleLargest(const chop::Sample& sample)
{
    auto largest = 0.05f;
    for (const auto& peak : sample.waveform)
        largest = std::max({ largest, std::abs(peak.low), std::abs(peak.high) });
    return largest;
}
}

// ============================================================================
// WaveformView

float WaveformView::xAt(int frame) const
{
    const auto* sample = editor.view.session.sample.get();
    if (sample == nullptr || sample->length() == 0)
        return 0.0f;
    return getWidth() * static_cast<float>(frame) / static_cast<float>(sample->length());
}

int WaveformView::frameAt(float x) const
{
    const auto* sample = editor.view.session.sample.get();
    if (sample == nullptr)
        return 0;
    return juce::jlimit(0, sample->length() - 1, juce::roundToInt(x / std::max(1, getWidth()) * sample->length()));
}

int WaveformView::markerNear(float x) const
{
    if (editor.view.session.sample == nullptr)
        return -1;
    const auto& slices = editor.view.session.slices;
    int nearest = -1;
    float distance = 6.0f;
    for (int index = 1; index < slices.count; ++index)
    {
        const auto d = std::abs(x - xAt(slices.edge[index]));
        if (d < distance) { nearest = index; distance = d; }
    }
    return nearest;
}

int WaveformView::sliceAt(float x) const
{
    const auto& slices = editor.view.session.slices;
    if (editor.view.session.sample == nullptr || slices.count == 0)
        return -1;
    const auto frame = frameAt(x);
    const auto end = slices.edge.begin() + slices.count + 1;
    const auto found = std::upper_bound(slices.edge.begin(), end, frame);
    return juce::jlimit(0, slices.count - 1, static_cast<int>(std::distance(slices.edge.begin(), found)) - 1);
}

void WaveformView::renderCache(juce::Graphics& g, int width, int height) const
{
    const auto& state = editor.view.session;
    const auto* sample = state.sample.get();
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(ink0);
    g.fillRoundedRectangle(bounds, 10.0f);
    if (sample == nullptr)
        return;

    const auto top = 24.0f, bottom = bounds.getBottom() - 6.0f;
    const auto centre = (top + bottom) * 0.5f;
    const auto scale = (bottom - top) * 0.48f / sampleLargest(*sample);
    const auto frames = sample->length();
    auto xFor = [&](int frame) { return width * static_cast<float>(frame) / static_cast<float>(frames); };

    for (int slice = 0; slice < state.slices.count; ++slice)
    {
        const auto first = state.slices.edge[slice], last = state.slices.edge[slice + 1];
        const auto x0 = xFor(first), x1 = xFor(last);
        const auto colour = chopColour(slice);
        const auto selected = slice == editor.selectedChop;
        g.setColour(colour.withAlpha(selected ? 0.13f : 0.045f));
        g.fillRect(x0, top, x1 - x0, bottom - top);

        // Waveform in the chop's own colour
        g.setColour(colour.withAlpha(selected ? 1.0f : 0.86f));
        const auto px0 = static_cast<int>(std::floor(x0)), px1 = std::max(px0 + 1, static_cast<int>(std::ceil(x1)));
        for (int px = px0; px < px1; ++px)
        {
            const auto a = std::max(first, static_cast<int>(static_cast<std::int64_t>(px) * frames / width));
            const auto b = std::min(last, std::max(a + 1, static_cast<int>(static_cast<std::int64_t>(px + 1) * frames / width)));
            const auto peak = peakBetween(*sample, a, b);
            const auto y0 = centre - scale * peak.high, y1 = std::max(centre - scale * peak.low, y0 + 1.0f);
            g.fillRect(static_cast<float>(px), y0, 1.0f, y1 - y0);
        }

        if (slice > 0)
        {
            g.setColour(colour.withAlpha(0.75f));
            g.fillRect(x0 - 0.5f, top - 18.0f, 1.0f, bottom - top + 18.0f);
        }
        if (x1 - x0 > 24.0f)
        {
            const auto tag = juce::Rectangle<float>(x0 + 3.0f, 4.0f, 24.0f, 15.0f);
            g.setColour(selected ? colour : ink2);
            g.fillRoundedRectangle(tag, 4.0f);
            g.setColour(selected ? ink0 : colour);
            g.setFont(mono(10.0f));
            g.drawText(twoDigits(slice + 1), tag, juce::Justification::centred);
            if (slice < static_cast<int>(chopKeys.size()) && x1 - x0 > 46.0f)
            {
                // The computer key that plays this chop
                g.setColour(faint);
                g.setFont(mono(10.0f));
                g.drawText(juce::String::charToString(chopKeys[static_cast<std::size_t>(slice)]),
                           juce::Rectangle<float>(tag.getRight() + 3.0f, tag.getY(), 12.0f, tag.getHeight()), juce::Justification::centred);
            }
        }

        // Hit Align: where the attack really starts, if it isn't right at the cut
        const auto hit = state.slices.hit[static_cast<std::size_t>(slice)];
        if (editor.alignAmount() > 0.0f && hit > 0)
        {
            const auto hx = xFor(first + hit);
            juce::Path marker;
            marker.addTriangle(hx - 3.5f, top - 6.0f, hx + 3.5f, top - 6.0f, hx, top - 1.0f);
            g.setColour(colour);
            g.fillPath(marker);
            g.setColour(colour.withAlpha(0.35f));
            g.fillRect(hx - 0.5f, top, 1.0f, bottom - top);
        }
    }
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawHorizontalLine(juce::roundToInt(centre), 0.0f, static_cast<float>(width));
}

void WaveformView::paint(juce::Graphics& g)
{
    const auto& state = editor.view.session;
    const auto* sample = state.sample.get();
    const auto bounds = getLocalBounds().toFloat();

    if (sample == nullptr)
    {
        g.setColour(ink0);
        g.fillRoundedRectangle(bounds, 10.0f);
        // Dashed drop zone
        juce::Path zone;
        zone.addRoundedRectangle(bounds.reduced(10.0f), 10.0f);
        juce::Path dashed;
        const float dashes[] = { 6.0f, 6.0f };
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, zone, dashes, 2);
        g.setColour(lineStrong);
        g.fillPath(dashed);

        // A row of coloured bars hints at what will happen to the sample.
        const auto centreX = bounds.getCentreX();
        for (int i = 0; i < 8; ++i)
        {
            const auto h = 10.0f + 26.0f * std::abs(std::sin(i * 1.7f + 0.4f));
            g.setColour(chopColour(i).withAlpha(0.85f));
            g.fillRoundedRectangle(centreX - 76.0f + i * 20.0f, bounds.getCentreY() - 40.0f - h * 0.5f, 12.0f, h, 3.0f);
        }
        g.setColour(text);
        g.setFont(sans(24.0f, true));
        g.drawText(editor.view.loading ? "Analysing..." : "Drop a sample to start chopping",
                   bounds.withTrimmedTop(bounds.getHeight() * 0.5f - 6.0f).withHeight(32.0f), juce::Justification::centred);
        g.setColour(dim);
        g.setFont(sans(12.5f));
        g.drawText(juce::String::fromUTF8("WAV  ·  AIFF  ·  FLAC  ·  MP3     or click LOAD SAMPLE"),
                   bounds.withTrimmedTop(bounds.getHeight() * 0.5f + 30.0f).withHeight(20.0f), juce::Justification::centred);
        return;
    }

    const auto physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (!cacheValid || cache.isNull() || std::abs(physical - cacheScale) > 0.01f
        || cache.getWidth() != juce::roundToInt(getWidth() * physical))
    {
        cacheScale = physical;
        cache = juce::Image(juce::Image::ARGB, std::max(1, juce::roundToInt(getWidth() * physical)),
                            std::max(1, juce::roundToInt(getHeight() * physical)), true, juce::NativeImageType());
        juce::Graphics cg(cache);
        cg.addTransform(juce::AffineTransform::scale(physical));
        renderCache(cg, getWidth(), getHeight());
        cacheValid = true;
    }
    g.drawImage(cache, bounds, juce::RectanglePlacement::stretchToFit);

    const auto top = 24.0f, bottom = bounds.getBottom() - 6.0f;
    const auto& slices = state.slices;

    // What's sounding right now
    const auto playing = editor.processor.playSlice();
    if (playing >= 0 && playing < slices.count)
    {
        const auto x0 = xAt(slices.edge[playing]), x1 = xAt(slices.edge[playing + 1]);
        g.setColour(chopColour(playing).withAlpha(0.14f));
        g.fillRect(x0, top, x1 - x0, bottom - top);
        g.setColour(chopColour(playing));
        g.fillRect(x0, top - 3.0f, x1 - x0, 3.0f);
    }
    if (hoverSlice >= 0 && hoverSlice < slices.count && dragMarker < 0 && hoverMarker < 0)
    {
        const auto x0 = xAt(slices.edge[hoverSlice]), x1 = xAt(slices.edge[hoverSlice + 1]);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(x0, top, x1 - x0, bottom - top);
    }
    if (hoverMarker > 0 || dragMarker > 0)
    {
        const auto index = dragMarker > 0 ? dragMarker : hoverMarker;
        const auto x = dragMarker > 0 && dragRevision == state.revision ? xAt(dragFrame) : xAt(slices.edge[index]);
        g.setColour(text);
        g.fillRect(x - 1.0f, 0.0f, 2.0f, bounds.getHeight());
        g.fillEllipse(x - 4.0f, top - 10.0f, 8.0f, 8.0f);
    }

    const auto frame = editor.processor.playFrame();
    if (frame >= 0 && frame < sample->length())
    {
        const auto x = xAt(frame);
        g.setColour(live.withAlpha(0.18f));
        g.fillRect(x - 3.0f, top, 6.0f, bottom - top);
        g.setColour(live);
        g.fillRect(x - 0.75f, top, 1.5f, bottom - top);
    }
}

void WaveformView::updatePlayhead(int frame, int slice)
{
    auto line = [this](int f)
    {
        if (f < 0)
            return juce::Rectangle<int>();
        return juce::Rectangle<float>(xAt(f) - 4.0f, 0.0f, 8.0f, static_cast<float>(getHeight())).getSmallestIntegerContainer();
    };
    auto band = [this](int s)
    {
        const auto& slices = editor.view.session.slices;
        if (s < 0 || s >= slices.count)
            return juce::Rectangle<int>();
        const auto x0 = xAt(slices.edge[static_cast<std::size_t>(s)]), x1 = xAt(slices.edge[static_cast<std::size_t>(s + 1)]);
        return juce::Rectangle<float>(x0 - 1.0f, 0.0f, x1 - x0 + 2.0f, static_cast<float>(getHeight())).getSmallestIntegerContainer();
    };
    if (slice != shownSlice)
    {
        repaint(band(shownSlice));
        repaint(band(slice));
    }
    repaint(line(shownFrame));
    repaint(line(frame));
    shownFrame = frame;
    shownSlice = slice;
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    const auto marker = markerNear(e.position.x);
    const auto slice = sliceAt(e.position.x);
    setMouseCursor(marker > 0 ? juce::MouseCursor::LeftRightResizeCursor
                              : (editor.view.session.sample != nullptr ? juce::MouseCursor::PointingHandCursor
                                                                       : juce::MouseCursor::NormalCursor));
    if (marker != hoverMarker || slice != hoverSlice)
    {
        hoverMarker = marker;
        hoverSlice = slice;
        repaint();
    }
}

void WaveformView::mouseExit(const juce::MouseEvent&)
{
    hoverMarker = hoverSlice = -1;
    repaint();
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    if (editor.view.session.sample == nullptr)
    {
        if (e.mods.isLeftButtonDown())
            editor.chooseSample();
        return;
    }
    dragStarted = false;
    downSlice = -1;
    const auto marker = markerNear(e.position.x);
    if (e.mods.isPopupMenu())
    {
        if (marker > 0)
        {
            editor.processor.editBoundary(marker, -1);
            editor.pull();
        }
        return;
    }
    if (marker > 0)
    {
        dragMarker = marker;
        dragFrame = editor.view.session.slices.edge[marker];
        dragRevision = editor.view.session.revision;
        return;
    }
    downSlice = sliceAt(e.position.x);
    if (downSlice >= 0)
    {
        editor.selectChop(downSlice);
        editor.processor.previewChop(downSlice);
    }
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMarker > 0)
    {
        if (dragRevision != editor.view.session.revision)
            return;
        const auto& slices = editor.view.session.slices;
        const auto spacing = std::max(1, juce::roundToInt(editor.view.session.sample->sampleRate * 0.004));
        const auto first = slices.edge[dragMarker - 1] + spacing;
        const auto last = slices.edge[dragMarker + 1] - spacing;
        if (first <= last)
            dragFrame = juce::jlimit(first, last, frameAt(e.position.x));
        repaint();
        return;
    }
    if (downSlice >= 0 && !dragStarted && e.getDistanceFromDragStart() > 8)
    {
        dragStarted = true;
        // Drag a chop onto any step of the ribbon.
        juce::Image chip(juce::Image::ARGB, 44, 26, true);
        {
            juce::Graphics cg(chip);
            cg.setColour(chopColour(downSlice));
            cg.fillRoundedRectangle(0.0f, 0.0f, 44.0f, 26.0f, 6.0f);
            cg.setColour(ink0);
            cg.setFont(mono(13.0f));
            cg.drawText(twoDigits(downSlice + 1), 0, 0, 44, 26, juce::Justification::centred);
        }
        editor.startDragging("chop:" + juce::String(downSlice), this, juce::ScaledImage(chip), true);
    }
}

void WaveformView::mouseUp(const juce::MouseEvent&)
{
    if (dragMarker > 0)
    {
        if (dragRevision == editor.view.session.revision)
            editor.processor.editBoundary(dragMarker, dragFrame);
        dragMarker = -1;
        editor.pull();
    }
    downSlice = -1;
}

void WaveformView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (editor.view.session.sample != nullptr && markerNear(e.position.x) < 0)
    {
        editor.processor.editBoundary(-1, frameAt(e.position.x));
        editor.pull();
    }
}

// ============================================================================
// RibbonView

juce::Rectangle<float> RibbonView::cell(int index) const
{
    constexpr float gap = 6.0f, groupGap = 16.0f, ruler = 16.0f;
    const auto width = (getWidth() - 12.0f * gap - 3.0f * groupGap) / chop::stepCount;
    const auto x = index * (width + gap) + (index / 4) * (groupGap - gap);
    return { x, 0.0f, width, getHeight() - ruler };
}

int RibbonView::cellAt(juce::Point<float> p) const
{
    for (int i = 0; i < chop::stepCount; ++i)
        if (cell(i).expanded(3.0f, 0.0f).contains(p))
            return i;
    return -1;
}

void RibbonView::paintCell(juce::Graphics& g, int index, juce::Rectangle<float> r) const
{
    const auto& state = editor.view.session;
    const auto& step = editor.pattern()[static_cast<std::size_t>(index)];
    const auto count = state.slices.count;
    const auto slice = step.slice >= 0 && count > 0 ? step.slice % count : (step.slice >= 0 ? step.slice : -1);
    const auto hit = slice >= 0;

    g.setColour(hit ? ink2 : ink1.darker(0.15f));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(index % 4 == 0 ? lineStrong : line);
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);

    if (!hit)
    {
        g.setColour(faint.withAlpha(0.6f));
        g.fillRoundedRectangle(r.getCentreX() - 6.0f, r.getCentreY() - 1.0f, 12.0f, 2.0f, 1.0f);
        return;
    }

    const auto colour = chopColour(slice);
    auto inner = r.reduced(6.0f, 5.0f);
    auto header = inner.removeFromTop(14.0f);
    g.setColour(colour);
    g.setFont(mono(11.5f));
    g.drawText(twoDigits(slice + 1), header, juce::Justification::centredLeft);

    // Badges: pitch, reverse
    auto badges = header;
    g.setFont(mono(9.5f));
    if (step.pitch != 0)
    {
        g.setColour(text);
        g.drawText((step.pitch > 0 ? "+" : "") + juce::String(step.pitch), badges, juce::Justification::centredRight);
        badges.removeFromRight(20.0f);
    }
    if (step.reverse)
    {
        juce::Path arrow;
        const auto ax = badges.getRight() - 4.0f, ay = badges.getCentreY();
        arrow.addTriangle(ax - 8.0f, ay, ax, ay - 4.0f, ax, ay + 4.0f);
        g.setColour(text);
        g.fillPath(arrow);
    }

    auto gateBar = inner.removeFromBottom(3.0f);
    inner.removeFromBottom(4.0f);
    g.setColour(ink3);
    g.fillRoundedRectangle(gateBar, 1.5f);
    g.setColour(colour.withAlpha(0.9f));
    g.fillRoundedRectangle(gateBar.withWidth(gateBar.getWidth() * step.gate * 0.01f), 1.5f);

    // The chop's own waveform, repeated once per ratchet, mirrored if reversed.
    const auto* sample = state.sample.get();
    const auto ratchet = std::clamp(step.ratchet, 1, chop::maxRatchet);
    const auto segmentGap = 2.0f;
    const auto segmentWidth = (inner.getWidth() - (ratchet - 1) * segmentGap) / ratchet;
    for (int r2 = 0; r2 < ratchet; ++r2)
    {
        const auto seg = juce::Rectangle<float>(inner.getX() + r2 * (segmentWidth + segmentGap), inner.getY(), segmentWidth, inner.getHeight());
        if (sample == nullptr || slice >= count)
        {
            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(seg.reduced(0.0f, seg.getHeight() * 0.3f), 2.0f);
            continue;
        }
        const auto first = state.slices.edge[slice], last = state.slices.edge[slice + 1];
        const auto columns = std::max(1, static_cast<int>(seg.getWidth()));
        auto largest = 0.02f;
        for (int c = 0; c < columns; ++c)
        {
            const auto a = first + static_cast<int>(static_cast<std::int64_t>(last - first) * c / columns);
            const auto b = first + static_cast<int>(static_cast<std::int64_t>(last - first) * (c + 1) / columns);
            const auto p = peakBetween(*sample, a, std::max(a + 1, b));
            largest = std::max({ largest, std::abs(p.low), std::abs(p.high) });
        }
        const auto mid = seg.getCentreY();
        const auto amp = seg.getHeight() * 0.48f / largest;
        const auto gateColumns = std::max(1, static_cast<int>(columns * step.gate * 0.01f));
        for (int c = 0; c < columns; ++c)
        {
            const auto source = step.reverse ? columns - 1 - c : c;
            const auto a = first + static_cast<int>(static_cast<std::int64_t>(last - first) * source / columns);
            const auto b = first + static_cast<int>(static_cast<std::int64_t>(last - first) * (source + 1) / columns);
            const auto p = peakBetween(*sample, a, std::max(a + 1, b));
            const auto y0 = mid - amp * p.high, y1 = std::max(mid - amp * p.low, y0 + 1.0f);
            g.setColour(colour.withAlpha(c < gateColumns ? 0.95f : 0.22f));
            g.fillRect(seg.getX() + c, y0, 1.0f, y1 - y0);
        }
        const auto hit = state.slices.hit[static_cast<std::size_t>(slice)];
        if (editor.alignAmount() > 0.0f && hit > 0 && !step.reverse)
        {
            const auto hx = seg.getX() + seg.getWidth() * static_cast<float>(hit) / static_cast<float>(std::max(1, last - first));
            g.setColour(live.withAlpha(0.8f));
            g.fillRect(hx - 0.5f, seg.getBottom() - 6.0f, 1.0f, 6.0f);
        }
    }
}

void RibbonView::renderCache(juce::Graphics& g) const
{
    for (int i = 0; i < chop::stepCount; ++i)
        paintCell(g, i, cell(i));

    // Dovetail: the pinned step is the keystone, and every join shows how well it fits.
    const auto& state = editor.view.session;
    const auto anchor = state.edit.anchor[static_cast<std::size_t>(state.edit.bank)];
    const auto& steps = editor.pattern();
    if (anchor >= 0 && steps[static_cast<std::size_t>(anchor)].slice >= 0 && state.fit != nullptr && state.fit->count > 0)
    {
        std::vector<int> hits;
        for (int i = 0; i < chop::stepCount; ++i)
            if (steps[static_cast<std::size_t>(i)].slice >= 0)
                hits.push_back(i);
        const auto& pinned = steps[static_cast<std::size_t>(anchor)];
        for (std::size_t k = 0; k < hits.size() && hits.size() > 1; ++k)
        {
            const auto from = hits[(k + hits.size() - 1) % hits.size()], to = hits[k];
            const auto quality = chop::joinQuality(*state.fit, steps[static_cast<std::size_t>(from)], steps[static_cast<std::size_t>(to)], &pinned);
            const auto colourFrom = chopColour(steps[static_cast<std::size_t>(from)].slice % state.fit->count);
            const auto colourTo = chopColour(steps[static_cast<std::size_t>(to)].slice % state.fit->count);
            const auto a = cell(from), b = cell(to);
            const auto midY = b.getCentreY();
            if (to == from + 1)
            {
                // Neighbours lock together: a tab bridges the gap, brighter the better they fit.
                const auto gap = juce::Rectangle<float>(a.getRight() - 2.0f, midY - 9.0f, b.getX() - a.getRight() + 4.0f, 18.0f);
                juce::ColourGradient glow(colourFrom.withAlpha(0.15f + 0.85f * quality), gap.getX(), midY,
                                          colourTo.withAlpha(0.15f + 0.85f * quality), gap.getRight(), midY, false);
                g.setGradientFill(glow);
                g.fillRoundedRectangle(gap, 3.0f);
            }
            else
            {
                // A join across rests: a notch on the incoming edge
                g.setColour(colourTo.withAlpha(0.15f + 0.85f * quality));
                g.fillRoundedRectangle(b.getX() - 3.0f, midY - 8.0f, 4.0f, 16.0f, 2.0f);
            }
        }
        const auto keystone = cell(anchor);
        g.setColour(text);
        g.drawRoundedRectangle(keystone.reduced(1.0f), 8.0f, 2.2f);
        // Pin glyph
        const auto px = keystone.getRight() - 11.0f, py = keystone.getY() + 9.0f;
        g.fillEllipse(px - 3.5f, py - 3.5f, 7.0f, 7.0f);
        g.fillRect(px - 0.75f, py + 2.0f, 1.5f, 5.0f);
    }

    g.setFont(mono(10.5f));
    for (int beat = 0; beat < 4; ++beat)
    {
        const auto r = cell(beat * 4);
        g.setColour(dim);
        g.drawText(juce::String(beat + 1), juce::Rectangle<float>(r.getX(), r.getBottom() + 3.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
        for (int sub = 1; sub < 4; ++sub)
        {
            const auto c = cell(beat * 4 + sub);
            g.setColour(faint.withAlpha(0.6f));
            g.fillEllipse(c.getX() + 1.0f, c.getBottom() + 7.0f, 2.0f, 2.0f);
        }
    }
}

void RibbonView::paint(juce::Graphics& g)
{
    const auto physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (!cacheValid || cache.isNull() || std::abs(physical - cacheScale) > 0.01f
        || cache.getWidth() != juce::roundToInt(getWidth() * physical))
    {
        cacheScale = physical;
        cache = juce::Image(juce::Image::ARGB, std::max(1, juce::roundToInt(getWidth() * physical)),
                            std::max(1, juce::roundToInt(getHeight() * physical)), true, juce::NativeImageType());
        juce::Graphics cg(cache);
        cg.addTransform(juce::AffineTransform::scale(physical));
        renderCache(cg);
        cacheValid = true;
    }
    g.drawImage(cache, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const auto playing = editor.processor.playStep();
    for (int i = 0; i < chop::stepCount; ++i)
    {
        const auto r = cell(i);
        if (i == playing)
        {
            g.setColour(live.withAlpha(0.10f));
            g.fillRoundedRectangle(r, 8.0f);
            g.setColour(live);
            g.drawRoundedRectangle(r.reduced(1.0f), 8.0f, 2.0f);
        }
        if (i == editor.selectedStep)
        {
            g.setColour(text);
            g.drawRoundedRectangle(r.expanded(2.5f), 10.0f, 1.4f);
        }
        if (i == dropTarget)
        {
            g.setColour(text.withAlpha(0.12f));
            g.fillRoundedRectangle(r, 8.0f);
        }
    }
}

void RibbonView::stepMoved(int previous, int current)
{
    for (const auto index : { previous, current })
        if (index >= 0 && index < chop::stepCount)
            repaint(cell(index).expanded(4.0f).getSmallestIntegerContainer());
}

void RibbonView::mouseDown(const juce::MouseEvent& e)
{
    const auto index = cellAt(e.position);
    if (index < 0)
        return;
    editor.selectStep(index);
    auto step = editor.pattern()[static_cast<std::size_t>(index)];
    if (e.mods.isPopupMenu() || e.mods.isShiftDown())
    {
        step.slice = step.slice < 0 ? editor.selectedChop : -1;
        editor.setStep(index, step);
        return;
    }
    if (step.slice >= 0 && editor.view.session.slices.count > 0)
        editor.processor.previewChop(step.slice % editor.view.session.slices.count);
}

void RibbonView::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto index = cellAt(e.position);
    if (index < 0 || e.mods.isPopupMenu() || e.mods.isShiftDown())
        return;
    auto step = editor.pattern()[static_cast<std::size_t>(index)];
    step.slice = step.slice < 0 ? editor.selectedChop : -1;
    editor.setStep(index, step);
}

void RibbonView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto index = cellAt(e.position);
    const auto count = editor.view.session.slices.count;
    if (index < 0 || count == 0)
        return;
    wheelAccumulator += wheel.deltaY;
    if (std::abs(wheelAccumulator) < 0.12)
        return;
    const auto direction = wheelAccumulator > 0 ? 1 : -1;
    wheelAccumulator = 0.0;
    editor.selectStep(index);
    auto step = editor.pattern()[static_cast<std::size_t>(index)];
    step.slice = step.slice < 0 ? editor.selectedChop : ((step.slice % count) + direction + count) % count;
    editor.setStep(index, step);
    editor.processor.previewChop(step.slice);
}

bool RibbonView::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith("chop:");
}

void RibbonView::itemDragMove(const SourceDetails& details)
{
    const auto index = cellAt(details.localPosition.toFloat());
    if (index != dropTarget) { dropTarget = index; repaint(); }
}

void RibbonView::itemDragExit(const SourceDetails&)
{
    dropTarget = -1;
    repaint();
}

void RibbonView::itemDropped(const SourceDetails& details)
{
    const auto index = cellAt(details.localPosition.toFloat());
    dropTarget = -1;
    if (index < 0)
    {
        repaint();
        return;
    }
    auto step = editor.pattern()[static_cast<std::size_t>(index)];
    step.slice = details.description.toString().fromFirstOccurrenceOf(":", false, false).getIntValue();
    editor.selectStep(index);
    editor.setStep(index, step);
}

// ============================================================================
// Editor

EpsilonChopEditor::EpsilonChopEditor(EpsilonChopProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner), view(owner.editorState())
{
    setLookAndFeel(&look);
    addAndMakeVisible(content);
    content.setBounds(0, 0, designWidth, designHeight);
    content.painter = [this](juce::Graphics& g)
    {
        g.fillAll(ink0);
        paintHeader(g);
        paintPanels(g);
        paintFooter(g);
    };
    content.overPainter = [this](juce::Graphics& g)
    {
        if (draggingFile)
        {
            g.setColour(live.withAlpha(0.08f));
            g.fillRoundedRectangle(heroPanel, 14.0f);
            g.setColour(live);
            g.drawRoundedRectangle(heroPanel.reduced(1.0f), 14.0f, 2.0f);
        }
    };

    for (auto* c : std::initializer_list<juce::Component*> {
             &waveform, &ribbon, &load, &output, &mode, &density, &division, &sensitivity, &rechop, &snap, &loopBpm,
             &banks, &grooves, &inOrderButton, &clearButton, &dragChops, &dragLoop, &play, &tempo, &rate, &hostSync, &tempoFit, &swing,
             &dice, &chaos, &undoButton, &previousChop, &nextChop, &gate, &pitch, &ratchet, &reverse, &rest, &root,
             &pin, &fitKnob, &vintageMode, &chokeMode, &drive, &alignKnob })
        content.addAndMakeVisible(*c);
    setWantsKeyboardFocus(true);
    content.addMouseListener(this, true);

    // Header
    load.onClick = [this] { chooseSample(); };
    load.setTooltip("Load a WAV, AIFF, FLAC or MP3. You can also drop a file anywhere on the plugin.");
    output.slider.setRange(-24.0, 6.0, 0.1);
    output.format = [](double v) { return juce::String(v, 1) + " dB"; };
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "output", output.slider);
    output.slider.setDoubleClickReturnValue(true, -3.0);

    // Chop mode bar
    mode.setOptions({ "TRANSIENT", "BEAT", "GRID", "RANDOM", "MANUAL" },
                    { static_cast<int>(chop::SliceMode::transient), static_cast<int>(chop::SliceMode::beat),
                      static_cast<int>(chop::SliceMode::grid), static_cast<int>(chop::SliceMode::random),
                      static_cast<int>(chop::SliceMode::manual) });
    mode.reportReselect = true;
    mode.onChange = [this](int value)
    {
        if (value == static_cast<int>(chop::SliceMode::random) && view.session.edit.mode == chop::SliceMode::random)
            processor.rechop();   // clicking RANDOM again rolls new cut points
        else
            processor.setMode(static_cast<chop::SliceMode>(value));
        pull();
    };
    division.setOptions({ "1/4", "1/8", "1/16" }, { 1, 2, 4 });
    division.onChange = [this](int value) { processor.setBeatDivision(value); pull(); };
    density.onChange = [this](int value) { processor.setDensity(value); pull(); };
    sensitivity.setSliderStyle(juce::Slider::LinearHorizontal);
    sensitivity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sensitivity.setRange(0.0, 100.0, 1.0);
    sensitivity.setTooltip("How many hits count as a new chop. Higher finds quieter attacks.");
    sensitivity.onDragStart = [this] { firstSensitivityChange = true; };
    sensitivity.onValueChange = [this]
    {
        processor.setSensitivity(juce::roundToInt(sensitivity.getValue()), firstSensitivityChange);
        firstSensitivityChange = false;
        pull();
    };
    rechop.onClick = [this] { processor.rechop(); pull(); };
    rechop.setTooltip("Roll a new set of random cut points");
    snap.setClickingTogglesState(true);
    snap.setTooltip("Snap every cut to the nearest zero crossing, for click-free chops");
    snap.onClick = [this] { processor.setSnapZero(snap.getToggleState()); pull(); };
    loopBpm.caption = "LOOP BPM";
    loopBpm.minimum = 30.0;
    loopBpm.maximum = 400.0;
    loopBpm.perPixel = 0.1;
    loopBpm.valueHeight = 15.0f;
    loopBpm.setTooltip("Tempo of the loaded loop. Drag to correct it, double-click to re-detect. Used by BEAT and TEMPO FIT.");
    loopBpm.onCommit = [this](double v) { processor.setSampleBpm(v); pull(); };
    loopBpm.onReset = [this] { processor.setSampleBpm(0.0); pull(); };

    // Ribbon header
    banks.setOptions({ "A", "B", "C", "D" }, { 0, 1, 2, 3 });
    banks.fontHeight = 12.0f;
    banks.onChange = [this](int value) { processor.setBank(value); pull(); };
    grooves.setTextWhenNothingSelected("GROOVES");
    for (int i = 0; i < chop::presetCount; ++i)
        grooves.addItem(chop::presetName(i), i + 1);
    grooves.onChange = [this]
    {
        if (const auto id = grooves.getSelectedId(); id > 0)
            processor.applyPreset(id - 1);
        grooves.setSelectedId(0, juce::dontSendNotification);
        pull();
    };
    inOrderButton.setTooltip("Lay the chops out in their original order, so the loop plays back as recorded");
    inOrderButton.onClick = [this] { processor.arrangeInOrder(); pull(); };
    clearButton.onClick = [this] { processor.clearPattern(); pull(); };

    // Export to FL Studio / any DAW
    dragChops.setTooltip("Drag every chop into FL Studio: drop on the Channel Rack for one channel per chop, or on the "
                         "Playlist. Drag a single chop from the waveform past the plugin's edge to drop just that one. "
                         "Click to show the files in Finder.");
    dragLoop.setTooltip("Drag this bank out as one seamless audio loop, exactly as you hear it (swing, choke, vintage, "
                        "pitch, ratchets) at the project tempo. Drop it on FL's Playlist. Click to show the files in Finder.");
    const auto reveal = [this](juce::File f)
    {
        if (f.existsAsFile())
            f.revealToUser();
        else
            processor.exportFolder().revealToUser();
    };
    dragChops.files = [this] { return processor.exportAllChops(); };
    dragChops.onClick = [this, reveal]
    {
        const auto files = processor.exportAllChops();
        reveal(files.isEmpty() ? juce::File() : juce::File(files[0]));
    };
    dragLoop.files = [this]
    {
        const auto f = processor.exportPattern(1);
        return f.existsAsFile() ? juce::StringArray { f.getFullPathName() } : juce::StringArray {};
    };
    dragLoop.onClick = [this, reveal] { reveal(processor.exportPattern(1)); };

    // Transport
    play.onClick = [this] { processor.setPlaying(!processor.isPlaying()); };
    play.setTooltip("Preview the arrangement on the internal clock (no need to press play in FL Studio)");
    tempo.caption = "TEMPO";
    tempo.unit = "BPM";
    tempo.minimum = 40.0;
    tempo.maximum = 240.0;
    tempo.perPixel = 0.2;
    tempo.onChange = [this](double v) { setParameter("tempo", static_cast<float>(v)); };
    tempo.onReset = [this] { setParameter("tempo", 120.0f); };
    rate.setOptions({ "1/8", "1/16", "1/32" }, { 0, 1, 2 });
    rate.onChange = [this](int value) { setParameter("rate", static_cast<float>(value)); };
    hostSync.setClickingTogglesState(true);
    hostSync.setTooltip("Play the pattern whenever FL Studio's transport is running, locked to its bar position");
    hostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.parameters, "pattern", hostSync);
    tempoFit.setClickingTogglesState(true);
    tempoFit.setTooltip("Repitch the sample so its loop BPM matches the tempo, so chops line up with the grid");
    fitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.parameters, "fit", tempoFit);
    swing.slider.setRange(0.0, 60.0, 1.0);
    swing.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "swing", swing.slider);
    swing.slider.setDoubleClickReturnValue(true, 0.0);

    // Variation
    dice.onClick = [this]
    {
        processor.randomize();
        // Hear the flip straight away.
        if (!processor.isPlaying() && !processor.hostSynced())
            processor.setPlaying(true);
        pull();
    };
    dice.setTooltip("Rearrange this bank. With a step pinned, Dovetail picks chops that fit around it. Everything can be undone.");
    fitKnob.slider.setRange(0.0, 100.0, 1.0);
    fitKnob.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    fitKnob.slider.setTooltip("Dovetail: PIN a step, then roll. Every chop after it is chosen to flow out of the one before it "
                              "(key, loudness, tone), wrapping back round to the pinned step. Higher = stricter fit; CHAOS adds adventure.");
    fitKnob.slider.onDragEnd = [this] { processor.setFitAmount(juce::roundToInt(fitKnob.slider.getValue())); pull(); };
    fitKnob.onValue = [this] { if (!fitKnob.slider.isMouseButtonDown()) processor.setFitAmount(juce::roundToInt(fitKnob.slider.getValue())); };
    fitKnob.slider.setDoubleClickReturnValue(true, 70.0);
    chaos.slider.setRange(0.0, 100.0, 1.0);
    chaos.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    chaos.slider.setTooltip("How much each roll changes");
    chaos.slider.onDragEnd = [this] { processor.setChaos(juce::roundToInt(chaos.slider.getValue())); pull(); };
    chaos.onValue = [this] { if (!chaos.slider.isMouseButtonDown()) processor.setChaos(juce::roundToInt(chaos.slider.getValue())); };
    const std::array<const char*, 6> flagNames { "SWAP", "RHYTHM", "STUTTER", "REVERSE", "PITCH", "GATE" };
    const std::array<const char*, 6> flagTips {
        "Chops trade places: e.g. every 11 becomes 13 and every 13 becomes 11, timing untouched. CHAOS sets how many swaps; 100% shuffles every chop", "Move hits to new steps (downbeats are favoured)",
        "Add ratchets: 2, 3 or 4 quick repeats inside a step", "Play some chops backwards",
        "Shift some hits by a fourth, fifth or octave", "Shorten or lengthen hits" };
    for (int i = 0; i < 6; ++i)
    {
        auto& chip = flags[static_cast<std::size_t>(i)];
        chip.setButtonText(flagNames[static_cast<std::size_t>(i)]);
        chip.setTooltip(flagTips[static_cast<std::size_t>(i)]);
        chip.setClickingTogglesState(true);
        content.addAndMakeVisible(chip);
        chip.onClick = [this]
        {
            int bits = 0;
            for (int b = 0; b < 6; ++b)
                if (flags[static_cast<std::size_t>(b)].getToggleState()) bits |= 1 << b;
            processor.setRandomFlags(bits);
            pull();
        };
    }
    undoButton.onClick = [this] { processor.undo(); pull(); };

    // Step inspector
    previousChop.onClick = [this]
    {
        const auto count = std::max(1, view.session.slices.count);
        editSelected([count, this](chop::Step& s) { s.slice = s.slice < 0 ? selectedChop : (s.slice % count + count - 1) % count; });
        processor.previewChop(pattern()[static_cast<std::size_t>(selectedStep)].slice);
    };
    nextChop.onClick = [this]
    {
        const auto count = std::max(1, view.session.slices.count);
        editSelected([count, this](chop::Step& s) { s.slice = s.slice < 0 ? selectedChop : (s.slice % count + 1) % count; });
        processor.previewChop(pattern()[static_cast<std::size_t>(selectedStep)].slice);
    };
    gate.slider.setRange(10.0, 100.0, 1.0);
    gate.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    gate.slider.onDragStart = [this] { firstGateChange = true; };
    gate.onValue = [this]
    {
        const auto value = juce::roundToInt(gate.slider.getValue());
        editSelected([value](chop::Step& s) { s.gate = value; }, firstGateChange || !gate.slider.isMouseButtonDown());
        firstGateChange = false;
    };
    pitch.slider.setRange(-12.0, 12.0, 1.0);
    pitch.format = [](double v) { const auto i = juce::roundToInt(v); return (i > 0 ? "+" : "") + juce::String(i) + " st"; };
    pitch.slider.setDoubleClickReturnValue(true, 0.0);
    pitch.slider.onDragStart = [this] { firstPitchChange = true; };
    pitch.onValue = [this]
    {
        const auto value = juce::roundToInt(pitch.slider.getValue());
        editSelected([value](chop::Step& s) { s.pitch = value; }, firstPitchChange || !pitch.slider.isMouseButtonDown());
        firstPitchChange = false;
    };
    ratchet.setOptions({ "1", "2", "3", "4" }, { 1, 2, 3, 4 });
    ratchet.onChange = [this](int value) { editSelected([value](chop::Step& s) { s.ratchet = value; }); };
    reverse.setClickingTogglesState(true);
    reverse.onClick = [this] { const auto on = reverse.getToggleState(); editSelected([on](chop::Step& s) { s.reverse = on; }); };
    reverse.setTooltip("Play this step backwards");
    pin.setClickingTogglesState(true);
    pin.setTooltip("Pin this step for Dovetail. RANDOMIZE never changes it, and builds the rest of the bank around it.");
    pin.onClick = [this] { processor.togglePin(selectedStep); pull(); };
    rest.setClickingTogglesState(true);
    rest.setTooltip("Turn this step into a rest, or back into a hit");
    rest.onClick = [this]
    {
        const auto makeRest = rest.getToggleState();
        editSelected([makeRest, this](chop::Step& s) { s.slice = makeRest ? -1 : selectedChop; });
    };

    // Sound
    vintageMode.setOptions({ "CLEAN", "12-BIT 40K", "12-BIT 26K", "16-BIT WARM" }, { 0, 1, 2, 3 });
    vintageMode.setTooltip("Classic sampler character. 12-BIT 40K: punchy and warm, like the late-'80s drum samplers. "
                           "12-BIT 26K: gritty, with aliasing folding back in. 16-BIT WARM: clean but rounded, like the early-'90s ones.");
    vintageMode.onChange = [this](int value) { setParameter("vintage", static_cast<float>(value)); };
    drive.slider.setRange(0.0, 100.0, 1.0);
    drive.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    drive.slider.setTooltip("How hard the sound hits the vintage input stage");
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "drive", drive.slider);
    drive.slider.setDoubleClickReturnValue(true, 35.0);
    chokeMode.setOptions({ "CUT", "GATE", "POLY" }, { 0, 1, 2 });
    chokeMode.setTooltip("CUT: every hit cuts the last and rings until the next, like FL Studio's \"Cut by 1\" on every pad. "
                         "GATE: hits stop at their step gate, and MIDI note lengths count. POLY: hits ring over each other.");
    chokeMode.onChange = [this](int value) { setParameter("choke", static_cast<float>(value)); };
    alignKnob.slider.setRange(0.0, 100.0, 1.0);
    alignKnob.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    alignKnob.slider.setTooltip("Hit Align measures how late each chop's attack comes after its cut and starts it early, "
                                "so every hit lands on the grid whatever chop goes where. Keys and MIDI start right at the attack. "
                                "Lower it to keep some natural looseness.");
    alignAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "align", alignKnob.slider);
    alignKnob.slider.setDoubleClickReturnValue(true, 100.0);

    // Footer: MIDI root
    for (int note = 24; note <= 96; ++note)
        root.addItem(noteName(note), note + 1);
    root.setTooltip("MIDI note that plays Chop 1. Every other key plays the next chop up, wrapping around.");
    root.onChange = [this] { if (root.getSelectedId() > 0) setParameter("root", static_cast<float>(root.getSelectedId() - 1)); };

    setResizable(true, true);
    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio(static_cast<double>(designWidth) / designHeight);
        constrainer->setSizeLimits(designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 3 / 2, designHeight * 3 / 2);
    }
    setSize(designWidth, designHeight);
    pull();
    startTimerHz(30);
}

EpsilonChopEditor::~EpsilonChopEditor()
{
    stopTimer();
    chooser.reset();
    setLookAndFeel(nullptr);
}

void EpsilonChopEditor::setParameter(const juce::String& id, float plainValue)
{
    if (auto* parameter = processor.parameters.getParameter(id))
    {
        const auto normalised = parameter->convertTo0to1(plainValue);
        if (std::abs(parameter->getValue() - normalised) > 1.0e-6f)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalised);
            parameter->endChangeGesture();
        }
    }
}

void EpsilonChopEditor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Choose a sample", juce::File {},
                                                  "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    juce::Component::SafePointer<EpsilonChopEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& result)
        {
            if (safe != nullptr && result.getResult().existsAsFile())
            {
                safe->processor.loadFile(result.getResult());
                safe->pull();
            }
        });
}

void EpsilonChopEditor::selectStep(int index)
{
    selectedStep = juce::jlimit(0, chop::stepCount - 1, index);
    const auto& step = pattern()[static_cast<std::size_t>(selectedStep)];
    if (step.slice >= 0 && view.session.slices.count > 0)
        selectChop(step.slice % view.session.slices.count);
    syncControls();
    ribbon.repaint();
}

void EpsilonChopEditor::selectChop(int index)
{
    if (selectedChop == index)
        return;
    selectedChop = index;
    waveform.contentChanged();
}

void EpsilonChopEditor::setStep(int index, chop::Step step, bool remember)
{
    processor.setStep(index, step, remember);
    pull();
}

void EpsilonChopEditor::editSelected(const std::function<void(chop::Step&)>& change, bool remember)
{
    auto step = pattern()[static_cast<std::size_t>(selectedStep)];
    change(step);
    if (step.slice >= 0 && view.session.slices.count > 0)
        selectChop(step.slice % view.session.slices.count);
    setStep(selectedStep, step, remember);
}

void EpsilonChopEditor::pull()
{
    view = processor.editorState();
    seenRevision = processor.revisionNow();
    seenStatus = processor.statusRevision();
    selectedChop = juce::jlimit(0, std::max(0, view.session.slices.count - 1), selectedChop);
    syncControls();
    waveform.contentChanged();
    ribbon.contentChanged();
    content.repaint(0, 0, designWidth, 72);
    content.repaint(heroPanel.toNearestInt());
    content.repaint(ribbonPanel.toNearestInt());
    content.repaint(stepPanel.toNearestInt());
    content.repaint(variationPanel.toNearestInt());
    content.repaint(0, designHeight - 34, designWidth, 34);
}

void EpsilonChopEditor::syncControls()
{
    const auto& edit = view.session.edit;
    const auto hasSample = view.session.sample != nullptr;
    mode.setSelected(static_cast<int>(edit.mode));

    if (edit.mode == chop::SliceMode::transient)
        density.setOptions({ "8", "16", "32" }, { 8, 16, 32 });
    else
        density.setOptions({ "4", "8", "16", "32" }, { 4, 8, 16, 32 });
    density.setSelected(edit.density);
    division.setSelected(edit.beatDivision);
    if (!sensitivity.isMouseButtonDown())
        sensitivity.setValue(edit.sensitivity, juce::dontSendNotification);
    snap.setToggleState(edit.snapZero, juce::dontSendNotification);
    snap.setEnabled(edit.mode != chop::SliceMode::manual);
    if (hasSample)
        loopBpm.setValue(chop::sampleBpm(*view.session.sample, edit));
    loopBpm.tag = hasSample ? (edit.sampleBpmTenths > 0 ? "SET" : "AUTO") : "";
    loopBpm.editable = hasSample;
    loopBpm.repaint();
    layoutContext();

    banks.setSelected(edit.bank);
    const auto hits = chop::hitCount(pattern());
    clearButton.setEnabled(hits > 0);
    inOrderButton.setEnabled(hasSample);
    dragChops.setEnabled(hasSample && view.session.slices.count > 0);
    dragLoop.setEnabled(hasSample && hits > 0);

    for (int b = 0; b < 6; ++b)
        flags[static_cast<std::size_t>(b)].setToggleState((edit.randomFlags & (1 << b)) != 0, juce::dontSendNotification);
    if (!chaos.slider.isMouseButtonDown())
        chaos.slider.setValue(edit.chaos, juce::dontSendNotification);
    if (!fitKnob.slider.isMouseButtonDown())
        fitKnob.slider.setValue(edit.fitAmount, juce::dontSendNotification);
    const auto anchor = edit.anchor[static_cast<std::size_t>(edit.bank)];
    const auto dovetail = anchor >= 0 && pattern()[static_cast<std::size_t>(anchor)].slice >= 0 && edit.fitAmount > 0;
    dice.setEnabled(hasSample && edit.randomFlags != 0);
    static const std::array<const char*, 6> flagWords { "Swap", "Rhythm", "Stutter", "Reverse", "Pitch", "Gate" };
    juce::StringArray active;
    for (int b = 0; b < 6; ++b)
        if ((edit.randomFlags & (1 << b)) != 0)
            active.add(flagWords[static_cast<std::size_t>(b)]);
    dice.setSubtitle(!hasSample ? "Load a sample first"
                     : (dovetail ? juce::String::fromUTF8("Dovetail · step ") + twoDigits(anchor + 1)
                                 : (active.isEmpty() ? "Pick what to change below" : active.joinIntoString(juce::String::fromUTF8("  ·  ")))));
    undoButton.setEnabled(view.canUndo);

    const auto& step = pattern()[static_cast<std::size_t>(selectedStep)];
    const auto isHit = step.slice >= 0;
    rest.setToggleState(!isHit, juce::dontSendNotification);
    rest.setEnabled(hasSample);
    for (auto* c : std::initializer_list<juce::Component*> { &gate, &pitch, &ratchet, &reverse })
        c->setEnabled(isHit && hasSample);
    previousChop.setEnabled(hasSample);
    nextChop.setEnabled(hasSample);
    if (!gate.slider.isMouseButtonDown())
        gate.slider.setValue(step.gate, juce::dontSendNotification);
    if (!pitch.slider.isMouseButtonDown())
        pitch.slider.setValue(step.pitch, juce::dontSendNotification);
    gate.repaint();
    pitch.repaint();
    ratchet.setSelected(step.ratchet);
    reverse.setToggleState(step.reverse, juce::dontSendNotification);
    pin.setToggleState(anchor == selectedStep, juce::dontSendNotification);
    pin.setEnabled(hasSample && (isHit || anchor == selectedStep));
    vintageMode.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("vintage")->load()));
    chokeMode.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("choke")->load()));

    const auto rateIndex = juce::roundToInt(processor.parameters.getRawParameterValue("rate")->load());
    rate.setSelected(rateIndex);
    root.setSelectedId(juce::roundToInt(processor.parameters.getRawParameterValue("root")->load()) + 1, juce::dontSendNotification);
}

bool EpsilonChopEditor::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& details,
                                                            juce::StringArray& files, bool& canMoveFiles)
{
    const auto description = details.description.toString();
    if (!description.startsWith("chop:"))
        return false;
    const auto file = processor.exportChop(description.fromFirstOccurrenceOf("chop:", false, false).getIntValue());
    if (!file.existsAsFile())
        return false;
    files.add(file.getFullPathName());
    canMoveFiles = false;
    return true;
}

void EpsilonChopEditor::layoutContext()
{
    const auto m = view.session.edit.mode;
    const auto showDensity = m == chop::SliceMode::transient || m == chop::SliceMode::grid || m == chop::SliceMode::random;
    sensitivity.setVisible(m == chop::SliceMode::transient);
    density.setVisible(showDensity);
    division.setVisible(m == chop::SliceMode::beat);
    rechop.setVisible(m == chop::SliceMode::random);
    if (m == chop::SliceMode::transient)
        density.setBounds(768, 90, 96, 28);
    else
        density.setBounds(548, 90, 168, 28);
}

void EpsilonChopEditor::resized()
{
    scale = static_cast<float>(getWidth()) / designWidth;
    content.setTransform(juce::AffineTransform::scale(scale));

    load.setBounds(978, 20, 122, 34);
    output.setBounds(880, 10, 84, 58);

    mode.setBounds(36, 90, 430, 28);
    sensitivity.setBounds(566, 90, 132, 28);
    division.setBounds(560, 90, 156, 28);
    rechop.setBounds(726, 90, 86, 28);
    snap.setBounds(872, 90, 76, 28);
    loopBpm.setBounds(956, 84, 128, 40);
    waveform.setBounds(36, 132, 1048, 230);

    banks.setBounds(166, 400, 132, 26);
    dragChops.setBounds(612, 400, 96, 26);
    dragLoop.setBounds(714, 400, 96, 26);
    grooves.setBounds(818, 400, 116, 26);
    inOrderButton.setBounds(942, 400, 72, 26);
    clearButton.setBounds(1020, 400, 64, 26);
    ribbon.setBounds(36, 436, 1048, 96);

    play.setBounds(36, 586, 88, 88);
    tempo.setBounds(140, 574, 130, 44);
    rate.setBounds(140, 634, 130, 26);
    hostSync.setBounds(140, 670, 104, 26);
    tempoFit.setBounds(250, 670, 114, 26);
    swing.setBounds(282, 574, 84, 86);

    dice.setBounds(408, 578, 152, 66);
    chaos.setBounds(566, 570, 88, 86);
    fitKnob.setBounds(656, 570, 88, 86);
    for (int i = 0; i < 6; ++i)
        flags[static_cast<std::size_t>(i)].setBounds(408 + i * 56, 654, 52, 24);
    undoButton.setBounds(408, 686, 64, 20);

    previousChop.setBounds(1018, 574, 28, 26);
    nextChop.setBounds(1054, 574, 28, 26);
    gate.setBounds(778, 612, 72, 86);
    pitch.setBounds(852, 612, 72, 86);
    ratchet.setBounds(936, 628, 148, 26);
    reverse.setBounds(936, 666, 46, 26);
    rest.setBounds(986, 666, 46, 26);
    pin.setBounds(1036, 666, 48, 26);

    vintageMode.setBounds(36, 760, 400, 30);
    drive.setBounds(446, 728, 80, 84);
    chokeMode.setBounds(548, 760, 210, 30);
    alignKnob.setBounds(770, 728, 84, 84);

    root.setBounds(96, designHeight - 30, 72, 22);
    layoutContext();
}

void EpsilonChopEditor::paint(juce::Graphics& g)
{
    g.fillAll(ink0);
}

void EpsilonChopEditor::paintHeader(juce::Graphics& g)
{
    // Epsilon Sounds product mark (shared design system)
    epsilon_ui::drawProductMark(g, { 24.0f, 21.0f }, 1.0f, epsilon_ui::Tier::Epsilon, "Chop",
                                juce::String::fromUTF8("Slice  \xc2\xb7  shuffle  \xc2\xb7  flip"),
                                [](float h, bool b) { return sans(h, b); });

    // Sample pill
    const auto pill = juce::Rectangle<float>(322.0f, 20.0f, 540.0f, 34.0f);
    g.setColour(ink1);
    g.fillRoundedRectangle(pill, 17.0f);
    g.setColour(line);
    g.drawRoundedRectangle(pill.reduced(0.5f), 17.0f, 1.0f);
    const auto* sample = view.session.sample.get();
    auto inside = pill.reduced(16.0f, 0.0f).toNearestInt();
    if (view.status.isNotEmpty() || sample == nullptr)
    {
        g.setColour(view.loading ? live : dim);
        g.setFont(sans(12.5f, view.loading));
        g.drawText(view.status.isNotEmpty() ? view.status : "No sample loaded", inside, juce::Justification::centred);
    }
    else
    {
        g.setColour(chopColour(0));
        g.fillEllipse(static_cast<float>(inside.getX()), pill.getCentreY() - 3.5f, 7.0f, 7.0f);
        inside.removeFromLeft(16);
        const auto info = juce::String(sample->length() / sample->sampleRate, 2) + juce::String::fromUTF8(" s   ·   ")
                          + juce::String(sample->sampleRate / 1000.0, 1) + juce::String::fromUTF8(" kHz   ·   ")
                          + (sample->channels == 2 ? "STEREO" : "MONO") + juce::String::fromUTF8("   ·   ")
                          + juce::String(view.session.slices.count) + " CHOPS";
        g.setFont(sans(11.0f, true));
        const auto infoWidth = juce::GlyphArrangement::getStringWidthInt(sans(11.0f, true), info);
        g.setColour(dim);
        g.drawText(info, inside, juce::Justification::centredRight);
        g.setColour(text);
        g.setFont(sans(13.5f, true));
        g.drawFittedText(juce::String(sample->name), inside.withTrimmedRight(infoWidth + 14), juce::Justification::centredLeft, 1);
    }
}

void EpsilonChopEditor::paintPanels(juce::Graphics& g)
{
    drawPanel(g, heroPanel);
    drawPanel(g, ribbonPanel);
    drawPanel(g, transportPanel, "TRANSPORT");
    drawPanel(g, variationPanel, "VARIATION");
    drawPanel(g, stepPanel, "STEP");
    drawPanel(g, soundPanel);
    caption(g, "VINTAGE", { 36, 740, 120, 14 });
    caption(g, "CHOKE", { 548, 740, 120, 14 });
    g.setColour(text);
    g.setFont(sans(11.0f, true));
    g.drawText("PLAY FROM YOUR KEYBOARD", 880, 744, 210, 16, juce::Justification::centredLeft);
    g.setColour(dim);
    g.setFont(mono(11.0f));
    g.drawText(juce::String::fromUTF8("1 – 0   chops 01 – 10"), 880, 764, 210, 16, juce::Justification::centredLeft);
    g.drawText(juce::String::fromUTF8("Q – P   chops 11 – 20"), 880, 782, 210, 16, juce::Justification::centredLeft);

    // Context captions in the chop bar
    const auto m = view.session.edit.mode;
    if (m == chop::SliceMode::transient)
    {
        caption(g, "SENSITIVITY", { 484, 90, 92, 28 });
        g.setColour(text);
        g.setFont(mono(11.0f));
        g.drawText(juce::String(view.session.edit.sensitivity), 702, 90, 30, 28, juce::Justification::centredLeft);
        caption(g, "MAX", { 734, 90, 32, 28 });
    }
    else if (m == chop::SliceMode::beat)
        caption(g, "DIVISION", { 484, 90, 76, 28 });
    else if (m == chop::SliceMode::grid || m == chop::SliceMode::random)
        caption(g, "CHOPS", { 484, 90, 64, 28 });
    else
    {
        g.setColour(dim);
        g.setFont(sans(11.5f));
        g.drawText(juce::String::fromUTF8("Double-click to add a cut   ·   drag to move   ·   right-click to remove"), 484, 90, 380, 28, juce::Justification::centredLeft);
    }

    // Ribbon header
    g.setColour(text);
    g.setFont(sans(13.0f, true));
    g.drawText("ARRANGEMENT", 36, 400, 120, 26, juce::Justification::centredLeft);
    g.setColour(faint);
    g.setFont(sans(11.0f));
    g.drawFittedText(juce::String::fromUTF8("Drag chops onto steps  ·  scroll to change  ·  PIN, then roll"),
                     312, 400, 292, 26, juce::Justification::centredLeft, 1, 0.9f);

    // Transport readout under the play button
    const auto synced = processor.hostSynced();
    const auto running = processor.isPlaying() || synced;
    g.setColour(running ? live : dim);
    g.setFont(sans(9.5f, true));
    g.drawText(synced ? "HOST" : (processor.isPlaying() ? "PLAYING" : "PREVIEW"), 36, 680, 88, 14, juce::Justification::centred);
    caption(g, "RATE", { 142, 620, 60, 12 });

    // Undo hint
    g.setColour(faint);
    g.setFont(sans(10.5f));
    if (view.lastRoll.isNotEmpty())
    {
        g.setColour(text);
        g.setFont(mono(10.5f));
        g.drawFittedText(view.lastRoll, 480, 686, 266, 20, juce::Justification::centredLeft, 1, 0.75f);
    }
    else
        g.drawText("Every roll can be undone", 480, 686, 240, 20, juce::Justification::centredLeft);

    // Step inspector header
    const auto& step = pattern()[static_cast<std::size_t>(selectedStep)];
    g.setColour(text);
    g.setFont(mono(20.0f));
    g.drawText(twoDigits(selectedStep + 1), 780, 572, 40, 30, juce::Justification::centredLeft);
    g.setColour(dim);
    g.setFont(sans(10.0f, true));
    g.drawText("STEP", 816, 574, 40, 26, juce::Justification::centredLeft);
    const auto count = view.session.slices.count;
    const auto shown = step.slice >= 0 ? (count > 0 ? step.slice % count : step.slice) : -1;
    const auto chip = juce::Rectangle<float>(898.0f, 574.0f, 112.0f, 26.0f);
    g.setColour(shown >= 0 ? chopColour(shown) : ink2);
    g.fillRoundedRectangle(chip, 8.0f);
    g.setColour(shown >= 0 ? ink0 : faint);
    g.setFont(sans(11.5f, true));
    g.drawText(shown >= 0 ? "CHOP " + twoDigits(shown + 1) : "REST", chip, juce::Justification::centred);
    caption(g, "RATCHET", { 936, 612, 80, 14 });
}

void EpsilonChopEditor::paintFooter(juce::Graphics& g)
{
    const auto y = designHeight - 30;
    caption(g, "MIDI ROOT", { 24, y, 70, 22 });
    g.setColour(faint);
    g.setFont(sans(11.0f));
    g.drawText("= Chop 01. Every key above plays the next chop, wrapping around.", 176, y, 420, 22, juce::Justification::centredLeft);

    juce::String clock;
    const auto synced = processor.hostSynced();
    if (synced)
        clock = juce::String::fromUTF8("LOCKED TO HOST  ·  ") + juce::String(processor.tempo(), 1) + " BPM";
    else if (processor.isPlaying())
        clock = juce::String::fromUTF8("INTERNAL CLOCK  ·  ") + juce::String(processor.tempo(), 1) + " BPM";
    else
        clock = juce::String("v" JucePlugin_VersionString) + juce::String::fromUTF8("  ·  EPSILON SOUNDS");
   #if JUCE_DEBUG && !defined(__OPTIMIZE__)
    clock = juce::String::fromUTF8("SLOW UNOPTIMISED BUILD  ·  ") + clock;
   #endif
    g.setColour(synced || processor.isPlaying() ? live : faint);
    g.setFont(sans(10.0f, true));
    g.drawText(clock, designWidth - 444, y, 420, 22, juce::Justification::centredRight);
}

void EpsilonChopEditor::timerCallback()
{
    if (processor.revisionNow() != seenRevision || processor.statusRevision() != seenStatus)
        pull();

    const auto playing = processor.isPlaying();
    const auto synced = processor.hostSynced();
    const auto hostHasTempo = processor.hostHasTempo();
    play.setState(playing, synced);
    if (playing != seenPlaying || synced != seenSynced || hostHasTempo != seenHostTempo)
    {
        seenPlaying = playing;
        seenSynced = synced;
        seenHostTempo = hostHasTempo;
        content.repaint(transportPanel.toNearestInt());
        content.repaint(0, designHeight - 34, designWidth, 34);
    }

    // Tempo: follow the host when it reports one, otherwise the internal tempo is draggable.
    tempo.editable = !hostHasTempo;
    tempo.tag = hostHasTempo ? "HOST" : "INTERNAL";
    tempo.tagLive = synced;
    const auto shownTempo = hostHasTempo ? processor.tempo() : static_cast<double>(processor.parameters.getRawParameterValue("tempo")->load());
    tempo.setValue(shownTempo);
    if (std::abs(shownTempo - seenTempo) > 1.0e-6)
    {
        seenTempo = shownTempo;
        tempo.repaint();
    }
    rate.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("rate")->load()));
    vintageMode.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("vintage")->load()));
    chokeMode.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("choke")->load()));
    if (const auto a = alignAmount(); std::abs(a - seenAlign) > 1.0e-4f)
    {
        // Hit markers appear and disappear with Hit Align.
        seenAlign = a;
        waveform.contentChanged();
        ribbon.contentChanged();
    }

    const auto step = processor.playStep();
    const auto frame = processor.playFrame();
    const auto slice = processor.playSlice();
    if (frame != seenFrame || slice != seenSlice)
    {
        seenFrame = frame;
        seenSlice = slice;
        waveform.updatePlayhead(frame, slice);
    }
    if (step != seenStep)
    {
        ribbon.stepMoved(seenStep, step);
        seenStep = step;
    }
}

float EpsilonChopEditor::alignAmount() const
{
    return processor.parameters.getRawParameterValue("align")->load() * 0.01f;
}

void EpsilonChopEditor::mouseDown(const juce::MouseEvent&)
{
    // Clicking anywhere in the plugin gives it the keyboard, so 1-0 / Q-P play chops.
    grabKeyboardFocus();
}

bool EpsilonChopEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
        return false;
    const auto typed = juce::CharacterFunctions::toUpperCase(key.getTextCharacter());
    const auto code = juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    for (std::size_t i = 0; i < chopKeys.size(); ++i)
    {
        if (typed != chopKeys[i] && code != chopKeys[i])
            continue;
        const auto chopIndex = static_cast<int>(i);
        if (view.session.sample == nullptr || chopIndex >= view.session.slices.count)
            return true;
        if (heldKeys[i] == 0)   // ignore auto-repeat
        {
            heldKeys[i] = key.getKeyCode();
            processor.keyDown(chopIndex);
            selectChop(chopIndex);
        }
        return true;
    }
    return false;
}

bool EpsilonChopEditor::keyStateChanged(bool)
{
    auto handled = false;
    for (std::size_t i = 0; i < heldKeys.size(); ++i)
        if (heldKeys[i] != 0 && !juce::KeyPress::isKeyCurrentlyDown(heldKeys[i]))
        {
            heldKeys[i] = 0;
            processor.keyUp(static_cast<int>(i));
            handled = true;
        }
    return handled;
}

bool EpsilonChopEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const auto& path) { return supportedFile(juce::File(path)); });
}

void EpsilonChopEditor::fileDragEnter(const juce::StringArray&, int, int)
{
    draggingFile = true;
    content.repaint();
}

void EpsilonChopEditor::fileDragExit(const juce::StringArray&)
{
    draggingFile = false;
    content.repaint();
}

void EpsilonChopEditor::filesDropped(const juce::StringArray& files, int, int)
{
    draggingFile = false;
    const auto found = std::find_if(files.begin(), files.end(),
                                   [](const auto& path) { return supportedFile(juce::File(path)); });
    if (found != files.end())
        processor.loadFile(juce::File(*found));
    pull();
    content.repaint();
}
