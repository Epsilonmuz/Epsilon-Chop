#include "MobileEditor.h"

#include <cmath>

namespace mobile
{
using namespace theme;

namespace
{
juce::String two(int n) { return juce::String(n).paddedLeft('0', 2); }

void label(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r,
           juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour(dim);
    g.setFont(sans(10.5f, true).withExtraKerningFactor(0.12f));
    g.drawText(s, r, j);
}

// The notch and home bar: the layout stays inside the screen's safe area.
juce::BorderSize<int> safeInsets()
{
   #if JUCE_IOS
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return d->safeAreaInsets;
   #endif
    return {};
}
}

//==============================================================================
// Wave

void Wave::render(float px)
{
    cacheScale = px;
    cache = juce::Image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(getWidth() * px)),
                        juce::jmax(1, juce::roundToInt(getHeight() * px)), true);
    juce::Graphics g(cache);
    g.addTransform(juce::AffineTransform::scale(px));
    auto r = getLocalBounds().toFloat();
    g.setColour(ink2);
    g.fillRoundedRectangle(r, 10.0f);
    const auto& s = editor.view.session;
    if (s.sample == nullptr)
    {
        g.setColour(dim);
        g.setFont(sans(15.0f, true));
        g.drawText("Tap LOAD to pick a sample", r, juce::Justification::centred);
        g.setFont(sans(12.0f));
        g.setColour(faint);
        g.drawText("Files, voice memos, AirDrop: WAV, AIFF, MP3, M4A", r.withTrimmedTop(44.0f), juce::Justification::centred);
        return;
    }
    const auto& sample = *s.sample;
    const auto& sl = s.slices;
    const auto frames = std::max(1, sample.length());
    const auto mid = r.getCentreY();
    const auto half = r.getHeight() * 0.42f;
    for (int c = 0; c < sl.count; ++c)
    {
        const auto x0 = r.getX() + r.getWidth() * (float) sl.edge[(size_t) c] / (float) frames;
        const auto x1 = r.getX() + r.getWidth() * (float) sl.edge[(size_t) c + 1] / (float) frames;
        const auto colour = chopColour(c);
        g.setColour(colour.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(x0, r.getY(), x1 - x0, r.getHeight()));
        g.setColour(colour);
        for (float x = x0; x < x1; x += 1.0f)
        {
            const auto bin = juce::jlimit(0, chop::waveformBins - 1, (int) ((x - r.getX()) / r.getWidth() * chop::waveformBins));
            const auto& p = sample.waveform[(size_t) bin];
            g.fillRect(x, mid - p.high * half, 1.0f, std::max(1.0f, (p.high - p.low) * half));
        }
        g.setColour(colour.withAlpha(0.8f));
        g.fillRect(x0, r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);
        if (x1 - x0 > 16.0f)
        {
            g.setFont(mono(9.5f));
            g.drawText(two(c + 1), juce::Rectangle<float>(x0 + 2.0f, r.getY() + 3.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
        }
    }
}

void Wave::setPlaying(int slice, int frame)
{
    if (slice == shownSlice && std::abs(frame - shownFrame) < 64)
        return;
    shownSlice = slice;
    shownFrame = frame;
    repaint();
}

void Wave::paint(juce::Graphics& g)
{
    const auto px = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (!cache.isValid() || std::abs(px - cacheScale) > 0.01f)
        render(px);
    g.drawImage(cache, getLocalBounds().toFloat());
    const auto& s = editor.view.session;
    if (s.sample == nullptr)
        return;
    const auto frames = std::max(1, s.sample->length());
    auto r = getLocalBounds().toFloat();
    const auto sel = editor.selectedChop;
    if (sel >= 0 && sel < s.slices.count)
    {
        const auto x0 = r.getWidth() * (float) s.slices.edge[(size_t) sel] / (float) frames;
        const auto x1 = r.getWidth() * (float) s.slices.edge[(size_t) sel + 1] / (float) frames;
        g.setColour(chopColour(sel));
        g.fillRect(juce::Rectangle<float>(x0, r.getBottom() - 3.0f, x1 - x0, 3.0f));
    }
    if (shownFrame >= 0)
    {
        g.setColour(live);
        g.fillRect(r.getWidth() * (float) shownFrame / (float) frames - 0.75f, r.getY(), 1.5f, r.getHeight());
    }
}

int Wave::sliceAt(float x) const
{
    const auto& s = editor.view.session;
    if (s.sample == nullptr || s.slices.count <= 0)
        return -1;
    const auto frame = (int) (x / (float) getWidth() * (float) s.sample->length());
    for (int c = 0; c < s.slices.count; ++c)
        if (frame < s.slices.edge[(size_t) c + 1])
            return c;
    return s.slices.count - 1;
}

void Wave::mouseDown(const juce::MouseEvent& e)
{
    if (editor.view.session.sample == nullptr)
        return;
    if (const auto c = sliceAt(e.position.x); c >= 0)
        editor.auditionChop(c);
}

//==============================================================================
// Pad

void Pad::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(3.0f);
    const auto colour = exists ? chopColour(chop) : faint;
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.5f), 10.0f);
    g.setGradientFill(juce::ColourGradient(held || lit ? ink4 : ink3, 0.0f, r.getY(), ink2, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(lit ? colour : (held ? colour.withAlpha(0.7f) : line));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, lit ? 2.0f : 1.0f);
    if (lit)
    {
        g.setColour(colour.withAlpha(0.18f));
        g.fillRoundedRectangle(r.reduced(1.0f), 9.0f);
    }
    // colour bar = this chop's colour on the waveform and the steps
    g.setColour(exists ? colour : faint.withAlpha(0.3f));
    g.fillRoundedRectangle(r.getX() + 10.0f, r.getBottom() - 9.0f, r.getWidth() - 20.0f, 3.0f, 1.5f);
    g.setColour(exists ? text : faint);
    g.setFont(mono(17.0f));
    g.drawText(two(chop + 1), r.withTrimmedBottom(10.0f), juce::Justification::centred);
}

