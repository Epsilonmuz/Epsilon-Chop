#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
constexpr int rate = 48000;
constexpr int frameCount = rate * 4;
constexpr std::array<double, 7> onsets { 0.0, 0.47, 0.96, 1.49, 2.02, 2.53, 3.12 };

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct TempFolder
{
    juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("epsilon-chop-" + juce::Uuid().toString());

    TempFolder() { require(path.createDirectory().wasOk(), "create fixture folder"); }
    ~TempFolder() { path.deleteRecursively(); }
};

void writeFixture(const juce::File& path)
{
    juce::FileOutputStream stream(path);
    require(stream.openedOk(), "open WAV fixture");
    const auto payload = frameCount * 4;
    stream.write("RIFF", 4);
    stream.writeInt(36 + payload);
    stream.write("WAVEfmt ", 8);
    stream.writeInt(16);
    stream.writeShort(1);
    stream.writeShort(2);
    stream.writeInt(rate);
    stream.writeInt(rate * 4);
    stream.writeShort(4);
    stream.writeShort(16);
    stream.write("data", 4);
    stream.writeInt(payload);
    for (int index = 0; index < frameCount; ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        double left = 0.0, right = 0.0;
        for (std::size_t hit = 0; hit < onsets.size(); ++hit)
        {
            const auto age = time - onsets[hit];
            if (age < 0.0 || age > 0.18)
                continue;
            const auto amplitude = (0.72 - 0.065 * hit) * std::exp(-30.0 * age);
            const auto wave = std::sin(juce::MathConstants<double>::twoPi * (104.0 + 28.0 * hit) * age);
            const auto click = 0.11 * std::exp(-165.0 * age)
                * std::sin(juce::MathConstants<double>::twoPi * 1620.0 * age);
            left += amplitude * wave + click;
            right += 0.75 * amplitude * wave - click;
        }
        stream.writeShort(static_cast<short>(32760.0 * left));
        stream.writeShort(static_cast<short>(32760.0 * right));
    }
    stream.flush();
}

EpsilonChopProcessor::EditorState waitForSample(EpsilonChopProcessor& processor)
{
    for (int attempt = 0; attempt < 800; ++attempt)
    {
        auto state = processor.editorState();
        if (state.session.sample != nullptr && !state.loading)
            return state;
        if (!state.loading)
            throw std::runtime_error(state.status.toStdString());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("sample import timed out");
}

juce::AudioBuffer<float> renderNote(EpsilonChopProcessor& processor, int note)
{
    processor.releaseResources();
    processor.prepareToPlay(rate, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(120)), 37);
    processor.processBlock(audio, midi);
    require(midi.isEmpty(), "instrument does not pass through MIDI");
    require(audio.getMagnitude(0, 37) == 0.0f, "MIDI offset preserves silence before note");
    require(audio.getMagnitude(37, 475) > 0.05f, "MIDI note plays a chop");
    return audio;
}

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setBpm(bpm);
        if (hasPosition)
            position.setPpqPosition(ppq);
        position.setIsPlaying(playing);
        return position;
    }

    bool playing = true, hasPosition = true;
    double ppq = 0.0, bpm = 120.0;
};

void checkPreviewWithoutHost(EpsilonChopProcessor& processor)
{
    // FL Studio stopped: transport not playing. The PLAY button must still sound.
    TestPlayHead playhead;
    playhead.playing = false;
    processor.setPlayHead(&playhead);
    processor.releaseResources();
    processor.prepareToPlay(rate, 512);
    processor.setPlaying(true);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    float energy = 0.0f;
    int steps = 0, lastStep = -1;
    for (int block = 0; block < 200; ++block)
    {
        processor.processBlock(audio, midi);
        energy += audio.getMagnitude(0, 512);
        if (processor.playStep() != lastStep) { ++steps; lastStep = processor.playStep(); }
    }
    require(energy > 1.0f, "preview plays with the host stopped");
    require(steps >= 8, "preview walks through the steps");
    processor.setPlaying(false);
    processor.processBlock(audio, midi);
    processor.processBlock(audio, midi);
    require(audio.getMagnitude(0, 512) == 0.0f && processor.playStep() == -1, "stopping preview goes silent");
    processor.setPlayHead(nullptr);
}

