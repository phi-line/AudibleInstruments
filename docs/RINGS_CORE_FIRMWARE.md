# Rings Core Firmware Documentation

## Overview

The Rings core firmware implements three resonator models for physical modeling synthesis, originally designed for the Mutable Instruments Rings eurorack module. The firmware provides modal synthesis, string synthesis with sympathetic resonance, and non-linear string models, all with polyphonic support.

## Architecture

### Core Components

The firmware is organized into several key components:

1. **`Part`** (`part.h`/`part.cc`) - Main polyphonic voice manager and audio processing coordinator
2. **`Resonator`** (`resonator.h`) - Modal resonator implementation
3. **`String`** (`string.h`) - String synthesis with dispersion and non-linearities
4. **`Strummer`** (`strummer.h`) - Note detection and strumming logic
5. **`StringSynthPart`** (`string_synth_part.h`) - Easter egg string synth mode
6. **`Patch`** (`patch.h`) - Parameter structure
7. **`PerformanceState`** (`performance_state.h`) - Performance/note state structure

### Constants

- **Sample Rate**: Fixed at 48kHz (`kSampleRate = 48000.0f`)
- **Block Size**: 24 samples (`kMaxBlockSize = 24`)
- **Max Polyphony**: 4 voices (`kMaxPolyphony = 4`)
- **Max Strings**: 8 strings (`kNumStrings = kMaxPolyphony * 2`)
- **Max Modes**: 64 modes per resonator (`kMaxModes = 64`)

## Resonator Models

### 1. Modal Resonator (`RESONATOR_MODEL_MODAL`)

Based on the resonator used in Elements, this model uses a bank of modal filters to simulate physical materials.

**Characteristics**:
- Uses `Resonator` class with configurable number of modes
- Resolution scales with polyphony: `64 / polyphony - 4` modes
- Excitation filter processes input before modal synthesis
- Internal exciter: pulse generator triggered on strum
- Outputs split into odd/even partials

**Parameters**:
- `structure` - Controls inharmonicity (0.0-0.9995)
- `brightness` - Spectrum brightness (squared for smoother response)
- `damping` - Decay time (0.0-0.9995)
- `position` - Excitation point on structure

### 2. Sympathetic Strings (`RESONATOR_MODEL_SYMPATHETIC_STRING`)

Models a network of strings that resonate sympathetically with each other.

**Characteristics**:
- Uses multiple `String` objects (2 strings per voice)
- Computes sympathetic string frequencies based on structure parameter
- Structure parameter controls intervals between strings
- Strings can be detuned for realism
- Supports quantized chord mode when structure ≥ 2.0

**String Frequency Calculation**:
- Base frequencies derived from tonic and note
- Structure parameter interpolates between harmonic intervals
- Detuning applied to secondary strings
- Chord quantization available (11 chord types)

**Sympathetic Resonance**:
- Main string (string 0) excites sympathetic strings
- Sympathetic strings have modified brightness/damping/position
- LFO modulation applied to sympathetic string position
- Gain coupling: `0.2 / num_strings` for sympathetic input

### 3. String with Non-linearity (`RESONATOR_MODEL_STRING`)

Comb filter with multimode filter and non-linearities in the feedback loop.

**Characteristics**:
- Uses `String` class with dispersion enabled
- Dispersion calculation:
  - Below 0.24: negative dispersion (stiff string)
  - Above 0.26: positive dispersion (flexible string)
  - Between: no dispersion
- Non-linear processing in feedback loop
- Plucker exciter for internal mode

**Dispersion**:
```cpp
dispersion = structure < 0.24f
    ? (structure - 0.24f) * 4.166f
    : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
```

### 4. FM Voice (`RESONATOR_MODEL_FM_VOICE`)

Frequency modulation synthesis voice.

**Characteristics**:
- Uses `FMVoice` class
- Structure parameter controls FM ratio
- Position parameter controls feedback amount
- Internal envelope triggered on strum

### 5. Quantized Sympathetic Strings (`RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED`)

Variant of sympathetic strings with quantized chord intervals.

**Characteristics**:
- Uses chord table for string frequencies
- 11 chord types available
- Structure parameter selects chord (mapped to `performance_state.chord`)

### 6. String and Reverb (`RESONATOR_MODEL_STRING_AND_REVERB`)

String model with integrated reverb processing.

**Characteristics**:
- Uses string model with reverb post-processing
- Damping parameter affects both string decay and reverb time
- Position parameter controls stereo crossfade
- Reverb parameters:
  - Amount: `0.1 + damping * 0.5`
  - Time: `0.35 + 0.63 * damping`
  - Diffusion: `0.625`
  - LP cutoff: `0.3 + brightness * 0.6`

## Data Structures

### `Patch`

Contains the four main resonator parameters:

```cpp
struct Patch {
  float structure;   // 0.0-0.9995: Inharmonicity or string intervals
  float brightness; // 0.0-1.0: Spectrum brightness
  float damping;    // 0.0-0.9995: Decay time (100ms to 10s)
  float position;   // 0.0-0.9995: Excitation point
};
```

### `PerformanceState`

Contains performance and note information:

```cpp
struct PerformanceState {
  bool strum;              // Strum trigger
  bool internal_exciter;   // Use internal exciter
  bool internal_strum;     // Use internal strum detection
  bool internal_note;      // Use internal note generation
  
  float tonic;             // Base frequency (MIDI note)
  float note;              // Current note (MIDI note)
  float fm;                // Frequency modulation (semitones)
  int32_t chord;           // Chord index (0-10)
};
```

## Core Classes

### `Part` Class

Main polyphonic voice manager and audio processing coordinator.

#### Key Methods

**`Init(uint16_t* reverb_buffer)`**
- Initializes all voice objects
- Sets up excitation filters, pluckers, DC blockers
- Initializes reverb and limiter
- Sets default polyphony (1) and model (Modal)

**`Process(PerformanceState, Patch, in, out, aux, size)`**
- Main audio processing function
- Handles voice allocation and note filtering
- Routes audio through appropriate resonator model
- Manages polyphonic voice mixing
- Applies reverb and limiting

**`ConfigureResonators()`**
- Configures resonator objects based on current model
- Sets resolution for modal resonators
- Initializes strings with appropriate dispersion settings
- Sets up LFO frequencies for string models

**Voice Rendering Methods**:
- `RenderModalVoice()` - Processes modal resonator voice
- `RenderStringVoice()` - Processes string voice (with sympathetic resonance)
- `RenderFMVoice()` - Processes FM synthesis voice

#### Voice Management

- **Note Filtering**: Uses `NoteFilter` to smooth note transitions and prevent glitches
- **Voice Allocation**: Round-robin with special ping-pong pattern for 3-voice mode
- **Active Voice**: Only active voice receives input; others process silence
- **Polyphony Scaling**: Excitation filter cutoff scales with polyphony

#### Excitation Processing

- **Excitation Filter**: Low-pass filter processes input before resonator
- **Filter Cutoff**: Based on brightness parameter, scaled differently for internal vs external exciter
- **Internal Exciter**: Pulse (modal) or noise burst (string) triggered on strum
- **DC Blocker**: Applied to string model input

### `Strummer` Class

Handles automatic strum detection and note change detection.

#### Key Methods

**`Init(float ioi, float sr)`**
- Initializes onset detector
- Sets inhibit timer to prevent double-triggering
- `ioi`: Inter-onset interval (default 0.01s = 10ms)
- `sr`: Sample rate

**`Process(in, size, performance_state)`**
- Detects note changes and audio onsets
- Sets `performance_state.strum` flag
- Implements inhibit timer to prevent rapid retriggering
- Logic:
  - If external note CV: strum on note change (>0.4 semitones)
  - If external exciter: strum on audio onset
  - If both disconnected: no auto-strum

#### Onset Detection

- Uses `OnsetDetector` to detect transients in audio input
- Configurable thresholds and timing
- Inhibit timer prevents double-triggering (4x longer for onset detection)

### `Resonator` Class

Modal resonator implementation using a bank of SVF (State Variable Filter) bandpass filters.

#### Characteristics

- **Modes**: Up to 64 modal filters
- **Frequency**: Set per voice
- **Structure**: Controls mode frequency distribution (inharmonicity)
- **Brightness**: Controls mode amplitude distribution
- **Damping**: Controls mode decay rates
- **Position**: Controls excitation point (which modes are excited)

#### Processing

- Each mode is a bandpass filter tuned to a harmonic or inharmonic frequency
- Modes are summed to create the output
- Odd/even mode splitting for stereo output

### `String` Class

Physical string model with dispersion and non-linearities.

#### Characteristics

- **Dispersion**: Frequency-dependent wave velocity (stiffness)
- **Non-linearity**: Non-linear processing in feedback loop
- **Brightness**: Controls harmonic content
- **Damping**: Controls decay time
- **Position**: Pluck position along string
- **Glide**: Frequency glide for sympathetic strings

#### Processing

- Comb filter with multimode filter in feedback
- Dispersion applied to delay line
- Non-linear waveshaping
- Output split into odd/even harmonics

### `StringSynthPart` Class

Easter egg "Disastrous Peace" string synth mode.

#### Characteristics

- 12 voices arranged in 4 groups (polyphony)
- Each voice has 3 harmonics
- Formant filtering, chorus, ensemble, and reverb effects
- Chord-based voicing
- Envelope-based amplitude control

#### FX Types

- `FX_FORMANT` / `FX_FORMANT_2` - Formant filtering
- `FX_CHORUS` - Chorus effect
- `FX_REVERB` / `FX_REVERB_2` - Reverb effect
- `FX_ENSEMBLE` - Ensemble effect