void Pad::mouseDown(const juce::MouseEvent&)
{
    if (!exists)
        return;
    held = true;
    editor.processor.keyDown(chop);
    editor.selectedChop = chop;
    repaint();
}

void Pad::mouseUp(const juce::MouseEvent&)
{
    if (!held)
        return;
    held = false;
    editor.processor.keyUp(chop);
    repaint();
}

//==============================================================================
// Steps

juce::Rectangle<float> Steps::cell(int index) const
{
    const float gap = 6.0f;
    const auto w = (getWidth() - 7.0f * gap) / 8.0f;
    const auto h = (getHeight() - gap) / 2.0f;
    const auto row = index / 8, col = index % 8;
    return { col * (w + gap), row * (h + gap), w, h };
}

void Steps::paint(juce::Graphics& g)
{
    const auto& p = editor.pattern();
    const auto count = editor.view.session.slices.count;
    const auto anchor = editor.view.session.edit.anchor[(size_t) editor.view.session.edit.bank];
    for (int i = 0; i < chop::stepCount; ++i)
    {
        auto r = cell(i);
        const auto& st = p[(size_t) i];
        const auto hit = st.slice >= 0 && count > 0;
        const auto colour = hit ? chopColour(st.slice % count) : faint;
        g.setColour(i == playing ? ink4 : ink2);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(i == editor.selectedStep ? text : (i == playing ? live : line));
        g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, i == editor.selectedStep ? 1.6f : 1.0f);
        if (i % 4 == 0)
        {
            g.setColour(faint);
            g.setFont(mono(8.0f));
            g.drawText(juce::String(i / 4 + 1), r.reduced(4.0f, 2.0f), juce::Justification::topLeft);
        }
        if (hit)
        {
            g.setColour(colour);
            g.fillRoundedRectangle(r.getX() + 6.0f, r.getBottom() - 7.0f, r.getWidth() - 12.0f, 3.0f, 1.5f);
            g.setFont(mono(13.0f));
            g.drawText(two(st.slice % count + 1), r.withTrimmedBottom(6.0f), juce::Justification::centred);
            juce::String marks;
            if (st.reverse) marks << "R";
            if (st.ratchet > 1) marks << "x" << st.ratchet;
            if (st.pitch != 0) marks << (st.pitch > 0 ? "+" : "") << st.pitch;
            if (marks.isNotEmpty())
            {
                g.setColour(dim);
                g.setFont(mono(7.5f));
                g.drawText(marks, r.reduced(3.0f, 2.0f), juce::Justification::topRight);
            }
        }
        else
        {
            g.setColour(faint);
            g.fillRect(r.getCentreX() - 5.0f, r.getCentreY(), 10.0f, 1.5f);
        }
        if (i == anchor)
        {
            g.setColour(live);
            g.fillEllipse(r.getRight() - 9.0f, r.getY() + 4.0f, 5.0f, 5.0f);
        }
    }
}