void checkHostSync(EpsilonChopProcessor& processor)
{
    TestPlayHead playhead;
    processor.setPlayHead(&playhead);
    processor.parameters.getParameter("pattern")->setValueNotifyingHost(1.0f);
    processor.releaseResources();
    processor.prepareToPlay(rate, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    require(processor.playStep() == 0 && processor.hostSynced(), "host downbeat fires step one");
    playhead.playing = false;
    processor.processBlock(audio, midi);
    require(processor.playStep() == -1 && audio.getMagnitude(200, 312) == 0.0f, "transport stop releases the pattern");
    processor.setPlayHead(nullptr);
    processor.parameters.getParameter("pattern")->setValueNotifyingHost(0.0f);
}

void checkVersionOneState()
{
    // A v0.1.0 chunk with no sample must still load.
    juce::MemoryBlock block;
    {
        juce::MemoryOutputStream out(block, false);
        out.writeInt(0x50484345);
        out.writeInt(1);
        out.writeFloat(-6.0f);
        out.writeInt(0);
        out.writeInt64(1234);
        out.writeInt(0);      // grid
        out.writeInt(16);     // density
        out.writeInt(2);      // bank C
        out.writeInt(0);      // manual count
        out.writeInt(0);      // manual edge 0
        for (int bank = 0; bank < chop::bankCount; ++bank)
            for (int step = 0; step < chop::stepCount; ++step)
            {
                out.writeInt(step % 4 == 0 ? step / 4 : -1);
                out.writeInt(80);
            }
        out.writeInt(0);      // no sample
    }
    EpsilonChopProcessor processor;
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    const auto state = processor.editorState();
    require(state.session.edit.bank == 2 && state.session.edit.density == 16
            && state.session.edit.patterns[2][4].slice == 1 && state.session.edit.patterns[2][4].gate == 80,
            "v0.1.0 sessions still load");
}

void savePreview(EpsilonChopProcessor& processor, const juce::File& path, bool playing)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(EpsilonChopEditor::designWidth, EpsilonChopEditor::designHeight);
    if (playing)
    {
        processor.setPlaying(true);
        processor.releaseResources();
        processor.prepareToPlay(rate, 512);
        juce::AudioBuffer<float> audio(2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 37; ++i)
            processor.processBlock(audio, midi);
        // Let the editor pick up the live state.
        if (auto* chopEditor = dynamic_cast<EpsilonChopEditor*>(editor.get()))
            chopEditor->tick();
    }
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    require(image.isValid(), "render editor image");
    path.deleteFile();
    juce::FileOutputStream out(path);
    require(out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out), "write editor preview");
    processor.setPlaying(false);
}

void timeEditor(EpsilonChopProcessor& processor)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    juce::Image frame(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(frame);
        editor->paintEntireComponent(g, true);   // warm the caches
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    constexpr int frames = 60;
    for (int i = 0; i < frames; ++i)
    {
        juce::Graphics g(frame);
        editor->paintEntireComponent(g, true);
    }
    const auto perFrame = (juce::Time::getMillisecondCounterHiRes() - start) / frames;
    std::cout << "Full editor repaint: " << perFrame << " ms per frame\n";
}

juce::AudioBuffer<float> readWav(const juce::File& f, double& sampleRate)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::FileInputStream(f), true));
    require(reader != nullptr, "exported file is a readable WAV");
    require(reader->bitsPerSample == 24, "exports are 24-bit");
    sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
    return audio;
}

