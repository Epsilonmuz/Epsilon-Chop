# Epsilon Chop

**Epsilon Sounds · v0.3.0 · sample chopper instrument (VST3 · AU · Standalone)**

**Pin the chop you love. Epsilon Chop builds the rest around it.**

Drop in a sample. Epsilon Chop cuts it into colour-coded chops, lays them on a 16-step ribbon and flips them with one roll of the dice. Pin one chop and **Dovetail** picks every other chop so it flows out of the one before it: same key, smooth level, matching tone, all the way round the bar. **Hit Align** lands every hit exactly on the grid whatever chop goes where, **CUT** choke works like FL Studio's "Cut by 1" on every pad, and **VINTAGE** adds the punch and grit of classic 12-bit samplers. Finger-drum chops straight from your computer keyboard.

![Epsilon Chop v0.3.0](preview.png)

[Download the v0.3.0 Projucer project (JUCE 9.0.5 included)](Epsilon-Chop-v0.3.0-Projucer.zip)

## New in v0.3.0

| Feature | What it does |
| --- | --- |
| **Dovetail** | **PIN** a step, then RANDOMIZE. Every other hit gets the chop that best continues the one before it, judged on key, loudness and tone as each step actually plays, and learned from how your own sample flows. The whole chain is chosen at once and wraps back into the pinned step, so the loop point fits too. **DOVETAIL** sets how strict, **CHAOS** how adventurous. On the ribbon, cells that fit lock together and glow. |
| **Hit Align** | Measures how many ms after its cut each chop's attack really starts, and starts it that much early, so every hit lands on the grid (within 0.6 ms in tests; without it, late chops dragged up to 8 ms). MIDI and keyboard hits start right at the attack. |
| **CHOKE: CUT / GATE / POLY** | CUT (default) = every pad on "Cut by 1": each hit rings until the next cuts it. GATE = v0.2 behaviour, and MIDI note lengths now count. POLY = hits overlap. |
| **VINTAGE + DRIVE** | 12-BIT 40K (punchy, warm), 12-BIT 26K (gritty, aliasing), 16-BIT WARM, or CLEAN. Analogue input saturation, sample-and-hold converter, 12/16-bit steps, output filter and low-end weight. |
| **Computer keyboard** | 1–0 play chops 01–10 and Q–P play 11–20. Each key is shown next to its chop on the waveform. |
| **Fixes** | Undo is per bank. Low CHAOS keeps your hand-set ratchets, reverses and pitches (it's now the chance each hit is re-rolled). The swap readout is always exactly what happened (pairs never share a chop). ZERO-X only snaps cuts backwards, so attacks are never cut off, and voices fade in over 0.3 ms instead of 2 ms, so hits stay sharp. |
| **Lighter and faster** | The sample is saved in the FL project as 24-bit FLAC with its analysis, so projects are much smaller and reopen without re-analysing. The waveform and ribbon only redraw what moved during playback, which matters on the Intel MacBook Pro. |

Projects from v0.1 and v0.2 open and sound exactly as before (Choke = Gate, Hit Align = 0, Vintage = Clean). The plug-in IDs (`EpsS` / `EpCh`) are unchanged.

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

1. **Load.** Drop a WAV, AIFF, FLAC or MP3 anywhere on the window, or click **LOAD SAMPLE** (up to 120 s).
2. **Chop.** Pick TRANSIENT, BEAT, GRID, RANDOM or MANUAL. Click a chop to hear it, drag a cut to move it, double-click to add one, right-click to remove one.
3. **Arrange.** Drag chops onto steps, scroll a step to change its chop, **IN ORDER** to rebuild the loop, **GROOVES** for starting rhythms, banks **A–D**.
4. **Play.** **PLAY** runs the internal clock; **HOST SYNC** follows FL. Click the plugin and play chops on **1–0 / Q–P**, or from C5 up in the Piano Roll.
5. **Flip it.** Choose what RANDOMIZE may change, set CHAOS, roll. **PIN** a step first to Dovetail around it. **UNDO** steps back per bank.
6. **Sound.** Set CHOKE, HIT ALIGN, VINTAGE and DRIVE in the bottom panel.

Full details of every control and what happens under the hood: **[MANUAL.md](MANUAL.md)**.

## Validation (Linux build of the same source)

- Engine and processor tests pass. They now also cover:
  - attack measurement (within 0.5 ms) and Hit Align timing (every hit within 0.6 ms of the grid)
  - CUT / GATE / POLY and note-off
  - all vintage modes stay finite and bounded; 12-bit mode drops signal below its last bit
  - Dovetail: the pinned step never changes, the chain is repeatable per seed, a twin of the pinned chop is placed after it 85% of the time, and chains fit about 5× better than a shuffle; Fit 0 is plain RANDOMIZE
  - disjoint swaps, CHAOS keeping hand-set ratchets, per-bank undo, keyboard hits, PIN and FLAC save/restore
- Full editor repaint is about 6 ms in software; during playback only the playhead strip and the two step cells that changed redraw.

The macOS build and FL Studio scan are still to be checked on the Mac. `.github/workflows/build.yml` builds a universal VST3, AU and Standalone on macOS 15, runs the tests and pluginval, and uploads them with `INSTALL.txt`.

**[Text manual (v0.3)](MANUAL.md)** · [Illustrated manual (PDF, v0.2)](Manual/Epsilon-Chop-Manual.pdf) · [HTML version](Manual/Epsilon-Chop-Manual.html) · [Design and research](DESIGN.md) · Fonts: Instrument Sans and JetBrains Mono, SIL OFL 1.1 (`Resources/licenses`) · [JUCE licence](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
