#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chop
{
constexpr int maxSlices = 32;
constexpr int stepCount = 16;
constexpr int bankCount = 4;
constexpr int defaultRootNote = 60;   // C5 in FL Studio, which is what its step sequencer plays
constexpr int waveformBins = 2048;
constexpr int maxOnsets = 256;
constexpr int maxSampleFrames = 8'000'000;
constexpr int maxSampleSeconds = 120;
constexpr int maxRatchet = 4;
constexpr int maxPitch = 12;

// Numeric values 0-2 are kept from v0.1.0 so saved sessions keep their mode.
enum class SliceMode : int { grid = 0, transient = 1, manual = 2, beat = 3, random = 4 };
constexpr int sliceModeCount = 5;
enum class VoiceOrigin : int { midi, pattern, preview };

enum RandomFlag : int
{
    randomChops   = 1 << 0,
    randomRhythm  = 1 << 1,
    randomStutter = 1 << 2,
    randomReverse = 1 << 3,
    randomPitch   = 1 << 4,
    randomGate    = 1 << 5,
    randomAll     = (1 << 6) - 1
};
constexpr int defaultRandomFlags = randomChops | randomRhythm | randomStutter;

struct Peak { float low = 0.0f, high = 0.0f; };
struct Onset { int position = 0; float strength = 0.0f; };

struct Sample
{
    std::string name;
    double sampleRate = 44100.0;
    int channels = 0;
    std::array<std::vector<float>, 2> pcm;
    std::array<Peak, waveformBins> waveform {};
    std::vector<Onset> onsets;          // sorted by position
    float strongest = 0.0f;
    double estimatedBpm = 120.0;

    int length() const noexcept { return static_cast<int>(pcm[0].size()); }
};

struct Slices
{
    std::array<int, maxSlices + 1> edge {};
    int count = 0;
};

struct Step
{
    int slice = -1;     // -1 = rest
    int gate = 100;     // 10..100 % of a step
    int ratchet = 1;    // 1..4 repeats inside the step
    int pitch = 0;      // -12..+12 semitones
    bool reverse = false;
    bool operator==(const Step& o) const noexcept
    {
        return slice == o.slice && gate == o.gate && ratchet == o.ratchet && pitch == o.pitch && reverse == o.reverse;
    }
    bool operator!=(const Step& o) const noexcept { return !(*this == o); }
};

using Pattern = std::array<Step, stepCount>;

struct Edit
{
    SliceMode mode = SliceMode::transient;
    int density = 16;          // Grid / Random: chop count. Transient: maximum chops.
    int sensitivity = 60;      // Transient: 0..100
    int beatDivision = 2;      // Beat: chops per beat (1, 2, 4)
    int sampleBpmTenths = 0;   // 0 = use the estimate
    bool snapZero = true;
    std::uint32_t chopSeed = 0x2b7e1516u;
    int bank = 0;
    int randomFlags = defaultRandomFlags;
    int chaos = 50;
    Slices manual;
    std::array<Pattern, bankCount> patterns;
};

struct Session
{
    std::shared_ptr<const Sample> sample;
    Edit edit;
    Slices slices;
    std::uint64_t sampleRevision = 0;
    std::uint64_t revision = 0;
    std::uint32_t seed = 0x7f3259ac;
};

// ---- slicing ---------------------------------------------------------------
bool validSampleSize(double rate, int channels, std::int64_t frames) noexcept;
Slices equalSlices(int frames, int density) noexcept;
Slices makeSlices(const Sample&, const Edit&) noexcept;
bool validSlices(const Slices&, int frames) noexcept;
bool insertBoundary(Slices&, int position, int minimumSpacing) noexcept;
bool moveBoundary(Slices&, int index, int position, int minimumSpacing) noexcept;
bool removeBoundary(Slices&, int index) noexcept;
double sampleBpm(const Sample&, const Edit&) noexcept;
double estimateLoopBpm(int frames, double sampleRate) noexcept;

// ---- patterns --------------------------------------------------------------
constexpr int presetCount = 4;
const char* presetName(int index) noexcept;
Pattern makePreset(int index) noexcept;
Pattern inOrder(int slices) noexcept;
Edit defaultEdit() noexcept;
bool sanitiseStep(Step&) noexcept;
// CHOPS swaps chops' places: a swap of 11 and 13 turns every 11 into 13 and every
// 13 into 11, timing untouched. Chaos sets how many swaps; 100% shuffles every chop.
// The swaps made are reported in `swaps` (empty after a full shuffle).
using Swap = std::pair<int, int>;
bool randomizePattern(Pattern&, int slices, int flags, int chaos, std::uint32_t& seed,
                      std::vector<Swap>* swaps = nullptr, bool* fullShuffle = nullptr) noexcept;
int hitCount(const Pattern&) noexcept;

// ---- playback --------------------------------------------------------------
struct Transport
{
    double ppq = 0.0;
    double bpm = 0.0;
    bool playing = false;
    bool hasPosition = false;
};

struct PlaySettings
{
    bool followHost = false;    // run the pattern when the host transport plays
    bool preview = false;       // run the pattern on the internal clock
    int stepsPerQuarter = 4;    // 2 = 1/8, 4 = 1/16, 8 = 1/32
    float swing = 0.0f;         // 0..0.6 of a step pair offset
    bool tempoFit = false;      // repitch the sample so its loop BPM matches the tempo
    double fallbackBpm = 120.0; // used when the host gives no tempo
    float gain = 1.0f;
};

class Engine
{
public:
    void prepare(double outputRate, float initialGain = 1.0f);
    void reset() noexcept;
    void beginBlock(const Session&, const Transport&, const PlaySettings&, int samples) noexcept;
    void trigger(int slice, float velocity, VoiceOrigin, int gateFrames = 0, int pitch = 0, bool reverse = false) noexcept;
    void stop(VoiceOrigin) noexcept;
    void render(int sampleInBlock, float& left, float& right) noexcept;
    void endBlock() noexcept { sample = nullptr; slices = nullptr; pattern = nullptr; }

    int currentStep() const noexcept { return playStep; }
    int currentFrame() const noexcept { return playFrame; }
    int currentSlice() const noexcept { return playSlice; }
    bool hostSynced() const noexcept { return clock == Clock::host; }
    bool running() const noexcept { return clock != Clock::off; }
    double tempo() const noexcept { return bpm; }

private:
    enum class Clock { off, host, internal };

    struct Voice
    {
        double position = 0.0, increment = 1.0;
        int start = 0, end = 0;
        int frames = 0, elapsed = 0;
        int fade = 0, release = 0, releaseLength = 0;
        int filter = 0, slice = -1;
        float velocity = 0.0f;
        VoiceOrigin origin = VoiceOrigin::midi;

        bool active() const noexcept { return elapsed < frames; }
    };

    struct StepPoint { std::int64_t step; double local; double duration; };
    StepPoint locate(double tick) const noexcept;
    void release(Voice&) const noexcept;
    void advancePattern(int sampleInBlock) noexcept;
    float interpolate(const float*, const Voice&) const noexcept;

    const Sample* sample = nullptr;
    const Slices* slices = nullptr;
    const Pattern* pattern = nullptr;
    std::array<Voice, 8> voices {};
    std::uint64_t currentSample = ~0ull;
    double sampleRate = 44100.0, baseRatio = 1.0, speed = 1.0, bpm = 120.0;
    double tickStart = 0.0, tickIncrement = 0.0, expectedTick = 0.0, internalTick = 0.0;
    std::int64_t previousEvent = 0;
    float swing = 0.0f;
    Clock clock = Clock::off;
    bool previewWasOn = false;
    int microfadeFrames = 88, playStep = -1, playFrame = -1, playSlice = -1;
    int replacementFade = 0;
    float gain = 1.0f, gainTarget = 1.0f, gainStep = 1.0f;
    float lastLeft = 0.0f, lastRight = 0.0f, replacedLeft = 0.0f, replacedRight = 0.0f;
};
}