void Steps::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < chop::stepCount; ++i)
        if (cell(i).contains(e.position))
        {
            editor.selectStep(i);
            return;
        }
}

//==============================================================================
// Editor

Editor::Editor(EpsilonChopProcessor& p) : juce::AudioProcessorEditor(p), processor(p)
{
    setLookAndFeel(&look);
    setOpaque(true);
   #if JUCE_IOS
    juce::Desktop::getInstance().setOrientationsEnabled(juce::Desktop::upright);   // phone held upright
   #endif
    addAndMakeVisible(content);
    content.painter = [this](juce::Graphics& g) { paintContent(g); };

    for (auto* c : std::initializer_list<juce::Component*> { &wave, &load, &mode, &density, &division, &tabs, &play, &tempo, &dice })
        content.addChildComponent(c);
    for (auto* c : std::initializer_list<juce::Component*> { &wave, &load, &mode, &tabs, &play, &tempo, &dice })
        c->setVisible(true);
    for (int i = 0; i < 16; ++i)
    {
        pads.push_back(std::make_unique<Pad>(*this, i));
        content.addChildComponent(*pads.back());
    }
    for (auto* c : std::initializer_list<juce::Component*> {
             &padBanks, &banks, &rate, &steps, &prevChop, &nextChop, &rest, &pin, &reverse, &inOrder, &clear, &undo,
             &chaos, &dovetail, &swing, &pitch, &output, &drive, &align, &pitchMode, &vintage, &choke,
             &shareLoop, &shareChops, &shareChop, &loopBars })
        content.addChildComponent(c);

    load.onClick = [this] { chooseSample(); };

    mode.setOptions({ "TRANSIENT", "BEAT", "GRID", "RANDOM" },
                    { (int) chop::SliceMode::transient, (int) chop::SliceMode::beat, (int) chop::SliceMode::grid,
                      (int) chop::SliceMode::random });
    mode.fontHeight = 10.5f;
    mode.reportReselect = true;
    mode.onChange = [this](int v)
    {
        if (v == (int) chop::SliceMode::random && view.session.edit.mode == chop::SliceMode::random)
            processor.rechop();
        else
            processor.setMode((chop::SliceMode) v);
        pull();
    };
    density.fontHeight = 10.5f;
    density.onChange = [this](int v) { processor.setDensity(v); pull(); };
    division.setOptions({ "1/4", "1/8", "1/16" }, { 1, 2, 4 });
    division.fontHeight = 10.5f;
    division.onChange = [this](int v) { processor.setBeatDivision(v); pull(); };

    play.onClick = [this] { processor.setPlaying(!processor.isPlaying()); };
    tempo.caption = "TEMPO";
    tempo.unit = "BPM";
    tempo.minimum = 40.0;
    tempo.maximum = 240.0;
    tempo.perPixel = 0.25;
    tempo.valueHeight = 17.0f;
    tempo.onChange = [this](double v) { setParameter("tempo", (float) v); };
    tempo.onReset = [this] { setParameter("tempo", 120.0f); };
    dice.onClick = [this]
    {
        processor.randomize();
        if (!processor.isPlaying())
            processor.setPlaying(true);
        pull();
    };

    tabs.setOptions({ "PADS", "PATTERN", "SOUND", "EXPORT" }, { 0, 1, 2, 3 });
    tabs.fontHeight = 11.5f;
    tabs.onChange = [this](int v) { showPage(v); };

    // PADS
    padBanks.setOptions({ "CHOPS 1-16", "17-32" }, { 0, 1 });
    padBanks.fontHeight = 10.5f;
    padBanks.onChange = [this](int v) { padBank = v; syncControls(); };

    // PATTERN
    banks.setOptions({ "A", "B", "C", "D" }, { 0, 1, 2, 3 });
    banks.onChange = [this](int v) { processor.setBank(v); pull(); };
    rate.setOptions({ "1/8", "1/16", "1/32" }, { 0, 1, 2 });
    rate.onChange = [this](int v) { setParameter("rate", (float) v); };
    prevChop.onClick = [this] { editStep([this](chop::Step& s) { const auto n = std::max(1, view.session.slices.count);
                                                                 s.slice = s.slice < 0 ? n - 1 : (s.slice + n - 1) % n; }); };
    nextChop.onClick = [this] { editStep([this](chop::Step& s) { const auto n = std::max(1, view.session.slices.count);
                                                                 s.slice = s.slice < 0 ? 0 : (s.slice + 1) % n; }); };
    rest.onClick = [this] { editStep([](chop::Step& s) { s.slice = -1; }); };
    reverse.onClick = [this] { editStep([](chop::Step& s) { s.reverse = !s.reverse; }); };
    pin.onClick = [this] { processor.togglePin(selectedStep); pull(); };
    inOrder.onClick = [this] { processor.arrangeInOrder(); pull(); };
    clear.onClick = [this] { processor.clearPattern(); pull(); };
    undo.onClick = [this] { processor.undo(); pull(); };
    for (auto* k : { &chaos, &dovetail })
    {
        k->slider.setRange(0.0, 100.0, 1.0);
        k->format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    }
    chaos.slider.onDragEnd = [this] { processor.setChaos(juce::roundToInt(chaos.slider.getValue())); pull(); };
    dovetail.slider.onDragEnd = [this] { processor.setFitAmount(juce::roundToInt(dovetail.slider.getValue())); pull(); };
    swing.slider.setRange(0.0, 60.0, 1.0);
    swing.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    swingAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "swing", swing.slider);

    // SOUND
    pitch.slider.setRange(-24.0, 24.0, 0.01);
    pitch.format = [](double v)
    {
        const auto r = std::round(v * 10.0) / 10.0;
        return (r > 0.0 ? "+" : "") + juce::String(r, std::abs(r - std::round(r)) < 0.05 ? 0 : 1) + " st";
    };
    pitchAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "masterPitch", pitch.slider);
    pitch.slider.setDoubleClickReturnValue(true, 0.0);
    pitchMode.setOptions({ "MPC", "KEEP LENGTH" }, { 0, 1 });
    pitchMode.onChange = [this](int v) { setParameter("pitchMode", (float) v); };
    output.slider.setRange(-24.0, 6.0, 0.1);
    output.format = [](double v) { return juce::String(v, 1) + " dB"; };
    outputAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "output", output.slider);
    vintage.setOptions({ "CLEAN", "12-BIT 40K", "12-BIT 26K", "16 WARM" }, { 0, 1, 2, 3 });
    vintage.fontHeight = 10.5f;
    vintage.onChange = [this](int v) { setParameter("vintage", (float) v); };
    drive.slider.setRange(0.0, 100.0, 1.0);
    drive.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    driveAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "drive", drive.slider);
    choke.setOptions({ "CUT", "GATE", "POLY" }, { 0, 1, 2 });
    choke.onChange = [this](int v) { setParameter("choke", (float) v); };
    align.slider.setRange(0.0, 100.0, 1.0);
    align.format = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    alignAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "align", align.slider);

    // EXPORT
    loopBars.setOptions({ "1 PASS", "2", "4", "8" }, { 1, 2, 4, 8 });
    loopBars.setSelected(1);
    shareLoop.onClick = [this]
    {
        const auto f = processor.exportPattern(std::max(1, loopBars.getSelected()));
        share(f.existsAsFile() ? juce::StringArray { f.getFullPathName() } : juce::StringArray {});
    };
    shareChops.onClick = [this] { share(processor.exportAllChops()); };
    shareChop.onClick = [this]
    {
        const auto f = processor.exportChop(selectedChop);
        share(f.existsAsFile() ? juce::StringArray { f.getFullPathName() } : juce::StringArray {});
    };

    setResizable(true, false);
    setResizeLimits(320, 560, 2000, 3000);
    setSize(designWidth, designHeight);
    pull();
    showPage(0);
    startTimerHz(30);
}

