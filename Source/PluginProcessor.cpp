#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SampleAnalysis.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int stateMagic = 0x50484345;
constexpr int stateVersion = 3;
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
    // v0.3.0
    const auto percent = juce::AudioParameterFloatAttributes {}.withLabel("%");
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "choke", 1 }, "Choke",
               juce::StringArray { "Cut", "Gate", "Poly" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "align", 1 }, "Hit Align",
               juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 100.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "vintage", 1 }, "Vintage",
               juce::StringArray { "Clean", "12-bit 40k", "12-bit 26k", "16-bit Warm" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "drive", 1 }, "Drive",
               juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 35.0f, percent));
    // v0.5.0: master pitch for the whole loop
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "masterPitch", 2 }, "Pitch",
               juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 0.0f,
               juce::AudioParameterFloatAttributes {}.withLabel("st")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "pitchMode", 2 }, "Pitch Mode",
               juce::StringArray { "MPC", "Keep Length" }, 0));
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
    out.writeInt(edit.fitAmount);
    for (const auto pin : edit.anchor)
        out.writeInt(pin);
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
    const auto dovetailFields = version >= 3 ? 1 + chop::bankCount : 0;
    const auto minimumBytes = 4 * (edit.manual.count + 1 + fieldsPerStep * chop::bankCount * chop::stepCount + dovetailFields + 1);
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
    if (version >= 3)
    {
        edit.fitAmount = input.readInt();
        if (edit.fitAmount < 0 || edit.fitAmount > 100)
            return false;
        for (auto& pin : edit.anchor)
        {
            pin = input.readInt();
            if (pin < -1 || pin >= chop::stepCount)
                return false;
        }
    }
    else
    {
        edit.anchor.fill(-1);
    }
    return true;
}

