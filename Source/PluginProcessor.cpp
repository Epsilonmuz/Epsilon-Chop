#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SampleAnalysis.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int stateMagic = 0x50484345;
constexpr int stateVersion = 2;
constexpr int historyLimit = 32;
constexpr int maximumNameBytes = 1024;

juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // "pattern" and "output" keep their v0.1.0 IDs so automation and sessions survive.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "pattern", 1 }, "Host Sync", false));
    const auto db = juce::AudioParameterFloatAttributes {}.withLabel("dB");
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "output", 1 }, "Output",
               juce::NormalisableRange<float> { -24.0f, 6.0f, 0.1f }, -3.0f, db));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "rate", 1 }, "Step Rate",
               juce::StringArray { "1/8", "1/16", "1/32" }, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "swing", 1 }, "Swing",
               juce::NormalisableRange<float> { 0.0f, 60.0f, 1.0f }, 0.0f,
               juce::AudioParameterFloatAttributes {}.withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "fit", 1 }, "Tempo Fit", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "root", 1 }, "Root Note", 24, 96, chop::defaultRootNote));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "tempo", 1 }, "Internal Tempo",
               juce::NormalisableRange<float> { 40.0f, 240.0f, 0.1f }, 120.0f,
               juce::AudioParameterFloatAttributes {}.withLabel("BPM")));
    return layout;
}

int markerSpacing(const chop::Sample& sample)
{
    return std::max(1, juce::roundToInt(sample.sampleRate * 0.004));
}

float safeAudio(float value) noexcept
{
    return std::isfinite(value) ? juce::jlimit(-8.0f, 8.0f, value) : 0.0f;
}

void writeEdit(juce::MemoryOutputStream& out, const chop::Edit& edit)
{
    out.writeInt(static_cast<int>(edit.mode));
    out.writeInt(edit.density);
    out.writeInt(edit.sensitivity);
    out.writeInt(edit.beatDivision);
    out.writeInt(edit.sampleBpmTenths);
    out.writeInt(edit.snapZero ? 1 : 0);
    out.writeInt64(static_cast<juce::int64>(edit.chopSeed));
    out.writeInt(edit.bank);
    out.writeInt(edit.randomFlags);
    out.writeInt(edit.chaos);
    out.writeInt(edit.manual.count);
    for (int index = 0; index <= edit.manual.count; ++index)
        out.writeInt(edit.manual.edge[index]);
    for (const auto& bank : edit.patterns)
        for (const auto& step : bank)
        {
            out.writeInt(step.slice);
            out.writeInt(step.gate);
            out.writeInt(step.ratchet);
            out.writeInt(step.pitch);
            out.writeInt(step.reverse ? 1 : 0);
        }
}

bool readEdit(juce::MemoryInputStream& input, chop::Edit& edit, int version)
{
    const auto mode = input.readInt();
    edit.density = input.readInt();
    if (version >= 2)
    {
        edit.sensitivity = input.readInt();
        edit.beatDivision = input.readInt();
        edit.sampleBpmTenths = input.readInt();
        edit.snapZero = input.readInt() != 0;
        edit.chopSeed = static_cast<std::uint32_t>(input.readInt64());
    }
    else
    {
        edit.snapZero = false; // v0.1.0 cut exactly where its markers were
    }
    edit.bank = input.readInt();
    if (version >= 2)
    {
        edit.randomFlags = input.readInt() & chop::randomAll;
        edit.chaos = input.readInt();
    }
    edit.manual.count = input.readInt();
    if (mode < 0 || mode >= chop::sliceModeCount || edit.density < 1 || edit.density > chop::maxSlices
        || edit.bank < 0 || edit.bank >= chop::bankCount || edit.sensitivity < 0 || edit.sensitivity > 100
        || edit.beatDivision < 1 || edit.beatDivision > 8 || edit.sampleBpmTenths < 0 || edit.sampleBpmTenths > 4000
        || edit.chaos < 0 || edit.chaos > 100
        || edit.manual.count < 0 || edit.manual.count > chop::maxSlices)
        return false;
    const auto fieldsPerStep = version >= 2 ? 5 : 2;
    const auto minimumBytes = 4 * (edit.manual.count + 1 + fieldsPerStep * chop::bankCount * chop::stepCount + 1);
    if (input.getNumBytesRemaining() < minimumBytes)
        return false;

    edit.mode = static_cast<chop::SliceMode>(mode);
    for (int index = 0; index <= edit.manual.count; ++index)
        edit.manual.edge[index] = input.readInt();
    for (auto& bank : edit.patterns)
        for (auto& step : bank)
        {
            step.slice = input.readInt();
            step.gate = input.readInt();
            if (version >= 2)
            {
                step.ratchet = input.readInt();
                step.pitch = input.readInt();
                step.reverse = input.readInt() != 0;
            }
            if (!chop::sanitiseStep(step))
                return false;
        }
    return true;
}