// Drag-out to FL Studio: chops, all chops, and the pattern as a seamless loop.
void testExport(EpsilonChopProcessor& processor)
{
    processor.setMode(chop::SliceMode::grid);
    processor.setDensity(8);
    processor.setBank(0);
    processor.arrangeInOrder();
    auto state = processor.editorState();
    const auto folder = processor.exportFolder();
    folder.deleteRecursively();
    require(folder.getFileName() == "Jersey Chop Demo", "exports go in a folder named after the sample");
    require(!folder.isAChildOf(juce::File::getSpecialLocation(juce::File::tempDirectory)),
            "exports are kept out of temp, so FL projects keep finding them");

    // One chop: raw audio, exact length, original rate and channels
    const auto chop3 = processor.exportChop(2);
    require(chop3.getFileName() == "Jersey Chop Demo - Chop 03.wav", "chop file is named after sample and number");
    double fileRate = 0.0;
    auto audio = readWav(chop3, fileRate);
    const auto& slices = state.session.slices;
    require(fileRate == rate && audio.getNumChannels() == 2, "chop keeps the sample's rate and channels");
    require(audio.getNumSamples() == slices.edge[3] - slices.edge[2], "chop is exactly the cut");
    require(std::abs(audio.getSample(0, 0)) < 1.0e-3f && std::abs(audio.getSample(0, audio.getNumSamples() - 1)) < 1.0e-3f,
            "chop starts and ends at silence (no clicks)");
    {
        const auto& pcm = state.session.sample->pcm[0];
        const auto middle = audio.getNumSamples() / 2;
        require(std::abs(audio.getSample(0, middle) - pcm[static_cast<std::size_t>(slices.edge[2] + middle)]) < 1.0e-4f,
                "chop audio is the untouched sample");
    }
    require(processor.exportChop(2) == chop3, "an unchanged export reuses its file");
    require(processor.exportChop(99) == juce::File(), "an invalid chop exports nothing");

    // Every chop
    const auto all = processor.exportAllChops();
    require(all.size() == slices.count, "every chop is exported");
    require(all[0].endsWith("Chop 01.wav") && all[all.size() - 1].endsWith("Chop " + juce::String(slices.count).paddedLeft('0', 2) + ".wav"),
            "chops are numbered in order");

    // Pattern loop at 140 BPM, 1/16: one bar
    processor.prepareToPlay(rate, 512);
    auto* tempo = processor.parameters.getParameter("tempo");
    tempo->setValueNotifyingHost(tempo->convertTo0to1(140.0f));
    const auto loop = processor.exportPattern(1);
    const auto bpm = processor.exportTempo();   // the host's tempo once it has played, else the TEMPO box
    require(processor.hostHasTempo() || std::abs(bpm - 140.0) < 0.01, "without a host the TEMPO box sets the loop tempo");
    require(loop.getFileName() == "Jersey Chop Demo - Pattern A " + juce::String(bpm, bpm == std::floor(bpm) ? 0 : 1) + " BPM.wav",
            "loop file names bank and tempo");
    audio = readWav(loop, fileRate);
    const auto barFrames = juce::roundToInt(4.0 * 60.0 / bpm * rate);
    require(std::abs(audio.getNumSamples() - barFrames) <= 1, "loop is exactly one bar at the tempo");
    require(audio.getMagnitude(0, audio.getNumSamples()) > 0.05f, "loop has audio");
    // Seamless: the join from the last frame to the first is no bigger than normal frame-to-frame steps
    float largest = 0.0f;
    for (int i = 1; i < audio.getNumSamples(); ++i)
        largest = std::max(largest, std::abs(audio.getSample(0, i) - audio.getSample(0, i - 1)));
    const auto join = std::abs(audio.getSample(0, 0) - audio.getSample(0, audio.getNumSamples() - 1));
    require(join <= largest + 1.0e-4f, "loop point is seamless");
    // The first step plays chop 1 at the downbeat
    require(audio.getMagnitude(0, 0, rate / 100) > 0.05f, "loop starts on the downbeat hit");

    // A changed pattern never overwrites the earlier file
    processor.randomize();
    const auto second = processor.exportPattern(1);
    require(second != loop && loop.existsAsFile() && second.getFileName().contains("(2)"),
            "a changed loop gets a new name, the old file stays");
    require(processor.exportPattern(4).existsAsFile(), "multi-bar export works");

    folder.deleteRecursively();
    std::cout << "Export: chop, all chops, seamless pattern loop, file reuse and no overwrites passed.\n";
}