// v3 stores the sample as 24-bit FLAC (scaled so nothing clips) plus its onsets, so FL
// projects stay small and reopening doesn't have to re-analyse.
bool writeFlac(juce::MemoryOutputStream& out, const chop::Sample& sample)
{
    auto peak = 1.0f;
    for (int channel = 0; channel < sample.channels; ++channel)
        for (const auto value : sample.pcm[channel])
            peak = std::max(peak, std::abs(value));
    juce::MemoryBlock encoded;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(encoded, false);
        juce::FlacAudioFormat flac;
        auto writer = flac.createWriterFor(stream, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate(48000.0)
                                                       .withNumChannels(sample.channels)
                                                       .withBitsPerSample(24)
                                                       .withQualityOptionIndex(5));
        if (writer == nullptr)
            return false;
        constexpr int blockSize = 32768;
        std::vector<float> scaled(static_cast<std::size_t>(blockSize * 2));
        for (int first = 0; first < sample.length(); first += blockSize)
        {
            const auto count = std::min(blockSize, sample.length() - first);
            std::array<const float*, 2> pointers {};
            for (int channel = 0; channel < sample.channels; ++channel)
            {
                auto* destination = scaled.data() + channel * blockSize;
                for (int i = 0; i < count; ++i)
                    destination[i] = sample.pcm[channel][static_cast<std::size_t>(first + i)] / peak;
                pointers[static_cast<std::size_t>(channel)] = destination;
            }
            if (!writer->writeFromFloatArrays(pointers.data(), sample.channels, count))
                return false;
        }
    }
    out.writeInt(1);
    out.writeFloat(peak);
    out.writeInt64(static_cast<juce::int64>(encoded.getSize()));
    out.write(encoded.getData(), encoded.getSize());
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
    juce::MemoryOutputStream flac;
    if (writeFlac(flac, *sample))
        out.write(flac.getData(), flac.getDataSize());
    else
    {
        out.writeInt(0);
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
    const auto onsets = static_cast<int>(std::min<std::size_t>(chop::maxOnsets, sample->onsets.size()));
    out.writeInt(onsets);
    for (int i = 0; i < onsets; ++i)
    {
        out.writeInt(sample->onsets[static_cast<std::size_t>(i)].position);
        out.writeFloat(sample->onsets[static_cast<std::size_t>(i)].strength);
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

    const auto encoding = version >= 3 ? input.readInt() : 0;
    if (encoding == 1)
    {
        const auto peak = input.readFloat();
        const auto bytes = input.readInt64();
        if (!std::isfinite(peak) || peak < 1.0f || peak > 8.01f || bytes <= 0 || bytes > input.getNumBytesRemaining())
            return false;
        juce::MemoryBlock encoded;
        if (input.readIntoMemoryBlock(encoded, static_cast<size_t>(bytes)) != static_cast<std::size_t>(bytes))
            return false;
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader(flac.createReaderFor(new juce::MemoryInputStream(encoded, false), true));
        if (reader == nullptr || static_cast<int>(reader->numChannels) != channels || reader->lengthInSamples != frames)
            return false;
        std::array<float*, 2> pointers {};
        for (int channel = 0; channel < channels; ++channel)
        {
            sample->pcm[channel].resize(static_cast<std::size_t>(frames));
            pointers[static_cast<std::size_t>(channel)] = sample->pcm[channel].data();
        }
        juce::AudioBuffer<float> view(pointers.data(), channels, frames);
        if (!reader->read(&view, 0, frames, 0, true, channels > 1))
            return false;
        for (int channel = 0; channel < channels; ++channel)
            for (auto& value : sample->pcm[channel])
                value = safeAudio(value * peak);
    }
    else if (encoding != 0)
        return false;

    const auto bytesPerChannel = static_cast<juce::int64>(frames) * static_cast<juce::int64>(sizeof(float));
    if (encoding == 0 && input.getNumBytesRemaining() < channels * bytesPerChannel)
        return false;
    for (int channel = 0; channel < channels && encoding == 0; ++channel)
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
    auto analysed = false;
    if (version >= 3)
    {
        const auto onsets = input.readInt();
        if (onsets < 0 || onsets > chop::maxOnsets || input.getNumBytesRemaining() < 8 * onsets)
            return false;
        for (int i = 0; i < onsets; ++i)
        {
            chop::Onset onset;
            onset.position = input.readInt();
            onset.strength = input.readFloat();
            if (onset.position < 0 || onset.position >= frames || !std::isfinite(onset.strength) || onset.strength < 0.0f)
                return false;
            sample->onsets.push_back(onset);
            sample->strongest = std::max(sample->strongest, onset.strength);
        }
        std::sort(sample->onsets.begin(), sample->onsets.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        sample->estimatedBpm = chop::estimateLoopBpm(frames, rate);
        analysed = onsets > 0;
    }
    if (!analysed)
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
    chokeParameter = parameters.getRawParameterValue("choke");
    alignParameter = parameters.getRawParameterValue("align");
    vintageParameter = parameters.getRawParameterValue("vintage");
    driveParameter = parameters.getRawParameterValue("drive");
    masterPitchParameter = parameters.getRawParameterValue("masterPitch");
    pitchModeParameter = parameters.getRawParameterValue("pitchMode");
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
    hostRate.store(sampleRate);
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

    auto settings = playSettings();
    settings.preview = playing.load(std::memory_order_relaxed);
    engine.beginBlock(*current, transport, settings, frames);
    const auto aligned = settings.align > 0.0f;

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

    // Computer keyboard hits from the editor
    {
        const auto scope = keyFifo.read(keyFifo.getNumReady());
        auto handle = [&](int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto code = keyEvents[static_cast<std::size_t>(i)];
                const auto chopIndex = std::abs(code) - 1;
                if (code > 0 && chopIndex < chops)
                    engine.trigger(chopIndex, 1.0f, chop::VoiceOrigin::keys, 0, 0, false, chopIndex, aligned);
                else if (code < 0)
                    engine.noteOff(chop::VoiceOrigin::keys, chopIndex);
            }
        };
        handle(scope.startIndex1, scope.blockSize1);
        handle(scope.startIndex2, scope.blockSize2);
    }
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
                // Notes can't start early, so Hit Align starts the chop at its attack.
                const auto offset = static_cast<int>(message.data[1]) - root;
                engine.trigger(((offset % chops) + chops) % chops, message.data[2] / 127.0f, chop::VoiceOrigin::midi,
                               0, 0, false, message.data[1], aligned);
            }
            else if (statusByte == 0x80 || (statusByte == 0x90 && message.data[2] == 0))
                engine.noteOff(chop::VoiceOrigin::midi, message.data[1]);
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
    // Dovetail fingerprints depend only on the sample and where it's cut.
    const auto* previous = published.load();
    const auto sameCuts = previous != nullptr && previous->fit != nullptr && previous->sample == session->sample
                          && previous->slices.count == session->slices.count
                          && std::equal(previous->slices.edge.begin(), previous->slices.edge.begin() + previous->slices.count + 1,
                                        session->slices.edge.begin());
    if (session->sample == nullptr)
        session->fit = nullptr;
    else if (sameCuts)
        session->fit = previous->fit;
    else
        session->fit = chop::buildFitTable(*session->sample, session->slices);
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
    const auto* current = published.load();
    return { *current, status, canUndoLocked(current->edit.bank), loading, lastRoll };
}