void writeSample(juce::MemoryOutputStream& out, const chop::Sample* sample)
{
    out.writeInt(sample == nullptr ? 0 : sample->channels);
    if (sample == nullptr)
        return;

    out.writeDouble(sample->sampleRate);
    out.writeInt(sample->length());
    const auto nameSize = static_cast<int>(std::min<std::size_t>(maximumNameBytes, sample->name.size()));
    out.writeInt(nameSize);
    out.write(sample->name.data(), static_cast<std::size_t>(nameSize));
    for (int channel = 0; channel < sample->channels; ++channel)
    {
       #if JUCE_BIG_ENDIAN
        for (const auto value : sample->pcm[channel])
            out.writeFloat(value);
       #else
        out.write(sample->pcm[channel].data(), sample->pcm[channel].size() * sizeof(float));
       #endif
    }
}

bool readSample(juce::MemoryInputStream& input, std::shared_ptr<chop::Sample>& destination, int version)
{
    const auto channels = input.readInt();
    if (channels == 0)
        return true;
    if (input.getNumBytesRemaining() < 16)
        return false;
    const auto rate = input.readDouble();
    const auto frames = input.readInt();
    const auto nameSize = input.readInt();
    if (!chop::validSampleSize(rate, channels, frames) || nameSize < 0 || nameSize > maximumNameBytes
        || input.getNumBytesRemaining() < nameSize)
        return false;

    auto sample = std::make_shared<chop::Sample>();
    sample->channels = channels;
    sample->sampleRate = rate;
    sample->name.resize(static_cast<std::size_t>(nameSize));
    if (nameSize > 0 && input.read(sample->name.data(), nameSize) != nameSize)
        return false;

    if (version < 2)
    {
        // v0.1.0 stored its own transient list; it is recomputed now.
        const auto old = input.readInt();
        if (old < 0 || old >= chop::maxSlices || input.getNumBytesRemaining() < 4 * old)
            return false;
        input.skipNextBytes(4 * old);
    }

    const auto bytesPerChannel = static_cast<juce::int64>(frames) * static_cast<juce::int64>(sizeof(float));
    if (input.getNumBytesRemaining() < channels * bytesPerChannel)
        return false;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto& data = sample->pcm[channel];
        data.resize(static_cast<std::size_t>(frames));
       #if JUCE_BIG_ENDIAN
        for (auto& value : data)
            value = input.readFloat();
       #else
        if (input.read(data.data(), static_cast<size_t>(bytesPerChannel)) != bytesPerChannel)
            return false;
       #endif
        for (auto& value : data)
            value = safeAudio(value);
    }
    chop::buildWaveform(*sample);
    chop::analyzeTransients(*sample, [] { return false; });
    destination = std::move(sample);
    return true;
}
}

