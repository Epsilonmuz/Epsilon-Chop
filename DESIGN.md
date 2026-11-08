# Epsilon Chop v0.1.0 · Design and research

## Workflow research

| Reference | Behavior studied | Choice in Epsilon Chop |
| --- | --- | --- |
| Image-Line Slicex Wave Editor | Peak-based slicing, grid slicing, editable regions and MIDI note assignment. It also offers zero-crossing correction for cut boundaries. | Grid for loop material, spectral-flux attacks for uneven material, and editable markers in one waveform. A short playback envelope softens cuts without shifting the marker. |
| Ableton Simpler | A choice of transient, beat, equal-region and manual slicing with MIDI-triggered slices. | A density selector limits automatic slices. Grid divides the selected file by length. Host tempo controls the sequencer, and a chop’s original length controls its natural playback end. |
| Logic Pro Quick Sampler | Editable markers, fades and a selectable first MIDI note. | Draggable sample-frame boundaries and automatic short fades. A fixed first note of 36 makes the FL Studio Piano Roll mapping predictable across instances. |
| Böck and Widmer, DAFx 2013 | A frequency-axis maximum filter reduces false novelty caused by vibrato moving energy between nearby bins. | A three-bin neighborhood in positive log spectral flux. The detector uses its own window, local threshold, spacing and attack refinement. |
| J. O. Smith, *Physical Audio Signal Processing* | Band-limited reconstruction with a finite, windowed sinc kernel. | A compact precomputed low-pass kernel for rate conversion, plus direct PCM reads at matched rates. |
| JUCE AudioPlayHead and Image-Line plug-in documentation | Host PPQ, BPM and transport state, and FL Studio’s macOS VST3 scanning workflow. | A VST3 instrument with a PPQ scheduler and sample-offset MIDI notes. The Projucer exporter generates a 64-bit Intel build. |

[Image-Line Slicex Wave Editor](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual-de/html/plugins/Slicex%20Editor.htm) · [Ableton Live Instrument Reference](https://www.ableton.com/en/manual/live-instrument-reference/#slicing-playback-mode) · [Apple Quick Sampler Slice Mode](https://support.apple.com/guide/logicpro/quick-sampler-slice-mode-lgcp18515788/mac) · [Böck and Widmer, 2013](https://phenicx.upf.edu/system/files/publications/Boeck_DAFx-13.pdf) · [Smith, Windowed Sinc Interpolation](https://www.dsprelated.com/freebooks/pasp/Windowed_Sinc_Interpolation.html) · [JUCE PositionInfo](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html) · [FL Studio Plug-in Installation](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)

## Why the two variation buttons are separate

Each bank is an array of 16 slots. A slot is either a rest or a pair of chop number and gate percentage. The sample and slice map live separately from these patterns.

- **Random** draws new slice IDs only for occupied slots. This makes it possible to keep a Jersey club rhythm intact while auditioning completely different parts of a vocal or melody. When the draw reproduces every existing assignment, one occupied slot is changed.
- **Move & Trim** chooses new unique positions for the same hits and places the hits in their original order. Full patterns rotate because every position is occupied. Each gate is capped at one of 25%, 50% or 75%. A short existing gate stays short.
- Bank A starts with an even eighth-note pulse, B has offbeats and a short repeated hit, C leaves space between hits, and D has a syncopated accent pattern. These are editable one-bar starting points. The host’s BPM determines their pace.

## Detection and playback

**Chop boundaries.** The worker decodes native-rate PCM and creates a cached 1,536-column min/max waveform. Grid boundaries use integer division of total source frames. In Transient, the detector uses a Hann window with a power-of-two FFT of at least 16 ms and a quarter-window hop. It uses the higher of the two channels’ log magnitudes, so opposite stereo polarity cannot cancel a hit. The novelty score sums positive changes relative to the maximum of the three neighboring bins in the preceding frame. Local maxima must clear both an adaptive neighborhood threshold and a small absolute floor. An attack refinement around each candidate uses a local amplitude threshold with about 1 ms of lead-in. A 35 ms minimum spacing keeps one attack from becoming several chops. The strongest eligible boundaries are selected, then sorted by source position. Sustained, very quiet, or closely spaced sounds may need Manual cleanup. Manual enforces 4 ms minimum spacing between newly edited boundaries.

**Host timing.** Quarter-note position from the host is multiplied by four to get sixteenth-note phase. Inside a block, phase advances by BPM divided by 15 times the host sample rate. Crossing an integer phase fires the corresponding step modulo 16. The gate in samples is the rounded step duration times the gate fraction, capped by the chop’s natural playback length. A host jump resets the phase anchor, and stopping the host releases the sequencer voice. MIDI input uses the timestamp attached to each note event. The pattern stays anchored to four quarter notes from PPQ zero, including in other time signatures. Hosts that omit PPQ or BPM can still trigger chops from MIDI.

**Resampling and cut edges.** Source position advances by source rate divided by host rate. Rate mismatches use 32 Blackman-windowed sinc taps, a table of 64 fractional phase intervals, and a cutoff table that reduces bandwidth when downsampling. Coefficients are normalized for unity DC gain. Reads are clamped to the active slice. Native-rate playback reads PCM directly. A smooth fade of up to 2 ms at each boundary and during a choke reduces clicks. Very short chops get a shorter fade. This preserves the clip’s pitch and speed as tempo changes. The sequencer can trim the playback window.

**Threading and project recall.** File I/O and FFT analysis run on a worker. The render callback reads a published immutable state, holds it with a hazard pointer for the block, and uses a fixed voice pool. Snapshot reclamation, allocations and edits run outside the callback. State stores the PCM, transient candidates, mode, manual boundaries, patterns, selected bank, output gain and variation seed. Restore validates bounds and recalculates waveform peaks. Because sample audio is embedded, the project can reopen after the original file is moved.

## Validation

| Check | Result |
| --- | --- |
| Linux x86-64 Release VST3 | Built with GCC and JUCE 8.0.17-security. The JUCE manifest helper loaded the VST3 factory and reported Instrument / Sampler. |
| Portable engine | Grid and manual boundaries, variation invariants, gates, seek behavior, and identical scheduling with 64-, 257- and 1,024-frame blocks passed. |
| Sample-rate conversion | A 70 kHz tone downsampled from 192 kHz to 48 kHz met the test’s 49 dB alias-rejection threshold relative to the low-frequency reference. This is one tested frequency and rate pair. |
| Processor integration | The synthetic stereo WAV decoded on the worker. All six interior attacks were located within 21 ms. Manual marker editing, bank isolation, undo, MIDI offsets, host stop, and rejection of truncated state passed. |
| Project recall | After deleting the fixture file, restoring the plug-in state reproduced sample-exact stereo MIDI output. |
| Editor and Intel export | The included preview was rendered directly from the running JUCE editor. Projucer regenerated the Xcode project and reported the expected modules. The exporter sets x86_64, VST3 Instrument / Sampler, and MIDI input. |
| macOS / FL Studio | Build, scan, interactive transport, FLP round-trip, and render remain to be checked on the Intel Mac. |

Build and run the native tests with CMake 3.22 or newer and the JUCE platform dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEPSILON_CHOP_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On the Mac, scan the built VST3 in FL Studio, load a short WAV, audition Chop 01, play the internal pattern at 44.1 kHz and 48 kHz, and trigger a few notes from the Piano Roll. Save an FLP, close it, and reopen it to verify the waveform and banks. Export one bar and check that the first hit and rests match the visible pattern.
