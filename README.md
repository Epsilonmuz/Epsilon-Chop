# Epsilon Chop

**Epsilon Sounds · v0.2.0 · sample chopper instrument (VST3 · AU · Standalone)**

Drop in a sample. Epsilon Chop cuts it into colour-coded chops, lays them out on a 16-step ribbon, and rearranges them with one roll of the dice. You can hear it right away. Press **PLAY**, and FL Studio doesn't need to be running.

![Epsilon Chop v0.2.0](preview.png)

[Download the v0.2.0 Projucer project (JUCE 9.0.5 included)](Epsilon-Chop-v0.2.0-Projucer.zip)

## What changed in v0.2.0

v0.1.0 seemed broken because nothing made a sound unless FL Studio's transport was running *and* Play Pattern was on. **Random** only changed the numbers in the step boxes. FL's step sequencer plays **C5**, but chops started at C3, so that was silent too.

| Problem in v0.1.0 | v0.2.0 |
| --- | --- |
| No way to hear the pattern without starting FL | **PLAY** button with its own clock. It uses FL's tempo when FL reports one, and the internal tempo otherwise |
| Random only swapped chop numbers | **RANDOMIZE** with a **Chaos** amount. **SWAP** makes chops trade places (every 11 becomes 13 and every 13 becomes 11) and shows each swap under the button. Other toggles: Rhythm, Stutter, Reverse, Pitch and Gate. RANDOMIZE starts PLAY so you hear the flip at once |
| Three chop modes | **Transient** with Sensitivity, **Beat** at 1/4, 1/8 or 1/16 and snapped to hits, **Grid**, **Random** with re-chop, and **Manual**. Every cut snaps to a zero crossing |
| C5 was silent | **MIDI root C5 = Chop 01**, and every key wraps onto a chop. The root is changeable in the footer |
| Steps had only a chop and a gate | Each step also has **ratchet** (1–4 repeats), **pitch** (±12 st) and **reverse** |
| Whole UI redrawn 30 times a second, Debug at -O0 | Waveform and ribbon are cached, and only the playhead redraws. Debug is optimised (-O3) |

Saved v0.1.0 sessions still load. The plug-in IDs (`EpsS` / `EpCh`) and the `pattern` / `output` parameters are unchanged.

## Build on the 2019 Intel MacBook Pro

1. Unzip and keep `JUCE`, `JuceLibraryCode`, `Source`, `Resources` and `Builds` together.
2. Double-click **`Epsilon-Chop.jucer`**. In Projucer, click the Xcode button, or open `Builds/MacOSX/Epsilon Chop.xcodeproj` directly.
3. Pick **Epsilon Chop - All** and **My Mac**, then **Product → Build**. Debug and Release are both -O3. The VST3 is copied to `~/Library/Audio/Plug-Ins/VST3` and the AU to `~/Library/Audio/Plug-Ins/Components`.
4. In FL Studio, choose **Options → Manage plugins → Find installed plugins**. Add **Epsilon Chop** to the Channel Rack as a generator.

The **Epsilon Chop - Standalone Plugin** target builds an app, so you can try chopping without opening FL.

The CMake build is an alternative. Put JUCE 9.0.5 in `JUCE/`, which the ZIP already has, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEPSILON_CHOP_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Using it

1. **Load.** Drop a WAV, AIFF, FLAC or MP3 anywhere on the window, or click **LOAD SAMPLE**. The limit is 120 seconds.
2. **Chop.** Choose a mode in the bar above the waveform. Click a chop to hear it. Drag a cut line to move it, which switches to Manual. Double-click to add a cut, and right-click a cut to remove it. **LOOP BPM** is detected from the loop length. Drag it to correct it, and double-click it to re-detect.
3. **Arrange.** Drag a chop from the waveform onto any step. Scroll over a step to change its chop. Right-click or double-click a step to make it a rest. **IN ORDER** lays the chops out as recorded, and **GROOVES** loads starting rhythms. **A–D** are four independent banks.
4. **Play.** Press **PLAY** to preview on the internal clock. Turn on **HOST SYNC** to play the bank whenever FL's transport runs, locked to the bar. **RATE** sets 1/8, 1/16 or 1/32 steps, and **SWING** adds shuffle. **TEMPO FIT** repitches the sample so its loop BPM matches the tempo, which keeps chops on the grid.
5. **Flip it.** Pick what **RANDOMIZE** may change, set **CHAOS**, and roll. With **SWAP**, chops trade places and the readout shows which ones (for example `Swapped 11 ↔ 13`). **UNDO** steps back through up to 32 edits.
6. **Shape a step.** Click a step, then set its chop, gate, pitch, ratchet and reverse in the **STEP** panel.
7. **Piano Roll.** C5 plays Chop 01, C#5 plays Chop 02, and so on. Keys wrap around, so every note plays something.

Each instance saves its decoded sample inside the FL project, so projects reopen even if the original file has moved.

## Validation (Linux build of the same source)

- Engine tests cover:
  - all five chop modes
  - zero-crossing snap
  - randomizer invariants per toggle
  - the internal clock with no host (step timing at 1/8 and 1/16)
  - host-synced output that is identical at 64, 257 and 1,024-frame blocks
  - ratchet retrigger timing
  - reverse playback
  - octave-up length
- Processor tests cover:
  - WAV import and onset timing within 21 ms
  - mode switching, re-chop, and drag-to-manual
  - randomize and undo with bank isolation
  - MIDI root and wrapping
  - PLAY with FL stopped
  - host sync start and stop
  - sample-exact project recall
  - rejection of a truncated state
  - loading a v0.1.0 session
- pluginval strictness 8: **SUCCESS**.
- Full editor repaint takes about 5 ms in software rendering. In normal use only the playhead strip and ribbon redraw.

The macOS build and FL Studio scan are still to be checked on the Mac. `.github/workflows/build.yml` builds a universal VST3, AU and Standalone on macOS 15, runs the tests and pluginval, and uploads them with `INSTALL.txt`.

**[Full manual: every control and how it works](MANUAL.md)** · [Design and research](DESIGN.md) · Fonts: Instrument Sans and JetBrains Mono, SIL OFL 1.1 (`Resources/licenses`) · [JUCE licence](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
