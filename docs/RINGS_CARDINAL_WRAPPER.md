# Rings Cardinal Wrapper Documentation

## Overview

The Cardinal wrapper (`Rings.cpp`) adapts the original Mutable Instruments Rings firmware for use in the Cardinal VST plugin environment. It provides a Rack module interface that bridges Cardinal's audio system with the Rings DSP engine, handling sample rate conversion, parameter mapping, and polyphonic voice management.

## Module Structure

### Class: `Rings : Module`

The main module class that wraps the Rings DSP engine for Cardinal.

#### Parameter IDs (`ParamIds`)

- **`POLYPHONY_PARAM`** - Button to cycle through polyphony modes (1, 2, 4 voices)
- **`RESONATOR_PARAM`** - Button to cycle through resonator models
- **`FREQUENCY_PARAM`** - Main frequency control (0.0-60.0 semitones, default 30.0)
- **`STRUCTURE_PARAM`** - Structure parameter (0.0-1.0, default 0.5)
- **`BRIGHTNESS_PARAM`** - Brightness parameter (0.0-1.0, default 0.5)
- **`DAMPING_PARAM`** - Damping parameter (0.0-1.0, default 0.5)
- **`POSITION_PARAM`** - Position parameter (0.0-1.0, default 0.5)
- **`BRIGHTNESS_MOD_PARAM`** - Brightness CV attenuverter (-1.0 to 1.0)
- **`FREQUENCY_MOD_PARAM`** - Frequency CV attenuverter (-1.0 to 1.0)
- **`DAMPING_MOD_PARAM`** - Damping CV attenuverter (-1.0 to 1.0)
- **`STRUCTURE_MOD_PARAM`** - Structure CV attenuverter (-1.0 to 1.0)
- **`POSITION_MOD_PARAM`** - Position CV attenuverter (-1.0 to 1.0)

#### Input IDs (`InputIds`)

- **`BRIGHTNESS_MOD_INPUT`** - CV input for brightness modulation
- **`FREQUENCY_MOD_INPUT`** - CV input for frequency modulation (FM)
- **`DAMPING_MOD_INPUT`** - CV input for damping modulation
- **`STRUCTURE_MOD_INPUT`** - CV input for structure modulation
- **`POSITION_MOD_INPUT`** - CV input for position modulation
- **`STRUM_INPUT`** - Trigger/gate input for strumming
- **`PITCH_INPUT`** - 1V/oct pitch CV input
- **`IN_INPUT`** - Audio input (accepts up to 16Vpp modular levels)

#### Output IDs (`OutputIds`)

- **`ODD_OUTPUT`** - Odd partials output (mono) or odd voices output (poly)
- **`EVEN_OUTPUT`** - Even partials output (mono) or even voices output (poly)

#### Light IDs (`LightIds`)

- **`POLYPHONY_GREEN_LIGHT`** / **`POLYPHONY_RED_LIGHT`** - Indicates polyphony mode:
  - Green only: 1 voice
  - Both: 2 voices
  - Red only: 4 voices
- **`RESONATOR_GREEN_LIGHT`** / **`RESONATOR_RED_LIGHT`** - Indicates resonator model

## Key Components

### Sample Rate Conversion

The wrapper handles sample rate conversion between Cardinal's variable sample rate and the Rings DSP engine's fixed 48kHz internal rate:

- **`inputSrc`** - `dsp::SampleRateConverter<1>` converts input from Cardinal's sample rate to 48kHz
- **`outputSrc`** - `dsp::SampleRateConverter<2>` converts output from 48kHz back to Cardinal's sample rate
- **`inputBuffer`** - `DoubleRingBuffer<dsp::Frame<1>, 256>` buffers input samples
- **`outputBuffer`** - `DoubleRingBuffer<dsp::Frame<2>, 256>` buffers output samples

### DSP Engine Objects

- **`part`** - `rings::Part` - Main resonator processing engine
- **`string_synth`** - `rings::StringSynthPart` - String synth easter egg mode
- **`strummer`** - `rings::Strummer` - Handles strumming logic and note detection
- **`reverb_buffer`** - Shared reverb buffer (32KB) for both engines

### State Management

- **`polyphonyMode`** - Current polyphony setting (0=1 voice, 1=2 voices, 2=4 voices)
- **`resonatorModel`** - Current resonator model (`rings::ResonatorModel`)
- **`easterEgg`** - Boolean flag for "Disastrous Peace" string synth mode
- **`strum`** / **`lastStrum`** - Strum trigger state tracking
- **`polyphonyTrigger`** / **`modelTrigger`** - Schmitt triggers for button presses

## Audio Processing Flow

### Input Processing (`process()` method)

