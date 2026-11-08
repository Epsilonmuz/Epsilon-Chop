# Epsilon Chop

**Epsilon Sounds · v0.1.0 · VST3 sample instrument**

Load a sample, cut it into playable chops, and find a new rhythm without leaving FL Studio. **Random** changes which chop plays on each hit. **Move & Trim** changes where the hits land and shortens their tails. Four banks keep different patterns ready to play.

![Epsilon Chop editor showing the waveform, chop markers and Jersey Bounce pattern](preview.png)

[Download Epsilon Chop v0.1.0 for Projucer](Epsilon-Chop-v0.1.0-Projucer.zip)

## Build and install on a 2019 Intel Mac

This source package includes `Epsilon-Chop.jucer`, the generated Xcode project, C++ source, and JUCE 8.0.17-security modules. The Xcode target is **x86_64 VST3**, with a macOS 11.0 deployment target. The Linux VST3 built and passed the included tests. Building and scanning the macOS VST3 are the remaining host checks.

1. Extract the ZIP and keep the `JUCE`, `JuceLibraryCode`, `Source`, and `Builds` folders together.
2. Open `Builds/MacOSX/Epsilon Chop.xcodeproj` in Xcode. Select **Epsilon Chop - VST3**, choose **My Mac**, and use **Product → Build**. To edit the project configuration, open `Epsilon-Chop.jucer` in JUCE 8 Projucer and save the Xcode exporter again.
3. The Xcode target copies `Epsilon Chop.vst3` into `~/Library/Audio/Plug-Ins/VST3`. The build output is also under `Builds/MacOSX/build/Debug` or `Release`, depending on the build configuration.
4. In FL Studio, choose **Options → Manage plugins**, enable **Verify plugins**, and click **Find installed plugins**. Search for **Epsilon Chop**. Add it as a generator in the **Channel Rack**. [Image-Line plug-in installation guide](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)

Build the Release target from Terminal while inside the extracted folder.

```sh
xcodebuild -project 'Builds/MacOSX/Epsilon Chop.xcodeproj' \
  -target 'Epsilon Chop - VST3' -configuration Release build
```

## First session

1. Click **Load Sample** or drop a file onto the editor. Use a mono or stereo WAV, AIFF, FLAC or MP3 at 8–192 kHz. The limit is the smaller of 120 seconds and 8 million frames.
2. Start with **Grid / 8 chops** for a cleanly trimmed loop. Try **Transient** for drums, consonants, or a sample with uneven attacks. Click the colored regions to hear them.
3. Choose **B** for Jersey Bounce, turn on **Play Pattern**, and press Play in FL Studio. The highlighted step follows the host. You can also play the chops directly in the Piano Roll from **MIDI note 36** upward.
4. Try **Random** to swap the chops on the same hits. Try **Move & Trim** to shift the hits and tighten the gaps. **Undo** backs out the last edit.

## Controls

| Control | What it does |
| --- | --- |
| **Load Sample** | Imports one local audio file. The editor shows its name, length, sample rate and chop count. |
| **Grid / Transient / Manual** | Grid divides the file equally. Transient picks strong attacks and uses a grid when it finds none. Manual lets you edit the boundaries directly. On first entry it takes the current cuts as its starting point. |
| **4 / 8 / 16 / 32 chops** | Sets the number of grid chops or the maximum number in Transient mode. Manual uses the markers you place. |
| **Waveform** | Click a region to preview and select its chop. In Manual, double-click open waveform space to add a boundary, drag an internal boundary to move it, or right-click a boundary to remove it. The playhead shows where the current chop is reading. |
| **Stop Preview** | Fades out the waveform audition. |
| **Output** | Sets the instrument level from −24 to +6 dB. Default is −3 dB. |
| **Play Pattern** | Loops the current 16-step pattern in sixteenth notes on the FL Studio transport. One cycle spans four quarter notes. Leave it off to arrange chops directly in the Piano Roll. |
| **A / B / C / D** | Selects a pattern bank. Each bank has its own chop assignments, rests, and gates. The waveform and its markers are shared by the four banks. |
| **Load Groove** | Replaces the selected bank with Straight, Jersey Bounce, Drill Space, or Dembow Pocket. These are starting rhythms you can edit. |
| **Step row / Chop picker** | Click a step to edit it, then choose a numbered chop or **Rest**. Right-click or Shift-click a step to toggle between a rest and the selected waveform chop. The small bar shows the gate. |
| **Gate** | Sets the selected hit to 10–100% of one step. A shorter chop finishes at its own end. A 50% gate is about 62.5 ms at 120 BPM. |
| **Random** | Picks new chop assignments for occupied steps and keeps their timing, rests and gates. Requires at least two chops and one occupied step. |
| **Move & Trim** | Moves hits into a new set of step positions, keeps their chop order, and caps gates at 25%, 50%, or 75% per hit. When all 16 steps are filled it rotates the sequence. Repeated presses can shorten gates further. |
| **Undo** | Steps back through up to 24 pattern or marker edits since the current sample was loaded. |

## Make a variation

- **Jersey bounce** — Try Bank B with a vocal or keys loop. Listen to the late double hit on steps 11 and 12. Press **Random** to change the phrase on that rhythm.
- **Drill pocket** — Try Bank C with a sustained texture. Press **Move & Trim**, then raise the gate of a hit you want to ring into a gap.
- **Dembow fill** — Keep a main pattern in A and audition D as a turnaround. For an exact bar-by-bar arrangement, turn off **Play Pattern** and write the chop notes in FL Studio’s Piano Roll. Chop 01 is MIDI note 36, Chop 02 is 37, and so on through the available chops.

Playback follows the host grid at the original sample pitch and speed. A new chop chokes the previous one with a short fade. The original audio file stays intact, and each instance embeds its decoded sample, markers and banks in its saved plug-in state. Long stereo samples can add up to about 64 MB per instance to the project state.

[Design, research sources and validation](DESIGN.md) · [JUCE 8 license and included notices](https://github.com/juce-framework/JUCE/blob/8.0.17-security/LICENSE.md)
