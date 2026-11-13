#pragma once

#include <JuceHeader.h>
#include "ChopEngine.h"

#include <atomic>
#include <functional>
#include <mutex>

class EpsilonChopProcessor final : public juce::AudioProcessor
{
public:
    EpsilonChopProcessor();
    ~EpsilonChopProcessor() override;

    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int size) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Epsilon Chop"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    struct EditorState
    {
        chop::Session session;
        juce::String status;
        bool canUndo = false;
        bool loading = false;
        juce::String lastRoll;
    };

    EditorState editorState() const;
    std::uint64_t revisionNow() const noexcept { return publishedRevision.load(std::memory_order_relaxed); }
    std::uint64_t statusRevision() const noexcept { return statusSerial.load(std::memory_order_relaxed); }

    // Editing (message thread). Every call is undoable unless noted.
    void loadFile(const juce::File&);
    void loadRecording(juce::AudioBuffer<float>, int frames, double rate, const juce::File& folder);
    void saveMobileProject(const juce::File&);
    void openMobileProject(const juce::File&);
    void setMode(chop::SliceMode);
    void setDensity(int);
    void setSensitivity(int, bool remember = true);
    void setBeatDivision(int);
    void setSampleBpm(double bpm);        // <= 0 returns to the estimate
    void setSnapZero(bool);
    void rechop();
    void setBank(int);                    // not undoable
    void applyPreset(int);
    void arrangeInOrder();
    void clearPattern();
    void randomize();
    void setRandomFlags(int);             // not undoable
    void setChaos(int);                   // not undoable
    void setFitAmount(int);               // Dovetail amount, not undoable
    void togglePin(int step);             // pin / unpin a step for Dovetail, not undoable
    void undo();
    void setStep(int index, chop::Step, bool remember = true);
    bool commitPadTake(int bank, std::uint64_t sample, const chop::Pattern&);
    void editBoundary(int index, int position);

    // Audition
    void previewChop(int slice) noexcept { previewRequest.store(slice, std::memory_order_relaxed); }
    void stopAll() noexcept { previewRequest.store(-2, std::memory_order_relaxed); setPlaying(false); }
    // Computer keyboard: 1-9, 0, then Q-P play chops 1-20 (Hit Align applies, like MIDI).
    void keyDown(int chop) noexcept { pushKey(chop + 1); }
    void keyUp(int chop) noexcept { pushKey(-(chop + 1)); }
    void setPlaying(bool shouldPlay) noexcept { playing.store(shouldPlay, std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }

    // Export for FL Studio and other DAWs (message thread). Files are kept in
    // Music/Epsilon Sounds/Epsilon Chop/<sample>/, not a temp folder, because FL links
    // to dragged-in audio by its path: a temp file would vanish from the project later.
    // An unchanged export reuses its file; a changed one gets a new name, so audio
    // already used in a project is never overwritten.
    juce::File exportFolder() const;
    juce::File exportChop(int slice);                 // the raw chop, as cut
    juce::StringArray exportAllChops();               // every chop, numbered
    juce::File exportPattern(int passes = 1);         // this bank as you hear it, a seamless loop at the tempo
    double exportTempo() const;

    // Live readouts for the editor
    int playStep() const noexcept { return currentStep.load(std::memory_order_relaxed); }
    int padRecordStep() const noexcept { return nearestStep.load(std::memory_order_relaxed); }
    int playFrame() const noexcept { return currentFrame.load(std::memory_order_relaxed); }
    int playSlice() const noexcept { return currentSlice.load(std::memory_order_relaxed); }
    bool hostSynced() const noexcept { return synced.load(std::memory_order_relaxed); }
    bool hostHasTempo() const noexcept { return hostTempo.load(std::memory_order_relaxed); }
    double tempo() const noexcept { return currentTempo.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState parameters;

private:
    using Mutation = std::function<bool(chop::Session&)>;
    void edit(const Mutation&, bool remember = true);
    juce::String parameterXml();
    static void writeState(juce::MemoryBlock&, const chop::Session&, const juce::String& xml, float gain, bool followHost);
    bool restoreState(const void*, int size, std::uint64_t ticket = 0);
    void publishLocked(std::unique_ptr<chop::Session>);
    void setStatusLocked(juce::String);
    std::uint64_t beginLoad(juce::String message);
    void finishLoad(std::uint64_t ticket, juce::String message);
    void loadOnWorker(juce::File, std::uint64_t ticket);
    const chop::Session* acquireAudioState() noexcept;
    void reclaimLocked();
    void pushKey(int code) noexcept;
    bool canUndoLocked(int bank) const;
    chop::PlaySettings playSettings() const noexcept;
    juce::File writeWav(const juce::AudioBuffer<float>&, double rate, const juce::String& name);

    struct HistoryEntry
    {
        chop::Edit edit;
        int bank = 0;
        bool patternOnly = false;   // only this bank's pattern changed: undone only from this bank
    };

    mutable std::mutex mutex;
    std::vector<std::unique_ptr<chop::Session>> snapshots;
    std::vector<HistoryEntry> history;
    juce::String status { "Drop a sample anywhere, or click LOAD" };
    bool loading = false;
    juce::String lastRoll;
    std::uint64_t revision = 0, sampleRevision = 0;
    std::atomic<std::uint64_t> publishedRevision { 0 }, statusSerial { 0 };
    std::atomic<const chop::Session*> published { nullptr }, audioHazard { nullptr };
    static_assert(std::atomic<const chop::Session*>::is_always_lock_free);
    std::atomic<std::uint64_t> loadSerial { 0 };
    std::atomic<int> previewRequest { -1 }, currentStep { -1 }, currentFrame { -1 }, currentSlice { -1 };
    std::atomic<int> nearestStep { -1 };
    std::atomic<bool> playing { false }, synced { false }, hostTempo { false };
    std::atomic<double> currentTempo { 120.0 }, hostRate { 0.0 };
    std::atomic<float>* outputParameter = nullptr;
    std::atomic<float>* patternParameter = nullptr;
    std::atomic<float>* rateParameter = nullptr;
    std::atomic<float>* swingParameter = nullptr;
    std::atomic<float>* fitParameter = nullptr;
    std::atomic<float>* rootParameter = nullptr;
    std::atomic<float>* tempoParameter = nullptr;
    std::atomic<float>* chokeParameter = nullptr;
    std::atomic<float>* alignParameter = nullptr;
    std::atomic<float>* vintageParameter = nullptr;
    std::atomic<float>* driveParameter = nullptr;
    std::atomic<float>* masterPitchParameter = nullptr;
    std::atomic<float>* pitchModeParameter = nullptr;
    juce::AbstractFifo keyFifo { 64 };
    std::array<int, 64> keyEvents {};
    chop::Engine engine;
    juce::ThreadPool loader { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonChopProcessor)
};
