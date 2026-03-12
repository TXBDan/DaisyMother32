# DaisyMother32 — Moog Mother-32 Emulator for Daisy Pod

A synthesizer emulating the Moog Mother-32 signal chain, built for the Electro-Smith Daisy Pod.

## Signal Chain

```
VCO1 ─┐
VCO2 ─┼─ Mixer → VCF (Moog Ladder) → VCA → Output
Noise─┘              ↑                  ↑
               EG / LFO mod          ADSR
```

---

## Building & Flashing

```bash
make
# Flash build/TestSynth.bin to the Daisy Pod using the DFU bootloader
```

---

## Controls

### Encoder
- **Press** — cycle selection: VCO1 → VCO2 → LFO → VCO1 …
- **Rotate** — cycle waveforms for the selected target

### Waveforms & LED Colours

| Target | Waveform | LED Colour |
|--------|----------|------------|
| VCO1 | Sawtooth | Cyan |
| VCO1 | Square | Blue |
| VCO2 | Sawtooth | Yellow |
| VCO2 | Square | Red |
| LFO | Triangle | Purple |
| LFO | Square | White |
| LFO | Sawtooth | Orange |
| LFO | Sine | Green |

### Knobs
- **Knob 1** — VCO1 pitch ±1 octave (centre = no offset)
- **Knob 2** — VCO2 pitch ±1 octave (centre = unison with VCO1)

### Buttons
- **Button 1** — Gate (hold = note on, release = note off). Plays C4 when no MIDI note is active.
- **Button 2** — Toggle Hold mode. Gate stays open after releasing. LED 2 turns blue.

### LEDs
- **LED 1** — Colour indicates the currently selected target and its active waveform (see table above).
- **LED 2** — Green brightness tracks the envelope level in real time. Solid blue when Hold mode is active.

---

## MIDI

Connect a MIDI controller to the Daisy Pod USB or TRS MIDI input.

### Notes
- Note stack (up to 16 notes) with last-note priority.
- Releasing a held note returns to the previously held note (legato style).
- Both VCOs track the same MIDI note; their relative pitch is set by the knobs.
- Velocity scales the VCA level with a square-root response curve for expressive sensitivity at low velocities.

### Knobs / Faders

| CC | Parameter | Range |
|----|-----------|-------|
| CC 1 | VCF Cutoff (mod wheel) | 20 Hz – 20 kHz (log) |
| CC 21 | VCF Resonance | 0 – 0.95 |
| CC 22 | Glide (portamento) | 0 – 2 s (quadratic) |
| CC 23 | VCO1 / VCO2 Blend | 0 = VCO1 only → 127 = VCO2 only |
| CC 24 | LFO Rate | 0.2 – 350 Hz (log) |
| CC 25 | VCO Mod Amount | 0 – 1 |
| CC 26 | VCF Mod Amount | 0 – 1 |
| CC 27 | Envelope Attack | 0 – 5 s (quadratic) |
| CC 28 | Envelope Decay | 0 – 5 s (quadratic) |

### Switches (toggle buttons, 0 or 127)

| CC | Switch | 0 | 127 |
|----|--------|---|-----|
| CC 44 | VCO Mod Source | EG | LFO |
| CC 45 | VCO Mod Destination | Pulse Width | Frequency |
| CC 46 | VCF Mod Source | LFO | EG |
| CC 64 | Sustain Pedal | Off | On |

---

## Synthesis Parameters

### VCO1 & VCO2
- Independent waveforms: Polyblep Sawtooth or Square (selected via encoder)
- Both VCOs track the same MIDI note
- VCO1 pitch offset: Knob 1 (±1 octave)
- VCO2 pitch offset: Knob 2 (±1 octave, centre = unison)
- Blend between VCO1 and VCO2: CC 23
- Glide (portamento): CC 22, 0–2 s quadratic taper
- Pulse width: 0.5 default, modulatable via CC 45

### VCO Modulation
- **Amount:** CC 25
- **Source (CC 44):** LFO or EG
- **Destination (CC 45):** Frequency (vibrato / FM) or Pulse Width (PWM)
- Modulation applies to both VCO1 and VCO2

### VCF — Moog Ladder Filter
- Cutoff: 20 Hz – 20 kHz (CC 1 / mod wheel)
- Resonance: 0–0.95 (CC 21)
- Keyboard tracking: cutoff rises with note pitch relative to C4
- **VCF Mod Source (CC 46):** LFO or EG
  - LFO: sweeps ±9990 Hz around base cutoff
  - EG: opens filter from base cutoff up toward 20 kHz
- Mod depth: CC 26

### ADSR Envelope
- Controls both VCF mod depth and VCA amplitude
- Attack / Decay: CC 27 / CC 28 (0–5 s, quadratic taper)
- Sustain / Release: fixed defaults (0.7 / 0.5 s)
- VCA level is additionally scaled by MIDI velocity (square-root curve)

### LFO
- Waveforms: Triangle, Square, Sawtooth, Sine (selected via encoder)
- Rate: 0.2–350 Hz (CC 24) — crosses into audio-rate FM at the top end
- Routable to VCO pitch/PW (CC 44/45) or VCF cutoff (CC 46)

### Noise
- White noise mixed with VCO signal before the filter
- Level: `noise_level` variable (default 0, no CC assigned)