EpsilonChopProcessor::EpsilonChopProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "EpsilonChopParameters", parameterLayout())
{
    outputParameter = parameters.getRawParameterValue("output");
    patternParameter = parameters.getRawParameterValue("pattern");
    rateParameter = parameters.getRawParameterValue("rate");
    swingParameter = parameters.getRawParameterValue("swing");
    fitParameter = parameters.getRawParameterValue("fit");
    rootParameter = parameters.getRawParameterValue("root");
    tempoParameter = parameters.getRawParameterValue("tempo");
    history.reserve(historyLimit);
    auto initial = std::make_unique<chop::Session>();
    initial->edit = chop::defaultEdit();
    const std::lock_guard<std::mutex> lock(mutex);
    publishLocked(std::move(initial));
}

EpsilonChopProcessor::~EpsilonChopProcessor()
{
    loadSerial.fetch_add(1);
    loader.removeAllJobs(true, -1);
}

bool EpsilonChopProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void EpsilonChopProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate, juce::Decibels::decibelsToGain(outputParameter->load()));
}

void EpsilonChopProcessor::releaseResources()
{
    engine.reset();
    currentStep.store(-1);
    currentFrame.store(-1);
    currentSlice.store(-1);
}

const chop::Session* EpsilonChopProcessor::acquireAudioState() noexcept
{
    // Publish the hazard before validating the pointer. Retired generations
    // are freed only on the editor, state, or loader threads.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto* candidate = published.load();
        audioHazard.store(candidate);
        const auto* confirmed = published.load();
        if (candidate == confirmed)
            return confirmed;
    }
    audioHazard.store(nullptr);
    return nullptr;
}

void EpsilonChopProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto frames = buffer.getNumSamples();
    buffer.clear();
    if (frames == 0 || buffer.getNumChannels() == 0)
    {
        midi.clear();
        return;
    }

    const auto* current = acquireAudioState();
    if (current == nullptr)
    {
        midi.clear();
        return;
    }

    chop::Transport transport;
    if (auto* playhead = getPlayHead())
        if (auto position = playhead->getPosition())
        {
            if (auto bpm = position->getBpm())
                transport.bpm = *bpm;
            if (auto ppq = position->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.hasPosition = true;
            }
            transport.playing = position->getIsPlaying();
        }

    constexpr std::array<int, 3> stepsPerQuarter { 2, 4, 8 };
    chop::PlaySettings settings;
    settings.followHost = patternParameter->load(std::memory_order_relaxed) >= 0.5f;
    settings.preview = playing.load(std::memory_order_relaxed);
    settings.stepsPerQuarter = stepsPerQuarter[static_cast<std::size_t>(
        juce::jlimit(0, 2, juce::roundToInt(rateParameter->load(std::memory_order_relaxed))))];
    settings.swing = swingParameter->load(std::memory_order_relaxed) * 0.01f;
    settings.tempoFit = fitParameter->load(std::memory_order_relaxed) >= 0.5f;
    settings.fallbackBpm = tempoParameter->load(std::memory_order_relaxed);
    settings.gain = juce::Decibels::decibelsToGain(outputParameter->load(std::memory_order_relaxed));
    engine.beginBlock(*current, transport, settings, frames);

    const auto request = previewRequest.exchange(-1, std::memory_order_relaxed);
    if (request == -2)
    {
        engine.stop(chop::VoiceOrigin::preview);
        engine.stop(chop::VoiceOrigin::midi);
    }
    else if (request >= 0)
        engine.trigger(request, 1.0f, chop::VoiceOrigin::preview);

    const auto root = juce::roundToInt(rootParameter->load(std::memory_order_relaxed));
    const auto chops = current->slices.count;
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    auto event = midi.begin();
    const auto end = midi.end();
    for (int frame = 0; frame < frames; ++frame)
    {
        while (event != end && (*event).samplePosition <= frame)
        {
            const auto message = *event++;
            if (message.numBytes < 3)
                continue;
            const auto statusByte = message.data[0] & 0xf0;
            if (statusByte == 0x90 && message.data[2] != 0 && chops > 0)
            {
                // Every key plays a chop: the root note is Chop 1 and the map wraps around.
                const auto offset = static_cast<int>(message.data[1]) - root;
                engine.trigger(((offset % chops) + chops) % chops, message.data[2] / 127.0f, chop::VoiceOrigin::midi);
            }
            else if (statusByte == 0xb0 && (message.data[1] == 120 || message.data[1] == 123))
                engine.stop(chop::VoiceOrigin::midi);
        }
        float l = 0.0f, r = 0.0f;
        engine.render(frame, l, r);
        left[frame] = right != nullptr ? l : (l + r) * 0.5f;
        if (right != nullptr)
            right[frame] = r;
    }
    midi.clear();
    currentStep.store(engine.currentStep(), std::memory_order_relaxed);
    currentFrame.store(engine.currentFrame(), std::memory_order_relaxed);
    currentSlice.store(engine.currentSlice(), std::memory_order_relaxed);
    synced.store(engine.hostSynced(), std::memory_order_relaxed);
    hostTempo.store(std::isfinite(transport.bpm) && transport.bpm >= 10.0, std::memory_order_relaxed);
    currentTempo.store(engine.tempo(), std::memory_order_relaxed);
    engine.endBlock();
    audioHazard.store(nullptr);
}