1. **Input Buffering**: Audio input is normalized from ±5V to ±1.0 and buffered
   ```cpp
   f.samples[0] = inputs[IN_INPUT].getVoltage() / 5.0;
   ```

2. **Strum Detection**: STRUM input is detected when voltage ≥ 1.0V

3. **Polyphony/Model Switching**: Button presses cycle through modes using Schmitt triggers

4. **Block Processing** (when output buffer is empty):
   - Converts input buffer from Cardinal's sample rate to 48kHz (24-sample blocks)
   - Configures polyphony and resonator model
   - Builds `Patch` structure with CV modulation applied:
     - Uses `quadraticBipolar()` for structure, brightness, damping, position
     - Uses `quarticBipolar()` for frequency modulation
     - CV inputs are scaled by 3.3x and normalized to ±5V range
   - Builds `PerformanceState`:
     - Converts pitch CV to MIDI note (12 semitones per volt)
     - Handles frequency transpose (quantized if pitch input connected)
     - Sets internal exciter/strum/note flags based on input connections
   - Processes audio through DSP engine:
     - Normal mode: `part.Process()` with `strummer.Process()`
     - Easter egg mode: `string_synth.Process()` with `strummer.Process()`
   - Converts output from 48kHz back to Cardinal's sample rate

5. **Output Routing**:
   - If both outputs connected: split odd/even signals
   - If only one output connected: mix both signals together (matching hardware behavior)

### Parameter Modulation

All CV inputs use attenuverters with bipolar ranges:
- **Structure/Brightness/Damping/Position**: `quadraticBipolar()` curve for smooth modulation
- **Frequency**: `quarticBipolar()` curve for FM synthesis
- CV scaling: `3.3 * attenuverter * CV_voltage / 5.0`

### Frequency Control

- **Base frequency**: `FREQUENCY_PARAM` (0-60 semitones, default 30)
- **Pitch CV**: 1V/oct standard, normalized to 1/12V default
- **Transpose quantization**: When pitch input is connected, transpose is rounded to nearest semitone
- **FM**: Frequency modulation via `FREQUENCY_MOD_INPUT` (±48 semitones range)

## Polyphony Management

The module supports 1, 2, or 4 voice polyphony:
- Polyphony is set via `part.set_polyphony(polyphony)` where `polyphony = 1 << polyphonyMode`
- Voice allocation uses round-robin with special ping-pong pattern for 3-voice mode
- Each voice maintains its own note state in `note_[voice]` array

## Resonator Models

The module supports 6 resonator models (3 standard + 3 bonus):

1. **Modal Resonator** (`RESONATOR_MODEL_MODAL`) - As used in Elements
2. **Sympathetic Strings** (`RESONATOR_MODEL_SYMPATHETIC_STRING`) - Network of comb filters
3. **String with Non-linearity** (`RESONATOR_MODEL_STRING`) - Comb filter with multimode filter and non-linearities
4. **FM Voice** (`RESONATOR_MODEL_FM_VOICE`) - Bonus model
5. **Quantized Sympathetic Strings** (`RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED`) - Bonus model
6. **String and Reverb** (`RESONATOR_MODEL_STRING_AND_REVERB`) - Bonus model

Model switching is done via button press or context menu.

## Easter Egg Mode

"Disastrous Peace" mode (`easterEgg` flag):
- Switches from `part` to `string_synth` processing
- Uses resonator model parameter to select FX type instead
- Available via context menu

## UI Components

### `RingsWidget : ModuleWidget`

- **Panel**: SVG loaded from `res/Rings.svg`
- **Controls**:
  - Two buttons (polyphony, resonator) with LED indicators
  - Two large knobs (frequency, structure)
  - Three small knobs (brightness, damping, position)
  - Five trim pots (CV attenuverters)
  - Three inputs (strum, pitch, audio)
  - Two outputs (odd, even)
- **Context Menu**: Allows selection of all 6 resonator models and easter egg toggle

## Serialization

The module saves/loads:
- `polyphony` - Current polyphony mode
- `model` - Current resonator model
- `easterEgg` - Easter egg mode state

## Initialization

- `strummer.Init(0.01, 44100.0 / 24)` - Initializes with 10ms IOI and sample rate
- `part.Init(reverb_buffer)` - Initializes main DSP engine
- `string_synth.Init(reverb_buffer)` - Initializes string synth engine

## Notes

- The DSP engine processes in 24-sample blocks at 48kHz
- Input normalization: Modular levels (±5V) are scaled to ±1.0 for DSP processing
- Output scaling: DSP output (±1.0) is scaled back to ±5V modular levels
- Bypass routing: Audio input is routed to both outputs when bypassed
- The module matches hardware behavior where single output connection mixes both signals