Editor::~Editor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void Editor::setParameter(const juce::String& id, float value)
{
    if (auto* p = processor.parameters.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

void Editor::pull()
{
    view = processor.editorState();
    seenRevision = processor.revisionNow();
    selectedChop = juce::jlimit(0, std::max(0, view.session.slices.count - 1), selectedChop);
    wave.contentChanged();
    syncControls();
    content.repaint();
}

void Editor::syncControls()
{
    const auto& edit = view.session.edit;
    const auto hasSample = view.session.sample != nullptr;
    const auto chops = view.session.slices.count;
    mode.setSelected((int) edit.mode);
    if (edit.mode == chop::SliceMode::transient)
        density.setOptions({ "8", "16", "32" }, { 8, 16, 32 });
    else
        density.setOptions({ "4", "8", "16", "32" }, { 4, 8, 16, 32 });
    density.setSelected(edit.density);
    division.setSelected(edit.beatDivision);
    const auto beat = edit.mode == chop::SliceMode::beat;
    density.setVisible(!beat);
    division.setVisible(beat);
    tempo.setValue(processor.parameters.getRawParameterValue("tempo")->load());
    dice.setEnabled(hasSample);
    dice.setSubtitle(!hasSample ? "Load a sample first" : (edit.anchor[(size_t) edit.bank] >= 0 ? "Dovetail on" : "Flip the chops"));
    banks.setSelected(edit.bank);
    rate.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("rate")->load()));
    pitchMode.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("pitchMode")->load()));
    vintage.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("vintage")->load()));
    choke.setSelected(juce::roundToInt(processor.parameters.getRawParameterValue("choke")->load()));
    if (!chaos.slider.isMouseButtonDown()) chaos.slider.setValue(edit.chaos, juce::dontSendNotification);
    if (!dovetail.slider.isMouseButtonDown()) dovetail.slider.setValue(edit.fitAmount, juce::dontSendNotification);
    if (chops <= 16) padBank = 0;
    padBanks.setVisible(page == 0 && chops > 16);
    padBanks.setSelected(padBank);
    for (int i = 0; i < 16; ++i)
    {
        auto& p = *pads[(size_t) i];
        p.chop = padBank * 16 + i;
        p.exists = hasSample && p.chop < chops;
        p.repaint();
    }
    pin.setToggleState(edit.anchor[(size_t) edit.bank] == selectedStep, juce::dontSendNotification);
    reverse.setToggleState(pattern()[(size_t) selectedStep].reverse, juce::dontSendNotification);
    shareChops.setEnabled(hasSample);
    shareChop.setEnabled(hasSample);
    shareLoop.setEnabled(hasSample && chop::hitCount(pattern()) > 0);
    steps.repaint();
}