void EpsilonChopProcessor::reclaimLocked()
{
    const auto* current = published.load();
    const auto* active = audioHazard.load();
    snapshots.erase(std::remove_if(snapshots.begin(), snapshots.end(), [current, active](const auto& item)
    {
        return item.get() != current && item.get() != active;
    }), snapshots.end());
}

void EpsilonChopProcessor::publishLocked(std::unique_ptr<chop::Session> session)
{
    session->revision = ++revision;
    session->slices = session->sample != nullptr ? chop::makeSlices(*session->sample, session->edit) : chop::Slices {};
    auto* next = session.get();
    snapshots.push_back(std::move(session));
    published.store(next);
    publishedRevision.store(revision, std::memory_order_relaxed);
    reclaimLocked();
}

void EpsilonChopProcessor::setStatusLocked(juce::String message)
{
    status = std::move(message);
    statusSerial.fetch_add(1, std::memory_order_relaxed);
}

EpsilonChopProcessor::EditorState EpsilonChopProcessor::editorState() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return { *published.load(), status, !history.empty(), loading, lastRoll };
}

void EpsilonChopProcessor::loadFile(const juce::File& file)
{
    const auto ticket = loadSerial.fetch_add(1) + 1;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        loading = true;
        setStatusLocked("Loading " + file.getFileName() + "...");
    }
    loader.addJob([this, file, ticket] { loadOnWorker(file, ticket); });
}