bool EpsilonChopProcessor::canUndoLocked(int bank) const
{
    return std::any_of(history.begin(), history.end(), [bank](const auto& entry) { return !entry.patternOnly || entry.bank == bank; });
}

void EpsilonChopProcessor::pushKey(int code) noexcept
{
    const auto scope = keyFifo.write(1);
    if (scope.blockSize1 > 0)
        keyEvents[static_cast<std::size_t>(scope.startIndex1)] = code;
    else if (scope.blockSize2 > 0)
        keyEvents[static_cast<std::size_t>(scope.startIndex2)] = code;
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
        const auto& before = current->edit;
        const auto& after = next->edit;
        const auto sameChops = before.mode == after.mode && before.density == after.density
            && before.sensitivity == after.sensitivity && before.beatDivision == after.beatDivision
            && before.sampleBpmTenths == after.sampleBpmTenths && before.snapZero == after.snapZero
            && before.chopSeed == after.chopSeed && before.manual.count == after.manual.count
            && std::equal(before.manual.edge.begin(), before.manual.edge.begin() + before.manual.count + 1, after.manual.edge.begin());
        if (history.size() == historyLimit)
            history.erase(history.begin());
        history.push_back({ before, before.bank, sameChops });
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
        std::vector<int> chain;
        bool shuffled = false;
        chop::FitContext fit;
        fit.table = state.fit.get();
        fit.anchor = state.edit.anchor[static_cast<std::size_t>(state.edit.bank)];
        fit.amount = state.edit.fitAmount;
        const auto changed = chop::randomizePattern(state.edit.patterns[state.edit.bank], std::max(1, state.slices.count),
                                                    state.edit.randomFlags, state.edit.chaos, state.seed, &swaps, &shuffled,
                                                    &fit, &chain);
        // Called under the mutex: describe the roll so the editor can show what moved.
        if (!chain.empty())
        {
            juce::StringArray links;
            for (std::size_t i = 0; i < chain.size() && i < 9; ++i)
                links.add(juce::String(chain[i] + 1).paddedLeft('0', 2));
            lastRoll = "Dovetail " + links.joinIntoString(juce::String::fromUTF8(" \u2192 "))
                       + (chain.size() > 9 ? juce::String::fromUTF8(" \u2026") : juce::String());
            return changed;
        }
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

void EpsilonChopProcessor::setFitAmount(int amount)
{
    edit([amount](auto& state)
    {
        const auto clamped = juce::jlimit(0, 100, amount);
        if (state.edit.fitAmount == clamped)
            return false;
        state.edit.fitAmount = clamped;
        return true;
    }, false);
}

void EpsilonChopProcessor::togglePin(int step)
{
    edit([step](auto& state)
    {
        if (step < 0 || step >= chop::stepCount)
            return false;
        auto& pin = state.edit.anchor[static_cast<std::size_t>(state.edit.bank)];
        if (pin == step)
            pin = -1;
        else if (state.edit.patterns[static_cast<std::size_t>(state.edit.bank)][static_cast<std::size_t>(step)].slice >= 0)
            pin = step;
        else
            return false;
        return true;
    }, false);
}

// Undo works per bank: a pattern edit is undone only from the bank it was made in,
// and chop edits (mode, cuts, BPM) from anywhere. Bank, Chaos, toggles and pins stay.
void EpsilonChopProcessor::undo()
{
    const std::lock_guard<std::mutex> lock(mutex);
    auto next = std::make_unique<chop::Session>(*published.load());
    const auto bank = next->edit.bank;
    const auto found = std::find_if(history.rbegin(), history.rend(),
                                    [bank](const auto& entry) { return !entry.patternOnly || entry.bank == bank; });
    if (found == history.rend())
        return;
    const auto& entry = *found;
    if (entry.patternOnly)
        next->edit.patterns[static_cast<std::size_t>(bank)] = entry.edit.patterns[static_cast<std::size_t>(bank)];
    else
    {
        auto restored = entry.edit;
        restored.bank = bank;
        restored.randomFlags = next->edit.randomFlags;
        restored.chaos = next->edit.chaos;
        restored.fitAmount = next->edit.fitAmount;
        restored.anchor = next->edit.anchor;
        // Other banks keep any later edits that have their own undo entries.
        for (int b = 0; b < chop::bankCount; ++b)
            if (b != entry.bank)
                restored.patterns[static_cast<std::size_t>(b)] = next->edit.patterns[static_cast<std::size_t>(b)];
        next->edit = restored;
    }
    history.erase(std::next(found).base());
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
        if (version < 3)
        {
            // Projects from v0.1/v0.2 keep sounding exactly as they did.
            auto set = [this](const char* id, float plain)
            {
                auto* parameter = parameters.getParameter(id);
                parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
            };
            set("choke", 1.0f);
            set("align", 0.0f);
            set("vintage", 0.0f);
        }
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

// ============================================================================
// Export

chop::PlaySettings EpsilonChopProcessor::playSettings() const noexcept
{
    constexpr std::array<int, 3> stepsPerQuarter { 2, 4, 8 };
    chop::PlaySettings settings;
    settings.followHost = patternParameter->load(std::memory_order_relaxed) >= 0.5f;
    settings.stepsPerQuarter = stepsPerQuarter[static_cast<std::size_t>(
        juce::jlimit(0, 2, juce::roundToInt(rateParameter->load(std::memory_order_relaxed))))];
    settings.swing = swingParameter->load(std::memory_order_relaxed) * 0.01f;
    settings.tempoFit = fitParameter->load(std::memory_order_relaxed) >= 0.5f;
    settings.fallbackBpm = tempoParameter->load(std::memory_order_relaxed);
    settings.gain = juce::Decibels::decibelsToGain(outputParameter->load(std::memory_order_relaxed));
    settings.choke = static_cast<chop::Choke>(juce::jlimit(0, chop::chokeCount - 1, juce::roundToInt(chokeParameter->load(std::memory_order_relaxed))));
    settings.align = alignParameter->load(std::memory_order_relaxed) * 0.01f;
    settings.vintage = static_cast<chop::Vintage>(juce::jlimit(0, chop::vintageCount - 1, juce::roundToInt(vintageParameter->load(std::memory_order_relaxed))));
    settings.drive = driveParameter->load(std::memory_order_relaxed) * 0.01f;
    settings.masterPitch = masterPitchParameter->load(std::memory_order_relaxed);
    settings.pitchMode = static_cast<chop::PitchMode>(juce::jlimit(0, chop::pitchModeCount - 1,
                                                                   juce::roundToInt(pitchModeParameter->load(std::memory_order_relaxed))));
    return settings;
}

double EpsilonChopProcessor::exportTempo() const
{
    // The host's tempo once it has told us one, otherwise the TEMPO box.
    const auto t = hostHasTempo() ? tempo() : static_cast<double>(tempoParameter->load());
    return juce::jlimit(20.0, 400.0, std::isfinite(t) ? t : 120.0);
}

namespace
{
juce::String safeName(juce::String s)
{
    for (auto* ext : { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a" })
        if (s.endsWithIgnoreCase(ext))
            s = s.dropLastCharacters(static_cast<int>(std::strlen(ext)));
    s = juce::File::createLegalFileName(s).trim();
    return s.isEmpty() ? juce::String("Untitled") : s.substring(0, 60);
}
}

juce::File EpsilonChopProcessor::exportFolder() const
{
    const auto state = editorState();
    auto folder = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                      .getChildFile("Epsilon Sounds").getChildFile("Epsilon Chop");
    if (state.session.sample != nullptr)
        folder = folder.getChildFile(safeName(juce::String::fromUTF8(state.session.sample->name.c_str())));
    return folder;
}

juce::File EpsilonChopProcessor::writeWav(const juce::AudioBuffer<float>& audio, double rate, const juce::String& name)
{
    juce::MemoryBlock bytes;
    {
        juce::WavAudioFormat wav;
        auto stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(rate)
                                 .withNumChannels(audio.getNumChannels())
                                 .withBitsPerSample(24);
        std::unique_ptr<juce::OutputStream> out(stream.release());
        auto writer = wav.createWriterFor(out, options);
        if (writer == nullptr || !writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
            return {};
    }
    const auto folder = exportFolder();
    if (!folder.createDirectory())
        return {};
    auto file = folder.getChildFile(name + ".wav");
    // Same name and same audio: reuse it. Same name, different audio: never overwrite a
    // file a project may already use; take the next free name instead.
    for (int n = 2; file.existsAsFile(); ++n)
    {
        juce::MemoryBlock existing;
        if (file.getSize() == static_cast<juce::int64>(bytes.getSize()) && file.loadFileAsData(existing) && existing == bytes)
            return file;
        file = folder.getChildFile(name + " (" + juce::String(n) + ").wav");
    }
    return file.replaceWithData(bytes.getData(), bytes.getSize()) ? file : juce::File();
}

juce::File EpsilonChopProcessor::exportChop(int slice)
{
    const auto state = editorState();
    const auto& sample = state.session.sample;
    const auto& slices = state.session.slices;
    if (sample == nullptr || slice < 0 || slice >= slices.count)
        return {};
    const auto start = slices.edge[static_cast<std::size_t>(slice)];
    const auto frames = slices.edge[static_cast<std::size_t>(slice + 1)] - start;
    if (frames <= 0)
        return {};
    const auto channels = juce::jlimit(1, 2, sample->channels);
    juce::AudioBuffer<float> audio(channels, frames);
    for (int c = 0; c < channels; ++c)
        audio.copyFrom(c, 0, sample->pcm[static_cast<std::size_t>(c)].data() + start, frames);
    // Click guards: 0.3 ms in (keeps the attack), 2 ms out.
    const auto in = std::min(frames / 4, std::max(1, juce::roundToInt(sample->sampleRate * 0.0003)));
    const auto out = std::min(frames / 4, std::max(1, juce::roundToInt(sample->sampleRate * 0.002)));
    for (int c = 0; c < channels; ++c)
    {
        auto* d = audio.getWritePointer(c);
        for (int i = 0; i < in; ++i)
            d[i] *= static_cast<float>(i) / static_cast<float>(in);                      // first frame exactly 0
        for (int i = 0; i < out; ++i)
            d[frames - 1 - i] *= static_cast<float>(i) / static_cast<float>(out);        // last frame exactly 0
    }
    const auto name = safeName(juce::String::fromUTF8(sample->name.c_str())) + " - Chop " + juce::String(slice + 1).paddedLeft('0', 2);
    return writeWav(audio, sample->sampleRate, name);
}

juce::StringArray EpsilonChopProcessor::exportAllChops()
{
    juce::StringArray files;
    const auto count = editorState().session.slices.count;
    for (int i = 0; i < count; ++i)
        if (const auto f = exportChop(i); f.existsAsFile())
            files.add(f.getFullPathName());
    return files;
}

juce::File EpsilonChopProcessor::exportPattern(int passes)
{
    const auto state = editorState();
    if (state.session.sample == nullptr || chop::hitCount(state.session.edit.patterns[static_cast<std::size_t>(state.session.edit.bank)]) == 0)
        return {};
    passes = juce::jlimit(1, 16, passes);
    const auto rate = hostRate.load() >= 8000.0 ? hostRate.load() : state.session.sample->sampleRate;
    const auto bpm = exportTempo();
    auto settings = playSettings();
    settings.followHost = true;
    settings.preview = false;
    const auto loopQuarters = static_cast<double>(chop::stepCount) / settings.stepsPerQuarter;
    const auto framesAt = [&](double quarters) { return static_cast<juce::int64>(std::llround(quarters * 60.0 / bpm * rate)); };
    // One pass first so tails ringing over the loop point are already there:
    // the clip then loops seamlessly in the playlist.
    const auto keepFrom = framesAt(loopQuarters);
    const auto total = framesAt(loopQuarters * (passes + 1));
    const auto keep = static_cast<int>(total - keepFrom);

    chop::Engine offline;
    offline.prepare(rate, settings.gain);
    juce::AudioBuffer<float> audio(2, keep);
    audio.clear();
    constexpr int block = 512;
    for (juce::int64 at = 0; at < total; at += block)
    {
        const auto n = static_cast<int>(std::min<juce::int64>(block, total - at));
        chop::Transport transport;
        transport.bpm = bpm;
        transport.ppq = static_cast<double>(at) / rate * bpm / 60.0;
        transport.playing = transport.hasPosition = true;
        offline.beginBlock(state.session, transport, settings, n);
        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            offline.render(i, l, r);
            const auto index = at + i - keepFrom;
            if (index >= 0)
            {
                audio.setSample(0, static_cast<int>(index), l);
                audio.setSample(1, static_cast<int>(index), r);
            }
        }
        offline.endBlock();
    }
    const auto bank = juce::String::charToString(static_cast<juce::juce_wchar>('A' + state.session.edit.bank));
    const auto name = safeName(juce::String::fromUTF8(state.session.sample->name.c_str())) + " - Pattern " + bank + " "
                    + juce::String(bpm, bpm == std::floor(bpm) ? 0 : 1) + " BPM";
    return writeWav(audio, rate, name);
}