## Audio Processing Pipeline

### Standard Flow

1. **Input Processing**
   - Input buffered and sample rate converted (if needed)
   - Excitation filter applied (low-pass based on brightness)
   - DC blocking (string models)

2. **Note Processing**
   - Note filter smooths note transitions
   - MIDI note converted to frequency: `SemitonesToRatio(note - 69.0) * a3`
   - Frequency modulation applied

3. **Resonator Processing**
   - Model-specific rendering:
     - Modal: Bank of modal filters
     - String: Comb filter with dispersion
     - FM: FM synthesis
   - Parameters applied (structure, brightness, damping, position)

4. **Polyphonic Mixing**
   - Monophonic: Odd/even partials to separate outputs
   - Polyphonic: Odd/even voices to separate outputs
   - Gain compensation applied

5. **Post-Processing**
   - Reverb (string+reverb model)
   - Limiting (model-specific gains)
   - Output scaling

### Internal Exciter Modes

**Modal Model**:
- Pulse generator triggered on strum
- Pulse amplitude: `0.25 * SemitonesToRatio(cutoff^2 * 24) / cutoff`

**String Model**:
- Noise burst via `Plucker` class
- Triggered on strum with frequency and cutoff parameters
- Burst filtered and added to input

## Polyphony Implementation

### Voice Allocation

- **Round-Robin**: Standard allocation cycles through voices
- **Ping-Pong Pattern**: For 3-voice mode, uses pattern `[1, 0, 2, 1, 0, 2, 1, 0]`
- **Note Assignment**: New notes assigned to `active_voice_` on strum
- **Voice Stealing**: Oldest voice is reused when all voices active

### Note Management

- Each voice maintains its own `note_[voice]` value
- Note filter prevents glitches during transitions
- Stable note assigned on strum trigger
- Active voice follows note filter output

### Output Routing

**Monophonic Mode (1 voice)**:
- `out`: Odd partials/harmonics
- `aux`: Even partials/harmonics

**Polyphonic Mode (2 or 4 voices)**:
- `out`: Odd-numbered voices (1, 3, ...)
- `aux`: Even-numbered voices (2, 4, ...)
- Each voice output: `out_buffer - aux_buffer` (difference signal)

## Sympathetic Strings Algorithm

### Frequency Calculation

For sympathetic strings model, frequencies are computed based on:
- Tonic frequency
- Main note frequency
- Structure parameter (interpolates between intervals)

### Interval Table

Base intervals (in semitones from tonic):
- `0.0` (unison)
- `-12.0` (octave below)
- `-7.01955` (fifth below)
- `+7.01955` (fifth above)
- `+12.0` (octave)
- `+19.01955` (octave + fifth)
- `+24.0` (two octaves)

### Detuning

Secondary strings are detuned by small amounts:
- `0.013`, `0.011`, `0.007`, `0.017` semitones (cycled)

### Chord Quantization

When structure ≥ 2.0, quantized chords are used:
- 11 chord types available
- Structure parameter maps to chord index
- Chord table defines string frequencies relative to note

## Performance Optimizations

### Block Processing

- Fixed 24-sample block size at 48kHz
- Reduces function call overhead
- Enables SIMD optimizations

### Conditional Processing

- Inactive voices process silence (no input)
- Excitation filter cutoff minimized for inactive voices
- Model configuration only when changed (`dirty_` flag)

### Gain Compensation

- Model-specific output gains prevent clipping
- Sympathetic string coupling gain: `0.2 / num_strings`
- Limiter prevents overshoot

## Mathematical Details

### Frequency Conversion

- MIDI note to frequency: `f = a3 * 2^((note - 69) / 12)`
- Where `a3 = 440.0 / 48000.0` (normalized A3)

### Parameter Curves

- **Brightness (modal)**: Squared for smoother response
- **Structure (CV)**: Quadratic bipolar curve
- **Frequency (CV)**: Quartic bipolar curve (for FM)
- **Damping**: Linear 0.0-0.9995 maps to 100ms-10s decay

### Squash Function

Non-linear mapping used for structure interpolation:
- Below 0.5: `x^32` curve
- Above 0.5: `1 - (1-x)^32` curve
- Creates smooth interpolation with emphasis at extremes

## Reverb Implementation

- Shared 32KB buffer (`uint16_t[32768]`)
- Used by both `Part` and `StringSynthPart`
- Parameters controlled by damping and brightness
- Stereo processing with crossfade

## Limiter

- Prevents clipping on string outputs
- Model-specific gain compensation
- Applied after all processing

## Easter Egg: Disastrous Peace

String synth mode accessible via context menu:
- Switches from `Part` to `StringSynthPart`
- Uses resonator model parameter to select FX type
- 12-voice polyphonic string synthesizer
- Formant filtering, chorus, ensemble, and reverb effects
