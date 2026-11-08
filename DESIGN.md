# Epsilon Chop v0.2.0 · Design and research

## What the established choppers do

| Reference | What it offers | What Epsilon Chop takes from it |
| --- | --- | --- |
| [FL Studio Fruity Slicer 2](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Slicer%202.htm) | Dull, Medium and Sharp auto-slicing. Beat-quantised slicing at 1/6–1 beat. A *Random* dump places each slice at a random position and keeps the groove. A *Crazy* dump randomises pitch, pan and velocity. Stutter dumps split slices in 2 or 4. Per-slice reverse and pitch | Transient mode with a Sensitivity control. Beat mode. RANDOMIZE with **Rhythm** that keeps the groove by favouring downbeats. **Pitch**, **Reverse** and **Stutter** (ratchets) as per-step properties the dice can roll |
| [Akai MPC Chop Shop](https://www.audeobox.com/learn/mpc-software/chop-shop-sampling/) | Threshold, Region (4/8/16/32 equal), BPM and Manual chop modes. Snap to zero crossing. Chops map to pads in order | Grid with 4/8/16/32. Zero-X snap on every automatic cut. **IN ORDER**, which rebuilds the loop as recorded |
| [Serato Sample](https://serato.com/sample) | *Find Samples* auto cue points, *Set Random* chops at random points, and a colour-coded waveform. Per-pad reverse, key and time | **Random** chop mode with **RE-CHOP**. Each chop owns a colour that follows it into the arrangement |
| [Ableton Simpler, Slice mode](https://www.musicradar.com/tuition/tech/how-to-chop-beats-with-ableton-simplers-new-slice-mode-632939) | Transient slicing, draggable markers, and slices mapped to MIDI notes. Mono, Poly and Thru playback | Draggable cuts in any mode, which switch to Manual on edit. Mono choke playback with 2 ms fades |
| Logic Quick Sampler, Slice mode | Transient, beat and equal division with editable markers | Same set of modes, and one waveform for all banks |

## Why v0.1.0 felt dead

1. **Nothing ran without the host transport.** The step sequencer needed FL's transport to be playing and Play Pattern to be on. With FL stopped, Random, the grooves and Move & Trim changed only the numbers on screen. There was no preview of the arrangement at all.
2. **MIDI started at note 36.** FL's step sequencer and default Piano Roll note is C5 (60). With 8 chops only notes 36–43 sounded, so a fresh channel was silent.
3. **"Random" only swapped chop numbers** on the existing hits. Every other choice was left alone.
4. **Performance.** The editor repainted the whole window, including a ~1,500-segment waveform path, 30 times a second. The Debug configuration that Xcode uses for Build/Run was -O0.

## Signature: the chop ribbon

Every chop gets a colour from an eight-hue palette (ember, lagoon, saffron, iris, rose, sky, peach, orchid). The chassis stays graphite, so the sample is the only colourful thing on screen. Each of the 16 steps draws the **actual waveform of the chop it plays**:

- repeated once per ratchet
- mirrored when reversed
- faded beyond its gate

When you roll RANDOMIZE, you see the sample re-cut before you hear it. The only accent colour, volt (`#DCFF4F`), is reserved for things that are running: the PLAY button, the playhead and the step being played. That's readable even as a small store thumbnail.

The rest of the visual system:

- **Type:** Instrument Sans for labels, embedded so it looks the same on every Mac, and JetBrains Mono for numbers.
- **Chassis:** a few large rounded panels, with no skeuomorphic hardware.
- **Resizing:** the whole layout scales with a fixed aspect ratio from 75% to 150%.

## Engine

- **Clock.** The clock is host-locked when HOST SYNC is on and FL is playing. Otherwise PLAY runs it from the editor, using FL's tempo when it reports one and the internal tempo when it doesn't. Swing delays every second step by up to 60% of a step. Ratchets subdivide the step and retrigger the chop. Repeat hits are slightly softer.
- **Voices.** Mono choke, with a 2 ms fade on every start, stop and choke. Pitch and Tempo Fit set a per-voice playback ratio. A 32-tap Blackman-windowed sinc resampler handles any ratio and narrows its band-limit when reading faster. Reverse reads from the chop end backwards.
- **Analysis** runs on a worker thread. It is spectral-flux onset detection with a 3-bin maximum filter (Böck and Widmer, DAFx 2013). Up to 256 onsets are kept with their strengths, and Sensitivity picks among them without re-analysing. The loop BPM estimate assumes a power-of-two number of 4/4 beats and picks the length that lands between 80 and 160 BPM. You can override it.
- **Threading.** The audio thread reads an immutable published session through a hazard pointer. It does no allocation, locking or file I/O.
- **State v2** stores:
  - the decoded sample
  - the chop settings and patterns
  - the randomizer settings
  - all parameters

  It ends with an end marker, so a truncated chunk is rejected. v1 chunks from v0.1.0 are still read.