void Editor::auditionChop(int c)
{
    selectedChop = c;
    processor.previewChop(c);
    wave.repaint();
    content.repaint();
}

void Editor::selectStep(int s)
{
    selectedStep = juce::jlimit(0, chop::stepCount - 1, s);
    const auto& st = pattern()[(size_t) selectedStep];
    if (st.slice >= 0 && view.session.slices.count > 0)
        auditionChop(st.slice % view.session.slices.count);
    syncControls();
    content.repaint();
}

void Editor::editStep(const std::function<void(chop::Step&)>& change)
{
    if (view.session.sample == nullptr)
        return;
    auto st = pattern()[(size_t) selectedStep];
    change(st);
    processor.setStep(selectedStep, st);
    pull();
    if (st.slice >= 0 && view.session.slices.count > 0)
        auditionChop(st.slice % view.session.slices.count);
}

void Editor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Choose a sample", juce::File {},
                                                  "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf;*.ogg");
    juce::Component::SafePointer<Editor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& result)
        {
            if (safe == nullptr)
                return;
            const auto url = result.getURLResult();
            if (url.isEmpty())
                return;
            auto file = url.isLocalFile() ? url.getLocalFile() : juce::File();
            if (!file.existsAsFile())
            {
                // Files from iCloud or another app can arrive as a URL: copy them into the app first.
                const auto copy = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile(url.getFileName().isNotEmpty() ? url.getFileName() : juce::String("sample.wav"));
                if (auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)))
                {
                    copy.deleteFile();
                    juce::FileOutputStream out(copy);
                    if (out.openedOk())
                        out.writeFromInputStream(*in, -1);
                }
                file = copy;
            }
            if (file.existsAsFile())
            {
                safe->processor.loadFile(file);
                safe->pull();
            }
        });
}