void EpsilonChopProcessor::loadOnWorker(juce::File file, std::uint64_t ticket)
{
    const auto cancelled = [this, ticket] { return loadSerial.load() != ticket; };
    const auto report = [this, &cancelled](juce::String message)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!cancelled())
        {
            loading = false;
            setStatusLocked(std::move(message));
        }
    };
    if (cancelled())
        return;

    try
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
            return report("Couldn't read that file. Try a WAV, AIFF, FLAC or MP3.");
        if (!chop::validSampleSize(reader->sampleRate, static_cast<int>(reader->numChannels), reader->lengthInSamples))
            return report("Use mono or stereo audio at 8-192 kHz, up to 120 seconds");

        auto sample = std::make_shared<chop::Sample>();
        const auto frames = static_cast<int>(reader->lengthInSamples);
        sample->channels = static_cast<int>(reader->numChannels);
        sample->sampleRate = reader->sampleRate;
        sample->name = file.getFileNameWithoutExtension().toStdString();
        std::array<float*, 2> pointers {};
        for (int channel = 0; channel < sample->channels; ++channel)
        {
            sample->pcm[channel].resize(static_cast<std::size_t>(frames));
            pointers[channel] = sample->pcm[channel].data();
        }
        juce::AudioBuffer<float> view(pointers.data(), sample->channels, frames);
        constexpr int decodeBlock = 65536;
        for (int first = 0; first < frames; first += decodeBlock)
        {
            if (cancelled())
                return;
            const auto length = std::min(decodeBlock, frames - first);
            if (!reader->read(&view, first, length, first, true, sample->channels > 1))
                return report("Couldn't decode " + file.getFileName());
            for (int channel = 0; channel < sample->channels; ++channel)
                for (int frame = first; frame < first + length; ++frame)
                    pointers[channel][frame] = safeAudio(pointers[channel][frame]);
        }

        chop::buildWaveform(*sample);
        if (!chop::analyzeTransients(*sample, cancelled))
            return;
        const std::lock_guard<std::mutex> lock(mutex);
        if (cancelled())
            return;
        auto next = std::make_unique<chop::Session>(*published.load());
        next->sample = std::move(sample);
        next->sampleRevision = ++sampleRevision;
        next->edit.sampleBpmTenths = 0;
        next->edit.manual = next->edit.mode == chop::SliceMode::manual
            ? chop::equalSlices(frames, next->edit.density) : chop::Slices {};
        history.clear();
        previewRequest.store(-1);
        loading = false;
        setStatusLocked({});
        publishLocked(std::move(next));
    }
    catch (const std::exception&)
    {
        report("Couldn't load " + file.getFileName());
    }
}

void EpsilonChopProcessor::edit(const Mutation& change, bool remember)
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto* current = published.load();
    auto next = std::make_unique<chop::Session>(*current);
    if (!change(*next))
        return;
    if (remember)
    {
        if (history.size() == historyLimit)
            history.erase(history.begin());
        history.push_back(current->edit);
    }
    publishLocked(std::move(next));
}

void EpsilonChopProcessor::setMode(chop::SliceMode mode)
{
    edit([mode](auto& state)
    {
        if (state.edit.mode == mode)
            return false;
        if (mode == chop::SliceMode::manual && state.sample != nullptr
            && !chop::validSlices(state.edit.manual, state.sample->length()))
            state.edit.manual = state.slices;   // Manual starts from whatever you were looking at
        state.edit.mode = mode;
        return true;
    });
}

void EpsilonChopProcessor::setDensity(int density)
{
    edit([density](auto& state)
    {
        if (density < 2 || density > chop::maxSlices || state.edit.density == density)
            return false;
        state.edit.density = density;
        return true;
    });
}

void EpsilonChopProcessor::setSensitivity(int value, bool remember)
{
    edit([value](auto& state)
    {
        const auto clamped = juce::jlimit(0, 100, value);
        if (state.edit.sensitivity == clamped)
            return false;
        state.edit.sensitivity = clamped;
        return true;
    }, remember);
}

void EpsilonChopProcessor::setBeatDivision(int division)
{
    edit([division](auto& state)
    {
        if ((division != 1 && division != 2 && division != 4) || state.edit.beatDivision == division)
            return false;
        state.edit.beatDivision = division;
        return true;
    });
}

void EpsilonChopProcessor::setSampleBpm(double bpm)
{
    edit([bpm](auto& state)
    {
        const auto tenths = bpm <= 0.0 ? 0 : juce::jlimit(300, 4000, juce::roundToInt(bpm * 10.0));
        if (state.edit.sampleBpmTenths == tenths)
            return false;
        state.edit.sampleBpmTenths = tenths;
        return true;
    });
}

void EpsilonChopProcessor::setSnapZero(bool snap)
{
    edit([snap](auto& state)
    {
        if (state.edit.snapZero == snap)
            return false;
        state.edit.snapZero = snap;
        return true;
    });
}