void run(const char* previewFolder)
{
    TempFolder fixture;
    const auto input = fixture.path.getChildFile("Jersey Chop Demo.wav");
    writeFixture(input);

    if (previewFolder != nullptr)
    {
        EpsilonChopProcessor empty;
        savePreview(empty, juce::File(juce::String(previewFolder)).getChildFile("preview-empty.png"), false);
    }

    EpsilonChopProcessor processor;
    processor.loadFile(input);
    auto state = waitForSample(processor);
    require(state.session.sample->length() == frameCount && state.session.sample->channels == 2,
            "decode stereo WAV at original length");
    require(state.session.edit.mode == chop::SliceMode::transient, "transient is the default mode");
    require(state.session.slices.count >= 6, "transient mode chops the hits");
    const auto& analysis = *state.session.sample;
    for (std::size_t hit = 1; hit < onsets.size(); ++hit)
    {
        const auto expected = static_cast<int>(onsets[hit] * rate);
        require(std::any_of(analysis.onsets.begin(), analysis.onsets.end(),
                           [expected](const chop::Onset& o) { return std::abs(o.position - expected) < 1000; }),
                "onset timing within 21 ms");
    }

    processor.setMode(chop::SliceMode::grid);
    processor.setDensity(8);
    require(processor.editorState().session.slices.count == 8, "grid gives eight chops");
    processor.setMode(chop::SliceMode::beat);
    require(processor.editorState().session.slices.count >= 4, "beat mode chops on the beat grid");
    processor.setMode(chop::SliceMode::random);
    const auto randomCuts = processor.editorState().session.slices.edge;
    processor.rechop();
    require(processor.editorState().session.slices.edge != randomCuts, "re-chop moves the random cuts");

    processor.setMode(chop::SliceMode::transient);
    const auto firstTransient = processor.editorState().session.slices.edge[1];
    processor.editBoundary(1, firstTransient + 300);
    state = processor.editorState();
    require(state.session.edit.mode == chop::SliceMode::manual && state.session.slices.edge[1] == firstTransient + 300,
            "dragging a cut switches to manual and moves it");

    processor.setBank(1);
    const auto otherBank = processor.editorState().session.edit.patterns[0];
    processor.randomize();
    const auto randomized = processor.editorState().session.edit.patterns[1];
    require(randomized != chop::makePreset(1), "randomize changes the bank");
    processor.randomize();
    processor.undo();
    state = processor.editorState();
    require(state.session.edit.patterns[1] == randomized, "undo restores the previous roll");
    require(state.session.edit.patterns[0] == otherBank, "bank edits remain isolated");

    // v0.3: undo is per bank. A roll on bank B is never undone from bank A.
    processor.randomize();
    const auto rolledB = processor.editorState().session.edit.patterns[1];
    processor.setBank(0);
    processor.undo();
    processor.setBank(1);
    require(processor.editorState().session.edit.patterns[1] == rolledB, "undo on another bank leaves this bank's roll alone");
    processor.undo();
    require(processor.editorState().session.edit.patterns[1] == randomized, "undo from the bank itself reverts its roll");
    state = processor.editorState();

    // MIDI: C5 (60) is chop 1 by default and every key wraps onto a chop.
    renderNote(processor, 60);
    renderNote(processor, 36);
    const auto reference = renderNote(processor, 61);

    checkPreviewWithoutHost(processor);
    checkHostSync(processor);

    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    require(input.deleteFile(), "remove source WAV before project restore");

    EpsilonChopProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    const auto restoredState = restored.editorState();
    require(restoredState.session.sample != nullptr && restoredState.session.edit.bank == 1
            && restoredState.session.edit.patterns[1] == randomized, "restore embedded sample and banks");
    require(restoredState.session.slices.edge == state.session.slices.edge, "restore marker locations");
    const auto actual = renderNote(restored, 61);
    // v0.3 embeds the sample as 24-bit FLAC: the restore matches to within 24-bit resolution.
    for (int channel = 0; channel < 2; ++channel)
    {
        auto largest = 0.0f;
        for (int i = 0; i < 512; ++i)
            largest = std::max(largest, std::abs(reference.getSample(channel, i) - actual.getSample(channel, i)));
        require(largest < 2.0e-6f, "restored audio matches to 24-bit resolution");
    }
    require(saved.getSize() < 2u * 4u * static_cast<std::size_t>(state.session.sample->length()) * 3u / 4u + 65536u,
            "FLAC keeps the saved project smaller than raw floats");

    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()) - 1);
    require(restored.editorState().session.revision == restoredState.session.revision, "truncated state is ignored");
    checkVersionOneState();

    // Dovetail through the processor: pin, roll, save, reload.
    {
        const auto before = restored.editorState().session;
        const auto& bank = before.edit.patterns[static_cast<std::size_t>(before.edit.bank)];
        int pinned = -1;
        for (int i = 0; i < chop::stepCount && pinned < 0; ++i)
            if (bank[static_cast<std::size_t>(i)].slice >= 0) pinned = i;
        restored.togglePin(pinned);
        restored.randomize();
        const auto after = restored.editorState();
        require(after.session.edit.anchor[static_cast<std::size_t>(after.session.edit.bank)] == pinned, "PIN pins the selected step");
        require(after.session.edit.patterns[static_cast<std::size_t>(after.session.edit.bank)][static_cast<std::size_t>(pinned)]
                    == bank[static_cast<std::size_t>(pinned)], "a Dovetail roll keeps the pinned step");
        require(after.lastRoll.startsWith("Dovetail"), "the readout shows the Dovetail chain");
        require(after.session.fit != nullptr && after.session.fit->count == after.session.slices.count, "fit table follows the chops");
        juce::MemoryBlock pinnedState;
        restored.getStateInformation(pinnedState);
        EpsilonChopProcessor reopened;
        reopened.setStateInformation(pinnedState.getData(), static_cast<int>(pinnedState.getSize()));
        require(reopened.editorState().session.edit.anchor == after.session.edit.anchor
                    && reopened.editorState().session.edit.fitAmount == after.session.edit.fitAmount, "pins and Fit are saved");
    }

    // Computer keyboard: key down plays the chop at once, key up ends it in GATE mode.
    {
        restored.releaseResources();
        restored.prepareToPlay(rate, 512);
        restored.keyDown(0);
        juce::AudioBuffer<float> audio(2, 512);
        juce::MidiBuffer midi;
        restored.processBlock(audio, midi);
        require(audio.getMagnitude(0, 512) > 0.05f, "a key plays its chop");
    }

    std::cout << "Import, chop modes, randomize/undo, MIDI wrap, preview without host, host sync, recall, Dovetail and keys passed.\n";
    testExport(restored);
    if (previewFolder != nullptr)
    {
        restored.setMode(chop::SliceMode::grid);
        restored.setDensity(16);
        restored.setBank(0);
        restored.arrangeInOrder();
        restored.togglePin(0);
        restored.randomize();
        auto* vintageParameter = restored.parameters.getParameter("vintage");
        vintageParameter->setValueNotifyingHost(vintageParameter->convertTo0to1(1.0f));
        savePreview(restored, juce::File(juce::String(previewFolder)).getChildFile("preview.png"), true);
        timeEditor(restored);
    }
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    try
    {
        run(argc > 1 ? argv[1] : nullptr);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