void Editor::share(const juce::StringArray& files)
{
    if (files.isEmpty())
    {
        toast = "Nothing to export yet";
        toastTicks = 60;
        content.repaint();
        return;
    }
    toast = files.size() == 1 ? "Saved " + juce::File(files[0]).getFileName() : "Saved " + juce::String(files.size()) + " chops";
    toastTicks = 90;
    content.repaint();
   #if JUCE_IOS || JUCE_ANDROID
    juce::Array<juce::URL> urls;
    for (const auto& f : files)
        urls.add(juce::URL(juce::File(f)));
    shareBox = juce::ContentSharer::shareFilesScoped(urls, [](bool, const juce::String&) {}, this);
   #else
    juce::File(files[0]).revealToUser();
   #endif
}

void Editor::showPage(int p)
{
    page = juce::jlimit(0, 3, p);
    tabs.setSelected(page);
    for (auto& pad : pads) pad->setVisible(page == 0);
    padBanks.setVisible(page == 0 && view.session.slices.count > 16);
    for (auto* c : std::initializer_list<juce::Component*> { &banks, &rate, &steps, &prevChop, &nextChop, &rest, &pin, &reverse,
                                                              &inOrder, &clear, &undo, &chaos, &dovetail, &swing })
        c->setVisible(page == 1);
    for (auto* c : std::initializer_list<juce::Component*> { &pitch, &output, &drive, &align, &pitchMode, &vintage, &choke })
        c->setVisible(page == 2);
    for (auto* c : std::initializer_list<juce::Component*> { &shareLoop, &shareChops, &shareChop, &loopBars })
        c->setVisible(page == 3);
    layoutPage();
    content.repaint();
}