void EpsilonChopProcessor::rechop()
{
    edit([](auto& state)
    {
        state.edit.chopSeed = state.edit.chopSeed * 747796405u + 2891336453u;
        if (state.edit.mode != chop::SliceMode::random)
            state.edit.mode = chop::SliceMode::random;
        return true;
    });
}

void EpsilonChopProcessor::setBank(int bank)
{
    edit([bank](auto& state)
    {
        if (bank < 0 || bank >= chop::bankCount || bank == state.edit.bank)
            return false;
        state.edit.bank = bank;
        return true;
    }, false);
}

void EpsilonChopProcessor::applyPreset(int preset)
{
    edit([preset](auto& state)
    {
        state.edit.patterns[state.edit.bank] = chop::makePreset(preset);
        return true;
    });
}

void EpsilonChopProcessor::arrangeInOrder()
{
    edit([](auto& state)
    {
        const auto next = chop::inOrder(std::max(1, state.slices.count));
        if (state.edit.patterns[state.edit.bank] == next)
            return false;
        state.edit.patterns[state.edit.bank] = next;
        return true;
    });
}

void EpsilonChopProcessor::clearPattern()
{
    edit([](auto& state)
    {
        if (chop::hitCount(state.edit.patterns[state.edit.bank]) == 0)
            return false;
        state.edit.patterns[state.edit.bank] = {};
        return true;
    });
}

void EpsilonChopProcessor::randomize()
{
    edit([this](auto& state)
    {
        std::vector<chop::Swap> swaps;
        bool shuffled = false;
        const auto changed = chop::randomizePattern(state.edit.patterns[state.edit.bank], std::max(1, state.slices.count),
                                                    state.edit.randomFlags, state.edit.chaos, state.seed, &swaps, &shuffled);
        // Called under the mutex: describe the roll so the editor can show what moved.
        juce::StringArray parts;
        if (shuffled)
            parts.add("Every chop shuffled");
        for (const auto& [a, b] : swaps)
            parts.add(juce::String(a + 1).paddedLeft('0', 2) + juce::String::fromUTF8(" \u2194 ")
                      + juce::String(b + 1).paddedLeft('0', 2));
        lastRoll = parts.isEmpty() ? juce::String() : "Swapped " + parts.joinIntoString("   ");
        if (shuffled)
            lastRoll = parts[0];
        return changed;
    });
}

void EpsilonChopProcessor::setRandomFlags(int flags)
{
    edit([flags](auto& state)
    {
        const auto masked = flags & chop::randomAll;
        if (state.edit.randomFlags == masked)
            return false;
        state.edit.randomFlags = masked;
        return true;
    }, false);
}

void EpsilonChopProcessor::setChaos(int chaos)
{
    edit([chaos](auto& state)
    {
        const auto clamped = juce::jlimit(0, 100, chaos);
        if (state.edit.chaos == clamped)
            return false;
        state.edit.chaos = clamped;
        return true;
    }, false);
}

void EpsilonChopProcessor::undo()
{
    const std::lock_guard<std::mutex> lock(mutex);
    if (history.empty())
        return;
    auto next = std::make_unique<chop::Session>(*published.load());
    next->edit = history.back();
    history.pop_back();
    lastRoll = {};
    publishLocked(std::move(next));
}

void EpsilonChopProcessor::setStep(int index, chop::Step value, bool remember)
{
    if (index < 0 || index >= chop::stepCount)
        return;
    chop::sanitiseStep(value);
    edit([=](auto& state)
    {
        auto& step = state.edit.patterns[state.edit.bank][index];
        if (step == value)
            return false;
        step = value;
        return true;
    }, remember);
}