void Editor::resized()
{
    // Fit the 390 x 760 design inside the screen's safe area (notch, home bar), scaled to fit.
    const auto insets = safeInsets();
    auto area = insets.subtractedFrom(getLocalBounds());
    scale = std::min(area.getWidth() / (float) designWidth, area.getHeight() / (float) designHeight);
    const auto x = area.getX() + (area.getWidth() - designWidth * scale) * 0.5f;
    // A taller phone gives the extra height to the pads.
    extraHeight = std::max(0, (int) (area.getHeight() / scale) - designHeight);
    content.setBounds(0, 0, designWidth, designHeight + extraHeight);
    content.setTransform(juce::AffineTransform::scale(scale).translated(x, (float) area.getY()));

    load.setBounds(306, 8, 72, 32);
    wave.setBounds(12, 50, 366, 120);
    mode.setBounds(12, 178, 236, 30);
    density.setBounds(254, 178, 124, 30);
    division.setBounds(254, 178, 124, 30);
    play.setBounds(12, 216, 56, 56);
    tempo.setBounds(76, 218, 104, 52);
    dice.setBounds(188, 216, 190, 56);
    tabs.setBounds(12, 282, 366, 34);
    layoutPage();
}

void Editor::layoutPage()
{
    const juce::Rectangle<int> pageArea(12, 326, 366, 426 + extraHeight);
    // PADS: 4 x 4, MPC order (pad 1 bottom left)
    const auto top = padBanks.isVisible() ? 36 : 0;
    padBanks.setBounds(pageArea.getX(), pageArea.getY(), 180, 28);
    const auto w = pageArea.getWidth() / 4.0f, h = (pageArea.getHeight() - top) / 4.0f;
    for (int i = 0; i < 16; ++i)
    {
        const auto row = 3 - i / 4, col = i % 4;
        pads[(size_t) i]->setBounds(juce::Rectangle<float>(pageArea.getX() + col * w, pageArea.getY() + top + row * h, w, h).toNearestInt());
    }
    // PATTERN
    banks.setBounds(12, 326, 160, 30);
    rate.setBounds(218, 326, 160, 30);
    steps.setBounds(12, 366, 366, 112);
    prevChop.setBounds(96, 488, 40, 34);
    nextChop.setBounds(192, 488, 40, 34);
    rest.setBounds(244, 488, 64, 34);
    reverse.setBounds(314, 488, 64, 34);
    chaos.setBounds(12, 532, 100, 96);
    dovetail.setBounds(145, 532, 100, 96);
    swing.setBounds(278, 532, 100, 96);
    pin.setBounds(12, 640, 84, 36);
    inOrder.setBounds(102, 640, 92, 36);
    clear.setBounds(200, 640, 84, 36);
    undo.setBounds(290, 640, 88, 36);
    // SOUND
    pitch.setBounds(12, 346, 110, 104);
    pitchMode.setBounds(134, 370, 244, 34);
    vintage.setBounds(12, 478, 366, 32);
    drive.setBounds(12, 524, 110, 104);
    output.setBounds(140, 524, 110, 104);
    align.setBounds(268, 524, 110, 104);
    choke.setBounds(12, 658, 366, 32);
    // EXPORT
    loopBars.setBounds(134, 336, 244, 32);
    shareLoop.setBounds(12, 380, 366, 52);
    shareChops.setBounds(12, 480, 366, 52);
    shareChop.setBounds(12, 580, 366, 52);
}

void Editor::paint(juce::Graphics& g)
{
    g.fillAll(ink0);
}

void Editor::paintContent(juce::Graphics& g)
{
    // header
    epsilon_ui::drawProductMark(g, { 14.0f, 10.0f }, 0.9f, epsilon_ui::Tier::Epsilon, "Chop", {},
                                [](float h, bool b) { return sans(h, b); });
    const auto& s = view.session;
    g.setFont(sans(11.0f));
    g.setColour(s.sample != nullptr ? text : faint);
    auto name = s.sample != nullptr ? juce::String::fromUTF8(s.sample->name.c_str()) : juce::String("No sample");
    if (s.sample != nullptr)
        name << "  -  " << s.slices.count << " chops";
    g.drawFittedText(name, 52, 30, 248, 14, juce::Justification::centredLeft, 1, 0.8f);

    if (page == 1)
    {
        g.setColour(text);
        g.setFont(mono(13.0f));
        const auto& st = pattern()[(size_t) selectedStep];
        const auto chops = s.slices.count;
        g.drawText("STEP " + two(selectedStep + 1), 12, 488, 80, 34, juce::Justification::centredLeft);
        g.setColour(st.slice >= 0 && chops > 0 ? chopColour(st.slice % chops) : faint);
        g.setFont(mono(15.0f));
        g.drawText(st.slice >= 0 && chops > 0 ? two(st.slice % chops + 1) : juce::String("--"), 136, 488, 56, 34, juce::Justification::centred);
        g.setColour(faint);
        g.setFont(sans(10.5f));
        g.drawFittedText("Tap a step, then pick its chop with < >. PIN a step and roll RANDOMIZE to Dovetail around it.",
                         12, 684, 366, 30, juce::Justification::topLeft, 2);
    }
    else if (page == 2)
    {
        label(g, "PITCH MODE", { 134.0f, 350.0f, 200.0f, 16.0f });
        label(g, "VINTAGE", { 12.0f, 460.0f, 200.0f, 16.0f });
        label(g, "CHOKE", { 12.0f, 640.0f, 200.0f, 16.0f });
        g.setColour(faint);
        g.setFont(sans(10.5f));
        g.drawFittedText("MPC: down = slower, darker, heavier. KEEP LENGTH: only the pitch moves.",
                         134, 408, 244, 30, juce::Justification::topLeft, 2);
    }
    else if (page == 3)
    {
        label(g, "LOOP LENGTH", { 12.0f, 336.0f, 120.0f, 32.0f });
        g.setColour(dim);
        g.setFont(sans(11.0f));
        g.drawFittedText("This bank as you hear it, at the tempo, looping seamlessly.", 12, 436, 366, 34, juce::Justification::topLeft, 2);
        g.drawFittedText("Every chop as its own file, numbered.", 12, 536, 366, 34, juce::Justification::topLeft, 2);
        g.drawFittedText("The chop you tapped last (" + two(selectedChop + 1) + ").", 12, 636, 366, 20, juce::Justification::topLeft, 1);
        g.setColour(faint);
        g.drawFittedText("Share to AirDrop, Messages, FL Studio Mobile or any app that opens audio. Everything is also kept in "
                         "Files > On My iPhone > Epsilon Chop.", 12, 672, 366, 48, juce::Justification::topLeft, 3);
    }

    if (toastTicks > 0)
    {
        auto r = juce::Rectangle<float>(40.0f, 704.0f, 310.0f, 34.0f);
        g.setColour(ink3.withAlpha(0.96f));
        g.fillRoundedRectangle(r, 17.0f);
        g.setColour(live);
        g.drawRoundedRectangle(r.reduced(0.5f), 17.0f, 1.0f);
        g.setColour(text);
        g.setFont(sans(12.0f, true));
        g.drawFittedText(toast, r.toNearestInt().reduced(12, 0), juce::Justification::centred, 1);
    }
}

void Editor::timerCallback()
{
    if (processor.revisionNow() != seenRevision)
        pull();
    const auto playing = processor.isPlaying();
    if (playing != seenPlaying)
    {
        seenPlaying = playing;
        play.setState(playing, false);
    }
    const auto slice = processor.playSlice(), frame = processor.playFrame();
    wave.setPlaying(slice, frame);
    if (slice != seenSlice)
    {
        for (auto& p : pads)
        {
            const auto lit = p->exists && p->chop == slice;
            if (lit != p->lit) { p->lit = lit; p->repaint(); }
        }
        seenSlice = slice;
    }
    const auto step = processor.playStep();
    if (step != seenStep)
    {
        seenStep = step;
        steps.playing = step;
        if (page == 1) steps.repaint();
    }
    if (toastTicks > 0 && --toastTicks == 0)
        content.repaint(0, 700, designWidth, 44);
}
}