void EpsilonChopProcessor::editBoundary(int index, int position)
{
    edit([=](auto& state)
    {
        if (state.sample == nullptr)
            return false;
        if (state.edit.mode != chop::SliceMode::manual)
        {
            // Editing a marker in an automatic mode switches to Manual, starting from these cuts.
            state.edit.manual = state.slices;
            state.edit.mode = chop::SliceMode::manual;
        }
        const auto spacing = markerSpacing(*state.sample);
        if (index < 0)
            return chop::insertBoundary(state.edit.manual, position, spacing);
        if (position < 0)
            return chop::removeBoundary(state.edit.manual, index);
        return chop::moveBoundary(state.edit.manual, index, position, spacing);
    });
}

void EpsilonChopProcessor::getStateInformation(juce::MemoryBlock& data)
{
    const auto snapshot = editorState().session;
    juce::MemoryOutputStream out(data, false);
    out.writeInt(stateMagic);
    out.writeInt(stateVersion);
    out.writeFloat(outputParameter->load());
    out.writeInt(patternParameter->load() >= 0.5f ? 1 : 0);
    out.writeInt64(static_cast<juce::int64>(snapshot.seed));
    writeEdit(out, snapshot.edit);
    writeSample(out, snapshot.sample.get());
    juce::String xmlText;
    if (auto xml = parameters.copyState().createXml())
        xmlText = xml->toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
    const auto utf8 = xmlText.toStdString();
    out.writeInt(static_cast<int>(utf8.size()));
    out.write(utf8.data(), utf8.size());
    out.writeInt(stateMagic);   // end marker: a truncated chunk is rejected
    out.flush();
}

void EpsilonChopProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size < 24 || size > 2 * chop::maxSampleFrames * 4 + 65536)
        return;
    try
    {
        juce::MemoryInputStream in(data, static_cast<std::size_t>(size), false);
        if (in.readInt() != stateMagic)
            return;
        const auto version = in.readInt();
        if (version < 1 || version > stateVersion)
            return;
        const auto gain = in.readFloat();
        const auto run = in.readInt();
        const auto seed = in.readInt64();
        auto next = std::make_unique<chop::Session>();
        std::shared_ptr<chop::Sample> sample;
        if (!std::isfinite(gain) || gain < -24.01f || gain > 6.01f || run < 0 || run > 1
            || seed < 0 || seed > 0xffffffffLL || !readEdit(in, next->edit, version) || !readSample(in, sample, version))
            return;
        next->sample = sample;
        if (next->edit.manual.count > 0 && (next->sample == nullptr
            || !chop::validSlices(next->edit.manual, next->sample->length())))
            next->edit.manual = {};
        next->seed = static_cast<std::uint32_t>(seed);

        std::unique_ptr<juce::XmlElement> xml;
        if (version >= 2)
        {
            const auto xmlSize = in.readInt();
            if (xmlSize < 0 || xmlSize > 65536 || in.getNumBytesRemaining() != xmlSize + 4)
                return;
            juce::MemoryBlock text;
            if (xmlSize > 0 && in.readIntoMemoryBlock(text, xmlSize) != xmlSize)
                return;
            if (in.readInt() != stateMagic)
                return;
            if (xmlSize > 0)
                xml = juce::parseXML(text.toString());
        }
        else if (!in.isExhausted())
            return;

        {
            const std::lock_guard<std::mutex> lock(mutex);
            loadSerial.fetch_add(1);
            loading = false;
            next->sampleRevision = ++sampleRevision;
            history.clear();
            previewRequest.store(-1);
            setStatusLocked({});
            publishLocked(std::move(next));
        }
        if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
        auto* output = parameters.getParameter("output");
        output->setValueNotifyingHost(output->convertTo0to1(gain));
        parameters.getParameter("pattern")->setValueNotifyingHost(static_cast<float>(run));
    }
    catch (const std::exception&)
    {
        // Leave the current state available if a host passes an unusable chunk.
    }
}

juce::AudioProcessorEditor* EpsilonChopProcessor::createEditor()
{
    return new EpsilonChopEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EpsilonChopProcessor();
}
